/*
 * BDB card database - core
 */
#include "Text.h"

#include "Limits.h"

#include <chrono>
#include <cstdio>
#include <ctime>


namespace bdb {


const char* const kUntitled = "無題";


// Length of the UTF-8 sequence starting with the given byte (1 for
// invalid bytes so that malformed input is still processed bytewise).
static size_t
SequenceLength(unsigned char c)
{
	if (c < 0x80)
		return 1;
	if ((c & 0xe0) == 0xc0)
		return 2;
	if ((c & 0xf0) == 0xe0)
		return 3;
	if ((c & 0xf8) == 0xf0)
		return 4;
	return 1;
}


size_t
CountCodePoints(const std::string& text)
{
	size_t count = 0;
	for (size_t i = 0; i < text.size();) {
		i += SequenceLength(text[i]);
		count++;
	}
	return count;
}


static bool
IsTrimSpaceAt(const std::string& text, size_t offset, size_t& length)
{
	unsigned char c = text[offset];
	if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v'
		|| c == '\f') {
		length = 1;
		return true;
	}
	// U+3000 IDEOGRAPHIC SPACE
	if (text.compare(offset, 3, "\xe3\x80\x80") == 0) {
		length = 3;
		return true;
	}
	return false;
}


static std::string
Trim(const std::string& text, bool dots)
{
	size_t start = 0;
	size_t end = text.size();
	while (start < end) {
		size_t length;
		if (dots && text[start] == '.')
			start++;
		else if (IsTrimSpaceAt(text, start, length))
			start += length;
		else
			break;
	}
	while (end > start) {
		if (dots && text[end - 1] == '.') {
			end--;
			continue;
		}
		size_t length;
		if (IsTrimSpaceAt(text, end - 1, length)) {
			end--;
			continue;
		}
		if (end - start >= 3 && IsTrimSpaceAt(text, end - 3, length)
			&& length == 3) {
			end -= 3;
			continue;
		}
		break;
	}
	return text.substr(start, end - start);
}


std::string
TrimSpaces(const std::string& text)
{
	return Trim(text, false);
}


std::string
FirstLine(const std::string& text)
{
	size_t end = text.find_first_of("\r\n");
	if (end == std::string::npos)
		return text;
	return text.substr(0, end);
}


std::string
SanitizeTitle(const std::string& title)
{
	// 1. Replace forbidden characters and control characters with '_'.
	std::string replaced;
	replaced.reserve(title.size());
	for (unsigned char c : title) {
		if (c < 0x20 || c == 0x7f || c == '/' || c == ':' || c == '*'
			|| c == '?' || c == '"' || c == '<' || c == '>' || c == '|'
			|| c == '\\') {
			replaced += '_';
		} else
			replaced += char(c);
	}

	// 2. Trim leading/trailing spaces and dots.
	std::string trimmed = Trim(replaced, true);

	// 3. At most 40 code points and 120 bytes, never cutting a sequence.
	std::string cut;
	size_t codePoints = 0;
	for (size_t i = 0; i < trimmed.size();) {
		size_t length = SequenceLength(trimmed[i]);
		if (i + length > trimmed.size())
			length = trimmed.size() - i;
		if (codePoints + 1 > kMaxTitleChars
			|| cut.size() + length > kMaxTitleBytes) {
			break;
		}
		cut.append(trimmed, i, length);
		i += length;
		codePoints++;
	}
	// Cutting can expose a trailing space or dot again.
	cut = Trim(cut, true);

	// 4. Empty -> "無題".
	if (cut.empty())
		return kUntitled;
	return cut;
}


std::string
MakeFileName(const std::string& title, const std::string& id,
	const char* extension)
{
	return SanitizeTitle(title) + "-" + id.substr(0, 6) + extension;
}


static std::tm
UtcNow(int* millis)
{
	auto now = std::chrono::system_clock::now();
	std::time_t seconds = std::chrono::system_clock::to_time_t(now);
	if (millis != nullptr) {
		*millis = int(std::chrono::duration_cast<std::chrono::milliseconds>(
			now.time_since_epoch()).count() % 1000);
	}
	std::tm utc;
	gmtime_r(&seconds, &utc);
	return utc;
}


std::string
NowTimestamp()
{
	std::tm utc = UtcNow(nullptr);
	char buffer[32];
	strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
	return buffer;
}


std::string
NowTimestampMillis()
{
	int millis;
	std::tm utc = UtcNow(&millis);
	char buffer[40];
	strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &utc);
	char out[48];
	snprintf(out, sizeof(out), "%s.%03dZ", buffer, millis);
	return out;
}


std::string
CurrentMonth()
{
	std::tm utc = UtcNow(nullptr);
	char buffer[16];
	strftime(buffer, sizeof(buffer), "%Y-%m", &utc);
	return buffer;
}


std::string
FoldCase(const std::string& text)
{
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size(); i++) {
		unsigned char c = text[i];
		if (c >= 'A' && c <= 'Z') {
			out += char(c - 'A' + 'a');
			continue;
		}
		// Full-width Latin capitals U+FF21..U+FF3A (EF BC A1..BA) map to
		// U+FF41..U+FF5A (EF BD 81..9A).
		if (c == 0xef && i + 2 < text.size()
			&& (unsigned char)text[i + 1] == 0xbc
			&& (unsigned char)text[i + 2] >= 0xa1
			&& (unsigned char)text[i + 2] <= 0xba) {
			out += char(0xef);
			out += char(0xbd);
			out += char((unsigned char)text[i + 2] - 0xa1 + 0x81);
			i += 2;
			continue;
		}
		out += char(c);
	}
	return out;
}


}	// namespace bdb
