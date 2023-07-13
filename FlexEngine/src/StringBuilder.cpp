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

	void StringBuilder::Append(u32 val)
	{
		const u32 BUF_SIZE = 200;
		char charBuff[BUF_SIZE];

		// TODO: Use snsprintf_s on windows
		sprintf(charBuff, "%u", val);
		if ((length + (u32)strlen(charBuff)) >= (u32)buffer.size())
		{
			Resize((length + (u32)strlen(charBuff)) * 2);
		}

		for (char c : charBuff)
		{
			if (c == '\0')
			{
				break;
			}
			buffer[length++] = c;
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

	void StringBuilder::AppendLine(const std::string& str)
	{
		AppendLine(str.c_str());
	}

	void StringBuilder::AppendLine(const char* str)
	{
		if ((length + (u32)strlen(str) + 1) >= (u32)buffer.size())
		{
			Resize((length + (u32)strlen(str) + 1) * 2);
		}

		for (u32 i = 0; i < strlen(str); ++i)
		{
			buffer[length++] = str[i];
		}
		buffer[length++] = '\n';
	}

	void StringBuilder::AppendLine(real val)
	{
		const u32 BUF_SIZE = 200;
		char charBuff[BUF_SIZE];

		// TODO: Use snsprintf_s on windows
		sprintf(charBuff, "%.2f", val);
		if ((length + (u32)strlen(charBuff)) >= (u32)buffer.size())
		{
			Resize((length + (u32)strlen(charBuff)) * 2);
		}

		for (char c : charBuff)
		{
			if (c == '\0')
			{
				break;
			}
			buffer[length++] = c;
		}
		buffer[length++] = '\n';
	}

	void StringBuilder::AppendLine(i32 val)
	{
		const u32 BUF_SIZE = 200;
		char charBuff[BUF_SIZE];

		// TODO: Use snsprintf_s on windows
		sprintf(charBuff, "%i", val);
		if ((length + (u32)strlen(charBuff)) >= (u32)buffer.size())
		{
			Resize((length + (u32)strlen(charBuff)) * 2);
		}

		for (char c : charBuff)
		{
			if (c == '\0')
			{
				break;
			}
			buffer[length++] = c;
		}
		buffer[length++] = '\n';
	}

	void StringBuilder::AppendLine(u32 val)
	{
		const u32 BUF_SIZE = 200;
		char charBuff[BUF_SIZE];

		// TODO: Use snsprintf_s on windows
		sprintf(charBuff, "%u", val);
		if ((length + (u32)strlen(charBuff)) >= (u32)buffer.size())
		{
			Resize((length + (u32)strlen(charBuff)) * 2);
		}

		for (char c : charBuff)
		{
			if (c == '\0')
			{
				break;
			}
			buffer[length++] = c;
		}
		buffer[length++] = '\n';
	}

	void StringBuilder::AppendLine(char c)
	{
		if ((length + 2) >= (u32)buffer.size())
		{
			Resize((length + 2) * 2);
		}

		buffer[length++] = c;
		buffer[length++] = '\n';
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
