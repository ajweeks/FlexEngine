#pragma once

#undef VOID

namespace flex
{
	namespace IR
	{
		struct Value;
	}

	namespace VM
	{
		class VirtualMachine;

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

				_NONE
			};

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
				valBool(val ? 1 : 0)
			{}

			explicit Value(const char* val) :
				type(Type::STRING),
				valStr(val)
			{}

			explicit Value(char val) :
				type(Type::CHAR),
				valChar(val)
			{}

			explicit Value(const Value& other)
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

			Value(const Value&& other) :
				type(other.type),
				valInt(other.valInt)
			{
			}

			explicit Value(const IR::Value& other);

			std::string ToString() const;

			i32 AsInt() const;
			real AsFloat() const;
			i32 AsBool() const;
			const char* AsString() const;
			char AsChar() const;

			bool IsZero() const;
			bool IsPositive() const;

			Value& operator=(const Value& other);
			Value& operator=(const Value&& other);
			bool operator<(const Value& other);
			bool operator<=(const Value& other);
			bool operator>(const Value& other);
			bool operator>=(const Value& other);
			bool operator==(const Value& other);
			bool operator!=(const Value& other);

			Type type = Type::_NONE;
			union
			{
				i32 valInt;
				real valFloat;
				i32 valBool;
				const char* valStr;
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

		extern Value g_EmptyVMValue;

		struct ValueWrapper
		{
			enum class Type
			{
				REGISTER,
				CONSTANT,
				TERMINAL_OUTPUT,
				NONE
			};

			ValueWrapper() :
				type(Type::NONE),
				value(g_EmptyVMValue)
			{}

			ValueWrapper(Type type, const Value& value) :
				type(type),
				value(value)
			{}

			Type type;
			Value value;

			Value& Get(VirtualMachine* vm);
			Value& GetW(VirtualMachine* vm);
			bool Valid() const;
			std::string ToString() const;
		};

	} // namespace VM
} // namespace flex
