/*
 * BDB card database - core
 * Links of image, label and button parts (I/O spec v0.1, chapter 2).
 */
#ifndef BDB_LINK_H
#define BDB_LINK_H

#include <string>

#include "Json.h"


namespace bdb {


class Card;
class Template;


enum class LinkCheck {
	kOk = 0,
	kNone,			// the part has no link
	kEmpty,			// the URL (or the field's value) is empty
	kScheme,		// not http, https or mailto
	kTooLong,		// more than 2048 characters
	kInvalid,		// spaces, control characters, malformed
	kNoHost,		// http(s) without a host
	kField			// field missing or not a text part
};

// Short code for the log ("reason" of link.blocked).
const char* LinkCheckCode(LinkCheck check);

// I/O spec 2.2. `outScheme` receives the lower case scheme (also when the
// scheme is not allowed), for the log.
LinkCheck CheckUrl(const std::string& url, std::string* outScheme = nullptr);

// The URL a part links to on a card: the fixed URL, or the effective
// value of the text part named by "field" (2.1). `card` may be nullptr
// (edit mode: field links cannot be resolved, kEmpty).
LinkCheck ResolveLink(const Json& part, const Template& tmpl,
	const Card* card, std::string& outUrl, std::string* outScheme = nullptr);

bool HasLink(const Json& part);


}	// namespace bdb

#endif	// BDB_LINK_H
