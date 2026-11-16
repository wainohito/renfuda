/*
 * BDB card database - core
 * A small PDF writer for the PDF export (I/O spec v0.1, chapter 4).
 *
 * - Objects are written to a temporary file as they are made, so long
 *   exports do not keep every page and image in memory; Finish() renames
 *   the file into place, Abort() removes it (4.4).
 * - Text uses Type 3 fonts whose glyphs are vector outlines supplied by a
 *   PdfGlyphSource (the application asks the system font engine). The
 *   fonts are therefore always embedded, work with any installed font
 *   (also variable CFF2 fonts, which PDF cannot embed directly), and carry
 *   a ToUnicode map so that the text can be searched and copied.
 * - JPEG files are embedded as they are (no recompression); other images
 *   are embedded losslessly as Flate compressed RGB with an alpha mask.
 *
 * Coordinates: points from the top left corner of the page, y down.
 */
#ifndef BDB_PDF_H
#define BDB_PDF_H

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "Status.h"
#include "Theme.h"


namespace bdb {


struct PdfPath {
	struct Op {
		char	op;		// 'm' move, 'l' line, 'c' cubic, 'h' close
		double	v[6];
	};
	std::vector<Op>	ops;

	void	MoveTo(double x, double y) { ops.push_back({'m', {x, y}}); }
	void	LineTo(double x, double y) { ops.push_back({'l', {x, y}}); }
	void	CurveTo(double x1, double y1, double x2, double y2, double x3,
				double y3)
				{ ops.push_back({'c', {x1, y1, x2, y2, x3, y3}}); }
	void	Close() { ops.push_back({'h', {0}}); }
	bool	IsEmpty() const { return ops.empty(); }

	void	AddRect(double x, double y, double w, double h);
	void	AddRoundRect(double x, double y, double w, double h,
				double radius);
};


struct PdfFont {
	std::string	family;		// empty: the system font
	std::string	style;

	bool	operator<(const PdfFont& other) const
				{ return family != other.family ? family < other.family
					: style < other.style; }
};


// Glyph outlines in a 1000 unit em, y up, origin on the baseline.
class PdfGlyphSource {
public:
	virtual					~PdfGlyphSource() {}
	virtual	bool			Glyph(const PdfFont& font, uint32_t codePoint,
								double& advance, PdfPath& outline) = 0;
};


class PdfWriter {
public:
							PdfWriter(PdfGlyphSource* glyphs);
							~PdfWriter();

	// Starts writing to "<path>.tmp" next to the target.
			Status			Begin(const std::string& path,
								const std::string& title);

			void			BeginPage(double width, double height);
			void			EndPage();
			int				PageCount() const { return fPageCount; }

	// Graphics state
			void			Save();
			void			Restore();
			void			ClipRect(double x, double y, double w,
								double h);
			void			SetFillColor(const Color& color);
			void			SetStrokeColor(const Color& color);
			void			SetLineWidth(double width);

			void			FillRect(double x, double y, double w,
								double h);
			void			StrokeRect(double x, double y, double w,
								double h);
			void			StrokeLine(double x1, double y1, double x2,
								double y2);
			void			FillPath(const PdfPath& path);
			void			StrokePath(const PdfPath& path);

	// Text on one line; `baseline` is the y of the baseline.
			void			DrawText(const std::string& utf8, double x,
								double baseline, const PdfFont& font,
								double size);
			double			TextWidth(const std::string& utf8,
								const PdfFont& font, double size);

	// Images. `key` (for example the media path) lets the same image be
	// embedded once. Return an image number, -1 on failure.
			int				AddJpeg(const std::string& key,
								const std::string& data, int width,
								int height, int components);
	// rgb: width * height * 3 bytes; alpha: width * height or empty.
			int				AddRgb(const std::string& key,
								const std::vector<uint8_t>& rgb,
								const std::vector<uint8_t>& alpha,
								int width, int height);
			int				FindImage(const std::string& key) const;
			void			DrawImage(int image, double x, double y,
								double w, double h);

	// Writes fonts, pages and the trailer, then renames the file into
	// place. `outBytes` receives the file size.
			Status			Finish(uint64_t* outBytes = nullptr);
	// Removes the temporary file.
			void			Abort();

	// Reads width, height and components of a baseline/progressive JPEG.
	static	bool			JpegInfo(const std::string& data, int& width,
								int& height, int& components);

private:
	struct GlyphInfo {
		int			font;		// sub font number
		int			code;		// 0-255 within the sub font
	};
	struct SubFont {
		PdfFont		font;
		std::vector<uint32_t> codePoints;
		std::vector<double> advances;
		std::vector<PdfPath> outlines;
		int			object = 0;
	};
	struct FontState {
		std::map<uint32_t, GlyphInfo> glyphs;
		std::map<uint32_t, double> advances;
		std::vector<int> subFonts;
	};

			int				_NewObject();
			void			_StartObject(int object);
			void			_Write(const std::string& text);
			void			_WriteStream(int object,
								const std::string& dictionary,
								const std::string& data, bool compress);
			GlyphInfo		_Glyph(const PdfFont& font, uint32_t codePoint,
								double* advance);
			double			_Advance(const PdfFont& font,
								uint32_t codePoint);
			std::string		_Number(double value) const;
			void			_Op(const std::string& text);

			PdfGlyphSource*	fGlyphs;
			FILE*			fFile = nullptr;
			std::string		fPath;
			std::string		fTmpPath;
			std::string		fTitle;
			uint64_t		fOffset = 0;
			std::vector<uint64_t> fOffsets;		// by object number
			bool			fFailed = false;

			std::vector<int> fPages;			// page objects
			int				fPagesObject = 0;
			int				fPageCount = 0;
			double			fPageHeight = 0;
			double			fPageWidth = 0;
			std::string		fContent;
			std::map<int, bool> fPageFonts;		// sub fonts used
			std::map<int, bool> fPageImages;	// images used

			std::map<PdfFont, FontState> fFonts;
			std::vector<SubFont> fSubFonts;
			std::vector<int> fImageObjects;
			std::map<std::string, int> fImageKeys;
};


}	// namespace bdb

#endif	// BDB_PDF_H
