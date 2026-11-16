/*
 * BDB card database - RenkuOS application
 */
#include "MiscWindows.h"

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>

#include "Constants.h"
#include "Limits.h"
#include "Text.h"
#include "Stack.h"
#include "StackDocument.h"
#include "Strings.h"


using bdb::Json;


static const char*
ReasonText(const std::string& reason)
{
	if (reason == "invalid_format" || reason == "read_failed")
		return str::kReasonInvalid;
	if (reason == "limit_exceeded")
		return str::kReasonLimit;
	if (reason == "encrypted")
		return str::kReasonEncrypted;
	if (reason == "newer_version")
		return str::kReasonNewer;
	if (reason == "duplicate_id")
		return str::kReasonDuplicate;
	if (reason == "missing_template")
		return str::kReasonMissingTemplate;
	if (reason == "theme_missing")
		return str::kReasonThemeMissing;
	return str::kReasonOther;
}


// Windows of a document register as observers so that the document stays
// alive while they are open.
class DocumentWindow : public BWindow {
public:
	DocumentWindow(StackDocument* document, const char* title,
		window_type type, uint32 flags)
		:
		BWindow(BRect(140, 140, 560, 440), title, type,
			flags | B_AUTO_UPDATE_SIZE_LIMITS),
		fDocument(document)
	{
		fDocument->AddObserver(BMessenger(this));
	}

	virtual bool QuitRequested()
	{
		fDocument->RemoveObserver(BMessenger(this));
		BMessage released(kMsgDocumentReleased);
		released.AddPointer("document", fDocument);
		be_app->PostMessage(&released);
		return true;
	}

protected:
	StackDocument*	fDocument;
};


// #pragma mark - problems


class ProblemsWindow : public DocumentWindow {
public:
	ProblemsWindow(StackDocument* document)
		:
		DocumentWindow(document, str::kProblemsTitle, B_TITLED_WINDOW, 0)
	{
		fList = new BListView("problems");
		BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
			.Add(new BScrollView("scroll", fList, 0, true, true));
		_Fill();
		ResizeTo(520, 260);
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgDocCardsChanged:
			case kMsgDocTemplateChanged:
				_Fill();
				break;
			default:
				DocumentWindow::MessageReceived(message);
				break;
		}
	}

private:
	void _Fill()
	{
		for (int32 i = fList->CountItems() - 1; i >= 0; i--)
			delete fList->RemoveItem(i);
		DocLocker lock(fDocument);
		const std::vector<bdb::Problem>& problems
			= fDocument->GetStack().Problems();
		if (problems.empty())
			fList->AddItem(new BStringItem(str::kProblemsNone));
		for (const bdb::Problem& problem : problems) {
			BString text;
			text << ReasonText(problem.reason) << " — "
				<< problem.relPath.c_str();
			fList->AddItem(new BStringItem(text));
		}
	}

	BListView*	fList;
};


BWindow*
CreateProblemsWindow(StackDocument* document)
{
	return new ProblemsWindow(document);
}


// #pragma mark - stack settings


class StackSettingsWindow : public DocumentWindow {
public:
	StackSettingsWindow(StackDocument* document)
		:
		DocumentWindow(document, str::kSettingsTitle, B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_NOT_RESIZABLE)
	{
		BString title;
		{
			DocLocker lock(fDocument);
			title = fDocument->GetStack().Title().c_str();
		}
		fTitle = new BTextControl("title", str::kStackTitle, title, NULL);
		BButton* ok = new BButton("ok", str::kOK,
			new BMessage(kMsgSettingsOK));
		BButton* cancel = new BButton("cancel", str::kCancel,
			new BMessage(B_QUIT_REQUESTED));
		BLayoutBuilder::Group<>(this, B_VERTICAL)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(fTitle)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(cancel)
				.Add(ok)
			.End();
		SetDefaultButton(ok);
		fTitle->MakeFocus(true);
		ResizeTo(380, 100);
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != kMsgSettingsOK) {
			DocumentWindow::MessageReceived(message);
			return;
		}
		bdb::Status status;
		{
			DocLocker lock(fDocument);
			status = fDocument->GetStack().SetTitle(fTitle->Text());
		}
		if (!status) {
			BString text;
			text.SetToFormat(str::kTemplateSaveFailed, status.Code());
			(new BAlert(str::kAppName, text, str::kOK, NULL, NULL,
				B_WIDTH_AS_USUAL, B_STOP_ALERT))->Go();
			return;
		}
		BMessage notice(kMsgDocStackChanged);
		fDocument->Notify(&notice);
		PostMessage(B_QUIT_REQUESTED);
	}

private:
	BTextControl*	fTitle;
};


BWindow*
CreateStackSettingsWindow(StackDocument* document)
{
	return new StackSettingsWindow(document);
}


// #pragma mark - template manager


class TemplateItem : public BStringItem {
public:
	TemplateItem(const char* text, const std::string& id)
		:
		BStringItem(text),
		fId(id)
	{
	}

	const std::string& Id() const { return fId; }

private:
	std::string	fId;
};


class TemplateManagerWindow : public DocumentWindow {
public:
	TemplateManagerWindow(StackDocument* document)
		:
		DocumentWindow(document, str::kTemplatesTitle, B_TITLED_WINDOW, 0)
	{
		fList = new BListView("templates");
		fList->SetSelectionMessage(new BMessage(kMsgTemplateListSelect));
		fName = new BTextControl("name", str::kTemplateName, "", NULL);
		fWidth = new BTextControl("w", str::kTemplateWidth, "", NULL);
		fHeight = new BTextControl("h", str::kTemplateHeight, "", NULL);
		fTitlePart = new BMenuField("title part", str::kTemplateTitlePart,
			new BPopUpMenu("part"));
		fDefault = new BCheckBox("default", str::kTemplateDefault, NULL);
		fApply = new BButton("apply", str::kApply,
			new BMessage(kMsgTemplateApply));
		fDuplicate = new BButton("duplicate", str::kTemplateDuplicate,
			new BMessage(kMsgTemplateDuplicate));
		fDelete = new BButton("delete", str::kDelete,
			new BMessage(kMsgTemplateDelete));
		BButton* create = new BButton("new", str::kTemplateNew,
			new BMessage(kMsgTemplateNew));

		BLayoutBuilder::Group<>(this, B_HORIZONTAL)
			.SetInsets(B_USE_WINDOW_INSETS)
			.AddGroup(B_VERTICAL)
				.Add(new BScrollView("scroll", fList, 0, false, true))
				.AddGroup(B_HORIZONTAL)
					.Add(create)
					.Add(fDuplicate)
					.Add(fDelete)
				.End()
			.End()
			.AddGroup(B_VERTICAL)
				.AddGrid()
					.AddTextControl(fName, 0, 0)
					.AddTextControl(fWidth, 0, 1)
					.AddTextControl(fHeight, 0, 2)
					.AddMenuField(fTitlePart, 0, 3)
				.End()
				.Add(fDefault)
				.AddGlue()
				.AddGroup(B_HORIZONTAL)
					.AddGlue()
					.Add(fApply)
				.End()
			.End();
		_Fill("");
		ResizeTo(620, 320);
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgTemplateListSelect:
				_ShowSelected();
				break;
			case kMsgTemplateNew:
			{
				std::string id;
				bdb::Status status;
				{
					DocLocker lock(fDocument);
					status = fDocument->GetStack().CreateTemplate(
						bdb::kUntitled, bdb::kDefaultCardWidth,
						bdb::kDefaultCardHeight, &id);
				}
				_Done(status, id);
				break;
			}
			case kMsgTemplateDuplicate:
			{
				std::string selected = _SelectedId();
				if (selected.empty())
					break;
				std::string id;
				bdb::Status status;
				{
					DocLocker lock(fDocument);
					bdb::Stack& stack = fDocument->GetStack();
					std::string title = stack.FindTemplate(selected)->Title()
						+ str::kTemplateCopySuffix.Get();
					status = stack.DuplicateTemplate(selected, title, &id);
				}
				_Done(status, id);
				break;
			}
			case kMsgTemplateDelete:
				_Delete();
				break;
			case kMsgTemplateApply:
				_Apply();
				break;
			case kMsgDocTemplateChanged:
			case kMsgDocCardsChanged:
				_Fill(_SelectedId());
				break;
			default:
				DocumentWindow::MessageReceived(message);
				break;
		}
	}

private:
	std::string _SelectedId()
	{
		TemplateItem* item = dynamic_cast<TemplateItem*>(
			fList->ItemAt(fList->CurrentSelection()));
		return item != NULL ? item->Id() : std::string();
	}

	void _Fill(const std::string& select)
	{
		for (int32 i = fList->CountItems() - 1; i >= 0; i--)
			delete fList->RemoveItem(i);
		DocLocker lock(fDocument);
		bdb::Stack& stack = fDocument->GetStack();
		int32 selectIndex = 0;
		for (bdb::Template* tmpl : stack.Templates()) {
			BString text;
			text.SetToFormat(str::kTemplateUsage, tmpl->Title().c_str(),
				stack.TemplateUsage(tmpl->Id()));
			if (tmpl->Id() == select)
				selectIndex = fList->CountItems();
			fList->AddItem(new TemplateItem(text, tmpl->Id()));
		}
		fList->Select(selectIndex);
		_ShowSelected();
	}

	void _ShowSelected()
	{
		std::string id = _SelectedId();
		DocLocker lock(fDocument);
		bdb::Stack& stack = fDocument->GetStack();
		bdb::Template* tmpl = stack.FindTemplate(id);
		bool enabled = tmpl != NULL;
		fName->SetEnabled(enabled);
		fWidth->SetEnabled(enabled);
		fHeight->SetEnabled(enabled);
		fTitlePart->SetEnabled(enabled);
		fDefault->SetEnabled(enabled);
		fApply->SetEnabled(enabled);
		fDuplicate->SetEnabled(enabled);
		fDelete->SetEnabled(enabled);
		BMenu* menu = fTitlePart->Menu();
		menu->RemoveItems(0, menu->CountItems(), true);
		if (tmpl == NULL)
			return;
		fName->SetText(tmpl->Title().c_str());
		BString number;
		number << tmpl->Width();
		fWidth->SetText(number);
		number = "";
		number << tmpl->Height();
		fHeight->SetText(number);
		fDefault->SetValue(stack.DefaultTemplateId() == id
			? B_CONTROL_ON : B_CONTROL_OFF);

		// Title part: value parts only (UI spec 6.9).
		std::string current = tmpl->TitlePartSetting();
		BMenuItem* automatic = new BMenuItem(str::kTemplateTitlePartAuto,
			new BMessage('none'));
		automatic->SetMarked(current.empty()
			|| tmpl->FindPart(current) == NULL);
		menu->AddItem(automatic);
		for (const Json& part : tmpl->Parts()) {
			if (!bdb::KindHasValue(bdb::part::Kind(part)))
				continue;
			BMessage* message = new BMessage('none');
			message->AddString("part", bdb::part::Id(part).c_str());
			BMenuItem* item = new BMenuItem(bdb::part::Name(part).c_str(),
				message);
			item->SetMarked(bdb::part::Id(part) == current);
			menu->AddItem(item);
		}
		menu->SetLabelFromMarked(true);
	}

	void _Apply()
	{
		std::string id = _SelectedId();
		int32 width = atoi(fWidth->Text());
		int32 height = atoi(fHeight->Text());
		if (width < bdb::kMinCardSize || width > bdb::kMaxCardSize
			|| height < bdb::kMinCardSize || height > bdb::kMaxCardSize) {
			(new BAlert(str::kAppName, str::kTemplateSizeInvalid, str::kOK,
				NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT))->Go();
			return;
		}
		std::string titlePart;
		if (BMenuItem* marked = fTitlePart->Menu()->FindMarked()) {
			if (const char* part = marked->Message()->GetString("part", NULL))
				titlePart = part;
		}
		bdb::Status status;
		{
			DocLocker lock(fDocument);
			bdb::Stack& stack = fDocument->GetStack();
			bdb::Template* tmpl = stack.FindTemplate(id);
			if (tmpl == NULL)
				return;
			Json backup = tmpl->Doc();
			tmpl->SetTitle(fName->Text());
			if (width != tmpl->Width() || height != tmpl->Height()) {
				// A changed size no longer matches the paper (data 7).
				if (tmpl->Doc().Find("paper") != NULL) {
					tmpl->SetPaper("custom", tmpl->PaperOrientation(), width,
						height);
				} else
					tmpl->SetSize(width, height);
			}
			tmpl->SetTitlePart(titlePart);
			status = stack.SaveTemplate(id);
			if (!status) {
				Json rev = tmpl->Rev();
				tmpl->Doc() = backup;
				tmpl->Doc().Set("rev", rev);
			}
			if (status && fDefault->Value() == B_CONTROL_ON
				&& stack.GetManifest().DefaultTemplate() != id) {
				status = stack.SetDefaultTemplate(id);
			}
		}
		_Done(status, id);
	}

	void _Delete()
	{
		std::string id = _SelectedId();
		int32 usage;
		{
			DocLocker lock(fDocument);
			usage = fDocument->GetStack().TemplateUsage(id);
		}
		BString text;
		if (usage > 0)
			text.SetToFormat(str::kTemplateDeleteQuestion, (int)usage);
		else
			text = str::kTemplateDeleteUnused;
		BAlert* alert = new BAlert(str::kAppName, text, str::kCancel,
			str::kDelete, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		if (alert->Go() != 1)
			return;
		bdb::Status status;
		{
			DocLocker lock(fDocument);
			status = fDocument->GetStack().DeleteTemplate(id);
		}
		_Done(status, "");
	}

	void _Done(const bdb::Status& status, const std::string& select)
	{
		if (!status) {
			BString text;
			text.SetToFormat(str::kTemplateSaveFailed, status.Code());
			(new BAlert(str::kAppName, text, str::kOK, NULL, NULL,
				B_WIDTH_AS_USUAL, B_STOP_ALERT))->Go();
		}
		BMessage notice(kMsgDocTemplateChanged);
		notice.AddPointer("source", this);
		fDocument->Notify(&notice);
		BMessage cards(kMsgDocCardsChanged);
		cards.AddPointer("source", this);
		fDocument->Notify(&cards);
		_Fill(select);
	}

	BListView*		fList;
	BTextControl*	fName;
	BTextControl*	fWidth;
	BTextControl*	fHeight;
	BMenuField*		fTitlePart;
	BCheckBox*		fDefault;
	BButton*		fApply;
	BButton*		fDuplicate;
	BButton*		fDelete;
};


BWindow*
CreateTemplateManager(StackDocument* document)
{
	return new TemplateManagerWindow(document);
}


// #pragma mark - AskText


class AskTextWindow : public BWindow {
public:
	AskTextWindow(const char* title, const char* label, const char* text,
		BMessenger target, uint32 what)
		:
		BWindow(BRect(0, 0, 300, 80), title, B_MODAL_WINDOW_LOOK,
			B_MODAL_APP_WINDOW_FEEL, B_NOT_ZOOMABLE | B_NOT_RESIZABLE
				| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
		fTarget(target),
		fWhat(what)
	{
		fText = new BTextControl("text", label, text, NULL);
		BButton* ok = new BButton("ok", str::kOK, new BMessage('askO'));
		BButton* cancel = new BButton("cancel", str::kCancel,
			new BMessage(B_QUIT_REQUESTED));
		BLayoutBuilder::Group<>(this, B_VERTICAL)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(fText)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(cancel)
				.Add(ok)
			.End();
		SetDefaultButton(ok);
		fText->MakeFocus(true);
		fText->TextView()->SelectAll();
		CenterOnScreen();
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == 'askO') {
			BMessage reply(fWhat);
			reply.AddString("text", fText->Text());
			fTarget.SendMessage(&reply);
			Quit();
			return;
		}
		BWindow::MessageReceived(message);
	}

private:
	BMessenger		fTarget;
	uint32			fWhat;
	BTextControl*	fText;
};


void
AskText(const char* title, const char* label, const char* text,
	BMessenger target, uint32 what)
{
	(new AskTextWindow(title, label, text, target, what))->Show();
}
