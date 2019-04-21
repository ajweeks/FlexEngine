#pragma once

namespace flex
{
	class GameObject;

	class ICallbackGameObject
	{
	public:
		virtual void Execute(GameObject* obj) = 0;
	};

	template<typename T>
	class OnGameObjectDestroyedCallback : public ICallbackGameObject
	{
		using CallbackFunction = void(T::*)(GameObject*);

	public:
		OnGameObjectDestroyedCallback(T* obj, CallbackFunction fun) :
			mObject(obj),
			mFunction(fun)
		{
		}

		virtual void Execute(GameObject* obj) override
		{
			(mObject->*mFunction)(obj);
		}

	private:
		T* mObject;
		CallbackFunction mFunction;
	};
} // namespace flex
