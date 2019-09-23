#pragma once

namespace flex
{
	struct AST;
	struct Expression;
	struct IfStatement;
	struct Statement;
	struct Tokenizer;
	struct Value;
	struct WhileStatement;
	enum class TypeName;

	enum class OperatorType
	{
		ASSIGN,
		ADD,
		SUB,
		MUL,
		DIV,
		MOD,
		BIN_AND,
		BIN_OR,
		BIN_XOR,
		EQUAL,
		NOT_EQUAL,
		GREATER,
		GREATER_EQUAL,
		LESS,
		LESS_EQUAL,
		BOOLEAN_AND,
		BOOLEAN_OR,
		NEGATE,

		/*
		- [] operator
		- +=, -=, *=, /=
		- ! operator
		- parentheses
		- power
		- fmod
		*/

		_NONE
	};

	bool ValidOperatorOnType(OperatorType op, TypeName type);

	struct Operator
	{
		static OperatorType Parse(Tokenizer& tokenizer);
	};

	struct Error
	{
		Error(i32 num, const std::string& str) :
			lineNumber(num),
			str(str)
		{}

		i32 lineNumber; // zero based
		std::string str;
	};

	enum class TokenType
	{
		ROOT,

		IDENTIFIER,
		SINGLE_COLON,
		DOUBLE_COLON,
		SEMICOLON,
		BANG,
		TERNARY,
		INT_LITERAL,
		FLOAT_LITERAL,
		ASSIGNMENT,
		//FUNCTION_DEF,
		//FUNCITON_CALL,
		OPEN_PAREN,
		CLOSE_PAREN,
		OPEN_BRACKET,
		CLOSE_BRACKET,
		OPEN_SQUARE_BRACKET,
		CLOSE_SQUARE_BRACKET,
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
		STRING,
		TILDE,
		BACK_QUOTE,

		KEYWORDS_START, // Not a valid type!
		//KEY_RETURN,
		KEY_INT,
		//KEY_FLOAT,
		KEY_BOOL,
		//KEY_STRING,
		KEY_TRUE,
		KEY_FALSE,
		KEY_IF,
		KEY_ELIF,
		KEY_ELSE,
		KEY_DO,
		KEY_WHILE,
		KEY_BREAK,
		//KEY_CONST,
		KEYWORDS_END, // Not a valid type!

		_NONE
	};

	static const char* g_KeywordStrings[] =
	{
		//"return",
		"int",
		//"float",
		"bool",
		"true",
		"false",
		"if",
		"elif",
		"else",
		"do",
		"while",
		"break",
		//"const",
	};

	static_assert(ARRAY_LENGTH(g_KeywordStrings) == (u32)((u32)TokenType::KEYWORDS_END - (u32)TokenType::KEYWORDS_START - 1), "Length of g_KeywordStrings must match number of keyword entries in TokenType enum");

	struct Token
	{
		std::string ToString() const;

		i32 lineNum; // zero based
		i32 linePos;
		i32 tokenID;
		TokenType type;
		char const* charPtr;
		i32 len;
	};

	extern Token g_EmptyToken;

	struct TokenContext
	{
		TokenContext();
		~TokenContext();

		void Reset();

		bool HasNextChar() const;
		char ConsumeNextChar();
		char PeekNextChar() const;
		char PeekChar(i32 index) const;
		i32 GetRemainingLength() const;

		bool CanNextCharBeIdentifierPart() const;

		TokenType AttemptParseKeyword(const char* keywordStr, TokenType keywordType);
		TokenType AttemptParseKeywords(const char* potentialKeywordStrs[], TokenType potentialKeywordTypes[], i32 pos[], i32 potentialCount);

		Value* InstantiateIdentifier(const Token& identifierToken, TypeName typeName);
		Value* GetVarInstanceFromToken(const Token& token);

		static bool IsKeyword(const char* str);
		static bool IsKeyword(TokenType type);

		const char* buffer = nullptr;
		i32 bufferLen = -1;
		char const* bufferPtr = nullptr;
		std::string errorReason;
		Token errorToken;
		i32 linePos = 0;
		i32 lineNumber = 0;

		std::vector<Error> errors;

		static const i32 MAX_VARS = 512;
		struct InstantiatedIdentifier
		{
			std::string name;
			i32 index = 0;
			Value* value = nullptr;
		};
		InstantiatedIdentifier* instantiatedIdentifiers = nullptr; // Array of length MAX_VARS
		i32 variableCount = 0;

		// TODO: Hash strings
		std::map<std::string, i32> tokenNameToInstantiatedIdentifierIdx;
	};

	struct Tokenizer
	{
		Tokenizer();
		explicit Tokenizer(const std::string& codeStr);
		~Tokenizer();

		void SetCodeStr(const std::string& newCodeStr);

		Token PeekNextToken();
		Token GetNextToken();

		void ConsumeWhitespaceAndComments();
		TokenType Type1IfNextCharIsCElseType2(char c, TokenType ifYes, TokenType ifNo);

		inline bool ValidDigitChar(char c);

		std::string codeStrCopy;
		TokenContext* context = nullptr;

		i32 nextTokenID = 0;

		// True when we have peeked a token but not yet consumed it
		// Prevents parsing a token multiple times when calling Peek followed by Get
		bool bConsumedLastParsedToken = true;
		Token lastParsedToken;
	};

	enum class TypeName
	{
		INT,
		FLOAT,
		BOOL,

		_NONE
	};

	static const char* g_TypeNameStrings[] =
	{
		"int",
		"float",
		"bool",

		"NONE"
	};

	static_assert(ARRAY_LENGTH(g_TypeNameStrings) == ((u32)TypeName::_NONE + 1), "Length of g_TypeNameStrings must match length of TypeName enum");

	struct Type
	{
		static TypeName GetTypeNameFromStr(const std::string& str);
		static TypeName Parse(Tokenizer& tokenizer);
	};

	struct Node
	{
		explicit Node(const Token& token);

		Token token;
	};

	enum class ValueType
	{
		OPERATION,
		IDENTIFIER,
		INT_RAW,
		FLOAT_RAW,
		BOOL_RAW,

		_NONE
	};

	TypeName ValueTypeToTypeName(ValueType valueType);
	ValueType TypeNameToValueType(TypeName typeName);

	struct Identifier : public Node
	{
		Identifier(const Token& token, const std::string& identifierStr, TypeName type);

		// type name will be _NONE when referencing existing identifiers
		static Identifier* Parse(Tokenizer& tokenizer, TypeName typeName);

		std::string identifierStr;
		TypeName type;
	};

	struct Operation : public Node
	{
		Operation(const Token& token, Expression* lhs, OperatorType op, Expression* rhs);
		~Operation();

		OperatorType op;
		Expression* lhs; // Is null when this is a unary operation
		Expression* rhs;

		// Returns the result of the comparison, or the new value of the lhs of the assignment
		Value* Evaluate(TokenContext& context);
		static Operation* Parse(Tokenizer& tokenizer);

	};

	struct Value
	{
		Value();
		explicit Value(Operation* opearation);
		explicit Value(Identifier* identifierValue);
		Value(i32 intRaw, bool bTemporary);
		Value(real floatRaw, bool bTemporary);
		Value(bool boolRaw, bool bTemporary);
		~Value();

		std::string ToString() const;

		ValueType type;

		bool bIsTemporary = true;

		union Val
		{
			Operation* operation;
			Identifier* identifier;
			i32 intRaw;
			real floatRaw;
			bool boolRaw;
			void* nullValue;

			Val(Operation* operation) : operation(operation) {}
			Val(Identifier* identifier) : identifier(identifier) {}
			Val(i32 intRaw) : intRaw(intRaw) {}
			Val(real floatRaw) : floatRaw(floatRaw) {}
			Val(bool boolRaw) : boolRaw(boolRaw) {}
			Val() : nullValue(nullptr) {}
		} val;
	};

	struct Expression : public Node
	{
		Expression(const Token& token, Operation* operation);
		Expression(const Token& token, i32 intRaw);
		Expression(const Token& token, real floatRaw);
		Expression(const Token& token, bool boolRaw);
		Expression(const Token& token, Identifier* identifier);
		~Expression();

		Value value;

		// Returns a pointer to the result of this expression
		Value* Evaluate(TokenContext& context);
		//bool Compare(TokenContext& context, Expression* other, OperatorType op);

		static Expression* Parse(Tokenizer& tokenizer);
		static bool ExpectOperator(Tokenizer &tokenizer, Token token, OperatorType* outOp);
	};

	struct Assignment : public Node
	{
		// If typename is specified, this assignment will instantiate var on evaluation
		// Otherwise, assignment is to pre-existing variable
		Assignment(const Token& token,
			Identifier* identifier,
			Expression* rhs,
			TypeName typeName = TypeName::_NONE);
		~Assignment();

		TypeName typeName = TypeName::_NONE; // Optional, not used when re-assigning
		Identifier* identifier = nullptr;
		Expression* rhs = nullptr; // If null, this assignment is actually just a declaration

		void Evaluate(TokenContext& context);
		// Should be called when tokenizer is pointing at char after '='
		static Assignment* Parse(Tokenizer& tokenizer);
	};

	enum class StatementType
	{
		ASSIGNMENT,
		IF,
		ELIF,
		ELSE,
		WHILE,

		_NONE
	};

	static const char* StatementTypeStrings[] =
	{
		"assignment",
		"If",
		"elif",
		"else",
		"while",

		"NONE"
	};

	static_assert(ARRAY_LENGTH(StatementTypeStrings) == (u32)StatementType::_NONE + 1, "Length of StatementTypeStrings must match length of StatementType enum");

	struct Statement : public Node
	{
		Statement(const Token& token, Assignment* assignment);
		Statement(const Token& token, StatementType type, IfStatement* ifStatement);
		Statement(const Token& token, Statement* elseStatement);
		Statement(const Token& token, WhileStatement* whileStatement);
		~Statement();

		StatementType type;
		// TODO: Rename
		union Contents
		{
			Assignment* assignment;
			IfStatement* ifStatement;
			Statement* elseStatement;
			WhileStatement* whileStatement;

			Contents(Assignment* assignment) : assignment(assignment) {}
			Contents(IfStatement* ifStatement) : ifStatement(ifStatement) {}
			Contents(Statement* elseStatement) : elseStatement(elseStatement) {}
			Contents(WhileStatement* whileStatement) : whileStatement(whileStatement) {}
		} contents;

		void Evaluate(TokenContext& context);
		static Statement* Parse(Tokenizer& tokenizer);
	};

	enum class IfFalseAction
	{
		ELSE,
		ELIF,
		NONE
	};

	struct IfStatement : public Node
	{
		IfStatement(const Token& token, Expression* condition, Statement* body, IfStatement* elseIfStatement);
		IfStatement(const Token& token, Expression* condition, Statement* body, Statement* elseStatement);
		IfStatement(const Token& token, Expression* condition, Statement* body);
		~IfStatement();

		union IfFalse
		{
			void* nothingStatement;
			IfStatement* elseIfStatement;
			Statement* elseStatement;

			IfFalse() : nothingStatement(nullptr) {}
			IfFalse(IfStatement* elseIfStatement) : elseIfStatement(elseIfStatement) {}
			IfFalse(Statement* elseStatement) : elseStatement(elseStatement) {}
		} ifFalseStatement;

		IfFalseAction ifFalseAction = IfFalseAction::NONE;
		Expression* condition = nullptr;
		Statement* body = nullptr;

		void Evaluate(TokenContext& context);
		static IfStatement* Parse(Tokenizer& tokenizer);
	};

	struct WhileStatement : public Node
	{
		WhileStatement(const Token& token, Expression* condition, Statement* body);
		~WhileStatement();

		Expression* condition = nullptr;
		Statement* body = nullptr;

		// TODO: Handle Do While loops

		void Evaluate(TokenContext& context);
		static WhileStatement* Parse(Tokenizer& tokenizer);
	};

	struct RootItem
	{
		RootItem(Statement* statement, RootItem* nextItem);
		~RootItem();

		Statement* statement = nullptr;
		RootItem* nextItem = nullptr;

		void Evaluate(TokenContext& context);
		static RootItem* Parse(Tokenizer& tokenizer);
	};

	struct AST
	{
		explicit AST(Tokenizer* tokenizer);

		void Generate();
		void Evaluate();
		void Destroy();

		RootItem* rootItem = nullptr;
		Tokenizer* tokenizer = nullptr;
		bool bValid = false;

		glm::vec2i lastErrorTokenLocation = glm::vec2i(-1);
		i32 lastErrorTokenLen = 0;
	};
} // namespace flex
