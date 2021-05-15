
#include "stdafx.hpp"

#include "JSONTypes.hpp"
#include "Helpers.hpp"

namespace flex
{
	JSONObject JSONObject::s_EmptyObject;
	std::vector<JSONObject> JSONObject::s_EmptyObjectArray;
	std::vector<JSONField> JSONObject::s_EmptyFieldArray;

	JSONValue::Type JSONValue::TypeFromChar(char c, const std::string& stringAfter)
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
			bool isDigit = (isdigit(c) != 0);
			bool isDecimal = (c == '.');
			bool isNegation = (c == '-');

			if (isDigit || isDecimal || isNegation)
			{
				i32 nextNonAlphaNumeric = NextNonAlphaNumeric(stringAfter, 0);
				if (isDecimal || (nextNonAlphaNumeric != -1 && !stringAfter.empty() && stringAfter[nextNonAlphaNumeric] == '.'))
				{
					return Type::FLOAT;
				}
				else
				{
					std::string numberStr = std::string(1, c) + stringAfter.substr(0, nextNonAlphaNumeric);

					if (isNegation)
					{
						i64 result = strtoll(numberStr.c_str(), nullptr, 10);

						return (result < -INT_MAX || result > INT_MAX) ? Type::LONG : Type::INT;
					}
					else
					{
						u64 result = strtoull(numberStr.c_str(), nullptr, 10);

						return (result > INT_MAX) ? Type::ULONG : Type::UINT;
					}
				}
			}
		} return Type::UNINITIALIZED;
		}
	}

	JSONValue::JSONValue() :
		type(Type::UNINITIALIZED)
	{
	}

	JSONValue::JSONValue(const std::string& inStrValue) :
		type(Type::STRING),
		strValue(inStrValue)
	{
	}

	JSONValue::JSONValue(const char* inStrValue) :
		type(Type::STRING),
		strValue(inStrValue)
	{
	}

	JSONValue::JSONValue(i32 inIntValue) :
		type(Type::INT),
		intValue(inIntValue)
	{
		ENSURE(!IsNanOrInf((real)inIntValue));
	}

	JSONValue::JSONValue(u32 inUIntValue) :
		type(Type::UINT),
		uintValue(inUIntValue)
	{
	}

	JSONValue::JSONValue(i64 inLongValue) :
		type(Type::LONG),
		longValue(inLongValue)
	{
	}

	JSONValue::JSONValue(u64 inULongValue) :
		type(Type::ULONG),
		ulongValue(inULongValue)
	{
	}

	JSONValue::JSONValue(real inFloatValue) :
		JSONValue(inFloatValue, DEFAULT_FLOAT_PRECISION)
	{
	}

	JSONValue::JSONValue(real inFloatValue, u32 inFloatPrecision) :
		type(Type::FLOAT),
		floatValue(inFloatValue),
		floatPrecision(inFloatPrecision)
	{
		ENSURE(!IsNanOrInf(inFloatValue));
	}

	JSONValue::JSONValue(bool inBoolValue) :
		type(Type::BOOL),
		boolValue(inBoolValue)
	{
	}

	JSONValue::JSONValue(const JSONObject& inObjectValue) :
		type(Type::OBJECT),
		objectValue(inObjectValue)
	{
	}

	JSONValue::JSONValue(const std::vector<JSONObject>& inObjectArrayValue) :
		type(Type::OBJECT_ARRAY),
		objectArrayValue(inObjectArrayValue)
	{
	}

	JSONValue::JSONValue(const std::vector<JSONField>& inFieldArrayValue) :
		type(Type::FIELD_ARRAY),
		fieldArrayValue(inFieldArrayValue)
	{
	}

	JSONValue::JSONValue(const GUID& inGUIDValue) :
		type(Type::STRING),
		strValue(inGUIDValue.ToString())
	{
	}

	i32 JSONValue::AsInt() const
	{
		switch (type)
		{
		case Type::INT:
			return intValue;
		case Type::UINT:
			return (i32)uintValue;
		case Type::LONG:
			return (i32)longValue;
		case Type::ULONG:
			return (i32)ulongValue;
		case Type::FLOAT:
			return (i32)floatValue;
		case Type::BOOL:
			return boolValue ? 1 : 0;
		default:
			PrintError("AsInt was called on non-integer value\\n");
			return i32_max;
		}
	}

	u32 JSONValue::AsUInt() const
	{
		switch (type)
		{
		case Type::INT:
			return (u32)intValue;
		case Type::UINT:
			return uintValue;
		case Type::LONG:
			return (u32)longValue;
		case Type::ULONG:
			return (u32)ulongValue;
		case Type::FLOAT:
			return (u32)floatValue;
		case Type::BOOL:
			return boolValue;
		default:
			PrintError("AsUInt was called on non-integer value\\n");
			return u32_max;
		}
	}

	i64 JSONValue::AsLong() const
	{
		switch (type)
		{
		case Type::INT:
			return (i64)intValue;
		case Type::UINT:
			return (i64)uintValue;
		case Type::LONG:
			return longValue;
		case Type::ULONG:
			return (i64)ulongValue;
		case Type::FLOAT:
			return (i64)floatValue;
		case Type::BOOL:
			return boolValue;
		default:
			PrintError("AsLong was called on non-integer value\\n");
			return -1;
		}
	}

	u64 JSONValue::AsULong() const
	{
		switch (type)
		{
		case Type::INT:
			return (u64)intValue;
		case Type::UINT:
			return (u64)uintValue;
		case Type::LONG:
			return (u64)longValue;
		case Type::ULONG:
			return ulongValue;
		case Type::FLOAT:
			return (u64)floatValue;
		case Type::BOOL:
			return boolValue;
		default:
			PrintError("AsULong was called on non-integer value\\n");
			return u64_max;
		}
	}

	real JSONValue::AsFloat() const
	{
		switch (type)
		{
		case Type::INT:
			return (real)intValue;
		case Type::UINT:
			return (real)uintValue;
		case Type::LONG:
			return (real)longValue;
		case Type::ULONG:
			return (real)ulongValue;
		case Type::FLOAT:
			return floatValue;
		case Type::BOOL:
			return (boolValue != 0) ? 1.0f : 0.0f;
		default:
			PrintError("AsFloat was called on non-floating point value\n");
			return -1.0f;
		}
	}

	bool JSONValue::AsBool() const
	{
		switch (type)
		{
		case Type::INT:
			return (intValue != 0) ? 1 : 0;
		case Type::UINT:
			return (uintValue != 0) ? 1 : 0;
		case Type::LONG:
			return (longValue != 0) ? 1 : 0;
		case Type::ULONG:
			return (ulongValue != 0) ? 1 : 0;
		case Type::FLOAT:
			return (floatValue != 0.0f ? 1 : 0);
		case Type::BOOL:
			return (boolValue != 0 ? 1 : 0);
		default:
			PrintError("AsBool was called on non-bool value\\n");
			return false;
		}
	}

	std::string JSONValue::AsString() const
	{
		switch (type)
		{
		case Type::STRING:
			return strValue;
		default:
			PrintError("AsFloat was called on non-string value\\n");
			return "";
		}
	}

	bool JSONObject::HasField(const std::string& label) const
	{
		for (const JSONField& field : fields)
		{
			if (field.label == label)
			{
				return true;
			}
		}
		return false;
	}

	std::string JSONObject::GetString(const std::string& label) const
	{
		for (const JSONField& field : fields)
		{
			if (field.label == label)
			{
				return field.value.strValue;
			}
		}
		return "";
	}

	bool JSONObject::TryGetString(const std::string& label, std::string& value) const
	{
		// TODO: PERFORMANCE: Return index of found item to avoid duplicate looping
		if (HasField(label))
		{
			value = GetString(label);
			return true;
		}
		return false;
	}

	StringID JSONObject::GetStringID(const std::string& label) const
	{
		if (HasField(label))
		{
			u64 result = GetULong(label);
			return (StringID)result;
		}
		return InvalidStringID;
	}

	bool JSONObject::TryGetStringID(const std::string& label, StringID& value) const
	{
		if (HasField(label))
		{
			value = (StringID)GetULong(label);
			return true;
		}
		return false;
	}

	bool JSONObject::TryGetVec2(const std::string& label, glm::vec2& value) const
	{
		if (HasField(label))
		{
			value = ParseVec2(GetString(label));
			ENSURE(!IsNanOrInf(value));
			return true;
		}
		return false;
	}

	bool JSONObject::TryGetVec3(const std::string& label, glm::vec3& value) const
	{
		if (HasField(label))
		{
			value = ParseVec3(GetString(label));
			ENSURE(!IsNanOrInf(value));
			return true;
		}
		return false;
	}

	bool JSONObject::TryGetVec4(const std::string& label, glm::vec4& value) const
	{
		if (HasField(label))
		{
			value = ParseVec4(GetString(label));
			ENSURE(!IsNanOrInf(value));
			return true;
		}
		return false;
	}

	glm::vec2 JSONObject::GetVec2(const std::string& label) const
	{
		if (HasField(label))
		{
			glm::vec2 value = ParseVec2(GetString(label));
			ENSURE(!IsNanOrInf(value));
			return value;
		}
		return VEC2_ZERO;
	}

	glm::vec3 JSONObject::GetVec3(const std::string& label) const
	{
		if (HasField(label))
		{
			glm::vec3 value = ParseVec3(GetString(label));
			ENSURE(!IsNanOrInf(value));
			return value;
		}
		return VEC3_ZERO;
	}

	glm::vec4 JSONObject::GetVec4(const std::string& label) const
	{
		if (HasField(label))
		{
			glm::vec4 value = ParseVec4(GetString(label));
			ENSURE(!IsNanOrInf(value));
			return value;
		}
		return VEC4_ZERO;
	}

	i32 JSONObject::GetInt(const std::string& label) const
	{
		for (const JSONField& field : fields)
		{
			if (field.label == label)
			{
				return field.value.intValue;
			}
		}
		return 0;
	}

	bool JSONObject::TryGetInt(const std::string& label, i32& value) const
	{
		if (HasField(label))
		{
			value = GetInt(label);
			return true;
		}
		return false;
	}

	u32 JSONObject::GetUInt(const std::string& label) const
	{
		for (const JSONField& field : fields)
		{
			if (field.label == label)
			{
				return field.value.uintValue;
			}
		}
		return 0;
	}

	bool JSONObject::TryGetUInt(const std::string& label, u32& value) const
	{
		if (HasField(label))
		{
			value = GetUInt(label);
			return true;
		}
		return false;
	}

	i64 JSONObject::GetLong(const std::string& label) const
	{
		for (const JSONField& field : fields)
		{
			if (field.label == label)
			{
				return field.value.longValue;
			}
		}
		return 0;
	}

	bool JSONObject::TryGetLong(const std::string& label, i64& value) const
	{
		if (HasField(label))
		{
			value = GetLong(label);
			return true;
		}
		return false;
	}

	u64 JSONObject::GetULong(const std::string& label) const
	{
		for (const JSONField& field : fields)
		{
			if (field.label == label)
			{
				// TODO: Call AsULong here
				return field.value.ulongValue;
			}
		}
		return 0;
	}

	bool JSONObject::TryGetULong(const std::string& label, u64& value) const
	{
		if (HasField(label))
		{
			value = GetULong(label);
			return true;
		}
		return false;
	}

	real JSONObject::GetFloat(const std::string& label) const
	{
		for (const JSONField& field : fields)
		{
			if (field.label == label)
			{
				// A float might be written without a decimal, making the system think it's an int
				if (field.value.floatValue != 0.0f)
				{
					ENSURE(!IsNanOrInf(field.value.floatValue));
					return field.value.floatValue;
				}
				ENSURE(!IsNanOrInf((real)field.value.intValue));
				return (real)field.value.intValue;
			}
		}
		return 0.0f;
	}

	bool JSONObject::TryGetFloat(const std::string& label, real& value) const
	{
		if (HasField(label))
		{
			value = GetFloat(label);
			ENSURE(!IsNanOrInf(value));
			return true;
		}
		return false;
	}

	bool JSONObject::GetBool(const std::string& label) const
	{
		for (const JSONField& field : fields)
		{
			if (field.label == label)
			{
				return field.value.boolValue;
			}
		}
		return false;
	}

	bool JSONObject::TryGetBool(const std::string& label, bool& value) const
	{
		if (HasField(label))
		{
			value = GetBool(label);
			return true;
		}
		return false;
	}

	GUID JSONObject::GetGUID(const std::string& label) const
	{
		for (const JSONField& field : fields)
		{
			if (field.label == label)
			{
				return GUID::FromString(field.value.strValue);
			}
		}
		return InvalidGUID;
	}

	bool JSONObject::TryGetGUID(const std::string& label, GUID& value) const
	{
		if (HasField(label))
		{
			value = GetGUID(label);
			return true;
		}
		return false;
	}

	GameObjectID JSONObject::GetGameObjectID(const std::string& label) const
	{
		GUID guid = GetGUID(label);
		return *(GameObjectID*)&guid;
	}

	bool JSONObject::TryGetGameObjectID(const std::string& label, GameObjectID& value) const
	{
		return TryGetGUID(label, *(GUID*)&value);
	}

	const std::vector<JSONField>& JSONObject::GetFieldArray(const std::string& label) const
	{
		for (const JSONField& field : fields)
		{
			if (field.label == label)
			{
				return field.value.fieldArrayValue;
			}
		}
		return s_EmptyFieldArray;
	}

	bool JSONObject::TryGetFieldArray(const std::string& label, std::vector<JSONField>& value) const
	{
		if (HasField(label))
		{
			value = GetFieldArray(label);
			return true;
		}
		return false;
	}

	const std::vector<JSONObject>& JSONObject::GetObjectArray(const std::string& label) const
	{
		for (const JSONField& field : fields)
		{
			if (field.label == label)
			{
				return field.value.objectArrayValue;
			}
		}
		return s_EmptyObjectArray;
	}

	bool JSONObject::TryGetObjectArray(const std::string& label, std::vector<JSONObject>& value) const
	{
		if (HasField(label))
		{
			value = GetObjectArray(label);
			return true;
		}
		return false;
	}

	const JSONObject& JSONObject::GetObject(const std::string& label) const
	{
		for (const JSONField& field : fields)
		{
			if (field.label == label)
			{
				return field.value.objectValue;
			}
		}
		return s_EmptyObject;
	}

	bool JSONObject::TryGetObject(const std::string& label, JSONObject& value) const
	{
		if (HasField(label))
		{
			value = GetObject(label);
			return true;
		}
		return false;
	}


	JSONField::JSONField()
	{
	}

	JSONField::JSONField(const std::string& label, const JSONValue& value) :
		label(label),
		value(value)
	{
	}

	std::string JSONField::ToString(i32 tabCount) const
	{
		const std::string tabs(tabCount, '\t');
		std::string result(tabs);

		if (!label.empty())
		{
			result += '\"' + label + "\" : ";
		}

		switch (value.type)
		{
		case JSONValue::Type::STRING:
			result += '\"' + value.strValue + '\"';
			break;
		case JSONValue::Type::INT:
			result += IntToString(value.intValue);
			break;
		case JSONValue::Type::UINT:
			result += UIntToString(value.uintValue);
			break;
		case JSONValue::Type::LONG:
			result += LongToString(value.longValue);
			break;
		case JSONValue::Type::ULONG:
			result += ULongToString(value.ulongValue);
			break;
		case JSONValue::Type::FLOAT:
			result += FloatToString(value.floatValue, value.floatPrecision);
			break;
		case JSONValue::Type::BOOL:
			result += (value.boolValue ? "true" : "false");
			break;
		case JSONValue::Type::OBJECT:
			result += '\n' + tabs + "{\n";
			for (u32 i = 0; i < value.objectValue.fields.size(); ++i)
			{
				result += value.objectValue.fields[i].ToString(tabCount + 1);
				if (i != value.objectValue.fields.size() - 1)
				{
					result += ",\n";
				}
				else
				{
					result += '\n';
				}
			}
			result += tabs + "}";
			break;
		case JSONValue::Type::OBJECT_ARRAY:
			result += '\n' + tabs + "[\n";
			for (u32 i = 0; i < value.objectArrayValue.size(); ++i)
			{
				result += value.objectArrayValue[i].ToString(tabCount + 1);
				if (i != value.objectArrayValue.size() - 1)
				{
					result += ",\n";
				}
				else
				{
					result += '\n';
				}
			}
			result += tabs + "]";
			break;
		case JSONValue::Type::FIELD_ARRAY:
			result += '\n' + tabs + "[\n";
			for (u32 i = 0; i < value.fieldArrayValue.size(); ++i)
			{
				result += tabs + "\t" + value.fieldArrayValue[i].ToString(0);

				if (i != value.fieldArrayValue.size() - 1)
				{
					result += ",\n";
				}
				else
				{
					result += '\n';
				}
			}
			result += tabs + "]";
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

	std::string JSONObject::ToString(i32 tabCount /* = 0 */) const
	{
		// TODO: Use StringBuilder in PrintFunctions
		const std::string tabs(tabCount, '\t');
		std::string result(tabs + "{\n");

		for (u32 i = 0; i < fields.size(); ++i)
		{
			result += fields[i].ToString(tabCount + 1);
			if (i != fields.size() - 1)
			{
				result += ",\n";
			}
			else
			{
				result += '\n';
			}
		}

		result += tabs + "}";

		return result;
	}

} // namespace flex
