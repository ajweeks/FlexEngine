#pragma once

#include "BaseCamera.hpp"

#include "GameContext.hpp"
#include "InputManager.hpp"

namespace flex
{
	class OverheadCamera final : public BaseCamera
	{
	public:
		OverheadCamera(GameContext& gameContext, real FOV = glm::radians(45.0f), real zNear = 0.1f, real zFar = 10000.0f);
		~OverheadCamera();

		virtual void Update(const GameContext& gameContext) override;

	private:

	};
} // namespace flex
