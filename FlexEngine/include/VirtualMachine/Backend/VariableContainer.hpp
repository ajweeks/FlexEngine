#pragma once

namespace flex
{
	struct Declaration;
	struct FunctionDeclaration;
	enum class TypeName;

	struct VariableContainer
	{
		void DeclareVariable(Declaration* declaration);
		void DeclareFunction(FunctionDeclaration* funcDeclaration);
		bool GetTypeName(const std::string& identifierStr, TypeName& outTypeName);
		bool SetVariableType(const std::string& identifierStr, TypeName typeName);
		void PushFrame();
		void ClearVarsInFrame();
		void PopFrame();

		void ClearWriteFlag();
		void SetWriteFlag();
		bool GetWriteFlag() const;

		void SetFinalPass();
		bool IsFinalPass() const;

	private:
		std::vector<std::vector<Declaration*>> vars;
		std::vector<std::vector<FunctionDeclaration*>> funcs;

		bool bWriteFlag = false;
		bool bFinalPass = false;

	};
}