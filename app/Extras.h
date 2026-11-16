/*
 * Renfuda (連札) card database - RenkuOS application
 * Opening links with the chosen browser, the application settings window,
 * the image viewer, and saving a card as an image (prototype extensions,
 * see the README).
 */
#ifndef BDB_EXTRAS_H
#define BDB_EXTRAS_H


#include <Messenger.h>
#include <SupportDefs.h>

#include <set>
#include <string>


namespace bdb {
class Card;
class Stack;
class Template;
class Theme;
}


// Opens a URL. http and https use the browser of the settings when one is
// set; everything else (and a missing browser) uses the system's preferred
// application. `usedFallback` (may be NULL) tells when the chosen browser
// was not found.
status_t OpenUrl(const char* url, bool* usedFallback);

// Opens a video in its preferred application (MediaPlayer). The player
// shows the file name, so the file is copied (in a thread) to the
// temporary folder under `name` first; equal copies are reused. Sends
// kMsgVideoFailed to `replyTo` when it cannot be opened.
void OpenVideo(const char* path, const char* name, BMessenger replyTo);
// Removes the copies made by OpenVideo() in earlier runs (at start: a
// player may still show a copy after the application quits).
void RemoveVideoCopies();

// The settings window (one at a time).
void ShowPreferences();

// Shows an image file at its own size; larger images scroll.
void ShowImage(const char* path, const char* title);

// Draws the card at its own size as in play mode (with the values, without
// the parts hidden by switches) and writes it as a PNG file.
status_t SaveCardImage(const bdb::Stack& stack, const bdb::Card& card,
	const bdb::Template& tmpl, const bdb::Theme& theme, const char* path,
	int32 position, int32 count);


#endif	// BDB_EXTRAS_H
