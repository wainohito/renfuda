/*
 * BDB card database - RenkuOS application
 * Start window and preset chooser (UI spec 3.2, 3.3).
 */
#ifndef BDB_START_WINDOW_H
#define BDB_START_WINDOW_H

#include <String.h>
#include <Window.h>

#include <vector>

#include "Preset.h"


class BButton;
class BListView;
class BTextView;
class PresetPreview;


// How a recent stack is shown: the stack name, with a mark for samples
// (UI 3.5).
BString RecentStackLabel(const char* path);


class StartWindow : public BWindow {
public:
								StartWindow();
	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_FillRecent();

			BListView*			fRecent;
};


class PresetWindow : public BWindow {
public:
								PresetWindow(const char* directory);
	virtual	void				MessageReceived(BMessage* message);

private:
			std::vector<bdb::PresetInfo> fPresets;
			BListView*			fList;
			PresetPreview*		fPreview;
			BTextView*			fDescription;
			BButton*			fCreate;
};


#endif	// BDB_START_WINDOW_H
