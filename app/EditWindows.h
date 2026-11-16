/*
 * BDB card database - RenkuOS application
 * Floating windows of the edit mode (UI spec v0.3, 2.3, 6.2, 6.5,
 * 6.12-6.15) and the icon chooser (theme spec 8.4).
 */
#ifndef BDB_EDIT_WINDOWS_H
#define BDB_EDIT_WINDOWS_H

#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <string>
#include <vector>


class BCheckBox;
class BColorControl;
class BListView;
class BMenuField;
class BScrollView;
class BSlider;
class BStringView;
class BTextControl;
class IconGridView;
class PaletteView;


static const uint32 kMsgInspectorUpdate = 'iUpd';
static const uint32 kMsgColorPicked = 'iClr';

// The label of a palette item (Constants.h kPaletteItems).
const char* PaletteLabel(int32 index);


class ToolPalette : public BWindow {
public:
								ToolPalette(BMessenger target);
	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			BMessenger			fTarget;
			PaletteView*		fView;
};


// The inspector shows the state it is sent as a kMsgInspectorUpdate
// message and reports every change as kMsgInspectorChanged ("field",
// "value") to its target (the stack window), which owns the data.
//
// State: "mode" ("part", "multi", "template"), "count", "locked",
// "kind", and a string per field (see StackWindow::_UpdateInspector).
// "own:<field>" (bool) tells whether a style value is the part's own
// (shown normally) or the theme's (shown dimmed, UI 6.12). "menu:<field>"
// holds the choices of a menu as "value\tlabel" strings.
class InspectorWindow : public BWindow {
public:
								InspectorWindow(BMessenger target);
	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

	// Moves the keyboard focus to a field ("name" after placing a part,
	// UI 6.2).
			void				FocusField(const char* field);

private:
			void				_Rebuild(const BMessage& state);
			void				_Update(const BMessage& state);
			void				_AddRow(BView* label, BView* control);
			void				_AddRow(const char* label, BView* control);
			void				_AddWide(BView* view);
			void				_AddSection(const char* title);
			BTextControl*		_Text(const char* field);
			BMenuField*			_Menu(const BMessage& state,
									const char* field);
			BCheckBox*			_Check(const char* field,
									const char* label);
			BView*				_Color(const char* field);
			BView*				_Button(const char* field,
									const char* label);
			void				_Send(const char* field, const char* value);

			BMessenger			fTarget;
			BView*				fContainer;
			BScrollView*		fScroll;
			int32				fRow;
			BString				fSignature;
			std::vector<BView*>	fControls;
			bool				fLocked;
};


// A small color chooser (UI 6.12): the system color control, the recent
// colors and a "#rrggbb" field. Sends kMsgInspectorChanged ("field",
// "value") to the target.
class ColorWindow : public BWindow {
public:
								ColorWindow(BMessenger target,
									const char* field, const char* color);
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_SetColor(rgb_color color);

			BMessenger			fTarget;
			BString				fField;
			BColorControl*		fControl;
			BTextControl*		fText;
			BView*				fSwatch;
};


// Theme spec 8.4: a searchable grid of the bundled HVIF icons, the size
// and position, a preview, and "from file". Sends kMsgIconChosen with
// "path", "size", "placement".
class IconChooserWindow : public BWindow {
public:
								IconChooserWindow(BMessenger target,
									const char* directory, int32 size,
									const char* placement);
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Filter();
			void				_Choose(const char* path);

			BMessenger			fTarget;
			std::vector<BString> fAll;
			IconGridView*		fGrid;
			BTextControl*		fSearch;
			BSlider*			fSize;
			BMenuField*			fPlacement;
			BString				fPlacementValue;
			BView*				fPreview;
};


#endif	// BDB_EDIT_WINDOWS_H
