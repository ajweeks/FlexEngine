#include "stdafx.hpp"

#include "StringBuilder.hpp"
#include "VirtualMachine/Frontend/Lexer.hpp"
#include "VirtualMachine/Frontend/Parser.hpp"
#include "VirtualMachine/Frontend/Token.hpp"

namespace flex
{
	const char* UnaryOperatorTypeToString(UnaryOperatorType opType)
	{
		return g_UnaryOperatorTypeStrings[(u32)opType];
	}

	const char* BinaryOperatorTypeToString(BinaryOperatorType opType)
	{
		return g_BinaryOperatorTypeStrings[(u32)opType];
	}

	bool IsCompoundAssignment(BinaryOperatorType type)
	{
		switch (type)
		{
		case BinaryOperatorType::ADD_ASSIGN:
		case BinaryOperatorType::SUB_ASSIGN:
		case BinaryOperatorType::MUL_ASSIGN:
		case BinaryOperatorType::DIV_ASSIGN:
		case BinaryOperatorType::MOD_ASSIGN:
			return true;
		}

		return false;
	}

	BinaryOperatorType Parse(Lexer& lexer)
	{
		Token token = lexer.Next();
		switch (token.kind)
		{
		case TokenKind::ASSIGNMENT:			return BinaryOperatorType::ASSIGN;
		case TokenKind::ADD:				return BinaryOperatorType::ADD;
		case TokenKind::ADD_ASSIGN:			return BinaryOperatorType::ADD_ASSIGN;
		case TokenKind::SUBTRACT:			return BinaryOperatorType::SUB;
		case TokenKind::SUBTRACT_ASSIGN:	return BinaryOperatorType::SUB_ASSIGN;
		case TokenKind::MULTIPLY:			return BinaryOperatorType::MUL;
		case TokenKind::MULTIPLY_ASSIGN:	return BinaryOperatorType::MUL_ASSIGN;
		case TokenKind::DIVIDE:				return BinaryOperatorType::DIV;
		case TokenKind::DIVIDE_ASSIGN:		return BinaryOperatorType::DIV_ASSIGN;
		case TokenKind::MODULO:				return BinaryOperatorType::MOD;
		case TokenKind::MODULO_ASSIGN:		return BinaryOperatorType::MOD_ASSIGN;
		case TokenKind::BINARY_AND:			return BinaryOperatorType::BIN_AND;
		case TokenKind::BINARY_OR:			return BinaryOperatorType::BIN_OR;
		case TokenKind::BINARY_XOR:			return BinaryOperatorType::BIN_XOR;
		case TokenKind::EQUAL_TEST:			return BinaryOperatorType::EQUAL_TEST;
		case TokenKind::NOT_EQUAL_TEST:		return BinaryOperatorType::NOT_EQUAL_TEST;
		case TokenKind::GREATER:			return BinaryOperatorType::GREATER_TEST;
		case TokenKind::GREATER_EQUAL:		return BinaryOperatorType::GREATER_EQUAL_TEST;
		case TokenKind::LESS:				return BinaryOperatorType::LESS_TEST;
		case TokenKind::LESS_EQUAL:			return BinaryOperatorType::LESS_EQUAL_TEST;
		case TokenKind::BOOLEAN_AND:		return BinaryOperatorType::BOOLEAN_AND;
		case TokenKind::BOOLEAN_OR:			return BinaryOperatorType::BOOLEAN_OR;
		default:
		{
			lexer.AddDiagnostic(token.span, lexer.sourceIter.lineNumber, lexer.sourceIter.columnIndex, "Unexpected binary operator");
			return BinaryOperatorType::_NONE;
		}
		}
	}

	/*
	BinaryOperatorType Parse(Lexer& lexer)
	{
		Token token = lexer.Next();
		switch (token.kind)
		{
		case TokenKind::ASSIGNMENT:			return BinaryOperatorType::ASSIGN;
		case TokenKind::ADD:				return BinaryOperatorType::ADD;
		case TokenKind::ADD_ASSIGN:			return BinaryOperatorType::ADD_ASSIGN;
		case TokenKind::SUBTRACT:			return BinaryOperatorType::SUB;
		case TokenKind::SUBTRACT_ASSIGN:	return BinaryOperatorType::SUB_ASSIGN;
		case TokenKind::MULTIPLY:			return BinaryOperatorType::MUL;
		case TokenKind::MULTIPLY_ASSIGN:	return BinaryOperatorType::MUL_ASSIGN;
		case TokenKind::DIVIDE:				return BinaryOperatorType::DIV;
		case TokenKind::DIVIDE_ASSIGN:		return BinaryOperatorType::DIV_ASSIGN;
		case TokenKind::MODULO:				return BinaryOperatorType::MOD;
		case TokenKind::MODULO_ASSIGN:		return BinaryOperatorType::MOD_ASSIGN;
		case TokenKind::BINARY_AND:			return BinaryOperatorType::BIN_AND;
		case TokenKind::BINARY_OR:			return BinaryOperatorType::BIN_OR;
		case TokenKind::BINARY_XOR:			return BinaryOperatorType::BIN_XOR;
		case TokenKind::EQUAL_TEST:			return BinaryOperatorType::EQUAL_TEST;
		case TokenKind::NOT_EQUAL_TEST:		return BinaryOperatorType::NOT_EQUAL_TEST;
		case TokenKind::GREATER:			return BinaryOperatorType::GREATER_TEST;
		case TokenKind::GREATER_EQUAL:		return BinaryOperatorType::GREATER_EQUAL_TEST;
		case TokenKind::LESS:				return BinaryOperatorType::LESS_TEST;
		case TokenKind::LESS_EQUAL:			return BinaryOperatorType::LESS_EQUAL_TEST;
		case TokenKind::BOOLEAN_AND:		return BinaryOperatorType::BOOLEAN_AND;
		case TokenKind::BOOLEAN_OR:			return BinaryOperatorType::BOOLEAN_OR;
		default:
		{
			lexer.AddDiagnostic(token.span, lexer.sourceIter.lineNumber, lexer.sourceIter.columnIndex, "Unexpected binary operator");
			return BinaryOperatorType::_NONE;
		}
		}
	}
	*/

	TypeName Type::GetTypeNameFromStr(const std::string& str)
	{
		const char* tokenCStr = str.c_str();
		for (i32 i = 0; i < (i32)TypeName::_NONE; ++i)
		{
			if (strcmp(g_TypeNameStrings[i], tokenCStr) == 0)
			{
				return (TypeName)i;
			}
		}
		return TypeName::_NONE;
	}

	Statement::Statement(const Span& span) :
		span(span)
	{
	}

	StatementBlock::StatementBlock(const Span& span) :
		Statement(span)
	{
	}

	void StatementBlock::Push(Statement* statement)
	{
		statements.push_back(statement);
	}

	std::string StatementBlock::ToString() const
	{
		StringBuilder stringBuilder;

		stringBuilder.Append("{\n");
		for (Statement* statement : statements)
		{
			stringBuilder.Append(statement->ToString());
			if (dynamic_cast<Expression*>(statement))
			{
				stringBuilder.Append(";\n");
			}
		}
		stringBuilder.Append("\n}");

		return stringBuilder.ToString();
	}

	TypeName ValueTypeToTypeName(ValueType valueType)
	{
		switch (valueType)
		{
		case ValueType::INT_RAW: return TypeName::INT;
		case ValueType::FLOAT_RAW: return TypeName::FLOAT;
		case ValueType::BOOL_RAW: return TypeName::BOOL;
		default: return TypeName::_NONE;
		}
	}

	ValueType TypeNameToValueType(TypeName typeName)
	{
		switch (typeName)
		{
		case TypeName::INT: return ValueType::INT_RAW;
		case TypeName::FLOAT: return ValueType::FLOAT_RAW;
		case TypeName::BOOL: return ValueType::BOOL_RAW;
		default: return ValueType::_NONE;
		}
	}

	/*
	template<class T>
	Value* EvaluateUnaryOperation(T* val, UnaryOperatorType op)
	{
		switch (op)
		{
		case UnaryOperatorType::NEGATE:			return new Value(-(*val), true);
		default:							return nullptr;
		}
	}

	template<class T>
	Value* EvaluateOperation(T* lhs, T* rhs, BinaryOperatorType op)
	{
		switch (op)
		{
		case BinaryOperatorType::ASSIGN:
			*lhs = *rhs;
			return nullptr;
		case BinaryOperatorType::ADD:				return new Value(*lhs + *rhs, true);
		case BinaryOperatorType::SUB:				return new Value(*lhs - *rhs, true);
		case BinaryOperatorType::MUL:				return new Value(*lhs * *rhs, true);
		case BinaryOperatorType::DIV:
			// :grimmacing:
			if (typeid(*lhs) == typeid(i32))
			{
				return new Value((i32)*lhs / (i32)*rhs, true);
			}
			else if (typeid(*lhs) == typeid(real))
			{
				return new Value((real)*lhs / (real)*rhs, true);
			}
			else
			{
				return nullptr;
			}
		case OperatorType::MOD:
			if (typeid(*lhs) == typeid(real))
			{
				return new Value(fmod((real)*lhs, (real)*rhs), true);
			}
			else
			{
				return new Value((i32)*lhs % (i32)*rhs, true);
			}
		case BinaryOperatorType::BIN_AND:			return new Value((bool)*lhs & (bool)*rhs, true);
		case BinaryOperatorType::BIN_OR:			return new Value((bool)*lhs | (bool)*rhs, true);
		case BinaryOperatorType::BIN_XOR:			return new Value((bool)*lhs ^ (bool)*rhs, true);
		case BinaryOperatorType::EQUAL_TEST:			return new Value(*lhs == *rhs, true);
		case BinaryOperatorType::NOT_EQUAL_TEST:		return new Value(*lhs != *rhs, true);
		case BinaryOperatorType::GREATER_TEST:			return new Value(*lhs > * rhs, true);
		case BinaryOperatorType::GREATER_EQUAL_TEST:	return new Value(*lhs >= *rhs, true);
		case BinaryOperatorType::LESS_TEST:			return new Value(*lhs < *rhs, true);
		case BinaryOperatorType::LESS_EQUAL_TEST:		return new Value(*lhs <= *rhs, true);
		case BinaryOperatorType::BOOLEAN_AND:		return new Value(*lhs && *rhs, true);
		case BinaryOperatorType::BOOLEAN_OR:		return new Value(*lhs || *rhs, true);
		default:							return nullptr;
		}
	}
	*/

	std::string IfStatement::ToString() const
	{
		StringBuilder stringBuilder;

		stringBuilder.Append("if (");
		stringBuilder.Append(condition->ToString());
		stringBuilder.Append(") {\n");
		stringBuilder.Append(then->ToString());
		stringBuilder.Append("\n}");
		if (otherwise != nullptr)
		{
			stringBuilder.Append(otherwise->ToString());
		}

		return stringBuilder.ToString();
	}

	std::string ForStatement::ToString() const
	{
		StringBuilder stringBuilder;

		stringBuilder.Append("for (");
		stringBuilder.Append(setup->ToString());
		stringBuilder.Append(";");
		stringBuilder.Append(condition->ToString());
		stringBuilder.Append(";");
		stringBuilder.Append(update->ToString());
		stringBuilder.Append(") {\n");
		stringBuilder.Append(body->ToString());
		stringBuilder.Append("\n}");

		return stringBuilder.ToString();
	}

	std::string WhileStatement::ToString() const
	{
		StringBuilder stringBuilder;

		stringBuilder.Append("while (");
		stringBuilder.Append(condition->ToString());
		stringBuilder.Append(") {\n");
		stringBuilder.Append(body->ToString());
		stringBuilder.Append("\n}");

		return stringBuilder.ToString();
	}

	std::string DoWhileStatement::ToString() const
	{
		StringBuilder stringBuilder;

		stringBuilder.Append("do {\n");
		stringBuilder.Append(body->ToString());
		stringBuilder.Append("\n} while (");
		stringBuilder.Append(condition->ToString());
		stringBuilder.Append(")");

		return stringBuilder.ToString();
	}

	std::string UnaryOperation::ToString() const
	{
		StringBuilder stringBuilder;

		switch (type)
		{
		case UnaryOperatorType::PLUS:
			stringBuilder.Append('+');
			break;
		case UnaryOperatorType::NEGATE:
			stringBuilder.Append('-');
			break;
		case UnaryOperatorType::NOT:
			stringBuilder.Append('!');
			break;
		default:
			assert(false);
		}

		stringBuilder.Append(expression->ToString());

		return stringBuilder.ToString();
	}

	std::string FunctionCall::ToString() const
	{
		StringBuilder stringBuilder;

		stringBuilder.Append(target);
		stringBuilder.Append("(");
		for (u32 i = 0; i < (u32)arguments.size(); ++i)
		{
			stringBuilder.Append(arguments[i]->ToString());

			if (i < (u32)arguments.size() - 1)
			{
				stringBuilder.Append(", ");
			}

		}
		stringBuilder.Append(")");

		return stringBuilder.ToString();
	}

	AST::AST(Lexer* lexer) :
		lexer(lexer)
	{
	}

	void AST::Destroy()
	{
		delete rootBlock;
		rootBlock = nullptr;
		bValid = false;
	}

	void AST::Generate()
	{
		delete rootBlock;

		bValid = false;

		rootBlock = lexer->Parse();

		bValid = true;
	}
} // namespace flex
