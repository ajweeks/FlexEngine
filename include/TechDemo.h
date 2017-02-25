#pragma once

// Include glad *before* glfw
#include <glad\glad.h>
#include <GLFW\glfw3.h>

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

private:

	GLFWwindow* m_Window;

	int m_FramesThisSecond;
	int m_FPS;

	bool m_VSyncEnabled;

	GLuint m_ProgramID;
	GLuint m_MVPID;

	GLuint m_VertexArrayID;

	GLuint m_VertexBufferID;
	GLuint m_ColorBufferID;
	GLuint m_IndicesBufferID;
	GLuint m_TexCoordBufferID;

	GLuint m_TextureID;

	static const int NUM_ICONS = 3;
	GLFWimage icons[NUM_ICONS];

	TechDemo(const TechDemo&) = delete;
	TechDemo& operator=(const TechDemo&) = delete;

};
