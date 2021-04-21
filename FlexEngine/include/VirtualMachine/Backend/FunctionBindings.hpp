#pragma once

#include "VirtualMachine/Backend/VMValue.hpp"

namespace flex
{
	using FuncAddress = i32;
	static const FuncAddress InvalidFuncAddress = -1;

	struct FuncPtr
	{
		typedef VM::Value(*PtrType)(const VM::Value& val0, const VM::Value& val1, const VM::Value& val2, const VM::Value& val3, const VM::Value& val4);

		FuncPtr(PtrType ptr,
			FuncAddress address,
			const std::string& name,
			VM::Value::Type arg0Type,
			VM::Value::Type arg1Type,
			VM::Value::Type arg2Type,
			VM::Value::Type arg3Type,
			VM::Value::Type arg4Type,
			VM::Value::Type returnType) :
			ptr(ptr),
			address(address),
			name(name),
			returnType(returnType)
		{
			argTypes.reserve(5);
			argTypes.emplace_back(arg0Type);
			argTypes.emplace_back(arg1Type);
			argTypes.emplace_back(arg2Type);
			argTypes.emplace_back(arg3Type);
			argTypes.emplace_back(arg4Type);
		}

		FuncPtr(PtrType ptr,
			FuncAddress address,
			const std::string& name,
			VM::Value::Type arg0Type,
			VM::Value::Type arg1Type,
			VM::Value::Type arg2Type,
			VM::Value::Type arg3Type,
			VM::Value::Type returnType) :
			ptr(ptr),
			address(address),
			name(name),
			returnType(returnType)
		{
			argTypes.reserve(4);
			argTypes.emplace_back(arg0Type);
			argTypes.emplace_back(arg1Type);
			argTypes.emplace_back(arg2Type);
			argTypes.emplace_back(arg3Type);
		}


		FuncPtr(PtrType ptr,
			FuncAddress address,
			const std::string& name,
			VM::Value::Type arg0Type,
			VM::Value::Type arg1Type,
			VM::Value::Type arg2Type,
			VM::Value::Type returnType) :
			ptr(ptr),
			address(address),
			name(name),
			returnType(returnType)
		{
			argTypes.reserve(3);
			argTypes.emplace_back(arg0Type);
			argTypes.emplace_back(arg1Type);
			argTypes.emplace_back(arg2Type);
		}


		FuncPtr(PtrType ptr,
			FuncAddress address,
			const std::string& name,
			VM::Value::Type arg0Type,
			VM::Value::Type arg1Type,
			VM::Value::Type returnType) :
			ptr(ptr),
			address(address),
			name(name),
			returnType(returnType)
		{
			argTypes.reserve(2);
			argTypes.emplace_back(arg0Type);
			argTypes.emplace_back(arg1Type);
		}

		FuncPtr(PtrType ptr,
			FuncAddress address,
			const std::string& name,
			VM::Value::Type arg0Type,
			VM::Value::Type returnType) :
			ptr(ptr),
			address(address),
			name(name),
			returnType(returnType)
		{
			argTypes.reserve(1);
			argTypes.emplace_back(arg0Type);
		}

		FuncPtr(PtrType ptr,
			FuncAddress address,
			const std::string& name,
			VM::Value::Type returnType) :
			ptr(ptr),
			address(address),
			name(name),
			returnType(returnType)
		{
		}

		VM::Value operator()(const VM::Value& val0, const VM::Value& val1, const VM::Value& val2, const VM::Value& val3, const VM::Value& val4)
		{
			return ptr(val0, val1, val2, val3, val4);
		}

		VM::Value operator()(const VM::Value& val0, const VM::Value& val1, const VM::Value& val2, const VM::Value& val3)
		{
			return ptr(val0, val1, val2, val3, VM::g_EmptyVMValue);
		}

		VM::Value operator()(const VM::Value& val0, const VM::Value& val1, const VM::Value& val2)
		{
			return ptr(val0, val1, val2, VM::g_EmptyVMValue, VM::g_EmptyVMValue);
		}

		VM::Value operator()(const VM::Value& val0, const VM::Value& val1)
		{
			return ptr(val0, val1, VM::g_EmptyVMValue, VM::g_EmptyVMValue, VM::g_EmptyVMValue);
		}

		VM::Value operator()(const VM::Value& val0)
		{
			return ptr(val0, VM::g_EmptyVMValue, VM::g_EmptyVMValue, VM::g_EmptyVMValue, VM::g_EmptyVMValue);
		}

		VM::Value operator()()
		{
			return ptr(VM::g_EmptyVMValue, VM::g_EmptyVMValue, VM::g_EmptyVMValue, VM::g_EmptyVMValue, VM::g_EmptyVMValue);
		}

		PtrType ptr;
		FuncAddress address;
		std::string name;
		std::vector<VM::Value::Type> argTypes;
		VM::Value::Type returnType;
	};

	class FunctionBindings
	{
	public:
		void RegisterBindings();

		void ClearBindings();

		std::map<FuncAddress, FuncPtr*> ExternalFuncTable;
	};
} // namespace flex
