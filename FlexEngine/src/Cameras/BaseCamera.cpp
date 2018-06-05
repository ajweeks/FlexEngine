#include "stdafx.hpp"

#include "Cameras/BaseCamera.hpp"

#include <glm/vec2.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Logger.hpp"
#include "Window/Window.hpp"
#include "Helpers.hpp"

namespace flex
{
	BaseCamera::BaseCamera(const std::string& cameraName, GameContext& gameContext, real FOV, real zNear, real zFar) :
		m_Name(cameraName),
		m_FOV(FOV), m_ZNear(zNear), m_ZFar(zFar),
		m_Position(glm::vec3(0.0f)),
		m_View(glm::mat4(0.0f)),
		m_Proj(glm::mat4(0.0f)),
		m_ViewProjection(glm::mat4(0.0f)),
		m_Yaw(0.0f),
		m_Pitch(0.0f),
		m_MoveSpeed(25.0f),
		m_PanSpeed(10.0f),
		m_DragDollySpeed(0.1f),
		m_ScrollDollySpeed(2.0f),
		m_MoveSpeedFastMultiplier(3.5f),
		m_MoveSpeedSlowMultiplier(0.05f),
		m_GamepadRotationSpeed(100.0f),
		m_MouseRotationSpeed(0.001f)
	{
		ResetOrientation();
		CalculateAxisVectors();
		RecalculateViewProjection(gameContext);
	}

	BaseCamera::~BaseCamera()
	{
	}

	void BaseCamera::Initialize(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
	}

	void BaseCamera::SetFOV(real FOV)
	{
		m_FOV = FOV;
	}

	real BaseCamera::GetFOV() const
	{
		return m_FOV;
	}

	void BaseCamera::SetZNear(real zNear)
	{
		m_ZNear = zNear;
	}

	real BaseCamera::GetZNear() const
	{
		return m_ZNear;
	}

	void BaseCamera::SetZFar(real zFar)
	{
		m_ZFar = zFar;
	}

	real BaseCamera::GetZFar() const
	{
		return m_ZFar;
	}

	glm::mat4 BaseCamera::GetViewProjection() const
	{
		return m_ViewProjection;
	}

	glm::mat4 BaseCamera::GetView() const
	{
		return m_View;
	}

	glm::mat4 BaseCamera::GetProjection() const
	{
		return m_Proj;
	}

	void BaseCamera::LookAt(glm::vec3 point, real speed)
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

	void BaseCamera::Translate(glm::vec3 translation)
	{
		m_Position += translation;
	}

	void BaseCamera::SetPosition(glm::vec3 position)
	{
		m_Position = position;
	}

	glm::vec3 BaseCamera::GetPosition() const
	{
		return m_Position;
	}

	void BaseCamera::SetViewDirection(real yawRad, real pitchRad)
	{
		m_Yaw = yawRad;
		m_Pitch = pitchRad;
	}

	glm::vec3 BaseCamera::GetRight() const
	{
		return m_Right;
	}

	glm::vec3 BaseCamera::GetUp() const
	{
		return m_Up;
	}

	glm::vec3 BaseCamera::GetForward() const
	{
		return m_Forward;
	}

	void BaseCamera::ResetPosition()
	{
		m_Position = glm::vec3(0.0f);
	}

	void BaseCamera::ResetOrientation()
	{
		m_Pitch = 0.0f;
		m_Yaw = PI;
	}

	void BaseCamera::CalculateAxisVectors()
	{
		m_Forward = {};
		m_Forward.x = cos(m_Pitch) * cos(m_Yaw);
		m_Forward.y = sin(m_Pitch);
		m_Forward.z = cos(m_Pitch) * sin(m_Yaw);
		m_Forward = normalize(m_Forward);

		glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

		m_Right = normalize(glm::cross(worldUp, m_Forward));
		m_Up = cross(m_Forward, m_Right);
	}

	void BaseCamera::CalculateYawAndPitchFromForward()
	{
		m_Pitch = asin(m_Forward.y);
		m_Yaw = atan2(m_Forward.x, m_Forward.z);
	}

	// TODO: Measure impact of calling this every frame (optimize? Only call when values change? Only update changed values)
	void BaseCamera::RecalculateViewProjection(const GameContext& gameContext)
	{
		const glm::vec2 windowSize = gameContext.window->GetSize();
		if (windowSize.x == 0.0f || windowSize.y == 0.0f)
		{
			return;
		}

		real aspectRatio = windowSize.x / (real)windowSize.y;
		m_Proj = glm::perspective(m_FOV, aspectRatio, m_ZNear, m_ZFar);

		m_View = lookAt(m_Position, m_Position + m_Forward, m_Up);
		m_ViewProjection = m_Proj * m_View;

	}

	void BaseCamera::ClampPitch()
	{
		real pitchLimit = PI_DIV_TWO - 0.02f;
		if (m_Pitch > pitchLimit)
		{
			m_Pitch = pitchLimit;
		}
		else if (m_Pitch < -pitchLimit)
		{
			m_Pitch = -pitchLimit;
		}
	}

	real BaseCamera::GetYaw() const
	{
		return m_Yaw;
	}

	void BaseCamera::SetYaw(real yawRad)
	{
		m_Yaw = yawRad;
	}

	real BaseCamera::GetPitch() const
	{
		return m_Pitch;
	}

	void BaseCamera::SetPitch(real pitchRad)
	{
		m_Pitch = pitchRad;
	}

	void BaseCamera::SetMoveSpeed(real moveSpeed)
	{
		m_MoveSpeed = moveSpeed;
	}

	real BaseCamera::GetMoveSpeed() const
	{
		return m_MoveSpeed;
	}

	void BaseCamera::SetRotationSpeed(real rotationSpeed)
	{
		m_MouseRotationSpeed = rotationSpeed;
	}

	real BaseCamera::GetRotationSpeed() const
	{
		return m_MouseRotationSpeed;
	}

	std::string BaseCamera::GetName() const
	{
		return m_Name;
	}
} // namespace flex
