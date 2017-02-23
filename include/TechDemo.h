#pragma once

// Include glad *before* glfw
#include <glad\glad.h>

struct GLFWwindow;

class TechDemo final
{
public:
	TechDemo();
	~TechDemo();

	void Initialize();
	
	void UpdateAndRun();
	
	void SetVSyncEnabled(bool enabled);
	void ToggleVSyncEnabled();

private:

	GLFWwindow* m_Window;

	bool m_VSyncEnabled;

	GLuint m_ProgramID;
	GLuint m_MVPID;

	GLuint m_VertexArrayID;

	GLuint m_VertexBufferID;
	GLuint m_ColorBufferID;
	GLuint m_IndicesBufferID;

	TechDemo(const TechDemo&) = delete;
	TechDemo& operator=(const TechDemo&) = delete;

};
