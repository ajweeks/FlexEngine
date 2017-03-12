
#include "Window/Window.h"
#include "Window/GLFWWindowWrapper.h"
#include "Helpers.h"
#include "Logger.h"
#include "InputManager.h"

using namespace glm;

Window::Window(GameContext& gameContext, std::string title, glm::vec2 size) :
	m_TitleString(title),
	m_Size(size),
	m_ShowFPSInWindowTitle(true),
	m_ShowMSInWindowTitle(true),
	m_GameContextRef(gameContext),
	m_UpdateWindowTitleFrequency(0.0f),
	m_SecondsSinceTitleUpdate(0.0f)
{
	gameContext.window = this;
}

Window::~Window()
{
}

void Window::Update(const GameContext& gameContext)
{
	m_GameContextRef = gameContext;

	m_SecondsSinceTitleUpdate += gameContext.deltaTime;
	if (m_SecondsSinceTitleUpdate >= m_UpdateWindowTitleFrequency)
	{
		m_SecondsSinceTitleUpdate = 0.0f;
		SetWindowTitle(GenerateWindowTitle(gameContext.deltaTime));
	}
}

vec2i Window::GetSize() const
{
	return m_Size;
}

void Window::SetSize(int width, int height)
{
	SetSize(vec2(width, height));
}

void Window::SetSize(vec2i windowSize)
{
	m_Size = windowSize;
	glViewport(0, 0, windowSize.x, windowSize.y);
}

bool Window::HasFocus() const
{
	return m_HasFocus;
}

GLFWwindow* Window::IsGLFWWindow() 
{
	GLFWWindowWrapper* subclass = dynamic_cast<GLFWWindowWrapper*>(this);
	if (subclass) return subclass->GetWindow();
	return nullptr;
}

void Window::SetTitleString(std::string title)
{
	m_TitleString = title;
}

std::string Window::GenerateWindowTitle(float dt)
{
	std::string result = m_TitleString;
	if (m_ShowMSInWindowTitle) result += "   " + FloatToString(dt, 3) + " ms";
	if (m_ShowFPSInWindowTitle) result += +" | " + FloatToString(1.0f / dt, 0) + " FPS ";
	return result;
}

void Window::SetShowFPSInTitleBar(bool showFPS)
{
	m_ShowFPSInWindowTitle = showFPS;
}

void Window::SetShowMSInTitleBar(bool showMS)
{
	m_ShowMSInWindowTitle = showMS;
}

void Window::SetUpdateWindowTitleFrequency(float updateFrequencyinSeconds)
{
	m_UpdateWindowTitleFrequency = updateFrequencyinSeconds;
}


// Callbacks
void Window::KeyCallback(int key, int scancode, int action, int mods)
{
	m_GameContextRef.inputManager->KeyCallback(key, scancode, action, mods);
}

void Window::MouseButtonCallback(int button, int action, int mods)
{
	m_GameContextRef.inputManager->MouseButtonCallback(button, action, mods);
}

void Window::WindowFocusCallback(int focused)
{
	m_HasFocus = focused;
}

void Window::CursorPosCallback(double x, double y)
{
	m_GameContextRef.inputManager->CursorPosCallback(x, y);
}

void Window::WindowSizeCallback(int width, int height)
{
	SetSize(width, height);
}
