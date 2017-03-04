#pragma once

#include <glm\vec2.hpp>

#include <GLFW\glfw3.h>

#include <map>

class InputManager final
{
public:
	InputManager();
	~InputManager();

	void Update();
	void PostUpdate();

	int GetKeyDown(int vkCode);
	bool GetKeyPressed(int vkCode);
	
	void CursorPosCallback(GLFWwindow* window, double x, double y);
	void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
	void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

	glm::vec2 GetMousePosition() const;
	glm::vec2 GetMouseMovement() const;
	int GetMouseButtonDown(int button);
	bool GetMouseButtonClicked(int button);

private:
	struct Key
	{
		int down; // A count of how many frames this key has been down for
	};

	std::map<int, Key> m_Keys;

	static const int MOUSE_BUTTON_COUNT = 3;
	Key m_MouseButtons[MOUSE_BUTTON_COUNT];
	glm::vec2 m_MousePosition;
	glm::vec2 m_PrevMousePosition;

};
