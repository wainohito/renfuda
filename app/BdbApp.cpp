/*
 * BDB card database - RenkuOS application
 */
#include "BdbApp.h"

#include <Alert.h>
#include <Directory.h>
#include <Entry.h>
#include <fs_attr.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <Mime.h>
#include <MimeType.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Resources.h>
#include <Roster.h>

#include <sys/stat.h>

#include <algorithm>

#include "Constants.h"
#include "Csv.h"
#include "Limits.h"
#include "CsvWizard.h"
#include "Extras.h"
#include "PaperWindow.h"
#include "Preset.h"
#include "Settings.h"
#include "Stack.h"
#include "StackDocument.h"
#include "StackWindow.h"
#include "StartWindow.h"
#include "Strings.h"


BdbApp::BdbApp()
	:
	BApplication(BDB_APP_SIGNATURE),
	fOpenPanel(NULL),
	fSavePanel(NULL),
	fOpenedFromArgs(false),
	fLogFailureShown(false)
{
	str::InitLanguage();
	bdb::CsvReader::SetLanguage(str::Language());
	gSettings.Load();
	fThemes.SetDirectories(UserThemeDirectory().String(),
		ThemeDirectory().String());
	fLogSink.reset(new bdb::FileLogSink(gSettings.LogDirectory().String()));
	fLogSink->onFirstFailure = [this]() {
		PostMessage(kMsgLogFailed);
	};
	fLogSink->Prune();
	fLogger.reset(new bdb::Logger(fLogSink.get(), ""));
	fLogger->Info("app.start", {{"version", BDB_VERSION},
		{"lang", str::Language()}});
}


BdbApp::~BdbApp()
{
	for (auto& entry : fDocuments) {
		entry.second->GetStack().Log().Info("stack.close");
		delete entry.second;
	}
	fLogger->Info("app.quit", {{"version", BDB_VERSION},
		{"lang", str::Language()}});
	gSettings.Save();
	delete fOpenPanel;
	delete fSavePanel;
}


BString
BdbApp::AppDirectory()
{
	app_info info;
	BPath path;
	if (be_app->GetAppInfo(&info) == B_OK) {
		BEntry entry(&info.ref);
		BPath appPath;
		entry.GetPath(&appPath);
		appPath.GetParent(&path);
	}
	return path.Path() != NULL ? path.Path() : "";
}


BString
BdbApp::PresetDirectory()
{
	// The presets of the UI language (UI 14.1).
	BString path = AppDirectory();
	path << "/presets/" << str::Language();
	return path;
}


BString
BdbApp::SampleDirectory()
{
	BString path = AppDirectory();
	path << "/samples/" << str::Language();
	return path;
}


BString
BdbApp::ThemeDirectory()
{
	BString path = AppDirectory();
	path << "/themes";
	return path;
}


BString
BdbApp::IconDirectory()
{
	BString path = AppDirectory();
	path << "/icons/hvif";
	return path;
}


BString
BdbApp::UserThemeDirectory()
{
	BString path = Settings::SettingsDirectory();
	path << "/themes";
	return path;
}


BString
BdbApp::SampleCopyDirectory()
{
	BPath path;
	if (find_directory(B_USER_DIRECTORY, &path) != B_OK)
		path.SetTo("/boot/home");
	path.Append("Documents/Renfuda/Samples");
	return path.Path();
}


void
BdbApp::_InstallMimeTypes()
{
	struct {
		const char*	type;
		const char*	description;
		const char*	extension;
		const char*	key;
	} types[] = {
		{ BDB_MIME_MANIFEST, str::kMimeManifest, "rfdm", "manifest" },
		{ BDB_MIME_TEMPLATE, str::kMimeTemplate, "rfdt", "template" },
		{ BDB_MIME_CARD, str::kMimeCard, "rfdd", "card" },
		{ BDB_MIME_THEME, str::kMimeTheme, "rfds", "theme" }
	};
	for (const auto& entry : types) {
		BMimeType mime(entry.type);
		if (!mime.IsInstalled())
			mime.Install();
		mime.SetShortDescription(entry.description);
		mime.SetPreferredApp(BDB_APP_SIGNATURE);
		BMessage extensions;
		extensions.AddString("extensions", entry.extension);
		mime.SetFileExtensions(&extensions);
		// The writer puts "bdb" first (spec v0.2, 5.2).
		BString rule;
		rule << "0.80 [0:16] ('\"bdb\": \"" << entry.key << "\"')";
		BString error;
		if (BMimeType::CheckSnifferRule(rule.String(), &error) == B_OK)
			mime.SetSnifferRule(rule.String());
		// The icon of the type (tools/make_icon.py), for Tracker.
		BString name("BEOS:L:");
		name << entry.type;
		size_t size = 0;
		const void* data = AppResources() != NULL
			? AppResources()->LoadResource('VICN', name.String(), &size)
			: NULL;
		if (data != NULL && size > 0)
			mime.SetIcon((const uint8*)data, size);
	}
}


// Stacks made or copied before the icon existed get it too: the stacks
// on the recent list and the copied samples.
void
BdbApp::_SetRecentStackIcons()
{
	for (int32 i = 0; i < gSettings.CountRecent(); i++) {
		BString path = gSettings.RecentAt(i);
		if (path.StartsWith("sample:"))
			continue;
		BPath manifest(path.String());
		BPath folder;
		if (manifest.GetParent(&folder) == B_OK
			&& BString(folder.Leaf()).EndsWith(".rfdf")) {
			_SetStackIcon(folder.Path());
		}
	}
	BDirectory samples(SampleCopyDirectory().String());
	BEntry entry;
	while (samples.GetNextEntry(&entry) == B_OK) {
		BPath path;
		if (entry.IsDirectory() && entry.GetPath(&path) == B_OK
			&& BString(path.Leaf()).EndsWith(".rfdf")) {
			_SetStackIcon(path.Path());
		}
	}
}


// A stack folder gets the stack icon (a folder with slips), so that it is
// told apart from other folders in Tracker. A folder that has an icon of
// its own (or cannot be written) is left alone.
void
BdbApp::_SetStackIcon(const char* folderPath)
{
	// The files of the stack get their types (by extension), so that
	// Tracker shows the manifest, card and template icons. The registrar
	// does it in the background; typed files are left as they are.
	update_mime_info(folderPath, true, false, B_UPDATE_MIME_INFO_NO_FORCE);
	BNode node(folderPath);
	attr_info attr;
	if (node.InitCheck() != B_OK
		|| node.GetAttrInfo("BEOS:ICON", &attr) == B_OK) {
		return;
	}
	size_t size = 0;
	const void* data = AppResources() != NULL
		? AppResources()->LoadResource('VICN', "renfuda:stack", &size) : NULL;
	if (data == NULL || size == 0)
		return;
	BNodeInfo info(&node);
	info.SetIcon((const uint8*)data, size);
}


void
BdbApp::_SeedSamples()
{
	// The bundled samples start on the recent list once per installation
	// (UI 3.5). The install stamp is the application file's time.
	app_info info;
	BString stamp = BDB_VERSION;
	if (GetAppInfo(&info) == B_OK) {
		BEntry entry(&info.ref);
		time_t modified = 0;
		entry.GetModificationTime(&modified);
		stamp << ":" << (int64)modified;
	}
	if (gSettings.SamplesSeeded(stamp))
		return;
	gSettings.SetSamplesSeeded(stamp);
	std::vector<bdb::PresetInfo> samples;
	bdb::ListPresets(SampleDirectory().String(), samples);
	for (const bdb::PresetInfo& sample : samples) {
		BString name = BPath(sample.path.c_str()).Leaf();
		name.RemoveLast(".rfdf");
		if (gSettings.IsSampleRemoved(name))
			continue;
		BString entry("sample:");
		entry << name;
		gSettings.AppendRecent(entry);
	}
	gSettings.Save();
}


// A copy unpacked from the zip has no attributes (made on Linux): give
// the executable the attributes from its resources (the icon for Tracker),
// as mimeset does.
void
BdbApp::_SetOwnIcon()
{
	app_info info;
	if (GetAppInfo(&info) != B_OK)
		return;
	BNode node(&info.ref);
	attr_info attr;
	if (node.InitCheck() != B_OK
		|| node.GetAttrInfo("BEOS:ICON", &attr) == B_OK) {
		return;
	}
	BPath path(&info.ref);
	update_mime_info(path.Path(), false, true,
		B_UPDATE_MIME_INFO_FORCE_KEEP_TYPE);
}


void
BdbApp::ReadyToRun()
{
	RemoveVideoCopies();
	_SetOwnIcon();
	_SetRecentStackIcons();
	_InstallMimeTypes();
	_SeedSamples();
	if (!fOpenedFromArgs && fDocuments.empty())
		_ShowStart();
}


void
BdbApp::ArgvReceived(int32 argc, char** argv)
{
	for (int32 i = 1; i < argc; i++) {
		fOpenedFromArgs = true;
		BEntry entry(argv[i], true);
		BPath path;
		if (entry.GetPath(&path) == B_OK)
			_OpenPath(path.Path());
	}
}


void
BdbApp::RefsReceived(BMessage* message)
{
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		fOpenedFromArgs = true;
		BEntry entry(&ref, true);
		BPath path;
		if (entry.GetPath(&path) == B_OK)
			_OpenPath(path.Path());
	}
}


void
BdbApp::_OpenSample(const char* name)
{
	// Samples are copied before the first use and the copy opens from
	// then on (UI 3.5).
	BString source = SampleDirectory();
	source << "/" << name << ".rfdf";
	BString copy = SampleCopyDirectory();
	copy << "/" << name << ".rfdf";
	struct stat st;
	if (lstat(copy.String(), &st) != 0) {
		bdb::Status status = bdb::CopyStackFolder(source.String(),
			copy.String());
		if (!status) {
			BString text;
			text.SetToFormat(str::kSampleCopyFailed.Get(), status.Code());
			(new BAlert(str::kAppName, text, str::kOK, NULL, NULL,
				B_WIDTH_AS_USUAL, B_STOP_ALERT))->Go(NULL);
			_QuitIfIdle();
			return;
		}
		fLogger->Info("sample.copy", {{"sample", name}});
	}
	BString recent("sample:");
	recent << name;
	_OpenPath(copy.String(), recent.String());
	// Samples whose images come from the network ask once (UI 3.5).
	std::string root;
	if (!bdb::FolderStore::ResolveRoot(copy.String(), root))
		return;
	auto found = fDocuments.find(root.c_str());
	if (found == fDocuments.end())
		return;
	StackDocument* document = found->second;
	size_t toFetch;
	{
		DocLocker lock(document);
		toFetch = bdb::FindImagesToFetch(document->GetStack()).size();
	}
	if (toFetch == 0 || gSettings.FetchAsked(document->Id()))
		return;
	gSettings.SetFetchAsked(document->Id());
	gSettings.Save();
	BAlert* alert = new BAlert(str::kAppName, str::kFetchSampleQuestion,
		str::kFetchLater, str::kFetchNow, NULL, B_WIDTH_AS_USUAL,
		B_IDEA_ALERT);
	if (alert->Go() == 1) {
		auto window = fStackWindows.find(document);
		if (window != fStackWindows.end())
			window->second.SendMessage(kMsgFetchImages);
	}
}


void
BdbApp::_OpenPath(const char* path, const char* recentAs, bool fetch)
{
	BString pathString(path);
	if (pathString.StartsWith("sample:")) {
		pathString.Remove(0, 7);
		_OpenSample(pathString.String());
		return;
	}
	if (pathString.EndsWith(".rfdt")) {
		// A template dropped on the app: new stack from it (UI spec 3.1).
		_ShowSavePanel("template", path, NULL);
		return;
	}

	std::string root;
	bdb::Status status = bdb::FolderStore::ResolveRoot(path, root);
	BString key = root.c_str();
	auto existing = fDocuments.find(key);
	if (status && existing != fDocuments.end()) {
		// Already open: bring it to the front (UI spec 2.4).
		auto window = fStackWindows.find(existing->second);
		BLooper* looper = NULL;
		if (window != fStackWindows.end() && window->second.IsValid()
			&& window->second.Target(&looper) != NULL) {
			BWindow* stackWindow = dynamic_cast<BWindow*>(looper);
			if (stackWindow != NULL && stackWindow->Lock()) {
				stackWindow->Activate();
				stackWindow->Unlock();
			}
		} else {
			StackWindow* stackWindow = new StackWindow(existing->second,
				false);
			fStackWindows[existing->second] = BMessenger(stackWindow);
			stackWindow->Show();
		}
		_CloseStartWindow();
		return;
	}

	std::unique_ptr<bdb::Stack> stack;
	if (status)
		status = bdb::Stack::Open(path, fLogSink.get(), stack, &fThemes);
	if (!status) {
		const char* reason = str::kOpenInvalid;
		if (status.Kind() == bdb::ErrorKind::kNewerVersion)
			reason = str::kOpenNewer;
		else if (status.Kind() == bdb::ErrorKind::kUnsupported)
			reason = str::kOpenEncrypted;
		BString text;
		text.SetToFormat(str::kOpenFailed, reason);
		(new BAlert(str::kAppName, text, str::kOK, NULL, NULL,
			B_WIDTH_AS_USUAL, B_STOP_ALERT))->Go(NULL);
		_QuitIfIdle();
		return;
	}

	// Remember the manifest, so that "recent" opens the same file.
	BString manifestPath = path;
	if (!manifestPath.EndsWith(".rfdm")) {
		BDirectory dir(root.c_str());
		BEntry entry;
		while (dir.GetNextEntry(&entry) == B_OK) {
			char name[B_FILE_NAME_LENGTH];
			entry.GetName(name);
			if (BString(name).EndsWith(".rfdm")) {
				manifestPath = root.c_str();
				manifestPath << "/" << name;
				break;
			}
		}
	}
	gSettings.AddRecent(recentAs != NULL ? recentAs : manifestPath.String());
	gSettings.Save();

	// A blank stack (no cards, one empty template) opens in edit mode
	// (UI 3.3, 4).
	bool empty = stack->CardCount() == 0 && stack->Templates().size() == 1
		&& stack->Templates()[0]->Parts().empty();
	_SetStackIcon(root.c_str());
	StackDocument* document = new StackDocument(std::move(stack),
		manifestPath.String());
	fDocuments[key] = document;
	// A blank stack opens in edit mode (UI spec 3.3, 4).
	StackWindow* window = new StackWindow(document, empty);
	fStackWindows[document] = BMessenger(window);
	window->Show();
	_CloseStartWindow();
	if (fetch)
		BMessenger(window).SendMessage(kMsgFetchImages);
}


void
BdbApp::_ShowStart()
{
	BLooper* looper = NULL;
	if (fStartWindow.IsValid() && fStartWindow.Target(&looper) != NULL) {
		BWindow* window = dynamic_cast<BWindow*>(looper);
		if (window != NULL && window->Lock()) {
			window->Activate();
			window->Unlock();
			return;
		}
	}
	StartWindow* window = new StartWindow();
	fStartWindow = BMessenger(window);
	window->Show();
}


void
BdbApp::_CloseStartWindow()
{
	if (fStartWindow.IsValid()) {
		BMessenger start = fStartWindow;
		fStartWindow = BMessenger();
		start.SendMessage(B_QUIT_REQUESTED);
	}
}


void
BdbApp::_ChoosePaper()
{
	// Paper size and orientation before the save panel (UI 3.4); the last
	// choice is the next default.
	BString size = gSettings.LastPaper();
	if (size.IsEmpty())
		size = "screen";
	BString orientation = gSettings.LastOrientation();
	int32 width = bdb::kDefaultCardWidth;
	int32 height = bdb::kDefaultCardHeight;
	BMessage extra;
	extra.AddString("kind", "blank");
	PaperWindow* window = new PaperWindow(BMessenger(this), size,
		orientation, width, height, &extra);
	window->Show();
}


void
BdbApp::_ShowSavePanel(const char* kind, const char* source,
	const char* preset, const BMessage* extra)
{
	BMessage message(kMsgCreateStack);
	if (extra != NULL)
		message = *extra;
	message.what = kMsgCreateStack;
	message.RemoveName("kind");
	message.AddString("kind", kind);
	if (source != NULL)
		message.AddString("source", source);
	if (preset != NULL)
		message.AddString("preset", preset);
	if (fSavePanel == NULL) {
		BMessenger target(this);
		fSavePanel = new BFilePanel(B_SAVE_PANEL, &target, NULL, 0, false);
		fSavePanel->Window()->SetTitle(str::kSavePanelTitle);
		BPath documents;
		if (find_directory(B_USER_DIRECTORY, &documents) == B_OK) {
			documents.Append("Documents");
			create_directory(documents.Path(), 0755);
			fSavePanel->SetPanelDirectory(documents.Path());
		}
	}
	fSavePanel->SetMessage(&message);
	BString name = preset != NULL ? preset : str::kNewStackName;
	fSavePanel->SetSaveText(name.String());
	fSavePanel->Show();
}


void
BdbApp::_CreateStack(BMessage* message)
{
	entry_ref directoryRef;
	BString name;
	if (message->FindRef("directory", &directoryRef) != B_OK
		|| message->FindString("name", &name) != B_OK) {
		// A request from the start or preset window: ask where first.
		_ShowSavePanel(message->GetString("kind", "blank"),
			message->GetString("source", NULL),
			message->GetString("preset", NULL));
		return;
	}
	BPath directory(&directoryRef);
	BString title = name;
	if (title.EndsWith(".rfdf"))
		title.Truncate(title.Length() - 5);
	BString folder = title;
	folder << ".rfdf";
	BPath destination(directory.Path(), folder.String());

	BString kind = message->GetString("kind", "blank");
	std::string manifest;
	bdb::Status status;
	if (kind == "preset") {
		status = bdb::CreateStackFromPreset(message->GetString("source", ""),
			destination.Path(), title.String(),
			message->GetString("preset", ""), false, fLogSink.get(),
			&manifest);
	} else if (kind == "template") {
		status = bdb::CreateStackFromTemplateFile(
			message->GetString("source", ""), destination.Path(),
			title.String(), fLogSink.get(), &manifest);
	} else {
		bdb::NewStackOptions options;
		BString size = message->GetString("size", "screen");
		options.orientation = message->GetString("orientation", "landscape");
		options.width = message->GetInt32("width", bdb::kDefaultCardWidth);
		options.height = message->GetInt32("height",
			bdb::kDefaultCardHeight);
		// The screen standard has no "paper" (data spec 19.1).
		options.paperSize = size == "screen" ? "" : size.String();
		status = bdb::CreateEmptyStack(destination.Path(), title.String(),
			fLogSink.get(), &manifest, &options);
	}
	if (!status) {
		BString text;
		if (status.Kind() == bdb::ErrorKind::kExists)
			text = str::kCreateExists;
		else
			text.SetToFormat(str::kCreateFailed, status.Code());
		(new BAlert(str::kAppName, text, str::kOK, NULL, NULL,
			B_WIDTH_AS_USUAL, B_STOP_ALERT))->Go(NULL);
		return;
	}
	_OpenPath(manifest.c_str());
}


void
BdbApp::_Released(StackDocument* document)
{
	if (document->CountObservers() > 0) {
		// The stack window may have closed while the list stays open.
		auto window = fStackWindows.find(document);
		if (window != fStackWindows.end() && !window->second.IsValid())
			fStackWindows.erase(window);
		return;
	}
	for (auto it = fDocuments.begin(); it != fDocuments.end(); ++it) {
		if (it->second == document) {
			fDocuments.erase(it);
			break;
		}
	}
	fStackWindows.erase(document);
	{
		DocLocker lock(document);
		document->GetStack().Log().Info("stack.close");
	}
	delete document;
	gSettings.Save();
	_QuitIfIdle();
}


void
BdbApp::_QuitIfIdle()
{
	if (fDocuments.empty() && !fStartWindow.IsValid() && !fWizard.IsValid()
		&& !fPresetWindow.IsValid()
		&& (fSavePanel == NULL || !fSavePanel->IsShowing())
		&& (fOpenPanel == NULL || !fOpenPanel->IsShowing())) {
		PostMessage(B_QUIT_REQUESTED);
	}
}


void
BdbApp::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgShowStart:
			_ShowStart();
			break;
		case kMsgNewBlankStack:
			_ChoosePaper();
			break;
		case kMsgPaperChosen:
			gSettings.SetLastPaper(message->GetString("size", "screen"),
				message->GetString("orientation", "landscape"));
			_ShowSavePanel("blank", NULL, NULL, message);
			break;
		case kMsgNewStackCsv:
		{
			CsvWizard* wizard = new CsvWizard(NULL, BMessenger(this));
			fWizard = BMessenger(wizard);
			wizard->Show();
			break;
		}
		case kMsgCsvFinished:
		{
			BString manifest = message->GetString("manifest", "");
			if (!manifest.IsEmpty()) {
				_OpenPath(manifest.String(), NULL,
					message->GetBool("fetch", false));
			}
			break;
		}
		case kMsgRemoveRecent:
		{
			BString path = message->GetString("path", "");
			gSettings.RemoveRecent(path);
			if (path.StartsWith("sample:")) {
				path.Remove(0, 7);
				gSettings.RemoveSample(path);
			}
			gSettings.Save();
			break;
		}
		case kMsgShowPresets:
		{
			PresetWindow* window = new PresetWindow(
				PresetDirectory().String());
			fPresetWindow = BMessenger(window);
			window->Show();
			break;
		}
		case kMsgOpenPanel:
			if (fOpenPanel == NULL) {
				BMessenger target(this);
				fOpenPanel = new BFilePanel(B_OPEN_PANEL, &target, NULL,
					B_FILE_NODE | B_DIRECTORY_NODE, false);
			}
			fOpenPanel->Show();
			break;
		case kMsgOpenPath:
		{
			BString path;
			if (message->FindString("path", &path) == B_OK)
				_OpenPath(path.String());
			break;
		}
		case kMsgCreateStack:
			_CreateStack(message);
			break;
		case kMsgCsvWizardClosed:
			fWizard = BMessenger();
			_QuitIfIdle();
			break;
		case kMsgStartWindowClosed:
			fStartWindow = BMessenger();
			_QuitIfIdle();
			break;
		case kMsgDocumentReleased:
		{
			void* pointer = NULL;
			if (message->FindPointer("document", &pointer) == B_OK)
				_Released(static_cast<StackDocument*>(pointer));
			break;
		}
		case kMsgLogFailed:
			if (!fLogFailureShown) {
				fLogFailureShown = true;
				(new BAlert(str::kAppName, str::kLogFailed, str::kOK, NULL,
					NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT))->Go(NULL);
			}
			break;
		case B_CANCEL:
			// A file panel was closed.
			_QuitIfIdle();
			break;
		default:
			BApplication::MessageReceived(message);
			break;
	}
}


bool
BdbApp::QuitRequested()
{
	// Ask the stack windows first; they save and may refuse.
	for (auto& entry : fStackWindows) {
		BLooper* looper = NULL;
		if (!entry.second.IsValid() || entry.second.Target(&looper) == NULL)
			continue;
		BWindow* window = dynamic_cast<BWindow*>(looper);
		if (window == NULL || !window->Lock())
			continue;
		bool ok = window->QuitRequested();
		if (ok)
			window->Quit();
		else {
			window->Unlock();
			return false;
		}
	}
	return BApplication::QuitRequested();
}


int
main()
{
	BdbApp app;
	app.Run();
	return 0;
}
