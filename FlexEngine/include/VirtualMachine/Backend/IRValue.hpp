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

				_NONE
			};

			static constexpr const char* g_TypeStrings[] =
			{
				"int",
				"float",
				"bool",
				"string",
				"char",
				"idntifier",
				"unary",
				"binary",
				"func call",

				"NONE"
			};

			static_assert(ARRAY_LENGTH(g_TypeStrings) == ((size_t)Type::_NONE + 1), "Length of g_TypeStrings must match length of IR::Value::Type enum");

			static const char* TypeToString(Type type);

			static Type FromASTTypeName(AST::TypeName typeName);

			static bool IsLiteral(Type type);

			Value() :
				type(Type::_NONE),
				valInt(0)
			{}

			explicit Value(i32 val) :
				type(Type::INT),
				valInt(val)
			{}

			explicit Value(real val) :
				type(Type::FLOAT),
				valFloat(val)
			{}

			explicit Value(bool val) :
				type(Type::BOOL),
				valBool(val)
			{}

			explicit Value(char* val) :
				type(Type::STRING),
				valStr(val)
			{}

			explicit Value(char val) :
				type(Type::CHAR),
				valChar(val)
			{}

			// Non "POD" types
			explicit Value(Type type) :
				type(type),
				valInt(0)
			{}

			virtual ~Value()
			{}

			Value(const Value& other);
			Value(const Value&& other);
			explicit Value(const VM::Value& other);

			i32 AsInt() const;
			real AsFloat() const;
			bool AsBool() const;
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
				bool valBool;
				char* valStr;
				char valChar;
			};

			static Type CheckAssignmentType(Type lhsType, Type rhsType);

			static bool TypesAreCoercible(Type lhsType, Type rhsType, Type& outResultType);
			bool ConvertableTo(Type otherType) const;

		private:
			void CheckAssignmentType(Type otherType);

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

		extern Value g_EmptyIRValue;
	}// namespace IR
} // namespace flex
