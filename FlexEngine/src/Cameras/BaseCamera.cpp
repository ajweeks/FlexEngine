#include "stdafx.hpp"

#include "Cameras/BaseCamera.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>

IGNORE_WARNINGS_POP

#include "Game.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "Player.hpp"
#include "PlayerController.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
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
		// Delta time is zero while paused (and on the first frame), which would give an invalid (NaN) velocity
		velocity = g_DeltaTime > 0.0f ? (position - prevPosition) / g_DeltaTime : VEC3_ZERO;

		UpdateSmoothnessStats();
	}

	void BaseCamera::UpdateSmoothnessStats()
	{
		// FLEX_LOOK_TEST=<degrees per second>[,<camera name>]: turns the player at a constant rate (see PlayerController::UpdateLook),
		// switches to that camera, & prints how evenly the camera turned & moved every 300 frames. Each frame's turn rate
		// (yaw change / frame time) should be constant, so "jitter" (the mean frame to frame change in that rate, relative
		// to the rate) should be about 0
		static const char* lookTest = getenv("FLEX_LOOK_TEST");
		if (lookTest == nullptr || !bPossessPlayer || g_DeltaTime <= 0.0f)
		{
			return;
		}

		static real s_PrevYaw = 0.0f, s_PrevRate = 0.0f;
		static glm::vec3 s_PrevPos = VEC3_ZERO;
		static u32 s_Frames = 0, s_StalledFrames = 0;
		static real s_SumRate = 0.0f, s_SumRateChange = 0.0f, s_MaxRateChange = 0.0f, s_SumDt = 0.0f, s_MaxPosStep = 0.0f;
		static real s_SumPixelError = 0.0f, s_MaxPixelError = 0.0f, s_SumPixelErrorOld = 0.0f, s_MaxPixelErrorOld = 0.0f;
		static real s_MaxDepthError = 0.0f, s_MaxDepthErrorOld = 0.0f;
		static i32 s_Warmup = 30;

		// FLEX_LOOK_TEST_POS=<x>,<z>: first teleports the player there (e.g. far from the world origin, to check rendering
		// precision), & waits for the terrain to load before measuring
		static bool s_bTeleported = false;
		if (!s_bTeleported && (g_Game == nullptr || g_Game->IsPlaying()))
		{
			s_bTeleported = true;
			real x, z;
			const char* lookTestPos = getenv("FLEX_LOOK_TEST_POS");
			Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
			TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
			if (lookTestPos != nullptr && sscanf(lookTestPos, "%f,%f", &x, &z) == 2 && player != nullptr && terrainGenerator != nullptr)
			{
				const terrain::ColumnSample column = terrainGenerator->SampleColumn(x, z);
				player->TeleportOnTerrain(glm::vec3(x, glm::max(column.height, column.waterLevel) + 2.0f, z));
				s_Warmup = 900;
			}
		}

		const real yawNow = atan2(forward.x, forward.z);
		real delta = yawNow - s_PrevYaw;
		delta = glm::mod(delta + PI, TWO_PI) - PI;
		const real rate = delta / g_DeltaTime;
		const real posStep = glm::distance(position, s_PrevPos);
		s_PrevYaw = yawNow;
		s_PrevPos = position;

		if (s_Warmup > 0)
		{
			--s_Warmup;
			s_PrevRate = rate;
			return;
		}

		// Rendering precision: where a point 1m in front of the camera lands on screen (& its view space depth, which
		// z-fighting depends on) when it's transformed in single precision as the GPU does, compared to exactly
		{
			const glm::vec3 pointWS = position + forward + right * 0.3f + up * 0.2f;
			const glm::dvec3 eye(position);
			const glm::dmat4 exactView = glm::lookAt(eye, eye + glm::dvec3(forward), glm::dvec3(up));
			const glm::dvec4 exactClip = glm::dmat4(m_Proj) * exactView * glm::dvec4(glm::dvec3(pointWS), 1.0);
			const double exactDepth = (exactView * glm::dvec4(glm::dvec3(pointWS), 1.0)).z;
			const glm::dvec2 exactNDC = glm::dvec2(exactClip) / exactClip.w;
			auto PixelError = [&](const glm::mat4& viewProj)
			{
				const glm::vec4 clip = viewProj * glm::vec4(pointWS, 1.0f);
				return (real)glm::length(glm::dvec2(clip.x / clip.w, clip.y / clip.w) - exactNDC) * 0.5f * g_Window->GetSize().y;
			};
			// As the GPU transforms it now (relative to the render origin, see Renderer::RenderOriginFor), & as it did before
			// camera-relative rendering (in world space)
			const glm::vec3 pointRS = pointWS - m_RenderOrigin;
			const glm::vec4 clipRS = (m_Proj * m_ViewRelative) * glm::vec4(pointRS, 1.0f);
			const real errorNow = (real)glm::length(glm::dvec2(clipRS.x / clipRS.w, clipRS.y / clipRS.w) - exactNDC) * 0.5f * g_Window->GetSize().y;
			const real errorOld = PixelError(m_Proj * m_View);
			s_SumPixelError += errorNow;
			s_MaxPixelError = glm::max(s_MaxPixelError, errorNow);
			s_SumPixelErrorOld += errorOld;
			s_MaxPixelErrorOld = glm::max(s_MaxPixelErrorOld, errorOld);
			s_MaxDepthError = glm::max(s_MaxDepthError, (real)glm::abs((m_ViewRelative * glm::vec4(pointRS, 1.0f)).z - exactDepth));
			s_MaxDepthErrorOld = glm::max(s_MaxDepthErrorOld, (real)glm::abs((m_View * glm::vec4(pointWS, 1.0f)).z - exactDepth));
		}

		++s_Frames;
		s_SumRate += glm::abs(rate);
		s_SumDt += g_DeltaTime;
		const real rateChange = glm::abs(rate - s_PrevRate);
		s_SumRateChange += rateChange;
		s_MaxRateChange = glm::max(s_MaxRateChange, rateChange);
		s_MaxPosStep = glm::max(s_MaxPosStep, posStep);
		s_StalledFrames += (glm::abs(delta) < 1e-6f) ? 1 : 0;
		s_PrevRate = rate;

		if (s_Frames == 300)
		{
			const real meanRate = s_SumRate / s_Frames;
			Print("Camera smoothness (%s at %.0f, %.0f, %.0f): %.1f fps, turn rate %.1f deg/s, jitter %.2f%% (max %.1f%%), %u stalled frames, max move %.3fm/frame\n",
				m_Name.c_str(), position.x, position.y, position.z, s_Frames / s_SumDt, glm::degrees(meanRate),
				100.0f * (s_SumRateChange / s_Frames) / glm::max(meanRate, 1e-4f), 100.0f * s_MaxRateChange / glm::max(meanRate, 1e-4f),
				s_StalledFrames, s_MaxPosStep);
			Print("  projection error 1m ahead: %.3fpx avg, %.3fpx max, depth %.4fmm max (in world space, as before camera-relative rendering: %.2fpx avg, %.2fpx max, depth %.2fmm max)\n",
				s_SumPixelError / s_Frames, s_MaxPixelError, s_MaxDepthError * 1000.0f, s_SumPixelErrorOld / s_Frames, s_MaxPixelErrorOld, s_MaxDepthErrorOld * 1000.0f);
			s_SumPixelError = s_MaxPixelError = s_SumPixelErrorOld = s_MaxPixelErrorOld = s_MaxDepthError = s_MaxDepthErrorOld = 0.0f;
			s_Frames = s_StalledFrames = 0;
			s_SumRate = s_SumRateChange = s_MaxRateChange = s_SumDt = s_MaxPosStep = 0.0f;
		}
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

		// In double precision, & from the forward vector rather than from "position + forward": far from the origin (e.g.
		// 1/64m apart at 200km) that sum is rounded to the nearest representable point, which turned the view direction in
		// visible steps as the camera rotated. The translation (-dot(axis, position)) is only rounded once like this too.
		const glm::dvec3 eye(position);
		const glm::dmat4 view = glm::lookAt(eye, eye + glm::dvec3(forward), glm::dvec3(up));
		m_View = glm::mat4(view);

		real aspectRatio = (real)windowSize.x / (real)windowSize.y;
		m_Proj = glm::perspective(glm::clamp(FOV + fovOffset, glm::radians(5.0f), glm::radians(150.0f)), aspectRatio, zFar, zNear);

		m_ViewProjection = glm::mat4(glm::dmat4(m_Proj) * view);

		// The same from the render origin (exact, since the origin is representable & the subtraction is in double). When
		// the origin is zero these are identical to the matrices above.
		m_RenderOrigin = Renderer::RenderOriginFor(position);
		const glm::dvec3 eyeRelative = eye - glm::dvec3(m_RenderOrigin);
		const glm::dmat4 viewRelative = glm::lookAt(eyeRelative, eyeRelative + glm::dvec3(forward), glm::dvec3(up));
		m_ViewRelative = glm::mat4(viewRelative);
		m_ViewProjectionRelative = glm::mat4(glm::dmat4(m_Proj) * viewRelative);

		if (g_Renderer->IsTAAEnabled())
		{
			JitterMatrix(m_ViewProjection);
			JitterMatrix(m_ViewProjectionRelative);
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
