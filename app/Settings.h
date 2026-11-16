/*
 * BDB card database - RenkuOS application
 * Application settings (recent stacks, window frames, zoom, list columns).
 * Kept in the user settings folder, never inside a stack (UI spec 2.4).
 */
#ifndef BDB_SETTINGS_H
#define BDB_SETTINGS_H

#include <Locker.h>
#include <Message.h>
#include <Rect.h>
#include <String.h>


class Settings {
public:
								Settings();

			status_t			Load();
			status_t			Save();

	static	BString				SettingsDirectory();	// .../config/settings/BDB
			BString				LogDirectory() const;

			void				AddRecent(const char* path);
			int32				CountRecent();
			BString				RecentAt(int32 index);

			bool				GetWindowFrame(const char* stackId,
									BRect& frame);
			void				SetWindowFrame(const char* stackId,
									BRect frame);
			float				Zoom(const char* stackId);
			bool				ZoomFit(const char* stackId);
			void				SetZoom(const char* stackId, float zoom,
									bool fit);

	// Column widths ("" key -> list of widths) and hidden flags.
			bool				GetColumns(const char* key, BMessage& out);
			void				SetColumns(const char* key,
									const BMessage& columns);

			bool				DebugLog();

	// Recent colors of the color chooser (UI 6.12), newest first.
			void				GetRecentColors(BMessage& out);
			void				AddRecentColor(const char* color);

	// Samples (UI 3.5): whether the bundled samples were put on the
	// recent list, and the ones the user removed (until reinstalled:
	// keyed by the install stamp).
			bool				SamplesSeeded(const char* installStamp);
			void				SetSamplesSeeded(const char* installStamp);
			bool				IsSampleRemoved(const char* name);
			void				RemoveSample(const char* name);
	// Recent entries can be samples: "sample:<name>".
			void				RemoveRecent(const char* path);
			void				AppendRecent(const char* path);

	// Links (I/O 2.3): stacks whose links open without asking.
			bool				TrustsLinks(const char* stackId);
			void				SetTrustsLinks(const char* stackId);
	// Image fetch prompt shown once per stack (UI 3.5).
			bool				FetchAsked(const char* stackId);
			void				SetFetchAsked(const char* stackId);

	// Last paper chosen for a new stack (UI 3.4).
			BString				LastPaper();
			BString				LastOrientation();
			void				SetLastPaper(const char* size,
									const char* orientation);
	// The browser for http(s) links: the path of an application, or
	// empty for the system's preferred application.
			BString				Browser();
			void				SetBrowser(const char* path);

private:
			BLocker				fLock;
			BMessage			fData;
};


extern Settings gSettings;


#endif	// BDB_SETTINGS_H
