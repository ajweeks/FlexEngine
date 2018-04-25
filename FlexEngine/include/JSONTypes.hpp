#pragma once

#include <string>
#include <vector>

#include "Types.hpp"

namespace flex
{
	struct JSONValue;		// Holds some data
	struct JSONField;		// Holds a label and a value
	struct JSONObject;		// Holds fields
	struct ParsedJSONFile;	// Holds a root object

	struct JSONObject
	{
		std::vector<JSONField> fields;

		bool HasField(const std::string& label) const;
		std::string GetString(const std::string& label) const;
		i32 GetInt(const std::string& label) const;
		real GetFloat(const std::string& label) const;
		bool GetBool(const std::string& label) const;
		const std::vector<JSONField>& GetFieldArray(const std::string& label) const;
		const std::vector<JSONObject>& GetObjectArray(const std::string& label) const;
		const JSONObject& GetObject(const std::string& label) const;
		//JSONField& operator[](const std::string& label);

		std::string Print(i32 tabCount);
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

		explicit JSONValue();
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
		JSONField();
		JSONField(const std::string& label, const JSONValue& value);

		std::string label;
		JSONValue value;

		std::string Print(i32 tabCount);
	};
} // namespace flex
