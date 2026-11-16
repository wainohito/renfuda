/*
 * BDB card database - RenkuOS application
 * The card surface (UI spec v0.3, 2.1). Play and find mode place input
 * views on the text and number parts; buttons, switches, images, links
 * and the position display are handled here (UI 5, 6.10, I/O 2). Edit
 * mode draws the template and edits it (UI 6).
 */
#ifndef BDB_CARD_SURFACE_H
#define BDB_CARD_SURFACE_H

#include <View.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "CardPainter.h"
#include "Constants.h"
#include "Json.h"
#include "Model.h"
#include "Query.h"
#include "Theme.h"


class FieldTextView;
class StackDocument;
class BTextView;


class CardSurface : public BView {
public:
								CardSurface(StackDocument* document);
	virtual						~CardSurface();

	// What to show. Play/find: the card (and its template); edit: the
	// template being edited.
			void				SetMode(Mode mode);
			Mode				GetMode() const { return fMode; }
			void				SetCard(const std::string& cardId);
			const std::string&	CardId() const { return fCardId; }
			void				SetEditTemplate(const std::string& templateId);
			std::string			TemplateId() const;

	// The position display and the navigation buttons (UI 6.10).
			void				SetNavigation(int32 position, int32 count,
									bool findActive);

			void				SetScale(float scale);
			float				Scale() const { return fScale; }
			void				SetFitToWindow(bool fit);
			bool				FitToWindow() const { return fFit; }
	// Card size in view pixels at the current scale.
			BSize				ScaledCardSize() const;
	// Scale that fits the card into the given area.
			float				FitScale(float width, float height) const;

	// Recreates the input views from the current card (play/find).
			void				Rebuild();
			FieldTextView*		FindField(const std::string& partId) const;
			void				FocusFirstField();
			void				FocusNeighbor(BView* from, bool backward);
			std::vector<bdb::FindCondition> FindConditions() const;
			void				ClearFindConditions();
	// The image part under a drop / the image the user acted on.
			bool				IsGuarded(const std::string& partId) const;
			void				SetGuarded(const std::string& partId,
									bool guarded);

	// Edit mode
			void				SetSnap(bool snap) { fSnap = snap; }
			bool				Snap() const { return fSnap; }
			void				SetShowGrid(bool show);
			bool				ShowGrid() const { return fShowGrid; }
	// The palette item that the next drag on the card creates (-1:
	// select tool).
			void				SetCreateItem(int32 item) { fCreateItem = item; }
			int32				CreateItem() const { return fCreateItem; }
	const	std::set<std::string>& Selection() const { return fSelection; }
			void				SetSelection(const std::set<std::string>& ids);
			void				SelectAll();
			void				ClearSelection();
	// The template being edited (document must be locked).
			bdb::Template*		EditTemplate() const;
			void				PushUndo();
			bool				CanUndo() const { return !fUndo.empty(); }
			bool				CanRedo() const { return !fRedo.empty(); }
			void				Undo();
			void				Redo();
			void				ClearUndo();
	// Forgets the last PushUndo() (the change did not happen).
			void				DiscardUndo()
									{ if (!fUndo.empty()) fUndo.pop_back(); }
			void				DeleteSelection();
			void				CopySelection();
			void				CutSelection();
			void				Paste();
			void				DuplicateSelection();
			void				NudgeSelection(int32 dx, int32 dy);
			void				BringToFront();
			void				SendToBack();
			void				SetLocked(bool locked, bool all);
			bool				SelectionHasLocked() const;
			bool				SelectionHasUnlocked() const;
	// Creates parts of a palette item at a card point (UI 6.2).
			void				CreateFromPalette(int32 item, BPoint where,
									const bdb::Rect* size = NULL);
	// An image part for a dropped file (UI 6.2): `defaultRef` is its
	// default image.
			void				CreateImagePart(BPoint where,
									const bdb::Json& defaultRef);
	// Call after changing the template from outside (inspector).
			void				TemplateChanged(bool notify = true);
			void				CommitLabelEditor();
	// Theme or media changed: reload.
			void				ThemeChanged();
	// Locked or hidden by a switch of the current card (play mode).
			bool				IsPartLocked(const std::string& partId) const;
			bool				IsPartHidden(const std::string& partId) const;
			bool				UsesSystemColors() const
									{ return fTheme.Native(); }
	// The theme used for the shown template.
	const	bdb::Theme&			CurrentTheme() const { return fTheme; }

	virtual	void				AttachedToWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 code,
									const BMessage* dragMessage);
	virtual	void				MouseUp(BPoint where);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);
	virtual	void				FrameResized(float width, float height);
	virtual	void				MessageReceived(BMessage* message);

private:
			enum DragMode {
				kDragNone,
				kDragMove,
				kDragResize,
				kDragRubberBand,
				kDragCreate,
				kDragButton
			};

			const bdb::Template* _Template() const;
			const bdb::Card*	_Card() const;
			BRect				_ToView(const bdb::Rect& rect) const;
			BPoint				_ToCard(BPoint where) const;
			int32				_SnapValue(float value) const;
			void				_UpdateScrollBars();
			void				_RemoveInputViews();
			void				_LoadTheme();
			void				_FillContext(PaintContext& context);
			void				_DrawEditOverlay(const bdb::Template* tmpl);
			void				_DrawHandles(BRect rect);
			void				_DrawLock(BRect rect);
			void				_DrawMissingTemplate();
			int32				_HandleAt(BPoint where) const;
			std::string			_PartAt(BPoint cardPoint) const;
	const	bdb::Json*			_PartAtPlay(BPoint cardPoint) const;
			void				_NotifySelection();
			void				_NotifyEdited(
									const std::vector<std::string>* removed
										= NULL);
			void				_StartLabelEditor(const std::string& partId);
			std::string			_UniqueName(const std::string& kind) const;
			void				_InsertParts(const bdb::JsonArray& parts,
									int32 offset);
			void				_PlayMouseDown(BPoint where, int32 buttons,
									int32 clicks);
			void				_PlayMouseMoved(BPoint where);
			void				_EditMouseDown(BPoint where, int32 buttons,
									int32 clicks, int32 modifiers);
			void				_EditContextMenu(BPoint where,
									const std::string& hit);
			void				_PlayContextMenu(BPoint where,
									const bdb::Json* part);
			bool				_ButtonEnabled(const bdb::Json& part) const;
			void				_SetHover(const std::string& partId,
									BPoint where);
			void				_Post(uint32 what, const std::string& partId);
			void				_SetCursorFor(BPoint where);

			StackDocument*		fDocument;
			Mode				fMode;
			std::string			fCardId;
			std::string			fTemplateId;
			float				fScale;
			bool				fFit;
			bdb::Theme			fTheme;
			std::string			fThemeKey;
			BSize				fLastCardSize;
			std::string			fThemeTemplate;
			BitmapCache			fBitmaps;

			int32				fPosition;
			int32				fCount;
			bool				fFindActive;

			std::vector<BView*>	fInputs;
			std::map<std::string, int> fFindBool;
			std::set<std::string> fGuarded;
			// Switch effects of the current card (play mode only).
			bdb::SwitchState	fSwitches;

			std::string			fPressedPart;
			std::string			fHoverPart;
			bool				fPressedInside;

			int32				fCreateItem;
			bool				fSnap;
			bool				fShowGrid;
			std::set<std::string> fSelection;
			std::vector<bdb::Json> fUndo;
			std::vector<bdb::Json> fRedo;

			DragMode			fDragMode;
			BPoint				fDragStart;		// card coordinates
			BPoint				fDragCurrent;
			int32				fDragHandle;
			bool				fDragUndoPushed;
			std::map<std::string, bdb::Rect> fDragOriginal;
			std::set<std::string> fDragBaseSelection;
			// Palette item dragged over the card (preview, UI 6.2).
			int32				fDropItem;
			BPoint				fDropPoint;

			BTextView*			fLabelEditor;
			std::string			fLabelEditorPart;
			int32				fCursorKind;
};


#endif	// BDB_CARD_SURFACE_H
