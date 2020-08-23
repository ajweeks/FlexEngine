#pragma once

namespace flex
{
	namespace VM
	{
		struct Value;
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

			Value(const Value& other);
			Value(const Value&& other);
			explicit Value(const VM::Value& other);

			Value& operator=(const Value& other);
			Value& operator=(const Value&& other);
			bool operator<(const Value& other);
			bool operator<=(const Value& other);
			bool operator>(const Value& other);
			bool operator>=(const Value& other);
			bool operator==(const Value& other);
			bool operator!=(const Value& other);

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
