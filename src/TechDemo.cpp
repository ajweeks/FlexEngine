
#include "TechDemo.h"
#include "ShaderUtils.h"

#include <iostream>
#include <string> 

#include <GLFW\glfw3.h>

#include <glm\glm.hpp> // vec3, vec3, mat4
#include <glm\gtc\matrix_transform.hpp> // translate, rotation, scale, perspective
#include <glm\gtc\type_ptr.hpp> // value_ptr

#define ArrayCount(arr) (sizeof(arr[0]) / sizeof(arr))

using namespace glm;

void ErrorCallback(int error, const char* description)
{
	fprintf(stderr, "Error: %s\n", description);
}

void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
	{
		glfwSetWindowShouldClose(window, GLFW_TRUE);
	}
}

// TODO: Consolidate these two functions?
mat4 Transform(const vec2& Orientation, const vec3& Position, const vec3& Up)
{
	mat4 Proj = perspective(radians(45.f), 1.33f, 0.1f, 10.f);
	mat4 ViewTranslate = translate(mat4(1.f), Position);
	mat4 ViewRotateX = rotate(ViewTranslate, Orientation.y, Up);
	mat4 View = rotate(ViewRotateX, Orientation.x, Up);
	mat4 Model = mat4(1.0f);

	return Proj * View * Model;
}

void SetUniformMVP(GLuint Location, const vec3& Position, const vec3& Rotation)
{
	float FOV = 45.0f;
	float aspect = 16.0f / 9.0f;
	float zNear = 0.1f;
	float zFar = 100.0f;
	mat4 Projection = perspective(FOV, aspect, zNear, zFar);

	float Scale = 0.5f;

	mat4 ViewTranslate = translate(mat4(1.0f), Position);
	mat4 ViewRotateX = rotate(ViewTranslate, Rotation.y, vec3(-1.0f, 0.0f, 0.0f));
	mat4 View = rotate(ViewRotateX, Rotation.x, vec3(0.0f, 1.0f, 0.0f));
	mat4 Model = scale(mat4(1.0f), vec3(Scale));
	mat4 MVP = Projection * View * Model;
	glUniformMatrix4fv(Location, 1, GL_FALSE, value_ptr(MVP));
}

vec3 ComputeNormal(const vec3& a, const vec3& b, const vec3& c)
{
	return normalize(cross(c - a, b - a));
}

TechDemo::TechDemo()
{
}

TechDemo::~TechDemo()
{
	glDeleteProgram(m_ProgramID);
	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDeleteBuffers(1, &m_VertexBufferID);
	glDeleteVertexArrays(1, &m_VertexArrayID);
	glDeleteBuffers(1, &m_ColorBufferID);
	glBindVertexArray(0);

	glfwDestroyWindow(m_Window);

	glfwTerminate();
}

void TechDemo::Initialize()
{
	glfwSetErrorCallback(ErrorCallback);

	if (!glfwInit())
	{
		exit(EXIT_FAILURE);
	}

	// Call before window creation
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

	m_Window = glfwCreateWindow(1920, 1080, "Window title", NULL, NULL);
	if (!m_Window)
	{
		glfwTerminate();
		exit(EXIT_FAILURE);
	}

	glfwSetKeyCallback(m_Window, KeyCallback);

	glfwMakeContextCurrent(m_Window);

	gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

	std::cout << "INFO: OpenGL Version: " << glGetString(GL_VERSION) << std::endl;

	glClearColor(0.05f, 0.1f, 0.45f, 0.0f);

	static const GLfloat vertices[] = {
		-1.0f, -1.0f, 0.0f, 1.0f,
		1.0f, -1.0f, 0.0f, 1.0f,
		1.0f, 1.0f, 0.0f, 1.0f,
		-1.0f,  1.0f, 0.0f, 1.0f,
	};

	static const GLfloat colors[] = {
		0.9f, 0.0f, 0.1f, 1.0f,
		0.1f, 1.0f, 0.2f, 1.0f,
		0.0f, 0.2f, 0.8f, 1.0f,
		1.0f, 1.0f, 1.0f, 1.0f
	};

	static const GLuint indices[] = {
		0, 1, 2,
		0, 2, 3
	};

	m_ProgramID = ShaderUtils::LoadShaders("resources/shaders/simple.vert", "resources/shaders/simple.frag");

	glUseProgram(m_ProgramID);

	// Vertex buffer object
	glGenVertexArrays(1, &m_VertexArrayID);
	glBindVertexArray(m_VertexArrayID);

	glGenBuffers(1, &m_VertexBufferID);
	glBindBuffer(GL_ARRAY_BUFFER, m_VertexBufferID);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	GLint posAttrib = glGetAttribLocation(m_ProgramID, "in_Position");
	glVertexAttribPointer(posAttrib, 4, GL_FLOAT, GL_FALSE, 0, 0);
	glEnableVertexAttribArray(0);

	glGenBuffers(1, &m_ColorBufferID);
	glBindBuffer(GL_ARRAY_BUFFER, m_ColorBufferID);
	glBufferData(GL_ARRAY_BUFFER, sizeof(colors), colors, GL_STATIC_DRAW);
	GLint colorAttrib = glGetAttribLocation(m_ProgramID, "in_Color");
	glVertexAttribPointer(colorAttrib, 4, GL_FLOAT, GL_FALSE, 0, 0);
	glEnableVertexAttribArray(1);

	glGenBuffers(1, &m_IndicesBufferID);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IndicesBufferID);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

	m_MVPID = glGetUniformLocation(m_ProgramID, "MVP");

	SetVSyncEnabled(true);
}

void TechDemo::UpdateAndRun()
{
	while (!glfwWindowShouldClose(m_Window))
	{
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glUseProgram(m_ProgramID);

		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);

		float time = (float)glfwGetTime();

		float FOV = 45.0f;
		float aspectRatio = viewport[0] / float(viewport[1]);
		glm::mat4 Projection = glm::perspective(45.0f, 4.0f / 3.0f, 0.1f, 100.0f);

		glm::mat4 View = glm::lookAt(
			glm::vec3(2 + cos(time) * 3.0f, 3, sin(time) * 2.0f),
			glm::vec3(0, 0, 0),
			glm::vec3(0, 1, 0)
		);

		glm::mat4 Model = glm::mat4(1.0f);
		glm::mat4 MVP = Projection * View * Model;
		glUniformMatrix4fv(m_MVPID, 1, GL_FALSE, &MVP[0][0]);

		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

		glfwSwapBuffers(m_Window);
		glfwPollEvents();
	}
}

void TechDemo::SetVSyncEnabled(bool enabled)
{
	m_VSyncEnabled = enabled;
	glfwSwapInterval(m_VSyncEnabled ? 1 : 0);
}

void TechDemo::ToggleVSyncEnabled()
{
	SetVSyncEnabled(!m_VSyncEnabled);
}
