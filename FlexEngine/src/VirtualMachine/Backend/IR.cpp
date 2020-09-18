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
		void Block::Destroy()
		{
			for (auto iter = assignments.begin(); iter != assignments.end(); ++iter)
			{
				Assignment* assignment = *iter;
				if (assignment != nullptr)
				{
					assignment->Destroy();
					delete assignment;
				}
			}
			assignments.clear();

			predecessors.clear();

			if (terminator != nullptr)
			{
				terminator->Destroy();
				delete terminator;
				terminator = nullptr;
			}
		}

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

		void Block::AddReturn(Span returnOrigin, IR::Value* returnVal)
		{
			if (!Filled())
			{
				terminator = new Return(returnVal, returnOrigin);
			}
		}

		void Block::AddYield(Span yieldOrigin, IR::Value* yieldVal)
		{
			if (!Filled())
			{
				terminator = new YieldReturn(yieldVal, yieldOrigin);
			}
		}

		void Block::AddBranch(Span branchOrigin, Block* target)
		{
			if (!Filled())
			{
				terminator = new Branch(target, branchOrigin);
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

		void Block::AddHalt()
		{
			if (!Filled())
			{
				terminator = new Halt();
			}
		}

		void Block::SealBlock()
		{
		}

		void Block::AddConditionalBranch(Span branchOrigin, IR::Value* condition, Block* then, Block* otherwise)
		{
			if (!Filled())
			{
				// TODO: Eliminate dead branches here

				ConditionalBranch* conditionalBranch = new ConditionalBranch(condition, then, otherwise, branchOrigin);
				conditionalBranch->then->predecessors.emplace_back(this);
				if (conditionalBranch->otherwise != nullptr)
				{
					conditionalBranch->otherwise->predecessors.emplace_back(this);
				}
				terminator = conditionalBranch;
			}
		}

		std::string Block::ToString() const
		{
			StringBuilder builder;

			builder.Append(std::to_string(index));
			builder.AppendLine(": {");

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

		State::State()
		{
			diagnosticContainer = new DiagnosticContainer();
			PushInstructionBlock(new Block(Span(0, 0)));
		}

		void State::Clear()
		{
			diagnosticContainer->diagnostics.clear();

			for (u32 i = 0; i < (u32)blocks.size(); ++i)
			{
				blocks[i]->Destroy();
				delete blocks[i];
			}
		}

		void State::Destroy()
		{
			delete diagnosticContainer;
		}

		void State::PushInstructionBlock(Block* block)
		{
			blocks.push_back(block);
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

		Value::Type State::GetValueType(Value const* value)
		{
			switch (value->type)
			{
			case Value::Type::IDENTIFIER:
			{
				Identifier* identifier = (Identifier*)value;
				return variableTypes[identifier->variable];
			}
			case Value::Type::BINARY:
			{
				BinaryValue* binary = (BinaryValue*)value;
				Value::Type leftType = GetValueType(binary->left);
				//Value::Type rightType = GetValueType(binary->right);
				return leftType;
			}
			case Value::Type::FUNC_CALL:
			{
				FunctionCallValue* funcCall = (FunctionCallValue*)value;
				auto iter = functionTypes.find(funcCall->target);
				if (iter != functionTypes.end())
				{
					return iter->second;
				}
				else
				{
					std::string str = "Undeclared function \"" + funcCall->target + "\"\n";
					diagnosticContainer->AddDiagnostic(funcCall->origin, str);
					return Value::Type::_NONE;
				}
			}
			case Value::Type::CAST:
			{
				CastValue* cast = (CastValue*)value;
				return cast->castedType;
			}
			default:
			{
			} break;
			}

			return value->type;
		}

		Block* State::InsertionBlock()
		{
			return blocks[blocks.size() - 1];
		}

		void State::WriteVariableInBlock(const std::string& variable, IR::Value* value)
		{
			if (value == nullptr)
			{
				return;
			}

			InsertionBlock()->AddAssignment(new Assignment(this, value->origin, variable, value));
			if (variableTypes.find(variable) == variableTypes.end())
			{
				variableTypes[variable] = GetValueType(value);
			}
		}

		void Assignment::Destroy()
		{
			delete value;
			value = nullptr;
		}

		std::string Assignment::ToString() const
		{
			StringBuilder builder;

			builder.Append(variable);
			builder.Append(" = ");
			builder.Append(value->ToString());

			return builder.ToString();
		}

		std::string Identifier::ToString() const
		{
			return variable;
		}

		std::string Halt::ToString() const
		{
			return "halt";
		}

		void Return::Destroy()
		{
			delete returnValue;
			returnValue = nullptr;
		}

		std::string Return::ToString() const
		{
			StringBuilder builder;

			builder.Append("return ");
			builder.Append(returnValue->ToString());

			return builder.ToString();
		}

		void YieldReturn::Destroy()
		{
			delete yieldValue;
			yieldValue = nullptr;
		}

		std::string YieldReturn::ToString() const
		{
			StringBuilder builder;

			builder.Append("yield ");
			builder.Append(yieldValue->ToString());

			return builder.ToString();
		}

		void Break::Destroy()
		{
			target = nullptr;
		}

		std::string Break::ToString() const
		{
			return "break";
		}

		void Branch::Destroy()
		{
			target = nullptr;
		}

		std::string Branch::ToString() const
		{
			StringBuilder builder;

			builder.Append("branch ");
			builder.Append(std::to_string(target->index));

			return builder.ToString();
		}

		void ConditionalBranch::Destroy()
		{
			delete condition;
			condition = nullptr;

			then = nullptr;

			if (otherwise != nullptr)
			{
				otherwise = nullptr;
			}
		}

		std::string ConditionalBranch::ToString() const
		{
			StringBuilder builder;

			builder.Append("if (");
			builder.Append(condition->ToString());
			builder.Append(")\n");
			//builder.Append(then->ToString());
			//if (otherwise != nullptr)
			//{
			//	builder.Append(" else \n");
			//	builder.Append(otherwise->ToString());
			//}
			//builder.Append("\n");

			return builder.ToString();
		}

		void UnaryValue::Destroy()
		{
			delete operand;
			operand = nullptr;
		}

		std::string UnaryValue::ToString() const
		{
			StringBuilder builder;

			builder.Append(UnaryOperatorTypeToString(opType));
			builder.Append(operand->ToString());

			return builder.ToString();
		}

		void BinaryValue::Destroy()
		{
			delete left;
			left = nullptr;
			delete right;
			right = nullptr;
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

		void FunctionCallValue::Destroy()
		{
			for (u32 i = 0; i < (u32)arguments.size(); ++i)
			{
				delete arguments[i];
			}
			arguments.clear();
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

		void CastValue::Destroy()
		{
			delete target;
		}

		std::string CastValue::ToString() const
		{
			StringBuilder builder;

			builder.Append("(");
			builder.Append(IR::Value::TypeToString(castedType));
			builder.Append(")");
			builder.Append(target->ToString());

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

		VM::OpCode OpCodeFromUnaryOperatorType(UnaryOperatorType opType)
		{
			switch (opType)
			{
			case UnaryOperatorType::NEGATE:		return VM::OpCode::SUB;
			case UnaryOperatorType::NOT:		return VM::OpCode::SUB;
			case UnaryOperatorType::BIN_INVERT: return VM::OpCode::INV;
			default:							return VM::OpCode::_NONE;
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

		VM::OpCode OpCodeFromBinaryOperatorType(BinaryOperatorType opType)
		{
			switch (opType)
			{
			case BinaryOperatorType::ASSIGN:				return VM::OpCode::MOV;
			case BinaryOperatorType::ADD:					return VM::OpCode::ADD;
			case BinaryOperatorType::SUB:					return VM::OpCode::SUB;
			case BinaryOperatorType::MUL:					return VM::OpCode::MUL;
			case BinaryOperatorType::DIV:					return VM::OpCode::DIV;
			case BinaryOperatorType::MOD:					return VM::OpCode::MOD;
			case BinaryOperatorType::BIN_AND:				return VM::OpCode::AND;
			case BinaryOperatorType::BIN_OR:				return VM::OpCode::OR;
			case BinaryOperatorType::BIN_XOR:				return VM::OpCode::XOR;
			case BinaryOperatorType::EQUAL_TEST:			return VM::OpCode::JEQ;
			case BinaryOperatorType::NOT_EQUAL_TEST:		return VM::OpCode::JNE;
			case BinaryOperatorType::GREATER_TEST:			return VM::OpCode::JGT;
			case BinaryOperatorType::GREATER_EQUAL_TEST:	return VM::OpCode::JGE;
			case BinaryOperatorType::LESS_TEST:				return VM::OpCode::JLT;
			case BinaryOperatorType::LESS_EQUAL_TEST:		return VM::OpCode::JLE;
			case BinaryOperatorType::BOOLEAN_AND:			return VM::OpCode::AND; // ?
			case BinaryOperatorType::BOOLEAN_OR:			return VM::OpCode::OR; // ?
			default:										return VM::OpCode::_NONE;
			}
		}

		void IntermediateRepresentation::GenerateFromAST(AST::AST* ast)
		{
			if (state)
			{
				state->Clear();
			}
			else
			{
				state = new State();
			}

			delete g_EmptyIRValue;
			g_EmptyIRValue = new Value(Span(0, 0), state, Value::Type::_NONE);

			//std::vector<Statement*> emptyList;
			//ast->rootBlock->RewriteCompoundStatements(ast->parser, emptyList);
			//VariableContainer varContainer;
			//ast->rootBlock->ResolveTypesAndLifetimes(&varContainer, ast->diagnosticContainer);

			//s = ast->rootBlock->ToString();
			//Print("\n---\n\n%s\n", s.c_str());
			//for (const Diagnostic& diagnostic : ast->diagnosticContainer->diagnostics)
			//{
			//	PrintError("L%u: %s\n", diagnostic.lineNumber, diagnostic.message.c_str());
			//}

			if (ast->diagnosticContainer->diagnostics.empty())
			{
				LowerStatement(ast->rootBlock);
				state->InsertionBlock()->AddHalt();
				blocks = state->blocks;
				SetBlockIndices();
			}

			//s = firstBlock->ToString();
			//Print("\n---\n\n%s\n", s.c_str());
		}

		void IntermediateRepresentation::Destroy()
		{
			state->Destroy();
			delete state;

			for (u32 i = 0; i < (u32)blocks.size(); ++i)
			{
				blocks[i]->Destroy();
				delete blocks[i];
			}
			blocks.clear();

			delete g_EmptyIRValue;
			g_EmptyIRValue = nullptr;
		}

		std::string IntermediateRepresentation::ToString() const
		{
			StringBuilder stringBuilder;

			for (u32 i = 0; i < (u32)blocks.size(); ++i)
			{
				stringBuilder.AppendLine(blocks[i]->ToString());
			}

			return stringBuilder.ToString();
		}

		void IntermediateRepresentation::LowerStatement(AST::Statement* statement)
		{
			if (IsExpression(statement->statementType))
			{
				state->WriteVariableInBlock(state->NextTemporary(), LowerExpression((AST::Expression*)statement));
			}

			if (IsLiteral(statement->statementType))
			{
				//ValueWrapper val1 = ValueWrapper(ValueWrapper::Type::CONSTANT, ((AST::Expression*)statement)->GetValue());
			}
			else
			{
				switch (statement->statementType)
				{
				case AST::StatementType::IF:
				{
					AST::IfStatement* ifStatement = (AST::IfStatement*)statement;

					Block* ifTrueBlock = new Block(ifStatement->then->span);
					Block* mergeBlock = new Block(state->InsertionBlock()->origin);
					Block* ifFalseBlock = ifStatement->otherwise == nullptr ? nullptr : new Block(ifStatement->otherwise->span);

					state->InsertionBlock()->AddConditionalBranch(ifStatement->span, LowerExpression(ifStatement->condition), ifTrueBlock, ifFalseBlock);

					state->PushInstructionBlock(ifTrueBlock);
					LowerStatement(ifStatement->then);
					state->InsertionBlock()->AddBranch(Span(Span::Source::GENERATED), mergeBlock);
					state->InsertionBlock()->SealBlock();

					if (ifStatement->otherwise != nullptr)
					{
						state->PushInstructionBlock(ifFalseBlock);
						LowerStatement(ifStatement->otherwise);
						state->InsertionBlock()->AddBranch(Span(Span::Source::GENERATED), mergeBlock);
						state->InsertionBlock()->SealBlock();
					}

					state->PushInstructionBlock(mergeBlock);
				} break;
				case AST::StatementType::ASSIGNMENT:
				{
					AST::Assignment* assignment = (AST::Assignment*)statement;

					IR::Value* rhs = LowerExpression(assignment->rhs);

					if (state->variableTypes.find(assignment->lhs) == state->variableTypes.end())
					{
						state->diagnosticContainer->AddDiagnostic(assignment->span, "Undeclared identifier \"" + assignment->lhs + "\"");
					}
					else
					{
						Value::Type lhsType = state->variableTypes[assignment->lhs];
						if (Value::TypeAssignable(state, lhsType, rhs))
						{
							assert(state->variableTypes[assignment->lhs] == state->GetValueType(rhs));
							state->InsertionBlock()->AddAssignment(new IR::Assignment(state, assignment->span, assignment->lhs, rhs));
						}
						else
						{
							StringBuilder diagnosticStr;
							diagnosticStr.Append("Mismatched types (");
							diagnosticStr.Append(IR::Value::TypeToString(state->GetValueType(rhs)));
							diagnosticStr.Append(" & ");
							diagnosticStr.Append(IR::Value::TypeToString(lhsType));
							diagnosticStr.Append(")");
							state->diagnosticContainer->AddDiagnostic(assignment->span, diagnosticStr.ToString());
						}
					}
				} break;
				case AST::StatementType::FUNC_DECL:
				{
					AST::FunctionDeclaration* funcDecl = (AST::FunctionDeclaration*)statement;

					IR::Block* bodyBlock = new IR::Block(funcDecl->span);
					IR::Block* mergeBlock = new IR::Block();

					state->PushInstructionBlock(bodyBlock);
					LowerStatement(funcDecl->body);
					state->InsertionBlock()->AddBranch(Span(Span::Source::GENERATED), mergeBlock);
					state->InsertionBlock()->SealBlock();

					AddFunctionType(funcDecl->span, funcDecl->name, Value::FromASTTypeName(funcDecl->returnType));

					state->PushInstructionBlock(mergeBlock);
				} break;
				case AST::StatementType::IDENTIFIER:
				{
					//AST::Identifier* identifier = (AST::Identifier*)statement;
					//ValueWrapper val1 = GetValueWrapperFromExpression(identifier);

					//state->InsertionBlock()->AddAssignment(new Assignment(identifier->identifierStr, );
				} break;
				case AST::StatementType::STATEMENT_BLOCK:
				{
					//state->PushInstructionBlock();
					AST::StatementBlock* statementBlock = (AST::StatementBlock*)statement;
					for (AST::Statement* innerStatement : statementBlock->statements)
					{
						LowerStatement(innerStatement);
					}
					//state->PopInstructionBlock();
				} break;
				case AST::StatementType::VARIABLE_DECL:
				{
					//Block& InsertionBlock() = state->InsertionBlock();

					AST::Declaration* decl = (AST::Declaration*)statement;

					IR::Value* initializerValue = LowerExpression(decl->initializer);

					if (IR::Value::TypeAssignable(state, IR::Value::FromASTTypeName(decl->typeName), initializerValue))
					{
						state->variableTypes[decl->identifierStr] = state->GetValueType(initializerValue);
						state->InsertionBlock()->AddAssignment(new IR::Assignment(state, decl->span, decl->identifierStr, initializerValue));
					}
					else
					{
						StringBuilder diagnosticStr;
						diagnosticStr.Append("Mismatched types (");
						diagnosticStr.Append(IR::Value::TypeToString(state->GetValueType(initializerValue)));
						diagnosticStr.Append(" & ");
						diagnosticStr.Append(IR::Value::TypeToString(IR::Value::FromASTTypeName(decl->typeName)));
						diagnosticStr.Append(")");
						state->diagnosticContainer->AddDiagnostic(decl->span, diagnosticStr.ToString());
					}

				} break;
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
						state->InsertionBlock()->PushBack(binInst);

						OpCode jumpCode = BinaryOpToJumpCode(binOp->operatorType);
						if (jumpCode != OpCode::_NONE)
						{
							i32 jumpAddress = 0;
							Instruction jumpInst(jumpCode, ValueWrapper(ValueWrapper::Type::CONSTANT, IR::Value(jumpAddress)));
							state->InsertionBlock()->PushBack(jumpInst);
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
						state->InsertionBlock()->PushBack(binInst);
					}
					*/
				}break;
				default:
				{
					//std::string errorMsg = "Unhandled statement type in VM::GenerateStatementInstructions: %u\n" + std::to_string((i32)statement->statementType);
					//state->diagnosticContainer->AddDiagnostic(Span(0, 0), errorMsg);
				} break;
				}
			}
		}

		IR::Value* IntermediateRepresentation::LowerExpression(AST::Expression* expression)
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
				return new IR::Assignment(state, assignment->span, assignment->lhs, LowerExpression(assignment->rhs));
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

				return new IR::UnaryValue(state, unaryOperation->span, irOpType, LowerExpression(unaryOperation->expression));
			}
			case AST::StatementType::BINARY_OPERATION:
			{
				AST::BinaryOperation* binaryOperation = (AST::BinaryOperation*)expression;

				if (binaryOperation->operatorType == AST::BinaryOperatorType::ASSIGN)
				{
					if (binaryOperation->lhs->statementType == AST::StatementType::IDENTIFIER)
					{
						AST::Identifier* lhs = (AST::Identifier*)binaryOperation->lhs;
						// TODO: Add to usages here
						return new IR::Assignment(state, lhs->span, lhs->identifierStr, LowerExpression(binaryOperation->rhs));
					}

					return nullptr;
				}

				IR::BinaryOperatorType irOpType = IR::IRBinaryOperatorTypeFromASTBinaryOperatorType(binaryOperation->operatorType);
				IR::Value* lhsVal = LowerExpression(binaryOperation->lhs);
				IR::Value* rhsVal = LowerExpression(binaryOperation->rhs);

				if (IR::Value::IsLiteral(lhsVal->type) &&
					IR::Value::IsLiteral(rhsVal->type))
				{
					IR::Value::Type resultType;
					if (IR::Value::TypesAreCoercible(state, lhsVal, rhsVal, resultType))
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
						case IR::BinaryOperatorType::EQUAL_TEST:
						{
							IR::Value val = IR::Value(lhsVal->origin.Extend(rhsVal->origin), state, *lhsVal == *rhsVal);
							return new IR::Constant(val);
						}
						case IR::BinaryOperatorType::NOT_EQUAL_TEST:		return new IR::Constant(IR::Value(lhsVal->origin.Extend(rhsVal->origin), state, *lhsVal != *rhsVal));
						case IR::BinaryOperatorType::GREATER_TEST:			return new IR::Constant(IR::Value(lhsVal->origin.Extend(rhsVal->origin), state, *lhsVal > * rhsVal));
						case IR::BinaryOperatorType::GREATER_EQUAL_TEST:	return new IR::Constant(IR::Value(lhsVal->origin.Extend(rhsVal->origin), state, *lhsVal >= *rhsVal));
						case IR::BinaryOperatorType::LESS_TEST:				return new IR::Constant(IR::Value(lhsVal->origin.Extend(rhsVal->origin), state, *lhsVal < *rhsVal));
						case IR::BinaryOperatorType::LESS_EQUAL_TEST:		return new IR::Constant(IR::Value(lhsVal->origin.Extend(rhsVal->origin), state, *lhsVal <= *rhsVal));
						case IR::BinaryOperatorType::BOOLEAN_AND:			return new IR::Constant(*lhsVal && *rhsVal);
						case IR::BinaryOperatorType::BOOLEAN_OR:			return new IR::Constant(*lhsVal || *rhsVal);
						default:
							assert(false);
							return new IR::Constant(IR::Value(lhsVal->origin.Extend(rhsVal->origin), state, -1));
						}
					}
					else
					{
						state->diagnosticContainer->AddDiagnostic(binaryOperation->span, "Types are not coercible");
					}
				}
				else
				{
					if (!IR::Value::IsLiteral(lhsVal->type) && lhsVal->type != IR::Value::Type::IDENTIFIER)
					{
						std::string lhsVar = state->NextTemporary();
						state->WriteVariableInBlock(lhsVar, lhsVal);
						lhsVal = new IR::Identifier(state, Span(Span::Source::GENERATED), lhsVar);
					}
					if (!IR::Value::IsLiteral(rhsVal->type) && rhsVal->type != IR::Value::Type::IDENTIFIER)
					{
						std::string rhsVar = state->NextTemporary();
						state->WriteVariableInBlock(rhsVar, rhsVal);
						rhsVal = new IR::Identifier(state, Span(Span::Source::GENERATED), rhsVar);
					}

					IR::Value::Type resultType;
					if (!IR::Value::TypesAreCoercible(state, lhsVal, rhsVal, resultType))
					{
						state->diagnosticContainer->AddDiagnostic(binaryOperation->span, "Types are not coercible");
						return nullptr;
					}

					// Add cast for implicit conversions
					Value::Type lhsType = state->GetValueType(lhsVal);
					Value::Type rhsType = state->GetValueType(rhsVal);
					if (lhsType != rhsType)
					{
						if (lhsType != resultType)
						{
							std::string lhsVar = state->NextTemporary();
							state->WriteVariableInBlock(lhsVar, new IR::CastValue(state, lhsVal->origin, resultType, lhsVal));
							lhsVal = new IR::Identifier(state, Span(Span::Source::GENERATED), lhsVar);
						}

						if (rhsType != resultType)
						{
							std::string rhsVar = state->NextTemporary();
							state->WriteVariableInBlock(rhsVar, new IR::CastValue(state, rhsVal->origin, resultType, rhsVal));
							rhsVal = new IR::Identifier(state, Span(Span::Source::GENERATED), rhsVar);
						}
					}

					return new IR::BinaryValue(state, lhsVal->origin.Extend(rhsVal->origin), irOpType, lhsVal, rhsVal);
				}
			}
			case AST::StatementType::TERNARY_OPERATION:
			{
				AST::TernaryOperation* ternary = (AST::TernaryOperation*)expression;
				IR::Block* ifTrueBlock = new IR::Block(ternary->ifTrue->span);
				IR::Block* ifFalseBlock = new IR::Block(ternary->ifFalse->span);
				IR::Block* mergeBlock = new IR::Block(state->InsertionBlock()->origin);

				state->InsertionBlock()->AddConditionalBranch(ternary->span, LowerExpression(ternary->condition), ifTrueBlock, ifFalseBlock);

				state->PushInstructionBlock(ifTrueBlock);
				LowerStatement(ternary->ifTrue);
				state->InsertionBlock()->AddBranch(Span(Span::Source::GENERATED), mergeBlock);
				state->InsertionBlock()->SealBlock();

				state->PushInstructionBlock(ifFalseBlock);
				LowerStatement(ternary->ifFalse);
				state->InsertionBlock()->AddBranch(Span(Span::Source::GENERATED), mergeBlock);
				state->InsertionBlock()->SealBlock();

				state->PushInstructionBlock(mergeBlock);

				return nullptr;
			}
			case AST::StatementType::FUNC_CALL:
			{
				AST::FunctionCall* functionCall = (AST::FunctionCall*)expression;
				std::vector<IR::Value*> arguments;
				for (u32 i = 0; i < (u32)functionCall->arguments.size(); ++i)
				{
					arguments.push_back(LowerExpression(functionCall->arguments[i]));
				}
				return new IR::FunctionCallValue(state, functionCall->span, functionCall->target, arguments);
			}
			case AST::StatementType::IDENTIFIER:
			{
				AST::Identifier* identifier = (AST::Identifier*)expression;
				return new IR::Identifier(state, identifier->span, identifier->identifierStr);
			} break;
			case AST::StatementType::CAST:
			{
				AST::Cast* cast = (AST::Cast*)expression;
				std::string tempIdent = state->NextTemporary();
				state->WriteVariableInBlock(tempIdent, LowerExpression(cast->target));
				return new IR::CastValue(state, cast->span, IR::Value::FromASTTypeName(cast->typeName), new IR::Identifier(state, cast->span, tempIdent));
			} break;
			case AST::StatementType::BREAK:
			{
				AST::BreakStatement* breakStatement = (AST::BreakStatement*)expression;

				IR::Block* nextBlock = new IR::Block(breakStatement->span);

				state->InsertionBlock()->AddBranch(breakStatement->span, nextBlock);
				state->InsertionBlock()->SealBlock();

				state->PushInstructionBlock(nextBlock);
				return nullptr;
			} break;
			case AST::StatementType::YIELD:
			{
				AST::YieldStatement* yieldStatement = (AST::YieldStatement*)expression;

				IR::Block* nextBlock = new IR::Block(yieldStatement->span);

				state->InsertionBlock()->AddYield(yieldStatement->span, LowerExpression(yieldStatement->yieldValue));
				state->InsertionBlock()->SealBlock();

				state->PushInstructionBlock(nextBlock);
				return nullptr;
			} break;
			case AST::StatementType::RETURN:
			{
				AST::ReturnStatement* returnStatement = (AST::ReturnStatement*)expression;

				IR::Block* nextBlock = new IR::Block(returnStatement->span);

				state->InsertionBlock()->AddReturn(returnStatement->span, LowerExpression(returnStatement->returnValue));
				state->InsertionBlock()->SealBlock();

				state->PushInstructionBlock(nextBlock);
				return nullptr;
			} break;
			}

			return new IR::Value(expression->span, state, IR::Value::Type::_NONE);
		}

		void IntermediateRepresentation::AddFunctionType(Span origin, const std::string& funcName, Value::Type returnType)
		{
			auto iter = state->functionTypes.find(funcName);
			if (iter != state->functionTypes.end())
			{
				state->diagnosticContainer->AddDiagnostic(origin, "Redeclaration of function \"" + funcName + "\"");
				return;
			}

			state->functionTypes[funcName] = returnType;
		}

		void IntermediateRepresentation::SetBlockIndices()
		{
			for (u32 i = 0; i < (u32)blocks.size(); ++i)
			{
				blocks[i]->index = i;
			}
		}

		/*
		void IntermediateRepresentation::DiscoverFuncDeclarations(const std::vector<AST::Statement*>& statements)
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
					state->funcNameToBlockIndexTable.emplace(funcDecl->name, (i32)state->funcNameToBlockIndexTable.size());
				}break;
				}
			}
		}

		void IntermediateRepresentation::GenerateFunctionInstructions(const std::vector<AST::Statement*>& statements)
		{
			for (u32 i = 0; i < (u32)statements.size(); ++i)
			{
				AST::Statement* statement = statements[i];

				switch (statement->statementType)
				{
				case StatementType::STATEMENT_BLOCK:
				{
					state->PushInstructionBlock();
					StatementBlock* statementBlock = (StatementBlock*)statement;
					GenerateFunctionInstructions(statementBlock->statements);
					state->PopInstructionBlock();
				} break;
				case StatementType::FUNC_DECL:
				{
					InstructionBlock& instrBlock = state->PushInstructionBlock();
					FunctionDeclaration* funcDecl = (FunctionDeclaration*)statement;

					for (u32 j = 0; j < (u32)funcDecl->arguments.size(); ++j)
					{
						Instruction popInst(OpCode::POP, ValueWrapper(ValueWrapper::Type::REGISTER, IR::Value((i32)j)), GetValueWrapperFromExpression(funcDecl->arguments[j]));
						instrBlock.PushBack(popInst);
					}

					//GenerateStatementInstructions(funcDecl->body->statements);
					state->PopInstructionBlock();
				}break;
				}
			}
		}
		*/

		/*
		ValueWrapper IntermediateRepresentation::GetValueWrapperFromExpression(AST::Expression* expression)
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
					//i32 reg = state->varToRegisterMap[ident->identifierStr];
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

		i32 IntermediateRepresentation::CombineInstructionIndex(i32 instructionBlockIndex, i32 instructionIndex)
		{
			u32 value = ((u32)instructionBlockIndex << 16) + ((u32)instructionIndex & 0xFFFF);
			assert((value >> 16) == (u32)instructionBlockIndex);
			assert((value & 0xFFFF) == (u32)instructionIndex);
			return static_cast<i32>(value);
		}

		void IntermediateRepresentation::SplitInstructionIndex(i32 combined, i32& outInstructionBlockIndex, i32& outInstructionIndex)
		{
			u32 valueUnsigned = static_cast<u32>(combined);
			outInstructionBlockIndex = (i32)(valueUnsigned >> 16);
			outInstructionIndex = (i32)(valueUnsigned & 0xFFFF);
		}

		i32 IntermediateRepresentation::GenerateCallInstruction(AST::FunctionCall* funcCallstate)
		{
			FLEX_UNUSED(funcCallstate);
			/*
			InstructionBlock& InsertionBlock() = state->CurrentInstructionBlock();

			// Temporary identifier for the function since we
			// don't know where it will be located in the end
			i32 funcUID = state->funcNameToBlockIndexTable[funcCall->target];

			// Push return IP
			i32 pushInstructionIndex = -1;
			{
				// Actual IP will be patched up below
				Instruction pushReturnIP(OpCode::PUSH, ValueWrapper(ValueWrapper::Type::CONSTANT, IR::Value(0)));
				InsertionBlock().PushBack(pushReturnIP);
				pushInstructionIndex = (i32)InsertionBlock().instructions.size();
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
						i32 getRegister = state->varToRegisterMap[initializerIdent->identifierStr];
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
				InsertionBlock().PushBack(pushArg);
			}

			// Call
			Instruction inst(OpCode::CALL, ValueWrapper(ValueWrapper::Type::CONSTANT, IR::Value(funcUID)));
			InsertionBlock().PushBack(inst);

			// Resume point
			{
				// Patch up push call to current instruction offset
				i32 instructionBlockIndex = (i32)state->instructionBlocks.size();
				i32 instructionIndex = (i32)InsertionBlock().instructions.size();
				i32 value = CombineInstructionIndex(instructionBlockIndex, instructionIndex);
				InsertionBlock().instructions[pushInstructionIndex].val0.value.valInt = value;
			}

			i32 returnValueRegister = 0;
			Instruction popReturnVal(OpCode::POP, ValueWrapper(ValueWrapper::Type::REGISTER, IR::Value(returnValueRegister)));
			InsertionBlock().PushBack(popReturnVal);
			return returnValueRegister;
			*/
			return 0;
		}
	} // namespace IR
} // namespace flex
