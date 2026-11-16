/*
 * BDB card database - RenkuOS application
 */
#include "PaperWindow.h"

#include <Beep.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <TextControl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Constants.h"
#include "Limits.h"
#include "Model.h"
#include "Strings.h"


static const uint32 kMsgPaperSize = 'ppSz';
static const uint32 kMsgPaperOrientation = 'ppOr';
static const uint32 kMsgPaperCustom = 'ppCu';
static const uint32 kMsgPaperOK = 'ppOK';


// The shape of the chosen paper, to scale (UI 3.4).
class PaperPreview : public BView {
public:
	PaperPreview()
		:
		BView("preview", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
		fWidth(512),
		fHeight(342)
	{
		SetExplicitMinSize(BSize(160, 160));
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	}

	void SetSize(int32 width, int32 height)
	{
		fWidth = width;
		fHeight = height;
		Invalidate();
	}

	virtual void Draw(BRect)
	{
		BRect bounds = Bounds().InsetByCopy(8, 8);
		float scale = std::min((bounds.Width() + 1) / fWidth,
			(bounds.Height() + 1) / fHeight);
		float width = fWidth * scale;
		float height = fHeight * scale;
		BRect paper(bounds.left + (bounds.Width() - width) / 2,
			bounds.top + (bounds.Height() - height) / 2, 0, 0);
		paper.right = paper.left + width;
		paper.bottom = paper.top + height;
		SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
			B_DARKEN_2_TINT));
		FillRect(paper.OffsetByCopy(3, 3));
		SetHighColor(255, 255, 255);
		FillRect(paper);
		SetHighColor(ui_color(B_CONTROL_BORDER_COLOR));
		StrokeRect(paper);
		char text[32];
		snprintf(text, sizeof(text), "%d × %d", (int)fWidth, (int)fHeight);
		SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
		DrawString(text, BPoint(paper.left + 4, paper.bottom - 4));
	}

private:
	int32	fWidth;
	int32	fHeight;
};


PaperWindow::PaperWindow(BMessenger target, const char* size,
	const char* orientation, int32 width, int32 height, const BMessage* extra)
	:
	BWindow(BRect(0, 0, 420, 260), str::kPaperTitle, B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_CLOSE_ON_ESCAPE),
	fTarget(target),
	fSize(size),
	fOrientation(orientation)
{
	if (extra != NULL)
		fExtra = *extra;
	BPopUpMenu* sizes = new BPopUpMenu("size");
	auto addSize = [&](const char* value, const char* label) {
		BMessage* message = new BMessage(kMsgPaperSize);
		message->AddString("size", value);
		BMenuItem* item = new BMenuItem(label, message);
		item->SetMarked(fSize == value);
		sizes->AddItem(item);
	};
	addSize("screen", str::kPaperScreen);
	size_t count;
	const bdb::PaperInfo* papers = bdb::PaperSizes(&count);
	for (size_t i = 0; i < count; i++) {
		char label[64];
		bool postcard = BString(papers[i].size) == "postcard";
		snprintf(label, sizeof(label), "%s (%d×%d mm)",
			postcard ? str::kPaperPostcard.Get() : papers[i].size,
			(int)papers[i].widthMm, (int)papers[i].heightMm);
		addSize(papers[i].size, label);
	}
	addSize("custom", str::kPaperCustom);
	fSizeField = new BMenuField("size", str::kPaperSize, sizes);

	BPopUpMenu* orientations = new BPopUpMenu("orientation");
	const char* values[] = { "portrait", "landscape" };
	const char* labels[] = { str::kPortrait, str::kLandscape };
	for (int i = 0; i < 2; i++) {
		BMessage* message = new BMessage(kMsgPaperOrientation);
		message->AddString("orientation", values[i]);
		BMenuItem* item = new BMenuItem(labels[i], message);
		item->SetMarked(fOrientation == values[i]);
		orientations->AddItem(item);
	}
	fOrientationField = new BMenuField("orientation", str::kPaperOrientation,
		orientations);

	char number[16];
	snprintf(number, sizeof(number), "%d", (int)width);
	fWidth = new BTextControl("width", str::kPaperWidth, number,
		new BMessage(kMsgPaperCustom));
	fWidth->SetModificationMessage(new BMessage(kMsgPaperCustom));
	snprintf(number, sizeof(number), "%d", (int)height);
	fHeight = new BTextControl("height", str::kPaperHeight, number,
		new BMessage(kMsgPaperCustom));
	fHeight->SetModificationMessage(new BMessage(kMsgPaperCustom));
	fPreview = new PaperPreview();

	// Keep the menus as wide as their longest item, so that they do not
	// change size with the choice.
	BMenuField* fields[] = { fSizeField, fOrientationField };
	float menuWidth = 0;
	for (BMenuField* field : fields) {
		BMenu* menu = field->Menu();
		for (int32 i = 0; i < menu->CountItems(); i++) {
			menuWidth = std::max(menuWidth,
				be_plain_font->StringWidth(menu->ItemAt(i)->Label()));
		}
	}
	for (BMenuField* field : fields) {
		field->MenuBar()->SetExplicitMinSize(
			BSize(ceilf(menuWidth) + 40, B_SIZE_UNSET));
	}

	BButton* ok = new BButton("ok", str::kOK, new BMessage(kMsgPaperOK));
	BButton* cancel = new BButton("cancel", str::kCancel,
		new BMessage(B_QUIT_REQUESTED));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGroup(B_HORIZONTAL)
			.AddGrid()
				.AddMenuField(fSizeField, 0, 0)
				.AddMenuField(fOrientationField, 0, 1)
				.AddTextControl(fWidth, 0, 2)
				.AddTextControl(fHeight, 0, 3)
			.End()
			.Add(fPreview)
		.End()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(ok)
		.End();
	SetDefaultButton(ok);
	CenterOnScreen();
	_Update();
}


bool
PaperWindow::_Size(int32& width, int32& height) const
{
	if (fSize == "screen") {
		width = bdb::kDefaultCardWidth;
		height = bdb::kDefaultCardHeight;
		// The screen standard is landscape; portrait swaps it.
		if (fOrientation == "portrait")
			std::swap(width, height);
		return true;
	}
	if (fSize == "custom") {
		width = atoi(fWidth->Text());
		height = atoi(fHeight->Text());
		return width >= bdb::kMinCardSize && width <= bdb::kMaxCardSize
			&& height >= bdb::kMinCardSize && height <= bdb::kMaxCardSize;
	}
	return bdb::PaperPixels(fSize.String(), fOrientation.String(), width,
		height);
}


void
PaperWindow::_Update()
{
	bool custom = fSize == "custom";
	fWidth->SetEnabled(custom);
	fHeight->SetEnabled(custom);
	int32 width, height;
	if (_Size(width, height)) {
		if (!custom) {
			// SetText() sends the modification message, so only touch the
			// fields when the number really changes.
			char number[16];
			snprintf(number, sizeof(number), "%d", (int)width);
			if (strcmp(fWidth->Text(), number) != 0)
				fWidth->SetText(number);
			snprintf(number, sizeof(number), "%d", (int)height);
			if (strcmp(fHeight->Text(), number) != 0)
				fHeight->SetText(number);
		}
		fPreview->SetSize(width, height);
		if (custom && width != height) {
			// A custom size takes its orientation from its shape.
			fOrientation = width > height ? "landscape" : "portrait";
			BMenuItem* item = fOrientationField->Menu()->ItemAt(
				fOrientation == "landscape" ? 1 : 0);
			if (item != NULL && !item->IsMarked())
				item->SetMarked(true);
		}
	}
}


void
PaperWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPaperSize:
			fSize = message->GetString("size", "screen");
			if (fSize == "screen" && fOrientation == "portrait")
				fOrientation = "landscape";
			_Update();
			break;
		case kMsgPaperOrientation:
		{
			BString orientation = message->GetString("orientation",
				"portrait");
			if (orientation != fOrientation && fSize == "custom") {
				// Turning a custom size swaps width and height.
				BString width = fWidth->Text();
				fWidth->SetText(fHeight->Text());
				fHeight->SetText(width);
			}
			fOrientation = orientation;
			_Update();
			break;
		}
		case kMsgPaperCustom:
			// Only typing in a custom size needs an update; the fields
			// also report the numbers _Update() writes into them.
			if (fSize == "custom")
				_Update();
			break;
		case kMsgPaperOK:
		{
			int32 width, height;
			if (!_Size(width, height)) {
				beep();
				break;
			}
			BMessage reply(fExtra);
			reply.what = kMsgPaperChosen;
			reply.AddString("size", fSize);
			reply.AddString("orientation", fOrientation);
			reply.AddInt32("width", width);
			reply.AddInt32("height", height);
			fTarget.SendMessage(&reply);
			Quit();
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}
