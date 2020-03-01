#pragma once

// Mostly copied from https://github.com/SaschaWillems/Vulkan

#include <functional>

IGNORE_WARNINGS_PUSH
#include "volk/volk.h"
IGNORE_WARNINGS_POP

namespace flex
{
	namespace vk
	{
		template <typename T>
		class VDeleter
		{
		public:
			VDeleter();

			VDeleter(std::function<void(T, VkAllocationCallbacks*)> deletef);
			VDeleter(const VDeleter<VkInstance>& instance, std::function<void(VkInstance, T, VkAllocationCallbacks*)> deletef);
			VDeleter(const VDeleter<VkDevice>& device, std::function<void(VkDevice, T, VkAllocationCallbacks*)> deletef);
			~VDeleter();

			T* operator &();
			T* replace();
			operator T() const;
			void operator=(T rhs);

		private:
			T object{ VK_NULL_HANDLE };
			std::function<void(T)> deleter;

			void cleanup();
		};

		// TODO: Monitor number of VDeleters being created/destroyed
		template<typename T>
		VDeleter<T>::VDeleter() :
			VDeleter([](T, VkAllocationCallbacks*) {})
		{
		}

		template<typename T>
		VDeleter<T>::VDeleter(std::function<void(T, VkAllocationCallbacks*)> deletef)
		{
			this->deleter = [=](T obj) { deletef(obj, nullptr); };
		}

		template<typename T>
		VDeleter<T>::VDeleter(const VDeleter<VkInstance>& instance, std::function<void(VkInstance, T, VkAllocationCallbacks*)> deletef)
		{
			this->deleter = [&instance, deletef](T obj) { deletef(instance, obj, nullptr); };
		}

		template<typename T>
		VDeleter<T>::VDeleter(const VDeleter<VkDevice>& device, std::function<void(VkDevice, T, VkAllocationCallbacks*)> deletef)
		{
			this->deleter = [&device, deletef](T obj) { deletef(device, obj, nullptr); };
		}

		template<typename T>
		VDeleter<T>::~VDeleter()
		{
			cleanup();
		}

		template<typename T>
		T* VDeleter<T>::operator &()
		{
			return &object;
		}

		template<typename T>
		T* VDeleter<T>::replace()
		{
			cleanup();
			return &object;
		}

		template<typename T>
		VDeleter<T>::operator T() const
		{
			return object;
		}

		template<typename T>
		void VDeleter<T>::operator=(T rhs)
		{
			if (rhs != object)
			{
				cleanup();
				object = rhs;
			}
		}

		template<typename T>
		void VDeleter<T>::cleanup()
		{
			if (object != VK_NULL_HANDLE)
			{
				deleter(object);
			}
			object = VK_NULL_HANDLE;
		}
	} // namespace vk
} // namespace flex