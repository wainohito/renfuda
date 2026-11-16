/*
 * Renfuda (連札) card database - RenkuOS application
 */
#include "Extras.h"

#include <Application.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <Button.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Path.h>
#include <RadioButton.h>
#include <Roster.h>
#include <Screen.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <TranslationUtils.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>
#include <Url.h>
#include <View.h>
#include <Window.h>

#include <algorithm>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include "CardPainter.h"
#include "Constants.h"
#include "Model.h"
#include "Settings.h"
#include "Stack.h"
#include "Strings.h"


// #pragma mark - links


status_t
OpenUrl(const char* url, bool* usedFallback)
{
	if (usedFallback != NULL)
		*usedFallback = false;
	BString text(url);
	bool web = text.IStartsWith("http://") || text.IStartsWith("https://");
	BString browser = gSettings.Browser();
	if (web && !browser.IsEmpty()) {
		entry_ref ref;
		BEntry entry(browser.String(), true);
		if (entry.Exists() && entry.GetRef(&ref) == B_OK) {
			const char* argv[] = { url };
			status_t status = be_roster->Launch(&ref, 1, argv);
			if (status == B_OK || status == B_ALREADY_RUNNING)
				return B_OK;
		}
		if (usedFallback != NULL)
			*usedFallback = true;
	}
	BUrl link(url, true);
	return link.OpenWithPreferredApplication(false);
}


// #pragma mark - video


static BPath
VideoFolder()
{
	BPath folder;
	if (find_directory(B_SYSTEM_TEMP_DIRECTORY, &folder) == B_OK) {
		BString leaf("Renfuda-video-");
		leaf << (int32)getuid();
		folder.Append(leaf.String());
	}
	return folder;
}


struct VideoJob {
	BString		path;
	BString		name;
	BMessenger	replyTo;
};


static bool
SameFile(const char* a, const char* b)
{
	struct stat sa, sb;
	return stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_size == sb.st_size
		&& sa.st_mtime == sb.st_mtime;
}


static status_t
CopyFile(const char* from, const char* to)
{
	BFile source(from, B_READ_ONLY);
	BString part(to);
	part << ".part";
	BFile target(part.String(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (source.InitCheck() != B_OK || target.InitCheck() != B_OK)
		return B_ERROR;
	char buffer[64 * 1024];
	while (true) {
		ssize_t count = source.Read(buffer, sizeof(buffer));
		if (count < 0)
			return count;
		if (count == 0)
			break;
		if (target.Write(buffer, count) != count)
			return B_IO_ERROR;
	}
	target.Unset();
	if (rename(part.String(), to) != 0)
		return B_ERROR;
	// Same time as the original: an equal copy is reused next time.
	struct stat info;
	if (stat(from, &info) == 0) {
		struct utimbuf times = { info.st_atime, info.st_mtime };
		utime(to, &times);
	}
	return B_OK;
}


static status_t
OpenVideoThread(void* data)
{
	VideoJob* job = static_cast<VideoJob*>(data);
	BString open = job->path;
	BString leaf = job->name;
	leaf.ReplaceAll("/", "-");
	BPath folder = VideoFolder();
	if (!leaf.IsEmpty() && leaf != "." && leaf != ".."
		&& folder.InitCheck() == B_OK
		&& create_directory(folder.Path(), 0700) == B_OK) {
		// One sub folder per media file: equal names do not collide.
		BPath sub(folder.Path(), BPath(job->path.String()).Leaf());
		BPath copy(sub.Path(), leaf.String());
		if (create_directory(sub.Path(), 0700) == B_OK
			&& (SameFile(job->path.String(), copy.Path())
				|| CopyFile(job->path.String(), copy.Path()) == B_OK)) {
			open = copy.Path();
		}
	}
	entry_ref ref;
	status_t status = get_ref_for_path(open.String(), &ref);
	if (status == B_OK) {
		status = be_roster->Launch(&ref);
		if (status != B_OK && status != B_ALREADY_RUNNING) {
			// No preferred application (the file has no type yet).
			BMessage refs(B_REFS_RECEIVED);
			refs.AddRef("refs", &ref);
			status = be_roster->Launch("application/x-vnd.Haiku-MediaPlayer",
				&refs);
		}
	}
	if (status != B_OK && status != B_ALREADY_RUNNING)
		job->replyTo.SendMessage(kMsgVideoFailed);
	delete job;
	return 0;
}


void
OpenVideo(const char* path, const char* name, BMessenger replyTo)
{
	VideoJob* job = new VideoJob;
	job->path = path;
	job->name = name;
	job->replyTo = replyTo;
	thread_id thread = spawn_thread(OpenVideoThread, "open video",
		B_LOW_PRIORITY, job);
	if (thread < 0 || resume_thread(thread) != B_OK) {
		delete job;
		replyTo.SendMessage(kMsgVideoFailed);
	}
}


static void
RemoveTree(BEntry& entry)
{
	BDirectory directory(&entry);
	if (directory.InitCheck() == B_OK) {
		BEntry child;
		while (directory.GetNextEntry(&child) == B_OK)
			RemoveTree(child);
	}
	entry.Remove();
}


void
RemoveVideoCopies()
{
	BPath folder = VideoFolder();
	if (folder.InitCheck() != B_OK)
		return;
	BEntry entry(folder.Path());
	if (entry.Exists())
		RemoveTree(entry);
}


// #pragma mark - settings window


static const uint32 kMsgPrefsDefault = 'pfDf';
static const uint32 kMsgPrefsApp = 'pfAp';
static const uint32 kMsgPrefsChoose = 'pfCh';
static const uint32 kMsgPrefsChosen = 'pfCn';
static const uint32 kMsgPrefsOK = 'pfOK';


class PreferencesWindow : public BWindow {
public:
	PreferencesWindow()
		:
		BWindow(BRect(0, 0, 440, 200), str::kPrefsTitle, B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
				| B_CLOSE_ON_ESCAPE),
		fPanel(NULL)
	{
		BString browser = gSettings.Browser();
		fDefault = new BRadioButton("default", str::kPrefsBrowserDefault,
			new BMessage(kMsgPrefsDefault));
		fApp = new BRadioButton("app", str::kPrefsBrowserApp,
			new BMessage(kMsgPrefsApp));
		fPath = new BTextControl("path", NULL, browser, NULL);
		fPath->SetExplicitMinSize(BSize(260, B_SIZE_UNSET));
		fChoose = new BButton("choose", str::kChoose,
			new BMessage(kMsgPrefsChoose));
		BTextView* note = new BTextView("note");
		note->SetText(str::kPrefsBrowserNote);
		note->MakeEditable(false);
		note->MakeSelectable(false);
		note->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		note->SetExplicitMinSize(BSize(360, 36));
		BButton* ok = new BButton("ok", str::kOK, new BMessage(kMsgPrefsOK));
		BButton* cancel = new BButton("cancel", str::kCancel,
			new BMessage(B_QUIT_REQUESTED));
		BStringView* heading = new BStringView("heading", str::kPrefsBrowser);
		BFont bold(be_bold_font);
		heading->SetFont(&bold);

		BLayoutBuilder::Group<>(this, B_VERTICAL)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(heading)
			.Add(fDefault)
			.AddGroup(B_HORIZONTAL)
				.Add(fApp)
				.Add(fPath, 1)
				.Add(fChoose)
			.End()
			.Add(note)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(cancel)
				.Add(ok)
			.End();
		(browser.IsEmpty() ? fDefault : fApp)->SetValue(B_CONTROL_ON);
		_Update();
		SetDefaultButton(ok);
		CenterOnScreen();
	}

	virtual ~PreferencesWindow()
	{
		delete fPanel;
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgPrefsDefault:
			case kMsgPrefsApp:
				_Update();
				break;
			case kMsgPrefsChoose:
			{
				if (fPanel == NULL) {
					BMessenger target(this);
					fPanel = new BFilePanel(B_OPEN_PANEL, &target, NULL,
						B_FILE_NODE, false, new BMessage(kMsgPrefsChosen));
					fPanel->Window()->SetTitle(str::kPrefsChooseApp);
					fPanel->SetPanelDirectory("/boot/system/apps");
				}
				fPanel->Show();
				break;
			}
			case kMsgPrefsChosen:
			{
				entry_ref ref;
				if (message->FindRef("refs", &ref) == B_OK) {
					BEntry entry(&ref, true);
					BPath path;
					if (entry.GetPath(&path) == B_OK) {
						fPath->SetText(path.Path());
						fApp->SetValue(B_CONTROL_ON);
						_Update();
					}
				}
				break;
			}
			case kMsgPrefsOK:
				gSettings.SetBrowser(fApp->Value() == B_CONTROL_ON
					? fPath->Text() : "");
				gSettings.Save();
				Quit();
				break;
			default:
				BWindow::MessageReceived(message);
				break;
		}
	}

private:
	void _Update()
	{
		bool app = fApp->Value() == B_CONTROL_ON;
		fPath->SetEnabled(app);
		fChoose->SetEnabled(app);
	}

	BRadioButton*	fDefault;
	BRadioButton*	fApp;
	BTextControl*	fPath;
	BButton*		fChoose;
	BFilePanel*		fPanel;
};


static BMessenger sPreferences;


void
ShowPreferences()
{
	// The open one comes to the front.
	BLooper* looper = NULL;
	if (sPreferences.IsValid())
		sPreferences.Target(&looper);
	BWindow* open = dynamic_cast<BWindow*>(looper);
	if (open != NULL && open->Lock()) {
		open->Activate();
		open->Unlock();
		return;
	}
	PreferencesWindow* window = new PreferencesWindow();
	sPreferences = BMessenger(window);
	window->Show();
}


// #pragma mark - image viewer


class BitmapView : public BView {
public:
	BitmapView(BBitmap* bitmap)
		:
		BView("image", B_WILL_DRAW),
		fBitmap(bitmap)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR, B_DARKEN_2_TINT);
		BRect bounds = bitmap->Bounds();
		SetExplicitMinSize(BSize(bounds.Width(), bounds.Height()));
		SetExplicitMaxSize(BSize(bounds.Width(), bounds.Height()));
	}

	virtual ~BitmapView()
	{
		delete fBitmap;
	}

	virtual void Draw(BRect updateRect)
	{
		SetDrawingMode(B_OP_ALPHA);
		DrawBitmap(fBitmap, B_ORIGIN);
	}

private:
	BBitmap*	fBitmap;
};


void
ShowImage(const char* path, const char* title)
{
	BBitmap* bitmap = BTranslationUtils::GetBitmap(path);
	if (bitmap == NULL || !bitmap->IsValid()) {
		delete bitmap;
		return;
	}
	BRect bounds = bitmap->Bounds();
	BString name(title);
	name.SetToFormat("%s (%d × %d)", title, (int)bounds.Width() + 1,
		(int)bounds.Height() + 1);
	BWindow* window = new BWindow(BRect(0, 0, 100, 100), name,
		B_DOCUMENT_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_CLOSE_ON_ESCAPE);
	BitmapView* view = new BitmapView(bitmap);
	BScrollView* scroll = new BScrollView("scroll", view, 0, true, true,
		B_NO_BORDER);
	BLayoutBuilder::Group<>(window, B_VERTICAL, 0)
		.Add(scroll);
	// Its own size, or the screen when larger.
	BRect screen = BScreen(window).Frame();
	float bar = B_V_SCROLL_BAR_WIDTH;
	window->ResizeTo(std::min(bounds.Width() + bar + 1, screen.Width() - 80),
		std::min(bounds.Height() + bar + 1, screen.Height() - 100));
	window->SetSizeLimits(80, bounds.Width() + bar + 1, 60,
		bounds.Height() + bar + 1);
	window->CenterOnScreen();
	window->Show();
}


// #pragma mark - card image


status_t
SaveCardImage(const bdb::Stack& stack, const bdb::Card& card,
	const bdb::Template& tmpl, const bdb::Theme& theme, const char* path,
	int32 position, int32 count)
{
	BRect bounds(0, 0, tmpl.Width() - 1, tmpl.Height() - 1);
	BBitmap bitmap(bounds, B_RGB32, true);
	if (bitmap.InitCheck() != B_OK)
		return bitmap.InitCheck();
	BView* view = new BView(bounds, "card", B_FOLLOW_NONE, 0);
	bitmap.AddChild(view);
	if (!bitmap.Lock())
		return B_ERROR;
	BitmapCache bitmaps;
	view->SetHighColor(ui_color(B_DOCUMENT_BACKGROUND_COLOR));
	view->FillRect(bounds);
	ViewCanvas canvas(view, 1.0f);
	PaintContext context;
	context.tmpl = &tmpl;
	context.card = &card;
	context.stack = &stack;
	context.theme = &theme;
	context.bitmaps = &bitmaps;
	context.mode = kPaintPlay;
	context.drawValues = true;
	context.position = position;
	context.count = count;
	context.hiddenParts = bdb::ComputeSwitchState(tmpl, &card).hidden;
	PaintCard(canvas, context);
	view->Sync();
	bitmap.RemoveChild(view);
	delete view;
	bitmap.Unlock();

	BFile file(path, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();
	BBitmapStream stream(&bitmap);
	status_t status = BTranslatorRoster::Default()->Translate(&stream, NULL,
		NULL, &file, B_PNG_FORMAT);
	BBitmap* detached = NULL;
	stream.DetachBitmap(&detached);
	if (status == B_OK)
		file.WriteAttr("BEOS:TYPE", B_MIME_STRING_TYPE, 0, "image/png", 10);
	return status;
}
