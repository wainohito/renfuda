/*
 * BDB card database - RenkuOS application
 */
#include "CsvWizard.h"

#include <Alert.h>
#include <Application.h>
#include <Autolock.h>
#include <Beep.h>
#include <Button.h>
#include <CardLayout.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <Entry.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <GridLayout.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <RadioButton.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <StatusBar.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <sys/stat.h>

#include "BdbApp.h"
#include "Constants.h"
#include "ImageFetch.h"
#include "Limits.h"
#include "Model.h"
#include "Preset.h"
#include "Stack.h"
#include "StackDocument.h"
#include "Store.h"
#include "Strings.h"
#include "Text.h"


using bdb::Json;

static const uint32 kMsgBack = 'cwBk';
static const uint32 kMsgNextPage = 'cwNx';
static const uint32 kMsgChooseFile = 'cwCF';
static const uint32 kMsgFileChosen = 'cwFC';
static const uint32 kMsgFormatChanged = 'cwFm';
static const uint32 kMsgColumnChanged = 'cwCl';
static const uint32 kMsgTemplateMode = 'cwTm';
static const uint32 kMsgChooseDestination = 'cwCD';
static const uint32 kMsgDestinationChosen = 'cwDC';
static const uint32 kMsgScanDone = 'cwSD';
static const uint32 kMsgImportProgress = 'cwIP';
static const uint32 kMsgImportDone = 'cwID';
static const uint32 kMsgOpenStack = 'cwOS';
static const uint32 kMsgSaveReport = 'cwSR';
static const uint32 kMsgReportChosen = 'cwRC';
static const uint32 kMsgNoop = 'cwNo';

// Usages in the order of the menus (3.3).
static const bdb::CsvUsage kUsages[] = {
	bdb::CsvUsage::kIgnore, bdb::CsvUsage::kText, bdb::CsvUsage::kNumber,
	bdb::CsvUsage::kBool, bdb::CsvUsage::kImageUrl, bdb::CsvUsage::kImagePath,
	bdb::CsvUsage::kLinkUrl
};


static const char*
UsageLabel(bdb::CsvUsage usage)
{
	switch (usage) {
		case bdb::CsvUsage::kIgnore:	return str::kCsvIgnore;
		case bdb::CsvUsage::kText:		return str::kCsvText;
		case bdb::CsvUsage::kNumber:	return str::kCsvNumber;
		case bdb::CsvUsage::kBool:		return str::kCsvBool;
		case bdb::CsvUsage::kImageUrl:	return str::kCsvImageUrl;
		case bdb::CsvUsage::kImagePath:	return str::kCsvImagePath;
		case bdb::CsvUsage::kLinkUrl:	return str::kCsvLinkUrl;
	}
	return str::kCsvText;
}


static BString
SizeText(off_t size)
{
	char buffer[64];
	if (size >= 1024 * 1024)
		snprintf(buffer, sizeof(buffer), "%.1f MiB", size / 1048576.0);
	else if (size >= 1024)
		snprintf(buffer, sizeof(buffer), "%.1f KiB", size / 1024.0);
	else
		snprintf(buffer, sizeof(buffer), "%d B", (int)size);
	return buffer;
}


// Renames the "automatic" item (the first one). A marked item is marked
// again, so the field shows the new label too.
static void
SetAutoLabel(BMenuField* field, const char* label)
{
	BMenuItem* item = field->Menu()->ItemAt(0);
	item->SetLabel(label);
	if (item->IsMarked())
		item->SetMarked(true);
}


static BMenuField*
MenuField(const char* name, const char* label, BMenu* menu)
{
	return new BMenuField(name, label, menu);
}


static BString
MarkedValue(BMenuField* field, const char* key, const char* fallback = "")
{
	if (field == NULL)
		return fallback;
	BMenuItem* item = field->Menu()->FindMarked();
	if (item == NULL || item->Message() == NULL)
		return fallback;
	return item->Message()->GetString(key, fallback);
}


// The scroll range of a layout view whose content changed (BScrollView
// only updates it when it is resized).
static void
UpdateScrollRange(BView* target)
{
	BScrollBar* bar = target->ScrollBar(B_VERTICAL);
	if (bar == NULL)
		return;
	target->InvalidateLayout(true);
	BSize size = target->PreferredSize();
	float visible = target->Bounds().Height();
	float range = std::max(0.0f, size.height - visible);
	bar->SetRange(0, range);
	bar->SetProportion(size.height > 0 ? std::min(1.0f,
		visible / size.height) : 1.0f);
	bar->SetSteps(be_plain_font->Size() * 1.33f, visible);
}


// #pragma mark - PreviewTable


// The first rows of the file as a table (3.3, screen 2).
class PreviewTable : public BView {
public:
	PreviewTable()
		:
		BView("preview", B_WILL_DRAW | B_FRAME_EVENTS)
	{
		SetViewUIColor(B_LIST_BACKGROUND_COLOR);
		SetExplicitMinSize(BSize(400, 200));
	}

	void SetRows(const std::vector<std::string>& header,
		const std::vector<std::vector<std::string>>& rows)
	{
		fHeader = header;
		fRows.assign(rows.begin(), rows.begin()
			+ std::min(rows.size(), size_t(10)));
		_UpdateScrollBars();
		Invalidate();
	}

	virtual void Draw(BRect updateRect)
	{
		font_height height;
		GetFontHeight(&height);
		float row = ceilf(height.ascent + height.descent) + 6;
		float column = 140;
		size_t columns = fHeader.size();
		SetHighUIColor(B_PANEL_BACKGROUND_COLOR);
		FillRect(BRect(0, 0, columns * column, row - 1));
		for (size_t c = 0; c < columns; c++) {
			BString text = fHeader[c].c_str();
			TruncateString(&text, B_TRUNCATE_END, column - 8);
			SetHighUIColor(B_PANEL_TEXT_COLOR);
			DrawString(text, BPoint(c * column + 4, height.ascent + 3));
		}
		for (size_t r = 0; r < fRows.size(); r++) {
			float y = (r + 1) * row;
			for (size_t c = 0; c < fRows[r].size() && c < columns + 2; c++) {
				BString text = bdb::FirstLine(fRows[r][c]).c_str();
				TruncateString(&text, B_TRUNCATE_END, column - 8);
				SetHighUIColor(c < columns ? B_LIST_ITEM_TEXT_COLOR
					: B_FAILURE_COLOR);
				DrawString(text, BPoint(c * column + 4,
					y + height.ascent + 3));
			}
			SetHighUIColor(B_LIST_BACKGROUND_COLOR, B_DARKEN_1_TINT);
			StrokeLine(BPoint(0, y), BPoint(Bounds().right, y));
		}
		for (size_t c = 1; c <= columns; c++) {
			SetHighUIColor(B_LIST_BACKGROUND_COLOR, B_DARKEN_1_TINT);
			StrokeLine(BPoint(c * column - 1, 0),
				BPoint(c * column - 1, Bounds().bottom));
		}
	}

	virtual void FrameResized(float width, float height)
	{
		BView::FrameResized(width, height);
		_UpdateScrollBars();
	}

private:
	void _UpdateScrollBars()
	{
		if (BScrollBar* bar = ScrollBar(B_HORIZONTAL)) {
			float width = fHeader.size() * 140.0f;
			float range = std::max(0.0f, width - Bounds().Width());
			bar->SetRange(0, range);
			bar->SetProportion(width > 0 ? std::min(1.0f,
				Bounds().Width() / width) : 1.0f);
		}
	}

	std::vector<std::string>	fHeader;
	std::vector<std::vector<std::string>> fRows;
};


// #pragma mark - CsvWizard


CsvWizard::CsvWizard(StackDocument* document, BMessenger replyTo)
	:
	BWindow(BRect(0, 0, 720, 520), str::kCsvTitle, B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS),
	fDocument(document),
	fReplyTo(replyTo),
	fPage(kPageFile),
	fOpenPanel(NULL),
	fDetected(bdb::CsvEncoding::kUtf8),
	fColumnsView(NULL),
	fColumnsGrid(NULL),
	fMappingView(NULL),
	fMappingGrid(NULL),
	fUnusedMapping(NULL),
	fSavePanel(NULL),
	fScanDone(false),
	fScanFailed(false),
	fScanThread(-1),
	fCancelImport(false),
	fImportThread(-1),
	fImporting(false),
	fFetchBackground(false)
{
	bdb::CsvReader::SetLanguage(str::Language());
	fStepTitle = new BStringView("step", "");
	BFont bold(be_bold_font);
	fStepTitle->SetFont(&bold);
	fBack = new BButton("back", str::kBack, new BMessage(kMsgBack));
	fNext = new BButton("next", str::kNext, new BMessage(kMsgNextPage));
	fCancel = new BButton("cancel", str::kCancel,
		new BMessage(B_QUIT_REQUESTED));

	BView* pages = new BView("pages", 0);
	fCards = new BCardLayout();
	pages->SetLayout(fCards);
	// The window keeps the size it opens with: some pages would make it
	// as narrow as their widest control otherwise.
	pages->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	fStepTitle->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fCards->AddView(_BuildFilePage());
	fCards->AddView(_BuildFormatPage());
	fCards->AddView(_BuildColumnsPage());
	fCards->AddView(_BuildTemplatePage());
	fCards->AddView(_BuildImagesPage());
	fCards->AddView(_BuildConfirmPage());
	fCards->AddView(_BuildProgressPage());
	fCards->AddView(_BuildDonePage());

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fStepTitle)
		.Add(new BSeparatorView(B_HORIZONTAL))
		.Add(pages, 1)
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddGroup(B_HORIZONTAL)
			.Add(fCancel)
			.AddGlue()
			.Add(fBack)
			.Add(fNext)
		.End();
	SetDefaultButton(fNext);
	CenterOnScreen();
	_ShowPage(kPageFile);
}


CsvWizard::~CsvWizard()
{
	delete fOpenPanel;
	delete fSavePanel;
	delete fUnusedMapping;
}


// #pragma mark - pages


BView*
CsvWizard::_BuildFilePage()
{
	BView* page = new BView("file", 0);
	fFileInfo = new BStringView("info", str::kCsvNoFile);
	BButton* choose = new BButton("choose", str::kCsvChooseFile,
		new BMessage(kMsgChooseFile));
	BLayoutBuilder::Group<>(page, B_VERTICAL)
		.Add(choose)
		.Add(fFileInfo)
		.AddGlue();
	return page;
}


BView*
CsvWizard::_BuildFormatPage()
{
	BView* page = new BView("format", 0);
	BPopUpMenu* encodings = new BPopUpMenu("encoding");
	const struct {
		bdb::CsvEncoding	value;
		const char*			label;
	} kEncodings[] = {
		{ bdb::CsvEncoding::kAuto, str::kCsvAutoPlain },
		{ bdb::CsvEncoding::kUtf8, "UTF-8" },
		{ bdb::CsvEncoding::kShiftJis, "Shift_JIS (CP932)" },
		{ bdb::CsvEncoding::kEucJp, "EUC-JP" }
	};
	for (const auto& entry : kEncodings) {
		BMessage* message = new BMessage(kMsgFormatChanged);
		message->AddInt32("encoding", int32(entry.value));
		BMenuItem* item = new BMenuItem(entry.label, message);
		item->SetMarked(entry.value == bdb::CsvEncoding::kAuto);
		encodings->AddItem(item);
	}
	fEncodingField = MenuField("encoding", str::kCsvEncoding, encodings);

	BPopUpMenu* delimiters = new BPopUpMenu("delimiter");
	const struct {
		char		value;
		const char*	label;
	} kDelimiters[] = {
		{ 0, str::kCsvAutoPlain }, { ',', str::kCsvComma }, { '\t', str::kCsvTab },
		{ ';', str::kCsvSemicolon }
	};
	for (const auto& entry : kDelimiters) {
		BMessage* message = new BMessage(kMsgFormatChanged);
		message->AddInt32("delimiter", entry.value);
		BMenuItem* item = new BMenuItem(entry.label, message);
		item->SetMarked(entry.value == 0);
		delimiters->AddItem(item);
	}
	fDelimiterField = MenuField("delimiter", str::kCsvDelimiter, delimiters);
	fHeader = new BCheckBox("header", str::kCsvHeader,
		new BMessage(kMsgFormatChanged));
	fHeader->SetValue(B_CONTROL_ON);
	fSkip = new BTextControl("skip", str::kCsvSkip, "0",
		new BMessage(kMsgFormatChanged));
	fPreview = new PreviewTable();
	BScrollView* scroll = new BScrollView("scroll", fPreview, 0, true, false);

	BLayoutBuilder::Group<>(page, B_VERTICAL)
		.AddGrid()
			.AddMenuField(fEncodingField, 0, 0)
			.AddMenuField(fDelimiterField, 0, 1)
			.AddTextControl(fSkip, 0, 2)
		.End()
		.Add(fHeader)
		.Add(scroll, 1);
	return page;
}


BView*
CsvWizard::_BuildColumnsPage()
{
	BView* page = new BView("columns", 0);
	fColumnsGrid = new BGridLayout(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING);
	fColumnsView = new BView("column list", 0, fColumnsGrid);
	fColumnsView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fColumnsGrid->SetInsets(B_USE_SMALL_INSETS);
	BScrollView* scroll = new BScrollView("scroll", fColumnsView, 0, false,
		true, B_NO_BORDER);
	BLayoutBuilder::Group<>(page, B_VERTICAL)
		.Add(scroll, 1);
	return page;
}


BView*
CsvWizard::_BuildTemplatePage()
{
	BView* page = new BView("template", 0);
	fAutoTemplate = new BRadioButton("auto", str::kCsvTemplateAuto,
		new BMessage(kMsgTemplateMode));
	fExistingTemplate = new BRadioButton("existing",
		str::kCsvTemplateExisting, new BMessage(kMsgTemplateMode));
	fAutoTemplate->SetValue(B_CONTROL_ON);

	// The menu's name is the field's label while it has no items.
	BPopUpMenu* templates = new BPopUpMenu(str::kNoTemplates);
	if (fDocument != NULL) {
		DocLocker lock(fDocument);
		for (bdb::Template* tmpl : fDocument->GetStack().Templates()) {
			BMessage* message = new BMessage(kMsgTemplateMode);
			message->AddString("template", tmpl->Id().c_str());
			BMenuItem* item = new BMenuItem(tmpl->Title().c_str(), message);
			templates->AddItem(item);
			if (templates->CountItems() == 1)
				item->SetMarked(true);
		}
	}
	fTemplateField = MenuField("template", str::kListTemplate, templates);

	// Paper and orientation (UI 3.4) for the automatic template.
	BPopUpMenu* papers = new BPopUpMenu("paper");
	auto addPaper = [papers](const char* size, const char* orientation,
			const char* label, bool marked) {
		BMessage* message = new BMessage(kMsgNoop);
		message->AddString("size", size);
		message->AddString("orientation", orientation);
		BMenuItem* item = new BMenuItem(label, message);
		item->SetMarked(marked);
		papers->AddItem(item);
	};
	addPaper("", "landscape", str::kPaperScreen, true);
	size_t count;
	const bdb::PaperInfo* sizes = bdb::PaperSizes(&count);
	for (size_t i = 0; i < count; i++) {
		for (const char* orientation : { "portrait", "landscape" }) {
			BString label = BString(sizes[i].size) == "postcard"
				? str::kPaperPostcard.Get() : sizes[i].size;
			label << " " << (BString(orientation) == "portrait"
				? str::kPortrait.Get() : str::kLandscape.Get());
			addPaper(sizes[i].size, orientation, label, false);
		}
	}
	fPaperField = MenuField("paper", str::kPaperSize, papers);

	BPopUpMenu* themes = new BPopUpMenu("themes");
	BdbApp* app = dynamic_cast<BdbApp*>(be_app);
	if (app != NULL) {
		for (const bdb::ThemeInfo& info : app->Themes().List(NULL)) {
			BMessage* message = new BMessage(kMsgNoop);
			message->AddString("theme", info.id.c_str());
			BMenuItem* item = new BMenuItem(str::gEnglish
				? info.nameEn.c_str() : info.nameJa.c_str(), message);
			item->SetMarked(info.id == "beos");
			themes->AddItem(item);
		}
	}
	fThemeField = MenuField("theme", str::kTemplateTheme, themes);
	fTitleField = MenuField("title", str::kCsvTitleColumn,
		new BPopUpMenu("title"));

	fMappingGrid = new BGridLayout(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING);
	fMappingView = new BView("mapping", 0, fMappingGrid);
	fMappingView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	BScrollView* scroll = new BScrollView("scroll", fMappingView, 0, false,
		true, B_NO_BORDER);

	BGroupLayout* existing = BLayoutBuilder::Group<>(B_VERTICAL)
		.SetInsets(20, 0, 0, 0)
		.Add(fTemplateField);
	if (fDocument != NULL)
		existing->AddView(scroll, 1);
	else {
		// A new stack can only get an automatic template (3.3), so there
		// is nothing to map.
		fExistingTemplate->SetEnabled(false);
		fTemplateField->SetEnabled(false);
		existing->AddItem(BSpaceLayoutItem::CreateGlue());
		fUnusedMapping = scroll;
	}
	BLayoutBuilder::Group<>(page, B_VERTICAL)
		.Add(fAutoTemplate)
		.AddGrid()
			.SetInsets(20, 0, 0, 0)
			.AddMenuField(fPaperField, 0, 0)
			.AddMenuField(fThemeField, 0, 1)
			.AddMenuField(fTitleField, 0, 2)
		.End()
		.Add(fExistingTemplate)
		.Add(existing);
	return page;
}


BView*
CsvWizard::_BuildImagesPage()
{
	BView* page = new BView("images", 0);
	fFetchNow = new BRadioButton("now", str::kCsvFetchNow, NULL);
	fFetchLater = new BRadioButton("later", str::kCsvFetchBackground, NULL);
	fFetchNow->SetValue(B_CONTROL_ON);
	fImagesInfo = new BStringView("info", "");
	BTextView* notice = new BTextView("notice");
	notice->SetText(str::kCsvFetchNotice);
	notice->MakeEditable(false);
	notice->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	notice->SetExplicitMinSize(BSize(300, 60));
	BLayoutBuilder::Group<>(page, B_VERTICAL)
		.Add(new BStringView(NULL, str::kCsvFetchWhen))
		.Add(fFetchNow)
		.Add(fFetchLater)
		.Add(fImagesInfo)
		.Add(notice)
		.AddGlue();
	return page;
}


BView*
CsvWizard::_BuildConfirmPage()
{
	BView* page = new BView("confirm", 0);
	fConfirmText = new BTextView("summary");
	fConfirmText->MakeEditable(false);
	fConfirmText->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fConfirmText->SetWordWrap(true);
	fStackName = new BTextControl("name", str::kCsvStackName, "", NULL);
	fDestination = new BStringView("destination", "");
	BButton* change = new BButton("change", str::kChoose,
		new BMessage(kMsgChooseDestination));
	BPath documents;
	if (find_directory(B_USER_DIRECTORY, &documents) == B_OK)
		documents.Append("Documents");
	fDestinationDir = documents.Path();
	fDestination->SetText(fDestinationDir);

	BLayoutBuilder::Group<> layout(page, B_VERTICAL);
	layout.Add(new BScrollView("scroll", fConfirmText, 0, false, true), 1);
	if (fDocument == NULL) {
		layout.Add(fStackName)
			.AddGroup(B_HORIZONTAL)
				.Add(new BStringView(NULL, str::kCsvDestination))
				.Add(fDestination, 1)
				.Add(change)
			.End();
	}
	return page;
}


BView*
CsvWizard::_BuildProgressPage()
{
	BView* page = new BView("progress", 0);
	fProgress = new BStatusBar("bar", "");
	fProgressText = new BStringView("text", "");
	BLayoutBuilder::Group<>(page, B_VERTICAL)
		.Add(fProgress)
		.Add(fProgressText)
		.AddGlue();
	return page;
}


BView*
CsvWizard::_BuildDonePage()
{
	BView* page = new BView("done", 0);
	fReportText = new BTextView("report");
	fReportText->MakeEditable(false);
	fOpenStack = new BButton("open", str::kCsvOpenStack,
		new BMessage(kMsgOpenStack));
	BButton* save = new BButton("save", str::kCsvSaveReport,
		new BMessage(kMsgSaveReport));
	BLayoutBuilder::Group<>(page, B_VERTICAL)
		.Add(new BScrollView("scroll", fReportText, 0, false, true), 1)
		.AddGroup(B_HORIZONTAL)
			.Add(save)
			.AddGlue()
			.Add(fOpenStack)
		.End();
	return page;
}


// #pragma mark - navigation


bool
CsvWizard::_HasImageUrls() const
{
	for (const bdb::CsvColumn& column : fColumns) {
		if (column.usage == bdb::CsvUsage::kImageUrl)
			return true;
	}
	return false;
}


int32
CsvWizard::_NextPage(int32 page, int32 direction) const
{
	int32 next = page + direction;
	// The image page only when there are image columns (3.3).
	if (next == kPageImages && !_HasImageUrls())
		next += direction;
	return std::max(int32(kPageFile), std::min(int32(kPageDone), next));
}


void
CsvWizard::_ShowPage(int32 page)
{
	fPage = page;
	fCards->SetVisibleItem(page);
	static const str::Text* const kTitles[] = {
		&str::kCsvStepFile, &str::kCsvStepFormat, &str::kCsvStepColumns,
		&str::kCsvStepTemplate, &str::kCsvStepImages, &str::kCsvStepConfirm,
		&str::kCsvStepProgress, &str::kCsvStepDone
	};
	fStepTitle->SetText(*kTitles[page]);
	bool working = page == kPageProgress;
	bool done = page == kPageDone;
	fBack->SetEnabled(page > kPageFile && !working && !done);
	fNext->SetEnabled((page != kPageFile || !fPath.IsEmpty()) && !working
		&& !done);
	fNext->SetLabel(page == kPageConfirm ? str::kCsvImport.Get()
		: str::kNext.Get());
	fCancel->SetLabel(done ? str::kDone.Get() : str::kCancel.Get());
	if (page == kPageConfirm)
		_UpdateConfirm();
	if (page == kPageImages) {
		// Up to 500 rows while importing; more in the background (3.6).
		size_t rows = fScanDone ? fScan.rows : fSampleRows.size();
		bool background = rows > 500 || fSampleRows.size() >= bdb::kCsvInferRows;
		if (background)
			fFetchLater->SetValue(B_CONTROL_ON);
		else
			fFetchNow->SetValue(B_CONTROL_ON);
	}
}


bool
CsvWizard::_Leave(int32 page)
{
	switch (page) {
		case kPageFile:
			if (fPath.IsEmpty())
				return false;
			_LoadPreview();
			return true;
		case kPageFormat:
			if (fHeaderNames.empty()) {
				beep();
				return false;
			}
			fColumns = bdb::GuessColumns(fHeaderNames, fSampleRows,
				str::Language());
			_FillColumns();
			_StartScan();
			return true;
		case kPageColumns:
		{
			_ReadColumns();
			// The title menu of the template page.
			BMenu* menu = fTitleField->Menu();
			menu->RemoveItems(0, menu->CountItems(), true);
			bool first = true;
			for (size_t i = 0; i < fColumns.size(); i++) {
				if (fColumns[i].usage != bdb::CsvUsage::kText)
					continue;
				BMessage* message = new BMessage(kMsgNoop);
				message->AddInt32("column", int32(i));
				BMenuItem* item = new BMenuItem(fColumns[i].name.c_str(),
					message);
				item->SetMarked(first);
				first = false;
				menu->AddItem(item);
			}
			_FillMapping();
			return true;
		}
		default:
			return true;
	}
}


void
CsvWizard::_LoadPreview()
{
	fOptions.header = fHeader->Value() == B_CONTROL_ON;
	fOptions.skipLines = std::max(0, atoi(fSkip->Text()));
	BString encoding = "";
	{
		BMenuItem* item = fEncodingField->Menu()->FindMarked();
		fOptions.encoding = item != NULL && item->Message() != NULL
			? bdb::CsvEncoding(item->Message()->GetInt32("encoding", 0))
			: bdb::CsvEncoding::kAuto;
		item = fDelimiterField->Menu()->FindMarked();
		fOptions.delimiter = item != NULL && item->Message() != NULL
			? char(item->Message()->GetInt32("delimiter", 0)) : 0;
	}
	bdb::CsvReader reader;
	bdb::Status status = reader.Open(fPath.String(), fOptions);
	fHeaderNames.clear();
	fSampleRows.clear();
	if (!status) {
		BString text;
		text.SetToFormat(str::kCsvTooLarge.Get(), status.Code());
		(new BAlert(str::kAppName, text, str::kOK))->Go(NULL);
		fPreview->SetRows(fHeaderNames, fSampleRows);
		return;
	}
	fDetected = reader.Encoding();
	fHeaderNames = reader.Header();
	bdb::CsvRecord record;
	while (fSampleRows.size() < bdb::kCsvInferRows
		&& reader.Next(record, status)) {
		fSampleRows.push_back(record.fields);
	}
	// Show what automatic chose.
	BString autoLabel;
	const char* detected = fDetected == bdb::CsvEncoding::kShiftJis
		? "Shift_JIS" : fDetected == bdb::CsvEncoding::kEucJp ? "EUC-JP"
		: "UTF-8";
	autoLabel.SetToFormat(str::kCsvAuto.Get(), detected);
	SetAutoLabel(fEncodingField, autoLabel);
	const char* delimiter = reader.Delimiter() == '\t' ? str::kCsvTab.Get()
		: reader.Delimiter() == ';' ? str::kCsvSemicolon.Get()
		: str::kCsvComma.Get();
	autoLabel.SetToFormat(str::kCsvAuto.Get(), delimiter);
	SetAutoLabel(fDelimiterField, autoLabel);
	fPreview->SetRows(fHeaderNames, fSampleRows);
}


// One row per column: name, usage, the first value and the options.
void
CsvWizard::_FillColumns()
{
	while (BLayoutItem* item = static_cast<BLayout*>(fColumnsGrid)->ItemAt(0)) {
		BView* view = item->View();
		fColumnsGrid->RemoveItem(item);
		delete item;
		if (view != NULL) {
			view->RemoveSelf();
			delete view;
		}
	}
	int32 row = 0;
	const char* headers[] = { str::kCsvColumn, str::kCsvUsage,
		str::kFieldName, str::kCsvSample };
	for (int32 i = 0; i < 4; i++) {
		BStringView* label = new BStringView(NULL, headers[i]);
		BFont bold(be_bold_font);
		label->SetFont(&bold);
		fColumnsGrid->AddView(label, i, row);
	}
	row++;
	for (size_t i = 0; i < fColumns.size(); i++) {
		const bdb::CsvColumn& column = fColumns[i];
		BString prefix;
		prefix << "c" << int32(i) << ":";
		fColumnsGrid->AddView(new BStringView(NULL, column.header.c_str()), 0,
			row);
		BPopUpMenu* usages = new BPopUpMenu("usage");
		for (bdb::CsvUsage usage : kUsages) {
			BMessage* message = new BMessage(kMsgColumnChanged);
			message->AddInt32("usage", int32(usage));
			BMenuItem* item = new BMenuItem(UsageLabel(usage), message);
			item->SetMarked(usage == column.usage);
			usages->AddItem(item);
		}
		BMenuField* usage = new BMenuField(BString(prefix) << "usage", NULL,
			usages);
		fColumnsGrid->AddView(usage, 1, row);
		BTextControl* name = new BTextControl(BString(prefix) << "name", NULL,
			column.name.c_str(), NULL);
		fColumnsGrid->AddView(name, 2, row);
		BString sample;
		for (const auto& values : fSampleRows) {
			if (i < values.size() && !bdb::TrimSpaces(values[i]).empty()) {
				sample = bdb::FirstLine(values[i]).c_str();
				break;
			}
		}
		BStringView* sampleView = new BStringView(NULL, sample);
		sampleView->SetExplicitMaxSize(BSize(160, B_SIZE_UNSET));
		sampleView->SetTruncation(B_TRUNCATE_END);
		fColumnsGrid->AddView(sampleView, 3, row);
		row++;
		// Options of the column, in two lines: text and image options,
		// then link options (one line is wider than the window).
		BGroupLayout* optionLines = new BGroupLayout(B_VERTICAL, 0);
		BView* optionView = new BView(NULL, 0, optionLines);
		BGroupLayout* options = new BGroupLayout(B_HORIZONTAL,
			B_USE_SMALL_SPACING);
		BGroupLayout* linkOptions = new BGroupLayout(B_HORIZONTAL,
			B_USE_SMALL_SPACING);
		optionLines->AddItem(options);
		optionLines->AddItem(linkOptions);
		BCheckBox* multiline = new BCheckBox(BString(prefix) << "multiline",
			str::kCsvMultiline, NULL);
		multiline->SetValue(column.multiline);
		options->AddView(multiline);
		BCheckBox* keepUrl = new BCheckBox(BString(prefix) << "keep",
			str::kCsvKeepUrl, NULL);
		keepUrl->SetValue(column.keepUrl);
		options->AddView(keepUrl);
		BPopUpMenu* edges = new BPopUpMenu("edge");
		for (int edge : { 128, 256, 512 }) {
			BMessage* message = new BMessage(kMsgNoop);
			message->AddInt32("edge", edge);
			BString label;
			label << edge;
			BMenuItem* item = new BMenuItem(label, message);
			item->SetMarked(edge == column.maxEdge);
			edges->AddItem(item);
		}
		options->AddView(new BMenuField(BString(prefix) << "edge",
			str::kCsvThumbSize, edges));
		BCheckBox* button = new BCheckBox(BString(prefix) << "button",
			str::kCsvMakeButton, NULL);
		button->SetValue(column.makeButton);
		linkOptions->AddView(button);
		linkOptions->AddView(new BTextControl(BString(prefix)
			<< "buttontext", NULL, column.buttonText.c_str(), NULL));
		BCheckBox* linkImage = new BCheckBox(BString(prefix) << "linkimage",
			str::kCsvLinkImage, NULL);
		linkImage->SetValue(column.linkImage);
		linkOptions->AddView(linkImage);
		options->AddItem(BSpaceLayoutItem::CreateGlue());
		linkOptions->AddItem(BSpaceLayoutItem::CreateGlue());
		fColumnsGrid->AddView(optionView, 1, row, 3);
		row++;
		// Show only the options of the usage.
		bool text = column.usage == bdb::CsvUsage::kText;
		bool imageUrl = column.usage == bdb::CsvUsage::kImageUrl;
		bool image = imageUrl || column.usage == bdb::CsvUsage::kImagePath;
		bool link = column.usage == bdb::CsvUsage::kLinkUrl;
		multiline->SetEnabled(text);
		keepUrl->SetEnabled(imageUrl);
		static_cast<BMenuField*>(optionView->FindView(BString(prefix)
			<< "edge"))->SetEnabled(image);
		button->SetEnabled(link);
		static_cast<BTextControl*>(optionView->FindView(BString(prefix)
			<< "buttontext"))->SetEnabled(link);
		linkImage->SetEnabled(link);
		fColumnsGrid->AddView(new BSeparatorView(B_HORIZONTAL), 0, row++, 4);
	}
	fColumnsGrid->AddItem(BSpaceLayoutItem::CreateGlue(), 0, row, 4);
	// The list scrolls only vertically: keep the window as wide as it.
	BView* scroll = fColumnsView->Parent();
	if (scroll != NULL) {
		scroll->SetExplicitMinSize(BSize(fColumnsView->MinSize().width
			+ be_control_look->GetScrollBarWidth(B_VERTICAL) + 2,
			B_SIZE_UNSET));
	}
	UpdateScrollRange(fColumnsView);
	// Menus report to the window.
	for (size_t i = 0; i < fColumns.size(); i++) {
		BString name;
		name << "c" << int32(i) << ":usage";
		if (BMenuField* field = dynamic_cast<BMenuField*>(
				fColumnsView->FindView(name))) {
			field->Menu()->SetTargetForItems(this);
		}
	}
}


void
CsvWizard::_ReadColumns()
{
	for (size_t i = 0; i < fColumns.size(); i++) {
		bdb::CsvColumn& column = fColumns[i];
		BString prefix;
		prefix << "c" << int32(i) << ":";
		if (BMenuField* field = dynamic_cast<BMenuField*>(
				fColumnsView->FindView(BString(prefix) << "usage"))) {
			BMenuItem* item = field->Menu()->FindMarked();
			if (item != NULL && item->Message() != NULL)
				column.usage = bdb::CsvUsage(item->Message()->GetInt32(
					"usage", 1));
		}
		if (BTextControl* name = dynamic_cast<BTextControl*>(
				fColumnsView->FindView(BString(prefix) << "name"))) {
			column.name = bdb::TrimSpaces(name->Text());
			if (column.name.empty())
				column.name = column.header;
			if (bdb::CountCodePoints(column.name) > bdb::kMaxPartNameChars)
				column.name = column.name.substr(0, 60);
		}
		auto checked = [&](const char* key) {
			BCheckBox* box = dynamic_cast<BCheckBox*>(
				fColumnsView->FindView(BString(prefix) << key));
			return box != NULL && box->Value() == B_CONTROL_ON;
		};
		column.multiline = checked("multiline");
		column.keepUrl = checked("keep");
		column.makeButton = checked("button");
		column.linkImage = checked("linkimage");
		if (BMenuField* field = dynamic_cast<BMenuField*>(
				fColumnsView->FindView(BString(prefix) << "edge"))) {
			BMenuItem* item = field->Menu()->FindMarked();
			if (item != NULL && item->Message() != NULL)
				column.maxEdge = item->Message()->GetInt32("edge", 256);
		}
		if (BTextControl* text = dynamic_cast<BTextControl*>(
				fColumnsView->FindView(BString(prefix) << "buttontext"))) {
			column.buttonText = text->Text();
		}
	}
}


// Existing template: which part each column fills (default: the part
// with the column's name).
void
CsvWizard::_FillMapping()
{
	while (BLayoutItem* item = static_cast<BLayout*>(fMappingGrid)->ItemAt(0)) {
		BView* view = item->View();
		fMappingGrid->RemoveItem(item);
		delete item;
		if (view != NULL) {
			view->RemoveSelf();
			delete view;
		}
	}
	if (fDocument == NULL)
		return;
	std::string templateId = MarkedValue(fTemplateField, "template").String();
	DocLocker lock(fDocument);
	const bdb::Template* tmpl = fDocument->GetStack().FindTemplate(templateId);
	if (tmpl == NULL)
		return;
	int32 row = 0;
	for (size_t i = 0; i < fColumns.size(); i++) {
		fMappingGrid->AddView(new BStringView(NULL,
			fColumns[i].header.c_str()), 0, row);
		BPopUpMenu* parts = new BPopUpMenu("parts");
		BMessage* none = new BMessage(kMsgNoop);
		none->AddString("part", "");
		parts->AddItem(new BMenuItem(str::kCsvIgnore, none));
		bool marked = false;
		for (const Json& part : tmpl->Parts()) {
			if (!bdb::KindHasValue(bdb::part::Kind(part)))
				continue;
			BMessage* message = new BMessage(kMsgNoop);
			message->AddString("part", bdb::part::Id(part).c_str());
			BMenuItem* item = new BMenuItem(bdb::part::Name(part).c_str(),
				message);
			if (!marked && bdb::part::Name(part) == fColumns[i].name) {
				item->SetMarked(true);
				marked = true;
			}
			parts->AddItem(item);
		}
		if (!marked)
			parts->ItemAt(0)->SetMarked(true);
		BString name;
		name << "m" << int32(i);
		fMappingGrid->AddView(new BMenuField(name, str::kCsvTargetPart, parts),
			1, row++);
	}
	fMappingGrid->AddItem(BSpaceLayoutItem::CreateGlue(), 0, row, 2);
	UpdateScrollRange(fMappingView);
}


void
CsvWizard::_UpdateConfirm()
{
	BString fields;
	for (const bdb::CsvColumn& column : fColumns) {
		if (column.usage == bdb::CsvUsage::kIgnore)
			continue;
		if (!fields.IsEmpty())
			fields << (str::gEnglish ? ", " : "、");
		fields << column.name.c_str() << "(" << UsageLabel(column.usage)
			<< ")";
	}
	BString text;
	if (!fScanDone)
		text = str::kCsvScanning.Get();
	else if (fScanFailed) {
		text.SetToFormat(str::kCsvTooLarge.Get(), fScanError.String());
	} else {
		text.SetToFormat(str::kCsvConfirmText.Get(), (int)fScan.rows,
			(int)fScan.mismatched, fields.String());
	}
	fConfirmText->SetText(text);
	fNext->SetEnabled(fScanDone && !fScanFailed);
	if (fDocument == NULL && BString(fStackName->Text()).IsEmpty()) {
		BString name = BPath(fPath.String()).Leaf();
		int32 dot = name.FindLast('.');
		if (dot > 0)
			name.Truncate(dot);
		fStackName->SetText(name);
	}
}


// #pragma mark - scanning and importing


void
CsvWizard::_StartScan()
{
	if (fScanThread >= 0) {
		status_t result;
		wait_for_thread(fScanThread, &result);
	}
	fScanDone = false;
	fScanFailed = false;
	fScanThread = spawn_thread(&_ScanThread, "bdb csv scan",
		B_NORMAL_PRIORITY, this);
	if (fScanThread >= 0)
		resume_thread(fScanThread);
}


status_t
CsvWizard::_ScanThread(void* data)
{
	CsvWizard* self = static_cast<CsvWizard*>(data);
	bdb::CsvScan scan;
	bdb::Status status = bdb::ScanCsv(self->fPath.String(), self->fOptions,
		scan);
	BMessage message(kMsgScanDone);
	message.AddInt64("rows", int64(scan.rows));
	message.AddInt64("mismatched", int64(scan.mismatched));
	message.AddString("status", status ? "ok" : status.Code());
	message.AddString("detail", scan.tooManyRows ? "rows"
		: scan.fieldTooLarge ? "field" : "");
	BMessenger(self).SendMessage(&message);
	return B_OK;
}


void
CsvWizard::_StartImport()
{
	_ReadColumns();
	if (fDocument != NULL && fExistingTemplate->Value() == B_CONTROL_ON) {
		fTemplateId = MarkedValue(fTemplateField, "template").String();
		for (size_t i = 0; i < fColumns.size(); i++) {
			BString name;
			name << "m" << int32(i);
			BMenuField* field = dynamic_cast<BMenuField*>(
				fMappingView->FindView(name));
			fColumns[i].targetPart = MarkedValue(field, "part").String();
			if (fColumns[i].targetPart.empty())
				fColumns[i].usage = bdb::CsvUsage::kIgnore;
		}
	} else {
		fTemplateId.clear();
		for (bdb::CsvColumn& column : fColumns)
			column.targetPart.clear();
	}
	fFetchBackground = _HasImageUrls()
		&& fFetchLater->Value() == B_CONTROL_ON;

	// Everything the import thread needs from the controls.
	BdbApp* app = dynamic_cast<BdbApp*>(be_app);
	fLayout = bdb::CsvLayoutOptions();
	BMenuItem* paper = fPaperField->Menu()->FindMarked();
	fLayout.paperSize = paper != NULL && paper->Message() != NULL
		? paper->Message()->GetString("size", "") : "";
	fLayout.orientation = paper != NULL && paper->Message() != NULL
		? paper->Message()->GetString("orientation", "portrait")
		: "portrait";
	if (!fLayout.paperSize.empty()) {
		bdb::PaperPixels(fLayout.paperSize, fLayout.orientation,
			fLayout.width, fLayout.height);
	}
	fLayout.themeId = MarkedValue(fThemeField, "theme", "beos").String();
	BMenuItem* title = fTitleField->Menu()->FindMarked();
	fLayout.titleColumn = title != NULL && title->Message() != NULL
		? title->Message()->GetInt32("column", -1) : -1;
	fLayout.lang = str::Language();
	bdb::Theme theme;
	if (app != NULL && app->Themes().Find(fLayout.themeId, NULL, theme)) {
		fLayout.baseFontSize = theme.FontSize();
		fLayout.smallTextColor = bdb::ColorToString(
			theme.GetColor("text_disabled"));
	}
	fTitle = fDocument == NULL ? BString(fStackName->Text())
		: BString(BPath(fPath.String()).Leaf());
	fLayout.title = fTitle.String();
	fCancelImport = false;
	fImporting = true;
	_ShowPage(kPageProgress);
	fImportThread = spawn_thread(&_ImportThread, "bdb csv import",
		B_NORMAL_PRIORITY, this);
	if (fImportThread >= 0)
		resume_thread(fImportThread);
}


status_t
CsvWizard::_ImportThread(void* data)
{
	static_cast<CsvWizard*>(data)->_Import();
	return B_OK;
}


// Runs in the import thread. Only reads the settings gathered on the
// pages; reports through messages.
void
CsvWizard::_Import()
{
	BdbApp* app = dynamic_cast<BdbApp*>(be_app);
	bdb::LogSink* sink = app != NULL ? app->LogSink() : NULL;
	bdb::CsvImportReport report;
	BString error;
	BString manifestPath;
	std::unique_ptr<bdb::Stack> ownStack;
	bdb::Stack* stack = NULL;
	std::string staging;
	std::string dest;
	std::string templateId = fTemplateId;
	std::vector<bdb::CsvColumnParts> parts(fColumns.size());
	AppImageSource images(&fCancelImport);

	auto progress = [this](const bdb::CsvImportReport& current) {
		if (current.cards % 25 == 0) {
			BMessage message(kMsgImportProgress);
			message.AddInt32("cards", int32(current.cards));
			message.AddInt32("rows", int32(fScan.rows));
			BMessenger(this).SendMessage(&message);
			if (fDocument != NULL) {
				// Let the stack's windows draw in between (2.2).
				fDocument->Unlock();
				snooze(1000);
				fDocument->Lock();
			}
		}
		return !fCancelImport;
	};

	bdb::Status status;
	if (templateId.empty()) {
		// The automatic template (3.5), with the settings captured by
		// _StartImport() on the window thread.
		bdb::CsvLayoutOptions layout = fLayout;
		BString name = fTitle;
		Json templateDoc;
		status = bdb::BuildCsvTemplate(fColumns, layout, templateDoc, parts);
		if (status && fDocument == NULL) {
			BString folder = name;
			folder << ".rfdf";
			dest = BPath(fDestinationDir.String(), folder.String()).Path();
			std::string manifestName;
			status = bdb::CreateCsvStack(dest, name.String(), templateDoc,
				sink, staging, manifestName);
			if (status) {
				status = bdb::Stack::Open(staging + "/" + manifestName, sink,
					ownStack, app != NULL ? &app->Themes() : NULL);
			}
			if (status) {
				stack = ownStack.get();
				templateId = templateDoc.GetString("template_id");
				if (app != NULL && layout.themeId != "beos") {
					app->Themes().CopyToStack(layout.themeId, stack->Store(),
						true);
				}
				manifestPath = (dest + "/" + manifestName).c_str();
			}
		} else if (status) {
			fDocument->Lock();
			stack = &fDocument->GetStack();
			status = stack->AddTemplate(templateDoc, &templateId);
			if (status && app != NULL && layout.themeId != "beos") {
				app->Themes().CopyToStack(layout.themeId, stack->Store(),
					true);
			}
			fDocument->Unlock();
		}
	}
	if (status && stack == NULL && fDocument != NULL) {
		stack = &fDocument->GetStack();
	}

	if (status) {
		bdb::CsvReader reader;
		status = reader.Open(fPath.String(), fOptions);
		if (status) {
			if (fDocument != NULL)
				fDocument->Lock();
			status = bdb::ImportCsvRows(*stack, templateId, reader, fColumns,
				parts, &images, progress, report);
			if (fDocument != NULL)
				fDocument->Unlock();
		}
	}

	// Images now, unless they are fetched in the background (3.6).
	if (status && !report.canceled && !fFetchBackground) {
		std::vector<bdb::FetchItem> items;
		if (fDocument != NULL)
			fDocument->Lock();
		items = bdb::FindImagesToFetch(*stack);
		if (fDocument != NULL)
			fDocument->Unlock();
		int32 done = 0;
		int32 total = int32(items.size());
		FetchItems(items, &fCancelImport, [&](const bdb::FetchItem& item,
				bool ok, const std::string& bytes, const std::string& ext,
				const std::string& reason) {
			done++;
			if (fDocument != NULL)
				fDocument->Lock();
			if (ok) {
				bdb::StringInStream stream(bytes);
				ok = stack->SetMedia(item.cardId, item.partId, "image", ext,
					stream).IsOk();
			}
			if (ok)
				report.imagesOk++;
			else {
				report.imagesFailed++;
				report.imageFailures[reason.empty() ? "write_failed"
					: reason]++;
				stack->Log().Warn("image.fetch.failed", {{"card",
					item.cardId}, {"part", item.partId}, {"reason",
					reason.empty() ? "write_failed" : reason}});
			}
			if (fDocument != NULL)
				fDocument->Unlock();
			BMessage message(kMsgImportProgress);
			message.AddInt32("cards", int32(report.cards));
			message.AddInt32("rows", int32(fScan.rows));
			message.AddInt32("images", done);
			message.AddInt32("imagesTotal", total);
			BMessenger(this).SendMessage(&message);
		});
		if (fCancelImport)
			report.canceled = true;
	}

	if (status) {
		if (fDocument != NULL)
			fDocument->Lock();
		bdb::LogCsvImport(stack->Log(), report);
		if (fDocument != NULL)
			fDocument->Unlock();
	}
	ownStack.reset();
	if (fDocument == NULL && !staging.empty()) {
		if (!status || report.canceled) {
			// A canceled new stack leaves nothing behind (3.6).
			bdb::RemoveTree(staging);
			manifestPath = "";
		} else {
			status = bdb::FinishCsvStack(staging, dest);
			if (!status) {
				bdb::RemoveTree(staging);
				manifestPath = "";
			}
		}
	}
	if (!status)
		error = status.Code();

	BMessage message(kMsgImportDone);
	message.AddString("error", error);
	message.AddString("manifest", manifestPath);
	message.AddString("report", report.Text(str::Language()).c_str());
	message.AddBool("canceled", report.canceled);
	message.AddInt32("cards", int32(report.cards));
	BMessenger(this).SendMessage(&message);
}


// #pragma mark - messages


void
CsvWizard::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgBack:
			_ShowPage(_NextPage(fPage, -1));
			break;
		case kMsgNextPage:
			if (fPage == kPageConfirm) {
				if (fDocument == NULL
					&& BString(fStackName->Text()).IsEmpty()) {
					beep();
					break;
				}
				_StartImport();
				break;
			}
			if (_Leave(fPage))
				_ShowPage(_NextPage(fPage, 1));
			break;
		case kMsgChooseFile:
			if (fOpenPanel == NULL) {
				BMessenger target(this);
				fOpenPanel = new BFilePanel(B_OPEN_PANEL, &target, NULL,
					B_FILE_NODE, false, new BMessage(kMsgFileChosen));
			}
			fOpenPanel->Show();
			break;
		case kMsgFileChosen:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) != B_OK)
				break;
			BPath path(&ref);
			fPath = path.Path();
			BEntry entry(&ref);
			off_t size = 0;
			entry.GetSize(&size);
			BString info;
			info.SetToFormat(str::kCsvFileInfo.Get(), path.Leaf(),
				SizeText(size).String());
			fFileInfo->SetText(info);
			fNext->SetEnabled(true);
			break;
		}
		case kMsgFormatChanged:
			// Every change shows in the preview at once (3.3).
			_LoadPreview();
			break;
		case kMsgColumnChanged:
			_ReadColumns();
			_FillColumns();
			break;
		case kMsgTemplateMode:
			_FillMapping();
			break;
		case kMsgChooseDestination:
			if (fSavePanel == NULL) {
				BMessenger target(this);
				fSavePanel = new BFilePanel(B_SAVE_PANEL, &target, NULL, 0,
					false, new BMessage(kMsgDestinationChosen));
			}
			fSavePanel->SetSaveText(fStackName->Text());
			fSavePanel->Show();
			break;
		case kMsgDestinationChosen:
		{
			entry_ref directory;
			BString name;
			if (message->FindRef("directory", &directory) == B_OK
				&& message->FindString("name", &name) == B_OK) {
				fDestinationDir = BPath(&directory).Path();
				fDestination->SetText(fDestinationDir);
				if (name.EndsWith(".rfdf"))
					name.Truncate(name.Length() - 5);
				fStackName->SetText(name);
			}
			break;
		}
		case kMsgScanDone:
		{
			fScanThread = -1;
			fScanDone = true;
			fScan.rows = size_t(message->GetInt64("rows", 0));
			fScan.mismatched = size_t(message->GetInt64("mismatched", 0));
			BString status = message->GetString("status", "ok");
			fScanFailed = status != "ok";
			fScanError = message->GetString("detail", "");
			if (fScanError.IsEmpty())
				fScanError = status;
			if (fPage == kPageConfirm)
				_UpdateConfirm();
			break;
		}
		case kMsgImportProgress:
		{
			int32 cards = message->GetInt32("cards", 0);
			int32 rows = std::max(int32(1), message->GetInt32("rows", 1));
			int32 images = message->GetInt32("images", 0);
			int32 imagesTotal = message->GetInt32("imagesTotal", 0);
			fProgress->SetMaxValue(imagesTotal > 0 ? imagesTotal : rows);
			fProgress->SetTo(imagesTotal > 0 ? images : cards);
			BString text;
			text.SetToFormat(str::kCsvProgressText.Get(), (int)cards,
				(int)images, (int)imagesTotal);
			fProgressText->SetText(text);
			break;
		}
		case kMsgImportDone:
		{
			fImporting = false;
			fImportThread = -1;
			BString error = message->GetString("error", "");
			fManifestPath = message->GetString("manifest", "");
			BString report = message->GetString("report", "");
			if (!error.IsEmpty()) {
				BString text;
				text.SetToFormat(str::kCsvFailed.Get(), error.String());
				report.Prepend("\n\n");
				report.Prepend(text);
			} else if (message->GetBool("canceled", false)
				&& fDocument != NULL) {
				BString text;
				text.SetToFormat(str::kCsvAppendCanceled.Get(),
					(int)message->GetInt32("cards", 0));
				report.Prepend("\n\n");
				report.Prepend(text);
			}
			fReportText->SetText(report);
			if (fDocument != NULL) {
				BMessage changed(kMsgDocCardsChanged);
				fDocument->Notify(&changed);
				BMessage templates(kMsgDocTemplateChanged);
				fDocument->Notify(&templates);
				if (fFetchBackground)
					fReplyTo.SendMessage(kMsgFetchImages);
				fOpenStack->SetEnabled(false);
			} else
				fOpenStack->SetEnabled(!fManifestPath.IsEmpty());
			_ShowPage(kPageDone);
			break;
		}
		case kMsgOpenStack:
		{
			if (fManifestPath.IsEmpty())
				break;
			BMessage open(kMsgCsvFinished);
			open.AddString("manifest", fManifestPath);
			open.AddBool("fetch", fFetchBackground);
			be_app->PostMessage(&open);
			PostMessage(B_QUIT_REQUESTED);
			break;
		}
		case kMsgSaveReport:
		{
			if (fSavePanel == NULL) {
				BMessenger target(this);
				fSavePanel = new BFilePanel(B_SAVE_PANEL, &target, NULL, 0,
					false);
			}
			BMessage reply(kMsgReportChosen);
			fSavePanel->SetMessage(&reply);
			fSavePanel->SetSaveText(str::kCsvReportName);
			fSavePanel->Show();
			break;
		}
		case kMsgReportChosen:
		{
			entry_ref directory;
			BString name;
			if (message->FindRef("directory", &directory) != B_OK
				|| message->FindString("name", &name) != B_OK) {
				break;
			}
			BPath path(&directory);
			path.Append(name);
			// Counts and line numbers only (3.7).
			bdb::FolderStore::WriteFileAtomic(path.Path(),
				fReportText->Text());
			break;
		}
		case kMsgNoop:
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
CsvWizard::QuitRequested()
{
	if (fImporting) {
		// Cancel first; the thread reports and the window stays until then.
		fCancelImport = true;
		return false;
	}
	if (fScanThread >= 0) {
		status_t result;
		wait_for_thread(fScanThread, &result);
	}
	if (fPage == kPageDone && fDocument == NULL && !fManifestPath.IsEmpty()) {
		// The new stack opens when the wizard closes.
		BMessage open(kMsgCsvFinished);
		open.AddString("manifest", fManifestPath);
		open.AddBool("fetch", fFetchBackground);
		be_app->PostMessage(&open);
		fManifestPath = "";
	}
	if (fDocument == NULL)
		be_app->PostMessage(kMsgCsvWizardClosed);
	return true;
}
