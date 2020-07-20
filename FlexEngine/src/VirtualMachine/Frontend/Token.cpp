#include "stdafx.hpp"

#include "StringBuilder.hpp"
#include "VirtualMachine/Frontend/Token.hpp"

namespace flex
{
	const char* TokenKindToString(TokenKind tokenKind)
	{
		return g_TokenStrings[(u32)tokenKind];
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
} // namespace flex
