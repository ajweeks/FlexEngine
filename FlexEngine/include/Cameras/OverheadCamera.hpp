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

	private:
		void FindPlayers();

		GameObject* player0 = nullptr;
		GameObject* player1 = nullptr;

	};
} // namespace flex
