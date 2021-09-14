#pragma once

#include <vector>

#include "Variant.hpp"

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

	class IFunction
	{
	public:
		virtual Variant Execute() = 0;
		virtual Variant Execute(const std::vector<Variant>& args) = 0;
		virtual Variant Execute(Variant* args, i32 argCount) = 0;

		virtual ~IFunction() = default;

		std::string name;
		std::vector<Variant::Type> argTypes;
		Variant::Type returnType;
	};

	template<typename RetVal, typename... Args, size_t... Is>
	RetVal CallWithVariableArgCountHelper(RetVal(*func)(Args...), Variant* args, IndexSequence<Is...>)
	{
		return func(args[Is]...);
	}

	template<typename RetVal, typename... Args>
	RetVal CallWithVariableArgCount(RetVal(*func)(Args...), Variant* args, i32 argCount)
	{
		if (sizeof...(Args) != argCount)
		{
			PrintError("Invalid number of arguments passed to CallWithVariableArgCount (%u, expected: %u)\n", (u32)argCount, (u32)sizeof...(Args));
			return RetVal(0);
		}
		return CallWithVariableArgCountHelper(func, args, BuildIndexSequence<sizeof...(Args)>{});
	}

	template<typename TLambda, size_t... Is>
	Variant CallLambdaWithVariableArgCountHelper(TLambda func, Variant* args, IndexSequence<Is...>)
	{
		return (func)(args[Is]...);
	}

	template<typename TLambda, typename... ArgTypes>
	Variant CallLambdaWithVariableArgCount(TLambda func, Variant* args, i32 argCount)
	{
		if (sizeof...(ArgTypes) != argCount)
		{
			PrintError("Invalid number of arguments passed to CallWithVariableArgCount (%u, expected: %u)\n", (u32)argCount, (u32)sizeof...(ArgTypes));
			return g_EmptyVariant;
		}
		return CallLambdaWithVariableArgCountHelper(func, args, BuildIndexSequence<sizeof...(ArgTypes)>{});
	}

	template<typename TLambda, size_t... Is>
	void CallVoidLambdaWithVariableArgCountHelper(TLambda func, Variant* args, IndexSequence<Is...>)
	{
		(func)(args[Is]...);
	}

	template<typename TLambda, typename... ArgTypes>
	void CallVoidLambdaWithVariableArgCount(TLambda func, Variant* args, i32 argCount)
	{
		if (sizeof...(ArgTypes) != argCount)
		{
			PrintError("Invalid number of arguments passed to CallVoidLambdaWithVariableArgCount (%u, expected: %u)\n", (u32)argCount, (u32)sizeof...(ArgTypes));
			return;
		}
		CallVoidLambdaWithVariableArgCountHelper(func, args, BuildIndexSequence<sizeof...(ArgTypes)>{});
	}

	// TODO: Replace RetVal with Variant
	// Function pointer to function that has return value & takes parameters
	template<typename RetVal, typename... Args>
	class FunctionRP : public IFunction
	{
	public:
		RetVal(*func)(Args...);

		FunctionRP(const std::string& name, RetVal(*f)(Args...), Variant::Type retType, Variant::Type* argTypesList, u32 argCount) :
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

		virtual Variant Execute(const std::vector<Variant>& args) override
		{
			return Execute((Variant*)args.data(), (i32)args.size());
		}

		virtual Variant Execute(Variant* args, i32 argCount) override
		{
			return CallWithVariableArgCount(func, args, argCount);
		}

		virtual Variant Execute() override
		{
			PrintError("Called FunctionRP::Execute with incorrect number of arguments, function will not be called\n");
			return g_EmptyVariant;
		}
	};

	// Function pointer to void function which takes no parameters
	class Function : public IFunction
	{
	public:
		void(*func)();

		Function(const std::string& name, void(*f)()) :
			func(f)
		{
			this->name = name;
			returnType = Variant::Type::VOID_;
		}

		virtual Variant Execute(const std::vector<Variant>& args) override
		{
			FLEX_UNUSED(args);
			PrintError("Called Function::Execute with incorrect number of arguments, function will not be called\n");
			return g_EmptyVariant;
		}

		virtual Variant Execute(Variant* args, i32 argCount) override
		{
			FLEX_UNUSED(args);
			FLEX_UNUSED(argCount);
			PrintError("Called Function::Execute with incorrect number of arguments, function will not be called\n");
			return g_EmptyVariant;
		}

		virtual Variant Execute() override
		{
			func();

			return g_EmptyVariant;
		}
	};

	// Function pointer to function with a Variant return type and takes no parameters
	class FunctionR : public IFunction
	{
	public:
		Variant(*func)();

		FunctionR(const std::string& name, Variant(*f)()) :
			func(f)
		{
			this->name = name;
			returnType = Variant::Type::VOID_;
		}

		virtual Variant Execute(const std::vector<Variant>& args) override
		{
			FLEX_UNUSED(args);
			PrintError("Called FunctionR::Execute with incorrect number of arguments, function will not be called\n");
			return g_EmptyVariant;
		}

		virtual Variant Execute(Variant* args, i32 argCount) override
		{
			FLEX_UNUSED(args);
			FLEX_UNUSED(argCount);
			PrintError("Called FunctionR::Execute with incorrect number of arguments, function will not be called\n");
			return g_EmptyVariant;
		}

		virtual Variant Execute() override
		{
			return func();
		}
	};

	// Function pointer to lambda which has a non-void return type & takes parameters
	template<typename TLambda, typename... ArgTypes>
	class FunctionLambdaRP : public IFunction
	{
	public:
		TLambda func;

		FunctionLambdaRP(const std::string& name, TLambda f, Variant::Type retType, Variant::Type* argTypesList, u32 argCount) :
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

		virtual Variant Execute(const std::vector<Variant>& args) override
		{
			return Execute((Variant*)args.data(), (i32)args.size());
		}

		virtual Variant Execute(Variant* args, i32 argCount) override
		{
			return CallLambdaWithVariableArgCount<TLambda, ArgTypes...>(func, args, argCount);
		}

		virtual Variant Execute() override
		{
			PrintError("Called FunctionLambdaRP::Execute with incorrect number of arguments, function will not be called\n");
			return g_EmptyVariant;
		}
	};

	// Function pointer to lambda which has a void return type & takes parameters
	template<typename TLambda, typename... ArgTypes>
	class FunctionLambdaP : public IFunction
	{
	public:
		TLambda func;

		FunctionLambdaP(const std::string& name, TLambda f, Variant::Type* argTypesList, u32 argCount) :
			func(f)
		{
			this->name = name;
			this->returnType = Variant::Type::VOID_;
			argTypes.reserve(argCount);
			for (u32 i = 0; i < (u32)argCount; ++i)
			{
				argTypes.emplace_back(argTypesList[i]);
			}
		}

		virtual Variant Execute(const std::vector<Variant>& args) override
		{
			Execute((Variant*)args.data(), (i32)args.size());
			return g_EmptyVariant;
		}

		virtual Variant Execute(Variant* args, i32 argCount) override
		{
			CallVoidLambdaWithVariableArgCount<TLambda, ArgTypes...>(func, args, argCount);
			return g_EmptyVariant;
		}

		virtual Variant Execute() override
		{
			PrintError("Called FunctionLambdaP::Execute with incorrect number of arguments, function will not be called\n");
			return g_EmptyVariant;
		}
	};

	class FunctionBindings
	{
	public:
		void RegisterBindings();
		void ClearBindings();

		std::map<FuncAddress, IFunction*> ExternalFuncTable;

		static IFunction* Bind(const std::string& name, Variant(*func)())
		{
			IFunction* funcPtr = new FunctionR(name, func);
			return funcPtr;
		}

		static IFunction* BindV(const std::string& name, void(*func)())
		{
			IFunction* funcPtr = new Function(name, func);
			return funcPtr;
		}

		template<typename RetVal, typename... Args, typename... ArgTypes>
		static IFunction* Bind(const std::string& name, RetVal(*func)(Args...), Variant::Type returnType, ArgTypes... argTypes)
		{
			std::vector<Variant::Type> argTypesList = { argTypes... };
			IFunction* funcPtr = new FunctionRP<RetVal, Args...>(name, func, returnType, (Variant::Type*)argTypesList.data(), (u32)argTypesList.size());
			return funcPtr;
		}

		template<typename TLambda, typename... ArgTypes>
		static IFunction* Bind(const char* name, TLambda func, Variant::Type returnType, ArgTypes... args)
		{
			std::vector<Variant::Type> argTypesList = { ((Variant::Type)args)... };
			IFunction* funcPtr = new FunctionLambdaRP<TLambda, ArgTypes...>(name, std::forward<TLambda>(func), returnType, (Variant::Type*)argTypesList.data(), (u32)argTypesList.size());
			return funcPtr;
		}

		template<typename TLambda, typename... ArgTypes>
		static IFunction* BindP(const char* name, TLambda func, ArgTypes... args)
		{
			std::vector<Variant::Type> argTypesList = { ((Variant::Type)args)... };
			IFunction* funcPtr = new FunctionLambdaP<TLambda, ArgTypes...>(name, std::forward<TLambda>(func), (Variant::Type*)argTypesList.data(), (u32)argTypesList.size());
			return funcPtr;
		}

	private:
		void Register(IFunction* func);

	};
} // namespace flex
