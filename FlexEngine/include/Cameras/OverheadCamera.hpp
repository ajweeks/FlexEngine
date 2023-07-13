#pragma once

#include "BaseCamera.hpp"

#include "RollingAverage.hpp"

namespace flex
{
	class GameObject;
	class Player;

	class OverheadCamera final : public BaseCamera
	{
	public:
		explicit OverheadCamera(real FOV = glm::radians(45.0f));
		virtual ~OverheadCamera();

		virtual void Initialize() override;
		virtual void OnPostSceneChange() override;
		virtual void FixedUpdate() override;

		virtual void DrawImGuiObjects() override;

	private:
		glm::vec3 GetOffsetPosition(const glm::vec3& pos);
		void SetPosAndLookAt();
		void SetLookAt();
		void FindPlayer();
		void TrackPlayer();

		void ResetValues();

		Player* m_Player0 = nullptr;

		RollingAverage<glm::vec3> m_PlayerPosRollingAvg;
		RollingAverage<glm::vec3> m_PlayerForwardRollingAvg;

		real m_ZoomLevel;
		real m_TargetZoomLevel;
		const real m_MinZoomLevel = 3.0f;
		const real m_MaxZoomLevel = 15.0f;
		const i32 m_ZoomLevels = 7;

		// Where we point at on the ground
		glm::vec3 m_TargetLookAtPos;
		glm::vec3 m_Vel;

	};
} // namespace flex
