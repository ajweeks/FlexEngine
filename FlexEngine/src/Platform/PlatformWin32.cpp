#include "stdafx.hpp"

#include "Platform/Platform.hpp"

IGNORE_WARNINGS_PUSH
#include <tlhelp32.h> // For CreateToolhelp32Snapshot, Process32First
IGNORE_WARNINGS_POP

namespace flex
{
	bool Platform::IsProcessRunning(u32 PID)
	{
		HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, PID);
		if (h != INVALID_HANDLE_VALUE)
		{
			PROCESSENTRY32 entry = {};
			entry.dwSize = sizeof(PROCESSENTRY32);
			if (Process32First(h, &entry))
			{
				do
				{
					if (entry.th32ProcessID == PID)
					{
						return true;
					}
				} while (Process32Next(h, &entry));
			}
		}

		return false;
	}
} // namespace flex
