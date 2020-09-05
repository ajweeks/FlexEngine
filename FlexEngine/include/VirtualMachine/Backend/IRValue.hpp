#pragma once

namespace flex
{
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
				IDENTIFIER,
				UNARY,
				BINARY,
				FUNC_CALL,
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
				"identifier",
				"unary",
				"binary",
				"func call",
				"cast",

				"NONE"
			};

			static_assert(ARRAY_LENGTH(g_TypeStrings) == ((size_t)Type::_NONE + 1), "Length of g_TypeStrings must match length of IR::Value::Type enum");

			static const char* TypeToString(Type type);

			static Type FromASTTypeName(AST::TypeName typeName);

			static bool IsLiteral(Type type);

			Value(State* irState) :
				type(Type::_NONE),
				valInt(0),
				irState(irState)
			{}

			explicit Value(State* irState, i32 val) :
				type(Type::INT),
				valInt(val),
				irState(irState)
			{}

			explicit Value(State* irState, real val) :
				type(Type::FLOAT),
				valFloat(val),
				irState(irState)
			{}

			explicit Value(State* irState, bool val) :
				type(Type::BOOL),
				valBool(val ? 1 : 0),
				irState(irState)
			{}

			explicit Value(State* irState, char* val) :
				type(Type::STRING),
				valStr(val),
				irState(irState)
			{}

			explicit Value(State* irState, char val) :
				type(Type::CHAR),
				valChar(val),
				irState(irState)
			{}

			// Non "POD" types
			explicit Value(State* irState, Type type) :
				type(type),
				valInt(0),
				irState(irState)
			{}

			virtual ~Value()
			{}

			Value(const Value& other);
			Value(const Value&& other);
			explicit Value(const VM::Value& other);

			i32 AsInt() const;
			real AsFloat() const;
			i32 AsBool() const;
			char* AsString() const;
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

			virtual std::string ToString() const;

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
