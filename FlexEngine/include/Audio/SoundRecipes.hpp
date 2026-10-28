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
			SAND,

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
			FOOTSTEP_SAND,

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
			DIG_STONE, // Pickaxe clink
			DIG_SAND, // Swoosh
			DIG_GRAVEL, // Crunch
			ROPE_THROW,
			GASP, // Breathing in after surfacing

			// Seamless loops
			LOOP_ENGINE,
			LOOP_SKID,
			LOOP_WIND,
			LOOP_AIR_RUSH,
			LOOP_CRICKETS,
			LOOP_CAMPFIRE,
			LOOP_TREASURE_HUM,
			LOOP_WAVES,
			LOOP_UNDERWATER,

			// Long calm background tracks (not looped; each variant is a different key & melody)
			MUSIC_CALM,

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

		// When the recipes were compiled. Caches of generated sounds include it (& synth::BuildStamp), so they're
		// remade after any change to a recipe.
		const char* BuildStamp();

		// Generates one variant of a sound. Deterministic & thread safe.
		synth::Buffer Generate(Sound sound, u32 variant);
	} // namespace sound_recipes
} // namespace flex
