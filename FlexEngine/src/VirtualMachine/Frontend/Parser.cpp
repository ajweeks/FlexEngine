#include "stdafx.hpp"

#include "StringBuilder.hpp"
#include "VirtualMachine/Frontend/Lexer.hpp"
#include "VirtualMachine/Frontend/Parser.hpp"
#include "VirtualMachine/Frontend/Token.hpp"

namespace flex
{
	static Identifier* g_InvalidIdentifier = (Identifier*)0xFFFFFFFFFFFFFFFF;

	const char* UnaryOperatorTypeToString(UnaryOperatorType opType)
	{
		return g_UnaryOperatorTypeStrings[(u32)opType];
	}

	UnaryOperatorType TokenKindToUnaryOperatorType(TokenKind tokenKind)
	{
		switch (tokenKind)
		{
		case TokenKind::PLUS:			return UnaryOperatorType::PLUS;
		case TokenKind::MINUS:			return UnaryOperatorType::NEGATE;
		case TokenKind::BANG:			return UnaryOperatorType::NOT;
		case TokenKind::TILDE:			return UnaryOperatorType::BIN_INVERT;
		default: return UnaryOperatorType::_NONE;
		}
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
		case BinaryOperatorType::BIN_AND_ASSIGN:
		case BinaryOperatorType::BIN_OR_ASSIGN:
		case BinaryOperatorType::BIN_XOR_ASSIGN:
			return true;
		}

		return false;
	}

	BinaryOperatorType GetNonCompoundType(BinaryOperatorType type)
	{

		switch (type)
		{
		case BinaryOperatorType::ADD_ASSIGN:		return BinaryOperatorType::ADD;
		case BinaryOperatorType::SUB_ASSIGN:		return BinaryOperatorType::SUB;
		case BinaryOperatorType::MUL_ASSIGN:		return BinaryOperatorType::MUL;
		case BinaryOperatorType::DIV_ASSIGN:		return BinaryOperatorType::DIV;
		case BinaryOperatorType::MOD_ASSIGN:		return BinaryOperatorType::MOD;
		case BinaryOperatorType::BIN_AND_ASSIGN:	return BinaryOperatorType::BIN_AND;
		case BinaryOperatorType::BIN_OR_ASSIGN:		return BinaryOperatorType::BIN_OR;
		case BinaryOperatorType::BIN_XOR_ASSIGN:	return BinaryOperatorType::BIN_XOR;
		default: return BinaryOperatorType::_NONE;
		}

	}

	BinaryOperatorType TokenKindToBinaryOperatorType(TokenKind tokenKind)
	{
		switch (tokenKind)
		{
		case TokenKind::EQUALS:				return BinaryOperatorType::ASSIGN;
		case TokenKind::PLUS:				return BinaryOperatorType::ADD;
		case TokenKind::PLUS_EQUALS:		return BinaryOperatorType::ADD_ASSIGN;
		case TokenKind::MINUS:				return BinaryOperatorType::SUB;
		case TokenKind::MINUS_EQUALS:		return BinaryOperatorType::SUB_ASSIGN;
		case TokenKind::STAR:				return BinaryOperatorType::MUL;
		case TokenKind::STAR_EQUALS:		return BinaryOperatorType::MUL_ASSIGN;
		case TokenKind::FOR_SLASH:			return BinaryOperatorType::DIV;
		case TokenKind::FOR_SLASH_EQUALS:	return BinaryOperatorType::DIV_ASSIGN;
		case TokenKind::PERCENT:			return BinaryOperatorType::MOD;
		case TokenKind::PERCENT_EQUALS:		return BinaryOperatorType::MOD_ASSIGN;
		case TokenKind::BINARY_AND:			return BinaryOperatorType::BIN_AND;
		case TokenKind::BINARY_AND_EQUALS:	return BinaryOperatorType::BIN_AND_ASSIGN;
		case TokenKind::BINARY_OR:			return BinaryOperatorType::BIN_OR;
		case TokenKind::BINARY_OR_EQUALS:	return BinaryOperatorType::BIN_OR_ASSIGN;
		case TokenKind::BINARY_XOR:			return BinaryOperatorType::BIN_XOR;
		case TokenKind::BINARY_XOR_EQUALS:	return BinaryOperatorType::BIN_XOR_ASSIGN;
		case TokenKind::EQUAL_EQUAL:		return BinaryOperatorType::EQUAL_TEST;
		case TokenKind::NOT_EQUAL:			return BinaryOperatorType::NOT_EQUAL_TEST;
		case TokenKind::GREATER:			return BinaryOperatorType::GREATER_TEST;
		case TokenKind::GREATER_EQUAL:		return BinaryOperatorType::GREATER_EQUAL_TEST;
		case TokenKind::LESS:				return BinaryOperatorType::LESS_TEST;
		case TokenKind::LESS_EQUAL:			return BinaryOperatorType::LESS_EQUAL_TEST;
		case TokenKind::BOOLEAN_AND:		return BinaryOperatorType::BOOLEAN_AND;
		case TokenKind::BOOLEAN_OR:			return BinaryOperatorType::BOOLEAN_OR;
		default: return BinaryOperatorType::_NONE;
		}
	}

	i32 GetBinaryOperatorPrecedence(TokenKind tokenKind)
	{
		switch (tokenKind)
		{
		case TokenKind::STAR:
		case TokenKind::FOR_SLASH:
		case TokenKind::PERCENT:
			return 1100;
		case TokenKind::PLUS:
		case TokenKind::MINUS:
			return 1000;
		case TokenKind::GREATER:
		case TokenKind::GREATER_EQUAL:
		case TokenKind::LESS:
		case TokenKind::LESS_EQUAL:
			return 900;
		case TokenKind::EQUAL_EQUAL:
		case TokenKind::NOT_EQUAL:
			return 800;
		case TokenKind::BINARY_AND:
			return 700;
		case TokenKind::BINARY_XOR:
			return 600;
		case TokenKind::BINARY_OR:
			return 500;
		case TokenKind::BOOLEAN_AND:
			return 400;
		case TokenKind::BOOLEAN_OR:
			return 300;
		case TokenKind::EQUALS:
		case TokenKind::PLUS_EQUALS:
		case TokenKind::MINUS_EQUALS:
		case TokenKind::STAR_EQUALS:
		case TokenKind::FOR_SLASH_EQUALS:
		case TokenKind::PERCENT_EQUALS:
			return 200;
		case TokenKind::COMMA:
			return 100;
		default:
			return -1;
		}
	}

	std::string TypeNameToString(TypeName typeName)
	{
		return g_TypeNameStrings[(u32)typeName];
	}

	TypeName TokenKindToTypeName(TokenKind tokenKind)
	{
		switch (tokenKind)
		{
		case TokenKind::INT_KEYWORD:	return TypeName::INT;
		case TokenKind::FLOAT_KEYWORD:	return TypeName::FLOAT;
		case TokenKind::BOOL_KEYWORD:	return TypeName::BOOL;
		case TokenKind::STRING_KEYWORD:	return TypeName::STRING;
		case TokenKind::CHAR_KEYWORD:	return TypeName::CHAR;
		default: return TypeName::_NONE;
		}
	}

	TypeName TypeNameToListVariant(TypeName baseType)
	{
		switch (baseType)
		{
		case TypeName::INT:		return TypeName::INT_LIST;
		case TypeName::FLOAT:	return TypeName::FLOAT_LIST;
		case TypeName::BOOL:	return TypeName::BOOL_LIST;
		case TypeName::STRING:	return TypeName::STRING_LIST;
		case TypeName::CHAR:	return TypeName::CHAR_LIST;
		default: return TypeName::_NONE;
		}
	}

	TypeName ValueTypeToTypeName(ValueType valueType)
	{
		switch (valueType)
		{
		case ValueType::INT_RAW:	return TypeName::INT;
		case ValueType::FLOAT_RAW:	return TypeName::FLOAT;
		case ValueType::BOOL_RAW:	return TypeName::BOOL;
		default: return TypeName::_NONE;
		}
	}

	ValueType TypeNameToValueType(TypeName typeName)
	{
		switch (typeName)
		{
		case TypeName::INT:		return ValueType::INT_RAW;
		case TypeName::FLOAT:	return ValueType::FLOAT_RAW;
		case TypeName::BOOL:	return ValueType::BOOL_RAW;
		default: return ValueType::_NONE;
		}
	}

	bool CanBeReduced(StatementType statementType)
	{
		return statementType == StatementType::UNARY_OPERATION ||
			statementType == StatementType::BINARY_OPERATION ||
			statementType == StatementType::TERNARY_OPERATION ||
			statementType == StatementType::FUNC_CALL ||
			statementType == StatementType::INDEX_OPERATION;
	}

	Statement::Statement(const Span& span, StatementType statementType) :
		span(span),
		statementType(statementType)
	{
	}

	Identifier* Statement::RewriteCompoundStatements(Parser* parser, std::vector<Statement*>& tmpStatements)
	{
		UNREFERENCED_PARAMETER(parser);
		UNREFERENCED_PARAMETER(tmpStatements);
		return nullptr;
	}

	StatementBlock::StatementBlock(const Span& span, const std::vector<Statement*>& statements) :
		Statement(span, StatementType::STATEMENT_BLOCK),
		statements(statements)
	{
	}

	StatementBlock::~StatementBlock()
	{
		for (u32 i = 0; i < (u32)statements.size(); ++i)
		{
			delete statements[i];
		}
	}

	std::string StatementBlock::ToString() const
	{
		StringBuilder stringBuilder;

		// Don't print brackets for root block
		bool bBrackets = (span.low > 0);

		if (bBrackets)
		{
			stringBuilder.Append("{\n");
		}
		for (Statement* statement : statements)
		{
			stringBuilder.Append(statement->ToString());
			stringBuilder.Append("\n");
		}
		if (bBrackets)
		{
			stringBuilder.Append("}");
		}

		return stringBuilder.ToString();
	}

	Identifier* StatementBlock::RewriteCompoundStatements(Parser* parser, std::vector<Statement*>& tmpStatements)
	{
		for (u32 i = 0; i < (u32)statements.size(); /* */)
		{
			Identifier* tmpIdent = statements[i]->RewriteCompoundStatements(parser, tmpStatements);

			if (tmpIdent != nullptr)
			{
				if (tmpIdent == g_InvalidIdentifier)
				{
					delete statements[i];
					statements.erase(statements.begin() + i);
				}
				else
				{
					statements[i] = tmpIdent;
				}
			}

			if (tmpStatements.empty())
			{
				++i;
			}
			else
			{
				statements.insert(statements.begin() + i, tmpStatements.begin(), tmpStatements.end());
				i += (u32)tmpStatements.size();
				tmpStatements.clear();
			}
		}

		return nullptr;
	}

	IfStatement::~IfStatement()
	{
		delete condition;
		delete then;
		delete otherwise;
	}

	std::string IfStatement::ToString() const
	{
		StringBuilder stringBuilder;

		stringBuilder.Append("if (");
		stringBuilder.Append(condition->ToString());
		stringBuilder.Append(")\n");
		stringBuilder.Append(then->ToString());
		if (otherwise != nullptr)
		{
			if (dynamic_cast<IfStatement*>(otherwise) != nullptr)
			{
				stringBuilder.Append("\nelif (");
			}
			else
			{
				stringBuilder.Append("\nelse\n");
			}
			stringBuilder.Append(otherwise->ToString());
		}

		return stringBuilder.ToString();
	}

	Identifier* IfStatement::RewriteCompoundStatements(Parser* parser, std::vector<Statement*>& tmpStatements)
	{
		Identifier* newCondition = condition->RewriteCompoundStatements(parser, tmpStatements);
		if (newCondition != nullptr)
		{
			condition = newCondition;
		}

		Identifier* newThen = then->RewriteCompoundStatements(parser, tmpStatements);
		if (newThen != nullptr)
		{
			then = newThen;

		}

		Identifier* newOtherwise = otherwise->RewriteCompoundStatements(parser, tmpStatements);
		if (newOtherwise != nullptr)
		{
			otherwise = newOtherwise;
		}

		return nullptr;
	}

	ForStatement::~ForStatement()
	{
		delete setup;
		delete condition;
		delete update;
		delete body;
	}

	std::string ForStatement::ToString() const
	{
		StringBuilder stringBuilder;

		stringBuilder.Append("for (");
		if (setup != nullptr)
		{
			stringBuilder.Append(setup->ToString());
		}
		stringBuilder.Append(";");
		if (condition != nullptr)
		{
			stringBuilder.Append(condition->ToString());
		}
		stringBuilder.Append(";");
		if (update != nullptr)
		{
			stringBuilder.Append(update->ToString());
		}
		stringBuilder.Append(")\n");
		stringBuilder.Append(body->ToString());

		return stringBuilder.ToString();
	}

	Identifier* ForStatement::RewriteCompoundStatements(Parser* parser, std::vector<Statement*>& tmpStatements)
	{
		Identifier* newSetup = setup->RewriteCompoundStatements(parser, tmpStatements);
		if (newSetup != nullptr)
		{
			setup = newSetup;
		}

		Identifier* newCondition = condition->RewriteCompoundStatements(parser, tmpStatements);
		if (newCondition != nullptr)
		{
			condition = newCondition;
		}

		Identifier* newUpdate = update->RewriteCompoundStatements(parser, tmpStatements);
		if (newUpdate != nullptr)
		{
			update = newUpdate;
		}

		Identifier* newBody = body->RewriteCompoundStatements(parser, tmpStatements);
		if (newBody != nullptr)
		{
			body = newBody;
		}

		return nullptr;
	}

	WhileStatement::~WhileStatement()
	{
		delete condition;
		delete body;
	}

	std::string WhileStatement::ToString() const
	{
		StringBuilder stringBuilder;

		stringBuilder.Append("while (");
		stringBuilder.Append(condition->ToString());
		stringBuilder.Append(")\n");
		stringBuilder.Append(body->ToString());

		return stringBuilder.ToString();
	}

	Identifier* WhileStatement::RewriteCompoundStatements(Parser* parser, std::vector<Statement*>& tmpStatements)
	{
		Identifier* conditionTempIdent = condition->RewriteCompoundStatements(parser, tmpStatements);
		if (conditionTempIdent != nullptr)
		{
			condition = conditionTempIdent;
		}

		Identifier* bodyTempIdent = body->RewriteCompoundStatements(parser, tmpStatements);
		if (bodyTempIdent != nullptr)
		{
			body = bodyTempIdent;
		}

		return nullptr;
	}

	DoWhileStatement::~DoWhileStatement()
	{
		delete condition;
		delete body;
	}

	std::string DoWhileStatement::ToString() const
	{
		StringBuilder stringBuilder;

		stringBuilder.Append("do\n");
		stringBuilder.Append(body->ToString());
		stringBuilder.Append(" while (");
		stringBuilder.Append(condition->ToString());
		stringBuilder.Append(");");

		return stringBuilder.ToString();
	}

	Identifier* DoWhileStatement::RewriteCompoundStatements(Parser* parser, std::vector<Statement*>& tmpStatements)
	{
		Identifier* conditionTempIdent = condition->RewriteCompoundStatements(parser, tmpStatements);
		if (conditionTempIdent != nullptr)
		{
			condition = conditionTempIdent;
		}

		Identifier* bodyTempIdent = body->RewriteCompoundStatements(parser, tmpStatements);
		if (bodyTempIdent != nullptr)
		{
			body = bodyTempIdent;
		}

		return nullptr;
	}

	FunctionDeclaration::~FunctionDeclaration()
	{
		for (u32 i = 0; i < (u32)arguments.size(); ++i)
		{
			delete arguments[i];
		}

		delete body;
	}

	std::string FunctionDeclaration::ToString() const
	{
		StringBuilder stringBuilder;

		stringBuilder.Append("func ");
		stringBuilder.Append(name);
		stringBuilder.Append("(");
		for (u32 i = 0; i < (u32)arguments.size(); ++i)
		{
			stringBuilder.Append(TypeNameToString(arguments[i]->typeName));
			stringBuilder.Append(" ");
			stringBuilder.Append(arguments[i]->ToString());
			if (i < (u32)arguments.size() - 1)
			{
				stringBuilder.Append(", ");
			}
		}
		stringBuilder.Append(") -> ");
		stringBuilder.Append(TypeNameToString(returnType));
		stringBuilder.Append(" ");
		stringBuilder.Append(body->ToString());

		return stringBuilder.ToString();
	}

	Identifier* FunctionDeclaration::RewriteCompoundStatements(Parser* parser, std::vector<Statement*>& tmpStatements)
	{
		body->RewriteCompoundStatements(parser, tmpStatements);
		return nullptr;
	}

	YieldStatement::~YieldStatement()
	{
		delete yieldValue;
	}

	std::string YieldStatement::ToString() const
	{
		StringBuilder stringBuilder;

		stringBuilder.Append("yield");

		if (yieldValue != nullptr)
		{
			stringBuilder.Append(" ");
			stringBuilder.Append(yieldValue->ToString());
		}

		stringBuilder.Append(";");

		return stringBuilder.ToString();
	}

	Identifier* YieldStatement::RewriteCompoundStatements(Parser* parser, std::vector<Statement*>& tmpStatements)
	{
		if (yieldValue != nullptr)
		{
			Identifier* yieldValueTempIdent = yieldValue->RewriteCompoundStatements(parser, tmpStatements);
			if (yieldValueTempIdent != nullptr)
			{
				yieldValue = yieldValueTempIdent;
			}
		}

		return nullptr;
	}

	ReturnStatement::~ReturnStatement()
	{
		delete returnValue;
	}

	std::string ReturnStatement::ToString() const
	{
		StringBuilder stringBuilder;

		stringBuilder.Append("return");

		if (returnValue != nullptr)
		{
			stringBuilder.Append(" ");
			stringBuilder.Append(returnValue->ToString());
		}

		stringBuilder.Append(";");

		return stringBuilder.ToString();
	}

	Identifier* ReturnStatement::RewriteCompoundStatements(Parser* parser, std::vector<Statement*>& tmpStatements)
	{
		if (returnValue != nullptr)
		{
			Identifier* returnValueTempIdent = returnValue->RewriteCompoundStatements(parser, tmpStatements);
			if (returnValueTempIdent != nullptr)
			{
				returnValue = returnValueTempIdent;
			}
		}

		return nullptr;
	}

	Declaration::~Declaration()
	{
		delete initializer;
	}

	std::string Declaration::ToString() const
	{
		StringBuilder stringBuilder;

		stringBuilder.Append(TypeNameToString(typeName));
		stringBuilder.Append(" ");
		stringBuilder.Append(identifierStr);

		if (initializer != nullptr)
		{
			stringBuilder.Append(" = ");
			stringBuilder.Append(initializer->ToString());
		}

		stringBuilder.Append(";");

		return stringBuilder.ToString();
	}

	Identifier* Declaration::RewriteCompoundStatements(Parser* parser, std::vector<Statement*>& tmpStatements)
	{
		Identifier* initializerTempIdent = initializer->RewriteCompoundStatements(parser, tmpStatements);
		if (initializerTempIdent != nullptr)
		{
			initializer = initializerTempIdent;
		}

		return nullptr;
	}

	Assignment::~Assignment()
	{
		delete rhs;
	}

	std::string Assignment::ToString() const
	{
		StringBuilder stringBuilder;

		stringBuilder.Append(lhs);
		stringBuilder.Append(" = ");
		stringBuilder.Append(rhs->ToString());
		stringBuilder.Append(";");

		return stringBuilder.ToString();
	}

	Identifier* Assignment::RewriteCompoundStatements(Parser* parser, std::vector<Statement*>& tmpStatements)
	{
		Identifier* rhsTempIdent = rhs->RewriteCompoundStatements(parser, tmpStatements);
		if (rhsTempIdent != nullptr)
		{
			rhs = rhsTempIdent;
		}

		return nullptr;
	}

	CompoundAssignment::~CompoundAssignment()
	{
		delete rhs;
	}

	std::string CompoundAssignment::ToString() const
	{

		StringBuilder stringBuilder;

		stringBuilder.Append(lhs);
		stringBuilder.Append(" ");
		stringBuilder.Append(BinaryOperatorTypeToString(operatorType));
		stringBuilder.Append(" ");
		stringBuilder.Append(rhs->ToString());
		stringBuilder.Append(";");

		return stringBuilder.ToString();
	}

	Identifier* CompoundAssignment::RewriteCompoundStatements(Parser* parser, std::vector<Statement*>& tmpStatements)
	{
		assert(IsCompoundAssignment(operatorType));

		Identifier* rhsTempIdent = rhs->RewriteCompoundStatements(parser, tmpStatements);
		if (rhsTempIdent != nullptr)
		{
			rhs = rhsTempIdent;
		}

		Span newSpan(span.low, span.high);
		newSpan.source = Span::Source::GENERATED;

		// e.g.
		// a += b
		//
		// becomes:
		//
		// int tmp = a + b;
		// a = tmp;

		BinaryOperation* simpleBinOp = new BinaryOperation(newSpan, GetNonCompoundType(operatorType), new Identifier(newSpan, lhs), rhs);
		Declaration* tempResult = new Declaration(span, parser->NextTempIdentifier(), simpleBinOp, typeName);

		tmpStatements.push_back(tempResult);

		Assignment* newAssignment = new Assignment(span, lhs, new Identifier(span, tempResult->identifierStr));
		tmpStatements.push_back(newAssignment);

		// Clear out members to prepare for our deletion
		lhs = "";
		rhs = nullptr;
		return g_InvalidIdentifier;
	}

	ListInitializer::~ListInitializer()
	{
		for (u32 i = 0; i < (u32)listValues.size(); ++i)
		{
			delete listValues[i];
		}
	}

	std::string ListInitializer::ToString() const
	{
		StringBuilder stringBuilder;

		stringBuilder.Append("{ ");
		for (u32 i = 0; i < (u32)listValues.size(); ++i)
		{
			stringBuilder.Append(listValues[i]->ToString());
			if (i < (u32)listValues.size() - 1)
			{
				stringBuilder.Append(", ");
			}
		}
		stringBuilder.Append(" }");

		return stringBuilder.ToString();
	}

	Identifier* ListInitializer::RewriteCompoundStatements(Parser* parser, std::vector<Statement*>& tmpStatements)
	{
		for (u32 i = 0; i < (u32)listValues.size(); ++i)
		{
			Identifier* listValueTempIdent = listValues[i]->RewriteCompoundStatements(parser, tmpStatements);
			if (listValueTempIdent != nullptr)
			{
				listValues[i] = listValueTempIdent;
			}
		}

		return nullptr;
	}

	IndexOperation::~IndexOperation()
	{
		delete indexExpression;
	}

	std::string IndexOperation::ToString() const
	{
		StringBuilder stringBuilder;
		stringBuilder.Append(container);
		stringBuilder.Append("[");
		stringBuilder.Append(indexExpression->ToString());
		stringBuilder.Append("]");
		return stringBuilder.ToString();
	}

	Identifier* IndexOperation::RewriteCompoundStatements(Parser* parser, std::vector<Statement*>& tmpStatements)
	{
		Identifier* indexExpressionTempIdent = indexExpression->RewriteCompoundStatements(parser, tmpStatements);
		if (indexExpressionTempIdent != nullptr)
		{
			indexExpression = indexExpressionTempIdent;
		}

		Declaration* tmpDecl = new Declaration(span, parser->NextTempIdentifier(), this, typeName);
		tmpStatements.push_back(tmpDecl);

		return new Identifier(span, tmpDecl->identifierStr);
	}

	UnaryOperation::~UnaryOperation()
	{
		delete expression;
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
		case UnaryOperatorType::BIN_INVERT:
			stringBuilder.Append('~');
			break;
		default:
			assert(false);
		}

		stringBuilder.Append(expression->ToString());

		return stringBuilder.ToString();
	}

	Identifier* UnaryOperation::RewriteCompoundStatements(Parser* parser, std::vector<Statement*>& tmpStatements)
	{
		Span newSpan(span.low, span.high);
		newSpan.source = Span::Source::GENERATED;

		Identifier* expressionTempIdent = expression->RewriteCompoundStatements(parser, tmpStatements);
		if (expressionTempIdent != nullptr)
		{
			expression = expressionTempIdent;
		}

		Declaration* tmpDecl = new Declaration(span, parser->NextTempIdentifier(), this, typeName);
		tmpStatements.push_back(tmpDecl);

		return new Identifier(span, tmpDecl->identifierStr);
	}

	BinaryOperation::~BinaryOperation()
	{
		delete lhs;
		delete rhs;
	}

	std::string BinaryOperation::ToString() const
	{
		StringBuilder stringBuilder;

		stringBuilder.Append("(");
		stringBuilder.Append(lhs->ToString());
		stringBuilder.Append(" ");
		stringBuilder.Append(BinaryOperatorTypeToString(type));
		stringBuilder.Append(" ");
		stringBuilder.Append(rhs->ToString());
		stringBuilder.Append(")");

		return stringBuilder.ToString();
	}

	Identifier* BinaryOperation::RewriteCompoundStatements(Parser* parser, std::vector<Statement*>& tmpStatements)
	{
		Span newSpan(span.low, span.high);
		newSpan.source = Span::Source::GENERATED;

		Identifier* lhsTempIdent = lhs->RewriteCompoundStatements(parser, tmpStatements);
		if (lhsTempIdent != nullptr)
		{
			lhs = lhsTempIdent;
		}

		Identifier* rhsTempIdent = rhs->RewriteCompoundStatements(parser, tmpStatements);
		if (rhsTempIdent != nullptr)
		{
			rhs = lhsTempIdent;
		}

		Declaration* tmpDecl = new Declaration(span, parser->NextTempIdentifier(), this, typeName);
		tmpStatements.push_back(tmpDecl);

		return new Identifier(span, tmpDecl->identifierStr);
	}

	TernaryOperation::~TernaryOperation()
	{
		delete condition;
		delete ifTrue;
		delete ifFalse;
	}

	std::string TernaryOperation::ToString() const
	{
		StringBuilder stringBuilder;

		stringBuilder.Append(condition->ToString());
		stringBuilder.Append(" ? ");
		stringBuilder.Append(ifTrue->ToString());
		stringBuilder.Append(" : ");
		stringBuilder.Append(ifFalse->ToString());

		return stringBuilder.ToString();
	}

	Identifier* TernaryOperation::RewriteCompoundStatements(Parser* parser, std::vector<Statement*>& tmpStatements)
	{
		Span newSpan(span.low, span.high);
		newSpan.source = Span::Source::GENERATED;

		Identifier* conditionTempIdent = condition->RewriteCompoundStatements(parser, tmpStatements);
		if (conditionTempIdent != nullptr)
		{
			condition = conditionTempIdent;
		}

		Identifier* ifTrueTempIdent = ifTrue->RewriteCompoundStatements(parser, tmpStatements);
		if (ifTrueTempIdent != nullptr)
		{
			ifTrue = ifTrueTempIdent;
		}

		Identifier* ifFalseTempIdent = ifFalse->RewriteCompoundStatements(parser, tmpStatements);
		if (ifFalseTempIdent != nullptr)
		{
			ifFalse = ifFalseTempIdent;
		}

		Declaration* tmpDecl = new Declaration(span, parser->NextTempIdentifier(), this, typeName);
		tmpStatements.push_back(tmpDecl);

		return new Identifier(span, tmpDecl->identifierStr);
	}

	FunctionCall::~FunctionCall()
	{
		for (u32 i = 0; i < (u32)arguments.size(); ++i)
		{
			delete arguments[i];
		}
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

	Identifier* FunctionCall::RewriteCompoundStatements(Parser* parser, std::vector<Statement*>& tmpStatements)
	{
		for (u32 i = 0; i < (u32)arguments.size(); ++i)
		{
			Identifier* argTempIdent = arguments[i]->RewriteCompoundStatements(parser, tmpStatements);
			if (argTempIdent != nullptr)
			{
				arguments[i] = argTempIdent;
			}
		}

		// TODO: Determine called function's return type to know if we need to store the result or not.

		Declaration* tmpDecl = new Declaration(span, parser->NextTempIdentifier(), this, typeName);
		tmpStatements.push_back(tmpDecl);

		return new Identifier(span, tmpDecl->identifierStr);
	}

	Parser::Parser(Lexer* lexer, DiagnosticContainer* diagnosticContainer) :
		m_Lexer(lexer),
		diagnosticContainer(diagnosticContainer)
	{
		m_Current = m_Lexer->Next();
	}

	bool Parser::HasNext()
	{
		return !m_Lexer->sourceIter.EndOfFileReached();
	}

	bool Parser::NextIs(TokenKind tokenKind)
	{
		return m_Current.kind == tokenKind;
	}

	bool Parser::NextIsTypename()
	{
		return NextIs(TokenKind::INT_KEYWORD) ||
			NextIs(TokenKind::FLOAT_KEYWORD) ||
			NextIs(TokenKind::BOOL_KEYWORD) ||
			NextIs(TokenKind::STRING_KEYWORD) ||
			NextIs(TokenKind::CHAR_KEYWORD);
	}

	bool Parser::NextIsBinaryOperator()
	{
		return NextIs(TokenKind::PLUS) ||
			NextIs(TokenKind::MINUS) ||
			NextIs(TokenKind::STAR) ||
			NextIs(TokenKind::FOR_SLASH) ||
			NextIs(TokenKind::PERCENT) ||
			NextIs(TokenKind::EQUAL_EQUAL) ||
			NextIs(TokenKind::NOT_EQUAL) ||
			NextIs(TokenKind::GREATER) ||
			NextIs(TokenKind::GREATER_EQUAL) ||
			NextIs(TokenKind::LESS) ||
			NextIs(TokenKind::LESS_EQUAL) ||
			NextIs(TokenKind::BOOLEAN_AND) ||
			NextIs(TokenKind::BOOLEAN_OR) ||
			NextIs(TokenKind::BINARY_AND) ||
			NextIs(TokenKind::BINARY_OR) ||
			NextIs(TokenKind::BINARY_XOR);
	}

	bool Parser::NextIsCompoundAssignment()
	{
		return NextIs(TokenKind::PLUS_EQUALS) ||
			NextIs(TokenKind::MINUS_EQUALS) ||
			NextIs(TokenKind::STAR_EQUALS) ||
			NextIs(TokenKind::FOR_SLASH_EQUALS) ||
			NextIs(TokenKind::PERCENT_EQUALS) ||
			NextIs(TokenKind::BINARY_AND_EQUALS) ||
			NextIs(TokenKind::BINARY_OR_EQUALS) ||
			NextIs(TokenKind::BINARY_XOR_EQUALS);
	}

	Token Parser::Eat(TokenKind tokenKind)
	{
		const u32 lineNumber = m_Lexer->sourceIter.lineNumber;
		const u32 columnIndex = m_Lexer->sourceIter.columnIndex;

		if (NextIs(tokenKind))
		{
			Token previous = m_Current;
			m_Current = m_Lexer->Next();
			return previous;
		}

		StringBuilder diagnosticStr;
		diagnosticStr.Append("Expected \"");
		diagnosticStr.Append(TokenKindToString(tokenKind));
		diagnosticStr.Append("\" but found \"");
		diagnosticStr.Append(TokenKindToString(m_Current.kind));
		diagnosticStr.Append("\"");
		diagnosticContainer->AddDiagnostic(m_Current.span, lineNumber, columnIndex, diagnosticStr.ToString());

		return g_EmptyToken;
	}

	std::string Parser::NextTempIdentifier()
	{
		return "__tmp" + IntToString(m_TempIdentIdx++);
	}

	StatementBlock* Parser::NextStatementBlock()
	{
		Span span = Eat(TokenKind::OPEN_CURLY).span;
		std::vector<Statement*> statements;
		while (HasNext() && !NextIs(TokenKind::CLOSE_CURLY))
		{
			statements.push_back(NextStatement());

			while (NextIs(TokenKind::SEMICOLON))
			{
				Eat(TokenKind::SEMICOLON);
			}
		}

		return new StatementBlock(span.Extend(Eat(TokenKind::CLOSE_CURLY).span), statements);
	}

	StatementBlock* Parser::Parse()
	{
		std::vector<Statement*> statements;
		Span span = Span(0, 0);

		while (!m_Lexer->sourceIter.EndOfFileReached())
		{
			statements.push_back(NextStatement());

			while (NextIs(TokenKind::SEMICOLON))
			{
				Eat(TokenKind::SEMICOLON);
			}
		}

		Eat(TokenKind::END_OF_FILE);

		return new StatementBlock(span.Extend(m_Lexer->GetSpan()), statements);
	}

	Expression* Parser::NextExpression()
	{
		Expression* expression = NextPrimary();

		if (NextIsBinaryOperator())
		{
			return NextBinary(0, expression);
		}
		else if (NextIs(TokenKind::QUESTION))
		{
			return NextTernary(expression);
		}

		return expression;
	}

	Expression* Parser::NextPrimary()
	{
		if (NextIsTypename())
		{
			Token typeToken = Eat(m_Current.kind);
			Span span = m_Current.span;

			TypeName typeName = TokenKindToTypeName(typeToken.kind);

			if (NextIs(TokenKind::OPEN_SQUARE))
			{
				Eat(TokenKind::OPEN_SQUARE);
				Eat(TokenKind::CLOSE_SQUARE);
				typeName = TypeNameToListVariant(typeName);
			}

			Token identifierToken = Eat(TokenKind::IDENTIFIER);

			Expression* initializer = nullptr;

			if (NextIs(TokenKind::EQUALS))
			{
				Eat(TokenKind::EQUALS);
				initializer = NextExpression();
				span = span.Extend(initializer->span);
			}

			return new Declaration(span, identifierToken.value, initializer, typeName);
		}
		else if (NextIs(TokenKind::OPEN_PAREN))
		{
			Eat(TokenKind::OPEN_PAREN);
			Expression* subexpression = NextExpression();
			Eat(TokenKind::CLOSE_PAREN);
			return subexpression;
		}
		else if (NextIs(TokenKind::INT_LITERAL))
		{
			Token intToken = Eat(TokenKind::INT_LITERAL);
			return new IntLiteral(intToken.span, ParseInt(intToken.value));
		}
		else if (NextIs(TokenKind::FLOAT_LITERAL))
		{
			Token floatToken = Eat(TokenKind::FLOAT_LITERAL);
			return new FloatLiteral(floatToken.span, ParseFloat(floatToken.value));
		}
		else if (NextIs(TokenKind::TRUE))
		{
			Token boolToken = Eat(TokenKind::TRUE);
			return new BoolLiteral(boolToken.span, true);
		}
		else if (NextIs(TokenKind::FALSE))
		{
			Token boolToken = Eat(TokenKind::FALSE);
			return new BoolLiteral(boolToken.span, false);
		}
		else if (NextIs(TokenKind::STRING_LITERAL))
		{
			Token stringToken = Eat(TokenKind::STRING_LITERAL);
			return new StringLiteral(stringToken.span, stringToken.value);
		}
		else if (NextIs(TokenKind::CHAR_LITERAL))
		{
			Token charToken = Eat(TokenKind::CHAR_LITERAL);
			return new CharLiteral(charToken.span, charToken.value[0]);
		}
		else if (NextIs(TokenKind::IDENTIFIER))
		{
			Token identifierToken = Eat(TokenKind::IDENTIFIER);
			Span span = identifierToken.span;

			if (NextIs(TokenKind::OPEN_PAREN))
			{
				Eat(TokenKind::OPEN_PAREN);
				std::vector<Expression*> argumentList = NextArgumentList();
				return new FunctionCall(span.Extend(Eat(TokenKind::CLOSE_PAREN).span), identifierToken.value, argumentList);
			}
			else if (NextIs(TokenKind::OPEN_SQUARE))
			{
				Eat(TokenKind::OPEN_SQUARE);
				Expression* indexExpression = NextExpression();
				Eat(TokenKind::CLOSE_SQUARE);

				return new IndexOperation(span.Extend(indexExpression->span), identifierToken.value, indexExpression);
			}
			else if (NextIs(TokenKind::EQUALS))
			{
				Eat(TokenKind::EQUALS);
				Expression* rhs = NextExpression();
				rhs->span = rhs->span.Extend(Eat(TokenKind::SEMICOLON).span);

				return new Assignment(span.Extend(rhs->span), identifierToken.value, rhs);
			}
			else if (NextIsCompoundAssignment())
			{
				Token opToken = Eat(m_Current.kind);

				Expression* rhs = NextExpression();
				rhs->span = rhs->span.Extend(Eat(TokenKind::SEMICOLON).span);

				return new CompoundAssignment(span.Extend(rhs->span), identifierToken.value, rhs, TokenKindToBinaryOperatorType(opToken.kind));
			}
			else if (NextIs(TokenKind::PLUS_PLUS) || NextIs(TokenKind::MINUS_MINUS))
			{
				diagnosticContainer->AddDiagnostic(m_Current.span, m_Lexer->sourceIter.lineNumber, m_Lexer->sourceIter.columnIndex, "Increment/decrement operator not supported!");
				Eat(m_Current.kind);
			}

			return new Identifier(span, identifierToken.value);
		}
		else if (NextIs(TokenKind::TILDE))
		{
			return NextUnary();
		}
		else if (NextIs(TokenKind::BANG))
		{
			return NextUnary();
		}
		else if (NextIs(TokenKind::OPEN_CURLY))
		{
			return NextListInitializer();
		}
		else if (NextIs(TokenKind::PLUS_PLUS) || NextIs(TokenKind::MINUS_MINUS))
		{
			diagnosticContainer->AddDiagnostic(m_Current.span, m_Lexer->sourceIter.lineNumber, m_Lexer->sourceIter.columnIndex, "Increment/decrement operator not supported!");
			Eat(m_Current.kind);
			return nullptr;
		}

		StringBuilder diagnosticStr;
		diagnosticStr.Append("Expected expression, but found \"");
		diagnosticStr.Append(TokenKindToString(m_Current.kind));
		diagnosticStr.Append("\"");
		diagnosticContainer->AddDiagnostic(m_Current.span, m_Lexer->sourceIter.lineNumber, m_Lexer->sourceIter.columnIndex, diagnosticStr.ToString());

		return nullptr;
	}

	Expression* Parser::NextBinary(i32 lhsPrecendence, Expression* lhs)
	{
		while (true)
		{
			i32 precedence = GetBinaryOperatorPrecedence(m_Current.kind);
			if (precedence < lhsPrecendence)
			{
				return lhs;
			}

			Token binaryOperator = Eat(m_Current.kind);

			Expression* rhs = NextPrimary();
			i32 nextPrecedence = GetBinaryOperatorPrecedence(m_Current.kind);
			if (precedence < nextPrecedence)
			{
				rhs = NextBinary(precedence + 1, rhs);
			}

			lhs = new BinaryOperation(lhs->span.Extend(rhs->span), TokenKindToBinaryOperatorType(binaryOperator.kind), lhs, rhs);
		}
	}

	UnaryOperation* Parser::NextUnary()
	{
		Token unaryOperatorToken = Eat(m_Current.kind);
		Expression* expression = NextExpression();
		return new UnaryOperation(unaryOperatorToken.span.Extend(expression->span), TokenKindToUnaryOperatorType(unaryOperatorToken.kind), expression);
	}

	TernaryOperation* Parser::NextTernary(Expression* condition)
	{
		Eat(TokenKind::QUESTION);
		Expression* ifTrue = NextExpression();
		Eat(TokenKind::SINGLE_COLON);
		Expression* ifFalse = NextExpression();
		return new TernaryOperation(condition->span.Extend(ifFalse->span), condition, ifTrue, ifFalse);
	}

	ListInitializer* Parser::NextListInitializer()
	{
		Span span = Eat(TokenKind::OPEN_CURLY).span;

		std::vector<Expression*> listValues;
		if (!NextIs(TokenKind::CLOSE_CURLY))
		{
			while (true)
			{
				listValues.push_back(NextExpression());

				if (NextIs(TokenKind::CLOSE_CURLY))
				{
					break;
				}

				Eat(TokenKind::COMMA);
			}
		}

		return new ListInitializer(span.Extend(Eat(TokenKind::CLOSE_CURLY).span), listValues);
	}

	Statement* Parser::NextStatement()
	{
		if (NextIs(TokenKind::IDENTIFIER))
		{
			return NextExpression();
		}
		else if (NextIsTypename())
		{
			return NextExpression();
		}
		else if (NextIs(TokenKind::OPEN_CURLY))
		{
			return NextStatementBlock();
		}
		else if (NextIs(TokenKind::IF))
		{
			return NextIfStatement();
		}
		else if (NextIs(TokenKind::FOR))
		{
			return NextForStatement();
		}
		else if (NextIs(TokenKind::DO))
		{
			return NextDoWhileStatement();
		}
		else if (NextIs(TokenKind::WHILE))
		{
			return NextWhileStatement();
		}
		else if (NextIs(TokenKind::FUNC))
		{
			return NextFunctionDeclaration();
		}
		else if (NextIs(TokenKind::BREAK))
		{
			Token breakToken = Eat(TokenKind::BREAK);
			Eat(TokenKind::SEMICOLON);
			return new BreakStatement(breakToken.span);
		}
		else if (NextIs(TokenKind::YIELD))
		{
			Token yieldToken = Eat(TokenKind::YIELD);

			Expression* yieldValue = nullptr;
			if (!NextIs(TokenKind::SEMICOLON))
			{
				yieldValue = NextExpression();
			}
			Eat(TokenKind::SEMICOLON);

			return new YieldStatement(yieldToken.span.Extend(Eat(TokenKind::SEMICOLON).span), yieldValue);
		}
		else if (NextIs(TokenKind::RETURN))
		{
			Token returnToken = Eat(TokenKind::RETURN);

			Expression* returnValue = nullptr;
			if (!NextIs(TokenKind::SEMICOLON))
			{
				returnValue = NextExpression();
			}
			Eat(TokenKind::SEMICOLON);

			return new ReturnStatement(returnToken.span, returnValue);
		}

		StringBuilder diagnosticStr;
		diagnosticStr.Append("Expected expression, but found \"");
		diagnosticStr.Append(TokenKindToString(m_Current.kind));
		diagnosticStr.Append("\"");
		diagnosticContainer->AddDiagnostic(m_Current.span, m_Lexer->sourceIter.lineNumber, m_Lexer->sourceIter.columnIndex, diagnosticStr.ToString());

		Eat(m_Current.kind);

		return nullptr;
	}

	IfStatement* Parser::NextIfStatement()
	{
		if (!NextIs(TokenKind::IF) && !NextIs(TokenKind::ELIF))
		{
			std::string tokenKindStr(TokenKindToString(m_Current.kind));
			diagnosticContainer->AddDiagnostic(m_Current.span, m_Lexer->sourceIter.lineNumber, m_Lexer->sourceIter.columnIndex, "Expected if or elif, instead got " + tokenKindStr);
			return nullptr;
		}

		Span span = Eat(m_Current.kind).span;
		Eat(TokenKind::OPEN_PAREN);
		Expression* condition = NextExpression();
		Eat(TokenKind::CLOSE_PAREN);
		Statement* then = NextStatement();
		span = span.Extend(then->span);

		Statement* otherwise = nullptr;
		if (NextIs(TokenKind::ELIF))
		{
			otherwise = NextIfStatement();
		}
		else if (NextIs(TokenKind::ELSE))
		{
			Eat(TokenKind::ELSE);
			otherwise = NextStatement();
			span = span.Extend(otherwise->span);
		}

		return new IfStatement(span, condition, then, otherwise);
	}

	ForStatement* Parser::NextForStatement()
	{
		Span span = Eat(TokenKind::FOR).span;
		Expression* setup = nullptr;
		Expression* condition = nullptr;
		Expression* update = nullptr;
		Eat(TokenKind::OPEN_PAREN);
		if (!NextIs(TokenKind::SEMICOLON))
		{
			setup = NextExpression();
		}
		Eat(TokenKind::SEMICOLON);
		if (!NextIs(TokenKind::SEMICOLON))
		{
			condition = NextExpression();
		}
		Eat(TokenKind::SEMICOLON);
		if (!NextIs(TokenKind::CLOSE_PAREN))
		{
			update = NextExpression();
		}
		Eat(TokenKind::CLOSE_PAREN);
		Statement* body = NextStatement();
		return new ForStatement(span.Extend(body->span), setup, condition, update, body);
	}

	WhileStatement* Parser::NextWhileStatement()
	{
		Span span = Eat(TokenKind::WHILE).span;
		Eat(TokenKind::OPEN_PAREN);
		Expression* condition = NextExpression();
		Eat(TokenKind::CLOSE_PAREN);
		Statement* body = NextStatement();
		return new WhileStatement(span.Extend(body->span), condition, body);
	}

	DoWhileStatement* Parser::NextDoWhileStatement()
	{
		Span span = Eat(TokenKind::DO).span;
		Statement* body = NextStatement();
		Eat(TokenKind::WHILE);
		Eat(TokenKind::OPEN_PAREN);
		Expression* condition = NextExpression();
		Eat(TokenKind::CLOSE_PAREN);
		return new DoWhileStatement(span.Extend(Eat(TokenKind::SEMICOLON).span), condition, body);
	}

	FunctionDeclaration* Parser::NextFunctionDeclaration()
	{
		Span span = Eat(TokenKind::FUNC).span;
		Token functionNameToken = Eat(TokenKind::IDENTIFIER);
		Eat(TokenKind::OPEN_PAREN);
		std::vector<Identifier*> arguments = NextArgumentDefinitionList();
		Eat(TokenKind::CLOSE_PAREN);
		Eat(TokenKind::ARROW);
		Token returnTypeToken = Eat(m_Current.kind);
		StatementBlock* body = NextStatementBlock();
		return new FunctionDeclaration(span.Extend(body->span), functionNameToken.value, arguments, TokenKindToTypeName(returnTypeToken.kind), body);
	}

	std::vector<Identifier*> Parser::NextArgumentDefinitionList()
	{
		std::vector<Identifier*> result;

		while (!NextIs(TokenKind::CLOSE_PAREN))
		{
			Token typeToken = Eat(m_Current.kind);
			Span span = typeToken.span;
			Token identifier = Eat(TokenKind::IDENTIFIER);

			result.push_back(new Identifier(span.Extend(identifier.span), identifier.value, TokenKindToTypeName(typeToken.kind)));

			if (!NextIs(TokenKind::COMMA))
			{
				break;
			}

			Eat(TokenKind::COMMA);
		}

		return result;
	}

	std::vector<Expression*> Parser::NextArgumentList()
	{
		std::vector<Expression*> result;

		while (true)
		{
			result.push_back(NextExpression());

			if (!NextIs(TokenKind::COMMA))
			{
				break;
			}

			Eat(TokenKind::COMMA);
		}

		return result;
	}

	AST::AST()
	{
	}

	void AST::Generate(const std::string& sourceText)
	{
		bValid = false;

		diagnosticContainer = new DiagnosticContainer();
		lexer = new Lexer(sourceText, diagnosticContainer);
		parser = new Parser(lexer, diagnosticContainer);

		rootBlock = parser->Parse();

		bValid = true;
	}

	void AST::Destroy()
	{
		bValid = false;

		delete diagnosticContainer;
		diagnosticContainer = nullptr;
		delete rootBlock;
		rootBlock = nullptr;
		delete parser;
		parser = nullptr;
	}
} // namespace flex
