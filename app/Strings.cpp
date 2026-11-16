/*
 * BDB card database - RenkuOS application
 */
#include "Strings.h"

#include <LocaleRoster.h>
#include <Message.h>
#include <String.h>


namespace str {


bool gEnglish = false;


void
InitLanguage()
{
	// Japanese when the first preferred language is Japanese, or when it
	// cannot be told; English otherwise (UI 14.1).
	BMessage languages;
	BString first;
	if (BLocaleRoster::Default() != NULL
		&& BLocaleRoster::Default()->GetPreferredLanguages(&languages) == B_OK
		&& languages.FindString("language", 0, &first) == B_OK
		&& !first.IsEmpty()) {
		gEnglish = !first.StartsWith("ja");
	} else
		gEnglish = false;
}


}	// namespace str
