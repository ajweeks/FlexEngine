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

#include "Audio/AudioManager.hpp"
#include "Cameras/CameraManager.hpp"
#include "FlexEngine.hpp"
#include "Graphics/BitmapFont.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "InputManager.hpp"
#include "JSONParser.hpp"
#include "Localization.hpp"
#include "Physics/PhysicsHelpers.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Platform/Platform.hpp"
#include "PlayerController.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Objects/Carts.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/SceneManager.hpp"
#include "StringBuilder.hpp"
#include "Systems/TrackManager.hpp"
#include "Window/Window.hpp"
#include "WorldStats.hpp"

// Building, editing & riding tracks
namespace flex
{
	real Player::GetDistAlongTrack() const
	{
		if (m_TrackRidingID == InvalidTrackID)
		{
			return -1.0f;
		}
		else
		{
			return m_DistAlongTrack;
		}
	}

	void Player::AttachToTrack(TrackID trackID, real distAlongTrack)
	{
		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
		BezierCurveList* track = trackManager->GetTrack(trackID);
		CHECK_NE(track, nullptr);

		if (m_TrackRidingID != InvalidTrackID)
		{
			PrintWarn("Player::AttachToTrack called when already attached! Detaching...\n");
			DetachFromTrack();
		}

		distAlongTrack = Saturate(distAlongTrack);

		m_TrackRidingID = trackID;
		m_DistAlongTrack = distAlongTrack;

		// Keep facing the way the player was looking. IsVectorFacingDownTrack is true when the vector points towards
		// decreasing t, which is the way FACING_FORWARD faces
		if (track->IsVectorFacingDownTrack(m_DistAlongTrack, GetLookDirection()))
		{
			m_TrackState = TrackState::FACING_FORWARD;
		}
		else
		{
			m_TrackState = TrackState::FACING_BACKWARD;
		}

		AudioManager::PlaySource(m_SoundTrackAttachID);
	}

	void Player::DetachFromTrack()
	{
		if (m_TrackRidingID != InvalidTrackID)
		{
			// Hop off to the side the player is looking towards so it's clear they've left the track
			TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
			glm::vec3 trackDir = trackManager->GetTrack(m_TrackRidingID)->GetCurveDirectionAt(m_DistAlongTrack);
			trackDir.y = 0.0f;
			if (glm::length2(trackDir) > 0.0001f)
			{
				const glm::vec3 trackRight = glm::normalize(glm::cross(VEC3_UP, glm::normalize(trackDir)));
				const real side = glm::dot(GetLookDirection(), trackRight) >= 0.0f ? 1.0f : -1.0f;
				const real hopDist = 1.75f;
				// Tracks can run through cuttings, so only hop to a side which isn't inside the terrain
				TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
				auto IsClear = [&](const glm::vec3& pos)
				{
					return terrainGenerator == nullptr ||
						(terrainGenerator->SampleDensity(pos) > 0.0f && terrainGenerator->SampleDensity(pos - VEC3_UP * (m_Height * 0.5f - 0.2f)) > 0.0f);
				};
				const glm::vec3 pos = m_Transform.GetWorldPosition();
				glm::vec3 hopPos = pos + VEC3_UP * 0.25f;
				for (real hopSide : { side, -side })
				{
					const glm::vec3 candidate = pos + trackRight * hopSide * hopDist + VEC3_UP * 0.25f;
					if (IsClear(candidate))
					{
						hopPos = candidate;
						break;
					}
				}
				m_Transform.SetWorldPosition(hopPos);
			}

			m_TrackRidingID = InvalidTrackID;
			m_DistAlongTrack = -1.0f;
			m_Controller->ResetForkSteer();

			// Turn the body to face where the player was looking so the view doesn't jump
			if (m_RidingLookYaw != 0.0f)
			{
				m_Transform.SetWorldRotation(GetLookYawRotation());
				m_RidingLookYaw = 0.0f;
			}
			AudioManager::PlaySource(m_SoundTrackDetachID);
		}
	}

	bool Player::IsFacingDownTrack() const
	{
		return (m_TrackState == TrackState::FACING_FORWARD);
	}

	void Player::BeginTurnTransition()
	{
		if (m_TrackState == TrackState::FACING_FORWARD)
		{
			m_TrackState = TrackState::FACING_BACKWARD;
		}
		else if (m_TrackState == TrackState::FACING_BACKWARD)
		{
			m_TrackState = TrackState::FACING_FORWARD;
		}
		else
		{
			PrintWarn("Unhandled track state when starting turn transition: %d/n", (i32)m_TrackState);
		}
	}

	bool Player::IsRidingTrack()
	{
		return m_TrackRidingID != InvalidTrackID;
	}

	void Player::ClearTrackBuildingState()
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;
		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);

		if (!ctx.m_GrabbedPoints.empty())
		{
			ctx.m_GrabbedPoints.clear();
			ctx.m_GrabbedTrackEndDirs.clear();
			trackManager->FindJunctions();
		}
		ctx.m_PlacedNodes.clear();
		ctx.m_StartDir = VEC3_ZERO;
		ctx.m_HoveredPoint = {};
		ctx.m_HoveredPlacedNodeIndex = -1;
		ctx.m_HoveredTrackID = InvalidTrackID;
		ctx.m_bReticleValid = false;
		ctx.m_bPieceAvailable = false;
		ctx.m_bPieceSnapped = false;
		ctx.m_PlacedPieces.clear();

		trackManager->SetPreviewTrack(nullptr);
		trackManager->SetHighlightedTrack(InvalidTrackID);
	}

	void Player::UpdateTrackBuildingReticle()
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;
		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);

		ctx.m_bReticleValid = false;
		ctx.m_HoveredPoint = {};
		ctx.m_HoveredPlacedNodeIndex = -1;

		struct IgnoreSelfRayResultCallback : public btCollisionWorld::ClosestRayResultCallback
		{
			IgnoreSelfRayResultCallback(const btVector3& rayFrom, const btVector3& rayTo, const btCollisionObject* selfObject) :
				ClosestRayResultCallback(rayFrom, rayTo),
				self(selfObject)
			{
			}

			virtual bool needsCollision(btBroadphaseProxy* proxy0) const override
			{
				const btCollisionObject* collisionObject = (const btCollisionObject*)proxy0->m_clientObject;
				if (collisionObject == self || (collisionObject->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE))
				{
					return false;
				}
				return ClosestRayResultCallback::needsCollision(proxy0);
			}

			const btCollisionObject* self;
		};

		// Aim where the camera is looking
		btVector3 rayStart, rayEnd;
		FlexEngine::GenerateRayAtScreenCenter(rayStart, rayEnd, ctx.m_MaxReach);

		btDiscreteDynamicsWorld* world = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
		IgnoreSelfRayResultCallback rayCallback(rayStart, rayEnd, GetRigidBody()->GetRigidBodyInternal());
		if (!ctx.m_bReticleOverride)
		{
			world->rayTest(rayStart, rayEnd, rayCallback);
		}
		if (ctx.m_bReticleOverride)
		{
			ctx.m_ReticlePos = ctx.m_ReticleOverridePos;
			ctx.m_bReticleValid = true;
		}
		else if (rayCallback.hasHit())
		{
			ctx.m_ReticlePos = ToVec3(rayCallback.m_hitPointWorld);
			ctx.m_bReticleValid = true;
		}
		else
		{
			// Nothing was hit, fall back to intersecting with the plane the track is being built on
			real planeHeight = ctx.m_PlacedNodes.empty() ? (m_Transform.GetWorldPosition().y - m_Height * 0.5f) : ctx.m_PlacedNodes.back().y;
			glm::vec3 start = ToVec3(rayStart);
			glm::vec3 delta = ToVec3(rayEnd - rayStart);
			if (delta.y < -0.0001f)
			{
				real t = (planeHeight - start.y) / delta.y;
				if (t >= 0.0f && t <= 1.0f)
				{
					ctx.m_ReticlePos = start + delta * t;
					ctx.m_bReticleValid = true;
				}
			}
		}

		if (!ctx.m_bReticleValid || IsPlacingTrackPieces())
		{
			// (pieces snap to track ends instead, see UpdateTrackPiecePlacement)
			return;
		}

		// Snap to nearby nodes
		ctx.m_HoveredPoint = trackManager->GetClosestNode(ctx.m_ReticlePos, ctx.m_SnapThreshold,
			ctx.m_GrabbedPoints.empty() ? nullptr : &ctx.m_GrabbedPoints);

		if (ctx.m_bPlacingTrack)
		{
			// Nodes of the track being placed can be snapped to to close a loop, which needs at least three nodes
			real closestDistSq = ctx.m_HoveredPoint.IsValid() ? glm::distance2(ctx.m_ReticlePos, trackManager->GetPoint(ctx.m_HoveredPoint)) : ctx.m_SnapThreshold * ctx.m_SnapThreshold;
			for (i32 i = 0; i < (i32)ctx.m_PlacedNodes.size() - 2; ++i)
			{
				real distSq = glm::distance2(ctx.m_ReticlePos, ctx.m_PlacedNodes[i]);
				if (distSq < closestDistSq)
				{
					closestDistSq = distSq;
					ctx.m_HoveredPlacedNodeIndex = i;
				}
			}
			if (ctx.m_HoveredPlacedNodeIndex != -1)
			{
				ctx.m_HoveredPoint = {};
				ctx.m_ReticlePos = ctx.m_PlacedNodes[ctx.m_HoveredPlacedNodeIndex];
			}
		}

		if (ctx.m_HoveredPoint.IsValid())
		{
			ctx.m_ReticlePos = trackManager->GetPoint(ctx.m_HoveredPoint);
		}
	}

	void Player::UpdateTrackBuilding()
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;
		CHECK(ctx.m_bPlacingTrack || ctx.m_bEditingTrack);

		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
		DebugRenderer* debugRenderer = g_Renderer->GetDebugRenderer();

		if (ctx.m_bPlacingTrack && m_bPossessed && !ImGui::GetIO().WantCaptureMouse && !IsAnyInventoryShowing())
		{
			// Choose a piece with the scroll wheel, number keys (0 is freeform) or R (shift + R goes back)
			const i32 choiceCount = (i32)TrackPieceType::_COUNT + 1;
			i32 selected = ctx.m_SelectedPiece;
			const real scroll = g_InputManager->GetVerticalScrollDistance();
			if (scroll > 0.0f)
			{
				selected = (selected + choiceCount - 1) % choiceCount;
			}
			else if (scroll < 0.0f)
			{
				selected = (selected + 1) % choiceCount;
			}
			if (g_InputManager->GetKeyPressed(KeyCode::KEY_R))
			{
				const i32 step = g_InputManager->GetKeyDown(KeyCode::KEY_LEFT_SHIFT) != 0 ? -1 : 1;
				selected = (selected + step + choiceCount) % choiceCount;
			}
			for (i32 i = 0; i < 9 && i < choiceCount - 1; ++i)
			{
				if (g_InputManager->GetKeyPressed((KeyCode)((i32)KeyCode::KEY_1 + i)))
				{
					selected = i;
				}
			}
			if (g_InputManager->GetKeyPressed(KeyCode::KEY_0))
			{
				selected = choiceCount - 1;
			}
			if (selected != ctx.m_SelectedPiece)
			{
				SelectTrackPiece(selected);
			}
		}

		UpdateTrackBuildingReticle();

		if (IsPlacingTrackPieces())
		{
			UpdateTrackPiecePlacement();
			return;
		}

		const btVector3 nodeCol(0.95f, 0.75f, 0.2f);
		const btVector3 highlightCol(1.0f, 1.0f, 1.0f);

		// Nodes lie at ground level, inside the track mesh, so mark them above it
		auto DrawNodeMarker = [debugRenderer](const glm::vec3& node, const btVector3& col)
		{
			const glm::vec3 markerPos = node + VEC3_UP * 1.0f;
			debugRenderer->drawLine(ToBtVec3(node), ToBtVec3(markerPos), col);
			debugRenderer->drawSphere(ToBtVec3(markerPos), 0.25f, col);
		};

		if (ctx.m_bPlacingTrack)
		{
			// Preview the track as it'd be if the next node were placed at the reticle
			std::vector<glm::vec3> previewNodes = ctx.m_PlacedNodes;
			glm::vec3 endDir = VEC3_ZERO;
			if (ctx.m_bReticleValid && !ctx.m_PlacedNodes.empty() && ctx.m_HoveredPlacedNodeIndex == -1 &&
				glm::distance(ctx.m_PlacedNodes.back(), ctx.m_ReticlePos) > 0.01f)
			{
				previewNodes.push_back(ctx.m_ReticlePos);
				if (ctx.m_HoveredPoint.IsValid())
				{
					endDir = trackManager->GetNodeDirection(ctx.m_HoveredPoint);
				}
			}

			if (ctx.m_HoveredPlacedNodeIndex != -1)
			{
				// The loop's tracks are contiguous, so they can be previewed as one
				std::vector<BezierCurveList> loopTracks;
				TrackManager::CreateLoopedTracks(ctx.m_PlacedNodes, ctx.m_StartDir, ctx.m_HoveredPlacedNodeIndex, loopTracks);
				BezierCurveList previewTrack;
				for (const BezierCurveList& loopTrack : loopTracks)
				{
					previewTrack.curves.insert(previewTrack.curves.end(), loopTrack.curves.begin(), loopTrack.curves.end());
				}
				trackManager->SetPreviewTrack(&previewTrack);
			}
			else if (previewNodes.size() >= 2)
			{
				BezierCurveList previewTrack = TrackManager::CreateTrackThroughNodes(previewNodes, ctx.m_StartDir, endDir);
				trackManager->SetPreviewTrack(&previewTrack);
			}
			else
			{
				trackManager->SetPreviewTrack(nullptr);
			}

			for (i32 i = 0; i < (i32)ctx.m_PlacedNodes.size(); ++i)
			{
				DrawNodeMarker(ctx.m_PlacedNodes[i], i == ctx.m_HoveredPlacedNodeIndex ? highlightCol : nodeCol);
			}
		}

		if (ctx.m_bEditingTrack)
		{
			trackManager->SetPreviewTrack(nullptr);

			// Aimed at track will be deleted by the delete action, so show that
			const real hoverRange = 1.25f;
			ctx.m_HoveredTrackID = (ctx.m_GrabbedPoints.empty() && ctx.m_bReticleValid) ? trackManager->GetClosestTrack(ctx.m_ReticlePos, hoverRange) : InvalidTrackID;
			trackManager->SetHighlightedTrack(ctx.m_HoveredTrackID);

			if (!ctx.m_GrabbedPoints.empty() && ctx.m_bReticleValid &&
				glm::distance2(trackManager->GetPoint(ctx.m_GrabbedPoints[0]), ctx.m_ReticlePos) > 0.000001f)
			{
				for (const TrackPointRef& ref : ctx.m_GrabbedPoints)
				{
					trackManager->GetTrack(ref.trackID)->SetPointPosAtIndex(ref.curveIndex, ref.pointIndex, ctx.m_ReticlePos, false);
				}
				// Handles are always generated, keeping each track smooth through its nodes
				for (const GrabbedTrackEndDirs& grabbedTrack : ctx.m_GrabbedTrackEndDirs)
				{
					trackManager->SmoothTrack(grabbedTrack.trackID, grabbedTrack.startDir, grabbedTrack.endDir);
				}
			}
		}

		// Show nearby nodes of existing tracks (to snap to when placing, or to edit)
		const real nodeDrawDistSq = 50.0f * 50.0f;
		const glm::vec3 playerPos = m_Transform.GetWorldPosition();
		for (i32 t = 0; t < (i32)trackManager->tracks.size(); ++t)
		{
			const BezierCurveList& track = trackManager->tracks[t];
			for (i32 c = 0; c < (i32)track.curves.size(); ++c)
			{
				// Curves share nodes, so only draw the end of the last curve
				for (i32 p = 0; p < 4; p += 3)
				{
					if (p == 3 && c != (i32)track.curves.size() - 1)
					{
						continue;
					}

					const glm::vec3& node = track.curves[c].points[p];
					if (glm::distance2(node, playerPos) > nodeDrawDistSq)
					{
						continue;
					}

					TrackPointRef ref = { (TrackID)t, c, p };
					bool bHighlighted = (ref == ctx.m_HoveredPoint) || Contains(ctx.m_GrabbedPoints, ref);
					DrawNodeMarker(node, bHighlighted ? highlightCol : nodeCol);
				}
			}
		}

		if (ctx.m_bReticleValid)
		{
			static const btVector3 placingCol(0.3f, 0.55f, 0.95f);
			static const btVector3 snappedCol(0.3f, 0.95f, 0.45f);
			static const btVector3 editingCol(0.8f, 0.3f, 0.7f);
			static const btVector3 grabbingCol(1.0f, 1.0f, 1.0f);

			btVector3 col = ctx.m_bPlacingTrack ? placingCol : editingCol;
			if (!ctx.m_GrabbedPoints.empty())
			{
				col = grabbingCol;
			}
			else if (ctx.m_HoveredPoint.IsValid())
			{
				col = snappedCol;
			}

			btTransform ringTransform(btQuaternion::getIdentity(), ToBtVec3(ctx.m_ReticlePos + VEC3_UP * 0.05f));
			debugRenderer->drawCylinder(0.5f, 0.01f, 1, ringTransform, col);
			debugRenderer->drawCylinder(0.52f, 0.01f, 1, ringTransform, col);
			debugRenderer->drawLine(ToBtVec3(ctx.m_ReticlePos), ToBtVec3(ctx.m_ReticlePos + VEC3_UP * 1.0f), col);
		}
	}

	namespace
	{
		// Adds a newly laid track to the world's stats
		void CountTrackLaid(const BezierCurveList& track)
		{
			WorldStats* stats = GetCurrentWorldStats();
			if (stats == nullptr)
			{
				return;
			}
			real length = 0.0f;
			for (BezierCurve3D curve : track.curves)
			{
				if (curve.calculatedLength < 0.0f)
				{
					curve.CalculateLength();
				}
				length += curve.calculatedLength;
			}
			stats->railLaid += length;
			stats->tracksLaid++;
		}
	} // anonymous namespace

	void Player::OnTrackBuildingPrimaryAction()
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;
		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);

		if (IsPlacingTrackPieces())
		{
			PlaceTrackPiece();
		}
		else if (ctx.m_bPlacingTrack)
		{
			if (!ctx.m_bReticleValid)
			{
				return;
			}

			// Ignore accidental double placements
			if (!ctx.m_PlacedNodes.empty() && glm::distance(ctx.m_PlacedNodes.back(), ctx.m_ReticlePos) < 0.25f)
			{
				return;
			}

			if (ctx.m_HoveredPlacedNodeIndex != -1)
			{
				// Closing a loop finishes the track
				std::vector<BezierCurveList> loopTracks;
				TrackManager::CreateLoopedTracks(ctx.m_PlacedNodes, ctx.m_StartDir, ctx.m_HoveredPlacedNodeIndex, loopTracks);
				for (const BezierCurveList& loopTrack : loopTracks)
				{
					trackManager->AddTrack(loopTrack);
					CountTrackLaid(loopTrack);
				}
				trackManager->FindJunctions();
				trackManager->SetPreviewTrack(nullptr);

				ctx.m_PlacedNodes.clear();
				ctx.m_StartDir = VEC3_ZERO;
				ctx.m_HoveredPlacedNodeIndex = -1;

				AudioManager::PlaySource(m_SoundPlaceFinalTrackNodeID);
				return;
			}

			const bool bSnappedToTrack = ctx.m_HoveredPoint.IsValid();
			const glm::vec3 snappedDir = bSnappedToTrack ? trackManager->GetNodeDirection(ctx.m_HoveredPoint) : VEC3_ZERO;

			ctx.m_PlacedNodes.push_back(ctx.m_ReticlePos);

			if (ctx.m_PlacedNodes.size() == 1)
			{
				// Starting on an existing track, line up with it
				ctx.m_StartDir = snappedDir;
				AudioManager::PlaySource(m_SoundPlaceTrackNodeID);
			}
			else if (bSnappedToTrack)
			{
				// Joining onto an existing track finishes this one
				AttemptCompleteTrack(snappedDir);
			}
			else
			{
				AudioManager::PlaySource(m_SoundPlaceTrackNodeID);
			}
		}
		else if (ctx.m_bEditingTrack)
		{
			if (!ctx.m_GrabbedPoints.empty())
			{
				ctx.m_GrabbedPoints.clear();
				ctx.m_GrabbedTrackEndDirs.clear();
				trackManager->FindJunctions();
				AudioManager::PlaySource(m_SoundPlaceTrackNodeID);
			}
			else if (ctx.m_HoveredPoint.IsValid())
			{
				// Move every track meeting at this node so junctions stay connected
				trackManager->GetCoincidentNodes(ctx.m_HoveredPoint, ctx.m_GrabbedPoints);

				// Directions where tracks meet others are captured now, as re-reading them every frame would drift while dragging
				ctx.m_GrabbedTrackEndDirs.clear();
				for (const TrackPointRef& ref : ctx.m_GrabbedPoints)
				{
					bool bAlreadyAdded = false;
					for (const GrabbedTrackEndDirs& grabbedTrack : ctx.m_GrabbedTrackEndDirs)
					{
						bAlreadyAdded = bAlreadyAdded || (grabbedTrack.trackID == ref.trackID);
					}
					if (!bAlreadyAdded)
					{
						GrabbedTrackEndDirs grabbedTrack = {};
						grabbedTrack.trackID = ref.trackID;
						trackManager->GetJunctionEndDirs(ref.trackID, grabbedTrack.startDir, grabbedTrack.endDir);
						ctx.m_GrabbedTrackEndDirs.push_back(grabbedTrack);
					}
				}
				AudioManager::PlaySource(m_SoundPlaceTrackNodeID);
			}
		}
	}

	void Player::OnTrackBuildingUndoAction()
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;
		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);

		if (IsPlacingTrackPieces())
		{
			// Removes the last piece placed (if it's still there, unbroken)
			if (!ctx.m_PlacedPieces.empty())
			{
				const BezierCurve3D piece = ctx.m_PlacedPieces.back();
				ctx.m_PlacedPieces.pop_back();
				for (i32 t = (i32)trackManager->tracks.size() - 1; t >= 0; --t)
				{
					const BezierCurveList& track = trackManager->tracks[t];
					if (track.curves.size() == 1 && track.curves[0].points[0] == piece.points[0] && track.curves[0].points[3] == piece.points[3])
					{
						trackManager->RemoveTrack((TrackID)t);
						AudioManager::PlaySource(m_SoundTrackDetachID);
						break;
					}
				}
			}
		}
		else if (ctx.m_bPlacingTrack)
		{
			if (!ctx.m_PlacedNodes.empty())
			{
				ctx.m_PlacedNodes.pop_back();
				if (ctx.m_PlacedNodes.empty())
				{
					ctx.m_StartDir = VEC3_ZERO;
				}
				AudioManager::PlaySource(m_SoundTrackDetachID);
			}
		}
		else if (ctx.m_bEditingTrack)
		{
			if (ctx.m_GrabbedPoints.empty() && ctx.m_HoveredPoint.IsValid())
			{
				TrackPointRef nodeToRemove = ctx.m_HoveredPoint;
				ctx.m_HoveredPoint = {};
				trackManager->RemoveNode(nodeToRemove);
				AudioManager::PlaySource(m_SoundTrackDetachID);
			}
		}
	}

	void Player::OnTrackBuildingDeleteTrackAction()
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;
		if (!ctx.m_bEditingTrack || !ctx.m_GrabbedPoints.empty() || ctx.m_HoveredTrackID == InvalidTrackID)
		{
			return;
		}

		TrackID trackToRemove = ctx.m_HoveredTrackID;
		ctx.m_HoveredTrackID = InvalidTrackID;
		GetSystem<TrackManager>(SystemType::TRACK_MANAGER)->RemoveTrack(trackToRemove);
		AudioManager::PlaySource(m_SoundTrackDetachID);
	}

	bool Player::AttemptCompleteTrack(const glm::vec3& endDir /* = VEC3_ZERO */)
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;
		CHECK(ctx.m_bPlacingTrack);

		if (ctx.m_PlacedNodes.size() < 2)
		{
			return false;
		}

		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
		const BezierCurveList newTrack = TrackManager::CreateTrackThroughNodes(ctx.m_PlacedNodes, ctx.m_StartDir, endDir);
		trackManager->AddTrack(newTrack);
		CountTrackLaid(newTrack);
		trackManager->FindJunctions();
		trackManager->SetPreviewTrack(nullptr);

		ctx.m_PlacedNodes.clear();
		ctx.m_StartDir = VEC3_ZERO;

		AudioManager::PlaySource(m_SoundPlaceFinalTrackNodeID);

		return true;
	}

	void Player::ToggleEditingTrack()
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;

		if (ctx.m_bPlacingTrack)
		{
			AttemptCompleteTrack();
		}
		ClearTrackBuildingState();

		ctx.m_bEditingTrack = !ctx.m_bEditingTrack;
		ctx.m_bPlacingTrack = false;
	}

	void Player::TogglePlacingTrack()
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;

		// Leaving build mode keeps whatever was placed
		if (ctx.m_bPlacingTrack)
		{
			AttemptCompleteTrack();
		}
		ClearTrackBuildingState();

		ctx.m_bPlacingTrack = !ctx.m_bPlacingTrack;
		ctx.m_bEditingTrack = false;
	}

	void Player::OnTrackRemoved(TrackID removedTrackID)
	{
		if (m_TrackRidingID != InvalidTrackID)
		{
			if (m_TrackRidingID == removedTrackID)
			{
				DetachFromTrack();
			}
			else if (m_TrackRidingID > removedTrackID)
			{
				--m_TrackRidingID;
			}
		}

		// Point references may now be stale
		m_TrackBuildingContext.m_GrabbedPoints.clear();
		m_TrackBuildingContext.m_GrabbedTrackEndDirs.clear();
		m_TrackBuildingContext.m_HoveredPoint = {};
		m_TrackBuildingContext.m_HoveredTrackID = InvalidTrackID;
		m_TrackBuildingContext.m_bPieceSnapped = false;
	}

	void Player::OnTrackCut(const TrackCut& cut)
	{
		if (m_TrackRidingID != InvalidTrackID)
		{
			TrackID trackID = m_TrackRidingID;
			real distAlongTrack = m_DistAlongTrack;
			if (cut.Remap(trackID, distAlongTrack))
			{
				m_TrackRidingID = trackID;
				m_DistAlongTrack = distAlongTrack;
			}
			else
			{
				// The track fell away beneath the player
				DetachFromTrack();
			}
		}

		// Point references may now be stale
		m_TrackBuildingContext.m_GrabbedPoints.clear();
		m_TrackBuildingContext.m_GrabbedTrackEndDirs.clear();
		m_TrackBuildingContext.m_HoveredPoint = {};
		m_TrackBuildingContext.m_HoveredTrackID = InvalidTrackID;
		m_TrackBuildingContext.m_bPieceSnapped = false;
	}

	void Player::SetTrackReticleOverride(const glm::vec3* pos)
	{
		m_TrackBuildingContext.m_bReticleOverride = (pos != nullptr);
		if (pos != nullptr)
		{
			m_TrackBuildingContext.m_ReticleOverridePos = *pos;
		}
	}

	void Player::SelectTrackPiece(i32 index)
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;
		index = glm::clamp(index, 0, (i32)TrackPieceType::_COUNT);
		if (index == ctx.m_SelectedPiece)
		{
			return;
		}

		// Leaving freeform placement keeps whatever was placed
		if (ctx.m_bPlacingTrack && !IsPlacingTrackPieces())
		{
			AttemptCompleteTrack();
			ctx.m_PlacedNodes.clear();
			ctx.m_StartDir = VEC3_ZERO;
		}
		ctx.m_SelectedPiece = index;
		ctx.m_bPieceAvailable = false;
		GetSystem<TrackManager>(SystemType::TRACK_MANAGER)->SetPreviewTrack(nullptr);
	}

	void Player::UpdateTrackPiecePlacement()
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;
		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
		const TrackPieceSettings& settings = trackManager->GetPieceSettings();
		DebugRenderer* debugRenderer = g_Renderer->GetDebugRenderer();

		ctx.m_PieceCooldown -= g_DeltaTime;
		ctx.m_bPieceAvailable = false;
		ctx.m_bPieceSnapped = false;
		if (!ctx.m_bReticleValid)
		{
			trackManager->SetPreviewTrack(nullptr);
			return;
		}

		// Continue on from a track end near the reticle, or start a new track on the ground there
		glm::vec3 startPos;
		glm::vec3 startDir;
		TrackID continuesFromTrackID = InvalidTrackID;
		if (trackManager->GetClosestFreeEnd(ctx.m_ReticlePos, ctx.m_PieceSnapRange, ctx.m_PieceSnappedEnd))
		{
			ctx.m_bPieceSnapped = true;
			startPos = ctx.m_PieceSnappedEnd.pos;
			startDir = ctx.m_PieceSnappedEnd.dir;
			continuesFromTrackID = ctx.m_PieceSnappedEnd.ref.trackID;
		}
		else
		{
			// Heading the way the player is looking (in 15 degree steps), on a grade following the ground
			glm::vec3 look = GetLookDirection();
			look.y = 0.0f;
			real yaw = glm::length2(look) > 1e-6f ? std::atan2(look.x, look.z) : 0.0f;
			const real yawStep = PI / 12.0f;
			yaw = glm::round(yaw / yawStep) * yawStep;
			const glm::vec3 heading(std::sin(yaw), 0.0f, std::cos(yaw));

			startPos = ctx.m_ReticlePos;
			real grade = 0.0f;
			glm::vec3 groundBehind, groundAhead;
			const real sampleDist = 2.0f;
			const real searchHeight = 6.0f;
			if (trackManager->FindGroundBelow(startPos - heading * sampleDist + VEC3_UP * searchHeight, searchHeight * 2.0f, groundBehind) &&
				trackManager->FindGroundBelow(startPos + heading * sampleDist + VEC3_UP * searchHeight, searchHeight * 2.0f, groundAhead))
			{
				grade = (groundAhead.y - groundBehind.y) / (sampleDist * 2.0f);
				grade = glm::clamp(glm::round(grade / settings.gradeStep) * settings.gradeStep, -settings.maxGrade, settings.maxGrade);
			}
			startDir = glm::normalize(heading + VEC3_UP * grade);
		}

		const TrackPieceType type = (TrackPieceType)ctx.m_SelectedPiece;
		const real gradeChange = (type == TrackPieceType::FOLLOW_GROUND) ? trackManager->ChooseFollowGroundGradeChange(startPos, startDir) : 0.0f;
		ctx.m_Piece = trackManager->BuildPiece(type, startPos, startDir, gradeChange);
		ctx.m_Piece.CalculateLength();
		ctx.m_PieceStartDir = startDir;
		ctx.m_PieceCheck = trackManager->CheckPiece(ctx.m_Piece, startDir, continuesFromTrackID, &ctx.m_PieceMaxCut, &ctx.m_PieceMaxFill);
		ctx.m_bPieceAvailable = true;

		BezierCurveList previewTrack(std::vector<BezierCurve3D>{ ctx.m_Piece });
		trackManager->SetPreviewTrack(&previewTrack, ctx.m_PieceCheck == TrackPieceCheck::OK);

		// Mark where the piece starts (green when continuing a track)
		const btVector3 col = ctx.m_bPieceSnapped ? btVector3(0.3f, 0.95f, 0.45f) : btVector3(0.3f, 0.55f, 0.95f);
		btTransform ringTransform(btQuaternion::getIdentity(), ToBtVec3(startPos + VEC3_UP * 0.05f));
		debugRenderer->drawCylinder(0.5f, 0.01f, 1, ringTransform, col);
		debugRenderer->drawLine(ToBtVec3(startPos), ToBtVec3(startPos + VEC3_UP * 1.0f), col);
	}

	void Player::PlaceTrackPiece()
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;
		if (!ctx.m_bPieceAvailable || ctx.m_PieceCooldown > 0.0f)
		{
			return;
		}
		if (ctx.m_PieceCheck != TrackPieceCheck::OK)
		{
			AudioManager::PlaySource(m_SoundTrackDetachID);
			return;
		}

		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);

		// Earthworks first, so the ground under the track is solid
		trackManager->ConformTerrainToTrack(ctx.m_Piece);

		const BezierCurveList newTrack(std::vector<BezierCurve3D>{ ctx.m_Piece });
		trackManager->AddTrack(newTrack);
		CountTrackLaid(newTrack);
		trackManager->FindJunctions();
		ctx.m_PlacedPieces.push_back(ctx.m_Piece);
		ctx.m_PieceCooldown = 0.2f;
		ctx.m_bPieceAvailable = false;

		AudioManager::PlaySource(m_SoundPlaceTrackNodeID);
	}

	void Player::DrawTrackBuildingHUD() const
	{
		const TrackBuildingContext& ctx = m_TrackBuildingContext;
		const bool bBuilding = ctx.m_bPlacingTrack || ctx.m_bEditingTrack;
		const bool bForkAhead = (m_TrackRidingID != InvalidTrackID) && GetSystem<TrackManager>(SystemType::TRACK_MANAGER)->IsForkAhead();
		if (!m_bPossessed || !(bBuilding || bForkAhead))
		{
			return;
		}

		auto BindingName = [](Action action)
		{
			StringBuilder name;
			if (!g_InputManager->GetActionBindingName(action, name))
			{
				return std::string(LOC(Input_Unbound));
			}
			return name.ToString();
		};

		// LMB is checked for directly in PlayerController
		const std::string primaryBinding = LOCF(Track_PrimaryBinding, BindingName(Action::INTERACT_RIGHT_HAND).c_str());

		auto PieceName = [](i32 index) -> const char*
		{
			switch ((TrackPieceType)index)
			{
			case TrackPieceType::STRAIGHT: return LOC(Track_Piece_Straight);
			case TrackPieceType::STRAIGHT_LONG: return LOC(Track_Piece_StraightLong);
			case TrackPieceType::CURVE_LEFT: return LOC(Track_Piece_CurveLeft);
			case TrackPieceType::CURVE_RIGHT: return LOC(Track_Piece_CurveRight);
			case TrackPieceType::RAMP_UP: return LOC(Track_Piece_RampUp);
			case TrackPieceType::RAMP_DOWN: return LOC(Track_Piece_RampDown);
			case TrackPieceType::S_BEND_LEFT: return LOC(Track_Piece_SBendLeft);
			case TrackPieceType::S_BEND_RIGHT: return LOC(Track_Piece_SBendRight);
			case TrackPieceType::FOLLOW_GROUND: return LOC(Track_Piece_FollowGround);
			default: return LOC(Track_Piece_Freeform);
			}
		};

		std::vector<std::string> lines;
		if (IsPlacingTrackPieces())
		{
			const i32 choiceCount = (i32)TrackPieceType::_COUNT + 1;
			lines.push_back(LOCF(Track_PieceTitle, PieceName(ctx.m_SelectedPiece), ctx.m_SelectedPiece + 1, choiceCount));
			lines.push_back(LOCF(Track_PieceChoose, PieceName((ctx.m_SelectedPiece + choiceCount - 1) % choiceCount), PieceName((ctx.m_SelectedPiece + 1) % choiceCount)));
			lines.push_back(LOCF(Track_PlacePiece, primaryBinding.c_str()));
			lines.push_back(LOCF(Track_UndoPiece, BindingName(Action::UNDO_TRACK_NODE).c_str()));
			lines.push_back(LOCF(Track_Exit, BindingName(Action::ENTER_TRACK_BUILD_MODE).c_str()));
			if (!ctx.m_bPieceAvailable)
			{
				lines.push_back(LOC(Track_PieceAimAtGround));
			}
			else
			{
				lines.push_back(ctx.m_bPieceSnapped ? LOC(Track_PieceSnapped) : LOC(Track_PieceNewTrack));
				switch (ctx.m_PieceCheck)
				{
				case TrackPieceCheck::TOO_STEEP: lines.push_back(LOC(Track_PieceTooSteep)); break;
				case TrackPieceCheck::CUT_TOO_DEEP: lines.push_back(LOC(Track_PieceCutTooDeep)); break;
				case TrackPieceCheck::FILL_TOO_DEEP: lines.push_back(LOC(Track_PieceFillTooDeep)); break;
				case TrackPieceCheck::BLOCKED_BY_TRACK: lines.push_back(LOC(Track_PieceBlocked)); break;
				default:
				{
					const real endGrade = TrackManager::GetGrade(ctx.m_Piece.GetFirstDerivativeOnCurve(1.0f));
					lines.push_back(LOCF(Track_PieceInfo, (i32)glm::round(endGrade * 100.0f), ctx.m_PieceMaxCut, ctx.m_PieceMaxFill));
				} break;
				}
			}
		}
		else if (ctx.m_bPlacingTrack)
		{
			lines.push_back(LOCF(Track_BuildingTitle, (i32)ctx.m_PlacedNodes.size()));
			lines.push_back(LOCF(Track_PieceChoose, PieceName((i32)TrackPieceType::_COUNT - 1), PieceName(0)));
			lines.push_back(LOCF(Track_PlaceNode, primaryBinding.c_str()));
			lines.push_back(LOCF(Track_UndoNode, BindingName(Action::UNDO_TRACK_NODE).c_str()));
			lines.push_back(LOCF(Track_Finish, BindingName(Action::COMPLETE_TRACK).c_str()));
			lines.push_back(LOCF(Track_FinishExit, BindingName(Action::ENTER_TRACK_BUILD_MODE).c_str()));
			if (!ctx.m_bReticleValid)
			{
				lines.push_back(LOC(Track_AimAtGround));
			}
			else if (ctx.m_HoveredPlacedNodeIndex != -1)
			{
				lines.push_back(LOC(Track_SnappedLoop));
			}
			else if (ctx.m_HoveredPoint.IsValid())
			{
				lines.push_back(ctx.m_PlacedNodes.empty() ? LOC(Track_SnappedBranch) : LOC(Track_SnappedJoin));
			}
		}
		else if (ctx.m_bEditingTrack)
		{
			lines.push_back(LOC(Track_EditingTitle));
			lines.push_back(ctx.m_GrabbedPoints.empty() ? LOCF(Track_GrabPoint, primaryBinding.c_str()) : LOCF(Track_ReleasePoint, primaryBinding.c_str()));
			lines.push_back(LOCF(Track_DeleteNode, BindingName(Action::UNDO_TRACK_NODE).c_str()));
			lines.push_back(LOCF(Track_DeleteTrack, BindingName(Action::DELETE_TRACK).c_str()));
			lines.push_back(LOCF(Track_Exit, BindingName(Action::ENTER_TRACK_EDIT_MODE).c_str()));
		}
		else
		{
			const real forkSteer = m_Controller->GetForkSteer();
			lines.push_back(forkSteer < 0.0f ? LOC(Track_ForkLeft) : forkSteer > 0.0f ? LOC(Track_ForkRight) : LOC(Track_ForkStraight));
			lines.push_back(LOCF(Track_ChooseFork, BindingName(Action::MOVE_LEFT).c_str(), BindingName(Action::MOVE_RIGHT).c_str()));
		}

		BitmapFont* font = g_Renderer->SetFont(SID("editor-02"));
		const real lineHeight = 3.2f * font->GetMetric('W')->height / (real)g_Window->GetSize().y;
		const glm::vec4 titleColour(1.0f, 0.85f, 0.4f, 1.0f);
		const glm::vec4 textColour(0.95f, 0.95f, 0.95f, 1.0f);

		// Draw bottom-up so the title sits at the top, above the quick access bar
		real yOffset = 0.13f;
		for (i32 i = (i32)lines.size() - 1; i >= 0; --i)
		{
			g_Renderer->DrawStringSS(lines[i], i == 0 ? titleColour : textColour, AnchorPoint::BOTTOM_LEFT, glm::vec2(0.03f, yOffset), 1.5f, 0.6f);
			yOffset += lineHeight;
		}
	}

	// --- Scripted test (FLEX_TRACK_TEST=1 with --test-scenes on a terrain scene, see SceneTestRunner) ---

	namespace
	{
		class TrackTestScript
		{
		public:
			enum class Step
			{
				WAIT_FOR_GROUND,
				SETTLE,
				PLACE,
				VIEW,
				RIDE,
				CART,
				DIG,
				VIEW_DIG,
				RIDE_TO_GAP,
				SAVE_LOAD,
				DONE
			};

			explicit TrackTestScript(bool bScreenshots) :
				m_bScreenshots(bScreenshots)
			{
				m_Plan = {
					TrackPieceType::FOLLOW_GROUND, TrackPieceType::RAMP_UP, TrackPieceType::STRAIGHT, TrackPieceType::CURVE_LEFT,
					TrackPieceType::FOLLOW_GROUND, TrackPieceType::S_BEND_RIGHT, TrackPieceType::CURVE_RIGHT, TrackPieceType::RAMP_DOWN,
					TrackPieceType::STRAIGHT_LONG, TrackPieceType::FOLLOW_GROUND, TrackPieceType::CURVE_RIGHT, TrackPieceType::STRAIGHT,
				};
			}

			bool IsDone() const { return m_Step == Step::DONE; }

			void Update()
			{
				if (m_Step == Step::DONE)
				{
					return;
				}

				BaseScene* scene = g_SceneManager->CurrentScene();
				Player* player = scene->GetPlayer(0);
				TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
				TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
				if (player == nullptr || terrainGenerator == nullptr)
				{
					return;
				}
				m_StepTime += g_DeltaTime;
				++m_Frame;

				switch (m_Step)
				{
				case Step::WAIT_FOR_GROUND:
				{
					if (m_Frame == 1)
					{
						g_InputManager->SetIgnoreWindowFocus(true);
						g_CameraManager->SetCameraByName("first-person", false);
						// (it'd cover the screenshots, the test runner restores it)
						*TerrainGenerator::GetShowWorldMapPtr() = false;
					}
					// (the track doesn't need the player to have landed, so carry on regardless after a while)
					const bool bLanded = !player->IsAwaitingTerrainCollision() && player->IsGrounded();
					if ((bLanded && m_StepTime > 1.0f) || m_StepTime > 10.0f)
					{
						if (!bLanded)
						{
							Print("Track test: the player hasn't landed, carrying on anyway\n");
						}
						if (!FindSlope(terrainGenerator, trackManager, player->GetTransform()->GetWorldPosition()))
						{
							PrintError("Track test: no sloped ground found nearby\n");
							NextStep(Step::DONE);
							return;
						}
						Print("Track test: starting on a %.0f%% slope at (%.1f, %.1f, %.1f)\n", m_SlopeGrade * 100.0f, m_SlopeStart.x, m_SlopeStart.y, m_SlopeStart.z);
						const glm::vec3 right = glm::normalize(glm::cross(VEC3_UP, m_SlopeHeading));
						player->TeleportOnTerrain(m_SlopeStart - m_SlopeHeading * 4.0f - right * 3.0f + VEC3_UP * 2.0f);
						NextStep(Step::SETTLE);
					}
				} break;
				case Step::SETTLE:
				{
					if ((m_StepTime > 2.0f && !player->IsAwaitingTerrainCollision()) || m_StepTime > 6.0f)
					{
						m_FirstTrackID = (TrackID)trackManager->tracks.size();
						Look(player, m_SlopeHeading, -20.0f);
						player->TogglePlacingTrack();
						NextStep(Step::PLACE);
					}
				} break;
				case Step::PLACE:
				{
					UpdatePlace(player, trackManager);
				} break;
				case Step::VIEW:
				{
					if (m_Frame == 1)
					{
						// From the side, looking at the middle of the track
						const BezierCurveList& middle = trackManager->tracks[(i32)m_FirstTrackID + m_PlacedCount / 2];
						m_ViewTarget = middle.curves[0].GetPointOnCurve(0.5f);
						PositionCamera(player, terrainGenerator, m_ViewTarget, 14.0f);
					}
					if (m_StepTime > 2.0f && !terrainGenerator->IsApplyingEdits())
					{
						if (m_SubStep == 0)
						{
							MeasureFlushness(trackManager);
							Screenshot("track_pieces");
							m_SubStep = 1;
						}
						else if (m_StepTime > 3.0f)
						{
							// Ride from the start, facing towards increasing t
							const BezierCurveList& first = trackManager->tracks[(i32)m_FirstTrackID];
							glm::vec3 dir = first.GetCurveDirectionAt(0.0f);
							Look(player, dir, -5.0f);
							player->AttachToTrack(m_FirstTrackID, 0.0f);
							g_InputManager->KeyCallback(KeyCode::KEY_W, KeyAction::KEY_PRESS, 0);
							m_VisitedTracks.clear();
							m_MaxTrackDeviation = 0.0f;
							NextStep(Step::RIDE);
						}
					}
				} break;
				case Step::RIDE:
				{
					const TrackID ridingID = player->GetTrackRidingID();
					if (ridingID == InvalidTrackID)
					{
						PrintError("Track test: fell off the track while riding\n");
						g_InputManager->KeyCallback(KeyCode::KEY_W, KeyAction::KEY_RELEASE, 0);
						NextStep(Step::CART);
						break;
					}
					if (!Contains(m_VisitedTracks, ridingID))
					{
						m_VisitedTracks.push_back(ridingID);
					}
					// The player rides half their height above the track
					i32 curveIndex;
					const glm::vec3 trackPos = trackManager->tracks[(i32)ridingID].GetPointOnCurve(player->GetDistAlongTrack(), &curveIndex);
					const glm::vec3 playerPos = player->GetTransform()->GetWorldPosition() - VEC3_UP * (player->GetHeight() * 0.5f);
					m_MaxFrameTime = glm::max(m_MaxFrameTime, g_DeltaTime);
					if (m_StepTime > 0.5f)
					{
						m_MaxTrackDeviation = glm::max(m_MaxTrackDeviation, glm::distance(trackPos, playerPos));
					}
					if (m_SubStep == 0 && m_StepTime > 0.7f)
					{
						Screenshot("track_ride");
						m_SubStep = 1;
					}
					const bool bAtEnd = (ridingID == (TrackID)((i32)m_FirstTrackID + m_PlacedCount - 1)) && player->GetDistAlongTrack() > 0.98f;
					if (bAtEnd || m_StepTime > 30.0f)
					{
						g_InputManager->KeyCallback(KeyCode::KEY_W, KeyAction::KEY_RELEASE, 0);
						std::string visited;
						for (TrackID visitedID : m_VisitedTracks)
						{
							visited += IntToString((i32)visitedID - (i32)m_FirstTrackID) + " ";
						}
						Print("Track test: rode over %d of %d pieces (%s) in %.1fs (reached the end: %d, longest frame %.0fms), max distance from the track %.2fm\n",
							(i32)m_VisitedTracks.size(), m_PlacedCount, visited.c_str(), m_StepTime, bAtEnd ? 1 : 0, m_MaxFrameTime * 1000.0f, m_MaxTrackDeviation);
						if (!bAtEnd)
						{
							PrintError("Track test: riding didn't reach the end of the track\n");
						}
						player->DetachFromTrack();
						NextStep(Step::CART);
					}
				} break;
				case Step::CART:
				{
					if (m_Frame == 1)
					{
						EngineCart* cart = new EngineCart(scene->GetUniqueObjectName("Track test cart_", 2), InvalidGameObjectID, InvalidPrefabIDPair, false);
						scene->AddRootObject(cart);
						m_CartID = cart->ID;
						m_VisitedTracks.clear();
						m_bCartMounted = false;
					}
					EngineCart* cart = (EngineCart*)m_CartID.Get();
					if (cart == nullptr)
					{
						if (m_StepTime > 1.0f)
						{
							PrintError("Track test: cart wasn't created\n");
							NextStep(Step::DIG);
						}
						break;
					}
					if (!m_bCartMounted)
					{
						cart->OnTrackMount(m_FirstTrackID, 0.01f);
						m_bCartMounted = true;
						m_StepTime = 0.0f;
					}
					if (cart->currentTrackID != InvalidTrackID)
					{
						if (!Contains(m_VisitedTracks, cart->currentTrackID))
						{
							m_VisitedTracks.push_back(cart->currentTrackID);
						}
					}
					const bool bAtEnd = cart->currentTrackID == (TrackID)((i32)m_FirstTrackID + m_PlacedCount - 1) && cart->distAlongTrack > 0.95f;
					if (bAtEnd || m_StepTime > 20.0f || cart->currentTrackID == InvalidTrackID)
					{
						Print("Track test: engine cart rolled over %d of %d pieces in %.1fs (reached the end: %d)\n", (i32)m_VisitedTracks.size(), m_PlacedCount, m_StepTime, bAtEnd ? 1 : 0);
						if ((i32)m_VisitedTracks.size() < m_PlacedCount)
						{
							PrintError("Track test: engine cart didn't make it along the whole track\n");
						}
						NextStep(Step::DIG);
					}
				} break;
				case Step::DIG:
				{
					// Dig under the middle of a piece with the cart & the player on it
					const i32 digPiece = m_PlacedCount / 2;
					const TrackID digTrackID = (TrackID)((i32)m_FirstTrackID + digPiece);
					if (m_Frame == 1)
					{
						if (EngineCart* cart = (EngineCart*)m_CartID.Get())
						{
							cart->OnTrackMount(digTrackID, 0.5f);
							cart->powerRemaining = 0.0f;
							cart->moveDirection = 0.0f;
						}
						const BezierCurveList& track = trackManager->tracks[(i32)digTrackID];
						m_DigPos = track.curves[0].GetPointOnCurve(0.5f);
						player->AttachToTrack(digTrackID, 0.45f);
						break;
					}
					if (m_Frame == 3)
					{
						m_TrackCountBeforeDig = (i32)trackManager->tracks.size();
						terrainGenerator->Dig(m_DigPos - VEC3_UP * 0.6f, 1.5f);
						const i32 trackCount = (i32)trackManager->tracks.size();
						const TrackID gapTrackID = trackManager->GetClosestTrack(m_DigPos, 0.6f);
						const EngineCart* cart = (const EngineCart*)m_CartID.Get();
						const bool bCartDerailed = cart != nullptr && cart->currentTrackID == InvalidTrackID;
						const bool bPlayerOff = player->GetTrackRidingID() == InvalidTrackID;
						Print("Track test: digging under piece %d: %d tracks -> %d, gap left: %d, cart derailed: %d, player thrown off: %d\n",
							digPiece, m_TrackCountBeforeDig, trackCount, gapTrackID == InvalidTrackID ? 1 : 0, bCartDerailed ? 1 : 0, bPlayerOff ? 1 : 0);
						if (trackCount != m_TrackCountBeforeDig + 1 || gapTrackID != InvalidTrackID)
						{
							PrintError("Track test: digging under the track didn't break it in two\n");
						}
						if (!bCartDerailed || !bPlayerOff)
						{
							PrintError("Track test: riders on the broken section stayed on it\n");
						}
						NextStep(Step::VIEW_DIG);
					}
				} break;
				case Step::VIEW_DIG:
				{
					if (m_Frame == 1)
					{
						PositionCamera(player, terrainGenerator, m_DigPos, 9.0f);
					}
					if (m_SubStep == 0 && m_StepTime > 2.5f && !terrainGenerator->IsApplyingEdits())
					{
						Screenshot("track_dug");
						m_SubStep = 1;
						m_StepTime = 0.0f;
					}
					else if (m_SubStep == 1 && m_StepTime > 0.5f)
					{
						const EngineCart* cart = (const EngineCart*)m_CartID.Get();
						if (cart != nullptr)
						{
							const glm::vec3 cartPos = cart->GetTransform()->GetWorldPosition();
							Print("Track test: derailed cart is %.2fm below the track (falling: %d)\n", m_DigPos.y - cartPos.y, cart->IsDerailed() ? 1 : 0);
						}

						// Ride from the start again: the player should stop at the break
						const BezierCurveList& first = trackManager->tracks[(i32)m_FirstTrackID];
						glm::vec3 dir = first.GetCurveDirectionAt(0.0f);
						Look(player, dir, -5.0f);
						player->AttachToTrack(m_FirstTrackID, 0.0f);
						g_InputManager->KeyCallback(KeyCode::KEY_W, KeyAction::KEY_PRESS, 0);
						NextStep(Step::RIDE_TO_GAP);
					}
				} break;
				case Step::RIDE_TO_GAP:
				{
					if (m_StepTime > 12.0f || player->GetTrackRidingID() == InvalidTrackID)
					{
						g_InputManager->KeyCallback(KeyCode::KEY_W, KeyAction::KEY_RELEASE, 0);
						const TrackID ridingID = player->GetTrackRidingID();
						const TrackID expectedID = (TrackID)((i32)m_FirstTrackID + m_PlacedCount / 2);
						const glm::vec3 playerPos = player->GetTransform()->GetWorldPosition();
						Print("Track test: riding into the break stopped on track %d at %.2f (expected %d at the end), %.1fm from the dig\n",
							(i32)ridingID, player->GetDistAlongTrack(), (i32)expectedID, glm::distance(playerPos, m_DigPos));
						if (ridingID != expectedID || player->GetDistAlongTrack() < 0.95f)
						{
							PrintError("Track test: riding into the break didn't stop at it\n");
						}
						player->DetachFromTrack();
						NextStep(Step::SAVE_LOAD);
					}
				} break;
				case Step::SAVE_LOAD:
				{
					// Through text, like world saves
					const std::vector<BezierCurveList> tracksBefore = trackManager->tracks;
					const i32 junctionsBefore = (i32)trackManager->junctions.size();
					const std::string saved = trackManager->Serialize().ToString();
					JSONObject parsed;
					bool bOk = JSONParser::Parse(saved, parsed);
					if (bOk)
					{
						trackManager->InitializeFromJSON(parsed);
						bOk = trackManager->tracks.size() == tracksBefore.size() && (i32)trackManager->junctions.size() == junctionsBefore;
						for (u32 t = 0; bOk && t < tracksBefore.size(); ++t)
						{
							bOk = trackManager->tracks[t].curves.size() == tracksBefore[t].curves.size();
							for (u32 c = 0; bOk && c < tracksBefore[t].curves.size(); ++c)
							{
								for (i32 p = 0; p < 4; ++p)
								{
									bOk = bOk && glm::distance(trackManager->tracks[t].curves[c].points[p], tracksBefore[t].curves[c].points[p]) < 1e-3f;
								}
							}
						}
					}
					Print("Track test: save & load of %d tracks (%u bytes) %s\n", (i32)tracksBefore.size(), (u32)saved.size(), bOk ? "matched" : "DIFFERED");
					if (!bOk)
					{
						PrintError("Track test: tracks changed when saved & loaded\n");
					}
					NextStep(Step::DONE);
				} break;
				default:
					break;
				}
			}

		private:
			void NextStep(Step step)
			{
				m_Step = step;
				m_StepTime = 0.0f;
				m_Frame = 0;
				m_SubStep = 0;
			}

			void UpdatePlace(Player* player, TrackManager* trackManager)
			{
				const TrackPieceSettings& settings = trackManager->GetPieceSettings();
				if (m_PlanIndex >= (i32)m_Plan.size())
				{
					// Checks which should refuse placement: climbing past the max grade
					const glm::vec3 steepDir = glm::normalize(m_SlopeHeading + VEC3_UP * settings.maxGrade);
					const glm::vec3 steepStart = trackManager->tracks.back().curves[0].points[3];
					const BezierCurve3D steepPiece = trackManager->BuildPiece(TrackPieceType::RAMP_UP, steepStart, steepDir);
					const TrackPieceCheck steepCheck = trackManager->CheckPiece(steepPiece, steepDir, InvalidTrackID);
					if (steepCheck != TrackPieceCheck::TOO_STEEP)
					{
						PrintError("Track test: a ramp past the max grade wasn't refused (%d)\n", (i32)steepCheck);
					}

					Print("Track test: placed %d of %d pieces (%d refused)\n", m_PlacedCount, (i32)m_Plan.size(), m_RefusedCount);
					if (m_PlacedCount < (i32)m_Plan.size() - 2)
					{
						PrintError("Track test: too many pieces couldn't be placed\n");
					}
					player->SetTrackReticleOverride(nullptr);
					player->TogglePlacingTrack();
					NextStep(m_PlacedCount >= 3 ? Step::VIEW : Step::DONE);
					return;
				}

				// Give the player a few frames to update the preview, then place it
				if (m_Frame == 1)
				{
					player->SelectTrackPiece((i32)m_Plan[m_PlanIndex]);
					glm::vec3 reticle = m_SlopeStart;
					if (m_PlacedCount > 0)
					{
						reticle = trackManager->tracks.back().curves[0].points[3];
					}
					player->SetTrackReticleOverride(&reticle);
				}
				else if (m_Frame >= 4 && m_StepTime > 0.3f)
				{
					// The previews (blue, or red when refused), seen from where the player stands
					if (m_PlanIndex == 1 && m_PreviewShots == 0)
					{
						Screenshot("track_preview");
						++m_PreviewShots;
						return;
					}
					if (!player->IsTrackPieceValid() && m_RefusedShots == 0)
					{
						Look(player, trackManager->tracks.back().curves[0].points[3] - player->GetTransform()->GetWorldPosition(), -15.0f);
						Screenshot("track_preview_refused");
						++m_RefusedShots;
						return;
					}
					if (player->IsTrackPieceValid())
					{
						const i32 trackCountBefore = (i32)trackManager->tracks.size();
						player->OnTrackBuildingPrimaryAction();
						if ((i32)trackManager->tracks.size() == trackCountBefore + 1)
						{
							++m_PlacedCount;
							const BezierCurve3D& piece = trackManager->tracks.back().curves[0];
							Print("Track test: placed piece %d (type %d), %.1fm, ends at grade %.0f%%\n", m_PlacedCount, (i32)m_Plan[m_PlanIndex],
								piece.calculatedLength, TrackManager::GetGrade(piece.GetFirstDerivativeOnCurve(1.0f)) * 100.0f);
						}
						else
						{
							PrintError("Track test: placing a valid piece didn't add a track\n");
						}
					}
					else
					{
						++m_RefusedCount;
						Print("Track test: piece %d (type %d) refused here\n", m_PlanIndex, (i32)m_Plan[m_PlanIndex]);
					}
					++m_PlanIndex;
					m_Frame = 0;
					m_StepTime = 0.0f;
				}
			}

			// Flat enough to build on, but sloped
			bool FindSlope(TerrainGenerator* terrainGenerator, TrackManager* trackManager, const glm::vec3& center)
			{
				real bestScore = 1e30f;
				const real run = 16.0f;
				for (i32 ring = 1; ring <= 8; ++ring)
				{
					for (i32 i = 0; i < 16; ++i)
					{
						const real angle = TWO_PI * (real)i / 16.0f;
						const glm::vec3 pos = center + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * (real)ring * 12.0f;
						for (i32 d = 0; d < 8; ++d)
						{
							const real headingAngle = TWO_PI * (real)d / 8.0f;
							const glm::vec3 heading(std::sin(headingAngle), 0.0f, std::cos(headingAngle));
							glm::vec3 a, b, mid;
							if (!GroundAt(terrainGenerator, trackManager, pos, a) ||
								!GroundAt(terrainGenerator, trackManager, pos + heading * run * 0.5f, mid) ||
								!GroundAt(terrainGenerator, trackManager, pos + heading * run, b))
							{
								continue;
							}
							const real grade = (b.y - a.y) / run;
							const real bumpiness = glm::abs(mid.y - (a.y + b.y) * 0.5f);
							if (grade < 0.06f || grade > 0.3f)
							{
								continue;
							}
							const real score = glm::abs(grade - 0.15f) + bumpiness * 0.2f + ring * 0.01f;
							if (score < bestScore)
							{
								bestScore = score;
								m_SlopeStart = a;
								m_SlopeHeading = heading;
								m_SlopeGrade = grade;
							}
						}
					}
				}
				return bestScore < 1e30f;
			}

			static bool GroundAt(TerrainGenerator* terrainGenerator, TrackManager* trackManager, const glm::vec3& pos, glm::vec3& outGround)
			{
				const real height = terrainGenerator->SampleHeight(glm::vec2(pos.x, pos.z));
				if (!trackManager->FindGroundBelow(glm::vec3(pos.x, height + 30.0f, pos.z), 80.0f, outGround))
				{
					return false;
				}
				const terrain::ColumnSample column = terrainGenerator->SampleColumn(pos.x, pos.z);
				return !(column.lakeID != 0 && column.lakeT < 1.0f && outGround.y < column.waterLevel + 0.5f);
			}

			// Stands the player off to the side of target, looking at it
			static void PositionCamera(Player* player, TerrainGenerator* terrainGenerator, const glm::vec3& target, real distance)
			{
				FLEX_UNUSED(terrainGenerator);
				TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
				// (above the track to one side if there's nowhere better)
				glm::vec3 bestPos = target + VEC3_RIGHT * distance * 0.5f + VEC3_UP * 6.0f;
				real bestHeight = -1e30f;
				for (i32 i = 0; i < 16; ++i)
				{
					const real angle = TWO_PI * (real)i / 16.0f;
					const glm::vec3 candidate = target + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * distance;
					glm::vec3 ground;
					// The highest spot not far above the target, for a view down onto the track
					if (trackManager->FindGroundBelow(candidate + VEC3_UP * 40.0f, 80.0f, ground) && ground.y > target.y - 1.0f && ground.y < target.y + 8.0f &&
						ground.y > bestHeight && trackManager->GetClosestTrack(ground, 3.0f) == InvalidTrackID)
					{
						bestHeight = ground.y;
						bestPos = ground + VEC3_UP * 1.0f;
					}
				}
				player->TeleportOnTerrain(bestPos, true, false);
				const glm::vec3 toTarget = target - (bestPos + VEC3_UP * player->GetHeight() * 0.4f);
				const real horizontal = glm::length(glm::vec2(toTarget.x, toTarget.z));
				Look(player, toTarget, glm::degrees(std::atan2(toTarget.y, horizontal)));
			}

			static void Look(Player* player, glm::vec3 horizontalDir, real pitchDegrees)
			{
				horizontalDir.y = 0.0f;
				horizontalDir = glm::length2(horizontalDir) > 1e-8f ? glm::normalize(horizontalDir) : VEC3_FORWARD;
				const real yaw = std::atan2(glm::cross(VEC3_FORWARD, horizontalDir).y, glm::dot(VEC3_FORWARD, horizontalDir));
				player->GetTransform()->SetWorldRotation(glm::angleAxis(yaw, VEC3_UP));
				// The pitch's sign convention isn't assumed: find which way makes the view look up
				player->AddToPitch(-player->GetPitch() + 0.1f);
				const real upSign = player->GetLookDirection().y > 0.0f ? 1.0f : -1.0f;
				player->AddToPitch(-player->GetPitch() + upSign * glm::radians(pitchDegrees));
			}

			// How far the ground is from the bottom of the sleepers along the placed pieces: the terrain's density, & its
			// (marching cubes) mesh, which is what's seen
			void MeasureFlushness(TrackManager* trackManager)
			{
				btDiscreteDynamicsWorld* world = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
				real sum[2] = { 0.0f, 0.0f };
				real maxError[2] = { 0.0f, 0.0f };
				i32 count[2] = { 0, 0 };
				for (i32 i = 0; i < m_PlacedCount; ++i)
				{
					const BezierCurve3D& curve = trackManager->tracks[(i32)m_FirstTrackID + i].curves[0];
					for (i32 s = 0; s <= 8; ++s)
					{
						const glm::vec3 pos = curve.GetPointOnCurve((real)s / 8.0f);
						for (i32 k = 0; k < 2; ++k)
						{
							glm::vec3 ground;
							bool bHit = false;
							if (k == 0)
							{
								bHit = trackManager->FindGroundBelow(pos + VEC3_UP * 2.0f, 6.0f, ground);
							}
							else
							{
								const btVector3 rayStart = ToBtVec3(pos + VEC3_UP * 2.0f);
								const btVector3 rayEnd = ToBtVec3(pos - VEC3_UP * 4.0f);
								btCollisionWorld::ClosestRayResultCallback rayCallback(rayStart, rayEnd);
								rayCallback.m_collisionFilterMask = (i32)CollisionType::STATIC;
								world->rayTest(rayStart, rayEnd, rayCallback);
								bHit = rayCallback.hasHit();
								ground = ToVec3(rayCallback.m_hitPointWorld);
							}
							if (bHit)
							{
								const real error = glm::abs(pos.y - ground.y);
								sum[k] += error;
								maxError[k] = glm::max(maxError[k], error);
								++count[k];
							}
						}
					}
				}
				Print("Track test: track base to ground: terrain density avg %.3fm (max %.3fm, %d samples), collision mesh avg %.3fm (max %.3fm, %d samples)\n",
					count[0] > 0 ? sum[0] / count[0] : -1.0f, maxError[0], count[0], count[1] > 0 ? sum[1] / count[1] : -1.0f, maxError[1], count[1]);
				if (maxError[0] > 0.3f || maxError[1] > 0.6f || count[0] == 0)
				{
					PrintError("Track test: track isn't flush with the ground\n");
				}
			}

			void Screenshot(const char* name)
			{
				if (!m_bScreenshots)
				{
					return;
				}
				const std::string directory = RelativePathToAbsolute(SCREENSHOT_DIRECTORY);
				Platform::CreateDirectoryRecursive(directory);
				g_Renderer->RequestScreenshot(directory + "test_" + name + ".png");
			}

			bool m_bScreenshots = false;
			Step m_Step = Step::WAIT_FOR_GROUND;
			sec m_StepTime = 0.0f;
			i32 m_Frame = 0;
			i32 m_SubStep = 0;

			glm::vec3 m_SlopeStart = VEC3_ZERO;
			glm::vec3 m_SlopeHeading = VEC3_FORWARD;
			real m_SlopeGrade = 0.0f;

			std::vector<TrackPieceType> m_Plan;
			i32 m_PlanIndex = 0;
			i32 m_PlacedCount = 0;
			i32 m_RefusedCount = 0;
			i32 m_PreviewShots = 0;
			i32 m_RefusedShots = 0;
			TrackID m_FirstTrackID = InvalidTrackID;

			glm::vec3 m_ViewTarget = VEC3_ZERO;
			std::vector<TrackID> m_VisitedTracks;
			real m_MaxTrackDeviation = 0.0f;
			sec m_MaxFrameTime = 0.0f;
			GameObjectID m_CartID = InvalidGameObjectID;
			bool m_bCartMounted = false;
			glm::vec3 m_DigPos = VEC3_ZERO;
			i32 m_TrackCountBeforeDig = 0;
		};

		TrackTestScript* s_TrackTest = nullptr;
	} // anonymous namespace

	void BeginTrackTest(bool bScreenshots)
	{
		delete s_TrackTest;
		s_TrackTest = new TrackTestScript(bScreenshots);
	}

	void UpdateTrackTest()
	{
		if (s_TrackTest != nullptr)
		{
			s_TrackTest->Update();
		}
	}

	bool EndTrackTest()
	{
		const bool bDone = s_TrackTest == nullptr || s_TrackTest->IsDone();
		delete s_TrackTest;
		s_TrackTest = nullptr;
		return bDone;
	}
} // namespace flex
