#pragma once

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
		bool HasField(const std::string& label) const;

		std::string GetString(const std::string& label) const;
		/* Sets value to the result of GetString(label) if that field is present */
		bool SetStringChecked(const std::string& label, std::string& value) const;
		/* Sets value to the result of ParseVec2(GetString(label)) if that field is present */
		bool SetVec2Checked(const std::string& label, glm::vec2& value) const;
		/* Sets value to the result of ParseVec3(GetString(label)) if that field is present */
		bool SetVec3Checked(const std::string& label, glm::vec3& value) const;
		/* Sets value to the result of ParseVec4(GetString(label)) if that field is present */
		bool SetVec4Checked(const std::string& label, glm::vec4& value) const;

		glm::vec2 GetVec2(const std::string& label) const;
		glm::vec3 GetVec3(const std::string& label) const;
		glm::vec4 GetVec4(const std::string& label) const;

		i32 GetInt(const std::string& label) const;
		/* Sets value to the result of GetString(label) if that field is present */
		bool SetIntChecked(const std::string& label, int& value) const;

		real GetFloat(const std::string& label) const;
		/* Sets value to the result of GetString(label) if that field is present */
		bool SetFloatChecked(const std::string& label, real& value) const;

		bool GetBool(const std::string& label) const;
		/* Sets value to the result of GetString(label) if that field is present */
		bool SetBoolChecked(const std::string& label, bool& value) const;

		const std::vector<JSONField>& GetFieldArray(const std::string& label) const;
		/* Sets value to the result of GetString(label) if that field is present */
		bool SetFieldArrayChecked(const std::string& label, std::vector<JSONField>& value) const;

		const std::vector<JSONObject>& GetObjectArray(const std::string& label) const;
		/* Sets value to the result of GetString(label) if that field is present */
		bool SetObjectArrayChecked(const std::string& label, std::vector<JSONObject>& value) const;

		const JSONObject& GetObject(const std::string& label) const;
		/* Sets value to the result of GetString(label) if that field is present */
		bool SetObjectChecked(const std::string& label, JSONObject& value) const;

		std::string Print(i32 tabCount);

		static JSONObject s_EmptyObject;
		static std::vector<JSONObject> s_EmptyObjectArray;
		static std::vector<JSONField> s_EmptyFieldArray;

		std::vector<JSONField> fields;
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
		explicit JSONValue(const char* strValue);
		explicit JSONValue(i32 intValue);
		explicit JSONValue(real floatValue);
		explicit JSONValue(bool boolValue);
		explicit JSONValue(const JSONObject& objectValue);
		explicit JSONValue(const std::vector<JSONObject>& objectArrayValue);
		explicit JSONValue(const std::vector<JSONField>& fieldArrayValue);

		Type type = Type::UNINITIALIZED;
		union
		{
			i32 intValue = 0;
			real floatValue;
			bool boolValue;
		};
		JSONObject objectValue;
		std::string strValue;
		std::vector<JSONField> fieldArrayValue;
		std::vector<JSONObject> objectArrayValue;
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
