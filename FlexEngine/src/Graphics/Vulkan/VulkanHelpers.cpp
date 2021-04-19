#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanHelpers.hpp"

IGNORE_WARNINGS_PUSH
#include "stb_image.h"

#if COMPILE_SHADER_COMPILER
#include "spvc/spvc.hpp"
#include "shaderc/shaderc.hpp"
#endif
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
#include "Profiler.hpp"
#include "Time.hpp"

namespace flex
{
	namespace vk
	{
#if COMPILE_SHADER_COMPILER
		std::string VulkanShaderCompiler::s_ChecksumFilePathAbs;

		const char* VulkanShaderCompiler::s_RecognizedShaderTypes[] = { "vert", "geom", "frag", "comp", "glsl" };
#endif //  COMPILE_SHADER_COMPILER

		void VK_CHECK_RESULT(VkResult result)
		{
			if (result != VK_SUCCESS)
			{
				PrintError("Vulkan fatal error: %s\n", VulkanErrorString(result).c_str());
				((VulkanRenderer*)g_Renderer)->GetCheckPointData();
				DEBUG_BREAK();
				assert(result == VK_SUCCESS);
			}
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

		UniformBuffer::UniformBuffer(VulkanDevice* device, UniformBufferType type) :
			buffer(device),
			type(type)
		{
		}

		UniformBuffer::~UniformBuffer()
		{
			if (data.data)
			{
				if (type == UniformBufferType::DYNAMIC ||
					type == UniformBufferType::PARTICLE_DATA)
				{
					flex_aligned_free(data.data);
				}
				else
				{
					free(data.data);
				}
				data.data = nullptr;
			}
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
			vertexCount = 0;
			indexCount = 0;
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
			vertexCount = 0;
			indexCount = 0;
		}

		VulkanTexture::VulkanTexture(VulkanDevice* device, VkQueue graphicsQueue) :
			Texture(),
			image(device->m_LogicalDevice, vkDestroyImage),
			imageMemory(device->m_LogicalDevice, vkFreeMemory),
			imageView(device->m_LogicalDevice, vkDestroyImageView),
			sampler(device->m_LogicalDevice, vkDestroySampler),
			m_VulkanDevice(device),
			m_GraphicsQueue(graphicsQueue)
		{
		}

		VulkanTexture::VulkanTexture(VulkanDevice* device, VkQueue graphicsQueue, const std::string& name) :
			Texture(name),
			image(device->m_LogicalDevice, vkDestroyImage),
			imageMemory(device->m_LogicalDevice, vkFreeMemory),
			imageView(device->m_LogicalDevice, vkDestroyImageView),
			sampler(device->m_LogicalDevice, vkDestroySampler),
			m_VulkanDevice(device),
			m_GraphicsQueue(graphicsQueue)
		{
		}

		u32 VulkanTexture::CreateFromMemory(void* buffer, u32 bufferSize, u32 inWidth, u32 inHeight, u32 inChannelCount,
			VkFormat inFormat, i32 inMipLevels, VkFilter filter /* = VK_FILTER_LINEAR */, i32 layerCount /* = 1 */)
		{
			assert(inWidth != 0 && inHeight != 0);
			assert(buffer != nullptr);
			assert(bufferSize != 0);
			assert((!bIsArray && layerCount == 1) || (bIsArray && layerCount >= 1));

			width = inWidth;
			height = inHeight;
			channelCount = inChannelCount;
			imageFormat = inFormat;

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
			stagingBuffer.Create(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			void* data = nullptr;
			VK_CHECK_RESULT(vkMapMemory(m_VulkanDevice->m_LogicalDevice, stagingBuffer.m_Memory, 0, imageSize, 0, &data));
			memcpy(data, buffer, bufferSize);
			vkUnmapMemory(m_VulkanDevice->m_LogicalDevice, stagingBuffer.m_Memory);
			{
				VkCommandBuffer cmdBuffer = BeginSingleTimeCommands(m_VulkanDevice);
				std::string debugMarkerStr = "Uploading texture data from memory into " + name;
				VulkanRenderer::BeginDebugMarkerRegion(cmdBuffer, debugMarkerStr.c_str());

				TransitionToLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmdBuffer);
				CopyFromBuffer(stagingBuffer.m_Buffer, width, height, cmdBuffer);
				TransitionToLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmdBuffer);

				VulkanRenderer::EndDebugMarkerRegion(cmdBuffer);
				EndSingleTimeCommands(m_VulkanDevice, m_GraphicsQueue, cmdBuffer);
			}

			ImageViewCreateInfo viewCreateInfo = {};
			viewCreateInfo.DBG_Name = name.c_str();
			viewCreateInfo.viewType = (bIsArray ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
			viewCreateInfo.format = inFormat;
			viewCreateInfo.image = &image;
			viewCreateInfo.imageView = &imageView;
			viewCreateInfo.mipLevels = inMipLevels;
			CreateImageView(m_VulkanDevice, viewCreateInfo);

			SamplerCreateInfo samplerCreateInfo = {};
			samplerCreateInfo.DBG_Name = name.c_str();
			samplerCreateInfo.sampler = &sampler;
			samplerCreateInfo.minFilter = filter;
			samplerCreateInfo.magFilter = filter;
			if (bSamplerClampToBorder)
			{
				samplerCreateInfo.samplerAddressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			}
			else
			{
				samplerCreateInfo.samplerAddressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			}
			CreateSampler(m_VulkanDevice, samplerCreateInfo);

			return imageSize;
		}

		void VulkanTexture::TransitionToLayout(VkImageLayout newLayout, VkCommandBuffer optCommandBuffer /* = VK_NULL_HANDLE */)
		{
			if (imageLayout == newLayout)
			{
				PrintWarn("Redundant image layout transition on %s, already in %u\n", name.c_str(), (u32)imageLayout);
			}
			TransitionImageLayout(m_VulkanDevice, m_GraphicsQueue, image, imageFormat, imageLayout, newLayout, mipLevels, optCommandBuffer);
			imageLayout = newLayout;
		}

		void VulkanTexture::CopyFromBuffer(VkBuffer buffer, u32 inWidth, u32 inHeight, VkCommandBuffer optCommandBuffer /* = 0 */)
		{
			CopyBufferToImage(m_VulkanDevice, m_GraphicsQueue, buffer, image, inWidth, inHeight, optCommandBuffer);
			width = inWidth;
			height = inHeight;
		}

		VkDeviceSize VulkanTexture::CreateEmpty(u32 inWidth, u32 inHeight, u32 inChannelCount, VkFormat inFormat, u32 inMipLevels, VkImageUsageFlags inUsage)
		{
			assert(inWidth > 0);
			assert(inHeight > 0);

			width = inWidth;
			height = inHeight;
			channelCount = inChannelCount;
			mipLevels = inMipLevels;
			imageFormat = inFormat;

			ImageCreateInfo imageCreateInfo = {};
			imageCreateInfo.image = image.replace();
			imageCreateInfo.imageMemory = imageMemory.replace();
			imageCreateInfo.format = inFormat;
			imageCreateInfo.width = inWidth;
			imageCreateInfo.height = inHeight;
			imageCreateInfo.mipLevels = inMipLevels;
			imageCreateInfo.usage = inUsage;
			imageCreateInfo.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

			VkDeviceSize imageSize = CreateImage(m_VulkanDevice, imageCreateInfo);

			ImageViewCreateInfo imageViewCreateInfo = {};
			imageViewCreateInfo.image = &image;
			imageViewCreateInfo.imageView = &imageView;
			imageViewCreateInfo.format = inFormat;
			imageViewCreateInfo.mipLevels = inMipLevels;
			CreateImageView(m_VulkanDevice, imageViewCreateInfo);

			SamplerCreateInfo samplerCreateInfo = {};
			samplerCreateInfo.sampler = &sampler;
			CreateSampler(m_VulkanDevice, samplerCreateInfo);

			return imageSize;
		}

		VkDeviceSize VulkanTexture::CreateCubemap(VulkanDevice* device, VkQueue graphicsQueue, CubemapCreateInfo& createInfo)
		{
			FLEX_UNUSED(graphicsQueue);

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

			VK_CHECK_RESULT(vkAllocateMemory(device->m_LogicalDevice, &memAllocInfo, nullptr, createInfo.imageMemory));
			VK_CHECK_RESULT(vkBindImageMemory(device->m_LogicalDevice, *createInfo.image, *createInfo.imageMemory, 0));

			VkSamplerCreateInfo samplerCreateInfo = vks::samplerCreateInfo();
			samplerCreateInfo.maxAnisotropy = 1.0f;
			samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
			samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
			samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerCreateInfo.addressModeV = samplerCreateInfo.addressModeU;
			samplerCreateInfo.addressModeW = samplerCreateInfo.addressModeU;
			samplerCreateInfo.mipLodBias = 0.0f;
			samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
			samplerCreateInfo.minLod = 0.0f;
			samplerCreateInfo.maxLod = (real)createInfo.mipLevels;
			samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			// Enable anisotropy if desired with:
			//if (device->m_PhysicalDeviceFeatures.samplerAnisotropy)
			//{
			//	samplerCreateInfo.maxAnisotropy = device->m_PhysicalDeviceProperties.limits.maxSamplerAnisotropy;
			//	samplerCreateInfo.anisotropyEnable = VK_TRUE;
			//}

			VK_CHECK_RESULT(vkCreateSampler(device->m_LogicalDevice, &samplerCreateInfo, nullptr, createInfo.sampler));
			VulkanRenderer::SetSamplerName(device, *createInfo.sampler, createInfo.DBG_Name);

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

		VkDeviceSize VulkanTexture::CreateCubemapEmpty(u32 inWidth, u32 inHeight, u32 inChannelCount, VkFormat inFormat, u32 inMipLevels, bool bEnableTrilinearFiltering)
		{
			assert(inWidth > 0);
			assert(inHeight > 0);
			assert(inChannelCount > 0);

			width = inWidth;
			height = inHeight;
			channelCount = inChannelCount;

			CubemapCreateInfo createInfo = {};
			createInfo.image = &image;
			createInfo.imageMemory = &imageMemory;
			createInfo.imageView = &imageView;
			createInfo.sampler = &sampler;
			createInfo.format = inFormat;
			createInfo.width = width;
			createInfo.height = height;
			createInfo.channels = channelCount;
			createInfo.totalSize = width * height * channelCount * 6;
			createInfo.mipLevels = inMipLevels;
			createInfo.bEnableTrilinearFiltering = bEnableTrilinearFiltering;

			VkDeviceSize imageSize = CreateCubemap(m_VulkanDevice, m_GraphicsQueue, createInfo);

			// Retrieve out variables
			imageLayout = createInfo.imageLayoutOut;
			imageFormat = inFormat;

			return imageSize;
		}

		VkDeviceSize VulkanTexture::CreateCubemapFromTextures(VkFormat inFormat, const std::array<std::string, 6>& filePaths, bool bEnableTrilinearFiltering)
		{
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
			stagingBuffer.Create(totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			stagingBuffer.Map();
			memcpy(stagingBuffer.m_Mapped, pixels, totalSize);
			stagingBuffer.Unmap();
			free(pixels);

			CubemapCreateInfo createInfo = {};
			createInfo.image = &image;
			createInfo.imageMemory = &imageMemory;
			createInfo.imageView = &imageView;
			createInfo.sampler = &sampler;
			createInfo.totalSize = totalSize;
			createInfo.format = inFormat;
			createInfo.filePaths = filePaths;
			createInfo.bEnableTrilinearFiltering = bEnableTrilinearFiltering;

			VkDeviceSize imageSize = CreateCubemap(m_VulkanDevice, m_GraphicsQueue, createInfo);

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

			VulkanCommandBufferManager::FlushCommandBuffer(m_VulkanDevice, copyCmd, m_GraphicsQueue, true);

			return imageSize;
		}

		void VulkanTexture::GenerateMipmaps()
		{
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

			EndSingleTimeCommands(m_VulkanDevice, m_GraphicsQueue, cmdBuffer);
		}

		VkDeviceSize VulkanTexture::CreateImage(VulkanDevice* device, ImageCreateInfo& createInfo)
		{
			assert(createInfo.width != 0);
			assert(createInfo.height != 0);
			assert(createInfo.format != VK_FORMAT_UNDEFINED);

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

			VK_CHECK_RESULT(vkAllocateMemory(device->m_LogicalDevice, &allocInfo, nullptr, createInfo.imageMemory));

			VK_CHECK_RESULT(vkBindImageMemory(device->m_LogicalDevice, *createInfo.image, *createInfo.imageMemory, 0));

			return memRequirements.size;
		}

		VkDeviceSize VulkanTexture::CreateFromFile(
			const std::string& inRelativeFilePath,
			VkFormat inFormat /* = VK_FORMAT_UNDEFINED */,
			bool bGenerateFullMipChain /* = false */)
		{
			relativeFilePath = inRelativeFilePath;
			fileName = StripLeadingDirectories(inRelativeFilePath);

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

			u32 imageSize = (u32)CreateImage(m_VulkanDevice, imageCreateInfo);

			if (imageSize == 0)
			{
				width = 0;
				height = 0;
				channelCount = 0;
				return 0;
			}

			imageLayout = imageCreateInfo.initialLayout;

			VulkanBuffer stagingBuffer(m_VulkanDevice);
			stagingBuffer.Create(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

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
				EndSingleTimeCommands(m_VulkanDevice, m_GraphicsQueue, cmdBuffer);
			}

			ImageViewCreateInfo viewCreateInfo = {};
			viewCreateInfo.format = imageFormat;
			viewCreateInfo.image = &image;
			viewCreateInfo.imageView = &imageView;
			viewCreateInfo.mipLevels = mipLevels;
			CreateImageView(m_VulkanDevice, viewCreateInfo);

			SamplerCreateInfo samplerCreateInfo = {};
			samplerCreateInfo.sampler = &sampler;
			samplerCreateInfo.maxLod = (real)mipLevels;
			CreateSampler(m_VulkanDevice, samplerCreateInfo);

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
			samplerInfo.addressModeU = createInfo.samplerAddressMode;
			samplerInfo.addressModeV = createInfo.samplerAddressMode;
			samplerInfo.addressModeW = createInfo.samplerAddressMode;
			samplerInfo.anisotropyEnable = VK_FALSE;
			samplerInfo.maxAnisotropy = createInfo.maxAnisotropy;
			samplerInfo.borderColor = createInfo.borderColor;
			samplerInfo.unnormalizedCoordinates = VK_FALSE;
			samplerInfo.compareEnable = VK_FALSE;
			samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerInfo.mipLodBias = 0.0f;
			samplerInfo.minLod = createInfo.minLod;
			samplerInfo.maxLod = createInfo.maxLod;

			VK_CHECK_RESULT(vkCreateSampler(device->m_LogicalDevice, &samplerInfo, nullptr, createInfo.sampler));
			VulkanRenderer::SetSamplerName(device, *createInfo.sampler, createInfo.DBG_Name);
		}

		bool VulkanTexture::SaveToFile(const std::string& absoluteFilePath, ImageFormat saveFormat)
		{
			assert(channelCount == 3 || channelCount == 4);

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
				VK_CHECK_RESULT(vkAllocateMemory(m_VulkanDevice->m_LogicalDevice, &memAllocInfo, nullptr, &dstImageMemory));
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

				TransitionToLayout(previousLayout);

				VulkanCommandBufferManager::FlushCommandBuffer(m_VulkanDevice, copyCmd, m_GraphicsQueue, true);

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
				vkFreeMemory(m_VulkanDevice->m_LogicalDevice, dstImageMemory, nullptr);
				vkDestroyImage(m_VulkanDevice->m_LogicalDevice, dstImage, nullptr);
			}
			else
			{
				PrintError("Failed to allocate %d bytes to save out to texture at %s\n", u8BufSize, absoluteFilePath.c_str());
			}

			free(u8Data);

			return bResult;
		}

		void VulkanTexture::Build(void* data /* = nullptr */)
		{
			FLEX_UNUSED(data);
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
		void TransitionImageLayout(VulkanDevice* device, VkQueue graphicsQueue, VkImage image, VkFormat format, VkImageLayout oldLayout,
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
				assert(oldLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
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
				EndSingleTimeCommands(device, graphicsQueue, commandBuffer);
			}
		}

		void CopyImage(VulkanDevice* device, VkQueue graphicsQueue, VkImage srcImage, VkImage dstImage, u32 width, u32 height,
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
				EndSingleTimeCommands(device, graphicsQueue, commandBuffer);
			}
		}

		void CopyBufferToImage(VulkanDevice* device, VkQueue graphicsQueue, VkBuffer buffer, VkImage image,
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
				EndSingleTimeCommands(device, graphicsQueue, commandBuffer);
			}
		}

		void CopyBuffer(VulkanDevice* device, VkQueue graphicsQueue, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset)
		{
			VkCommandBuffer commandBuffer = BeginSingleTimeCommands(device);

			VkBufferCopy copyRegion = {};
			copyRegion.size = size;
			copyRegion.dstOffset = dstOffset;
			copyRegion.srcOffset = srcOffset;
			vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

			EndSingleTimeCommands(device, graphicsQueue, commandBuffer);
		}

		VulkanQueueFamilyIndices FindQueueFamilies(VkSurfaceKHR surface, VkPhysicalDevice device)
		{
			VulkanQueueFamilyIndices indices;

			u32 queueFamilyCount;
			vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
			assert(queueFamilyCount > 0);
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

		bool GraphicsPipelineCreateInfo::operator=(const GraphicsPipelineCreateInfo& other)
		{
			// TODO: memcmp
			return shaderID == other.shaderID &&
				vertexAttributes == other.vertexAttributes &&
				topology == other.topology &&
				cullMode == other.cullMode &&
				renderPass == other.renderPass &&
				subpass == other.subpass &&
				pushConstantRangeCount == other.pushConstantRangeCount &&
				descriptorSetLayoutIndex == other.descriptorSetLayoutIndex &&
				bSetDynamicStates == other.bSetDynamicStates &&
				bEnableColourBlending == other.bEnableColourBlending &&
				bEnableAdditiveColourBlending == other.bEnableAdditiveColourBlending &&
				depthTestEnable == other.depthTestEnable &&
				depthWriteEnable == other.depthWriteEnable &&
				depthCompareOp == other.depthCompareOp &&
				stencilTestEnable == other.stencilTestEnable;
			// TODO: Check push constant value count/types?
		}

		u64 GraphicsPipelineCreateInfo::Hash()
		{
			// NOTE: Is this hash cryptographically secure? Heck no! Does it work for my purposes? Yes it does :)
			u64 result = 0;

			result += (u64)shaderID * 11;
			result += (u64)vertexAttributes * 2;
			result += (u64)topology * 5;
			result <<= 2;
			result *= 982451653;
			result += (u64)cullMode * 3;
			result += (u64)renderPass;
			result += (u64)subpass * 5;
			result += (u64)(pushConstantRangeCount + 1) * 7;
			result += (u64)descriptorSetLayoutIndex * 13;
			result += (u64)(bSetDynamicStates ? 68 : 458);
			result += (u64)(bEnableColourBlending ? 19956 : 15485863);
			result += (u64)(bEnableAdditiveColourBlending ? 898 : 123456789);
			result += (u64)(depthTestEnable ? 77 : 2829);
			result <<= 1;
			result *= 492876847;
			result += (u64)(depthWriteEnable ? 13 : 9);
			result += (u64)depthCompareOp * 6;
			result += (u64)(stencilTestEnable ? 3 : 199);

			return result;
		}

		std::string VulkanErrorString(VkResult errorCode)
		{
			switch (errorCode)
			{
#define STR(r) case VK_ ##r: return #r
				STR(NOT_READY);
				STR(TIMEOUT);
				STR(EVENT_SET);
				STR(EVENT_RESET);
				STR(INCOMPLETE);
				STR(ERROR_OUT_OF_HOST_MEMORY);
				STR(ERROR_OUT_OF_DEVICE_MEMORY);
				STR(ERROR_INITIALIZATION_FAILED);
				STR(ERROR_DEVICE_LOST);
				STR(ERROR_MEMORY_MAP_FAILED);
				STR(ERROR_LAYER_NOT_PRESENT);
				STR(ERROR_EXTENSION_NOT_PRESENT);
				STR(ERROR_FEATURE_NOT_PRESENT);
				STR(ERROR_INCOMPATIBLE_DRIVER);
				STR(ERROR_TOO_MANY_OBJECTS);
				STR(ERROR_FORMAT_NOT_SUPPORTED);
				STR(ERROR_SURFACE_LOST_KHR);
				STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
				STR(SUBOPTIMAL_KHR);
				STR(ERROR_OUT_OF_DATE_KHR);
				STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
				STR(ERROR_VALIDATION_FAILED_EXT);
				STR(ERROR_INVALID_SHADER_NV);
				STR(ERROR_OUT_OF_POOL_MEMORY_KHR);
				STR(ERROR_INVALID_EXTERNAL_HANDLE_KHR);
#undef STR
			case VK_SUCCESS:
				// No error to print
				return "";
			case VK_RESULT_RANGE_SIZE:
			case VK_RESULT_MAX_ENUM:
			case VK_RESULT_BEGIN_RANGE:
				return "INVALID_ENUM";
			default:
				return "UNKNOWN_ERROR";
			}
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
			assert(format != VK_FORMAT_UNDEFINED);
			assert(width != 0 && height != 0);
			assert(width <= MAX_TEXTURE_DIM);
			assert(height <= MAX_TEXTURE_DIM);

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
			VK_CHECK_RESULT(vkAllocateMemory(device->m_LogicalDevice, &memAlloc, nullptr, memory));
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
				vkFreeMemory(device->m_LogicalDevice, frameBufferAttachment->mem, nullptr);
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

		void EndSingleTimeCommands(VulkanDevice* device, VkQueue graphicsQueue, VkCommandBuffer commandBuffer)
		{
			VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

			VkSubmitInfo submitInfo = vks::submitInfo(1, &commandBuffer);

			VK_CHECK_RESULT(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
			VK_CHECK_RESULT(vkQueueWaitIdle(graphicsQueue));

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
					vkFreeMemory(device->m_LogicalDevice, mem, nullptr);
				}
				if (view != VK_NULL_HANDLE)
				{
					vkDestroyImageView(device->m_LogicalDevice, view, nullptr);
				}
			}
		}

		void FrameBufferAttachment::TransitionToLayout(VkImageLayout newLayout, VkQueue graphicsQueue, VkCommandBuffer optCmdBuf /* = VK_NULL_HANDLE */)
		{
			TransitionImageLayout(device, graphicsQueue, image, format, layout, newLayout, 1, optCmdBuf, bIsDepth);
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
				vkFreeMemory(device->m_LogicalDevice, mem, nullptr);
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

#if COMPILE_SHADER_COMPILER
		std::vector<VulkanShaderCompiler::ShaderError> VulkanShaderCompiler::s_ShaderErrors;

		VulkanShaderCompiler::VulkanShaderCompiler(bool bForceRecompile)
		{
			s_ChecksumFilePathAbs = RelativePathToAbsolute(SHADER_CHECKSUM_LOCATION);

			const std::string spvDirectoryAbs = RelativePathToAbsolute(COMPILED_SHADERS_DIRECTORY);
			if (!Platform::DirectoryExists(spvDirectoryAbs))
			{
				Platform::CreateDirectoryRecursive(spvDirectoryAbs);
			}

			// Absolute file path => checksum
			// Any file in the shaders directory not in this map still need to be compiled
			std::map<std::string, u64> compiledShaders;

			s_ShaderErrors.clear();

			if (!bForceRecompile)
			{
				const char* blockName = "Calculate shader contents checksum";
				PROFILE_AUTO(blockName);

				const std::string shaderInputDirectory = SHADER_SOURCE_DIRECTORY;

				if (FileExists(SHADER_CHECKSUM_LOCATION))
				{
					std::string fileContents;
					if (ReadFile(SHADER_CHECKSUM_LOCATION, fileContents, false))
					{
						std::vector<std::string> lines = Split(fileContents, '\n');

						for (const std::string& line : lines)
						{
							size_t midPoint = line.find('=');
							if (midPoint != std::string::npos && line.length() > 7) // Ignore degenerate lines
							{
								std::string filePath = Trim(line.substr(0, midPoint));

								if (filePath.length() > 0)
								{
									std::string storedChecksumStr = Trim(line.substr(midPoint + 1));
									u64 storedChecksum = std::stoull(storedChecksumStr);

									u64 calculatedChecksum = CalculteChecksum(filePath);
									if (calculatedChecksum == storedChecksum)
									{
										compiledShaders.emplace(filePath, calculatedChecksum);
									}
								}
							}
						}
					}
				}
			}

			shaderc::Compiler compiler;
			if (compiler.IsValid())
			{
				bSuccess = true;

				std::vector<std::string> filePaths;
				if (Platform::FindFilesInDirectory(SHADER_SOURCE_DIRECTORY, filePaths, "*"))
				{
					startTime = Time::CurrentMilliseconds();

					u32 compiledShaderCount = 0;
					u32 invalidShaderCount = 0;

					class FlexIncluder : public shaderc::CompileOptions::IncluderInterface
					{
						virtual shaderc_include_result* GetInclude(const char* requested_source,
							shaderc_include_type type,
							const char* requesting_source,
							size_t include_depth) override
						{
							FLEX_UNUSED(type);
							FLEX_UNUSED(requesting_source);
							FLEX_UNUSED(include_depth);

							shaderc_include_result* result = new shaderc_include_result();

							std::string requestedFilePath = SHADER_SOURCE_DIRECTORY + std::string(requested_source);
							std::string fileContent;
							if (FileExists(requestedFilePath) && ReadFile(requestedFilePath, fileContent, false))
							{
								u32 requestedFilePathLen = (u32)requestedFilePath.size();
								result->source_name = (const char*)malloc(requestedFilePathLen + 1);
								memset((void*)result->source_name, 0, requestedFilePathLen + 1);
								strncpy((char*)result->source_name, requestedFilePath.c_str(), requestedFilePathLen);
								result->source_name_length = requestedFilePathLen;

								u32 fileContentLen = (u32)fileContent.size();
								result->content = (const char*)malloc(fileContentLen);
								memset((void*)result->content, 0, fileContentLen + 1);
								strncpy((char*)result->content, fileContent.c_str(), fileContentLen);
								result->content_length = strlen(result->content);
							}
							else
							{
								result->source_name = "";
								result->source_name_length = 0;
								result->content = "Failed to include shader";
								result->content_length = strlen(result->content);
							}

							return result;
						}

						virtual void ReleaseInclude(shaderc_include_result* data) override
						{
							// This causes heap corruption for some reason... but it's leaking without this.
							//if (data->source_name_length != 0)
							//{
							//	free((void*)data->source_name);
							//	free((void*)data->content);
							//}
							delete data;
						}
					};

					std::string newChecksumFileContents;

					for (const std::string& filePath : filePaths)
					{
						const std::string absoluteFilePath = RelativePathToAbsolute(filePath);
						std::string fileType = ExtractFileType(filePath);

						if (!Contains(s_RecognizedShaderTypes, ARRAY_SIZE(s_RecognizedShaderTypes), fileType.c_str()))
						{
							continue;
						}

						shaderc_shader_kind shaderKind = FilePathToShaderKind(fileType);
						if (shaderKind != (shaderc_shader_kind)-1)
						{
							const std::string fileName = StripLeadingDirectories(filePath);

							if (!StartsWith(fileName, "vk_"))
							{
								// TODO: EZ: Move vulkan shaders into their own directory
								continue;
							}

							if (compiledShaders.find(absoluteFilePath) != compiledShaders.end())
							{
								// Already compiled
								continue;
							}

							std::string fileContents;
							if (ReadFile(filePath, fileContents, false))
							{
								shaderc::CompileOptions options = {};
								options.SetOptimizationLevel(shaderc_optimization_level_performance);
								// TODO: Remove for release builds
								options.SetGenerateDebugInfo();
								options.SetWarningsAsErrors();

								options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
								options.SetTargetSpirv(shaderc_spirv_version_1_5);

								options.SetIncluder(std::make_unique<FlexIncluder>());

								if (bEnableAssemblyCompilation)
								{
									shaderc::AssemblyCompilationResult assemblyResult = compiler.CompileGlslToSpvAssembly(fileContents, shaderKind, fileName.c_str(), options);
									if (assemblyResult.GetCompilationStatus() == shaderc_compilation_status_success)
									{
										std::vector<char> spvBytes(assemblyResult.begin(), assemblyResult.end());
										std::string strippedFileName = StripFileType(fileName);
										std::string spvFilePath = spvDirectoryAbs + strippedFileName + "_" + fileType + ".asm";
										std::ofstream fileStream(spvFilePath, std::ios::out);
										if (fileStream.is_open())
										{
											fileStream.write((char*)spvBytes.data(), spvBytes.size() * sizeof(char));
											fileStream.close();
										}
									}
									else
									{
										if (g_bEnableLogging_Shaders)
										{
											PrintWarn("%llu shader compilation errors, %llu warnings: \n", assemblyResult.GetNumErrors(), assemblyResult.GetNumWarnings());
										}
										std::string errorStr = assemblyResult.GetErrorMessage();
										if (g_bEnableLogging_Shaders)
										{
											PrintWarn("%s\n", errorStr.c_str());
										}
									}
								}

								shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(fileContents, shaderKind, fileName.c_str(), options);
								if (result.GetCompilationStatus() == shaderc_compilation_status_success)
								{
									Print("Compiled %s\n", fileName.c_str());

									++compiledShaderCount;

									const u64 calculatedChecksum = CalculteChecksum(filePath);
									compiledShaders.emplace(absoluteFilePath, calculatedChecksum);

									std::vector<u32> spvBytes(result.begin(), result.end());
									std::string strippedFileName = StripFileType(fileName);
									std::string spvFilePath = spvDirectoryAbs + strippedFileName + "_" + fileType + ".spv";
									std::ofstream fileStream(spvFilePath, std::ios::out | std::ios::binary);
									if (fileStream.is_open())
									{
										fileStream.write((char*)spvBytes.data(), spvBytes.size() * sizeof(u32));
										fileStream.close();
									}
									else
									{
										PrintError("Failed to write compiled shader to %s\n", spvFilePath.c_str());
										++invalidShaderCount;
										bSuccess = false;
									}
								}
								else
								{
									if (g_bEnableLogging_Shaders)
									{
										PrintWarn("%llu shader compilation errors, %llu warnings: \n", result.GetNumErrors(), result.GetNumWarnings());
									}
									std::string errorStr = result.GetErrorMessage();
									if (g_bEnableLogging_Shaders)
									{
										PrintWarn("%s\n", errorStr.c_str());
									}

									size_t colon0 = errorStr.find_first_of(':');
									size_t colon1 = errorStr.find_first_of(':', colon0 + 1);

									u32 lineNumber = 0;
									if (colon0 != std::string::npos && colon1 != std::string::npos)
									{
										lineNumber = ParseInt(errorStr.substr(colon0 + 1, colon1 - colon0 - 1));
									}

									s_ShaderErrors.push_back({ errorStr, absoluteFilePath, lineNumber });

									++invalidShaderCount;
									bSuccess = false;
									auto iter = compiledShaders.find(absoluteFilePath);
									if (iter != compiledShaders.end())
									{
										compiledShaders.erase(iter);
									}
								}
							}
							else
							{
								PrintError("Failed to read shader file %s\n", fileName.c_str());
								++invalidShaderCount;
								bSuccess = false;
							}
						}
					}

					for (auto iter = compiledShaders.begin(); iter != compiledShaders.end(); ++iter)
					{
						newChecksumFileContents.append(iter->first + " = " + std::to_string(iter->second) + "\n");
					}

					if ((compiledShaderCount > 0 || invalidShaderCount > 0) && newChecksumFileContents.length() > 0)
					{
						Platform::CreateDirectoryRecursive(ExtractDirectoryString(s_ChecksumFilePathAbs).c_str());
						std::ofstream checksumFileStream(s_ChecksumFilePathAbs, std::ios::out);
						if (checksumFileStream.is_open())
						{
							checksumFileStream.write(newChecksumFileContents.c_str(), newChecksumFileContents.size());
							checksumFileStream.close();
						}
						else
						{
							PrintWarn("Failed to write shader checksum file to %s\n", SHADER_SOURCE_DIRECTORY);
						}
					}

					lastCompileDuration = Time::CurrentMilliseconds() - startTime;
					if (compiledShaderCount > 0)
					{
						Print("Compiled %u shader%s in %.1fms\n", compiledShaderCount, (compiledShaderCount > 1 ? "s" : ""), lastCompileDuration);
					}

					if (invalidShaderCount > 0)
					{
						PrintError("%u shader%s had errors\n", invalidShaderCount, (invalidShaderCount > 1 ? "s" : ""));
					}

					bComplete = true;
				}
				else
				{
					PrintError("Didn't find any shaders!\n");
					bSuccess = false;
					bComplete = true;
				}
			}
			else
			{
				PrintError("Unable to compile shaders, compiler is not valid!\n");
				bSuccess = false;
				bComplete = true;
			}
		}

		void VulkanShaderCompiler::ClearShaderHash(const std::string& shaderName)
		{
			if (FileExists(SHADER_SOURCE_DIRECTORY))
			{
				std::string fileContents;
				if (ReadFile(SHADER_SOURCE_DIRECTORY, fileContents, false))
				{
					std::string searchStr = "vk_" + shaderName + '.';
					size_t index = 0;
					do
					{
						index = fileContents.find(searchStr, index);
						if (index != std::string::npos)
						{
							size_t prevNewLine = fileContents.rfind('\n', index);
							size_t nextNewLine = fileContents.find('\n', index);
							if (prevNewLine == std::string::npos)
							{
								prevNewLine = 0;
							}
							if (nextNewLine == std::string::npos)
							{
								nextNewLine = fileContents.size() - 1;
							}
							fileContents = fileContents.substr(0, prevNewLine + 1) + fileContents.substr(nextNewLine + 1);
						}
					} while (index != std::string::npos);

					WriteFile(s_ChecksumFilePathAbs, fileContents, false);
				}
			}
		}

		void VulkanShaderCompiler::DrawImGuiShaderErrorsWindow(bool* bWindowShowing)
		{
			if (!s_ShaderErrors.empty())
			{
				if (bWindowShowing == nullptr || *bWindowShowing)
				{
					ImGui::SetNextWindowSize(ImVec2(800.0f, 150.0f), ImGuiCond_Appearing);
					if (ImGui::Begin("Shader errors", bWindowShowing))
					{
						DrawImGuiShaderErrors();
					}

					ImGui::End();
				}
			}
		}

		void VulkanShaderCompiler::DrawImGuiShaderErrors()
		{
			if (!s_ShaderErrors.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
				ImGui::Text(s_ShaderErrors.size() > 1 ? "%u errors" : "%u error", (u32)s_ShaderErrors.size());
				ImGui::PopStyleColor();

				ImGui::Separator();

				for (const ShaderError& shaderError : s_ShaderErrors)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
					ImGui::TextWrapped("%s", shaderError.errorStr.c_str());
					ImGui::PopStyleColor();

					std::string fileName = StripLeadingDirectories(shaderError.filePath);
					std::string openStr = "Open " + fileName + ":" + std::to_string(shaderError.lineNumber);
					if (ImGui::Button(openStr.c_str()))
					{
						std::string shaderEditorPath = g_EngineInstance->GetShaderEditorPath();
						if (shaderEditorPath.empty())
						{
							Platform::OpenFileWithDefaultApplication(shaderError.filePath);
						}
						else
						{
							std::string param0 = shaderError.filePath + ":" + std::to_string(shaderError.lineNumber);
							Platform::LaunchApplication(shaderEditorPath.c_str(), param0);
						}
					}

					ImGui::Separator();
				}
			}
		}

		shaderc_shader_kind VulkanShaderCompiler::FilePathToShaderKind(const std::string& fileSuffix)
		{
			if (fileSuffix.compare("vert") == 0) return shaderc_vertex_shader;
			if (fileSuffix.compare("frag") == 0) return shaderc_fragment_shader;
			if (fileSuffix.compare("geom") == 0) return shaderc_geometry_shader;
			if (fileSuffix.compare("comp") == 0) return shaderc_compute_shader;

			return (shaderc_shader_kind)-1;
		}

		u64 VulkanShaderCompiler::CalculteChecksum(const std::string& filePath)
		{
			u64 checksum = 0;

			const std::string fileName = StripLeadingDirectories(filePath);
			const std::string fileSuffix = ExtractFileType(filePath);
			bool bGoodStart = StartsWith(fileName, "vk_");
			bool bGoodEnd = Contains(s_RecognizedShaderTypes, ARRAY_SIZE(s_RecognizedShaderTypes), fileSuffix.c_str());
			if (bGoodStart && bGoodEnd)
			{
				std::string fileContents;
				if (FileExists(filePath) && ReadFile(filePath, fileContents, false))
				{
					const char* includeString = "include ";

					u32 i = 1;
					u32 fileContentsLen = (u32)fileContents.size();
					for (char c : fileContents)
					{
						if (c == '#' && i < (fileContentsLen + strlen(includeString)))
						{
							if (memcmp((void*)&fileContents[i], (void*)includeString, strlen(includeString)) == 0)
							{
								// Handle include
								bool bFoundInclude = false;

								size_t newLine = fileContents.find('\n', i);
								if (newLine != std::string::npos)
								{
									u32 includePathStart = i + (u32)strlen(includeString);
									std::string includePathRaw = fileContents.substr(includePathStart, newLine - includePathStart);
									includePathRaw = Trim(includePathRaw);

									if (includePathRaw.size() >= 3)
									{
										std::string includedPath;
										if (includePathRaw[0] == '"')
										{
											size_t endQuote = includePathRaw.find('"', 1);
											if (endQuote != std::string::npos)
											{
												includedPath = SHADER_SOURCE_DIRECTORY + includePathRaw.substr(1, endQuote - 1);
											}
										}
										else if (includePathRaw[0] == '<')
										{
											size_t endBracket = includePathRaw.find('>', 1);
											if (endBracket != std::string::npos)
											{
												includedPath = SHADER_SOURCE_DIRECTORY + includePathRaw.substr(1, endBracket - 1);
											}
										}

										if (!includedPath.empty() && FileExists(includedPath))
										{
											bFoundInclude = true;
											checksum += CalculteChecksum(includedPath);
										}
									}
								}

								if (!bFoundInclude)
								{
									PrintWarn("Invalid include directive in shader, ignoring.\n");
								}
							}
						}

						checksum += (u64)i * (u64)c;
						++i;
					}
				}
			}
			return checksum;
		}

		bool VulkanShaderCompiler::TickStatus()
		{
			// TODO: Make async again
			//sec now = Time::CurrentSeconds();
			//secSinceStatusCheck += (now - lastTime);
			//totalSecWaiting += (now - lastTime);
			//lastTime = now;
			//if (secSinceStatusCheck > secBetweenStatusChecks)
			//{
			//	secSinceStatusCheck -= secBetweenStatusChecks;

			//	if (is_done)
			//	{
			//		bComplete = true;
			//		if (taskThread.joinable())
			//		{
			//			Print("Vulkan async shader compilation completed after %.2fs\n", totalSecWaiting);
			//			taskThread.join();
			//		}

			//		if (bSuccess)
			//		{
			//			std::string fileContents = std::to_string(m_ShaderCodeChecksum);
			//			if (!WriteFile(m_ChecksumFilePath, fileContents, false))
			//			{
			//				PrintError("Failed to write out shader checksum file to %s\n", m_ChecksumFilePath.c_str());
			//			}
			//		}
			//	}
			//}
			//
			//return bComplete;
			return true;
		}
#endif // COMPILE_SHADER_COMPILER

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

		UniformBufferList::UniformBufferList()
		{
			// Every instance will have at least one type
			uniformBufferList.reserve(1);
		}

		void UniformBufferList::Add(VulkanDevice* device, UniformBufferType type)
		{
			uniformBufferList.emplace_back(device, type);
		}

		const UniformBuffer* UniformBufferList::Get(UniformBufferType type) const
		{
			for (const UniformBuffer& buffer : uniformBufferList)
			{
				if (buffer.type == type)
				{
					return &buffer;
				}
			}
			return nullptr;
		}

		bool UniformBufferList::Has(UniformBufferType type) const
		{
			for (const UniformBuffer& buffer : uniformBufferList)
			{
				if (buffer.type == type)
				{
					return true;
				}
			}
			return false;
		}

		UniformBuffer* UniformBufferList::Get(UniformBufferType type)
		{
			for (UniformBuffer& buffer : uniformBufferList)
			{
				if (buffer.type == type)
				{
					return &buffer;
				}
			}
			return nullptr;
		}

		VulkanParticleSystem::VulkanParticleSystem(VulkanDevice* device) :
			computePipeline(device->m_LogicalDevice, vkDestroyPipeline)
		{
		}

		GraphicsPipeline::GraphicsPipeline()
		{
		}

		GraphicsPipeline::GraphicsPipeline(const VDeleter<VkDevice>& vulkanDevice) :
			pipeline(vulkanDevice, vkDestroyPipeline),
			layout(vulkanDevice, vkDestroyPipelineLayout)
		{
		}

		void GraphicsPipeline::replace()
		{
			pipeline.replace();
			layout.replace();
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

		VulkanDescriptorPool::VulkanDescriptorPool(VulkanDevice* device) :
			device(device),
			size(maxNumDescSets)
		{
			std::vector<VkDescriptorPoolSize> poolSizes
			{
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_NUM_DESC_COMBINED_IMAGE_SAMPLERS },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_NUM_DESC_UNIFORM_BUFFERS },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, MAX_NUM_DESC_DYNAMIC_UNIFORM_BUFFERS },
				{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, MAX_NUM_DESC_DYNAMIC_STORAGE_BUFFERS },
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
			VkDescriptorSetLayout layouts[] = { *createInfo->descriptorSetLayout };
			VkDescriptorSetAllocateInfo allocInfo = vks::descriptorSetAllocateInfo(pool, layouts, 1);

			if ((allocatedSetCount + 1) > maxNumDescSets)
			{
				// TODO: Create new pool or recreate and copy old one?
				//maxNumDescSets *= 2;
				assert(false);
			}

			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
			// TODO: Optimization: Allocate all required descriptor sets in one call rather than 1 at a time
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device->m_LogicalDevice, &allocInfo, &descriptorSet));

			++allocatedSetCount;

			Shader* shader = g_Renderer->GetShader(createInfo->shaderID);

			Uniforms constantBufferUniforms = shader->constantBufferUniforms;
			Uniforms dynamicBufferUniforms = shader->dynamicBufferUniforms;
			Uniforms additionalBufferUniforms = shader->additionalBufferUniforms;
			Uniforms textureUniforms = shader->textureUniforms;

			std::vector<VkWriteDescriptorSet> writeDescriptorSets;
			writeDescriptorSets.reserve(createInfo->bufferDescriptors.Count() + createInfo->imageDescriptors.Count());

			u32 binding = 0;

			std::vector<VkDescriptorBufferInfo> bufferInfos(createInfo->bufferDescriptors.Count());
			u32 i = 0;
			for (auto& pair : createInfo->bufferDescriptors)
			{
				const u64 uniformID = pair.uniformID;
				const BufferDescriptorInfo& bufferDescInfo = pair.object;
				assert((bufferDescInfo.type == UniformBufferType::DYNAMIC && dynamicBufferUniforms.HasUniform(uniformID)) ||
					(bufferDescInfo.type == UniformBufferType::PARTICLE_DATA && additionalBufferUniforms.HasUniform(uniformID)) ||
					(bufferDescInfo.type == UniformBufferType::STATIC && constantBufferUniforms.HasUniform(uniformID)));
				assert(bufferDescInfo.buffer != VK_NULL_HANDLE);

				VkDescriptorType type;
				switch (bufferDescInfo.type)
				{
				case UniformBufferType::STATIC:
					type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
					break;
				case UniformBufferType::DYNAMIC:
					type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
					break;
				case UniformBufferType::PARTICLE_DATA:
					type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
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
			for (auto& pair : createInfo->imageDescriptors)
			{
				const u64 uniformID = pair.uniformID;
				assert(textureUniforms.HasUniform(uniformID));
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
						imageInfo.sampler = ((VulkanRenderer*)g_Renderer)->m_LinMipLinSampler;
					}
				}
				else
				{
					VulkanTexture* blankTexture = ((VulkanTexture*)g_Renderer->m_BlankTexture);
					imageInfo.imageView = blankTexture->imageView;
					imageInfo.sampler = blankTexture->sampler;
				}
				writeDescriptorSets.push_back(vks::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, binding, &imageInfo));

				++binding;
				++i;
			}

			if (!writeDescriptorSets.empty())
			{
				vkUpdateDescriptorSets(device->m_LogicalDevice, (u32)writeDescriptorSets.size(), writeDescriptorSets.data(), 0u, nullptr);
			}

			if (createInfo->DBG_Name != nullptr)
			{
				((VulkanRenderer*)g_Renderer)->SetDescriptorSetName(device, descriptorSet, createInfo->DBG_Name);
			}

			return descriptorSet;
		}

		void VulkanDescriptorPool::CreateDescriptorSet(MaterialID materialID, const char* DBG_Name /* = nullptr */)
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

			createInfo.descriptorSetLayout = &descriptorSetLayouts[material->shaderID];
			createInfo.shaderID = material->shaderID;
			createInfo.uniformBufferList = &material->uniformBufferList;

			((VulkanRenderer*)g_Renderer)->FillOutImageDescriptorInfos(&createInfo.imageDescriptors, materialID);
			((VulkanRenderer*)g_Renderer)->FillOutBufferDescriptorInfos(&createInfo.bufferDescriptors, createInfo.uniformBufferList, createInfo.shaderID);

			VkDescriptorSet descriptorSet = CreateDescriptorSet(&createInfo);
			if (descriptorSets.size() <= materialID)
			{
				descriptorSets.resize(materialID + 1);
			}
			descriptorSets[materialID] = descriptorSet;
		}

		void VulkanDescriptorPool::CreateDescriptorSetLayout(ShaderID shaderID)
		{
			descriptorSetLayouts.push_back(VkDescriptorSetLayout());
			VkDescriptorSetLayout* descriptorSetLayout = &descriptorSetLayouts.back();

			VulkanShader* shader = (VulkanShader*)g_Renderer->GetShader(shaderID);

			struct DescriptorSetInfo
			{
				u64 uniform;
				VkDescriptorType descriptorType;
				VkShaderStageFlags shaderStageFlags;
			};

			// TODO: Specify stage flags per shader
			static DescriptorSetInfo descriptorSetInfos[] = {
				{ U_UNIFORM_BUFFER_CONSTANT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_GEOMETRY_BIT },

				{ U_UNIFORM_BUFFER_DYNAMIC, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_COMPUTE_BIT },

				{ U_ALBEDO_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_EMISSIVE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_METALLIC_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_ROUGHNESS_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_NORMAL_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_HDR_EQUIRECTANGULAR_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_CUBEMAP_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_BRDF_LUT_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_IRRADIANCE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_PREFILTER_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_FB_0_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_FB_1_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_LTC_SAMPLERS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_LTC_SAMPLERS + 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_HIGH_RES_TEX, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_DEPTH_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_SSAO_NORMAL_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_NOISE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_SSAO_RAW_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_SSAO_FINAL_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_SHADOW_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_SCENE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_HISTORY_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_PARTICLE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
				VK_SHADER_STAGE_COMPUTE_BIT },
			};

			std::vector<VkDescriptorSetLayoutBinding> bindings;

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
				}
			}

			VkDescriptorSetLayoutCreateInfo layoutInfo = vks::descriptorSetLayoutCreateInfo(bindings);

			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->m_LogicalDevice, &layoutInfo, nullptr, descriptorSetLayout));
		}

		void VulkanDescriptorPool::Replace()
		{
			for (const VkDescriptorSetLayout& descriptorSetLayout : descriptorSetLayouts)
			{
				vkDestroyDescriptorSetLayout(device->m_LogicalDevice, descriptorSetLayout, nullptr);
			}
			descriptorSetLayouts.clear();

			descriptorSets.clear();

			vkDestroyDescriptorPool(device->m_LogicalDevice, pool, nullptr);
			pool = VK_NULL_HANDLE;

			allocatedSetCount = 0;
		}

		void VulkanDescriptorPool::Reset()
		{
			for (const VkDescriptorSetLayout& descriptorSetLayout : descriptorSetLayouts)
			{
				vkDestroyDescriptorSetLayout(device->m_LogicalDevice, descriptorSetLayout, nullptr);
			}
			descriptorSetLayouts.clear();
			descriptorSets.clear();

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
				PrintError("Didn't find descriptor set in VulkanDescriptorPool::FreeSet: %llu\n", (u64)descSet);
			}

			vkFreeDescriptorSets(device->m_LogicalDevice, pool, 1, &descSet);
		}
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN