#pragma once

#include <glm\vec2.hpp>

class Window;
class FreeCamera;
class InputManager;
class Renderer;
class MainApp;

struct GameContext
{
	Window* window;
	FreeCamera* camera;
	InputManager* inputManager;
	Renderer* renderer;
	MainApp* mainApp;

	glm::uint program;
	float elapsedTime;
	float deltaTime;
};