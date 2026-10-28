#include "stdafx.hpp"

#include "Woodcraft.hpp"
#include "Localization.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtx/norm.hpp> // For distance2
IGNORE_WARNINGS_POP

#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "FlexEngine.hpp"
#include "Game.hpp"
#include "GameSounds.hpp"
#include "Graphics/BitmapFont.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "GameControls.hpp"
#include "InputManager.hpp"
#include "Platform/Platform.hpp"
#include "Player.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Objects/Campfire.hpp"
#include "Scene/Objects/Props.hpp"
#include "Physics/RigidBody.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

namespace flex
{
	namespace
	{
		const real NEARBY_PLANT_EXTENT = 10.0f; // Half width of the box plants are looked for in around the player
		const real CAMPFIRE_PLACE_REACH = 7.0f;
		const real CAMPFIRE_LIGHT_DISTANCE = 2.8f;

		// Distance along the ray to where it enters a vertical cylinder (or -1 on a miss)
		real RayCylinder(const glm::vec3& origin, const glm::vec3& dir, const glm::vec3& base, real radius, real yMin, real yMax)
		{
			const glm::vec2 o(origin.x - base.x, origin.z - base.z);
			const glm::vec2 d(dir.x, dir.z);
			const real a = glm::dot(d, d);
			const real c = glm::dot(o, o) - radius * radius;
			real t;
			if (c <= 0.0f)
			{
				// Starting inside
				t = 0.0f;
			}
			else
			{
				if (a < 1e-8f)
				{
					return -1.0f;
				}
				const real b = 2.0f * glm::dot(o, d);
				const real discriminant = b * b - 4.0f * a * c;
				if (discriminant < 0.0f)
				{
					return -1.0f;
				}
				t = (-b - glm::sqrt(discriminant)) / (2.0f * a);
				if (t < 0.0f)
				{
					return -1.0f;
				}
			}
			const real y = origin.y + dir.y * t;
			return (y >= yMin && y <= yMax) ? t : -1.0f;
		}

		// Untextured screen space quad (pixel coordinates, from the top left)
		void DrawQuad(const glm::vec2& centerPx, const glm::vec2& halfSizePx, const glm::vec4& colour)
		{
			const glm::vec2 screenSize = glm::vec2(g_Window->GetSize());
			const real aspectRatio = screenSize.x / screenSize.y;
			SpriteQuadDrawInfo drawInfo = {};
			drawInfo.anchor = AnchorPoint::CENTER;
			drawInfo.bScreenSpace = true;
			drawInfo.bReadDepth = false;
			drawInfo.bEnableAlbedoSampler = false;
			drawInfo.zOrder = 90;
			drawInfo.colour = colour;
			drawInfo.pos = glm::vec3((centerPx.x / screenSize.x * 2.0f - 1.0f) * aspectRatio, 1.0f - centerPx.y / screenSize.y * 2.0f, 0.0f);
			drawInfo.scale = glm::vec3(halfSizePx.x / screenSize.y * 2.0f, halfSizePx.y / screenSize.y * 2.0f, 1.0f);
			g_Renderer->EnqueueSprite(drawInfo);
		}

		// A progress bar just below the prompts (which are drawn around the middle of the screen)
		void DrawProgressBar(real progress, const glm::vec4& colour)
		{
			const glm::vec2 screenSize = glm::vec2(g_Window->GetSize());
			const glm::vec2 center(screenSize.x * 0.5f, screenSize.y * 0.5f + screenSize.y * 0.085f);
			const glm::vec2 halfSize(screenSize.y * 0.09f, screenSize.y * 0.006f);
			DrawQuad(center, halfSize + glm::vec2(2.0f), glm::vec4(0.0f, 0.0f, 0.0f, 0.6f));
			const real filled = glm::clamp(progress, 0.0f, 1.0f);
			if (filled > 0.0f)
			{
				DrawQuad(glm::vec2(center.x - halfSize.x * (1.0f - filled), center.y), glm::vec2(halfSize.x * filled, halfSize.y), colour);
			}
		}

		void DrawPrompt(const std::string& text, real yOffset = -0.08f)
		{
			g_Renderer->SetFont(SID("editor-02"));
			g_Renderer->DrawStringSS(text, glm::vec4(1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, yOffset), 1.5f, 0.6f);
		}
	} // anonymous namespace

	void WoodcraftController::Initialize(Player* player)
	{
		m_Player = player;
		m_TestStep = getenv("FLEX_TEST_WOODCRAFT") != nullptr ? 0 : -1;
	}

	void WoodcraftController::Destroy()
	{
		m_Player = nullptr;
	}

	bool WoodcraftController::CanAct() const
	{
		return m_Player->m_bPossessed &&
			m_Player->AbleToInteract() &&
			!m_Player->IsAnyInventoryShowing() &&
			!m_Player->GetRidingVehicleID().IsValid() &&
			m_Player->GetTrackRidingID() == InvalidTrackID &&
			!m_Player->IsBuilding() &&
			!m_Player->IsPlacingTrack() &&
			!m_Player->IsEditingTrack() &&
			!g_EngineInstance->IsSimulationPaused() &&
			g_CameraManager->CurrentCamera() != nullptr;
	}

	void WoodcraftController::Update()
	{
		if (m_TestStep >= 0)
		{
			UpdateTest();
		}

		harvest::Tick(g_DeltaTime);
		const bool bCanAct = CanAct();
		m_ChopCooldown -= g_DeltaTime;
		m_TimeSinceChop += g_DeltaTime;

		if (!bCanAct || TerrainGenerator::GetActive() == nullptr)
		{
			m_bHasTarget = false;
			m_bHasHarvestTarget = false;
			m_bPlacingCampfire = m_bPlacingCampfire && bCanAct;
			m_CampfirePreview.Hide();
			m_NearbyCampfireID = InvalidGameObjectID;
			m_LightingProgress = 0.0f;
			return;
		}

		UpdateCampfirePlacement(bCanAct);
		UpdateLighting(bCanAct);

		if (m_bPlacingCampfire)
		{
			m_bHasTarget = false;
			m_bHasHarvestTarget = false;
		}
		else
		{
			RefreshNearbyPlants(false);
			UpdateTarget();
			UpdateChopping(bCanAct);
			UpdateHarvest(bCanAct);
		}
	}

	void WoodcraftController::RefreshNearbyPlants(bool bForce)
	{
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		const glm::vec3 pos = m_Player->GetTransform()->GetWorldPosition();
		m_NearbyQueryAge += g_DeltaTime;
		const real moved = glm::distance(glm::vec2(pos.x, pos.z), glm::vec2(m_NearbyQueryPos.x, m_NearbyQueryPos.z));
		if (!bForce && moved < 2.0f && glm::abs(pos.y - m_NearbyQueryPos.y) < 3.0f && m_NearbyQueryAge < 3.0f)
		{
			return;
		}

		PROFILE_AUTO("Woodcraft find plants");
		m_NearbyQueryPos = pos;
		m_NearbyQueryAge = 0.0f;
		m_NearbyPlants.clear();
		m_NearbyBushes.clear();
		const glm::vec3 extent(NEARBY_PLANT_EXTENT, 16.0f, NEARBY_PLANT_EXTENT);
		terrainGenerator->FindPlants(pos - extent, pos + extent, m_NearbyPlants);
		terrainGenerator->FindBerryBushes(pos - extent, pos + extent, m_NearbyBushes);
	}

	void WoodcraftController::UpdateTarget()
	{
		m_bHasTarget = false;

		BaseCamera* camera = g_CameraManager->CurrentCamera();
		const glm::vec3 origin = camera->position;
		const glm::vec3 dir = camera->forward;
		real closest = FLT_MAX;
		for (const terrain::PlantInfo& plant : m_NearbyPlants)
		{
			// A little generous, so thin trunks are easy to aim at
			const real radius = plant.trunkRadius * 1.15f + 0.08f;
			const real t = RayCylinder(origin, dir, plant.base, radius, plant.base.y - 0.5f, plant.base.y + plant.trunkHeight);
			if (t >= 0.0f && t < closest && t <= CHOP_REACH + radius)
			{
				closest = t;
				m_Target = plant;
				m_bHasTarget = true;
			}
		}

		if (!m_bHasTarget)
		{
			return;
		}

		// Not through the ground
		glm::vec3 terrainHit, terrainNormal;
		if (TerrainGenerator::GetActive()->Raycast(origin, dir, closest, terrainHit, terrainNormal) &&
			glm::distance(origin, terrainHit) < closest - 0.2f)
		{
			m_bHasTarget = false;
			return;
		}

		m_TargetHitPos = origin + dir * closest;
		glm::vec3 out = m_TargetHitPos - m_Target.base;
		out.y = 0.0f;
		m_TargetHitNormal = glm::length(out) > 1e-4f ? glm::normalize(out) : -dir;
	}

	i32 WoodcraftController::HitsToFell(const terrain::PlantInfo& plant) const
	{
		// (Burnt trees are brittle)
		const i32 hits = plant.kind == terrain::PlantKind::CACTUS || plant.kind == terrain::PlantKind::SNAG ? 2 : 3 + (i32)(plant.size01 * 2.99f);
		// Much quicker with a tool, & quickest with an axe
		if (m_Player->IsWieldingAxe())
		{
			return glm::max((hits + 1) / 2, 1);
		}
		return m_Player->GetWieldedToolType() != InvalidStringID ? hits : hits * 2;
	}

	void WoodcraftController::UpdateChopping(bool bCanAct)
	{
		if (m_TimeSinceChop > 12.0f)
		{
			// Forget about trees left half chopped
			m_ChopHits = 0;
			m_ChopPlantHash = 0;
		}

		if (!m_bHasTarget || !bCanAct)
		{
			return;
		}

		if (m_Target.hash != m_ChopPlantHash)
		{
			m_ChopPlantHash = m_Target.hash;
			m_ChopHits = 0;
		}

		if (!g_InputManager->IsMouseButtonDown(MouseButton::LEFT) || m_ChopCooldown > 0.0f)
		{
			return;
		}

		Chop();
	}

	void WoodcraftController::Chop()
	{
		if (!m_bHasTarget)
		{
			return;
		}
		if (m_Target.hash != m_ChopPlantHash)
		{
			m_ChopPlantHash = m_Target.hash;
			m_ChopHits = 0;
		}

		m_ChopCooldown = m_Player->IsWieldingAxe() ? 0.4f : 0.45f;
		m_TimeSinceChop = 0.0f;
		++m_ChopHits;
		m_Player->SwingTool(m_ChopCooldown * 0.9f);

		const glm::vec3 chipColour = m_Target.kind == terrain::PlantKind::CACTUS ? glm::vec3(0.4f, 0.55f, 0.3f) :
			m_Target.kind == terrain::PlantKind::SNAG ? glm::vec3(0.09f, 0.08f, 0.075f) : glm::vec3(0.7f, 0.55f, 0.35f);
		if (WoodcraftEffects* effects = WoodcraftEffects::Get())
		{
			effects->SpawnChips(m_TargetHitPos, m_TargetHitNormal, 8, chipColour);
		}
		GameSounds::PlayChop(m_TargetHitPos);

		if (m_ChopHits >= HitsToFell(m_Target))
		{
			BaseCamera* camera = g_CameraManager->CurrentCamera();
			glm::vec3 fallDirection(camera->forward.x, 0.0f, camera->forward.z);
			TerrainGenerator::GetActive()->FellPlant(m_Target, fallDirection);
			m_ChopHits = 0;
			m_ChopPlantHash = 0;
			m_bHasTarget = false;
			m_ChopCooldown = 0.8f;
			RefreshNearbyPlants(true);
		}
	}

	void WoodcraftController::UpdateHarvest(bool bCanAct)
	{
		m_bHasHarvestTarget = false;
		BaseCamera* camera = g_CameraManager->CurrentCamera();
		if (!bCanAct || camera == nullptr)
		{
			return;
		}

		const glm::vec3 origin = camera->position;
		const glm::vec3 dir = camera->forward;
		real closest = FLT_MAX;
		// Palm trees (their trunk is the chop target)
		if (m_bHasTarget && m_Target.species == terrain::PlantSpecies::PALM)
		{
			closest = glm::distance(origin, m_TargetHitPos);
			m_bHasHarvestTarget = true;
			m_HarvestKind = harvest::Kind::COCONUTS;
			m_HarvestHash = m_Target.hash;
			m_HarvestPos = m_Target.base + VEC3_UP * m_Target.trunkHeight * 0.9f;
		}

		// Berry bushes
		for (const terrain::BerryBushInfo& bush : m_NearbyBushes)
		{
			const glm::vec3 center = bush.base + VEC3_UP * (bush.radius * 0.6f);
			const glm::vec3 oc = origin - center;
			const real b = glm::dot(oc, dir);
			const real cc = glm::dot(oc, oc) - bush.radius * bush.radius;
			const real disc = b * b - cc;
			if (disc < 0.0f)
			{
				continue;
			}
			const real t = glm::max(-b - glm::sqrt(disc), 0.0f);
			if (t < closest && t <= CHOP_REACH)
			{
				closest = t;
				m_bHasHarvestTarget = true;
				m_HarvestKind = harvest::Kind::BERRIES;
				m_HarvestHash = bush.hash;
				m_HarvestPos = center;
			}
		}

		if (!m_bHasHarvestTarget)
		{
			return;
		}

		if (g_InputManager->GetKeyPressed(GameKeys::LIGHT_CAMPFIRE) && harvest::IsReady(m_HarvestKind, m_HarvestHash))
		{
			const char* prefabName = harvest::ItemPrefabName(m_HarvestKind);
			const PrefabID itemID = g_ResourceManager->GetPrefabID(prefabName);
			const i32 count = harvest::GetYield(m_HarvestKind, m_HarvestHash);
			if (itemID.IsValid())
			{
				harvest::MarkPicked(m_HarvestKind, m_HarvestHash);
				m_Player->AddToInventory(itemID, count);
				GameSounds::PlayPlace(m_HarvestPos);
				m_Player->ShowNotification(LOCF(Notify_Picked, count, Loc::ItemName(prefabName)));
			}
		}
	}

	void WoodcraftController::UpdateCampfirePlacement(bool bCanAct)
	{
		// (hidden unless it's shown below)
		PlacementPreview::Frame previewFrame(m_CampfirePreview);

		const PrefabID woodID = woodcraft::GetWoodPrefabID();
		const i32 wood = woodID.IsValid() ? CountItems(woodID) : 0;

		if (!m_bCampfireHintShown && wood >= Campfire::WOOD_COST)
		{
			m_bCampfireHintShown = true;
			m_Player->ShowNotification(LOC(Notify_CampfireReady));
		}

		if (g_InputManager->GetKeyPressed(GameKeys::CAMPFIRE))
		{
			if (m_bPlacingCampfire)
			{
				m_bPlacingCampfire = false;
			}
			else if (wood >= Campfire::WOOD_COST)
			{
				m_bPlacingCampfire = true;
			}
			else
			{
				m_Player->ShowNotification(LOCF(Notify_CampfireNeedsWood, (i32)Campfire::WOOD_COST, (i32)wood));
			}
		}

		if (!m_bPlacingCampfire)
		{
			return;
		}
		if (!bCanAct || wood < Campfire::WOOD_COST || g_InputManager->IsMouseButtonPressed(MouseButton::RIGHT))
		{
			m_bPlacingCampfire = false;
			return;
		}

		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		BaseCamera* camera = g_CameraManager->CurrentCamera();
		glm::vec3 hitPos, hitNormal;
		m_bPlacementValid = false;
		if (terrainGenerator->Raycast(camera->position, camera->forward, CAMPFIRE_PLACE_REACH, hitPos, hitNormal))
		{
			m_PlacementPos = hitPos;
			const terrain::ColumnSample column = terrainGenerator->SampleColumn(hitPos.x, hitPos.z);
			const bool bUnderWater = column.lakeID != 0 && column.lakeT < 1.0f && hitPos.y < column.waterLevel + 0.2f;
			const Campfire* nearest = Campfire::FindNearest(hitPos, 2.2f);
			// (averaged over the campfire's footprint, so small bumps don't make it wobble)
			const glm::vec3 groundNormal = terrainGenerator->SampleSmoothNormal(hitPos, 0.8f);
			m_bPlacementValid = hitNormal.y > 0.75f && groundNormal.y > 0.75f && !bUnderWater && nearest == nullptr;

			// Hologram of the campfire, lying on the ground
			const real yaw = std::atan2(camera->forward.x, camera->forward.z);
			const glm::vec3 forward(std::sin(yaw), 0.0f, std::cos(yaw));
			m_CampfirePreview.Show(SID("campfire"), Campfire::BuildPreviewGeometry, hitPos, RotationFromUp(groundNormal, forward), VEC3_ONE,
				PlacementPreview::ValidColour(m_bPlacementValid));
		}

		if (m_bPlacementValid && g_InputManager->IsMouseButtonPressed(MouseButton::LEFT))
		{
			if (RemoveItems(woodID, Campfire::WOOD_COST))
			{
				const real yaw = std::atan2(camera->forward.x, camera->forward.z);
				BuildCampfire(m_PlacementPos, yaw);
				m_Player->ShowNotification(LOC(Notify_CampfireBuilt));
			}
			m_bPlacingCampfire = false;
		}
	}

	Campfire* WoodcraftController::BuildCampfire(const glm::vec3& pos, real yaw)
	{
		BaseScene* scene = g_SceneManager->CurrentScene();
		Campfire* campfire = new Campfire(scene->GetUniqueObjectName("Campfire_", 3));
		campfire->GetTransform()->SetWorldPosition(pos);
		campfire->GetTransform()->SetWorldRotation(glm::angleAxis(yaw, VEC3_UP));
		scene->AddRootObject(campfire);
		GameSounds::PlayPlace(pos);
		return campfire;
	}

	void WoodcraftController::UpdateLighting(bool bCanAct)
	{
		const glm::vec3 playerPos = m_Player->GetTransform()->GetWorldPosition();
		// (from the player's feet)
		Campfire* campfire = Campfire::FindNearest(playerPos - VEC3_UP * (m_Player->GetHeight() * 0.5f), CAMPFIRE_LIGHT_DISTANCE);
		m_NearbyCampfireID = campfire != nullptr ? campfire->ID : InvalidGameObjectID;

		if (campfire == nullptr || !bCanAct || campfire->GetState() != Campfire::State::UNLIT || m_bPlacingCampfire)
		{
			m_LightingProgress = 0.0f;
			return;
		}

		if (g_InputManager->GetKeyDown(GameKeys::LIGHT_CAMPFIRE))
		{
			m_LightingProgress += g_DeltaTime;
			if (m_LightingProgress >= Campfire::LIGHT_HOLD_DURATION)
			{
				m_LightingProgress = 0.0f;
				campfire->Light();
				m_Player->ShowNotification(LOCF(Notify_CampfireLit, (i32)(Campfire::BURN_DURATION / 60.0f)));
			}
		}
		else
		{
			m_LightingProgress = 0.0f;
		}
	}

	void WoodcraftController::DrawHUD() const
	{
		if (m_Player == nullptr || !m_Player->m_bPossessed || !CanAct())
		{
			return;
		}

		if (m_bPlacingCampfire)
		{
			DrawPrompt(m_bPlacementValid ? LOCF(Woodcraft_BuildCampfire, (i32)Campfire::WOOD_COST) : std::string(LOC(Woodcraft_FindGround)));
			return;
		}

		if (m_bHasHarvestTarget && !m_bHasTarget)
		{
			const sec wait = harvest::TimeUntilReady(m_HarvestKind, m_HarvestHash);
			if (wait > 0.0f)
			{
				DrawPrompt(LOCF(Woodcraft_PickedClean, (i32)glm::ceil(wait / 60.0f)));
			}
			else
			{
				DrawPrompt(m_HarvestKind == harvest::Kind::BERRIES ? LOC(Woodcraft_PickBerries) : LOC(Woodcraft_PickCoconuts));
			}
			return;
		}

		if (m_bHasTarget)
		{
			const i32 hitsNeeded = HitsToFell(m_Target);
			const i32 hits = m_Target.hash == m_ChopPlantHash ? m_ChopHits : 0;
			std::string prompt = m_Target.kind == terrain::PlantKind::CACTUS ? LOC(Woodcraft_ChopCactus) :
				m_Target.kind == terrain::PlantKind::SNAG ? LOC(Woodcraft_ChopBurntTree) : LOC(Woodcraft_ChopTree);
			if (!m_Player->IsWieldingAxe())
			{
				prompt += LOC(Woodcraft_QuickerWithAxe);
			}
			if (m_bHasHarvestTarget && harvest::IsReady(m_HarvestKind, m_HarvestHash))
			{
				prompt += std::string("   ") + LOC(Woodcraft_PickCoconuts);
			}
			DrawPrompt(prompt);
			if (hits > 0)
			{
				DrawProgressBar((real)hits / (real)hitsNeeded, glm::vec4(0.85f, 0.62f, 0.32f, 0.95f));
			}
			return;
		}

		Campfire* campfire = (Campfire*)m_NearbyCampfireID.Get();
		if (campfire != nullptr)
		{
			switch (campfire->GetState())
			{
			case Campfire::State::UNLIT:
				DrawPrompt(LOC(Woodcraft_LightCampfire));
				if (m_LightingProgress > 0.0f)
				{
					DrawProgressBar(m_LightingProgress / Campfire::LIGHT_HOLD_DURATION, glm::vec4(1.0f, 0.55f, 0.15f, 0.95f));
				}
				break;
			case Campfire::State::LIT:
			{
				const i32 seconds = (i32)campfire->GetBurnTimeRemaining();
				DrawPrompt(LOCF(Woodcraft_CampfireBurning, seconds / 60, seconds % 60));
			} break;
			default:
				break;
			}
		}
	}

	i32 WoodcraftController::CountItems(const PrefabID& prefabID) const
	{
		i32 count = 0;
		for (const GameObjectStack& stack : m_Player->m_Inventory)
		{
			if (stack.count > 0 && stack.prefabID == prefabID)
			{
				count += stack.count;
			}
		}
		for (const GameObjectStack& stack : m_Player->m_QuickAccessInventory)
		{
			if (stack.count > 0 && stack.prefabID == prefabID)
			{
				count += stack.count;
			}
		}
		return count;
	}

	bool WoodcraftController::RemoveItems(const PrefabID& prefabID, i32 count)
	{
		if (CountItems(prefabID) < count)
		{
			return false;
		}

		// From the main inventory first, keeping the quick access slots stocked
		auto RemoveFrom = [&](GameObjectStack* stacks, u32 stackCount)
		{
			for (u32 i = 0; i < stackCount && count > 0; ++i)
			{
				GameObjectStack& stack = stacks[i];
				if (stack.count > 0 && stack.prefabID == prefabID)
				{
					const i32 taken = glm::min(stack.count, count);
					stack.count -= taken;
					count -= taken;
					if (stack.count == 0)
					{
						stack.Clear();
					}
				}
			}
		};
		RemoveFrom(m_Player->m_Inventory.data(), (u32)m_Player->m_Inventory.size());
		RemoveFrom(m_Player->m_QuickAccessInventory.data(), (u32)m_Player->m_QuickAccessInventory.size());
		return true;
	}

	bool WoodcraftController::FellNearestPlant(real maxDistance, glm::vec3* outPlantPos /* = nullptr */)
	{
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (terrainGenerator == nullptr)
		{
			return false;
		}

		const glm::vec3 pos = m_Player->GetTransform()->GetWorldPosition();
		std::vector<terrain::PlantInfo> plants;
		const glm::vec3 extent(maxDistance, 40.0f, maxDistance);
		terrainGenerator->FindPlants(pos - extent, pos + extent, plants);
		const terrain::PlantInfo* nearest = nullptr;
		real nearestDistSq = maxDistance * maxDistance;
		for (const terrain::PlantInfo& plant : plants)
		{
			const real distSq = glm::distance2(glm::vec2(plant.base.x, plant.base.z), glm::vec2(pos.x, pos.z));
			if (plant.kind != terrain::PlantKind::CACTUS && distSq < nearestDistSq)
			{
				nearestDistSq = distSq;
				nearest = &plant;
			}
		}
		if (nearest == nullptr)
		{
			return false;
		}

		if (outPlantPos != nullptr)
		{
			*outPlantPos = nearest->base;
		}
		glm::vec3 away = nearest->base - pos;
		away.y = 0.0f;
		terrainGenerator->FellPlant(*nearest, away);
		RefreshNearbyPlants(true);
		return true;
	}

	void WoodcraftController::UpdateTest()
	{
		BaseScene* scene = g_SceneManager->CurrentScene();
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (!m_Player->m_bPossessed || terrainGenerator == nullptr || m_Player->IsAwaitingTerrainCollision() || !terrainGenerator->IsLoadComplete())
		{
			m_TestTime = 0.0f;
			return;
		}
		m_TestTime += g_UnpausedDeltaTime;

		auto Screenshot = [](const char* name)
		{
			const std::string directory = RelativePathToAbsolute(SCREENSHOT_DIRECTORY);
			Platform::CreateDirectoryRecursive(directory);
			g_Renderer->RequestScreenshot(directory + "woodcraft_" + name + ".png");
		};
		auto Next = [this]() { ++m_TestStep; m_TestTime = 0.0f; };

		// How long after the tree falls the first wood lands in the inventory (the player stands beside the tree)
		if (m_TestSinceFelled >= 0.0f)
		{
			m_TestSinceFelled += g_UnpausedDeltaTime;
			const PrefabID woodPrefabID = woodcraft::GetWoodPrefabID();
			if (CountItems(woodPrefabID) > m_TestWoodBeforeFelling)
			{
				Print("Woodcraft test: first wood picked up %.2fs after felling\n", m_TestSinceFelled);
				m_TestSinceFelled = -1.0f;
			}
			else if (m_TestSinceFelled > 15.0f)
			{
				Print("Woodcraft test: no wood picked up within 15s of felling\n");
				m_TestSinceFelled = -1.0f;
			}
		}
		auto Aim = [this](const glm::vec3& target)
		{
			const glm::vec3 delta = target - m_Player->GetEyePosition();
			m_Player->GetTransform()->SetWorldRotation(glm::angleAxis(std::atan2(delta.x, delta.z), VEC3_UP));
			m_Player->SetPitch(-std::atan2(delta.y, glm::length(glm::vec2(delta.x, delta.z))));
		};
		// Nearest tree (not a cactus) to pos, searching further & further out
		auto FindTree = [terrainGenerator](const glm::vec3& pos, u32 skipHash, terrain::PlantInfo& outPlant)
		{
			for (real extent : { 30.0f, 80.0f, 200.0f, 400.0f })
			{
				std::vector<terrain::PlantInfo> plants;
				terrainGenerator->FindPlants(pos - glm::vec3(extent, 300.0f, extent), pos + glm::vec3(extent, 300.0f, extent), plants);
				real best = FLT_MAX;
				for (const terrain::PlantInfo& plant : plants)
				{
					const real dist = glm::distance(plant.base, pos);
					// (away from water, so the player can stand around it)
					const terrain::ColumnSample column = terrainGenerator->SampleColumn(plant.base.x, plant.base.z);
					const bool bNearWater = column.lakeID != 0 && column.lakeT < 1.6f;
					if (plant.kind != terrain::PlantKind::CACTUS && plant.hash != skipHash && !bNearWater && dist < best)
					{
						best = dist;
						outPlant = plant;
					}
				}
				if (best < FLT_MAX)
				{
					return true;
				}
			}
			return false;
		};
		auto PlantExists = [terrainGenerator](const terrain::PlantInfo& plant)
		{
			std::vector<terrain::PlantInfo> plants;
			terrainGenerator->FindPlants(plant.base - glm::vec3(1.0f, 10.0f, 1.0f), plant.base + glm::vec3(1.0f, 10.0f, 1.0f), plants);
			for (const terrain::PlantInfo& other : plants)
			{
				if (other.hash == plant.hash)
				{
					return true;
				}
			}
			return false;
		};

		const PrefabID woodID = woodcraft::GetWoodPrefabID();
		switch (m_TestStep)
		{
		case 0:
			if (m_TestTime > 2.0f)
			{
				// (campfires saved in the world by an earlier run)
				for (Campfire* campfire : Campfire::GetAll())
				{
					const glm::vec3 pos = campfire->GetTransform()->GetWorldPosition();
					Print("Woodcraft test: existing campfire at (%.1f, %.1f, %.1f), state %d, %.0fs left\n", pos.x, pos.y, pos.z,
						(i32)campfire->GetState(), campfire->GetBurnTimeRemaining());
				}
				if (!FindTree(m_Player->GetTransform()->GetWorldPosition(), 0, m_TestPlant))
				{
					PrintError("Woodcraft test: FAIL, no trees nearby\n");
					m_TestStep = 100;
					break;
				}
				Print("Woodcraft test: tree at (%.1f, %.1f, %.1f), height %.1f\n", m_TestPlant.base.x, m_TestPlant.base.y, m_TestPlant.base.z, m_TestPlant.height);
				m_Player->TeleportOnTerrain(m_TestPlant.base + glm::vec3(2.5f, 1.5f, 1.2f));
				Next();
			}
			break;
		case 1:
			if (m_TestTime > 3.0f)
			{
				Aim(m_TestPlant.base + VEC3_UP * 1.0f);
				Next();
			}
			break;
		case 2:
			Aim(m_TestPlant.base + VEC3_UP * 1.0f);
			if (m_TestTime > 1.0f)
			{
				Print("Woodcraft test: aiming at the tree: %s (%d hits to fell)\n", m_bHasTarget && m_Target.hash == m_TestPlant.hash ? "PASS" : "FAIL", HitsToFell(m_TestPlant));
				Screenshot("1_tree");
				Next();
			}
			break;
		case 3:
			// Chop until it falls
			Aim(m_TestPlant.base + VEC3_UP * 1.0f);
			if (m_ChopCooldown <= 0.0f && m_bHasTarget)
			{
				Chop();
				if (m_ChopHits == 2)
				{
					Screenshot("2_chopping");
				}
			}
			if (!PlantExists(m_TestPlant))
			{
				Print("Woodcraft test: felled the tree after %.1fs\n", m_TestTime);
				m_TestSinceFelled = 0.0f;
				m_TestWoodBeforeFelling = CountItems(woodID);
				Next();
			}
			else if (m_TestTime > 20.0f)
			{
				PrintError("Woodcraft test: FAIL, couldn't chop the tree down\n");
				terrainGenerator->FellPlant(m_TestPlant, VEC3_ZERO);
				Next();
			}
			break;
		case 4:
			if (m_TestTime > 1.0f)
			{
				Screenshot("3_falling");
				Next();
			}
			break;
		case 5:
			if (m_TestTime > 3.5f)
			{
				Screenshot("4_felled");
				Next();
			}
			break;
		case 6:
			// Walk over to each piece of wood to pick it up
			if (m_TestTime > 3.0f)
			{
				std::vector<DroppedItem*> items;
				scene->GetDroppedItemsInRadius(m_TestPlant.base, 30.0f, items);
				for (DroppedItem* item : items)
				{
					if (item->prefabID == woodID && item->CanBePickedUp())
					{
						const glm::vec3 pos = item->GetTransform()->GetWorldPosition() + VEC3_UP * 0.5f;
						m_Player->GetRigidBody()->SetWorldPosition(pos);
						m_Player->GetTransform()->SetWorldPosition(pos);
						break;
					}
				}
			}
			if (m_TestTime > 9.0f)
			{
				const i32 wood = CountItems(woodID);
				Print("Woodcraft test: picked up %d wood (expected %d): %s\n", wood, woodcraft::WoodYield(m_TestPlant), wood > 0 ? "PASS" : "FAIL");
				if (wood < Campfire::WOOD_COST)
				{
					m_Player->AddToInventory(woodID, Campfire::WOOD_COST - wood);
				}
				Next();
			}
			break;
		case 7:
		{
			// Build a campfire where the tree stood, & stand back from it
			const glm::vec3 heading(0.0f, 0.0f, 1.0f);
			glm::vec3 hitPos, hitNormal;
			if (terrainGenerator->Raycast(m_TestPlant.base + VEC3_UP * 3.0f, -VEC3_UP, 10.0f, hitPos, hitNormal))
			{
				const glm::vec3 standPos = hitPos - heading * 3.5f + VEC3_UP * 1.5f;
				m_Player->GetRigidBody()->SetWorldPosition(standPos);
				m_Player->GetTransform()->SetWorldPosition(standPos);
				const i32 woodBefore = CountItems(woodID);
				if (RemoveItems(woodID, Campfire::WOOD_COST))
				{
					Campfire* campfire = BuildCampfire(hitPos, std::atan2(heading.x, heading.z));
					m_TestCampfireID = campfire->ID;
					Print("Woodcraft test: built a campfire, wood %d -> %d\n", woodBefore, CountItems(woodID));
				}
				m_TestCampfirePos = hitPos;
			}
			Next();
		} break;
		case 8:
			Aim(m_TestCampfirePos + VEC3_UP * 0.4f);
			if (m_TestTime > 1.5f)
			{
				Screenshot("5_campfire");
				Next();
			}
			break;
		case 9:
		{
			// Light it (as if [E] were held), at night
			Campfire* campfire = (Campfire*)m_TestCampfireID.Get();
			if (campfire != nullptr)
			{
				m_LightingProgress = Campfire::LIGHT_HOLD_DURATION;
				campfire->Light();
			}
			scene->SetTimeOfDay(0.47f);
			scene->SetTimeOfDayPaused(true);
			Next();
		} break;
		case 10:
			Aim(m_TestCampfirePos + VEC3_UP * 0.4f);
			if (m_TestTime > 3.0f)
			{
				Screenshot("6_lit_night");
				Next();
			}
			break;
		case 11:
			if (m_TestTime > 0.37f)
			{
				// Flicker check: another frame a moment later
				Screenshot("6b_lit_night");
				scene->SetTimeOfDay(0.08f);
				Next();
			}
			break;
		case 12:
			if (m_TestTime > 2.0f)
			{
				Screenshot("7_lit_day");
				Campfire* campfire = (Campfire*)m_TestCampfireID.Get();
				if (campfire != nullptr)
				{
					campfire->SetBurnTimeRemaining(0.5f);
				}
				Next();
			}
			break;
		case 13:
			Aim(m_TestCampfirePos);
			if (m_TestTime > 3.0f)
			{
				Campfire* campfire = (Campfire*)m_TestCampfireID.Get();
				Print("Woodcraft test: campfire burnt out: %s\n", campfire != nullptr && campfire->GetState() == Campfire::State::BURNT ? "PASS" : "FAIL");
				Screenshot("8_burnt");
				Next();
			}
			break;
		case 14:
		{
			// Dig out the ground under another tree
			terrain::PlantInfo second;
			if (!FindTree(m_Player->GetTransform()->GetWorldPosition(), m_TestPlant.hash, second))
			{
				PrintError("Woodcraft test: no second tree to dig under\n");
				m_TestStep = 100;
				break;
			}
			m_TestPlant = second;
			m_Player->TeleportOnTerrain(second.base + glm::vec3(4.0f, 1.5f, 0.0f));
			Next();
		} break;
		case 15:
			if (m_TestTime > 3.0f)
			{
				Aim(m_TestPlant.base + VEC3_UP * 2.0f);
				terrainGenerator->Dig(m_TestPlant.base + glm::vec3(0.6f, -0.6f, 0.0f), 1.6f);
				Next();
			}
			break;
		case 16:
			if (m_TestTime > 1.0f)
			{
				Print("Woodcraft test: digging under a tree felled it: %s\n", PlantExists(m_TestPlant) ? "FAIL" : "PASS");
				Screenshot("9_dug");
				Next();
			}
			break;
		case 17:
			if (m_TestTime > 4.0f)
			{
				Screenshot("10_dug_after");
				// Held items & tools (the tools are given to new worlds)
				for (const char* toolName : { "pickaxe", "axe", "shovel" })
				{
					const PrefabID toolID = g_ResourceManager->GetPrefabID(toolName);
					if (toolID.IsValid() && CountItems(toolID) == 0)
					{
						m_Player->AddToInventory(toolID, 1);
					}
				}
				if (CountItems(woodID) == 0)
				{
					m_Player->AddToInventory(woodID, 3);
				}
				Next();
			}
			break;
		case 18:
		case 19:
		case 20:
		case 21:
		{
			// Hold each item in turn, looking ahead
			const char* itemNames[] = { "wood", "pickaxe", "axe", "shovel" };
			const char* itemName = itemNames[m_TestStep - 18];
			const PrefabID itemID = g_ResourceManager->GetPrefabID(itemName);
			for (i32 i = 0; i < (i32)Player::QUICK_ACCESS_ITEM_COUNT; ++i)
			{
				if (m_Player->m_QuickAccessInventory[i].prefabID == itemID && m_Player->m_QuickAccessInventory[i].count > 0)
				{
					m_Player->SetSelectedQuickAccessItemSlot(i);
					break;
				}
			}
			m_Player->SetPitch(glm::radians(10.0f));
			if (m_TestStep == 18)
			{
				// Wood, looking down (e.g. at the ground) & up
				if (m_TestTime > 1.5f && m_TestTime - g_UnpausedDeltaTime <= 1.5f)
				{
					Screenshot("11_holding_wood_level");
				}
				if (m_TestTime > 1.5f)
				{
					m_Player->SetPitch(glm::radians(50.0f));
				}
				if (m_TestTime > 3.0f && m_TestTime - g_UnpausedDeltaTime <= 3.0f)
				{
					Screenshot("11_holding_wood_down");
				}
				if (m_TestTime > 3.0f)
				{
					m_Player->SetPitch(glm::radians(-30.0f));
				}
				if (m_TestTime < 4.5f)
				{
					break;
				}
			}
			if (m_TestTime > 1.5f)
			{
				Screenshot((std::string("11_holding_") + itemName).c_str());
				Print("Woodcraft test: holding %s (wielded tool: %s)\n", itemName,
					m_Player->GetWieldedToolType() == InvalidStringID ? "none" : itemName);
				Next();
			}
		} break;
		case 22:
		case 23:
		case 24:
		{
			// Continuous digging (with the shovel, then the pickaxe) & filling (shovel)
			const bool bFillTest = m_TestStep == 24;
			const char* toolName = m_TestStep == 23 ? "pickaxe" : "shovel";
			const PrefabID toolID = g_ResourceManager->GetPrefabID(toolName);
			for (i32 i = 0; i < (i32)Player::QUICK_ACCESS_ITEM_COUNT; ++i)
			{
				if (m_Player->m_QuickAccessInventory[i].prefabID == toolID)
				{
					m_Player->SetSelectedQuickAccessItemSlot(i);
					break;
				}
			}
			m_Player->SetPitch(glm::radians(bFillTest ? 30.0f : 45.0f));
			if (m_TestEditCount == 0)
			{
				// Each tool digs fresh ground (holding dig at one spot tunnels along the view, to the edge of the reach)
				const glm::quat turn = glm::angleAxis(glm::radians(120.0f), VEC3_UP);
				m_Player->GetTransform()->SetWorldRotation(turn * m_Player->GetTransform()->GetWorldRotation());
			}
			if (m_TestEditCount == 0)
			{
				m_TestEditCount = (i32)terrainGenerator->GetEditCount() + 1;
				m_Player->m_TestTerrainStrokeCount = 0;
				m_Player->m_TestTerrainEditCount = 0;
				m_Player->m_TestTerrainWaitTime = 0.0f;
			}
			m_Player->m_TestTerrainAction = bFillTest ? 2 : 1;
			if (m_TestTime > 1.5f && m_TestTime - g_UnpausedDeltaTime <= 1.5f)
			{
				Screenshot((std::string(bFillTest ? "12_filling_" : "12_digging_") + toolName).c_str());
			}
			if (m_TestTime > 4.0f)
			{
				m_Player->m_TestTerrainAction = 0;
				Print("Woodcraft test: %s with the %s for %.1fs (ground %.0f%% rock): %u holes/mounds, %u terrain edits applied, "
					"%d saved edits added, %.2fs waiting for re-meshing\n",
					bFillTest ? "filled" : "dug", toolName, m_TestTime, m_Player->m_TerrainStroke.hardness * 100.0f,
					m_Player->m_TestTerrainStrokeCount, m_Player->m_TestTerrainEditCount,
					(i32)terrainGenerator->GetEditCount() - (m_TestEditCount - 1), m_Player->m_TestTerrainWaitTime);
				m_TestEditCount = 0;
				Next();
			}
		} break;
		case 25:
			if (m_TestTime > 2.0f)
			{
				Screenshot("13_dug_after");
				g_Game->SaveWorld();
				m_TestStep = 100;
			}
			break;
		case 100:
			if (m_TestTime > 1.0f)
			{
				Print("Woodcraft test: done\n");
				g_EngineInstance->Stop();
				m_TestStep = 101;
			}
			break;
		default:
			break;
		}
	}
} // namespace flex
