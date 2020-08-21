#pragma once

#include "VirtualMachine/Diagnostics.hpp"
#include "VirtualMachine/Frontend/Token.hpp"

namespace flex
{
	struct DiagnosticContainer;

	extern Token g_EmptyToken;

	struct SourceIter
	{
		SourceIter() :
			source(""),
			index(0),
			lineNumber(0),
			columnIndex(0)
		{
		}

		SourceIter(const std::string& source) :
			source(source),
			index(0),
			lineNumber(0),
			columnIndex(0)
		{
		}

		char Current()
		{
			return source[index];
		}

		bool HasNext()
		{
			return (index + 1) < (u32)source.size();
		}

		bool Has(u32 offset)
		{
			return (index + offset) < (u32)source.size();
		}

		char PeekNext()
		{
			return source[index + 1];
		}

		char Peek(u32 offset)
		{
			return source[index + offset];
		}

		// Returns true if iter is valid, false means eof reached
		bool MoveNext()
		{
			++index;
			++columnIndex;

			return (index != (u32)source.size());
		}

		bool EndOfFileReached()
		{
			return (index == (u32)source.size());
		}

		std::string ToSource(u32 start, u32 end)
		{
			return source.substr(start, end - start);
		}

		std::string source;
		u32 lineNumber;
		u32 columnIndex;
		u32 index;
	};

	struct Lexer
	{
		Lexer(const char* sourceText, DiagnosticContainer* diagnosticContainer);

		Lexer(Lexer&) = delete;
		Lexer(Lexer&&) = delete;
		Lexer& operator=(Lexer&) = delete;
		Lexer& operator=(Lexer&&) = delete;

		bool Advance();

		Token Next();
		Token NextNumericLiteral();
		Token NextStringLiteral();
		Token NextIdentifierOrKeyword();

		bool IsNewLine(char c);
		bool IsDigit(char c);
		bool IsLetter(char c);
		bool IsSpace(char c);

		// Consumes all ignored characters and advances the
		// source iter to point at the next non-ignored char
		void EatWhitespaceAndComments();
		TokenKind Type1IfNextCharIsCElseType2(char c, TokenKind ifYes, TokenKind ifNo);

		SourceIter sourceIter;
		DiagnosticContainer* diagnosticContainer = nullptr;

		Span GetSpan();

		Span m_CurrentSpan;

	};
} // namespace flex
