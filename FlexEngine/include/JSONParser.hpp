#pragma once

#include <string>

#include "JSONTypes.hpp"

/*
 * Parses standard JSON files
 *
 * Supports the following:
 * + single line comments like in C - denoted as "//"
 * + lists of objects (e.g. `[ { "A" : "ay", "B" : "bee" }, { "C" : "see", "D" : "dee" } ]` )
 * + lists of fields (e.g. `[ "A" : "ay", "B" : "bee", "C" : "see", "D" : "dee" ]` )
 * + lists of simple values (e.g. `[ "A", "B", "C" ]`, or `[ 1, 2, 3 ]` )
 *
 */
namespace flex
{
	class JSONParser
	{
	public:
		JSONParser() = delete;

		/*
		* Parses a JSON file located at filePath and stores the result in parsedFile
		* Returns true if the file was parsed successfully
		*/
		static bool ParseFromFile(const std::string& filePath, JSONObject& rootObject);

		/*
		* Returns true if the file was parsed successfully
		*/
		static bool Parse(const std::string& fileContents, JSONObject& rootObject);

		static void ClearErrors();

		static const char* GetErrorString();

	private:
		/*
		* Parses an object starting at offset
		* Expects offset to point to the opening '{'
		* Returns true if the parse was successful
		*/
		static bool ParseObject(const std::string& fileContents, i32* offset, JSONObject& outObject);

		/*
		* Parses a single field (and recursively parses all children)
		* Returns true if parse was successful
		*/
		static bool ParseField(const std::string& fileContents, i32* offset, JSONField& field);

		/*
		* Parses an array of fields
		* Returns true if parse was successful
		* Fails when array type is
		*/
		static bool ParseArray(const std::string& fileContents, size_t quoteEnd, i32* offset, const std::string& fieldName, std::vector<JSONField>& fields);

		static bool ParseValue(ValueType fieldType, const std::string& fieldName, const std::string& fileContents, size_t quoteEnd, i32* offset, JSONValue& outValue);

		/*
		 * Expects offset to point at the opening bracket
		 * Returns the index of the closing bracket for the given opening bracket - (, [, and { are allowed
		 * Returns -1 if no matching bracket is found
		*/
		static i32 MatchingBracket(char openingBracket, const std::string& fileContents, i32 offset);

		static bool ReadNumericField(const std::string& fileContents, std::string& outValueStr, i32* offset);

		static std::string s_ErrorStr;
	};
} // namespace flex
