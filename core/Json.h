/*
 * BDB card database - core
 * Minimal JSON model that keeps key order and unknown keys (spec v0.2, 5.5),
 * detects duplicate keys and limits nesting depth (5.6), and distinguishes
 * integers from decimals so that seq/rev stay exact 64 bit values.
 */
#ifndef BDB_JSON_H
#define BDB_JSON_H

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "Status.h"


namespace bdb {


class Json;
typedef std::vector<Json> JsonArray;
typedef std::vector<std::pair<std::string, Json>> JsonObject;


class Json {
public:
	enum Type {
		kNull = 0,
		kBool,
		kInt,
		kDouble,
		kString,
		kArray,
		kObject
	};

								Json() {}
								Json(std::nullptr_t) {}
								Json(bool value) : fValue(value) {}
								Json(int value) : fValue(int64_t(value)) {}
								Json(int64_t value) : fValue(value) {}
								Json(double value) : fValue(value) {}
								Json(const char* value)
									: fValue(std::string(value)) {}
								Json(std::string value)
									: fValue(std::move(value)) {}
								Json(JsonArray value)
									: fValue(std::move(value)) {}
								Json(JsonObject value)
									: fValue(std::move(value)) {}

	static	Json				MakeObject() { return Json(JsonObject()); }
	static	Json				MakeArray() { return Json(JsonArray()); }

			Type				GetType() const
									{ return Type(fValue.index()); }
			bool				IsNull() const { return GetType() == kNull; }
			bool				IsBool() const { return GetType() == kBool; }
			bool				IsInt() const { return GetType() == kInt; }
			bool				IsDouble() const
									{ return GetType() == kDouble; }
			bool				IsNumber() const
									{ return IsInt() || IsDouble(); }
			bool				IsString() const
									{ return GetType() == kString; }
			bool				IsArray() const { return GetType() == kArray; }
			bool				IsObject() const
									{ return GetType() == kObject; }

			bool				AsBool(bool fallback = false) const;
			int64_t				AsInt(int64_t fallback = 0) const;
			double				AsDouble(double fallback = 0) const;
	const	std::string&		AsString() const;
			JsonArray&			Array();
	const	JsonArray&			Array() const;
			JsonObject&			Object();
	const	JsonObject&			Object() const;

	// Object helpers. Find() returns nullptr when absent or not an object.
	const	Json*				Find(const std::string& key) const;
			Json*				Find(const std::string& key);
			bool				Has(const std::string& key) const
									{ return Find(key) != nullptr; }
	// Sets an existing key in place (keeps its position) or appends it.
			void				Set(const std::string& key, Json value);
			bool				Remove(const std::string& key);
	// Puts the key at the given position (used for the "bdb" and
	// "format_version" leading keys, 5.2).
			void				SetAt(size_t index, const std::string& key,
									Json value);

	// Convenience getters for objects.
			std::string			GetString(const std::string& key,
									const std::string& fallback = "") const;
			int64_t				GetInt(const std::string& key,
									int64_t fallback = 0) const;

			bool				operator==(const Json& other) const;
			bool				operator!=(const Json& other) const
									{ return !(*this == other); }

	// Serialization: UTF-8, LF, 2 space indent, trailing LF.
			std::string			Dump(bool pretty = true) const;
	// Compact single-line output (log lines).
			std::string			DumpCompact() const { return Dump(false); }

	static	Status				Parse(const std::string& text, Json& out,
									int maxDepth = 32);

private:
			void				_Dump(std::string& out, int indent,
									bool pretty) const;

			std::variant<std::nullptr_t, bool, int64_t, double, std::string,
				JsonArray, JsonObject> fValue;
};


// Number formatting shared by the JSON writer and number -> text
// conversion (5.1, 20.2): shortest decimal text that reads back to the same
// value; integral values are written without a decimal point.
std::string FormatNumber(double value);
// Strict decimal parser used by text -> number conversion (20.2) and by the
// number field input: optional sign, digits, optional fraction and
// exponent. No thousands separators, no full-width digits.
bool ParseDecimal(const std::string& text, double& out);

void AppendJsonString(std::string& out, const std::string& value);


}	// namespace bdb

#endif	// BDB_JSON_H
