/*
 * BDB card database - RenkuOS application
 * Stack menu windows (UI spec v0.3, 6.9, 9) and a text prompt.
 */
#ifndef BDB_MISC_WINDOWS_H
#define BDB_MISC_WINDOWS_H


#include <Messenger.h>


class BWindow;
class StackDocument;


BWindow* CreateProblemsWindow(StackDocument* document);
BWindow* CreateStackSettingsWindow(StackDocument* document);
BWindow* CreateTemplateManager(StackDocument* document);

// A small window asking for one line of text; replies `what` with
// "text" to the target.
void AskText(const char* title, const char* label, const char* text,
	BMessenger target, uint32 what);


#endif	// BDB_MISC_WINDOWS_H
