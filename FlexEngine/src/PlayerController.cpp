#include "stdafx.hpp"

#include "PlayerController.hpp"

#include "BulletDynamics/Dynamics/btRigidBody.h"

#include "Scene/GameObject.hpp"
#include "Physics/RigidBody.hpp"
#include "InputManager.hpp"
#include "Player.hpp"

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
		btVector3 force(0.0f, 0.0f, 0.0f);

		force.setX(m_MoveSpeed *
				   gameContext.inputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::LEFT_STICK_X) *
				   gameContext.deltaTime);

		force.setZ(m_MoveSpeed *
				   gameContext.inputManager->GetGamepadAxisValue(m_PlayerIndex, InputManager::GamepadAxis::LEFT_STICK_Y) *
				   gameContext.deltaTime);

		if (gameContext.inputManager->IsGamepadButtonDown(m_PlayerIndex, InputManager::GamepadButton::A))
		{
			force.setY(m_JumpForce);
		}

		btQuaternion orientation = m_Player->GetRigidBody()->GetRigidBodyInternal()->getOrientation();
		glm::vec3 euler = glm::eulerAngles(BtQuaternionToQuaternion(orientation));
		if (false) //m_PlayerIndex == 0)
		{
			Logger::LogInfo(std::to_string(orientation.getX()) + ", " +
							std::to_string(orientation.getY()) + ", " +
							std::to_string(orientation.getZ()) + ", " +
							std::to_string(orientation.getW()) + "\t\t" +
							std::to_string(euler.x) + ", " +
							std::to_string(euler.y) + ", " +
							std::to_string(euler.z) + ", ");
		}
		btQuaternion target = btQuaternion(0.0f, 0.0f, 0.0f);

		m_Player->GetRigidBody()->GetRigidBodyInternal()->activate(true);
		m_Player->GetRigidBody()->GetRigidBodyInternal()->applyCentralForce(force);
	}

	void PlayerController::Destroy()
	{
	}
} // namespace flex
