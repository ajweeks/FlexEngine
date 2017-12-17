#include "stdafx.hpp"

#include "FreeCamera.hpp"

#include <glm/vec2.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Logger.hpp"
#include "Window/Window.hpp"
#include "Helpers.hpp"

namespace flex
{
	FreeCamera::FreeCamera(GameContext& gameContext, real FOV, real zNear, real zFar) :
		m_FOV(FOV), m_ZNear(zNear), m_ZFar(zFar),
		m_Position(glm::vec3(0.0f)),
		m_MoveSpeed(50.0f),
		m_PanSpeed(10.0f),
		m_DragDollySpeed(0.1f),
		m_ScrollDollySpeed(2.0f),
		m_MoveSpeedFastMultiplier(3.5f),
		m_MoveSpeedSlowMultiplier(0.05f),
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

			real pitchLimit = PI - 0.02f;
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

		if (gameContext.inputManager->GetMouseButtonClicked(InputManager::MouseButton::MIDDLE))
		{
			m_DragStartPosition = m_Position;
		}
		else if (gameContext.inputManager->GetMouseButtonDown(InputManager::MouseButton::MIDDLE))
		{
			glm::vec2 dragDist = gameContext.inputManager->GetMouseDragDistance(InputManager::MouseButton::MIDDLE);
			glm::vec2 frameBufferSize = (glm::vec2)gameContext.window->GetFrameBufferSize();
			glm::vec2 normDragDist = dragDist / frameBufferSize;
			m_Position = (m_DragStartPosition + (normDragDist.x * m_Right + normDragDist.y * m_Up) * m_PanSpeed);
		}

		real scrollDistance = gameContext.inputManager->GetVerticalScrollDistance();
		if (scrollDistance != 0.0f)
		{
			translation += m_Forward * scrollDistance * m_ScrollDollySpeed;
		}

		if (gameContext.inputManager->GetMouseButtonDown(InputManager::MouseButton::RIGHT))
		{
			glm::vec2 zoom = gameContext.inputManager->GetMouseMovement();
			translation += m_Forward * -zoom.y * m_DragDollySpeed;
		}

		real speedMultiplier = 1.0f;
		if (gameContext.inputManager->GetKeyDown(m_MoveFasterKey))
		{
			speedMultiplier = m_MoveSpeedFastMultiplier;
		}
		else if (gameContext.inputManager->GetKeyDown(m_MoveSlowerKey))
		{
			speedMultiplier = m_MoveSpeedSlowMultiplier;
		}

		glm::vec3 finalTranslation = translation * m_MoveSpeed * speedMultiplier * gameContext.deltaTime;
		Translate(finalTranslation);
		m_DragStartPosition += finalTranslation;

		RecalculateViewProjection(gameContext);
	}

	void FreeCamera::SetFOV(real FOV)
	{
		m_FOV = FOV;
	}

	real FreeCamera::GetFOV() const
	{
		return m_FOV;
	}

	void FreeCamera::SetZNear(real zNear)
	{
		m_ZNear = zNear;
	}

	real FreeCamera::GetZNear() const
	{
		return m_ZNear;
	}

	void FreeCamera::SetZFar(real zFar)
	{
		m_ZFar = zFar;
	}

	real FreeCamera::GetZFar() const
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

	void FreeCamera::LookAt(glm::vec3 point, real speed)
	{
		glm::vec3 dPos = glm::normalize(point - m_Position);
		glm::vec3 dDir = glm::normalize(dPos);

		glm::vec3 targetRight = glm::cross(dPos, m_Up);
		glm::vec3 targetForward = glm::cross(targetRight, m_Up);

		//real dYaw = glm::acos(glm::dot(glm::vec2(dPos.x, dPos.z), glm::vec2(m_Forward.x, m_Forward.z)));
		//real dPitch = glm::dot(glm::vec2(dPos.y, dPos.x), glm::vec2(m_Forward.y, m_Forward.x));

		const real targetYaw = glm::atan(dDir.z, dDir.x);
		const real targetPitch = glm::atan(dDir.y, dDir.z);

		m_Yaw = Lerp(m_Yaw, targetYaw, glm::clamp(speed, 0.0f, 1.0f));
		m_Pitch = Lerp(m_Pitch, targetPitch, glm::clamp(speed, 0.0f, 1.0f));
	}

	void FreeCamera::SetMoveSpeed(real moveSpeed)
	{
		m_MoveSpeed = moveSpeed;
	}

	real FreeCamera::GetMoveSpeed() const
	{
		return m_MoveSpeed;
	}

	void FreeCamera::SetRotationSpeed(real rotationSpeed)
	{
		m_RotationSpeed = rotationSpeed;
	}

	real FreeCamera::GetRotationSpeed() const
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

	void FreeCamera::SetViewDirection(real yawRad, real pitchRad)
	{
		m_Yaw = yawRad;
		m_Pitch = pitchRad;
	}

	glm::vec3 FreeCamera::GetRight() const
	{
		return m_Right;
	}

	glm::vec3 FreeCamera::GetUp() const
	{
		return m_Up;
	}

	glm::vec3 FreeCamera::GetForward() const
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

		real aspectRatio = windowSize.x / (real)windowSize.y;
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

	real FreeCamera::GetYaw() const
	{
		return m_Yaw;
	}

	void FreeCamera::SetYaw(real yawRad)
	{
		m_Yaw = yawRad;
	}

	real FreeCamera::GetPitch() const
	{
		return m_Pitch;
	}

	void FreeCamera::SetPitch(real pitchRad)
	{
		m_Pitch = pitchRad;
	}
} // namespace flex
