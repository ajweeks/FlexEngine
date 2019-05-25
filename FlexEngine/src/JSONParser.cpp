
#include "stdafx.hpp"

#include "JSONParser.hpp"

#include "Helpers.hpp"
#include "Scene/GameObject.hpp"

namespace flex
{
	bool JSONParser::Parse(const std::string& filePath, JSONObject& rootObject)
	{
		std::string dirtyFileContents;
		if (!ReadFile(filePath, dirtyFileContents, false))
		{
			PrintError("Couldn't find JSON file: %s\n", filePath.c_str());
			return false;
		}

		size_t firstBracket = dirtyFileContents.find('{');
		size_t lastBracket = dirtyFileContents.rfind('}');

		if (firstBracket == std::string::npos ||
			lastBracket == std::string::npos ||
			firstBracket > lastBracket)
		{
			PrintError("Failed to parse JSON file. No valid bracket pairs found in %s\n", filePath.c_str());
		}

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

						i += endLine - i;
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

		i32 fileContentOffset = 0;
		bool bParseSucceeded = true;
		bool bParsing = true;
		while (bParsing)
		{
			JSONField field;
			bParsing = ParseField(filePath, cleanFileContents, &fileContentOffset, field);
			rootObject.fields.push_back(field);

			bParseSucceeded |= bParsing;

			if (fileContentOffset == (i32)(cleanFileContents.size() - 1))
			{
				// We've reached the end of the file!
				bParsing = false;
			}
		}

		return bParseSucceeded;
	}

	bool JSONParser::ParseObject(const std::string& filePath, const std::string& fileContents, i32* offset, JSONObject& outObject)
	{
		i32 objectClosingBracket = MatchingBracket(filePath, '{', fileContents, *offset);
		if (objectClosingBracket == -1)
		{
			PrintError("Couldn't find matching bracket for '{' in %s\n", filePath.c_str());
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
			bParsing = ParseField(filePath, fileContents, offset, field);
			outObject.fields.push_back(field);
		}

		*offset = objectClosingBracket + 1;

		if (fileContents[*offset] == ',')
		{
			*offset += 1;
		}

		return true;
	}

	bool JSONParser::ParseField(const std::string& filePath, const std::string& fileContents, i32* offset, JSONField& field)
	{
		size_t quoteStart = fileContents.find('\"', *offset);

		if (quoteStart == std::string::npos)
		{
			PrintError("Couldn't find opening quote after offset %i in %s\n", *offset, filePath.c_str());
			return false;
		}

		size_t quoteEnd = fileContents.find('\"', quoteStart + 1);
		if (quoteEnd == std::string::npos)
		{
			PrintError("Couldn't find closing quote after offset %i in %s\n", *offset, filePath.c_str());
			return false;
		}

		field.label = fileContents.substr(quoteStart + 1, quoteEnd - (quoteStart + 1));
		*offset = quoteEnd + 1;

		if (fileContents[quoteEnd + 1] != ':')
		{
			PrintError("Invalidly formatted JSON file, ':' must occur after a field label - %s\n...\n", filePath.c_str());
			std::string surroundingText(fileContents.substr(quoteStart - 20, 40));
			PrintError("%s\n...\n", surroundingText.c_str());
			return false;
		}

		char valueFirstChar = fileContents[quoteEnd + 2];
		JSONValue::Type fieldType = JSONValue::TypeFromChar(valueFirstChar, fileContents.substr(quoteEnd + 3));

		switch (fieldType)
		{
		case JSONValue::Type::STRING:
		{
			size_t strQuoteStart = fileContents.find('\"', *offset);

			if (strQuoteStart == std::string::npos)
			{
				PrintError("Couldn't find quote after offset %i in %s\n", offset, filePath.c_str());
				return false;
			}

			size_t strQuoteEnd = fileContents.find('\"', strQuoteStart + 1);
			if (strQuoteEnd == std::string::npos)
			{
				PrintError("Couldn't find end quote after offset %i, %s\n", *offset, filePath.c_str());
				return false;
			}

			std::string stringValue = fileContents.substr(strQuoteStart + 1, strQuoteEnd - (strQuoteStart + 1));
			field.value = JSONValue(stringValue);

			*offset = strQuoteEnd + 1;
		} break;
		case JSONValue::Type::INT:
		{
			size_t intStart = quoteEnd + 2;
			size_t nextNonAlphaNumeric = NextNonAlphaNumeric(fileContents, intStart + 1);
			size_t intCharCount = nextNonAlphaNumeric - intStart;
			std::string intStr = fileContents.substr(intStart, intCharCount);
			i32 intValue = 0;
			if (!intStr.empty())
			{
				intValue = stoi(intStr);
			}
			field.value = JSONValue(intValue);

			*offset = nextNonAlphaNumeric;
		} break;
		case JSONValue::Type::FLOAT:
		{
			size_t floatStart = quoteEnd + 2;
			size_t decimalIndex = fileContents.find('.', floatStart);
			size_t floatEnd = NextNonAlphaNumeric(fileContents, decimalIndex + 1);
			size_t floatCharCount = floatEnd - floatStart;
			std::string floatStr = fileContents.substr(floatStart, floatCharCount);
			real floatValue = 0.0f;
			if (!floatStr.empty())
			{
				floatValue = stof(floatStr);
			}
			field.value = JSONValue(floatValue);

			*offset = floatEnd;
		} break;
		case JSONValue::Type::BOOL:
		{
			bool boolValue = valueFirstChar == 't';
			field.value = JSONValue(boolValue);

			*offset = NextNonAlphaNumeric(fileContents, quoteEnd + 3);
		} break;
		case JSONValue::Type::OBJECT:
		{
			*offset = quoteEnd + 2;

			JSONObject object;
			ParseObject(filePath, fileContents, offset, object);

			field.value = JSONValue(object);
		} break;
		case JSONValue::Type::OBJECT_ARRAY:
		{
			std::vector<JSONObject> objects;

			*offset = quoteEnd + 2;

			i32 arrayClosingBracket = MatchingBracket(filePath, '[', fileContents, *offset);
			if (arrayClosingBracket == -1)
			{
				PrintError("Couldn't find matching bracket %s (for '[') in %s\n", field.label.c_str(), filePath.c_str());
				return false;
			}

			*offset += 1;

			while (*offset < arrayClosingBracket)
			{
				JSONObject object;
				ParseObject(filePath, fileContents, offset, object);

				objects.push_back(object);
			}

			*offset = arrayClosingBracket + 1;

			field.value = JSONValue(objects);
		} break;
		case JSONValue::Type::FIELD_ARRAY:
		{
			std::vector<JSONField> fields;
			// TODO:
			field.value = JSONValue(fields);
		} break;
		case JSONValue::Type::UNINITIALIZED:
		default:
		{
			size_t nextNonAlphaNumeric = NextNonAlphaNumeric(fileContents, *offset);
			PrintError("Unhandled JSON value type: %s in %s\n", fileContents.substr(quoteEnd + 2, nextNonAlphaNumeric - (quoteEnd + 2)).c_str(), filePath.c_str());
			*offset = -1;
			return false;
		} break;
		}

		return true;
	}

	i32 JSONParser::MatchingBracket(const std::string& filePath, char openingBracket, const std::string& fileContents, i32 offset)
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
			PrintError("Unhandled opening bracket type: %c in %s\n", openingBracket, filePath.c_str());
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
