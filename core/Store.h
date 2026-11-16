/*
 * BDB card database - core
 * Stack storage layer interface (spec v0.2, chapter 17) and the Phase 1
 * implementation backed by a folder of JSON files (chapters 4, 11, 12).
 */
#ifndef BDB_STORE_H
#define BDB_STORE_H

#include <cstdint>
#include <functional>
#include <string>
#include <sys/types.h>
#include <vector>

#include "Model.h"
#include "Status.h"


namespace bdb {


// Byte streams for media (never loaded into memory as a whole).
class InStream {
public:
	virtual					~InStream() {}
	// Returns the number of bytes read, 0 at end, -1 on error.
	virtual	ssize_t			Read(void* buffer, size_t size) = 0;
};


class OutStream {
public:
	virtual					~OutStream() {}
	// Returns false on error.
	virtual	bool			Write(const void* buffer, size_t size) = 0;
};


class FileInStream : public InStream {
public:
							FileInStream(const std::string& path);
	virtual					~FileInStream();
			bool			IsOpen() const { return fFD >= 0; }
	virtual	ssize_t			Read(void* buffer, size_t size);
private:
			int				fFD;
};


class StringInStream : public InStream {
public:
							StringInStream(const std::string& data)
								: fData(data) {}
	virtual	ssize_t			Read(void* buffer, size_t size);
private:
	const	std::string&	fData;
			size_t			fOffset = 0;
};


class StringOutStream : public OutStream {
public:
	virtual	bool			Write(const void* buffer, size_t size);
			std::string		data;
};


enum class DocType {
	kManifest,
	kTemplate,
	kCard
};


struct DocListing {
	std::string	relPath;
	bool		isSymlink = false;
};


class StackStore {
public:
	virtual					~StackStore() {}

	// Path of the stack folder (for display and logging only).
	virtual	std::string		RootPath() const = 0;

	virtual	Status			ReadManifest(StoredDoc& out) = 0;
	virtual	Status			WriteManifest(StoredDoc& doc) = 0;

	// Lists relative paths (templates/*.rfdt, cards/**.rfdd up to depth 2).
	// Files starting with '.', ending in ".tmp", symlinks and other
	// extensions are skipped (11.4).
	virtual	Status			ListTemplates(std::vector<std::string>& out) = 0;
	virtual	Status			ListCards(std::vector<std::string>& out) = 0;

	// Reads and parses one document; enforces the size limit before
	// reading (5.6, 14) and detects the reserved "BDBE" header (16).
	virtual	Status			ReadDoc(const std::string& relPath, DocType type,
								StoredDoc& out) = 0;

	// Atomic save (chapter 12). `doc.relPath` is the current file; empty
	// for a new document, in which case `desiredRelPath` must not exist.
	// When the title changed, the file is renamed to `desiredRelPath` as
	// a best effort. With `checkConflict`, a changed file on disk makes
	// the call fail with kConflict. Updates relPath and stamp on success.
	virtual	Status			WriteDoc(StoredDoc& doc,
								const std::string& desiredRelPath,
								bool checkConflict) = 0;
	virtual	Status			RemoveDoc(const std::string& relPath) = 0;
	virtual	bool			Exists(const std::string& relPath) const = 0;

	// Media (chapter 9). relPath is built by MediaRelPath().
	virtual	Status			WriteMedia(const std::string& relPath,
								InStream& source, uint64_t& outBytes) = 0;
	virtual	Status			ReadMedia(const std::string& relPath,
								OutStream& sink) = 0;
	// Absolute path of a media file (the folder store only; used by the
	// UI to hand files to the Translation Kit).
	virtual	std::string		MediaPath(const std::string& relPath) const = 0;

	// Small whole files below the stack folder (themes/<id>.rfds). The
	// path must stay inside the folder; the size limit is checked before
	// reading. Writing is atomic (chapter 12).
	virtual	Status			ReadSmallFile(const std::string& relPath,
								size_t limit, std::string& out) = 0;
	virtual	Status			WriteSmallFile(const std::string& relPath,
								const std::string& data) = 0;

	// Cleanup support (chapter 13).
	virtual	Status			ListMedia(std::vector<std::string>& out) = 0;
	virtual	Status			ListTempFiles(std::vector<std::string>& out) = 0;
	virtual	Status			RemoveFile(const std::string& relPath) = 0;

	// Bulk work (cleanup, conversion, renumbering, CSV import) may skip
	// the fsync of every single file and sync once at the end (12.4).
	virtual	void			SetDeferSync(bool defer) = 0;
	virtual	void			SyncAll() = 0;

	// Called after a document was saved successfully (Phase 3 attribute
	// hook, 15). Failures inside the hook must not fail the save.
			std::function<void(const StoredDoc&, DocType)> postSaveHook;
};


class FolderStore : public StackStore {
public:
	// `root` is the stack folder (the parent of the .rfdm file).
							FolderStore(const std::string& root);

	// Locates the stack folder for a .rfdm path or a folder path.
	static	Status			ResolveRoot(const std::string& path,
								std::string& outRoot);

	virtual	std::string		RootPath() const { return fRoot; }

	virtual	Status			ReadManifest(StoredDoc& out);
	virtual	Status			WriteManifest(StoredDoc& doc);
	virtual	Status			ListTemplates(std::vector<std::string>& out);
	virtual	Status			ListCards(std::vector<std::string>& out);
	virtual	Status			ReadDoc(const std::string& relPath, DocType type,
								StoredDoc& out);
	virtual	Status			WriteDoc(StoredDoc& doc,
								const std::string& desiredRelPath,
								bool checkConflict);
	virtual	Status			RemoveDoc(const std::string& relPath);
	virtual	bool			Exists(const std::string& relPath) const;
	virtual	Status			WriteMedia(const std::string& relPath,
								InStream& source, uint64_t& outBytes);
	virtual	Status			ReadMedia(const std::string& relPath,
								OutStream& sink);
	virtual	std::string		MediaPath(const std::string& relPath) const;
	virtual	Status			ReadSmallFile(const std::string& relPath,
								size_t limit, std::string& out);
	virtual	Status			WriteSmallFile(const std::string& relPath,
								const std::string& data);
	virtual	Status			ListMedia(std::vector<std::string>& out);
	virtual	Status			ListTempFiles(std::vector<std::string>& out);
	virtual	Status			RemoveFile(const std::string& relPath);
	virtual	void			SetDeferSync(bool defer) { fDeferSync = defer; }
	virtual	void			SyncAll();

	// How removed documents are disposed of. The UI installs "move to
	// Trash" (UI spec 5.2); the default unlinks the file.
			std::function<Status(const std::string& absPath)> trashFunction;

	// Fault injection for the tests (acceptance tests 8, 9, 27).
	struct Faults {
		bool	failWrite = false;			// fail while writing the tmp
		bool	failTitleRename = false;	// fail step 6 (rename to title)
		// Called between writing the tmp file and the conflict check.
		std::function<void()> beforeCommit;
	};
			Faults			faults;

	static	Status			WriteFileAtomic(const std::string& path,
								const std::string& data);

private:
			std::string		_Abs(const std::string& relPath) const;
			Status			_FindManifest(std::string& outRelPath) const;
			Status			_AtomicWrite(const std::string& relPath,
								const std::string& data,
								const FileStamp* expected);
			std::string		fRoot;
			std::string		fManifestRelPath;
			bool			fDeferSync = false;
};


// Reads a file's change stamp (lstat). Returns invalid when missing.
FileStamp StatFile(const std::string& absPath);
// mkdir -p for a path below an existing folder.
Status MakeDirectories(const std::string& absPath);
// Reads a whole regular file (no symlinks) of at most `limit` bytes.
Status ReadWholeFile(const std::string& absPath, size_t limit,
	std::string& out);


}	// namespace bdb

#endif	// BDB_STORE_H
