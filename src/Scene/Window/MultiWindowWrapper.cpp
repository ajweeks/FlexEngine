#include "stdafx.h"

#include "Window\MultiWindowWrapper.h"
#include "Logger.h"

MultiWindowWrapper::MultiWindowWrapper(const std::string& title, glm::vec2 size, GameContext& gameContext) :
	Window(title, size, gameContext)
{
}

MultiWindowWrapper::~MultiWindowWrapper()
{
}

void MultiWindowWrapper::AddWindow(Window* window)
{
	for (size_t i = 0; i < m_Windows.size(); ++i)
	{
		if (m_Windows[i] == window) return;
	}

	m_Windows.push_back(window);
}

void MultiWindowWrapper::RemoveWindow(Window* window)
{
	for (size_t i = 0; i < m_Windows.size(); ++i)
	{
		if (m_Windows[i] == window)
		{
			m_Windows.erase(m_Windows.begin() + i);
			return;
		}
	}

	Logger::LogError("Couldn't remove window from MultiWindow!");
}

float MultiWindowWrapper::GetTime()
{
	if (!m_Windows.empty())
	{
		return m_Windows[0]->GetTime();
	}

	return 0.0f;
}

void MultiWindowWrapper::Update(const GameContext & gameContext)
{
	for (size_t i = 0; i < m_Windows.size(); ++i)
	{
		m_Windows[i]->Update(gameContext);
	}
}

void MultiWindowWrapper::PollEvents()
{
	for (size_t i = 0; i < m_Windows.size(); ++i)
	{
		m_Windows[i]->PollEvents();
	}
}

glm::vec2i MultiWindowWrapper::GetSize() const
{
	if (!m_Windows.empty())
	{
		return m_Windows[0]->GetSize();
	}

	return{ 0, 0 };
}

void MultiWindowWrapper::SetSize(int width, int height)
{
	for (size_t i = 0; i < m_Windows.size(); ++i)
	{
		m_Windows[i]->SetSize(width, height);
	}
}

bool MultiWindowWrapper::HasFocus() const
{
	if (!m_Windows.empty())
	{
		return m_Windows[0]->HasFocus();
	}

	return false;
}

void MultiWindowWrapper::SetTitleString(const std::string& title)
{
	for (size_t i = 0; i < m_Windows.size(); ++i)
	{
		m_Windows[i]->SetTitleString(title);
	}
}

void MultiWindowWrapper::SetShowFPSInTitleBar(bool showFPS)
{
	for (size_t i = 0; i < m_Windows.size(); ++i)
	{
		m_Windows[i]->SetShowFPSInTitleBar(showFPS);
	}
}

void MultiWindowWrapper::SetShowMSInTitleBar(bool showMS)
{
	for (size_t i = 0; i < m_Windows.size(); ++i)
	{
		m_Windows[i]->SetShowMSInTitleBar(showMS);
	}
}

void MultiWindowWrapper::SetUpdateWindowTitleFrequency(float updateFrequencyinSeconds)
{
	for (size_t i = 0; i < m_Windows.size(); ++i)
	{
		m_Windows[i]->SetUpdateWindowTitleFrequency(updateFrequencyinSeconds);
	}
}

void MultiWindowWrapper::SetCursorMode(CursorMode mode)
{
	for (size_t i = 0; i < m_Windows.size(); ++i)
	{
		m_Windows[i]->SetCursorMode(mode);
	}
}

//GLFWwindow* MultiWindowWrapper::IsGLFWWindow()
//{
//	if (!m_Windows.empty())
//	{
//		return m_Windows[0]->IsGLFWWindow();
//	}
//
//	return nullptr;
//}

void MultiWindowWrapper::SetWindowTitle(const std::string& title)
{
	// TODO:
	//for (size_t i = 0; i < m_Windows.size(); ++i)
	//{
	//	m_Windows[i]->SetWindowTitle(title);
	//}
}
