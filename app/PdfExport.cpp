/*
 * BDB card database - RenkuOS application
 */
#include "PdfExport.h"

#include <Alert.h>
#include <Autolock.h>
#include <Bitmap.h>
#include <Button.h>
#include <CheckBox.h>
#include <Entry.h>
#include <FilePanel.h>
#include <Font.h>
#include <IconUtils.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Shape.h>
#include <StatusBar.h>
#include <Window.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#include "CardPainter.h"
#include "Constants.h"
#include "Limits.h"
#include "Model.h"
#include "Pdf.h"
#include "Stack.h"
#include "StackDocument.h"
#include "Store.h"
#include "Strings.h"
#include "Text.h"


using bdb::Json;

static const uint32 kMsgOptionsSave = 'poSv';
static const uint32 kMsgProgressCancel = 'ppCn';
// 1 card pixel = 0.75 point (96 dpi, data spec 7).
static const double kPointsPerPixel = 72.0 / 96.0;


// #pragma mark - glyphs


namespace {


class ShapeConverter : public BShapeIterator {
public:
	ShapeConverter(bdb::PdfPath& path, double scale)
		:
		fPath(path),
		fScale(scale)
	{
	}

	virtual status_t IterateMoveTo(BPoint* point)
	{
		fPath.MoveTo(point->x * fScale, -point->y * fScale);
		return B_OK;
	}

	virtual status_t IterateLineTo(int32 count, BPoint* points)
	{
		for (int32 i = 0; i < count; i++)
			fPath.LineTo(points[i].x * fScale, -points[i].y * fScale);
		return B_OK;
	}

	virtual status_t IterateBezierTo(int32 count, BPoint* points)
	{
		for (int32 i = 0; i < count; i++) {
			BPoint* p = points + i * 3;
			fPath.CurveTo(p[0].x * fScale, -p[0].y * fScale,
				p[1].x * fScale, -p[1].y * fScale,
				p[2].x * fScale, -p[2].y * fScale);
		}
		return B_OK;
	}

	virtual status_t IterateClose()
	{
		fPath.Close();
		return B_OK;
	}

private:
	bdb::PdfPath&	fPath;
	double			fScale;
};


// Glyph outlines from the system font engine (app_server).
class SystemGlyphs : public bdb::PdfGlyphSource {
public:
	virtual bool Glyph(const bdb::PdfFont& pdfFont, uint32_t codePoint,
		double& advance, bdb::PdfPath& outline)
	{
		BFont font(be_plain_font);
		if (!pdfFont.family.empty()) {
			if (font.SetFamilyAndStyle(pdfFont.family.c_str(),
					pdfFont.style.c_str()) != B_OK) {
				font.SetFamilyAndStyle(pdfFont.family.c_str(), NULL);
			}
		}
		const float kSize = 100.0f;
		font.SetSize(kSize);
		char utf8[5] = { 0 };
		if (codePoint < 0x80)
			utf8[0] = char(codePoint);
		else if (codePoint < 0x800) {
			utf8[0] = char(0xc0 | (codePoint >> 6));
			utf8[1] = char(0x80 | (codePoint & 0x3f));
		} else if (codePoint < 0x10000) {
			utf8[0] = char(0xe0 | (codePoint >> 12));
			utf8[1] = char(0x80 | ((codePoint >> 6) & 0x3f));
			utf8[2] = char(0x80 | (codePoint & 0x3f));
		} else {
			utf8[0] = char(0xf0 | (codePoint >> 18));
			utf8[1] = char(0x80 | ((codePoint >> 12) & 0x3f));
			utf8[2] = char(0x80 | ((codePoint >> 6) & 0x3f));
			utf8[3] = char(0x80 | (codePoint & 0x3f));
		}
		float escapement = 0;
		font.GetEscapements(utf8, 1, &escapement);
		advance = escapement * 1000.0;
		if (codePoint == ' ' || codePoint == 0x3000 || codePoint < 0x20)
			return true;
		BShape* shape = new BShape();
		BShape* shapes[1] = { shape };
		_FontWithGlyph(font, utf8).GetGlyphShapes(utf8, 1, shapes);
		// Outline units: size 100 -> 1000 units per em.
		ShapeConverter converter(outline, 1000.0 / kSize);
		converter.Iterate(shape);
		delete shape;
		return true;
	}

private:
	// GetGlyphShapes() uses only the font itself, while drawing falls back
	// to other fonts for missing characters (Japanese, for instance). Same
	// families and order as the app_server (GlyphLayoutEngine).
	static BFont _FontWithGlyph(const BFont& font, const char* utf8)
	{
		bool has = false;
		font.GetHasGlyphs(utf8, 1, &has, false);
		if (has)
			return font;
		static const char* kFallbacks[] = {
			"Noto Sans", "Noto Sans Thai", "Noto Sans CJK JP",
			"Noto Sans Cherokee", "Noto Sans Symbols",
			"Noto Sans Symbols 2", "Noto Emoji"
		};
		font_family family;
		font_style style;
		font.GetFamilyAndStyle(&family, &style);
		const char* styles[] = { style, "Regular", NULL };
		for (const char* fallbackStyle : styles) {
			for (const char* fallback : kFallbacks) {
				BFont candidate(font);
				if (candidate.SetFamilyAndStyle(fallback, fallbackStyle)
						!= B_OK) {
					continue;
				}
				candidate.GetHasGlyphs(utf8, 1, &has, false);
				if (has)
					return candidate;
			}
		}
		return font;
	}
};


bdb::PdfFont
PdfFontFor(const BFont& font)
{
	font_family family;
	font_style style;
	font.GetFamilyAndStyle(&family, &style);
	bdb::PdfFont pdfFont;
	pdfFont.family = family;
	pdfFont.style = style;
	return pdfFont;
}


bool
IsJpegPath(const char* path)
{
	if (path == NULL)
		return false;
	BString name(path);
	return name.IEndsWith(".jpg") || name.IEndsWith(".jpeg");
}


// RGB and alpha of a bitmap (any color space the system can convert).
bool
BitmapPixels(const BBitmap* source, std::vector<uint8_t>& rgb,
	std::vector<uint8_t>& alpha, int& width, int& height)
{
	BRect bounds = source->Bounds();
	width = int(bounds.Width() + 1);
	height = int(bounds.Height() + 1);
	std::unique_ptr<BBitmap> converted;
	const BBitmap* bitmap = source;
	color_space space = source->ColorSpace();
	if (space != B_RGBA32 && space != B_RGB32) {
		converted.reset(new BBitmap(bounds, B_RGBA32));
		if (converted->ImportBits(source) != B_OK)
			return false;
		bitmap = converted.get();
		space = B_RGBA32;
	}
	rgb.resize(size_t(width) * height * 3);
	alpha.resize(size_t(width) * height);
	const uint8* bits = static_cast<const uint8*>(bitmap->Bits());
	int32 rowBytes = bitmap->BytesPerRow();
	for (int y = 0; y < height; y++) {
		const uint8* row = bits + y * rowBytes;
		for (int x = 0; x < width; x++) {
			size_t index = size_t(y) * width + x;
			// B_RGBA32 is B, G, R, A in memory.
			rgb[index * 3] = row[x * 4 + 2];
			rgb[index * 3 + 1] = row[x * 4 + 1];
			rgb[index * 3 + 2] = row[x * 4];
			alpha[index] = space == B_RGBA32 ? row[x * 4 + 3] : 255;
		}
	}
	return true;
}


}	// namespace


// #pragma mark - PdfCanvas


class PdfCanvas : public Canvas {
public:
	PdfCanvas(bdb::PdfWriter& writer, double scale, double offsetX,
		double offsetY)
		:
		fWriter(writer),
		fScale(scale),
		fX(offsetX),
		fY(offsetY)
	{
	}

	double X(float x) const { return fX + x * fScale; }
	double Y(float y) const { return fY + y * fScale; }
	// Card pixels cover [left, right + 1).
	double W(BRect rect) const { return (rect.Width() + 1) * fScale; }
	double H(BRect rect) const { return (rect.Height() + 1) * fScale; }

	virtual void FillRect(BRect rect, rgb_color color)
	{
		if (!rect.IsValid())
			return;
		fWriter.SetFillColor(FromRgb(color));
		fWriter.FillRect(X(rect.left), Y(rect.top), W(rect), H(rect));
	}

	virtual void FillRoundRect(BRect rect, float radius, rgb_color color)
	{
		bdb::PdfPath path;
		path.AddRoundRect(X(rect.left), Y(rect.top), W(rect), H(rect),
			radius * fScale);
		fWriter.SetFillColor(FromRgb(color));
		fWriter.FillPath(path);
	}

	virtual void StrokeRoundRect(BRect rect, float radius, float width,
		rgb_color color)
	{
		double line = width * fScale;
		bdb::PdfPath path;
		path.AddRoundRect(X(rect.left) + line / 2, Y(rect.top) + line / 2,
			W(rect) - line, H(rect) - line, radius * fScale);
		fWriter.SetStrokeColor(FromRgb(color));
		fWriter.SetLineWidth(line);
		fWriter.StrokePath(path);
	}

	virtual void FillEllipse(BRect rect, rgb_color color)
	{
		double cx = X(rect.left) + W(rect) / 2;
		double cy = Y(rect.top) + H(rect) / 2;
		double rx = W(rect) / 2;
		double ry = H(rect) / 2;
		double k = 0.5522847498;
		bdb::PdfPath path;
		path.MoveTo(cx + rx, cy);
		path.CurveTo(cx + rx, cy + ry * k, cx + rx * k, cy + ry, cx, cy + ry);
		path.CurveTo(cx - rx * k, cy + ry, cx - rx, cy + ry * k, cx - rx, cy);
		path.CurveTo(cx - rx, cy - ry * k, cx - rx * k, cy - ry, cx, cy - ry);
		path.CurveTo(cx + rx * k, cy - ry, cx + rx, cy - ry * k, cx + rx, cy);
		path.Close();
		fWriter.SetFillColor(FromRgb(color));
		fWriter.FillPath(path);
	}

	virtual void StrokeLine(BPoint from, BPoint to, float width,
		rgb_color color)
	{
		fWriter.SetStrokeColor(FromRgb(color));
		fWriter.SetLineWidth(width * fScale);
		fWriter.StrokeLine(X(from.x), Y(from.y), X(to.x), Y(to.y));
	}

	virtual void DrawString(const char* text, BPoint baseline,
		const BFont& font, rgb_color color)
	{
		fWriter.SetFillColor(FromRgb(color));
		fWriter.DrawText(text, X(baseline.x), Y(baseline.y),
			PdfFontFor(font), font.Size() * fScale);
	}

	virtual float StringWidth(const char* text, const BFont& font)
	{
		// The screen's measure, so that lines break the same way.
		return font.StringWidth(text);
	}

	virtual void DrawBitmap(const BBitmap* bitmap, BRect target,
		const char* path)
	{
		int image = -1;
		BString key = path != NULL ? path : "";
		if (key.IEndsWith(".hvif")) {
			// Vector icons: rendered sharp for print.
			int32 size = int32(std::min(512.0, std::max(32.0,
				ceil(std::max(W(target), H(target)) * 4))));
			key << "#" << size;
			image = fWriter.FindImage(key.String());
			if (image < 0) {
				std::string data;
				if (bdb::ReadWholeFile(path, 1024 * 1024, data)) {
					BBitmap icon(BRect(0, 0, size - 1, size - 1), B_RGBA32);
					if (BIconUtils::GetVectorIcon(
							reinterpret_cast<const uint8*>(data.data()),
							data.size(), &icon) == B_OK) {
						std::vector<uint8_t> rgb, alpha;
						int width, height;
						if (BitmapPixels(&icon, rgb, alpha, width, height)) {
							image = fWriter.AddRgb(key.String(), rgb, alpha,
								width, height);
						}
					}
				}
			}
		} else if (IsJpegPath(path)) {
			// The JPEG file itself: no recompression (I/O 4.2).
			image = fWriter.FindImage(key.String());
			if (image < 0) {
				std::string data;
				int width, height, components;
				if (bdb::ReadWholeFile(path, 64 * 1024 * 1024, data)
					&& bdb::PdfWriter::JpegInfo(data, width, height,
						components)) {
					image = fWriter.AddJpeg(key.String(), data, width, height,
						components);
				}
			}
		}
		if (image < 0 && bitmap != NULL) {
			if (key.IsEmpty() || key.IEndsWith(".hvif")) {
				key.SetToFormat("bitmap:%p", (void*)bitmap);
			}
			image = fWriter.FindImage(key.String());
			if (image < 0) {
				std::vector<uint8_t> rgb, alpha;
				int width, height;
				if (BitmapPixels(bitmap, rgb, alpha, width, height)) {
					image = fWriter.AddRgb(key.String(), rgb, alpha, width,
						height);
				}
			}
		}
		if (image >= 0) {
			fWriter.DrawImage(image, X(target.left), Y(target.top), W(target),
				H(target));
		}
	}

	virtual void PushClip(BRect rect)
	{
		fWriter.Save();
		fWriter.ClipRect(X(rect.left), Y(rect.top), W(rect), H(rect));
	}

	virtual void PopClip()
	{
		fWriter.Restore();
	}

	virtual bool IsPrinting() const { return true; }

	virtual float PixelSize() const { return 0.25f; }

private:
	bdb::PdfWriter&	fWriter;
	double			fScale;
	double			fX;
	double			fY;
};


// #pragma mark - progress


class ProgressWindow : public BWindow {
public:
	ProgressWindow(volatile bool* cancel)
		:
		BWindow(BRect(0, 0, 360, 100), str::kPdfTitle, B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_NOT_CLOSABLE
				| B_AUTO_UPDATE_SIZE_LIMITS),
		fCancel(cancel)
	{
		fBar = new BStatusBar("progress", "");
		BButton* cancelButton = new BButton("cancel", str::kCancel,
			new BMessage(kMsgProgressCancel));
		BLayoutBuilder::Group<>(this, B_VERTICAL)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(fBar)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(cancelButton)
			.End();
		CenterOnScreen();
	}

	void SetProgress(int32 done, int32 total)
	{
		BString text;
		text.SetToFormat(str::kPdfProgress.Get(), (int)done, (int)total);
		fBar->SetMaxValue(total);
		fBar->SetTo(done, text);
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == kMsgProgressCancel) {
			*fCancel = true;
			return;
		}
		BWindow::MessageReceived(message);
	}

private:
	BStatusBar*		fBar;
	volatile bool*	fCancel;
};


// #pragma mark - export


namespace {


struct ExportState {
	PdfJob*			job;
	volatile bool	cancel = false;
	ProgressWindow*	progress = NULL;
};


void
PageSizeFor(const bdb::Template* tmpl, double& width, double& height)
{
	// The template's paper; otherwise the card size at 96 dpi (I/O 4.2).
	int32 w = tmpl != NULL ? tmpl->Width() : bdb::kDefaultCardWidth;
	int32 h = tmpl != NULL ? tmpl->Height() : bdb::kDefaultCardHeight;
	if (tmpl != NULL && tmpl->PaperSize() != "custom") {
		bdb::PaperPixels(tmpl->PaperSize(), tmpl->PaperOrientation(), w, h);
	}
	width = w * kPointsPerPixel;
	height = h * kPointsPerPixel;
}


bool
ExportCards(ExportState& state, bdb::PdfWriter& writer, BitmapCache& bitmaps)
{
	PdfJob* job = state.job;
	int32 total = int32(job->cardIds.size());
	for (int32 i = 0; i < total; i++) {
		if (state.cancel)
			return false;
		{
			DocLocker lock(job->document);
			bdb::Stack& stack = job->document->GetStack();
			const bdb::Card* card = stack.FindCard(job->cardIds[i]);
			if (card == NULL)
				continue;
			const bdb::Template* tmpl = stack.FindTemplate(
				card->TemplateId());
			bdb::Theme theme = WithSystemColors(stack.ThemeFor(tmpl));
			double width, height;
			PageSizeFor(tmpl, width, height);
			writer.BeginPage(width, height);
			PdfCanvas canvas(writer, kPointsPerPixel, 0, 0);
			if (tmpl == NULL) {
				// Missing template: the values as a list.
				BFont font(be_plain_font);
				font.SetSize(12);
				float y = 24;
				for (const auto& entry : card->Values().Object()) {
					BString line;
					line << entry.first.c_str() << ": "
						<< bdb::FirstLine(bdb::ValueText(entry.second))
							.c_str();
					canvas.DrawString(line.String(), BPoint(16, y), font,
						make_color(0, 0, 0));
					y += 18;
				}
			} else {
				PaintContext context;
				context.tmpl = tmpl;
				context.card = card;
				context.stack = &stack;
				context.theme = &theme;
				context.bitmaps = &bitmaps;
				context.mode = kPaintPrint;
				context.drawValues = true;
				context.printBackground = job->printBackground;
				context.hiddenParts
					= bdb::ComputeSwitchState(*tmpl, card).hidden;
				PaintCard(canvas, context);
			}
			writer.EndPage();
			bitmaps.TrimTo(32);
		}
		if (state.progress != NULL && state.progress->Lock()) {
			state.progress->SetProgress(i + 1, total);
			state.progress->Unlock();
		}
	}
	return true;
}


// Lines of a table cell: wrapped at the column width, at most 3, the
// rest replaced by "…" (I/O 4.3).
std::vector<std::string>
CellLines(bdb::PdfWriter& writer, const bdb::PdfFont& font, double size,
	const std::string& text, double width)
{
	std::vector<std::string> lines;
	std::string current;
	auto flushLine = [&]() {
		lines.push_back(current);
		current.clear();
	};
	for (size_t i = 0; i < text.size();) {
		unsigned char c = text[i];
		size_t length = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
		std::string piece = text.substr(i, length);
		i += length;
		if (piece == "\r")
			continue;
		if (piece == "\n") {
			flushLine();
			continue;
		}
		if (!current.empty()
			&& writer.TextWidth(current + piece, font, size) > width) {
			flushLine();
		}
		current += piece;
	}
	if (!current.empty() || lines.empty())
		lines.push_back(current);
	if (lines.size() > 3) {
		lines.resize(3);
		std::string& last = lines[2];
		while (!last.empty() && writer.TextWidth(last + "…", font, size)
				> width) {
			size_t cut = last.size() - 1;
			while (cut > 0 && (last[cut] & 0xc0) == 0x80)
				cut--;
			last.erase(cut);
		}
		last += "…";
	}
	return lines;
}


bool
ExportList(ExportState& state, bdb::PdfWriter& writer, BitmapCache& bitmaps)
{
	PdfJob* job = state.job;
	int32 pageWidthPx, pageHeightPx;
	if (!bdb::PaperPixels(job->paperSize.String(),
			job->orientation.String(), pageWidthPx, pageHeightPx)) {
		bdb::PaperPixels("A4", "landscape", pageWidthPx, pageHeightPx);
	}
	double pageWidth = pageWidthPx * kPointsPerPixel;
	double pageHeight = pageHeightPx * kPointsPerPixel;
	const double kMargin = 36;
	const double kFontSize = 9;
	const double kLine = kFontSize * 1.35;
	const double kPadding = 3;
	const double kThumb = 36;
	bdb::PdfFont font;
	{
		BFont plain(be_plain_font);
		font = PdfFontFor(plain);
	}

	// Cell texts (and image paths) first.
	struct Cell {
		std::string	text;
		std::string	image;
		bool		right = false;
	};
	std::vector<std::vector<Cell>> rows;
	BString stackTitle, templateTitle;
	{
		DocLocker lock(job->document);
		bdb::Stack& stack = job->document->GetStack();
		stackTitle = stack.Title().c_str();
		const bdb::Template* tmpl = stack.FindTemplate(job->templateId);
		templateTitle = tmpl != NULL ? tmpl->Title().c_str() : "";
		for (const std::string& id : job->cardIds) {
			const bdb::Card* card = stack.FindCard(id);
			if (card == NULL || tmpl == NULL)
				continue;
			std::vector<Cell> row;
			for (const PdfColumn& column : job->columns) {
				Cell cell;
				const Json* part = tmpl->FindPart(column.partId);
				if (part != NULL) {
					const Json& value = bdb::EffectiveValue(*card, *part);
					if (column.kind == bdb::kKindImage) {
						if (bdb::IsMediaRef(value) && job->imageThumbnails)
							cell.image = stack.MediaPath(value);
						else {
							cell.text = bdb::IsMediaRef(value)
								? str::kListImageYes.Get()
								: str::kListImageNo.Get();
						}
					} else if (column.kind == bdb::kKindNumber) {
						cell.text = FormatNumberValue(value, *part).String();
						cell.right = true;
					} else if (column.kind == bdb::kKindBool)
						cell.text = value.IsBool() && value.AsBool() ? "✓" : "";
					else
						cell.text = bdb::ValueText(value);
				}
				row.push_back(cell);
			}
			rows.push_back(row);
		}
	}

	// Column widths from the content, shrunk to fit the page (I/O 4.3).
	size_t columns = job->columns.size();
	if (columns == 0)
		return true;
	std::vector<double> widths(columns, 40);
	for (size_t c = 0; c < columns; c++) {
		widths[c] = std::max(widths[c], writer.TextWidth(
			job->columns[c].title.String(), font, kFontSize) + kPadding * 2);
		for (size_t r = 0; r < rows.size() && r < 200; r++) {
			double width = rows[r][c].image.empty()
				? writer.TextWidth(bdb::FirstLine(rows[r][c].text), font,
					kFontSize)
				: kThumb;
			widths[c] = std::max(widths[c], std::min(220.0,
				width + kPadding * 2));
		}
	}
	double available = pageWidth - kMargin * 2;
	double total = 0;
	for (double width : widths)
		total += width;
	if (total > available) {
		for (double& width : widths)
			width = width * available / total;
	}

	// Row heights and pages.
	std::vector<std::vector<std::vector<std::string>>> lines(rows.size());
	std::vector<double> heights(rows.size());
	for (size_t r = 0; r < rows.size(); r++) {
		size_t most = 1;
		bool thumb = false;
		lines[r].resize(columns);
		for (size_t c = 0; c < columns; c++) {
			if (!rows[r][c].image.empty()) {
				thumb = true;
				continue;
			}
			lines[r][c] = CellLines(writer, font, kFontSize, rows[r][c].text,
				widths[c] - kPadding * 2);
			most = std::max(most, lines[r][c].size());
		}
		heights[r] = std::max(most * kLine + kPadding * 2,
			thumb ? kThumb + kPadding * 2 : 0.0);
	}
	double headerHeight = kLine + kPadding * 2;
	double bodyBottom = pageHeight - kMargin - kLine * 2;
	std::vector<size_t> pageStarts;
	pageStarts.push_back(0);
	double y = kMargin + headerHeight;
	for (size_t r = 0; r < rows.size(); r++) {
		if (y + heights[r] > bodyBottom && y > kMargin + headerHeight) {
			pageStarts.push_back(r);
			y = kMargin + headerHeight;
		}
		y += heights[r];
	}
	int32 pages = int32(pageStarts.size());

	bdb::Color black = bdb::Color::Make(0, 0, 0);
	bdb::Color grid = bdb::Color::Make(0xb0, 0xb0, 0xb0);
	bdb::Color headerFill = bdb::Color::Make(0xe4, 0xe4, 0xe4);
	for (int32 page = 0; page < pages; page++) {
		if (state.cancel)
			return false;
		writer.BeginPage(pageWidth, pageHeight);
		// Header row on every page.
		double x = kMargin;
		writer.SetFillColor(headerFill);
		writer.FillRect(kMargin, kMargin, available, headerHeight);
		writer.SetLineWidth(0.5);
		for (size_t c = 0; c < columns; c++) {
			writer.SetFillColor(black);
			writer.Save();
			writer.ClipRect(x, kMargin, widths[c], headerHeight);
			writer.DrawText(job->columns[c].title.String(), x + kPadding,
				kMargin + kPadding + kFontSize, font, kFontSize);
			writer.Restore();
			x += widths[c];
		}
		y = kMargin + headerHeight;
		size_t end = page + 1 < pages ? pageStarts[page + 1] : rows.size();
		for (size_t r = pageStarts[page]; r < end; r++) {
			x = kMargin;
			for (size_t c = 0; c < columns; c++) {
				const Cell& cell = rows[r][c];
				if (!cell.image.empty()) {
					const BBitmap* bitmap = bitmaps.Get(cell.image);
					if (bitmap != NULL) {
						BRect bounds = bitmap->Bounds();
						double scale = std::min(kThumb / (bounds.Width() + 1),
							kThumb / (bounds.Height() + 1));
						double w = (bounds.Width() + 1) * scale;
						double h = (bounds.Height() + 1) * scale;
						PdfCanvas canvas(writer, 1.0, 0, 0);
						canvas.DrawBitmap(bitmap, BRect(x + kPadding,
							y + kPadding, x + kPadding + w - 1,
							y + kPadding + h - 1), cell.image.c_str());
					}
				} else {
					writer.SetFillColor(black);
					writer.Save();
					writer.ClipRect(x, y, widths[c], heights[r]);
					double lineY = y + kPadding + kFontSize;
					for (const std::string& text : lines[r][c]) {
						double textX = x + kPadding;
						if (cell.right) {
							textX = x + widths[c] - kPadding
								- writer.TextWidth(text, font, kFontSize);
						}
						writer.DrawText(text, textX, lineY, font, kFontSize);
						lineY += kLine;
					}
					writer.Restore();
				}
				x += widths[c];
			}
			writer.SetStrokeColor(grid);
			writer.StrokeLine(kMargin, y + heights[r], kMargin + available,
				y + heights[r]);
			y += heights[r];
		}
		// Column lines and the frame.
		writer.SetStrokeColor(grid);
		x = kMargin;
		for (size_t c = 0; c <= columns; c++) {
			writer.StrokeLine(x, kMargin, x, y);
			if (c < columns)
				x += widths[c];
		}
		writer.StrokeRect(kMargin, kMargin, available, y - kMargin);
		// Footer: stack, template, page (I/O 4.3).
		BString footer;
		footer.SetToFormat(str::kPdfFooter.Get(), stackTitle.String(),
			templateTitle.String(), (int)(page + 1), (int)pages);
		double footerWidth = writer.TextWidth(footer.String(), font, kFontSize);
		writer.SetFillColor(black);
		writer.DrawText(footer.String(), (pageWidth - footerWidth) / 2,
			pageHeight - kMargin + kFontSize, font, kFontSize);
		writer.EndPage();
		bitmaps.TrimTo(32);
		if (state.progress != NULL && state.progress->Lock()) {
			state.progress->SetProgress(page + 1, pages);
			state.progress->Unlock();
		}
	}
	return true;
}


status_t
ExportThread(void* data)
{
	std::unique_ptr<ExportState> state(static_cast<ExportState*>(data));
	std::unique_ptr<PdfJob> job(state->job);
	SystemGlyphs glyphs;
	bdb::PdfWriter writer(&glyphs);
	BitmapCache bitmaps;

	BString title;
	{
		DocLocker lock(job->document);
		title = job->document->GetStack().Title().c_str();
	}
	// Progress and cancel for 20 pages or more (I/O 4.5).
	if (job->kind == PdfJob::kCards && job->cardIds.size() >= 20) {
		state->progress = new ProgressWindow(&state->cancel);
		state->progress->Show();
	} else if (job->kind == PdfJob::kList && job->cardIds.size() >= 400) {
		state->progress = new ProgressWindow(&state->cancel);
		state->progress->Show();
	}

	bdb::Status status = writer.Begin(job->path.String(), title.String());
	bool ok = status.IsOk();
	if (ok) {
		ok = job->kind == PdfJob::kCards
			? ExportCards(*state, writer, bitmaps)
			: ExportList(*state, writer, bitmaps);
	}
	uint64_t bytes = 0;
	BString result = "ok";
	if (ok) {
		status = writer.Finish(&bytes);
		if (!status)
			result = status.Code();
	} else {
		// Canceled or failed: nothing is left behind (I/O 4.4).
		writer.Abort();
		result = state->cancel ? "canceled" : status.Code();
	}
	if (state->progress != NULL && state->progress->Lock())
		state->progress->Quit();

	{
		DocLocker lock(job->document);
		bdb::Logger& logger = job->document->GetStack().Log();
		if (result == "ok") {
			logger.Info("export.pdf", {{"kind", job->logKind.String()},
				{"count", int64_t(job->cardIds.size())},
				{"pages", int64_t(writer.PageCount())},
				{"bytes", int64_t(bytes)}});
		} else if (result != "canceled") {
			logger.Error("export.pdf.failed", {{"kind",
				job->logKind.String()}, {"err", result.String()}});
		}
	}
	BMessage reply(kMsgPdfDone);
	reply.AddString("status", result);
	reply.AddInt32("pages", writer.PageCount());
	reply.AddInt64("bytes", int64(bytes));
	job->replyTo.SendMessage(&reply);
	return B_OK;
}


}	// namespace


void
StartPdfExport(PdfJob* job)
{
	ExportState* state = new ExportState();
	state->job = job;
	thread_id thread = spawn_thread(&ExportThread, "bdb pdf export",
		B_NORMAL_PRIORITY, state);
	if (thread < 0) {
		BMessage reply(kMsgPdfDone);
		reply.AddString("status", "write_failed");
		job->replyTo.SendMessage(&reply);
		delete job;
		delete state;
		return;
	}
	resume_thread(thread);
}


// #pragma mark - options and save panel


namespace {


BFilePanel* sPdfPanel = NULL;


class PdfOptionsWindow : public BWindow {
public:
	PdfOptionsWindow(BMessenger target, const char* name, bool list)
		:
		BWindow(BRect(0, 0, 320, 160), str::kPdfTitle, B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
				| B_CLOSE_ON_ESCAPE),
		fTarget(target),
		fName(name),
		fList(list),
		fBackground(NULL),
		fPaper(NULL),
		fThumbs(NULL)
	{
		BButton* save = new BButton("save", str::kSave,
			new BMessage(kMsgOptionsSave));
		BButton* cancel = new BButton("cancel", str::kCancel,
			new BMessage(B_QUIT_REQUESTED));
		BLayoutBuilder::Group<> layout(this, B_VERTICAL);
		layout.SetInsets(B_USE_WINDOW_INSETS);
		if (list) {
			// Paper and orientation (A4 landscape by default, I/O 4.3).
			BPopUpMenu* paper = new BPopUpMenu("paper");
			size_t count;
			const bdb::PaperInfo* papers = bdb::PaperSizes(&count);
			for (size_t i = 0; i < count; i++) {
				for (const char* orientation : { "portrait", "landscape" }) {
					BMessage* message = new BMessage('pPpr');
					message->AddString("size", papers[i].size);
					message->AddString("orientation", orientation);
					BString label = BString(papers[i].size) == "postcard"
						? str::kPaperPostcard.Get() : papers[i].size;
					label << " " << (BString(orientation) == "portrait"
						? str::kPortrait.Get() : str::kLandscape.Get());
					BMenuItem* item = new BMenuItem(label, message);
					item->SetMarked(BString(papers[i].size) == "A4"
						&& BString(orientation) == "landscape");
					paper->AddItem(item);
				}
			}
			fPaper = new BMenuField("paper", str::kPaperSize, paper);
			BPopUpMenu* thumbs = new BPopUpMenu("thumbs");
			BMenuItem* text = new BMenuItem(str::kPdfImageText,
				new BMessage('pTxt'));
			text->SetMarked(true);
			thumbs->AddItem(text);
			thumbs->AddItem(new BMenuItem(str::kPdfImageThumb,
				new BMessage('pThb')));
			fThumbs = new BMenuField("thumbs", str::kPdfImageColumn, thumbs);
			layout.Add(fPaper).Add(fThumbs);
		} else {
			fBackground = new BCheckBox("background",
				str::kPdfPrintBackground, NULL);
			fBackground->SetValue(B_CONTROL_ON);
			layout.Add(fBackground);
		}
		layout.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(save)
		.End();
		SetDefaultButton(save);
		CenterOnScreen();
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != kMsgOptionsSave) {
			BWindow::MessageReceived(message);
			return;
		}
		BMessage reply(kMsgPdfPanel);
		if (fBackground != NULL) {
			reply.AddBool("background",
				fBackground->Value() == B_CONTROL_ON);
		}
		if (fPaper != NULL) {
			BMenuItem* item = fPaper->Menu()->FindMarked();
			if (item != NULL && item->Message() != NULL) {
				reply.AddString("paper",
					item->Message()->GetString("size", "A4"));
				reply.AddString("orientation",
					item->Message()->GetString("orientation", "landscape"));
			}
			BMenuItem* thumbs = fThumbs->Menu()->FindMarked();
			reply.AddBool("thumbs", thumbs != NULL
				&& thumbs->Message() != NULL
				&& thumbs->Message()->what == 'pThb');
		}
		if (sPdfPanel == NULL) {
			sPdfPanel = new BFilePanel(B_SAVE_PANEL, NULL, NULL, 0, false);
			sPdfPanel->Window()->SetTitle(str::kPdfTitle);
		}
		sPdfPanel->SetTarget(fTarget);
		sPdfPanel->SetMessage(&reply);
		sPdfPanel->SetSaveText(fName);
		sPdfPanel->Show();
		Quit();
	}

private:
	BMessenger	fTarget;
	BString		fName;
	bool		fList;
	BCheckBox*	fBackground;
	BMenuField*	fPaper;
	BMenuField*	fThumbs;
};


}	// namespace


void
ShowPdfSavePanel(BMessenger target, const char* defaultName, bool list)
{
	PdfOptionsWindow* window = new PdfOptionsWindow(target, defaultName,
		list);
	window->Show();
}


bool
PdfPanelPath(BMessage* message, BString& outPath)
{
	entry_ref directory;
	BString name;
	if (message->FindRef("directory", &directory) != B_OK
		|| message->FindString("name", &name) != B_OK) {
		return false;
	}
	if (!name.IEndsWith(".pdf"))
		name << ".pdf";
	BPath path(&directory);
	path.Append(name);
	outPath = path.Path();
	return true;
}


void
ReportPdfResult(BMessage* message)
{
	BString status = message->GetString("status", "ok");
	if (status == "ok" || status == "canceled")
		return;
	BString text;
	text.SetToFormat(str::kPdfFailed.Get(), status.String());
	(new BAlert(str::kAppName, text, str::kOK, NULL, NULL, B_WIDTH_AS_USUAL,
		B_STOP_ALERT))->Go(NULL);
}
