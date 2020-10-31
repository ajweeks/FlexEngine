
#include "stdafx.hpp"

#include "JSONParser.hpp"

#include "Helpers.hpp"
#include "Scene/GameObject.hpp"

namespace flex
{
	std::string JSONParser::s_ErrorStr;

	bool JSONParser::ParseFromFile(const std::string& filePath, JSONObject& rootObject)
	{
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
		JSONParser::ClearErrors();

		std::string dirtyFileContents = fileContents;

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
				}
				else
				{
					// Skip single line comments
					if (currentChar == '/' && dirtyFileContents[i + 1] == '/')
					{
						size_t endLine = dirtyFileContents.find('\n', i + 2);
						if (endLine == std::string::npos)
						{
							endLine = dirtyFileContents.size();
						}

						i += (u32)endLine - i;
						//fileContents.erase(i, endLine - i);
						continue;
					}
					// Skip all whitespace characters
					else if (!isspace(currentChar))
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

		i32 fileContentOffset = 0;
		bool bParseSucceeded = true;
		bool bParsing = true;
		while (bParsing)
		{
			JSONField field;
			bParsing = ParseField(cleanFileContents, &fileContentOffset, field);
			rootObject.fields.push_back(field);

			bParseSucceeded |= bParsing;

			if (fileContentOffset == (i32)(cleanFileContents.size() - 1))
			{
				// We've reached the end of the file!
				bParsing = false;
			}
		}

		if (!s_ErrorStr.empty())
		{
			bParseSucceeded = false;
		}

		return bParseSucceeded;
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
			return true;
		}

		bool bParsing = true;
		while (bParsing && *offset < objectClosingBracket)
		{
			JSONField field;
			bParsing = ParseField(fileContents, offset, field);
			outObject.fields.push_back(field);
		}

		*offset = objectClosingBracket + 1;

		if (fileContents[*offset] == ',')
		{
			*offset += 1;
		}

		return true;
	}

	bool JSONParser::ParseField(const std::string& fileContents, i32* offset, JSONField& field)
	{
		size_t quoteStart = fileContents.find('\"', *offset);

		if (quoteStart == std::string::npos)
		{
			s_ErrorStr = "Couldn't find opening quote after offset " + std::to_string(*offset);
			return false;
		}

		size_t quoteEnd = fileContents.find('\"', quoteStart + 1);
		if (quoteEnd == std::string::npos)
		{
			s_ErrorStr = "Couldn't find closing quote after offset " + std::to_string(*offset);
			return false;
		}

		field.label = fileContents.substr(quoteStart + 1, quoteEnd - (quoteStart + 1));
		*offset = (i32)quoteEnd + 1;

		if (fileContents[quoteEnd + 1] == ':') // Non field-array field
		{
			char valueFirstChar = fileContents[quoteEnd + 2];
			JSONValue::Type fieldType = JSONValue::TypeFromChar(valueFirstChar, fileContents.substr(quoteEnd + 3));

			switch (fieldType)
			{
			case JSONValue::Type::STRING:
			{
				size_t strQuoteStart = fileContents.find('\"', *offset);

				if (strQuoteStart == std::string::npos)
				{
					s_ErrorStr = "Couldn't find quote after offset " + std::to_string(*offset);
					return false;
				}

				size_t strQuoteEnd = strQuoteStart;
				do
				{
					strQuoteEnd = fileContents.find('\"', strQuoteEnd + 1);
				} while (strQuoteEnd != std::string::npos && fileContents[strQuoteEnd - 1] == '\\');

				if (strQuoteEnd == std::string::npos)
				{
					s_ErrorStr = "Couldn't find end quote after offset " + std::to_string(*offset);
					return false;
				}

				std::string stringValue = fileContents.substr(strQuoteStart + 1, strQuoteEnd - (strQuoteStart + 1));
				stringValue = Replace(stringValue, "\\\"", "\"");
				field.value = JSONValue(stringValue);

				*offset = (i32)strQuoteEnd + 1;
			} break;
			case JSONValue::Type::INT:
			{
				size_t intStart = quoteEnd + 2;
				size_t nextNonAlphaNumeric = NextNonAlphaNumeric(fileContents, (i32)intStart + 1);
				size_t intCharCount = nextNonAlphaNumeric - intStart;
				std::string intStr = fileContents.substr(intStart, intCharCount);
				i32 intValue = 0;
				if (!intStr.empty())
				{
					if (intStr == "nan" || intStr == "-nan")
					{
						PrintWarn("Found NaN in json string, replacing with 0\n");
						intValue = 0;
					}
					else
					{
						intValue = stoi(intStr);
					}
				}
				field.value = JSONValue(intValue);

				*offset = (i32)nextNonAlphaNumeric;
			} break;
			case JSONValue::Type::FLOAT:
			{
				size_t floatStart = quoteEnd + 2;
				size_t decimalIndex = fileContents.find('.', floatStart);
				size_t floatEnd = NextNonAlphaNumeric(fileContents, (i32)decimalIndex + 1);
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
						floatValue = stof(floatStr);
					}
				}
				field.value = JSONValue(floatValue);

				*offset = (i32)floatEnd;
			} break;
			case JSONValue::Type::BOOL:
			{
				bool boolValue = valueFirstChar == 't';
				field.value = JSONValue(boolValue);

				*offset = NextNonAlphaNumeric(fileContents, (i32)quoteEnd + 3);
			} break;
			case JSONValue::Type::OBJECT:
			{
				*offset = (i32)quoteEnd + 2;

				JSONObject object;
				ParseObject(fileContents, offset, object);

				field.value = JSONValue(object);
			} break;
			case JSONValue::Type::OBJECT_ARRAY:
			{
				std::vector<JSONObject> objects;

				*offset = (i32)quoteEnd + 2;

				i32 arrayClosingBracket = MatchingBracket('[', fileContents, *offset);
				if (arrayClosingBracket == -1)
				{
					s_ErrorStr = "Couldn't find matching square bracket for " + field.label;
					return false;
				}

				*offset += 1;

				while (*offset < arrayClosingBracket)
				{
					JSONObject object;
					ParseObject(fileContents, offset, object);

					objects.push_back(object);
				}

				*offset = arrayClosingBracket + 1;

				field.value = JSONValue(objects);
			} break;
			case JSONValue::Type::FIELD_ARRAY:
			{
				std::vector<JSONField> fields;

				*offset = (i32)quoteEnd + 2;

				i32 arrayClosingBracket = MatchingBracket('[', fileContents, *offset);
				if (arrayClosingBracket == -1)
				{
					s_ErrorStr = "Couldn't find matching square bracket " + field.label;
					return false;
				}

				*offset += 1;

				while (*offset < arrayClosingBracket)
				{
					JSONField fieldArrayEntry;
					ParseField(fileContents, offset, fieldArrayEntry);

					if (fileContents[*offset] != ',' &&
						fileContents[*offset] != '}' &&
						fileContents[*offset] != ']')
					{
						s_ErrorStr = "Expected , } or ] after field array entry " + field.label;
						return false;
					}

					fields.push_back(fieldArrayEntry);
				}

				*offset = arrayClosingBracket + 1;

				field.value = JSONValue(fields);
			} break;
			case JSONValue::Type::UNINITIALIZED:
			default:
			{
				size_t nextNonAlphaNumeric = NextNonAlphaNumeric(fileContents, *offset);
				s_ErrorStr = "Unhandled JSON value type: " + fileContents.substr(quoteEnd + 2, nextNonAlphaNumeric - (quoteEnd + 2));
				*offset = -1;
				return false;
			} break;
			}
		}
		else
		{
			field.value.type = JSONValue::Type::FIELD_ENTRY;
			// Field array entries have no data in their value field
		}

		return true;
	}

	i32 JSONParser::MatchingBracket(char openingBracket, const std::string& fileContents, i32 offset)
	{
		assert(fileContents[offset] == openingBracket);

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
			s_ErrorStr = "Unhandled opening bracket type: " + std::string(openingBracket, 1);
			return -1;
		}

		i32 openSquareBracketCount = 0; // [
		i32 openCurlyBracketCount = 0;  // {
		i32 openParenthesesCount = 0;   // (

		bool bInQuotes = false;
		while (offset < (i32)fileContents.size())
		{
			char currentChar = fileContents[offset];

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
