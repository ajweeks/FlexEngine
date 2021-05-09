#pragma once

#include "VirtualMachine/Frontend/Span.hpp"

#undef VOID

namespace flex
{
	struct Variant;

	namespace VM
	{
		struct Value;
	}

	namespace AST
	{
		enum class TypeName;
	}

	namespace IR
	{
		struct State;

		struct Value
		{
			enum class Type
			{
				INT,
				FLOAT,
				BOOL,
				STRING,
				CHAR,
				VOID,
				IDENTIFIER,
				UNARY,
				BINARY,
				TERNARY,
				CALL,
				ARGUMENT,
				CAST,

				_NONE
			};

			static constexpr const char* g_TypeStrings[] =
			{
				"int",
				"float",
				"bool",
				"string",
				"char",
				"void",
				"identifier",
				"unary",
				"binary",
				"ternary",
				"call",
				"argument",
				"cast",

				"NONE"
			};

			static_assert(ARRAY_LENGTH(g_TypeStrings) == ((size_t)Type::_NONE + 1), "Length of g_TypeStrings must match length of IR::Value::Type enum");

			static const char* TypeToString(Type type);

			virtual std::string ToString() const;

			static Type FromASTTypeName(AST::TypeName typeName);

			static bool IsLiteral(Type type);
			static bool IsNumeric(Type type);

			static bool IsSimple(Type type);

			Value(Span origin, State* irState) :
				origin(origin),
				type(Type::_NONE),
				valInt(0),
				irState(irState)
			{}

			explicit Value(Span origin, State* irState, i32 val) :
				origin(origin),
				type(Type::INT),
				valInt(val),
				irState(irState)
			{}

			explicit Value(Span origin, State* irState, real val) :
				origin(origin),
				type(Type::FLOAT),
				valFloat(val),
				irState(irState)
			{}

			explicit Value(Span origin, State* irState, bool val) :
				origin(origin),
				type(Type::BOOL),
				valBool(val ? 1 : 0),
				irState(irState)
			{}

			explicit Value(Span origin, State* irState, char* val) :
				origin(origin),
				type(Type::STRING),
				valStr(val),
				irState(irState)
			{}

			explicit Value(Span origin, State* irState, char val) :
				origin(origin),
				type(Type::CHAR),
				valChar(val),
				irState(irState)
			{}

			// Non "POD" types
			explicit Value(Span origin, State* irState, Type type) :
				origin(origin),
				type(type),
				valInt(0),
				irState(irState)
			{}

			virtual ~Value()
			{}

			Value(const Value& other);
			Value(const Value&& other);
			explicit Value(const Variant& other);

			i32 AsInt() const;
			real AsFloat() const;
			i32 AsBool() const;
			const char* AsString() const;
			char AsChar() const;

			Value& operator=(const Value& other);
			Value& operator=(const Value&& other);
			bool operator<(const Value& other);
			bool operator<=(const Value& other);
			bool operator>(const Value& other);
			bool operator>=(const Value& other);
			bool operator==(const Value& other);
			bool operator!=(const Value& other);

			virtual void Destroy()
			{}

			bool IsZero() const;
			bool IsPositive() const;


			Type type = Type::_NONE;
			union
			{
				i32 valInt;
				real valFloat;
				i32 valBool;
				char* valStr;
				char valChar;
			};

			static Type CheckAssignmentType(State* irState, Value const* lhs, Value const* rhs);

			static bool TypesAreCoercible(State* irState, Value const * lhs, Value const * rhs, Type& outResultType);
			static bool TypeAssignable(State* irState, Type lhsType, Value const * rhs);
			bool ConvertableTo(Value const * other) const;
			bool ConvertableTo(Type otherType) const;

			State* irState = nullptr;

			Span origin;

		private:
			void CheckAssignmentType(Value const * other);


		};

		Value operator+(const Value& lhs, const Value& rhs);
		Value operator-(const Value& lhs, const Value& rhs);
		Value operator*(const Value& lhs, const Value& rhs);
		Value operator/(const Value& lhs, const Value& rhs);
		Value operator%(const Value& lhs, const Value& rhs);
		Value operator^(const Value& lhs, const Value& rhs);
		Value operator&(const Value& lhs, const Value& rhs);
		Value operator&&(const Value& lhs, const Value& rhs);
		Value operator|(const Value& lhs, const Value& rhs);
		Value operator||(const Value& lhs, const Value& rhs);

		extern Value* g_EmptyIRValue;
	}// namespace IR
} // namespace flex
