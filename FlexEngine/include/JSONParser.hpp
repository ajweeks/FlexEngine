#pragma once

#include <string>
#include <vector>

/*
 * Parses standard JSON files
 *
 * Supports the following:
 * + single line comments like in C - denoted as "//"
 * + lists of objects (e.g. [ { "A" : "ay", "B" : "bee" }, { "C" : "see", "D" : "dee" } ] )
 * + lists of fields (e.g. [ "A" : "ay", "B" : "bee", "C" : "see", "D" : "dee" ] )
 * 
 * Does not currently support:
 * - lists of values (e.g. [ "A", "B", "C" ] )
 *
 */
namespace flex
{
	class JSONParser
	{
	public:
		JSONParser() = delete;

		struct JSONField;

		struct JSONObject
		{
			std::vector<JSONField> fields;
		};

		struct JSONValue
		{
			enum class Type
			{
				STRING,
				INT,
				FLOAT,
				BOOL,
				OBJECT,
				OBJECT_ARRAY,
				FIELD_ARRAY,
				UNINITIALIZED
			};

			static Type TypeFromChar(char c, const std::string& stringAfter);

			JSONValue();
			explicit JSONValue(const std::string& strValue);
			explicit JSONValue(i32 intValue);
			explicit JSONValue(real floatValue);
			explicit JSONValue(bool boolValue);
			explicit JSONValue(const JSONObject& objectValue);
			explicit JSONValue(const std::vector<JSONObject>& objectArrayValue);
			explicit JSONValue(const std::vector<JSONField>& fieldArrayValue);

			Type type;
			std::string strValue;
			i32 intValue = 0;
			real floatValue = 0.0f;
			bool boolValue = false;
			std::vector<JSONField> fieldArrayValue;
			std::vector<JSONObject> objectArrayValue;
			JSONObject objectValue;
		};

		struct JSONField
		{
			std::string label;
			JSONValue value;
		};


		struct ParsedFile
		{
			JSONObject rootObject;
		};

		static std::string Print(const JSONObject& object, i32 tabCount);

		static void Parse(const std::string& filePath, ParsedFile& parsedFile);

	private:
		static std::string Print(const JSONField& field, i32 tabCount);

		/*
		* Parses a single field (and recursively parses all children)
		* Returns true if parse was successful
		*/
		static bool ParseField(const std::string& fileContents, i32* offset, JSONField& field);

		/* 
		 * Returns the index of the next character which is not a letter or number starting from offset
		 * Returns -1 if no non alpha numeric characters exist after offset
		 */
		static i32 NextNonAlphaNumeric(const std::string& fileContents, i32 offset);

		/* 
		 * Expects offset to point at the opening bracket
		 * Returns the index of the closing bracket for the given opening bracket - (, [, and { are allowed
		 * Returns -1 if no matching bracket is found
		*/
		static i32 MatchingBracket(char openingBracket, const std::string& fileContents, i32 offset);

		/*
		 * Parses an object starting at offset
		 * Expects offset to point to the opening '{'
		 * Returns true if the parse was successful
		*/
		static bool ParseObject(const std::string& fileContents, i32* offset, JSONObject& outObject);

	};
} // namespace flex
