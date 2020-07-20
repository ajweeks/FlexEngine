#include "stdafx.hpp"

#include "StringBuilder.hpp"
#include "VirtualMachine/Frontend/Frontend.hpp"

namespace flex
{
	struct Token g_EmptyToken = Token(Span(0, 0), TokenKind::_NONE, "");

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

	const char* TokenKindToString(TokenKind tokenKind)
	{
		return g_TokenStrings[(u32)tokenKind];
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

	std::string Token::ToString() const
	{
		StringBuilder stringBuilder;

		switch (kind)
		{
		case TokenKind::IDENTIFIER:
			stringBuilder.Append("identifier '");
			stringBuilder.Append(value);
			stringBuilder.Append("'");
			break;
		case TokenKind::INT_LITERAL:
			stringBuilder.Append("int literal '");
			stringBuilder.Append(value);
			stringBuilder.Append("'");
			break;
		case TokenKind::FLOAT_LITERAL:
			stringBuilder.Append("float literal '");
			stringBuilder.Append(value);
			stringBuilder.Append("'");
			break;
		case TokenKind::BOOL_LITERAL:
			stringBuilder.Append("bool literal '");
			stringBuilder.Append(value);
			stringBuilder.Append("'");
			break;
		case TokenKind::STRING_LITERAL:
			stringBuilder.Append("string literal '");
			stringBuilder.Append(value);
			stringBuilder.Append("'");
			break;
		case TokenKind::CHAR_LITERAL:
			stringBuilder.Append("char literal '");
			stringBuilder.Append(value);
			stringBuilder.Append("'");
			break;
		default:
			stringBuilder.Append(TokenKindToString(kind));
			stringBuilder.Append(" '");
			stringBuilder.Append(value);
			stringBuilder.Append("'");
			break;
		}

		return stringBuilder.ToString();
	}

	Lexer::Lexer() :
		m_CurrentSpan(0, 0)
	{
		SetSource("");
	}

	Lexer::Lexer(const std::string& inSource) :
		m_CurrentSpan(0, 0)
	{
		SetSource(inSource);
	}

	Lexer::~Lexer()
	{
	}

	void Lexer::SetSource(const std::string& source)
	{
		sourceIter = SourceIter(source);
	}

	StatementBlock* Lexer::Parse()
	{
		StatementBlock* rootBlock = new StatementBlock(Span(0, (u32)sourceIter.source.size()));

		while (true)
		{
			Token next = Next();
			std::string tokenKindStr = TokenKindToString(next.kind);
			if (next.value.empty())
			{
				Print("%s (%i:%i)\n", tokenKindStr.c_str(), next.span.low, next.span.high);
			}
			else
			{
				Print("%s (%i:%i): %s\n", tokenKindStr.c_str(), next.span.low, next.span.high, next.value.c_str());
			}
			if (next.kind == TokenKind::END_OF_FILE)
			{
				break;
			}
		}

		return rootBlock;
	}

	bool Lexer::Advance()
	{
		if (IsNewLine(sourceIter.Current()))
		{
			sourceIter.lineNumber++;
			sourceIter.columnIndex = 0;
		}

		bool bIterValid = sourceIter.MoveNext();
		if (bIterValid)
		{
			m_CurrentSpan = m_CurrentSpan.Grow();
		}

		return bIterValid;
	}

	Token Lexer::Next()
	{
		EatWhitespaceAndComments();
		m_CurrentSpan = m_CurrentSpan.Clip();

		if (sourceIter.EndOfFileReached())
		{
			return Token(GetSpan(), TokenKind::END_OF_FILE);
		}

		char c = sourceIter.Current();
		if (IsLetter(c) || c == '_')
		{
			return NextIdentifierOrKeyword();
		}
		else if (IsDigit(c))
		{
			return NextNumericLiteral();
		}

		switch (c)
		{
		case '{':
			Advance();
			return Token(GetSpan(), TokenKind::OPEN_CURLY);
		case '}':
			Advance();
			return Token(GetSpan(), TokenKind::CLOSE_CURLY);
		case '(':
			Advance();
			return Token(GetSpan(), TokenKind::OPEN_PAREN);
		case ')':
			Advance();
			return Token(GetSpan(), TokenKind::CLOSE_PAREN);
		case '[':
			Advance();
			return Token(GetSpan(), TokenKind::OPEN_SQUARE);
		case ']':
			Advance();
			return Token(GetSpan(), TokenKind::CLOSE_SQUARE);
		case ':':
			Advance();
			return Token(GetSpan(), TokenKind::SINGLE_COLON);
		case ';':
			Advance();
			return Token(GetSpan(), TokenKind::SEMICOLON);
		case '!':
			if (Advance())
			{
				if (sourceIter.Current() == '=')
				{
					Advance();
					return Token(GetSpan(), TokenKind::NOT_EQUAL_TEST);
				}
			}

			return Token(GetSpan(), TokenKind::BANG);
		case '?':
			Advance();
			return Token(GetSpan(), TokenKind::TERNARY);
		case '=':
			if (Advance())
			{
				if (sourceIter.Current() == '=')
				{
					Advance();
					return Token(GetSpan(), TokenKind::EQUAL_TEST);
				}
			}

			return Token(GetSpan(), TokenKind::ASSIGNMENT);
		case '>':
			if (Advance())
			{
				if (sourceIter.Current() == '=')
				{
					Advance();
					return Token(GetSpan(), TokenKind::GREATER_EQUAL);
				}
			}

			return Token(GetSpan(), TokenKind::GREATER);
		case '<':
			if (Advance())
			{
				if (sourceIter.Current() == '=')
				{
					Advance();
					return Token(GetSpan(), TokenKind::LESS_EQUAL);
				}
			}

			return Token(GetSpan(), TokenKind::LESS);
		case '+':
			if (Advance())
			{
				if (sourceIter.Current() == '=')
				{
					Advance();
					return Token(GetSpan(), TokenKind::ADD_ASSIGN);
				}
			}

			return Token(GetSpan(), TokenKind::ADD);
		case '-':
			if (Advance())
			{
				if (sourceIter.Current() == '=')
				{
					Advance();
					return Token(GetSpan(), TokenKind::SUBTRACT_ASSIGN);
				}
			}

			return Token(GetSpan(), TokenKind::SUBTRACT);
		case '*':
			if (Advance())
			{
				if (sourceIter.Current() == '=')
				{
					Advance();
					return Token(GetSpan(), TokenKind::MULTIPLY_ASSIGN);
				}
			}

			return Token(GetSpan(), TokenKind::MULTIPLY);
		case '/':
			if (Advance())
			{
				if (sourceIter.Current() == '=')
				{
					Advance();
					return Token(GetSpan(), TokenKind::DIVIDE_ASSIGN);
				}
			}

			return Token(GetSpan(), TokenKind::DIVIDE);
		case '%':
			if (Advance())
			{
				if (sourceIter.Current() == '=')
				{
					Advance();
					return Token(GetSpan(), TokenKind::MODULO_ASSIGN);
				}
			}

			return Token(GetSpan(), TokenKind::MODULO);
		case '&':
			if (Advance())
			{
				if (sourceIter.Current() == '&')
				{
					Advance();
					return Token(GetSpan(), TokenKind::BOOLEAN_AND);
				}
				else if (sourceIter.Current() == '=')
				{
					Advance();
					return Token(GetSpan(), TokenKind::BINARY_AND_ASSIGN);
				}
			}

			return Token(GetSpan(), TokenKind::BINARY_AND);
		case '|':
			if (Advance())
			{
				if (sourceIter.Current() == '|')
				{
					Advance();
					return Token(GetSpan(), TokenKind::BOOLEAN_OR);
				}
				else if (sourceIter.Current() == '=')
				{
					Advance();
					return Token(GetSpan(), TokenKind::BINARY_OR_ASSIGN);
				}
			}

			return Token(GetSpan(), TokenKind::BINARY_OR);
		case '^':
			if (Advance())
			{
				if (sourceIter.Current() == '=')
				{
					Advance();
					return Token(GetSpan(), TokenKind::BINARY_XOR_ASSIGN);
				}
			}

			return Token(GetSpan(), TokenKind::BINARY_XOR);
		case '\'':
			if (Advance() && sourceIter.HasNext())
			{
				char c0 = sourceIter.Current();
				Advance();
				char c1 = sourceIter.Current();
				Advance();
				if (c1 != '\'')
				{
					AddDiagnostic(GetSpan(), sourceIter.lineNumber, sourceIter.columnIndex, "Expected closing quote for char literal");
					return g_EmptyToken;
				}
				return Token(GetSpan(), TokenKind::CHAR_LITERAL, std::string(1, c0));
			}

			AddDiagnostic(GetSpan(), sourceIter.lineNumber, sourceIter.columnIndex, "Expected closing quote for char literal");
			return g_EmptyToken;
		case'\"':
			Advance();
			return NextStringLiteral();
		case'.':
			Advance();
			return Token(GetSpan(), TokenKind::DOT);
		case '~':
			Advance();
			return Token(GetSpan(), TokenKind::TILDE);
		case '`':
			Advance();
			return Token(GetSpan(), TokenKind::BACK_QUOTE);
		case ',':
			Advance();
			return Token(GetSpan(), TokenKind::COMMA);
		case '#':
			Advance();
			return Token(GetSpan(), TokenKind::HASH);
		default:
			Advance();
			AddDiagnostic(GetSpan(), sourceIter.lineNumber, sourceIter.columnIndex, "Unrecognized character encountered");
			return g_EmptyToken;
		}
	}

	Token Lexer::NextNumericLiteral()
	{
		StringBuilder stringBuilder;

		while (IsDigit(sourceIter.Current()))
		{
			stringBuilder.Append(sourceIter.Current());
			if (!Advance())
			{
				return Token(GetSpan(), TokenKind::INT_LITERAL, stringBuilder.ToString());
			}
		}

		if (sourceIter.Current() == '.')
		{
			stringBuilder.Append('.');

			if (!Advance())
			{
				return Token(GetSpan(), TokenKind::FLOAT_LITERAL, stringBuilder.ToString());
			}

			while (IsDigit(sourceIter.Current()))
			{
				stringBuilder.Append(sourceIter.Current());
				if (!Advance())
				{
					break;
				}
			}

			if (sourceIter.Current() == 'f' || sourceIter.Current() == 'F')
			{
				stringBuilder.Append(sourceIter.Current());
				Advance();
			}

			return Token(GetSpan(), TokenKind::FLOAT_LITERAL, stringBuilder.ToString());
		}

		return Token(GetSpan(), TokenKind::INT_LITERAL, stringBuilder.ToString());
	}

	Token Lexer::NextStringLiteral()
	{
		StringBuilder stringBuilder;

		while (sourceIter.Current() != '"')
		{
			if (IsNewLine(sourceIter.Current()))
			{
				AddDiagnostic(GetSpan(), sourceIter.lineNumber, sourceIter.columnIndex, "Unexpected termination of string literal");
				return g_EmptyToken;
			}

			if (sourceIter.Current() == '\\')
			{
				if (!Advance())
				{
					AddDiagnostic(GetSpan(), sourceIter.lineNumber, sourceIter.columnIndex, "Unexpected trailing backslash");
					return g_EmptyToken;
				}
				switch (sourceIter.Current())
				{
				case 'n':
					stringBuilder.Append('\n');
					break;
				case 'r':
					stringBuilder.Append('\r');
					break;
				case 't':
					stringBuilder.Append('\t');
					break;
				case '\\':
					stringBuilder.Append('\\');
					break;
				case '"':
					stringBuilder.Append('\"');
					break;
				default:
					AddDiagnostic(GetSpan(), sourceIter.lineNumber, sourceIter.columnIndex, "Invalid escaped character");
					return g_EmptyToken;
				}
			}
			else
			{
				stringBuilder.Append(sourceIter.Current());
			}

			if (!Advance())
			{
				AddDiagnostic(GetSpan(), sourceIter.lineNumber, sourceIter.columnIndex, "Unexpected termination of string literal");
				return g_EmptyToken;
			}
		}

		Advance();

		return Token(GetSpan(), TokenKind::STRING_LITERAL, stringBuilder.ToString());
	}

	Token Lexer::NextIdentifierOrKeyword()
	{
		StringBuilder stringBuilder;
		while (
			IsLetter(sourceIter.Current()) ||
			IsDigit(sourceIter.Current()) ||
			sourceIter.Current() == '_')
		{
			stringBuilder.Append(sourceIter.Current());
			if (!Advance())
			{
				break;
			}
		}

		std::string identifier = stringBuilder.ToString();
		if (identifier.compare("int") == 0)
		{
			return Token(GetSpan(), TokenKind::INT_KEYWORD);
		}
		else if (identifier.compare("float") == 0)
		{
			return Token(GetSpan(), TokenKind::FLOAT_KEYWORD);
		}
		else if (identifier.compare("bool") == 0)
		{
			return Token(GetSpan(), TokenKind::BOOL_KEYWORD);
		}
		else if (identifier.compare("string") == 0)
		{
			return Token(GetSpan(), TokenKind::STRING_KEYWORD);
		}
		else if (identifier.compare("char") == 0)
		{
			return Token(GetSpan(), TokenKind::CHAR_KEYWORD);
		}
		else if (identifier.compare("true") == 0)
		{
			return Token(GetSpan(), TokenKind::TRUE);
		}
		else if (identifier.compare("false") == 0)
		{
			return Token(GetSpan(), TokenKind::FALSE);
		}
		else if (identifier.compare("if") == 0)
		{
			return Token(GetSpan(), TokenKind::IF);
		}
		else if (identifier.compare("elif") == 0)
		{
			return Token(GetSpan(), TokenKind::ELIF);
		}
		else if (identifier.compare("else") == 0)
		{
			return Token(GetSpan(), TokenKind::ELSE);
		}
		else if (identifier.compare("do") == 0)
		{
			return Token(GetSpan(), TokenKind::DO);
		}
		else if (identifier.compare("while") == 0)
		{
			return Token(GetSpan(), TokenKind::WHILE);
		}
		else if (identifier.compare("break") == 0)
		{
			return Token(GetSpan(), TokenKind::BREAK);
		}
		else if (identifier.compare("yield") == 0)
		{
			return Token(GetSpan(), TokenKind::YIELD);
		}
		else if (identifier.compare("func") == 0)
		{
			return Token(GetSpan(), TokenKind::FUNCTION_DEF);
		}
		else if (identifier.compare("return") == 0)
		{
			return Token(GetSpan(), TokenKind::RETURN);
		}
		else
		{
			return Token(GetSpan(), TokenKind::IDENTIFIER, identifier);
		}
	}

	bool Lexer::IsNewLine(char c)
	{
		return c == '\n' || c == '\r';
	}

	bool Lexer::IsDigit(char c)
	{
		return c >= '0' && c <= '9';
	}

	bool Lexer::IsLetter(char c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
	}

	bool Lexer::IsSpace(char c)
	{
		return isspace(c);
	}

	// Consume all ignored characters and leave the source
	// iter one index before the next non-ingored char
	void Lexer::EatWhitespaceAndComments()
	{
		bool bAdvanced = true;
		while (!sourceIter.EndOfFileReached() && bAdvanced)
		{
			bAdvanced = false;
			char c0 = sourceIter.Current();

			if (IsSpace(c0))
			{
				Advance();
				bAdvanced = true;
				continue;
			}

			if (sourceIter.HasNext())
			{
				char c1 = sourceIter.PeekNext();

				if (c0 == '/' && c1 == '/')
				{
					Advance();
					bAdvanced = true;
					// Consume remainder of line
					while (Advance())
					{
						if (IsNewLine(sourceIter.Current()))
						{
							Advance();
							break;
						}
					}
				}
				else if (c0 == '/' && c1 == '*')
				{
					Advance();
					Advance();
					bAdvanced = true;

					// Consume (potentially nested) block comment(s)
					i32 levelsDeep = 1;
					while (sourceIter.HasNext())
					{
						char bc0 = sourceIter.Current();
						char bc1 = sourceIter.PeekNext();
						if (bc0 == '/' && bc1 == '*')
						{
							levelsDeep++;
							if (!Advance() || !Advance())
							{
								return;
							}
						}
						else if (bc0 == '*' && bc1 == '/')
						{
							levelsDeep--;
							if (!Advance() || !Advance())
							{
								return;
							}
						}
						else
						{
							Advance();
						}

						if (levelsDeep == 0)
						{
							break;
						}
					}

					if (levelsDeep != 0)
					{
						AddDiagnostic(GetSpan(), sourceIter.lineNumber, sourceIter.columnIndex, "Uneven number of block comment opens/closes");
					}
				}
			}
		}
	}

	TokenKind Lexer::Type1IfNextCharIsCElseType2(char c, TokenKind ifYes, TokenKind ifNo)
	{
		if (sourceIter.HasNext() && sourceIter.PeekNext() == c)
		{
			Advance();
			return ifYes;
		}
		else
		{
			return ifNo;
		}
	}

	void Lexer::AddDiagnostic(Span span, u32 lineNumber, u32 columnIndex, const std::string& message)
	{
		AddDiagnostic({ span, lineNumber, columnIndex, message });
	}

	void Lexer::AddDiagnostic(const Diagnostic& diagnostic)
	{
		diagnostics.push_back(diagnostic);
	}

	Span Lexer::GetSpan()
	{
		Span result = m_CurrentSpan;
		m_CurrentSpan = m_CurrentSpan.Clip();
		return result;
	}

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
