#pragma once

#include "Typedefs.h"
#include "Primitives.h"
#include "GameContext.h"

// Include glad *before* glfw
#include <glad\glad.h>
#include <GLFW\glfw3.h>

#include <glm\vec2.hpp>

struct GLFWwindow;
class FreeCamera;
class InputManager;

class TechDemo final
{
public:
	TechDemo();
	~TechDemo();

	void Initialize();
	
	void UpdateAndRender();
	
	void SetVSyncEnabled(bool enabled);
	void ToggleVSyncEnabled();

	glm::vec2 GetWindowSize() const;

private:
	// Callback accessors
	void UpdateWindowSize(int width, int height);
	void UpdateWindowSize(glm::vec2i windowSize);
	void UpdateWindowFocused(int focused);

	GLFWwindow* m_Window;

	glm::vec2i m_WindowSize;
	bool m_WindowFocused;

	int m_FramesThisSecond;
	int m_FPS;

	bool m_VSyncEnabled;

	FreeCamera *m_Camera;
	InputManager *m_InputManager;
	GameContext m_GameContext;

	GLuint m_ProgramID;

	// Uniforms
	GLuint m_TextureID;
	
	CubePosCol m_Cube;
	CubePosCol m_Cube2;
	SpherePosCol m_Sphere1;

	static const int NUM_ICONS = 3;
	GLFWimage icons[NUM_ICONS];

	// Allow callbacks to access us
	friend void CursorPosCallback(GLFWwindow* window, double x, double y);
	friend void WindowSizeCallback(GLFWwindow* window, int width, int height);
	friend void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
	friend void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
	friend void ErrorCallback(int error, const char* description);
	friend void WindowFocusCallback(GLFWwindow* window, int focused);

	TechDemo(const TechDemo&) = delete;
	TechDemo& operator=(const TechDemo&) = delete;
};
