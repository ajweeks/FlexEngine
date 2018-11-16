#pragma once

#include "BaseCamera.hpp"

#include "InputEnums.hpp"

namespace flex
{
	class DebugCamera final : public BaseCamera
	{
	public:
		DebugCamera(real FOV = glm::radians(45.0f), real zNear = 0.1f, real zFar = 10000.0f);
		~DebugCamera();

		virtual void Update() override;

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

		Input::KeyCode m_MoveForwardKey;
		Input::KeyCode m_MoveBackwardKey;
		Input::KeyCode m_MoveLeftKey;
		Input::KeyCode m_MoveRightKey;
		Input::KeyCode m_MoveUpKey;
		Input::KeyCode m_MoveDownKey;
		Input::KeyCode m_MoveFasterKey;
		Input::KeyCode m_MoveSlowerKey;
	};
} // namespace flex
