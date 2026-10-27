#include "stdafx.hpp"

#include "Cameras/OverheadCamera.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp> // for rotateY
#include <glm/vec2.hpp>
IGNORE_WARNINGS_POP

#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/SceneManager.hpp"
#include "Time.hpp"
#include "Window/Window.hpp"

namespace flex
{
	OverheadCamera::OverheadCamera(real FOV) :
		BaseCamera("overhead", CameraType::OVERHEAD, true, FOV)
	{
		bPossessPlayer = true;
		// The camera can be pulled in to within m_MinCollisionDistance (+ m_CollisionRadius) of a surface
		zNear = 0.1f;
		ResetValues();
	}

	OverheadCamera::~OverheadCamera()
	{
	}

	void OverheadCamera::Initialize()
	{
		if (!m_bInitialized)
		{
			FindPlayer();

			m_PlayerPosRollingAvg = RollingAverage<glm::vec3>(3, SamplingType::LINEAR);
			m_PlayerForwardRollingAvg = RollingAverage<glm::vec3>(8, SamplingType::LINEAR);

			ResetValues();

			BaseCamera::Initialize();
		}
	}

	void OverheadCamera::OnPostSceneChange()
	{
		BaseCamera::OnPostSceneChange();

		m_Player0 = nullptr;
		FindPlayer();

		ResetValues();
	}

	void OverheadCamera::FixedUpdate()
	{
		if (m_Player0 == nullptr)
		{
			return;
		}

		// Sample on the fixed step so smoothing is independent of frame rate
		m_PlayerForwardRollingAvg.AddValue(m_Player0->GetTransform()->GetForward());
		m_PlayerPosRollingAvg.AddValue(m_Player0->GetTransform()->GetWorldPosition());

		m_PrevPlayerPosAvg = m_CurrPlayerPosAvg;
		m_CurrPlayerPosAvg = m_PlayerPosRollingAvg.currentAverage;
		m_PrevPlayerForwardAvg = m_CurrPlayerForwardAvg;
		m_CurrPlayerForwardAvg = m_PlayerForwardRollingAvg.currentAverage;
	}

	void OverheadCamera::LateUpdate()
	{
		TrackPlayer();

		BaseCamera::LateUpdate();
	}

	void OverheadCamera::TrackPlayer()
	{
		if (m_Player0 == nullptr)
		{
			return;
		}

		if (g_InputManager->GetActionPressed(Action::ZOOM_OUT))
		{
			m_TargetZoomLevel += (m_MaxZoomLevel - m_MinZoomLevel) / (real)(m_ZoomLevels - 1);
			m_TargetZoomLevel = glm::clamp(m_TargetZoomLevel, m_MinZoomLevel, m_MaxZoomLevel);
		}

		if (g_InputManager->GetActionPressed(Action::ZOOM_IN))
		{
			m_TargetZoomLevel -= (m_MaxZoomLevel - m_MinZoomLevel) / (real)(m_ZoomLevels - 1);
			m_TargetZoomLevel = glm::clamp(m_TargetZoomLevel, m_MinZoomLevel, m_MaxZoomLevel);
		}

		if (!NearlyEquals(m_ZoomLevel, m_TargetZoomLevel, 0.01f))
		{
			m_ZoomLevel = MoveTowards(m_ZoomLevel, m_TargetZoomLevel, g_DeltaTime * 15.0f);
		}

		PhysicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld();
		real alpha = (physicsWorld != nullptr) ? physicsWorld->GetInterpolationAlpha() : 1.0f;
		m_TargetLookAtPos = glm::mix(m_PrevPlayerPosAvg, m_CurrPlayerPosAvg, alpha);
		glm::vec3 playerForward = glm::mix(m_PrevPlayerForwardAvg, m_CurrPlayerForwardAvg, alpha);

#if THOROUGH_CHECKS
		ENSURE(!IsNanOrInf(m_TargetLookAtPos));
#endif

		{
			// FLEX_CAMERA_COLLISION_DEBUG=<elevation degrees>[,wall]: holds the camera low (so it often meets the ground) &
			// periodically prints how often it was pulled in, how long that took & whether it still ended up inside
			// the terrain. ",wall" also adds an invisible (except in the collision wireframe) 4m wide wall 1.5m from the
			// player after 5s, so turning around brings the camera up against it
			static const char* debugElevation = getenv("FLEX_CAMERA_COLLISION_DEBUG");
			if (debugElevation != nullptr)
			{
				m_Elevation = glm::radians((real)atof(debugElevation));

				static real s_WallTimer = 0.0f;
				PhysicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld();
				if (strstr(debugElevation, "wall") != nullptr && s_WallTimer >= 0.0f && physicsWorld != nullptr && physicsWorld->GetWorld() != nullptr)
				{
					s_WallTimer += g_DeltaTime;
					if (s_WallTimer > 5.0f)
					{
						s_WallTimer = -1.0f;
						btCollisionObject* wall = new btCollisionObject();
						wall->setCollisionShape(new btBoxShape(btVector3(0.25f, 4.0f, 2.0f)));
						wall->setWorldTransform(btTransform(btQuaternion::getIdentity(), ToBtVec3(m_TargetLookAtPos + glm::vec3(1.75f, 2.0f, 0.0f))));
						physicsWorld->GetWorld()->addCollisionObject(wall, (i32)CollisionType::STATIC, (i32)CollisionType::EVERYTHING);
						Print("Overhead camera collision: added a test wall beside the player at %s\n", VecToString(m_TargetLookAtPos, 1).c_str());
					}
				}
			}

			const ms start = debugElevation != nullptr ? Time::CurrentMilliseconds() : 0.0f;
			position = ResolveCollision(m_TargetLookAtPos, GetOffsetPosition(m_TargetLookAtPos, playerForward));

			if (debugElevation != nullptr)
			{
				static u32 s_Frames = 0, s_HitFrames = 0, s_AsideFrames = 0, s_InsideFrames = 0, s_SideJumps = 0, s_StateChanges = 0;
				static real s_PrevSideOffset = 0.0f;
				static bool s_bPrevPushedAside = false;
				static real s_MinDistance = 1e9f;
				static ms s_TotalMs = 0.0f, s_MaxMs = 0.0f;
				static real s_MinFraction = 1.0f;
				const ms elapsed = Time::CurrentMilliseconds() - start;
				++s_Frames;
				s_HitFrames += m_bCollisionHit ? 1 : 0;
				s_AsideFrames += m_bPushedAside ? 1 : 0;
				s_SideJumps += glm::abs(m_SideOffset - s_PrevSideOffset) > 0.5f ? 1 : 0;
				s_StateChanges += m_bPushedAside != s_bPrevPushedAside ? 1 : 0;
				s_PrevSideOffset = m_SideOffset;
				s_bPrevPushedAside = m_bPushedAside;
				s_MinDistance = glm::min(s_MinDistance, glm::distance(position, m_TargetLookAtPos));
				s_TotalMs += elapsed;
				s_MaxMs = glm::max(s_MaxMs, elapsed);
				s_MinFraction = glm::min(s_MinFraction, m_CollisionFraction);
				TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
				if (terrainGenerator != nullptr && terrainGenerator->SampleDensity(position) < 0.0f)
				{
					++s_InsideFrames;
				}
				if (s_Frames % 300 == 0)
				{
					Print("Overhead camera collision: %u frames, %.0f%% pulled in (min distance %.0f%%, %.2fm from the player), %.0f%% pushed aside (%u side jumps, %u state changes), %u inside terrain, %.3f ms avg, %.3f ms max\n",
						s_Frames, 100.0f * s_HitFrames / s_Frames, s_MinFraction * 100.0f, s_MinDistance, 100.0f * s_AsideFrames / s_Frames, s_SideJumps, s_StateChanges, s_InsideFrames, s_TotalMs / s_Frames, s_MaxMs);
				}
			}
		}
		SetLookAt();

		CalculateYawAndPitchFromForward();
		RecalculateViewProjection();
	}

	void OverheadCamera::DrawImGuiObjects()
	{
		if (m_Player0 != nullptr)
		{
			if (ImGui::TreeNode("Overhead camera"))
			{
				glm::vec3 start = m_Player0->GetTransform()->GetWorldPosition();
				glm::vec3 end = start + m_PlayerForwardRollingAvg.currentAverage * 10.0f;
				g_Renderer->GetDebugRenderer()->drawLine(ToBtVec3(start), ToBtVec3(end), btVector3(1.0f, 1.0f, 1.0f));

				ImGui::Text("Avg player forward: %s", VecToString(m_PlayerForwardRollingAvg.currentAverage, 2).c_str());
				ImGui::Text("For: %s", VecToString(forward, 2).c_str());
				ImGui::Text("Collision: %s, distance %.0f%%, side %.2fm%s", m_bCollisionHit ? "hit" : "clear", m_CollisionFraction * 100.0f,
					m_SideOffset, m_bPushedAside ? " (pushed aside)" : "");
				ImGui::TreePop();
			}
		}
	}

	void OverheadCamera::AddToElevation(real deltaElevation)
	{
		m_Elevation = glm::clamp(m_Elevation + deltaElevation, m_MinElevation, m_MaxElevation);
	}

	glm::vec3 OverheadCamera::GetOffsetPosition(const glm::vec3& pos, const glm::vec3& playerForward)
	{
		glm::vec3 backward = -playerForward;
		const real dist = glm::root_two<real>() * 2.0f * m_ZoomLevel;
		glm::vec3 offsetVec = (backward * glm::cos(m_Elevation) + VEC3_UP * glm::sin(m_Elevation)) * dist;
		return pos + offsetVec;
	}

	real OverheadCamera::ClearDistance(const glm::vec3& from, const glm::vec3& to) const
	{
		const glm::vec3 offset = to - from;
		const real length = glm::length(offset);
		if (length < 1e-3f || IsNanOrInf(offset))
		{
			return length;
		}
		real clearDist = length;

		// Physics: a sphere swept from the player out to the camera. Ignores the player's own body, triggers,
		// anything that moves (dropped items, vehicles) & small props, so only large static things (terrain,
		// buildings) push the camera in
		BaseScene* scene = g_SceneManager->CurrentScene();
		PhysicsWorld* physicsWorld = scene->GetPhysicsWorld();
		if (physicsWorld != nullptr && physicsWorld->GetWorld() != nullptr)
		{
			struct CameraSweepCallback final : public btCollisionWorld::ClosestConvexResultCallback
			{
				CameraSweepCallback(const btVector3& from, const btVector3& to, const btCollisionObject* ignored) :
					ClosestConvexResultCallback(from, to),
					ignored(ignored)
				{
					m_collisionFilterMask = (i32)CollisionType::STATIC | (i32)CollisionType::DEFAULT;
				}

				virtual bool needsCollision(btBroadphaseProxy* proxy) const override
				{
					if (!ClosestConvexResultCallback::needsCollision(proxy))
					{
						return false;
					}
					const btCollisionObject* object = static_cast<const btCollisionObject*>(proxy->m_clientObject);
					if (object == ignored || !object->hasContactResponse() || !object->isStaticOrKinematicObject())
					{
						return false;
					}
					const btVector3 extent = proxy->m_aabbMax - proxy->m_aabbMin;
					const real SMALL_PROP_SIZE = 1.5f;
					return btMax(btMax(extent.x(), extent.y()), extent.z()) >= SMALL_PROP_SIZE;
				}

				const btCollisionObject* ignored;
			};

			const btCollisionObject* playerBody = (m_Player0 != nullptr && m_Player0->GetRigidBody() != nullptr) ? m_Player0->GetRigidBody()->GetRigidBodyInternal() : nullptr;
			btSphereShape sphere(m_CollisionRadius);
			const btTransform fromTransform(btQuaternion::getIdentity(), ToBtVec3(from));
			const btTransform toTransform(btQuaternion::getIdentity(), ToBtVec3(to));
			CameraSweepCallback callback(fromTransform.getOrigin(), toTransform.getOrigin(), playerBody);
			physicsWorld->GetWorld()->convexSweepTest(&sphere, fromTransform, toTransform, callback);
			if (callback.hasHit())
			{
				clearDist = glm::min(clearDist, callback.m_closestHitFraction * length);
			}
		}

		// Terrain whose collision hasn't been created yet (it's only made near the player, a little after chunks
		// load): march the density instead. Starting inside the terrain finds nothing, so this can't trap the camera
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (terrainGenerator != nullptr && !terrainGenerator->IsWorldChangePending())
		{
			glm::vec3 hitPos, hitNormal;
			if (terrainGenerator->Raycast(from, offset / length, clearDist + m_CollisionRadius, hitPos, hitNormal))
			{
				clearDist = glm::min(clearDist, glm::distance(from, hitPos) - m_CollisionRadius);
			}
		}

		return glm::max(clearDist, 0.0f);
	}

	glm::vec3 OverheadCamera::ResolveCollision(const glm::vec3& pivot, const glm::vec3& desiredPos)
	{
		const glm::vec3 offset = desiredPos - pivot;
		const real desiredDist = glm::length(offset);
		if (desiredDist < 1e-3f || IsNanOrInf(offset))
		{
			return desiredPos;
		}
		const glm::vec3 dir = offset / desiredDist;

		// Sideways (horizontal, across the view)
		glm::vec3 side = glm::cross(VEC3_UP, dir);
		if (glm::length2(side) < 1e-6f)
		{
			side = m_Player0 != nullptr ? m_Player0->GetTransform()->GetRight() : VEC3_RIGHT;
		}
		side = glm::normalize(side);

		const real clearDist = ClearDistance(pivot, desiredPos);
		const real minDist = glm::min(m_MinObstructedDistance, desiredDist);
		auto PositionFor = [&](real dist, real sideOffset) { return pivot + dir * dist + side * sideOffset; };
		auto IsClear = [&](const glm::vec3& pos) { return ClearDistance(pivot, pos) >= glm::distance(pivot, pos) - 0.01f; };

		real targetDist = clearDist;
		real targetSideOffset = 0.0f;
		const bool bWasPushedAside = m_bPushedAside;
		m_bPushedAside = false;
		if (clearDist < minDist)
		{
			// Rather than closing right in on the player, stay at least minDist back & move sideways until there's a
			// clear view of them. The offset used last is kept while it works, then the same side is preferred, so it
			// doesn't flip between options
			static const real SIDE_STEPS[] = { 1.5f, 3.0f, 4.5f };
			if (bWasPushedAside && m_SideOffset != 0.0f && IsClear(PositionFor(minDist, m_SideTarget)))
			{
				targetSideOffset = m_SideTarget;
				m_bPushedAside = true;
			}
			for (u32 i = 0; i < ARRAY_LENGTH(SIDE_STEPS) && !m_bPushedAside; ++i)
			{
				for (real sign : { m_SideSign, -m_SideSign })
				{
					if (IsClear(PositionFor(minDist, sign * SIDE_STEPS[i])))
					{
						targetSideOffset = sign * SIDE_STEPS[i];
						m_SideSign = sign;
						m_bPushedAside = true;
						break;
					}
				}
			}
			if (m_bPushedAside)
			{
				targetDist = minDist;
			}
		}
		m_SideTarget = targetSideOffset;

		// Nowhere clear to the sides either (e.g. a tunnel): pulled in, as close as m_MinCollisionDistance
		targetDist = glm::clamp(targetDist, glm::min(m_MinCollisionDistance, desiredDist), desiredDist);
		const real targetFraction = targetDist / desiredDist;
		m_bCollisionHit = targetFraction < 0.999f;

		// Fast in (immediately, so the view never clips through), slow out
		if (targetFraction <= m_CollisionFraction)
		{
			m_CollisionFraction = targetFraction;
		}
		else
		{
			m_CollisionFraction = glm::mix(m_CollisionFraction, targetFraction, 1.0f - glm::exp(-m_CollisionReturnSpeed * g_DeltaTime));
			m_CollisionFraction = glm::min(m_CollisionFraction + 0.02f * g_DeltaTime, targetFraction);
		}
		const real dist = desiredDist * m_CollisionFraction;

		// Likewise sideways: straight out to the side when blocked (easing across would pass behind the obstruction),
		// & eased back, only through clear positions
		const bool bMovingOut = targetSideOffset != 0.0f && (glm::sign(targetSideOffset) != glm::sign(m_SideOffset) || glm::abs(targetSideOffset) > glm::abs(m_SideOffset));
		if (bMovingOut)
		{
			m_SideOffset = targetSideOffset;
		}
		else if (m_SideOffset != targetSideOffset)
		{
			real eased = glm::mix(m_SideOffset, targetSideOffset, 1.0f - glm::exp(-m_SideOffsetSpeed * g_DeltaTime));
			if (glm::abs(eased - targetSideOffset) < 0.01f)
			{
				eased = targetSideOffset;
			}
			if (IsClear(PositionFor(dist, eased)))
			{
				m_SideOffset = eased;
			}
		}

		glm::vec3 result = PositionFor(dist, m_SideOffset);
		if (m_SideOffset != 0.0f)
		{
			// (The view of the player can still be blocked, e.g. as they turn, so pull in along the line to them)
			const real resultDist = glm::distance(pivot, result);
			const real clearToResult = ClearDistance(pivot, result);
			if (clearToResult < resultDist)
			{
				result = pivot + (result - pivot) * (glm::max(clearToResult, glm::min(m_MinCollisionDistance, resultDist)) / resultDist);
			}
		}

		return result;
	}

	void OverheadCamera::SetPosAndLookAt()
	{
		if (m_Player0 == nullptr)
		{
			return;
		}

		m_TargetLookAtPos = m_Player0->GetTransform()->GetWorldPosition();

		glm::vec3 desiredPos = GetOffsetPosition(m_TargetLookAtPos, m_Player0->GetTransform()->GetForward());
		position = desiredPos;

		SetLookAt();
	}

	void OverheadCamera::SetLookAt()
	{
		forward = glm::normalize(m_TargetLookAtPos - position);
		right = normalize(glm::cross(VEC3_UP, forward));
		up = cross(forward, right);
	}

	void OverheadCamera::ResetSmoothedSamples()
	{
		m_CurrPlayerPosAvg = m_PrevPlayerPosAvg = m_PlayerPosRollingAvg.currentAverage;
		m_CurrPlayerForwardAvg = m_PrevPlayerForwardAvg = m_PlayerForwardRollingAvg.currentAverage;
	}

	void OverheadCamera::FindPlayer()
	{
		m_Player0 = g_SceneManager->CurrentScene()->GetPlayer(0);
	}

	void OverheadCamera::ResetValues()
	{
		ResetOrientation();

		m_Vel = VEC3_ZERO;
		m_CollisionFraction = 1.0f;
		m_bCollisionHit = false;
		m_SideOffset = 0.0f;
		m_SideTarget = 0.0f;
		m_bPushedAside = false;
		m_TargetZoomLevel = (real)(m_ZoomLevels / 2) * ((m_MaxZoomLevel - m_MinZoomLevel) / (real)(m_ZoomLevels - 1)) + m_MinZoomLevel;
		m_ZoomLevel = m_TargetZoomLevel;
		pitch = -PI_DIV_FOUR;
		m_Elevation = m_MinElevation + (m_MaxElevation - m_MinElevation) * 0.5f;
		SetPosAndLookAt();

		if (m_Player0 != nullptr)
		{
			m_PlayerPosRollingAvg.Reset(m_Player0->GetTransform()->GetWorldPosition());
			m_PlayerForwardRollingAvg.Reset(m_Player0->GetTransform()->GetForward());
		}
		else
		{
			m_PlayerPosRollingAvg.Reset();
			m_PlayerForwardRollingAvg.Reset();
		}

		ResetSmoothedSamples();

		RecalculateViewProjection();
	}

} // namespace flex
