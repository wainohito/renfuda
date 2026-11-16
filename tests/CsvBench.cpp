/*
 * BDB card database - I/O spec acceptance test 12 (measurement only):
 * imports a CSV of 100,000 rows into a new stack and reports the time
 * and memory.
 *
 *   csv_bench <work dir> [rows]
 */
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <sys/resource.h>

#include "Csv.h"
#include "Preset.h"
#include "Stack.h"
#include "Store.h"


using namespace bdb;
typedef std::chrono::steady_clock Clock;


static double
Seconds(Clock::time_point start)
{
	return std::chrono::duration<double>(Clock::now() - start).count();
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <work dir> [rows]\n", argv[0]);
		return 2;
	}
	std::string dir = argv[1];
	int rows = argc > 2 ? atoi(argv[2]) : 100000;
	MakeDirectories(dir);
	std::string csvPath = dir + "/bench.csv";
	{
		std::ofstream out(csvPath, std::ios::binary | std::ios::trunc);
		out << "名称,説明,価格,済,サムネイルURL\n";
		for (int i = 0; i < rows; i++) {
			out << "項目" << i << ",\"説明の文章 " << i
				<< "、カンマ, と改行\n二行目\"," << (i % 1000) << ","
				<< (i % 2) << ",https://example.com/" << i << ".jpg\n";
		}
	}
	RemoveTree(dir + "/bench-csv.rfdf");

	Clock::time_point start = Clock::now();
	CsvScan scan;
	Status status = ScanCsv(csvPath, CsvOptions(), scan);
	printf("scan: %zu rows in %.2f s (%s)\n", scan.rows, Seconds(start),
		status.Code());

	start = Clock::now();
	CsvReader preview;
	preview.Open(csvPath, CsvOptions());
	std::vector<std::vector<std::string>> sample;
	CsvRecord record;
	while (sample.size() < 1000 && preview.Next(record, status))
		sample.push_back(record.fields);
	std::vector<CsvColumn> columns = GuessColumns(preview.Header(), sample,
		"ja");
	Json templateDoc;
	std::vector<CsvColumnParts> parts;
	BuildCsvTemplate(columns, CsvLayoutOptions(), templateDoc, parts);
	std::string staging, manifestName;
	status = CreateCsvStack(dir + "/bench-csv.rfdf", "bench", templateDoc,
		nullptr, staging, manifestName);
	if (!status) {
		fprintf(stderr, "create: %s\n", status.Code());
		return 1;
	}
	CsvImportReport report;
	{
		std::unique_ptr<Stack> stack;
		Stack::Open(staging + "/" + manifestName, nullptr, stack);
		CsvReader reader;
		reader.Open(csvPath, CsvOptions());
		status = ImportCsvRows(*stack, templateDoc.GetString("template_id"),
			reader, columns, parts, nullptr, nullptr, report);
		printf("import: %zu cards in %.1f s (%s), %zu images to fetch\n",
			report.cards, Seconds(start), status.Code(),
			FindImagesToFetch(*stack).size());
	}
	FinishCsvStack(staging, dir + "/bench-csv.rfdf");
	struct rusage usage;
	getrusage(RUSAGE_SELF, &usage);
	printf("max rss: %ld KiB\n", usage.ru_maxrss);
	return 0;
}
