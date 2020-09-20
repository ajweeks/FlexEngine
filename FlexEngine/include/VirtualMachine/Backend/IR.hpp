#pragma once

#include "VirtualMachine/Backend/IRValue.hpp"
#include "VirtualMachine/Frontend/Span.hpp"

#include <list>

namespace flex
{
	struct DiagnosticContainer;

	namespace AST
	{
		struct AST;
		struct Statement;
		struct Expression;
		struct Declaration;
		struct FunctionCall;
		enum class BinaryOperatorType;
		enum class UnaryOperatorType;
	}

	namespace VM
	{
		struct Value;
		enum class OpCode;
	}

	//
	// Low level, intermediate representation. Close to bytecode, but doesn't reference registers/the stack.
	//
	namespace IR
	{
		struct Assignment;
		struct Terminator;
		struct Return;

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
			EQUAL_TEST,
			NOT_EQUAL_TEST,
			GREATER_TEST,
			GREATER_EQUAL_TEST,
			LESS_TEST,
			LESS_EQUAL_TEST,
			BOOLEAN_AND,
			BOOLEAN_OR,
			NEGATE,
			NOT,
			BIN_INVERT,
			CAST,

			_NONE
		};

		struct Block
		{
			Block() :
				origin(0, 0)
			{}

			Block(Span origin) :
				origin(origin)
			{}

			~Block()
			{}

			void Destroy();

			bool Filled() const { return terminator != nullptr; }

			void AddAssignment(Assignment* assignment);
			void RemovePredecessor(Block* predecessor);
			void AddReturn(Span returnOrigin, Value* returnVal);
			void AddYield(Span yieldOrigin, Value* yieldVal);
			void AddBranch(Span branchOrigin, Block* target);
			void AddCall(const std::string& target, const std::vector<Value*>& arguments);
			void AddHalt();
			void SealBlock();
			void AddConditionalBranch(Span branchOrigin, Value* condition, Block* then, Block* otherwise);

			std::string ToString() const;

			u32 index = 0;
			std::list<Block*> predecessors;
			std::list<Assignment*> assignments;
			Terminator* terminator = nullptr;
			Span origin;
		};

		struct Assignment : IR::Value
		{
			Assignment(State* state, Span origin, const std::string& variable, IR::Value* value) :
				Value(origin, state),
				variable(variable),
				value(value)
			{}

			virtual void Destroy() override;
			virtual std::string ToString() const override;

			std::string variable;
			IR::Value* value = nullptr;
		};

		struct Identifier : IR::Value
		{
			Identifier(State* state, Span origin, const std::string& variable) :
				Value(origin, state, Value::Type::IDENTIFIER),
				variable(variable)
			{}

			virtual std::string ToString() const override;

			std::string variable;
		};

		enum class TerminatorType
		{
			RETURN,
			YIELD,
			BREAK,
			HALT,
			BRANCH,
			CONDITIONAL_BRANCH,

			_NONE
		};

		struct Terminator
		{
			Terminator(Span origin, TerminatorType type) :
				origin(origin),
				type(type)
			{}

			virtual ~Terminator()
			{}

			virtual void Destroy() = 0;

			virtual std::string ToString() const = 0;

			Span origin;
			TerminatorType type;
		};

		struct Halt : Terminator
		{
			Halt() :
				Terminator(Span(0, 0), TerminatorType::HALT)
			{
				origin.source = Span::Source::GENERATED;
			}

			virtual void Destroy() override
			{}

			virtual std::string ToString() const override;
		};

		struct Return : Terminator
		{
			Return(IR::Value* returnValue, Span origin) :
				Terminator(origin, TerminatorType::RETURN),
				returnValue(returnValue)
			{}

			virtual void Destroy() override;

			virtual std::string ToString() const override;

			IR::Value* returnValue = nullptr;
		};

		struct YieldReturn : Terminator
		{
			YieldReturn(IR::Value* yieldValue, Span origin) :
				Terminator(origin, TerminatorType::YIELD),
				yieldValue(yieldValue)
			{}

			virtual void Destroy() override;

			virtual std::string ToString() const override;

			//Block * target = nullptr;
			IR::Value* yieldValue = nullptr;
		};

		struct Break : Terminator
		{
			Break(Block* target, Span origin) :
				Terminator(origin, TerminatorType::BREAK),
				target(target)
			{}

			virtual void Destroy() override;

			virtual std::string ToString() const override;

			Block* target = nullptr;
		};

		struct Branch : Terminator
		{
			Branch(Block* target, Span origin) :
				Terminator(origin, TerminatorType::BRANCH),
				target(target)
			{}

			virtual void Destroy() override;

			virtual std::string ToString() const override;

			Block* target = nullptr;
		};

		struct ConditionalBranch : Terminator
		{
			ConditionalBranch(IR::Value* condition, Block* then, Block* otherwise, Span origin) :
				Terminator(origin, TerminatorType::CONDITIONAL_BRANCH),
				condition(condition),
				then(then),
				otherwise(otherwise)
			{
			}

			virtual void Destroy() override;

			virtual std::string ToString() const override;

			IR::Value* condition = nullptr;
			Block* then = nullptr;
			Block* otherwise = nullptr;
		};

		struct Constant : IR::Value
		{
			Constant(const IR::Value& value) :
				Value(value)
			{}
		};

		enum class UnaryOperatorType
		{
			NEGATE,
			NOT,
			BIN_INVERT,

			_NONE
		};

		static const char* g_UnaryOperatorTypeStrings[] =
		{
			"-",
			"!",
			"~",

			"NONE"
		};

		static_assert(ARRAY_LENGTH(g_UnaryOperatorTypeStrings) == ((size_t)UnaryOperatorType::_NONE + 1), "Length of g_UnaryOperatorTypeStrings must match number of entries in UnaryOperatorType enum");

		const char* UnaryOperatorTypeToString(UnaryOperatorType opType);

		UnaryOperatorType IRUnaryOperatorTypeFromASTUnaryOperatorType(AST::UnaryOperatorType opType);
		VM::OpCode OpCodeFromUnaryOperatorType(UnaryOperatorType opType);

		struct UnaryValue : IR::Value
		{
			UnaryValue(State* state, Span origin, UnaryOperatorType opType, IR::Value* operand) :
				Value(origin, state, Value::Type::UNARY),
				opType(opType),
				operand(operand)
			{}

			virtual void Destroy() override;
			virtual std::string ToString() const override;

			IR::Value* operand = nullptr;
			UnaryOperatorType opType;
		};

		enum class BinaryOperatorType
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
			"-",
			"*",
			"/",
			"%",
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

		bool IsComparisonOp(BinaryOperatorType opType);

		BinaryOperatorType IRBinaryOperatorTypeFromASTBinaryOperatorType(AST::BinaryOperatorType opType);
		VM::OpCode OpCodeFromBinaryOperatorType(BinaryOperatorType opType);

		struct BinaryValue : IR::Value
		{
			BinaryValue(State* state, Span origin, BinaryOperatorType opType, IR::Value* left, IR::Value* right) :
				Value(origin, state, Value::Type::BINARY),
				opType(opType),
				left(left),
				right(right)
			{}

			virtual void Destroy() override;
			virtual std::string ToString() const override;

			BinaryOperatorType opType;
			IR::Value* left = nullptr;
			IR::Value* right = nullptr;
		};

		struct FunctionCallValue : IR::Value
		{
			FunctionCallValue(State* state, Span origin, const std::string& target, const std::vector<IR::Value*>& arguments) :
				Value(origin, state, Value::Type::FUNC_CALL),
				target(target),
				arguments(arguments)
			{}

			virtual void Destroy() override;
			virtual std::string ToString() const override;

			std::string target;
			std::vector<IR::Value*> arguments;
		};

		struct CastValue : IR::Value
		{
			CastValue(State* state, Span origin, Value::Type castedType, Value* target) :
				Value(origin, state, Value::Type::CAST),
				castedType(castedType),
				target(target)
			{}

			virtual void Destroy() override;
			virtual std::string ToString() const override;

			Value* target = nullptr;
			Value::Type castedType;
		};

		struct State
		{
			State();
			void Destroy();
			void Clear();
			void PushInstructionBlock(Block* block);
			std::string NextTemporary();
			void WriteVariableInBlock(const std::string& variable, IR::Value* value);
			Value::Type GetValueType(Value const * value);

			Block* InsertionBlock();
			std::vector<Block*> blocks;

			u32 tempCount = 0;
			std::map<std::string, Value::Type> variableTypes;
			std::map<std::string, Value::Type> functionTypes;

			DiagnosticContainer* diagnosticContainer = nullptr;
		};

		struct IntermediateRepresentation
		{
			void GenerateFromAST(AST::AST* ast);
			void Destroy();

			std::string ToString() const;

			State* state = nullptr;
			std::vector<Block*> blocks;

		private:
			//void DiscoverFuncDeclarations(const std::vector<Statement*>& statements);
			//void GenerateFunctionInstructions(const std::vector<Statement*>& statements);

			void DiscoverFunctionDefinitions(AST::Statement* statement);
			void LowerStatement(AST::Statement* statement);
			IR::Value* LowerExpression(AST::Expression* expression);
			void LowerFunctionDefinitions(AST::Statement* statement);
			//ValueWrapper GetValueWrapperFromExpression(AST::Expression* expression);

			void AddFunctionType(Span origin, const std::string& funcName, Value::Type returnType);
			void CheckReturnTypesMatch(Value::Type returnType, Span origin, Block* block);

			void SetBlockIndices();

			i32 CombineInstructionIndex(i32 instructionBlockIndex, i32 instructionIndex);
			void SplitInstructionIndex(i32 combined, i32& outInstructionBlockIndex, i32& outInstructionIndex);
			i32 GenerateCallInstruction(AST::FunctionCall* funcCall);
		};

	} // namespace IR
} // namespace flex
