#include "stdafx.hpp"

#include "VirtualMachine.hpp"

IGNORE_WARNINGS_PUSH
IGNORE_WARNINGS_POP

#include "Helpers.hpp"

namespace flex
{
	struct Token g_EmptyToken = Token();

	OperatorType Operator::Parse(Tokenizer& tokenizer)
	{
		Token token = tokenizer.GetNextToken();
		switch (token.type)
		{
		case TokenType::ADD: return OperatorType::ADD;
		case TokenType::SUBTRACT: return OperatorType::SUB;
		case TokenType::MULTIPLY: return OperatorType::MUL;
		case TokenType::DIVIDE: return OperatorType::DIV;
		case TokenType::MODULO: return OperatorType::MOD;
		case TokenType::BINARY_AND: return OperatorType::BIN_AND;
		case TokenType::BINARY_OR: return OperatorType::BIN_OR;
		case TokenType::BINARY_XOR: return OperatorType::BIN_XOR;
		case TokenType::EQUAL_TEST: return OperatorType::EQUAL;
		case TokenType::NOT_EQUAL_TEST: return OperatorType::NOT_EQUAL;
		case TokenType::GREATER: return OperatorType::GREATER;
		case TokenType::GREATER_EQUAL: return OperatorType::GREATER_EQUAL;
		case TokenType::LESS: return OperatorType::LESS;
		case TokenType::LESS_EQUAL: return OperatorType::LESS_EQUAL;
		case TokenType::BOOLEAN_AND: return OperatorType::BOOLEAN_AND;
		case TokenType::BOOLEAN_OR: return OperatorType::BOOLEAN_OR;
		default:
		{
			tokenizer.context->errorReason = "Unexpected operator";
			tokenizer.context->errorToken = token;
			return OperatorType::_NONE;
		}
		}
	}

	std::string Token::ToString() const
	{
		return std::string(charPtr, len);
	}

	TokenContext::TokenContext()
	{
		Reset();
	}

	TokenContext::~TokenContext()
	{
		if (instantiatedIdentifiers != nullptr)
		{
			for (i32 i = 0; i < variableCount; ++i)
			{
				assert(instantiatedIdentifiers[i].value != nullptr);
				delete instantiatedIdentifiers[i].value;
			}
			delete[] instantiatedIdentifiers;
		}
		instantiatedIdentifiers = nullptr;
		variableCount = 0;
	}

	void TokenContext::Reset()
	{
		buffer = nullptr;
		bufferPtr = nullptr;
		bufferLen = -1;
		errorReason = "";
		errorToken = g_EmptyToken;
		linePos = 0;
		lineNumber = 0;

		errors.clear();

		if (instantiatedIdentifiers != nullptr)
		{
			for (i32 i = 0; i < variableCount; ++i)
			{
				assert(instantiatedIdentifiers[i].value != nullptr);
				delete instantiatedIdentifiers[i].value;
			}
			delete[] instantiatedIdentifiers;
		}
		variableCount = 0;
		instantiatedIdentifiers = new InstantiatedIdentifier[MAX_VARS];
		tokenNameToInstantiatedIdentifierIdx.clear();
	}

	bool TokenContext::HasNextChar() const
	{
		return GetRemainingLength() > 0;
	}

	char TokenContext::ConsumeNextChar()
	{
		assert((bufferPtr + 1 - buffer) <= bufferLen);

		char nextChar = bufferPtr[0];
		bufferPtr++;
		linePos++;
		if (nextChar == '\n')
		{
			linePos = 0;
			lineNumber++;
		}
		return nextChar;
	}

	char TokenContext::PeekNextChar() const
	{
		assert((bufferPtr - buffer) <= bufferLen);

		return bufferPtr[0];
	}

	char TokenContext::PeekChar(i32 index) const
	{
		assert(index >= 0 && index < GetRemainingLength());

		return bufferPtr[index];

	}

	glm::i32 TokenContext::GetRemainingLength() const
	{
		return bufferLen - (bufferPtr - buffer);
	}

	bool TokenContext::CanNextCharBeIdentifierPart() const
	{
		if (!HasNextChar())
		{
			return false;
		}

		char next = PeekNextChar();
		if (isalpha(next) || isdigit(next) || next == '_')
		{
			return true;
		}

		return false;
	}

	TokenType TokenContext::AttemptParseKeyword(const char* keywordStr, TokenType keywordType)
	{
		i32 keywordPos = 1; // Assume first letter is already consumed
		char c = PeekNextChar();
		if (!(isalpha(c) || isdigit(c) || c == '_'))
		{
			// Next char is delimiter, token must be one char long
			return TokenType::IDENTIFIER;
		}

		while (keywordPos < (i32)strlen(keywordStr) && PeekNextChar() == keywordStr[keywordPos])
		{
			c = ConsumeNextChar();
			++keywordPos;
		}

		char nc = PeekNextChar();
		const bool bCIsDelimiter = !(isalpha(nc) || isdigit(nc) || nc == '_');
		if (keywordPos == (i32)strlen(keywordStr) && bCIsDelimiter)
		{
			return keywordType;
		}

		// Not this keyword, must be a identifier
		while (CanNextCharBeIdentifierPart())
		{
			ConsumeNextChar();
		}

		return TokenType::IDENTIFIER;
	}

	TokenType TokenContext::AttemptParseKeywords(const char* potentialKeywordStrs[], TokenType potentialKeywordTypes[], i32 keywordPositions[], i32 potentialCount)
	{
		char c = PeekNextChar();
		if (!(isalpha(c) || isdigit(c) || c == '_'))
		{
			// Next char is delimiter, token must be one char long
			return TokenType::IDENTIFIER;
		}

		for (i32 i = 0; i < potentialCount; ++i)
		{
			keywordPositions[i] = 1;
		}

		i32 matchedKeywordIndex = -1;
		bool bMatched = true;

		while (bMatched)
		{
			bMatched = false;
			for (i32 i = 0; i < potentialCount; ++i)
			{
				if (keywordPositions[i] != -1)
				{
					if (c == potentialKeywordStrs[i][keywordPositions[i]])
					{
						bMatched = true;
						if (++keywordPositions[i] >= (i32)strlen(potentialKeywordStrs[i]))
						{
							bool bLastChar = (GetRemainingLength() == 1);
							char nc = bLastChar ? ' ' : PeekChar(1);
							if (bLastChar || !(isalpha(nc) || isdigit(nc) || nc == '_'))
							{
								matchedKeywordIndex = i;
								ConsumeNextChar();
								break;
							}
							else
							{
								keywordPositions[i] = -1;
								bMatched = false;
							}
						}
					}
					else
					{
						keywordPositions[i] = -1;
					}
				}
			}

			if (matchedKeywordIndex != -1)
			{
				return potentialKeywordTypes[matchedKeywordIndex];
			}

			if (bMatched)
			{
				ConsumeNextChar();
				c = PeekNextChar();
			}
			else
			{
				// Doesn't match any keywords, must be a identifier

				while (CanNextCharBeIdentifierPart())
				{
					ConsumeNextChar();
				}
				return TokenType::IDENTIFIER;
			}
		}

		return TokenType::_NONE;
	}

	Value* TokenContext::InstantiateIdentifier(const Token& identifierToken, TypeName typeName)
	{
		const std::string tokenName = identifierToken.ToString();

		if (tokenNameToInstantiatedIdentifierIdx.find(tokenName) != tokenNameToInstantiatedIdentifierIdx.end())
		{
			errorReason = "Redeclaration of type";
			errorToken = identifierToken;
			return nullptr;
		}

		const i32 nextIndex = variableCount++;
		tokenNameToInstantiatedIdentifierIdx.emplace(tokenName, nextIndex);

		assert(nextIndex >= 0 && nextIndex < MAX_VARS);

		assert(instantiatedIdentifiers[nextIndex].index == 0);
		assert(instantiatedIdentifiers[nextIndex].value == nullptr);

		switch (typeName)
		{
		case TypeName::INT:
			instantiatedIdentifiers[nextIndex].value = new Value(0, false);
			break;
		case TypeName::FLOAT:
			instantiatedIdentifiers[nextIndex].value = new Value(0.0f, false);
			break;
		case TypeName::BOOL:
			instantiatedIdentifiers[nextIndex].value = new Value(false, false);
			break;
		default:
			errorReason = "Unhandled identifier typename encountered in InstantiateIdentifier";
			errorToken = identifierToken;
			return nullptr;
		}
		// NOTE: Enforce a copy of the string
		instantiatedIdentifiers[nextIndex].name = tokenName;
		instantiatedIdentifiers[nextIndex].index = nextIndex;
		return instantiatedIdentifiers[nextIndex].value;
	}

	Value* TokenContext::GetVarInstanceFromToken(const Token& token)
	{
		assert(token.tokenID >= 0 && token.tokenID < MAX_VARS);
		std::string tokenName = token.ToString();
		if (tokenNameToInstantiatedIdentifierIdx.find(tokenName) == tokenNameToInstantiatedIdentifierIdx.end())
		{
			errorReason = "Use of undefined type";
			errorToken = token;
			return nullptr;
		}
		i32 idx = tokenNameToInstantiatedIdentifierIdx[tokenName];
		assert(idx >= 0 && idx < variableCount);
		return instantiatedIdentifiers[idx].value;
	}

	bool TokenContext::IsKeyword(const char* str)
	{
		for (const char* keyword : g_KeywordStrings)
		{
			if (strcmp(str, keyword) == 0)
			{
				return true;
			}
		}
		return false;
	}

	bool TokenContext::IsKeyword(TokenType type)
	{
		return (i32)type > (i32)TokenType::KEYWORDS_START &&
			(i32)type < (i32)TokenType::KEYWORDS_END;
	}

	Tokenizer::Tokenizer()
	{
		assert(context == nullptr);

		context = new TokenContext();
		SetCodeStr("");
	}

	Tokenizer::Tokenizer(const std::string& codeStr)
	{
		assert(context == nullptr);

		context = new TokenContext();
		SetCodeStr(codeStr);
	}

	Tokenizer::~Tokenizer()
	{
		delete context;
		context = nullptr;
		codeStrCopy = "";
	}

	void Tokenizer::SetCodeStr(const std::string& newCodeStr)
	{
		assert(context != nullptr);

		codeStrCopy = newCodeStr;

		context->Reset();
		context->buffer = context->bufferPtr = (char*)codeStrCopy.c_str();
		context->bufferLen = (i32)codeStrCopy.size();
		nextTokenID = 0;
		bConsumedLastParsedToken = true;
		lastParsedToken = g_EmptyToken;
	}

	Token Tokenizer::PeekNextToken()
	{
		if (bConsumedLastParsedToken)
		{
			lastParsedToken = GetNextToken();
			bConsumedLastParsedToken = false;
		}
		return lastParsedToken;
	}

	Token Tokenizer::GetNextToken()
	{
		if (!bConsumedLastParsedToken)
		{
			bConsumedLastParsedToken = true;
			return lastParsedToken;
		}

		ConsumeWhitespaceAndComments();

		char const* const tokenStart = context->bufferPtr;
		i32 tokenLineNum = context->lineNumber;
		i32 tokenLinePos = context->linePos;

		TokenType nextTokenType = TokenType::_NONE;

		if (context->HasNextChar())
		{
			char c = context->ConsumeNextChar();

			switch (c)
			{
				// Keywords:
			case 't':
				nextTokenType = context->AttemptParseKeyword("true", TokenType::KEY_TRUE);
				break;
			case 'f':
				nextTokenType = context->AttemptParseKeyword("false", TokenType::KEY_FALSE);
				break;
			case 'b':
			{
				const char* potentialKeywords[] = { "bool", "break" };
				TokenType potentialTokenTypes[] = { TokenType::KEY_BOOL, TokenType::KEY_BREAK };
				i32 pos[] = { 0, 0 };
				nextTokenType = context->AttemptParseKeywords(potentialKeywords, potentialTokenTypes, pos, ARRAY_LENGTH(pos));
			} break;
			case 'i':
			{
				const char* potentialKeywords[] = { "int", "if" };
				TokenType potentialTokenTypes[] = { TokenType::KEY_INT, TokenType::KEY_IF };
				i32 pos[] = { 0, 0 };
				nextTokenType = context->AttemptParseKeywords(potentialKeywords, potentialTokenTypes, pos, ARRAY_LENGTH(pos));
			} break;
			case 'e':
			{
				const char* potentialKeywords[] = { "else", "elif" };
				TokenType potentialTokenTypes[] = { TokenType::KEY_ELSE, TokenType::KEY_ELIF };
				i32 pos[] = { 0, 0 };
				nextTokenType = context->AttemptParseKeywords(potentialKeywords, potentialTokenTypes, pos, ARRAY_LENGTH(pos));
			} break;
			case 'd':
				nextTokenType = context->AttemptParseKeyword("do", TokenType::KEY_DO);
				break;
			case 'w':
				nextTokenType = context->AttemptParseKeyword("while", TokenType::KEY_WHILE);
				break;

				// Non-keywords
			case '{':
				nextTokenType = TokenType::OPEN_BRACKET;
				break;
			case '}':
				nextTokenType = TokenType::CLOSE_BRACKET;
				break;
			case '(':
				nextTokenType = TokenType::OPEN_PAREN;
				break;
			case ')':
				nextTokenType = TokenType::CLOSE_PAREN;
				break;
			case '[':
				nextTokenType = TokenType::OPEN_SQUARE_BRACKET;
				break;
			case ']':
				nextTokenType = TokenType::CLOSE_SQUARE_BRACKET;
				break;
			case ':':
				nextTokenType = Type1IfNextCharIsCElseType2(':', TokenType::DOUBLE_COLON, TokenType::SINGLE_COLON);
				break;
			case ';':
				nextTokenType = TokenType::SEMICOLON;
				break;
			case '!':
				nextTokenType = Type1IfNextCharIsCElseType2('=', TokenType::NOT_EQUAL_TEST, TokenType::BANG);
				break;
			case '?':
				nextTokenType = TokenType::TERNARY;
				break;
			case '=':
				nextTokenType = Type1IfNextCharIsCElseType2('=', TokenType::EQUAL_TEST, TokenType::ASSIGNMENT);
				break;
			case '>':
				nextTokenType = Type1IfNextCharIsCElseType2('=', TokenType::GREATER_EQUAL, TokenType::GREATER);
				break;
			case '<':
				nextTokenType = Type1IfNextCharIsCElseType2('=', TokenType::LESS_EQUAL, TokenType::LESS);
				break;
			case '+':
				nextTokenType = Type1IfNextCharIsCElseType2('=', TokenType::ADD_ASSIGN, TokenType::ADD);
				break;
			case '-':
				nextTokenType = Type1IfNextCharIsCElseType2('=', TokenType::SUBTRACT_ASSIGN, TokenType::SUBTRACT);
				break;
			case '*':
				nextTokenType = Type1IfNextCharIsCElseType2('=', TokenType::MULTIPLY_ASSIGN, TokenType::MULTIPLY);
				break;
			case '/':
				nextTokenType = Type1IfNextCharIsCElseType2('=', TokenType::DIVIDE_ASSIGN, TokenType::DIVIDE);
				break;
			case '%':
				nextTokenType = Type1IfNextCharIsCElseType2('=', TokenType::MODULO_ASSIGN, TokenType::MODULO);
				break;
			case '&':
				if (context->PeekNextChar() == '=')
				{
					nextTokenType = TokenType::BINARY_AND_ASSIGN;
				}
				else
				{
					nextTokenType = Type1IfNextCharIsCElseType2('&', TokenType::BOOLEAN_AND, TokenType::BINARY_AND);
				}
				break;
			case '|':
				if (context->PeekNextChar() == '=')
				{
					nextTokenType = TokenType::BINARY_OR_ASSIGN;
				}
				else
				{
					nextTokenType = Type1IfNextCharIsCElseType2('|', TokenType::BOOLEAN_OR, TokenType::BINARY_OR);
				}
				break;
			case '^':
				nextTokenType = Type1IfNextCharIsCElseType2('=', TokenType::BINARY_XOR_ASSIGN, TokenType::BINARY_XOR);
				break;
			case '\\':
				if (context->HasNextChar())
				{
					// Escaped char (unhandled)
					context->ConsumeNextChar();
				}
				else
				{
					context->errorReason = "Input ended with a single backslash";
				}
				break;
			case '\'':
			{
				nextTokenType = TokenType::STRING;
				while (context->HasNextChar())
				{
					char cn = context->ConsumeNextChar();
					if (cn == '\\' || cn == '\'')
					{
						break;
					}
				}
			} break;
			case'\"':
			{
				nextTokenType = TokenType::STRING;
				while (context->HasNextChar())
				{
					char cn = context->ConsumeNextChar();
					if (cn == '\\' || cn == '\"')
					{
						break;
					}
				}
			} break;
			case '~':
				nextTokenType = TokenType::TILDE;
				break;
			case '`':
				nextTokenType = TokenType::BACK_QUOTE;
				break;
			default:
				if (isalpha(c) || c == '_')
				{
					nextTokenType = TokenType::IDENTIFIER;
					while (context->CanNextCharBeIdentifierPart())
					{
						context->ConsumeNextChar();
					}
				}
				else if (isdigit(c) || c == '-' || c == '.')
				{
					// TODO: Handle "0x" prefix

					bool bConsumedDigit = isdigit(c);
					bool bConsumedDecimal = c == '.';
					bool bConsumedF = false;
					bool bValidNum = bConsumedDigit;

					if (context->HasNextChar())
					{
						bool bCharIsValid = true;
						char nc;
						do
						{
							nc = context->PeekNextChar();
							bCharIsValid = ValidDigitChar(nc);

							if (isdigit(nc))
							{
								bConsumedDigit = true;
								bValidNum = true;
							}

							if (nc == 'f')
							{
								// Ensure f comes last if at all
								bConsumedF = true;
								if (context->HasNextChar())
								{
									char nnc = context->PeekChar(1);
									if (ValidDigitChar(nnc))
									{
										context->errorReason = "Incorrectly formatted number, 'f' must be final character";
										bValidNum = false;
										break;
									}
								}
							}

							// Ensure . appears only once if at all
							if (nc == '.')
							{
								if (bConsumedDecimal)
								{
									context->errorReason = "Incorrectly formatted number";
									bValidNum = false;
								}
								else
								{
									bConsumedDecimal = true;
								}
							}

							if (bCharIsValid)
							{
								context->ConsumeNextChar();
							}
						} while (bValidNum && context->HasNextChar() && bCharIsValid && !bConsumedF);
					}

					if (bConsumedDigit)
					{
						if (bConsumedDecimal || bConsumedF)
						{
							nextTokenType = TokenType::FLOAT_LITERAL;
						}
						else
						{
							nextTokenType = TokenType::INT_LITERAL;
						}
					}
				}
				break;
			}
		}

		Token token = {};
		token.lineNum = tokenLineNum;
		token.linePos = tokenLinePos;
		token.charPtr = tokenStart;
		token.len = (context->bufferPtr - tokenStart);
		token.type = nextTokenType;
		token.tokenID = nextTokenID++;

		lastParsedToken = token;

		return token;
	}

	void Tokenizer::ConsumeWhitespaceAndComments()
	{
		while (context->GetRemainingLength() > 1)
		{
			char c0 = context->PeekChar(0);
			char c1 = context->PeekChar(1);

			if (isspace(c0))
			{
				context->ConsumeNextChar();
				continue;
			}
			else if (c0 == '/' && c1 == '/')
			{
				// Consume remainder of line
				while (context->HasNextChar())
				{
					char c = context->ConsumeNextChar();
					if (c == '\n')
					{
						break;
					}
				}
			}
			else if (c0 == '/' && c1 == '*')
			{
				context->ConsumeNextChar();
				context->ConsumeNextChar();
				// Consume (potentially nested) block comment(s)
				i32 levelsDeep = 1;
				while (context->HasNextChar())
				{
					char bc0 = context->ConsumeNextChar();
					char bc1 = context->PeekNextChar();
					if (bc0 == '/' && bc1 == '*')
					{
						levelsDeep++;
						context->ConsumeNextChar();
					}
					else if (bc0 == '*' && bc1 == '/')
					{
						levelsDeep--;
						context->ConsumeNextChar();
					}

					if (levelsDeep == 0)
					{
						break;
					}
				}

				if (levelsDeep != 0)
				{
					context->errorReason = "Uneven number of block comment opens/closes";
				}
			}
			else
			{
				return;
			}
		}
		if (context->HasNextChar())
		{
			if (isspace(context->PeekNextChar()))
			{
				context->ConsumeNextChar();
			}
		}
	}

	TokenType Tokenizer::Type1IfNextCharIsCElseType2(char c, TokenType ifYes, TokenType ifNo)
	{
		if (context->HasNextChar() && context->PeekNextChar() == c)
		{
			context->ConsumeNextChar();
			return ifYes;
		}
		else
		{
			return ifNo;
		}
	}

	bool Tokenizer::ValidDigitChar(char c)
	{
		return (isdigit(c) || c == 'f' || c == 'F' || c == '.');
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

	TypeName Type::Parse(Tokenizer& tokenizer)
	{
		Token token = tokenizer.GetNextToken();
		std::string tokenStr = token.ToString();
		TypeName result = GetTypeNameFromStr(tokenStr);
		if (result != TypeName::_NONE)
		{
			return result;
		}

		tokenizer.context->errorReason = "Expected typename";
		tokenizer.context->errorToken = token;
		return TypeName::_NONE;
	}

	Node::Node(const Token& token) :
		token(token)
	{
	}

	// TODO: Support implicit casting
	bool ValidOperatorOnType(OperatorType op, TypeName type)
	{
		switch (op)
		{
		case OperatorType::ADD:
		case OperatorType::SUB:
		case OperatorType::MUL:
		case OperatorType::DIV:
		case OperatorType::MOD:
		case OperatorType::BIN_AND:
		case OperatorType::BIN_OR:
		case OperatorType::BIN_XOR:
		case OperatorType::GREATER:
		case OperatorType::GREATER_EQUAL:
		case OperatorType::LESS:
		case OperatorType::LESS_EQUAL:
		case OperatorType::NEGATE:
			return (type == TypeName::INT || type == TypeName::FLOAT);
		case OperatorType::ASSIGN:
		case OperatorType::EQUAL:
		case OperatorType::NOT_EQUAL:
			return true;
		case OperatorType::BOOLEAN_AND:
		case OperatorType::BOOLEAN_OR:
			return (type == TypeName::BOOL);
		default:
			PrintError("Unhandled operator type in ValidOperatorOnType: %d\n", (i32)op);
			return false;
		}
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

	Identifier::Identifier(const Token& token, const std::string& identifierStr, TypeName type) :
		Node(token),
		identifierStr(identifierStr),
		type(type)
	{
	}

	Identifier* Identifier::Parse(Tokenizer& tokenizer, TypeName type)
	{
		Token token = tokenizer.GetNextToken();
		if (token.type != TokenType::IDENTIFIER)
		{
			tokenizer.context->errorReason = "Expected identifier";
			tokenizer.context->errorToken = token;
			return nullptr;
		}

		return new Identifier(token, token.ToString(), type);
	}

	Operation::Operation(const Token& token, Expression* lhs, OperatorType op, Expression* rhs) :
		Node(token),
		lhs(lhs),
		op(op),
		rhs(rhs)
	{
	}

	Operation::~Operation()
	{
		delete lhs;
		lhs = nullptr;
		delete rhs;
		rhs = nullptr;
	}

	template<class T>
	Value* EvaluateUnaryOperation(T* val, OperatorType op)
	{
		switch (op)
		{
		case OperatorType::NEGATE:			return new Value(-(*val), true);
		default:							return nullptr;
		}
	}

	template<class T>
	Value* EvaluateOperation(T* lhs, T* rhs, OperatorType op)
	{
		switch (op)
		{
		case OperatorType::ASSIGN:
			*lhs = *rhs;
			return nullptr;
		case OperatorType::ADD:				return new Value(*lhs + *rhs, true);
		case OperatorType::SUB:				return new Value(*lhs - *rhs, true);
		case OperatorType::MUL:				return new Value(*lhs * *rhs, true);
		case OperatorType::DIV:
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
		case OperatorType::BIN_AND:			return new Value((bool)*lhs & (bool)*rhs, true);
		case OperatorType::BIN_OR:			return new Value((bool)*lhs | (bool)*rhs, true);
		case OperatorType::BIN_XOR:			return new Value((bool)*lhs ^ (bool)*rhs, true);
		case OperatorType::EQUAL:			return new Value(*lhs == *rhs, true);
		case OperatorType::NOT_EQUAL:		return new Value(*lhs != *rhs, true);
		case OperatorType::GREATER:			return new Value(*lhs > *rhs, true);
		case OperatorType::GREATER_EQUAL:	return new Value(*lhs >= *rhs, true);
		case OperatorType::LESS:			return new Value(*lhs < *rhs, true);
		case OperatorType::LESS_EQUAL:		return new Value(*lhs <= *rhs, true);
		case OperatorType::BOOLEAN_AND:		return new Value(*lhs && *rhs, true);
		case OperatorType::BOOLEAN_OR:		return new Value(*lhs || *rhs, true);
		default:							return nullptr;
		}
	}

	Value* Operation::Evaluate(TokenContext& context)
	{
		Value* newVal = nullptr;

		Value* rhsVar = rhs->Evaluate(context);
		if (rhsVar == nullptr)
		{
			return nullptr;
		}

		if (lhs == nullptr)
		{
			switch (rhsVar->type)
			{
			case ValueType::INT_RAW:
				newVal = EvaluateUnaryOperation(&rhsVar->val.intRaw, op);
				break;
			case ValueType::FLOAT_RAW:
				newVal = EvaluateUnaryOperation(&rhsVar->val.floatRaw, op);
				break;
			case ValueType::BOOL_RAW:
				context.errorReason = "Only unary operation currently supported is negation, which is invalid on type bool";
				context.errorToken = rhs->token;
				return nullptr;
				//newVal = EvaluateUnaryOperation(&rhsVar->val.boolRaw, op);
				//break;
			default:
				context.errorReason = "Unexpected type name in operation";
				context.errorToken = rhs->token;
				break;
			}

			if (rhsVar->bIsTemporary)
			{
				delete rhsVar;
				rhsVar = nullptr;
			}

			if (newVal == nullptr)
			{
				context.errorReason = "Malformed unary operation";
				context.errorToken = rhs->token;
				return nullptr;
			}

			return newVal;
		}

		Value* lhsVar = lhs->Evaluate(context);

		if (lhsVar == nullptr)
		{
			context.errorReason = "Invalid expression";
			context.errorToken = lhs->token;
			return nullptr;
		}

		// TODO: Handle implicit conversions
		if (lhsVar->type != rhsVar->type)
		{
			context.errorReason = "Operation on different types";
			context.errorToken = lhs->token;
			if (lhsVar->bIsTemporary)
			{
				delete lhsVar;
				lhsVar = nullptr;
			}
			if (rhsVar->bIsTemporary)
			{
				delete rhsVar;
			}
			return nullptr;
		}

		switch (lhsVar->type)
		{
		case ValueType::INT_RAW:
			newVal = EvaluateOperation(&lhsVar->val.intRaw, &rhsVar->val.intRaw, op);
			break;
		case ValueType::FLOAT_RAW:
			newVal = EvaluateOperation(&lhsVar->val.floatRaw, &rhsVar->val.floatRaw, op);
			break;
		case ValueType::BOOL_RAW:
			newVal = EvaluateOperation(&lhsVar->val.boolRaw, &rhsVar->val.boolRaw, op);
			break;
		default:
			context.errorReason = "Unexpected type name in operation";
			context.errorToken = lhs->token;
			break;
		}

		if (lhsVar->bIsTemporary)
		{
			delete lhsVar;
			lhsVar = nullptr;
		}
		if (rhsVar->bIsTemporary)
		{
			delete rhsVar;
			rhsVar = nullptr;
		}

		if (newVal == nullptr)
		{
			context.errorReason = "Malformed operation";
			context.errorToken = token;
			return nullptr;
		}

		return newVal;
	}

	Operation* Operation::Parse(Tokenizer& tokenizer)
	{
		Token token = tokenizer.GetNextToken();

		Expression* lhs = Expression::Parse(tokenizer);
		if (lhs == nullptr)
		{
			return nullptr;
		}
		OperatorType op = Operator::Parse(tokenizer);
		if (op == OperatorType::_NONE)
		{
			delete lhs;
			return nullptr;
		}
		Expression* rhs = Expression::Parse(tokenizer);
		if (rhs == nullptr)
		{
			delete lhs;
			return nullptr;
		}
		return new Operation(token, lhs, op, rhs);
	}

	Value::Value() :
		type(ValueType::_NONE),
		bIsTemporary(true),
		val()
	{
	}

	Value::Value(Operation* opearation) :
		type(ValueType::OPERATION),
		bIsTemporary(false),
		val(opearation)
	{
	}

	Value::Value(Identifier* identifier) :
		type(ValueType::IDENTIFIER),
		bIsTemporary(false),
		val(identifier)
	{
	}

	Value::Value(i32 intRaw, bool bTemporary) :
		type(ValueType::INT_RAW),
		bIsTemporary(bTemporary),
		val(intRaw)
	{
	}

	Value::Value(real floatRaw, bool bTemporary) :
		type(ValueType::FLOAT_RAW),
		bIsTemporary(bTemporary),
		val(floatRaw)
	{
	}

	Value::Value(bool boolRaw, bool bTemporary) :
		type(ValueType::BOOL_RAW),
		bIsTemporary(bTemporary),
		val(boolRaw)
	{
	}

	Value::~Value()
	{
		switch (type)
		{
		case ValueType::INT_RAW:
		case ValueType::FLOAT_RAW:
		case ValueType::BOOL_RAW:
			// No memory to free
			break;
		case ValueType::IDENTIFIER:
			delete val.identifier;
			val.identifier = nullptr;
			break;
		case ValueType::OPERATION:
			delete val.operation;
			val.operation = nullptr;
			break;
		default:
			PrintError("Unhandled statement type in ~Value(): %d\n", (i32)type);
			break;
		}
	}

	std::string Value::ToString() const
	{
		switch (type)
		{
		case ValueType::INT_RAW: return IntToString(val.intRaw);
		case ValueType::FLOAT_RAW: return FloatToString(val.floatRaw, 2);
		case ValueType::BOOL_RAW: return BoolToString(val.boolRaw);
		default: return EMPTY_STRING;
		}
	}

	Expression::Expression(const Token& token, Operation* operation) :
		Node(token),
		value(operation)
	{
	}

	Expression::Expression(const Token& token, Identifier* identifier) :
		Node(token),
		value(identifier)
	{
	}

	Expression::Expression(const Token& token, i32 intRaw) :
		Node(token),
		value(intRaw, false)
	{
	}

	Expression::Expression(const Token& token, real floatRaw) :
		Node(token),
		value(floatRaw, false)
	{
	}

	Expression::Expression(const Token& token, bool boolRaw) :
		Node(token),
		value(boolRaw, false)
	{
	}

	Expression::~Expression()
	{
	}

	Value* Expression::Evaluate(TokenContext& context)
	{
		switch (value.type)
		{
		case ValueType::OPERATION:
			return value.val.operation->Evaluate(context);
		case ValueType::IDENTIFIER:
			// TODO: Apply implicit casting here when necessary
			return context.GetVarInstanceFromToken(token);
		case ValueType::INT_RAW:
		case ValueType::FLOAT_RAW:
		case ValueType::BOOL_RAW:
			return &value;
		}

		context.errorReason = "Unexpected value type";
		context.errorToken = token;
		return nullptr;
	}

	template<class T>
	bool CompareExpression(T* lhs, T* rhs, OperatorType op, TokenContext& context)
	{
		switch (op)
		{
		case OperatorType::EQUAL:			return *lhs == *rhs;
		case OperatorType::NOT_EQUAL:		return *lhs != *rhs;
		case OperatorType::GREATER:			return *lhs > *rhs;
		case OperatorType::GREATER_EQUAL:	return *lhs >= *rhs;
		case OperatorType::LESS:			return *lhs < *rhs;
		case OperatorType::LESS_EQUAL:		return *lhs <= *rhs;
		case OperatorType::BOOLEAN_AND:		return *lhs && *rhs;
		case OperatorType::BOOLEAN_OR:		return *lhs || *rhs;
		default:
			context.errorReason = "Unexpected operator on int in expression";
			context.errorToken = token;
		}
	}

	//bool Expression::Compare(TokenContext& context, Expression* other, OperatorType op)
	//{
		//if (value.type == ValueType::IDENTIFIER)
		//{

		//	context.GetVarInstanceFromToken(token)->val.identifier->;
		//}

		//if (value.type == ValueType::OPERATION)
		//{
		//	Value* newVal = value.val.operation->Evaluate(context);
		//	if (newVal->type == ValueType::BOOL_RAW)
		//	{
		//		bool bResult = newVal->val.boolRaw;
		//		if (newVal->bIsTemporary)
		//		{
		//			delete newVal);
		//		}
		//		return bResult;
		//	}
		//	else
		//	{
		//		context.errorReason = "Operation expression didn't evaluate to bool value";
		//		context.errorToken = token;
		//		return nullptr;
		//	}
		//}

	//	return false;
	//}

	Expression* Expression::Parse(Tokenizer& tokenizer)
	{
		Token token = tokenizer.PeekNextToken();
		if (token.type == TokenType::SUBTRACT)
		{
			tokenizer.GetNextToken();
			return new Expression(token, new Operation(token, nullptr, OperatorType::NEGATE, Expression::Parse(tokenizer)));
		}

		if (!tokenizer.context->HasNextChar())
		{
			return nullptr;
		}

		if (token.type == TokenType::IDENTIFIER)
		{
			Identifier* identifier = Identifier::Parse(tokenizer, TypeName::_NONE);
			if (identifier == nullptr)
			{
				return nullptr;
			}

			Token nextToken = tokenizer.PeekNextToken();
			if (nextToken.type == TokenType::SEMICOLON)
			{
				return new Expression(token, identifier);
			}
			else
			{
				OperatorType op = Operator::Parse(tokenizer);
				if (op == OperatorType::_NONE)
				{
					tokenizer.context->errorReason = "Expected operator";
					tokenizer.context->errorToken = token;
					delete identifier;
					return nullptr;
				}
				else
				{
					Expression* rhs = Expression::Parse(tokenizer);
					if (rhs == nullptr)
					{
						delete identifier;
						return nullptr;
					}
					Expression* lhs = new Expression(token, identifier);
					Operation* operation = new Operation(token, lhs, op, rhs);
					if (operation == nullptr)
					{
						delete identifier;
						delete lhs;
						delete rhs;
						return nullptr;
					}
					return new Expression(token, operation);
				}
			}
		}
		if (token.type == TokenType::INT_LITERAL)
		{
			i32 intRaw = ParseInt(tokenizer.GetNextToken().ToString());

			Token nextToken = tokenizer.PeekNextToken();
			if (nextToken.type == TokenType::SEMICOLON)
			{
				// TODO: Check able to be ended here
				tokenizer.GetNextToken();
				return new Expression(token, intRaw);
			}
			else
			{
				OperatorType op = OperatorType::_NONE;
				if (ExpectOperator(tokenizer, token, &op))
				{
					if (!ValidOperatorOnType(op, TypeName::INT))
					{
						tokenizer.context->errorReason = "Invalid operator on type int";
						tokenizer.context->errorToken = token;
						return nullptr;
					}
					Expression* rhs = Expression::Parse(tokenizer);
					if (rhs == nullptr)
					{
						return nullptr;
					}
					Expression* lhs = new Expression(token, intRaw);
					Operation* operation = new Operation(token, lhs, op, rhs);
					return new Expression(token, operation);
				}
				else
				{
					return nullptr;
				}
			}
		}
		if (token.type == TokenType::FLOAT_LITERAL)
		{
			real floatRaw = ParseFloat(tokenizer.GetNextToken().ToString());

			Token nextToken = tokenizer.PeekNextToken();
			if (nextToken.type == TokenType::SEMICOLON)
			{
				// TODO: Check able to be ended here
				tokenizer.GetNextToken();
				return new Expression(token, floatRaw);
			}
			else
			{
				OperatorType op = OperatorType::_NONE;
				if (ExpectOperator(tokenizer, token, &op))
				{
					if (!ValidOperatorOnType(op, TypeName::FLOAT))
					{
						tokenizer.context->errorReason = "Invalid operator on type float";
						tokenizer.context->errorToken = token;
						return nullptr;
					}
					Expression* rhs = Expression::Parse(tokenizer);
					if (rhs == nullptr)
					{
						return nullptr;
					}
					Expression* lhs = new Expression(token, floatRaw);
					Operation* operation = new Operation(token, lhs, op, rhs);
					return new Expression(token, operation);
				}
				else
				{
					return nullptr;
				}
			}
		}
		if (token.type == TokenType::KEY_TRUE || token.type == TokenType::KEY_FALSE)
		{
			bool boolRaw = ParseBool(tokenizer.GetNextToken().ToString());

			Token nextToken = tokenizer.PeekNextToken();
			if (nextToken.type == TokenType::CLOSE_PAREN)
			{
				return new Expression(token, boolRaw);
			}
			if (nextToken.type == TokenType::SEMICOLON)
			{
				// TODO: Check able to be ended here
				tokenizer.GetNextToken();
				return new Expression(token, boolRaw);
			}
			else
			{
				OperatorType op = OperatorType::_NONE;
				if (ExpectOperator(tokenizer, token, &op))
				{
					if (!ValidOperatorOnType(op, TypeName::BOOL))
					{
						tokenizer.context->errorReason = "Invalid operator on type bool";
						tokenizer.context->errorToken = token;
						return nullptr;
					}
					Expression* rhs = Expression::Parse(tokenizer);
					if (rhs == nullptr)
					{
						return nullptr;
					}
					Expression* lhs = new Expression(token, boolRaw);
					Operation* operation = new Operation(token, lhs, op, rhs);
					return new Expression(token, operation);
				}
				else
				{
					return nullptr;
				}
			}
		}

		TypeName typeName = Type::Parse(tokenizer);
		if (typeName != TypeName::_NONE)
		{
			Identifier* identifier = Identifier::Parse(tokenizer, typeName);
			if (identifier == nullptr)
			{
				tokenizer.context->errorReason = "Unexpected identifier after typename";
				tokenizer.context->errorToken = token;
				return nullptr;
			}
			return new Expression(token, identifier);
		}

		tokenizer.context->errorReason = "Unexpected expression type";
		tokenizer.context->errorToken = token;
		return nullptr;
	}

	bool Expression::ExpectOperator(Tokenizer &tokenizer, Token token, OperatorType* outOp)
	{
		*outOp = Operator::Parse(tokenizer);
		if (*outOp == OperatorType::_NONE)
		{
			tokenizer.context->errorToken = token;
			tokenizer.context->errorReason = "Expected '=' or ';' after int declaration";
			return false;
		}
		return true;
	}

	Assignment::Assignment(const Token& token,
		Identifier* identifier,
		Expression* rhs,
		TypeName typeName /* = TypeName::_NONE */) :
		Node(token),
		identifier(identifier),
		rhs(rhs),
		typeName(typeName)
	{
	}

	Assignment::~Assignment()
	{
		delete identifier;
		identifier = nullptr;
		delete rhs;
		rhs = nullptr;
	}

	void Assignment::Evaluate(TokenContext& context)
	{
		TypeName varTypeName = TypeName::_NONE;
		void* varVal = nullptr;
		{
			Value* var = nullptr;
			if (typeName == TypeName::_NONE)
			{
				var = context.GetVarInstanceFromToken(identifier->token);
				if (var == nullptr)
				{
					return;
				}
				varTypeName = ValueTypeToTypeName(var->type);
			}
			else
			{
				var = context.InstantiateIdentifier(identifier->token, typeName);
				if (var == nullptr)
				{
					return;
				}
				varTypeName = typeName;
			}

			if (var != nullptr)
			{
				switch (varTypeName)
				{
				case TypeName::INT:
					varVal = static_cast<void*>(&var->val.intRaw);
					break;
				case TypeName::FLOAT:
					varVal = static_cast<void*>(&var->val.floatRaw);
					break;
				case TypeName::BOOL:
					varVal = static_cast<void*>(&var->val.boolRaw);
					break;
				default:
					context.errorReason = "Unexpected variable type name";
					context.errorToken = token;
					return;
				}
			}
		}

		if (varVal == nullptr)
		{
			return;
		}

		if (rhs != nullptr)
		{
			switch (varTypeName)
			{
			case TypeName::INT:
			{
				Value* rhsVal = rhs->Evaluate(context);
				if (rhsVal == nullptr)
				{
					return;
				}
				if (rhsVal->type == ValueType::INT_RAW)
				{
					*static_cast<i32*>(varVal) = rhsVal->val.intRaw;
				}
				else if (rhsVal->type == ValueType::FLOAT_RAW)
				{
					*static_cast<i32*>(varVal) = (i32)rhsVal->val.floatRaw;
				}
				else
				{
					context.errorReason = "Invalid value type assigned to int";
					context.errorToken = token;
				}
				if (rhsVal->bIsTemporary)
				{
					delete rhsVal;
					rhsVal = nullptr;
				}
			} break;
			case TypeName::FLOAT:
			{
				Value* rhsVal = rhs->Evaluate(context);
				if (rhsVal == nullptr)
				{
					return;
				}
				if (rhsVal->type == ValueType::INT_RAW)
				{
					*static_cast<real*>(varVal) = (real)rhsVal->val.intRaw;
				}
				else if (rhsVal->type == ValueType::FLOAT_RAW)
				{
					*static_cast<real*>(varVal) = rhsVal->val.floatRaw;
				}
				else
				{
					context.errorReason = "Invalid value type assigned to float";
					context.errorToken = token;
				}
				if (rhsVal->bIsTemporary)
				{
					delete rhsVal;
					rhsVal = nullptr;
				}
			} break;
			case TypeName::BOOL:
			{
				Value* rhsVal = rhs->Evaluate(context);
				if (rhsVal == nullptr)
				{
					return;
				}
				if (rhsVal->type == ValueType::BOOL_RAW)
				{
					*static_cast<bool*>(varVal) = rhsVal->val.boolRaw;
				}
				else
				{
					context.errorReason = "Invalid value type assigned to bool";
					context.errorToken = token;
				}
				if (rhsVal->bIsTemporary)
				{
					delete rhsVal;
					rhsVal = nullptr;
				}
			} break;
			default:
			{
				context.errorReason = "Unexpected typename encountered in assignment";
				context.errorToken = token;
			} break;
			}
		}
	}

	Assignment* Assignment::Parse(Tokenizer& tokenizer)
	{
		Token token = tokenizer.PeekNextToken();

		TypeName typeName = TypeName::_NONE;
		if (Type::GetTypeNameFromStr(token.ToString()) != TypeName::_NONE)
		{
			typeName = Type::Parse(tokenizer);
		}

		Identifier* lhs = Identifier::Parse(tokenizer, typeName);
		if (lhs == nullptr)
		{
			return nullptr;
		}

		Token nextToken = tokenizer.GetNextToken();
		if (nextToken.type == TokenType::SEMICOLON)
		{
			delete lhs;
			tokenizer.context->errorReason = "Uninitialized variables are not supported. Add default value";
			tokenizer.context->errorToken = token;
			return nullptr;
		}
		if (nextToken.type != TokenType::ASSIGNMENT)
		{
			delete lhs;
			tokenizer.context->errorReason = "Expected '=' after identifier";
			tokenizer.context->errorToken = token;
			return nullptr;
		}

		Expression* rhs = Expression::Parse(tokenizer);
		if (rhs == nullptr)
		{
			delete lhs;
			return nullptr;
		}
		return new Assignment(token, lhs, rhs, typeName);
	}

	Statement::Statement(const Token& token,
		Assignment* assignment) :
		Node(token),
		type(StatementType::ASSIGNMENT),
		contents(assignment)
	{
	}

	Statement::Statement(const Token& token,
		StatementType type,
		IfStatement* ifStatement) :
		Node(token),
		type(type),
		contents(ifStatement)
	{
		assert(type == StatementType::IF || type == StatementType::ELIF);
	}

	Statement::Statement(const Token& token,
		Statement* elseStatement) :
		Node(token),
		type(StatementType::ELSE),
		contents(elseStatement)
	{
	}

	Statement::Statement(const Token& token,
		WhileStatement* whileStatement) :
		Node(token),
		type(StatementType::WHILE),
		contents(whileStatement)
	{
	}

	Statement::~Statement()
	{
		switch (type)
		{
		case StatementType::ASSIGNMENT:
			delete contents.assignment;
			contents.assignment = nullptr;
			break;
		case StatementType::IF:
		case StatementType::ELIF:
			delete contents.ifStatement;
			contents.ifStatement = nullptr;
			break;
		case StatementType::ELSE:
			delete contents.elseStatement;
			contents.elseStatement = nullptr;
			break;
		case StatementType::WHILE:
			delete contents.whileStatement;
			contents.whileStatement = nullptr;
			break;
		default:
			PrintError("Unhandled statement type in ~Statement(): %d\n", (i32)type);
			break;
		}
	}

	void Statement::Evaluate(TokenContext& context)
	{
		switch (type)
		{
		case StatementType::ASSIGNMENT:
			contents.assignment->Evaluate(context);
			break;
		case StatementType::IF:
			contents.ifStatement->Evaluate(context);
			break;
		case StatementType::ELIF:
		case StatementType::ELSE:
			assert(false);
			break;
		case StatementType::WHILE:
			contents.whileStatement->Evaluate(context);
		default:
			break;
		}
	}

	Statement* Statement::Parse(Tokenizer& tokenizer)
	{
		StatementType statementType = StatementType::_NONE;

		IfStatement* ifStatement = nullptr;
		Statement* elseStatement = nullptr;
		WhileStatement* whileStatement = nullptr;
		Assignment* assignmentStatement = nullptr;

		Token token = tokenizer.PeekNextToken();

		if (token.len == 0)
		{
			// Likely reached EOF
			return nullptr;
		}

		if (token.type == TokenType::SEMICOLON)
		{
			// Empty statement
			return nullptr;
		}
		if (token.type == TokenType::KEY_IF)
		{
			statementType = StatementType::IF;
			ifStatement = IfStatement::Parse(tokenizer);
			if (ifStatement == nullptr)
			{
				return nullptr;
			}
		}
		else if (token.type == TokenType::KEY_ELIF)
		{
			// elif statements should only be parsed in IfStatement::Parse
			tokenizer.context->errorReason = "Found elif with no matching if";
			tokenizer.context->errorToken = token;
		}
		else if (token.type == TokenType::KEY_ELSE)
		{
			tokenizer.GetNextToken(); // Consume 'else'

			{
				Token nextToken = tokenizer.GetNextToken();
				if (nextToken.type != TokenType::OPEN_BRACKET)
				{
					tokenizer.context->errorReason = "Expected '{' after else";
					tokenizer.context->errorToken = token;
					return nullptr;
				}
			}

			statementType = StatementType::ELSE;
			elseStatement = Statement::Parse(tokenizer);
			if (elseStatement == nullptr)
			{
				return nullptr;
			}

			{
				Token nextToken = tokenizer.GetNextToken();
				if (nextToken.type != TokenType::CLOSE_BRACKET)
				{
					tokenizer.context->errorReason = "Expected '}' after else body";
					tokenizer.context->errorToken = token;
					return nullptr;
				}
			}

		}
		else if (token.type == TokenType::KEY_WHILE)
		{
			statementType = StatementType::WHILE;
			whileStatement = WhileStatement::Parse(tokenizer);
			if (whileStatement == nullptr)
			{
				return nullptr;
			}
		}
		else
		{
			// Assigning to existing instance of var
			statementType = StatementType::ASSIGNMENT;
			assignmentStatement = Assignment::Parse(tokenizer);
			if (assignmentStatement == nullptr)
			{
				return nullptr;
			}
		}

		if (statementType == StatementType::_NONE)
		{
			if (tokenizer.context->errorReason.empty())
			{
				tokenizer.context->errorReason = "Expected statement";
				tokenizer.context->errorToken = token;
			}
			return nullptr;
		}

		Token nextToken = tokenizer.PeekNextToken();
		if (nextToken.type == TokenType::SEMICOLON)
		{
			tokenizer.GetNextToken();
		}

		switch (statementType)
		{
		case StatementType::ASSIGNMENT:
		{
			assert(assignmentStatement != nullptr);
			return new Statement(token, assignmentStatement);
		} break;
		case StatementType::IF:
		{
			assert(ifStatement != nullptr);
			return new Statement(token, statementType, ifStatement);
		} break;
		case StatementType::ELIF:
		{
			assert(false);
			return nullptr;
		} break;
		case StatementType::ELSE:
		{
			assert(elseStatement != nullptr);
			return new Statement(token, elseStatement);
		} break;
		case StatementType::WHILE:
		{
			assert(whileStatement != nullptr);
			return new Statement(token, whileStatement);
		} break;
		}

		tokenizer.context->errorReason = "Expected statement";
		tokenizer.context->errorToken = token;
		return nullptr;
	}

	IfStatement::IfStatement(const Token& token,
		Expression* condition,
		Statement* body,
		IfStatement* elseIfStatement) :
		Node(token),
		ifFalseAction(IfFalseAction::ELIF),
		condition(condition),
		body(body),
		ifFalseStatement(elseIfStatement)
	{
	}

	IfStatement::IfStatement(const Token& token,
		Expression* condition,
		Statement* body,
		Statement* elseStatement) :
		Node(token),
		ifFalseAction(IfFalseAction::ELSE),
		condition(condition),
		body(body),
		ifFalseStatement(elseStatement)
	{
	}

	IfStatement::IfStatement(const Token& token,
		Expression* condition,
		Statement* body) :
		Node(token),
		ifFalseAction(IfFalseAction::NONE),
		condition(condition),
		body(body),
		ifFalseStatement()
	{
	}

	IfStatement::~IfStatement()
	{
		delete condition;
		condition = nullptr;
		delete body;
		body = nullptr;
	}

	void IfStatement::Evaluate(TokenContext& context)
	{
		Value* bConditionResult = condition->Evaluate(context);
		if (bConditionResult->val.boolRaw)
		{
			body->Evaluate(context);
		}
		else
		{
			switch (ifFalseAction)
			{
			case IfFalseAction::NONE:
				break;
			case IfFalseAction::ELSE:
				ifFalseStatement.elseStatement->Evaluate(context);
				break;
			case IfFalseAction::ELIF:
				ifFalseStatement.elseIfStatement->Evaluate(context);
				break;
			default:
				context.errorReason = "Unhandled if false action";
				context.errorToken = token;
				break;
			}
		}

		if (bConditionResult->bIsTemporary)
		{
			delete bConditionResult;
		}
	}

	IfStatement* IfStatement::Parse(Tokenizer& tokenizer)
	{
		Token token = tokenizer.GetNextToken();
		if (token.type != TokenType::KEY_IF && token.type != TokenType::KEY_ELIF)
		{
			tokenizer.context->errorReason = "Expected if or elif";
			tokenizer.context->errorToken = token;
			return nullptr;
		}

		{
			Token nextToken = tokenizer.GetNextToken();
			if (nextToken.type != TokenType::OPEN_PAREN)
			{
				tokenizer.context->errorReason = "Expected '(' after if";
				tokenizer.context->errorToken = token;
				return nullptr;
			}
		}

		Expression* condition = Expression::Parse(tokenizer);
		if (condition == nullptr)
		{
			return nullptr;
		}

		{
			Token nextToken = tokenizer.GetNextToken();
			if (nextToken.type != TokenType::CLOSE_PAREN)
			{
				tokenizer.context->errorReason = "Expected ')' after if statement condition";
				tokenizer.context->errorToken = token;
				return nullptr;
			}
		}

		{
			Token nextToken = tokenizer.GetNextToken();
			if (nextToken.type != TokenType::OPEN_BRACKET)
			{
				tokenizer.context->errorReason = "Expected '{' after if statement condition";
				tokenizer.context->errorToken = token;
				delete condition;
				return nullptr;
			}
		}

		Statement* body = Statement::Parse(tokenizer);
		if (body == nullptr)
		{
			return nullptr;
		}

		{
			Token nextToken = tokenizer.GetNextToken();
			if (nextToken.type != TokenType::CLOSE_BRACKET)
			{
				tokenizer.context->errorReason = "Expected '}' after if statement body";
				tokenizer.context->errorToken = token;
				delete condition;
				delete body;
				return nullptr;
			}
		}

		IfStatement* elseIfStatement = nullptr;
		Statement* elseStatement = nullptr;

		Token nextToken = tokenizer.PeekNextToken();
		if (nextToken.type == TokenType::KEY_ELIF)
		{
			elseIfStatement = IfStatement::Parse(tokenizer);
			if (elseIfStatement == nullptr)
			{
				return nullptr;
			}
		}
		else if (nextToken.type == TokenType::KEY_ELSE)
		{
			elseStatement = Statement::Parse(tokenizer);
			if (elseStatement == nullptr)
			{
				return nullptr;
			}
		}

		if (elseIfStatement != nullptr)
		{
			return new IfStatement(token, condition, body, elseIfStatement);
		}
		else if (elseStatement != nullptr)
		{
			return new IfStatement(token, condition, body, elseStatement);
		}
		else
		{
			return new IfStatement(token, condition, body);
		}
	}

	WhileStatement::WhileStatement(const Token& token, Expression* condition, Statement* body) :
		Node(token),
		condition(condition),
		body(body)
	{
	}

	WhileStatement::~WhileStatement()
	{
		delete condition;
		condition = nullptr;
		delete body;
		body = nullptr;
	}

	void WhileStatement::Evaluate(TokenContext& context)
	{
		Value* result = condition->Evaluate(context);
		i32 iterationCount = 0;
		while (result->val.boolRaw)
		{
			body->Evaluate(context);
			Value* pResult = result;
			result = condition->Evaluate(context);

			assert(pResult->bIsTemporary);
			delete pResult;

			if (result == nullptr)
			{
				return;
			}

			if (++iterationCount > 4'194'303)
			{
				context.errorReason = "Exceeded max number of iterations in while loop";
				context.errorToken = token;
				return;
			}
		}
		assert(result->bIsTemporary);
		delete result;
	}

	WhileStatement* WhileStatement::Parse(Tokenizer& tokenizer)
	{
		Token token = tokenizer.GetNextToken();
		if (token.type != TokenType::KEY_WHILE)
		{
			tokenizer.context->errorReason = "Expected while";
			tokenizer.context->errorToken = token;
			return nullptr;
		}

		Expression* condition = Expression::Parse(tokenizer);
		if (condition == nullptr)
		{
			return nullptr;
		}

		// TODO: Handle single line while loops
		{
			Token nextToken = tokenizer.GetNextToken();
			if (nextToken.type != TokenType::OPEN_BRACKET)
			{
				tokenizer.context->errorReason = "Expected '{' after while statement";
				tokenizer.context->errorToken = token;
				delete condition;
				return nullptr;
			}
		}

		Statement* body = Statement::Parse(tokenizer);
		if (body == nullptr)
		{
			return nullptr;
		}

		{
			Token nextToken = tokenizer.GetNextToken();
			if (nextToken.type != TokenType::CLOSE_BRACKET)
			{
				tokenizer.context->errorReason = "Expected '}' after while statement body";
				tokenizer.context->errorToken = token;
				delete condition;
				delete body;
				return nullptr;
			}
		}

		return new WhileStatement(token, condition, body);
	}

	RootItem::RootItem(Statement* statement, RootItem* nextItem) :
		statement(statement),
		nextItem(nextItem)
	{
	}

	RootItem::~RootItem()
	{
		delete statement;
		statement = nullptr;
		delete nextItem;
		nextItem = nullptr;
	}

	void RootItem::Evaluate(TokenContext& context)
	{
		statement->Evaluate(context);

		if (!context.errorReason.empty())
		{
			return;
		}

		if (nextItem)
		{
			nextItem->Evaluate(context);
		}
	}

	RootItem* RootItem::Parse(Tokenizer& tokenizer)
	{
		Statement* rootStatement = Statement::Parse(tokenizer);
		if (rootStatement == nullptr ||
			!tokenizer.context->errorReason.empty())
		{
			return nullptr;
		}

		RootItem* nextItem = nullptr;

		if (tokenizer.context->HasNextChar())
		{
			nextItem = RootItem::Parse(tokenizer);
		}

		return new RootItem(rootStatement, nextItem);
	}

	AST::AST(Tokenizer* tokenizer) :
		tokenizer(tokenizer)
	{
	}

	void AST::Destroy()
	{
		delete rootItem;
		rootItem = nullptr;
		bValid = false;
	}

	void AST::Generate()
	{
		delete rootItem;

		bValid = false;

		rootItem = RootItem::Parse(*tokenizer);

		if (!tokenizer->context->errorReason.empty())
		{
			lastErrorTokenLocation = glm::vec2i(tokenizer->context->errorToken.linePos, tokenizer->context->errorToken.lineNum);
			lastErrorTokenLen = tokenizer->context->errorToken.len;
			tokenizer->context->errors = { Error(tokenizer->context->errorToken.lineNum, tokenizer->context->errorReason) };
			return;
		}

		bValid = true;

		lastErrorTokenLocation = glm::vec2i(-1);
		lastErrorTokenLen = 0;
	}

	void AST::Evaluate()
	{
		if (bValid == false)
		{
			return;
		}

		bool bSuccess = true;

		RootItem* currentItem = rootItem;
		while (currentItem != nullptr)
		{
			//Print("Type: %d\n", currentItem->statement->type);

			currentItem->statement->Evaluate(*tokenizer->context);
			if (tokenizer->context->errorReason.empty())
			{
				currentItem = currentItem->nextItem;
			}
			else
			{
				lastErrorTokenLocation = glm::vec2i(tokenizer->context->errorToken.linePos, tokenizer->context->errorToken.lineNum);
				lastErrorTokenLen = tokenizer->context->errorToken.len;
				bSuccess = false;
				tokenizer->context->errors = { Error(tokenizer->context->errorToken.lineNum, tokenizer->context->errorReason) };
				break;
			}
		}

		if (bSuccess)
		{
			lastErrorTokenLocation = glm::vec2i(-1);
			lastErrorTokenLen = 0;
		}
	}
} // namespace flex
