/*
 * BDB card database - core
 */
#include "Model.h"

#include <cmath>
#include <limits>

#include "Ids.h"
#include "Limits.h"
#include "Text.h"


namespace bdb {


const char* const kKindText = "text";
const char* const kKindNumber = "number";
const char* const kKindImage = "image";
const char* const kKindLabel = "label";
const char* const kKindBlank = "blank";
const char* const kKindBool = "bool";
const char* const kKindButton = "button";
const char* const kKindCounter = "counter";


bool
KindHasValue(const std::string& kind)
{
	return kind == kKindText || kind == kKindNumber || kind == kKindImage
		|| kind == kKindBool;
}


bool
IsKnownKind(const std::string& kind)
{
	return KindHasValue(kind) || kind == kKindLabel || kind == kKindBlank
		|| kind == kKindButton || kind == kKindCounter;
}


bool
IsAssignableKind(const std::string& kind)
{
	return KindHasValue(kind) || kind == kKindBlank;
}


bool
KindHasText(const std::string& kind)
{
	return KindHasValue(kind) || kind == kKindLabel || kind == kKindButton
		|| kind == kKindCounter;
}


char
PartIdPrefix(const std::string& kind)
{
	if (kind == kKindLabel)
		return 'l';
	if (kind == kKindButton)
		return 'b';
	if (kind == kKindCounter)
		return 'c';
	return 'f';
}


bool
IsKnownAction(const std::string& action)
{
	static const char* const kActions[] = {
		"card.first", "card.prev", "card.next", "card.last", "card.new",
		"card.duplicate", "card.delete", "list.open", "link.open",
		"find.start", "find.run", "find.clear",
		// Prototype extensions (README).
		"card.pdf", "card.image", "sound.play", "video.play"
	};
	for (const char* known : kActions) {
		if (action == known)
			return true;
	}
	return false;
}


static int
HexDigit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}


bool
ParseColor(const std::string& color, uint8_t& r, uint8_t& g, uint8_t& b)
{
	if (color.size() != 7 || color[0] != '#')
		return false;
	int digits[6];
	for (int i = 0; i < 6; i++) {
		digits[i] = HexDigit(color[i + 1]);
		if (digits[i] < 0)
			return false;
	}
	r = uint8_t(digits[0] * 16 + digits[1]);
	g = uint8_t(digits[2] * 16 + digits[3]);
	b = uint8_t(digits[4] * 16 + digits[5]);
	return true;
}


bool
IsValidColor(const std::string& color)
{
	uint8_t r, g, b;
	return ParseColor(color, r, g, b);
}


// #pragma mark - paper


static const PaperInfo kPapers[] = {
	{ "A4", 210, 297 },
	{ "A5", 148, 210 },
	{ "A6", 105, 148 },
	{ "B4", 257, 364 },
	{ "B5", 182, 257 },
	{ "B6", 128, 182 },
	{ "postcard", 100, 148 }
};


const PaperInfo*
PaperSizes(size_t* count)
{
	*count = sizeof(kPapers) / sizeof(kPapers[0]);
	return kPapers;
}


const PaperInfo*
FindPaper(const std::string& size)
{
	for (const PaperInfo& paper : kPapers) {
		if (size == paper.size)
			return &paper;
	}
	return nullptr;
}


int32_t
MmToPixels(double mm)
{
	return int32_t(std::floor(mm * 96.0 / 25.4 + 0.5));
}


bool
PaperPixels(const std::string& size, const std::string& orientation,
	int32_t& width, int32_t& height)
{
	const PaperInfo* paper = FindPaper(size);
	if (paper == nullptr)
		return false;
	width = MmToPixels(paper->widthMm);
	height = MmToPixels(paper->heightMm);
	if (orientation == "landscape")
		std::swap(width, height);
	return true;
}


double
PixelsToPoints(double pixels)
{
	return pixels * 72.0 / 96.0;
}


static bool
IsInt32(const Json& value)
{
	return value.IsInt()
		&& value.AsInt() >= std::numeric_limits<int32_t>::min()
		&& value.AsInt() <= std::numeric_limits<int32_t>::max();
}


static Status
Invalid(const std::string& what)
{
	return Status(ErrorKind::kInvalidFormat, what);
}


// Checks "bdb" and "format_version" (5.2, 11.3).
static Status
ValidateHeader(const Json& doc, const char* type)
{
	if (!doc.IsObject())
		return Invalid("not an object");
	const Json* bdb = doc.Find("bdb");
	if (bdb == nullptr || !bdb->IsString() || bdb->AsString() != type)
		return Invalid(std::string("bdb is not \"") + type + "\"");
	const Json* version = doc.Find("format_version");
	if (version == nullptr || !version->IsInt() || version->AsInt() < 1)
		return Invalid("format_version");
	if (version->AsInt() > kFormatVersion)
		return Status(ErrorKind::kNewerVersion, "format_version");
	return Status::Ok();
}


static Json
MakeHeader(const char* type)
{
	Json doc = Json::MakeObject();
	doc.Set("bdb", type);
	doc.Set("format_version", kFormatVersion);
	return doc;
}


// #pragma mark - parts


namespace part {


std::string
Id(const Json& part)
{
	return part.GetString("id");
}


std::string
Kind(const Json& part)
{
	return part.GetString("kind");
}


std::string
Name(const Json& part)
{
	return part.GetString("name");
}


Rect
GetRect(const Json& part)
{
	Rect rect;
	const Json* value = part.Find("rect");
	if (value != nullptr && value->IsArray() && value->Array().size() == 4) {
		const JsonArray& array = value->Array();
		rect.x = int32_t(array[0].AsInt());
		rect.y = int32_t(array[1].AsInt());
		rect.w = int32_t(array[2].AsInt(1));
		rect.h = int32_t(array[3].AsInt(1));
	}
	return rect;
}


void
SetRect(Json& part, const Rect& rect)
{
	JsonArray array;
	array.push_back(Json(int64_t(rect.x)));
	array.push_back(Json(int64_t(rect.y)));
	array.push_back(Json(int64_t(rect.w < 1 ? 1 : rect.w)));
	array.push_back(Json(int64_t(rect.h < 1 ? 1 : rect.h)));
	part.Set("rect", Json(std::move(array)));
}


bool
IsLocked(const Json& part)
{
	const Json* locked = part.Find("locked");
	return locked != nullptr && locked->IsBool() && locked->AsBool();
}


void
SetLocked(Json& part, bool locked)
{
	if (locked)
		part.Set("locked", true);
	else
		part.Remove("locked");
}


const Json*
Style(const Json& part)
{
	const Json* style = part.Find("style");
	return style != nullptr && style->IsObject() ? style : nullptr;
}


Json&
MutableStyle(Json& part)
{
	Json* style = part.Find("style");
	if (style != nullptr && style->IsObject())
		return *style;
	Json object = Json::MakeObject();
	if (style != nullptr && style->IsString())
		object.Set("control", style->AsString());
	part.Set("style", std::move(object));
	return *part.Find("style");
}


void
ClearStyle(Json& part)
{
	const Json* style = part.Find("style");
	if (style == nullptr)
		return;
	if (style->IsObject()) {
		std::string control = style->GetString("control");
		if (Kind(part) == kKindBool && !control.empty())
			part.Set("style", control);
		else
			part.Remove("style");
	}
}


std::string
BoolLook(const Json& part)
{
	const Json* style = part.Find("style");
	std::string look;
	if (style != nullptr && style->IsString())
		look = style->AsString();
	else if (style != nullptr && style->IsObject())
		look = style->GetString("control");
	return look == "checkbox" ? "checkbox" : "switch";
}


void
SetBoolLook(Json& part, const std::string& look)
{
	Json* style = part.Find("style");
	if (style != nullptr && style->IsObject())
		style->Set("control", look);
	else
		part.Set("style", look);
}


std::string
SwitchEffect(const Json& part)
{
	if (Kind(part) != kKindBool)
		return "";
	const Json* effect = part.Find("effect");
	if (effect == nullptr || !effect->IsObject())
		return "";
	std::string type = effect->GetString("type");
	if (type == "lock_card" || type == "lock" || type == "hide")
		return type;
	return "";
}


std::vector<std::string>
SwitchTargets(const Json& part)
{
	std::vector<std::string> targets;
	const Json* effect = part.Find("effect");
	const Json* list = effect != nullptr && effect->IsObject()
		? effect->Find("targets") : nullptr;
	if (list == nullptr || !list->IsArray())
		return targets;
	for (const Json& item : list->Array()) {
		if (item.IsString())
			targets.push_back(item.AsString());
	}
	return targets;
}


void
SetSwitchEffect(Json& part, const std::string& type,
	const std::vector<std::string>& targets)
{
	if (type.empty()) {
		part.Remove("effect");
		return;
	}
	Json effect = Json::MakeObject();
	effect.Set("type", type);
	if (type != "lock_card") {
		Json list = Json::MakeArray();
		for (const std::string& id : targets)
			list.Array().push_back(Json(id));
		effect.Set("targets", std::move(list));
	}
	part.Set("effect", std::move(effect));
}


const Json*
ButtonMedia(const Json& part)
{
	if (Kind(part) != kKindButton)
		return nullptr;
	const Json* media = part.Find("media");
	return media != nullptr && IsMediaRef(*media) ? media : nullptr;
}


static void
AddKindKeys(Json& part, const std::string& kind)
{
	if (kind == kKindText) {
		part.Set("default", "");
		part.Set("multiline", false);
	} else if (kind == kKindNumber) {
		part.Set("default", Json());
		part.Set("decimals", 0);
	} else if (kind == kKindImage) {
		part.Set("default", Json());
		part.Set("fit", "contain");
	} else if (kind == kKindBool) {
		part.Set("default", false);
		// The visual style object, when present, keeps its place.
		if (Style(part) == nullptr)
			part.Set("style", "switch");
		else
			MutableStyle(part).Set("control", "switch");
	} else if (kind == kKindLabel) {
		part.Set("text", "");
	} else if (kind == kKindButton) {
		part.Set("text", "");
	}
}


Json
Make(const std::string& kind, const std::string& id, const std::string& name,
	const Rect& rect)
{
	Json part = Json::MakeObject();
	part.Set("id", id);
	if (KindHasValue(kind) || kind == kKindBlank)
		part.Set("name", name);
	part.Set("kind", kind);
	SetRect(part, rect);
	AddKindKeys(part, kind);
	return part;
}


void
ChangeKind(Json& part, const std::string& newKind)
{
	static const char* const kKindKeys[] = {
		"default", "multiline", "decimals", "fit", "text", "action"
	};
	for (const char* key : kKindKeys)
		part.Remove(key);
	// A bool look stored as the "style" string is a kind key; the visual
	// style object stays (only its "control" entry is bool specific).
	if (Json* style = part.Find("style")) {
		if (style->IsString())
			part.Remove("style");
		else if (style->IsObject()) {
			style->Remove("control");
			if (style->Object().empty())
				part.Remove("style");
		}
	}
	// Keys that only some kinds may have (chapter 7).
	if (newKind != kKindImage)
		part.Remove("source");
	if (newKind != kKindImage && newKind != kKindLabel
		&& newKind != kKindButton) {
		part.Remove("link");
	}
	if (newKind != kKindLabel && newKind != kKindButton)
		part.Remove("icon");
	part.Set("kind", newKind);
	AddKindKeys(part, newKind);
}


}	// namespace part


// #pragma mark - values


bool
IsMediaRef(const Json& value)
{
	if (!value.IsObject())
		return false;
	const Json* media = value.Find("media");
	const Json* kind = value.Find("kind");
	const Json* ext = value.Find("ext");
	return media != nullptr && media->IsString() && IsValidId(media->AsString())
		&& kind != nullptr && kind->IsString()
		&& IsValidMediaKind(kind->AsString())
		&& ext != nullptr && ext->IsString()
		&& IsValidExtension(ext->AsString());
}


bool
IsValidNumber(double value)
{
	return std::isfinite(value) && std::fabs(value) <= kMaxNumberMagnitude;
}


Json
MakeNumberValue(double value)
{
	if (value == std::floor(value) && std::fabs(value) <= kMaxNumberMagnitude)
		return Json(int64_t(value));
	return Json(value);
}


bool
ValueMatchesKind(const Json& value, const std::string& kind)
{
	if (kind == kKindText)
		return value.IsString();
	if (kind == kKindNumber)
		return value.IsNull()
			|| (value.IsNumber() && IsValidNumber(value.AsDouble()));
	if (kind == kKindImage) {
		return value.IsNull()
			|| (IsMediaRef(value) && value.GetString("kind") == kKindImage);
	}
	if (kind == kKindBool)
		return value.IsNull() || value.IsBool();
	return false;
}


static const Json&
FallbackDefault(const std::string& kind)
{
	static const Json kEmptyText("");
	static const Json kNull;
	static const Json kFalse(false);
	if (kind == kKindText)
		return kEmptyText;
	if (kind == kKindBool)
		return kFalse;
	return kNull;
}


const Json&
EffectiveValue(const Card& card, const Json& part)
{
	std::string kind = part::Kind(part);
	const Json* value = card.Value(part::Id(part));
	if (value != nullptr && ValueMatchesKind(*value, kind))
		return *value;
	const Json* defaultValue = part.Find("default");
	if (defaultValue != nullptr && ValueMatchesKind(*defaultValue, kind))
		return *defaultValue;
	return FallbackDefault(kind);
}


std::vector<std::string>
OrphanKeys(const Card& card, const Template& tmpl)
{
	std::vector<std::string> orphans;
	for (const auto& entry : card.Values().Object()) {
		const Json* part = tmpl.FindPart(entry.first);
		if (part == nullptr) {
			orphans.push_back(entry.first);
			continue;
		}
		std::string kind = part::Kind(*part);
		if (!IsKnownKind(kind))
			continue;	// opaque value of an unknown kind
		if (!KindHasValue(kind) || !ValueMatchesKind(entry.second, kind))
			orphans.push_back(entry.first);
	}
	return orphans;
}


bool
HasMismatchedValue(const Card& card, const Json& part)
{
	const Json* value = card.Value(part::Id(part));
	return value != nullptr && !ValueMatchesKind(*value, part::Kind(part));
}


std::string
ValueText(const Json& value)
{
	if (value.IsString())
		return value.AsString();
	if (value.IsNumber())
		return FormatNumber(value.AsDouble());
	if (value.IsBool())
		return value.AsBool() ? "true" : "false";
	return "";
}


std::string
CardTitle(const Card& card, const Template* tmpl)
{
	if (tmpl == nullptr)
		return kUntitled;
	const Json* titlePart = tmpl->TitlePart();
	if (titlePart == nullptr)
		return kUntitled;
	std::string title = TrimSpaces(FirstLine(
		ValueText(EffectiveValue(card, *titlePart))));
	if (title.empty())
		return kUntitled;
	return title;
}


std::string
MediaRelPath(const Json& mediaRef)
{
	// Built only from validated kind, id and ext (9, 14).
	if (!IsMediaRef(mediaRef))
		return "";
	return "media/" + mediaRef.GetString("kind") + "/"
		+ mediaRef.GetString("media") + "." + mediaRef.GetString("ext");
}


Json
MakeMediaRef(const std::string& mediaId, const std::string& kind,
	const std::string& ext)
{
	Json ref = Json::MakeObject();
	ref.Set("media", mediaId);
	ref.Set("kind", kind);
	ref.Set("ext", ext);
	return ref;
}


bool
DiffersFromDefaults(const Card& card, const Template& tmpl)
{
	for (const Json& part : tmpl.Parts()) {
		std::string kind = part::Kind(part);
		if (!KindHasValue(kind))
			continue;
		const Json* value = card.Value(part::Id(part));
		if (value == nullptr)
			continue;
		const Json* defaultValue = part.Find("default");
		Json fallback = kind == kKindText ? Json("") : Json();
		if (!(*value == (defaultValue != nullptr ? *defaultValue : fallback)))
			return true;
	}
	return false;
}


void
CollectMediaRefs(const Json& value, std::set<std::string>& outRelPaths)
{
	if (IsMediaRef(value)) {
		outRelPaths.insert(MediaRelPath(value));
		return;
	}
	if (value.IsObject()) {
		for (const auto& entry : value.Object())
			CollectMediaRefs(entry.second, outRelPaths);
	} else if (value.IsArray()) {
		for (const Json& item : value.Array())
			CollectMediaRefs(item, outRelPaths);
	}
}


// #pragma mark - Manifest


Status
Manifest::Validate(const Json& doc)
{
	Status status = ValidateHeader(doc, "manifest");
	if (!status)
		return status;
	if (!IsValidId(doc.GetString("stack_id")))
		return Invalid("stack_id");
	const Json* title = doc.Find("title");
	if (title == nullptr || !title->IsString())
		return Invalid("title");
	const Json* defaultTemplate = doc.Find("default_template");
	if (defaultTemplate != nullptr && !defaultTemplate->IsNull()
		&& !IsValidId(defaultTemplate->AsString())) {
		return Invalid("default_template");
	}
	const Json* encryption = doc.Find("encryption");
	if (encryption == nullptr)
		return Invalid("encryption");
	if (!encryption->IsNull())
		return Status(ErrorKind::kUnsupported, "encrypted stack");
	return Status::Ok();
}


Manifest
Manifest::Create(const std::string& title, const std::string& defaultTemplate)
{
	Json doc = MakeHeader("manifest");
	doc.Set("stack_id", NewId());
	doc.Set("title", title);
	std::string now = NowTimestamp();
	doc.Set("created", now);
	doc.Set("modified", now);
	if (!defaultTemplate.empty())
		doc.Set("default_template", defaultTemplate);
	doc.Set("encryption", Json());
	StoredDoc stored;
	stored.doc = std::move(doc);
	return Manifest(std::move(stored));
}


void
Manifest::SetDefaultTemplate(const std::string& id)
{
	if (id.empty())
		fStored.doc.Remove("default_template");
	else
		fStored.doc.Set("default_template", id);
}


// #pragma mark - Template


static bool
IsIntIn(const Json* value, int64_t low, int64_t high)
{
	return value != nullptr && value->IsInt() && value->AsInt() >= low
		&& value->AsInt() <= high;
}


static bool
IsColorValue(const Json* value, bool allowNull)
{
	if (value == nullptr)
		return true;
	if (value->IsNull())
		return allowNull;
	return value->IsString() && IsValidColor(value->AsString());
}


// border: { visible, width (0-16), color } - all optional.
static Status
ValidateBorder(const Json& border)
{
	if (!border.IsObject())
		return Invalid("border");
	const Json* visible = border.Find("visible");
	if (visible != nullptr && !visible->IsBool())
		return Invalid("border.visible");
	const Json* width = border.Find("width");
	if (width != nullptr && !IsIntIn(width, 0, 16))
		return Invalid("border.width");
	if (!IsColorValue(border.Find("color"), false))
		return Invalid("border.color");
	return Status::Ok();
}


// font: { family, style, size (6-96), color } - all optional.
static Status
ValidateFont(const Json& font)
{
	if (!font.IsObject())
		return Invalid("font");
	const Json* family = font.Find("family");
	if (family != nullptr && !family->IsString())
		return Invalid("font.family");
	const Json* style = font.Find("style");
	if (style != nullptr && !style->IsString())
		return Invalid("font.style");
	const Json* size = font.Find("size");
	if (size != nullptr && !IsIntIn(size, 6, 96))
		return Invalid("font.size");
	if (!IsColorValue(font.Find("color"), false))
		return Invalid("font.color");
	return Status::Ok();
}


// The visual style object (chapter 7).
static Status
ValidateStyle(const Json& style, const std::string& kind)
{
	if (const Json* border = style.Find("border")) {
		Status status = ValidateBorder(*border);
		if (!status)
			return status;
	}
	if (!IsColorValue(style.Find("fill"), true))
		return Invalid("style.fill");
	if (const Json* font = style.Find("font")) {
		Status status = ValidateFont(*font);
		if (!status)
			return status;
	}
	if (const Json* control = style.Find("control")) {
		if (kind != kKindBool || !control->IsString()
			|| (control->AsString() != "switch"
				&& control->AsString() != "checkbox")) {
			return Invalid("style.control");
		}
	}
	return Status::Ok();
}


namespace part {


Status
Validate(const Json& part, std::set<std::string>& ids)
{
	if (!part.IsObject())
		return Invalid("part is not an object");
	std::string id = part::Id(part);
	if (!IsValidPartId(id))
		return Invalid("part id");
	if (!ids.insert(id).second)
		return Invalid("duplicate part id");
	const Json* kind = part.Find("kind");
	if (kind == nullptr || !kind->IsString())
		return Invalid("part kind");
	const Json* rect = part.Find("rect");
	if (rect == nullptr || !rect->IsArray() || rect->Array().size() != 4)
		return Invalid("part rect");
	for (size_t i = 0; i < 4; i++) {
		const Json& value = rect->Array()[i];
		if (!IsInt32(value) || (i >= 2 && value.AsInt() < 1))
			return Invalid("part rect");
	}
	const Json* name = part.Find("name");
	if (name != nullptr) {
		if (!name->IsString())
			return Invalid("part name");
		if (CountCodePoints(name->AsString()) > kMaxPartNameChars)
			return Status(ErrorKind::kLimitExceeded, "part name");
	}

	const std::string& kindName = kind->AsString();
	// Unknown kinds are kept as they are (5.5).
	if (!IsKnownKind(kindName))
		return Status::Ok();

	// Common keys (chapter 7).
	const Json* locked = part.Find("locked");
	if (locked != nullptr && !locked->IsBool())
		return Invalid("locked");
	if (const Json* style = part.Find("style")) {
		if (style->IsObject()) {
			Status status = ValidateStyle(*style, kindName);
			if (!status)
				return status;
		} else if (!(kindName == kKindBool && style->IsString()
				&& (style->AsString() == "switch"
					|| style->AsString() == "checkbox"))) {
			return Invalid("style");
		}
	}
	if (const Json* icon = part.Find("icon")) {
		if (kindName != kKindButton && kindName != kKindLabel)
			return Invalid("icon");
		if (!icon->IsNull()) {
			if (!IsMediaRef(*icon) || icon->GetString("kind") != kKindImage)
				return Invalid("icon");
			const Json* size = icon->Find("size");
			if (size != nullptr && !IsIntIn(size, 12, 128))
				return Invalid("icon.size");
			const Json* placement = icon->Find("placement");
			if (placement != nullptr && (!placement->IsString()
					|| (placement->AsString() != "left"
						&& placement->AsString() != "top"))) {
				return Invalid("icon.placement");
			}
		}
	}
	if (const Json* link = part.Find("link")) {
		if (kindName != kKindImage && kindName != kKindLabel
			&& kindName != kKindButton) {
			return Invalid("link");
		}
		// Only the shape is checked here; the URL rules (scheme, length,
		// field kind) decide whether the link works (I/O spec 2.2).
		if (!link->IsNull()) {
			if (!link->IsObject())
				return Invalid("link");
			const Json* url = link->Find("url");
			const Json* field = link->Find("field");
			if ((url == nullptr) == (field == nullptr))
				return Invalid("link");
			if (url != nullptr && !url->IsString())
				return Invalid("link.url");
			if (field != nullptr && !field->IsString())
				return Invalid("link.field");
		}
	}
	if (const Json* source = part.Find("source")) {
		if (kindName != kKindImage)
			return Invalid("source");
		if (!source->IsNull()) {
			if (!source->IsObject())
				return Invalid("source");
			const Json* field = source->Find("field");
			if (field == nullptr || !field->IsString())
				return Invalid("source.field");
			const Json* maxEdge = source->Find("max_edge");
			if (maxEdge != nullptr && !IsIntIn(maxEdge, 32, 2048))
				return Invalid("source.max_edge");
		}
	}

	if (const Json* effect = part.Find("effect")) {
		if (kindName != kKindBool)
			return Invalid("effect");
		if (!effect->IsNull()) {
			if (!effect->IsObject())
				return Invalid("effect");
			std::string type = effect->GetString("type");
			if (type != "lock_card" && type != "lock" && type != "hide")
				return Invalid("effect.type");
			const Json* targets = effect->Find("targets");
			if (targets != nullptr) {
				if (!targets->IsArray())
					return Invalid("effect.targets");
				for (const Json& item : targets->Array()) {
					if (!item.IsString())
						return Invalid("effect.targets");
				}
			}
		}
	}
	if (const Json* media = part.Find("media")) {
		if (kindName != kKindButton)
			return Invalid("media");
		if (!media->IsNull() && (!IsMediaRef(*media)
				|| (media->GetString("kind") != "audio"
					&& media->GetString("kind") != "video"))) {
			return Invalid("media");
		}
	}

	const Json* defaultValue = part.Find("default");
	if (KindHasValue(kindName) && defaultValue != nullptr
		&& !ValueMatchesKind(*defaultValue, kindName)) {
		return Invalid("part default");
	}
	if (kindName == kKindText) {
		const Json* multiline = part.Find("multiline");
		if (multiline != nullptr && !multiline->IsBool())
			return Invalid("multiline");
	} else if (kindName == kKindNumber) {
		const Json* decimals = part.Find("decimals");
		if (decimals != nullptr && !IsIntIn(decimals, 0, kMaxDecimals))
			return Invalid("decimals");
	} else if (kindName == kKindImage) {
		const Json* fit = part.Find("fit");
		if (fit != nullptr && fit->AsString() != "contain"
			&& fit->AsString() != "cover" && fit->AsString() != "stretch") {
			return Invalid("fit");
		}
	} else if (kindName == kKindLabel || kindName == kKindButton) {
		const Json* text = part.Find("text");
		if (text != nullptr && !text->IsString())
			return Invalid("text");
		if (kindName == kKindButton) {
			// Unknown actions are kept (chapter 7).
			const Json* action = part.Find("action");
			if (action != nullptr && !action->IsString()
				&& !action->IsNull()) {
				return Invalid("action");
			}
		}
	}
	return Status::Ok();
}


}	// namespace part


static Status
ValidateBackground(const Json& background)
{
	if (!background.IsObject())
		return Invalid("background");
	if (!IsColorValue(background.Find("color"), true))
		return Invalid("background.color");
	const Json* image = background.Find("image");
	if (image != nullptr && !image->IsNull()
		&& (!IsMediaRef(*image) || image->GetString("kind") != kKindImage)) {
		return Invalid("background.image");
	}
	const Json* fit = background.Find("fit");
	if (fit != nullptr) {
		std::string value = fit->IsString() ? fit->AsString() : "";
		if (value != "contain" && value != "cover" && value != "stretch"
			&& value != "tile") {
			return Invalid("background.fit");
		}
	}
	return Status::Ok();
}


static Status
ValidatePaper(const Json& paper)
{
	if (!paper.IsObject())
		return Invalid("paper");
	const Json* size = paper.Find("size");
	if (size == nullptr || !size->IsString()
		|| (size->AsString() != "custom" && FindPaper(size->AsString())
			== nullptr)) {
		return Invalid("paper.size");
	}
	const Json* orientation = paper.Find("orientation");
	if (orientation != nullptr && (!orientation->IsString()
			|| (orientation->AsString() != "portrait"
				&& orientation->AsString() != "landscape"))) {
		return Invalid("paper.orientation");
	}
	return Status::Ok();
}


bool
IsValidThemeId(const std::string& id)
{
	if (id.empty() || id.size() > 32 || id[0] < 'a' || id[0] > 'z')
		return false;
	for (char c : id) {
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'
				|| c == '-')) {
			return false;
		}
	}
	return true;
}


Status
Template::Validate(const Json& doc)
{
	Status status = ValidateHeader(doc, "template");
	if (!status)
		return status;
	if (!IsValidId(doc.GetString("template_id")))
		return Invalid("template_id");
	const Json* title = doc.Find("title");
	if (title == nullptr || !title->IsString())
		return Invalid("title");
	const Json* rev = doc.Find("rev");
	if (rev == nullptr || !rev->IsInt() || rev->AsInt() < 1)
		return Invalid("rev");
	const Json* size = doc.Find("size");
	if (size == nullptr || !size->IsObject())
		return Invalid("size");
	const Json* w = size->Find("w");
	const Json* h = size->Find("h");
	if (w == nullptr || h == nullptr || !w->IsInt() || !h->IsInt()
		|| w->AsInt() < kMinCardSize || w->AsInt() > kMaxCardSize
		|| h->AsInt() < kMinCardSize || h->AsInt() > kMaxCardSize) {
		return Invalid("size");
	}
	const Json* titlePart = doc.Find("title_part");
	if (titlePart != nullptr && !titlePart->IsString() && !titlePart->IsNull())
		return Invalid("title_part");
	if (const Json* background = doc.Find("background")) {
		status = ValidateBackground(*background);
		if (!status)
			return status;
	}
	if (const Json* paper = doc.Find("paper")) {
		status = ValidatePaper(*paper);
		if (!status)
			return status;
	}
	const Json* theme = doc.Find("theme");
	if (theme != nullptr && (!theme->IsString()
			|| !IsValidThemeId(theme->AsString()))) {
		return Invalid("theme");
	}
	const Json* parts = doc.Find("parts");
	if (parts == nullptr || !parts->IsArray())
		return Invalid("parts");
	if (parts->Array().size() > kMaxParts)
		return Status(ErrorKind::kLimitExceeded, "parts");
	std::set<std::string> ids;
	for (const Json& part : parts->Array()) {
		status = part::Validate(part, ids);
		if (!status)
			return status;
	}
	return Status::Ok();
}


Template
Template::Create(const std::string& title, int32_t width, int32_t height)
{
	Json doc = MakeHeader("template");
	doc.Set("template_id", NewId());
	doc.Set("title", title);
	doc.Set("rev", 1);
	Json size = Json::MakeObject();
	size.Set("w", int64_t(width));
	size.Set("h", int64_t(height));
	doc.Set("size", std::move(size));
	doc.Set("parts", Json::MakeArray());
	StoredDoc stored;
	stored.doc = std::move(doc);
	return Template(std::move(stored));
}


int32_t
Template::Width() const
{
	const Json* size = Doc().Find("size");
	return size != nullptr ? int32_t(size->GetInt("w", kDefaultCardWidth))
		: kDefaultCardWidth;
}


int32_t
Template::Height() const
{
	const Json* size = Doc().Find("size");
	return size != nullptr ? int32_t(size->GetInt("h", kDefaultCardHeight))
		: kDefaultCardHeight;
}


void
Template::SetSize(int32_t width, int32_t height)
{
	Json* size = fStored.doc.Find("size");
	if (size == nullptr || !size->IsObject()) {
		fStored.doc.Set("size", Json::MakeObject());
		size = fStored.doc.Find("size");
	}
	size->Set("w", int64_t(width));
	size->Set("h", int64_t(height));
}


std::string
Template::PaperSize() const
{
	const Json* paper = Doc().Find("paper");
	if (paper == nullptr || !paper->IsObject())
		return "custom";
	std::string size = paper->GetString("size", "custom");
	int32_t width, height;
	if (!PaperPixels(size, PaperOrientation(), width, height))
		return "custom";
	// A paper that does not match the card size counts as custom.
	if (width != Width() || height != Height())
		return "custom";
	return size;
}


std::string
Template::PaperOrientation() const
{
	// Without a paper (the screen size), the card's shape tells.
	std::string fallback = Width() > Height() ? "landscape" : "portrait";
	const Json* paper = Doc().Find("paper");
	std::string orientation = paper != nullptr && paper->IsObject()
		? paper->GetString("orientation", fallback) : fallback;
	return orientation == "landscape" ? "landscape" : "portrait";
}


void
Template::SetPaper(const std::string& size, const std::string& orientation,
	int32_t width, int32_t height)
{
	int32_t paperWidth, paperHeight;
	if (PaperPixels(size, orientation, paperWidth, paperHeight)) {
		width = paperWidth;
		height = paperHeight;
	}
	SetSize(width, height);
	bool known = FindPaper(size) != nullptr;
	// A custom size has no paper of its own; its shape tells the
	// orientation.
	bool landscape = known ? orientation == "landscape" : width > height;
	Json paper = Json::MakeObject();
	paper.Set("size", known ? size : "custom");
	paper.Set("orientation", landscape ? "landscape" : "portrait");
	fStored.doc.Set("paper", std::move(paper));
}


void
Template::ClearPaper()
{
	fStored.doc.Remove("paper");
}


std::string
Template::ThemeId() const
{
	std::string id = Doc().GetString("theme");
	return id.empty() ? "beos" : id;
}


void
Template::SetThemeId(const std::string& themeId)
{
	fStored.doc.Set("theme", themeId);
}


const Json*
Template::Background() const
{
	const Json* background = Doc().Find("background");
	return background != nullptr && background->IsObject() ? background
		: nullptr;
}


Json&
Template::MutableBackground()
{
	Json* background = fStored.doc.Find("background");
	if (background == nullptr || !background->IsObject()) {
		fStored.doc.Set("background", Json::MakeObject());
		background = fStored.doc.Find("background");
	}
	return *background;
}


void
Template::SetTitlePart(const std::string& partId)
{
	if (partId.empty())
		fStored.doc.Remove("title_part");
	else
		fStored.doc.Set("title_part", partId);
}


const Json*
Template::TitlePart() const
{
	std::string id = TitlePartSetting();
	if (!id.empty()) {
		const Json* part = FindPart(id);
		if (part != nullptr && KindHasValue(part::Kind(*part)))
			return part;
	}
	for (const Json& part : Parts()) {
		if (part::Kind(part) == kKindText)
			return &part;
	}
	return nullptr;
}


JsonArray&
Template::Parts()
{
	Json* parts = fStored.doc.Find("parts");
	if (parts == nullptr) {
		fStored.doc.Set("parts", Json::MakeArray());
		parts = fStored.doc.Find("parts");
	}
	return parts->Array();
}


const JsonArray&
Template::Parts() const
{
	const Json* parts = Doc().Find("parts");
	static const JsonArray kEmpty;
	return parts != nullptr ? parts->Array() : kEmpty;
}


const Json*
Template::FindPart(const std::string& id) const
{
	for (const Json& part : Parts()) {
		if (part::Id(part) == id)
			return &part;
	}
	return nullptr;
}


Json*
Template::FindPart(const std::string& id)
{
	for (Json& part : Parts()) {
		if (part::Id(part) == id)
			return &part;
	}
	return nullptr;
}


std::set<std::string>
Template::PartIds() const
{
	std::set<std::string> ids;
	for (const Json& part : Parts())
		ids.insert(part::Id(part));
	return ids;
}


std::string
Template::NewPartId(const std::string& kind) const
{
	return bdb::NewPartId(PartIdPrefix(kind), PartIds());
}


// #pragma mark - Card


Status
Card::Validate(const Json& doc)
{
	Status status = ValidateHeader(doc, "card");
	if (!status)
		return status;
	if (!IsValidId(doc.GetString("card_id")))
		return Invalid("card_id");
	if (!IsValidId(doc.GetString("template_id")))
		return Invalid("template_id");
	const Json* seq = doc.Find("seq");
	if (seq == nullptr || !seq->IsInt())
		return Invalid("seq");
	const Json* rev = doc.Find("rev");
	if (rev == nullptr || !rev->IsInt() || rev->AsInt() < 1)
		return Invalid("rev");
	const Json* values = doc.Find("values");
	if (values == nullptr || !values->IsObject())
		return Invalid("values");
	return Status::Ok();
}


Card
Card::Create(const std::string& templateId, int64_t seq)
{
	Json doc = MakeHeader("card");
	doc.Set("card_id", NewId());
	doc.Set("template_id", templateId);
	doc.Set("seq", seq);
	doc.Set("rev", 1);
	std::string now = NowTimestamp();
	doc.Set("created", now);
	doc.Set("modified", now);
	doc.Set("values", Json::MakeObject());
	StoredDoc stored;
	stored.doc = std::move(doc);
	return Card(std::move(stored));
}


const Json&
Card::Values() const
{
	static const Json kEmpty = Json::MakeObject();
	const Json* values = Doc().Find("values");
	return values != nullptr ? *values : kEmpty;
}


Json&
Card::Values()
{
	Json* values = fStored.doc.Find("values");
	if (values == nullptr || !values->IsObject()) {
		fStored.doc.Set("values", Json::MakeObject());
		values = fStored.doc.Find("values");
	}
	return *values;
}


const Json*
Card::Value(const std::string& partId) const
{
	return Values().Find(partId);
}


void
Card::SetValue(const std::string& partId, Json value)
{
	Values().Set(partId, std::move(value));
}


SwitchState
ComputeSwitchState(const Template& tmpl, const Card* card)
{
	SwitchState state;
	if (card == nullptr)
		return state;
	std::string cardLocker;
	for (const Json& part : tmpl.Parts()) {
		std::string type = part::SwitchEffect(part);
		if (type.empty())
			continue;
		const Json& value = EffectiveValue(*card, part);
		if (!value.IsBool() || !value.AsBool())
			continue;
		if (type == "lock_card") {
			state.cardLocked = true;
			if (cardLocker.empty())
				cardLocker = part::Id(part);
			continue;
		}
		for (const std::string& id : part::SwitchTargets(part)) {
			if (id == part::Id(part))
				continue;	// a switch never hides or locks itself
			(type == "hide" ? state.hidden : state.locked).insert(id);
		}
	}
	if (state.cardLocked) {
		// Everything but the switch that locks (so it can unlock).
		for (const Json& part : tmpl.Parts()) {
			std::string id = part::Id(part);
			if (id != cardLocker)
				state.locked.insert(id);
		}
	}
	return state;
}


}	// namespace bdb
