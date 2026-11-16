/*
 * BDB card database - core
 */
#include "Link.h"

#include "Limits.h"
#include "Model.h"
#include "Text.h"


namespace bdb {


const char*
LinkCheckCode(LinkCheck check)
{
	switch (check) {
		case LinkCheck::kOk:		return "ok";
		case LinkCheck::kNone:		return "none";
		case LinkCheck::kEmpty:		return "empty";
		case LinkCheck::kScheme:	return "scheme";
		case LinkCheck::kTooLong:	return "too_long";
		case LinkCheck::kInvalid:	return "invalid";
		case LinkCheck::kNoHost:	return "no_host";
		case LinkCheck::kField:		return "field";
	}
	return "invalid";
}


LinkCheck
CheckUrl(const std::string& url, std::string* outScheme)
{
	if (outScheme != nullptr)
		outScheme->clear();
	if (url.empty())
		return LinkCheck::kEmpty;
	if (CountCodePoints(url) > kMaxUrlLength)
		return LinkCheck::kTooLong;
	for (unsigned char c : url) {
		// Spaces and control characters (2.2). Non-ASCII bytes are part
		// of IRIs and allowed.
		if (c <= 0x20 || c == 0x7f)
			return LinkCheck::kInvalid;
	}
	size_t colon = url.find(':');
	if (colon == std::string::npos || colon == 0)
		return LinkCheck::kInvalid;
	std::string scheme;
	for (size_t i = 0; i < colon; i++) {
		char c = url[i];
		if (c >= 'A' && c <= 'Z')
			c = char(c - 'A' + 'a');
		bool valid = (c >= 'a' && c <= 'z')
			|| (i > 0 && ((c >= '0' && c <= '9') || c == '+' || c == '-'
				|| c == '.'));
		if (!valid)
			return LinkCheck::kInvalid;
		scheme += c;
	}
	if (outScheme != nullptr)
		*outScheme = scheme;
	if (scheme != "http" && scheme != "https" && scheme != "mailto")
		return LinkCheck::kScheme;
	std::string rest = url.substr(colon + 1);
	if (scheme == "mailto")
		return rest.empty() ? LinkCheck::kInvalid : LinkCheck::kOk;
	if (rest.compare(0, 2, "//") != 0)
		return LinkCheck::kNoHost;
	size_t hostEnd = rest.find_first_of("/?#", 2);
	std::string authority = rest.substr(2, hostEnd == std::string::npos
		? std::string::npos : hostEnd - 2);
	size_t at = authority.rfind('@');
	if (at != std::string::npos)
		authority = authority.substr(at + 1);
	std::string host = authority;
	if (!host.empty() && host[0] == '[') {
		size_t close = host.find(']');
		host = close == std::string::npos ? std::string()
			: host.substr(1, close - 1);
	} else {
		size_t port = host.find(':');
		if (port != std::string::npos)
			host = host.substr(0, port);
	}
	if (host.empty())
		return LinkCheck::kNoHost;
	return LinkCheck::kOk;
}


bool
HasLink(const Json& part)
{
	const Json* link = part.Find("link");
	return link != nullptr && link->IsObject();
}


LinkCheck
ResolveLink(const Json& part, const Template& tmpl, const Card* card,
	std::string& outUrl, std::string* outScheme)
{
	outUrl.clear();
	if (outScheme != nullptr)
		outScheme->clear();
	const Json* link = part.Find("link");
	if (link == nullptr || !link->IsObject())
		return LinkCheck::kNone;
	if (const Json* url = link->Find("url")) {
		if (!url->IsString())
			return LinkCheck::kInvalid;
		outUrl = url->AsString();
	} else if (const Json* field = link->Find("field")) {
		if (!field->IsString())
			return LinkCheck::kField;
		const Json* target = tmpl.FindPart(field->AsString());
		if (target == nullptr || part::Kind(*target) != kKindText)
			return LinkCheck::kField;
		if (card == nullptr)
			return LinkCheck::kEmpty;
		outUrl = TrimSpaces(EffectiveValue(*card, *target).AsString());
	} else
		return LinkCheck::kInvalid;
	return CheckUrl(outUrl, outScheme);
}


}	// namespace bdb
