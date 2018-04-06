#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanHelpers.hpp"

#pragma warning(push, 0) // Don't generate warnings for third party code
#include "stb_image.h"
#pragma warning(pop)

#include "Logger.hpp"
#include "VertexAttribute.hpp"
#include "VertexBufferData.hpp"
#include "VertexAttribute.hpp"
#include "Helpers.hpp"
#include "Graphics/Vulkan/VulkanDevice.hpp"
#include "Graphics/Vulkan/VulkanCommandBufferManager.hpp"

namespace flex
{
	namespace vk
	{
		void VK_CHECK_RESULT(VkResult result)
		{
			if (result != VK_SUCCESS)
			{
				VkErrorSS << "Vulkan fatal error: VkResult is \"" << VulkanErrorString(result) << std::endl;
				Logger::LogError(VkErrorSS.str());
				VkErrorSS.clear();
				assert(result == VK_SUCCESS);
			}
		}

		VkVertexInputBindingDescription GetVertexBindingDescription(u32 vertexStride)
		{
			VkVertexInputBindingDescription bindingDesc = {};
			bindingDesc.binding = 0;
			bindingDesc.stride = vertexStride;
			bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			return bindingDesc;
		}

		void GetVertexAttributeDescriptions(VertexAttributes vertexAttributes,
			std::vector<VkVertexInputAttributeDescription>& attributeDescriptions)
		{
			attributeDescriptions.clear();

			u32 offset = 0;
			u32 location = 0;

			// TODO: Roll i32o iteration over array

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

			if (vertexAttributes & (u32)VertexAttribute::POSITION_2D)
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

			if (vertexAttributes & (u32)VertexAttribute::UVW)
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
			if (vertexAttributes & (u32)VertexAttribute::COLOR_R8G8B8A8_UNORM)
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

			if (vertexAttributes & (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT)
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

			if (vertexAttributes & (u32)VertexAttribute::BITANGENT)
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
		}

		UniformBuffer::UniformBuffer(const VDeleter<VkDevice>& device) :
			constantBuffer(device),
			constantData{},
			dynamicBuffer(device),
			dynamicData{}
		{
		}

		UniformBuffer::~UniformBuffer()
		{
			if (constantData.data)
			{
				free(constantData.data);
				constantData.data = nullptr;
			}

			if (dynamicData.data)
			{
				_aligned_free(dynamicData.data);
				dynamicData.data = nullptr;
			}
		}

		VulkanTexture::VulkanTexture(VulkanDevice* device, VkQueue graphicsQueue) :
			m_VulkanDevice(device),
			m_GraphicsQueue(graphicsQueue),
			image(device->m_LogicalDevice, vkDestroyImage),
			imageMemory(device->m_LogicalDevice, vkFreeMemory),
			imageView(device->m_LogicalDevice, vkDestroyImageView),
			sampler(device->m_LogicalDevice, vkDestroySampler)
		{
			UpdateImageDescriptor();
		}

		void VulkanTexture::UpdateImageDescriptor()
		{
			imageInfoDescriptor.imageLayout = imageLayout;
			imageInfoDescriptor.imageView = imageView;
			imageInfoDescriptor.sampler = sampler;
		}

		void VulkanTexture::Create(ImageCreateInfo& imageCreateInfo, ImageViewCreateInfo& imageViewCreateInfo, SamplerCreateInfo& samplerCreateInfo)
		{
			CreateImage(m_VulkanDevice, m_GraphicsQueue, imageCreateInfo);
			CreateImageView(m_VulkanDevice, imageViewCreateInfo);
			CreateSampler(m_VulkanDevice, samplerCreateInfo);
		}

		VkDeviceSize VulkanTexture::CreateEmpty(VkFormat format, u32 width, u32 height, u32 mipLevels, VkImageUsageFlags usage)
		{
			ImageCreateInfo imageCreateInfo = {};
			imageCreateInfo.image = image.replace();
			imageCreateInfo.imageMemory = imageMemory.replace();
			imageCreateInfo.format = format;
			imageCreateInfo.width = width;
			imageCreateInfo.height = height;
			imageCreateInfo.mipLevels = mipLevels;
			imageCreateInfo.usage = usage;
			imageCreateInfo.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

			VkDeviceSize imageSize = CreateImage(m_VulkanDevice, m_GraphicsQueue, imageCreateInfo);

			ImageViewCreateInfo imageViewCreateInfo = {};
			imageViewCreateInfo.image = &image;
			imageViewCreateInfo.imageView = &imageView;
			imageViewCreateInfo.format = format;
			imageViewCreateInfo.mipLevels = mipLevels;
			CreateImageView(m_VulkanDevice, imageViewCreateInfo);

			SamplerCreateInfo samplerCreateInfo = {};
			samplerCreateInfo.sampler = &sampler;
			CreateSampler(m_VulkanDevice, samplerCreateInfo);

			return imageSize;
		}
		
		VkDeviceSize VulkanTexture::CreateCubemap(VulkanDevice* device, VkQueue graphicsQueue, CubemapCreateInfo& createInfo)
		{
			if (createInfo.width == 0 ||
				createInfo.height== 0 ||
				createInfo.channels == 0 ||
				createInfo.mipLevels == 0)
			{
				Logger::LogError("Cubemap create info missing required size data");
				return 0;
			}

			const u32 calculatedMipLevels = createInfo.generateMipMaps ? static_cast<u32>(floor(log2(std::min(createInfo.width, createInfo.height)))) + 1 : 0;

			VkImageCreateInfo imageCreateInfo = {};
			imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = createInfo.format;
			imageCreateInfo.mipLevels = createInfo.mipLevels;
			imageCreateInfo.arrayLayers = 6;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.extent = { (u32)createInfo.width, (u32)createInfo.height, 1u };
			//imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT; // TODO: ?
			imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

			VK_CHECK_RESULT(vkCreateImage(device->m_LogicalDevice, &imageCreateInfo, nullptr, createInfo.image));

			VkMemoryRequirements memReqs;
			vkGetImageMemoryRequirements(device->m_LogicalDevice, *createInfo.image, &memReqs);

			VkMemoryAllocateInfo memAllocInfo = {};
			memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = device->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			VK_CHECK_RESULT(vkAllocateMemory(device->m_LogicalDevice, &memAllocInfo, nullptr, createInfo.imageMemory));
			VK_CHECK_RESULT(vkBindImageMemory(device->m_LogicalDevice, *createInfo.image, *createInfo.imageMemory, 0));

			VkSamplerCreateInfo samplerCreateInfo = {};
			samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
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
			if (device->m_PhysicalDeviceFeatures.samplerAnisotropy)
			{
				samplerCreateInfo.maxAnisotropy = device->m_PhysicalDeviceProperties.limits.maxSamplerAnisotropy;
				samplerCreateInfo.anisotropyEnable = VK_TRUE;
			}

			VK_CHECK_RESULT(vkCreateSampler(device->m_LogicalDevice, &samplerCreateInfo, nullptr, createInfo.sampler));

			VkImageViewCreateInfo imageViewCreateInfo = {};
			imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
			imageViewCreateInfo.format = createInfo.format;
			imageViewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
			// TODO: Depth!!
			imageViewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			imageViewCreateInfo.subresourceRange.layerCount = 6;
			imageViewCreateInfo.subresourceRange.levelCount = createInfo.mipLevels;
			imageViewCreateInfo.image = *createInfo.image;
			imageViewCreateInfo.flags = 0;
			VK_CHECK_RESULT(vkCreateImageView(device->m_LogicalDevice, &imageViewCreateInfo, nullptr, createInfo.imageView));
		}

		VkDeviceSize VulkanTexture::CreateCubemapEmpty(VkFormat format, u32 width, u32 height, u32 channels, u32 mipLevels, bool enableTrilinearFiltering)
		{
			width = width;
			height = height;
			channelCount = channels;

			CubemapCreateInfo createInfo = {};
			createInfo.image = &image;
			createInfo.imageMemory = &imageMemory;
			createInfo.imageView = &imageView;
			createInfo.sampler = &sampler;

			createInfo.format = format;
			createInfo.width = width;
			createInfo.height = height;
			createInfo.channels = channels;
			createInfo.totalSize = width * height * channels * 6;
			createInfo.mipLevels = mipLevels;
			createInfo.enableTrilinearFiltering = enableTrilinearFiltering;

			VkDeviceSize imageSize = CreateCubemap(m_VulkanDevice, m_GraphicsQueue, createInfo);

			// Retrieve out variables
			imageLayout = createInfo.imageLayoutOut;

			return imageSize;
		}

		VkDeviceSize VulkanTexture::CreateCubemapFromTextures(VkFormat format, const std::array<std::string, 6>& filePaths, bool enableTrilinearFiltering)
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

			std::string fileName = filePaths[0];
			if (fileName.empty())
			{
				Logger::LogError("CreateCubemapFromTextures was given an empty filepath!");
				return 0;
			}

			StripLeadingDirectories(fileName);
			Logger::LogInfo("Loading cubemap textures " + filePaths[0] + " , " + filePaths[1] + " , " + filePaths[2] + " , " + filePaths[3] + " , " + filePaths[4] + " , " + filePaths[5]);

			// TODO: Handle hdr textures!!! FIXME
			images.reserve(filePaths.size());
			for (const std::string& filePath : filePaths)
			{
				int w, h, c;
				unsigned char* pixels = stbi_load(filePath.c_str(), &w, &h, &c, STBI_rgb_alpha);
				if (!pixels)
				{
					const char* failureReasonStr = stbi_failure_reason();
					Logger::LogError("CreateCubemapFromTextures failed to load image" + filePath + ", failure reason: " + std::string(failureReasonStr));
					return 0;
				}
				width = (u32)w;
				height = (u32)h;
				// stbi_load returns the original channel count of the image, 
				// it was forced to have 4 channels because we passed STBI_rgb_alpha
				// TODO: Investigate 3 channel cubemaps
				c = 4;
				channelCount = (u32)c;

				i32 size = width * height * channelCount * sizeof(unsigned char);
				images.push_back({ pixels, (i32)width, (i32)height, (i32)channelCount, size });
				totalSize += size;
			}

			if (totalSize == 0)
			{
				Logger::LogError("CreateCubemapFromTextures failed to load cubemap textures (" + fileName + ")");
				return 0;
			}

			unsigned char* pixels = (unsigned char*)malloc(totalSize);
			if (pixels == nullptr)
			{
				Logger::LogError("CreateCubemapFromTextures Failed to allocate " + std::to_string(totalSize) + " bytes");
				return 0;
			}

			unsigned char* pixelData = pixels;
			for (Image& image : images)
			{
				memcpy(pixelData, image.pixels, image.size);
				pixelData += (image.size / sizeof(unsigned char));
				stbi_image_free(image.pixels);
			}


			// Create a host-visible staging buffer that contains the raw image data
			VulkanBuffer stagingBuffer(m_VulkanDevice->m_LogicalDevice);

			VkBufferCreateInfo bufferCreateInfo = {};
			bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferCreateInfo.size = totalSize;
			bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;


			VK_CHECK_RESULT(vkCreateBuffer(m_VulkanDevice->m_LogicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer.m_Buffer));

			VkMemoryRequirements memReqs;
			vkGetBufferMemoryRequirements(m_VulkanDevice->m_LogicalDevice, stagingBuffer.m_Buffer, &memReqs);

			VkMemoryAllocateInfo memAllocInfo = {};
			memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = m_VulkanDevice->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			VK_CHECK_RESULT(vkAllocateMemory(m_VulkanDevice->m_LogicalDevice, &memAllocInfo, nullptr, &stagingBuffer.m_Memory));
			stagingBuffer.Bind();

			stagingBuffer.Map(memReqs.size);
			memcpy(stagingBuffer.m_Mapped, pixels, totalSize);
			free(pixels);
			stagingBuffer.Unmap();


			CubemapCreateInfo createInfo = {};
			createInfo.image = &image;
			createInfo.imageMemory = &imageMemory;
			createInfo.imageView = &imageView;
			createInfo.sampler = &sampler;
			createInfo.totalSize = totalSize;

			createInfo.format = format;
			createInfo.filePaths = filePaths;
			createInfo.enableTrilinearFiltering = enableTrilinearFiltering;

			VkDeviceSize imageSize = CreateCubemap(m_VulkanDevice, m_GraphicsQueue, createInfo);


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
			VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			createInfo.imageLayoutOut = imageLayout;
			SetImageLayout(
				copyCmd,
				*createInfo.image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				imageLayout,
				subresourceRange);

			VulkanCommandBufferManager::FlushCommandBuffer(m_VulkanDevice, copyCmd, m_GraphicsQueue, true);


			// Retrieve out variables
			imageLayout = createInfo.imageLayoutOut;

			return imageSize;
		}

		VkDeviceSize VulkanTexture::CreateImage(VulkanDevice* device, VkQueue graphicsQueue, ImageCreateInfo& createInfo)
		{
			if (createInfo.width > Renderer::MAX_TEXTURE_DIM ||
				createInfo.height > Renderer::MAX_TEXTURE_DIM ||
				createInfo.width == 0 ||
				createInfo.height == 0)
			{
				Logger::LogError("Invalid dimensions passed into CreateImage: " + std::to_string(createInfo.width) + "x" + std::to_string(createInfo.height));
				return 0;
			}

			VkImageCreateInfo imageInfo = {};
			imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
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

			VkResult result = vkGetPhysicalDeviceImageFormatProperties(device->m_PhysicalDevice, imageInfo.format, imageInfo.imageType, imageInfo.tiling, imageInfo.usage, imageInfo.flags, &formatProperties);
			if (result != VK_SUCCESS)
			{
				// TODO: Handle error gracefully
				Logger::LogError("Invalid image format!");
			}

			VK_CHECK_RESULT(vkCreateImage(device->m_LogicalDevice, &imageInfo, nullptr, createInfo.image));

			VkMemoryRequirements memRequirements;
			vkGetImageMemoryRequirements(device->m_LogicalDevice, *createInfo.image, &memRequirements);

			VkMemoryAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memRequirements.size;
			allocInfo.memoryTypeIndex = FindMemoryType(device, memRequirements.memoryTypeBits, createInfo.properties);

			VK_CHECK_RESULT(vkAllocateMemory(device->m_LogicalDevice, &allocInfo, nullptr, createInfo.imageMemory));

			VK_CHECK_RESULT(vkBindImageMemory(device->m_LogicalDevice, *createInfo.image, *createInfo.imageMemory, 0));

			return memRequirements.size;
		}

		VkDeviceSize VulkanTexture::CreateFromTexture(const std::string& filePath, VkFormat format, bool hdr, u32 mipLevels)
		{
			VkDeviceSize textureSize = 0;

			VulkanBuffer stagingBuffer(m_VulkanDevice->m_LogicalDevice);

			if (hdr)
			{
				HDRImage hdrImage = {};
				if (!hdrImage.Load(filePath, false))
				{
					const char* failureReasonStr = stbi_failure_reason();
					Logger::LogError("Couldn't load HDR image, failure reason: " + std::string(failureReasonStr) + " filepath: " + filePath);
					return 0;
				}

				width = hdrImage.width;
				height = hdrImage.height;
				channelCount = hdrImage.channelCount;
				textureSize = (VkDeviceSize)(width * height * channelCount * sizeof(float));


				CreateAndAllocateBuffer(m_VulkanDevice, textureSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer);

				void* data = nullptr;
				vkMapMemory(m_VulkanDevice->m_LogicalDevice, stagingBuffer.m_Memory, 0, textureSize, 0, &data);
				memcpy(data, hdrImage.pixels, (size_t)textureSize);
				vkUnmapMemory(m_VulkanDevice->m_LogicalDevice, stagingBuffer.m_Memory);

				hdrImage.Free();
			}
			else
			{
				std::string fileName = filePath;
				StripLeadingDirectories(fileName);
				Logger::LogInfo("Loading texture " + fileName);

				int w, h, c;
				unsigned char* pixels = stbi_load(filePath.c_str(), &w, &h, &c, STBI_rgb_alpha);
				width = (u32)w;
				height = (u32)h;
				channelCount = (u32)c;

				if (!pixels)
				{
					const char* failureReasonStr = stbi_failure_reason();
					Logger::LogError("Couldn't load image, failure reason: " + std::string(failureReasonStr) + " filepath: " + filePath);
					return 0;
				}

				channelCount = 4;

				textureSize = (VkDeviceSize)(width * height * channelCount * sizeof(unsigned char));

				CreateAndAllocateBuffer(m_VulkanDevice, textureSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer);

				void* data = nullptr;
				vkMapMemory(m_VulkanDevice->m_LogicalDevice, stagingBuffer.m_Memory, 0, textureSize, 0, &data);
				memcpy(data, pixels, (size_t)textureSize);
				vkUnmapMemory(m_VulkanDevice->m_LogicalDevice, stagingBuffer.m_Memory);

				stbi_image_free(pixels);
			}

			if (width == 0 ||
				height == 0 ||
				channelCount == 0 ||
				textureSize == 0)
			{
				Logger::LogError("Failed to load in texture data from " + filePath);
				return 0;
			}

			ImageCreateInfo imageCreateInfo = {};
			imageCreateInfo.image = image.replace();
			imageCreateInfo.imageMemory = imageMemory.replace();
			imageCreateInfo.format = format;
			imageCreateInfo.width  = (u32)width;
			imageCreateInfo.height  = (u32)height;
			imageCreateInfo.imageType  = VK_IMAGE_TYPE_2D;
			imageCreateInfo.tiling  = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.usage  = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			imageCreateInfo.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

			u32 imageSize = CreateImage(m_VulkanDevice, m_GraphicsQueue, imageCreateInfo);

			TransitionImageLayout(m_VulkanDevice, m_GraphicsQueue, image, format, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);
			CopyBufferToImage(m_VulkanDevice, m_GraphicsQueue, stagingBuffer.m_Buffer, image, width, height);
			TransitionImageLayout(m_VulkanDevice, m_GraphicsQueue, image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels);

			ImageViewCreateInfo viewCreateInfo = {};
			viewCreateInfo.format = format;
			viewCreateInfo.image = &image;
			viewCreateInfo.imageView = &imageView;
			viewCreateInfo.mipLevels = mipLevels;
			CreateImageView(m_VulkanDevice, viewCreateInfo);

			SamplerCreateInfo samplerCreateInfo = {};
			samplerCreateInfo.sampler = &sampler;
			CreateSampler(m_VulkanDevice, samplerCreateInfo);

			return imageSize;
		}

		void VulkanTexture::CreateImageView(VulkanDevice* device, ImageViewCreateInfo& createInfo)
		{
			VkImageViewCreateInfo viewInfo = {};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
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
		}

		void VulkanTexture::CreateSampler(VulkanDevice* device, SamplerCreateInfo& createInfo)
		{
			VkSamplerCreateInfo samplerInfo = {};
			samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.magFilter = createInfo.magFilter;
			samplerInfo.minFilter = createInfo.minFilter;
			samplerInfo.addressModeU = createInfo.samplerAddressMode;
			samplerInfo.addressModeV = createInfo.samplerAddressMode;
			samplerInfo.addressModeW = createInfo.samplerAddressMode;
			samplerInfo.anisotropyEnable = VK_TRUE;
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

			throw std::runtime_error("failed to find supported formats!");
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

			throw std::runtime_error("Failed to find any suitable memory type!");
		}

		void TransitionImageLayout(VulkanDevice* device, VkQueue graphicsQueue, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, u32 mipLevels)
		{
			VkCommandBuffer commandBuffer = BeginSingleTimeCommands(device);

			VkImageMemoryBarrier barrier = {};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = oldLayout;
			barrier.newLayout = newLayout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image;

			if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
			{
				barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

				if (HasStencilComponent(format))
				{
					barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
				}
			}
			else
			{
				barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			}

			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = mipLevels;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;

			if (oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
			{
				barrier.srcAccessMask = 0;
				barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			}
			else
			{
				throw std::invalid_argument("unsupported layout transition!");
			}

			vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &barrier);

			EndSingleTimeCommands(device, graphicsQueue, commandBuffer);
		}

		void CopyImage(VulkanDevice* device, VkQueue graphicsQueue, VkImage srcImage, VkImage dstImage, u32 width, u32 height)
		{
			VkCommandBuffer commandBuffer = BeginSingleTimeCommands(device);

			VkImageSubresourceLayers subresource = {};
			subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
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

			EndSingleTimeCommands(device, graphicsQueue, commandBuffer);
		}

		void CopyBufferToImage(VulkanDevice* device, VkQueue graphicsQueue, VkBuffer buffer, VkImage image, u32 width, u32 height)
		{
			VkCommandBuffer commandBuffer = BeginSingleTimeCommands(device);

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

			EndSingleTimeCommands(device, graphicsQueue, commandBuffer);
		}

		void CreateAndAllocateBuffer(VulkanDevice* device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VulkanBuffer* buffer)
		{
			VkBufferCreateInfo bufferInfo = {};
			bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferInfo.size = size;
			bufferInfo.usage = usage;
			bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VK_CHECK_RESULT(vkCreateBuffer(device->m_LogicalDevice, &bufferInfo, nullptr, buffer->m_Buffer.replace()));

			VkMemoryRequirements memRequirements;
			vkGetBufferMemoryRequirements(device->m_LogicalDevice, buffer->m_Buffer, &memRequirements);

			VkMemoryAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memRequirements.size;
			allocInfo.memoryTypeIndex = FindMemoryType(device, memRequirements.memoryTypeBits, properties);

			VK_CHECK_RESULT(vkAllocateMemory(device->m_LogicalDevice, &allocInfo, nullptr, buffer->m_Memory.replace()));

			// Create the memory backing up the buffer handle
			buffer->m_Alignment = memRequirements.alignment;
			buffer->m_Size = allocInfo.allocationSize;
			buffer->m_UsageFlags = usage;
			buffer->m_MemoryPropertyFlags = properties;

			// Initialize a default descriptor that covers the whole buffer size
			buffer->m_DescriptorInfo.offset = 0;
			buffer->m_DescriptorInfo.range = VK_WHOLE_SIZE;
			buffer->m_DescriptorInfo.buffer = buffer->m_Buffer;

			vkBindBufferMemory(device->m_LogicalDevice, buffer->m_Buffer, buffer->m_Memory, 0);
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

				if (indices.IsComplete())
				{
					break;
				}

				++i;
			}

			return indices;
		}

		VulkanRenderObject::VulkanRenderObject(const VDeleter<VkDevice>& device, RenderID renderID) :
			pipelineLayout(device, vkDestroyPipelineLayout),
			graphicsPipeline(device, vkDestroyPipeline),
			renderID(renderID)
		{
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
				// No error to pri32
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
			VkImageMemoryBarrier imageMemoryBarrier = {};
			imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
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
				// Image is a color attachment
				// Make sure any writes to the color buffer have been finished
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
				// Image will be used as a color attachment
				// Make sure any writes to the color buffer have been finished
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

		void CreateAttachment(
			VulkanDevice* device,
			VkFormat format,
			VkImageUsageFlagBits usage,
			u32 width,
			u32 height,
			u32 arrayLayers,
			VkImageViewType imageViewType,
			VkImageCreateFlags imageFlags,
			FrameBufferAttachment *attachment)
		{
			VkImageAspectFlags aspectMask = 0;
			VkImageLayout imageLayout;

			attachment->format = format;

			if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
			{
				aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			}
			if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
			{
				aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
				imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			}

			assert(aspectMask > 0);
			assert(width <= Renderer::MAX_TEXTURE_DIM);
			assert(height <= Renderer::MAX_TEXTURE_DIM);

			VkImageCreateInfo imageCreateInfo = {};
			imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = format;
			imageCreateInfo.extent.width = width;
			imageCreateInfo.extent.height = height;
			imageCreateInfo.extent.depth = 1;
			imageCreateInfo.mipLevels = 1;
			imageCreateInfo.arrayLayers = arrayLayers;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT;
			imageCreateInfo.flags = imageFlags;

			VkMemoryAllocateInfo memAlloc = {};
			memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			VkMemoryRequirements memReqs;

			VK_CHECK_RESULT(vkCreateImage(device->m_LogicalDevice, &imageCreateInfo, nullptr, attachment->image.replace()));
			vkGetImageMemoryRequirements(device->m_LogicalDevice, attachment->image, &memReqs);
			memAlloc.allocationSize = memReqs.size;
			memAlloc.memoryTypeIndex = device->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(device->m_LogicalDevice, &memAlloc, nullptr, attachment->mem.replace()));
			VK_CHECK_RESULT(vkBindImageMemory(device->m_LogicalDevice, attachment->image, attachment->mem, 0));

			VkImageViewCreateInfo imageView = {};
			imageView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			imageView.viewType = imageViewType;
			imageView.format = format;
			imageView.subresourceRange = {};
			imageView.subresourceRange.aspectMask = aspectMask;
			imageView.subresourceRange.baseMipLevel = 0;
			imageView.subresourceRange.levelCount = 1; // Number of mipmap levels
			imageView.subresourceRange.baseArrayLayer = 0;
			imageView.subresourceRange.layerCount = arrayLayers;
			imageView.image = attachment->image;
			imageView.flags = 0;
			VK_CHECK_RESULT(vkCreateImageView(device->m_LogicalDevice, &imageView, nullptr, attachment->view.replace()));
		}

		VkBool32 GetSupportedDepthFormat(VkPhysicalDevice physicalDevice, VkFormat *depthFormat)
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
			VkCommandBufferAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			// TODO: Create command pool just for these types of allocations, using VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
			allocInfo.commandPool = device->m_CommandPool;
			allocInfo.commandBufferCount = 1;

			VkCommandBuffer commandBuffer;
			vkAllocateCommandBuffers(device->m_LogicalDevice, &allocInfo, &commandBuffer);

			VkCommandBufferBeginInfo beginInfo = {};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

			vkBeginCommandBuffer(commandBuffer, &beginInfo);

			return commandBuffer;
		}

		void EndSingleTimeCommands(VulkanDevice* device, VkQueue graphicsQueue, VkCommandBuffer commandBuffer)
		{
			vkEndCommandBuffer(commandBuffer);

			VkSubmitInfo submitInfo = {};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffer;

			vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
			vkQueueWaitIdle(graphicsQueue);

			vkFreeCommandBuffers(device->m_LogicalDevice, device->m_CommandPool, 1, &commandBuffer);
		}


		VkPrimitiveTopology TopologyModeToVkPrimitiveTopology(Renderer::TopologyMode mode)
		{
			switch (mode)
			{
			case Renderer::TopologyMode::POINT_LIST: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
			case Renderer::TopologyMode::LINE_LIST: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
			case Renderer::TopologyMode::LINE_STRIP: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
			case Renderer::TopologyMode::TRIANGLE_LIST: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			case Renderer::TopologyMode::TRIANGLE_STRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
			case Renderer::TopologyMode::TRIANGLE_FAN: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
			case Renderer::TopologyMode::LINE_LOOP:
			{
				Logger::LogWarning("Unsupported TopologyMode passed to Vulkan Renderer: LINE_LOOP");
				return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
			}
			default: return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
			}
		}

		VkCullModeFlagBits CullFaceToVkCullMode(Renderer::CullFace cullFace)
		{
			switch (cullFace)
			{
			case Renderer::CullFace::BACK: return VK_CULL_MODE_BACK_BIT;
			case Renderer::CullFace::FRONT: return VK_CULL_MODE_FRONT_BIT;
			case Renderer::CullFace::FRONT_AND_BACK: return VK_CULL_MODE_FRONT_AND_BACK;
			default: return VK_CULL_MODE_NONE;
			}
		}


		VkResult CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback)
		{
			auto func = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
			if (func != nullptr)
			{
				return func(instance, pCreateInfo, pAllocator, pCallback);
			}
			else
			{
				return VK_ERROR_EXTENSION_NOT_PRESENT;
			}
		}

		void DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator)
		{
			auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
			if (func != nullptr)
			{
				func(instance, callback, pAllocator);
			}
		}

		FrameBufferAttachment::FrameBufferAttachment(const VDeleter<VkDevice>& device) :
			image(device, vkDestroyImage),
			mem(device, vkFreeMemory),
			view(device, vkDestroyImageView)
		{
		}

		FrameBufferAttachment::FrameBufferAttachment(const VDeleter<VkDevice>& device, VkFormat format) :
			image(device, vkDestroyImage),
			mem(device, vkFreeMemory),
			view(device, vkDestroyImageView),
			format(format)
		{
		}

		FrameBuffer::FrameBuffer(const VDeleter<VkDevice>& device) :
			frameBuffer(device, vkDestroyFramebuffer),
			renderPass(device, vkDestroyRenderPass)
		{
		}

		VulkanShader::VulkanShader(const std::string& name, const std::string& vertexShaderFilePath, const std::string& fragmentShaderFilePath, const VDeleter<VkDevice>& device) :
			uniformBuffer(device),
			shader(name, vertexShaderFilePath, fragmentShaderFilePath)
		{
		}

		VulkanCubemapGBuffer::VulkanCubemapGBuffer(u32 id, const char* name, VkFormat internalFormat) :
			id(id),
			name(name),
			internalFormat(internalFormat)
		{

		}
		VulkanMaterial::VulkanMaterial()
		{
		}
} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN