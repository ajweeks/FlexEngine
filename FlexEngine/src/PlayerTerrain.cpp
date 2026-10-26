#include "stdafx.hpp"

#include "Player.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletCollision/CollisionDispatch/btManifoldResult.h>

#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/norm.hpp> // For distance2
IGNORE_WARNINGS_POP

#include <queue>

#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "FlexEngine.hpp"
#include "Graphics/BitmapFont.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "InputManager.hpp"
#include "Inventory.hpp"
#include "Localization.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Objects/Industry.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/SceneManager.hpp"
#include "Systems/TrackManager.hpp"
#include "Traversal.hpp"
#include "Woodcraft.hpp"
#include "Window/Window.hpp"

// The player in streamed terrain: digging/filling with a pickaxe or shovel, & staying on top of the terrain
namespace flex
{
	namespace
	{
		// Seconds to dig out (or fill) a full sized hole/mound with each tool. Shovels are best in loose ground,
		// pickaxes in rock.
		struct ToolDigTimes
		{
			real loose;
			real rock;
			real fill;
		};

		bool GetToolDigTimes(StringID toolTypeID, ToolDigTimes& outTimes)
		{
			switch (toolTypeID)
			{
			case ShovelSID: outTimes = { 0.35f, 2.4f, 0.3f }; return true;
			case PickAxeSID: outTimes = { 0.8f, 0.65f, 0.5f }; return true;
			default: return false;
			}
		}

		std::string ToolName(StringID toolTypeID)
		{
			switch (toolTypeID)
			{
			case ShovelSID: return LOC(Item_Shovel);
			case PickAxeSID: return LOC(Item_Pickaxe);
			case AxeSID: return LOC(Item_Axe);
			default: return "";
			}
		}

		// New holes/mounds start at this fraction of their full size (so there's immediate feedback)
		const real STROKE_START_RADIUS = 0.35f;
		// How much a hole/mound must grow by (meters) before it's applied again (each application re-meshes chunks)
		const real STROKE_RADIUS_STEP = 0.1f;
		// At most this many edits are made per second, & only once the previous edit's chunks have been re-meshed
		const real MAX_TERRAIN_EDIT_RATE = 15.0f;
		// The aim moved this far (meters) off the hole/mound being worked on starts a new one
		const real STROKE_AIM_SLACK = 0.6f;
	} // anonymous namespace

	void Player::UpdateTerrainTool()
	{
		m_bTerrainToolActive = false;
		m_TimeSinceTerrainEdit += g_DeltaTime;

		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		const StringID toolTypeID = GetWieldedToolType();
		ToolDigTimes digTimes;
		if (terrainGenerator == nullptr ||
			!m_bPossessed ||
			!GetToolDigTimes(toolTypeID, digTimes) ||
			m_BuildMode.IsActive() ||
			IsAnyInventoryShowing() ||
			m_RidingVehicleID.IsValid() ||
			m_TrackRidingID != InvalidTrackID ||
			IsPlacingTrack() ||
			IsEditingTrack() ||
			g_EngineInstance->IsSimulationPaused() ||
			(m_Woodcraft != nullptr && m_Woodcraft->BlocksTerrainTool()))
		{
			EndTerrainStroke();
			return;
		}

		const i32 materialCount = (i32)ARRAY_LENGTH(terrain::FILL_MATERIALS);
		if (g_InputManager->GetKeyPressed(KeyCode::KEY_R))
		{
			const i32 step = g_InputManager->GetKeyDown(KeyCode::KEY_LEFT_SHIFT) != 0 ? -1 : 1;
			m_TerrainFillMaterialIndex = (m_TerrainFillMaterialIndex + step + materialCount) % materialCount;
			// (the mound being placed is of the previous material)
			EndTerrainStroke();
		}
		m_TerrainFillMaterialIndex = glm::clamp(m_TerrainFillMaterialIndex, 0, materialCount - 1);

		BaseCamera* camera = g_CameraManager->CurrentCamera();
		glm::vec3 hitPos, hitNormal;
		// Digging into the same spot again looks through the hole just dug, at its far side (up to a dig radius
		// further away). Surfaces a little past the reach are still dug into, with the brush held at the reach.
		const real rayLength = m_TerrainToolReach + m_TerrainDigRadius * 0.9f;
		if (!terrainGenerator->Raycast(camera->position, camera->forward, rayLength, hitPos, hitNormal))
		{
			EndTerrainStroke();
			return;
		}
		const real hitDistance = glm::distance(camera->position, hitPos);
		if (hitDistance > m_TerrainToolReach)
		{
			hitPos = camera->position + camera->forward * m_TerrainToolReach;
		}
		m_bTerrainToolActive = true;

		const bool bDig = g_InputManager->IsMouseButtonDown(MouseButton::LEFT) || m_TestTerrainAction == 1;
		const bool bFill = !bDig && (g_InputManager->IsMouseButtonDown(MouseButton::RIGHT) || m_TestTerrainAction == 2);

		if (m_TerrainStroke.bActive)
		{
			const bool bComplete = m_TerrainStroke.progress >= 1.0f && m_TerrainStroke.appliedRadius >= m_TerrainDigRadius - 1e-3f;
			const real currentRadius = glm::max(m_TerrainStroke.appliedRadius, m_TerrainDigRadius * STROKE_START_RADIUS);
			const bool bAimedAway = glm::distance(hitPos, GetTerrainStrokeCenter(currentRadius)) > currentRadius + STROKE_AIM_SLACK;
			if ((!bDig && !bFill) || m_TerrainStroke.bFill != bFill || bComplete || bAimedAway)
			{
				EndTerrainStroke();
			}
		}

		if (!bDig && !bFill)
		{
			// Where a hole would be dug
			g_Renderer->GetDebugRenderer()->drawSphere(ToBtVec3(hitPos), m_TerrainDigRadius * 0.98f, btVector3(1.0f, 0.6f, 0.15f));
			return;
		}

		if (!m_TerrainStroke.bActive)
		{
			m_TerrainStroke = {};
			m_TerrainStroke.bActive = true;
			m_TerrainStroke.bFill = bFill;
			m_TerrainStroke.anchor = hitPos;
			m_TerrainStroke.normal = hitNormal;
			m_TerrainStroke.hardness = bFill ? 0.0f : SampleTerrainHardness(hitPos, hitNormal);
			m_TerrainStroke.progress = STROKE_START_RADIUS * STROKE_START_RADIUS * STROKE_START_RADIUS;
		}

		// Volume grows at a constant rate (so the radius with its cube root)
		const real strokeTime = bFill ? digTimes.fill : Lerp(digTimes.loose, digTimes.rock, m_TerrainStroke.hardness);
		// (a long frame, e.g. a hitch, doesn't dig a whole hole at once)
		m_TerrainStroke.progress = glm::min(m_TerrainStroke.progress + glm::min(g_DeltaTime, 0.1f) / strokeTime, 1.0f);
		const real radius = m_TerrainDigRadius * std::cbrt(m_TerrainStroke.progress);
		const glm::vec3 center = GetTerrainStrokeCenter(radius);

		g_Renderer->GetDebugRenderer()->drawSphere(ToBtVec3(center), radius * 0.98f,
			bFill ? btVector3(0.3f, 0.8f, 1.0f) : btVector3(1.0f, 0.6f, 0.15f));

		if (m_ToolSwingTimer <= 0.0f)
		{
			SwingTool(glm::clamp(strokeTime * 0.4f, 0.25f, 0.45f));
		}

		// Rate limited, & waiting for the previous edit's chunks (restarting their re-meshing each frame could mean
		// they never finish)
		const bool bGrown = radius - m_TerrainStroke.appliedRadius >= STROKE_RADIUS_STEP ||
			(m_TerrainStroke.progress >= 1.0f && radius > m_TerrainStroke.appliedRadius);
		if (!bGrown || m_TimeSinceTerrainEdit < 1.0f / MAX_TERRAIN_EDIT_RATE || terrainGenerator->IsApplyingEdits())
		{
			if (bGrown)
			{
				m_TestTerrainWaitTime += g_DeltaTime;
			}
			return;
		}

		if (bFill)
		{
			// Don't bury ourselves (distance to the capsule's axis)
			const glm::vec3 playerPos = m_Transform.GetWorldPosition();
			const real axisHalfLength = m_Height * 0.5f - m_Radius;
			const glm::vec3 closestOnAxis = playerPos + VEC3_UP * glm::clamp(center.y - playerPos.y, -axisHalfLength, axisHalfLength);
			if (glm::distance(center, closestOnAxis) <= radius + m_Radius + 0.2f)
			{
				return;
			}
			terrainGenerator->Fill(center, radius, terrain::FILL_MATERIALS[m_TerrainFillMaterialIndex]);
		}
		else
		{
			// Small scraps left floating are removed once the hole is finished (see EndTerrainStroke)
			terrainGenerator->Dig(center, radius, false);
		}
		m_TerrainStroke.appliedRadius = radius;
		m_TimeSinceTerrainEdit = 0.0f;
		++m_TestTerrainEditCount;
	}

	void Player::EndTerrainStroke()
	{
		if (m_TerrainStroke.bActive && m_TerrainStroke.appliedRadius > 0.0f)
		{
			++m_TestTerrainStrokeCount;
		}
		if (m_TerrainStroke.bActive && !m_TerrainStroke.bFill && m_TerrainStroke.appliedRadius > 0.0f)
		{
			// Re-applying the same dig (which replaces the previous edit) also removes any scraps of terrain it left
			// floating
			if (TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive())
			{
				terrainGenerator->Dig(m_TerrainStroke.anchor, m_TerrainStroke.appliedRadius, true);
				m_TimeSinceTerrainEdit = 0.0f;
			}
		}
		m_TerrainStroke = {};
	}

	glm::vec3 Player::GetTerrainStrokeCenter(real radius) const
	{
		// Placed material sits on top of the surface. Each mound contains the previous (smaller) one, so they replace
		// each other as they grow (see terrain::EditStore::Add).
		return m_TerrainStroke.bFill ? m_TerrainStroke.anchor + m_TerrainStroke.normal * radius * 0.6f : m_TerrainStroke.anchor;
	}

	real Player::SampleTerrainHardness(const glm::vec3& pos, const glm::vec3& normal) const
	{
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		terrain::Material placedMaterial;
		if (terrainGenerator->FindPlacedMaterial(pos, placedMaterial))
		{
			switch (placedMaterial)
			{
			case terrain::Material::ROCK:
			case terrain::Material::GRANITE:
			case terrain::Material::SANDSTONE:
			case terrain::Material::MARBLE:
			case terrain::Material::BASALT:
				return 1.0f;
			case terrain::Material::CLAY:
				return 0.35f;
			default:
				// Soil, sand, gravel, snow & grass
				return 0.0f;
			}
		}

		// Generated terrain. On its surface cliffs are rock & the ground is soil/sand/gravel/grass/snow. Below the surface
		// (e.g. the walls of a hole, which are steep, so would otherwise look like cliffs) there's a few meters of loose
		// ground over rock.
		const terrain::ColumnSample column = terrainGenerator->SampleColumn(pos.x, pos.z);
		const real depth = column.height - pos.y;
		const real SOIL_DEPTH = 4.0f;
		const real depthHardness = glm::smoothstep(SOIL_DEPTH, SOIL_DEPTH + 3.0f, depth);
		if (depth > 0.75f)
		{
			return depthHardness;
		}
		const glm::vec4 weights = terrainGenerator->SampleSurface(pos, normal).weights;
		const real total = weights.x + weights.y + weights.z + weights.w;
		const real surfaceHardness = total > 1e-3f ? glm::clamp(weights.y / total, 0.0f, 1.0f) : 0.0f;
		return glm::max(surfaceHardness, depthHardness);
	}

	void Player::TeleportOnTerrain(const glm::vec3& pos, bool bKeepExactPosition /* = false */)
	{
		m_Transform.SetWorldPosition(pos);
		m_RigidBody->GetRigidBodyInternal()->setLinearVelocity(btVector3(0, 0, 0));
		m_bTerrainPlacementPending = true;
		m_bTerrainPlacementExact = bKeepExactPosition;
	}

	void Player::PlaceOnTerrain(bool bKeepHeight)
	{
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		const glm::vec3 pos = m_Transform.GetWorldPosition();
		glm::vec3 groundPos;
		if (terrainGenerator == nullptr || !terrainGenerator->FindStandingPoint(pos, bKeepHeight, groundPos))
		{
			return;
		}

		// The capsule's center is half its height above its feet
		HoldUntilTerrainCollision(groundPos + glm::vec3(0.0f, m_Height * 0.5f + 0.25f, 0.0f));
	}

	bool Player::IsSupportedBelow(const glm::vec3& pos) const
	{
		// Feet, plus some slack
		const real maxDrop = m_Height * 0.5f + 1.0f;
		const glm::vec3 down(0.0f, -1.0f, 0.0f);

		// Terrain (its collision may not have loaded yet, so this uses the density)
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		glm::vec3 hitPos, hitNormal;
		if (terrainGenerator != nullptr && terrainGenerator->Raycast(pos, down, maxDrop, hitPos, hitNormal))
		{
			return true;
		}

		// Anything else solid (e.g. something built), other than the player
		struct IgnoreObjectCallback : public btCollisionWorld::ClosestRayResultCallback
		{
			IgnoreObjectCallback(const btVector3& from, const btVector3& to, const btCollisionObject* ignored) :
				btCollisionWorld::ClosestRayResultCallback(from, to), ignored(ignored) {}
			btScalar addSingleResult(btCollisionWorld::LocalRayResult& rayResult, bool bNormalInWorldSpace) override
			{
				if (rayResult.m_collisionObject == ignored)
				{
					return 1.0f;
				}
				return btCollisionWorld::ClosestRayResultCallback::addSingleResult(rayResult, bNormalInWorldSpace);
			}
			const btCollisionObject* ignored;
		};
		const btVector3 rayStart = ToBtVec3(pos);
		const btVector3 rayEnd = ToBtVec3(pos + down * maxDrop);
		IgnoreObjectCallback rayCallback(rayStart, rayEnd, m_RigidBody->GetRigidBodyInternal());
		rayCallback.m_collisionFilterMask = (i32)CollisionType::STATIC;
		g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld()->rayTest(rayStart, rayEnd, rayCallback);
		return rayCallback.hasHit();
	}

	void Player::HoldUntilTerrainCollision(const glm::vec3& holdPos)
	{
		m_TerrainHoldPos = holdPos;
		m_Transform.SetWorldPosition(m_TerrainHoldPos);
		btRigidBody* rigidBodyInternal = m_RigidBody->GetRigidBodyInternal();
		rigidBodyInternal->clearForces();
		rigidBodyInternal->setLinearVelocity(btVector3(0, 0, 0));
		m_bAwaitingTerrainCollision = true;
		m_AwaitingTerrainCollisionTime = 0.0f;
		m_InsideTerrainTime = 0.0f;
		m_Traversal->Reset();
	}

	void Player::UpdateTerrainSafety()
	{
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (terrainGenerator == nullptr || m_RidingVehicleID.IsValid() || m_TrackRidingID != InvalidTrackID)
		{
			m_bAwaitingTerrainCollision = false;
			return;
		}

		const glm::vec3 pos = m_Transform.GetWorldPosition();
		if (m_bTerrainPlacementPending && terrainGenerator->IsWorldChangePending())
		{
			// The density still describes the previous world (e.g. just after loading a world), so wait for it
			btRigidBody* rigidBodyInternal = m_RigidBody->GetRigidBodyInternal();
			rigidBodyInternal->clearForces();
			rigidBodyInternal->setLinearVelocity(btVector3(0, 0, 0));
			m_RigidBody->SetWorldPosition(pos);
			return;
		}

		if (m_bTerrainPlacementPending)
		{
			m_bTerrainPlacementPending = false;
			// Restoring a saved position: keep it if the whole capsule is in open air & standing on something (it
			// may be on a building, so the terrain below isn't necessarily the ground)
			bool bKeepExact = false;
			if (m_bTerrainPlacementExact)
			{
				const real axisHalfLength = glm::max(m_Height * 0.5f - m_Radius, 0.0f);
				const bool bClear = terrainGenerator->SampleDensity(pos) > 0.0f &&
					terrainGenerator->SampleDensity(pos - VEC3_UP * axisHalfLength) > 0.0f &&
					terrainGenerator->SampleDensity(pos + VEC3_UP * axisHalfLength) > 0.0f;
				bKeepExact = bClear && IsSupportedBelow(pos);
			}
			if (bKeepExact)
			{
				HoldUntilTerrainCollision(pos);
			}
			else
			{
				PlaceOnTerrain(true);
			}
			m_bTerrainPlacementExact = false;
		}
		else if (!m_bAwaitingTerrainCollision)
		{
			// Stuck inside the terrain (e.g. collision changed under the player, or something was placed on them)
			if (terrainGenerator->SampleDensity(pos) < -1.0f)
			{
				m_InsideTerrainTime += g_DeltaTime;
				if (m_InsideTerrainTime > 0.3f)
				{
					PrintWarn("Player was inside terrain at (%.1f, %.1f, %.1f), moving to the surface\n", pos.x, pos.y, pos.z);
					PlaceOnTerrain(false);
				}
			}
			else
			{
				m_InsideTerrainTime = 0.0f;
			}
		}

		if (m_bAwaitingTerrainCollision)
		{
			// Hold in place until there's something to stand on
			m_AwaitingTerrainCollisionTime += g_DeltaTime;
			m_Transform.SetWorldPosition(m_TerrainHoldPos);
			m_RigidBody->GetRigidBodyInternal()->setLinearVelocity(btVector3(0, 0, 0));

			if (terrainGenerator->IsCollisionLoadedBelow(m_TerrainHoldPos))
			{
				m_bAwaitingTerrainCollision = false;
			}
			else if (m_AwaitingTerrainCollisionTime > 30.0f)
			{
				PrintWarn("Timed out waiting for terrain collision below the player\n");
				m_bAwaitingTerrainCollision = false;
			}
		}
	}

	void Player::DrawTerrainToolHUD() const
	{
		if (m_Woodcraft != nullptr)
		{
			m_Woodcraft->DrawHUD();
		}

		if (!m_bTerrainToolActive)
		{
			return;
		}

		const std::string material = terrain::MaterialName(terrain::FILL_MATERIALS[m_TerrainFillMaterialIndex]);
		const StringID toolTypeID = GetWieldedToolType();
		std::vector<std::string> lines;
		std::string title = ToolName(toolTypeID);
		lines.push_back(ToUpper(title));
		lines.push_back(toolTypeID == ShovelSID ? LOC(TerrainTool_DigLoose) : LOC(TerrainTool_DigRock));
		lines.push_back(LOCF(TerrainTool_Place, material.c_str()));
		lines.push_back(LOC(TerrainTool_ChangeMaterial));

		BitmapFont* font = g_Renderer->SetFont(SID("editor-02"));
		const real lineHeight = 3.2f * font->GetMetric('W')->height / (real)g_Window->GetSize().y;
		const glm::vec4 titleColour(1.0f, 0.85f, 0.4f, 1.0f);
		const glm::vec4 textColour(0.95f, 0.95f, 0.95f, 1.0f);

		real yOffset = 0.11f; // (above the quick access bar)
		for (i32 i = (i32)lines.size() - 1; i >= 0; --i)
		{
			g_Renderer->DrawStringSS(lines[i], i == 0 ? titleColour : textColour, AnchorPoint::BOTTOM_LEFT, glm::vec2(0.03f, yOffset), 1.5f, 0.6f);
			yOffset += lineHeight;
		}
	}

	StringID Player::GetWieldedToolType() const
	{
		const GameObjectStack& stack = m_QuickAccessInventory[m_SelectedQuickAccessItemSlot];
		if (stack.count <= 0 || !stack.prefabID.IsValid())
		{
			return InvalidStringID;
		}

		GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(stack.prefabID);
		return prefabTemplate != nullptr && IsHandToolType(prefabTemplate->GetTypeID()) ? prefabTemplate->GetTypeID() : InvalidStringID;
	}

	bool Player::IsWieldingPickAxe() const
	{
		return GetWieldedToolType() == PickAxeSID;
	}

	bool Player::IsWieldingAxe() const
	{
		return GetWieldedToolType() == AxeSID;
	}

	void Player::SwingTool(real duration)
	{
		if (m_ToolSwingTimer <= 0.0f && duration > 0.0f)
		{
			m_ToolSwingDuration = duration;
			m_ToolSwingTimer = duration;
		}
	}
} // namespace flex
