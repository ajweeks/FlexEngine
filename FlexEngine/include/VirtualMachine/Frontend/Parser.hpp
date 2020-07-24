#pragma once

#include "Helpers.hpp"
#include "VirtualMachine/Diagnostics.hpp"
#include "VirtualMachine/Frontend/Token.hpp"

#undef CHAR

namespace flex
{
	struct Lexer;
	enum class TokenKind;

	enum class UnaryOperatorType
	{
		PLUS,
		NEGATE,
		NOT,
		BIN_INVERT,

		_NONE
	};

	static const char* g_UnaryOperatorTypeStrings[] =
	{
		"+",
		"-",
		"!",
		"~",

		"NONE"
	};

	static_assert(ARRAY_LENGTH(g_UnaryOperatorTypeStrings) == ((size_t)UnaryOperatorType::_NONE + 1), "Length of g_UnaryOperatorTypeStrings must match number of entries in UnaryOperatorType enum");

	const char* UnaryOperatorTypeToString(UnaryOperatorType opType);

	UnaryOperatorType TokenKindToUnaryOperatorType(TokenKind tokenKind);

	enum class BinaryOperatorType
	{
		ASSIGN,
		ADD,
		ADD_ASSIGN,
		SUB,
		SUB_ASSIGN,
		MUL,
		MUL_ASSIGN,
		DIV,
		DIV_ASSIGN,
		MOD,
		MOD_ASSIGN,
		BIN_AND,
		BIN_OR,
		BIN_XOR,
		EQUAL_TEST,
		NOT_EQUAL_TEST,
		GREATER_TEST,
		GREATER_EQUAL_TEST,
		LESS_TEST,
		LESS_EQUAL_TEST,
		BOOLEAN_AND,
		BOOLEAN_OR,

		_NONE
	};

	static const char* g_BinaryOperatorTypeStrings[] =
	{
		"=",
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
		"&",
		"|",
		"^",
		"==",
		"!=",
		">",
		">=",
		"<",
		"<=",
		"&&",
		"||",

		"NONE"
	};

	static_assert(ARRAY_LENGTH(g_BinaryOperatorTypeStrings) == ((size_t)BinaryOperatorType::_NONE + 1), "Length of g_BinaryOperatorTypeStrings must match number of entries in BinaryOperatorType enum");

	const char* BinaryOperatorTypeToString(BinaryOperatorType opType);

	bool IsCompoundAssignment(BinaryOperatorType type);

	BinaryOperatorType TokenKindToBinaryOperatorType(TokenKind tokenKind);

	i32 GetBinaryOperatorPrecedence(TokenKind tokenKind);

	enum class TypeName
	{
		INT,
		INT_LIST,
		FLOAT,
		FLOAT_LIST,
		BOOL,
		BOOL_LIST,
		STRING,
		STRING_LIST,
		CHAR,
		CHAR_LIST,
		UNKNOWN,

		_NONE
	};

	static const char* g_TypeNameStrings[] =
	{
		"int",
		"int[]",
		"float",
		"float[]",
		"bool",
		"bool[]",
		"string",
		"string[]",
		"char",
		"char[]",
		"unknown",

		"NONE"
	};

	static_assert(ARRAY_LENGTH(g_TypeNameStrings) == ((size_t)TypeName::_NONE + 1), "Length of g_TypeNameStrings must match length of TypeName enum");

	std::string TypeNameToString(TypeName typeName);
	TypeName TokenKindToTypeName(TokenKind tokenKind);
	TypeName TypeNameToListVariant(TypeName baseType);

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

	struct Statement
	{
		explicit Statement(const Span& span);

		virtual ~Statement()
		{
		}

		virtual std::string ToString() const = 0;

		Span span;
	};

	struct Expression : Statement
	{
		Expression(Span span, TypeName typeName) :
			Statement(span),
			typeName(typeName)
		{
		}

		virtual ~Expression()
		{
		}

		Expression(Span span) :
			Statement(span)
		{
		}

		TypeName typeName = TypeName::UNKNOWN;
	};

	struct EmptyStatement final : public Statement
	{
		EmptyStatement(const Span& span) :
			Statement(span)
		{
		}

		virtual std::string ToString() const override
		{
			return ";";
		}
	};

	struct StatementBlock final : public Statement
	{
		StatementBlock(const Span& span, const std::vector<Statement*>& statements);
		virtual ~StatementBlock();

		virtual std::string ToString() const override;

		std::vector<Statement*> statements;
	};

	struct IfStatement final : public Statement
	{
		IfStatement(const Span& span, Expression* condition, Statement* then, Statement* otherwise) :
			Statement(span),
			condition(condition),
			then(then),
			otherwise(otherwise)
		{
		}

		virtual ~IfStatement();

		virtual std::string ToString() const override;

		Expression* condition = nullptr;
		Statement* then = nullptr;
		Statement* otherwise = nullptr;
	};

	struct ForStatement final : public Statement
	{
		ForStatement(const Span& span, Expression* setup, Expression* condition, Expression* update, Statement* body) :
			Statement(span),
			setup(setup),
			condition(condition),
			update(update),
			body(body)
		{
		}

		virtual ~ForStatement();

		virtual std::string ToString() const override;

		Expression* setup = nullptr;
		Expression* condition = nullptr;
		Expression* update = nullptr;
		Statement* body = nullptr;
	};

	struct WhileStatement final : public Statement
	{
		WhileStatement(const Span& span, Expression* condition, Statement* body) :
			Statement(span),
			condition(condition),
			body(body)
		{
		}

		virtual ~WhileStatement();

		virtual std::string ToString() const override;

		Expression* condition = nullptr;
		Statement* body = nullptr;
	};

	struct DoWhileStatement final : public Statement
	{
		DoWhileStatement(const Span& span, Expression* condition, Statement* body) :
			Statement(span),
			condition(condition),
			body(body)
		{
		}

		virtual ~DoWhileStatement();

		virtual std::string ToString() const override;

		Expression* condition = nullptr;
		Statement* body = nullptr;
	};

	struct FunctionDeclaration final : Statement
	{
		FunctionDeclaration(Span span, const std::string& name, const std::vector<struct Identifier*>& arguments, TypeName returnType, StatementBlock* body) :
			Statement(span),
			name(name),
			returnType(returnType),
			arguments(arguments),
			body(body)
		{
		}

		virtual ~FunctionDeclaration();

		virtual std::string ToString() const override;

		std::string name;
		TypeName returnType;
		std::vector<Identifier*> arguments;
		StatementBlock* body;
	};

	struct BreakStatement final : Statement
	{
		BreakStatement(Span span) :
			Statement(span)
		{
		}

		virtual std::string ToString() const override
		{
			return "break;";
		}
	};

	struct YieldStatement final : Statement
	{
		YieldStatement(Span span, Expression* yieldValue) :
			Statement(span),
			yieldValue(yieldValue)
		{
		}

		virtual ~YieldStatement();

		virtual std::string ToString() const override;

		Expression* yieldValue = nullptr;
	};

	struct ReturnStatement final : Statement
	{
		ReturnStatement(Span span, Expression* returnValue) :
			Statement(span),
			returnValue(returnValue)
		{
		}

		virtual ~ReturnStatement();

		virtual std::string ToString() const override;

		Expression* returnValue = nullptr;
	};

	struct Declaration final : Expression
	{
		Declaration(const Span& span, const std::string& identifierStr, Expression* initializer, TypeName typeName) :
			Expression(span, typeName),
			identifierStr(identifierStr),
			initializer(initializer)
		{
		}

		virtual ~Declaration();

		virtual std::string ToString() const override;

		std::string identifierStr;
		Expression* initializer = nullptr;
	};

	struct Identifier final : Expression
	{
		Identifier(const Span& span, const std::string& identifierStr, TypeName typeName) :
			Expression(span, typeName),
			identifierStr(identifierStr)
		{
		}

		Identifier(const Span& span, const std::string& identifierStr) :
			Expression(span),
			identifierStr(identifierStr)
		{
		}

		virtual std::string ToString() const override
		{
			return identifierStr;
		}

		std::string identifierStr;
	};

	struct Assignment final : Expression
	{
		Assignment(const Span& span, Identifier* lhs, Expression* rhs) :
			Expression(span),
			lhs(lhs),
			rhs(rhs)
		{
		}

		virtual ~Assignment();

		virtual std::string ToString() const override;

		Identifier* lhs;
		Expression* rhs;
	};

	struct IntLiteral final : Expression
	{
		IntLiteral(const Span& span, i32 value) :
			Expression(span, TypeName::INT),
			value(value)
		{
		}

		virtual std::string ToString() const override
		{
			return IntToString(value);
		}

		i32 value;
	};

	struct FloatLiteral final : Expression
	{
		FloatLiteral(const Span& span, real value) :
			Expression(span, TypeName::FLOAT),
			value(value)
		{
		}

		virtual std::string ToString() const override
		{
			return FloatToString(value) + "f";
		}

		real value;
	};

	struct BoolLiteral final : Expression
	{
		BoolLiteral(const Span& span, bool value) :
			Expression(span, TypeName::BOOL),
			value(value)
		{
		}

		virtual std::string ToString() const override
		{
			return value ? "true" : "false";
		}

		bool value;
	};

	struct StringLiteral final : Expression
	{
		StringLiteral(const Span& span, const std::string& value) :
			Expression(span, TypeName::STRING),
			value(value)
		{
		}

		virtual std::string ToString() const override
		{
			return "\"" + value + "\"";
		}

		std::string value;
	};

	struct CharLiteral final : Expression
	{
		CharLiteral(const Span& span, char value) :
			Expression(span, TypeName::CHAR),
			value(value)
		{
		}

		virtual std::string ToString() const override
		{
			return std::string(1, value);
		}

		char value;
	};

	struct ListInitializer final : Expression
	{
		ListInitializer(const Span& span, const std::vector<Expression*>& listValues) :
			Expression(span),
			listValues(listValues)
		{
		}

		virtual ~ListInitializer();

		virtual std::string ToString() const override;

		std::vector<Expression*> listValues;
	};

	struct IndexOperation final : Expression
	{
		IndexOperation(const Span& span, const std::string& container, Expression* indexExpression) :
			Expression(span),
			container(container),
			indexExpression(indexExpression)
		{
		}

		virtual ~IndexOperation();

		virtual std::string ToString() const override;

		std::string container;
		Expression* indexExpression = nullptr;
	};

	struct UnaryOperation final : Expression
	{
		UnaryOperation(Span span, UnaryOperatorType type, Expression* expression) :
			Expression(span),
			expression(expression),
			type(type)
		{
		}

		virtual ~UnaryOperation();

		virtual std::string ToString() const override;

		Expression* expression = nullptr;
		UnaryOperatorType type;
	};

	struct BinaryOperation final : Expression
	{
		BinaryOperation(Span span, BinaryOperatorType type, Expression* lhs, Expression* rhs) :
			Expression(span),
			type(type),
			lhs(lhs),
			rhs(rhs)
		{
		}

		virtual ~BinaryOperation();

		virtual std::string ToString() const override;

		BinaryOperatorType type;
		Expression* lhs = nullptr;
		Expression* rhs = nullptr;
	};

	struct TernaryOperation final : Expression
	{
		TernaryOperation(Span span, Expression* condition, Expression* ifTrue, Expression* ifFalse) :
			Expression(span),
			condition(condition),
			ifTrue(ifTrue),
			ifFalse(ifFalse)
		{
		}

		virtual ~TernaryOperation();

		virtual std::string ToString() const override;

		Expression* condition;
		Expression* ifTrue;
		Expression* ifFalse;
	};

	struct FunctionCall final : Expression
	{
		FunctionCall(Span span, const std::string& target, const std::vector<Expression*>& arguments) :
			Expression(span),
			target(target),
			arguments(arguments)
		{
		}

		virtual ~FunctionCall();

		virtual std::string ToString() const override;

		std::string target;
		std::vector<Expression*> arguments;
	};

	class Parser
	{
	public:
		Parser(Lexer* lexer, DiagnosticContainer* diagnosticContainer);

		Parser(Parser&) = delete;
		Parser(Parser&&) = delete;
		Parser& operator=(Parser&) = delete;
		Parser& operator=(Parser&&) = delete;

		bool HasNext();
		bool NextIs(TokenKind tokenKind);
		bool NextIsTypename();
		bool NextIsBinaryOperator();
		Token Eat(TokenKind tokenKind);

		StatementBlock* Parse();
		Statement* NextStatement();
		Expression* NextExpression();
		Expression* NextPrimary();
		Expression* NextBinary(i32 lhsPrecendence, Expression* lhs);
		UnaryOperation* NextUnary();
		TernaryOperation* NextTernary(Expression* condition);
		ListInitializer* NextListInitializer();
		Statement* NextIfStatement();
		Statement* NextForStatement();
		Statement* NextWhileStatement();
		Statement* NextDoWhileStatement();
		FunctionDeclaration* NextFunctionDeclaration();
		StatementBlock* NextStatementBlock();
		std::vector<Identifier*> NextArgumentDefinitionList();
		std::vector<Expression*> NextArgumentList();

	private:
		Token m_Current;

		Lexer* m_Lexer = nullptr;
		DiagnosticContainer* diagnosticContainer = nullptr;
	};

	struct AST
	{
		explicit AST();

		AST(AST&) = delete;
		AST(AST&&) = delete;
		AST& operator=(AST&) = delete;
		AST& operator=(AST&&) = delete;

		void Generate(const std::string& sourceText);
		void Destroy();

		StatementBlock* rootBlock = nullptr;

		DiagnosticContainer* diagnosticContainer = nullptr;
		Parser* parser = nullptr;
		Lexer* lexer = nullptr;
		bool bValid = false;
	};
} // namespace flex
