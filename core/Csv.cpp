/*
 * BDB card database - core
 */
#include "Csv.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "Ids.h"
#include "Limits.h"
#include "Link.h"
#include "Model.h"
#include "Preset.h"
#include "Stack.h"
#include "Store.h"
#include "Text.h"


namespace bdb {


#include "JisTables.inc"


static std::string sLanguage = "ja";


static void
AppendUtf8(std::string& out, uint32_t cp)
{
	if (cp < 0x80) {
		out += char(cp);
	} else if (cp < 0x800) {
		out += char(0xc0 | (cp >> 6));
		out += char(0x80 | (cp & 0x3f));
	} else if (cp < 0x10000) {
		out += char(0xe0 | (cp >> 12));
		out += char(0x80 | ((cp >> 6) & 0x3f));
		out += char(0x80 | (cp & 0x3f));
	} else {
		out += char(0xf0 | (cp >> 18));
		out += char(0x80 | ((cp >> 12) & 0x3f));
		out += char(0x80 | ((cp >> 6) & 0x3f));
		out += char(0x80 | (cp & 0x3f));
	}
}


static const uint32_t kReplacement = 0xfffd;


// #pragma mark - encodings


const char*
CsvEncodingName(CsvEncoding encoding)
{
	switch (encoding) {
		case CsvEncoding::kAuto:		return "auto";
		case CsvEncoding::kUtf8:		return "utf-8";
		case CsvEncoding::kShiftJis:	return "shift_jis";
		case CsvEncoding::kEucJp:		return "euc-jp";
	}
	return "utf-8";
}


// Length of the UTF-8 sequence at `data`: >0 valid, 0 incomplete at the
// end, -1 invalid.
static int
Utf8Sequence(const unsigned char* data, size_t size)
{
	unsigned char c = data[0];
	int length;
	uint32_t minimum;
	if (c < 0x80)
		return 1;
	if (c >= 0xc2 && c <= 0xdf) {
		length = 2;
		minimum = 0x80;
	} else if (c >= 0xe0 && c <= 0xef) {
		length = 3;
		minimum = 0x800;
	} else if (c >= 0xf0 && c <= 0xf4) {
		length = 4;
		minimum = 0x10000;
	} else
		return -1;
	uint32_t cp = c & (0xff >> (length + 1));
	for (int i = 1; i < length; i++) {
		if (size_t(i) >= size)
			return 0;
		if ((data[i] & 0xc0) != 0x80)
			return -1;
		cp = (cp << 6) | (data[i] & 0x3f);
	}
	if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
		return -1;
	return length;
}


static uint16_t
Cp932Lookup(unsigned char lead, unsigned char trail)
{
	int leadIndex;
	if (lead >= 0x81 && lead <= 0x9f)
		leadIndex = lead - 0x81;
	else if (lead >= 0xe0 && lead <= 0xfc)
		leadIndex = lead - 0xe0 + 31;
	else
		return 0;
	if (trail < 0x40 || trail > 0xfc)
		return 0;
	return kCp932Table[leadIndex][trail - 0x40];
}


TextDecoder::TextDecoder(CsvEncoding encoding)
	:
	fEncoding(encoding == CsvEncoding::kAuto ? CsvEncoding::kUtf8 : encoding)
{
}


void
TextDecoder::Feed(const char* data, size_t size, std::string& out)
{
	std::string buffer = fPending;
	buffer.append(data, size);
	fPending.clear();
	_Decode(reinterpret_cast<const unsigned char*>(buffer.data()),
		buffer.size(), false, out);
}


void
TextDecoder::Finish(std::string& out)
{
	std::string buffer = fPending;
	fPending.clear();
	_Decode(reinterpret_cast<const unsigned char*>(buffer.data()),
		buffer.size(), true, out);
}


void
TextDecoder::_Decode(const unsigned char* data, size_t size, bool final,
	std::string& out)
{
	size_t i = 0;
	if (fStart && fEncoding == CsvEncoding::kUtf8) {
		if (size < 3 && !final) {
			fPending.assign(reinterpret_cast<const char*>(data), size);
			return;
		}
		if (size >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf)
			i = 3;
	}
	fStart = false;
	out.reserve(out.size() + size + size / 2);
	while (i < size) {
		unsigned char c = data[i];
		if (c < 0x80) {
			out += char(c);
			i++;
			continue;
		}
		size_t remaining = size - i;
		if (fEncoding == CsvEncoding::kUtf8) {
			int length = Utf8Sequence(data + i, remaining);
			if (length == 0 && !final) {
				fPending.assign(reinterpret_cast<const char*>(data + i),
					remaining);
				return;
			}
			if (length <= 0) {
				AppendUtf8(out, kReplacement);
				fErrors++;
				i++;
				continue;
			}
			out.append(reinterpret_cast<const char*>(data + i), length);
			i += size_t(length);
		} else if (fEncoding == CsvEncoding::kShiftJis) {
			if (c >= 0xa1 && c <= 0xdf) {
				AppendUtf8(out, 0xff61 + (c - 0xa1));
				i++;
				continue;
			}
			if ((c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc)) {
				if (remaining < 2) {
					if (!final) {
						fPending.assign(reinterpret_cast<const char*>(
							data + i), remaining);
						return;
					}
					AppendUtf8(out, kReplacement);
					fErrors++;
					i++;
					continue;
				}
				uint16_t cp = Cp932Lookup(c, data[i + 1]);
				if (cp == 0) {
					AppendUtf8(out, kReplacement);
					fErrors++;
					// A bad trail byte may be ASCII; do not swallow it.
					i += data[i + 1] >= 0x40 ? 2 : 1;
					continue;
				}
				AppendUtf8(out, cp);
				i += 2;
				continue;
			}
			AppendUtf8(out, kReplacement);
			fErrors++;
			i++;
		} else {
			// EUC-JP
			size_t need = c == 0x8f ? 3 : 2;
			if ((c == 0x8e || c == 0x8f || (c >= 0xa1 && c <= 0xfe))
				&& remaining < need) {
				if (!final) {
					fPending.assign(reinterpret_cast<const char*>(data + i),
						remaining);
					return;
				}
				AppendUtf8(out, kReplacement);
				fErrors++;
				i++;
				continue;
			}
			if (c == 0x8e) {
				unsigned char next = data[i + 1];
				if (next >= 0xa1 && next <= 0xdf) {
					AppendUtf8(out, 0xff61 + (next - 0xa1));
					i += 2;
				} else {
					AppendUtf8(out, kReplacement);
					fErrors++;
					i++;
				}
				continue;
			}
			if (c == 0x8f) {
				// JIS X 0212 is not supported.
				AppendUtf8(out, kReplacement);
				fErrors++;
				i += (data[i + 1] >= 0xa1 && data[i + 2] >= 0xa1) ? 3 : 1;
				continue;
			}
			if (c >= 0xa1 && c <= 0xfe) {
				unsigned char next = data[i + 1];
				uint16_t cp = next >= 0xa1 && next <= 0xfe
					? kEucJpTable[c - 0xa1][next - 0xa1] : 0;
				if (cp == 0) {
					AppendUtf8(out, kReplacement);
					fErrors++;
					i += next >= 0xa1 ? 2 : 1;
					continue;
				}
				AppendUtf8(out, cp);
				i += 2;
				continue;
			}
			AppendUtf8(out, kReplacement);
			fErrors++;
			i++;
		}
	}
}


CsvEncoding
DetectEncoding(const std::string& sample)
{
	const unsigned char* data
		= reinterpret_cast<const unsigned char*>(sample.data());
	size_t size = sample.size();
	if (size >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf)
		return CsvEncoding::kUtf8;
	bool validUtf8 = true;
	for (size_t i = 0; i < size;) {
		int length = Utf8Sequence(data + i, size - i);
		if (length == 0)
			break;		// cut at the end of the sample
		if (length < 0) {
			validUtf8 = false;
			break;
		}
		i += size_t(length);
	}
	if (validUtf8)
		return CsvEncoding::kUtf8;
	std::string ignored;
	TextDecoder sjis(CsvEncoding::kShiftJis);
	sjis.Feed(sample.data(), size, ignored);
	TextDecoder euc(CsvEncoding::kEucJp);
	euc.Feed(sample.data(), size, ignored);
	return euc.Errors() < sjis.Errors() ? CsvEncoding::kEucJp
		: CsvEncoding::kShiftJis;
}


// #pragma mark - parsing helpers


// Splits up to `maxLines` records of `text` with `delimiter`, returning the
// field counts (quotes respected; the last, possibly cut, line is dropped).
static std::vector<size_t>
FieldCounts(const std::string& text, char delimiter, size_t maxLines)
{
	std::vector<size_t> counts;
	size_t fields = 1;
	bool quoted = false;
	bool lineHasContent = false;
	for (size_t i = 0; i < text.size() && counts.size() < maxLines; i++) {
		char c = text[i];
		if (quoted) {
			if (c == '"') {
				if (i + 1 < text.size() && text[i + 1] == '"')
					i++;
				else
					quoted = false;
			}
			continue;
		}
		if (c == '"') {
			quoted = true;
			lineHasContent = true;
		} else if (c == delimiter) {
			fields++;
			lineHasContent = true;
		} else if (c == '\n' || c == '\r') {
			if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
				i++;
			if (lineHasContent)
				counts.push_back(fields);
			fields = 1;
			lineHasContent = false;
		} else
			lineHasContent = true;
	}
	return counts;
}


char
DetectDelimiter(const std::string& text)
{
	static const char kCandidates[] = { ',', '\t', ';' };
	char best = ',';
	size_t bestScore = 0;
	for (char candidate : kCandidates) {
		std::vector<size_t> counts = FieldCounts(text, candidate, 50);
		// The most common field count above 1, and how many lines have it.
		std::map<size_t, size_t> histogram;
		for (size_t count : counts) {
			if (count > 1)
				histogram[count]++;
		}
		size_t score = 0;
		for (const auto& entry : histogram)
			score = std::max(score, entry.second);
		if (score > bestScore) {
			bestScore = score;
			best = candidate;
		}
	}
	return best;
}


// #pragma mark - CsvReader


CsvReader::CsvReader()
{
}


CsvReader::~CsvReader()
{
	if (fFD >= 0)
		close(fFD);
}


void
CsvReader::SetLanguage(const std::string& lang)
{
	sLanguage = lang == "en" ? "en" : "ja";
}


static std::string
ColumnName(size_t index)
{
	char buffer[32];
	if (sLanguage == "en")
		snprintf(buffer, sizeof(buffer), "Column %zu", index + 1);
	else
		snprintf(buffer, sizeof(buffer), "列%zu", index + 1);
	return buffer;
}


Status
CsvReader::Open(const std::string& path, const CsvOptions& options)
{
	struct stat st;
	if (stat(path.c_str(), &st) != 0)
		return Status(ErrorKind::kNotFound, path);
	if (!S_ISREG(st.st_mode))
		return Status(ErrorKind::kInvalidFormat, "not a file");
	if (uint64_t(st.st_size) > kMaxCsvBytes)
		return Status(ErrorKind::kLimitExceeded, "file size");
	fFD = open(path.c_str(), O_RDONLY);
	if (fFD < 0)
		return Status(ErrorKind::kReadFailed, strerror(errno));
	fTotalBytes = uint64_t(st.st_size);

	std::string sample;
	sample.resize(64 * 1024);
	ssize_t count = pread(fFD, &sample[0], sample.size(), 0);
	if (count < 0)
		return Status(ErrorKind::kReadFailed, strerror(errno));
	sample.resize(size_t(count));
	return _Start(options, sample);
}


Status
CsvReader::OpenString(const std::string& data, const CsvOptions& options)
{
	fFromString = true;
	fData = data;
	fTotalBytes = data.size();
	return _Start(options, data.substr(0, 64 * 1024));
}


Status
CsvReader::_Start(const CsvOptions& options, const std::string& sample)
{
	fEncoding = options.encoding == CsvEncoding::kAuto
		? DetectEncoding(sample) : options.encoding;
	fDecoder.reset(new TextDecoder(fEncoding));
	if (options.delimiter != 0)
		fDelimiter = options.delimiter;
	else {
		TextDecoder decoder(fEncoding);
		std::string text;
		decoder.Feed(sample.data(), sample.size(), text);
		// Skip the lines the user asked to skip before guessing.
		size_t start = 0;
		for (int i = 0; i < options.skipLines && start < text.size(); i++) {
			size_t end = text.find_first_of("\r\n", start);
			if (end == std::string::npos) {
				start = text.size();
				break;
			}
			if (text[end] == '\r' && end + 1 < text.size()
				&& text[end + 1] == '\n') {
				end++;
			}
			start = end + 1;
		}
		fDelimiter = DetectDelimiter(text.substr(start));
	}

	Status status;
	CsvRecord record;
	for (int i = 0; i < options.skipLines; i++) {
		if (!_ParseRecord(record, status)) {
			if (!status)
				return status;
			break;
		}
	}
	// The first non-empty row: header names or the column count.
	bool found = false;
	while (_ParseRecord(record, status)) {
		if (record.fields.size() == 1 && record.fields[0].empty())
			continue;
		found = true;
		break;
	}
	if (!status)
		return status;
	fHeader.clear();
	if (found) {
		for (size_t i = 0; i < record.fields.size(); i++) {
			std::string name = options.header ? TrimSpaces(record.fields[i])
				: std::string();
			fHeader.push_back(name.empty() ? ColumnName(i) : name);
		}
		if (!options.header)
			fPushedBack.push_back(record);
	}
	return Status::Ok();
}


bool
CsvReader::_Fill()
{
	if (fEof)
		return false;
	// Drop what was parsed already.
	if (fPos > 0 && (fPos > 1024 * 1024 || fPos == fText.size())) {
		fText.erase(0, fPos);
		fPos = 0;
	}
	char buffer[64 * 1024];
	ssize_t count;
	if (fFromString) {
		count = ssize_t(std::min(sizeof(buffer), fData.size() - fDataOffset));
		memcpy(buffer, fData.data() + fDataOffset, size_t(count));
		fDataOffset += size_t(count);
	} else {
		do {
			count = read(fFD, buffer, sizeof(buffer));
		} while (count < 0 && errno == EINTR);
	}
	if (count <= 0) {
		fEof = true;
		fDecoder->Finish(fText);
		return true;
	}
	fBytesRead += uint64_t(count);
	fDecoder->Feed(buffer, size_t(count), fText);
	return true;
}


// Parses one record starting at fPos. Returns false at the end of the
// input (status OK) or on an error.
bool
CsvReader::_ParseRecord(CsvRecord& out, Status& status)
{
	status = Status::Ok();
	while (true) {
		out.fields.clear();
		out.line = fLine;
		size_t i = fPos;
		size_t lines = 0;
		std::string field;
		bool quoted = false;
		bool complete = false;
		bool atFieldStart = true;
		while (i < fText.size()) {
			char c = fText[i];
			if (quoted) {
				if (c == '"') {
					if (i + 1 >= fText.size() && !fEof)
						break;		// need to see the next character
					if (i + 1 < fText.size() && fText[i + 1] == '"') {
						field += '"';
						i += 2;
					} else {
						quoted = false;
						i++;
					}
					continue;
				}
				if (c == '\r') {
					if (i + 1 >= fText.size() && !fEof)
						break;
					lines++;
					if (i + 1 < fText.size() && fText[i + 1] == '\n') {
						field += "\r\n";
						i += 2;
						continue;
					}
				} else if (c == '\n')
					lines++;
				field += c;
				i++;
			} else if (c == '"' && atFieldStart) {
				quoted = true;
				atFieldStart = false;
				i++;
				continue;
			} else if (c == fDelimiter) {
				out.fields.push_back(std::move(field));
				field.clear();
				atFieldStart = true;
				i++;
				continue;
			} else if (c == '\r' || c == '\n') {
				if (c == '\r') {
					if (i + 1 >= fText.size() && !fEof)
						break;
					if (i + 1 < fText.size() && fText[i + 1] == '\n')
						i++;
				}
				i++;
				lines++;
				complete = true;
				break;
			} else {
				field += c;
				atFieldStart = false;
				i++;
			}
			if (field.size() > kMaxCsvFieldBytes) {
				status = Status(ErrorKind::kLimitExceeded, "field");
				return false;
			}
		}
		if (!complete && !fEof) {
			// The record continues beyond the decoded text.
			_Fill();
			continue;
		}
		if (field.size() > kMaxCsvFieldBytes) {
			status = Status(ErrorKind::kLimitExceeded, "field");
			return false;
		}
		if (!complete && i == fPos && out.fields.empty() && field.empty()
			&& !quoted) {
			fPos = i;
			return false;	// nothing left
		}
		out.fields.push_back(std::move(field));
		fPos = i;
		fLine += lines;
		return true;
	}
}


bool
CsvReader::Next(CsvRecord& out, Status& status)
{
	status = Status::Ok();
	if (!fPushedBack.empty()) {
		out = fPushedBack.back();
		fPushedBack.pop_back();
		return true;
	}
	while (_ParseRecord(out, status)) {
		// Empty rows are ignored (3.2).
		if (out.fields.size() == 1 && out.fields[0].empty())
			continue;
		return true;
	}
	return false;
}


Status
ScanCsv(const std::string& path, const CsvOptions& options, CsvScan& out,
	const std::function<bool(uint64_t, uint64_t)>& progress)
{
	out = CsvScan();
	CsvReader reader;
	Status status = reader.Open(path, options);
	if (!status)
		return status;
	CsvRecord record;
	while (reader.Next(record, status)) {
		out.rows++;
		if (record.fields.size() != reader.ColumnCount())
			out.mismatched++;
		for (const std::string& field : record.fields)
			out.maxFieldBytes = std::max(out.maxFieldBytes, field.size());
		if (out.rows > kMaxCsvRows) {
			out.tooManyRows = true;
			return Status(ErrorKind::kLimitExceeded, "rows");
		}
		if (progress && (out.rows % 1000) == 0
			&& !progress(reader.BytesRead(), reader.TotalBytes())) {
			return Status(ErrorKind::kInvalidArgument, "canceled");
		}
	}
	if (status.Kind() == ErrorKind::kLimitExceeded)
		out.fieldTooLarge = true;
	return status;
}


// #pragma mark - columns


const char*
CsvUsageName(CsvUsage usage)
{
	switch (usage) {
		case CsvUsage::kIgnore:		return "ignore";
		case CsvUsage::kText:		return "text";
		case CsvUsage::kNumber:		return "number";
		case CsvUsage::kBool:		return "bool";
		case CsvUsage::kImageUrl:	return "image_url";
		case CsvUsage::kImagePath:	return "image_path";
		case CsvUsage::kLinkUrl:	return "link_url";
	}
	return "text";
}


static std::string
AsciiLower(const std::string& text)
{
	std::string out = text;
	for (char& c : out) {
		if (c >= 'A' && c <= 'Z')
			c = char(c - 'A' + 'a');
	}
	return out;
}


// I/O spec 3.6: true/false, はい/いいえ, 1/0.
static bool
ParseBoolText(const std::string& text, bool& out)
{
	std::string value = AsciiLower(TrimSpaces(text));
	if (value == "true" || value == "はい" || value == "1") {
		out = true;
		return true;
	}
	if (value == "false" || value == "いいえ" || value == "0") {
		out = false;
		return true;
	}
	return false;
}


static bool
IsWebUrl(const std::string& text)
{
	std::string scheme;
	return CheckUrl(text, &scheme) == LinkCheck::kOk
		&& (scheme == "http" || scheme == "https");
}


CsvUsage
GuessUrlUsage(const std::string& header, const std::vector<std::string>& values)
{
	static const char* const kLinkWords[] = {
		"精細", "高精細", "原寸", "原本", "詳細", "ページ", "リンク", "link",
		"full", "original", "large", "detail", "page"
	};
	static const char* const kImageWords[] = {
		"サムネイル", "サムネ", "画像", "写真", "thumbnail", "thumb", "image",
		"img", "photo"
	};
	std::string name = AsciiLower(header);
	for (const char* word : kLinkWords) {
		if (name.find(word) != std::string::npos)
			return CsvUsage::kLinkUrl;
	}
	for (const char* word : kImageWords) {
		if (name.find(word) != std::string::npos)
			return CsvUsage::kImageUrl;
	}
	static const char* const kExtensions[] = {
		".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp"
	};
	bool any = false;
	for (const std::string& raw : values) {
		std::string value = AsciiLower(TrimSpaces(raw));
		if (value.empty())
			continue;
		any = true;
		size_t end = value.find_first_of("?#");
		std::string path = value.substr(0, end);
		bool image = false;
		for (const char* extension : kExtensions) {
			size_t length = strlen(extension);
			if (path.size() > length
				&& path.compare(path.size() - length, length, extension)
					== 0) {
				image = true;
			}
		}
		if (!image)
			return CsvUsage::kLinkUrl;
	}
	return any ? CsvUsage::kImageUrl : CsvUsage::kLinkUrl;
}


// "精細画像URL" -> "精細画像"
static std::string
StripUrlSuffix(const std::string& header)
{
	std::string name = TrimSpaces(header);
	static const char* const kSuffixes[] = {
		"URL", "url", "Url", "リンク", "Link", "link", "LINK"
	};
	bool stripped = true;
	while (stripped) {
		stripped = false;
		for (const char* suffix : kSuffixes) {
			size_t length = strlen(suffix);
			if (name.size() > length
				&& name.compare(name.size() - length, length, suffix) == 0) {
				name = name.substr(0, name.size() - length);
				stripped = true;
			}
		}
		while (!name.empty() && (name.back() == ' ' || name.back() == '_'
				|| name.back() == '-')) {
			name.pop_back();
			stripped = true;
		}
	}
	return name;
}


std::string
LinkButtonText(const std::string& header, const std::string& lang)
{
	std::string name = StripUrlSuffix(header);
	if (lang == "en")
		return name.empty() ? std::string("Open") : "Open " + name;
	return name.empty() ? std::string("開く") : name + "を開く";
}


std::vector<CsvColumn>
GuessColumns(const std::vector<std::string>& header,
	const std::vector<std::vector<std::string>>& rows, const std::string& lang)
{
	std::vector<CsvColumn> columns;
	bool anyImage = false;
	for (size_t index = 0; index < header.size(); index++) {
		CsvColumn column;
		column.header = header[index];
		column.name = header[index];
		std::vector<std::string> values;
		size_t limit = std::min(rows.size(), kCsvInferRows);
		for (size_t row = 0; row < limit; row++) {
			if (index < rows[row].size())
				values.push_back(rows[row][index]);
		}
		bool any = false, numbers = true, bools = true, urls = true;
		for (const std::string& raw : values) {
			column.maxLength = std::max(column.maxLength,
				CountCodePoints(raw));
			std::string value = TrimSpaces(raw);
			if (value.empty())
				continue;
			any = true;
			double number;
			if (!ParseDecimal(value, number) || !IsValidNumber(number))
				numbers = false;
			bool flag;
			if (!ParseBoolText(value, flag))
				bools = false;
			if (!IsWebUrl(value))
				urls = false;
		}
		if (!any)
			column.usage = CsvUsage::kText;
		else if (bools)
			column.usage = CsvUsage::kBool;	// 0/1 only: a switch
		else if (numbers)
			column.usage = CsvUsage::kNumber;
		else if (urls)
			column.usage = GuessUrlUsage(column.header, values);
		else
			column.usage = CsvUsage::kText;
		column.multiline = column.usage == CsvUsage::kText
			&& column.maxLength > kCsvLongText;
		if (column.usage == CsvUsage::kImageUrl)
			anyImage = true;
		if (column.usage == CsvUsage::kLinkUrl)
			column.buttonText = LinkButtonText(column.header, lang);
		columns.push_back(column);
	}
	// The first link column is where the image points (3.3).
	if (anyImage) {
		for (CsvColumn& column : columns) {
			if (column.usage == CsvUsage::kLinkUrl) {
				column.linkImage = true;
				break;
			}
		}
	}
	return columns;
}


// #pragma mark - template


namespace {


class TemplateBuilder {
public:
	TemplateBuilder(const CsvLayoutOptions& options)
		:
		fOptions(options)
	{
	}

	Json Make(const std::string& kind, const std::string& name, int32_t x,
		int32_t y, int32_t w, int32_t h)
	{
		std::string id = NewPartId(PartIdPrefix(kind), fIds);
		fIds.insert(id);
		Rect rect;
		rect.x = x;
		rect.y = y;
		rect.w = std::max(int32_t(1), w);
		rect.h = std::max(int32_t(1), h);
		return part::Make(kind, id, name, rect);
	}

	Json Label(const std::string& text, int32_t x, int32_t y, int32_t w)
	{
		Json label = Make(kKindLabel, "", x, y, w, 20);
		label.Set("text", text);
		return label;
	}

private:
	const CsvLayoutOptions&	fOptions;
	std::set<std::string>	fIds;
};


}	// namespace


static void
SetSmallFont(Json& part, const CsvLayoutOptions& options)
{
	Json& style = part::MutableStyle(part);
	Json font = Json::MakeObject();
	font.Set("size", int64_t(std::max(6, options.baseFontSize - 2)));
	if (IsValidColor(options.smallTextColor))
		font.Set("color", options.smallTextColor);
	style.Set("font", std::move(font));
}


Status
BuildCsvTemplate(const std::vector<CsvColumn>& columns,
	const CsvLayoutOptions& options, Json& outTemplate,
	std::vector<CsvColumnParts>& outParts)
{
	const int32_t kMargin = 16, kGap = 8, kLine = 24, kMulti = 72,
		kLabel = 100, kNav = 28, kSmall = 18, kButton = 28;
	TemplateBuilder builder(options);
	outParts.assign(columns.size(), CsvColumnParts());

	int32_t width = options.width;
	int32_t height = options.height;
	if (width < kMinCardSize || width > kMaxCardSize || height < kMinCardSize
		|| height > kMaxCardSize) {
		return Status(ErrorKind::kInvalidArgument, "size");
	}

	int titleColumn = options.titleColumn;
	if (titleColumn < 0 || titleColumn >= int(columns.size())
		|| columns[titleColumn].usage != CsvUsage::kText) {
		titleColumn = -1;
		for (size_t i = 0; i < columns.size(); i++) {
			if (columns[i].usage == CsvUsage::kText) {
				titleColumn = int(i);
				break;
			}
		}
	}

	// Value parts in the right column, label on the left (3.5).
	struct Row {
		Json	label;
		Json	field;
		int32_t	height;
	};
	std::vector<Row> rows;
	std::vector<Json> images, buttons, urls;
	for (size_t i = 0; i < columns.size(); i++) {
		const CsvColumn& column = columns[i];
		std::string name = column.name.empty() ? column.header : column.name;
		switch (column.usage) {
			case CsvUsage::kText:
			case CsvUsage::kNumber:
			case CsvUsage::kBool:
			{
				std::string kind = column.usage == CsvUsage::kText ? kKindText
					: column.usage == CsvUsage::kNumber ? kKindNumber
					: kKindBool;
				int32_t fieldHeight = kLine;
				if (column.usage == CsvUsage::kText && column.multiline)
					fieldHeight = kMulti;
				Row row;
				row.field = builder.Make(kind, name, 0, 0, 1, fieldHeight);
				if (kind == kKindText)
					row.field.Set("multiline", column.multiline);
				if (int(i) == titleColumn && !column.multiline) {
					// The title is shown a little larger.
					Json font = Json::MakeObject();
					font.Set("size", int64_t(std::min(96,
						options.baseFontSize + 4)));
					part::MutableStyle(row.field).Set("font", font);
					fieldHeight = kLine + 4;
					Rect rect = part::GetRect(row.field);
					rect.h = fieldHeight;
					part::SetRect(row.field, rect);
				}
				row.label = builder.Label(name, 0, 0, kLabel);
				row.height = fieldHeight;
				outParts[i].value = part::Id(row.field);
				rows.push_back(std::move(row));
				break;
			}
			case CsvUsage::kImageUrl:
			case CsvUsage::kImagePath:
			{
				// "サムネイルURL": the image is "サムネイル", the kept URL
				// keeps the column name.
				std::string imageName = name;
				std::string urlName = name + " URL";
				if (column.usage == CsvUsage::kImageUrl) {
					std::string stripped = StripUrlSuffix(name);
					if (!stripped.empty() && stripped != name) {
						imageName = stripped;
						urlName = name;
					}
				}
				Json image = builder.Make(kKindImage, imageName, 0, 0, 1, 1);
				outParts[i].value = part::Id(image);
				if (column.usage == CsvUsage::kImageUrl && column.keepUrl) {
					Json url = builder.Make(kKindText, urlName, 0, 0, 1,
						kSmall);
					SetSmallFont(url, options);
					outParts[i].url = part::Id(url);
					Json source = Json::MakeObject();
					source.Set("field", outParts[i].url);
					source.Set("max_edge", int64_t(std::max(32,
						std::min(2048, column.maxEdge))));
					image.Set("source", std::move(source));
					// Clicking the image opens its URL (3.3).
					Json link = Json::MakeObject();
					link.Set("field", outParts[i].url);
					image.Set("link", std::move(link));
					urls.push_back(std::move(url));
				}
				images.push_back(std::move(image));
				break;
			}
			case CsvUsage::kLinkUrl:
			{
				Json url = builder.Make(kKindText, name, 0, 0, 1, kSmall);
				SetSmallFont(url, options);
				outParts[i].url = part::Id(url);
				if (column.makeButton) {
					Json button = builder.Make(kKindButton, "", 0, 0, 1,
						kButton);
					button.Set("text", column.buttonText.empty()
						? LinkButtonText(column.header, options.lang)
						: column.buttonText);
					button.Set("action", "link.open");
					Json link = Json::MakeObject();
					link.Set("field", outParts[i].url);
					button.Set("link", std::move(link));
					outParts[i].button = part::Id(button);
					buttons.push_back(std::move(button));
				}
				urls.push_back(std::move(url));
				break;
			}
			case CsvUsage::kIgnore:
				break;
		}
	}
	// The image links to the chosen link column instead of its own URL.
	for (size_t i = 0; i < columns.size(); i++) {
		if (columns[i].usage != CsvUsage::kLinkUrl || !columns[i].linkImage
			|| images.empty()) {
			continue;
		}
		Json link = Json::MakeObject();
		link.Set("field", outParts[i].url);
		images[0].Set("link", std::move(link));
		break;
	}

	bool hasImage = !images.empty();
	// Under the image (or after the fields): buttons, then URL fields.
	int32_t underHeight = 0;
	for (size_t i = 0; i < buttons.size(); i++)
		underHeight += kButton + kGap;
	for (size_t i = 0; i < urls.size(); i++)
		underHeight += kSmall + 4;

	int32_t rowsHeight = 0;
	for (const Row& row : rows)
		rowsHeight += row.height + kGap;

	bool customPaper = options.paperSize.empty()
		|| options.paperSize == "custom";
	int columnsUsed = 1;
	auto available = [&]() { return height - kMargin * 2 - kNav - kGap; };
	int32_t leftWidth = hasImage
		? int32_t(std::lround((width - kMargin * 2) * 0.4)) : 0;
	int32_t rightX = hasImage ? kMargin + leftWidth + 16 : kMargin;
	int32_t rightWidth = width - kMargin - rightX;
	int32_t neededRight = rowsHeight + (hasImage ? 0 : underHeight);
	int32_t neededLeft = hasImage ? underHeight + 64 : 0;
	bool grewHeight = false;
	if (std::max(neededRight, neededLeft) > available()) {
		if (!customPaper && rows.size() > 1) {
			// Two columns on a fixed paper (3.5).
			columnsUsed = 2;
			int32_t half = 0, other = 0;
			for (size_t i = 0; i < rows.size(); i++) {
				if (i < (rows.size() + 1) / 2)
					half += rows[i].height + kGap;
				else
					other += rows[i].height + kGap;
			}
			neededRight = std::max(half, other)
				+ (hasImage ? 0 : underHeight);
		}
		int32_t needed = std::max(neededRight, neededLeft);
		if (needed > available()) {
			height = std::min(kMaxCardSize, needed + kMargin * 2 + kNav
				+ kGap);
			grewHeight = true;
		}
	}

	JsonArray parts;
	// Right column(s)
	int32_t columnWidth = columnsUsed == 1 ? rightWidth
		: (rightWidth - 16) / 2;
	int32_t y = kMargin;
	size_t split = columnsUsed == 1 ? rows.size() : (rows.size() + 1) / 2;
	for (size_t i = 0; i < rows.size(); i++) {
		if (i == split)
			y = kMargin;
		int32_t x = rightX + (i >= split ? columnWidth + 16 : 0);
		Row& row = rows[i];
		int32_t labelWidth = std::min(kLabel, columnWidth / 3);
		Rect labelRect;
		labelRect.x = x;
		labelRect.y = y + (row.height > kLine ? 2 : 3);
		labelRect.w = labelWidth;
		labelRect.h = 20;
		part::SetRect(row.label, labelRect);
		Rect fieldRect;
		fieldRect.x = x + labelWidth + 4;
		fieldRect.y = y;
		fieldRect.w = std::max(int32_t(16), columnWidth - labelWidth - 4);
		if (part::Kind(row.field) == kKindBool)
			fieldRect.w = std::min(fieldRect.w, int32_t(80));
		fieldRect.h = row.height;
		part::SetRect(row.field, fieldRect);
		parts.push_back(row.label);
		parts.push_back(row.field);
		y += row.height + kGap;
	}
	int32_t contentBottom = height - kMargin - kNav - kGap;

	// Left column: images, then buttons and URLs below them.
	int32_t underX = hasImage ? kMargin : rightX;
	int32_t underWidth = hasImage ? leftWidth : rightWidth;
	int32_t underY = hasImage ? contentBottom - underHeight + kGap : y;
	if (hasImage) {
		int32_t imageArea = underY - kGap - kMargin;
		int32_t count = int32_t(images.size());
		int32_t each = std::max(int32_t(16),
			(imageArea - kGap * (count - 1)) / count);
		int32_t imageY = kMargin;
		for (Json& image : images) {
			Rect rect;
			rect.x = kMargin;
			rect.y = imageY;
			rect.w = leftWidth;
			rect.h = each;
			part::SetRect(image, rect);
			image.Set("fit", "contain");
			parts.push_back(image);
			imageY += each + kGap;
		}
	}
	for (Json& button : buttons) {
		Rect rect;
		rect.x = underX;
		rect.y = underY;
		rect.w = underWidth;
		rect.h = kButton;
		part::SetRect(button, rect);
		parts.push_back(button);
		underY += kButton + kGap;
	}
	for (Json& url : urls) {
		Rect rect;
		rect.x = underX;
		rect.y = underY;
		rect.w = underWidth;
		rect.h = kSmall;
		part::SetRect(url, rect);
		parts.push_back(url);
		underY += kSmall + 4;
	}

	// Navigation at the bottom (3.5): previous, position, next (and first
	// / last for the full set).
	{
		int32_t navY = height - kMargin - kNav;
		struct NavItem {
			const char*	kind;
			const char*	text;
			const char*	action;
			int32_t		w;
		};
		std::vector<NavItem> items;
		if (options.fullNavigation)
			items.push_back({kKindButton, "◀◀", "card.first", 60});
		items.push_back({kKindButton, "◀", "card.prev", 60});
		items.push_back({kKindCounter, "", "", 72});
		items.push_back({kKindButton, "▶", "card.next", 60});
		if (options.fullNavigation)
			items.push_back({kKindButton, "▶▶", "card.last", 60});
		int32_t total = 0;
		for (const NavItem& item : items)
			total += item.w;
		total += kGap * int32_t(items.size() - 1);
		int32_t x = (width - total) / 2;
		for (const NavItem& item : items) {
			bool counter = std::string(item.kind) == kKindCounter;
			Json part = builder.Make(item.kind, "", x, counter ? navY + 4
				: navY, item.w, counter ? 20 : kNav);
			if (!counter) {
				part.Set("text", item.text);
				part.Set("action", item.action);
			}
			parts.push_back(part);
			x += item.w + kGap;
		}
	}

	if (parts.size() > kMaxParts)
		return Status(ErrorKind::kLimitExceeded, "parts");

	Template tmpl = Template::Create(options.title.empty() ? kUntitled
		: options.title, width, height);
	if (!options.paperSize.empty()) {
		if (grewHeight)
			tmpl.SetPaper("custom", options.orientation, width, height);
		else
			tmpl.SetPaper(options.paperSize, options.orientation, width,
				height);
	}
	if (IsValidThemeId(options.themeId))
		tmpl.SetThemeId(options.themeId);
	if (titleColumn >= 0)
		tmpl.SetTitlePart(outParts[titleColumn].value);
	tmpl.Parts() = parts;
	std::string now = NowTimestamp();
	tmpl.Doc().Set("created", now);
	tmpl.Doc().Set("modified", now);
	Status status = Template::Validate(tmpl.Doc());
	if (!status)
		return status;
	outTemplate = tmpl.Doc();
	return Status::Ok();
}


// #pragma mark - import


std::string
CsvImportReport::Text(const std::string& lang) const
{
	bool en = lang == "en";
	std::string text;
	char line[256];
	auto add = [&](const char* ja, const char* english, size_t value) {
		snprintf(line, sizeof(line), "%s: %zu\n", en ? english : ja, value);
		text += line;
	};
	add("読み込んだ行数", "Rows read", rows);
	add("作成したカード", "Cards created", cards);
	add("スキップした行(保存できない)", "Rows skipped (could not save)",
		skippedWrite);
	add("スキップした行(上限超過)", "Rows skipped (over a limit)",
		skippedLimit);
	add("数値などに変換できなかった値", "Values that could not be converted",
		valueErrors);
	add("列数が合わない行", "Rows with a different number of fields",
		mismatched);
	add("画像の取得に成功", "Images fetched", imagesOk);
	add("画像の取得に失敗", "Images failed", imagesFailed);
	for (const auto& entry : imageFailures) {
		snprintf(line, sizeof(line), "  %s: %zu\n", entry.first.c_str(),
			entry.second);
		text += line;
	}
	if (!failedLines.empty()) {
		text += en ? "Failed rows (CSV line numbers):" : "失敗した行(CSVの行番号):";
		for (size_t i = 0; i < failedLines.size(); i++) {
			snprintf(line, sizeof(line), "%s%zu", i == 0 ? " " : ", ",
				failedLines[i]);
			text += line;
		}
		text += "\n";
	}
	if (canceled)
		text += en ? "Canceled.\n" : "キャンセルしました。\n";
	return text;
}


static void
CountImageFailure(CsvImportReport& report, const std::string& reason)
{
	report.imagesFailed++;
	report.imageFailures[reason.empty() ? "unknown" : reason]++;
}


Status
ImportCsvRows(Stack& stack, const std::string& templateId, CsvReader& reader,
	const std::vector<CsvColumn>& columns,
	const std::vector<CsvColumnParts>& parts, CsvImageSource* images,
	const CsvProgress& progress, CsvImportReport& report)
{
	const Template* tmpl = stack.FindTemplate(templateId);
	if (tmpl == nullptr)
		return Status(ErrorKind::kNotFound, templateId);
	if (parts.size() != columns.size())
		return Status(ErrorKind::kInvalidArgument, "parts");

	// Bulk writes: one sync at the end (12.4, 3.6).
	stack.Store().SetDeferSync(true);
	Status status;
	CsvRecord record;
	while (reader.Next(record, status)) {
		report.rows++;
		if (record.fields.size() != reader.ColumnCount())
			report.mismatched++;
		if (report.rows > kMaxCsvRows) {
			report.skippedLimit++;
			report.failedLines.push_back(record.line);
			break;
		}
		Json values = Json::MakeObject();
		for (size_t i = 0; i < columns.size(); i++) {
			const CsvColumn& column = columns[i];
			const CsvColumnParts& target = parts[i];
			std::string raw = i < record.fields.size() ? record.fields[i]
				: std::string();
			std::string trimmed = TrimSpaces(raw);
			CsvUsage usage = column.usage;
			std::string valuePart = target.value;
			if (!column.targetPart.empty()) {
				// Existing template: the part's kind decides.
				const Json* part = tmpl->FindPart(column.targetPart);
				if (part == nullptr || usage == CsvUsage::kIgnore)
					continue;
				valuePart = column.targetPart;
				std::string kind = part::Kind(*part);
				if (kind == kKindText)
					usage = CsvUsage::kText;
				else if (kind == kKindNumber)
					usage = CsvUsage::kNumber;
				else if (kind == kKindBool)
					usage = CsvUsage::kBool;
				else if (kind == kKindImage) {
					if (usage != CsvUsage::kImagePath)
						usage = CsvUsage::kImageUrl;
				} else
					continue;
			}
			switch (usage) {
				case CsvUsage::kIgnore:
					break;
				case CsvUsage::kText:
					if (!valuePart.empty())
						values.Set(valuePart, raw);
					break;
				case CsvUsage::kNumber:
				{
					if (valuePart.empty())
						break;
					double number;
					if (trimmed.empty())
						values.Set(valuePart, Json());
					else if (ParseDecimal(trimmed, number)
						&& IsValidNumber(number)) {
						values.Set(valuePart, MakeNumberValue(number));
					} else {
						values.Set(valuePart, Json());
						report.valueErrors++;
					}
					break;
				}
				case CsvUsage::kBool:
				{
					if (valuePart.empty())
						break;
					bool flag;
					if (trimmed.empty())
						values.Set(valuePart, Json());
					else if (ParseBoolText(trimmed, flag))
						values.Set(valuePart, flag);
					else {
						values.Set(valuePart, Json());
						report.valueErrors++;
					}
					break;
				}
				case CsvUsage::kLinkUrl:
					if (!target.url.empty())
						values.Set(target.url, trimmed);
					break;
				case CsvUsage::kImageUrl:
				case CsvUsage::kImagePath:
				{
					if (!target.url.empty())
						values.Set(target.url, trimmed);
					bool now = usage == CsvUsage::kImagePath
						|| target.url.empty();
					if (!now || trimmed.empty() || valuePart.empty())
						break;
					if (images == nullptr) {
						CountImageFailure(report, "no_source");
						break;
					}
					std::string data, ext, reason;
					bool ok;
					if (usage == CsvUsage::kImagePath) {
						ok = images->FromFile(trimmed, column.maxEdge, data, ext,
							reason);
					} else if (!IsWebUrl(trimmed)) {
						ok = false;
						reason = "invalid_url";
					} else {
						ok = images->FromUrl(trimmed, column.maxEdge, data, ext,
							reason);
					}
					if (!ok) {
						CountImageFailure(report, reason);
						break;
					}
					StringInStream stream(data);
					Json ref;
					Status mediaStatus = stack.AddMedia(kKindImage, ext, stream,
						false, templateId, valuePart, ref);
					if (!mediaStatus) {
						CountImageFailure(report, mediaStatus.Code());
						break;
					}
					values.Set(valuePart, ref);
					report.imagesOk++;
					break;
				}
			}
		}
		Status saved = stack.AppendCard(templateId, values, nullptr);
		if (!saved) {
			report.skippedWrite++;
			report.failedLines.push_back(record.line);
		} else
			report.cards++;
		if (progress && !progress(report)) {
			report.canceled = true;
			break;
		}
	}
	stack.Store().SetDeferSync(false);
	stack.Store().SyncAll();
	if (!status) {
		if (status.Kind() == ErrorKind::kLimitExceeded)
			report.skippedLimit++;
		return status;
	}
	return Status::Ok();
}


Status
CreateCsvStack(const std::string& destDir, const std::string& title,
	const Json& templateDoc, LogSink* sink, std::string& outStagingDir,
	std::string& outManifestName)
{
	struct stat st;
	if (lstat(destDir.c_str(), &st) == 0)
		return Status(ErrorKind::kExists, destDir);
	std::string staging;
	Status status = MakeStagingDirectory(destDir, staging);
	if (!status)
		return status;
	status = MakeDirectories(staging + "/templates");
	if (status)
		status = MakeDirectories(staging + "/cards");
	if (status)
		status = MakeDirectories(staging + "/media/image");
	FolderStore store(staging);
	StoredDoc stored;
	stored.doc = templateDoc;
	Template tmpl(stored);
	if (status) {
		status = store.WriteDoc(stored, "templates/"
			+ MakeFileName(tmpl.Title(), tmpl.Id(), ".rfdt"), false);
	}
	Manifest manifest = Manifest::Create(title, tmpl.Id());
	std::string manifestName = SanitizeTitle(title) + ".rfdm";
	if (status)
		status = store.WriteDoc(manifest.Stored(), manifestName, false);
	if (!status) {
		RemoveTree(staging);
		return status;
	}
	Logger logger(sink, manifest.StackId());
	logger.Info("stack.create", {{"preset", Json()}, {"source", "csv"}});
	outStagingDir = staging;
	outManifestName = manifestName;
	return Status::Ok();
}


Status
FinishCsvStack(const std::string& stagingDir, const std::string& destDir)
{
	struct stat st;
	if (lstat(destDir.c_str(), &st) == 0)
		return Status(ErrorKind::kExists, destDir);
	if (rename(stagingDir.c_str(), destDir.c_str()) != 0)
		return Status(ErrorKind::kWriteFailed, strerror(errno));
	return Status::Ok();
}


void
LogCsvImport(Logger& logger, const CsvImportReport& report)
{
	logger.Info("stack.csv_import", {
		{"rows", int64_t(report.rows)},
		{"cards", int64_t(report.cards)},
		{"skipped", int64_t(report.Skipped())},
		{"value_errors", int64_t(report.valueErrors)},
		{"images_ok", int64_t(report.imagesOk)},
		{"images_failed", int64_t(report.imagesFailed)}});
}


// #pragma mark - fetching


std::vector<FetchItem>
FindImagesToFetch(const Stack& stack)
{
	std::vector<FetchItem> items;
	for (int i = 0; i < stack.CardCount(); i++) {
		const Card* card = stack.CardAt(i);
		const Template* tmpl = stack.FindTemplate(card->TemplateId());
		if (tmpl == nullptr)
			continue;
		for (const Json& part : tmpl->Parts()) {
			if (part::Kind(part) != kKindImage)
				continue;
			const Json* source = part.Find("source");
			if (source == nullptr || !source->IsObject())
				continue;
			if (IsMediaRef(EffectiveValue(*card, part)))
				continue;
			// Only empty images: an old value of another type stays.
			if (HasMismatchedValue(*card, part))
				continue;
			const Json* field = tmpl->FindPart(source->GetString("field"));
			if (field == nullptr || part::Kind(*field) != kKindText)
				continue;
			std::string url = TrimSpaces(EffectiveValue(*card, *field)
				.AsString());
			if (!IsWebUrl(url))
				continue;
			FetchItem item;
			item.cardId = card->Id();
			item.partId = part::Id(part);
			item.url = url;
			item.maxEdge = int(source->GetInt("max_edge",
				kDefaultThumbnailEdge));
			items.push_back(item);
		}
	}
	return items;
}


}	// namespace bdb
