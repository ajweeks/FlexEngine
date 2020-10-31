#pragma once

#include "VirtualMachine/Frontend/Span.hpp"

#undef TRUE
#undef FALSE

namespace flex
{
	enum class TokenKind
	{
		IDENTIFIER,
		SINGLE_COLON,
		DOUBLE_COLON,
		SEMICOLON,
		BANG,
		QUESTION,
		INT_LITERAL,
		FLOAT_LITERAL,
		STRING_LITERAL,
		CHAR_LITERAL,
		TRUE,
		FALSE,
		INT_KEYWORD,
		FLOAT_KEYWORD,
		BOOL_KEYWORD,
		STRING_KEYWORD,
		CHAR_KEYWORD,
		VOID_KEYWORD,
		EQUALS,
		OPEN_PAREN,
		CLOSE_PAREN,
		OPEN_CURLY,
		CLOSE_CURLY,
		OPEN_SQUARE,
		CLOSE_SQUARE,
		DOT,
		PLUS,
		PLUS_EQUALS,
		PLUS_PLUS,
		MINUS,
		MINUS_EQUALS,
		MINUS_MINUS,
		STAR,
		STAR_EQUALS,
		FOR_SLASH,
		FOR_SLASH_EQUALS,
		PERCENT,
		PERCENT_EQUALS,
		EQUAL_EQUAL,
		NOT_EQUAL,
		GREATER,
		GREATER_EQUAL,
		LESS,
		LESS_EQUAL,
		BOOLEAN_AND,
		BOOLEAN_OR,
		BINARY_AND,
		BINARY_AND_EQUALS,
		BINARY_OR,
		BINARY_OR_EQUALS,
		BINARY_XOR,
		BINARY_XOR_EQUALS,
		TILDE,
		BACK_QUOTE,
		COMMA,
		HASH,
		ARROW,
		IF,
		ELIF,
		ELSE,
		FOR,
		DO,
		WHILE,
		BREAK,
		YIELD,
		FUNC,
		RETURN,
		END_OF_FILE,

		_NONE
	};

	static const char* g_TokenStrings[] =
	{
		"identifier",
		":",
		"::",
		";",
		"!",
		"?",
		"int literal",
		"float literal",
		"string literal",
		"char literal",
		"true",
		"false",
		"int",
		"float",
		"bool",
		"string",
		"char",
		"void",
		"=",
		"(",
		")",
		"{",
		"}",
		"[",
		"]",
		".",
		"+",
		"++",
		"+=",
		"-",
		"-=",
		"--",
		"*",
		"*=",
		"/",
		"/=",
		"%",
		"%=",
		"==",
		"!=",
		">",
		">=",
		"<",
		"<=",
		"&&",
		"||",
		"&",
		"&=",
		"|",
		"|=",
		"^",
		"^=",
		"~",
		"`",
		",",
		"#",
		"->",
		"if",
		"elif",
		"else",
		"for",
		"do",
		"while",
		"break",
		"yield",
		"func",
		"return",
		"eof",

		"NONE"
	};

	static_assert(ARRAY_LENGTH(g_TokenStrings) == ((size_t)TokenKind::_NONE + 1), "Length of g_TokenStrings must match number of keyword entries in TokenKind enum");

	const char* TokenKindToString(TokenKind tokenKind);

	struct Token
	{
		Token() :
			span(0, 0),
			kind(TokenKind::_NONE)
		{
		}

		Token(Span span, TokenKind kind, const std::string& value) :
			span(span),
			kind(kind),
			value(value)
		{
		}

		Token(Span span, TokenKind kind) :
			span(span),
			kind(kind)
		{
		}

		std::string ToString() const;

		Span span;
		TokenKind kind;
		// Optional, can be filled out for additional info
		std::string value;
	};

} // namespace flex
