#pragma once

#include <stack>

namespace flex
{
	struct DiagnosticContainer;
	struct AST;

	class VM
	{
	public:
		VM();
		~VM();

		// _ Calling convention _
		// Caller pushes registers onto stack
		// Caller pushes return address onto stack
		// Caller pushes args in reverse order onto stack
		// Called runs
		//   Called pops args first to last
		//   Called returns (with optional value)
		//   Resume address is popped off stack
		//   Return value is pushed onto stack
		// Caller resumes
		// Caller pops return value off stack (if non-void func)
		// Caller pops registers off stack

		static const i32 REGISTER_COUNT = 16;
		static const u32 MEMORY_POOL_SIZE = 32768;

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

		static Value g_EmptyValue;

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
		};

		struct FuncPtr
		{
			typedef Value(*PtrType)(const Value& val0, const Value& val1, const Value& val2, const Value& val3, const Value& val4, const Value& val5);

			FuncPtr(PtrType ptr) :
				ptr(ptr)
			{
			}

			Value operator()(const Value& val0, const Value& val1, const Value& val2, const Value& val3, const Value& val4, const Value& val5)
			{
				return ptr(val0, val1, val2, val3, val4, val5);
			}

			Value operator()(const Value& val0, const Value& val1, const Value& val2, const Value& val3, const Value& val4)
			{
				return ptr(val0, val1, val2, val3, val4, g_EmptyValue);
			}

			Value operator()(const Value& val0, const Value& val1, const Value& val2, const Value& val3)
			{
				return ptr(val0, val1, val2, val3, g_EmptyValue, g_EmptyValue);
			}

			Value operator()(const Value& val0, const Value& val1, const Value& val2)
			{
				return ptr(val0, val1, val2, g_EmptyValue, g_EmptyValue, g_EmptyValue);
			}

			Value operator()(const Value& val0, const Value& val1)
			{
				return ptr(val0, val1, g_EmptyValue, g_EmptyValue, g_EmptyValue, g_EmptyValue);
			}

			Value operator()(const Value& val0)
			{
				return ptr(val0, g_EmptyValue, g_EmptyValue, g_EmptyValue, g_EmptyValue, g_EmptyValue);
			}

			Value operator()()
			{
				return ptr(g_EmptyValue, g_EmptyValue, g_EmptyValue, g_EmptyValue, g_EmptyValue, g_EmptyValue);
			}

			PtrType ptr;
		};

		enum class OpCode
		{
			MOV,
			ADD,
			SUB,
			MUL,
			DIV,
			MOD,
			CALL,
			PUSH,
			POP,
			JMP,
			JLT,
			JLE,
			JGT,
			JGE,
			JE,
			JNE,
			YIELD,
			RETURN,
			TERMINATE
		};

		struct Instruction
		{
			Instruction(OpCode opCode) :
				opCode(opCode),
				val0(ValueWrapper::Type::CONSTANT, g_EmptyValue),
				val1(ValueWrapper::Type::CONSTANT, g_EmptyValue),
				val2(ValueWrapper::Type::CONSTANT, g_EmptyValue)
			{}

			Instruction(OpCode opCode, const ValueWrapper& val0) :
				opCode(opCode),
				val0(val0),
				val1(ValueWrapper::Type::CONSTANT, g_EmptyValue),
				val2(ValueWrapper::Type::CONSTANT, g_EmptyValue)
			{}

			Instruction(OpCode opCode, const ValueWrapper& val0, const ValueWrapper& val1) :
				opCode(opCode),
				val0(val0),
				val1(val1),
				val2(ValueWrapper::Type::CONSTANT, g_EmptyValue)
			{}

			Instruction(OpCode opCode, const ValueWrapper& val0, const ValueWrapper& val1, const ValueWrapper& val2) :
				opCode(opCode),
				val0(val0),
				val1(val1),
				val2(val2)
			{}

			OpCode opCode;
			ValueWrapper val0;
			ValueWrapper val1;
			ValueWrapper val2;
		};

		i32 instructionIdx = 0;
		std::vector<Instruction> instructions;
		std::array<Value, REGISTER_COUNT> registers;
		std::stack<Value> stack;

		u32* memory = nullptr;
		bool bTerminated = false;

		using FuncAddress = i32;
		std::map<FuncAddress, FuncPtr*> ExternalFuncTable;

		void GenerateFromAST(AST* ast);
		void GenerateFromInstStream(const std::vector<Instruction>& inInstructions);
		void Execute();

		DiagnosticContainer* diagnosticContainer = nullptr;

	private:
		void AllocateMemory();
		void ZeroOutRegisters();
		void ClearStack();

		bool IsExternal(FuncAddress funcAddress);
		i32 TranslateLocalFuncAddress(FuncAddress localFuncAddress);
		void DispatchExternalCall(FuncAddress funcAddress);

	};

} // namespace flex
