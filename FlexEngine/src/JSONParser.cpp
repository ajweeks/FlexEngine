
#include "stdafx.hpp"

#include "JSONParser.hpp"

#include "Helpers.hpp"
#include "Scene/GameObject.hpp"

namespace flex
{
	std::string JSONParser::s_ErrorStr;

	// TODO: Track line & column index to generate better error messages

	namespace
	{
		// Returns the index of the quote which closes the string opened by the quote at openQuoteIndex, skipping
		// backslash-escaped characters, or npos if the string is never closed
		size_t FindClosingQuote(const std::string& str, size_t openQuoteIndex)
		{
			for (size_t i = openQuoteIndex + 1; i < str.size(); ++i)
			{
				if (str[i] == '\\')
				{
					++i; // Skip escaped character
				}
				else if (str[i] == '\"')
				{
					return i;
				}
			}
			return std::string::npos;
		}

		// Reverses JSONField::ToString's escaping (\\ & \"). Other backslashes are kept as they are.
		std::string Unescape(const std::string& str)
		{
			if (str.find('\\') == std::string::npos)
			{
				return str;
			}

			std::string result;
			result.reserve(str.size());
			for (size_t i = 0; i < str.size(); ++i)
			{
				if (str[i] == '\\' && i + 1 < str.size() && (str[i + 1] == '\\' || str[i + 1] == '\"'))
				{
					++i;
				}
				result.push_back(str[i]);
			}
			return result;
		}

		// Returns true if every comma separated part of str is a number (e.g. "1.0, 2, -3.5")
		bool IsNumberList(const std::string& str)
		{
			std::vector<std::string> parts = SplitNoStrip(str, ',');
			for (const std::string& part : parts)
			{
				std::string trimmed = Trim(part);
				if (trimmed.empty())
				{
					return false;
				}
				char* end = nullptr;
				strtod(trimmed.c_str(), &end);
				if (end != trimmed.c_str() + trimmed.size())
				{
					return false;
				}
			}
			return true;
		}

		// Avoids copying the rest of the file just to peek at a value's first few characters
		ValueType TypeAt(const std::string& str, i32 offset)
		{
			if (offset < 0 || offset >= (i32)str.size())
			{
				return ValueType::UNINITIALIZED;
			}
			// Long enough for any number we write (incl. FLT_MAX in fixed notation)
			const size_t maxLookahead = 96;
			return JSONValue::TypeFromChar(str[offset], str.substr((size_t)offset + 1, maxLookahead));
		}

		// Like NextNonAlphaNumeric, but returns the string's length (rather than -1) when there are none
		i32 EndOfAlphaNumeric(const std::string& str, i32 offset)
		{
			i32 result = NextNonAlphaNumeric(str, offset);
			return result == -1 ? (i32)str.size() : result;
		}
	} // namespace

	bool JSONParser::ParseFromFile(const std::string& filePath, JSONObject& rootObject)
	{
		PROFILE_AUTO("JSONParser ParseFromFile");

		std::string fileContents;
		if (!ReadFile(filePath, fileContents, false))
		{
			PrintError("Couldn't find JSON file: %s\n", filePath.c_str());
			return false;
		}

		return Parse(fileContents, rootObject);
	}

	bool JSONParser::Parse(const std::string& fileContents, JSONObject& rootObject)
	{
		PROFILE_AUTO("JSONParser Parse");

		JSONParser::ClearErrors();

		const std::string& dirtyFileContents = fileContents;

		std::string cleanFileContents;
		cleanFileContents.reserve(dirtyFileContents.size());
		bool bInQuote = false;
		i32 i = 0;
		while (i < (i32)dirtyFileContents.size())
		{
			char currentChar = dirtyFileContents[i];
			if (currentChar == '\"')
			{
				bInQuote = !bInQuote;
				cleanFileContents.push_back(currentChar);
			}
			else
			{
				if (bInQuote)
				{
					cleanFileContents.push_back(currentChar);
					if (currentChar == '\\' && i + 1 < (i32)dirtyFileContents.size())
					{
						// Keep escaped characters (incl. quotes) as they are
						cleanFileContents.push_back(dirtyFileContents[i + 1]);
						++i;
					}
				}
				else
				{
					// Skip single line comments
					if (currentChar == '/' && i + 1 < (i32)dirtyFileContents.size() && dirtyFileContents[i + 1] == '/')
					{
						size_t endLine = dirtyFileContents.find('\n', i + 2);
						if (endLine == std::string::npos)
						{
							endLine = dirtyFileContents.size();
						}

						i = (i32)endLine;
						continue;
					}
					// Skip all whitespace characters
					else if (!IsSpace(currentChar))
					{
						cleanFileContents.push_back(currentChar);
					}
				}
			}

			++i;
		}

		rootObject = {};

		if (cleanFileContents == "{}" || cleanFileContents.empty())
		{
			return true; // Empty files are valid
		}

		if (cleanFileContents[0] == '{' && MatchingBracket('{', cleanFileContents, 0) != (i32)cleanFileContents.size() - 1)
		{
			s_ErrorStr = "Root object's closing bracket is missing (or isn't at the end of the file)";
			return false;
		}

		i32 fileContentOffset = 0;
		bool bParseSucceeded = true;
		bool bParsing = true;
		while (bParsing)
		{
			JSONField field;
			bParsing = ParseField(cleanFileContents, &fileContentOffset, field);
			if (!bParsing)
			{
				bParseSucceeded = false;
				break;
			}
			rootObject.fields.push_back(field);

			if (fileContentOffset >= (i32)(cleanFileContents.size() - 1))
			{
				// We've reached the end of the file!
				bParsing = false;
			}
			else if (cleanFileContents[fileContentOffset] == ',')
			{
				++fileContentOffset;
			}
		}

		if (!s_ErrorStr.empty())
		{
			bParseSucceeded = false;
		}

		return bParseSucceeded;
	}

	JSONValue JSONParser::ValueFromString(const std::string& stringValue)
	{
		JSONValue value(stringValue);

		// Is this string actually representing a vector?
		// (Only when it's purely numbers, so that text containing commas stays a string)
		if (Contains(stringValue, ',') && IsNumberList(stringValue))
		{
			glm::vec4 vec4Value;
			glm::vec3 vec3Value;
			glm::vec2 vec2Value;
			if (TryParseVec4(stringValue, vec4Value, -1.0f))
			{
				value = JSONValue(vec4Value);
			}
			else if (TryParseVec3(stringValue, vec3Value))
			{
				value = JSONValue(vec3Value);
			}
			else if (TryParseVec2(stringValue, vec2Value))
			{
				value = JSONValue(vec2Value);
			}
			// Keep the original text so the value can still be read as a string
			value.strValue = stringValue;
		}
		return value;
	}

	void JSONParser::ClearErrors()
	{
		s_ErrorStr.clear();
	}

	const char* JSONParser::GetErrorString()
	{
		return s_ErrorStr.c_str();
	}

	bool JSONParser::ParseObject(const std::string& fileContents, i32* offset, JSONObject& outObject)
	{
		PROFILE_AUTO("JSONParser ParseObject");

		if (*offset < 0 || *offset >= (i32)fileContents.size() || fileContents[*offset] != '{')
		{
			s_ErrorStr = "Expected '{' at offset " + std::to_string(*offset);
			return false;
		}

		i32 objectClosingBracket = MatchingBracket('{', fileContents, *offset);
		if (objectClosingBracket == -1)
		{
			s_ErrorStr = "Couldn't find matching bracket for '{'";
			return false;
		}

		if (objectClosingBracket == *offset + 1)
		{
			// Empty block
			*offset = objectClosingBracket + 1;
			if (*offset < (i32)fileContents.size() && fileContents[*offset] == ',')
			{
				*offset += 1;
			}
			return true;
		}

		*offset += 1; // Advance past opening bracket

		while (*offset < objectClosingBracket)
		{
			JSONField field;
			if (!ParseField(fileContents, offset, field))
			{
				return false;
			}
			outObject.fields.push_back(field);

			if (*offset > objectClosingBracket)
			{
				s_ErrorStr = "Field " + field.label + " extends past the end of its object";
				return false;
			}

			// (Missing commas between fields are tolerated)
			if (fileContents[*offset] == ',')
			{
				*offset += 1;
			}
		}

		*offset = objectClosingBracket + 1;

		if (*offset < (i32)fileContents.size() && fileContents[*offset] == ',')
		{
			*offset += 1;
		}

		return true;
	}

	bool JSONParser::ReadNumericField(const std::string& fileContents, std::string& outValueStr, i32* offset)
	{
		PROFILE_AUTO("JSONParser ReadNumericField");

		size_t start = (size_t)*offset;
		size_t nextNonAlphaNumeric = (size_t)EndOfAlphaNumeric(fileContents, (i32)start + 1);
		size_t length = nextNonAlphaNumeric - start;
		outValueStr = fileContents.substr(start, length);
		if (outValueStr.empty())
		{
			return false;
		}

		if (outValueStr == "nan" || outValueStr == "-nan")
		{
			PrintWarn("Found NaN in json string, replacing with 0\n");
			outValueStr = "0";
		}

		*offset = (i32)nextNonAlphaNumeric;
		return true;
	}

	bool JSONParser::ParseValue(ValueType fieldType, const std::string& fieldName, const std::string& fileContents, i32* offset, JSONValue& outValue)
	{
		PROFILE_AUTO("JSONParser ParseValue");

		if (*offset < 0 || *offset >= (i32)fileContents.size())
		{
			s_ErrorStr = "Missing value for " + fieldName;
			return false;
		}

		switch (fieldType)
		{
		case ValueType::STRING:
		{
			size_t strQuoteStart = fileContents.find('\"', *offset);

			if (strQuoteStart == std::string::npos)
			{
				s_ErrorStr = "Couldn't find quote after offset " + std::to_string(*offset);
				return false;
			}

			size_t strQuoteEnd = FindClosingQuote(fileContents, strQuoteStart);
			if (strQuoteEnd == std::string::npos)
			{
				s_ErrorStr = "Couldn't find end quote after offset " + std::to_string(*offset);
				return false;
			}

			std::string stringValue = Unescape(fileContents.substr(strQuoteStart + 1, strQuoteEnd - (strQuoteStart + 1)));
			outValue = ValueFromString(stringValue);

			*offset = (i32)strQuoteEnd + 1;
		} break;
		case ValueType::INT:
		case ValueType::UINT:
		case ValueType::LONG:
		case ValueType::ULONG:
		{
			std::string valueStr;
			if (!ReadNumericField(fileContents, valueStr, offset))
			{
				s_ErrorStr = "Invalid number for " + fieldName;
				return false;
			}

			// Not std::stoi etc., which throw on invalid or out of range input
			const char* valueStart = valueStr.c_str();
			char* valueEnd = nullptr;
			switch (fieldType)
			{
			case ValueType::INT:
			{
				i64 value = (i64)strtoll(valueStart, &valueEnd, 10);
				outValue = JSONValue((i32)glm::clamp(value, (i64)INT_MIN, (i64)INT_MAX));
			} break;
			case ValueType::UINT:
			{
				u64 value = (u64)strtoull(valueStart, &valueEnd, 10);
				outValue = JSONValue((u32)glm::min(value, (u64)UINT_MAX));
			} break;
			case ValueType::LONG:
				outValue = JSONValue((i64)strtoll(valueStart, &valueEnd, 10));
				break;
			default:
				outValue = JSONValue((u64)strtoull(valueStart, &valueEnd, 10));
				break;
			}

			if (valueEnd == valueStart)
			{
				s_ErrorStr = "Invalid number for " + fieldName + ": " + valueStr;
				return false;
			}
		} break;
		case ValueType::FLOAT:
		{
			size_t floatStart = (size_t)*offset;
			size_t decimalIndex = fileContents.find('.', floatStart);
			if (decimalIndex == std::string::npos)
			{
				decimalIndex = floatStart;
			}
			size_t floatEnd = (size_t)EndOfAlphaNumeric(fileContents, (i32)decimalIndex + 1);
			size_t floatCharCount = floatEnd - floatStart;
			std::string floatStr = fileContents.substr(floatStart, floatCharCount);
			real floatValue = 0.0f;
			if (!floatStr.empty())
			{
				if (floatStr == "nan" || floatStr == "-nan")
				{
					PrintWarn("Found NaN in json string, replacing with 0.0f\n");
					floatValue = 0.0f;
				}
				else
				{
					// Not std::stof, which throws on invalid or out of range input
					char* floatStrEnd = nullptr;
					floatValue = strtof(floatStr.c_str(), &floatStrEnd);
					if (floatStrEnd == floatStr.c_str())
					{
						s_ErrorStr = "Invalid float for " + fieldName + ": " + floatStr;
						return false;
					}
					if (IsNanOrInf(floatValue))
					{
						floatValue = 0.0f;
					}
				}
			}
			outValue = JSONValue(floatValue);

			*offset = (i32)floatEnd;
		} break;
		case ValueType::BOOL:
		{
			// TODO: Be more strict here? (Require "true" or "false")
			char valueFirstChar = fileContents[*offset];
			bool boolValue = valueFirstChar == 't';
			outValue = JSONValue(boolValue);

			*offset = EndOfAlphaNumeric(fileContents, *offset + 1);
		} break;
		case ValueType::OBJECT:
		{
			JSONObject object;
			if (!ParseObject(fileContents, offset, object))
			{
				return false;
			}

			outValue = JSONValue(object);
		} break;
		case ValueType::OBJECT_ARRAY:
		{
			std::vector<JSONObject> objects;

			i32 arrayClosingBracket = MatchingBracket('[', fileContents, *offset);
			if (arrayClosingBracket == -1)
			{
				s_ErrorStr = "Couldn't find matching square bracket for " + fieldName;
				return false;
			}

			*offset += 1;

			while (*offset < arrayClosingBracket)
			{
				JSONObject object;
				if (!ParseObject(fileContents, offset, object))
				{
					return false;
				}

				objects.push_back(object);
			}

			if (*offset > arrayClosingBracket + 1)
			{
				s_ErrorStr = "Object array " + fieldName + " is malformed";
				return false;
			}

			*offset = arrayClosingBracket + 1;

			outValue = JSONValue(objects);
		} break;
		case ValueType::FIELD_ARRAY:
		{
			std::vector<JSONField> fields;

			if (ParseArray(fileContents, offset, fieldName, fields))
			{
				outValue = JSONValue(fields);
			}
			else
			{
				return false;
			}
		} break;
		case ValueType::UNINITIALIZED:
		default:
		{
			size_t valueEnd = (size_t)EndOfAlphaNumeric(fileContents, *offset);
			s_ErrorStr = "Unhandled JSON value type: " + fileContents.substr((size_t)*offset, valueEnd - (size_t)*offset);
			return false;
		} break;
		}

		return true;
	}

	bool JSONParser::ParseField(const std::string& fileContents, i32* offset, JSONField& field)
	{
		PROFILE_AUTO("JSONParser ParseField");

		if (*offset < 0 || *offset >= (i32)fileContents.size())
		{
			s_ErrorStr = "Unexpected end of file";
			return false;
		}

		size_t quoteStart = fileContents.find('\"', *offset);

		if (quoteStart == std::string::npos)
		{
			s_ErrorStr = "Couldn't find opening quote after offset " + std::to_string(*offset);
			return false;
		}

		size_t quoteEnd = FindClosingQuote(fileContents, quoteStart);
		if (quoteEnd == std::string::npos)
		{
			s_ErrorStr = "Couldn't find closing quote after offset " + std::to_string(*offset);
			return false;
		}

		field.label = Unescape(fileContents.substr(quoteStart + 1, quoteEnd - (quoteStart + 1)));
		*offset = (i32)quoteEnd + 1;

		if (*offset < (i32)fileContents.size() && fileContents[*offset] == ':') // Non field-array field
		{
			*offset += 1; // Advance past colon

			ValueType fieldType = TypeAt(fileContents, *offset);

			JSONValue newValue;
			if (ParseValue(fieldType, field.label, fileContents, offset, newValue))
			{
				field.value = newValue;
			}
			else
			{
				return false;
			}
		}
		else
		{
			s_ErrorStr = "Invalid json field " + field.label + ", expected ':'";
			return false;
		}

		return true;
	}

	bool JSONParser::ParseArray(const std::string& fileContents, i32* offset, const std::string& fieldName, std::vector<JSONField>& fields)
	{
		PROFILE_AUTO("JSONParser ParseArray");

		i32 arrayClosingBracket = MatchingBracket('[', fileContents, *offset);
		if (arrayClosingBracket == -1)
		{
			s_ErrorStr = "Couldn't find matching square bracket " + fieldName;
			return false;
		}

		*offset += 1; // Advance past opening bracket

		ValueType fieldType = TypeAt(fileContents, *offset);

		// Check what type of int the data requires
		if (fieldType == ValueType::INT || fieldType == ValueType::UINT ||
			fieldType == ValueType::LONG || fieldType == ValueType::ULONG)
		{
			bool bRequireSigned = false;
			bool bRequireLong = false;

			i32 offsetCopy = *offset;

			while (offsetCopy < arrayClosingBracket)
			{
				ValueType fieldType1 = TypeAt(fileContents, offsetCopy);

				JSONValue newValue;
				if (!ParseValue(fieldType1, fieldName, fileContents, &offsetCopy, newValue))
				{
					return false;
				}

				if (fieldType1 == ValueType::LONG || fieldType1 == ValueType::ULONG)
				{
					bRequireLong = true;
				}

				if (fieldType1 == ValueType::INT || fieldType1 == ValueType::LONG)
				{
					bRequireSigned = true;
				}

				if (fieldType1 == ValueType::ULONG && bRequireSigned)
				{
					s_ErrorStr = "Mismatched integer types found in array";
					return false;
				}

				if (newValue.type != fieldType1)
				{
					s_ErrorStr = "Mismatched types found in array " + fieldName;
					return false;
				}

				if (offsetCopy > arrayClosingBracket ||
					(fileContents[offsetCopy] != ',' &&
					fileContents[offsetCopy] != ']'))
				{
					s_ErrorStr = "Expected ',' or ']' after field array entry " + fieldName;
					return false;
				}

				if (fileContents[offsetCopy] == ',')
				{
					offsetCopy += 1;
				}
			}

			if (bRequireLong)
			{
				fieldType = bRequireSigned ? ValueType::LONG : ValueType::ULONG;
			}
			else
			{
				fieldType = bRequireSigned ? ValueType::INT : ValueType::UINT;
			}
		}

		while (*offset < arrayClosingBracket)
		{
			JSONValue newValue;
			if (!ParseValue(fieldType, fieldName, fileContents, offset, newValue))
			{
				return false;
			}

			if (newValue.type != fieldType)
			{
				s_ErrorStr = "Mismatched types found in array " + fieldName;
				return false;
			}

			if (*offset > arrayClosingBracket ||
				(fileContents[*offset] != ',' &&
				fileContents[*offset] != ']'))
			{
				s_ErrorStr = "Expected ',' or ']' after field array entry " + fieldName;
				return false;
			}

			fields.emplace_back("", newValue);

			if (fileContents[*offset] == ',')
			{
				*offset += 1;
			}
		}

		*offset = arrayClosingBracket + 1; // Advance past closing bracket

		return true;
	}

	i32 JSONParser::MatchingBracket(char openingBracket, const std::string& fileContents, i32 offset)
	{
		PROFILE_AUTO("JSONParser MatchingBracket");

		if (offset < 0 || offset >= (i32)fileContents.size() || fileContents[offset] != openingBracket)
		{
			s_ErrorStr = "Expected '" + std::string(1, openingBracket) + "' at offset " + std::to_string(offset);
			return -1;
		}

		char closingBracket;
		if (openingBracket == '[')
		{
			closingBracket = ']';
		}
		else if (openingBracket == '{')
		{
			closingBracket = '}';
		}
		else if (openingBracket == '(')
		{
			closingBracket = ')';
		}
		else
		{
			s_ErrorStr = "Unhandled opening bracket type: " + std::string(1, openingBracket);
			return -1;
		}

		i32 openSquareBracketCount = 0; // [
		i32 openCurlyBracketCount = 0;  // {
		i32 openParenthesesCount = 0;   // (

		bool bInQuotes = false;
		while (offset < (i32)fileContents.size())
		{
			char currentChar = fileContents[offset];

			if (bInQuotes && currentChar == '\\')
			{
				// Skip escaped character
				offset += 2;
				continue;
			}

			if (currentChar == '\"')
			{
				bInQuotes = !bInQuotes;
			}

			if (!bInQuotes)
			{
				if (currentChar == '[')
				{
					++openSquareBracketCount;
				}
				else if (currentChar == ']')
				{
					--openSquareBracketCount;
				}
				else if (currentChar == '{')
				{
					++openCurlyBracketCount;
				}
				else if (currentChar == '}')
				{
					--openCurlyBracketCount;
				}
				else if (currentChar == '(')
				{
					++openParenthesesCount;
				}
				else if (currentChar == ')')
				{
					--openParenthesesCount;
				}

				if (currentChar == closingBracket &&
					openSquareBracketCount == 0 &&
					openCurlyBracketCount == 0 &&
					openParenthesesCount == 0)
				{
					return offset;
				}
			}

			++offset;
		}

		return -1;
	}
} // namespace flex
