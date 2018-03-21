#pragma once

#include "Window/Monitor.hpp"
#include "Types.hpp"

namespace flex
{
	class Window;
	class FreeCamera;
	class InputManager;
	class Renderer;
	class FlexEngine;
	class SceneManager;
	class PhysicsManager;

	struct GameContext
	{
		Window* window = nullptr;
		FreeCamera* camera = nullptr;
		InputManager* inputManager = nullptr;
		Renderer* renderer = nullptr;
		FlexEngine* engineInstance = nullptr;
		SceneManager* sceneManager = nullptr;
		Monitor monitor = {};
		PhysicsManager* physicsManager = nullptr;

		sec elapsedTime = 0;
		sec deltaTime = 0;
	};
} // namespace flex