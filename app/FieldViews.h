/*
 * BDB card database - RenkuOS application
 * Text and number inputs placed on the card surface in play and find mode
 * (UI spec v0.3, 5.3, 8). Images, switches and buttons are drawn and
 * handled by the card surface.
 */
#ifndef BDB_FIELD_VIEWS_H
#define BDB_FIELD_VIEWS_H

#include <String.h>
#include <TextView.h>


// Notifies the window with kMsgFieldEdited on every change,
// kMsgFieldCommit when focus leaves, kMsgFieldEnter on Enter in a single
// line field. A guarded field (an old value of another type is stored
// under the same key) does not accept input until the window has warned
// the user (kMsgFieldGuarded).
class FieldTextView : public BTextView {
public:
								FieldTextView(BRect frame, const char* partId,
									bool multiline, const BFont& font,
									rgb_color textColor, rgb_color background);

			const BString&		PartId() const { return fPartId; }

	// Replaces the text without sending kMsgFieldEdited.
			void				SetValueText(const char* text);
	// Number fields: the formatted text is shown while not focused, the
	// stored value while editing (UI spec 5.3).
			void				SetNumberTexts(const char* display,
									const char* raw);
			bool				IsNumber() const { return fIsNumber; }
			BString				RawText() const { return fRawText; }

			void				SetGuarded(bool guarded);
			bool				IsGuarded() const { return fGuarded; }
	// Locked by a switch: shown, but neither focused nor selected.
			void				SetLocked(bool locked);
			bool				IsLocked() const { return fLocked; }

	virtual	void				KeyDown(const char* bytes, int32 numBytes);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MakeFocus(bool focus = true);
	virtual	void				FrameResized(float width, float height);

protected:
	virtual	void				InsertText(const char* text, int32 length,
									int32 offset, const text_run_array* runs);
	virtual	void				DeleteText(int32 fromOffset, int32 toOffset);

private:
			void				_Post(uint32 what);

			BString				fPartId;
			bool				fMultiline;
			bool				fIsNumber;
			bool				fGuarded;
			bool				fLocked;
			bool				fSuppress;
			BString				fDisplayText;
			BString				fRawText;
};


#endif	// BDB_FIELD_VIEWS_H
