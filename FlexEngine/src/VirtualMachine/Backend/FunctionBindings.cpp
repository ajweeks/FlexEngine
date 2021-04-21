#include "stdafx.hpp"

#include "VirtualMachine/Backend/FunctionBindings.hpp"

namespace flex
{
	using Value = VM::Value;

	Value DoMath(const Value& val0, const Value& val1, const Value& val2, const Value& val3, const Value& val4)
	{
		FLEX_UNUSED(val3);
		FLEX_UNUSED(val4);

		i32 result = val0.AsInt() + val1.AsInt() * val2.AsInt();
		return Value(result);
	}

	void FunctionBindings::RegisterBindings()
	{
		FuncAddress doMath = 0xFFFF + (u32)ExternalFuncTable.size();
		ExternalFuncTable[doMath] = new FuncPtr(&DoMath, doMath, "DoMath", Value::Type::INT, Value::Type::INT, Value::Type::INT, Value::Type::INT);
	}

	void FunctionBindings::ClearBindings()
	{
		for (auto& funcPtrPair : ExternalFuncTable)
		{
			delete funcPtrPair.second;
		}
		ExternalFuncTable.clear();
	}
} // namespace flex

