/*
 * BDB card database - builds the CSV based sample stack "連札" (I/O spec
 * v0.1, 3.8) with the core's CSV import:
 *
 *     build/host/make_samples tools/data/renfudasample.csv samples
 *
 * Images are not fetched: the sample keeps the URLs (source) and the
 * application offers to fetch them the first time the sample is opened
 * (UI spec 3.5). No third party images are bundled.
 */
#include <cstdio>
#include <memory>
#include <string>
#include <sys/stat.h>

#include "Csv.h"
#include "Preset.h"
#include "Stack.h"
#include "Store.h"


using namespace bdb;


static int
Build(const std::string& csvPath, const std::string& outDir,
	const std::string& title, const std::string& lang)
{
	CsvOptions options;
	// The file starts with a title line above the header.
	options.skipLines = 1;
	CsvReader preview;
	Status status = preview.Open(csvPath, options);
	if (!status) {
		fprintf(stderr, "open: %s\n", status.Code());
		return 1;
	}
	std::vector<std::vector<std::string>> rows;
	CsvRecord record;
	while (preview.Next(record, status))
		rows.push_back(record.fields);
	std::vector<CsvColumn> columns = GuessColumns(preview.Header(), rows,
		lang);
	for (CsvColumn& column : columns) {
		if (column.header == "説明")
			column.multiline = true;
	}

	CsvLayoutOptions layout;
	layout.title = title;
	layout.paperSize = "A6";
	layout.orientation = "landscape";
	layout.width = 559;
	layout.height = 397;
	layout.themeId = "beos";
	layout.fullNavigation = true;
	layout.lang = lang;
	Json templateDoc;
	std::vector<CsvColumnParts> parts;
	status = BuildCsvTemplate(columns, layout, templateDoc, parts);
	if (!status) {
		fprintf(stderr, "template: %s %s\n", status.Code(),
			status.Detail().c_str());
		return 1;
	}

	MakeDirectories(outDir);
	std::string dest = outDir + "/" + title + ".rfdf";
	RemoveTree(dest);
	std::string staging, manifestName;
	status = CreateCsvStack(dest, title, templateDoc, nullptr, staging,
		manifestName);
	if (!status) {
		fprintf(stderr, "create: %s\n", status.Code());
		return 1;
	}
	{
		std::unique_ptr<Stack> stack;
		status = Stack::Open(staging + "/" + manifestName, nullptr, stack);
		CsvReader reader;
		if (status)
			status = reader.Open(csvPath, options);
		CsvImportReport report;
		if (status) {
			status = ImportCsvRows(*stack, templateDoc.GetString("template_id"),
				reader, columns, parts, nullptr, nullptr, report);
		}
		if (!status) {
			fprintf(stderr, "import: %s\n", status.Code());
			RemoveTree(staging);
			return 1;
		}
		printf("%s: %zu cards, %zu images to fetch\n", title.c_str(),
			report.cards, FindImagesToFetch(*stack).size());
	}
	status = FinishCsvStack(staging, dest);
	if (!status) {
		RemoveTree(staging);
		return 1;
	}
	return 0;
}


int
main(int argc, char** argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: make_samples <renfudasample.csv> <samples>\n");
		return 2;
	}
	std::string samples = argv[2];
	if (Build(argv[1], samples + "/ja", "連札", "ja") != 0)
		return 1;
	CsvReader::SetLanguage("en");
	return Build(argv[1], samples + "/en", "Renfuda", "en");
}
