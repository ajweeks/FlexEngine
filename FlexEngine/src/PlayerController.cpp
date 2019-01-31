#include "stdafx.hpp"

#include "PlayerController.hpp"

#pragma warning(push, 0)
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <LinearMath/btIDebugDraw.h>
#pragma warning(pop)

#include "Audio/AudioManager.hpp"
#include "Cameras/CameraManager.hpp"
#include "Cameras/OverheadCamera.hpp"
#include "Cameras/FirstPersonCamera.hpp"
#include "Graphics/Renderer.hpp"
#include "InputManager.hpp"
#include "Physics/PhysicsTypeConversions.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	PlayerController::PlayerController()
	{
	}

	PlayerController::~PlayerController()
	{
	}

	void PlayerController::Initialize(Player* player)
	{
		m_Player = player;
		m_PlayerIndex = m_Player->GetIndex();

		assert(m_PlayerIndex == 0 ||
			   m_PlayerIndex == 1);

		m_Player->UpdateIsPossessed();
	}

	void PlayerController::Destroy()
	{
	}

	void PlayerController::Update()
	{
		// TODO: Make frame-rate-independent!

		m_Player->GetRigidBody()->UpdateParentTransform();

		btRigidBody* rb = m_Player->GetRigidBody()->GetRigidBodyInternal();
		btVector3 rbPos = rb->getInterpolationWorldTransform().getOrigin();

		Transform* transform = m_Player->GetTransform();
		glm::vec3 up = transform->GetUp();
		glm::vec3 right = transform->GetRight();
		glm::vec3 forward = transform->GetForward();
		TrackManager* trackManager = g_SceneManager->CurrentScene()->GetTrackManager();
		TrackID pTrackRidingID = m_Player->m_TrackRidingID;
		bool bWasFacingDownTrack = m_Player->IsFacingDownTrack();

		if (m_Player->m_bPossessed)
		{
			if (g_InputManager->IsGamepadButtonDown(m_PlayerIndex, GamepadButton::BACK))
			{
				ResetTransformAndVelocities();
				return;
			}

			if (g_InputManager->GetActionPressed(Action::INTERACT))
			{
				if (m_Player->m_TrackRidingID == InvalidTrackID)
				{
					real distAlongTrack = -1.0f;
					TrackID trackInRangeIndex = trackManager->GetTrackInRangeID(transform->GetWorldPosition(), m_Player->m_TrackAttachMinDist, &distAlongTrack);
					if (trackInRangeIndex != -1)
					{
						m_Player->AttachToTrack(trackInRangeIndex, distAlongTrack);

						SnapPosToTrack(m_Player->m_DistAlongTrack, false);
					}
				}
				else
				{
					m_Player->DetachFromTrack();
				}
			}
		}

		btVector3 force(0.0f, 0.0f, 0.0f);

		m_Player->UpdateIsGrounded();

		if (m_Player->m_bPossessed)
		{
			if (m_Player->m_TrackRidingID != InvalidTrackID)
			{
				real moveForward = g_InputManager->GetActionAxisValue(Action::MOVE_FORWARD);
				real moveBackward = g_InputManager->GetActionAxisValue(Action::MOVE_BACKWARD);

				glm::vec3 newCurveDir = trackManager->GetTrack(m_Player->m_TrackRidingID)->GetCurveDirectionAt(m_Player->m_DistAlongTrack);
				static glm::vec3 pCurveDir = newCurveDir;

				if (!m_Player->IsFacingDownTrack())
				{
					moveForward = -moveForward;
					moveBackward = -moveBackward;
				}

				const bool bReversing = (moveBackward < 0.0f);

				real targetDDist = (moveForward + moveBackward) * m_Player->m_TrackMoveSpeed * g_DeltaTime;
				real pDist = m_Player->m_DistAlongTrack;
				m_Player->m_DistAlongTrack = trackManager->AdvanceTAlongTrack(m_Player->m_TrackRidingID,
					targetDDist, m_Player->m_DistAlongTrack);
				SnapPosToTrack(pDist, bReversing);

				if (m_Player->IsFacingDownTrack() != bWasFacingDownTrack &&
					m_Player->m_TrackRidingID == pTrackRidingID)
				{
					AudioManager::PlaySource(m_Player->m_SoundTrackSwitchDirID);
				}

				m_Player->m_pDTrackMovement = m_Player->m_DistAlongTrack - pDist;
				pCurveDir = newCurveDir;
			}
			else if (!m_Player->GetObjectInteractingWith())
			{
				real moveForward = g_InputManager->GetActionAxisValue(Action::MOVE_FORWARD);
				real moveBackward = g_InputManager->GetActionAxisValue(Action::MOVE_BACKWARD);
				real moveLeft = g_InputManager->GetActionAxisValue(Action::MOVE_LEFT);
				real moveRight = g_InputManager->GetActionAxisValue(Action::MOVE_RIGHT);

				real moveH = moveLeft + moveRight;
				real moveV = moveBackward + moveForward;

				force += ToBtVec3(transform->GetRight()) * m_MoveAcceleration * moveH;
				force += ToBtVec3(transform->GetForward()) * m_MoveAcceleration * moveV;
			}
		}

		btIDebugDraw* debugDrawer = g_Renderer->GetDebugDrawer();

		bool bDrawLocalAxes = (m_Mode != Mode::FIRST_PERSON);
		if (bDrawLocalAxes)
		{
			const real lineLength = 4.0f;
			debugDrawer->drawLine(rbPos, rbPos + ToBtVec3(up) * lineLength, btVector3(0.0f, 1.0f, 0.0f));
			debugDrawer->drawLine(rbPos, rbPos + ToBtVec3(forward) * lineLength, btVector3(0.0f, 0.0f, 1.0f));
			debugDrawer->drawLine(rbPos, rbPos + ToBtVec3(right) * lineLength, btVector3(1.0f, 0.0f, 0.0f));
		}

		btQuaternion orientation = rb->getOrientation();
		glm::vec3 euler = glm::eulerAngles(ToQuaternion(orientation));

		rb->applyCentralForce(force);

		const btVector3& vel = rb->getLinearVelocity();
		btVector3 xzVel(vel.getX(), 0, vel.getZ());
		real xzVelMagnitude = xzVel.length();
		bool bMaxVel = false;
		if (xzVelMagnitude > m_MaxMoveSpeed)
		{
			btVector3 xzVelNorm = xzVel.normalized();
			btVector3 newVel(xzVelNorm.getX() * m_MaxMoveSpeed, vel.getY(), xzVelNorm.getZ() * m_MaxMoveSpeed);;
			rb->setLinearVelocity(newVel);
			xzVelMagnitude = m_MaxMoveSpeed;
			bMaxVel = true;
		}

		// Jitter tester:
		//m_Player->GetTransform()->SetWorldPosition(m_Player->GetTransform()->GetWorldPosition() + glm::vec3(g_DeltaTime * 1.0f, 0.0f, 0.0f));

		bool bDrawVelocity = false;
		if (bDrawVelocity)
		{
			real scale = 1.0f;
			btVector3 start = rbPos + btVector3(0.0f, -0.5f, 0.0f);
			btVector3 end = start + rb->getLinearVelocity() * scale;
			debugDrawer->drawLine(start, end, bMaxVel ? btVector3(0.9f, 0.3f, 0.4f) : btVector3(0.1f, 0.85f, 0.98f));
		}

		if (m_Player->m_bPossessed)
		{
			real lookH = g_InputManager->GetActionAxisValue(Action::LOOK_LEFT) + g_InputManager->GetActionAxisValue(Action::LOOK_RIGHT);
			if (m_Player->IsRidingTrack())
			{
				lookH += g_InputManager->GetActionAxisValue(Action::MOVE_LEFT) + g_InputManager->GetActionAxisValue(Action::MOVE_RIGHT);
			}
			real lookV = 0.0f;
			if (m_Mode == Mode::FIRST_PERSON)
			{
				lookV = g_InputManager->GetActionAxisValue(Action::LOOK_DOWN) + g_InputManager->GetActionAxisValue(Action::LOOK_UP);
			}

			glm::quat rot = transform->GetLocalRotation();
			real angle = -lookH * m_RotateHSpeed * g_DeltaTime;
			rot = glm::rotate(rot, angle, up);
			transform->SetWorldRotation(rot);

			m_Player->AddToPitch(lookV * m_RotateVSpeed * g_DeltaTime);
		}
	}

	void PlayerController::ResetTransformAndVelocities()
	{
		btRigidBody* rb = m_Player->GetRigidBody()->GetRigidBodyInternal();

		rb->clearForces();
		rb->setLinearVelocity(btVector3(0, 0, 0));
		rb->setAngularVelocity(btVector3(0, 0, 0));
		btTransform identity = btTransform::getIdentity();
		identity.setOrigin(btVector3(0, 5, 0));
		rb->setWorldTransform(identity);
	}

	void PlayerController::DrawImGuiObjects()
	{
		const std::string treeName = "Player Controller " + IntToString(m_PlayerIndex);
		if (ImGui::TreeNode(treeName.c_str()))
		{
			ImGui::Text("Seconds attempting to turn: %.5f", m_SecondsAttemptingToTurn);
			ImGui::Text("Turning dir: %s", m_DirTurning == TurningDir::LEFT ? "left" : m_DirTurning == TurningDir::RIGHT ? "right" : "none");

			ImGui::TreePop();
		}
	}

	void PlayerController::SnapPosToTrack(real pDistAlongTrack, bool bReversingDownTrack)
	{
		TrackManager* trackManager = g_SceneManager->CurrentScene()->GetTrackManager();
		TrackID newTrackID = m_Player->m_TrackRidingID;
		real newDistAlongTrack = m_Player->m_DistAlongTrack;
		LookDirection desiredDir = LookDirection::CENTER;
		const real lookH = g_InputManager->GetActionAxisValue(Action::LOOK_LEFT) + g_InputManager->GetActionAxisValue(Action::LOOK_RIGHT);
		const real lookV = g_InputManager->GetActionAxisValue(Action::LOOK_DOWN) + g_InputManager->GetActionAxisValue(Action::LOOK_UP);

		if (lookH > 0.5f)
		{
			desiredDir = LookDirection::RIGHT;
		}
		else if (lookH < -0.5f)
		{
			desiredDir = LookDirection::LEFT;
		}

		trackManager->UpdatePreview(m_Player->m_TrackRidingID, m_Player->m_DistAlongTrack,
			desiredDir, m_Player->GetTransform()->GetForward(), m_Player->IsFacingDownTrack(),
			bReversingDownTrack);

		i32 newJunctionIndex = -1;
		i32 newCurveIndex = -1;
		TrackState newTrackState = TrackState::_NONE;
		glm::vec3 newPos = trackManager->GetPointOnTrack(m_Player->m_TrackRidingID, m_Player->m_DistAlongTrack,
			pDistAlongTrack, desiredDir, bReversingDownTrack, &newTrackID, &newDistAlongTrack,
			&newJunctionIndex, &newCurveIndex, &newTrackState, false);

		bool bSwitchedTracks = (newTrackID != InvalidTrackID) && (newTrackID != m_Player->m_TrackRidingID);
		if (bSwitchedTracks)
		{
			m_SecondsAttemptingToTurn = 0.0f;
			m_DirTurning = TurningDir::NONE;

			m_Player->m_TrackRidingID = newTrackID;
			m_Player->m_DistAlongTrack = newDistAlongTrack;

			if (newTrackState != TrackState::_NONE)
			{
				m_Player->m_TrackState = newTrackState;
			}
		}

		newPos += glm::vec3(0.0f, m_Player->m_Height / 2.0f, 0.0f);
		m_Player->GetTransform()->SetWorldPosition(newPos);

		real moveH = g_InputManager->GetActionAxisValue(Action::MOVE_LEFT) + g_InputManager->GetActionAxisValue(Action::MOVE_RIGHT);

		bool bTurningRight = moveH > m_TurnStartStickXThreshold;
		bool bTurningLeft = moveH < -m_TurnStartStickXThreshold;
		m_DirTurning = bTurningRight ? TurningDir::RIGHT : bTurningLeft ? TurningDir::LEFT : TurningDir::NONE;

		if (m_DirTurning != TurningDir::NONE || m_SecondsAttemptingToTurn < 0.0f)
		{
			m_SecondsAttemptingToTurn += g_DeltaTime;
		}
		else
		{
			if (m_SecondsAttemptingToTurn > 0.0f)
			{
				m_SecondsAttemptingToTurn = 0.0f;
			}
		}

		if (m_SecondsAttemptingToTurn > m_AttemptToTurnTimeThreshold)
		{
			m_SecondsAttemptingToTurn = -m_TurnAroundCooldown;
			m_DirTurning = TurningDir::NONE;
			m_Player->BeginTurnTransition();
		}
	}

	void PlayerController::UpdateMode()
	{
		BaseCamera* cam = g_CameraManager->CurrentCamera();
		FirstPersonCamera* fpCam = dynamic_cast<FirstPersonCamera*>(cam);
		m_Mode = (fpCam == nullptr) ? Mode::THIRD_PERSON : Mode::FIRST_PERSON;
	}

} // namespace flex
