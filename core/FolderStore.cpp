/*
 * BDB card database - core
 * Folder + JSON file implementation of StackStore.
 */
#include "Store.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "Limits.h"


namespace bdb {


static const char* const kMediaKinds[] = { "image", "audio", "video" };


static bool
EndsWith(const std::string& text, const char* suffix)
{
	size_t length = strlen(suffix);
	return text.size() >= length
		&& text.compare(text.size() - length, length, suffix) == 0;
}


// #pragma mark - streams


ssize_t
StringInStream::Read(void* buffer, size_t size)
{
	size_t count = std::min(size, fData.size() - fOffset);
	memcpy(buffer, fData.data() + fOffset, count);
	fOffset += count;
	return ssize_t(count);
}



FileInStream::FileInStream(const std::string& path)
	:
	fFD(open(path.c_str(), O_RDONLY | O_NOFOLLOW))
{
}


FileInStream::~FileInStream()
{
	if (fFD >= 0)
		close(fFD);
}


ssize_t
FileInStream::Read(void* buffer, size_t size)
{
	if (fFD < 0)
		return -1;
	while (true) {
		ssize_t result = read(fFD, buffer, size);
		if (result < 0 && errno == EINTR)
			continue;
		return result;
	}
}


bool
StringOutStream::Write(const void* buffer, size_t size)
{
	data.append(static_cast<const char*>(buffer), size);
	return true;
}


// #pragma mark - helpers


FileStamp
StatFile(const std::string& absPath)
{
	FileStamp stamp;
	struct stat st;
	if (lstat(absPath.c_str(), &st) != 0)
		return stamp;
	stamp.valid = true;
	stamp.size = int64_t(st.st_size);
	stamp.mtimeNs = int64_t(st.st_mtim.tv_sec) * 1000000000LL
		+ st.st_mtim.tv_nsec;
	return stamp;
}


Status
MakeDirectories(const std::string& absPath)
{
	std::string current;
	size_t position = 0;
	while (position <= absPath.size()) {
		size_t next = absPath.find('/', position);
		if (next == std::string::npos)
			next = absPath.size();
		current = absPath.substr(0, next);
		position = next + 1;
		if (current.empty())
			continue;
		struct stat st;
		if (lstat(current.c_str(), &st) == 0) {
			if (!S_ISDIR(st.st_mode))
				return Status(ErrorKind::kWriteFailed, "not a directory");
			continue;
		}
		if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST)
			return Status(ErrorKind::kWriteFailed, strerror(errno));
	}
	return Status::Ok();
}


static bool
WriteAll(int fd, const char* data, size_t size)
{
	while (size > 0) {
		ssize_t written = write(fd, data, size);
		if (written < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		data += written;
		size -= size_t(written);
	}
	return true;
}


// Lists plain files of a folder (no symlinks, no dot files, no .tmp) and
// optionally sub folders.
static void
ListFolder(const std::string& absDir, const std::string& relDir,
	const char* extension, int depth, std::vector<std::string>& out)
{
	DIR* dir = opendir(absDir.c_str());
	if (dir == nullptr)
		return;
	std::vector<std::string> subdirs;
	while (struct dirent* entry = readdir(dir)) {
		std::string name = entry->d_name;
		if (name.empty() || name[0] == '.')
			continue;
		std::string absPath = absDir + "/" + name;
		struct stat st;
		if (lstat(absPath.c_str(), &st) != 0)
			continue;
		if (S_ISLNK(st.st_mode))
			continue;
		if (S_ISDIR(st.st_mode)) {
			if (depth > 0)
				subdirs.push_back(name);
			continue;
		}
		if (!S_ISREG(st.st_mode))
			continue;
		if (EndsWith(name, ".tmp"))
			continue;
		if (extension != nullptr && !EndsWith(name, extension))
			continue;
		out.push_back(relDir + "/" + name);
	}
	closedir(dir);
	for (const std::string& subdir : subdirs) {
		ListFolder(absDir + "/" + subdir, relDir + "/" + subdir, extension,
			depth - 1, out);
	}
}


static void
ListTemp(const std::string& absDir, const std::string& relDir, int depth,
	std::vector<std::string>& out)
{
	DIR* dir = opendir(absDir.c_str());
	if (dir == nullptr)
		return;
	std::vector<std::string> subdirs;
	while (struct dirent* entry = readdir(dir)) {
		std::string name = entry->d_name;
		if (name == "." || name == "..")
			continue;
		std::string absPath = absDir + "/" + name;
		struct stat st;
		if (lstat(absPath.c_str(), &st) != 0 || S_ISLNK(st.st_mode))
			continue;
		if (S_ISDIR(st.st_mode)) {
			if (depth > 0 && name[0] != '.')
				subdirs.push_back(name);
			continue;
		}
		if (S_ISREG(st.st_mode) && EndsWith(name, ".tmp"))
			out.push_back(relDir.empty() ? name : relDir + "/" + name);
	}
	closedir(dir);
	for (const std::string& subdir : subdirs) {
		ListTemp(absDir + "/" + subdir,
			relDir.empty() ? subdir : relDir + "/" + subdir, depth - 1, out);
	}
}


static std::string
ParentOf(const std::string& path)
{
	size_t slash = path.rfind('/');
	if (slash == std::string::npos)
		return "";
	return path.substr(0, slash);
}


// Rejects anything that could leave the stack folder (14).
static bool
IsSafeRelPath(const std::string& relPath)
{
	if (relPath.empty() || relPath[0] == '/')
		return false;
	size_t position = 0;
	while (position <= relPath.size()) {
		size_t next = relPath.find('/', position);
		if (next == std::string::npos)
			next = relPath.size();
		std::string component = relPath.substr(position, next - position);
		if (component.empty() || component == "." || component == "..")
			return false;
		position = next + 1;
	}
	return true;
}


// #pragma mark - FolderStore


FolderStore::FolderStore(const std::string& root)
	:
	fRoot(root)
{
	while (fRoot.size() > 1 && fRoot.back() == '/')
		fRoot.pop_back();
}


Status
FolderStore::ResolveRoot(const std::string& path, std::string& outRoot)
{
	struct stat st;
	if (stat(path.c_str(), &st) != 0)
		return Status(ErrorKind::kNotFound, path);
	if (S_ISDIR(st.st_mode)) {
		outRoot = path;
		return Status::Ok();
	}
	if (S_ISREG(st.st_mode) && EndsWith(path, ".rfdm")) {
		outRoot = ParentOf(path);
		if (outRoot.empty())
			outRoot = ".";
		return Status::Ok();
	}
	return Status(ErrorKind::kInvalidArgument, "not a stack");
}


std::string
FolderStore::_Abs(const std::string& relPath) const
{
	return fRoot + "/" + relPath;
}


Status
FolderStore::_FindManifest(std::string& outRelPath) const
{
	DIR* dir = opendir(fRoot.c_str());
	if (dir == nullptr)
		return Status(ErrorKind::kNotFound, "stack folder");
	std::vector<std::string> found;
	while (struct dirent* entry = readdir(dir)) {
		std::string name = entry->d_name;
		if (name.empty() || name[0] == '.' || !EndsWith(name, ".rfdm"))
			continue;
		struct stat st;
		if (lstat(_Abs(name).c_str(), &st) != 0 || !S_ISREG(st.st_mode))
			continue;
		found.push_back(name);
	}
	closedir(dir);
	if (found.size() != 1) {
		return Status(ErrorKind::kInvalidFormat, found.empty()
			? "no manifest" : "more than one manifest");
	}
	outRelPath = found[0];
	return Status::Ok();
}


Status
FolderStore::ReadManifest(StoredDoc& out)
{
	std::string relPath;
	Status status = _FindManifest(relPath);
	if (!status)
		return status;
	status = ReadDoc(relPath, DocType::kManifest, out);
	if (status)
		fManifestRelPath = relPath;
	return status;
}


Status
FolderStore::WriteManifest(StoredDoc& doc)
{
	std::string relPath = doc.relPath.empty() ? fManifestRelPath : doc.relPath;
	if (relPath.empty())
		return Status(ErrorKind::kInvalidArgument, "manifest path");
	std::string desired = relPath;
	if (doc.relPath.empty())
		return WriteDoc(doc, desired, false);
	return WriteDoc(doc, desired, true);
}


Status
FolderStore::ListTemplates(std::vector<std::string>& out)
{
	ListFolder(_Abs("templates"), "templates", ".rfdt", 0, out);
	return Status::Ok();
}


Status
FolderStore::ListCards(std::vector<std::string>& out)
{
	ListFolder(_Abs("cards"), "cards", ".rfdd", kCardScanDepth, out);
	return Status::Ok();
}


Status
FolderStore::ReadDoc(const std::string& relPath, DocType type, StoredDoc& out)
{
	if (!IsSafeRelPath(relPath))
		return Status(ErrorKind::kInvalidArgument, "path");
	size_t limit = kMaxCardBytes;
	if (type == DocType::kManifest)
		limit = kMaxManifestBytes;
	else if (type == DocType::kTemplate)
		limit = kMaxTemplateBytes;

	std::string absPath = _Abs(relPath);
	struct stat st;
	if (lstat(absPath.c_str(), &st) != 0)
		return Status(ErrorKind::kNotFound, relPath);
	if (!S_ISREG(st.st_mode))
		return Status(ErrorKind::kInvalidFormat, "not a regular file");
	if (size_t(st.st_size) > limit)
		return Status(ErrorKind::kLimitExceeded, "file size");

	int fd = open(absPath.c_str(), O_RDONLY | O_NOFOLLOW);
	if (fd < 0)
		return Status(ErrorKind::kReadFailed, strerror(errno));
	std::string data;
	data.resize(size_t(st.st_size));
	size_t total = 0;
	while (total < data.size()) {
		ssize_t result = read(fd, &data[total], data.size() - total);
		if (result < 0 && errno == EINTR)
			continue;
		if (result <= 0)
			break;
		total += size_t(result);
	}
	// Re-check: the file may have grown since the stat.
	char extra;
	bool grown = read(fd, &extra, 1) > 0;
	close(fd);
	if (grown)
		return Status(ErrorKind::kLimitExceeded, "file size");
	data.resize(total);

	// Reserved encrypted container (16).
	if (data.size() >= 4 && data.compare(0, 4, "BDBE") == 0)
		return Status(ErrorKind::kUnsupported, "encrypted file");

	Json doc;
	Status status = Json::Parse(data, doc, kMaxJsonDepth);
	if (!status)
		return status;
	out.relPath = relPath;
	out.stamp.valid = true;
	out.stamp.size = int64_t(st.st_size);
	out.stamp.mtimeNs = int64_t(st.st_mtim.tv_sec) * 1000000000LL
		+ st.st_mtim.tv_nsec;
	out.doc = std::move(doc);
	return Status::Ok();
}


Status
FolderStore::_AtomicWrite(const std::string& relPath, const std::string& data,
	const FileStamp* expected)
{
	std::string absPath = _Abs(relPath);
	std::string tmpPath = absPath + ".tmp";
	int fd = open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW,
		0644);
	if (fd < 0)
		return Status(ErrorKind::kWriteFailed, strerror(errno));

	bool ok;
	if (faults.failWrite) {
		WriteAll(fd, data.data(), data.size() / 2);
		ok = false;
	} else
		ok = WriteAll(fd, data.data(), data.size());
	if (ok && !fDeferSync)
		fsync(fd);
	if (close(fd) != 0)
		ok = false;
	if (!ok) {
		unlink(tmpPath.c_str());
		return Status(ErrorKind::kWriteFailed, "write");
	}

	if (faults.beforeCommit)
		faults.beforeCommit();

	// Minimal conflict detection (12).
	FileStamp current = StatFile(absPath);
	if (expected != nullptr) {
		if (current != *expected) {
			unlink(tmpPath.c_str());
			return Status(ErrorKind::kConflict, relPath);
		}
	}

	if (rename(tmpPath.c_str(), absPath.c_str()) != 0) {
		Status status(ErrorKind::kWriteFailed, strerror(errno));
		unlink(tmpPath.c_str());
		return status;
	}
	return Status::Ok();
}


Status
FolderStore::WriteFileAtomic(const std::string& path, const std::string& data)
{
	FolderStore store(ParentOf(path));
	std::string name = path.substr(path.rfind('/') + 1);
	return store._AtomicWrite(name, data, nullptr);
}


Status
FolderStore::WriteDoc(StoredDoc& doc, const std::string& desiredRelPath,
	bool checkConflict)
{
	std::string data = doc.doc.Dump();
	DocType type = DocType::kCard;
	std::string bdbType = doc.doc.GetString("bdb");
	if (bdbType == "manifest")
		type = DocType::kManifest;
	else if (bdbType == "template")
		type = DocType::kTemplate;

	if (doc.relPath.empty()) {
		// A new document.
		if (!IsSafeRelPath(desiredRelPath))
			return Status(ErrorKind::kInvalidArgument, "path");
		if (Exists(desiredRelPath))
			return Status(ErrorKind::kExists, desiredRelPath);
		std::string parent = ParentOf(desiredRelPath);
		if (!parent.empty()) {
			Status status = MakeDirectories(_Abs(parent));
			if (!status)
				return status;
		}
		// Treat "must not exist" as the expected state.
		FileStamp missing;
		Status status = _AtomicWrite(desiredRelPath, data, &missing);
		if (!status)
			return status.Kind() == ErrorKind::kConflict
				? Status(ErrorKind::kExists, desiredRelPath) : status;
		doc.relPath = desiredRelPath;
	} else {
		if (!IsSafeRelPath(doc.relPath))
			return Status(ErrorKind::kInvalidArgument, "path");
		Status status = _AtomicWrite(doc.relPath, data,
			checkConflict ? &doc.stamp : nullptr);
		if (!status)
			return status;

		// Step 6: best effort rename to the new title.
		if (!desiredRelPath.empty() && desiredRelPath != doc.relPath
			&& IsSafeRelPath(desiredRelPath) && !Exists(desiredRelPath)
			&& !faults.failTitleRename) {
			if (rename(_Abs(doc.relPath).c_str(),
					_Abs(desiredRelPath).c_str()) == 0) {
				doc.relPath = desiredRelPath;
			}
		}
	}
	doc.stamp = StatFile(_Abs(doc.relPath));

	if (postSaveHook)
		postSaveHook(doc, type);
	return Status::Ok();
}


Status
FolderStore::RemoveDoc(const std::string& relPath)
{
	if (!IsSafeRelPath(relPath))
		return Status(ErrorKind::kInvalidArgument, "path");
	std::string absPath = _Abs(relPath);
	if (trashFunction)
		return trashFunction(absPath);
	if (unlink(absPath.c_str()) != 0)
		return Status(ErrorKind::kWriteFailed, strerror(errno));
	return Status::Ok();
}


bool
FolderStore::Exists(const std::string& relPath) const
{
	struct stat st;
	return lstat(_Abs(relPath).c_str(), &st) == 0;
}


Status
FolderStore::WriteMedia(const std::string& relPath, InStream& source,
	uint64_t& outBytes)
{
	if (!IsSafeRelPath(relPath))
		return Status(ErrorKind::kInvalidArgument, "path");
	Status status = MakeDirectories(_Abs(ParentOf(relPath)));
	if (!status)
		return status;

	std::string absPath = _Abs(relPath);
	std::string tmpPath = absPath + ".tmp";
	int fd = open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW,
		0644);
	if (fd < 0)
		return Status(ErrorKind::kWriteFailed, strerror(errno));
	char buffer[64 * 1024];
	uint64_t total = 0;
	bool ok = !faults.failWrite;
	while (ok) {
		ssize_t count = source.Read(buffer, sizeof(buffer));
		if (count < 0) {
			ok = false;
			break;
		}
		if (count == 0)
			break;
		if (!WriteAll(fd, buffer, size_t(count)))
			ok = false;
		total += uint64_t(count);
	}
	if (ok && !fDeferSync)
		fsync(fd);
	if (close(fd) != 0)
		ok = false;
	if (!ok || rename(tmpPath.c_str(), absPath.c_str()) != 0) {
		unlink(tmpPath.c_str());
		return Status(ErrorKind::kWriteFailed, "media");
	}
	outBytes = total;
	return Status::Ok();
}


Status
FolderStore::ReadMedia(const std::string& relPath, OutStream& sink)
{
	if (!IsSafeRelPath(relPath))
		return Status(ErrorKind::kInvalidArgument, "path");
	FileInStream source(_Abs(relPath));
	if (!source.IsOpen())
		return Status(ErrorKind::kNotFound, relPath);
	char buffer[64 * 1024];
	while (true) {
		ssize_t count = source.Read(buffer, sizeof(buffer));
		if (count < 0)
			return Status(ErrorKind::kReadFailed, relPath);
		if (count == 0)
			break;
		if (!sink.Write(buffer, size_t(count)))
			return Status(ErrorKind::kReadFailed, relPath);
	}
	return Status::Ok();
}


std::string
FolderStore::MediaPath(const std::string& relPath) const
{
	if (!IsSafeRelPath(relPath))
		return "";
	return _Abs(relPath);
}


Status
ReadWholeFile(const std::string& absPath, size_t limit, std::string& out)
{
	struct stat st;
	if (lstat(absPath.c_str(), &st) != 0)
		return Status(ErrorKind::kNotFound, absPath);
	if (!S_ISREG(st.st_mode))
		return Status(ErrorKind::kInvalidFormat, "not a regular file");
	if (size_t(st.st_size) > limit)
		return Status(ErrorKind::kLimitExceeded, "file size");
	int fd = open(absPath.c_str(), O_RDONLY | O_NOFOLLOW);
	if (fd < 0)
		return Status(ErrorKind::kReadFailed, strerror(errno));
	std::string data;
	data.resize(size_t(st.st_size));
	size_t total = 0;
	while (total < data.size()) {
		ssize_t result = read(fd, &data[total], data.size() - total);
		if (result < 0 && errno == EINTR)
			continue;
		if (result <= 0)
			break;
		total += size_t(result);
	}
	char extra;
	bool grown = read(fd, &extra, 1) > 0;
	close(fd);
	if (grown)
		return Status(ErrorKind::kLimitExceeded, "file size");
	data.resize(total);
	out = std::move(data);
	return Status::Ok();
}


Status
FolderStore::ReadSmallFile(const std::string& relPath, size_t limit,
	std::string& out)
{
	if (!IsSafeRelPath(relPath))
		return Status(ErrorKind::kInvalidArgument, "path");
	return ReadWholeFile(_Abs(relPath), limit, out);
}


Status
FolderStore::WriteSmallFile(const std::string& relPath,
	const std::string& data)
{
	if (!IsSafeRelPath(relPath))
		return Status(ErrorKind::kInvalidArgument, "path");
	Status status = MakeDirectories(_Abs(ParentOf(relPath)));
	if (!status)
		return status;
	return _AtomicWrite(relPath, data, nullptr);
}


Status
FolderStore::ListMedia(std::vector<std::string>& out)
{
	for (const char* kind : kMediaKinds) {
		std::string relDir = std::string("media/") + kind;
		ListFolder(_Abs(relDir), relDir, nullptr, 0, out);
	}
	return Status::Ok();
}


Status
FolderStore::ListTempFiles(std::vector<std::string>& out)
{
	ListTemp(fRoot, "", 0, out);
	ListTemp(_Abs("templates"), "templates", 0, out);
	ListTemp(_Abs("themes"), "themes", 0, out);
	ListTemp(_Abs("cards"), "cards", kCardScanDepth, out);
	for (const char* kind : kMediaKinds) {
		std::string relDir = std::string("media/") + kind;
		ListTemp(_Abs(relDir), relDir, 0, out);
	}
	return Status::Ok();
}


void
FolderStore::SyncAll()
{
	sync();
}


Status
FolderStore::RemoveFile(const std::string& relPath)
{
	if (!IsSafeRelPath(relPath))
		return Status(ErrorKind::kInvalidArgument, "path");
	if (unlink(_Abs(relPath).c_str()) != 0)
		return Status(ErrorKind::kWriteFailed, strerror(errno));
	return Status::Ok();
}


}	// namespace bdb
