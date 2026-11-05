#include "stdafx.hpp"

#include "VirtualMachine/Backend/IR.hpp"

#include "Scene/Objects/Terminal.hpp"
#include "StringBuilder.hpp"
#include "VirtualMachine/Backend/VariableContainer.hpp"
#include "VirtualMachine/Backend/VirtualMachine.hpp"
#include "VirtualMachine/Frontend/Parser.hpp"

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

		void Block::AddCall(State* state, FunctionCallValue* callValue)
		{
			if (!Filled())
			{
				// Empty variable name signals call to void function. Calls to
				// functions with return values go through AddAssignment path.
				std::string variable = "";
				assignments.push_back(new Assignment(state, callValue->origin, variable, callValue));
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
			blocks.clear();
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
				// (The operand's value type, not its node type: it's usually an identifier)
				Value::Type operandType = GetValueType(unary->operand);
				switch (unary->opType)
				{
				case UnaryOperatorType::NOT:
					if (operandType == Value::Type::BOOL)
					{
						return Value::Type::BOOL;
					}
					diagnosticContainer->AddDiagnostic(unary->origin, "Not operator can only be applied to boolean types");
					return Value::Type::_NONE;
				case UnaryOperatorType::NEGATE:
					if (IR::Value::IsNumeric(operandType))
					{
						return operandType;
					}
					diagnosticContainer->AddDiagnostic(unary->origin, "Negate operator can only be applied to numeric types");
					return Value::Type::_NONE;
				case UnaryOperatorType::BIN_INVERT:
					if (operandType == Value::Type::INT)
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
			case Value::Type::CALL:
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
				Value::Type valueType = GetValueType(value);
				if (iter->second != valueType)
				{
					std::string message = "Type mismatch (" + std::string(Value::TypeToString(valueType)) + " vs. " + std::string(Value::TypeToString(iter->second)) + ")";
					diagnosticContainer->AddDiagnostic(value->origin, message);
				}
			}
		}

		void Assignment::Destroy()
		{
			value->Destroy();
			delete value;
			value = nullptr;
		}

		std::string Assignment::ToString() const
		{
			StringBuilder builder;

			if (value->type == Value::Type::CALL)
			{
				// variable will be empty for functions with a return type of void
				if (!variable.empty())
				{
					builder.Append(variable);
					builder.Append(" = ");
				}

				builder.Append(value->ToString());
			}
			else
			{
				const char* typeStr = IR::Value::TypeToString(value->type);
				builder.Append(typeStr);
				builder.Append(" ");
				builder.Append(variable);
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
			// (Void returns have no value)
			if (returnValue != nullptr)
			{
				returnValue->Destroy();
				delete returnValue;
				returnValue = nullptr;
			}
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
			if (yieldValue != nullptr)
			{
				yieldValue->Destroy();
				delete yieldValue;
				yieldValue = nullptr;
			}
		}

		std::string YieldReturn::ToString() const
		{
			StringBuilder builder;

			builder.Append("yield ");
			if (yieldValue != nullptr)
			{
				builder.Append(yieldValue->ToString());
			}

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
			condition->Destroy();
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
			operand->Destroy();
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
			left->Destroy();
			delete left;
			left = nullptr;
			right->Destroy();
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
			condition->Destroy();
			delete condition;
			condition = nullptr;
			ifTrue->Destroy();
			delete ifTrue;
			ifTrue = nullptr;
			ifFalse->Destroy();
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
				arguments[i]->Destroy();
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
			builder.Append(" -> ");
			State::FuncSig funcSig = irState->functionTypes[target];
			builder.Append(TypeToString(funcSig.returnType));

			return builder.ToString();
		}

		void CastValue::Destroy()
		{
			target->Destroy();
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

			if (g_EmptyIRValue != nullptr)
			{
				g_EmptyIRValue->Destroy();
				delete g_EmptyIRValue;
			}
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
					IFunction* funcPtr = funcPtrPair.second;

					IR::Value::Type returnType = IR::Value::IRTypeFromVariantType(funcPtr->returnType);

					std::vector<IR::Value::Type> argumentTypes;
					argumentTypes.reserve(funcPtr->argTypes.size());
					for (Variant::Type argType : funcPtr->argTypes)
					{
						argumentTypes.emplace_back(IR::Value::IRTypeFromVariantType(argType));
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
					FinishBlocks(0, false, Value::Type::VOID_, ast->rootBlock->span);
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
			// (The state owns the blocks, blocks is either empty or a copy of them. Failed lowering leaves them only in
			// the state.)
			state->Clear();
			blocks.clear();
			state->Destroy();
			delete state;
			state = nullptr;

			// (Shared by every VM, so another VM's Destroy may already have freed it)
			if (g_EmptyIRValue != nullptr)
			{
				g_EmptyIRValue->Destroy();
				delete g_EmptyIRValue;
				g_EmptyIRValue = nullptr;
			}
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

		void IntermediateRepresentation::FinishBlocks(u32 firstBlockIndex, bool bIsFunction, Value::Type returnType, Span origin)
		{
			if (firstBlockIndex >= (u32)state->blocks.size())
			{
				return;
			}

			// Find the blocks reachable from the entry block
			std::set<Block*> reachable;
			std::vector<Block*> toVisit = { state->blocks[firstBlockIndex] };
			while (!toVisit.empty())
			{
				Block* block = toVisit.back();
				toVisit.pop_back();
				if (block == nullptr || !reachable.insert(block).second || block->terminator == nullptr)
				{
					continue;
				}

				switch (block->terminator->type)
				{
				case TerminatorType::BRANCH:
					toVisit.push_back(((Branch*)block->terminator)->target);
					break;
				case TerminatorType::BREAK:
					toVisit.push_back(((Break*)block->terminator)->target);
					break;
				case TerminatorType::YIELD:
					toVisit.push_back(((YieldReturn*)block->terminator)->target);
					break;
				case TerminatorType::CONDITIONAL_BRANCH:
					toVisit.push_back(((ConditionalBranch*)block->terminator)->then);
					toVisit.push_back(((ConditionalBranch*)block->terminator)->otherwise);
					break;
				}
			}

			bool bMissingReturn = false;
			for (u32 i = firstBlockIndex; i < (u32)state->blocks.size(); ++i)
			{
				Block* block = state->blocks[i];
				if (block->terminator != nullptr)
				{
					if (block->terminator->type == TerminatorType::RETURN)
					{
						Return* returnVal = (Return*)block->terminator;
						if (!bIsFunction)
						{
							state->diagnosticContainer->AddDiagnostic(returnVal->origin, "Return statement outside of a function");
							continue;
						}

						// (Checked in unreachable blocks too)
						Value::Type returnValType = returnVal->returnValue != nullptr ? state->GetValueType(returnVal->returnValue) : Value::Type::VOID_;
						if (returnValType != returnType)
						{
							std::string message = "Returned type doesn't match function's return type (" + std::string(Value::TypeToString(returnValType)) + " vs. " + std::string(Value::TypeToString(returnType)) + ")";
							state->diagnosticContainer->AddDiagnostic(returnVal->origin, message);
						}
					}
					continue;
				}

				// Execution must never run off the end of a block into whatever is placed after it
				if (!bIsFunction)
				{
					block->AddHalt();
				}
				else
				{
					if (returnType != Value::Type::VOID_ && reachable.find(block) != reachable.end())
					{
						bMissingReturn = true;
					}
					// Void functions get auto generated return statements (non-void ones only in unreachable blocks)
					block->AddReturn(Span(Span::Source::GENERATED), nullptr);
				}
			}

			if (bMissingReturn)
			{
				state->diagnosticContainer->AddDiagnostic(origin, "Not all code paths return a value");
			}
		}

		IR::Value* IntermediateRepresentation::SplitIntoTemporary(IR::Value* value)
		{
			if (value == nullptr || IR::Value::IsSimple(value->type))
			{
				return value;
			}

			std::string tempIdent = state->NextTemporary();
			state->WriteVariableInBlock(tempIdent, value);
			return new IR::Identifier(state, value->origin, tempIdent);
		}

		IR::Value* IntermediateRepresentation::ToBoolOperand(IR::Value* value)
		{
			if (value == nullptr)
			{
				return nullptr;
			}

			Value::Type valueType = state->GetValueType(value);
			if (valueType == Value::Type::BOOL)
			{
				return value;
			}

			if (valueType == Value::Type::INT || valueType == Value::Type::FLOAT)
			{
				if (IR::Value::IsLiteral(value->type))
				{
					IR::Value* result = new IR::Constant(IR::Value(value->origin, state, value->AsBool() != 0));
					value->Destroy();
					delete value;
					return result;
				}

				IR::Value* zeroConst = (valueType == Value::Type::INT) ? new IR::Value(value->origin, state, 0) : new IR::Value(value->origin, state, 0.0f);
				return SplitIntoTemporary(new IR::BinaryValue(state, value->origin, IR::BinaryOperatorType::NOT_EQUAL_TEST, value, zeroConst));
			}

			state->diagnosticContainer->AddDiagnostic(value->origin, "Expected a bool operand");
			return value;
		}

		bool IntermediateRepresentation::LowerConditionalBranch(AST::Expression* condition, Span origin, Block* then, Block* otherwise)
		{
			IR::Value* conditionExpr = LowerExpression(condition);
			if (conditionExpr == nullptr)
			{
				return false;
			}

			if (conditionExpr->type == IR::Value::Type::STRING ||
				conditionExpr->type == IR::Value::Type::CHAR)
			{
				state->diagnosticContainer->AddDiagnostic(conditionExpr->origin, "Invalid conditional expression");
				conditionExpr->Destroy();
				delete conditionExpr;
				return false;
			}

			if (IR::Value::IsLiteral(conditionExpr->type))
			{
				// Constant condition (the other block is unreachable)
				bool bTrue = conditionExpr->AsBool() != 0;
				conditionExpr->Destroy();
				delete conditionExpr;
				state->InsertionBlock()->AddBranch(origin, bTrue ? then : otherwise);
				return true;
			}

			IR::Value::Type valueType = state->GetValueType(conditionExpr);
			IR::Value* zeroConst = nullptr;
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
				std::string conditionString = condition->ToString();
				state->diagnosticContainer->AddDiagnostic(conditionExpr->origin, "Invalid conditional \"" + conditionString + "\"");
				conditionExpr->Destroy();
				delete conditionExpr;
				return false;
			}

			std::string tempIdent = state->NextTemporary();
			Span conditionOrigin = conditionExpr->origin;
			state->WriteVariableInBlock(tempIdent, conditionExpr);

			IR::Identifier* newTemp = new IR::Identifier(state, conditionOrigin, tempIdent);
			IR::Value* rewrittenConditionExpr = new IR::BinaryValue(state, conditionOrigin, IR::BinaryOperatorType::NOT_EQUAL_TEST, newTemp, zeroConst);
			state->InsertionBlock()->AddConditionalBranch(state, origin, rewrittenConditionExpr, then, otherwise);
			return true;
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

				const u32 firstBlockIndex = (u32)state->blocks.size();
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
				FinishBlocks(firstBlockIndex, true, IR::Value::FromASTTypeName(funcDecl->returnType), funcDecl->span);
				state->InsertionBlock()->SealBlock();
			} break;
			}
		}

		void IntermediateRepresentation::LowerStatement(AST::Statement* statement)
		{
			if (IsExpression(statement->statementType))
			{
				IR::Value* value = LowerExpression((AST::Expression*)statement);

				if (statement->statementType == AST::StatementType::CALL)
				{
					IR::FunctionCallValue* call = (IR::FunctionCallValue*)value;
					auto iter = state->functionTypes.find(call->target);
					if (iter != state->functionTypes.end())
					{
						if (iter->second.returnType == Value::Type::VOID_)
						{
							state->InsertionBlock()->AddCall(state, call);
							return;
						}
						else
						{
							state->WriteVariableInBlock(state->NextTemporary(), value);
							return;
						}
					}
					else
					{
						// (LowerExpression already reported the undeclared function)
						value->Destroy();
						delete value;
						return;
					}
				}
				state->WriteVariableInBlock(state->NextTemporary(), value);
				return;
			}

			if (IsLiteral(statement->statementType))
			{
				//VariantWrapper val1 = VariantWrapper(VariantWrapper::Type::CONSTANT, ((AST::Expression*)statement)->GetValue());
				DEBUG_BREAK();
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

					// (Without an else clause the false path goes straight to the merge block)
					if (!LowerConditionalBranch(ifStatement->condition, ifStatement->span, ifTrueBlock, ifFalseBlock != nullptr ? ifFalseBlock : mergeBlock))
					{
						delete ifTrueBlock;
						delete mergeBlock;
						delete ifFalseBlock;
						return;
					}

					state->PushInstructionBlock(ifTrueBlock);
					LowerStatement(ifStatement->then);
					state->InsertionBlock()->AddBranch(ifStatement->span, mergeBlock);
					state->InsertionBlock()->SealBlock();

					if (ifFalseBlock != nullptr)
					{
						state->PushInstructionBlock(ifFalseBlock);
						LowerStatement(ifStatement->otherwise);
						state->InsertionBlock()->AddBranch(ifStatement->span, mergeBlock);
						state->InsertionBlock()->SealBlock();
					}

					state->PushInstructionBlock(mergeBlock);
				} break;
				case AST::StatementType::WHILE:
				{
					AST::WhileStatement* whileStatement = (AST::WhileStatement*)statement;

					Block* conditionBlock = new Block(state, whileStatement->condition->span);
					Block* bodyBlock = new Block(state, whileStatement->body->span);
					Block* exitBlock = new Block(state, whileStatement->span);

					state->InsertionBlock()->AddBranch(whileStatement->span, conditionBlock);
					state->PushInstructionBlock(conditionBlock);
					if (!LowerConditionalBranch(whileStatement->condition, whileStatement->span, bodyBlock, exitBlock))
					{
						delete bodyBlock;
						delete exitBlock;
						return;
					}

					state->PushInstructionBlock(bodyBlock);
					m_BreakTargets.push_back(exitBlock);
					LowerStatement(whileStatement->body);
					m_BreakTargets.pop_back();
					state->InsertionBlock()->AddBranch(whileStatement->span, conditionBlock);

					state->PushInstructionBlock(exitBlock);
				} break;
				case AST::StatementType::DO_WHILE:
				{
					AST::DoWhileStatement* doWhileStatement = (AST::DoWhileStatement*)statement;

					Block* bodyBlock = new Block(state, doWhileStatement->body->span);
					Block* conditionBlock = new Block(state, doWhileStatement->condition->span);
					Block* exitBlock = new Block(state, doWhileStatement->span);

					state->InsertionBlock()->AddBranch(doWhileStatement->span, bodyBlock);
					state->PushInstructionBlock(bodyBlock);
					m_BreakTargets.push_back(exitBlock);
					LowerStatement(doWhileStatement->body);
					m_BreakTargets.pop_back();
					state->InsertionBlock()->AddBranch(doWhileStatement->span, conditionBlock);

					state->PushInstructionBlock(conditionBlock);
					LowerConditionalBranch(doWhileStatement->condition, doWhileStatement->span, bodyBlock, exitBlock);

					// (Pushed even on error since breaks in the body may already target it)
					state->PushInstructionBlock(exitBlock);
				} break;
				case AST::StatementType::FOR:
				{
					AST::ForStatement* forStatement = (AST::ForStatement*)statement;

					if (forStatement->setup != nullptr)
					{
						LowerStatement(forStatement->setup);
					}

					Block* conditionBlock = new Block(state, forStatement->span);
					Block* bodyBlock = new Block(state, forStatement->body->span);
					Block* updateBlock = new Block(state, forStatement->span);
					Block* exitBlock = new Block(state, forStatement->span);

					state->InsertionBlock()->AddBranch(forStatement->span, conditionBlock);
					state->PushInstructionBlock(conditionBlock);
					if (forStatement->condition != nullptr)
					{
						if (!LowerConditionalBranch((AST::Expression*)forStatement->condition, forStatement->span, bodyBlock, exitBlock))
						{
							delete bodyBlock;
							delete updateBlock;
							delete exitBlock;
							return;
						}
					}
					else
					{
						state->InsertionBlock()->AddBranch(forStatement->span, bodyBlock);
					}

					state->PushInstructionBlock(bodyBlock);
					m_BreakTargets.push_back(exitBlock);
					LowerStatement(forStatement->body);
					m_BreakTargets.pop_back();
					state->InsertionBlock()->AddBranch(forStatement->span, updateBlock);

					state->PushInstructionBlock(updateBlock);
					if (forStatement->update != nullptr)
					{
						LowerStatement(forStatement->update);
					}
					state->InsertionBlock()->AddBranch(forStatement->span, conditionBlock);

					state->PushInstructionBlock(exitBlock);
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
								CHECK_EQ((u32)state->variableTypes[assignment->lhs], (u32)state->GetValueType(rhs));
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
					CHECK_NE(binaryOpTranslation, OpCode::_NONE);

					VariantWrapper lhsWrapper = GetValueWrapperFromExpression(binOp->lhs);
					VariantWrapper rhsWrapper = GetValueWrapperFromExpression(binOp->rhs);

					if (binaryOpTranslation == OpCode::CMP)
					{
						Instruction binInst(binaryOpTranslation, lhsWrapper, rhsWrapper);
						state->InsertionBlock()->PushBack(binInst);

						OpCode jumpCode = BinaryOpToJumpCode(binOp->operatorType);
						if (jumpCode != OpCode::_NONE)
						{
							i32 jumpAddress = 0;
							Instruction jumpInst(jumpCode, VariantWrapper(VariantWrapper::Type::CONSTANT, IR::Value(jumpAddress)));
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
								CHECK(false);
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

					if (state->variableTypes.find(compoundAssignment->lhs) == state->variableTypes.end())
					{
						// (Otherwise the assignment below silently declares it, e.g. out_100 += 1 would index past the terminal outputs)
						state->diagnosticContainer->AddDiagnostic(compoundAssignment->span, "Undeclared identifier \"" + compoundAssignment->lhs + "\"");
						return;
					}

					IR::Value* rhs = LowerExpression(compoundAssignment->rhs);
					if (rhs == nullptr)
					{
						return;
					}

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
				IR::Constant* constant = new IR::Constant(IR::Value(expression->GetValue()));
				constant->origin = expression->span;
				constant->irState = state; // (Used to report errors when folding constants)
				return constant;
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
					// Fold constants
					IR::Value* folded = nullptr;
					if (irOpType == IR::UnaryOperatorType::NEGATE && loweredExpr->type == IR::Value::Type::INT)
					{
						folded = new IR::Constant(IR::Value(unaryOperation->span, state, -loweredExpr->valInt));
					}
					else if (irOpType == IR::UnaryOperatorType::NEGATE && loweredExpr->type == IR::Value::Type::FLOAT)
					{
						folded = new IR::Constant(IR::Value(unaryOperation->span, state, -loweredExpr->valFloat));
					}
					else if (irOpType == IR::UnaryOperatorType::NOT && loweredExpr->type == IR::Value::Type::BOOL)
					{
						folded = new IR::Constant(IR::Value(unaryOperation->span, state, loweredExpr->valBool == 0));
					}
					else if (irOpType == IR::UnaryOperatorType::BIN_INVERT && loweredExpr->type == IR::Value::Type::INT)
					{
						folded = new IR::Constant(IR::Value(unaryOperation->span, state, ~loweredExpr->valInt));
					}
					if (folded != nullptr)
					{
						loweredExpr->Destroy();
						delete loweredExpr;
						return folded;
					}

					// (Codegen needs a simple operand)
					return new IR::UnaryValue(state, unaryOperation->span, irOpType, SplitIntoTemporary(loweredExpr));
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
					if (lhsVal != nullptr)
					{
						lhsVal->Destroy();
						delete lhsVal;
					}
					if (rhsVal != nullptr)
					{
						rhsVal->Destroy();
						delete rhsVal;
					}
					return nullptr;
				}

				const bool bBooleanOp = irOpType == IR::BinaryOperatorType::BOOLEAN_AND || irOpType == IR::BinaryOperatorType::BOOLEAN_OR;
				if (bBooleanOp)
				{
					// && & || take bools (ints & floats are compared against zero)
					lhsVal = ToBoolOperand(SplitIntoTemporary(lhsVal));
					rhsVal = ToBoolOperand(SplitIntoTemporary(rhsVal));
				}

				if (IR::Value::IsLiteral(lhsVal->type) &&
					IR::Value::IsLiteral(rhsVal->type))
				{
					IR::Value* folded = nullptr;
					IR::Value::Type resultType;
					if (IR::Value::TypesAreCoercible(state, lhsVal, rhsVal, resultType))
					{
						Span foldedOrigin = lhsVal->origin.Extend(rhsVal->origin);
						switch (irOpType)
						{
						case IR::BinaryOperatorType::ADD:					folded = new IR::Constant(*lhsVal + *rhsVal); break;
						case IR::BinaryOperatorType::SUB:					folded = new IR::Constant(*lhsVal - *rhsVal); break;
						case IR::BinaryOperatorType::MUL:					folded = new IR::Constant(*lhsVal * *rhsVal); break;
						case IR::BinaryOperatorType::DIV:					folded = new IR::Constant(*lhsVal / *rhsVal); break;
						case IR::BinaryOperatorType::MOD:					folded = new IR::Constant(*lhsVal % *rhsVal); break;
						case IR::BinaryOperatorType::BIN_AND:				folded = new IR::Constant(*lhsVal & *rhsVal); break;
						case IR::BinaryOperatorType::BIN_OR:				folded = new IR::Constant(*lhsVal | *rhsVal); break;
						case IR::BinaryOperatorType::BIN_XOR:				folded = new IR::Constant(*lhsVal ^ *rhsVal); break;
						case IR::BinaryOperatorType::EQUAL_TEST:			folded = new IR::Constant(IR::Value(foldedOrigin, state, *lhsVal == *rhsVal)); break;
						case IR::BinaryOperatorType::NOT_EQUAL_TEST:		folded = new IR::Constant(IR::Value(foldedOrigin, state, *lhsVal != *rhsVal)); break;
						case IR::BinaryOperatorType::GREATER_TEST:			folded = new IR::Constant(IR::Value(foldedOrigin, state, *lhsVal > *rhsVal)); break;
						case IR::BinaryOperatorType::GREATER_EQUAL_TEST:	folded = new IR::Constant(IR::Value(foldedOrigin, state, *lhsVal >= *rhsVal)); break;
						case IR::BinaryOperatorType::LESS_TEST:				folded = new IR::Constant(IR::Value(foldedOrigin, state, *lhsVal < *rhsVal)); break;
						case IR::BinaryOperatorType::LESS_EQUAL_TEST:		folded = new IR::Constant(IR::Value(foldedOrigin, state, *lhsVal <= *rhsVal)); break;
						case IR::BinaryOperatorType::BOOLEAN_AND:			folded = new IR::Constant(IR::Value(foldedOrigin, state, lhsVal->AsBool() != 0 && rhsVal->AsBool() != 0)); break;
						case IR::BinaryOperatorType::BOOLEAN_OR:			folded = new IR::Constant(IR::Value(foldedOrigin, state, lhsVal->AsBool() != 0 || rhsVal->AsBool() != 0)); break;
						default:
							CHECK(false);
							folded = new IR::Constant(IR::Value(foldedOrigin, state, -1));
							break;
						}
					}
					else
					{
						state->diagnosticContainer->AddDiagnostic(binaryOperation->span, "Types are not coercible");
					}

					lhsVal->Destroy();
					delete lhsVal;
					rhsVal->Destroy();
					delete rhsVal;
					return folded;
				}
				else
				{
					// Codegen needs simple operands (nested comparisons are split too, their results are bools)
					lhsVal = SplitIntoTemporary(lhsVal);
					rhsVal = SplitIntoTemporary(rhsVal);

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
				// Lowered to branches which write a temporary (so only the chosen side is evaluated)
				AST::TernaryOperation* ternary = (AST::TernaryOperation*)expression;

				Block* ifTrueBlock = new Block(state, ternary->ifTrue->span);
				Block* ifFalseBlock = new Block(state, ternary->ifFalse->span);
				Block* mergeBlock = new Block(state, ternary->span);

				if (!LowerConditionalBranch(ternary->condition, ternary->span, ifTrueBlock, ifFalseBlock))
				{
					delete ifTrueBlock;
					delete ifFalseBlock;
					delete mergeBlock;
					return nullptr;
				}

				std::string tempIdent = state->NextTemporary();

				state->PushInstructionBlock(ifTrueBlock);
				IR::Value* ifTrue = LowerExpression(ternary->ifTrue);
				state->WriteVariableInBlock(tempIdent, ifTrue);
				state->InsertionBlock()->AddBranch(ternary->span, mergeBlock);

				state->PushInstructionBlock(ifFalseBlock);
				IR::Value* ifFalse = LowerExpression(ternary->ifFalse);
				state->WriteVariableInBlock(tempIdent, ifFalse);
				state->InsertionBlock()->AddBranch(ternary->span, mergeBlock);

				state->PushInstructionBlock(mergeBlock);

				if (ifTrue == nullptr || ifFalse == nullptr)
				{
					return nullptr;
				}
				return new IR::Identifier(state, ternary->span, tempIdent);
			} break;
			case AST::StatementType::CALL:
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
							if (funcSig.argumentTypes[i] != IR::Value::Type::VOID_ &&
								state->GetValueType(arguments[i]) != funcSig.argumentTypes[i])
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

				if (m_BreakTargets.empty())
				{
					state->diagnosticContainer->AddDiagnostic(breakStatement->span, "Break statement outside of a loop");
					return nullptr;
				}

				// (Following statements go in an unreachable block)
				IR::Block* nextBlock = new IR::Block(state, breakStatement->span);

				state->InsertionBlock()->AddBranch(breakStatement->span, m_BreakTargets.back());
				state->InsertionBlock()->SealBlock();

				state->PushInstructionBlock(nextBlock);
				return nullptr;
			} break;
			case AST::StatementType::YIELD:
			{
				AST::YieldStatement* yieldStatement = (AST::YieldStatement*)expression;

				IR::Block* nextBlock = new IR::Block(state, yieldStatement->span);

				IR::Value* loweredVal = nullptr;
				if (yieldStatement->yieldValue != nullptr)
				{
					loweredVal = SplitIntoTemporary(LowerExpression(yieldStatement->yieldValue));
				}

				Block* yieldingBlock = state->InsertionBlock();
				yieldingBlock->AddYield(yieldStatement->span, loweredVal);
				if (yieldingBlock->terminator != nullptr && yieldingBlock->terminator->type == TerminatorType::YIELD)
				{
					// Execution resumes in the next block
					((YieldReturn*)yieldingBlock->terminator)->target = nextBlock;
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
						nextBlock->Destroy();
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
						Instruction popInst(OpCode::POP, VariantWrapper(VariantWrapper::Type::REGISTER, IR::Value((i32)j)), GetValueWrapperFromExpression(funcDecl->arguments[j]));
						instrBlock.PushBack(popInst);
					}

					//GenerateStatementInstructions(funcDecl->body->statements);
					state->PopInstructionBlock();
				}break;
				}
			}
		}

		VariantWrapper IntermediateRepresentation::GetValueWrapperFromExpression(AST::Expression* expression)
		{
			VariantWrapper valWrapper;

			if (IsLiteral(expression->statementType))
			{
				valWrapper = VariantWrapper(VariantWrapper::Type::CONSTANT, expression->GetValue());
			}
			else
			{
				switch (expression->statementType)
				{
				case AST::StatementType::IDENTIFIER:
				{
					//AST::Identifier* ident = (AST::Identifier*)expression;
					//i32 reg = state->varToRegisterMap[ident->identifierStr];
					//valWrapper = VariantWrapper(VariantWrapper::Type::REGISTER, IR::Value(reg));
				} break;
				case AST::StatementType::CALL:
				{
					AST::FunctionCall* funcCall = (AST::FunctionCall*)expression;
					i32 registerStored = GenerateCallInstruction(funcCall);
					valWrapper = VariantWrapper(VariantWrapper::Type::REGISTER, IR::Value(registerStored));
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
