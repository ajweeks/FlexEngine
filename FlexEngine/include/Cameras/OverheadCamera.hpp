#pragma once

#include "BaseCamera.hpp"

#include "GameContext.hpp"
#include "InputManager.hpp"

namespace flex
{
	class OverheadCamera final : public BaseCamera
	{
	public:
		OverheadCamera(GameContext& gameContext, real FOV = glm::radians(45.0f), real zNear = 0.1f, real zFar = 10000.0f);
		~OverheadCamera();

		virtual void Update(const GameContext& gameContext) override;

		// TODO: Load from file
		void LoadDefaultKeybindings();
		void LoadAzertyKeybindings();

	private:
		glm::vec3 m_DragStartPosition;

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
