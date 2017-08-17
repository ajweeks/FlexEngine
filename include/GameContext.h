#pragma once

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

	float elapsedTime;
	float deltaTime;
};