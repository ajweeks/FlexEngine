#pragma once

#include "Audio/SoundSynth.hpp"

// The game's procedurally generated sounds. Each sound has a number of variants (seeded differently), so repeated
// sounds like footsteps don't sound identical. Engine independent (see SoundSynth.hpp).
namespace flex
{
	namespace sound_recipes
	{
		enum class Surface : u32
		{
			GRASS,
			GRAVEL, // Sand, gravel & dirt
			ROCK,
			SNOW,
			WOOD, // Built floors etc.
			WATER, // Shallow water

			_COUNT
		};

		enum class Sound : u32
		{
			// NOTE: Must stay in the same order as Surface
			FOOTSTEP_GRASS,
			FOOTSTEP_GRAVEL,
			FOOTSTEP_ROCK,
			FOOTSTEP_SNOW,
			FOOTSTEP_WOOD,
			FOOTSTEP_WATER,

			JUMP,
			LAND_LIGHT,
			LAND_HEAVY,
			GLIDER_STOW,
			PLACE,
			TORCH_IGNITE,
			CAMPFIRE_IGNITE,
			CHOP,
			TREE_FALL,
			UI_TICK,
			UI_CONFIRM,

			// Seamless loops
			LOOP_ENGINE,
			LOOP_SKID,
			LOOP_WIND,
			LOOP_AIR_RUSH,
			LOOP_CRICKETS,
			LOOP_CAMPFIRE,

			_COUNT
		};

		struct SoundDesc
		{
			const char* name;
			u32 variantCount;
			bool bLoop;
			// Positional (otherwise played relative to the listener)
			bool b3D;
		};

		const SoundDesc& GetSoundDesc(Sound sound);

		// Generates one variant of a sound. Deterministic & thread safe.
		synth::Buffer Generate(Sound sound, u32 variant);
	} // namespace sound_recipes
} // namespace flex
