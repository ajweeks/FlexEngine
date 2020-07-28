#include "stdafx.hpp"

#include "VirtualMachine/Backend/VirtualMachine.hpp"

#include "Helpers.hpp"
#include "VirtualMachine/Diagnostics.hpp"
#include "VirtualMachine/Frontend/Parser.hpp"

namespace flex
{
	VM::Value VM::g_EmptyValue = Value();

	std::string VM::Value::ToString() const
	{
		switch (type)
		{
		case Type::INT:		return IntToString(valInt);
		case Type::FLOAT:	return FloatToString(valFloat);
		case Type::BOOL:	return valBool ? "true" : "false";
		case Type::STRING:	return std::string(valStr);
		case Type::CHAR:	return std::string(1, valChar);
		default:
			assert(false);
			return "";
		}
	}

	VM::Value& VM::Value::operator=(const VM::Value& other)
	{
		CheckAssignmentType(other.type);

		valInt = other.valInt;

		return *this;
	}

	VM::Value& VM::Value::operator=(const VM::Value&& other)
	{
		CheckAssignmentType(other.type);

		valInt = other.valInt;

		return *this;
	}

	VM::Value& VM::Value::operator+(const VM::Value& other)
	{
		CheckAssignmentType(other.type);

		switch (type)
		{
		case Type::INT:
			valInt += other.valInt;
			break;
		case Type::FLOAT:
			valFloat += other.valFloat;
			break;
		default:
			PrintError("Attempted to add non-numeric types!\n");
			assert(false);
			break;
		}

		return *this;
	}

	VM::Value& VM::Value::operator-(const VM::Value& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			valInt -= other.valInt;
			break;
		case Type::FLOAT:
			valFloat -= other.valFloat;
			break;
		default:
			PrintError("Attempted to subtract non-numeric types!\n");
			assert(false);
			break;
		}

		return *this;
	}

	VM::Value& VM::Value::operator*(const VM::Value& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			valInt *= other.valInt;
			break;
		case Type::FLOAT:
			valFloat *= other.valFloat;
			break;
		default:
			PrintError("Attempted to multiply non-numeric types!\n");
			assert(false);
			break;
		}

		return *this;
	}

	VM::Value& VM::Value::operator/(const VM::Value& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			valInt /= other.valInt;
			break;
		case Type::FLOAT:
			valFloat /= other.valFloat;
			break;
		default:
			PrintError("Attempted to divide non-numeric types!\n");
			assert(false);
			break;
		}

		return *this;
	}

	VM::Value& VM::Value::operator%(const VM::Value& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			valInt %= other.valInt;
			break;
		case Type::FLOAT:
			valFloat = fmod(valFloat, other.valFloat);
			break;
		default:
			PrintError("Attempted to modulo non-numeric types!\n");
			assert(false);
			break;
		}

		return *this;
	}

	bool VM::Value::operator<(const VM::Value& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			return valInt < other.valInt;
		case Type::FLOAT:
			return valFloat < other.valFloat;
		default:
			PrintError("Attempted to compare non-numeric types!\n");
			assert(false);
			return false;
		}
	}

	bool VM::Value::operator<=(const VM::Value& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			return valInt <= other.valInt;
		case Type::FLOAT:
			return valFloat <= other.valFloat;
		default:
			PrintError("Attempted to compare non-numeric types!\n");
			assert(false);
			return false;
		}
	}

	bool VM::Value::operator>(const VM::Value& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			return valInt > other.valInt;
		case Type::FLOAT:
			return valFloat > other.valFloat;
		default:
			PrintError("Attempted to compare non-numeric types!\n");
			assert(false);
			return false;
		}
	}

	bool VM::Value::operator>=(const VM::Value& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			return valInt >= other.valInt;
		case Type::FLOAT:
			return valFloat >= other.valFloat;
		default:
			PrintError("Attempted to compare non-numeric types!\n");
			assert(false);
			return false;
		}
	}

	bool VM::Value::operator==(const VM::Value& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			return valInt == other.valInt;
		case Type::FLOAT:
			return valFloat == other.valFloat;
		case Type::BOOL:
			return valBool == other.valBool;
		case Type::STRING:
			return strcmp(valStr, other.valStr) == 0;
		case Type::CHAR:
			return valChar == other.valChar;
		default:
			assert(false);
			return false;
		}
	}

	bool VM::Value::operator!=(const VM::Value& other)
	{
		CheckAssignmentType(other.type);
		switch (type)
		{
		case Type::INT:
			return valInt != other.valInt;
		case Type::FLOAT:
			return valFloat != other.valFloat;
		case Type::BOOL:
			return valBool != other.valBool;
		case Type::STRING:
			return strcmp(valStr, other.valStr) != 0;
		case Type::CHAR:
			return valChar != other.valChar;
		default:
			assert(false);
			return false;
		}
	}

	void VM::Value::CheckAssignmentType(Type otherType)
	{
		if (type == Type::_NONE)
		{
			type = otherType;
		}
		else
		{
			assert(type == otherType);
		}
	}

	VM::Value& VM::ValueWrapper::Get(VM* vm)
	{
		if (type == Type::REGISTER)
		{
			return vm->registers[value.valInt];
		}
		else if (type == Type::CONSTANT)
		{
			return value;
		}
		else
		{
			assert(false);
			return g_EmptyValue;
		}
	}

	VM::Value& VM::ValueWrapper::GetW(VM* vm)
	{
		assert(type == Type::REGISTER);
		return Get(vm);
	}

	bool VM::ValueWrapper::Valid() const
	{
		return type != Type::NONE;
	}

	VM::VM() :
		diagnosticContainer(new DiagnosticContainer())
	{
	}

	VM::~VM()
	{
		delete diagnosticContainer;
	}

	void VM::GenerateFromAST(AST* ast)
	{
		std::string s = ast->rootBlock->ToString();
		Print("%s\n", s.c_str());
		std::vector<Statement*> emptyList;
		ast->rootBlock->RewriteCompoundStatements(ast->parser, emptyList);
		s = ast->rootBlock->ToString();
		Print("\n---\n\n%s\n", s.c_str());

		for (u32 i = 0; i < (u32)ast->rootBlock->statements.size(); ++i)
		{
			Statement* statement = ast->rootBlock->statements[i];

			//ValueWrapper val0(ValueWrapper::Type::CONSTANT, Value(0)), val1, val2;
			//Instruction inst(OpCode::ADD, val0, val1, val2);
			//instructions.push_back(inst);
		}
	}

	void VM::GenerateFromInstStream(const std::vector<Instruction>& inInstructions)
	{
		instructions = inInstructions;
		AllocateMemory();
		ZeroOutRegisters();
		ClearStack();
		instructionIdx = 0;
		bTerminated = false;
		ExternalFuncTable.clear();
	}

	void VM::Execute()
	{
		if (instructions.empty())
		{
			PrintError("Attempted to execute VM without any instructions present\n");
			return;
		}

		u32 loopCount = 0;
		bool bBreak = false;
		while (!bTerminated && !bBreak)
		{
			Instruction& inst = instructions[instructionIdx];

			i32 pInstIdx = instructionIdx;
			switch (inst.opCode)
			{
			case OpCode::MOV:
				inst.val0.GetW(this) = inst.val1.Get(this);
				break;
			case OpCode::ADD:
				inst.val0.GetW(this) = inst.val1.Get(this) + inst.val2.Get(this);
				break;
			case OpCode::SUB:
				inst.val0.GetW(this) = inst.val1.Get(this) - inst.val2.Get(this);
				break;
			case OpCode::MUL:
				inst.val0.GetW(this) = inst.val1.Get(this) * inst.val2.Get(this);
				break;
			case OpCode::DIV:
				inst.val0.GetW(this) = inst.val1.Get(this) / inst.val2.Get(this);
				break;
			case OpCode::MOD:
				inst.val0.GetW(this) = inst.val1.Get(this) % inst.val2.Get(this);
				break;
			case OpCode::PUSH:
				stack.push(inst.val0.Get(this));
				break;
			case OpCode::POP:
				if (inst.val0.Valid())
				{
					inst.val0.GetW(this) = stack.top();
				}
				stack.pop();
				break;
			case OpCode::JMP:
				instructionIdx = inst.val0.Get(this).valInt;
				break;
			case OpCode::JLT:
				if (inst.val0.Get(this) < inst.val1.Get(this))
				{
					instructionIdx = inst.val2.Get(this).valInt;
				}
				break;
			case OpCode::JLE:
				if (inst.val0.Get(this) <= inst.val1.Get(this))
				{
					instructionIdx = inst.val2.Get(this).valInt;
				}
				break;
			case OpCode::JGT:
				if (inst.val0.Get(this) > inst.val1.Get(this))
				{
					instructionIdx = inst.val2.Get(this).valInt;
				}
				break;
			case OpCode::JGE:
				if (inst.val0.Get(this) >= inst.val1.Get(this))
				{
					instructionIdx = inst.val2.Get(this).valInt;
				}
				break;
			case OpCode::JE:
				if (inst.val0.Get(this) == inst.val1.Get(this))
				{
					instructionIdx = inst.val2.Get(this).valInt;
				}
				break;
			case OpCode::JNE:
				if (inst.val0.Get(this) != inst.val1.Get(this))
				{
					instructionIdx = inst.val2.Get(this).valInt;
				}
				break;
			case OpCode::CALL:
			{
				FuncAddress funcAddress = inst.val0.Get(this).valInt;
				if (IsExternal(funcAddress))
				{
					DispatchExternalCall(funcAddress);
				}
				else
				{
					instructionIdx = TranslateLocalFuncAddress(funcAddress);
				}
			} break;
			//case OpCode::BREAK:
			//	// TODO:
			//	bBreak = true;
			//	break;
			case OpCode::YIELD:
				// TODO:
				bBreak = true;
				break;
			case OpCode::RETURN:
			{
				const bool bVoid = !inst.val0.Valid();

				i32 returnVal = 0;
				if (!bVoid)
				{
					returnVal = inst.val0.Get(this).valInt;
				}
				instructionIdx = stack.top().valInt;
				stack.pop();
				if (!bVoid)
				{
					stack.push(returnVal);
				}
			} break;
			case OpCode::TERMINATE:
				bTerminated = true;
				break;
			}

			if (!bTerminated && pInstIdx == instructionIdx)
			{
				++instructionIdx;
			}

			if (++loopCount > 10'000'000)
			{
				PrintError("Execution loop took too long, broke out early\n");
				bBreak = true;
			}
		}
	}

	void VM::AllocateMemory()
	{
		memory = (u32*)malloc(MEMORY_POOL_SIZE);
		if (memory == nullptr)
		{
			PrintError("Failed to allocate %u bytes for VM memory!\n", MEMORY_POOL_SIZE);
		}
	}

	void VM::ZeroOutRegisters()
	{
		for (u32 i = 0; i < REGISTER_COUNT; ++i)
		{
			registers[i] = g_EmptyValue;
		}
	}

	void VM::ClearStack()
	{
		while (!stack.empty())
		{
			stack.pop();
		}
	}

	bool VM::IsExternal(FuncAddress funcAddress)
	{
		return funcAddress > 0xFFFF;
	}

	i32 VM::TranslateLocalFuncAddress(FuncAddress localFuncAddress)
	{
		// TODO:
		return localFuncAddress;
	}

	void VM::DispatchExternalCall(FuncAddress funcAddress)
	{
		FuncPtr* funcPtr = ExternalFuncTable[funcAddress];
		i32 returnVal = (*funcPtr)().valInt;
		stack.push(returnVal);
	}

} // namespace flex
