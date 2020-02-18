#include "stdafx.hpp"

#ifdef _WIN32

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

	u32 Platform::GetCurrentProcessID()
	{
		return (u32)GetCurrentProcessId();
	}

	void Platform::MoveConsole(i32 width /* = 800 */, i32 height /* = 800 */)
	{
		HWND hWnd = GetConsoleWindow();

		// The following four variables store the bounding rectangle of all monitors
		i32 virtualScreenLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
		//i32 virtualScreenTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
		i32 virtualScreenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		//i32 virtualScreenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

		i32 monitorWidth = GetSystemMetrics(SM_CXSCREEN);
		//i32 monitorHeight = GetSystemMetrics(SM_CYSCREEN);

		// If another monitor is present, move the console to it
		if (virtualScreenWidth > monitorWidth)
		{
			i32 newX;
			i32 newY = 10;

			if (virtualScreenLeft < 0)
			{
				// The other monitor is to the left of the main one
				newX = -(width + 67);
			}
			else
			{
				// The other monitor is to the right of the main one
				newX = virtualScreenWidth - monitorWidth + 10;
			}

			MoveWindow(hWnd, newX, newY, width, height, TRUE);

			// Call again to set size correctly (based on other monitor's DPI)
			MoveWindow(hWnd, newX, newY, width, height, TRUE);
		}
		else // There's only one monitor, move the console to the top left corner
		{
			RECT rect;
			GetWindowRect(hWnd, &rect);
			if (rect.top != 0)
			{
				// A negative value is needed to line the console up to the left side of my monitor
				MoveWindow(hWnd, -7, 0, width, height, TRUE);
			}
		}
	}
} // namespace flex
#endif // _WIN32
