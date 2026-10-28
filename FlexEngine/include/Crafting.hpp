#pragma once

#include "GameUI.hpp"
#include "PlacementPreview.hpp"
#include "Scene/GameObject.hpp" // For GameObjectStack

// Crafting (see docs/crafting.md): recipes are read from resources/recipes.txt. Hand recipes are made from the
// crafting panel ([I]), furnace recipes in a placed furnace over time. A recipe is listed once the player has held any
// of its ingredients (or its output), so finding new things unlocks new recipes. Chests & furnaces are crafted, then
// placed in the world with a hologram.
namespace flex
{
	class CraftingStation;
	class Player;

	namespace crafting
	{
		enum class Station : u8
		{
			HAND,
			FURNACE,

			_COUNT
		};

		struct ItemCount
		{
			std::string name; // Prefab name
			PrefabID prefabID = InvalidPrefabID;
			i32 count = 1;
		};

		struct Recipe
		{
			Station station = Station::HAND;
			ItemCount output;
			std::vector<ItemCount> inputs;
			sec time = 10.0f; // Furnace recipes: per item
			StringID placeTypeID = InvalidStringID; // Chests & furnaces: built in the world instead of carried
		};

		// (loaded on first use)
		const std::vector<Recipe>& GetRecipes();
		void ReloadRecipes();
		// The furnace recipe taking this item (null if none)
		const Recipe* FindFurnaceRecipe(const PrefabID& input);
		// How long one of this item burns in a furnace (0: it isn't fuel)
		sec FuelBurnTime(const PrefabID& prefabID);
		// The recipe building this station type
		const Recipe* FindPlaceableRecipe(StringID typeID);

		std::string PrefabName(const PrefabID& prefabID);
		// Localized name, as shown to the player
		std::string ItemDisplayName(const PrefabID& prefabID);
	} // namespace crafting

	// The player's crafting (owned by Player): the crafting panel, chest & furnace screens, & placing stations
	class CraftingController final
	{
	public:
		void Initialize(Player* player);
		void Destroy();

		// Input, screens & placement, called every frame
		void Update();

		// A panel is open (the player can't look around or act)
		bool IsPanelOpen() const { return m_Screen != Screen::NONE; }
		// Placing a chest or furnace (the mouse buttons are in use)
		bool IsPlacing() const { return m_PlacingRecipe != nullptr; }
		void ClosePanel();
		void OpenCraftingPanel();
		void OpenStation(CraftingStation* station);

		// Items in the player's inventory & quick access slots
		i32 CountItems(const PrefabID& prefabID) const;
		// Returns false (removing nothing) if there aren't enough
		bool RemoveItems(const PrefabID& prefabID, i32 count);
		bool HasIngredients(const crafting::Recipe& recipe) const;
		// Hand recipes: makes the item (or starts placing a station). Returns false if it can't be made.
		bool Craft(const crafting::Recipe& recipe);

		// A dig stroke finished (hardness: 0 loose ground to 1 rock): rock gives stone, & sometimes ore (chosen from the spot)
		void OnDug(const glm::vec3& pos, real hardness);

		bool IsDiscovered(const crafting::Recipe& recipe) const;
		void Discover(const PrefabID& prefabID);

		// Saved in the player's inventory object (discovered items)
		void Serialize(JSONObject& inventoryObj) const;
		void Parse(const JSONObject& inventoryObj);
		void Reset();

	private:
		enum class Screen
		{
			NONE,
			CRAFTING,
			CHEST,
			FURNACE,
		};

		bool CanAct() const;
		void UpdateDiscovery();
		void UpdatePlacement();
		CraftingStation* PlaceStation(const crafting::Recipe& recipe, const glm::vec3& pos, real yaw);
		CraftingStation* GetOpenStation() const;

		void DrawCraftingPanel();
		void DrawChestScreen();
		void DrawFurnaceScreen();
		// Lists the player's non-empty slots that pass the filter (as rows), calling onClick with the clicked slot
		void DrawPlayerItems(const UIRect& area, const std::function<bool(const GameObjectStack&)>& filter,
			const std::function<void(GameObjectStack&)>& onClick, const std::string& emptyText);
		// Moves as much of a player stack as fits into a station's slots
		void StoreStack(GameObjectStack& stack, CraftingStation* station, u32 firstSlot, u32 slotCount);
		// Moves a station's slot into the player's inventory
		void TakeStack(GameObjectStack& stack);
		// Removes an empty station, refunding its recipe
		void Dismantle(CraftingStation* station);
		void DrawPrompt(const std::string& text) const;

		// Scripted test (FLEX_TEST_CRAFTING=1, or =verify on a later run to check the world save)
		void UpdateTest();

		Player* m_Player = nullptr;
		GameUI m_UI;
		Screen m_Screen = Screen::NONE;
		GameObjectID m_StationID = InvalidGameObjectID;
		real m_Scroll = 0.0f;
		std::string m_Message;
		sec m_MessageTime = 0.0f;

		std::set<std::string> m_Discovered; // Prefab names
		sec m_DiscoveryTimer = 0.0f;
		bool m_bSilentDiscovery = true; // (no notifications for what's already known when a world loads)

		const crafting::Recipe* m_PlacingRecipe = nullptr;
		bool m_bPlacementValid = false;
		glm::vec3 m_PlacementPos = VEC3_ZERO;
		real m_PlacementYaw = 0.0f;
		PlacementPreview m_Preview;

		i32 m_TestStep = -1; // -1: not testing
		bool m_bTestVerify = false;
		sec m_TestTime = 0.0f;
		bool m_bTestPass = true;
	};
} // namespace flex
