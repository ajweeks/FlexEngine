#pragma once

#include "PlacementPreview.hpp"
#include "Scene/TerrainGeneration.hpp" // For PlantInfo

// The player's woodcraft (owned by Player): chopping down trees & cacti (left mouse button while aiming at a trunk),
// crafting & placing campfires from wood ([H]), & lighting them (hold [E])
namespace flex
{
	class Campfire;
	class Player;

	class WoodcraftController final
	{
	public:
		void Initialize(Player* player);
		void Destroy();

		// Called every frame (before the terrain tool)
		void Update();
		void DrawHUD() const;

		// The left mouse button is in use (aiming at a plant to chop, or placing a campfire)
		bool BlocksTerrainTool() const { return m_bHasTarget || m_bPlacingCampfire; }
		bool IsPlacingCampfire() const { return m_bPlacingCampfire; }

		// Items in the player's inventory & quick access slots
		i32 CountItems(const PrefabID& prefabID) const;
		// Returns false (removing nothing) if there aren't enough
		bool RemoveItems(const PrefabID& prefabID, i32 count);

		// Test hooks (FLEX_TEST_WOODCRAFT): fell the nearest plant within maxDistance, & build a campfire at pos
		bool FellNearestPlant(real maxDistance, glm::vec3* outPlantPos = nullptr);
		Campfire* BuildCampfire(const glm::vec3& pos, real yaw);
		// (as if [H] were pressed)
		void SetPlacingCampfire(bool bPlacing) { m_bPlacingCampfire = bPlacing; }
		bool IsCampfirePlacementValid() const { return m_bPlacementValid; }

		static constexpr real CHOP_REACH = 3.6f;

	private:
		bool CanAct() const;
		void RefreshNearbyPlants(bool bForce);
		void UpdateTarget();
		void UpdateChopping(bool bCanAct);
		void UpdateCampfirePlacement(bool bCanAct);
		void UpdateLighting(bool bCanAct);
		// One hit on the targeted plant
		void Chop();
		// Scripted test (FLEX_TEST_WOODCRAFT=1): chops a tree, builds, lights & burns out a campfire, & digs out a tree,
		// taking screenshots (saved/screenshots/woodcraft_*.png)
		void UpdateTest();
		i32 HitsToFell(const terrain::PlantInfo& plant) const;

		Player* m_Player = nullptr;

		// Plants around the player (refreshed as they move)
		std::vector<terrain::PlantInfo> m_NearbyPlants;
		glm::vec3 m_NearbyQueryPos = glm::vec3(1e30f);
		sec m_NearbyQueryAge = 0.0f;

		// Plant being aimed at
		bool m_bHasTarget = false;
		terrain::PlantInfo m_Target;
		glm::vec3 m_TargetHitPos = VEC3_ZERO;
		glm::vec3 m_TargetHitNormal = VEC3_UP;

		// Progress chopping a plant
		u32 m_ChopPlantHash = 0;
		i32 m_ChopHits = 0;
		sec m_ChopCooldown = 0.0f;
		sec m_TimeSinceChop = 0.0f;

		bool m_bPlacingCampfire = false;
		bool m_bPlacementValid = false;
		glm::vec3 m_PlacementPos = VEC3_ZERO;
		PlacementPreview m_CampfirePreview;

		// Campfire near enough to light, & how long [E] has been held
		GameObjectID m_NearbyCampfireID = InvalidGameObjectID;
		sec m_LightingProgress = 0.0f;

		bool m_bCampfireHintShown = false;

		i32 m_TestStep = -1; // -1: not testing
		sec m_TestTime = 0.0f;
		terrain::PlantInfo m_TestPlant;
		GameObjectID m_TestCampfireID = InvalidGameObjectID;
		glm::vec3 m_TestCampfirePos = VEC3_ZERO;
		i32 m_TestEditCount = 0; // (+1, 0: not counting)
		sec m_TestSinceFelled = -1.0f; // Until the first wood is picked up (-1: not timing)
		i32 m_TestWoodBeforeFelling = 0;
	};
} // namespace flex
