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

		BaseCamera* cam = g_CameraManager->CurrentCamera();
		FirstPersonCamera* fpCam = dynamic_cast<FirstPersonCamera*>(cam);
		m_Mode = (fpCam == nullptr) ? Mode::THIRD_PERSON : Mode::FIRST_PERSON;
	}

	void PlayerController::Destroy()
	{
	}

	void PlayerController::Update()
	{
		m_Player->GetRigidBody()->UpdateParentTransform();

		if (g_InputManager->GetKeyPressed(Input::KeyCode::KEY_EQUAL) ||
			g_InputManager->IsGamepadButtonPressed(m_PlayerIndex, Input::GamepadButton::RIGHT_BUMPER))
		{
			g_CameraManager->SetActiveIndexRelative(1, false);
		}
		else if (g_InputManager->GetKeyPressed(Input::KeyCode::KEY_MINUS) ||
			g_InputManager->IsGamepadButtonPressed(m_PlayerIndex, Input::GamepadButton::LEFT_BUMPER))
		{
			g_CameraManager->SetActiveIndexRelative(-1, false);
		}
		// TODO: Make frame-rate-independent!

		btRigidBody* rb = m_Player->GetRigidBody()->GetRigidBodyInternal();
		btVector3 rbPos = rb->getInterpolationWorldTransform().getOrigin();

		Transform* transform = m_Player->GetTransform();
		glm::vec3 up = transform->GetUp();
		glm::vec3 right = transform->GetRight();
		glm::vec3 forward = transform->GetForward();
		BezierCurveList* pTrackRiding = m_Player->m_TrackRiding;
		bool bWasFacingDownTrack = m_Player->IsFacingDownTrack();

		if (m_Player->m_bPossessed)
		{
			if (g_InputManager->IsGamepadButtonDown(m_PlayerIndex, Input::GamepadButton::BACK))
			{
				ResetTransformAndVelocities();
				return;
			}
			else if (g_InputManager->IsGamepadButtonPressed(m_PlayerIndex, Input::GamepadButton::X) ||
				(g_InputManager->bPlayerUsingKeyboard[m_PlayerIndex] && g_InputManager->GetKeyPressed(Input::KeyCode::KEY_Z)))
			{
				if (m_Player->m_TrackRiding)
				{
					m_Player->DetachFromTrack();
				}
				else
				{
					real distAlongTrack = -1.0f;
					TrackManager* trackManager = g_SceneManager->CurrentScene()->GetTrackManager();
					i32 trackInRangeIndex = trackManager->GetTrackInRangeIndex(transform->GetWorldPosition(), m_Player->m_TrackAttachMinDist, distAlongTrack);
					if (trackInRangeIndex != -1)
					{
						m_Player->AttachToTrack(&trackManager->m_Tracks[trackInRangeIndex], distAlongTrack);

						SnapPosToTrack(m_Player->m_DistAlongTrack, 0.0f, 0.0f);
					}
				}
			}
		}

		btVector3 force(0.0f, 0.0f, 0.0f);

		m_Player->UpdateIsGrounded();

		if (m_Player->m_bPossessed)
		{
			if (m_Player->m_TrackRiding)
			{
				real moveForward = 0.0f;
				real moveBackward = 0.0f;
				if (g_InputManager->bPlayerUsingKeyboard[m_PlayerIndex])
				{
					moveForward = g_InputManager->GetKeyDown(Input::KeyCode::KEY_W) ? 1.0f : moveForward;
					moveBackward = g_InputManager->GetKeyDown(Input::KeyCode::KEY_S) ? 1.0f : moveBackward;
				}
				else
				{
					moveForward = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, Input::GamepadAxis::RIGHT_TRIGGER);
					moveBackward = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, Input::GamepadAxis::LEFT_TRIGGER);
				}

				glm::vec3 newCurveDir = m_Player->m_TrackRiding->GetCurveDirectionAt(m_Player->m_DistAlongTrack);
				static glm::vec3 pCurveDir = newCurveDir;

				const real oMoveForward = moveForward;
				const real oMoveBackward = moveBackward;
				if (m_Player->IsFacingDownTrack())
				{
					moveForward = 1.0f - moveForward;
					moveBackward = 1.0f - moveBackward;
				}

				real pDist = m_Player->m_DistAlongTrack;
				TrackManager* trackManager = g_SceneManager->CurrentScene()->GetTrackManager();
				m_Player->m_DistAlongTrack = trackManager->AdvanceTAlongTrack(m_Player->m_TrackRiding,
					(moveForward - moveBackward) * m_Player->m_TrackMoveSpeed * g_DeltaTime, m_Player->m_DistAlongTrack);
				SnapPosToTrack(pDist, oMoveForward, oMoveBackward);

				if (m_Player->IsFacingDownTrack() != bWasFacingDownTrack &&
					m_Player->m_TrackRiding == pTrackRiding)
				{
					AudioManager::PlaySource(m_Player->m_SoundTrackSwitchDirID);
				}

				m_Player->m_pDTrackMovement = m_Player->m_DistAlongTrack - pDist;
				pCurveDir = newCurveDir;
			}
			else if (!m_Player->GetObjectInteractingWith())
			{
				real moveH = 0.0f;
				real moveV = 0.0f;

				if (g_InputManager->bPlayerUsingKeyboard[m_PlayerIndex])
				{
					moveH = g_InputManager->GetKeyDown(Input::KeyCode::KEY_D) > 0 ? 1.0f :
						g_InputManager->GetKeyDown(Input::KeyCode::KEY_A) > 0 ? -1.0f : 0.0f;
					moveV = g_InputManager->GetKeyDown(Input::KeyCode::KEY_W) > 0 ? -1.0f :
						g_InputManager->GetKeyDown(Input::KeyCode::KEY_S) > 0 ? 1.0f : 0.0f;
				}
				else
				{
					moveH = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, Input::GamepadAxis::LEFT_STICK_X);
					moveV = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, Input::GamepadAxis::LEFT_STICK_Y);
				}


				force += ToBtVec3(transform->GetRight()) * m_MoveAcceleration * -moveH;
				force += ToBtVec3(transform->GetForward()) * m_MoveAcceleration * -moveV;
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
			real lookH = 0.0f;
			real lookV = 0.0f;

			if (g_InputManager->bPlayerUsingKeyboard[m_PlayerIndex])
			{
				lookH = g_InputManager->GetKeyDown(Input::KeyCode::KEY_RIGHT) > 0 ? 1.0f :
					g_InputManager->GetKeyDown(Input::KeyCode::KEY_LEFT) > 0 ? -1.0f : 0.0f;
				if (m_Mode == Mode::FIRST_PERSON)
				{
					lookV = g_InputManager->GetKeyDown(Input::KeyCode::KEY_UP) > 0 ? -1.0f :
						g_InputManager->GetKeyDown(Input::KeyCode::KEY_DOWN) > 0 ? 1.0f : 0.0f;
				}
			}
			else
			{
				lookH = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, Input::GamepadAxis::RIGHT_STICK_X);
				if (m_Mode == Mode::FIRST_PERSON)
				{
					lookV = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, Input::GamepadAxis::RIGHT_STICK_Y);
				}
			}

			glm::quat rot = transform->GetLocalRotation();
			rot = glm::rotate(rot, -lookH * m_RotateHSpeed * g_DeltaTime, up);
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
			ImGui::Checkbox("Using keyboard", &g_InputManager->bPlayerUsingKeyboard[m_PlayerIndex]);

			ImGui::Text("Seconds attempting to turn: %.5f", m_SecondsAttemptingToTurn);
			ImGui::Text("Turning dir: %s", m_DirTurning == TurningDir::LEFT ? "left" : m_DirTurning == TurningDir::RIGHT ? "right" : "none");

			ImGui::TreePop();
		}
	}

	void PlayerController::SnapPosToTrack(real pDistAlongTrack, real moveForward, real moveBackward)
	{
		TrackManager* trackManager = g_SceneManager->CurrentScene()->GetTrackManager();
		BezierCurveList* newTrack = m_Player->m_TrackRiding;
		real newDistAlongTrack = m_Player->m_DistAlongTrack;
		i32 desiredDir = 1;
		const real leftStickX = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, Input::GamepadAxis::LEFT_STICK_X);
		real rightStickX = 0.0f;
		if (g_InputManager->bPlayerUsingKeyboard[m_PlayerIndex])
		{
			rightStickX = g_InputManager->GetKeyDown(Input::KeyCode::KEY_D) ? 1.0f : rightStickX;
			rightStickX = g_InputManager->GetKeyDown(Input::KeyCode::KEY_A) ? -1.0f : rightStickX;
		}
		else
		{
			rightStickX = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, Input::GamepadAxis::RIGHT_STICK_X);
		}
		static const real STICK_THRESHOLD = 0.5f;
		if (leftStickX < -STICK_THRESHOLD)
		{
			desiredDir = 0;
		}
		else if (leftStickX > STICK_THRESHOLD)
		{
			desiredDir = 2;
		}
		bool bReversingDownTrack = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, Input::GamepadAxis::LEFT_TRIGGER) > 0.0f;

		trackManager->UpdatePreview(m_Player->m_TrackRiding, m_Player->m_DistAlongTrack, desiredDir, m_Player->GetTransform()->GetForward(), m_Player->IsFacingDownTrack(), bReversingDownTrack);

		i32 newJunctionIndex = -1;
		i32 newCurveIndex = -1;
		glm::vec3 newPos = trackManager->GetPointOnTrack(m_Player->m_TrackRiding, m_Player->m_DistAlongTrack, pDistAlongTrack, desiredDir, bReversingDownTrack, &newTrack, &newDistAlongTrack, &newJunctionIndex, &newCurveIndex);

		bool bSwitchedTracks = newTrack != m_Player->m_TrackRiding;
		if (bSwitchedTracks)
		{
			BezierCurveList* pTrack = m_Player->m_TrackRiding;
			real pDist = m_Player->m_DistAlongTrack;
			m_Player->m_TrackRiding = newTrack;
			m_Player->m_DistAlongTrack = newDistAlongTrack;


			glm::vec3 pTrackF = pTrack->GetCurveDirectionAt(pDist);
			glm::vec3 newTrackF = newTrack->GetCurveDirectionAt(newDistAlongTrack);

			//if (pDist > 0.5f)
			//{
			//	pTrackF = -pTrackF;
			//}

			//if (newDistAlongTrack > 0.5f)
			//{
			//	newTrackF = -newTrackF;
			//}

			real angleBetweenTracks = glm::angle(pTrackF, newTrackF);
			Print("angle between tracks; %.3f\n", angleBetweenTracks);

			bool bForwardFacingDownNewTrack = newTrack->IsVectorFacingDownTrack(newDistAlongTrack, m_Player->GetTransform()->GetForward());
			real epsilon = 0.01f;
			if (angleBetweenTracks <= (PI_DIV_TWO + epsilon) || angleBetweenTracks >= (THREE_PI_DIV_TWO - epsilon))
			{
				Print("Acute\n");
				if (bForwardFacingDownNewTrack)
				{
					m_Player->m_TrackState = TrackState::FACING_BACKWARD;
				}
				else
				{
					m_Player->m_TrackState = TrackState::FACING_FORWARD;
				}
			}
			else
			{
				Print("Obtuse\n");
				if (bForwardFacingDownNewTrack)
				{
					m_Player->m_TrackState = TrackState::FACING_FORWARD;
				}
				else
				{
					m_Player->m_TrackState = TrackState::FACING_BACKWARD;
				}
			}
		}

		Transform* playerTransform = m_Player->GetTransform();

		glm::vec3 trackForward = m_Player->m_TrackRiding->GetCurveDirectionAt(m_Player->m_DistAlongTrack);
		glm::vec3 trackRight = glm::cross(trackForward, VEC3_UP);

		//if (bSwitchedTracks)
		//{
		//	real PFoTF = glm::dot(playerTransform->GetForward(), trackForward);


		//	if (moveForward)
		//	{
		//		if (PFoTF > 0.0f)
		//		{
		//			Print("Switched tracks - not turning around - hopped onto next track going forward\n");
		//		}
		//		else
		//		{
		//			bool bAtStartOfNextTrack = (newDistAlongTrack < 0.1f);
		//			if (bAtStartOfNextTrack)
		//			{
		//				Print("Switched tracks - turning around - hopped onto start of next track going forward\n");
		//				m_Player->m_TrackState = TrackState::FACING_FORWARD;
		//			}
		//			else
		//			{
		//				Print("Switched tracks - turning around - hopped onto end of next track going forward\n");
		//				m_Player->m_TrackState = TrackState::FACING_BACKWARD;
		//			}
		//		}
		//	}
		//	else if (moveBackward)
		//	{
		//		if (PFoTF < 0.0f)
		//		{
		//			Print("Switched tracks - not turning around - hopped onto next track going backward\n");
		//		}
		//		else
		//		{
		//			bool bAtStartOfNextTrack = (newDistAlongTrack < 0.1f);
		//			if (bAtStartOfNextTrack)
		//			{
		//				Print("Switched tracks - turning around - hopped onto start of next track going backward\n");
		//				m_Player->m_TrackState = TrackState::FACING_BACKWARD;
		//			}
		//			else
		//			{
		//				Print("Switched tracks - turning around - hopped onto end of next track going backward\n");
		//				m_Player->m_TrackState = TrackState::FACING_FORWARD;
		//			}
		//		}
		//	}
		//}

		newPos += glm::vec3(0.0f, m_Player->m_Height / 2.0f, 0.0f);
		playerTransform->SetWorldPosition(newPos, true);

		glm::vec3 playerF = playerTransform->GetForward();
		real TFoPF = glm::dot(trackForward, playerF);
		if (TFoPF > 0.0f)
		{
			trackForward = -trackForward;
		}

		bool bTurningRight = rightStickX > m_TurnStartStickXThreshold;
		bool bTurningLeft = rightStickX < -m_TurnStartStickXThreshold;
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
} // namespace flex
