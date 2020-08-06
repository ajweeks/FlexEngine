#pragma once

#include <stack>

#include "VirtualMachine/Backend/Value.hpp"

namespace flex
{
	struct DiagnosticContainer;
	struct AST;
	struct Statement;
	struct Expression;
	struct Declaration;
	struct FunctionCall;
	enum class TypeName;
	enum class BinaryOperatorType;

	enum class OpCode
	{
		MOV,
		ADD,
		SUB,
		MUL,
		DIV,
		MOD,
		AND,
		OR,
		XOR,
		CALL,
		PUSH,
		POP,
		JMP,
		CMP,
		JEQ,
		JNE,
		JLT,
		JLE,
		JGT,
		JGE,
		YIELD,
		RETURN,
		TERMINATE,

		_NONE
	};

	static const char* s_OpCodeStrings[] =
	{
		"mov",
		"add",
		"sub",
		"mul",
		"div",
		"mod",
		"and",
		"or",
		"xor",
		"call",
		"push",
		"pop",
		"jmp",
		"cmp",
		"jeq",
		"jne",
		"jlt",
		"jle",
		"jgt",
		"jge",
		"yield",
		"return",
		"terminate",

		"NONE"
	};

	static_assert(ARRAY_LENGTH(s_OpCodeStrings) == ((size_t)OpCode::_NONE + 1), "Length of s_OpCodeStrings must match length of OpCode enum");

	const char* OpCodeToString(OpCode opCode);

	OpCode BinaryOperatorTypeToOpCode(BinaryOperatorType operatorType);
	OpCode BinaryOpToJumpCode(BinaryOperatorType operatorType);

	class VM
	{
	public:
		VM();
		~VM();

		// _ Syntax rules _
		// Vars must be declared (with type) and given an initial value prior to usage
		// Functions are global and can be called prior to definition

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

		static const i32 REGISTER_COUNT = 64;
		static const u32 MEMORY_POOL_SIZE = 32768;

		//template<typename Ret, typename... Ts>
		//struct FuncPtr
		//{
		//	typedef Ret(*PtrType)(Ts...);

		//	FuncPtr(PtrType ptr) : ptr(ptr) {}

		//	Ret operator()()
		//	{
		//		return ptr();
		//	}

		//	PtrType ptr;
		//};

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

		// Flags bitfield
		u32 zf : 1, sf : 1;

		std::array<Value, REGISTER_COUNT> registers;
		std::stack<Value> stack;

		u32* memory = nullptr;
		bool bTerminated = false;

		struct IntermediateFuncAddress
		{
			i32 uid;
			i32 ip;
		};

		using FuncAddress = i32;
		std::map<FuncAddress, FuncPtr*> ExternalFuncTable;

		struct ParseState
		{
			std::map<std::string, i32> varToRegisterMap;
			std::map<std::string, i32> funcNameToUIDTable;
			std::map<i32, i32> funcUIDToAddressTable;
		};

		void GenerateFromAST(AST* ast);
		void GenerateFromInstStream(const std::vector<Instruction>& inInstructions);
		void Execute();

		DiagnosticContainer* diagnosticContainer = nullptr;

	private:
		void GenerateInstructions(ParseState& parseState, const std::vector<Statement*>& statements);
		ValueWrapper GetValueWrapperFromExpression(Expression* expression, ParseState& parseState);
		i32 GenerateCallInstruction(FunctionCall* funcCall, ParseState& parseState);

		void AllocateMemory();
		void ZeroOutRegisters();
		void ClearStack();

		bool IsExternal(FuncAddress funcAddress);
		i32 TranslateLocalFuncAddress(FuncAddress localFuncAddress);
		void DispatchExternalCall(FuncAddress funcAddress);

	};

} // namespace flex
