#pragma once

#include <vector>

#include "VirtualMachine/Backend/VMValue.hpp"

namespace flex
{
	using FuncAddress = i32;
	static const FuncAddress InvalidFuncAddress = -1;

	// Home-made index sequence trick, so it can be used everywhere without the costly include of std::tuple.
	// https://stackoverflow.com/questions/15014096/c-index-of-type-during-variadic-template-expansion
	template <size_t... Is>
	struct IndexSequence {};

	template <size_t N, size_t... Is>
	struct BuildIndexSequence : BuildIndexSequence<N - 1, N - 1, Is...> {};

	template <size_t... Is>
	struct BuildIndexSequence<0, Is...> : IndexSequence<Is...> {};

	struct IFunction
	{
		virtual VM::Value Execute(const std::vector<VM::Value>& args) = 0;
		virtual VM::Value Execute(VM::Value* args, i32 argCount) = 0;
		virtual ~IFunction() = default;

		std::string name;
		std::vector<VM::Value::Type> argTypes;
		VM::Value::Type returnType;
	};

	template<typename RetVal, typename... Args>
	struct FunctionRP : public IFunction
	{
		RetVal(*func)(Args...);

		template<typename RetVal, typename... Args>
		FunctionRP(const std::string& name, RetVal(*f)(Args...), VM::Value::Type retType, VM::Value::Type* argTypesList, u32 argCount) :
			func(f)
		{
			this->name = name;
			this->returnType = retType;
			argTypes.reserve(argCount);
			for (u32 i = 0; i < (u32)argCount; ++i)
			{
				argTypes.emplace_back(argTypesList[i]);
			}
		}

		virtual VM::Value Execute(const std::vector<VM::Value>& args) override
		{
			return Execute((VM::Value*)args.data(), (i32)args.size());
		}

		virtual VM::Value Execute(VM::Value* args, i32 argCount) override
		{
			return CallWithVariableArgCount(func, args, argCount);
		}
	};

	template<typename RetVal, typename... Args, size_t... Is>
	RetVal CallWithVariableArgCountHelper(RetVal(*func)(Args...), VM::Value* args, IndexSequence<Is...>)
	{
		return func(args[Is]...);
	}

	template<typename RetVal, typename... Args>
	RetVal CallWithVariableArgCount(RetVal(*func)(Args...), VM::Value* args, i32 argCount)
	{
		if (sizeof...(Args) != argCount)
		{
			PrintError("Invalid number of arguments passed to FunctionRP (%u, expected: %u)\n", (u32)argCount, (u32)sizeof...(Args));
			return RetVal(0);
		}
		return CallWithVariableArgCountHelper(func, args, BuildIndexSequence<sizeof...(Args)>{});
	}

	class FunctionBindings
	{
	public:
		void RegisterBindings();
		void ClearBindings();

		std::map<FuncAddress, IFunction*> ExternalFuncTable;

	private:
		// Arg types of VOID can have any Value type
		template<typename Func, typename... ArgTypes>
		IFunction* Bind(const std::string& name, Func func, VM::Value::Type returnType, ArgTypes... argTypes)
		{
			IFunction* funcPtr = CreateFunctionBind(name, func, returnType, argTypes...);
			FuncAddress funcAddress = 0xFFFF + (u32)ExternalFuncTable.size();
			ExternalFuncTable[funcAddress] = funcPtr;
			return funcPtr;
		}

		template<typename RetVal, typename... Args, typename... ArgTypes>
		static IFunction* CreateFunctionBind(const std::string& name, RetVal(*func)(Args...), VM::Value::Type returnType, ArgTypes... argTypes)
		{
			std::vector<VM::Value::Type> argTypesList = { argTypes... };
			IFunction* funcPtr = new FunctionRP<RetVal, Args...>(name, func, returnType, (VM::Value::Type*)argTypesList.data(), (u32)argTypesList.size());
			return funcPtr;
		}
	};
} // namespace flex
