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
		Block::Block(State* state) :
			origin(0, 0)
		{
			index = (u32)state->blocks.size();
		}

		Block::Block(State* state, Span origin) :
			origin(origin)
		{
			index = (u32)state->blocks.size();
		}

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

		void Block::AddCall(State* state, const std::string& target, const std::vector<IR::Value*>& arguments)
		{
			FLEX_UNUSED(state);
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

		void Block::AddConditionalBranch(State* state, Span branchOrigin, IR::Value* condition, Block* then, Block* otherwise)
		{
			if (!Filled())
			{
				// TODO: Eliminate dead branches here

				if (condition == nullptr || then == nullptr)
				{
					state->diagnosticContainer->AddDiagnostic(branchOrigin, "Invalid conditional branch");
				}
				else
				{
					ConditionalBranch* conditionalBranch = new ConditionalBranch(condition, then, otherwise, branchOrigin);
					conditionalBranch->then->predecessors.emplace_back(this);
					if (conditionalBranch->otherwise != nullptr)
					{
						conditionalBranch->otherwise->predecessors.emplace_back(this);
					}
					terminator = conditionalBranch;
				}
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
			PushInstructionBlock(new Block(this, Span(0, 0)));
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
			if (value == nullptr)
			{
				return Value::Type::_NONE;
			}

			switch (value->type)
			{
			case Value::Type::IDENTIFIER:
			{
				Identifier* identifier = (Identifier*)value;
				auto iter = variableTypes.find(identifier->variable);
				if (iter != variableTypes.end())
				{
					return variableTypes[identifier->variable];
				}
				std::string str = "Undeclared identifier \"" + identifier->variable + "\"\n";
				diagnosticContainer->AddDiagnostic(identifier->origin, str);
				return Value::Type::_NONE;
			}
			case Value::Type::UNARY:
			{
				UnaryValue* unary = (UnaryValue*)value;
				switch (unary->opType)
				{
				case UnaryOperatorType::NOT:
					if (unary->operand->type == Value::Type::BOOL)
					{
						return Value::Type::BOOL;
					}
					diagnosticContainer->AddDiagnostic(unary->origin, "Not operator can only be applied to boolean types");
					return Value::Type::_NONE;
				case UnaryOperatorType::NEGATE:
					if (!IR::Value::IsLiteral(unary->operand->type) || IR::Value::IsNumeric(unary->operand->type))
					{
						return GetValueType(unary->operand);
					}
					diagnosticContainer->AddDiagnostic(unary->origin, "Negate operator can only be applied to numeric types");
					return Value::Type::_NONE;
				case UnaryOperatorType::BIN_INVERT:
					if (unary->operand->type == Value::Type::INT)
					{
						return Value::Type::INT;
					}
					diagnosticContainer->AddDiagnostic(unary->origin, "Binary invert operator can only be applied to integral types");
					return Value::Type::_NONE;
				default:
					diagnosticContainer->AddDiagnostic(unary->origin, "Unhandled binary operator type");
					return Value::Type::_NONE;
				}
			}
			case Value::Type::BINARY:
			{
				BinaryValue* binary = (BinaryValue*)value;
				Value::Type leftType = GetValueType(binary->left);
				Value::Type rightType = GetValueType(binary->right);
				if (leftType != rightType)
				{
					diagnosticContainer->AddDiagnostic(binary->origin, "Mismatched types in binary operation");
					return Value::Type::_NONE;
				}
				if (IsComparisonOp(binary->opType))
				{
					return Value::Type::BOOL;
				}
				return leftType;
			}
			case Value::Type::TERNARY:
			{
				TernaryValue* ternary = (TernaryValue*)value;
				Value::Type ifTrueType = GetValueType(ternary->ifTrue);
				Value::Type ifFalseType = GetValueType(ternary->ifFalse);
				if (ifTrueType != ifFalseType)
				{
					diagnosticContainer->AddDiagnostic(ternary->origin, "Mismatched types in ternary operation");
					return Value::Type::_NONE;
				}
				return ifTrueType;
			}
			case Value::Type::FUNC_CALL:
			{
				FunctionCallValue* funcCall = (FunctionCallValue*)value;
				auto iter = functionTypes.find(funcCall->target);
				if (iter != functionTypes.end())
				{
					return iter->second.returnType;
				}

				std::string str = "Undeclared function \"" + funcCall->target + "\"\n";
				diagnosticContainer->AddDiagnostic(funcCall->origin, str);
				return Value::Type::_NONE;
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

			if (functionTypes.find(variable) != functionTypes.end())
			{
				diagnosticContainer->AddDiagnostic(value->origin, "Redefinition of name \"" + variable + "\"");
				return;
			}

			InsertionBlock()->AddAssignment(new Assignment(this, value->origin, variable, value));
			auto iter = variableTypes.find(variable);
			if (iter == variableTypes.end())
			{
				variableTypes[variable] = GetValueType(value);
			}
			else
			{
				if (iter->second != value->type)
				{
					std::string message = "Type mismatch (" + std::string(Value::TypeToString(value->type)) + " vs. " + std::string(Value::TypeToString(iter->second)) + ")";
					diagnosticContainer->AddDiagnostic(value->origin, message);
				}
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

			if (value->type == Value::Type::ARGUMENT)
			{
				const char* typeStr = IR::Value::TypeToString(value->type);
				builder.Append(typeStr);
				builder.Append(" ");
				builder.Append(variable);
				builder.Append(" (arg)");
			}
			else
			{
				builder.Append(variable);
				builder.Append(" = ");
				builder.Append(value->ToString());
			}

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
			if (returnValue != nullptr)
			{
				builder.Append(returnValue->ToString());
			}

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
			builder.Append(") ");
			builder.Append(std::to_string(then->index));
			if (otherwise != nullptr)
			{
				builder.Append(" else ");
				builder.Append(std::to_string(otherwise->index));
			}

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

		void TernaryValue::Destroy()
		{
			delete condition;
			condition = nullptr;
			delete ifTrue;
			ifTrue = nullptr;
			delete ifFalse;
			ifFalse = nullptr;
		}

		std::string TernaryValue::ToString() const
		{
			StringBuilder builder;

			builder.Append(condition->ToString());
			builder.Append(" ? ");
			builder.Append(ifTrue->ToString());
			builder.Append(" : ");
			builder.Append(ifFalse->ToString());

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

		bool IsComparisonOp(BinaryOperatorType opType)
		{
			return opType == BinaryOperatorType::BOOLEAN_AND ||
				opType == BinaryOperatorType::BOOLEAN_OR ||
				opType == BinaryOperatorType::EQUAL_TEST ||
				opType == BinaryOperatorType::NOT_EQUAL_TEST ||
				opType == BinaryOperatorType::GREATER_TEST ||
				opType == BinaryOperatorType::GREATER_EQUAL_TEST ||
				opType == BinaryOperatorType::LESS_EQUAL_TEST ||
				opType == BinaryOperatorType::LESS_TEST;
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
			case BinaryOperatorType::EQUAL_TEST:			return VM::OpCode::CMP;
			case BinaryOperatorType::NOT_EQUAL_TEST:		return VM::OpCode::CMP;
			case BinaryOperatorType::GREATER_TEST:			return VM::OpCode::CMP;
			case BinaryOperatorType::GREATER_EQUAL_TEST:	return VM::OpCode::CMP;
			case BinaryOperatorType::LESS_TEST:				return VM::OpCode::CMP;
			case BinaryOperatorType::LESS_EQUAL_TEST:		return VM::OpCode::CMP;
			case BinaryOperatorType::BOOLEAN_AND:			return VM::OpCode::AND; // ?
			case BinaryOperatorType::BOOLEAN_OR:			return VM::OpCode::OR; // ?
			default:										return VM::OpCode::_NONE;
			}
		}

		void IntermediateRepresentation::GenerateFromAST(AST::AST* ast, FunctionBindings* functionBindings)
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

			{
				char buffer[256];
				for (i32 terminalIndex = 0; terminalIndex < Terminal::MAX_OUTPUT_COUNT; ++terminalIndex)
				{
					sprintf(buffer, "out_%d", terminalIndex);
					state->variableTypes[std::string(buffer)] = Value::Type::INT;
				}
			}

			if (ast->diagnosticContainer->diagnostics.empty())
			{
				DiscoverFunctionDefinitions(ast->rootBlock);

				for (auto& funcPtrPair : functionBindings->ExternalFuncTable)
				{
					FuncPtr* funcPtr = funcPtrPair.second;

					IR::Value::Type returnType = (IR::Value::Type)funcPtr->returnType;

					std::vector<IR::Value::Type> argumentTypes;
					argumentTypes.reserve(funcPtr->argTypes.size());
					for (VM::Value::Type argType : funcPtr->argTypes)
					{
						argumentTypes.emplace_back((IR::Value::Type)argType);
					}

					if (!AddFunctionType(Span(Span::Source::GENERATED), funcPtr->name, returnType, argumentTypes))
					{
						PrintWarn("Failed to bind external function with name %s\n", funcPtr->name.c_str());
					}
				}

				if (state->diagnosticContainer->diagnostics.empty())
				{
					LowerStatement(ast->rootBlock);
					state->InsertionBlock()->AddHalt();
					LowerFunctionDefinitions(ast->rootBlock);
					blocks = state->blocks;
					SetBlockIndices();
				}
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

		void IntermediateRepresentation::DiscoverFunctionDefinitions(AST::Statement* statement)
		{
			switch (statement->statementType)
			{
			case AST::StatementType::STATEMENT_BLOCK:
			{
				AST::StatementBlock* statementBlock = (AST::StatementBlock*)statement;
				for (AST::Statement* innerStatement : statementBlock->statements)
				{
					DiscoverFunctionDefinitions(innerStatement);
				}
			} break;
			case AST::StatementType::FUNC_DECL:
			{
				AST::FunctionDeclaration* funcDecl = (AST::FunctionDeclaration*)statement;

				std::vector<Value::Type> argumentTypes(funcDecl->arguments.size());
				for (u32 i = 0; i < (u32)funcDecl->arguments.size(); ++i)
				{
					argumentTypes[i] = Value::FromASTTypeName(funcDecl->arguments[i]->typeName);
				}

				AddFunctionType(funcDecl->span, funcDecl->name, Value::FromASTTypeName(funcDecl->returnType), argumentTypes);
			} break;
			}
		}

		void IntermediateRepresentation::CheckReturnTypesMatch(Value::Type returnType, Span origin, Block* block)
		{
			if (block == nullptr)
			{
				return;
			}

			if (block->terminator != nullptr)
			{
				switch (block->terminator->type)
				{
				case TerminatorType::RETURN:
				{
					Return* returnVal = (Return*)block->terminator;
					Value::Type returnValType = returnVal->returnValue != nullptr ? state->GetValueType(returnVal->returnValue) : Value::Type::_NONE;
					if (returnValType != returnType)
					{
						std::string message = "Returned type doesn't match function's return type (" + std::string(Value::TypeToString(returnValType)) + " vs. " + std::string(Value::TypeToString(returnType)) + ")";
						state->diagnosticContainer->AddDiagnostic(origin, message);
					}
				} break;
				case TerminatorType::YIELD:
				{
					YieldReturn* yieldVal = (YieldReturn*)block->terminator;
					Value::Type yieldValType = yieldVal->yieldValue != nullptr ? state->GetValueType(yieldVal->yieldValue) : Value::Type::_NONE;
					if (yieldValType != returnType)
					{
						std::string message = "Yielded type doesn't match function's return type (" + std::string(Value::TypeToString(yieldValType)) + " vs. " + std::string(Value::TypeToString(returnType)) + ")";
						state->diagnosticContainer->AddDiagnostic(origin, message);
					}
				} break;
				case TerminatorType::BREAK:
				{
					Break* breakVal = (Break*)block->terminator;
					CheckReturnTypesMatch(returnType, origin, breakVal->target);
				} break;
				case TerminatorType::BRANCH:
				{
					Branch* branch = (Branch*)block->terminator;
					CheckReturnTypesMatch(returnType, origin, branch->target);
				} break;
				case TerminatorType::CONDITIONAL_BRANCH:
				{
					ConditionalBranch* branch = (ConditionalBranch*)block->terminator;
					CheckReturnTypesMatch(returnType, origin, branch->then);
					CheckReturnTypesMatch(returnType, origin, branch->otherwise);
				} break;
				}
			}
		}

		void IntermediateRepresentation::LowerFunctionDefinitions(AST::Statement* statement)
		{
			switch (statement->statementType)
			{
			case AST::StatementType::STATEMENT_BLOCK:
			{
				AST::StatementBlock* statementBlock = (AST::StatementBlock*)statement;
				for (AST::Statement* innerStatement : statementBlock->statements)
				{
					LowerFunctionDefinitions(innerStatement);
				}
			} break;
			case AST::StatementType::FUNC_DECL:
			{
				AST::FunctionDeclaration* funcDecl = (AST::FunctionDeclaration*)statement;

				IR::Block* bodyBlock = new IR::Block(state, funcDecl->span);
				bodyBlock->funcName = funcDecl->name;

				state->PushInstructionBlock(bodyBlock);

				State::FuncSig& funcSig = state->functionTypes[funcDecl->name];
				// TODO: Handle lifetimes
				for (u32 i = 0; i < (u32)funcDecl->arguments.size(); ++i)
				{
					AST::Declaration* arg = funcDecl->arguments[i];
					state->variableTypes[arg->identifierStr] = funcSig.argumentTypes[i];
					//arg->initializer = new AST::(Span::Source::GENERATED, arg->typeName, AST::StatementType::FUNC_ARG);
					//state->WriteVariableInBlock(arg->identifierStr, LowerExpression(arg));
					IR::Argmuent* argument = new IR::Argmuent(state, arg->identifierStr);
					state->InsertionBlock()->AddAssignment(new IR::Assignment(state, arg->span, arg->identifierStr, argument));
				}
				LowerStatement(funcDecl->body);
				if (bodyBlock->terminator != nullptr)
				{
					CheckReturnTypesMatch(IR::Value::FromASTTypeName(funcDecl->returnType), funcDecl->span, bodyBlock);
				}
				else
				{
					if (funcDecl->returnType == AST::TypeName::VOID)
					{
						// Void functions get auto generated return statement
						bodyBlock->AddReturn(Span(Span::Source::GENERATED), nullptr);
					}
					else
					{
						state->diagnosticContainer->AddDiagnostic(funcDecl->span, "Expected return statement");
					}
				}
				state->InsertionBlock()->SealBlock();
			} break;
			}
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

					Block* ifTrueBlock = new Block(state, ifStatement->then->span);
					Block* mergeBlock = new Block(state, state->InsertionBlock()->origin);
					Block* ifFalseBlock = ifStatement->otherwise == nullptr ? nullptr : new Block(state, ifStatement->otherwise->span);

					IR::Value* conditionExpr = LowerExpression(ifStatement->condition);
					if (conditionExpr == nullptr)
					{
						delete ifTrueBlock;
						delete mergeBlock;
						delete ifFalseBlock;
						return;
					}

					bool bAlwaysTakeTrue = false;
					bool bAlwaysTakeFalse = false;

					// Eliminate dead branches
					if (conditionExpr->type == IR::Value::Type::INT ||
						conditionExpr->type == IR::Value::Type::FLOAT ||
						conditionExpr->type == IR::Value::Type::BOOL)
					{
						if (conditionExpr->AsInt() == 0)
						{
							bAlwaysTakeFalse = true;
						}
						else
						{
							bAlwaysTakeTrue = true;
						}
					}
					else if (conditionExpr->type == IR::Value::Type::STRING ||
							conditionExpr->type == IR::Value::Type::CHAR)
					{
						state->diagnosticContainer->AddDiagnostic(Span(Span::Source::GENERATED), "Invalid conditional expression");
						return;
					}

					if (bAlwaysTakeTrue)
					{
						delete conditionExpr;
						conditionExpr = nullptr;
						delete ifFalseBlock;
						ifFalseBlock = nullptr;

						state->InsertionBlock()->AddBranch(ifStatement->span, ifTrueBlock);
						state->PushInstructionBlock(ifTrueBlock);
						LowerStatement(ifStatement->then);
						state->InsertionBlock()->AddBranch(ifStatement->span, mergeBlock);
						state->InsertionBlock()->SealBlock();
						state->PushInstructionBlock(mergeBlock);
						return;
					}

					if (bAlwaysTakeFalse)
					{
						delete conditionExpr;
						conditionExpr = nullptr;
						delete ifTrueBlock;
						ifTrueBlock = nullptr;

						if (ifStatement->otherwise != nullptr)
						{
							state->InsertionBlock()->AddBranch(ifStatement->span, ifFalseBlock);
							state->PushInstructionBlock(ifFalseBlock);
							LowerStatement(ifStatement->otherwise);
							state->InsertionBlock()->AddBranch(ifStatement->span, mergeBlock);
							state->InsertionBlock()->SealBlock();
							state->PushInstructionBlock(mergeBlock);
						}
						else
						{
							delete ifFalseBlock;
							ifFalseBlock = nullptr;
						}
						return;
					}

					IR::Value::Type valueType = state->GetValueType(conditionExpr);

					std::string tempIdent = state->NextTemporary();
					state->WriteVariableInBlock(tempIdent, conditionExpr);

					IR::Identifier* newTemp = new IR::Identifier(state, conditionExpr->origin, tempIdent);
					IR::Value* zeroConst;
					if (valueType == IR::Value::Type::INT)
					{
						zeroConst = new IR::Value(conditionExpr->origin, state, 0);
					}
					else if (valueType == IR::Value::Type::FLOAT)
					{
						zeroConst = new IR::Value(conditionExpr->origin, state, 0.0f);
					}
					else if (valueType == IR::Value::Type::BOOL)
					{
						zeroConst = new IR::Value(conditionExpr->origin, state, false);
					}
					else
					{
						delete newTemp;
						std::string ifString = ifStatement->condition->ToString();
						state->diagnosticContainer->AddDiagnostic(conditionExpr->origin, "Invalid conditional \"" + ifString + "\"");
						delete conditionExpr;
						return;
					}

					IR::Value* previosuConditionExpr = conditionExpr;
					conditionExpr = new IR::BinaryValue(state, conditionExpr->origin, IR::BinaryOperatorType::NOT_EQUAL_TEST, newTemp, zeroConst);
					delete previosuConditionExpr;
					previosuConditionExpr = nullptr;

					state->InsertionBlock()->AddConditionalBranch(state, ifStatement->span, conditionExpr, ifTrueBlock, ifFalseBlock);
					state->PushInstructionBlock(ifTrueBlock);
					LowerStatement(ifStatement->then);
					state->InsertionBlock()->AddBranch(ifStatement->span, mergeBlock);
					state->InsertionBlock()->SealBlock();

					if (ifStatement->otherwise != nullptr)
					{
						state->PushInstructionBlock(ifFalseBlock);
						LowerStatement(ifStatement->otherwise);
						state->InsertionBlock()->AddBranch(ifStatement->span, mergeBlock);
						state->InsertionBlock()->SealBlock();
					}

					state->PushInstructionBlock(mergeBlock);
				} break;
				case AST::StatementType::ASSIGNMENT:
				{
					AST::Assignment* assignment = (AST::Assignment*)statement;

					IR::Value* rhs = LowerExpression(assignment->rhs);

					if (rhs != nullptr)
					{
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
					}
				} break;
				case AST::StatementType::IDENTIFIER:
				{
					AST::Identifier* identifier = (AST::Identifier*)statement;
					Value* value = LowerExpression(identifier);
					state->InsertionBlock()->AddAssignment(new Assignment(state, identifier->span, identifier->identifierStr, value));
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

					if (state->variableTypes.find(decl->identifierStr) != state->variableTypes.end())
					{
						state->diagnosticContainer->AddDiagnostic(decl->span, "Redefinition of variable \"" + decl->identifierStr + "\"");
						return;
					}
					if (state->functionTypes.find(decl->identifierStr) != state->functionTypes.end())
					{
						state->diagnosticContainer->AddDiagnostic(decl->span, "Redefinition of name \"" + decl->identifierStr + "\"");
						return;
					}

					IR::Value* initializerValue = LowerExpression(decl->initializer);

					if (initializerValue != nullptr)
					{
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
				case AST::StatementType::COMPOUND_ASSIGNMENT:
				{
					AST::CompoundAssignment* compoundAssignment = (AST::CompoundAssignment*)statement;

					/*

					Simple case:
						a += 10;

							v

						a = a + 10;
					else:
						a += 8 * 9;

							v

						tmp = 8 * 9;
						a = a + tmp;

					*/

					IR::Value* rhs = LowerExpression(compoundAssignment->rhs);

					IR::Identifier* lhsIdentVal = new IR::Identifier(state, compoundAssignment->span, compoundAssignment->lhs);
					BinaryOperatorType opType = IRBinaryOperatorTypeFromASTBinaryOperatorType(GetNonCompoundType(compoundAssignment->operatorType));

					IR::BinaryValue* operation;
					if (IR::Value::IsSimple(rhs->type))
					{
						operation = new IR::BinaryValue(state, compoundAssignment->span, opType, lhsIdentVal, rhs);

					}
					else
					{
						// Non simple case requires temporary
						std::string tempIdent = state->NextTemporary();
						state->WriteVariableInBlock(tempIdent, rhs);

						IR::Identifier* tempIdentVal = new IR::Identifier(state, compoundAssignment->span, tempIdent);
						operation = new IR::BinaryValue(state, compoundAssignment->span, opType, lhsIdentVal, tempIdentVal);
					}

					state->variableTypes[compoundAssignment->lhs] = state->GetValueType(rhs);
					state->InsertionBlock()->AddAssignment(new IR::Assignment(state, compoundAssignment->span, compoundAssignment->lhs, operation));
				} break;
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
				IR::Value* loweredRHS = LowerExpression(assignment->rhs);
				if (loweredRHS != nullptr)
				{
					return new IR::Assignment(state, assignment->span, assignment->lhs, loweredRHS);
				}
			} break;
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
				case AST::UnaryOperatorType::NEGATE:
					irOpType = IR::UnaryOperatorType::NEGATE;
					break;
				case AST::UnaryOperatorType::NOT:
					irOpType = IR::UnaryOperatorType::NOT;
					break;
				case AST::UnaryOperatorType::BIN_INVERT:
					irOpType = IR::UnaryOperatorType::BIN_INVERT;
					break;
				default:
					ENSURE_NO_ENTRY();
				}

				IR::Value* loweredExpr = LowerExpression(unaryOperation->expression);
				if (loweredExpr != nullptr)
				{
					return new IR::UnaryValue(state, unaryOperation->span, irOpType, loweredExpr);
				}
			} break;
			case AST::StatementType::BINARY_OPERATION:
			{
				AST::BinaryOperation* binaryOperation = (AST::BinaryOperation*)expression;

				if (binaryOperation->operatorType == AST::BinaryOperatorType::ASSIGN)
				{
					if (binaryOperation->lhs->statementType == AST::StatementType::IDENTIFIER)
					{
						AST::Identifier* lhs = (AST::Identifier*)binaryOperation->lhs;
						IR::Value* loweredRHS = LowerExpression(binaryOperation->rhs);
						if (loweredRHS != nullptr)
						{
							// TODO: Add to usages here
							return new IR::Assignment(state, lhs->span, lhs->identifierStr, loweredRHS);
						}
					}

					return nullptr;
				}

				IR::BinaryOperatorType irOpType = IR::IRBinaryOperatorTypeFromASTBinaryOperatorType(binaryOperation->operatorType);
				IR::Value* lhsVal = LowerExpression(binaryOperation->lhs);
				IR::Value* rhsVal = LowerExpression(binaryOperation->rhs);

				if (lhsVal == nullptr ||
					rhsVal == nullptr)
				{
					return nullptr;
				}

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
						case IR::BinaryOperatorType::GREATER_TEST:			return new IR::Constant(IR::Value(lhsVal->origin.Extend(rhsVal->origin), state, *lhsVal > *rhsVal));
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
					if (!Value::IsLiteral(lhsVal->type) && lhsVal->type != Value::Type::IDENTIFIER)
					{
						bool bSplit = true;
						if (lhsVal->type == Value::Type::BINARY)
						{
							IR::BinaryValue* binaryVal = (IR::BinaryValue*)lhsVal;
							if (IsComparisonOp(binaryVal->opType))
							{
								bSplit = false;
							}
						}
						if (bSplit)
						{
							std::string lhsVar = state->NextTemporary();
							state->WriteVariableInBlock(lhsVar, lhsVal);
							lhsVal = new IR::Identifier(state, lhsVal->origin, lhsVar);
						}
					}
					if (!Value::IsLiteral(rhsVal->type) && rhsVal->type != Value::Type::IDENTIFIER)
					{
						bool bSplit = true;
						if (rhsVal->type == Value::Type::BINARY)
						{
							IR::BinaryValue* binaryVal = (IR::BinaryValue*)rhsVal;
							if (IsComparisonOp(binaryVal->opType))
							{
								bSplit = false;
							}
						}
						if (bSplit)
						{
							std::string rhsVar = state->NextTemporary();
							state->WriteVariableInBlock(rhsVar, rhsVal);
							rhsVal = new IR::Identifier(state, rhsVal->origin, rhsVar);
						}
					}

					// Turn simple types e.g. 'a' into 'a != 0' tests
					if (irOpType == IR::BinaryOperatorType::BOOLEAN_AND ||
						irOpType == IR::BinaryOperatorType::BOOLEAN_OR)
					{
						IR::BinaryValue* binaryVal = (IR::BinaryValue*)rhsVal;
						if (!IsComparisonOp(binaryVal->opType))
						{
							if (!Value::IsLiteral(lhsVal->type))
							{
								lhsVal = new IR::BinaryValue(state, lhsVal->origin, IR::BinaryOperatorType::NOT_EQUAL_TEST, lhsVal, new IR::Value(Span(Span::Source::GENERATED), state, 0));
							}
							if (!Value::IsLiteral(rhsVal->type))
							{
								rhsVal = new IR::BinaryValue(state, rhsVal->origin, IR::BinaryOperatorType::NOT_EQUAL_TEST, rhsVal, new IR::Value(Span(Span::Source::GENERATED), state, 0));
							}
						}
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
							lhsVal = new IR::Identifier(state, lhsVal->origin, lhsVar);
						}

						if (rhsType != resultType)
						{
							std::string rhsVar = state->NextTemporary();
							state->WriteVariableInBlock(rhsVar, new IR::CastValue(state, rhsVal->origin, resultType, rhsVal));
							rhsVal = new IR::Identifier(state, rhsVal->origin, rhsVar);
						}
					}

					return new IR::BinaryValue(state, lhsVal->origin.Extend(rhsVal->origin), irOpType, lhsVal, rhsVal);
				}
			} break;
			case AST::StatementType::TERNARY_OPERATION:
			{
				AST::TernaryOperation* ternary = (AST::TernaryOperation*)expression;

				IR::Value* condition = LowerExpression(ternary->condition);
				IR::Value* ifTrue = LowerExpression(ternary->ifTrue);
				IR::Value* ifFalse = LowerExpression(ternary->ifFalse);

				if (condition != nullptr && ifTrue != nullptr && ifFalse != nullptr)
				{
					return new IR::TernaryValue(state, ternary->span, condition, ifTrue, ifFalse);
				}
			} break;
			case AST::StatementType::FUNC_CALL:
			{
				AST::FunctionCall* functionCall = (AST::FunctionCall*)expression;
				std::vector<IR::Value*> arguments;
				auto iter = state->functionTypes.find(functionCall->target);
				if (iter != state->functionTypes.end())
				{
					State::FuncSig& funcSig = iter->second;
					bool bSigMatches = functionCall->arguments.size() == funcSig.argumentTypes.size();

					for (u32 i = 0; i < (u32)functionCall->arguments.size(); ++i)
					{
						Value* argVal = LowerExpression(functionCall->arguments[i]);
						if (argVal != nullptr && !Value::IsLiteral(argVal->type))
						{
							std::string tempVar = state->NextTemporary();
							state->WriteVariableInBlock(tempVar, argVal);
							argVal = new IR::Identifier(state, functionCall->arguments[i]->span, tempVar);
						}
						arguments.push_back(argVal);
					}

					if (functionCall->arguments.size() == funcSig.argumentTypes.size())
					{
						for (u32 i = 0; i < (u32)functionCall->arguments.size(); ++i)
						{
							if (state->GetValueType(arguments[i]) != funcSig.argumentTypes[i])
							{
								bSigMatches = false;
								break;
							}
						}
					}

					if (!bSigMatches)
					{
						StringBuilder message;
						message.Append("Incorrect number/type of arguments, expected:\n(");
						for (u32 i = 0; i < (u32)funcSig.argumentTypes.size(); ++i)
						{
							message.Append(Value::TypeToString(funcSig.argumentTypes[i]));
							if (i < (u32)funcSig.argumentTypes.size() - 1)
							{
								message.Append(", ");
							}
						}

						message.Append(")\ngiven:\n(");
						for (u32 i = 0; i < (u32)arguments.size(); ++i)
						{
							message.Append(Value::TypeToString(state->GetValueType(arguments[i])));
							if (i < (u32)arguments.size() - 1)
							{
								message.Append(", ");
							}
						}
						message.Append(')');

						state->diagnosticContainer->AddDiagnostic(functionCall->span, message.ToString());
					}
				}
				else
				{
					std::string message = "Undeclared function \"" + functionCall->target + "\"";
					state->diagnosticContainer->AddDiagnostic(functionCall->span, message);
				}

				return new IR::FunctionCallValue(state, functionCall->span, functionCall->target, arguments);
			} break;
			case AST::StatementType::IDENTIFIER:
			{
				AST::Identifier* identifier = (AST::Identifier*)expression;
				return new IR::Identifier(state, identifier->span, identifier->identifierStr);
			} break;
			case AST::StatementType::CAST:
			{
				AST::Cast* cast = (AST::Cast*)expression;
				IR::Value* loweredVal = LowerExpression(cast->target);
				if (loweredVal != nullptr)
				{
					if (AST::IsSimple(cast->target->statementType))
					{
						return new IR::CastValue(state, cast->span, IR::Value::FromASTTypeName(cast->typeName), loweredVal);
					}
					else
					{
						std::string tempIdent = state->NextTemporary();
						state->WriteVariableInBlock(tempIdent, loweredVal);
						return new IR::CastValue(state, cast->span, IR::Value::FromASTTypeName(cast->typeName), new IR::Identifier(state, cast->span, tempIdent));
					}
				}
			} break;
			case AST::StatementType::BREAK:
			{
				AST::BreakStatement* breakStatement = (AST::BreakStatement*)expression;

				IR::Block* nextBlock = new IR::Block(state, breakStatement->span);

				state->InsertionBlock()->AddBranch(breakStatement->span, nextBlock);
				state->InsertionBlock()->SealBlock();

				state->PushInstructionBlock(nextBlock);
				return nullptr;
			} break;
			case AST::StatementType::YIELD:
			{
				AST::YieldStatement* yieldStatement = (AST::YieldStatement*)expression;

				IR::Block* nextBlock = new IR::Block(state, yieldStatement->span);

				if (yieldStatement->yieldValue != nullptr)
				{
					IR::Value* loweredVal = LowerExpression(yieldStatement->yieldValue);
					if (IR::Value::IsLiteral(loweredVal->type) || loweredVal->type == IR::Value::Type::IDENTIFIER)
					{
						state->InsertionBlock()->AddYield(yieldStatement->span, loweredVal);
					}
					else
					{
						std::string tempIdent = state->NextTemporary();
						state->WriteVariableInBlock(tempIdent, loweredVal);
						state->InsertionBlock()->AddYield(yieldStatement->span, new IR::Identifier(state, yieldStatement->span, tempIdent));
					}
				}
				else
				{
					state->InsertionBlock()->AddYield(yieldStatement->span, nullptr);
				}

				state->PushInstructionBlock(nextBlock);
				return nullptr;
			} break;
			case AST::StatementType::RETURN:
			{
				AST::ReturnStatement* returnStatement = (AST::ReturnStatement*)expression;

				IR::Block* nextBlock = new IR::Block(state, returnStatement->span);

				if (returnStatement->returnValue != nullptr)
				{
					IR::Value* loweredVal = LowerExpression(returnStatement->returnValue);
					if (loweredVal == nullptr)
					{
						delete nextBlock;
						return nullptr;
					}
					if (IR::Value::IsSimple(loweredVal->type))
					{
						state->InsertionBlock()->AddReturn(returnStatement->span, loweredVal);
					}
					else
					{
						std::string tempIdent = state->NextTemporary();
						state->WriteVariableInBlock(tempIdent, loweredVal);
						state->InsertionBlock()->AddReturn(returnStatement->span, new IR::Identifier(state, returnStatement->span, tempIdent));
					}
				}
				else
				{
					state->InsertionBlock()->AddReturn(returnStatement->span, nullptr);
				}

				state->InsertionBlock()->SealBlock();

				state->PushInstructionBlock(nextBlock);
				return nullptr;
			} break;
			}

			return new IR::Value(expression->span, state, IR::Value::Type::_NONE);
		}

		bool IntermediateRepresentation::AddFunctionType(Span origin, const std::string& funcName, Value::Type returnType, const std::vector<Value::Type>& argumentTypes)
		{
			if (state->functionTypes.find(funcName) != state->functionTypes.end())
			{
				state->diagnosticContainer->AddDiagnostic(origin, "Redefinition of function \"" + funcName + "\"");
				return false;
			}
			if (state->variableTypes.find(funcName) != state->variableTypes.end())
			{
				state->diagnosticContainer->AddDiagnostic(origin, "Redefinition of name \"" + funcName + "\"");
				return false;
			}

			state->functionTypes[funcName] = { returnType, argumentTypes };
			return true;
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
	} // namespace IR
} // namespace flex
