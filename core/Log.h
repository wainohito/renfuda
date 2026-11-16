/*
 * BDB card database - core
 * User action and error log (spec v0.2, chapter 21).
 *
 * Never pass field values, titles, part names or search strings to the
 * logger (21.6). Only IDs, counts and kinds.
 */
#ifndef BDB_LOG_H
#define BDB_LOG_H

#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "Json.h"


namespace bdb {


enum class LogLevel {
	kDebug = 0,
	kInfo,
	kWarn,
	kError
};

const char* LogLevelName(LogLevel level);


class LogSink {
public:
	virtual					~LogSink() {}
	// `stackId` may be empty ("_app"). `line` is one JSON object without
	// the trailing LF. Must be thread safe. Returns false on failure.
	virtual	bool			Write(const std::string& stackId,
								const std::string& line) = 0;
};


// Keeps lines in memory (tests).
class MemoryLogSink : public LogSink {
public:
	virtual	bool			Write(const std::string& stackId,
								const std::string& line);
			std::vector<std::string> Lines() const;
			void			SetFailing(bool failing);
private:
	mutable	std::mutex		fLock;
			std::vector<std::string> fLines;
			bool			fFailing = false;
};


// <baseDir>/<stack_id or _app>/<YYYY-MM>[-n].jsonl (21.2, 21.7)
class FileLogSink : public LogSink {
public:
							FileLogSink(const std::string& baseDir);

	virtual	bool			Write(const std::string& stackId,
								const std::string& line);

	// Removes files older than the retention period and trims each stack
	// folder to the total size limit. Call once at application start.
			void			Prune();
	// For the tests: override the month and the limits.
			void			SetMonthForTesting(const std::string& month)
								{ fMonthOverride = month; }
			void			SetLimitsForTesting(size_t rotateBytes,
								size_t totalBytes)
								{ fRotateBytes = rotateBytes;
									fTotalBytes = totalBytes; }
			void			PruneForTesting(const std::string& nowMonth);

			std::string		BaseDir() const { return fBaseDir; }
			std::string		DirectoryFor(const std::string& stackId) const;

	// Invoked once (per sink) on the first write failure (21.1).
			std::function<void()> onFirstFailure;

private:
			std::string		_CurrentFile(const std::string& dir,
								const std::string& month, size_t lineSize);
			void			_Prune(const std::string& nowMonth);

			std::mutex		fLock;
			std::string		fBaseDir;
			std::string		fMonthOverride;
			size_t			fRotateBytes;
			size_t			fTotalBytes;
			bool			fFailed = false;
};


typedef std::vector<std::pair<std::string, Json>> LogFields;


class Logger {
public:
							Logger(LogSink* sink, const std::string& stackId,
								LogLevel minimum = LogLevel::kInfo);

			void			SetStackId(const std::string& stackId);
			void			SetMinimumLevel(LogLevel level)
								{ fMinimum = level; }
			LogLevel		MinimumLevel() const { return fMinimum; }

			void			Log(LogLevel level, const char* event,
								const LogFields& fields = LogFields());
			void			Info(const char* event,
								const LogFields& fields = LogFields())
								{ Log(LogLevel::kInfo, event, fields); }
			void			Warn(const char* event,
								const LogFields& fields = LogFields())
								{ Log(LogLevel::kWarn, event, fields); }
			void			Error(const char* event,
								const LogFields& fields = LogFields())
								{ Log(LogLevel::kError, event, fields); }
			void			Debug(const char* event,
								const LogFields& fields = LogFields())
								{ Log(LogLevel::kDebug, event, fields); }

private:
			std::mutex		fLock;
			LogSink*		fSink;
			std::string		fStackId;
			LogLevel		fMinimum;
};


}	// namespace bdb

#endif	// BDB_LOG_H
