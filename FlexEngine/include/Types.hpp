#pragma once

#include <cstdint>
#include <limits>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define ARRAY_LENGTH(a) ARRAY_SIZE(a)

namespace flex
{
	class GameObject;

	using u8 = uint8_t;
	using u16 = uint16_t;
	using u32 = uint32_t;
	using u64 = uint64_t;
	using i8 = int8_t;
	using i16 = int16_t;
	using i32 = int32_t;
	using i64 = int64_t;
	using real = float;
	using deg = real;
	using rad = real;

	using sec = real;	// Seconds
	using ms = real;	// Milliseconds		1x10^-3 seconds
	using us = real;	// Microseconds		1x10^-6 seconds
	using ns = real;	// Nanoseconds		1x10^-9 seconds

	using VertexAttributes = u32;
	using RenderID = u32;
	using ShaderID = u32;
	using MaterialID = u32;
	using TextureID = u32;
	using MeshID = u32;
	using FrameBufferAttachmentID = u32;
	using GraphicsPipelineID = u32;
	using PointLightID = u32;
	using SpotLightID = u32;
	using AreaLightID = u32;
	using AudioSourceID = u32;
	using TrackID = u32;
	using CartID = u32;
	using CartChainID = u32;
	using VariableID = u32;
	using ParticleSystemID = u32;
	using ParticleEmitterID = u32;
	using GameObjectStackID = u32;
	using GPUBufferID = u32;

	using StringID = u64;

	using ThreadHandle = u64;

	using SpecializationConstantID = u32;

	static constexpr auto u8_min = std::numeric_limits<u8>::min();
	static constexpr auto u8_max = std::numeric_limits<u8>::max();
	static constexpr auto u16_min = std::numeric_limits<u16>::min();
	static constexpr auto u16_max = std::numeric_limits<u16>::max();
	static constexpr auto u32_min = std::numeric_limits<u32>::min();
	static constexpr auto u32_max = std::numeric_limits<u32>::max();
	static constexpr auto u64_min = std::numeric_limits<u64>::min();
	static constexpr auto u64_max = std::numeric_limits<u64>::max();

	static constexpr auto i8_min = std::numeric_limits<i8>::min();
	static constexpr auto i8_max = std::numeric_limits<i8>::max();
	static constexpr auto i16_min = std::numeric_limits<i16>::min();
	static constexpr auto i16_max = std::numeric_limits<i16>::max();
	static constexpr auto i32_min = std::numeric_limits<i32>::min();
	static constexpr auto i32_max = std::numeric_limits<i32>::max();
	static constexpr auto i64_min = std::numeric_limits<i64>::min();
	static constexpr auto i64_max = std::numeric_limits<i64>::max();

	static constexpr auto real_min = std::numeric_limits<real>::min();
	static constexpr auto real_max = std::numeric_limits<real>::max();

	static constexpr auto InvalidRenderID = ((RenderID)u32_max);
	static constexpr auto InvalidShaderID = ((ShaderID)u32_max);
	static constexpr auto InvalidMaterialID = ((MaterialID)u32_max);
	static constexpr auto InvalidTextureID = ((TextureID)u32_max);
	static constexpr auto InvalidMeshID = ((MeshID)u32_max);
	static constexpr auto InvalidFrameBufferAttachmentID = ((FrameBufferAttachmentID)u32_max);
	static constexpr auto InvalidGraphicsPipelineID = ((GraphicsPipelineID)u32_max);
	static constexpr auto InvalidPointLightID = ((PointLightID)u32_max);
	static constexpr auto InvalidSpotLightID = ((SpotLightID)u32_max);
	static constexpr auto InvalidAreaLightID = ((AreaLightID)u32_max);
	static constexpr auto InvalidAudioSourceID = ((AudioSourceID)u32_max);
	static constexpr auto InvalidTrackID = ((TrackID)u32_max);
	static constexpr auto InvalidCartID = ((CartChainID)u32_max);
	static constexpr auto InvalidCartChainID = ((CartChainID)u32_max);
	static constexpr auto InvalidVariableID = ((VariableID)u32_max);
	static constexpr auto InvalidParticleSystemID = ((ParticleSystemID)u32_max);
	static constexpr auto InvalidParticleEmitterID = ((ParticleEmitterID)u32_max);
	static constexpr auto InvalidGPUBufferID = ((GPUBufferID)u32_max);
	static constexpr auto InvalidThreadHandle = ((ThreadHandle)u64_max);
	static constexpr auto InvalidBufferID = u64_max;
	static constexpr auto InvalidSpecializationConstantID = (SpecializationConstantID)u32_max;
	static constexpr auto InvalidStringID = (StringID)u64_max;
	static constexpr auto InvalidID = u32_max;


	//template<bool> struct StaticAssert;
	//template<> struct StaticAssert<true> {};

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
		WHOLE, // Covers the whole screen

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
		LINEAR,   // Latest sample is weighted N times higher than Nth sample
		CUBIC,    // Latest samples contribute much more than old samples
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

	static const char* TrackStateStrs[] =
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
} // namespace flex
