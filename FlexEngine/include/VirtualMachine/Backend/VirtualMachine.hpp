#pragma once

#include <stack>

#include "VirtualMachine/Backend/VMValue.hpp"
#include "VirtualMachine/Backend/IR.hpp"

namespace flex
{
	struct DiagnosticContainer;

	namespace IR
	{
		struct Assignment;
		struct Value;
		struct IntermediateRepresentation;
		enum class OperatorType;
	}

	namespace AST
	{
		struct AST;
	}

	namespace VM
	{
		enum class OpCode
		{
			MOV,
			ADD,
			SUB,
			MUL,
			DIV,
			MOD,
			INV,
			AND,
			OR,
			XOR,
			CALL,
			PUSH,
			POP,
			CMP,
			JMP,
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
			"inv",
			"mod",
			"and",
			"or",
			"xor",
			"call",
			"push",
			"pop",
			"cmp",
			"jmp",
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

		OpCode IROperatorTypeToOpCode(IR::OperatorType irOperatorType);
		// TODO: Delete:
		//OpCode BinaryOperatorTypeToOpCode(AST::BinaryOperatorType operatorType);
		//OpCode BinaryOpToJumpCode(AST::BinaryOperatorType operatorType);

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
				return ptr(val0, val1, val2, val3, val4, g_EmptyVMValue);
			}

			Value operator()(const Value& val0, const Value& val1, const Value& val2, const Value& val3)
			{
				return ptr(val0, val1, val2, val3, g_EmptyVMValue, g_EmptyVMValue);
			}

			Value operator()(const Value& val0, const Value& val1, const Value& val2)
			{
				return ptr(val0, val1, val2, g_EmptyVMValue, g_EmptyVMValue, g_EmptyVMValue);
			}

			Value operator()(const Value& val0, const Value& val1)
			{
				return ptr(val0, val1, g_EmptyVMValue, g_EmptyVMValue, g_EmptyVMValue, g_EmptyVMValue);
			}

			Value operator()(const Value& val0)
			{
				return ptr(val0, g_EmptyVMValue, g_EmptyVMValue, g_EmptyVMValue, g_EmptyVMValue, g_EmptyVMValue);
			}

			Value operator()()
			{
				return ptr(g_EmptyVMValue, g_EmptyVMValue, g_EmptyVMValue, g_EmptyVMValue, g_EmptyVMValue, g_EmptyVMValue);
			}

			PtrType ptr;
		};

		struct Instruction
		{
			Instruction(OpCode opCode) :
				opCode(opCode),
				val0(ValueWrapper::Type::CONSTANT, g_EmptyVMValue),
				val1(ValueWrapper::Type::CONSTANT, g_EmptyVMValue),
				val2(ValueWrapper::Type::CONSTANT, g_EmptyVMValue)
			{}

			Instruction(OpCode opCode, const ValueWrapper& val0) :
				opCode(opCode),
				val0(val0),
				val1(ValueWrapper::Type::CONSTANT, g_EmptyVMValue),
				val2(ValueWrapper::Type::CONSTANT, g_EmptyVMValue)
			{}

			Instruction(OpCode opCode, const ValueWrapper& val0, const ValueWrapper& val1) :
				opCode(opCode),
				val0(val0),
				val1(val1),
				val2(ValueWrapper::Type::CONSTANT, g_EmptyVMValue)
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

		struct IntermediateFuncAddress
		{
			i32 uid;
			i32 ip;
		};

		struct InstructionBlock
		{
			void PushBack(const Instruction& inst);

			std::vector<Instruction> instructions;
			std::string name;

			i32 startOffset = -1;
		};

		struct State
		{
			void Clear();
			InstructionBlock& CurrentInstructionBlock();
			InstructionBlock& PushInstructionBlock();
			void PopInstructionBlock();

			std::map<std::string, IR::Assignment> varUsages;
			std::map<std::string, i32> funcNameToBlockIndexTable;
			std::map<std::string, i32> varRegisterMap;
			std::vector<InstructionBlock> instructionBlocks;

			DiagnosticContainer* diagnosticContainer = nullptr;
		};

		class VirtualMachine
		{
		public:
			VirtualMachine();
			~VirtualMachine();

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

			void GenerateFromSource(const char* source);
			void GenerateFromIR(IR::IntermediateRepresentation* ir);
			void GenerateFromInstStream(const std::vector<Instruction>& inInstructions);
			void Execute();

			DiagnosticContainer* GetDiagnosticContainer();

			static const i32 REGISTER_COUNT = 64;
			static const u32 MEMORY_POOL_SIZE = 32768;

			i32 instructionIdx = 0;
			std::vector<Instruction> instructions;

			// Flags bitfield
			u32 zf : 1, sf : 1;

			std::array<VM::Value, REGISTER_COUNT> registers;
			std::stack<VM::Value> stack;

			u32* memory = nullptr;
			bool bTerminated = false;

			using FuncAddress = i32;
			std::map<FuncAddress, FuncPtr*> ExternalFuncTable;

			State state;
			DiagnosticContainer* diagnosticContainer = nullptr;

			std::string astStr;
			std::string irStr;
			std::string instructionStr;

		private:
			void AllocateMemory();
			void ZeroOutRegisters();
			void ClearStack();

			bool IsExternal(FuncAddress funcAddress);
			i32 TranslateLocalFuncAddress(FuncAddress localFuncAddress);
			void DispatchExternalCall(FuncAddress funcAddress);

			ValueWrapper GetValueWrapperFromIRValue(IR::Value* value);

			AST::AST* m_AST = nullptr;
			IR::IntermediateRepresentation* m_IR = nullptr;

		};
	}// namespace VM
} // namespace flex
