#include "stdafx.hpp"

#include "VirtualMachine/Backend/VariableContainer.hpp"

#include "VirtualMachine/Frontend/Parser.hpp"

namespace flex
{
	void VariableContainer::DeclareVariable(AST::Declaration* declaration)
	{
		vars[vars.size() - 1].push_back(declaration);
	}

	void VariableContainer::DeclareFunction(AST::FunctionDeclaration* funcDeclaration)
	{
		funcs[funcs.size() - 1].push_back(funcDeclaration);
	}

	bool VariableContainer::GetTypeName(const std::string& identifierStr, AST::TypeName& outTypeName)
	{
		assert(vars.size() == funcs.size());

		for (u32 i = 0; i < (u32)vars.size(); ++i)
		{
			std::vector<AST::Declaration*>& vec = vars[i];
			for (u32 j = 0; j < (u32)vec.size(); ++j)
			{
				if (vec[j]->identifierStr.compare(identifierStr) == 0)
				{
					outTypeName = vec[j]->typeName;
					return true;
				}
			}

			std::vector<AST::FunctionDeclaration*>& funcDecls = funcs[i];
			for (u32 j = 0; j < (u32)funcDecls.size(); ++j)
			{
				if (funcDecls[j]->name.compare(identifierStr) == 0)
				{
					outTypeName = funcDecls[j]->returnType;
					return true;
				}
			}
		}

		return false;
	}

	bool VariableContainer::SetVariableType(const std::string& identifierStr, AST::TypeName typeName)
	{
		for (u32 i = 0; i < (u32)vars.size(); ++i)
		{
			std::vector<AST::Declaration*>& vec = vars[i];
			for (u32 j = 0; j < (u32)vec.size(); ++j)
			{
				if (vec[j]->identifierStr.compare(identifierStr) == 0)
				{
					if (vec[j]->typeName != typeName)
					{
						bWriteFlag = true;
						vec[j]->typeName = typeName;
					}
					return true;
				}
			}
		}

		return false;
	}

	void VariableContainer::PushFrame()
	{
		vars.push_back({});
		funcs.push_back({});
	}

	void VariableContainer::ClearVarsInFrame()
	{
		assert(!vars.empty());
		vars[vars.size() - 1].clear();
	}

	void VariableContainer::PopFrame()
	{
		vars.pop_back();
		funcs.pop_back();
	}

	void VariableContainer::ClearWriteFlag()
	{
		bWriteFlag = false;
	}

	void VariableContainer::SetWriteFlag()
	{
		bWriteFlag = true;
	}

	bool VariableContainer::GetWriteFlag() const
	{
		return bWriteFlag;
	}

	void VariableContainer::SetFinalPass()
	{
		bFinalPass = true;
	}

	bool VariableContainer::IsFinalPass() const
	{
		return bFinalPass;
	}
} // namespace flex
