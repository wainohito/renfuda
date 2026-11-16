/*
 * BDB card database - core unit tests
 * Covers the Phase 1 acceptance tests (spec v0.2, chapter 18). Test 15
 * (100,000 cards) is the separate bench program.
 */
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#include "Csv.h"
#include "Ids.h"
#include "Json.h"
#include "Link.h"
#include "Pdf.h"
#include "Theme.h"
#include "Limits.h"
#include "Log.h"
#include "Model.h"
#include "Preset.h"
#include "Query.h"
#include "Stack.h"
#include "Store.h"
#include "Text.h"

#include "Test.h"


using namespace bdb;


// #pragma mark - helpers


static std::string
TempDir()
{
	const char* base = getenv("TMPDIR");
	std::string pattern = std::string(base != nullptr ? base : "/tmp")
		+ "/bdbtest-XXXXXX";
	std::vector<char> buffer(pattern.begin(), pattern.end());
	buffer.push_back('\0');
	if (mkdtemp(buffer.data()) == nullptr) {
		perror("mkdtemp");
		abort();
	}
	return buffer.data();
}


static std::string
ReadFile(const std::string& path)
{
	std::ifstream stream(path, std::ios::binary);
	std::stringstream buffer;
	buffer << stream.rdbuf();
	return buffer.str();
}


static void
WriteFile(const std::string& path, const std::string& data)
{
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	stream << data;
}


static bool
FileExists(const std::string& path)
{
	struct stat st;
	return lstat(path.c_str(), &st) == 0;
}


static void
ListRecursive(const std::string& root, const std::string& rel,
	std::vector<std::string>& out)
{
	DIR* dir = opendir((root + "/" + rel).c_str());
	if (dir == nullptr)
		return;
	std::vector<std::string> names;
	while (struct dirent* entry = readdir(dir)) {
		std::string name = entry->d_name;
		if (name != "." && name != "..")
			names.push_back(name);
	}
	closedir(dir);
	for (const std::string& name : names) {
		std::string child = rel.empty() ? name : rel + "/" + name;
		struct stat st;
		lstat((root + "/" + child).c_str(), &st);
		if (S_ISDIR(st.st_mode))
			ListRecursive(root, child, out);
		else
			out.push_back(child);
	}
}


// Path -> content of every file below root.
static std::map<std::string, std::string>
Snapshot(const std::string& root)
{
	std::vector<std::string> files;
	ListRecursive(root, "", files);
	std::map<std::string, std::string> result;
	for (const std::string& file : files)
		result[file] = ReadFile(root + "/" + file);
	return result;
}


static std::vector<std::string>
FilesIn(const std::string& dir)
{
	std::vector<std::string> files;
	ListRecursive(dir, "", files);
	std::sort(files.begin(), files.end());
	return files;
}


static std::string
NewStack(const std::string& dir, LogSink* sink = nullptr,
	const std::string& title = "テスト")
{
	std::string manifest;
	Status status = CreateEmptyStack(dir + "/S.rfdf", title, sink, &manifest);
	if (!status) {
		printf("CreateEmptyStack: %s %s\n", status.Code(),
			status.Detail().c_str());
		abort();
	}
	// Most tests work on one card. An empty stack has none (19.1), so
	// add one (without logging it).
	std::unique_ptr<Stack> stack;
	if (!Stack::Open(manifest, nullptr, stack)
		|| !stack->CreateCard(stack->DefaultTemplateId(), -1, nullptr)) {
		printf("NewStack: cannot add the first card\n");
		abort();
	}
	return manifest;
}


// An empty stack exactly as CreateEmptyStack makes it.
static std::string
NewEmptyStack(const std::string& dir, LogSink* sink = nullptr,
	const NewStackOptions* options = nullptr)
{
	std::string manifest;
	Status status = CreateEmptyStack(dir + "/S.rfdf", "テスト", sink,
		&manifest, options);
	if (!status) {
		printf("CreateEmptyStack: %s %s\n", status.Code(),
			status.Detail().c_str());
		abort();
	}
	return manifest;
}


static std::unique_ptr<Stack>
OpenStack(const std::string& path, LogSink* sink = nullptr)
{
	std::unique_ptr<Stack> stack;
	Status status = Stack::Open(path, sink, stack);
	if (!status) {
		printf("Open: %s %s\n", status.Code(), status.Detail().c_str());
		return nullptr;
	}
	return stack;
}


static Template*
FirstTemplate(Stack& stack)
{
	return stack.FindTemplate(stack.DefaultTemplateId());
}


static std::string
AddPart(Stack& stack, const std::string& kind, const std::string& name,
	int y = 16)
{
	Template* tmpl = FirstTemplate(stack);
	std::string id = tmpl->NewPartId(kind);
	tmpl->Parts().push_back(part::Make(kind, id, name, {16, y, 200, 24}));
	Status status = stack.SaveTemplate(tmpl->Id());
	if (!status)
		printf("SaveTemplate: %s\n", status.Code());
	return id;
}


static std::string
RootOf(const std::string& manifestPath)
{
	return manifestPath.substr(0, manifestPath.rfind('/'));
}


static std::string
StackDir(const std::string& dir)
{
	return dir + "/S.rfdf";
}


// #pragma mark - JSON


TEST("json: parse and dump keep key order")
{
	Json doc;
	REQUIRE(Json::Parse("{\"z\":1,\"a\":[1,2,{\"k\":null}],\"m\":\"x\"}",
		doc));
	CHECK_EQ(doc.Object()[0].first, std::string("z"));
	CHECK_EQ(doc.Object()[1].first, std::string("a"));
	Json again;
	REQUIRE(Json::Parse(doc.Dump(), again));
	CHECK(again == doc);
}


TEST("json: duplicate keys are rejected (5.6)")
{
	Json doc;
	Status status = Json::Parse("{\"a\":1,\"a\":2}", doc);
	CHECK(!status);
	CHECK(status.Kind() == ErrorKind::kInvalidFormat);
}


TEST("json: nesting depth limit (5.6)")
{
	std::string deep;
	for (int i = 0; i < 40; i++)
		deep += "[";
	for (int i = 0; i < 40; i++)
		deep += "]";
	Json doc;
	Status status = Json::Parse(deep, doc, kMaxJsonDepth);
	CHECK(status.Kind() == ErrorKind::kLimitExceeded);
	std::string ok;
	for (int i = 0; i < 32; i++)
		ok += "[";
	for (int i = 0; i < 32; i++)
		ok += "]";
	CHECK(Json::Parse(ok, doc, kMaxJsonDepth).IsOk());
}


TEST("json: strings, escapes and UTF-8")
{
	Json doc;
	REQUIRE(Json::Parse("\"a\\n\\u3042\\ud83d\\ude00\\\"\"", doc));
	CHECK_EQ(doc.AsString(), std::string("a\nあ\xf0\x9f\x98\x80\""));
	CHECK(!Json::Parse("\"\xff\"", doc));		// invalid UTF-8
	CHECK(!Json::Parse("\"a\nb\"", doc));		// raw control character
	Json text("日本語\t\x01");
	CHECK_EQ(text.Dump(false), std::string("\"日本語\\t\\u0001\""));
}


TEST("json: leading keys are bdb and format_version (5.2)")
{
	std::string dir = TempDir();
	std::string manifest = NewStack(dir);
	std::string text = ReadFile(manifest);
	std::string prefix
		= "{\n  \"bdb\": \"manifest\",\n  \"format_version\": 1,\n";
	CHECK(text.compare(0, prefix.size(), prefix) == 0);
	for (const std::string& file : FilesIn(StackDir(dir))) {
		std::string content = ReadFile(StackDir(dir) + "/" + file);
		CHECK(content.compare(0, 11, "{\n  \"bdb\": ") == 0);
		CHECK(content.find("\"format_version\": 1") < 60);
		CHECK(content.back() == '\n');
		CHECK(content.find('\r') == std::string::npos);
	}
}


// #pragma mark - 1. round trip


TEST("18.1 round trip")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	REQUIRE(stack != nullptr);
	std::string text = AddPart(*stack, kKindText, "タイトル");
	std::string number = AddPart(*stack, kKindNumber, "価格", 48);
	Card* card = stack->CardAt(0);
	REQUIRE(card != nullptr);
	card->SetValue(text, "朝の散歩");
	card->SetValue(number, 1200);
	REQUIRE(stack->SaveCard(card->Id()));
	std::string cardId = card->Id();
	Json cardDoc = card->Doc();
	Json templateDoc = FirstTemplate(*stack)->Doc();
	Json manifestDoc = stack->GetManifest().Doc();
	stack.reset();

	std::unique_ptr<Stack> reopened = OpenStack(manifestPath);
	REQUIRE(reopened != nullptr);
	CHECK(reopened->Problems().empty());
	Card* again = reopened->FindCard(cardId);
	REQUIRE(again != nullptr);
	CHECK(again->Doc() == cardDoc);
	CHECK(FirstTemplate(*reopened)->Doc() == templateDoc);
	CHECK(reopened->GetManifest().Doc() == manifestDoc);
	CHECK_EQ(reopened->TitleOf(*again), std::string("朝の散歩"));
}


// #pragma mark - 2. unknown keys


TEST("18.2 unknown keys survive load and save")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::string root = RootOf(manifestPath);
	{
		std::unique_ptr<Stack> stack = OpenStack(manifestPath);
		REQUIRE(stack != nullptr);
		AddPart(*stack, kKindText, "名前");
	}
	// Inject unknown keys into every document and into a part.
	for (const std::string& file : FilesIn(root)) {
		std::string path = root + "/" + file;
		Json doc;
		REQUIRE(Json::Parse(ReadFile(path), doc));
		doc.Set("x_future", Json("keep me"));
		if (doc.GetString("bdb") == "template") {
			Json& part = doc.Find("parts")->Array()[0];
			part.Set("x_part_future", Json(42));
			Json unknownPart = Json::MakeObject();
			unknownPart.Set("id", "f_zzzzzz");
			unknownPart.Set("name", "将来");
			unknownPart.Set("kind", "date");
			JsonArray rect = {Json(0), Json(0), Json(10), Json(10)};
			unknownPart.Set("rect", Json(rect));
			unknownPart.Set("x_opts", Json("opaque"));
			doc.Find("parts")->Array().push_back(unknownPart);
		}
		if (doc.GetString("bdb") == "card")
			doc.Find("values")->Set("f_zzzzzz", Json("2026-10-14"));
		WriteFile(path, doc.Dump());
	}
	{
		std::unique_ptr<Stack> stack = OpenStack(manifestPath);
		REQUIRE(stack != nullptr);
		CHECK(stack->Problems().empty());
		Card* card = stack->CardAt(0);
		card->SetValue(FirstTemplate(*stack)->Parts()[0].GetString("id"),
			"値");
		REQUIRE(stack->SaveCard(card->Id()));
		REQUIRE(stack->SaveTemplate(FirstTemplate(*stack)->Id()));
		REQUIRE(stack->SetTitle("新しい名前"));
		// Unknown kind values are opaque, never orphans.
		CHECK(OrphanKeys(*card, *FirstTemplate(*stack)).empty());
	}
	for (const std::string& file : FilesIn(root)) {
		std::string content = ReadFile(root + "/" + file);
		CHECK(content.find("\"x_future\": \"keep me\"") != std::string::npos);
		if (content.find("\"bdb\": \"template\"") != std::string::npos) {
			CHECK(content.find("\"x_part_future\": 42") != std::string::npos);
			CHECK(content.find("\"kind\": \"date\"") != std::string::npos);
			CHECK(content.find("\"x_opts\": \"opaque\"") != std::string::npos);
		}
		if (content.find("\"bdb\": \"card\"") != std::string::npos)
			CHECK(content.find("2026-10-14") != std::string::npos);
	}
}


// #pragma mark - 3. effective value


TEST("18.3 effective value: absent, null, wrong type")
{
	Json tmplDoc = Template::Create("t", 512, 342).Doc();
	StoredDoc stored;
	stored.doc = tmplDoc;
	Template tmpl(stored);
	Json text = part::Make(kKindText, "f_aaaaaa", "a", {0, 0, 10, 10});
	text.Set("default", "既定");
	Json number = part::Make(kKindNumber, "f_bbbbbb", "b", {0, 0, 10, 10});
	number.Set("default", 5);
	Card card = Card::Create(tmpl.Id(), 1024);

	// absent -> default
	CHECK_EQ(EffectiveValue(card, text).AsString(), std::string("既定"));
	CHECK_EQ(EffectiveValue(card, number).AsInt(), int64_t(5));
	// null -> null for number (valid), default for text (wrong type)
	card.SetValue("f_bbbbbb", Json());
	card.SetValue("f_aaaaaa", Json());
	CHECK(EffectiveValue(card, number).IsNull());
	CHECK_EQ(EffectiveValue(card, text).AsString(), std::string("既定"));
	// wrong type -> default
	card.SetValue("f_bbbbbb", "abc");
	card.SetValue("f_aaaaaa", 12);
	CHECK_EQ(EffectiveValue(card, number).AsInt(), int64_t(5));
	CHECK_EQ(EffectiveValue(card, text).AsString(), std::string("既定"));
	// present and right type
	card.SetValue("f_aaaaaa", "値");
	CHECK_EQ(EffectiveValue(card, text).AsString(), std::string("値"));
}


TEST("switch effects lock and hide parts (extension)")
{
	StoredDoc stored;
	stored.doc = Template::Create("t", 512, 342).Doc();
	Template tmpl(stored);
	Json lockAll = part::Make(kKindBool, "f_sw0001", "lock", {0, 0, 40, 20});
	part::SetSwitchEffect(lockAll, "lock_card", {});
	Json hide = part::Make(kKindBool, "f_sw0002", "hide", {0, 30, 40, 20});
	part::SetSwitchEffect(hide, "hide", {"f_txt001", "l_lab001", "f_sw0002"});
	Json lock = part::Make(kKindBool, "f_sw0003", "lock2", {0, 60, 40, 20});
	part::SetSwitchEffect(lock, "lock", {"f_num001"});
	Json text = part::Make(kKindText, "f_txt001", "t", {50, 0, 80, 20});
	Json number = part::Make(kKindNumber, "f_num001", "n", {50, 30, 80, 20});
	Json label = part::Make(kKindLabel, "l_lab001", "", {50, 60, 80, 20});
	for (const Json* p : { &lockAll, &hide, &lock, &text, &number, &label })
		tmpl.Parts().push_back(*p);
	std::set<std::string> ids;
	for (const Json& p : tmpl.Parts())
		CHECK(part::Validate(p, ids));
	CHECK_EQ(part::SwitchEffect(hide), std::string("hide"));
	CHECK_EQ(part::SwitchTargets(hide).size(), size_t(3));
	CHECK_EQ(part::SwitchEffect(text), std::string(""));

	Card card = Card::Create(tmpl.Id(), 1024);
	SwitchState none = ComputeSwitchState(tmpl, &card);
	CHECK(!none.cardLocked && none.locked.empty() && none.hidden.empty());
	CHECK(ComputeSwitchState(tmpl, nullptr).hidden.empty());

	card.SetValue("f_sw0002", true);
	card.SetValue("f_sw0003", true);
	SwitchState state = ComputeSwitchState(tmpl, &card);
	CHECK(!state.cardLocked);
	CHECK(state.hidden.count("f_txt001") == 1);
	CHECK(state.hidden.count("l_lab001") == 1);
	CHECK(state.hidden.count("f_sw0002") == 0);	// never itself
	CHECK(state.locked.count("f_num001") == 1);
	CHECK(state.locked.count("f_txt001") == 0);

	card.SetValue("f_sw0001", true);
	state = ComputeSwitchState(tmpl, &card);
	CHECK(state.cardLocked);
	CHECK(state.locked.count("f_sw0001") == 0);	// can unlock
	CHECK(state.locked.count("f_txt001") == 1);
	CHECK(state.locked.count("f_sw0003") == 1);

	// Removing the effect; invalid shapes.
	part::SetSwitchEffect(hide, "", {});
	CHECK(hide.Find("effect") == nullptr);
	Json bad = text;
	bad.Set("effect", Json::MakeObject());
	std::set<std::string> other;
	CHECK(!part::Validate(bad, other));
	Json badType = lock;
	badType.Find("effect")->Set("type", "explode");
	other.clear();
	CHECK(!part::Validate(badType, other));
}


TEST("button actions and media (extension)")
{
	CHECK(IsKnownAction("card.pdf"));
	CHECK(IsKnownAction("card.image"));
	CHECK(IsKnownAction("sound.play"));
	CHECK(IsKnownAction("video.play"));
	Json button = part::Make(kKindButton, "b_btn001", "", {0, 0, 60, 28});
	button.Set("action", "sound.play");
	CHECK(part::ButtonMedia(button) == nullptr);
	button.Set("media", MakeMediaRef(NewId(), "audio", "ogg"));
	CHECK(part::ButtonMedia(button) != nullptr);
	std::set<std::string> ids;
	CHECK(part::Validate(button, ids));
	button.Set("media", MakeMediaRef(NewId(), "image", "png"));
	ids.clear();
	CHECK(!part::Validate(button, ids));
	Json text = part::Make(kKindText, "f_txt002", "t", {0, 0, 60, 20});
	text.Set("media", MakeMediaRef(NewId(), "audio", "ogg"));
	ids.clear();
	CHECK(!part::Validate(text, ids));
}


// #pragma mark - 4./5. orphans and cleanup


TEST("18.4 orphans are kept, revived and cleaned up")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	REQUIRE(stack != nullptr);
	std::string a = AddPart(*stack, kKindText, "A");
	std::string b = AddPart(*stack, kKindText, "B", 48);
	Card* card = stack->CardAt(0);
	card->SetValue(a, "aaa");
	card->SetValue(b, "bbb");
	REQUIRE(stack->SaveCard(card->Id()));
	std::string cardId = card->Id();

	// Remove part B.
	Template* tmpl = FirstTemplate(*stack);
	Json removed = *tmpl->FindPart(b);
	JsonArray& parts = tmpl->Parts();
	parts.erase(std::remove_if(parts.begin(), parts.end(),
		[&](const Json& p) { return part::Id(p) == b; }), parts.end());
	REQUIRE(stack->SaveTemplate(tmpl->Id()));
	stack.reset();

	stack = OpenStack(manifestPath);
	card = stack->FindCard(cardId);
	CHECK(card->Value(b) != nullptr);
	CHECK_EQ(OrphanKeys(*card, *FirstTemplate(*stack)).size(), size_t(1));
	// Saving the card keeps the orphan.
	card->SetValue(a, "aaa2");
	REQUIRE(stack->SaveCard(cardId));
	stack.reset();
	stack = OpenStack(manifestPath);
	card = stack->FindCard(cardId);
	CHECK_EQ(card->Value(b)->AsString(), std::string("bbb"));

	// Put the part back: the value is effective again.
	FirstTemplate(*stack)->Parts().push_back(removed);
	REQUIRE(stack->SaveTemplate(FirstTemplate(*stack)->Id()));
	CHECK_EQ(EffectiveValue(*card, removed).AsString(), std::string("bbb"));
	CHECK(OrphanKeys(*card, *FirstTemplate(*stack)).empty());

	// Remove again and clean up.
	tmpl = FirstTemplate(*stack);
	tmpl->Parts().pop_back();
	REQUIRE(stack->SaveTemplate(tmpl->Id()));
	CleanupReport report;
	REQUIRE(stack->Cleanup(true, report));
	CHECK_EQ(report.orphanCards, 1);
	CHECK_EQ(report.orphanKeys, 1);
	stack.reset();
	stack = OpenStack(manifestPath);
	card = stack->FindCard(cardId);
	CHECK(card->Value(b) == nullptr);
	CHECK_EQ(card->Value(a)->AsString(), std::string("aaa2"));
}


TEST("18.5 cleanup dry run changes nothing")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::string root = RootOf(manifestPath);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	Card* card = stack->CardAt(0);
	card->SetValue("f_gone00", "orphan");
	REQUIRE(stack->SaveCard(card->Id()));
	MakeDirectories(root + "/media/image");
	WriteFile(root + "/media/image/" + NewId() + ".png", "x");
	WriteFile(root + "/cards/left.rfdd.tmp", "partial");

	std::map<std::string, std::string> before = Snapshot(root);
	CleanupReport report;
	REQUIRE(stack->Cleanup(false, report));
	CHECK_EQ(report.orphanCards, 1);
	CHECK_EQ(report.unusedMedia, 1);
	CHECK_EQ(report.tempFiles, 1);
	CHECK(Snapshot(root) == before);
}


// #pragma mark - 6. file names


TEST("18.6 file names (chapter 10)")
{
	CHECK_EQ(SanitizeTitle("a/b:c*d?e\"f<g>h|i\\j"),
		std::string("a_b_c_d_e_f_g_h_i_j"));
	CHECK_EQ(SanitizeTitle("tab\there\x7f"), std::string("tab_here_"));
	CHECK_EQ(SanitizeTitle(""), std::string("無題"));
	CHECK_EQ(SanitizeTitle("  ..  "), std::string("無題"));
	CHECK_EQ(SanitizeTitle(" .名前. "), std::string("名前"));
	CHECK_EQ(SanitizeTitle("\xe3\x80\x80全角空白\xe3\x80\x80"),
		std::string("全角空白"));
	CHECK_EQ(SanitizeTitle("朝の散歩"), std::string("朝の散歩"));

	std::string longAscii(100, 'a');
	CHECK_EQ(SanitizeTitle(longAscii), std::string(40, 'a'));
	// 40 code points of 3 bytes = 120 bytes: fits exactly.
	std::string kana;
	for (int i = 0; i < 50; i++)
		kana += "あ";
	std::string cut = SanitizeTitle(kana);
	CHECK_EQ(CountCodePoints(cut), size_t(40));
	CHECK_EQ(cut.size(), size_t(120));
	// 4 byte characters: the byte limit wins (30 * 4 = 120).
	std::string emoji;
	for (int i = 0; i < 40; i++)
		emoji += "\xf0\x9f\x98\x80";
	cut = SanitizeTitle(emoji);
	CHECK_EQ(cut.size(), size_t(120));
	CHECK_EQ(CountCodePoints(cut), size_t(30));
	// Never cuts in the middle of a sequence.
	std::string mixed = "a" + kana;
	cut = SanitizeTitle(mixed);
	CHECK_EQ(CountCodePoints(cut), size_t(40));
	CHECK_EQ(cut.size(), size_t(118));
	Json check;
	CHECK(Json::Parse("\"" + cut + "\"", check).IsOk());

	CHECK_EQ(MakeFileName("日記", "abcdefghijklmnopqrstuvwxyz", ".rfdd"),
		std::string("日記-abcdef.rfdd"));
}


// #pragma mark - 7. IDs


TEST("18.7 IDs: format and duplicate resolution")
{
	for (int i = 0; i < 100; i++)
		CHECK(IsValidId(NewId()));
	CHECK(!IsValidId("ABCDEFGHIJKLMNOPQRSTUVWXYZ"));
	CHECK(!IsValidId("abcdefghijklmnopqrstuvwxy1"));
	CHECK(!IsValidId("abc"));
	CHECK(!IsValidId("../../../../etc/passwd/xyz"));

	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::string root = RootOf(manifestPath);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	Card* card = stack->CardAt(0);
	std::string cardId = card->Id();
	std::string relPath = card->Stored().relPath;
	stack.reset();

	// A second file with the same card_id and a higher rev.
	Json doc;
	REQUIRE(Json::Parse(ReadFile(root + "/" + relPath), doc));
	doc.Set("rev", 5);
	doc.Set("x_marker", "winner");
	WriteFile(root + "/cards/copy-xxxxxx.rfdd", doc.Dump());

	stack = OpenStack(manifestPath);
	REQUIRE(stack != nullptr);
	CHECK_EQ(stack->CardCount(), 1);
	CHECK_EQ(stack->FindCard(cardId)->Doc().GetString("x_marker"),
		std::string("winner"));
	REQUIRE(stack->Problems().size() == 1);
	CHECK_EQ(stack->Problems()[0].reason, std::string("duplicate_id"));
	CHECK_EQ(stack->Problems()[0].relPath, relPath);
	// The losing file is not deleted.
	CHECK(FileExists(root + "/" + relPath));
}


// #pragma mark - 8. atomic save


TEST("18.8 atomic save: a failed write keeps the original")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::string root = RootOf(manifestPath);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	std::string text = AddPart(*stack, kKindText, "T");
	Card* card = stack->CardAt(0);
	card->SetValue(text, "original");
	REQUIRE(stack->SaveCard(card->Id()));
	std::string relPath = card->Stored().relPath;
	std::string before = ReadFile(root + "/" + relPath);
	int64_t rev = card->Rev();

	FolderStore& store = static_cast<FolderStore&>(stack->Store());
	store.faults.failWrite = true;
	card->SetValue(text, "original");	// same title -> same name
	card->SetValue("f_other0", "changed");
	Status status = stack->SaveCard(card->Id());
	CHECK(status.Kind() == ErrorKind::kWriteFailed);
	store.faults.failWrite = false;
	CHECK_EQ(ReadFile(root + "/" + relPath), before);
	CHECK(!FileExists(root + "/" + relPath + ".tmp"));
	CHECK_EQ(card->Rev(), rev);	// rev rolled back
	// The value stays in memory for a retry (UI 11.2).
	CHECK_EQ(card->Value("f_other0")->AsString(), std::string("changed"));
	CHECK(stack->SaveCard(card->Id()).IsOk());
	CHECK_EQ(card->Rev(), rev + 1);
}


// #pragma mark - 9. rename


TEST("18.9 title change renames the file, rename failure is not an error")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::string root = RootOf(manifestPath);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	std::string text = AddPart(*stack, kKindText, "T");
	Card* card = stack->CardAt(0);
	std::string id6 = card->Id().substr(0, 6);
	CHECK_EQ(card->Stored().relPath, "cards/無題-" + id6 + ".rfdd");

	card->SetValue(text, "買い物リスト\n2行目");
	REQUIRE(stack->SaveCard(card->Id()));
	CHECK_EQ(card->Stored().relPath, "cards/買い物リスト-" + id6 + ".rfdd");
	CHECK(FileExists(root + "/cards/買い物リスト-" + id6 + ".rfdd"));
	CHECK(!FileExists(root + "/cards/無題-" + id6 + ".rfdd"));

	FolderStore& store = static_cast<FolderStore&>(stack->Store());
	store.faults.failTitleRename = true;
	card->SetValue(text, "別の名前");
	CHECK(stack->SaveCard(card->Id()).IsOk());
	CHECK_EQ(card->Stored().relPath, "cards/買い物リスト-" + id6 + ".rfdd");
	Json doc;
	REQUIRE(Json::Parse(ReadFile(root + "/" + card->Stored().relPath), doc));
	CHECK_EQ(doc.Find("values")->GetString(text), std::string("別の名前"));
	store.faults.failTitleRename = false;
	// The next save catches up.
	CHECK(stack->SaveCard(card->Id()).IsOk());
	CHECK_EQ(card->Stored().relPath, "cards/別の名前-" + id6 + ".rfdd");
}


// #pragma mark - 10. conflict


TEST("18.10 external change makes the save fail with a conflict")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::string root = RootOf(manifestPath);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	Card* card = stack->CardAt(0);
	std::string path = root + "/" + card->Stored().relPath;

	Json doc;
	REQUIRE(Json::Parse(ReadFile(path), doc));
	doc.Set("x_external", "edit with more bytes");
	WriteFile(path, doc.Dump());
	std::string external = ReadFile(path);

	card->SetValue("f_aaaaaa", "mine");
	Status status = stack->SaveCard(card->Id());
	CHECK(status.Kind() == ErrorKind::kConflict);
	CHECK_EQ(ReadFile(path), external);
	CHECK(!FileExists(path + ".tmp"));

	// Overwrite ...
	CHECK(stack->SaveCard(card->Id(), true).IsOk());
	REQUIRE(Json::Parse(ReadFile(path), doc));
	CHECK(doc.Find("x_external") == nullptr);
	// ... or reload.
	REQUIRE(Json::Parse(ReadFile(path), doc));
	doc.Set("x_external", "second edit");
	WriteFile(path, doc.Dump());
	CHECK(stack->SaveCard(card->Id()).Kind() == ErrorKind::kConflict);
	REQUIRE(stack->ReloadCard(card->Id()));
	CHECK_EQ(stack->FindCard(card->Id())->Doc().GetString("x_external"),
		std::string("second edit"));
	CHECK(stack->SaveCard(card->Id()).IsOk());

	// Conflict detected between writing the tmp and committing.
	card = stack->CardAt(0);
	FolderStore& store = static_cast<FolderStore&>(stack->Store());
	store.faults.beforeCommit = [&]() { WriteFile(path, "{}  "); };
	CHECK(stack->SaveCard(card->Id()).Kind() == ErrorKind::kConflict);
	store.faults.beforeCommit = nullptr;
}


// #pragma mark - 11. broken files


TEST("18.11 broken files go to the problem list")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::string root = RootOf(manifestPath);
	WriteFile(root + "/cards/broken-aaaaaa.rfdd", "{\"bdb\": \"card\", ");
	WriteFile(root + "/cards/dupkey-bbbbbb.rfdd",
		"{\"bdb\":\"card\",\"bdb\":\"card\"}");
	WriteFile(root + "/cards/big-cccccc.rfdd",
		std::string(kMaxCardBytes + 1, ' '));
	WriteFile(root + "/templates/bad-dddddd.rfdt", "[]");
	WriteFile(root + "/cards/ignored.txt", "not a card");
	WriteFile(root + "/cards/.hidden.rfdd", "{");
	WriteFile(root + "/cards/x.rfdd.tmp", "{");

	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	REQUIRE(stack != nullptr);
	CHECK_EQ(stack->CardCount(), 1);
	CHECK_EQ(stack->Problems().size(), size_t(4));
	std::map<std::string, std::string> reasons;
	for (const Problem& problem : stack->Problems())
		reasons[problem.relPath] = problem.reason;
	CHECK_EQ(reasons["cards/broken-aaaaaa.rfdd"],
		std::string("invalid_format"));
	CHECK_EQ(reasons["cards/dupkey-bbbbbb.rfdd"],
		std::string("invalid_format"));
	CHECK_EQ(reasons["cards/big-cccccc.rfdd"],
		std::string("limit_exceeded"));
	CHECK_EQ(reasons["templates/bad-dddddd.rfdt"],
		std::string("invalid_format"));
}


// #pragma mark - 12./13. versions and reserved encryption


TEST("18.12 newer format_version is not opened")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	Json doc;
	REQUIRE(Json::Parse(ReadFile(manifestPath), doc));
	doc.Set("format_version", 2);
	WriteFile(manifestPath, doc.Dump());
	std::unique_ptr<Stack> stack;
	Status status = Stack::Open(manifestPath, nullptr, stack);
	CHECK(status.Kind() == ErrorKind::kNewerVersion);

	// A newer card inside a current stack is skipped, not overwritten.
	doc.Set("format_version", 1);
	WriteFile(manifestPath, doc.Dump());
	std::string root = RootOf(manifestPath);
	std::string cardFile = FilesIn(root + "/cards")[0];
	Json card;
	REQUIRE(Json::Parse(ReadFile(root + "/cards/" + cardFile), card));
	card.Set("format_version", 7);
	WriteFile(root + "/cards/" + cardFile, card.Dump());
	stack = OpenStack(manifestPath);
	REQUIRE(stack != nullptr);
	CHECK_EQ(stack->CardCount(), 0);
	REQUIRE(stack->Problems().size() == 1);
	CHECK_EQ(stack->Problems()[0].reason, std::string("newer_version"));
}


TEST("18.13 reserved encryption is unsupported")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::string root = RootOf(manifestPath);
	WriteFile(root + "/cards/secret-eeeeee.rfdd", "BDBE\x01\x02\x03");
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	REQUIRE(stack != nullptr);
	REQUIRE(stack->Problems().size() == 1);
	CHECK_EQ(stack->Problems()[0].reason, std::string("encrypted"));
	stack.reset();

	Json doc;
	REQUIRE(Json::Parse(ReadFile(manifestPath), doc));
	Json encryption = Json::MakeObject();
	encryption.Set("scheme", "x");
	doc.Set("encryption", encryption);
	WriteFile(manifestPath, doc.Dump());
	Status status = Stack::Open(manifestPath, nullptr, stack);
	CHECK(status.Kind() == ErrorKind::kUnsupported);
}


// #pragma mark - 14. paths


TEST("18.14 hostile IDs and ext never leave the stack folder")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::string root = RootOf(manifestPath);
	WriteFile(dir + "/outside.png", "precious");

	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	std::string image = AddPart(*stack, kKindImage, "画像");
	Card* card = stack->CardAt(0);
	Json evil = Json::MakeObject();
	evil.Set("media", "../../outside");
	evil.Set("kind", "image");
	evil.Set("ext", "png");
	card->SetValue(image, evil);
	Json evil2 = Json::MakeObject();
	evil2.Set("media", NewId());
	evil2.Set("kind", "../..");
	evil2.Set("ext", "png/../../x");
	card->SetValue("f_other0", evil2);
	REQUIRE(stack->SaveCard(card->Id()));
	CHECK(!IsMediaRef(evil));
	CHECK(!IsMediaRef(evil2));
	CHECK(stack->MediaPath(evil).empty());
	CHECK(stack->MediaPath(evil2).empty());
	const Json* imagePart = FirstTemplate(*stack)->FindPart(image);
	CHECK(EffectiveValue(*card, *imagePart).IsNull());

	// A card file whose card_id contains a path.
	WriteFile(root + "/cards/evil-ffffff.rfdd",
		"{\"bdb\":\"card\",\"format_version\":1,\"card_id\":\"../../outside\","
		"\"template_id\":\"" + stack->DefaultTemplateId() + "\",\"seq\":1,"
		"\"rev\":1,\"values\":{}}");
	stack.reset();
	stack = OpenStack(manifestPath);
	REQUIRE(stack != nullptr);
	CHECK_EQ(stack->CardCount(), 1);
	CleanupReport report;
	REQUIRE(stack->Cleanup(true, report));
	CHECK_EQ(ReadFile(dir + "/outside.png"), std::string("precious"));
	StringOutStream sink;
	CHECK(stack->Store().ReadMedia("../outside.png", sink).Kind()
		== ErrorKind::kInvalidArgument);
}


// #pragma mark - 16. numbers


TEST("18.16 number values round trip")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	std::string number = AddPart(*stack, kKindNumber, "数");
	const double values[] = { 0, -1, 0.1, 1e-6, 123456789012345.0, 2.5e-300,
		-0.000123, 1e15 };
	std::vector<std::string> ids;
	for (double value : values) {
		std::string id;
		REQUIRE(stack->CreateCard(stack->DefaultTemplateId(), -1, &id));
		Card* card = stack->FindCard(id);
		card->SetValue(number, MakeNumberValue(value));
		REQUIRE(stack->SaveCard(id));
		ids.push_back(id);
	}
	std::string root = RootOf(manifestPath);
	std::string text = ReadFile(root + "/"
		+ stack->FindCard(ids[4])->Stored().relPath);
	CHECK(text.find("123456789012345") != std::string::npos);
	CHECK(text.find("123456789012345.") == std::string::npos);
	stack.reset();

	stack = OpenStack(manifestPath);
	const Json* part = FirstTemplate(*stack)->FindPart(number);
	for (size_t i = 0; i < ids.size(); i++) {
		const Json& value = EffectiveValue(*stack->FindCard(ids[i]), *part);
		CHECK(value.IsNumber());
		CHECK(value.AsDouble() == values[i]);
	}
	CHECK_EQ(FormatNumber(0.1), std::string("0.1"));
	CHECK_EQ(FormatNumber(1e-6), std::string("1e-06"));
	CHECK_EQ(FormatNumber(-1), std::string("-1"));
	CHECK_EQ(FormatNumber(1200), std::string("1200"));

	// Over the limit: not a valid number value.
	CHECK(!ValueMatchesKind(Json(1e16), kKindNumber));
	CHECK(!ValueMatchesKind(Json(int64_t(1000000000000001)), kKindNumber));
	CHECK(ValueMatchesKind(Json(int64_t(1000000000000000)), kKindNumber));
	Card* card = stack->FindCard(ids[0]);
	card->SetValue(number, Json(1e16));
	CHECK(EffectiveValue(*card, *part).IsNull());	// default
	CHECK_EQ(OrphanKeys(*card, *FirstTemplate(*stack)).size(), size_t(1));
}


// #pragma mark - 17. part IDs


TEST("18.17 generated part IDs")
{
	std::set<std::string> existing;
	for (int i = 0; i < 2000; i++) {
		std::string id = NewPartId(i % 2 == 0 ? 'l' : 'f', existing);
		CHECK(IsValidPartId(id));
		CHECK_EQ(id.size(), size_t(8));
		CHECK(id.compare(0, 2, i % 2 == 0 ? "l_" : "f_") == 0);
		CHECK(existing.insert(id).second);
	}
	CHECK(!IsValidPartId("F_abc"));
	CHECK(!IsValidPartId("1abc"));
	CHECK(!IsValidPartId(std::string(33, 'a')));
}


// #pragma mark - 18./19./20. blank, kind change, conversion


TEST("18.18 blank parts are kept; assigning a type keeps id/name/rect")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	std::string blank = AddPart(*stack, kKindBlank, "未定");
	Card* card = stack->CardAt(0);
	std::string cardId = card->Id();
	Json cardBefore = card->Doc();
	stack.reset();

	stack = OpenStack(manifestPath);
	Template* tmpl = FirstTemplate(*stack);
	const Json* part = tmpl->FindPart(blank);
	REQUIRE(part != nullptr);
	CHECK_EQ(part::Kind(*part), std::string("blank"));
	CHECK(part->Find("default") == nullptr);
	Rect rect = part::GetRect(*part);
	REQUIRE(stack->ChangePartKind(tmpl->Id(), blank, kKindNumber));
	REQUIRE(stack->SaveTemplate(tmpl->Id()));
	part = tmpl->FindPart(blank);
	CHECK_EQ(part::Kind(*part), std::string("number"));
	CHECK_EQ(part::Name(*part), std::string("未定"));
	Rect after = part::GetRect(*part);
	CHECK(after.x == rect.x && after.y == rect.y && after.w == rect.w
		&& after.h == rect.h);
	CHECK(part->Find("decimals") != nullptr);
	CHECK(stack->FindCard(cardId)->Doc() == cardBefore);
}


TEST("18.19 kind change keeps values as orphans and revives them")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	std::string text = AddPart(*stack, kKindText, "メモ");
	Template* tmpl = FirstTemplate(*stack);
	Card* card = stack->CardAt(0);
	card->SetValue(text, "abc");
	REQUIRE(stack->SaveCard(card->Id()));

	REQUIRE(stack->ChangePartKind(tmpl->Id(), text, kKindNumber));
	REQUIRE(stack->SaveTemplate(tmpl->Id()));
	const Json* part = tmpl->FindPart(text);
	CHECK(HasMismatchedValue(*card, *part));
	CHECK(EffectiveValue(*card, *part).IsNull());
	stack.reset();
	stack = OpenStack(manifestPath);
	tmpl = FirstTemplate(*stack);
	card = stack->CardAt(0);
	CHECK_EQ(card->Value(text)->AsString(), std::string("abc"));

	REQUIRE(stack->ChangePartKind(tmpl->Id(), text, kKindText));
	REQUIRE(stack->SaveTemplate(tmpl->Id()));
	CHECK_EQ(EffectiveValue(*card, *tmpl->FindPart(text)).AsString(),
		std::string("abc"));
	// Changing the kind drops the old kind's keys (20.1).
	REQUIRE(stack->ChangePartKind(tmpl->Id(), text, kKindImage));
	CHECK(tmpl->FindPart(text)->Find("multiline") == nullptr);
	CHECK_EQ(tmpl->FindPart(text)->GetString("fit"), std::string("contain"));
	CHECK(!stack->ChangePartKind(tmpl->Id(), text, kKindLabel));
}


TEST("18.20 value conversion: dry run and execute")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	std::string field = AddPart(*stack, kKindText, "価格");
	std::string templateId = stack->DefaultTemplateId();
	const char* inputs[] = { " 120 ", "-3.5", "1,000", "１２", "abc", "",
		"0123", "+7", "1e3" };
	std::vector<std::string> ids;
	for (const char* input : inputs) {
		std::string id;
		REQUIRE(stack->CreateCard(templateId, -1, &id));
		stack->FindCard(id)->SetValue(field, input);
		REQUIRE(stack->SaveCard(id));
		ids.push_back(id);
	}
	std::string root = RootOf(manifestPath);
	REQUIRE(stack->ChangePartKind(templateId, field, kKindNumber));
	REQUIRE(stack->SaveTemplate(templateId));

	std::map<std::string, std::string> before = Snapshot(root);
	ConvertReport report;
	REQUIRE(stack->ConvertValues(templateId, field, kKindText, kKindNumber,
		false, report));
	CHECK(Snapshot(root) == before);
	CHECK_EQ(report.cards, 10);		// 9 + the initial empty card
	CHECK_EQ(report.convertible, 6);	// 120, -3.5, "", 0123, +7, 1e3
	CHECK_EQ(report.notConvertible, 3);	// 1,000 / full width / abc

	REQUIRE(stack->ConvertValues(templateId, field, kKindText, kKindNumber,
		true, report));
	CHECK_EQ(report.converted, 6);
	CHECK(stack->FindCard(ids[0])->Value(field)->AsInt() == 120);
	CHECK(stack->FindCard(ids[1])->Value(field)->AsDouble() == -3.5);
	CHECK_EQ(stack->FindCard(ids[2])->Value(field)->AsString(),
		std::string("1,000"));
	CHECK(stack->FindCard(ids[5])->Value(field)->IsNull());
	CHECK(stack->FindCard(ids[6])->Value(field)->AsInt() == 123);
	CHECK(stack->FindCard(ids[8])->Value(field)->AsInt() == 1000);

	// And back to text.
	REQUIRE(stack->ChangePartKind(templateId, field, kKindText));
	REQUIRE(stack->ConvertValues(templateId, field, kKindNumber, kKindText,
		true, report));
	CHECK_EQ(stack->FindCard(ids[1])->Value(field)->AsString(),
		std::string("-3.5"));
	CHECK_EQ(stack->FindCard(ids[5])->Value(field)->AsString(),
		std::string(""));
	CHECK_EQ(stack->FindCard(ids[6])->Value(field)->AsString(),
		std::string("123"));
}


// #pragma mark - 21./22. presets and empty stacks


TEST("18.21 presets: fresh IDs, references follow, preset untouched")
{
	std::string dir = TempDir();
	// Build a "preset" with two templates and two cards.
	std::string presetManifest = NewStack(dir, nullptr, "住所録");
	std::string presetRoot = RootOf(presetManifest);
	std::string oldStackId, oldTemplate, secondTemplate;
	std::vector<std::string> oldCards;
	{
		std::unique_ptr<Stack> preset = OpenStack(presetManifest);
		oldStackId = preset->Id();
		oldTemplate = preset->DefaultTemplateId();
		std::string name = AddPart(*preset, kKindText, "氏名");
		REQUIRE(preset->CreateTemplate("第二", 400, 300, &secondTemplate));
		REQUIRE(preset->SetDefaultTemplate(secondTemplate));
		std::string id;
		REQUIRE(preset->CreateCard(secondTemplate, -1, &id));
		for (int i = 0; i < preset->CardCount(); i++)
			oldCards.push_back(preset->CardAt(i)->Id());
	}
	std::map<std::string, std::string> presetBefore = Snapshot(presetRoot);

	std::string manifestPath;
	MemoryLogSink sink;
	REQUIRE(CreateStackFromPreset(presetRoot, dir + "/new.rfdf", "我が家",
		"住所録", true, &sink, &manifestPath));
	CHECK(Snapshot(presetRoot) == presetBefore);

	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	REQUIRE(stack != nullptr);
	CHECK(stack->Problems().empty());
	CHECK(stack->Id() != oldStackId);
	CHECK_EQ(stack->Title(), std::string("我が家"));
	CHECK(stack->FindTemplate(oldTemplate) == nullptr);
	CHECK(stack->FindTemplate(secondTemplate) == nullptr);
	CHECK_EQ(stack->Templates().size(), size_t(2));
	CHECK_EQ(stack->CardCount(), 2);
	for (int i = 0; i < stack->CardCount(); i++) {
		Card* card = stack->CardAt(i);
		CHECK(std::find(oldCards.begin(), oldCards.end(), card->Id())
			== oldCards.end());
		CHECK(stack->FindTemplate(card->TemplateId()) != nullptr);
		CHECK_EQ(card->Rev(), int64_t(1));
	}
	Template* defaultTemplate = stack->FindTemplate(
		stack->GetManifest().DefaultTemplate());
	REQUIRE(defaultTemplate != nullptr);
	CHECK_EQ(defaultTemplate->Title(), std::string("第二"));
	CHECK_EQ(defaultTemplate->Rev(), int64_t(1));
	// Part ids are kept.
	for (Template* tmpl : stack->Templates()) {
		if (tmpl->Title() == "無題")
			CHECK_EQ(tmpl->Parts().size(), size_t(1));
	}
	REQUIRE(sink.Lines().size() == 1);
	CHECK(sink.Lines()[0].find("\"ev\":\"stack.create\"") != std::string::npos);
	CHECK(sink.Lines()[0].find("\"preset\":\"住所録\"") != std::string::npos);

	// Existing destination is refused.
	CHECK(CreateStackFromPreset(presetRoot, dir + "/new.rfdf", "x", "x", true,
		nullptr, nullptr).Kind() == ErrorKind::kExists);
}


TEST("18.22 empty stack")
{
	std::string dir = TempDir();
	MemoryLogSink sink;
	std::string manifestPath = NewEmptyStack(dir, &sink);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	REQUIRE(stack != nullptr);
	CHECK_EQ(stack->Templates().size(), size_t(1));
	Template* tmpl = FirstTemplate(*stack);
	CHECK_EQ(tmpl->Title(), std::string("無題"));
	CHECK_EQ(tmpl->Width(), 512);
	CHECK_EQ(tmpl->Height(), 342);
	CHECK(tmpl->Parts().empty());
	CHECK(tmpl->Doc().Find("paper") == nullptr);
	// No cards (v0.4, 19.1); cards/ exists and is empty.
	CHECK_EQ(stack->CardCount(), 0);
	CHECK(FilesIn(RootOf(manifestPath) + "/cards").empty());
	CHECK_EQ(stack->GetManifest().DefaultTemplate(), tmpl->Id());
	CHECK(sink.Lines()[0].find("\"preset\":null") != std::string::npos);
	// No staging folder is left behind.
	for (const std::string& file : FilesIn(dir))
		CHECK(file.find(".rfd-new-") == std::string::npos);
}


// #pragma mark - 23. media


TEST("18.23 media: kind folders, orphan references, unknown folders")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::string root = RootOf(manifestPath);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	std::string image = AddPart(*stack, kKindImage, "写真");
	Card* card = stack->CardAt(0);

	WriteFile(dir + "/photo.png", "\x89PNG fake");
	FileInStream first(dir + "/photo.png");
	REQUIRE(stack->SetMedia(card->Id(), image, "image", "png", first));
	const Json* ref = card->Value(image);
	REQUIRE(ref != nullptr && IsMediaRef(*ref));
	std::string firstPath = MediaRelPath(*ref);
	CHECK(firstPath.compare(0, 12, "media/image/") == 0);
	CHECK(FileExists(root + "/" + firstPath));
	StringOutStream read;
	REQUIRE(stack->Store().ReadMedia(firstPath, read));
	CHECK_EQ(read.data, std::string("\x89PNG fake"));

	// Replace: the old file stays until cleanup.
	FileInStream second(dir + "/photo.png");
	REQUIRE(stack->SetMedia(card->Id(), image, "image", "png", second));
	std::string secondPath = MediaRelPath(*card->Value(image));
	CHECK(FileExists(root + "/" + firstPath));

	// Move the current image into an orphan (remove the part).
	Template* tmpl = FirstTemplate(*stack);
	tmpl->Parts().clear();
	REQUIRE(stack->SaveTemplate(tmpl->Id()));

	MakeDirectories(root + "/media/other");
	WriteFile(root + "/media/other/" + NewId() + ".png", "keep");
	WriteFile(root + "/media/image/notes.txt", "keep");

	CleanupReport report;
	REQUIRE(stack->Cleanup(false, report));
	CHECK_EQ(report.unusedMedia, 1);
	REQUIRE(report.unusedMediaPaths.size() == 1);
	CHECK_EQ(report.unusedMediaPaths[0], firstPath);
	REQUIRE(stack->Cleanup(true, report));
	CHECK(!FileExists(root + "/" + firstPath));
	// The orphan's reference protected the second file during this run.
	CHECK(FileExists(root + "/" + secondPath));
	CHECK(FileExists(root + "/media/image/notes.txt"));
	CHECK_EQ(FilesIn(root + "/media/other").size(), size_t(1));
}


// #pragma mark - 24. recursive load


TEST("18.24 cards in sub folders are loaded and saved in place")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::string root = RootOf(manifestPath);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	std::string text = AddPart(*stack, kKindText, "T");
	Card* card = stack->CardAt(0);
	std::string id = card->Id();
	std::string oldPath = card->Stored().relPath;
	stack.reset();

	MakeDirectories(root + "/cards/001/a");
	std::string name = oldPath.substr(oldPath.rfind('/') + 1);
	rename((root + "/" + oldPath).c_str(),
		(root + "/cards/001/a/" + name).c_str());
	// Depth 3 is out of range.
	MakeDirectories(root + "/cards/1/2/3");
	Card deep = Card::Create(NewId(), 1);
	WriteFile(root + "/cards/1/2/3/deep.rfdd", deep.Doc().Dump());

	stack = OpenStack(manifestPath);
	REQUIRE(stack != nullptr);
	REQUIRE(stack->CardCount() == 1);
	card = stack->FindCard(id);
	REQUIRE(card != nullptr);
	CHECK_EQ(card->Stored().relPath, "cards/001/a/" + name);
	card->SetValue(text, "新しい題");
	REQUIRE(stack->SaveCard(id));
	CHECK_EQ(card->Stored().relPath,
		"cards/001/a/新しい題-" + id.substr(0, 6) + ".rfdd");

	std::string newId;
	REQUIRE(stack->CreateCard(stack->DefaultTemplateId(), -1, &newId));
	CHECK_EQ(stack->FindCard(newId)->Stored().relPath,
		"cards/無題-" + newId.substr(0, 6) + ".rfdd");
}


// #pragma mark - seq


TEST("8.4 seq: append, insert between, renumber")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	std::string templateId = stack->DefaultTemplateId();
	std::string first = stack->CardAt(0)->Id();
	std::string last;
	REQUIRE(stack->CreateCard(templateId, -1, &last));
	CHECK_EQ(stack->FindCard(last)->Seq(), int64_t(2048));
	std::string middle;
	REQUIRE(stack->CreateCard(templateId, 0, &middle));
	CHECK_EQ(stack->FindCard(middle)->Seq(), int64_t(1536));
	CHECK_EQ(stack->IndexOf(middle), 1);
	// Fill the gap until renumbering is needed.
	for (int i = 0; i < 12; i++) {
		std::string id;
		REQUIRE(stack->CreateCard(templateId, 0, &id));
		CHECK_EQ(stack->IndexOf(id), 1);
	}
	CHECK_EQ(stack->CardCount(), 15);
	CHECK_EQ(stack->CardAt(0)->Id(), first);
	CHECK_EQ(stack->CardAt(14)->Id(), last);
	for (int i = 1; i < stack->CardCount(); i++)
		CHECK(stack->CardAt(i - 1)->Seq() < stack->CardAt(i)->Seq());
	// Order survives a reload.
	std::vector<std::string> order;
	for (int i = 0; i < stack->CardCount(); i++)
		order.push_back(stack->CardAt(i)->Id());
	stack.reset();
	stack = OpenStack(manifestPath);
	for (int i = 0; i < stack->CardCount(); i++)
		CHECK_EQ(stack->CardAt(i)->Id(), order[i]);
}


// #pragma mark - 25.-29. log


static void
CheckLines(const std::vector<std::string>& lines)
{
	for (const std::string& line : lines) {
		Json doc;
		CHECK(Json::Parse(line, doc).IsOk());
		CHECK(doc.Has("t") && doc.Has("level") && doc.Has("ev"));
		CHECK(line.find('\n') == std::string::npos);
	}
}


static int
CountEvent(const std::vector<std::string>& lines, const std::string& event)
{
	int count = 0;
	for (const std::string& line : lines) {
		if (line.find("\"ev\":\"" + event + "\"") != std::string::npos)
			count++;
	}
	return count;
}


TEST("18.25 log: one event per operation, each line is JSON")
{
	std::string dir = TempDir();
	MemoryLogSink sink;
	std::string manifestPath = NewStack(dir, &sink);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath, &sink);
	std::string templateId = stack->DefaultTemplateId();
	std::string id, copy;
	REQUIRE(stack->CreateCard(templateId, -1, &id));
	REQUIRE(stack->DuplicateCard(id, &copy));
	stack->FindCard(id)->SetValue("f_aaaaaa", "x");
	REQUIRE(stack->SaveCard(id));
	REQUIRE(stack->DeleteCard(copy));
	REQUIRE(stack->SaveTemplate(templateId));
	CleanupReport report;
	REQUIRE(stack->Cleanup(true, report));

	std::vector<std::string> lines = sink.Lines();
	CheckLines(lines);
	CHECK_EQ(CountEvent(lines, "stack.create"), 1);
	CHECK_EQ(CountEvent(lines, "stack.open"), 1);
	CHECK_EQ(CountEvent(lines, "card.create"), 1);
	CHECK_EQ(CountEvent(lines, "card.duplicate"), 1);
	CHECK_EQ(CountEvent(lines, "card.save"), 2);	// save + cleanup rewrite
	CHECK_EQ(CountEvent(lines, "card.delete"), 1);
	CHECK_EQ(CountEvent(lines, "template.save"), 1);
	CHECK_EQ(CountEvent(lines, "stack.cleanup"), 1);
	for (const std::string& line : lines) {
		if (line.find("card.create") != std::string::npos) {
			CHECK(line.find("\"card\":\"" + id + "\"") != std::string::npos);
			CHECK(line.find("\"stack\":\"" + stack->Id() + "\"")
				!= std::string::npos);
		}
	}
}


TEST("18.26 log never contains values, titles, names or search strings")
{
	const char* secrets[] = { "SECRET-VALUE-東京都", "SECRET-NAME-氏名",
		"SECRET-TITLE-スタック", "SECRET-SEARCH-検索", "SECRET-TEMPLATE" };
	std::string dir = TempDir();
	MemoryLogSink sink;
	std::string manifestPath = NewStack(dir, &sink, secrets[2]);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath, &sink);
	std::string templateId = stack->DefaultTemplateId();
	Template* tmpl = stack->FindTemplate(templateId);
	tmpl->SetTitle(secrets[4]);
	std::string field = AddPart(*stack, kKindText, secrets[1]);
	std::string number = AddPart(*stack, kKindNumber, "n", 48);
	Card* card = stack->CardAt(0);
	card->SetValue(field, secrets[0]);
	card->SetValue(number, 4242);
	REQUIRE(stack->SaveCard(card->Id()));
	std::string copy;
	REQUIRE(stack->DuplicateCard(card->Id(), &copy));
	REQUIRE(stack->ChangePartKind(templateId, field, kKindNumber));
	ConvertReport convert;
	REQUIRE(stack->ConvertValues(templateId, field, kKindText, kKindNumber,
		true, convert));
	REQUIRE(stack->ChangePartKind(templateId, field, kKindText));
	REQUIRE(stack->SaveTemplate(templateId));
	MemoryCardQuery query;
	std::vector<std::string> found;
	REQUIRE(query.Find(*stack, {{field, secrets[3]}}, found));
	REQUIRE(stack->SetTitle(std::string(secrets[2]) + "2"));
	CleanupReport report;
	REQUIRE(stack->Cleanup(true, report));

	std::vector<std::string> lines = sink.Lines();
	CHECK(lines.size() > 8);
	for (const std::string& line : lines) {
		for (const char* secret : secrets)
			CHECK(line.find(secret) == std::string::npos);
		CHECK(line.find("4242") == std::string::npos);
	}
}


TEST("18.27 operations succeed when the log cannot be written")
{
	std::string dir = TempDir();
	MemoryLogSink failing;
	failing.SetFailing(true);
	std::string manifestPath = NewStack(dir, &failing);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath, &failing);
	REQUIRE(stack != nullptr);
	std::string id;
	CHECK(stack->CreateCard(stack->DefaultTemplateId(), -1, &id).IsOk());
	CHECK(stack->SaveCard(id).IsOk());

	// A file sink pointing at a non-writable location.
	WriteFile(dir + "/blocker", "file, not a folder");
	FileLogSink fileSink(dir + "/blocker/logs");
	int failures = 0;
	fileSink.onFirstFailure = [&]() { failures++; };
	std::unique_ptr<Stack> second = OpenStack(manifestPath, &fileSink);
	REQUIRE(second != nullptr);
	CHECK(second->CreateCard(second->DefaultTemplateId(), -1, &id).IsOk());
	CHECK(second->DeleteCard(id).IsOk());
	CHECK_EQ(failures, 1);	// reported once
}


TEST("18.28 log rotation and retention")
{
	std::string dir = TempDir();
	FileLogSink sink(dir + "/logs");
	sink.SetLimitsForTesting(1000, 1000000);
	std::string stackId = NewId();
	Logger logger(&sink, stackId);
	sink.SetMonthForTesting("2025-01");
	logger.Info("old.event");
	sink.SetMonthForTesting("2026-10");
	for (int i = 0; i < 40; i++)
		logger.Info("card.save", {{"card", NewId()}, {"rev", i}});
	std::vector<std::string> files = FilesIn(dir + "/logs/" + stackId);
	CHECK(files.size() >= 3);
	CHECK(std::find(files.begin(), files.end(), "2026-10.jsonl")
		!= files.end());
	CHECK(std::find(files.begin(), files.end(), "2026-10-2.jsonl")
		!= files.end());
	CHECK(std::find(files.begin(), files.end(), "2025-01.jsonl")
		!= files.end());
	for (const std::string& file : files) {
		struct stat st;
		lstat((dir + "/logs/" + stackId + "/" + file).c_str(), &st);
		CHECK(st.st_size <= 1000);
	}

	// Retention: 2025-01 is more than 12 months before 2026-10.
	sink.PruneForTesting("2026-10");
	files = FilesIn(dir + "/logs/" + stackId);
	CHECK(std::find(files.begin(), files.end(), "2025-01.jsonl")
		== files.end());
	CHECK(std::find(files.begin(), files.end(), "2026-10.jsonl")
		!= files.end());

	// Total size limit: the oldest files go first.
	sink.SetLimitsForTesting(1000, 2500);
	sink.PruneForTesting("2026-10");
	files = FilesIn(dir + "/logs/" + stackId);
	size_t total = 0;
	for (const std::string& file : files) {
		struct stat st;
		lstat((dir + "/logs/" + stackId + "/" + file).c_str(), &st);
		total += size_t(st.st_size);
	}
	CHECK(total <= 2500);
	CHECK(std::find(files.begin(), files.end(), "2026-10.jsonl")
		== files.end());

	// No stack id: _app.
	Logger app(&sink, "");
	app.Info("app.start", {{"version", "0.1"}});
	CHECK(FileExists(dir + "/logs/_app/2026-10.jsonl"));
}


TEST("18.29 concurrent logging does not mix lines")
{
	std::string dir = TempDir();
	FileLogSink sink(dir + "/logs");
	std::string stackId = NewId();
	Logger logger(&sink, stackId);
	std::vector<std::thread> threads;
	for (int t = 0; t < 8; t++) {
		threads.emplace_back([&logger, t]() {
			for (int i = 0; i < 500; i++) {
				logger.Info("card.save", {{"card", NewId()},
					{"thread", t}, {"i", i},
					{"pad", std::string(200, 'x')}});
			}
		});
	}
	for (std::thread& thread : threads)
		thread.join();
	std::string content;
	for (const std::string& file : FilesIn(dir + "/logs/" + stackId))
		content += ReadFile(dir + "/logs/" + stackId + "/" + file);
	std::stringstream stream(content);
	std::string line;
	int count = 0;
	while (std::getline(stream, line)) {
		Json doc;
		CHECK(Json::Parse(line, doc).IsOk());
		count++;
	}
	CHECK_EQ(count, 8 * 500);
}


// #pragma mark - queries


TEST("UI 8 find conditions and quick search")
{
	NumberCondition condition;
	REQUIRE(ParseNumberCondition("10..20", condition));
	CHECK(condition.op == NumberCondition::kRange);
	CHECK(condition.Matches(10) && condition.Matches(20)
		&& !condition.Matches(20.5));
	REQUIRE(ParseNumberCondition(">= 100", condition));
	CHECK(condition.Matches(100) && !condition.Matches(99));
	REQUIRE(ParseNumberCondition("<100", condition));
	CHECK(condition.Matches(99) && !condition.Matches(100));
	REQUIRE(ParseNumberCondition("-1.5", condition));
	CHECK(condition.Matches(-1.5));
	CHECK(!ParseNumberCondition("abc", condition));
	CHECK(!ParseNumberCondition("1..", condition));

	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	std::string name = AddPart(*stack, kKindText, "名前");
	std::string price = AddPart(*stack, kKindNumber, "価格", 48);
	std::string templateId = stack->DefaultTemplateId();
	struct { const char* name; int price; } rows[] = {
		{"Apple りんご", 120}, {"apple pie", 450}, {"Banana", 15},
		{"ＡＰＰＬＥ全角", 18}
	};
	for (const auto& row : rows) {
		std::string id;
		REQUIRE(stack->CreateCard(templateId, -1, &id));
		stack->FindCard(id)->SetValue(name, row.name);
		stack->FindCard(id)->SetValue(price, row.price);
		REQUIRE(stack->SaveCard(id));
	}
	MemoryCardQuery query;
	std::vector<std::string> found;
	REQUIRE(query.Find(*stack, {{name, "APPLE"}}, found));
	CHECK_EQ(found.size(), size_t(2));
	REQUIRE(query.Find(*stack, {{price, "10..20"}}, found));
	CHECK_EQ(found.size(), size_t(2));
	REQUIRE(query.Find(*stack, {{name, "apple"}, {price, ">200"}}, found));
	CHECK_EQ(found.size(), size_t(1));
	REQUIRE(query.Find(*stack, {{name, "ａｐｐｌｅ"}}, found));
	CHECK_EQ(found.size(), size_t(1));
	REQUIRE(query.Find(*stack, {}, found));
	CHECK_EQ(found.size(), size_t(5));
	CHECK(query.Find(*stack, {{price, "lots"}}, found).Kind()
		== ErrorKind::kInvalidArgument);

	CHECK_EQ(QuickSearch(*stack, templateId, "an").size(), size_t(1));
	CHECK_EQ(QuickSearch(*stack, templateId, "").size(), size_t(5));
	CHECK_EQ(QuickSearch(*stack, templateId, "りんご").size(), size_t(1));
}


TEST("UI 8 bool conditions")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	std::string flag = AddPart(*stack, kKindBool, "済");
	std::string tmpl = stack->DefaultTemplateId();
	std::string on, off, none;
	REQUIRE(stack->CreateCard(tmpl, -1, &on));
	REQUIRE(stack->CreateCard(tmpl, -1, &off));
	stack->FindCard(on)->SetValue(flag, true);
	stack->FindCard(off)->SetValue(flag, false);
	none = stack->CardAt(0)->Id();
	MemoryCardQuery query;
	std::vector<std::string> found;
	REQUIRE(query.Find(*stack, {{flag, "on"}}, found));
	REQUIRE(found.size() == 1);
	CHECK_EQ(found[0], on);
	REQUIRE(query.Find(*stack, {{flag, "off"}}, found));
	CHECK_EQ(found.size(), size_t(2));
	REQUIRE(query.Find(*stack, {}, found));
	CHECK_EQ(found.size(), size_t(3));
}


TEST("card titles (8.5)")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	Card* card = stack->CardAt(0);
	CHECK_EQ(stack->TitleOf(*card), std::string("無題"));	// no text part
	std::string number = AddPart(*stack, kKindNumber, "番号");
	std::string text = AddPart(*stack, kKindText, "名前", 48);
	card->SetValue(text, "  一行目  \n二行目");
	card->SetValue(number, 3.25);
	CHECK_EQ(stack->TitleOf(*card), std::string("一行目"));
	FirstTemplate(*stack)->SetTitlePart(number);
	CHECK_EQ(stack->TitleOf(*card), std::string("3.25"));
	FirstTemplate(*stack)->SetTitlePart("f_nothere");
	CHECK_EQ(stack->TitleOf(*card), std::string("一行目"));
}


TEST("templates: create, duplicate, delete -> missing template")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	std::string second;
	REQUIRE(stack->CreateTemplate("住所", 600, 400, &second));
	std::string copy;
	REQUIRE(stack->DuplicateTemplate(second, "住所のコピー", &copy));
	CHECK_EQ(stack->Templates().size(), size_t(3));
	Card* card = stack->CardAt(0);
	REQUIRE(stack->ChangeCardTemplate(card->Id(), copy));
	CHECK_EQ(stack->TemplateUsage(copy), 1);
	REQUIRE(stack->DeleteTemplate(copy));
	CHECK(stack->IsTemplateMissing(*card));
	stack.reset();
	stack = OpenStack(manifestPath);
	REQUIRE(stack->Problems().size() == 1);
	CHECK_EQ(stack->Problems()[0].reason, std::string("missing_template"));
	// Missing template: values are kept and not cleaned.
	card = stack->CardAt(0);
	card->SetValue("f_keep00", "kept");
	REQUIRE(stack->SaveCard(card->Id()));
	CleanupReport report;
	REQUIRE(stack->Cleanup(true, report));
	CHECK_EQ(report.orphanKeys, 0);
	CHECK(stack->CardAt(0)->Value("f_keep00") != nullptr);
}


TEST("bundled presets load and create stacks")
{
	std::vector<PresetInfo> presets;
	REQUIRE(ListPresets("presets/ja", presets));
	CHECK_EQ(presets.size(), size_t(3));
	std::string dir = TempDir();
	for (const PresetInfo& info : presets) {
		CHECK(!info.description.empty());
		std::unique_ptr<Stack> preset = OpenStack(info.path);
		REQUIRE(preset != nullptr);
		CHECK(preset->Problems().empty());
		std::string manifest;
		REQUIRE(CreateStackFromPreset(info.path, dir + "/" + info.title
			+ ".rfdf", info.title, info.title, true, nullptr, &manifest));
		std::unique_ptr<Stack> stack = OpenStack(manifest);
		REQUIRE(stack != nullptr);
		CHECK(stack->Problems().empty());
		CHECK_EQ(stack->CardCount(), 1);
		Template* tmpl = FirstTemplate(*stack);
		REQUIRE(tmpl != nullptr);
		CHECK(tmpl->TitlePart() != nullptr);
		int counters = 0;
		for (const Json& part : tmpl->Parts()) {
			if (part::Kind(part) == kKindCounter)
				counters++;
		}
		CHECK_EQ(counters, 1);
		CHECK(stack->GetManifest().Doc().Find("preset_description")
			== nullptr);
	}
}


// #pragma mark - v0.4 (tests 30-38)


TEST("18.30 bool values round trip; default and style are kept")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	std::string flag = AddPart(*stack, kKindBool, "読了");
	Template* tmpl = FirstTemplate(*stack);
	Json* part = tmpl->FindPart(flag);
	REQUIRE(part != nullptr);
	CHECK_EQ(part->GetString("style"), std::string("switch"));
	CHECK(part->Find("default")->IsBool());
	part::SetBoolLook(*part, "checkbox");
	part->Set("default", Json());
	REQUIRE(stack->SaveTemplate(tmpl->Id()));

	std::string ids[3];
	Json values[3] = { Json(true), Json(false), Json() };
	for (int i = 0; i < 3; i++) {
		REQUIRE(stack->CreateCard(tmpl->Id(), -1, &ids[i]));
		stack->FindCard(ids[i])->SetValue(flag, values[i]);
		REQUIRE(stack->SaveCard(ids[i]));
	}
	stack.reset();
	stack = OpenStack(manifestPath);
	REQUIRE(stack != nullptr);
	CHECK(stack->Problems().empty());
	for (int i = 0; i < 3; i++) {
		const Card* card = stack->FindCard(ids[i]);
		REQUIRE(card != nullptr);
		CHECK(*card->Value(flag) == values[i]);
		const Json* p = FirstTemplate(*stack)->FindPart(flag);
		CHECK(EffectiveValue(*card, *p) == values[i]);
	}
	const Json* p = FirstTemplate(*stack)->FindPart(flag);
	CHECK_EQ(part::BoolLook(*p), std::string("checkbox"));
	CHECK(p->Find("default")->IsNull());
	// A string in a bool part is an orphan.
	Card* card = stack->FindCard(ids[0]);
	card->SetValue(flag, "yes");
	CHECK(EffectiveValue(*card, *p).IsNull());
	CHECK_EQ(OrphanKeys(*card, *FirstTemplate(*stack)).size(), size_t(1));
	// Visual style together with the look.
	Json copy = *p;
	Json& style = part::MutableStyle(copy);
	style.Set("fill", "#ffeedd");
	CHECK_EQ(part::BoolLook(copy), std::string("checkbox"));
	std::set<std::string> partIds;
	CHECK(part::Validate(copy, partIds));
	part::ClearStyle(copy);
	CHECK_EQ(copy.GetString("style"), std::string("checkbox"));
}


TEST("18.31 buttons and counters are kept; unknown actions too")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	Template* tmpl = FirstTemplate(*stack);
	std::string buttonId = tmpl->NewPartId(kKindButton);
	CHECK_EQ(buttonId.compare(0, 2, "b_"), 0);
	Json button = part::Make(kKindButton, buttonId, "", {10, 10, 60, 28});
	CHECK(button.Find("name") == nullptr);
	button.Set("text", "▶");
	button.Set("action", "card.next");
	tmpl->Parts().push_back(button);
	std::string unknownId = tmpl->NewPartId(kKindButton);
	Json unknown = part::Make(kKindButton, unknownId, "", {80, 10, 60, 28});
	unknown.Set("action", "future.thing");
	tmpl->Parts().push_back(unknown);
	std::string counterId = tmpl->NewPartId(kKindCounter);
	CHECK_EQ(counterId.compare(0, 2, "c_"), 0);
	tmpl->Parts().push_back(part::Make(kKindCounter, counterId, "",
		{150, 14, 72, 20}));
	REQUIRE(stack->SaveTemplate(tmpl->Id()));
	stack.reset();
	stack = OpenStack(manifestPath);
	tmpl = FirstTemplate(*stack);
	REQUIRE(tmpl->FindPart(buttonId) != nullptr);
	CHECK_EQ(tmpl->FindPart(buttonId)->GetString("text"), std::string("▶"));
	CHECK_EQ(tmpl->FindPart(buttonId)->GetString("action"),
		std::string("card.next"));
	CHECK_EQ(tmpl->FindPart(unknownId)->GetString("action"),
		std::string("future.thing"));
	CHECK(!IsKnownAction("future.thing"));
	CHECK(IsKnownAction("link.open"));
	CHECK(tmpl->FindPart(counterId) != nullptr);
	CHECK(!KindHasValue(kKindButton) && !KindHasValue(kKindCounter));
}


TEST("18.32 background is kept and validated; its image survives cleanup")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	Template* tmpl = FirstTemplate(*stack);
	std::string data = "fake png";
	StringInStream stream(data);
	Json ref;
	REQUIRE(stack->AddMedia("image", "png", stream, false, tmpl->Id(), "",
		ref));
	Json& background = tmpl->MutableBackground();
	background.Set("color", "#f4f0e8");
	background.Set("image", ref);
	background.Set("fit", "tile");
	REQUIRE(stack->SaveTemplate(tmpl->Id()));
	CleanupReport report;
	REQUIRE(stack->Cleanup(true, report));
	CHECK_EQ(report.unusedMedia, 0);
	CHECK(FileExists(RootOf(manifestPath) + "/" + MediaRelPath(ref)));
	stack.reset();
	stack = OpenStack(manifestPath);
	tmpl = FirstTemplate(*stack);
	REQUIRE(tmpl->Background() != nullptr);
	CHECK_EQ(tmpl->Background()->GetString("color"), std::string("#f4f0e8"));
	CHECK_EQ(tmpl->Background()->GetString("fit"), std::string("tile"));
	CHECK(*tmpl->Background()->Find("image") == ref);
	// Invalid colors and fits make the template invalid.
	Json doc = tmpl->Doc();
	doc.Find("background")->Set("color", "red");
	CHECK(!Template::Validate(doc));
	doc.Find("background")->Set("color", "#12345g");
	CHECK(!Template::Validate(doc));
	doc.Find("background")->Set("color", "#ABCDEF");
	CHECK(Template::Validate(doc));
	doc.Find("background")->Set("fit", "zoom");
	CHECK(!Template::Validate(doc));
}


TEST("18.33 cleanup keeps media while cards are unreadable")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::string root = RootOf(manifestPath);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	std::string partId = AddPart(*stack, kKindText, "名前");
	Card* card = stack->CardAt(0);
	card->SetValue("f_gone00", "orphan");
	REQUIRE(stack->SaveCard(card->Id()));
	MakeDirectories(root + "/media/image");
	std::string unused = "media/image/" + NewId() + ".png";
	WriteFile(root + "/" + unused, "x");
	WriteFile(root + "/cards/broken-aaaaaa.rfdd", "{ not json");
	stack.reset();
	stack = OpenStack(manifestPath);
	CleanupReport report;
	REQUIRE(stack->Cleanup(true, report));
	CHECK(report.mediaSkipped);
	CHECK(FileExists(root + "/" + unused));
	CHECK_EQ(report.orphanKeys, 1);
	CHECK(stack->CardAt(0)->Value("f_gone00") == nullptr);
	(void)partId;
}


TEST("18.34 paper sizes, orientation and custom")
{
	struct {
		const char*	size;
		int32_t		w;
		int32_t		h;
	} expected[] = {
		{"A4", 794, 1123}, {"A5", 559, 794}, {"A6", 397, 559},
		{"B4", 971, 1376}, {"B5", 688, 971}, {"B6", 484, 688},
		{"postcard", 378, 559}
	};
	for (const auto& entry : expected) {
		int32_t w, h;
		REQUIRE(PaperPixels(entry.size, "portrait", w, h));
		CHECK_EQ(w, entry.w);
		CHECK_EQ(h, entry.h);
		REQUIRE(PaperPixels(entry.size, "landscape", w, h));
		CHECK_EQ(w, entry.h);
		CHECK_EQ(h, entry.w);
	}
	int32_t w, h;
	CHECK(!PaperPixels("custom", "portrait", w, h));
	CHECK(!PaperPixels("A3", "portrait", w, h));

	std::string dir = TempDir();
	NewStackOptions options;
	options.paperSize = "A6";
	options.orientation = "landscape";
	std::string manifestPath = NewEmptyStack(dir, nullptr, &options);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	Template* tmpl = FirstTemplate(*stack);
	CHECK_EQ(tmpl->Width(), 559);
	CHECK_EQ(tmpl->Height(), 397);
	CHECK_EQ(tmpl->PaperSize(), std::string("A6"));
	CHECK_EQ(tmpl->PaperOrientation(), std::string("landscape"));
	// Kept across save and load.
	REQUIRE(stack->SaveTemplate(tmpl->Id()));
	stack.reset();
	stack = OpenStack(manifestPath);
	tmpl = FirstTemplate(*stack);
	CHECK_EQ(tmpl->PaperSize(), std::string("A6"));
	Json paper = *tmpl->Doc().Find("paper");
	CHECK_EQ(paper.GetString("size"), std::string("A6"));
	// A size that does not match the paper counts as custom.
	tmpl->SetSize(600, 400);
	CHECK_EQ(tmpl->PaperSize(), std::string("custom"));
	CHECK_EQ(tmpl->Doc().Find("paper")->GetString("size"), std::string("A6"));
	// Without a paper, the card's shape gives the orientation.
	tmpl->ClearPaper();
	CHECK_EQ(tmpl->PaperOrientation(), std::string("landscape"));
	tmpl->SetSize(342, 512);
	CHECK_EQ(tmpl->PaperOrientation(), std::string("portrait"));
	// A custom size takes its orientation from its shape.
	tmpl->SetPaper("custom", "landscape", 600, 794);
	CHECK_EQ(tmpl->PaperOrientation(), std::string("portrait"));
	tmpl->SetPaper("custom", "portrait", 794, 600);
	CHECK_EQ(tmpl->PaperOrientation(), std::string("landscape"));
	tmpl->SetPaper("A6", "landscape", 0, 0);
	// Invalid paper values.
	Json doc = tmpl->Doc();
	doc.Find("paper")->Set("size", "A3");
	CHECK(!Template::Validate(doc));
	doc.Find("paper")->Set("size", "custom");
	doc.Find("paper")->Set("orientation", "sideways");
	CHECK(!Template::Validate(doc));
	CHECK_EQ(MmToPixels(25.4), 96);
	CHECK(std::fabs(PixelsToPoints(512) - 384) < 1e-9);
}


TEST("18.35 part styles are kept; out of range values are invalid")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	std::string text = AddPart(*stack, kKindText, "名前");
	Template* tmpl = FirstTemplate(*stack);
	Json* part = tmpl->FindPart(text);
	Json style;
	REQUIRE(Json::Parse("{\"border\": {\"visible\": true, \"width\": 3, "
		"\"color\": \"#808080\"}, \"fill\": null, \"font\": {\"family\": "
		"\"Noto Sans\", \"style\": \"Bold\", \"size\": 14, \"color\": "
		"\"#102030\"}}", style));
	part->Set("style", style);
	REQUIRE(stack->SaveTemplate(tmpl->Id()));
	stack.reset();
	stack = OpenStack(manifestPath);
	CHECK(*FirstTemplate(*stack)->FindPart(text)->Find("style") == style);

	std::set<std::string> ids;
	Json bad = *FirstTemplate(*stack)->FindPart(text);
	bad.Find("style")->Find("border")->Set("width", 17);
	CHECK(!part::Validate(bad, ids));
	ids.clear();
	bad = *FirstTemplate(*stack)->FindPart(text);
	bad.Find("style")->Find("font")->Set("size", 5);
	CHECK(!part::Validate(bad, ids));
	ids.clear();
	bad.Find("style")->Find("font")->Set("size", 97);
	CHECK(!part::Validate(bad, ids));
	ids.clear();
	bad.Find("style")->Find("font")->Set("size", 96);
	CHECK(part::Validate(bad, ids));
	ids.clear();
	bad.Find("style")->Set("fill", "#zzzzzz");
	CHECK(!part::Validate(bad, ids));
	// A broken style makes the file a problem, others still load.
	Json doc = FirstTemplate(*stack)->Doc();
	doc.Find("parts")->Array()[0].Find("style")->Find("border")->Set("width",
		17);
	CHECK(!Template::Validate(doc));
}


TEST("18.36 locked, icon, link and source are kept with unknown keys")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	std::string url = AddPart(*stack, kKindText, "URL");
	std::string image = AddPart(*stack, kKindImage, "画像", 60);
	Template* tmpl = FirstTemplate(*stack);
	std::string buttonId = tmpl->NewPartId(kKindButton);
	Json button = part::Make(kKindButton, buttonId, "", {300, 10, 80, 28});
	button.Set("action", "link.open");
	Json link = Json::MakeObject();
	link.Set("url", "https://example.com/");
	button.Set("link", link);
	Json icon = MakeMediaRef(NewId(), "image", "hvif");
	icon.Set("size", 24);
	icon.Set("placement", "top");
	icon.Set("x_future", 1);
	button.Set("icon", icon);
	button.Set("locked", true);
	button.Set("x_unknown", "kept");
	tmpl->Parts().push_back(button);
	Json* imagePart = tmpl->FindPart(image);
	Json source = Json::MakeObject();
	source.Set("field", url);
	source.Set("max_edge", 256);
	imagePart->Set("source", source);
	Json fieldLink = Json::MakeObject();
	fieldLink.Set("field", url);
	imagePart->Set("link", fieldLink);
	REQUIRE(stack->SaveTemplate(tmpl->Id()));
	stack.reset();
	stack = OpenStack(manifestPath);
	REQUIRE(stack->Problems().empty());
	tmpl = FirstTemplate(*stack);
	const Json* loaded = tmpl->FindPart(buttonId);
	REQUIRE(loaded != nullptr);
	CHECK(part::IsLocked(*loaded));
	CHECK(*loaded->Find("icon") == icon);
	CHECK(*loaded->Find("link") == link);
	CHECK_EQ(loaded->GetString("x_unknown"), std::string("kept"));
	CHECK(*tmpl->FindPart(image)->Find("source") == source);
	// Icons are media references: cleanup keeps them (13).
	std::set<std::string> refs;
	CollectMediaRefs(tmpl->Doc(), refs);
	CHECK(refs.count(MediaRelPath(icon)) == 1);
	// Keys on the wrong kinds are invalid.
	std::set<std::string> ids;
	Json wrong = *tmpl->FindPart(url);
	wrong.Set("icon", icon);
	CHECK(!part::Validate(wrong, ids));
	ids.clear();
	wrong = *tmpl->FindPart(url);
	wrong.Set("link", link);
	CHECK(!part::Validate(wrong, ids));
	ids.clear();
	wrong = *tmpl->FindPart(url);
	wrong.Set("source", source);
	CHECK(!part::Validate(wrong, ids));
	ids.clear();
	wrong = *loaded;
	wrong.Find("icon")->Set("size", 200);
	CHECK(!part::Validate(wrong, ids));
	ids.clear();
	wrong = *tmpl->FindPart(image);
	wrong.Find("source")->Set("max_edge", 16);
	CHECK(!part::Validate(wrong, ids));
	// Locking survives a kind change; kind specific keys do not.
	Json changing = *tmpl->FindPart(image);
	part::SetLocked(changing, true);
	part::ChangeKind(changing, kKindText);
	CHECK(part::IsLocked(changing));
	CHECK(changing.Find("source") == nullptr);
	CHECK(changing.Find("link") == nullptr);
}


TEST("18.37 link checks (I/O spec 2.2)")
{
	std::string scheme;
	CHECK(CheckUrl("https://example.com/a?b=1#c", &scheme) == LinkCheck::kOk);
	CHECK_EQ(scheme, std::string("https"));
	CHECK(CheckUrl("HTTP://Example.com", &scheme) == LinkCheck::kOk);
	CHECK_EQ(scheme, std::string("http"));
	CHECK(CheckUrl("mailto:someone@example.com") == LinkCheck::kOk);
	CHECK(CheckUrl("file:///etc/passwd", &scheme) == LinkCheck::kScheme);
	CHECK_EQ(scheme, std::string("file"));
	CHECK(CheckUrl("ftp://example.com/") == LinkCheck::kScheme);
	CHECK(CheckUrl("javascript:alert(1)") == LinkCheck::kScheme);
	CHECK(CheckUrl("application/x-vnd.Be-TRAK:x") == LinkCheck::kInvalid);
	CHECK(CheckUrl("https://exa mple.com") == LinkCheck::kInvalid);
	CHECK(CheckUrl("https://example.com/\x01") == LinkCheck::kInvalid);
	CHECK(CheckUrl("https:///path") == LinkCheck::kNoHost);
	CHECK(CheckUrl("https:example.com") == LinkCheck::kNoHost);
	CHECK(CheckUrl("") == LinkCheck::kEmpty);
	CHECK(CheckUrl("https://example.com/" + std::string(2028, 'a'))
		== LinkCheck::kOk);
	CHECK(CheckUrl("https://example.com/" + std::string(2029, 'a'))
		== LinkCheck::kTooLong);

	// Field links follow the value; wrong targets are invalid.
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	std::string url = AddPart(*stack, kKindText, "URL");
	std::string number = AddPart(*stack, kKindNumber, "数", 48);
	Template* tmpl = FirstTemplate(*stack);
	Json label = part::Make(kKindLabel, tmpl->NewPartId(kKindLabel), "",
		{10, 100, 80, 20});
	Json link = Json::MakeObject();
	link.Set("field", url);
	label.Set("link", link);
	Card* card = stack->CardAt(0);
	std::string out;
	CHECK(ResolveLink(label, *tmpl, card, out) == LinkCheck::kEmpty);
	card->SetValue(url, " https://a.example/x ");
	CHECK(ResolveLink(label, *tmpl, card, out) == LinkCheck::kOk);
	CHECK_EQ(out, std::string("https://a.example/x"));
	card->SetValue(url, "https://b.example/");
	CHECK(ResolveLink(label, *tmpl, card, out) == LinkCheck::kOk);
	CHECK_EQ(out, std::string("https://b.example/"));
	card->SetValue(url, "file:///boot");
	CHECK(ResolveLink(label, *tmpl, card, out) == LinkCheck::kScheme);
	link.Set("field", number);
	label.Set("link", link);
	CHECK(ResolveLink(label, *tmpl, card, out) == LinkCheck::kField);
	link.Set("field", "f_nothere");
	label.Set("link", link);
	CHECK(ResolveLink(label, *tmpl, card, out) == LinkCheck::kField);
	Json fixed = Json::MakeObject();
	fixed.Set("url", "https://fixed.example/");
	label.Set("link", fixed);
	CHECK(ResolveLink(label, *tmpl, nullptr, out) == LinkCheck::kOk);
	label.Remove("link");
	CHECK(ResolveLink(label, *tmpl, card, out) == LinkCheck::kNone);
}


TEST("18.38 provisional cards")
{
	std::string dir = TempDir();
	MemoryLogSink sink;
	std::string manifestPath = NewEmptyStack(dir);
	std::string root = RootOf(manifestPath);
	{
		std::unique_ptr<Stack> stack = OpenStack(manifestPath, &sink);
		std::string partId = AddPart(*stack, kKindText, "名前");
		Template* tmpl = FirstTemplate(*stack);
		tmpl->FindPart(partId)->Set("default", "既定");
		REQUIRE(stack->SaveTemplate(tmpl->Id()));
		REQUIRE(stack->CardCount() == 0);
		std::string id;
		REQUIRE(stack->CreateProvisionalCard(tmpl->Id(), "", &id));
		CHECK(stack->IsProvisional(id));
		CHECK(stack->FindCard(id) != nullptr);
		CHECK_EQ(stack->CardCount(), 0);
		CHECK_EQ(stack->IndexOf(id), -1);
		// Unchanged: no file.
		bool committed = true;
		REQUIRE(stack->CommitIfChanged(id, &committed));
		CHECK(!committed);
		CHECK(stack->SaveCard(id));
		CHECK(FilesIn(root + "/cards").empty());
		// Typed and set back to the default: still unchanged.
		stack->FindCard(id)->SetValue(partId, "x");
		stack->FindCard(id)->SetValue(partId, "既定");
		REQUIRE(stack->CommitIfChanged(id, &committed));
		CHECK(!committed);
		CHECK(FilesIn(root + "/cards").empty());
		stack->DiscardProvisional();
		CHECK(stack->FindCard(id) == nullptr);
	}
	for (const std::string& line : sink.Lines())
		CHECK(line.find("card.create") == std::string::npos);
	CHECK(FilesIn(root + "/cards").empty());

	// Changed: saved with rev 1 and logged.
	std::unique_ptr<Stack> stack = OpenStack(manifestPath, &sink);
	CHECK_EQ(stack->CardCount(), 0);
	Template* tmpl = FirstTemplate(*stack);
	std::string partId = tmpl->Parts()[0].GetString("id");
	std::string id;
	REQUIRE(stack->CreateProvisionalCard(tmpl->Id(), "", &id));
	stack->FindCard(id)->SetValue(partId, "入力");
	bool committed = false;
	REQUIRE(stack->CommitIfChanged(id, &committed));
	CHECK(committed);
	CHECK(!stack->IsProvisional(id));
	REQUIRE(stack->CardCount() == 1);
	CHECK_EQ(stack->CardAt(0)->Id(), id);
	CHECK_EQ(stack->CardAt(0)->Rev(), int64_t(1));
	CHECK_EQ(stack->CardAt(0)->Seq(), int64_t(1024));
	CHECK_EQ(FilesIn(root + "/cards").size(), size_t(1));
	int creates = 0;
	for (const std::string& line : sink.Lines()) {
		if (line.find("\"card.create\"") != std::string::npos)
			creates++;
	}
	CHECK_EQ(creates, 1);
	// A provisional card after a card goes after it when committed.
	std::string second;
	REQUIRE(stack->CreateProvisionalCard(tmpl->Id(), id, &second));
	stack->FindCard(second)->SetValue(partId, "二枚目");
	REQUIRE(stack->SaveCard(second));
	CHECK_EQ(stack->CardCount(), 2);
	CHECK_EQ(stack->IndexOf(second), 1);
	// Deleting a provisional card just drops it.
	std::string third;
	REQUIRE(stack->CreateProvisionalCard(tmpl->Id(), "", &third));
	REQUIRE(stack->DeleteCard(third));
	CHECK(stack->FindCard(third) == nullptr);
	CHECK_EQ(stack->CardCount(), 2);
}


// #pragma mark - themes


TEST("theme: bundled files parse; beos equals the built-in default")
{
	ThemeLibrary library("", "themes");
	std::vector<ThemeInfo> themes = library.List(nullptr);
	REQUIRE(themes.size() == 5);
	const char* ids[] = { "beos", "haiku", "nextstep", "macos", "win95" };
	for (int i = 0; i < 5; i++) {
		CHECK_EQ(themes[i].id, std::string(ids[i]));
		CHECK_EQ(themes[i].origin, std::string("bundled"));
	}
	Theme beos;
	REQUIRE(library.Find("beos", nullptr, beos));
	CHECK(beos.Doc() == Theme::Default().Doc());
	CHECK_EQ(beos.Name("ja"), std::string("BeOS"));
	Theme haiku;
	REQUIRE(library.Find("haiku", nullptr, haiku));
	CHECK(haiku.Native());
	Theme win95;
	REQUIRE(library.Find("win95", nullptr, win95));
	CHECK(win95.GetColor("panel") == Color::Make(0xc0, 0xc0, 0xc0));
	CHECK(win95.GetColor("accent") == Color::Make(0, 0, 0x80));
	CHECK_EQ(win95.ResolveKind("button").look, std::string("raised2"));
	CHECK_EQ(win95.ResolveKind("text").look, std::string("sunken2"));
	Theme macos;
	REQUIRE(library.Find("macos", nullptr, macos));
	CHECK_EQ(macos.ResolveKind("button").look, std::string("round"));
	CHECK(macos.ResolveKind("button").radius > 0);
	// Without files the built-in beos is still there.
	ThemeLibrary empty;
	Theme fallback;
	std::string origin;
	REQUIRE(empty.Find("beos", nullptr, fallback, &origin));
	CHECK_EQ(origin, std::string("builtin"));
	CHECK(!empty.Find("win95", nullptr, fallback));
}


TEST("theme: precedence part style > kind > colors > default (5)")
{
	const Theme& theme = Theme::Default();
	Json part = part::Make(kKindText, "f_aaaaaa", "名前", {0, 0, 100, 24});
	PartLook look = theme.Resolve(part);
	CHECK_EQ(look.look, std::string("sunken"));
	CHECK_EQ(look.borderWidth, 2);
	CHECK(look.hasFill && look.fill == Color::Make(255, 255, 255));
	CHECK(look.textColor == Color::Make(0, 0, 0));
	CHECK_EQ(look.fontSize, 12);
	CHECK(!look.ownFill && !look.ownBorder && !look.ownFont);
	Json style;
	REQUIRE(Json::Parse("{\"border\": {\"width\": 0}, \"fill\": null, "
		"\"font\": {\"size\": 20, \"color\": \"#ff0000\"}}", style));
	part.Set("style", style);
	look = theme.Resolve(part);
	CHECK(look.NoBorder());
	CHECK(!look.hasFill);
	CHECK_EQ(look.fontSize, 20);
	CHECK(look.textColor == Color::Make(255, 0, 0));
	CHECK(look.ownFill && look.ownBorder && look.ownFont);
	// A theme without kinds falls back to the default theme (4).
	Theme bare;
	REQUIRE(Theme::Parse("{\"bdb\": \"theme\", \"format_version\": 1, "
		"\"theme_id\": \"bare\", \"colors\": {\"panel\": \"#000000\"}}",
		bare));
	CHECK(bare.GetColor("panel") == Color::Make(0, 0, 0));
	CHECK(bare.GetColor("accent") == Color::Make(0xff, 0xcb, 0));
	CHECK_EQ(bare.ResolveKind("button").look, std::string("raised"));
	// A theme with kinds draws missing kinds as a thin flat line.
	Theme partial;
	REQUIRE(Theme::Parse("{\"bdb\": \"theme\", \"format_version\": 1, "
		"\"theme_id\": \"partial\", \"kinds\": {\"text\": {\"look\": "
		"\"etched\"}, \"label\": {\"look\": \"wavy\"}}}", partial));
	CHECK_EQ(partial.ResolveKind("text").look, std::string("etched"));
	CHECK_EQ(partial.ResolveKind("label").look, std::string("flat"));
	CHECK_EQ(partial.ResolveKind("button").look, std::string("flat"));
	CHECK_EQ(partial.ResolveKind("button").borderWidth, 1);
	// Bad documents.
	Theme bad;
	CHECK(!Theme::Parse("{\"bdb\": \"template\", \"format_version\": 1, "
		"\"theme_id\": \"x\"}", bad));
	CHECK(!Theme::Parse("{\"bdb\": \"theme\", \"format_version\": 1, "
		"\"theme_id\": \"Bad Id\"}", bad));
}


TEST("theme: applying copies into the stack; missing themes are problems")
{
	std::string dir = TempDir();
	std::string manifestPath = NewStack(dir);
	std::string root = RootOf(manifestPath);
	std::string userDir = dir + "/user-themes";
	MakeDirectories(userDir);
	std::string themeData = ReadFile("themes/win95.rfds");
	ThemeLibrary library(userDir, "themes");
	MemoryLogSink sink;
	std::unique_ptr<Stack> stack;
	REQUIRE(Stack::Open(manifestPath, &sink, stack, &library));
	Template* tmpl = FirstTemplate(*stack);
	CHECK_EQ(tmpl->ThemeId(), std::string("beos"));
	REQUIRE(stack->ApplyTheme(tmpl->Id(), "win95", false));
	REQUIRE(stack->SaveTemplate(tmpl->Id()));
	CHECK_EQ(ReadFile(root + "/themes/win95.rfds"), themeData);
	bool logged = false;
	for (const std::string& line : sink.Lines()) {
		if (line.find("\"theme.apply\"") != std::string::npos)
			logged = true;
	}
	CHECK(logged);
	// A different rev in the user folder needs confirmation.
	Json changed;
	REQUIRE(Json::Parse(themeData, changed));
	changed.Set("rev", 2);
	WriteFile(userDir + "/win95.rfds", changed.Dump());
	Status status = stack->ApplyTheme(tmpl->Id(), "win95", false);
	CHECK(status.Kind() == ErrorKind::kConflict);
	REQUIRE(stack->ApplyTheme(tmpl->Id(), "win95", true));
	Theme copied;
	REQUIRE(stack->FindTheme("win95", copied));
	CHECK_EQ(copied.Rev(), int64_t(2));
	// The stack copy wins over the outside ones (3.2), even without them.
	stack.reset();
	ThemeLibrary none;
	REQUIRE(Stack::Open(manifestPath, nullptr, stack, &none));
	CHECK(stack->Problems().empty());
	CHECK(stack->ThemeFor(FirstTemplate(*stack)).GetColor("panel")
		== Color::Make(0xc0, 0xc0, 0xc0));
	// Cleanup never touches themes/.
	CleanupReport report;
	REQUIRE(stack->Cleanup(true, report));
	CHECK(FileExists(root + "/themes/win95.rfds"));
	// A missing theme: problem, and the default theme is used.
	FirstTemplate(*stack)->SetThemeId("nothere");
	REQUIRE(stack->SaveTemplate(FirstTemplate(*stack)->Id()));
	stack.reset();
	REQUIRE(Stack::Open(manifestPath, nullptr, stack, &library));
	REQUIRE(stack->Problems().size() == 1);
	CHECK_EQ(stack->Problems()[0].reason, std::string("theme_missing"));
	CHECK(stack->ThemeFor(FirstTemplate(*stack)).Doc()
		== Theme::Default().Doc());
}


// #pragma mark - media


TEST("media: template media and icon reuse by content")
{
	std::string dir = TempDir();
	MemoryLogSink sink;
	std::string manifestPath = NewStack(dir);
	std::unique_ptr<Stack> stack = OpenStack(manifestPath, &sink);
	std::string icon = "ncif-icon-bytes";
	StringInStream first(icon);
	Json ref1, ref2, ref3;
	REQUIRE(stack->AddMedia("image", "hvif", first, true, "t", "p", ref1));
	StringInStream second(icon);
	REQUIRE(stack->AddMedia("image", "hvif", second, true, "t", "p", ref2));
	CHECK(ref1 == ref2);
	std::string other = "other";
	StringInStream third(other);
	REQUIRE(stack->AddMedia("image", "hvif", third, true, "t", "p", ref3));
	CHECK(!(ref1 == ref3));
	CHECK_EQ(FilesIn(RootOf(manifestPath) + "/media/image").size(), size_t(2));
	StringInStream bad(icon);
	Json ignored;
	CHECK(!stack->AddMedia("image", "../x", bad, false, "t", "p", ignored));
}


// #pragma mark - CSV


TEST("csv: RFC 4180 quotes, line breaks, empty rows, mismatches")
{
	CsvReader reader;
	CsvOptions options;
	std::string data = "名前,説明,数\r\n"
		"\"山田, 太郎\",\"一行目\r\n二行目\",1\r\n"
		"\r\n"
		"\"引用\"\"符\"\"\",,2\n"
		"短い\r"
		"多い,a,3,余分\n"
		"最後,\"b\",4";
	REQUIRE(reader.OpenString(data, options));
	CHECK_EQ(reader.Delimiter(), ',');
	REQUIRE(reader.ColumnCount() == 3);
	CHECK_EQ(reader.Header()[0], std::string("名前"));
	std::vector<CsvRecord> records;
	CsvRecord record;
	Status status;
	while (reader.Next(record, status))
		records.push_back(record);
	CHECK(status);
	REQUIRE(records.size() == 5);
	CHECK_EQ(records[0].fields[0], std::string("山田, 太郎"));
	CHECK_EQ(records[0].fields[1], std::string("一行目\r\n二行目"));
	CHECK_EQ(records[0].line, size_t(2));
	CHECK_EQ(records[1].fields[0], std::string("引用\"符\""));
	CHECK_EQ(records[1].fields[1], std::string(""));
	CHECK_EQ(records[1].line, size_t(5));
	CHECK_EQ(records[2].fields.size(), size_t(1));
	CHECK_EQ(records[3].fields.size(), size_t(4));
	CHECK_EQ(records[4].fields[1], std::string("b"));
	CHECK_EQ(records[4].line, size_t(8));
}


TEST("csv: delimiters, header off, skipped lines")
{
	CsvOptions options;
	{
		CsvReader reader;
		REQUIRE(reader.OpenString("a\tb\tc\n1\t2,5\t3\n4\t5\t6\n", options));
		CHECK_EQ(reader.Delimiter(), '\t');
		CHECK_EQ(reader.ColumnCount(), size_t(3));
	}
	{
		CsvReader reader;
		REQUIRE(reader.OpenString("a;b\n1;2\n3;4\n", options));
		CHECK_EQ(reader.Delimiter(), ';');
	}
	{
		CsvOptions noHeader;
		noHeader.header = false;
		CsvReader reader;
		REQUIRE(reader.OpenString("1,2\n3,4\n", noHeader));
		CHECK_EQ(reader.Header()[0], std::string("列1"));
		CHECK_EQ(reader.Header()[1], std::string("列2"));
		CsvRecord record;
		Status status;
		int count = 0;
		while (reader.Next(record, status))
			count++;
		CHECK_EQ(count, 2);
	}
	{
		// A title line above the header (renfudasample.csv).
		CsvOptions skip;
		skip.skipLines = 1;
		CsvReader reader;
		REQUIRE(reader.OpenString("title\r\nA,B\r\n1,2\r\n", skip));
		CHECK_EQ(reader.ColumnCount(), size_t(2));
		CHECK_EQ(reader.Header()[1], std::string("B"));
	}
}


TEST("csv: Shift_JIS and EUC-JP are detected and converted")
{
	// "名前,住所\n山田,東京都\n" in Shift_JIS and EUC-JP.
	std::string sjis = "\x96\xbc\x91\x4f,\x8f\x5a\x8f\x8a\n"
		"\x8e\x52\x93\x63,\x93\x8c\x8b\x9e\x93\x73\n";
	std::string euc = "\xcc\xbe\xc1\xb0,\xbd\xbb\xbd\xea\n"
		"\xbb\xb3\xc5\xc4,\xc5\xec\xb5\xfe\xc5\xd4\n";
	CHECK(DetectEncoding(sjis) == CsvEncoding::kShiftJis);
	CHECK(DetectEncoding(euc) == CsvEncoding::kEucJp);
	CHECK(DetectEncoding("\xef\xbb\xbf" "abc") == CsvEncoding::kUtf8);
	CHECK(DetectEncoding("名前,住所") == CsvEncoding::kUtf8);
	for (const std::string& data : { sjis, euc }) {
		CsvReader reader;
		REQUIRE(reader.OpenString(data, CsvOptions()));
		CHECK_EQ(reader.Header()[0], std::string("名前"));
		CHECK_EQ(reader.Header()[1], std::string("住所"));
		CsvRecord record;
		Status status;
		REQUIRE(reader.Next(record, status));
		CHECK_EQ(record.fields[1], std::string("東京都"));
	}
	// Half-width katakana, split sequences, invalid bytes.
	TextDecoder decoder(CsvEncoding::kShiftJis);
	std::string out;
	decoder.Feed("\xb1\x96", 2, out);
	decoder.Feed("\xbc", 1, out);
	decoder.Finish(out);
	CHECK_EQ(out, std::string("ｱ名"));
	TextDecoder utf8(CsvEncoding::kUtf8);
	out.clear();
	utf8.Feed("\xef\xbb\xbf\xe5\x90", 5, out);
	utf8.Feed("\x8d\xff", 2, out);
	utf8.Finish(out);
	CHECK_EQ(out, std::string("名\xef\xbf\xbd"));
	CHECK_EQ(utf8.Errors(), size_t(1));
	// Explicit choice overrides detection.
	CsvOptions options;
	options.encoding = CsvEncoding::kShiftJis;
	CsvReader reader;
	REQUIRE(reader.OpenString(sjis, options));
	CHECK(reader.Encoding() == CsvEncoding::kShiftJis);
}


TEST("csv: guessing column usage (3.4)")
{
	std::vector<std::string> header = { "名称", "価格", "有無", "サムネイルURL",
		"精細画像URL", "詳細ページURL", "img", "説明" };
	std::string longText(90, 'x');
	std::vector<std::vector<std::string>> rows = {
		{ "a", "100", "はい", "https://x.example/1", "https://x.example/f1",
			"https://x.example/p1", "https://x.example/a.JPG?w=1", longText },
		{ "b", "1.5", "いいえ", "https://x.example/2", "https://x.example/f2",
			"https://x.example/p2", "https://x.example/b.png", "short" },
		{ "c", "", "", "", "", "", "", "" }
	};
	std::vector<CsvColumn> columns = GuessColumns(header, rows, "ja");
	REQUIRE(columns.size() == 8);
	CHECK(columns[0].usage == CsvUsage::kText);
	CHECK(columns[1].usage == CsvUsage::kNumber);
	CHECK(columns[2].usage == CsvUsage::kBool);
	CHECK(columns[3].usage == CsvUsage::kImageUrl);
	CHECK(columns[4].usage == CsvUsage::kLinkUrl);
	CHECK(columns[5].usage == CsvUsage::kLinkUrl);
	CHECK(columns[6].usage == CsvUsage::kImageUrl);
	CHECK(columns[7].usage == CsvUsage::kText);
	CHECK(columns[7].multiline);
	CHECK(!columns[0].multiline);
	CHECK(columns[4].linkImage);
	CHECK(!columns[5].linkImage);
	CHECK_EQ(columns[4].buttonText, std::string("精細画像を開く"));
	CHECK_EQ(LinkButtonText("詳細ページURL", "ja"),
		std::string("詳細ページを開く"));
	CHECK_EQ(LinkButtonText("Detail URL", "en"), std::string("Open Detail"));
	// No words: the extensions decide.
	CHECK(GuessUrlUsage("URL", { "https://a/b.webp", "https://a/c.gif" })
		== CsvUsage::kImageUrl);
	CHECK(GuessUrlUsage("URL", { "https://a/b.webp", "https://a/c" })
		== CsvUsage::kLinkUrl);
	// 0/1 only: a switch.
	std::vector<CsvColumn> flags = GuessColumns({ "f" }, { { "0" }, { "1" } },
		"ja");
	CHECK(flags[0].usage == CsvUsage::kBool);
}


namespace {


class FakeImages : public CsvImageSource {
public:
	virtual bool FromFile(const std::string& path, int maxEdge,
		std::string& outData, std::string& outExt, std::string& reason)
	{
		if (path.find("missing") != std::string::npos) {
			reason = "not_found";
			return false;
		}
		outData = "jpeg:" + path;
		outExt = "jpg";
		return true;
	}

	virtual bool FromUrl(const std::string& url, int maxEdge,
		std::string& outData, std::string& outExt, std::string& reason)
	{
		reason = "not_image";
		return false;
	}
};


}	// namespace


TEST("csv: import into a new stack (3.5, 3.6, 3.7)")
{
	std::string dir = TempDir();
	std::string csvPath = dir + "/in.csv";
	WriteFile(csvPath, "renfudasample\r\n"
		"名称,説明,収蔵場所,サムネイルURL,精細画像URL,数,写真ファイル\r\n"
		"一,説明一,場所一,https://ndl.go.jp,https://ndl.go.jp,1,/a.jpg\r\n"
		"二,説明二,場所二,https://ndl.go.jp,https://ndl.go.jp,x,/missing.jpg"
		"\r\n"
		"三,説明三,場所三,https://ndl.go.jp,https://ndl.go.jp\r\n");
	CsvOptions options;
	options.skipLines = 1;
	CsvScan scan;
	REQUIRE(ScanCsv(csvPath, options, scan));
	CHECK_EQ(scan.rows, size_t(3));
	CHECK_EQ(scan.mismatched, size_t(1));

	CsvReader preview;
	REQUIRE(preview.Open(csvPath, options));
	std::vector<std::vector<std::string>> rows;
	CsvRecord record;
	Status status;
	while (preview.Next(record, status))
		rows.push_back(record.fields);
	std::vector<CsvColumn> columns = GuessColumns(preview.Header(), rows,
		"ja");
	REQUIRE(columns.size() == 7);
	CHECK(columns[3].usage == CsvUsage::kImageUrl);
	CHECK(columns[4].usage == CsvUsage::kLinkUrl);
	CHECK(columns[5].usage == CsvUsage::kText);	// "x" is not a number
	columns[5].usage = CsvUsage::kNumber;
	columns[6].usage = CsvUsage::kImagePath;

	CsvLayoutOptions layout;
	layout.title = "連札";
	layout.paperSize = "A6";
	layout.orientation = "landscape";
	layout.width = 559;
	layout.height = 397;
	layout.fullNavigation = true;
	Json templateDoc;
	std::vector<CsvColumnParts> parts;
	REQUIRE(BuildCsvTemplate(columns, layout, templateDoc, parts));
	CHECK(Template::Validate(templateDoc));
	StoredDoc stored;
	stored.doc = templateDoc;
	Template tmpl(stored);
	CHECK_EQ(tmpl.TitlePartSetting(), parts[0].value);
	const Json* image = tmpl.FindPart(parts[3].value);
	REQUIRE(image != nullptr);
	CHECK_EQ(image->Find("source")->GetString("field"), parts[3].url);
	// The image links to the detail URL (3.8).
	CHECK_EQ(image->Find("link")->GetString("field"), parts[4].url);
	const Json* button = tmpl.FindPart(parts[4].button);
	REQUIRE(button != nullptr);
	CHECK_EQ(button->GetString("action"), std::string("link.open"));
	CHECK_EQ(button->GetString("text"), std::string("精細画像を開く"));
	int navigation = 0, counters = 0;
	for (const Json& part : tmpl.Parts()) {
		Rect rect = part::GetRect(part);
		CHECK(rect.x >= 0 && rect.y >= 0 && rect.x + rect.w <= tmpl.Width()
			&& rect.y + rect.h <= tmpl.Height());
		if (part::Kind(part) == kKindButton
			&& part.GetString("action").compare(0, 5, "card.") == 0) {
			navigation++;
		}
		if (part::Kind(part) == kKindCounter)
			counters++;
	}
	CHECK_EQ(navigation, 4);
	CHECK_EQ(counters, 1);

	std::string dest = dir + "/連札.rfdf";
	std::string staging, manifestName;
	MemoryLogSink sink;
	REQUIRE(CreateCsvStack(dest, "連札", templateDoc, &sink, staging,
		manifestName));
	{
		std::unique_ptr<Stack> stack = OpenStack(staging + "/" + manifestName,
			&sink);
		REQUIRE(stack != nullptr);
		CsvReader reader;
		REQUIRE(reader.Open(csvPath, options));
		FakeImages images;
		CsvImportReport report;
		int calls = 0;
		REQUIRE(ImportCsvRows(*stack, tmpl.Id(), reader, columns, parts,
			&images, [&calls](const CsvImportReport&) { calls++;
				return true; }, report));
		CHECK_EQ(report.rows, size_t(3));
		CHECK_EQ(report.cards, size_t(3));
		CHECK_EQ(calls, 3);
		CHECK_EQ(report.valueErrors, size_t(1));
		CHECK_EQ(report.mismatched, size_t(1));
		CHECK_EQ(report.imagesOk, size_t(1));
		CHECK_EQ(report.imagesFailed, size_t(1));
		CHECK_EQ(report.imageFailures["not_found"], size_t(1));
		CHECK_EQ(stack->CardCount(), 3);
		const Card* card = stack->CardAt(0);
		CHECK_EQ(card->Value(parts[0].value)->AsString(), std::string("一"));
		CHECK_EQ(card->Value(parts[5].value)->AsInt(), int64_t(1));
		CHECK(IsMediaRef(*card->Value(parts[6].value)));
		CHECK_EQ(card->Value(parts[3].url)->AsString(),
			std::string("https://ndl.go.jp"));
		CHECK(stack->CardAt(1)->Value(parts[5].value)->IsNull());
		CHECK_EQ(stack->CardAt(2)->Seq(), int64_t(3072));
		// Images to fetch: the three URL thumbnails.
		std::vector<FetchItem> fetch = FindImagesToFetch(*stack);
		CHECK_EQ(fetch.size(), size_t(3));
		CHECK_EQ(fetch[0].maxEdge, 256);
		LogCsvImport(stack->Log(), report);
		std::string text = report.Text("ja");
		CHECK(text.find("説明一") == std::string::npos);
		CHECK(text.find("ndl.go.jp") == std::string::npos);
	}
	CHECK(!FileExists(dest));
	REQUIRE(FinishCsvStack(staging, dest));
	std::unique_ptr<Stack> stack = OpenStack(dest);
	REQUIRE(stack != nullptr);
	CHECK(stack->Problems().empty());
	CHECK_EQ(stack->CardCount(), 3);
	// The log has counts only.
	bool imported = false;
	for (const std::string& line : sink.Lines()) {
		CHECK(line.find("ndl.go.jp") == std::string::npos);
		CHECK(line.find("説明") == std::string::npos);
		Json parsed;
		CHECK(Json::Parse(line, parsed));
		if (line.find("\"stack.csv_import\"") != std::string::npos) {
			imported = true;
			CHECK_EQ(parsed.GetInt("cards"), int64_t(3));
			CHECK_EQ(parsed.GetInt("images_ok"), int64_t(1));
		}
	}
	CHECK(imported);
}


TEST("csv: cancel leaves nothing; limits; existing template mode")
{
	std::string dir = TempDir();
	std::string csvPath = dir + "/in.csv";
	std::string data = "a,b\n";
	for (int i = 0; i < 50; i++)
		data += std::to_string(i) + ",x" + std::to_string(i) + "\n";
	WriteFile(csvPath, data);
	CsvReader preview;
	REQUIRE(preview.Open(csvPath, CsvOptions()));
	std::vector<CsvColumn> columns = GuessColumns(preview.Header(), {}, "ja");
	Json templateDoc;
	std::vector<CsvColumnParts> parts;
	REQUIRE(BuildCsvTemplate(columns, CsvLayoutOptions(), templateDoc, parts));
	std::string dest = dir + "/new.rfdf";
	std::string staging, manifestName;
	REQUIRE(CreateCsvStack(dest, "new", templateDoc, nullptr, staging,
		manifestName));
	{
		std::unique_ptr<Stack> stack = OpenStack(staging + "/" + manifestName);
		CsvReader reader;
		REQUIRE(reader.Open(csvPath, CsvOptions()));
		CsvImportReport report;
		REQUIRE(ImportCsvRows(*stack, templateDoc.GetString("template_id"),
			reader, columns, parts, nullptr,
			[](const CsvImportReport& r) { return r.cards < 10; }, report));
		CHECK(report.canceled);
		CHECK_EQ(report.cards, size_t(10));
	}
	RemoveTree(staging);
	for (const std::string& file : FilesIn(dir))
		CHECK(file.find(".rfd-new-") == std::string::npos);
	CHECK(!FileExists(dest));

	// A field over 1 MiB is refused before importing.
	std::string bigPath = dir + "/big.csv";
	WriteFile(bigPath, "a\n\"" + std::string(kMaxCsvFieldBytes + 1, 'x')
		+ "\"\n");
	CsvScan scan;
	Status status = ScanCsv(bigPath, CsvOptions(), scan);
	CHECK(status.Kind() == ErrorKind::kLimitExceeded);
	CHECK(scan.fieldTooLarge);

	// Existing template: columns go to the chosen parts.
	MakeDirectories(dir + "/x");
	std::string manifestPath = NewStack(dir + "/x");
	std::unique_ptr<Stack> stack = OpenStack(manifestPath);
	std::string number = AddPart(*stack, kKindNumber, "番号");
	std::string text = AddPart(*stack, kKindText, "名前", 48);
	std::vector<CsvColumn> mapped = columns;
	mapped[0].targetPart = number;
	mapped[1].targetPart = text;
	CsvReader reader;
	REQUIRE(reader.Open(csvPath, CsvOptions()));
	CsvImportReport report;
	std::vector<CsvColumnParts> none(2);
	REQUIRE(ImportCsvRows(*stack, stack->DefaultTemplateId(), reader, mapped,
		none, nullptr, nullptr, report));
	CHECK_EQ(report.cards, size_t(50));
	CHECK_EQ(stack->CardCount(), 51);
	CHECK_EQ(stack->CardAt(1)->Value(number)->AsInt(), int64_t(0));
	CHECK_EQ(stack->CardAt(50)->Value(text)->AsString(), std::string("x49"));
}


TEST("csv: layout grows or splits when the fields do not fit")
{
	std::vector<CsvColumn> columns;
	for (int i = 0; i < 20; i++) {
		CsvColumn column;
		column.header = column.name = "項目" + std::to_string(i);
		columns.push_back(column);
	}
	Json doc;
	std::vector<CsvColumnParts> parts;
	REQUIRE(BuildCsvTemplate(columns, CsvLayoutOptions(), doc, parts));
	StoredDoc stored;
	stored.doc = doc;
	Template grown(stored);
	CHECK(grown.Height() > 342);
	for (const Json& part : grown.Parts()) {
		Rect rect = part::GetRect(part);
		CHECK(rect.y + rect.h <= grown.Height());
	}
	CsvLayoutOptions a4;
	a4.paperSize = "A6";
	a4.orientation = "landscape";
	a4.width = 559;
	a4.height = 397;
	REQUIRE(BuildCsvTemplate(columns, a4, doc, parts));
	stored.doc = doc;
	Template split(stored);
	std::set<int32_t> xs;
	for (const Json& part : split.Parts()) {
		if (part::Kind(part) == kKindText)
			xs.insert(part::GetRect(part).x);
	}
	CHECK_EQ(xs.size(), size_t(2));
}


// #pragma mark - PDF


namespace {


class BoxGlyphs : public PdfGlyphSource {
public:
	virtual bool Glyph(const PdfFont& font, uint32_t codePoint,
		double& advance, PdfPath& outline)
	{
		advance = codePoint < 0x100 ? 600 : 1000;
		if (codePoint != ' ')
			outline.AddRect(50, 0, advance - 100, 700);
		return true;
	}
};


}	// namespace


TEST("pdf: pages, Type 3 text with ToUnicode, images, atomic write")
{
	std::string dir = TempDir();
	std::string path = dir + "/out.pdf";
	BoxGlyphs glyphs;
	PdfWriter writer(&glyphs);
	REQUIRE(writer.Begin(path, "テスト"));
	CHECK(FileExists(path + ".tmp"));
	CHECK(!FileExists(path));
	writer.BeginPage(PixelsToPoints(559), PixelsToPoints(397));
	writer.SetFillColor(Color::Make(0xd8, 0xd8, 0xd8));
	writer.FillRect(0, 0, 100, 100);
	writer.SetStrokeColor(Color::Make(0, 0, 0));
	writer.SetLineWidth(1);
	writer.StrokeRect(10, 10, 50, 20);
	PdfPath round;
	round.AddRoundRect(10, 40, 60, 20, 6);
	writer.StrokePath(round);
	writer.Save();
	writer.ClipRect(10, 10, 50, 20);
	writer.SetFillColor(Color::Make(0, 0, 0));
	PdfFont font;
	writer.DrawText("名前 Abc", 12, 25, font, 12);
	writer.Restore();
	CHECK(std::fabs(writer.TextWidth("Ab", font, 10) - 12) < 1e-9);
	std::vector<uint8_t> rgb(4 * 3 * 3, 128);
	std::vector<uint8_t> alpha(4 * 3, 255);
	alpha[0] = 0;
	int image = writer.AddRgb("key", rgb, alpha, 4, 3);
	CHECK(image >= 0);
	CHECK_EQ(writer.AddRgb("key", rgb, alpha, 4, 3), image);
	writer.DrawImage(image, 0, 0, 40, 30);
	writer.EndPage();
	// Over 256 glyphs: several sub fonts.
	writer.BeginPage(100, 100);
	std::string many;
	for (uint32_t cp = 0x4e00; cp < 0x4e00 + 300; cp++) {
		char buffer[4] = { char(0xe0 | (cp >> 12)),
			char(0x80 | ((cp >> 6) & 0x3f)), char(0x80 | (cp & 0x3f)), 0 };
		many += buffer;
	}
	writer.DrawText(many, 0, 50, font, 8);
	writer.EndPage();
	CHECK_EQ(writer.PageCount(), 2);
	uint64_t bytes = 0;
	REQUIRE(writer.Finish(&bytes));
	CHECK(!FileExists(path + ".tmp"));
	std::string pdf = ReadFile(path);
	CHECK_EQ(pdf.size(), size_t(bytes));
	CHECK_EQ(pdf.compare(0, 8, "%PDF-1.4"), 0);
	CHECK(pdf.find("/Subtype /Type3") != std::string::npos);
	CHECK(pdf.find("/ToUnicode") != std::string::npos);
	CHECK(pdf.find("/SMask") != std::string::npos);
	CHECK(pdf.find("/Count 2") != std::string::npos);
	CHECK(pdf.find("/MediaBox [0 0 419.25 297.75]") != std::string::npos);
	CHECK(pdf.rfind("%%EOF") != std::string::npos);
	// Each xref offset points at "<n> 0 obj".
	size_t start = pdf.rfind("startxref\n");
	REQUIRE(start != std::string::npos);
	size_t xref = std::stoul(pdf.substr(start + 10));
	REQUIRE(pdf.compare(xref, 4, "xref") == 0);
	std::istringstream table(pdf.substr(xref));
	std::string word;
	size_t count;
	table >> word >> word >> count;
	std::string offset, generation, flag;
	table >> offset >> generation >> flag;
	int checked = 0;
	for (size_t i = 1; i < count; i++) {
		table >> offset >> generation >> flag;
		size_t position = std::stoul(offset);
		std::string expected = std::to_string(i) + " 0 obj";
		CHECK(pdf.compare(position, expected.size(), expected) == 0);
		checked++;
	}
	CHECK(checked > 10);
	int fonts = 0;
	for (size_t at = 0; (at = pdf.find("/Subtype /Type3", at))
			!= std::string::npos; at++) {
		fonts++;
	}
	CHECK_EQ(fonts, 2);	// 306 glyphs: 256 + 50

	// Abort leaves nothing.
	PdfWriter aborted(&glyphs);
	REQUIRE(aborted.Begin(dir + "/aborted.pdf", "x"));
	aborted.BeginPage(10, 10);
	aborted.EndPage();
	aborted.Abort();
	CHECK(!FileExists(dir + "/aborted.pdf"));
	CHECK(!FileExists(dir + "/aborted.pdf.tmp"));
}


TEST("pdf: JPEG header parsing and pass-through")
{
	// Minimal SOI + APP0 + SOF0 (16x8, 3 components).
	std::string jpeg = std::string("\xff\xd8\xff\xe0\x00\x04\x00\x00"
		"\xff\xc0\x00\x11\x08\x00\x08\x00\x10\x03", 18)
		+ std::string(9, '\x01') + std::string("\xff\xd9", 2);
	int width, height, components;
	REQUIRE(PdfWriter::JpegInfo(jpeg, width, height, components));
	CHECK_EQ(width, 16);
	CHECK_EQ(height, 8);
	CHECK_EQ(components, 3);
	CHECK(!PdfWriter::JpegInfo("not a jpeg", width, height, components));
	std::string dir = TempDir();
	PdfWriter writer(nullptr);
	REQUIRE(writer.Begin(dir + "/j.pdf", "j"));
	writer.BeginPage(100, 100);
	int image = writer.AddJpeg("j", jpeg, width, height, components);
	writer.DrawImage(image, 0, 0, 16, 8);
	writer.EndPage();
	REQUIRE(writer.Finish());
	std::string pdf = ReadFile(dir + "/j.pdf");
	CHECK(pdf.find("/DCTDecode") != std::string::npos);
	CHECK(pdf.find(jpeg) != std::string::npos);
}


TEST("bundled English presets and samples")
{
	std::vector<PresetInfo> presets;
	REQUIRE(ListPresets("presets/en", presets));
	CHECK_EQ(presets.size(), size_t(3));
	std::set<std::string> titles;
	for (const PresetInfo& info : presets) {
		titles.insert(info.title);
		std::unique_ptr<Stack> preset = OpenStack(info.path);
		REQUIRE(preset != nullptr);
		CHECK(preset->Problems().empty());
		// Navigation in every preset (19.2).
		int buttons = 0, counters = 0;
		for (const Json& part : FirstTemplate(*preset)->Parts()) {
			if (part::Kind(part) == kKindButton)
				buttons++;
			if (part::Kind(part) == kKindCounter)
				counters++;
		}
		CHECK(buttons >= 2);
		CHECK_EQ(counters, 1);
	}
	CHECK(titles.count("Address Book") == 1);
	CHECK(titles.count("Books") == 1);
	CHECK(titles.count("Business Cards") == 1);
	for (const char* lang : { "ja", "en" }) {
		std::vector<PresetInfo> samples;
		REQUIRE(ListPresets(std::string("samples/") + lang, samples));
		CHECK(samples.size() >= 2);
		for (const PresetInfo& info : samples) {
			std::unique_ptr<Stack> sample = OpenStack(info.path);
			REQUIRE(sample != nullptr);
			CHECK(sample->Problems().empty());
			CHECK(sample->CardCount() > 0);
		}
	}
}


int
main(int argc, char** argv)
{
	return test::RunAll(argc > 1 ? argv[1] : nullptr);
}
