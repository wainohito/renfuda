/*
 * BDB card database - acceptance test 15 (measurement only)
 * Builds a 100,000 card stack with flat cards/ and measures opening time
 * and memory, plus the in-memory operations the list view needs.
 *
 *   bench <work dir> [card count]
 */
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>

#include "Json.h"
#include "Limits.h"
#include "Model.h"
#include "Preset.h"
#include "Query.h"
#include "Stack.h"
#include "Text.h"


using namespace bdb;
typedef std::chrono::steady_clock Clock;


static double
Seconds(Clock::time_point start)
{
	return std::chrono::duration<double>(Clock::now() - start).count();
}


static long
MaxRssKiB()
{
	struct rusage usage;
	getrusage(RUSAGE_SELF, &usage);
	return usage.ru_maxrss;
}


static long
CurrentRssKiB()
{
	FILE* file = fopen("/proc/self/statm", "r");
	if (file == nullptr)
		return -1;
	long pages = 0, resident = 0;
	if (fscanf(file, "%ld %ld", &pages, &resident) != 2)
		resident = -1;
	fclose(file);
	return resident * 4;
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <work dir> [cards]\n", argv[0]);
		return 1;
	}
	std::string work = argv[1];
	int count = argc > 2 ? atoi(argv[2]) : 100000;
	mkdir(work.c_str(), 0755);
	std::string stackDir = work + "/bench-" + std::to_string(count) + ".rfdf";

	struct stat st;
	if (lstat(stackDir.c_str(), &st) != 0) {
		printf("creating %d cards in %s ...\n", count, stackDir.c_str());
		Clock::time_point start = Clock::now();
		std::string manifest;
		if (!CreateEmptyStack(stackDir, "bench", nullptr, &manifest)) {
			fprintf(stderr, "create failed\n");
			return 1;
		}
		std::unique_ptr<Stack> stack;
		Stack::Open(manifest, nullptr, stack);
		std::string templateId = stack->DefaultTemplateId();
		Template* tmpl = stack->FindTemplate(templateId);
		const char* names[] = { "氏名", "ふりがな", "住所", "電話", "メモ" };
		std::vector<std::string> text;
		for (int i = 0; i < 5; i++) {
			std::string id = tmpl->NewPartId(kKindText);
			tmpl->Parts().push_back(part::Make(kKindText, id, names[i],
				{16, 16 + 30 * i, 300, 24}));
			text.push_back(id);
		}
		std::string number = tmpl->NewPartId(kKindText);
		tmpl->Parts().push_back(part::Make(kKindNumber, number, "番号",
			{16, 200, 100, 24}));
		stack->SaveTemplate(templateId);
		srand(1);
		// Cards are written directly (no fsync) - the bench measures
		// opening, not creating. The per-save cost is measured below.
		std::string cardsDir = stackDir + "/cards/";
		// An empty stack has no cards (spec v0.4, 19.1).
		for (int i = 0; i < count; i++) {
			Card card = Card::Create(templateId, int64_t(i + 1) * kSeqStep);
			card.SetValue(text[0], "山田 太郎 " + std::to_string(rand()));
			card.SetValue(text[1], "やまだ たろう");
			card.SetValue(text[2], "東京都千代田区千代田1-1 "
				+ std::to_string(i));
			card.SetValue(text[3], "03-0000-" + std::to_string(i % 10000));
			card.SetValue(text[4], "メモ\n複数行の文章がここに入ります。");
			card.SetValue(number, rand() % 100000);
			std::string path = cardsDir + MakeFileName(
				CardTitle(card, tmpl), card.Id(), ".rfdd");
			FILE* file = fopen(path.c_str(), "w");
			std::string data = card.Doc().Dump();
			fwrite(data.data(), 1, data.size(), file);
			fclose(file);
			if (i % 20000 == 0)
				printf("  %d\n", i);
		}
		printf("created in %.1f s\n", Seconds(start));
		printf("(re-run to measure a cold-ish open)\n");
	}

	long rssBefore = CurrentRssKiB();
	Clock::time_point start = Clock::now();
	std::unique_ptr<Stack> stack;
	Status status = Stack::Open(stackDir, nullptr, stack);
	if (!status) {
		fprintf(stderr, "open failed: %s\n", status.Code());
		return 1;
	}
	double openTime = Seconds(start);
	long rssAfter = CurrentRssKiB();
	printf("open: %d cards, %zu problems, %.2f s\n", stack->CardCount(),
		stack->Problems().size(), openTime);
	printf("memory: +%ld KiB resident (max rss %ld KiB)\n",
		rssAfter - rssBefore, MaxRssKiB());

	Template* tmpl = stack->FindTemplate(stack->DefaultTemplateId());
	std::string firstText, number;
	for (const Json& part : tmpl->Parts()) {
		if (firstText.empty() && part::Kind(part) == kKindText)
			firstText = part::Id(part);
		if (part::Kind(part) == kKindNumber)
			number = part::Id(part);
	}

	start = Clock::now();
	std::vector<int> all = QuickSearch(*stack, tmpl->Id(), "");
	std::vector<int> hits = QuickSearch(*stack, tmpl->Id(), "千代田1-1 9999");
	printf("quick search: %zu hits in %.3f s\n", hits.size(), Seconds(start));

	start = Clock::now();
	std::vector<std::pair<std::string, int>> keys;
	keys.reserve(all.size());
	const Json* textPart = tmpl->FindPart(firstText);
	for (int index : all) {
		keys.push_back({EffectiveValue(*stack->CardAt(index), *textPart)
			.AsString(), index});
	}
	std::sort(keys.begin(), keys.end());
	printf("sort by text column: %.3f s\n", Seconds(start));

	start = Clock::now();
	std::vector<std::pair<double, int>> numbers;
	const Json* numberPart = tmpl->FindPart(number);
	for (int index : all) {
		numbers.push_back({EffectiveValue(*stack->CardAt(index), *numberPart)
			.AsDouble(), index});
	}
	std::sort(numbers.begin(), numbers.end());
	printf("sort by number column: %.3f s\n", Seconds(start));

	start = Clock::now();
	MemoryCardQuery query;
	std::vector<std::string> found;
	query.Find(*stack, {{number, "100..200"}}, found);
	printf("find 100..200: %zu cards in %.3f s\n", found.size(),
		Seconds(start));

	start = Clock::now();
	std::string newId;
	stack->CreateCard(tmpl->Id(), stack->CardCount() / 2, &newId);
	printf("insert a card in the middle (incl. fsync): %.3f s\n",
		Seconds(start));
	start = Clock::now();
	for (int i = 0; i < 20; i++) {
		stack->FindCard(newId)->SetValue(firstText, "x" + std::to_string(i));
		stack->SaveCard(newId);
	}
	printf("save one card (atomic, fsync): %.4f s each\n",
		Seconds(start) / 20);
	stack->DeleteCard(newId);

	start = Clock::now();
	CleanupReport report;
	stack->Cleanup(false, report);
	printf("cleanup dry run: %.2f s\n", Seconds(start));
	return 0;
}
