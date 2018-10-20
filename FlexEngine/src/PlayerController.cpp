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

		m_SoundRailAttachID = AudioManager::AddAudioSource(RESOURCE_LOCATION + "audio/crunch-13.wav");
		m_SoundRailDetachID = AudioManager::AddAudioSource(RESOURCE_LOCATION + "audio/schluck-02.wav");
		//m_SoundRailAttachID = AudioManager::AddAudioSource(RESOURCE_LOCATION + "audio/schluck-07.wav");

		UpdateIsPossessed();
	}

	void PlayerController::Destroy()
	{
	}

	void PlayerController::Update()
	{
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
		btVector3 rbPos = rb->getWorldTransform().getOrigin();

		btTransform& transformBT = rb->getWorldTransform();
		Transform* transform = m_Player->GetTransform();
		glm::vec3 up = transform->GetUp();
		glm::vec3 right = transform->GetRight();
		glm::vec3 forward = transform->GetForward();

		if (m_bPossessed)
		{
			if (g_InputManager->IsGamepadButtonDown(m_PlayerIndex, InputManager::GamepadButton::BACK))
			{
				ResetTransformAndVelocities();
				return;
			}
			else if (g_InputManager->IsGamepadButtonPressed(m_PlayerIndex, InputManager::GamepadButton::X))
			{
				if (m_RailRiding)
				{
					m_RailRiding = nullptr;
					AudioManager::PlaySource(m_SoundRailDetachID);
					Print("Detached\n");
				}
				else
				{
					real distAlongRail = -1.0f;
					m_RailRiding = g_SceneManager->CurrentScene()->GetRailInRange(transform->GetWorldPosition(), m_RailAttachMinDist, distAlongRail);
					if (m_RailRiding)
					{
						m_DistAlongRail = distAlongRail;
						SnapPosToRail();
						AudioManager::PlaySource(m_SoundRailAttachID);
						Print("Attached\n");
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
			// TODO: Do several raytests to allow jumping off an edge
			physWorld->rayTest(rayStart, rayEnd, rayCallback);
			m_bGrounded = rayCallback.hasHit();
		}


		if (m_bPossessed)
		{
			real moveH = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::LEFT_STICK_X);
			real moveV = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::LEFT_STICK_Y);

			if (m_RailRiding)
			{
				glm::vec3 railForward = glm::normalize(m_RailRiding->GetFirstDerivative(m_DistAlongRail));
				//glm::vec3 railRight = glm::cross(railForward, glm::vec3(0.0f, 1.0f, 0.0f));

				m_DistAlongRail += (-moveV * glm::dot(railForward, forward) + -moveH * glm::dot(railForward, right))
					* m_RailMoveSpeed * g_DeltaTime;
				m_DistAlongRail = glm::clamp(m_DistAlongRail, 0.0f, 1.0f);
				SnapPosToRail();
			}
			else if (!m_Player->GetObjectInteractingWith())
			{
				switch (m_Mode)
				{
				case Mode::THIRD_PERSON:
				{
					real inAirMovementMultiplier = (m_bGrounded ? 1.0f : 0.5f);
					force += btVector3(1.0f, 0.0f, 0.0f) * m_MoveAcceleration * inAirMovementMultiplier * -moveH;
					force += btVector3(0.0f, 0.0f, 1.0f) * m_MoveAcceleration * inAirMovementMultiplier * -moveV;
				} break;
				case Mode::FIRST_PERSON:
				{
					force += ToBtVec3(transform->GetRight()) * m_MoveAcceleration * -moveH;
					force += ToBtVec3(transform->GetForward()) * m_MoveAcceleration * -moveV;
				} break;
				}
			}
		}

		btIDebugDraw* debugDrawer = g_Renderer->GetDebugDrawer();

#define DRAW_LOCAL_AXES 0
#if DRAW_LOCAL_AXES
			const real lineLength = 4.0f;
			debugDrawer->drawLine(rbPos, rbPos + ToBtVec3(up) * lineLength, btVector3(0.0f, 1.0f, 0.0f));
			debugDrawer->drawLine(rbPos, rbPos + ToBtVec3(forward) * lineLength, btVector3(0.0f, 0.0f, 1.0f));
			debugDrawer->drawLine(rbPos, rbPos + ToBtVec3(right) * lineLength, btVector3(1.0f, 0.0f, 0.0f));
#endif

		btQuaternion orientation = rb->getOrientation();
		glm::vec3 euler = glm::eulerAngles(ToQuaternion(orientation));

		rb->applyCentralForce(force);
		btVector3 angularVel = rb->getAngularVelocity();
		angularVel.setY(angularVel.getY() * (1.0f - m_RotateFriction));
		rb->setAngularVelocity(angularVel);

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

		m_Player->GetRigidBody()->UpdateParentTransform();

		bool bDrawVelocity = true;
		if (bDrawVelocity)
		{
			real scale = 1.0f;
			btVector3 start = rbPos;
			btVector3 end = start + rb->getLinearVelocity() * scale;
			debugDrawer->drawLine(start, end, bMaxVel ? btVector3(0.9f, 0.3f, 0.4f) : btVector3(0.1f, 0.85f, 0.98f));
		}

		if (m_bPossessed)
		{
			switch (m_Mode)
			{
			case Mode::THIRD_PERSON:
			{
				// Look in direction of movement
				if (xzVelMagnitude > 0.1f)
				{
					//btQuaternion oldRotation = transformBT.getRotation();
					//real angle = -atan2((real)vel.getZ(), (real)vel.getX()) + PI_DIV_TWO;
					//btQuaternion targetRotation(btVector3(0.0f, 1.0f, 0.0f), angle);
					//real movementSpeedSlowdown = glm::clamp(xzVelMagnitude / m_MaxSlowDownRotationSpeedVel, 0.0f, 1.0f);
					//real turnSpeed = m_RotationSnappiness * movementSpeedSlowdown * g_DeltaTime;
					//btQuaternion newRotation = oldRotation.slerp(targetRotation, turnSpeed);
					//transformBT.setRotation(newRotation);
				}
			} break;
			case Mode::FIRST_PERSON:
			{
				real lookH = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::RIGHT_STICK_X);
				real lookV = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::RIGHT_STICK_Y);

				glm::quat rot = transform->GetLocalRotation();
				rot = glm::rotate(rot, -lookH * m_RotateHSpeed * g_DeltaTime, up);
				transform->SetLocalRotation(rot);

				m_Player->AddToPitch(lookV * m_RotateVSpeed * g_DeltaTime);
			} break;
			}
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
		if (fpCam)
		{
			m_bPossessed = true;
		}
	}

	BezierCurve* PlayerController::GetRailRiding() const
	{
		return m_RailRiding;
	}

	void PlayerController::SnapPosToRail()
	{
		glm::vec3 pos(m_RailRiding->GetPointOnCurve(m_DistAlongRail));
		glm::vec3 railForward = glm::normalize(m_RailRiding->GetFirstDerivative(m_DistAlongRail));
		glm::vec3 railRight = glm::cross(railForward, glm::vec3(0.0f, 1.0f, 0.0f));
		pos -= railRight * 0.8f;
		m_Player->GetTransform()->SetWorldPosition(pos, true);
	}
} // namespace flex
