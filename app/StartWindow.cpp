/*
 * BDB card database - RenkuOS application
 */
#include "StartWindow.h"

#include <Application.h>
#include <Button.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Window.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Path.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextView.h>

#include <algorithm>
#include <memory>

#include "BdbApp.h"
#include "CardPainter.h"
#include "Constants.h"
#include "Json.h"
#include "Model.h"
#include "Settings.h"
#include "Stack.h"
#include "Strings.h"


using bdb::Json;


// #pragma mark - StartWindow


BString
RecentStackLabel(const char* path)
{
	BString text(path);
	if (text.StartsWith("sample:")) {
		// Samples carry a mark (UI 3.5).
		text.Remove(0, 7);
		text << "  [" << str::kSampleMark.Get() << "]";
		return text;
	}
	// Show the stack folder name rather than the manifest file name.
	BPath parsed(path);
	BPath parent;
	if (BString(parsed.Leaf()).EndsWith(".rfdm")
		&& parsed.GetParent(&parent) == B_OK) {
		BString name = parent.Leaf();
		name.RemoveLast(".rfdf");
		return name;
	}
	return BString(parsed.Leaf());
}


class RecentItem : public BStringItem {
public:
	RecentItem(const char* path)
		:
		BStringItem(RecentStackLabel(path)),
		fPath(path)
	{
	}

	const BString& Path() const { return fPath; }

private:
	BString	fPath;
};


// The recent list with a context menu (UI 3.5 "一覧から外す").
class RecentListView : public BListView {
public:
	RecentListView()
		:
		BListView("recent")
	{
	}

	virtual void MouseDown(BPoint where)
	{
		int32 buttons = 0;
		Window()->CurrentMessage()->FindInt32("buttons", &buttons);
		if ((buttons & B_SECONDARY_MOUSE_BUTTON) == 0) {
			BListView::MouseDown(where);
			return;
		}
		int32 index = IndexOf(where);
		RecentItem* item = dynamic_cast<RecentItem*>(ItemAt(index));
		if (item == NULL)
			return;
		Select(index);
		BPopUpMenu* menu = new BPopUpMenu("recent", false, false);
		BMessage* message = new BMessage(kMsgRemoveRecent);
		message->AddString("path", item->Path());
		menu->AddItem(new BMenuItem(str::kRemoveFromList, message));
		menu->SetTargetForItems(Window());
		menu->SetAsyncAutoDestruct(true);
		menu->Go(ConvertToScreen(where), true, true, true);
	}
};


StartWindow::StartWindow()
	:
	BWindow(BRect(0, 0, 380, 360), str::kStartTitle, B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS)
{
	BButton* blank = new BButton("blank", str::kStartBlank,
		new BMessage(kMsgNewBlankStack));
	BButton* preset = new BButton("preset", str::kStartPreset,
		new BMessage(kMsgShowPresets));
	BButton* csv = new BButton("csv", str::kStartCsv,
		new BMessage(kMsgNewStackCsv));
	BButton* open = new BButton("open", str::kStartOpen,
		new BMessage(kMsgOpenPanel));
	BButton* buttons[] = { blank, preset, csv, open };
	for (BButton* button : buttons) {
		button->SetTarget(be_app);
		button->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	}

	fRecent = new RecentListView();
	fRecent->SetInvocationMessage(new BMessage(kMsgRecentInvoked));
	_FillRecent();
	// Wide enough for stack names (the window would be as narrow as the
	// buttons otherwise).
	BScrollView* recentScroll = new BScrollView("scroll", fRecent, 0, false,
		true);
	recentScroll->SetExplicitMinSize(BSize(320, 140));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(blank)
		.Add(preset)
		.Add(csv)
		.Add(open)
		.AddStrut(8)
		.Add(new BStringView("recent label", str::kStartRecent))
		.Add(recentScroll);
	CenterOnScreen();
}


void
StartWindow::_FillRecent()
{
	for (int32 i = fRecent->CountItems() - 1; i >= 0; i--)
		delete fRecent->RemoveItem(i);
	for (int32 i = 0; i < gSettings.CountRecent(); i++)
		fRecent->AddItem(new RecentItem(gSettings.RecentAt(i)));
}


void
StartWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRecentInvoked:
		{
			RecentItem* item = dynamic_cast<RecentItem*>(
				fRecent->ItemAt(fRecent->CurrentSelection()));
			if (item == NULL)
				break;
			BMessage open(kMsgOpenPath);
			open.AddString("path", item->Path());
			be_app->PostMessage(&open);
			break;
		}
		case kMsgRemoveRecent:
		{
			// The app updates the settings; the list follows.
			BMessage forward(*message);
			be_app->PostMessage(&forward);
			gSettings.RemoveRecent(message->GetString("path", ""));
			_FillRecent();
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
StartWindow::QuitRequested()
{
	be_app->PostMessage(kMsgStartWindowClosed);
	return true;
}


// #pragma mark - PresetPreview


// A scaled drawing of the preset's template with its theme, as the card
// will look (UI spec 3.3).
class PresetPreview : public BView {
public:
	PresetPreview()
		:
		BView("preview", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
	{
		SetExplicitMinSize(BSize(300, 200));
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	}

	void SetPreset(const char* path)
	{
		fStack.reset();
		BdbApp* app = dynamic_cast<BdbApp*>(be_app);
		if (path != NULL) {
			bdb::Stack::Open(path, NULL, fStack, app != NULL
				? &app->Themes() : NULL);
		}
		fBitmaps.Clear();
		Invalidate();
	}

	virtual void Draw(BRect updateRect)
	{
		if (fStack == NULL)
			return;
		const bdb::Template* tmpl = fStack->FindTemplate(
			fStack->DefaultTemplateId());
		if (tmpl == NULL)
			return;
		BRect bounds = Bounds().InsetByCopy(6, 6);
		float scale = std::min((bounds.Width() + 1) / tmpl->Width(),
			(bounds.Height() + 1) / tmpl->Height());
		bdb::Theme theme = WithSystemColors(fStack->ThemeFor(tmpl));
		PaintContext context;
		context.tmpl = tmpl;
		context.stack = fStack.get();
		context.theme = &theme;
		context.bitmaps = &fBitmaps;
		context.mode = kPaintEdit;
		ViewCanvas canvas(this, scale, bounds.LeftTop());
		PaintCard(canvas, context);
		SetHighColor(ui_color(B_CONTROL_BORDER_COLOR));
		StrokeRect(BRect(bounds.left - 1, bounds.top - 1,
			bounds.left + tmpl->Width() * scale,
			bounds.top + tmpl->Height() * scale));
	}

private:
	std::unique_ptr<bdb::Stack>	fStack;
	BitmapCache					fBitmaps;
};


// #pragma mark - PresetWindow


class PresetItem : public BStringItem {
public:
	PresetItem(const bdb::PresetInfo& info)
		:
		BStringItem(info.title.c_str()),
		fPath(info.path.c_str())
	{
	}

	const BString& Path() const { return fPath; }

private:
	BString	fPath;
};


PresetWindow::PresetWindow(const char* directory)
	:
	BWindow(BRect(0, 0, 560, 320), str::kPresetTitle, B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS)
{
	bdb::ListPresets(directory, fPresets);

	fList = new BListView("presets");
	fList->SetSelectionMessage(new BMessage(kMsgPresetSelected));
	fList->SetInvocationMessage(new BMessage(kMsgPresetCreate));
	for (const bdb::PresetInfo& info : fPresets)
		fList->AddItem(new PresetItem(info));
	fPreview = new PresetPreview();
	fDescription = new BTextView("description");
	fDescription->MakeEditable(false);
	fDescription->MakeSelectable(false);
	fDescription->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fDescription->SetExplicitMinSize(BSize(256, 48));
	if (fPresets.empty())
		fDescription->SetText(str::kPresetNone);
	fCreate = new BButton("create", str::kCreate,
		new BMessage(kMsgPresetCreate));
	fCreate->SetEnabled(false);

	BLayoutBuilder::Group<>(this, B_HORIZONTAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(new BScrollView("scroll", fList, 0, false, true), 1)
		.AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING, 2)
			.Add(fPreview)
			.Add(fDescription)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(fCreate)
			.End()
		.End();
	SetDefaultButton(fCreate);
	CenterOnScreen();
	if (!fPresets.empty())
		fList->Select(0);
}


void
PresetWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPresetSelected:
		{
			int32 index = fList->CurrentSelection();
			bool valid = index >= 0 && index < int32(fPresets.size());
			fCreate->SetEnabled(valid);
			fPreview->SetPreset(valid ? fPresets[index].path.c_str() : NULL);
			fDescription->SetText(valid
				? fPresets[index].description.c_str() : "");
			break;
		}
		case kMsgPresetCreate:
		{
			int32 index = fList->CurrentSelection();
			if (index < 0 || index >= int32(fPresets.size()))
				break;
			BMessage create(kMsgCreateStack);
			create.AddString("kind", "preset");
			create.AddString("source", fPresets[index].path.c_str());
			create.AddString("preset", fPresets[index].title.c_str());
			be_app->PostMessage(&create);
			PostMessage(B_QUIT_REQUESTED);
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}
