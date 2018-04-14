
#include "stdafx.hpp"

#include "JSONParser.hpp"
#include "Logger.hpp"

#include <fstream>
#include <cwctype>

namespace flex
{
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

	JSONParser::JSONValue::JSONValue(JSONObject objectValue) :
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

	void* JSONParser::JSONValue::GetData()
	{
		switch (type)
		{
		case JSONValue::Type::STRING:		return (void*)&strValue;
		case JSONValue::Type::INT:			return (void*)&intValue;
		case JSONValue::Type::FLOAT:		return (void*)&floatValue;
		case JSONValue::Type::OBJECT:		return (void*)&objectValue;
		case JSONValue::Type::OBJECT_ARRAY:	return (void*)&objectArrayValue;
		case JSONValue::Type::FIELD_ARRAY:	return (void*)&fieldArrayValue;
		default:							return nullptr;
		}
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
							Logger::LogError("Invalidly formatted json file (contains '/' at end of line): " + filePath);
							return;
						}

						fileContents.erase(i, endLine - i);
						continue;
					}
					// Remove all whitespace
					else if (isspace(currentChar))
					{
						fileContents.erase(i, 1);
						continue;
					}
				}
			}

			++i;
		}

		Logger::LogInfo("Cleaned json file:");
		Logger::LogInfo(fileContents);


		++parsedFile.numFields;
		parsedFile.rootObject = {};
	}

} // namespace flex
