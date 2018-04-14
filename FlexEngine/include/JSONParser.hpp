#pragma once

#include <string>
#include <vector>

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
				OBJECT,
				OBJECT_ARRAY,
				FIELD_ARRAY
			};

			explicit JSONValue(const std::string& strValue);
			explicit JSONValue(i32 intValue);
			explicit JSONValue(real floatValue);
			explicit JSONValue(JSONObject objectValue);
			explicit JSONValue(const std::vector<JSONObject>& objectArrayValue);
			explicit JSONValue(const std::vector<JSONField>& fieldArrayValue);

			void* GetData();

			Type type;
			std::string strValue;
			i32 intValue = 0;
			real floatValue = 0.0f;
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
			int numFields = 0;
		};

		static void Parse(const std::string& filePath, ParsedFile& parsedFile);

	private:

	};
} // namespace flex
