#pragma once

namespace flex
{
	class VM;

	struct Value
	{
		enum class Type
		{
			INT,
			FLOAT,
			BOOL,
			STRING,
			CHAR,

			_NONE
		};

		Value() :
			type(Type::_NONE),
			valInt(0)
		{}

		Value(i32 val) :
			type(Type::INT),
			valInt(val)
		{}

		Value(real val) :
			type(Type::FLOAT),
			valFloat(val)
		{}

		Value(bool val) :
			type(Type::BOOL),
			valBool(val)
		{}

		Value(char* val) :
			type(Type::STRING),
			valStr(val)
		{}

		Value(char val) :
			type(Type::CHAR),
			valChar(val)
		{}

		Value(const Value& other)
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

		Value(const Value&& other)
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

		std::string ToString() const;

		Value& operator=(const Value& other);
		Value& operator=(const Value&& other);
		Value& operator+(const Value& other);
		Value& operator-(const Value& other);
		Value& operator*(const Value& other);
		Value& operator/(const Value& other);
		Value& operator%(const Value& other);
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
			bool valBool;
			char* valStr;
			char valChar;
		};

	private:
		void CheckAssignmentType(Type otherType);

	};

	extern Value g_EmptyValue;

	struct ValueWrapper
	{
		enum class Type
		{
			REGISTER,
			CONSTANT,
			NONE
		};

		ValueWrapper() :
			type(Type::NONE),
			value(g_EmptyValue)
		{}

		ValueWrapper(Type type, const Value& value) :
			type(type),
			value(value)
		{}

		Type type;
		Value value;

		Value& Get(VM* vm);
		Value& GetW(VM* vm);
		bool Valid() const;
		std::string ToString() const;
	};

} // namespace flex
