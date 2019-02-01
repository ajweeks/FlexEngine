#pragma once

#include <cstdint>
#include <limits>

#define STATIC_ASSERT(e) StaticAssert<(e)>{}

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define ARRAY_LENGTH(a) ARRAY_SIZE(a)

namespace flex
{
	class GameObject;

	using i8 = int8_t;
	using i16 = int16_t;
	using i32 = int32_t;
	using i64 = int64_t;
	using u8 = uint8_t;
	using u16 = uint16_t;
	using u32 = uint32_t;
	using u64 = uint64_t;
	using real = float;
	using deg = real;
	using rad = real;

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

	using sec = real;	// Seconds
	using ms = real;	// Milliseconds		1x10^-3 seconds
	using us = real;	// Microseconds		1x10^-6 seconds
	using ns = real;	// Nanoseconds		1x10^-9 seconds

	using VertexAttributes = u32;
	using RenderID = u32;
	using ShaderID = u32;
	using MaterialID = u32;
	using TextureID = u32;
	using PointLightID = u32;
	using AudioSourceID = u32;
	using TrackID = u32;
	using CartID = u32;
	using CartChainID = u32;

#define InvalidRenderID ((RenderID)u32_max)
#define InvalidShaderID ((ShaderID)u32_max)
#define InvalidMaterialID ((MaterialID)u32_max)
#define InvalidTextureID ((TextureID)u32_max)
#define InvalidPointLightID ((PointLightID)u32_max)
#define InvalidAudioSourceID ((AudioSourceID)u32_max)
#define InvalidTrackID ((TrackID)u32_max)
#define InvalidCartID ((CartChainID)u32_max)
#define InvalidCartChainID ((CartChainID)u32_max)

	//template<bool> struct StaticAssert;
	//template<> struct StaticAssert<true> {};

	enum class GameObjectType
	{
		OBJECT,
		POINT_LIGHT,
		DIRECTIONAL_LIGHT,
		PLAYER,
		SKYBOX,
		REFLECTION_PROBE,
		VALVE,
		RISING_BLOCK,
		GLASS_PANE,
		CART,
		ENGINE_CART,
		MOBILE_LIQUID_BOX,


		// NOTE: Add new types above this line
		// NOTE: All additions *must* be also added to GameObjectTypeStrings in the same order!
		_NONE
	};

	static constexpr const char* GameObjectTypeStrings[] =
	{
		"object",
		"point light",
		"directional light",
		"player",
		"skybox",
		"reflection probe",
		"valve",
		"rising block",
		"glass pane",
		"cart",
		"engine cart",
		"mobile liquid box",

		"NONE"
	};

	static_assert(ARRAY_LENGTH(GameObjectTypeStrings) == (u32)GameObjectType::_NONE + 1, "Length of GameObjectTypeStrings must match length of GameObjectType enum");

	// Screen-space anchors
	enum class AnchorPoint
	{
		CENTER,
		TOP_LEFT,
		TOP,
		TOP_RIGHT,
		RIGHT,
		BOTTOM_RIGHT,
		BOTTOM,
		BOTTOM_LEFT,
		LEFT,
		WHOLE, // cover the whole screen

		_NONE
	};

	enum class EventReply
	{
		CONSUMED,
		UNCONSUMED
	};

	// TODO: Move enums to their own header
	enum class SamplingType
	{
		CONSTANT, // All samples are equally-weighted
		LINEAR    // Latest sample is weighted N times higher than Nth sample
	};

	enum class TurningDir
	{
		LEFT,
		NONE,
		RIGHT
	};

	enum class TransformState
	{
		TRANSLATE,
		ROTATE,
		SCALE,

		_NONE
	};

	enum class TrackState
	{
		FACING_FORWARD,
		FACING_BACKWARD,

		_NONE
	};

	static constexpr const char* TrackStateStrs[] =
	{
		"Facing forward",
		"Facing backward",

		"NONE",
	};

	static_assert(ARRAY_LENGTH(TrackStateStrs) == (u32)TrackState::_NONE + 1, "Length of TrackStateStrs must match length of TrackState enum");

	enum class LookDirection
	{
		LEFT,
		CENTER,
		RIGHT,

		_NONE
	};

	struct MeshImportSettings
	{
		/* Whether or not to invert the horizontal texture coordinate */
		bool flipU = false;
		/* Whether or not to invert the vertical texture coordinate */
		bool flipV = false;
		/* Whether or not to invert the Z component (up) of all normals */
		bool flipNormalZ = false;
		/* Whether or not to swap Y and Z components of all normals (converts from Y-up to Z-up) */
		bool swapNormalYZ = false;
	};

} // namespace flex
