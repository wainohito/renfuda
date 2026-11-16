/*
 * BDB card database - RenkuOS application
 */
#include "StackWindow.h"


#include <Alert.h>
#include <Application.h>
#include <Beep.h>
#include <Bitmap.h>
#include <Button.h>
#include <ControlLook.h>
#include <Cursor.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FileGameSound.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <MimeType.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Screen.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <StringView.h>
#include <SymLink.h>
#include <TextControl.h>
#include <TranslationUtils.h>
#include <TranslatorRoster.h>
#include <Url.h>

#include <algorithm>
#include <cstdio>
#include <set>

#include "BdbApp.h"
#include "CardSurface.h"
#include "Csv.h"
#include "CsvWizard.h"
#include "EditWindows.h"
#include "Extras.h"
#include "FieldViews.h"
#include "Ids.h"
#include "ImageFetch.h"
#include "Limits.h"
#include "Link.h"
#include "ListWindow.h"
#include "MiscWindows.h"
#include "PaperWindow.h"
#include "PdfExport.h"
#include "Query.h"
#include "Settings.h"
#include "Stack.h"
#include "StackDocument.h"
#include "StartWindow.h"
#include "Strings.h"
#include "Text.h"


using bdb::Json;

static const float kZoomLevels[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f,
	2.0f, 3.0f };
static const int32 kZoomLevelCount = 8;
static const uint32 kMsgGoToNumberDone = 'cNmD';

enum {
	kAuxProblems = 0,
	kAuxSettings,
	kAuxTemplates
};


static BMenuItem*
Item(const char* label, uint32 what, char shortcut = 0, uint32 modifiers = 0)
{
	return new BMenuItem(label, new BMessage(what), shortcut, modifiers);
}


// #pragma mark - BannerView


// The colored strip above the card in edit and find mode (UI 4.1).
class BannerView : public BView {
public:
	BannerView()
		:
		BView("banner", B_WILL_DRAW)
	{
		fText = new BStringView("text", "");
		// Cut to the window (full text in the tool tip), rather than
		// widening it.
		fText->SetExplicitMinSize(BSize(16, B_SIZE_UNSET));
		fText->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
		fText->SetTruncation(B_TRUNCATE_END);
		BFont font(be_bold_font);
		fText->SetFont(&font);
		fFirst = new BButton("first", "", NULL);
		fSecond = new BButton("second", "", NULL);
		BLayoutBuilder::Group<>(this, B_HORIZONTAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_SMALL_INSETS, 3, B_USE_SMALL_INSETS, 3)
			.Add(fText)
			.Add(fSecond)
			.Add(fFirst);
	}

	void Set(Mode mode, const char* text)
	{
		rgb_color background = mode == kModeEdit ? make_color(52, 96, 168)
			: make_color(240, 196, 64);
		rgb_color foreground = mode == kModeEdit ? make_color(255, 255, 255)
			: make_color(0, 0, 0);
		SetViewColor(background);
		fText->SetViewColor(background);
		fText->SetLowColor(background);
		fText->SetHighColor(foreground);
		fText->SetText(text);
		fText->SetToolTip(text);
		if (mode == kModeEdit) {
			fFirst->SetLabel(str::kEndEdit);
			fFirst->SetMessage(new BMessage(kMsgModeBrowse));
			if (!fSecond->IsHidden(fSecond))
				fSecond->Hide();
		} else {
			fFirst->SetLabel(str::kRunFind);
			fFirst->SetMessage(new BMessage(kMsgRunFind));
			fSecond->SetLabel(str::kShowAll);
			fSecond->SetMessage(new BMessage(kMsgShowAll));
			if (fSecond->IsHidden(fSecond))
				fSecond->Show();
		}
		Invalidate();
		fText->Invalidate();
	}

	void SetTarget(BHandler* target)
	{
		fFirst->SetTarget(target);
		fSecond->SetTarget(target);
	}

private:
	BStringView*	fText;
	BButton*		fFirst;
	BButton*		fSecond;
};


// The template name in the status bar; clicking enters edit mode (UI 4.1).
class StatusLink : public BStringView {
public:
	StatusLink()
		:
		BStringView("template", "")
	{
	}

	virtual void MouseDown(BPoint where)
	{
		if (Window() != NULL)
			Window()->PostMessage(kMsgStatusClicked);
	}

	virtual void MouseMoved(BPoint where, uint32 code,
		const BMessage* dragMessage)
	{
		BCursor cursor(code == B_EXITED_VIEW ? B_CURSOR_ID_SYSTEM_DEFAULT
			: B_CURSOR_ID_FOLLOW_LINK);
		SetViewCursor(&cursor);
	}
};


// #pragma mark - CardFrame


// Holds the card view and its scroll bars. The scroll bars show only when
// the card does not fit; in play and find mode the window fits the card,
// so they show only when the card is larger than the screen. A card
// smaller than the window (the menus are wider) is centered.
class CardFrame : public BView {
public:
	CardFrame(CardSurface* card)
		:
		BView("card frame", B_FRAME_EVENTS),
		fCard(card),
		fCentered(true)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		fHorizontal = new BScrollBar("horizontal", card, 0, 0, B_HORIZONTAL);
		fVertical = new BScrollBar("vertical", card, 0, 0, B_VERTICAL);
		AddChild(card);
		AddChild(fHorizontal);
		AddChild(fVertical);
		// Hidden until the card does not fit.
		fHorizontal->SetTarget((BView*)NULL);
		fVertical->SetTarget((BView*)NULL);
		fHorizontal->Hide();
		fVertical->Hide();
		SetExplicitMinSize(BSize(64, 64));
		SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	}

	void SetCentered(bool centered)
	{
		if (centered == fCentered)
			return;
		fCentered = centered;
		Arrange();
	}

	virtual void AttachedToWindow()
	{
		BView::AttachedToWindow();
		Arrange();
	}

	virtual void FrameResized(float width, float height)
	{
		BView::FrameResized(width, height);
		Arrange();
	}

	void Arrange()
	{
		BSize card = fCard->ScaledCardSize();
		float barWidth = be_control_look->GetScrollBarWidth(B_VERTICAL);
		float barHeight = be_control_look->GetScrollBarWidth(B_HORIZONTAL);
		float width = Bounds().Width() + 1;
		float height = Bounds().Height() + 1;
		bool horizontal = card.width > width + 0.5f;
		if (horizontal)
			height -= barHeight;
		bool vertical = card.height > height + 0.5f;
		if (vertical) {
			width -= barWidth;
			if (!horizontal && card.width > width + 0.5f) {
				horizontal = true;
				height -= barHeight;
			}
		}
		_ShowBar(fHorizontal, horizontal);
		_ShowBar(fVertical, vertical);
		fCard->MoveTo(0, 0);
		fCard->ResizeTo(width - 1, height - 1);
		if (horizontal) {
			fHorizontal->MoveTo(0, height);
			fHorizontal->ResizeTo(width - 1, barHeight - 1);
		}
		if (vertical) {
			fVertical->MoveTo(width, 0);
			fVertical->ResizeTo(barWidth - 1, height - 1);
		}
		// Without a scroll bar, the card is centered in its direction (play
		// and find) or at the left/top (edit).
		float x = horizontal ? fHorizontal->Value() : fCentered
			? -floorf(std::max(0.0f, width - card.width) / 2) : 0;
		float y = vertical ? fVertical->Value() : fCentered
			? -floorf(std::max(0.0f, height - card.height) / 2) : 0;
		fCard->ScrollTo(x, y);
		Invalidate();
	}

private:
	void _ShowBar(BScrollBar* bar, bool show)
	{
		// A hidden bar is not the card's: the card view would set its
		// value when it scrolls, and the bar would scroll it back to 0
		// (the centering scrolls to negative positions).
		if (show && bar->IsHidden()) {
			bar->SetTarget(fCard);
			bar->Show();
		} else if (!show && !bar->IsHidden()) {
			bar->SetValue(0);
			bar->SetTarget((BView*)NULL);
			bar->Hide();
		}
	}

	CardSurface*	fCard;
	BScrollBar*		fHorizontal;
	BScrollBar*		fVertical;
	bool			fCentered;
};


// #pragma mark - ModeButton


// A mode in the status bar: the current mode is green, edit mode red.
class ModeButton : public BView {
public:
	ModeButton(const char* label, uint32 what, rgb_color active)
		:
		BView(label, B_WILL_DRAW),
		fLabel(label),
		fWhat(what),
		fActive(active),
		fSelected(false),
		fPressed(false)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
		BFont font(be_bold_font);
		font.SetSize(be_plain_font->Size());
		SetFont(&font);
	}

	void SetSelected(bool selected)
	{
		if (selected == fSelected)
			return;
		fSelected = selected;
		Invalidate();
	}

	virtual BSize MinSize()
	{
		font_height height;
		GetFontHeight(&height);
		return BSize(ceilf(StringWidth(fLabel.String())) + 22,
			ceilf(height.ascent + height.descent) + 8);
	}

	virtual BSize MaxSize() { return MinSize(); }
	virtual BSize PreferredSize() { return MinSize(); }

	virtual void Draw(BRect updateRect)
	{
		BRect rect = Bounds();
		rgb_color base = fSelected ? fActive
			: ui_color(B_CONTROL_BACKGROUND_COLOR);
		uint32 flags = fPressed ? (uint32)BControlLook::B_ACTIVATED : 0;
		be_control_look->DrawButtonFrame(this, rect, updateRect, base,
			ViewColor(), flags);
		be_control_look->DrawButtonBackground(this, rect, updateRect, base,
			flags);
		font_height height;
		GetFontHeight(&height);
		float width = StringWidth(fLabel.String());
		BPoint point(floorf((Bounds().Width() - width) / 2) + 1,
			floorf((Bounds().Height() + height.ascent - height.descent) / 2)
				+ 1);
		SetDrawingMode(B_OP_OVER);
		SetHighColor(fSelected ? make_color(255, 255, 255)
			: ui_color(B_CONTROL_TEXT_COLOR));
		DrawString(fLabel.String(), point);
	}

	virtual void MouseDown(BPoint where)
	{
		fPressed = true;
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
		Invalidate();
	}

	virtual void MouseMoved(BPoint where, uint32 code,
		const BMessage* dragMessage)
	{
		if (!fPressed)
			return;
		Invalidate();
	}

	virtual void MouseUp(BPoint where)
	{
		if (!fPressed)
			return;
		fPressed = false;
		Invalidate();
		if (Bounds().Contains(where) && Window() != NULL)
			Window()->PostMessage(fWhat);
	}

private:
	BString		fLabel;
	uint32		fWhat;
	rgb_color	fActive;
	bool		fSelected;
	bool		fPressed;
};


// #pragma mark - StackWindow


StackWindow::StackWindow(StackDocument* document, bool startInEditMode)
	:
	BWindow(BRect(80, 80, 680, 560), "Renfuda", B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS),
	fDocument(document),
	fFindActive(false),
	fDirty(false),
	fSaveFailed(false),
	fTemplateDirty(false),
	fAutosave(NULL),
	fPalette(NULL),
	fInspector(NULL),
	fImagePanel(NULL),
	fBackgroundPanel(NULL),
	fPdfPanel(NULL),
	fMediaPanel(NULL),
	fCardImagePanel(NULL),
	fSound(NULL),
	fPdfKind(0),
	fFetcher(NULL),
	fFetchDone(0),
	fFetchTotal(0),
	fFetchOk(0),
	fFetchFailed(0)
{
	fMode = kModeBrowse;
	fCardView = new CardSurface(document);
	fCardFrame = new CardFrame(fCardView);
	fZoomField = NULL;
	fBanner = new BannerView();
	fMenuBar = new BMenuBar("menu");
	fTemplateMenu = NULL;
	fRecentMenu = NULL;

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar)
		.Add(fBanner)
		.Add(fCardFrame)
		.Add(new BSeparatorView(B_HORIZONTAL))
		.Add(_BuildStatusBar());
	fBanner->SetTarget(this);
	fBanner->Hide();
	SetKeyMenuBar(fMenuBar);
	_BuildMenus();
	// The modes are buttons in the status bar; the keys stay.
	AddShortcut('B', B_COMMAND_KEY, new BMessage(kMsgModeBrowse));
	AddShortcut('L', B_COMMAND_KEY, new BMessage(kMsgModeEdit));
	AddShortcut('F', B_COMMAND_KEY, new BMessage(kMsgModeFind));

	BString stackId = document->Id();
	// The window fits the card, so "fit to window" is gone: a stack that
	// had it keeps the scale it last had.
	fCardView->SetFitToWindow(false);
	fCardView->SetScale(gSettings.Zoom(stackId));
	_UpdateZoomMenus();

	{
		DocLocker lock(fDocument);
		bdb::Stack& stack = fDocument->GetStack();
		if (stack.CardCount() > 0)
			fCurrentId = stack.CardAt(0)->Id();
		if (gSettings.DebugLog())
			stack.Log().SetMinimumLevel(bdb::LogLevel::kDebug);
	}
	if (!startInEditMode)
		_EnsureCard();
	fCardView->SetCard(fCurrentId);

	// The window fits the card (UI 2.1); only its place is remembered.
	BRect frame;
	bool placed = gSettings.GetWindowFrame(stackId, frame);
	if (placed)
		MoveTo(frame.LeftTop());
	_FitWindow();
	if (!placed)
		CenterOnScreen();

	fPalette = new ToolPalette(BMessenger(this));
	fInspector = new InspectorWindow(BMessenger(this));
	fPalette->AddToSubset(this);
	fInspector->AddToSubset(this);
	fPalette->Hide();
	fInspector->Hide();
	fPalette->Show();
	fInspector->Show();

	fDocument->AddObserver(BMessenger(this));
	_UpdateTitle();
	_UpdateStatus();

	if (startInEditMode)
		_SetMode(kModeEdit);
	else
		fCardView->FocusFirstField();
}


StackWindow::~StackWindow()
{
	delete fFetcher;
	delete fAutosave;
	delete fImagePanel;
	delete fBackgroundPanel;
	delete fPdfPanel;
	delete fMediaPanel;
	delete fCardImagePanel;
	delete fSound;
}


// #pragma mark - construction


void
StackWindow::_BuildMenus()
{
	// The menus differ per mode (UI 10): a mode shows only its own menus.
	fMenuBar->RemoveItems(0, fMenuBar->CountItems(), true);
	fTemplateMenu = NULL;
	fRecentMenu = NULL;
	bool play = fMode == kModeBrowse;
	bool edit = fMode == kModeEdit;
	bool find = fMode == kModeFind;

	BMenu* menu = new BMenu(str::kMenuFile);
	menu->AddItem(Item(str::kNewStack, kMsgShowStart, 'N', B_SHIFT_KEY));
	if (play)
		menu->AddItem(Item(str::kNewStackFromCsv, kMsgNewStackCsv));
	menu->AddItem(Item(str::kOpenStack, kMsgOpenPanel, 'O'));
	if (play) {
		fRecentMenu = new BMenu(str::kRecentStacks);
		menu->AddItem(fRecentMenu);
		menu->AddSeparatorItem();
		menu->AddItem(Item(str::kSaveCardPdf, kMsgSaveCardPdf));
	}
	menu->AddSeparatorItem();
	menu->AddItem(Item(str::kPrefsMenu, kMsgPreferences, ','));
	menu->AddSeparatorItem();
	menu->AddItem(Item(str::kClose, B_QUIT_REQUESTED, 'W'));
	BMenuItem* quit = Item(str::kQuit, B_QUIT_REQUESTED, 'Q');
	menu->AddItem(quit);
	for (int32 i = 0; BMenuItem* item = menu->ItemAt(i); i++) {
		uint32 what = item->Message() != NULL ? item->Message()->what : 0;
		if (what == kMsgShowStart || what == kMsgOpenPanel
			|| what == kMsgNewStackCsv) {
			item->SetTarget(be_app);
		}
	}
	quit->SetTarget(be_app);
	fMenuBar->AddItem(menu);

	menu = new BMenu(str::kMenuEdit);
	menu->AddItem(Item(str::kUndo, kMsgUndo, 'Z'));
	if (!find)
		menu->AddItem(Item(str::kRedo, kMsgRedo, 'Z', B_SHIFT_KEY));
	menu->AddSeparatorItem();
	menu->AddItem(Item(str::kCut, kMsgCut, 'X'));
	menu->AddItem(Item(str::kCopy, kMsgCopy, 'C'));
	menu->AddItem(Item(str::kPaste, kMsgPaste, 'V'));
	menu->AddItem(Item(str::kSelectAll, kMsgSelectAll, 'A'));
	if (edit) {
		menu->AddSeparatorItem();
		menu->AddItem(Item(str::kDuplicatePart, kMsgDuplicatePart, 'D',
			B_SHIFT_KEY));
		menu->AddItem(Item(str::kDeletePart, kMsgDeletePart));
	}
	fMenuBar->AddItem(menu);

	if (edit) {
		menu = new BMenu(str::kMenuPart);
		menu->AddItem(Item(str::kBringToFront, kMsgBringToFront, ']'));
		menu->AddItem(Item(str::kSendToBack, kMsgSendToBack, '['));
		menu->AddSeparatorItem();
		menu->AddItem(Item(str::kLock, kMsgLock, 'K'));
		menu->AddItem(Item(str::kUnlock, kMsgUnlock, 'K', B_SHIFT_KEY));
		menu->AddItem(Item(str::kLockAll, kMsgLockAll));
		menu->AddItem(Item(str::kUnlockAll, kMsgUnlockAll));
		menu->AddSeparatorItem();
		menu->AddItem(Item(str::kChooseIcon, kMsgChooseIcon));
		menu->AddItem(Item(str::kSetLink, kMsgSetLink));
		fMenuBar->AddItem(menu);

		menu = new BMenu(str::kMenuTemplate);
		menu->AddItem(Item(str::kTemplateProperties, kMsgTemplateProps));
		menu->AddItem(Item(str::kPaperMenu, kMsgPaperDialog));
		menu->AddItem(Item(str::kBackgroundMenu, kMsgBackgroundDialog));
		menu->AddItem(Item(str::kThemeMenu, kMsgThemeDialog));
		menu->AddSeparatorItem();
		menu->AddItem(Item(str::kManageTemplates, kMsgManageTemplates));
		fMenuBar->AddItem(menu);
	}

	if (find) {
		menu = new BMenu(str::kMenuFind);
		menu->AddItem(Item(str::kRunFind, kMsgRunFind));
		menu->AddItem(Item(str::kClearFind, kMsgClearFind));
		menu->AddItem(Item(str::kShowAll, kMsgShowAll));
		fMenuBar->AddItem(menu);
	}

	if (play || find) {
		menu = new BMenu(str::kMenuCard);
		if (play) {
			menu->AddItem(Item(str::kNewCard, kMsgNewCard, 'N'));
			menu->AddItem(Item(str::kDuplicateCard, kMsgDuplicateCard, 'D'));
			menu->AddItem(Item(str::kDeleteCard, kMsgDeleteCard));
			menu->AddSeparatorItem();
		}
		menu->AddItem(Item(str::kPrevCard, kMsgPrevCard, B_LEFT_ARROW));
		menu->AddItem(Item(str::kNextCard, kMsgNextCard, B_RIGHT_ARROW));
		menu->AddItem(Item(str::kFirstCard, kMsgFirstCard, B_UP_ARROW));
		menu->AddItem(Item(str::kLastCard, kMsgLastCard, B_DOWN_ARROW));
		if (play) {
			menu->AddItem(Item(str::kGoToNumber, kMsgGoToNumber, 'G'));
			menu->AddSeparatorItem();
			fTemplateMenu = new BMenu(str::kChangeTemplate);
			menu->AddItem(fTemplateMenu);
			menu->AddItem(Item(str::kShowAll, kMsgShowAll));
		}
		fMenuBar->AddItem(menu);
	}

	menu = new BMenu(str::kMenuView);
	if (!edit)
		menu->AddItem(Item(str::kShowList, kMsgShowList, 'T'));
	if (edit) {
		BMenuItem* grid = Item(str::kShowGrid, kMsgToggleGrid);
		grid->SetMarked(fCardView->ShowGrid());
		menu->AddItem(grid);
		BMenuItem* snap = Item(str::kSnapToGrid, kMsgToggleSnap);
		snap->SetMarked(fCardView->Snap());
		menu->AddItem(snap);
	}
	menu->AddSeparatorItem();
	menu->AddItem(Item(str::kZoomIn, kMsgZoomIn, '+'));
	menu->AddItem(Item(str::kZoomOut, kMsgZoomOut, '-'));
	menu->AddItem(Item(str::kZoomActual, kMsgZoomActual, '0'));
	menu->AddSeparatorItem();
	for (int32 i = 0; i < kZoomLevelCount; i++) {
		BMessage* message = new BMessage(kMsgZoomSet);
		message->AddFloat("zoom", kZoomLevels[i]);
		BString label;
		label << int32(kZoomLevels[i] * 100 + 0.5f) << "%";
		menu->AddItem(new BMenuItem(label, message));
	}
	fMenuBar->AddItem(menu);

	if (play) {
		menu = new BMenu(str::kMenuStack);
		menu->AddItem(Item(str::kImportCsv, kMsgImportCsv));
		menu->AddItem(Item(str::kFetchImages, kMsgFetchImages));
		menu->AddSeparatorItem();
		menu->AddItem(Item(str::kCleanup, kMsgCleanup));
		menu->AddItem(Item(str::kProblems, kMsgProblems));
		menu->AddItem(Item(str::kManageTemplates, kMsgManageTemplates));
		menu->AddItem(Item(str::kStackSettings, kMsgStackSettings));
		menu->AddSeparatorItem();
		menu->AddItem(Item(str::kOpenLog, kMsgOpenLog));
		fMenuBar->AddItem(menu);
	}

	menu = new BMenu(str::kMenuHelp);
	menu->AddItem(Item(str::kAbout, kMsgAbout));
	fMenuBar->AddItem(menu);
}


BView*
StackWindow::_BuildStatusBar()
{
	// No navigation buttons in the window (UI 2.1): the position, the
	// template (click: edit mode), and notices.
	fPositionText = new BStringView("position", "");
	fTemplateLink = new StatusLink();
	fStatusText = new BStringView("status", "");
	// A long notice (a link's URL) is cut, rather than widening the window:
	// a string view's minimum width is its whole text otherwise.
	fStatusText->SetExplicitMinSize(BSize(16, B_SIZE_UNSET));
	fStatusText->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fStatusText->SetTruncation(B_TRUNCATE_END);
	fShowAllButton = new BButton("all", str::kShowAll,
		new BMessage(kMsgShowAll));
	fShowAllButton->SetFlags(fShowAllButton->Flags() & ~B_NAVIGABLE);
	// A long template name is cut rather than widening the window.
	fTemplateLink->SetExplicitMinSize(BSize(40, B_SIZE_UNSET));
	fTemplateLink->SetTruncation(B_TRUNCATE_END);

	// The modes: the current one is green, edit mode red.
	fModeButtons[0] = new ModeButton(str::kModeButtonPlay, kMsgModeBrowse,
		make_color(60, 160, 70));
	fModeButtons[1] = new ModeButton(str::kModeButtonFind, kMsgModeFind,
		make_color(60, 160, 70));
	fModeButtons[2] = new ModeButton(str::kModeButtonEdit, kMsgModeEdit,
		make_color(205, 55, 45));

	// The zoom in percent.
	BPopUpMenu* zooms = new BPopUpMenu("zoom");
	for (int32 i = 0; i < kZoomLevelCount; i++) {
		BMessage* message = new BMessage(kMsgZoomSet);
		message->AddFloat("zoom", kZoomLevels[i]);
		BString label;
		label << int32(kZoomLevels[i] * 100 + 0.5f) << "%";
		zooms->AddItem(new BMenuItem(label, message));
	}
	fZoomField = new BMenuField("zoom", NULL, zooms);
	fZoomField->SetToolTip(str::kZoomTip);
	fZoomField->SetExplicitMaxSize(BSize(ceilf(be_plain_font->StringWidth("300%")) + 44, B_SIZE_UNSET));

	BView* bar = new BView("status bar", 0);
	BLayoutBuilder::Group<>(bar, B_HORIZONTAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_SMALL_INSETS, 2, B_USE_SMALL_INSETS, 2)
		.AddGroup(B_HORIZONTAL, 2)
			.Add(fModeButtons[0])
			.Add(fModeButtons[1])
			.Add(fModeButtons[2])
		.End()
		.AddStrut(6)
		.Add(fPositionText)
		.Add(new BStringView("dot", "・"))
		.Add(fTemplateLink)
		.AddStrut(8)
		.Add(fStatusText)
		.Add(fShowAllButton)
		.Add(fZoomField);
	bar->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fShowAllButton->Hide();
	return bar;
}


// #pragma mark - status


void
StackWindow::_UpdateTitle()
{
	DocLocker lock(fDocument);
	bdb::Stack& stack = fDocument->GetStack();
	BString title = stack.Title().c_str();
	if (fMode == kModeEdit) {
		bdb::Template* tmpl = stack.FindTemplate(fCardView->TemplateId());
		BString edit;
		edit.SetToFormat(str::kWindowTitleEdit.Get(), title.String(),
			tmpl != NULL ? tmpl->Title().c_str() : "");
		title = edit;
	} else if (fMode == kModeFind) {
		BString find;
		find.SetToFormat(str::kWindowTitleFind.Get(), title.String());
		title = find;
	}
	SetTitle(title.String());
}


void
StackWindow::_UpdateBanner()
{
	if (fMode == kModeBrowse) {
		if (!fBanner->IsHidden(fBanner))
			fBanner->Hide();
		return;
	}
	BString text;
	if (fMode == kModeEdit) {
		DocLocker lock(fDocument);
		bdb::Stack& stack = fDocument->GetStack();
		std::string templateId = fCardView->TemplateId();
		bdb::Template* tmpl = stack.FindTemplate(templateId);
		text.SetToFormat(str::kBannerEdit.Get(),
			tmpl != NULL ? tmpl->Title().c_str() : "",
			(int)stack.TemplateUsage(templateId));
	} else
		text = str::kBannerFind.Get();
	fBanner->Set(fMode, text);
	if (fBanner->IsHidden(fBanner))
		fBanner->Show();
}


void
StackWindow::_UpdateStatus()
{
	DocLocker lock(fDocument);
	bdb::Stack& stack = fDocument->GetStack();
	std::vector<std::string> list = _NavigationList();
	int32 index = _CurrentNavIndex();
	bool provisional = stack.IsProvisional(fCurrentId);
	int32 count = int32(list.size());
	int32 position = index + 1;
	if (provisional) {
		// The provisional card counts as the current card (8.6).
		count++;
		position = count;
		std::vector<std::string> all = list;
		(void)all;
	}
	fCardView->SetNavigation(position, count, fFindActive);

	BString text;
	if (fFindActive) {
		text.SetToFormat(str::kStatusFound.Get(), (int)position, (int)count);
	} else
		text.SetToFormat(str::kStatusPosition.Get(), (int)position,
			(int)count);
	fPositionText->SetText(text);

	const bdb::Card* card = stack.FindCard(fCurrentId);
	std::string templateId = fMode == kModeEdit ? fCardView->TemplateId()
		: card != NULL ? card->TemplateId() : stack.DefaultTemplateId();
	bdb::Template* tmpl = stack.FindTemplate(templateId);
	BString templateText;
	templateText.SetToFormat(str::kStatusTemplate.Get(),
		tmpl != NULL ? tmpl->Title().c_str() : "-");
	fTemplateLink->SetText(templateText);

	BString status;
	auto append = [&status](const char* text) {
		if (!status.IsEmpty())
			status << "   ";
		status << text;
	};
	fModeButtons[0]->SetSelected(fMode == kModeBrowse);
	fModeButtons[1]->SetSelected(fMode == kModeFind);
	fModeButtons[2]->SetSelected(fMode == kModeEdit);
	if (provisional)
		append(str::kStatusNewCard);
	else if (list.empty() && fMode == kModeBrowse)
		append(str::kStatusNoCards);
	if (fSaveFailed)
		append(str::kStatusSaveFailed);
	if (!stack.Problems().empty()) {
		BString problems;
		problems.SetToFormat(str::kStatusProblems.Get(),
			(int)stack.Problems().size());
		append(problems);
	}
	if (fFetcher != NULL && fFetchTotal > 0) {
		BString fetching;
		fetching.SetToFormat(str::kStatusFetching.Get(), (int)fFetchDone,
			(int)fFetchTotal);
		append(fetching);
	}
	if (!fHint.IsEmpty())
		append(fHint);
	fStatusText->SetText(status);

	bool showAll = fMode == kModeBrowse && fFindActive;
	if (fShowAllButton->IsHidden(fShowAllButton) == showAll) {
		if (showAll)
			fShowAllButton->Show();
		else
			fShowAllButton->Hide();
	}
}


void
StackWindow::_Hint(const char* text)
{
	fHint = text;
	_UpdateStatus();
}


void
StackWindow::MenusBeginning()
{
	BWindow::MenusBeginning();
	bool play = fMode == kModeBrowse;
	bool edit = fMode == kModeEdit;
	bool hasCard, real;
	{
		DocLocker lock(fDocument);
		hasCard = fDocument->GetStack().FindCard(fCurrentId) != NULL;
		real = hasCard && !fDocument->GetStack().IsProvisional(fCurrentId);
	}
	bool selection = !fCardView->Selection().empty();
	bool unlocked = edit && fCardView->SelectionHasUnlocked();
	bool single = edit && fCardView->Selection().size() == 1 && unlocked;

	struct {
		uint32	what;
		bool	enabled;
	} states[] = {
		{ kMsgDuplicateCard, play && real },
		{ kMsgDeleteCard, play && real },
		{ kMsgSaveCardPdf, play && hasCard },
		{ kMsgShowAll, fFindActive || fMode == kModeFind },
		{ kMsgDuplicatePart, edit && selection },
		{ kMsgDeletePart, unlocked },
		{ kMsgBringToFront, unlocked },
		{ kMsgSendToBack, unlocked },
		{ kMsgLock, unlocked },
		{ kMsgUnlock, edit && fCardView->SelectionHasLocked() },
		{ kMsgChooseIcon, single },
		{ kMsgSetLink, single },
		{ kMsgUndo, edit ? fCardView->CanUndo() : true },
		{ kMsgRedo, edit ? fCardView->CanRedo() : true },
	};
	for (const auto& state : states) {
		if (BMenuItem* item = fMenuBar->FindItem(state.what))
			item->SetEnabled(state.enabled);
	}
	_UpdateZoomMenus();

	if (fTemplateMenu != NULL) {
		// Templates for "テンプレートを変更"
		fTemplateMenu->RemoveItems(0, fTemplateMenu->CountItems(), true);
		DocLocker lock(fDocument);
		bdb::Stack& stack = fDocument->GetStack();
		const bdb::Card* card = stack.FindCard(fCurrentId);
		for (bdb::Template* tmpl : stack.Templates()) {
			BMessage* message = new BMessage(kMsgChangeTemplate);
			message->AddString("template", tmpl->Id().c_str());
			BMenuItem* item = new BMenuItem(tmpl->Title().c_str(), message);
			item->SetMarked(card != NULL
				&& card->TemplateId() == tmpl->Id());
			fTemplateMenu->AddItem(item);
		}
		fTemplateMenu->SetEnabled(play && hasCard);
	}
	if (fRecentMenu != NULL) {
		fRecentMenu->RemoveItems(0, fRecentMenu->CountItems(), true);
		for (int32 i = 0; i < gSettings.CountRecent(); i++) {
			BString path = gSettings.RecentAt(i);
			BMessage* message = new BMessage(kMsgOpenPath);
			message->AddString("path", path);
			BMenuItem* item = new BMenuItem(
				RecentStackLabel(path.String()).String(), message);
			item->SetTarget(be_app);
			fRecentMenu->AddItem(item);
		}
	}
}


// #pragma mark - navigation


std::vector<std::string>
StackWindow::_NavigationList() const
{
	DocLocker lock(fDocument);
	bdb::Stack& stack = fDocument->GetStack();
	std::vector<std::string> list;
	if (fFindActive) {
		for (const std::string& id : fFound) {
			if (stack.FindCard(id) != NULL && !stack.IsProvisional(id))
				list.push_back(id);
		}
		return list;
	}
	list.reserve(stack.CardCount());
	for (int32 i = 0; i < stack.CardCount(); i++)
		list.push_back(stack.CardAt(i)->Id());
	return list;
}


int32
StackWindow::_CurrentNavIndex() const
{
	if (fCurrentId.empty())
		return -1;
	DocLocker lock(fDocument);
	if (!fFindActive)
		return fDocument->GetStack().IndexOf(fCurrentId);
	std::vector<std::string> list = _NavigationList();
	auto found = std::find(list.begin(), list.end(), fCurrentId);
	return found == list.end() ? -1 : int32(found - list.begin());
}


// Leaves the current card: saves the input (also text not yet confirmed,
// UI 5.3) and drops an untouched provisional card (8.6). False when the
// card could not be saved: the caller stays on the card.
bool
StackWindow::_LeaveCard()
{
	if (fMode == kModeBrowse)
		_CommitFields();
	if (!_SaveCurrent(true))
		return false;
	DocLocker lock(fDocument);
	bdb::Stack& stack = fDocument->GetStack();
	if (stack.IsProvisional(fCurrentId)) {
		stack.DiscardProvisional();
		fCurrentId.clear();
	}
	return true;
}


bool
StackWindow::_GoTo(const std::string& cardId)
{
	if (cardId == fCurrentId && !cardId.empty())
		return true;
	if (!_LeaveCard())
		return false;
	fCurrentId = cardId;
	fCardView->SetCard(cardId);
	if (fMode == kModeBrowse)
		fCardView->FocusFirstField();
	{
		DocLocker lock(fDocument);
		fDocument->GetStack().Log().Debug("ui.navigate", {{"card", cardId}});
	}
	_UpdateStatus();
	return true;
}


void
StackWindow::_GoToIndex(int32 index)
{
	std::vector<std::string> list = _NavigationList();
	if (list.empty()) {
		_EnsureCard();
		return;
	}
	index = std::max(int32(0), std::min(index, int32(list.size()) - 1));
	_GoTo(list[index]);
}


void
StackWindow::_Step(int32 delta)
{
	int32 index = _CurrentNavIndex();
	_GoToIndex(index < 0 ? 0 : index + delta);
}


// A stack without cards shows a provisional new card in play mode (UI 5.5).
void
StackWindow::_EnsureCard()
{
	if (fMode != kModeBrowse)
		return;
	std::string id;
	{
		DocLocker lock(fDocument);
		bdb::Stack& stack = fDocument->GetStack();
		if (stack.CardCount() > 0 || fFindActive) {
			if (stack.FindCard(fCurrentId) == NULL && stack.CardCount() > 0)
				fCurrentId = stack.CardAt(0)->Id();
			fCardView->SetCard(fCurrentId);
			return;
		}
		if (stack.IsProvisional(fCurrentId))
			return;
		std::string templateId = stack.DefaultTemplateId();
		if (templateId.empty())
			return;
		stack.CreateProvisionalCard(templateId, "", &id);
	}
	fCurrentId = id;
	fCardView->SetCard(id);
	fCardView->FocusFirstField();
	_UpdateStatus();
}


void
StackWindow::_GoToNumberDialog()
{
	int32 index = _CurrentNavIndex();
	BString current;
	current << (index >= 0 ? index + 1 : 1);
	AskText(str::kGoToTitle, str::kGoToLabel, current, BMessenger(this),
		kMsgGoToNumberDone);
}


// #pragma mark - values


void
StackWindow::_FieldEdited(const char* partId)
{
	if (fMode != kModeBrowse)
		return;
	FieldTextView* field = fCardView->FindField(partId);
	if (field == NULL || field->IsNumber())
		return;	// numbers are validated on commit (UI spec 5.3)
	DocLocker lock(fDocument);
	bdb::Card* card = fDocument->GetStack().FindCard(fCurrentId);
	if (card == NULL)
		return;
	card->SetValue(partId, Json(field->Text()));
	_MarkDirty();
}


bool
StackWindow::_CommitNumber(const char* partId, const char* text)
{
	FieldTextView* field = fCardView->FindField(partId);
	DocLocker lock(fDocument);
	bdb::Stack& stack = fDocument->GetStack();
	bdb::Card* card = stack.FindCard(fCurrentId);
	bdb::Template* tmpl = card != NULL ? stack.FindTemplate(card->TemplateId())
		: NULL;
	const Json* part = tmpl != NULL ? tmpl->FindPart(partId) : NULL;
	if (card == NULL || part == NULL || field == NULL)
		return true;
	// A guarded field keeps the old value until the user agreed to
	// overwrite it (also covers commits from views being removed).
	if (field->IsGuarded())
		return true;

	std::string trimmed = bdb::TrimSpaces(text);
	Json value;
	bool ok = true;
	if (!trimmed.empty()) {
		double number;
		if (bdb::ParseDecimal(trimmed, number) && bdb::IsValidNumber(number))
			value = bdb::MakeNumberValue(number);
		else
			ok = false;
	}
	if (ok) {
		const Json* stored = card->Value(partId);
		bool unchanged = stored != NULL && *stored == value
			&& stored->GetType() != Json::kString;
		if (!unchanged && !(stored == NULL && value.IsNull()
				&& trimmed.empty())) {
			card->SetValue(partId, value);
			_MarkDirty();
		}
	}

	// Show the (possibly reverted) value.
	const Json& effective = bdb::EffectiveValue(*card, *part);
	BString raw = bdb::ValueText(effective).c_str();
	field->SetNumberTexts(FormatNumberValue(effective, *part), raw);
	if (!ok)
		_Notice(str::kNumberInvalid);
	return ok;
}


void
StackWindow::_CommitFields()
{
	if (fMode != kModeBrowse)
		return;
	BView* focus = CurrentFocus();
	FieldTextView* field = dynamic_cast<FieldTextView*>(focus);
	if (field != NULL && field->IsNumber())
		_CommitNumber(field->PartId(), field->Text());
}


void
StackWindow::_MarkDirty()
{
	fDirty = true;
	delete fAutosave;
	BMessage message(kMsgAutosave);
	// Save when typing pauses for 2 seconds (UI spec 11.1).
	fAutosave = new BMessageRunner(BMessenger(this), &message, 2000000, 1);
}


bool
StackWindow::_SaveCurrent(bool interactive)
{
	delete fAutosave;
	fAutosave = NULL;
	if (!fDirty)
		return true;

	bdb::Status status;
	bool provisional;
	{
		DocLocker lock(fDocument);
		provisional = fDocument->GetStack().IsProvisional(fCurrentId);
		// A provisional card is saved only when it differs from the
		// defaults (8.6).
		bdb::Stack& stack = fDocument->GetStack();
		status = stack.SaveCard(fCurrentId);
		if (status && provisional && !stack.IsProvisional(fCurrentId)) {
			// Saved as a normal card (8.6). A clash of file names gives
			// it a new ID; it is the newest card then.
			if (stack.FindCard(fCurrentId) == NULL && stack.CardCount() > 0) {
				fCurrentId = stack.CardAt(stack.CardCount() - 1)->Id();
				fCardView->SetCard(fCurrentId);
			}
			provisional = false;
			_NotifyCardsChanged();
		}
	}
	if (status.Kind() == bdb::ErrorKind::kConflict) {
		BAlert* alert = new BAlert(str::kAppName, str::kConflict,
			str::kReload, str::kOverwrite, NULL, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		int32 choice = alert->Go();
		DocLocker lock(fDocument);
		if (choice == 1)
			status = fDocument->GetStack().SaveCard(fCurrentId, true);
		else {
			status = fDocument->GetStack().ReloadCard(fCurrentId);
			if (status) {
				fDirty = false;
				fSaveFailed = false;
				fCardView->Rebuild();
				_UpdateStatus();
				return true;
			}
		}
	}
	if (status) {
		fDirty = false;
		bool wasFailed = fSaveFailed;
		fSaveFailed = false;
		_NotifyCardChanged(fCurrentId);
		if (wasFailed || !provisional)
			_UpdateStatus();
		return true;
	}

	// Keep the values in memory and retry later (UI spec 11.2).
	bool firstFailure = !fSaveFailed;
	fSaveFailed = true;
	_UpdateStatus();
	if (interactive || firstFailure)
		_ShowError(str::kSaveFailed, status);
	return false;
}


bool
StackWindow::_ConfirmGuarded(const char* partId)
{
	// Several queued clicks or keys may ask; warn only once.
	if (!fCardView->IsGuarded(partId))
		return true;
	BAlert* alert = new BAlert(str::kAppName, str::kOldValueWarning,
		str::kCancel, str::kContinue, NULL, B_WIDTH_AS_USUAL,
		B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	if (alert->Go() != 1)
		return false;
	fCardView->SetGuarded(partId, false);
	if (FieldTextView* field = fCardView->FindField(partId))
		field->MakeFocus(true);
	return true;
}


void
StackWindow::_ToggleBool(const char* partId)
{
	if (fMode != kModeBrowse)
		return;
	if (fCardView->IsGuarded(partId) && !_ConfirmGuarded(partId))
		return;
	if (fCardView->IsPartLocked(partId)) {
		_Hint(str::kPartLocked);
		return;
	}
	_CommitFields();
	bool effect = false;
	{
		DocLocker lock(fDocument);
		bdb::Stack& stack = fDocument->GetStack();
		bdb::Card* card = stack.FindCard(fCurrentId);
		bdb::Template* tmpl = card != NULL
			? stack.FindTemplate(card->TemplateId()) : NULL;
		const Json* part = tmpl != NULL ? tmpl->FindPart(partId) : NULL;
		if (part == NULL)
			return;
		const Json& value = bdb::EffectiveValue(*card, *part);
		// null shows as off; a click turns it on (UI 5.3).
		bool on = value.IsBool() && value.AsBool();
		card->SetValue(partId, Json(!on));
		effect = !bdb::part::SwitchEffect(*part).empty();
	}
	_MarkDirty();
	_SaveCurrent(true);
	if (effect) {
		// The switch locks or hides parts: the inputs change.
		fCardView->Rebuild();
	} else
		fCardView->Invalidate();
}


// #pragma mark - cards


void
StackWindow::_NewCard()
{
	{
		DocLocker lock(fDocument);
		bdb::Stack& stack = fDocument->GetStack();
		// An untouched provisional card stays as it is (UI 5.5).
		if (stack.IsProvisional(fCurrentId) && !fDirty)
			return;
	}
	_CommitFields();
	if (!_SaveCurrent(true))
		return;
	std::string newId;
	bdb::Status status;
	{
		DocLocker lock(fDocument);
		bdb::Stack& stack = fDocument->GetStack();
		if (stack.IsProvisional(fCurrentId))
			return;		// typed and back to the defaults: still new
		const bdb::Card* current = stack.FindCard(fCurrentId);
		std::string templateId = current != NULL
			&& !stack.IsTemplateMissing(*current)
			? current->TemplateId() : stack.DefaultTemplateId();
		if (templateId.empty()) {
			status = stack.CreateTemplate(bdb::kUntitled,
				bdb::kDefaultCardWidth, bdb::kDefaultCardHeight, &templateId);
		}
		if (status) {
			int32 index = current != NULL ? stack.IndexOf(fCurrentId) : -1;
			status = stack.CreateCard(templateId, index, &newId);
		}
	}
	if (!status) {
		_ShowError(str::kSaveFailed, status);
		return;
	}
	if (fFindActive) {
		auto position = std::find(fFound.begin(), fFound.end(), fCurrentId);
		fFound.insert(position == fFound.end() ? position : position + 1,
			newId);
	}
	_NotifyCardsChanged();
	_GoTo(newId);
}


void
StackWindow::_DuplicateCard()
{
	_CommitFields();
	if (!_SaveCurrent(true))
		return;
	std::string newId;
	bdb::Status status;
	{
		DocLocker lock(fDocument);
		if (fDocument->GetStack().IsProvisional(fCurrentId))
			return;
		status = fDocument->GetStack().DuplicateCard(fCurrentId, &newId);
	}
	if (!status) {
		_ShowError(str::kSaveFailed, status);
		return;
	}
	if (fFindActive) {
		auto position = std::find(fFound.begin(), fFound.end(), fCurrentId);
		fFound.insert(position == fFound.end() ? position : position + 1,
			newId);
	}
	_NotifyCardsChanged();
	_GoTo(newId);
}


void
StackWindow::_DeleteCard()
{
	if (fCurrentId.empty())
		return;
	{
		DocLocker lock(fDocument);
		if (fDocument->GetStack().IsProvisional(fCurrentId))
			return;
	}
	BAlert* alert = new BAlert(str::kAppName, str::kDeleteCardQuestion,
		str::kCancel, str::kDelete, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	if (alert->Go() != 1)
		return;

	int32 index = _CurrentNavIndex();
	bdb::Status status;
	{
		DocLocker lock(fDocument);
		status = fDocument->GetStack().DeleteCard(fCurrentId);
	}
	if (!status) {
		_ShowError(str::kSaveFailed, status);
		return;
	}
	fDirty = false;
	fSaveFailed = false;
	delete fAutosave;
	fAutosave = NULL;
	fCurrentId.clear();
	_NotifyCardsChanged();
	_GoToIndex(index);
}


void
StackWindow::_ChangeTemplate(const char* templateId)
{
	_CommitFields();
	if (!_SaveCurrent(true))
		return;
	bdb::Status status;
	{
		DocLocker lock(fDocument);
		status = fDocument->GetStack().ChangeCardTemplate(fCurrentId,
			templateId);
	}
	if (!status) {
		_ShowError(str::kSaveFailed, status);
		return;
	}
	fCardView->Rebuild();
	_NotifyCardChanged(fCurrentId);
	_UpdateStatus();
}


void
StackWindow::_ButtonAction(const char* partId, const char* action)
{
	{
		DocLocker lock(fDocument);
		fDocument->GetStack().Log().Debug("ui.button", {{"part", partId},
			{"action", action}});
	}
	BString name(action);
	if (fMode == kModeFind) {
		// Only the find actions work in find mode (UI 6.10).
		if (name == "find.run")
			_RunFind();
		else if (name == "find.clear") {
			_ShowAll();
			_SetMode(kModeBrowse);
		} else if (name == "find.start")
			fCardView->ClearFindConditions();
		return;
	}
	if (fMode != kModeBrowse)
		return;
	if (name == "card.first")
		_GoToIndex(0);
	else if (name == "card.prev")
		_Step(-1);
	else if (name == "card.next")
		_Step(1);
	else if (name == "card.last")
		_GoToIndex(INT32_MAX);
	else if (name == "card.new")
		_NewCard();
	else if (name == "card.duplicate")
		_DuplicateCard();
	else if (name == "card.delete")
		_DeleteCard();
	else if (name == "list.open")
		_ShowList();
	else if (name == "link.open")
		_OpenLink(partId);
	else if (name == "find.start")
		_SetMode(kModeFind);
	else if (name == "find.clear")
		_ShowAll();
	else if (name == "card.pdf")
		PostMessage(kMsgSaveCardPdf);
	else if (name == "card.image")
		_ChooseCardImageFile();
	else if (name == "sound.play")
		_PlaySound(partId);
	else if (name == "video.play")
		_PlayVideo(partId);
	// Unknown or missing actions do nothing (data spec 7).
}


void
StackWindow::_OpenLink(const char* partId)
{
	if (fMode != kModeBrowse)
		return;
	std::string url, scheme;
	bdb::LinkCheck check;
	{
		DocLocker lock(fDocument);
		bdb::Stack& stack = fDocument->GetStack();
		const bdb::Card* card = stack.FindCard(fCurrentId);
		const bdb::Template* tmpl = card != NULL
			? stack.FindTemplate(card->TemplateId()) : NULL;
		const Json* part = tmpl != NULL ? tmpl->FindPart(partId) : NULL;
		if (part == NULL)
			return;
		check = bdb::ResolveLink(*part, *tmpl, card, url, &scheme);
		if (check == bdb::LinkCheck::kEmpty || check == bdb::LinkCheck::kNone)
			return;	// empty links do nothing (I/O 2.2)
		if (check != bdb::LinkCheck::kOk) {
			// Reason and scheme only; never the URL (I/O 2.2).
			stack.Log().Warn("link.blocked", {{"part", partId},
				{"reason", bdb::LinkCheckCode(check)}, {"scheme", scheme}});
		}
	}
	if (check != bdb::LinkCheck::kOk) {
		BString text;
		text.SetToFormat(str::kLinkBlocked.Get(), bdb::LinkCheckCode(check));
		_Hint(text);
		return;
	}
	BString stackId = fDocument->Id();
	if (!gSettings.TrustsLinks(stackId)) {
		// The first link of a stack asks (I/O 2.3).
		BString text;
		text.SetToFormat(str::kLinkQuestion.Get(), url.c_str());
		BAlert* alert = new BAlert(str::kAppName, text, str::kCancel,
			str::kLinkTrust, str::kOpen, B_WIDTH_AS_USUAL, B_OFFSET_SPACING,
			B_INFO_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		int32 choice = alert->Go();
		if (choice == 0)
			return;
		if (choice == 1) {
			gSettings.SetTrustsLinks(stackId);
			gSettings.Save();
		}
	}
	bool fallback = false;
	if (OpenUrl(url.c_str(), &fallback) != B_OK)
		_Hint(str::kLinkOpenFailed);
	else if (fallback)
		_Hint(str::kPrefsBrowserMissing);
	DocLocker lock(fDocument);
	fDocument->GetStack().Log().Debug("link.open", {{"part", partId},
		{"scheme", scheme}});
}


// #pragma mark - images


static std::string
ExtensionFor(const BPath& path)
{
	BString name = path.Leaf();
	int32 dot = name.FindLast('.');
	if (dot > 0) {
		BString ext;
		name.CopyInto(ext, dot + 1, name.Length() - dot - 1);
		ext.ToLower();
		if (bdb::IsValidExtension(ext.String()))
			return ext.String();
	}
	// No usable extension: ask the Translation Kit (UI spec 5.4).
	BFile file(path.Path(), B_READ_ONLY);
	translator_info info;
	if (BTranslatorRoster::Default()->Identify(&file, NULL, &info) == B_OK) {
		BString mime = info.MIME;
		int32 slash = mime.FindFirst('/');
		if (slash >= 0) {
			BString ext;
			mime.CopyInto(ext, slash + 1, mime.Length() - slash - 1);
			ext.RemoveAll("x-");
			ext.ToLower();
			if (ext == "jpeg")
				ext = "jpg";
			if (bdb::IsValidExtension(ext.String()))
				return ext.String();
		}
	}
	return "img";
}


static bool
IsReadableImage(const BPath& path)
{
	if (BString(path.Leaf()).IEndsWith(".hvif"))
		return true;
	BBitmap* bitmap = BTranslationUtils::GetBitmapFile(path.Path());
	if (bitmap == NULL)
		return false;
	delete bitmap;
	return true;
}


void
StackWindow::_SetImage(const char* partId, const entry_ref& ref)
{
	if (fMode != kModeBrowse)
		return;
	if (fCardView->IsGuarded(partId) && !_ConfirmGuarded(partId))
		return;

	BPath path(&ref);
	if (!IsReadableImage(path)) {
		_Notice(str::kImageUnreadable);
		return;
	}
	_CommitFields();
	bdb::Status status;
	{
		DocLocker lock(fDocument);
		bdb::FileInStream source(path.Path());
		status = fDocument->GetStack().SetMedia(fCurrentId, partId, "image",
			ExtensionFor(path), source);
	}
	if (!status) {
		_ShowError(str::kSaveFailed, status);
		return;
	}
	fDirty = false;
	fCardView->Rebuild();
	_NotifyCardsChanged();
	_UpdateStatus();
}


void
StackWindow::_ChooseImage(const char* partId)
{
	if (fCardView->IsGuarded(partId) && !_ConfirmGuarded(partId))
		return;
	BMessage message(kMsgImagePanelDone);
	message.AddString("part", partId);
	if (fImagePanel == NULL) {
		BMessenger target(this);
		fImagePanel = new BFilePanel(B_OPEN_PANEL, &target, NULL,
			B_FILE_NODE, false);
	}
	fImagePanel->SetMessage(&message);
	fImagePanel->Show();
}


void
StackWindow::_ClearImage(const char* partId)
{
	bdb::Status status;
	{
		DocLocker lock(fDocument);
		status = fDocument->GetStack().ClearMedia(fCurrentId, partId);
	}
	if (!status) {
		_ShowError(str::kSaveFailed, status);
		return;
	}
	fDirty = false;
	fCardView->Rebuild();
	_NotifyCardChanged(fCurrentId);
}


// #pragma mark - modes


// Beside the stack window and inside the screen: the palette, and the
// inspector below it, as tall as the screen allows. The left side is used
// when the right one has no room.
void
StackWindow::_PlaceToolWindows()
{
	const float kGap = 12;
	const float kTab = 24;
	BRect screen = BScreen(this).Frame().InsetByCopy(4, 4);
	BRect window = Frame();
	BSize palette(160, 400);
	if (fPalette->Lock()) {
		palette = fPalette->GetLayout()->PreferredSize();
		fPalette->Unlock();
	}
	float inspectorWidth = 280;
	if (fInspector->Lock()) {
		inspectorWidth = std::max(fInspector->Frame().Width(),
			fInspector->GetLayout()->MinSize().width);
		fInspector->Unlock();
	}
	float width = std::max(palette.width, inspectorWidth);
	float x = window.right + kGap;
	if (x + width > screen.right)
		x = std::max(screen.left, window.left - kGap - width);
	float y = std::max(window.top, screen.top + kTab);
	fPalette->MoveTo(x, y);
	float top = y + palette.height + kGap + kTab;
	float height = std::min(520.0f, screen.bottom - top);
	if (height < 240) {
		height = 240;
		top = screen.bottom - height;
	}
	fInspector->ResizeTo(inspectorWidth, height);
	fInspector->MoveTo(x, top);
}


void
StackWindow::_SetMode(Mode mode)
{
	if (mode == fMode)
		return;
	if (fMode == kModeBrowse) {
		// Leaving play mode saves like moving to another card (UI 5.3).
		if (!_LeaveCard())
			return;
	} else if (fMode == kModeEdit) {
		if (!_LeaveEdit())
			return;
	}

	if (mode == kModeEdit) {
		std::string templateId;
		{
			DocLocker lock(fDocument);
			bdb::Stack& stack = fDocument->GetStack();
			const bdb::Card* card = stack.FindCard(fCurrentId);
			templateId = card != NULL && !stack.IsTemplateMissing(*card)
				? card->TemplateId() : stack.DefaultTemplateId();
			if (templateId.empty()) {
				stack.CreateTemplate(bdb::kUntitled, bdb::kDefaultCardWidth,
					bdb::kDefaultCardHeight, &templateId);
			}
		}
		fTemplateDirty = false;
		fCardView->SetEditTemplate(templateId);
	}

	fMode = mode;
	fCardView->SetMode(mode);
	_BuildMenus();
	if (mode == kModeEdit) {
		_PlaceToolWindows();
		if (fPalette->Lock()) {
			if (fPalette->IsHidden())
				fPalette->Show();
			fPalette->Unlock();
		}
		if (fInspector->Lock()) {
			if (fInspector->IsHidden())
				fInspector->Show();
			fInspector->Unlock();
		}
		_SetCreateItem(-1);
		_UpdateInspector();
		fCardView->MakeFocus(true);
	} else {
		if (fPalette->Lock()) {
			if (!fPalette->IsHidden())
				fPalette->Hide();
			fPalette->Unlock();
		}
		if (fInspector->Lock()) {
			if (!fInspector->IsHidden())
				fInspector->Hide();
			fInspector->Unlock();
		}
		if (mode == kModeBrowse) {
			_EnsureCard();
			fCardView->SetCard(fCurrentId);
		}
		fCardView->FocusFirstField();
	}
	{
		DocLocker lock(fDocument);
		const char* names[] = { "play", "edit", "find" };
		fDocument->GetStack().Log().Debug("ui.mode", {{"mode", names[mode]}});
	}
	_UpdateTitle();
	_UpdateBanner();
	_UpdateStatus();
	_FitWindow();
	if (mode == kModeEdit)
		_PlaceToolWindows();
}


bool
StackWindow::_LeaveEdit()
{
	fCardView->CommitLabelEditor();
	std::string templateId = fCardView->TemplateId();
	if (fTemplateDirty) {
		bdb::Status status;
		{
			DocLocker lock(fDocument);
			status = fDocument->GetStack().SaveTemplate(templateId);
		}
		if (status.Kind() == bdb::ErrorKind::kConflict) {
			BAlert* alert = new BAlert(str::kAppName, str::kTemplateConflict,
				str::kReload, str::kOverwrite, NULL, B_WIDTH_AS_USUAL,
				B_WARNING_ALERT);
			int32 choice = alert->Go();
			DocLocker lock(fDocument);
			if (choice == 1)
				status = fDocument->GetStack().SaveTemplate(templateId, true);
			else
				status = fDocument->GetStack().ReloadTemplate(templateId);
		}
		if (!status) {
			_ShowError(str::kTemplateSaveFailed, status);
			return false;
		}
		fTemplateDirty = false;
		BMessage notice(kMsgDocTemplateChanged);
		notice.AddString("template", templateId.c_str());
		notice.AddPointer("source", this);
		fDocument->Notify(&notice);
	}

	// Remaining unset frames (UI spec 6.7).
	int32 blanks = 0;
	{
		DocLocker lock(fDocument);
		bdb::Template* tmpl = fDocument->GetStack().FindTemplate(templateId);
		if (tmpl != NULL) {
			for (const Json& part : tmpl->Parts()) {
				if (bdb::part::Kind(part) == bdb::kKindBlank)
					blanks++;
			}
		}
	}
	if (blanks > 0) {
		BString text;
		text.SetToFormat(str::kUnsetFramesLeft.Get(), (int)blanks);
		_Notice(text.String());
	}
	fCardView->ClearUndo();
	return true;
}


void
StackWindow::_RunFind()
{
	if (fMode != kModeFind)
		return;
	std::vector<bdb::FindCondition> conditions = fCardView->FindConditions();
	std::vector<std::string> found;
	bdb::Status status;
	{
		DocLocker lock(fDocument);
		bdb::MemoryCardQuery query;
		status = query.Find(fDocument->GetStack(), conditions, found);
		if (status) {
			fDocument->GetStack().Log().Info("ui.search", {
				{"conditions", int64_t(conditions.size())},
				{"results", int64_t(found.size())}});
		}
	}
	if (!status) {
		_Notice(str::kFindInvalidNumber);
		return;
	}
	if (found.empty()) {
		_Notice(str::kNotFound);
		return;
	}
	fFound = found;
	fFindActive = !conditions.empty();
	fCurrentId = found[0];
	_SetMode(kModeBrowse);
	fCardView->SetCard(fCurrentId);
	fCardView->FocusFirstField();
	_UpdateStatus();
}


void
StackWindow::_ShowAll()
{
	fFindActive = false;
	fFound.clear();
	_UpdateStatus();
}


// #pragma mark - edit mode


void
StackWindow::_SetCreateItem(int32 item)
{
	fCardView->SetCreateItem(item);
	BMessage message(kMsgPaletteSelect);
	message.AddInt32("item", item);
	BMessenger(fPalette).SendMessage(&message);
}


static void
AddChoice(BMessage& state, const char* field, const char* value,
	const char* label)
{
	BString key("menu:");
	key << field;
	BString choice(value);
	choice << "\t" << label;
	state.AddString(key.String(), choice);
}


static BString
IntText(int64 value)
{
	BString text;
	text << value;
	return text;
}


void
StackWindow::_UpdateInspector()
{
	if (fMode != kModeEdit)
		return;
	BMessage state(kMsgInspectorUpdate);
	DocLocker lock(fDocument);
	bdb::Stack& stack = fDocument->GetStack();
	bdb::Template* tmpl = fCardView->EditTemplate();
	if (tmpl == NULL)
		return;
	const bdb::Theme& theme = fCardView->CurrentTheme();
	const std::set<std::string>& selection = fCardView->Selection();
	std::vector<const Json*> parts;
	for (const std::string& id : selection) {
		if (const Json* part = tmpl->FindPart(id))
			parts.push_back(part);
	}

	if (parts.empty()) {
		// Template properties (UI 4.1, 6.5, 6.11, 6.15).
		state.AddString("mode", "template");
		state.AddString("tmpl_title", tmpl->Title().c_str());
		BString paper = tmpl->Doc().Find("paper") == NULL ? "screen"
			: tmpl->PaperSize().c_str();
		if (paper == "screen" && (tmpl->Width() != bdb::kDefaultCardWidth
				|| tmpl->Height() != bdb::kDefaultCardHeight)) {
			paper = "custom";
		}
		state.AddString("paper", paper);
		AddChoice(state, "paper", "screen", str::kPaperScreen);
		size_t count;
		const bdb::PaperInfo* papers = bdb::PaperSizes(&count);
		for (size_t i = 0; i < count; i++) {
			AddChoice(state, "paper", papers[i].size,
				BString(papers[i].size) == "postcard"
					? str::kPaperPostcard.Get() : papers[i].size);
		}
		AddChoice(state, "paper", "custom", str::kPaperCustom);
		state.AddString("orientation", tmpl->PaperOrientation().c_str());
		AddChoice(state, "orientation", "portrait", str::kPortrait);
		AddChoice(state, "orientation", "landscape", str::kLandscape);
		state.AddString("tmpl_w", IntText(tmpl->Width()));
		state.AddString("tmpl_h", IntText(tmpl->Height()));
		state.AddString("title_part", tmpl->TitlePartSetting().c_str());
		AddChoice(state, "title_part", "", str::kTemplateTitlePartAuto);
		for (const Json& part : tmpl->Parts()) {
			if (bdb::KindHasValue(bdb::part::Kind(part))) {
				AddChoice(state, "title_part", bdb::part::Id(part).c_str(),
					bdb::part::Name(part).c_str());
			}
		}
		const Json* background = tmpl->Background();
		std::string color = background != NULL
			? background->GetString("color") : std::string();
		state.AddString("bg_color", color.empty()
			? bdb::ColorToString(FromRgb(PanelColor(NULL, theme))).c_str()
			: color.c_str());
		state.AddBool("own:bg_color", !color.empty());
		std::string fit = background != NULL
			? background->GetString("fit", "cover") : std::string("cover");
		state.AddString("bg_fit", fit.c_str());
		AddChoice(state, "bg_fit", "cover", str::kFitCover);
		AddChoice(state, "bg_fit", "contain", str::kFitContain);
		AddChoice(state, "bg_fit", "stretch", str::kFitStretch);
		AddChoice(state, "bg_fit", "tile", str::kFitTile);
		state.AddBool("disabled:bg_image_clear", background == NULL
			|| background->Find("image") == NULL
			|| background->Find("image")->IsNull());
		state.AddString("theme", tmpl->ThemeId().c_str());
		BdbApp* app = dynamic_cast<BdbApp*>(be_app);
		if (app != NULL) {
			for (const bdb::ThemeInfo& info
					: app->Themes().List(&stack.Store())) {
				AddChoice(state, "theme", info.id.c_str(), str::gEnglish
					? info.nameEn.c_str() : info.nameJa.c_str());
			}
		}
		BString signature;
		signature << tmpl->Id().c_str() << ":" << int32(tmpl->Parts().size());
		state.AddString("signature", signature);
		BMessenger(fInspector).SendMessage(&state);
		return;
	}

	const Json* single = parts.size() == 1 ? parts[0] : NULL;
	bool locked = false;
	for (const Json* part : parts)
		locked = locked || bdb::part::IsLocked(*part);
	state.AddString("mode", single != NULL ? "part" : "multi");
	state.AddInt32("count", int32(parts.size()));
	state.AddBool("locked", locked && single != NULL);
	state.AddString("locked", locked ? "1" : "0");
	BString signature;
	signature << tmpl->Id().c_str();

	bdb::Rect rect = bdb::part::GetRect(*parts[0]);
	bool sameX = true, sameY = true, sameW = true, sameH = true;
	for (const Json* part : parts) {
		bdb::Rect other = bdb::part::GetRect(*part);
		sameX = sameX && other.x == rect.x;
		sameY = sameY && other.y == rect.y;
		sameW = sameW && other.w == rect.w;
		sameH = sameH && other.h == rect.h;
	}
	state.AddString("x", sameX ? IntText(rect.x) : BString());
	state.AddString("y", sameY ? IntText(rect.y) : BString());
	state.AddString("w", sameW ? IntText(rect.w) : BString());
	state.AddString("h", sameH ? IntText(rect.h) : BString());

	if (single != NULL) {
		std::string kind = bdb::part::Kind(*single);
		state.AddString("kind", kind.c_str());
		signature << ":" << bdb::part::Id(*single).c_str();
		bool assignable = bdb::IsAssignableKind(kind);
		const char* kinds[] = { "blank", "text", "number", "bool", "image" };
		const char* kindLabels[] = { str::kKindBlank, str::kKindText,
			str::kKindNumber, str::kKindBool, str::kKindImage };
		if (assignable) {
			for (int i = 0; i < 5; i++)
				AddChoice(state, "kind", kinds[i], kindLabels[i]);
		} else {
			const char* label = kind == bdb::kKindLabel ? str::kKindLabel.Get()
				: kind == bdb::kKindButton ? str::kKindButton.Get()
				: kind == bdb::kKindCounter ? str::kKindCounter.Get()
				: str::kKindUnknown.Get();
			AddChoice(state, "kind", kind.c_str(), label);
			state.AddBool("disabled:kind", true);
		}
		std::string name = bdb::part::Name(*single);
		state.AddString("name", name.c_str());
		int32 sameName = 0;
		for (const Json& part : tmpl->Parts()) {
			if (!name.empty() && bdb::part::Name(part) == name)
				sameName++;
		}
		state.AddBool("duplicate", sameName > 1);
		const Json* defaultValue = single->Find("default");
		if (defaultValue != NULL && (kind == bdb::kKindText
				|| kind == bdb::kKindNumber)) {
			state.AddString("default", bdb::ValueText(*defaultValue).c_str());
		}
		state.AddString("multiline", single->Find("multiline") != NULL
			&& single->Find("multiline")->AsBool() ? "1" : "0");
		state.AddString("decimals", IntText(single->GetInt("decimals", 0)));
		state.AddString("fit", single->GetString("fit", "contain").c_str());
		AddChoice(state, "fit", "contain", str::kFitContain);
		AddChoice(state, "fit", "cover", str::kFitCover);
		AddChoice(state, "fit", "stretch", str::kFitStretch);
		state.AddString("text", single->GetString("text").c_str());
		if (kind == bdb::kKindBool) {
			state.AddString("bool_default", defaultValue == NULL
				|| defaultValue->IsNull() ? "null"
				: defaultValue->AsBool() ? "true" : "false");
			AddChoice(state, "bool_default", "true", str::kBoolOn);
			AddChoice(state, "bool_default", "false", str::kBoolOff);
			AddChoice(state, "bool_default", "null", str::kBoolNone);
			state.AddString("bool_look", bdb::part::BoolLook(*single).c_str());
			AddChoice(state, "bool_look", "switch", str::kLookSwitch);
			AddChoice(state, "bool_look", "checkbox", str::kLookCheckbox);
		}
		if (kind == bdb::kKindButton) {
			std::string action = single->GetString("action");
			state.AddString("action", action.c_str());
			struct { const char* value; const str::Text* label; } actions[] = {
				{ "", &str::kActionNone },
				{ "card.first", &str::kActionFirst },
				{ "card.prev", &str::kActionPrev },
				{ "card.next", &str::kActionNext },
				{ "card.last", &str::kActionLast },
				{ "card.new", &str::kActionNew },
				{ "card.duplicate", &str::kActionDuplicate },
				{ "card.delete", &str::kActionDelete },
				{ "list.open", &str::kActionList },
				{ "link.open", &str::kActionLink },
				{ "find.start", &str::kActionFindStart },
				{ "find.run", &str::kActionFindRun },
				{ "find.clear", &str::kActionFindClear },
				{ "card.pdf", &str::kActionCardPdf },
				{ "card.image", &str::kActionCardImage },
				{ "sound.play", &str::kActionSound },
				{ "video.play", &str::kActionVideo }
			};
			for (const auto& entry : actions)
				AddChoice(state, "action", entry.value, *entry.label);
			if (!action.empty() && !bdb::IsKnownAction(action))
				AddChoice(state, "action", action.c_str(), str::kActionUnknown);
			// The sound or video file (sound.play / video.play).
			const Json* media = bdb::part::ButtonMedia(*single);
			BString mediaName = str::kMediaNone.Get();
			if (media != NULL) {
				mediaName = media->GetString("kind") == "audio" ? "♪ " : "▶ ";
				std::string original = media->GetString("name");
				mediaName << (original.empty() ? media->GetString("ext")
					: original).c_str();
			}
			state.AddString("media_name", mediaName);
			state.AddBool("disabled:media_name", true);
			bool usesMedia = action == "sound.play" || action == "video.play";
			state.AddBool("disabled:media_choose", !usesMedia);
			state.AddBool("disabled:media_clear", media == NULL);
		}
		if (kind == bdb::kKindBool) {
			// What the switch does while on: the effect and its parts.
			std::string effect = bdb::part::SwitchEffect(*single);
			state.AddString("effect", effect.empty() ? "none"
				: effect.c_str());
			AddChoice(state, "effect", "none", str::kEffectNone);
			AddChoice(state, "effect", "lock_card", str::kEffectLockCard);
			AddChoice(state, "effect", "lock", str::kEffectLock);
			AddChoice(state, "effect", "hide", str::kEffectHide);
			std::vector<std::string> targets
				= bdb::part::SwitchTargets(*single);
			bool chooses = effect == "lock" || effect == "hide";
			std::string self = bdb::part::Id(*single);
			for (const Json& other : tmpl->Parts()) {
				std::string id = bdb::part::Id(other);
				if (id == self)
					continue;
				std::string otherKind = bdb::part::Kind(other);
				BString label = bdb::part::Name(other).c_str();
				if (label.IsEmpty())
					label = other.GetString("text").c_str();
				if (label.IsEmpty())
					label = id.c_str();
				label << " [" << KindName(otherKind) << "]";
				BString entry;
				entry << id.c_str() << "\t" << label;
				state.AddString("targets", entry);
				BString field("target:");
				field << id.c_str();
				bool on = std::find(targets.begin(), targets.end(), id)
					!= targets.end();
				state.AddString(field, on ? "1" : "0");
				BString disabled("disabled:");
				disabled << field;
				state.AddBool(disabled, !chooses);
			}
			signature << ":" << self.c_str() << ":"
				<< int32(tmpl->Parts().size());
		}
		if (kind == bdb::kKindButton || kind == bdb::kKindLabel) {
			const Json* icon = single->Find("icon");
			bool hasIcon = icon != NULL && icon->IsObject();
			state.AddString("icon_size", IntText(hasIcon
				? icon->GetInt("size", 24) : 24));
			state.AddString("icon_placement", hasIcon
				? icon->GetString("placement", "left").c_str() : "left");
			AddChoice(state, "icon_placement", "left", str::kIconLeft);
			AddChoice(state, "icon_placement", "top", str::kIconTop);
			state.AddBool("disabled:icon_clear", !hasIcon);
			state.AddBool("disabled:icon_size", !hasIcon);
			state.AddBool("disabled:icon_placement", !hasIcon);
		}
		// Text parts of the template, for links and image sources.
		std::vector<const Json*> textParts;
		for (const Json& part : tmpl->Parts()) {
			if (bdb::part::Kind(part) == bdb::kKindText)
				textParts.push_back(&part);
		}
		if (kind == bdb::kKindButton || kind == bdb::kKindLabel
			|| kind == bdb::kKindImage) {
			const Json* link = single->Find("link");
			std::string mode = "none";
			if (link != NULL && link->Find("url") != NULL)
				mode = "url";
			else if (link != NULL && link->Find("field") != NULL)
				mode = "field";
			state.AddString("link_mode", mode.c_str());
			AddChoice(state, "link_mode", "none", str::kLinkNone);
			AddChoice(state, "link_mode", "url", str::kLinkUrl);
			AddChoice(state, "link_mode", "field", str::kLinkField);
			state.AddString("link_url", link != NULL
				? link->GetString("url").c_str() : "");
			state.AddString("link_field", link != NULL
				? link->GetString("field").c_str() : "");
			for (const Json* part : textParts) {
				AddChoice(state, "link_field", bdb::part::Id(*part).c_str(),
					bdb::part::Name(*part).c_str());
			}
			state.AddBool("disabled:link_url", mode != "url");
			state.AddBool("disabled:link_field", mode != "field");
		}
		if (kind == bdb::kKindImage) {
			const Json* source = single->Find("source");
			state.AddString("source_field", source != NULL
				&& source->IsObject() ? source->GetString("field").c_str()
				: "");
			AddChoice(state, "source_field", "", str::kSourceNone);
			for (const Json* part : textParts) {
				AddChoice(state, "source_field", bdb::part::Id(*part).c_str(),
					bdb::part::Name(*part).c_str());
			}
			state.AddString("source_edge", IntText(source != NULL
				&& source->IsObject() ? source->GetInt("max_edge", 256)
				: 256));
			AddChoice(state, "source_edge", "128", "128");
			AddChoice(state, "source_edge", "256", "256");
			AddChoice(state, "source_edge", "512", "512");
			state.AddBool("disabled:source_edge", source == NULL
				|| !source->IsObject());
		}
		signature << ":" << int32(textParts.size());
	}

	// Style (UI 6.12): the first part's values; dimmed when from the theme.
	bdb::PartLook look = theme.Resolve(*parts[0]);
	state.AddString("border_visible", look.borderVisible ? "1" : "0");
	state.AddString("border_width", IntText(look.borderWidth));
	state.AddBool("own:border_width", look.ownBorder);
	state.AddString("border_color",
		bdb::ColorToString(look.borderColor).c_str());
	state.AddBool("own:border_color", look.borderColorSet);
	std::string fillMode = !look.ownFill ? "theme"
		: look.hasFill ? "color" : "none";
	state.AddString("fill_mode", fillMode.c_str());
	AddChoice(state, "fill_mode", "theme", str::kStyleReset);
	AddChoice(state, "fill_mode", "color", str::kStyleColor);
	AddChoice(state, "fill_mode", "none", str::kStyleTransparent);
	state.AddString("fill_color", look.hasFill
		? bdb::ColorToString(look.fill).c_str() : "-");
	state.AddBool("own:fill_color", look.ownFill);
	state.AddBool("disabled:fill_color", fillMode == "none");
	const Json* style = bdb::part::Style(*parts[0]);
	const Json* font = style != NULL ? style->Find("font") : NULL;
	std::string family = font != NULL ? font->GetString("family")
		: std::string();
	std::string fontStyle = font != NULL ? font->GetString("style")
		: std::string();
	BString familyValue;
	if (!family.empty())
		familyValue << family.c_str() << "\t" << fontStyle.c_str();
	state.AddString("font_family", familyValue);
	AddChoice(state, "font_family", "", str::kStyleSystemFont);
	AddChoice(state, "font_family", "-", "");
	int32 families = count_font_families();
	for (int32 i = 0; i < families; i++) {
		font_family name;
		if (get_font_family(i, &name) != B_OK)
			continue;
		int32 styles = count_font_styles(name);
		for (int32 j = 0; j < styles; j++) {
			font_style styleName;
			if (get_font_style(name, j, &styleName) != B_OK)
				continue;
			BString value;
			value << name << "\t" << styleName;
			BString label;
			label << name << " " << styleName;
			AddChoice(state, "font_family", value, label);
		}
	}
	state.AddString("font_size", IntText(look.fontSize));
	state.AddBool("own:font_size", font != NULL && font->Find("size") != NULL);
	state.AddString("text_color", bdb::ColorToString(look.textColor).c_str());
	state.AddBool("own:text_color", font != NULL
		&& font->Find("color") != NULL);
	if (locked && single == NULL)
		state.AddBool("locked", false);
	state.AddString("signature", signature);
	BMessenger(fInspector).SendMessage(&state);
}


static bool
ParseInt(const char* text, long low, long high, long& out)
{
	char* end = NULL;
	long number = strtol(text, &end, 10);
	if (end == text || *end != '\0' || number < low || number > high)
		return false;
	out = number;
	return true;
}


bool
StackWindow::_ApplyStyleField(Json& part, const BString& field,
	const char* value)
{
	// UI 6.12: only the changed key is set; the rest stays the theme's.
	if (field == "style_reset") {
		bdb::part::ClearStyle(part);
		return true;
	}
	Json& style = bdb::part::MutableStyle(part);
	auto border = [&style]() -> Json& {
		Json* object = style.Find("border");
		if (object == NULL || !object->IsObject()) {
			style.Set("border", Json::MakeObject());
			object = style.Find("border");
		}
		return *object;
	};
	auto font = [&style]() -> Json& {
		Json* object = style.Find("font");
		if (object == NULL || !object->IsObject()) {
			style.Set("font", Json::MakeObject());
			object = style.Find("font");
		}
		return *object;
	};
	long number;
	if (field == "border_visible") {
		border().Set("visible", BString(value) == "1");
	} else if (field == "border_width") {
		if (!ParseInt(value, 0, 16, number))
			return false;
		border().Set("width", int64_t(number));
	} else if (field == "border_color") {
		if (!bdb::IsValidColor(value))
			return false;
		border().Set("color", value);
	} else if (field == "fill_mode") {
		BString mode(value);
		if (mode == "theme")
			style.Remove("fill");
		else if (mode == "none")
			style.Set("fill", Json());
		else if (style.Find("fill") == NULL || style.Find("fill")->IsNull())
			style.Set("fill", "#ffffff");
	} else if (field == "fill_color") {
		if (!bdb::IsValidColor(value))
			return false;
		style.Set("fill", value);
	} else if (field == "font_family") {
		BString text(value);
		if (text == "-")
			return false;
		if (text.IsEmpty()) {
			font().Remove("family");
			font().Remove("style");
		} else {
			int32 tab = text.FindFirst('\t');
			BString family, styleName;
			text.CopyInto(family, 0, tab);
			text.CopyInto(styleName, tab + 1, text.Length() - tab - 1);
			font().Set("family", family.String());
			font().Set("style", styleName.String());
		}
	} else if (field == "font_size") {
		if (BString(value).IsEmpty()) {
			font().Remove("size");
		} else {
			if (!ParseInt(value, 6, 96, number))
				return false;
			font().Set("size", int64_t(number));
		}
	} else if (field == "text_color") {
		if (!bdb::IsValidColor(value))
			return false;
		font().Set("color", value);
	} else
		return false;
	// Tidy empty objects so that the theme decides again.
	if (Json* object = style.Find("font")) {
		if (object->IsObject() && object->Object().empty())
			style.Remove("font");
	}
	if (style.Object().empty())
		part.Remove("style");
	return true;
}


bool
StackWindow::_ApplyPartField(Json& part, const BString& field,
	const char* value)
{
	std::string kind = bdb::part::Kind(part);
	long number;
	if (field == "name") {
		if (bdb::CountCodePoints(value) > bdb::kMaxPartNameChars)
			return false;
		if (bdb::part::Name(part) == value)
			return false;
		part.Set("name", value);
	} else if (field == "default") {
		Json newValue;
		if (kind == bdb::kKindText)
			newValue = Json(value);
		else if (kind == bdb::kKindNumber) {
			std::string trimmed = bdb::TrimSpaces(value);
			double parsed;
			if (!trimmed.empty()) {
				if (!bdb::ParseDecimal(trimmed, parsed)
					|| !bdb::IsValidNumber(parsed)) {
					return false;
				}
				newValue = bdb::MakeNumberValue(parsed);
			}
		} else
			return false;
		part.Set("default", newValue);
	} else if (field == "multiline") {
		part.Set("multiline", BString(value) == "1");
	} else if (field == "decimals") {
		if (!ParseInt(value, 0, bdb::kMaxDecimals, number))
			return false;
		part.Set("decimals", int64_t(number));
	} else if (field == "fit") {
		part.Set("fit", value);
	} else if (field == "text") {
		part.Set("text", value);
	} else if (field == "bool_default") {
		BString text(value);
		part.Set("default", text == "true" ? Json(true)
			: text == "false" ? Json(false) : Json());
	} else if (field == "bool_look") {
		bdb::part::SetBoolLook(part, value);
	} else if (field == "action") {
		if (BString(value).IsEmpty())
			part.Remove("action");
		else
			part.Set("action", value);
	} else if (field == "icon_clear") {
		part.Remove("icon");
	} else if (field == "media_clear") {
		part.Remove("media");
	} else if (field == "effect") {
		BString type(value);
		bdb::part::SetSwitchEffect(part, type == "none" ? "" : type.String(),
			bdb::part::SwitchTargets(part));
	} else if (field.StartsWith("target:")) {
		std::string type = bdb::part::SwitchEffect(part);
		if (type != "lock" && type != "hide")
			return false;
		BString id(field);
		id.Remove(0, 7);
		std::vector<std::string> targets = bdb::part::SwitchTargets(part);
		auto found = std::find(targets.begin(), targets.end(), id.String());
		bool on = BString(value) == "1";
		if (on && found == targets.end())
			targets.push_back(id.String());
		else if (!on && found != targets.end())
			targets.erase(found);
		else
			return false;
		bdb::part::SetSwitchEffect(part, type, targets);
	} else if (field == "icon_size") {
		Json* icon = part.Find("icon");
		if (icon == NULL || !icon->IsObject()
			|| !ParseInt(value, 12, 128, number)) {
			return false;
		}
		icon->Set("size", int64_t(number));
	} else if (field == "icon_placement") {
		Json* icon = part.Find("icon");
		if (icon == NULL || !icon->IsObject())
			return false;
		icon->Set("placement", value);
	} else if (field == "link_mode") {
		BString mode(value);
		if (mode == "none")
			part.Remove("link");
		else {
			Json link = Json::MakeObject();
			if (mode == "url")
				link.Set("url", "https://");
			else {
				// The first text part as a start.
				std::string first;
				DocLocker lock(fDocument);
				bdb::Template* tmpl = fCardView->EditTemplate();
				for (const Json& other : tmpl->Parts()) {
					if (bdb::part::Kind(other) == bdb::kKindText) {
						first = bdb::part::Id(other);
						break;
					}
				}
				link.Set("field", first);
			}
			part.Set("link", link);
		}
	} else if (field == "link_url") {
		Json link = Json::MakeObject();
		link.Set("url", value);
		part.Set("link", link);
	} else if (field == "link_field") {
		Json link = Json::MakeObject();
		link.Set("field", value);
		part.Set("link", link);
	} else if (field == "source_field") {
		if (BString(value).IsEmpty())
			part.Remove("source");
		else {
			Json source = Json::MakeObject();
			source.Set("field", value);
			const Json* old = part.Find("source");
			source.Set("max_edge", old != NULL && old->IsObject()
				? old->GetInt("max_edge", 256) : 256);
			part.Set("source", source);
		}
	} else if (field == "source_edge") {
		Json* source = part.Find("source");
		if (source == NULL || !source->IsObject()
			|| !ParseInt(value, 32, 2048, number)) {
			return false;
		}
		source->Set("max_edge", int64_t(number));
	} else
		return _ApplyStyleField(part, field, value);
	return true;
}


void
StackWindow::_ApplyInspector(const char* field, const char* value)
{
	if (fMode != kModeEdit)
		return;
	BString name(field);
	if (name.StartsWith("tmpl_") || name == "paper" || name == "orientation"
		|| name == "title_part" || name.StartsWith("bg_")
		|| name.StartsWith("theme")) {
		_ApplyTemplateField(name, value);
		return;
	}
	if (name == "icon_choose") {
		_ChooseIcon();
		return;
	}
	if (name == "media_choose") {
		_ChooseMedia();
		return;
	}
	if (name == "kind") {
		_ChangeKind(value);
		return;
	}
	DocLocker lock(fDocument);
	bdb::Template* tmpl = fCardView->EditTemplate();
	if (tmpl == NULL)
		return;
	std::set<std::string> selection = fCardView->Selection();
	if (selection.empty())
		return;

	if (name == "locked") {
		fCardView->SetLocked(BString(value) == "1", false);
		return;
	}
	// Locked parts keep their settings (UI 6.13).
	std::vector<Json*> parts;
	for (const std::string& id : selection) {
		Json* part = tmpl->FindPart(id);
		if (part != NULL && !bdb::part::IsLocked(*part))
			parts.push_back(part);
	}
	if (parts.empty()) {
		beep();
		_UpdateInspector();
		return;
	}

	if (name == "x" || name == "y" || name == "w" || name == "h") {
		long number;
		bool size = name == "w" || name == "h";
		if (!ParseInt(value, size ? 1 : -100000, 100000, number)) {
			beep();
			_UpdateInspector();
			return;
		}
		fCardView->PushUndo();
		for (Json* part : parts) {
			bdb::Rect rect = bdb::part::GetRect(*part);
			if (name == "x")
				rect.x = int32(number);
			else if (name == "y")
				rect.y = int32(number);
			else if (name == "w")
				rect.w = int32(number);
			else
				rect.h = int32(number);
			bdb::part::SetRect(*part, rect);
		}
		fCardView->TemplateChanged();
		return;
	}

	// Style fields apply to every selected part (UI 6.12); the others to
	// one part.
	bool styleField = name.StartsWith("border_") || name.StartsWith("fill_")
		|| name.StartsWith("font_") || name == "text_color"
		|| name == "style_reset";
	if (!styleField && parts.size() != 1)
		return;
	Json before = tmpl->Doc();
	fCardView->PushUndo();
	bool changed = false;
	for (Json* part : parts) {
		if (styleField ? _ApplyStyleField(*part, name, value)
				: _ApplyPartField(*part, name, value)) {
			changed = true;
		}
	}
	if (!changed || tmpl->Doc() == before) {
		// Nothing changed: drop the undo step again.
		fCardView->DiscardUndo();
		tmpl->Doc() = before;
		if (!changed)
			beep();
		_UpdateInspector();
		return;
	}
	fCardView->TemplateChanged();
}


void
StackWindow::_ApplyTemplateField(const BString& field, const char* value)
{
	std::string templateId = fCardView->TemplateId();
	if (field == "bg_image_choose") {
		if (fBackgroundPanel == NULL) {
			BMessenger target(this);
			fBackgroundPanel = new BFilePanel(B_OPEN_PANEL, &target, NULL,
				B_FILE_NODE, false, new BMessage(kMsgBackgroundPanel));
		}
		fBackgroundPanel->Show();
		return;
	}
	if (field == "theme") {
		_ApplyTheme(templateId, value);
		return;
	}
	if (field == "theme_all") {
		std::string themeId;
		{
			DocLocker lock(fDocument);
			bdb::Template* tmpl = fCardView->EditTemplate();
			if (tmpl == NULL)
				return;
			themeId = tmpl->ThemeId();
		}
		_ApplyThemeToAll(themeId);
		return;
	}
	if (field == "paper" || field == "orientation") {
		BMessage message(kMsgPaperChosen);
		DocLocker lock(fDocument);
		bdb::Template* tmpl = fCardView->EditTemplate();
		if (tmpl == NULL)
			return;
		BString size = field == "paper" ? BString(value)
			: BString(tmpl->Doc().Find("paper") == NULL ? "screen"
				: tmpl->PaperSize().c_str());
		BString orientation = field == "orientation" ? BString(value)
			: BString(tmpl->PaperOrientation().c_str());
		message.AddString("size", size);
		message.AddString("orientation", orientation);
		message.AddInt32("width", tmpl->Width());
		message.AddInt32("height", tmpl->Height());
		_PaperChosen(&message);
		return;
	}

	DocLocker lock(fDocument);
	bdb::Template* tmpl = fCardView->EditTemplate();
	if (tmpl == NULL)
		return;
	Json before = tmpl->Doc();
	fCardView->PushUndo();
	bool ok = true;
	long number;
	if (field == "tmpl_title") {
		if (BString(value).IsEmpty())
			ok = false;
		else
			tmpl->SetTitle(value);
	} else if (field == "tmpl_w" || field == "tmpl_h") {
		if (!ParseInt(value, bdb::kMinCardSize, bdb::kMaxCardSize, number))
			ok = false;
		else {
			int32 width = field == "tmpl_w" ? int32(number) : tmpl->Width();
			int32 height = field == "tmpl_h" ? int32(number) : tmpl->Height();
			if (tmpl->Doc().Find("paper") != NULL)
				tmpl->SetPaper("custom", tmpl->PaperOrientation(), width,
					height);
			else
				tmpl->SetSize(width, height);
		}
	} else if (field == "title_part") {
		tmpl->SetTitlePart(value);
	} else if (field == "bg_color") {
		if (!bdb::IsValidColor(value))
			ok = false;
		else
			tmpl->MutableBackground().Set("color", value);
	} else if (field == "bg_color_reset") {
		if (tmpl->Background() != NULL)
			tmpl->MutableBackground().Remove("color");
	} else if (field == "bg_image_clear") {
		// The file stays until the stack is cleaned up (UI 6.11).
		if (tmpl->Background() != NULL)
			tmpl->MutableBackground().Remove("image");
	} else if (field == "bg_fit") {
		tmpl->MutableBackground().Set("fit", value);
	} else
		ok = false;
	if (const Json* background = tmpl->Background()) {
		if (background->Object().empty())
			tmpl->Doc().Remove("background");
	}
	if (!ok || tmpl->Doc() == before) {
		fCardView->DiscardUndo();
		tmpl->Doc() = before;
		if (!ok)
			beep();
		_UpdateInspector();
		return;
	}
	fCardView->TemplateChanged();
	_UpdateTitle();
	_UpdateBanner();
}


bool
StackWindow::_ApplyTheme(const std::string& templateId,
	const std::string& themeId)
{
	DocLocker lock(fDocument);
	bdb::Stack& stack = fDocument->GetStack();
	bdb::Template* tmpl = stack.FindTemplate(templateId);
	if (tmpl == NULL)
		return false;
	Json before = tmpl->Doc();
	bool editing = fMode == kModeEdit && templateId == fCardView->TemplateId();
	if (editing)
		fCardView->PushUndo();
	bdb::Status status = stack.ApplyTheme(templateId, themeId, false);
	if (status.Kind() == bdb::ErrorKind::kConflict) {
		// Another revision is in the stack: ask (theme spec 3.3).
		BString text;
		text.SetToFormat(str::kThemeOverwrite.Get(), themeId.c_str());
		BAlert* alert = new BAlert(str::kAppName, text, str::kCancel,
			str::kOverwrite, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		fDocument->Unlock();
		int32 choice = alert->Go();
		fDocument->Lock();
		if (choice != 1) {
			if (editing)
				fCardView->DiscardUndo();
			return false;
		}
		status = stack.ApplyTheme(templateId, themeId, true);
	}
	if (!status) {
		if (editing)
			fCardView->DiscardUndo();
		_ShowError(str::kTemplateSaveFailed, status);
		return false;
	}
	if (editing) {
		fCardView->ThemeChanged();
		fCardView->TemplateChanged();
	} else
		stack.SaveTemplate(templateId);
	return true;
}


void
StackWindow::_ApplyThemeToAll(const std::string& themeId)
{
	std::vector<std::string> ids;
	{
		DocLocker lock(fDocument);
		for (bdb::Template* tmpl : fDocument->GetStack().Templates())
			ids.push_back(tmpl->Id());
	}
	for (const std::string& id : ids) {
		if (!_ApplyTheme(id, themeId))
			break;
	}
	BMessage notice(kMsgDocTemplateChanged);
	notice.AddPointer("source", this);
	fDocument->Notify(&notice);
}


void
StackWindow::_ChangeKind(const char* newKind)
{
	if (fMode != kModeEdit || fCardView->Selection().size() != 1)
		return;
	std::string partId = *fCardView->Selection().begin();
	std::string templateId = fCardView->TemplateId();
	std::string oldKind;
	int32 withValues = 0;
	bdb::ConvertReport report;
	{
		DocLocker lock(fDocument);
		bdb::Stack& stack = fDocument->GetStack();
		bdb::Template* tmpl = stack.FindTemplate(templateId);
		const Json* part = tmpl != NULL ? tmpl->FindPart(partId) : NULL;
		if (part == NULL || bdb::part::IsLocked(*part))
			return;
		oldKind = bdb::part::Kind(*part);
		if (oldKind == newKind)
			return;
		if (oldKind != bdb::kKindBlank)
			withValues = stack.CountValues(templateId, partId);
		if (withValues > 0) {
			stack.ConvertValues(templateId, partId, oldKind, newKind, false,
				report);
		}
	}

	bool convert = false;
	if (withValues > 0) {
		// UI spec 6.6
		BString text;
		text.SetToFormat(str::kKindChangeQuestion.Get(), report.cards,
			report.convertible, report.notConvertible);
		BAlert* alert = new BAlert(str::kAppName, text.String(),
			str::kCancel, str::kKeepValues, str::kConvert, B_WIDTH_AS_USUAL,
			B_OFFSET_SPACING, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		int32 choice = alert->Go();
		if (choice == 0) {
			_UpdateInspector();
			return;
		}
		convert = choice == 2;
	}

	fCardView->PushUndo();
	bdb::Status status;
	{
		DocLocker lock(fDocument);
		bdb::Stack& stack = fDocument->GetStack();
		status = stack.ChangePartKind(templateId, partId, newKind);
		if (status && convert) {
			stack.ConvertValues(templateId, partId, oldKind, newKind, true,
				report);
		}
		// Values were looked at: store the new kind right away so that
		// the cards and the template stay consistent.
		if (status && withValues > 0)
			status = stack.SaveTemplate(templateId);
	}
	if (!status) {
		_ShowError(str::kTemplateSaveFailed, status);
		return;
	}
	if (convert) {
		BString text;
		text.SetToFormat(str::kConvertDone.Get(), report.converted,
			report.notConvertible + report.failed);
		_Notice(text.String());
		_NotifyCardsChanged();
	}
	fCardView->TemplateChanged();
	if (withValues > 0)
		fTemplateDirty = false;
}


void
StackWindow::_TemplateEdited(BMessage* message)
{
	fTemplateDirty = true;
	_UpdateInspector();
	// Deleted value parts: tell where the values remain (UI spec 6.8).
	int32 remaining = 0;
	BString partId;
	DocLocker lock(fDocument);
	std::string templateId = fCardView->TemplateId();
	for (int32 i = 0; message->FindString("removed", i, &partId) == B_OK;
			i++) {
		remaining += fDocument->GetStack().CountValues(templateId,
			partId.String());
	}
	if (remaining > 0) {
		BString text;
		text.SetToFormat(str::kValuesRemain.Get(), (int)remaining);
		_Notice(text.String());
	}
}


bool
StackWindow::_AddMediaFromFile(const entry_ref& ref, bool reuse,
	const char* partId, Json& outRef)
{
	BPath path(&ref);
	if (!IsReadableImage(path)) {
		_Notice(str::kImageUnreadable);
		return false;
	}
	bdb::Status status;
	{
		DocLocker lock(fDocument);
		bdb::FileInStream source(path.Path());
		status = fDocument->GetStack().AddMedia("image", ExtensionFor(path),
			source, reuse, fCardView->TemplateId(), partId, outRef);
	}
	if (!status) {
		_ShowError(str::kTemplateSaveFailed, status);
		return false;
	}
	return true;
}


void
StackWindow::_EditImageDrop(BMessage* message)
{
	entry_ref ref;
	BPoint point;
	if (message->FindRef("refs", &ref) != B_OK
		|| message->FindPoint("point", &point) != B_OK) {
		return;
	}
	// UI 6.2: background or image part.
	BAlert* alert = new BAlert(str::kAppName, str::kImageDropQuestion,
		str::kCancel, str::kImageAsFrame, str::kImageAsBackground,
		B_WIDTH_AS_USUAL, B_OFFSET_SPACING, B_IDEA_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	int32 choice = alert->Go();
	if (choice == 0)
		return;
	Json ref2;
	if (!_AddMediaFromFile(ref, false, "", ref2))
		return;
	if (choice == 1) {
		fCardView->CreateImagePart(point, ref2);
		return;
	}
	DocLocker lock(fDocument);
	bdb::Template* tmpl = fCardView->EditTemplate();
	if (tmpl == NULL)
		return;
	fCardView->PushUndo();
	tmpl->MutableBackground().Set("image", ref2);
	fCardView->TemplateChanged();
}


void
StackWindow::_ChooseIcon()
{
	int32 size = 24;
	BString placement = "left";
	{
		DocLocker lock(fDocument);
		bdb::Template* tmpl = fCardView->EditTemplate();
		if (tmpl == NULL || fCardView->Selection().size() != 1)
			return;
		const Json* part = tmpl->FindPart(*fCardView->Selection().begin());
		if (part == NULL || bdb::part::IsLocked(*part))
			return;
		std::string kind = bdb::part::Kind(*part);
		if (kind != bdb::kKindButton && kind != bdb::kKindLabel)
			return;
		if (const Json* icon = part->Find("icon")) {
			size = int32(icon->GetInt("size", 24));
			placement = icon->GetString("placement", "left").c_str();
		}
	}
	IconChooserWindow* window = new IconChooserWindow(BMessenger(this),
		BdbApp::IconDirectory().String(), size, placement);
	window->Show();
}


void
StackWindow::_IconChosen(BMessage* message)
{
	if (fMode != kModeEdit || fCardView->Selection().size() != 1)
		return;
	BString path = message->GetString("path", "");
	entry_ref ref;
	if (get_ref_for_path(path.String(), &ref) != B_OK)
		return;
	std::string partId = *fCardView->Selection().begin();
	// Copied into the stack, reusing the same content (theme spec 8.3).
	Json mediaRef;
	if (!_AddMediaFromFile(ref, true, partId.c_str(), mediaRef))
		return;
	DocLocker lock(fDocument);
	bdb::Template* tmpl = fCardView->EditTemplate();
	Json* part = tmpl != NULL ? tmpl->FindPart(partId) : NULL;
	if (part == NULL)
		return;
	fCardView->PushUndo();
	mediaRef.Set("size", int64_t(std::max(12, std::min(128,
		(int)message->GetInt32("size", 24)))));
	mediaRef.Set("placement", message->GetString("placement", "left"));
	part->Set("icon", mediaRef);
	fCardView->TemplateChanged();
}


// #pragma mark - sound, video, card image (prototype extensions)


void
StackWindow::_ChooseMedia()
{
	std::string partId;
	{
		DocLocker lock(fDocument);
		bdb::Template* tmpl = fCardView->EditTemplate();
		if (tmpl == NULL || fCardView->Selection().size() != 1)
			return;
		partId = *fCardView->Selection().begin();
		const Json* part = tmpl->FindPart(partId);
		if (part == NULL || bdb::part::IsLocked(*part)
			|| bdb::part::Kind(*part) != bdb::kKindButton) {
			return;
		}
	}
	BMessage message(kMsgMediaChosen);
	message.AddString("part", partId.c_str());
	if (fMediaPanel == NULL) {
		BMessenger target(this);
		fMediaPanel = new BFilePanel(B_OPEN_PANEL, &target, NULL,
			B_FILE_NODE, false);
	}
	fMediaPanel->SetMessage(&message);
	fMediaPanel->Show();
}


// "audio" or "video" for a file, from its type (or its name).
static std::string
MediaKindOf(const BPath& path)
{
	BMimeType type;
	if (BMimeType::GuessMimeType(path.Path(), &type) == B_OK) {
		BString name = type.Type();
		if (name.StartsWith("audio/"))
			return "audio";
		if (name.StartsWith("video/"))
			return "video";
	}
	return "";
}


void
StackWindow::_MediaChosen(BMessage* message)
{
	entry_ref ref;
	BString partId = message->GetString("part", "");
	if (fMode != kModeEdit || message->FindRef("refs", &ref) != B_OK)
		return;
	BPath path(&ref);
	std::string kind = MediaKindOf(path);
	std::string action;
	{
		DocLocker lock(fDocument);
		bdb::Template* tmpl = fCardView->EditTemplate();
		const Json* part = tmpl != NULL ? tmpl->FindPart(partId.String())
			: NULL;
		if (part == NULL)
			return;
		action = part->GetString("action");
	}
	if (kind.empty()) {
		_Notice(str::kMediaUnreadable);
		return;
	}
	if ((action == "sound.play" && kind != "audio")
		|| (action == "video.play" && kind != "video")) {
		_Notice(str::kMediaKindMismatch);
		return;
	}
	Json mediaRef;
	bdb::Status status;
	{
		DocLocker lock(fDocument);
		bdb::FileInStream source(path.Path());
		status = fDocument->GetStack().AddMedia(kind, ExtensionFor(path),
			source, false, fCardView->TemplateId(), partId.String(),
			mediaRef);
	}
	if (!status) {
		_ShowError(str::kTemplateSaveFailed, status);
		return;
	}
	DocLocker lock(fDocument);
	bdb::Template* tmpl = fCardView->EditTemplate();
	Json* part = tmpl != NULL ? tmpl->FindPart(partId.String()) : NULL;
	if (part == NULL)
		return;
	fCardView->PushUndo();
	// The original file name, for the player's window (the copy in the
	// stack is named by its ID).
	mediaRef.Set("name", path.Leaf());
	part->Set("media", mediaRef);
	fCardView->TemplateChanged();
}


bool
StackWindow::_ButtonMediaPath(const char* partId, std::string& path,
	std::string* name)
{
	DocLocker lock(fDocument);
	bdb::Stack& stack = fDocument->GetStack();
	const bdb::Card* card = stack.FindCard(fCurrentId);
	const bdb::Template* tmpl = card != NULL
		? stack.FindTemplate(card->TemplateId())
		: stack.FindTemplate(stack.DefaultTemplateId());
	const Json* part = tmpl != NULL ? tmpl->FindPart(partId) : NULL;
	const Json* media = part != NULL ? bdb::part::ButtonMedia(*part) : NULL;
	if (media == NULL)
		return false;
	path = stack.MediaPath(*media);
	if (name != NULL)
		*name = media->GetString("name");
	return !path.empty();
}


void
StackWindow::_PlaySound(const char* partId)
{
	// The same button again stops the sound.
	if (fSound != NULL && fSoundPart == partId && fSound->IsPlaying()) {
		fSound->StopPlaying();
		delete fSound;
		fSound = NULL;
		return;
	}
	delete fSound;
	fSound = NULL;
	std::string path;
	if (!_ButtonMediaPath(partId, path)) {
		_Hint(str::kMediaMissing);
		return;
	}
	entry_ref ref;
	if (get_ref_for_path(path.c_str(), &ref) != B_OK) {
		_Hint(str::kSoundFailed);
		return;
	}
	fSound = new BFileGameSound(&ref, false);
	if (fSound->InitCheck() != B_OK || fSound->StartPlaying() != B_OK) {
		delete fSound;
		fSound = NULL;
		_Hint(str::kSoundFailed);
		return;
	}
	fSoundPart = partId;
}


void
StackWindow::_PlayVideo(const char* partId)
{
	std::string path;
	std::string name;
	if (!_ButtonMediaPath(partId, path, &name)) {
		_Hint(str::kMediaMissing);
		return;
	}
	// The player names its window after the file; the copy in the stack is
	// named by its ID, so the player gets a copy with the original name.
	OpenVideo(path.c_str(), name.c_str(), BMessenger(this));
}


void
StackWindow::_ChooseCardImageFile()
{
	if (!_LeaveCard() && fDirty)
		return;
	_EnsureCard();
	BString name;
	{
		DocLocker lock(fDocument);
		bdb::Stack& stack = fDocument->GetStack();
		const bdb::Card* card = stack.FindCard(fCurrentId);
		if (card == NULL)
			return;
		name = bdb::CardTitle(*card, stack.FindTemplate(card->TemplateId()))
			.c_str();
	}
	name.ReplaceAll("/", "-");
	name << ".png";
	if (fCardImagePanel == NULL) {
		BMessenger target(this);
		fCardImagePanel = new BFilePanel(B_SAVE_PANEL, &target, NULL, 0,
			false, new BMessage(kMsgSaveCardImage));
		fCardImagePanel->Window()->SetTitle(str::kSaveCardImageTitle);
	}
	fCardImagePanel->SetSaveText(name);
	fCardImagePanel->Show();
}


void
StackWindow::_SaveCardImage(BMessage* message)
{
	entry_ref directory;
	BString name;
	if (message->FindRef("directory", &directory) != B_OK
		|| message->FindString("name", &name) != B_OK) {
		return;
	}
	BPath path(&directory);
	path.Append(name.String());
	status_t status = B_ERROR;
	int32 count = int32(_NavigationList().size());
	int32 position = _CurrentNavIndex() + 1;
	{
		DocLocker lock(fDocument);
		bdb::Stack& stack = fDocument->GetStack();
		const bdb::Card* card = stack.FindCard(fCurrentId);
		const bdb::Template* tmpl = card != NULL
			? stack.FindTemplate(card->TemplateId()) : NULL;
		if (card != NULL && tmpl != NULL) {
			bdb::Theme theme = WithSystemColors(stack.ThemeFor(tmpl));
			status = SaveCardImage(stack, *card, *tmpl, theme, path.Path(),
				std::max(int32(1), position), std::max(int32(1), count));
		}
	}
	if (status != B_OK) {
		_Notice(str::kImageSaveFailed);
		return;
	}
	BString text;
	text.SetToFormat(str::kImageSaved.Get(), name.String());
	_Hint(text);
}


void
StackWindow::_ViewImage(const char* partId)
{
	std::string path;
	std::string title;
	{
		DocLocker lock(fDocument);
		bdb::Stack& stack = fDocument->GetStack();
		const bdb::Card* card = stack.FindCard(fCurrentId);
		const bdb::Template* tmpl = card != NULL
			? stack.FindTemplate(card->TemplateId()) : NULL;
		const Json* part = tmpl != NULL ? tmpl->FindPart(partId) : NULL;
		if (part == NULL)
			return;
		const Json& value = bdb::EffectiveValue(*card, *part);
		if (!bdb::IsMediaRef(value))
			return;
		path = stack.MediaPath(value);
		title = bdb::part::Name(*part);
	}
	ShowImage(path.c_str(), title.c_str());
}


void
StackWindow::_ShowTemplateProperties()
{
	if (fMode != kModeEdit)
		_SetMode(kModeEdit);
	if (fMode != kModeEdit)
		return;
	fCardView->ClearSelection();
	_UpdateInspector();
	if (fInspector->Lock()) {
		fInspector->Activate();
		fInspector->Unlock();
	}
}


void
StackWindow::_PaperDialog()
{
	if (fMode != kModeEdit)
		_SetMode(kModeEdit);
	if (fMode != kModeEdit)
		return;
	BString size, orientation;
	int32 width, height;
	{
		DocLocker lock(fDocument);
		bdb::Template* tmpl = fCardView->EditTemplate();
		if (tmpl == NULL)
			return;
		size = tmpl->Doc().Find("paper") == NULL ? "screen"
			: tmpl->PaperSize().c_str();
		orientation = tmpl->PaperOrientation().c_str();
		width = tmpl->Width();
		height = tmpl->Height();
	}
	PaperWindow* window = new PaperWindow(BMessenger(this), size,
		orientation, width, height);
	window->Show();
}


int32
StackWindow::_PartsOutside() const
{
	DocLocker lock(fDocument);
	bdb::Template* tmpl = fCardView->EditTemplate();
	if (tmpl == NULL)
		return 0;
	int32 outside = 0;
	for (const Json& part : tmpl->Parts()) {
		bdb::Rect rect = bdb::part::GetRect(part);
		if (rect.x + rect.w > tmpl->Width() || rect.y + rect.h > tmpl->Height())
			outside++;
	}
	return outside;
}


void
StackWindow::_PaperChosen(BMessage* message)
{
	if (fMode != kModeEdit)
		return;
	BString size = message->GetString("size", "screen");
	BString orientation = message->GetString("orientation", "portrait");
	int32 width = message->GetInt32("width", bdb::kDefaultCardWidth);
	int32 height = message->GetInt32("height", bdb::kDefaultCardHeight);
	{
		DocLocker lock(fDocument);
		bdb::Template* tmpl = fCardView->EditTemplate();
		if (tmpl == NULL)
			return;
		fCardView->PushUndo();
		if (size == "screen") {
			// The screen standard has no paper (UI 3.4).
			tmpl->ClearPaper();
			if (orientation == "portrait")
				tmpl->SetSize(bdb::kDefaultCardHeight, bdb::kDefaultCardWidth);
			else
				tmpl->SetSize(bdb::kDefaultCardWidth, bdb::kDefaultCardHeight);
		} else if (size == "custom") {
			width = std::max(bdb::kMinCardSize, std::min(bdb::kMaxCardSize,
				width));
			height = std::max(bdb::kMinCardSize, std::min(bdb::kMaxCardSize,
				height));
			tmpl->SetPaper("custom", orientation.String(), width, height);
		} else
			tmpl->SetPaper(size.String(), orientation.String(), width, height);
	}
	fCardView->TemplateChanged();
	// Parts stay where they are; tell when some are outside (UI 6.15).
	int32 outside = _PartsOutside();
	if (outside > 0) {
		BString text;
		text.SetToFormat(str::kPaperOutside.Get(), (int)outside);
		_Notice(text);
	}
}


// #pragma mark - stack


void
StackWindow::_Cleanup()
{
	if (fMode == kModeBrowse) {
		_CommitFields();
		_SaveCurrent(true);
	}
	bdb::CleanupReport report;
	{
		DocLocker lock(fDocument);
		fDocument->GetStack().Cleanup(false, report);
	}
	if (report.orphanKeys == 0 && report.unusedMedia == 0
		&& report.tempFiles == 0) {
		_Notice(str::kCleanupNothing);
		return;
	}
	BString text;
	text.SetToFormat(str::kCleanupQuestion.Get(), report.orphanCards,
		report.orphanKeys, report.unusedMedia, report.tempFiles);
	if (str::gEnglish) {
		text.SetToFormat(str::kCleanupQuestion.Get(), report.orphanKeys,
			report.orphanCards, report.unusedMedia, report.tempFiles);
	}
	if (report.mediaSkipped)
		text << str::kCleanupMediaSkipped.Get();
	BAlert* alert = new BAlert(str::kAppName, text.String(), str::kCancel,
		str::kDoCleanup, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	if (alert->Go() != 1)
		return;
	{
		DocLocker lock(fDocument);
		fDocument->GetStack().Cleanup(true, report);
	}
	fCardView->Rebuild();
	_NotifyCardsChanged();
	_Notice(str::kCleanupDone);
}


void
StackWindow::_OpenLog()
{
	BdbApp* app = dynamic_cast<BdbApp*>(be_app);
	if (app == NULL || app->LogSink() == NULL)
		return;
	BString dir = app->LogSink()->DirectoryFor(fDocument->Id().String())
		.c_str();
	bdb::MakeDirectories(dir.String());
	entry_ref ref;
	if (get_ref_for_path(dir.String(), &ref) != B_OK)
		return;
	BMessage message(B_REFS_RECEIVED);
	message.AddRef("refs", &ref);
	BMessenger tracker("application/x-vnd.Be-TRAK");
	tracker.SendMessage(&message);
}


void
StackWindow::_ShowList()
{
	if (fListWindow.IsValid()) {
		BLooper* looper = NULL;
		fListWindow.Target(&looper);
		BWindow* window = dynamic_cast<BWindow*>(looper);
		if (window != NULL && window->Lock()) {
			window->Activate();
			window->Unlock();
			return;
		}
	}
	ListWindow* window = new ListWindow(fDocument, BMessenger(this));
	fListWindow = BMessenger(window);
	window->Show();
}


void
StackWindow::_SetZoom(float zoom, bool fit)
{
	// The window follows the card (kMsgCardSizeChanged), so there is no
	// fitting to the window any more.
	fCardView->SetFitToWindow(false);
	fCardView->SetScale(zoom);
	gSettings.SetZoom(fDocument->Id(), fCardView->Scale(), false);
	_UpdateZoomMenus();
	if (fMode == kModeBrowse)
		fCardView->FocusFirstField();
}


void
StackWindow::_UpdateZoomMenus()
{
	// The zoom field of the status bar and the zoom items of the view menu.
	float scale = fCardView->Scale();
	auto mark = [scale](BMenu* menu) {
		if (menu == NULL)
			return;
		for (int32 i = 0; BMenuItem* item = menu->ItemAt(i); i++) {
			if (item->Message() == NULL
				|| item->Message()->what != kMsgZoomSet) {
				continue;
			}
			float zoom = item->Message()->GetFloat("zoom", 1.0f);
			item->SetMarked(fabsf(zoom - scale) < 0.01f);
		}
	};
	if (fZoomField != NULL) {
		BMenu* menu = fZoomField->Menu();
		mark(menu);
		if (menu->FindMarked() == NULL) {
			// A scale between the levels: show it as the label.
			BString label;
			label << int32(scale * 100 + 0.5f) << "%";
			fZoomField->MenuItem()->SetLabel(label);
		}
	}
	if (BMenuItem* item = fMenuBar->FindItem(kMsgZoomIn))
		mark(item->Menu());
}


void
StackWindow::_FitWindow()
{
	// Play and find: the window is just the card (with the menus and the
	// status bar), not resizable; scroll bars only for a card larger than
	// the screen. Edit: the same size to start with, then resizable.
	if (fCardFrame == NULL || GetLayout() == NULL)
		return;
	bool fixed = fMode != kModeEdit;
	fCardFrame->SetCentered(fixed);
	BSize card = fCardView->ScaledCardSize();
	float cardWidth = ceilf(card.width);
	float cardHeight = ceilf(card.height);
	BSize minimum = GetLayout()->MinSize();
	float chromeHeight = minimum.height - fCardFrame->MinSize().height;
	float barWidth = be_control_look->GetScrollBarWidth(B_VERTICAL);
	float barHeight = be_control_look->GetScrollBarWidth(B_HORIZONTAL);

	BRect screen = BScreen(this).Frame();
	BRect frame = Frame();
	BRect decorator = DecoratorFrame();
	float borderWidth = std::max(0.0f, decorator.Width() - frame.Width());
	float borderHeight = std::max(0.0f, decorator.Height() - frame.Height());
	float maxWidth = screen.Width() + 1 - borderWidth - 8;
	float maxHeight = screen.Height() + 1 - borderHeight - 8;

	float width = maxWidth;
	float height = maxHeight - chromeHeight;
	bool horizontal = cardWidth > width;
	if (horizontal)
		height -= barHeight;
	bool vertical = cardHeight > height;
	if (vertical) {
		width -= barWidth;
		if (!horizontal && cardWidth > width)
			horizontal = true;
	}
	width = std::min(maxWidth, std::max(minimum.width + 1,
		cardWidth + (vertical ? barWidth : 0)));
	height = std::min(maxHeight, std::max(minimum.height + 1,
		cardHeight + chromeHeight + (horizontal ? barHeight : 0)));

	if (fixed) {
		SetFlags(Flags() | B_NOT_RESIZABLE | B_NOT_ZOOMABLE);
		SetSizeLimits(width - 1, width - 1, height - 1, height - 1);
	} else {
		SetFlags(Flags() & ~(B_NOT_RESIZABLE | B_NOT_ZOOMABLE));
		SetSizeLimits(minimum.width, 32768, minimum.height, 32768);
	}
	ResizeTo(width - 1, height - 1);

	// Keep the window on the screen.
	decorator = DecoratorFrame();
	float dx = 0;
	float dy = 0;
	if (decorator.right > screen.right)
		dx = screen.right - decorator.right;
	if (decorator.bottom > screen.bottom)
		dy = screen.bottom - decorator.bottom;
	if (decorator.left + dx < screen.left)
		dx = screen.left - decorator.left;
	if (decorator.top + dy < screen.top)
		dy = screen.top - decorator.top;
	if (dx != 0 || dy != 0)
		MoveBy(dx, dy);
	fCardFrame->Arrange();
}


void
StackWindow::_SavePdf(uint32 what)
{
	if (what != kMsgSaveCardPdf)
		return;
	if (!_LeaveCard() && fDirty)
		return;
	_EnsureCard();
	BString name;
	{
		DocLocker lock(fDocument);
		bdb::Stack& stack = fDocument->GetStack();
		const bdb::Card* card = stack.FindCard(fCurrentId);
		if (card == NULL)
			return;
		// "<stack>-<card title>.pdf" (I/O 4.4)
		name << bdb::SanitizeTitle(stack.Title()).c_str() << "-"
			<< bdb::SanitizeTitle(stack.TitleOf(*card)).c_str() << ".pdf";
	}
	fPdfKind = what;
	ShowPdfSavePanel(BMessenger(this), name, false);
}


void
StackWindow::_PdfPanelDone(BMessage* message)
{
	BString path;
	if (!PdfPanelPath(message, path))
		return;
	PdfJob* job = new PdfJob();
	job->document = fDocument;
	job->kind = PdfJob::kCards;
	job->cardIds.push_back(fCurrentId);
	job->path = path;
	job->printBackground = message->GetBool("background", true);
	job->replyTo = BMessenger(this);
	StartPdfExport(job);
}


void
StackWindow::_ImportCsv()
{
	if (!_LeaveCard())
		return;
	CsvWizard* wizard = new CsvWizard(fDocument, BMessenger(this));
	wizard->Show();
}


void
StackWindow::StartFetching()
{
	if (fFetcher != NULL)
		return;
	std::vector<bdb::FetchItem> items;
	{
		DocLocker lock(fDocument);
		items = bdb::FindImagesToFetch(fDocument->GetStack());
	}
	fFetchDone = 0;
	fFetchOk = 0;
	fFetchFailed = 0;
	fFetchTotal = int32(items.size());
	if (items.empty()) {
		BString text;
		text.SetToFormat(str::kStatusFetchDone.Get(), 0, 0);
		_Hint(text);
		return;
	}
	fFetcher = new ImageFetcher(BMessenger(this), items);
	fFetcher->Start();
	_UpdateStatus();
}


void
StackWindow::_FetchResult(BMessage* message)
{
	BString cardId = message->GetString("card", "");
	BString partId = message->GetString("part", "");
	if (!message->GetBool("ok", false)) {
		fFetchFailed++;
		DocLocker lock(fDocument);
		// Reasons only; never the URL (I/O 3.6).
		fDocument->GetStack().Log().Warn("image.fetch.failed", {
			{"card", cardId.String()}, {"part", partId.String()},
			{"reason", message->GetString("reason", "unknown")}});
		return;
	}
	const void* data = NULL;
	ssize_t size = 0;
	if (message->FindData("data", B_RAW_TYPE, &data, &size) != B_OK)
		return;
	std::string bytes(static_cast<const char*>(data), size_t(size));
	bdb::StringInStream stream(bytes);
	bdb::Status status;
	bool current = cardId == fCurrentId.c_str();
	{
		DocLocker lock(fDocument);
		bdb::Stack& stack = fDocument->GetStack();
		bdb::Card* card = stack.FindCard(cardId.String());
		bdb::Template* tmpl = card != NULL
			? stack.FindTemplate(card->TemplateId()) : NULL;
		const Json* part = tmpl != NULL ? tmpl->FindPart(partId.String())
			: NULL;
		// Fill only images that are still empty.
		if (part == NULL || bdb::IsMediaRef(bdb::EffectiveValue(*card,
				*part))) {
			return;
		}
		status = stack.SetMedia(cardId.String(), partId.String(), "image",
			message->GetString("ext", "jpg"), stream);
	}
	if (status) {
		fFetchOk++;
		if (current && !fDirty)
			fCardView->Rebuild();
		_NotifyCardChanged(cardId.String());
	} else
		fFetchFailed++;
}


void
StackWindow::_FetchProgress(BMessage* message)
{
	fFetchDone = message->GetInt32("done", fFetchDone);
	if (message->GetBool("finished", false)) {
		delete fFetcher;
		fFetcher = NULL;
		BString text;
		text.SetToFormat(str::kStatusFetchDone.Get(), (int)fFetchOk,
			(int)fFetchFailed);
		fHint = text;
		fFetchTotal = 0;
	}
	_UpdateStatus();
}


// #pragma mark - helpers


void
StackWindow::_NotifyCardChanged(const std::string& id)
{
	BMessage message(kMsgDocCardChanged);
	message.AddString("card", id.c_str());
	message.AddPointer("source", this);
	fDocument->Notify(&message);
}


void
StackWindow::_NotifyCardsChanged()
{
	BMessage message(kMsgDocCardsChanged);
	message.AddPointer("source", this);
	fDocument->Notify(&message);
}


void
StackWindow::_ShowError(const char* format, const bdb::Status& status)
{
	BString text;
	text.SetToFormat(format, status.Code());
	BAlert* alert = new BAlert(str::kAppName, text.String(), str::kOK, NULL,
		NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->Go();
}


void
StackWindow::_Notice(const char* text)
{
	BAlert* alert = new BAlert(str::kAppName, text, str::kOK, NULL, NULL,
		B_WIDTH_AS_USUAL, B_INFO_ALERT);
	alert->Go(NULL);
}


static BMessenger
ShowAux(BMessenger& existing, BWindow* (*factory)(StackDocument*),
	StackDocument* document)
{
	BLooper* looper = NULL;
	if (existing.IsValid() && existing.Target(&looper) != NULL) {
		BWindow* window = dynamic_cast<BWindow*>(looper);
		if (window != NULL && window->Lock()) {
			window->Activate();
			window->Unlock();
			return existing;
		}
	}
	BWindow* window = factory(document);
	window->Show();
	return BMessenger(window);
}


// #pragma mark - messages


void
StackWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_COLORS_UPDATED:
			// The haiku theme takes the system's colors (theme 4.2).
			if (fCardView->UsesSystemColors()) {
				if (fMode == kModeBrowse)
					_CommitFields();
				fCardView->ThemeChanged();
			} else
				fCardView->Invalidate();
			BWindow::MessageReceived(message);
			break;
		// modes
		case kMsgModeBrowse:
			_SetMode(kModeBrowse);
			break;
		case kMsgModeEdit:
			_SetMode(kModeEdit);
			break;
		case kMsgModeFind:
			_SetMode(kModeFind);
			break;
		case kMsgStatusClicked:
			if (fMode != kModeEdit)
				_SetMode(kModeEdit);
			break;

		// navigation (find mode: within the results, UI 10.3)
		case kMsgFirstCard:
		case kMsgLastCard:
		case kMsgPrevCard:
		case kMsgNextCard:
			if (fMode == kModeFind)
				_SetMode(kModeBrowse);
			if (fMode != kModeBrowse)
				break;
			if (message->what == kMsgFirstCard)
				_GoToIndex(0);
			else if (message->what == kMsgLastCard)
				_GoToIndex(INT32_MAX);
			else
				_Step(message->what == kMsgPrevCard ? -1 : 1);
			break;
		case kMsgGoToNumber:
			if (fMode == kModeBrowse)
				_GoToNumberDialog();
			break;
		case kMsgGoToNumberDone:
		{
			int32 number = atoi(message->GetString("text", ""));
			if (fMode == kModeBrowse && number > 0)
				_GoToIndex(number - 1);
			break;
		}
		case kMsgGoToCard:
		{
			BString id;
			if (message->FindString("card", &id) != B_OK)
				break;
			if (fMode != kModeBrowse)
				_SetMode(kModeBrowse);
			if (fMode != kModeBrowse)
				break;
			if (fFindActive && std::find(fFound.begin(), fFound.end(),
					id.String()) == fFound.end()) {
				_ShowAll();
			}
			_GoTo(id.String());
			Activate();
			break;
		}

		// cards
		case kMsgNewCard:
			if (fMode == kModeBrowse)
				_NewCard();
			break;
		case kMsgDuplicateCard:
			if (fMode == kModeBrowse)
				_DuplicateCard();
			break;
		case kMsgDeleteCard:
			if (fMode == kModeBrowse)
				_DeleteCard();
			break;
		case kMsgChangeTemplate:
		{
			BString id;
			if (message->FindString("template", &id) == B_OK)
				_ChangeTemplate(id.String());
			break;
		}
		case kMsgButtonAction:
			_ButtonAction(message->GetString("part", ""),
				message->GetString("action", ""));
			break;
		case kMsgBoolToggle:
			_ToggleBool(message->GetString("part", ""));
			break;
		case kMsgOpenLink:
			_OpenLink(message->GetString("part", ""));
			break;
		case kMsgHint:
			fHint = message->GetString("text", "");
			_UpdateStatus();
			break;

		// find
		case kMsgRunFind:
			_RunFind();
			break;
		case kMsgClearFind:
			fCardView->ClearFindConditions();
			break;
		case kMsgShowAll:
			_ShowAll();
			if (fMode == kModeFind)
				_SetMode(kModeBrowse);
			break;

		// fields
		case kMsgFieldEdited:
			_FieldEdited(message->GetString("part", ""));
			break;
		case kMsgFieldCommit:
		{
			const char* partId = message->GetString("part", "");
			FieldTextView* field = fCardView->FindField(partId);
			if (fMode == kModeBrowse && field != NULL && field->IsNumber())
				_CommitNumber(partId, field->RawText());
			break;
		}
		case kMsgFieldEnter:
		{
			const char* partId = message->GetString("part", "");
			if (fMode == kModeFind) {
				_RunFind();
				break;
			}
			FieldTextView* field = fCardView->FindField(partId);
			if (field != NULL)
				fCardView->FocusNeighbor(field, false);
			break;
		}
		case kMsgFieldGuarded:
			_ConfirmGuarded(message->GetString("part", ""));
			break;
		case kMsgAutosave:
			delete fAutosave;
			fAutosave = NULL;
			if (fMode == kModeBrowse)
				_SaveCurrent(false);
			break;

		// images
		case kMsgImageSet:
		case kMsgImagePanelDone:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK)
				_SetImage(message->GetString("part", ""), ref);
			break;
		}
		case kMsgImageChoose:
			_ChooseImage(message->GetString("part", ""));
			break;
		case kMsgImageView:
			_ViewImage(message->GetString("part", ""));
			break;
		case kMsgMediaChosen:
			_MediaChosen(message);
			break;
		case kMsgSaveCardImage:
			_SaveCardImage(message);
			break;
		case kMsgPreferences:
			ShowPreferences();
			break;
		case kMsgVideoFailed:
			_Hint(str::kVideoFailed);
			break;
		case kMsgImageClear:
			_ClearImage(message->GetString("part", ""));
			break;

		// edit menu
		case kMsgUndo:
		case kMsgRedo:
		case kMsgCut:
		case kMsgCopy:
		case kMsgPaste:
		case kMsgSelectAll:
		{
			BView* focus = CurrentFocus();
			bool textFocus = dynamic_cast<BTextView*>(focus) != NULL;
			if (fMode == kModeEdit && !textFocus) {
				switch (message->what) {
					case kMsgUndo: fCardView->Undo(); break;
					case kMsgRedo: fCardView->Redo(); break;
					case kMsgCut: fCardView->CutSelection(); break;
					case kMsgCopy: fCardView->CopySelection(); break;
					case kMsgPaste: fCardView->Paste(); break;
					case kMsgSelectAll: fCardView->SelectAll(); break;
				}
				break;
			}
			if (focus == NULL)
				break;
			uint32 forward = B_UNDO;
			switch (message->what) {
				case kMsgRedo: forward = B_REDO; break;
				case kMsgCut: forward = B_CUT; break;
				case kMsgCopy: forward = B_COPY; break;
				case kMsgPaste: forward = B_PASTE; break;
				case kMsgSelectAll: forward = B_SELECT_ALL; break;
			}
			PostMessage(forward, focus);
			break;
		}
		case kMsgDuplicatePart:
			if (fMode == kModeEdit)
				fCardView->DuplicateSelection();
			break;
		case kMsgDeletePart:
			if (fMode == kModeEdit)
				fCardView->DeleteSelection();
			break;
		case kMsgToggleSnap:
			fCardView->SetSnap(!fCardView->Snap());
			_BuildMenus();
			break;
		case kMsgToggleGrid:
			fCardView->SetShowGrid(!fCardView->ShowGrid());
			_BuildMenus();
			break;

		// edit mode: parts and template
		case kMsgPaletteSelect:
			if (message->GetBool("fromPalette", false))
				fCardView->SetCreateItem(message->GetInt32("item", -1));
			else
				_SetCreateItem(-1);
			break;
		case kMsgPartCreated:
		{
			// Name the new part right away (UI 6.2).
			_UpdateInspector();
			BString kind = message->GetString("kind", "");
			if (fInspector->Lock()) {
				if (kind == "button")
					fInspector->FocusField("text");
				else if (kind != "label" && kind != "counter")
					fInspector->FocusField("name");
				fInspector->Unlock();
			}
			break;
		}
		case kMsgBringToFront:
			if (fMode == kModeEdit)
				fCardView->BringToFront();
			break;
		case kMsgSendToBack:
			if (fMode == kModeEdit)
				fCardView->SendToBack();
			break;
		case kMsgLock:
		case kMsgUnlock:
		case kMsgLockAll:
		case kMsgUnlockAll:
			if (fMode == kModeEdit) {
				fCardView->SetLocked(message->what == kMsgLock
						|| message->what == kMsgLockAll,
					message->what == kMsgLockAll
						|| message->what == kMsgUnlockAll);
			}
			break;
		case kMsgChooseIcon:
			_ChooseIcon();
			break;
		case kMsgIconChosen:
			_IconChosen(message);
			break;
		case kMsgSetLink:
			if (fMode == kModeEdit && fInspector->Lock()) {
				fInspector->Activate();
				fInspector->FocusField("link_url");
				fInspector->Unlock();
			}
			break;
		case kMsgTemplateProps:
		case kMsgBackgroundDialog:
		case kMsgThemeDialog:
			_ShowTemplateProperties();
			break;
		case kMsgPaperDialog:
			_PaperDialog();
			break;
		case kMsgPaperChosen:
			_PaperChosen(message);
			break;
		case kMsgBackgroundPanel:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) != B_OK || fMode != kModeEdit)
				break;
			Json mediaRef;
			if (!_AddMediaFromFile(ref, false, "", mediaRef))
				break;
			DocLocker lock(fDocument);
			bdb::Template* tmpl = fCardView->EditTemplate();
			if (tmpl == NULL)
				break;
			fCardView->PushUndo();
			tmpl->MutableBackground().Set("image", mediaRef);
			fCardView->TemplateChanged();
			break;
		}
		case kMsgEditImageDrop:
			_EditImageDrop(message);
			break;
		case kMsgPartKind:
			_ChangeKind(message->GetString("kind", ""));
			break;
		case kMsgSelectionChanged:
			_UpdateInspector();
			break;
		case kMsgTemplateEdited:
			_TemplateEdited(message);
			break;
		case kMsgInspectorChanged:
			_ApplyInspector(message->GetString("field", ""),
				message->GetString("value", ""));
			break;
		case kMsgInspectorKind:
			_ChangeKind(message->GetString("kind", ""));
			break;

		// view
		case kMsgShowList:
			_ShowList();
			break;
		case kMsgZoomIn:
		case kMsgZoomOut:
		{
			float current = fCardView->Scale();
			float zoom = current;
			if (message->what == kMsgZoomIn) {
				for (int32 i = 0; i < kZoomLevelCount; i++) {
					if (kZoomLevels[i] > current + 0.01f) {
						zoom = kZoomLevels[i];
						break;
					}
				}
			} else {
				for (int32 i = kZoomLevelCount - 1; i >= 0; i--) {
					if (kZoomLevels[i] < current - 0.01f) {
						zoom = kZoomLevels[i];
						break;
					}
				}
			}
			_SetZoom(zoom, false);
			break;
		}
		case kMsgZoomActual:
			_SetZoom(1.0f, false);
			break;
		case kMsgZoomSet:
			_SetZoom(message->GetFloat("zoom", 1.0f), false);
			break;
		case kMsgCardSizeChanged:
		case kMsgFitWindow:
			_FitWindow();
			break;
		case kMsgPaletteTheme:
			if (fPalette != NULL)
				fPalette->PostMessage(message);
			break;
		case kMsgZoomFit:
			_SetZoom(fCardView->Scale(), !fCardView->FitToWindow());
			break;

		// stack
		case kMsgImportCsv:
			_ImportCsv();
			break;
		case kMsgFetchImages:
			StartFetching();
			break;
		case kMsgFetchResult:
			_FetchResult(message);
			break;
		case kMsgFetchProgress:
			_FetchProgress(message);
			break;
		case kMsgSaveCardPdf:
			_SavePdf(message->what);
			break;
		case kMsgPdfPanel:
			_PdfPanelDone(message);
			break;
		case kMsgPdfDone:
			ReportPdfResult(message);
			break;
		case kMsgCleanup:
			_Cleanup();
			break;
		case kMsgProblems:
			fAuxWindows[kAuxProblems] = ShowAux(fAuxWindows[kAuxProblems],
				&CreateProblemsWindow, fDocument);
			break;
		case kMsgStackSettings:
			fAuxWindows[kAuxSettings] = ShowAux(fAuxWindows[kAuxSettings],
				&CreateStackSettingsWindow, fDocument);
			break;
		case kMsgManageTemplates:
			if (fMode == kModeBrowse) {
				_CommitFields();
				_SaveCurrent(true);
			}
			fAuxWindows[kAuxTemplates] = ShowAux(fAuxWindows[kAuxTemplates],
				&CreateTemplateManager, fDocument);
			break;
		case kMsgOpenLog:
			_OpenLog();
			break;
		case kMsgAbout:
		{
			BString text;
			text.SetToFormat(str::kAboutText.Get(), BDB_VERSION);
			_Notice(text.String());
			break;
		}

		// document notifications
		case kMsgDocCardChanged:
		{
			void* source = NULL;
			message->FindPointer("source", &source);
			BString id = message->GetString("card", "");
			if (source != this && id == fCurrentId.c_str() && !fDirty)
				fCardView->Rebuild();
			_UpdateStatus();
			break;
		}
		case kMsgDocCardsChanged:
		{
			void* source = NULL;
			message->FindPointer("source", &source);
			if (source != this) {
				bool missing;
				{
					DocLocker lock(fDocument);
					missing = fDocument->GetStack().FindCard(fCurrentId)
						== NULL;
				}
				if (missing) {
					fCurrentId.clear();
					fDirty = false;
					_GoToIndex(0);
				} else if (!fDirty && fMode != kModeEdit)
					fCardView->Rebuild();
			}
			_UpdateStatus();
			break;
		}
		case kMsgDocTemplateChanged:
		{
			void* source = NULL;
			message->FindPointer("source", &source);
			if (source == this)
				break;
			if (fMode == kModeEdit) {
				fCardView->ThemeChanged();
				fCardView->TemplateChanged(false);
			} else if (!fDirty)
				fCardView->Rebuild();
			_UpdateStatus();
			break;
		}
		case kMsgDocStackChanged:
			_UpdateTitle();
			_UpdateStatus();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
StackWindow::QuitRequested()
{
	if (fMode == kModeBrowse) {
		_CommitFields();
		if (!_SaveCurrent(true) && fDirty) {
			BAlert* alert = new BAlert(str::kAppName, str::kUnsavedChanges,
				str::kCancel, str::kCloseWithoutSaving, NULL,
				B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			if (alert->Go() != 1)
				return false;
		}
		// An untouched provisional card is not saved (UI 5.5).
		DocLocker lock(fDocument);
		if (fDocument->GetStack().IsProvisional(fCurrentId))
			fDocument->GetStack().DiscardProvisional();
	} else if (fMode == kModeEdit) {
		if (!_LeaveEdit()) {
			BAlert* alert = new BAlert(str::kAppName, str::kUnsavedChanges,
				str::kCancel, str::kCloseWithoutSaving, NULL,
				B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			if (alert->Go() != 1)
				return false;
		}
	}
	if (fFetcher != NULL)
		fFetcher->Cancel();

	gSettings.SetWindowFrame(fDocument->Id(), Frame());
	gSettings.SetZoom(fDocument->Id(), fCardView->Scale(),
		fCardView->FitToWindow());

	BWindow* floating[] = { fPalette, fInspector };
	for (BWindow* window : floating) {
		if (window != NULL && window->Lock())
			window->Quit();
	}
	fPalette = NULL;
	fInspector = NULL;
	if (fListWindow.IsValid())
		fListWindow.SendMessage(B_QUIT_REQUESTED);
	for (BMessenger& aux : fAuxWindows) {
		if (aux.IsValid())
			aux.SendMessage(B_QUIT_REQUESTED);
	}

	fDocument->RemoveObserver(BMessenger(this));
	BMessage released(kMsgDocumentReleased);
	released.AddPointer("document", fDocument);
	be_app->PostMessage(&released);
	return true;
}
