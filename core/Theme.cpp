/*
 * BDB card database - core
 */
#include "Theme.h"

#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <map>

#include "Limits.h"
#include "Model.h"
#include "Store.h"


namespace bdb {


#include "ThemeDefault.inc"


bool
ColorFromString(const std::string& text, Color& out)
{
	uint8_t r, g, b;
	if (!ParseColor(text, r, g, b))
		return false;
	out = Color::Make(r, g, b);
	return true;
}


std::string
ColorToString(const Color& color)
{
	char buffer[8];
	snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", color.r, color.g,
		color.b);
	return buffer;
}


std::string
ThemeRelPath(const std::string& themeId)
{
	return "themes/" + themeId + ".rfds";
}


// #pragma mark - Theme


Status
Theme::Parse(const std::string& data, Theme& out)
{
	Json doc;
	Status status = Json::Parse(data, doc, kMaxJsonDepth);
	if (!status)
		return status;
	if (!doc.IsObject() || doc.GetString("bdb") != "theme")
		return Status(ErrorKind::kInvalidFormat, "bdb is not \"theme\"");
	const Json* version = doc.Find("format_version");
	if (version == nullptr || !version->IsInt() || version->AsInt() < 1)
		return Status(ErrorKind::kInvalidFormat, "format_version");
	if (version->AsInt() > kFormatVersion)
		return Status(ErrorKind::kNewerVersion, "format_version");
	if (!IsValidThemeId(doc.GetString("theme_id")))
		return Status(ErrorKind::kInvalidFormat, "theme_id");
	out.fDoc = std::move(doc);
	return Status::Ok();
}


const char*
Theme::DefaultData()
{
	return kDefaultThemeData;
}


const Theme&
Theme::Default()
{
	static Theme theme;
	static bool initialized = false;
	if (!initialized) {
		Parse(kDefaultThemeData, theme);
		initialized = true;
	}
	return theme;
}


static std::string
LocalizedString(const Json& doc, const char* key, const std::string& lang)
{
	const Json* value = doc.Find(key);
	if (value == nullptr)
		return "";
	if (value->IsString())
		return value->AsString();
	if (!value->IsObject())
		return "";
	std::string text = value->GetString(lang);
	if (text.empty())
		text = value->GetString(lang == "ja" ? "en" : "ja");
	return text;
}


std::string
Theme::Name(const std::string& lang) const
{
	std::string name = LocalizedString(fDoc, "name", lang);
	return name.empty() ? Id() : name;
}


std::string
Theme::Description(const std::string& lang) const
{
	return LocalizedString(fDoc, "description", lang);
}


bool
Theme::Native() const
{
	const Json* native = fDoc.Find("native");
	return native != nullptr && native->IsBool() && native->AsBool();
}


Color
Theme::GetColor(const char* key) const
{
	Color color;
	const Json* colors = fDoc.Find("colors");
	if (colors != nullptr && ColorFromString(colors->GetString(key), color))
		return color;
	if (this != &Default())
		return Default().GetColor(key);
	return Color();
}


void
Theme::SetColor(const char* key, const Color& color)
{
	Json* colors = fDoc.Find("colors");
	if (colors == nullptr || !colors->IsObject()) {
		fDoc.Set("colors", Json::MakeObject());
		colors = fDoc.Find("colors");
	}
	colors->Set(key, ColorToString(color));
}


int
Theme::FocusWidth() const
{
	const Json* focus = fDoc.Find("focus");
	int64_t width = focus != nullptr ? focus->GetInt("width", -1) : -1;
	if (width < 0 || width > 16)
		return this != &Default() ? Default().FocusWidth() : 2;
	return int(width);
}


Color
Theme::FocusColor() const
{
	const Json* focus = fDoc.Find("focus");
	Color color;
	if (focus != nullptr && ColorFromString(focus->GetString("color"), color))
		return color;
	return GetColor("accent");
}


std::string
Theme::FontFamily() const
{
	const Json* font = fDoc.Find("font");
	return font != nullptr && font->IsObject() ? font->GetString("family")
		: std::string();
}


std::string
Theme::FontStyle() const
{
	const Json* font = fDoc.Find("font");
	std::string style = font != nullptr && font->IsObject()
		? font->GetString("style") : std::string();
	return style.empty() ? "Regular" : style;
}


int
Theme::FontSize() const
{
	const Json* font = fDoc.Find("font");
	int64_t size = font != nullptr && font->IsObject()
		? font->GetInt("size", 0) : 0;
	if (size < 6 || size > 96)
		return this != &Default() ? Default().FontSize() : 12;
	return int(size);
}


const Json*
Theme::_Kind(const std::string& kind) const
{
	const Json* kinds = fDoc.Find("kinds");
	if (kinds == nullptr || !kinds->IsObject())
		return nullptr;
	const Json* entry = kinds->Find(kind);
	return entry != nullptr && entry->IsObject() ? entry : nullptr;
}


static bool
IsKnownLook(const std::string& look)
{
	static const char* const kLooks[] = {
		"none", "flat", "raised", "sunken", "raised2", "sunken2", "etched",
		"round"
	};
	for (const char* known : kLooks) {
		if (look == known)
			return true;
	}
	return false;
}


// Applies a border object (theme kind or part style) on top of `look`.
static void
ApplyBorder(const Json& border, PartLook& look, bool own)
{
	if (!border.IsObject())
		return;
	const Json* visible = border.Find("visible");
	if (visible != nullptr && visible->IsBool()) {
		look.borderVisible = visible->AsBool();
		look.ownBorder = look.ownBorder || own;
	}
	const Json* width = border.Find("width");
	if (width != nullptr && width->IsInt() && width->AsInt() >= 0
		&& width->AsInt() <= 16) {
		look.borderWidth = int(width->AsInt());
		look.ownBorder = look.ownBorder || own;
	}
	Color color;
	if (ColorFromString(border.GetString("color"), color)) {
		look.borderColor = color;
		look.borderColorSet = look.borderColorSet || own;
		look.ownBorder = look.ownBorder || own;
	}
}


static void
ApplyFill(const Json* fill, PartLook& look, bool own)
{
	if (fill == nullptr)
		return;
	Color color;
	if (fill->IsNull()) {
		look.hasFill = false;
		look.ownFill = look.ownFill || own;
	} else if (fill->IsString() && ColorFromString(fill->AsString(), color)) {
		look.hasFill = true;
		look.fill = color;
		look.ownFill = look.ownFill || own;
	}
}


static void
ApplyFont(const Json& font, PartLook& look, bool own)
{
	if (!font.IsObject())
		return;
	if (const Json* family = font.Find("family")) {
		if (family->IsString()) {
			look.fontFamily = family->AsString();
			look.ownFont = look.ownFont || own;
		}
	}
	std::string style = font.GetString("style");
	if (!style.empty()) {
		look.fontStyle = style;
		look.ownFont = look.ownFont || own;
	}
	const Json* size = font.Find("size");
	if (size != nullptr && size->IsInt() && size->AsInt() >= 6
		&& size->AsInt() <= 96) {
		look.fontSize = int(size->AsInt());
		look.ownFont = look.ownFont || own;
	}
	Color color;
	if (ColorFromString(font.GetString("color"), color)) {
		look.textColor = color;
		look.ownFont = look.ownFont || own;
	}
}


PartLook
Theme::ResolveKind(const std::string& kind) const
{
	PartLook look;
	// 3. Theme colors and font (4. the default theme fills the gaps).
	look.look = "flat";
	look.borderVisible = true;
	look.borderWidth = 1;
	look.borderColor = GetColor("control_border");
	look.textColor = GetColor("text");
	look.fontFamily = FontFamily();
	look.fontStyle = FontStyle();
	look.fontSize = FontSize();

	// 2. The theme's entry for the kind; missing kinds are a thin flat
	// line (4.2). Unknown kinds use the blank entry's look.
	const Json* entry = _Kind(kind);
	if (entry == nullptr && this != &Default() && fDoc.Find("kinds") == nullptr)
		entry = Default()._Kind(kind);
	if (entry != nullptr) {
		std::string name = entry->GetString("look");
		if (!name.empty())
			look.look = IsKnownLook(name) ? name : "flat";
		std::string pressed = entry->GetString("pressed");
		if (!pressed.empty())
			look.pressedLook = IsKnownLook(pressed) ? pressed : "flat";
		if (const Json* border = entry->Find("border"))
			ApplyBorder(*border, look, false);
		ApplyFill(entry->Find("fill"), look, false);
		if (const Json* font = entry->Find("font"))
			ApplyFont(*font, look, false);
		const Json* radius = entry->Find("radius");
		if (radius != nullptr && radius->IsInt() && radius->AsInt() >= 0
			&& radius->AsInt() <= 64) {
			look.radius = int(radius->AsInt());
		}
	}
	if (Native()) {
		// The system decides the fills of native themes (4.2).
		if (kind == "text" || kind == "number" || kind == "counter") {
			look.hasFill = true;
			look.fill = GetColor("control_fill");
		} else if (kind == "button") {
			look.hasFill = true;
			look.fill = GetColor("panel");
		}
	}
	if (look.look == "none")
		look.borderVisible = false;
	return look;
}


PartLook
Theme::Resolve(const Json& part) const
{
	std::string kind = part::Kind(part);
	PartLook look = ResolveKind(kind);
	// 1. The part's own style.
	if (const Json* style = part::Style(part)) {
		if (const Json* border = style->Find("border")) {
			ApplyBorder(*border, look, true);
			// A part that turns the border on over a "none" look gets a
			// plain line.
			if (look.look == "none" && look.borderVisible)
				look.look = "flat";
		}
		ApplyFill(style->Find("fill"), look, true);
		if (const Json* font = style->Find("font"))
			ApplyFont(*font, look, true);
	}
	return look;
}


// #pragma mark - ThemeLibrary


static Status
ReadThemeFile(const std::string& path, Theme& out, std::string* data)
{
	std::string text;
	Status status = ReadWholeFile(path, kMaxThemeBytes, text);
	if (!status)
		return status;
	status = Theme::Parse(text, out);
	if (status && data != nullptr)
		*data = std::move(text);
	return status;
}


Status
ThemeLibrary::_FindOutside(const std::string& themeId, Theme& out,
	std::string* origin, std::string* data) const
{
	if (!IsValidThemeId(themeId))
		return Status(ErrorKind::kInvalidArgument, "theme_id");
	const std::string* dirs[] = { &fUserDir, &fBundledDir };
	const char* origins[] = { "user", "bundled" };
	for (int i = 0; i < 2; i++) {
		if (dirs[i]->empty())
			continue;
		Theme theme;
		if (ReadThemeFile(*dirs[i] + "/" + themeId + ".rfds", theme, data)
			&& theme.Id() == themeId) {
			out = theme;
			if (origin != nullptr)
				*origin = origins[i];
			return Status::Ok();
		}
	}
	if (themeId == "beos") {
		out = Theme::Default();
		if (origin != nullptr)
			*origin = "builtin";
		if (data != nullptr)
			*data = Theme::DefaultData();
		return Status::Ok();
	}
	return Status(ErrorKind::kNotFound, themeId);
}


Status
ThemeLibrary::Find(const std::string& themeId, StackStore* store, Theme& out,
	std::string* origin) const
{
	if (!IsValidThemeId(themeId))
		return Status(ErrorKind::kInvalidArgument, "theme_id");
	if (store != nullptr) {
		std::string text;
		Theme theme;
		if (store->ReadSmallFile(ThemeRelPath(themeId), kMaxThemeBytes, text)
			&& Theme::Parse(text, theme) && theme.Id() == themeId) {
			out = theme;
			if (origin != nullptr)
				*origin = "stack";
			return Status::Ok();
		}
	}
	return _FindOutside(themeId, out, origin, nullptr);
}


static void
ListThemeFolder(const std::string& dir, std::vector<std::string>& ids)
{
	if (dir.empty())
		return;
	DIR* handle = opendir(dir.c_str());
	if (handle == nullptr)
		return;
	while (struct dirent* entry = readdir(handle)) {
		std::string name = entry->d_name;
		if (name.size() > 5 && name[0] != '.'
			&& name.compare(name.size() - 5, 5, ".rfds") == 0) {
			ids.push_back(name.substr(0, name.size() - 5));
		}
	}
	closedir(handle);
}


std::vector<ThemeInfo>
ThemeLibrary::List(StackStore* store) const
{
	std::vector<std::string> ids;
	if (store != nullptr)
		ListThemeFolder(store->RootPath() + "/themes", ids);
	ListThemeFolder(fUserDir, ids);
	ListThemeFolder(fBundledDir, ids);
	ids.push_back("beos");

	std::vector<ThemeInfo> result;
	std::map<std::string, bool> seen;
	for (const std::string& id : ids) {
		if (!IsValidThemeId(id) || seen[id])
			continue;
		Theme theme;
		std::string origin;
		if (!Find(id, store, theme, &origin))
			continue;
		seen[id] = true;
		ThemeInfo info;
		info.id = id;
		info.nameJa = theme.Name("ja");
		info.nameEn = theme.Name("en");
		info.rev = theme.Rev();
		info.origin = origin;
		result.push_back(info);
	}
	// The bundled order of the spec first (6), then the rest by ID.
	static const char* const kOrder[] = {
		"beos", "haiku", "nextstep", "macos", "win95"
	};
	auto rank = [](const std::string& id) {
		for (int i = 0; i < 5; i++) {
			if (id == kOrder[i])
				return i;
		}
		return 5;
	};
	std::stable_sort(result.begin(), result.end(),
		[&rank](const ThemeInfo& a, const ThemeInfo& b) {
			int rankA = rank(a.id);
			int rankB = rank(b.id);
			if (rankA != rankB)
				return rankA < rankB;
			return a.id < b.id;
		});
	return result;
}


Status
ThemeLibrary::CopyToStack(const std::string& themeId, StackStore& store,
	bool overwrite) const
{
	// The source is the user's or the bundled theme, not the stack copy.
	Theme theme;
	std::string data;
	Status status = _FindOutside(themeId, theme, nullptr, &data);
	if (!status) {
		// Only a stack copy exists: nothing to copy.
		Theme existing;
		std::string origin;
		if (Find(themeId, &store, existing, &origin) && origin == "stack")
			return Status::Ok();
		return status;
	}
	std::string current;
	Theme existing;
	if (store.ReadSmallFile(ThemeRelPath(themeId), kMaxThemeBytes, current)
		&& Theme::Parse(current, existing)) {
		if (existing.Rev() == theme.Rev())
			return Status::Ok();
		if (!overwrite)
			return Status(ErrorKind::kConflict, themeId);
	}
	return store.WriteSmallFile(ThemeRelPath(themeId), data);
}


}	// namespace bdb
