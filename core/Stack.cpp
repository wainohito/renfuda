/*
 * BDB card database - core
 */
#include "Stack.h"

#include <algorithm>
#include <climits>
#include <cstring>

#include "Ids.h"
#include "Limits.h"
#include "Text.h"


namespace bdb {


// Writes "bdb" and "format_version" as the first two keys (5.2). Older
// format versions are converted in memory and written as the current one.
static void
NormalizeHeader(Json& doc, const char* type)
{
	doc.SetAt(0, "bdb", type);
	doc.SetAt(1, "format_version", kFormatVersion);
}


static std::string
ProblemReason(const Status& status)
{
	switch (status.Kind()) {
		case ErrorKind::kLimitExceeded:	return "limit_exceeded";
		case ErrorKind::kUnsupported:	return "encrypted";
		case ErrorKind::kNewerVersion:	return "newer_version";
		case ErrorKind::kNotFound:		return "not_found";
		case ErrorKind::kReadFailed:	return "read_failed";
		default:						return "invalid_format";
	}
}


static std::string
DirectoryOf(const std::string& relPath)
{
	size_t slash = relPath.rfind('/');
	return slash == std::string::npos ? std::string() : relPath.substr(0, slash);
}


// Doc with the higher rev wins; equal rev: the newer file (11.6).
static bool
Supersedes(const StoredDoc& candidate, const StoredDoc& existing)
{
	int64_t candidateRev = candidate.doc.GetInt("rev", 0);
	int64_t existingRev = existing.doc.GetInt("rev", 0);
	if (candidateRev != existingRev)
		return candidateRev > existingRev;
	return candidate.stamp.mtimeNs > existing.stamp.mtimeNs;
}


// One fsync at the end of bulk work instead of one per file (12.4).
class BulkSync {
public:
	BulkSync(StackStore& store, bool active = true)
		: fStore(store), fActive(active)
	{
		if (fActive)
			fStore.SetDeferSync(true);
	}
	~BulkSync()
	{
		if (fActive) {
			fStore.SetDeferSync(false);
			fStore.SyncAll();
		}
	}
private:
	StackStore&	fStore;
	bool		fActive;
};


// #pragma mark - open / load


Stack::Stack(std::unique_ptr<StackStore> store, LogSink* sink)
	:
	fStore(std::move(store)),
	fLogger(sink, "")
{
}


Stack::~Stack()
{
}


Status
Stack::Open(const std::string& path, LogSink* sink, std::unique_ptr<Stack>& out,
	const ThemeLibrary* themes)
{
	std::string root;
	Status status = FolderStore::ResolveRoot(path, root);
	if (!status) {
		Logger(sink, "").Error("stack.open.failed", {{"err", status.Code()}});
		return status;
	}
	return Open(std::unique_ptr<StackStore>(new FolderStore(root)), sink, out,
		themes);
}


Status
Stack::Open(std::unique_ptr<StackStore> store, LogSink* sink,
	std::unique_ptr<Stack>& out, const ThemeLibrary* themes)
{
	std::unique_ptr<Stack> stack(new Stack(std::move(store), sink));
	stack->fThemes = themes;
	Status status = stack->_Load();
	if (!status) {
		Logger(sink, "").Error("stack.open.failed", {{"err", status.Code()}});
		return status;
	}
	out = std::move(stack);
	return Status::Ok();
}


void
Stack::_AddProblem(const std::string& reason, const std::string& relPath,
	const std::string& detail)
{
	fProblems.push_back({reason, relPath, detail});
	fLogger.Warn("stack.problem", {{"reason", reason}, {"path", relPath}});
}


Status
Stack::_Load()
{
	StoredDoc manifest;
	Status status = fStore->ReadManifest(manifest);
	if (!status)
		return status;
	status = Manifest::Validate(manifest.doc);
	if (!status)
		return status;
	fManifest = Manifest(std::move(manifest));
	fLogger.SetStackId(fManifest.StackId());

	std::vector<std::string> paths;
	fStore->ListTemplates(paths);
	std::sort(paths.begin(), paths.end());
	for (const std::string& relPath : paths) {
		StoredDoc stored;
		status = fStore->ReadDoc(relPath, DocType::kTemplate, stored);
		if (status)
			status = Template::Validate(stored.doc);
		if (!status) {
			_AddProblem(ProblemReason(status), relPath, status.Detail());
			continue;
		}
		std::string id = stored.doc.GetString("template_id");
		auto existing = fTemplates.find(id);
		if (existing != fTemplates.end()) {
			if (Supersedes(stored, existing->second->Stored())) {
				_AddProblem("duplicate_id", existing->second->Stored().relPath,
					id);
				existing->second.reset(new Template(std::move(stored)));
			} else
				_AddProblem("duplicate_id", relPath, id);
			continue;
		}
		fTemplates[id].reset(new Template(std::move(stored)));
	}

	paths.clear();
	fStore->ListCards(paths);
	std::sort(paths.begin(), paths.end());
	for (const std::string& relPath : paths) {
		StoredDoc stored;
		status = fStore->ReadDoc(relPath, DocType::kCard, stored);
		if (status)
			status = Card::Validate(stored.doc);
		if (!status) {
			fUnreadableCards++;
			_AddProblem(ProblemReason(status), relPath, status.Detail());
			continue;
		}
		std::string id = stored.doc.GetString("card_id");
		auto existing = fCards.find(id);
		if (existing != fCards.end()) {
			if (Supersedes(stored, existing->second->Stored())) {
				_AddProblem("duplicate_id", existing->second->Stored().relPath,
					id);
				existing->second.reset(new Card(std::move(stored)));
			} else
				_AddProblem("duplicate_id", relPath, id);
			continue;
		}
		fCards[id].reset(new Card(std::move(stored)));
	}

	for (const auto& entry : fCards) {
		if (FindTemplate(entry.second->TemplateId()) == nullptr) {
			_AddProblem("missing_template", entry.second->Stored().relPath,
				entry.second->TemplateId());
		}
	}

	_CheckThemes();
	_SortOrder();
	fLogger.Info("stack.open", {
		{"cards", int64_t(fCards.size())},
		{"templates", int64_t(fTemplates.size())},
		{"problems", int64_t(fProblems.size())}});
	return Status::Ok();
}


void
Stack::_SortOrder()
{
	fOrder.clear();
	fOrder.reserve(fCards.size());
	for (const auto& entry : fCards)
		fOrder.push_back(entry.second.get());
	// Cached keys: Seq()/Id() look up JSON keys.
	std::vector<std::pair<std::pair<int64_t, std::string>, Card*>> keyed;
	keyed.reserve(fOrder.size());
	for (Card* card : fOrder)
		keyed.push_back({{card->Seq(), card->Id()}, card});
	std::sort(keyed.begin(), keyed.end(),
		[](const auto& a, const auto& b) { return a.first < b.first; });
	for (size_t i = 0; i < keyed.size(); i++)
		fOrder[i] = keyed[i].second;
}


// #pragma mark - manifest


Status
Stack::SetTitle(const std::string& title)
{
	std::string previous = fManifest.Title();
	fManifest.SetTitle(title);
	fManifest.Doc().Set("modified", NowTimestamp());
	NormalizeHeader(fManifest.Doc(), "manifest");
	Status status = fStore->WriteManifest(fManifest.Stored());
	if (!status) {
		fManifest.SetTitle(previous);
		fLogger.Error("stack.save.failed", {{"err", status.Code()}});
		return status;
	}
	fLogger.Info("stack.update", {{"fields", Json(JsonArray{Json("title")})}});
	return Status::Ok();
}


std::string
Stack::DefaultTemplateId() const
{
	std::string id = fManifest.DefaultTemplate();
	if (!id.empty() && FindTemplate(id) != nullptr)
		return id;
	std::vector<Template*> templates = Templates();
	return templates.empty() ? std::string() : templates[0]->Id();
}


Status
Stack::SetDefaultTemplate(const std::string& id)
{
	std::string previous = fManifest.DefaultTemplate();
	fManifest.SetDefaultTemplate(id);
	fManifest.Doc().Set("modified", NowTimestamp());
	NormalizeHeader(fManifest.Doc(), "manifest");
	Status status = fStore->WriteManifest(fManifest.Stored());
	if (!status) {
		fManifest.SetDefaultTemplate(previous);
		return status;
	}
	fLogger.Info("stack.update", {{"fields",
		Json(JsonArray{Json("default_template")})}});
	return Status::Ok();
}


// #pragma mark - templates


std::vector<Template*>
Stack::Templates() const
{
	std::vector<Template*> result;
	for (const auto& entry : fTemplates)
		result.push_back(entry.second.get());
	std::sort(result.begin(), result.end(),
		[](const Template* a, const Template* b) {
			std::string titleA = a->Title();
			std::string titleB = b->Title();
			if (titleA != titleB)
				return titleA < titleB;
			return a->Id() < b->Id();
		});
	return result;
}


Template*
Stack::FindTemplate(const std::string& id) const
{
	auto found = fTemplates.find(id);
	return found != fTemplates.end() ? found->second.get() : nullptr;
}


std::string
Stack::_DesiredTemplatePath(const Template& tmpl) const
{
	std::string dir = tmpl.Stored().relPath.empty() ? std::string("templates")
		: DirectoryOf(tmpl.Stored().relPath);
	return dir + "/" + MakeFileName(tmpl.Title(), tmpl.Id(), ".rfdt");
}


Status
Stack::CreateTemplate(const std::string& title, int32_t width, int32_t height,
	std::string* outId)
{
	if (width < kMinCardSize || width > kMaxCardSize || height < kMinCardSize
		|| height > kMaxCardSize) {
		return Status(ErrorKind::kInvalidArgument, "size");
	}
	std::unique_ptr<Template> tmpl(new Template(
		Template::Create(title, width, height)));
	while (true) {
		NormalizeHeader(tmpl->Doc(), "template");
		Status status = fStore->WriteDoc(tmpl->Stored(),
			_DesiredTemplatePath(*tmpl), false);
		if (status)
			break;
		if (status.Kind() != ErrorKind::kExists) {
			fLogger.Error("template.save.failed", {{"err", status.Code()}});
			return status;
		}
		// Same file name: pick a new ID (10).
		tmpl->Doc().Set("template_id", NewId());
	}
	std::string id = tmpl->Id();
	fLogger.Info("template.create", {{"template", id}, {"rev", tmpl->Rev()}});
	fTemplates[id] = std::move(tmpl);
	if (outId != nullptr)
		*outId = id;
	return Status::Ok();
}


Status
Stack::DuplicateTemplate(const std::string& id, const std::string& title,
	std::string* outId)
{
	Template* source = FindTemplate(id);
	if (source == nullptr)
		return Status(ErrorKind::kNotFound, id);
	StoredDoc stored;
	stored.doc = source->Doc();
	std::unique_ptr<Template> tmpl(new Template(std::move(stored)));
	tmpl->Doc().Set("template_id", NewId());
	tmpl->SetTitle(title);
	tmpl->Doc().Set("rev", 1);
	while (true) {
		NormalizeHeader(tmpl->Doc(), "template");
		Status status = fStore->WriteDoc(tmpl->Stored(),
			"templates/" + MakeFileName(tmpl->Title(), tmpl->Id(), ".rfdt"),
			false);
		if (status)
			break;
		if (status.Kind() != ErrorKind::kExists) {
			fLogger.Error("template.save.failed", {{"err", status.Code()}});
			return status;
		}
		tmpl->Doc().Set("template_id", NewId());
	}
	std::string newId = tmpl->Id();
	fLogger.Info("template.create", {{"template", newId},
		{"rev", tmpl->Rev()}});
	fTemplates[newId] = std::move(tmpl);
	if (outId != nullptr)
		*outId = newId;
	return Status::Ok();
}


Status
Stack::AddTemplate(const Json& doc, std::string* outId)
{
	StoredDoc stored;
	stored.doc = doc;
	std::unique_ptr<Template> tmpl(new Template(std::move(stored)));
	if (!IsValidId(tmpl->Id()) || FindTemplate(tmpl->Id()) != nullptr)
		tmpl->Doc().Set("template_id", NewId());
	tmpl->Doc().Set("rev", 1);
	NormalizeHeader(tmpl->Doc(), "template");
	Status status = Template::Validate(tmpl->Doc());
	if (!status)
		return status;
	while (true) {
		status = fStore->WriteDoc(tmpl->Stored(), _DesiredTemplatePath(*tmpl),
			false);
		if (status)
			break;
		if (status.Kind() != ErrorKind::kExists) {
			fLogger.Error("template.save.failed", {{"err", status.Code()}});
			return status;
		}
		tmpl->Doc().Set("template_id", NewId());
	}
	std::string id = tmpl->Id();
	fLogger.Info("template.create", {{"template", id}, {"rev", tmpl->Rev()}});
	fTemplates[id] = std::move(tmpl);
	if (outId != nullptr)
		*outId = id;
	return Status::Ok();
}


Status
Stack::SaveTemplate(const std::string& id, bool force)
{
	Template* tmpl = FindTemplate(id);
	if (tmpl == nullptr)
		return Status(ErrorKind::kNotFound, id);
	Json previousRev = tmpl->Rev();
	tmpl->Doc().Set("rev", tmpl->Rev() + 1);
	tmpl->Doc().Set("modified", NowTimestamp());
	NormalizeHeader(tmpl->Doc(), "template");
	Status status = Template::Validate(tmpl->Doc());
	if (status) {
		status = fStore->WriteDoc(tmpl->Stored(), _DesiredTemplatePath(*tmpl),
			!force);
	}
	if (!status) {
		tmpl->Doc().Set("rev", previousRev);
		if (status.Kind() == ErrorKind::kConflict)
			fLogger.Warn("template.save.conflict", {{"template", id}});
		else {
			fLogger.Error("template.save.failed", {{"template", id},
				{"err", status.Code()}});
		}
		return status;
	}
	fLogger.Info("template.save", {{"template", id}, {"rev", tmpl->Rev()}});
	return Status::Ok();
}


Status
Stack::ReloadTemplate(const std::string& id)
{
	Template* tmpl = FindTemplate(id);
	if (tmpl == nullptr)
		return Status(ErrorKind::kNotFound, id);
	StoredDoc stored;
	Status status = fStore->ReadDoc(tmpl->Stored().relPath, DocType::kTemplate,
		stored);
	if (status)
		status = Template::Validate(stored.doc);
	if (!status)
		return status;
	if (stored.doc.GetString("template_id") != id)
		return Status(ErrorKind::kInvalidFormat, "template_id changed");
	*tmpl = Template(std::move(stored));
	return Status::Ok();
}


Status
Stack::DeleteTemplate(const std::string& id)
{
	Template* tmpl = FindTemplate(id);
	if (tmpl == nullptr)
		return Status(ErrorKind::kNotFound, id);
	Status status = fStore->RemoveDoc(tmpl->Stored().relPath);
	if (!status) {
		fLogger.Error("template.delete.failed", {{"template", id},
			{"err", status.Code()}});
		return status;
	}
	fTemplates.erase(id);
	fLogger.Info("template.delete", {{"template", id}});
	return Status::Ok();
}


int
Stack::TemplateUsage(const std::string& id) const
{
	int count = 0;
	for (const Card* card : fOrder) {
		if (card->TemplateId() == id)
			count++;
	}
	return count;
}


int
Stack::CountValues(const std::string& templateId,
	const std::string& partId) const
{
	int count = 0;
	for (const Card* card : fOrder) {
		if (card->TemplateId() == templateId && card->Value(partId) != nullptr)
			count++;
	}
	return count;
}


Status
Stack::ChangePartKind(const std::string& templateId, const std::string& partId,
	const std::string& newKind)
{
	Template* tmpl = FindTemplate(templateId);
	if (tmpl == nullptr)
		return Status(ErrorKind::kNotFound, templateId);
	Json* part = tmpl->FindPart(partId);
	if (part == nullptr)
		return Status(ErrorKind::kNotFound, partId);
	std::string oldKind = part::Kind(*part);
	if (!IsAssignableKind(oldKind) || !IsAssignableKind(newKind))
		return Status(ErrorKind::kInvalidArgument, "kind");
	if (oldKind == newKind)
		return Status::Ok();
	part::ChangeKind(*part, newKind);
	fLogger.Info("template.kind_change", {{"template", templateId},
		{"part", partId}, {"from", oldKind}, {"to", newKind}});
	return Status::Ok();
}


// Converts one value (20.2). Returns false when it cannot be converted.
static bool
ConvertValue(const Json& value, const std::string& fromKind,
	const std::string& toKind, Json& out)
{
	if (fromKind == kKindText && toKind == kKindNumber) {
		if (!value.IsString())
			return false;
		std::string trimmed = TrimSpaces(value.AsString());
		if (trimmed.empty()) {
			out = Json();
			return true;
		}
		double number;
		if (!ParseDecimal(trimmed, number) || !IsValidNumber(number))
			return false;
		out = MakeNumberValue(number);
		return true;
	}
	if (fromKind == kKindNumber && toKind == kKindText) {
		if (value.IsNull()) {
			out = Json("");
			return true;
		}
		if (!value.IsNumber() || !IsValidNumber(value.AsDouble()))
			return false;
		out = Json(FormatNumber(value.AsDouble()));
		return true;
	}
	return false;
}


Status
Stack::ConvertValues(const std::string& templateId, const std::string& partId,
	const std::string& fromKind, const std::string& toKind, bool execute,
	ConvertReport& report)
{
	report = ConvertReport();
	if (FindTemplate(templateId) == nullptr)
		return Status(ErrorKind::kNotFound, templateId);
	BulkSync bulk(*fStore, execute);
	for (Card* card : fOrder) {
		if (card->TemplateId() != templateId)
			continue;
		report.cards++;
		const Json* value = card->Value(partId);
		if (value == nullptr || ValueMatchesKind(*value, toKind))
			continue;
		Json converted;
		if (!ConvertValue(*value, fromKind, toKind, converted)) {
			report.notConvertible++;
			continue;
		}
		report.convertible++;
		if (!execute)
			continue;
		Json original = *value;
		card->SetValue(partId, converted);
		if (_SaveCard(*card, false)) {
			report.converted++;
		} else {
			card->SetValue(partId, original);
			report.failed++;
		}
	}
	if (execute) {
		fLogger.Info("template.convert", {{"template", templateId},
			{"part", partId}, {"from", fromKind}, {"to", toKind},
			{"converted", int64_t(report.converted)},
			{"skipped", int64_t(report.notConvertible + report.failed)}});
	}
	return Status::Ok();
}


// #pragma mark - cards


Card*
Stack::CardAt(int index) const
{
	if (index < 0 || index >= int(fOrder.size()))
		return nullptr;
	return fOrder[index];
}


Card*
Stack::FindCard(const std::string& id) const
{
	auto found = fCards.find(id);
	if (found != fCards.end())
		return found->second.get();
	if (fProvisional != nullptr && !id.empty() && fProvisional->Id() == id)
		return fProvisional.get();
	return nullptr;
}


int
Stack::IndexOf(const std::string& id) const
{
	for (size_t i = 0; i < fOrder.size(); i++) {
		if (fOrder[i]->Id() == id)
			return int(i);
	}
	return -1;
}


std::string
Stack::TitleOf(const Card& card) const
{
	return CardTitle(card, FindTemplate(card.TemplateId()));
}


bool
Stack::IsTemplateMissing(const Card& card) const
{
	return FindTemplate(card.TemplateId()) == nullptr;
}


std::string
Stack::_DesiredCardPath(const Card& card) const
{
	// Existing cards stay in their folder (4); new cards go to cards/.
	std::string dir = card.Stored().relPath.empty() ? std::string("cards")
		: DirectoryOf(card.Stored().relPath);
	return dir + "/" + MakeFileName(TitleOf(card), card.Id(), ".rfdd");
}


Status
Stack::_SaveCard(Card& card, bool force)
{
	bool isNew = card.Stored().relPath.empty();
	Json previousRev = card.Rev();
	Json previousModified;
	if (const Json* modified = card.Doc().Find("modified"))
		previousModified = *modified;
	if (!isNew)
		card.Doc().Set("rev", card.Rev() + 1);
	card.Doc().Set("modified", NowTimestamp());
	NormalizeHeader(card.Doc(), "card");

	Status status = fStore->WriteDoc(card.Stored(), _DesiredCardPath(card),
		!force);
	if (!status) {
		card.Doc().Set("rev", previousRev);
		if (!previousModified.IsNull())
			card.Doc().Set("modified", previousModified);
		if (status.Kind() == ErrorKind::kConflict)
			fLogger.Warn("card.save.conflict", {{"card", card.Id()}});
		else if (status.Kind() != ErrorKind::kExists) {
			fLogger.Error("card.save.failed", {{"card", card.Id()},
				{"err", status.Code()}});
		}
		return status;
	}
	if (!isNew)
		fLogger.Info("card.save", {{"card", card.Id()}, {"rev", card.Rev()}});
	return Status::Ok();
}


int64_t
Stack::_SeqForInsert(int afterIndex)
{
	if (fOrder.empty())
		return kSeqStep;
	if (afterIndex < 0 || afterIndex >= int(fOrder.size()) - 1)
		return fOrder.back()->Seq() + kSeqStep;
	int64_t previous = fOrder[afterIndex]->Seq();
	int64_t next = fOrder[afterIndex + 1]->Seq();
	if (next - previous >= 2)
		return previous + (next - previous) / 2;
	return INT64_MIN;
}


void
Stack::_Renumber()
{
	BulkSync bulk(*fStore);
	for (size_t i = 0; i < fOrder.size(); i++) {
		Card* card = fOrder[i];
		int64_t seq = int64_t(i + 1) * kSeqStep;
		if (card->Seq() == seq)
			continue;
		int64_t previous = card->Seq();
		card->SetSeq(seq);
		if (!_SaveCard(*card, false))
			card->SetSeq(previous);
	}
	_SortOrder();
}


Status
Stack::_SaveNewCard(Card& card)
{
	while (true) {
		Status status = _SaveCard(card, false);
		if (status)
			return status;
		if (status.Kind() != ErrorKind::kExists)
			return status;
		// Same file name: pick a new ID (10).
		card.Doc().Set("card_id", NewId());
	}
}


void
Stack::_AdoptCard(std::unique_ptr<Card> card)
{
	std::string id = card->Id();
	Card* inserted = card.get();
	fCards[id] = std::move(card);
	// Binary insertion keeps adding a card O(n) instead of a full sort.
	std::pair<int64_t, std::string> key(inserted->Seq(), id);
	auto position = std::upper_bound(fOrder.begin(), fOrder.end(), key,
		[](const std::pair<int64_t, std::string>& value, const Card* other) {
			int64_t seq = other->Seq();
			if (value.first != seq)
				return value.first < seq;
			return value.second < other->Id();
		});
	fOrder.insert(position, inserted);
}


Status
Stack::_InsertNewCard(std::unique_ptr<Card> card, std::string* outId)
{
	Status status = _SaveNewCard(*card);
	if (!status)
		return status;
	std::string id = card->Id();
	_AdoptCard(std::move(card));
	if (outId != nullptr)
		*outId = id;
	return Status::Ok();
}


Status
Stack::CreateCard(const std::string& templateId, int afterIndex,
	std::string* outId)
{
	int64_t seq = _SeqForInsert(afterIndex);
	if (seq == INT64_MIN) {
		_Renumber();
		seq = _SeqForInsert(afterIndex);
		if (seq == INT64_MIN)
			return Status(ErrorKind::kWriteFailed, "renumber");
	}
	std::unique_ptr<Card> card(new Card(Card::Create(templateId, seq)));
	std::string id;
	Status status = _InsertNewCard(std::move(card), &id);
	if (!status)
		return status;
	fLogger.Info("card.create", {{"card", id}, {"template", templateId}});
	if (outId != nullptr)
		*outId = id;
	return Status::Ok();
}


Status
Stack::AppendCard(const std::string& templateId, const Json& values,
	std::string* outId)
{
	int64_t seq = fOrder.empty() ? kSeqStep : fOrder.back()->Seq() + kSeqStep;
	std::unique_ptr<Card> card(new Card(Card::Create(templateId, seq)));
	card->Values() = values;
	return _InsertNewCard(std::move(card), outId);
}


Status
Stack::DuplicateCard(const std::string& id, std::string* outId)
{
	Card* source = FindCard(id);
	if (source == nullptr)
		return Status(ErrorKind::kNotFound, id);
	if (IsProvisional(id))
		return Status(ErrorKind::kInvalidArgument, "provisional card");
	int index = IndexOf(id);
	int64_t seq = _SeqForInsert(index);
	if (seq == INT64_MIN) {
		_Renumber();
		index = IndexOf(id);
		seq = _SeqForInsert(index);
	}
	StoredDoc stored;
	stored.doc = source->Doc();
	std::unique_ptr<Card> card(new Card(std::move(stored)));
	card->Doc().Set("card_id", NewId());
	card->Doc().Set("seq", seq);
	card->Doc().Set("rev", 1);
	std::string now = NowTimestamp();
	card->Doc().Set("created", now);
	card->Doc().Set("modified", now);
	std::string templateId = card->TemplateId();
	std::string newId;
	Status status = _InsertNewCard(std::move(card), &newId);
	if (!status)
		return status;
	fLogger.Info("card.duplicate", {{"card", newId},
		{"template", templateId}});
	if (outId != nullptr)
		*outId = newId;
	return Status::Ok();
}


Status
Stack::DeleteCard(const std::string& id)
{
	if (IsProvisional(id)) {
		DiscardProvisional();
		return Status::Ok();
	}
	Card* card = FindCard(id);
	if (card == nullptr)
		return Status(ErrorKind::kNotFound, id);
	if (!card->Stored().relPath.empty()) {
		Status status = fStore->RemoveDoc(card->Stored().relPath);
		if (!status) {
			fLogger.Error("card.delete.failed", {{"card", id},
				{"err", status.Code()}});
			return status;
		}
	}
	fOrder.erase(std::find(fOrder.begin(), fOrder.end(), card));
	fCards.erase(id);
	fLogger.Info("card.delete", {{"card", id}});
	return Status::Ok();
}


Status
Stack::SaveCard(const std::string& id, bool force)
{
	if (IsProvisional(id))
		return CommitIfChanged(id);
	Card* card = FindCard(id);
	if (card == nullptr)
		return Status(ErrorKind::kNotFound, id);
	return _SaveCard(*card, force);
}


Status
Stack::ReloadCard(const std::string& id)
{
	Card* card = FindCard(id);
	if (card == nullptr)
		return Status(ErrorKind::kNotFound, id);
	StoredDoc stored;
	Status status = fStore->ReadDoc(card->Stored().relPath, DocType::kCard,
		stored);
	if (status)
		status = Card::Validate(stored.doc);
	if (!status)
		return status;
	if (stored.doc.GetString("card_id") != id)
		return Status(ErrorKind::kInvalidFormat, "card_id changed");
	*card = Card(std::move(stored));
	_SortOrder();
	return Status::Ok();
}


Status
Stack::ChangeCardTemplate(const std::string& id, const std::string& templateId)
{
	Card* card = FindCard(id);
	if (card == nullptr)
		return Status(ErrorKind::kNotFound, id);
	if (FindTemplate(templateId) == nullptr)
		return Status(ErrorKind::kNotFound, templateId);
	std::string previous = card->TemplateId();
	if (previous == templateId)
		return Status::Ok();
	card->SetTemplateId(templateId);
	if (IsProvisional(id))
		return Status::Ok();	// in memory until a value is entered
	Status status = _SaveCard(*card, false);
	if (!status) {
		card->SetTemplateId(previous);
		return status;
	}
	fLogger.Info("card.template_change", {{"card", id}, {"from", previous},
		{"to", templateId}});
	return Status::Ok();
}


// #pragma mark - media


Status
Stack::SetMedia(const std::string& cardId, const std::string& partId,
	const std::string& kind, const std::string& ext, InStream& source)
{
	Card* card = FindCard(cardId);
	if (card == nullptr)
		return Status(ErrorKind::kNotFound, cardId);
	if (!IsValidMediaKind(kind) || !IsValidExtension(ext))
		return Status(ErrorKind::kInvalidArgument, "media");

	Json ref = Json::MakeObject();
	ref.Set("media", NewId());
	ref.Set("kind", kind);
	ref.Set("ext", ext);
	uint64_t bytes = 0;
	Status status = fStore->WriteMedia(MediaRelPath(ref), source, bytes);
	if (!status) {
		fLogger.Error("media.add.failed", {{"card", cardId}, {"part", partId},
			{"err", status.Code()}});
		return status;
	}

	const Json* previous = card->Value(partId);
	bool replace = previous != nullptr && IsMediaRef(*previous);
	Json original = previous != nullptr ? *previous : Json();
	bool hadValue = previous != nullptr;
	card->SetValue(partId, ref);
	status = IsProvisional(cardId) ? _CommitProvisional()
		: _SaveCard(*card, false);
	if (!status) {
		if (hadValue)
			card->SetValue(partId, original);
		else
			card->Values().Remove(partId);
		return status;
	}
	fLogger.Info(replace ? "media.replace" : "media.add", {{"card", cardId},
		{"part", partId}, {"kind", kind}, {"media", ref.GetString("media")},
		{"ext", ext}, {"bytes", int64_t(bytes)}});
	return Status::Ok();
}


Status
Stack::ClearMedia(const std::string& cardId, const std::string& partId)
{
	Card* card = FindCard(cardId);
	if (card == nullptr)
		return Status(ErrorKind::kNotFound, cardId);
	const Json* previous = card->Value(partId);
	Json original = previous != nullptr ? *previous : Json();
	bool hadValue = previous != nullptr;
	card->SetValue(partId, Json());
	Status status = IsProvisional(cardId) ? CommitIfChanged(cardId)
		: _SaveCard(*card, false);
	if (!status) {
		if (hadValue)
			card->SetValue(partId, original);
		else
			card->Values().Remove(partId);
		return status;
	}
	LogFields fields = {{"card", cardId}, {"part", partId}};
	if (IsMediaRef(original)) {
		fields.push_back({"kind", original.GetString("kind")});
		fields.push_back({"media", original.GetString("media")});
		fields.push_back({"ext", original.GetString("ext")});
	}
	fLogger.Info("media.clear", fields);
	return Status::Ok();
}


std::string
Stack::MediaPath(const Json& mediaRef) const
{
	std::string relPath = MediaRelPath(mediaRef);
	if (relPath.empty())
		return "";
	return fStore->MediaPath(relPath);
}


Status
Stack::AddMedia(const std::string& kind, const std::string& ext,
	InStream& source, bool reuse, const std::string& templateId,
	const std::string& partId, Json& outRef)
{
	if (!IsValidMediaKind(kind) || !IsValidExtension(ext))
		return Status(ErrorKind::kInvalidArgument, "media");

	std::string mediaId = NewId();
	Json ref = MakeMediaRef(mediaId, kind, ext);
	uint64_t bytes = 0;
	if (reuse) {
		// Small files only (icons): read, then look for the same content
		// (theme spec 8.3).
		std::string data;
		char buffer[16 * 1024];
		while (true) {
			ssize_t count = source.Read(buffer, sizeof(buffer));
			if (count < 0)
				return Status(ErrorKind::kReadFailed, "media");
			if (count == 0)
				break;
			data.append(buffer, size_t(count));
			if (data.size() > kMaxIconBytes)
				return Status(ErrorKind::kLimitExceeded, "media");
		}
		std::vector<std::string> existing;
		fStore->ListMedia(existing);
		std::string prefix = "media/" + kind + "/";
		std::string suffix = "." + ext;
		for (const std::string& relPath : existing) {
			if (relPath.compare(0, prefix.size(), prefix) != 0
				|| relPath.size() != prefix.size() + 26 + suffix.size()
				|| relPath.compare(relPath.size() - suffix.size(),
					suffix.size(), suffix) != 0) {
				continue;
			}
			std::string id = relPath.substr(prefix.size(), 26);
			if (!IsValidId(id))
				continue;
			std::string other;
			if (fStore->ReadSmallFile(relPath, data.size(), other)
				&& other == data) {
				outRef = MakeMediaRef(id, kind, ext);
				return Status::Ok();
			}
		}
		StringInStream stream(data);
		Status status = fStore->WriteMedia(MediaRelPath(ref), stream, bytes);
		if (!status) {
			fLogger.Error("media.add.failed", {{"template", templateId},
				{"part", partId}, {"err", status.Code()}});
			return status;
		}
	} else {
		Status status = fStore->WriteMedia(MediaRelPath(ref), source, bytes);
		if (!status) {
			fLogger.Error("media.add.failed", {{"template", templateId},
				{"part", partId}, {"err", status.Code()}});
			return status;
		}
	}
	fLogger.Info("media.add", {{"template", templateId}, {"part", partId},
		{"kind", kind}, {"media", mediaId}, {"ext", ext},
		{"bytes", int64_t(bytes)}});
	outRef = ref;
	return Status::Ok();
}


// #pragma mark - provisional card (8.6)


Status
Stack::CreateProvisionalCard(const std::string& templateId,
	const std::string& afterId, std::string* outId)
{
	std::unique_ptr<Card> card(new Card(Card::Create(templateId, 0)));
	// The ID must not collide with a card that exists.
	while (fCards.find(card->Id()) != fCards.end())
		card->Doc().Set("card_id", NewId());
	fProvisional = std::move(card);
	fProvisionalAfter = afterId;
	if (outId != nullptr)
		*outId = fProvisional->Id();
	return Status::Ok();
}


bool
Stack::IsProvisional(const std::string& id) const
{
	return fProvisional != nullptr && !id.empty() && fProvisional->Id() == id;
}


std::string
Stack::ProvisionalId() const
{
	return fProvisional != nullptr ? fProvisional->Id() : std::string();
}


void
Stack::DiscardProvisional()
{
	// No file, no log entry (8.6).
	fProvisional.reset();
	fProvisionalAfter.clear();
}


Status
Stack::_CommitProvisional()
{
	int afterIndex = fProvisionalAfter.empty() ? -1
		: IndexOf(fProvisionalAfter);
	int64_t seq = _SeqForInsert(afterIndex);
	if (seq == INT64_MIN) {
		_Renumber();
		afterIndex = fProvisionalAfter.empty() ? -1
			: IndexOf(fProvisionalAfter);
		seq = _SeqForInsert(afterIndex);
		if (seq == INT64_MIN)
			return Status(ErrorKind::kWriteFailed, "renumber");
	}
	Card* card = fProvisional.get();
	card->SetSeq(seq);
	card->Doc().Set("rev", 1);
	std::string now = NowTimestamp();
	card->Doc().Set("created", now);
	card->Doc().Set("modified", now);
	std::string templateId = card->TemplateId();
	Status status = _SaveNewCard(*card);
	if (!status) {
		// Keep it provisional (and its values) for a retry (UI 11.2).
		return status;
	}
	std::string id = card->Id();
	_AdoptCard(std::move(fProvisional));
	fProvisionalAfter.clear();
	fLogger.Info("card.create", {{"card", id}, {"template", templateId}});
	return Status::Ok();
}


Status
Stack::CommitIfChanged(const std::string& id, bool* outCommitted)
{
	if (outCommitted != nullptr)
		*outCommitted = false;
	if (!IsProvisional(id))
		return Status(ErrorKind::kNotFound, id);
	const Template* tmpl = FindTemplate(fProvisional->TemplateId());
	bool changed = tmpl != nullptr
		? DiffersFromDefaults(*fProvisional, *tmpl)
		: !fProvisional->Values().Object().empty();
	if (!changed)
		return Status::Ok();
	Status status = _CommitProvisional();
	if (status && outCommitted != nullptr)
		*outCommitted = true;
	return status;
}


// #pragma mark - themes


void
Stack::_CheckThemes()
{
	for (const auto& entry : fTemplates) {
		const Json* theme = entry.second->Doc().Find("theme");
		if (theme == nullptr)
			continue;	// the default theme always exists
		Theme found;
		if (!FindTheme(theme->AsString(), found)) {
			_AddProblem("theme_missing", entry.second->Stored().relPath,
				theme->AsString());
		}
	}
}


Status
Stack::FindTheme(const std::string& themeId, Theme& out) const
{
	if (fThemes != nullptr)
		return fThemes->Find(themeId, fStore.get(), out);
	ThemeLibrary none;
	return none.Find(themeId, fStore.get(), out);
}


Theme
Stack::ThemeFor(const Template* tmpl) const
{
	Theme theme;
	if (tmpl != nullptr && FindTheme(tmpl->ThemeId(), theme))
		return theme;
	return Theme::Default();
}


Status
Stack::ApplyTheme(const std::string& templateId, const std::string& themeId,
	bool overwrite)
{
	Template* tmpl = FindTemplate(templateId);
	if (tmpl == nullptr)
		return Status(ErrorKind::kNotFound, templateId);
	if (!IsValidThemeId(themeId))
		return Status(ErrorKind::kInvalidArgument, "theme_id");
	Status status;
	if (fThemes != nullptr)
		status = fThemes->CopyToStack(themeId, *fStore, overwrite);
	else {
		ThemeLibrary none;
		status = none.CopyToStack(themeId, *fStore, overwrite);
	}
	if (!status && status.Kind() != ErrorKind::kNotFound)
		return status;
	if (!status) {
		// Not found outside: fine when the stack has its own copy.
		Theme theme;
		if (!FindTheme(themeId, theme))
			return status;
	}
	tmpl->SetThemeId(themeId);
	fLogger.Info("theme.apply", {{"template", templateId},
		{"theme", themeId}});
	return Status::Ok();
}


// #pragma mark - cleanup


// "media/<kind>/<media_id>.<ext>" with a valid ID and extension.
static bool
IsManagedMediaPath(const std::string& relPath)
{
	size_t slash = relPath.rfind('/');
	if (slash == std::string::npos)
		return false;
	std::string name = relPath.substr(slash + 1);
	size_t dot = name.find('.');
	if (dot == std::string::npos)
		return false;
	return IsValidId(name.substr(0, dot))
		&& IsValidExtension(name.substr(dot + 1));
}


Status
Stack::Cleanup(bool execute, CleanupReport& report)
{
	report = CleanupReport();

	std::vector<std::pair<Card*, std::vector<std::string>>> orphanCards;
	for (Card* card : fOrder) {
		Template* tmpl = FindTemplate(card->TemplateId());
		if (tmpl == nullptr)
			continue;	// cannot tell; keep everything (8.2)
		std::vector<std::string> keys = OrphanKeys(*card, *tmpl);
		if (keys.empty())
			continue;
		report.orphanCards++;
		report.orphanKeys += int(keys.size());
		orphanCards.push_back({card, keys});
	}

	// Media referenced from card values (orphans included) and templates.
	bool templateProblems = false;
	for (const Problem& problem : fProblems) {
		if (problem.relPath.compare(0, 10, "templates/") == 0)
			templateProblems = true;
	}
	if (fUnreadableCards > 0 || templateProblems)
		report.mediaSkipped = true;
	else {
		std::set<std::string> referenced;
		for (const Card* card : fOrder)
			CollectMediaRefs(card->Doc(), referenced);
		for (const auto& entry : fTemplates)
			CollectMediaRefs(entry.second->Doc(), referenced);
		std::vector<std::string> media;
		fStore->ListMedia(media);
		for (const std::string& relPath : media) {
			if (!IsManagedMediaPath(relPath))
				continue;
			if (referenced.find(relPath) == referenced.end())
				report.unusedMediaPaths.push_back(relPath);
		}
		report.unusedMedia = int(report.unusedMediaPaths.size());
	}

	fStore->ListTempFiles(report.tempFilePaths);
	report.tempFiles = int(report.tempFilePaths.size());

	if (!execute)
		return Status::Ok();

	BulkSync bulk(*fStore);
	for (auto& entry : orphanCards) {
		Card* card = entry.first;
		Json original = card->Values();
		for (const std::string& key : entry.second)
			card->Values().Remove(key);
		if (!_SaveCard(*card, false)) {
			card->Values() = original;
			report.failed++;
		}
	}
	int mediaDeleted = 0;
	for (const std::string& relPath : report.unusedMediaPaths) {
		if (fStore->RemoveFile(relPath))
			mediaDeleted++;
		else
			report.failed++;
	}
	int tempDeleted = 0;
	for (const std::string& relPath : report.tempFilePaths) {
		if (fStore->RemoveFile(relPath))
			tempDeleted++;
		else
			report.failed++;
	}
	fLogger.Info("stack.cleanup", {
		{"orphan_cards", int64_t(report.orphanCards)},
		{"orphan_keys", int64_t(report.orphanKeys)},
		{"media_deleted", int64_t(mediaDeleted)},
		{"tmp_deleted", int64_t(tempDeleted)}});
	return Status::Ok();
}


}	// namespace bdb
