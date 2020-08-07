#include "stdafx.hpp"

#include "VirtualMachine/Backend/Value.hpp"

#include "Helpers.hpp"
#include "VirtualMachine/Backend/VirtualMachine.hpp"

namespace flex
{
	Value g_EmptyValue = Value();

	std::string Value::ToString() const
	{
		switch (type)
		{
		case Type::INT:		return IntToString(valInt);
		case Type::FLOAT:	return FloatToString(valFloat);
		case Type::BOOL:	return valBool ? "true" : "false";
		case Type::STRING:	return std::string(valStr);
		case Type::CHAR:	return std::string(1, valChar);
		default:			return "";
		}
	}

	Value& Value::operator=(const Value& other)
	{
		CheckAssignmentType(other.type);

		valInt = other.valInt;

		return *this;
	}

	Value& Value::operator=(const Value&& other)
	{
		CheckAssignmentType(other.type);

		valInt = other.valInt;

		return *this;
	}

	Value operator+(const Value& lhs, const Value& rhs)
	{
		Value result;
		result.type = Value::CheckAssignmentType(lhs.type, rhs.type);

		switch (result.type)
		{
		case Value::Type::INT:
			result.valInt = lhs.valInt + rhs.valInt;
			break;
		case Value::Type::FLOAT:
			result.valFloat = lhs.valFloat + rhs.valFloat;
			break;
		default:
			PrintError("Attempted to add non-numeric types!\n");
			assert(false);
			break;
		}

		return result;
	}

	Value operator-(const Value& lhs, const Value& rhs)
	{
		Value result;
		result.type = Value::CheckAssignmentType(lhs.type, rhs.type);
		switch (result.type)
		{
		case Value::Type::INT:
			result.valInt = lhs.valInt - rhs.valInt;
			break;
		case Value::Type::FLOAT:
			result.valFloat = lhs.valFloat - rhs.valFloat;
			break;
		default:
			PrintError("Attempted to subtract non-numeric types!\n");
			assert(false);
			break;
		}

		return result;
	}

	Value operator*(const Value& lhs, const Value& rhs)
	{
		Value result;
		result.type = Value::CheckAssignmentType(lhs.type, rhs.type);
		switch (result.type)
		{
		case Value::Type::INT:
			result.valInt = lhs.valInt * rhs.valInt;
			break;
		case Value::Type::FLOAT:
			result.valFloat = lhs.valFloat * rhs.valFloat;
			break;
		default:
			PrintError("Attempted to multiply non-numeric types!\n");
			assert(false);
			break;
		}

		return result;
	}

	Value operator/(const Value& lhs, const Value& rhs)
	{
		Value result;
		result.type = Value::CheckAssignmentType(lhs.type, rhs.type);
		switch (result.type)
		{
		case Value::Type::INT:
			result.valInt = lhs.valInt / rhs.valInt;
			break;
		case Value::Type::FLOAT:
			result.valFloat = lhs.valFloat / rhs.valFloat;
			break;
		default:
			PrintError("Attempted to divide non-numeric types!\n");
			assert(false);
			break;
		}

		return result;
	}

	Value operator%(const Value& lhs, const Value& rhs)
	{
		Value result;
		result.type = Value::CheckAssignmentType(lhs.type, rhs.type);
		switch (result.type)
		{
		case Value::Type::INT:
			result.valInt = lhs.valInt % rhs.valInt;
			break;
		case Value::Type::FLOAT:
			result.valFloat = fmod(lhs.valFloat, rhs.valFloat);
			break;
		default:
			PrintError("Attempted to modulo non-numeric types!\n");
			assert(false);
			break;
		}

		return result;
	}

	bool Value::operator<(const Value& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			return valInt < other.valInt;
		case Type::FLOAT:
			return valFloat < other.valFloat;
		default:
			PrintError("Attempted to compare non-numeric types!\n");
			assert(false);
			return false;
		}
	}

	bool Value::operator<=(const Value& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			return valInt <= other.valInt;
		case Type::FLOAT:
			return valFloat <= other.valFloat;
		default:
			PrintError("Attempted to compare non-numeric types!\n");
			assert(false);
			return false;
		}
	}

	bool Value::operator>(const Value& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			return valInt > other.valInt;
		case Type::FLOAT:
			return valFloat > other.valFloat;
		default:
			PrintError("Attempted to compare non-numeric types!\n");
			assert(false);
			return false;
		}
	}

	bool Value::operator>=(const Value& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			return valInt >= other.valInt;
		case Type::FLOAT:
			return valFloat >= other.valFloat;
		default:
			PrintError("Attempted to compare non-numeric types!\n");
			assert(false);
			return false;
		}
	}

	bool Value::operator==(const Value& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			return valInt == other.valInt;
		case Type::FLOAT:
			return valFloat == other.valFloat;
		case Type::BOOL:
			return valBool == other.valBool;
		case Type::STRING:
			return strcmp(valStr, other.valStr) == 0;
		case Type::CHAR:
			return valChar == other.valChar;
		default:
			assert(false);
			return false;
		}
	}

	bool Value::operator!=(const Value& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			return valInt != other.valInt;
		case Type::FLOAT:
			return valFloat != other.valFloat;
		case Type::BOOL:
			return valBool != other.valBool;
		case Type::STRING:
			return strcmp(valStr, other.valStr) != 0;
		case Type::CHAR:
			return valChar != other.valChar;
		default:
			assert(false);
			return false;
		}
	}

	Value::Type Value::CheckAssignmentType(Type lhsType, Type rhsType)
	{
		if (lhsType == Type::_NONE)
		{
			return rhsType;
		}
		else
		{
			assert(lhsType == rhsType);
			return rhsType;
		}
	}

	void Value::CheckAssignmentType(Type otherType)
	{
		if (type == Type::_NONE)
		{
			type = otherType;
		}
		else
		{
			assert(type == otherType);
		}
	}

	Value& ValueWrapper::Get(VM* vm)
	{
		if (type == Type::REGISTER)
		{
			return vm->registers[value.valInt];
		}
		else if (type == Type::CONSTANT)
		{
			return value;
		}
		else
		{
			assert(false);
			return g_EmptyValue;
		}
	}

	Value& ValueWrapper::GetW(VM* vm)
	{
		assert(type == Type::REGISTER);
		return Get(vm);
	}

	bool ValueWrapper::Valid() const
	{
		return type != Type::NONE;
	}

	std::string ValueWrapper::ToString() const
	{
		if (type == Type::REGISTER)
		{
			return "r" + value.ToString();
		}
		else
		{
			return value.ToString();
		}
	}
} // namespace flex
