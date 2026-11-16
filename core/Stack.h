/*
 * BDB card database - core
 * An open stack: loading (spec v0.4, chapter 11), the in-memory index,
 * card / template operations, provisional cards (8.6), value conversion
 * (20), cleanup (13).
 *
 * Not thread safe; the UI serializes access.
 */
#ifndef BDB_STACK_H
#define BDB_STACK_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Log.h"
#include "Model.h"
#include "Status.h"
#include "Store.h"
#include "Theme.h"


namespace bdb {


// Entry of the problem list (11.5, 11.6).
struct Problem {
	std::string	reason;		// invalid_format, limit_exceeded, encrypted,
							// newer_version, duplicate_id, missing_template,
							// theme_missing
	std::string	relPath;
	std::string	detail;
};


struct CleanupReport {
	int			orphanCards = 0;
	int			orphanKeys = 0;
	int			unusedMedia = 0;
	int			tempFiles = 0;
	// Media deletion is skipped while unreadable cards exist, because
	// they could reference media we cannot see.
	bool		mediaSkipped = false;
	std::vector<std::string> unusedMediaPaths;
	std::vector<std::string> tempFilePaths;
	int			failed = 0;		// execute: files that could not be written
};


struct ConvertReport {
	int			cards = 0;			// cards using the template
	int			convertible = 0;	// values that can be converted
	int			notConvertible = 0;	// values that cannot
	int			converted = 0;		// execute: values written
	int			failed = 0;			// execute: cards that failed to save
};


class Stack {
public:
	// `path` is a .rfdm file or the stack folder. `sink` may be nullptr.
	// With `themes`, templates whose theme cannot be found are put on the
	// problem list (11.9); without, only the stack's themes/ folder and
	// the built-in theme are known.
	static	Status			Open(const std::string& path, LogSink* sink,
								std::unique_ptr<Stack>& out,
								const ThemeLibrary* themes = nullptr);
	// Opens with a caller-supplied store (Phase 4 layer, tests).
	static	Status			Open(std::unique_ptr<StackStore> store,
								LogSink* sink, std::unique_ptr<Stack>& out,
								const ThemeLibrary* themes = nullptr);

							~Stack();

			StackStore&		Store() { return *fStore; }
			Logger&			Log() { return fLogger; }
	const	std::vector<Problem>& Problems() const { return fProblems; }

	// Manifest
			std::string		Id() const { return fManifest.StackId(); }
			std::string		Title() const { return fManifest.Title(); }
			Status			SetTitle(const std::string& title);
			std::string		DefaultTemplateId() const;
			Status			SetDefaultTemplate(const std::string& id);
	const	Manifest&		GetManifest() const { return fManifest; }

	// Templates
			std::vector<Template*> Templates() const;	// sorted by title
			Template*		FindTemplate(const std::string& id) const;
			Status			CreateTemplate(const std::string& title,
								int32_t width, int32_t height,
								std::string* outId);
			Status			DuplicateTemplate(const std::string& id,
								const std::string& title,
								std::string* outId);
	// Adds a complete template document (CSV import into an existing
	// stack). A template_id that exists already gets a new ID.
			Status			AddTemplate(const Json& doc, std::string* outId);
	// Saves the in-memory template (rev + 1).
			Status			SaveTemplate(const std::string& id,
								bool force = false);
			Status			ReloadTemplate(const std::string& id);
	// Moves the file away (Trash in the UI). Cards keep their values and
	// become "template missing".
			Status			DeleteTemplate(const std::string& id);
			int				TemplateUsage(const std::string& id) const;
	// Cards of the template that hold a value under the part id.
			int				CountValues(const std::string& templateId,
								const std::string& partId) const;

	// 20.1 - in memory; the caller saves the template.
			Status			ChangePartKind(const std::string& templateId,
								const std::string& partId,
								const std::string& newKind);
	// 20.2 - dry run when !execute.
			Status			ConvertValues(const std::string& templateId,
								const std::string& partId,
								const std::string& fromKind,
								const std::string& toKind, bool execute,
								ConvertReport& report);

	// Cards, in seq order (8.4). The provisional card (8.6) is not counted
	// and has no index, but FindCard() returns it.
			int				CardCount() const { return int(fOrder.size()); }
			Card*			CardAt(int index) const;
			Card*			FindCard(const std::string& id) const;
			int				IndexOf(const std::string& id) const;
			std::string		TitleOf(const Card& card) const;
			bool			IsTemplateMissing(const Card& card) const;

	// Inserts after `afterIndex` (-1: at the end).
			Status			CreateCard(const std::string& templateId,
								int afterIndex, std::string* outId);
			Status			DuplicateCard(const std::string& id,
								std::string* outId);
	// A card with the given values after the last card, written once and
	// not logged (bulk import, I/O spec 3.6; the import is logged as a
	// whole).
			Status			AppendCard(const std::string& templateId,
								const Json& values, std::string* outId);

	// 8.6: a new card that exists only in memory until a value differs
	// from the template defaults. Replaces an earlier provisional card.
	// It will be inserted after `afterId` (empty: at the end).
			Status			CreateProvisionalCard(
								const std::string& templateId,
								const std::string& afterId,
								std::string* outId);
			bool			IsProvisional(const std::string& id) const;
			std::string		ProvisionalId() const;
	// Saves the provisional card as a normal card (rev 1, card.create)
	// when a value differs from the defaults; otherwise does nothing.
	// `outCommitted` tells which happened.
			Status			CommitIfChanged(const std::string& id,
								bool* outCommitted = nullptr);
	// Drops the provisional card without a file or a log entry.
			void			DiscardProvisional();
			Status			DeleteCard(const std::string& id);
	// Saves the in-memory card. kConflict when the file changed on disk;
	// call again with force to overwrite, or ReloadCard(). For the
	// provisional card this is CommitIfChanged().
			Status			SaveCard(const std::string& id,
								bool force = false);
			Status			ReloadCard(const std::string& id);
			Status			ChangeCardTemplate(const std::string& id,
								const std::string& templateId);

	// Media (9). The previous file stays until cleanup (UI 5.4).
			Status			SetMedia(const std::string& cardId,
								const std::string& partId,
								const std::string& kind,
								const std::string& ext, InStream& source);
			Status			ClearMedia(const std::string& cardId,
								const std::string& partId);
			std::string		MediaPath(const Json& mediaRef) const;
	// Copies a file into media/<kind>/ with a new media_id and returns the
	// reference (backgrounds, icons; UI 6.11, theme spec 8.3). With
	// `reuse`, a file with the same content already in the folder is
	// referenced instead of copying again. `templateId` and `partId` are
	// only logged.
			Status			AddMedia(const std::string& kind,
								const std::string& ext, InStream& source,
								bool reuse, const std::string& templateId,
								const std::string& partId, Json& outRef);

	// Themes (theme spec 3)
			Status			FindTheme(const std::string& themeId,
								Theme& out) const;
			void			SetThemeLibrary(const ThemeLibrary* themes)
								{ fThemes = themes; }
	// The theme of a template: its own, or "beos" when missing.
			Theme			ThemeFor(const Template* tmpl) const;
	// Sets the template's theme in memory and copies the theme file into
	// the stack (3.3). kConflict: a different rev is in the stack; call
	// again with overwrite after asking. The caller saves the template.
			Status			ApplyTheme(const std::string& templateId,
								const std::string& themeId, bool overwrite);
	// 13 - dry run when !execute.
			Status			Cleanup(bool execute, CleanupReport& report);

private:
							Stack(std::unique_ptr<StackStore> store,
								LogSink* sink);
			Status			_Load();
			void			_AddProblem(const std::string& reason,
								const std::string& relPath,
								const std::string& detail);
			void			_SortOrder();
			int64_t			_SeqForInsert(int afterIndex);
			void			_Renumber();
			Status			_SaveCard(Card& card, bool force);
			Status			_InsertNewCard(std::unique_ptr<Card> card,
								std::string* outId);
			Status			_SaveNewCard(Card& card);
			void			_AdoptCard(std::unique_ptr<Card> card);
			Status			_CommitProvisional();
			void			_CheckThemes();
			std::string		_DesiredCardPath(const Card& card) const;
			std::string		_DesiredTemplatePath(const Template& tmpl)
								const;

			std::unique_ptr<StackStore> fStore;
			Logger			fLogger;
			Manifest		fManifest;
			std::map<std::string, std::unique_ptr<Template>> fTemplates;
			std::map<std::string, std::unique_ptr<Card>> fCards;
			std::vector<Card*> fOrder;
			std::vector<Problem> fProblems;
			int				fUnreadableCards = 0;
			std::unique_ptr<Card> fProvisional;
			std::string		fProvisionalAfter;
			const ThemeLibrary* fThemes = nullptr;
};


}	// namespace bdb

#endif	// BDB_STACK_H
