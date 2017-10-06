#pragma once

#include "Window/Monitor.hpp"

namespace flex
{
	class Window;
	class FreeCamera;
	class InputManager;
	class Renderer;
	class FlexEngine;
	class SceneManager;

	struct GameContext
	{
		Window* window;
		FreeCamera* camera;
		InputManager* inputManager;
		Renderer* renderer;
		FlexEngine* engineInstance;
		SceneManager* sceneManager;
		Monitor monitor;

		float elapsedTime;
		float deltaTime;
	};
} // namespace flex