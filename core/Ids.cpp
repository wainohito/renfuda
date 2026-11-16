/*
 * BDB card database - core
 */
#include "Ids.h"

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>


namespace bdb {


static const char kBase32[] = "abcdefghijklmnopqrstuvwxyz234567";


void
SecureRandom(void* buffer, size_t length)
{
	uint8_t* bytes = static_cast<uint8_t*>(buffer);
	while (length > 0) {
		size_t chunk = length > 256 ? 256 : length;
		if (getentropy(bytes, chunk) != 0) {
			// There is no safe fallback; IDs must not be predictable.
			fprintf(stderr, "bdb: getentropy() failed\n");
			abort();
		}
		bytes += chunk;
		length -= chunk;
	}
}


static std::string
EncodeBase32(const uint8_t* bytes, size_t byteCount, size_t chars)
{
	std::string out;
	out.reserve(chars);
	uint32_t buffer = 0;
	int bits = 0;
	size_t index = 0;
	while (out.size() < chars) {
		if (bits < 5) {
			buffer = (buffer << 8)
				| (index < byteCount ? bytes[index] : 0);
			index++;
			bits += 8;
		}
		out += kBase32[(buffer >> (bits - 5)) & 0x1f];
		bits -= 5;
	}
	return out;
}


static bool
IsBase32Char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= '2' && c <= '7');
}


std::string
NewId()
{
	uint8_t bytes[16];
	SecureRandom(bytes, sizeof(bytes));
	return EncodeBase32(bytes, sizeof(bytes), 26);
}


bool
IsValidId(const std::string& id)
{
	if (id.size() != 26)
		return false;
	for (char c : id) {
		if (!IsBase32Char(c))
			return false;
	}
	return true;
}


std::string
NewPartId(char prefix, const std::set<std::string>& existing)
{
	while (true) {
		uint8_t bytes[4];
		SecureRandom(bytes, sizeof(bytes));
		std::string id = std::string(1, prefix) + "_"
			+ EncodeBase32(bytes, sizeof(bytes), 6);
		if (existing.find(id) == existing.end())
			return id;
	}
}


bool
IsValidPartId(const std::string& id)
{
	if (id.empty() || id.size() > 32)
		return false;
	if (id[0] < 'a' || id[0] > 'z')
		return false;
	for (char c : id) {
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
			return false;
	}
	return true;
}


bool
IsValidExtension(const std::string& ext)
{
	if (ext.empty() || ext.size() > 8)
		return false;
	for (char c : ext) {
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
			return false;
	}
	return true;
}


bool
IsValidMediaKind(const std::string& kind)
{
	return kind == "image" || kind == "audio" || kind == "video";
}


}	// namespace bdb
