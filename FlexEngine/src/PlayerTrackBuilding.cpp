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
#include "FlexEngine.hpp"
#include "Graphics/BitmapFont.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "InputManager.hpp"
#include "Localization.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "PlayerController.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
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
				m_Transform.SetWorldPosition(m_Transform.GetWorldPosition() + trackRight * side * hopDist + VEC3_UP * 0.25f);
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
		world->rayTest(rayStart, rayEnd, rayCallback);
		if (rayCallback.hasHit())
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

		if (!ctx.m_bReticleValid)
		{
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

		UpdateTrackBuildingReticle();

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

		if (ctx.m_bPlacingTrack)
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

		if (ctx.m_bPlacingTrack)
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

		std::vector<std::string> lines;
		if (ctx.m_bPlacingTrack)
		{
			lines.push_back(LOCF(Track_BuildingTitle, (i32)ctx.m_PlacedNodes.size()));
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

		// Draw bottom-up so the title sits at the top
		real yOffset = 0.05f;
		for (i32 i = (i32)lines.size() - 1; i >= 0; --i)
		{
			g_Renderer->DrawStringSS(lines[i], i == 0 ? titleColour : textColour, AnchorPoint::BOTTOM_LEFT, glm::vec2(0.03f, yOffset), 1.5f, 0.6f);
			yOffset += lineHeight;
		}
	}
} // namespace flex
