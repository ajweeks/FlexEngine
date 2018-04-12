
#include "stdafx.hpp"

#include "JSONParser.hpp"
#include "Logger.hpp"

#include <fstream>

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

		size_t fileSize = ifStream.tellg();
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

		// Trim start/end of file
		fileContents = fileContents.substr(firstBracket + 1, lastBracket - firstBracket);


	}

} // namespace flex
