/*
 * BDB card database - core
 * Manifest, template and card documents (spec v0.4, chapters 6-8).
 *
 * The documents are kept as Json objects so that unknown keys survive a
 * load/save round trip (5.5). The classes only add typed accessors.
 */
#ifndef BDB_MODEL_H
#define BDB_MODEL_H

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "Json.h"
#include "Status.h"


namespace bdb {


// Part kinds (chapter 7)
extern const char* const kKindText;
extern const char* const kKindNumber;
extern const char* const kKindImage;
extern const char* const kKindLabel;
extern const char* const kKindBlank;
extern const char* const kKindBool;
extern const char* const kKindButton;
extern const char* const kKindCounter;

// text, number, bool, image
bool KindHasValue(const std::string& kind);
// The eight implemented kinds (chapter 7).
bool IsKnownKind(const std::string& kind);
// Kinds a part can be changed between (20.1): blank, text, number, bool,
// image.
bool IsAssignableKind(const std::string& kind);
// Kinds that show text with a font (style.font, chapter 7).
bool KindHasText(const std::string& kind);
// Prefix of generated part IDs: f, l, b or c (chapter 7).
char PartIdPrefix(const std::string& kind);

// Button actions (chapter 7). Unknown actions are kept but do nothing.
bool IsKnownAction(const std::string& action);
// Theme IDs: ^[a-z][a-z0-9_-]{0,31}$ (theme spec 3.4).
bool IsValidThemeId(const std::string& id);

// "#rrggbb" (lower or upper case hex digits).
bool IsValidColor(const std::string& color);
// Parses "#rrggbb"; false when invalid.
bool ParseColor(const std::string& color, uint8_t& r, uint8_t& g,
	uint8_t& b);


struct Rect {
	int32_t x = 0;
	int32_t y = 0;
	int32_t w = 1;
	int32_t h = 1;
};


// #pragma mark - paper (chapter 7, "用紙サイズ")


struct PaperInfo {
	const char*	size;		// "A4", ..., "postcard"
	int32_t		widthMm;	// portrait
	int32_t		heightMm;
};

// The seven paper sizes (A4, A5, A6, B4, B5, B6, postcard).
const PaperInfo* PaperSizes(size_t* count);
const PaperInfo* FindPaper(const std::string& size);
// mm * 96 / 25.4, rounded.
int32_t MmToPixels(double mm);
// Card size in pixels for a paper size and orientation. False for
// "custom" and unknown sizes.
bool PaperPixels(const std::string& size, const std::string& orientation,
	int32_t& width, int32_t& height);
// Page size in PDF points (1/72 inch) for a size in 96 dpi pixels.
double PixelsToPoints(double pixels);


// Change stamp of a file, for the minimal conflict check (chapter 12).
struct FileStamp {
	int64_t	mtimeNs = 0;
	int64_t	size = -1;
	bool	valid = false;

	bool operator==(const FileStamp& other) const
		{ return valid == other.valid && mtimeNs == other.mtimeNs
			&& size == other.size; }
	bool operator!=(const FileStamp& other) const
		{ return !(*this == other); }
};


// A document as read from / written to the store.
struct StoredDoc {
	std::string	relPath;	// path relative to the stack folder
	FileStamp	stamp;
	Json		doc;
};


// #pragma mark - parts


namespace part {

std::string	Id(const Json& part);
std::string	Kind(const Json& part);
std::string	Name(const Json& part);
Rect		GetRect(const Json& part);
void		SetRect(Json& part, const Rect& rect);

bool		IsLocked(const Json& part);
void		SetLocked(Json& part, bool locked);

// The visual style object ("style", chapter 7), or nullptr. For bool
// parts a string "style" is the switch/checkbox choice (see BoolLook).
const Json*	Style(const Json& part);
// Returns the style object, creating it (and keeping a bool part's look).
Json&		MutableStyle(Json& part);
// Removes the visual style (keeps a bool part's look).
void		ClearStyle(Json& part);
// "switch" or "checkbox" for bool parts. The look is the string "style"
// (spec v0.4, chapter 7) or, when the part also has a visual style,
// "style": { "control": ... } (prototype extension, see the README).
std::string	BoolLook(const Json& part);
void		SetBoolLook(Json& part, const std::string& look);

// Creates a new part with the kind's default keys. Value kinds and blank
// get a name; label, button and counter do not.
Json		Make(const std::string& kind, const std::string& id,
				const std::string& name, const Rect& rect);
// Replaces the kind specific keys (20.1). id, name, rect and the common
// keys (style, locked, ...) stay.
void		ChangeKind(Json& part, const std::string& newKind);

// Validates one part (chapter 7). `ids` collects the IDs seen so far.
Status		Validate(const Json& part, std::set<std::string>& ids);

// Switch effects (prototype extension, see the README): while a bool part
// is on, it locks the card, locks some parts or hides some parts.
// "effect": { "type": "lock_card" | "lock" | "hide", "targets": [ids] }.
// Returns "" when the part has no (valid) effect.
std::string	SwitchEffect(const Json& part);
std::vector<std::string> SwitchTargets(const Json& part);
// type "" removes the effect.
void		SetSwitchEffect(Json& part, const std::string& type,
				const std::vector<std::string>& targets);

// The sound or video of a button (actions sound.play / video.play,
// prototype extension): "media" is a media reference, or nullptr.
const Json*	ButtonMedia(const Json& part);

}	// namespace part


// #pragma mark - documents


class Manifest {
public:
						Manifest() {}
	explicit			Manifest(StoredDoc stored)
							: fStored(std::move(stored)) {}

	static	Status		Validate(const Json& doc);
	static	Manifest	Create(const std::string& title,
							const std::string& defaultTemplate);

			std::string	StackId() const
							{ return Doc().GetString("stack_id"); }
			std::string	Title() const { return Doc().GetString("title"); }
			void		SetTitle(const std::string& title)
							{ fStored.doc.Set("title", title); }
			std::string	DefaultTemplate() const
							{ return Doc().GetString("default_template"); }
			void		SetDefaultTemplate(const std::string& id);

	const	Json&		Doc() const { return fStored.doc; }
			Json&		Doc() { return fStored.doc; }
			StoredDoc&	Stored() { return fStored; }
	const	StoredDoc&	Stored() const { return fStored; }

private:
			StoredDoc	fStored;
};


class Template {
public:
						Template() {}
	explicit			Template(StoredDoc stored)
							: fStored(std::move(stored)) {}

	static	Status		Validate(const Json& doc);
	static	Template	Create(const std::string& title, int32_t width,
							int32_t height);

			std::string	Id() const { return Doc().GetString("template_id"); }
			std::string	Title() const { return Doc().GetString("title"); }
			void		SetTitle(const std::string& title)
							{ fStored.doc.Set("title", title); }
			int64_t		Rev() const { return Doc().GetInt("rev", 0); }
			int32_t		Width() const;
			int32_t		Height() const;
			void		SetSize(int32_t width, int32_t height);

	// Paper (chapter 7). PaperSize() is "custom" when there is no paper,
	// it is "custom", or its size does not match `size` (then the stored
	// value is ignored).
			std::string	PaperSize() const;
			std::string	PaperOrientation() const;	// portrait, landscape
	// Sets paper and size together; "custom" keeps the given pixel size.
			void		SetPaper(const std::string& size,
							const std::string& orientation, int32_t width,
							int32_t height);
			void		ClearPaper();

	// Theme ID; "beos" when absent (chapter 7).
			std::string	ThemeId() const;
			void		SetThemeId(const std::string& themeId);

	// Background (chapter 7). nullptr when absent.
	const	Json*		Background() const;
			Json&		MutableBackground();

	// The configured title_part (may be empty or dangling).
			std::string	TitlePartSetting() const
							{ return Doc().GetString("title_part"); }
			void		SetTitlePart(const std::string& partId);
	// The part actually used for card titles (8.5): title_part when it
	// names a value part, else the first text part; nullptr when none.
	const	Json*		TitlePart() const;

			JsonArray&	Parts();
	const	JsonArray&	Parts() const;
	const	Json*		FindPart(const std::string& id) const;
			Json*		FindPart(const std::string& id);
			std::set<std::string> PartIds() const;
			std::string	NewPartId(const std::string& kind) const;

	const	Json&		Doc() const { return fStored.doc; }
			Json&		Doc() { return fStored.doc; }
			StoredDoc&	Stored() { return fStored; }
	const	StoredDoc&	Stored() const { return fStored; }

private:
			StoredDoc	fStored;
};


class Card {
public:
						Card() {}
	explicit			Card(StoredDoc stored)
							: fStored(std::move(stored)) {}

	static	Status		Validate(const Json& doc);
	static	Card		Create(const std::string& templateId, int64_t seq);

			std::string	Id() const { return Doc().GetString("card_id"); }
			std::string	TemplateId() const
							{ return Doc().GetString("template_id"); }
			void		SetTemplateId(const std::string& id)
							{ fStored.doc.Set("template_id", id); }
			int64_t		Seq() const { return Doc().GetInt("seq", 0); }
			void		SetSeq(int64_t seq) { fStored.doc.Set("seq", seq); }
			int64_t		Rev() const { return Doc().GetInt("rev", 0); }

	const	Json&		Values() const;
			Json&		Values();
	const	Json*		Value(const std::string& partId) const;
			void		SetValue(const std::string& partId, Json value);

	const	Json&		Doc() const { return fStored.doc; }
			Json&		Doc() { return fStored.doc; }
			StoredDoc&	Stored() { return fStored; }
	const	StoredDoc&	Stored() const { return fStored; }

private:
			StoredDoc	fStored;
};


// #pragma mark - values


bool IsMediaRef(const Json& value);
// Matches the value type of a value part kind (8.1).
bool ValueMatchesKind(const Json& value, const std::string& kind);
// A valid number part value: finite and |x| <= 1e15 (5.1).
bool IsValidNumber(double value);
Json MakeNumberValue(double value);

// 8.1: the card value when present and of the right type, else the
// part default. Never nullptr for value parts.
const Json& EffectiveValue(const Card& card, const Json& part);

// 8.2: keys of `values` that are orphans with respect to the template.
// Values of parts with an unknown kind are opaque, never orphans.
std::vector<std::string> OrphanKeys(const Card& card,
	const Template& tmpl);
// True when the card holds a value under the part id that does not match
// the part's current kind (typing would overwrite it, 8.2 / UI 5.3).
bool HasMismatchedValue(const Card& card, const Json& part);

// 8.5: first line of the title part's effective value, trimmed,
// "無題" when empty. tmpl may be nullptr (missing template).
std::string CardTitle(const Card& card, const Template* tmpl);
// Display text of a value (number -> shortest decimal, null -> "").
std::string ValueText(const Json& value);

// Collects every media reference in a JSON tree (cleanup, 13).
void CollectMediaRefs(const Json& value,
	std::set<std::string>& outRelPaths);
std::string MediaRelPath(const Json& mediaRef);
Json MakeMediaRef(const std::string& mediaId, const std::string& kind,
	const std::string& ext);

// The effective values of a card that differ from the part defaults
// (provisional cards, 8.6): true when any value part's effective value is
// not its default.
bool DiffersFromDefaults(const Card& card, const Template& tmpl);

// What the switches of a card do (part::SwitchEffect): the parts that are
// read-only and the parts that are not shown. With "lock_card", every part
// but the switch that locks is read-only. card may be nullptr (nothing is
// on then).
struct SwitchState {
	bool					cardLocked = false;
	std::set<std::string>	locked;
	std::set<std::string>	hidden;
};
SwitchState ComputeSwitchState(const Template& tmpl, const Card* card);


}	// namespace bdb

#endif	// BDB_MODEL_H
