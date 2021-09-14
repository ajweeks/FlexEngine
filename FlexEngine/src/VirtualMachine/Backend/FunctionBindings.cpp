#include "stdafx.hpp"

#include "VirtualMachine/Backend/FunctionBindings.hpp"

namespace flex
{
	Variant DoMathSmall(const Variant& val0, const Variant& val1, const Variant& val2)
	{
		i32 result = val0.AsInt() + val1.AsInt() * val2.AsInt();
		return Variant(result);
	}

	Variant Quintuple(const Variant& val0)
	{
		i32 result = val0.AsInt() * 4;
		return Variant(result);
	}

	Variant PrintHelper(const Variant& val0)
	{
		switch (val0.type)
		{
		case Variant::Type::INT:
			Print("Int: %d\n", val0.AsInt());
			break;
		case Variant::Type::UINT:
			Print("UInt: %d\n", val0.AsUInt());
			break;
		case Variant::Type::LONG:
			Print("Long: %ld\n", val0.AsLong());
			break;
		case Variant::Type::ULONG:
			Print("ULong: %lu\n", val0.AsULong());
			break;
		case Variant::Type::FLOAT:
			Print("Float: %.3f\n", val0.AsFloat());
			break;
		case Variant::Type::BOOL:
		{
			const char* boolStr = (val0.AsBool() ? "true" : "false");
			Print("Bool: %s\n", boolStr);
		} break;
		case Variant::Type::CHAR:
			Print("Char: %c\n", val0.AsChar());
			break;
		case Variant::Type::STRING:
			Print("String: %s\n", val0.AsString());
			break;
		default:
			PrintError("Unhandled arg type in Print\n");
			break;
		}

		return Variant(0);
	}

	void FunctionBindings::RegisterBindings()
	{
		Register(Bind("Quintuple", &Quintuple, Variant::Type::INT, Variant::Type::INT));
		Register(Bind("DoMathSmall", &DoMathSmall, Variant::Type::INT, Variant::Type::INT, Variant::Type::INT, Variant::Type::INT));
		Register(Bind("Print", &PrintHelper, Variant::Type::VOID_, Variant::Type::VOID_));
	}

	void FunctionBindings::ClearBindings()
	{
		for (auto& funcPtrPair : ExternalFuncTable)
		{
			delete funcPtrPair.second;
		}
		ExternalFuncTable.clear();
	}

	void FunctionBindings::Register(IFunction* func)
	{
		if (func != nullptr)
		{
			FuncAddress funcAddress = 0xFFFF + (u32)ExternalFuncTable.size();
			ExternalFuncTable[funcAddress] = func;
		}
	}
} // namespace flex

