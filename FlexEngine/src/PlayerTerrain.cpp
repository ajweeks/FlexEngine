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

// The player in streamed terrain: digging/filling with the pickaxe, & staying on top of the terrain
namespace flex
{
	void Player::UpdateTerrainTool()
	{
		m_bTerrainToolActive = false;

		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (terrainGenerator == nullptr ||
			!m_bPossessed ||
			!IsWieldingPickAxe() ||
			m_BuildMode.IsActive() ||
			IsAnyInventoryShowing() ||
			m_RidingVehicleID.IsValid() ||
			m_TrackRidingID != InvalidTrackID ||
			IsPlacingTrack() ||
			IsEditingTrack() ||
			g_EngineInstance->IsSimulationPaused() ||
			(m_Woodcraft != nullptr && m_Woodcraft->BlocksTerrainTool()))
		{
			return;
		}

		const i32 materialCount = (i32)ARRAY_LENGTH(terrain::FILL_MATERIALS);
		if (g_InputManager->GetKeyPressed(KeyCode::KEY_R))
		{
			const i32 step = g_InputManager->GetKeyDown(KeyCode::KEY_LEFT_SHIFT) != 0 ? -1 : 1;
			m_TerrainFillMaterialIndex = (m_TerrainFillMaterialIndex + step + materialCount) % materialCount;
		}
		m_TerrainFillMaterialIndex = glm::clamp(m_TerrainFillMaterialIndex, 0, materialCount - 1);

		BaseCamera* camera = g_CameraManager->CurrentCamera();
		glm::vec3 hitPos, hitNormal;
		// Digging into the same spot again looks through the hole just dug, at its far side (up to a dig radius
		// further away). Surfaces a little past the reach are still dug into, with the brush held at the reach.
		const real rayLength = m_TerrainToolReach + m_TerrainDigRadius * 0.9f;
		if (!terrainGenerator->Raycast(camera->position, camera->forward, rayLength, hitPos, hitNormal))
		{
			return;
		}
		const real hitDistance = glm::distance(camera->position, hitPos);
		if (hitDistance > m_TerrainToolReach)
		{
			hitPos = camera->position + camera->forward * m_TerrainToolReach;
		}
		m_bTerrainToolActive = true;

		m_TerrainToolCooldown -= g_DeltaTime;

		const bool bDig = g_InputManager->IsMouseButtonDown(MouseButton::LEFT);
		const bool bFill = !bDig && g_InputManager->IsMouseButtonDown(MouseButton::RIGHT);

		// Placed material sits on top of the surface
		const glm::vec3 fillCenter = hitPos + hitNormal * m_TerrainDigRadius * 0.6f;
		const glm::vec3 previewCenter = bFill ? fillCenter : hitPos;
		g_Renderer->GetDebugRenderer()->drawSphere(ToBtVec3(previewCenter), m_TerrainDigRadius * 0.98f,
			bFill ? btVector3(0.3f, 0.8f, 1.0f) : btVector3(1.0f, 0.6f, 0.15f));

		if ((bDig || bFill) && m_TerrainToolCooldown <= 0.0f)
		{
			m_TerrainToolCooldown = 0.25f;
			if (bDig)
			{
				// Also removes small scraps of terrain the dig leaves floating
				terrainGenerator->Dig(hitPos, m_TerrainDigRadius);
			}
			else
			{
				// Don't bury ourselves (distance to the capsule's axis)
				const glm::vec3 playerPos = m_Transform.GetWorldPosition();
				const real axisHalfLength = m_Height * 0.5f - m_Radius;
				const glm::vec3 closestOnAxis = playerPos + VEC3_UP * glm::clamp(fillCenter.y - playerPos.y, -axisHalfLength, axisHalfLength);
				if (glm::distance(fillCenter, closestOnAxis) > m_TerrainDigRadius + m_Radius + 0.2f)
				{
					terrainGenerator->Fill(fillCenter, m_TerrainDigRadius, terrain::FILL_MATERIALS[m_TerrainFillMaterialIndex]);
				}
			}
		}
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
		std::vector<std::string> lines;
		lines.push_back("PICKAXE");
		lines.push_back("[LMB] Dig");
		lines.push_back("[RMB] Place " + material);
		lines.push_back("[R] / [Shift+R] Change material");

		BitmapFont* font = g_Renderer->SetFont(SID("editor-02"));
		const real lineHeight = 3.2f * font->GetMetric('W')->height / (real)g_Window->GetSize().y;
		const glm::vec4 titleColour(1.0f, 0.85f, 0.4f, 1.0f);
		const glm::vec4 textColour(0.95f, 0.95f, 0.95f, 1.0f);

		real yOffset = 0.05f;
		for (i32 i = (i32)lines.size() - 1; i >= 0; --i)
		{
			g_Renderer->DrawStringSS(lines[i], i == 0 ? titleColour : textColour, AnchorPoint::BOTTOM_LEFT, glm::vec2(0.03f, yOffset), 1.5f, 0.6f);
			yOffset += lineHeight;
		}
	}

	bool Player::IsWieldingPickAxe() const
	{
		const GameObjectStack& stack = m_QuickAccessInventory[m_SelectedQuickAccessItemSlot];
		if (stack.count <= 0 || !stack.prefabID.IsValid())
		{
			return false;
		}

		GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(stack.prefabID);
		return prefabTemplate != nullptr && prefabTemplate->GetTypeID() == PickAxeSID;
	}
} // namespace flex
