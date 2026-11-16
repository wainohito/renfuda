/*
 * BDB card database - core
 */
#include "Log.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "Limits.h"
#include "Store.h"
#include "Text.h"


namespace bdb {


const char*
LogLevelName(LogLevel level)
{
	switch (level) {
		case LogLevel::kDebug:	return "debug";
		case LogLevel::kInfo:	return "info";
		case LogLevel::kWarn:	return "warn";
		case LogLevel::kError:	return "error";
	}
	return "info";
}


// #pragma mark - MemoryLogSink


bool
MemoryLogSink::Write(const std::string& stackId, const std::string& line)
{
	(void)stackId;
	std::lock_guard<std::mutex> lock(fLock);
	if (fFailing)
		return false;
	fLines.push_back(line);
	return true;
}


std::vector<std::string>
MemoryLogSink::Lines() const
{
	std::lock_guard<std::mutex> lock(fLock);
	return fLines;
}


void
MemoryLogSink::SetFailing(bool failing)
{
	std::lock_guard<std::mutex> lock(fLock);
	fFailing = failing;
}


// #pragma mark - FileLogSink


FileLogSink::FileLogSink(const std::string& baseDir)
	:
	fBaseDir(baseDir),
	fRotateBytes(kLogRotateBytes),
	fTotalBytes(kLogStackTotalBytes)
{
}


std::string
FileLogSink::DirectoryFor(const std::string& stackId) const
{
	// Only validated IDs become path components (14).
	return fBaseDir + "/" + (stackId.empty() ? std::string("_app") : stackId);
}


std::string
FileLogSink::_CurrentFile(const std::string& dir, const std::string& month,
	size_t lineSize)
{
	for (int index = 1;; index++) {
		std::string path = dir + "/" + month
			+ (index == 1 ? std::string() : "-" + std::to_string(index))
			+ ".jsonl";
		struct stat st;
		if (lstat(path.c_str(), &st) != 0)
			return path;
		if (size_t(st.st_size) + lineSize <= fRotateBytes || st.st_size == 0)
			return path;
	}
}


bool
FileLogSink::Write(const std::string& stackId, const std::string& line)
{
	std::lock_guard<std::mutex> lock(fLock);
	std::string dir = DirectoryFor(stackId);
	std::string month = fMonthOverride.empty() ? CurrentMonth()
		: fMonthOverride;
	bool ok = MakeDirectories(dir).IsOk();
	if (ok) {
		std::string data = line + "\n";
		std::string path = _CurrentFile(dir, month, data.size());
		int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW,
			0644);
		if (fd < 0)
			ok = false;
		else {
			// One write() per event (21.8). No fsync.
			ssize_t written = write(fd, data.data(), data.size());
			ok = written == ssize_t(data.size());
			close(fd);
		}
	}
	if (!ok && !fFailed) {
		fFailed = true;
		if (onFirstFailure)
			onFirstFailure();
	}
	return ok;
}


// Month string "YYYY-MM" -> months since year 0.
static int
MonthIndex(const std::string& month)
{
	if (month.size() < 7 || month[4] != '-')
		return -1;
	int year = atoi(month.substr(0, 4).c_str());
	int number = atoi(month.substr(5, 2).c_str());
	if (number < 1 || number > 12)
		return -1;
	return year * 12 + number - 1;
}


void
FileLogSink::_Prune(const std::string& nowMonth)
{
	int now = MonthIndex(nowMonth);
	DIR* base = opendir(fBaseDir.c_str());
	if (base == nullptr)
		return;
	std::vector<std::string> stacks;
	while (struct dirent* entry = readdir(base)) {
		std::string name = entry->d_name;
		if (name != "." && name != "..")
			stacks.push_back(name);
	}
	closedir(base);

	for (const std::string& stack : stacks) {
		std::string dir = fBaseDir + "/" + stack;
		DIR* handle = opendir(dir.c_str());
		if (handle == nullptr)
			continue;
		struct LogFile {
			std::string	name;
			int			month;
			int			index;
			size_t		size;
		};
		std::vector<LogFile> files;
		while (struct dirent* entry = readdir(handle)) {
			std::string name = entry->d_name;
			if (name.size() < 13 || name.compare(name.size() - 6, 6, ".jsonl"))
				continue;
			int month = MonthIndex(name.substr(0, 7));
			if (month < 0)
				continue;
			int index = 1;
			if (name[7] == '-')
				index = atoi(name.c_str() + 8);
			struct stat st;
			if (lstat((dir + "/" + name).c_str(), &st) != 0
				|| !S_ISREG(st.st_mode)) {
				continue;
			}
			files.push_back({name, month, index, size_t(st.st_size)});
		}
		closedir(handle);

		std::sort(files.begin(), files.end(),
			[](const LogFile& a, const LogFile& b) {
				if (a.month != b.month)
					return a.month < b.month;
				return a.index < b.index;
			});
		size_t total = 0;
		for (const LogFile& file : files)
			total += file.size;
		for (const LogFile& file : files) {
			bool expired = now >= 0 && now - file.month >= kLogRetentionMonths;
			bool tooLarge = total > fTotalBytes;
			if (!expired && !tooLarge)
				break;
			if (unlink((dir + "/" + file.name).c_str()) == 0)
				total -= file.size;
		}
	}
}


void
FileLogSink::Prune()
{
	std::lock_guard<std::mutex> lock(fLock);
	_Prune(CurrentMonth());
}


void
FileLogSink::PruneForTesting(const std::string& nowMonth)
{
	std::lock_guard<std::mutex> lock(fLock);
	_Prune(nowMonth);
}


// #pragma mark - Logger


Logger::Logger(LogSink* sink, const std::string& stackId, LogLevel minimum)
	:
	fSink(sink),
	fStackId(stackId),
	fMinimum(minimum)
{
}


void
Logger::SetStackId(const std::string& stackId)
{
	std::lock_guard<std::mutex> lock(fLock);
	fStackId = stackId;
}


void
Logger::Log(LogLevel level, const char* event, const LogFields& fields)
{
	if (fSink == nullptr || level < fMinimum)
		return;
	std::string stackId;
	{
		std::lock_guard<std::mutex> lock(fLock);
		stackId = fStackId;
	}
	Json line = Json::MakeObject();
	line.Set("t", NowTimestampMillis());
	line.Set("level", LogLevelName(level));
	line.Set("ev", event);
	if (!stackId.empty())
		line.Set("stack", stackId);
	for (const auto& field : fields)
		line.Set(field.first, field.second);
	// Best effort: a failing log never stops the user's operation.
	fSink->Write(stackId, line.DumpCompact());
}


}	// namespace bdb
