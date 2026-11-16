/*
 * BDB card database - RenkuOS application
 * PDF export (I/O spec v0.1, chapter 4): the current card, selected cards
 * and all cards (one card per page, drawn by the card painter like on the
 * screen), and the list as a table.
 *
 * Implementation choice (4.6): the core's own PDF writer. Text is
 * embedded as Type 3 fonts built from the outlines the system font engine
 * returns (BFont::GetGlyphShapes), so Japanese text is embedded with any
 * installed font - including the CFF2 variable Noto Sans CJK of RenkuOS,
 * which PDF cannot embed directly - and stays searchable (ToUnicode).
 * Frames are vector graphics; JPEG files are embedded unchanged, other
 * images losslessly. Runs in its own thread; 32 and 64 bit.
 */
#ifndef BDB_PDF_EXPORT_H
#define BDB_PDF_EXPORT_H

#include <Messenger.h>
#include <String.h>

#include <string>
#include <vector>


class BMessage;
class StackDocument;


struct PdfColumn {
	std::string	partId;
	std::string	kind;
	BString		title;
};


struct PdfJob {
	enum Kind {
		kCards,			// one card per page
		kList			// the list as a table
	};

	StackDocument*		document = NULL;
	Kind				kind = kCards;
	BString				logKind = "card";	// card, cards, list (log)
	std::vector<std::string> cardIds;		// in list order
	std::string			templateId;			// list
	std::vector<PdfColumn> columns;			// list, visible columns
	BString				path;
	bool				printBackground = true;
	BString				paperSize = "A4";	// list
	BString				orientation = "landscape";
	bool				imageThumbnails = false;	// list: image columns
	BMessenger			replyTo;
};


// Starts the export thread (takes the job). The reply is kMsgPdfDone with
// "status" ("ok", "canceled", or an error code), "pages", "bytes".
void StartPdfExport(PdfJob* job);

// Options (background, list paper and image columns), then the save
// panel. The panel reply is kMsgPdfPanel carrying the options.
void ShowPdfSavePanel(BMessenger target, const char* defaultName,
	bool list);
// The chosen path of a kMsgPdfPanel reply.
bool PdfPanelPath(BMessage* message, BString& outPath);
// Shows the result of a kMsgPdfDone (errors only; success is quiet).
void ReportPdfResult(BMessage* message);


#endif	// BDB_PDF_EXPORT_H
