#include "stdafx.hpp"

#include "VirtualMachine/Backend/VirtualMachine.hpp"

#include "Helpers.hpp"
#include "Scene/Objects/Terminal.hpp"
#include "StringBuilder.hpp"
#include "VirtualMachine/Backend/IR.hpp"
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
			varRegisterMap.clear();
			funcNameToBlockIndexTable.clear();
			instructionBlocks.clear();
			instructions.clear();
			instructionOrigins.clear();
			labelOffsets.clear();

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

			state->instructions.push_back(inst);
			state->instructionOrigins.push_back(origin);
			++count;
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

		i32 State::NewLabel()
		{
			labelOffsets.push_back(-1);
			return (i32)labelOffsets.size() - 1;
		}

		void State::PlaceLabel(i32 label)
		{
			InstructionBlock& currentInstBlock = CurrentInstructionBlock();
			labelOffsets[label] = currentInstBlock.startOffset + (i32)currentInstBlock.count;
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
		}

		void VirtualMachine::GenerateFromSource(const char* source)
		{
			m_bCompiled = false;
			instructions.clear();
			instructionOrigins.clear();
			m_Code.clear();
			m_bDebugStringsBuilt = false;
			m_bASTValid = false;
			m_bIRValid = false;
			diagnosticContainer->diagnostics.clear();

			// (The AST, IR & bindings are reused by every compile, so their arenas' memory is too)
			if (m_AST == nullptr)
			{
				m_AST = new AST::AST(&m_ASTArena);
			}
			if (m_FunctionBindings == nullptr)
			{
				m_FunctionBindings = new FunctionBindings();
				m_FunctionBindings->RegisterBindings();
			}
			if (m_IR == nullptr)
			{
				m_IR = new IR::IntermediateRepresentation();
			}

			m_AST->Generate(source);
			if (m_AST->rootBlock != nullptr && m_AST->diagnosticContainer->diagnostics.empty())
			{
				m_bASTValid = true;

				m_IR->GenerateFromAST(m_AST, m_FunctionBindings);

				if (!m_IR->blocks.empty() && m_IR->state->diagnosticContainer->diagnostics.empty())
				{
					m_bIRValid = true;

					GenerateFromIR(m_IR, m_FunctionBindings);

					// (Codegen errors, e.g. invalid terminal output indices, must also stop the script from running)
					for (const Diagnostic& diagnostic : state->diagnosticContainer->diagnostics)
					{
						diagnosticContainer->diagnostics.push_back(diagnostic);
					}
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
					i32 terminalOutputVarIndex = GetTerminalOutputVar(identifier->variable);
					if (terminalOutputVarIndex != -1)
					{
						// (Terminal outputs aren't stored in registers)
						if (terminalOutputVarIndex >= Terminal::MAX_OUTPUT_COUNT)
						{
							irState->diagnosticContainer->AddDiagnostic(value->origin, "Used invalid terminal output var");
							terminalOutputVarIndex = 0;
						}
						valWrapper = VariantWrapper(VariantWrapper::Type::TERMINAL_OUTPUT, Variant(terminalOutputVarIndex));
					}
					else if (state->varRegisterMap.find(identifier->variable) == state->varRegisterMap.end())
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

			// Save the variable registers allocated so far which the callee may write: it can be the caller itself, or
			// share variable names with it (linking narrows the list down to the registers the callee really writes)
			const i32 savedRegisterCount = bFuncIsExternal ? 0 : (i32)state->varRegisterMap.size();
			if (savedRegisterCount > 0)
			{
				currentInstBlock.PushBack(Instruction(OpCode::SAVE, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(funcAddress)), VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(savedRegisterCount))), funcCallValue->origin, state);
			}

			// Push return IP
			// We don't jump to external functions, so a resume
			// point doesn't need to be pushed
			i32 resumePointPushInstructionIndex = -1;
			if (!bFuncIsExternal)
			{
				// Actual IP will be patched up below
				resumePointPushInstructionIndex = (i32)currentInstBlock.count;
				Instruction pushReturnIP(OpCode::PUSH, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(0)));
				currentInstBlock.PushBack(pushReturnIP, funcCallValue->origin, state);
			}

			// Push arguments in reverse order
			for (i32 i = (i32)funcCallValue->arguments.size() - 1; i >= 0; --i)
			{
				IR::Value* arg = funcCallValue->arguments[i];
				if (arg == nullptr)
				{
					irState->diagnosticContainer->AddDiagnostic(funcCallValue->origin, "Invalid argument");
					return -1;
				}

				VariantWrapper argVal;
				if (IR::Value::IsLiteral(arg->type) || arg->type == IR::Value::Type::IDENTIFIER)
				{
					argVal = GetValueWrapperFromIRValue(irState, arg);
				}
				else
				{
					// (IR lowering moves non-simple arguments into temporaries)
					irState->diagnosticContainer->AddDiagnostic(arg->origin, "Compiler error: unexpected argument type");
					return -1;
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
				i32 instructionIndex = (i32)currentInstBlock.count;
				i32 value = currentInstBlock.startOffset + instructionIndex;
				state->instructions[currentInstBlock.startOffset + resumePointPushInstructionIndex].val0.variant.valInt = value;
			}

			IR::Value::Type funcReturnType = irState->functionTypes[funcCallValue->target].returnType;

			// (Not a variable register, so restoring the saved registers below doesn't overwrite it)
			const i32 returnValueRegister = SCRATCH_REGISTER;
			if (funcReturnType != IR::Value::Type::VOID_)
			{
				Instruction popReturnVal(OpCode::POP, VariantWrapper(VariantWrapper::Type::REGISTER, Variant(returnValueRegister)));
				currentInstBlock.PushBack(popReturnVal, funcCallValue->origin, state);
			}

			if (savedRegisterCount > 0)
			{
				currentInstBlock.PushBack(Instruction(OpCode::RESTORE, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(funcAddress)), VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(savedRegisterCount))), funcCallValue->origin, state);
			}

			return funcReturnType != IR::Value::Type::VOID_ ? returnValueRegister : -1;
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
			}
			// fall through
			default:
				irState->diagnosticContainer->AddDiagnostic(irValue->origin, "Unexpected type in cast statement");
				return IR::Value::Type::_NONE;
			}
		}

		void VirtualMachine::HandleComparison(IR::IntermediateRepresentation* ir, IR::Value* condition, i32 ifTrueBlockIndex, i32 ifFalseBlockIndex, i32 nextBlockIndex)
		{
			InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();
			if (condition->type != IR::Value::Type::BINARY)
			{
				ir->state->diagnosticContainer->AddDiagnostic(condition->origin, "Unhandled conditional type");
				return;
			}

			IR::BinaryValue* binary = (IR::BinaryValue*)condition;
			OpCode opCode = BinaryOperatorTypeToOpCode(binary->opType);
			if (opCode == OpCode::_NONE)
			{
				ir->state->diagnosticContainer->AddDiagnostic(condition->origin, "Invalid comparison");
				return;
			}

			VariantWrapper lhsWrapper = GetValueWrapperFromIRValue(ir->state, binary->left);
			VariantWrapper rhsWrapper = GetValueWrapperFromIRValue(ir->state, binary->right);
			currentInstBlock.PushBack(Instruction(OpCode::CMP, lhsWrapper, rhsWrapper), binary->origin, state);

			// (Blocks are laid out in order, so a jump to the next block can be left out)
			if (ifFalseBlockIndex == nextBlockIndex)
			{
				currentInstBlock.PushBack(Instruction(opCode, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(ifTrueBlockIndex))), binary->origin, state);
			}
			else if (ifTrueBlockIndex == nextBlockIndex)
			{
				currentInstBlock.PushBack(Instruction(InverseOpCode(opCode), VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(ifFalseBlockIndex))), binary->origin, state);
			}
			else
			{
				currentInstBlock.PushBack(Instruction(opCode, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(ifTrueBlockIndex))), binary->origin, state);
				currentInstBlock.PushBack(Instruction(OpCode::JMP, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(ifFalseBlockIndex))), binary->origin, state);
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

			// One label per IR block (each gets exactly one instruction block)
			state->labelOffsets.resize(ir->blocks.size(), -1);

			u32 instructionIndex = 0;
			for (u32 i = 0; i < (u32)ir->blocks.size(); ++i)
			{
				IR::Block* block = ir->blocks[i];
				state->PushInstructionBlock();

				{
					InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();
					currentInstBlock.startOffset = instructionIndex;
				}
				state->PlaceLabel((i32)block->index);

				for (auto assignmentIter = block->assignments.begin(); assignmentIter != block->assignments.end(); ++assignmentIter)
				{
					IR::Assignment* assignment = *assignmentIter;
					VariantWrapper outputVal;
					i32 terminalOutputVarIndex = GetTerminalOutputVar(assignment->variable);
					if (terminalOutputVarIndex == -1 && state->varRegisterMap.find(assignment->variable) == state->varRegisterMap.end())
					{
						// (The last register is reserved, see SCRATCH_REGISTER)
						if ((i32)state->varRegisterMap.size() >= SCRATCH_REGISTER)
						{
							ir->state->diagnosticContainer->AddDiagnostic(assignment->origin, "Ran out of registers!");
							return;
						}
						state->varRegisterMap[assignment->variable] = (i32)state->varRegisterMap.size();
					}

					if (terminalOutputVarIndex != -1)
					{
						if (terminalOutputVarIndex >= Terminal::MAX_OUTPUT_COUNT)
						{
							std::string message = "Used invalid terminal output var, range is between 0 & " + IntToString(Terminal::MAX_OUTPUT_COUNT);
							state->diagnosticContainer->AddDiagnostic(assignment->origin, message);
							// (Keeps the wrapper in bounds if execution is attempted anyway)
							terminalOutputVarIndex = 0;
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
							// !a == a ^ true
							VariantWrapper val = GetValueWrapperFromIRValue(ir->state, unaryValue->operand);
							VariantWrapper trueVal(VariantWrapper::Type::CONSTANT, Variant(true));
							currentInstBlock.PushBack(Instruction(VM::OpCode::XOR, outputVal, val, trueVal), unaryValue->origin, state);
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
							// Comparison result as a value:
							//   cmp lhs rhs
							//   j<op> true
							//   mov out false
							//   jmp end
							// true:
							//   mov out true
							// end:
							OpCode jumpOpCode = BinaryOperatorTypeToOpCode(binaryValue->opType);
							if (jumpOpCode == OpCode::_NONE)
							{
								ir->state->diagnosticContainer->AddDiagnostic(binaryValue->origin, "Invalid comparison");
								return;
							}

							i32 trueLabel = state->NewLabel();
							i32 endLabel = state->NewLabel();
							VariantWrapper trueVal(VariantWrapper::Type::CONSTANT, Variant(true));
							VariantWrapper falseVal(VariantWrapper::Type::CONSTANT, Variant(false));
							currentInstBlock.PushBack(Instruction(OpCode::CMP, GetValueWrapperFromIRValue(ir->state, binaryValue->left), GetValueWrapperFromIRValue(ir->state, binaryValue->right)), binaryValue->origin, state);
							currentInstBlock.PushBack(Instruction(jumpOpCode, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(trueLabel))), binaryValue->origin, state);
							currentInstBlock.PushBack(Instruction(OpCode::MOV, outputVal, falseVal), binaryValue->origin, state);
							currentInstBlock.PushBack(Instruction(OpCode::JMP, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant(endLabel))), binaryValue->origin, state);
							state->PlaceLabel(trueLabel);
							currentInstBlock.PushBack(Instruction(OpCode::MOV, outputVal, trueVal), binaryValue->origin, state);
							state->PlaceLabel(endLabel);
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
						// (IR lowering turns ternaries into branches)
						ir->state->diagnosticContainer->AddDiagnostic(assignment->origin, "Compiler error: unexpected ternary");
						return;
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
								// (Still needs to be copied to the output)
								currentInstBlock.PushBack(Instruction(OpCode::MOV, outputVal, GetValueWrapperFromIRValue(ir->state, castValue->target)), castValue->origin, state);
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
								currentInstBlock.PushBack(Instruction(OpCode::MOV, outputVal, GetValueWrapperFromIRValue(ir->state, castValue->target)), castValue->origin, state);
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

						// (Not a variable register, so this doesn't clobber the caller's variables)
						VariantWrapper resumePoint(VariantWrapper::Type::REGISTER, Variant(SCRATCH_REGISTER));
						currentInstBlock.PushBack(Instruction(OpCode::POP, resumePoint), ret->origin, state);
						// Void returns push nothing (the caller only pops a return value of non-void functions)
						if (returnVal.Valid())
						{
							currentInstBlock.PushBack(Instruction(OpCode::PUSH, returnVal), ret->origin, state);
						}
						currentInstBlock.PushBack(Instruction(OpCode::JMP, resumePoint), ret->origin, state);
					} break;
					case IR::TerminatorType::YIELD:
					{
						InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();

						IR::YieldReturn* yield = (IR::YieldReturn*)block->terminator;
						currentInstBlock.PushBack(Instruction(OpCode::YIELD), yield->origin, state);
						if (yield->target != nullptr && yield->target->index != i + 1)
						{
							currentInstBlock.PushBack(Instruction(OpCode::JMP, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant((i32)yield->target->index))), yield->origin, state);
						}
					} break;
					case IR::TerminatorType::BRANCH:
					{
						InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();

						IR::Branch* branch = (IR::Branch*)block->terminator;
						// (Falls through to the next block)
						if (branch->target->index != i + 1)
						{
							currentInstBlock.PushBack(Instruction(OpCode::JMP, VariantWrapper(VariantWrapper::Type::CONSTANT, Variant((i32)branch->target->index))), branch->origin, state);
						}
					} break;
					case IR::TerminatorType::CONDITIONAL_BRANCH:
					{
						//InstructionBlock& currentInstBlock = state->CurrentInstructionBlock();

						IR::ConditionalBranch* conditional = (IR::ConditionalBranch*)block->terminator;

						i32 thenBlockIndex = conditional->then->index;
						// (IR lowering always gives conditional branches both targets)
						i32 otherwiseBlockIndex = conditional->otherwise != nullptr ? conditional->otherwise->index : conditional->then->index + 1;
						HandleComparison(ir, conditional->condition, thenBlockIndex, otherwiseBlockIndex, (i32)i + 1);

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
				instructionIndex += currentInstBlock.count;
			}

			LineTable lineTable;
			lineTable.Build(m_AST->lexer->sourceIter.source);

			// Turn instruction blocks into contiguous instruction list
			{
				instructions = state->instructions;
				instructionOrigins = state->instructionOrigins;
				for (Span& origin : instructionOrigins)
				{
					lineTable.Apply(origin);
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
				case OpCode::JZ:
				case OpCode::JNZ:
				case OpCode::JEQ:
				case OpCode::JNE:
				case OpCode::JGE:
				case OpCode::JGT:
				case OpCode::JLE:
				case OpCode::JLT:
				{
					// (Jumps to registers, i.e. returns, are already absolute)
					if (inst.val0.type != VariantWrapper::Type::CONSTANT)
					{
						break;
					}

					i32 label = inst.val0.variant.valInt;
					if (label >= 0 && label < (i32)state->labelOffsets.size())
					{
						i32 labelOffset = state->labelOffsets[label];
						if (labelOffset != -1)
						{
							inst.val0.variant.valInt = labelOffset;
						}
						else
						{
							state->diagnosticContainer->AddDiagnostic(Span(Span::Source::GENERATED), "Compiler error: Jump to invalid block offset (-1)");
						}
					}
					else
					{
						state->diagnosticContainer->AddDiagnostic(Span(Span::Source::GENERATED), "Invalid block index");
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
								state->diagnosticContainer->AddDiagnostic(Span(Span::Source::GENERATED), "Compiler error: Calling invalid function (block start offset = -1)");
							}
						}
						else
						{
							state->diagnosticContainer->AddDiagnostic(Span(Span::Source::GENERATED), "Invalid block index");
						}
					}
				} break;
				}
			}


			if (state->diagnosticContainer->diagnostics.empty() && ir->state->diagnosticContainer->diagnostics.empty())
			{
				std::vector<std::vector<u8>> savedRegisters;
				ComputeSavedRegisters(ir, savedRegisters);
				Link(&savedRegisters);
			}
		}

		void VirtualMachine::GenerateFromInstStream(const std::vector<Instruction>& inInstructions)
		{
			ClearRuntimeState();
			state->Clear();
			m_bDebugStringsBuilt = false;
			m_bASTValid = false;
			m_bIRValid = false;
			instructions = inInstructions;
			// TODO: require instructionOrigins as well
			instructionOrigins.clear();
			instructionOrigins.resize(instructions.size(), Span(Span::Source::GENERATED));
			m_bCompiled = Link(nullptr);
		}

#if defined(_MSC_VER)
#define VM_INLINE __forceinline
#else
#define VM_INLINE inline __attribute__((always_inline))
#endif

		namespace
		{
			// Linked op codes (ExecInstruction::op)
			enum ExecOp : u8
			{
				X_NOP,
				X_MOV,
				X_ADD,
				X_SUB,
				X_MUL,
				X_DIV,
				X_MOD,
				X_INV,
				X_AND,
				X_OR,
				X_XOR,
				X_ITF,
				X_FTI,
				X_CALL,
				X_CALL_EXTERNAL,
				X_PUSH,
				X_POP,
				X_POP_REGISTER,
				X_POP_DISCARD,
				X_CMP,
				X_JMP,
				X_JEQ,
				X_JNE,
				X_JLT,
				X_JLE,
				X_JGT,
				X_JGE,
				// A compare fused with the conditional jump after it (the jump also stays at the next index for
				// anything jumping to it)
				X_CMP_JEQ,
				X_CMP_JNE,
				X_CMP_JLT,
				X_CMP_JLE,
				X_CMP_JGT,
				X_CMP_JGE,
				X_YIELD,
				X_RETURN,
				X_HALT,
				X_SAVE,
				X_RESTORE,
				// Placed after the last instruction (execution running off the end reaches it)
				X_END,
			};

			ExecOp JumpExecOp(OpCode opCode)
			{
				switch (opCode)
				{
				case OpCode::JZ:
				case OpCode::JEQ: return X_JEQ;
				case OpCode::JNZ:
				case OpCode::JNE: return X_JNE;
				case OpCode::JLT: return X_JLT;
				case OpCode::JLE: return X_JLE;
				case OpCode::JGT: return X_JGT;
				case OpCode::JGE: return X_JGE;
				default: return X_JMP;
				}
			}

			// Whether an op code writes its first operand
			bool WritesFirstOperand(OpCode opCode)
			{
				switch (opCode)
				{
				case OpCode::MOV:
				case OpCode::ADD:
				case OpCode::SUB:
				case OpCode::MUL:
				case OpCode::DIV:
				case OpCode::MOD:
				case OpCode::INV:
				case OpCode::AND:
				case OpCode::OR:
				case OpCode::XOR:
				case OpCode::ITF:
				case OpCode::FTI:
				case OpCode::POP:
					return true;
				default:
					return false;
				}
			}

			// Assignment with Variant::operator='s semantics (a typed value keeps its type), without its call in the
			// common cases
			VM_INLINE void Assign(Variant* dst, const Variant& src)
			{
				if (dst->type == src.type || dst->type == Variant::Type::_NONE)
				{
					dst->type = src.type;
					dst->_largestField = src._largestField;
				}
				else
				{
					*dst = src; // (Reports the mismatch)
				}
			}

			VM_INLINE void AssignInt(Variant* dst, i32 value)
			{
				if (dst->type == Variant::Type::INT || dst->type == Variant::Type::_NONE)
				{
					dst->type = Variant::Type::INT;
					dst->_largestField = 0;
					dst->valInt = value;
				}
				else
				{
					*dst = Variant(value); // (Reports the mismatch)
				}
			}

			VM_INLINE void AssignFloat(Variant* dst, real value)
			{
				if (dst->type == Variant::Type::FLOAT || dst->type == Variant::Type::_NONE)
				{
					dst->type = Variant::Type::FLOAT;
					dst->_largestField = 0;
					dst->valFloat = value;
				}
				else
				{
					*dst = Variant(value); // (Reports the mismatch)
				}
			}

			VM_INLINE void AssignBool(Variant* dst, i32 value)
			{
				if (dst->type == Variant::Type::BOOL || dst->type == Variant::Type::_NONE)
				{
					dst->type = Variant::Type::BOOL;
					dst->_largestField = 0;
					dst->valBool = value;
				}
				else
				{
					*dst = Variant(value != 0); // (Reports the mismatch)
				}
			}

			// (Wraps around instead of overflowing like signed arithmetic would)
			VM_INLINE i32 WrapAdd(i32 a, i32 b) { return (i32)((u32)a + (u32)b); }
			VM_INLINE i32 WrapSub(i32 a, i32 b) { return (i32)((u32)a - (u32)b); }
			VM_INLINE i32 WrapMul(i32 a, i32 b) { return (i32)((u32)a * (u32)b); }
			// (Scripts can divide by zero, & INT_MIN / -1 would trap)
			VM_INLINE i32 SafeDiv(i32 a, i32 b) { return b == 0 ? 0 : (b == -1 ? WrapSub(0, a) : a / b); }
			VM_INLINE i32 SafeMod(i32 a, i32 b) { return (b == 0 || b == -1) ? 0 : a % b; }

			// Sets the flags like comparing via Variant subtraction does (zf: equal, sf: lhs > rhs)
			VM_INLINE void Compare(const Variant& lhs, const Variant& rhs, bool& zf, bool& sf)
			{
				if (lhs.type == rhs.type)
				{
					if (lhs.type == Variant::Type::INT)
					{
						zf = lhs.valInt == rhs.valInt;
						sf = lhs.valInt > rhs.valInt;
						return;
					}
					if (lhs.type == Variant::Type::FLOAT)
					{
						zf = lhs.valFloat == rhs.valFloat;
						sf = lhs.valFloat > rhs.valFloat;
						return;
					}
				}
				Variant diff = lhs - rhs;
				zf = diff.IsZero();
				sf = diff.IsPositive();
			}
		} // namespace

		bool VirtualMachine::ResolveOperand(VariantWrapper& wrapper, bool bWritten, i32 instructionIndex, Variant*& outPtr)
		{
			outPtr = nullptr;
			const Span origin = instructionIndex < (i32)instructionOrigins.size() ? instructionOrigins[instructionIndex] : Span(Span::Source::GENERATED);
			switch (wrapper.type)
			{
			case VariantWrapper::Type::REGISTER:
				if (wrapper.variant.valInt < 0 || wrapper.variant.valInt >= REGISTER_COUNT)
				{
					state->diagnosticContainer->AddDiagnostic(origin, "Compiler error: invalid register");
					return false;
				}
				outPtr = &registers[wrapper.variant.valInt];
				return true;
			case VariantWrapper::Type::TERMINAL_OUTPUT:
				if (wrapper.variant.valInt < 0 || wrapper.variant.valInt >= Terminal::MAX_OUTPUT_COUNT)
				{
					state->diagnosticContainer->AddDiagnostic(origin, "Compiler error: invalid terminal output");
					return false;
				}
				outPtr = &terminalOutputs[wrapper.variant.valInt];
				return true;
			case VariantWrapper::Type::CONSTANT:
				if (bWritten)
				{
					state->diagnosticContainer->AddDiagnostic(origin, "Compiler error: instruction writes to a constant");
					return false;
				}
				// (instructions isn't changed after linking, so this stays valid)
				outPtr = &wrapper.variant;
				return true;
			default:
				state->diagnosticContainer->AddDiagnostic(origin, "Compiler error: missing operand");
				return false;
			}
		}

		void VirtualMachine::ComputeSavedRegisters(IR::IntermediateRepresentation* ir, std::vector<std::vector<u8>>& outSavedRegisters)
		{
			outSavedRegisters.clear();
			outSavedRegisters.resize(instructions.size());

			// Each function is the contiguous run of blocks from the one carrying its name up to the next function
			struct Function
			{
				i32 blockIndex;
				i32 start;
				i32 end;
				u64 writes[2];
				std::vector<i32> callees;
			};
			static_assert(REGISTER_COUNT <= 128, "Register sets are two u64 bit masks");

			std::vector<Function> functions;
			for (u32 i = 0; i < (u32)ir->blocks.size() && i < (u32)state->instructionBlocks.size(); ++i)
			{
				if (!ir->blocks[i]->funcName.empty())
				{
					Function function = {};
					function.blockIndex = (i32)i;
					function.start = state->instructionBlocks[i].startOffset;
					functions.push_back(function);
				}
			}
			for (u32 f = 0; f < (u32)functions.size(); ++f)
			{
				functions[f].end = (f + 1 < (u32)functions.size()) ? functions[f + 1].start : (i32)instructions.size();
			}

			auto FindFunctionByStart = [&functions](i32 start) -> i32
			{
				for (u32 f = 0; f < (u32)functions.size(); ++f)
				{
					if (functions[f].start == start)
					{
						return (i32)f;
					}
				}
				return -1;
			};
			auto FindFunctionByBlock = [&functions](i32 blockIndex) -> i32
			{
				for (u32 f = 0; f < (u32)functions.size(); ++f)
				{
					if (functions[f].blockIndex == blockIndex)
					{
						return (i32)f;
					}
				}
				return -1;
			};

			for (Function& function : functions)
			{
				for (i32 i = function.start; i < function.end; ++i)
				{
					const Instruction& inst = instructions[i];
					if (WritesFirstOperand(inst.opCode) && inst.val0.type == VariantWrapper::Type::REGISTER)
					{
						u32 reg = (u32)inst.val0.variant.valInt;
						if (reg < (u32)REGISTER_COUNT)
						{
							function.writes[reg / 64] |= 1ull << (reg % 64);
						}
					}
					else if (inst.opCode == OpCode::CALL && inst.val0.type == VariantWrapper::Type::CONSTANT && !IsExternal(inst.val0.variant.valInt))
					{
						i32 callee = FindFunctionByStart(inst.val0.variant.valInt);
						if (callee != -1)
						{
							function.callees.push_back(callee);
						}
					}
				}
			}

			// Calls clobber everything their callees (transitively) write
			bool bChanged = true;
			while (bChanged)
			{
				bChanged = false;
				for (Function& function : functions)
				{
					for (i32 callee : function.callees)
					{
						for (u32 w = 0; w < 2; ++w)
						{
							u64 combined = function.writes[w] | functions[callee].writes[w];
							if (combined != function.writes[w])
							{
								function.writes[w] = combined;
								bChanged = true;
							}
						}
					}
				}
			}

			for (u32 i = 0; i < (u32)instructions.size(); ++i)
			{
				const Instruction& inst = instructions[i];
				if (inst.opCode != OpCode::SAVE && inst.opCode != OpCode::RESTORE)
				{
					continue;
				}

				i32 callee = FindFunctionByBlock(inst.val0.variant.valInt);
				i32 count = std::min(inst.val1.variant.valInt, (i32)REGISTER_COUNT);
				std::vector<u8>& saved = outSavedRegisters[i];
				for (i32 reg = 0; reg < count; ++reg)
				{
					// (Unknown callees save everything)
					if (callee == -1 || (functions[callee].writes[reg / 64] & (1ull << (reg % 64))) != 0)
					{
						saved.push_back((u8)reg);
					}
				}
			}
		}

		bool VirtualMachine::Link(const std::vector<std::vector<u8>>* savedRegisters)
		{
			m_Code.clear();
			m_SavedRegisterLists.clear();
			m_Code.resize(instructions.size());

			bool bSuccess = true;
			for (u32 i = 0; i < (u32)instructions.size(); ++i)
			{
				Instruction& inst = instructions[i];
				ExecInstruction& exec = m_Code[i];
				exec.b = nullptr;

				Variant* val0 = nullptr;
				Variant* val1 = nullptr;
				Variant* val2 = nullptr;
				auto Resolve = [&](VariantWrapper& wrapper, bool bWritten, Variant*& outPtr)
				{
					if (!ResolveOperand(wrapper, bWritten, (i32)i, outPtr))
					{
						bSuccess = false;
					}
				};

				switch (inst.opCode)
				{
				case OpCode::MOV:
				case OpCode::INV:
				case OpCode::ITF:
				case OpCode::FTI:
					Resolve(inst.val0, true, val0);
					Resolve(inst.val1, false, val1);
					exec.op = inst.opCode == OpCode::MOV ? X_MOV : inst.opCode == OpCode::INV ? X_INV : inst.opCode == OpCode::ITF ? X_ITF : X_FTI;
					exec.dst = val0;
					exec.a = val1;
					break;
				case OpCode::ADD:
				case OpCode::SUB:
				case OpCode::MUL:
				case OpCode::DIV:
				case OpCode::MOD:
				case OpCode::AND:
				case OpCode::OR:
				case OpCode::XOR:
				{
					Resolve(inst.val0, true, val0);
					Resolve(inst.val1, false, val1);
					Resolve(inst.val2, false, val2);
					static const ExecOp arithmeticOps[] = { X_ADD, X_SUB, X_MUL, X_DIV, X_MOD };
					static const ExecOp bitwiseOps[] = { X_AND, X_OR, X_XOR };
					exec.op = (inst.opCode >= OpCode::ADD && inst.opCode <= OpCode::MOD) ?
						arithmeticOps[(i32)inst.opCode - (i32)OpCode::ADD] :
						bitwiseOps[(i32)inst.opCode - (i32)OpCode::AND];
					exec.dst = val0;
					exec.a = val1;
					exec.b = val2;
				} break;
				case OpCode::CALL:
				{
					if (inst.val0.type != VariantWrapper::Type::CONSTANT)
					{
						state->diagnosticContainer->AddDiagnostic(instructionOrigins[i], "Compiler error: call to a non constant address");
						bSuccess = false;
						break;
					}
					FuncAddress funcAddress = inst.val0.variant.valInt;
					if (IsExternal(funcAddress))
					{
						IFunction* function = nullptr;
						if (m_FunctionBindings != nullptr)
						{
							auto iter = m_FunctionBindings->ExternalFuncTable.find(funcAddress);
							if (iter != m_FunctionBindings->ExternalFuncTable.end())
							{
								function = iter->second;
							}
						}
						// (Calls to unknown external functions do nothing)
						exec.op = function != nullptr ? X_CALL_EXTERNAL : X_NOP;
						exec.func = function;
						exec.count = function != nullptr ? (u16)function->argTypes.size() : 0;
						if (exec.count > MAX_EXTERNAL_ARG_COUNT)
						{
							state->diagnosticContainer->AddDiagnostic(instructionOrigins[i], "Too many arguments for an external function");
							bSuccess = false;
						}
					}
					else
					{
						exec.op = X_CALL;
						exec.target = TranslateLocalFuncAddress(funcAddress);
					}
				} break;
				case OpCode::PUSH:
					Resolve(inst.val0, false, val0);
					exec.op = X_PUSH;
					exec.a = val0;
					break;
				case OpCode::POP:
					if (!inst.val0.Valid())
					{
						exec.op = X_POP_DISCARD;
						break;
					}
					Resolve(inst.val0, true, val0);
					// Registers can hold any type
					exec.op = inst.val0.type == VariantWrapper::Type::REGISTER ? X_POP_REGISTER : X_POP;
					exec.dst = val0;
					break;
				case OpCode::CMP:
					Resolve(inst.val0, false, val0);
					Resolve(inst.val1, false, val1);
					exec.op = X_CMP;
					exec.a = val0;
					exec.b = val1;
					break;
				case OpCode::JMP:
				case OpCode::JZ:
				case OpCode::JNZ:
				case OpCode::JEQ:
				case OpCode::JNE:
				case OpCode::JLT:
				case OpCode::JLE:
				case OpCode::JGT:
				case OpCode::JGE:
					exec.op = JumpExecOp(inst.opCode);
					if (inst.val0.type == VariantWrapper::Type::CONSTANT)
					{
						exec.target = inst.val0.variant.valInt;
					}
					else
					{
						// (Returns jump to the address in a register)
						Resolve(inst.val0, false, val0);
						exec.target = -1;
						exec.a = val0;
					}
					break;
				case OpCode::YIELD:
					exec.op = X_YIELD;
					break;
				case OpCode::RETURN:
					exec.op = X_RETURN;
					if (inst.val0.Valid())
					{
						Resolve(inst.val0, false, val0);
						exec.a = val0;
					}
					break;
				case OpCode::HALT:
					exec.op = X_HALT;
					break;
				case OpCode::SAVE:
				case OpCode::RESTORE:
				{
					exec.op = inst.opCode == OpCode::SAVE ? X_SAVE : X_RESTORE;
					exec.target = (i32)m_SavedRegisterLists.size();
					if (savedRegisters != nullptr && i < (u32)savedRegisters->size())
					{
						const std::vector<u8>& saved = (*savedRegisters)[i];
						m_SavedRegisterLists.insert(m_SavedRegisterLists.end(), saved.begin(), saved.end());
					}
					else
					{
						i32 count = std::max(0, std::min(inst.val1.variant.valInt, (i32)REGISTER_COUNT));
						for (i32 reg = 0; reg < count; ++reg)
						{
							m_SavedRegisterLists.push_back((u8)reg);
						}
					}
					exec.count = (u16)((i32)m_SavedRegisterLists.size() - exec.target);
				} break;
				default:
				{
					std::string errorMsg = "Unhandled op code: " + std::string(OpCodeToString(inst.opCode));
					state->diagnosticContainer->AddDiagnostic(instructionOrigins[i], errorMsg);
					bSuccess = false;
				} break;
				}
			}

			// Jump & call targets (indices up to the end sentinel are valid)
			const i32 codeSize = (i32)m_Code.size();
			for (i32 i = 0; i < codeSize; ++i)
			{
				const ExecInstruction& exec = m_Code[i];
				const bool bJump = exec.op == X_CALL || (exec.op >= X_JMP && exec.op <= X_JGE);
				if (bJump && exec.target != -1 && (exec.target < 0 || exec.target > codeSize))
				{
					state->diagnosticContainer->AddDiagnostic(instructionOrigins[i], "Compiler error: jump to an invalid instruction");
					bSuccess = false;
				}
			}
			ExecInstruction end;
			end.op = X_END;
			end.b = nullptr;
			m_Code.push_back(end);

			// Fuse compares with the conditional jumps following them
			for (u32 i = 0; i + 1 < (u32)m_Code.size(); ++i)
			{
				ExecInstruction& exec = m_Code[i];
				const ExecInstruction& next = m_Code[i + 1];
				if (exec.op == X_CMP && next.op >= X_JEQ && next.op <= X_JGE && next.target >= 0)
				{
					exec.op = (u8)(X_CMP_JEQ + (next.op - X_JEQ));
					exec.target = next.target;
				}
			}

			if (!bSuccess)
			{
				m_Code.clear();
			}
			return bSuccess;
		}

		void VirtualMachine::Execute(bool bSingleStep /* = false */)
		{
			m_LastExecutedInstructionCount = 0;

			if (!m_bCompiled)
			{
				return;
			}

			if (m_Code.size() <= 1)
			{
				PrintError("Attempted to execute VM without any instructions present\n");
				return;
			}

			if (!bSingleStep)
			{
				// (A program paused by a yield resumes where it left off, a finished one restarts below)
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

			const ExecInstruction* code = m_Code.data();
			// (Excluding the end sentinel)
			const u32 codeSize = (u32)m_Code.size() - 1;
			const u8* savedRegisterLists = m_SavedRegisterLists.data();
			std::vector<Variant>& stackData = stack.m_Data;
			if (stackData.capacity() < 256)
			{
				stackData.reserve(256);
			}

			i32 ip = m_RunningState.instructionIdx;
			bool zf = m_RunningState.zf != 0;
			bool sf = m_RunningState.sf != 0;
			bool bTerminated = false;
			u64 executedCount = 0;

			auto Fail = [&](const char* message)
			{
				diagnosticContainer->AddDiagnostic(Span(Span::Source::GENERATED), message);
				bTerminated = true;
			};

			// Conditions of the conditional jumps (on the flags set by the last compare)
#define VM_JEQ (zf)
#define VM_JNE (!zf)
#define VM_JLT (!sf && !zf)
#define VM_JLE (!sf || zf)
#define VM_JGT (sf && !zf)
#define VM_JGE (sf || zf)
			// (Register targets are only known now: out of range ones go to the end sentinel, which reports it)
#define VM_JUMP_TARGET(exec) ((exec).target >= 0 ? (exec).target : ((u32)(exec).a->valInt <= codeSize ? (exec).a->valInt : (i32)codeSize))
#define VM_CONDITIONAL_JUMP(op, condition) \
			case op: \
				ip = (condition) ? VM_JUMP_TARGET(exec) : ip + 1; \
				break;
			// In single step mode fused compares only compare, so the jump is its own step
#define VM_FUSED_COMPARE_JUMP(op, condition) \
			case op: \
				Compare(*exec.a, *exec.b, zf, sf); \
				ip = bSingleStep ? ip + 1 : ((condition) ? exec.target : ip + 2); \
				break;

			if ((u32)ip > codeSize)
			{
				ip = (i32)codeSize;
			}

			// One instruction per step, otherwise up to the limit (going past it reports an error below)
			const u64 instructionBudget = bSingleStep ? 1 : (u64)MAX_INSTRUCTIONS_PER_EXECUTE + 1;
			bool bYielded = false;
			while (true)
			{
				++executedCount;
				const ExecInstruction& exec = code[ip];
				switch (exec.op)
				{
				case X_NOP:
					++ip;
					break;
				case X_MOV:
					Assign(exec.dst, *exec.a);
					++ip;
					break;
				case X_ADD:
				{
					const Variant& lhs = *exec.a;
					const Variant& rhs = *exec.b;
					if (lhs.type == rhs.type && lhs.type == Variant::Type::INT) AssignInt(exec.dst, WrapAdd(lhs.valInt, rhs.valInt));
					else if (lhs.type == rhs.type && lhs.type == Variant::Type::FLOAT) AssignFloat(exec.dst, lhs.valFloat + rhs.valFloat);
					else *exec.dst = lhs + rhs;
					++ip;
				} break;
				case X_SUB:
				{
					const Variant& lhs = *exec.a;
					const Variant& rhs = *exec.b;
					if (lhs.type == rhs.type && lhs.type == Variant::Type::INT) AssignInt(exec.dst, WrapSub(lhs.valInt, rhs.valInt));
					else if (lhs.type == rhs.type && lhs.type == Variant::Type::FLOAT) AssignFloat(exec.dst, lhs.valFloat - rhs.valFloat);
					else *exec.dst = lhs - rhs;
					++ip;
				} break;
				case X_MUL:
				{
					const Variant& lhs = *exec.a;
					const Variant& rhs = *exec.b;
					if (lhs.type == rhs.type && lhs.type == Variant::Type::INT) AssignInt(exec.dst, WrapMul(lhs.valInt, rhs.valInt));
					else if (lhs.type == rhs.type && lhs.type == Variant::Type::FLOAT) AssignFloat(exec.dst, lhs.valFloat * rhs.valFloat);
					else *exec.dst = lhs * rhs;
					++ip;
				} break;
				case X_DIV:
				{
					const Variant& lhs = *exec.a;
					const Variant& rhs = *exec.b;
					if (lhs.type == rhs.type && lhs.type == Variant::Type::INT) AssignInt(exec.dst, SafeDiv(lhs.valInt, rhs.valInt));
					else if (lhs.type == rhs.type && lhs.type == Variant::Type::FLOAT) AssignFloat(exec.dst, lhs.valFloat / rhs.valFloat);
					else *exec.dst = lhs / rhs;
					++ip;
				} break;
				case X_MOD:
				{
					const Variant& lhs = *exec.a;
					const Variant& rhs = *exec.b;
					if (lhs.type == rhs.type && lhs.type == Variant::Type::INT) AssignInt(exec.dst, SafeMod(lhs.valInt, rhs.valInt));
					else *exec.dst = lhs % rhs;
					++ip;
				} break;
				case X_INV:
				{
					const Variant& operand = *exec.a;
					if (operand.type == Variant::Type::INT)
					{
						exec.dst->type = Variant::Type::INT;
						exec.dst->valInt = ~operand.valInt;
						++ip;
					}
					else
					{
						diagnosticContainer->AddDiagnostic(instructionOrigins[ip], "Binary invert can only be applied to ints");
						bTerminated = true;
					}
				} break;
				case X_AND:
				{
					// (Also used on bools)
					const Variant& lhs = *exec.a;
					const Variant& rhs = *exec.b;
					if (lhs.type == rhs.type && lhs.type == Variant::Type::INT) AssignInt(exec.dst, lhs.valInt & rhs.valInt);
					else if (lhs.type == rhs.type && lhs.type == Variant::Type::BOOL) AssignBool(exec.dst, lhs.valBool & rhs.valBool);
					else *exec.dst = lhs & rhs;
					++ip;
				} break;
				case X_OR:
				{
					const Variant& lhs = *exec.a;
					const Variant& rhs = *exec.b;
					if (lhs.type == rhs.type && lhs.type == Variant::Type::INT) AssignInt(exec.dst, lhs.valInt | rhs.valInt);
					else if (lhs.type == rhs.type && lhs.type == Variant::Type::BOOL) AssignBool(exec.dst, lhs.valBool | rhs.valBool);
					else *exec.dst = lhs | rhs;
					++ip;
				} break;
				case X_XOR:
				{
					// (!a is a ^ true)
					const Variant& lhs = *exec.a;
					const Variant& rhs = *exec.b;
					if (lhs.type == rhs.type && lhs.type == Variant::Type::INT) AssignInt(exec.dst, lhs.valInt ^ rhs.valInt);
					else if (lhs.type == rhs.type && lhs.type == Variant::Type::BOOL) AssignBool(exec.dst, lhs.valBool != rhs.valBool ? 1 : 0);
					else *exec.dst = lhs ^ rhs;
					++ip;
				} break;
				case X_ITF:
				{
					real value = exec.a->AsFloat();
					exec.dst->type = Variant::Type::FLOAT;
					exec.dst->valFloat = value;
					++ip;
				} break;
				case X_FTI:
				{
					i32 value = exec.a->AsInt();
					exec.dst->type = Variant::Type::INT;
					exec.dst->valInt = value;
					++ip;
				} break;
				case X_CALL:
					ip = exec.target;
					break;
				case X_CALL_EXTERNAL:
				{
					const u32 argCount = exec.count;
					if ((u32)stackData.size() < argCount)
					{
						Fail("Mismatched stack pop (attempted to pop from empty stack)");
						break;
					}
					// Arguments were pushed in reverse order, so the first one is on top
					Variant args[MAX_EXTERNAL_ARG_COUNT];
					const u32 top = (u32)stackData.size();
					for (u32 i = 0; i < argCount; ++i)
					{
						args[i] = stackData[top - 1 - i];
					}
					stackData.resize(top - argCount);

					Variant returnVal = exec.func->Execute(args, (i32)argCount);
					if (exec.func->returnType != Variant::Type::VOID_)
					{
						stackData.push_back(returnVal);
					}
					++ip;
				} break;
				case X_PUSH:
					if (stackData.size() >= (size_t)MAX_STACK_HEIGHT)
					{
						std::string errorMessage = "Stack overflow (reached max value: " + IntToString(MAX_STACK_HEIGHT) + ")";
						Fail(errorMessage.c_str());
						break;
					}
					stackData.push_back(*exec.a);
					++ip;
					break;
				case X_POP:
				case X_POP_REGISTER:
				case X_POP_DISCARD:
					if (stackData.empty())
					{
						Fail("Mismatched stack pop (attempted to pop from empty stack)");
						break;
					}
					if (exec.op == X_POP_REGISTER)
					{
						// (Registers can hold any type)
						exec.dst->type = stackData.back().type;
						exec.dst->_largestField = stackData.back()._largestField;
					}
					else if (exec.op == X_POP)
					{
						Assign(exec.dst, stackData.back());
					}
					stackData.pop_back();
					++ip;
					break;
				case X_CMP:
					Compare(*exec.a, *exec.b, zf, sf);
					++ip;
					break;
				case X_JMP:
					ip = VM_JUMP_TARGET(exec);
					break;
				VM_CONDITIONAL_JUMP(X_JEQ, VM_JEQ)
				VM_CONDITIONAL_JUMP(X_JNE, VM_JNE)
				VM_CONDITIONAL_JUMP(X_JLT, VM_JLT)
				VM_CONDITIONAL_JUMP(X_JLE, VM_JLE)
				VM_CONDITIONAL_JUMP(X_JGT, VM_JGT)
				VM_CONDITIONAL_JUMP(X_JGE, VM_JGE)
				VM_FUSED_COMPARE_JUMP(X_CMP_JEQ, VM_JEQ)
				VM_FUSED_COMPARE_JUMP(X_CMP_JNE, VM_JNE)
				VM_FUSED_COMPARE_JUMP(X_CMP_JLT, VM_JLT)
				VM_FUSED_COMPARE_JUMP(X_CMP_JLE, VM_JLE)
				VM_FUSED_COMPARE_JUMP(X_CMP_JGT, VM_JGT)
				VM_FUSED_COMPARE_JUMP(X_CMP_JGE, VM_JGE)
				case X_YIELD:
					++ip;
					bYielded = true;
					break;
				case X_RETURN:
				{
					if (stackData.empty())
					{
						Fail("Mismatched stack pop (attempted to pop from empty stack)");
						break;
					}
					i32 resumePoint = stackData.back().valInt;
					stackData.pop_back();
					if (exec.a != nullptr)
					{
						stackData.push_back(*exec.a);
					}
					ip = (u32)resumePoint <= codeSize ? resumePoint : (i32)codeSize;
				} break;
				case X_END:
					// (e.g. a function whose last block has no terminator)
					Fail("Execution ran past the end of the program (missing return?)");
					break;
				case X_HALT:
					bTerminated = true;
					break;
				case X_SAVE:
				{
					const u32 count = exec.count;
					if (stackData.size() + count > (size_t)MAX_STACK_HEIGHT)
					{
						std::string errorMessage = "Stack overflow (reached max value: " + IntToString(MAX_STACK_HEIGHT) + ")";
						Fail(errorMessage.c_str());
						break;
					}
					const u8* regs = savedRegisterLists + exec.target;
					for (u32 i = 0; i < count; ++i)
					{
						stackData.push_back(registers[regs[i]]);
					}
					++ip;
				} break;
				case X_RESTORE:
				{
					const u32 count = exec.count;
					if ((u32)stackData.size() < count)
					{
						Fail("Mismatched stack pop (attempted to pop from empty stack)");
						break;
					}
					const u8* regs = savedRegisterLists + exec.target;
					for (i32 i = (i32)count - 1; i >= 0; --i)
					{
						// (Registers can hold any type)
						Variant& reg = registers[regs[i]];
						reg.type = stackData.back().type;
						reg._largestField = stackData.back()._largestField;
						stackData.pop_back();
					}
					++ip;
				} break;
				default:
					Fail("Unhandled op code in VirtualMachine::Execute");
					break;
				}

				if (bTerminated | bYielded | (executedCount >= instructionBudget))
				{
					break;
				}
			}

			if (!bSingleStep && !bTerminated && !bYielded && executedCount > MAX_INSTRUCTIONS_PER_EXECUTE)
			{
				Fail("Execution loop took too long, broke out early\n");
			}

#undef VM_JEQ
#undef VM_JNE
#undef VM_JLT
#undef VM_JLE
#undef VM_JGT
#undef VM_JGE
#undef VM_JUMP_TARGET
#undef VM_CONDITIONAL_JUMP
#undef VM_FUSED_COMPARE_JUMP

			m_LastExecutedInstructionCount = executedCount;
			m_RunningState.zf = zf ? 1 : 0;
			m_RunningState.sf = sf ? 1 : 0;
			m_RunningState.terminated = bTerminated;
			m_RunningState.instructionIdx = bTerminated ? -1 : ip;
		}

		u64 VirtualMachine::LastExecutedInstructionCount() const
		{
			return m_LastExecutedInstructionCount;
		}

		void VirtualMachine::BuildDebugStrings()
		{
			if (m_bDebugStringsBuilt)
			{
				return;
			}
			m_bDebugStringsBuilt = true;

			m_ASTStr = (m_bASTValid && m_AST != nullptr && m_AST->rootBlock != nullptr) ? m_AST->rootBlock->ToString() : "";
			m_IRStr = (m_bIRValid && m_IR != nullptr) ? m_IR->ToString() : "";

			StringBuilder strBuilder;
			for (u32 i = 0; i < (u32)state->instructionBlocks.size(); ++i)
			{
				const InstructionBlock& instBlock = state->instructionBlocks[i];
				strBuilder.Append(IntToString(i, 1, ' '));
				strBuilder.AppendLine(": {");
				for (u32 j = 0; j < instBlock.count; ++j)
				{
					const Instruction& inst = state->instructions[instBlock.startOffset + j];
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
			m_UnpatchedInstructionStr = strBuilder.ToString();

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
			m_InstructionStr = strBuilder.ToString();
		}

		const std::string& VirtualMachine::GetASTString()
		{
			BuildDebugStrings();
			return m_ASTStr;
		}

		const std::string& VirtualMachine::GetIRString()
		{
			BuildDebugStrings();
			return m_IRStr;
		}

		const std::string& VirtualMachine::GetUnpatchedInstructionString()
		{
			BuildDebugStrings();
			return m_UnpatchedInstructionStr;
		}

		const std::string& VirtualMachine::GetInstructionString()
		{
			BuildDebugStrings();
			return m_InstructionStr;
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
			stack.clear();
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
