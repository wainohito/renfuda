/*
 * BDB card database - core
 */
#include "Preset.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

#include "Ids.h"
#include "Limits.h"
#include "Model.h"
#include "Store.h"
#include "Text.h"


namespace bdb {


std::string
ParentDirectory(const std::string& path)
{
	size_t slash = path.rfind('/');
	if (slash == std::string::npos)
		return ".";
	if (slash == 0)
		return "/";
	return path.substr(0, slash);
}


void
RemoveTree(const std::string& path)
{
	struct stat st;
	if (lstat(path.c_str(), &st) != 0)
		return;
	if (S_ISDIR(st.st_mode)) {
		if (DIR* dir = opendir(path.c_str())) {
			std::vector<std::string> names;
			while (struct dirent* entry = readdir(dir)) {
				std::string name = entry->d_name;
				if (name != "." && name != "..")
					names.push_back(name);
			}
			closedir(dir);
			for (const std::string& name : names)
				RemoveTree(path + "/" + name);
		}
		rmdir(path.c_str());
	} else
		unlink(path.c_str());
}


static Status
CopyFile(const std::string& from, const std::string& to)
{
	FileInStream source(from);
	if (!source.IsOpen())
		return Status(ErrorKind::kNotFound, from);
	int fd = open(to.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (fd < 0)
		return Status(ErrorKind::kWriteFailed, strerror(errno));
	char buffer[64 * 1024];
	bool ok = true;
	while (true) {
		ssize_t count = source.Read(buffer, sizeof(buffer));
		if (count < 0) {
			ok = false;
			break;
		}
		if (count == 0)
			break;
		if (write(fd, buffer, size_t(count)) != count) {
			ok = false;
			break;
		}
	}
	if (ok)
		fsync(fd);
	close(fd);
	if (!ok) {
		unlink(to.c_str());
		return Status(ErrorKind::kWriteFailed, to);
	}
	return Status::Ok();
}


Status
MakeStagingDirectory(const std::string& destDir, std::string& outStaging)
{
	std::string staging = ParentDirectory(destDir) + "/.rfd-new-XXXXXX";
	std::vector<char> buffer(staging.begin(), staging.end());
	buffer.push_back('\0');
	if (mkdtemp(buffer.data()) == nullptr)
		return Status(ErrorKind::kWriteFailed, strerror(errno));
	outStaging = buffer.data();
	chmod(outStaging.c_str(), 0755);
	return Status::Ok();
}


struct StackContent {
	std::vector<Json>	templates;
	std::vector<Json>	cards;
	std::string			defaultTemplate;	// an ID from `templates`
	std::string			mediaRoot;			// where referenced media live
	NewStackOptions		emptyOptions;		// for a stack without templates
};


// Builds a stack from documents with old IDs: assigns new stack, template
// and card IDs, keeps part IDs (19.2).
static Status
BuildStack(const std::string& destDir, const std::string& title,
	StackContent content, const Json* presetName, bool ensureCard,
	LogSink* sink, std::string* outManifestPath)
{
	struct stat st;
	if (lstat(destDir.c_str(), &st) == 0)
		return Status(ErrorKind::kExists, destDir);

	std::string staging;
	Status status = MakeStagingDirectory(destDir, staging);
	if (!status)
		return status;

	FolderStore store(staging);
	std::string now = NowTimestamp();

	// Templates
	std::map<std::string, std::string> templateIds;
	for (Json& doc : content.templates) {
		std::string oldId = doc.GetString("template_id");
		std::string newId = NewId();
		templateIds[oldId] = newId;
		doc.Set("template_id", newId);
		doc.Set("rev", 1);
		doc.Set("created", now);
		doc.Set("modified", now);
		doc.SetAt(0, "bdb", "template");
		doc.SetAt(1, "format_version", kFormatVersion);
	}
	if (content.templates.empty()) {
		const NewStackOptions& options = content.emptyOptions;
		int32_t width = options.width;
		int32_t height = options.height;
		if (width < kMinCardSize || width > kMaxCardSize
			|| height < kMinCardSize || height > kMaxCardSize) {
			RemoveTree(staging);
			return Status(ErrorKind::kInvalidArgument, "size");
		}
		Template empty = Template::Create(kUntitled, width, height);
		if (!options.paperSize.empty()) {
			empty.SetPaper(options.paperSize, options.orientation, width,
				height);
		}
		empty.Doc().Set("created", now);
		empty.Doc().Set("modified", now);
		templateIds[""] = empty.Id();
		content.templates.push_back(empty.Doc());
	}

	// Cards
	for (Json& doc : content.cards) {
		doc.Set("card_id", NewId());
		auto mapped = templateIds.find(doc.GetString("template_id"));
		if (mapped != templateIds.end())
			doc.Set("template_id", mapped->second);
		doc.Set("rev", 1);
		doc.Set("created", now);
		doc.Set("modified", now);
		doc.SetAt(0, "bdb", "card");
		doc.SetAt(1, "format_version", kFormatVersion);
	}

	std::string defaultTemplate;
	auto mapped = templateIds.find(content.defaultTemplate);
	if (mapped != templateIds.end())
		defaultTemplate = mapped->second;
	else
		defaultTemplate = content.templates[0].GetString("template_id");

	if (content.cards.empty() && ensureCard) {
		Card card = Card::Create(defaultTemplate, kSeqStep);
		content.cards.push_back(card.Doc());
	}

	// Themes of the source (stack spec 4: copies kept with the stack).
	if (!content.mediaRoot.empty()) {
		if (DIR* dir = opendir((content.mediaRoot + "/themes").c_str())) {
			std::vector<std::string> names;
			while (struct dirent* entry = readdir(dir)) {
				std::string name = entry->d_name;
				if (name.size() > 5 && name[0] != '.'
					&& name.compare(name.size() - 5, 5, ".rfds") == 0) {
					names.push_back(name);
				}
			}
			closedir(dir);
			for (const std::string& name : names) {
				std::string from = content.mediaRoot + "/themes/" + name;
				if (lstat(from.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
					continue;
				status = MakeDirectories(staging + "/themes");
				if (status)
					status = CopyFile(from, staging + "/themes/" + name);
				if (!status) {
					RemoveTree(staging);
					return status;
				}
			}
		}
	}

	// Media referenced by the copied documents.
	std::set<std::string> media;
	for (const Json& doc : content.templates)
		CollectMediaRefs(doc, media);
	for (const Json& doc : content.cards)
		CollectMediaRefs(doc, media);
	if (!content.mediaRoot.empty()) {
		for (const std::string& relPath : media) {
			std::string from = content.mediaRoot + "/" + relPath;
			std::string to = staging + "/" + relPath;
			if (lstat(from.c_str(), &st) != 0)
				continue;	// missing media stays a dangling reference
			status = MakeDirectories(ParentDirectory(to));
			if (status)
				status = CopyFile(from, to);
			if (!status) {
				RemoveTree(staging);
				return status;
			}
		}
	}

	status = MakeDirectories(staging + "/templates");
	if (status)
		status = MakeDirectories(staging + "/cards");
	if (status)
		status = MakeDirectories(staging + "/media/image");

	for (Json& doc : content.templates) {
		if (!status)
			break;
		StoredDoc stored;
		stored.doc = doc;
		Template tmpl(stored);
		status = store.WriteDoc(stored, "templates/"
			+ MakeFileName(tmpl.Title(), tmpl.Id(), ".rfdt"), false);
	}
	for (Json& doc : content.cards) {
		if (!status)
			break;
		StoredDoc stored;
		stored.doc = doc;
		Card card(stored);
		std::string templateId = card.TemplateId();
		const Template* tmpl = nullptr;
		Template found;
		for (const Json& templateDoc : content.templates) {
			if (templateDoc.GetString("template_id") == templateId) {
				StoredDoc templateStored;
				templateStored.doc = templateDoc;
				found = Template(templateStored);
				tmpl = &found;
				break;
			}
		}
		status = store.WriteDoc(stored, "cards/" + MakeFileName(
			CardTitle(card, tmpl), card.Id(), ".rfdd"), false);
	}

	Manifest manifest = Manifest::Create(title, defaultTemplate);
	std::string manifestName = SanitizeTitle(title) + ".rfdm";
	if (status)
		status = store.WriteDoc(manifest.Stored(), manifestName, false);

	if (status && rename(staging.c_str(), destDir.c_str()) != 0)
		status = Status(ErrorKind::kWriteFailed, strerror(errno));
	if (!status) {
		RemoveTree(staging);
		return status;
	}

	Logger logger(sink, manifest.StackId());
	logger.Info("stack.create", {{"preset", presetName != nullptr
		? *presetName : Json()}});
	if (outManifestPath != nullptr)
		*outManifestPath = destDir + "/" + manifestName;
	return Status::Ok();
}


Status
CreateEmptyStack(const std::string& destDir, const std::string& title,
	LogSink* sink, std::string* outManifestPath,
	const NewStackOptions* options)
{
	StackContent content;
	if (options != nullptr)
		content.emptyOptions = *options;
	return BuildStack(destDir, title, std::move(content), nullptr, false,
		sink, outManifestPath);
}


static Status
ReadPreset(const std::string& presetDir, StackContent& content,
	std::string* outTitle, std::string* outDescription)
{
	std::string root;
	Status status = FolderStore::ResolveRoot(presetDir, root);
	if (!status)
		return status;
	FolderStore store(root);
	StoredDoc manifest;
	status = store.ReadManifest(manifest);
	if (status)
		status = Manifest::Validate(manifest.doc);
	if (!status)
		return status;
	if (outTitle != nullptr)
		*outTitle = manifest.doc.GetString("title");
	if (outDescription != nullptr)
		*outDescription = manifest.doc.GetString("preset_description");
	content.defaultTemplate = manifest.doc.GetString("default_template");
	content.mediaRoot = root;

	std::vector<std::string> paths;
	store.ListTemplates(paths);
	std::sort(paths.begin(), paths.end());
	for (const std::string& relPath : paths) {
		StoredDoc stored;
		status = store.ReadDoc(relPath, DocType::kTemplate, stored);
		if (status)
			status = Template::Validate(stored.doc);
		if (!status)
			return status;
		content.templates.push_back(std::move(stored.doc));
	}
	paths.clear();
	store.ListCards(paths);
	std::sort(paths.begin(), paths.end());
	for (const std::string& relPath : paths) {
		StoredDoc stored;
		status = store.ReadDoc(relPath, DocType::kCard, stored);
		if (status)
			status = Card::Validate(stored.doc);
		if (!status)
			return status;
		content.cards.push_back(std::move(stored.doc));
	}
	return Status::Ok();
}


Status
ListPresets(const std::string& dir, std::vector<PresetInfo>& out)
{
	DIR* handle = opendir(dir.c_str());
	if (handle == nullptr)
		return Status(ErrorKind::kNotFound, dir);
	std::vector<std::string> names;
	while (struct dirent* entry = readdir(handle)) {
		std::string name = entry->d_name;
		if (name.size() > 5 && name[0] != '.'
			&& name.compare(name.size() - 5, 5, ".rfdf") == 0) {
			names.push_back(name);
		}
	}
	closedir(handle);
	std::sort(names.begin(), names.end());
	for (const std::string& name : names) {
		std::string path = dir + "/" + name;
		FolderStore store(path);
		StoredDoc manifest;
		if (!store.ReadManifest(manifest)
			|| !Manifest::Validate(manifest.doc)) {
			continue;
		}
		PresetInfo info;
		info.path = path;
		info.title = manifest.doc.GetString("title");
		info.description = manifest.doc.GetString("preset_description");
		out.push_back(info);
	}
	return Status::Ok();
}


Status
CreateStackFromPreset(const std::string& presetDir, const std::string& destDir,
	const std::string& title, const std::string& presetName, bool ensureCard,
	LogSink* sink, std::string* outManifestPath)
{
	StackContent content;
	Status status = ReadPreset(presetDir, content, nullptr, nullptr);
	if (!status)
		return status;
	Json name(presetName);
	return BuildStack(destDir, title, std::move(content), &name, ensureCard,
		sink, outManifestPath);
}


Status
CreateStackFromTemplateFile(const std::string& templatePath,
	const std::string& destDir, const std::string& title, LogSink* sink,
	std::string* outManifestPath)
{
	std::string parent = ParentDirectory(templatePath);
	std::string name = templatePath.substr(templatePath.rfind('/') + 1);
	FolderStore store(parent);
	StoredDoc stored;
	Status status = store.ReadDoc(name, DocType::kTemplate, stored);
	if (status)
		status = Template::Validate(stored.doc);
	if (!status)
		return status;
	StackContent content;
	content.defaultTemplate = stored.doc.GetString("template_id");
	content.templates.push_back(std::move(stored.doc));
	// Media next to a loose template file are not looked up.
	Json presetName(name);
	return BuildStack(destDir, title, std::move(content), &presetName, false,
		sink, outManifestPath);
}


static Status
CopyTree(const std::string& from, const std::string& to)
{
	struct stat st;
	if (lstat(from.c_str(), &st) != 0)
		return Status(ErrorKind::kNotFound, from);
	if (S_ISLNK(st.st_mode))
		return Status::Ok();
	if (S_ISREG(st.st_mode))
		return CopyFile(from, to);
	if (!S_ISDIR(st.st_mode))
		return Status::Ok();
	if (mkdir(to.c_str(), 0755) != 0 && errno != EEXIST)
		return Status(ErrorKind::kWriteFailed, strerror(errno));
	DIR* dir = opendir(from.c_str());
	if (dir == nullptr)
		return Status(ErrorKind::kReadFailed, from);
	std::vector<std::string> names;
	while (struct dirent* entry = readdir(dir)) {
		std::string name = entry->d_name;
		if (name != "." && name != "..")
			names.push_back(name);
	}
	closedir(dir);
	for (const std::string& name : names) {
		Status status = CopyTree(from + "/" + name, to + "/" + name);
		if (!status)
			return status;
	}
	return Status::Ok();
}


Status
CopyStackFolder(const std::string& sourceDir, const std::string& destDir)
{
	struct stat st;
	if (lstat(destDir.c_str(), &st) == 0)
		return Status(ErrorKind::kExists, destDir);
	Status status = MakeDirectories(ParentDirectory(destDir));
	if (!status)
		return status;
	std::string staging;
	status = MakeStagingDirectory(destDir, staging);
	if (!status)
		return status;
	DIR* dir = opendir(sourceDir.c_str());
	if (dir == nullptr) {
		RemoveTree(staging);
		return Status(ErrorKind::kNotFound, sourceDir);
	}
	std::vector<std::string> names;
	while (struct dirent* entry = readdir(dir)) {
		std::string name = entry->d_name;
		if (name != "." && name != "..")
			names.push_back(name);
	}
	closedir(dir);
	for (const std::string& name : names) {
		status = CopyTree(sourceDir + "/" + name, staging + "/" + name);
		if (!status) {
			RemoveTree(staging);
			return status;
		}
	}
	if (rename(staging.c_str(), destDir.c_str()) != 0) {
		status = Status(ErrorKind::kWriteFailed, strerror(errno));
		RemoveTree(staging);
		return status;
	}
	return Status::Ok();
}


}	// namespace bdb
