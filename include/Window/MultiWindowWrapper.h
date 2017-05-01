#pragma once

#include "Window.h"

#include <vector>

class MultiWindowWrapper : public Window
{
public:
	MultiWindowWrapper(const std::string& title, glm::vec2 size, GameContext& gameContext);
	virtual ~MultiWindowWrapper();

	void AddWindow(Window* window);
	void RemoveWindow(Window* window);

	virtual float GetTime() override;

	virtual void Update(const GameContext& gameContext);
	virtual void PollEvents() override;

	glm::vec2i GetSize() const;
	virtual void SetSize(int width, int height) override;
	bool HasFocus() const;

	void SetTitleString(const std::string& title);
	void SetShowFPSInTitleBar(bool showFPS);
	void SetShowMSInTitleBar(bool showMS);
	// Set to 0 to update window title every frame
	void SetUpdateWindowTitleFrequency(float updateFrequencyinSeconds);

	virtual void SetCursorMode(CursorMode mode) override;

	//GLFWwindow* IsGLFWWindow();

protected:
	virtual void SetWindowTitle(const std::string& title) override;

private:
	MultiWindowWrapper& operator=(const MultiWindowWrapper&) = delete;
	MultiWindowWrapper(const MultiWindowWrapper&) = delete;

	std::vector<Window*> m_Windows;
	
};