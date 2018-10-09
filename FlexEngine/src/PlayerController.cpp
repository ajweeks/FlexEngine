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

		if (m_bPossessed &&
			g_InputManager->IsGamepadButtonDown(m_PlayerIndex, InputManager::GamepadButton::BACK))
		{
			ResetTransformAndVelocities();
			return;
		}

		btVector3 force(0.0f, 0.0f, 0.0f);

		// Grounded check
		{
			btVector3 rayStart = ToBtVec3(m_Player->GetTransform()->GetWorldPosition());
			btVector3 rayEnd = rayStart + btVector3(0, -(m_Player->GetHeight() / 2.0f + 0.05f), 0);

			btDynamicsWorld::ClosestRayResultCallback rayCallback(rayStart, rayEnd);
			btDiscreteDynamicsWorld* physWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
			// TODO: Do several raytests to allow jumping off an edge
			physWorld->rayTest(rayStart, rayEnd, rayCallback);
			m_bGrounded = rayCallback.hasHit();
		}


		if (m_bPossessed &&
			!m_Player->GetObjectInteractingWith())
		{
			switch (m_Mode)
			{
			case Mode::THIRD_PERSON:
			{
				real inAirMovementMultiplier = (m_bGrounded ? 1.0f : 0.5f);
				real moveH = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::LEFT_STICK_X);
				real moveV = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::LEFT_STICK_Y);
				force += btVector3(1.0f, 0.0f, 0.0f) * m_MoveAcceleration * inAirMovementMultiplier * -moveH;
				force += btVector3(0.0f, 0.0f, 1.0f) * m_MoveAcceleration * inAirMovementMultiplier * -moveV;
			} break;
			case Mode::FIRST_PERSON:
			{
				real moveH = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::LEFT_STICK_X);
				real moveV = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::LEFT_STICK_Y);
				force += ToBtVec3(m_Player->GetTransform()->GetRight()) * m_MoveAcceleration * -moveH;
				force += ToBtVec3(m_Player->GetTransform()->GetForward()) * m_MoveAcceleration * -moveV;
			} break;
			}
		}

		btIDebugDraw* debugDrawer = g_Renderer->GetDebugDrawer();

		{
			const real lineLength = 4.0f;
			debugDrawer->drawLine(rbPos, rbPos + ToBtVec3(up) * lineLength, btVector3(0.0f, 1.0f, 0.0f));
			debugDrawer->drawLine(rbPos, rbPos + ToBtVec3(forward) * lineLength, btVector3(0.0f, 0.0f, 1.0f));
			debugDrawer->drawLine(rbPos, rbPos + ToBtVec3(right) * lineLength, btVector3(1.0f, 0.0f, 0.0f));
		}

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
					btQuaternion oldRotation = transformBT.getRotation();
					real angle = -atan2((real)vel.getZ(), (real)vel.getX()) + PI_DIV_TWO;
					btQuaternion targetRotation(btVector3(0.0f, 1.0f, 0.0f), angle);
					real movementSpeedSlowdown = glm::clamp(xzVelMagnitude / m_MaxSlowDownRotationSpeedVel, 0.0f, 1.0f);
					real turnSpeed = m_RotationSnappiness * movementSpeedSlowdown * g_DeltaTime;
					btQuaternion newRotation = oldRotation.slerp(targetRotation, turnSpeed);
					transformBT.setRotation(newRotation);
				}
			} break;
			case Mode::FIRST_PERSON:
			{
				real lookH = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::RIGHT_STICK_X);
				//real lookV = g_InputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::RIGHT_STICK_Y);

				glm::vec3 pos = transform->GetWorldPosition();
				//Print("forward:  %.2f, %.2f, %.2f,  up:  %.2f, %.2f, %.2f,  right:  %.2f, %.2f, %.2f\n", forward.x, forward.y, forward.z, up.x, up.y, up.z, right.x, right.y, right.z);

				btQuaternion oldRotation = transformBT.getRotation();
				glm::quat rot = ToQuaternion(oldRotation);

				//glm::vec3 dUp = (up - glm::vec3(0.0f, 1.0f, 0.0f));
				//real fallingOverAmount = (glm::dot(dUp, forward));
				//glm::vec3 rotEuler = glm::eulerAngles(rot);

				//glm::vec3 newUp = glm::normalize(up + glm::vec3(0.0f, 0.5f, 0.0f));

				//real yaw = atan(forward.z / forward.x);
				//Print("Falling over amount: %.2f  yaw: %.2f  euler: %.0f, %.0f, %.0f,  forward: %.2f, %.2f, %.2f\n", fallingOverAmount, yaw, glm::degrees(rotEuler.x), glm::degrees(rotEuler.y), glm::degrees(rotEuler.z), forward.x, forward.y, forward.z);

				//glm::quat newRot = glm::quat(glm::vec3(0.0f));
				//newRot = glm::rotate(newRot, yaw, glm::vec3(0.0f, 1.0f, 0.0f));

				//glm::quat newRot = glm::toQuat(glm::lookAtLH(pos, pos + forward, newUp));
				//rot = glm::rotate(rot, fallingOverAmount * g_DeltaTime * 5.0f, forward);


				//rot = glm::rotate(rot, glm::clamp(fallingOverAmount * g_DeltaTime * 10.0f, 0.0f, 1.0f), right);

				//glm::vec3 newRotEuler = rotEuler;
				//newRotEuler.z *= 0.1f;
				//newRotEuler.x *= 0.1f;
				//glm::quat targetRot(newRotEuler);

				//rot = glm::lerp(rot, newRot, glm::clamp(g_DeltaTime * 40.0f * fallingOverAmount, 0.0f, 1.0f));

				rot = glm::rotate(rot, -lookH * g_DeltaTime * m_RotateSpeed, up);
				//rot = glm::rotate(rot, lookV * g_DeltaTime, right);

				transformBT.setRotation(ToBtQuaternion(rot));
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
} // namespace flex
