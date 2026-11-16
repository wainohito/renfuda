/*
 * BDB card database - RenkuOS application
 * Start-up, opening and creating stacks (UI spec v0.3, 3), samples (3.5),
 * the bundled themes, icons and presets.
 */
#ifndef BDB_APP_H
#define BDB_APP_H

#include <Application.h>
#include <Messenger.h>
#include <String.h>

#include <map>
#include <memory>

#include "Log.h"
#include "Theme.h"


class BFilePanel;
class StackDocument;


class BdbApp : public BApplication {
public:
								BdbApp();
	virtual						~BdbApp();

	virtual	void				ReadyToRun();
	virtual	void				ArgvReceived(int32 argc, char** argv);
	virtual	void				RefsReceived(BMessage* message);
	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

			bdb::FileLogSink*	LogSink() const { return fLogSink.get(); }
	const	bdb::ThemeLibrary&	Themes() const { return fThemes; }

	// <application folder>/presets/<ja|en>, samples/<ja|en>, themes,
	// icons/hvif
	static	BString				AppDirectory();
	static	BString				PresetDirectory();
	static	BString				SampleDirectory();
	static	BString				ThemeDirectory();
	static	BString				IconDirectory();
	// ~/config/settings/BDB/themes (the user's themes, theme spec 3.1)
	static	BString				UserThemeDirectory();
	// ~/Documents/BDB/Samples (UI 3.5)
	static	BString				SampleCopyDirectory();

private:
			void				_SetOwnIcon();
			void				_SetStackIcon(const char* folderPath);
			void				_SetRecentStackIcons();
			void				_OpenPath(const char* path,
									const char* recentAs = NULL,
									bool fetch = false);
			void				_OpenSample(const char* name);
			void				_SeedSamples();
			void				_ShowStart();
			void				_ShowSavePanel(const char* kind,
									const char* source, const char* preset,
									const BMessage* extra = NULL);
			void				_ChoosePaper();
			void				_CreateStack(BMessage* message);
			void				_Released(StackDocument* document);
			void				_CloseStartWindow();
			void				_InstallMimeTypes();
			void				_QuitIfIdle();

			std::unique_ptr<bdb::FileLogSink> fLogSink;
			std::unique_ptr<bdb::Logger> fLogger;
			bdb::ThemeLibrary	fThemes;
			std::map<BString, StackDocument*> fDocuments;	// by root path
			std::map<StackDocument*, BMessenger> fStackWindows;
			BFilePanel*			fOpenPanel;
			BFilePanel*			fSavePanel;
			BMessenger			fStartWindow;
			BMessenger			fPresetWindow;
			BMessenger			fWizard;
			bool				fOpenedFromArgs;
			bool				fLogFailureShown;
};


#endif	// BDB_APP_H
