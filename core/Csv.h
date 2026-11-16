/*
 * BDB card database - core
 * CSV import (I/O spec v0.1, chapter 3): reading (3.2), guessing how
 * columns are used (3.4), the automatic template (3.5) and creating the
 * cards (3.6). Fetching and scaling images is done by the caller through
 * CsvImageSource (network and image decoding are not part of the core).
 */
#ifndef BDB_CSV_H
#define BDB_CSV_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Json.h"
#include "Log.h"
#include "Status.h"


namespace bdb {


class Stack;


// #pragma mark - encodings


enum class CsvEncoding {
	kAuto = 0,
	kUtf8,
	kShiftJis,		// CP932
	kEucJp
};

const char* CsvEncodingName(CsvEncoding encoding);	// "utf-8", ...

// Guesses from the first bytes of a file: BOM or valid UTF-8, else the
// Japanese encoding with fewer invalid sequences (Shift_JIS on a tie).
CsvEncoding DetectEncoding(const std::string& sample);


// Converts a byte stream to UTF-8 piece by piece; sequences split between
// two Feed() calls are handled. Invalid bytes become U+FFFD.
class TextDecoder {
public:
							TextDecoder(CsvEncoding encoding);
			void			Feed(const char* data, size_t size,
								std::string& out);
			void			Finish(std::string& out);
			size_t			Errors() const { return fErrors; }

private:
			void			_Decode(const unsigned char* data, size_t size,
								bool final, std::string& out);

			CsvEncoding		fEncoding;
			std::string		fPending;
			bool			fStart = true;
			size_t			fErrors = 0;
};


// #pragma mark - reading


struct CsvOptions {
	CsvEncoding	encoding = CsvEncoding::kAuto;
	char		delimiter = 0;		// 0: detect (',', '\t' or ';')
	bool		header = true;		// the first row holds the names
	int			skipLines = 0;		// rows to skip before the data (a title
									// line above the header; prototype
									// extension)
};


struct CsvRecord {
	std::vector<std::string> fields;
	size_t		line = 0;			// first line of the row in the file
};


// Guesses the delimiter from the start of the (UTF-8) text: the candidate
// that splits the most lines into the same number of fields.
char DetectDelimiter(const std::string& text);


// Streams the rows of a CSV file (RFC 4180; quoted fields, "" quotes,
// line breaks inside quotes, CRLF / LF / CR). Empty rows are skipped. The
// whole file is never loaded into memory.
class CsvReader {
public:
							CsvReader();
							~CsvReader();

	// Checks the file size limit (512 MiB) before reading.
			Status			Open(const std::string& path,
								const CsvOptions& options);
	// For previews and tests.
			Status			OpenString(const std::string& data,
								const CsvOptions& options);

			CsvEncoding		Encoding() const { return fEncoding; }
			char			Delimiter() const { return fDelimiter; }
	// Column names: the header row, or "列1", "列2", ... (en: "Column 1")
	// when there is no header. Set after Open().
	const	std::vector<std::string>& Header() const { return fHeader; }
			size_t			ColumnCount() const { return fHeader.size(); }

	// The next data row. Returns false at the end or on an error
	// (`status` tells which: limit_exceeded for an oversized field).
			bool			Next(CsvRecord& out, Status& status);

			uint64_t		BytesRead() const { return fBytesRead; }
			uint64_t		TotalBytes() const { return fTotalBytes; }

	// Language of the generated column names ("ja" or "en").
	static	void			SetLanguage(const std::string& lang);

private:
			Status			_Start(const CsvOptions& options,
								const std::string& sample);
			bool			_Fill();
			bool			_ParseRecord(CsvRecord& out, Status& status);

			int				fFD = -1;
			std::string		fData;			// OpenString
			bool			fFromString = false;
			size_t			fDataOffset = 0;
			bool			fEof = false;
			std::unique_ptr<TextDecoder> fDecoder;
			std::string		fText;			// decoded, not yet parsed
			size_t			fPos = 0;
			size_t			fLine = 1;
			CsvEncoding		fEncoding = CsvEncoding::kUtf8;
			char			fDelimiter = ',';
			std::vector<std::string> fHeader;
			std::vector<CsvRecord> fPushedBack;
			uint64_t		fBytesRead = 0;
			uint64_t		fTotalBytes = 0;
};


// A full pass over the file before importing: the row count, the widest
// field and rows whose field count differs from the columns (3.2, the
// confirmation screen 3.3).
struct CsvScan {
	size_t		rows = 0;
	size_t		mismatched = 0;
	size_t		maxFieldBytes = 0;
	bool		tooManyRows = false;
	bool		fieldTooLarge = false;
};

Status ScanCsv(const std::string& path, const CsvOptions& options,
	CsvScan& out, const std::function<bool(uint64_t, uint64_t)>& progress
		= nullptr);


// #pragma mark - columns


enum class CsvUsage {
	kIgnore = 0,
	kText,
	kNumber,
	kBool,
	kImageUrl,
	kImagePath,
	kLinkUrl
};

const char* CsvUsageName(CsvUsage usage);


struct CsvColumn {
	std::string	header;				// as in the file
	std::string	name;				// the part name (default: header)
	CsvUsage	usage = CsvUsage::kText;
	bool		multiline = false;	// long values (3.5)
	size_t		maxLength = 0;		// longest sample value (code points)
	// Image (URL): keep the URL as a text part that source/link refer to.
	bool		keepUrl = true;
	int			maxEdge = 256;		// thumbnail size (128, 256, 512)
	// Link (URL): make a button that opens it; link the image to it.
	bool		makeButton = true;
	std::string	buttonText;
	bool		linkImage = false;
	// Existing template mode: the part to fill (empty: ignore the column).
	std::string	targetPart;
};

// 3.4: guesses usages from up to 1000 sample rows and the header names.
// `rows` are data rows (without the header).
std::vector<CsvColumn> GuessColumns(const std::vector<std::string>& header,
	const std::vector<std::vector<std::string>>& rows,
	const std::string& lang);
// 3.4, the URL column rule: link words win over image words; with
// neither, image extensions decide.
CsvUsage GuessUrlUsage(const std::string& header,
	const std::vector<std::string>& values);
// "精細画像URL" -> "精細画像を開く" (3.3); en: "Open <name>".
std::string LinkButtonText(const std::string& header, const std::string& lang);


// #pragma mark - template


struct CsvLayoutOptions {
	std::string	title;				// template title
	int32_t		width = 512;
	int32_t		height = 342;
	std::string	paperSize;			// "" = no paper key
	std::string	orientation = "portrait";
	std::string	themeId = "beos";
	int			titleColumn = -1;	// -1: the first text column
	bool		fullNavigation = false;	// first/last buttons too
	int			baseFontSize = 12;	// the theme font size
	std::string	smallTextColor = "#808080";	// theme text_disabled
	std::string	lang = "ja";
};


// Part IDs created for one column.
struct CsvColumnParts {
	std::string	value;				// text/number/bool/image part
	std::string	url;				// URL text part (image keepUrl, link)
	std::string	button;				// link.open button
};


// 3.5: builds the template document (with fresh IDs) for the columns.
Status BuildCsvTemplate(const std::vector<CsvColumn>& columns,
	const CsvLayoutOptions& options, Json& outTemplate,
	std::vector<CsvColumnParts>& outParts);


// #pragma mark - import


// Makes the stored image for an image column (thumbnail, 3.6). Implemented
// by the application (Translation Kit, HTTP). `reason` is a short code:
// not_found, not_image, unreadable, connect, http, too_large, timeout, ...
class CsvImageSource {
public:
	virtual					~CsvImageSource() {}
	virtual	bool			FromFile(const std::string& path, int maxEdge,
								std::string& outData, std::string& outExt,
								std::string& reason) = 0;
	virtual	bool			FromUrl(const std::string& url, int maxEdge,
								std::string& outData, std::string& outExt,
								std::string& reason) = 0;
};


struct CsvImportReport {
	size_t		rows = 0;				// data rows read
	size_t		cards = 0;				// cards created
	size_t		skippedWrite = 0;		// rows whose card could not be saved
	size_t		skippedLimit = 0;		// rows over a limit
	size_t		valueErrors = 0;		// numbers / switches not readable
	size_t		mismatched = 0;			// rows with too few / many fields
	size_t		imagesOk = 0;
	size_t		imagesFailed = 0;
	std::map<std::string, size_t> imageFailures;	// reason -> count
	std::vector<size_t> failedLines;	// CSV line numbers (no values)
	bool		canceled = false;

			size_t	Skipped() const { return skippedWrite + skippedLimit; }
	// Plain text for "レポートを保存…": counts and line numbers only.
			std::string	Text(const std::string& lang) const;
};


// Called after every card; return false to cancel.
typedef std::function<bool(const CsvImportReport&)> CsvProgress;


// 3.6: one card per row in file order (seq step 1024 after the last card).
// `parts` comes from BuildCsvTemplate, or names existing parts through
// CsvColumn::targetPart. Image (file) columns, and image (URL) columns
// without keepUrl, are made through `images` right away (may be nullptr:
// they stay empty); URL images with keepUrl are left empty for a later
// fetch (FindImagesToFetch). Bulk writes skip the per-file fsync.
Status ImportCsvRows(Stack& stack, const std::string& templateId,
	CsvReader& reader, const std::vector<CsvColumn>& columns,
	const std::vector<CsvColumnParts>& parts, CsvImageSource* images,
	const CsvProgress& progress, CsvImportReport& report);

// New stack from a template (3.6, 19.4): built in a hidden staging folder
// next to `destDir`. The caller opens the stack at `outStagingDir`,
// imports, then calls FinishCsvStack() or RemoveTree(outStagingDir).
Status CreateCsvStack(const std::string& destDir, const std::string& title,
	const Json& templateDoc, LogSink* sink, std::string& outStagingDir,
	std::string& outManifestName);
Status FinishCsvStack(const std::string& stagingDir,
	const std::string& destDir);

void LogCsvImport(Logger& logger, const CsvImportReport& report);


// #pragma mark - fetching images


struct FetchItem {
	std::string	cardId;
	std::string	partId;
	std::string	url;
	int			maxEdge = 256;
};

// Image parts with a "source" whose value is empty while the source text
// part holds an http(s) URL (3.6 "再取得").
std::vector<FetchItem> FindImagesToFetch(const Stack& stack);


}	// namespace bdb

#endif	// BDB_CSV_H
