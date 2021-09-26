#pragma once

#include "BaseCamera.hpp"

namespace flex
{
	class Player;

	class FirstPersonCamera final : public BaseCamera
	{
	public:
		explicit FirstPersonCamera(real FOV = glm::radians(45.0f));
		virtual ~FirstPersonCamera();

		virtual void Initialize() override;
		virtual void OnSceneChanged() override;
		virtual void FixedUpdate() override;

	private:
		void TrackPlayer();
		void FindPlayer();

		Player* m_Player = nullptr;

	};
} // namespace flex
