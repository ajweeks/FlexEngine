#pragma once

#include "VirtualMachine/Backend/IRValue.hpp"
#include "VirtualMachine/Frontend/Span.hpp"
//#include "VirtualMachine/Frontend/Parser.hpp"

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
		enum class TypeName;
		enum class BinaryOperatorType;
		enum class UnaryOperatorType;
	}

	namespace VM
	{
		struct Value;
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

			bool Filled() const { return terminator != nullptr; }

			void AddAssignment(Assignment* assignment);
			void RemovePredecessor(Block* predecessor);
			void AddReturn(Value* returnVal);
			void AddYield(Value* yieldVal);
			void AddBranch(const Block& target);
			void AddCall(const std::string& target, const std::vector<Value*>& arguments);
			void SealBlock();
			void AddConditionalBranch(Value* condition, const Block& then, const Block& otherwise);

			std::string ToString() const;

			u32 index;
			std::list<Block*> predecessors;
			std::list<Assignment*> assignments;
			Terminator* terminator = nullptr;
			Span origin;
		};

		struct BlockList
		{
			std::vector<Block> blocks;
		};


		struct AssignmentUsage
		{
			//virtual bool UpdateAssignmentReferences(Assignment& from, Assignment& to) = 0;
		};

		struct Assignment : IR::Value
		{
			Assignment(const std::string& variable, IR::Value* value) :
				variable(variable),
				value(value)
			{}

			virtual std::string ToString() const override;


			//virtual bool UpdateAssignmentReferences(Assignment& from, Assignment& to) override;
			std::string variable;
			IR::Value* value;
		};

		struct Terminator : AssignmentUsage
		{
			//virtual bool UpdateAssignmentReferences(Assignment& from, Assignment& to) = 0;

			virtual std::string ToString() const = 0;
		};

		struct Return : Terminator
		{
			Return(IR::Value* returnValue) :
				returnValue(returnValue)
			{}

			virtual std::string ToString() const override;


			//virtual bool UpdateAssignmentReferences(Assignment& from, Assignment& to) override;

			IR::Value* returnValue;
		};

		struct YieldReturn : Terminator
		{
			YieldReturn(IR::Value* yieldValue) :
				yieldValue(yieldValue)
			{}

			virtual std::string ToString() const override;


			//virtual bool UpdateAssignmentReferences(Assignment& from, Assignment& to) override;

//			Block target;
			IR::Value* yieldValue;
		};

		struct Break : Terminator
		{
			Break(const Block& target) :
				target(target)
			{}

			virtual std::string ToString() const override;


			//virtual bool UpdateAssignmentReferences(Assignment& from, Assignment& to) override;

			Block target;
		};

		struct Branch : Terminator
		{
			Branch(const Block& target) :
				target(target)
			{}

			virtual std::string ToString() const override;


			//virtual bool UpdateAssignmentReferences(Assignment& from, Assignment& to) override;

			Block target;
		};

		struct ConditionalBranch : Terminator
		{
			//virtual bool UpdateAssignmentReferences(Assignment& from, Assignment& to) override;
			ConditionalBranch(IR::Value* condition, const Block& then, const Block& otherwise) :
				condition(condition),
				then(then),
				otherwise(otherwise)
			{
			}

			virtual std::string ToString() const override;


			IR::Value* condition;
			Block then;
			Block otherwise;
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

		struct UnaryValue : IR::Value
		{
			UnaryValue(UnaryOperatorType opType, IR::Value* operand) :
				Value(Value::Type::UNARY),
				opType(opType),
				operand(operand)
			{}

			virtual std::string ToString() const override;

			IR::Value* operand;
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

		BinaryOperatorType IRBinaryOperatorTypeFromASTBinaryOperatorType(AST::BinaryOperatorType opType);

		struct BinaryValue : IR::Value
		{
			BinaryValue(BinaryOperatorType opType, IR::Value* left, IR::Value* right) :
				Value(Value::Type::BINARY),
				opType(opType),
				left(left),
				right(right)
			{}

			virtual std::string ToString() const override;

			BinaryOperatorType opType;
			IR::Value* left;
			IR::Value* right;
		};

		struct FunctionCallValue : IR::Value
		{
			FunctionCallValue(const std::string& target, const std::vector<IR::Value*>& arguments) :
				Value(Value::Type::FUNC_CALL),
				target(target),
				arguments(arguments)
			{}

			virtual std::string ToString() const override;

			std::string target;
			std::vector<IR::Value*> arguments;
		};

		struct State
		{
			void Clear();
			Block& InsertionBlock();
			void SetCurrentInstructionBlock(Block& block);
			//BlockList& PushInstructionBlock();
			//void PopInstructionBlock();
			std::string NextTemporary();
			void WriteVariableInBlock(const std::string& variable, IR::Value* value);

			Block insertionBlock;
			//std::vector<BlockList> blockLists;

			u32 tempCount = 0;

			DiagnosticContainer* diagnosticContainer = nullptr;
		};

	} // namespace IR

	struct Generator
	{
		void GenerateFromAST(AST::AST* ast);
		void Destroy();

		IR::State state;

	private:
		//void DiscoverFuncDeclarations(const std::vector<Statement*>& statements);
		//void GenerateFunctionInstructions(const std::vector<Statement*>& statements);

		void LowerStatement(AST::Statement* statement);
		IR::Value* LowerExpression(AST::Expression* expression);
		//ValueWrapper GetValueWrapperFromExpression(AST::Expression* expression);

		i32 CombineInstructionIndex(i32 instructionBlockIndex, i32 instructionIndex);
		void SplitInstructionIndex(i32 combined, i32& outInstructionBlockIndex, i32& outInstructionIndex);
		i32 GenerateCallInstruction(AST::FunctionCall* funcCall);


	};
} // namespace flex
