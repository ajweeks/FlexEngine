#include "stdafx.hpp"

#include "VirtualMachine/Backend/VirtualMachine.hpp"

#include "Helpers.hpp"
#include "VirtualMachine/Backend/VariableContainer.hpp"
#include "VirtualMachine/Diagnostics.hpp"
#include "VirtualMachine/Frontend/Parser.hpp"

namespace flex
{
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
		VariableContainer varContainer;
		ast->rootBlock->ResolveTypesAndLifetimes(&varContainer, ast->diagnosticContainer);
		s = ast->rootBlock->ToString();
		Print("\n---\n\n%s\n", s.c_str());

		for (const Diagnostic& diagnostic : ast->diagnosticContainer->diagnostics)
		{
			PrintError("L%u: %s\n", diagnostic.lineNumber, diagnostic.message.c_str());
		}

		if (ast->diagnosticContainer->diagnostics.empty())
		{
			ParseState parseState;

			// Discover all func declarations to fill out func address table UIDs
			for (u32 i = 0; i < (u32)ast->rootBlock->statements.size(); ++i)
			{
				Statement* statement = ast->rootBlock->statements[i];

				switch (statement->statementType)
				{
				case StatementType::FUNC_DECL:
				{
					FunctionDeclaration* funcDecl = (FunctionDeclaration*)statement;
					parseState.funcNameToUIDTable.emplace(funcDecl->name, (i32)parseState.funcNameToUIDTable.size());
				}break;
				}
			}

			GenerateInstructions(parseState, ast->rootBlock->statements);

			instructions.push_back({ OpCode::TERMINATE });

			// Generate function instructions (at end of inst stream)
			for (u32 i = 0; i < (u32)ast->rootBlock->statements.size(); ++i)
			{
				Statement* statement = ast->rootBlock->statements[i];

				switch (statement->statementType)
				{
				case StatementType::FUNC_DECL:
				{
					FunctionDeclaration* funcDecl = (FunctionDeclaration*)statement;
					i32 funcUID = parseState.funcNameToUIDTable[funcDecl->name];
					parseState.funcUIDToAddressTable[funcUID] = (i32)instructions.size();
					GenerateInstructions(parseState, funcDecl->body->statements);
				}break;
				}
			}

			// Patch up function calls to point at proper locations
			for (u32 i = 0; i < (u32)instructions.size(); ++i)
			{
				Instruction& inst = instructions[i];

				switch (inst.opCode)
				{
				case OpCode::CALL:
				{
					i32 funcAddress = parseState.funcUIDToAddressTable[inst.val0.Get(this).valInt];
					inst.val0.value.valInt = funcAddress;
				} break;
				}
			}

			for (const Instruction& inst : instructions)
			{
				std::string val0Str = inst.val0.ToString();
				std::string val1Str = inst.val1.ToString();
				std::string val2Str = inst.val2.ToString();
				Print("%s %s %s %s\n", OpCodeToString(inst.opCode), val0Str.c_str(), val1Str.c_str(), val2Str.c_str());
			}
		}
	}

	void VM::GenerateInstructions(ParseState& parseState, const std::vector<Statement*>& statements)
	{
		// Parse all other statements
		for (u32 i = 0; i < (u32)statements.size(); ++i)
		{
			Statement* statement = statements[i];

			switch (statement->statementType)
			{
			case StatementType::STATEMENT_BLOCK:
			{
				StatementBlock* statementBlock = (StatementBlock*)statement;
				GenerateInstructions(parseState, statementBlock->statements);
			} break;
			case StatementType::VARIABLE_DECL:
			{
				Declaration* decl = (Declaration*)statement;

				parseState.varToRegisterMap.emplace(decl->identifierStr, (i32)parseState.varToRegisterMap.size() + 1);

				i32 storeRegister = parseState.varToRegisterMap[decl->identifierStr];
				ValueWrapper val0 = ValueWrapper(ValueWrapper::Type::REGISTER, Value(storeRegister));

				if (IsLiteral(decl->initializer->statementType))
				{
					ValueWrapper val1 = ValueWrapper(ValueWrapper::Type::CONSTANT, decl->initializer->GetValue());

					Instruction inst(OpCode::MOV, val0, val1);
					instructions.push_back(inst);
				}
				else
				{
					if (decl->initializer->statementType == StatementType::IDENTIFIER)
					{
						ValueWrapper val1 = GetValueWrapperFromExpression(decl->initializer, parseState);

						Instruction inst(OpCode::MOV, val0, val1);
						instructions.push_back(inst);
					}
					else if (decl->initializer->statementType == StatementType::FUNC_CALL)
					{
						ValueWrapper val1 = GetValueWrapperFromExpression(decl->initializer, parseState);

						Instruction inst(OpCode::MOV, val0, val1);
						instructions.push_back(inst);
					}
					else if (decl->initializer->statementType == StatementType::BINARY_OPERATION)
					{
						BinaryOperation* binOp = (BinaryOperation*)decl->initializer;
						OpCode binaryOpTranslation = BinaryOperatorTypeToOpCode(binOp->operatorType);
						if (binaryOpTranslation != OpCode::_NONE)
						{
							ValueWrapper lhsWrapper = GetValueWrapperFromExpression(binOp->lhs, parseState);
							ValueWrapper rhsWrapper = GetValueWrapperFromExpression(binOp->rhs, parseState);

							Instruction binInst(binaryOpTranslation, val0, lhsWrapper, rhsWrapper);
							instructions.push_back(binInst);

							OpCode jumpCode = BinaryOpToJumpCode(binOp->operatorType);
							if (jumpCode != OpCode::_NONE)
							{
								Instruction jumpInst(jumpCode, lhsWrapper, rhsWrapper);
								instructions.push_back(jumpInst);
							}
						}
						else
						{
							int poppy = 0;
						}
					}
				}
			} break;
			case StatementType::RETURN:
			{
				ReturnStatement* returnStatement = (ReturnStatement*)statement;
				Instruction returnInst(OpCode::RETURN, GetValueWrapperFromExpression(returnStatement->returnValue, parseState));
				instructions.push_back(returnInst);
			}break;
			}
		}
	}

	ValueWrapper VM::GetValueWrapperFromExpression(Expression* expression, ParseState& parseState)
	{
		ValueWrapper valWrapper;

		if (IsLiteral(expression->statementType))
		{
			valWrapper = ValueWrapper(ValueWrapper::Type::CONSTANT, expression->GetValue());
		}
		else
		{
			switch (expression->statementType)
			{
			case StatementType::IDENTIFIER:
			{
				Identifier* ident = (Identifier*)expression;
				i32 reg = parseState.varToRegisterMap[ident->identifierStr];
				valWrapper = ValueWrapper(ValueWrapper::Type::REGISTER, Value(reg));
			} break;
			case StatementType::FUNC_CALL:
			{
				FunctionCall* funcCall = (FunctionCall*)expression;
				i32 registerStored = GenerateCallInstruction(funcCall, parseState);
				valWrapper = ValueWrapper(ValueWrapper::Type::REGISTER, Value(registerStored));
			} break;
			}
		}

		return valWrapper;
	}

	i32 VM::GenerateCallInstruction(FunctionCall* funcCall, ParseState& parseState)
	{
		// Temporary identifier for the function since we
		// don't know where it will be located in the end
		i32 funcUID = parseState.funcNameToUIDTable[funcCall->target];

		// Push return IP (two past current IP)
		Instruction pushReturnIP(OpCode::PUSH, ValueWrapper(ValueWrapper::Type::CONSTANT, Value((i32)instructions.size() + 2)));
		instructions.push_back(pushReturnIP);

		// Push arguments
		for (Expression* arg : funcCall->arguments)
		{
			ValueWrapper argVal;
			if (IsLiteral(arg->statementType))
			{
				argVal = ValueWrapper(ValueWrapper::Type::CONSTANT, arg->GetValue());
			}
			else
			{
				if (arg->statementType == StatementType::IDENTIFIER)
				{
					Identifier* initializerIdent = (Identifier*)arg;
					i32 getRegister = parseState.varToRegisterMap[initializerIdent->identifierStr];
					argVal = ValueWrapper(ValueWrapper::Type::REGISTER, Value(getRegister));
				}
				else if (arg->statementType == StatementType::FUNC_CALL)
				{
					FunctionCall* subFuncCall = (FunctionCall*)arg;
					i32 registerStored = GenerateCallInstruction(subFuncCall, parseState);
					argVal = ValueWrapper(ValueWrapper::Type::REGISTER, Value(registerStored));
				}
			}

			Instruction pushArg(OpCode::PUSH, argVal);
			instructions.push_back(pushArg);
		}

		// Call
		Instruction inst(OpCode::CALL, ValueWrapper(ValueWrapper::Type::CONSTANT, Value(funcUID)));
		instructions.push_back(inst);

		// Resume point
		i32 returnValueRegister = 0;
		Instruction popReturnVal(OpCode::POP, ValueWrapper(ValueWrapper::Type::REGISTER, Value(returnValueRegister)));
		instructions.push_back(popReturnVal);

		return returnValueRegister;
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
				if (sf && !zf)
				{
					instructionIdx = inst.val0.Get(this).valInt;
				}
				break;
			case OpCode::JLE:
				if (sf || zf)
				{
					instructionIdx = inst.val0.Get(this).valInt;
				}
				break;
			case OpCode::JGT:
				if (!sf && !zf)
				{
					instructionIdx = inst.val0.Get(this).valInt;
				}
				break;
			case OpCode::JGE:
				if (!sf || zf)
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

	const char* OpCodeToString(OpCode opCode)
	{
		return s_OpCodeStrings[(u32)opCode];
	}

	OpCode BinaryOperatorTypeToOpCode(BinaryOperatorType operatorType)
	{
		switch (operatorType)
		{
		case BinaryOperatorType::ADD:					return OpCode::ADD;
		case BinaryOperatorType::SUB:					return OpCode::SUB;
		case BinaryOperatorType::MUL:					return OpCode::MUL;
		case BinaryOperatorType::DIV:					return OpCode::DIV;
		case BinaryOperatorType::MOD:					return OpCode::MOD;
		case BinaryOperatorType::BIN_AND:				return OpCode::AND;
		case BinaryOperatorType::BIN_OR:				return OpCode::OR;
		case BinaryOperatorType::BIN_XOR:				return OpCode::XOR;
		case BinaryOperatorType::EQUAL_TEST:			return OpCode::CMP;
		case BinaryOperatorType::NOT_EQUAL_TEST:		return OpCode::CMP;
		case BinaryOperatorType::GREATER_TEST:			return OpCode::CMP;
		case BinaryOperatorType::GREATER_EQUAL_TEST:	return OpCode::CMP;
		case BinaryOperatorType::LESS_TEST:				return OpCode::CMP;
		case BinaryOperatorType::LESS_EQUAL_TEST:		return OpCode::CMP;
		case BinaryOperatorType::BOOLEAN_AND:			return OpCode::CMP;
		case BinaryOperatorType::BOOLEAN_OR:			return OpCode::CMP;
		default:										return OpCode::_NONE;
		}
	}

	OpCode BinaryOpToJumpCode(BinaryOperatorType operatorType)
	{
		switch (operatorType)
		{
		case BinaryOperatorType::EQUAL_TEST:			return OpCode::JEQ;
		case BinaryOperatorType::NOT_EQUAL_TEST:		return OpCode::JNE;
		case BinaryOperatorType::GREATER_TEST:			return OpCode::JGE;
		case BinaryOperatorType::GREATER_EQUAL_TEST:	return OpCode::JGT;
		case BinaryOperatorType::LESS_TEST:				return OpCode::JLT;
		case BinaryOperatorType::LESS_EQUAL_TEST:		return OpCode::JLE;
		default:										return OpCode::_NONE;
		}
	}

} // namespace flex
