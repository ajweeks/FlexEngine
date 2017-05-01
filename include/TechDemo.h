#pragma once

#include "GameContext.h"

class FreeCamera;
class InputManager;
class SceneManager;
class Window;

class TechDemo final
{
public:
	TechDemo();
	~TechDemo();

	void Initialize();
	void UpdateAndRender();
	void Stop();
	
private:
	void Destroy();

	Window* m_Window;
	SceneManager* m_SceneManager;
	GameContext m_GameContext;
	FreeCamera* m_DefaultCamera;

	int m_RendererIndex = 0;

	bool m_Running;

	TechDemo(const TechDemo&) = delete;
	TechDemo& operator=(const TechDemo&) = delete;
};
