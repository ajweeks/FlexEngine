#include "stdafx.hpp"

#include "VirtualMachine/Backend/VirtualMachine.hpp"

#include "Helpers.hpp"
#include "StringBuilder.hpp"
#include "VirtualMachine/Backend/IR.hpp"
#include "VirtualMachine/Backend/VariableContainer.hpp"
#include "VirtualMachine/Diagnostics.hpp"
#include "VirtualMachine/Frontend/Lexer.hpp"
#include "VirtualMachine/Frontend/Parser.hpp"

namespace flex
{
	namespace VM
	{
		VariantWrapper VirtualMachine::g_ZeroIntVariantWrapper = VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(0));
		VariantWrapper VirtualMachine::g_ZeroFloatVariantWrapper = VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(0.0f));
		VariantWrapper VirtualMachine::g_OneIntVariantWrapper = VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(1));
		VariantWrapper VirtualMachine::g_OneFloatVariantWrapper = VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(1.0f));

		Variant& VariantWrapper::Get(VirtualMachine* vm)
		{
			if (type == Type::REGISTER)
			{
				return vm->registers[variant.valInt];
			}
			else if (type == Type::CONSTANT)
			{
				return variant;
			}
			else if (type == Type::TERMINAL_OUTPUT)
			{
				return vm->terminalOutputs[variant.valInt];
			}
			else
			{
				PrintError("Unhandled variant type\n");
				return g_EmptyVariant;
			}
		}

		Variant& VariantWrapper::GetW(VirtualMachine* vm)
		{
			if (type != Type::REGISTER && type != Type::TERMINAL_OUTPUT)
			{
				PrintError("GetW can only be called on register or terminal output values\n");
			}
			return Get(vm);
		}

		bool VariantWrapper::Valid() const
		{
			return type != Type::NONE;
		}

		std::string VariantWrapper::ToString() const
		{
			if (type == Type::REGISTER)
			{
				return "r" + variant.ToString();
			}
			else if (type == Type::TERMINAL_OUTPUT)
			{
				return "out_" + variant.ToString();
			}
			else
			{
				return variant.ToString();
			}
		}

		State::State()
		{
			diagnosticContainer = new DiagnosticContainer();
		}

		void State::Destroy()
		{
			delete diagnosticContainer;
		}

		void State::Clear()
		{
			varUsages.clear();
			varRegisterMap.clear();
			funcNameToBlockIndexTable.clear();
			instructionBlocks.clear();

			diagnosticContainer->diagnostics.clear();
		}

		void InstructionBlock::PushBack(const Instruction& inst, Span origin, State* state)
		{
			if (inst.opCode == OpCode::JMP ||
				inst.opCode == OpCode::JZ ||
				inst.opCode == OpCode::JNZ ||
				inst.opCode == OpCode::JEQ ||
				inst.opCode == OpCode::JNE ||
				inst.opCode == OpCode::JLT ||
				inst.opCode == OpCode::JLE ||
				inst.opCode == OpCode::JGT ||
				inst.opCode == OpCode::JGE)
			{
				if (inst.val0.variant.AsInt() == -1)
				{
					state->diagnosticContainer->AddDiagnostic(origin, "Compiler error: jump to -1 generated");
				}
			}

			instructions.push_back(inst);
			instructionOrigins.push_back(origin);
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

		VirtualMachine::VirtualMachine()
		{
			state = new State();
			diagnosticContainer = new DiagnosticContainer();
		}

		VirtualMachine::~VirtualMachine()
		{
			state->Destroy();
			delete state;

			delete diagnosticContainer;

			if (m_AST != nullptr)
			{
				m_AST->Destroy();
				delete m_AST;
			}
			if (m_FunctionBindings != nullptr)
			{
				m_FunctionBindings->ClearBindings();
				delete m_FunctionBindings;
				m_FunctionBindings = nullptr;
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
			if (m_FunctionBindings != nullptr)
			{
				m_FunctionBindings->ClearBindings();
				delete m_FunctionBindings;
				m_FunctionBindings = nullptr;
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

				m_FunctionBindings = new FunctionBindings();
				m_FunctionBindings->RegisterBindings();

				m_IR = new IR::IntermediateRepresentation();
				m_IR->GenerateFromAST(m_AST, m_FunctionBindings);

				if (!m_IR->blocks.empty() && m_IR->state->diagnosticContainer->diagnostics.empty())
				{
					irStr = m_IR->ToString();

					GenerateFromIR(m_IR, m_FunctionBindings);
				}

				for (const Diagnostic& diagnostic : m_IR->state->diagnosticContainer->diagnostics)
				{
					diagnosticContainer->diagnostics.push_back(diagnostic);
				}
			}

			for (const Diagnostic& diagnostic : m_AST->diagnosticContainer->diagnostics)
			{
				diagnosticContainer->diagnostics.push_back(diagnostic);
			}

			diagnosticContainer->ComputeLineColumnIndicesFromSource(m_AST->lexer->sourceIter.source);

			m_bCompiled = diagnosticContainer->diagnostics.empty();
		}

		VariantWrapper VirtualMachine::GetValueWrapperFromIRValue(IR::State* irState, IR::Value* value)
		{
			VariantWrapper valWrapper;

			if (IR::Value::IsLiteral(value->type))
			{
				valWrapper = VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(*value));
			}
			else
			{
				switch (value->type)
				{
				case IR::Value::Type::IDENTIFIER:
				{
					IR::Identifier* identifier = (IR::Identifier*)value;
					if (state->varRegisterMap.find(identifier->variable) == state->varRegisterMap.end())
					{
						std::string diagnosticStr = "Undeclared identifier: " + identifier->variable;
						irState->diagnosticContainer->AddDiagnostic(value->origin, diagnosticStr.c_str());
					}
					else
					{
						i32 reg = state->varRegisterMap[identifier->variable];
						valWrapper = VariantWrapper(VariantWrapper::Type::REGISTER, Variant(reg));
					}
				} break;
				case IR::Value::Type::UNARY:
				{
					// TODO:
				} break;
				case IR::Value::Type::BINARY:
				{
					IR::BinaryValue* binary = (IR::BinaryValue*)value;

					// TODO:
					return GetValueWrapperFromIRValue(irState, binary->left);
					//i32 reg = state->varRegisterMap[binary->left];
					//valWrapper = VariantWrapper(VariantWrapper::Type::REGISTER, IR::Value(reg));
				} break;
				case IR::Value::Type::CALL:
				{
					IR::FunctionCallValue* funcCallValue = (IR::FunctionCallValue*)value;
					i32 registerStored = GenerateCallInstruction(irState, funcCallValue);
					if (registerStored != -1)
					{
						valWrapper = VariantWrapper(VariantWrapper::Type::REGISTER, Variant(registerStored));
					}
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

		i32 VirtualMachine::GenerateCallInstruction(IR::State* irState, IR::FunctionCallValue* funcCallValue)
		{
			if (state->funcNameToBlockIndexTable.find(funcCallValue->target) == state->funcNameToBlockIndexTable.end())
			{
				std::string errorMessage = "Unexpected function: " + funcCallValue->target;
				irState->diagnosticContainer->AddDiagnostic(funcCallValue->origin, errorMessage);
				return -1;
			}
			// Temporary identifier for the function since we
			// don't know where it will be located in the end
			i32 funcAddress = state->funcNameToBlockIndexTable[funcCallValue->target];
			bool bFuncIsExternal = IsExternal(funcAddress);

			InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();

			// Push return IP
			// We don't jump to external functions, so a resume
			// point doesn't need to be pushed
			i32 resumePointPushInstructionIndex = -1;
			if (!bFuncIsExternal)
			{
				// Actual IP will be patched up below
				resumePointPushInstructionIndex = (i32)currentInstBlock.instructions.size();
				Instruction pushReturnIP(OpCode::PUSH, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(0)));
				currentInstBlock.PushBack(pushReturnIP, funcCallValue->origin, state);
			}

			// Push arguments in reverse order
			for (i32 i = (i32)funcCallValue->arguments.size() - 1; i >= 0; --i)
			{
				IR::Value* arg = funcCallValue->arguments[i];

				VariantWrapper argVal;
				if (IR::Value::IsLiteral(arg->type))
				{
					argVal = GetValueWrapperFromIRValue(irState, arg);
				}
				else
				{
					switch (arg->type)
					{
					case IR::Value::Type::IDENTIFIER:
					{
						IR::Identifier* initializerIdent = (IR::Identifier*)arg;
						i32 getRegister = state->varRegisterMap[initializerIdent->variable];
						argVal = VariantWrapper(VariantWrapper::Type::REGISTER, Variant(getRegister));
					} break;
					case IR::Value::Type::UNARY:
					{
						IR::UnaryValue* unaryValue = (IR::UnaryValue*)arg;
						argVal = GetValueWrapperFromIRValue(irState, unaryValue);
					} break;
					case IR::Value::Type::BINARY:
					{
						IR::BinaryValue* binaryValue = (IR::BinaryValue*)arg;
						argVal = GetValueWrapperFromIRValue(irState, binaryValue);
					} break;
					case IR::Value::Type::TERNARY:
					{
						IR::TernaryValue* ternaryValue = (IR::TernaryValue*)arg;
						argVal = GetValueWrapperFromIRValue(irState, ternaryValue);
					} break;
					case IR::Value::Type::CALL:
					{
						IR::FunctionCallValue* subFuncCall = (IR::FunctionCallValue*)arg;
						i32 registerStored = GenerateCallInstruction(irState, subFuncCall);
						if (registerStored != -1)
						{
							argVal = VariantWrapper(VariantWrapper::Type::REGISTER, Variant(registerStored));
						}
						else
						{
							return -1;
						}
					} break;
					case IR::Value::Type::CAST:
					{
						IR::CastValue* castValue = (IR::CastValue*)arg;
						argVal = GetValueWrapperFromIRValue(irState, castValue);
					} break;
					case IR::Value::Type::VOID_:
					{
						ENSURE_NO_ENTRY();
					} break;
					}
				}

				Instruction pushArg(OpCode::PUSH, argVal);
				currentInstBlock.PushBack(pushArg, funcCallValue->origin, state);
			}

			// Call
			Instruction inst(OpCode::CALL, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(funcAddress)));
			currentInstBlock.PushBack(inst, funcCallValue->origin, state);

			// Resume point
			if (!bFuncIsExternal)
			{
				// Patch up push call to current instruction offset
				i32 instructionIndex = (i32)currentInstBlock.instructions.size();
				i32 value = currentInstBlock.startOffset + instructionIndex;
				currentInstBlock.instructions[resumePointPushInstructionIndex].val0.variant.valInt = value;
			}

			IR::Value::Type funcReturnType = irState->functionTypes[funcCallValue->target].returnType;

			// TODO: Choose register intelligently
			i32 returnValueRegister = 0;
			if (funcReturnType != IR::Value::Type::VOID_)
			{
				Instruction popReturnVal(OpCode::POP, VariantWrapper(VariantWrapper::Type::REGISTER, Variant(returnValueRegister)));
				currentInstBlock.PushBack(popReturnVal, funcCallValue->origin, state);
				return returnValueRegister;
			}

			return -1;
		}

		i32 VirtualMachine::CombineInstructionIndex(i32 instructionBlockIndex, i32 instructionIndex)
		{
			u32 value = ((u32)instructionBlockIndex << 16) + ((u32)instructionIndex & 0xFFFF);
			CHECK_EQ((value >> 16), (u32)instructionBlockIndex);
			CHECK_EQ((value & 0xFFFF), (u32)instructionIndex);
			return static_cast<i32>(value);
		}

		void VirtualMachine::SplitInstructionIndex(i32 combined, i32& outInstructionBlockIndex, i32& outInstructionIndex)
		{
			u32 valueUnsigned = static_cast<u32>(combined);
			outInstructionBlockIndex = (i32)(valueUnsigned >> 16);
			outInstructionIndex = (i32)(valueUnsigned & 0xFFFF);
		}

		bool VirtualMachine::IsTerminalOutputVar(const std::string& varName)
		{
			return GetTerminalOutputVar(varName) != -1;
		}

		i32 VirtualMachine::GetTerminalOutputVar(const std::string& varName)
		{
			if (StartsWith(varName, "out_"))
			{
				std::string remainder = varName.substr(4);
				if (strcmp(remainder.c_str(), "0") == 0)
				{
					return 0;
				}

				i32 result = strtol(remainder.c_str(), nullptr, 0);
				if (result != 0) // 0 is error case when str != "0"
				{
					return result;
				}
			}

			return -1;
		}

		IR::Value::Type VirtualMachine::FindIRType(IR::State* irState, IR::Value* irValue)
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
				auto iter = irState->variableTypes.find(identifier->variable);
				if (iter != irState->variableTypes.end())
				{
					return iter->second;
				}
				auto iter2 = state->tmpVarTypes.find(identifier->variable);
				if (iter2 != irState->variableTypes.end())
				{
					return iter2->second;
				}
			}
			// fall through
			default:
				irState->diagnosticContainer->AddDiagnostic(irValue->origin, "Unexpected type in cast statement");
				return IR::Value::Type::_NONE;
			}
		}

		void VirtualMachine::HandleComparison(VariantWrapper& regVal, IR::IntermediateRepresentation* ir, IR::BinaryValue* binaryValue)
		{
			if (binaryValue->left->type == IR::Value::Type::BINARY)
			{
				IR::BinaryValue* lhsBinaryValue = (IR::BinaryValue*)binaryValue->left;
				HandleComparison(regVal, ir, lhsBinaryValue);
			}

			InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();

			currentInstBlock.PushBack(Instruction(OpCode::CMP, GetValueWrapperFromIRValue(ir->state, binaryValue->left), g_ZeroIntVariantWrapper), binaryValue->origin, state);

			i32 trueBlock = 0;
			i32 falseBlock = 1;

			if (binaryValue->opType == IR::BinaryOperatorType::BOOLEAN_AND)
			{
				currentInstBlock.PushBack(Instruction(OpCode::JEQ, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(falseBlock))), binaryValue->origin, state);

				currentInstBlock.PushBack(Instruction(OpCode::CMP, GetValueWrapperFromIRValue(ir->state, binaryValue->right), g_ZeroIntVariantWrapper), binaryValue->origin, state);

			}
			else if (binaryValue->opType == IR::BinaryOperatorType::BOOLEAN_OR)
			{
				currentInstBlock.PushBack(Instruction(OpCode::JEQ, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(trueBlock))), binaryValue->origin, state);

				currentInstBlock.PushBack(Instruction(OpCode::CMP, GetValueWrapperFromIRValue(ir->state, binaryValue->right), g_ZeroIntVariantWrapper), binaryValue->origin, state);
			}
			else
			{
				currentInstBlock.PushBack(Instruction(BinaryOperatorTypeToInverseOpCode(binaryValue->opType), VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(falseBlock))), binaryValue->origin, state);
				currentInstBlock.PushBack(Instruction(OpCode::JMP, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(trueBlock))), binaryValue->origin, state);
			}

			currentInstBlock.PushBack(Instruction(OpCode::MOV, regVal), binaryValue->origin, state);

			// Move up?
			if (binaryValue->right->type == IR::Value::Type::BINARY)
			{
				IR::BinaryValue* rhsBinaryValue = (IR::BinaryValue*)binaryValue->right;
				HandleComparison(regVal, ir, rhsBinaryValue);
			}
		}

		void VirtualMachine::HandleComparison(IR::IntermediateRepresentation* ir, IR::Value* condition, i32 ifTrueBlockIndex, i32 ifFalseBlockIndex, bool bInvCondition)
		{
			InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();
			OpCode opCode = OpCode::_NONE;
			bool bHandled = false;
			switch (condition->type)
			{
			case IR::Value::Type::BINARY:
			{
				IR::BinaryValue* binary = (IR::BinaryValue*)condition;

				switch (binary->opType)
				{
				case IR::BinaryOperatorType::EQUAL_TEST:
					opCode = OpCode::JEQ;
					break;
				case IR::BinaryOperatorType::NOT_EQUAL_TEST:
					opCode = OpCode::JNE;
					break;
				case IR::BinaryOperatorType::GREATER_EQUAL_TEST:
					opCode = OpCode::JGE;
					break;
				case IR::BinaryOperatorType::GREATER_TEST:
					opCode = OpCode::JGT;
					break;
				case IR::BinaryOperatorType::LESS_EQUAL_TEST:
					opCode = OpCode::JLE;
					break;
				case IR::BinaryOperatorType::LESS_TEST:
					opCode = OpCode::JLT;
					break;
				case IR::BinaryOperatorType::BIN_AND:
					opCode = OpCode::AND;
					break;
				case IR::BinaryOperatorType::BIN_OR:
					opCode = OpCode::OR;
					break;
				case IR::BinaryOperatorType::BIN_XOR:
					opCode = OpCode::XOR;
					break;
				case IR::BinaryOperatorType::ADD:

					break;
				case IR::BinaryOperatorType::BOOLEAN_AND:
				{
					HandleComparison(ir, binary->left, ifTrueBlockIndex, ifFalseBlockIndex, true);
					HandleComparison(ir, binary->right, ifTrueBlockIndex, ifFalseBlockIndex, true);

					bHandled = true;
					opCode = OpCode::_NONE;
				} break;
				case IR::BinaryOperatorType::BOOLEAN_OR:
				{
					HandleComparison(ir, binary->left, ifTrueBlockIndex, ifFalseBlockIndex, false);
					HandleComparison(ir, binary->right, ifTrueBlockIndex, ifFalseBlockIndex, false);

					bHandled = true;
					opCode = OpCode::_NONE;
				} break;
				}

				if (!bHandled)
				{
					if (opCode == OpCode::_NONE)
					{
						ir->state->diagnosticContainer->AddDiagnostic(condition->origin, "Invalid comparison");
						return;
					}

					VariantWrapper lhsWrapper = GetValueWrapperFromIRValue(ir->state, binary->left);
					VariantWrapper rhsWrapper = GetValueWrapperFromIRValue(ir->state, binary->right);

					currentInstBlock.PushBack(Instruction(OpCode::CMP, lhsWrapper, rhsWrapper), binary->origin, state);
					opCode = bInvCondition ? InverseOpCode(opCode) : opCode;
					i32 jumpBlockIndex = (i32)(bInvCondition ? ifFalseBlockIndex : ifTrueBlockIndex);
					i32 jumpBlockIndexOther = (i32)(bInvCondition ? ifTrueBlockIndex : ifFalseBlockIndex);
					currentInstBlock.PushBack(Instruction(opCode, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(jumpBlockIndex))), binary->origin, state);
					currentInstBlock.PushBack(Instruction(OpCode::JMP, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(jumpBlockIndexOther))), binary->origin, state);
				}
			} break;
			case IR::Value::Type::IDENTIFIER:
			case IR::Value::Type::CALL:
			case IR::Value::Type::CAST:
			case IR::Value::Type::UNARY:
			{
				ir->state->diagnosticContainer->AddDiagnostic(condition->origin, "Unhandled conditional");
				//VariantWrapper conditionWrapper = GetValueWrapperFromIRValue(ir->state, condition);
				//VariantWrapper zeroWrapper = GetValueWrapperFromIRValue(ir->state, condition);
				//currentInstBlock.PushBack(Instruction(OpCode::CMP, conditionWrapper, rhsWrapper), condition->origin);
				//currentInstBlock.PushBack(Instruction(OpCode::JNE, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(jumpBlockIndex))), binary->origin);
				//currentInstBlock.PushBack(Instruction(OpCode::JMP, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(jumpBlockIndexOther))), binary->origin);
			} break;
			default:
			{
				ir->state->diagnosticContainer->AddDiagnostic(condition->origin, "Unhandled conditional type");
			} break;
			}
		}

		void VirtualMachine::GenerateFromIR(IR::IntermediateRepresentation* ir, FunctionBindings* functionBindings)
		{
			instructions.clear();
			instructionOrigins.clear();
			ClearRuntimeState();

			state->Clear();

			// Find all internal function block indices
			for (i32 i = 0; i < (i32)ir->blocks.size(); ++i)
			{
				IR::Block* block = ir->blocks[i];
				if (!block->funcName.empty())
				{
					state->funcNameToBlockIndexTable[block->funcName] = i;
				}
			}

			for (auto iter = functionBindings->ExternalFuncTable.begin(); iter != functionBindings->ExternalFuncTable.end(); ++iter)
			{
				FuncAddress funcAddress = iter->first;
				IFunction* function = iter->second;
				state->funcNameToBlockIndexTable[function->name] = funcAddress;
			}

			u32 instructionIndex = 0;
			for (u32 i = 0; i < (u32)ir->blocks.size(); ++i)
			{
				IR::Block* block = ir->blocks[i];
				state->PushInstructionBlock();

				{
					InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();
					currentInstBlock.startOffset = instructionIndex;
				}

				for (auto assignmentIter = block->assignments.begin(); assignmentIter != block->assignments.end(); ++assignmentIter)
				{
					IR::Assignment* assignment = *assignmentIter;
					VariantWrapper outputVal;
					if (state->varRegisterMap.find(assignment->variable) == state->varRegisterMap.end())
					{
						if (state->varRegisterMap.size() == REGISTER_COUNT)
						{
							ir->state->diagnosticContainer->AddDiagnostic(assignment->origin, "Ran out of registers!");
							return;
						}
						state->varRegisterMap[assignment->variable] = (i32)state->varRegisterMap.size();
					}

					i32 terminalOutputVarIndex = GetTerminalOutputVar(assignment->variable);
					if (terminalOutputVarIndex != -1)
					{
						if (terminalOutputVarIndex >= Terminal::MAX_OUTPUT_COUNT)
						{
							std::string message = "Used invalid terminal output var, range is between 0 & " + IntToString(Terminal::MAX_OUTPUT_COUNT);
							state->diagnosticContainer->AddDiagnostic(assignment->origin, message);
						}
						outputVal = VariantWrapper(VariantWrapper::Type::TERMINAL_OUTPUT, Variant(terminalOutputVarIndex));
					}
					else
					{
						i32 reg = state->varRegisterMap[assignment->variable];
						outputVal = VariantWrapper(VariantWrapper::Type::REGISTER, Variant(reg));
					}

					InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();

					switch (assignment->value->type)
					{
					case IR::Value::Type::UNARY:
					{
						IR::UnaryValue* unaryValue = (IR::UnaryValue*)assignment->value;

						switch (unaryValue->opType)
						{
						case IR::UnaryOperatorType::NEGATE:
						{
							IR::Value::Type operandType = ir->state->GetValueType(unaryValue->operand);
							VariantWrapper zeroVal = operandType == IR::Value::Type::INT ? g_ZeroIntVariantWrapper : g_ZeroFloatVariantWrapper;
							currentInstBlock.PushBack(Instruction(VM::OpCode::SUB, outputVal, zeroVal, GetValueWrapperFromIRValue(ir->state, unaryValue->operand)), unaryValue->origin, state);
						} break;
						case IR::UnaryOperatorType::NOT:
						{
							VariantWrapper val = GetValueWrapperFromIRValue(ir->state, unaryValue->operand);
							currentInstBlock.PushBack(Instruction(VM::OpCode::XOR, outputVal, val, val), unaryValue->origin, state);
						} break;
						case IR::UnaryOperatorType::BIN_INVERT:
							currentInstBlock.PushBack(Instruction(VM::OpCode::INV, outputVal, GetValueWrapperFromIRValue(ir->state, unaryValue->operand)), unaryValue->origin, state);
							break;
						default:
							diagnosticContainer->AddDiagnostic(assignment->origin, "Unexpected assignment type in unary expression\n");
							return;
							break;
						}
					} break;
					case IR::Value::Type::BINARY:
					{
						IR::BinaryValue* binaryValue = (IR::BinaryValue*)assignment->value;
						OpCode opCode = OpCodeFromBinaryOperatorType(binaryValue->opType);
						if (opCode == OpCode::CMP)
						{
							i32 ifTrueBlockIndex = (i32)state->instructionBlocks.size() + 0;
							i32 ifFalseBlockIndex = (i32)state->instructionBlocks.size() + 1;
							//i32 mergeBlockIndex = (i32)state->instructionBlocks.size() + 2;
							HandleComparison(ir, binaryValue, ifTrueBlockIndex, ifFalseBlockIndex, true);
							currentInstBlock.PushBack(Instruction(opCode, GetValueWrapperFromIRValue(ir->state, binaryValue->left), GetValueWrapperFromIRValue(ir->state, binaryValue->right)), binaryValue->origin, state);
							currentInstBlock.PushBack(Instruction(OpCode::MOV, outputVal), binaryValue->origin, state);
						}
						else
						{
							currentInstBlock.PushBack(Instruction(opCode, outputVal, GetValueWrapperFromIRValue(ir->state, binaryValue->left), GetValueWrapperFromIRValue(ir->state, binaryValue->right)), binaryValue->origin, state);
						}
					} break;
					case IR::Value::Type::ARGUMENT:
					{
						IR::Argmuent* argument = (IR::Argmuent*)assignment->value;
						currentInstBlock.PushBack(Instruction(OpCode::POP, outputVal), argument->origin, state);
					} break;
					case IR::Value::Type::TERNARY:
					{
						IR::TernaryValue* ternaryValue = (IR::TernaryValue*)assignment->value;

						i32 ifTrueBlockIndex = (i32)state->instructionBlocks.size() + 0;
						i32 ifFalseBlockIndex = (i32)state->instructionBlocks.size() + 1;
						i32 mergeBlockIndex = (i32)state->instructionBlocks.size() + 2;
						HandleComparison(ir, ternaryValue->condition, ifTrueBlockIndex, ifFalseBlockIndex, true);

						VariantWrapper mergeBlockIndexValueWrapper(VariantWrapper::Type::CONSTANT, Variant(mergeBlockIndex));
						InstructionBlock& ifTrueBlock = state->PushInstructionBlock();
						ifTrueBlock.PushBack(Instruction(OpCode::MOV, outputVal, GetValueWrapperFromIRValue(ir->state, ternaryValue->ifTrue)), ternaryValue->origin, state);
						ifTrueBlock.PushBack(Instruction(OpCode::JMP, mergeBlockIndexValueWrapper), ternaryValue->origin, state);

						InstructionBlock& ifFalseBlock = state->PushInstructionBlock();
						ifFalseBlock.PushBack(Instruction(OpCode::MOV, outputVal, GetValueWrapperFromIRValue(ir->state, ternaryValue->ifFalse)), ternaryValue->origin, state);

						state->PushInstructionBlock();
					} break;
					case IR::Value::Type::CAST:
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
								currentInstBlock.PushBack(Instruction(OpCode::FTI, outputVal, GetValueWrapperFromIRValue(ir->state, castValue->target)), castValue->origin, state);
								break;
							default:
								state->diagnosticContainer->AddDiagnostic(assignment->value->origin, "Unexpected type in cast statement");
								break;
							}
						} break;
						case IR::Value::Type::FLOAT:
						{
							IR::Value::Type irValueType = FindIRType(ir->state, castValue->target);
							switch (irValueType)
							{
							case IR::Value::Type::INT:
								currentInstBlock.PushBack(Instruction(OpCode::ITF, outputVal, GetValueWrapperFromIRValue(ir->state, castValue->target)), castValue->origin, state);
								break;
							case IR::Value::Type::FLOAT:
								// no-op
								break;
							default:
								state->diagnosticContainer->AddDiagnostic(assignment->value->origin, "Unexpected type in cast statement");
								break;
							}
						} break;
						default:
						{
							state->diagnosticContainer->AddDiagnostic(assignment->value->origin, "Unexpected type in cast statement");
						} break;
						}
					} break;
					default:
					{
						VariantWrapper valueWrapper = GetValueWrapperFromIRValue(ir->state, assignment->value);
						// Calls to void functions will return empty ValueWrappers
						if (valueWrapper.type != VariantWrapper::Type::NONE)
						{
							currentInstBlock.PushBack(Instruction(OpCode::MOV, outputVal, valueWrapper), assignment->origin, state);
						}
					} break;
					}
				}

				if (block->terminator != nullptr)
				{
					switch (block->terminator->type)
					{
					case IR::TerminatorType::RETURN:
					{
						InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();

						IR::Return* ret = (IR::Return*)block->terminator;

						VariantWrapper returnVal;
						if (ret->returnValue != nullptr)
						{
							returnVal = GetValueWrapperFromIRValue(ir->state, ret->returnValue);
						}

						// TODO: Select register intelligently
						i32 resumePointRegister = 0;
						VariantWrapper resumePoint(VariantWrapper::Type::REGISTER, Variant(resumePointRegister));
						currentInstBlock.PushBack(Instruction(OpCode::POP, resumePoint), ret->origin, state);
						currentInstBlock.PushBack(Instruction(OpCode::PUSH, returnVal), ret->origin, state);
						currentInstBlock.PushBack(Instruction(OpCode::JMP, resumePoint), ret->origin, state);

						//state->PushInstructionBlock();
					} break;
					//case IR::TerminatorType::BREAK:
					//case IR::TerminatorType::YIELD:
					case IR::TerminatorType::BRANCH:
					{
						InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();

						IR::Branch* branch = (IR::Branch*)block->terminator;
						currentInstBlock.PushBack(Instruction(OpCode::JMP, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant((i32)branch->target->index))), branch->origin, state);
						//state->PushInstructionBlock();
					} break;
					case IR::TerminatorType::CONDITIONAL_BRANCH:
					{
						//InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();

						IR::ConditionalBranch* conditional = (IR::ConditionalBranch*)block->terminator;

						i32 thenBlockIndex = conditional->then->index;
						// TODO: +1?
						i32 otherwiseBlockIndex = conditional->otherwise != nullptr ? conditional->otherwise->index : conditional->then->index + 1;
						HandleComparison(ir, conditional->condition, thenBlockIndex, otherwiseBlockIndex, false);

						//currentInstBlock.PushBack(Instruction(OpCode::JMP, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant((i32)ifFalseBlock->index)), state));
						//state->PushInstructionBlock();
					} break;
					case IR::TerminatorType::HALT:
					{
						InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();
						currentInstBlock.PushBack(Instruction(OpCode::HALT), Span(Span::Source::GENERATED), state);
					} break;
					}
					//currentInstBlock.PushBack(Instruction(OpCode::JMP, block->terminator), state);
				}

				InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();
				instructionIndex += (u32)currentInstBlock.instructions.size();
			}

			std::vector<std::string> sourceLines = SplitNoStrip(m_AST->lexer->sourceIter.source, '\n');

			// Turn instruction blocks into contiguous instruction list
			{
				for (u32 i = 0; i < (u32)state->instructionBlocks.size(); ++i)
				{
					std::vector<Instruction>& blockInstructions = state->instructionBlocks[i].instructions;
					std::vector<Span>& blockInstructionOrigins = state->instructionBlocks[i].instructionOrigins;
					for (u32 j = 0; j < (u32)blockInstructions.size(); ++j)
					{
						instructions.push_back(blockInstructions[j]);
						blockInstructionOrigins[j].ComputeLineColumnIndicesFromSource(sourceLines);
						instructionOrigins.push_back(blockInstructionOrigins[j]);
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
				case OpCode::JMP:
				case OpCode::JEQ:
				case OpCode::JNE:
				case OpCode::JGE:
				case OpCode::JGT:
				case OpCode::JLE:
				case OpCode::JLT:
				{
					i32 blockIndex = inst.val0.Get(this).valInt;
					if (blockIndex < (i32)state->instructionBlocks.size())
					{
						i32 blockStartOffset = state->instructionBlocks[blockIndex].startOffset;
						if (blockStartOffset != -1)
						{
							inst.val0.variant.valInt = blockStartOffset;
						}
						else
						{
							diagnosticContainer->AddDiagnostic(Span(Span::Source::GENERATED), "Compiler error: Jump to invalid block offset (-1)");
						}
					}
					else
					{
						diagnosticContainer->AddDiagnostic(Span(Span::Source::GENERATED), "Invalid block index");
					}
				} break;
				case OpCode::CALL:
				{
					FuncAddress funcAddress = inst.val0.Get(this).valInt;
					if (!IsExternal(funcAddress))
					{
						if (funcAddress < (i32)state->instructionBlocks.size())
						{
							i32 funcAddressLocal = state->instructionBlocks[funcAddress].startOffset;
							if (funcAddressLocal != -1)
							{
								inst.val0.variant.valInt = funcAddressLocal;
							}
							else
							{
								diagnosticContainer->AddDiagnostic(Span(Span::Source::GENERATED), "Compiler error: Calling invalid function (block start offset = -1)");
							}
						}
						else
						{
							diagnosticContainer->AddDiagnostic(Span(Span::Source::GENERATED), "Invalid block index");
						}
					}
				} break;
				}
			}

			StringBuilder strBuilder;
			for (u32 i = 0; i < (u32)state->instructionBlocks.size(); ++i)
			{
				const InstructionBlock& instBlock = state->instructionBlocks[i];
				strBuilder.Append(IntToString(i, 1, ' '));
				strBuilder.AppendLine(": {");
				for (u32 j = 0; j < (u32)instBlock.instructions.size(); ++j)
				{
					const Instruction& inst = instBlock.instructions[j];
					std::string val0Str = inst.val0.ToString();
					std::string val1Str = inst.val1.ToString();
					std::string val2Str = inst.val2.ToString();
					strBuilder.Append("  ");
					strBuilder.Append(OpCodeToString(inst.opCode));
					strBuilder.Append(" ");
					strBuilder.Append(val0Str.c_str());
					strBuilder.Append(" ");
					strBuilder.Append(val1Str.c_str());
					strBuilder.Append(" ");
					strBuilder.Append(val2Str.c_str());
					strBuilder.Append("\n");
				}
				strBuilder.AppendLine("}");
			}
			unpatchedInstructionStr = strBuilder.ToString();

			strBuilder.Clear();
			for (u32 i = 0; i < (u32)instructions.size(); ++i)
			{
				const Instruction& inst = instructions[i];
				std::string val0Str = inst.val0.ToString();
				std::string val1Str = inst.val1.ToString();
				std::string val2Str = inst.val2.ToString();
				strBuilder.Append(IntToString(i, 2, ' '));
				strBuilder.Append("  ");
				strBuilder.Append(OpCodeToString(inst.opCode));
				strBuilder.Append(" ");
				strBuilder.Append(val0Str.c_str());
				strBuilder.Append(" ");
				strBuilder.Append(val1Str.c_str());
				strBuilder.Append(" ");
				strBuilder.Append(val2Str.c_str());
				strBuilder.Append("\n");
			}
			instructionStr = strBuilder.ToString();
		}

		void VirtualMachine::GenerateFromInstStream(const std::vector<Instruction>& inInstructions)
		{
			ClearRuntimeState();
			instructions = inInstructions;
			// TODO: require instructionOrigins as well
			instructionOrigins.resize(instructions.size(), Span(Span::Source::GENERATED));
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

			if (m_RunningState.instructionIdx == -1)
			{
				ClearRuntimeState();
				m_RunningState.instructionIdx = 0;
				m_RunningState.terminated = false;
				if (bSingleStep)
				{
					return;
				}
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
					// TODO:
					//case OpCode::INV:
					//case OpCode::AND:
					//case OpCode::OR:
					//case OpCode::XOR:
				case OpCode::ITF:
				{
					Variant& regVal = inst.val0.GetW(this);
					regVal.type = Variant::Type::FLOAT;
					regVal.valFloat = inst.val1.Get(this).AsFloat();
				} break;
				case OpCode::FTI:
				{
					Variant& regVal = inst.val0.GetW(this);
					regVal.type = Variant::Type::INT;
					regVal.valInt = inst.val1.Get(this).AsInt();
				} break;
				case OpCode::CALL:
				{
					FuncAddress funcAddress = inst.val0.Get(this).valInt;
					if (IsExternal(funcAddress))
					{
						if (!DispatchExternalCall(funcAddress))
						{
							m_RunningState.terminated = true;
						}
					}
					else
					{
						m_RunningState.instructionIdx = TranslateLocalFuncAddress(funcAddress);
					}
				} break;
				case OpCode::PUSH:
					if (stack.size() >= MAX_STACK_HEIGHT)
					{
						std::string errorMessage = "Stack overflow (reached max value: " + IntToString(MAX_STACK_HEIGHT) + ")";
						diagnosticContainer->AddDiagnostic(Span(Span::Source::GENERATED), errorMessage);
						m_RunningState.terminated = true;
						break;
					}
					stack.push(inst.val0.Get(this));
					break;
				case OpCode::POP:
					if (stack.empty())
					{
						std::string errorMessage = "Mismatched stack pop (attempted to pop from empty stack)";
						diagnosticContainer->AddDiagnostic(Span(Span::Source::GENERATED), errorMessage);
						m_RunningState.terminated = true;
						break;
					}

					if (inst.val0.Valid())
					{
						if (inst.val0.type == VM::VariantWrapper::Type::REGISTER)
						{
							// Registers can hold any type
							inst.val0.GetW(this).type = Variant::Type::_NONE;
						}
						inst.val0.GetW(this) = stack.top();
					}
					stack.pop();
					break;
				case OpCode::CMP:
				{
					Variant diff = inst.val0.Get(this) - inst.val1.Get(this);
					m_RunningState.zf = diff.IsZero();
					m_RunningState.sf = diff.IsPositive();
				} break;
				case OpCode::JMP:
					m_RunningState.instructionIdx = inst.val0.Get(this).valInt;
					break;
				case OpCode::JZ:
					if (m_RunningState.zf)
					{
						m_RunningState.instructionIdx = inst.val0.Get(this).valInt;
					}
					break;
				case OpCode::JNZ:
					if (!m_RunningState.zf)
					{
						m_RunningState.instructionIdx = inst.val0.Get(this).valInt;
					}
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
					i32 resumePoint = stack.top().valInt;
					m_RunningState.instructionIdx = resumePoint;
					stack.pop();

					const bool bVoid = !inst.val0.Valid();
					if (!bVoid)
					{
						Variant returnVal = Variant(inst.val0.Get(this));
						stack.push(returnVal);
					}
				} break;
				case OpCode::HALT:
					m_RunningState.terminated = true;
					break;
				default:
					std::string opCodeStr(OpCodeToString(inst.opCode));
					std::string errorMsg = "Unhandled op code in VirtualMachine::Execute: " + opCodeStr;
					diagnosticContainer->AddDiagnostic(Span(0, 0), errorMsg);
					break;
				}

				if (!m_RunningState.terminated && pInstIdx == m_RunningState.instructionIdx)
				{
					++m_RunningState.instructionIdx;
				}

				if (++loopCount > 10'000'000)
				{
					diagnosticContainer->AddDiagnostic(Span(0, 0), "Execution loop took too long, broke out early\n");
					m_RunningState.terminated = true;
				}

				if (bSingleStep)
				{
					break;
				}
			}

			if (m_RunningState.terminated)
			{
				m_RunningState.instructionIdx = -1;
			}
		}

		DiagnosticContainer* VirtualMachine::GetDiagnosticContainer()
		{
			return diagnosticContainer;
		}

		bool VirtualMachine::IsExecuting() const
		{
			return m_RunningState.instructionIdx != -1;
		}

		i32 VirtualMachine::InstructionIndex() const
		{
			return m_RunningState.instructionIdx;
		}

		i32 flex::VM::VirtualMachine::CurrentLineNumber() const
		{
			if (m_RunningState.instructionIdx != -1 && m_RunningState.instructionIdx < (i32)instructionOrigins.size())
			{
				return instructionOrigins[m_RunningState.instructionIdx].lineNumber;
			}
			return -1;
		}

		void VirtualMachine::ClearRuntimeState()
		{
			m_RunningState.Clear();
			AllocateMemory();
			ZeroOutRegisters();
			ZeroOutTerminalOutputs();
			ClearStack();
			diagnosticContainer->diagnostics.clear();
		}

		bool VirtualMachine::ZeroFlagSet() const
		{
			return m_RunningState.zf != 0;
		}

		bool VirtualMachine::SignFlagSet() const
		{
			return m_RunningState.sf != 0;
		}

		bool VirtualMachine::IsCompiled() const
		{
			return m_bCompiled;
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
				registers[i].type = Variant::Type::_NONE;
			}
		}

		void VirtualMachine::ZeroOutTerminalOutputs()
		{
			for (u32 i = 0; i < Terminal::MAX_OUTPUT_COUNT; ++i)
			{
				terminalOutputs[i].valInt = -1;
				terminalOutputs[i].type = Variant::Type::INT;
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
			return funcAddress >= 0xFFFF;
		}

		FuncAddress VirtualMachine::GetExternalFuncAddress(const std::string& functionName)
		{
			for (auto& funcPtrPair : m_FunctionBindings->ExternalFuncTable)
			{
				if (funcPtrPair.second->name == functionName)
				{
					return funcPtrPair.first;
				}
			}

			return InvalidFuncAddress;
		}

		i32 VirtualMachine::TranslateLocalFuncAddress(FuncAddress localFuncAddress)
		{
			return localFuncAddress;
		}

		bool VirtualMachine::DispatchExternalCall(FuncAddress funcAddress)
		{
			auto iter = m_FunctionBindings->ExternalFuncTable.find(funcAddress);
			if (iter != m_FunctionBindings->ExternalFuncTable.end())
			{
				IFunction* funcPtr = iter->second;

				std::vector<Variant> args;
				i32 argCount = (i32)funcPtr->argTypes.size();
				args.reserve(argCount);
				for (i32 i = 0; i < argCount; ++i)
				{
					const Variant& arg = stack.top();
					args.emplace_back(arg);
					stack.pop();
				}

				Variant returnVal = funcPtr->Execute(args);

				if (funcPtr->returnType != Variant::Type::VOID_)
				{
					stack.push(returnVal);
				}
			}

			return true;
		}

		const char* OpCodeToString(OpCode opCode)
		{
			return s_OpCodeStrings[(u32)opCode];
		}

		OpCode BinaryOperatorTypeToOpCode(IR::BinaryOperatorType opType)
		{
			switch (opType)
			{
			case IR::BinaryOperatorType::EQUAL_TEST:			return OpCode::JEQ;
			case IR::BinaryOperatorType::NOT_EQUAL_TEST:		return OpCode::JNE;
			case IR::BinaryOperatorType::GREATER_TEST:			return OpCode::JGT;
			case IR::BinaryOperatorType::GREATER_EQUAL_TEST:	return OpCode::JGE;
			case IR::BinaryOperatorType::LESS_TEST:				return OpCode::JLT;
			case IR::BinaryOperatorType::LESS_EQUAL_TEST:		return OpCode::JLE;
			default:
			{

				return OpCode::_NONE;
			}
			}
		}

		OpCode BinaryOperatorTypeToInverseOpCode(IR::BinaryOperatorType opType)
		{
			switch (opType)
			{
			case IR::BinaryOperatorType::EQUAL_TEST:			return OpCode::JNE;
			case IR::BinaryOperatorType::NOT_EQUAL_TEST:		return OpCode::JEQ;
			case IR::BinaryOperatorType::GREATER_TEST:			return OpCode::JLE;
			case IR::BinaryOperatorType::GREATER_EQUAL_TEST:	return OpCode::JLT;
			case IR::BinaryOperatorType::LESS_TEST:				return OpCode::JGE;
			case IR::BinaryOperatorType::LESS_EQUAL_TEST:		return OpCode::JGT;
			default:
			{
				return OpCode::_NONE;
			}
			}
		}

		OpCode InverseOpCode(OpCode opCode)
		{
			switch (opCode)
			{
			case OpCode::JEQ: return OpCode::JNE;
			case OpCode::JNE: return OpCode::JEQ;
			case OpCode::JGT: return OpCode::JLE;
			case OpCode::JGE: return OpCode::JLT;
			case OpCode::JLT: return OpCode::JGE;
			case OpCode::JLE: return OpCode::JGT;
			default:
			{
				return OpCode::_NONE;
			}
			}
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
