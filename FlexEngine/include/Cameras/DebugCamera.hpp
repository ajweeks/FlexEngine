#pragma once

#include "BaseCamera.hpp"

#include "GameContext.hpp"
#include "InputManager.hpp"

namespace flex
{
	class DebugCamera final : public BaseCamera
	{
	public:
		DebugCamera(GameContext& gameContext, real FOV = glm::radians(45.0f), real zNear = 0.1f, real zFar = 10000.0f);
		~DebugCamera();

		virtual void Update(const GameContext& gameContext) override;

		void LoadDefaultKeybindings();

	private:
		bool m_EnableGamepadMovement = true;
		bool m_EnableKeyboardMovement = true;

		glm::vec3 m_DragStartPosition;

		// [0, 1) 0 = no lag, higher values give smoother movement
		real m_MoveLag = 0.5f;
		// TODO: Remove turn lag entirely?
		real m_TurnLag = 0.1f;
		glm::vec3 m_MoveVel;
		glm::vec2 m_TurnVel; // Contains amount pitch and yaw changed last frame

		InputManager::KeyCode m_MoveForwardKey;
		InputManager::KeyCode m_MoveBackwardKey;
		InputManager::KeyCode m_MoveLeftKey;
		InputManager::KeyCode m_MoveRightKey;
		InputManager::KeyCode m_MoveUpKey;
		InputManager::KeyCode m_MoveDownKey;
		InputManager::KeyCode m_MoveFasterKey;
		InputManager::KeyCode m_MoveSlowerKey;
	};
} // namespace flex
