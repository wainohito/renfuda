/*
 * BDB card database - RenkuOS application
 * Table of cards (UI spec 7). Only the visible rows are drawn, so a
 * 100,000 card stack scrolls without creating per-row objects.
 */
#ifndef BDB_LIST_WINDOW_H
#define BDB_LIST_WINDOW_H

#include <Messenger.h>
#include <String.h>
#include <View.h>
#include <Window.h>

#include <set>
#include <string>
#include <vector>


class BMenuField;
class BStringView;
class BTextControl;
class StackDocument;


struct ListColumn {
	std::string	partId;
	std::string	kind;
	BString		title;
	float		width;
	bool		hidden;
};


class TableView : public BView {
public:
								TableView(StackDocument* document,
									BMessenger stackWindow);

	// Recomputes columns and rows (template, filter, sort).
			void				Reload(const std::string& templateId,
									const std::string& filter);
			int32				RowCount() const
									{ return int32(fRows.size()); }
			void				SaveColumns();
			void				SelectAll();
	// Card ids in the shown order (all rows / the selected rows).
	const	std::vector<std::string>& Rows() const { return fRows; }
			std::vector<std::string> SelectedRows() const;
	const	std::vector<ListColumn>& Columns() const { return fColumns; }
			std::string			TemplateId() const { return fTemplateId; }

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 code,
									const BMessage* dragMessage);
	virtual	void				MouseUp(BPoint where);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);
	virtual	void				FrameResized(float width, float height);
	virtual	void				ScrollTo(BPoint where);
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Sort();
			void				_UpdateScrollBars();
			float				_RowHeight() const;
			float				_HeaderHeight() const;
			float				_TotalWidth() const;
			int32				_ColumnAt(float x, bool* onEdge) const;
			void				_Open(int32 row);
			void				_Select(int32 row, int32 modifiers = 0);
			void				_RowMenu(BPoint where, int32 row);
			BString				_CellText(int32 row,
									const ListColumn& column) const;
			std::string			_ColumnKey() const;

			StackDocument*		fDocument;
			BMessenger			fStackWindow;
			std::string			fTemplateId;
			std::vector<ListColumn> fColumns;
			std::vector<std::string> fRows;		// card ids
			int32				fSortColumn;
			bool				fSortAscending;
			int32				fSelected;
			int32				fAnchor;
			std::set<std::string> fSelectedIds;
			int32				fResizing;
			float				fResizeStart;
			float				fResizeWidth;
};


class ListWindow : public BWindow {
public:
								ListWindow(StackDocument* document,
									BMessenger stackWindow);

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_BuildTemplateMenu();
			void				_Reload();
			void				_SavePdf(uint32 what);

			StackDocument*		fDocument;
			BMenuField*			fTemplateField;
			BTextControl*		fSearch;
			BStringView*		fCount;
			TableView*			fTable;
			std::string			fTemplateId;
			uint32				fPdfKind;
			BMessenger			fStackWindow;
};


#endif	// BDB_LIST_WINDOW_H
