#pragma once

namespace flex
{
	struct Monitor
	{
		i32 width = 0;
		i32 height = 0;
		i32 redBits = 0;
		i32 greenBits = 0;
		i32 blueBits = 0;
		i32 refreshRate = 0;
		glm::vec2 DPI;
		real contentScaleX = 1.0f;
		real contentScaleY = 1.0f;
	};
} // namespace flex
