#pragma once

namespace flex
{
	namespace AST
	{
		struct Declaration;
		struct FunctionDeclaration;
		enum class TypeName;
	}

	struct VariableContainer
	{
		void DeclareVariable(AST::Declaration* declaration);
		void DeclareFunction(AST::FunctionDeclaration* funcDeclaration);
		bool GetTypeName(const std::string& identifierStr, AST::TypeName& outTypeName);
		bool SetVariableType(const std::string& identifierStr, AST::TypeName typeName);
		void PushFrame();
		void ClearVarsInFrame();
		void PopFrame();

		void ClearWriteFlag();
		void SetWriteFlag();
		bool GetWriteFlag() const;

		void SetFinalPass();
		bool IsFinalPass() const;

	private:
		std::vector<std::vector<AST::Declaration*>> vars;
		std::vector<std::vector<AST::FunctionDeclaration*>> funcs;

		bool bWriteFlag = false;
		bool bFinalPass = false;

	};
}