/*
 * BDB card database - RenkuOS application
 */
#include "CardSurface.h"

#include <Beep.h>
#include <Clipboard.h>
#include <Cursor.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Region.h>
#include <ScrollBar.h>
#include <TextView.h>
#include <Window.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "EditWindows.h"
#include "FieldViews.h"
#include "Limits.h"
#include "Link.h"
#include "Stack.h"
#include "StackDocument.h"
#include "Strings.h"
#include "Text.h"


using bdb::Json;
using bdb::JsonArray;

static const int32 kGrid = 8;
static const float kHandleSize = 6.0f;
static const int32 kMinPartSize = 4;
static const uint32 kMsgLabelCommit = 'lbCm';
static const size_t kMaxCachedBitmaps = 64;

enum {
	kCursorDefault = 0,
	kCursorMove,
	kCursorNotAllowed,
	kCursorLink,
	kCursorResizeNWSE,
	kCursorResizeNESW,
	kCursorResizeNS,
	kCursorResizeEW,
	kCursorCrossHair
};


namespace {


// In-place editor for label text (UI 6.2).
class LabelEditor : public BTextView {
public:
	LabelEditor(BRect frame, BHandler* target)
		:
		BTextView(frame, "label editor", BRect(0, 0, frame.Width(),
			frame.Height()).InsetByCopy(2, 1), B_FOLLOW_NONE,
			B_WILL_DRAW | B_NAVIGABLE),
		fTarget(target),
		fDone(false)
	{
		SetWordWrap(false);
	}

	virtual void KeyDown(const char* bytes, int32 numBytes)
	{
		if (numBytes == 1 && (bytes[0] == B_ENTER || bytes[0] == B_ESCAPE)) {
			_Commit();
			return;
		}
		BTextView::KeyDown(bytes, numBytes);
	}

	virtual void MakeFocus(bool focus)
	{
		BTextView::MakeFocus(focus);
		if (!focus)
			_Commit();
	}

private:
	void _Commit()
	{
		if (fDone)
			return;
		fDone = true;
		BMessenger(fTarget).SendMessage(kMsgLabelCommit);
	}

	BHandler*	fTarget;
	bool		fDone;
};


struct PartOrder {
	bool operator()(const std::pair<bdb::Rect, BView*>& a,
		const std::pair<bdb::Rect, BView*>& b) const
	{
		if (a.first.y != b.first.y)
			return a.first.y < b.first.y;
		return a.first.x < b.first.x;
	}
};


}	// namespace


static bool
Contains(const bdb::Rect& rect, BPoint point)
{
	return point.x >= rect.x && point.x < rect.x + rect.w
		&& point.y >= rect.y && point.y < rect.y + rect.h;
}


static const char*
KindDisplayName(const std::string& kind)
{
	if (kind == bdb::kKindText)
		return str::kKindText;
	if (kind == bdb::kKindNumber)
		return str::kKindNumber;
	if (kind == bdb::kKindBool)
		return str::kKindBool;
	if (kind == bdb::kKindImage)
		return str::kKindImage;
	if (kind == bdb::kKindLabel)
		return str::kKindLabel;
	if (kind == bdb::kKindButton)
		return str::kKindButton;
	if (kind == bdb::kKindCounter)
		return str::kKindCounter;
	return str::kKindBlank;
}


CardSurface::CardSurface(StackDocument* document)
	:
	BView("card", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE_JUMP
		| B_FULL_UPDATE_ON_RESIZE),
	fDocument(document),
	fMode(kModeBrowse),
	fScale(1.0f),
	fFit(false),
	fTheme(bdb::Theme::Default()),
	fPosition(0),
	fCount(0),
	fFindActive(false),
	fPressedInside(false),
	fCreateItem(-1),
	fSnap(true),
	fShowGrid(true),
	fDragMode(kDragNone),
	fDragHandle(-1),
	fDragUndoPushed(false),
	fDropItem(-1),
	fLabelEditor(NULL),
	fCursorKind(-1)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	// A view without a layout reports its current size as its minimum,
	// which would keep the window from ever getting smaller.
	SetExplicitMinSize(BSize(64, 64));
}


CardSurface::~CardSurface()
{
}


void
CardSurface::AttachedToWindow()
{
	BView::AttachedToWindow();
	{
		// Tells the window the theme (for the parts palette).
		DocLocker lock(fDocument);
		_LoadTheme();
	}
	_UpdateScrollBars();
}


// #pragma mark - state


const bdb::Template*
CardSurface::_Template() const
{
	bdb::Stack& stack = fDocument->GetStack();
	if (fMode == kModeEdit)
		return stack.FindTemplate(fTemplateId);
	const bdb::Card* card = _Card();
	if (card == NULL)
		return stack.FindTemplate(stack.DefaultTemplateId());
	return stack.FindTemplate(card->TemplateId());
}


bdb::Template*
CardSurface::EditTemplate() const
{
	return fDocument->GetStack().FindTemplate(fTemplateId);
}


const bdb::Card*
CardSurface::_Card() const
{
	if (fCardId.empty())
		return NULL;
	return fDocument->GetStack().FindCard(fCardId);
}


std::string
CardSurface::TemplateId() const
{
	if (fMode == kModeEdit)
		return fTemplateId;
	DocLocker lock(fDocument);
	const bdb::Card* card = _Card();
	return card != NULL ? card->TemplateId() : std::string();
}


void
CardSurface::_LoadTheme()
{
	const bdb::Template* tmpl = _Template();
	fTheme = WithSystemColors(fDocument->GetStack().ThemeFor(tmpl));
	fThemeTemplate = tmpl != NULL ? tmpl->Id() : std::string();
	// The parts palette shows the parts in the theme of the card.
	std::string key = fTheme.Doc().DumpCompact();
	if (key != fThemeKey && Window() != NULL) {
		fThemeKey = key;
		BMessage message(kMsgPaletteTheme);
		message.AddString("theme", key.c_str());
		Window()->PostMessage(&message);
	}
}


void
CardSurface::ThemeChanged()
{
	DocLocker lock(fDocument);
	_LoadTheme();
	Rebuild();
}


void
CardSurface::SetMode(Mode mode)
{
	CommitLabelEditor();
	fMode = mode;
	fDragMode = kDragNone;
	fPressedPart.clear();
	if (mode != kModeEdit)
		fSelection.clear();
	if (mode != kModeFind)
		fFindBool.clear();
	fCursorKind = -1;
	Rebuild();
}


void
CardSurface::SetCard(const std::string& cardId)
{
	fCardId = cardId;
	if (fMode != kModeEdit)
		Rebuild();
}


void
CardSurface::SetNavigation(int32 position, int32 count, bool findActive)
{
	if (position == fPosition && count == fCount && findActive == fFindActive)
		return;
	fPosition = position;
	fCount = count;
	fFindActive = findActive;
	Invalidate();
}


void
CardSurface::SetEditTemplate(const std::string& templateId)
{
	if (templateId != fTemplateId) {
		ClearUndo();
		fSelection.clear();
	}
	fTemplateId = templateId;
	if (fMode == kModeEdit) {
		DocLocker lock(fDocument);
		_LoadTheme();
		_UpdateScrollBars();
		Invalidate();
	}
}


void
CardSurface::SetShowGrid(bool show)
{
	fShowGrid = show;
	Invalidate();
}


void
CardSurface::SetScale(float scale)
{
	if (scale < 0.25f)
		scale = 0.25f;
	if (scale > 4.0f)
		scale = 4.0f;
	if (scale == fScale)
		return;
	CommitLabelEditor();
	fScale = scale;
	Rebuild();
}


void
CardSurface::SetFitToWindow(bool fit)
{
	fFit = fit;
	if (fit)
		SetScale(FitScale(Bounds().Width(), Bounds().Height()));
}


BSize
CardSurface::ScaledCardSize() const
{
	DocLocker lock(fDocument);
	const bdb::Template* tmpl = _Template();
	int32 width = tmpl != NULL ? tmpl->Width() : bdb::kDefaultCardWidth;
	int32 height = tmpl != NULL ? tmpl->Height() : bdb::kDefaultCardHeight;
	return BSize(width * fScale, height * fScale);
}


float
CardSurface::FitScale(float width, float height) const
{
	DocLocker lock(fDocument);
	const bdb::Template* tmpl = _Template();
	int32 cardWidth = tmpl != NULL ? tmpl->Width() : bdb::kDefaultCardWidth;
	int32 cardHeight = tmpl != NULL ? tmpl->Height()
		: bdb::kDefaultCardHeight;
	float scale = std::min((width + 1) / cardWidth, (height + 1) / cardHeight);
	return std::max(0.25f, std::min(4.0f, scale));
}


BRect
CardSurface::_ToView(const bdb::Rect& rect) const
{
	return BRect(rect.x * fScale, rect.y * fScale,
		(rect.x + rect.w) * fScale - 1, (rect.y + rect.h) * fScale - 1);
}


BPoint
CardSurface::_ToCard(BPoint where) const
{
	return BPoint(where.x / fScale, where.y / fScale);
}


int32
CardSurface::_SnapValue(float value) const
{
	if (!fSnap)
		return int32(floorf(value + 0.5f));
	return int32(floorf(value / kGrid + 0.5f)) * kGrid;
}


void
CardSurface::_UpdateScrollBars()
{
	BSize size = ScaledCardSize();
	if (size != fLastCardSize && Window() != NULL) {
		// The window fits itself to the card.
		fLastCardSize = size;
		Window()->PostMessage(kMsgCardSizeChanged);
	}
	BRect bounds = Bounds();
	if (BScrollBar* bar = ScrollBar(B_HORIZONTAL)) {
		float range = std::max(0.0f, size.width - bounds.Width());
		bar->SetRange(0, range);
		bar->SetProportion(range > 0 ? bounds.Width() / size.width : 1.0f);
		bar->SetSteps(16, bounds.Width() * 0.8f);
	}
	if (BScrollBar* bar = ScrollBar(B_VERTICAL)) {
		float range = std::max(0.0f, size.height - bounds.Height());
		bar->SetRange(0, range);
		bar->SetProportion(range > 0 ? bounds.Height() / size.height : 1.0f);
		bar->SetSteps(16, bounds.Height() * 0.8f);
	}
}


void
CardSurface::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	if (fFit) {
		float scale = FitScale(width, height);
		if (fabsf(scale - fScale) > 0.001f) {
			fScale = scale;
			Rebuild();
			return;
		}
	}
	_UpdateScrollBars();
}


// #pragma mark - play / find


void
CardSurface::_RemoveInputViews()
{
	for (BView* view : fInputs) {
		RemoveChild(view);
		delete view;
	}
	fInputs.clear();
}


void
CardSurface::Rebuild()
{
	DocLocker lock(fDocument);
	_RemoveInputViews();
	fSwitches = bdb::SwitchState();
	_LoadTheme();
	// Keep the cache small while browsing many cards with images.
	fBitmaps.TrimTo(kMaxCachedBitmaps);
	_UpdateScrollBars();
	Invalidate();
	if (fMode == kModeEdit)
		return;

	const bdb::Card* card = _Card();
	const bdb::Template* tmpl = _Template();
	if (tmpl == NULL || (card == NULL && fMode == kModeBrowse))
		return;
	if (card != NULL && fDocument->GetStack().IsTemplateMissing(*card))
		return;
	if (fMode == kModeBrowse)
		fSwitches = bdb::ComputeSwitchState(*tmpl, card);

	rgb_color panel = PanelColor(tmpl, fTheme);
	for (const Json& part : tmpl->Parts()) {
		std::string kind = bdb::part::Kind(part);
		if (kind != bdb::kKindText && kind != bdb::kKindNumber)
			continue;
		std::string id = bdb::part::Id(part);
		if (IsPartHidden(id))
			continue;
		bdb::PartLook look = fTheme.Resolve(part);
		bdb::Rect r = bdb::part::GetRect(part);
		BRect cardRect(r.x, r.y, r.x + r.w - 1, r.y + r.h - 1);
		BRect inner = FrameInset(cardRect, look);
		BRect frame(inner.left * fScale, inner.top * fScale,
			(inner.right + 1) * fScale - 1, (inner.bottom + 1) * fScale - 1);
		if (!frame.IsValid() || frame.Width() < 2 || frame.Height() < 2)
			continue;
		bool multiline = kind == bdb::kKindText
			&& part.Find("multiline") != NULL
			&& part.Find("multiline")->AsBool();
		BFont font = FontForLook(look);
		font.SetSize(look.fontSize * fScale);
		FieldTextView* field = new FieldTextView(frame, id.c_str(),
			multiline, font, ToRgb(look.textColor),
			look.hasFill ? ToRgb(look.fill) : panel);
		if (kind == bdb::kKindNumber)
			field->SetAlignment(B_ALIGN_RIGHT);
		if (fMode == kModeBrowse && card != NULL) {
			const Json& value = bdb::EffectiveValue(*card, part);
			if (kind == bdb::kKindNumber) {
				BString raw = bdb::ValueText(value).c_str();
				field->SetNumberTexts(FormatNumberValue(value, part), raw);
			} else
				field->SetValueText(value.AsString().c_str());
			bool guarded = bdb::HasMismatchedValue(*card, part)
				&& fGuarded.count(id) == 0;
			field->SetGuarded(guarded);
			if (IsPartLocked(id))
				field->SetLocked(true);
		}
		AddChild(field);
		fInputs.push_back(field);
	}
}


bool
CardSurface::IsPartLocked(const std::string& partId) const
{
	return fMode == kModeBrowse && fSwitches.locked.count(partId) != 0;
}


bool
CardSurface::IsPartHidden(const std::string& partId) const
{
	return fMode == kModeBrowse && fSwitches.hidden.count(partId) != 0;
}


bool
CardSurface::IsGuarded(const std::string& partId) const
{
	DocLocker lock(fDocument);
	const bdb::Card* card = _Card();
	const bdb::Template* tmpl = _Template();
	if (card == NULL || tmpl == NULL || fGuarded.count(partId) != 0)
		return false;
	const Json* part = tmpl->FindPart(partId);
	return part != NULL && bdb::HasMismatchedValue(*card, *part);
}


void
CardSurface::SetGuarded(const std::string& partId, bool guarded)
{
	// `guarded` false: the user agreed to overwrite (UI 5.3, data 8.2).
	if (guarded)
		fGuarded.erase(partId);
	else
		fGuarded.insert(partId);
	if (FieldTextView* field = FindField(partId))
		field->SetGuarded(guarded);
}


FieldTextView*
CardSurface::FindField(const std::string& partId) const
{
	for (BView* view : fInputs) {
		FieldTextView* field = dynamic_cast<FieldTextView*>(view);
		if (field != NULL && field->PartId() == partId.c_str())
			return field;
	}
	return NULL;
}


// Text inputs in tab order: top to bottom, left to right (UI 5.3).
static std::vector<BView*>
TabOrder(const std::vector<BView*>& inputs, float scale)
{
	std::vector<std::pair<bdb::Rect, BView*>> order;
	for (BView* view : inputs) {
		FieldTextView* field = dynamic_cast<FieldTextView*>(view);
		if (field != NULL && field->IsLocked())
			continue;	// not reachable with Tab (switch lock)
		BRect frame = view->Frame();
		bdb::Rect rect;
		rect.x = int32(frame.left / scale);
		rect.y = int32(frame.top / scale);
		order.push_back({rect, view});
	}
	std::stable_sort(order.begin(), order.end(), PartOrder());
	std::vector<BView*> result;
	for (auto& entry : order)
		result.push_back(entry.second);
	return result;
}


void
CardSurface::FocusFirstField()
{
	std::vector<BView*> order = TabOrder(fInputs, fScale);
	if (!order.empty())
		order[0]->MakeFocus(true);
	else
		MakeFocus(true);
}


void
CardSurface::FocusNeighbor(BView* from, bool backward)
{
	std::vector<BView*> order = TabOrder(fInputs, fScale);
	if (order.empty())
		return;
	auto position = std::find(order.begin(), order.end(), from);
	int32 index = position == order.end() ? -1
		: int32(position - order.begin());
	int32 count = int32(order.size());
	index = backward ? (index <= 0 ? count - 1 : index - 1)
		: (index + 1) % count;
	order[index]->MakeFocus(true);
	if (BTextView* text = dynamic_cast<BTextView*>(order[index]))
		text->SelectAll();
}


std::vector<bdb::FindCondition>
CardSurface::FindConditions() const
{
	std::vector<bdb::FindCondition> conditions;
	for (BView* view : fInputs) {
		FieldTextView* field = dynamic_cast<FieldTextView*>(view);
		if (field == NULL || field->TextLength() == 0)
			continue;
		conditions.push_back({field->PartId().String(), field->Text()});
	}
	for (const auto& entry : fFindBool) {
		if (entry.second >= 0)
			conditions.push_back({entry.first, entry.second ? "on" : "off"});
	}
	return conditions;
}


void
CardSurface::ClearFindConditions()
{
	for (BView* view : fInputs) {
		if (FieldTextView* field = dynamic_cast<FieldTextView*>(view))
			field->SetValueText("");
	}
	fFindBool.clear();
	Invalidate();
}


// #pragma mark - drawing


void
CardSurface::_FillContext(PaintContext& context)
{
	bdb::Stack& stack = fDocument->GetStack();
	context.tmpl = _Template();
	context.card = fMode == kModeEdit ? NULL : _Card();
	context.stack = &stack;
	context.theme = &fTheme;
	context.bitmaps = &fBitmaps;
	context.mode = fMode == kModeEdit ? kPaintEdit
		: fMode == kModeFind ? kPaintFind : kPaintPlay;
	context.position = fPosition;
	context.count = fCount;
	context.pressedPart = fPressedInside ? fPressedPart : std::string();
	context.hoverPart = fHoverPart;
	context.findBool = fFindBool;
	BView* focus = Window() != NULL ? Window()->CurrentFocus() : NULL;
	if (FieldTextView* field = dynamic_cast<FieldTextView*>(focus))
		context.focusPart = field->PartId().String();
	context.buttonEnabled = [this](const Json& part) {
		return _ButtonEnabled(part);
	};
}


bool
CardSurface::_ButtonEnabled(const Json& part) const
{
	std::string action = part.GetString("action");
	bool play = fMode == kModeBrowse;
	if (fMode == kModeFind) {
		// Find mode: find.* and moving within the results (UI 6.10).
		if (action.compare(0, 5, "find.") == 0)
			return action != "find.clear" || fFindActive;
		if (action == "card.first" || action == "card.prev"
			|| action == "card.next" || action == "card.last") {
			return false;
		}
		return false;
	}
	if (!play)
		return false;
	if (action == "card.first" || action == "card.prev")
		return fPosition > 1;
	if (action == "card.next" || action == "card.last")
		return fPosition < fCount;
	if (action == "find.clear")
		return fFindActive;
	if (action == "find.run")
		return false;
	if (action == "card.duplicate" || action == "card.delete") {
		DocLocker lock(fDocument);
		return _Card() != NULL && !fDocument->GetStack().IsProvisional(fCardId);
	}
	if (action == "card.pdf" || action == "card.image") {
		DocLocker lock(fDocument);
		return _Card() != NULL;
	}
	if (action == "sound.play" || action == "video.play")
		return bdb::part::ButtonMedia(part) != NULL;
	if (action == "link.open") {
		DocLocker lock(fDocument);
		const bdb::Template* tmpl = _Template();
		std::string url;
		return tmpl != NULL && bdb::ResolveLink(part, *tmpl, _Card(), url)
			== bdb::LinkCheck::kOk;
	}
	return bdb::IsKnownAction(action);
}


void
CardSurface::Draw(BRect updateRect)
{
	DocLocker lock(fDocument);
	BSize size = ScaledCardSize();
	BRect card(0, 0, size.width - 1, size.height - 1);

	// Outside of the card: a different color in edit mode (UI 4.1).
	rgb_color outside = ui_color(B_PANEL_BACKGROUND_COLOR);
	if (fMode == kModeEdit)
		outside = mix_color(outside, make_color(70, 110, 170), 110);
	else if (fMode == kModeFind)
		outside = mix_color(outside, make_color(230, 190, 60), 90);
	else
		outside = tint_color(outside, B_DARKEN_2_TINT);
	SetHighColor(outside);
	BRegion region(updateRect);
	region.Exclude(card);
	FillRegion(&region);

	const bdb::Card* cardDoc = _Card();
	const bdb::Template* tmpl = _Template();
	if (fThemeTemplate != (tmpl != NULL ? tmpl->Id() : std::string()))
		_LoadTheme();
	if (fMode != kModeEdit && cardDoc != NULL
		&& fDocument->GetStack().IsTemplateMissing(*cardDoc)) {
		SetHighColor(ui_color(B_DOCUMENT_BACKGROUND_COLOR));
		FillRect(card & updateRect);
		_DrawMissingTemplate();
		return;
	}
	if (tmpl == NULL) {
		SetHighColor(ui_color(B_DOCUMENT_BACKGROUND_COLOR));
		FillRect(card & updateRect);
		SetHighColor(tint_color(ui_color(B_DOCUMENT_TEXT_COLOR),
			B_LIGHTEN_1_TINT));
		DrawString(str::kStatusNoCards, BPoint(16 * fScale, 24 * fScale));
		return;
	}

	PaintContext context;
	_FillContext(context);
	ViewCanvas canvas(this, fScale);
	PaintBackground(canvas, context);
	if (fMode == kModeEdit && fShowGrid) {
		// Light dots every grid step (UI 4.1).
		rgb_color dot = tint_color(PanelColor(tmpl, fTheme), B_DARKEN_2_TINT);
		SetHighColor(dot);
		float step = kGrid * fScale;
		if (step < 4)
			step *= 2;
		BRect area = updateRect & card;
		float startX = ceilf(area.left / step) * step;
		float startY = ceilf(area.top / step) * step;
		for (float y = startY; y <= area.bottom; y += step) {
			for (float x = startX; x <= area.right; x += step)
				FillRect(BRect(x, y, x, y));
		}
	}
	for (const Json& part : tmpl->Parts()) {
		if (IsPartHidden(bdb::part::Id(part)))
			continue;	// hidden by a switch
		BRect rect = _ToView(bdb::part::GetRect(part)).InsetByCopy(-4, -4);
		if (rect.Intersects(updateRect))
			PaintPart(canvas, context, part);
	}
	if (fMode == kModeEdit)
		_DrawEditOverlay(tmpl);
}


void
CardSurface::_DrawEditOverlay(const bdb::Template* tmpl)
{
	rgb_color navigation = ui_color(B_KEYBOARD_NAVIGATION_COLOR);
	// Every part's frame as a guide (UI 4.1).
	for (const Json& part : tmpl->Parts()) {
		BRect rect = _ToView(bdb::part::GetRect(part));
		SetHighColor(tint_color(PanelColor(tmpl, fTheme), B_DARKEN_3_TINT));
		SetLowColor(PanelColor(tmpl, fTheme));
		StrokeRect(rect, B_MIXED_COLORS);
		if (bdb::part::IsLocked(part))
			_DrawLock(rect);
	}
	for (const Json& part : tmpl->Parts()) {
		if (fSelection.count(bdb::part::Id(part)) == 0)
			continue;
		BRect rect = _ToView(bdb::part::GetRect(part));
		SetHighColor(navigation);
		StrokeRect(rect.InsetByCopy(-1, -1));
		StrokeRect(rect.InsetByCopy(-2, -2));
		// No handles on locked parts (UI 6.13).
		if (fSelection.size() == 1 && !bdb::part::IsLocked(part))
			_DrawHandles(rect);
	}
	if (fDragMode == kDragRubberBand || fDragMode == kDragCreate) {
		BRect band(std::min(fDragStart.x, fDragCurrent.x) * fScale,
			std::min(fDragStart.y, fDragCurrent.y) * fScale,
			std::max(fDragStart.x, fDragCurrent.x) * fScale,
			std::max(fDragStart.y, fDragCurrent.y) * fScale);
		SetHighColor(navigation);
		SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
		StrokeRect(band, fDragMode == kDragCreate ? B_SOLID_HIGH
			: B_MIXED_COLORS);
	}
	if (fDropItem >= 0 && fDropItem < kPaletteItemCount) {
		// Where the dragged palette item will go.
		const PaletteItem& item = kPaletteItems[fDropItem];
		SetHighColor(navigation);
		SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
		for (int32 i = 0; i < item.count; i++) {
			const PalettePiece& piece = item.pieces[i];
			bdb::Rect rect;
			rect.x = _SnapValue(fDropPoint.x) + piece.x;
			rect.y = _SnapValue(fDropPoint.y) + piece.y;
			rect.w = piece.width;
			rect.h = piece.height;
			StrokeRect(_ToView(rect), B_MIXED_COLORS);
		}
	}
}


void
CardSurface::_DrawHandles(BRect rect)
{
	SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
	float midX = (rect.left + rect.right) / 2;
	float midY = (rect.top + rect.bottom) / 2;
	BPoint points[8] = {
		rect.LeftTop(), BPoint(midX, rect.top), rect.RightTop(),
		BPoint(rect.right, midY), rect.RightBottom(),
		BPoint(midX, rect.bottom), rect.LeftBottom(), BPoint(rect.left, midY)
	};
	float half = kHandleSize / 2;
	for (const BPoint& point : points) {
		FillRect(BRect(point.x - half, point.y - half, point.x + half,
			point.y + half));
	}
}


void
CardSurface::_DrawLock(BRect rect)
{
	// A small padlock in the top right corner (UI 6.13).
	BRect body(rect.right - 11, rect.top + 6, rect.right - 3, rect.top + 12);
	SetHighColor(make_color(150, 110, 20));
	FillRect(body);
	SetPenSize(1.5f);
	StrokeArc(BRect(body.left + 1, rect.top + 2, body.right - 1,
		body.top + 3), 0, 180);
	StrokeLine(BPoint(body.left + 1, body.top - 1), BPoint(body.left + 1,
		body.top));
	StrokeLine(BPoint(body.right - 1, body.top - 1), BPoint(body.right - 1,
		body.top));
	SetPenSize(1);
}


void
CardSurface::_DrawMissingTemplate()
{
	const bdb::Card* card = _Card();
	font_height fontHeight;
	GetFontHeight(&fontHeight);
	float lineHeight = ceilf(fontHeight.ascent + fontHeight.descent
		+ fontHeight.leading) + 2;
	float y = 8 + fontHeight.ascent;
	SetHighColor(ui_color(B_FAILURE_COLOR));
	DrawString(str::kTemplateMissing, BPoint(8, y));
	y += lineHeight * 1.5f;
	SetHighColor(ui_color(B_DOCUMENT_TEXT_COLOR));
	for (const auto& entry : card->Values().Object()) {
		BString line;
		line << entry.first.c_str() << ": ";
		if (entry.second.IsString() || entry.second.IsNumber())
			line << bdb::FirstLine(bdb::ValueText(entry.second)).c_str();
		else
			line << entry.second.DumpCompact().c_str();
		DrawString(line.String(), BPoint(8, y));
		y += lineHeight;
	}
}


// #pragma mark - hit testing


int32
CardSurface::_HandleAt(BPoint where) const
{
	if (fSelection.size() != 1)
		return -1;
	const bdb::Template* tmpl = _Template();
	if (tmpl == NULL)
		return -1;
	const Json* part = tmpl->FindPart(*fSelection.begin());
	if (part == NULL || bdb::part::IsLocked(*part))
		return -1;
	BRect rect = _ToView(bdb::part::GetRect(*part));
	float midX = (rect.left + rect.right) / 2;
	float midY = (rect.top + rect.bottom) / 2;
	BPoint points[8] = {
		rect.LeftTop(), BPoint(midX, rect.top), rect.RightTop(),
		BPoint(rect.right, midY), rect.RightBottom(),
		BPoint(midX, rect.bottom), rect.LeftBottom(), BPoint(rect.left, midY)
	};
	for (int32 i = 0; i < 8; i++) {
		if (fabsf(where.x - points[i].x) <= kHandleSize
			&& fabsf(where.y - points[i].y) <= kHandleSize) {
			return i;
		}
	}
	return -1;
}


std::string
CardSurface::_PartAt(BPoint cardPoint) const
{
	const bdb::Template* tmpl = _Template();
	if (tmpl == NULL)
		return "";
	const JsonArray& parts = tmpl->Parts();
	// Topmost first: later parts are drawn on top.
	for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
		if (Contains(bdb::part::GetRect(*it), cardPoint))
			return bdb::part::Id(*it);
	}
	return "";
}


// The part that reacts in play / find mode (blank parts are invisible).
const Json*
CardSurface::_PartAtPlay(BPoint cardPoint) const
{
	const bdb::Template* tmpl = _Template();
	if (tmpl == NULL)
		return NULL;
	const JsonArray& parts = tmpl->Parts();
	for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
		if (bdb::part::Kind(*it) == bdb::kKindBlank
			|| IsPartHidden(bdb::part::Id(*it))) {
			continue;
		}
		if (Contains(bdb::part::GetRect(*it), cardPoint))
			return &*it;
	}
	return NULL;
}


// #pragma mark - selection and undo


void
CardSurface::_NotifySelection()
{
	if (Window() != NULL)
		Window()->PostMessage(kMsgSelectionChanged);
	Invalidate();
}


void
CardSurface::_NotifyEdited(const std::vector<std::string>* removed)
{
	if (Window() == NULL)
		return;
	BMessage message(kMsgTemplateEdited);
	if (removed != NULL) {
		for (const std::string& id : *removed)
			message.AddString("removed", id.c_str());
	}
	Window()->PostMessage(&message);
}


void
CardSurface::SetSelection(const std::set<std::string>& ids)
{
	fSelection = ids;
	_NotifySelection();
}


void
CardSurface::SelectAll()
{
	DocLocker lock(fDocument);
	const bdb::Template* tmpl = _Template();
	if (tmpl == NULL)
		return;
	fSelection = tmpl->PartIds();
	_NotifySelection();
}


void
CardSurface::ClearSelection()
{
	if (fSelection.empty())
		return;
	fSelection.clear();
	_NotifySelection();
}


void
CardSurface::PushUndo()
{
	DocLocker lock(fDocument);
	bdb::Template* tmpl = EditTemplate();
	if (tmpl == NULL)
		return;
	fUndo.push_back(tmpl->Doc());
	if (fUndo.size() > 100)
		fUndo.erase(fUndo.begin());
	fRedo.clear();
}


static void
DropMissing(std::set<std::string>& selection, const bdb::Template* tmpl)
{
	std::set<std::string> ids = tmpl->PartIds();
	for (auto it = selection.begin(); it != selection.end();) {
		if (ids.count(*it) == 0)
			it = selection.erase(it);
		else
			++it;
	}
}


void
CardSurface::Undo()
{
	CommitLabelEditor();
	DocLocker lock(fDocument);
	bdb::Template* tmpl = EditTemplate();
	if (tmpl == NULL || fUndo.empty())
		return;
	fRedo.push_back(tmpl->Doc());
	// Keep rev: it belongs to the file, not to the edit.
	Json rev = tmpl->Rev();
	tmpl->Doc() = fUndo.back();
	tmpl->Doc().Set("rev", rev);
	fUndo.pop_back();
	DropMissing(fSelection, tmpl);
	_LoadTheme();
	TemplateChanged();
}


void
CardSurface::Redo()
{
	CommitLabelEditor();
	DocLocker lock(fDocument);
	bdb::Template* tmpl = EditTemplate();
	if (tmpl == NULL || fRedo.empty())
		return;
	fUndo.push_back(tmpl->Doc());
	Json rev = tmpl->Rev();
	tmpl->Doc() = fRedo.back();
	tmpl->Doc().Set("rev", rev);
	fRedo.pop_back();
	DropMissing(fSelection, tmpl);
	_LoadTheme();
	TemplateChanged();
}


void
CardSurface::ClearUndo()
{
	fUndo.clear();
	fRedo.clear();
}


void
CardSurface::TemplateChanged(bool notify)
{
	_UpdateScrollBars();
	Invalidate();
	if (notify) {
		_NotifyEdited();
		if (Window() != NULL)
			Window()->PostMessage(kMsgSelectionChanged);
	}
}


bool
CardSurface::SelectionHasLocked() const
{
	DocLocker lock(fDocument);
	const bdb::Template* tmpl = _Template();
	if (tmpl == NULL)
		return false;
	for (const std::string& id : fSelection) {
		const Json* part = tmpl->FindPart(id);
		if (part != NULL && bdb::part::IsLocked(*part))
			return true;
	}
	return false;
}


bool
CardSurface::SelectionHasUnlocked() const
{
	DocLocker lock(fDocument);
	const bdb::Template* tmpl = _Template();
	if (tmpl == NULL)
		return false;
	for (const std::string& id : fSelection) {
		const Json* part = tmpl->FindPart(id);
		if (part != NULL && !bdb::part::IsLocked(*part))
			return true;
	}
	return false;
}


void
CardSurface::DeleteSelection()
{
	if (fSelection.empty())
		return;
	DocLocker lock(fDocument);
	bdb::Template* tmpl = EditTemplate();
	if (tmpl == NULL)
		return;
	if (!SelectionHasUnlocked()) {
		beep();		// locked parts cannot be deleted (UI 6.13)
		return;
	}
	PushUndo();
	std::vector<std::string> removed;
	JsonArray& parts = tmpl->Parts();
	for (auto it = parts.begin(); it != parts.end();) {
		std::string id = bdb::part::Id(*it);
		if (fSelection.count(id) != 0 && !bdb::part::IsLocked(*it)) {
			if (bdb::KindHasValue(bdb::part::Kind(*it)))
				removed.push_back(id);
			fSelection.erase(id);
			it = parts.erase(it);
		} else
			++it;
	}
	_UpdateScrollBars();
	Invalidate();
	_NotifyEdited(&removed);
	_NotifySelection();
}


void
CardSurface::CopySelection()
{
	DocLocker lock(fDocument);
	const bdb::Template* tmpl = _Template();
	if (tmpl == NULL || fSelection.empty())
		return;
	Json array = Json::MakeArray();
	for (const Json& part : tmpl->Parts()) {
		if (fSelection.count(bdb::part::Id(part)) != 0)
			array.Array().push_back(part);
	}
	std::string data = array.DumpCompact();
	if (be_clipboard->Lock()) {
		be_clipboard->Clear();
		BMessage* clip = be_clipboard->Data();
		clip->AddData(BDB_MIME_PARTS, B_MIME_TYPE, data.data(), data.size());
		be_clipboard->Commit();
		be_clipboard->Unlock();
	}
}


void
CardSurface::CutSelection()
{
	CopySelection();
	DeleteSelection();
}


void
CardSurface::_InsertParts(const JsonArray& source, int32 offset)
{
	DocLocker lock(fDocument);
	bdb::Template* tmpl = EditTemplate();
	if (tmpl == NULL || source.empty())
		return;
	if (tmpl->Parts().size() + source.size() > bdb::kMaxParts) {
		beep();
		return;
	}
	PushUndo();
	std::set<std::string> pasted;
	for (Json part : source) {
		if (!part.IsObject() || !part.Find("rect"))
			continue;
		// New id (6.3); a copy starts unlocked; keep the rest.
		std::string kind = bdb::part::Kind(part);
		std::string id = tmpl->NewPartId(kind);
		part.Set("id", id);
		bdb::part::SetLocked(part, false);
		bdb::Rect rect = bdb::part::GetRect(part);
		rect.x = std::min(rect.x + offset, tmpl->Width() - kMinPartSize);
		rect.y = std::min(rect.y + offset, tmpl->Height() - kMinPartSize);
		bdb::part::SetRect(part, rect);
		tmpl->Parts().push_back(part);
		pasted.insert(id);
	}
	fSelection = pasted;
	TemplateChanged();
}


void
CardSurface::Paste()
{
	std::string data;
	if (be_clipboard->Lock()) {
		const void* bytes = NULL;
		ssize_t size = 0;
		if (be_clipboard->Data()->FindData(BDB_MIME_PARTS, B_MIME_TYPE,
				&bytes, &size) == B_OK) {
			data.assign(static_cast<const char*>(bytes), size_t(size));
		}
		be_clipboard->Unlock();
	}
	Json parts;
	if (data.empty() || !Json::Parse(data, parts) || !parts.IsArray())
		return;
	_InsertParts(parts.Array(), 16);
}


void
CardSurface::DuplicateSelection()
{
	DocLocker lock(fDocument);
	const bdb::Template* tmpl = _Template();
	if (tmpl == NULL || fSelection.empty())
		return;
	JsonArray parts;
	for (const Json& part : tmpl->Parts()) {
		if (fSelection.count(bdb::part::Id(part)) != 0)
			parts.push_back(part);
	}
	_InsertParts(parts, 16);
}


void
CardSurface::NudgeSelection(int32 dx, int32 dy)
{
	DocLocker lock(fDocument);
	bdb::Template* tmpl = EditTemplate();
	if (tmpl == NULL || fSelection.empty())
		return;
	if (!SelectionHasUnlocked()) {
		beep();
		return;
	}
	PushUndo();
	for (Json& part : tmpl->Parts()) {
		if (fSelection.count(bdb::part::Id(part)) == 0
			|| bdb::part::IsLocked(part)) {
			continue;
		}
		bdb::Rect rect = bdb::part::GetRect(part);
		rect.x += dx;
		rect.y += dy;
		bdb::part::SetRect(part, rect);
	}
	TemplateChanged();
}


void
CardSurface::BringToFront()
{
	DocLocker lock(fDocument);
	bdb::Template* tmpl = EditTemplate();
	if (tmpl == NULL || fSelection.empty() || !SelectionHasUnlocked())
		return;
	PushUndo();
	JsonArray& parts = tmpl->Parts();
	JsonArray moved, rest;
	for (Json& part : parts) {
		if (fSelection.count(bdb::part::Id(part)) != 0
			&& !bdb::part::IsLocked(part)) {
			moved.push_back(part);
		} else
			rest.push_back(part);
	}
	rest.insert(rest.end(), moved.begin(), moved.end());
	parts = rest;
	TemplateChanged();
}


void
CardSurface::SendToBack()
{
	DocLocker lock(fDocument);
	bdb::Template* tmpl = EditTemplate();
	if (tmpl == NULL || fSelection.empty() || !SelectionHasUnlocked())
		return;
	PushUndo();
	JsonArray& parts = tmpl->Parts();
	JsonArray moved, rest;
	for (Json& part : parts) {
		if (fSelection.count(bdb::part::Id(part)) != 0
			&& !bdb::part::IsLocked(part)) {
			moved.push_back(part);
		} else
			rest.push_back(part);
	}
	moved.insert(moved.end(), rest.begin(), rest.end());
	parts = moved;
	TemplateChanged();
}


void
CardSurface::SetLocked(bool locked, bool all)
{
	DocLocker lock(fDocument);
	bdb::Template* tmpl = EditTemplate();
	if (tmpl == NULL)
		return;
	bool changes = false;
	for (const Json& part : tmpl->Parts()) {
		if ((all || fSelection.count(bdb::part::Id(part)) != 0)
			&& bdb::part::IsLocked(part) != locked) {
			changes = true;
		}
	}
	if (!changes)
		return;
	PushUndo();
	for (Json& part : tmpl->Parts()) {
		if (all || fSelection.count(bdb::part::Id(part)) != 0)
			bdb::part::SetLocked(part, locked);
	}
	TemplateChanged();
}


std::string
CardSurface::_UniqueName(const std::string& kind) const
{
	// "テキスト1", "数字2" ... ("Text 1" in English, UI 6.2, 14.1).
	const bdb::Template* tmpl = _Template();
	std::set<std::string> names;
	if (tmpl != NULL) {
		for (const Json& part : tmpl->Parts())
			names.insert(bdb::part::Name(part));
	}
	const char* base = KindDisplayName(kind);
	for (int32 i = 1;; i++) {
		BString name;
		name.SetToFormat(str::kDefaultNameFormat.Get(), base, (int)i);
		if (names.count(name.String()) == 0)
			return name.String();
	}
}


void
CardSurface::CreateFromPalette(int32 index, BPoint where,
	const bdb::Rect* size)
{
	if (index < 0 || index >= kPaletteItemCount)
		return;
	const PaletteItem& item = kPaletteItems[index];
	DocLocker lock(fDocument);
	bdb::Template* tmpl = EditTemplate();
	if (tmpl == NULL)
		return;
	if (tmpl->Parts().size() + item.count > bdb::kMaxParts) {
		beep();
		return;
	}
	PushUndo();
	int32 originX = _SnapValue(where.x);
	int32 originY = _SnapValue(where.y);
	std::set<std::string> created;
	std::string first;
	std::string firstKind;
	for (int32 i = 0; i < item.count; i++) {
		const PalettePiece& piece = item.pieces[i];
		bdb::Rect rect;
		if (size != NULL && item.count == 1)
			rect = *size;
		else {
			rect.x = originX + piece.x;
			rect.y = originY + piece.y;
			rect.w = piece.width;
			rect.h = piece.height;
		}
		// Keep it on the card.
		rect.x = std::max(0, std::min(rect.x, tmpl->Width() - rect.w));
		rect.y = std::max(0, std::min(rect.y, tmpl->Height() - rect.h));
		std::string kind = piece.kind;
		std::string id = tmpl->NewPartId(kind);
		Json part = bdb::part::Make(kind, id, bdb::KindHasValue(kind)
			|| kind == bdb::kKindBlank ? _UniqueName(kind) : std::string(),
			rect);
		if (kind == bdb::kKindLabel)
			part.Set("text", str::kNewLabelText.Get());
		if (kind == bdb::kKindButton) {
			part.Set("text", piece.text[0] != '\0' ? piece.text
				: str::kNewButtonText.Get());
			if (piece.action[0] != '\0')
				part.Set("action", piece.action);
		}
		tmpl->Parts().push_back(part);
		created.insert(id);
		if (first.empty()) {
			first = id;
			firstKind = kind;
		}
	}
	fSelection = created;
	TemplateChanged();
	if (Window() != NULL) {
		Window()->PostMessage(kMsgPaletteSelect);
		BMessage message(kMsgPartCreated);
		message.AddString("part", first.c_str());
		message.AddString("kind", firstKind.c_str());
		message.AddInt32("count", item.count);
		Window()->PostMessage(&message);
	}
	if (item.count == 1 && firstKind == bdb::kKindLabel)
		_StartLabelEditor(first);
}


void
CardSurface::CreateImagePart(BPoint where, const Json& defaultRef)
{
	DocLocker lock(fDocument);
	bdb::Template* tmpl = EditTemplate();
	if (tmpl == NULL || tmpl->Parts().size() >= bdb::kMaxParts)
		return;
	PushUndo();
	bdb::Rect rect;
	rect.w = 120;
	rect.h = 120;
	rect.x = std::max(0, std::min(_SnapValue(where.x), tmpl->Width() - rect.w));
	rect.y = std::max(0, std::min(_SnapValue(where.y),
		tmpl->Height() - rect.h));
	std::string id = tmpl->NewPartId(bdb::kKindImage);
	Json part = bdb::part::Make(bdb::kKindImage, id,
		_UniqueName(bdb::kKindImage), rect);
	part.Set("default", defaultRef);
	tmpl->Parts().push_back(part);
	fSelection = {id};
	TemplateChanged();
}


// #pragma mark - mouse


void
CardSurface::_Post(uint32 what, const std::string& partId)
{
	if (Window() == NULL)
		return;
	BMessage message(what);
	message.AddString("part", partId.c_str());
	Window()->PostMessage(&message);
}


void
CardSurface::MouseDown(BPoint where)
{
	int32 modifiers = 0;
	int32 clicks = 1;
	int32 buttons = B_PRIMARY_MOUSE_BUTTON;
	if (BMessage* current = Window()->CurrentMessage()) {
		current->FindInt32("modifiers", &modifiers);
		current->FindInt32("clicks", &clicks);
		current->FindInt32("buttons", &buttons);
	}
	if (fMode == kModeEdit)
		_EditMouseDown(where, buttons, clicks, modifiers);
	else
		_PlayMouseDown(where, buttons, clicks);
}


void
CardSurface::_PlayMouseDown(BPoint where, int32 buttons, int32 clicks)
{
	DocLocker lock(fDocument);
	const bdb::Template* tmpl = _Template();
	if (tmpl == NULL)
		return;
	BPoint point = _ToCard(where);
	const Json* part = _PartAtPlay(point);
	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		_PlayContextMenu(where, part);
		return;
	}
	if (part == NULL) {
		MakeFocus(true);
		return;
	}
	std::string kind = bdb::part::Kind(*part);
	std::string id = bdb::part::Id(*part);
	if (kind == bdb::kKindButton) {
		if (!_ButtonEnabled(*part))
			return;
		// Pressed while the button is held (theme 4.3); acts on release.
		fPressedPart = id;
		fPressedInside = true;
		fDragMode = kDragButton;
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
		Invalidate(_ToView(bdb::part::GetRect(*part)).InsetByCopy(-2, -2));
		return;
	}
	if (kind == bdb::kKindBool) {
		if (fMode == kModeFind) {
			// Any -> on -> off -> any (UI 8).
			auto found = fFindBool.find(id);
			int state = found != fFindBool.end() ? found->second : -1;
			fFindBool[id] = state == -1 ? 1 : state == 1 ? 0 : -1;
			Invalidate(_ToView(bdb::part::GetRect(*part)));
		} else
			_Post(kMsgBoolToggle, id);
		return;
	}
	if (fMode != kModeBrowse)
		return;
	if ((kind == bdb::kKindImage || kind == bdb::kKindLabel)
		&& bdb::HasLink(*part)) {
		// A linked image or label goes to its link (I/O 2.3); links open
		// in play mode only.
		if (clicks == 1)
			_Post(kMsgOpenLink, id);
		return;
	}
	if (kind == bdb::kKindImage && clicks == 2) {
		// Double click: the image at its own size; an empty frame asks for
		// an image (unless a switch locks it).
		const bdb::Card* card = _Card();
		if (card != NULL
			&& bdb::IsMediaRef(bdb::EffectiveValue(*card, *part))) {
			_Post(kMsgImageView, id);
		} else if (!IsPartLocked(id))
			_Post(kMsgImageChoose, id);
	}
}


void
CardSurface::_PlayContextMenu(BPoint where, const Json* part)
{
	BPopUpMenu* menu = new BPopUpMenu("context", false, false);
	std::string id = part != NULL ? bdb::part::Id(*part) : std::string();
	std::string kind = part != NULL ? bdb::part::Kind(*part) : std::string();
	if (fMode == kModeBrowse && kind == bdb::kKindImage
		&& !IsPartLocked(id)) {
		BMessage* choose = new BMessage(kMsgImageChoose);
		choose->AddString("part", id.c_str());
		menu->AddItem(new BMenuItem(str::kImageFromFile, choose));
		BMessage* clear = new BMessage(kMsgImageClear);
		clear->AddString("part", id.c_str());
		menu->AddItem(new BMenuItem(str::kImageClear, clear));
	}
	if (part != NULL && bdb::HasLink(*part) && fMode == kModeBrowse
		&& kind != bdb::kKindButton) {
		BMessage* open = new BMessage(kMsgOpenLink);
		open->AddString("part", id.c_str());
		menu->AddItem(new BMenuItem(str::kOpenLink, open));
	}
	if (menu->CountItems() == 0 && fMode == kModeBrowse) {
		// The empty card (UI 10.5).
		menu->AddItem(new BMenuItem(str::kNewCard, new BMessage(kMsgNewCard)));
		BMenuItem* duplicate = new BMenuItem(str::kDuplicateCard,
			new BMessage(kMsgDuplicateCard));
		BMenuItem* remove = new BMenuItem(str::kDeleteCard,
			new BMessage(kMsgDeleteCard));
		bool real = _Card() != NULL
			&& !fDocument->GetStack().IsProvisional(fCardId);
		duplicate->SetEnabled(real);
		remove->SetEnabled(real);
		menu->AddItem(duplicate);
		menu->AddItem(remove);
	}
	if (menu->CountItems() == 0) {
		delete menu;
		return;
	}
	menu->SetTargetForItems(Window());
	menu->SetAsyncAutoDestruct(true);
	menu->Go(ConvertToScreen(where), true, true, true);
}


void
CardSurface::_EditContextMenu(BPoint where, const std::string& hit)
{
	BPopUpMenu* menu = new BPopUpMenu("context", false, false);
	if (!hit.empty()) {
		bool locked = SelectionHasLocked();
		bool unlocked = SelectionHasUnlocked();
		BMenuItem* lockItem = new BMenuItem(str::kLock,
			new BMessage(kMsgLock));
		lockItem->SetEnabled(unlocked);
		menu->AddItem(lockItem);
		BMenuItem* unlockItem = new BMenuItem(str::kUnlock,
			new BMessage(kMsgUnlock));
		unlockItem->SetEnabled(locked);
		menu->AddItem(unlockItem);
		menu->AddSeparatorItem();
		const char* labels[] = { str::kDuplicatePart, str::kDeletePart,
			str::kBringToFront, str::kSendToBack };
		uint32 whats[] = { kMsgDuplicatePart, kMsgDeletePart,
			kMsgBringToFront, kMsgSendToBack };
		for (int i = 0; i < 4; i++) {
			BMenuItem* item = new BMenuItem(labels[i], new BMessage(whats[i]));
			item->SetEnabled(i == 0 || unlocked);
			menu->AddItem(item);
		}
		const bdb::Template* tmpl = _Template();
		const Json* part = tmpl != NULL ? tmpl->FindPart(hit) : NULL;
		std::string kind = part != NULL ? bdb::part::Kind(*part)
			: std::string();
		bool single = fSelection.size() == 1 && part != NULL
			&& !bdb::part::IsLocked(*part);
		if (kind == bdb::kKindButton || kind == bdb::kKindLabel) {
			BMenuItem* icon = new BMenuItem(str::kChooseIcon,
				new BMessage(kMsgChooseIcon));
			icon->SetEnabled(single);
			menu->AddItem(icon);
		}
		if (kind == bdb::kKindButton || kind == bdb::kKindLabel
			|| kind == bdb::kKindImage) {
			BMenuItem* link = new BMenuItem(str::kSetLink,
				new BMessage(kMsgSetLink));
			link->SetEnabled(single);
			menu->AddItem(link);
		}
		if (bdb::IsAssignableKind(kind)) {
			BMenu* kinds = new BMenu(str::kKindSubmenu);
			const char* names[] = { "blank", "text", "number", "bool",
				"image" };
			const char* labelsKind[] = { str::kKindBlank, str::kKindText,
				str::kKindNumber, str::kKindBool, str::kKindImage };
			for (int i = 0; i < 5; i++) {
				BMessage* message = new BMessage(kMsgPartKind);
				message->AddString("kind", names[i]);
				BMenuItem* item = new BMenuItem(labelsKind[i], message);
				item->SetMarked(kind == names[i]);
				kinds->AddItem(item);
			}
			kinds->SetTargetForItems(Window());
			kinds->SetEnabled(single);
			menu->AddItem(kinds);
		}
	} else {
		menu->AddItem(new BMenuItem(str::kPaste, new BMessage(kMsgPaste)));
		menu->AddItem(new BMenuItem(str::kTemplateProperties,
			new BMessage(kMsgTemplateProps)));
		menu->AddItem(new BMenuItem(str::kBackgroundMenu,
			new BMessage(kMsgBackgroundDialog)));
	}
	menu->SetTargetForItems(Window());
	menu->SetAsyncAutoDestruct(true);
	menu->Go(ConvertToScreen(where), true, true, true);
}


void
CardSurface::_EditMouseDown(BPoint where, int32 buttons, int32 clicks,
	int32 modifiers)
{
	CommitLabelEditor();
	MakeFocus(true);
	DocLocker lock(fDocument);
	bdb::Template* tmpl = EditTemplate();
	if (tmpl == NULL)
		return;

	bool shift = (modifiers & B_SHIFT_KEY) != 0;
	BPoint point = _ToCard(where);
	fDragStart = point;
	fDragCurrent = point;
	fDragUndoPushed = false;

	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		std::string hit = _PartAt(point);
		if (!hit.empty() && fSelection.count(hit) == 0) {
			fSelection = {hit};
			_NotifySelection();
		} else if (hit.empty() && !fSelection.empty())
			ClearSelection();
		_EditContextMenu(where, hit);
		return;
	}

	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);

	if (fCreateItem >= 0) {
		// A palette item is chosen: drag out its size (UI 6.2).
		fDragStart = BPoint(_SnapValue(point.x), _SnapValue(point.y));
		fDragCurrent = fDragStart;
		fDragMode = kDragCreate;
		return;
	}

	int32 handle = _HandleAt(where);
	if (handle >= 0) {
		fDragMode = kDragResize;
		fDragHandle = handle;
		fDragOriginal.clear();
		const Json* part = tmpl->FindPart(*fSelection.begin());
		fDragOriginal[*fSelection.begin()] = bdb::part::GetRect(*part);
		return;
	}
	std::string hit = _PartAt(point);
	if (!hit.empty()) {
		if (shift) {
			if (fSelection.count(hit) != 0)
				fSelection.erase(hit);
			else
				fSelection.insert(hit);
		} else if (fSelection.count(hit) == 0)
			fSelection = {hit};
		_NotifySelection();
		const Json* part = tmpl->FindPart(hit);
		if (clicks == 2 && part != NULL && !bdb::part::IsLocked(*part)
			&& bdb::part::Kind(*part) == bdb::kKindLabel) {
			fDragMode = kDragNone;
			_StartLabelEditor(hit);
			return;
		}
		if (fSelection.count(hit) == 0) {
			fDragMode = kDragNone;
			return;
		}
		fDragMode = kDragMove;
		fDragOriginal.clear();
		for (const std::string& id : fSelection) {
			const Json* selected = tmpl->FindPart(id);
			// Locked parts do not move (UI 6.13).
			if (selected != NULL && !bdb::part::IsLocked(*selected))
				fDragOriginal[id] = bdb::part::GetRect(*selected);
		}
		if (fDragOriginal.empty())
			fDragMode = kDragNone;
		return;
	}
	fDragBaseSelection = shift ? fSelection : std::set<std::string>();
	if (!shift)
		fSelection.clear();
	fDragMode = kDragRubberBand;
	_NotifySelection();
}


void
CardSurface::_SetHover(const std::string& partId, BPoint where)
{
	if (partId == fHoverPart)
		return;
	std::string previous = fHoverPart;
	fHoverPart = partId;
	const bdb::Template* tmpl = _Template();
	const Json* part = tmpl != NULL && !partId.empty()
		? tmpl->FindPart(partId) : NULL;
	BString tip;
	BString hint;
	if (part != NULL) {
		std::string kind = bdb::part::Kind(*part);
		if (bdb::HasLink(*part)) {
			// Show where a link goes (I/O 2.3).
			std::string url;
			bdb::ResolveLink(*part, *tmpl, _Card(), url);
			tip = url.c_str();
			hint = url.c_str();
		} else if (kind == bdb::kKindButton) {
			tip = part->GetString("text").c_str();
		}
	}
	SetToolTip(tip.IsEmpty() ? NULL : tip.String());
	BMessage message(kMsgHint);
	message.AddString("text", hint);
	if (Window() != NULL)
		Window()->PostMessage(&message);
	// Buttons light up under the pointer.
	if (tmpl != NULL) {
		for (const std::string& id : { previous, partId }) {
			const Json* changed = id.empty() ? NULL : tmpl->FindPart(id);
			if (changed != NULL && bdb::part::Kind(*changed)
					== bdb::kKindButton) {
				Invalidate(_ToView(bdb::part::GetRect(*changed)));
			}
		}
	}
}


void
CardSurface::_PlayMouseMoved(BPoint where)
{
	DocLocker lock(fDocument);
	BPoint point = _ToCard(where);
	if (fDragMode == kDragButton) {
		const bdb::Template* tmpl = _Template();
		const Json* part = tmpl != NULL ? tmpl->FindPart(fPressedPart) : NULL;
		bool inside = part != NULL
			&& Contains(bdb::part::GetRect(*part), point);
		if (inside != fPressedInside) {
			fPressedInside = inside;
			Invalidate(_ToView(bdb::part::GetRect(*part)).InsetByCopy(-2,
				-2));
		}
		return;
	}
	const Json* part = _PartAtPlay(point);
	std::string id = part != NULL ? bdb::part::Id(*part) : std::string();
	_SetHover(id, where);
	_SetCursorFor(where);
}


void
CardSurface::_SetCursorFor(BPoint where)
{
	int32 kind = kCursorDefault;
	BPoint point = _ToCard(where);
	const bdb::Template* tmpl = _Template();
	if (fMode == kModeEdit && tmpl != NULL) {
		int32 handle = _HandleAt(where);
		if (fCreateItem >= 0)
			kind = kCursorCrossHair;
		else if (handle >= 0) {
			static const int32 kHandleCursors[8] = {
				kCursorResizeNWSE, kCursorResizeNS, kCursorResizeNESW,
				kCursorResizeEW, kCursorResizeNWSE, kCursorResizeNS,
				kCursorResizeNESW, kCursorResizeEW
			};
			kind = kHandleCursors[handle];
		} else {
			std::string hit = _PartAt(point);
			const Json* part = hit.empty() ? NULL : tmpl->FindPart(hit);
			if (part != NULL) {
				kind = bdb::part::IsLocked(*part) ? kCursorNotAllowed
					: kCursorMove;
			}
		}
	} else if (fMode == kModeBrowse && tmpl != NULL) {
		const Json* part = _PartAtPlay(point);
		if (part != NULL && bdb::HasLink(*part)) {
			std::string url;
			if (bdb::ResolveLink(*part, *tmpl, _Card(), url)
					== bdb::LinkCheck::kOk) {
				kind = kCursorLink;
			}
		}
	}
	if (kind == fCursorKind)
		return;
	fCursorKind = kind;
	BCursorID ids[] = {
		B_CURSOR_ID_SYSTEM_DEFAULT, B_CURSOR_ID_MOVE,
		B_CURSOR_ID_NOT_ALLOWED, B_CURSOR_ID_FOLLOW_LINK,
		B_CURSOR_ID_RESIZE_NORTH_WEST_SOUTH_EAST,
		B_CURSOR_ID_RESIZE_NORTH_EAST_SOUTH_WEST,
		B_CURSOR_ID_RESIZE_NORTH_SOUTH, B_CURSOR_ID_RESIZE_EAST_WEST,
		B_CURSOR_ID_CROSS_HAIR
	};
	BCursor cursor(ids[kind]);
	SetViewCursor(&cursor);
}


void
CardSurface::MouseMoved(BPoint where, uint32 code, const BMessage* dragMessage)
{
	if (dragMessage != NULL && dragMessage->what == kMsgPaletteDrag
		&& fMode == kModeEdit) {
		// Preview the dropped part's frame (UI 6.2).
		if (code == B_EXITED_VIEW)
			fDropItem = -1;
		else {
			fDropItem = dragMessage->GetInt32("item", -1);
			// The item is held where it was grabbed in the palette.
			fDropPoint = _ToCard(where)
				- dragMessage->GetPoint("offset", B_ORIGIN);
		}
		Invalidate();
		return;
	}
	if (fDropItem >= 0) {
		fDropItem = -1;
		Invalidate();
	}
	if (fMode != kModeEdit) {
		if (code == B_EXITED_VIEW && fDragMode != kDragButton) {
			DocLocker lock(fDocument);
			_SetHover("", where);
			return;
		}
		_PlayMouseMoved(where);
		return;
	}
	if (fDragMode == kDragNone) {
		DocLocker lock(fDocument);
		_SetCursorFor(where);
		BView::MouseMoved(where, code, dragMessage);
		return;
	}
	DocLocker lock(fDocument);
	bdb::Template* tmpl = EditTemplate();
	if (tmpl == NULL)
		return;
	BPoint point = _ToCard(where);
	fDragCurrent = point;

	switch (fDragMode) {
		case kDragMove:
		case kDragResize:
		{
			float dx = point.x - fDragStart.x;
			float dy = point.y - fDragStart.y;
			if (!fDragUndoPushed) {
				if (fabsf(dx) < 2 && fabsf(dy) < 2)
					return;
				// Snapshot of the state before the drag.
				Json current = tmpl->Doc();
				for (auto& entry : fDragOriginal) {
					if (Json* part = tmpl->FindPart(entry.first))
						bdb::part::SetRect(*part, entry.second);
				}
				PushUndo();
				tmpl->Doc() = current;
				fDragUndoPushed = true;
			}
			if (fDragMode == kDragMove) {
				// Snap the first part's origin; move the rest with it.
				const bdb::Rect& anchor = fDragOriginal.begin()->second;
				int32 snappedX = _SnapValue(anchor.x + dx) - anchor.x;
				int32 snappedY = _SnapValue(anchor.y + dy) - anchor.y;
				for (auto& entry : fDragOriginal) {
					Json* part = tmpl->FindPart(entry.first);
					if (part == NULL)
						continue;
					bdb::Rect rect = entry.second;
					rect.x += snappedX;
					rect.y += snappedY;
					bdb::part::SetRect(*part, rect);
				}
			} else {
				auto& entry = *fDragOriginal.begin();
				Json* part = tmpl->FindPart(entry.first);
				if (part == NULL)
					return;
				bdb::Rect original = entry.second;
				int32 left = original.x;
				int32 top = original.y;
				int32 right = original.x + original.w;
				int32 bottom = original.y + original.h;
				// Handles: 0 LT, 1 T, 2 RT, 3 R, 4 RB, 5 B, 6 LB, 7 L
				int32 h = fDragHandle;
				if (h == 0 || h == 6 || h == 7)
					left = std::min(_SnapValue(original.x + dx),
						right - kMinPartSize);
				if (h == 2 || h == 3 || h == 4)
					right = std::max(_SnapValue(right + dx),
						left + kMinPartSize);
				if (h == 0 || h == 1 || h == 2)
					top = std::min(_SnapValue(original.y + dy),
						bottom - kMinPartSize);
				if (h == 4 || h == 5 || h == 6)
					bottom = std::max(_SnapValue(bottom + dy),
						top + kMinPartSize);
				bdb::Rect rect;
				rect.x = left;
				rect.y = top;
				rect.w = right - left;
				rect.h = bottom - top;
				bdb::part::SetRect(*part, rect);
			}
			Invalidate();
			if (Window() != NULL)
				Window()->PostMessage(kMsgSelectionChanged);
			break;
		}
		case kDragRubberBand:
		{
			float left = std::min(fDragStart.x, point.x);
			float right = std::max(fDragStart.x, point.x);
			float top = std::min(fDragStart.y, point.y);
			float bottom = std::max(fDragStart.y, point.y);
			fSelection = fDragBaseSelection;
			for (const Json& part : tmpl->Parts()) {
				bdb::Rect rect = bdb::part::GetRect(part);
				if (rect.x < right && rect.x + rect.w > left
					&& rect.y < bottom && rect.y + rect.h > top) {
					fSelection.insert(bdb::part::Id(part));
				}
			}
			Invalidate();
			break;
		}
		case kDragCreate:
			fDragCurrent = BPoint(_SnapValue(point.x), _SnapValue(point.y));
			Invalidate();
			break;
		default:
			break;
	}
}


void
CardSurface::MouseUp(BPoint where)
{
	if (fDragMode == kDragButton) {
		fDragMode = kDragNone;
		std::string id = fPressedPart;
		bool inside = fPressedInside;
		fPressedPart.clear();
		fPressedInside = false;
		Invalidate();
		if (!inside)
			return;
		DocLocker lock(fDocument);
		const bdb::Template* tmpl = _Template();
		const Json* part = tmpl != NULL ? tmpl->FindPart(id) : NULL;
		if (part == NULL || Window() == NULL)
			return;
		BMessage message(kMsgButtonAction);
		message.AddString("part", id.c_str());
		message.AddString("action", part->GetString("action").c_str());
		Window()->PostMessage(&message);
		return;
	}
	if (fMode != kModeEdit || fDragMode == kDragNone) {
		BView::MouseUp(where);
		return;
	}
	DocLocker lock(fDocument);
	bdb::Template* tmpl = EditTemplate();
	DragMode mode = fDragMode;
	fDragMode = kDragNone;
	if (tmpl == NULL)
		return;

	if (mode == kDragCreate) {
		bdb::Rect rect;
		rect.x = int32(std::min(fDragStart.x, fDragCurrent.x));
		rect.y = int32(std::min(fDragStart.y, fDragCurrent.y));
		rect.w = int32(fabsf(fDragCurrent.x - fDragStart.x));
		rect.h = int32(fabsf(fDragCurrent.y - fDragStart.y));
		int32 item = fCreateItem;
		fCreateItem = -1;
		if (rect.w >= kMinPartSize && rect.h >= kMinPartSize)
			CreateFromPalette(item, fDragStart, &rect);
		else
			CreateFromPalette(item, fDragStart);
		Invalidate();
		return;
	}
	if (mode == kDragMove || mode == kDragResize) {
		if (fDragUndoPushed)
			_NotifyEdited();
		Invalidate();
		return;
	}
	_NotifySelection();
}


void
CardSurface::KeyDown(const char* bytes, int32 numBytes)
{
	if (fMode != kModeEdit || numBytes != 1) {
		BView::KeyDown(bytes, numBytes);
		return;
	}
	int32 modifiers = 0;
	if (BMessage* current = Window()->CurrentMessage())
		current->FindInt32("modifiers", &modifiers);
	int32 step = (modifiers & B_SHIFT_KEY) != 0 ? 10 : 1;
	switch (bytes[0]) {
		case B_LEFT_ARROW:
			NudgeSelection(-step, 0);
			break;
		case B_RIGHT_ARROW:
			NudgeSelection(step, 0);
			break;
		case B_UP_ARROW:
			NudgeSelection(0, -step);
			break;
		case B_DOWN_ARROW:
			NudgeSelection(0, step);
			break;
		case B_DELETE:
		case B_BACKSPACE:
			DeleteSelection();
			break;
		case B_ESCAPE:
			if (fCreateItem >= 0) {
				fCreateItem = -1;
				if (Window() != NULL)
					Window()->PostMessage(kMsgPaletteSelect);
			} else
				ClearSelection();
			break;
		default:
			BView::KeyDown(bytes, numBytes);
			break;
	}
}


// #pragma mark - label editor


void
CardSurface::_StartLabelEditor(const std::string& partId)
{
	CommitLabelEditor();
	DocLocker lock(fDocument);
	const bdb::Template* tmpl = _Template();
	const Json* part = tmpl != NULL ? tmpl->FindPart(partId) : NULL;
	if (part == NULL)
		return;
	BRect frame = _ToView(bdb::part::GetRect(*part));
	frame.right = std::max(frame.right, frame.left + 120 * fScale);
	frame.bottom = std::max(frame.bottom, frame.top + 20 * fScale);
	LabelEditor* editor = new LabelEditor(frame, this);
	BFont font = FontForLook(fTheme.Resolve(*part));
	font.SetSize(font.Size() * fScale);
	editor->SetFontAndColor(&font);
	editor->SetText(part->GetString("text").c_str());
	AddChild(editor);
	editor->SelectAll();
	editor->MakeFocus(true);
	fLabelEditor = editor;
	fLabelEditorPart = partId;
}


void
CardSurface::CommitLabelEditor()
{
	if (fLabelEditor == NULL)
		return;
	BTextView* editor = fLabelEditor;
	fLabelEditor = NULL;
	{
		DocLocker lock(fDocument);
		bdb::Template* tmpl = EditTemplate();
		Json* part = tmpl != NULL ? tmpl->FindPart(fLabelEditorPart) : NULL;
		if (part != NULL && part->GetString("text") != editor->Text()) {
			PushUndo();
			part->Set("text", editor->Text());
		}
	}
	RemoveChild(editor);
	delete editor;
	TemplateChanged();
	MakeFocus(true);
}


// #pragma mark - messages


void
CardSurface::MessageReceived(BMessage* message)
{
	if (message->WasDropped()) {
		BPoint dropPoint = ConvertFromScreen(message->DropPoint());
		BPoint point = _ToCard(dropPoint);
		if (message->what == kMsgPaletteDrag && fMode == kModeEdit) {
			fDropItem = -1;
			CreateFromPalette(message->GetInt32("item", -1),
				point - message->GetPoint("offset", B_ORIGIN));
			Invalidate();
			return;
		}
		if (message->HasRef("refs")) {
			entry_ref ref;
			message->FindRef("refs", &ref);
			if (fMode == kModeEdit) {
				// Background or image part (UI 6.2).
				BMessage forward(kMsgEditImageDrop);
				forward.AddRef("refs", &ref);
				forward.AddPoint("point", point);
				Window()->PostMessage(&forward);
				return;
			}
			if (fMode == kModeBrowse) {
				DocLocker lock(fDocument);
				const Json* part = _PartAtPlay(point);
				if (part != NULL
					&& bdb::part::Kind(*part) == bdb::kKindImage
					&& !IsPartLocked(bdb::part::Id(*part))) {
					BMessage forward(kMsgImageSet);
					forward.AddString("part", bdb::part::Id(*part).c_str());
					forward.AddRef("refs", &ref);
					Window()->PostMessage(&forward);
				}
				return;
			}
		}
	}
	switch (message->what) {
		case kMsgLabelCommit:
			CommitLabelEditor();
			break;
		default:
			BView::MessageReceived(message);
			break;
	}
}
