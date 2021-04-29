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

		//
		// TODO: Rename SetXChecked functions to TryGetX
		//

		bool HasField(const std::string& label) const;

		std::string GetString(const std::string& label) const;
		bool SetStringChecked(const std::string& label, std::string& value) const;
		bool SetVec2Checked(const std::string& label, glm::vec2& value) const;
		bool SetVec3Checked(const std::string& label, glm::vec3& value) const;
		bool SetVec4Checked(const std::string& label, glm::vec4& value) const;

		glm::vec2 GetVec2(const std::string& label) const;
		glm::vec3 GetVec3(const std::string& label) const;
		glm::vec4 GetVec4(const std::string& label) const;

		i32 GetInt(const std::string& label) const;
		bool SetIntChecked(const std::string& label, i32& value) const;

		u32 GetUInt(const std::string& label) const;
		bool SetUIntChecked(const std::string& label, u32& value) const;

		real GetFloat(const std::string& label) const;
		bool SetFloatChecked(const std::string& label, real& value) const;

		bool GetBool(const std::string& label) const;
		bool SetBoolChecked(const std::string& label, bool& value) const;

		GUID GetGUID(const std::string& label) const;
		bool SetGUIDChecked(const std::string& label, GUID& value) const;

		GameObjectID GetGameObjectID(const std::string& label) const;
		bool SetGameObjectIDChecked(const std::string& label, GameObjectID& value) const;

		const std::vector<JSONField>& GetFieldArray(const std::string& label) const;
		bool SetFieldArrayChecked(const std::string& label, std::vector<JSONField>& value) const;

		const std::vector<JSONObject>& GetObjectArray(const std::string& label) const;
		bool SetObjectArrayChecked(const std::string& label, std::vector<JSONObject>& value) const;

		const JSONObject& GetObject(const std::string& label) const;
		bool SetObjectChecked(const std::string& label, JSONObject& value) const;

		std::string ToString(i32 tabCount = 0) const;

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
			UINT,
			FLOAT,
			BOOL,
			OBJECT,
			OBJECT_ARRAY,
			FIELD_ARRAY,
			FIELD_ENTRY,
			UNINITIALIZED
		};

		static const u32 DEFAULT_FLOAT_PRECISION = 6;

		static Type TypeFromChar(char c, const std::string& stringAfter);

		explicit JSONValue();
		explicit JSONValue(const std::string& inStrValue);
		explicit JSONValue(const char* inStrValue);
		explicit JSONValue(i32 inIntValue);
		explicit JSONValue(u32 inUIntValue);
		explicit JSONValue(real inFloatValue);
		explicit JSONValue(real inFloatValue, u32 precision);
		explicit JSONValue(bool inBoolValue);
		explicit JSONValue(const JSONObject& inObjectValue);
		explicit JSONValue(const std::vector<JSONObject>& inObjectArrayValue);
		explicit JSONValue(const std::vector<JSONField>& inFieldArrayValue);
		explicit JSONValue(const GUID& inGUIDValue);

		Type type = Type::UNINITIALIZED;
		union
		{
			i32 intValue = 0;
			u32 uintValue;
			real floatValue;
			bool boolValue;
		};
		JSONObject objectValue;
		std::string strValue;
		u32 floatPrecision;
		std::vector<JSONField> fieldArrayValue;
		std::vector<JSONObject> objectArrayValue;
	};

	struct JSONField
	{
		JSONField();
		JSONField(const std::string& label, const JSONValue& value);

		std::string label;
		JSONValue value;

		std::string ToString(i32 tabCount) const;
	};
} // namespace flex
