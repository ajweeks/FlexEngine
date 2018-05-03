#pragma once

#include <cstdint>
#include <limits>

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

#define u8_min std::numeric_limits<u8>::min()
#define u8_max std::numeric_limits<u8>::max()
#define u16_min std::numeric_limits<u16>::min()
#define u16_max std::numeric_limits<u16>::max()
#define u32_min std::numeric_limits<u32>::min()
#define u32_max std::numeric_limits<u32>::max()
#define u64_min std::numeric_limits<u64>::min()
#define u64_max std::numeric_limits<u64>::max()

#define i8_min std::numeric_limits<i8>::min()
#define i8_max std::numeric_limits<i8>::max()
#define i16_min std::numeric_limits<i16>::min()
#define i16_max std::numeric_limits<i16>::max()
#define i32_min std::numeric_limits<i32>::min()
#define i32_max std::numeric_limits<i32>::max()
#define i64_min std::numeric_limits<i64>::min()
#define i64_max std::numeric_limits<i64>::max()

#define real_min std::numeric_limits<real>::min()
#define real_max std::numeric_limits<real>::max()

	typedef real sec;	// Seconds
	typedef real ms;	// Milliseconds		1x10^-3 seconds
	typedef real us;	// Microseconds		1x10^-6 seconds
	typedef real ns;	// Nanoseconds		1x10^-9 seconds

	typedef u32 VertexAttributes;
	typedef u32 RenderID;
	typedef u32 ShaderID;
	typedef u32 MaterialID;
	typedef u32 PointLightID;

#define InvalidRenderID ((RenderID)u32_max)
#define InvalidShaderID ((ShaderID)u32_max)
#define InvalidMaterialID ((MaterialID)u32_max)
#define InvalidPointLightID ((PointLightID)u32_max)

	template<bool> struct StaticAssert;
	template<> struct StaticAssert<true> {};

	enum class SerializableType
	{
		MESH,
		SKYBOX,
		REFLECTION_PROBE,
		NONE
	};

#define STATIC_ASSERT(e) StaticAssert<(e)>{}

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

} // namespace flex

namespace glm
{
	typedef tvec2<flex::i32> vec2i;
}
