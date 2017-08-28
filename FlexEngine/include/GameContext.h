#pragma once

#include "Window/Monitor.h"

namespace flex
{
	class Window;
	class FreeCamera;
	class InputManager;
	class Renderer;
	class FlexEngine;

	struct GameContext
	{
		Window* window;
		FreeCamera* camera;
		InputManager* inputManager;
		Renderer* renderer;
		FlexEngine* engineInstance;
		Monitor monitor;

		float elapsedTime;
		float deltaTime;
	};
} // namespace flex