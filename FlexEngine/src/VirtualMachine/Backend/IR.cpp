#include "stdafx.hpp"

#include "VirtualMachine/Backend/IR.hpp"

#include "VirtualMachine/Backend/VariableContainer.hpp"
#include "VirtualMachine/Backend/VMValue.hpp"
#include "VirtualMachine/Backend/VirtualMachine.hpp"
#include "VirtualMachine/Frontend/Parser.hpp"
#include "StringBuilder.hpp"

namespace flex
{
	namespace IR
	{
		void Block::AddAssignment(Assignment* assignment)

		{
			assignments.push_back(assignment);
		}

		void Block::RemovePredecessor(Block* predecessor)
		{
			for (auto iter = predecessors.begin(); iter != predecessors.end(); ++iter)
			{
				if (predecessor == *iter)
				{
					predecessors.erase(iter);
					break;
				}
			}
		}

		void Block::AddReturn(IR::Value* returnVal)
		{
			if (!Filled())
			{
				terminator = new Return(returnVal);
			}
		}

		void Block::AddYield(IR::Value* yieldVal)
		{
			if (!Filled())
			{
				terminator = new YieldReturn(yieldVal);
			}
		}

		void Block::AddBranch(const Block& target)
		{
			if (!Filled())
			{
				terminator = new Branch(target);
			}
		}

		void Block::AddCall(const std::string& target, const std::vector<IR::Value*>& arguments)
		{
			FLEX_UNUSED(target);
			FLEX_UNUSED(arguments);
			if (!Filled())
			{
				// TODO: Look up arg address
				// TODO: Args
				//Block targetBlock;
				//terminator = new Branch(targetBlock);
			}
		}

		void Block::SealBlock()
		{
		}

		void Block::AddConditionalBranch(IR::Value* condition, const Block& then, const Block& otherwise)
		{
			if (!Filled())
			{
				// TODO: Eliminate dead branches here

				ConditionalBranch* conditionalBranch = new ConditionalBranch(condition, then, otherwise);
				conditionalBranch->then.predecessors.emplace_back(this);
				conditionalBranch->otherwise.predecessors.emplace_back(this);
				terminator = conditionalBranch;
			}
		}

		std::string Block::ToString() const
		{
			StringBuilder builder;

			builder.AppendLine("{");

			for (auto iter = assignments.begin(); iter != assignments.end(); ++iter)
			{
				builder.AppendLine((*iter)->ToString());
			}
			if (terminator != nullptr)
			{
				builder.AppendLine(terminator->ToString());
			}
			else
			{
				builder.AppendLine("no terminator");
			}

			builder.AppendLine("}");

			//builder.Append(std::to_string(index));

			return builder.ToString();
		}

		void State::Clear()
		{
			diagnosticContainer->diagnostics.clear();
		}

		Block& State::InsertionBlock()
		{
			return insertionBlock;
		}

		void State::SetCurrentInstructionBlock(Block& block)
		{
			insertionBlock = block;
		}

		//BlockList& State::PushInstructionBlock()
		//{
		//	blockLists.push_back({});
		//	return blockLists[blockLists.size() - 1];
		//}

		//void State::PopInstructionBlock()
		//{
		//}

		std::string State::NextTemporary()
		{
			return "__tmp" + std::to_string(tempCount++);
		}

		void State::WriteVariableInBlock(const std::string& variable, IR::Value* value)
		{
			InsertionBlock().AddAssignment(new Assignment(variable, value));
		}

		std::string Assignment::ToString() const
		{
			StringBuilder builder;

			builder.Append(variable);
			builder.Append(" = ");
			builder.Append(value->ToString());

			return builder.ToString();
		}

		std::string Return::ToString() const
		{
			StringBuilder builder;

			builder.Append("return ");
			builder.Append(returnValue->ToString());

			return builder.ToString();
		}

		std::string YieldReturn::ToString() const
		{
			StringBuilder builder;

			builder.Append("yield ");
			builder.Append(yieldValue->ToString());

			return builder.ToString();
		}

		std::string Break::ToString() const
		{
			return "break";
		}

		std::string Branch::ToString() const
		{
			StringBuilder builder;

			builder.Append("branch ");
			builder.Append(target.ToString());

			return builder.ToString();
		}

		std::string ConditionalBranch::ToString() const
		{
			StringBuilder builder;

			builder.Append("if (");
			builder.Append(condition->ToString());
			builder.Append(") {");
			builder.Append(then.ToString());
			builder.Append("} else {");
			builder.Append(otherwise.ToString());
			builder.Append("}");

			return builder.ToString();
		}

		std::string UnaryValue::ToString() const
		{
			StringBuilder builder;

			builder.Append(UnaryOperatorTypeToString(opType));
			builder.Append(operand->ToString());

			return builder.ToString();
		}

		std::string BinaryValue::ToString() const
		{
			StringBuilder builder;

			builder.Append(left->ToString());
			builder.Append(" ");
			builder.Append(BinaryOperatorTypeToString(opType));
			builder.Append(" ");
			builder.Append(right->ToString());

			return builder.ToString();
		}

		std::string FunctionCallValue::ToString() const
		{
			StringBuilder builder;

			builder.Append(target);
			builder.Append("(");
			for (u32 i = 0; i < (u32)arguments.size(); ++i)
			{
				builder.Append(arguments[i]->ToString());
				if (i < (u32)arguments.size() - 1)
				{
					builder.Append(", ");
				}
			}
			builder.Append(")");

			return builder.ToString();
		}

		const char* UnaryOperatorTypeToString(UnaryOperatorType opType)
		{
			return g_UnaryOperatorTypeStrings[(u32)opType];
		}

		UnaryOperatorType IRUnaryOperatorTypeFromASTUnaryOperatorType(AST::UnaryOperatorType opType)
		{
			switch (opType)
			{
			case AST::UnaryOperatorType::NEGATE:		return IR::UnaryOperatorType::NEGATE;
			case AST::UnaryOperatorType::NOT:			return IR::UnaryOperatorType::NOT;
			case AST::UnaryOperatorType::BIN_INVERT:	return IR::UnaryOperatorType::BIN_INVERT;
			default:									return IR::UnaryOperatorType::_NONE;
			}
		}

		const char* BinaryOperatorTypeToString(BinaryOperatorType opType)
		{
			return g_BinaryOperatorTypeStrings[(u32)opType];
		}

		BinaryOperatorType IRBinaryOperatorTypeFromASTBinaryOperatorType(AST::BinaryOperatorType opType)
		{
			switch (opType)
			{
			case AST::BinaryOperatorType::ASSIGN:				return IR::BinaryOperatorType::ASSIGN;
			case AST::BinaryOperatorType::ADD:					return IR::BinaryOperatorType::ADD;
			case AST::BinaryOperatorType::SUB:					return IR::BinaryOperatorType::SUB;
			case AST::BinaryOperatorType::MUL:					return IR::BinaryOperatorType::MUL;
			case AST::BinaryOperatorType::DIV:					return IR::BinaryOperatorType::DIV;
			case AST::BinaryOperatorType::MOD:					return IR::BinaryOperatorType::MOD;
			case AST::BinaryOperatorType::BIN_AND:				return IR::BinaryOperatorType::BIN_AND;
			case AST::BinaryOperatorType::BIN_OR:				return IR::BinaryOperatorType::BIN_OR;
			case AST::BinaryOperatorType::BIN_XOR:				return IR::BinaryOperatorType::BIN_XOR;
			case AST::BinaryOperatorType::EQUAL_TEST:			return IR::BinaryOperatorType::EQUAL_TEST;
			case AST::BinaryOperatorType::NOT_EQUAL_TEST:		return IR::BinaryOperatorType::NOT_EQUAL_TEST;
			case AST::BinaryOperatorType::GREATER_TEST:			return IR::BinaryOperatorType::GREATER_TEST;
			case AST::BinaryOperatorType::GREATER_EQUAL_TEST:	return IR::BinaryOperatorType::GREATER_EQUAL_TEST;
			case AST::BinaryOperatorType::LESS_TEST:			return IR::BinaryOperatorType::LESS_TEST;
			case AST::BinaryOperatorType::LESS_EQUAL_TEST:		return IR::BinaryOperatorType::LESS_EQUAL_TEST;
			case AST::BinaryOperatorType::BOOLEAN_AND:			return IR::BinaryOperatorType::BOOLEAN_AND;
			case AST::BinaryOperatorType::BOOLEAN_OR:			return IR::BinaryOperatorType::BOOLEAN_OR;
			default:											return IR::BinaryOperatorType::_NONE;
			}
		}

	}

	void Generator::GenerateFromAST(AST::AST* ast)
	{
		if (state.diagnosticContainer == nullptr)
		{
			state.diagnosticContainer = new DiagnosticContainer();
		}
		state.Clear();

		std::string s = ast->rootBlock->ToString();
		Print("%s\n", s.c_str());
		//std::vector<Statement*> emptyList;
		//ast->rootBlock->RewriteCompoundStatements(ast->parser, emptyList);
		//VariableContainer varContainer;
		//ast->rootBlock->ResolveTypesAndLifetimes(&varContainer, ast->diagnosticContainer);
		s = ast->rootBlock->ToString();
		Print("\n---\n\n%s\n", s.c_str());

		for (const Diagnostic& diagnostic : ast->diagnosticContainer->diagnostics)
		{
			PrintError("L%u: %s\n", diagnostic.lineNumber, diagnostic.message.c_str());
		}

		state.Clear();

		IR::Block& firstBlock = state.insertionBlock;
		if (ast->diagnosticContainer->diagnostics.empty())
		{
			LowerStatement(ast->rootBlock);
		}

		s = firstBlock.ToString();
		Print("\n---\n\n%s\n", s.c_str());
	}

	void Generator::Destroy()
	{
		delete state.diagnosticContainer;
	}

	void Generator::LowerStatement(AST::Statement* statement)
	{
		if (IsExpression(statement->statementType))
		{
			state.WriteVariableInBlock(state.NextTemporary(), LowerExpression((AST::Expression*)statement));
		}

		if (IsLiteral(statement->statementType))
		{
			//ValueWrapper val1 = ValueWrapper(ValueWrapper::Type::CONSTANT, ((AST::Expression*)statement)->GetValue());
		}
		else
		{
			switch (statement->statementType)
			{
			case AST::StatementType::IDENTIFIER:
			{
				//AST::Identifier* identifier = (AST::Identifier*)statement;
				//ValueWrapper val1 = GetValueWrapperFromExpression(identifier);

				//state.InsertionBlock().AddAssignment(new Assignment(identifier->identifierStr, );
			} break;
			case AST::StatementType::FUNC_CALL:
			{
				AST::FunctionCall* funcCall = (AST::FunctionCall*)statement;

				std::vector<IR::Value*> args;
				for (u32 i = 0; i < (u32)funcCall->arguments.size(); ++i)
				{
					args.push_back(LowerExpression(funcCall->arguments[i]));
				}
				state.InsertionBlock().AddCall(funcCall->target, args);
			} break;
			case AST::StatementType::STATEMENT_BLOCK:
			{
				//state.PushInstructionBlock();
				AST::StatementBlock* statementBlock = (AST::StatementBlock*)statement;
				for (AST::Statement* innerStatement : statementBlock->statements)
				{
					LowerStatement(innerStatement);
				}
				//state.PopInstructionBlock();
			} break;
			case AST::StatementType::VARIABLE_DECL:
			{
				//Block& insertionBlock = state.InsertionBlock();

				AST::Declaration* decl = (AST::Declaration*)statement;

				state.InsertionBlock().AddAssignment(new IR::Assignment(decl->identifierStr, LowerExpression(decl->initializer)));
			} break;
			case AST::StatementType::BREAK:
			{
				IR::Block nextBlock;

				state.InsertionBlock().AddBranch(nextBlock);
				state.InsertionBlock().SealBlock();

				state.SetCurrentInstructionBlock(nextBlock);
			}break;
			case AST::StatementType::YIELD:
			{
				IR::Block nextBlock;

				AST::YieldStatement* yieldStatement = (AST::YieldStatement*)statement;

				state.InsertionBlock().AddYield(LowerExpression(yieldStatement->yieldValue));
				state.InsertionBlock().SealBlock();

				state.SetCurrentInstructionBlock(nextBlock);
			}break;
			case AST::StatementType::RETURN:
			{
				IR::Block nextBlock;

				AST::ReturnStatement* returnStatement = (AST::ReturnStatement*)statement;

				state.InsertionBlock().AddYield(LowerExpression(returnStatement->returnValue));
				state.InsertionBlock().SealBlock();

				state.SetCurrentInstructionBlock(nextBlock);
			}break;
			case AST::StatementType::UNARY_OPERATION:
			{
			}break;
			case AST::StatementType::BINARY_OPERATION:
			{
				/*
				AST::BinaryOperation* binOp = (AST::BinaryOperation*)statement;
				OpCode binaryOpTranslation = BinaryOperatorTypeToOpCode(binOp->operatorType);
				assert(binaryOpTranslation != OpCode::_NONE);

				ValueWrapper lhsWrapper = GetValueWrapperFromExpression(binOp->lhs);
				ValueWrapper rhsWrapper = GetValueWrapperFromExpression(binOp->rhs);

				if (binaryOpTranslation == OpCode::CMP)
				{
					Instruction binInst(binaryOpTranslation, lhsWrapper, rhsWrapper);
					state.InsertionBlock().PushBack(binInst);

					OpCode jumpCode = BinaryOpToJumpCode(binOp->operatorType);
					if (jumpCode != OpCode::_NONE)
					{
						i32 jumpAddress = 0;
						Instruction jumpInst(jumpCode, ValueWrapper(ValueWrapper::Type::CONSTANT, IR::Value(jumpAddress)));
						state.InsertionBlock().PushBack(jumpInst);
					}
					else
					{
						if (binOp->operatorType == BinaryOperatorType::BOOLEAN_AND)
						{

						}
						else if (binOp->operatorType == BinaryOperatorType::BOOLEAN_OR)
						{

						}
						else
						{
							assert(false);
						}
					}
				}
				else
				{
					Instruction binInst(binaryOpTranslation, val0, lhsWrapper, rhsWrapper);
					state.InsertionBlock().PushBack(binInst);
				}
				*/
			}break;
			case AST::StatementType::TERNARY_OPERATION:
			{
			}break;
			default:
			{
				std::string errorMsg = "Unhandled statement type in VM::GenerateStatementInstructions: %u\n" + std::to_string((i32)statement->statementType);
				state.diagnosticContainer->AddDiagnostic(Span(0, 0), 0, 0, errorMsg);
			} break;
			}
		}
	}

	IR::Value* Generator::LowerExpression(AST::Expression* expression)
	{
		if (IsLiteral(expression->statementType))
		{
			return new IR::Constant(IR::Value(expression->GetValue()));
		}

		switch (expression->statementType)
		{
		case AST::StatementType::ASSIGNMENT:
		{
			AST::Assignment* assignment = (AST::Assignment*)expression;
			return new IR::Assignment(assignment->lhs, LowerExpression(assignment->rhs));
		}
		//case StatementType::INDEX_OPERATION:
		//	return Assignment(NextTemporary(), expression->GetValue());
		//case StatementType::LIST_INITIALIZER:
		//	return Assignment(NextTemporary(), expression->GetValue());
		case AST::StatementType::UNARY_OPERATION:
		{
			AST::UnaryOperation* unaryOperation = (AST::UnaryOperation*)expression;
			IR::UnaryOperatorType irOpType = IR::UnaryOperatorType::_NONE;
			switch (unaryOperation->operatorType)
			{
			case AST::UnaryOperatorType::NEGATE:		irOpType = IR::UnaryOperatorType::NEGATE;
			case AST::UnaryOperatorType::NOT:			irOpType = IR::UnaryOperatorType::NOT;
			case AST::UnaryOperatorType::BIN_INVERT:	irOpType = IR::UnaryOperatorType::BIN_INVERT;
			}

			return new IR::UnaryValue(irOpType, LowerExpression(unaryOperation->expression));
		}
		case AST::StatementType::BINARY_OPERATION:
		{
			AST::BinaryOperation* binaryOperation = (AST::BinaryOperation*)expression;

			if (binaryOperation->operatorType == AST::BinaryOperatorType::ASSIGN)
			{
				AST::Identifier* lhs = (AST::Identifier*)binaryOperation->lhs;
				// TODO: Add to usages here
				return new IR::Assignment(lhs->identifierStr, LowerExpression(binaryOperation->rhs));
			}

			IR::BinaryOperatorType irOpType = IR::IRBinaryOperatorTypeFromASTBinaryOperatorType(binaryOperation->operatorType);
			IR::Value* lhsVal = LowerExpression(binaryOperation->lhs);
			IR::Value* rhsVal = LowerExpression(binaryOperation->rhs);

			if (IR::Value::IsLiteral(lhsVal->type) &&
				IR::Value::IsLiteral(rhsVal->type))
			{
				switch (irOpType)
				{
				case IR::BinaryOperatorType::ADD:					return new IR::Constant(*lhsVal + *rhsVal);
				case IR::BinaryOperatorType::SUB:					return new IR::Constant(*lhsVal - *rhsVal);
				case IR::BinaryOperatorType::MUL:					return new IR::Constant(*lhsVal * *rhsVal);
				case IR::BinaryOperatorType::DIV:					return new IR::Constant(*lhsVal / *rhsVal);
				case IR::BinaryOperatorType::MOD:					return new IR::Constant(*lhsVal % *rhsVal);
				case IR::BinaryOperatorType::BIN_AND:				return new IR::Constant(*lhsVal & *rhsVal);
				case IR::BinaryOperatorType::BIN_OR:				return new IR::Constant(*lhsVal | *rhsVal);
				case IR::BinaryOperatorType::BIN_XOR:				return new IR::Constant(*lhsVal ^ *rhsVal);
				case IR::BinaryOperatorType::EQUAL_TEST:			return new IR::Constant(IR::Value(*lhsVal == *rhsVal));
				case IR::BinaryOperatorType::NOT_EQUAL_TEST:		return new IR::Constant(IR::Value(*lhsVal != *rhsVal));
				case IR::BinaryOperatorType::GREATER_TEST:			return new IR::Constant(IR::Value(*lhsVal > *rhsVal));
				case IR::BinaryOperatorType::GREATER_EQUAL_TEST:	return new IR::Constant(IR::Value(*lhsVal >= *rhsVal));
				case IR::BinaryOperatorType::LESS_TEST:				return new IR::Constant(IR::Value(*lhsVal < *rhsVal));
				case IR::BinaryOperatorType::LESS_EQUAL_TEST:		return new IR::Constant(IR::Value(*lhsVal <= *rhsVal));
				case IR::BinaryOperatorType::BOOLEAN_AND:			return new IR::Constant(IR::Value(*lhsVal && *rhsVal));
				case IR::BinaryOperatorType::BOOLEAN_OR:			return new IR::Constant(IR::Value(*lhsVal || *rhsVal));
				default:
					assert(false);
					return new IR::Constant(IR::Value(-1));
				}
			}
			else
			{
				return new IR::BinaryValue(irOpType, lhsVal, rhsVal);
			}

		}
		case AST::StatementType::TERNARY_OPERATION:
		{
			AST::TernaryOperation* ternary = (AST::TernaryOperation*)expression;
			IR::Block ifTrueBlock(ternary->ifTrue->span);
			IR::Block ifFalseBlock(ternary->ifFalse->span);
			IR::Block mergeBlock(state.InsertionBlock().origin);

			state.InsertionBlock().AddConditionalBranch(LowerExpression(ternary->condition), ifTrueBlock, ifFalseBlock);

			state.SetCurrentInstructionBlock(ifTrueBlock);
			LowerStatement(ternary->ifTrue);
			state.InsertionBlock().AddBranch(mergeBlock);
			state.InsertionBlock().SealBlock();

			state.SetCurrentInstructionBlock(ifFalseBlock);
			LowerStatement(ternary->ifFalse);
			state.InsertionBlock().AddBranch(mergeBlock);
			state.InsertionBlock().SealBlock();

			state.SetCurrentInstructionBlock(mergeBlock);
		}
		case AST::StatementType::FUNC_CALL:
		{
			AST::FunctionCall* functionCall = (AST::FunctionCall*)expression;
			std::vector<IR::Value*> arguments;
			for (u32 i = 0; i < (u32)functionCall->arguments.size(); ++i)
			{
				arguments.push_back(LowerExpression(functionCall->arguments[i]));
			}
			return new IR::FunctionCallValue(functionCall->target, arguments);
		}
		}

		return new IR::Value(IR::Value::Type::_NONE);

	}

	/*
	void Generator::DiscoverFuncDeclarations(const std::vector<AST::Statement*>& statements)
	{
		for (u32 i = 0; i < (u32)statements.size(); ++i)
		{
			AST::Statement* statement = statements[i];

			switch (statement->statementType)
			{
			case StatementType::STATEMENT_BLOCK:
			{
				StatementBlock* statementBlock = (StatementBlock*)statement;
				DiscoverFuncDeclarations(statementBlock->statements);
			} break;
			case StatementType::FUNC_DECL:
			{
				FunctionDeclaration* funcDecl = (FunctionDeclaration*)statement;
				state.funcNameToBlockIndexTable.emplace(funcDecl->name, (i32)state.funcNameToBlockIndexTable.size());
			}break;
			}
		}
	}

	void Generator::GenerateFunctionInstructions(const std::vector<AST::Statement*>& statements)
	{
		for (u32 i = 0; i < (u32)statements.size(); ++i)
		{
			AST::Statement* statement = statements[i];

			switch (statement->statementType)
			{
			case StatementType::STATEMENT_BLOCK:
			{
				state.PushInstructionBlock();
				StatementBlock* statementBlock = (StatementBlock*)statement;
				GenerateFunctionInstructions(statementBlock->statements);
				state.PopInstructionBlock();
			} break;
			case StatementType::FUNC_DECL:
			{
				InstructionBlock& instrBlock = state.PushInstructionBlock();
				FunctionDeclaration* funcDecl = (FunctionDeclaration*)statement;

				for (u32 j = 0; j < (u32)funcDecl->arguments.size(); ++j)
				{
					Instruction popInst(OpCode::POP, ValueWrapper(ValueWrapper::Type::REGISTER, IR::Value((i32)j)), GetValueWrapperFromExpression(funcDecl->arguments[j]));
					instrBlock.PushBack(popInst);
				}

				//GenerateStatementInstructions(funcDecl->body->statements);
				state.PopInstructionBlock();
			}break;
			}
		}
	}
	*/

	/*
	ValueWrapper Generator::GetValueWrapperFromExpression(AST::Expression* expression)
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
			case AST::StatementType::IDENTIFIER:
			{
				//AST::Identifier* ident = (AST::Identifier*)expression;
				//i32 reg = state.varToRegisterMap[ident->identifierStr];
				//valWrapper = ValueWrapper(ValueWrapper::Type::REGISTER, IR::Value(reg));
			} break;
			case AST::StatementType::FUNC_CALL:
			{
				AST::FunctionCall* funcCall = (AST::FunctionCall*)expression;
				i32 registerStored = GenerateCallInstruction(funcCall);
				valWrapper = ValueWrapper(ValueWrapper::Type::REGISTER, IR::Value(registerStored));
			} break;
			default:
			{
				//GenerateStatementInstructions(expression);
			} break;
			}
		}

		return valWrapper;
	}
	*/

	i32 Generator::CombineInstructionIndex(i32 instructionBlockIndex, i32 instructionIndex)
	{
		u32 value = ((u32)instructionBlockIndex << 16) + ((u32)instructionIndex & 0xFFFF);
		assert((value >> 16) == (u32)instructionBlockIndex);
		assert((value & 0xFFFF) == (u32)instructionIndex);
		return static_cast<i32>(value);
	}

	void Generator::SplitInstructionIndex(i32 combined, i32& outInstructionBlockIndex, i32& outInstructionIndex)
	{
		u32 valueUnsigned = static_cast<u32>(combined);
		outInstructionBlockIndex = (i32)(valueUnsigned >> 16);
		outInstructionIndex = (i32)(valueUnsigned & 0xFFFF);
	}

	i32 Generator::GenerateCallInstruction(AST::FunctionCall* funcCallstate)
	{
		FLEX_UNUSED(funcCallstate);
		/*
		InstructionBlock& insertionBlock = state.CurrentInstructionBlock();

		// Temporary identifier for the function since we
		// don't know where it will be located in the end
		i32 funcUID = state.funcNameToBlockIndexTable[funcCall->target];

		// Push return IP
		i32 pushInstructionIndex = -1;
		{
			// Actual IP will be patched up below
			Instruction pushReturnIP(OpCode::PUSH, ValueWrapper(ValueWrapper::Type::CONSTANT, IR::Value(0)));
			insertionBlock.PushBack(pushReturnIP);
			pushInstructionIndex = (i32)insertionBlock.instructions.size();
		}

		// Push arguments in reverse order
		for (i32 i = (i32)funcCall->arguments.size() - 1; i >= 0; --i)
		{
			AST::Expression* arg = funcCall->arguments[i];

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
					i32 getRegister = state.varToRegisterMap[initializerIdent->identifierStr];
					argVal = ValueWrapper(ValueWrapper::Type::REGISTER, IR::Value(getRegister));
				}
				else if (arg->statementType == StatementType::FUNC_CALL)
				{
					FunctionCall* subFuncCall = (FunctionCall*)arg;
					i32 registerStored = GenerateCallInstruction(subFuncCall);
					argVal = ValueWrapper(ValueWrapper::Type::REGISTER, IR::Value(registerStored));
				}
			}

			Instruction pushArg(OpCode::PUSH, argVal);
			insertionBlock.PushBack(pushArg);
		}

		// Call
		Instruction inst(OpCode::CALL, ValueWrapper(ValueWrapper::Type::CONSTANT, IR::Value(funcUID)));
		insertionBlock.PushBack(inst);

		// Resume point
		{
			// Patch up push call to current instruction offset
			i32 instructionBlockIndex = (i32)state.instructionBlocks.size();
			i32 instructionIndex = (i32)insertionBlock.instructions.size();
			i32 value = CombineInstructionIndex(instructionBlockIndex, instructionIndex);
			insertionBlock.instructions[pushInstructionIndex].val0.value.valInt = value;
		}

		i32 returnValueRegister = 0;
		Instruction popReturnVal(OpCode::POP, ValueWrapper(ValueWrapper::Type::REGISTER, IR::Value(returnValueRegister)));
		insertionBlock.PushBack(popReturnVal);
		return returnValueRegister;
		*/
		return 0;
	}

} // namespace flex
