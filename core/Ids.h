/*
 * BDB card database - core
 * IDs (spec v0.4, 5.3) and part IDs (chapter 7).
 */
#ifndef BDB_IDS_H
#define BDB_IDS_H

#include <set>
#include <string>


namespace bdb {


// 128 bit random value as 26 lower case base32 characters.
std::string NewId();
bool IsValidId(const std::string& id);

// "<prefix>_" followed by 6 random base32 characters (chapter 7): 'f' for
// value parts and blank, 'l' label, 'b' button, 'c' counter. Retries until
// the result is not in `existing`.
std::string NewPartId(char prefix, const std::set<std::string>& existing);
bool IsValidPartId(const std::string& id);

// Media extension: ^[a-z0-9]{1,8}$ (chapter 9).
bool IsValidExtension(const std::string& ext);
// Media kind: image, audio or video (chapter 9).
bool IsValidMediaKind(const std::string& kind);

// Fills the buffer from the OS random source (getentropy()).
void SecureRandom(void* buffer, size_t length);


}	// namespace bdb

#endif	// BDB_IDS_H
