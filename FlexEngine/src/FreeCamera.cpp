#include "stdafx.hpp"

#include "FreeCamera.hpp"

#include <glm/vec2.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Logger.hpp"
#include "Window/Window.hpp"

namespace flex
{
	FreeCamera::FreeCamera(GameContext& gameContext, float FOV, float zNear, float zFar) :
		m_FOV(FOV), m_ZNear(zNear), m_ZFar(zFar),
		m_Position(glm::vec3(0.0f)),
		m_MoveSpeed(50.0f),
		m_MoveSpeedFastMultiplier(3.5f),
		m_MoveSpeedSlowMultiplier(0.1f),
		m_ScrollDollySpeed(2.0f),
		m_DragDollySpeed(0.1f),
		m_RotationSpeed(0.0011f),
		m_View(glm::mat4(0.0f)),
		m_Proj(glm::mat4(0.0f)),
		m_ViewProjection(glm::mat4(0.0f)),
		m_Yaw(0.0f),
		m_Pitch(0.0f)
	{
		gameContext.camera = this;
		ResetOrientation();
		RecalculateViewProjection(gameContext);

		LoadDefaultKeybindings();
	}

	FreeCamera::~FreeCamera()
	{
	}

	void FreeCamera::Update(const GameContext& gameContext)
	{
		glm::vec2 look(0.0f);
		if (gameContext.inputManager->GetMouseButtonDown(InputManager::MouseButton::LEFT))
		{
			look = gameContext.inputManager->GetMouseMovement();
			look.y = -look.y;

			m_Yaw += look.x * m_RotationSpeed;
			m_Pitch += look.y * m_RotationSpeed;

			float pitchLimit = PI - 0.017f;
			if (m_Pitch > pitchLimit) m_Pitch = pitchLimit;
			if (m_Pitch < -pitchLimit) m_Pitch = -pitchLimit;
		}
		
		m_Forward = {};
		m_Forward.x = cos(m_Pitch) * cos(m_Yaw);
		m_Forward.y = sin(m_Pitch);
		m_Forward.z = cos(m_Pitch) * sin(m_Yaw);
		m_Forward = normalize(m_Forward);

		glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

		m_Right = normalize(glm::cross(worldUp, m_Forward));
		m_Up = cross(m_Forward, m_Right);

		glm::vec3 translation(0.0f);
		if (gameContext.inputManager->GetKeyDown(m_MoveForwardKey))
		{
			translation += m_Forward;
		}
		if (gameContext.inputManager->GetKeyDown(m_MoveBackwardKey))
		{
			translation -= m_Forward;
		}
		if (gameContext.inputManager->GetKeyDown(m_MoveLeftKey))
		{
			translation += m_Right;
		}
		if (gameContext.inputManager->GetKeyDown(m_MoveRightKey))
		{
			translation -= m_Right;
		}
		if (gameContext.inputManager->GetKeyDown(m_MoveUpKey))
		{
			translation += m_Up;
		}
		if (gameContext.inputManager->GetKeyDown(m_MoveDownKey))
		{
			translation -= m_Up;
		}

		float scrollDistance = gameContext.inputManager->GetVerticalScrollDistance();
		if (scrollDistance != 0.0f)
		{
			translation += m_Forward * scrollDistance * m_ScrollDollySpeed;
		}

		if (gameContext.inputManager->GetMouseButtonDown(InputManager::MouseButton::RIGHT))
		{
			glm::vec2 zoom = gameContext.inputManager->GetMouseMovement();
			translation += m_Forward * -zoom.y * m_DragDollySpeed;
		}

		float speedMultiplier = 1.0f;
		if (gameContext.inputManager->GetKeyDown(m_MoveFasterKey))
		{
			speedMultiplier = m_MoveSpeedFastMultiplier;
		}
		else if (gameContext.inputManager->GetKeyDown(m_MoveSlowerKey))
		{
			speedMultiplier = m_MoveSpeedSlowMultiplier;
		}

		Translate(translation * m_MoveSpeed * speedMultiplier * gameContext.deltaTime);

		RecalculateViewProjection(gameContext);
	}

	void FreeCamera::SetFOV(float FOV)
	{
		m_FOV = FOV;
	}

	float FreeCamera::GetFOV() const
	{
		return m_FOV;
	}

	void FreeCamera::SetZNear(float zNear)
	{
		m_ZNear = zNear;
	}

	float FreeCamera::GetZNear() const
	{
		return m_ZNear;
	}

	void FreeCamera::SetZFar(float zFar)
	{
		m_ZFar = zFar;
	}

	float FreeCamera::GetZFar() const
	{
		return m_ZFar;
	}

	glm::mat4 FreeCamera::GetViewProjection() const
	{
		return m_ViewProjection;
	}

	glm::mat4 FreeCamera::GetView() const
	{
		return m_View;
	}

	glm::mat4 FreeCamera::GetProjection() const
	{
		return m_Proj;
	}

	void FreeCamera::LookAt(glm::vec3 point)
	{
		glm::vec3 dPos = point - m_Position;
		glm::vec3 dDir = glm::normalize(dPos);

		const float targetYaw = glm::atan(dDir.z, dDir.x);
		const float targetPitch = glm::atan(dDir.y, dDir.z);

		m_Yaw = targetYaw;
		m_Pitch = targetPitch;
	}

	void FreeCamera::SetMoveSpeed(float moveSpeed)
	{
		m_MoveSpeed = moveSpeed;
	}

	float FreeCamera::GetMoveSpeed() const
	{
		return m_MoveSpeed;
	}

	void FreeCamera::SetRotationSpeed(float rotationSpeed)
	{
		m_RotationSpeed = rotationSpeed;
	}

	float FreeCamera::GetRotationSpeed() const
	{
		return m_RotationSpeed;
	}

	void FreeCamera::Translate(glm::vec3 translation)
	{
		m_Position += translation;
	}

	void FreeCamera::SetPosition(glm::vec3 position)
	{
		m_Position = position;
	}

	glm::vec3 FreeCamera::GetPosition() const
	{
		return m_Position;
	}

	void FreeCamera::SetViewDirection(float yawRad, float pitchRad)
	{
		m_Yaw = yawRad;
		m_Pitch = pitchRad;
	}

	glm::vec3 FreeCamera::GetViewDirection() const
	{
		return m_Forward;
	}

	void FreeCamera::ResetPosition()
	{
		m_Position = glm::vec3(0.0f);
	}

	void FreeCamera::ResetOrientation()
	{
		m_Pitch = 0.0f;
		m_Yaw = PI;
	}

	// TODO: Measure impact of calling this every frame (optimize? Only call when values change? Only update changed values)
	void FreeCamera::RecalculateViewProjection(const GameContext& gameContext)
	{
		const glm::vec2 windowSize = gameContext.window->GetSize();
		if (windowSize.x == 0.0f || windowSize.y == 0.0f) return;

		float aspectRatio = windowSize.x / (float)windowSize.y;
		m_Proj = glm::perspective(m_FOV, aspectRatio, m_ZNear, m_ZFar);

		m_View = lookAt(m_Position, m_Position + m_Forward, m_Up);
		m_ViewProjection = m_Proj * m_View;

	}

	void FreeCamera::LoadDefaultKeybindings()
	{
		m_MoveForwardKey = InputManager::KeyCode::KEY_W;
		m_MoveBackwardKey = InputManager::KeyCode::KEY_S;
		m_MoveLeftKey = InputManager::KeyCode::KEY_A;
		m_MoveRightKey = InputManager::KeyCode::KEY_D;
		m_MoveUpKey = InputManager::KeyCode::KEY_E;
		m_MoveDownKey = InputManager::KeyCode::KEY_Q;
		m_MoveFasterKey = InputManager::KeyCode::KEY_LEFT_SHIFT;
		m_MoveSlowerKey = InputManager::KeyCode::KEY_LEFT_CONTROL;
	}

	void FreeCamera::LoadAzertyKeybindings()
	{
		m_MoveForwardKey = InputManager::KeyCode::KEY_Z;
		m_MoveBackwardKey = InputManager::KeyCode::KEY_S;
		m_MoveLeftKey = InputManager::KeyCode::KEY_Q;
		m_MoveRightKey = InputManager::KeyCode::KEY_D;
		m_MoveUpKey = InputManager::KeyCode::KEY_E;
		m_MoveDownKey = InputManager::KeyCode::KEY_A;
		m_MoveFasterKey = InputManager::KeyCode::KEY_LEFT_SHIFT;
		m_MoveSlowerKey = InputManager::KeyCode::KEY_LEFT_CONTROL;
	}

	float FreeCamera::GetYaw() const
	{
		return m_Yaw;
	}

	void FreeCamera::SetYaw(float yawRad)
	{
		m_Yaw = yawRad;
	}

	float FreeCamera::GetPitch() const
	{
		return m_Pitch;
	}

	void FreeCamera::SetPitch(float pitchRad)
	{
		m_Pitch = pitchRad;
	}
} // namespace flex
