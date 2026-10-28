#include "stdafx.hpp"

#include "Crafting.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtx/norm.hpp> // For distance2
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
IGNORE_WARNINGS_POP

#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "FlexEngine.hpp"
#include "GameControls.hpp"
#include "GameSounds.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "JSONTypes.hpp"
#include "Localization.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Platform/Platform.hpp"
#include "Player.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObjectTypes.hpp"
#include "Scene/Objects/Stations.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	namespace crafting
	{
		namespace
		{
			const char* RECIPES_FILE = RESOURCE_DIRECTORY "recipes.txt";

			std::vector<Recipe> s_Recipes;
			std::vector<std::pair<PrefabID, sec>> s_Fuels;
			bool s_bLoaded = false;

			std::string Trim(const std::string& s)
			{
				size_t start = s.find_first_not_of(" \t\r\n");
				if (start == std::string::npos)
				{
					return "";
				}
				size_t end = s.find_last_not_of(" \t\r\n");
				return s.substr(start, end - start + 1);
			}

			// "<name> [xN]"
			bool ParseItem(const std::string& text, ItemCount& outItem)
			{
				std::string s = Trim(text);
				outItem.count = 1;
				const size_t space = s.find_last_of(' ');
				if (space != std::string::npos && space + 2 <= s.size() && s[space + 1] == 'x' && space + 2 < s.size() &&
					s.find_first_not_of("0123456789", space + 2) == std::string::npos)
				{
					outItem.count = glm::max(atoi(s.c_str() + space + 2), 1);
					s = Trim(s.substr(0, space));
				}
				outItem.name = s;
				outItem.prefabID = g_ResourceManager->GetPrefabID(s.c_str());
				return !s.empty();
			}

			StringID PlaceTypeFor(const std::string& name)
			{
				if (name == "chest") return ChestSID;
				if (name == "furnace") return FurnaceSID;
				return InvalidStringID;
			}

			void Load()
			{
				s_Recipes.clear();
				s_Fuels.clear();
				s_bLoaded = true;

				std::string contents;
				if (!ReadFile(RECIPES_FILE, contents, false))
				{
					PrintError("Failed to read recipes from %s\n", RECIPES_FILE);
					return;
				}

				std::stringstream stream(contents);
				std::string line;
				i32 lineNumber = 0;
				while (std::getline(stream, line))
				{
					++lineNumber;
					const size_t comment = line.find('#');
					if (comment != std::string::npos)
					{
						line = line.substr(0, comment);
					}
					line = Trim(line);
					if (line.empty())
					{
						continue;
					}
					const size_t colon = line.find(':');
					const size_t equals = line.find('=');
					if (colon == std::string::npos || equals == std::string::npos || equals < colon)
					{
						PrintWarn("%s:%d: expected \"<station>: <output> = <inputs>\"\n", RECIPES_FILE, lineNumber);
						continue;
					}
					const std::string station = Trim(line.substr(0, colon));
					const std::string lhs = line.substr(colon + 1, equals - colon - 1);
					std::string rhs = line.substr(equals + 1);

					if (station == "fuel")
					{
						ItemCount fuel;
						ParseItem(lhs, fuel);
						if (!fuel.prefabID.IsValid())
						{
							PrintWarn("%s:%d: unknown fuel \"%s\"\n", RECIPES_FILE, lineNumber, fuel.name.c_str());
							continue;
						}
						s_Fuels.emplace_back(fuel.prefabID, (sec)atof(rhs.c_str()));
						continue;
					}

					Recipe recipe = {};
					if (station == "hand")
					{
						recipe.station = Station::HAND;
					}
					else if (station == "furnace")
					{
						recipe.station = Station::FURNACE;
					}
					else
					{
						PrintWarn("%s:%d: unknown station \"%s\"\n", RECIPES_FILE, lineNumber, station.c_str());
						continue;
					}

					const size_t at = rhs.find('@');
					if (at != std::string::npos)
					{
						recipe.time = glm::max((sec)atof(rhs.c_str() + at + 1), 0.1f);
						rhs = rhs.substr(0, at);
					}

					ParseItem(lhs, recipe.output);
					recipe.placeTypeID = PlaceTypeFor(recipe.output.name);
					bool bValid = recipe.output.prefabID.IsValid() || recipe.placeTypeID != InvalidStringID;
					if (!bValid)
					{
						PrintWarn("%s:%d: unknown item \"%s\" (recipe skipped)\n", RECIPES_FILE, lineNumber, recipe.output.name.c_str());
					}

					std::stringstream inputs(rhs);
					std::string input;
					while (bValid && std::getline(inputs, input, ','))
					{
						ItemCount item;
						if (!ParseItem(input, item))
						{
							continue;
						}
						if (!item.prefabID.IsValid())
						{
							PrintWarn("%s:%d: unknown item \"%s\" (recipe skipped)\n", RECIPES_FILE, lineNumber, item.name.c_str());
							bValid = false;
							break;
						}
						recipe.inputs.push_back(item);
					}
					if (!bValid || recipe.inputs.empty())
					{
						continue;
					}
					if (recipe.station == Station::FURNACE && recipe.inputs.size() != 1)
					{
						PrintWarn("%s:%d: furnace recipes take one input\n", RECIPES_FILE, lineNumber);
						continue;
					}
					s_Recipes.push_back(recipe);
				}
				Print("Loaded %u recipes & %u fuels from %s\n", (u32)s_Recipes.size(), (u32)s_Fuels.size(), RECIPES_FILE);
			}
		} // anonymous namespace

		const std::vector<Recipe>& GetRecipes()
		{
			if (!s_bLoaded)
			{
				Load();
			}
			return s_Recipes;
		}

		void ReloadRecipes()
		{
			Load();
		}

		const Recipe* FindFurnaceRecipe(const PrefabID& input)
		{
			for (const Recipe& recipe : GetRecipes())
			{
				if (recipe.station == Station::FURNACE && recipe.inputs[0].prefabID == input)
				{
					return &recipe;
				}
			}
			return nullptr;
		}

		sec FuelBurnTime(const PrefabID& prefabID)
		{
			GetRecipes();
			for (const auto& fuel : s_Fuels)
			{
				if (fuel.first == prefabID)
				{
					return fuel.second;
				}
			}
			return 0.0f;
		}

		const Recipe* FindPlaceableRecipe(StringID typeID)
		{
			for (const Recipe& recipe : GetRecipes())
			{
				if (recipe.placeTypeID == typeID)
				{
					return &recipe;
				}
			}
			return nullptr;
		}

		std::string PrefabName(const PrefabID& prefabID)
		{
			GameObject* prefab = prefabID.IsValid() ? g_ResourceManager->GetPrefabTemplate(prefabID) : nullptr;
			return prefab != nullptr ? prefab->GetName() : std::string();
		}

		namespace
		{
			// Item_<CamelCase name> (e.g. "iron ingot" -> Item_IronIngot), for items Loc::ItemName doesn't know
			std::string DisplayNameFromName(const std::string& name)
			{
				const char* known = Loc::ItemName(name);
				if (name != known)
				{
					return known;
				}
				static std::map<std::string, LocID> s_IDs;
				auto iter = s_IDs.find(name);
				if (iter == s_IDs.end())
				{
					std::string key = "Item_";
					bool bUpper = true;
					for (char c : name)
					{
						if (c == ' ')
						{
							bUpper = true;
							continue;
						}
						key += bUpper ? (char)toupper(c) : c;
						bUpper = false;
					}
					iter = s_IDs.emplace(name, Loc::Intern(key.c_str())).first;
				}
				return Loc::Get(iter->second);
			}
		} // anonymous namespace

		std::string ItemDisplayName(const PrefabID& prefabID)
		{
			return DisplayNameFromName(PrefabName(prefabID));
		}

		std::string ItemDisplayName(const ItemCount& item)
		{
			return DisplayNameFromName(item.name);
		}
	} // namespace crafting

	using namespace crafting;

	namespace
	{
		u32 HashU32(u32 x)
		{
			x ^= x >> 16;
			x *= 0x7feb352du;
			x ^= x >> 15;
			x *= 0x846ca68bu;
			x ^= x >> 16;
			return x;
		}

		const real STATION_USE_DISTANCE = 2.6f;
		const real PLACE_REACH = 6.0f;

		// Closest hit along the camera's ray against colliders (ignoring the player) & the terrain
		bool RaycastWorld(const glm::vec3& from, const glm::vec3& dir, real maxDistance, const btCollisionObject* ignored, glm::vec3& outHitPos, glm::vec3& outHitNormal)
		{
			struct IgnoreObjectRayCallback final : public btCollisionWorld::ClosestRayResultCallback
			{
				IgnoreObjectRayCallback(const btVector3& from, const btVector3& to, const btCollisionObject* ignored) :
					ClosestRayResultCallback(from, to),
					ignored(ignored)
				{
					m_collisionFilterMask = (i32)CollisionType::STATIC | (i32)CollisionType::DEFAULT;
				}

				virtual bool needsCollision(btBroadphaseProxy* proxy) const override
				{
					return proxy->m_clientObject != ignored && ClosestRayResultCallback::needsCollision(proxy);
				}

				const btCollisionObject* ignored;
			};

			bool bHit = false;
			real closest = maxDistance;
			const btVector3 btFrom = ToBtVec3(from);
			const btVector3 btTo = ToBtVec3(from + dir * maxDistance);
			IgnoreObjectRayCallback callback(btFrom, btTo, ignored);
			g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld()->rayTest(btFrom, btTo, callback);
			if (callback.hasHit())
			{
				bHit = true;
				outHitPos = ToVec3(callback.m_hitPointWorld);
				outHitNormal = glm::normalize(ToVec3(callback.m_hitNormalWorld));
				closest = glm::distance(from, outHitPos);
			}
			TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
			glm::vec3 terrainHitPos, terrainHitNormal;
			if (terrainGenerator != nullptr && terrainGenerator->Raycast(from, dir, closest, terrainHitPos, terrainHitNormal) &&
				glm::distance(from, terrainHitPos) < closest - 0.1f)
			{
				bHit = true;
				outHitPos = terrainHitPos;
				outHitNormal = terrainHitNormal;
			}
			return bHit;
		}

		std::string StackLabel(const GameObjectStack& stack)
		{
			return stack.count > 1 ? LOCF(Crafting_ItemCount, ItemDisplayName(stack.prefabID).c_str(), stack.count) : ItemDisplayName(stack.prefabID);
		}

		std::string RecipeOutputLabel(const Recipe& recipe)
		{
			const std::string name = ItemDisplayName(recipe.output);
			return recipe.output.count > 1 ? LOCF(Crafting_ItemCount, name.c_str(), recipe.output.count) : name;
		}
	} // anonymous namespace

	void CraftingController::Initialize(Player* player)
	{
		m_Player = player;
		const char* test = getenv("FLEX_TEST_CRAFTING"); // FLEX_TEST_CRAFTING=<1|verify>: 1: builds & uses a chest & furnace, crafts, screenshots the panels (crafting_*.png) & prints PASS/FAIL; verify (a later run of the same world): checks they were saved
		if (test != nullptr)
		{
			m_TestStep = 0;
			m_bTestVerify = strcmp(test, "verify") == 0;
		}
	}

	void CraftingController::Destroy()
	{
		m_Preview.Destroy();
		m_Player = nullptr;
	}

	bool CraftingController::CanAct() const
	{
		return m_Player->m_bPossessed &&
			m_Player->AbleToInteract() &&
			!m_Player->IsInventoryShowing() &&
			!m_Player->IsMinerInventoryShowing() &&
			!m_Player->GetRidingVehicleID().IsValid() &&
			m_Player->GetTrackRidingID() == InvalidTrackID &&
			!m_Player->IsBuilding() &&
			!g_EngineInstance->IsSimulationPaused();
	}

	void CraftingController::ClosePanel()
	{
		m_Screen = Screen::NONE;
		m_StationID = InvalidGameObjectID;
	}

	void CraftingController::OpenCraftingPanel()
	{
		m_Screen = Screen::CRAFTING;
		m_StationID = InvalidGameObjectID;
		m_Scroll = 0.0f;
		m_PlacingRecipe = nullptr;
	}

	void CraftingController::OpenStation(CraftingStation* station)
	{
		if (station == nullptr)
		{
			return;
		}
		m_StationID = station->ID;
		m_Screen = station->GetTypeID() == FurnaceSID ? Screen::FURNACE : Screen::CHEST;
		m_Scroll = 0.0f;
		m_PlacingRecipe = nullptr;
	}

	CraftingStation* CraftingController::GetOpenStation() const
	{
		GameObject* object = m_StationID.Get();
		if (object == nullptr || (object->GetTypeID() != ChestSID && object->GetTypeID() != FurnaceSID))
		{
			return nullptr;
		}
		return (CraftingStation*)object;
	}

	i32 CraftingController::CountItems(const PrefabID& prefabID) const
	{
		i32 count = 0;
		for (const GameObjectStack& stack : m_Player->m_Inventory)
		{
			count += (stack.count > 0 && stack.prefabID == prefabID) ? stack.count : 0;
		}
		for (const GameObjectStack& stack : m_Player->m_QuickAccessInventory)
		{
			count += (stack.count > 0 && stack.prefabID == prefabID) ? stack.count : 0;
		}
		return count;
	}

	bool CraftingController::RemoveItems(const PrefabID& prefabID, i32 count)
	{
		if (CountItems(prefabID) < count)
		{
			return false;
		}
		// From the main inventory first, so held items are taken last
		for (auto* stacks : { &m_Player->m_Inventory[0], &m_Player->m_QuickAccessInventory[0] })
		{
			const u32 size = stacks == &m_Player->m_Inventory[0] ? Player::INVENTORY_ITEM_COUNT : Player::QUICK_ACCESS_ITEM_COUNT;
			for (u32 i = 0; i < size && count > 0; ++i)
			{
				GameObjectStack& stack = stacks[i];
				if (stack.count > 0 && stack.prefabID == prefabID)
				{
					const i32 taken = glm::min(stack.count, count);
					stack.count -= taken;
					count -= taken;
					if (stack.count <= 0)
					{
						stack.Clear();
					}
				}
			}
		}
		return true;
	}

	bool CraftingController::HasIngredients(const Recipe& recipe) const
	{
		for (const ItemCount& input : recipe.inputs)
		{
			if (CountItems(input.prefabID) < input.count)
			{
				return false;
			}
		}
		return true;
	}

	bool CraftingController::Craft(const Recipe& recipe)
	{
		if (recipe.station != Station::HAND || !HasIngredients(recipe))
		{
			return false;
		}
		if (recipe.placeTypeID != InvalidStringID)
		{
			// Ingredients are taken when it's placed
			m_PlacingRecipe = &recipe;
			ClosePanel();
			return true;
		}
		for (const ItemCount& input : recipe.inputs)
		{
			RemoveItems(input.prefabID, input.count);
		}
		m_Player->AddToInventory(recipe.output.prefabID, recipe.output.count);
		Discover(recipe.output.prefabID);
		m_Message = LOCF(Crafting_Crafted, RecipeOutputLabel(recipe).c_str());
		m_MessageTime = 3.0f;
		GameSounds::PlayPlace(m_Player->GetTransform()->GetWorldPosition());
		return true;
	}

	bool CraftingController::IsDiscovered(const Recipe& recipe) const
	{
		if (m_Discovered.count(recipe.output.name) != 0)
		{
			return true;
		}
		for (const ItemCount& input : recipe.inputs)
		{
			if (m_Discovered.count(input.name) != 0)
			{
				return true;
			}
		}
		return false;
	}

	void CraftingController::Discover(const PrefabID& prefabID)
	{
		const std::string name = PrefabName(prefabID);
		if (name.empty() || m_Discovered.count(name) != 0)
		{
			return;
		}

		std::vector<const Recipe*> before;
		for (const Recipe& recipe : GetRecipes())
		{
			if (!IsDiscovered(recipe))
			{
				before.push_back(&recipe);
			}
		}
		m_Discovered.insert(name);
		if (m_bSilentDiscovery)
		{
			return;
		}
		std::string unlocked;
		for (const Recipe* recipe : before)
		{
			if (IsDiscovered(*recipe))
			{
				unlocked += (unlocked.empty() ? "" : ", ") + ItemDisplayName(recipe->output);
			}
		}
		if (!unlocked.empty())
		{
			m_Player->ShowNotification(LOCF(Notify_NewRecipes, unlocked.c_str()));
		}
	}

	void CraftingController::UpdateDiscovery()
	{
		m_DiscoveryTimer -= g_DeltaTime;
		if (m_DiscoveryTimer > 0.0f)
		{
			return;
		}
		m_DiscoveryTimer = 0.5f;
		for (const GameObjectStack& stack : m_Player->m_Inventory)
		{
			if (stack.count > 0) Discover(stack.prefabID);
		}
		for (const GameObjectStack& stack : m_Player->m_QuickAccessInventory)
		{
			if (stack.count > 0) Discover(stack.prefabID);
		}
		m_bSilentDiscovery = false;
	}

	void CraftingController::Serialize(JSONObject& inventoryObj) const
	{
		std::vector<JSONObject> discovered;
		for (const std::string& name : m_Discovered)
		{
			JSONObject item = {};
			item.fields.emplace_back("name", JSONValue(name));
			discovered.push_back(item);
		}
		inventoryObj.fields.emplace_back("discovered items", JSONValue(discovered));
	}

	void CraftingController::Parse(const JSONObject& inventoryObj)
	{
		Reset();
		std::vector<JSONObject> discovered;
		if (inventoryObj.TryGetObjectArray("discovered items", discovered))
		{
			for (const JSONObject& item : discovered)
			{
				std::string name;
				if (item.TryGetString("name", name) && !name.empty())
				{
					m_Discovered.insert(name);
				}
			}
		}
	}

	void CraftingController::Reset()
	{
		m_Discovered.clear();
		m_bSilentDiscovery = true;
		m_DiscoveryTimer = 0.0f;
		m_PlacingRecipe = nullptr;
		ClosePanel();
	}

	void CraftingController::OnDug(const glm::vec3& pos, real hardness)
	{
		if (hardness < 0.5f)
		{
			return;
		}
		// Deterministic from the spot (on a 1m grid)
		const u32 h = HashU32((u32)(i32)glm::floor(pos.x) * 73856093u ^ (u32)(i32)glm::floor(pos.y) * 19349663u ^ (u32)(i32)glm::floor(pos.z) * 83492791u);
		const PrefabID stoneID = g_ResourceManager->GetPrefabID("mineral deposit stone");
		if (stoneID.IsValid())
		{
			m_Player->AddToInventory(stoneID, 1 + (i32)(h % 2u));
		}
		const u32 roll = HashU32(h + 7u) % 100u;
		const char* ore = roll < 14 ? "mineral deposit iron" : roll < 22 ? "mineral deposit tin" : roll < 25 ? "mineral deposit silicon" : nullptr;
		const PrefabID oreID = ore != nullptr ? g_ResourceManager->GetPrefabID(ore) : InvalidPrefabID;
		if (oreID.IsValid())
		{
			m_Player->AddToInventory(oreID, 1);
			m_Player->ShowNotification(LOCF(Notify_FoundOre, ItemDisplayName(oreID).c_str()));
		}
	}

	void CraftingController::DrawPrompt(const std::string& text) const
	{
		g_Renderer->SetFont(SID("editor-02"));
		g_Renderer->DrawStringSS(text, UITheme::TEXT_PRIMARY, AnchorPoint::CENTER, glm::vec2(0.0f, -0.08f), 1.5f, 0.6f);
	}

	void CraftingController::Update()
	{
		if (m_Player == nullptr)
		{
			return;
		}
		UpdateDiscovery();
		if (m_TestStep >= 0)
		{
			UpdateTest();
		}

		if (!CanAct())
		{
			if (!m_Player->m_bPossessed)
			{
				return;
			}
			ClosePanel();
			m_PlacingRecipe = nullptr;
			return;
		}

		const bool bTogglePressed = g_InputManager->GetKeyPressed(GameKeys::CRAFTING);
		const bool bUsePressed = g_InputManager->GetKeyPressed(GameKeys::USE_STATION);
		const glm::vec3 feet = m_Player->GetTransform()->GetWorldPosition() - VEC3_UP * (m_Player->GetHeight() * 0.5f);

		if (m_Screen != Screen::NONE)
		{
			CraftingStation* station = GetOpenStation();
			const bool bStationGone = m_Screen != Screen::CRAFTING &&
				(station == nullptr || glm::distance(station->GetTransform()->GetWorldPosition(), feet) > STATION_USE_DISTANCE + 1.5f);
			if (bTogglePressed || g_InputManager->GetKeyPressed(KeyCode::KEY_ESCAPE) || bStationGone || (bUsePressed && m_Screen != Screen::CRAFTING))
			{
				ClosePanel();
				return;
			}

			m_UI.BeginFrame();
			switch (m_Screen)
			{
			case Screen::CRAFTING: DrawCraftingPanel(); break;
			case Screen::CHEST: DrawChestScreen(); break;
			case Screen::FURNACE: DrawFurnaceScreen(); break;
			default: break;
			}
			return;
		}

		if (m_PlacingRecipe != nullptr)
		{
			UpdatePlacement();
			if (bTogglePressed)
			{
				m_PlacingRecipe = nullptr;
			}
			return;
		}

		if (bTogglePressed)
		{
			OpenCraftingPanel();
			return;
		}

		CraftingStation* nearby = CraftingStation::FindNearest(feet, STATION_USE_DISTANCE);
		if (nearby != nullptr)
		{
			if (bUsePressed)
			{
				OpenStation(nearby);
				return;
			}
			DrawPrompt(nearby->GetTypeID() == FurnaceSID ? LOC(Prompt_UseFurnace) : LOC(Prompt_OpenStorage));
		}

		if (m_MessageTime > 0.0f)
		{
			m_MessageTime -= g_DeltaTime;
		}
	}

	CraftingStation* CraftingController::PlaceStation(const Recipe& recipe, const glm::vec3& pos, real yaw)
	{
		for (const ItemCount& input : recipe.inputs)
		{
			if (CountItems(input.prefabID) < input.count)
			{
				return nullptr;
			}
		}
		for (const ItemCount& input : recipe.inputs)
		{
			RemoveItems(input.prefabID, input.count);
		}
		CraftingStation* station = CraftingStation::Build(recipe.placeTypeID, pos, yaw);
		GameSounds::PlayPlace(pos);
		return station;
	}

	void CraftingController::UpdatePlacement()
	{
		PlacementPreview::Frame previewFrame(m_Preview);
		const Recipe& recipe = *m_PlacingRecipe;

		if (!HasIngredients(recipe) || g_InputManager->IsMouseButtonPressed(MouseButton::RIGHT))
		{
			m_PlacingRecipe = nullptr;
			return;
		}

		BaseCamera* camera = g_CameraManager->CurrentCamera();
		glm::vec3 hitPos, hitNormal;
		m_bPlacementValid = false;
		const btCollisionObject* playerBody = m_Player->GetRigidBody() != nullptr ? m_Player->GetRigidBody()->GetRigidBodyInternal() : nullptr;
		if (RaycastWorld(camera->position, camera->forward, PLACE_REACH, playerBody, hitPos, hitNormal))
		{
			m_PlacementPos = hitPos;
			// Facing the player
			m_PlacementYaw = std::atan2(-camera->forward.x, -camera->forward.z);
			const CraftingStation* nearest = CraftingStation::FindNearest(hitPos, 1.1f);
			const glm::vec3 playerPos = m_Player->GetTransform()->GetWorldPosition();
			const bool bOnPlayer = glm::distance(glm::vec2(playerPos.x, playerPos.z), glm::vec2(hitPos.x, hitPos.z)) < 0.9f;
			m_bPlacementValid = hitNormal.y > 0.7f && nearest == nullptr && !bOnPlayer;

			const StringID typeID = recipe.placeTypeID;
			m_Preview.Show(typeID, [typeID](PlacementPreview::Geometry& geometry) { CraftingStation::BuildPreviewGeometry(typeID, geometry); },
				hitPos, glm::angleAxis(m_PlacementYaw, VEC3_UP), VEC3_ONE, PlacementPreview::ValidColour(m_bPlacementValid));
		}

		const std::string name = ItemDisplayName(recipe.output);
		DrawPrompt(m_bPlacementValid ? LOCF(Crafting_Place, name.c_str()) : LOCF(Crafting_PlaceInvalid, name.c_str()));

		if (m_bPlacementValid && g_InputManager->IsMouseButtonPressed(MouseButton::LEFT))
		{
			if (PlaceStation(recipe, m_PlacementPos, m_PlacementYaw) != nullptr)
			{
				m_Player->ShowNotification(LOCF(Notify_StationBuilt, name.c_str()));
			}
			m_PlacingRecipe = nullptr;
		}
	}

	void CraftingController::DrawCraftingPanel()
	{
		GameUI& ui = m_UI;
		const real s = ui.Scale();
		const real w = glm::min(980.0f * s, ui.Width() - 32.0f * s);
		const real h = 700.0f * s;
		// (below the notifications at the top of the screen)
		const real top = glm::max(ui.Height() * 0.3f, 16.0f * s);
		const UIRect panel((ui.Width() - w) * 0.5f, top, w, glm::min(h, ui.Height() - top - 140.0f * s));
		ui.FillRect(UIRect(0.0f, 0.0f, ui.Width(), ui.Height()), UITheme::WithAlpha(UITheme::INK, 0.45f));
		ui.Panel(panel);

		UIRect content = panel.Inset(UITheme::SPACE_XL * s);
		UIRect header = content.CutTop(56.0f * s, UITheme::SPACE_S * s);
		ui.Text(LOC(Crafting_Title), header.x, header.y + header.h * 0.4f, UITheme::TEXT_H2 * s, UITheme::TEXT_PRIMARY, TextAlign::LEFT, true);
		ui.Text(LOC(Crafting_Hint), header.x + header.w, header.y + header.h * 0.4f, UITheme::TEXT_SMALL * s, UITheme::TEXT_SECONDARY, TextAlign::RIGHT);

		// Footer: what's left to find & the last crafted item
		const real footerHeight = 32.0f * s;
		const UIRect footer(content.x, content.y + content.h - footerHeight, content.w, footerHeight);
		content.h -= footerHeight + UITheme::SPACE_S * s;

		std::vector<const Recipe*> visible;
		u32 hidden = 0;
		for (i32 pass = 0; pass < 2; ++pass)
		{
			for (const Recipe& recipe : GetRecipes())
			{
				if ((pass == 0) != (recipe.station == Station::HAND))
				{
					continue;
				}
				if (IsDiscovered(recipe))
				{
					visible.push_back(&recipe);
				}
				else
				{
					++hidden;
				}
			}
		}

		const real rowHeight = 70.0f * s;
		const real rowGap = UITheme::SPACE_S * s;
		const real viewRows = glm::floor((content.h + rowGap) / (rowHeight + rowGap));
		const real maxScroll = glm::max((real)visible.size() - viewRows, 0.0f);
		if (ui.IsMouseOver(content))
		{
			m_Scroll -= ui.ScrollDelta();
		}
		const UIRect scrollbarRect(content.x + content.w - 8.0f * s, content.y, 8.0f * s, content.h);
		ui.Scrollbar("crafting scroll", scrollbarRect, (real)visible.size(), viewRows, &m_Scroll);
		m_Scroll = glm::clamp(m_Scroll, 0.0f, maxScroll);
		const u32 first = (u32)m_Scroll;

		UIRect rows(content.x, content.y, content.w - (maxScroll > 0.0f ? 20.0f * s : 0.0f), content.h);
		if (visible.empty())
		{
			ui.Text(LOC(Crafting_NothingYet), rows.x, rows.y + 24.0f * s, UITheme::TEXT_BODY * s, UITheme::TEXT_SECONDARY);
		}
		for (u32 i = first; i < (u32)visible.size() && i < first + (u32)viewRows; ++i)
		{
			const Recipe& recipe = *visible[i];
			const UIRect row = rows.CutTop(rowHeight, rowGap);
			ui.FillRect(row, UITheme::RAISED, UITheme::RADIUS_M * s);

			const real textX = row.x + UITheme::SPACE_L * s;
			ui.Text(RecipeOutputLabel(recipe), textX, row.y + row.h * 0.32f, UITheme::TEXT_BODY_LARGE * s, UITheme::TEXT_PRIMARY);

			// Ingredients, coloured by whether there are enough
			real x = textX;
			const real y = row.y + row.h * 0.72f;
			const real size = UITheme::TEXT_SMALL * s;
			if (recipe.station == Station::FURNACE)
			{
				const std::string where = LOCF(Crafting_InFurnace, (i32)glm::round(recipe.time));
				ui.Text(where, x, y, size, UITheme::SKY);
				x += ui.MeasureText(where, size) + UITheme::SPACE_M * s;
			}
			for (const ItemCount& input : recipe.inputs)
			{
				const i32 have = CountItems(input.prefabID);
				const std::string text = LOCF(Crafting_Ingredient, ItemDisplayName(input).c_str(), have, input.count);
				ui.Text(text, x, y, size, have >= input.count ? UITheme::MOSS : UITheme::TEXT_SECONDARY);
				x += ui.MeasureText(text, size) + UITheme::SPACE_L * s;
			}

			if (recipe.station == Station::HAND)
			{
				const UIRect button(row.x + row.w - 170.0f * s - UITheme::SPACE_M * s, row.y + (row.h - 46.0f * s) * 0.5f, 170.0f * s, 46.0f * s);
				const std::string id = "craft " + std::to_string(i);
				const bool bCanCraft = HasIngredients(recipe);
				if (ui.Button(id.c_str(), button, recipe.placeTypeID != InvalidStringID ? LOC(Crafting_Build) : LOC(Crafting_Craft), bCanCraft, bCanCraft))
				{
					Craft(recipe);
					if (m_Screen == Screen::NONE)
					{
						return;
					}
				}
			}
		}

		if (hidden > 0)
		{
			ui.Text(LOCF(Crafting_Undiscovered, (i32)hidden), footer.x, footer.y + footer.h * 0.5f, UITheme::TEXT_SMALL * s, UITheme::TEXT_SECONDARY);
		}
		if (m_MessageTime > 0.0f)
		{
			m_MessageTime -= g_DeltaTime;
			ui.Text(m_Message, footer.x + footer.w, footer.y + footer.h * 0.5f, UITheme::TEXT_BODY * s, UITheme::GOLD, TextAlign::RIGHT);
		}
	}

	void CraftingController::DrawPlayerItems(const UIRect& area, const std::function<bool(const GameObjectStack&)>& filter,
		const std::function<void(GameObjectStack&)>& onClick, const std::string& emptyText)
	{
		GameUI& ui = m_UI;
		const real s = ui.Scale();
		UIRect rows = area;
		const real rowHeight = 44.0f * s;
		const real gap = UITheme::SPACE_XS * s;
		u32 shown = 0;
		const u32 maxRows = (u32)glm::floor((area.h + gap) / (rowHeight + gap));
		for (u32 list = 0; list < 2; ++list)
		{
			GameObjectStack* stacks = list == 0 ? &m_Player->m_QuickAccessInventory[0] : &m_Player->m_Inventory[0];
			const u32 size = list == 0 ? Player::QUICK_ACCESS_ITEM_COUNT : Player::INVENTORY_ITEM_COUNT;
			for (u32 i = 0; i < size && shown < maxRows; ++i)
			{
				GameObjectStack& stack = stacks[i];
				if (stack.count <= 0 || !filter(stack))
				{
					continue;
				}
				const std::string id = "player item " + std::to_string(list) + " " + std::to_string(i);
				if (ui.ListItem(id.c_str(), rows.CutTop(rowHeight, gap), StackLabel(stack), "", false))
				{
					onClick(stack);
				}
				++shown;
			}
		}
		if (shown == 0)
		{
			ui.Text(emptyText, area.x, area.y + rowHeight * 0.5f, UITheme::TEXT_BODY * s, UITheme::TEXT_SECONDARY);
		}
	}

	void CraftingController::StoreStack(GameObjectStack& stack, CraftingStation* station, u32 firstSlot, u32 slotCount)
	{
		if (stack.count <= 0)
		{
			return;
		}
		const i32 left = station->AddItems(stack.prefabID, stack.count, firstSlot, slotCount);
		stack.count = left;
		if (stack.count <= 0)
		{
			stack.Clear();
		}
	}

	void CraftingController::TakeStack(GameObjectStack& stack)
	{
		if (stack.count <= 0)
		{
			return;
		}
		u32 left = m_Player->AddToInventory(stack.prefabID, stack.count, stack.userData, InventoryType::PLAYER_INVENTORY);
		if (left > 0)
		{
			left = m_Player->AddToInventory(stack.prefabID, (i32)left, stack.userData, InventoryType::QUICK_ACCESS);
		}
		stack.count = (i32)left;
		if (stack.count <= 0)
		{
			stack.Clear();
		}
	}

	void CraftingController::Dismantle(CraftingStation* station)
	{
		if (station == nullptr || !station->IsEmpty())
		{
			return;
		}
		if (const Recipe* recipe = FindPlaceableRecipe(station->GetTypeID()))
		{
			for (const ItemCount& input : recipe->inputs)
			{
				m_Player->AddToInventory(input.prefabID, input.count);
			}
		}
		ClosePanel();
		g_SceneManager->CurrentScene()->RemoveObject(station, true);
	}

	void CraftingController::DrawChestScreen()
	{
		CraftingStation* chest = GetOpenStation();
		if (chest == nullptr)
		{
			ClosePanel();
			return;
		}
		GameUI& ui = m_UI;
		const real s = ui.Scale();
		const real w = glm::min(1100.0f * s, ui.Width() - 32.0f * s);
		const real h = 700.0f * s;
		// (below the notifications at the top of the screen)
		const real top = glm::max(ui.Height() * 0.3f, 16.0f * s);
		const UIRect panel((ui.Width() - w) * 0.5f, top, w, glm::min(h, ui.Height() - top - 140.0f * s));
		ui.FillRect(UIRect(0.0f, 0.0f, ui.Width(), ui.Height()), UITheme::WithAlpha(UITheme::INK, 0.45f));
		ui.Panel(panel);

		UIRect content = panel.Inset(UITheme::SPACE_XL * s);
		UIRect header = content.CutTop(56.0f * s, UITheme::SPACE_S * s);
		ui.Text(LOC(Station_Chest), header.x, header.y + header.h * 0.4f, UITheme::TEXT_H2 * s, UITheme::TEXT_PRIMARY, TextAlign::LEFT, true);
		ui.Text(LOC(Station_ChestHint), header.x + header.w, header.y + header.h * 0.4f, UITheme::TEXT_SMALL * s, UITheme::TEXT_SECONDARY, TextAlign::RIGHT);

		const real buttonHeight = 50.0f * s;
		const UIRect buttons(content.x, content.y + content.h - buttonHeight, content.w, buttonHeight);
		content.h -= buttonHeight + UITheme::SPACE_L * s;

		const real gap = UITheme::SPACE_XL * s;
		const real columnWidth = (content.w - gap) * 0.5f;
		UIRect left(content.x, content.y, columnWidth, content.h);
		UIRect right(content.x + columnWidth + gap, content.y, columnWidth, content.h);
		const UIRect leftTitle = left.CutTop(32.0f * s, UITheme::SPACE_S * s);
		const UIRect rightTitle = right.CutTop(32.0f * s, UITheme::SPACE_S * s);
		ui.Text(LOC(Station_YourItems), leftTitle.x, leftTitle.y + leftTitle.h * 0.5f, UITheme::TEXT_BODY_LARGE * s, UITheme::TEXT_SECONDARY);

		u32 used = 0;
		for (u32 i = 0; i < chest->GetSlotCount(); ++i)
		{
			used += chest->GetSlot(i).count > 0 ? 1 : 0;
		}
		ui.Text(LOCF(Station_ChestSlots, (i32)used, (i32)chest->GetSlotCount()), rightTitle.x, rightTitle.y + rightTitle.h * 0.5f, UITheme::TEXT_BODY_LARGE * s, UITheme::TEXT_SECONDARY);

		DrawPlayerItems(left, [](const GameObjectStack&) { return true; },
			[this, chest](GameObjectStack& stack) { StoreStack(stack, chest, 0, chest->GetSlotCount()); }, LOC(Station_NoItems));

		// The chest's slots (two columns of rows)
		const real rowHeight = 44.0f * s;
		const real rowGap = UITheme::SPACE_XS * s;
		const real subColumnWidth = (right.w - UITheme::SPACE_S * s) * 0.5f;
		u32 shown = 0;
		const u32 rowsPerColumn = (u32)glm::floor((right.h + rowGap) / (rowHeight + rowGap));
		for (u32 i = 0; i < chest->GetSlotCount(); ++i)
		{
			GameObjectStack& stack = chest->GetSlot(i);
			if (stack.count <= 0)
			{
				continue;
			}
			const u32 column = shown / glm::max(rowsPerColumn, 1u);
			const u32 rowIndex = shown % glm::max(rowsPerColumn, 1u);
			if (column >= 2)
			{
				break;
			}
			const UIRect row(right.x + column * (subColumnWidth + UITheme::SPACE_S * s), right.y + rowIndex * (rowHeight + rowGap), subColumnWidth, rowHeight);
			const std::string id = "chest slot " + std::to_string(i);
			if (ui.ListItem(id.c_str(), row, StackLabel(stack), "", false))
			{
				TakeStack(stack);
			}
			++shown;
		}
		if (shown == 0)
		{
			ui.Text(LOC(Station_ChestEmpty), right.x, right.y + rowHeight * 0.5f, UITheme::TEXT_BODY * s, UITheme::TEXT_SECONDARY);
		}

		const real buttonWidth = 220.0f * s;
		UIRect button(buttons.x, buttons.y, buttonWidth, buttons.h);
		if (ui.Button("store all", button, LOC(Station_StoreAll)))
		{
			// The main inventory only (quick access holds tools)
			for (GameObjectStack& stack : m_Player->m_Inventory)
			{
				StoreStack(stack, chest, 0, chest->GetSlotCount());
			}
		}
		button.x += buttonWidth + UITheme::SPACE_M * s;
		if (ui.Button("take all", button, LOC(Station_TakeAll)))
		{
			for (u32 i = 0; i < chest->GetSlotCount(); ++i)
			{
				TakeStack(chest->GetSlot(i));
			}
		}
		const UIRect dismantle(buttons.x + buttons.w - buttonWidth, buttons.y, buttonWidth, buttons.h);
		if (ui.Button("dismantle", dismantle, LOC(Station_Dismantle), false, chest->IsEmpty()))
		{
			Dismantle(chest);
		}
	}

	void CraftingController::DrawFurnaceScreen()
	{
		Furnace* furnace = (Furnace*)GetOpenStation();
		if (furnace == nullptr || furnace->GetTypeID() != FurnaceSID)
		{
			ClosePanel();
			return;
		}
		GameUI& ui = m_UI;
		const real s = ui.Scale();
		const real w = glm::min(1100.0f * s, ui.Width() - 32.0f * s);
		const real h = 640.0f * s;
		// (below the notifications at the top of the screen)
		const real top = glm::max(ui.Height() * 0.3f, 16.0f * s);
		const UIRect panel((ui.Width() - w) * 0.5f, top, w, glm::min(h, ui.Height() - top - 140.0f * s));
		ui.FillRect(UIRect(0.0f, 0.0f, ui.Width(), ui.Height()), UITheme::WithAlpha(UITheme::INK, 0.45f));
		ui.Panel(panel);

		UIRect content = panel.Inset(UITheme::SPACE_XL * s);
		UIRect header = content.CutTop(56.0f * s, UITheme::SPACE_S * s);
		ui.Text(LOC(Station_Furnace), header.x, header.y + header.h * 0.4f, UITheme::TEXT_H2 * s, UITheme::TEXT_PRIMARY, TextAlign::LEFT, true);
		ui.Text(LOC(Station_FurnaceHint), header.x + header.w, header.y + header.h * 0.4f, UITheme::TEXT_SMALL * s, UITheme::TEXT_SECONDARY, TextAlign::RIGHT);

		const real gap = UITheme::SPACE_XL * s;
		const real columnWidth = (content.w - gap) * 0.5f;
		UIRect left(content.x, content.y, columnWidth, content.h);
		UIRect right(content.x + columnWidth + gap, content.y, columnWidth, content.h);
		const UIRect leftTitle = left.CutTop(32.0f * s, UITheme::SPACE_S * s);
		ui.Text(LOC(Station_SmeltableItems), leftTitle.x, leftTitle.y + leftTitle.h * 0.5f, UITheme::TEXT_BODY_LARGE * s, UITheme::TEXT_SECONDARY);

		DrawPlayerItems(left,
			[](const GameObjectStack& stack) { return FindFurnaceRecipe(stack.prefabID) != nullptr || FuelBurnTime(stack.prefabID) > 0.0f; },
			[this, furnace](GameObjectStack& stack)
			{
				// Smeltable items go in to be smelted (unless something else is), fuel into the fuel slot
				const GameObjectStack& input = furnace->GetSlot(Furnace::INPUT);
				const bool bSmeltable = FindFurnaceRecipe(stack.prefabID) != nullptr;
				const bool bInputFree = input.count <= 0 || input.prefabID == stack.prefabID;
				if (bSmeltable && (bInputFree || FuelBurnTime(stack.prefabID) <= 0.0f))
				{
					StoreStack(stack, furnace, Furnace::INPUT, 1);
				}
				else
				{
					StoreStack(stack, furnace, Furnace::FUEL, 1);
				}
			}, LOC(Station_NoSmeltables));

		// Slots
		const real slotHeight = 64.0f * s;
		const char* slotNames[] = { LOC(Station_FurnaceInput), LOC(Station_FurnaceFuel), LOC(Station_FurnaceOutput) };
		for (u32 i = 0; i < Furnace::_COUNT; ++i)
		{
			const UIRect label = right.CutTop(28.0f * s, UITheme::SPACE_XS * s);
			ui.Text(slotNames[i], label.x, label.y + label.h * 0.5f, UITheme::TEXT_BODY * s, UITheme::TEXT_SECONDARY);
			const UIRect slotRect = right.CutTop(slotHeight, UITheme::SPACE_S * s);
			GameObjectStack& stack = furnace->GetSlot(i);
			const std::string id = "furnace slot " + std::to_string(i);
			if (ui.ListItem(id.c_str(), slotRect, stack.count > 0 ? StackLabel(stack) : std::string(LOC(Station_Empty)), "", false) && stack.count > 0)
			{
				TakeStack(stack);
			}

			// Progress under the input (smelting) & fuel (burning) slots
			if (i != Furnace::OUTPUT)
			{
				const UIRect bar = right.CutTop(8.0f * s, UITheme::SPACE_M * s);
				const real amount = i == Furnace::INPUT ? furnace->GetProgress() : furnace->GetFuelFraction();
				ui.FillRect(bar, UITheme::OUTLINE, UITheme::RADIUS_S * s);
				if (amount > 0.0f)
				{
					ui.FillRect(UIRect(bar.x, bar.y, bar.w * amount, bar.h), i == Furnace::INPUT ? UITheme::GOLD : UITheme::EMBER, UITheme::RADIUS_S * s);
				}
			}
		}

		// What it's doing
		const GameObjectStack& input = furnace->GetSlot(Furnace::INPUT);
		const Recipe* recipe = input.count > 0 ? FindFurnaceRecipe(input.prefabID) : nullptr;
		std::string status;
		glm::vec4 statusColour = UITheme::TEXT_SECONDARY;
		if (recipe == nullptr)
		{
			status = LOC(Station_FurnaceIdle);
		}
		else if (!furnace->IsBurning() && furnace->GetSlot(Furnace::FUEL).count <= 0)
		{
			status = LOC(Station_FurnaceNeedsFuel);
			statusColour = UITheme::CLAY;
		}
		else
		{
			status = LOCF(Station_FurnaceSmelting, ItemDisplayName(recipe->output).c_str());
			statusColour = UITheme::EMBER;
		}
		const UIRect statusRect = right.CutTop(32.0f * s, UITheme::SPACE_S * s);
		ui.Text(status, statusRect.x, statusRect.y + statusRect.h * 0.5f, UITheme::TEXT_BODY * s, statusColour);

		const real buttonWidth = 220.0f * s;
		const UIRect dismantle(right.x + right.w - buttonWidth, content.y + content.h - 50.0f * s, buttonWidth, 50.0f * s);
		if (ui.Button("dismantle", dismantle, LOC(Station_Dismantle), false, furnace->IsEmpty() && !furnace->IsBurning()))
		{
			Dismantle(furnace);
		}
	}

	void CraftingController::UpdateTest()
	{
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		BaseScene* scene = g_SceneManager->CurrentScene();
		if (!m_Player->m_bPossessed || terrainGenerator == nullptr || !terrainGenerator->IsLoadComplete() ||
			m_Player->IsAwaitingTerrainCollision() || g_EngineInstance->IsSimulationPaused())
		{
			m_TestTime = 0.0f;
			return;
		}
		m_TestTime += g_DeltaTime;

		auto Screenshot = [](const char* name)
		{
			const std::string directory = RelativePathToAbsolute(SCREENSHOT_DIRECTORY);
			Platform::CreateDirectoryRecursive(directory);
			g_Renderer->RequestScreenshot(directory + "crafting_" + name + ".png");
		};
		auto Check = [this](bool bOk, const char* what)
		{
			Print("Crafting test: %s: %s\n", what, bOk ? "ok" : "FAILED");
			m_bTestPass = m_bTestPass && bOk;
		};
		auto Next = [this]() { ++m_TestStep; m_TestTime = 0.0f; };
		auto ID = [](const char* name) { return g_ResourceManager->GetPrefabID(name); };
		auto Find = [](StringID typeID) -> CraftingStation*
		{
			for (CraftingStation* station : CraftingStation::GetAll())
			{
				if (station->GetTypeID() == typeID)
				{
					return station;
				}
			}
			return nullptr;
		};
		auto CountIn = [](CraftingStation* station, const PrefabID& prefabID)
		{
			i32 count = 0;
			for (u32 i = 0; station != nullptr && i < station->GetSlotCount(); ++i)
			{
				count += station->GetSlot(i).prefabID == prefabID ? station->GetSlot(i).count : 0;
			}
			return count;
		};
		auto Ground = [terrainGenerator](const glm::vec3& pos)
		{
			glm::vec3 hitPos, hitNormal;
			if (terrainGenerator->Raycast(pos + VEC3_UP * 5.0f, -VEC3_UP, 20.0f, hitPos, hitNormal))
			{
				return hitPos;
			}
			return pos;
		};

		auto AllStacks = [this]()
		{
			std::vector<std::reference_wrapper<GameObjectStack>> stacks;
			for (GameObjectStack& stack : m_Player->m_Inventory) stacks.push_back(stack);
			for (GameObjectStack& stack : m_Player->m_QuickAccessInventory) stacks.push_back(stack);
			return stacks;
		};
		const glm::vec3 playerPos = m_Player->GetTransform()->GetWorldPosition();
		const glm::vec3 forward = glm::normalize(glm::vec3(m_Player->GetTransform()->GetForward().x, 0.0f, m_Player->GetTransform()->GetForward().z) + glm::vec3(1e-4f));
		const glm::vec3 right = glm::cross(forward, VEC3_UP);

		if (m_bTestVerify)
		{
			if (m_TestStep == 0 && m_TestTime > 3.0f)
			{
				Chest* chest = (Chest*)Find(ChestSID);
				Furnace* furnace = (Furnace*)Find(FurnaceSID);
				Check(chest != nullptr && furnace != nullptr, "chest & furnace loaded");
				Check(CountIn(chest, ID("wood")) == 3 && CountIn(chest, ID("mineral deposit stone")) == 2, "chest contents loaded");
				Check(furnace != nullptr && CountIn(furnace, ID("iron ingot")) >= 2, "furnace output loaded");
				Check(m_Discovered.count("iron ingot") != 0, "discovered items loaded");
				Print("Crafting test (verify): %s\n", m_bTestPass ? "PASS" : "FAIL");
				Next();
				g_EngineInstance->Stop();
			}
			return;
		}

		switch (m_TestStep)
		{
		case 0:
			if (m_TestTime > 2.0f)
			{
				// Start from known amounts
				for (const char* name : { "wood", "mineral deposit stone", "mineral deposit iron", "charcoal" })
				{
					RemoveItems(ID(name), CountItems(ID(name)));
				}
				m_Player->AddToInventory(ID("wood"), 30);
				m_Player->AddToInventory(ID("mineral deposit stone"), 20);
				m_Player->AddToInventory(ID("mineral deposit iron"), 2);
				m_Player->AddToInventory(ID("charcoal"), 2);
				m_DiscoveryTimer = 0.0f;
				Next();
			}
			break;
		case 1:
			if (m_TestTime > 1.0f)
			{
				const Recipe* chestRecipe = FindPlaceableRecipe(ChestSID);
				Check(chestRecipe != nullptr && IsDiscovered(*chestRecipe), "chest recipe discovered");
				// Shows the placement hologram
				m_Player->SetPitch(glm::radians(30.0f));
				Check(chestRecipe != nullptr && Craft(*chestRecipe) && IsPlacing(), "crafting a chest starts placing it");
				Next();
			}
			break;
		case 2:
			if (m_TestTime > 1.5f)
			{
				Screenshot("place");
				m_PlacingRecipe = nullptr;
				m_Player->SetPitch(glm::radians(15.0f));
				const i32 woodBefore = CountItems(ID("wood"));
				Chest* chest = (Chest*)PlaceStation(*FindPlaceableRecipe(ChestSID), Ground(playerPos + forward * 3.0f - right * 1.0f), std::atan2(-forward.x, -forward.z));
				Furnace* furnace = (Furnace*)PlaceStation(*FindPlaceableRecipe(FurnaceSID), Ground(playerPos + forward * 3.0f + right * 1.2f), std::atan2(-forward.x, -forward.z));
				Check(chest != nullptr && furnace != nullptr, "chest & furnace built");
				Check(CountItems(ID("wood")) == woodBefore - 14 && CountItems(ID("mineral deposit stone")) == 8, "ingredients taken");
				Next();
			}
			break;
		case 3:
			if (m_TestTime > 0.5f)
			{
				Chest* chest = (Chest*)Find(ChestSID);
				Furnace* furnace = (Furnace*)Find(FurnaceSID);
				if (chest == nullptr || furnace == nullptr)
				{
					Check(false, "stations exist");
					m_TestStep = 99;
					break;
				}
				// Store wood & stone, take some back
				for (GameObjectStack& stack : AllStacks())
				{
					if (stack.prefabID == ID("wood") || stack.prefabID == ID("mineral deposit stone"))
					{
						StoreStack(stack, chest, 0, chest->GetSlotCount());
					}
				}
				Check(CountIn(chest, ID("wood")) == 16 && CountItems(ID("wood")) == 0, "stored in chest");
				for (u32 i = 0; i < chest->GetSlotCount(); ++i)
				{
					if (chest->GetSlot(i).prefabID == ID("wood"))
					{
						TakeStack(chest->GetSlot(i));
					}
				}
				Check(CountItems(ID("wood")) == 16 && CountIn(chest, ID("wood")) == 0, "taken from chest");
				chest->AddItems(ID("wood"), 3);
				RemoveItems(ID("wood"), 3);
				for (u32 i = 0; i < chest->GetSlotCount(); ++i)
				{
					if (chest->GetSlot(i).prefabID == ID("mineral deposit stone"))
					{
						chest->GetSlot(i).count = 2;
					}
				}

				// Smelt iron with charcoal (sped up)
				Furnace::s_SpeedMultiplier = 6.0f;
				for (GameObjectStack& stack : AllStacks())
				{
					if (stack.prefabID == ID("mineral deposit iron"))
					{
						StoreStack(stack, furnace, Furnace::INPUT, 1);
					}
					else if (stack.prefabID == ID("charcoal"))
					{
						StoreStack(stack, furnace, Furnace::FUEL, 1);
					}
				}
				Check(furnace->GetSlot(Furnace::INPUT).count == 2 && furnace->GetSlot(Furnace::FUEL).count == 2, "furnace loaded");
				OpenStation(chest);
				Next();
			}
			break;
		case 4:
			if (m_TestTime > 1.0f) { Screenshot("chest"); Next(); }
			break;
		case 5:
			// (a frame after the screenshot was requested, so it's still of the chest)
			if (m_TestTime > 0.3f && m_Screen != Screen::FURNACE) { OpenStation(Find(FurnaceSID)); }
			if (m_TestTime > 1.5f) { Screenshot("furnace"); Next(); }
			break;
		case 6:
		{
			Furnace* furnace = (Furnace*)Find(FurnaceSID);
			const bool bDone = furnace != nullptr && furnace->GetSlot(Furnace::OUTPUT).count >= 2;
			if (bDone || m_TestTime > 12.0f)
			{
				Check(bDone, "furnace smelted 2 iron ingots");
				Check(furnace != nullptr && furnace->GetSlot(Furnace::FUEL).count == 1, "furnace burnt 1 fuel");
				Furnace::s_SpeedMultiplier = 1.0f;
				// Take an ingot to unlock its recipes, then craft an axe
				m_Player->AddToInventory(ID("iron ingot"), 1);
				m_DiscoveryTimer = 0.0f;
				OpenCraftingPanel();
				Next();
			}
		} break;
		case 7:
			if (m_TestTime > 1.0f)
			{
				Screenshot("panel");
				const i32 axesBefore = CountItems(ID("axe"));
				bool bCrafted = false;
				for (const Recipe& recipe : GetRecipes())
				{
					if (recipe.output.name == "axe")
					{
						m_Player->AddToInventory(ID("mineral deposit stone"), 2);
						bCrafted = Craft(recipe);
					}
				}
				Check(bCrafted && CountItems(ID("axe")) == axesBefore + 1, "crafted an axe");
				Check(m_Discovered.count("iron ingot") != 0, "iron ingot discovered");
				Next();
			}
			break;
		case 8:
			if (m_TestTime > 0.5f)
			{
				ClosePanel();
				// Save round trip of the stations (also saved with the world on exit, see =verify)
				Chest* chest = (Chest*)Find(ChestSID);
				JSONObject saved = chest->Serialize(scene, true, false);
				JSONObject stationObj;
				std::vector<JSONObject> slots;
				Check(saved.TryGetObject("station", stationObj) && stationObj.TryGetObjectArray("slots", slots) && slots.size() == 2, "chest serializes its slots");
				Print("Crafting test: %s\n", m_bTestPass ? "PASS" : "FAIL");
				Next();
				g_EngineInstance->Stop();
			}
			break;
		default:
			break;
		}
	}
} // namespace flex
