#pragma once

#include "BaseCamera.hpp"

namespace flex
{
	class Player;

	class FirstPersonCamera final : public BaseCamera
	{
	public:
		explicit FirstPersonCamera(real FOV = glm::radians(70.0f));
		virtual ~FirstPersonCamera();

		virtual void Initialize() override;
		virtual void OnPostSceneChange() override;
		virtual void FixedUpdate() override;
		virtual void LateUpdate() override;
		virtual void OnPlayerPoseUpdated() override { TrackPlayer(); }

	private:
		void TrackPlayer();
		void FindPlayer();

		Player* m_Player = nullptr;

		// The eye is only the capsule's radius (0.4m) from a wall the player stands against, so the near plane must be well
		// inside that (including its corners, which are further away than its center) or walls get clipped
		static constexpr real MAX_NEAR_PLANE = 0.05f;

	};
} // namespace flex
