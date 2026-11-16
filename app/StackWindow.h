/*
 * BDB card database - RenkuOS application
 * The stack window: card surface, menus, banner, status bar, modes
 * (UI spec v0.3, 2-6, 8-11).
 */
#ifndef BDB_STACK_WINDOW_H
#define BDB_STACK_WINDOW_H

#include <Messenger.h>
#include <Window.h>

#include <string>
#include <vector>

#include "Constants.h"
#include "Json.h"
#include "Status.h"


class BButton;
class BFileGameSound;
class BFilePanel;
class BMenu;
class BMenuBar;
class BMenuItem;
class BMessageRunner;
class BScrollView;
class BStringView;
class BannerView;
class BMenuField;
class CardFrame;
class ModeButton;
class CardSurface;
class ImageFetcher;
class InspectorWindow;
class StackDocument;
class StatusLink;
class ToolPalette;


class StackWindow : public BWindow {
public:
								StackWindow(StackDocument* document,
									bool startInEditMode);
	virtual						~StackWindow();

			StackDocument*		Document() const { return fDocument; }

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();
	virtual	void				MenusBeginning();

	// Starts fetching the images of the stack in the background (I/O 3.6
	// "再取得", UI 3.5).
			void				StartFetching();

private:
			void				_BuildMenus();
			BView*				_BuildStatusBar();

			// navigation
			std::vector<std::string> _NavigationList() const;
			bool				_GoTo(const std::string& cardId);
			void				_GoToIndex(int32 index);
			void				_Step(int32 delta);
			int32				_CurrentNavIndex() const;
			void				_EnsureCard();
			void				_GoToNumberDialog();

			// values
			void				_FieldEdited(const char* partId);
			bool				_CommitNumber(const char* partId,
									const char* text);
			void				_CommitFields();
			void				_MarkDirty();
			bool				_SaveCurrent(bool interactive);
			bool				_LeaveCard();
			bool				_ConfirmGuarded(const char* partId);
			void				_ToggleBool(const char* partId);

			// cards
			void				_NewCard();
			void				_DuplicateCard();
			void				_DeleteCard();
			void				_ChangeTemplate(const char* templateId);
			void				_ButtonAction(const char* partId,
									const char* action);
			void				_OpenLink(const char* partId);

			// images
			void				_SetImage(const char* partId,
									const entry_ref& ref);
			void				_ChooseImage(const char* partId);
			void				_ClearImage(const char* partId);

			// modes
			void				_SetMode(Mode mode);
			void				_PlaceToolWindows();
			bool				_LeaveEdit();
			void				_RunFind();
			void				_ShowAll();

			// edit mode
			void				_UpdateInspector();
			void				_ApplyInspector(const char* field,
									const char* value);
			bool				_ApplyPartField(bdb::Json& part,
									const BString& field, const char* value);
			bool				_ApplyStyleField(bdb::Json& part,
									const BString& field, const char* value);
			void				_ApplyTemplateField(const BString& field,
									const char* value);
			void				_ChangeKind(const char* kind);
			void				_TemplateEdited(BMessage* message);
			void				_SetCreateItem(int32 item);
			void				_EditImageDrop(BMessage* message);
			bool				_AddMediaFromFile(const entry_ref& ref,
									bool reuse, const char* partId,
									bdb::Json& outRef);
			void				_ChooseIcon();
			void				_ChooseMedia();
			void				_MediaChosen(BMessage* message);
			void				_PlaySound(const char* partId);
			void				_PlayVideo(const char* partId);
			bool				_ButtonMediaPath(const char* partId,
									std::string& path,
									std::string* name = NULL);
			void				_ChooseCardImageFile();
			void				_SaveCardImage(BMessage* message);
			void				_ViewImage(const char* partId);
			void				_IconChosen(BMessage* message);
			void				_ShowTemplateProperties();
			bool				_ApplyTheme(const std::string& templateId,
									const std::string& themeId);
			void				_ApplyThemeToAll(const std::string& themeId);
			void				_PaperDialog();
			void				_PaperChosen(BMessage* message);
			int32				_PartsOutside() const;

			// stack
			void				_Cleanup();
			void				_OpenLog();
			void				_ShowList();
			void				_SetZoom(float zoom, bool fit);
			void				_UpdateZoomMenus();
			void				_FitWindow();
			void				_SavePdf(uint32 what);
			void				_PdfPanelDone(BMessage* message);
			void				_ImportCsv();
			void				_FetchProgress(BMessage* message);
			void				_FetchResult(BMessage* message);

			void				_UpdateStatus();
			void				_UpdateTitle();
			void				_UpdateBanner();
			void				_NotifyCardChanged(const std::string& id);
			void				_NotifyCardsChanged();
			void				_ShowError(const char* format,
									const bdb::Status& status);
			void				_Notice(const char* text);
			void				_Hint(const char* text);

			StackDocument*		fDocument;
			CardSurface*		fCardView;
			CardFrame*			fCardFrame;
			ModeButton*			fModeButtons[3];
			BMenuField*			fZoomField;
			BannerView*			fBanner;
			BMenuBar*			fMenuBar;
			BStringView*		fPositionText;
			StatusLink*			fTemplateLink;
			BStringView*		fStatusText;
			BButton*			fShowAllButton;
			BMenu*				fTemplateMenu;
			BMenu*				fRecentMenu;

			Mode				fMode;
			std::string			fCurrentId;
			bool				fFindActive;
			std::vector<std::string> fFound;
			bool				fDirty;
			bool				fSaveFailed;
			bool				fTemplateDirty;
			BMessageRunner*		fAutosave;
			BString				fHint;

			ToolPalette*		fPalette;
			InspectorWindow*	fInspector;
			BFilePanel*			fImagePanel;
			BFilePanel*			fBackgroundPanel;
			BFilePanel*			fPdfPanel;
			BFilePanel*			fMediaPanel;
			BFilePanel*			fCardImagePanel;
			BFileGameSound*		fSound;
			std::string			fSoundPart;
			uint32				fPdfKind;
			BMessenger			fListWindow;
			BMessenger			fAuxWindows[4];
			ImageFetcher*		fFetcher;
			int32				fFetchDone;
			int32				fFetchTotal;
			int32				fFetchOk;
			int32				fFetchFailed;
};


#endif	// BDB_STACK_WINDOW_H
