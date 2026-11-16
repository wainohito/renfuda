/*
 * BDB card database - RenkuOS application
 * Draws a card - background, parts in their theme look, values, images and
 * icons - on a Canvas (theme spec 4, 5, 7; I/O spec 4.2). The card surface
 * draws on a BView canvas, the PDF export on a PDF canvas, so a printed
 * card looks like the card on screen.
 *
 * All coordinates are card pixels (the template's coordinate system);
 * canvases apply their own scale.
 */
#ifndef BDB_CARD_PAINTER_H
#define BDB_CARD_PAINTER_H

#include <Font.h>
#include <GraphicsDefs.h>
#include <Rect.h>
#include <String.h>

#include <functional>
#include <map>
#include <set>
#include <string>

#include "Json.h"
#include "Theme.h"


class BBitmap;
class BView;

namespace bdb {
class Card;
class Stack;
class Template;
}


class Canvas {
public:
	virtual						~Canvas() {}

	virtual	void				FillRect(BRect rect, rgb_color color) = 0;
	virtual	void				FillRoundRect(BRect rect, float radius,
									rgb_color color) = 0;
	virtual	void				StrokeRoundRect(BRect rect, float radius,
									float width, rgb_color color) = 0;
	virtual	void				FillEllipse(BRect rect, rgb_color color) = 0;
	virtual	void				StrokeLine(BPoint from, BPoint to,
									float width, rgb_color color) = 0;
	// `font` has its size in card pixels.
	virtual	void				DrawString(const char* text, BPoint baseline,
									const BFont& font, rgb_color color) = 0;
	virtual	float				StringWidth(const char* text,
									const BFont& font) = 0;
	// `path`: the file the bitmap came from (the PDF embeds JPEG files as
	// they are), may be NULL.
	virtual	void				DrawBitmap(const BBitmap* bitmap,
									BRect target, const char* path) = 0;
	virtual	void				PushClip(BRect rect) = 0;
	virtual	void				PopClip() = 0;
	virtual	bool				IsPrinting() const { return false; }
	// Width of one device pixel in card pixels (for crisp lines).
	virtual	float				PixelSize() const { return 1.0f; }
};


class ViewCanvas : public Canvas {
public:
								ViewCanvas(BView* view, float scale,
									BPoint origin = BPoint(0, 0));

	virtual	void				FillRect(BRect rect, rgb_color color);
	virtual	void				FillRoundRect(BRect rect, float radius,
									rgb_color color);
	virtual	void				StrokeRoundRect(BRect rect, float radius,
									float width, rgb_color color);
	virtual	void				FillEllipse(BRect rect, rgb_color color);
	virtual	void				StrokeLine(BPoint from, BPoint to,
									float width, rgb_color color);
	virtual	void				DrawString(const char* text, BPoint baseline,
									const BFont& font, rgb_color color);
	virtual	float				StringWidth(const char* text,
									const BFont& font);
	virtual	void				DrawBitmap(const BBitmap* bitmap,
									BRect target, const char* path);
	virtual	void				PushClip(BRect rect);
	virtual	void				PopClip();
	virtual	float				PixelSize() const { return 1.0f / fScale; }

			BRect				ToView(BRect rect) const;
			BPoint				ToView(BPoint point) const;

private:
			BView*				fView;
			float				fScale;
			BPoint				fOrigin;
			int32				fClipDepth;
};


// Loaded images, keyed by file path (and size for vector icons).
class BitmapCache {
public:
								BitmapCache();
								~BitmapCache();

	// NULL when the file cannot be read.
			const BBitmap*		Get(const std::string& path);
	// HVIF icons are rendered at the requested size; other image files
	// are loaded at their own size.
			const BBitmap*		GetIcon(const std::string& path, int32 size);
			void				Clear();
	// Drops everything when more than `count` images are cached.
			void				TrimTo(size_t count)
									{ if (fBitmaps.size() > count) Clear(); }

private:
			std::map<std::string, BBitmap*> fBitmaps;
			std::set<std::string> fFailed;
};


rgb_color ToRgb(const bdb::Color& color);
// A native theme (theme spec 4.2) with the system's current colors.
bdb::Theme WithSystemColors(const bdb::Theme& theme);
bdb::Color FromRgb(rgb_color color);
// The BFont of a resolved look (family/style may be missing: the system
// font is used, theme spec 6).
BFont FontForLook(const bdb::PartLook& look);


enum PaintMode {
	kPaintPlay = 0,		// play mode: inputs are views on top
	kPaintFind,
	kPaintEdit,			// template editing: names instead of values
	kPaintPrint			// PDF: values drawn, buttons and counters left out
};


struct PaintContext {
	const bdb::Template*	tmpl = NULL;
	const bdb::Card*		card = NULL;
	const bdb::Stack*		stack = NULL;
	const bdb::Theme*		theme = NULL;
	BitmapCache*			bitmaps = NULL;
	PaintMode				mode = kPaintPlay;
	// Value parts are drawn with their values (print, previews). In play
	// and find mode the input views show them.
	bool					drawValues = false;
	bool					printBackground = true;
	int32					position = 1;		// counter: 1-based
	int32					count = 1;
	std::string				pressedPart;
	std::string				focusPart;
	std::string				hoverPart;
	// Buttons that are disabled (card.first on the first card, ...).
	std::function<bool(const bdb::Json& part)> buttonEnabled;
	// Find mode: the switch conditions (-1 any, 0 off, 1 on).
	std::map<std::string, int>	findBool;
	// Parts hidden by switches (bdb::ComputeSwitchState).
	std::set<std::string>	hiddenParts;
};


// Background color, image and parts (data spec 7: color, image, parts in
// their order).
void PaintCard(Canvas& canvas, const PaintContext& context);
void PaintBackground(Canvas& canvas, const PaintContext& context);
void PaintPart(Canvas& canvas, const PaintContext& context,
	const bdb::Json& part);

// The frame of a part in its look (theme spec 4.3). Returns the inner
// rectangle (inside the border).
BRect PaintFrame(Canvas& canvas, const bdb::Theme& theme, BRect rect,
	const bdb::PartLook& look, bool pressed);
// Inner rectangle of a frame without drawing.
BRect FrameInset(BRect rect, const bdb::PartLook& look);

// Text in a rectangle: line breaks, wrapping when `wrap`, clipped.
void DrawTextBlock(Canvas& canvas, const char* text, BRect rect,
	const BFont& font, rgb_color color, bool wrap, int alignment,
	bool centerVertically);

// The panel color of a template: background.color, else the theme panel.
rgb_color PanelColor(const bdb::Template* tmpl, const bdb::Theme& theme);

// The name of a part kind ("テキスト", ...).
const char* KindName(const std::string& kind);
// Text shown for a number value with the part's decimals (UI 5.3).
BString FormatNumberValue(const bdb::Json& value, const bdb::Json& part);


#endif	// BDB_CARD_PAINTER_H
