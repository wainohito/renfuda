/*
 * BDB card database - RenkuOS application
 */
#include "CardPainter.h"

#include <Bitmap.h>
#include <IconUtils.h>
#include <Region.h>
#include <TranslationUtils.h>
#include <View.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Link.h"
#include "Model.h"
#include "Stack.h"
#include "Store.h"
#include "Strings.h"
#include "Text.h"


using bdb::Json;


rgb_color
ToRgb(const bdb::Color& color)
{
	return make_color(color.r, color.g, color.b, 255);
}


bdb::Color
FromRgb(rgb_color color)
{
	return bdb::Color::Make(color.red, color.green, color.blue);
}


bdb::Theme
WithSystemColors(const bdb::Theme& theme)
{
	if (!theme.Native())
		return theme;
	bdb::Theme native = theme;
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	native.SetColor("panel", FromRgb(panel));
	native.SetColor("text", FromRgb(ui_color(B_DOCUMENT_TEXT_COLOR)));
	native.SetColor("text_disabled", FromRgb(tint_color(
		ui_color(B_PANEL_TEXT_COLOR), B_DISABLED_LABEL_TINT)));
	native.SetColor("control_fill", FromRgb(
		ui_color(B_DOCUMENT_BACKGROUND_COLOR)));
	native.SetColor("control_border", FromRgb(
		ui_color(B_CONTROL_BORDER_COLOR)));
	native.SetColor("accent", FromRgb(
		ui_color(B_KEYBOARD_NAVIGATION_COLOR)));
	native.SetColor("link", FromRgb(ui_color(B_LINK_TEXT_COLOR)));
	native.SetColor("bevel_light", FromRgb(tint_color(panel,
		B_LIGHTEN_MAX_TINT)));
	native.SetColor("bevel_dark", FromRgb(tint_color(panel,
		B_DARKEN_2_TINT)));
	native.SetColor("bevel_darker", FromRgb(tint_color(panel,
		B_DARKEN_4_TINT)));
	return native;
}


BFont
FontForLook(const bdb::PartLook& look)
{
	BFont font(be_plain_font);
	if (!look.fontFamily.empty()) {
		if (font.SetFamilyAndStyle(look.fontFamily.c_str(),
				look.fontStyle.c_str()) != B_OK) {
			font.SetFamilyAndStyle(look.fontFamily.c_str(), NULL);
		}
	} else if (!look.fontStyle.empty() && look.fontStyle != "Regular") {
		font_family family;
		font_style style;
		be_plain_font->GetFamilyAndStyle(&family, &style);
		if (font.SetFamilyAndStyle(family, look.fontStyle.c_str()) != B_OK) {
			if (look.fontStyle.find("Bold") != std::string::npos)
				font.SetFace(B_BOLD_FACE);
			else if (look.fontStyle.find("Italic") != std::string::npos)
				font.SetFace(B_ITALIC_FACE);
		}
	}
	font.SetSize(look.fontSize);
	return font;
}


rgb_color
PanelColor(const bdb::Template* tmpl, const bdb::Theme& theme)
{
	bdb::Color color;
	if (tmpl != NULL) {
		const Json* background = tmpl->Background();
		if (background != NULL
			&& bdb::ColorFromString(background->GetString("color"), color)) {
			return ToRgb(color);
		}
	}
	if (theme.Native())
		return ui_color(B_PANEL_BACKGROUND_COLOR);
	return ToRgb(theme.GetColor("panel"));
}


BString
FormatNumberValue(const Json& value, const Json& part)
{
	if (!value.IsNumber())
		return "";
	char buffer[64];
	snprintf(buffer, sizeof(buffer), "%.*f",
		int(part.GetInt("decimals", 0)), value.AsDouble());
	return buffer;
}


// #pragma mark - ViewCanvas


ViewCanvas::ViewCanvas(BView* view, float scale, BPoint origin)
	:
	fView(view),
	fScale(scale),
	fOrigin(origin),
	fClipDepth(0)
{
}


BRect
ViewCanvas::ToView(BRect rect) const
{
	// Whole pixels: lines and bevels stay crisp at any scale (theme 7.1).
	BRect result(floorf(fOrigin.x + rect.left * fScale + 0.5f),
		floorf(fOrigin.y + rect.top * fScale + 0.5f),
		floorf(fOrigin.x + (rect.right + 1) * fScale + 0.5f) - 1,
		floorf(fOrigin.y + (rect.bottom + 1) * fScale + 0.5f) - 1);
	return result;
}


BPoint
ViewCanvas::ToView(BPoint point) const
{
	return BPoint(fOrigin.x + point.x * fScale, fOrigin.y + point.y * fScale);
}


void
ViewCanvas::FillRect(BRect rect, rgb_color color)
{
	BRect target = ToView(rect);
	if (!target.IsValid())
		return;
	fView->SetHighColor(color);
	fView->FillRect(target);
}


void
ViewCanvas::FillRoundRect(BRect rect, float radius, rgb_color color)
{
	fView->SetHighColor(color);
	fView->FillRoundRect(ToView(rect), radius * fScale, radius * fScale);
}


void
ViewCanvas::StrokeRoundRect(BRect rect, float radius, float width,
	rgb_color color)
{
	fView->SetHighColor(color);
	float penSize = std::max(1.0f, floorf(width * fScale + 0.5f));
	fView->SetPenSize(penSize);
	BRect target = ToView(rect).InsetByCopy(penSize / 2 - 0.5f,
		penSize / 2 - 0.5f);
	fView->StrokeRoundRect(target, radius * fScale, radius * fScale);
	fView->SetPenSize(1);
}


void
ViewCanvas::FillEllipse(BRect rect, rgb_color color)
{
	fView->SetHighColor(color);
	fView->FillEllipse(ToView(rect));
}


void
ViewCanvas::StrokeLine(BPoint from, BPoint to, float width, rgb_color color)
{
	fView->SetHighColor(color);
	fView->SetPenSize(std::max(1.0f, width * fScale));
	fView->StrokeLine(ToView(from), ToView(to));
	fView->SetPenSize(1);
}


void
ViewCanvas::DrawString(const char* text, BPoint baseline, const BFont& font,
	rgb_color color)
{
	BFont scaled(font);
	scaled.SetSize(font.Size() * fScale);
	fView->SetFont(&scaled);
	fView->SetHighColor(color);
	fView->SetDrawingMode(B_OP_OVER);
	fView->DrawString(text, ToView(baseline));
}


float
ViewCanvas::StringWidth(const char* text, const BFont& font)
{
	return font.StringWidth(text);
}


void
ViewCanvas::DrawBitmap(const BBitmap* bitmap, BRect target, const char* path)
{
	if (bitmap == NULL)
		return;
	fView->SetDrawingMode(B_OP_ALPHA);
	fView->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	fView->DrawBitmap(bitmap, bitmap->Bounds(), ToView(target),
		B_FILTER_BITMAP_BILINEAR);
	fView->SetDrawingMode(B_OP_COPY);
}


void
ViewCanvas::PushClip(BRect rect)
{
	fView->PushState();
	BRegion region(ToView(rect));
	fView->ConstrainClippingRegion(&region);
	fClipDepth++;
}


void
ViewCanvas::PopClip()
{
	if (fClipDepth == 0)
		return;
	fView->PopState();
	fClipDepth--;
}


// #pragma mark - BitmapCache


BitmapCache::BitmapCache()
{
}


BitmapCache::~BitmapCache()
{
	Clear();
}


void
BitmapCache::Clear()
{
	for (auto& entry : fBitmaps)
		delete entry.second;
	fBitmaps.clear();
	fFailed.clear();
}


const BBitmap*
BitmapCache::Get(const std::string& path)
{
	auto found = fBitmaps.find(path);
	if (found != fBitmaps.end())
		return found->second;
	if (path.empty() || fFailed.count(path) != 0)
		return NULL;
	BBitmap* bitmap = BTranslationUtils::GetBitmapFile(path.c_str());
	if (bitmap == NULL) {
		fFailed.insert(path);
		return NULL;
	}
	fBitmaps[path] = bitmap;
	return bitmap;
}


static bool
IsHvif(const std::string& path)
{
	return path.size() > 5 && path.compare(path.size() - 5, 5, ".hvif") == 0;
}


const BBitmap*
BitmapCache::GetIcon(const std::string& path, int32 size)
{
	if (!IsHvif(path))
		return Get(path);
	char key[32];
	snprintf(key, sizeof(key), "#%d", (int)size);
	std::string id = path + key;
	auto found = fBitmaps.find(id);
	if (found != fBitmaps.end())
		return found->second;
	if (fFailed.count(id) != 0)
		return NULL;
	std::string data;
	if (!bdb::ReadWholeFile(path, 1024 * 1024, data)) {
		fFailed.insert(id);
		return NULL;
	}
	BBitmap* bitmap = new BBitmap(BRect(0, 0, size - 1, size - 1),
		B_RGBA32);
	if (BIconUtils::GetVectorIcon(
			reinterpret_cast<const uint8*>(data.data()), data.size(), bitmap)
			!= B_OK) {
		delete bitmap;
		fFailed.insert(id);
		return NULL;
	}
	fBitmaps[id] = bitmap;
	return bitmap;
}


// #pragma mark - frames


static rgb_color
Mix(rgb_color a, rgb_color b, float amount)
{
	return make_color(uint8(a.red + (b.red - a.red) * amount),
		uint8(a.green + (b.green - a.green) * amount),
		uint8(a.blue + (b.blue - a.blue) * amount), 255);
}


// One-pixel bevel ring: top/left in `topLeft`, bottom/right in
// `bottomRight`.
static void
BevelRing(Canvas& canvas, BRect rect, rgb_color topLeft,
	rgb_color bottomRight)
{
	canvas.FillRect(BRect(rect.left, rect.top, rect.right, rect.top),
		topLeft);
	canvas.FillRect(BRect(rect.left, rect.top, rect.left, rect.bottom),
		topLeft);
	canvas.FillRect(BRect(rect.left + 1, rect.bottom, rect.right,
		rect.bottom), bottomRight);
	canvas.FillRect(BRect(rect.right, rect.top + 1, rect.right,
		rect.bottom), bottomRight);
}


static void
FlatRing(Canvas& canvas, BRect rect, int width, rgb_color color)
{
	for (int i = 0; i < width && rect.IsValid(); i++) {
		BevelRing(canvas, rect, color, color);
		rect.InsetBy(1, 1);
	}
}


BRect
FrameInset(BRect rect, const bdb::PartLook& look)
{
	if (look.NoBorder())
		return rect;
	int width = look.borderWidth;
	if (look.look == "raised2" || look.look == "sunken2")
		width = std::max(width, 2);
	if (look.look == "etched")
		width = std::max(width, 2);
	return rect.InsetByCopy(width, width);
}


BRect
PaintFrame(Canvas& canvas, const bdb::Theme& theme, BRect rect,
	const bdb::PartLook& look, bool pressed)
{
	std::string kind = pressed && !look.pressedLook.empty()
		? look.pressedLook : look.look;
	rgb_color light = ToRgb(theme.GetColor("bevel_light"));
	rgb_color dark = ToRgb(theme.GetColor("bevel_dark"));
	rgb_color darker = ToRgb(theme.GetColor("bevel_darker"));
	rgb_color line = ToRgb(look.borderColor);
	float radius = look.radius;

	// The fill first (inside rounded corners for "round").
	if (look.hasFill) {
		rgb_color fill = ToRgb(look.fill);
		if (pressed && kind == look.look)
			fill = Mix(fill, darker, 0.15f);
		if (kind == "round")
			canvas.FillRoundRect(rect, radius, fill);
		else
			canvas.FillRect(rect, fill);
	}
	if (look.NoBorder())
		return rect;

	int width = look.borderWidth;
	// A color chosen by the part draws a plain line (UI 6.12).
	if (look.borderColorSet && kind != "round")
		kind = "flat";
	if (kind == "flat") {
		FlatRing(canvas, rect, width, line);
		return rect.InsetByCopy(width, width);
	}
	if (kind == "round") {
		canvas.StrokeRoundRect(rect, radius, width,
			look.borderColorSet ? line : darker);
		return rect.InsetByCopy(width + radius / 3, width);
	}
	if (kind == "etched") {
		BevelRing(canvas, rect, dark, light);
		BevelRing(canvas, rect.InsetByCopy(1, 1), light, dark);
		return rect.InsetByCopy(2, 2);
	}
	bool sunken = kind == "sunken" || kind == "sunken2";
	bool twice = kind == "raised2" || kind == "sunken2";
	BRect ring = rect;
	if (twice) {
		// Outer line in bevel_darker (NeXTSTEP / Windows 95, theme 4.3).
		if (sunken)
			BevelRing(canvas, ring, dark, light);
		else
			BevelRing(canvas, ring, light, darker);
		ring.InsetBy(1, 1);
		if (sunken)
			BevelRing(canvas, ring, darker, Mix(light, dark, 0.3f));
		else
			BevelRing(canvas, ring, Mix(light, dark, 0.3f), dark);
		ring.InsetBy(1, 1);
		for (int i = 2; i < width && ring.IsValid(); i++) {
			BevelRing(canvas, ring, sunken ? dark : light,
				sunken ? light : dark);
			ring.InsetBy(1, 1);
		}
		return ring;
	}
	for (int i = 0; i < width && ring.IsValid(); i++) {
		if (sunken)
			BevelRing(canvas, ring, i == 0 ? dark : darker, light);
		else
			BevelRing(canvas, ring, light, i == 0 ? darker : dark);
		ring.InsetBy(1, 1);
	}
	return ring;
}


// #pragma mark - text


// Splits into lines; with `wrap`, long lines break at the width (at
// spaces for Latin text, anywhere for Japanese).
static std::vector<std::string>
LayoutLines(Canvas& canvas, const std::string& text, const BFont& font,
	float width, bool wrap)
{
	std::vector<std::string> lines;
	size_t start = 0;
	while (start <= text.size()) {
		size_t end = text.find('\n', start);
		if (end == std::string::npos)
			end = text.size();
		std::string line = text.substr(start, end - start);
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (!wrap || canvas.StringWidth(line.c_str(), font) <= width) {
			lines.push_back(line);
		} else {
			std::string current;
			size_t lastSpace = std::string::npos;
			for (size_t i = 0; i < line.size();) {
				size_t length = 1;
				unsigned char c = line[i];
				if (c >= 0xf0)
					length = 4;
				else if (c >= 0xe0)
					length = 3;
				else if (c >= 0xc0)
					length = 2;
				std::string next = current + line.substr(i, length);
				if (!current.empty()
					&& canvas.StringWidth(next.c_str(), font) > width) {
					if (c != ' ' && lastSpace != std::string::npos
						&& lastSpace > 0) {
						lines.push_back(current.substr(0, lastSpace));
						current = current.substr(lastSpace + 1);
					} else {
						lines.push_back(current);
						current.clear();
						if (c == ' ') {
							i += length;
							lastSpace = std::string::npos;
							continue;
						}
					}
					lastSpace = std::string::npos;
					next = current + line.substr(i, length);
				}
				if (c == ' ')
					lastSpace = current.size();
				current = next;
				i += length;
			}
			lines.push_back(current);
		}
		start = end + 1;
	}
	return lines;
}


void
DrawTextBlock(Canvas& canvas, const char* text, BRect rect,
	const BFont& font, rgb_color color, bool wrap, int alignment,
	bool centerVertically)
{
	if (text == NULL || text[0] == '\0' || !rect.IsValid())
		return;
	font_height height;
	font.GetHeight(&height);
	float lineHeight = ceilf(height.ascent + height.descent + height.leading);
	std::vector<std::string> lines = LayoutLines(canvas, text, font,
		rect.Width() - 2, wrap);
	float total = lineHeight * lines.size();
	float y = rect.top + 1;
	if (centerVertically)
		y = rect.top + (rect.Height() + 1 - total) / 2;
	canvas.PushClip(rect);
	for (const std::string& line : lines) {
		if (y > rect.bottom)
			break;
		float x = rect.left + 2;
		if (alignment != B_ALIGN_LEFT) {
			float lineWidth = canvas.StringWidth(line.c_str(), font);
			x = alignment == B_ALIGN_RIGHT ? rect.right - 2 - lineWidth
				: rect.left + (rect.Width() + 1 - lineWidth) / 2;
		}
		canvas.DrawString(line.c_str(), BPoint(x, y + height.ascent), font,
			color);
		y += lineHeight;
	}
	canvas.PopClip();
}


// #pragma mark - images


static void
DrawFitted(Canvas& canvas, const BBitmap* bitmap, BRect rect,
	const std::string& fit, const char* path)
{
	BRect source = bitmap->Bounds();
	float sourceWidth = source.Width() + 1;
	float sourceHeight = source.Height() + 1;
	float width = rect.Width() + 1;
	float height = rect.Height() + 1;
	canvas.PushClip(rect);
	if (fit == "tile") {
		for (float y = rect.top; y <= rect.bottom; y += sourceHeight) {
			for (float x = rect.left; x <= rect.right; x += sourceWidth) {
				canvas.DrawBitmap(bitmap, BRect(x, y, x + sourceWidth - 1,
					y + sourceHeight - 1), path);
			}
		}
	} else {
		BRect target = rect;
		if (fit != "stretch") {
			float scaleX = width / sourceWidth;
			float scaleY = height / sourceHeight;
			float scale = fit == "cover" ? std::max(scaleX, scaleY)
				: std::min(scaleX, scaleY);
			float drawWidth = sourceWidth * scale;
			float drawHeight = sourceHeight * scale;
			target.left = rect.left + (width - drawWidth) / 2;
			target.top = rect.top + (height - drawHeight) / 2;
			target.right = target.left + drawWidth - 1;
			target.bottom = target.top + drawHeight - 1;
		}
		canvas.DrawBitmap(bitmap, target, path);
	}
	canvas.PopClip();
}


void
PaintBackground(Canvas& canvas, const PaintContext& context)
{
	const bdb::Template* tmpl = context.tmpl;
	if (tmpl == NULL || context.theme == NULL)
		return;
	BRect card(0, 0, tmpl->Width() - 1, tmpl->Height() - 1);
	bool print = context.mode == kPaintPrint;
	if (print && !context.printBackground) {
		canvas.FillRect(card, make_color(255, 255, 255));
		return;
	}
	canvas.FillRect(card, PanelColor(tmpl, *context.theme));
	const Json* background = tmpl->Background();
	if (background == NULL || context.stack == NULL
		|| context.bitmaps == NULL) {
		return;
	}
	const Json* image = background->Find("image");
	if (image == NULL || !bdb::IsMediaRef(*image))
		return;
	std::string path = context.stack->MediaPath(*image);
	const BBitmap* bitmap = context.bitmaps->Get(path);
	if (bitmap != NULL) {
		DrawFitted(canvas, bitmap, card, background->GetString("fit",
			"cover"), path.c_str());
	}
}


// #pragma mark - parts


const char*
KindName(const std::string& kind)
{
	if (kind == bdb::kKindBlank)
		return str::kKindBlank;
	if (kind == bdb::kKindText)
		return str::kKindText;
	if (kind == bdb::kKindNumber)
		return str::kKindNumber;
	if (kind == bdb::kKindBool)
		return str::kKindBool;
	if (kind == bdb::kKindImage)
		return str::kKindImage;
	if (kind == bdb::kKindLabel)
		return str::kKindLabel;
	if (kind == bdb::kKindButton)
		return str::kKindButton;
	if (kind == bdb::kKindCounter)
		return str::kKindCounter;
	return str::kKindUnknown;
}


static void
PaintSwitch(Canvas& canvas, const bdb::Theme& theme, BRect rect,
	const std::string& look, int state, bool focus)
{
	// state: 1 on, 0 off, -1 "any" (find mode)
	rgb_color accent = ToRgb(theme.GetColor("accent"));
	rgb_color dark = ToRgb(theme.GetColor("bevel_dark"));
	rgb_color darker = ToRgb(theme.GetColor("bevel_darker"));
	rgb_color light = ToRgb(theme.GetColor("bevel_light"));
	rgb_color fill = ToRgb(theme.GetColor("control_fill"));
	float midY = (rect.top + rect.bottom) / 2;
	if (look == "checkbox") {
		float size = std::min(14.0f, rect.Height() - 2);
		BRect box(rect.left + 3, midY - size / 2, rect.left + 3 + size - 1,
			midY + size / 2 - 1);
		canvas.FillRect(box, fill);
		BevelRing(canvas, box, dark, light);
		BevelRing(canvas, box.InsetByCopy(1, 1), darker, Mix(light, dark,
			0.3f));
		if (state == 1) {
			BRect inner = box.InsetByCopy(3, 3);
			canvas.StrokeLine(BPoint(inner.left, (inner.top + inner.bottom)
				/ 2), BPoint(inner.left + inner.Width() / 3, inner.bottom), 2,
				darker);
			canvas.StrokeLine(BPoint(inner.left + inner.Width() / 3,
				inner.bottom), BPoint(inner.right, inner.top), 2, darker);
		} else if (state < 0) {
			canvas.FillRect(BRect(box.left + 4, midY - 1, box.right - 4,
				midY), dark);
		}
		return;
	}
	// A track with a knob; the track shows the accent color when on.
	float height = std::min(16.0f, rect.Height() - 4);
	float width = std::min(rect.Width() - 6, height * 2.2f);
	BRect track(rect.left + 3, midY - height / 2, rect.left + 3 + width,
		midY + height / 2);
	canvas.FillRoundRect(track, height / 2, state == 1 ? accent
		: Mix(fill, dark, 0.35f));
	canvas.StrokeRoundRect(track, height / 2, 1, darker);
	BRect knob(track.left + 1, track.top + 1, track.left + height - 1,
		track.bottom - 1);
	if (state == 1)
		knob.OffsetTo(track.right - height + 1, knob.top);
	else if (state < 0)
		knob.OffsetTo(track.left + (width - height) / 2, knob.top);
	canvas.FillEllipse(knob, light);
	canvas.FillEllipse(knob.InsetByCopy(0.5f, 0.5f), Mix(light, dark,
		0.15f));
	(void)focus;
}


static void
PaintIconAndText(Canvas& canvas, const PaintContext& context,
	const Json& part, BRect inner, const BFont& font, rgb_color color,
	bool center)
{
	std::string text = part.GetString("text");
	const Json* icon = part.Find("icon");
	const BBitmap* bitmap = NULL;
	std::string iconPath;
	int32 iconSize = 0;
	std::string placement = "left";
	if (icon != NULL && bdb::IsMediaRef(*icon) && context.stack != NULL
		&& context.bitmaps != NULL) {
		iconSize = int32(icon->GetInt("size", 24));
		placement = icon->GetString("placement", "left");
		iconPath = context.stack->MediaPath(*icon);
		bitmap = context.bitmaps->GetIcon(iconPath, iconSize);
	}
	font_height height;
	font.GetHeight(&height);
	float lineHeight = ceilf(height.ascent + height.descent);
	if (bitmap == NULL) {
		DrawTextBlock(canvas, text.c_str(), inner, font, color,
			bdb::part::Kind(part) == bdb::kKindLabel, center ? B_ALIGN_CENTER
				: B_ALIGN_LEFT, center);
		return;
	}
	float textWidth = text.empty() ? 0 : canvas.StringWidth(text.c_str(),
		font);
	BRect iconRect;
	if (placement == "top") {
		float total = iconSize + (text.empty() ? 0 : 2 + lineHeight);
		float top = inner.top + (inner.Height() + 1 - total) / 2;
		float left = inner.left + (inner.Width() + 1 - iconSize) / 2;
		iconRect.Set(left, top, left + iconSize - 1, top + iconSize - 1);
		if (!text.empty()) {
			BRect textRect(inner.left, iconRect.bottom + 2, inner.right,
				iconRect.bottom + 2 + lineHeight);
			DrawTextBlock(canvas, text.c_str(), textRect, font, color, false,
				B_ALIGN_CENTER, false);
		}
	} else {
		float total = iconSize + (text.empty() ? 0 : 4 + textWidth);
		float left = center ? inner.left + (inner.Width() + 1 - total) / 2
			: inner.left + 2;
		float top = inner.top + (inner.Height() + 1 - iconSize) / 2;
		iconRect.Set(left, top, left + iconSize - 1, top + iconSize - 1);
		if (!text.empty()) {
			BRect textRect(iconRect.right + 4, inner.top, inner.right,
				inner.bottom);
			DrawTextBlock(canvas, text.c_str(), textRect, font, color, false,
				B_ALIGN_LEFT, true);
		}
	}
	canvas.PushClip(inner);
	canvas.DrawBitmap(bitmap, iconRect, iconPath.c_str());
	canvas.PopClip();
}


static bool
LinkWorks(const PaintContext& context, const Json& part)
{
	if (!bdb::HasLink(part) || context.tmpl == NULL)
		return false;
	std::string url;
	return bdb::ResolveLink(part, *context.tmpl, context.card, url)
		== bdb::LinkCheck::kOk;
}


void
PaintPart(Canvas& canvas, const PaintContext& context, const Json& part)
{
	const bdb::Theme& theme = context.theme != NULL ? *context.theme
		: bdb::Theme::Default();
	std::string kind = bdb::part::Kind(part);
	std::string id = bdb::part::Id(part);
	bdb::Rect r = bdb::part::GetRect(part);
	BRect rect(r.x, r.y, r.x + r.w - 1, r.y + r.h - 1);
	bool edit = context.mode == kPaintEdit;
	bool print = context.mode == kPaintPrint;
	bdb::PartLook look = theme.Resolve(part);
	BFont font = FontForLook(look);
	rgb_color textColor = ToRgb(look.textColor);
	rgb_color disabled = ToRgb(theme.GetColor("text_disabled"));

	if (kind == bdb::kKindBlank) {
		if (!edit)
			return;
		// Dashed frame and "未設定" (UI 6.4).
		rgb_color line = disabled;
		for (float x = rect.left; x <= rect.right; x += 6) {
			float end = std::min(rect.right, x + 2);
			canvas.FillRect(BRect(x, rect.top, end, rect.top), line);
			canvas.FillRect(BRect(x, rect.bottom, end, rect.bottom), line);
		}
		for (float y = rect.top; y <= rect.bottom; y += 6) {
			float end = std::min(rect.bottom, y + 2);
			canvas.FillRect(BRect(rect.left, y, rect.left, end), line);
			canvas.FillRect(BRect(rect.right, y, rect.right, end), line);
		}
		BString caption = bdb::part::Name(part).c_str();
		if (!caption.IsEmpty())
			caption << " ";
		caption << "[" << str::kKindBlank.Get() << "]";
		DrawTextBlock(canvas, caption.String(), rect.InsetByCopy(2, 1), font,
			disabled, false, B_ALIGN_LEFT, false);
		return;
	}
	if (!bdb::IsKnownKind(kind)) {
		// Unknown kind: a placeholder (data spec 5.5).
		FlatRing(canvas, rect, 1, disabled);
		DrawTextBlock(canvas, str::kKindUnknown, rect.InsetByCopy(2, 1), font,
			disabled, false, B_ALIGN_LEFT, false);
		return;
	}
	if (print && (kind == bdb::kKindButton || kind == bdb::kKindCounter))
		return;	// not printed (I/O spec 4.2)

	if (kind == bdb::kKindLabel) {
		BRect inner = PaintFrame(canvas, theme, rect, look, false);
		rgb_color color = textColor;
		if (!edit && LinkWorks(context, part) && !look.ownFont)
			color = ToRgb(theme.GetColor("link"));
		PaintIconAndText(canvas, context, part, inner, font, color, false);
		return;
	}
	if (kind == bdb::kKindButton) {
		bool pressed = !edit && context.pressedPart == id;
		bool enabled = edit || !context.buttonEnabled
			|| context.buttonEnabled(part);
		BRect inner = PaintFrame(canvas, theme, rect, look, pressed);
		if (!edit && context.hoverPart == id && !pressed && enabled
			&& look.hasFill) {
			// Slightly lighter under the pointer (theme 4.3).
			canvas.FillRect(inner, Mix(ToRgb(look.fill),
				make_color(255, 255, 255), 0.25f));
		}
		if (pressed)
			inner.OffsetBy(1, 1);
		PaintIconAndText(canvas, context, part, inner, font,
			enabled ? textColor : disabled, true);
		return;
	}
	if (kind == bdb::kKindCounter) {
		BRect inner = PaintFrame(canvas, theme, rect, look, false);
		char text[48];
		if (edit)
			snprintf(text, sizeof(text), "1 / 1");
		else {
			snprintf(text, sizeof(text), "%d / %d", (int)context.position,
				(int)context.count);
		}
		DrawTextBlock(canvas, text, inner, font, textColor, false,
			B_ALIGN_CENTER, true);
		return;
	}

	// Value parts: text, number, bool, image.
	BRect inner = PaintFrame(canvas, theme, rect, look, false);
	if (!edit && context.focusPart == id && !print) {
		// Keyboard focus in the theme's focus color (theme 4.2).
		FlatRing(canvas, rect, std::max(1, theme.FocusWidth()),
			ToRgb(theme.FocusColor()));
	}
	if (edit) {
		// Names and types instead of values (UI 4.1, 6.4).
		BString caption = bdb::part::Name(part).c_str();
		caption << "  [" << KindName(kind) << "]";
		DrawTextBlock(canvas, caption.String(), inner, font,
			Mix(textColor, disabled, 0.6f), false, B_ALIGN_LEFT, false);
		return;
	}

	const Json* value = NULL;
	if (context.card != NULL)
		value = &bdb::EffectiveValue(*context.card, part);
	if (kind == bdb::kKindBool) {
		int state = 0;
		if (context.mode == kPaintFind) {
			auto found = context.findBool.find(id);
			state = found != context.findBool.end() ? found->second : -1;
		} else if (value != NULL && value->IsBool() && value->AsBool())
			state = 1;	// null is shown as off (UI 5.3)
		PaintSwitch(canvas, theme, inner, bdb::part::BoolLook(part), state,
			context.focusPart == id);
		if (context.mode == kPaintFind && state != -1) {
			BRect textRect = inner;
			textRect.left += std::min(inner.Width() / 2, 44.0f);
			DrawTextBlock(canvas, state == 1 ? str::kBoolOn.Get()
				: str::kBoolOff.Get(), textRect, font, textColor, false,
				B_ALIGN_LEFT, true);
		} else if (context.mode == kPaintFind) {
			BRect textRect = inner;
			textRect.left += std::min(inner.Width() / 2, 44.0f);
			DrawTextBlock(canvas, str::kBoolAny, textRect, font, disabled,
				false, B_ALIGN_LEFT, true);
		}
		return;
	}
	if (kind == bdb::kKindImage) {
		if (context.mode == kPaintFind)
			return;	// images are not search conditions (UI 8)
		if (value != NULL && bdb::IsMediaRef(*value)
			&& context.stack != NULL && context.bitmaps != NULL) {
			std::string path = context.stack->MediaPath(*value);
			const BBitmap* bitmap = context.bitmaps->Get(path);
			if (bitmap != NULL) {
				DrawFitted(canvas, bitmap, inner, part.GetString("fit",
					"contain"), path.c_str());
			} else if (!print) {
				DrawTextBlock(canvas, str::kImageMissing, inner, font,
					disabled, true, B_ALIGN_CENTER, true);
			}
		} else if (!print && context.mode == kPaintPlay) {
			DrawTextBlock(canvas, str::kImageDrop, inner, font, disabled,
				true, B_ALIGN_CENTER, true);
		}
		return;
	}
	if (!context.drawValues || value == NULL)
		return;
	BString text;
	if (kind == bdb::kKindNumber)
		text = FormatNumberValue(*value, part);
	else
		text = value->IsString() ? value->AsString().c_str() : "";
	bool multiline = kind == bdb::kKindText
		&& part.Find("multiline") != NULL && part.Find("multiline")->AsBool();
	DrawTextBlock(canvas, text.String(), inner.InsetByCopy(1, 1), font,
		textColor, multiline, kind == bdb::kKindNumber ? B_ALIGN_RIGHT
			: B_ALIGN_LEFT, !multiline);
}


void
PaintCard(Canvas& canvas, const PaintContext& context)
{
	PaintBackground(canvas, context);
	if (context.tmpl == NULL)
		return;
	for (const Json& part : context.tmpl->Parts()) {
		if (context.hiddenParts.count(bdb::part::Id(part)) == 0)
			PaintPart(canvas, context, part);
	}
}
