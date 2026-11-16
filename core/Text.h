/*
 * BDB card database - core
 * UTF-8 helpers, file name sanitizing (spec v0.2, chapter 10) and time
 * stamps (5.4).
 */
#ifndef BDB_TEXT_H
#define BDB_TEXT_H

#include <cstdint>
#include <string>


namespace bdb {


// The literal used for empty titles (8.5, 10).
extern const char* const kUntitled;

size_t CountCodePoints(const std::string& text);
std::string TrimSpaces(const std::string& text);
std::string FirstLine(const std::string& text);

// Chapter 10: replace forbidden characters, trim spaces and dots, cut to
// 40 code points / 120 bytes, "無題" when empty.
std::string SanitizeTitle(const std::string& title);
// "<sanitized title>-<first 6 id chars>.<extension>"
std::string MakeFileName(const std::string& title, const std::string& id,
	const char* extension);

// RFC 3339 UTC, seconds precision ("2026-10-14T12:34:56Z").
std::string NowTimestamp();
// Milliseconds precision for the log ("2026-10-14T12:34:56.123Z").
std::string NowTimestampMillis();
// "YYYY-MM" of the current UTC month.
std::string CurrentMonth();

// ASCII case folding for "大文字小文字を区別しない" substring matches.
std::string FoldCase(const std::string& text);


}	// namespace bdb

#endif	// BDB_TEXT_H
