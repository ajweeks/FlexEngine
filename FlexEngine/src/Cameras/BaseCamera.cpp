#include "stdafx.hpp"

#include "Cameras/BaseCamera.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>

IGNORE_WARNINGS_POP

#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "Player.hpp"
#include "PlayerController.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

namespace flex
{
	BaseCamera::BaseCamera(const std::string& cameraName, bool bIsGameplayCam, real FOV, real zNear, real zFar) :
		bIsGameplayCam(bIsGameplayCam),
		m_Name(cameraName),
		m_View(MAT4_ZERO),
		m_Proj(MAT4_ZERO),
		m_ViewProjection(MAT4_ZERO),
		m_FOV(FOV),
		m_ZNear(zNear),
		m_ZFar(zFar),
		m_MoveSpeed(18.0f),
		m_PanSpeed(10.0f),
		m_DragDollySpeed(0.1f),
		m_ScrollDollySpeed(2.0f),
		m_OrbitingSpeed(0.1f),
		m_MouseRotationSpeed(0.0015f),
		m_GamepadRotationSpeed(2.0f),
		m_MoveSpeedFastMultiplier(3.5f),
		m_MoveSpeedSlowMultiplier(0.05f),
		m_TurnSpeedFastMultiplier(2.0f),
		m_TurnSpeedSlowMultiplier(0.1f),
		m_PanSpeedFastMultiplier(2.5f),
		m_PanSpeedSlowMultiplier(0.2f),
		m_RollRestorationSpeed(12.0f),
		m_Position(VEC3_ZERO),
		m_Yaw(0.0f),
		m_Pitch(0.0f),
		m_Roll(0.0f),
		m_Forward(VEC3_FORWARD),
		m_Up(VEC3_UP),
		m_Right(VEC3_RIGHT)
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
		m_bInitialized = true;
	}

	void BaseCamera::Update()
	{
		m_Roll = Lerp(m_Roll, 0.0f, m_RollRestorationSpeed * g_DeltaTime);
	}

	void BaseCamera::Destroy()
	{
		if (m_bInitialized)
		{
			m_bInitialized = false;
		}
	}

	void BaseCamera::OnSceneChanged()
	{
	}

	void BaseCamera::OnPossess()
	{
		Player* player0 = g_SceneManager->CurrentScene()->GetPlayer(0);
		Player* player1 = g_SceneManager->CurrentScene()->GetPlayer(1);

		if (player0)
		{
			player0->UpdateIsPossessed();
		}

		if (player1)
		{
			player1->UpdateIsPossessed();
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

#if THOROUGH_CHECKS
		ENSURE(!IsNanOrInf(m_Forward));
#endif

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
		m_Roll = 0.0f;
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
		m_Roll = 0.0f;
	}

	void BaseCamera::CalculateAxisVectorsFromPitchAndYaw()
	{
		m_Forward = {};
		m_Forward.x = cos(m_Pitch) * cos(m_Yaw);
		m_Forward.y = sin(m_Pitch);
		m_Forward.z = cos(m_Pitch) * sin(m_Yaw);
		m_Forward = normalize(m_Forward);

		glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
		worldUp += m_Right * m_Roll;

		m_Right = normalize(glm::cross(m_Forward, worldUp));
		m_Up = cross(m_Right, m_Forward);
	}

	void BaseCamera::CalculateYawAndPitchFromForward()
	{
		m_Pitch = asin(m_Forward.y);
		ClampPitch();
		m_Yaw = atan2(m_Forward.z, m_Forward.x);
		m_Roll = 0.0f;

#if THOROUGH_CHECKS
		ENSURE(!IsNanOrInf(m_Pitch));
		ENSURE(!IsNanOrInf(m_Yaw));
#endif
	}

	void BaseCamera::RecalculateViewProjection()
	{
		const glm::vec2 windowSize = g_Window->GetSize();
		if (windowSize.x == 0.0f || windowSize.y == 0.0f)
		{
			return;
		}

		m_View = glm::lookAt(m_Position, m_Position + m_Forward, m_Up);

		real aspectRatio = (real)windowSize.x / (real)windowSize.y;
		m_Proj = glm::perspective(m_FOV, aspectRatio, m_ZFar, m_ZNear);

		m_ViewProjection = m_Proj * m_View;

		if (g_Renderer->IsTAAEnabled())
		{
			JitterMatrix(m_ViewProjection);
		}
	}

	void BaseCamera::JitterMatrix(glm::mat4& matrix)
	{
		// [-1.0f, 1.0f]
		static const glm::vec2 SAMPLE_LOCS_16[16] =
		{
			glm::vec2(-8.0f, 0.0f) / 8.0f,
			glm::vec2(-6.0f, -4.0f) / 8.0f,
			glm::vec2(-3.0f, -2.0f) / 8.0f,
			glm::vec2(-2.0f, -6.0f) / 8.0f,
			glm::vec2(1.0f, -1.0f) / 8.0f,
			glm::vec2(2.0f, -5.0f) / 8.0f,
			glm::vec2(6.0f, -7.0f) / 8.0f,
			glm::vec2(5.0f, -3.0f) / 8.0f,
			glm::vec2(4.0f, 1.0f) / 8.0f,
			glm::vec2(7.0f, 4.0f) / 8.0f,
			glm::vec2(3.0f, 5.0f) / 8.0f,
			glm::vec2(0.0f, 7.0f) / 8.0f,
			glm::vec2(-1.0f, 3.0f) / 8.0f,
			glm::vec2(-4.0f, 6.0f) / 8.0f,
			glm::vec2(-7.0f, 8.0f) / 8.0f,
			glm::vec2(-5.0f, 2.0f) / 8.0f
		};

		static const glm::vec2 SAMPLE_LOCS_8[8] =
		{
			glm::vec2(-7.0f, 1.0f) / 8.0f,
			glm::vec2(-5.0f, -5.0f) / 8.0f,
			glm::vec2(-1.0f, -3.0f) / 8.0f,
			glm::vec2(3.0f, -7.0f) / 8.0f,
			glm::vec2(5.0f, -1.0f) / 8.0f,
			glm::vec2(7.0f, 7.0f) / 8.0f,
			glm::vec2(1.0f, 3.0f) / 8.0f,
			glm::vec2(-3.0f, 5.0f) / 8.0f
		};

		static const glm::vec2 SAMPLE_LOCS_4[4] =
		{
			glm::vec2(-7.0f, 1.0f) / 8.0f,
			glm::vec2(-1.0f, -3.0f) / 8.0f,
			glm::vec2(5.0f, -1.0f) / 8.0f,
			glm::vec2(1.0f, 3.0f) / 8.0f,
		};

		static const glm::vec2 SAMPLE_LOCS_2[2] =
		{
			glm::vec2(-7.0f, 1.0f) / 8.0f,
			glm::vec2(5.0f, -7.0f) / 8.0f,
		};

		const i32 sampleCount = g_Renderer->GetTAASampleCount();
		if (sampleCount <= 0)
		{
			return;
		}

		const glm::vec2* samples = (sampleCount == 16 ? SAMPLE_LOCS_16 : (sampleCount == 8 ? SAMPLE_LOCS_8 : (sampleCount == 4 ? SAMPLE_LOCS_4 : SAMPLE_LOCS_2)));

		const glm::vec2i swapChainSize = g_Window->GetFrameBufferSize();
		const unsigned subsampleIdx = g_Renderer->GetFramesRenderedCount() % sampleCount;

		// [-halfPix, halfPix] (in NDC)
		glm::vec2 subsample = (samples[subsampleIdx] / 4.0f) / (glm::vec2)swapChainSize;

		glm::mat4 jitterMat = glm::translate(MAT4_IDENTITY, glm::vec3(subsample.x, subsample.y, 0.0f));
		matrix = jitterMat * matrix;
	}

	void BaseCamera::ClampPitch()
	{
		real pitchLimit = glm::radians(89.5f);
		m_Pitch = glm::clamp(m_Pitch, -pitchLimit, pitchLimit);
	}

	real BaseCamera::CalculateEV100(real aperture, real shutterSpeed, real sensitivity)
	{
		return log2((aperture * aperture) / shutterSpeed * 100.0f / sensitivity);
	}

	real BaseCamera::ComputeExposureNormFactor(real EV100)
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
