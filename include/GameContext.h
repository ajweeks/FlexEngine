#pragma once

#include <glm\vec2.hpp>

class Window;
class FreeCamera;
class InputManager;
class Renderer;
class TechDemo;

struct GameContext
{
	Window* window;
	FreeCamera* camera;
	InputManager* inputManager;
	Renderer* renderer;
	TechDemo* mainApp;

	glm::uint program;
	float elapsedTime;
	float deltaTime;
};