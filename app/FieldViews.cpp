/*
 * BDB card database - RenkuOS application
 */
#include "FieldViews.h"

#include <algorithm>

#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Window.h>

#include "CardSurface.h"
#include "Constants.h"
#include "Strings.h"


FieldTextView::FieldTextView(BRect frame, const char* partId, bool multiline,
	const BFont& font, rgb_color textColor, rgb_color background)
	:
	BTextView(frame, partId, BRect(0, 0, frame.Width(), frame.Height())
		.InsetByCopy(2, 1), B_FOLLOW_NONE, B_WILL_DRAW | B_NAVIGABLE
		| B_FRAME_EVENTS),
	fPartId(partId),
	fMultiline(multiline),
	fIsNumber(false),
	fGuarded(false),
	fLocked(false),
	fSuppress(false)
{
	SetWordWrap(multiline);
	SetStylable(false);
	SetFontAndColor(&font, B_FONT_ALL, &textColor);
	SetViewColor(background);
	SetLowColor(background);
	SetDoesUndo(true);
	if (!multiline) {
		// Center one line vertically in the frame.
		font_height height;
		font.GetHeight(&height);
		float lineHeight = height.ascent + height.descent + height.leading;
		float inset = std::max(0.0f, (frame.Height() + 1 - lineHeight) / 2
			- 1);
		SetInsets(2, inset, 2, 0);
	}
}


void
FieldTextView::_Post(uint32 what)
{
	if (Window() == NULL)
		return;
	BMessage message(what);
	message.AddString("part", fPartId);
	Window()->PostMessage(&message);
}


void
FieldTextView::SetValueText(const char* text)
{
	fSuppress = true;
	SetText(text);
	fSuppress = false;
}


void
FieldTextView::SetNumberTexts(const char* display, const char* raw)
{
	fIsNumber = true;
	fDisplayText = display;
	fRawText = raw;
	SetValueText(IsFocus() ? raw : display);
}


void
FieldTextView::SetGuarded(bool guarded)
{
	fGuarded = guarded;
	MakeEditable(!guarded);
}


void
FieldTextView::InsertText(const char* text, int32 length, int32 offset,
	const text_run_array* runs)
{
	BTextView::InsertText(text, length, offset, runs);
	if (!fSuppress)
		_Post(kMsgFieldEdited);
}


void
FieldTextView::DeleteText(int32 fromOffset, int32 toOffset)
{
	BTextView::DeleteText(fromOffset, toOffset);
	if (!fSuppress)
		_Post(kMsgFieldEdited);
}


void
FieldTextView::KeyDown(const char* bytes, int32 numBytes)
{
	if (numBytes == 1 && bytes[0] == B_TAB) {
		int32 modifiers = 0;
		if (Window() != NULL && Window()->CurrentMessage() != NULL)
			Window()->CurrentMessage()->FindInt32("modifiers", &modifiers);
		if (CardSurface* card = dynamic_cast<CardSurface*>(Parent()))
			card->FocusNeighbor(this, (modifiers & B_SHIFT_KEY) != 0);
		return;
	}
	if (fGuarded) {
		_Post(kMsgFieldGuarded);
		return;
	}
	if (numBytes == 1 && bytes[0] == B_ENTER && !fMultiline) {
		_Post(kMsgFieldEnter);
		return;
	}
	BTextView::KeyDown(bytes, numBytes);
}


void
FieldTextView::SetLocked(bool locked)
{
	fLocked = locked;
	MakeEditable(!locked);
	MakeSelectable(!locked);
	SetFlags(locked ? Flags() & ~B_NAVIGABLE : Flags() | B_NAVIGABLE);
	if (locked && IsFocus())
		MakeFocus(false);
	// Neither selectable nor editable: BTextView shows the arrow pointer.
}


void
FieldTextView::MouseDown(BPoint where)
{
	if (fLocked) {
		// Nothing to select or type in (switch lock).
		if (CardSurface* card = dynamic_cast<CardSurface*>(Parent()))
			card->MakeFocus(true);
		return;
	}
	int32 buttons = 0;
	if (Window() != NULL && Window()->CurrentMessage() != NULL)
		Window()->CurrentMessage()->FindInt32("buttons", &buttons);
	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		// Cut, copy, paste (UI 10.5).
		MakeFocus(true);
		int32 start, end;
		GetSelection(&start, &end);
		BPopUpMenu* menu = new BPopUpMenu("edit", false, false);
		BMenuItem* cut = new BMenuItem(str::kCut, new BMessage(B_CUT));
		BMenuItem* copy = new BMenuItem(str::kCopy, new BMessage(B_COPY));
		BMenuItem* paste = new BMenuItem(str::kPaste, new BMessage(B_PASTE));
		cut->SetEnabled(start != end && IsEditable());
		copy->SetEnabled(start != end);
		paste->SetEnabled(IsEditable());
		menu->AddItem(cut);
		menu->AddItem(copy);
		menu->AddItem(paste);
		menu->AddSeparatorItem();
		menu->AddItem(new BMenuItem(str::kSelectAll,
			new BMessage(B_SELECT_ALL)));
		menu->SetTargetForItems(this);
		menu->SetAsyncAutoDestruct(true);
		menu->Go(ConvertToScreen(where), true, true, true);
		return;
	}
	if (fGuarded) {
		MakeFocus(true);
		_Post(kMsgFieldGuarded);
		return;
	}
	BTextView::MouseDown(where);
}


void
FieldTextView::MakeFocus(bool focus)
{
	if (focus && fLocked)
		return;
	bool wasFocus = IsFocus();
	if (focus && !wasFocus && fIsNumber)
		SetValueText(fRawText);
	BTextView::MakeFocus(focus);
	if (!focus && wasFocus) {
		// The window validates RawText() and then calls SetNumberTexts().
		if (fIsNumber) {
			fRawText = Text();
			SetValueText(fDisplayText);
		}
		_Post(kMsgFieldCommit);
	}
	// The surface draws the focus ring around the part.
	if (Parent() != NULL)
		Parent()->Invalidate(Frame().InsetByCopy(-6, -6));
}


void
FieldTextView::FrameResized(float width, float height)
{
	BTextView::FrameResized(width, height);
}
