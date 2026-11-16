/*
 * BDB card database - core
 * Themes (theme spec v0.1): the .rfds file, resolving how a part looks
 * (chapter 5) and finding theme files (chapter 3).
 */
#ifndef BDB_THEME_H
#define BDB_THEME_H

#include <cstdint>
#include <string>
#include <vector>

#include "Json.h"
#include "Status.h"


namespace bdb {


class StackStore;


struct Color {
	uint8_t	r = 0;
	uint8_t	g = 0;
	uint8_t	b = 0;

	static	Color	Make(uint8_t r, uint8_t g, uint8_t b)
						{ Color c; c.r = r; c.g = g; c.b = b; return c; }
	bool	operator==(const Color& other) const
				{ return r == other.r && g == other.g && b == other.b; }
	bool	operator!=(const Color& other) const
				{ return !(*this == other); }
};

// "#rrggbb" -> color; false when invalid.
bool ColorFromString(const std::string& text, Color& out);
std::string ColorToString(const Color& color);


// How one part looks after applying the precedence rules (chapter 5).
struct PartLook {
	std::string	look;			// none, flat, raised, sunken, raised2,
								// sunken2, etched, round (4.3)
	std::string	pressedLook;	// buttons while pressed; empty: look
	bool		borderVisible = true;
	int			borderWidth = 1;
	Color		borderColor;	// flat / round lines
	bool		borderColorSet = false;	// the part chose a color
	bool		hasFill = false;
	Color		fill;
	int			radius = 0;
	std::string	fontFamily;		// empty: the system font
	std::string	fontStyle;
	int			fontSize = 12;
	Color		textColor;
	// Which values came from the part's own style (inspector: shown
	// normally) rather than from the theme (shown dimmed, UI 6.12).
	bool		ownBorder = false;
	bool		ownFill = false;
	bool		ownFont = false;

	// True when the look draws no line at all.
	bool		NoBorder() const
					{ return !borderVisible || borderWidth == 0
						|| look == "none"; }
};


class Theme {
public:
	// Parses and checks a .rfds document (4.1, 4.2). Unknown keys are
	// ignored; invalid colors are dropped (they fall back to the
	// default theme) rather than failing the theme.
	static	Status			Parse(const std::string& data, Theme& out);
	// The built-in "beos" theme; used when nothing else is found (3.2).
	static	const Theme&	Default();
	static	const char*		DefaultData();

			std::string		Id() const { return fDoc.GetString("theme_id"); }
			int64_t			Rev() const { return fDoc.GetInt("rev", 1); }
	// lang: "ja" or "en"; falls back to the other language (4.2).
			std::string		Name(const std::string& lang) const;
			std::string		Description(const std::string& lang) const;
			bool			Native() const;

	// colors.<key>, falling back to the default theme (chapter 5, 4).
			Color			GetColor(const char* key) const;
	// Replaces a color in memory (native themes take the system's colors,
	// 4.2).
			void			SetColor(const char* key, const Color& color);
			int				FocusWidth() const;
			Color			FocusColor() const;
	// The theme font (4.2): family may be empty (system font).
			std::string		FontFamily() const;
			std::string		FontStyle() const;
			int				FontSize() const;

	// The resolved look of a part (chapter 5): part style > theme kind
	// > theme colors and font > default theme.
			PartLook		Resolve(const Json& part) const;
	// The look of a kind without a part style.
			PartLook		ResolveKind(const std::string& kind) const;

	const	Json&			Doc() const { return fDoc; }

private:
	const	Json*			_Kind(const std::string& kind) const;

			Json			fDoc;
};


struct ThemeInfo {
	std::string	id;
	std::string	nameJa;
	std::string	nameEn;
	int64_t		rev = 1;
	std::string	origin;		// "stack", "user", "bundled", "builtin"
};


// Finds theme files: the stack's themes/ folder first, then the user's
// folder, then the bundled folder (3.2).
class ThemeLibrary {
public:
							ThemeLibrary() {}
							ThemeLibrary(const std::string& userDir,
								const std::string& bundledDir)
								: fUserDir(userDir), fBundledDir(bundledDir)
								{}

			void			SetDirectories(const std::string& userDir,
								const std::string& bundledDir)
								{ fUserDir = userDir;
									fBundledDir = bundledDir; }

	// `store` may be nullptr. kNotFound when the theme does not exist
	// anywhere ("beos" is always found: the built-in one at least).
			Status			Find(const std::string& themeId,
								StackStore* store, Theme& out,
								std::string* origin = nullptr) const;
	// All themes that can be chosen, by ID (stack, user, bundled, built-in;
	// the first origin wins).
			std::vector<ThemeInfo> List(StackStore* store) const;

	// Copies the theme into the stack's themes/ folder (3.3). Nothing is
	// written when the same rev is already there. When a different rev
	// is there and !overwrite, returns kConflict (the UI asks first).
			Status			CopyToStack(const std::string& themeId,
								StackStore& store, bool overwrite) const;

private:
			Status			_FindOutside(const std::string& themeId,
								Theme& out, std::string* origin,
								std::string* data) const;

			std::string		fUserDir;
			std::string		fBundledDir;
};


// themes/<id>.rfds
std::string ThemeRelPath(const std::string& themeId);


}	// namespace bdb

#endif	// BDB_THEME_H
