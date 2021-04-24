#include "stdafx.hpp"

#include "VirtualMachine/Backend/FunctionBindings.hpp"

namespace flex
{
	using Value = VM::Value;

	Value DoMathSmall(const Value& val0, const Value& val1, const Value& val2)
	{
		i32 result = val0.AsInt() + val1.AsInt() * val2.AsInt();
		return Value(result);
	}

	Value Quintuple(const Value& val0)
	{
		i32 result = val0.AsInt() * 4;
		return Value(result);
	}

	Value PrintHelper(const Value& val0)
	{
		switch (val0.type)
		{
		case Value::Type::INT:
			Print("Int: %d\n", val0.AsInt());
			break;
		case Value::Type::FLOAT:
			Print("Float: %.3f\n", val0.AsFloat());
			break;
		case Value::Type::BOOL:
		{
			const char* boolStr = (val0.AsBool() ? "true" : "false");
			Print("Bool: %s\n", boolStr);
		} break;
		case Value::Type::CHAR:
			Print("Char: %c\n", val0.AsChar());
			break;
		default:
			PrintError("Unhandled arg type in Print\n");
			break;
		}

		return Value(0);
	}

	void FunctionBindings::RegisterBindings()
	{
		Bind("Quintuple", &Quintuple, Value::Type::INT, Value::Type::INT);
		Bind("DoMathSmall", &DoMathSmall, Value::Type::INT, Value::Type::INT, Value::Type::INT, Value::Type::INT);
		Bind("Print", &PrintHelper, Value::Type::VOID, Value::Type::VOID);
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

