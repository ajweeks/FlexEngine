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
		TERNARY,
		INT_LITERAL,
		FLOAT_LITERAL,
		BOOL_LITERAL,
		STRING_LITERAL,
		CHAR_LITERAL,
		TRUE,
		FALSE,
		INT_KEYWORD,
		FLOAT_KEYWORD,
		BOOL_KEYWORD,
		STRING_KEYWORD,
		CHAR_KEYWORD,
		ASSIGNMENT,
		OPEN_PAREN,
		CLOSE_PAREN,
		OPEN_CURLY,
		CLOSE_CURLY,
		OPEN_SQUARE,
		CLOSE_SQUARE,
		DOT,
		ADD,
		ADD_ASSIGN,
		SUBTRACT,
		SUBTRACT_ASSIGN,
		MULTIPLY,
		MULTIPLY_ASSIGN,
		DIVIDE,
		DIVIDE_ASSIGN,
		MODULO,
		MODULO_ASSIGN,
		EQUAL_TEST,
		NOT_EQUAL_TEST,
		GREATER,
		GREATER_EQUAL,
		LESS,
		LESS_EQUAL,
		BOOLEAN_AND,
		BOOLEAN_OR,
		BINARY_AND,
		BINARY_AND_ASSIGN,
		BINARY_OR,
		BINARY_OR_ASSIGN,
		BINARY_XOR,
		BINARY_XOR_ASSIGN,
		TILDE,
		BACK_QUOTE,
		COMMA,
		HASH,
		ARROW,
		IF,
		ELIF,
		ELSE,
		DO,
		WHILE,
		BREAK,
		YIELD,
		FUNCTION_DEF,
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
		"bool literal",
		"string literal",
		"char literal",
		"true",
		"false",
		"int",
		"float",
		"bool",
		"string",
		"char",
		"=",
		"(",
		")",
		"{",
		"}",
		"[",
		"]",
		".",
		"+",
		"+=",
		"-",
		"-=",
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
