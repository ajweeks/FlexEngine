#pragma once

#include "FreeCamera.h"
#include "InputManager.h"

#include <glm\vec2.hpp>

struct GameContext
{
	FreeCamera* camera;
	InputManager* inputManager;
	glm::vec2 windowSize;
	bool windowFocused;
	float elapsedTime;
	float deltaTime;
};