#include "stdafx.hpp"

#include "VirtualMachine/Backend/VMValue.hpp"

#include "Helpers.hpp"
#include "VirtualMachine/Backend/VirtualMachine.hpp"
#include "VirtualMachine/Backend/IRValue.hpp"

namespace flex
{
	namespace VM
	{
		Value g_EmptyVMValue = Value();

		Value::Value(const IR::Value& other)
		{
			type = (Value::Type)other.type;
			assert((i32)type < (u32)Value::Type::_NONE);
			valInt = other.valInt;
		}

		std::string Value::ToString() const
		{
			switch (type)
			{
			case Type::INT:		return IntToString(valInt);
			case Type::FLOAT:	return FloatToString(valFloat);
			case Type::BOOL:	return IntToString(valBool);
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

		i32 Value::AsInt() const
		{
			switch (type)
			{
			case Type::INT:
				return valInt;
			case Type::FLOAT:
				// TODO: Require explicit cast
				return (i32)valFloat;
			case Type::BOOL:
				return valBool;
			case Type::CHAR:
				return (i32)valChar;
			default:
				PrintError("Unhandled value type\n");
				return -1;
			}
		}

		real Value::AsFloat() const
		{
			switch (type)
			{
			case Type::INT:
				return (real)valInt;
			case Type::FLOAT:
				return valFloat;
			case Type::BOOL:
				return (valBool != 0) ? 1.0f : 0.0f;
			default:
				PrintError("Unhandled value type\n");
				return -1.0f;
			}
		}

		i32 Value::AsBool() const
		{
			switch (type)
			{
			case Type::INT:
				return (valInt != 0) ? 1 : 0;
			case Type::FLOAT:
				return (valFloat != 0.0f ? 1 : 0);
			case Type::BOOL:
				return (valBool != 0 ? 1 : 0);
			default:
				PrintError("Unhandled value type\n");
				return false;
			}
		}

		char* Value::AsString() const
		{
			switch (type)
			{
			case Type::STRING:
				return valStr;
			default:
				PrintError("Unhandled value type\n");
				return "";
			}
		}

		char Value::AsChar() const
		{
			switch (type)
			{
			case Type::INT:
				return (char)valInt;
			case Type::FLOAT:
				// TODO: Require explicit cast
				return (char)valFloat;
			case Type::CHAR:
				return valChar;
			default:
				PrintError("Unhandled value type\n");
				return 0;
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
				result.valBool = (lhs.valBool & rhs.valBool);
			default:
				PrintError("Attempted to binary and invalid types!\n");
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
				result.valBool = ((lhs.valBool != 0) && (rhs.valBool != 0));
			default:
				PrintError("Attempted to boolean and invalid types!\n");
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
				result.valBool = (lhs.valBool | rhs.valBool);
			default:
				PrintError("Attempted to binary or invalid types!\n");
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
				result.valBool = ((lhs.valBool != 0) || (rhs.valBool != 0));
			default:
				PrintError("Attempted to boolean or invalid types!\n");
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
				PrintError("Unhandled type\n");
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
				PrintError("Unhandled type\n");
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
				if (lhsType != rhsType)
				{
					PrintError("Potentially mismatched types\n");
				}
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
				if (type != otherType)
				{
					PrintError("Potentially mismatched types\n");
				}
			}
		}

		Value& ValueWrapper::Get(VirtualMachine* vm)
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
				PrintError("Unhandled value type\n");
				return g_EmptyVMValue;
			}
		}

		Value& ValueWrapper::GetW(VirtualMachine* vm)
		{
			if (type != Type::REGISTER)
			{
				PrintError("GetW can only be called on register values\n");
			}
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
	} // namespace VM
} // namespace flex
