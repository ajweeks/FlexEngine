#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanHelpers.hpp"

IGNORE_WARNINGS_PUSH
#include "vulkan/vk_enum_string_helper.h"

#include "stb_image.h"
IGNORE_WARNINGS_POP

#include "FlexEngine.hpp"
#include "Graphics/VertexAttribute.hpp"
#include "Graphics/VertexBufferData.hpp"
#include "Graphics/Vulkan/VulkanCommandBufferManager.hpp"
#include "Graphics/Vulkan/VulkanDevice.hpp"
#include "Graphics/Vulkan/VulkanInitializers.hpp"
#include "Graphics/Vulkan/VulkanRenderer.hpp"
#include "Helpers.hpp"
#include "Platform/Platform.hpp"
#include "Time.hpp"

namespace flex
{
	namespace vk
	{
		void VK_CHECK_RESULT(VkResult result)
		{
			if (result != VK_SUCCESS)
			{
				PrintError("Vulkan fatal error: %s\n", string_VkResult(result));
				((VulkanRenderer*)g_Renderer)->GetCheckPointData();
				DEBUG_BREAK();
				CHECK_EQ(result, VK_SUCCESS);
			}
		}

		VkResult vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory)
		{
			FLEX_UNUSED(device);
			FLEX_UNUSED(pAllocateInfo);
			FLEX_UNUSED(pAllocator);
			FLEX_UNUSED(pMemory);

			ENSURE_NO_ENTRY();
			return VK_ERROR_UNKNOWN;
		}

		void vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator)
		{
			FLEX_UNUSED(device);
			FLEX_UNUSED(memory);
			FLEX_UNUSED(pAllocator);

			ENSURE_NO_ENTRY();
		}

		VkResult deviceAllocateMemory(const char* debugName, VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory)
		{
			FLEX_UNUSED(device);

			return ((VulkanRenderer*)g_Renderer)->GetDevice()->AllocateMemory(debugName, pAllocateInfo, pAllocator, pMemory);
		}

		void deviceFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator)
		{
			FLEX_UNUSED(device);

			((VulkanRenderer*)g_Renderer)->GetDevice()->FreeMemory(memory, pAllocator);
		}

		void GetVertexAttributeDescriptions(VertexAttributes vertexAttributes,
			std::vector<VkVertexInputAttributeDescription>& attributeDescriptions)
		{
			attributeDescriptions.clear();

			u32 offset = 0;
			u32 location = 0;

			// TODO: Roll into iteration over array

			if (vertexAttributes & (u32)VertexAttribute::POSITION)
			{
				VkVertexInputAttributeDescription attributeDescription = {};
				attributeDescription.binding = 0;
				attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
				attributeDescription.location = location;
				attributeDescription.offset = offset;
				attributeDescriptions.push_back(attributeDescription);

				offset += sizeof(glm::vec3);
				++location;
			}

			if (vertexAttributes & (u32)VertexAttribute::POSITION2)
			{
				VkVertexInputAttributeDescription attributeDescription = {};
				attributeDescription.binding = 0;
				attributeDescription.format = VK_FORMAT_R32G32_SFLOAT;
				attributeDescription.location = location;
				attributeDescription.offset = offset;
				attributeDescriptions.push_back(attributeDescription);

				offset += sizeof(glm::vec2);
				++location;
			}

			if (vertexAttributes & (u32)VertexAttribute::POSITION4)
			{
				VkVertexInputAttributeDescription attributeDescription = {};
				attributeDescription.binding = 0;
				attributeDescription.format = VK_FORMAT_R32G32B32A32_SFLOAT;
				attributeDescription.location = location;
				attributeDescription.offset = offset;
				attributeDescriptions.push_back(attributeDescription);

				offset += sizeof(glm::vec4);
				++location;
			}

			if (vertexAttributes & (u32)VertexAttribute::VELOCITY3)
			{
				VkVertexInputAttributeDescription attributeDescription = {};
				attributeDescription.binding = 0;
				attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
				attributeDescription.location = location;
				attributeDescription.offset = offset;
				attributeDescriptions.push_back(attributeDescription);

				offset += sizeof(glm::vec3);
				++location;
			}

			if (vertexAttributes & (u32)VertexAttribute::UV)
			{
				VkVertexInputAttributeDescription attributeDescription = {};
				attributeDescription.binding = 0;
				attributeDescription.format = VK_FORMAT_R32G32_SFLOAT;
				attributeDescription.location = location;
				attributeDescription.offset = offset;
				attributeDescriptions.push_back(attributeDescription);

				offset += sizeof(glm::vec2);
				++location;
			}

			if (vertexAttributes & (u32)VertexAttribute::COLOUR_R8G8B8A8_UNORM)
			{
				VkVertexInputAttributeDescription attributeDescription = {};
				attributeDescription.binding = 0;
				attributeDescription.format = VK_FORMAT_R8G8B8A8_UNORM;
				attributeDescription.location = location;
				attributeDescription.offset = offset;
				attributeDescriptions.push_back(attributeDescription);

				offset += sizeof(i32);
				++location;
			}

			if (vertexAttributes & (u32)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT)
			{
				VkVertexInputAttributeDescription attributeDescription = {};
				attributeDescription.binding = 0;
				attributeDescription.format = VK_FORMAT_R32G32B32A32_SFLOAT;
				attributeDescription.location = location;
				attributeDescription.offset = offset;
				attributeDescriptions.push_back(attributeDescription);

				offset += sizeof(glm::vec4);
				++location;
			}

			if (vertexAttributes & (u32)VertexAttribute::TANGENT)
			{
				VkVertexInputAttributeDescription attributeDescription = {};
				attributeDescription.binding = 0;
				attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
				attributeDescription.location = location;
				attributeDescription.offset = offset;
				attributeDescriptions.push_back(attributeDescription);

				offset += sizeof(glm::vec3);
				++location;
			}

			if (vertexAttributes & (u32)VertexAttribute::NORMAL)
			{
				VkVertexInputAttributeDescription attributeDescription = {};
				attributeDescription.binding = 0;
				attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
				attributeDescription.location = location;
				attributeDescription.offset = offset;
				attributeDescriptions.push_back(attributeDescription);

				offset += sizeof(glm::vec3);
				++location;
			}

			if (vertexAttributes & (u32)VertexAttribute::EXTRA_VEC4)
			{
				VkVertexInputAttributeDescription attributeDescription = {};
				attributeDescription.binding = 0;
				attributeDescription.format = VK_FORMAT_R32G32B32A32_SFLOAT;
				attributeDescription.location = location;
				attributeDescription.offset = offset;
				attributeDescriptions.push_back(attributeDescription);

				offset += sizeof(glm::vec4);
				++location;
			}

			if (vertexAttributes & (u32)VertexAttribute::EXTRA_INT)
			{
				VkVertexInputAttributeDescription attributeDescription = {};
				attributeDescription.binding = 0;
				attributeDescription.format = VK_FORMAT_R32_SINT;
				attributeDescription.location = location;
				attributeDescription.offset = offset;
				attributeDescriptions.push_back(attributeDescription);

				offset += sizeof(i32);
				++location;
			}
		}

		VulkanGPUBuffer::VulkanGPUBuffer(VulkanDevice* device, GPUBufferType type, const std::string& debugName) :
			GPUBuffer(type, debugName),
			buffer(device)
		{
		}

		VulkanGPUBuffer::~VulkanGPUBuffer()
		{
			buffer.Destroy();
		}

		void VertexIndexBufferPair::Destroy()
		{
			if (vertexBuffer != nullptr)
			{
				vertexBuffer->Destroy();
				delete vertexBuffer;
				vertexBuffer = nullptr;
			}
			if (indexBuffer != nullptr)
			{
				indexBuffer->Destroy();
				delete indexBuffer;
				indexBuffer = nullptr;
			}
		}

		void VertexIndexBufferPair::Clear()
		{
			if (vertexBuffer != nullptr)
			{
				vertexBuffer->Reset();
			}
			if (indexBuffer != nullptr)
			{
				indexBuffer->Reset();
			}
		}

		VulkanTexture::VulkanTexture(VulkanDevice* device, VkQueue queue) :
			Texture(),
			image(device->m_LogicalDevice, vkDestroyImage),
			imageMemory(device->m_LogicalDevice, deviceFreeMemory),
			imageView(device->m_LogicalDevice, vkDestroyImageView),
			m_VulkanDevice(device),
			m_Queue(queue)
		{
		}

		VulkanTexture::VulkanTexture(VulkanDevice* device, VkQueue queue, const std::string& name) :
			Texture(name),
			image(device->m_LogicalDevice, vkDestroyImage),
			imageMemory(device->m_LogicalDevice, deviceFreeMemory),
			imageView(device->m_LogicalDevice, vkDestroyImageView),
			m_VulkanDevice(device),
			m_Queue(queue)
		{
		}

		void VulkanTexture::Reload()
		{
			if (!fileName.empty())
			{
				CreateFromFile(relativeFilePath, sampler, imageFormat, mipLevels > 1);

				g_Renderer->OnTextureReloaded(this);
			}
		}

		u32 VulkanTexture::CreateFromMemory(void* buffer, u32 bufferSize, u32 inWidth, u32 inHeight, u32 inChannelCount,
			VkFormat inFormat, i32 inMipLevels, VkSampler* inSampler, i32 layerCount /* = 1 */)
		{
			CHECK(inWidth != 0u && inHeight != 0u);
			CHECK_NE(buffer, nullptr);
			CHECK_NE(bufferSize, 0u);
			CHECK((!bIsArray && layerCount == 1) || (bIsArray && layerCount >= 1));

			width = inWidth;
			height = inHeight;
			channelCount = inChannelCount;
			imageFormat = inFormat;
			sampler = inSampler;

			ImageCreateInfo imageCreateInfo = {};
			imageCreateInfo.DBG_Name = name.c_str();
			imageCreateInfo.image = image.replace();
			imageCreateInfo.imageMemory = imageMemory.replace();
			imageCreateInfo.format = inFormat;
			imageCreateInfo.width = inWidth;
			imageCreateInfo.height = inHeight;
			imageCreateInfo.arrayLayers = layerCount;
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			imageCreateInfo.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			u32 imageSize = (u32)CreateImage(m_VulkanDevice, imageCreateInfo);

			imageLayout = imageCreateInfo.initialLayout;

			VulkanBuffer stagingBuffer(m_VulkanDevice);
			stagingBuffer.Create(imageSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				name.c_str());

			void* stagingBufferData = nullptr;
			VK_CHECK_RESULT(vkMapMemory(m_VulkanDevice->m_LogicalDevice, stagingBuffer.m_Memory, 0, imageSize, 0, &stagingBufferData));
			memcpy(stagingBufferData, buffer, bufferSize);
			vkUnmapMemory(m_VulkanDevice->m_LogicalDevice, stagingBuffer.m_Memory);
			{
				VkCommandBuffer cmdBuffer = BeginSingleTimeCommands(m_VulkanDevice);
				std::string debugMarkerStr = "Uploading texture data from memory into " + name;
				VulkanRenderer::BeginDebugMarkerRegion(cmdBuffer, debugMarkerStr.c_str());

				TransitionToLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmdBuffer);
				CopyFromBuffer(stagingBuffer.m_Buffer, width, height, cmdBuffer);
				TransitionToLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmdBuffer);

				VulkanRenderer::EndDebugMarkerRegion(cmdBuffer);
				EndSingleTimeCommands(m_VulkanDevice, m_Queue, cmdBuffer);
			}

			ImageViewCreateInfo viewCreateInfo = {};
			viewCreateInfo.DBG_Name = name.c_str();
			viewCreateInfo.viewType = (bIsArray ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
			viewCreateInfo.format = inFormat;
			viewCreateInfo.image = &image;
			viewCreateInfo.imageView = &imageView;
			viewCreateInfo.mipLevels = inMipLevels;
			CreateImageView(m_VulkanDevice, viewCreateInfo);

			return imageSize;
		}

		void VulkanTexture::TransitionToLayout(VkImageLayout newLayout, VkCommandBuffer optCommandBuffer /* = VK_NULL_HANDLE */)
		{
			if (imageLayout == newLayout)
			{
				PrintWarn("Redundant image layout transition on %s, already in %u\n", name.c_str(), (u32)imageLayout);
			}
			TransitionImageLayout(m_VulkanDevice, m_Queue, image, imageFormat, imageLayout, newLayout, mipLevels, optCommandBuffer);
			imageLayout = newLayout;
		}

		void VulkanTexture::CopyFromBuffer(VkBuffer buffer, u32 inWidth, u32 inHeight, VkCommandBuffer optCommandBuffer /* = 0 */)
		{
			CopyBufferToImage(m_VulkanDevice, m_Queue, buffer, image, inWidth, inHeight, optCommandBuffer);
			width = inWidth;
			height = inHeight;
		}

		VkDeviceSize VulkanTexture::CreateEmpty(u32 inWidth, u32 inHeight, u32 inChannelCount, VkFormat inFormat, VkSampler* inSampler, u32 inMipLevels /* = 1 */, VkImageUsageFlags inUsage /* = VK_IMAGE_USAGE_SAMPLED_BIT */)
		{
			PROFILE_AUTO("VulkanTexture CreateEmpty");

			CHECK_GT(inWidth, 0u);
			CHECK_GT(inHeight, 0u);

			width = inWidth;
			height = inHeight;
			channelCount = inChannelCount;
			mipLevels = inMipLevels;
			imageFormat = inFormat;
			sampler = inSampler;

			ImageCreateInfo imageCreateInfo = {};
			imageCreateInfo.image = image.replace();
			imageCreateInfo.imageMemory = imageMemory.replace();
			imageCreateInfo.format = inFormat;
			imageCreateInfo.width = inWidth;
			imageCreateInfo.height = inHeight;
			imageCreateInfo.mipLevels = inMipLevels;
			imageCreateInfo.usage = inUsage;
			imageCreateInfo.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			imageCreateInfo.DBG_Name = name.c_str();

			VkDeviceSize imageSize = CreateImage(m_VulkanDevice, imageCreateInfo);

			ImageViewCreateInfo imageViewCreateInfo = {};
			imageViewCreateInfo.image = &image;
			imageViewCreateInfo.imageView = &imageView;
			imageViewCreateInfo.format = inFormat;
			imageViewCreateInfo.mipLevels = inMipLevels;
			CreateImageView(m_VulkanDevice, imageViewCreateInfo);

			return imageSize;
		}

		VkDeviceSize VulkanTexture::CreateCubemap(VulkanDevice* device, CubemapCreateInfo& createInfo)
		{
			if (createInfo.width == 0 ||
				createInfo.height == 0 ||
				createInfo.channels == 0 ||
				createInfo.mipLevels == 0)
			{
				PrintError("Cubemap create info missing required size data\n");
				return 0;
			}

			VkImageCreateInfo imageCreateInfo = vks::imageCreateInfo();
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = createInfo.format;
			imageCreateInfo.mipLevels = createInfo.mipLevels;
			imageCreateInfo.arrayLayers = 6;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.extent = { (u32)createInfo.width, (u32)createInfo.height, 1u };
			imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

			VK_CHECK_RESULT(vkCreateImage(device->m_LogicalDevice, &imageCreateInfo, nullptr, createInfo.image));
			VulkanRenderer::SetImageName(device, *createInfo.image, createInfo.DBG_Name);

			VkMemoryRequirements memRequirements;
			vkGetImageMemoryRequirements(device->m_LogicalDevice, *createInfo.image, &memRequirements);

			VkMemoryAllocateInfo memAllocInfo = vks::memoryAllocateInfo(memRequirements.size);
			memAllocInfo.memoryTypeIndex = device->GetMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			VK_CHECK_RESULT(device->AllocateMemory(createInfo.DBG_Name, &memAllocInfo, nullptr, createInfo.imageMemory));
			VK_CHECK_RESULT(vkBindImageMemory(device->m_LogicalDevice, *createInfo.image, *createInfo.imageMemory, 0));

			VkImageViewCreateInfo imageViewCreateInfo = vks::imageViewCreateInfo();
			imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
			imageViewCreateInfo.format = createInfo.format;
			imageViewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
			imageViewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			imageViewCreateInfo.subresourceRange.layerCount = 6;
			imageViewCreateInfo.subresourceRange.levelCount = createInfo.mipLevels;
			imageViewCreateInfo.image = *createInfo.image;
			imageViewCreateInfo.flags = 0;
			VK_CHECK_RESULT(vkCreateImageView(device->m_LogicalDevice, &imageViewCreateInfo, nullptr, createInfo.imageView));
			VulkanRenderer::SetImageViewName(device, *createInfo.imageView, createInfo.DBG_Name);

			return memRequirements.size;
		}

		VkDeviceSize VulkanTexture::CreateCubemapEmpty(u32 inWidth, u32 inHeight, u32 inChannelCount, VkFormat inFormat, VkSampler* inSampler, u32 inMipLevels, bool bEnableTrilinearFiltering)
		{
			PROFILE_AUTO("VulkanTexture CreateCubemapEmpty");

			CHECK_GT(inWidth, 0u);
			CHECK_GT(inHeight, 0u);
			CHECK_GT(inChannelCount, 0u);

			width = inWidth;
			height = inHeight;
			channelCount = inChannelCount;
			sampler = inSampler;

			CubemapCreateInfo createInfo = {};
			createInfo.image = &image;
			createInfo.imageMemory = &imageMemory;
			createInfo.imageView = &imageView;
			createInfo.format = inFormat;
			createInfo.width = width;
			createInfo.height = height;
			createInfo.channels = channelCount;
			createInfo.totalSize = width * height * channelCount * 6;
			createInfo.mipLevels = inMipLevels;
			createInfo.bEnableTrilinearFiltering = bEnableTrilinearFiltering;
			createInfo.DBG_Name = "Empty cubemap";

			VkDeviceSize imageSize = CreateCubemap(m_VulkanDevice, createInfo);

			// Retrieve out variables
			imageLayout = createInfo.imageLayoutOut;
			imageFormat = inFormat;

			return imageSize;
		}

		VkDeviceSize VulkanTexture::CreateCubemapFromTextures(VkFormat inFormat, const std::array<std::string, 6>& filePaths, VkSampler* inSampler, bool bEnableTrilinearFiltering)
		{
			PROFILE_AUTO("VulkanTexture CreateCubemapFromTextures");

			sampler = inSampler;

			struct Image
			{
				unsigned char* pixels;
				i32 width;
				i32 height;
				i32 textureChannels;
				i32 size;
			};

			std::vector<Image> images;
			u32 totalSize = 0;

			fileName = StripLeadingDirectories(filePaths[0]);
			if (fileName.empty())
			{
				PrintError("CreateCubemapFromTextures was given an empty filepath!\n");
				return 0;
			}

			if (name.empty())
			{
				name = fileName;
			}

			if (g_bEnableLogging_Loading)
			{
				Print("Loading cubemap textures %s, %s, %s, %s, %s, %s\n", filePaths[0].c_str(), filePaths[1].c_str(), filePaths[2].c_str(), filePaths[3].c_str(), filePaths[4].c_str(), filePaths[5].c_str());
			}

			images.reserve(filePaths.size());
			for (const std::string& path : filePaths)
			{
				int w, h, c;
				unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &c, STBI_rgb_alpha);
				if (!pixels)
				{
					const char* failureReasonStr = stbi_failure_reason();
					PrintError("CreateCubemapFromTextures failed to load image %s, failure reason: %s\n", path.c_str(), failureReasonStr);
					// NOTE: Potential leak of pixels from other images, too annoying to fix though, won't occur normally
					return 0;
				}
				width = (u32)w;
				height = (u32)h;

				c = 4;
				channelCount = (u32)c;

				i32 size = width * height * channelCount * sizeof(unsigned char);
				images.push_back({ pixels, (i32)width, (i32)height, (i32)channelCount, size });
				totalSize += size;
			}

			if (totalSize == 0)
			{
				PrintError("CreateCubemapFromTextures failed to load cubemap textures (%s)\n", fileName.c_str());
				return 0;
			}

			unsigned char* pixels = (unsigned char*)malloc(totalSize);
			if (pixels == nullptr)
			{
				PrintError("CreateCubemapFromTextures Failed to allocate %u bytes\n", totalSize);
				// NOTE: Leak of all pixel data
				return 0;
			}

			unsigned char* pixelData = pixels;
			for (Image& cubeImage : images)
			{
				memcpy(pixelData, cubeImage.pixels, cubeImage.size);

				pixelData += (cubeImage.size / sizeof(unsigned char));
				stbi_image_free(cubeImage.pixels);
			}

			// Create a host-visible staging buffer that contains the raw image data
			VulkanBuffer stagingBuffer(m_VulkanDevice);
			stagingBuffer.Create(totalSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				name.c_str());

			stagingBuffer.Map();
			memcpy(stagingBuffer.m_Mapped, pixels, totalSize);
			stagingBuffer.Unmap();
			free(pixels);

			CubemapCreateInfo createInfo = {};
			createInfo.image = &image;
			createInfo.imageMemory = &imageMemory;
			createInfo.imageView = &imageView;
			createInfo.totalSize = totalSize;
			createInfo.format = inFormat;
			createInfo.filePaths = filePaths;
			createInfo.bEnableTrilinearFiltering = bEnableTrilinearFiltering;
			createInfo.DBG_Name = name.c_str();

			VkDeviceSize imageSize = CreateCubemap(m_VulkanDevice, createInfo);

			if (imageSize == 0)
			{
				return 0;
			}

			// Image barrier for optimal image (target)
			// Set initial layout for all array layers (faces) of the optimal (target) tiled texture
			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = createInfo.mipLevels;
			subresourceRange.layerCount = 6;

			VkCommandBuffer copyCmd = VulkanCommandBufferManager::CreateCommandBuffer(m_VulkanDevice, VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			// Setup buffer copy regions for each face including all of it's mip levels
			std::vector<VkBufferImageCopy> bufferCopyRegions;
			u32 offset = 0;

			for (u32 face = 0; face < 6; ++face)
			{
				for (u32 mipLevel = 0; mipLevel < mipLevels; ++mipLevel)
				{
					VkBufferImageCopy bufferCopyRegion = {};
					bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					bufferCopyRegion.imageSubresource.mipLevel = mipLevel;
					bufferCopyRegion.imageSubresource.baseArrayLayer = face;
					bufferCopyRegion.imageSubresource.layerCount = 1;
					bufferCopyRegion.imageExtent.width = static_cast<u32>(width * std::pow(0.5f, mipLevel));
					bufferCopyRegion.imageExtent.height = static_cast<u32>(height * std::pow(0.5f, mipLevel));
					bufferCopyRegion.imageExtent.depth = 1;
					bufferCopyRegion.bufferOffset = offset;

					bufferCopyRegions.push_back(bufferCopyRegion);

					offset += (totalSize / 6);
				}
			}

			SetImageLayout(
				copyCmd,
				*createInfo.image,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);
			// TODO: Set smaller range with srcStageMask & dstStageMask?

			// Copy the cube map faces from the staging buffer to the optimal tiled image
			vkCmdCopyBufferToImage(
				copyCmd,
				stagingBuffer.m_Buffer,
				*createInfo.image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				static_cast<u32>(bufferCopyRegions.size()),
				bufferCopyRegions.data()
			);

			// Change texture image layout to shader read after all faces have been copied
			SetImageLayout(
				copyCmd,
				*createInfo.image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				imageLayout,
				subresourceRange);

			VulkanCommandBufferManager::FlushCommandBuffer(m_VulkanDevice, copyCmd, m_Queue, true);

			return imageSize;
		}

		void VulkanTexture::GenerateMipmaps()
		{
			PROFILE_AUTO("VulkanTexture GenerateMipmaps");

			VkCommandBuffer cmdBuffer = BeginSingleTimeCommands(m_VulkanDevice);

			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.layerCount = 1;
			subresourceRange.levelCount = 1;

			// Transition first mip to transfer src
			{
				VkImageMemoryBarrier barrier = vks::imageMemoryBarrier();
				barrier.image = image;
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				barrier.oldLayout = imageLayout;
				barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				barrier.subresourceRange = subresourceRange;

				vkCmdPipelineBarrier(cmdBuffer,
					VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_PIPELINE_STAGE_TRANSFER_BIT,
					0,
					0, nullptr,
					0, nullptr,
					1, &barrier);
			}

			VkImageMemoryBarrier barrier = vks::imageMemoryBarrier();
			barrier.image = image;
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.layerCount = 1;
			barrier.subresourceRange.levelCount = 1;

			i32 mipWidth = width;
			i32 mipHeight = height;

			// Copy mips down from i-1 to i iteratively, halving each time
			for (u32 i = 1; i < mipLevels; ++i)
			{
				VkImageSubresourceLayers srcSubresource = {};
				srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				srcSubresource.mipLevel = i - 1;
				srcSubresource.baseArrayLayer = 0;
				srcSubresource.layerCount = 1;

				VkImageSubresourceLayers dstSubresource = {};
				dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				dstSubresource.mipLevel = i;
				dstSubresource.baseArrayLayer = 0;
				dstSubresource.layerCount = 1;

				i32 nextMipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
				i32 nextMipHeight = mipHeight > 1 ? mipHeight / 2 : 1;

				VkImageBlit blitRegion = {};
				blitRegion.srcOffsets[0] = { 0, 0, 0 };
				blitRegion.srcOffsets[1] = { mipWidth, mipHeight, 1 };
				blitRegion.dstOffsets[0] = { 0, 0, 0 };
				blitRegion.dstOffsets[1] = { nextMipWidth, nextMipHeight, 1 };
				blitRegion.srcSubresource = srcSubresource;
				blitRegion.dstSubresource = dstSubresource;

				barrier.subresourceRange.baseMipLevel = i;// -1;

				// Transition blit region to transfer dest
				{
					barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					barrier.srcAccessMask = 0;
					barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

					vkCmdPipelineBarrier(cmdBuffer,
						VK_PIPELINE_STAGE_TRANSFER_BIT,        // Wait on any previous _transfer work_ before
						VK_PIPELINE_STAGE_TRANSFER_BIT, // doing any _fragment work_
						0,
						0, nullptr,
						0, nullptr,
						1, &barrier);
				}

				// Dispatch downsample
				vkCmdBlitImage(cmdBuffer,
					image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1, &blitRegion, VK_FILTER_LINEAR);

				// Transition current mip level to src for next level
				{
					barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
					barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

					vkCmdPipelineBarrier(cmdBuffer,
						VK_PIPELINE_STAGE_TRANSFER_BIT,
						VK_PIPELINE_STAGE_TRANSFER_BIT,
						0,
						0, nullptr,
						0, nullptr,
						1, &barrier);
				}

				mipWidth = nextMipWidth;
				mipHeight = nextMipHeight;
			}

			// Transition all layers from transfer src to shader readable
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = mipLevels;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(cmdBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &barrier);

			imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			EndSingleTimeCommands(m_VulkanDevice, m_Queue, cmdBuffer);
		}

		VkDeviceSize VulkanTexture::CreateImage(VulkanDevice* device, ImageCreateInfo& createInfo)
		{
			CHECK_NE(createInfo.width, 0u);
			CHECK_NE(createInfo.height, 0u);
			CHECK_NE((u32)createInfo.format, (u32)VK_FORMAT_UNDEFINED);

			if (createInfo.width > MAX_TEXTURE_DIM ||
				createInfo.height > MAX_TEXTURE_DIM ||
				createInfo.width == 0 ||
				createInfo.height == 0)
			{
				PrintError("Invalid dimensions passed into CreateImage: %ux%u\n", createInfo.width, createInfo.height);
				return 0;
			}

			VkImageCreateInfo imageInfo = vks::imageCreateInfo();
			imageInfo.imageType = createInfo.imageType;
			imageInfo.extent.width = createInfo.width;
			imageInfo.extent.height = createInfo.height;
			imageInfo.extent.depth = 1;
			imageInfo.mipLevels = createInfo.mipLevels;
			imageInfo.arrayLayers = createInfo.arrayLayers;
			imageInfo.format = createInfo.format;
			imageInfo.tiling = createInfo.tiling;
			imageInfo.initialLayout = createInfo.initialLayout;
			imageInfo.usage = createInfo.usage;
			imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageInfo.flags = createInfo.flags;

			VkImageFormatProperties formatProperties = {};
			formatProperties.maxArrayLayers = createInfo.arrayLayers;

			VkResult result = vkGetPhysicalDeviceImageFormatProperties(device->m_PhysicalDevice,
				imageInfo.format, imageInfo.imageType, imageInfo.tiling,
				imageInfo.usage, imageInfo.flags, &formatProperties);
			if (result != VK_SUCCESS)
			{
				// TODO: Handle error gracefully
				PrintError("VulkanTexture::CreateImage: Invalid image format!\n");
				return 0;
			}

			VK_CHECK_RESULT(vkCreateImage(device->m_LogicalDevice, &imageInfo, nullptr, createInfo.image));
			VulkanRenderer::SetImageName(device, *createInfo.image, createInfo.DBG_Name);

			VkMemoryRequirements memRequirements;
			vkGetImageMemoryRequirements(device->m_LogicalDevice, *createInfo.image, &memRequirements);

			VkMemoryAllocateInfo allocInfo = vks::memoryAllocateInfo(memRequirements.size);
			allocInfo.memoryTypeIndex = FindMemoryType(device, memRequirements.memoryTypeBits, createInfo.properties);

			VK_CHECK_RESULT(device->AllocateMemory(createInfo.DBG_Name, &allocInfo, nullptr, createInfo.imageMemory));

			VK_CHECK_RESULT(vkBindImageMemory(device->m_LogicalDevice, *createInfo.image, *createInfo.imageMemory, 0));

			return memRequirements.size;
		}

		VkDeviceSize VulkanTexture::CreateFromFile(
			const std::string& inRelativeFilePath,
			VkSampler* inSampler,
			VkFormat inFormat /* = VK_FORMAT_UNDEFINED */,
			bool bGenerateFullMipChain /* = false */)
		{
			PROFILE_AUTO("VulkanTexture CreateFromFile");

			PROFILE_BEGIN("Load");

			sampler = inSampler;

			relativeFilePath = inRelativeFilePath;
			fileName = StripLeadingDirectories(inRelativeFilePath);
			if (name.empty())
			{
				name = fileName;
			}

			if (g_bEnableLogging_Loading)
			{
				Print("Loading texture %s\n", fileName.c_str());
			}

			width = height = channelCount = 0;

			// TODO: Make HDRImage subclass of Image class to unify code paths here
			HDRImage hdrImage;
			unsigned char* pixels = nullptr;

			// TODO: Unify hdr path with non-hdr & cubemap paths
			if (bHDR)
			{
				if (hdrImage.Load(relativeFilePath, 4, false))
				{
					width = hdrImage.width;
					height = hdrImage.height;
					channelCount = hdrImage.channelCount;
				}
			}
			else
			{
				int w, h, c;
				pixels = stbi_load(relativeFilePath.c_str(), &w, &h, &c, STBI_rgb_alpha);

				if (pixels != nullptr)
				{
					width = (u32)w;
					height = (u32)h;
					channelCount = (u32)c;
				}
			}

			PROFILE_END("Load");

			if (width == 0 || height == 0 || channelCount == 0 ||
				((bHDR && hdrImage.pixels == nullptr) || (!bHDR && pixels == nullptr)))
			{
				const char* failureReasonStr = stbi_failure_reason();
				PrintError("Failed to load texture data from %s error: %s\n", relativeFilePath.c_str(), failureReasonStr);
				return 0;
			}

			channelCount = 4; // ??

			if (inFormat == VK_FORMAT_UNDEFINED)
			{
				inFormat = CalculateFormat();
			}
			imageFormat = inFormat;

			if (bGenerateFullMipChain)
			{
				bool bFormatSupportsBlit = true;

				VkFormatProperties formatProps;
				vkGetPhysicalDeviceFormatProperties(m_VulkanDevice->m_PhysicalDevice, imageFormat, &formatProps);
				if (!(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
				{
					bFormatSupportsBlit = false;
				}

				if (!bFormatSupportsBlit)
				{
					// TODO: Create mip chain manually when support doesn't exist
					bGenerateFullMipChain = false;
				}
			}

			if (bGenerateFullMipChain)
			{
				// Mip levels going down to 2x2
				mipLevels = ((u32)(glm::log2((real)glm::max(width, height))));
			}

			PROFILE_BEGIN("Create image");
			ImageCreateInfo imageCreateInfo = {};
			imageCreateInfo.image = image.replace();
			imageCreateInfo.imageMemory = imageMemory.replace();
			imageCreateInfo.format = imageFormat;
			imageCreateInfo.width = (u32)width;
			imageCreateInfo.height = (u32)height;
			imageCreateInfo.mipLevels = mipLevels;
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
			imageCreateInfo.usage =
				VK_IMAGE_USAGE_TRANSFER_DST_BIT |
				VK_IMAGE_USAGE_SAMPLED_BIT |
				(bGenerateFullMipChain ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
			imageCreateInfo.DBG_Name = name.c_str();

			u32 imageSize = (u32)CreateImage(m_VulkanDevice, imageCreateInfo);
			PROFILE_END("Create image");

			if (imageSize == 0)
			{
				width = 0;
				height = 0;
				channelCount = 0;
				return 0;
			}

			imageLayout = imageCreateInfo.initialLayout;

			PROFILE_BEGIN("Upload data");
			VulkanBuffer stagingBuffer(m_VulkanDevice);
			stagingBuffer.Create(imageSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				name.c_str());

			u32 pixelBufSize = bHDR ?
				(u32)(width * height * channelCount * sizeof(real)) :
				(u32)(width * height * channelCount * sizeof(unsigned char));

			void* data = nullptr;
			VK_CHECK_RESULT(vkMapMemory(m_VulkanDevice->m_LogicalDevice, stagingBuffer.m_Memory, 0, imageSize, 0, &data));
			if (bHDR)
			{
				memcpy(data, hdrImage.pixels, (size_t)pixelBufSize);
			}
			else
			{
				memcpy(data, pixels, (size_t)pixelBufSize);
			}

			if (bHDR)
			{
				hdrImage.Free();
			}
			else
			{
				stbi_image_free(pixels);
			}

			// Upload data via staging buffer
			{
				VkCommandBuffer cmdBuffer = BeginSingleTimeCommands(m_VulkanDevice);
				std::string debugMarkerStr = "Uploading texture data from " + fileName;
				VulkanRenderer::BeginDebugMarkerRegion(cmdBuffer, debugMarkerStr.c_str());

				TransitionToLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmdBuffer);
				CopyFromBuffer(stagingBuffer.m_Buffer, width, height, cmdBuffer);
				if (!bGenerateFullMipChain)
				{
					// If generating mipmaps we'll be blitting to and from this image
					TransitionToLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmdBuffer);
				}

				VulkanRenderer::EndDebugMarkerRegion(cmdBuffer);
				EndSingleTimeCommands(m_VulkanDevice, m_Queue, cmdBuffer);
			}
			PROFILE_END("Upload data");

			PROFILE_BEGIN("Create image view");
			ImageViewCreateInfo viewCreateInfo = {};
			viewCreateInfo.format = imageFormat;
			viewCreateInfo.image = &image;
			viewCreateInfo.imageView = &imageView;
			viewCreateInfo.mipLevels = mipLevels;
			CreateImageView(m_VulkanDevice, viewCreateInfo);
			PROFILE_END("Create image view");

			if (bGenerateFullMipChain)
			{
				GenerateMipmaps();
			}

			return imageSize;
		}

		void VulkanTexture::CreateImageView(VulkanDevice* device, ImageViewCreateInfo& createInfo)
		{
			VkImageViewCreateInfo viewInfo = vks::imageViewCreateInfo();
			viewInfo.image = *createInfo.image;
			viewInfo.viewType = createInfo.viewType;
			viewInfo.format = createInfo.format;
			viewInfo.subresourceRange.aspectMask = createInfo.aspectFlags;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = createInfo.mipLevels;
			viewInfo.subresourceRange.baseArrayLayer = 0;
			viewInfo.subresourceRange.layerCount = createInfo.layerCount;
			viewInfo.flags = 0;

			VK_CHECK_RESULT(vkCreateImageView(device->m_LogicalDevice, &viewInfo, nullptr, createInfo.imageView));
			VulkanRenderer::SetImageViewName(device, *createInfo.imageView, createInfo.DBG_Name);
		}

		void VulkanTexture::CreateSampler(VulkanDevice* device, SamplerCreateInfo& createInfo)
		{
			VkSamplerCreateInfo samplerInfo = vks::samplerCreateInfo();
			samplerInfo.magFilter = createInfo.magFilter;
			samplerInfo.minFilter = createInfo.minFilter;
			samplerInfo.mipmapMode = createInfo.mipmapMode;
			samplerInfo.addressModeU = createInfo.samplerAddressMode;
			samplerInfo.addressModeV = createInfo.samplerAddressMode;
			samplerInfo.addressModeW = createInfo.samplerAddressMode;
			samplerInfo.mipLodBias = 0.0f;
			samplerInfo.anisotropyEnable = VK_FALSE;
			samplerInfo.maxAnisotropy = createInfo.maxAnisotropy;
			samplerInfo.compareEnable = VK_FALSE;
			samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
			samplerInfo.minLod = createInfo.minLod;
			samplerInfo.maxLod = createInfo.maxLod;
			samplerInfo.borderColor = createInfo.borderColor;
			samplerInfo.unnormalizedCoordinates = VK_FALSE;

			VK_CHECK_RESULT(vkCreateSampler(device->m_LogicalDevice, &samplerInfo, nullptr, createInfo.sampler));
			VulkanRenderer::SetSamplerName(device, *createInfo.sampler, createInfo.DBG_Name);
		}

		bool VulkanTexture::SaveToFile(VulkanDevice* device, const std::string& absoluteFilePath, ImageFormat saveFormat)
		{
			CHECK(channelCount == 3 || channelCount == 4);

			bool bSupportsBlit = true;

			// Check blit support for source and destination
			VkFormatProperties formatProps;

			// Check if the device supports blitting from optimal images (the swapchain images are in optimal format)
			vkGetPhysicalDeviceFormatProperties(m_VulkanDevice->m_PhysicalDevice, imageFormat, &formatProps);
			if (!(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT))
			{
				bSupportsBlit = false;
			}

			// Check if the device supports blitting to linear images
			vkGetPhysicalDeviceFormatProperties(m_VulkanDevice->m_PhysicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProps);
			if (!(formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
			{
				bSupportsBlit = false;
			}

			bool bResult = false;

			i32 pixelCount = width * height;

			i32 u8BufStride = channelCount * sizeof(u8);
			i32 u8BufSize = u8BufStride * pixelCount;
			u8* u8Data = (u8*)malloc((u32)u8BufSize);

			if (u8Data)
			{
				// Create the linear tiled destination image to copy to and to read the memory from
				VkImage dstImage;
				VkImageCreateInfo createInfo = vks::imageCreateInfo();
				createInfo.imageType = VK_IMAGE_TYPE_2D;
				// Note that vkCmdBlitImage (if supported) will also do format conversions if the swapchain colour format would differ
				createInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
				createInfo.extent.width = width;
				createInfo.extent.height = height;
				createInfo.extent.depth = 1;
				createInfo.arrayLayers = 1;
				createInfo.mipLevels = 1;
				createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
				createInfo.tiling = VK_IMAGE_TILING_LINEAR;
				createInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
				VK_CHECK_RESULT(vkCreateImage(m_VulkanDevice->m_LogicalDevice, &createInfo, nullptr, &dstImage));

				VkMemoryRequirements memRequirements;
				vkGetImageMemoryRequirements(m_VulkanDevice->m_LogicalDevice, dstImage, &memRequirements);
				VkMemoryAllocateInfo memAllocInfo = vks::memoryAllocateInfo(memRequirements.size);
				memAllocInfo.memoryTypeIndex = m_VulkanDevice->GetMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				VkDeviceMemory dstImageMemory;
				VK_CHECK_RESULT(device->AllocateMemory("Saving texture to file", &memAllocInfo, nullptr, &dstImageMemory));
				VK_CHECK_RESULT(vkBindImageMemory(m_VulkanDevice->m_LogicalDevice, dstImage, dstImageMemory, 0));

				VkCommandBuffer copyCmd = VulkanCommandBufferManager::CreateCommandBuffer(m_VulkanDevice, VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

				// Transition destination image to transfer destination layout
				InsertImageMemoryBarrier(
					copyCmd,
					dstImage,
					0,
					VK_ACCESS_TRANSFER_WRITE_BIT,
					VK_IMAGE_LAYOUT_UNDEFINED,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_PIPELINE_STAGE_TRANSFER_BIT,
					VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

				VkImageLayout previousLayout = imageLayout;
				TransitionToLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

				if (bSupportsBlit)
				{
					// Define the region to blit (we will blit the whole swapchain image)
					VkOffset3D blitSize;
					blitSize.x = width;
					blitSize.y = height;
					blitSize.z = 1;
					VkImageBlit imageBlitRegion = {};
					imageBlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					imageBlitRegion.srcSubresource.layerCount = 1;
					imageBlitRegion.srcOffsets[1] = blitSize;
					imageBlitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					imageBlitRegion.dstSubresource.layerCount = 1;
					imageBlitRegion.dstOffsets[1] = blitSize;

					// Issue the blit command
					vkCmdBlitImage(
						copyCmd,
						image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						1,
						&imageBlitRegion,
						VK_FILTER_NEAREST);
				}
				else
				{
					// Otherwise use image copy (may require manual component swizzling later on)
					VkImageCopy imageCopyRegion = {};
					imageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					imageCopyRegion.srcSubresource.layerCount = 1;
					imageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					imageCopyRegion.dstSubresource.layerCount = 1;
					imageCopyRegion.extent.width = width;
					imageCopyRegion.extent.height = height;
					imageCopyRegion.extent.depth = 1;

					// Issue the copy command
					vkCmdCopyImage(
						copyCmd,
						image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						1,
						&imageCopyRegion);
				}

				// Transition destination image to general layout, which is the required layout for mapping the image memory later on
				InsertImageMemoryBarrier(
					copyCmd,
					dstImage,
					VK_ACCESS_TRANSFER_WRITE_BIT,
					VK_ACCESS_MEMORY_READ_BIT,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_IMAGE_LAYOUT_GENERAL,
					VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_PIPELINE_STAGE_TRANSFER_BIT,
					VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

				VulkanCommandBufferManager::FlushCommandBuffer(m_VulkanDevice, copyCmd, m_Queue, true);

				// Get layout of the image (including row pitch)
				VkImageSubresource subResource{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
				VkSubresourceLayout subResourceLayout;
				vkGetImageSubresourceLayout(m_VulkanDevice->m_LogicalDevice, dstImage, &subResource, &subResourceLayout);

				// Map image memory so we can start copying from it
				const u8* data;
				vkMapMemory(m_VulkanDevice->m_LogicalDevice, dstImageMemory, 0, VK_WHOLE_SIZE, 0, (void**)&data);

				bool bColourSwizzle = false;
				// Check if source is BGR
				if (!bSupportsBlit)
				{
					std::vector<VkFormat> formatsBGR = { VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SNORM, VK_FORMAT_B8G8R8A8_UINT, VK_FORMAT_B8G8R8A8_SINT };
					bColourSwizzle = (std::find(formatsBGR.begin(), formatsBGR.end(), imageFormat) != formatsBGR.end());
				}

				CopyPixels(data, u8Data, (u32)subResourceLayout.offset, width, height, channelCount, (u32)subResourceLayout.rowPitch, bColourSwizzle);

				bResult = SaveImage(absoluteFilePath, saveFormat, width, height, channelCount, u8Data, bFlipVertically);

				vkUnmapMemory(m_VulkanDevice->m_LogicalDevice, dstImageMemory);
				device->FreeMemory(dstImageMemory, nullptr);
				vkDestroyImage(m_VulkanDevice->m_LogicalDevice, dstImage, nullptr);

				TransitionToLayout(previousLayout);
			}
			else
			{
				PrintError("Failed to allocate %d bytes to save out to texture at %s\n", u8BufSize, absoluteFilePath.c_str());
			}

			free(u8Data);

			return bResult;
		}

		VkFormat VulkanTexture::CalculateFormat()
		{
			if (channelCount == 4)
			{
				if (bHDR)
				{
					imageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
				}
				else
				{
					imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
				}
			}
			else if (channelCount == 3)
			{
				if (bHDR)
				{
					imageFormat = VK_FORMAT_R32G32B32_SFLOAT;
				}
				else
				{
					imageFormat = VK_FORMAT_R8G8B8_UINT;
				}
			}
			else if (channelCount == 2)
			{
				if (bHDR)
				{
					imageFormat = VK_FORMAT_R32G32_SFLOAT;
				}
				else
				{
					imageFormat = VK_FORMAT_R8G8_UINT;
				}
			}
			else if (channelCount == 1)
			{
				if (bHDR)
				{
					imageFormat = VK_FORMAT_R32_SFLOAT;
				}
				else
				{
					imageFormat = VK_FORMAT_R8_UINT;
				}
			}

			return imageFormat;
		}

		VkFormat FindSupportedFormat(VulkanDevice* device, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
		{
			for (VkFormat format : candidates)
			{
				VkFormatProperties properties;
				vkGetPhysicalDeviceFormatProperties(device->m_PhysicalDevice, format, &properties);

				if (tiling == VK_IMAGE_TILING_LINEAR && (properties.linearTilingFeatures & features) == features)
				{
					return format;
				}
				else if (tiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & features) == features)
				{
					return format;
				}
			}

			PrintError("Failed to find supported formats!");
			ENSURE_NO_ENTRY();
			return VK_FORMAT_MAX_ENUM;
		}

		bool HasStencilComponent(VkFormat format)
		{
			return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
		}

		u32 FindMemoryType(VulkanDevice* device, u32 typeFilter, VkMemoryPropertyFlags properties)
		{
			VkPhysicalDeviceMemoryProperties memProperties;
			vkGetPhysicalDeviceMemoryProperties(device->m_PhysicalDevice, &memProperties);

			for (u32 i = 0; i < memProperties.memoryTypeCount; ++i)
			{
				if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
				{
					return i;
				}
			}

			PrintError("Failed to find any suitable memory type!\n");
			ENSURE_NO_ENTRY();
			return u32_max;
		}

		// TODO: FIXME: This function needs to take the src & dst access masks as args, clearly we can't keep trying to handle all cases...
		void TransitionImageLayout(VulkanDevice* device, VkQueue queue, VkImage image, VkFormat format, VkImageLayout oldLayout,
			VkImageLayout newLayout, u32 mipLevels, VkCommandBuffer optCmdBuf /* = VK_NULL_HANDLE */, bool bIsDepthTexture /* = false */)
		{
			if (oldLayout == newLayout)
			{
				return;
			}

			VkCommandBuffer commandBuffer = optCmdBuf;
			if (commandBuffer == VK_NULL_HANDLE)
			{
				commandBuffer = BeginSingleTimeCommands(device);
			}

			VkImageMemoryBarrier barrier = vks::imageMemoryBarrier();
			barrier.oldLayout = oldLayout;
			barrier.newLayout = newLayout;
			barrier.image = image;

			if (bIsDepthTexture ||
				oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
				newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
			{
				barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;

				if (HasStencilComponent(format))
				{
					barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
				}
			}
			else
			{
				CHECK(oldLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
					newLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

				barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				barrier.subresourceRange.levelCount = mipLevels;
			}

			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;

			if (oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED &&
				newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
				newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
			{
				barrier.srcAccessMask = 0;
				barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED &&
				newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED &&
				newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
				newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
				newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
				newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
				newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
				newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
				newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
				newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
				barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
				newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; // hmm...
				barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
				newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
			{
				barrier.srcAccessMask = 0;
				barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
				newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
			{
				barrier.srcAccessMask = 0;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
				newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
				newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
				newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
				newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED &&
				newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
				newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
				barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
				newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
				newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			{
				barrier.srcAccessMask = 0;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			}
			else
			{
				PrintError("Unsupported layout transition!\n");
				ENSURE_NO_ENTRY();
				return;
			}

			// TODO: Set src & dst stage masks intelligently
			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &barrier);

			if (optCmdBuf == VK_NULL_HANDLE)
			{
				EndSingleTimeCommands(device, queue, commandBuffer);
			}
		}

		void CopyImage(VulkanDevice* device, VkQueue queue, VkImage srcImage, VkImage dstImage, u32 width, u32 height,
			VkCommandBuffer optCmdBuf /* = VK_NULL_HANDLE */, VkImageAspectFlags aspectMask /* = VK_IMAGE_ASPECT_COLOR_BIT */)
		{
			VkCommandBuffer commandBuffer = optCmdBuf;
			if (commandBuffer == VK_NULL_HANDLE)
			{
				commandBuffer = BeginSingleTimeCommands(device);
			}

			VkImageSubresourceLayers subresource = {};
			subresource.aspectMask = aspectMask;
			subresource.baseArrayLayer = 0;
			subresource.mipLevel = 0;
			subresource.layerCount = 1;

			VkImageCopy region = {};
			region.srcSubresource = subresource;
			region.dstSubresource = subresource;
			region.srcOffset = { 0, 0, 0 };
			region.dstOffset = { 0, 0, 0 };
			region.extent.width = width;
			region.extent.height = height;
			region.extent.depth = 1;

			vkCmdCopyImage(commandBuffer, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

			if (optCmdBuf == VK_NULL_HANDLE)
			{
				EndSingleTimeCommands(device, queue, commandBuffer);
			}
		}

		void CopyBufferToImage(VulkanDevice* device, VkQueue queue, VkBuffer buffer, VkImage image,
			u32 width, u32 height, VkCommandBuffer optCommandBuffer /* = VK_NULL_HANDLE */)
		{
			VkCommandBuffer commandBuffer = optCommandBuffer;
			if (commandBuffer == VK_NULL_HANDLE)
			{
				commandBuffer = BeginSingleTimeCommands(device);
			}

			VkBufferImageCopy region = {};
			region.bufferOffset = 0;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.mipLevel = 0;
			region.imageSubresource.baseArrayLayer = 0;
			region.imageSubresource.layerCount = 1;
			region.imageOffset = { 0, 0, 0 };
			region.imageExtent = {
				width,
				height,
				1
			};

			vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

			if (optCommandBuffer == VK_NULL_HANDLE)
			{
				EndSingleTimeCommands(device, queue, commandBuffer);
			}
		}

		void CopyBuffer(VulkanDevice* device, VkQueue queue, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset)
		{
			VkCommandBuffer commandBuffer = BeginSingleTimeCommands(device);

			VkBufferCopy copyRegion = {};
			copyRegion.size = size;
			copyRegion.dstOffset = dstOffset;
			copyRegion.srcOffset = srcOffset;
			vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

			EndSingleTimeCommands(device, queue, commandBuffer);
		}

		VulkanQueueFamilyIndices FindQueueFamilies(VkSurfaceKHR surface, VkPhysicalDevice device)
		{
			PROFILE_AUTO("FindQueueFamilies");

			VulkanQueueFamilyIndices indices;

			u32 queueFamilyCount;
			vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
			CHECK_GT(queueFamilyCount, 0u);
			std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilyProperties.data());

			i32 i = 0;
			for (const auto& queueFamily : queueFamilyProperties)
			{
				if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
				{
					indices.graphicsFamily = i;
				}

				VkBool32 presentSupport = false;
				vkGetPhysicalDeviceSurfaceSupportKHR(device, (u32)i, surface, &presentSupport);

				if (queueFamily.queueCount > 0 && presentSupport)
				{
					indices.presentFamily = i;
				}

				if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
				{
					indices.computeFamily = i;
					//// If compute family index differs, we need an additional queue create info for the compute queue
					//VkDeviceQueueCreateInfo queueInfo{};
					//queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
					//queueInfo.queueFamilyIndex = queueFamilyIndices.compute;
					//queueInfo.queueCount = 1;
					//queueInfo.pQueuePriorities = &defaultQueuePriority;
					//queueCreateInfos.push_back(queueInfo);
				}

				if (indices.IsComplete())
				{
					break;
				}

				++i;
			}

			return indices;
		}

		VulkanRenderObject::VulkanRenderObject(RenderID renderID) :
			renderID(renderID)
		{
		}

		u64 GraphicsPipelineCreateInfo::Hash()
		{
			// NOTE: Is this hash cryptographically secure? Heck no! Does it work for my purposes? Yes it does :)
			u64 result = 0u;

			result += (u64)shaderID * 111u;
			result += (u64)vertexAttributes * 652u;
			result += ((u64)topology * 931u) << 1u;
			result <<= 2;
			result += (u64)cullMode * 84u;
			result *= 982451653u;
			result += ((u64)renderPass + 1) * ((u64)renderPass + 1u);
			result += (u64)subpass * 46;
			result += ((u64)pushConstantRangeCount + 1) * 7u; // TODO: Deeper hash of push contant types
			result += (u64)(bSetDynamicStates ? 9568u : 458u);
			result += (u64)(bEnableColourBlending ? 19956u : 15485863u);
			result += (u64)(bEnableAdditiveColourBlending ? 898u : 123456789u);
			result <<= 1u;
			result += (u64)(depthTestEnable ? 77u : 2829u);
			result *= 492876847u;
			result += (u64)(depthWriteEnable ? 1613u : 259u);
			result += (u64)depthCompareOp * 45u;
			result += (u64)(stencilTestEnable ? 869u : 199u);

			return result;
		}

		void SetImageLayout(
			VkCommandBuffer cmdbuffer,
			VkImage image,
			VkImageLayout oldImageLayout,
			VkImageLayout newImageLayout,
			const VkImageSubresourceRange& subresourceRange,
			VkPipelineStageFlags srcStageMask,
			VkPipelineStageFlags dstStageMask)
		{
			// Create an image barrier object
			VkImageMemoryBarrier imageMemoryBarrier = vks::imageMemoryBarrier();
			imageMemoryBarrier.oldLayout = oldImageLayout;
			imageMemoryBarrier.newLayout = newImageLayout;
			imageMemoryBarrier.image = image;
			imageMemoryBarrier.subresourceRange = subresourceRange;

			// Source layouts (old)
			// Source access mask controls actions that have to be finished on the old layout
			// before it will be transitioned to the new layout
			switch (oldImageLayout)
			{
			case VK_IMAGE_LAYOUT_UNDEFINED:
				// Image layout is undefined (or does not matter)
				// Only valid as initial layout
				// No flags required, listed only for completeness
				imageMemoryBarrier.srcAccessMask = 0;
				break;

			case VK_IMAGE_LAYOUT_PREINITIALIZED:
				// Image is preinitialized
				// Only valid as initial layout for linear images, preserves memory contents
				// Make sure host writes have been finished
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
				break;

			case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
				// Image is a colour attachment
				// Make sure any writes to the colour buffer have been finished
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				break;

			case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
				// Image is a depth/stencil attachment
				// Make sure any writes to the depth/stencil buffer have been finished
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				break;

			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
				// Image is a transfer source
				// Make sure any reads from the image have been finished
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				break;

			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				// Image is a transfer destination
				// Make sure any writes to the image have been finished
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				break;

			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
				// Image is read by a shader
				// Make sure any shader reads from the image have been finished
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				break;
			default:
				// Other source layouts aren't handled (yet)
				break;
			}

			// Target layouts (new)
			// Destination access mask controls the dependency for the new image layout
			switch (newImageLayout)
			{
			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				// Image will be used as a transfer destination
				// Make sure any writes to the image have been finished
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				break;

			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
				// Image will be used as a transfer source
				// Make sure any reads from the image have been finished
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				break;

			case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
				// Image will be used as a colour attachment
				// Make sure any writes to the colour buffer have been finished
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				break;

			case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
				// Image layout will be used as a depth/stencil attachment
				// Make sure any writes to depth/stencil buffer have been finished
				imageMemoryBarrier.dstAccessMask = imageMemoryBarrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				break;

			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
				// Image will be read in a shader (sampler, input attachment)
				// Make sure any writes to the image have been finished
				if (imageMemoryBarrier.srcAccessMask == 0)
				{
					imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
				}
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				break;
			default:
				// Other source layouts aren't handled (yet)
				break;
			}

			// Put barrier inside setup command buffer
			vkCmdPipelineBarrier(
				cmdbuffer,
				srcStageMask,
				dstStageMask,
				0,
				0, nullptr,
				0, nullptr,
				1, &imageMemoryBarrier);
		}

		void SetImageLayout(VkCommandBuffer cmdbuffer, VulkanTexture* texture, VkImageLayout oldImageLayout, VkImageLayout newImageLayout, const VkImageSubresourceRange& subresourceRange, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask)
		{
			SetImageLayout(cmdbuffer, texture->image, oldImageLayout, newImageLayout, subresourceRange, srcStageMask, dstStageMask);
			texture->imageLayout = newImageLayout;
		}

		// Fixed sub resource on first mip level and layer
		void SetImageLayout(
			VkCommandBuffer cmdbuffer,
			VkImage image,
			VkImageAspectFlags aspectMask,
			VkImageLayout oldImageLayout,
			VkImageLayout newImageLayout,
			VkPipelineStageFlags srcStageMask,
			VkPipelineStageFlags dstStageMask)
		{
			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = aspectMask;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = 1;
			subresourceRange.layerCount = 1;
			SetImageLayout(cmdbuffer, image, oldImageLayout, newImageLayout, subresourceRange, srcStageMask, dstStageMask);
		}

		void SetImageLayout(VkCommandBuffer cmdbuffer, VulkanTexture* texture, VkImageAspectFlags aspectMask, VkImageLayout oldImageLayout, VkImageLayout newImageLayout, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask)
		{
			SetImageLayout(cmdbuffer, texture->image, aspectMask, oldImageLayout, newImageLayout, srcStageMask, dstStageMask);
			texture->imageLayout = newImageLayout;
		}

		void InsertImageMemoryBarrier(VkCommandBuffer cmdbuffer, VkImage image, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkImageLayout oldImageLayout, VkImageLayout newImageLayout, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkImageSubresourceRange subresourceRange)
		{
			VkImageMemoryBarrier imageMemoryBarrier = vks::imageMemoryBarrier();
			imageMemoryBarrier.srcAccessMask = srcAccessMask;
			imageMemoryBarrier.dstAccessMask = dstAccessMask;
			imageMemoryBarrier.oldLayout = oldImageLayout;
			imageMemoryBarrier.newLayout = newImageLayout;
			imageMemoryBarrier.image = image;
			imageMemoryBarrier.subresourceRange = subresourceRange;

			vkCmdPipelineBarrier(
				cmdbuffer,
				srcStageMask,
				dstStageMask,
				0,
				0, nullptr,
				0, nullptr,
				1, &imageMemoryBarrier);
		}

		void CreateAttachment(VulkanDevice* device, VkFormat format, VkImageUsageFlags usage, u32 width, u32 height,
			u32 arrayLayers, VkImageViewType imageViewType, VkImageCreateFlags imageFlags, VkImage* image,
			VkDeviceMemory* memory, VkImageView* imageView,
			const char* DBG_ImageName /* = nullptr */, const char* DBG_ImageViewName /* = nullptr */)
		{
			CHECK_NE((u32)format, (u32)VK_FORMAT_UNDEFINED);
			CHECK(width != 0 && height != 0);
			CHECK_LE(width, MAX_TEXTURE_DIM);
			CHECK_LE(height, MAX_TEXTURE_DIM);

			VkImageAspectFlags aspectMask = 0;

			if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
			{
				aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			}
			if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
			{
				aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

				// TODO: Verify
				if (HasStencilComponent(format))
				{
					aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
				}
			}

			if (aspectMask == 0)
			{
				// NOTE: Assuming colour here, if depth texture is needed without DEPTH_STENCIL_ATTACHMENT_BIT more robust solution is required
				aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			}

			VkImageCreateInfo imageCreateInfo = vks::imageCreateInfo();
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = format;
			imageCreateInfo.extent.width = width;
			imageCreateInfo.extent.height = height;
			imageCreateInfo.extent.depth = 1;
			imageCreateInfo.mipLevels = 1;
			imageCreateInfo.arrayLayers = arrayLayers;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT; // TODO: Get rid!
			imageCreateInfo.flags = imageFlags;

			VK_CHECK_RESULT(vkCreateImage(device->m_LogicalDevice, &imageCreateInfo, nullptr, image));
			VulkanRenderer::SetImageName(device, *image, DBG_ImageName);

			VkMemoryRequirements memRequirements;
			vkGetImageMemoryRequirements(device->m_LogicalDevice, *image, &memRequirements);
			VkMemoryAllocateInfo memAlloc = vks::memoryAllocateInfo(memRequirements.size);
			memAlloc.memoryTypeIndex = device->GetMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK_RESULT(device->AllocateMemory(DBG_ImageName, &memAlloc, nullptr, memory));
			VK_CHECK_RESULT(vkBindImageMemory(device->m_LogicalDevice, *image, *memory, 0));

			VkImageViewCreateInfo imageViewCreateInfo = vks::imageViewCreateInfo();
			imageViewCreateInfo.viewType = imageViewType;
			imageViewCreateInfo.format = format;
			imageViewCreateInfo.subresourceRange = {};
			imageViewCreateInfo.subresourceRange.aspectMask = aspectMask;
			imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
			imageViewCreateInfo.subresourceRange.levelCount = 1; // Number of mipmap levels
			imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
			imageViewCreateInfo.subresourceRange.layerCount = arrayLayers;
			imageViewCreateInfo.image = *image;
			imageViewCreateInfo.flags = 0;
			VK_CHECK_RESULT(vkCreateImageView(device->m_LogicalDevice, &imageViewCreateInfo, nullptr, imageView));
			VulkanRenderer::SetImageViewName(device, *imageView, DBG_ImageViewName);
		}

		void CreateAttachment(VulkanDevice* device, FrameBufferAttachment* frameBufferAttachment,
			const char* DBG_ImageName /* = nullptr */, const char* DBG_ImageViewName /* = nullptr */)
		{
			if (frameBufferAttachment->image != VK_NULL_HANDLE)
			{
				vkDestroyImage(device->m_LogicalDevice, frameBufferAttachment->image, nullptr);
				frameBufferAttachment->image = VK_NULL_HANDLE;
				vkDestroyImageView(device->m_LogicalDevice, frameBufferAttachment->view, nullptr);
				frameBufferAttachment->view = VK_NULL_HANDLE;
				device->FreeMemory(frameBufferAttachment->mem, nullptr);
				frameBufferAttachment->mem = VK_NULL_HANDLE;
			}

			CreateAttachment(
				device,
				frameBufferAttachment->format,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
				(frameBufferAttachment->bIsTransferedDst ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0) |
				(frameBufferAttachment->bIsTransferedSrc ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0),
				frameBufferAttachment->width,
				frameBufferAttachment->height,
				frameBufferAttachment->bIsCubemap ? 6 : 1,
				frameBufferAttachment->bIsCubemap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D,
				frameBufferAttachment->bIsCubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0,
				&frameBufferAttachment->image,
				&frameBufferAttachment->mem,
				&frameBufferAttachment->view,
				DBG_ImageName,
				DBG_ImageViewName);
			frameBufferAttachment->layout = VK_IMAGE_LAYOUT_UNDEFINED;

			((VulkanRenderer*)g_Renderer)->RegisterFramebufferAttachment(frameBufferAttachment);
		}

		template<class T>
		void CopyPixels(const T* srcData, T* dstData, u32 dstOffset, u32 width, u32 height, u32 channelCount, u32 pitch, bool bColourSwizzle)
		{
			dstData += dstOffset;

			i32 swizzle[4];
			if (bColourSwizzle)
			{
				swizzle[0] = 2;
				swizzle[1] = 1;
				swizzle[2] = 0;
				swizzle[3] = 3;
			}
			else
			{
				swizzle[0] = 0;
				swizzle[1] = 1;
				swizzle[2] = 2;
				swizzle[3] = 3;
			}

			u32 i = 0;
			for (u32 y = 0; y < height; y++)
			{
				const T* row = srcData;
				for (u32 x = 0; x < width; x++)
				{
					dstData[i + 0] = *(row + swizzle[0]);
					dstData[i + 1] = *(row + swizzle[1]);
					dstData[i + 2] = *(row + swizzle[2]);
					if (channelCount == 4)
					{
						dstData[i + 3] = *(row + swizzle[3]);
					}
					i += channelCount;
					row += channelCount;
				}
				srcData += pitch;
			}
		}

		VkBool32 GetSupportedDepthFormat(VkPhysicalDevice physicalDevice, VkFormat* depthFormat)
		{
			// Since all depth formats may be optional, we need to find a suitable depth format to use
			// Start with the highest precision packed format
			std::vector<VkFormat> depthFormats = {
				VK_FORMAT_D32_SFLOAT_S8_UINT,
				VK_FORMAT_D32_SFLOAT,
				VK_FORMAT_D24_UNORM_S8_UINT,
				VK_FORMAT_D16_UNORM_S8_UINT,
				VK_FORMAT_D16_UNORM
			};

			for (auto& format : depthFormats)
			{
				VkFormatProperties formatProps;
				vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProps);
				// Format must support depth stencil attachment for optimal tiling
				if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
				{
					*depthFormat = format;
					return true;
				}
			}

			return false;
		}

		VkCommandBuffer BeginSingleTimeCommands(VulkanDevice* device)
		{
			// TODO: Create command pool just for these types of allocations, using VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
			VkCommandBufferAllocateInfo allocInfo = vks::commandBufferAllocateInfo(device->m_CommandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);

			VkCommandBuffer commandBuffer;
			VK_CHECK_RESULT(vkAllocateCommandBuffers(device->m_LogicalDevice, &allocInfo, &commandBuffer));

			VkCommandBufferBeginInfo beginInfo = vks::commandBufferBeginInfo();
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

			VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginInfo));

			return commandBuffer;
		}

		void EndSingleTimeCommands(VulkanDevice* device, VkQueue queue, VkCommandBuffer commandBuffer)
		{
			VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

			VkSubmitInfo submitInfo = vks::submitInfo(1, &commandBuffer);

			VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
			VK_CHECK_RESULT(vkQueueWaitIdle(queue));

			vkFreeCommandBuffers(device->m_LogicalDevice, device->m_CommandPool, 1, &commandBuffer);
		}

		FrameBufferAttachment::FrameBufferAttachment(VulkanDevice* device, const CreateInfo& createInfo) :
			ID(GenerateUID()),
			device(device),
			width(createInfo.width),
			height(createInfo.height),
			bIsDepth(createInfo.bIsDepth),
			bIsSampled(createInfo.bIsSampled),
			bIsCubemap(createInfo.bIsCubemap),
			bIsTransferedSrc(createInfo.bIsTransferedSrc),
			bIsTransferedDst(createInfo.bIsTransferedDst),
			format(createInfo.format),
			layout(createInfo.initialLayout)
		{
		}

		FrameBufferAttachment::~FrameBufferAttachment()
		{
			if (bOwnsResources)
			{
				if (image != VK_NULL_HANDLE)
				{
					vkDestroyImage(device->m_LogicalDevice, image, nullptr);
				}
				if (mem != VK_NULL_HANDLE)
				{
					device->FreeMemory(mem, nullptr);
				}
				if (view != VK_NULL_HANDLE)
				{
					vkDestroyImageView(device->m_LogicalDevice, view, nullptr);
				}
			}
		}

		void FrameBufferAttachment::TransitionToLayout(VkImageLayout newLayout, VkQueue queue, VkCommandBuffer optCmdBuf /* = VK_NULL_HANDLE */)
		{
			TransitionImageLayout(device, queue, image, format, layout, newLayout, 1, optCmdBuf, bIsDepth);
			layout = newLayout;
		}

		void FrameBufferAttachment::CreateImage(u32 inWidth /* = 0 */, u32 inHeight /* = 0 */, const char* optDBGName /* = nullptr */)
		{
			if (inWidth != 0 && inHeight != 0)
			{
				width = inWidth;
				height = inHeight;
			}

			if (image != VK_NULL_HANDLE)
			{
				vkDestroyImage(device->m_LogicalDevice, image, nullptr);
				image = VK_NULL_HANDLE;
			}

			if (mem != VK_NULL_HANDLE)
			{
				device->FreeMemory(mem, nullptr);
				mem = VK_NULL_HANDLE;
			}

			VulkanTexture::ImageCreateInfo createInfo = {};
			createInfo.image = &image;
			createInfo.imageMemory = &mem;
			createInfo.width = width;
			createInfo.height = height;
			createInfo.format = format;
			if (bIsTransferedSrc)
			{
				createInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			}
			if (bIsTransferedDst)
			{
				createInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			}
			if (bIsSampled)
			{
				createInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
			}
			if (bIsDepth)
			{
				createInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			}
			else
			{
				createInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
			}
			createInfo.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			if (bIsCubemap)
			{
				createInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
				createInfo.arrayLayers = 6;
			}
			createInfo.DBG_Name = optDBGName;
			VulkanTexture::CreateImage(device, createInfo);

			layout = VK_IMAGE_LAYOUT_UNDEFINED;
		}

		void FrameBufferAttachment::CreateImageView(const char* optDBGName /* = nullptr */)
		{
			if (view != VK_NULL_HANDLE)
			{
				vkDestroyImageView(device->m_LogicalDevice, view, nullptr);
			}

			VulkanTexture::ImageViewCreateInfo createInfo = {};
			createInfo.image = &image;
			createInfo.imageView = &view;
			createInfo.format = format;
			if (bIsDepth)
			{
				createInfo.aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
			}
			else
			{
				createInfo.aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
			}
			if (bIsCubemap)
			{
				createInfo.layerCount = 6;
				createInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
			}
			createInfo.DBG_Name = optDBGName;
			VulkanTexture::CreateImageView(device, createInfo);
		}

		FrameBuffer::FrameBuffer(VulkanDevice* device) :
			m_VulkanDevice(device)
		{
		}

		FrameBuffer::~FrameBuffer()
		{
			Replace();
		}

		void FrameBuffer::Create(VkFramebufferCreateInfo* createInfo, VulkanRenderPass* inRenderPass, const char* debugName)
		{
			VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, createInfo, nullptr, Replace()));
			VulkanRenderer::SetFramebufferName(m_VulkanDevice, frameBuffer, debugName);

			width = createInfo->width;
			height = createInfo->height;
			renderPass = inRenderPass;
		}

		VkFramebuffer* FrameBuffer::Replace()
		{
			if (frameBuffer != VK_NULL_HANDLE)
			{
				vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, frameBuffer, nullptr);
				frameBuffer = VK_NULL_HANDLE;
			}
			return &frameBuffer;
		}

		VulkanShader::VulkanShader(const VDeleter<VkDevice>& device, ShaderInfo shaderInfo) :
			Shader(shaderInfo)
		{
			vertShaderModule = { device, vkDestroyShaderModule };
			fragShaderModule = { device, vkDestroyShaderModule };
			geomShaderModule = { device, vkDestroyShaderModule };
			computeShaderModule = { device, vkDestroyShaderModule };
		}

		VulkanShader::~VulkanShader()
		{
			if (fragSpecializationInfo != nullptr)
			{
				free((void*)fragSpecializationInfo->pMapEntries);
				free((void*)fragSpecializationInfo->pData);
				delete fragSpecializationInfo;
			}
		}

		Cascade::Cascade(VulkanDevice* device) :
			frameBuffer(device),
			imageView(device->m_LogicalDevice, vkDestroyImageView)
		{
		}

		Cascade::~Cascade()
		{
			if (attachment != nullptr)
			{
				// Prevent cleanup - cascade attachments don't own their resources, they just point at them
				attachment->image = VK_NULL_HANDLE;
				attachment->view = VK_NULL_HANDLE;
				attachment->mem = VK_NULL_HANDLE;
				delete attachment;
				attachment = nullptr;
			}
		}

		VulkanParticleSystem::VulkanParticleSystem(VulkanDevice* device) :
			computePipeline(device->m_LogicalDevice, vkDestroyPipeline)
		{
		}

		GraphicsPipeline::GraphicsPipeline(const VDeleter<VkDevice>& vulkanDevice) :
			pipeline(vulkanDevice, vkDestroyPipeline),
			layout(vulkanDevice, vkDestroyPipelineLayout)
		{
		}

		VkPrimitiveTopology TopologyModeToVkPrimitiveTopology(TopologyMode mode)
		{
			switch (mode)
			{
			case TopologyMode::POINT_LIST:		return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
			case TopologyMode::LINE_LIST:		return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
			case TopologyMode::LINE_STRIP:		return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
			case TopologyMode::TRIANGLE_LIST:	return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			case TopologyMode::TRIANGLE_STRIP:	return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
			case TopologyMode::TRIANGLE_FAN:	return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
			case TopologyMode::LINE_LOOP:
			{
				PrintError("LINE_LOOP is an unsupported TopologyMode passed to TopologyModeToVkPrimitiveTopology\n");
				return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
			}
			default:
			{
				PrintError("Unhandled TopologyMode passed to TopologyModeToVkPrimitiveTopology: %d\n", (i32)mode);
				return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
			}
			}
		}

		VkCullModeFlagBits CullFaceToVkCullMode(CullFace cullFace)
		{
			switch (cullFace)
			{
			case CullFace::BACK:			return VK_CULL_MODE_BACK_BIT;
			case CullFace::FRONT:			return VK_CULL_MODE_FRONT_BIT;
			case CullFace::FRONT_AND_BACK:	return VK_CULL_MODE_FRONT_AND_BACK;
			case CullFace::NONE:			return VK_CULL_MODE_NONE;
			default:
			{
				PrintError("Unhandled CullFace passed to CullFaceToVkCullMode: %d\n", (i32)cullFace);
				return VK_CULL_MODE_NONE;
			}
			}
		}

		TopologyMode VkPrimitiveTopologyToTopologyMode(VkPrimitiveTopology primitiveTopology)
		{
			if (primitiveTopology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST)
			{
				return TopologyMode::POINT_LIST;
			}
			else if (primitiveTopology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST)
			{
				return TopologyMode::LINE_LIST;
			}
			else if (primitiveTopology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP)
			{
				return TopologyMode::LINE_STRIP;
			}
			else if (primitiveTopology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
			{
				return TopologyMode::TRIANGLE_LIST;
			}
			else if (primitiveTopology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
			{
				return TopologyMode::TRIANGLE_STRIP;
			}
			else if (primitiveTopology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN)
			{
				return TopologyMode::TRIANGLE_FAN;
			}
			else
			{
				PrintError("Unhandled VkPrimitiveTopology passed to VkPrimitiveTopologyToTopologyMode: %d\n", (i32)primitiveTopology);
				return TopologyMode::_NONE;
			}
		}

		CullFace VkCullModeToCullFace(VkCullModeFlags cullMode)
		{
			if (cullMode == VK_CULL_MODE_BACK_BIT)
			{
				return CullFace::BACK;
			}
			else if (cullMode == VK_CULL_MODE_FRONT_BIT)
			{
				return CullFace::FRONT;
			}
			else if (cullMode == VK_CULL_MODE_FRONT_AND_BACK)
			{
				return CullFace::FRONT_AND_BACK;
			}
			else if (cullMode == VK_CULL_MODE_NONE)
			{
				return CullFace::NONE;
			}
			else
			{
				PrintError("Unhandled VkCullModeFlagBits passed to VkCullModeToCullFace: %d\n", (i32)cullMode);
				return CullFace::_INVALID;
			}
		}


		VkCompareOp DepthTestFuncToVkCompareOp(DepthTestFunc func)
		{
			switch (func)
			{
			case DepthTestFunc::ALWAYS:		return VK_COMPARE_OP_ALWAYS;
			case DepthTestFunc::NEVER:		return VK_COMPARE_OP_NEVER;
			case DepthTestFunc::LESS:		return VK_COMPARE_OP_LESS;
			case DepthTestFunc::LEQUAL:		return VK_COMPARE_OP_LESS_OR_EQUAL;
			case DepthTestFunc::GREATER:	return VK_COMPARE_OP_GREATER;
			case DepthTestFunc::GEQUAL:		return VK_COMPARE_OP_GREATER_OR_EQUAL;
			case DepthTestFunc::EQUAL:		return VK_COMPARE_OP_EQUAL;
			case DepthTestFunc::NOTEQUAL:	return VK_COMPARE_OP_NOT_EQUAL;
			default:						return VK_COMPARE_OP_MAX_ENUM;
			}
		}

		std::string DeviceTypeToString(VkPhysicalDeviceType type)
		{
			switch (type)
			{
			case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "Integrated GPU";
			case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "Discrete GPU";
			case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "Virtual GPU";
			case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU";
			case VK_PHYSICAL_DEVICE_TYPE_OTHER:
			default: return  "Other";
			}
		}

		VkResult CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* createInfo, const VkAllocationCallbacks* allocator, VkDebugReportCallbackEXT* callback)
		{
			auto func = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
			if (func != nullptr)
			{
				return func(instance, createInfo, allocator, callback);
			}
			else
			{
				return VK_ERROR_EXTENSION_NOT_PRESENT;
			}
		}

		VulkanDescriptorPool::VulkanDescriptorPool()
		{
		}

		VulkanDescriptorPool::VulkanDescriptorPool(VulkanDevice* device, const char* name) :
			device(device),
			name(name),
			size(maxNumDescSets)
		{
			std::vector<VkDescriptorPoolSize> poolSizes
			{
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_NUM_DESC_COMBINED_IMAGE_SAMPLERS * maxNumDescSets },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_NUM_DESC_UNIFORM_BUFFERS * maxNumDescSets },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, MAX_NUM_DESC_DYNAMIC_UNIFORM_BUFFERS * maxNumDescSets },
				{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, MAX_NUM_DESC_DYNAMIC_STORAGE_BUFFERS * maxNumDescSets },
				{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_NUM_DESC_STORAGE_BUFFERS * maxNumDescSets },
			};

			VkDescriptorPoolCreateInfo poolInfo = vks::descriptorPoolCreateInfo(poolSizes, maxNumDescSets);
			// TODO: Avoid using this flag at all
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // Allow descriptor sets to be added/removed individually

			VK_CHECK_RESULT(vkCreateDescriptorPool(device->m_LogicalDevice, &poolInfo, nullptr, &pool));
		}

		VulkanDescriptorPool::~VulkanDescriptorPool()
		{
			vkDestroyDescriptorPool(device->m_LogicalDevice, pool, nullptr);
		}

		VkDescriptorSet VulkanDescriptorPool::CreateDescriptorSet(DescriptorSetCreateInfo* createInfo)
		{
			VkDescriptorSetLayout layouts[] = { createInfo->descriptorSetLayout };
			VkDescriptorSetAllocateInfo allocInfo = vks::descriptorSetAllocateInfo(pool, layouts, 1);

			if ((allocatedSetCount + 1) > maxNumDescSets)
			{
				// TODO: Create new pool or recreate and copy old one?
				//maxNumDescSets *= 2;
				PRINT_FATAL("Ran out of descriptor sets (max: %d)\n", maxNumDescSets);
				return VK_NULL_HANDLE;
			}

			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
			// TODO: Optimization: Allocate all required descriptor sets in one call rather than 1 at a time
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device->m_LogicalDevice, &allocInfo, &descriptorSet));

			++allocatedSetCount;

			if (createInfo->DBG_Name != nullptr)
			{
				((VulkanRenderer*)g_Renderer)->SetDescriptorSetName(device, descriptorSet, createInfo->DBG_Name);
			}

			Shader* shader = g_Renderer->GetShader(createInfo->shaderID);

			UniformList constantBufferUniforms = shader->constantBufferUniforms;
			UniformList dynamicBufferUniforms = shader->dynamicBufferUniforms;
			UniformList additionalBufferUniforms = shader->additionalBufferUniforms;
			UniformList textureUniforms = shader->textureUniforms;

			std::vector<VkWriteDescriptorSet> writeDescriptorSets;
			writeDescriptorSets.reserve(
				(size_t)createInfo->bufferDescriptors.Count() +
				(size_t)createInfo->imageDescriptors.Count());

			u32 binding = 0;

			std::vector<VkDescriptorBufferInfo> bufferInfos(createInfo->bufferDescriptors.Count());
			u32 i = 0;
			for (auto& pair : createInfo->bufferDescriptors)
			{
				const u64 uniformID = pair.uniform->id;
				const BufferDescriptorInfo& bufferDescInfo = pair.object;
				CHECK((bufferDescInfo.type == GPUBufferType::DYNAMIC && dynamicBufferUniforms.HasUniform(uniformID)) ||
					(bufferDescInfo.type == GPUBufferType::STATIC && constantBufferUniforms.HasUniform(uniformID)) ||
					(bufferDescInfo.type == GPUBufferType::PARTICLE_DATA && additionalBufferUniforms.HasUniform(uniformID)) ||
					(bufferDescInfo.type == GPUBufferType::TERRAIN_POINT_BUFFER && additionalBufferUniforms.HasUniform(uniformID)) ||
					(bufferDescInfo.type == GPUBufferType::TERRAIN_VERTEX_BUFFER && additionalBufferUniforms.HasUniform(uniformID)));
				CHECK_NE(bufferDescInfo.buffer, (VkBuffer)VK_NULL_HANDLE);

				VkDescriptorType type;
				switch (bufferDescInfo.type)
				{
				case GPUBufferType::STATIC:
					type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
					break;
				case GPUBufferType::DYNAMIC:
					type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
					break;
				case GPUBufferType::PARTICLE_DATA:
					type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
					break;
				case GPUBufferType::TERRAIN_POINT_BUFFER:
					type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					break;
				case GPUBufferType::TERRAIN_VERTEX_BUFFER:
					type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					break;
				default:
					type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
					ENSURE_NO_ENTRY();
					break;
				}

				VkDescriptorBufferInfo& bufferInfo = bufferInfos[i];
				bufferInfo.buffer = pair.object.buffer;
				bufferInfo.range = pair.object.bufferSize;
				writeDescriptorSets.push_back(vks::writeDescriptorSet(descriptorSet, type, binding, &bufferInfo));

				++binding;
				++i;
			}

			std::vector<VkDescriptorImageInfo> imageInfos(createInfo->imageDescriptors.Count());
			i = 0;
			for (const auto& pair : createInfo->imageDescriptors)
			{
				CHECK(textureUniforms.HasUniform(pair.uniform));
				const ImageDescriptorInfo& imageDescInfo = pair.object;

				VkDescriptorImageInfo& imageInfo = imageInfos[i];
				imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				if (imageDescInfo.imageView != VK_NULL_HANDLE)
				{
					imageInfo.imageView = imageDescInfo.imageView;
					if (imageDescInfo.imageSampler != VK_NULL_HANDLE)
					{
						imageInfo.sampler = imageDescInfo.imageSampler;
					}
					else
					{
						imageInfo.sampler = ((VulkanRenderer*)g_Renderer)->m_SamplerLinearRepeat;
					}
				}
				else
				{
					VulkanTexture* blankTexture = ((VulkanTexture*)g_Renderer->m_BlankTexture);
					imageInfo.imageView = blankTexture->imageView;
					imageInfo.sampler = *blankTexture->sampler;
				}
				writeDescriptorSets.push_back(vks::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, binding, &imageInfo));

				++binding;
				++i;
			}

			if (!writeDescriptorSets.empty())
			{
				vkUpdateDescriptorSets(device->m_LogicalDevice, (u32)writeDescriptorSets.size(), writeDescriptorSets.data(), 0u, nullptr);
			}

			return descriptorSet;
		}

		VkDescriptorSet VulkanDescriptorPool::CreateDescriptorSet(MaterialID materialID, const char* DBG_Name /* = nullptr */)
		{
			DescriptorSetCreateInfo createInfo = {};
			createInfo.DBG_Name = DBG_Name;

			if (descriptorSets.size() > materialID)
			{
				if (descriptorSets[materialID] != VK_NULL_HANDLE)
				{
					vkFreeDescriptorSets(device->m_LogicalDevice, pool, 1, &descriptorSets[materialID]);
					descriptorSets[materialID] = VK_NULL_HANDLE;
					--allocatedSetCount;
				}
			}

			VulkanMaterial* material = (VulkanMaterial*)g_Renderer->GetMaterial(materialID);

			createInfo.descriptorSetLayout = GetOrCreateLayout(material->shaderID);
			createInfo.shaderID = material->shaderID;
			createInfo.gpuBufferList = &material->gpuBufferList;

			((VulkanRenderer*)g_Renderer)->FillOutTextureDescriptorInfos(&createInfo.imageDescriptors, materialID);
			((VulkanRenderer*)g_Renderer)->FillOutBufferDescriptorInfos(&createInfo.bufferDescriptors, createInfo.gpuBufferList, createInfo.shaderID);

			VkDescriptorSet descriptorSet = CreateDescriptorSet(&createInfo);

			if (materialID >= (u32)descriptorSets.size())
			{
				descriptorSets.resize(materialID + 1);
			}
			descriptorSets[materialID] = descriptorSet;

			layoutUsageCounts[createInfo.descriptorSetLayout]++;

			return descriptorSet;
		}

		VkDescriptorSet VulkanDescriptorPool::GetSet(MaterialID materialID)
		{
			if (materialID < (u32)descriptorSets.size())
			{
				return descriptorSets[materialID];
			}

			return VK_NULL_HANDLE;
		}

		VkDescriptorSet VulkanDescriptorPool::GetOrCreateSet(MaterialID materialID, const char* DBG_Name)
		{
			if (materialID >= (u32)descriptorSets.size() || descriptorSets[materialID] == VK_NULL_HANDLE)
			{
				return CreateDescriptorSet(materialID, DBG_Name);
			}

			return descriptorSets[materialID];
		}

		VkDescriptorSetLayout VulkanDescriptorPool::CreateDescriptorSetLayout(ShaderID shaderID)
		{
			PROFILE_AUTO("CreateDescriptorSetLayout");

			VkDescriptorSetLayout descriptorSetLayout = {};

			VulkanShader* shader = (VulkanShader*)g_Renderer->GetShader(shaderID);

			struct DescriptorSetInfo
			{
				Uniform const* uniform;
				VkDescriptorType descriptorType;
				VkShaderStageFlags shaderStageFlags;
			};

			// TODO: Specify stage flags per shader!
			static DescriptorSetInfo descriptorSetInfos[] = {
				{ &U_UNIFORM_BUFFER_CONSTANT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_COMPUTE_BIT },

				{ &U_UNIFORM_BUFFER_DYNAMIC, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_COMPUTE_BIT },

				{ &U_ALBEDO_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_EMISSIVE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_METALLIC_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_ROUGHNESS_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_NORMAL_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_HDR_EQUIRECTANGULAR_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_CUBEMAP_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_BRDF_LUT_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_IRRADIANCE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_PREFILTER_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_FB_0_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_FB_1_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_LTC_MATRICES_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_LTC_AMPLITUDES_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_HIGH_RES_TEX, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_DEPTH_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_SSAO_NORMAL_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_NOISE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_SSAO_RAW_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_SSAO_FINAL_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_SHADOW_CASCADES_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_SCENE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_HISTORY_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ &U_PARTICLE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
				VK_SHADER_STAGE_COMPUTE_BIT },

				{ &U_TERRAIN_POINT_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				VK_SHADER_STAGE_COMPUTE_BIT },

				{ &U_TERRAIN_VERTEX_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT },

				{ &U_RANDOM_TABLES, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_COMPUTE_BIT },
			};

			std::vector<VkDescriptorSetLayoutBinding> bindings;

			std::vector<Uniform const*> uniforms;
			for (DescriptorSetInfo& descSetInfo : descriptorSetInfos)
			{
				if (shader->constantBufferUniforms.HasUniform(descSetInfo.uniform) ||
					shader->dynamicBufferUniforms.HasUniform(descSetInfo.uniform) ||
					shader->additionalBufferUniforms.HasUniform(descSetInfo.uniform) ||
					shader->textureUniforms.HasUniform(descSetInfo.uniform))
				{
					VkDescriptorSetLayoutBinding descSetLayoutBinding = {};
					descSetLayoutBinding.binding = (u32)bindings.size();
					descSetLayoutBinding.descriptorCount = 1;
					descSetLayoutBinding.descriptorType = descSetInfo.descriptorType;
					descSetLayoutBinding.stageFlags = descSetInfo.shaderStageFlags;
					bindings.push_back(descSetLayoutBinding);

					descriptorTypeCounts[descSetInfo.descriptorType]++;

					uniforms.push_back(descSetInfo.uniform);
				}
			}

			VkDescriptorSetLayoutCreateInfo layoutInfo = vks::descriptorSetLayoutCreateInfo(bindings);

			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->m_LogicalDevice, &layoutInfo, nullptr, &descriptorSetLayout));

			std::string descSetLayoutName = shader->name + " descriptor set layout";
			((VulkanRenderer*)g_Renderer)->SetDescriptorSetLayoutName(device, descriptorSetLayout, descSetLayoutName.c_str());

			descriptorSetLayouts[shaderID] = descriptorSetLayout;
			layoutUniforms[descriptorSetLayout] = uniforms;

			return descriptorSetLayouts[shaderID];
		}

		VkDescriptorSetLayout VulkanDescriptorPool::GetOrCreateLayout(ShaderID shaderID)
		{
			auto iter = descriptorSetLayouts.find(shaderID);
			if (iter == descriptorSetLayouts.end())
			{
				return CreateDescriptorSetLayout(shaderID);
			}

			return iter->second;
		}

		void VulkanDescriptorPool::Replace()
		{
			for (const auto& pair : descriptorSetLayouts)
			{
				vkDestroyDescriptorSetLayout(device->m_LogicalDevice, pair.second, nullptr);
			}
			descriptorSetLayouts.clear();
			descriptorSets.clear();

			layoutUniforms.clear();
			layoutUsageCounts.clear();
			descriptorTypeCounts.clear();

			vkDestroyDescriptorPool(device->m_LogicalDevice, pool, nullptr);
			pool = VK_NULL_HANDLE;

			allocatedSetCount = 0;
		}

		void VulkanDescriptorPool::Reset()
		{
			for (const auto& pair : descriptorSetLayouts)
			{
				vkDestroyDescriptorSetLayout(device->m_LogicalDevice, pair.second, nullptr);
			}
			descriptorSetLayouts.clear();
			descriptorSets.clear();

			layoutUniforms.clear();
			layoutUsageCounts.clear();
			descriptorTypeCounts.clear();

			vkResetDescriptorPool(device->m_LogicalDevice, pool, 0);

			allocatedSetCount = 0;
		}

		void VulkanDescriptorPool::FreeSet(VkDescriptorSet descSet)
		{
			bool bFound = false;
			for (u32 i = 0; i < descriptorSets.size(); ++i)
			{
				if (descriptorSets[i] == descSet)
				{
					descriptorSets[i] = VK_NULL_HANDLE;
					--allocatedSetCount;
					bFound = true;
					break;
				}
			}

			if (!bFound)
			{
				PrintError("Didn't find descriptor set in VulkanDescriptorPool::FreeSet: %lu\n", (u64)descSet);
			}

			vkFreeDescriptorSets(device->m_LogicalDevice, pool, 1, &descSet);
		}

		void VulkanDescriptorPool::DrawImGui()
		{
			if (ImGui::TreeNode(name))
			{
				if (ImGui::TreeNode("Layouts"))
				{
					i32 i = 0;
					for (const auto& pair : layoutUniforms)
					{
						ImGui::PushID(i++);
						std::string label = "Layout";
						auto usageCountIter = layoutUsageCounts.find(pair.first);
						if (usageCountIter != layoutUsageCounts.end())
						{
							u32 usageCount = usageCountIter->second;
							label += " (instances: " + std::to_string(usageCount) + ")";
						}
						if (ImGui::TreeNode(label.c_str()))
						{
							for (Uniform const* uniform : pair.second)
							{
#if DEBUG
								ImGui::Text("%s", uniform->DBG_name);
#else
								ImGui::Text("Size: %u (name stripped)", uniform->size);
#endif
							}

							ImGui::TreePop();
						}
						ImGui::PopID();
					}
					ImGui::TreePop();
				}

				ImGui::Text("Sets allocated: %u/%u", allocatedSetCount, maxNumDescSets);

				ImGui::Text("Types allocated");
				ImGui::Indent();
				for (const auto& pair : descriptorTypeCounts)
				{
					std::string descTypeStr = vkhpp::to_string((vkhpp::DescriptorType)pair.first);
					ImGui::Text("%s: %u", descTypeStr.c_str(), pair.second);
				}
				ImGui::Unindent();

				ImGui::TreePop();
			}
		}

		VkDescriptorPool VulkanDescriptorPool::GetPool() const
		{
			return pool;
		}
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN