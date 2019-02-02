#pragma once

#include "InputTypes.hpp"

namespace flex
{
	class ICallbackMouseButton
	{
	public:
		virtual EventReply Execute(MouseButton button, KeyAction action) = 0;
	};

	template<typename T>
	class MouseButtonCallback : public ICallbackMouseButton
	{
	public:
		using CallbackFunction = EventReply(T::*)(MouseButton, KeyAction);

		MouseButtonCallback(T* inst, CallbackFunction func) :
			inst(inst),
			func(func)
		{
		}

		virtual EventReply Execute(MouseButton button, KeyAction action) override
		{
			return (inst->*func)(button, action);
		}

	private:
		CallbackFunction func;
		T* inst;
	};

	class ICallbackMouseMoved
	{
	public:
		virtual EventReply Execute(const glm::vec2& dMousePos) = 0;
	};

	template<typename T>
	class MouseMovedCallback : public ICallbackMouseMoved
	{
	public:
		using CallbackFunction = EventReply(T::*)(const glm::vec2&);

		MouseMovedCallback(T* inst, CallbackFunction func) :
			inst(inst),
			func(func)
		{
		}

		virtual EventReply Execute(const glm::vec2& dMousePos) override
		{
			return (inst->*func)(dMousePos);
		}

	private:
		CallbackFunction func;
		T* inst;
	};

	class ICallbackKeyEvent
	{
	public:
		virtual EventReply Execute(KeyCode keyCode, KeyAction action, i32 modifiers) = 0;
	};

	template<typename T>
	class KeyEventCallback : public ICallbackKeyEvent
	{
	public:
		using CallbackFunction = EventReply(T::*)(KeyCode, KeyAction, i32);

		KeyEventCallback(T* inst, CallbackFunction func) :
			inst(inst),
			func(func)
		{
		}

		virtual EventReply Execute(KeyCode keyCode, KeyAction action, i32 modifiers) override
		{
			return (inst->*func)(keyCode, action, modifiers);
		}

	private:
		CallbackFunction func;
		T* inst;
	};

} // namespace flex
