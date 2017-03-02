#pragma once

#include "Typedefs.h"
#include "Primitives.h"

// Include glad *before* glfw
#include <glad\glad.h>
#include <GLFW\glfw3.h>

#include <glm\vec2.hpp>

struct GLFWwindow;

class TechDemo final
{
public:
	TechDemo();
	~TechDemo();

	void Initialize();
	
	void UpdateAndRender();
	
	void SetVSyncEnabled(bool enabled);
	void ToggleVSyncEnabled();

	glm::mat4 GetViewProjection() const;


private:
	// Callback accessors
	void SetMousePosition(float x, float y);
	void SetMousePosition(glm::vec2 mousePos);
	void UpdateWindowSize(int width, int height);
	void UpdateWindowSize(glm::vec2i windowSize);
	void UpdateWindowFocused(int focused);

	void CalculateViewProjection(float dt);

	GLFWwindow* m_Window;

	glm::vec2i m_WindowSize;

	int m_FramesThisSecond;
	int m_FPS;

	bool m_VSyncEnabled;

	bool m_WindowFocused;

	// Camera variables
	glm::mat4 m_ViewProjection;
	float m_FOV;
	float m_ZNear;
	float m_ZFar;

	glm::vec2 m_MousePos;

	GLuint m_ProgramID;

	// Uniforms
	GLuint m_TextureID;
	
	CubePosCol m_Cube;
	CubePosCol m_Cube2;
	Sphere m_Sphere1;

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
