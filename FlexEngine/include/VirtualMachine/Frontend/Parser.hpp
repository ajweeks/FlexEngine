#pragma once

#include "VirtualMachine/Diagnostics.hpp"

namespace flex
{
	struct AST;
	struct Expression;
	struct IfStatement;
	struct Statement;
	struct Lexer;
	struct WhileStatement;
	enum class TypeName;

	enum class UnaryOperatorType
	{
		PLUS,
		NEGATE,
		NOT,

		_NONE
	};

	static const char* g_UnaryOperatorTypeStrings[] =
	{
		"+",
		"-",
		"!",

		"NONE"
	};

	static_assert(ARRAY_LENGTH(g_UnaryOperatorTypeStrings) == ((size_t)UnaryOperatorType::_NONE + 1), "Length of g_UnaryOperatorTypeStrings must match number of keyword entries in UnaryOperatorType enum");

	const char* UnaryOperatorTypeToString(UnaryOperatorType opType);

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

		/*
		- [] operator
		- +=, -=, *=, /=
		- ! operator
		*/

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

	static_assert(ARRAY_LENGTH(g_BinaryOperatorTypeStrings) == ((size_t)BinaryOperatorType::_NONE + 1), "Length of g_BinaryOperatorTypeStrings must match number of keyword entries in BinaryOperatorType enum");

	const char* BinaryOperatorTypeToString(BinaryOperatorType opType);

	bool IsCompoundAssignment(BinaryOperatorType type);

	enum class TypeName
	{
		INT,
		FLOAT,
		BOOL,
		STRING,

		_NONE
	};

	static const char* g_TypeNameStrings[] =
	{
		"int",
		"float",
		"bool",
		"bool",

		"NONE"
	};

	static_assert(ARRAY_LENGTH(g_TypeNameStrings) == ((size_t)TypeName::_NONE + 1), "Length of g_TypeNameStrings must match length of TypeName enum");

	struct Type
	{
		static TypeName GetTypeNameFromStr(const std::string& str);
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

	struct Statement
	{
		explicit Statement(const Span& span);

		virtual std::string ToString() const = 0;

		Span span;
	};

	struct EmptyStatement : public Statement
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

	struct StatementBlock : public Statement
	{
		explicit StatementBlock(const Span& span);

		void Push(Statement* statement);

		virtual std::string ToString() const override;

		std::vector<Statement*> statements;
	};

	struct IfStatement : public Statement
	{
		IfStatement(const Span& span, Expression* condition, Statement* then, Statement* otherwise) :
			Statement(span),
			condition(condition),
			then(then),
			otherwise(otherwise)
		{
		}

		virtual std::string ToString() const override;

		Expression* condition = nullptr;
		Statement* then = nullptr;
		Statement* otherwise = nullptr;
	};

	struct ForStatement : public Statement
	{
		ForStatement(const Span& span, Expression* setup, Expression* condition, Expression* update, Statement* body) :
			Statement(span),
			setup(setup),
			condition(condition),
			update(update),
			body(body)
		{
		}

		Expression* setup = nullptr;
		Expression* condition = nullptr;
		Expression* update = nullptr;
		Statement* body = nullptr;

		virtual std::string ToString() const override;
	};

	struct WhileStatement : public Statement
	{
		WhileStatement(const Span& span, Expression* condition, Statement* body) :
			Statement(span),
			condition(condition),
			body(body)
		{
		}

		Expression* condition = nullptr;
		Statement* body = nullptr;

		virtual std::string ToString() const override;
	};

	struct DoWhileStatement : public Statement
	{
		DoWhileStatement(const Span& span, Expression* condition, Statement* body) :
			Statement(span),
			condition(condition),
			body(body)
		{
		}

		Expression* condition = nullptr;
		Statement* body = nullptr;

		virtual std::string ToString() const override;
	};

	struct Expression : Statement
	{
		Expression(Span span) :
			Statement(span)
		{
		}
		~Expression();

		Type type;
	};

	struct Identifier : Expression
	{
		Identifier(const Span& span, const std::string& identifierStr, TypeName type) :
			Expression(span),
			identifierStr(identifierStr),
			type(type)
		{
		}

		std::string identifierStr;
		TypeName type;
	};

	struct BinaryOperation : Expression
	{
		BinaryOperation(Span span, BinaryOperatorType type, Expression* lhs, Expression* rhs) :
			Expression(span),
			type(type),
			lhs(lhs),
			rhs(rhs)
		{
		}

		virtual std::string ToString() const override
		{
			return lhs->ToString() + " " + BinaryOperatorTypeToString(type) + " " + rhs->ToString();
		}

		BinaryOperatorType type;
		Expression* lhs = nullptr;
		Expression* rhs = nullptr;
	};

	struct UnaryOperation : Expression
	{
		UnaryOperation(Span span, UnaryOperatorType type, Expression* expression) :
			Expression(span),
			expression(expression),
			type(type)
		{
		}

		virtual std::string ToString() const override;

		Expression* expression = nullptr;
		UnaryOperatorType type;
	};

	struct FunctionCall : Expression
	{
		FunctionCall(Span span, const std::string& target, const std::vector<Expression*>& arguments) :
			Expression(span),
			target(target),
			arguments(arguments)
		{
		}

		virtual std::string ToString() const override;

		std::string target;
		std::vector<Expression*> arguments;
	};

	struct AST
	{
		explicit AST(Lexer* tokenizer);

		void Generate();
		void Destroy();

		StatementBlock* rootBlock = nullptr;
		Lexer* lexer = nullptr;
		bool bValid = false;
	};
} // namespace flex
