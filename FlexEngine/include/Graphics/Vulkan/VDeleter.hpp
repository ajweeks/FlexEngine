#pragma once

// Mostly copied from https://github.com/SaschaWillems/Vulkan

#include <functional>

namespace flex
{
	namespace vk
	{
		template <typename T>
		class VDeleter
		{
		public:
			VDeleter() :
				VDeleter([](T, VkAllocationCallbacks*) {})
			{
			}

			VDeleter(std::function<void(T, VkAllocationCallbacks*)> deletef)
			{
				this->deleter = [=](T obj) { deletef(obj, nullptr); };
			}

			VDeleter(const VDeleter<VkDevice>& device, std::function<void(VkDevice, T, VkAllocationCallbacks*)> deletef)
			{
				this->deleter = [&device, deletef](T obj) { deletef(device, obj, nullptr); };
			}

			~VDeleter()
			{
				cleanup();
			}

			T* operator &()
			{
				return &object;
			}

			T* replace()
			{
				cleanup();
				return &object;
			}

			operator T() const
			{
				return object;
			}

			void operator=(T rhs)
			{
				if (rhs != object)
				{
					cleanup();
					object = rhs;
				}
			}

		private:
			T object{ VK_NULL_HANDLE };
			std::function<void(T)> deleter;

			void cleanup()
			{
				if (object != VK_NULL_HANDLE)
				{
					deleter(object);
				}
				object = VK_NULL_HANDLE;
			}
		};
	} // namespace vk
} // namespace flex