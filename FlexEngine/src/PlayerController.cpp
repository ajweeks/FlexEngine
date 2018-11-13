#include "stdafx.hpp"

#include "PlayerController.hpp"

#pragma warning(push, 0)
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <glm/gtc/matrix_transform.hpp>

#include <glm/gtx/quaternion.hpp>
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

		m_SoundTrackAttachID = AudioManager::AddAudioSource(RESOURCE_LOCATION + "audio/crunch-13.wav");
		m_SoundTrackDetachID = AudioManager::AddAudioSource(RESOURCE_LOCATION + "audio/schluck-02.wav");
		m_SoundTrackSwitchDirID = AudioManager::AddAudioSource(RESOURCE_LOCATION + "audio/whistle-01.wav");
		//m_SoundTrackAttachID = AudioManager::AddAudioSource(RESOURCE_LOCATION + "audio/schluck-07.wav");

		UpdateIsPossessed();
	}

	void PlayerController::Destroy()
	{
		AudioManager::DestroyAudioSource(m_SoundTrackAttachID);
		AudioManager::DestroyAudioSource(m_SoundTrackDetachID);
		AudioManager::DestroyAudioSource(m_SoundTrackSwitchDirID);
	}

	void PlayerController::Update()
	{
		m_Player->GetRigidBody()->UpdateParentTransform();

		if (g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_EQUAL) ||
			g_InputManager->IsGamepadButtonPressed(m_PlayerIndex, InputManager::GamepadButton::RIGHT_BUMPER))
		{
			g_CameraManager->SetActiveIndexRelative(1, false);
		}
		else if (g_InputManager->GetKeyPressed(InputManager::KeyCode::KEY_MINUS) ||
			g_InputManager->IsGamepadButtonPressed(m_PlayerIndex, InputManager::GamepadButton::LEFT_BUMPER))
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
		BezierCurveList* pTrackRiding = m_TrackRiding;

		if (m_bPossessed)
		{
			if (g_InputManager->IsGamepadButtonDown(m_PlayerIndex, InputManager::GamepadButton::BACK))
			{
				ResetTransformAndVelocities();
				return;
			}
			else if (g_InputManager->IsGamepadButtonPressed(m_PlayerIndex, InputManager::GamepadButton::X))
			{
				if (m_TrackRiding)
				{
					m_TrackRiding = nullptr;
					AudioManager::PlaySource(m_SoundTrackDetachID);
					m_DistAlongTrack = -1.0f;
				}
				else
				{
					real distAlongTrack = -1.0f;
					TrackManager* trackManager = g_SceneManager->CurrentScene()->GetTrackManager();
					m_TrackRiding = trackManager->GetTrackInRange(transform->GetWorldPosition(), m_TrackAttachMinDist, distAlongTrack);
					if (m_TrackRiding)
					{
						m_DistAlongTrack = distAlongTrack;
						SnapPosToTrack(m_DistAlongTrack);
						AudioManager::PlaySource(m_SoundTrackAttachID);
					}
				}
			}
		}

		btVector3 force(0.0f, 0.0f, 0.0f);

		// Grounded check
		{
			btVector3 rayStart = ToBtVec3(transform->GetWorldPosition());
			btVector3 rayEnd = rayStart + btVector3(0, -(m_Player->GetHeight() / 2.0f + 0.05f), 0);

			btDynamicsWorld::ClosestRayResultCallback rayCallback(rayStart, rayEnd);
			btDiscreteDynamicsWorld* physWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
			physWorld->rayTest(rayStart, rayEnd, rayCallback);
			m_bGrounded = rayCallback.hasHit();
		}


		if (m_bPossessed)
		{
			if (m_TrackRiding)
			{
				real moveForward = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::RIGHT_TRIGGER);
				real moveBackward = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::LEFT_TRIGGER);

				bool bUpdateFacing = false;
				if (moveForward == 0.0f && moveBackward == 0.0f)
				{
					bUpdateFacing = true;
				}

				bool pFacingForwardDownTrack = m_Player->bFacingForwardDownTrack;
				if (m_bUpdateFacingAndForceFoward || bUpdateFacing)
				{
					glm::vec3 trackForward = glm::normalize(m_TrackRiding->GetCurveDirectionAt(m_DistAlongTrack));
					m_Player->bFacingForwardDownTrack = (glm::dot(trackForward, forward) > 0.0f);
					if (m_bUpdateFacingAndForceFoward)
					{
						m_Player->bFacingForwardDownTrack =
							(m_Player->bFacingForwardDownTrack && moveForward > 0.0f) ||
							(m_Player->bFacingForwardDownTrack && moveBackward > 0.0f);
					}

					m_bUpdateFacingAndForceFoward = false;
				}

				if (!m_Player->bFacingForwardDownTrack)
				{
					moveForward = 1.0f - moveForward;
					moveBackward = 1.0f - moveBackward;
				}

				real pDist = m_DistAlongTrack;
				TrackManager* trackManager = g_SceneManager->CurrentScene()->GetTrackManager();
				m_DistAlongTrack = trackManager->AdvanceTAlongTrack(m_TrackRiding, (moveForward - moveBackward) * m_TrackMoveSpeed * g_DeltaTime, m_DistAlongTrack);
				SnapPosToTrack(pDist);

				if (m_Player->bFacingForwardDownTrack != pFacingForwardDownTrack &&
					m_TrackRiding == pTrackRiding)
				{
					AudioManager::PlaySource(m_SoundTrackSwitchDirID);
				}

				m_pDTrackMovement = m_DistAlongTrack - pDist;
			}
			else if (!m_Player->GetObjectInteractingWith())
			{
				real moveH = 0.0f;
				real moveV = 0.0f;

				if (g_InputManager->bPlayerUsingKeyboard[m_PlayerIndex])
				{
					moveH = g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_D) > 0 ? 1.0f :
						g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_A) > 0 ? -1.0f : 0.0f;
					moveV = g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_W) > 0 ? -1.0f :
						g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_S) > 0 ? 1.0f : 0.0f;
				}
				else
				{
					moveH = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::LEFT_STICK_X);
					moveV = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::LEFT_STICK_Y);
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

		//m_Player->GetTransform()->SetWorldPosition(m_Player->GetTransform()->GetWorldPosition() + glm::vec3(g_DeltaTime * 1.0f, 0.0f, 0.0f));

		bool bDrawVelocity = false;
		if (bDrawVelocity)
		{
			real scale = 1.0f;
			btVector3 start = rbPos + btVector3(0.0f, -0.5f, 0.0f);
			btVector3 end = start + rb->getLinearVelocity() * scale;
			debugDrawer->drawLine(start, end, bMaxVel ? btVector3(0.9f, 0.3f, 0.4f) : btVector3(0.1f, 0.85f, 0.98f));
		}

		if (m_bPossessed)
		{
			real lookH = 0.0f;
			real lookV = 0.0f;

			if (g_InputManager->bPlayerUsingKeyboard[m_PlayerIndex])
			{
				lookH = g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_RIGHT) > 0 ? 1.0f :
					g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_LEFT) > 0 ? -1.0f : 0.0f;
				lookV = g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_UP) > 0 ? -1.0f :
					g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_DOWN) > 0 ? 1.0f : 0.0f;
			}
			else
			{
				lookH = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::RIGHT_STICK_X);
				lookV = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::RIGHT_STICK_Y);
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

	void PlayerController::UpdateIsPossessed()
	{
		m_bPossessed = false;

		// TODO: Implement more robust solution
		BaseCamera* cam = g_CameraManager->CurrentCamera();
		FirstPersonCamera* fpCam = dynamic_cast<FirstPersonCamera*>(cam);
		OverheadCamera* ohCam = dynamic_cast<OverheadCamera*>(cam);
		if (fpCam || ohCam)
		{
			m_bPossessed = true;
		}
		m_Mode = (fpCam == nullptr) ? Mode::THIRD_PERSON : Mode::FIRST_PERSON;
	}

	bool PlayerController::IsPossessed() const
	{
		return m_bPossessed;
	}

	real PlayerController::GetTrackAttachDistThreshold() const
	{
		return m_TrackAttachMinDist;
	}

	real PlayerController::GetDistAlongTrack() const
	{
		if (m_TrackRiding)
		{
			return m_DistAlongTrack;
		}
		else
		{
			return -1.0f;
		}
	}

	BezierCurveList* PlayerController::GetTrackRiding() const
	{
		return m_TrackRiding;
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

	void PlayerController::SnapPosToTrack(real pDistAlongTrack)
	{
		TrackManager* trackManager = g_SceneManager->CurrentScene()->GetTrackManager();
		BezierCurveList* newTrack = m_TrackRiding;
		real newDistAlongTrack = m_DistAlongTrack;
		i32 desiredDir = 1;
		const real leftStickX = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::LEFT_STICK_X);
		const real rightStickX = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::RIGHT_STICK_X);
		static const real STICK_THRESHOLD = 0.5f;
		if (leftStickX < -STICK_THRESHOLD)
		{
			desiredDir = 0;
		}
		else if (leftStickX > STICK_THRESHOLD)
		{
			desiredDir = 2;
		}
		bool bReversingDownTrack = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::LEFT_TRIGGER) > 0.0f;

		trackManager->UpdatePreview(m_TrackRiding, m_DistAlongTrack, desiredDir, m_Player->GetTransform()->GetForward(), m_Player->bFacingForwardDownTrack, bReversingDownTrack);

		i32 newJunctionIndex = -1;
		i32 newCurveIndex = -1;
		glm::vec3 newPos = trackManager->GetPointOnTrack(m_TrackRiding, m_DistAlongTrack, pDistAlongTrack, desiredDir, bReversingDownTrack, &newTrack, &newDistAlongTrack, &newJunctionIndex, &newCurveIndex);

		if (newTrack != m_TrackRiding)
		{
			m_TrackRiding = newTrack;
			m_DistAlongTrack = newDistAlongTrack;
			m_bUpdateFacingAndForceFoward = true;
		}

		Transform* playerTransform = m_Player->GetTransform();

		glm::vec3 trackForward = trackManager->GetDirectionOnTrack(m_TrackRiding, m_DistAlongTrack);
		glm::vec3 trackRight = glm::cross(trackForward, VEC3_UP);
		newPos += glm::vec3(0.0f, 1.9f, 0.0f);
		playerTransform->SetWorldPosition(newPos, true);

		glm::quat rot = playerTransform->GetWorldRotation();

		real invTurnSpeed = m_TurnToFaceDownTrackInvSpeed;

		if (m_bTurningAroundOnTrack)
		{
			invTurnSpeed = m_FlipTrackDirInvSpeed;
			m_bUpdateFacingAndForceFoward = true;
			trackForward = m_TargetTrackFor;
		}
		else
		{
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

			if (1.0f - glm::abs(TFoPF) > m_MinForDotTurnThreshold)
			{
				if (m_SecondsAttemptingToTurn > m_AttemptToTurnTimeThreshold)
				{
					m_SecondsAttemptingToTurn = -m_TurnAroundCooldown;
					trackForward = -trackForward;
					m_bTurningAroundOnTrack = true;
					m_DirTurning = TurningDir::NONE;
					m_TargetTrackFor = trackForward;
				}
			}
		}

		glm::quat desiredRot = glm::quatLookAt(trackForward, playerTransform->GetUp());
		rot = glm::slerp(rot, desiredRot, 1.0f - glm::clamp(g_DeltaTime * invTurnSpeed, 0.0f, 0.99f));
		playerTransform->SetWorldRotation(rot, true);

		if (m_bTurningAroundOnTrack)
		{
			real newAlignedness = glm::dot(trackForward, playerTransform->GetForward());
			real exitTurnAlignednessDotThreshold = 0.9f;
			if (newAlignedness < -exitTurnAlignednessDotThreshold)
			{
				m_bTurningAroundOnTrack = false;
			}
		}
	}
} // namespace flex
