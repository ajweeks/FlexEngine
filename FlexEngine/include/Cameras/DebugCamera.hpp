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
		virtual bool IsDebugCam() const override;

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

		KeyCode m_MoveForwardKey;
		KeyCode m_MoveBackwardKey;
		KeyCode m_MoveLeftKey;
		KeyCode m_MoveRightKey;
		KeyCode m_MoveUpKey;
		KeyCode m_MoveDownKey;
		KeyCode m_MoveFasterKey;
		KeyCode m_MoveSlowerKey;
	};
} // namespace flex
