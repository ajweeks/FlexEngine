#include "stdafx.hpp"

#include "PlayerController.hpp"

#include <LinearMath/btIDebugDraw.h>
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>

#include "Cameras/CameraManager.hpp"
#include "GameContext.hpp"
#include "InputManager.hpp"
#include "Physics/PhysicsTypeConversions.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/BaseScene.hpp"
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
	}

	void PlayerController::Update(const GameContext& gameContext)
	{
		if (gameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_EQUAL) ||
			gameContext.inputManager->IsGamepadButtonPressed(m_PlayerIndex, InputManager::GamepadButton::RIGHT_BUMPER))
		{
			gameContext.cameraManager->SwtichToIndexRelative(gameContext, 1, false);
		}
		else if (gameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_MINUS) ||
				 gameContext.inputManager->IsGamepadButtonPressed(m_PlayerIndex, InputManager::GamepadButton::LEFT_BUMPER))
		{
			gameContext.cameraManager->SwtichToIndexRelative(gameContext, -1, false);
		}

		// TODO: Make frame-rate-independent!

		btRigidBody* rb = m_Player->GetRigidBody()->GetRigidBodyInternal();
		btVector3 pos = rb->getWorldTransform().getOrigin();

		if (gameContext.inputManager->IsGamepadButtonDown(m_PlayerIndex, InputManager::GamepadButton::BACK))
		{
			rb->clearForces();
			rb->setLinearVelocity(btVector3(0, 0, 0));
			rb->setAngularVelocity(btVector3(0, 0, 0));
			btTransform identity = btTransform::getIdentity();
			identity.setOrigin(btVector3(0, 5, 0));
			rb->setWorldTransform(identity);
			return;
		}

		btVector3 force(0.0f, 0.0f, 0.0f);

		// Grounded check
		{
			btVector3 rayStart = Vec3ToBtVec3(m_Player->GetTransform()->GetWorldlPosition());
			btVector3 rayEnd = rayStart + btVector3(0, -(m_Player->GetHeight() / 2.0f + 0.05f), 0);

			//gameContext.renderer->GetDebugDrawer()->drawLine(rayStart, rayEnd, btVector3(0, 0, 1));

			btDynamicsWorld::ClosestRayResultCallback rayCallback(rayStart, rayEnd);
			btDiscreteDynamicsWorld* physWorld = gameContext.sceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
			// TODO: Do several raytests to allow jumping off an edge
			physWorld->rayTest(rayStart, rayEnd, rayCallback);
			m_bGrounded = rayCallback.hasHit();
			
		}


		real inAirMovementMultiplier = (m_bGrounded ? 1.0f : 0.5f);
		force += btVector3(1.0f, 0.0f, 0.0f) * m_MoveAcceleration * inAirMovementMultiplier *
			-gameContext.inputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::LEFT_STICK_X);

		force += btVector3(0.0f, 0.0f, 1.0f) * m_MoveAcceleration *inAirMovementMultiplier *
			-gameContext.inputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::LEFT_STICK_Y);

		btIDebugDraw* debugDrawer = gameContext.renderer->GetDebugDrawer();

		btVector3 up, right, forward;
		m_Player->GetRigidBody()->GetUpRightForward(up, right, forward);

		const real lineLength = 4.0f;
		debugDrawer->drawLine(pos, pos + up * lineLength, btVector3(0, 1, 0));
		debugDrawer->drawLine(pos, pos + forward * lineLength, btVector3(0, 0, 1));
		debugDrawer->drawLine(pos, pos + right * lineLength, btVector3(1, 0, 0));

		btQuaternion orientation = rb->getOrientation();
		glm::vec3 euler = glm::eulerAngles(BtQuaternionToQuaternion(orientation));

		rb->applyCentralForce(force);

		btVector3 angularVel = rb->getAngularVelocity();
		angularVel.setY(angularVel.getY() * (1.0 - m_RotateFriction));
		rb->setAngularVelocity(angularVel);

		const btVector3& vel = rb->getLinearVelocity();
		real velMagnitude = vel.length();
		bool bMaxVel = false;
		if (velMagnitude > m_MaxMoveSpeed)
		{
			rb->setLinearVelocity(vel.normalized() * m_MaxMoveSpeed);
			velMagnitude = m_MaxMoveSpeed;
			bMaxVel = true;
		}
		bool bDrawVelocity = true;
		if (bDrawVelocity)
		{
			real scale = 1.0f;
			btVector3 start = pos;
			btVector3 end = start + rb->getLinearVelocity() * scale;
			debugDrawer->drawLine(start, end, bMaxVel ? btVector3(0.9f, 0.3f, 0.4f) : btVector3(0.1f, 0.85f, 0.98f));
		}

		// Look in direction of movement
		if (velMagnitude > 0.1f)
		{
			btTransform& transform = rb->getWorldTransform();
			btQuaternion oldRotation = transform.getRotation();
			real angle = -atan2((real)vel.getZ(), (real)vel.getX()) + PI_DIV_TWO;
			btQuaternion targetRotation(btVector3(0.0f, 1.0f, 0.0f), angle);
			real movementSpeedSlowdown = glm::clamp(velMagnitude / m_MaxSlowDownRotationSpeedVel, 0.0f, 1.0f);
			real turnSpeed = m_RotationSnappiness * movementSpeedSlowdown;
			btQuaternion newRotation = oldRotation.slerp(targetRotation, turnSpeed);
			transform.setRotation(newRotation);
		}
	}

	void PlayerController::Destroy()
	{
	}
} // namespace flex
