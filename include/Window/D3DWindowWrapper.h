#pragma once
#if COMPILE_D3D

#include "Window.h"

struct GameContext;

class D3DWindowWrapper : public Window
{
public:
	D3DWindowWrapper(const std::string& title, glm::vec2i size, GameContext& gameContext);
	virtual ~D3DWindowWrapper();
	
	virtual float GetTime() override;

	virtual void Update(const GameContext& gameContext) override;
	virtual void PollEvents() override;
	virtual void SetCursorMode(CursorMode mode) override;

	virtual void SetSize(int width, int height) override;

	static InputManager::Action D3DActionToInputManagerAction(int action);
	static InputManager::KeyCode D3DKeyToInputManagerKey(int keyCode);
	static int D3DModsToInputManagerMods(int windowsMods);
	static InputManager::MouseButton D3DButtonToInputManagerMouseButton(int button);

	HWND GetWindowHandle() const;

private:

	void RegisterWindow(const std::string& title, glm::vec2i size, const GameContext& gameContext);
	virtual void SetWindowTitle(const std::string& title) override;


	HWND m_Window;
	float m_PreviousFrameTime;

	double m_StartingTime;

	D3DWindowWrapper(const D3DWindowWrapper&) = delete;
	D3DWindowWrapper& operator=(const D3DWindowWrapper&) = delete;
};

LRESULT CALLBACK WndProcedure(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
uint16_t ExtractInt(uint16_t orig16BitWord, unsigned from, unsigned to);

// Event callbacks
//void D3DErrorCallback(HWND hWnd, int error, const char* description);
//LRESULT D3DKeyCallback(HWND hWnd, int keyCode, WPARAM wParam, LPARAM lParam);
//LRESULT D3DMouseButtonCallback(HWND hWnd, int button, WPARAM wParam, LPARAM lParam);
//LRESULT D3DWindowFocusCallback(HWND hWnd, int focused);
//LRESULT D3DCursorPosCallback(HWND hWnd, double x, double y);
//LRESULT D3DWindowSizeCallback(HWND hWnd, int width, int height);

#endif // COMPILE_D3D
