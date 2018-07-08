#pragma once

#include <string>
#include <vector>

#include "JSONTypes.hpp"

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

		/*
		* Parses a JSON file located at filePath and stores the result in parsedFile
		* Returns true if the file was parsed successfully
		*/
		static bool Parse(const std::string& filePath, JSONObject& rootObject);

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
		 * Expects offset to point at the opening bracket
		 * Returns the index of the closing bracket for the given opening bracket - (, [, and { are allowed
		 * Returns -1 if no matching bracket is found
		*/
		static i32 MatchingBracket(char openingBracket, const std::string& fileContents, i32 offset);
	};
} // namespace flex
