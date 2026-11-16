/*
 * BDB card database - RenkuOS application
 * The CSV import wizard (I/O spec v0.1, 3.3): file, format, columns,
 * template, images, confirmation, progress, report. Importing runs in its
 * own thread; canceling a new stack leaves nothing behind.
 */
#ifndef BDB_CSV_WIZARD_H
#define BDB_CSV_WIZARD_H

#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <string>
#include <vector>

#include "Csv.h"


class BButton;
class BCardLayout;
class BCheckBox;
class BFilePanel;
class BGridLayout;
class BMenuField;
class BRadioButton;
class BStatusBar;
class BStringView;
class BTextControl;
class BTextView;
class PreviewTable;
class StackDocument;


class CsvWizard : public BWindow {
public:
	// `document` NULL: a new stack (3.1 "CSVから始める"); otherwise the
	// cards are added to that stack ("CSVを読み込む…").
								CsvWizard(StackDocument* document,
									BMessenger replyTo);
	virtual						~CsvWizard();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
	enum {
		kPageFile = 0,
		kPageFormat,
		kPageColumns,
		kPageTemplate,
		kPageImages,
		kPageConfirm,
		kPageProgress,
		kPageDone,
		kPageCount
	};

			BView*				_BuildFilePage();
			BView*				_BuildFormatPage();
			BView*				_BuildColumnsPage();
			BView*				_BuildTemplatePage();
			BView*				_BuildImagesPage();
			BView*				_BuildConfirmPage();
			BView*				_BuildProgressPage();
			BView*				_BuildDonePage();

			void				_ShowPage(int32 page);
			int32				_NextPage(int32 page, int32 direction) const;
			bool				_Leave(int32 page);
			void				_LoadPreview();
			void				_FillColumns();
			void				_ReadColumns();
			void				_FillMapping();
			void				_UpdateConfirm();
			bool				_HasImageUrls() const;
			void				_StartScan();
			void				_StartImport();
	static	status_t			_ScanThread(void* data);
	static	status_t			_ImportThread(void* data);
			void				_Import();

			StackDocument*		fDocument;
			BMessenger			fReplyTo;
			BCardLayout*		fCards;
			int32				fPage;
			BButton*			fBack;
			BButton*			fNext;
			BButton*			fCancel;
			BStringView*		fStepTitle;

			// file
			BString				fPath;
			BStringView*		fFileInfo;
			BFilePanel*			fOpenPanel;
			// format
			bdb::CsvOptions		fOptions;
			bdb::CsvEncoding	fDetected;
			BMenuField*			fEncodingField;
			BMenuField*			fDelimiterField;
			BCheckBox*			fHeader;
			BTextControl*		fSkip;
			PreviewTable*		fPreview;
			std::vector<std::string> fHeaderNames;
			std::vector<std::vector<std::string>> fSampleRows;
			// columns
			std::vector<bdb::CsvColumn> fColumns;
			BView*				fColumnsView;
			BGridLayout*		fColumnsGrid;
			// template
			BRadioButton*		fAutoTemplate;
			BRadioButton*		fExistingTemplate;
			BMenuField*			fTemplateField;
			BMenuField*			fPaperField;
			BMenuField*			fThemeField;
			BMenuField*			fTitleField;
			BView*				fMappingView;
			BGridLayout*		fMappingGrid;
			BView*				fUnusedMapping;
			std::string			fTemplateId;
			// images
			BRadioButton*		fFetchNow;
			BRadioButton*		fFetchLater;
			BStringView*		fImagesInfo;
			// confirm
			BTextView*			fConfirmText;
			BTextControl*		fStackName;
			BStringView*		fDestination;
			BString				fDestinationDir;
			BFilePanel*			fSavePanel;
			bdb::CsvScan		fScan;
			bdb::CsvLayoutOptions fLayout;
			BString				fTitle;
			bool				fScanDone;
			bool				fScanFailed;
			BString				fScanError;
			thread_id			fScanThread;
			// progress
			BStatusBar*			fProgress;
			BStringView*		fProgressText;
			volatile bool		fCancelImport;
			thread_id			fImportThread;
			bool				fImporting;
			// done
			BTextView*			fReportText;
			BButton*			fOpenStack;
			bdb::CsvImportReport fReport;
			BString				fManifestPath;
			bool				fFetchBackground;
};


#endif	// BDB_CSV_WIZARD_H
