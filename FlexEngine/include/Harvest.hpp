#pragma once

// Berries (from berry bushes) & coconuts (from palm trees) which the player picks by pressing [E] while aiming at the
// source (see WoodcraftController). Sources are procedural, so only the ones picked are remembered: when (on a clock
// which only runs while playing) so they can regrow. Saved with the world.
namespace flex
{
	namespace harvest
	{
		enum class Kind : u8
		{
			BERRIES,
			COCONUTS,
		};

		static constexpr sec REGROW_TIME = 480.0f;

		// Forgets everything (new world)
		void Reset();
		// Advances the clock
		void Tick(sec dt);

		// Not picked recently
		bool IsReady(Kind kind, u32 sourceHash);
		void MarkPicked(Kind kind, u32 sourceHash);
		// Seconds until it regrows (0 if ready)
		sec TimeUntilReady(Kind kind, u32 sourceHash);

		// How many the source gives (deterministic from its hash)
		i32 GetYield(Kind kind, u32 sourceHash);
		// Prefab names of the items
		const char* ItemPrefabName(Kind kind);

		// World save: "clock;key,pickedAt;key,pickedAt;..." (sources which have regrown aren't saved)
		std::string Serialize();
		void Deserialize(const std::string& str);
	} // namespace harvest
} // namespace flex
