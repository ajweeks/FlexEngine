#pragma once

// Per world statistics, shown in the main menu's world list & saved with each world. Counters are bumped where the
// events happen, e.g.:
//     if (WorldStats* stats = GetCurrentWorldStats()) { stats->torchesPlaced++; }
// (null when no world is being played, e.g. in the editor's test scenes)
namespace flex
{
	struct JSONObject;

	// Side of the square cells explored area is counted in (meters)
	const real EXPLORED_CELL_SIZE = 128.0f;

	struct WorldStats
	{
		u32 torchesPlaced = 0;
		u32 piecesBuilt = 0; // Build pieces of any kind (including torches)
		u32 tracksLaid = 0;
		real railLaid = 0.0f; // Meters
		real distanceTravelled = 0.0f; // Meters
		u32 chestsOpened = 0;
		// Explored area: the number of EXPLORED_CELL_SIZE square cells the player has been near (see Game::UpdateExplored)
		u32 exploredCells = 0;

		real ExploredAreaKm2() const { return (real)exploredCells * EXPLORED_CELL_SIZE * EXPLORED_CELL_SIZE / 1e6f; }

		// Written to / read from a "stats" object in the world's save (see Game::SaveWorld)
		void Serialize(JSONObject& parentObject) const;
		void Parse(const JSONObject& parentObject);
	};

	// The stats of the world being played (null if none)
	WorldStats* GetCurrentWorldStats();
} // namespace flex
