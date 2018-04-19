
#include "stdafx.hpp"

#include "JSONParser.hpp"
#include "Logger.hpp"
#include "Helpers.hpp"

#include <fstream>
#include <cwctype>

namespace flex
{
	bool JSONParser::Parse(const std::string& filePath, ParsedJSONFile& parsedFile)
	{
		std::ifstream ifStream(filePath, std::fstream::ate);
		if (!ifStream)
		{
			Logger::LogError("Couldn't find JSON file: " + filePath);
			return false;
		}

		size_t fileSize = (size_t)ifStream.tellg();
		ifStream.seekg(0, ifStream.beg);

		if (fileSize == 0)
		{
			Logger::LogError("Attempted to parse empty JSON file: " + filePath);
			return false;
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
		bool parseSucceeded = true;
		bool parsing = true;
		while (parsing)
		{
			JSONField field;
			parsing = ParseField(fileContents, &fileContentOffset, field);
			parsedFile.rootObject.fields.push_back(field);

			parseSucceeded |= parsing;

			if (fileContentOffset == (i32)(fileContents.size() - 1))
			{
				// We've reached the end of the file!
				parsing = false;
			}
		}

		return parseSucceeded;
	}

	Transform JSONParser::ParseTransform(const JSONObject& transformObject)
	{
		std::string posStr = transformObject.GetString("position");
		std::string rotStr = transformObject.GetString("rotation");
		std::string scaleStr = transformObject.GetString("scale");

		glm::vec3 pos(0.0f);
		if (!posStr.empty())
		{
			std::vector<std::string> posParts = Split(posStr, ',');
			
			if (posParts.size() != 3)
			{
				Logger::LogError("Invalid position specified in scene file: " + posStr);
			}
			else
			{
				pos = glm::vec3(atof(posParts[0].c_str()), atof(posParts[1].c_str()), atof(posParts[2].c_str()));
			}
		}

		glm::quat rot = glm::quat();
		if (!rotStr.empty())
		{
			std::vector<std::string> rotParts = Split(rotStr, ',');

			if (rotParts.size() != 3)
			{
				Logger::LogError("Invalid rotation specified in scene file: " + rotStr);
			}
			else
			{
				rot = glm::quat(glm::vec3(atof(rotParts[0].c_str()), atof(rotParts[1].c_str()), atof(rotParts[2].c_str())));
			}
		}

		glm::vec3 scale(1.0f);
		if (!scaleStr.empty())
		{
			std::vector<std::string> scaleParts = Split(scaleStr, ',');

			if (scaleParts.size() != 3)
			{
				Logger::LogError("Invalid scale specified in scene file: " + posStr);
			}
			else
			{
				scale = glm::vec3(atof(scaleParts[0].c_str()), atof(scaleParts[1].c_str()), atof(scaleParts[2].c_str()));
			}
		}

		return Transform(pos, rot, scale);
	}

	glm::vec2 JSONParser::ParseVec2(const std::string& vecStr)
	{
		std::vector<std::string> parts = Split(vecStr, ',');

		if (parts.size() != 2)
		{
			Logger::LogError("Invalid vec2 field: " + vecStr);
			return glm::vec2(-1);
		}
		else
		{
			glm::vec2 result(
				std::stof(parts[0].c_str()),
				std::stof(parts[1].c_str()));

			return result;
		}
	}

	glm::vec3 JSONParser::ParseVec3(const std::string& vecStr)
	{
		std::vector<std::string> parts = Split(vecStr, ',');

		if (parts.size() != 3 && parts.size() != 4)
		{
			Logger::LogError("Invalid vec3 field: " + vecStr);
			return glm::vec3(-1);
		}
		else
		{
			glm::vec3 result(
				std::stof(parts[0].c_str()),
				std::stof(parts[1].c_str()),
				std::stof(parts[2].c_str()));

			return result;
		}
	}

	glm::vec4 JSONParser::ParseVec4(const std::string& vecStr, bool requireW)
	{
		std::vector<std::string> parts = Split(vecStr, ',');

		if ((parts.size() != 4 && parts.size() != 3) || (requireW && parts.size() != 4))
		{
			Logger::LogError("Invalid vec4 field: " + vecStr);
			return glm::vec4(-1);
		}
		else
		{
			glm::vec4 result;

			if (parts.size() == 4)
			{
				result = glm::vec4(
					std::stof(parts[0].c_str()),
					std::stof(parts[1].c_str()),
					std::stof(parts[2].c_str()),
					std::stof(parts[3].c_str()));
			}
			else
			{
				result = glm::vec4(
					std::stof(parts[0].c_str()),
					std::stof(parts[1].c_str()),
					std::stof(parts[2].c_str()),
					1.0f);
			}

			return result;
		}
	}

	glm::vec4 JSONParser::ParseColor4(const std::string& colorStr)
	{
		return ParseVec4(colorStr, false);
	}

	glm::vec3 JSONParser::ParseColor3(const std::string& colorStr)
	{
		return ParseVec3(colorStr);
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
	bool JSONParser::ParseField(const std::string& fileContents, i32* offset, JSONField& field)
	{
		size_t quoteStart = fileContents.find('\"', *offset);

		if (quoteStart == std::string::npos)
		{
			Logger::LogError("Couldn't find quote after offset " + std::to_string(*offset));
			return false;
		}

		size_t quoteEnd = fileContents.find('\"', quoteStart + 1);
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
			size_t strQuoteStart = fileContents.find('\"', *offset);

			if (strQuoteStart == std::string::npos)
			{
				Logger::LogError("Couldn't find quote after offset " + std::to_string(*offset));
				return false;
			}

			size_t strQuoteEnd = fileContents.find('\"', strQuoteStart + 1);
			if (strQuoteEnd == std::string::npos)
			{
				Logger::LogError("Couldn't find end quote after offset " + std::to_string(*offset));
				return false;
			}

			std::string stringValue = fileContents.substr(strQuoteStart + 1, strQuoteEnd - (strQuoteStart + 1));
			field.value = JSONValue(stringValue);

			*offset = strQuoteEnd + 1;
		} break;
		case JSONValue::Type::INT:
		{
			size_t intStart = quoteEnd + 2;
			size_t nextNonAlphaNumeric = NextNonAlphaNumeric(fileContents, intStart);
			size_t intCharCount = nextNonAlphaNumeric - intStart;
			std::string intStr = fileContents.substr(intStart, intCharCount);
			int intValue = stoi(intStr);
			field.value = JSONValue(intValue);

			*offset = nextNonAlphaNumeric;
		} break;
		case JSONValue::Type::FLOAT:
		{
			size_t floatStart = quoteEnd + 2;
			size_t decminalIndex = NextNonAlphaNumeric(fileContents, floatStart);
			size_t floatEnd = NextNonAlphaNumeric(fileContents, decminalIndex + 1);
			size_t floatCharCount = floatEnd - floatStart;
			std::string floatStr = fileContents.substr(floatStart, floatCharCount);
			real floatValue = stof(floatStr);
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
			Logger::LogError("Unhandled JSON value type: " + fileContents.substr(quoteEnd + 2, nextNonAlphaNumeric - (quoteEnd + 2)));
			*offset = -1;
			return false;
		} break;
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
			Logger::LogError("Unhandled opening bracket type: " + openingBracket);
			return -1;
		}

		i32 openSquareBracketCount = 0; // [
		i32 openCurlyBracketCount = 0;  // {
		i32 openParenthesesCount = 0;   // (

		bool inQuotes = false;
		while (offset < (i32)fileContents.size())
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
} // namespace flex
