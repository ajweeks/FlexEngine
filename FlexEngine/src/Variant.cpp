#include "stdafx.hpp"

#include "Variant.hpp"

#include "Helpers.hpp"
#include "VirtualMachine/Backend/IRValue.hpp"

namespace flex
{
	Variant g_EmptyVariant = Variant();

	using Type = Variant::Type;

	const char* Variant::TypeToString(Type type)
	{
		return VariantTypeNames[(u32)type];
	}

	Variant::Variant(const IR::Value& other)
	{
		switch (other.type)
		{
		case IR::Value::Type::INT: type = Type::INT; break;
		case IR::Value::Type::FLOAT: type = Type::FLOAT; break;
		case IR::Value::Type::BOOL: type = Type::BOOL; break;
		case IR::Value::Type::STRING: type = Type::STRING; break;
		case IR::Value::Type::CHAR: type = Type::CHAR; break;
		case IR::Value::Type::VOID_: type = Type::VOID_; break;
		default: type = Type::_NONE; break;
		}
		_largestField = other._largestField;
	}

	bool Variant::IsValid() const
	{
		return type != Type::_NONE;
	}

	std::string Variant::ToString() const
	{
		switch (type)
		{
		case Type::INT:		return IntToString(valInt);
		case Type::UINT:	return UIntToString(valInt);
		case Type::LONG:	return LongToString(valInt);
		case Type::ULONG:	return ULongToString(valInt);
		case Type::FLOAT:	return FloatToString(valFloat);
		case Type::BOOL:	return BoolToString(valBool);
		case Type::STRING:	return std::string(valStr);
		case Type::CHAR:	return std::string(1, valChar);
		default:			return "";
		}
	}

	bool Variant::IsIntegral(Type type)
	{
		return type == Type::INT || type == Type::UINT ||
			type == Type::LONG || type == Type::ULONG;
	}

	bool Variant::IsZero() const
	{
		switch (type)
		{
		case Type::INT:		return valInt == 0;
		case Type::UINT:	return valUInt == 0;
		case Type::LONG:	return valLong == 0;
		case Type::ULONG:	return valULong == 0;
		case Type::FLOAT:	return valFloat == 0.0f;
		case Type::BOOL:	return valBool == 0;
		default:
		{
			PrintError("Variant::IsZero called on non-numeric type\n");
			return false;
		}
		}
	}

	bool Variant::IsPositive() const
	{

		switch (type)
		{
		case Type::INT:		return valInt > 0;
		case Type::UINT:	return valUInt > 0;
		case Type::LONG:	return valLong > 0;
		case Type::ULONG:	return valULong > 0;
		case Type::FLOAT:	return valFloat > 0.0f;
		case Type::BOOL:	return valBool > 0;
		default:
		{
			PrintError("Variant::IsPositive called on non-numeric type\n");
			return false;
		}
		}
	}

	i32 Variant::AsInt() const
	{
		switch (type)
		{
		case Type::INT:
			return valInt;
			// TODO: Require explicit cast
		case Type::UINT:
			return (i32)valUInt;
		case Type::LONG:
			return (i32)valLong;
		case Type::ULONG:
			return (i32)valULong;
		case Type::FLOAT:
			return (i32)valFloat;
		case Type::BOOL:
			return (i32)valBool;
		case Type::CHAR:
			return (i32)valChar;
		default:
			PrintError("Unhandled variant type\n");
			return -1;
		}
	}

	u32 Variant::AsUInt() const
	{
		switch (type)
		{
		case Type::INT:
			return (u32)valInt;
			// TODO: Require explicit cast
		case Type::UINT:
			return valUInt;
		case Type::LONG:
			return (u32)valLong;
		case Type::ULONG:
			return (u32)valULong;
		case Type::FLOAT:
			return (u32)valFloat;
		case Type::BOOL:
			return (u32)valBool;
		case Type::CHAR:
			return (u32)valChar;
		default:
			PrintError("Unhandled variant type\n");
			return u32_max;
		}
	}

	i64 Variant::AsLong() const
	{
		switch (type)
		{
		case Type::INT:
			return (i64)valInt;
			// TODO: Require explicit cast
		case Type::UINT:
			return (i64)valUInt;
		case Type::LONG:
			return valLong;
		case Type::ULONG:
			return (i64)valULong;
		case Type::FLOAT:
			return (i64)valFloat;
		case Type::BOOL:
			return (i64)valBool;
		case Type::CHAR:
			return (i64)valChar;
		default:
			PrintError("Unhandled variant type\n");
			return -1;
		}
	}

	u64 Variant::AsULong() const
	{
		switch (type)
		{
		case Type::INT:
			return (u64)valInt;
			// TODO: Require explicit cast
		case Type::UINT:
			return (u64)valUInt;
		case Type::LONG:
			return (u64)valLong;
		case Type::ULONG:
			return valULong;
		case Type::FLOAT:
			return (u64)valFloat;
		case Type::BOOL:
			return (u64)valBool;
		case Type::CHAR:
			return (u64)valChar;
		default:
			PrintError("Unhandled variant type\n");
			return u64_max;
		}
	}

	real Variant::AsFloat() const
	{
		switch (type)
		{
		case Type::INT:
			return (real)valInt;
		case Type::UINT:
			return (real)valUInt;
		case Type::LONG:
			return (real)valLong;
		case Type::ULONG:
			return (real)valULong;
		case Type::FLOAT:
			return valFloat;
		case Type::BOOL:
			return (valBool != 0) ? 1.0f : 0.0f;
		default:
			PrintError("Unhandled variant type\n");
			return -1.0f;
		}
	}

	i32 Variant::AsBool() const
	{
		switch (type)
		{
		case Type::INT:
			return (valInt != 0) ? 1 : 0;
		case Type::UINT:
			return (valUInt != 0) ? 1 : 0;
		case Type::LONG:
			return (valLong != 0) ? 1 : 0;
		case Type::ULONG:
			return (valULong != 0) ? 1 : 0;
		case Type::FLOAT:
			return (valFloat != 0.0f ? 1 : 0);
		case Type::BOOL:
			return (valBool != 0 ? 1 : 0);
		default:
			PrintError("Unhandled variant type\n");
			return false;
		}
	}

	const char* Variant::AsString() const
	{
		switch (type)
		{
		case Type::STRING:
			return valStr;
		default:
			PrintError("Unhandled variant type\n");
			return "";
		}
	}

	char Variant::AsChar() const
	{
		switch (type)
		{
		case Type::INT:
			return (char)valInt;
		case Type::UINT:
			return (char)valUInt;
		case Type::LONG:
			return (char)valLong;
		case Type::ULONG:
			return (char)valULong;
		case Type::FLOAT:
			// TODO: Require explicit cast
			return (char)valFloat;
		case Type::CHAR:
			return valChar;
		default:
			PrintError("Unhandled variant type\n");
			return 0;
		}
	}

	Variant& Variant::operator=(const Variant& other)
	{
		CheckAssignmentType(other.type);

		_largestField = other._largestField;

		return *this;
	}

	Variant& Variant::operator=(const Variant&& other)
	{
		if (this != &other)
		{
			CheckAssignmentType(other.type);

			_largestField = other._largestField;
		}

		return *this;
	}

	Variant operator+(const Variant& lhs, const Variant& rhs)
	{
		Variant result;
		result.type = Variant::CheckAssignmentType(lhs.type, rhs.type);

		switch (result.type)
		{
		case Type::INT:
			result.valInt = lhs.valInt + rhs.valInt;
			break;
		case Type::UINT:
			result.valUInt = lhs.valUInt + rhs.valUInt;
			break;
		case Type::LONG:
			result.valLong = lhs.valLong + rhs.valLong;
			break;
		case Type::ULONG:
			result.valULong = lhs.valULong + rhs.valULong;
			break;
		case Type::FLOAT:
			result.valFloat = lhs.valFloat + rhs.valFloat;
			break;
		default:
			PrintError("Attempted to add non-numeric types!\n");
			break;
		}

		return result;
	}

	Variant operator-(const Variant& lhs, const Variant& rhs)
	{
		Variant result;
		result.type = Variant::CheckAssignmentType(lhs.type, rhs.type);
		switch (result.type)
		{
		case Type::INT:
			result.valInt = lhs.valInt - rhs.valInt;
			break;
		case Type::UINT:
			result.valUInt = lhs.valUInt - rhs.valUInt;
			break;
		case Type::LONG:
			result.valLong = lhs.valLong - rhs.valLong;
			break;
		case Type::ULONG:
			result.valULong = lhs.valULong - rhs.valULong;
			break;
		case Type::FLOAT:
			result.valFloat = lhs.valFloat - rhs.valFloat;
			break;
		case Type::BOOL:
			// Used by compare operator
			result.valBool = (lhs.valBool != rhs.valBool) ? 1 : 0;
			break;
		default:
			PrintError("Attempted to subtract non-numeric types!\n");
			break;
		}

		return result;
	}

	Variant operator*(const Variant& lhs, const Variant& rhs)
	{
		Variant result;
		result.type = Variant::CheckAssignmentType(lhs.type, rhs.type);
		switch (result.type)
		{
		case Type::INT:
			result.valInt = lhs.valInt * rhs.valInt;
			break;
		case Type::UINT:
			result.valUInt = lhs.valUInt * rhs.valUInt;
			break;
		case Type::LONG:
			result.valLong = lhs.valLong * rhs.valLong;
			break;
		case Type::ULONG:
			result.valULong = lhs.valULong * rhs.valULong;
			break;
		case Type::FLOAT:
			result.valFloat = lhs.valFloat * rhs.valFloat;
			break;
		default:
			PrintError("Attempted to multiply non-numeric types!\n");
			break;
		}

		return result;
	}

	Variant operator/(const Variant& lhs, const Variant& rhs)
	{
		Variant result;
		result.type = Variant::CheckAssignmentType(lhs.type, rhs.type);
		switch (result.type)
		{
		case Type::INT:
			result.valInt = lhs.valInt / rhs.valInt;
			break;
		case Type::UINT:
			result.valUInt = lhs.valUInt / rhs.valUInt;
			break;
		case Type::LONG:
			result.valLong = lhs.valLong / rhs.valLong;
			break;
		case Type::ULONG:
			result.valULong = lhs.valULong / rhs.valULong;
			break;
		case Type::FLOAT:
			result.valFloat = lhs.valFloat / rhs.valFloat;
			break;
		default:
			PrintError("Attempted to divide non-numeric types!\n");
			break;
		}

		return result;
	}

	Variant operator%(const Variant& lhs, const Variant& rhs)
	{
		Variant result;
		result.type = Variant::CheckAssignmentType(lhs.type, rhs.type);
		switch (result.type)
		{
		case Type::INT:
			result.valInt = lhs.valInt % rhs.valInt;
			break;
		case Type::UINT:
			result.valUInt = lhs.valUInt % rhs.valUInt;
			break;
		case Type::LONG:
			result.valLong = lhs.valLong % rhs.valLong;
			break;
		case Type::ULONG:
			result.valULong = lhs.valULong % rhs.valULong;
			break;
		case Type::FLOAT:
			result.valFloat = fmod(lhs.valFloat, rhs.valFloat);
			break;
		default:
			PrintError("Attempted to modulo non-numeric types!\n");
			break;
		}

		return result;
	}

	Variant operator^(const Variant& lhs, const Variant& rhs)
	{
		Variant result;
		result.type = Variant::CheckAssignmentType(lhs.type, rhs.type);
		switch (result.type)
		{
		case Type::INT:
			result.valInt = lhs.valInt ^ rhs.valInt;
			break;
		case Type::UINT:
			result.valUInt = lhs.valUInt ^ rhs.valUInt;
			break;
		case Type::LONG:
			result.valLong = lhs.valLong ^ rhs.valLong;
			break;
		case Type::ULONG:
			result.valULong = lhs.valULong ^ rhs.valULong;
			break;
		default:
			PrintError("Attempted to xor non-numeric types!\n");
			break;
		}

		return result;
	}

	Variant operator&(const Variant& lhs, const Variant& rhs)
	{
		Variant result;
		result.type = Variant::CheckAssignmentType(lhs.type, rhs.type);
		switch (result.type)
		{
		case Type::INT:
			result.valInt = lhs.valInt & rhs.valInt;
			break;
		case Type::UINT:
			result.valUInt = lhs.valUInt & rhs.valUInt;
			break;
		case Type::LONG:
			result.valLong = lhs.valLong & rhs.valLong;
			break;
		case Type::ULONG:
			result.valULong = lhs.valULong & rhs.valULong;
			break;
		case Type::BOOL:
			result.valBool = (lhs.valBool & rhs.valBool);
			break;
		default:
			PrintError("Attempted to binary and invalid types!\n");
			break;
		}

		return result;
	}

	Variant operator&&(const Variant& lhs, const Variant& rhs)
	{
		Variant result;
		result.type = Variant::CheckAssignmentType(lhs.type, rhs.type);
		switch (result.type)
		{
		case Type::INT:
			result.valInt = lhs.valInt && rhs.valInt;
			break;
		case Type::UINT:
			result.valUInt = lhs.valUInt && rhs.valUInt;
			break;
		case Type::LONG:
			result.valLong = lhs.valLong && rhs.valLong;
			break;
		case Type::ULONG:
			result.valULong = lhs.valULong && rhs.valULong;
			break;
		case Type::BOOL:
			result.valBool = ((lhs.valBool != 0) && (rhs.valBool != 0));
			break;
		default:
			PrintError("Attempted to boolean and invalid types!\n");
			break;
		}

		return result;
	}

	Variant operator|(const Variant& lhs, const Variant& rhs)
	{
		Variant result;
		result.type = Variant::CheckAssignmentType(lhs.type, rhs.type);
		switch (result.type)
		{
		case Type::INT:
			result.valInt = lhs.valInt | rhs.valInt;
			break;
		case Type::UINT:
			result.valUInt = lhs.valUInt | rhs.valUInt;
			break;
		case Type::LONG:
			result.valLong = lhs.valLong | rhs.valLong;
			break;
		case Type::ULONG:
			result.valULong = lhs.valULong | rhs.valULong;
			break;
		case Type::BOOL:
			result.valBool = (lhs.valBool | rhs.valBool);
			break;
		default:
			PrintError("Attempted to binary or invalid types!\n");
			break;
		}

		return result;
	}

	Variant operator||(const Variant& lhs, const Variant& rhs)
	{
		Variant result;
		result.type = Variant::CheckAssignmentType(lhs.type, rhs.type);
		switch (result.type)
		{
		case Type::INT:
			result.valInt = lhs.valInt || rhs.valInt;
			break;
		case Type::UINT:
			result.valUInt = lhs.valUInt || rhs.valUInt;
			break;
		case Type::LONG:
			result.valLong = lhs.valLong || rhs.valLong;
			break;
		case Type::ULONG:
			result.valULong = lhs.valULong || rhs.valULong;
			break;
		case Type::BOOL:
			result.valBool = ((lhs.valBool != 0) || (rhs.valBool != 0));
			break;
		default:
			PrintError("Attempted to boolean or invalid types!\n");
			break;
		}

		return result;
	}

	bool Variant::operator<(const Variant& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			return valInt < other.valInt;
		case Type::UINT:
			return valUInt < other.valUInt;
		case Type::LONG:
			return valLong < other.valLong;
		case Type::ULONG:
			return valULong < other.valULong;
		case Type::FLOAT:
			return valFloat < other.valFloat;
		default:
			PrintError("Attempted to compare non-numeric types!\n");
			return false;
		}
	}

	bool Variant::operator<=(const Variant& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			return valInt <= other.valInt;
		case Type::UINT:
			return valUInt <= other.valUInt;
		case Type::LONG:
			return valLong <= other.valLong;
		case Type::ULONG:
			return valULong <= other.valULong;
		case Type::FLOAT:
			return valFloat <= other.valFloat;
		default:
			PrintError("Attempted to compare non-numeric types!\n");
			return false;
		}
	}

	bool Variant::operator>(const Variant& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			return valInt > other.valInt;
		case Type::UINT:
			return valUInt > other.valUInt;
		case Type::LONG:
			return valLong > other.valLong;
		case Type::ULONG:
			return valULong > other.valULong;
		case Type::FLOAT:
			return valFloat > other.valFloat;
		default:
			PrintError("Attempted to compare non-numeric types!\n");
			return false;
		}
	}

	bool Variant::operator>=(const Variant& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			return valInt >= other.valInt;
		case Type::UINT:
			return valUInt >= other.valUInt;
		case Type::LONG:
			return valLong >= other.valLong;
		case Type::ULONG:
			return valULong >= other.valULong;
		case Type::FLOAT:
			return valFloat >= other.valFloat;
		default:
			PrintError("Attempted to compare non-numeric types!\n");
			return false;
		}
	}

	bool Variant::operator==(const Variant& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			return valInt == other.valInt;
		case Type::UINT:
			return valUInt == other.valUInt;
		case Type::LONG:
			return valLong == other.valLong;
		case Type::ULONG:
			return valULong == other.valULong;
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

	bool Variant::operator!=(const Variant& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			return valInt != other.valInt;
		case Type::UINT:
			return valUInt != other.valUInt;
		case Type::LONG:
			return valLong != other.valLong;
		case Type::ULONG:
			return valULong != other.valULong;
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

	Variant::Type Variant::CheckAssignmentType(Type lhsType, Type rhsType)
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

	void Variant::CheckAssignmentType(Type otherType)
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
} // namespace flex
