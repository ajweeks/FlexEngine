#include "stdafx.hpp"

#include "VirtualMachine/Frontend/Lexer.hpp"

#include "StringBuilder.hpp"
#include "VirtualMachine/Diagnostics.hpp"
#include "VirtualMachine/Frontend/Parser.hpp"

namespace flex
{
	struct Token g_EmptyToken = Token(Span(0, 0), TokenKind::_NONE, "");

	Lexer::Lexer(const char* sourceText, DiagnosticContainer* diagnosticContainer) :
		m_CurrentSpan(0, 0),
		diagnosticContainer(diagnosticContainer)
	{
		sourceIter = SourceIter(sourceText);
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
					return Token(GetSpan(), TokenKind::NOT_EQUAL);
				}
			}

			return Token(GetSpan(), TokenKind::BANG);
		case '?':
			Advance();
			return Token(GetSpan(), TokenKind::QUESTION);
		case '=':
			if (Advance())
			{
				if (sourceIter.Current() == '=')
				{
					Advance();
					return Token(GetSpan(), TokenKind::EQUAL_EQUAL);
				}
			}

			return Token(GetSpan(), TokenKind::EQUALS);
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
					return Token(GetSpan(), TokenKind::PLUS_EQUALS);
				}
				else if (sourceIter.Current() == '+')
				{
					Advance();
					return Token(GetSpan(), TokenKind::PLUS_PLUS);
				}
			}

			return Token(GetSpan(), TokenKind::PLUS);
		case '-':
			if (sourceIter.Has(1))
			{
				if (IsDigit(sourceIter.Peek(1)))
				{
					return NextNumericLiteral();
				}
				else
				{
					Advance();
					return Token(GetSpan(), TokenKind::MINUS);
				}
			}

			if (Advance())
			{
				if (sourceIter.Current() == '=')
				{
					Advance();
					return Token(GetSpan(), TokenKind::MINUS_EQUALS);
				}
				else if (sourceIter.Current() == '>')
				{
					Advance();
					return Token(GetSpan(), TokenKind::ARROW);
				}
				else if (sourceIter.Current() == '-')
				{
					Advance();
					return Token(GetSpan(), TokenKind::MINUS_MINUS);
				}
			}

			return Token(GetSpan(), TokenKind::MINUS);
		case '*':
			if (Advance())
			{
				if (sourceIter.Current() == '=')
				{
					Advance();
					return Token(GetSpan(), TokenKind::STAR_EQUALS);
				}
			}

			return Token(GetSpan(), TokenKind::STAR);
		case '/':
			if (Advance())
			{
				if (sourceIter.Current() == '=')
				{
					Advance();
					return Token(GetSpan(), TokenKind::FOR_SLASH_EQUALS);
				}
			}

			return Token(GetSpan(), TokenKind::FOR_SLASH);
		case '%':
			if (Advance())
			{
				if (sourceIter.Current() == '=')
				{
					Advance();
					return Token(GetSpan(), TokenKind::PERCENT_EQUALS);
				}
			}

			return Token(GetSpan(), TokenKind::PERCENT);
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
					return Token(GetSpan(), TokenKind::BINARY_AND_EQUALS);
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
					return Token(GetSpan(), TokenKind::BINARY_OR_EQUALS);
				}
			}

			return Token(GetSpan(), TokenKind::BINARY_OR);
		case '^':
			if (Advance())
			{
				if (sourceIter.Current() == '=')
				{
					Advance();
					return Token(GetSpan(), TokenKind::BINARY_XOR_EQUALS);
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
					diagnosticContainer->AddDiagnostic(GetSpan(), "Expected closing quote for char literal");
					return g_EmptyToken;
				}
				return Token(GetSpan(), TokenKind::CHAR_LITERAL, std::string(1, c0));
			}

			diagnosticContainer->AddDiagnostic(GetSpan(), "Expected closing quote for char literal");
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
			diagnosticContainer->AddDiagnostic(GetSpan(), "Unrecognized character encountered");
			return g_EmptyToken;
		}
	}

	Token Lexer::NextNumericLiteral()
	{
		StringBuilder stringBuilder;

		if (sourceIter.Current() == '-')
		{
			stringBuilder.Append('-');
			Advance();
		}

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
				diagnosticContainer->AddDiagnostic(GetSpan(), "Unexpected termination of string literal");
				return g_EmptyToken;
			}

			if (sourceIter.Current() == '\\')
			{
				if (!Advance())
				{
					diagnosticContainer->AddDiagnostic(GetSpan(), "Unexpected trailing backslash");
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
					diagnosticContainer->AddDiagnostic(GetSpan(), "Invalid escaped character");
					return g_EmptyToken;
				}
			}
			else
			{
				stringBuilder.Append(sourceIter.Current());
			}

			if (!Advance())
			{
				diagnosticContainer->AddDiagnostic(GetSpan(), "Unexpected termination of string literal");
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

		if (identifier.empty())
		{
			diagnosticContainer->AddDiagnostic(GetSpan(), "Expected identifier or keyword");
			return g_EmptyToken;
		}

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
		else if (identifier.compare("void") == 0)
		{
			return Token(GetSpan(), TokenKind::VOID_KEYWORD);
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
		else if (identifier.compare("for") == 0)
		{
			return Token(GetSpan(), TokenKind::FOR);
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
			return Token(GetSpan(), TokenKind::FUNC);
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
						diagnosticContainer->AddDiagnostic(GetSpan(), "Uneven number of block comment opens/closes");
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

	Span Lexer::GetSpan()
	{
		Span result = m_CurrentSpan;
		m_CurrentSpan = m_CurrentSpan.Clip();
		return result;
	}
} // namespace flex
