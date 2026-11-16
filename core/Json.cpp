/*
 * BDB card database - core
 */
#include "Json.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>


namespace bdb {


static const std::string kEmptyString;


bool
Json::AsBool(bool fallback) const
{
	if (const bool* value = std::get_if<bool>(&fValue))
		return *value;
	return fallback;
}


int64_t
Json::AsInt(int64_t fallback) const
{
	if (const int64_t* value = std::get_if<int64_t>(&fValue))
		return *value;
	return fallback;
}


double
Json::AsDouble(double fallback) const
{
	if (const double* value = std::get_if<double>(&fValue))
		return *value;
	if (const int64_t* value = std::get_if<int64_t>(&fValue))
		return double(*value);
	return fallback;
}


const std::string&
Json::AsString() const
{
	if (const std::string* value = std::get_if<std::string>(&fValue))
		return *value;
	return kEmptyString;
}


JsonArray&
Json::Array()
{
	if (!IsArray())
		fValue = JsonArray();
	return std::get<JsonArray>(fValue);
}


const JsonArray&
Json::Array() const
{
	static const JsonArray kEmpty;
	if (const JsonArray* value = std::get_if<JsonArray>(&fValue))
		return *value;
	return kEmpty;
}


JsonObject&
Json::Object()
{
	if (!IsObject())
		fValue = JsonObject();
	return std::get<JsonObject>(fValue);
}


const JsonObject&
Json::Object() const
{
	static const JsonObject kEmpty;
	if (const JsonObject* value = std::get_if<JsonObject>(&fValue))
		return *value;
	return kEmpty;
}


const Json*
Json::Find(const std::string& key) const
{
	const JsonObject* object = std::get_if<JsonObject>(&fValue);
	if (object == nullptr)
		return nullptr;
	for (const auto& entry : *object) {
		if (entry.first == key)
			return &entry.second;
	}
	return nullptr;
}


Json*
Json::Find(const std::string& key)
{
	JsonObject* object = std::get_if<JsonObject>(&fValue);
	if (object == nullptr)
		return nullptr;
	for (auto& entry : *object) {
		if (entry.first == key)
			return &entry.second;
	}
	return nullptr;
}


void
Json::Set(const std::string& key, Json value)
{
	if (Json* existing = Find(key)) {
		*existing = std::move(value);
		return;
	}
	Object().emplace_back(key, std::move(value));
}


bool
Json::Remove(const std::string& key)
{
	JsonObject* object = std::get_if<JsonObject>(&fValue);
	if (object == nullptr)
		return false;
	for (auto it = object->begin(); it != object->end(); ++it) {
		if (it->first == key) {
			object->erase(it);
			return true;
		}
	}
	return false;
}


void
Json::SetAt(size_t index, const std::string& key, Json value)
{
	Remove(key);
	JsonObject& object = Object();
	if (index > object.size())
		index = object.size();
	object.emplace(object.begin() + index, key, std::move(value));
}


std::string
Json::GetString(const std::string& key, const std::string& fallback) const
{
	const Json* value = Find(key);
	if (value == nullptr || !value->IsString())
		return fallback;
	return value->AsString();
}


int64_t
Json::GetInt(const std::string& key, int64_t fallback) const
{
	const Json* value = Find(key);
	if (value == nullptr || !value->IsInt())
		return fallback;
	return value->AsInt();
}


bool
Json::operator==(const Json& other) const
{
	// Numbers compare by value regardless of int/double representation.
	if (IsNumber() && other.IsNumber()) {
		if (IsInt() && other.IsInt())
			return AsInt() == other.AsInt();
		return AsDouble() == other.AsDouble();
	}
	if (GetType() != other.GetType())
		return false;
	switch (GetType()) {
		case kNull:
			return true;
		case kBool:
			return AsBool() == other.AsBool();
		case kString:
			return AsString() == other.AsString();
		case kArray:
			return Array() == other.Array();
		case kObject:
			return Object() == other.Object();
		default:
			return false;
	}
}


// #pragma mark - numbers


std::string
FormatNumber(double value)
{
	if (!std::isfinite(value))
		return "null";
	if (value == std::floor(value) && std::fabs(value) < 9.0e15) {
		char buffer[32];
		auto result = std::to_chars(buffer, buffer + sizeof(buffer),
			int64_t(value));
		return std::string(buffer, result.ptr);
	}
	char buffer[64];
	auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
	return std::string(buffer, result.ptr);
}


bool
ParseDecimal(const std::string& text, double& out)
{
	const char* start = text.c_str();
	const char* end = start + text.size();
	const char* p = start;
	if (p < end && (*p == '+' || *p == '-'))
		p++;
	const char* digitsStart = p;
	int digits = 0;
	while (p < end && *p >= '0' && *p <= '9') {
		p++;
		digits++;
	}
	if (p < end && *p == '.') {
		p++;
		while (p < end && *p >= '0' && *p <= '9') {
			p++;
			digits++;
		}
	}
	if (digits == 0)
		return false;
	if (p < end && (*p == 'e' || *p == 'E')) {
		p++;
		if (p < end && (*p == '+' || *p == '-'))
			p++;
		int exponentDigits = 0;
		while (p < end && *p >= '0' && *p <= '9') {
			p++;
			exponentDigits++;
		}
		if (exponentDigits == 0)
			return false;
	}
	if (p != end)
		return false;

	// from_chars does not accept a leading '+'.
	const char* parseStart = *start == '+' ? start + 1 : start;
	(void)digitsStart;
	double value = 0;
	auto result = std::from_chars(parseStart, end, value);
	if (result.ec != std::errc() || result.ptr != end)
		return false;
	if (!std::isfinite(value))
		return false;
	if (value == 0)
		value = 0;	// normalize -0
	out = value;
	return true;
}


// #pragma mark - writer


void
AppendJsonString(std::string& out, const std::string& value)
{
	out += '"';
	for (unsigned char c : value) {
		switch (c) {
			case '"':
				out += "\\\"";
				break;
			case '\\':
				out += "\\\\";
				break;
			case '\n':
				out += "\\n";
				break;
			case '\r':
				out += "\\r";
				break;
			case '\t':
				out += "\\t";
				break;
			case '\b':
				out += "\\b";
				break;
			case '\f':
				out += "\\f";
				break;
			default:
				if (c < 0x20 || c == 0x7f) {
					char buffer[8];
					snprintf(buffer, sizeof(buffer), "\\u%04x", c);
					out += buffer;
				} else
					out += char(c);
				break;
		}
	}
	out += '"';
}


static void
AppendIndent(std::string& out, int indent)
{
	out.append(size_t(indent) * 2, ' ');
}


void
Json::_Dump(std::string& out, int indent, bool pretty) const
{
	switch (GetType()) {
		case kNull:
			out += "null";
			break;
		case kBool:
			out += AsBool() ? "true" : "false";
			break;
		case kInt:
		{
			char buffer[32];
			auto result = std::to_chars(buffer, buffer + sizeof(buffer),
				AsInt());
			out.append(buffer, result.ptr);
			break;
		}
		case kDouble:
			out += FormatNumber(AsDouble());
			break;
		case kString:
			AppendJsonString(out, AsString());
			break;
		case kArray:
		{
			const JsonArray& array = Array();
			if (array.empty()) {
				out += "[]";
				break;
			}
			// Arrays of plain scalars (rect, ...) stay on one line.
			bool scalars = true;
			for (const Json& item : array) {
				if (item.IsArray() || item.IsObject()) {
					scalars = false;
					break;
				}
			}
			if (scalars || !pretty) {
				out += '[';
				for (size_t i = 0; i < array.size(); i++) {
					if (i > 0)
						out += pretty ? ", " : ",";
					array[i]._Dump(out, indent, pretty);
				}
				out += ']';
				break;
			}
			out += "[\n";
			for (size_t i = 0; i < array.size(); i++) {
				AppendIndent(out, indent + 1);
				array[i]._Dump(out, indent + 1, pretty);
				if (i + 1 < array.size())
					out += ',';
				out += '\n';
			}
			AppendIndent(out, indent);
			out += ']';
			break;
		}
		case kObject:
		{
			const JsonObject& object = Object();
			if (object.empty()) {
				out += "{}";
				break;
			}
			out += pretty ? "{\n" : "{";
			for (size_t i = 0; i < object.size(); i++) {
				if (pretty)
					AppendIndent(out, indent + 1);
				AppendJsonString(out, object[i].first);
				out += pretty ? ": " : ":";
				object[i].second._Dump(out, indent + 1, pretty);
				if (i + 1 < object.size())
					out += ',';
				if (pretty)
					out += '\n';
			}
			if (pretty)
				AppendIndent(out, indent);
			out += '}';
			break;
		}
	}
}


std::string
Json::Dump(bool pretty) const
{
	std::string out;
	_Dump(out, 0, pretty);
	if (pretty)
		out += '\n';
	return out;
}


// #pragma mark - parser


namespace {


class Parser {
public:
	Parser(const std::string& text, int maxDepth)
		:
		fText(text),
		fPosition(0),
		fMaxDepth(maxDepth)
	{
	}

	Status Parse(Json& out)
	{
		_SkipSpace();
		Status status = _ParseValue(out, 0);
		if (!status)
			return status;
		_SkipSpace();
		if (fPosition != fText.size())
			return _Error("trailing characters");
		return Status::Ok();
	}

private:
	Status _Error(const char* message)
	{
		return Status(ErrorKind::kInvalidFormat, std::string(message)
			+ " at offset " + std::to_string(fPosition));
	}

	void _SkipSpace()
	{
		while (fPosition < fText.size()) {
			char c = fText[fPosition];
			if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
				break;
			fPosition++;
		}
	}

	bool _Consume(const char* literal)
	{
		size_t length = strlen(literal);
		if (fText.compare(fPosition, length, literal) != 0)
			return false;
		fPosition += length;
		return true;
	}

	Status _ParseValue(Json& out, int depth)
	{
		if (fPosition >= fText.size())
			return _Error("unexpected end");
		char c = fText[fPosition];
		switch (c) {
			case '{':
				return _ParseObject(out, depth + 1);
			case '[':
				return _ParseArray(out, depth + 1);
			case '"':
			{
				std::string value;
				Status status = _ParseString(value);
				if (status)
					out = Json(std::move(value));
				return status;
			}
			case 't':
				if (_Consume("true")) {
					out = Json(true);
					return Status::Ok();
				}
				return _Error("invalid literal");
			case 'f':
				if (_Consume("false")) {
					out = Json(false);
					return Status::Ok();
				}
				return _Error("invalid literal");
			case 'n':
				if (_Consume("null")) {
					out = Json();
					return Status::Ok();
				}
				return _Error("invalid literal");
			default:
				if (c == '-' || (c >= '0' && c <= '9'))
					return _ParseNumber(out);
				return _Error("unexpected character");
		}
	}

	Status _ParseObject(Json& out, int depth)
	{
		if (depth > fMaxDepth)
			return Status(ErrorKind::kLimitExceeded, "nesting too deep");
		fPosition++;	// '{'
		JsonObject object;
		std::set<std::string> keys;
		_SkipSpace();
		if (fPosition < fText.size() && fText[fPosition] == '}') {
			fPosition++;
			out = Json(std::move(object));
			return Status::Ok();
		}
		while (true) {
			_SkipSpace();
			if (fPosition >= fText.size() || fText[fPosition] != '"')
				return _Error("expected key");
			std::string key;
			Status status = _ParseString(key);
			if (!status)
				return status;
			if (!keys.insert(key).second)
				return _Error("duplicate key");
			_SkipSpace();
			if (fPosition >= fText.size() || fText[fPosition] != ':')
				return _Error("expected ':'");
			fPosition++;
			_SkipSpace();
			Json value;
			status = _ParseValue(value, depth);
			if (!status)
				return status;
			object.emplace_back(std::move(key), std::move(value));
			_SkipSpace();
			if (fPosition >= fText.size())
				return _Error("unexpected end");
			if (fText[fPosition] == ',') {
				fPosition++;
				continue;
			}
			if (fText[fPosition] == '}') {
				fPosition++;
				break;
			}
			return _Error("expected ',' or '}'");
		}
		out = Json(std::move(object));
		return Status::Ok();
	}

	Status _ParseArray(Json& out, int depth)
	{
		if (depth > fMaxDepth)
			return Status(ErrorKind::kLimitExceeded, "nesting too deep");
		fPosition++;	// '['
		JsonArray array;
		_SkipSpace();
		if (fPosition < fText.size() && fText[fPosition] == ']') {
			fPosition++;
			out = Json(std::move(array));
			return Status::Ok();
		}
		while (true) {
			_SkipSpace();
			Json value;
			Status status = _ParseValue(value, depth);
			if (!status)
				return status;
			array.push_back(std::move(value));
			_SkipSpace();
			if (fPosition >= fText.size())
				return _Error("unexpected end");
			if (fText[fPosition] == ',') {
				fPosition++;
				continue;
			}
			if (fText[fPosition] == ']') {
				fPosition++;
				break;
			}
			return _Error("expected ',' or ']'");
		}
		out = Json(std::move(array));
		return Status::Ok();
	}

	static void _AppendUtf8(std::string& out, uint32_t codePoint)
	{
		if (codePoint < 0x80)
			out += char(codePoint);
		else if (codePoint < 0x800) {
			out += char(0xc0 | (codePoint >> 6));
			out += char(0x80 | (codePoint & 0x3f));
		} else if (codePoint < 0x10000) {
			out += char(0xe0 | (codePoint >> 12));
			out += char(0x80 | ((codePoint >> 6) & 0x3f));
			out += char(0x80 | (codePoint & 0x3f));
		} else {
			out += char(0xf0 | (codePoint >> 18));
			out += char(0x80 | ((codePoint >> 12) & 0x3f));
			out += char(0x80 | ((codePoint >> 6) & 0x3f));
			out += char(0x80 | (codePoint & 0x3f));
		}
	}

	bool _ParseHex4(uint32_t& out)
	{
		if (fPosition + 4 > fText.size())
			return false;
		uint32_t value = 0;
		for (int i = 0; i < 4; i++) {
			char c = fText[fPosition + i];
			value <<= 4;
			if (c >= '0' && c <= '9')
				value |= uint32_t(c - '0');
			else if (c >= 'a' && c <= 'f')
				value |= uint32_t(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F')
				value |= uint32_t(c - 'A' + 10);
			else
				return false;
		}
		fPosition += 4;
		out = value;
		return true;
	}

	// Validates one UTF-8 sequence starting at fPosition and copies it.
	bool _CopyUtf8(std::string& out)
	{
		unsigned char c = fText[fPosition];
		int length;
		uint32_t minimum;
		if (c < 0x80) {
			out += char(c);
			fPosition++;
			return true;
		} else if ((c & 0xe0) == 0xc0) {
			length = 2;
			minimum = 0x80;
		} else if ((c & 0xf0) == 0xe0) {
			length = 3;
			minimum = 0x800;
		} else if ((c & 0xf8) == 0xf0) {
			length = 4;
			minimum = 0x10000;
		} else
			return false;
		if (fPosition + length > fText.size())
			return false;
		uint32_t codePoint = c & (0x7f >> length);
		for (int i = 1; i < length; i++) {
			unsigned char next = fText[fPosition + i];
			if ((next & 0xc0) != 0x80)
				return false;
			codePoint = (codePoint << 6) | (next & 0x3f);
		}
		if (codePoint < minimum || codePoint > 0x10ffff
			|| (codePoint >= 0xd800 && codePoint <= 0xdfff)) {
			return false;
		}
		out.append(fText, fPosition, length);
		fPosition += length;
		return true;
	}

	Status _ParseString(std::string& out)
	{
		fPosition++;	// '"'
		while (true) {
			if (fPosition >= fText.size())
				return _Error("unterminated string");
			unsigned char c = fText[fPosition];
			if (c == '"') {
				fPosition++;
				return Status::Ok();
			}
			if (c < 0x20)
				return _Error("control character in string");
			if (c != '\\') {
				if (!_CopyUtf8(out))
					return _Error("invalid UTF-8");
				continue;
			}
			fPosition++;
			if (fPosition >= fText.size())
				return _Error("unterminated escape");
			char escape = fText[fPosition++];
			switch (escape) {
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u':
				{
					uint32_t codePoint;
					if (!_ParseHex4(codePoint))
						return _Error("invalid \\u escape");
					if (codePoint >= 0xd800 && codePoint <= 0xdbff) {
						uint32_t low;
						if (!_Consume("\\u") || !_ParseHex4(low)
							|| low < 0xdc00 || low > 0xdfff) {
							return _Error("invalid surrogate pair");
						}
						codePoint = 0x10000 + ((codePoint - 0xd800) << 10)
							+ (low - 0xdc00);
					} else if (codePoint >= 0xdc00 && codePoint <= 0xdfff)
						return _Error("invalid surrogate");
					_AppendUtf8(out, codePoint);
					break;
				}
				default:
					return _Error("invalid escape");
			}
		}
	}

	Status _ParseNumber(Json& out)
	{
		size_t start = fPosition;
		bool isInteger = true;
		if (fText[fPosition] == '-')
			fPosition++;
		if (fPosition >= fText.size())
			return _Error("invalid number");
		if (fText[fPosition] == '0')
			fPosition++;
		else if (fText[fPosition] >= '1' && fText[fPosition] <= '9') {
			while (fPosition < fText.size() && fText[fPosition] >= '0'
				&& fText[fPosition] <= '9') {
				fPosition++;
			}
		} else
			return _Error("invalid number");
		if (fPosition < fText.size() && fText[fPosition] == '.') {
			isInteger = false;
			fPosition++;
			size_t digits = fPosition;
			while (fPosition < fText.size() && fText[fPosition] >= '0'
				&& fText[fPosition] <= '9') {
				fPosition++;
			}
			if (digits == fPosition)
				return _Error("invalid number");
		}
		if (fPosition < fText.size()
			&& (fText[fPosition] == 'e' || fText[fPosition] == 'E')) {
			isInteger = false;
			fPosition++;
			if (fPosition < fText.size()
				&& (fText[fPosition] == '+' || fText[fPosition] == '-')) {
				fPosition++;
			}
			size_t digits = fPosition;
			while (fPosition < fText.size() && fText[fPosition] >= '0'
				&& fText[fPosition] <= '9') {
				fPosition++;
			}
			if (digits == fPosition)
				return _Error("invalid number");
		}
		const char* begin = fText.data() + start;
		const char* end = fText.data() + fPosition;
		if (isInteger) {
			int64_t value = 0;
			auto result = std::from_chars(begin, end, value);
			if (result.ec == std::errc() && result.ptr == end) {
				out = Json(value);
				return Status::Ok();
			}
			// Out of int64 range: fall through to double.
		}
		double value = 0;
		auto result = std::from_chars(begin, end, value);
		if (result.ec != std::errc() || result.ptr != end
			|| !std::isfinite(value)) {
			return _Error("number out of range");
		}
		out = Json(value);
		return Status::Ok();
	}

	const std::string&	fText;
	size_t				fPosition;
	int					fMaxDepth;
};


}	// namespace


Status
Json::Parse(const std::string& text, Json& out, int maxDepth)
{
	Parser parser(text, maxDepth);
	return parser.Parse(out);
}


}	// namespace bdb
