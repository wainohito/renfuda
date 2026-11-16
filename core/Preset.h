/*
 * BDB card database - core
 * New stacks: empty stack (spec v0.4, 19.1), presets (19.2) and sample
 * copies (UI spec 3.5).
 */
#ifndef BDB_PRESET_H
#define BDB_PRESET_H

#include <cstdint>
#include <string>
#include <vector>

#include "Log.h"
#include "Status.h"


namespace bdb {


struct PresetInfo {
	std::string	path;			// the preset's .rfdf folder
	std::string	title;			// manifest title
	std::string	description;	// manifest "preset_description" (optional)
};


// Lists the *.rfdf folders inside `dir` that have a readable manifest.
Status ListPresets(const std::string& dir, std::vector<PresetInfo>& out);

// Card size and paper of a new empty stack (19.1, UI spec 3.4). An empty
// paperSize means no "paper" key (the screen standard 512x342).
struct NewStackOptions {
	int32_t		width = 512;
	int32_t		height = 342;
	std::string	paperSize;			// "", "A4", ..., "postcard", "custom"
	std::string	orientation = "portrait";
};

// `destDir` is the new stack folder (normally "<name>.rfdf"); it must not
// exist. The stack is built in a hidden folder next to it and renamed into
// place, so a failure leaves nothing behind (19.4). `outManifestPath`
// receives the path of the new .rfdm file. The stack has one empty
// template and no cards (19.1).
Status CreateEmptyStack(const std::string& destDir, const std::string& title,
	LogSink* sink, std::string* outManifestPath,
	const NewStackOptions* options = nullptr);

// Copies a preset with fresh IDs (19.2). The preset is only read. When the
// preset has no cards and `ensureCard` is set, one empty card is added
// (normally not: the UI opens a provisional card, 8.6).
Status CreateStackFromPreset(const std::string& presetDir,
	const std::string& destDir, const std::string& title,
	const std::string& presetName, bool ensureCard, LogSink* sink,
	std::string* outManifestPath);

// A stack from a single .rfdt file (UI spec 3.1, dropping a template).
Status CreateStackFromTemplateFile(const std::string& templatePath,
	const std::string& destDir, const std::string& title, LogSink* sink,
	std::string* outManifestPath);

// Copies a whole stack folder as it is (IDs kept), through a hidden staging
// folder (sample stacks, UI spec 3.5). Symlinks are not followed.
Status CopyStackFolder(const std::string& sourceDir,
	const std::string& destDir);

// Helpers shared with the CSV import.
std::string ParentDirectory(const std::string& path);
void RemoveTree(const std::string& path);
// mkdtemp of "<parent of destDir>/.rfd-new-XXXXXX" (19.4).
Status MakeStagingDirectory(const std::string& destDir,
	std::string& outStaging);


}	// namespace bdb

#endif	// BDB_PRESET_H
