#include "stdafx.hpp"

#include "VirtualMachine/Backend/IRValue.hpp"

#include "Helpers.hpp"
#include "VirtualMachine/Backend/VirtualMachine.hpp"
#include "VirtualMachine/Backend/VMValue.hpp"

namespace flex
{
	namespace IR
	{
		Value g_EmptyIRValue = Value();

		bool Value::IsLiteral(Type type)
		{
			return type == Value::Type::INT ||
				type == Value::Type::FLOAT ||
				type == Value::Type::BOOL ||
				type == Value::Type::STRING ||
				type == Value::Type::CHAR;
		}

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

		bool Value::IsZero() const
		{
			switch (type)
			{
			case Type::INT:		return valInt == 0;
			case Type::FLOAT:	return valFloat == 0.0f;
			default:
			{
				PrintError("Value::IsZero called on non-numeric type\n");
				return false;
			}
			}
		}

		bool Value::IsPositive() const
		{

			switch (type)
			{
			case Type::INT:		return valInt > 0;
			case Type::FLOAT:	return valFloat > 0.0f;
			default:
			{
				PrintError("Value::IsZero called on non-numeric type\n");
				return false;
			}
			}
		}

		Value::Value(const Value& other)
		{
			if (type == Type::_NONE)
			{
				type = other.type;
			}
			else
			{
				assert(type == other.type);
			}

			valInt = other.valInt;
		}

		Value::Value(const Value&& other) :
			type(other.type),
			valInt(other.valInt)
		{
		}

		Value::Value(const VM::Value& other)
		{
			if (other.type == VM::Value::Type::_NONE)
			{
				type = IR::Value::Type::_NONE;
			}
			else
			{
				type = (IR::Value::Type)other.type;
			}

			valInt = other.valInt;
		}

		Value& Value::operator=(const Value& other)
		{
			CheckAssignmentType(other.type);

			valInt = other.valInt;

			return *this;
		}

		Value& Value::operator=(const Value&& other)
		{
			if (this != &other)
			{
				CheckAssignmentType(other.type);

				valInt = other.valInt;
			}

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

		Value operator^(const Value& lhs, const Value& rhs)
		{
			Value result;
			result.type = Value::CheckAssignmentType(lhs.type, rhs.type);
			switch (result.type)
			{
			case Value::Type::INT:
				result.valInt = lhs.valInt ^ rhs.valInt;
				break;
			default:
				PrintError("Attempted to xor non-numeric types!\n");
				assert(false);
				break;
			}

			return result;
		}

		Value operator&(const Value& lhs, const Value& rhs)
		{
			Value result;
			result.type = Value::CheckAssignmentType(lhs.type, rhs.type);
			switch (result.type)
			{
			case Value::Type::INT:
				result.valInt = lhs.valInt & rhs.valInt;
				break;
			case Value::Type::BOOL:
				result.valBool = (bool)((i32)lhs.valBool & (i32)rhs.valBool);
			default:
				PrintError("Attempted to binary and invalid types!\n");
				assert(false);
				break;
			}

			return result;
		}

		Value operator&&(const Value& lhs, const Value& rhs)
		{
			Value result;
			result.type = Value::CheckAssignmentType(lhs.type, rhs.type);
			switch (result.type)
			{
			case Value::Type::INT:
				result.valInt = lhs.valInt && rhs.valInt;
				break;
			case Value::Type::BOOL:
				result.valBool = (bool)((i32)lhs.valBool && (i32)rhs.valBool);
			default:
				PrintError("Attempted to boolean and invalid types!\n");
				assert(false);
				break;
			}

			return result;
		}

		Value operator|(const Value& lhs, const Value& rhs)
		{
			Value result;
			result.type = Value::CheckAssignmentType(lhs.type, rhs.type);
			switch (result.type)
			{
			case Value::Type::INT:
				result.valInt = lhs.valInt | rhs.valInt;
				break;
			case Value::Type::BOOL:
				result.valBool = (bool)((i32)lhs.valBool | (i32)rhs.valBool);
			default:
				PrintError("Attempted to binary or invalid types!\n");
				assert(false);
				break;
			}

			return result;
		}

		Value operator||(const Value& lhs, const Value& rhs)
		{
			Value result;
			result.type = Value::CheckAssignmentType(lhs.type, rhs.type);
			switch (result.type)
			{
			case Value::Type::INT:
				result.valInt = lhs.valInt || rhs.valInt;
				break;
			case Value::Type::BOOL:
				result.valBool = (bool)((i32)lhs.valBool || (i32)rhs.valBool);
			default:
				PrintError("Attempted to boolean or invalid types!\n");
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
	} // namespace IR
} // namespace flex
