#include "stdafx.hpp"

#include "PlayerController.hpp"

#include "BulletDynamics/Dynamics/btRigidBody.h"
#include "LinearMath/btIDebugDraw.h"

#include "Scene/GameObject.hpp"
#include "Physics/RigidBody.hpp"
#include "InputManager.hpp"
#include "Player.hpp"
#include "Physics/PhysicsTypeConversions.hpp"

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
	}

	void PlayerController::Update(const GameContext& gameContext)
	{
		// TODO: Make frame-rate-independent!
		btVector3 up, right, forward;
		m_Player->GetRigidBody()->GetUpRightForward(up, right, forward);

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
		btVector3 torque(0.0f, 0.0f, 0.0f);

		force += right * m_MoveSpeed *
			gameContext.inputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::LEFT_STICK_X) *
			gameContext.deltaTime;

		force += forward * m_MoveSpeed *
			-gameContext.inputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::LEFT_STICK_Y) *
			gameContext.deltaTime;

		bool grounded = true;



		if (gameContext.inputManager->IsGamepadButtonDown(m_PlayerIndex, InputManager::GamepadButton::A))
		{
			force += up * m_JumpForce;
		}

		torque.setY(m_RotateSpeed *
					gameContext.inputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::RIGHT_STICK_X) *
					gameContext.deltaTime);

		btIDebugDraw* debugDrawer = gameContext.renderer->GetDebugDrawer();
		//debugDrawer->drawLine(pos, pos + btVector3(0, 1, 0) * 5.0f, btVector3(1, 1, 1));

		const real lineLength = 4.0f;
		debugDrawer->drawLine(pos, pos + up * lineLength, btVector3(0, 1, 0));
		debugDrawer->drawLine(pos, pos + forward * lineLength, btVector3(0, 0, 1));
		debugDrawer->drawLine(pos, pos + right * lineLength, btVector3(1, 0, 0));

		btQuaternion orientation = rb->getOrientation();
		glm::vec3 euler = glm::eulerAngles(BtQuaternionToQuaternion(orientation));

		rb->applyCentralForce(force);
		rb->applyTorque(torque);

		btVector3 angularVel = rb->getAngularVelocity();
		angularVel.setY(angularVel.getY() * (1.0 - gameContext.deltaTime * m_RotateFriction));
		rb->setAngularVelocity(angularVel);

		//btTransform newTransform = rb->getWorldTransform();
		//btQuaternion newRotation = newTransform.getRotation();

		//btQuaternion(0, 0, 0);
		//real halfAngle = newRotation.angle();

		//newTransform.setRotation(newRotation);
		//rb->setWorldTransform(newTransform);
	}

	void PlayerController::Destroy()
	{
	}
} // namespace flex
