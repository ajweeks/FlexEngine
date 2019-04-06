#pragma once

namespace flex
{
	class BaseCamera
	{
	public:
		BaseCamera(const std::string& cameraName, bool bIsGameplayCam, real FOV = glm::radians(45.0f),
			real zNear = 0.001f, real zFar = 1000.0f);
		virtual ~BaseCamera();

		virtual void Initialize();
		virtual void Update() = 0;
		virtual void Destroy();

		virtual void OnSceneChanged();

		virtual void OnPossess();
		virtual void OnDepossess();

		virtual void DrawImGuiObjects();

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

		void CalculateExposure();

		// Exposure control
		real aperture = 1.0f; // f-stops
		real shutterSpeed = 1.0f / 8.0f; // seconds
		real lightSensitivity = 800.0f; // ISO
		real exposure = 0.0f;

		bool bIsGameplayCam;
		bool bDEBUGCyclable = true;

	protected:
		// Sets m_Right, m_Up, and m_Forward based on m_Yaw and m_Pitch
		void CalculateAxisVectorsFromPitchAndYaw();
		void CalculateYawAndPitchFromForward();
		void RecalculateViewProjection();

		void ClampPitch();

		// Exposure calculations taken from Google's Filament rendering engine
		// Computes the camera's EV100
		// aperture measured in f-stops
		// shutterSpeed measured in seconds
		// sensitivity measured in ISO
		static float CalculateEV100(float aperture, float shutterSpeed, float sensitivity);

		// Computes the exposure normalization factor from the camera's EV100
		static float ComputeExposureNormFactor(float EV100);

		bool m_bInitialized = false;

		std::string m_Name;

		glm::mat4 m_View;
		glm::mat4 m_Proj;
		glm::mat4 m_ViewProjection;

		real m_FOV = 0.0f;
		real m_ZNear = 0.0f;
		real m_ZFar = 0.0f;

		real m_MoveSpeed = 0.0f;				// WASD or gamepad left stick
		real m_PanSpeed = 0.0f;				// MMB
		real m_DragDollySpeed = 0.0f;			// RMB
		real m_ScrollDollySpeed = 0.0f;		// Scroll wheel
		real m_MoveSpeedFastMultiplier = 0.0f;
		real m_MoveSpeedSlowMultiplier = 0.0f;
		real m_TurnSpeedFastMultiplier = 0.0f;
		real m_TurnSpeedSlowMultiplier = 0.0f;
		real m_OrbitingSpeed = 0.0f;			// Alt-LMB drag
		real m_MouseRotationSpeed = 0.0f;		// LMB drag
		real m_GamepadRotationSpeed = 0.0f;	// Gamepad right stick

		glm::vec3 m_Position;

		real m_Yaw = 0.0f;
		real m_Pitch = 0.0f;
		glm::vec3 m_Forward;
		glm::vec3 m_Up;
		glm::vec3 m_Right;
	};
} // namespace flex
