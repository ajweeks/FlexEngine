#pragma once

#include "Scene/GameObject.hpp"

#include <set>

// Treasure chests, placed rarely on mountain tops & prominent hilltops (deterministically from the world's seed), holding
// special items. Players find them by the glint they give off in the distance, a chime when close & the map.
namespace flex
{
	class Player;
	class TerrainGenerator;

	class TreasureChest final : public GameObject
	{
	public:
		TreasureChest(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;

		bool IsOpen() const { return m_bOpen; }
		// Gives the chest's loot to the player
		void Open(Player* player);

		// Spawns chests near the player & removes far away ones (called every frame in terrain scenes)
		static void UpdateNearby(TerrainGenerator* terrainGenerator, const glm::vec3& playerPos);
		// Helps players find unopened chests: a twinkling glint drawn over chests in sight (up to GLINT_RANGE away), a
		// notification the first time one is spotted & a faint chime when close by (call every frame after UpdateNearby)
		static void UpdateHints(TerrainGenerator* terrainGenerator, Player* player);
		static void DestroyAll();

		// Chests which have been looted (saved with the world)
		static std::set<u64>& GetOpenedChests() { return s_OpenedChests; }
		// Positions of nearby chest sites which have been found (for the map)
		static void GetKnownSites(std::vector<glm::vec3>& outPositions, std::vector<bool>& outOpened);

	private:
		struct Site
		{
			bool bValid = false;
			glm::vec3 pos;
			GameObjectID chestID;

			// Hints
			bool bInSight = false;
			bool bSpotted = false; // The player has been told about it
			bool bChimed = false; // Re-armed once the player moves away
			real nextSightCheckTime = 0.0f;
		};

		static bool IsInSight(TerrainGenerator* terrainGenerator, const glm::vec3& eye, const glm::vec3& target);

		static Site ComputeSite(TerrainGenerator* terrainGenerator, i32 cellX, i32 cellZ);
		void CreateMeshes();

		u64 m_SiteID = 0;
		bool m_bOpen = false;
		real m_LidAngle = 0.0f;
		GameObject* m_Lid = nullptr;

		static constexpr real SITE_CELL_SIZE = 2500.0f;
		static std::map<u64, Site> s_Sites;
		static std::set<u64> s_OpenedChests;
		static i32 s_SitesSeed;
	};
} // namespace flex
