#pragma once

#include "JSONTypes.hpp"

namespace flex
{
	// TODO: Test
	class StringBuilder
	{
	public:
		StringBuilder(u32 initialCapacity = 0);

		void Append(const std::string& str);
		void Append(const char* str);
		void Append(char c);

		void AppendLine(const std::string& str);
		void AppendLine(const char* str);
		void AppendLine(real val);
		void AppendLine(i32 val);
		void AppendLine(u32 val);
		void AppendLine(char c);

		void Clear();

		std::string ToString();
		// NOTE: Pointer should not be held onto! Unsafe once anything else is appended
		const char* ToCString();
		u32 Length() const;

	private:
		void Resize(u32 newSize);

		std::vector<char> buffer;
		u32 length;

	};
} // namespace flex
