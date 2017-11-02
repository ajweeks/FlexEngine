#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanRenderer.hpp"

#include <algorithm>
#include <set>
#include <iostream>
#include <unordered_map>
#include <functional>

#include "stb_image.h"

#include <glm/gtc/matrix_transform.hpp>

#include "FreeCamera.hpp"
#include "Helpers.hpp"
#include "Logger.hpp"
#include "VertexAttribute.hpp"
#include "VertexBufferData.hpp"
#include "GameContext.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	namespace vk
	{
		std::array<glm::mat4, 6> VulkanRenderer::m_CaptureViews;

		VulkanRenderer::VulkanRenderer(const GameContext& gameContext)
		{
			CreateInstance(gameContext);
			SetupDebugCallback();
			CreateSurface(gameContext.window);
			VkPhysicalDevice physicalDevice = PickPhysicalDevice();
			CreateLogicalDevice(physicalDevice);

			m_SwapChain = { m_VulkanDevice->m_LogicalDevice, vkDestroySwapchainKHR };
			m_DeferredCombineRenderPass = { m_VulkanDevice->m_LogicalDevice, vkDestroyRenderPass };
			m_DepthImage = { m_VulkanDevice->m_LogicalDevice, vkDestroyImage };
			m_DepthImageMemory = { m_VulkanDevice->m_LogicalDevice, vkFreeMemory };
			m_DepthImageView = { m_VulkanDevice->m_LogicalDevice, vkDestroyImageView };
			m_DescriptorPool = { m_VulkanDevice->m_LogicalDevice, vkDestroyDescriptorPool };
			m_PresentCompleteSemaphore = { m_VulkanDevice->m_LogicalDevice, vkDestroySemaphore };
			m_RenderCompleteSemaphore = { m_VulkanDevice->m_LogicalDevice, vkDestroySemaphore };

			offScreenFrameBuf = new FrameBuffer(m_VulkanDevice->m_LogicalDevice);
			offScreenFrameBuf->frameBufferAttachments = {
				{ "positionMetallicFrameBufferSampler",{ m_VulkanDevice->m_LogicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT } },
				{ "normalRoughnessFrameBufferSampler",{ m_VulkanDevice->m_LogicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT } },
				{ "albedoAOFrameBufferSampler", { m_VulkanDevice->m_LogicalDevice, VK_FORMAT_R8G8B8A8_UNORM } },
			};

			// NOTE: This is different from theh GLRenderer's capture views
			m_CaptureViews = {
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  -1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  -1.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f,  0.0f), glm::vec3(0.0f,  0.0f, 1.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
			};


			CreateSwapChain(gameContext.window);
			CreateSwapChainImageViews();
			CreateRenderPass();

			CreateCommandPool();
			CreateDepthResources();
			CreateFramebuffers();

			PrepareOffscreenFrameBuffer(gameContext.window);

			LoadDefaultShaderCode();

			const glm::uint shaderCount = m_Shaders.size();
			m_VertexIndexBufferPairs.reserve(shaderCount);
			for (size_t i = 0; i < shaderCount; ++i)
			{
				m_VertexIndexBufferPairs.push_back({
					new VulkanBuffer(m_VulkanDevice->m_LogicalDevice), // Vertex buffer
					new VulkanBuffer(m_VulkanDevice->m_LogicalDevice)  // Index buffer
				});
			}

			// ImGui buffers are dynamic, they shouldn't use the staging buffer
			m_VertexIndexBufferPairs[m_IGuiShaderID].useStagingBuffer = false;
			
			CreateVulkanTexture(RESOURCE_LOCATION + "textures/blank.jpg", VK_FORMAT_R8G8B8A8_UNORM, 1, &m_BlankTexture);
		}

		VulkanRenderer::~VulkanRenderer()
		{
			{
				auto iter = m_RenderObjects.begin();
				while (iter != m_RenderObjects.end())
				{
					SafeDelete(*iter);
					iter = m_RenderObjects.erase(iter);
				}
				m_RenderObjects.clear();
			}

			for (auto iter = m_DescriptorSetLayouts.begin(); iter != m_DescriptorSetLayouts.end(); ++iter)
			{
				vkDestroyDescriptorSetLayout(m_VulkanDevice->m_LogicalDevice, *iter, nullptr);
			}

			m_PresentCompleteSemaphore.replace();
			m_RenderCompleteSemaphore.replace();

			for (size_t i = 0; i < m_VertexIndexBufferPairs.size(); ++i)
			{
				SafeDelete(m_VertexIndexBufferPairs[i].vertexBuffer);
				SafeDelete(m_VertexIndexBufferPairs[i].indexBuffer);
			}

			m_Shaders.clear();

			SafeDelete(offScreenFrameBuf);
			vkDestroySemaphore(m_VulkanDevice->m_LogicalDevice, offscreenSemaphore, nullptr);

			vkDestroySampler(m_VulkanDevice->m_LogicalDevice, colorSampler, nullptr);
			
			m_gBufferQuadVertexBufferData.Destroy();

			vkDestroyPipeline(m_VulkanDevice->m_LogicalDevice, m_ImGui_GraphicsPipeline, nullptr);
			vkDestroyPipelineLayout(m_VulkanDevice->m_LogicalDevice, m_ImGui_PipelineLayout, nullptr);

			vkDestroyPipelineCache(m_VulkanDevice->m_LogicalDevice, m_ImGuiPipelineCache, nullptr);
			vkDestroyPipelineCache(m_VulkanDevice->m_LogicalDevice, m_PipelineCache, nullptr);

			m_DescriptorPool.replace();
			m_DepthImageView.replace();
			m_DepthImageMemory.replace();
			m_DepthImage.replace();

			SafeDelete(m_BlankTexture);

			for (size_t i = 0; i < m_LoadedTextures.size(); ++i)
			{
				SafeDelete(m_LoadedTextures[i]);
			}
			m_LoadedTextures.clear();

			m_DeferredCombineRenderPass.replace();

			m_SwapChain.replace();
			m_SwapChainImageViews.clear();
			m_SwapChainFramebuffers.clear();

			vkDeviceWaitIdle(m_VulkanDevice->m_LogicalDevice);

			SafeDelete(m_VulkanDevice);

			glfwTerminate();
		}

		void VulkanRenderer::PostInitialize(const GameContext& gameContext)
		{
			ImGui_Init(gameContext);

			CreateDescriptorPool();

			ShaderID deferredCombineShaderID;
			if (!GetShaderID("deferred_combine", deferredCombineShaderID))
			{
				Logger::LogError("Failed to find deferred_combine shader!");
			}




			// TODO: FIXME: Remove this :grimmacing:
			MaterialID skyboxMatID = 0;
			for (size_t i = 0; i < m_LoadedMaterials.size(); ++i)
			{
				if (m_LoadedMaterials[i].material.name.compare("HDR Skybox") == 0)
				{
					skyboxMatID = i;
					break;
				}
			}



			MaterialCreateInfo gBufferMaterialCreateInfo = {};
			gBufferMaterialCreateInfo.name = "GBuffer material";
			gBufferMaterialCreateInfo.shaderName = "deferred_combine";
			gBufferMaterialCreateInfo.enableIrradianceSampler = true;
			gBufferMaterialCreateInfo.irradianceSamplerMatID = skyboxMatID;
			gBufferMaterialCreateInfo.enablePrefilteredMap = true;
			gBufferMaterialCreateInfo.prefilterMapSamplerMatID = skyboxMatID;
			gBufferMaterialCreateInfo.enableBRDFLUT = true;
			gBufferMaterialCreateInfo.brdfLUTSamplerMatID = skyboxMatID;
			for (size_t i = 0; i < offScreenFrameBuf->frameBufferAttachments.size(); ++i)
			{
				gBufferMaterialCreateInfo.frameBuffers.push_back({ 
					offScreenFrameBuf->frameBufferAttachments[i].first, (void*)&offScreenFrameBuf->frameBufferAttachments[i].second.view
				});
			}

			MaterialID gBufferMatID = InitializeMaterial(gameContext, &gBufferMaterialCreateInfo);

			VertexBufferData::CreateInfo gBufferQuadVertexBufferDataCreateInfo = {};
			gBufferQuadVertexBufferDataCreateInfo.positions_3D = {
				{ -1.0f,  1.0f, 0.0f },
				{ -1.0f, -1.0f, 0.0f },
				{ 1.0f,  1.0f, 0.0f },
				{ 1.0f, -1.0f, 0.0f },
			};

			gBufferQuadVertexBufferDataCreateInfo.texCoords_UV = {
				{ 0.0f, 1.0f },
				{ 0.0f, 0.0f },
				{ 1.0f, 1.0f },
				{ 1.0f, 0.0f },
			};
			gBufferQuadVertexBufferDataCreateInfo.attributes = (glm::uint)VertexAttribute::POSITION | (glm::uint)VertexAttribute::UV;
			m_gBufferQuadVertexBufferData.Initialize(&gBufferQuadVertexBufferDataCreateInfo);

			RenderObjectCreateInfo gBufferQuadCreateInfo = {};
			gBufferQuadCreateInfo.name = "G Buffer Quad";
			gBufferQuadCreateInfo.materialID = gBufferMatID;
			m_gBufferQuadTransform = Transform::Identity();
			gBufferQuadCreateInfo.transform = &m_gBufferQuadTransform;
			gBufferQuadCreateInfo.vertexBufferData = &m_gBufferQuadVertexBufferData;
			gBufferQuadCreateInfo.cullFace = CullFace::NONE;

			std::vector<glm::uint> indexBuffer = { 0, 1, 2,  2, 1, 3 };
			gBufferQuadCreateInfo.indices = &indexBuffer;

			m_GBufferQuadRenderID = InitializeRenderObject(gameContext, &gBufferQuadCreateInfo);

			m_gBufferQuadVertexBufferData.DescribeShaderVariables(this, m_GBufferQuadRenderID);

			VulkanRenderObject* gBufferRenderObject = GetRenderObject(m_GBufferQuadRenderID);
			gBufferRenderObject->visible = false; // Don't render the g buffer normally, we'll handle it separately


			for (size_t i = 0; i < m_Shaders.size(); ++i)
			{
				CreateDescriptorSetLayout(i);
				CreateUniformBuffers(&m_Shaders[i]);
			}


			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				CreateDescriptorSet(i);
				CreateGraphicsPipeline(i);
			}


			CreateStaticVertexBuffers();
			CreateStaticIndexBuffers();

			CreateCommandBuffers();
			CreateSemaphores();

			// Generate reflection probe data
			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (m_LoadedMaterials[renderObject->materialID].material.generateIrradianceSampler)
				{
					Logger::LogInfo("Generating cubemap from HDRI");
					GenerateCubemapFromHDR(gameContext, renderObject);

					Logger::LogInfo("Generating irradiance sampler from cubemap");
					GenerateIrradianceSampler(gameContext, renderObject);

					Logger::LogInfo("Generating prefilter map from cubemap");
					GeneratePrefilteredCube(gameContext, renderObject);

					Logger::LogInfo("Generating BRDF LUT");
					GenerateBRDFLUT(gameContext, renderObject);
				}
			}

			Logger::LogInfo("Ready!\n");
		}

		void VulkanRenderer::GenerateCubemapFromHDR(const GameContext& gameContext, VulkanRenderObject * renderObject)
		{
			MaterialCreateInfo equirectangularToCubeMatCreateInfo = {};
			equirectangularToCubeMatCreateInfo.name = "Equirectangular to Cube";
			equirectangularToCubeMatCreateInfo.shaderName = "equirectangular_to_cube";
			equirectangularToCubeMatCreateInfo.enableHDREquirectangularSampler = true;
			equirectangularToCubeMatCreateInfo.generateHDREquirectangularSampler = true;
			// TODO: Make cyclable at runtime
			equirectangularToCubeMatCreateInfo.hdrEquirectangularTexturePath =
				//RESOURCE_LOCATION + "textures/hdri/Arches_E_PineTree/Arches_E_PineTree_3k.hdr";
				//RESOURCE_LOCATION + "textures/hdri/Factory_Catwalk/Factory_Catwalk_2k.hdr";
				//RESOURCE_LOCATION + "textures/hdri/Ice_Lake/Ice_Lake_Ref.hdr";
				RESOURCE_LOCATION + "textures/hdri/Protospace_B/Protospace_B_Ref.hdr";
			MaterialID equirectangularToCubeMatID = InitializeMaterial(gameContext, &equirectangularToCubeMatCreateInfo);

			const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
			const uint32_t dim = (uint32_t)m_LoadedMaterials[renderObject->materialID].material.cubemapSamplerSize.x;
			
			const uint32_t mipLevels = static_cast<uint32_t>(floor(log2(dim))) + 1;

			VkAttachmentDescription attDesc = {};
			// HDR texture color attachment
			attDesc.format = format;
			attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
			attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

			VkSubpassDescription subpassDescription = {};
			subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpassDescription.colorAttachmentCount = 1;
			subpassDescription.pColorAttachments = &colorReference;

			// Use subpass dependencies for layout transitions
			std::array<VkSubpassDependency, 2> dependencies;
			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			// Renderpass
			VkRenderPassCreateInfo renderPassCreateInfo = {};
			renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassCreateInfo.attachmentCount = 1;
			renderPassCreateInfo.pAttachments = &attDesc;
			renderPassCreateInfo.subpassCount = 1;
			renderPassCreateInfo.pSubpasses = &subpassDescription;
			renderPassCreateInfo.dependencyCount = dependencies.size();
			renderPassCreateInfo.pDependencies = dependencies.data();
			VkRenderPass renderpass;
			VK_CHECK_RESULT(vkCreateRenderPass(m_VulkanDevice->m_LogicalDevice, &renderPassCreateInfo, nullptr, &renderpass));
			


			// Offfscreen framebuffer
			struct {
				VkImage image;
				VkImageView view;
				VkDeviceMemory memory;
				VkFramebuffer framebuffer;
			} offscreen;

			// Color attachment
			VkImageCreateInfo offscreenImageCreateInfo = {};
			offscreenImageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			offscreenImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			offscreenImageCreateInfo.format = format;
			offscreenImageCreateInfo.extent.width = dim;
			offscreenImageCreateInfo.extent.height = dim;
			offscreenImageCreateInfo.extent.depth = 1;
			offscreenImageCreateInfo.mipLevels = mipLevels;
			offscreenImageCreateInfo.arrayLayers = 1;
			offscreenImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			offscreenImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			offscreenImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			offscreenImageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			offscreenImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VK_CHECK_RESULT(vkCreateImage(m_VulkanDevice->m_LogicalDevice, &offscreenImageCreateInfo, nullptr, &offscreen.image));

			VkMemoryAllocateInfo offscreenMemAlloc = {};
			offscreenMemAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			VkMemoryRequirements offscreenMemReqs;
			vkGetImageMemoryRequirements(m_VulkanDevice->m_LogicalDevice, offscreen.image, &offscreenMemReqs);
			offscreenMemAlloc.allocationSize = offscreenMemReqs.size;
			offscreenMemAlloc.memoryTypeIndex = FindMemoryType(offscreenMemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(m_VulkanDevice->m_LogicalDevice, &offscreenMemAlloc, nullptr, &offscreen.memory));
			VK_CHECK_RESULT(vkBindImageMemory(m_VulkanDevice->m_LogicalDevice, offscreen.image, offscreen.memory, 0));

			VkImageViewCreateInfo colorImageView = {};
			colorImageView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
			colorImageView.format = format;
			colorImageView.flags = 0;
			colorImageView.subresourceRange = {};
			colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			colorImageView.subresourceRange.baseMipLevel = 0;
			colorImageView.subresourceRange.levelCount = 1;
			colorImageView.subresourceRange.baseArrayLayer = 0;
			colorImageView.subresourceRange.layerCount = 1;
			colorImageView.image = offscreen.image;
			VK_CHECK_RESULT(vkCreateImageView(m_VulkanDevice->m_LogicalDevice, &colorImageView, nullptr, &offscreen.view));

			VkFramebufferCreateInfo framebufCreateInfo = {};
			framebufCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufCreateInfo.renderPass = renderpass;
			framebufCreateInfo.attachmentCount = 1;
			framebufCreateInfo.pAttachments = &offscreen.view;
			framebufCreateInfo.width = dim;
			framebufCreateInfo.height = dim;
			framebufCreateInfo.layers = 1;
			VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &framebufCreateInfo, nullptr, &offscreen.framebuffer));

			VkCommandBuffer layoutCmd = CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			SetImageLayout(
				layoutCmd,
				offscreen.image,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
			FlushCommandBuffer(layoutCmd, m_GraphicsQueue, true);


			// Descriptors
			std::array<VkDescriptorSetLayoutBinding, 1> setLayoutBindings = {};
			setLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			setLayoutBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			setLayoutBindings[0].binding = 0;
			setLayoutBindings[0].descriptorCount = 1;
			
			VkDescriptorSetLayout descriptorsetlayout;
			VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
			descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorSetLayoutCreateInfo.pBindings = setLayoutBindings.data();
			descriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_VulkanDevice->m_LogicalDevice, &descriptorSetLayoutCreateInfo, nullptr, &descriptorsetlayout));
			
			ShaderID equirectangularToCubeShaderID;
			if (!GetShaderID(equirectangularToCubeMatCreateInfo.shaderName, equirectangularToCubeShaderID))
			{
				Logger::LogError("Failed to find equirectangular_to_cube shader ID!");
			}

			VkDescriptorSet descriptorSet;
			DescriptorSetCreateInfo equirectangularToCubeDescriptorCreateInfo = {};
			equirectangularToCubeDescriptorCreateInfo.descriptorSet = &descriptorSet;
			equirectangularToCubeDescriptorCreateInfo.descriptorSetLayout = &m_DescriptorSetLayouts[equirectangularToCubeShaderID];
			equirectangularToCubeDescriptorCreateInfo.shaderID = equirectangularToCubeShaderID;
			equirectangularToCubeDescriptorCreateInfo.uniformBuffer = &m_Shaders[equirectangularToCubeShaderID].uniformBuffer;
			equirectangularToCubeDescriptorCreateInfo.hdrEquirectangularTexture = m_LoadedMaterials[equirectangularToCubeMatID].hdrEquirectangularTexture;
			CreateDescriptorSet(&equirectangularToCubeDescriptorCreateInfo);

			VkPipelineLayout pipelinelayout;
			std::array<VkPushConstantRange, 1> pushConstantRanges = {};
			pushConstantRanges[0].size = sizeof(Material::PushConstantBlock);
			pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			pushConstantRanges[0].offset = 0;

			VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
			pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutCreateInfo.pSetLayouts = &descriptorsetlayout;
			pipelineLayoutCreateInfo.setLayoutCount = pushConstantRanges.size();
			pipelineLayoutCreateInfo.pushConstantRangeCount = pushConstantRanges.size();
			pipelineLayoutCreateInfo.pPushConstantRanges = pushConstantRanges.data();
			VK_CHECK_RESULT(vkCreatePipelineLayout(m_VulkanDevice->m_LogicalDevice, &pipelineLayoutCreateInfo, nullptr, &pipelinelayout));

			// Pipeline
			VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
			inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssemblyState.topology = renderObject->topology;
			inputAssemblyState.flags = 0;
			inputAssemblyState.primitiveRestartEnable = VK_FALSE;

			VkPipelineRasterizationStateCreateInfo rasterizationState = {};
			rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizationState.cullMode = VK_CULL_MODE_NONE;
			rasterizationState.frontFace = VK_FRONT_FACE_CLOCKWISE;
			rasterizationState.depthClampEnable = VK_FALSE;
			rasterizationState.lineWidth = 1.0f;

			VkPipelineColorBlendAttachmentState blendAttachmentState = {};
			blendAttachmentState.colorWriteMask = 0xf;
			blendAttachmentState.blendEnable = VK_FALSE;

			VkPipelineColorBlendStateCreateInfo colorBlendState = {};
			colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlendState.attachmentCount = 1;
			colorBlendState.pAttachments = &blendAttachmentState;

			VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
			depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencilState.depthTestEnable = VK_FALSE;
			depthStencilState.depthWriteEnable = VK_FALSE;
			depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
			depthStencilState.front = depthStencilState.back;
			depthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;

			VkPipelineViewportStateCreateInfo viewportState = {};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.scissorCount = 1;
			viewportState.viewportCount = 1;

			VkPipelineMultisampleStateCreateInfo multisampleState = {};
			multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dynamicState = {};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());
			dynamicState.pDynamicStates = dynamicStateEnables.data();

			// Vertex input state
			VkVertexInputBindingDescription vertexInputBinding = {};
			vertexInputBinding.binding = 0;
			vertexInputBinding.stride = renderObject->vertexBufferData->VertexStride;
			vertexInputBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription vertexInputAttribute = {};
			vertexInputAttribute.binding = 0;
			vertexInputAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
			vertexInputAttribute.location = 0;
			vertexInputAttribute.offset = 0;

			VkPipelineVertexInputStateCreateInfo vertexInputState = {};
			vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputState.vertexBindingDescriptionCount = 1;
			vertexInputState.pVertexBindingDescriptions = &vertexInputBinding;
			vertexInputState.vertexAttributeDescriptionCount = 1;
			vertexInputState.pVertexAttributeDescriptions = &vertexInputAttribute;

			std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

			VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
			pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineCreateInfo.layout = pipelinelayout;
			pipelineCreateInfo.renderPass = renderpass;
			pipelineCreateInfo.flags = 0;
			pipelineCreateInfo.basePipelineIndex = -1;
			pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
			pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
			pipelineCreateInfo.pRasterizationState = &rasterizationState;
			pipelineCreateInfo.pColorBlendState = &colorBlendState;
			pipelineCreateInfo.pMultisampleState = &multisampleState;
			pipelineCreateInfo.pViewportState = &viewportState;
			pipelineCreateInfo.pDepthStencilState = &depthStencilState;
			pipelineCreateInfo.pDynamicState = &dynamicState;
			pipelineCreateInfo.stageCount = 2;
			pipelineCreateInfo.pStages = shaderStages.data();
			pipelineCreateInfo.pVertexInputState = &vertexInputState;

			VDeleter<VkShaderModule> vertShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(m_Shaders[equirectangularToCubeShaderID].shader.vertexShaderCode, vertShaderModule))
			{
				Logger::LogError("Failed to compile vertex shader located at: " + m_Shaders[equirectangularToCubeShaderID].shader.vertexShaderFilePath);
			}

			VDeleter<VkShaderModule> fragShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(m_Shaders[equirectangularToCubeShaderID].shader.fragmentShaderCode, fragShaderModule))
			{
				Logger::LogError("Failed to compile fragment shader located at: " + m_Shaders[equirectangularToCubeShaderID].shader.fragmentShaderFilePath);
			}

			shaderStages[0] = {};
			shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			shaderStages[0].module = vertShaderModule;
			shaderStages[0].pName = "main";

			shaderStages[1] = {};
			shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			shaderStages[1].module = fragShaderModule;
			shaderStages[1].pName = "main";

			VkPipeline pipeline;
			VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_VulkanDevice->m_LogicalDevice, m_PipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));


			// Render

			VkClearValue clearValues[1];
			clearValues[0].color = m_ClearColor;

			VkRenderPassBeginInfo renderPassBeginInfo = {};
			renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			// Reuse render pass from example pass
			renderPassBeginInfo.renderPass = renderpass;
			renderPassBeginInfo.framebuffer = offscreen.framebuffer;
			renderPassBeginInfo.renderArea.extent.width = dim;
			renderPassBeginInfo.renderArea.extent.height = dim;
			renderPassBeginInfo.clearValueCount = 1;
			renderPassBeginInfo.pClearValues = clearValues;

			VkCommandBuffer cmdBuf = CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			VkViewport viewport = {};
			viewport.width = (float)dim;
			viewport.height = (float)dim;
			viewport.minDepth = 0.0;
			viewport.maxDepth = 1.0f;

			VkRect2D scissor = {};
			scissor.extent = { dim, dim };
			scissor.offset = { 0, 0 };

			vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
			vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = mipLevels;
			subresourceRange.layerCount = 6;

			// Change image layout for all cubemap faces to transfer destination
			SetImageLayout(
				cmdBuf,
				m_LoadedMaterials[renderObject->materialID].cubemapTexture,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);

			for (uint32_t mip = 0; mip < mipLevels; ++mip)
			{
				for (uint32_t face = 0; face < 6; ++face)
				{
					viewport.width = static_cast<float>(dim * std::pow(0.5f, mip));
					viewport.height = static_cast<float>(dim * std::pow(0.5f, mip));
					vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
					
					vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

					// Push constants
					m_LoadedMaterials[renderObject->materialID].material.pushConstantBlock.mvp = 
						glm::perspective(PI_DIV_TWO, 1.0f, 0.1f, (float)dim) * m_CaptureViews[face];
					vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Material::PushConstantBlock),
						&m_LoadedMaterials[renderObject->materialID].material.pushConstantBlock);

					vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

					BindDescriptorSet(&m_Shaders[m_LoadedMaterials[renderObject->materialID].material.shaderID], renderObject->renderID, cmdBuf, pipelinelayout, descriptorSet);

					VkDeviceSize offsets[1] = { 0 };

					const ShaderID shaderID = m_LoadedMaterials[renderObject->materialID].material.shaderID;
					vkCmdBindVertexBuffers(cmdBuf, 0, 1, &m_VertexIndexBufferPairs[shaderID].vertexBuffer->m_Buffer, offsets);
					if (renderObject->indexed)
					{
						vkCmdBindIndexBuffer(cmdBuf, m_VertexIndexBufferPairs[shaderID].indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
						vkCmdDrawIndexed(cmdBuf, m_VertexIndexBufferPairs[shaderID].indexCount, 1, 0, 0, 0);
					}
					else
					{
						vkCmdDraw(cmdBuf, renderObject->vertexBufferData->VertexCount, 1, 0, 0);
					}

					vkCmdEndRenderPass(cmdBuf);

					SetImageLayout(
						cmdBuf,
						offscreen.image,
						VK_IMAGE_ASPECT_COLOR_BIT,
						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
						VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

					// Copy region for transfer from framebuffer to cube face
					VkImageCopy copyRegion = {};

					copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					copyRegion.srcSubresource.baseArrayLayer = 0;
					copyRegion.srcSubresource.mipLevel = 0;
					copyRegion.srcSubresource.layerCount = 1;
					copyRegion.srcOffset = { 0, 0, 0 };

					copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					copyRegion.dstSubresource.baseArrayLayer = face;
					copyRegion.dstSubresource.mipLevel = mip;
					copyRegion.dstSubresource.layerCount = 1;
					copyRegion.dstOffset = { 0, 0, 0 };

					copyRegion.extent.width = static_cast<uint32_t>(viewport.width);
					copyRegion.extent.height = static_cast<uint32_t>(viewport.height);
					copyRegion.extent.depth = 1;

					vkCmdCopyImage(
						cmdBuf,
						offscreen.image,
						VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						m_LoadedMaterials[renderObject->materialID].cubemapTexture->image,
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						1,
						&copyRegion);

					// Transform framebuffer color attachment back 
					SetImageLayout(
						cmdBuf,
						offscreen.image,
						VK_IMAGE_ASPECT_COLOR_BIT,
						VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
				}
			}

			SetImageLayout(
				cmdBuf,
				m_LoadedMaterials[renderObject->materialID].cubemapTexture,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				subresourceRange);

			FlushCommandBuffer(cmdBuf, m_GraphicsQueue, true);


			vkDestroyRenderPass(m_VulkanDevice->m_LogicalDevice, renderpass, nullptr);
			vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, offscreen.framebuffer, nullptr);
			vkFreeMemory(m_VulkanDevice->m_LogicalDevice, offscreen.memory, nullptr);
			vkDestroyImageView(m_VulkanDevice->m_LogicalDevice, offscreen.view, nullptr);
			vkDestroyImage(m_VulkanDevice->m_LogicalDevice, offscreen.image, nullptr);
			vkDestroyDescriptorSetLayout(m_VulkanDevice->m_LogicalDevice, descriptorsetlayout, nullptr);
			vkDestroyPipeline(m_VulkanDevice->m_LogicalDevice, pipeline, nullptr);
			vkDestroyPipelineLayout(m_VulkanDevice->m_LogicalDevice, pipelinelayout, nullptr);
		}

		void VulkanRenderer::GenerateIrradianceSampler(const GameContext& gameContext, VulkanRenderObject* renderObject)
		{
			UNREFERENCED_PARAMETER(gameContext);

			const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
			const uint32_t dim = (uint32_t)m_LoadedMaterials[renderObject->materialID].material.irradianceSamplerSize.x;
			const uint32_t mipLevels = static_cast<uint32_t>(floor(log2(dim))) + 1;

			VkAttachmentDescription attDesc = {};
			// Color attachment
			attDesc.format = format;
			attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
			attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

			VkSubpassDescription subpassDescription = {};
			subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpassDescription.colorAttachmentCount = 1;
			subpassDescription.pColorAttachments = &colorReference;

			// Use subpass dependencies for layout transitions
			std::array<VkSubpassDependency, 2> dependencies;
			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			// Renderpass
			VkRenderPassCreateInfo renderPassCreateInfo = {};
			renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassCreateInfo.attachmentCount = 1;
			renderPassCreateInfo.pAttachments = &attDesc;
			renderPassCreateInfo.subpassCount = 1;
			renderPassCreateInfo.pSubpasses = &subpassDescription;
			renderPassCreateInfo.dependencyCount = dependencies.size();
			renderPassCreateInfo.pDependencies = dependencies.data();
			VkRenderPass renderpass;
			VK_CHECK_RESULT(vkCreateRenderPass(m_VulkanDevice->m_LogicalDevice, &renderPassCreateInfo, nullptr, &renderpass));


			// Offfscreen framebuffer
			struct {
				VkImage image;
				VkImageView view;
				VkDeviceMemory memory;
				VkFramebuffer framebuffer;
			} offscreen;

			// Color attachment
			VkImageCreateInfo offscreenImageCreateInfo = {};
			offscreenImageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			offscreenImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			offscreenImageCreateInfo.format = format;
			offscreenImageCreateInfo.extent.width = dim;
			offscreenImageCreateInfo.extent.height = dim;
			offscreenImageCreateInfo.extent.depth = 1;
			offscreenImageCreateInfo.mipLevels = mipLevels;
			offscreenImageCreateInfo.arrayLayers = 1;
			offscreenImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			offscreenImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			offscreenImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			offscreenImageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			offscreenImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VK_CHECK_RESULT(vkCreateImage(m_VulkanDevice->m_LogicalDevice, &offscreenImageCreateInfo, nullptr, &offscreen.image));

			VkMemoryAllocateInfo offscreenMemAlloc = {};
			offscreenMemAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			VkMemoryRequirements offscreenMemReqs;
			vkGetImageMemoryRequirements(m_VulkanDevice->m_LogicalDevice, offscreen.image, &offscreenMemReqs);
			offscreenMemAlloc.allocationSize = offscreenMemReqs.size;
			offscreenMemAlloc.memoryTypeIndex = FindMemoryType(offscreenMemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(m_VulkanDevice->m_LogicalDevice, &offscreenMemAlloc, nullptr, &offscreen.memory));
			VK_CHECK_RESULT(vkBindImageMemory(m_VulkanDevice->m_LogicalDevice, offscreen.image, offscreen.memory, 0));

			VkImageViewCreateInfo colorImageView = {};
			colorImageView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
			colorImageView.format = format;
			colorImageView.flags = 0;
			colorImageView.subresourceRange = {};
			colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			colorImageView.subresourceRange.baseMipLevel = 0;
			colorImageView.subresourceRange.levelCount = 1;
			colorImageView.subresourceRange.baseArrayLayer = 0;
			colorImageView.subresourceRange.layerCount = 1;
			colorImageView.image = offscreen.image;
			VK_CHECK_RESULT(vkCreateImageView(m_VulkanDevice->m_LogicalDevice, &colorImageView, nullptr, &offscreen.view));

			VkFramebufferCreateInfo framebufCreateInfo = {};
			framebufCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufCreateInfo.renderPass = renderpass;
			framebufCreateInfo.attachmentCount = 1;
			framebufCreateInfo.pAttachments = &offscreen.view;
			framebufCreateInfo.width = dim;
			framebufCreateInfo.height = dim;
			framebufCreateInfo.layers = 1;
			VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &framebufCreateInfo, nullptr, &offscreen.framebuffer));

			VkCommandBuffer layoutCmd = CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			SetImageLayout(
				layoutCmd,
				offscreen.image,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
			FlushCommandBuffer(layoutCmd, m_GraphicsQueue, true);

			// Descriptors
			VkDescriptorSetLayout descriptorsetlayout;
			std::array<VkDescriptorSetLayoutBinding, 1> setLayoutBindings = {};
			setLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			setLayoutBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			setLayoutBindings[0].binding = 0;
			setLayoutBindings[0].descriptorCount = 1;
			
			VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
			descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorSetLayoutCreateInfo.pBindings = setLayoutBindings.data();
			descriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_VulkanDevice->m_LogicalDevice, &descriptorSetLayoutCreateInfo, nullptr, &descriptorsetlayout));
			
			// Descriptor Pool
			std::array<VkDescriptorPoolSize, 1> poolSizes = {};
			poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSizes[0].descriptorCount = 1;
			
			VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
			descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			descriptorPoolCreateInfo.pPoolSizes = poolSizes.data();
			descriptorPoolCreateInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
			descriptorPoolCreateInfo.maxSets = 2;
			VkDescriptorPool descriptorPool;
			VK_CHECK_RESULT(vkCreateDescriptorPool(m_VulkanDevice->m_LogicalDevice, &descriptorPoolCreateInfo, nullptr, &descriptorPool));
			
			// Descriptor sets
			VkDescriptorSet descriptorSet;
			VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {};
			descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorSetAllocateInfo.descriptorPool = descriptorPool;
			descriptorSetAllocateInfo.pSetLayouts = &descriptorsetlayout;
			descriptorSetAllocateInfo.descriptorSetCount = 1;
			VK_CHECK_RESULT(vkAllocateDescriptorSets(m_VulkanDevice->m_LogicalDevice, &descriptorSetAllocateInfo, &descriptorSet));
			
			VkDescriptorImageInfo descriptorImageInfo = {};
			descriptorImageInfo.imageView = offscreen.view;
			descriptorImageInfo.imageLayout = m_LoadedMaterials[renderObject->materialID].cubemapTexture->imageLayout;
			
			VkWriteDescriptorSet writeDescriptorSet = {};
			writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSet.dstSet = descriptorSet;
			writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSet.dstBinding = 0;
			writeDescriptorSet.pImageInfo = &m_LoadedMaterials[renderObject->materialID].cubemapTexture->imageInfoDescriptor;
			writeDescriptorSet.descriptorCount = 1;
			vkUpdateDescriptorSets(m_VulkanDevice->m_LogicalDevice, 1, &writeDescriptorSet, 0, nullptr);

			VkPipelineLayout pipelinelayout;
			std::array<VkPushConstantRange, 1> pushConstantRanges = {};
			pushConstantRanges[0].size = sizeof(Material::PushConstantBlock);
			pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			pushConstantRanges[0].offset = 0;

			VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
			pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutCreateInfo.pSetLayouts = &descriptorsetlayout;
			pipelineLayoutCreateInfo.setLayoutCount = pushConstantRanges.size();
			pipelineLayoutCreateInfo.pushConstantRangeCount = pushConstantRanges.size();
			pipelineLayoutCreateInfo.pPushConstantRanges = pushConstantRanges.data();
			VK_CHECK_RESULT(vkCreatePipelineLayout(m_VulkanDevice->m_LogicalDevice, &pipelineLayoutCreateInfo, nullptr, &pipelinelayout));

			// Pipeline
			VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
			inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssemblyState.topology = renderObject->topology;
			inputAssemblyState.flags = 0;
			inputAssemblyState.primitiveRestartEnable = VK_FALSE;

			VkPipelineRasterizationStateCreateInfo rasterizationState = {};
			rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizationState.cullMode = VK_CULL_MODE_NONE;
			rasterizationState.frontFace = VK_FRONT_FACE_CLOCKWISE;
			rasterizationState.depthClampEnable = VK_FALSE;
			rasterizationState.lineWidth = 1.0f;

			VkPipelineColorBlendAttachmentState blendAttachmentState = {};
			blendAttachmentState.colorWriteMask = 0xf;
			blendAttachmentState.blendEnable = VK_FALSE;

			VkPipelineColorBlendStateCreateInfo colorBlendState = {};
			colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlendState.attachmentCount = 1;
			colorBlendState.pAttachments = &blendAttachmentState;

			VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
			depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencilState.depthTestEnable = VK_FALSE;
			depthStencilState.depthWriteEnable = VK_FALSE;
			depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
			depthStencilState.front = depthStencilState.back;
			depthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;

			VkPipelineViewportStateCreateInfo viewportState = {};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.scissorCount = 1;
			viewportState.viewportCount = 1;

			VkPipelineMultisampleStateCreateInfo multisampleState = {};
			multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dynamicState = {};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());
			dynamicState.pDynamicStates = dynamicStateEnables.data();

			// Vertex input state
			VkVertexInputBindingDescription vertexInputBinding = {};
			vertexInputBinding.binding = 0;
			vertexInputBinding.stride = renderObject->vertexBufferData->VertexStride;
			vertexInputBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription vertexInputAttribute = {};
			vertexInputAttribute.binding = 0;
			vertexInputAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
			vertexInputAttribute.location = 0;
			vertexInputAttribute.offset = 0;

			VkPipelineVertexInputStateCreateInfo vertexInputState = {};
			vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputState.vertexBindingDescriptionCount = 1;
			vertexInputState.pVertexBindingDescriptions = &vertexInputBinding;
			vertexInputState.vertexAttributeDescriptionCount = 1;
			vertexInputState.pVertexAttributeDescriptions = &vertexInputAttribute;

			std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

			VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
			pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineCreateInfo.layout = pipelinelayout;
			pipelineCreateInfo.renderPass = renderpass;
			pipelineCreateInfo.flags = 0;
			pipelineCreateInfo.basePipelineIndex = -1;
			pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
			pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
			pipelineCreateInfo.pRasterizationState = &rasterizationState;
			pipelineCreateInfo.pColorBlendState = &colorBlendState;
			pipelineCreateInfo.pMultisampleState = &multisampleState;
			pipelineCreateInfo.pViewportState = &viewportState;
			pipelineCreateInfo.pDepthStencilState = &depthStencilState;
			pipelineCreateInfo.pDynamicState = &dynamicState;
			pipelineCreateInfo.stageCount = 2;
			pipelineCreateInfo.pStages = shaderStages.data();
			pipelineCreateInfo.pVertexInputState = &vertexInputState;
			pipelineCreateInfo.renderPass = renderpass;

			ShaderID irradianceShaderID;
			if (!GetShaderID("irradiance", irradianceShaderID))
			{
				Logger::LogError("Failed to find irradiance shader!");
			}

			VDeleter<VkShaderModule> vertShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(m_Shaders[irradianceShaderID].shader.vertexShaderCode, vertShaderModule))
			{
				Logger::LogError("Failed to compile vertex shader located at: " + m_Shaders[irradianceShaderID].shader.vertexShaderFilePath);
			}

			VDeleter<VkShaderModule> fragShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(m_Shaders[irradianceShaderID].shader.fragmentShaderCode, fragShaderModule))
			{
				Logger::LogError("Failed to compile fragment shader located at: " + m_Shaders[irradianceShaderID].shader.fragmentShaderFilePath);
			}

			shaderStages[0] = {};
			shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			shaderStages[0].module = vertShaderModule;
			shaderStages[0].pName = "main";

			shaderStages[1] = {};
			shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			shaderStages[1].module = fragShaderModule;
			shaderStages[1].pName = "main";

			VkPipeline pipeline;
			VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_VulkanDevice->m_LogicalDevice, m_PipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));

			// Render

			VkClearValue clearValues[1];
			clearValues[0].color = m_ClearColor;

			VkRenderPassBeginInfo renderPassBeginInfo = {};
			renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassBeginInfo.renderPass = renderpass;
			renderPassBeginInfo.framebuffer = offscreen.framebuffer;
			renderPassBeginInfo.renderArea.extent.width = dim;
			renderPassBeginInfo.renderArea.extent.height = dim;
			renderPassBeginInfo.clearValueCount = 1;
			renderPassBeginInfo.pClearValues = clearValues;

			VkCommandBuffer cmdBuf = CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			VkViewport viewport = {};
			viewport.width = (float)dim;
			viewport.height = (float)dim;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;

			VkRect2D scissor = {};
			scissor.extent = { dim, dim };
			scissor.offset = { 0, 0 };

			vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
			vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = mipLevels;
			subresourceRange.layerCount = 6;

			// Change image layout for all cubemap faces to transfer destination
			SetImageLayout(
				cmdBuf,
				m_LoadedMaterials[renderObject->materialID].irradianceTexture,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);
			m_LoadedMaterials[renderObject->materialID].irradianceTexture->imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

			for (uint32_t mip = 0; mip < mipLevels; ++mip)
			{
				for (uint32_t face = 0; face < 6; ++face) 
				{
					viewport.width = static_cast<float>(dim * std::pow(0.5f, mip));
					viewport.height = static_cast<float>(dim * std::pow(0.5f, mip));
					vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

					vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

					// Push constants
					m_LoadedMaterials[renderObject->materialID].material.pushConstantBlock.mvp =
						glm::perspective(PI_DIV_TWO, 1.0f, 0.1f, (float)dim) * m_CaptureViews[face];
					vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Material::PushConstantBlock),
						&m_LoadedMaterials[renderObject->materialID].material.pushConstantBlock);

					vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

					BindDescriptorSet(&m_Shaders[m_LoadedMaterials[renderObject->materialID].material.shaderID], renderObject->renderID, cmdBuf, pipelinelayout, descriptorSet);

					VkDeviceSize offsets[1] = { 0 };

					const ShaderID shaderID = m_LoadedMaterials[renderObject->materialID].material.shaderID;
					vkCmdBindVertexBuffers(cmdBuf, 0, 1, &m_VertexIndexBufferPairs[shaderID].vertexBuffer->m_Buffer, offsets);
					if (renderObject->indexed)
					{
						vkCmdBindIndexBuffer(cmdBuf, m_VertexIndexBufferPairs[shaderID].indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
						vkCmdDrawIndexed(cmdBuf, m_VertexIndexBufferPairs[shaderID].indexCount, 1, 0, 0, 0);
					}
					else
					{
						vkCmdDraw(cmdBuf, renderObject->vertexBufferData->VertexCount, 1, 0, 0);
					}

					vkCmdEndRenderPass(cmdBuf);

					SetImageLayout(
						cmdBuf,
						offscreen.image,
						VK_IMAGE_ASPECT_COLOR_BIT,
						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
						VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

					// Copy region for transfer from framebuffer to cube face
					VkImageCopy copyRegion = {};

					copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					copyRegion.srcSubresource.baseArrayLayer = 0;
					copyRegion.srcSubresource.mipLevel = 0;
					copyRegion.srcSubresource.layerCount = 1;
					copyRegion.srcOffset = { 0, 0, 0 };

					copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					copyRegion.dstSubresource.baseArrayLayer = face;
					copyRegion.dstSubresource.mipLevel = mip;
					copyRegion.dstSubresource.layerCount = 1;
					copyRegion.dstOffset = { 0, 0, 0 };

					copyRegion.extent.width = static_cast<uint32_t>(viewport.width);
					copyRegion.extent.height = static_cast<uint32_t>(viewport.height);
					copyRegion.extent.depth = 1;

					vkCmdCopyImage(
						cmdBuf,
						offscreen.image,
						VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						m_LoadedMaterials[renderObject->materialID].irradianceTexture->image,
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						1,
						&copyRegion);

					// Transform framebuffer color attachment back 
					SetImageLayout(
						cmdBuf,
						offscreen.image,
						VK_IMAGE_ASPECT_COLOR_BIT,
						VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
				}
			}

			SetImageLayout(
				cmdBuf,
				m_LoadedMaterials[renderObject->materialID].irradianceTexture,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				subresourceRange);

			FlushCommandBuffer(cmdBuf, m_GraphicsQueue, true);


			vkDestroyRenderPass(m_VulkanDevice->m_LogicalDevice, renderpass, nullptr);
			vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, offscreen.framebuffer, nullptr);
			vkFreeMemory(m_VulkanDevice->m_LogicalDevice, offscreen.memory, nullptr);
			vkDestroyImageView(m_VulkanDevice->m_LogicalDevice, offscreen.view, nullptr);
			vkDestroyImage(m_VulkanDevice->m_LogicalDevice, offscreen.image, nullptr);
			vkDestroyDescriptorPool(m_VulkanDevice->m_LogicalDevice, descriptorPool, nullptr);
			vkDestroyDescriptorSetLayout(m_VulkanDevice->m_LogicalDevice, descriptorsetlayout, nullptr);
			vkDestroyPipeline(m_VulkanDevice->m_LogicalDevice, pipeline, nullptr);
			vkDestroyPipelineLayout(m_VulkanDevice->m_LogicalDevice, pipelinelayout, nullptr);
		}

		void VulkanRenderer::GeneratePrefilteredCube(const GameContext& gameContext, VulkanRenderObject* renderObject)
		{
			UNREFERENCED_PARAMETER(gameContext);

			const VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
			const uint32_t dim = m_LoadedMaterials[renderObject->materialID].material.prefilteredMapSize.x;
			const uint32_t mipLevels = static_cast<uint32_t>(floor(log2(dim))) + 1;

			VkAttachmentDescription attDesc = {};
			// Color attachment
			attDesc.format = format;
			attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
			attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

			VkSubpassDescription subpassDescription = {};
			subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpassDescription.colorAttachmentCount = 1;
			subpassDescription.pColorAttachments = &colorReference;

			// Use subpass dependencies for layout transitions
			std::array<VkSubpassDependency, 2> dependencies;
			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			// Renderpass
			VkRenderPassCreateInfo renderPassCreateInfo = {};
			renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassCreateInfo.attachmentCount = 1;
			renderPassCreateInfo.pAttachments = &attDesc;
			renderPassCreateInfo.subpassCount = 1;
			renderPassCreateInfo.pSubpasses = &subpassDescription;
			renderPassCreateInfo.dependencyCount = 2;
			renderPassCreateInfo.pDependencies = dependencies.data();
			VkRenderPass renderpass;
			VK_CHECK_RESULT(vkCreateRenderPass(m_VulkanDevice->m_LogicalDevice, &renderPassCreateInfo, nullptr, &renderpass));

			struct {
				VkImage image;
				VkImageView view;
				VkDeviceMemory memory;
				VkFramebuffer framebuffer;
			} offscreen;

			// Offfscreen framebuffer
			{
				// Color attachment
				VkImageCreateInfo imageCreateInfo = {};
				imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
				imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
				imageCreateInfo.format = format;
				imageCreateInfo.extent.width = dim;
				imageCreateInfo.extent.height = dim;
				imageCreateInfo.extent.depth = 1;
				imageCreateInfo.mipLevels = mipLevels;
				imageCreateInfo.arrayLayers = 1;
				imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
				imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
				imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
				imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				VK_CHECK_RESULT(vkCreateImage(m_VulkanDevice->m_LogicalDevice, &imageCreateInfo, nullptr, &offscreen.image));

				VkMemoryAllocateInfo memAlloc = {};
				memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				VkMemoryRequirements memReqs;
				vkGetImageMemoryRequirements(m_VulkanDevice->m_LogicalDevice, offscreen.image, &memReqs);
				memAlloc.allocationSize = memReqs.size;
				memAlloc.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				VK_CHECK_RESULT(vkAllocateMemory(m_VulkanDevice->m_LogicalDevice, &memAlloc, nullptr, &offscreen.memory));
				VK_CHECK_RESULT(vkBindImageMemory(m_VulkanDevice->m_LogicalDevice, offscreen.image, offscreen.memory, 0));

				VkImageViewCreateInfo colorImageView = {};
				colorImageView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
				colorImageView.format = format;
				colorImageView.flags = 0;
				colorImageView.subresourceRange = {};
				colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				colorImageView.subresourceRange.baseMipLevel = 0;
				colorImageView.subresourceRange.levelCount = 1;
				colorImageView.subresourceRange.baseArrayLayer = 0;
				colorImageView.subresourceRange.layerCount = 1;
				colorImageView.image = offscreen.image;
				VK_CHECK_RESULT(vkCreateImageView(m_VulkanDevice->m_LogicalDevice, &colorImageView, nullptr, &offscreen.view));

				VkFramebufferCreateInfo fbufCreateInfo = {};
				fbufCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				fbufCreateInfo.renderPass = renderpass;
				fbufCreateInfo.attachmentCount = 1;
				fbufCreateInfo.pAttachments = &offscreen.view;
				fbufCreateInfo.width = dim;
				fbufCreateInfo.height = dim;
				fbufCreateInfo.layers = 1;
				VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &fbufCreateInfo, nullptr, &offscreen.framebuffer));

				VkCommandBuffer layoutCmd = CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
				SetImageLayout(
					layoutCmd,
					offscreen.image,
					VK_IMAGE_ASPECT_COLOR_BIT,
					VK_IMAGE_LAYOUT_UNDEFINED,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
				FlushCommandBuffer(layoutCmd, m_GraphicsQueue, true);
			}

			ShaderID prefilterShaderID;
			if (!GetShaderID("prefilter", prefilterShaderID))
			{
				Logger::LogError("Failed to find prefilter shader!");
			}

			VDeleter<VkShaderModule> vertShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(m_Shaders[prefilterShaderID].shader.vertexShaderCode, vertShaderModule))
			{
				Logger::LogError("Failed to compile vertex shader located at: " + m_Shaders[prefilterShaderID].shader.vertexShaderFilePath);
			}

			VDeleter<VkShaderModule> fragShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(m_Shaders[prefilterShaderID].shader.fragmentShaderCode, fragShaderModule))
			{
				Logger::LogError("Failed to compile fragment shader located at: " + m_Shaders[prefilterShaderID].shader.fragmentShaderFilePath);
			}

			CreateUniformBuffers(&m_Shaders[prefilterShaderID]);

			VkDescriptorSet descriptorSet;
			DescriptorSetCreateInfo equirectangularToCubeDescriptorCreateInfo = {};
			equirectangularToCubeDescriptorCreateInfo.descriptorSet = &descriptorSet;
			equirectangularToCubeDescriptorCreateInfo.descriptorSetLayout = &m_DescriptorSetLayouts[prefilterShaderID];
			equirectangularToCubeDescriptorCreateInfo.shaderID = prefilterShaderID;
			equirectangularToCubeDescriptorCreateInfo.uniformBuffer = &m_Shaders[prefilterShaderID].uniformBuffer; 
			equirectangularToCubeDescriptorCreateInfo.cubemapTexture = m_LoadedMaterials[renderObject->materialID].cubemapTexture;
			CreateDescriptorSet(&equirectangularToCubeDescriptorCreateInfo);
			
			std::array<VkPushConstantRange, 1> pushConstantRanges = {};
			pushConstantRanges[0].offset = 0;
			pushConstantRanges[0].size = sizeof(Material::PushConstantBlock);
			pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			// Pipeline layout
			VkPipelineLayout pipelinelayout;
			VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
			pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutCreateInfo.setLayoutCount = 1;
			pipelineLayoutCreateInfo.pSetLayouts = &m_DescriptorSetLayouts[prefilterShaderID];
			pipelineLayoutCreateInfo.pushConstantRangeCount = pushConstantRanges.size();
			pipelineLayoutCreateInfo.pPushConstantRanges = pushConstantRanges.data();
			VK_CHECK_RESULT(vkCreatePipelineLayout(m_VulkanDevice->m_LogicalDevice, &pipelineLayoutCreateInfo, nullptr, &pipelinelayout));

			// Pipeline
			VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
			inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssemblyState.topology = renderObject->topology;
			inputAssemblyState.primitiveRestartEnable = VK_FALSE;
			inputAssemblyState.flags = 0;

			VkPipelineRasterizationStateCreateInfo rasterizationState = {};
			rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizationState.cullMode = renderObject->cullMode;
			rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rasterizationState.flags = 0;
			rasterizationState.depthClampEnable = VK_FALSE;
			rasterizationState.lineWidth = 1.0f;

			VkPipelineColorBlendAttachmentState blendAttachmentState = {};
			blendAttachmentState.colorWriteMask = 0xf;
			blendAttachmentState.blendEnable = VK_FALSE;

			VkPipelineColorBlendStateCreateInfo colorBlendState = {};
			colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlendState.attachmentCount = 1;
			colorBlendState.pAttachments = &blendAttachmentState;

			VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
			depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencilState.depthTestEnable = VK_FALSE;
			depthStencilState.depthWriteEnable = VK_FALSE;
			depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
			depthStencilState.front = depthStencilState.back;
			depthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;

			VkPipelineViewportStateCreateInfo viewportState = {};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.scissorCount = 1;
			viewportState.flags = 0;

			VkPipelineMultisampleStateCreateInfo multisampleState = {};
			multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			multisampleState.flags = 0;

			std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dynamicState = {}; (dynamicStateEnables);
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.pDynamicStates = dynamicStateEnables.data();
			dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());
			dynamicState.flags = 0;

			// Vertex input state
			VkVertexInputBindingDescription vertexInputBinding = {};
			vertexInputBinding.binding = 0;
			vertexInputBinding.stride = renderObject->vertexBufferData->VertexStride;
			vertexInputBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription vertexInputAttribute = {};
			vertexInputAttribute.location = 0;
			vertexInputAttribute.binding = 0;
			vertexInputAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
			vertexInputAttribute.offset = 0;

			VkPipelineVertexInputStateCreateInfo vertexInputState = {};
			vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputState.vertexBindingDescriptionCount = 1;
			vertexInputState.pVertexBindingDescriptions = &vertexInputBinding;
			vertexInputState.vertexAttributeDescriptionCount = 1;
			vertexInputState.pVertexAttributeDescriptions = &vertexInputAttribute;

			std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

			VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
			pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineCreateInfo.layout = pipelinelayout;
			pipelineCreateInfo.renderPass = renderpass;
			pipelineCreateInfo.flags = 0;
			pipelineCreateInfo.basePipelineIndex = -1;
			pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
			pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
			pipelineCreateInfo.pRasterizationState = &rasterizationState;
			pipelineCreateInfo.pColorBlendState = &colorBlendState;
			pipelineCreateInfo.pMultisampleState = &multisampleState;
			pipelineCreateInfo.pViewportState = &viewportState;
			pipelineCreateInfo.pDepthStencilState = &depthStencilState;
			pipelineCreateInfo.pDynamicState = &dynamicState;
			pipelineCreateInfo.stageCount = 2;
			pipelineCreateInfo.pStages = shaderStages.data();
			pipelineCreateInfo.pVertexInputState = &vertexInputState;
			pipelineCreateInfo.renderPass = renderpass;

			shaderStages[0] = {};
			shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			shaderStages[0].module = vertShaderModule;
			shaderStages[0].pName = "main";

			shaderStages[1] = {};
			shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			shaderStages[1].module = fragShaderModule;
			shaderStages[1].pName = "main";

			VkPipeline pipeline;
			VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_VulkanDevice->m_LogicalDevice, m_PipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));

			// Render

			VkClearValue clearValues[1];
			clearValues[0].color = m_ClearColor;

			VkRenderPassBeginInfo renderPassBeginInfo = {};
			renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			// Reuse render pass from example pass
			renderPassBeginInfo.renderPass = renderpass;
			renderPassBeginInfo.framebuffer = offscreen.framebuffer;
			renderPassBeginInfo.renderArea.extent.width = dim;
			renderPassBeginInfo.renderArea.extent.height = dim;
			renderPassBeginInfo.clearValueCount = 1;
			renderPassBeginInfo.pClearValues = clearValues;

			VkCommandBuffer cmdBuf = CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			VkViewport viewport = {};
			viewport.width = (float)dim;
			viewport.height = (float)dim;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;

			VkRect2D scissor = {};
			scissor.extent.width = dim;
			scissor.extent.height = dim;
			scissor.offset.x = 0;
			scissor.offset.y = 0;

			vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
			vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = mipLevels;
			subresourceRange.layerCount = 6;

			// Change image layout for all cubemap faces to transfer destination
			SetImageLayout(
				cmdBuf,
				m_LoadedMaterials[renderObject->materialID].prefilterTexture,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);

			for (uint32_t mip = 0; mip < mipLevels; ++mip) 
			{
				for (uint32_t face = 0; face < 6; ++face) 
				{
					viewport.width = static_cast<float>(dim * std::pow(0.5f, mip));
					viewport.height = static_cast<float>(dim * std::pow(0.5f, mip));
					vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

					vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

					// Push constants
					m_LoadedMaterials[renderObject->materialID].material.pushConstantBlock.mvp =
						glm::perspective(PI_DIV_TWO, 1.0f, 0.1f, (float)dim) * m_CaptureViews[face];
					vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Material::PushConstantBlock),
						&m_LoadedMaterials[renderObject->materialID].material.pushConstantBlock);

					vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

					BindDescriptorSet(&m_Shaders[prefilterShaderID], renderObject->renderID, cmdBuf, pipelinelayout, descriptorSet);

					VkDeviceSize offsets[1] = { 0 };

					vkCmdBindVertexBuffers(cmdBuf, 0, 1, &m_VertexIndexBufferPairs[m_LoadedMaterials[renderObject->materialID].material.shaderID].vertexBuffer->m_Buffer, offsets);
					if (renderObject->indexed)
					{
						vkCmdBindIndexBuffer(cmdBuf, m_VertexIndexBufferPairs[m_LoadedMaterials[renderObject->materialID].material.shaderID].indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
						vkCmdDrawIndexed(cmdBuf, m_VertexIndexBufferPairs[m_LoadedMaterials[renderObject->materialID].material.shaderID].indexCount, 1, 0, 0, 0);
					}
					else
					{
						vkCmdDraw(cmdBuf, m_VertexIndexBufferPairs[m_LoadedMaterials[renderObject->materialID].material.shaderID].vertexCount, 1, 0, 0);
					}

					vkCmdEndRenderPass(cmdBuf);

					SetImageLayout(
						cmdBuf,
						offscreen.image,
						VK_IMAGE_ASPECT_COLOR_BIT,
						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
						VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

					// Copy region for transfer from framebuffer to cube face
					VkImageCopy copyRegion = {};

					copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					copyRegion.srcSubresource.baseArrayLayer = 0;
					copyRegion.srcSubresource.mipLevel = 0;
					copyRegion.srcSubresource.layerCount = 1;
					copyRegion.srcOffset = { 0, 0, 0 };

					copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					copyRegion.dstSubresource.baseArrayLayer = face;
					copyRegion.dstSubresource.mipLevel = mip;
					copyRegion.dstSubresource.layerCount = 1;
					copyRegion.dstOffset = { 0, 0, 0 };

					copyRegion.extent.width = static_cast<uint32_t>(viewport.width);
					copyRegion.extent.height = static_cast<uint32_t>(viewport.height);
					copyRegion.extent.depth = 1;

					vkCmdCopyImage(
						cmdBuf,
						offscreen.image,
						VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						m_LoadedMaterials[renderObject->materialID].prefilterTexture->image,
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						1,
						&copyRegion);

					// Transform framebuffer color attachment back 
					SetImageLayout(
						cmdBuf,
						offscreen.image,
						VK_IMAGE_ASPECT_COLOR_BIT,
						VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
				}
			}

			SetImageLayout(
				cmdBuf,
				m_LoadedMaterials[renderObject->materialID].prefilterTexture,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				subresourceRange);

			FlushCommandBuffer(cmdBuf, m_GraphicsQueue, true);

			vkDestroyRenderPass(m_VulkanDevice->m_LogicalDevice, renderpass, nullptr);
			vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, offscreen.framebuffer, nullptr);
			vkFreeMemory(m_VulkanDevice->m_LogicalDevice, offscreen.memory, nullptr);
			vkDestroyImageView(m_VulkanDevice->m_LogicalDevice, offscreen.view, nullptr);
			vkDestroyImage(m_VulkanDevice->m_LogicalDevice, offscreen.image, nullptr);
			vkDestroyPipeline(m_VulkanDevice->m_LogicalDevice, pipeline, nullptr);
			vkDestroyPipelineLayout(m_VulkanDevice->m_LogicalDevice, pipelinelayout, nullptr);
		}

		void VulkanRenderer::GenerateBRDFLUT(const GameContext& gameContext, VulkanRenderObject* renderObject)
		{
			UNREFERENCED_PARAMETER(gameContext);

			const VkFormat format = VK_FORMAT_R16G16_SFLOAT;
			const uint32_t dim = (uint32_t)m_LoadedMaterials[renderObject->materialID].material.generatedBRDFLUTSize.x;

			// Color attachment
			VkAttachmentDescription attachmentDesc = {};
			attachmentDesc.format = format;
			attachmentDesc.samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachmentDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachmentDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

			VkSubpassDescription subpassDescription = {};
			subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpassDescription.colorAttachmentCount = 1;
			subpassDescription.pColorAttachments = &colorReference;

			// Use subpass dependencies for layout transitions
			std::array<VkSubpassDependency, 2> dependencies;
			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			// Create the actual renderpass
			VkRenderPassCreateInfo renderPassCreateInfo = {};
			renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassCreateInfo.attachmentCount = 1;
			renderPassCreateInfo.pAttachments = &attachmentDesc;
			renderPassCreateInfo.subpassCount = 1;
			renderPassCreateInfo.pSubpasses = &subpassDescription;
			renderPassCreateInfo.dependencyCount = dependencies.size();
			renderPassCreateInfo.pDependencies = dependencies.data();

			VkRenderPass renderpass;
			VK_CHECK_RESULT(vkCreateRenderPass(m_VulkanDevice->m_LogicalDevice, &renderPassCreateInfo, nullptr, &renderpass));

			VkFramebufferCreateInfo framebufferCreateInfo = {};
			framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferCreateInfo.renderPass = renderpass;
			framebufferCreateInfo.attachmentCount = 1;
			framebufferCreateInfo.pAttachments = &m_LoadedMaterials[renderObject->materialID].brdfLUT->imageView;
			framebufferCreateInfo.width = dim;
			framebufferCreateInfo.height = dim;
			framebufferCreateInfo.layers = 1;

			VkFramebuffer framebuffer;
			VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &framebufferCreateInfo, nullptr, &framebuffer));

			// Desriptors
			VkDescriptorSetLayout descriptorsetlayout;
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {};

			VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};
			descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorSetLayoutCreateInfo.pBindings = setLayoutBindings.data();
			descriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_VulkanDevice->m_LogicalDevice, &descriptorSetLayoutCreateInfo, nullptr, &descriptorsetlayout));

			// Descriptor Pool
			std::array<VkDescriptorPoolSize, 1> poolSizes;
			poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSizes[0].descriptorCount = 1;

			VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
			descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			descriptorPoolCreateInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
			descriptorPoolCreateInfo.pPoolSizes = poolSizes.data();
			descriptorPoolCreateInfo.maxSets = 2;
			VkDescriptorPool descriptorPool;
			VK_CHECK_RESULT(vkCreateDescriptorPool(m_VulkanDevice->m_LogicalDevice, &descriptorPoolCreateInfo, nullptr, &descriptorPool));

			// Descriptor sets
			VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {};
			descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorSetAllocateInfo.descriptorPool = descriptorPool;
			descriptorSetAllocateInfo.pSetLayouts = &descriptorsetlayout;
			descriptorSetAllocateInfo.descriptorSetCount = 1;
			VkDescriptorSet descriptorSet;
			VK_CHECK_RESULT(vkAllocateDescriptorSets(m_VulkanDevice->m_LogicalDevice, &descriptorSetAllocateInfo, &descriptorSet));

			// Pipeline layout
			VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
			pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutCreateInfo.setLayoutCount = 1;
			pipelineLayoutCreateInfo.pSetLayouts = &descriptorsetlayout;
			VkPipelineLayout pipelinelayout;
			VK_CHECK_RESULT(vkCreatePipelineLayout(m_VulkanDevice->m_LogicalDevice, &pipelineLayoutCreateInfo, nullptr, &pipelinelayout));

			// Pipeline
			VkPipelineInputAssemblyStateCreateInfo pipelineInputAssemblyStateCreateInfo = {};
			pipelineInputAssemblyStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			pipelineInputAssemblyStateCreateInfo.topology = renderObject->topology;
			pipelineInputAssemblyStateCreateInfo.flags = 0;
			pipelineInputAssemblyStateCreateInfo.primitiveRestartEnable = VK_FALSE;

			VkPipelineRasterizationStateCreateInfo pipelineRasterizationStateCreateInfo = {};
			pipelineRasterizationStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			pipelineRasterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
			pipelineRasterizationStateCreateInfo.cullMode = VK_CULL_MODE_NONE;
			pipelineRasterizationStateCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			pipelineRasterizationStateCreateInfo.flags = 0;
			pipelineRasterizationStateCreateInfo.depthClampEnable = VK_FALSE;
			pipelineRasterizationStateCreateInfo.lineWidth = 1.0f;

			VkPipelineColorBlendAttachmentState pipelineColorBlendAttachmentState = {};
			pipelineColorBlendAttachmentState.colorWriteMask = 0xf;
			pipelineColorBlendAttachmentState.blendEnable = VK_FALSE;

			VkPipelineColorBlendStateCreateInfo pipelineColorBlendStateCreateInfo = {};
			pipelineColorBlendStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			pipelineColorBlendStateCreateInfo.attachmentCount = 1;
			pipelineColorBlendStateCreateInfo.pAttachments = &pipelineColorBlendAttachmentState;

			VkPipelineDepthStencilStateCreateInfo pipelineDepthStencilStateCreateInfo = {};
			pipelineDepthStencilStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			pipelineDepthStencilStateCreateInfo.depthTestEnable = VK_FALSE;
			pipelineDepthStencilStateCreateInfo.depthWriteEnable = VK_FALSE;
			pipelineDepthStencilStateCreateInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
			pipelineDepthStencilStateCreateInfo.front = pipelineDepthStencilStateCreateInfo.back;
			pipelineDepthStencilStateCreateInfo.back.compareOp = VK_COMPARE_OP_ALWAYS;

			VkPipelineViewportStateCreateInfo pipelineViewportStateCreateInfo = {};
			pipelineViewportStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			pipelineViewportStateCreateInfo.viewportCount = 1;
			pipelineViewportStateCreateInfo.scissorCount = 1;
			pipelineViewportStateCreateInfo.flags = 0;

			VkPipelineMultisampleStateCreateInfo pipelineMultisampleStateCreateInfo = {};
			pipelineMultisampleStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			pipelineMultisampleStateCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			pipelineMultisampleStateCreateInfo.flags = 0;

			std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

			VkPipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo{};
			pipelineDynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			pipelineDynamicStateCreateInfo.pDynamicStates = dynamicStateEnables.data();
			pipelineDynamicStateCreateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());
			pipelineDynamicStateCreateInfo.flags = 0;

			VkPipelineVertexInputStateCreateInfo emptyInputState = {};
			emptyInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

			std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

			VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
			pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineCreateInfo.layout = pipelinelayout;
			pipelineCreateInfo.renderPass = renderpass;
			pipelineCreateInfo.flags = 0;
			pipelineCreateInfo.basePipelineIndex = -1;
			pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
			pipelineCreateInfo.pInputAssemblyState = &pipelineInputAssemblyStateCreateInfo;
			pipelineCreateInfo.pRasterizationState = &pipelineRasterizationStateCreateInfo;
			pipelineCreateInfo.pColorBlendState = &pipelineColorBlendStateCreateInfo;
			pipelineCreateInfo.pMultisampleState = &pipelineMultisampleStateCreateInfo;
			pipelineCreateInfo.pViewportState = &pipelineViewportStateCreateInfo;
			pipelineCreateInfo.pDepthStencilState = &pipelineDepthStencilStateCreateInfo;
			pipelineCreateInfo.pDynamicState = &pipelineDynamicStateCreateInfo;
			pipelineCreateInfo.stageCount = shaderStages.size();
			pipelineCreateInfo.pStages = shaderStages.data();
			pipelineCreateInfo.pVertexInputState = &emptyInputState;

			ShaderID brdfShaderID;
			if (!GetShaderID("brdf", brdfShaderID))
			{
				Logger::LogError("Failed to find brdf shader!");
			}

			VDeleter<VkShaderModule> vertShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(m_Shaders[brdfShaderID].shader.vertexShaderCode, vertShaderModule))
			{
				Logger::LogError("Failed to compile vertex shader located at: " + m_Shaders[brdfShaderID].shader.vertexShaderFilePath);
			}

			VDeleter<VkShaderModule> fragShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(m_Shaders[brdfShaderID].shader.fragmentShaderCode, fragShaderModule))
			{
				Logger::LogError("Failed to compile fragment shader located at: " + m_Shaders[brdfShaderID].shader.fragmentShaderFilePath);
			}

			shaderStages[0] = {};
			shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			shaderStages[0].module = vertShaderModule;
			shaderStages[0].pName = "main";

			shaderStages[1] = {};
			shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			shaderStages[1].module = fragShaderModule;
			shaderStages[1].pName = "main";

			VkPipeline pipeline;
			VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_VulkanDevice->m_LogicalDevice, m_PipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));

			// Render

			VkClearValue clearValues[1];
			clearValues[0].color = m_ClearColor;

			VkRenderPassBeginInfo renderPassBeginInfo = {};
			renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassBeginInfo.renderPass = renderpass;
			renderPassBeginInfo.renderArea.extent.width = dim;
			renderPassBeginInfo.renderArea.extent.height = dim;
			renderPassBeginInfo.clearValueCount = 1;
			renderPassBeginInfo.pClearValues = clearValues;
			renderPassBeginInfo.framebuffer = framebuffer;

			VkCommandBuffer cmdBuf = CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
			vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = {};
			viewport.width = (float)dim;
			viewport.height = (float)dim;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;

			VkRect2D scissor = {};
			scissor.extent = { dim, dim };
			scissor.offset = { 0 , 0 };

			vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
			vkCmdSetScissor(cmdBuf, 0, 1, &scissor);
			vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			vkCmdDraw(cmdBuf, 3, 1, 0, 0);
			vkCmdEndRenderPass(cmdBuf);
			FlushCommandBuffer(cmdBuf, m_GraphicsQueue, true); 

			vkQueueWaitIdle(m_GraphicsQueue);

			vkDestroyPipeline(m_VulkanDevice->m_LogicalDevice, pipeline, nullptr);
			vkDestroyPipelineLayout(m_VulkanDevice->m_LogicalDevice, pipelinelayout, nullptr);
			vkDestroyRenderPass(m_VulkanDevice->m_LogicalDevice, renderpass, nullptr);
			vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, framebuffer, nullptr);
			vkDestroyDescriptorSetLayout(m_VulkanDevice->m_LogicalDevice, descriptorsetlayout, nullptr);
			vkDestroyDescriptorPool(m_VulkanDevice->m_LogicalDevice, descriptorPool, nullptr);
		}

		MaterialID VulkanRenderer::InitializeMaterial(const GameContext& gameContext, const MaterialCreateInfo* createInfo)
		{
			UNREFERENCED_PARAMETER(gameContext);

			VulkanMaterial mat = {};
			mat.material = {};

			if (!GetShaderID(createInfo->shaderName, mat.material.shaderID))
			{
				if (createInfo->shaderName.empty())
				{
					Logger::LogError("Material's shader not set! MaterialCreateInfo::shaderName must be filled in");
				}
				else
				{
					Logger::LogError("Material's shader not set! Shader name " + createInfo->shaderName + " not found");
				}
			}
			mat.material.name = createInfo->name;

			mat.material.diffuseTexturePath = createInfo->diffuseTexturePath;
			mat.material.generateDiffuseSampler = createInfo->generateDiffuseSampler;
			mat.material.enableDiffuseSampler = createInfo->enableDiffuseSampler;

			mat.material.normalTexturePath = createInfo->normalTexturePath;
			mat.material.generateNormalSampler = createInfo->generateNormalSampler;
			mat.material.enableNormalSampler = createInfo->enableNormalSampler;

			mat.material.specularTexturePath = createInfo->specularTexturePath;
			mat.material.generateSpecularSampler = createInfo->generateSpecularSampler;
			mat.material.enableSpecularSampler = createInfo->enableSpecularSampler;

			mat.material.frameBuffers = createInfo->frameBuffers;

			mat.material.enableCubemapSampler = createInfo->enableCubemapSampler;
			mat.material.generateCubemapSampler = createInfo->generateCubemapSampler;
			mat.material.cubemapSamplerSize = createInfo->generatedCubemapSize;
			mat.material.cubeMapFilePaths = createInfo->cubeMapFilePaths;

			mat.material.constAlbedo = glm::vec4(createInfo->constAlbedo, 0);
			mat.material.generateAlbedoSampler = createInfo->generateAlbedoSampler;
			mat.material.albedoTexturePath = createInfo->albedoTexturePath;
			mat.material.enableAlbedoSampler = createInfo->enableAlbedoSampler;

			mat.material.constMetallic = createInfo->constMetallic;
			mat.material.generateMetallicSampler = createInfo->generateMetallicSampler;
			mat.material.metallicTexturePath = createInfo->metallicTexturePath;
			mat.material.enableMetallicSampler = createInfo->enableMetallicSampler;

			mat.material.constRoughness = createInfo->constRoughness;
			mat.material.generateRoughnessSampler = createInfo->generateRoughnessSampler;
			mat.material.roughnessTexturePath = createInfo->roughnessTexturePath;
			mat.material.enableRoughnessSampler = createInfo->enableRoughnessSampler;

			mat.material.constAO = createInfo->constAO;
			mat.material.generateAOSampler = createInfo->generateAOSampler;
			mat.material.aoTexturePath = createInfo->aoTexturePath;
			mat.material.enableAOSampler = createInfo->enableAOSampler;

			mat.material.enableHDREquirectangularSampler = createInfo->enableHDREquirectangularSampler;
			mat.material.generateHDREquirectangularSampler = createInfo->generateHDREquirectangularSampler;
			mat.material.hdrEquirectangularTexturePath = createInfo->hdrEquirectangularTexturePath;

			mat.material.generateHDRCubemapSampler = createInfo->generateHDRCubemapSampler;

			mat.material.enableIrradianceSampler = createInfo->enableIrradianceSampler;
			mat.material.generateIrradianceSampler = createInfo->generateIrradianceSampler;
			mat.material.irradianceSamplerSize = createInfo->generatedIrradianceCubemapSize;

			if (m_Shaders[mat.material.shaderID].shader.needIrradianceSampler)
			{
				mat.irradianceTexture = (createInfo->irradianceSamplerMatID < m_LoadedMaterials.size() ?
					m_LoadedMaterials[createInfo->irradianceSamplerMatID].irradianceTexture : nullptr);
			}
			if (m_Shaders[mat.material.shaderID].shader.needBRDFLUT)
			{
				mat.brdfLUT = (createInfo->brdfLUTSamplerMatID < m_LoadedMaterials.size() ?
					m_LoadedMaterials[createInfo->brdfLUTSamplerMatID].brdfLUT : nullptr);
			}
			if (m_Shaders[mat.material.shaderID].shader.needPrefilteredMap)
			{
				mat.prefilterTexture = (createInfo->prefilterMapSamplerMatID < m_LoadedMaterials.size() ?
					m_LoadedMaterials[createInfo->prefilterMapSamplerMatID].prefilterTexture : nullptr);
			}

			mat.material.enablePrefilteredMap = createInfo->enablePrefilteredMap;
			mat.material.generatePrefilteredMap = createInfo->generatePrefilteredMap;
			mat.material.prefilteredMapSize = createInfo->generatedPrefilteredCubemapSize;

			mat.material.enableBRDFLUT = createInfo->enableBRDFLUT;
			mat.material.generateBRDFLUT = createInfo->generateBRDFLUT;
			mat.material.generatedBRDFLUTSize = createInfo->generatedBRDFLUTSize;

			mat.descriptorSetLayoutIndex = mat.material.shaderID;

			struct TextureInfo
			{
				std::string filePath;
				VulkanTexture** texture = nullptr;
				bool* generate;
				VkFormat format;
				glm::uint mipLevels;
				VulkanTextureCreateFunction createFunction;
			};

			TextureInfo textureInfos[] =
			{
				{ createInfo->diffuseTexturePath, &mat.diffuseTexture, &mat.material.generateDiffuseSampler, VK_FORMAT_R8G8B8A8_UNORM, 1, &VulkanRenderer::CreateVulkanTexture },
				{ createInfo->normalTexturePath, &mat.normalTexture, &mat.material.generateNormalSampler, VK_FORMAT_R8G8B8A8_UNORM, 1, &VulkanRenderer::CreateVulkanTexture },
				{ createInfo->specularTexturePath, &mat.specularTexture, &mat.material.generateSpecularSampler, VK_FORMAT_R8G8B8A8_UNORM, 1, &VulkanRenderer::CreateVulkanTexture },
				{ createInfo->albedoTexturePath, &mat.albedoTexture, &mat.material.generateAlbedoSampler, VK_FORMAT_R8G8B8A8_UNORM, 1, &VulkanRenderer::CreateVulkanTexture },
				{ createInfo->metallicTexturePath, &mat.metallicTexture, &mat.material.generateMetallicSampler, VK_FORMAT_R8G8B8A8_UNORM, 1, &VulkanRenderer::CreateVulkanTexture },
				{ createInfo->roughnessTexturePath, &mat.roughnessTexture, &mat.material.generateRoughnessSampler, VK_FORMAT_R8G8B8A8_UNORM, 1, &VulkanRenderer::CreateVulkanTexture },
				{ createInfo->aoTexturePath, &mat.aoTexture, &mat.material.generateAOSampler, VK_FORMAT_R8G8B8A8_UNORM, 1, &VulkanRenderer::CreateVulkanTexture },
				{ createInfo->hdrEquirectangularTexturePath, &mat.hdrEquirectangularTexture, &mat.material.generateHDREquirectangularSampler, VK_FORMAT_R32G32B32A32_SFLOAT, 1, &VulkanRenderer::CreateVulkanTexture_HDR },
			};
			const size_t textureCount = sizeof(textureInfos) / sizeof(textureInfos[0]);

			// Calculate how many textures need to be allocated to prevent texture vector from resizing
			const size_t usedTextureCount = createInfo->generateAlbedoSampler + createInfo->generateAOSampler + createInfo->generateBRDFLUT + createInfo->generateCubemapSampler + createInfo->generateDiffuseSampler + createInfo->generateIrradianceSampler + createInfo->generateMetallicSampler + createInfo->generateNormalSampler + createInfo->generatePrefilteredMap + createInfo->generateRoughnessSampler + createInfo->generateSpecularSampler + createInfo->generateHDREquirectangularSampler; 
			m_LoadedTextures.reserve(usedTextureCount);

			for (TextureInfo& textureInfo : textureInfos)
			{
				if (!textureInfo.filePath.empty() && textureInfo.generate)
				{
					*textureInfo.texture = GetLoadedTexture(textureInfo.filePath);

					if (*textureInfo.texture == nullptr)
					{
						// Texture hasn't been loaded yet, load it now
						std::invoke(textureInfo.createFunction, this, textureInfo.filePath, textureInfo.format, textureInfo.mipLevels, textureInfo.texture);
						m_LoadedTextures.push_back(*textureInfo.texture);

						(*textureInfo.texture)->imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
						(*textureInfo.texture)->UpdateImageDescriptor();
					}
				}
			}

			// Cubemaps are treated differently than regular textures because they require 6 filepaths
			if (mat.material.generateCubemapSampler)
			{
				if (createInfo->cubeMapFilePaths[0].empty())
				{
					// Cubemap is needed, but doesn't need to loaded from file
					const glm::uint mipLevels = static_cast<uint32_t>(floor(log2(createInfo->generatedCubemapSize.x))) + 1;
					CreateVulkanCubemap_Empty(createInfo->generatedCubemapSize.x, createInfo->generatedCubemapSize.y, 4, mipLevels, createInfo->enableCubemapTrilinearFiltering, VK_FORMAT_R8G8B8A8_UNORM, &mat.cubemapTexture);
					m_LoadedTextures.push_back(mat.cubemapTexture);
				}
				else
				{
					mat.cubemapTexture = GetLoadedTexture(createInfo->cubeMapFilePaths[0]);

					if (mat.cubemapTexture == nullptr)
					{
						CreateVulkanCubemap(createInfo->cubeMapFilePaths, VK_FORMAT_R8G8B8A8_UNORM, &mat.cubemapTexture, true);
						m_LoadedTextures.push_back(mat.cubemapTexture);
					}
				}
			}

			if (mat.material.generateHDRCubemapSampler)
			{
				const glm::uint mipLevels = static_cast<uint32_t>(floor(log2(createInfo->generatedCubemapSize.x))) + 1;
				CreateVulkanCubemap_Empty(createInfo->generatedCubemapSize.x, createInfo->generatedCubemapSize.y, 4, mipLevels, false, VK_FORMAT_R32G32B32A32_SFLOAT, &mat.cubemapTexture);
				m_LoadedTextures.push_back(mat.cubemapTexture);
			}

			if (m_Shaders[mat.material.shaderID].shader.needCubemapSampler)
			{
				
			}

			if (mat.material.generateBRDFLUT)
			{
				CreateVulkanTexture_Empty(createInfo->generatedBRDFLUTSize.x, createInfo->generatedBRDFLUTSize.y, VK_FORMAT_R16G16_SFLOAT, 1, &mat.brdfLUT);
				m_LoadedTextures.push_back(mat.brdfLUT);
			}

			if (m_Shaders[mat.material.shaderID].shader.needBRDFLUT)
			{
				
			}

			if (mat.material.generateIrradianceSampler)
			{
				const glm::uint mipLevels = static_cast<uint32_t>(floor(log2(createInfo->generatedIrradianceCubemapSize.x))) + 1;
				CreateVulkanCubemap_Empty(createInfo->generatedIrradianceCubemapSize.x, createInfo->generatedIrradianceCubemapSize.y, 4, mipLevels, false, VK_FORMAT_R32G32B32A32_SFLOAT, &mat.irradianceTexture);
				m_LoadedTextures.push_back(mat.irradianceTexture);
			}

			if (m_Shaders[mat.material.shaderID].shader.needIrradianceSampler)
			{
				
			}

			if (mat.material.generatePrefilteredMap)
			{
				const glm::uint mipLevels = static_cast<uint32_t>(floor(log2(createInfo->generatedPrefilteredCubemapSize.x))) + 1;
				CreateVulkanCubemap_Empty(createInfo->generatedPrefilteredCubemapSize.x, createInfo->generatedPrefilteredCubemapSize.y, 4, mipLevels, true, VK_FORMAT_R16G16B16A16_SFLOAT, &mat.prefilterTexture);
				m_LoadedTextures.push_back(mat.prefilterTexture);
			}

			if (m_Shaders[mat.material.shaderID].shader.needPrefilteredMap)
			{
				
			}

			m_LoadedMaterials.push_back(mat);

			return m_LoadedMaterials.size() - 1;
		}

		glm::uint VulkanRenderer::InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo)
		{
			UNREFERENCED_PARAMETER(gameContext);

			RenderID renderID = GetFirstAvailableRenderID();
			VulkanRenderObject* renderObject = new VulkanRenderObject(m_VulkanDevice->m_LogicalDevice, renderID);
			InsertNewRenderObject(renderObject);

			renderObject->vertexBufferData = createInfo->vertexBufferData;
			renderObject->materialID = createInfo->materialID;
			renderObject->cullMode = CullFaceToVkCullMode(createInfo->cullFace);
			
			renderObject->name = createInfo->name;
			renderObject->materialName = m_LoadedMaterials[renderObject->materialID].material.name;
			renderObject->transform = createInfo->transform;
			renderObject->visible = true;

			if (createInfo->indices != nullptr)
			{
				renderObject->indices = createInfo->indices;
				renderObject->indexed = true;
			}

			return renderID;
		}

		void VulkanRenderer::CreateUniformBuffers(VulkanShader* shader)
		{
			shader->uniformBuffer.constantData.size = shader->shader.constantBufferUniforms.CalculateSize(m_PointLights.size());
			if (shader->uniformBuffer.constantData.size > 0)
			{
				if (shader->uniformBuffer.constantData.data) free(shader->uniformBuffer.constantData.data);
				
				shader->uniformBuffer.constantData.data = (float*)malloc(shader->uniformBuffer.constantData.size);
				assert(shader->uniformBuffer.constantData.data);

				PrepareUniformBuffer(&shader->uniformBuffer.constantBuffer, shader->uniformBuffer.constantData.size,
					VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			}

			shader->uniformBuffer.dynamicData.size = shader->shader.dynamicBufferUniforms.CalculateSize(m_PointLights.size());
			if (shader->uniformBuffer.dynamicData.size > 0 && m_RenderObjects.size() > 0)
			{
				if (shader->uniformBuffer.dynamicData.data) _aligned_free(shader->uniformBuffer.dynamicData.data);

				const size_t dynamicBufferSize = AllocateUniformBuffer(
					shader->uniformBuffer.dynamicData.size * m_RenderObjects.size(), (void**)&shader->uniformBuffer.dynamicData.data);
				if (dynamicBufferSize > 0)
				{
					PrepareUniformBuffer(&shader->uniformBuffer.dynamicBuffer, dynamicBufferSize,
						VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
				}
			}
		}

		void VulkanRenderer::SetTopologyMode(RenderID renderID, TopologyMode topology)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject) return;

			VkPrimitiveTopology vkTopology = TopologyModeToVkPrimitiveTopology(topology);

			if (vkTopology == VK_PRIMITIVE_TOPOLOGY_MAX_ENUM)
			{
				Logger::LogError("Unsupported TopologyMode passed to VulkanRenderer::SetTopologyMode: " + (int)topology);
			}
			else
			{
				renderObject->topology = vkTopology;
			}
		}

		void VulkanRenderer::SetClearColor(float r, float g, float b)
		{
			m_ClearColor = { r, g, b, 1.0f };
		}

		void VulkanRenderer::Update(const GameContext& gameContext)
		{
			// Update uniform buffer
			UpdateConstantUniformBuffers(gameContext);

			// TODO: Only update when things have changed
			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				UpdateDynamicUniformBuffer(gameContext, i);
			}

			// Update g-buffer uniforms
			UpdateDynamicUniformBuffer(gameContext, m_GBufferQuadRenderID);
		}

		void VulkanRenderer::Draw(const GameContext& gameContext)
		{
			UNREFERENCED_PARAMETER(gameContext);

			BuildCommandBuffers(gameContext); // TODO: Only call this when objects change
			BuildDeferredCommandBuffer(gameContext); // TODO: Only call this once at startup?

			if (m_SwapChainNeedsRebuilding)
			{
				m_SwapChainNeedsRebuilding = false;
				RecreateSwapChain(gameContext.window);
			}
			else
			{
				DrawFrame(gameContext.window);
			}
		}

		void VulkanRenderer::DrawImGuiItems(const GameContext& gameContext)
		{
			// TODO: Consolidate renderer ImGui code
			if (ImGui::CollapsingHeader("Scene info"))
			{
				const std::string sceneCountStr("Scene count: " + std::to_string(gameContext.sceneManager->GetSceneCount()));
				ImGui::Text(sceneCountStr.c_str());
				const std::string currentSceneStr("Current scene: " + gameContext.sceneManager->CurrentScene()->GetName());
				ImGui::Text(currentSceneStr.c_str());
				const glm::uint objectCount = GetRenderObjectCount();
				const glm::uint objectCapacity = GetRenderObjectCapacity();
				const std::string objectCountStr("Object count/capacity: " + std::to_string(objectCount) + "/" + std::to_string(objectCapacity));
				ImGui::Text(objectCountStr.c_str());

				if (ImGui::TreeNode("Render Objects"))
				{
					for (size_t i = 0; i < m_RenderObjects.size(); ++i)
					{
						VulkanRenderObject* renderObject = GetRenderObject(i);
						VulkanMaterial* material = &m_LoadedMaterials[renderObject->materialID];
						VulkanShader* shader = &m_Shaders[material->material.shaderID];
						const std::string objectName(renderObject->name + "##" + std::to_string(i));

						const std::string objectID("##" + objectName + "-visble");
						ImGui::Checkbox(objectID.c_str(), &renderObject->visible);
						ImGui::SameLine();
						if (ImGui::TreeNode(objectName.c_str()))
						{
							static const char* localTransformStr = "Transform (local)";
							static const char* globalTransformStr = "Transform (global)";

							bool local = true;

							ImGui::Text(local ? localTransformStr : globalTransformStr);

							Transform* transform = renderObject->transform;


							glm::vec3 translation = local ? transform->GetLocalPosition() : transform->GetGlobalPosition();
							glm::vec3 rotation = glm::eulerAngles(local ? transform->GetLocalRotation() : transform->GetGlobalRotation());
							glm::vec3 scale = local ? transform->GetLocalScale() : transform->GetGlobalScale();

							ImGui::DragFloat3("Translation", &translation[0], 0.1f);
							ImGui::DragFloat3("Rotation", &rotation[0], 0.01f);
							ImGui::DragFloat3("Scale", &scale[0], 0.01f);

							if (local)
							{
								transform->SetLocalPosition(translation);
								transform->SetLocalRotation(glm::quat(rotation));
								transform->SetLocalScale(scale);
							}
							else
							{
								transform->SetGlobalPosition(translation);
								transform->SetGlobalRotation(glm::quat(rotation));
								transform->SetGlobalScale(scale);
							}

							if (shader->shader.needIrradianceSampler)
							{
								ImGui::Checkbox("Use Irradiance Sampler", &material->material.enableIrradianceSampler);
							}

							ImGui::TreePop();
						}
					}

					ImGui::TreePop();
				}

				if (ImGui::TreeNode("Lights"))
				{
					ImGui::AlignFirstTextHeightToWidgets();

					ImGuiColorEditFlags colorEditFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_RGB | ImGuiColorEditFlags_PickerHueWheel;

					bool dirLightEnabled = m_DirectionalLight.enabled;
					ImGui::Checkbox("##dir-light-enabled", &dirLightEnabled);
					m_DirectionalLight.enabled = dirLightEnabled ? 1 : 0;
					ImGui::SameLine();
					if (ImGui::TreeNode("Directional Light"))
					{
						ImGui::DragFloat3("Rotation", &m_DirectionalLight.direction.x, 0.01f);

						CopyableColorEdit4("Color ", m_DirectionalLight.color, "c##diffuse", "p##color", colorEditFlags);

						ImGui::TreePop();
					}

					for (size_t i = 0; i < m_PointLights.size(); ++i)
					{
						const std::string iStr = std::to_string(i);
						const std::string objectName("Point Light##" + iStr);

						bool pointLightEnabled = m_PointLights[i].enabled;
						ImGui::Checkbox(std::string("##enabled" + iStr).c_str(), &pointLightEnabled);
						m_PointLights[i].enabled = pointLightEnabled ? 1 : 0;
						ImGui::SameLine();
						if (ImGui::TreeNode(objectName.c_str()))
						{
							ImGui::DragFloat3("Translation", &m_PointLights[i].position.x, 0.1f);

							CopyableColorEdit4("Color ", m_PointLights[i].color, "c##diffuse", "p##color", colorEditFlags);

							ImGui::TreePop();
						}
					}

					ImGui::TreePop();
				}
			}
		}

		void VulkanRenderer::ReloadShaders(GameContext& gameContext)
		{
			// TODO: Implement
			UNREFERENCED_PARAMETER(gameContext);
		}

		void VulkanRenderer::OnWindowSize(int width, int height)
		{
			UNREFERENCED_PARAMETER(width);
			UNREFERENCED_PARAMETER(height);

			m_SwapChainNeedsRebuilding = true;
		}

		void VulkanRenderer::SetVSyncEnabled(bool enableVSync)
		{
			m_VSyncEnabled = enableVSync;
		}

		glm::uint VulkanRenderer::GetRenderObjectCount() const
		{
			glm::uint count = 0;

			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject != nullptr) ++count;
			}

			return count;
		}

		glm::uint VulkanRenderer::GetRenderObjectCapacity() const
		{
			return m_RenderObjects.size();
		}

		void VulkanRenderer::DescribeShaderVariable(RenderID renderID, const std::string& variableName, int size, Renderer::Type renderType, bool normalized, int stride, void* pointer)
		{
			// TODO: Implement
			UNREFERENCED_PARAMETER(renderID);
			UNREFERENCED_PARAMETER(variableName);
			UNREFERENCED_PARAMETER(size);
			UNREFERENCED_PARAMETER(renderType);
			UNREFERENCED_PARAMETER(normalized);
			UNREFERENCED_PARAMETER(stride);
			UNREFERENCED_PARAMETER(pointer);
		}

		void VulkanRenderer::Destroy(RenderID renderID)
		{
			for (auto iter = m_RenderObjects.begin(); iter != m_RenderObjects.end(); ++iter)
			{
				if (*iter && (*iter)->renderID == renderID)
				{
					vkFreeDescriptorSets(m_VulkanDevice->m_LogicalDevice, m_DescriptorPool, 1, &((*iter)->descriptorSet));

					SafeDelete(*iter);
					m_RenderObjects[renderID] = nullptr;
					return;
				}
			}
		}

		void VulkanRenderer::ImGui_Init(const GameContext& gameContext)
		{
			ImGuiIO& io = ImGui::GetIO();

			io.RenderDrawListsFn = NULL;

			glm::vec2i windowSize = gameContext.window->GetSize();
			glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();
			io.DisplaySize = ImVec2((float)windowSize.x, (float)windowSize.y);
			io.DisplayFramebufferScale = ImVec2(
				windowSize.x > 0 ? ((float)frameBufferSize.x / windowSize.x) : 0,
				windowSize.y > 0 ? ((float)frameBufferSize.y / windowSize.y) : 0);

			io.SetClipboardTextFn = SetClipboardText;
			io.GetClipboardTextFn = GetClipboardText;
			io.ClipboardUserData = gameContext.window;

			ImGui_InitResources();
		}

		void VulkanRenderer::ImGui_ReleaseRenderObjects()
		{
			ImGui_InvalidateDeviceObjects();
		}

		void VulkanRenderer::ImGui_NewFrame(const GameContext& gameContext)
		{
			ImGui::NewFrame();

			ImGuiIO& io = ImGui::GetIO();

			// Setup display size (every frame to accommodate for window resizing)
			glm::vec2i windowSize = gameContext.window->GetSize();
			glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();
			io.DisplaySize = ImVec2((float)windowSize.x, (float)windowSize.y);
			io.DisplayFramebufferScale = ImVec2(
				windowSize.x > 0 ? ((float)frameBufferSize.x / windowSize.x) : 0,
				windowSize.y > 0 ? ((float)frameBufferSize.y / windowSize.y) : 0);

			io.DeltaTime = gameContext.deltaTime;
		}

		void VulkanRenderer::ImGui_Render()
		{
			ImGui::Render();

			ImGui_UpdateBuffers();
		}

		void VulkanRenderer::ImGui_DrawFrame(VkCommandBuffer commandBuffer)
		{
			ImGuiIO& io = ImGui::GetIO();

			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ImGui_PipelineLayout, 0, 1, &m_ImGuiDescriptorSet, 0, nullptr);
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ImGui_GraphicsPipeline);

			ShaderID imGuiShaderID;
			if (!GetShaderID("imgui", imGuiShaderID))
			{
				Logger::LogError("Failed to find imgui shader!");
			}
			VertexIndexBufferPair& bufferPair = m_VertexIndexBufferPairs[imGuiShaderID];

			// Bind vertex and index buffer
			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &bufferPair.vertexBuffer->m_Buffer, offsets);
			vkCmdBindIndexBuffer(commandBuffer, bufferPair.indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT16);

			VkViewport viewport = {};
			viewport.x = 0.0f;
			viewport.y = 0.0f;
			viewport.width = (float)m_SwapChainExtent.width;
			viewport.height = (float)m_SwapChainExtent.height;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

			// UI scale and translate via push constants
			m_ImGuiPushConstBlock.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
			m_ImGuiPushConstBlock.translate = glm::vec2(-1.0f);
			vkCmdPushConstants(commandBuffer, m_ImGui_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ImGui_PushConstBlock), &m_ImGuiPushConstBlock);

			// Render commands
			ImDrawData* imDrawData = ImGui::GetDrawData();
			int32_t vertexOffset = 0;
			int32_t indexOffset = 0;
			for (int32_t i = 0; i < imDrawData->CmdListsCount; ++i)
			{
				const ImDrawList* cmd_list = imDrawData->CmdLists[i];
				for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; ++j)
				{
					const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[j];
					VkRect2D scissorRect;
					scissorRect.offset.x = std::max((int32_t)(pcmd->ClipRect.x), 0);
					scissorRect.offset.y = std::max((int32_t)(pcmd->ClipRect.y), 0);
					scissorRect.extent.width = (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x);
					scissorRect.extent.height = (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y);
					vkCmdSetScissor(commandBuffer, 0, 1, &scissorRect);
					vkCmdDrawIndexed(commandBuffer, pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);
					indexOffset += pcmd->ElemCount;
				}
				vertexOffset += cmd_list->VtxBuffer.Size;
			}
		}

		void VulkanRenderer::ImGui_InitResources()
		{
			m_ImGuiPushConstBlock = { glm::vec2(1.0f, 1.0f), glm::vec2(0.0f, 0.0f) };

			{
				ImGui_CreateFontsTexture(m_GraphicsQueue);

				DescriptorSetCreateInfo createInfo = {};
				createInfo.descriptorSet = &m_ImGuiDescriptorSet;
				createInfo.descriptorSetLayout = &m_DescriptorSetLayouts[m_IGuiShaderID];
				createInfo.diffuseTexture = m_ImGuiFontTexture;
				createInfo.shaderID = m_IGuiShaderID;
				createInfo.uniformBuffer = &m_Shaders[m_IGuiShaderID].uniformBuffer;

				CreateDescriptorSet(&createInfo);
			}

			VkPushConstantRange pushConstants[1] = {};
			pushConstants[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			pushConstants[0].offset = sizeof(float) * 0;
			pushConstants[0].size = sizeof(float) * 4;

			VertexBufferData vertexBufferData = {};
			vertexBufferData.Attributes =
				(glm::uint)VertexAttribute::POSITION_2D |
				(glm::uint)VertexAttribute::UV |
				(glm::uint)VertexAttribute::COLOR_R8G8B8A8_UNORM;
			vertexBufferData.VertexStride = CalculateVertexStride(vertexBufferData.Attributes);

			assert(vertexBufferData.VertexStride == sizeof(ImDrawVert));

			glm::uint descriptorSetLayoutIndex = m_IGuiShaderID;

			GraphicsPipelineCreateInfo pipelineCreateInfo = {};

			pipelineCreateInfo.shaderID = m_IGuiShaderID;
			pipelineCreateInfo.vertexAttributes = vertexBufferData.Attributes;
			pipelineCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			pipelineCreateInfo.cullMode = VK_CULL_MODE_NONE;
			pipelineCreateInfo.descriptorSetLayoutIndex = descriptorSetLayoutIndex;
			pipelineCreateInfo.setDynamicStates = true;
			pipelineCreateInfo.enabledColorBlending = true;
			pipelineCreateInfo.pushConstants = pushConstants;
			pipelineCreateInfo.pushConstantRangeCount = 1;
			pipelineCreateInfo.pipelineLayout = &m_ImGui_PipelineLayout;
			pipelineCreateInfo.grahpicsPipeline = &m_ImGui_GraphicsPipeline;
			pipelineCreateInfo.pipelineCache = &m_ImGuiPipelineCache;
			pipelineCreateInfo.renderPass = m_DeferredCombineRenderPass;
			pipelineCreateInfo.depthWriteEnable = VK_FALSE;
			pipelineCreateInfo.depthTestEnable = VK_FALSE;
			pipelineCreateInfo.subpass = 1;

			CreateGraphicsPipeline(&pipelineCreateInfo);
		}

		void VulkanRenderer::ImGui_InvalidateDeviceObjects()
		{
			SafeDelete(m_ImGuiFontTexture);

			if (m_ImGui_PipelineLayout)
			{
				vkDestroyPipelineLayout(m_VulkanDevice->m_LogicalDevice, m_ImGui_PipelineLayout, nullptr);
				m_ImGui_PipelineLayout = VK_NULL_HANDLE;
			}

			if (m_ImGui_GraphicsPipeline)
			{
				vkDestroyPipeline(m_VulkanDevice->m_LogicalDevice, m_ImGui_GraphicsPipeline, nullptr);
				m_ImGui_GraphicsPipeline = VK_NULL_HANDLE;
			}

			if (m_ImGuiPipelineCache)
			{
				vkDestroyPipelineCache(m_VulkanDevice->m_LogicalDevice, m_ImGuiPipelineCache, nullptr);
				m_ImGuiPipelineCache = VK_NULL_HANDLE;
			}
		}

		void VulkanRenderer::PostInitializeRenderObject(const GameContext& gameContext, RenderID renderID)
		{
			UNREFERENCED_PARAMETER(gameContext);
			UNREFERENCED_PARAMETER(renderID);

			//CreateDescriptorSet(renderID);
			//CreateGraphicsPipeline(renderID);

		}

		DirectionalLightID VulkanRenderer::InitializeDirectionalLight(const DirectionalLight& dirLight)
		{
			m_DirectionalLight = dirLight;
			return 0;
		}

		PointLightID VulkanRenderer::InitializePointLight(const PointLight& pointLight)
		{
			m_PointLights.push_back(pointLight);
			return m_PointLights.size() - 1;
		}

		Renderer::DirectionalLight& VulkanRenderer::GetDirectionalLight(DirectionalLightID dirLightID)
		{
			UNREFERENCED_PARAMETER(dirLightID);
			return m_DirectionalLight;
		}

		Renderer::PointLight& VulkanRenderer::GetPointLight(PointLightID pointLightID)
		{
			return m_PointLights[pointLightID];
		}

		VulkanRenderObject* VulkanRenderer::GetRenderObject(RenderID renderID)
		{
			return m_RenderObjects[renderID];
		}

		bool VulkanRenderer::ImGui_CreateFontsTexture(VkQueue copyQueue)
		{
			ImGuiIO& io = ImGui::GetIO();

			unsigned char* pixels;
			int textureWidth, textureHeight;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &textureWidth, &textureHeight);
			size_t uploadSize = textureWidth * textureHeight * 4 * sizeof(char);

			m_ImGuiFontTexture = new VulkanTexture(m_VulkanDevice->m_LogicalDevice);

			CreateImage(textureWidth, textureHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_UNDEFINED, &m_ImGuiFontTexture->image, &m_ImGuiFontTexture->imageMemory);

			CreateImageView(m_ImGuiFontTexture->image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, m_ImGuiFontTexture->mipLevels, &m_ImGuiFontTexture->imageView);

			// Staging buffers for font data upload
			VulkanBuffer stagingBuffer(m_VulkanDevice->m_LogicalDevice);

			CreateAndAllocateBuffer(
				uploadSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				&stagingBuffer);

			stagingBuffer.Map();
			memcpy(stagingBuffer.m_Mapped, pixels, uploadSize);
			stagingBuffer.Unmap();

			// Copy buffer data to font image
			VkCommandBuffer copyCmd = CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			// Prepare for transfer
			SetImageLayout(
				copyCmd,
				m_ImGuiFontTexture,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_HOST_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT);

			// Copy
			VkBufferImageCopy bufferCopyRegion = {};
			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bufferCopyRegion.imageSubresource.layerCount = 1;
			bufferCopyRegion.imageExtent.width = textureWidth;
			bufferCopyRegion.imageExtent.height = textureHeight;
			bufferCopyRegion.imageExtent.depth = 1;

			vkCmdCopyBufferToImage(
				copyCmd,
				stagingBuffer.m_Buffer,
				m_ImGuiFontTexture->image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&bufferCopyRegion
			);

			// Prepare for shader read
			SetImageLayout(
				copyCmd,
				m_ImGuiFontTexture,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

			FlushCommandBuffer(copyCmd, copyQueue, true);

			stagingBuffer.Destroy();

			CreateTextureSampler(m_ImGuiFontTexture, 1.0f, -1000.0f, 1000.0f);

			// Store our identifier
			io.Fonts->TexID = (void *)(intptr_t)&m_ImGuiFontTexture->image;

			return true;
		}

		uint32_t VulkanRenderer::ImGui_MemoryType(VkMemoryPropertyFlags properties, uint32_t type_bits)
		{
			VkPhysicalDeviceMemoryProperties prop;
			vkGetPhysicalDeviceMemoryProperties(m_VulkanDevice->m_PhysicalDevice, &prop);
			for (uint32_t i = 0; i < prop.memoryTypeCount; ++i)
				if ((prop.memoryTypes[i].propertyFlags & properties) == properties && type_bits & (1 << i))
					return i;
			return 0xffffffff; // Unable to find memoryType
		}

		RenderID VulkanRenderer::GetFirstAvailableRenderID() const
		{
			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				if (!m_RenderObjects[i]) return i;
			}

			return m_RenderObjects.size();
		}

		void VulkanRenderer::InsertNewRenderObject(VulkanRenderObject* renderObject)
		{
			if (renderObject->renderID < m_RenderObjects.size())
			{
				assert(m_RenderObjects[renderObject->renderID] == nullptr);
				m_RenderObjects[renderObject->renderID] = renderObject;
			}
			else
			{
				m_RenderObjects.push_back(renderObject);
			}
		}

		void VulkanRenderer::CreateInstance(const GameContext& gameContext)
		{
			if (m_EnableValidationLayers && !CheckValidationLayerSupport())
			{
				throw std::runtime_error("validation layers requested, but not available!");
			}

			VkApplicationInfo appInfo = {};
			appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
			std::string applicationName = gameContext.window->GetTitle();
			appInfo.pApplicationName = applicationName.c_str();
			appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
			appInfo.pEngineName = "Flex Engine";
			appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
			appInfo.apiVersion = VK_API_VERSION_1_0;

			Logger::LogInfo("Vulkan Version: 1.0.0");

			VkInstanceCreateInfo createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
			createInfo.pApplicationInfo = &appInfo;

			auto extensions = GetRequiredExtensions();
			createInfo.enabledExtensionCount = extensions.size();
			createInfo.ppEnabledExtensionNames = extensions.data();

			if (m_EnableValidationLayers)
			{
				createInfo.enabledLayerCount = m_ValidationLayers.size();
				createInfo.ppEnabledLayerNames = m_ValidationLayers.data();
			}
			else
			{
				createInfo.enabledLayerCount = 0;
			}

			VK_CHECK_RESULT(vkCreateInstance(&createInfo, nullptr, m_Instance.replace()));
		}

		void VulkanRenderer::SetupDebugCallback()
		{
			if (!m_EnableValidationLayers) return;

			VkDebugReportCallbackCreateInfoEXT createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
			createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
			createInfo.pfnCallback = DebugCallback;

			VK_CHECK_RESULT(CreateDebugReportCallbackEXT(m_Instance, &createInfo, nullptr, m_Callback.replace()));
		}

		void VulkanRenderer::CreateSurface(Window* window)
		{
			VK_CHECK_RESULT(glfwCreateWindowSurface(m_Instance, ((VulkanWindowWrapper*)window)->GetWindow(), nullptr, m_Surface.replace()));
		}

		VkPhysicalDevice VulkanRenderer::PickPhysicalDevice()
		{
			VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

			uint32_t deviceCount = 0;
			vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);

			if (deviceCount == 0)
			{
				throw std::runtime_error("Failed to find GPUs with Vulkan support!");
			}

			std::vector<VkPhysicalDevice> devices(deviceCount);
			vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

			for (const auto& device : devices)
			{
				if (IsDeviceSuitable(device))
				{
					physicalDevice = device;
					break;
				}
			}

			if (physicalDevice == VK_NULL_HANDLE)
			{
				throw std::runtime_error("Failed to find a suitable GPU!");
			}

			return physicalDevice;
		}

		void VulkanRenderer::CreateLogicalDevice(VkPhysicalDevice physicalDevice)
		{
			VulkanQueueFamilyIndices indices = FindQueueFamilies(physicalDevice);

			std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
			std::set<int> uniqueQueueFamilies = { indices.graphicsFamily, indices.presentFamily };

			float queuePriority = 1.0f;
			for (int queueFamily : uniqueQueueFamilies)
			{
				VkDeviceQueueCreateInfo queueCreateInfo = {};
				queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueCreateInfo.queueFamilyIndex = (glm::uint32)queueFamily;
				queueCreateInfo.queueCount = 1;
				queueCreateInfo.pQueuePriorities = &queuePriority;
				queueCreateInfos.push_back(queueCreateInfo);
			}

			VkPhysicalDeviceFeatures deviceFeatures = {};
			deviceFeatures.samplerAnisotropy = VK_TRUE;

			VkDeviceCreateInfo createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

			createInfo.pQueueCreateInfos = queueCreateInfos.data();
			createInfo.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();

			createInfo.pEnabledFeatures = &deviceFeatures;

			createInfo.enabledExtensionCount = m_DeviceExtensions.size();
			createInfo.ppEnabledExtensionNames = m_DeviceExtensions.data();

			if (m_EnableValidationLayers)
			{
				createInfo.enabledLayerCount = m_ValidationLayers.size();
				createInfo.ppEnabledLayerNames = m_ValidationLayers.data();
			}
			else
			{
				createInfo.enabledLayerCount = 0;
			}

			m_VulkanDevice = new VulkanDevice(physicalDevice);

			VK_CHECK_RESULT(vkCreateDevice(physicalDevice, &createInfo, nullptr, m_VulkanDevice->m_LogicalDevice.replace()));

			vkGetPhysicalDeviceProperties(physicalDevice, &m_VulkanDevice->m_PhysicalDeviceProperties);

			vkGetDeviceQueue(m_VulkanDevice->m_LogicalDevice, (glm::uint32)indices.graphicsFamily, 0, &m_GraphicsQueue);
			vkGetDeviceQueue(m_VulkanDevice->m_LogicalDevice, (glm::uint32)indices.presentFamily, 0, &m_PresentQueue);
		}

		void VulkanRenderer::RecreateSwapChain(Window* window)
		{
			vkDeviceWaitIdle(m_VulkanDevice->m_LogicalDevice);

			CreateSwapChain(window);
			CreateSwapChainImageViews();

			CreateDepthResources();

			PrepareOffscreenFrameBuffer(window);
			CreateRenderPass();

			CreateFramebuffers();
			CreateCommandBuffers();
		}

		void VulkanRenderer::CreateSwapChain(Window* window)
		{
			VulkanSwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(m_VulkanDevice->m_PhysicalDevice);

			VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
			VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
			VkExtent2D extent = ChooseSwapExtent(window, swapChainSupport.capabilities);

			uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
			if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount)
			{
				imageCount = swapChainSupport.capabilities.maxImageCount;
			}

			VkSwapchainCreateInfoKHR createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
			createInfo.surface = m_Surface;

			createInfo.minImageCount = imageCount;
			createInfo.imageFormat = surfaceFormat.format;
			createInfo.imageColorSpace = surfaceFormat.colorSpace;
			createInfo.imageExtent = extent;
			createInfo.imageArrayLayers = 1;
			createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

			VulkanQueueFamilyIndices indices = FindQueueFamilies(m_VulkanDevice->m_PhysicalDevice);
			uint32_t queueFamilyIndices[] = { (uint32_t)indices.graphicsFamily, (uint32_t)indices.presentFamily };

			if (indices.graphicsFamily != indices.presentFamily)
			{
				createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
				createInfo.queueFamilyIndexCount = 2;
				createInfo.pQueueFamilyIndices = queueFamilyIndices;
			}
			else
			{
				createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			}

			createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
			createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
			createInfo.presentMode = presentMode;
			createInfo.clipped = VK_TRUE;

			VkSwapchainKHR oldSwapChain = m_SwapChain;
			createInfo.oldSwapchain = oldSwapChain;

			VkSwapchainKHR newSwapChain;
			VK_CHECK_RESULT(vkCreateSwapchainKHR(m_VulkanDevice->m_LogicalDevice, &createInfo, nullptr, &newSwapChain));

			m_SwapChain = newSwapChain;

			vkGetSwapchainImagesKHR(m_VulkanDevice->m_LogicalDevice, m_SwapChain, &imageCount, nullptr);
			m_SwapChainImages.resize(imageCount);
			vkGetSwapchainImagesKHR(m_VulkanDevice->m_LogicalDevice, m_SwapChain, &imageCount, m_SwapChainImages.data());

			m_SwapChainImageFormat = surfaceFormat.format;
			m_SwapChainExtent = extent;
		}

		void VulkanRenderer::CreateSwapChainImageViews()
		{
			m_SwapChainImageViews.resize(m_SwapChainImages.size(), VDeleter<VkImageView>{ m_VulkanDevice->m_LogicalDevice, vkDestroyImageView });

			for (uint32_t i = 0; i < m_SwapChainImages.size(); ++i)
			{
				CreateImageView(m_SwapChainImages[i], m_SwapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1, m_SwapChainImageViews[i].replace());
			}
		}

		void VulkanRenderer::CreateTextureImageView(VulkanTexture* texture, VkFormat format) const
		{
			CreateImageView(texture->image, format, VK_IMAGE_ASPECT_COLOR_BIT, texture->mipLevels, texture->imageView.replace());
		}

		void VulkanRenderer::CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, glm::uint mipLevels, VkImageView* imageView) const
		{
			VkImageViewCreateInfo viewInfo = {};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = image;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = format;
			viewInfo.subresourceRange.aspectMask = aspectFlags;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = mipLevels;
			viewInfo.subresourceRange.baseArrayLayer = 0;
			viewInfo.subresourceRange.layerCount = 1;

			VK_CHECK_RESULT(vkCreateImageView(m_VulkanDevice->m_LogicalDevice, &viewInfo, nullptr, imageView));
		}

		void VulkanRenderer::CreateRenderPass()
		{
			VkAttachmentDescription colorAttachment = {};
			colorAttachment.format = m_SwapChainImageFormat;
			colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

			VkAttachmentReference colorAttachmentRef = {};
			colorAttachmentRef.attachment = 0;
			colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkAttachmentDescription depthAttachment = {};
			VkFormat depthFormat;
			GetSupportedDepthFormat(m_VulkanDevice->m_PhysicalDevice, &depthFormat);
			depthAttachment.format = depthFormat;
			depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkAttachmentReference depthAttachmentRef = {};
			depthAttachmentRef.attachment = 1;
			depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			std::array<VkSubpassDescription, 2> subpasses;
			// Deferred subpass
			subpasses[0] = {};
			subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpasses[0].colorAttachmentCount = 1;
			subpasses[0].pColorAttachments = &colorAttachmentRef;
			subpasses[0].pDepthStencilAttachment = &depthAttachmentRef;

			// Forward subpass
			subpasses[1] = {};
			subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpasses[1].colorAttachmentCount = 1;
			subpasses[1].pColorAttachments = &colorAttachmentRef;
			subpasses[1].pDepthStencilAttachment = &depthAttachmentRef;


			std::array<VkSubpassDependency, 3> dependencies;
			// Deferred subpass
			dependencies[0] = {};
			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = 0;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			// Forward subpass
			dependencies[1] = {};
			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = 1;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].srcAccessMask = 0;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			// Final transition
			dependencies[2] = {};
			dependencies[2].srcSubpass = 1;
			dependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[2].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[2].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

			std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = attachments.size();
			renderPassInfo.pAttachments = attachments.data();
			renderPassInfo.subpassCount = subpasses.size();
			renderPassInfo.pSubpasses = subpasses.data();
			renderPassInfo.dependencyCount = dependencies.size();
			renderPassInfo.pDependencies = dependencies.data();

			VK_CHECK_RESULT(vkCreateRenderPass(m_VulkanDevice->m_LogicalDevice, &renderPassInfo, nullptr, m_DeferredCombineRenderPass.replace()));
		}

		void VulkanRenderer::CreateDescriptorSet(RenderID renderID)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject) return;

			VulkanMaterial* material = &m_LoadedMaterials[renderObject->materialID];

			DescriptorSetCreateInfo createInfo = {};
			createInfo.descriptorSet = &renderObject->descriptorSet;
			createInfo.descriptorSetLayout = &m_DescriptorSetLayouts[material->descriptorSetLayoutIndex];
			createInfo.shaderID = material->material.shaderID;
			createInfo.uniformBuffer = &m_Shaders[m_LoadedMaterials[renderObject->materialID].material.shaderID].uniformBuffer;
			createInfo.diffuseTexture = material->diffuseTexture;
			createInfo.normalTexture = material->normalTexture;
			createInfo.specularTexture = material->specularTexture;
			createInfo.cubemapTexture = material->cubemapTexture;
			createInfo.hdrEquirectangularTexture = material->hdrEquirectangularTexture;
			createInfo.albedoTexture = material->albedoTexture;
			createInfo.metallicTexture = material->metallicTexture;
			createInfo.roughnessTexture = material->roughnessTexture;
			createInfo.aoTexture = material->aoTexture;
			createInfo.irradianceTexture = material->irradianceTexture;
			createInfo.brdfLUT = material->brdfLUT;
			createInfo.prefilterTexture = material->prefilterTexture;

			for (size_t i = 0; i < material->material.frameBuffers.size(); ++i)
			{
				createInfo.frameBufferViews.push_back({
					material->material.frameBuffers[i].first, (VkImageView*)material->material.frameBuffers[i].second
				});
			}

			CreateDescriptorSet(&createInfo);
		}

		void VulkanRenderer::CreateDescriptorSet(DescriptorSetCreateInfo* createInfo)
		{
			VkDescriptorSetLayout layouts[] = { *createInfo->descriptorSetLayout };
			VkDescriptorSetAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = m_DescriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = layouts;

			VK_CHECK_RESULT(vkAllocateDescriptorSets(m_VulkanDevice->m_LogicalDevice, &allocInfo, createInfo->descriptorSet));


			Uniforms constantBufferUniforms = m_Shaders[createInfo->shaderID].shader.constantBufferUniforms;
			Uniforms dynamicBufferUniforms = m_Shaders[createInfo->shaderID].shader.dynamicBufferUniforms;

			struct DescriptorSetInfo
			{
				std::string uniformName;
				VkDescriptorType descriptorType;

				VkBuffer buffer = VK_NULL_HANDLE;
				VkDeviceSize bufferSize = 0;

				VkImageView imageView = VK_NULL_HANDLE;
				VkSampler imageSampler = VK_NULL_HANDLE;

				// Fill this member in to override the default ImageInfo below
				VkDescriptorImageInfo* imageInfoPtr = nullptr;

				// These should not be filled in, they are just here so that they are kept around until the call
				// to vkUpdateDescriptorSets, and can not be local to the following for loop
				VkDescriptorBufferInfo bufferInfo;
				VkDescriptorImageInfo imageInfo;
			};

			// TODO: Clean up nullptr checks somehow?
			std::vector<DescriptorSetInfo> descriptorSets = {
				{ "uniformBufferConstant", VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				createInfo->uniformBuffer->constantBuffer.m_Buffer, sizeof(VulkanUniformBufferObjectData) },

				{ "uniformBufferDynamic", VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				createInfo->uniformBuffer->dynamicBuffer.m_Buffer, sizeof(VulkanUniformBufferObjectData) * m_RenderObjects.size() },

				{ "albedoSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->albedoTexture ? createInfo->albedoTexture->imageView : 0u,
				createInfo->albedoTexture ? createInfo->albedoTexture->sampler : 0u,
				createInfo->albedoTexture ? &createInfo->albedoTexture->imageInfoDescriptor : nullptr },

				{ "metallicSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->metallicTexture ? createInfo->metallicTexture->imageView : 0u,
				createInfo->metallicTexture ? createInfo->metallicTexture->sampler : 0u,
				createInfo->metallicTexture ? &createInfo->metallicTexture->imageInfoDescriptor : nullptr },

				{ "roughnessSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->roughnessTexture ? createInfo->roughnessTexture->imageView : 0u,
				createInfo->roughnessTexture ? createInfo->roughnessTexture->sampler : 0u,
				createInfo->roughnessTexture ? &createInfo->roughnessTexture->imageInfoDescriptor : nullptr },

				{ "aoSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->aoTexture ? createInfo->aoTexture->imageView : 0u,
				createInfo->aoTexture ? createInfo->aoTexture->sampler : 0u,
				createInfo->aoTexture ? &createInfo->aoTexture->imageInfoDescriptor : nullptr },

				{ "diffuseSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->diffuseTexture ? createInfo->diffuseTexture->imageView : 0u,
				createInfo->diffuseTexture ? createInfo->diffuseTexture->sampler : 0u,
				createInfo->diffuseTexture ? &createInfo->diffuseTexture->imageInfoDescriptor : nullptr },

				{ "normalSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->normalTexture ? createInfo->normalTexture->imageView : 0u,
				createInfo->normalTexture ? createInfo->normalTexture->sampler : 0u,
				createInfo->normalTexture ? &createInfo->normalTexture->imageInfoDescriptor : nullptr },

				{ "specularSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->specularTexture ? createInfo->specularTexture->imageView : 0u,
				createInfo->specularTexture ? createInfo->specularTexture->sampler : 0u,
				createInfo->specularTexture ? &createInfo->specularTexture->imageInfoDescriptor : nullptr },

				{ "hdrEquirectangularSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->hdrEquirectangularTexture ? createInfo->hdrEquirectangularTexture->imageView : 0u,
				createInfo->hdrEquirectangularTexture ? createInfo->hdrEquirectangularTexture->sampler : 0u,
				createInfo->hdrEquirectangularTexture ? &createInfo->hdrEquirectangularTexture->imageInfoDescriptor : nullptr },

				{ "cubemapSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->cubemapTexture ? createInfo->cubemapTexture->imageView : 0u,
				createInfo->cubemapTexture ? createInfo->cubemapTexture->sampler : 0u,
				createInfo->cubemapTexture ? &createInfo->cubemapTexture->imageInfoDescriptor : nullptr },

				{ "brdfLUT", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->brdfLUT ? createInfo->brdfLUT->imageView : 0u,
				createInfo->brdfLUT ? createInfo->brdfLUT->sampler : 0u,
				createInfo->brdfLUT ? &createInfo->brdfLUT->imageInfoDescriptor : nullptr },

				{ "irradianceSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->irradianceTexture ? createInfo->irradianceTexture->imageView : 0u,
				createInfo->irradianceTexture ? createInfo->irradianceTexture->sampler : 0u,
				createInfo->irradianceTexture ? &createInfo->irradianceTexture->imageInfoDescriptor : nullptr },

				{ "prefilterMap", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->prefilterTexture ? createInfo->prefilterTexture->imageView : 0u,
				createInfo->prefilterTexture ? createInfo->prefilterTexture->sampler : 0u,
				createInfo->prefilterTexture ? &createInfo->prefilterTexture->imageInfoDescriptor : nullptr },
			};


			for (size_t i = 0; i < createInfo->frameBufferViews.size(); ++i)
			{
				descriptorSets.push_back({
					createInfo->frameBufferViews[i].first, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					VK_NULL_HANDLE, 0,
					createInfo->frameBufferViews[i].second ? *createInfo->frameBufferViews[i].second : 0u,
					colorSampler

				});
			}

			std::vector<VkWriteDescriptorSet> writeDescriptorSets;
			writeDescriptorSets.reserve(descriptorSets.size());

			glm::uint descriptorSetIndex = 0;
			glm::uint binding = 0;

			for (DescriptorSetInfo& descriptorSetInfo : descriptorSets)
			{
				if (constantBufferUniforms.HasUniform(descriptorSetInfo.uniformName) ||
					dynamicBufferUniforms.HasUniform(descriptorSetInfo.uniformName))
				{
					descriptorSetInfo.bufferInfo = {};
					descriptorSetInfo.bufferInfo.buffer = descriptorSetInfo.buffer;
					descriptorSetInfo.bufferInfo.range = descriptorSetInfo.bufferSize;

					descriptorSetInfo.imageInfo = {};
					descriptorSetInfo.imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					if (descriptorSetInfo.imageView)
					{
						descriptorSetInfo.imageInfo.imageView = descriptorSetInfo.imageView;
						descriptorSetInfo.imageInfo.sampler = descriptorSetInfo.imageSampler;
					}
					else
					{
						if (descriptorSetInfo.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER ||
							descriptorSetInfo.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
							descriptorSetInfo.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
							descriptorSetInfo.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
							descriptorSetInfo.descriptorType == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
						{
							// If setting a sampler type, image info must be filled in
							descriptorSetInfo.imageInfo.imageView = m_BlankTexture->imageView;
							descriptorSetInfo.imageInfo.sampler = m_BlankTexture->sampler;
						}
					}

					writeDescriptorSets.push_back({});
					writeDescriptorSets[descriptorSetIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					writeDescriptorSets[descriptorSetIndex].dstSet = *createInfo->descriptorSet;
					writeDescriptorSets[descriptorSetIndex].dstBinding = binding;
					writeDescriptorSets[descriptorSetIndex].dstArrayElement = 0;
					writeDescriptorSets[descriptorSetIndex].descriptorType = descriptorSetInfo.descriptorType;
					writeDescriptorSets[descriptorSetIndex].descriptorCount = 1;
					writeDescriptorSets[descriptorSetIndex].pBufferInfo = descriptorSetInfo.bufferInfo.buffer ? &descriptorSetInfo.bufferInfo : nullptr;
					writeDescriptorSets[descriptorSetIndex].pImageInfo = descriptorSetInfo.imageInfo.imageView ? &descriptorSetInfo.imageInfo : nullptr;

					++descriptorSetIndex;
					++binding;

					if (descriptorSetInfo.imageInfoPtr)
					{
						*descriptorSetInfo.imageInfoPtr = descriptorSetInfo.imageInfo;
					}
				}
			}

			if (!writeDescriptorSets.empty())
			{
				vkUpdateDescriptorSets(m_VulkanDevice->m_LogicalDevice, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, nullptr);
			}
		}

		void VulkanRenderer::CreateDescriptorSetLayout(ShaderID shaderID)
		{
			m_DescriptorSetLayouts.push_back(VkDescriptorSetLayout());
			VkDescriptorSetLayout* descriptorSetLayout = &m_DescriptorSetLayouts.back();

			VulkanShader* shader = &m_Shaders[shaderID];

			struct DescriptorSetInfo
			{
				std::string uniformName;
				VkDescriptorType descriptorType;
				VkShaderStageFlags shaderStageFlags;
			};

			static DescriptorSetInfo descriptorSets[] = {
				{ "uniformBufferConstant", VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "uniformBufferDynamic", VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "albedoSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "metallicSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "roughnessSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "aoSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "diffuseSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "normalSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "specularSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "hdrEquirectangularSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "cubemapSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "brdfLUT", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "irradianceSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "prefilterMap", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "positionMetallicFrameBufferSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "normalRoughnessFrameBufferSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "albedoAOFrameBufferSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },
			};

			std::vector<VkDescriptorSetLayoutBinding> bindings;
			glm::uint binding = 0;

			for (DescriptorSetInfo& descSetInfo : descriptorSets)
			{
				if (shader->shader.constantBufferUniforms.HasUniform(descSetInfo.uniformName) ||
					shader->shader.dynamicBufferUniforms.HasUniform(descSetInfo.uniformName))
				{
					VkDescriptorSetLayoutBinding descSetLayoutBinding = {};
					descSetLayoutBinding.binding = binding;
					descSetLayoutBinding.descriptorCount = 1;
					descSetLayoutBinding.descriptorType = descSetInfo.descriptorType;
					descSetLayoutBinding.stageFlags = descSetInfo.shaderStageFlags;
					bindings.push_back(descSetLayoutBinding);
					++binding;
				}
			}

			VkDescriptorSetLayoutCreateInfo layoutInfo = {};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = bindings.size();
			layoutInfo.pBindings = bindings.data();

			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_VulkanDevice->m_LogicalDevice, &layoutInfo, nullptr, descriptorSetLayout));
		}

		void VulkanRenderer::CreateTextureSampler(VulkanTexture* texture, float maxAnisotropy, float minLod, float maxLod, VkSamplerAddressMode samplerAddressMode, VkBorderColor borderColor) const
		{
			VkSamplerCreateInfo samplerInfo = {};
			samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.magFilter = VK_FILTER_LINEAR;
			samplerInfo.minFilter = VK_FILTER_LINEAR;
			samplerInfo.addressModeU = samplerAddressMode;
			samplerInfo.addressModeV = samplerAddressMode;
			samplerInfo.addressModeW = samplerAddressMode;
			samplerInfo.anisotropyEnable = VK_TRUE;
			samplerInfo.maxAnisotropy = maxAnisotropy;
			samplerInfo.borderColor = borderColor;
			samplerInfo.unnormalizedCoordinates = VK_FALSE;
			samplerInfo.compareEnable = VK_FALSE;
			samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerInfo.mipLodBias = 0.0f;
			samplerInfo.minLod = minLod;
			samplerInfo.maxLod = maxLod;

			VK_CHECK_RESULT(vkCreateSampler(m_VulkanDevice->m_LogicalDevice, &samplerInfo, nullptr, texture->sampler.replace()));
		}

		bool VulkanRenderer::GetShaderID(const std::string shaderName, ShaderID& shaderID)
		{
			// TODO: Store shaders using sorted data structure?
			for (size_t i = 0; i < m_Shaders.size(); ++i)
			{
				if (m_Shaders[i].shader.name.compare(shaderName) == 0)
				{
					shaderID = i;
					return true;
				}
			}

			return false;
		}

		VulkanTexture* VulkanRenderer::GetLoadedTexture(const std::string& filePath)
		{
			for (VulkanTexture* vulkanTexture : m_LoadedTextures)
			{
				if (!filePath.empty() && filePath.compare(vulkanTexture->filePath) == 0)
				{
					return vulkanTexture;
				}
			}

			return nullptr;
		}

		void VulkanRenderer::CreateGraphicsPipeline(RenderID renderID)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject) return;
			VulkanMaterial* material = &m_LoadedMaterials[renderObject->materialID];
			VulkanShader& shader = m_Shaders[material->material.shaderID];

			GraphicsPipelineCreateInfo pipelineCreateInfo = {};
			pipelineCreateInfo.shaderID = material->material.shaderID;
			pipelineCreateInfo.vertexAttributes = renderObject->vertexBufferData->Attributes;
			pipelineCreateInfo.topology = renderObject->topology;
			pipelineCreateInfo.cullMode = renderObject->cullMode;
			pipelineCreateInfo.descriptorSetLayoutIndex = material->descriptorSetLayoutIndex;
			pipelineCreateInfo.setDynamicStates = false;
			pipelineCreateInfo.enabledColorBlending = false;
			pipelineCreateInfo.pipelineLayout = renderObject->pipelineLayout.replace();
			pipelineCreateInfo.grahpicsPipeline = renderObject->graphicsPipeline.replace();
			// Deferred objects get drawn in a different render pass
			pipelineCreateInfo.renderPass = shader.shader.deferred ? offScreenFrameBuf->renderPass : m_DeferredCombineRenderPass;
			pipelineCreateInfo.subpass = shader.shader.subpass;
			pipelineCreateInfo.depthWriteEnable = shader.shader.depthWriteEnable ? VK_TRUE : VK_FALSE;

			VkPushConstantRange pushConstantRange = {};
			if (m_Shaders[material->material.shaderID].shader.needPushConstantBlock)
			{
				pipelineCreateInfo.pushConstantRangeCount = 1;
				pushConstantRange.offset = 0;
				pushConstantRange.size = sizeof(material->material.pushConstantBlock);
				pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
				pipelineCreateInfo.pushConstants = &pushConstantRange;
			}

			CreateGraphicsPipeline(&pipelineCreateInfo);
		}

		void VulkanRenderer::CreateGraphicsPipeline(GraphicsPipelineCreateInfo* createInfo)
		{
			VulkanShader& shader = m_Shaders[createInfo->shaderID];

			VDeleter<VkShaderModule> vertShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(shader.shader.vertexShaderCode, vertShaderModule))
			{
				Logger::LogError("Failed to compile vertex shader located at: " + shader.shader.vertexShaderFilePath);
			}

			VDeleter<VkShaderModule> fragShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(shader.shader.fragmentShaderCode, fragShaderModule))
			{
				Logger::LogError("Failed to compile fragment shader located at: " + shader.shader.fragmentShaderFilePath);
			}

			VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
			vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
			vertShaderStageInfo.module = vertShaderModule;
			vertShaderStageInfo.pName = "main";

			VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
			fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			fragShaderStageInfo.module = fragShaderModule;
			fragShaderStageInfo.pName = "main";

			std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = { vertShaderStageInfo, fragShaderStageInfo };

			const glm::uint vertexStride = CalculateVertexStride(createInfo->vertexAttributes);
			VkVertexInputBindingDescription bindingDescription = GetVertexBindingDescription(vertexStride);
			std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
			GetVertexAttributeDescriptions(createInfo->vertexAttributes, attributeDescriptions);

			VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
			vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputInfo.vertexBindingDescriptionCount = 1;
			vertexInputInfo.vertexAttributeDescriptionCount = attributeDescriptions.size();
			vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
			vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

			VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = createInfo->topology;
			inputAssembly.primitiveRestartEnable = VK_FALSE;

			VkViewport viewport = {};
			viewport.x = 0.0f;
			viewport.y = 0.0f;
			viewport.width = (float)m_SwapChainExtent.width;
			viewport.height = (float)m_SwapChainExtent.height;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;

			VkRect2D scissor = {};
			scissor.offset = { 0, 0 };
			scissor.extent = m_SwapChainExtent;

			VkPipelineViewportStateCreateInfo viewportState = {};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.pViewports = &viewport;
			viewportState.scissorCount = 1;
			viewportState.pScissors = &scissor;

			VkPipelineRasterizationStateCreateInfo rasterizer = {};
			rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizer.depthClampEnable = VK_FALSE;
			rasterizer.rasterizerDiscardEnable = VK_FALSE;
			rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = createInfo->cullMode;
			rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rasterizer.depthBiasEnable = VK_FALSE;

			VkPipelineMultisampleStateCreateInfo multisampling = {};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			// TODO: Enable if available as an option
			multisampling.sampleShadingEnable = VK_FALSE;
			multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments = {};
			colorBlendAttachments.resize(shader.shader.numAttachments, {});
			for (VkPipelineColorBlendAttachmentState& colorBlendAttachment : colorBlendAttachments)
			{
				colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

				if (createInfo->enabledColorBlending)
				{
					colorBlendAttachment.blendEnable = VK_TRUE;
					colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
					colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
					colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
				}
				else
				{
					colorBlendAttachment.blendEnable = VK_FALSE;
				}
			}

			VkPipelineColorBlendStateCreateInfo colorBlending = {};
			colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlending.logicOpEnable = VK_FALSE;
			colorBlending.logicOp = VK_LOGIC_OP_COPY;
			colorBlending.attachmentCount = colorBlendAttachments.size();
			colorBlending.pAttachments = colorBlendAttachments.data();
			colorBlending.blendConstants[0] = 0.0f;
			colorBlending.blendConstants[1] = 0.0f;
			colorBlending.blendConstants[2] = 0.0f;
			colorBlending.blendConstants[3] = 0.0f;

			std::array<VkDescriptorSetLayout, 1> setLayouts = { m_DescriptorSetLayouts[createInfo->descriptorSetLayoutIndex] };
			VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutInfo.setLayoutCount = setLayouts.size();
			pipelineLayoutInfo.pSetLayouts = setLayouts.data();
			pipelineLayoutInfo.pushConstantRangeCount = createInfo->pushConstantRangeCount;
			pipelineLayoutInfo.pPushConstantRanges = createInfo->pushConstants;

			if (createInfo->pipelineCache)
			{
				VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
				pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
				VK_CHECK_RESULT(vkCreatePipelineCache(m_VulkanDevice->m_LogicalDevice, &pipelineCacheCreateInfo, nullptr, createInfo->pipelineCache));
			}

			VK_CHECK_RESULT(vkCreatePipelineLayout(m_VulkanDevice->m_LogicalDevice, &pipelineLayoutInfo, nullptr, createInfo->pipelineLayout));

			VkPipelineDepthStencilStateCreateInfo depthStencil{};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencil.depthTestEnable = createInfo->depthTestEnable;
			depthStencil.depthWriteEnable = createInfo->depthWriteEnable ? VK_TRUE : VK_FALSE;
			depthStencil.depthCompareOp = createInfo->depthCompareOp;
			depthStencil.depthBoundsTestEnable = VK_FALSE;
			depthStencil.stencilTestEnable = createInfo->stencilTestEnable;
			depthStencil.minDepthBounds = 0.0f;
			depthStencil.maxDepthBounds = 1.0f;
			depthStencil.front = {};
			depthStencil.back = {};

			VkPipelineDynamicStateCreateInfo dynamicState = {};
			VkPipelineDynamicStateCreateInfo* pDynamicState = nullptr;
			if (createInfo->setDynamicStates)
			{
				VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
				dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
				dynamicState.dynamicStateCount = 2;
				dynamicState.pDynamicStates = dynamicStates;

				pDynamicState = &dynamicState;
			}

			VkGraphicsPipelineCreateInfo pipelineInfo = {};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = shaderStages.size();
			pipelineInfo.pStages = shaderStages.data();
			pipelineInfo.pVertexInputState = &vertexInputInfo;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.layout = *createInfo->pipelineLayout;
			pipelineInfo.renderPass = createInfo->renderPass;
			pipelineInfo.pDynamicState = pDynamicState;
			pipelineInfo.subpass = createInfo->subpass;
			pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

			VkPipelineCache pipelineCache = createInfo->pipelineCache ? *createInfo->pipelineCache : VK_NULL_HANDLE;

			VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_VulkanDevice->m_LogicalDevice, pipelineCache, 1, &pipelineInfo, nullptr, createInfo->grahpicsPipeline));
		}

		void VulkanRenderer::CreateCommandPool()
		{
			VulkanQueueFamilyIndices queueFamilyIndices = FindQueueFamilies(m_VulkanDevice->m_PhysicalDevice);

			VkCommandPoolCreateInfo poolInfo = {};
			poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			poolInfo.queueFamilyIndex = (glm::uint32)queueFamilyIndices.graphicsFamily;

			VK_CHECK_RESULT(vkCreateCommandPool(m_VulkanDevice->m_LogicalDevice, &poolInfo, nullptr, m_VulkanDevice->m_CommandPool.replace()));
		}

		VkCommandBuffer VulkanRenderer::BeginSingleTimeCommands() const
		{
			VkCommandBufferAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			// TODO: Create command pool just for these types of alocations, using VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
			allocInfo.commandPool = m_VulkanDevice->m_CommandPool;
			allocInfo.commandBufferCount = 1;

			VkCommandBuffer commandBuffer;
			vkAllocateCommandBuffers(m_VulkanDevice->m_LogicalDevice, &allocInfo, &commandBuffer);

			VkCommandBufferBeginInfo beginInfo = {};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

			vkBeginCommandBuffer(commandBuffer, &beginInfo);

			return commandBuffer;
		}

		void VulkanRenderer::EndSingleTimeCommands(VkCommandBuffer commandBuffer) const
		{
			vkEndCommandBuffer(commandBuffer);

			VkSubmitInfo submitInfo = {};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffer;

			vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
			vkQueueWaitIdle(m_GraphicsQueue);

			vkFreeCommandBuffers(m_VulkanDevice->m_LogicalDevice, m_VulkanDevice->m_CommandPool, 1, &commandBuffer);
		}

		VkDeviceSize VulkanRenderer::CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
			VkMemoryPropertyFlags properties, VkImageLayout initialLayout, VkImage* image, VkDeviceMemory* imageMemory, glm::uint arrayLayers, glm::uint mipLevels, VkImageCreateFlags flags) const
		{
			VkImageCreateInfo imageInfo = {};
			imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageInfo.imageType = VK_IMAGE_TYPE_2D;
			imageInfo.extent.width = width;
			imageInfo.extent.height = height;
			imageInfo.extent.depth = 1;
			imageInfo.mipLevels = mipLevels;
			imageInfo.arrayLayers = arrayLayers;
			imageInfo.format = format;
			imageInfo.tiling = tiling;
			imageInfo.initialLayout = initialLayout;
			imageInfo.usage = usage;
			imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageInfo.flags = flags;

			VK_CHECK_RESULT(vkCreateImage(m_VulkanDevice->m_LogicalDevice, &imageInfo, nullptr, image));

			VkMemoryRequirements memRequirements;
			vkGetImageMemoryRequirements(m_VulkanDevice->m_LogicalDevice, *image, &memRequirements);

			VkMemoryAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memRequirements.size;
			allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

			VK_CHECK_RESULT(vkAllocateMemory(m_VulkanDevice->m_LogicalDevice, &allocInfo, nullptr, imageMemory));

			VK_CHECK_RESULT(vkBindImageMemory(m_VulkanDevice->m_LogicalDevice, *image, *imageMemory, 0));

			return memRequirements.size;
		}

		VkFormat VulkanRenderer::FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const
		{
			for (VkFormat format : candidates)
			{
				VkFormatProperties properties;
				vkGetPhysicalDeviceFormatProperties(m_VulkanDevice->m_PhysicalDevice, format, &properties);

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

		bool VulkanRenderer::HasStencilComponent(VkFormat format) const
		{
			return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
		}

		void VulkanRenderer::CreateDepthResources()
		{
			VkFormat depthFormat;
			GetSupportedDepthFormat(m_VulkanDevice->m_PhysicalDevice, &depthFormat);

			m_DepthImageFormat = depthFormat;

			// Swapchain images
			CreateImage(m_SwapChainExtent.width, m_SwapChainExtent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_PREINITIALIZED,
				m_DepthImage.replace(), m_DepthImageMemory.replace());
			CreateImageView(m_DepthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1, m_DepthImageView.replace());

			TransitionImageLayout(m_DepthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1);
		}

		void VulkanRenderer::CreateFramebuffers()
		{
			m_SwapChainFramebuffers.resize(m_SwapChainImageViews.size(), VDeleter<VkFramebuffer>{ m_VulkanDevice->m_LogicalDevice, vkDestroyFramebuffer });

			for (size_t i = 0; i < m_SwapChainImageViews.size(); ++i)
			{
				std::array<VkImageView, 2> attachments = {
					m_SwapChainImageViews[i],
					m_DepthImageView
				};

				VkFramebufferCreateInfo framebufferInfo = {};
				framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				// Just defines which render passes this frame buffer is compatible with (can be either deferredCombine or forward)
				framebufferInfo.renderPass = m_DeferredCombineRenderPass;
				framebufferInfo.attachmentCount = attachments.size();
				framebufferInfo.pAttachments = attachments.data();
				framebufferInfo.width = m_SwapChainExtent.width;
				framebufferInfo.height = m_SwapChainExtent.height;
				framebufferInfo.layers = 1;

				VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &framebufferInfo, nullptr, m_SwapChainFramebuffers[i].replace()));
			}
		}

		void VulkanRenderer::PrepareOffscreenFrameBuffer(Window* window)
		{
			// TODO: This should be setting up the m_SwapChainFrameBuffers?

			auto oldFrameBufferAttachments = offScreenFrameBuf->frameBufferAttachments;

			// Is this needed?
			SafeDelete(offScreenFrameBuf);
			offScreenFrameBuf = new FrameBuffer(m_VulkanDevice->m_LogicalDevice);
			offScreenFrameBuf->frameBufferAttachments = oldFrameBufferAttachments;

			const glm::vec2i frameBufferSize = window->GetFrameBufferSize();
			offScreenFrameBuf->width = frameBufferSize.x;
			offScreenFrameBuf->height = frameBufferSize.y;

			// Does *not* include depth attachment
			const size_t frameBufferColorAttachmentCount = offScreenFrameBuf->frameBufferAttachments.size();

			// Color attachments
			for (uint32_t i = 0; i < frameBufferColorAttachmentCount; ++i)
			{
				CreateAttachment(
					m_VulkanDevice,
					offScreenFrameBuf->frameBufferAttachments[i].second.format,
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
					offScreenFrameBuf->width,
					offScreenFrameBuf->height,
					&offScreenFrameBuf->frameBufferAttachments[i].second);
			}

			// Depth attachment

			// Find a suitable depth format
			VkFormat attDepthFormat;
			VkBool32 validDepthFormat = GetSupportedDepthFormat(m_VulkanDevice->m_PhysicalDevice, &attDepthFormat);
			assert(validDepthFormat);

			// Set up separate renderpass with references to the color and depth attachments
			std::vector<VkAttachmentDescription> attachmentDescs(frameBufferColorAttachmentCount + 1); // + 1 for depth attachment

			// Init attachment properties
			for (uint32_t i = 0; i < frameBufferColorAttachmentCount; ++i)
			{
				attachmentDescs[i].samples = VK_SAMPLE_COUNT_1_BIT;
				attachmentDescs[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				attachmentDescs[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				attachmentDescs[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				attachmentDescs[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				attachmentDescs[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachmentDescs[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				attachmentDescs[i].format = offScreenFrameBuf->frameBufferAttachments[i].second.format;
			}
			attachmentDescs[frameBufferColorAttachmentCount].samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDescs[frameBufferColorAttachmentCount].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachmentDescs[frameBufferColorAttachmentCount].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescs[frameBufferColorAttachmentCount].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescs[frameBufferColorAttachmentCount].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDescs[frameBufferColorAttachmentCount].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachmentDescs[frameBufferColorAttachmentCount].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			attachmentDescs[frameBufferColorAttachmentCount].format = m_DepthImageFormat;


			std::vector<VkAttachmentReference> colorReferences;
			for (uint32_t i = 0; i < frameBufferColorAttachmentCount; ++i)
			{
				colorReferences.push_back({ i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
			}

			VkAttachmentReference depthReference = {};
			depthReference.attachment = attachmentDescs.size() - 1;
			depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.pColorAttachments = colorReferences.data();
			subpass.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
			subpass.pDepthStencilAttachment = &depthReference;

			// Use subpass dependencies for attachment layput transitions
			std::array<VkSubpassDependency, 2> dependencies;

			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.pAttachments = attachmentDescs.data();
			renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescs.size());
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = dependencies.size();
			renderPassInfo.pDependencies = dependencies.data();

			VK_CHECK_RESULT(vkCreateRenderPass(m_VulkanDevice->m_LogicalDevice, &renderPassInfo, nullptr, &offScreenFrameBuf->renderPass));

			std::vector<VkImageView> attachments;
			for (uint32_t i = 0; i < frameBufferColorAttachmentCount; ++i)
			{
				attachments.push_back(offScreenFrameBuf->frameBufferAttachments[i].second.view);
			}
			attachments.push_back(m_DepthImageView);

			VkFramebufferCreateInfo fbufCreateInfo = {};
			fbufCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbufCreateInfo.pNext = NULL;
			fbufCreateInfo.renderPass = offScreenFrameBuf->renderPass;
			fbufCreateInfo.pAttachments = attachments.data();
			fbufCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			fbufCreateInfo.width = offScreenFrameBuf->width;
			fbufCreateInfo.height = offScreenFrameBuf->height;
			fbufCreateInfo.layers = 1;
			VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &fbufCreateInfo, nullptr, &offScreenFrameBuf->frameBuffer));

			// Create sampler to sample from the color attachments
			VkSamplerCreateInfo samplerCreateInfo = {};
			samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerCreateInfo.maxAnisotropy = 1.0f;
			samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
			samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
			samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerCreateInfo.addressModeV = samplerCreateInfo.addressModeU;
			samplerCreateInfo.addressModeW = samplerCreateInfo.addressModeU;
			samplerCreateInfo.mipLodBias = 0.0f;
			samplerCreateInfo.maxAnisotropy = 1.0f;
			samplerCreateInfo.minLod = 0.0f;
			samplerCreateInfo.maxLod = 1.0f;
			samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			VK_CHECK_RESULT(vkCreateSampler(m_VulkanDevice->m_LogicalDevice, &samplerCreateInfo, nullptr, &colorSampler));
		}

		void VulkanRenderer::CreateVulkanTexture_Empty(glm::uint width, glm::uint height, VkFormat format, uint32_t mipLevels, VulkanTexture** texture) const
		{
			CreateTextureImage_Empty(width, height, format, mipLevels, texture);
			if (*texture != nullptr)
			{
				CreateTextureImageView(*texture, format);
				CreateTextureSampler(*texture, 16.0f, 0.0f, 1.0f, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE);
			}
		}

		void VulkanRenderer::CreateVulkanTexture(const std::string& filePath, VkFormat format, glm::uint mipLevels, VulkanTexture** texture) const
		{
			CreateTextureImage(filePath, format, mipLevels, texture);
			if (*texture != nullptr)
			{
				CreateTextureImageView(*texture, format);
				CreateTextureSampler(*texture);
			}

			(*texture)->filePath = filePath;
		}

		void VulkanRenderer::CreateVulkanTexture_HDR(const std::string & filePath, VkFormat format, glm::uint mipLevels, VulkanTexture** texture) const
		{
			CreateTextureImage_HDR(filePath, format, mipLevels, texture);
			if (*texture != nullptr)
			{
				CreateTextureImageView(*texture, format);
				CreateTextureSampler(*texture);
			}

			(*texture)->filePath = filePath;
		}

		// TODO: IMPORTANT: FIXME: XXX: Consolidate two cubemap functions (way too much duplicated code atm)
		void VulkanRenderer::CreateVulkanCubemap_Empty(glm::uint width, glm::uint height, glm::uint channels, glm::uint mipLevels, bool enableTrilinearFiltering, VkFormat format, VulkanTexture** texture) const
		{
			UNREFERENCED_PARAMETER(channels);
			UNREFERENCED_PARAMETER(enableTrilinearFiltering);

			*texture = new VulkanTexture(m_VulkanDevice->m_LogicalDevice);
			(*texture)->mipLevels = mipLevels;

			VkMemoryAllocateInfo memAllocInfo = {};
			memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

			// Create optimal tiled target image
			VkImageCreateInfo imageCreateInfo = {};
			imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = format;
			imageCreateInfo.mipLevels = mipLevels;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.extent = { (glm::uint)width, (glm::uint)height, 1u };
			imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			// Cube faces count as array layers in Vulkan
			imageCreateInfo.arrayLayers = 6;
			// This flag is required for cube map images
			imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

			VK_CHECK_RESULT(vkCreateImage(m_VulkanDevice->m_LogicalDevice, &imageCreateInfo, nullptr, &(*texture)->image));

			VkMemoryRequirements memReqs;
			vkGetImageMemoryRequirements(m_VulkanDevice->m_LogicalDevice, (*texture)->image, &memReqs);

			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = m_VulkanDevice->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			VK_CHECK_RESULT(vkAllocateMemory(m_VulkanDevice->m_LogicalDevice, &memAllocInfo, nullptr, &(*texture)->imageMemory));
			VK_CHECK_RESULT(vkBindImageMemory(m_VulkanDevice->m_LogicalDevice, (*texture)->image, (*texture)->imageMemory, 0));

			// Create sampler
			VkSamplerCreateInfo sampler = {};
			sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			sampler.maxAnisotropy = 1.0f;
			sampler.magFilter = VK_FILTER_LINEAR;
			sampler.minFilter = VK_FILTER_LINEAR;
			sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sampler.addressModeV = sampler.addressModeU;
			sampler.addressModeW = sampler.addressModeU;
			sampler.mipLodBias = 0.0f;
			sampler.compareOp = VK_COMPARE_OP_NEVER;
			sampler.minLod = 0.0f;
			sampler.maxLod = (float)mipLevels;
			sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			if (m_VulkanDevice->m_PhysicalDeviceFeatures.samplerAnisotropy)
			{
				sampler.maxAnisotropy = m_VulkanDevice->m_PhysicalDeviceProperties.limits.maxSamplerAnisotropy;
				sampler.anisotropyEnable = VK_TRUE;
			}

			VK_CHECK_RESULT(vkCreateSampler(m_VulkanDevice->m_LogicalDevice, &sampler, nullptr, &(*texture)->sampler));

			// Create image view
			VkImageViewCreateInfo imageViewCreateInfo = {};
			imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			// Cube map view type
			imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
			imageViewCreateInfo.format = format;
			imageViewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
			imageViewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			// 6 array layers (faces)
			imageViewCreateInfo.subresourceRange.layerCount = 6;
			// Set number of mip levels
			imageViewCreateInfo.subresourceRange.levelCount = mipLevels;
			imageViewCreateInfo.image = (*texture)->image;
			VK_CHECK_RESULT(vkCreateImageView(m_VulkanDevice->m_LogicalDevice, &imageViewCreateInfo, nullptr, &(*texture)->imageView));

			(*texture)->imageLayout = imageCreateInfo.initialLayout;

			(*texture)->width = width;
			(*texture)->height = height;
		}

		void VulkanRenderer::CreateVulkanCubemap(const std::array<std::string, 6>& filePaths, VkFormat format, VulkanTexture** texture, bool generateMipMaps) const
		{
			int textureWidth = 0;
			int textureHeight = 0;
			int textureChannels = 0;

			struct Image
			{
				unsigned char* pixels;
				int width;
				int height;
				int textureChannels;
				int size;
			};

			std::vector<Image> images;
			glm::uint totalSize = 0;

			std::string fileName = filePaths[0];
			StripLeadingDirectories(fileName);
			Logger::LogInfo("Loading cubemap textures " + filePaths[0] + " , " + filePaths[1] + " , " + filePaths[2] + " , " + filePaths[3] + " , " + filePaths[4] + " , " + filePaths[5]);

			images.reserve(filePaths.size());
			for (const std::string& filePath : filePaths)
			{
				unsigned char* pixels = stbi_load(filePath.c_str(), &textureWidth, &textureHeight, &textureChannels, STBI_rgb_alpha);
				if (!pixels)
				{
					const char* failureReasonStr = stbi_failure_reason();
					Logger::LogError("Couldn't load image, failure reason: " + std::string(failureReasonStr) + " filepath: " + filePath);
					return;
				}

				// The load function returns the original channel count, 
				// but it was forced to 4 because of the last parameter
				textureChannels = 4;

				int size = textureWidth * textureHeight * textureChannels * sizeof(unsigned char);
				images.push_back({ pixels, textureWidth, textureHeight, textureChannels, size });
				totalSize += size;
			}

			unsigned char* pixels = (unsigned char*)malloc(totalSize);
			unsigned char* pixelData = pixels;
			for (Image& image : images)
			{
				memcpy(pixelData, image.pixels, image.size);
				pixelData += (image.size / sizeof(unsigned char));
				stbi_image_free(image.pixels);
			}

			const glm::uint mipLevels = generateMipMaps ? static_cast<uint32_t>(floor(log2(std::min(textureWidth, textureHeight)))) + 1 : 0;

			*texture = new VulkanTexture(m_VulkanDevice->m_LogicalDevice);
			(*texture)->mipLevels = mipLevels;

			VkMemoryAllocateInfo memAllocInfo = {};
			memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

			VkMemoryRequirements memReqs;

			// Create a host-visible staging buffer that contains the raw image data
			VulkanBuffer stagingBuffer(m_VulkanDevice->m_LogicalDevice);

			VkBufferCreateInfo bufferCreateInfo = {};
			bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferCreateInfo.size = totalSize;
			bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;


			VK_CHECK_RESULT(vkCreateBuffer(m_VulkanDevice->m_LogicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer.m_Buffer));

			vkGetBufferMemoryRequirements(m_VulkanDevice->m_LogicalDevice, stagingBuffer.m_Buffer, &memReqs);
			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = m_VulkanDevice->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			VK_CHECK_RESULT(vkAllocateMemory(m_VulkanDevice->m_LogicalDevice, &memAllocInfo, nullptr, &stagingBuffer.m_Memory));
			stagingBuffer.Bind();

			stagingBuffer.Map(memReqs.size);
			memcpy(stagingBuffer.m_Mapped, pixels, totalSize);
			free(pixels);
			stagingBuffer.Unmap();

			VkImageCreateInfo imageCreateInfo = {};
			imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = format;
			imageCreateInfo.mipLevels = mipLevels;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.extent = { (glm::uint)textureWidth, (glm::uint)textureHeight, 1u };
			imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			imageCreateInfo.arrayLayers = 6;
			imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

			VK_CHECK_RESULT(vkCreateImage(m_VulkanDevice->m_LogicalDevice, &imageCreateInfo, nullptr, &(*texture)->image));

			vkGetImageMemoryRequirements(m_VulkanDevice->m_LogicalDevice, (*texture)->image, &memReqs);

			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = m_VulkanDevice->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			VK_CHECK_RESULT(vkAllocateMemory(m_VulkanDevice->m_LogicalDevice, &memAllocInfo, nullptr, &(*texture)->imageMemory));
			VK_CHECK_RESULT(vkBindImageMemory(m_VulkanDevice->m_LogicalDevice, (*texture)->image, (*texture)->imageMemory, 0));

			VkCommandBuffer copyCmd = CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			// Setup buffer copy regions for each face including all of it's miplevels
			std::vector<VkBufferImageCopy> bufferCopyRegions;
			uint32_t offset = 0;

			for (uint32_t face = 0; face < 6; ++face)
			{
				for (uint32_t mipLevel = 0; mipLevel < mipLevels; ++mipLevel)
				{
					VkBufferImageCopy bufferCopyRegion = {};
					bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					bufferCopyRegion.imageSubresource.mipLevel = mipLevel;
					bufferCopyRegion.imageSubresource.baseArrayLayer = face;
					bufferCopyRegion.imageSubresource.layerCount = 1;
					bufferCopyRegion.imageExtent.width = static_cast<uint32_t>(textureWidth * std::pow(0.5f, mipLevel));
					bufferCopyRegion.imageExtent.height = static_cast<uint32_t>(textureHeight * std::pow(0.5f, mipLevel));
					bufferCopyRegion.imageExtent.depth = 1;
					bufferCopyRegion.bufferOffset = offset;

					bufferCopyRegions.push_back(bufferCopyRegion);

					offset += images[face].size;
				}
			}

			// Image barrier for optimal image (target)
			// Set initial layout for all array layers (faces) of the optimal (target) tiled texture
			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = mipLevels;
			subresourceRange.layerCount = 6;

			SetImageLayout(
				copyCmd,
				*texture,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);

			// Copy the cube map faces from the staging buffer to the optimal tiled image
			vkCmdCopyBufferToImage(
				copyCmd,
				stagingBuffer.m_Buffer,
				(*texture)->image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				static_cast<uint32_t>(bufferCopyRegions.size()),
				bufferCopyRegions.data()
			);

			// Change texture image layout to shader read after all faces have been copied
			(*texture)->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			SetImageLayout(
				copyCmd,
				*texture,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				(*texture)->imageLayout,
				subresourceRange);

			FlushCommandBuffer(copyCmd, m_GraphicsQueue, true);

			VkSamplerCreateInfo sampler = {};
			sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			sampler.maxAnisotropy = 1.0f;
			sampler.magFilter = VK_FILTER_LINEAR;
			sampler.minFilter = VK_FILTER_LINEAR;
			sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sampler.addressModeV = sampler.addressModeU;
			sampler.addressModeW = sampler.addressModeU;
			sampler.mipLodBias = 0.0f;
			sampler.compareOp = VK_COMPARE_OP_NEVER;
			sampler.minLod = 0.0f;
			sampler.maxLod = (float)mipLevels;
			sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			if (m_VulkanDevice->m_PhysicalDeviceFeatures.samplerAnisotropy)
			{
				sampler.maxAnisotropy = m_VulkanDevice->m_PhysicalDeviceProperties.limits.maxSamplerAnisotropy;
				sampler.anisotropyEnable = VK_TRUE;
			}

			VK_CHECK_RESULT(vkCreateSampler(m_VulkanDevice->m_LogicalDevice, &sampler, nullptr, &(*texture)->sampler));

			VkImageViewCreateInfo imageViewCreateInfo = {};
			imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
			imageViewCreateInfo.format = format;
			imageViewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
			imageViewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			imageViewCreateInfo.subresourceRange.layerCount = 6;
			imageViewCreateInfo.subresourceRange.levelCount = mipLevels;
			imageViewCreateInfo.image = (*texture)->image;
			VK_CHECK_RESULT(vkCreateImageView(m_VulkanDevice->m_LogicalDevice, &imageViewCreateInfo, nullptr, &(*texture)->imageView));

			(*texture)->filePath = filePaths[0];
			(*texture)->width = textureWidth;
			(*texture)->height = textureHeight;
		}

		void VulkanRenderer::CreateTextureImage(const std::string& filePath, VkFormat format, glm::uint mipLevels, VulkanTexture** texture) const
		{
			std::string fileName = filePath;
			StripLeadingDirectories(fileName);
			Logger::LogInfo("Loading texture " + fileName);

			int textureWidth, textureHeight, textureChannels;
			unsigned char* pixels = stbi_load(filePath.c_str(), &textureWidth, &textureHeight, &textureChannels, STBI_rgb_alpha);

			if (!pixels)
			{
				const char* failureReasonStr = stbi_failure_reason();
				Logger::LogError("Couldn't load image, failure reason: " + std::string(failureReasonStr) + " filepath: " + filePath);
				return;
			}

			*texture = new VulkanTexture(m_VulkanDevice->m_LogicalDevice);
			(*texture)->mipLevels = mipLevels;

			VkDeviceSize imageSize = (VkDeviceSize)(textureWidth * textureHeight * 4);

			VulkanBuffer stagingBuffer(m_VulkanDevice->m_LogicalDevice);

			CreateAndAllocateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer);

			void* data;
			vkMapMemory(m_VulkanDevice->m_LogicalDevice, stagingBuffer.m_Memory, 0, imageSize, 0, &data);
			memcpy(data, pixels, (size_t)imageSize);
			vkUnmapMemory(m_VulkanDevice->m_LogicalDevice, stagingBuffer.m_Memory);

			stbi_image_free(pixels);

			CreateImage((glm::uint32)textureWidth, (glm::uint32)textureHeight, format, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_PREINITIALIZED,
				(*texture)->image.replace(), (*texture)->imageMemory.replace());

			TransitionImageLayout((*texture)->image, format, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);
			CopyBufferToImage(stagingBuffer.m_Buffer, (*texture)->image, (glm::uint32)textureWidth, (glm::uint32)textureHeight);
			TransitionImageLayout((*texture)->image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels);
		}

		void VulkanRenderer::CreateTextureImage_Empty(glm::uint width, glm::uint height, VkFormat format, glm::uint mipLevels, VulkanTexture** texture) const
		{
			*texture = new VulkanTexture(m_VulkanDevice->m_LogicalDevice);
			(*texture)->mipLevels = mipLevels;

			CreateImage((glm::uint32)width, (glm::uint32)height, format, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_PREINITIALIZED,
				(*texture)->image.replace(), (*texture)->imageMemory.replace(), 1, mipLevels);
		}

		void VulkanRenderer::CreateTextureImage_HDR(const std::string& filePath, VkFormat format, glm::uint mipLevels, VulkanTexture** texture) const
		{
			HDRImage image = {};
			if (!image.Load(filePath, false))
			{
				const char* failureReasonStr = stbi_failure_reason();
				Logger::LogError("Couldn't load HDR image, failure reason: " + std::string(failureReasonStr) + " filepath: " + filePath);
				return;
			}

			*texture = new VulkanTexture(m_VulkanDevice->m_LogicalDevice);
			(*texture)->mipLevels = mipLevels;

			VkDeviceSize imageSize = CreateImage((glm::uint32)image.width, (glm::uint32)image.height, format, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_PREINITIALIZED,
				(*texture)->image.replace(), (*texture)->imageMemory.replace(), 1, mipLevels);

			const int numChannels = 4;
			VkDeviceSize calculatedImageSize = (VkDeviceSize)(image.width * image.height * numChannels * sizeof(float));

			VulkanBuffer stagingBuffer(m_VulkanDevice->m_LogicalDevice);

			CreateAndAllocateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer);

			void* data;
			vkMapMemory(m_VulkanDevice->m_LogicalDevice, stagingBuffer.m_Memory, 0, imageSize, 0, &data);
			memcpy(data, image.pixels, (size_t)calculatedImageSize);
			vkUnmapMemory(m_VulkanDevice->m_LogicalDevice, stagingBuffer.m_Memory);

			image.Free();

			TransitionImageLayout((*texture)->image, format, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);
			CopyBufferToImage(stagingBuffer.m_Buffer, (*texture)->image, (glm::uint32)image.width, (glm::uint32)image.height);
			TransitionImageLayout((*texture)->image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels);
		}

		// TODO: Remove unused function:
		void VulkanRenderer::RebuildCommandBuffers(const GameContext& gameContext)
		{
			if (!CheckCommandBuffers())
			{
				DestroyCommandBuffers();
				CreateCommandBuffers();
			}
			BuildCommandBuffers(gameContext);
		}

		void VulkanRenderer::CreateCommandBuffers()
		{
			// One command buffer per frame back buffer
			m_CommandBuffers.resize(m_SwapChainFramebuffers.size());

			VkCommandBufferAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandPool = m_VulkanDevice->m_CommandPool;
			allocInfo.commandBufferCount = (uint32_t)m_CommandBuffers.size();

			VK_CHECK_RESULT(vkAllocateCommandBuffers(m_VulkanDevice->m_LogicalDevice, &allocInfo, m_CommandBuffers.data()));
		}

		VkCommandBuffer VulkanRenderer::CreateCommandBuffer(VkCommandBufferLevel level, bool begin) const
		{
			VkCommandBuffer commandBuffer;

			VkCommandBufferAllocateInfo commandBuffferAllocateInfo = {};
			commandBuffferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			commandBuffferAllocateInfo.commandPool = m_VulkanDevice->m_CommandPool;
			commandBuffferAllocateInfo.level = level;
			commandBuffferAllocateInfo.commandBufferCount = 1;

			VK_CHECK_RESULT(vkAllocateCommandBuffers(m_VulkanDevice->m_LogicalDevice, &commandBuffferAllocateInfo, &commandBuffer));

			if (begin)
			{
				VkCommandBufferBeginInfo commandBufferBeginInfo = {};
				commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));
			}

			return commandBuffer;
		}

		void VulkanRenderer::BuildCommandBuffers(const GameContext& gameContext)
		{
			std::array<VkClearValue, 2> clearValues = {};
			clearValues[0].color = m_ClearColor;
			clearValues[1].depthStencil = { 1.0f, 0 };

			VkRenderPassBeginInfo renderPassBeginInfo = {};
			renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassBeginInfo.renderPass = m_DeferredCombineRenderPass;
			renderPassBeginInfo.renderArea.offset = { 0, 0 };
			renderPassBeginInfo.renderArea.extent = m_SwapChainExtent;
			renderPassBeginInfo.clearValueCount = clearValues.size();
			renderPassBeginInfo.pClearValues = clearValues.data();
			
			VulkanRenderObject* gBufferObject = GetRenderObject(m_GBufferQuadRenderID);
			VulkanMaterial* gBufferMaterial = &m_LoadedMaterials[gBufferObject->materialID];

			for (size_t i = 0; i < m_CommandBuffers.size(); ++i)
			{
				VkCommandBuffer& commandBuffer = m_CommandBuffers[i];

				renderPassBeginInfo.framebuffer = m_SwapChainFramebuffers[i];

				VkCommandBufferBeginInfo cmdBufferbeginInfo = {};
				cmdBufferbeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				cmdBufferbeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

				VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &cmdBufferbeginInfo));

				vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport = VkViewport{ 0.0f, 1.0f, (float)m_SwapChainExtent.width, (float)m_SwapChainExtent.height, 0.1f, 1000.0f };
				vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

				VkRect2D scissor = VkRect2D{ { 0u, 0u },{ m_SwapChainExtent.width, m_SwapChainExtent.height } };
				vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

				BindDescriptorSet(&m_Shaders[gBufferMaterial->material.shaderID], gBufferObject->renderID, commandBuffer, gBufferObject->pipelineLayout, gBufferObject->descriptorSet);

				// Final composition as full screen quad (deferred combine)
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gBufferObject->graphicsPipeline);
				VkDeviceSize offsets[1] = { 0 };
				vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_VertexIndexBufferPairs[gBufferMaterial->material.shaderID].vertexBuffer->m_Buffer, offsets);
				vkCmdBindIndexBuffer(commandBuffer, m_VertexIndexBufferPairs[gBufferMaterial->material.shaderID].indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(commandBuffer, m_VertexIndexBufferPairs[gBufferMaterial->material.shaderID].indexCount, 1, 0, 0, 1);


				vkCmdNextSubpass(commandBuffer, VK_SUBPASS_CONTENTS_INLINE);


				// Forward rendered objects

				// TODO: Batch objects with same materials together like in GL renderer
				for (size_t j = 0; j < m_RenderObjects.size(); ++j)
				{
					VulkanRenderObject* renderObject = GetRenderObject(j);
					if (!renderObject || !renderObject->visible) continue;

					VulkanMaterial* material = &m_LoadedMaterials[renderObject->materialID];

					// Only render non-deferred (forward) objects in this pass
					if (m_Shaders[material->material.shaderID].shader.deferred) continue;

					vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_VertexIndexBufferPairs[material->material.shaderID].vertexBuffer->m_Buffer, offsets);

					if (m_VertexIndexBufferPairs[material->material.shaderID].indexBuffer->m_Size != 0)
					{
						vkCmdBindIndexBuffer(commandBuffer, m_VertexIndexBufferPairs[material->material.shaderID].indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
					}

					vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderObject->graphicsPipeline);

					// Push constants
					if (m_Shaders[material->material.shaderID].shader.needPushConstantBlock)
					{
						glm::mat4 view = glm::mat4(glm::mat3(gameContext.camera->GetView())); // Truncate translation part off to center around viwer
						glm::mat4 projection = gameContext.camera->GetProjection();
						m_LoadedMaterials[renderObject->materialID].material.pushConstantBlock.mvp =
							projection * view * glm::mat4(1.0f); // renderObject->model; TODO
						vkCmdPushConstants(commandBuffer, renderObject->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Material::PushConstantBlock), &m_LoadedMaterials[renderObject->materialID].material.pushConstantBlock);
					}

					BindDescriptorSet(&m_Shaders[m_LoadedMaterials[renderObject->materialID].material.shaderID], renderObject->renderID, commandBuffer, renderObject->pipelineLayout, renderObject->descriptorSet);

					if (renderObject->indexed)
					{
						vkCmdDrawIndexed(commandBuffer, m_VertexIndexBufferPairs[material->material.shaderID].indexCount, 1, renderObject->indexOffset, 0, 0);
					}
					else
					{
						vkCmdDraw(commandBuffer, renderObject->vertexBufferData->VertexCount, 1, renderObject->vertexOffset, 0);
					}
				}

				ImGui_DrawFrame(commandBuffer);


				vkCmdEndRenderPass(commandBuffer);

				VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));
			}
		}

		void VulkanRenderer::BuildDeferredCommandBuffer(const GameContext& gameContext)
		{
			if (offScreenCmdBuffer == VK_NULL_HANDLE)
			{
				offScreenCmdBuffer = CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
			}

			if (offscreenSemaphore == VK_NULL_HANDLE)
			{
				VkSemaphoreCreateInfo semaphoreCreateInfo = {};
				semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
				VK_CHECK_RESULT(vkCreateSemaphore(m_VulkanDevice->m_LogicalDevice, &semaphoreCreateInfo, nullptr, &offscreenSemaphore));
			}

			std::array<VkClearValue, 4> clearValues = {};
			clearValues[0].color = m_ClearColor;
			clearValues[1].color = m_ClearColor;
			clearValues[2].color = m_ClearColor;
			clearValues[3].depthStencil = { 1.0f, 0 };

			VkRenderPassBeginInfo renderPassBeginInfo = {};
			renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassBeginInfo.renderPass = offScreenFrameBuf->renderPass;
			renderPassBeginInfo.framebuffer = offScreenFrameBuf->frameBuffer;
			renderPassBeginInfo.renderArea.offset = { 0, 0 };
			renderPassBeginInfo.renderArea.extent.width = offScreenFrameBuf->width;
			renderPassBeginInfo.renderArea.extent.height = offScreenFrameBuf->height;
			renderPassBeginInfo.clearValueCount = clearValues.size();
			renderPassBeginInfo.pClearValues = clearValues.data();

			VkCommandBufferBeginInfo cmdBufferbeginInfo = {};
			cmdBufferbeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			cmdBufferbeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

			VK_CHECK_RESULT(vkBeginCommandBuffer(offScreenCmdBuffer, &cmdBufferbeginInfo));
			
			vkCmdBeginRenderPass(offScreenCmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			// TODO: Make min and max values members
			VkViewport viewport = VkViewport{ 0.0f, 1.0f, (float)offScreenFrameBuf->width, (float)offScreenFrameBuf->height, 0.1f, 1000.0f };
			vkCmdSetViewport(offScreenCmdBuffer, 0, 1, &viewport);

			VkRect2D scissor = VkRect2D{ { 0u, 0u },{ offScreenFrameBuf->width, offScreenFrameBuf->height } };
			vkCmdSetScissor(offScreenCmdBuffer, 0, 1, &scissor);

			VkDeviceSize offsets[1] = { 0 };

			// TODO: Batch objects with same materials together like in GL renderer
			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				VulkanRenderObject* renderObject = GetRenderObject(i);
				if (!renderObject || !renderObject->visible) continue;

				VulkanMaterial* material = &m_LoadedMaterials[renderObject->materialID];

				// Only render deferred objects in this pass
				if (!m_Shaders[material->material.shaderID].shader.deferred) continue;

				vkCmdBindVertexBuffers(offScreenCmdBuffer, 0, 1, &m_VertexIndexBufferPairs[material->material.shaderID].vertexBuffer->m_Buffer, offsets);

				if (m_VertexIndexBufferPairs[material->material.shaderID].indexBuffer->m_Size != 0)
				{
					vkCmdBindIndexBuffer(offScreenCmdBuffer, m_VertexIndexBufferPairs[material->material.shaderID].indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
				}

				vkCmdBindPipeline(offScreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderObject->graphicsPipeline);

				// Push constants
				if (m_Shaders[material->material.shaderID].shader.needPushConstantBlock)
				{
					glm::mat4 view = glm::mat4(glm::mat3(gameContext.camera->GetView())); // Truncate translation part off to center around viewer
					glm::mat4 projection = gameContext.camera->GetProjection();
					m_LoadedMaterials[renderObject->materialID].material.pushConstantBlock.mvp =
						projection * view * glm::mat4(1.0f); // renderObject->model; TODO
					vkCmdPushConstants(offScreenCmdBuffer, renderObject->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Material::PushConstantBlock), &m_LoadedMaterials[renderObject->materialID].material.pushConstantBlock);
				}

				BindDescriptorSet(&m_Shaders[material->material.shaderID], renderObject->renderID, offScreenCmdBuffer, renderObject->pipelineLayout, renderObject->descriptorSet);

				if (renderObject->indexed)
				{
					vkCmdDrawIndexed(offScreenCmdBuffer, m_VertexIndexBufferPairs[m_LoadedMaterials[renderObject->materialID].material.shaderID].indexCount, 1, renderObject->indexOffset, 0, 0);
				}
				else
				{
					vkCmdDraw(offScreenCmdBuffer, renderObject->vertexBufferData->VertexCount, 1, renderObject->vertexOffset, 0);
				}
			}

			vkCmdEndRenderPass(offScreenCmdBuffer);

			VK_CHECK_RESULT(vkEndCommandBuffer(offScreenCmdBuffer));
		}

		void VulkanRenderer::ImGui_UpdateBuffers()
		{
			ImDrawData* imDrawData = ImGui::GetDrawData();

			// Note: Alignment is done inside buffer creation
			VkDeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
			VkDeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

			// Update buffers only if vertex or index count has been changed compared to current buffer size

			VertexIndexBufferPair& bufferPair = m_VertexIndexBufferPairs[m_IGuiShaderID];

			// Vertex buffer
			if ((bufferPair.vertexBuffer->m_Buffer == VK_NULL_HANDLE) || ((int)bufferPair.vertexCount != imDrawData->TotalVtxCount)) {
				bufferPair.vertexBuffer->Unmap();
				bufferPair.vertexBuffer->Destroy();
				//assert(vertexBufferSize == bufferPair.vertexBuffer->m_Size);
				CreateAndAllocateBuffer(vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, bufferPair.vertexBuffer);
				bufferPair.vertexCount = imDrawData->TotalVtxCount;
				bufferPair.vertexBuffer->Unmap();
				bufferPair.vertexBuffer->Map();
			}

			// Index buffer
			if ((bufferPair.indexBuffer == VK_NULL_HANDLE) || ((int)bufferPair.indexCount < imDrawData->TotalIdxCount)) {
				bufferPair.indexBuffer->Unmap();
				bufferPair.indexBuffer->Destroy();
				//assert(indexBufferSize == bufferPair.indexBuffer->m_Size);
				CreateAndAllocateBuffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, bufferPair.indexBuffer);
				bufferPair.indexCount = imDrawData->TotalIdxCount;
				bufferPair.indexBuffer->Map();
			}

			// Upload data
			ImDrawVert* vtxDst = (ImDrawVert*)bufferPair.vertexBuffer->m_Mapped;
			ImDrawIdx* idxDst = (ImDrawIdx*)bufferPair.indexBuffer->m_Mapped;

			for (int n = 0; n < imDrawData->CmdListsCount; ++n)
			{
				const ImDrawList* cmd_list = imDrawData->CmdLists[n];
				memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
				memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
				vtxDst += cmd_list->VtxBuffer.Size;
				idxDst += cmd_list->IdxBuffer.Size;
			}

			// Flush to make writes visible to GPU
			bufferPair.vertexBuffer->Flush();
			bufferPair.indexBuffer->Flush();
		}

		bool VulkanRenderer::CheckCommandBuffers() const
		{
			for (auto& cmdBuffer : m_CommandBuffers)
			{
				if (cmdBuffer == VK_NULL_HANDLE)
				{
					return false;
				}
			}
			return true;
		}

		void VulkanRenderer::FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool free) const
		{
			if (commandBuffer == VK_NULL_HANDLE)
			{
				return;
			}

			VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

			VkSubmitInfo submitInfo = {};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffer;

			VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
			VK_CHECK_RESULT(vkQueueWaitIdle(queue));

			if (free)
			{
				vkFreeCommandBuffers(m_VulkanDevice->m_LogicalDevice, m_VulkanDevice->m_CommandPool, 1, &commandBuffer);
			}
		}

		void VulkanRenderer::DestroyCommandBuffers()
		{
			vkFreeCommandBuffers(m_VulkanDevice->m_LogicalDevice, m_VulkanDevice->m_CommandPool, static_cast<uint32_t>(m_CommandBuffers.size()), m_CommandBuffers.data());
		}

		void VulkanRenderer::BindDescriptorSet(VulkanShader* shader, RenderID renderID, VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, VkDescriptorSet descriptorSet)
		{
			uint32_t dynamicOffset = renderID * static_cast<uint32_t>(m_DynamicAlignment);
			uint32_t* dynamicOffsetPtr = nullptr;
			uint32_t dynamicOffsetCount = 0;
			if (shader->uniformBuffer.dynamicBuffer.m_Size != 0)
			{
				// This shader uses a dynamic buffer, so it needs a dynamic offset
				dynamicOffsetPtr = &dynamicOffset;
				dynamicOffsetCount = 1;
			}

			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
				0, 1, &descriptorSet,
				dynamicOffsetCount, dynamicOffsetPtr);
		}

		uint32_t VulkanRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
		{
			VkPhysicalDeviceMemoryProperties memProperties;
			vkGetPhysicalDeviceMemoryProperties(m_VulkanDevice->m_PhysicalDevice, &memProperties);

			for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i)
			{
				if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
				{
					return i;
				}
			}

			throw std::runtime_error("Failed to find any suitable memory type!");
		}

		void VulkanRenderer::TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, glm::uint mipLevels) const
		{
			VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

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

			vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &barrier);

			EndSingleTimeCommands(commandBuffer);
		}

		void VulkanRenderer::CopyImage(VkImage srcImage, VkImage dstImage, uint32_t width, uint32_t height) const
		{
			VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

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

			EndSingleTimeCommands(commandBuffer);
		}

		void VulkanRenderer::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const
		{
			VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

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

			EndSingleTimeCommands(commandBuffer);
		}

		void VulkanRenderer::CreateAndAllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VulkanBuffer* buffer) const
		{
			VkBufferCreateInfo bufferInfo = {};
			bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferInfo.size = size;
			bufferInfo.usage = usage;
			bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VK_CHECK_RESULT(vkCreateBuffer(m_VulkanDevice->m_LogicalDevice, &bufferInfo, nullptr, buffer->m_Buffer.replace()));

			VkMemoryRequirements memRequirements;
			vkGetBufferMemoryRequirements(m_VulkanDevice->m_LogicalDevice, buffer->m_Buffer, &memRequirements);

			VkMemoryAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memRequirements.size;
			allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

			VK_CHECK_RESULT(vkAllocateMemory(m_VulkanDevice->m_LogicalDevice, &allocInfo, nullptr, buffer->m_Memory.replace()));

			// Create the memory backing up the buffer handle
			buffer->m_Alignment = memRequirements.alignment;
			buffer->m_Size = allocInfo.allocationSize;
			buffer->m_UsageFlags = usage;
			buffer->m_MemoryPropertyFlags = properties;

			// Initialize a default descriptor that covers the whole buffer size
			buffer->m_DescriptorInfo.offset = 0;
			buffer->m_DescriptorInfo.range = VK_WHOLE_SIZE;
			buffer->m_DescriptorInfo.buffer = buffer->m_Buffer;

			vkBindBufferMemory(m_VulkanDevice->m_LogicalDevice, buffer->m_Buffer, buffer->m_Memory, 0);
		}

		void VulkanRenderer::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset) const
		{
			VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

			VkBufferCopy copyRegion = {};
			copyRegion.size = size;
			copyRegion.dstOffset = dstOffset;
			copyRegion.srcOffset = srcOffset;
			vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

			EndSingleTimeCommands(commandBuffer);
		}

		void VulkanRenderer::CreateStaticVertexBuffers()
		{
			for (size_t i = 0; i < m_VertexIndexBufferPairs.size(); ++i)
			{
				if (m_VertexIndexBufferPairs[i].useStagingBuffer)
				{
					size_t requiredMemory = 0;

					for (VulkanRenderObject* renderObject : m_RenderObjects)
					{
						if (renderObject && m_LoadedMaterials[renderObject->materialID].material.shaderID == i)
						{
							requiredMemory += renderObject->vertexBufferData->BufferSize;
						}
					}

					if (requiredMemory > 0)
					{
						m_VertexIndexBufferPairs[i].vertexCount = CreateStaticVertexBuffer(
							m_VertexIndexBufferPairs[i].vertexBuffer,
							i,
							requiredMemory);
					}
				}
			}
		}

		glm::uint VulkanRenderer::CreateStaticVertexBuffer(VulkanBuffer* vertexBuffer, ShaderID shaderID, int size)
		{
			void* vertexDataStart = malloc(size);
			if (!vertexDataStart)
			{
				Logger::LogError("Failed to allocate memory for vertex buffer " + std::to_string(shaderID) + "! Attempted to allocate " + std::to_string(size) + " bytes");
				return 0;
			}

			void* vertexBufferData = vertexDataStart;

			glm::uint vertexCount = 0;
			glm::uint vertexBufferSize = 0;
			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject && m_LoadedMaterials[renderObject->materialID].material.shaderID == shaderID)
				{
					renderObject->vertexOffset = vertexCount;

					memcpy(vertexBufferData, renderObject->vertexBufferData->pDataStart, renderObject->vertexBufferData->BufferSize);

					vertexCount += renderObject->vertexBufferData->VertexCount;
					vertexBufferSize += renderObject->vertexBufferData->BufferSize;

					vertexBufferData = (char*)vertexBufferData + renderObject->vertexBufferData->BufferSize;
				}
			}

			if (vertexBufferSize == 0 || vertexCount == 0)
			{
				Logger::LogError("Failed to create static vertex buffer (no verts use shader index " + std::to_string(shaderID) + "!)");
				return 0;
			}

			CreateStaticVertexBuffer(vertexBuffer, vertexDataStart, vertexBufferSize);
			free(vertexDataStart);

			return vertexCount;
		}

		void VulkanRenderer::CreateStaticVertexBuffer(VulkanBuffer* vertexBuffer, void* vertexBufferData, glm::uint vertexBufferSize)
		{
			VulkanBuffer stagingBuffer(m_VulkanDevice->m_LogicalDevice);
			CreateAndAllocateBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
				VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer);

			stagingBuffer.Map(vertexBufferSize);
			memcpy(stagingBuffer.m_Mapped, vertexBufferData, vertexBufferSize);
			stagingBuffer.Unmap();

			CreateAndAllocateBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer);

			CopyBuffer(stagingBuffer.m_Buffer, vertexBuffer->m_Buffer, vertexBufferSize);
		}

		void VulkanRenderer::CreateStaticIndexBuffers()
		{
			for (size_t i = 0; i < m_VertexIndexBufferPairs.size(); ++i)
			{
				m_VertexIndexBufferPairs[i].indexCount = CreateStaticIndexBuffer(m_VertexIndexBufferPairs[i].indexBuffer, i);
			}
		}

		glm::uint VulkanRenderer::CreateStaticIndexBuffer(VulkanBuffer* indexBuffer, ShaderID shaderID)
		{
			std::vector<glm::uint> indices;

			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject && m_LoadedMaterials[renderObject->materialID].material.shaderID == shaderID && renderObject->indexed)
				{
					renderObject->indexOffset = indices.size();
					indices.insert(indices.end(), renderObject->indices->begin(), renderObject->indices->end());
				}
			}

			if (indices.empty())
			{
				// No indexed render objects use specified shader
				return 0;
			}

			CreateStaticIndexBuffer(indexBuffer, indices);

			return indices.size();
		}

		void VulkanRenderer::CreateStaticIndexBuffer(VulkanBuffer* indexBuffer, const std::vector<glm::uint>& indices)
		{
			const size_t bufferSize = sizeof(indices[0]) * indices.size();

			VulkanBuffer stagingBuffer(m_VulkanDevice->m_LogicalDevice);
			CreateAndAllocateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer);

			stagingBuffer.Map(bufferSize);
			memcpy(stagingBuffer.m_Mapped, indices.data(), bufferSize);
			stagingBuffer.Unmap();

			CreateAndAllocateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer);

			CopyBuffer(stagingBuffer.m_Buffer, indexBuffer->m_Buffer, bufferSize);
		}

		glm::uint VulkanRenderer::AllocateUniformBuffer(glm::uint dynamicDataSize, void** data)
		{
			size_t uboAlignment = (size_t)m_VulkanDevice->m_PhysicalDeviceProperties.limits.minUniformBufferOffsetAlignment;
			glm::uint dynamicAllignment =
				(dynamicDataSize / uboAlignment) * uboAlignment +
				((dynamicDataSize % uboAlignment) > 0 ? uboAlignment : 0);

			if (dynamicAllignment > m_DynamicAlignment)
			{
				glm::uint newDynamicAllignment = 1;
				while (newDynamicAllignment < dynamicAllignment)
				{
					newDynamicAllignment <<= 1;
				}
				m_DynamicAlignment = newDynamicAllignment;
			}

			size_t dynamicBufferSize = m_RenderObjects.size() * m_DynamicAlignment;

			(*data) = _aligned_malloc(dynamicBufferSize, m_DynamicAlignment);
			assert(*data);

			return dynamicBufferSize;
		}

		void VulkanRenderer::PrepareUniformBuffer(VulkanBuffer* buffer, glm::uint bufferSize,
			VkBufferUsageFlags bufferUseageFlagBits, VkMemoryPropertyFlags memoryPropertyHostFlagBits)
		{
			CreateAndAllocateBuffer(bufferSize, bufferUseageFlagBits, memoryPropertyHostFlagBits, buffer);

			VK_CHECK_RESULT(vkMapMemory(m_VulkanDevice->m_LogicalDevice, buffer->m_Memory, 0, VK_WHOLE_SIZE, 0, &buffer->m_Mapped));
		}

		void VulkanRenderer::CreateDescriptorPool()
		{
			const size_t descriptorSetCount = 1000;

			std::vector<VkDescriptorPoolSize> poolSizes{
				{ VK_DESCRIPTOR_TYPE_SAMPLER, descriptorSetCount },
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, descriptorSetCount },
				{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptorSetCount },
				{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSetCount },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, descriptorSetCount },
				{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, descriptorSetCount },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, descriptorSetCount },
				{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSetCount },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, descriptorSetCount },
				{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, descriptorSetCount },
				{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, descriptorSetCount },
			};

			VkDescriptorPoolCreateInfo poolInfo = {};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = poolSizes.size();
			poolInfo.pPoolSizes = poolSizes.data();
			poolInfo.maxSets = descriptorSetCount * 11;
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // Allow descriptor sets to be added/removed often

			VK_CHECK_RESULT(vkCreateDescriptorPool(m_VulkanDevice->m_LogicalDevice, &poolInfo, nullptr, m_DescriptorPool.replace()));
		}

		void VulkanRenderer::CreateSemaphores()
		{
			VkSemaphoreCreateInfo semaphoreInfo = {};
			semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

			VK_CHECK_RESULT(vkCreateSemaphore(m_VulkanDevice->m_LogicalDevice, &semaphoreInfo, nullptr, m_PresentCompleteSemaphore.replace()));
			VK_CHECK_RESULT(vkCreateSemaphore(m_VulkanDevice->m_LogicalDevice, &semaphoreInfo, nullptr, m_RenderCompleteSemaphore.replace()));
		}

		void VulkanRenderer::DrawFrame(Window* window)
		{
			uint32_t imageIndex;
			VkResult result = vkAcquireNextImageKHR(m_VulkanDevice->m_LogicalDevice, m_SwapChain, std::numeric_limits<uint64_t>::max(), m_PresentCompleteSemaphore, VK_NULL_HANDLE, &imageIndex);

			if (result == VK_ERROR_OUT_OF_DATE_KHR)
			{
				RecreateSwapChain(window);
				return;
			}
			else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
			{
				throw std::runtime_error("failed to acquire swap chain image!");
			}

			// Offscreen rendering

			VkSubmitInfo submitInfo = {};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

			VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			submitInfo.pWaitDstStageMask = &waitStages;

			submitInfo.waitSemaphoreCount = 1;
			submitInfo.pWaitSemaphores = &m_PresentCompleteSemaphore;

			submitInfo.signalSemaphoreCount = 1;
			submitInfo.pSignalSemaphores = &offscreenSemaphore;

			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &offScreenCmdBuffer;

			VK_CHECK_RESULT(vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));


			// Scene rendering

			submitInfo.pWaitSemaphores = &offscreenSemaphore;
			submitInfo.pSignalSemaphores = &m_RenderCompleteSemaphore;

			submitInfo.pCommandBuffers = &m_CommandBuffers[imageIndex];
			VK_CHECK_RESULT(vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));



			VkPresentInfoKHR presentInfo = {};
			presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

			presentInfo.waitSemaphoreCount = 1;
			presentInfo.pWaitSemaphores = &m_RenderCompleteSemaphore;

			presentInfo.swapchainCount = 1;
			presentInfo.pSwapchains = &m_SwapChain;

			presentInfo.pImageIndices = &imageIndex;

			result = vkQueuePresentKHR(m_PresentQueue, &presentInfo);

			if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
			{
				RecreateSwapChain(window);
			}
			else
			{
				VK_CHECK_RESULT(result);
			}

			VK_CHECK_RESULT(vkQueueWaitIdle(m_PresentQueue));
		}

		bool VulkanRenderer::CreateShaderModule(const std::vector<char>& code, VDeleter<VkShaderModule>& shaderModule) const
		{
			VkShaderModuleCreateInfo createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			createInfo.codeSize = code.size();
			createInfo.pCode = (uint32_t*)code.data();

			VkResult result = vkCreateShaderModule(m_VulkanDevice->m_LogicalDevice, &createInfo, nullptr, shaderModule.replace());
			VK_CHECK_RESULT(result);

			return (result == VK_SUCCESS);
		}

		VkSurfaceFormatKHR VulkanRenderer::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const
		{
			if (availableFormats.size() == 1 && availableFormats[0].format == VK_FORMAT_UNDEFINED)
			{
				return{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
			}

			for (const auto& availableFormat : availableFormats)
			{
				if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
					return availableFormat;
				}
			}

			return availableFormats[0];
		}

		VkPresentModeKHR VulkanRenderer::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR> availablePresentModes) const
		{
			const VkPresentModeKHR bestMode = (m_VSyncEnabled ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR);
			VkPresentModeKHR secondBestMode = bestMode;

			for (const auto& availablePresentMode : availablePresentModes)
			{
				if (availablePresentMode == bestMode) return availablePresentMode;

				if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
				{
					secondBestMode = availablePresentMode;
				}
				else if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR)
				{
					secondBestMode = availablePresentMode;
				}
			}

			return secondBestMode;
		}

		VkExtent2D VulkanRenderer::ChooseSwapExtent(Window* window, const VkSurfaceCapabilitiesKHR& capabilities) const
		{
			if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
			{
				return capabilities.currentExtent;
			}
			else
			{
				int width, height;
				glfwGetWindowSize(((VulkanWindowWrapper*)window)->GetWindow(), &width, &height);

				VkExtent2D actualExtent = { (uint32_t)width, (uint32_t)height };

				actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
				actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));

				return actualExtent;
			}
		}

		VulkanSwapChainSupportDetails VulkanRenderer::QuerySwapChainSupport(VkPhysicalDevice device) const
		{
			VulkanSwapChainSupportDetails details;

			vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_Surface, &details.capabilities);

			uint32_t formatCount;
			vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, nullptr);

			if (formatCount != 0)
			{
				details.formats.resize(formatCount);
				vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, details.formats.data());
			}

			uint32_t presentModeCount;
			vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, nullptr);

			if (presentModeCount != 0)
			{
				details.presentModes.resize(presentModeCount);
				vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, details.presentModes.data());
			}

			return details;
		}

		bool VulkanRenderer::IsDeviceSuitable(VkPhysicalDevice device) const
		{
			VulkanQueueFamilyIndices indices = FindQueueFamilies(device);

			bool extensionsSupported = CheckDeviceExtensionSupport(device);

			bool swapChainAdequate = false;
			if (extensionsSupported)
			{
				VulkanSwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(device);
				swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
			}

			VkPhysicalDeviceFeatures supportedFeatures;
			vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

			return indices.IsComplete() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy;
		}

		bool VulkanRenderer::CheckDeviceExtensionSupport(VkPhysicalDevice device) const
		{
			uint32_t extensionCount;
			vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

			std::vector<VkExtensionProperties> availableExtensions(extensionCount);
			vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

			std::set<std::string> requiredExtensions(m_DeviceExtensions.begin(), m_DeviceExtensions.end());

			for (const auto& extension : availableExtensions)
			{
				requiredExtensions.erase(extension.extensionName);
			}

			return requiredExtensions.empty();
		}

		VulkanQueueFamilyIndices VulkanRenderer::FindQueueFamilies(VkPhysicalDevice device) const
		{
			// TODO: Move to VulkanDevice class?
			VulkanQueueFamilyIndices indices;

			uint32_t queueFamilyCount;
			vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
			assert(queueFamilyCount > 0);
			std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilyProperties.data());

			int i = 0;
			for (const auto& queueFamily : queueFamilyProperties)
			{
				if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
				{
					indices.graphicsFamily = i;
				}

				VkBool32 presentSupport = false;
				vkGetPhysicalDeviceSurfaceSupportKHR(device, (glm::uint32)i, m_Surface, &presentSupport);

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

		std::vector<const char*> VulkanRenderer::GetRequiredExtensions() const
		{
			std::vector<const char*> extensions;

			unsigned int glfwExtensionCount = 0;
			const char** glfwExtensions;
			glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

			for (unsigned int i = 0; i < glfwExtensionCount; ++i)
			{
				extensions.push_back(glfwExtensions[i]);
			}

			if (m_EnableValidationLayers)
			{
				extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
			}

			return extensions;
		}

		bool VulkanRenderer::CheckValidationLayerSupport() const
		{
			uint32_t layerCount;
			vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

			std::vector<VkLayerProperties> availableLayers(layerCount);
			vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

			for (const char* layerName : m_ValidationLayers)
			{
				bool layerFound = false;

				for (const auto& layerProperties : availableLayers)
				{
					if (strcmp(layerName, layerProperties.layerName) == 0)
					{
						layerFound = true;
						break;
					}
				}

				if (!layerFound)
				{
					return false;
				}
			}

			return true;
		}

		void VulkanRenderer::UpdateConstantUniformBuffers(const GameContext& gameContext, UniformOverrides const* overridenUniforms)
		{
			for (size_t i = 0; i < m_LoadedMaterials.size(); ++i)
			{
				UpdateConstantUniformBuffer(gameContext, overridenUniforms, i);
			}
		}

		void VulkanRenderer::UpdateConstantUniformBuffer(const GameContext& gameContext, UniformOverrides const* overridenUniforms, size_t bufferIndex)
		{
			Uniforms& constantUniforms = m_Shaders[m_LoadedMaterials[bufferIndex].material.shaderID].shader.constantBufferUniforms;
			VulkanUniformBufferObjectData& constantData = m_Shaders[m_LoadedMaterials[bufferIndex].material.shaderID].uniformBuffer.constantData;

			if (!constantData.data) return; // There is no constant data

			glm::mat4 projection = gameContext.camera->GetProjection();
			glm::mat4 view = gameContext.camera->GetView();
			glm::mat4 viewInv = glm::inverse(view);
			glm::mat4 viewProjection = projection * view;
			glm::vec4 camPos = glm::vec4(gameContext.camera->GetPosition(), 0.0f);

			if (overridenUniforms)
			{
				if (overridenUniforms->overridenUniforms.HasUniform("projection")) projection = overridenUniforms->projection;
				if (overridenUniforms->overridenUniforms.HasUniform("view")) view = overridenUniforms->view;
				if (overridenUniforms->overridenUniforms.HasUniform("viewInv")) viewInv = overridenUniforms->viewInv;
				if (overridenUniforms->overridenUniforms.HasUniform("viewProjection")) viewProjection = overridenUniforms->viewProjection;
				if (overridenUniforms->overridenUniforms.HasUniform("camPos")) camPos = overridenUniforms->camPos;
			}

			void* pointLightsDataStart = nullptr;
			size_t pointLightsSize = 0;
			size_t pointLightsMoveInBytes = 0;
			if (!m_PointLights.empty())
			{
				pointLightsDataStart = (void*)&m_PointLights[0];
				pointLightsSize = sizeof(m_PointLights[0]) * m_PointLights.size();
				pointLightsMoveInBytes = pointLightsSize / sizeof(float);
			}

			struct UniformInfo
			{
				std::string uniformName;
				void* dataStart = nullptr;
				size_t copySize;
				size_t moveInBytes;
			};
			UniformInfo uniformInfos[] = {
				{ "view", (void*)&view, sizeof(glm::mat4), 16 },
				{ "viewInv", (void*)&viewInv, sizeof(glm::mat4), 16 },
				{ "projection", (void*)&projection, sizeof(glm::mat4), 16 },
				{ "viewProjection", (void*)&viewProjection, sizeof(glm::mat4), 16 },
				{ "camPos", (void*)&camPos, sizeof(glm::vec4), 4 },
				{ "dirLight", (void*)&m_DirectionalLight, sizeof(m_DirectionalLight), sizeof(m_DirectionalLight) / sizeof(float) },
				{ "pointLights", (void*)pointLightsDataStart, pointLightsSize, pointLightsMoveInBytes },
			};

			size_t index = 0;
			for (UniformInfo& uniformInfo : uniformInfos)
			{
				if (constantUniforms.HasUniform(uniformInfo.uniformName))
				{
					memcpy(&constantData.data[index], uniformInfo.dataStart, uniformInfo.copySize);
					index += uniformInfo.moveInBytes;
				}
			}

			glm::uint size = constantData.size;

#if  _DEBUG
			glm::uint calculatedSize1 = index * 4;
			assert(calculatedSize1 == size);
#endif // _DEBUG

			memcpy(m_Shaders[m_LoadedMaterials[bufferIndex].material.shaderID].uniformBuffer.constantBuffer.m_Mapped, constantData.data, size);
		}
			
		void VulkanRenderer::UpdateDynamicUniformBuffer(const GameContext& gameContext, RenderID renderID, UniformOverrides const* uniformOverrides)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject) return;

			VulkanMaterial* material = &m_LoadedMaterials[renderObject->materialID];
			VulkanShader* shader = &m_Shaders[material->material.shaderID];

			UniformBuffer& uniformBuffer = shader->uniformBuffer;
			Uniforms dynamicUniforms = shader->shader.dynamicBufferUniforms;

			if (uniformBuffer.dynamicBuffer.m_Size == 0) return; // There are no dynamic uniforms to update

			bool updateMVP = false; // This is set to true when either the view or projection matrix get overriden

			glm::mat4 model = renderObject->transform->GetModelMatrix();
			glm::mat4 modelInvTranspose = glm::transpose(glm::inverse(model));
			glm::mat4 projection = gameContext.camera->GetProjection();
			glm::mat4 view = gameContext.camera->GetView();
			glm::mat4 modelViewProjection = projection * view * model;
			glm::uint enableAlbedoSampler = material->material.enableAlbedoSampler;
			glm::uint enableMetallicSampler = material->material.enableMetallicSampler;
			glm::uint enableRoughnessSampler = material->material.enableRoughnessSampler;
			glm::uint enableAOSampler = material->material.enableAOSampler;
			glm::uint enableDiffuseSampler = material->material.enableDiffuseSampler;
			glm::uint enableNormalSampler = material->material.enableNormalSampler;
			glm::uint enableSpecularSampler = material->material.enableSpecularSampler;
			glm::uint enableCubemapSampler = material->material.enableCubemapSampler;
			glm::uint enableIrradianceSampler = material->material.enableIrradianceSampler;

			// TODO: Roll into array?
			if (uniformOverrides)
			{
				if (uniformOverrides->overridenUniforms.HasUniform("model"))
				{
					model = uniformOverrides->model;
				}
				if (uniformOverrides->overridenUniforms.HasUniform("modelInvTranspose"))
				{
					modelInvTranspose = uniformOverrides->modelInvTranspose;
				}
				if (uniformOverrides->overridenUniforms.HasUniform("projection"))
				{
					projection = uniformOverrides->projection;
					updateMVP = true;
				}
				if (uniformOverrides->overridenUniforms.HasUniform("view"))
				{
					view = uniformOverrides->view;
					updateMVP = true;
				}
				if (uniformOverrides->overridenUniforms.HasUniform("modelViewProjection"))
				{
					modelViewProjection = uniformOverrides->modelViewProjection;
					updateMVP = false;	// Don't override modelViewProjection value with overriden view/projection matrices 
										// if it's being specifically overriden itself
				}
				if (uniformOverrides->overridenUniforms.HasUniform("enableAlbedoSampler"))
				{
					enableAlbedoSampler = uniformOverrides->enableAlbedoSampler;
				}
				if (uniformOverrides->overridenUniforms.HasUniform("enableMetallicSampler"))
				{
					enableMetallicSampler = uniformOverrides->enableMetallicSampler;
				}
				if (uniformOverrides->overridenUniforms.HasUniform("enableRoughnessSampler"))
				{
					enableRoughnessSampler = uniformOverrides->enableRoughnessSampler;
				}
				if (uniformOverrides->overridenUniforms.HasUniform("enableAOSampler"))
				{
					enableAOSampler = uniformOverrides->enableAOSampler;
				}
				if (uniformOverrides->overridenUniforms.HasUniform("enableDiffuseSampler"))
				{
					enableDiffuseSampler = uniformOverrides->enableDiffuseSampler;
				}
				if (uniformOverrides->overridenUniforms.HasUniform("enableNormalSampler"))
				{
					enableNormalSampler = uniformOverrides->enableNormalSampler;
				}
				if (uniformOverrides->overridenUniforms.HasUniform("enableSpecularSampler"))
				{
					enableSpecularSampler = uniformOverrides->enableSpecularSampler;
				}
				if (uniformOverrides->overridenUniforms.HasUniform("enableCubemapSampler"))
				{
					enableCubemapSampler = uniformOverrides->enableCubemapSampler;
				}
				if (uniformOverrides->overridenUniforms.HasUniform("enableIrradianceSampler"))
				{
					enableIrradianceSampler = uniformOverrides->enableIrradianceSampler;
				}
			}

			if (updateMVP)
			{
				modelViewProjection = projection * view * model;
				modelInvTranspose = glm::transpose(glm::inverse(model));
			}

			glm::uint offset = 0;
			for (VulkanRenderObject* renderObj : m_RenderObjects)
			{
				if (renderObj->renderID != renderObject->renderID &&
					m_LoadedMaterials[renderObj->materialID].material.shaderID == m_LoadedMaterials[renderObject->materialID].material.shaderID)
				{
					offset += uniformBuffer.dynamicData.size;
				}
			}
			glm::uint index = 0;

			struct UniformInfo
			{
				std::string uniformName;
				void* dataStart = nullptr;
				size_t copySize;
				size_t moveInBytes;
			};
			UniformInfo uniformInfos[] = {
				{ "model", (void*)&model, 64, 16 },
				{ "modelInvTranspose", (void*)&modelInvTranspose, 64, 16 },
				{ "modelViewProjection", (void*)&modelViewProjection, 64, 16 },
				// view, viewInv, viewProjection, projection, camPos, dirLight, pointLights should be updated in constant uniform buffer
				{ "constAlbedo", (void*)&material->material.constAlbedo, 16, 4 },
				{ "constMetallic", (void*)&material->material.constMetallic, 4, 1 },
				{ "constRoughness", (void*)&material->material.constRoughness, 4, 1 },
				{ "constAO", (void*)&material->material.constAO, 4, 1 },
				{ "enableAlbedoSampler", (void*)&enableAlbedoSampler, 4, 1 },
				{ "enableMetallicSampler", (void*)&enableMetallicSampler, 4, 1 },
				{ "enableRoughnessSampler", (void*)&enableRoughnessSampler, 4, 1 },
				{ "enableAOSampler", (void*)&enableAOSampler, 4, 1 },
				{ "enableDiffuseSampler", (void*)&enableDiffuseSampler, 4, 1 },
				{ "enableNormalSampler", (void*)&enableNormalSampler, 4, 1 },
				{ "enableSpecularSampler", (void*)&enableSpecularSampler, 4, 1 },
				{ "enableCubemapSampler", (void*)&enableCubemapSampler, 4, 1 },
				{ "enableIrradianceSampler", (void*)&enableIrradianceSampler, 4, 1 },
			};

			for (UniformInfo& uniformInfo : uniformInfos)
			{
				if (dynamicUniforms.HasUniform(uniformInfo.uniformName))
				{
					memcpy(&uniformBuffer.dynamicData.data[offset + index], uniformInfo.dataStart, uniformInfo.copySize);
					index += uniformInfo.moveInBytes;
				}
			}

			// Aligned offset
			glm::uint size = uniformBuffer.dynamicData.size;

#if  _DEBUG
			glm::uint calculatedSize1 = index * 4;
			assert(calculatedSize1 == size);
#endif // _DEBUG

			void* firstIndex = uniformBuffer.dynamicBuffer.m_Mapped;
			uint64_t dest = (uint64_t)firstIndex + (renderID * m_DynamicAlignment);
			memcpy((void*)(dest), &uniformBuffer.dynamicData.data[offset], size);

			// Flush to make changes visible to the host 
			VkMappedMemoryRange mappedMemoryRange{};
			mappedMemoryRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
			mappedMemoryRange.offset = offset;
			mappedMemoryRange.memory = uniformBuffer.dynamicBuffer.m_Memory;
			mappedMemoryRange.size = uniformBuffer.dynamicData.size;
			vkFlushMappedMemoryRanges(m_VulkanDevice->m_LogicalDevice, 1, &mappedMemoryRange);
		}

		void VulkanRenderer::LoadDefaultShaderCode()
		{
			const std::string shaderDirectory = RESOURCE_LOCATION + "shaders/GLSL/spv/";
			m_Shaders = {
				{ "deferred_simple", shaderDirectory + "vk_deferred_simple_vert.spv", shaderDirectory + "vk_deferred_simple_frag.spv", m_VulkanDevice->m_LogicalDevice },
				{ "color",  shaderDirectory + "vk_color_vert.spv", shaderDirectory + "vk_color_frag.spv", m_VulkanDevice->m_LogicalDevice },
				{ "imgui", shaderDirectory + "vk_imgui_vert.spv", shaderDirectory + "vk_imgui_frag.spv", m_VulkanDevice->m_LogicalDevice },
				{ "pbr", shaderDirectory + "vk_pbr_vert.spv", shaderDirectory + "vk_pbr_frag.spv", m_VulkanDevice->m_LogicalDevice },
				{ "skybox", shaderDirectory + "vk_skybox_vert.spv", shaderDirectory + "vk_skybox_frag.spv", m_VulkanDevice->m_LogicalDevice },
				{ "equirectangular_to_cube", shaderDirectory + "vk_skybox_vert.spv", shaderDirectory + "vk_equirectangular_to_cube_frag.spv", m_VulkanDevice->m_LogicalDevice },
				{ "irradiance", shaderDirectory + "vk_skybox_vert.spv", shaderDirectory + "vk_irradiance_frag.spv", m_VulkanDevice->m_LogicalDevice },
				{ "prefilter", shaderDirectory + "vk_skybox_vert.spv", shaderDirectory + "vk_prefilter_frag.spv", m_VulkanDevice->m_LogicalDevice },
				{ "brdf", shaderDirectory + "vk_brdf_vert.spv", shaderDirectory + "vk_brdf_frag.spv", m_VulkanDevice->m_LogicalDevice },
				{ "background", shaderDirectory + "vk_background_vert.spv", shaderDirectory + "vk_background_frag.spv", m_VulkanDevice->m_LogicalDevice },
				{ "deferred_combine", shaderDirectory + "vk_deferred_combine_vert.spv", shaderDirectory + "vk_deferred_combine_frag.spv", m_VulkanDevice->m_LogicalDevice },
			};

			// TODO: Specify vertex attributes expected here as well?

			// Specify shader uniforms
			ShaderID shaderID = 0;

			// TODO: Remove material?
			// Deferred Simple
			m_Shaders[shaderID].shader.numAttachments = 3;
			m_Shaders[shaderID].shader.deferred = true;
			m_Shaders[shaderID].shader.subpass = 0;
			m_Shaders[shaderID].shader.needDiffuseSampler = true;
			m_Shaders[shaderID].shader.needNormalSampler = true;
			m_Shaders[shaderID].shader.needSpecularSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("uniformBufferConstant");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("viewProjection");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("uniformBufferDynamic");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("modelInvTranspose");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableDiffuseSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("diffuseSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableNormalSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("normalSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableSpecularSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("specularSampler");
			++shaderID;

			// Color
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.subpass = 1;
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("uniformBufferConstant");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("viewProjection");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("uniformBufferDynamic");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			++shaderID;

			// ImGui
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.subpass = 1;
			m_Shaders[shaderID].shader.constantBufferUniforms = {};

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("diffuseSampler");
			++shaderID;

			// PBR
			m_Shaders[shaderID].shader.numAttachments = 3;
			m_Shaders[shaderID].shader.deferred = true;
			m_Shaders[shaderID].shader.subpass = 0;
			m_Shaders[shaderID].shader.needAlbedoSampler = true;
			m_Shaders[shaderID].shader.needMetallicSampler = true;
			m_Shaders[shaderID].shader.needRoughnessSampler = true;
			m_Shaders[shaderID].shader.needAOSampler = true;
			m_Shaders[shaderID].shader.needNormalSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("uniformBufferConstant");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("viewProjection");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("uniformBufferDynamic");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableAlbedoSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("albedoSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("constAlbedo");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableMetallicSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("metallicSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("constMetallic");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableRoughnessSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("roughnessSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("constRoughness");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableAOSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("aoSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("constAO");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableNormalSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("normalSampler");
			++shaderID;

			// Skybox
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.subpass = 1;
			m_Shaders[shaderID].shader.needCubemapSampler = true;
			m_Shaders[shaderID].shader.needPushConstantBlock = true;

			m_Shaders[shaderID].shader.constantBufferUniforms = {};

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("cubemapSampler");
			++shaderID;

			// Equirectangular to cube
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.subpass = 1;
			m_Shaders[shaderID].shader.needHDREquirectangularSampler = true;
			m_Shaders[shaderID].shader.needPushConstantBlock = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("hdrEquirectangularSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			++shaderID;

			// Irradiance
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.subpass = 1;
			m_Shaders[shaderID].shader.needCubemapSampler = true;
			m_Shaders[shaderID].shader.needPushConstantBlock = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("cubemapSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			++shaderID;

			// Prefilter
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.subpass = 1;
			m_Shaders[shaderID].shader.needCubemapSampler = true;
			m_Shaders[shaderID].shader.needPushConstantBlock = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("cubemapSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("uniformBufferDynamic");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("roughness");
			++shaderID;

			// BRDF
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.subpass = 1;

			m_Shaders[shaderID].shader.constantBufferUniforms = {};

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			++shaderID;

			// Background
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.subpass = 1;
			m_Shaders[shaderID].shader.needCubemapSampler = true;
			m_Shaders[shaderID].shader.needPushConstantBlock = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("cubemapSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			++shaderID;

			// Deferred combine (sample gbuffer)
			m_Shaders[shaderID].shader.deferred = false; // Sounds strange but this isn't deferred
			m_Shaders[shaderID].shader.subpass = 0;
			m_Shaders[shaderID].shader.depthWriteEnable = false; // Disable depth writing
			m_Shaders[shaderID].shader.needBRDFLUT = true;
			m_Shaders[shaderID].shader.needIrradianceSampler = true;
			m_Shaders[shaderID].shader.needPrefilteredMap = true;
			// TODO: Specify that this buffer is only used in the frag shader here

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("uniformBufferConstant");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("camPos");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("dirLight");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("pointLights");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("brdfLUT");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("irradianceSampler");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("prefilterMap");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("positionMetallicFrameBufferSampler");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("normalRoughnessFrameBufferSampler");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("albedoAOFrameBufferSampler");
			
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("uniformBufferDynamic");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableIrradianceSampler");
			++shaderID;
			

			const size_t shaderCount = m_Shaders.size();
			for (size_t i = 0; i < shaderCount; ++i)
			{
				std::string vertFileName = m_Shaders[i].shader.vertexShaderFilePath;
				StripLeadingDirectories(vertFileName);
				std::string fragFileName = m_Shaders[i].shader.fragmentShaderFilePath;
				StripLeadingDirectories(fragFileName);
				Logger::LogInfo("Loading shaders " + vertFileName + " & " + fragFileName);

				if (!ReadFile(m_Shaders[i].shader.vertexShaderFilePath, m_Shaders[i].shader.vertexShaderCode))
				{
					Logger::LogError("Could not find vertex shader " + m_Shaders[i].shader.name);
				}
				if (!ReadFile(m_Shaders[i].shader.fragmentShaderFilePath, m_Shaders[i].shader.fragmentShaderCode))
				{
					Logger::LogError("Could not find fragment shader " + m_Shaders[i].shader.name);
				}
			}

			if (!GetShaderID("imgui", m_IGuiShaderID))
			{
				Logger::LogError("Failed to find imgui shader!");
			}
		}

		VKAPI_ATTR VkBool32 VKAPI_CALL VulkanRenderer::DebugCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType,
			uint64_t obj, size_t location, int32_t code, const char* layerPrefix, const char* msg, void* userData)
		{
			UNREFERENCED_PARAMETER(userData);
			UNREFERENCED_PARAMETER(layerPrefix);
			UNREFERENCED_PARAMETER(code);
			UNREFERENCED_PARAMETER(location);
			UNREFERENCED_PARAMETER(obj);
			UNREFERENCED_PARAMETER(objType);
			UNREFERENCED_PARAMETER(flags);

			std::cerr << "[ERROR] (VL): " << msg << std::endl;

			return VK_FALSE;
		}


		void SetClipboardText(void* userData, const char* text)
		{
			GLFWWindowWrapper* glfwWindow = static_cast<GLFWWindowWrapper*>(userData);
			glfwWindow->SetClipboardText(text);
		}

		const char* GetClipboardText(void* userData)
		{
			GLFWWindowWrapper* glfwWindow = static_cast<GLFWWindowWrapper*>(userData);
			return glfwWindow->GetClipboardText();
		}

	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN