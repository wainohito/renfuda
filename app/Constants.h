/*
 * BDB card database - RenkuOS application
 */
#ifndef BDB_CONSTANTS_H
#define BDB_CONSTANTS_H

#include <SupportDefs.h>


#define BDB_APP_SIGNATURE "application/x-vnd.renfuda-app"
#define BDB_VERSION "0.5.0"

#define BDB_MIME_MANIFEST "application/x-vnd.renfuda-manifest"
#define BDB_MIME_TEMPLATE "application/x-vnd.renfuda-template"
#define BDB_MIME_CARD "application/x-vnd.renfuda-data"
#define BDB_MIME_THEME "application/x-vnd.renfuda-theme"
#define BDB_MIME_PARTS "application/x-vnd.renfuda-parts"


enum {
	// application
	kMsgNewBlankStack = 'nBlk',
	kMsgShowPresets = 'nPrs',
	kMsgOpenPanel = 'oPnl',
	kMsgOpenPath = 'oPth',
	kMsgCreateStack = 'cStk',		// from the save panel
	kMsgStartWindowClosed = 'swCl',
	kMsgDocumentReleased = 'dRel',
	kMsgShowStart = 'sStr',
	kMsgLogFailed = 'lgFl',

	// document notifications (sent to every window of a stack)
	kMsgDocCardChanged = 'dCrd',	// "card": id, "source": window pointer
	kMsgDocCardsChanged = 'dCds',	// cards added / removed / reordered
	kMsgDocTemplateChanged = 'dTpl',	// "template": id
	kMsgDocStackChanged = 'dStk',	// title etc.

	kMsgNewStackCsv = 'nCsv',
	kMsgOpenSample = 'oSmp',		// "name"
	kMsgRemoveRecent = 'rRem',		// "path"
	kMsgPaperChosen = 'pChs',		// from the paper window
	kMsgCsvFinished = 'csvF',		// "manifest", "fetch" (bool)
	kMsgCsvWizardClosed = 'csvC',

	// stack window
	kMsgModeBrowse = 'mBrw',
	kMsgModeEdit = 'mEdt',
	kMsgModeFind = 'mFnd',
	kMsgFirstCard = 'cFst',
	kMsgPrevCard = 'cPrv',
	kMsgNextCard = 'cNxt',
	kMsgLastCard = 'cLst',
	kMsgGoToNumber = 'cNum',
	kMsgGoToCard = 'cGto',			// "card": id
	kMsgNewCard = 'cNew',
	kMsgDuplicateCard = 'cDup',
	kMsgDeleteCard = 'cDel',
	kMsgChangeTemplate = 'cTpl',	// "template": id
	kMsgRunFind = 'fRun',
	kMsgShowAll = 'fAll',
	kMsgShowList = 'vLst',
	kMsgZoomIn = 'vZin',
	kMsgZoomOut = 'vZot',
	kMsgZoomActual = 'vZ1x',
	kMsgZoomFit = 'vZft',
	kMsgZoomSet = 'vZst',			// "zoom": float (1.0 = 100%)
	kMsgCardSizeChanged = 'vCsz',	// the card's size on screen changed
	kMsgFitWindow = 'vFit',
	kMsgCleanup = 'sCln',
	kMsgProblems = 'sPrb',
	kMsgManageTemplates = 'sTpl',
	kMsgStackSettings = 'sSet',
	kMsgOpenLog = 'sLog',
	kMsgAbout = 'hAbt',
	kMsgPreferences = 'hPrf',
	kMsgVideoFailed = 'vdFl',		// from OpenVideo()
	kMsgImageView = 'iVew',		// "part": the image at its own size
	kMsgSaveCardImage = 'sCiS',	// "refs"/"name" from the save panel
	kMsgMediaChosen = 'iMdC',	// "refs" from the media panel, "part"
	kMsgUndo = 'eUnd',
	kMsgRedo = 'eRdo',
	kMsgCut = 'eCut',
	kMsgCopy = 'eCpy',
	kMsgPaste = 'ePst',
	kMsgSelectAll = 'eAll',
	kMsgDuplicatePart = 'eDpl',
	kMsgDeletePart = 'eDel',
	kMsgToggleSnap = 'eSnp',
	kMsgAutosave = 'aSav',
	kMsgClearFind = 'fClr',
	kMsgEndEdit = 'eEnd',
	kMsgToggleGrid = 'eGrd',
	kMsgBringToFront = 'eFrt',
	kMsgSendToBack = 'eBck',
	kMsgLock = 'eLck',
	kMsgUnlock = 'eUlk',
	kMsgLockAll = 'eLkA',
	kMsgUnlockAll = 'eUlA',
	kMsgChooseIcon = 'eIco',
	kMsgIconChosen = 'eIcC',		// "path", "size", "placement"
	kMsgSetLink = 'eLnk',
	kMsgTemplateProps = 'tPrp',
	kMsgPaperDialog = 'tPpr',
	kMsgBackgroundDialog = 'tBgd',
	kMsgThemeDialog = 'tThm',
	kMsgPartKind = 'ePKd',			// "kind" (context menu)
	kMsgImportCsv = 'sCsv',
	kMsgFetchImages = 'sFch',
	kMsgFetchProgress = 'sFpr',		// "done", "total", "ok", "failed"
	kMsgFetchResult = 'sFrs',		// one fetched image
	kMsgSaveCardPdf = 'pdfC',
	kMsgSaveSelectedPdf = 'pdfS',
	kMsgSaveListPdf = 'pdfL',
	kMsgSaveAllPdf = 'pdfA',
	kMsgPdfPanel = 'pdfP',			// save panel reply
	kMsgPdfDone = 'pdfD',			// "status", "kind", "pages", "bytes"
	kMsgHint = 'hint',				// "text": status bar hint
	kMsgStatusClicked = 'stCl',

	// card surface -> stack window
	kMsgButtonAction = 'bAct',		// "part", "action"
	kMsgBoolToggle = 'bTgl',		// "part"
	kMsgOpenLink = 'oLnk',			// "part"
	kMsgEditImageDrop = 'eImg',		// "refs", "point"
	kMsgPaletteDrag = 'pDrg',		// "item", "offset" (point in the item)
	kMsgPaletteTheme = 'pThm',		// "theme": the theme document
	kMsgPaletteSelect = 'pSel',		// "item" (-1: select tool)
	kMsgPartCreated = 'pCrt',		// "part", "kind"
	kMsgBackgroundRef = 'bgRf',
	kMsgBackgroundPanel = 'bgPn',
	kMsgIconFilePanel = 'icFp',

	// field views -> stack window ("part": id)
	kMsgFieldEdited = 'fEdt',
	kMsgFieldCommit = 'fCmt',
	kMsgFieldGuarded = 'fGrd',
	kMsgFieldEnter = 'fEnt',
	kMsgImageSet = 'iSet',			// "part", "refs"
	kMsgImageChoose = 'iCho',		// "part"
	kMsgImageClear = 'iClr',		// "part"
	kMsgImagePanelDone = 'iPnl',

	// edit mode
	kMsgToolSelect = 'tSel',
	kMsgToolFrame = 'tFrm',
	kMsgToolLabel = 'tLbl',
	kMsgSelectionChanged = 'eSel',
	kMsgTemplateEdited = 'eTpl',	// CardSurface changed the template
	kMsgPushUndo = 'ePsh',
	kMsgInspectorChanged = 'iChg',	// "field": name, "value"
	kMsgInspectorKind = 'iKnd',		// "kind"
	kMsgInspectorFit = 'iFit',		// "fit"

	// misc windows
	kMsgTemplateListSelect = 'tlSl',
	kMsgTemplateNew = 'tlNw',
	kMsgTemplateDuplicate = 'tlDp',
	kMsgTemplateDelete = 'tlDl',
	kMsgTemplateApply = 'tlAp',
	kMsgSettingsOK = 'stOK',
	kMsgPresetSelected = 'prSl',
	kMsgPresetCreate = 'prCr',
	kMsgRecentInvoked = 'rcIn',

	// list window
	kMsgListTemplate = 'lTpl',
	kMsgListSearch = 'lSrc',
	kMsgListColumnToggle = 'lCol'
};


enum Mode {
	kModeBrowse = 0,
	kModeEdit,
	kModeFind
};


// Items of the object palette (UI 6.2).
struct PalettePiece {
	const char*	kind;
	const char*	text;		// label / button text
	const char*	action;		// button action
	int32		x;			// relative to the drop point
	int32		y;
	int32		width;
	int32		height;
};

struct PaletteItem {
	int32			label;		// index into PaletteLabel()
	int32			count;
	PalettePiece	pieces[5];
};

static const PaletteItem kPaletteItems[] = {
	{ 0, 1, { { "blank", "", "", 0, 0, 120, 24 } } },
	{ 1, 1, { { "text", "", "", 0, 0, 160, 24 } } },
	{ 2, 1, { { "number", "", "", 0, 0, 96, 24 } } },
	{ 3, 1, { { "bool", "", "", 0, 0, 80, 24 } } },
	{ 4, 1, { { "image", "", "", 0, 0, 120, 120 } } },
	{ 5, 1, { { "label", "", "", 0, 0, 80, 20 } } },
	{ 6, 1, { { "button", "", "", 0, 0, 72, 28 } } },
	{ 7, 1, { { "button", "◀◀", "card.first", 0, 0, 60, 28 } } },
	{ 8, 1, { { "button", "◀", "card.prev", 0, 0, 60, 28 } } },
	{ 9, 1, { { "button", "▶", "card.next", 0, 0, 60, 28 } } },
	{ 10, 1, { { "button", "▶▶", "card.last", 0, 0, 60, 28 } } },
	{ 11, 5, { { "button", "◀◀", "card.first", 0, 0, 48, 28 },
		{ "button", "◀", "card.prev", 54, 0, 48, 28 },
		{ "counter", "", "", 108, 4, 72, 20 },
		{ "button", "▶", "card.next", 186, 0, 48, 28 },
		{ "button", "▶▶", "card.last", 240, 0, 48, 28 } } },
	{ 12, 1, { { "counter", "", "", 0, 0, 72, 20 } } },
	{ 13, 3, { { "button", "◀", "card.prev", 0, 0, 60, 28 },
		{ "counter", "", "", 68, 4, 72, 20 },
		{ "button", "▶", "card.next", 148, 0, 60, 28 } } }
};

static const int32 kPaletteItemCount
	= sizeof(kPaletteItems) / sizeof(kPaletteItems[0]);


#endif	// BDB_CONSTANTS_H
