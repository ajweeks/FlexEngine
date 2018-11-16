#include "stdafx.hpp"

#include "Cameras/BaseCamera.hpp"

#pragma warning(push, 0)
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
#pragma warning(pop)

#include "Helpers.hpp"
#include "Player.hpp"
#include "PlayerController.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

namespace flex
{
	BaseCamera::BaseCamera(const std::string& cameraName, real FOV, real zNear, real zFar) :
		m_Name(cameraName),
		m_FOV(FOV), m_ZNear(zNear), m_ZFar(zFar),
		m_Position(VEC3_ZERO),
		m_View(MAT4_ZERO),
		m_Proj(MAT4_ZERO),
		m_ViewProjection(MAT4_ZERO),
		m_Yaw(0.0f),
		m_Pitch(0.0f),
		m_MoveSpeed(25.0f),
		m_PanSpeed(10.0f),
		m_DragDollySpeed(0.1f),
		m_ScrollDollySpeed(2.0f),
		m_MoveSpeedFastMultiplier(3.5f),
		m_MoveSpeedSlowMultiplier(0.05f),
		m_TurnSpeedFastMultiplier(2.0f),
		m_TurnSpeedSlowMultiplier(0.1f),
		m_OrbitingSpeed(0.1f),
		m_GamepadRotationSpeed(2.0f),
		m_MouseRotationSpeed(0.0015f)
	{
		ResetOrientation();
		CalculateAxisVectorsFromPitchAndYaw();
		RecalculateViewProjection();
		CalculateExposure();
	}

	BaseCamera::~BaseCamera()
	{
	}

	void BaseCamera::Initialize()
	{
	}

	void BaseCamera::OnSceneChanged()
	{
	}

	void BaseCamera::OnPossess()
	{
		Player* m_Player0 = g_SceneManager->CurrentScene()->GetPlayer(0);
		Player* player1 = g_SceneManager->CurrentScene()->GetPlayer(1);

		if (m_Player0)
		{
			PlayerController* pc0 = m_Player0->GetController();
			pc0->UpdateIsPossessed();
		}

		if (player1)
		{
			PlayerController* pc1 = player1->GetController();
			pc1->UpdateIsPossessed();
		}
	}

	void BaseCamera::OnDepossess()
	{
	}

	void BaseCamera::DrawImGuiObjects()
	{
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
		speed = glm::clamp(speed, 0.0f, 1.0f);

		glm::vec3 targetForward = glm::normalize(point - m_Position);
		m_Forward = glm::normalize(Lerp(m_Forward, targetForward, speed));

		CalculateYawAndPitchFromForward();
		RecalculateViewProjection();
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
		m_Position = VEC3_ZERO;
	}

	void BaseCamera::ResetOrientation()
	{
		m_Pitch = 0.0f;
		m_Yaw = PI;
	}

	void BaseCamera::CalculateAxisVectorsFromPitchAndYaw()
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
		ClampPitch();
		m_Yaw = atan2(m_Forward.z, m_Forward.x);
	}

	// TODO: Measure impact of calling this every frame (optimize? Only call when values change? Only update changed values)
	void BaseCamera::RecalculateViewProjection()
	{
		const glm::vec2 windowSize = g_Window->GetSize();
		if (windowSize.x == 0.0f || windowSize.y == 0.0f)
		{
			return;
		}

		m_View = glm::lookAt(m_Position, m_Position + m_Forward, m_Up);

		real aspectRatio = windowSize.x / (real)windowSize.y;
		m_Proj = glm::perspective(m_FOV, aspectRatio, m_ZNear, m_ZFar);

		m_ViewProjection = m_Proj * m_View;

	}

	void BaseCamera::ClampPitch()
	{
		real pitchLimit = glm::radians(89.5f);
		m_Pitch = glm::clamp(m_Pitch, -pitchLimit, pitchLimit);
	}

	float BaseCamera::CalculateEV100(float aperture, float shutterSpeed, float sensitivity)
	{
		return log2((aperture * aperture) / shutterSpeed * 100.0f / sensitivity);
	}

	float BaseCamera::ComputeExposureNormFactor(float EV100)
	{
		return pow(2.0f, EV100) * 1.2f;
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

	void BaseCamera::CalculateExposure()
	{
		real EV100 = CalculateEV100(aperture, shutterSpeed, lightSensitivity);
		exposure = ComputeExposureNormFactor(EV100);
	}
} // namespace flex
