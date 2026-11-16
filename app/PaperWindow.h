/*
 * BDB card database - RenkuOS application
 * Choosing the paper size and orientation (UI spec v0.3, 3.4, 6.15).
 */
#ifndef BDB_PAPER_WINDOW_H
#define BDB_PAPER_WINDOW_H

#include <Messenger.h>
#include <String.h>
#include <Window.h>


class BMenuField;
class BTextControl;
class PaperPreview;


// Replies kMsgPaperChosen with "size" ("screen", "A4", ..., "postcard",
// "custom"), "orientation", "width", "height" (card pixels) and the
// fields of `extra`.
class PaperWindow : public BWindow {
public:
								PaperWindow(BMessenger target,
									const char* size, const char* orientation,
									int32 width, int32 height,
									const BMessage* extra = NULL);
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Update();
			bool				_Size(int32& width, int32& height) const;

			BMessenger			fTarget;
			BMessage			fExtra;
			BString				fSize;
			BString				fOrientation;
			BMenuField*			fSizeField;
			BMenuField*			fOrientationField;
			BTextControl*		fWidth;
			BTextControl*		fHeight;
			PaperPreview*		fPreview;
};


#endif	// BDB_PAPER_WINDOW_H
