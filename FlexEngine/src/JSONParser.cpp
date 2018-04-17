
#include "stdafx.hpp"

#include "JSONParser.hpp"
#include "Logger.hpp"

#include <fstream>
#include <cwctype>

namespace flex
{
	JSONParser::JSONValue::Type JSONParser::JSONValue::TypeFromChar(char c, const std::string& stringAfter)
	{
		switch (c)
		{
		case '{':
			return Type::OBJECT;
		case '[':
			if (stringAfter[0] == '{')
			{
				return Type::OBJECT_ARRAY;
			}
			else
			{
				// Arrays of strings are not supported
				return Type::FIELD_ARRAY;
			}
		case '\"':
			return Type::STRING;
		case 't':
		case 'f':
			return Type::BOOL;
		default:
		{
			// Check if number
			bool isDigit = isdigit(c) != 0;
			bool isNegation = c == '-';

			if (isDigit || isNegation)
			{
				i32 nextNonAlphaNumeric = NextNonAlphaNumeric(stringAfter, 0);
				if (nextNonAlphaNumeric == '.')
				{
					return Type::FLOAT;
				}
				else
				{
					return Type::INT;
				}
			}
		}

		return Type::UNINITIALIZED;
		}
	}

	JSONParser::JSONValue::JSONValue() :
		type(Type::UNINITIALIZED)
	{
	}

	JSONParser::JSONValue::JSONValue(const std::string& strValue) :
		strValue(strValue),
		type(Type::STRING)
	{
	}

	JSONParser::JSONValue::JSONValue(i32 intValue) :
		intValue(intValue),
		type(Type::INT)
	{
	}

	JSONParser::JSONValue::JSONValue(real floatValue) :
		floatValue(floatValue),
		type(Type::FLOAT)
	{
	}

	JSONParser::JSONValue::JSONValue(bool boolValue) :
		boolValue(boolValue),
		type(Type::BOOL)
	{
	}

	JSONParser::JSONValue::JSONValue(const JSONObject& objectValue) :
		objectValue(objectValue),
		type(Type::OBJECT)
	{
	}

	JSONParser::JSONValue::JSONValue(const std::vector<JSONObject>& objectArrayValue) :
		objectArrayValue(objectArrayValue),
		type(Type::OBJECT_ARRAY)
	{
	}

	JSONParser::JSONValue::JSONValue(const std::vector<JSONField>& fieldArrayValue) :
		fieldArrayValue(fieldArrayValue),
		type(Type::FIELD_ARRAY)
	{
	}

	std::string JSONParser::Print(const JSONField& field, i32 tabCount)
	{
		const std::string tabs(tabCount, '\t');
		std::string result(tabs + field.label + " : ");

		switch (field.value.type)
		{
		case JSONValue::Type::STRING:
			result += '\"' + field.value.strValue + "\"\n";
			break;
		case JSONValue::Type::INT:
			result += std::to_string(field.value.intValue) + '\n';
			break;
		case JSONValue::Type::FLOAT:
			result += std::to_string(field.value.floatValue) + '\n';
			break;
		case JSONValue::Type::BOOL:
			result += (field.value.boolValue ? "true\n" : "false\n");
			break;
		case JSONValue::Type::OBJECT:
			result += '\n' + tabs + "{\n";
			for (i32 i = 0; i < field.value.objectValue.fields.size(); ++i)
			{
				result += Print(field.value.objectValue.fields[i], tabCount + 1);
			}
			result += tabs + "}\n";
			break;
		case JSONValue::Type::OBJECT_ARRAY:
			result += '\n' + tabs + "[\n";
			for (i32 i = 0; i < field.value.objectArrayValue.size(); ++i)
			{
				result += Print(field.value.objectArrayValue[i], tabCount + 1);
			}
			result += tabs + "]\n";
			break;
		case JSONValue::Type::FIELD_ARRAY:
			result += '\n' + tabs + "[\n";
			for (i32 i = 0; i < field.value.fieldArrayValue.size(); ++i)
			{
				result += Print(field.value.fieldArrayValue[i], tabCount + 1);
			}
			result += tabs + "]\n";
			break;
		case JSONValue::Type::UNINITIALIZED:
			result += "UNINITIALIZED TYPE\n";
			break;
		default:
			result += "UNHANDLED TYPE\n";
			break;
		}

		return result;
	}

	std::string JSONParser::Print(const JSONObject& object, i32 tabCount)
	{
		const std::string tabs(tabCount, '\t');
		std::string result(tabs + "{\n");

		for (i32 i = 0; i < object.fields.size(); ++i)
		{
			result += Print(object.fields[i], tabCount + 1);
		}

		result += tabs + "}\n";

		return result;
	}

	void JSONParser::Parse(const std::string& filePath, ParsedFile& parsedFile)
	{
		std::ifstream ifStream(filePath, std::fstream::ate);
		if (!ifStream)
		{
			Logger::LogError("Couldn't find JSON file: " + filePath);
			return;
		}

		size_t fileSize = (size_t)ifStream.tellg();
		ifStream.seekg(0, ifStream.beg);

		if (fileSize == 0)
		{
			Logger::LogError("Attempted to parse empty JSON file: " + filePath);
			return;
		}

		std::string fileContents;
		fileContents.reserve(fileSize);
		fileContents.assign(
			std::istreambuf_iterator<char>(ifStream),
			std::istreambuf_iterator<char>());

		size_t firstBracket = fileContents.find("{");
		size_t lastBracket = fileContents.rfind("}");

		if (firstBracket == std::string::npos ||
			lastBracket == std::string::npos || 
			firstBracket > lastBracket)
		{
			Logger::LogError("Failed to parse JSON file. No valid bracket pairs found!");
		}

		bool inQuote = false;
		i32 i = 0;
		while (i < (i32)fileContents.size())
		{
			char currentChar = fileContents[i];
			if (currentChar == '\"')
			{
				inQuote = !inQuote;
				++i;
				continue;
			}
			else
			{
				if (!inQuote)
				{
					// Remove all single line comments
					if (currentChar == '/' && fileContents[i + 1] == '/')
					{
						size_t endLine = fileContents.find('\n', i + 2);
						if (endLine == std::string::npos)
						{
							endLine = fileContents.size();
						}

						fileContents.erase(i, endLine - i);
						continue;
					}
					// Remove all whitespace characters
					else if (isspace(currentChar))
					{
						fileContents.erase(i, 1);
						continue;
					}
				}
			}

			++i;
		}

		//Logger::LogInfo("Cleaned JSON file:");
		//Logger::LogInfo(fileContents);

		parsedFile.rootObject = {};

		i32 fileContentOffset = 0;
		bool parsing = true;
		while (parsing)
		{
			JSONField field;
			parsing = ParseField(fileContents, &fileContentOffset, field);
			parsedFile.rootObject.fields.push_back(field);

			if (fileContentOffset == fileContents.size() - 1)
			{
				// We've reached the end of the file!
				parsing = false;
			}
		}
	}

	bool JSONParser::ParseField(const std::string& fileContents, i32* offset, JSONField& field)
	{
		size_t quoteStart = fileContents.find('/"', *offset);

		if (quoteStart == std::string::npos)
		{
			Logger::LogError("Couldn't find quote after offset " + std::to_string(*offset));
			return false;
		}

		size_t quoteEnd = fileContents.find('/"', quoteStart + 1);
		if (quoteEnd == std::string::npos)
		{
			Logger::LogError("Couldn't find end quote after offset " + std::to_string(*offset));
			return false;
		}

		field.label = fileContents.substr(quoteStart + 1, quoteEnd - (quoteStart + 1));
		*offset = quoteEnd + 1;

		if (fileContents[quoteEnd + 1] != ':')
		{
			Logger::LogError("Invalidly formatted JSON file (':' must occur after a field label)");
			return false;
		}

		char valueFirstChar = fileContents[quoteEnd + 2];
		JSONValue::Type fieldType = JSONValue::TypeFromChar(valueFirstChar, fileContents.substr(quoteEnd + 3));

		switch (fieldType)
		{
		case JSONValue::Type::STRING:
		{
			size_t quoteStart = fileContents.find('/"', *offset);

			if (quoteStart == std::string::npos)
			{
				Logger::LogError("Couldn't find quote after offset " + std::to_string(*offset));
				return false;
			}

			size_t quoteEnd = fileContents.find('/"', quoteStart + 1);
			if (quoteEnd == std::string::npos)
			{
				Logger::LogError("Couldn't find end quote after offset " + std::to_string(*offset));
				return false;
			}

			std::string stringValue = fileContents.substr(quoteStart + 1, quoteEnd - (quoteStart + 1));
			field.value = JSONValue(stringValue);

			*offset = quoteEnd + 1;
		} break;
		case JSONValue::Type::INT:
		{
			size_t nextNonAlphaNumeric = NextNonAlphaNumeric(fileContents, quoteEnd + 2);
			size_t intCharCount = nextNonAlphaNumeric - (quoteEnd + 2);
			std::string intStr = fileContents.substr(quoteEnd + 2, intCharCount);
			int intValue = stoi(intStr);
			field.value = JSONValue(intValue);

			*offset = nextNonAlphaNumeric;
		} break;
		case JSONValue::Type::FLOAT:
		{
			size_t nextNonAlphaNumeric = NextNonAlphaNumeric(fileContents, *offset);
			size_t floatEnd = nextNonAlphaNumeric - (quoteEnd + 3);
			std::string floatStr = fileContents.substr(quoteEnd + 3, floatEnd);
			real floatValue = stof(floatStr);
			field.value = JSONValue(floatValue);

			*offset = floatEnd + 1;
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
			ParseObject(fileContents, offset, object);

			field.value = JSONValue(object);
		} break;
		case JSONValue::Type::OBJECT_ARRAY:
		{
			std::vector<JSONObject> objects;

			*offset = quoteEnd + 2;

			i32 arrayClosingBracket = MatchingBracket('[', fileContents, *offset);
			if (arrayClosingBracket == -1)
			{
				Logger::LogError("Couldn't find matching bracket " + field.label + " (for '[' )");
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
			// TODO:
			field.value = JSONValue(fields);
		} break;
		case JSONValue::Type::UNINITIALIZED:
		default:
		{
			size_t nextNonAlphaNumeric = NextNonAlphaNumeric(fileContents, *offset);
			Logger::LogError("Unhandled JSON value type: " + fileContents.substr(quoteEnd + 2), nextNonAlphaNumeric - (quoteEnd + 2));
			*offset = -1;
			return false;
		} break;
		}

		return true;
	}

	i32 JSONParser::NextNonAlphaNumeric(const std::string& fileContents, i32 offset)
	{
		while (offset < fileContents.size())
		{
			if (!isdigit(fileContents[offset]) && !isalpha(fileContents[offset]))
			{
				return offset;
			}
			++offset;
		}

		return -1;
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
			Logger::LogError("Unhandled opening bracket type: " + openingBracket);
			return -1;
		}

		i32 openSquareBracketCount = 0; // [
		i32 openCurlyBracketCount = 0;  // {
		i32 openParenthesesCount = 0;   // (

		bool inQuotes = false;
		while (offset < fileContents.size())
		{
			char currentChar = fileContents[offset];

			if (currentChar == '\"')
			{
				inQuotes = !inQuotes;
			}
			
			if (!inQuotes)
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

	bool JSONParser::ParseObject(const std::string& fileContents, i32* offset, JSONObject& outObject)
	{
		i32 objectClosingBracket = MatchingBracket('{', fileContents, *offset);
		if (objectClosingBracket == -1)
		{
			Logger::LogError("Couldn't find matching bracket for '{'");
			return false;
		}

		bool parsing = true;
		while (parsing && *offset < objectClosingBracket)
		{
			JSONField field;
			parsing = ParseField(fileContents, offset, field);
			outObject.fields.push_back(field);
		}

		*offset = objectClosingBracket + 1;

		if (fileContents[*offset] == ',')
		{
			*offset += 1;
		}

		return true;
	}

} // namespace flex
