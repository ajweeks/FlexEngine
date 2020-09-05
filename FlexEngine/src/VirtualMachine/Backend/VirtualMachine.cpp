#include "stdafx.hpp"

#include "VirtualMachine/Backend/VirtualMachine.hpp"

#include "Helpers.hpp"
#include "StringBuilder.hpp"
#include "VirtualMachine/Backend/VariableContainer.hpp"
#include "VirtualMachine/Backend/IR.hpp"
#include "VirtualMachine/Diagnostics.hpp"
#include "VirtualMachine/Frontend/Parser.hpp"

namespace flex
{
	namespace VM
	{
		void State::Clear()
		{
			varUsages.clear();
			varRegisterMap.clear();
			funcNameToBlockIndexTable.clear();
			instructionBlocks.clear();

			diagnosticContainer->diagnostics.clear();
		}

		void InstructionBlock::PushBack(const Instruction& inst)
		{
			instructions.push_back(inst);
		}

		InstructionBlock& State::CurrentInstructionBlock()
		{
			return instructionBlocks[instructionBlocks.size() - 1];
		}

		InstructionBlock& State::PushInstructionBlock()
		{
			instructionBlocks.push_back({});
			return instructionBlocks[instructionBlocks.size() - 1];
		}

		void State::PopInstructionBlock()
		{
			instructionBlocks.resize(instructionBlocks.size() - 1);
		}

		VirtualMachine::VirtualMachine()
		{
			state.diagnosticContainer = new DiagnosticContainer();
			diagnosticContainer = new DiagnosticContainer();
		}

		VirtualMachine::~VirtualMachine()
		{
			delete state.diagnosticContainer;
			delete diagnosticContainer;

			if (m_AST != nullptr)
			{
				m_AST->Destroy();
				delete m_AST;
			}

			if (m_IR != nullptr)
			{
				m_IR->Destroy();
				delete m_IR;
			}

			free(memory);
		}

		void VirtualMachine::GenerateFromSource(const char* source)
		{
			m_bCompiled = false;
			astStr = "";
			irStr = "";
			instructionStr = "";
			diagnosticContainer->diagnostics.clear();

			if (m_AST != nullptr)
			{
				m_AST->Destroy();
				delete m_AST;
				m_AST = nullptr;
			}
			if (m_IR != nullptr)
			{
				m_IR->Destroy();
				delete m_IR;
				m_IR = nullptr;
			}

			m_AST = new AST::AST();
			m_AST->Generate(source);
			if (m_AST->rootBlock != nullptr && m_AST->diagnosticContainer->diagnostics.empty())
			{
				astStr = m_AST->rootBlock->ToString();

				m_IR = new IR::IntermediateRepresentation();
				m_IR->GenerateFromAST(m_AST);

				if (m_IR->firstBlock != nullptr && m_IR->state.diagnosticContainer->diagnostics.empty())
				{
					irStr = m_IR->firstBlock->ToString();

					GenerateFromIR(m_IR);
				}

				for (const Diagnostic& diagnostic : m_IR->state.diagnosticContainer->diagnostics)
				{
					diagnosticContainer->diagnostics.push_back(diagnostic);
				}
			}

			for (const Diagnostic& diagnostic : m_AST->diagnosticContainer->diagnostics)
			{
				diagnosticContainer->diagnostics.push_back(diagnostic);
			}

			m_bCompiled = diagnosticContainer->diagnostics.empty();
		}

		ValueWrapper VirtualMachine::GetValueWrapperFromIRValue(IR::State& irState, IR::Value* value)
		{
			ValueWrapper valWrapper;

			if (IR::Value::IsLiteral(value->type))
			{
				valWrapper = ValueWrapper(ValueWrapper::Type::CONSTANT, VM::Value(*value));
			}
			else
			{
				switch (value->type)
				{
				case IR::Value::Type::IDENTIFIER:
				{
					IR::Identifier* identifier = (IR::Identifier*)value;
					if (state.varRegisterMap.find(identifier->variable) == state.varRegisterMap.end())
					{
						std::string diagnosticStr = "Undeclared identifier: " + identifier->variable;
						irState.diagnosticContainer->AddDiagnostic(Span(0, 0), 0, 0, diagnosticStr.c_str());
					}
					else
					{
						i32 reg = state.varRegisterMap[identifier->variable];
						valWrapper = ValueWrapper(ValueWrapper::Type::REGISTER, VM::Value(reg));
					}
				} break;
				case IR::Value::Type::UNARY:
				{

				} break;
				case IR::Value::Type::BINARY:
				{
					//AST::Identifier* ident = (AST::Identifier*)expression;
					//i32 reg = state.varToRegisterMap[ident->identifierStr];
					//valWrapper = ValueWrapper(ValueWrapper::Type::REGISTER, IR::Value(reg));
				} break;
				case IR::Value::Type::FUNC_CALL:
				{
					//AST::FunctionCall* funcCall = (AST::FunctionCall*)expression;
					//i32 registerStored = GenerateCallInstruction(funcCall);
					//valWrapper = ValueWrapper(ValueWrapper::Type::REGISTER, IR::Value(registerStored));
				} break;
				case IR::Value::Type::CAST:
				{
					IR::CastValue* castValue = (IR::CastValue*)value;
					return GetValueWrapperFromIRValue(irState, castValue->target);
				} break;
				default:
				{
					//GenerateStatementInstructions(expression);
				} break;
				}
			}

			return valWrapper;
		}

		IR::Value::Type VirtualMachine::FindIRType(IR::State& irState, IR::Value* irValue)
		{
			switch (irValue->type)
			{
			case IR::Value::Type::INT:
				return IR::Value::Type::INT;
			case IR::Value::Type::FLOAT:
				return IR::Value::Type::FLOAT;
			case IR::Value::Type::IDENTIFIER:
			{
				IR::Identifier* identifier = (IR::Identifier*)irValue;
				auto iter = irState.variableTypes.find(identifier->variable);
				if (iter != irState.variableTypes.end())
				{
					return iter->second;
				}
				auto iter2 = state.tmpVarTypes.find(identifier->variable);
				if (iter2 != irState.variableTypes.end())
				{
					return iter2->second;
				}

				// Fallthrough
			}
			default:
				irState.diagnosticContainer->AddDiagnostic(Span(0, 0), 0, 0, "Unexpected type in cast statement");
				return IR::Value::Type::_NONE;
			}

		}

		void VirtualMachine::GenerateFromIR(IR::IntermediateRepresentation* ir)
		{
			instructions.clear();
			ClearRuntimeState();

			state.Clear();
			state.PushInstructionBlock();

			IR::Block* block = ir->firstBlock;
			//while (block != nullptr)
			{
				for (auto assignmentIter = block->assignments.begin(); assignmentIter != block->assignments.end(); ++assignmentIter)
				{
					IR::Assignment* assignment = *assignmentIter;
					i32 reg = 0;
					if (state.varRegisterMap.find(assignment->variable) == state.varRegisterMap.end())
					{
						state.varRegisterMap[assignment->variable] = (i32)state.varRegisterMap.size();
					}
					reg = state.varRegisterMap[assignment->variable];
					ValueWrapper regVal(ValueWrapper::Type::REGISTER, Value(reg));

					InstructionBlock& currentInstBlock = state.CurrentInstructionBlock();

					if (assignment->value->type == IR::Value::Type::BINARY)
					{
						IR::BinaryValue* binaryValue = (IR::BinaryValue*)assignment->value;
						OpCode opCode = OpCodeFromBinaryOperatorType(binaryValue->opType);
						currentInstBlock.PushBack(Instruction(opCode, regVal, GetValueWrapperFromIRValue(ir->state, binaryValue->left), GetValueWrapperFromIRValue(ir->state, binaryValue->right)));
					}
					else if (assignment->value->type == IR::Value::Type::UNARY)
					{
						IR::UnaryValue* unaryValue = (IR::UnaryValue*)assignment->value;
						OpCode opCode = OpCodeFromUnaryOperatorType(unaryValue->opType);
						currentInstBlock.PushBack(Instruction(opCode, regVal, GetValueWrapperFromIRValue(ir->state, unaryValue->operand)));
					}
					else if (assignment->value->type == IR::Value::Type::CAST)
					{
						IR::CastValue* castValue = (IR::CastValue*)assignment->value;
						switch (castValue->castedType)
						{
						case IR::Value::Type::INT:
						{
							IR::Value::Type irValueType = FindIRType(ir->state, castValue->target);
							switch (irValueType)
							{
							case IR::Value::Type::INT:
								// no-op
								break;
							case IR::Value::Type::FLOAT:
								currentInstBlock.PushBack(Instruction(OpCode::FTI, regVal, GetValueWrapperFromIRValue(ir->state, castValue->target)));
								break;
							default:
								state.diagnosticContainer->AddDiagnostic(Span(0, 0), 0, 0, "Unexpected type in cast statement");
								break;
							}
						} break;
						case IR::Value::Type::FLOAT:
						{
							IR::Value::Type irValueType = FindIRType(ir->state, castValue->target);
							switch (irValueType)
							{
							case IR::Value::Type::INT:
								currentInstBlock.PushBack(Instruction(OpCode::ITF, regVal, GetValueWrapperFromIRValue(ir->state, castValue->target)));
								break;
							case IR::Value::Type::FLOAT:
								// no-op
								break;
							default:
								state.diagnosticContainer->AddDiagnostic(Span(0, 0), 0, 0, "Unexpected type in cast statement");
								break;
							}
						} break;
						default:
						{
							state.diagnosticContainer->AddDiagnostic(Span(0, 0), 0, 0, "Unexpected type in cast statement");
						} break;
						}
					}
					else
					{
						currentInstBlock.PushBack(Instruction(OpCode::MOV, regVal, GetValueWrapperFromIRValue(ir->state, assignment->value)));
					}
				}

				if (block->terminator != nullptr)
				{
					//state.CurrentInstructionBlock().PushBack(Instruction(OpCode::JMP, block->terminator));
				}
			}

			state.CurrentInstructionBlock().PushBack({ OpCode::TERMINATE });

			// Turn instruction blocks into contiguous instruction list
			{
				u32 instructionIndex = 0;
				for (u32 i = 0; i < (u32)state.instructionBlocks.size(); ++i)
				{
					std::vector<Instruction>& blockInstructions = state.instructionBlocks[i].instructions;
					state.instructionBlocks[i].startOffset = instructionIndex;
					for (u32 j = 0; j < (u32)blockInstructions.size(); ++j)
					{
						instructions.push_back(blockInstructions[j]);
						++instructionIndex;
					}
				}
			}

			// TODO: Properly

			// Patch up function calls to point at proper locations
			for (u32 i = 0; i < (u32)instructions.size(); ++i)
			{
				Instruction& inst = instructions[i];

				switch (inst.opCode)
				{
				case OpCode::CALL:
				{
					i32 funcAddress = state.instructionBlocks[inst.val0.Get(this).valInt].startOffset;
					inst.val0.value.valInt = funcAddress;
				} break;
				}
			}

			StringBuilder instructionStrBuilder;
			for (u32 i = 0; i < (u32)instructions.size(); ++i)
			{
				const Instruction& inst = instructions[i];
				std::string val0Str = inst.val0.ToString();
				std::string val1Str = inst.val1.ToString();
				std::string val2Str = inst.val2.ToString();
				instructionStrBuilder.Append(OpCodeToString(inst.opCode));
				instructionStrBuilder.Append("  ");
				instructionStrBuilder.Append(val0Str.c_str());
				instructionStrBuilder.Append(" ");
				instructionStrBuilder.Append(val1Str.c_str());
				instructionStrBuilder.Append(" ");
				instructionStrBuilder.Append(val2Str.c_str());
				instructionStrBuilder.Append("\n");
			}
			instructionStr = instructionStrBuilder.ToString();
		}

		void VirtualMachine::GenerateFromInstStream(const std::vector<Instruction>& inInstructions)
		{
			ClearRuntimeState();
			instructions = inInstructions;
			m_bCompiled = true;
		}

		void VirtualMachine::Execute(bool bSingleStep /* = false */)
		{
			if (!m_bCompiled)
			{
				return;
			}

			if (instructions.empty())
			{
				PrintError("Attempted to execute VM without any instructions present\n");
				return;
			}

			if (!bSingleStep)
			{
				m_RunningState.Clear();

				diagnosticContainer->diagnostics.clear();
			}

			u32 loopCount = 0;
			bool bBreak = false;
			while (!m_RunningState.terminated && !bBreak)
			{
				Instruction& inst = instructions[m_RunningState.instructionIdx];

				i32 pInstIdx = m_RunningState.instructionIdx;
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
				//case OpCode::INV:
				//case OpCode::AND:
				//case OpCode::OR:
				//case OpCode::XOR:
				case OpCode::ITF:
				{
					Value& regVal = inst.val0.GetW(this);
					regVal.type = Value::Type::FLOAT;
					regVal.valFloat = inst.val1.Get(this).AsFloat();
				} break;
				case OpCode::FTI:
				{
					Value& regVal = inst.val0.GetW(this);
					regVal.type = Value::Type::INT;
					regVal.valInt = inst.val1.Get(this).AsInt();
				} break;
				case OpCode::CALL:
				{
					FuncAddress funcAddress = inst.val0.Get(this).valInt;
					if (IsExternal(funcAddress))
					{
						DispatchExternalCall(funcAddress);
					}
					else
					{
						m_RunningState.instructionIdx = TranslateLocalFuncAddress(funcAddress);
					}
				} break;
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
					m_RunningState.zf = diff.IsZero();
					m_RunningState.sf = diff.IsPositive();
				} break;
				case OpCode::JMP:
					m_RunningState.instructionIdx = inst.val0.Get(this).valInt;
					break;
				case OpCode::JLT:
					if (!m_RunningState.sf && !m_RunningState.zf)
					{
						m_RunningState.instructionIdx = inst.val0.Get(this).valInt;
					}
					break;
				case OpCode::JLE:
					if (!m_RunningState.sf || m_RunningState.zf)
					{
						m_RunningState.instructionIdx = inst.val0.Get(this).valInt;
					}
					break;
				case OpCode::JGT:
					if (m_RunningState.sf && !m_RunningState.zf)
					{
						m_RunningState.instructionIdx = inst.val0.Get(this).valInt;
					}
					break;
				case OpCode::JGE:
					if (m_RunningState.sf || m_RunningState.zf)
					{
						m_RunningState.instructionIdx = inst.val0.Get(this).valInt;
					}
					break;
				case OpCode::JEQ:
					if (m_RunningState.zf)
					{
						m_RunningState.instructionIdx = inst.val0.Get(this).valInt;
					}
					break;
				case OpCode::JNE:
					if (!m_RunningState.zf)
					{
						m_RunningState.instructionIdx = inst.val0.Get(this).valInt;
					}
					break;
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
					m_RunningState.instructionIdx = stack.top().valInt;
					stack.pop();
					if (!bVoid)
					{
						stack.push(returnVal);
					}
				} break;
				case OpCode::TERMINATE:
					m_RunningState.terminated = true;
					break;
				default:
					std::string opCodeStr(OpCodeToString(inst.opCode));
					std::string errorMsg = "Unhandled op code in VirtualMachine::Execute: " + opCodeStr;
					diagnosticContainer->AddDiagnostic(Span(0, 0), 0, 0, errorMsg);
					break;
				}

				if (!m_RunningState.terminated && pInstIdx == m_RunningState.instructionIdx)
				{
					++m_RunningState.instructionIdx;
				}

				if (++loopCount > 10'000'000)
				{
					diagnosticContainer->AddDiagnostic(Span(0, 0), 0, 0, "Execution loop took too long, broke out early\n");
					m_RunningState.terminated = true;
				}

				if (bSingleStep)
				{
					break;
				}
			}
		}

		DiagnosticContainer* VirtualMachine::GetDiagnosticContainer()
		{
			return diagnosticContainer;
		}

		bool VirtualMachine::IsExecuting() const
		{
			return !m_RunningState.terminated;
		}

		i32 VirtualMachine::InstructionIndex() const
		{
			return m_RunningState.instructionIdx;
		}

		void VirtualMachine::ClearRuntimeState()
		{
			m_RunningState.Clear();
			AllocateMemory();
			ZeroOutRegisters();
			ClearStack();
			ExternalFuncTable.clear();
		}

		void VirtualMachine::AllocateMemory()
		{
			if (memory == nullptr)
			{
				memory = (u32*)malloc(MEMORY_POOL_SIZE);
				if (memory == nullptr)
				{
					PrintError("Failed to allocate %u bytes for VM memory!\n", MEMORY_POOL_SIZE);
				}
			}
		}

		void VirtualMachine::ZeroOutRegisters()
		{
			for (u32 i = 0; i < REGISTER_COUNT; ++i)
			{
				registers[i].valInt = 0;
				registers[i].type = VM::Value::Type::_NONE;
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

		/*
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
		*/

	} // namespace VM
} // namespace flex
