/*
 * BDB card database - tiny test harness
 */
#ifndef BDB_TEST_H
#define BDB_TEST_H

#include <cstdio>
#include <functional>
#include <string>
#include <vector>


namespace test {


struct Case {
	const char*				name;
	std::function<void()>	body;
};


inline std::vector<Case>&
Registry()
{
	static std::vector<Case> cases;
	return cases;
}


inline int& Failures() { static int failures = 0; return failures; }
inline int& Checks() { static int checks = 0; return checks; }


struct Registrar {
	Registrar(const char* name, std::function<void()> body)
	{
		Registry().push_back({name, body});
	}
};


inline int
RunAll(const char* filter)
{
	int failedCases = 0;
	for (const Case& testCase : Registry()) {
		if (filter != nullptr && std::string(testCase.name).find(filter)
				== std::string::npos) {
			continue;
		}
		int before = Failures();
		testCase.body();
		bool ok = Failures() == before;
		printf("%s  %s\n", ok ? "ok  " : "FAIL", testCase.name);
		if (!ok)
			failedCases++;
	}
	printf("\n%d checks, %d failed checks, %d failed cases\n", Checks(),
		Failures(), failedCases);
	return failedCases == 0 ? 0 : 1;
}


}	// namespace test


#define TEST_CONCAT2(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT2(a, b)
#define TEST(name) \
	static void TEST_CONCAT(test_body_, __LINE__)(); \
	static test::Registrar TEST_CONCAT(test_registrar_, __LINE__)(name, \
		TEST_CONCAT(test_body_, __LINE__)); \
	static void TEST_CONCAT(test_body_, __LINE__)()

#define CHECK(condition) \
	do { \
		test::Checks()++; \
		if (!(condition)) { \
			test::Failures()++; \
			printf("    %s:%d: CHECK(%s)\n", __FILE__, __LINE__, #condition); \
		} \
	} while (false)

#define CHECK_EQ(a, b) \
	do { \
		test::Checks()++; \
		auto _va = (a); \
		auto _vb = (b); \
		if (!(_va == _vb)) { \
			test::Failures()++; \
			printf("    %s:%d: CHECK_EQ(%s, %s)\n", __FILE__, __LINE__, #a, \
				#b); \
		} \
	} while (false)

#define REQUIRE(condition) \
	do { \
		test::Checks()++; \
		if (!(condition)) { \
			test::Failures()++; \
			printf("    %s:%d: REQUIRE(%s)\n", __FILE__, __LINE__, \
				#condition); \
			return; \
		} \
	} while (false)


#endif	// BDB_TEST_H
