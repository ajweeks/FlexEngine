#pragma once

namespace flex
{
	enum class CameraType
	{
		DEBUG_CAM,
		OVERHEAD,
		FIRST_PERSON,
		TERMINAL,
		VEHICLE,

		_NONE
	};

	class BaseCamera
	{
	public:
		BaseCamera(const std::string& cameraName, CameraType type, bool bIsGameplayCam, real FOV = glm::radians(45.0f),
			real zNear = 0.5f, real zFar = 1000.0f);
		virtual ~BaseCamera();

		virtual void Initialize();
		virtual void Update();
		virtual void Destroy();

		virtual void OnSceneChanged();

		virtual void OnPossess();
		virtual void OnDepossess();

		virtual void DrawImGuiObjects();

		glm::mat4 GetViewProjection() const;
		glm::mat4 GetView() const;
		glm::mat4 GetProjection() const;

		// speed: Lerp amount to new rotation
		void LookAt(glm::vec3 point, real speed = 1.0f);

		void Translate(glm::vec3 translation);

		void SetViewDirection(real yawRad, real pitchRad);

		void ResetPosition();
		void ResetOrientation();

		std::string GetName() const;

		void CalculateExposure();

		// Exposure control
		real aperture = 1.0f; // f-stops
		real shutterSpeed = 1.0f / 8.0f; // seconds
		real lightSensitivity = 800.0f; // ISO
		real exposure = 0.0f;

		real FOV = 0.0f;
		real zNear = 0.0f;
		real zFar = 0.0f;

		real moveSpeed = 0.0f;				// WASD or gamepad left stick
		real panSpeed = 0.0f;				// MMB
		real dragDollySpeed = 0.0f;			// RMB
		real scrollDollySpeed = 0.0f;		// Scroll wheel
		real orbitingSpeed = 0.0f;			// Alt-LMB drag
		real mouseRotationSpeed = 0.0f;		// LMB drag
		real gamepadRotationSpeed = 0.0f;	// Gamepad right stick
		real moveSpeedFastMultiplier = 0.0f;
		real moveSpeedSlowMultiplier = 0.0f;
		real turnSpeedFastMultiplier = 0.0f;
		real turnSpeedSlowMultiplier = 0.0f;
		real panSpeedFastMultiplier = 0.0f;
		real panSpeedSlowMultiplier = 0.0f;
		real rollRestorationSpeed = 0.0f;

		glm::vec3 position;

		real yaw;
		real pitch;
		real roll;
		glm::vec3 forward;
		glm::vec3 up;
		glm::vec3 right;

		bool bIsGameplayCam = true;
		bool bIsFirstPerson = false;
		bool bDEBUGCyclable = true;
		bool bPossessPlayer = false;

		CameraType type;

	protected:
		// Sets right, up, and forward based on yaw and pitch
		void CalculateAxisVectorsFromPitchAndYaw();
		void CalculateYawAndPitchFromForward();
		void RecalculateViewProjection();

		void JitterMatrix(glm::mat4& matrix);
		void ClampPitch();

		// Exposure calculations taken from Google's Filament rendering engine
		// Computes the camera's EV100
		// aperture measured in f-stops
		// shutterSpeed measured in seconds
		// sensitivity measured in ISO
		static real CalculateEV100(real aperture, real shutterSpeed, real sensitivity);

		// Computes the exposure normalization factor from the camera's EV100
		static real ComputeExposureNormFactor(real EV100);

		bool m_bInitialized = false;

		std::string m_Name;

		glm::mat4 m_View;
		glm::mat4 m_Proj;
		glm::mat4 m_ViewProjection;
	};
} // namespace flex
