#pragma once

#include "BaseCamera.hpp"

#include "GameContext.hpp"
#include "InputManager.hpp"

namespace flex
{
	class GameObject;

	class OverheadCamera final : public BaseCamera
	{
	public:
		OverheadCamera(GameContext& gameContext, real FOV = glm::radians(45.0f), real zNear = 0.1f, real zFar = 10000.0f);
		~OverheadCamera();

		virtual void Initialize(const GameContext& gameContext) override;
		virtual void Update(const GameContext& gameContext) override;

	private:
		GameObject* player0 = nullptr;
		GameObject* player1 = nullptr;

	};
} // namespace flex
