#pragma once

#include <vector>

#include "GUID.hpp"
#include "Types.hpp"

namespace flex
{
	struct JSONValue;		// Holds some data
	struct JSONField;		// Holds a label and a value
	struct JSONObject;		// Holds fields
	struct ParsedJSONFile;	// Holds a root object

	enum class ValueType
	{
		STRING,
		INT,
		UINT,
		LONG,
		ULONG,
		FLOAT,
		BOOL,
		VEC2,
		VEC3,
		VEC4,
		QUAT,
		MAT4,
		GUID,
		OBJECT,
		OBJECT_ARRAY,
		FIELD_ARRAY,
		FIELD_ENTRY,
		UNINITIALIZED
	};

	struct JSONObject
	{
		bool HasField(const std::string& label) const;

		std::string GetString(const std::string& label) const;
		bool TryGetString(const std::string& label, std::string& value) const;
		StringID GetStringID(const std::string& label) const;
		bool TryGetStringID(const std::string& label, StringID& value) const;

		glm::vec2 GetVec2(const std::string& label) const;
		bool TryGetVec2(const std::string& label, glm::vec2& value) const;

		glm::vec3 GetVec3(const std::string& label) const;
		bool TryGetVec3(const std::string& label, glm::vec3& value) const;

		glm::vec4 GetVec4(const std::string& label) const;
		bool TryGetVec4(const std::string& label, glm::vec4& value) const;

		glm::quat GetQuat(const std::string& label) const;
		bool TryGetQuat(const std::string& label, glm::quat& value) const;

		glm::mat4 GetMat4(const std::string& label) const;
		bool TryGetMat4(const std::string& label, glm::mat4& value) const;

		i32 GetInt(const std::string& label) const;
		bool TryGetInt(const std::string& label, i32& value) const;

		u32 GetUInt(const std::string& label) const;
		bool TryGetUInt(const std::string& label, u32& value) const;

		i64 GetLong(const std::string& label) const;
		bool TryGetLong(const std::string& label, i64& value) const;

		u64 GetULong(const std::string& label) const;
		bool TryGetULong(const std::string& label, u64& value) const;

		real GetFloat(const std::string& label) const;
		bool TryGetFloat(const std::string& label, real& value) const;

		bool GetBool(const std::string& label) const;
		bool TryGetBool(const std::string& label, bool& value) const;

		GUID GetGUID(const std::string& label) const;
		bool TryGetGUID(const std::string& label, GUID& value) const;

		GameObjectID GetGameObjectID(const std::string& label) const;
		bool TryGetGameObjectID(const std::string& label, GameObjectID& value) const;

		PrefabID GetPrefabID(const std::string& label) const;
		bool TryGetPrefabID(const std::string& label, PrefabID& value) const;

		const std::vector<JSONField>& GetFieldArray(const std::string& label) const;
		bool TryGetFieldArray(const std::string& label, std::vector<JSONField>& value) const;

		const std::vector<JSONObject>& GetObjectArray(const std::string& label) const;
		bool TryGetObjectArray(const std::string& label, std::vector<JSONObject>& value) const;

		const JSONObject& GetObject(const std::string& label) const;
		bool TryGetObject(const std::string& label, JSONObject& value) const;

		bool TryGetValueOfType(const char* label, void* valuePtr, ValueType type) const;

		std::string ToString(i32 tabCount = 0) const;

		static JSONObject s_EmptyObject;
		static std::vector<JSONObject> s_EmptyObjectArray;
		static std::vector<JSONField> s_EmptyFieldArray;

		std::vector<JSONField> fields;
	};

	struct JSONValue
	{
		static const u32 DEFAULT_FLOAT_PRECISION = 6;

		static ValueType TypeFromChar(char c, const std::string& stringAfter);

		explicit JSONValue();
		explicit JSONValue(const std::string& inStrValue);
		explicit JSONValue(const char* inStrValue);
		explicit JSONValue(i32 inIntValue);
		explicit JSONValue(u32 inUIntValue);
		explicit JSONValue(i64 inLongValue);
		explicit JSONValue(u64 inULongValue);
		explicit JSONValue(real inFloatValue);
		explicit JSONValue(real inFloatValue, u32 precision);
		explicit JSONValue(bool inBoolValue);
		explicit JSONValue(const glm::vec2& inVec2Value, u32 inFloatPrecision = DEFAULT_FLOAT_PRECISION);
		explicit JSONValue(const glm::vec3& inVec3Value, u32 inFloatPrecision = DEFAULT_FLOAT_PRECISION);
		explicit JSONValue(const glm::vec4& inVec4Value, u32 inFloatPrecision = DEFAULT_FLOAT_PRECISION);
		explicit JSONValue(const glm::quat& inQuatValue, u32 inFloatPrecision = DEFAULT_FLOAT_PRECISION);
		explicit JSONValue(const glm::mat4& inMatValue, u32 inFloatPrecision = DEFAULT_FLOAT_PRECISION);
		explicit JSONValue(const GUID& inGUIDValue);
		explicit JSONValue(const JSONObject& inObjectValue);
		explicit JSONValue(const std::vector<JSONObject>& inObjectArrayValue);
		explicit JSONValue(const std::vector<JSONField>& inFieldArrayValue);

		static JSONValue FromRawPtr(void* valuePtr, ValueType type, u32 precision = DEFAULT_FLOAT_PRECISION);

		i32 AsInt() const;
		u32 AsUInt() const;
		i64 AsLong() const;
		u64 AsULong() const;
		real AsFloat() const;
		bool AsBool() const;
		std::string AsString() const;
		GUID AsGUID() const;

		ValueType type = ValueType::UNINITIALIZED;

		union
		{
			i32 intValue = 0;
			u32 uintValue;
			i64 longValue;
			u64 ulongValue;
			real floatValue;
			bool boolValue;
			glm::vec4 vecValue;
			glm::mat4 matValue;
		};
		GUID guidValue;
		JSONObject objectValue;
		std::string strValue;
		std::vector<JSONField> fieldArrayValue;
		std::vector<JSONObject> objectArrayValue;

		u32 floatPrecision;
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
