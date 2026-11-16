/*
 * BDB card database - RenkuOS application
 */
#include "EditWindows.h"

#include <Bitmap.h>
#include <Button.h>
#include <CheckBox.h>
#include <ColorControl.h>
#include <Directory.h>
#include <Entry.h>
#include <FilePanel.h>
#include <GridLayout.h>
#include <GroupLayout.h>
#include <IconUtils.h>
#include <MessageRunner.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Screen.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <Slider.h>
#include <StringView.h>
#include <TextControl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

#include "CardPainter.h"
#include "Constants.h"
#include "Model.h"
#include "Settings.h"
#include "Store.h"
#include "Strings.h"


static const uint32 kMsgInspectorText = 'iTxt';
static const uint32 kMsgInspectorMenu = 'iMnu';
static const uint32 kMsgInspectorCheck = 'iChk';
static const uint32 kMsgInspectorButton = 'iBtn';
static const uint32 kMsgInspectorColor = 'iCol';
static const uint32 kMsgColorChanged = 'cChg';
static const uint32 kMsgColorText = 'cTxt';
static const uint32 kMsgColorRecent = 'cRec';
static const uint32 kMsgColorOK = 'cOK ';
static const uint32 kMsgIconSearch = 'icSr';
static const uint32 kMsgIconSelect = 'icSl';
static const uint32 kMsgIconInvoke = 'icIv';
static const uint32 kMsgIconOK = 'icOK';
static const uint32 kMsgIconSize = 'icSz';
static const uint32 kMsgIconPlacement = 'icPl';
static const uint32 kMsgIconFile = 'icFl';
static const uint32 kMsgIconFileRef = 'icFr';


const char*
PaletteLabel(int32 index)
{
	static const str::Text* const kLabels[] = {
		&str::kPaletteBlank, &str::kPaletteText, &str::kPaletteNumber,
		&str::kPaletteBool, &str::kPaletteImage, &str::kPaletteLabel,
		&str::kPaletteButton, &str::kPaletteFirst, &str::kPalettePrev,
		&str::kPaletteNext, &str::kPaletteLast, &str::kPaletteNavFull,
		&str::kPaletteCounter, &str::kPaletteNav
	};
	if (index < 0 || index >= int32(sizeof(kLabels) / sizeof(kLabels[0])))
		return str::kToolSelect;
	return kLabels[index]->Get();
}


static rgb_color
ParseRgb(const char* text, bool* ok = NULL)
{
	unsigned int r = 0, g = 0, b = 0;
	bool valid = text != NULL && strlen(text) == 7 && text[0] == '#'
		&& sscanf(text + 1, "%2x%2x%2x", &r, &g, &b) == 3;
	if (ok != NULL)
		*ok = valid;
	return make_color(r, g, b, 255);
}


static BString
RgbText(rgb_color color)
{
	char buffer[8];
	snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", color.red, color.green,
		color.blue);
	return buffer;
}


// #pragma mark - NameTip


// The name of a palette item, shown after the pointer rests on it.
class NameTip : public BWindow {
public:
	NameTip()
		:
		BWindow(BRect(0, 0, 40, 16), "name tip", B_BORDERED_WINDOW_LOOK,
			B_FLOATING_ALL_WINDOW_FEEL, B_NOT_MOVABLE | B_NOT_CLOSABLE
				| B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_NOT_RESIZABLE
				| B_AVOID_FOCUS | B_AUTO_UPDATE_SIZE_LIMITS)
	{
		fText = new BStringView("text", "");
		fText->SetHighUIColor(B_TOOL_TIP_TEXT_COLOR);
		BView* back = new BView("back", 0);
		back->SetViewUIColor(B_TOOL_TIP_BACKGROUND_COLOR);
		fText->SetViewUIColor(B_TOOL_TIP_BACKGROUND_COLOR);
		fText->SetLowUIColor(B_TOOL_TIP_BACKGROUND_COLOR);
		BLayoutBuilder::Group<>(back, B_VERTICAL)
			.SetInsets(5, 2, 5, 2)
			.Add(fText);
		BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
			.Add(back);
	}

	void ShowAt(const char* text, BPoint where)
	{
		if (!Lock())
			return;
		fText->SetText(text);
		InvalidateLayout(true);
		BSize size = GetLayout()->PreferredSize();
		ResizeTo(size.width, size.height);
		BRect screen = BScreen(this).Frame();
		where.x = std::min(where.x, screen.right - size.width - 4);
		where.y = std::min(where.y, screen.bottom - size.height - 4);
		MoveTo(where);
		if (IsHidden())
			Show();
		Unlock();
	}

	void HideTip()
	{
		if (!Lock())
			return;
		if (!IsHidden())
			Hide();
		Unlock();
	}

private:
	BStringView*	fText;
};


// #pragma mark - PaletteView


static const bigtime_t kTipDelay = 2000000;


// Where each palette item is shown (palette coordinates), like the
// palettes of NeXT's Interface Builder: the parts themselves, no names.
struct PaletteCell {
	int32	item;
	float	x;
	float	y;
	float	width;
	float	height;
};

static const PaletteCell kPaletteCells[] = {
	{ 1, 8, 8, 128, 26 },		// text
	{ 2, 144, 8, 72, 26 },		// number
	{ 3, 8, 44, 96, 26 },		// switch
	{ 5, 112, 44, 104, 26 },	// label
	{ 6, 8, 80, 96, 28 },		// button
	{ 0, 112, 80, 104, 28 },	// blank frame
	{ 4, 8, 118, 96, 66 },		// image
	{ 12, 112, 118, 104, 22 },	// counter
	{ 7, 8, 194, 47, 28 },		// first
	{ 8, 61, 194, 47, 28 },		// previous
	{ 9, 114, 194, 47, 28 },	// next
	{ 10, 167, 194, 49, 28 },	// last
	{ 13, 8, 232, 208, 28 },	// previous, counter, next
	{ 11, 8, 270, 208, 28 }		// first ... last
};
static const int32 kPaletteCellCount
	= sizeof(kPaletteCells) / sizeof(kPaletteCells[0]);
static const float kPaletteWidth = 224;
static const float kPaletteHeight = 306;


// The palette: the parts in the card's theme. Drag a part onto the card
// to place it (UI 6.2); resting the pointer on a part for two seconds
// shows its name.
class PaletteView : public BView {
public:
	PaletteView(BMessenger target)
		:
		BView("palette", B_WILL_DRAW | B_FRAME_EVENTS | B_PULSE_NEEDED),
		fTarget(target),
		fTheme(bdb::Theme::Default()),
		fPressed(-1),
		fHover(-1),
		fDragging(false),
		fHoverSince(0),
		fTipShown(false),
		fTip(NULL)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
		SetExplicitMinSize(BSize(kPaletteWidth - 1, kPaletteHeight - 1));
		SetExplicitMaxSize(BSize(kPaletteWidth - 1, kPaletteHeight - 1));
	}

	virtual ~PaletteView()
	{
		if (fTip != NULL && fTip->Lock())
			fTip->Quit();
	}

	void SetSelected(int32 item)
	{
		// Parts are placed by dragging only.
	}

	void SetTheme(const char* document)
	{
		bdb::Theme theme;
		if (document == NULL
			|| !bdb::Theme::Parse(document, theme).IsOk()) {
			return;
		}
		fTheme = theme;
		Invalidate();
	}

	virtual void Draw(BRect updateRect)
	{
		SetHighColor(ToRgb(fTheme.GetColor("panel")));
		FillRect(updateRect);
		for (int32 i = 0; i < kPaletteCellCount; i++) {
			BRect cell = _CellRect(i);
			if (!cell.Intersects(updateRect))
				continue;
			_DrawItem(this, i, cell);
		}
	}

	virtual void MouseDown(BPoint where)
	{
		_SetHover(-1);
		fPressed = _CellAt(where);
		fDownPoint = where;
		fDragging = false;
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	}

	virtual void MouseMoved(BPoint where, uint32 code,
		const BMessage* dragMessage)
	{
		if (fPressed < 0) {
			// The name shows after the pointer rests on a part.
			_SetHover(code == B_EXITED_VIEW || dragMessage != NULL
				? -1 : _CellAt(where));
			return;
		}
		if (fDragging || (fabsf(where.x - fDownPoint.x) < 3
				&& fabsf(where.y - fDownPoint.y) < 3)) {
			return;
		}
		fDragging = true;
		_StartDrag(fPressed, fDownPoint);
	}

	virtual void MouseUp(BPoint where)
	{
		fPressed = -1;
		fDragging = false;
	}

	virtual void Pulse()
	{
		// The name shows after the pointer has rested on a part for two
		// seconds, and goes as soon as the pointer leaves it.
		BPoint where;
		uint32 buttons;
		GetMouse(&where, &buttons, false);
		int32 cell = Window() != NULL && Window()->Frame().Contains(
			ConvertToScreen(where)) && buttons == 0 && fPressed < 0
			? _CellAt(where) : -1;
		_SetHover(cell);
		if (fHover >= 0 && !fTipShown
			&& system_time() - fHoverSince >= kTipDelay) {
			fTipShown = true;
			// Made by the palette's thread: a window belongs (is locked)
			// to the thread that makes it until it runs.
			if (fTip == NULL)
				fTip = new NameTip();
			fTip->ShowAt(PaletteLabel(kPaletteCells[fHover].item),
				ConvertToScreen(where) + BPoint(12, 18));
		}
	}

private:
	BRect _CellRect(int32 cell) const
	{
		const PaletteCell& c = kPaletteCells[cell];
		return BRect(c.x, c.y, c.x + c.width - 1, c.y + c.height - 1);
	}

	int32 _CellAt(BPoint where) const
	{
		for (int32 i = 0; i < kPaletteCellCount; i++) {
			if (_CellRect(i).Contains(where))
				return i;
		}
		return -1;
	}

	void _SetHover(int32 cell)
	{
		if (cell == fHover)
			return;
		fHover = cell;
		fHoverSince = system_time();
		if (fTipShown && fTip != NULL) {
			fTip->HideTip();
			fTipShown = false;
		}
	}

	// The parts of an item, at their own size, and the size of the group.
	static std::vector<bdb::Json> _Parts(int32 item, BSize* size)
	{
		std::vector<bdb::Json> parts;
		const PaletteItem& entry = kPaletteItems[item];
		float width = 0;
		float height = 0;
		for (int32 i = 0; i < entry.count; i++) {
			const PalettePiece& piece = entry.pieces[i];
			bdb::Rect rect;
			rect.x = piece.x;
			rect.y = piece.y;
			rect.w = piece.width;
			rect.h = piece.height;
			BString id;
			id << "p" << i;
			bdb::Json part = bdb::part::Make(piece.kind, id.String(), "", rect);
			std::string kind = piece.kind;
			if (kind == bdb::kKindLabel)
				part.Set("text", str::kNewLabelText.Get());
			if (kind == bdb::kKindButton) {
				part.Set("text", piece.text[0] != '\0' ? piece.text
					: str::kNewButtonText.Get());
			}
			parts.push_back(part);
			width = std::max(width, float(piece.x + piece.width));
			height = std::max(height, float(piece.y + piece.height));
		}
		*size = BSize(width, height);
		return parts;
	}

	// Draws a palette item into `rect` of `view`: a single part fills the
	// cell, a group of parts is scaled down to it.
	void _DrawItem(BView* view, int32 cell, BRect rect)
	{
		int32 item = kPaletteCells[cell].item;
		BSize size;
		std::vector<bdb::Json> parts = _Parts(item, &size);
		float scale = 1.0f;
		BPoint origin = rect.LeftTop();
		if (parts.size() == 1) {
			bdb::Rect r;
			r.x = 0;
			r.y = 0;
			r.w = int32(rect.Width() + 1);
			r.h = int32(rect.Height() + 1);
			bdb::part::SetRect(parts[0], r);
		} else {
			scale = std::min(1.0f, std::min((rect.Width() + 1) / size.width,
				(rect.Height() + 1) / size.height));
			origin.x += floorf((rect.Width() + 1 - size.width * scale) / 2);
			origin.y += floorf((rect.Height() + 1 - size.height * scale) / 2);
		}
		_Paint(view, parts, scale, origin);
	}

	void _Paint(BView* view, const std::vector<bdb::Json>& parts, float scale,
		BPoint origin)
	{
		ViewCanvas canvas(view, scale, origin);
		PaintContext context;
		context.theme = &fTheme;
		context.position = 1;
		context.count = 3;
		for (const bdb::Json& part : parts) {
			std::string kind = bdb::part::Kind(part);
			bdb::Rect r = bdb::part::GetRect(part);
			BRect frame(r.x, r.y, r.x + r.w - 1, r.y + r.h - 1);
			if (kind == bdb::kKindBlank) {
				// A dashed frame (the "未設定" frame without its caption).
				rgb_color line = ToRgb(fTheme.GetColor("text_disabled"));
				for (float x = frame.left; x <= frame.right; x += 6) {
					float end = std::min(frame.right, x + 2);
					canvas.FillRect(BRect(x, frame.top, end, frame.top), line);
					canvas.FillRect(BRect(x, frame.bottom, end, frame.bottom),
						line);
				}
				for (float y = frame.top; y <= frame.bottom; y += 6) {
					float end = std::min(frame.bottom, y + 2);
					canvas.FillRect(BRect(frame.left, y, frame.left, end),
						line);
					canvas.FillRect(BRect(frame.right, y, frame.right, end),
						line);
				}
				continue;
			}
			// Images and value fields without values, as on a new card.
			context.mode = kind == bdb::kKindImage ? kPaintFind : kPaintPlay;
			PaintPart(canvas, context, part);
			bdb::PartLook look = fTheme.Resolve(part);
			BRect inner = FrameInset(frame, look).InsetByCopy(2, 1);
			rgb_color sample = Mix(ToRgb(look.textColor),
				ToRgb(fTheme.GetColor("text_disabled")), 0.5f);
			BFont font = FontForLook(look);
			if (kind == bdb::kKindText) {
				DrawTextBlock(canvas, "Abc", inner, font, sample, false,
					B_ALIGN_LEFT, true);
			} else if (kind == bdb::kKindNumber) {
				DrawTextBlock(canvas, "123", inner, font, sample, false,
					B_ALIGN_RIGHT, true);
			} else if (kind == bdb::kKindImage) {
				// A small landscape picture.
				BRect pic = FrameInset(frame, look);
				float w = pic.Width();
				float h = pic.Height();
				BRect sun(pic.left + w * 0.62f, pic.top + h * 0.18f,
					pic.left + w * 0.78f, pic.top + h * 0.18f + w * 0.16f);
				canvas.FillEllipse(sun, sample);
				float base = pic.bottom - h * 0.12f;
				for (float x = pic.left + w * 0.1f; x < pic.right - w * 0.1f;
						x += 1) {
					float t = (x - pic.left) / w;
					float top = base - h * 0.5f * (t < 0.45f ? t / 0.45f
						: (1 - t) / 0.55f);
					canvas.FillRect(BRect(x, top, x, base), sample);
				}
			}
		}
	}

	static rgb_color Mix(rgb_color a, rgb_color b, float amount)
	{
		return make_color(uint8(a.red + (b.red - a.red) * amount),
			uint8(a.green + (b.green - a.green) * amount),
			uint8(a.blue + (b.blue - a.blue) * amount));
	}

	// Drags the item at its own size; the part is held where it was
	// grabbed.
	void _StartDrag(int32 cell, BPoint where)
	{
		int32 item = kPaletteCells[cell].item;
		BSize size;
		std::vector<bdb::Json> parts = _Parts(item, &size);
		BRect cellRect = _CellRect(cell);
		BPoint offset((where.x - cellRect.left) / (cellRect.Width() + 1)
				* size.width,
			(where.y - cellRect.top) / (cellRect.Height() + 1) * size.height);
		if (parts.size() > 1) {
			float scale = std::min(1.0f, std::min(
				(cellRect.Width() + 1) / size.width,
				(cellRect.Height() + 1) / size.height));
			BPoint origin(cellRect.left + floorf((cellRect.Width() + 1
					- size.width * scale) / 2),
				cellRect.top + floorf((cellRect.Height() + 1
					- size.height * scale) / 2));
			offset = BPoint((where.x - origin.x) / scale,
				(where.y - origin.y) / scale);
		}
		offset.x = std::max(0.0f, std::min(size.width - 1, offset.x));
		offset.y = std::max(0.0f, std::min(size.height - 1, offset.y));

		BMessage drag(kMsgPaletteDrag);
		drag.AddInt32("item", item);
		drag.AddPoint("offset", offset);

		BRect bounds(0, 0, size.width - 1, size.height - 1);
		BBitmap* bitmap = new BBitmap(bounds, B_RGBA32, true);
		if (bitmap->InitCheck() == B_OK && bitmap->Lock()) {
			BView* view = new BView(bounds, "drag", B_FOLLOW_NONE, 0);
			bitmap->AddChild(view);
			view->SetHighColor(ToRgb(fTheme.GetColor("panel")));
			view->FillRect(bounds);
			_Paint(view, parts, 1.0f, B_ORIGIN);
			view->Sync();
			bitmap->RemoveChild(view);
			delete view;
			bitmap->Unlock();
			// Half transparent while dragged.
			uint8* bits = (uint8*)bitmap->Bits();
			int32 length = bitmap->BitsLength();
			for (int32 i = 3; i < length; i += 4)
				bits[i] = 180;
			DragMessage(&drag, bitmap, B_OP_ALPHA, offset, this);
		} else {
			delete bitmap;
			DragMessage(&drag, cellRect, this);
		}
	}

	BMessenger		fTarget;
	bdb::Theme		fTheme;
	int32			fPressed;
	int32			fHover;
	BPoint			fDownPoint;
	bool			fDragging;
	bigtime_t		fHoverSince;
	bool			fTipShown;
	NameTip*		fTip;
};


// #pragma mark - ToolPalette


ToolPalette::ToolPalette(BMessenger target)
	:
	BWindow(BRect(0, 0, 100, 100), str::kToolsTitle, B_FLOATING_WINDOW_LOOK,
		B_FLOATING_SUBSET_WINDOW_FEEL, B_NOT_ZOOMABLE | B_NOT_RESIZABLE
			| B_NOT_CLOSABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_AVOID_FOCUS),
	fTarget(target)
{
	fView = new PaletteView(BMessenger(this));
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(2)
		.Add(fView);
	// The palette view watches the pointer for the names of the parts.
	SetPulseRate(50000);
}


void
ToolPalette::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPaletteSelect:
			fView->SetSelected(message->GetInt32("item", -1));
			break;
		case kMsgPaletteTheme:
			fView->SetTheme(message->GetString("theme", NULL));
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
ToolPalette::QuitRequested()
{
	// Owned by the stack window.
	return true;
}


// #pragma mark - InspectorWindow


// Puts a laid out view in a scroll view. The view keeps at least its
// minimum height and the scroll bar covers the rest: a scroll view would
// squeeze a laid out target into the visible area.
class ScrollHolder : public BView {
public:
	ScrollHolder(BView* content)
		:
		BView("holder", B_FRAME_EVENTS),
		fContent(content)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		AddChild(content);
	}

	virtual BSize MinSize()
	{
		return BSize(fContent->MinSize().width, 60);
	}

	virtual BSize PreferredSize()
	{
		return fContent->PreferredSize();
	}

	virtual BSize MaxSize()
	{
		return BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);
	}

	virtual void AttachedToWindow()
	{
		BView::AttachedToWindow();
		_Update();
	}

	virtual void FrameResized(float width, float height)
	{
		BView::FrameResized(width, height);
		_Update();
	}

private:
	void _Update()
	{
		BRect bounds = Bounds();
		float height = std::max(bounds.Height(), fContent->MinSize().height);
		fContent->MoveTo(0, 0);
		fContent->ResizeTo(bounds.Width(), height);
		fContent->Layout(true);
		BScrollBar* bar = ScrollBar(B_VERTICAL);
		if (bar == NULL)
			return;
		float range = height - bounds.Height();
		bar->SetRange(0, range);
		bar->SetProportion(bounds.Height() / (height + 1));
		bar->SetSteps(16, std::max(16.0f, bounds.Height() - 16));
	}

	BView*	fContent;
};


InspectorWindow::InspectorWindow(BMessenger target)
	:
	BWindow(BRect(0, 0, 280, 520), str::kInspectorTitle,
		B_FLOATING_WINDOW_LOOK, B_FLOATING_SUBSET_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_CLOSABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fTarget(target),
	fContainer(NULL),
	fScroll(NULL),
	fRow(0),
	fLocked(false)
{
	SetLayout(new BGroupLayout(B_VERTICAL));
	// The mode shortcuts work here too (passed to the stack window).
	AddShortcut('B', B_COMMAND_KEY, new BMessage(kMsgModeBrowse));
	AddShortcut('L', B_COMMAND_KEY, new BMessage(kMsgModeEdit));
	AddShortcut('F', B_COMMAND_KEY, new BMessage(kMsgModeFind));
	BMessage empty;
	empty.AddString("mode", "template");
	_Rebuild(empty);
}


void
InspectorWindow::_Send(const char* field, const char* value)
{
	BMessage message(kMsgInspectorChanged);
	message.AddString("field", field);
	message.AddString("value", value);
	fTarget.SendMessage(&message);
}


void
InspectorWindow::_AddRow(BView* label, BView* control)
{
	BGridLayout* grid = static_cast<BGridLayout*>(fContainer->GetLayout());
	if (label != NULL)
		grid->AddView(label, 0, fRow);
	grid->AddView(control, 1, fRow);
	fRow++;
}


void
InspectorWindow::_AddRow(const char* label, BView* control)
{
	BStringView* text = new BStringView(NULL, label);
	text->SetAlignment(B_ALIGN_RIGHT);
	_AddRow(text, control);
}


void
InspectorWindow::_AddWide(BView* view)
{
	BGridLayout* grid = static_cast<BGridLayout*>(fContainer->GetLayout());
	grid->AddView(view, 0, fRow, 2);
	fRow++;
}


void
InspectorWindow::_AddSection(const char* title)
{
	BStringView* text = new BStringView(NULL, title);
	BFont font(be_bold_font);
	text->SetFont(&font);
	BGridLayout* grid = static_cast<BGridLayout*>(fContainer->GetLayout());
	grid->AddView(new BSeparatorView(B_HORIZONTAL), 0, fRow++, 2);
	grid->AddView(text, 0, fRow++, 2);
}


BTextControl*
InspectorWindow::_Text(const char* field)
{
	BMessage* message = new BMessage(kMsgInspectorText);
	message->AddString("field", field);
	BTextControl* control = new BTextControl(field, NULL, "", message);
	fControls.push_back(control);
	return control;
}


BMenuField*
InspectorWindow::_Menu(const BMessage& state, const char* field)
{
	BPopUpMenu* menu = new BPopUpMenu("menu");
	BString key("menu:");
	key << field;
	BString choice;
	for (int32 i = 0; state.FindString(key.String(), i, &choice) == B_OK;
			i++) {
		int32 tab = choice.FindFirst('\t');
		BString value, label;
		choice.CopyInto(value, 0, tab);
		choice.CopyInto(label, tab + 1, choice.Length() - tab - 1);
		if (value == "-") {
			menu->AddSeparatorItem();
			continue;
		}
		BMessage* message = new BMessage(kMsgInspectorMenu);
		message->AddString("field", field);
		message->AddString("value", value);
		menu->AddItem(new BMenuItem(label.String(), message));
	}
	menu->SetTargetForItems(this);
	BMenuField* control = new BMenuField(field, NULL, menu);
	fControls.push_back(control);
	return control;
}


BCheckBox*
InspectorWindow::_Check(const char* field, const char* label)
{
	BMessage* message = new BMessage(kMsgInspectorCheck);
	message->AddString("field", field);
	BCheckBox* control = new BCheckBox(field, label, message);
	fControls.push_back(control);
	return control;
}


// A swatch that opens the color window; draws a hatch for "unset".
class ColorWell : public BView {
public:
	ColorWell(const char* field, BMessenger target)
		:
		BView(field, B_WILL_DRAW),
		fTarget(target),
		fField(field),
		fColor(make_color(0, 0, 0)),
		fOwn(false),
		fEnabled(true)
	{
		SetExplicitMinSize(BSize(48, 18));
		SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 20));
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	void SetColor(const char* text, bool own)
	{
		bool ok;
		rgb_color color = ParseRgb(text, &ok);
		fValid = ok;
		fColor = color;
		fText = text;
		fOwn = own;
		Invalidate();
	}

	void SetEnabled(bool enabled)
	{
		fEnabled = enabled;
		Invalidate();
	}

	virtual void Draw(BRect)
	{
		BRect bounds = Bounds();
		SetHighUIColor(B_PANEL_BACKGROUND_COLOR);
		FillRect(bounds);
		BRect swatch(bounds.left, bounds.top + 1, bounds.left + 28,
			bounds.bottom - 1);
		if (fValid) {
			SetHighColor(fColor);
			FillRect(swatch);
		} else {
			SetHighColor(255, 255, 255);
			FillRect(swatch);
			SetHighColor(200, 0, 0);
			StrokeLine(swatch.LeftBottom(), swatch.RightTop());
		}
		SetHighUIColor(B_CONTROL_BORDER_COLOR);
		StrokeRect(swatch);
		// Theme values are shown dimmed (UI 6.12).
		rgb_color text = ui_color(B_PANEL_TEXT_COLOR);
		if (!fOwn || !fEnabled)
			text = tint_color(text, B_LIGHTEN_1_TINT);
		SetHighColor(text);
		font_height height;
		GetFontHeight(&height);
		DrawString(fText.String(), BPoint(swatch.right + 6,
			(bounds.top + bounds.bottom + height.ascent - height.descent)
				/ 2));
	}

	virtual void MouseDown(BPoint where)
	{
		if (!fEnabled)
			return;
		BMessage message(kMsgInspectorColor);
		message.AddString("field", fField);
		message.AddString("color", fValid ? fText.String() : "#000000");
		BMessenger(Window()).SendMessage(&message);
	}

private:
	BMessenger	fTarget;
	BString		fField;
	rgb_color	fColor;
	BString		fText;
	bool		fOwn;
	bool		fValid = false;
	bool		fEnabled;
};


BView*
InspectorWindow::_Color(const char* field)
{
	ColorWell* well = new ColorWell(field, fTarget);
	fControls.push_back(well);
	return well;
}


BView*
InspectorWindow::_Button(const char* field, const char* label)
{
	BMessage* message = new BMessage(kMsgInspectorButton);
	message->AddString("field", field);
	BButton* button = new BButton(field, label, message);
	fControls.push_back(button);
	return button;
}


void
InspectorWindow::_Rebuild(const BMessage& state)
{
	if (fScroll != NULL) {
		fScroll->RemoveSelf();
		delete fScroll;
	}
	fControls.clear();
	fRow = 0;
	BGridLayout* grid = new BGridLayout(B_USE_SMALL_SPACING,
		B_USE_SMALL_SPACING);
	fContainer = new BView("inspector", 0, grid);
	fContainer->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	grid->SetInsets(B_USE_SMALL_INSETS);
	grid->SetColumnWeight(1, 1);

	BString mode = state.GetString("mode", "template");
	BString kind = state.GetString("kind", "");
	bool valueKind = kind == "text" || kind == "number" || kind == "bool"
		|| kind == "image";

	if (mode == "template") {
		_AddSection(str::kInspectorTemplate);
		_AddRow(str::kTemplateName, _Text("tmpl_title"));
		_AddRow(str::kPaperSize, _Menu(state, "paper"));
		_AddRow(str::kPaperOrientation, _Menu(state, "orientation"));
		_AddRow(str::kPaperWidth, _Text("tmpl_w"));
		_AddRow(str::kPaperHeight, _Text("tmpl_h"));
		_AddRow(str::kTemplateTitlePart, _Menu(state, "title_part"));
		_AddSection(str::kTemplateBackground);
		_AddRow(str::kStyleColor, _Color("bg_color"));
		_AddRow((BView*)NULL, _Button("bg_color_reset", str::kTemplateSystemColor));
		_AddRow(str::kTemplateImage, _Button("bg_image_choose",
			str::kTemplateChooseFile));
		_AddRow((BView*)NULL, _Button("bg_image_clear", str::kClear));
		_AddRow(str::kFieldFit, _Menu(state, "bg_fit"));
		_AddSection(str::kTemplateTheme);
		_AddRow(str::kTemplateTheme, _Menu(state, "theme"));
		_AddWide(_Button("theme_all", str::kTemplateApplyAll));
	} else {
		if (mode == "multi") {
			BString text;
			text.SetToFormat(str::kInspectorMulti.Get(),
				(int)state.GetInt32("count", 0));
			_AddWide(new BStringView(NULL, text));
		} else {
			if (valueKind || kind == "blank") {
				_AddRow(str::kFieldName, _Text("name"));
				BStringView* warning = new BStringView("warning", "");
				warning->SetHighUIColor(B_FAILURE_COLOR);
				_AddWide(warning);
			}
			_AddRow(str::kFieldKind, _Menu(state, "kind"));
		}
		_AddRow(str::kFieldX, _Text("x"));
		_AddRow(str::kFieldY, _Text("y"));
		_AddRow(str::kFieldW, _Text("w"));
		_AddRow(str::kFieldH, _Text("h"));

		if (mode == "part") {
			if (kind == "text" || kind == "number")
				_AddRow(str::kFieldDefault, _Text("default"));
			if (kind == "text")
				_AddRow((BView*)NULL, _Check("multiline", str::kFieldMultiline));
			if (kind == "number")
				_AddRow(str::kFieldDecimals, _Text("decimals"));
			if (kind == "bool") {
				_AddRow(str::kFieldDefault, _Menu(state, "bool_default"));
				_AddRow(str::kFieldBoolLook, _Menu(state, "bool_look"));
			}
			if (kind == "image")
				_AddRow(str::kFieldFit, _Menu(state, "fit"));
			if (kind == "label" || kind == "button")
				_AddRow(str::kFieldLabelText, _Text("text"));
			if (kind == "button") {
				_AddRow(str::kFieldAction, _Menu(state, "action"));
				// The sound or video of sound.play / video.play.
				_AddSection(str::kSectionMedia);
				_AddRow(str::kMediaFile, _Text("media_name"));
				_AddRow((BView*)NULL, _Button("media_choose", str::kChoose));
				_AddRow((BView*)NULL, _Button("media_clear", str::kClear));
			}
			if (kind == "bool") {
				// What the switch does while it is on.
				_AddSection(str::kSectionSwitch);
				_AddRow(str::kEffect, _Menu(state, "effect"));
				bool first = true;
				const char* target;
				for (int32 i = 0; state.FindString("targets", i, &target)
						== B_OK; i++) {
					BString entry(target);
					int32 tab = entry.FindFirst('\t');
					BString id, label;
					entry.CopyInto(id, 0, tab);
					entry.CopyInto(label, tab + 1, entry.Length() - tab - 1);
					BString field("target:");
					field << id;
					BCheckBox* check = _Check(field, label);
					if (first)
						_AddRow(str::kEffectTargets, check);
					else
						_AddRow((BView*)NULL, check);
					first = false;
				}
				if (first) {
					_AddRow(str::kEffectTargets,
						new BStringView(NULL, str::kEffectNoTargets));
				}
			}
			if (kind == "label" || kind == "button") {
				_AddSection(str::kSectionIcon);
				_AddRow(str::kSectionIcon, _Button("icon_choose",
					str::kChoose));
				_AddRow((BView*)NULL, _Button("icon_clear", str::kClear));
				_AddRow(str::kIconSize, _Text("icon_size"));
				_AddRow(str::kIconPlacement, _Menu(state, "icon_placement"));
			}
			if (kind == "label" || kind == "button" || kind == "image") {
				_AddSection(str::kSectionLink);
				_AddRow(str::kSectionLink, _Menu(state, "link_mode"));
				_AddRow(str::kLinkUrl, _Text("link_url"));
				_AddRow(str::kLinkField, _Menu(state, "link_field"));
			}
			if (kind == "image") {
				_AddSection(str::kSectionSource);
				_AddRow(str::kSourceField, _Menu(state, "source_field"));
				_AddRow(str::kSourceEdge, _Menu(state, "source_edge"));
			}
		}

		// Style (UI 6.12), for one or several parts.
		_AddSection(str::kSectionStyle);
		_AddRow(str::kStyleBorder, _Check("border_visible",
			str::kStyleBorderShow));
		_AddRow(str::kStyleWidth, _Text("border_width"));
		_AddRow(str::kStyleColor, _Color("border_color"));
		_AddRow(str::kStyleFill, _Menu(state, "fill_mode"));
		_AddRow((BView*)NULL, _Color("fill_color"));
		_AddRow(str::kStyleFont, _Menu(state, "font_family"));
		_AddRow(str::kStyleSize, _Text("font_size"));
		_AddRow(str::kStyleTextColor, _Color("text_color"));
		_AddWide(_Button("style_reset", str::kStyleReset));
		_AddSection(str::kLock);
		BCheckBox* locked = _Check("locked", str::kLock);
		_AddRow((BView*)NULL, locked);
	}
	grid->AddItem(BSpaceLayoutItem::CreateGlue(), 0, fRow, 2);

	fScroll = new BScrollView("scroll", new ScrollHolder(fContainer), 0,
		false, true, B_NO_BORDER);
	GetLayout()->AddView(fScroll);

	for (BView* view : fControls) {
		if (BControl* control = dynamic_cast<BControl*>(view))
			control->SetTarget(this);
	}
}


static void
SetTextField(BTextControl* control, const char* text, bool own)
{
	// Do not overwrite what the user is typing.
	if (control->TextView()->IsFocus())
		return;
	if (strcmp(control->Text(), text) != 0)
		control->SetText(text);
	rgb_color color = ui_color(B_DOCUMENT_TEXT_COLOR);
	if (!own)
		color = tint_color(color, B_LIGHTEN_1_TINT);
	BFont font;
	control->TextView()->GetFontAndColor(0, &font);
	control->TextView()->SetFontAndColor(&font, B_FONT_ALL, &color);
}


void
InspectorWindow::_Update(const BMessage& state)
{
	fLocked = state.GetBool("locked", false);
	for (BView* view : fControls) {
		const char* field = view->Name();
		BString own("own:");
		own << field;
		bool isOwn = state.GetBool(own.String(), true);
		const char* value = state.GetString(field, "");
		bool enabled = !fLocked || strcmp(field, "locked") == 0;
		BString disabledKey("disabled:");
		disabledKey << field;
		if (state.GetBool(disabledKey.String(), false))
			enabled = false;
		if (BTextControl* text = dynamic_cast<BTextControl*>(view)) {
			SetTextField(text, value, isOwn);
			text->SetEnabled(enabled);
		} else if (BMenuField* menu = dynamic_cast<BMenuField*>(view)) {
			BMenu* items = menu->Menu();
			for (int32 i = 0; BMenuItem* item = items->ItemAt(i); i++) {
				BMessage* message = item->Message();
				item->SetMarked(message != NULL
					&& strcmp(message->GetString("value", ""), value) == 0);
			}
			menu->SetEnabled(enabled);
		} else if (BCheckBox* check = dynamic_cast<BCheckBox*>(view)) {
			check->SetValue(strcmp(value, "1") == 0 ? B_CONTROL_ON
				: B_CONTROL_OFF);
			check->SetEnabled(enabled);
		} else if (ColorWell* well = dynamic_cast<ColorWell*>(view)) {
			well->SetColor(value, isOwn);
			well->SetEnabled(enabled);
		} else if (BButton* button = dynamic_cast<BButton*>(view)) {
			button->SetEnabled(enabled);
		}
	}
	if (BStringView* warning = dynamic_cast<BStringView*>(
			FindView("warning"))) {
		warning->SetText(state.GetBool("duplicate", false)
			? str::kNameDuplicate.Get() : "");
	}
}


void
InspectorWindow::FocusField(const char* field)
{
	BTextControl* control = dynamic_cast<BTextControl*>(FindView(field));
	if (control == NULL)
		return;
	Activate();
	control->MakeFocus(true);
	control->TextView()->SelectAll();
}


void
InspectorWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgModeBrowse:
		case kMsgModeEdit:
		case kMsgModeFind:
			fTarget.SendMessage(message);
			break;
		case kMsgInspectorUpdate:
		{
			// Rebuild only when the set of controls changes, so that
			// typing and tabbing between fields is not interrupted.
			BString signature;
			signature << message->GetString("mode", "") << ":"
				<< message->GetString("kind", "") << ":"
				<< message->GetString("signature", "");
			if (signature != fSignature) {
				fSignature = signature;
				_Rebuild(*message);
			}
			_Update(*message);
			break;
		}
		case kMsgInspectorText:
		{
			BString field = message->GetString("field", "");
			BTextControl* control = dynamic_cast<BTextControl*>(
				FindView(field.String()));
			if (control != NULL)
				_Send(field, control->Text());
			break;
		}
		case kMsgInspectorMenu:
			_Send(message->GetString("field", ""),
				message->GetString("value", ""));
			break;
		case kMsgInspectorCheck:
		{
			BString field = message->GetString("field", "");
			BCheckBox* check = dynamic_cast<BCheckBox*>(
				FindView(field.String()));
			if (check != NULL)
				_Send(field, check->Value() == B_CONTROL_ON ? "1" : "0");
			break;
		}
		case kMsgInspectorButton:
			_Send(message->GetString("field", ""), "");
			break;
		case kMsgInspectorColor:
		{
			ColorWindow* window = new ColorWindow(fTarget,
				message->GetString("field", ""),
				message->GetString("color", "#000000"));
			window->Show();
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
InspectorWindow::QuitRequested()
{
	return true;
}


// #pragma mark - ColorWindow


class SwatchView : public BView {
public:
	SwatchView()
		:
		BView("swatch", B_WILL_DRAW)
	{
		SetExplicitMinSize(BSize(48, 24));
	}

	void SetColor(rgb_color color)
	{
		SetViewColor(color);
		Invalidate();
	}
};


ColorWindow::ColorWindow(BMessenger target, const char* field,
	const char* color)
	:
	BWindow(BRect(0, 0, 300, 200), str::kColorTitle, B_FLOATING_WINDOW_LOOK,
		B_FLOATING_APP_WINDOW_FEEL, B_NOT_ZOOMABLE | B_NOT_RESIZABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fTarget(target),
	fField(field)
{
	fControl = new BColorControl(B_ORIGIN, B_CELLS_32x8, 7.0f, "color",
		new BMessage(kMsgColorChanged));
	fText = new BTextControl("hex", NULL, color, new BMessage(kMsgColorText));
	fSwatch = new SwatchView();
	BGroupLayout* recent = new BGroupLayout(B_HORIZONTAL, 2);
	BView* recentView = new BView("recent", 0, recent);
	BMessage colors;
	gSettings.GetRecentColors(colors);
	BString text;
	for (int32 i = 0; colors.FindString("color", i, &text) == B_OK; i++) {
		BMessage* message = new BMessage(kMsgColorRecent);
		message->AddString("color", text);
		BButton* button = new BButton(NULL, "", message);
		button->SetExplicitSize(BSize(20, 20));
		bool ok;
		rgb_color swatch = ParseRgb(text, &ok);
		if (ok)
			button->SetViewColor(swatch);
		button->SetToolTip(text);
		recent->AddView(button);
	}
	recent->AddItem(BSpaceLayoutItem::CreateGlue());
	BButton* ok = new BButton("ok", str::kOK, new BMessage(kMsgColorOK));
	BButton* cancel = new BButton("cancel", str::kCancel,
		new BMessage(B_QUIT_REQUESTED));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fControl)
		.AddGroup(B_HORIZONTAL)
			.Add(fSwatch)
			.Add(fText)
		.End()
		.Add(new BStringView(NULL, str::kRecentColors))
		.Add(recentView)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(ok)
		.End();
	SetDefaultButton(ok);
	bool valid;
	rgb_color initial = ParseRgb(color, &valid);
	if (valid)
		_SetColor(initial);
	CenterOnScreen();
}


void
ColorWindow::_SetColor(rgb_color color)
{
	fControl->SetValue(color);
	static_cast<SwatchView*>(fSwatch)->SetColor(color);
	fText->SetText(RgbText(color));
}


void
ColorWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgColorChanged:
		{
			rgb_color color = fControl->ValueAsColor();
			static_cast<SwatchView*>(fSwatch)->SetColor(color);
			fText->SetText(RgbText(color));
			break;
		}
		case kMsgColorText:
		{
			bool ok;
			rgb_color color = ParseRgb(fText->Text(), &ok);
			if (ok)
				_SetColor(color);
			break;
		}
		case kMsgColorRecent:
		{
			bool ok;
			rgb_color color = ParseRgb(message->GetString("color", ""), &ok);
			if (ok)
				_SetColor(color);
			break;
		}
		case kMsgColorOK:
		{
			bool ok;
			rgb_color color = ParseRgb(fText->Text(), &ok);
			if (!ok)
				color = fControl->ValueAsColor();
			BString text = RgbText(color);
			gSettings.AddRecentColor(text);
			BMessage changed(kMsgInspectorChanged);
			changed.AddString("field", fField);
			changed.AddString("value", text);
			fTarget.SendMessage(&changed);
			Quit();
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


// #pragma mark - IconChooserWindow


class IconGridView : public BView {
public:
	IconGridView(BMessenger target)
		:
		BView("icons", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE),
		fTarget(target),
		fSelected(-1)
	{
		SetViewUIColor(B_LIST_BACKGROUND_COLOR);
		// Not the current size (the default for a view without a layout).
		SetExplicitMinSize(BSize(160, 120));
	}

	virtual ~IconGridView()
	{
		_ClearCache();
	}

	void SetIcons(const std::vector<BString>& paths)
	{
		fPaths = paths;
		fSelected = paths.empty() ? -1 : 0;
		_UpdateScrollBar();
		ScrollTo(0, 0);
		Invalidate();
		_Notify(kMsgIconSelect);
	}

	BString Selected() const
	{
		return fSelected >= 0 && fSelected < int32(fPaths.size())
			? fPaths[fSelected] : BString();
	}

	virtual void Draw(BRect updateRect)
	{
		float cell = _Cell();
		int32 columns = _Columns();
		font_height height;
		GetFontHeight(&height);
		for (int32 i = 0; i < int32(fPaths.size()); i++) {
			BRect rect = _CellRect(i, columns, cell);
			if (!rect.Intersects(updateRect))
				continue;
			if (i == fSelected) {
				SetHighUIColor(B_LIST_SELECTED_BACKGROUND_COLOR);
				FillRect(rect);
			}
			const BBitmap* icon = _Icon(fPaths[i]);
			if (icon != NULL) {
				SetDrawingMode(B_OP_ALPHA);
				DrawBitmap(icon, BPoint(rect.left + (cell - 32) / 2,
					rect.top + 4));
				SetDrawingMode(B_OP_COPY);
			}
			BString name = BPath(fPaths[i]).Leaf();
			name.RemoveLast(".hvif");
			TruncateString(&name, B_TRUNCATE_END, cell - 4);
			SetHighUIColor(i == fSelected ? B_LIST_SELECTED_ITEM_TEXT_COLOR
				: B_LIST_ITEM_TEXT_COLOR);
			DrawString(name, BPoint(rect.left + (cell - StringWidth(name)) / 2,
				rect.top + 40 + height.ascent));
		}
	}

	virtual void MouseDown(BPoint where)
	{
		MakeFocus(true);
		int32 index = _IndexAt(where);
		if (index < 0)
			return;
		fSelected = index;
		Invalidate();
		int32 clicks = 1;
		Window()->CurrentMessage()->FindInt32("clicks", &clicks);
		_Notify(clicks == 2 ? kMsgIconInvoke : kMsgIconSelect);
	}

	virtual void FrameResized(float width, float height)
	{
		BView::FrameResized(width, height);
		_UpdateScrollBar();
	}

private:
	float _Cell() const { return 84; }

	int32 _Columns() const
	{
		return std::max(int32(1), int32((Bounds().Width() + 1) / _Cell()));
	}

	BRect _CellRect(int32 index, int32 columns, float cell) const
	{
		float row = 40 + 18;
		float left = (index % columns) * cell;
		float top = (index / columns) * row;
		return BRect(left, top, left + cell - 1, top + row - 1);
	}

	int32 _IndexAt(BPoint where) const
	{
		int32 columns = _Columns();
		int32 column = int32(where.x / _Cell());
		int32 row = int32(where.y / 58);
		if (column >= columns)
			return -1;
		int32 index = row * columns + column;
		return index < int32(fPaths.size()) ? index : -1;
	}

	void _UpdateScrollBar()
	{
		BScrollBar* bar = ScrollBar(B_VERTICAL);
		if (bar == NULL)
			return;
		int32 rows = (int32(fPaths.size()) + _Columns() - 1) / _Columns();
		float height = rows * 58.0f;
		float range = std::max(0.0f, height - Bounds().Height());
		bar->SetRange(0, range);
		bar->SetProportion(height > 0 ? std::min(1.0f,
			Bounds().Height() / height) : 1.0f);
		bar->SetSteps(58, Bounds().Height());
	}

	const BBitmap* _Icon(const BString& path)
	{
		auto found = fCache.find(path.String());
		if (found != fCache.end())
			return found->second;
		BBitmap* bitmap = NULL;
		std::string data;
		if (bdb::ReadWholeFile(path.String(), 1024 * 1024, data)) {
			bitmap = new BBitmap(BRect(0, 0, 31, 31), B_RGBA32);
			if (BIconUtils::GetVectorIcon(
					reinterpret_cast<const uint8*>(data.data()), data.size(),
					bitmap) != B_OK) {
				delete bitmap;
				bitmap = NULL;
			}
		}
		fCache[path.String()] = bitmap;
		return bitmap;
	}

	void _ClearCache()
	{
		for (auto& entry : fCache)
			delete entry.second;
		fCache.clear();
	}

	void _Notify(uint32 what)
	{
		BMessage message(what);
		message.AddString("path", Selected());
		fTarget.SendMessage(&message);
	}

	BMessenger	fTarget;
	std::vector<BString> fPaths;
	int32		fSelected;
	std::map<std::string, BBitmap*> fCache;
};


class IconPreview : public BView {
public:
	IconPreview()
		:
		BView("preview", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
		fBitmap(NULL)
	{
		SetExplicitMinSize(BSize(140, 140));
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	}

	~IconPreview()
	{
		delete fBitmap;
	}

	void SetIcon(const char* path, int32 size)
	{
		delete fBitmap;
		fBitmap = NULL;
		std::string data;
		if (path != NULL && path[0] != '\0'
			&& bdb::ReadWholeFile(path, 1024 * 1024, data)) {
			fBitmap = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
			if (BIconUtils::GetVectorIcon(
					reinterpret_cast<const uint8*>(data.data()), data.size(),
					fBitmap) != B_OK) {
				delete fBitmap;
				fBitmap = NULL;
			}
		}
		Invalidate();
	}

	virtual void Draw(BRect)
	{
		if (fBitmap == NULL)
			return;
		BRect bounds = Bounds();
		BRect icon = fBitmap->Bounds();
		SetDrawingMode(B_OP_ALPHA);
		DrawBitmap(fBitmap, BPoint(
			floorf((bounds.Width() - icon.Width()) / 2),
			floorf((bounds.Height() - icon.Height()) / 2)));
	}

private:
	BBitmap*	fBitmap;
};


IconChooserWindow::IconChooserWindow(BMessenger target, const char* directory,
	int32 size, const char* placement)
	:
	BWindow(BRect(0, 0, 620, 440), str::kIconTitle, B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fTarget(target),
	fPlacementValue(placement)
{
	BDirectory dir(directory);
	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		BPath path;
		entry.GetPath(&path);
		if (BString(path.Leaf()).EndsWith(".hvif"))
			fAll.push_back(path.Path());
	}
	std::sort(fAll.begin(), fAll.end(), [](const BString& a,
			const BString& b) { return a.ICompare(b) < 0; });

	fSearch = new BTextControl("search", str::kIconSearch, "",
		new BMessage(kMsgIconSearch));
	fSearch->SetModificationMessage(new BMessage(kMsgIconSearch));
	fGrid = new IconGridView(BMessenger(this));
	BScrollView* scroll = new BScrollView("scroll", fGrid, 0, false, true);
	fSize = new BSlider("size", str::kIconSize, new BMessage(kMsgIconSize),
		12, 128, B_HORIZONTAL);
	fSize->SetModificationMessage(new BMessage(kMsgIconSize));
	fSize->SetValue(size);
	fSize->SetLimitLabels("12", "128");
	BPopUpMenu* menu = new BPopUpMenu("placement");
	const char* values[] = { "left", "top" };
	const char* labels[] = { str::kIconLeft, str::kIconTop };
	for (int i = 0; i < 2; i++) {
		BMessage* message = new BMessage(kMsgIconPlacement);
		message->AddString("value", values[i]);
		BMenuItem* item = new BMenuItem(labels[i], message);
		item->SetMarked(fPlacementValue == values[i]);
		menu->AddItem(item);
	}
	fPlacement = new BMenuField("placement", str::kIconPlacement, menu);
	fPreview = new IconPreview();
	BButton* file = new BButton("file", str::kIconFromFile,
		new BMessage(kMsgIconFile));
	BButton* ok = new BButton("ok", str::kChooseShort,
		new BMessage(kMsgIconOK));
	BButton* cancel = new BButton("cancel", str::kCancel,
		new BMessage(B_QUIT_REQUESTED));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fSearch)
		.AddGroup(B_HORIZONTAL)
			.Add(scroll, 3)
			.AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING, 1)
				.Add(fPreview)
				.Add(fSize)
				.Add(fPlacement)
				.AddGlue()
			.End()
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(file)
			.AddGlue()
			.Add(cancel)
			.Add(ok)
		.End();
	SetDefaultButton(ok);
	scroll->SetExplicitMinSize(BSize(400, 300));
	CenterOnScreen();
	_Filter();
	if (fAll.empty())
		fSearch->SetText(str::kIconNone);
}


void
IconChooserWindow::_Filter()
{
	BString needle = fSearch->Text();
	std::vector<BString> shown;
	for (const BString& path : fAll) {
		if (needle.IsEmpty() || BString(BPath(path).Leaf()).IFindFirst(needle)
				>= 0) {
			shown.push_back(path);
		}
	}
	fGrid->SetIcons(shown);
}


void
IconChooserWindow::_Choose(const char* path)
{
	if (path == NULL || path[0] == '\0')
		return;
	BMessage message(kMsgIconChosen);
	message.AddString("path", path);
	message.AddInt32("size", fSize->Value());
	message.AddString("placement", fPlacementValue);
	fTarget.SendMessage(&message);
	Quit();
}


void
IconChooserWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgIconSearch:
			_Filter();
			break;
		case kMsgIconSelect:
		case kMsgIconSize:
			static_cast<IconPreview*>(fPreview)->SetIcon(fGrid->Selected(),
				fSize->Value());
			break;
		case kMsgIconInvoke:
		case kMsgIconOK:
			_Choose(fGrid->Selected());
			break;
		case kMsgIconPlacement:
			fPlacementValue = message->GetString("value", "left");
			break;
		case kMsgIconFile:
		{
			// The user's own HVIF or image file (theme spec 8.4).
			BMessenger target(this);
			BFilePanel* panel = new BFilePanel(B_OPEN_PANEL, &target, NULL,
				B_FILE_NODE, false, new BMessage(kMsgIconFileRef), NULL,
				true, true);
			panel->Show();
			break;
		}
		case kMsgIconFileRef:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK)
				_Choose(BPath(&ref).Path());
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}
