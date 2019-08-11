#include "stdafx.hpp"

#include "InputManager.hpp"

#include "Callbacks/InputCallbacks.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp" // For WriteFile
#include "JSONParser.hpp"
#include "Window/Window.hpp"

namespace flex
{
	const real InputManager::MAX_JOYSTICK_ROTATION_SPEED = 15.0f;
	const std::string InputManager::s_InputBindingFilePath = ROOT_LOCATION "config/input-bindings.ini";

	void InputManager::Initialize()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.KeyMap[ImGuiKey_Tab] = (i32)KeyCode::KEY_TAB;
		io.KeyMap[ImGuiKey_LeftArrow] = (i32)KeyCode::KEY_LEFT;
		io.KeyMap[ImGuiKey_RightArrow] = (i32)KeyCode::KEY_RIGHT;
		io.KeyMap[ImGuiKey_UpArrow] = (i32)KeyCode::KEY_UP;
		io.KeyMap[ImGuiKey_DownArrow] = (i32)KeyCode::KEY_DOWN;
		io.KeyMap[ImGuiKey_PageUp] = (i32)KeyCode::KEY_PAGE_UP;
		io.KeyMap[ImGuiKey_PageDown] = (i32)KeyCode::KEY_PAGE_DOWN;
		io.KeyMap[ImGuiKey_Home] = (i32)KeyCode::KEY_HOME;
		io.KeyMap[ImGuiKey_End] = (i32)KeyCode::KEY_END;
		io.KeyMap[ImGuiKey_Insert] = (i32)KeyCode::KEY_INSERT;
		io.KeyMap[ImGuiKey_Delete] = (i32)KeyCode::KEY_DELETE;
		io.KeyMap[ImGuiKey_Backspace] = (i32)KeyCode::KEY_BACKSPACE;
		io.KeyMap[ImGuiKey_Space] = (i32)KeyCode::KEY_SPACE;
		io.KeyMap[ImGuiKey_Enter] = (i32)KeyCode::KEY_ENTER;
		io.KeyMap[ImGuiKey_Escape] = (i32)KeyCode::KEY_ESCAPE;
		io.KeyMap[ImGuiKey_A] = (i32)KeyCode::KEY_A; // for text edit CTRL+A: select all
		io.KeyMap[ImGuiKey_C] = (i32)KeyCode::KEY_C; // for text edit CTRL+C: copy
		io.KeyMap[ImGuiKey_V] = (i32)KeyCode::KEY_V; // for text edit CTRL+V: paste
		io.KeyMap[ImGuiKey_X] = (i32)KeyCode::KEY_X; // for text edit CTRL+X: cut
		io.KeyMap[ImGuiKey_Y] = (i32)KeyCode::KEY_Y; // for text edit CTRL+Y: redo
		io.KeyMap[ImGuiKey_Z] = (i32)KeyCode::KEY_Z; // for text edit CTRL+Z: undo

		if (!LoadInputBindingsFromFile())
		{
			// Immediately save input bindings file when file doesn't exist or is incomplete
			SaveInputBindingsToFile();
		}

		m_GamepadStates[0].averageRotationSpeeds = RollingAverage<real>(m_GamepadStates[0].framesToAverageOver);
		m_GamepadStates[1].averageRotationSpeeds = RollingAverage<real>(m_GamepadStates[1].framesToAverageOver);

		ClearAllInputs();
	}

	void InputManager::Update()
	{
		if (!g_Window->HasFocus())
		{
			ClearAllInputs();
			return;
		}

		// Keyboard keys
		for (auto& keyPair : m_Keys)
		{
			if (keyPair.second.down > 0)
			{
				++keyPair.second.down;
			}
		}

#if 0 // Log mouse states
		Print("states:   ");
		for (u32 i = 0; i < MOUSE_BUTTON_COUNT; ++i)
		{
			Print("%d, ", (m_MouseButtonStates & (1 << i)) != 0 ? 1 : 0);
		}
		Print("\n");

		Print("pressed:  ");
		for (u32 i = 0; i < MOUSE_BUTTON_COUNT; ++i)
		{
			Print("%d, ",  (m_MouseButtonsPressed & (1 << i)) != 0 ? 1 : 0);
		}
		Print("\n");

		Print("released: ");
		for (u32 i = 0; i < MOUSE_BUTTON_COUNT; ++i)
		{
			Print("%d, ", (m_MouseButtonsReleased & (1 << i)) != 0 ? 1 : 0);
		}
		Print("\n");

		ImGuiIO& io = ImGui::GetIO();
		Print("ImGui::IO.mousePos: %.1f, %.1f\n", io.MousePos.x, io.MousePos.y);
		Print("\n");
#endif

		// Mouse buttons
		for (u32 i = 0; i < MOUSE_BUTTON_COUNT; ++i)
		{
			m_MouseButtonsPressed  &= ~(1 << i);
			m_MouseButtonsReleased &= ~(1 << i);
			if (m_MouseButtonStates & (1 << i))
			{
				m_MouseButtonDrags[i].endLocation = m_MousePosition;
			}
		}

		// Gamepads
		for (i32 i = 0; i < 2; ++i)
		{
			GamepadState& gamepadState = m_GamepadStates[i];

			real joystickX = GetGamepadAxisValue(i, GamepadAxis::RIGHT_STICK_X);
			real joystickY = GetGamepadAxisValue(i, GamepadAxis::RIGHT_STICK_Y);

			i32 currentQuadrant = -1;

			real minimumExtensionLength = 0.35f;
			real extensionLength = glm::length(glm::vec2(joystickX, joystickY));
			if (extensionLength > minimumExtensionLength)
			{
				if (gamepadState.previousQuadrant != -1)
				{
					real currentAngle = atan2(joystickY, joystickX) + PI;
					real previousAngle = atan2(gamepadState.pJoystickY, gamepadState.pJoystickX) + PI;
					// Asymptote occurs on left
					if (joystickX < 0.0f)
					{
						if (gamepadState.pJoystickY < 0.0f && joystickY >= 0.0f)
						{
							// CCW
							currentAngle -= TWO_PI;
						}
						else if (gamepadState.pJoystickY >= 0.0f && joystickY < 0.0f)
						{
							// CW
							currentAngle += TWO_PI;
						}
					}

					real stickRotationSpeed = (currentAngle - previousAngle) / g_DeltaTime;
					stickRotationSpeed = glm::clamp(stickRotationSpeed,
													-MAX_JOYSTICK_ROTATION_SPEED,
													MAX_JOYSTICK_ROTATION_SPEED);

					gamepadState.averageRotationSpeeds.AddValue(stickRotationSpeed);
				}

				if (joystickX > 0.0f)
				{
					currentQuadrant = (joystickY > 0.0f ? 2 : 1);
				}
				else
				{
					currentQuadrant = (joystickY > 0.0f ? 3 : 0);
				}
			}
			else
			{
				gamepadState.averageRotationSpeeds.Reset();
			}

			gamepadState.previousQuadrant = currentQuadrant;

			gamepadState.pJoystickX = joystickX;
			gamepadState.pJoystickY = joystickY;
		}
	}

	void InputManager::PostUpdate()
	{
		//glm::vec2i dPos = (m_MousePosition - m_PrevMousePosition);
		//Print("%i, %i\n", dPos.x, dPos.y);
		//i32 threshold = 300;
		//m_bMouseWrapped = abs(dPos.x) > threshold ||
		//	abs(dPos.y) > threshold;

		for (auto iter : m_Keys)
		{
			iter.second.pDown = iter.second.down;
		}

		m_bMouseWrapped = false;
		m_PrevMousePosition = m_MousePosition;
		m_ScrollXOffset = 0.0f;
		m_ScrollYOffset = 0.0f;
	}

	void InputManager::UpdateGamepadState(i32 gamepadIndex, real axes[6], u8 buttons[15])
	{
		assert(gamepadIndex == 0 || gamepadIndex == 1);

		m_pGamepadStates[gamepadIndex] = m_GamepadStates[gamepadIndex];

		for (i32 i = 0; i < 6; ++i)
		{
			m_GamepadStates[gamepadIndex].axes[i] = axes[i];
		}

		// Invert Y (0 at bottom, 1 at top)
		// TODO: Make option for player
		m_GamepadStates[gamepadIndex].axes[1] = -m_GamepadStates[gamepadIndex].axes[1];
		m_GamepadStates[gamepadIndex].axes[3] = -m_GamepadStates[gamepadIndex].axes[3];

		// Map triggers into range [0.0,1.0]
		m_GamepadStates[gamepadIndex].axes[4] = m_GamepadStates[gamepadIndex].axes[4] * 0.5f + 0.5f;
		m_GamepadStates[gamepadIndex].axes[5] = m_GamepadStates[gamepadIndex].axes[5] * 0.5f + 0.5f;

		HandleRadialDeadZone(&m_GamepadStates[gamepadIndex].axes[0], &m_GamepadStates[gamepadIndex].axes[1]);
		HandleRadialDeadZone(&m_GamepadStates[gamepadIndex].axes[2], &m_GamepadStates[gamepadIndex].axes[3]);

		u32 pStates = m_GamepadStates[gamepadIndex].buttonStates;

		m_GamepadStates[gamepadIndex].buttonStates = 0;
		for (i32 i = 0; i < GAMEPAD_BUTTON_COUNT; ++i)
		{
			m_GamepadStates[gamepadIndex].buttonStates = m_GamepadStates[gamepadIndex].buttonStates | (buttons[i] << i);
		}

		u32 changedButtons = m_GamepadStates[gamepadIndex].buttonStates ^ pStates;
		m_GamepadStates[gamepadIndex].buttonsPressed = changedButtons & m_GamepadStates[gamepadIndex].buttonStates;
		m_GamepadStates[gamepadIndex].buttonsReleased = changedButtons & (~m_GamepadStates[gamepadIndex].buttonStates);

		for (i32 i = 0; i < GAMEPAD_BUTTON_COUNT; ++i)
		{
			if (IsGamepadButtonPressed(gamepadIndex, (GamepadButton)i))
			{
				Action gamepadButtonAction = GetActionFromGamepadButton((GamepadButton)i);
				if (gamepadButtonAction != Action::_NONE)
				{
					for (auto iter = m_ActionCallbacks.begin(); iter != m_ActionCallbacks.end(); ++iter)
					{
						if (iter->first->Execute(gamepadButtonAction) == EventReply::CONSUMED)
						{
							break;
						}
					}
				}
			}
		}
	}

	GamepadState& InputManager::GetGamepadState(i32 gamepadIndex)
	{
		assert(gamepadIndex == 0 || gamepadIndex == 1);
		return m_GamepadStates[gamepadIndex];
	}

	i32 InputManager::GetActionDown(Action action) const
	{
		// TODO: Set gamepad index correctly
		i32 gamepadIndex = 0;

		const InputBinding& binding = m_InputBindings[(i32)action];
		if (binding.keyCode != KeyCode::_NONE)
		{
			i32 down = GetKeyDown(binding.keyCode);
			if (down > 0)
			{
				return down;
			}
		}
		if (binding.mouseButton != MouseButton::_NONE)
		{
			if (IsMouseButtonDown(binding.mouseButton))
			{
				// TODO: Return 2 to prevent click handling?
				return 1;
			}
		}
		if (binding.gamepadButton != GamepadButton::_NONE)
		{
			if (IsGamepadButtonDown(gamepadIndex, binding.gamepadButton))
			{
				return 1;
			}
		}
		if (binding.gamepadAxis != GamepadAxis::_NONE)
		{
			real axisValue = GetGamepadAxisValue(gamepadIndex, binding.gamepadAxis);
			if (axisValue >= 0.5f)
			{
				return 1;
			}
		}
		if (binding.mouseAxis == MouseAxis::SCROLL_Y)
		{
			if (m_ScrollYOffset > 0.0f)
			{
				return 1;
			}
		}
		else if (binding.mouseAxis == MouseAxis::SCROLL_X)
		{
			if (m_ScrollXOffset > 0.0f)
			{
				return 1;
			}
		}

		return 0;
	}

	bool InputManager::GetActionPressed(Action action) const
	{
		// TODO: Set gamepad index correctly
		i32 gamepadIndex = 0;

		const InputBinding& binding = m_InputBindings[(i32)action];
		if (binding.keyCode != KeyCode::_NONE)
		{
			if (GetKeyPressed(binding.keyCode))
			{
				return true;
			}
		}
		if (binding.mouseButton != MouseButton::_NONE)
		{
			if (IsMouseButtonPressed(binding.mouseButton))
			{
				// TODO: Return 2 to prevent click handling?
				return true;
			}
		}
		if (binding.gamepadButton != GamepadButton::_NONE)
		{
			if (IsGamepadButtonPressed(gamepadIndex, binding.gamepadButton))
			{
				return true;
			}
		}
		if (binding.gamepadAxis != GamepadAxis::_NONE)
		{
			real pAxisValue = GetPrevGamepadAxisValue(gamepadIndex, binding.gamepadAxis);
			real axisValue = GetGamepadAxisValue(gamepadIndex, binding.gamepadAxis);
			if (axisValue >= 0.5f && pAxisValue < 0.5f)
			{
				return true;
			}
		}
		if (binding.mouseAxis == MouseAxis::SCROLL_Y)
		{
			if (m_ScrollYOffset > 0.0f)
			{
				return true;
			}
		}
		else if (binding.mouseAxis == MouseAxis::SCROLL_X)
		{
			if (m_ScrollXOffset > 0.0f)
			{
				return true;
			}
		}

		return false;
	}

	bool InputManager::GetActionReleased(Action action) const
	{
		// TODO: Set gamepad index correctly
		i32 gamepadIndex = 0;

		const InputBinding& binding = m_InputBindings[(i32)action];
		if (binding.keyCode != KeyCode::_NONE)
		{
			if (GetKeyReleased(binding.keyCode))
			{
				return true;
			}
		}
		if (binding.mouseButton != MouseButton::_NONE)
		{
			if (IsMouseButtonReleased(binding.mouseButton))
			{
				// TODO: Return 2 to prevent click handling?
				return true;
			}
		}
		if (binding.gamepadButton != GamepadButton::_NONE)
		{
			if (IsGamepadButtonReleased(gamepadIndex, binding.gamepadButton))
			{
				return true;
			}
		}
		if (binding.gamepadAxis != GamepadAxis::_NONE)
		{
			real pAxisValue = GetPrevGamepadAxisValue(gamepadIndex, binding.gamepadAxis);
			real axisValue = GetGamepadAxisValue(gamepadIndex, binding.gamepadAxis);
			if (axisValue >= 0.5f && pAxisValue < 0.5f)
			{
				return true;
			}
		}
		if (binding.mouseAxis == MouseAxis::SCROLL_Y)
		{
			if (m_ScrollYOffset > 0.0f)
			{
				return true;
			}
		}
		else if (binding.mouseAxis == MouseAxis::SCROLL_X)
		{
			if (m_ScrollXOffset > 0.0f)
			{
				return true;
			}
		}

		return false;
	}

	real InputManager::GetActionAxisValue(Action action) const
	{
		// TODO: Set gamepad index correctly
		i32 gamepadIndex = 0;

		const InputBinding& binding = m_InputBindings[(i32)action];
		if (binding.keyCode != KeyCode::_NONE)
		{
			if (GetKeyDown(binding.keyCode) > 0)
			{
				if (binding.bNegative)
				{
					return -1.0f;
				}
				else
				{
					return 1.0f;
				}
			}
		}
		if (binding.mouseButton != MouseButton::_NONE)
		{
			if (IsMouseButtonDown(binding.mouseButton))
			{
				if (binding.bNegative)
				{
					return -1.0f;
				}
				else
				{
					return 1.0f;
				}
			}
		}
		if (binding.gamepadButton != GamepadButton::_NONE)
		{
			if (IsGamepadButtonDown(gamepadIndex, binding.gamepadButton))
			{
				if (binding.bNegative)
				{
					return -1.0f;
				}
				else
				{
					return 1.0f;
				}
			}
		}
		if (binding.gamepadAxis != GamepadAxis::_NONE)
		{
			real axisValue = GetGamepadAxisValue(gamepadIndex, binding.gamepadAxis);
			if ((binding.bNegative && axisValue > 0.0f) ||
				(!binding.bNegative && axisValue < 0.0f))
			{
				return axisValue;
			}
		}
		if (binding.mouseAxis == MouseAxis::SCROLL_Y)
		{
			if (m_ScrollYOffset != 0.0f)
			{
				if (binding.bNegative)
				{
					return -m_ScrollYOffset;
				}
				else
				{
					return m_ScrollYOffset;
				}
			}
		}
		else if (binding.mouseAxis == MouseAxis::SCROLL_X)
		{
			if (m_ScrollXOffset != 0.0f)
			{
				if (binding.bNegative)
				{
					return m_ScrollXOffset;
				}
				else
				{
					return -m_ScrollXOffset;
				}
			}
		}

		return 0.0f;
	}

	i32 InputManager::GetKeyDown(KeyCode keyCode, bool bIgnoreImGui /* = false */) const
	{
		if (!bIgnoreImGui && ImGui::GetIO().WantCaptureKeyboard)
		{
			return 0;
		}

		auto iter = m_Keys.find(keyCode);
		if (iter != m_Keys.end())
		{
			return m_Keys.at(keyCode).down;
		}

		return 0;
	}

	bool InputManager::GetKeyPressed(KeyCode keyCode, bool bIgnoreImGui /* = false */) const
	{
		return GetKeyDown(keyCode, bIgnoreImGui) == 1;
	}

	bool InputManager::GetKeyReleased(KeyCode keyCode, bool bIgnoreImGui /* = false */) const
	{
		if (!bIgnoreImGui && ImGui::GetIO().WantCaptureKeyboard)
		{
			return false;
		}

		auto iter = m_Keys.find(keyCode);
		if (iter != m_Keys.end())
		{
			const Key& key = iter->second;
			return key.down == 0 && key.pDown > 0;
		}

		return false;
	}

	bool InputManager::IsGamepadButtonDown(i32 gamepadIndex, GamepadButton button) const
	{
		assert(gamepadIndex == 0 || gamepadIndex == 1);

		bool bDown = ((m_GamepadStates[gamepadIndex].buttonStates & (1 << (i32)button)) != 0);
		return bDown;
	}

	bool InputManager::IsGamepadButtonPressed(i32 gamepadIndex, GamepadButton button) const
	{
		assert(gamepadIndex == 0 || gamepadIndex == 1);

		bool bPressed = ((m_GamepadStates[gamepadIndex].buttonsPressed & (1 << (i32)button)) != 0);
		return bPressed;
	}

	bool InputManager::IsGamepadButtonReleased(i32 gamepadIndex, GamepadButton button) const
	{
		assert(gamepadIndex == 0 || gamepadIndex == 1);

		bool bReleased = ((m_GamepadStates[gamepadIndex].buttonsReleased & (1 << (i32)button)) != 0);
		return bReleased;
	}

	real InputManager::GetPrevGamepadAxisValue(i32 gamepadIndex, GamepadAxis axis) const
	{
		assert(gamepadIndex == 0 || gamepadIndex == 1);

		real axisValue = m_pGamepadStates[gamepadIndex].axes[(i32)axis];
		return axisValue;
	}

	real InputManager::GetGamepadAxisValue(i32 gamepadIndex, GamepadAxis axis) const
	{
		assert(gamepadIndex == 0 || gamepadIndex == 1);

		real axisValue = m_GamepadStates[gamepadIndex].axes[(i32)axis];
		return axisValue;
	}

	bool InputManager::HasGamepadAxisValueJustPassedThreshold(i32 gamepadIndex, GamepadAxis axis, real threshold) const
	{
		assert(gamepadIndex == 0 || gamepadIndex == 1);

		return (m_GamepadStates[gamepadIndex].axes[(i32)axis] >= threshold &&
			m_pGamepadStates[gamepadIndex].axes[(i32)axis] < threshold);
	}

	void InputManager::CursorPosCallback(double x, double y)
	{
		m_MousePosition = glm::vec2((real)x, (real)y);

		ImGuiIO& io = ImGui::GetIO();

		if (IsAnyMouseButtonDown(true) && !io.WantCaptureMouse)
		{
			glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
			if (m_MousePosition.x >= (real)(frameBufferSize.x - 1))
			{
				m_bMouseWrapped = true;
				m_MousePosition.x -= (frameBufferSize.x - 1);
				m_PrevMousePosition.x = m_MousePosition.x;
				io.MousePosPrev.x = m_MousePosition.x;
				for (MouseDrag& drag : m_MouseButtonDrags)
				{
					drag.startLocation -= glm::vec2(frameBufferSize.x - 1, 0.0f);
					drag.endLocation = m_MousePosition;
				}
				g_Window->SetCursorPos(m_MousePosition);
			}
			else if (m_MousePosition.x <= 0)
			{
				m_bMouseWrapped = true;
				m_MousePosition.x += (frameBufferSize.x - 1);
				m_PrevMousePosition.x = m_MousePosition.x;
				io.MousePosPrev.x = m_MousePosition.x;
				for (MouseDrag& drag : m_MouseButtonDrags)
				{
					drag.startLocation += glm::vec2(frameBufferSize.x - 1, 0.0f);
					drag.endLocation = m_MousePosition;
				}
				g_Window->SetCursorPos(m_MousePosition);
			}

			if (m_MousePosition.y >= (real)(frameBufferSize.y - 1))
			{
				m_bMouseWrapped = true;
				m_MousePosition.y -= (frameBufferSize.y - 1);
				m_PrevMousePosition.y = m_MousePosition.y;
				io.MousePosPrev.y = m_MousePosition.y;
				for (MouseDrag& drag : m_MouseButtonDrags)
				{
					drag.startLocation -= glm::vec2(0.0f, frameBufferSize.y - 1);
					drag.endLocation = m_MousePosition;
				}
				g_Window->SetCursorPos(m_MousePosition);
			}
			else if (m_MousePosition.y <= 0)
			{
				m_bMouseWrapped = true;
				m_MousePosition.y += (frameBufferSize.y - 1);
				m_PrevMousePosition.y = m_MousePosition.y;
				io.MousePosPrev.y = m_MousePosition.y;
				for (MouseDrag& drag : m_MouseButtonDrags)
				{
					drag.startLocation += glm::vec2(0.0f, frameBufferSize.y - 1);
					drag.endLocation = m_MousePosition;
				}
				g_Window->SetCursorPos(m_MousePosition);
			}
		}

		io.MousePos = m_MousePosition;

		if (m_PrevMousePosition.x == -1.0f)
		{
			io.MousePosPrev = m_MousePosition;
			m_PrevMousePosition = m_MousePosition;
		}

		if (!io.WantCaptureMouse)
		{
			const glm::vec2 dMousePos = m_MousePosition - m_PrevMousePosition;
			for (auto iter = m_MouseMovedCallbacks.begin(); iter != m_MouseMovedCallbacks.end(); ++iter)
			{
				if (iter->first->Execute(dMousePos) == EventReply::CONSUMED)
				{
					break;
				}
			}
		}
	}

	void InputManager::MouseButtonCallback(MouseButton mouseButton, KeyAction action, i32 mods)
	{
		UNREFERENCED_PARAMETER(mods);

		assert((u32)mouseButton < MOUSE_BUTTON_COUNT);

		if (action == KeyAction::PRESS)
		{
			if (m_MousePosition.x == -1.0f)
			{
				m_MousePosition = g_Window->GetMousePosition();
			}

			m_MouseButtonStates |= (1 << (u32)mouseButton);
			m_MouseButtonsPressed |= (1 << (u32)mouseButton);
			m_MouseButtonsReleased &= ~(1 << (u32)mouseButton);

			m_MouseButtonDrags[(i32)mouseButton].startLocation = m_MousePosition;
			m_MouseButtonDrags[(i32)mouseButton].endLocation = m_MousePosition;
		}
		else if (action == KeyAction::RELEASE)
		{
			m_MouseButtonStates    &= ~(1 << (u32)mouseButton);
			m_MouseButtonsPressed  &= ~(1 << (u32)mouseButton);
			m_MouseButtonsReleased |=  (1 << (u32)mouseButton);


			if (m_MousePosition.x == -1.0f)
			{
				m_MousePosition = g_Window->GetMousePosition();
			}

			m_MouseButtonDrags[(i32)mouseButton].endLocation = m_MousePosition;
		}

		ImGuiIO& io = ImGui::GetIO();
		if ((i32)mouseButton <= 4)
		{
			io.MouseDown[(i32)mouseButton] = (m_MouseButtonStates & (1 << (i32)mouseButton)) != 0;
		}

		if (!io.WantCaptureMouse)
		{
			// Call callbacks present in m_ActionCallbacks & m_MouseButtonCallbacks according to priorities
			auto actionIter = m_ActionCallbacks.end();
			auto mouseButtonIter = m_MouseButtonCallbacks.begin();
			Action mouseButtonAction = Action::_NONE;

			if (action == KeyAction::PRESS)
			{
				mouseButtonAction = GetActionFromMouseButton(mouseButton);
				if (mouseButtonAction != Action::_NONE)
				{
					actionIter = m_ActionCallbacks.begin();
				}
			}

			bool bEventsInQueue = (actionIter != m_ActionCallbacks.end()) ||
								  (mouseButtonIter != m_MouseButtonCallbacks.end());
			while (bEventsInQueue)
			{
				if (actionIter == m_ActionCallbacks.end())
				{
					if (mouseButtonIter->first->Execute(mouseButton, action) == EventReply::CONSUMED)
					{
						break;
					}
					++mouseButtonIter;
				}
				else if (mouseButtonIter == m_MouseButtonCallbacks.end())
				{
					if (actionIter->first->Execute(mouseButtonAction) == EventReply::CONSUMED)
					{
						break;
					}
					++actionIter;
				}
				else
				{
					if (actionIter->second >= mouseButtonIter->second)
					{
						if (actionIter->first->Execute(mouseButtonAction) == EventReply::CONSUMED)
						{
							break;
						}
						++actionIter;
					}
					else
					{
						if (mouseButtonIter->first->Execute(mouseButton, action) == EventReply::CONSUMED)
						{
							break;
						}
						++mouseButtonIter;
					}
				}

				bEventsInQueue = (actionIter != m_ActionCallbacks.end()) ||
								 (mouseButtonIter != m_MouseButtonCallbacks.end());
			}
		}
	}

	void InputManager::ScrollCallback(double xOffset, double yOffset)
	{
		m_ScrollXOffset = (real)xOffset;
		m_ScrollYOffset = (real)yOffset;

		ImGuiIO& io = ImGui::GetIO();
		io.MouseWheelH += m_ScrollXOffset * 0.5f;
		io.MouseWheel += m_ScrollYOffset * 0.5f;
	}

	void InputManager::KeyCallback(KeyCode keyCode, KeyAction action, i32 mods)
	{
		UNREFERENCED_PARAMETER(mods);

		m_Keys[keyCode].pDown = m_Keys[keyCode].down;

		if (action == KeyAction::PRESS)
		{
			m_Keys[keyCode].down = 1;
		}
		else if (action == KeyAction::REPEAT)
		{
			// Ignore repeat events (we're already counting how long the key is down for
		}
		else if (action == KeyAction::RELEASE)
		{
			m_Keys[keyCode].down = 0;
		}

		ImGuiIO& io = ImGui::GetIO();
		io.KeysDown[(i32)keyCode] = m_Keys[keyCode].down > 0;

		io.KeyCtrl = GetKeyDown(KeyCode::KEY_LEFT_CONTROL, true) || GetKeyDown(KeyCode::KEY_RIGHT_CONTROL, true);
		io.KeyShift = GetKeyDown(KeyCode::KEY_LEFT_SHIFT, true) || GetKeyDown(KeyCode::KEY_RIGHT_SHIFT, true);
		io.KeyAlt = GetKeyDown(KeyCode::KEY_LEFT_ALT, true) || GetKeyDown(KeyCode::KEY_RIGHT_ALT, true);
		io.KeySuper = GetKeyDown(KeyCode::KEY_LEFT_SUPER, true) || GetKeyDown(KeyCode::KEY_RIGHT_SUPER, true);

		if (!io.WantCaptureKeyboard)
		{
			// Call callbacks present in m_ActionCallbacks & m_KeyEventCallbacks according to priorities
			auto actionIter = m_ActionCallbacks.end();
			auto keyEventIter = m_KeyEventCallbacks.begin();
			Action keyPressAction = Action::_NONE;

			if (action == KeyAction::PRESS)
			{
				keyPressAction = GetActionFromKeyCode(keyCode);
				if (keyPressAction != Action::_NONE)
				{
					actionIter = m_ActionCallbacks.begin();
				}
			}

			bool bEventsInQueue = (actionIter != m_ActionCallbacks.end()) ||
								  (keyEventIter != m_KeyEventCallbacks.end());
			while (bEventsInQueue)
			{
				if (actionIter == m_ActionCallbacks.end())
				{
					if (keyEventIter->first->Execute(keyCode, action, mods) == EventReply::CONSUMED)
					{
						break;
					}
					++keyEventIter;
				}
				else if (keyEventIter == m_KeyEventCallbacks.end())
				{
					if (actionIter->first->Execute(keyPressAction) == EventReply::CONSUMED)
					{
						break;
					}
					++actionIter;
				}
				else
				{
					if (actionIter->second >= keyEventIter->second)
					{
						if (actionIter->first->Execute(keyPressAction) == EventReply::CONSUMED)
						{
							break;
						}
						++actionIter;
					}
					else
					{
						if (keyEventIter->first->Execute(keyCode, action, mods) == EventReply::CONSUMED)
						{
							break;
						}
						++keyEventIter;
					}
				}

				bEventsInQueue = (actionIter != m_ActionCallbacks.end()) ||
								 (keyEventIter != m_KeyEventCallbacks.end());
			}
		}
	}

	void InputManager::CharCallback(u32 character)
	{
		ImGuiIO& io = ImGui::GetIO();
		if (character > 0 && character < 0x10000)
		{
			io.AddInputCharacter((ImWchar)character);
		}
	}

	bool InputManager::DidMouseWrap() const
	{
		return m_bMouseWrapped;
	}

	glm::vec2 InputManager::GetMousePosition() const
	{
		return m_MousePosition;
	}

	void InputManager::ClearMouseMovement()
	{
		m_PrevMousePosition = m_MousePosition;
	}

	void InputManager::SetMousePosition(glm::vec2 mousePos, bool bUpdatePreviousPos)
	{
		m_MousePosition = mousePos;

		if (bUpdatePreviousPos)
		{
			m_PrevMousePosition = m_MousePosition;
		}
	}

	glm::vec2 InputManager::GetMouseMovement() const
	{
		if (ImGui::GetIO().WantCaptureMouse)
		{
			return VEC2_ZERO;
		}

		if (m_MousePosition == VEC2_NEG_ONE ||
			m_PrevMousePosition == VEC2_NEG_ONE)
		{
			return VEC2_ZERO;
		}

		return m_MousePosition - m_PrevMousePosition;
	}

	void InputManager::ClearMouseButton(MouseButton mouseButton)
	{
		m_MouseButtonStates &= ~(1 << ((i32)mouseButton));
		m_MouseButtonsPressed &= ~(1 << ((i32)mouseButton));
		m_MouseButtonsReleased &= ~(1 << ((i32)mouseButton));
	}

	bool InputManager::IsAnyMouseButtonDown(bool bIgnoreImGui /* = false */) const
	{
		if (!bIgnoreImGui && ImGui::GetIO().WantCaptureMouse)
		{
			return false;
		}

		for (i32 i = 0; i < MOUSE_BUTTON_COUNT; ++i)
		{
			if ((m_MouseButtonStates & (1 << i)) != 0)
			{
				return true;
			}
		}

		return false;
	}

	bool InputManager::IsMouseButtonDown(MouseButton mouseButton) const
	{
		if (ImGui::GetIO().WantCaptureMouse)
		{
			return false;
		}

		assert((i32)mouseButton >= 0 && (i32)mouseButton <= MOUSE_BUTTON_COUNT - 1);

		return (m_MouseButtonStates & (1 << (i32)mouseButton)) != 0;
	}

	bool InputManager::IsMouseButtonPressed(MouseButton mouseButton) const
	{
		if (ImGui::GetIO().WantCaptureMouse)
		{
			return false;
		}

		assert((i32)mouseButton >= 0 && (i32)mouseButton <= MOUSE_BUTTON_COUNT - 1);

		return (m_MouseButtonsPressed & (1 << (i32)mouseButton)) != 0;
	}

	bool InputManager::IsMouseButtonReleased(MouseButton mouseButton) const
	{
		if (ImGui::GetIO().WantCaptureMouse)
		{
			return false;
		}

		assert((i32)mouseButton >= 0 && (i32)mouseButton <= MOUSE_BUTTON_COUNT - 1);

		return (m_MouseButtonsReleased & (1 << (i32)mouseButton)) != 0;
	}

	real InputManager::GetVerticalScrollDistance() const
	{
		if (ImGui::GetIO().WantCaptureMouse)
		{
			return 0.0f;
		}

		return m_ScrollYOffset;
	}

	void InputManager::ClearVerticalScrollDistance()
	{
		m_ScrollYOffset = 0;
	}

	bool InputManager::IsMouseHoveringRect(const glm::vec2& posNorm, const glm::vec2& sizeNorm)
	{
		glm::vec2 posPixels;
		glm::vec2 sizePixels;
		g_Renderer->TransformRectToScreenSpace(posNorm, sizeNorm, posPixels, sizePixels);
		glm::vec2 halfSizePixels = sizePixels / 2.0f;

		bool bHoveringInArea = (m_MousePosition.x >= posPixels.x - halfSizePixels.x && m_MousePosition.x < posPixels.x + halfSizePixels.x &&
								m_MousePosition.y >= posPixels.y - halfSizePixels.y && m_MousePosition.y < posPixels.y + halfSizePixels.y);
		return bHoveringInArea;
	}

	glm::vec2 InputManager::GetMouseDragDistance(MouseButton mouseButton)
	{
		assert((i32)mouseButton >= 0 && (i32)mouseButton <= MOUSE_BUTTON_COUNT - 1);

		return (m_MouseButtonDrags[(i32)mouseButton].endLocation - m_MouseButtonDrags[(i32)mouseButton].startLocation);
	}

	void InputManager::ClearMouseDragDistance(MouseButton mouseButton)
	{
		assert((i32)mouseButton >= 0 && (i32)mouseButton <= MOUSE_BUTTON_COUNT - 1);

		m_MouseButtonDrags[(i32)mouseButton].startLocation = m_MouseButtonDrags[(i32)mouseButton].endLocation;
	}

	void InputManager::ClearAllInputs()
	{
		ClearMouseInput();
		ClearKeyboadInput();
		ClearGampadInput(0);
		ClearGampadInput(1);
	}

	void InputManager::ClearMouseInput()
	{
		m_PrevMousePosition = m_MousePosition = glm::vec2(-1.0f);
		m_ScrollXOffset = 0.0f;
		m_ScrollYOffset = 0.0f;

		m_MouseButtonStates = 0;
		m_MouseButtonsPressed = 0;
		m_MouseButtonsReleased = 0;

		for (MouseDrag& mouseDrag : m_MouseButtonDrags)
		{
			mouseDrag.startLocation = VEC2_ZERO;
			mouseDrag.endLocation = VEC2_ZERO;
		}
		g_Window->SetCursorMode(CursorMode::NORMAL);


		ImGuiIO& io = ImGui::GetIO();
		io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
		io.MouseWheel = m_ScrollYOffset;
	}

	void InputManager::ClearKeyboadInput()
	{
		for (auto& keyPair : m_Keys)
		{
			keyPair.second.pDown = 0;
			keyPair.second.down = 0;
		}
	}

	void InputManager::ClearGampadInput(i32 gamepadIndex)
	{
		assert(gamepadIndex == 0 || gamepadIndex == 1);

		GamepadState& gamepadState = m_GamepadStates[gamepadIndex];

		for (real& axis : gamepadState.axes)
		{
			axis = 0;
		}

		gamepadState.buttonStates = 0;
		gamepadState.buttonsPressed = 0;
		gamepadState.buttonsReleased = 0;

		gamepadState.averageRotationSpeeds.Reset();

		m_pGamepadStates[gamepadIndex] = gamepadState;
	}

	void InputManager::BindMouseButtonCallback(ICallbackMouseButton* callback, i32 priority)
	{
		for (auto callbackPair : m_MouseButtonCallbacks)
		{
			if (callbackPair.first == callback)
			{
				PrintWarn("Attempted to bind mouse button callback multiple times!\n");
				return;
			}
		}

		bool bAdded = false;
		for (auto iter = m_MouseButtonCallbacks.begin(); iter != m_MouseButtonCallbacks.end(); ++iter)
		{
			if (iter->second < priority)
			{
				m_MouseButtonCallbacks.insert(iter, Pair<ICallbackMouseButton*, i32>(callback, priority));
				bAdded = true;
				break;
			}
		}
		if (!bAdded)
		{
			m_MouseButtonCallbacks.push_back(Pair<ICallbackMouseButton*, i32>(callback, priority));
		}
	}

	void InputManager::UnbindMouseButtonCallback(ICallbackMouseButton* callback)
	{
		auto found = m_MouseButtonCallbacks.end();
		for (auto iter = m_MouseButtonCallbacks.begin(); iter != m_MouseButtonCallbacks.end(); ++iter)
		{
			if (iter->first == callback)
			{
				found = iter;
				break;
			}
		}

		if (found == m_MouseButtonCallbacks.end())
		{
			PrintWarn("Attempted to unbind mouse button callback that isn't present in callback list!\n");
			return;
		}

		m_MouseButtonCallbacks.erase(found);
	}

	void InputManager::BindMouseMovedCallback(ICallbackMouseMoved* callback, i32 priority)
	{
		for (auto callbackPair : m_MouseMovedCallbacks)
		{
			if (callbackPair.first == callback)
			{
				PrintWarn("Attempted to bind mouse moved callback multiple times!\n");
				return;
			}
		}

		bool bAdded = false;
		for (auto iter = m_MouseMovedCallbacks.begin(); iter != m_MouseMovedCallbacks.end(); ++iter)
		{
			if (iter->second < priority)
			{
				m_MouseMovedCallbacks.insert(iter, Pair<ICallbackMouseMoved*, i32>(callback, priority));
				bAdded = true;
				break;
			}
		}
		if (!bAdded)
		{
			m_MouseMovedCallbacks.push_back(Pair<ICallbackMouseMoved*, i32>(callback, priority));
		}
	}

	void InputManager::UnbindMouseMovedCallback(ICallbackMouseMoved* callback)
	{
		auto found = m_MouseMovedCallbacks.end();
		for (auto iter = m_MouseMovedCallbacks.begin(); iter != m_MouseMovedCallbacks.end(); ++iter)
		{
			if (iter->first == callback)
			{
				found = iter;
				break;
			}
		}

		if (found == m_MouseMovedCallbacks.end())
		{
			PrintWarn("Attempted to unbind mouse moved callback that isn't present in callback list!\n");
			return;
		}

		m_MouseMovedCallbacks.erase(found);
	}

	void InputManager::BindKeyEventCallback(ICallbackKeyEvent* callback, i32 priority)
	{
		for (auto callbackPair : m_KeyEventCallbacks)
		{
			if (callbackPair.first == callback)
			{
				PrintWarn("Attempted to bind key event callback multiple times!\n");
				return;
			}
		}

		bool bAdded = false;
		for (auto iter = m_KeyEventCallbacks.begin(); iter != m_KeyEventCallbacks.end(); ++iter)
		{
			if (iter->second < priority)
			{
				m_KeyEventCallbacks.insert(iter, Pair<ICallbackKeyEvent*, i32>(callback, priority));
				bAdded = true;
				break;
			}
		}
		if (!bAdded)
		{
			m_KeyEventCallbacks.push_back(Pair<ICallbackKeyEvent*, i32>(callback, priority));
		}
	}

	void InputManager::UnbindKeyEventCallback(ICallbackKeyEvent* callback)
	{
		auto found = m_KeyEventCallbacks.end();
		for (auto iter = m_KeyEventCallbacks.begin(); iter != m_KeyEventCallbacks.end(); ++iter)
		{
			if (iter->first == callback)
			{
				found = iter;
				break;
			}
		}

		if (found == m_KeyEventCallbacks.end())
		{
			PrintWarn("Attempted to unbind key event callback that isn't present in callback list!\n");
			return;
		}

		m_KeyEventCallbacks.erase(found);
	}

	void InputManager::BindActionCallback(ICallbackAction* callback, i32 priority)
	{
		for (auto callbackPair : m_ActionCallbacks)
		{
			if (callbackPair.first == callback)
			{
				PrintWarn("Attempted to bind action callback multiple times!\n");
				return;
			}
		}

		bool bAdded = false;
		for (auto iter = m_ActionCallbacks.begin(); iter != m_ActionCallbacks.end(); ++iter)
		{
			if (iter->second < priority)
			{
				m_ActionCallbacks.insert(iter, Pair<ICallbackAction*, i32>(callback, priority));
				bAdded = true;
				break;
			}
		}
		if (!bAdded)
		{
			m_ActionCallbacks.push_back(Pair<ICallbackAction*, i32>(callback, priority));
		}
	}

	void InputManager::UnbindActionCallback(ICallbackAction* callback)
	{
		auto found = m_ActionCallbacks.end();
		for (auto iter = m_ActionCallbacks.begin(); iter != m_ActionCallbacks.end(); ++iter)
		{
			if (iter->first == callback)
			{
				found = iter;
				break;
			}
		}

		if (found == m_ActionCallbacks.end())
		{
			PrintWarn("Attempted to unbind action callback that isn't present in callback list!\n");
			return;
		}

		m_ActionCallbacks.erase(found);
	}

	void InputManager::DrawImGuiKeyMapper(bool* bOpen)
	{
		if (ImGui::Begin("Key Mapper", bOpen))
		{
			if (ImGui::Button("Save"))
			{
				SaveInputBindingsToFile();
			}

			ImGui::SameLine();

			if (ImGui::Button("Load"))
			{
				LoadInputBindingsFromFile();
			}

			const i32 numCols = 4;
			ImGui::Columns(numCols);

			ImGui::SetColumnWidth(-1, 250);
			ImGui::Text("Action");
			ImGui::NextColumn();

			ImGui::SetColumnWidth(-1, 120);
			ImGui::Text("Keyboard");
			ImGui::NextColumn();

			ImGui::SetColumnWidth(-1, 150);
			ImGui::Text("Mouse");
			ImGui::NextColumn();

			ImGui::SetColumnWidth(-1, 150);
			ImGui::Text("Gamepad");
			ImGui::NextColumn();

			ImGui::Separator();
			ImGui::Separator();

			for (i32 i = 0; i < (i32)Action::_NONE; ++i)
			{
				const InputBinding& binding = m_InputBindings[i];

				ImGui::Columns(numCols);

				ImGui::Text("%s", ActionStrings[i]);
				ImGui::NextColumn();

				if (binding.keyCode != KeyCode::_NONE)
				{
					ImGui::Text("%s", KeyCodeStrings[(u32)binding.keyCode]);
				}
				ImGui::NextColumn();

				if (binding.mouseButton != MouseButton::_NONE)
				{
					ImGui::Text("%s", MouseButtonStrings[(u32)binding.mouseButton]);
				}
				if (binding.mouseAxis != MouseAxis::_NONE)
				{
					ImGui::Text("%s", MouseAxisStrings[(u32)binding.mouseAxis]);
				}
				ImGui::NextColumn();

				if (binding.gamepadButton != GamepadButton::_NONE)
				{
					ImGui::Text("%s", GamepadButtonStrings[(u32)binding.gamepadButton]);
				}
				if (binding.gamepadAxis != GamepadAxis::_NONE)
				{
					ImGui::Text("%s", GamepadAxisStrings[(i32)binding.gamepadAxis]);
				}
				ImGui::NextColumn();

				if (i != (i32)Action::_NONE - 1)
				{
					ImGui::Separator();
				}
			}
		}

		ImGui::End();
	}

	// http://www.third-helix.com/2013/04/12/doing-thumbstick-dead-zones-right.html
	void InputManager::HandleRadialDeadZone(real* x, real* y)
	{
		real deadzone = 0.25f;

		glm::vec2 stick(*x, *y);
		real stickMagnitude = glm::length(stick);
		if (stickMagnitude < deadzone)
		{
			*x = 0.0f;
			*y = 0.0f;
		}
		else
		{
			glm::vec2 stickDir = (stick / stickMagnitude);
			glm::vec2 stickScaled = stickDir * ((stickMagnitude - deadzone) / (1.0f - deadzone));

			*x = stickScaled.x;
			*y = stickScaled.y;
		}
	}

	bool InputManager::LoadInputBindingsFromFile()
	{
		JSONObject rootObject;
		if (!JSONParser::Parse(s_InputBindingFilePath, rootObject))
		{
			PrintError("Failed to load input bindings from file! Won't have any inputs mapped!!\n");
			return false;
		}

		bool bFileComplete = true;

		const u32 actionCount = (u32)Action::_NONE;
		if (rootObject.fields.size() != actionCount)
		{
			PrintWarn("Unexpected number of inputs found in input-bindings.ini! (%u expected, %u found)\n", actionCount, rootObject.fields.size());
			bFileComplete = false;
		}

		for (u32 i = 0; i < actionCount; ++i)
		{
			const JSONObject& child = rootObject.GetObject(ActionStrings[i]);
			if (child.fields.empty())
			{
				PrintWarn("Couldn't find action with name: %s in input-bindings.ini - using default values\n", ActionStrings[i]);
			}
			else
			{
				i32 keyCode = child.GetInt("key code");
				if (keyCode != -1)
				{
					m_InputBindings[i].keyCode = (KeyCode)keyCode;
				}
				i32 mouseButton = child.GetInt("mouse button");
				if (mouseButton != -1)
				{
					m_InputBindings[i].mouseButton = (MouseButton)mouseButton;
				}
				i32 mouseAxis = child.GetInt("mouse axis");
				if (mouseAxis != -1)
				{
					m_InputBindings[i].mouseAxis = (MouseAxis)mouseAxis;
				}
				i32 gamepadButton = child.GetInt("gamepad button");
				if (gamepadButton != -1)
				{
					m_InputBindings[i].gamepadButton = (GamepadButton)gamepadButton;
				}
				i32 gamepadAxis = child.GetInt("gamepad axis");
				if (gamepadAxis != -1)
				{
					m_InputBindings[i].gamepadAxis = (GamepadAxis)gamepadAxis;
				}

				m_InputBindings[i].bNegative = child.GetBool("negative");
			}
		}

		return bFileComplete;
	}

	void InputManager::SaveInputBindingsToFile()
	{
		JSONObject rootObject = {};

		for (u32 i = 0; i < (i32)Action::_NONE; ++i)
		{
			const InputBinding& binding = m_InputBindings[i];

			JSONObject bindingObj = {};

			i32 keyCode = binding.keyCode == KeyCode::_NONE ? -1 : (i32)binding.keyCode;
			bindingObj.fields.emplace_back("key code", JSONValue(keyCode));
			i32 mouseButton = binding.mouseButton == MouseButton::_NONE ? -1 : (i32)binding.mouseButton;
			bindingObj.fields.emplace_back("mouse button", JSONValue(mouseButton));
			i32 mouseAxis = binding.mouseAxis == MouseAxis::_NONE ? -1 : (i32)binding.mouseAxis;
			bindingObj.fields.emplace_back("mouse axis", JSONValue(mouseAxis));
			i32 gamepadButton = binding.gamepadButton == GamepadButton::_NONE ? -1 : (i32)binding.gamepadButton;
			bindingObj.fields.emplace_back("gamepad button", JSONValue(gamepadButton));
			i32 gamepadAxis = binding.gamepadAxis == GamepadAxis::_NONE ? -1 : (i32)binding.gamepadAxis;
			bindingObj.fields.emplace_back("gamepad axis", JSONValue(gamepadAxis));
			bindingObj.fields.emplace_back("negative", JSONValue(binding.bNegative));

			rootObject.fields.emplace_back(ActionStrings[i], JSONValue(bindingObj));
		}

		std::string inputBindingsFileName = StripLeadingDirectories(s_InputBindingFilePath);

		std::string fileContents = rootObject.Print(0);
		if (WriteFile(s_InputBindingFilePath, fileContents, false))
		{
			Print("Saved input bindings file to %s\n", inputBindingsFileName.c_str());
		}
		else
		{
			PrintWarn("Failed to save input bindings file to %s\n", inputBindingsFileName.c_str());
		}
	}

	Action InputManager::GetActionFromKeyCode(KeyCode keyCode)
	{
		for (i32 i = 0; i < (i32)Action::_NONE; ++i)
		{
			if (m_InputBindings[i].keyCode == keyCode)
			{
				return (Action)i;
			}
		}

		return Action::_NONE;
	}

	Action InputManager::GetActionFromMouseButton(MouseButton button)
	{
		for (i32 i = 0; i < (i32)Action::_NONE; ++i)
		{
			if (m_InputBindings[i].mouseButton == button)
			{
				return (Action)i;
			}
		}

		return Action::_NONE;
	}

	Action InputManager::GetActionFromGamepadButton(GamepadButton button)
	{
		for (i32 i = 0; i < (i32)Action::_NONE; ++i)
		{
			if (m_InputBindings[i].gamepadButton == button)
			{
				return (Action)i;
			}
		}

		return Action::_NONE;
	}

	char InputManager::GetShiftModifiedKeyCode(char c)
	{
		switch (c)
		{
		case '1':	return '!';
		case '2':	return '@';
		case '3':	return '#';
		case '4':	return '$';
		case '5':	return '%';
		case '6':	return '^';
		case '7':	return '&';
		case '8':	return '*';
		case '9':	return '(';
		case '0':	return ')';
		case '-':	return '_';
		case '=':	return '+';
		case '`':	return '~';
		case '\\':	return '|';
		case ',':	return '<';
		case '.':	return '>';
		case '/':	return '?';
		case ';':	return ':';
		case '\'':	return '"';
		case '[':	return '{';
		case ']':	return '}';
		default:	return 'X';
		}
	}

} // namespace flex
