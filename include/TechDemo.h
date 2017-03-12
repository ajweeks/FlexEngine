#pragma once

#include "Typedefs.h"
#include "Primitives.h"
#include "GameContext.h"
#include "Window/Window.h"

#include <glm\vec2.hpp>

class FreeCamera;
class InputManager;
class SceneManager;

class TechDemo final
{
public:
	TechDemo();
	~TechDemo();

	void Initialize();
	void UpdateAndRender();
	void Stop();
	
private:
	Window* m_Window;
	SceneManager* m_SceneManager;
	GameContext m_GameContext;

	bool m_Running;

	TechDemo(const TechDemo&) = delete;
	TechDemo& operator=(const TechDemo&) = delete;
};
