#include "stdafx.hpp"

#include "VirtualMachine/Backend/VirtualMachine.hpp"

#include "Helpers.hpp"
#include "VirtualMachine/Backend/VariableContainer.hpp"
#include "VirtualMachine/Diagnostics.hpp"
#include "VirtualMachine/Frontend/Parser.hpp"

namespace flex
{
	namespace VM
	{
		void ParseState::Clear()
		{
			//varToRegisterMap.clear();
			funcNameToBlockIndexTable.clear();
			instructionBlocks.clear();

			diagnosticContainer->diagnostics.clear();
		}

		void InstructionBlock::PushBack(const Instruction& inst)
		{
			instructions.push_back(inst);
		}

		InstructionBlock& ParseState::CurrentInstructionBlock()
		{
			return instructionBlocks[instructionBlocks.size() - 1];
		}

		InstructionBlock& ParseState::PushInstructionBlock()
		{
			instructionBlocks.push_back({});
			return instructionBlocks[instructionBlocks.size() - 1];
		}

		void ParseState::PopInstructionBlock()
		{
			instructionBlocks.resize(instructionBlocks.size() - 1);
		}

		VirtualMachine::VirtualMachine()
		{
			parseState.diagnosticContainer = new DiagnosticContainer();
			runtimeDiagnosticContainer = new DiagnosticContainer();
		}

		VirtualMachine::~VirtualMachine()
		{
			delete parseState.diagnosticContainer;
			delete runtimeDiagnosticContainer;
		}

		void VirtualMachine::GenerateFromSource(const char* source)
		{
			Print(source);
			Print("\n---\n");

			AST::AST* ast = new AST::AST();
			ast->Generate(source);

			Generator* generator = new Generator();
			generator->GenerateFromAST(ast);

			GenerateFromIR(generator);

			generator->Destroy();
			delete generator;
			ast->Destroy();
			delete ast;
		}

		void VirtualMachine::GenerateFromIR(Generator* generator)
		{
			FLEX_UNUSED(generator);

			/*

				DiscoverFuncDeclarations(ast->rootBlock->statements, parseState);

				LowerStatement(parseState, ast->rootBlock);

				parseState.CurrentInstructionBlock().PushBack({ OpCode::TERMINATE });

				GenerateFunctionInstructions(ast->rootBlock->statements, parseState);

			*/


			//for (u32 blockIndex = 0; blockIndex < (u32)generator->blockLists.size(); ++blockIndex)
			{
				//IR::BlockList& blockList = generator->blockLists[blockIndex];
				//for (u32 blockOffset = 0; blockOffset < (u32)blockList.blocks.size(); ++blockOffset)
				{
					//IR::Block& block = blockList.blocks[blockOffset];
					//OpCode opCode = IROperatorTypeToOpCode(block.operatorType);
					//instructions.emplace_back(opCode, block.val0, block.val1, block.val2);
				}
			}
			/*
			// Turn instruction blocks into contiguous instruction list
			{
				u32 instructionIndex = 0;
				for (u32 i = 0; i < (u32)parseState.instructionBlocks.size(); ++i)
				{
					std::vector<Instruction>& blockInstructions = parseState.instructionBlocks[i].instructions;
					parseState.instructionBlocks[i].startOffset = instructionIndex;
					for (u32 j = 0; j < (u32)blockInstructions.size(); ++j)
					{
						instructions[instructionIndex++] = blockInstructions[j];
					}
				}
			}
			*/

			// TODO: Properly

			// Patch up function calls to point at proper locations
			for (u32 i = 0; i < (u32)instructions.size(); ++i)
			{
				Instruction& inst = instructions[i];

				switch (inst.opCode)
				{
				case OpCode::CALL:
				{
					i32 funcAddress = parseState.instructionBlocks[inst.val0.Get(this).valInt].startOffset;
					inst.val0.value.valInt = funcAddress;
				} break;
				}
			}

			for (u32 i = 0; i < (u32)instructions.size(); ++i)
			{
				const Instruction& inst = instructions[i];
				std::string val0Str = inst.val0.ToString();
				std::string val1Str = inst.val1.ToString();
				std::string val2Str = inst.val2.ToString();
				Print("%u  %s %s %s %s\n", i, OpCodeToString(inst.opCode), val0Str.c_str(), val1Str.c_str(), val2Str.c_str());
			}
			Print("\n");
		}

		void VirtualMachine::GenerateFromInstStream(const std::vector<Instruction>& inInstructions)
		{
			instructions = inInstructions;
			AllocateMemory();
			ZeroOutRegisters();
			ClearStack();
			instructionIdx = 0;
			bTerminated = false;
			ExternalFuncTable.clear();
		}

		void VirtualMachine::Execute()
		{
			runtimeDiagnosticContainer->diagnostics.clear();

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
				case OpCode::CMP:
				{
					Value diff = inst.val0.Get(this) - inst.val1.Get(this);
					zf = diff.IsZero();
					sf = diff.IsPositive();
				} break;
				case OpCode::JMP:
					instructionIdx = inst.val0.Get(this).valInt;
					break;
				case OpCode::JLT:
					if (!sf && !zf)
					{
						instructionIdx = inst.val0.Get(this).valInt;
					}
					break;
				case OpCode::JLE:
					if (!sf || zf)
					{
						instructionIdx = inst.val0.Get(this).valInt;
					}
					break;
				case OpCode::JGT:
					if (sf && !zf)
					{
						instructionIdx = inst.val0.Get(this).valInt;
					}
					break;
				case OpCode::JGE:
					if (sf || zf)
					{
						instructionIdx = inst.val0.Get(this).valInt;
					}
					break;
				case OpCode::JEQ:
					if (zf)
					{
						instructionIdx = inst.val0.Get(this).valInt;
					}
					break;
				case OpCode::JNE:
					if (!zf)
					{
						instructionIdx = inst.val0.Get(this).valInt;
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
				case OpCode::YIELD:
					// TODO: Store registers/stack?
					bBreak = true;
					break;
				case OpCode::RETURN:
				{
					const bool bVoid = !inst.val0.Valid();

					Value returnVal;
					if (!bVoid)
					{
						returnVal = inst.val0.Get(this);
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
				default:
					std::string errorMsg = "Unhandled op code in VirtualMachine::Execute: %u\n" + std::to_string((u32)inst.opCode);
					runtimeDiagnosticContainer->AddDiagnostic(Span(0, 0), 0, 0, errorMsg);
					break;
				}

				if (!bTerminated && pInstIdx == instructionIdx)
				{
					++instructionIdx;
				}

				if (++loopCount > 10'000'000)
				{
					runtimeDiagnosticContainer->AddDiagnostic(Span(0, 0), 0, 0, "Execution loop took too long, broke out early\n");
					bBreak = true;
				}
			}
		}

		void VirtualMachine::AllocateMemory()
		{
			memory = (u32*)malloc(MEMORY_POOL_SIZE);
			if (memory == nullptr)
			{
				PrintError("Failed to allocate %u bytes for VM memory!\n", MEMORY_POOL_SIZE);
			}
		}

		void VirtualMachine::ZeroOutRegisters()
		{
			for (u32 i = 0; i < REGISTER_COUNT; ++i)
			{
				registers[i] = g_EmptyVMValue;
			}
		}

		void VirtualMachine::ClearStack()
		{
			while (!stack.empty())
			{
				stack.pop();
			}
		}

		bool VirtualMachine::IsExternal(FuncAddress funcAddress)
		{
			return funcAddress > 0xFFFF;
		}

		i32 VirtualMachine::TranslateLocalFuncAddress(FuncAddress localFuncAddress)
		{
			// TODO:
			return localFuncAddress;
		}

		void VirtualMachine::DispatchExternalCall(FuncAddress funcAddress)
		{
			FuncPtr* funcPtr = ExternalFuncTable[funcAddress];
			i32 returnVal = (*funcPtr)().valInt;
			stack.push(VM::Value(returnVal));
		}

		const char* OpCodeToString(OpCode opCode)
		{
			return s_OpCodeStrings[(u32)opCode];
		}

		OpCode IROperatorTypeToOpCode(IR::OperatorType irOperatorType)
		{
			switch (irOperatorType)
			{
			case IR::OperatorType::ASSIGN:				return OpCode::MOV;
			case IR::OperatorType::ADD:					return OpCode::ADD;
			case IR::OperatorType::SUB:					return OpCode::SUB;
			case IR::OperatorType::MUL:					return OpCode::MUL;
			case IR::OperatorType::DIV:					return OpCode::DIV;
			case IR::OperatorType::MOD:					return OpCode::MOD;
			case IR::OperatorType::BIN_AND:				return OpCode::AND;
			case IR::OperatorType::BIN_OR:				return OpCode::OR;
			case IR::OperatorType::BIN_XOR:				return OpCode::XOR;
			case IR::OperatorType::EQUAL_TEST:			return OpCode::CMP;
			case IR::OperatorType::NOT_EQUAL_TEST:		return OpCode::CMP;
			case IR::OperatorType::GREATER_TEST:		return OpCode::CMP;
			case IR::OperatorType::GREATER_EQUAL_TEST:	return OpCode::CMP;
			case IR::OperatorType::LESS_TEST:			return OpCode::CMP;
			case IR::OperatorType::LESS_EQUAL_TEST:		return OpCode::CMP;
			case IR::OperatorType::BOOLEAN_AND:			return OpCode::CMP;
			case IR::OperatorType::BOOLEAN_OR:			return OpCode::CMP;
			case IR::OperatorType::NEGATE:				return OpCode::SUB;
			case IR::OperatorType::NOT:					return OpCode::SUB;
			case IR::OperatorType::BIN_INVERT:			return OpCode::INV;
			default:									return OpCode::_NONE;
			}
		}

		OpCode BinaryOperatorTypeToOpCode(AST::BinaryOperatorType operatorType)
		{
			switch (operatorType)
			{
			case AST::BinaryOperatorType::ADD:					return OpCode::ADD;
			case AST::BinaryOperatorType::SUB:					return OpCode::SUB;
			case AST::BinaryOperatorType::MUL:					return OpCode::MUL;
			case AST::BinaryOperatorType::DIV:					return OpCode::DIV;
			case AST::BinaryOperatorType::MOD:					return OpCode::MOD;
			case AST::BinaryOperatorType::BIN_AND:				return OpCode::AND;
			case AST::BinaryOperatorType::BIN_OR:				return OpCode::OR;
			case AST::BinaryOperatorType::BIN_XOR:				return OpCode::XOR;
			case AST::BinaryOperatorType::EQUAL_TEST:			return OpCode::CMP;
			case AST::BinaryOperatorType::NOT_EQUAL_TEST:		return OpCode::CMP;
			case AST::BinaryOperatorType::GREATER_TEST:			return OpCode::CMP;
			case AST::BinaryOperatorType::GREATER_EQUAL_TEST:	return OpCode::CMP;
			case AST::BinaryOperatorType::LESS_TEST:			return OpCode::CMP;
			case AST::BinaryOperatorType::LESS_EQUAL_TEST:		return OpCode::CMP;
			case AST::BinaryOperatorType::BOOLEAN_AND:			return OpCode::CMP;
			case AST::BinaryOperatorType::BOOLEAN_OR:			return OpCode::CMP;
			default:											return OpCode::_NONE;
			}
		}

		OpCode BinaryOpToJumpCode(AST::BinaryOperatorType operatorType)
		{
			switch (operatorType)
			{
			case AST::BinaryOperatorType::EQUAL_TEST:			return OpCode::JEQ;
			case AST::BinaryOperatorType::NOT_EQUAL_TEST:		return OpCode::JNE;
			case AST::BinaryOperatorType::GREATER_TEST:			return OpCode::JGE;
			case AST::BinaryOperatorType::GREATER_EQUAL_TEST:	return OpCode::JGT;
			case AST::BinaryOperatorType::LESS_TEST:			return OpCode::JLT;
			case AST::BinaryOperatorType::LESS_EQUAL_TEST:		return OpCode::JLE;
			default:											return OpCode::_NONE;
			}
		}
	} // namespace VM
} // namespace flex
