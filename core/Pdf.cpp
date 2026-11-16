/*
 * BDB card database - core
 */
#include "Pdf.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <functional>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>


namespace bdb {


static const int kCatalogObject = 1;
static const int kPagesObject = 2;
static const double kKappa = 0.5522847498;


// #pragma mark - PdfPath


void
PdfPath::AddRect(double x, double y, double w, double h)
{
	MoveTo(x, y);
	LineTo(x + w, y);
	LineTo(x + w, y + h);
	LineTo(x, y + h);
	Close();
}


void
PdfPath::AddRoundRect(double x, double y, double w, double h, double radius)
{
	radius = std::min(radius, std::min(w, h) / 2);
	if (radius <= 0) {
		AddRect(x, y, w, h);
		return;
	}
	double k = radius * kKappa;
	MoveTo(x + radius, y);
	LineTo(x + w - radius, y);
	CurveTo(x + w - radius + k, y, x + w, y + radius - k, x + w, y + radius);
	LineTo(x + w, y + h - radius);
	CurveTo(x + w, y + h - radius + k, x + w - radius + k, y + h,
		x + w - radius, y + h);
	LineTo(x + radius, y + h);
	CurveTo(x + radius - k, y + h, x, y + h - radius + k, x, y + h - radius);
	LineTo(x, y + radius);
	CurveTo(x, y + radius - k, x + radius - k, y, x + radius, y);
	Close();
}


// #pragma mark - helpers


static bool
Deflate(const std::string& data, std::string& out)
{
	uLongf size = compressBound(uLong(data.size()));
	out.resize(size);
	if (compress2(reinterpret_cast<Bytef*>(&out[0]), &size,
			reinterpret_cast<const Bytef*>(data.data()), uLong(data.size()),
			6) != Z_OK) {
		return false;
	}
	out.resize(size);
	return true;
}


static std::vector<uint32_t>
CodePoints(const std::string& text)
{
	std::vector<uint32_t> result;
	const unsigned char* data
		= reinterpret_cast<const unsigned char*>(text.data());
	size_t size = text.size();
	for (size_t i = 0; i < size;) {
		unsigned char c = data[i];
		uint32_t cp;
		int length;
		if (c < 0x80) {
			cp = c;
			length = 1;
		} else if ((c & 0xe0) == 0xc0) {
			cp = c & 0x1f;
			length = 2;
		} else if ((c & 0xf0) == 0xe0) {
			cp = c & 0x0f;
			length = 3;
		} else if ((c & 0xf8) == 0xf0) {
			cp = c & 0x07;
			length = 4;
		} else {
			result.push_back(0xfffd);
			i++;
			continue;
		}
		if (i + length > size) {
			result.push_back(0xfffd);
			break;
		}
		for (int j = 1; j < length; j++)
			cp = (cp << 6) | (data[i + j] & 0x3f);
		result.push_back(cp);
		i += size_t(length);
	}
	return result;
}


static std::string
Utf16Hex(uint32_t cp)
{
	char buffer[16];
	if (cp >= 0x10000) {
		cp -= 0x10000;
		snprintf(buffer, sizeof(buffer), "%04X%04X", 0xd800 + (cp >> 10),
			0xdc00 + (cp & 0x3ff));
	} else
		snprintf(buffer, sizeof(buffer), "%04X", cp);
	return buffer;
}


static std::string
PdfString(const std::string& text)
{
	// UTF-16BE with BOM, hex.
	std::string out = "<FEFF";
	for (uint32_t cp : CodePoints(text))
		out += Utf16Hex(cp);
	out += ">";
	return out;
}


// #pragma mark - PdfWriter


PdfWriter::PdfWriter(PdfGlyphSource* glyphs)
	:
	fGlyphs(glyphs)
{
}


PdfWriter::~PdfWriter()
{
	Abort();
}


std::string
PdfWriter::_Number(double value) const
{
	if (std::fabs(value) < 0.0005)
		return "0";
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%.3f", value);
	std::string text = buffer;
	while (!text.empty() && text.back() == '0')
		text.pop_back();
	if (!text.empty() && text.back() == '.')
		text.pop_back();
	return text;
}


void
PdfWriter::_Write(const std::string& text)
{
	if (fFile == nullptr || fFailed)
		return;
	if (fwrite(text.data(), 1, text.size(), fFile) != text.size())
		fFailed = true;
	fOffset += text.size();
}


int
PdfWriter::_NewObject()
{
	fOffsets.push_back(0);
	return int(fOffsets.size()) - 1;
}


void
PdfWriter::_StartObject(int object)
{
	fOffsets[object] = fOffset;
	_Write(std::to_string(object) + " 0 obj\n");
}


void
PdfWriter::_WriteStream(int object, const std::string& dictionary,
	const std::string& data, bool compress)
{
	std::string body = data;
	std::string filter;
	if (compress) {
		std::string compressed;
		if (Deflate(data, compressed)) {
			body = std::move(compressed);
			filter = " /Filter /FlateDecode";
		}
	}
	_StartObject(object);
	_Write("<< " + dictionary + filter + " /Length "
		+ std::to_string(body.size()) + " >>\nstream\n");
	_Write(body);
	_Write("\nendstream\nendobj\n");
}


Status
PdfWriter::Begin(const std::string& path, const std::string& title)
{
	fPath = path;
	fTmpPath = path + ".tmp";
	fTitle = title;
	fFile = fopen(fTmpPath.c_str(), "wb");
	if (fFile == nullptr)
		return Status(ErrorKind::kWriteFailed, strerror(errno));
	fOffsets.assign(3, 0);		// 0 (free), catalog, pages
	_Write("%PDF-1.4\n%\xe2\xe3\xcf\xd3\n");
	return fFailed ? Status(ErrorKind::kWriteFailed, "write") : Status::Ok();
}


void
PdfWriter::Abort()
{
	if (fFile != nullptr) {
		fclose(fFile);
		fFile = nullptr;
		unlink(fTmpPath.c_str());
	}
}


void
PdfWriter::BeginPage(double width, double height)
{
	fPageWidth = width;
	fPageHeight = height;
	fContent.clear();
	fPageFonts.clear();
	fPageImages.clear();
	// y down from the top left (the coordinates of the card surface).
	_Op("1 0 0 -1 0 " + _Number(height) + " cm");
}


void
PdfWriter::_Op(const std::string& text)
{
	fContent += text;
	fContent += '\n';
}


void
PdfWriter::EndPage()
{
	int content = _NewObject();
	_WriteStream(content, "", fContent, true);
	int page = _NewObject();
	std::string resources = "<< /ProcSet [/PDF /Text /ImageB /ImageC]";
	if (!fPageFonts.empty()) {
		resources += " /Font <<";
		for (const auto& entry : fPageFonts) {
			resources += " /F" + std::to_string(entry.first) + " "
				+ std::to_string(fSubFonts[entry.first].object) + " 0 R";
		}
		resources += " >>";
	}
	if (!fPageImages.empty()) {
		resources += " /XObject <<";
		for (const auto& entry : fPageImages) {
			resources += " /Im" + std::to_string(entry.first) + " "
				+ std::to_string(fImageObjects[entry.first]) + " 0 R";
		}
		resources += " >>";
	}
	resources += " >>";
	_StartObject(page);
	_Write("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
		+ _Number(fPageWidth) + " " + _Number(fPageHeight) + "] /Resources "
		+ resources + " /Contents " + std::to_string(content) + " 0 R >>\n"
		"endobj\n");
	fPages.push_back(page);
	fPageCount++;
	fContent.clear();
}


void
PdfWriter::Save()
{
	_Op("q");
}


void
PdfWriter::Restore()
{
	_Op("Q");
}


void
PdfWriter::ClipRect(double x, double y, double w, double h)
{
	_Op(_Number(x) + " " + _Number(y) + " " + _Number(w) + " " + _Number(h)
		+ " re W n");
}


static std::string
ColorComponents(const Color& color)
{
	char buffer[48];
	snprintf(buffer, sizeof(buffer), "%.3f %.3f %.3f", color.r / 255.0,
		color.g / 255.0, color.b / 255.0);
	return buffer;
}


void
PdfWriter::SetFillColor(const Color& color)
{
	_Op(ColorComponents(color) + " rg");
}


void
PdfWriter::SetStrokeColor(const Color& color)
{
	_Op(ColorComponents(color) + " RG");
}


void
PdfWriter::SetLineWidth(double width)
{
	_Op(_Number(width) + " w");
}


void
PdfWriter::FillRect(double x, double y, double w, double h)
{
	_Op(_Number(x) + " " + _Number(y) + " " + _Number(w) + " " + _Number(h)
		+ " re f");
}


void
PdfWriter::StrokeRect(double x, double y, double w, double h)
{
	_Op(_Number(x) + " " + _Number(y) + " " + _Number(w) + " " + _Number(h)
		+ " re S");
}


void
PdfWriter::StrokeLine(double x1, double y1, double x2, double y2)
{
	_Op(_Number(x1) + " " + _Number(y1) + " m " + _Number(x2) + " "
		+ _Number(y2) + " l S");
}


static std::string
PathOps(const PdfPath& path, const std::function<std::string(double)>& number)
{
	std::string out;
	for (const PdfPath::Op& op : path.ops) {
		switch (op.op) {
			case 'm':
				out += number(op.v[0]) + " " + number(op.v[1]) + " m\n";
				break;
			case 'l':
				out += number(op.v[0]) + " " + number(op.v[1]) + " l\n";
				break;
			case 'c':
				out += number(op.v[0]) + " " + number(op.v[1]) + " "
					+ number(op.v[2]) + " " + number(op.v[3]) + " "
					+ number(op.v[4]) + " " + number(op.v[5]) + " c\n";
				break;
			case 'h':
				out += "h\n";
				break;
		}
	}
	return out;
}


void
PdfWriter::FillPath(const PdfPath& path)
{
	if (path.IsEmpty())
		return;
	fContent += PathOps(path, [this](double v) { return _Number(v); });
	_Op("f");
}


void
PdfWriter::StrokePath(const PdfPath& path)
{
	if (path.IsEmpty())
		return;
	fContent += PathOps(path, [this](double v) { return _Number(v); });
	_Op("S");
}


PdfWriter::GlyphInfo
PdfWriter::_Glyph(const PdfFont& font, uint32_t codePoint, double* advance)
{
	FontState& state = fFonts[font];
	auto found = state.glyphs.find(codePoint);
	if (found != state.glyphs.end()) {
		if (advance != nullptr)
			*advance = state.advances[codePoint];
		return found->second;
	}
	double width = 0;
	PdfPath outline;
	if (fGlyphs == nullptr || !fGlyphs->Glyph(font, codePoint, width, outline))
		width = 500;
	if (state.subFonts.empty()
		|| fSubFonts[state.subFonts.back()].codePoints.size() >= 256) {
		SubFont sub;
		sub.font = font;
		sub.object = _NewObject();
		fSubFonts.push_back(sub);
		state.subFonts.push_back(int(fSubFonts.size()) - 1);
	}
	SubFont& sub = fSubFonts[state.subFonts.back()];
	GlyphInfo info;
	info.font = state.subFonts.back();
	info.code = int(sub.codePoints.size());
	sub.codePoints.push_back(codePoint);
	sub.advances.push_back(width);
	sub.outlines.push_back(outline);
	state.glyphs[codePoint] = info;
	state.advances[codePoint] = width;
	if (advance != nullptr)
		*advance = width;
	return info;
}


double
PdfWriter::_Advance(const PdfFont& font, uint32_t codePoint)
{
	FontState& state = fFonts[font];
	auto found = state.advances.find(codePoint);
	if (found != state.advances.end())
		return found->second;
	double advance = 0;
	_Glyph(font, codePoint, &advance);
	return advance;
}


double
PdfWriter::TextWidth(const std::string& utf8, const PdfFont& font,
	double size)
{
	double width = 0;
	for (uint32_t cp : CodePoints(utf8))
		width += _Advance(font, cp);
	return width * size / 1000.0;
}


void
PdfWriter::DrawText(const std::string& utf8, double x, double baseline,
	const PdfFont& font, double size)
{
	std::vector<uint32_t> points = CodePoints(utf8);
	if (points.empty())
		return;
	std::string ops = "BT\n";
	int currentFont = -1;
	std::string run;
	double runX = x;
	double cursor = x;
	auto flush = [&]() {
		if (currentFont < 0 || run.empty())
			return;
		ops += "/F" + std::to_string(currentFont) + " " + _Number(size)
			+ " Tf 1 0 0 -1 " + _Number(runX) + " " + _Number(baseline)
			+ " Tm <" + run + "> Tj\n";
		run.clear();
	};
	for (uint32_t cp : points) {
		double advance = 0;
		GlyphInfo info = _Glyph(font, cp, &advance);
		if (info.font != currentFont) {
			flush();
			currentFont = info.font;
			runX = cursor;
			fPageFonts[info.font] = true;
		}
		char hex[4];
		snprintf(hex, sizeof(hex), "%02X", info.code);
		run += hex;
		cursor += advance * size / 1000.0;
	}
	flush();
	ops += "ET";
	_Op(ops);
}


bool
PdfWriter::JpegInfo(const std::string& data, int& width, int& height,
	int& components)
{
	const unsigned char* bytes
		= reinterpret_cast<const unsigned char*>(data.data());
	size_t size = data.size();
	if (size < 4 || bytes[0] != 0xff || bytes[1] != 0xd8)
		return false;
	size_t i = 2;
	while (i + 4 <= size) {
		if (bytes[i] != 0xff) {
			i++;
			continue;
		}
		unsigned char marker = bytes[i + 1];
		if (marker == 0xff) {
			i++;
			continue;
		}
		if (marker == 0xd8 || marker == 0x01
			|| (marker >= 0xd0 && marker <= 0xd7)) {
			i += 2;
			continue;
		}
		size_t length = size_t(bytes[i + 2]) << 8 | bytes[i + 3];
		if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4
			&& marker != 0xc8 && marker != 0xcc) {
			if (i + 9 >= size)
				return false;
			height = int(bytes[i + 5]) << 8 | bytes[i + 6];
			width = int(bytes[i + 7]) << 8 | bytes[i + 8];
			components = bytes[i + 9];
			return width > 0 && height > 0
				&& (components == 1 || components == 3 || components == 4);
		}
		i += 2 + length;
	}
	return false;
}


int
PdfWriter::FindImage(const std::string& key) const
{
	auto found = fImageKeys.find(key);
	return found != fImageKeys.end() ? found->second : -1;
}


int
PdfWriter::AddJpeg(const std::string& key, const std::string& data,
	int width, int height, int components)
{
	int existing = FindImage(key);
	if (!key.empty() && existing >= 0)
		return existing;
	if (width <= 0 || height <= 0)
		return -1;
	std::string colorSpace = components == 1 ? "/DeviceGray"
		: components == 4 ? "/DeviceCMYK /Decode [1 0 1 0 1 0 1 0]"
		: "/DeviceRGB";
	int object = _NewObject();
	_StartObject(object);
	_Write("<< /Type /XObject /Subtype /Image /Width " + std::to_string(width)
		+ " /Height " + std::to_string(height) + " /ColorSpace " + colorSpace
		+ " /BitsPerComponent 8 /Filter /DCTDecode /Length "
		+ std::to_string(data.size()) + " >>\nstream\n");
	_Write(data);
	_Write("\nendstream\nendobj\n");
	fImageObjects.push_back(object);
	int image = int(fImageObjects.size()) - 1;
	if (!key.empty())
		fImageKeys[key] = image;
	return image;
}


int
PdfWriter::AddRgb(const std::string& key, const std::vector<uint8_t>& rgb,
	const std::vector<uint8_t>& alpha, int width, int height)
{
	int existing = FindImage(key);
	if (!key.empty() && existing >= 0)
		return existing;
	size_t pixels = size_t(width) * size_t(height);
	if (width <= 0 || height <= 0 || rgb.size() != pixels * 3)
		return -1;
	int mask = 0;
	bool opaque = true;
	if (alpha.size() == pixels) {
		for (uint8_t value : alpha) {
			if (value != 255) {
				opaque = false;
				break;
			}
		}
	}
	if (!opaque) {
		mask = _NewObject();
		_WriteStream(mask, "/Type /XObject /Subtype /Image /Width "
			+ std::to_string(width) + " /Height " + std::to_string(height)
			+ " /ColorSpace /DeviceGray /BitsPerComponent 8",
			std::string(alpha.begin(), alpha.end()), true);
	}
	int object = _NewObject();
	std::string dictionary = "/Type /XObject /Subtype /Image /Width "
		+ std::to_string(width) + " /Height " + std::to_string(height)
		+ " /ColorSpace /DeviceRGB /BitsPerComponent 8";
	if (mask != 0)
		dictionary += " /SMask " + std::to_string(mask) + " 0 R";
	_WriteStream(object, dictionary, std::string(rgb.begin(), rgb.end()),
		true);
	fImageObjects.push_back(object);
	int image = int(fImageObjects.size()) - 1;
	if (!key.empty())
		fImageKeys[key] = image;
	return image;
}


void
PdfWriter::DrawImage(int image, double x, double y, double w, double h)
{
	if (image < 0 || image >= int(fImageObjects.size()))
		return;
	fPageImages[image] = true;
	_Op("q " + _Number(w) + " 0 0 " + _Number(-h) + " " + _Number(x) + " "
		+ _Number(y + h) + " cm /Im" + std::to_string(image) + " Do Q");
}


Status
PdfWriter::Finish(uint64_t* outBytes)
{
	if (fFile == nullptr)
		return Status(ErrorKind::kWriteFailed, "not started");

	// Type 3 fonts (glyph outlines are only known now).
	for (SubFont& sub : fSubFonts) {
		size_t count = sub.codePoints.size();
		std::vector<int> procs;
		double left = 0, bottom = -250, right = 1000, top = 1000;
		for (size_t i = 0; i < count; i++) {
			const PdfPath& outline = sub.outlines[i];
			double minX = 0, minY = 0, maxX = 0, maxY = 0;
			bool first = true;
			for (const PdfPath::Op& op : outline.ops) {
				int points = op.op == 'c' ? 3 : op.op == 'h' ? 0 : 1;
				for (int p = 0; p < points; p++) {
					double px = op.v[p * 2];
					double py = op.v[p * 2 + 1];
					if (first) {
						minX = maxX = px;
						minY = maxY = py;
						first = false;
					}
					minX = std::min(minX, px);
					maxX = std::max(maxX, px);
					minY = std::min(minY, py);
					maxY = std::max(maxY, py);
				}
			}
			left = std::min(left, minX);
			bottom = std::min(bottom, minY);
			right = std::max(right, maxX);
			top = std::max(top, maxY);
			std::string proc = _Number(sub.advances[i]) + " 0 "
				+ _Number(std::floor(minX)) + " " + _Number(std::floor(minY))
				+ " " + _Number(std::ceil(maxX)) + " "
				+ _Number(std::ceil(maxY)) + " d1\n";
			if (!outline.IsEmpty()) {
				proc += PathOps(outline, [this](double v) {
					return _Number(v); });
				proc += "f\n";
			}
			int object = _NewObject();
			_WriteStream(object, "", proc, true);
			procs.push_back(object);
		}
		int charProcs = _NewObject();
		std::string dictionary = "<<";
		for (size_t i = 0; i < count; i++) {
			dictionary += " /g" + std::to_string(i) + " "
				+ std::to_string(procs[i]) + " 0 R";
		}
		dictionary += " >>";
		_StartObject(charProcs);
		_Write(dictionary + "\nendobj\n");

		// ToUnicode (search and copy).
		std::string cmap = "/CIDInit /ProcSet findresource begin\n"
			"12 dict begin\nbegincmap\n"
			"/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) "
			"/Supplement 0 >> def\n/CMapName /BDB-UCS def\n/CMapType 2 def\n"
			"1 begincodespacerange\n<00> <FF>\nendcodespacerange\n";
		for (size_t start = 0; start < count; start += 100) {
			size_t end = std::min(count, start + 100);
			cmap += std::to_string(end - start) + " beginbfchar\n";
			for (size_t i = start; i < end; i++) {
				char code[8];
				snprintf(code, sizeof(code), "<%02X>", int(i));
				cmap += std::string(code) + " <"
					+ Utf16Hex(sub.codePoints[i]) + ">\n";
			}
			cmap += "endbfchar\n";
		}
		cmap += "endcmap\nCMapName currentdict /CMap defineresource pop\n"
			"end\nend\n";
		int toUnicode = _NewObject();
		_WriteStream(toUnicode, "", cmap, true);

		std::string differences = "[0";
		std::string widths = "[";
		for (size_t i = 0; i < count; i++) {
			differences += " /g" + std::to_string(i);
			widths += (i > 0 ? " " : "") + _Number(sub.advances[i]);
		}
		differences += "]";
		widths += "]";
		_StartObject(sub.object);
		_Write("<< /Type /Font /Subtype /Type3 /FontBBox ["
			+ _Number(std::floor(left)) + " " + _Number(std::floor(bottom))
			+ " " + _Number(std::ceil(right)) + " " + _Number(std::ceil(top))
			+ "] /FontMatrix [0.001 0 0 0.001 0 0] /CharProcs "
			+ std::to_string(charProcs) + " 0 R /Encoding << /Type /Encoding "
			"/Differences " + differences + " >> /FirstChar 0 /LastChar "
			+ std::to_string(count == 0 ? 0 : count - 1) + " /Widths "
			+ widths + " /Resources << >> /ToUnicode "
			+ std::to_string(toUnicode) + " 0 R >>\nendobj\n");
	}

	// Page tree and catalog.
	std::string kids = "[";
	for (size_t i = 0; i < fPages.size(); i++)
		kids += (i > 0 ? " " : "") + std::to_string(fPages[i]) + " 0 R";
	kids += "]";
	_StartObject(kPagesObject);
	_Write("<< /Type /Pages /Kids " + kids + " /Count "
		+ std::to_string(fPages.size()) + " >>\nendobj\n");
	_StartObject(kCatalogObject);
	_Write("<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
	int info = _NewObject();
	_StartObject(info);
	_Write("<< /Title " + PdfString(fTitle) + " /Producer (Renfuda) >>\nendobj\n");

	uint64_t xref = fOffset;
	std::string table = "xref\n0 " + std::to_string(fOffsets.size())
		+ "\n0000000000 65535 f \n";
	for (size_t i = 1; i < fOffsets.size(); i++) {
		char line[32];
		snprintf(line, sizeof(line), "%010llu 00000 n \n",
			(unsigned long long)fOffsets[i]);
		table += line;
	}
	_Write(table);
	_Write("trailer\n<< /Size " + std::to_string(fOffsets.size())
		+ " /Root 1 0 R /Info " + std::to_string(info) + " 0 R >>\n"
		"startxref\n" + std::to_string(xref) + "\n%%EOF\n");

	bool ok = !fFailed && fflush(fFile) == 0 && fsync(fileno(fFile)) == 0;
	ok = fclose(fFile) == 0 && ok;
	fFile = nullptr;
	if (!ok || rename(fTmpPath.c_str(), fPath.c_str()) != 0) {
		unlink(fTmpPath.c_str());
		return Status(ErrorKind::kWriteFailed, "pdf");
	}
	if (outBytes != nullptr)
		*outBytes = fOffset;
	return Status::Ok();
}


}	// namespace bdb
