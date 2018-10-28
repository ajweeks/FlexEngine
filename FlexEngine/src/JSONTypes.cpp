
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
				if (isDecimal || stringAfter[nextNonAlphaNumeric] == '.')
				{
					return Type::FLOAT;
				}
				else
				{
					return Type::INT;
				}
			}
		} return Type::UNINITIALIZED;
		}
	}

	JSONValue::JSONValue() :
		type(Type::UNINITIALIZED)
	{
	}

	JSONValue::JSONValue(const std::string& strValue) :
		strValue(strValue),
		type(Type::STRING)
	{
	}

	JSONValue::JSONValue(const char* strValue) :
		strValue(strValue),
		type(Type::STRING)
	{
	}

	JSONValue::JSONValue(i32 intValue) :
		intValue(intValue),
		type(Type::INT)
	{
	}

	JSONValue::JSONValue(real floatValue) :
		floatValue(floatValue),
		type(Type::FLOAT)
	{
	}

	JSONValue::JSONValue(bool boolValue) :
		boolValue(boolValue),
		type(Type::BOOL)
	{
	}

	JSONValue::JSONValue(const JSONObject& objectValue) :
		objectValue(objectValue),
		type(Type::OBJECT)
	{
	}

	JSONValue::JSONValue(const std::vector<JSONObject>& objectArrayValue) :
		objectArrayValue(objectArrayValue),
		type(Type::OBJECT_ARRAY)
	{
	}

	JSONValue::JSONValue(const std::vector<JSONField>& fieldArrayValue) :
		fieldArrayValue(fieldArrayValue),
		type(Type::FIELD_ARRAY)
	{
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

	bool JSONObject::SetStringChecked(const std::string& label, std::string& value) const
	{
		if (HasField(label))
		{
			value = GetString(label);
			return true;
		}
		return false;
	}

	bool JSONObject::SetVec2Checked(const std::string& label, glm::vec2& value) const
	{
		if (HasField(label))
		{
			value = ParseVec2(GetString(label));
			return true;
		}
		return false;
	}

	bool JSONObject::SetVec3Checked(const std::string& label, glm::vec3& value) const
	{
		if (HasField(label))
		{
			value = ParseVec3(GetString(label));
			return true;
		}
		return false;
	}

	bool JSONObject::SetVec4Checked(const std::string& label, glm::vec4& value) const
	{
		if (HasField(label))
		{
			value = ParseVec4(GetString(label));
			return true;
		}
		return false;
	}

	glm::vec2 JSONObject::GetVec2(const std::string& label) const
	{
		if (HasField(label))
		{
			return ParseVec2(GetString(label));
		}
		return VEC2_ZERO;
	}

	glm::vec3 JSONObject::GetVec3(const std::string& label) const
	{
		if (HasField(label))
		{
			return ParseVec3(GetString(label));
		}
		return VEC3_ZERO;
	}

	glm::vec4 JSONObject::GetVec4(const std::string& label) const
	{
		if (HasField(label))
		{
			return ParseVec4(GetString(label));
		}
		return VEC4_ZERO;
	}

	i32 JSONObject::GetInt(const std::string& label) const
	{
		for (const JSONField& field : fields)
		{
			if (field.label == label)
			{
				if (field.value.intValue != 0)
				{
					return field.value.intValue;
				}
				return (i32)field.value.floatValue;
			}
		}
		return 0;
	}

	bool JSONObject::SetIntChecked(const std::string& label, int & value) const
	{
		if (HasField(label))
		{
			value = GetInt(label);
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
					return field.value.floatValue;
				}
				return (real)field.value.intValue;
			}
		}
		return 0.0f;
	}

	bool JSONObject::SetFloatChecked(const std::string & label, float & value) const
	{
		if (HasField(label))
		{
			value = GetFloat(label);
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

	bool JSONObject::SetBoolChecked(const std::string& label, bool& value) const
	{
		if (HasField(label))
		{
			value = GetBool(label);
			return true;
		}
		return false;
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

	bool JSONObject::SetFieldArrayChecked(const std::string & label, std::vector<JSONField>& value) const
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

	bool JSONObject::SetObjectArrayChecked(const std::string & label, std::vector<JSONObject>& value) const
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

	bool JSONObject::SetObjectChecked(const std::string& label, JSONObject& value) const
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

	std::string JSONField::Print(i32 tabCount)
	{
		const std::string tabs(tabCount, '\t');
		std::string result(tabs + '\"' + label + "\" : ");

		switch (value.type)
		{
		case JSONValue::Type::STRING:
			result += '\"' + value.strValue + '\"';
			break;
		case JSONValue::Type::INT:
			result += std::to_string(value.intValue);
			break;
		case JSONValue::Type::FLOAT:
			result += std::to_string(value.floatValue);
			break;
		case JSONValue::Type::BOOL:
			result += (value.boolValue ? "true" : "false");
			break;
		case JSONValue::Type::OBJECT:
			result += '\n' + tabs + "{\n";
			for (u32 i = 0; i < value.objectValue.fields.size(); ++i)
			{
				result += value.objectValue.fields[i].Print(tabCount + 1);
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
				result += value.objectArrayValue[i].Print(tabCount + 1);
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
				result += value.fieldArrayValue[i].Print(tabCount + 1);
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

	std::string JSONObject::Print(i32 tabCount)
	{
		const std::string tabs(tabCount, '\t');
		std::string result(tabs + "{\n");

		for (u32 i = 0; i < fields.size(); ++i)
		{
			result += fields[i].Print(tabCount + 1);
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
