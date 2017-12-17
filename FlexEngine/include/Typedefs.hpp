#pragma once

#include <cstdint>

#include <glm/vec2.hpp>

namespace flex
{
	typedef int8_t i8;
	typedef int16_t i16;
	typedef int32_t i32;
	typedef int64_t i64;
	typedef uint8_t u8;
	typedef uint16_t u16;
	typedef uint32_t u32;
	typedef uint64_t u64;
	typedef float real;

	typedef real sec;	// Seconds
	typedef real ms;	// Milliseconds		1x10^-3 seconds
	typedef real us;	// Microseconds		1x10^-6 seconds
	typedef real ns;	// Nanoseconds		1x10^-9 seconds

	typedef u32 VertexAttributes;
	typedef u32 RenderID;
	typedef u32 ShaderID;
	typedef u32 MaterialID;
	typedef u32 PointLightID;
	typedef u32 DirectionalLightID;
} // namespace flex

namespace glm
{
	typedef tvec2<flex::i32> vec2i;
}
