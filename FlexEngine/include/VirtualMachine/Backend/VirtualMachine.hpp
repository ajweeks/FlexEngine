#pragma once

#include "Scene/Objects/Terminal.hpp" // For Terminal::MAX_OUTPUT_COUNT
#include "Variant.hpp"
#include "VirtualMachine/Backend/FunctionBindings.hpp"
#include "VirtualMachine/Backend/IR.hpp"
#include "VirtualMachine/StringMap.hpp"

namespace flex
{
	struct DiagnosticContainer;
	class FunctionBindings;

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
		struct VariantWrapper
		{
			enum class Type
			{
				REGISTER,
				CONSTANT,
				TERMINAL_OUTPUT,
				NONE
			};

			VariantWrapper() :
				type(Type::NONE),
				variant(g_EmptyVariant)
			{}

			VariantWrapper(Type type, const Variant& variant) :
				type(type),
				variant(variant)
			{}

			Type type;
			Variant variant;

			Variant& Get(VirtualMachine* vm);
			Variant& GetW(VirtualMachine* vm);
			bool Valid() const;
			std::string ToString() const;
		};

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
			ITF, // int-to-float cast
			FTI, // float-to-int cast
			CALL,
			PUSH,
			POP,
			CMP,
			JMP,
			JZ,
			JNZ,
			JEQ,
			JNE,
			JLT,
			JLE,
			JGT,
			JGE,
			YIELD,
			RETURN,
			HALT,
			// Push/pop the registers a call can clobber (val0: callee's address, val1: number of registers allocated at
			// the call). Linking turns them into the list of registers the callee (or anything it calls) writes.
			SAVE,
			RESTORE,

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
			"inv",
			"and",
			"or ",
			"xor",
			"itf",
			"fti",
			"cal",
			"psh",
			"pop",
			"cmp",
			"jmp",
			"jz",
			"jnz",
			"jeq",
			"jne",
			"jlt",
			"jle",
			"jgt",
			"jge",
			"yld",
			"rtn",
			"hlt",
			"sav",
			"rst",

			"NONE"
		};

		static_assert(ARRAY_LENGTH(s_OpCodeStrings) == ((size_t)OpCode::_NONE + 1), "Length of s_OpCodeStrings must match length of OpCode enum");

		const char* OpCodeToString(OpCode opCode);

		OpCode BinaryOperatorTypeToOpCode(IR::BinaryOperatorType opType);
		OpCode BinaryOperatorTypeToInverseOpCode(IR::BinaryOperatorType opType);
		OpCode InverseOpCode(OpCode opCode);

		// TODO: Delete:
		//OpCode IROperatorTypeToOpCode(IR::OperatorType irOperatorType);
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

		struct Instruction
		{
			Instruction(OpCode opCode) :
				opCode(opCode),
				val0(VariantWrapper::Type::CONSTANT, g_EmptyVariant),
				val1(VariantWrapper::Type::CONSTANT, g_EmptyVariant),
				val2(VariantWrapper::Type::CONSTANT, g_EmptyVariant)
			{}

			Instruction(OpCode opCode, const VariantWrapper& val0) :
				opCode(opCode),
				val0(val0),
				val1(VariantWrapper::Type::CONSTANT, g_EmptyVariant),
				val2(VariantWrapper::Type::CONSTANT, g_EmptyVariant)
			{}

			Instruction(OpCode opCode, const VariantWrapper& val0, const VariantWrapper& val1) :
				opCode(opCode),
				val0(val0),
				val1(val1),
				val2(VariantWrapper::Type::CONSTANT, g_EmptyVariant)
			{}

			Instruction(OpCode opCode, const VariantWrapper& val0, const VariantWrapper& val1, const VariantWrapper& val2) :
				opCode(opCode),
				val0(val0),
				val1(val1),
				val2(val2)
			{}

			OpCode opCode;
			VariantWrapper val0;
			VariantWrapper val1;
			VariantWrapper val2;
		};

		// Linked form of an Instruction which the interpreter runs: operands are resolved to pointers (registers,
		// terminal outputs & constants), jump & call targets to instruction indices
		struct ExecInstruction
		{
			u8 op = 0; // ExecOp (see VirtualMachine.cpp)
			u16 count = 0; // SAVE/RESTORE: register count, external CALL: argument count
			i32 target = -1; // Jumps & local calls: instruction index (-1: read it from a), SAVE/RESTORE: list offset
			Variant* dst = nullptr; // Written operand
			const Variant* a = nullptr;
			union
			{
				const Variant* b;
				IFunction* func; // External CALL
			};
		};

		// The VM's value stack: keeps its capacity between runs so steady state execution never allocates
		class ValueStack
		{
		public:
			ValueStack() = default;

			bool empty() const { return m_Data.empty(); }
			u32 size() const { return (u32)m_Data.size(); }
			const Variant& top() const { return m_Data.back(); }
			Variant& top() { return m_Data.back(); }
			void push(const Variant& value) { m_Data.push_back(value); }
			void pop() { m_Data.pop_back(); }
			void clear() { m_Data.clear(); }
			void reserve(u32 capacity) { m_Data.reserve(capacity); }
			// Index 0 is the bottom of the stack
			const Variant& operator[](u32 index) const { return m_Data[index]; }

		private:
			friend class VirtualMachine;
			std::vector<Variant> m_Data;
		};

		struct State;

		struct InstructionBlock
		{
			void PushBack(const Instruction& inst, Span origin, State* state);

			// The block's instructions are state->instructions[startOffset, startOffset + count)
			i32 startOffset = -1;
			u32 count = 0;
		};

		struct State
		{
			State();
			void Clear();
			void Destroy();
			InstructionBlock& CurrentInstructionBlock();
			InstructionBlock& PushInstructionBlock();
			// Jump targets: labels [0, IR block count) are the IR blocks' starts, NewLabel adds more
			i32 NewLabel();
			// Sets label to the next instruction in the current instruction block
			void PlaceLabel(i32 label);

			StringMap<i32> funcNameToBlockIndexTable;
			StringMap<i32> varRegisterMap;
			std::vector<InstructionBlock> instructionBlocks;
			// Every block's instructions before jumps & calls are patched (contiguous, in block order)
			std::vector<Instruction> instructions;
			std::vector<Span> instructionOrigins;
			std::vector<i32> labelOffsets; // Instruction index of each label

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
			// Caller pushes registers onto stack (all variable registers allocated so far)
			// Caller pushes return address onto stack
			// Caller pushes args in reverse order onto stack
			// Called runs
			//   Called pops args first to last
			//   Called returns (with optional value)
			//   Resume address is popped off stack
			//   Return value is pushed onto stack
			// Caller resumes
			// Caller pops return value off stack into SCRATCH_REGISTER (if non-void func)
			// Caller pops registers off stack

			void GenerateFromSource(const char* source);
			void GenerateFromIR(IR::IntermediateRepresentation* ir, FunctionBindings* functionBindings);
			void GenerateFromInstStream(const std::vector<Instruction>& inInstructions);

			void Execute(bool bSingleStep = false);

			// Number of instructions run by the last call to Execute
			u64 LastExecutedInstructionCount() const;

			DiagnosticContainer* GetDiagnosticContainer();

			bool IsExecuting() const;
			i32 InstructionIndex() const;
			i32 CurrentLineNumber() const;
			void ClearRuntimeState();

			bool ZeroFlagSet() const;
			bool SignFlagSet() const;

			bool IsCompiled() const;

			static const i32 REGISTER_COUNT = 128;
			// Never holds a variable: return addresses & return values pass through it
			static const i32 SCRATCH_REGISTER = REGISTER_COUNT - 1;
			static const i32 MAX_STACK_HEIGHT = 16384;
			// (Execution stops after this many instructions in one call to Execute)
			static const u32 MAX_INSTRUCTIONS_PER_EXECUTE = 10'000'000;
			static const i32 MAX_EXTERNAL_ARG_COUNT = 16;

			static VariantWrapper g_ZeroIntVariantWrapper;
			static VariantWrapper g_ZeroFloatVariantWrapper;
			static VariantWrapper g_OneIntVariantWrapper;
			static VariantWrapper g_OneFloatVariantWrapper;

			struct RunningState
			{
				void Clear()
				{
					instructionIdx = -1;
					terminated = false;
					zf = 0;
					sf = 0;
				}

				i32 instructionIdx = -1;
				bool terminated = false;
				// Flags bitfield
				u32 zf : 1, sf : 1;
			};

			std::vector<Instruction> instructions;
			std::vector<Span> instructionOrigins;

			std::array<Variant, REGISTER_COUNT> registers;
			std::array<Variant, Terminal::MAX_OUTPUT_COUNT> terminalOutputs;
			ValueStack stack;

			State* state = nullptr;
			DiagnosticContainer* diagnosticContainer = nullptr;

			// Listings of the last compile's AST, IR & instructions (before & after patching), built on first use
			const std::string& GetASTString();
			const std::string& GetIRString();
			const std::string& GetUnpatchedInstructionString();
			const std::string& GetInstructionString();

			static bool IsTerminalOutputVar(const std::string& varName);
			// Returns -1, or terminal output var index if valid name
			static i32 GetTerminalOutputVar(const std::string& varName);

			RunningState m_RunningState;

		private:
			IR::Value::Type FindIRType(IR::State* irState, IR::Value* irValue);

			// Resolves instructions into m_Code, returns false on errors (reported to state's diagnostics). savedRegisters
			// optionally holds the registers each SAVE/RESTORE instruction saves (by default all below its count).
			bool Link(const std::vector<std::vector<u8>>* savedRegisters);
			// For each SAVE/RESTORE: the registers the callee (& everything it calls) writes, below the call's count
			void ComputeSavedRegisters(IR::IntermediateRepresentation* ir, std::vector<std::vector<u8>>& outSavedRegisters);
			bool ResolveOperand(VariantWrapper& wrapper, bool bWritten, i32 instructionIndex, Variant*& outPtr);
			void ZeroOutRegisters();
			void ZeroOutTerminalOutputs();
			void ClearStack();
			// Emits a compare & jumps to the true/false blocks (leaving out a jump to nextBlockIndex, which follows)
			void HandleComparison(IR::IntermediateRepresentation* ir, IR::Value* condition, i32 ifTrueBlockIndex, i32 ifFalseBlockIndex, i32 nextBlockIndex);

			bool IsExternal(FuncAddress funcAddress);
			FuncAddress GetExternalFuncAddress(const std::string& functionName);
			i32 TranslateLocalFuncAddress(FuncAddress localFuncAddress);

			VariantWrapper GetValueWrapperFromIRValue(IR::State* irState, IR::Value* value);
			i32 CombineInstructionIndex(i32 instructionBlockIndex, i32 instructionIndex);
			void SplitInstructionIndex(i32 combined, i32& outInstructionBlockIndex, i32& outInstructionIndex);
			i32 GenerateCallInstruction(IR::State* irState, IR::FunctionCallValue* funcCallValue);

			Arena m_ASTArena;
			AST::AST* m_AST = nullptr;
			IR::IntermediateRepresentation* m_IR = nullptr;
			FunctionBindings* m_FunctionBindings = nullptr;

			bool m_bCompiled = false;

			void BuildDebugStrings();

			bool m_bDebugStringsBuilt = false;
			bool m_bASTValid = false;
			bool m_bIRValid = false;
			std::string m_ASTStr;
			std::string m_IRStr;
			std::string m_UnpatchedInstructionStr;
			std::string m_InstructionStr;

			std::vector<ExecInstruction> m_Code;
			std::vector<u8> m_SavedRegisterLists; // Register indices referenced by SAVE/RESTORE (offset, count)
			u64 m_LastExecutedInstructionCount = 0;

		};
	}// namespace VM
} // namespace flex
