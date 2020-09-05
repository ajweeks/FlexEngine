#include "stdafx.hpp"

#include "VirtualMachine/Backend/IRValue.hpp"

#include "Helpers.hpp"
#include "VirtualMachine/Backend/VirtualMachine.hpp"
#include "VirtualMachine/Backend/VMValue.hpp"
#include "VirtualMachine/Frontend/Parser.hpp"

namespace flex
{
	namespace IR
	{
		Value* g_EmptyIRValue = nullptr;

		const char* Value::TypeToString(Type type)
		{
			return g_TypeStrings[(u32)type];
		}

		Value::Type Value::FromASTTypeName(AST::TypeName typeName)
		{
			switch (typeName)
			{
			case AST::TypeName::INT:	return Value::Type::INT;
			case AST::TypeName::FLOAT:	return Value::Type::FLOAT;
			case AST::TypeName::BOOL:	return Value::Type::BOOL;
			case AST::TypeName::STRING: return Value::Type::STRING;
			case AST::TypeName::CHAR:	return Value::Type::CHAR;
			default:					return Value::Type::_NONE;
			}
		}

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

			memcpy(&valInt, &other.valInt, sizeof(void*));
		}

		Value::Value(const Value&& other) :
			type(other.type)
		{
			memcpy(&valInt, &other.valInt, sizeof(void*));
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

			memcpy(&valInt, &other.valInt, sizeof(void*));
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
				PrintError("%d: Unhandled type\n", __LINE__);
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
				PrintError("%d: Unhandled type\n", __LINE__);
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
				PrintError("%d: Unhandled type\n", __LINE__);
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
				PrintError("%d: Unhandled type\n", __LINE__);
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
				PrintError("%d: Unhandled type\n", __LINE__);
				return 0;
			}
		}

		Value& Value::operator=(const Value& other)
		{
			CheckAssignmentType(&other);

			valInt = other.valInt;

			return *this;
		}

		Value& Value::operator=(const Value&& other)
		{
			if (this != &other)
			{
				CheckAssignmentType(&other);

				valInt = other.valInt;
			}

			return *this;
		}

		Value operator+(const Value& lhs, const Value& rhs)
		{
			Value result(lhs.irState, Value::CheckAssignmentType(lhs.irState, &lhs, &rhs));

			switch (result.type)
			{
			case Value::Type::INT:
				result.valInt = lhs.AsInt() + rhs.AsInt();
				break;
			case Value::Type::FLOAT:
				result.valFloat = lhs.AsFloat() + rhs.AsFloat();
				break;
			default:
				PrintError("Attempted to add non-numeric types!\n");
				break;
			}

			return result;
		}

		Value operator-(const Value& lhs, const Value& rhs)
		{
			Value result(lhs.irState, Value::CheckAssignmentType(lhs.irState, &lhs, &rhs));
			switch (result.type)
			{
			case Value::Type::INT:
				result.valInt = lhs.AsInt() - rhs.AsInt();
				break;
			case Value::Type::FLOAT:
				result.valFloat = lhs.AsFloat() - rhs.AsFloat();
				break;
			default:
				PrintError("Attempted to subtract non-numeric types!\n");
				break;
			}

			return result;
		}

		Value operator*(const Value& lhs, const Value& rhs)
		{
			Value result(lhs.irState, Value::CheckAssignmentType(lhs.irState, &lhs, &rhs));
			switch (result.type)
			{
			case Value::Type::INT:
				result.valInt = lhs.AsInt() * rhs.AsInt();
				break;
			case Value::Type::FLOAT:
				result.valFloat = lhs.AsFloat() * rhs.AsFloat();
				break;
			default:
				PrintError("Attempted to multiply non-numeric types!\n");
				break;
			}

			return result;
		}

		Value operator/(const Value& lhs, const Value& rhs)
		{
			Value result(lhs.irState, Value::CheckAssignmentType(lhs.irState, &lhs, &rhs));
			switch (result.type)
			{
			case Value::Type::INT:
				result.valInt = lhs.AsInt() / rhs.AsInt();
				break;
			case Value::Type::FLOAT:
				result.valFloat = lhs.AsFloat() / rhs.AsFloat();
				break;
			default:
				PrintError("Attempted to divide non-numeric types!\n");
				break;
			}

			return result;
		}

		Value operator%(const Value& lhs, const Value& rhs)
		{
			Value result(lhs.irState, Value::CheckAssignmentType(lhs.irState, &lhs, &rhs));
			switch (result.type)
			{
			case Value::Type::INT:
				result.valInt = lhs.AsInt() % rhs.AsInt();
				break;
			case Value::Type::FLOAT:
				result.valFloat = fmod(lhs.AsFloat(), rhs.AsFloat());
				break;
			default:
				PrintError("Attempted to modulo non-numeric types!\n");
				break;
			}

			return result;
		}

		Value operator^(const Value& lhs, const Value& rhs)
		{
			Value result(lhs.irState, Value::CheckAssignmentType(lhs.irState, &lhs, &rhs));
			switch (result.type)
			{
			case Value::Type::INT:
				result.valInt = lhs.AsInt() ^ rhs.AsInt();
				break;
			default:
				PrintError("Attempted to xor non-numeric types!\n");
				break;
			}

			return result;
		}

		Value operator&(const Value& lhs, const Value& rhs)
		{
			Value result(lhs.irState, Value::CheckAssignmentType(lhs.irState, &lhs, &rhs));
			switch (result.type)
			{
			case Value::Type::INT:
				result.valInt = lhs.AsInt() & rhs.AsInt();
				break;
			case Value::Type::BOOL:
				result.valBool = (bool)((i32)lhs.AsBool() & (i32)rhs.AsBool());
			default:
				PrintError("Attempted to binary and invalid types!\n");
				break;
			}

			return result;
		}

		Value operator&&(const Value& lhs, const Value& rhs)
		{
			Value result(lhs.irState, Value::CheckAssignmentType(lhs.irState, &lhs, &rhs));
			switch (result.type)
			{
			case Value::Type::INT:
				result.valInt = lhs.AsInt() && rhs.AsInt();
				break;
			case Value::Type::BOOL:
				result.valBool = (bool)((i32)lhs.AsBool() && (i32)rhs.AsBool());
			default:
				PrintError("Attempted to boolean and invalid types!\n");
				break;
			}

			return result;
		}

		Value operator|(const Value& lhs, const Value& rhs)
		{
			Value result(lhs.irState, Value::CheckAssignmentType(lhs.irState, &lhs, &rhs));
			switch (result.type)
			{
			case Value::Type::INT:
				result.valInt = lhs.AsInt() | rhs.AsInt();
				break;
			case Value::Type::BOOL:
				result.valBool = (bool)((i32)lhs.AsBool() | (i32)rhs.AsBool());
			default:
				PrintError("Attempted to binary or invalid types!\n");
				break;
			}

			return result;
		}

		Value operator||(const Value& lhs, const Value& rhs)
		{
			Value result(lhs.irState, Value::CheckAssignmentType(lhs.irState, &lhs, &rhs));
			switch (result.type)
			{
			case Value::Type::INT:
				result.valInt = lhs.AsInt() || rhs.AsInt();
				break;
			case Value::Type::BOOL:
				result.valBool = (bool)((i32)lhs.AsBool() || (i32)rhs.AsBool());
			default:
				PrintError("Attempted to boolean or invalid types!\n");
				break;
			}

			return result;
		}

		bool Value::operator<(const Value& other)
		{
			if (ConvertableTo(Type::BOOL) && other.ConvertableTo(Type::BOOL))
			{
				type = Type::BOOL;
				switch (type)
				{
				case Type::INT:
					return valInt < other.AsInt();
				case Type::FLOAT:
					return valFloat < other.AsFloat();
				default:
					PrintError("Attempted to compare non-numeric types!\n");
					return false;
				}
			}

			return false;
		}

		bool Value::operator<=(const Value& other)
		{
			if (ConvertableTo(Type::BOOL) && other.ConvertableTo(Type::BOOL))
			{
				type = Type::BOOL;
				switch (type)
				{
				case Type::INT:
					return valInt <= other.AsInt();
				case Type::FLOAT:
					return valFloat <= other.AsFloat();
				default:
					PrintError("Attempted to compare non-numeric types!\n");
					return false;
				}
			}

			PrintError("Type not convertable to bool\n");
			return false;
		}

		bool Value::operator>(const Value& other)
		{
			if (ConvertableTo(Type::BOOL) && other.ConvertableTo(Type::BOOL))
			{
				type = Type::BOOL;
				switch (type)
				{
				case Type::INT:
					return valInt > other.AsInt();
				case Type::FLOAT:
					return valFloat > other.AsFloat();
				default:
					PrintError("Attempted to compare non-numeric types!\n");
					return false;
				}
			}

			PrintError("Type not convertable to bool\n");
			return false;
		}

		bool Value::operator>=(const Value& other)
		{
			if (ConvertableTo(Type::BOOL) && other.ConvertableTo(Type::BOOL))
			{
				type = Type::BOOL;
				switch (type)
				{
				case Type::INT:
					return valInt >= other.AsInt();
				case Type::FLOAT:
					return valFloat >= other.AsFloat();
				default:
					PrintError("Attempted to compare non-numeric types!\n");
					return false;
				}
			}

			PrintError("Type not convertable to bool\n");
			return false;
		}

		bool Value::operator==(const Value& other)
		{
			CheckAssignmentType(&other);
			switch (type)
			{
			case Type::INT:
				return valInt == other.AsInt();
			case Type::FLOAT:
				return valFloat == other.AsFloat();
			case Type::BOOL:
				return valBool == other.AsBool();
			case Type::STRING:
				return strcmp(AsString(), other.AsString()) == 0;
			case Type::CHAR:
				return valChar == other.AsChar();
			default:
				PrintError("%d: Unhandled type\n", __LINE__);
				return false;
			}
		}

		bool Value::operator!=(const Value& other)
		{
			CheckAssignmentType(&other);
			switch (type)
			{
			case Type::INT:
				return valInt != other.AsInt();
			case Type::FLOAT:
				return valFloat != other.AsFloat();
			case Type::BOOL:
				return valBool != other.AsBool();
			case Type::STRING:
				return strcmp(AsString(), other.AsString()) != 0;
			case Type::CHAR:
				return valChar != other.AsChar();
			default:
				PrintError("%d: Unhandled type\n", __LINE__);
				return false;
			}
		}

		bool Value::TypeAssignable(State* irState, Type lhsType, Value const* rhs)
		{
			Type rhsType = irState->GetValueType(rhs);

			if (lhsType == rhsType)
			{
				return true;
			}

			if (rhsType == Type::CAST)
			{
				IR::CastValue* castValue = (IR::CastValue*)rhs;
				if (castValue->castedType == lhsType)
				{
					return true;
				}
			}

			irState->diagnosticContainer->AddDiagnostic(Span(0, 0), 0, 0, "Type mismatch\n");
			return false;
		}

		bool Value::TypesAreCoercible(State* irState, Value const * lhs, Value const* rhs, Type& outResultType)
		{
			Type lhsType = irState->GetValueType(lhs);
			Type rhsType = irState->GetValueType(rhs);

			if (lhsType == rhsType)
			{
				outResultType = lhsType;
				return true;
			}

			if ((lhsType == Type::INT && rhsType == Type::FLOAT) ||
				(lhsType == Type::FLOAT && rhsType == Type::INT))
			{
				outResultType = Type::FLOAT;
				return true;
			}

			irState->diagnosticContainer->AddDiagnostic(Span(0, 0), 0, 0, "Type mismatch\n");
			outResultType = Type::_NONE;
			return false;
		}

		Value::Type Value::CheckAssignmentType(State* irState, Value const* lhs, Value const* rhs)
		{
			Type lhsType = irState->GetValueType(lhs);
			Type rhsType = irState->GetValueType(rhs);

			if (lhsType == Type::_NONE)
			{
				return rhsType;
			}
			else
			{
				Type resultType;
				if (TypesAreCoercible(irState, lhs, rhs, resultType))
				{
					return resultType;
				}
				else
				{

					return Type::_NONE;
				}
			}
		}

		bool Value::ConvertableTo(Value const * other) const
		{
			Type otherType = irState->GetValueType(other);
			return ConvertableTo(otherType);
		}

		bool Value::ConvertableTo(Type otherType) const
		{
			if (type == otherType)
			{
				return true;
			}

			switch (type)
			{
			case Type::INT:
				return otherType == Type::BOOL || otherType == Type::FLOAT;
			case Type::FLOAT:
				return otherType == Type::BOOL;
			case Type::BOOL:
				return otherType == Type::INT || otherType == Type::FLOAT;
			case Type::STRING:
				return false;
			case Type::CHAR:
				return otherType == Type::INT;
			default:
				irState->diagnosticContainer->AddDiagnostic(Span(0, 0), 0, 0, "Type mismatch\n");
				return false;
			}
		}

		void Value::CheckAssignmentType(Value const * other)
		{
			Type otherType = irState->GetValueType(other);

			if (type == Type::_NONE)
			{
				type = otherType;
			}
			else
			{
				if (type != otherType)
				{
					irState->diagnosticContainer->AddDiagnostic(Span(0, 0), 0, 0, "Type mismatch\n");
				}
			}
		}
	} // namespace IR
} // namespace flex
