#include "stdafx.hpp"

#include "VirtualMachine/Frontend/Parser.hpp"

#include "StringBuilder.hpp"
#include "VirtualMachine/Backend/VirtualMachine.hpp"
#include "VirtualMachine/Frontend/Lexer.hpp"
#include "VirtualMachine/Frontend/Token.hpp"

namespace flex
{
	namespace AST
	{

		// Deeper nesting (of parentheses, blocks, unary operators...) is rejected rather than overflowing the stack
		static const u32 MAX_PARSE_DEPTH = 64;
		// Longest chain of binary operators in one expression (each one adds a level to the AST)
		static const u32 MAX_BINARY_CHAIN_LENGTH = 256;

		struct ParseDepthGuard
		{
			explicit ParseDepthGuard(u32& depth) : depth(depth) { ++depth; }
			~ParseDepthGuard() { --depth; }
			u32& depth;
		};

		const char* UnaryOperatorTypeToString(UnaryOperatorType opType)
		{
			return g_UnaryOperatorTypeStrings[(u32)opType];
		}

		UnaryOperatorType TokenKindToUnaryOperatorType(TokenKind tokenKind)
		{
			switch (tokenKind)
			{
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

		bool IsCompoundAssignment(BinaryOperatorType operatorType)
		{
			switch (operatorType)
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
			default:
				return false;
			}
		}

		BinaryOperatorType GetNonCompoundType(BinaryOperatorType operatorType)
		{

			switch (operatorType)
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
			case TokenKind::QUESTION:
				return 250;
			case TokenKind::EQUALS:
			case TokenKind::PLUS_EQUALS:
			case TokenKind::MINUS_EQUALS:
			case TokenKind::STAR_EQUALS:
			case TokenKind::FOR_SLASH_EQUALS:
			case TokenKind::PERCENT_EQUALS:
				return 200;
			default:
				return -1;
			}
		}

		bool BinaryOperatorTypeIsTest(BinaryOperatorType operatorType)
		{
			return operatorType == BinaryOperatorType::EQUAL_TEST ||
				operatorType == BinaryOperatorType::NOT_EQUAL_TEST ||
				operatorType == BinaryOperatorType::GREATER_TEST ||
				operatorType == BinaryOperatorType::GREATER_EQUAL_TEST ||
				operatorType == BinaryOperatorType::LESS_TEST ||
				operatorType == BinaryOperatorType::LESS_EQUAL_TEST ||
				operatorType == BinaryOperatorType::BOOLEAN_AND ||
				operatorType == BinaryOperatorType::BOOLEAN_OR;
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
			case TokenKind::VOID_KEYWORD:	return TypeName::VOID;
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
				statementType == StatementType::CALL ||
				statementType == StatementType::INDEX_OPERATION;
		}

		bool IsLiteral(StatementType statementType)
		{
			return statementType == StatementType::INT_LIT ||
				statementType == StatementType::FLOAT_LIT ||
				statementType == StatementType::BOOL_LIT ||
				statementType == StatementType::STRING_LIT ||
				statementType == StatementType::CHAR_LIT;
		}

		bool IsExpression(StatementType statementType)
		{
			return statementType == StatementType::BREAK ||
				statementType == StatementType::YIELD ||
				statementType == StatementType::RETURN ||
				statementType == StatementType::IDENTIFIER ||
				statementType == StatementType::INT_LIT ||
				statementType == StatementType::FLOAT_LIT ||
				statementType == StatementType::BOOL_LIT ||
				statementType == StatementType::STRING_LIT ||
				statementType == StatementType::CHAR_LIT ||
				statementType == StatementType::INDEX_OPERATION ||
				statementType == StatementType::UNARY_OPERATION ||
				statementType == StatementType::BINARY_OPERATION ||
				statementType == StatementType::TERNARY_OPERATION ||
				statementType == StatementType::CALL;
		}

		bool IsSimple(StatementType statementType)
		{
			return IsLiteral(statementType) ||
				statementType == StatementType::IDENTIFIER ||
				statementType == StatementType::FUNC_ARG;
		}

		Statement::Statement(const Span& span, StatementType statementType) :
			span(span),
			statementType(statementType)
		{
		}

		StatementBlock::StatementBlock(const Span& span, const ArenaVector<Statement*>& statements) :
			Statement(span, StatementType::STATEMENT_BLOCK),
			statements(statements)
		{
		}

		StatementBlock::~StatementBlock()
		{
			// (Children are owned by the AST's arena)
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

		IfStatement::~IfStatement()
		{
			// (Children are owned by the AST's arena)
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

		ForStatement::~ForStatement()
		{
			// (Children are owned by the AST's arena)
		}

		std::string ForStatement::ToString() const
		{
			StringBuilder stringBuilder;

			stringBuilder.Append("for (");
			if (setup != nullptr)
			{
				// (Includes the semicolon)
				stringBuilder.Append(setup->ToString());
			}
			else
			{
				stringBuilder.Append(";");
			}
			if (condition != nullptr)
			{
				stringBuilder.Append(" ");
				stringBuilder.Append(condition->ToString());
			}
			stringBuilder.Append(";");
			if (update != nullptr)
			{
				stringBuilder.Append(" ");
				stringBuilder.Append(update->ToString());
			}
			stringBuilder.Append(")\n");
			stringBuilder.Append(body->ToString());

			return stringBuilder.ToString();
		}

		WhileStatement::~WhileStatement()
		{
			// (Children are owned by the AST's arena)
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

		DoWhileStatement::~DoWhileStatement()
		{
			// (Children are owned by the AST's arena)
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

		FunctionDeclaration::~FunctionDeclaration()
		{
			// (Children are owned by the AST's arena)
		}

		std::string FunctionDeclaration::ToString() const
		{
			StringBuilder stringBuilder;

			stringBuilder.Append("func ");
			stringBuilder.Append(name);
			stringBuilder.Append("(");
			for (u32 i = 0; i < (u32)arguments.size(); ++i)
			{
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

		YieldStatement::~YieldStatement()
		{
			// (Children are owned by the AST's arena)
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

		ReturnStatement::~ReturnStatement()
		{
			// (Children are owned by the AST's arena)
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

		Declaration::~Declaration()
		{
			// (Children are owned by the AST's arena)
		}

		std::string Declaration::ToString() const
		{
			StringBuilder stringBuilder;

			stringBuilder.Append(TypeNameToString(typeName));
			stringBuilder.Append(" ");
			stringBuilder.Append(identifierStr);

			if (!bIsFunctionArgDefinition)
			{
				stringBuilder.Append(" = ");
				stringBuilder.Append(initializer->ToString());
				stringBuilder.Append(";");
			}

			return stringBuilder.ToString();
		}

		Assignment::~Assignment()
		{
			// (Children are owned by the AST's arena)
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

		CompoundAssignment::~CompoundAssignment()
		{
			// (Children are owned by the AST's arena)
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

		ListInitializer::~ListInitializer()
		{
			// (Children are owned by the AST's arena)
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

		IndexOperation::~IndexOperation()
		{
			// (Children are owned by the AST's arena)
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

		UnaryOperation::~UnaryOperation()
		{
			// (Children are owned by the AST's arena)
		}

		std::string UnaryOperation::ToString() const
		{
			StringBuilder stringBuilder;

			switch (operatorType)
			{
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
				CHECK(false);
			}

			stringBuilder.Append(expression->ToString());

			return stringBuilder.ToString();
		}

		BinaryOperation::~BinaryOperation()
		{
			// (Children are owned by the AST's arena)
		}

		std::string BinaryOperation::ToString() const
		{
			StringBuilder stringBuilder;

			stringBuilder.Append("(");
			stringBuilder.Append(lhs->ToString());
			stringBuilder.Append(" ");
			stringBuilder.Append(BinaryOperatorTypeToString(operatorType));
			stringBuilder.Append(" ");
			stringBuilder.Append(rhs->ToString());
			stringBuilder.Append(")");

			return stringBuilder.ToString();
		}

		TernaryOperation::~TernaryOperation()
		{
			// (Children are owned by the AST's arena)
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

		FunctionCall::~FunctionCall()
		{
			// (Children are owned by the AST's arena)
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

		Cast::~Cast()
		{
			// (Children are owned by the AST's arena)
		}

		std::string Cast::ToString() const
		{
			StringBuilder stringBuilder;

			stringBuilder.Append("(");
			stringBuilder.Append(TypeNameToString(typeName));
			stringBuilder.Append(")");
			stringBuilder.Append(target->ToString());

			return stringBuilder.ToString();
		}

		Parser::Parser(Lexer* lexer, DiagnosticContainer* diagnosticContainer, Arena* arena) :
			m_Lexer(lexer),
			diagnosticContainer(diagnosticContainer),
			m_Arena(arena)
		{
			m_Current = m_Lexer->Next();
		}

		bool Parser::HasNext()
		{
			// (The lexer reaches the end of the source before the last token has been consumed)
			return !NextIs(TokenKind::END_OF_FILE);
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
			// (The lexer lexes "x-1" as a minus, so negative literals never follow an operand)
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
			diagnosticContainer->AddDiagnostic(m_Current.span, diagnosticStr.ToString());

			return g_EmptyToken;
		}

		std::string Parser::NextTempIdentifier()
		{
			return "__tmp" + IntToString(m_TempIdentIdx++);
		}

		StatementBlock* Parser::NextStatementBlock()
		{
			Span span = Eat(TokenKind::OPEN_CURLY).span;
			ArenaVector<Statement*> statements(m_Arena);
			while (HasNext() && !NextIs(TokenKind::CLOSE_CURLY))
			{
				Statement* statement = NextStatement();
				if (statement == nullptr)
				{
					for (u32 i = 0; i < (u32)statements.size(); ++i)
					{
					}

					return nullptr;
				}

				statements.push_back(statement);

				while (NextIs(TokenKind::SEMICOLON))
				{
					Eat(TokenKind::SEMICOLON);
				}
			}

			return m_Arena->New<StatementBlock>(span.Extend(Eat(TokenKind::CLOSE_CURLY).span), statements);
		}

		StatementBlock* Parser::Parse()
		{
			ArenaVector<Statement*> statements(m_Arena);
			Span span = Span(0, 0);

			while (HasNext())
			{
				Statement* statement = NextStatement();
				if (statement == nullptr)
				{
					for (u32 i = 0; i < (u32)statements.size(); ++i)
					{
					}

					return nullptr;
				}

				statements.push_back(statement);

				while (NextIs(TokenKind::SEMICOLON))
				{
					Eat(TokenKind::SEMICOLON);
				}

				const i32 maxStatementCount = 100000;
				if (statements.size() > maxStatementCount)
				{
					PrintError("Maximum number of statements reached (%d), aborting\n", maxStatementCount);
					m_Lexer->diagnosticContainer->AddDiagnostic(statement->span, "Maximum number of statements reached, aborting");
					break;
				}
			}

			Eat(TokenKind::END_OF_FILE);

			return m_Arena->New<StatementBlock>(span.Extend(m_Lexer->GetSpan()), statements);
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
			ParseDepthGuard depthGuard(m_Depth);
			if (m_Depth > MAX_PARSE_DEPTH)
			{
				diagnosticContainer->AddDiagnostic(m_Current.span, "Expression is nested too deeply");
				return nullptr;
			}

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

				Eat(TokenKind::EQUALS);
				Expression* initializer = NextExpression();
				if (initializer != nullptr)
				{
					//span = span.Extend(Eat(TokenKind::SEMICOLON).span);

					if (VM::VirtualMachine::IsTerminalOutputVar(identifierToken.value))
					{
						diagnosticContainer->AddDiagnostic(span, "Cannot overwrite system variable");
						return nullptr;
					}

					return m_Arena->New<Declaration>(span, identifierToken.value, initializer, typeName);
				}
			}
			else if (NextIs(TokenKind::OPEN_PAREN))
			{
				Eat(TokenKind::OPEN_PAREN);

				// Cast
				if (NextIsTypename())
				{
					Token typeToken = Eat(m_Current.kind);

					if (NextIs(TokenKind::CLOSE_PAREN))
					{
						Span span = m_Current.span;

						TypeName typeName = TokenKindToTypeName(typeToken.kind);

						span = span.Extend(Eat(TokenKind::CLOSE_PAREN).span);
						Expression* target = NextPrimary();
						if (target != nullptr)
						{
							// Ignore redundant casts
							if (target->typeName == typeName)
							{
								return target;
							}
							return m_Arena->New<Cast>(span, typeName, target);
						}
					}
				}
				else
				{
					Expression* subexpression = NextExpression();
					Eat(TokenKind::CLOSE_PAREN);
					return subexpression;
				}
			}
			else if (NextIs(TokenKind::INT_LITERAL))
			{
				Token intToken = Eat(TokenKind::INT_LITERAL);
				return m_Arena->New<IntLiteral>(intToken.span, ParseInt(intToken.value));
			}
			else if (NextIs(TokenKind::FLOAT_LITERAL))
			{
				Token floatToken = Eat(TokenKind::FLOAT_LITERAL);
				return m_Arena->New<FloatLiteral>(floatToken.span, ParseFloat(floatToken.value));
			}
			else if (NextIs(TokenKind::TRUE))
			{
				Token boolToken = Eat(TokenKind::TRUE);
				return m_Arena->New<BoolLiteral>(boolToken.span, true);
			}
			else if (NextIs(TokenKind::FALSE))
			{
				Token boolToken = Eat(TokenKind::FALSE);
				return m_Arena->New<BoolLiteral>(boolToken.span, false);
			}
			else if (NextIs(TokenKind::STRING_LITERAL))
			{
				Token stringToken = Eat(TokenKind::STRING_LITERAL);
				return m_Arena->New<StringLiteral>(stringToken.span, stringToken.value);
			}
			else if (NextIs(TokenKind::CHAR_LITERAL))
			{
				Token charToken = Eat(TokenKind::CHAR_LITERAL);
				return m_Arena->New<CharLiteral>(charToken.span, charToken.value[0]);
			}
			else if (NextIs(TokenKind::IDENTIFIER))
			{
				Token identifierToken = Eat(TokenKind::IDENTIFIER);
				Span span = identifierToken.span;

				if (NextIs(TokenKind::OPEN_PAREN))
				{
					Eat(TokenKind::OPEN_PAREN);
					ArenaVector<Expression*> argumentList = NextArgumentList();
					if (argumentList.size() != 1 || argumentList[0] != nullptr)
					{
						return m_Arena->New<FunctionCall>(span.Extend(Eat(TokenKind::CLOSE_PAREN).span), identifierToken.value, argumentList);
					}
				}
				else if (NextIs(TokenKind::OPEN_SQUARE))
				{
					Eat(TokenKind::OPEN_SQUARE);
					Expression* indexExpression = NextExpression();
					Eat(TokenKind::CLOSE_SQUARE);

					if (indexExpression != nullptr)
					{
						return m_Arena->New<IndexOperation>(span.Extend(indexExpression->span), identifierToken.value, indexExpression);
					}
				}
				else if (NextIs(TokenKind::EQUALS))
				{
					Eat(TokenKind::EQUALS);
					Expression* rhs = NextExpression();
					if (rhs != nullptr)
					{
						// (The trailing semicolon is eaten by the enclosing statement, so this also works in for loop headers)
						return m_Arena->New<Assignment>(span.Extend(rhs->span), identifierToken.value, rhs);
					}

				}
				else if (NextIsCompoundAssignment())
				{
					Token opToken = Eat(m_Current.kind);

					Expression* rhs = NextExpression();
					if (rhs != nullptr)
					{
						return m_Arena->New<CompoundAssignment>(span.Extend(rhs->span), identifierToken.value, rhs, TokenKindToBinaryOperatorType(opToken.kind));
					}
				}
				else if (NextIs(TokenKind::PLUS_PLUS) || NextIs(TokenKind::MINUS_MINUS))
				{
					diagnosticContainer->AddDiagnostic(m_Current.span, "Increment/decrement operator not supported!");
					Eat(m_Current.kind);
				}

				return m_Arena->New<Identifier>(span, identifierToken.value);
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
				diagnosticContainer->AddDiagnostic(m_Current.span, "Increment/decrement operator not supported!");
				Eat(m_Current.kind);
				return nullptr;
			}
			else if (NextIs(TokenKind::MINUS))
			{
				return NextUnary();
			}

			StringBuilder diagnosticStr;
			diagnosticStr.Append("Expected expression, but found \"");
			diagnosticStr.Append(TokenKindToString(m_Current.kind));
			diagnosticStr.Append("\"");
			diagnosticContainer->AddDiagnostic(m_Current.span, diagnosticStr.ToString());

			return nullptr;
		}

		Expression* Parser::NextBinary(i32 lhsPrecendence, Expression* lhs)
		{
			u32 chainLength = 0;
			while (true)
			{
				if (lhs == nullptr)
				{
					return nullptr;
				}

				if (++chainLength > MAX_BINARY_CHAIN_LENGTH)
				{
					diagnosticContainer->AddDiagnostic(lhs->span, "Expression is too long");
					return nullptr;
				}

				i32 precedence = GetBinaryOperatorPrecedence(m_Current.kind);
				if (precedence < lhsPrecendence)
				{
					return lhs;
				}

				Expression* rhs = nullptr;
				if (m_Current.kind == TokenKind::QUESTION)
				{
					rhs = NextTernary(lhs);
					if (rhs == nullptr)
					{
						return nullptr;
					}
					return rhs;
				}

				Token binaryOperator = Eat(m_Current.kind);

				{
					rhs = NextPrimary();
					if (rhs == nullptr)
					{
						return nullptr;
					}
				}

				i32 nextPrecedence = GetBinaryOperatorPrecedence(m_Current.kind);
				if (precedence < nextPrecedence)
				{
					Expression* nextRHS = NextBinary(precedence + 1, rhs);
					if (nextRHS != nullptr)
					{
						rhs = nextRHS;
					}
					else
					{
						return nullptr;
					}
				}

				lhs = m_Arena->New<BinaryOperation>(lhs->span.Extend(rhs->span), TokenKindToBinaryOperatorType(binaryOperator.kind), lhs, rhs);
			}
		}

		UnaryOperation* Parser::NextUnary()
		{
			Token unaryOperatorToken = Eat(m_Current.kind);
			// Unary operators bind tighter than binary ones ("-a + b" is "(-a) + b")
			Expression* expression = NextPrimary();
			if (expression != nullptr)
			{
				return m_Arena->New<UnaryOperation>(unaryOperatorToken.span.Extend(expression->span), TokenKindToUnaryOperatorType(unaryOperatorToken.kind), expression);
			}

			return nullptr;
		}

		TernaryOperation* Parser::NextTernary(Expression* condition)
		{
			if (condition == nullptr)
			{
				return nullptr;
			}

			Eat(TokenKind::QUESTION);
			Expression* ifTrue = NextExpression();
			if (ifTrue != nullptr)
			{
				Eat(TokenKind::SINGLE_COLON);
				Expression* ifFalse = NextExpression();
				if (ifFalse != nullptr)
				{
					return m_Arena->New<TernaryOperation>(condition->span.Extend(ifFalse->span), condition, ifTrue, ifFalse);
				}
			}

			return nullptr;
		}

		ListInitializer* Parser::NextListInitializer()
		{
			Span span = Eat(TokenKind::OPEN_CURLY).span;

			ArenaVector<Expression*> listValues(m_Arena);
			if (!NextIs(TokenKind::CLOSE_CURLY))
			{
				while (true)
				{
					Expression* expression = NextExpression();
					if (expression == nullptr)
					{
						for (u32 i = 0; i < (u32)listValues.size(); ++i)
						{
						}
						return nullptr;
					}

					listValues.push_back(expression);

					if (NextIs(TokenKind::CLOSE_CURLY))
					{
						break;
					}

					Eat(TokenKind::COMMA);
				}
			}

			return m_Arena->New<ListInitializer>(span.Extend(Eat(TokenKind::CLOSE_CURLY).span), listValues);
		}

		Statement* Parser::NextStatement()
		{
			ParseDepthGuard depthGuard(m_Depth);
			if (m_Depth > MAX_PARSE_DEPTH)
			{
				diagnosticContainer->AddDiagnostic(m_Current.span, "Statement is nested too deeply");
				return nullptr;
			}

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
				return m_Arena->New<BreakStatement>(breakToken.span);
			}
			else if (NextIs(TokenKind::YIELD))
			{
				Token yieldToken = Eat(TokenKind::YIELD);

				Expression* yieldValue = nullptr;
				if (!NextIs(TokenKind::SEMICOLON))
				{
					yieldValue = NextExpression();
				}
				return m_Arena->New<YieldStatement>(yieldToken.span.Extend(Eat(TokenKind::SEMICOLON).span), yieldValue);
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

				return m_Arena->New<ReturnStatement>(returnToken.span, returnValue);
			}

			StringBuilder diagnosticStr;
			diagnosticStr.Append("Expected expression, but found \"");
			diagnosticStr.Append(TokenKindToString(m_Current.kind));
			diagnosticStr.Append("\"");
			diagnosticContainer->AddDiagnostic(m_Current.span, diagnosticStr.ToString());

			Eat(m_Current.kind);

			return nullptr;
		}

		IfStatement* Parser::NextIfStatement()
		{
			if (!NextIs(TokenKind::IF) && !NextIs(TokenKind::ELIF))
			{
				std::string tokenKindStr(TokenKindToString(m_Current.kind));
				diagnosticContainer->AddDiagnostic(m_Current.span, "Expected if or elif, instead got " + tokenKindStr);
				return nullptr;
			}

			Span span = Eat(m_Current.kind).span;
			Eat(TokenKind::OPEN_PAREN);
			Expression* condition = NextExpression();
			if (condition != nullptr)
			{
				Eat(TokenKind::CLOSE_PAREN);
				Statement* then = NextStatement();
				if (then != nullptr)
				{
					span = span.Extend(then->span);

					// e.g. "if (a) b += 1; else ..."
					while (NextIs(TokenKind::SEMICOLON))
					{
						Eat(TokenKind::SEMICOLON);
					}

					Statement* otherwise = nullptr;
					if (NextIs(TokenKind::ELIF))
					{
						otherwise = NextIfStatement();
						if (otherwise == nullptr)
						{
							return nullptr;
						}
					}
					else if (NextIs(TokenKind::ELSE))
					{
						Eat(TokenKind::ELSE);
						otherwise = NextStatement();
						if (otherwise == nullptr)
						{
							return nullptr;
						}
						span = span.Extend(otherwise->span);
					}

					return m_Arena->New<IfStatement>(span, condition, then, otherwise);
				}
			}

			return nullptr;
		}

		ForStatement* Parser::NextForStatement()
		{
			Span span = Eat(TokenKind::FOR).span;
			Expression* setup = nullptr;
			Expression* condition = nullptr;
			Statement* update = nullptr;
			Eat(TokenKind::OPEN_PAREN);
			if (!NextIs(TokenKind::SEMICOLON))
			{
				setup = NextExpression();
				if (setup == nullptr)
				{
					return nullptr;
				}
			}
			Eat(TokenKind::SEMICOLON);
			if (!NextIs(TokenKind::SEMICOLON))
			{
				condition = NextExpression();
				if (condition == nullptr)
				{
					return nullptr;
				}
			}
			Eat(TokenKind::SEMICOLON);
			if (!NextIs(TokenKind::CLOSE_PAREN))
			{
				update = NextExpression();
				if (update == nullptr)
				{
					return nullptr;
				}
			}
			Eat(TokenKind::CLOSE_PAREN);
			Statement* body = NextStatement();
			if (body != nullptr)
			{
				return m_Arena->New<ForStatement>(span.Extend(body->span), setup, condition, update, body);
			}

			return nullptr;
		}

		WhileStatement* Parser::NextWhileStatement()
		{
			Span span = Eat(TokenKind::WHILE).span;
			Eat(TokenKind::OPEN_PAREN);
			Expression* condition = NextExpression();
			if (condition != nullptr)
			{
				Eat(TokenKind::CLOSE_PAREN);
				Statement* body = NextStatement();
				if (body != nullptr)
				{
					return m_Arena->New<WhileStatement>(span.Extend(body->span), condition, body);
				}
			}

			return nullptr;
		}

		DoWhileStatement* Parser::NextDoWhileStatement()
		{
			Span span = Eat(TokenKind::DO).span;
			Statement* body = NextStatement();
			if (body != nullptr)
			{
				while (NextIs(TokenKind::SEMICOLON))
				{
					Eat(TokenKind::SEMICOLON);
				}
				Eat(TokenKind::WHILE);
				Eat(TokenKind::OPEN_PAREN);
				Expression* condition = NextExpression();
				if (condition != nullptr)
				{
					Eat(TokenKind::CLOSE_PAREN);
					return m_Arena->New<DoWhileStatement>(span.Extend(Eat(TokenKind::SEMICOLON).span), condition, body);
				}
			}

			return nullptr;
		}

		FunctionDeclaration* Parser::NextFunctionDeclaration()
		{
			Span span = Eat(TokenKind::FUNC).span;
			Token functionNameToken = Eat(TokenKind::IDENTIFIER);
			Eat(TokenKind::OPEN_PAREN);
			ArenaVector<Declaration*> arguments = NextArgumentDefinitionList();
			Eat(TokenKind::CLOSE_PAREN);
			Eat(TokenKind::ARROW);
			Token returnTypeToken = Eat(m_Current.kind);
			StatementBlock* body = NextStatementBlock();
			if (body != nullptr)
			{
				return m_Arena->New<FunctionDeclaration>(span.Extend(body->span), functionNameToken.value, arguments, TokenKindToTypeName(returnTypeToken.kind), body);
			}

			for (u32 i = 0; i < (u32)arguments.size(); ++i)
			{
			}
			return nullptr;
		}

		ArenaVector<Declaration*> Parser::NextArgumentDefinitionList()
		{
			ArenaVector<Declaration*> result(m_Arena);

			if (!NextIs(TokenKind::CLOSE_PAREN))
			{
				while (true)
				{
					Token typeToken = Eat(m_Current.kind);
					Span span = typeToken.span;
					Token identifier = Eat(TokenKind::IDENTIFIER);

					if (VM::VirtualMachine::IsTerminalOutputVar(identifier.value))
					{
						diagnosticContainer->AddDiagnostic(span, "Cannot overwrite system variable");
						// TODO: Memory leak - delete list of previous results
						return ArenaVector<Declaration*>(m_Arena);
					}

					result.push_back(m_Arena->New<Declaration>(span.Extend(identifier.span), identifier.value, nullptr, TokenKindToTypeName(typeToken.kind), true));

					if (!NextIs(TokenKind::COMMA))
					{
						break;
					}

					Eat(TokenKind::COMMA);
				}
			}

			return result;
		}

		ArenaVector<Expression*> Parser::NextArgumentList()
		{
			ArenaVector<Expression*> result(m_Arena);

			if (!NextIs(TokenKind::CLOSE_PAREN))
			{
				while (true)
				{
					Expression* argument = NextExpression();
					if (argument == nullptr)
					{
						{
							// (A single null argument signals an error)
							ArenaVector<Expression*> error(m_Arena);
							error.push_back(nullptr);
							return error;
						}
					}

					result.push_back(argument);

					if (!NextIs(TokenKind::COMMA))
					{
						break;
					}

					Eat(TokenKind::COMMA);
				}
			}

			return result;
		}

		AST::AST(Arena* arena) :
			m_Arena(arena)
		{
		}

		AST::~AST()
		{
			Destroy();
		}

		void AST::Generate(const char* sourceText)
		{
			Destroy();
			bValid = false;

			if (m_Arena == nullptr)
			{
				m_OwnedArena = new Arena();
				m_Arena = m_OwnedArena;
			}

			diagnosticContainer = new DiagnosticContainer();
			lexer = new Lexer(sourceText, diagnosticContainer);
			parser = new Parser(lexer, diagnosticContainer, m_Arena);

			rootBlock = parser->Parse();

			bValid = true;
		}

		void AST::Destroy()
		{
			bValid = false;

			delete diagnosticContainer;
			diagnosticContainer = nullptr;
			// (Every node is in the arena)
			rootBlock = nullptr;
			if (m_Arena != nullptr)
			{
				m_Arena->Reset();
			}
			if (m_OwnedArena != nullptr)
			{
				delete m_OwnedArena;
				m_OwnedArena = nullptr;
				m_Arena = nullptr;
			}
			delete parser;
			parser = nullptr;
			delete lexer;
			lexer = nullptr;
		}
	} // namespace AST
} // namespace flex
