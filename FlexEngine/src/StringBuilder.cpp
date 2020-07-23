#include "stdafx.hpp"

#include "StringBuilder.hpp"

namespace flex
{
	StringBuilder::StringBuilder(u32 initialCapacity) :
		buffer(initialCapacity, '\0'),
		length(0)
	{
	}

	void StringBuilder::Append(const std::string& str)
	{
		Append(str.c_str());
	}

	void StringBuilder::Append(const char* str)
	{
		if ((length + (u32)strlen(str)) >= (u32)buffer.size())
		{
			Resize((length + (u32)strlen(str)) * 2);
		}

		for (u32 i = 0; i < strlen(str); ++i)
		{
			buffer[length++] = str[i];
		}
	}

	void StringBuilder::Append(char c)
	{
		if ((length + 1) >= (u32)buffer.size())
		{
			Resize((length + 1) * 2);
		}

		buffer[length++] = c;
	}

	void StringBuilder::Clear()
	{
		buffer.clear();
		length = 0;
	}

	std::string StringBuilder::ToString()
	{
		std::string result(buffer.data(), length);
		return result;
	}

	const char* StringBuilder::ToCString()
	{
		const char* result = buffer.data();
		return result;
	}

	u32 StringBuilder::Length() const
	{
		return length;
	}

	void StringBuilder::Resize(u32 newSize)
	{
		buffer.resize(newSize, '\0');
	}
} // namespace flex
