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
	BaseCamera::BaseCamera(const std::string& cameraName, CameraType type, bool bIsGameplayCam, real FOV, real zNear, real zFar) :
		bIsGameplayCam(bIsGameplayCam),
		type(type),
		m_Name(cameraName),
		m_View(MAT4_ZERO),
		m_Proj(MAT4_ZERO),
		m_ViewProjection(MAT4_ZERO),
		FOV(FOV),
		zNear(zNear),
		zFar(zFar),
		moveSpeed(18.0f),
		panSpeed(60.0f),
		dragDollySpeed(25.0f),
		scrollDollySpeed(10.0f),
		orbitingSpeed(0.03f),
		mouseRotationSpeed(0.015f),
		gamepadRotationSpeed(2.0f),
		moveSpeedFastMultiplier(3.0f),
		moveSpeedSlowMultiplier(0.08f),
		turnSpeedFastMultiplier(2.0f),
		turnSpeedSlowMultiplier(0.1f),
		panSpeedFastMultiplier(2.5f),
		panSpeedSlowMultiplier(0.2f),
		rollRestorationSpeed(12.0f),
		position(VEC3_ZERO),
		yaw(0.0f),
		pitch(0.0f),
		roll(0.0f),
		forward(VEC3_FORWARD),
		up(VEC3_UP),
		right(VEC3_RIGHT)
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

	void BaseCamera::FixedUpdate()
	{
	}

	void BaseCamera::Update()
	{
		prevPosition = position;

		roll = Lerp(roll, 0.0f, rollRestorationSpeed * g_DeltaTime);

		AudioManager::SetListenerPos(position);
		AudioManager::SetListenerVel(velocity);
	}

	void BaseCamera::LateUpdate()
	{
		velocity = (position - prevPosition) / g_DeltaTime;

	}

	void BaseCamera::Destroy()
	{
		if (m_bInitialized)
		{
			m_bInitialized = false;
		}
	}

	void BaseCamera::OnPostSceneChange()
	{
	}

	void BaseCamera::OnPossess()
	{
		Player* player0 = g_SceneManager->CurrentScene()->GetPlayer(0);
		Player* player1 = g_SceneManager->CurrentScene()->GetPlayer(1);

		if (player0 != nullptr)
		{
			player0->UpdateIsPossessed();
		}

		if (player1 != nullptr)
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
		speed = Saturate(speed);

		glm::vec3 targetForward = glm::normalize(point - position);
		forward = glm::normalize(Lerp(forward, targetForward, speed));

#if THOROUGH_CHECKS
		if (IsNanOrInf(forward))
		{
			PrintError("Forward vector was NaN or Inf!\n");
			forward = VEC3_FORWARD;
		}
#endif

		CalculateYawAndPitchFromForward();
		RecalculateViewProjection();
	}

	void BaseCamera::Translate(glm::vec3 translation)
	{
		position += translation;
	}

	void BaseCamera::SetViewDirection(real yawRad, real pitchRad)
	{
		yaw = yawRad;
		pitch = pitchRad;
		roll = 0.0f;
	}

	void BaseCamera::ResetPosition()
	{
		position = VEC3_ZERO;
	}

	void BaseCamera::ResetOrientation()
	{
		pitch = 0.0f;
		yaw = PI;
		roll = 0.0f;
	}

	void BaseCamera::CalculateAxisVectorsFromPitchAndYaw()
	{
		forward.x = cos(pitch) * cos(yaw);
		forward.y = sin(pitch);
		forward.z = cos(pitch) * sin(yaw);
		forward = normalize(forward);

		glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
		worldUp += right * roll;

		right = normalize(glm::cross(forward, worldUp));
		up = cross(right, forward);
	}

	void BaseCamera::CalculateYawAndPitchFromForward()
	{
		pitch = asin(forward.y);
		ClampPitch();
		yaw = atan2(forward.z, forward.x);
		roll = 0.0f;

#if THOROUGH_CHECKS
		if (IsNanOrInf(pitch))
		{
			PrintError("Pitch was NaN or Inf!\n");
			pitch = 0.0f;
		}
		if (IsNanOrInf(yaw))
		{
			PrintError("Yaw was NaN or Inf!\n");
			yaw = 0.0f;
		}
#endif
	}

	void BaseCamera::RecalculateViewProjection()
	{
		const glm::vec2 windowSize = g_Window->GetSize();
		if (windowSize.x == 0.0f || windowSize.y == 0.0f)
		{
			return;
		}

		m_View = glm::lookAt(position, position + forward, up);

		real aspectRatio = (real)windowSize.x / (real)windowSize.y;
		m_Proj = glm::perspective(FOV, aspectRatio, zFar, zNear);

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
		pitch = glm::clamp(pitch, -pitchLimit, pitchLimit);
	}

	real BaseCamera::CalculateEV100(real aperture, real shutterSpeed, real sensitivity)
	{
		return log2((aperture * aperture) / shutterSpeed * 100.0f / sensitivity);
	}

	real BaseCamera::ComputeExposureNormFactor(real EV100)
	{
		return pow(2.0f, EV100) * 1.2f;
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
