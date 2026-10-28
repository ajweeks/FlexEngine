#pragma once

#include "PlacementPreview.hpp"
#include "Scene/GameObject.hpp"

// Crafting stations the player builds (see docs/crafting.md & Crafting.hpp): chests store items, furnaces smelt &
// cook over time while burning fuel. Both keep their slots (& state) in the world save.
namespace flex
{
	class CraftingStation : public GameObject
	{
	public:
		CraftingStation(const std::string& name, StringID typeID, const GameObjectID& gameObjectID, const PrefabIDPair& sourcePrefabID,
			bool bIsPrefabTemplate, u32 slotCount);

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;

		u32 GetSlotCount() const { return (u32)m_Slots.size(); }
		GameObjectStack& GetSlot(u32 index) { return m_Slots[index]; }
		bool IsEmpty() const;
		// Adds items to the slots (stacking where possible), returns how many didn't fit
		i32 AddItems(const PrefabID& prefabID, i32 count, u32 firstSlot = 0, u32 slotCount = 0xFFFFFFFF);

		// Half extents of the station's box (its base is at the object's origin)
		virtual glm::vec3 GetHalfExtents() const = 0;

		// Nearest station within maxDistance of pos
		static CraftingStation* FindNearest(const glm::vec3& pos, real maxDistance);
		static const std::vector<CraftingStation*>& GetAll() { return s_Stations; }

		// Creates & adds a station of the given type (ChestSID or FurnaceSID) to the current scene
		static CraftingStation* Build(StringID typeID, const glm::vec3& pos, real yaw);
		// Placement hologram (local space) of a station type
		static void BuildPreviewGeometry(StringID typeID, PlacementPreview::Geometry& geometry);

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;
		virtual void ParseStationFields(const JSONObject& stationObj) { FLEX_UNUSED(stationObj); }
		virtual void SerializeStationFields(JSONObject& stationObj) const { FLEX_UNUSED(stationObj); }
		virtual void CreateMeshes() = 0;

		std::vector<GameObjectStack> m_Slots;

		static std::vector<CraftingStation*> s_Stations;
	};

	class Chest final : public CraftingStation
	{
	public:
		Chest(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual glm::vec3 GetHalfExtents() const override { return HalfExtents(); }
		static glm::vec3 HalfExtents() { return glm::vec3(0.55f, 0.36f, 0.35f); }

		static void BuildBoxes(std::vector<std::pair<glm::vec3, glm::vec3>>& outWood, std::vector<std::pair<glm::vec3, glm::vec3>>& outTrim);

		static constexpr u32 SLOT_COUNT = 24;

	protected:
		virtual void CreateMeshes() override;
	};

	class Furnace final : public CraftingStation
	{
	public:
		enum Slot : u32
		{
			INPUT,
			FUEL,
			OUTPUT,

			_COUNT
		};

		Furnace(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual void Update() override;
		virtual glm::vec3 GetHalfExtents() const override { return HalfExtents(); }
		static glm::vec3 HalfExtents() { return glm::vec3(0.5f, 0.6f, 0.5f); }

		bool IsBurning() const { return m_FuelRemaining > 0.0f; }
		// Of the item being smelted (0 to 1)
		real GetProgress() const;
		// Of the fuel item burning (1: just started)
		real GetFuelFraction() const { return m_FuelTotal > 0.0f ? m_FuelRemaining / m_FuelTotal : 0.0f; }
		// (for testing) multiplies smelting & burning speed
		static real s_SpeedMultiplier;

		static void BuildBoxes(std::vector<std::pair<glm::vec3, glm::vec3>>& outStone, std::vector<std::pair<glm::vec3, glm::vec3>>& outMouth);

	protected:
		virtual void CreateMeshes() override;
		virtual void ParseStationFields(const JSONObject& stationObj) override;
		virtual void SerializeStationFields(JSONObject& stationObj) const override;

	private:
		sec m_FuelRemaining = 0.0f;
		sec m_FuelTotal = 0.0f;
		sec m_Progress = 0.0f;
		GameObject* m_Glow = nullptr;
		GameObject* m_Mouth = nullptr;
		bool m_bGlowShown = true; // (both meshes start visible)
		bool m_bMouthShown = true;
	};
} // namespace flex
