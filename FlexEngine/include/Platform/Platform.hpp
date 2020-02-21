#pragma once

namespace flex
{
	class Platform
	{
	public:
		static bool IsProcessRunning(u32 PID);
		static u32 GetCurrentProcessID();
		static void MoveConsole(i32 width = 800, i32 height = 800);
	};
} // namespace flex
