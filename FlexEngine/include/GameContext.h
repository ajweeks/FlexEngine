#pragma once

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

		float elapsedTime;
		float deltaTime;
	};
} // namespace flex