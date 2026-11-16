/*
 * BDB card database - core
 */
#include "Query.h"

#include "Json.h"
#include "Model.h"
#include "Stack.h"
#include "Text.h"


namespace bdb {


bool
NumberCondition::Matches(double value) const
{
	switch (op) {
		case kEqual:		return value == a;
		case kRange:		return value >= a && value <= b;
		case kGreaterEqual:	return value >= a;
		case kLessEqual:	return value <= a;
		case kGreater:		return value > a;
		case kLess:			return value < a;
	}
	return false;
}


bool
ParseNumberCondition(const std::string& input, NumberCondition& out)
{
	std::string text = TrimSpaces(input);
	if (text.empty())
		return false;
	size_t range = text.find("..");
	if (range != std::string::npos) {
		double a, b;
		if (!ParseDecimal(TrimSpaces(text.substr(0, range)), a)
			|| !ParseDecimal(TrimSpaces(text.substr(range + 2)), b)) {
			return false;
		}
		out.op = NumberCondition::kRange;
		out.a = a <= b ? a : b;
		out.b = a <= b ? b : a;
		return true;
	}
	static const struct {
		const char*			prefix;
		NumberCondition::Op	op;
	} kOperators[] = {
		{">=", NumberCondition::kGreaterEqual},
		{"<=", NumberCondition::kLessEqual},
		{">", NumberCondition::kGreater},
		{"<", NumberCondition::kLess},
		{"=", NumberCondition::kEqual}
	};
	NumberCondition::Op op = NumberCondition::kEqual;
	for (const auto& entry : kOperators) {
		std::string prefix = entry.prefix;
		if (text.compare(0, prefix.size(), prefix) == 0) {
			op = entry.op;
			text = TrimSpaces(text.substr(prefix.size()));
			break;
		}
	}
	double a;
	if (!ParseDecimal(text, a))
		return false;
	out.op = op;
	out.a = a;
	return true;
}


namespace {


struct CompiledCondition {
	std::string		partId;
	bool			isNumber = false;
	NumberCondition	number;
	std::string		folded;
};


}	// namespace


Status
MemoryCardQuery::Find(const Stack& stack,
	const std::vector<FindCondition>& conditions,
	std::vector<std::string>& outCardIds)
{
	outCardIds.clear();
	// The conditions refer to part ids; the kind is taken per template
	// (a number condition is parsed lazily the first time it is needed).
	std::vector<CompiledCondition> compiled;
	for (const FindCondition& condition : conditions) {
		if (TrimSpaces(condition.text).empty())
			continue;
		CompiledCondition entry;
		entry.partId = condition.partId;
		entry.folded = FoldCase(condition.text);
		entry.isNumber = ParseNumberCondition(condition.text, entry.number);
		compiled.push_back(entry);
	}

	for (int i = 0; i < stack.CardCount(); i++) {
		const Card* card = stack.CardAt(i);
		const Template* tmpl = stack.FindTemplate(card->TemplateId());
		if (tmpl == nullptr) {
			if (compiled.empty())
				outCardIds.push_back(card->Id());
			continue;
		}
		bool matches = true;
		for (const CompiledCondition& condition : compiled) {
			const Json* part = tmpl->FindPart(condition.partId);
			if (part == nullptr) {
				matches = false;
				break;
			}
			std::string kind = part::Kind(*part);
			const Json& value = EffectiveValue(*card, *part);
			if (kind == kKindText) {
				if (FoldCase(value.AsString()).find(condition.folded)
						== std::string::npos) {
					matches = false;
					break;
				}
			} else if (kind == kKindNumber) {
				if (!condition.isNumber)
					return Status(ErrorKind::kInvalidArgument,
						condition.partId);
				if (!value.IsNumber()
					|| !condition.number.Matches(value.AsDouble())) {
					matches = false;
					break;
				}
			} else if (kind == kKindBool) {
				// "on" / "off" (UI 8); null counts as off (UI 5.3).
				std::string wanted = condition.folded;
				bool on = value.IsBool() && value.AsBool();
				if ((wanted == "on" || wanted == "true") != on) {
					matches = false;
					break;
				}
			} else {
				// image and others are not searchable (Phase 2).
				matches = false;
				break;
			}
		}
		if (matches)
			outCardIds.push_back(card->Id());
	}
	return Status::Ok();
}


std::vector<int>
QuickSearch(const Stack& stack, const std::string& templateId,
	const std::string& needle)
{
	std::vector<int> result;
	std::string folded = FoldCase(needle);
	for (int i = 0; i < stack.CardCount(); i++) {
		const Card* card = stack.CardAt(i);
		if (!templateId.empty() && card->TemplateId() != templateId)
			continue;
		if (folded.empty()) {
			result.push_back(i);
			continue;
		}
		const Template* tmpl = stack.FindTemplate(card->TemplateId());
		if (tmpl == nullptr)
			continue;
		for (const Json& part : tmpl->Parts()) {
			if (part::Kind(part) != kKindText)
				continue;
			if (FoldCase(EffectiveValue(*card, part).AsString()).find(folded)
					!= std::string::npos) {
				result.push_back(i);
				break;
			}
		}
	}
	return result;
}


}	// namespace bdb
