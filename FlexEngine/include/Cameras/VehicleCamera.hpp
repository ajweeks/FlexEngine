#pragma once

#include "BaseCamera.hpp"

#include "RollingAverage.hpp"

namespace flex
{
	class GameObject;

	class VehicleCamera final : public BaseCamera
	{
	public:
		explicit VehicleCamera(real FOV = glm::radians(45.0f));
		virtual ~VehicleCamera();

		virtual void Initialize() override;
		virtual void OnSceneChanged() override;
		virtual void Update() override;

		virtual void DrawImGuiObjects() override;

	private:
		glm::vec3 GetOffsetPosition(const glm::vec3& pos);
		void SetPosAndLookAt();
		void SetLookAt();
		void FindPlayer();

		void ResetValues();

		GameObject* m_Player0 = nullptr;

		RollingAverage<glm::vec3> m_PlayerPosRollingAvg;
		RollingAverage<glm::vec3> m_PlayerForwardRollingAvg;

		// Where we point at on the ground
		glm::vec3 m_TargetLookAtPos;
		glm::vec3 m_Vel;
	};
} // namespace flex
