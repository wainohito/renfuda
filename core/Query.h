/*
 * BDB card database - core
 * Find mode (UI spec 8) and the list view quick search (UI spec 7).
 * CardQuery is the seam where Phase 3 plugs in BFS queries.
 */
#ifndef BDB_QUERY_H
#define BDB_QUERY_H

#include <string>
#include <vector>

#include "Status.h"


namespace bdb {


class Stack;


struct FindCondition {
	std::string	partId;
	std::string	text;		// as typed by the user; bool parts: "on" or
							// "off"
};


struct NumberCondition {
	enum Op {
		kEqual,
		kRange,			// a..b, inclusive
		kGreaterEqual,
		kLessEqual,
		kGreater,
		kLess
	};
	Op		op = kEqual;
	double	a = 0;
	double	b = 0;

	bool	Matches(double value) const;
};

// "10", "10..20", ">=100", "<=100", ">100", "<100" (spaces allowed).
bool ParseNumberCondition(const std::string& text, NumberCondition& out);


class CardQuery {
public:
	virtual					~CardQuery() {}
	// All conditions must hold (AND). Empty condition texts are ignored.
	// Returns the matching card IDs in stack order. kInvalidArgument when
	// a number condition cannot be parsed (detail: the part id).
	virtual	Status			Find(const Stack& stack,
								const std::vector<FindCondition>& conditions,
								std::vector<std::string>& outCardIds) = 0;
};


class MemoryCardQuery : public CardQuery {
public:
	virtual	Status			Find(const Stack& stack,
								const std::vector<FindCondition>& conditions,
								std::vector<std::string>& outCardIds);
};


// Indexes (into the stack order) of the cards of `templateId` whose text
// parts contain `needle` (case-insensitive). Empty needle: all cards of the
// template. Empty templateId: all cards.
std::vector<int> QuickSearch(const Stack& stack, const std::string& templateId,
	const std::string& needle);


}	// namespace bdb

#endif	// BDB_QUERY_H
