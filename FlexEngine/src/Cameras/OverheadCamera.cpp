#include "stdafx.hpp"

#include "Cameras/OverheadCamera.hpp"

#include <glm/vec2.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Logger.hpp"
#include "Window/Window.hpp"
#include "Helpers.hpp"

namespace flex
{
	OverheadCamera::OverheadCamera(GameContext& gameContext, real FOV, real zNear, real zFar) :
		BaseCamera("Overhead Camera", gameContext, FOV, zNear, zFar)
	{
		ResetOrientation();
		RecalculateViewProjection(gameContext);
	}

	OverheadCamera::~OverheadCamera()
	{
	}

	void OverheadCamera::Update(const GameContext& gameContext)
	{
	}
} // namespace flex
