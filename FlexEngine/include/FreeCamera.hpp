#pragma once

#include "GameContext.hpp"
#include "InputManager.hpp"

namespace flex
{
	class FreeCamera final
	{
	public:
		FreeCamera(GameContext& gameContext, float FOV = glm::radians(45.0f), float zNear = 0.1f, float zFar = 10000.0f);
		~FreeCamera();

		void Update(const GameContext& gameContext);

		void SetFOV(float FOV);
		float GetFOV() const;
		void SetZNear(float zNear);
		float GetZNear() const;
		void SetZFar(float zFar);
		float GetZFar() const;
		glm::mat4 GetViewProjection() const;
		glm::mat4 GetView() const;
		glm::mat4 GetProjection() const;

		// speed: Lerp amount to new rotation
		void LookAt(glm::vec3 point, float speed = 1.0f);

		void SetMoveSpeed(float moveSpeed);
		float GetMoveSpeed() const;
		void SetRotationSpeed(float rotationSpeed);
		float GetRotationSpeed() const;

		void Translate(glm::vec3 translation);
		void SetPosition(glm::vec3 position);
		glm::vec3 GetPosition() const;

		void SetViewDirection(float yawRad, float pitchRad);

		glm::vec3 GetRight() const;
		glm::vec3 GetUp() const;
		glm::vec3 GetForward() const;

		void ResetPosition();
		void ResetOrientation();

		void LoadDefaultKeybindings();
		void LoadAzertyKeybindings();

		void SetYaw(float yawRad);
		float GetYaw() const; // Returns the yaw of the camera in radians
		void SetPitch(float pitchRad);
		float GetPitch() const; // Returns the pitch of the camera in radians

	private:
		void RecalculateViewProjection(const GameContext& gameContext);

		glm::mat4 m_View;
		glm::mat4 m_Proj;
		glm::mat4 m_ViewProjection;

		float m_FOV;
		float m_ZNear;
		float m_ZFar;

		glm::vec3 m_Position;

		float m_Yaw;
		float m_Pitch;
		glm::vec3 m_Forward;
		glm::vec3 m_Up;
		glm::vec3 m_Right;

		float m_MoveSpeed;
		float m_ScrollDollySpeed;
		float m_DragDollySpeed;
		float m_MoveSpeedFastMultiplier;
		float m_MoveSpeedSlowMultiplier;
		float m_RotationSpeed;

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
