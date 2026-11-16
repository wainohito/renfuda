/*
 * BDB card database - RenkuOS application
 */
#include "ListWindow.h"

#include <Application.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "CardPainter.h"
#include "Constants.h"
#include "PdfExport.h"
#include "Query.h"
#include "Settings.h"
#include "Stack.h"
#include "StackDocument.h"
#include "Strings.h"
#include "Text.h"

#include <TextView.h>


using bdb::Json;

static const float kDefaultColumnWidth = 140.0f;
static const float kMinColumnWidth = 24.0f;
static const float kCellInset = 4.0f;


// #pragma mark - TableView


TableView::TableView(StackDocument* document, BMessenger stackWindow)
	:
	BView("table", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE),
	fDocument(document),
	fStackWindow(stackWindow),
	fSortColumn(-1),
	fSortAscending(true),
	fSelected(-1),
	fAnchor(-1),
	fResizing(-1),
	fResizeStart(0),
	fResizeWidth(0)
{
	SetViewUIColor(B_LIST_BACKGROUND_COLOR);
	// Not the current size (the default for a view without a layout).
	SetExplicitMinSize(BSize(120, 60));
}


float
TableView::_RowHeight() const
{
	font_height height;
	be_plain_font->GetHeight(&height);
	return ceilf(height.ascent + height.descent + height.leading) + 4;
}


float
TableView::_HeaderHeight() const
{
	return _RowHeight() + 2;
}


float
TableView::_TotalWidth() const
{
	float width = 0;
	for (const ListColumn& column : fColumns) {
		if (!column.hidden)
			width += column.width;
	}
	return width;
}


std::string
TableView::_ColumnKey() const
{
	return std::string(fDocument->Id().String()) + ":" + fTemplateId;
}


void
TableView::Reload(const std::string& templateId, const std::string& filter)
{
	DocLocker lock(fDocument);
	bdb::Stack& stack = fDocument->GetStack();
	bool templateChanged = templateId != fTemplateId;
	fTemplateId = templateId;

	// Columns: the value parts (UI spec 7), remembered per template.
	std::vector<ListColumn> previous = fColumns;
	fColumns.clear();
	BMessage saved;
	bool haveSaved = gSettings.GetColumns(_ColumnKey().c_str(), saved);
	if (const bdb::Template* tmpl = stack.FindTemplate(templateId)) {
		for (const Json& part : tmpl->Parts()) {
			std::string kind = bdb::part::Kind(part);
			if (!bdb::KindHasValue(kind))
				continue;
			ListColumn column;
			column.partId = bdb::part::Id(part);
			column.kind = kind;
			column.title = bdb::part::Name(part).c_str();
			column.width = kDefaultColumnWidth;
			column.hidden = false;
			for (const ListColumn& old : previous) {
				if (!templateChanged && old.partId == column.partId) {
					column.width = old.width;
					column.hidden = old.hidden;
				}
			}
			if (haveSaved && (templateChanged || previous.empty())) {
				float width;
				if (saved.FindFloat(column.partId.c_str(), &width) == B_OK)
					column.width = std::max(kMinColumnWidth, width);
				BString hiddenKey = column.partId.c_str();
				hiddenKey << ":hidden";
				column.hidden = saved.GetBool(hiddenKey.String(), false);
			}
			fColumns.push_back(column);
		}
	}
	if (templateChanged) {
		fSortColumn = -1;
		fSelected = -1;
		fSelectedIds.clear();
	}

	std::vector<int> indexes = bdb::QuickSearch(stack, templateId, filter);
	fRows.clear();
	fRows.reserve(indexes.size());
	for (int index : indexes)
		fRows.push_back(stack.CardAt(index)->Id());
	_Sort();
	if (fSelected >= RowCount())
		fSelected = RowCount() - 1;
	_UpdateScrollBars();
	Invalidate();
}


void
TableView::SaveColumns()
{
	BMessage columns;
	for (const ListColumn& column : fColumns) {
		columns.AddFloat(column.partId.c_str(), column.width);
		BString hiddenKey = column.partId.c_str();
		hiddenKey << ":hidden";
		columns.AddBool(hiddenKey.String(), column.hidden);
	}
	gSettings.SetColumns(_ColumnKey().c_str(), columns);
}


void
TableView::_Sort()
{
	if (fSortColumn < 0 || fSortColumn >= int32(fColumns.size()))
		return;
	DocLocker lock(fDocument);
	bdb::Stack& stack = fDocument->GetStack();
	const bdb::Template* tmpl = stack.FindTemplate(fTemplateId);
	if (tmpl == NULL)
		return;
	const Json* part = tmpl->FindPart(fColumns[fSortColumn].partId);
	if (part == NULL)
		return;
	std::string kind = bdb::part::Kind(*part);

	// Keys are extracted once; sorting 100,000 rows stays fast.
	struct Key {
		int			group;		// 0 value, 1 empty (always last)
		double		number;
		std::string	text;
		size_t		order;
	};
	std::vector<std::pair<Key, std::string>> keyed;
	keyed.reserve(fRows.size());
	for (size_t i = 0; i < fRows.size(); i++) {
		const bdb::Card* card = stack.FindCard(fRows[i]);
		Key key = { 1, 0, std::string(), i };
		if (card != NULL) {
			const Json& value = bdb::EffectiveValue(*card, *part);
			if (kind == bdb::kKindNumber) {
				if (value.IsNumber()) {
					key.group = 0;
					key.number = value.AsDouble();
				}
			} else if (kind == bdb::kKindImage) {
				key.group = bdb::IsMediaRef(value) ? 0 : 1;
			} else if (kind == bdb::kKindBool) {
				key.group = 0;
				key.number = value.IsBool() && value.AsBool() ? 1 : 0;
			} else {
				key.text = value.AsString();
				key.group = key.text.empty() ? 1 : 0;
			}
		}
		keyed.push_back({key, fRows[i]});
	}
	bool ascending = fSortAscending;
	std::stable_sort(keyed.begin(), keyed.end(),
		[ascending](const auto& a, const auto& b) {
			if (a.first.group != b.first.group)
				return a.first.group < b.first.group;
			if (a.first.number != b.first.number) {
				return ascending ? a.first.number < b.first.number
					: a.first.number > b.first.number;
			}
			int compare = a.first.text.compare(b.first.text);
			if (compare != 0)
				return ascending ? compare < 0 : compare > 0;
			return a.first.order < b.first.order;
		});
	for (size_t i = 0; i < keyed.size(); i++)
		fRows[i] = keyed[i].second;
}


void
TableView::_UpdateScrollBars()
{
	BRect bounds = Bounds();
	float height = _HeaderHeight() + RowCount() * _RowHeight();
	float width = _TotalWidth();
	if (BScrollBar* bar = ScrollBar(B_VERTICAL)) {
		float range = std::max(0.0f, height - bounds.Height());
		bar->SetRange(0, range);
		bar->SetProportion(height > 0 ? std::min(1.0f,
			bounds.Height() / height) : 1.0f);
		bar->SetSteps(_RowHeight(), bounds.Height() - _RowHeight() * 2);
	}
	if (BScrollBar* bar = ScrollBar(B_HORIZONTAL)) {
		float range = std::max(0.0f, width - bounds.Width());
		bar->SetRange(0, range);
		bar->SetProportion(width > 0 ? std::min(1.0f,
			bounds.Width() / width) : 1.0f);
		bar->SetSteps(16, bounds.Width() / 2);
	}
}


void
TableView::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	_UpdateScrollBars();
}


void
TableView::ScrollTo(BPoint where)
{
	BView::ScrollTo(where);
	// The header is drawn at the top of the visible area.
	Invalidate();
}


BString
TableView::_CellText(int32 row, const ListColumn& column) const
{
	bdb::Stack& stack = fDocument->GetStack();
	const bdb::Card* card = stack.FindCard(fRows[row]);
	const bdb::Template* tmpl = stack.FindTemplate(fTemplateId);
	if (card == NULL || tmpl == NULL)
		return "";
	const Json* part = tmpl->FindPart(column.partId);
	if (part == NULL)
		return "";
	const Json& value = bdb::EffectiveValue(*card, *part);
	if (column.kind == bdb::kKindImage) {
		return bdb::IsMediaRef(value) ? str::kListImageYes.Get()
			: str::kListImageNo.Get();
	}
	if (column.kind == bdb::kKindNumber && value.IsNumber())
		return FormatNumberValue(value, *part);
	// Switches show a check mark when on (UI 7).
	if (column.kind == bdb::kKindBool)
		return value.IsBool() && value.AsBool() ? "✓" : "";
	return bdb::FirstLine(bdb::ValueText(value)).c_str();
}


void
TableView::Draw(BRect updateRect)
{
	DocLocker lock(fDocument);
	BRect bounds = Bounds();
	float rowHeight = _RowHeight();
	float headerHeight = _HeaderHeight();
	font_height fontHeight;
	GetFontHeight(&fontHeight);

	rgb_color background = ui_color(B_LIST_BACKGROUND_COLOR);
	rgb_color selected = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
	rgb_color text = ui_color(B_LIST_ITEM_TEXT_COLOR);
	rgb_color selectedText = ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);
	rgb_color grid = tint_color(background, B_DARKEN_1_TINT);

	// Rows: only those inside the update rect.
	float top = bounds.top + headerHeight;
	int32 first = std::max(int32(0),
		int32((updateRect.top - headerHeight) / rowHeight));
	int32 last = std::min(RowCount() - 1,
		int32((updateRect.bottom - headerHeight) / rowHeight) + 1);
	SetHighColor(background);
	FillRect(updateRect);
	for (int32 row = first; row <= last; row++) {
		float y = headerHeight + row * rowHeight;
		if (y + rowHeight < top)
			continue;
		BRect rowRect(bounds.left, y, bounds.right, y + rowHeight - 1);
		bool isSelected = fSelectedIds.count(fRows[row]) != 0;
		if (isSelected) {
			SetHighColor(selected);
			FillRect(rowRect);
		}
		float x = 0;
		for (const ListColumn& column : fColumns) {
			if (column.hidden)
				continue;
			BRect cell(x, y, x + column.width - 1, y + rowHeight - 1);
			x += column.width;
			if (!cell.Intersects(updateRect))
				continue;
			BString value = _CellText(row, column);
			SetHighColor(isSelected ? selectedText : text);
			BString truncated = value;
			TruncateString(&truncated, B_TRUNCATE_END,
				column.width - kCellInset * 2);
			float textX = cell.left + kCellInset;
			if (column.kind == bdb::kKindNumber)
				textX = cell.right - kCellInset - StringWidth(truncated);
			else if (column.kind == bdb::kKindBool)
				textX = cell.left + (column.width - StringWidth(truncated)) / 2;
			DrawString(truncated, BPoint(textX,
				y + fontHeight.ascent + 2));
			SetHighColor(grid);
			StrokeLine(cell.RightTop(), cell.RightBottom());
		}
	}

	// Header at the top of the visible area.
	BRect header(bounds.left, bounds.top, bounds.right,
		bounds.top + headerHeight - 1);
	SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	FillRect(header);
	SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_2_TINT));
	StrokeLine(header.LeftBottom(), header.RightBottom());
	float x = 0;
	for (size_t i = 0; i < fColumns.size(); i++) {
		const ListColumn& column = fColumns[i];
		if (column.hidden)
			continue;
		BString title = column.title;
		if (int32(i) == fSortColumn)
			title << (fSortAscending ? " ▲" : " ▼");
		TruncateString(&title, B_TRUNCATE_END, column.width - kCellInset * 2);
		SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
		DrawString(title, BPoint(x + kCellInset,
			header.top + fontHeight.ascent + 3));
		x += column.width;
		SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
			B_DARKEN_2_TINT));
		StrokeLine(BPoint(x - 1, header.top), BPoint(x - 1, header.bottom));
	}
}


int32
TableView::_ColumnAt(float x, bool* onEdge) const
{
	*onEdge = false;
	float left = 0;
	for (size_t i = 0; i < fColumns.size(); i++) {
		if (fColumns[i].hidden)
			continue;
		float right = left + fColumns[i].width;
		if (fabsf(x - right) <= 3) {
			*onEdge = true;
			return int32(i);
		}
		if (x < right)
			return int32(i);
		left = right;
	}
	return -1;
}


void
TableView::_Select(int32 row, int32 modifiers)
{
	if (row < 0 || row >= RowCount())
		return;
	if ((modifiers & B_SHIFT_KEY) != 0 && fAnchor >= 0) {
		// A range from the anchor.
		fSelectedIds.clear();
		int32 from = std::min(fAnchor, row);
		int32 to = std::max(fAnchor, row);
		for (int32 i = from; i <= to && i < RowCount(); i++)
			fSelectedIds.insert(fRows[i]);
	} else if ((modifiers & (B_COMMAND_KEY | B_CONTROL_KEY)) != 0) {
		// Add or remove one row.
		if (fSelectedIds.count(fRows[row]) != 0)
			fSelectedIds.erase(fRows[row]);
		else
			fSelectedIds.insert(fRows[row]);
		fAnchor = row;
	} else {
		fSelectedIds.clear();
		fSelectedIds.insert(fRows[row]);
		fAnchor = row;
	}
	fSelected = row;
	// Keep the selection visible below the header.
	BRect bounds = Bounds();
	float rowHeight = _RowHeight();
	float top = _HeaderHeight() + row * rowHeight;
	if (top < bounds.top + _HeaderHeight())
		ScrollTo(BPoint(bounds.left, top - _HeaderHeight()));
	else if (top + rowHeight > bounds.bottom)
		ScrollTo(BPoint(bounds.left, top + rowHeight - bounds.Height()));
	Invalidate();
}


void
TableView::SelectAll()
{
	fSelectedIds.clear();
	for (const std::string& id : fRows)
		fSelectedIds.insert(id);
	Invalidate();
}


std::vector<std::string>
TableView::SelectedRows() const
{
	std::vector<std::string> rows;
	for (const std::string& id : fRows) {
		if (fSelectedIds.count(id) != 0)
			rows.push_back(id);
	}
	return rows;
}


void
TableView::_RowMenu(BPoint where, int32 row)
{
	if (fSelectedIds.count(fRows[row]) == 0)
		_Select(row);
	// UI 10.5: open, PDF, duplicate, delete.
	BPopUpMenu* menu = new BPopUpMenu("row", false, false);
	BMessage* open = new BMessage(kMsgGoToCard);
	open->AddString("card", fRows[row].c_str());
	menu->AddItem(new BMenuItem(str::kListOpenCard, open));
	menu->AddItem(new BMenuItem(str::kSaveSelectedPdf,
		new BMessage(kMsgSaveSelectedPdf)));
	menu->AddSeparatorItem();
	BMessage* duplicate = new BMessage(kMsgDuplicateCard);
	duplicate->AddString("card", fRows[row].c_str());
	menu->AddItem(new BMenuItem(str::kDuplicateCard, duplicate));
	BMessage* remove = new BMessage(kMsgDeleteCard);
	remove->AddString("card", fRows[row].c_str());
	menu->AddItem(new BMenuItem(str::kDeleteCard, remove));
	menu->SetTargetForItems(Window());
	menu->SetAsyncAutoDestruct(true);
	menu->Go(ConvertToScreen(where), true, true, true);
}


void
TableView::_Open(int32 row)
{
	if (row < 0 || row >= RowCount())
		return;
	BMessage message(kMsgGoToCard);
	message.AddString("card", fRows[row].c_str());
	fStackWindow.SendMessage(&message);
}


void
TableView::MouseDown(BPoint where)
{
	MakeFocus(true);
	BMessage* current = Window()->CurrentMessage();
	int32 clicks = 1;
	int32 buttons = B_PRIMARY_MOUSE_BUTTON;
	if (current != NULL) {
		current->FindInt32("clicks", &clicks);
		current->FindInt32("buttons", &buttons);
	}
	BRect bounds = Bounds();

	if (where.y < bounds.top + _HeaderHeight()) {
		if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
			// Show / hide columns.
			BPopUpMenu* menu = new BPopUpMenu("columns", false, false);
			for (size_t i = 0; i < fColumns.size(); i++) {
				BMessage* message = new BMessage(kMsgListColumnToggle);
				message->AddInt32("column", int32(i));
				BMenuItem* item = new BMenuItem(fColumns[i].title, message);
				item->SetMarked(!fColumns[i].hidden);
				menu->AddItem(item);
			}
			menu->SetTargetForItems(this);
			menu->SetAsyncAutoDestruct(true);
			menu->Go(ConvertToScreen(where), true, true, true);
			return;
		}
		bool onEdge = false;
		int32 column = _ColumnAt(where.x, &onEdge);
		if (column < 0)
			return;
		if (onEdge) {
			fResizing = column;
			fResizeStart = where.x;
			fResizeWidth = fColumns[column].width;
			SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
			return;
		}
		if (column == fSortColumn)
			fSortAscending = !fSortAscending;
		else {
			fSortColumn = column;
			fSortAscending = true;
		}
		std::string selectedId = fSelected >= 0 && fSelected < RowCount()
			? fRows[fSelected] : std::string();
		_Sort();
		if (!selectedId.empty()) {
			auto found = std::find(fRows.begin(), fRows.end(), selectedId);
			fSelected = int32(found - fRows.begin());
		}
		{
			DocLocker lock(fDocument);
			fDocument->GetStack().Log().Debug("ui.sort", {
				{"part", fColumns[column].partId},
				{"ascending", fSortAscending}});
		}
		Invalidate();
		return;
	}

	int32 row = int32((where.y - _HeaderHeight()) / _RowHeight());
	if (row < 0 || row >= RowCount())
		return;
	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		_RowMenu(where, row);
		return;
	}
	int32 modifiers = 0;
	if (current != NULL)
		current->FindInt32("modifiers", &modifiers);
	_Select(row, clicks == 2 ? 0 : modifiers);
	if (clicks == 2)
		_Open(row);
}


void
TableView::MouseMoved(BPoint where, uint32 code, const BMessage* dragMessage)
{
	if (fResizing >= 0) {
		fColumns[fResizing].width = std::max(kMinColumnWidth,
			fResizeWidth + where.x - fResizeStart);
		_UpdateScrollBars();
		Invalidate();
		return;
	}
	BView::MouseMoved(where, code, dragMessage);
}


void
TableView::MouseUp(BPoint where)
{
	if (fResizing >= 0) {
		fResizing = -1;
		SaveColumns();
	}
	BView::MouseUp(where);
}


void
TableView::KeyDown(const char* bytes, int32 numBytes)
{
	if (numBytes != 1) {
		BView::KeyDown(bytes, numBytes);
		return;
	}
	int32 modifiers = 0;
	if (Window()->CurrentMessage() != NULL)
		Window()->CurrentMessage()->FindInt32("modifiers", &modifiers);
	modifiers &= B_SHIFT_KEY;
	int32 page = std::max(int32(1),
		int32((Bounds().Height() - _HeaderHeight()) / _RowHeight()) - 1);
	switch (bytes[0]) {
		case B_UP_ARROW:
			_Select(std::max(int32(0), fSelected - 1), modifiers);
			break;
		case B_DOWN_ARROW:
			_Select(std::min(RowCount() - 1, fSelected + 1), modifiers);
			break;
		case B_PAGE_UP:
			_Select(std::max(int32(0), fSelected - page), modifiers);
			break;
		case B_PAGE_DOWN:
			_Select(std::min(RowCount() - 1, fSelected + page), modifiers);
			break;
		case B_HOME:
			_Select(0);
			break;
		case B_END:
			_Select(RowCount() - 1);
			break;
		case B_ENTER:
			_Open(fSelected);
			break;
		default:
			BView::KeyDown(bytes, numBytes);
			break;
	}
}


void
TableView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgListColumnToggle:
		{
			int32 column = message->GetInt32("column", -1);
			if (column >= 0 && column < int32(fColumns.size())) {
				fColumns[column].hidden = !fColumns[column].hidden;
				SaveColumns();
				_UpdateScrollBars();
				Invalidate();
			}
			break;
		}
		default:
			BView::MessageReceived(message);
			break;
	}
}


// #pragma mark - ListWindow


ListWindow::ListWindow(StackDocument* document, BMessenger stackWindow)
	:
	BWindow(BRect(120, 120, 820, 560), "", B_DOCUMENT_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS),
	fDocument(document),
	fPdfKind(0),
	fStackWindow(stackWindow)
{
	// UI 10.4: PDF of the selection, the list and all cards; select all.
	BMenuBar* menuBar = new BMenuBar("menu");
	BMenu* file = new BMenu(str::kMenuFile);
	file->AddItem(new BMenuItem(str::kSaveSelectedPdf,
		new BMessage(kMsgSaveSelectedPdf)));
	file->AddItem(new BMenuItem(str::kSaveListPdf,
		new BMessage(kMsgSaveListPdf)));
	file->AddItem(new BMenuItem(str::kSaveAllPdf,
		new BMessage(kMsgSaveAllPdf)));
	file->AddSeparatorItem();
	file->AddItem(new BMenuItem(str::kClose, new BMessage(B_QUIT_REQUESTED),
		'W'));
	menuBar->AddItem(file);
	BMenu* edit = new BMenu(str::kMenuEdit);
	edit->AddItem(new BMenuItem(str::kSelectAll, new BMessage(kMsgSelectAll),
		'A'));
	menuBar->AddItem(edit);
	fTemplateField = new BMenuField("template", str::kListTemplate,
		new BPopUpMenu("template"));
	fSearch = new BTextControl("search", str::kListSearch, "",
		new BMessage(kMsgListSearch));
	fSearch->SetModificationMessage(new BMessage(kMsgListSearch));
	fCount = new BStringView("count", "");
	fTable = new TableView(document, stackWindow);
	BScrollView* scroll = new BScrollView("scroll", fTable, 0, true, true,
		B_NO_BORDER);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_SMALL_INSETS)
			.Add(fTemplateField)
			.Add(fSearch)
			.Add(fCount)
		.End()
		.Add(scroll);

	{
		DocLocker lock(fDocument);
		bdb::Stack& stack = fDocument->GetStack();
		BString title;
		title.SetToFormat(str::kListTitle, stack.Title().c_str());
		SetTitle(title);
		fTemplateId = stack.DefaultTemplateId();
	}
	_BuildTemplateMenu();
	fDocument->AddObserver(BMessenger(this));
	_Reload();
	fTable->MakeFocus(true);
}


void
ListWindow::_BuildTemplateMenu()
{
	BMenu* menu = fTemplateField->Menu();
	menu->RemoveItems(0, menu->CountItems(), true);
	DocLocker lock(fDocument);
	bdb::Stack& stack = fDocument->GetStack();
	std::vector<bdb::Template*> templates = stack.Templates();
	bool found = false;
	for (bdb::Template* tmpl : templates) {
		BMessage* message = new BMessage(kMsgListTemplate);
		message->AddString("template", tmpl->Id().c_str());
		BMenuItem* item = new BMenuItem(tmpl->Title().c_str(), message);
		if (tmpl->Id() == fTemplateId) {
			item->SetMarked(true);
			found = true;
		}
		menu->AddItem(item);
	}
	if (!found && !templates.empty()) {
		fTemplateId = templates[0]->Id();
		menu->ItemAt(0)->SetMarked(true);
	}
	menu->SetTargetForItems(this);
	// Only offer the choice when there is one (UI spec 7).
	if (templates.size() > 1) {
		if (fTemplateField->IsHidden(fTemplateField))
			fTemplateField->Show();
	} else if (!fTemplateField->IsHidden(fTemplateField))
		fTemplateField->Hide();
}


void
ListWindow::_Reload()
{
	fTable->Reload(fTemplateId, fSearch->Text());
	BString count;
	count.SetToFormat(str::kListCount, (int)fTable->RowCount());
	fCount->SetText(count);
}


void
ListWindow::_SavePdf(uint32 what)
{
	std::vector<std::string> rows = what == kMsgSaveSelectedPdf
		? fTable->SelectedRows() : fTable->Rows();
	if (rows.empty())
		return;
	BString stackTitle;
	{
		DocLocker lock(fDocument);
		stackTitle = bdb::SanitizeTitle(fDocument->GetStack().Title())
			.c_str();
	}
	// Default names (I/O 4.4).
	BString name;
	if (what == kMsgSaveListPdf) {
		name << stackTitle << "-" << str::kPdfListSuffix.Get() << ".pdf";
	} else {
		name.SetToFormat(str::kPdfCardsFormat.Get(), stackTitle.String(),
			(int)rows.size());
	}
	fPdfKind = what;
	ShowPdfSavePanel(BMessenger(this), name, what == kMsgSaveListPdf);
}


void
ListWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSelectAll:
			if (dynamic_cast<BTextView*>(CurrentFocus()) != NULL)
				PostMessage(B_SELECT_ALL, CurrentFocus());
			else
				fTable->SelectAll();
			break;
		case kMsgSaveSelectedPdf:
		case kMsgSaveListPdf:
		case kMsgSaveAllPdf:
			_SavePdf(message->what);
			break;
		case kMsgPdfPanel:
		{
			BString path;
			if (!PdfPanelPath(message, path))
				break;
			PdfJob* job = new PdfJob();
			job->document = fDocument;
			job->path = path;
			job->replyTo = BMessenger(this);
			if (fPdfKind == kMsgSaveListPdf) {
				job->kind = PdfJob::kList;
				job->logKind = "list";
				job->cardIds = fTable->Rows();
				job->templateId = fTable->TemplateId();
				for (const ListColumn& column : fTable->Columns()) {
					if (column.hidden)
						continue;
					PdfColumn pdfColumn;
					pdfColumn.partId = column.partId;
					pdfColumn.kind = column.kind;
					pdfColumn.title = column.title;
					job->columns.push_back(pdfColumn);
				}
				job->paperSize = message->GetString("paper", "A4");
				job->orientation = message->GetString("orientation",
					"landscape");
				job->imageThumbnails = message->GetBool("thumbs", false);
			} else {
				job->kind = PdfJob::kCards;
				job->logKind = "cards";
				job->cardIds = fPdfKind == kMsgSaveSelectedPdf
					? fTable->SelectedRows() : fTable->Rows();
				job->printBackground = message->GetBool("background", true);
			}
			StartPdfExport(job);
			break;
		}
		case kMsgPdfDone:
			ReportPdfResult(message);
			break;
		case kMsgGoToCard:
			fStackWindow.SendMessage(message);
			break;
		case kMsgDuplicateCard:
		case kMsgDeleteCard:
		{
			// Through the stack window, on that card.
			BMessage go(kMsgGoToCard);
			go.AddString("card", message->GetString("card", ""));
			fStackWindow.SendMessage(&go);
			fStackWindow.SendMessage(message->what);
			break;
		}
		case kMsgListTemplate:
		{
			BString id;
			if (message->FindString("template", &id) == B_OK) {
				fTemplateId = id.String();
				_Reload();
			}
			break;
		}
		case kMsgListSearch:
			_Reload();
			break;
		case kMsgDocCardChanged:
		case kMsgDocCardsChanged:
			_Reload();
			break;
		case kMsgDocTemplateChanged:
			_BuildTemplateMenu();
			_Reload();
			break;
		case kMsgDocStackChanged:
		{
			DocLocker lock(fDocument);
			BString title;
			title.SetToFormat(str::kListTitle,
				fDocument->GetStack().Title().c_str());
			SetTitle(title);
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
ListWindow::QuitRequested()
{
	fTable->SaveColumns();
	fDocument->RemoveObserver(BMessenger(this));
	BMessage released(kMsgDocumentReleased);
	released.AddPointer("document", fDocument);
	be_app->PostMessage(&released);
	return true;
}
