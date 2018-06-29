#pragma once

#include "GameContext.hpp"
#include "InputManager.hpp"

#pragma warning(push, 0)
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/trigonometric.hpp>
#pragma warning(pop)

namespace flex
{
	class BaseCamera
	{
	public:
		BaseCamera(const std::string& cameraName, GameContext& gameContext, real FOV = glm::radians(45.0f), real zNear = 0.1f, real zFar = 10000.0f);
		~BaseCamera();

		virtual void Initialize(const GameContext& gameContext);
		virtual void Update(const GameContext& gameContext) = 0;

		void SetFOV(real FOV);
		real GetFOV() const;
		void SetZNear(real zNear);
		real GetZNear() const;
		void SetZFar(real zFar);
		real GetZFar() const;
		glm::mat4 GetViewProjection() const;
		glm::mat4 GetView() const;
		glm::mat4 GetProjection() const;

		// speed: Lerp amount to new rotation
		void LookAt(glm::vec3 point, real speed = 1.0f);

		void Translate(glm::vec3 translation);
		void SetPosition(glm::vec3 position);
		glm::vec3 GetPosition() const;

		void SetViewDirection(real yawRad, real pitchRad);

		glm::vec3 GetRight() const;
		glm::vec3 GetUp() const;
		glm::vec3 GetForward() const;

		void ResetPosition();
		void ResetOrientation();

		void SetYaw(real rawRad);
		real GetYaw() const;
		void SetPitch(real pitchRad);
		real GetPitch() const;

		void SetMoveSpeed(real moveSpeed);
		real GetMoveSpeed() const;
		void SetRotationSpeed(real rotationSpeed);
		real GetRotationSpeed() const;

		std::string GetName() const;

	protected:
		// Sets m_Right, m_Up, and m_Forward based on m_Yaw and m_Pitch
		void CalculateAxisVectors();
		void CalculateYawAndPitchFromForward();
		void RecalculateViewProjection(const GameContext& gameContext);

		void ClampPitch();

		std::string m_Name;

		glm::mat4 m_View;
		glm::mat4 m_Proj;
		glm::mat4 m_ViewProjection;

		real m_FOV = 0;
		real m_ZNear = 0;
		real m_ZFar = 0;

		real m_MoveSpeed = 0;				// WASD or gamepad left stick
		real m_PanSpeed = 0;				// MMB
		real m_DragDollySpeed = 0;			// RMB
		real m_ScrollDollySpeed = 0;		// Scroll wheel
		real m_MoveSpeedFastMultiplier = 0;
		real m_MoveSpeedSlowMultiplier = 0;
		real m_TurnSpeedFastMultiplier = 0;
		real m_TurnSpeedSlowMultiplier = 0;
		real m_MouseRotationSpeed = 0;		// LMB drag
		real m_GamepadRotationSpeed = 0;	// Gamepad right stick

		glm::vec3 m_Position;

		real m_Yaw = 0;
		real m_Pitch = 0;
		glm::vec3 m_Forward;
		glm::vec3 m_Up;
		glm::vec3 m_Right;
	};
} // namespace flex
