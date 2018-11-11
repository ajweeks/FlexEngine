#pragma once

#include "BaseCamera.hpp"

#include "InputManager.hpp"

namespace flex
{
	class GameObject;

	class OverheadCamera final : public BaseCamera
	{
	public:
		OverheadCamera(real FOV = glm::radians(45.0f), real zNear = 0.1f, real zFar = 10000.0f);
		~OverheadCamera();

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

		real m_ZoomLevel;
		real m_TargetZoomLevel;
		const real m_MinZoomLevel = 3.0f;
		const real m_MaxZoomLevel = 15.0f;
		const i32 m_ZoomLevels = 7;

		// Where we point at on the ground
		glm::vec3 m_TargetLookAtPos;
		glm::vec3 m_TargetLookAtDir;
		glm::vec3 m_Vel;

	};
} // namespace flex
