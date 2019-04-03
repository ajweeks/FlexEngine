#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanRenderer.hpp"

IGNORE_WARNINGS_PUSH
#include "stb_image.h"

#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>

#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <freetype/ftbitmap.h>

#if COMPILE_IMGUI
#include "imgui_internal.h" // For columns API

#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_vulkan.h"
#endif

#include "BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h"
IGNORE_WARNINGS_POP

#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "FlexEngine.hpp"
#include "Graphics/BitmapFont.hpp"
#include "Graphics/VertexAttribute.hpp"
#include "Graphics/VertexBufferData.hpp"
#include "Graphics/Vulkan/VulkanBuffer.hpp"
#include "Graphics/Vulkan/VulkanDevice.hpp"
#include "Graphics/Vulkan/VulkanPhysicsDebugDraw.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Profiler.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/LoadedMesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/GLFWWindowWrapper.hpp"

namespace flex
{
	namespace vk
	{
		std::array<glm::mat4, 6> VulkanRenderer::s_CaptureViews;

		VulkanRenderer::VulkanRenderer()
		{
		}

		VulkanRenderer::~VulkanRenderer()
		{
		}

		void VulkanRenderer::Initialize()
		{
			Renderer::Initialize();

#ifdef DEBUG
			m_ShaderCompiler = new AsyncVulkanShaderCompiler(false);
#endif

			m_RenderObjects.resize(MAX_NUM_RENDER_OBJECTS);

			m_ClearColor = { 1.0f, 0.0f, 1.0f, 1.0f };
			m_BRDFSize = { 512, 512 };
			m_CubemapFramebufferSize = { 512, 512 };

			CreateInstance();
			SetupDebugCallback();
			CreateSurface();
			VkPhysicalDevice physicalDevice = PickPhysicalDevice();
			CreateLogicalDevice(physicalDevice);

			{
				VkPhysicalDeviceProperties& props = m_VulkanDevice->m_PhysicalDeviceProperties;
				u32 vkVersionMaj = VK_VERSION_MAJOR(props.apiVersion);
				u32 vkVersionMin = VK_VERSION_MINOR(props.apiVersion);
				u32 vkVersionPatch = VK_VERSION_PATCH(props.apiVersion);
				Print("Vulkan loaded - v%u.%u.%u\n", vkVersionMaj, vkVersionMin, vkVersionPatch);
				Print("Vendor ID, Device ID: 0x%u, 0x%u\n", props.vendorID, props.deviceID);
				Print("Device name: %s\n", (const char*)props.deviceName);
				Print("Device type: %s\n", DeviceTypeToString(props.deviceType).c_str());
				Print("Device driver version: %u\n", props.driverVersion);
			}

			m_CommandBufferManager = VulkanCommandBufferManager(m_VulkanDevice);

			m_DepthAttachment = new FrameBufferAttachment(m_VulkanDevice->m_LogicalDevice);

			VkFormat depthFormat;
			GetSupportedDepthFormat(m_VulkanDevice->m_PhysicalDevice, &depthFormat);
			m_CubemapDepthAttachment = new FrameBufferAttachment(m_VulkanDevice->m_LogicalDevice, depthFormat);

			m_SwapChain = { m_VulkanDevice->m_LogicalDevice, vkDestroySwapchainKHR };
			m_DeferredCombineRenderPass = { m_VulkanDevice->m_LogicalDevice, vkDestroyRenderPass };
			m_DescriptorPool = { m_VulkanDevice->m_LogicalDevice, vkDestroyDescriptorPool };
			m_PresentCompleteSemaphore = { m_VulkanDevice->m_LogicalDevice, vkDestroySemaphore };
			m_RenderCompleteSemaphore = { m_VulkanDevice->m_LogicalDevice, vkDestroySemaphore };
			m_ColorSampler = { m_VulkanDevice->m_LogicalDevice, vkDestroySampler };
			m_PipelineCache = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipelineCache };

			m_OffScreenFrameBuf = new FrameBuffer(m_VulkanDevice->m_LogicalDevice);
			m_OffScreenFrameBuf->frameBufferAttachments = {
				{ "positionMetallicFrameBufferSampler",{ m_VulkanDevice->m_LogicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT } },
				{ "normalRoughnessFrameBufferSampler",{ m_VulkanDevice->m_LogicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT } },
				{ "albedoAOFrameBufferSampler",{ m_VulkanDevice->m_LogicalDevice, VK_FORMAT_R8G8B8A8_UNORM } },
			};

			m_CubemapFrameBuffer = new FrameBuffer(m_VulkanDevice->m_LogicalDevice);
			m_CubemapFrameBuffer->frameBufferAttachments = {
				{ "positionMetallicFrameBufferSampler",{ m_VulkanDevice->m_LogicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT } },
				{ "normalRoughnessFrameBufferSampler",{ m_VulkanDevice->m_LogicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT } },
				{ "albedoAOFrameBufferSampler",{ m_VulkanDevice->m_LogicalDevice, VK_FORMAT_R8G8B8A8_UNORM } },
			};

			// NOTE: This is different from the GLRenderer's capture views
			s_CaptureViews = {
				glm::lookAt(VEC3_ZERO, glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(VEC3_ZERO, glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(VEC3_ZERO, glm::vec3(0.0f,  -1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  -1.0f)),
				glm::lookAt(VEC3_ZERO, glm::vec3(0.0f, 1.0f,  0.0f), glm::vec3(0.0f,  0.0f, 1.0f)),
				glm::lookAt(VEC3_ZERO, glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(VEC3_ZERO, glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
			};

			// TODO: Make variable
			m_CubemapFrameBuffer->width = 512;
			m_CubemapFrameBuffer->height = 512;

			CreateSwapChain();
			CreateSwapChainImageViews();
			CreateRenderPass();

			m_CommandBufferManager.CreatePool(m_Surface);
			CreateDepthResources();
			CreateFramebuffers();

			PrepareOffscreenFrameBuffer();
			PrepareCubemapFrameBuffer();

			m_BlankTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, RESOURCE_LOCATION  "textures/blank.jpg", 1, false, false, false);
			m_BlankTexture->CreateFromFile(VK_FORMAT_R8G8B8A8_UNORM);

			m_AlphaBGTextureID = InitializeTexture(RESOURCE_LOCATION  "textures/alpha-bg.png", 4, false, false, false);
			m_LoadingTextureID = InitializeTexture(RESOURCE_LOCATION  "textures/loading_1.png", 4, false, false, false);
			m_WorkTextureID = InitializeTexture(RESOURCE_LOCATION  "textures/work_d.jpg", 4, false, true, false);
			m_PointLightIconID = InitializeTexture(RESOURCE_LOCATION  "textures/icons/point-light-icon-256.png", 4, false, true, false);
			m_DirectionalLightIconID = InitializeTexture(RESOURCE_LOCATION  "textures/icons/directional-light-icon-256.png", 4, false, true, false);

#ifdef DEBUG
			while (!m_ShaderCompiler->TickStatus())
			{
				// Spin lock
			}

			if (m_ShaderCompiler->bSuccess == false)
			{
				PrintError("Failed to compile shader code!\n");
			}
#endif
			LoadDefaultShaderCode();

			const u32 shaderCount = m_Shaders.size();
			m_VertexIndexBufferPairs.reserve(shaderCount);
			for (size_t i = 0; i < shaderCount; ++i)
			{
				m_VertexIndexBufferPairs.push_back({
					new VulkanBuffer(m_VulkanDevice->m_LogicalDevice), // Vertex buffer
					new VulkanBuffer(m_VulkanDevice->m_LogicalDevice)  // Index buffer
					});
			}
		}

		void VulkanRenderer::PostInitialize()
		{
			CreateDescriptorPool();

			GenerateGBuffer();

			assert(m_PhysicsDebugDrawer == nullptr);
			m_PhysicsDebugDrawer = new VulkanPhysicsDebugDraw();
			m_PhysicsDebugDrawer->Initialize();

			btDiscreteDynamicsWorld* world = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
			world->setDebugDrawer(m_PhysicsDebugDrawer);

			// Figure out largest shader uniform buffer to set m_DynamicAlignment correctly
			{
				size_t uboAlignment = (size_t)m_VulkanDevice->m_PhysicalDeviceProperties.limits.minUniformBufferOffsetAlignment;
				for (const VulkanShader& shader : m_Shaders)
				{
					u32 size = GetAlignedUBOSize(shader.shader.dynamicBufferUniforms.CalculateSizeInBytes());
					u32 dynamicDataSize = size;
					u32 dynamicAllignment =
						(dynamicDataSize / uboAlignment) * uboAlignment +
						((dynamicDataSize % uboAlignment) > 0 ? uboAlignment : 0);

					if (dynamicAllignment > m_DynamicAlignment)
					{
						u32 newDynamicAllignment = 1;
						while (newDynamicAllignment < dynamicAllignment)
						{
							newDynamicAllignment <<= 1;
						}
						m_DynamicAlignment = newDynamicAllignment;
					}
				}
			}

			for (size_t i = 0; i < m_Shaders.size(); ++i)
			{
				CreateDescriptorSetLayout(i);
				CreateUniformBuffers(&m_Shaders[i]);
			}

			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				CreateDescriptorSet(i);
				CreateGraphicsPipeline(i, true);
			}

			m_CommandBufferManager.CreateCommandBuffers(m_SwapChainImages.size());

			GLFWWindowWrapper* castedWindow = dynamic_cast<GLFWWindowWrapper*>(g_Window);
			if (castedWindow == nullptr)
			{
				PrintError("VulkanRenderer::PostInitialize expects g_Window to be of type GLFWWindowWrapper!\n");
				return;
			}


			{
				// ImGui
				ImGui_ImplGlfw_InitForVulkan(((GLFWWindowWrapper*)g_Window)->GetWindow(), false);

				ImGui_ImplVulkan_InitInfo init_info = {};
				init_info.Instance = m_Instance;
				init_info.PhysicalDevice = m_VulkanDevice->m_PhysicalDevice;
				init_info.Device = *m_VulkanDevice;
				init_info.QueueFamily = FindQueueFamilies(m_Surface, m_VulkanDevice->m_PhysicalDevice).graphicsFamily;
				init_info.Queue = m_GraphicsQueue;
				init_info.PipelineCache = m_PipelineCache;
				init_info.DescriptorPool = m_DescriptorPool;
				init_info.Allocator = NULL;
				init_info.CheckVkResultFn = NULL;
				ImGui_ImplVulkan_Init(&init_info, m_DeferredCombineRenderPass);

				{
					// TODO: Use general purpose command buffer manager
					VkCommandBuffer command_buffer = m_CommandBufferManager.m_CommandBuffers[0];

					VkCommandBufferBeginInfo begin_info = {};
					begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
					begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
					VK_CHECK_RESULT(vkBeginCommandBuffer(command_buffer, &begin_info));

					ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

					VkSubmitInfo end_info = {};
					end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
					end_info.commandBufferCount = 1;
					end_info.pCommandBuffers = &command_buffer;
					VK_CHECK_RESULT(vkEndCommandBuffer(command_buffer));
					VK_CHECK_RESULT(vkQueueSubmit(m_GraphicsQueue, 1, &end_info, VK_NULL_HANDLE));

					VK_CHECK_RESULT(vkDeviceWaitIdle(*m_VulkanDevice));
					ImGui_ImplVulkan_InvalidateFontUploadObjects();
				}
			}

			CreateStaticVertexBuffers();
			CreateStaticIndexBuffers();

			CreateSemaphores();

			LoadFonts(false);

			GenerateIrradianceMaps();

			m_bPostInitialized = true;
		}

		void VulkanRenderer::Destroy()
		{
			Renderer::Destroy();

#ifdef DEBUG
			delete m_ShaderCompiler;
#endif

			// TODO: Is this needed?
			vkDeviceWaitIdle(m_VulkanDevice->m_LogicalDevice);

			ImGui_ImplVulkan_Shutdown();
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();

			for (const VkDescriptorSetLayout& descriptorSetLayout : m_DescriptorSetLayouts)
			{
				vkDestroyDescriptorSetLayout(m_VulkanDevice->m_LogicalDevice, descriptorSetLayout, nullptr);
			}

			m_PresentCompleteSemaphore.replace();
			m_RenderCompleteSemaphore.replace();

			for (VertexIndexBufferPair& pair : m_VertexIndexBufferPairs)
			{
				pair.Destroy();
			}
			m_VertexIndexBufferPairs.clear();

			m_SkyBoxMesh = nullptr;

			DestroyRenderObject(m_GBufferQuadRenderID);

			delete m_PhysicsDebugDrawer;

			for (GameObject* obj : m_PersistentObjects)
			{
				if (obj->GetRenderID() != InvalidRenderID)
				{
					DestroyRenderObject(obj->GetRenderID());
				}
				delete obj;
			}
			m_PersistentObjects.clear();

			u32 activeRenderObjectCount = GetActiveRenderObjectCount();
			if (activeRenderObjectCount > 0)
			{
				PrintError("=====================================================\n");
				PrintError("%u render objects were not destroyed before Vulkan render:\n", activeRenderObjectCount);

				for (VulkanRenderObject* renderObject : m_RenderObjects)
				{
					if (renderObject)
					{
						PrintError("render object with material name: %s\n", renderObject->materialName.c_str());
						DestroyRenderObject(renderObject->renderID);
					}
				}
				PrintError("=====================================================\n");
			}
			m_RenderObjects.clear();

			m_Shaders.clear();

			ClearMaterials(true);

			delete m_OffScreenFrameBuf;
			vkDestroySemaphore(m_VulkanDevice->m_LogicalDevice, offscreenSemaphore, nullptr);

			delete m_CubemapFrameBuffer;
			delete m_CubemapDepthAttachment;

			delete m_DepthAttachment;

			m_gBufferQuadVertexBufferData.Destroy();
			m_PipelineCache.replace();
			m_DescriptorPool.replace();
			m_ColorSampler.replace();

			delete m_BlankTexture;

			for (VulkanTexture* loadedTexture : m_LoadedTextures)
			{
				delete loadedTexture;
			}
			m_LoadedTextures.clear();

			for (GameObject* editorObject : m_EditorObjects)
			{
				editorObject->Destroy();
				delete editorObject;
			}
			m_EditorObjects.clear();

			for (BitmapFont* font : m_FontsSS)
			{
				delete font;
			}
			m_FontsSS.clear();

			for (BitmapFont* font : m_FontsWS)
			{
				delete font;
			}
			m_FontsWS.clear();

			m_DeferredCombineRenderPass.replace();
			m_SwapChain.replace();
			m_SwapChainImageViews.clear();
			m_SwapChainFramebuffers.clear();

			m_CommandBufferManager.DestroyCommandBuffers();

			vkDeviceWaitIdle(m_VulkanDevice->m_LogicalDevice);

			delete m_VulkanDevice;

			glfwTerminate();
		}

		void VulkanRenderer::GenerateCubemapFromHDR(VulkanRenderObject* renderObject, const std::string& environmentMapPath)
		{
			if (!m_SkyBoxMesh)
			{
				PrintError("Attempted to generate cubemap before skybox object was created!\n");
				return;
			}

			VulkanRenderObject* skyboxRenderObject = GetRenderObject(m_SkyBoxMesh->GetRenderID());
			Material& skyboxMat = m_Materials[renderObject->materialID].material;
			VulkanMaterial& renderObjectMat = m_Materials[renderObject->materialID];

			MaterialCreateInfo equirectangularToCubeMatCreateInfo = {};
			equirectangularToCubeMatCreateInfo.name = "Equirectangular to Cube";
			equirectangularToCubeMatCreateInfo.shaderName = "equirectangular_to_cube";
			equirectangularToCubeMatCreateInfo.enableHDREquirectangularSampler = true;
			equirectangularToCubeMatCreateInfo.generateHDREquirectangularSampler = true;
			equirectangularToCubeMatCreateInfo.hdrEquirectangularTexturePath = environmentMapPath;

			bool bRandomizeSkybox = true;
			if (bRandomizeSkybox && !m_AvailableHDRIs.empty())
			{
				equirectangularToCubeMatCreateInfo.hdrEquirectangularTexturePath = PickRandomSkyboxTexture();
			}

			equirectangularToCubeMatCreateInfo.engineMaterial = true;
			MaterialID equirectangularToCubeMatID = InitializeMaterial(&equirectangularToCubeMatCreateInfo);

			const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
			const u32 dim = (u32)renderObjectMat.material.cubemapSamplerSize.x;
			assert(dim <= MAX_TEXTURE_DIM);

			const u32 mipLevels = static_cast<u32>(floor(log2(dim))) + 1;

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



			// Offscreen framebuffer
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
			VkMemoryRequirements offscreenMemRequirements;
			vkGetImageMemoryRequirements(m_VulkanDevice->m_LogicalDevice, offscreen.image, &offscreenMemRequirements);
			offscreenMemAlloc.allocationSize = offscreenMemRequirements.size;
			offscreenMemAlloc.memoryTypeIndex = FindMemoryType(m_VulkanDevice, offscreenMemRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
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

			VkCommandBuffer layoutCmd = m_CommandBufferManager.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			SetImageLayout(
				layoutCmd,
				offscreen.image,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
			m_CommandBufferManager.FlushCommandBuffer(layoutCmd, m_GraphicsQueue, true);


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
			descriptorSetLayoutCreateInfo.bindingCount = static_cast<u32>(setLayoutBindings.size());
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_VulkanDevice->m_LogicalDevice, &descriptorSetLayoutCreateInfo, nullptr, &descriptorsetlayout));

			ShaderID equirectangularToCubeShaderID;
			if (!GetShaderID(equirectangularToCubeMatCreateInfo.shaderName, equirectangularToCubeShaderID))
			{
				PrintError("Failed to find equirectangular_to_cube shader ID!\n");
				return;
			}
			VulkanShader& equirectangularToCubeShader = m_Shaders[equirectangularToCubeShaderID];

			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
			DescriptorSetCreateInfo equirectangularToCubeDescriptorCreateInfo = {};
			equirectangularToCubeDescriptorCreateInfo.descriptorSet = &descriptorSet;
			equirectangularToCubeDescriptorCreateInfo.descriptorSetLayout = &m_DescriptorSetLayouts[equirectangularToCubeShaderID];
			equirectangularToCubeDescriptorCreateInfo.shaderID = equirectangularToCubeShaderID;
			equirectangularToCubeDescriptorCreateInfo.uniformBuffer = &equirectangularToCubeShader.uniformBuffer;
			equirectangularToCubeDescriptorCreateInfo.hdrEquirectangularTexture = m_Materials[equirectangularToCubeMatID].hdrEquirectangularTexture;
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
			pipelineLayoutCreateInfo.pushConstantRangeCount = pipelineLayoutCreateInfo.setLayoutCount;
			pipelineLayoutCreateInfo.pPushConstantRanges = pushConstantRanges.data();
			VK_CHECK_RESULT(vkCreatePipelineLayout(m_VulkanDevice->m_LogicalDevice, &pipelineLayoutCreateInfo, nullptr, &pipelinelayout));

			// Pipeline
			VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
			inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssemblyState.topology = skyboxRenderObject->topology;
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
			dynamicState.dynamicStateCount = static_cast<u32>(dynamicStateEnables.size());
			dynamicState.pDynamicStates = dynamicStateEnables.data();

			// Vertex input state
			VkVertexInputBindingDescription vertexInputBinding = {};
			vertexInputBinding.binding = 0;
			vertexInputBinding.stride = skyboxRenderObject->vertexBufferData->VertexStride;
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
			if (!CreateShaderModule(equirectangularToCubeShader.shader.vertexShaderCode, vertShaderModule))
			{
				PrintError("Failed to compile vertex shader located at: %s\n", equirectangularToCubeShader.shader.vertexShaderFilePath.c_str());
			}

			VDeleter<VkShaderModule> fragShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(equirectangularToCubeShader.shader.fragmentShaderCode, fragShaderModule))
			{
				PrintError("Failed to compile fragment shader located at: %s\n", equirectangularToCubeShader.shader.fragmentShaderFilePath.c_str());
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

			VkPipeline pipeline = VK_NULL_HANDLE;
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

			VkCommandBuffer cmdBuf = m_CommandBufferManager.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			VkViewport viewport = {};
			viewport.width = (real)dim;
			viewport.height = (real)dim;
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
				renderObjectMat.cubemapTexture,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);

			VertexIndexBufferPair& vertexIndexBufferPair = m_VertexIndexBufferPairs[skyboxMat.shaderID];

			if (vertexIndexBufferPair.vertexBuffer->m_Buffer == VK_NULL_HANDLE)
			{
				PrintError("Attempted to generate cubemap from HDR but vertex buffer has not been generated! (for shader %s)\n", skyboxMat.name.c_str());
			}
			if (skyboxRenderObject->bIndexed &&
				vertexIndexBufferPair.indexBuffer->m_Buffer == VK_NULL_HANDLE)
			{
				PrintError("Attempted to generate cubemap from HDR but index buffer has not been generated! (for shader %s)\n", skyboxMat.name.c_str());
			}

			for (u32 mip = 0; mip < mipLevels; ++mip)
			{
				for (u32 face = 0; face < 6; ++face)
				{
					viewport.width = static_cast<real>(dim * std::pow(0.5f, mip));
					viewport.height = viewport.width;
					vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

					vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

					// Push constants
					skyboxMat.pushConstantBlock.mvp =
						glm::perspective(PI_DIV_TWO, 1.0f, 0.1f, (real)dim) * s_CaptureViews[face];
					vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT,
						0, sizeof(Material::PushConstantBlock), &skyboxMat.pushConstantBlock);

					vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

					BindDescriptorSet(&m_Shaders[skyboxMat.shaderID], 0, cmdBuf, pipelinelayout, descriptorSet);

					VkDeviceSize offsets[1] = { 0 };

					vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vertexIndexBufferPair.vertexBuffer->m_Buffer, offsets);
					if (skyboxRenderObject->bIndexed)
					{
						vkCmdBindIndexBuffer(cmdBuf, vertexIndexBufferPair.indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
						vkCmdDrawIndexed(cmdBuf, skyboxRenderObject->indices->size(), 1, 0, 0, 0);
					}
					else
					{
						vkCmdDraw(cmdBuf, skyboxRenderObject->vertexBufferData->VertexCount, 1, 0, 0);
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

					copyRegion.extent.width = static_cast<u32>(viewport.width);
					copyRegion.extent.height = static_cast<u32>(viewport.height);
					copyRegion.extent.depth = 1;

					vkCmdCopyImage(
						cmdBuf,
						offscreen.image,
						VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						renderObjectMat.cubemapTexture->image,
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
				renderObjectMat.cubemapTexture,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				subresourceRange);

			m_CommandBufferManager.FlushCommandBuffer(cmdBuf, m_GraphicsQueue, true);


			vkDestroyRenderPass(m_VulkanDevice->m_LogicalDevice, renderpass, nullptr);
			vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, offscreen.framebuffer, nullptr);
			vkFreeMemory(m_VulkanDevice->m_LogicalDevice, offscreen.memory, nullptr);
			vkDestroyImageView(m_VulkanDevice->m_LogicalDevice, offscreen.view, nullptr);
			vkDestroyImage(m_VulkanDevice->m_LogicalDevice, offscreen.image, nullptr);
			vkDestroyDescriptorSetLayout(m_VulkanDevice->m_LogicalDevice, descriptorsetlayout, nullptr);
			vkDestroyPipeline(m_VulkanDevice->m_LogicalDevice, pipeline, nullptr);
			vkDestroyPipelineLayout(m_VulkanDevice->m_LogicalDevice, pipelinelayout, nullptr);
		}

		void VulkanRenderer::GenerateIrradianceSampler(VulkanRenderObject* renderObject)
		{
			if (!m_SkyBoxMesh)
			{
				PrintError("Attempted to generate cubemap before skybox object was created!\n");
				return;
			}

			VulkanRenderObject* skyboxRenderObject = GetRenderObject(m_SkyBoxMesh->GetRenderID());
			Material& skyboxMat = m_Materials[skyboxRenderObject->materialID].material;
			VulkanMaterial& renderObjectMat = m_Materials[renderObject->materialID];

			const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
			const u32 dim = (u32)renderObjectMat.material.irradianceSamplerSize.x;
			assert(dim <= MAX_TEXTURE_DIM);
			const u32 mipLevels = static_cast<u32>(floor(log2(dim))) + 1;

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


			// Offscreen framebuffer
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
			VkMemoryRequirements offscreenMemRequirements;
			vkGetImageMemoryRequirements(m_VulkanDevice->m_LogicalDevice, offscreen.image, &offscreenMemRequirements);
			offscreenMemAlloc.allocationSize = offscreenMemRequirements.size;
			offscreenMemAlloc.memoryTypeIndex = FindMemoryType(m_VulkanDevice, offscreenMemRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
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

			VkCommandBuffer layoutCmd = m_CommandBufferManager.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			SetImageLayout(
				layoutCmd,
				offscreen.image,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
			m_CommandBufferManager.FlushCommandBuffer(layoutCmd, m_GraphicsQueue, true);

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
			descriptorSetLayoutCreateInfo.bindingCount = static_cast<u32>(setLayoutBindings.size());
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_VulkanDevice->m_LogicalDevice, &descriptorSetLayoutCreateInfo, nullptr, &descriptorsetlayout));

			// Descriptor Pool
			std::array<VkDescriptorPoolSize, 1> poolSizes = {};
			poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSizes[0].descriptorCount = 1;

			VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
			descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			descriptorPoolCreateInfo.pPoolSizes = poolSizes.data();
			descriptorPoolCreateInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
			descriptorPoolCreateInfo.maxSets = 2;
			VkDescriptorPool descriptorPool;
			VK_CHECK_RESULT(vkCreateDescriptorPool(m_VulkanDevice->m_LogicalDevice, &descriptorPoolCreateInfo, nullptr, &descriptorPool));

			// Descriptor sets
			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
			VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {};
			descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorSetAllocateInfo.descriptorPool = descriptorPool;
			descriptorSetAllocateInfo.pSetLayouts = &descriptorsetlayout;
			descriptorSetAllocateInfo.descriptorSetCount = 1;
			VK_CHECK_RESULT(vkAllocateDescriptorSets(m_VulkanDevice->m_LogicalDevice, &descriptorSetAllocateInfo, &descriptorSet));

			renderObjectMat.cubemapTexture->UpdateImageDescriptor();

			VkDescriptorImageInfo descriptorImageInfo = {};
			descriptorImageInfo.imageView = offscreen.view;
			descriptorImageInfo.imageLayout = renderObjectMat.cubemapTexture->imageLayout;
			VkWriteDescriptorSet writeDescriptorSet = {};
			writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSet.dstSet = descriptorSet;
			writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSet.dstBinding = 0;
			writeDescriptorSet.pImageInfo = &renderObjectMat.cubemapTexture->imageInfoDescriptor;
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
			pipelineLayoutCreateInfo.pushConstantRangeCount = pipelineLayoutCreateInfo.setLayoutCount;
			pipelineLayoutCreateInfo.pPushConstantRanges = pushConstantRanges.data();
			VK_CHECK_RESULT(vkCreatePipelineLayout(m_VulkanDevice->m_LogicalDevice, &pipelineLayoutCreateInfo, nullptr, &pipelinelayout));

			// Pipeline
			VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
			inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssemblyState.topology = skyboxRenderObject->topology;
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
			dynamicState.dynamicStateCount = static_cast<u32>(dynamicStateEnables.size());
			dynamicState.pDynamicStates = dynamicStateEnables.data();

			// Vertex input state
			VkVertexInputBindingDescription vertexInputBinding = {};
			vertexInputBinding.binding = 0;
			vertexInputBinding.stride = skyboxRenderObject->vertexBufferData->VertexStride;
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

			ShaderID irradianceShaderID;
			if (!GetShaderID("irradiance", irradianceShaderID))
			{
				PrintError("Failed to find irradiance shader!\n");
				return;
			}
			VulkanShader& irradianceShader = m_Shaders[irradianceShaderID];

			VDeleter<VkShaderModule> vertShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(irradianceShader.shader.vertexShaderCode, vertShaderModule))
			{
				PrintError("Failed to compile vertex shader located at: %s\n", irradianceShader.shader.vertexShaderFilePath.c_str());
			}

			VDeleter<VkShaderModule> fragShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(irradianceShader.shader.fragmentShaderCode, fragShaderModule))
			{
				PrintError("Failed to compile fragment shader located at: %s\n", irradianceShader.shader.fragmentShaderFilePath.c_str());
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

			VkPipeline pipeline = VK_NULL_HANDLE;
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

			VkCommandBuffer cmdBuf = m_CommandBufferManager.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			VkViewport viewport = {};
			viewport.width = (real)dim;
			viewport.height = (real)dim;
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
				renderObjectMat.irradianceTexture,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);
			renderObjectMat.irradianceTexture->imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

			for (u32 mip = 0; mip < mipLevels; ++mip)
			{
				for (u32 face = 0; face < 6; ++face)
				{
					viewport.width = static_cast<real>(dim * std::pow(0.5f, mip));
					viewport.height = viewport.width;
					vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

					vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

					// Push constants
					skyboxMat.pushConstantBlock.mvp =
						glm::perspective(PI_DIV_TWO, 1.0f, 0.1f, (real)dim) * s_CaptureViews[face];
					vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Material::PushConstantBlock),
						&skyboxMat.pushConstantBlock);

					vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

					BindDescriptorSet(&m_Shaders[skyboxMat.shaderID], 0, cmdBuf, pipelinelayout, descriptorSet);

					VkDeviceSize offsets[1] = { 0 };

					const ShaderID shaderID = skyboxMat.shaderID;
					vkCmdBindVertexBuffers(cmdBuf, 0, 1, &m_VertexIndexBufferPairs[shaderID].vertexBuffer->m_Buffer, offsets);
					if (skyboxRenderObject->bIndexed)
					{
						vkCmdBindIndexBuffer(cmdBuf, m_VertexIndexBufferPairs[shaderID].indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
						vkCmdDrawIndexed(cmdBuf, skyboxRenderObject->indices->size(), 1, 0, 0, 0);
					}
					else
					{
						vkCmdDraw(cmdBuf, skyboxRenderObject->vertexBufferData->VertexCount, 1, 0, 0);
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

					copyRegion.extent.width = static_cast<u32>(viewport.width);
					copyRegion.extent.height = static_cast<u32>(viewport.height);
					copyRegion.extent.depth = 1;

					vkCmdCopyImage(
						cmdBuf,
						offscreen.image,
						VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						renderObjectMat.irradianceTexture->image,
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
				renderObjectMat.irradianceTexture,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				subresourceRange);

			m_CommandBufferManager.FlushCommandBuffer(cmdBuf, m_GraphicsQueue, true);


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

		void VulkanRenderer::GeneratePrefilteredCube(VulkanRenderObject* renderObject)
		{
			if (!m_SkyBoxMesh)
			{
				PrintError("Attempted to generate cubemap before skybox object was created!\n");
				return;
			}

			VulkanRenderObject* skyboxRenderObject = GetRenderObject(m_SkyBoxMesh->GetRenderID());
			Material& skyboxMat = m_Materials[skyboxRenderObject->materialID].material;
			VulkanMaterial& renderObjectMat = m_Materials[renderObject->materialID];

			const VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
			const u32 dim = (u32)renderObjectMat.material.prefilteredMapSize.x;
			assert(dim <= MAX_TEXTURE_DIM);
			const u32 mipLevels = static_cast<u32>(floor(log2(dim))) + 1;

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

			// Offscreen framebuffer
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
				VkMemoryRequirements memRequirements;
				vkGetImageMemoryRequirements(m_VulkanDevice->m_LogicalDevice, offscreen.image, &memRequirements);
				memAlloc.allocationSize = memRequirements.size;
				memAlloc.memoryTypeIndex = FindMemoryType(m_VulkanDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
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

				VkCommandBuffer layoutCmd = m_CommandBufferManager.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
				SetImageLayout(
					layoutCmd,
					offscreen.image,
					VK_IMAGE_ASPECT_COLOR_BIT,
					VK_IMAGE_LAYOUT_UNDEFINED,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
				m_CommandBufferManager.FlushCommandBuffer(layoutCmd, m_GraphicsQueue, true);
			}

			ShaderID prefilterShaderID;
			if (!GetShaderID("prefilter", prefilterShaderID))
			{
				PrintError("Failed to find prefilter shader!\n");
			}
			VulkanShader& prefilterShader = m_Shaders[prefilterShaderID];

			VDeleter<VkShaderModule> vertShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(prefilterShader.shader.vertexShaderCode, vertShaderModule))
			{
				PrintError("Failed to compile vertex shader located at: %s\n", prefilterShader.shader.vertexShaderFilePath.c_str());
			}

			VDeleter<VkShaderModule> fragShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(prefilterShader.shader.fragmentShaderCode, fragShaderModule))
			{
				PrintError("Failed to compile fragment shader located at: %s\n", prefilterShader.shader.fragmentShaderFilePath.c_str());
			}

			CreateUniformBuffers(&prefilterShader);

			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
			DescriptorSetCreateInfo equirectangularToCubeDescriptorCreateInfo = {};
			equirectangularToCubeDescriptorCreateInfo.descriptorSet = &descriptorSet;
			equirectangularToCubeDescriptorCreateInfo.descriptorSetLayout = &m_DescriptorSetLayouts[prefilterShaderID];
			equirectangularToCubeDescriptorCreateInfo.shaderID = prefilterShaderID;
			equirectangularToCubeDescriptorCreateInfo.uniformBuffer = &prefilterShader.uniformBuffer;
			equirectangularToCubeDescriptorCreateInfo.cubemapTexture = renderObjectMat.cubemapTexture;
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
			inputAssemblyState.topology = skyboxRenderObject->topology;
			inputAssemblyState.primitiveRestartEnable = VK_FALSE;
			inputAssemblyState.flags = 0;

			VkPipelineRasterizationStateCreateInfo rasterizationState = {};
			rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizationState.cullMode = skyboxRenderObject->cullMode;
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
			dynamicState.dynamicStateCount = static_cast<u32>(dynamicStateEnables.size());
			dynamicState.flags = 0;

			// Vertex input state
			VkVertexInputBindingDescription vertexInputBinding = {};
			vertexInputBinding.binding = 0;
			vertexInputBinding.stride = skyboxRenderObject->vertexBufferData->VertexStride;
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

			VkPipeline pipeline = VK_NULL_HANDLE;
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

			VkCommandBuffer cmdBuf = m_CommandBufferManager.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			VkViewport viewport = {};
			viewport.width = (real)dim;
			viewport.height = (real)dim;
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
				renderObjectMat.prefilterTexture,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);

			for (u32 mip = 0; mip < mipLevels; ++mip)
			{
				for (u32 face = 0; face < 6; ++face)
				{
					viewport.width = static_cast<real>(dim * std::pow(0.5f, mip));
					viewport.height = viewport.width;
					vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

					vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

					// Push constants
					skyboxMat.pushConstantBlock.mvp =
						glm::perspective(PI_DIV_TWO, 1.0f, 0.1f, (real)dim) * s_CaptureViews[face];
					vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Material::PushConstantBlock), &skyboxMat.pushConstantBlock);

					vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

					BindDescriptorSet(&prefilterShader, 0, cmdBuf, pipelinelayout, descriptorSet);

					VkDeviceSize offsets[1] = { 0 };

					ShaderID shaderID = skyboxMat.shaderID;
					vkCmdBindVertexBuffers(cmdBuf, 0, 1, &m_VertexIndexBufferPairs[skyboxMat.shaderID].vertexBuffer->m_Buffer, offsets);
					if (skyboxRenderObject->bIndexed)
					{
						vkCmdBindIndexBuffer(cmdBuf, m_VertexIndexBufferPairs[shaderID].indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
						vkCmdDrawIndexed(cmdBuf, skyboxRenderObject->indices->size(), 1, 0, 0, 0);
					}
					else
					{
						vkCmdDraw(cmdBuf, skyboxRenderObject->vertexBufferData->VertexCount, 1, 0, 0);
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

					copyRegion.extent.width = static_cast<u32>(viewport.width);
					copyRegion.extent.height = static_cast<u32>(viewport.height);
					copyRegion.extent.depth = 1;

					vkCmdCopyImage(
						cmdBuf,
						offscreen.image,
						VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						renderObjectMat.prefilterTexture->image,
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
				renderObjectMat.prefilterTexture,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				subresourceRange);

			m_CommandBufferManager.FlushCommandBuffer(cmdBuf, m_GraphicsQueue, true);

			vkDestroyRenderPass(m_VulkanDevice->m_LogicalDevice, renderpass, nullptr);
			vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, offscreen.framebuffer, nullptr);
			vkFreeMemory(m_VulkanDevice->m_LogicalDevice, offscreen.memory, nullptr);
			vkDestroyImageView(m_VulkanDevice->m_LogicalDevice, offscreen.view, nullptr);
			vkDestroyImage(m_VulkanDevice->m_LogicalDevice, offscreen.image, nullptr);
			vkDestroyPipeline(m_VulkanDevice->m_LogicalDevice, pipeline, nullptr);
			vkDestroyPipelineLayout(m_VulkanDevice->m_LogicalDevice, pipelinelayout, nullptr);
		}

		void VulkanRenderer::GenerateBRDFLUT(VulkanTexture* brdfTexture)
		{
			const VkFormat format = VK_FORMAT_R16G16_SFLOAT;
			const u32 dim = (u32)m_BRDFSize.x;
			assert(dim <= MAX_TEXTURE_DIM);

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
			framebufferCreateInfo.pAttachments = &brdfTexture->imageView;
			framebufferCreateInfo.width = dim;
			framebufferCreateInfo.height = dim;
			framebufferCreateInfo.layers = 1;

			VkFramebuffer framebuffer;
			VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &framebufferCreateInfo, nullptr, &framebuffer));

			// Descriptors
			VkDescriptorSetLayout descriptorsetlayout;
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {};

			VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};
			descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorSetLayoutCreateInfo.pBindings = setLayoutBindings.data();
			descriptorSetLayoutCreateInfo.bindingCount = static_cast<u32>(setLayoutBindings.size());
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_VulkanDevice->m_LogicalDevice, &descriptorSetLayoutCreateInfo, nullptr, &descriptorsetlayout));

			// Descriptor Pool
			std::array<VkDescriptorPoolSize, 1> poolSizes;
			poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSizes[0].descriptorCount = 1;

			VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
			descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			descriptorPoolCreateInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
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
			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
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
			pipelineInputAssemblyStateCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
			pipelineDynamicStateCreateInfo.dynamicStateCount = static_cast<u32>(dynamicStateEnables.size());
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

			// TODO: Bring shader compilation out to function
			ShaderID brdfShaderID;
			if (!GetShaderID("brdf", brdfShaderID))
			{
				PrintError("Failed to find brdf shader!\n");
			}
			VulkanShader& brdfShader = m_Shaders[brdfShaderID];

			VDeleter<VkShaderModule> vertShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(brdfShader.shader.vertexShaderCode, vertShaderModule))
			{
				PrintError("Failed to compile vertex shader located at: %s\n", brdfShader.shader.vertexShaderFilePath.c_str());
			}

			VDeleter<VkShaderModule> fragShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(brdfShader.shader.fragmentShaderCode, fragShaderModule))
			{
				PrintError("Failed to compile fragment shader located at: %s\n", brdfShader.shader.fragmentShaderFilePath.c_str());
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

			VkPipeline pipeline = VK_NULL_HANDLE;
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

			VkCommandBuffer cmdBuf = m_CommandBufferManager.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
			vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = {};
			viewport.width = (real)dim;
			viewport.height = (real)dim;
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
			m_CommandBufferManager.FlushCommandBuffer(cmdBuf, m_GraphicsQueue, true);

			vkQueueWaitIdle(m_GraphicsQueue);

			vkDestroyPipeline(m_VulkanDevice->m_LogicalDevice, pipeline, nullptr);
			vkDestroyPipelineLayout(m_VulkanDevice->m_LogicalDevice, pipelinelayout, nullptr);
			vkDestroyRenderPass(m_VulkanDevice->m_LogicalDevice, renderpass, nullptr);
			vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, framebuffer, nullptr);
			vkDestroyDescriptorSetLayout(m_VulkanDevice->m_LogicalDevice, descriptorsetlayout, nullptr);
			vkDestroyDescriptorPool(m_VulkanDevice->m_LogicalDevice, descriptorPool, nullptr);
		}

		MaterialID VulkanRenderer::InitializeMaterial(const MaterialCreateInfo* createInfo, MaterialID matToReplace /*= InvalidMaterialID*/)
		{
			MaterialID matID = InvalidMaterialID;
			if (matToReplace != InvalidMaterialID)
			{
				// TODO: Do any material destruction work here
				matID = matToReplace;
			}
			else
			{
				matID = GetNextAvailableMaterialID();
				m_Materials.insert(std::pair<MaterialID, VulkanMaterial>(matID, {}));
			}

			VulkanMaterial& mat = m_Materials.at(matID);
			mat.material = {};
			mat.material.name = createInfo->name;

			if (!GetShaderID(createInfo->shaderName, mat.material.shaderID))
			{
				if (createInfo->shaderName.empty())
				{
					PrintError("Material's shader not set! MaterialCreateInfo::shaderName must be filled in\n");
				}
				else
				{
					PrintError("Material's shader not set! Shader name %s not found\n", createInfo->shaderName.c_str());
				}

				// TODO: Handle more gracefully (use placeholder shader)
				return InvalidMaterialID;
			}
			VulkanShader& shader = m_Shaders[mat.material.shaderID];

			mat.material.normalTexturePath = createInfo->normalTexturePath;
			mat.material.generateNormalSampler = createInfo->generateNormalSampler;
			mat.material.enableNormalSampler = createInfo->enableNormalSampler;

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

			mat.material.colorMultiplier = createInfo->colorMultiplier;

			mat.material.enableHDREquirectangularSampler = createInfo->enableHDREquirectangularSampler;
			mat.material.generateHDREquirectangularSampler = createInfo->generateHDREquirectangularSampler;
			mat.material.hdrEquirectangularTexturePath = createInfo->hdrEquirectangularTexturePath;

			mat.material.generateHDRCubemapSampler = createInfo->generateHDRCubemapSampler;

			mat.material.enableIrradianceSampler = createInfo->enableIrradianceSampler;
			mat.material.generateIrradianceSampler = createInfo->generateIrradianceSampler;
			mat.material.irradianceSamplerSize = createInfo->generatedIrradianceCubemapSize;

			if (shader.shader.bNeedIrradianceSampler)
			{
				if (createInfo->irradianceSamplerMatID < m_Materials.size())
				{
					mat.irradianceTexture = m_Materials[createInfo->irradianceSamplerMatID].irradianceTexture;
				}
			}
			if (shader.shader.bNeedBRDFLUT)
			{
				if (!m_BRDFTexture)
				{
					m_BRDFTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "BRDF", m_BRDFSize.x, m_BRDFSize.y, 1);
					m_BRDFTexture->CreateEmpty(VK_FORMAT_R16G16_SFLOAT, m_BRDFSize.x, m_BRDFSize.y, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
					m_LoadedTextures.push_back(m_BRDFTexture);
					GenerateBRDFLUT(m_BRDFTexture);
				}
				mat.brdfLUT = m_BRDFTexture;
			}
			if (shader.shader.bNeedPrefilteredMap)
			{
				mat.prefilterTexture = (createInfo->prefilterMapSamplerMatID < m_Materials.size() ?
					m_Materials[createInfo->prefilterMapSamplerMatID].prefilterTexture : nullptr);
			}

			mat.material.enablePrefilteredMap = createInfo->enablePrefilteredMap;
			mat.material.generatePrefilteredMap = createInfo->generatePrefilteredMap;
			mat.material.prefilteredMapSize = createInfo->generatedPrefilteredCubemapSize;

			mat.material.enableBRDFLUT = createInfo->enableBRDFLUT;
			mat.material.renderToCubemap = createInfo->renderToCubemap;

			mat.material.engineMaterial = createInfo->engineMaterial;

			mat.descriptorSetLayoutIndex = mat.material.shaderID;

			struct TextureInfo
			{
				TextureInfo(const std::string& relativeFilePath,
					VulkanTexture** texture,
					bool* generate,
					VkFormat format = VK_FORMAT_R8G8B8A8_UNORM,
					u32 mipLevels = 1,
					bool bHDR = false) :
					relativeFilePath(relativeFilePath),
					texture(texture),
					generate(generate),
					format(format),
					mipLevels(mipLevels),
					bHDR(bHDR)
				{}

				const std::string relativeFilePath;
				VulkanTexture** texture;
				bool* generate;
				VkFormat format;
				u32 mipLevels;
				bool bHDR;
			};

			TextureInfo textureInfos[] =
			{
				{ createInfo->normalTexturePath, &mat.normalTexture, &mat.material.generateNormalSampler },
				{ createInfo->albedoTexturePath, &mat.albedoTexture, &mat.material.generateAlbedoSampler },
				{ createInfo->metallicTexturePath, &mat.metallicTexture, &mat.material.generateMetallicSampler },
				{ createInfo->roughnessTexturePath, &mat.roughnessTexture, &mat.material.generateRoughnessSampler },
				{ createInfo->aoTexturePath, &mat.aoTexture, &mat.material.generateAOSampler },
				{ createInfo->hdrEquirectangularTexturePath, &mat.hdrEquirectangularTexture, &mat.material.generateHDREquirectangularSampler, VK_FORMAT_R32G32B32A32_SFLOAT, 1, true },
			};
			const size_t textureCount = sizeof(textureInfos) / sizeof(textureInfos[0]);

			// Calculate how many textures need to be allocated to prevent texture vector from resizing
			const size_t usedTextureCount = createInfo->generateAlbedoSampler +
				createInfo->generateAOSampler + createInfo->generateCubemapSampler +
				createInfo->generateIrradianceSampler + createInfo->generateMetallicSampler +
				createInfo->generateNormalSampler + createInfo->generatePrefilteredMap +
				createInfo->generateRoughnessSampler + createInfo->generateHDREquirectangularSampler;
			m_LoadedTextures.reserve(usedTextureCount);

			for (TextureInfo& textureInfo : textureInfos)
			{
				if (!textureInfo.relativeFilePath.empty() && textureInfo.generate)
				{
					*textureInfo.texture = GetLoadedTexture(textureInfo.relativeFilePath);

					if (*textureInfo.texture == nullptr)
					{
						u32 channelCount = 4;
						bool bFlipVertically = false;
						*textureInfo.texture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue,
							textureInfo.relativeFilePath, channelCount, bFlipVertically, textureInfo.mipLevels > 1, textureInfo.bHDR);
						(*textureInfo.texture)->CreateFromFile(textureInfo.format);
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
					assert(mat.cubemapTexture == nullptr);

					const u32 mipLevels = static_cast<u32>(floor(log2(createInfo->generatedCubemapSize.x))) + 1;
					u32 channelCount = 4;
					mat.cubemapTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "Cubemap", (u32)createInfo->generatedCubemapSize.x,
						(u32)createInfo->generatedCubemapSize.y, channelCount);
					mat.cubemapTexture->CreateCubemapEmpty(VK_FORMAT_R8G8B8A8_UNORM, mipLevels, createInfo->enableCubemapTrilinearFiltering);
					m_LoadedTextures.push_back(mat.cubemapTexture);
				}
				else
				{
					mat.cubemapTexture = GetLoadedTexture(createInfo->cubeMapFilePaths[0]);

					if (mat.cubemapTexture == nullptr)
					{
						u32 channelCount = 4;
						mat.cubemapTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue,
							createInfo->cubeMapFilePaths, channelCount, false, false, false);
						mat.cubemapTexture->CreateCubemapFromTextures(VK_FORMAT_R8G8B8A8_UNORM, createInfo->cubeMapFilePaths, true);
						m_LoadedTextures.push_back(mat.cubemapTexture);
					}
				}
			}

			if (mat.material.generateHDRCubemapSampler)
			{
				assert(mat.cubemapTexture == nullptr);

				const u32 mipLevels = static_cast<u32>(floor(log2(createInfo->generatedCubemapSize.x))) + 1;
				mat.cubemapTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "HDR Cubemap",
					(u32)createInfo->generatedCubemapSize.x, (u32)createInfo->generatedCubemapSize.y, 4);
				mat.cubemapTexture->CreateCubemapEmpty(VK_FORMAT_R32G32B32A32_SFLOAT, mipLevels, false);
				m_LoadedTextures.push_back(mat.cubemapTexture);
			}

			if (shader.shader.bNeedCubemapSampler)
			{

			}


			if (shader.shader.bNeedBRDFLUT)
			{

			}

			if (mat.material.generateIrradianceSampler)
			{
				assert(mat.irradianceTexture == nullptr);

				const u32 mipLevels = static_cast<u32>(floor(log2(createInfo->generatedIrradianceCubemapSize.x))) + 1;
				mat.irradianceTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "Irradiance sampler",
					(u32)createInfo->generatedIrradianceCubemapSize.x,
					(u32)createInfo->generatedIrradianceCubemapSize.y, 4);
				mat.irradianceTexture->CreateCubemapEmpty(VK_FORMAT_R32G32B32A32_SFLOAT, mipLevels, false);
				m_LoadedTextures.push_back(mat.irradianceTexture);
			}

			if (shader.shader.bNeedIrradianceSampler)
			{

			}

			if (mat.material.generatePrefilteredMap)
			{
				assert(mat.prefilterTexture == nullptr);

				const u32 mipLevels = static_cast<u32>(floor(log2(createInfo->generatedPrefilteredCubemapSize.x))) + 1;
				mat.prefilterTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "Prefiltered map",
					(u32)createInfo->generatedPrefilteredCubemapSize.x,
					(u32)createInfo->generatedPrefilteredCubemapSize.y, 4);
				mat.prefilterTexture->CreateCubemapEmpty(VK_FORMAT_R16G16B16A16_SFLOAT,  mipLevels, true);
				m_LoadedTextures.push_back(mat.prefilterTexture);
			}

			if (shader.shader.bNeedPrefilteredMap)
			{

			}

			return matID;
		}

		TextureID VulkanRenderer::InitializeTexture(const std::string& relativeFilePath, i32 channelCount, bool bFlipVertically, bool bGenerateMipMaps, bool bHDR)
		{
			VulkanTexture* newTex = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, relativeFilePath,
				channelCount, bFlipVertically, false, bHDR);
			newTex->CreateFromFile(newTex->CalculateFormat());
			m_LoadedTextures.push_back(newTex);

			return (TextureID)(m_LoadedTextures.size() - 1);
		}

		u32 VulkanRenderer::InitializeRenderObject(const RenderObjectCreateInfo* createInfo)
		{
			const RenderID renderID = GetNextAvailableRenderID();
			VulkanRenderObject* renderObject = new VulkanRenderObject(m_VulkanDevice->m_LogicalDevice, renderID);

			m_bRebatchRenderObjects = true;

			InsertNewRenderObject(renderObject);
			renderObject->materialID = createInfo->materialID;

			if (renderObject->materialID == InvalidMaterialID)
			{
				if (m_Materials.empty())
				{
					PrintError("Render object created before any materials have been created! Returning...\n");
					return InvalidRenderID;
				}
				else
				{
					PrintError("Render object doesn't have its material ID set! Using first available material\n");
					renderObject->materialID = 0;
				}
			}

			renderObject->vertexBufferData = createInfo->vertexBufferData;
			renderObject->cullMode = CullFaceToVkCullMode(createInfo->cullFace);
			renderObject->materialName = m_Materials[renderObject->materialID].material.name;
			renderObject->gameObject = createInfo->gameObject;

			if (createInfo->indices != nullptr &&
				!createInfo->indices->empty())
			{
				renderObject->indices = createInfo->indices;
				renderObject->bIndexed = true;
			}

			// We've already been post initialized, so we need to create a descriptor set and pipeline here
			if (m_bPostInitialized)
			{
				CreateDescriptorSet(renderID);
				CreateGraphicsPipeline(renderID, false);
			}

			return renderID;
		}

		void VulkanRenderer::CreateUniformBuffers(VulkanShader* shader)
		{
			shader->uniformBuffer.constantData.size = shader->shader.constantBufferUniforms.CalculateSizeInBytes();
			if (shader->uniformBuffer.constantData.size > 0)
			{
				free(shader->uniformBuffer.constantData.data);

				shader->uniformBuffer.constantData.size = GetAlignedUBOSize(shader->uniformBuffer.constantData.size);

				shader->uniformBuffer.constantData.data = (real*)malloc(shader->uniformBuffer.constantData.size);
				assert(shader->uniformBuffer.constantData.data);

				PrepareUniformBuffer(&shader->uniformBuffer.constantBuffer, shader->uniformBuffer.constantData.size,
					VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			}

			shader->uniformBuffer.dynamicData.size = shader->shader.dynamicBufferUniforms.CalculateSizeInBytes();
			if (shader->uniformBuffer.dynamicData.size > 0 && !m_RenderObjects.empty())
			{
				if (shader->uniformBuffer.dynamicData.data) _aligned_free(shader->uniformBuffer.dynamicData.data);

				shader->uniformBuffer.dynamicData.size = GetAlignedUBOSize(shader->uniformBuffer.dynamicData.size);

				const size_t dynamicBufferSize = AllocateUniformBuffer(
					shader->uniformBuffer.dynamicData.size, (void**)&shader->uniformBuffer.dynamicData.data);
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
			if (!renderObject)
			{
				return;
			}

			VkPrimitiveTopology vkTopology = TopologyModeToVkPrimitiveTopology(topology);

			if (vkTopology == VK_PRIMITIVE_TOPOLOGY_MAX_ENUM)
			{
				PrintError("Unsupported TopologyMode passed to VulkanRenderer::SetTopologyMode: %d\n", (i32)topology);
			}
			else
			{
				renderObject->topology = vkTopology;
			}
		}

		void VulkanRenderer::SetClearColor(real r, real g, real b)
		{
			m_ClearColor = { r, g, b, 1.0f };
		}

		void VulkanRenderer::Update()
		{
			m_PhysicsDebugDrawer->UpdateDebugMode();

			UpdateConstantUniformBuffers();

			// TODO: Only update when things have changed
			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				UpdateDynamicUniformBuffer(i);
			}

			UpdateDynamicUniformBuffer(m_GBufferQuadRenderID);
		}

		void VulkanRenderer::Draw()
		{
			DrawCallInfo drawCallInfo = {};

			if (!m_PhysicsDebuggingSettings.DisableAll)
			{
				PhysicsDebugRender();
			}

			// TODO: Don't build command buffers when m_bRebatchRenderObjects is false (but dynamic UI elements still need to be rebuilt!)
			BuildDeferredCommandBuffer(drawCallInfo);
			BuildCommandBuffers(drawCallInfo);

			if (m_bSwapChainNeedsRebuilding)
			{
				m_bSwapChainNeedsRebuilding = false;
				RecreateSwapChain();
			}
			else
			{
				DrawFrame();
			}

			++m_FramesRendered;
		}

		void VulkanRenderer::DrawImGuiRenderObjects()
		{
			ImGui::NewLine();

			ImGui::BeginChild("SelectedObject", ImVec2(0.0f, 500.0f), true);

			const std::vector<GameObject*>& selectedObjects = g_EngineInstance->GetSelectedObjects();
			if (!selectedObjects.empty())
			{
				// TODO: Draw common fields for all selected objects?
				GameObject* selectedObject = selectedObjects[0];
				if (selectedObject)
				{
					selectedObject->DrawImGuiObjects();
				}
			}

			ImGui::EndChild();

			ImGui::NewLine();

			ImGui::Text("Game Objects");

			// Dropping objects onto this text makes them root objects
			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(m_GameObjectPayloadCStr);

				if (payload && payload->Data)
				{
					i32 draggedObjectCount = payload->DataSize / sizeof(GameObject*);

					std::vector<GameObject*> draggedGameObjectsVec;
					draggedGameObjectsVec.reserve(draggedObjectCount);
					for (i32 i = 0; i < draggedObjectCount; ++i)
					{
						draggedGameObjectsVec.push_back(*((GameObject**)payload->Data + i));
					}

					if (!draggedGameObjectsVec.empty())
					{
						std::vector<GameObject*> siblings = draggedGameObjectsVec[0]->GetLaterSiblings();

						for (GameObject* draggedGameObject : draggedGameObjectsVec)
						{
							bool bRootObject = draggedGameObject == draggedGameObjectsVec[0];
							bool bRootSibling = Find(siblings, draggedGameObject) != siblings.end();
							// Only re-parent root-most object (leave sub-hierarchy as-is)
							if ((bRootObject || bRootSibling) &&
								draggedGameObject->GetParent())
							{
								draggedGameObject->GetParent()->RemoveChild(draggedGameObject);
								g_SceneManager->CurrentScene()->AddRootObject(draggedGameObject);
							}
						}
					}
				}
				ImGui::EndDragDropTarget();
			}

			std::vector<GameObject*>& rootObjects = g_SceneManager->CurrentScene()->GetRootObjects();
			for (GameObject* rootObject : rootObjects)
			{
				if (DrawImGuiGameObjectNameAndChildren(rootObject))
				{
					break;
				}
			}

			DoCreateGameObjectButton("Add object...", "Add object");

			if (m_NumPointLightsEnabled < MAX_NUM_POINT_LIGHTS)
			{
				static const char* newPointLightStr = "Add point light";
				if (ImGui::Button(newPointLightStr))
				{
					BaseScene* scene = g_SceneManager->CurrentScene();
					PointLight* newPointLight = new PointLight(scene);
					scene->AddRootObject(newPointLight);
					newPointLight->Initialize();
					newPointLight->PostInitialize();
				}
			}
		}

		void VulkanRenderer::UpdateVertexData(RenderID renderID, VertexBufferData* vertexBufferData)
		{

		}

		void VulkanRenderer::DrawUntexturedQuad(const glm::vec2& pos, AnchorPoint anchor, const glm::vec2& size, const glm::vec4& color)
		{

		}

		void VulkanRenderer::DrawUntexturedQuadRaw(const glm::vec2& pos, const glm::vec2& size, const glm::vec4& color)
		{

		}

		void VulkanRenderer::DrawSprite(const SpriteQuadDrawInfo& drawInfo)
		{

		}

		void VulkanRenderer::DrawImGuiForRenderObjectAndChildren(GameObject* gameObject)
		{
			RenderID renderID = gameObject->GetRenderID();
			VulkanRenderObject* renderObject = nullptr;
			std::string objectName;
			if (renderID != InvalidRenderID)
			{
				renderObject = GetRenderObject(renderID);
				objectName = std::string(gameObject->GetName() + "##" + std::to_string(renderObject->renderID));

				if (!gameObject->IsVisibleInSceneExplorer())
				{
					return;
				}
			}
			else
			{
				// TODO: FIXME: This will fail if multiple objects share the same name
				// and have no valid RenderID. Add "##UID" to end of string to ensure uniqueness
				objectName = std::string(gameObject->GetName());
			}

			const std::string objectID("##" + objectName + "-visible");
			bool visible = gameObject->IsVisible();
			if (ImGui::Checkbox(objectID.c_str(), &visible))
			{
				gameObject->SetVisible(visible);
			}
			ImGui::SameLine();
			if (ImGui::TreeNode(objectName.c_str()))
			{
				//DrawImGuiForRenderObjectCommon(gameObject);

				if (renderObject)
				{
					VulkanMaterial& material = m_Materials[renderObject->materialID];
					VulkanShader& shader = m_Shaders[material.material.shaderID];

					std::string matNameStr = "Material: " + material.material.name;
					std::string shaderNameStr = "Shader: " + shader.shader.name;
					ImGui::TextUnformatted(matNameStr.c_str());
					ImGui::TextUnformatted(shaderNameStr.c_str());

					if (shader.shader.bNeedIrradianceSampler)
					{
						ImGui::Checkbox("Use Irradiance Sampler", &material.material.enableIrradianceSampler);
					}
				}

				ImGui::Indent();
				const std::vector<GameObject*>& children = gameObject->GetChildren();
				for (GameObject* child : children)
				{
					DrawImGuiForRenderObjectAndChildren(child);
				}
				ImGui::Unindent();

				ImGui::TreePop();
			}
		}

		bool VulkanRenderer::DoTextureSelector(const char* label,
			const std::vector<VulkanTexture*>& textures,
			i32* selectedIndex,
			bool* bGenerateSampler)
		{
			bool bValueChanged = false;

			std::string currentTexName = (*selectedIndex == 0 ? "NONE" : (textures[*selectedIndex - 1]->relativeFilePath.c_str()));
			if (ImGui::BeginCombo(label, currentTexName.c_str()))
			{
				for (i32 i = 0; i < (i32)textures.size() + 1; i++)
				{
					bool bTextureSelected = (*selectedIndex == i);

					if (i == 0)
					{
						if (ImGui::Selectable("NONE", bTextureSelected))
						{
							*bGenerateSampler = false;

							*selectedIndex = i;
							bValueChanged = true;
						}
					}
					else
					{
						std::string textureName = textures[i - 1]->relativeFilePath;
						if (ImGui::Selectable(textureName.c_str(), bTextureSelected))
						{
							if (*selectedIndex == 0)
							{
								*bGenerateSampler = true;
							}

							*selectedIndex = i;
							bValueChanged = true;
						}

						if (ImGui::IsItemHovered())
						{
							DoTexturePreviewTooltip(textures[i - 1]);
						}
					}
					if (bTextureSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}

			return bValueChanged;
		}

		void VulkanRenderer::ImGuiUpdateTextureIndexOrMaterial(bool bUpdateTextureMaterial,
			const std::string& texturePath,
			std::string& matTexturePath,
			VulkanTexture* texture,
			i32 i,
			i32* textureIndex,
			VkSampler* sampler)
		{
			if (bUpdateTextureMaterial)
			{
				if (*textureIndex == 0)
				{
					matTexturePath = "";
					*sampler = VK_NULL_HANDLE;
				}
				else if (i == *textureIndex - 1)
				{
					matTexturePath = texturePath;
					if (texture)
					{
						*sampler = texture->sampler;
					}
				}
			}
			else
			{
				if (matTexturePath.empty())
				{
					*textureIndex = 0;
				}
				else if (texturePath.compare(matTexturePath) == 0)
				{
					*textureIndex = i + 1;
				}
			}
		}

		void VulkanRenderer::DoTexturePreviewTooltip(VulkanTexture* texture)
		{
			ImGui::BeginTooltip();

			ImVec2 cursorPos = ImGui::GetCursorPos();

			real textureAspectRatio = (real)texture->width / (real)texture->height;
			real texSize = 128.0f;

			if (texture->channelCount == 4)
			{
				real tiling = 3.0f;
				ImVec2 uv0(0.0f, 0.0f);
				ImVec2 uv1(tiling * textureAspectRatio, tiling);
				VulkanTexture* alphaBGTexture = m_LoadedTextures[m_AlphaBGTextureID];
				ImGui::Image((void*)&alphaBGTexture->image, ImVec2(texSize * textureAspectRatio, texSize), uv0, uv1);
			}

			ImGui::SetCursorPos(cursorPos);

			ImGui::Image((void*)&texture->image, ImVec2(texSize * textureAspectRatio, texSize));

			ImGui::EndTooltip();
		}

		void VulkanRenderer::ReloadShaders()
		{
			// TODO: Implement
		}

		void VulkanRenderer::LoadFonts(bool bForceRender)
		{
			PROFILE_AUTO("Load fonts");

			m_FontsSS.clear();
			m_FontsWS.clear();

			for (FontMetaData& fontMetaData : m_FontMetaDatas)
			{
				if (*(fontMetaData.bitmap) != nullptr)
				{
					delete *(fontMetaData.bitmap);
					(*(fontMetaData.bitmap)) = nullptr;
				}

				std::string fontName = fontMetaData.filePath;
				StripLeadingDirectories(fontName);
				StripFileType(fontName);

				LoadFont(fontMetaData.bitmap,
					fontMetaData.size,
					fontMetaData.filePath,
					fontMetaData.renderedTextureFilePath,
					bForceRender,
					fontMetaData.bScreenSpace);
			}
		}

		void VulkanRenderer::ReloadSkybox(bool bRandomizeTexture)
		{

		}

		void VulkanRenderer::OnWindowSizeChanged(i32 width, i32 height)
		{
			UNREFERENCED_PARAMETER(width);
			UNREFERENCED_PARAMETER(height);

			if (width != 0 && height != 0)
			{
				m_bSwapChainNeedsRebuilding = true;
			}
		}

		void VulkanRenderer::OnPreSceneChange()
		{
			GenerateGBuffer();

			for (VertexIndexBufferPair& pair : m_VertexIndexBufferPairs)
			{
				pair.Empty();
			}
		}

		void VulkanRenderer::OnPostSceneChange()
		{
			if (m_bPostInitialized)
			{
				CreateStaticVertexBuffers();
				CreateStaticIndexBuffers();
			}
		}

		bool VulkanRenderer::GetRenderObjectCreateInfo(RenderID renderID, RenderObjectCreateInfo& outInfo)
		{
			outInfo = {};

			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject)
			{
				return false;
			}

			outInfo.materialID = renderObject->materialID;
			outInfo.vertexBufferData = renderObject->vertexBufferData;
			outInfo.indices = renderObject->indices;
			outInfo.gameObject = renderObject->gameObject;
			outInfo.visible = renderObject->gameObject->IsVisible();
			outInfo.visibleInSceneExplorer = renderObject->gameObject->IsVisibleInSceneExplorer();
			outInfo.cullFace = VkCullModeToCullFace(renderObject->cullMode);
			//outInfo.depthTestReadFunc =  // TODO: ?
			//outInfo.depthWriteEnable = // TODO: ?

			return true;
		}

		void VulkanRenderer::SetVSyncEnabled(bool bEnableVSync)
		{
			m_bVSyncEnabled = bEnableVSync;
		}

		u32 VulkanRenderer::GetRenderObjectCount() const
		{
			u32 count = 0;

			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject != nullptr)
				{
					++count;
				}
			}

			return count;
		}

		u32 VulkanRenderer::GetRenderObjectCapacity() const
		{
			return m_RenderObjects.size();
		}

		void VulkanRenderer::DescribeShaderVariable(RenderID renderID, const std::string& variableName, i32 size, DataType dataType, bool normalized, i32 stride, void* pointer)
		{
			// TODO: Implement
			UNREFERENCED_PARAMETER(renderID);
			UNREFERENCED_PARAMETER(variableName);
			UNREFERENCED_PARAMETER(size);
			UNREFERENCED_PARAMETER(dataType);
			UNREFERENCED_PARAMETER(normalized);
			UNREFERENCED_PARAMETER(stride);
			UNREFERENCED_PARAMETER(pointer);
		}

		void VulkanRenderer::SetSkyboxMesh(GameObject* skyboxMesh)
		{
			m_SkyBoxMesh = skyboxMesh;

			if (skyboxMesh == nullptr)
			{
				return;
			}

			MaterialID skyboxMatierialID = m_SkyBoxMesh->GetMeshComponent()->GetMaterialID();
			if (skyboxMatierialID == InvalidMaterialID)
			{
				PrintError("Skybox doesn't have a valid material! Irradiance textures can't be generated\n");
				return;
			}

			for (u32 i = 0; i < m_RenderObjects.size(); ++i)
			{
				VulkanRenderObject* renderObject = GetRenderObject(i);
				if (renderObject &&
					m_Shaders[m_Materials[renderObject->materialID].material.shaderID].shader.bNeedPrefilteredMap)
				{
					VulkanMaterial* mat = &m_Materials[renderObject->materialID];
					mat->irradianceTexture = m_Materials[skyboxMatierialID].irradianceTexture;
					mat->prefilterTexture = m_Materials[skyboxMatierialID].prefilterTexture;
				}
			}

			m_bRebatchRenderObjects = true;
		}

		GameObject* VulkanRenderer::GetSkyboxMesh()
		{
			return m_SkyBoxMesh;
		}

		void VulkanRenderer::SetRenderObjectMaterialID(RenderID renderID, MaterialID materialID)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (renderObject)
			{
				renderObject->materialID = materialID;
			}
			else
			{
				PrintError("SetRenderObjectMaterialID couldn't find render object with ID %u\n", renderID);
				m_bRebatchRenderObjects = true;
			}
		}

		Material& VulkanRenderer::GetMaterial(MaterialID materialID)
		{
			return m_Materials[materialID].material;
		}

		Shader& VulkanRenderer::GetShader(ShaderID shaderID)
		{
			return m_Shaders[shaderID].shader;
		}

		void VulkanRenderer::DestroyRenderObject(RenderID renderID)
		{
			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject && renderObject->renderID == renderID)
				{
					DestroyRenderObject(renderID, renderObject);
					return;
				}
			}
		}

		void VulkanRenderer::DestroyRenderObject(RenderID renderID, VulkanRenderObject* renderObject)
		{
			if (renderObject)
			{
				if (renderObject->descriptorSet)
				{
					vkFreeDescriptorSets(m_VulkanDevice->m_LogicalDevice, m_DescriptorPool, 1, &(renderObject->descriptorSet));
				}
				renderObject->graphicsPipeline.replace();
				renderObject->pipelineLayout.replace();
				delete renderObject;
			}
			m_RenderObjects[renderID] = nullptr;
			m_bRebatchRenderObjects = true;
		}

		void VulkanRenderer::NewFrame()
		{
			if (m_PhysicsDebugDrawer)
			{
				m_PhysicsDebugDrawer->ClearLines();
			}

			if (g_EngineInstance->IsRenderingImGui())
			{
				ImGui_ImplVulkan_NewFrame();
				ImGui_ImplGlfw_NewFrame();
				ImGui::NewFrame();
			}
		}

		btIDebugDraw* VulkanRenderer::GetDebugDrawer()
		{
			return m_PhysicsDebugDrawer;
		}

		void VulkanRenderer::DrawStringSS(const std::string& str, const glm::vec4& color, AnchorPoint anchor, const glm::vec2& pos, /* Positional offset from anchor */ real spacing, bool bRaw /*= false*/)
		{

		}

		void VulkanRenderer::DrawStringWS(const std::string& str, const glm::vec4& color, const glm::vec3& pos, const glm::quat& rot, real spacing, bool bRaw /*= false*/)
		{

		}

		void VulkanRenderer::DrawAssetBrowserImGui(bool* bShowing)
		{
			// TODO: Consolidate with GL renderer
			ImGui::SetNextWindowSize(ImVec2(400.0f, 350.0f), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Asset browser", bShowing))
			{
				if (ImGui::CollapsingHeader("Materials"))
				{
					static bool bUpdateFields = true;
					const i32 MAX_NAME_LEN = 128;
					static i32 selectedMaterialIndexShort = 0; // Index into shortened array
					static MaterialID selectedMaterialID = 0;
					while (m_Materials[selectedMaterialID].material.engineMaterial &&
						selectedMaterialID < m_Materials.size() - 1)
					{
						++selectedMaterialID;
					}
					static std::string matName = "";
					static i32 selectedShaderIndex = 0;
					// Texture index values of 0 represent no texture, 1 = first index into textures array and so on
					static i32 albedoTextureIndex = 0;
					static bool bUpdateAlbedoTextureMaterial = false;
					static i32 metallicTextureIndex = 0;
					static bool bUpdateMetallicTextureMaterial = false;
					static i32 roughnessTextureIndex = 0;
					static bool bUpdateRoughessTextureMaterial = false;
					static i32 normalTextureIndex = 0;
					static bool bUpdateNormalTextureMaterial = false;
					static i32 aoTextureIndex = 0;
					static bool bUpdateAOTextureMaterial = false;
					VulkanMaterial& mat = m_Materials[selectedMaterialID];

					if (bUpdateFields)
					{
						bUpdateFields = false;

						matName = mat.material.name;
						matName.resize(MAX_NAME_LEN);

						i32 i = 0;
						for (VulkanTexture* texture : m_LoadedTextures)
						{
							std::string texturePath = texture->GetRelativeFilePath();

							ImGuiUpdateTextureIndexOrMaterial(bUpdateAlbedoTextureMaterial,
								texturePath,
								mat.material.albedoTexturePath,
								texture,
								i,
								&albedoTextureIndex,
								&mat.albedoTexture->sampler);

							ImGuiUpdateTextureIndexOrMaterial(bUpdateMetallicTextureMaterial,
								texturePath,
								mat.material.metallicTexturePath,
								texture,
								i,
								&metallicTextureIndex,
								&mat.metallicTexture->sampler);

							ImGuiUpdateTextureIndexOrMaterial(bUpdateRoughessTextureMaterial,
								texturePath,
								mat.material.roughnessTexturePath,
								texture,
								i,
								&roughnessTextureIndex,
								&mat.roughnessTexture->sampler);

							ImGuiUpdateTextureIndexOrMaterial(bUpdateNormalTextureMaterial,
								texturePath,
								mat.material.normalTexturePath,
								texture,
								i,
								&normalTextureIndex,
								&mat.normalTexture->sampler);

							ImGuiUpdateTextureIndexOrMaterial(bUpdateAOTextureMaterial,
								texturePath,
								mat.material.aoTexturePath,
								texture,
								i,
								&aoTextureIndex,
								&mat.aoTexture->sampler);

							++i;
						}

						mat.material.enableAlbedoSampler = (albedoTextureIndex > 0);
						mat.material.enableMetallicSampler = (metallicTextureIndex > 0);
						mat.material.enableRoughnessSampler = (roughnessTextureIndex > 0);
						mat.material.enableNormalSampler = (normalTextureIndex > 0);
						mat.material.enableAOSampler = (aoTextureIndex > 0);

						selectedShaderIndex = mat.material.shaderID;
					}

					ImGui::PushItemWidth(160.0f);
					if (ImGui::InputText("Name", (char*)matName.data(), MAX_NAME_LEN, ImGuiInputTextFlags_EnterReturnsTrue))
					{
						// Remove trailing \0 characters
						matName = std::string(matName.c_str());
						mat.material.name = matName;
					}
					ImGui::PopItemWidth();

					ImGui::SameLine();

					struct ShaderFunctor
					{
						static bool GetShaderName(void* data, int idx, const char** out_str)
						{
							*out_str = ((VulkanShader*)data)[idx].shader.name.c_str();
							return true;
						}
					};
					ImGui::PushItemWidth(240.0f);
					if (ImGui::Combo("Shader", &selectedShaderIndex, &ShaderFunctor::GetShaderName,
						(void*)m_Shaders.data(), m_Shaders.size()))
					{
						mat = m_Materials[selectedMaterialID];
						mat.material.shaderID = selectedShaderIndex;

						bUpdateFields = true;
					}
					ImGui::PopItemWidth();

					ImGui::NewLine();

					ImGui::Columns(2);
					ImGui::SetColumnWidth(0, 240.0f);

					ImGui::ColorEdit3("Albedo", &mat.material.constAlbedo.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);

					if (mat.material.enableMetallicSampler)
					{
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
					}
					ImGui::SliderFloat("Metallic", &mat.material.constMetallic, 0.0f, 1.0f, "%.2f");
					if (mat.material.enableMetallicSampler)
					{
						ImGui::PopStyleColor();
					}

					if (mat.material.enableRoughnessSampler)
					{
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
					}
					ImGui::SliderFloat("Roughness", &mat.material.constRoughness, 0.0f, 1.0f, "%.2f");
					if (mat.material.enableRoughnessSampler)
					{
						ImGui::PopStyleColor();
					}

					if (mat.material.enableAOSampler)
					{
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
					}
					ImGui::SliderFloat("AO", &mat.material.constAO, 0.0f, 1.0f, "%.2f");
					if (mat.material.enableAOSampler)
					{
						ImGui::PopStyleColor();
					}

					ImGui::DragFloat("Texture scale", &mat.material.textureScale, 0.1f);

					ImGui::NextColumn();

					struct TextureFunctor
					{
						static bool GetTextureFileName(void* data, i32 idx, const char** out_str)
						{
							if (idx == 0)
							{
								*out_str = "NONE";
							}
							else
							{
								*out_str = ((VulkanTexture**)data)[idx - 1]->GetName().c_str();
							}
							return true;
						}
					};

					std::vector<VulkanTexture*> textures;
					textures.reserve(m_LoadedTextures.size());
					for (VulkanTexture* texture : m_LoadedTextures)
					{
						textures.push_back(texture);
					}

					bUpdateAlbedoTextureMaterial = DoTextureSelector("Albedo texture", textures,
						&albedoTextureIndex, &mat.material.generateAlbedoSampler);
					bUpdateFields |= bUpdateAlbedoTextureMaterial;
					bUpdateMetallicTextureMaterial = DoTextureSelector("Metallic texture", textures,
						&metallicTextureIndex, &mat.material.generateMetallicSampler);
					bUpdateFields |= bUpdateMetallicTextureMaterial;
					bUpdateRoughessTextureMaterial = DoTextureSelector("Roughness texture", textures,
						&roughnessTextureIndex, &mat.material.generateRoughnessSampler);
					bUpdateFields |= bUpdateRoughessTextureMaterial;
					bUpdateNormalTextureMaterial = DoTextureSelector("Normal texture", textures,
						&normalTextureIndex, &mat.material.generateNormalSampler);
					bUpdateFields |= bUpdateNormalTextureMaterial;
					bUpdateAOTextureMaterial = DoTextureSelector("AO texture", textures, &aoTextureIndex,
						&mat.material.generateAOSampler);
					bUpdateFields |= bUpdateAOTextureMaterial;

					ImGui::NewLine();

					ImGui::EndColumns();

					if (ImGui::BeginChild("material list", ImVec2(0.0f, 120.0f), true))
					{
						i32 matShortIndex = 0;
						for (i32 i = 0; i < (i32)m_Materials.size(); ++i)
						{
							if (m_Materials[i].material.engineMaterial)
							{
								continue;
							}

							bool bSelected = (matShortIndex == selectedMaterialIndexShort);
							if (ImGui::Selectable(m_Materials[i].material.name.c_str(), &bSelected))
							{
								if (selectedMaterialIndexShort != matShortIndex)
								{
									selectedMaterialIndexShort = matShortIndex;
									selectedMaterialID = i;
									bUpdateFields = true;
								}
							}

							if (ImGui::BeginPopupContextItem())
							{
								if (ImGui::Button("Duplicate"))
								{
									const Material& dupMat = m_Materials[i].material;

									MaterialCreateInfo createInfo = {};
									createInfo.name = GetIncrementedPostFixedStr(dupMat.name, "new material 00");
									createInfo.shaderName = m_Shaders[dupMat.shaderID].shader.name;
									createInfo.constAlbedo = dupMat.constAlbedo;
									createInfo.constRoughness = dupMat.constRoughness;
									createInfo.constMetallic = dupMat.constMetallic;
									createInfo.constAO = dupMat.constAO;
									createInfo.colorMultiplier = dupMat.colorMultiplier;
									// TODO: Copy other fields
									MaterialID newMaterialID = InitializeMaterial(&createInfo);

									g_SceneManager->CurrentScene()->AddMaterialID(newMaterialID);

									ImGui::CloseCurrentPopup();
								}

								ImGui::EndPopup();
							}

							if (ImGui::IsItemActive())
							{
								if (ImGui::BeginDragDropSource())
								{
									MaterialID draggedMaterialID = i;
									const void* data = (void*)(&draggedMaterialID);
									size_t size = sizeof(MaterialID);

									ImGui::SetDragDropPayload(m_MaterialPayloadCStr, data, size);

									ImGui::Text("%s", m_Materials[i].material.name.c_str());

									ImGui::EndDragDropSource();
								}
							}

							++matShortIndex;
						}
					}
					ImGui::EndChild(); // Material list

					const i32 MAX_MAT_NAME_LEN = 128;
					static std::string newMaterialName = "";

					const char* createMaterialPopupStr = "Create material##popup";
					if (ImGui::Button("Create material"))
					{
						ImGui::OpenPopup(createMaterialPopupStr);
						newMaterialName = "New Material 01";
						newMaterialName.resize(MAX_MAT_NAME_LEN);
					}

					if (ImGui::BeginPopupModal(createMaterialPopupStr, NULL, ImGuiWindowFlags_NoResize))
					{
						ImGui::Text("Name:");
						ImGui::InputText("##NameText", (char*)newMaterialName.data(), MAX_MAT_NAME_LEN);

						ImGui::Text("Shader:");
						static i32 newMatShaderIndex = 0;
						if (ImGui::BeginChild("Shader", ImVec2(0, 120), true))
						{
							i32 i = 0;
							for (VulkanShader& shader : m_Shaders)
							{
								bool bSelectedShader = (i == newMatShaderIndex);
								if (ImGui::Selectable(shader.shader.name.c_str(), &bSelectedShader))
								{
									newMatShaderIndex = i;
								}

								++i;
							}
						}
						ImGui::EndChild(); // Shader list

						if (ImGui::Button("Create new material"))
						{
							// Remove trailing /0 characters
							newMaterialName = std::string(newMaterialName.c_str());

							MaterialCreateInfo createInfo = {};
							createInfo.name = newMaterialName;
							createInfo.shaderName = m_Shaders[newMatShaderIndex].shader.name;
							MaterialID newMaterialID = InitializeMaterial(&createInfo);

							g_SceneManager->CurrentScene()->AddMaterialID(newMaterialID);

							ImGui::CloseCurrentPopup();
						}

						ImGui::SameLine();

						if (ImGui::Button("Cancel"))
						{
							ImGui::CloseCurrentPopup();
						}

						ImGui::EndPopup();
					}

					ImGui::SameLine();

					ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColor);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColor);
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColor);
					if (ImGui::Button("Delete material"))
					{
						g_SceneManager->CurrentScene()->RemoveMaterialID(selectedMaterialID);
						RemoveMaterial(selectedMaterialID);
					}
					ImGui::PopStyleColor();
					ImGui::PopStyleColor();
					ImGui::PopStyleColor();
				}

				if (ImGui::CollapsingHeader("Textures"))
				{
					static i32 selectedTextureIndex = 0;
					if (ImGui::BeginChild("texture list", ImVec2(0.0f, 120.0f), true))
					{
						i32 i = 0;
						for (VulkanTexture* texture : m_LoadedTextures)
						{
							bool bSelected = (i == selectedTextureIndex);
							std::string textureFileName = texture->GetName();
							if (ImGui::Selectable(textureFileName.c_str(), &bSelected))
							{
								selectedTextureIndex = i;
							}

							if (ImGui::BeginPopupContextItem())
							{
								if (ImGui::Button("Reload"))
								{
									texture->Reload();
									ImGui::CloseCurrentPopup();
								}

								ImGui::EndPopup();
							}

							if (ImGui::IsItemHovered())
							{
								DoTexturePreviewTooltip(texture);
							}
							++i;
						}
					}
					ImGui::EndChild();

					// TODO:
					//if (ImGui::Button("Import Texture"))
					//{
					//	// TODO: Not all textures are directly in this directory! CLEANUP to make more robust
					//	std::string relativeDirPath = RESOURCE_LOCATION  "textures/";
					//	std::string absoluteDirectoryStr = RelativePathToAbsolute(relativeDirPath);
					//	std::string selectedAbsFilePath;
					//	if (OpenFileDialog("Import texture", absoluteDirectoryStr, selectedAbsFilePath))
					//	{
					//		std::string fileNameAndExtension = selectedAbsFilePath;
					//		StripLeadingDirectories(fileNameAndExtension);
					//		std::string relativeFilePath = relativeDirPath + fileNameAndExtension;

					//		Print("Importing texture: %s\n", relativeFilePath.c_str());

					//		VulkanTexture* newTexture = new VulkanTexture(relativeFilePath, 3, false, false, false);
					//		if (newTexture->LoadFromFile())
					//		{
					//			m_LoadedTextures.push_back(newTexture);
					//		}

					//		ImGui::CloseCurrentPopup();
					//	}
					//}
				}

				if (ImGui::CollapsingHeader("Meshes"))
				{
					static i32 selectedMeshIndex = 0;
					//const i32 MAX_NAME_LEN = 128;
					// TODO: Implement mesh naming
					//static std::string selectedMeshName = "";
					static bool bUpdateName = true;

					std::string selectedMeshRelativeFilePath;
					LoadedMesh* selectedMesh = nullptr;
					i32 meshIdx = 0;
					for (auto meshPair : MeshComponent::m_LoadedMeshes)
					{
						if (meshIdx == selectedMeshIndex)
						{
							selectedMesh = meshPair.second;
							selectedMeshRelativeFilePath = meshPair.first;
							break;
						}
						++meshIdx;
					}

					ImGui::Text("Import settings");

					ImGui::Columns(2, "import settings columns", false);
					ImGui::Separator();
					ImGui::Checkbox("Flip U", &selectedMesh->importSettings.flipU); ImGui::NextColumn();
					ImGui::Checkbox("Flip V", &selectedMesh->importSettings.flipV); ImGui::NextColumn();
					ImGui::Checkbox("Swap Normal YZ", &selectedMesh->importSettings.swapNormalYZ); ImGui::NextColumn();
					ImGui::Checkbox("Flip Normal Z", &selectedMesh->importSettings.flipNormalZ); ImGui::NextColumn();
					ImGui::Columns(1);

					if (ImGui::Button("Re-import"))
					{
						for (VulkanRenderObject* renderObject : m_RenderObjects)
						{
							if (renderObject && renderObject->gameObject)
							{
								MeshComponent* gameObjectMesh = renderObject->gameObject->GetMeshComponent();
								if (gameObjectMesh &&  gameObjectMesh->GetRelativeFilePath().compare(selectedMeshRelativeFilePath) == 0)
								{
									MeshImportSettings importSettings = selectedMesh->importSettings;

									MaterialID matID = renderObject->materialID;
									GameObject* gameObject = renderObject->gameObject;

									DestroyRenderObject(gameObject->GetRenderID());
									gameObject->SetRenderID(InvalidRenderID);

									gameObjectMesh->Destroy();
									gameObjectMesh->SetOwner(gameObject);
									gameObjectMesh->SetRequiredAttributesFromMaterialID(matID);
									gameObjectMesh->LoadFromFile(selectedMeshRelativeFilePath, &importSettings);
								}
							}
						}
					}

					ImGui::SameLine();

					if (ImGui::Button("Save"))
					{
						g_SceneManager->CurrentScene()->SerializeToFile(true);
					}

					if (ImGui::BeginChild("mesh list", ImVec2(0.0f, 120.0f), true))
					{
						i32 i = 0;
						for (const auto& meshIter : MeshComponent::m_LoadedMeshes)
						{
							bool bSelected = (i == selectedMeshIndex);
							std::string meshFilePath = meshIter.first;
							std::string meshFileName = meshIter.first;
							StripLeadingDirectories(meshFileName);
							if (ImGui::Selectable(meshFileName.c_str(), &bSelected))
							{
								selectedMeshIndex = i;
								bUpdateName = true;
							}

							if (ImGui::BeginPopupContextItem())
							{
								if (ImGui::Button("Reload"))
								{
									MeshComponent::LoadMesh(meshIter.second->relativeFilePath);

									for (VulkanRenderObject* renderObject : m_RenderObjects)
									{
										if (renderObject)
										{
											MeshComponent* mesh = renderObject->gameObject->GetMeshComponent();
											if (mesh && mesh->GetRelativeFilePath().compare(meshFilePath) == 0)
											{
												mesh->Reload();
											}
										}
									}

									ImGui::CloseCurrentPopup();
								}

								ImGui::EndPopup();
							}
							else
							{
								if (ImGui::IsItemActive())
								{
									if (ImGui::BeginDragDropSource())
									{
										const void* data = (void*)(meshIter.first.c_str());
										size_t size = strlen(meshIter.first.c_str()) * sizeof(char);

										ImGui::SetDragDropPayload(m_MeshPayloadCStr, data, size);

										ImGui::Text("%s", meshFileName.c_str());

										ImGui::EndDragDropSource();
									}
								}
							}

							++i;
						}
					}
					ImGui::EndChild();

					if (ImGui::Button("Import Mesh"))
					{
						// TODO: Not all models are directly in this directory! CLEANUP to make more robust
						std::string relativeDirPath = RESOURCE_LOCATION  "meshes/";
						std::string absoluteDirectoryStr = RelativePathToAbsolute(relativeDirPath);
						std::string selectedAbsFilePath;
						if (OpenFileDialog("Import mesh", absoluteDirectoryStr, selectedAbsFilePath))
						{
							Print("Importing mesh: %s\n", selectedAbsFilePath.c_str());

							std::string fileNameAndExtension = selectedAbsFilePath;
							StripLeadingDirectories(fileNameAndExtension);
							std::string relativeFilePath = relativeDirPath + fileNameAndExtension;

							LoadedMesh* existingMesh = nullptr;
							if (MeshComponent::FindPreLoadedMesh(relativeFilePath, &existingMesh))
							{
								i32 j = 0;
								for (auto meshPair : MeshComponent::m_LoadedMeshes)
								{
									if (meshPair.first.compare(relativeFilePath) == 0)
									{
										selectedMeshIndex = j;
										break;
									}

									++j;
								}
							}
							else
							{
								MeshComponent::LoadMesh(relativeFilePath);
							}

							ImGui::CloseCurrentPopup();
						}
					}
				}
			}

			ImGui::End();
		}

		void VulkanRenderer::RecaptureReflectionProbe()
		{
			// UNIMPLEMENTED
		}

		u32 VulkanRenderer::GetTextureHandle(TextureID textureID) const
		{
			return 0;
		}

		void VulkanRenderer::RenderObjectStateChanged()
		{
			// NOTE: This member is currently ignored by VulkanRenderer
			m_bRebatchRenderObjects = true;
		}

		void VulkanRenderer::PostInitializeRenderObject(RenderID renderID)
		{
			UNREFERENCED_PARAMETER(renderID);
		}

		void VulkanRenderer::ClearMaterials(bool bDestroyEngineMats /* = false */)
		{
			auto iter = m_Materials.begin();
			while (iter != m_Materials.end())
			{
				if (bDestroyEngineMats || iter->second.material.engineMaterial == false)
				{
					if (iter->second.hdrCubemapFramebuffer)
					{
						vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, iter->second.hdrCubemapFramebuffer, nullptr);
					}

					iter = m_Materials.erase(iter);
				}
				else
				{
					++iter;
				}
			}
		}

		VulkanRenderObject* VulkanRenderer::GetRenderObject(RenderID renderID)
		{
#if DEBUG
			if (renderID > m_RenderObjects.size() ||
				renderID == InvalidRenderID)
			{
				PrintError("Invalid renderID passed to GetRenderObject: %u\n", renderID);
				return nullptr;
			}
#endif

			return m_RenderObjects[renderID];
		}

		u32 VulkanRenderer::GetActiveRenderObjectCount() const
		{
			u32 capacity = 0;
			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject)
				{
					++capacity;
				}
			}
			return capacity;
		}

		bool VulkanRenderer::LoadFont(BitmapFont** font, i16 size,
			const std::string& fontFilePath,
			const std::string& renderedFontFilePath,
			bool bForceRender, bool bScreenSpace)
		{
			FT_Library ft;
			if (FT_Init_FreeType(&ft) != FT_Err_Ok)
			{
				PrintError("Failed to initialize FreeType\n");
				return false;
			}

			std::map<i32, FontMetric*> characters;
			std::array<glm::vec2i, 4> maxPos;
			FT_Face face;
			if (!LoadFontMetrics(fontFilePath, ft, font, size, bScreenSpace, &characters, &maxPos, &face))
			{
				return false;
			}

			std::string fileName = fontFilePath;
			StripLeadingDirectories(fileName);

			BitmapFont* newFont = *font;

			// TODO: Save in common place
			u32 sampleDensity = 32;
			u32 padding = 1;
			u32 spread = 5;
			//u32 totPadding = padding + spread;

			std::string textureName = std::string("Font atlas ") + fileName;
			bool bUsingPreRenderedTexture = false;
			if (!bForceRender)
			{
				if (FileExists(renderedFontFilePath))
				{
					VulkanTexture* fontTex = newFont->SetTexture(new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, renderedFontFilePath, 4, false, false, false));

					if (fontTex->CreateFromFile(fontTex->CalculateFormat()))
					{
						bUsingPreRenderedTexture = true;

						for (auto& charPair : characters)
						{
							FontMetric* metric = charPair.second;

							if (isspace(metric->character) ||
								metric->character == '\0' ||
								metric->character == '\t' ||
								metric->character == '\r' ||
								metric->character == '\n' ||
								metric->character == '\b')
							{
								continue;
							}

							u32 glyphIndex = FT_Get_Char_Index(face, metric->character);
							if (glyphIndex == 0)
							{
								continue;
							}

							metric->texCoord = metric->texCoord / glm::vec2((real)fontTex->width, (real)fontTex->height);
						}
					}
					else
					{
						newFont->ClearTexture();
						SafeDelete(fontTex);
					}
				}
			}


			if (!bUsingPreRenderedTexture)
			{
				glm::vec2i textureSize(
					std::max(std::max(maxPos[0].x, maxPos[1].x), std::max(maxPos[2].x, maxPos[3].x)),
					std::max(std::max(maxPos[0].y, maxPos[1].y), std::max(maxPos[2].y, maxPos[3].y)));
				newFont->SetTextureSize(textureSize);

				VulkanTexture* fontTex = newFont->SetTexture(new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, textureName, 4, false, false, false));
				// TODO: VK_IMAGE_USAGE_TRANSFER_DST_BIT?
				VkFormat imageFormat = VK_FORMAT_R16G16B16A16_UNORM;
				fontTex->CreateEmpty(imageFormat, textureSize.x, textureSize.y, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
				fontTex->Build();

				ShaderID computeSDFShaderID;
				GetShaderID("compute_sdf", computeSDFShaderID);
				VulkanShader& computeSDFShader = m_Shaders[computeSDFShaderID];

				//"highResTex" =  0
				//"spread" = (real)spread
				//"sampleDensity" = (real)sampleDensity
				//VulkanRenderObject* gBufferRenderObject = GetRenderObject(m_GBufferQuadRenderID);

				// Render to Glyphs atlas
				FT_Set_Pixel_Sizes(face, 0, size * sampleDensity);

				for (auto& charPair : characters)
				{
					FontMetric* metric = charPair.second;

					if (metric->character == ' ')
					{
						continue;
					}

					u32 glyphIndex = FT_Get_Char_Index(face, metric->character);
					if (glyphIndex == 0)
					{
						continue;
					}

					if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER))
					{
						PrintError("Failed to load glyph with index %u\n", glyphIndex);
						continue;
					}

					if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP)
					{
						if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL))
						{
							PrintError("Failed to render glyph with index %u\n", glyphIndex);
							continue;
						}
					}

					if (face->glyph->bitmap.width == 0 ||
						face->glyph->bitmap.rows == 0)
					{
						continue;
					}

					FT_Bitmap alignedBitmap = {};
					if (FT_Bitmap_Convert(ft, &face->glyph->bitmap, &alignedBitmap, 4))
					{
						PrintError("Couldn't align free type bitmap size\n");
						continue;
					}

					u32 width = alignedBitmap.width;
					u32 height = alignedBitmap.rows;

					if (width == 0 || height == 0)
					{
						continue;
					}

					// GLuint texHandle;
					// GenTexture(texHandle);

					//glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, alignedBitmap.buffer);

					VulkanTexture* highResTex = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "High res tex", width, height, 4);

					highResTex->CreateFromMemory(alignedBitmap.buffer, imageFormat, 1);

					VkDescriptorSet descSet;
					VkDescriptorSet descSetLayout;

					DescriptorSetCreateInfo descSetCreateInfo = {};
					descSetCreateInfo.descriptorSet = &descSet;
					descSetCreateInfo.descriptorSetLayout = &descSetLayout;
					descSetCreateInfo.shaderID = computeSDFShaderID;
					descSetCreateInfo.uniformBuffer = &computeSDFShader.uniformBuffer;
					descSetCreateInfo.albedoTexture = highResTex;
					CreateDescriptorSet(&descSetCreateInfo);

					glm::vec4 borderColor(0.0f);

					if (metric->width > 0 && metric->height > 0)
					{
						glm::vec2i res = glm::vec2i(metric->width - padding * 2, metric->height - padding * 2);
						glm::vec2i viewportTL = glm::vec2i(metric->texCoord) + glm::vec2i(padding);

						// set viewport (viewportTL.x, viewportTL.y, res.x, res.y);

						// uniform texChannel = metric->channel
						// uniform charResolution = (real)res.x, (real)res.y
						// DrawArrays(gBufferRenderObject->topology, 0, gBufferRenderObject->vertexBufferData->VertexCount)
					}

					// DeleteTexture(texHandle);

					metric->texCoord = metric->texCoord / glm::vec2((real)textureSize.x, (real)textureSize.y);

					FT_Bitmap_Done(ft, &alignedBitmap);
				}

				std::string savedSDFTextureAbsFilePath = RelativePathToAbsolute(renderedFontFilePath);
				fontTex->SaveToFile(savedSDFTextureAbsFilePath, ImageFormat::PNG);
			}

			FT_Done_Face(face);
			FT_Done_FreeType(ft);

			return false;
		}

		u32 VulkanRenderer::GetAlignedUBOSize(u32 unalignedSize)
		{
			u32 alignedSize = unalignedSize;
			const u32 nCAS = (u32)m_VulkanDevice->m_PhysicalDeviceProperties.limits.nonCoherentAtomSize;
			if (unalignedSize % nCAS != 0)
			{
				alignedSize += nCAS - (alignedSize % nCAS);
			}
			return alignedSize;
		}

		void VulkanRenderer::DrawSpriteQuad(const SpriteQuadDrawInfo& drawInfo)
		{

		}

		void VulkanRenderer::DrawScreenSpaceSprites()
		{

		}

		void VulkanRenderer::DrawWorldSpaceSprites()
		{

		}

		void VulkanRenderer::DrawTextSS()
		{

		}

		void VulkanRenderer::DrawTextWS()
		{

		}

		void VulkanRenderer::UpdateRenderObjectVertexData(RenderID renderID)
		{
			//RenderObject renderObject = GetRenderObject(renderID);

			//CreateDescriptorSet(renderID);
			// TODO: ?
			CreateGraphicsPipeline(renderID, false);
		}

		MaterialID VulkanRenderer::GetNextAvailableMaterialID()
		{
			// Return lowest available ID
			MaterialID result = 0;
			while (m_Materials.find(result) != m_Materials.end())
			{
				++result;
			}
			return result;
		}

		RenderID VulkanRenderer::GetNextAvailableRenderID() const
		{
			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				if (!m_RenderObjects[i])
				{
					return i;
				}
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

		void VulkanRenderer::CreateInstance()
		{
			if (m_bEnableValidationLayers && !CheckValidationLayerSupport())
			{
				// TODO: Remove all exceptions
				throw std::runtime_error("validation layers requested, but not available!");
			}

			VkApplicationInfo appInfo = {};
			appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
			std::string applicationName = g_Window->GetTitle();
			appInfo.pApplicationName = applicationName.c_str();
			appInfo.applicationVersion = VK_API_VERSION_1_0;
			appInfo.pEngineName = "Flex Engine";
			appInfo.engineVersion = FLEX_VERSION(FlexEngine::EngineVersionMajor, FlexEngine::EngineVersionMinor, FlexEngine::EngineVersionPatch);
			appInfo.apiVersion = VK_API_VERSION_1_0;

			VkInstanceCreateInfo createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
			createInfo.pApplicationInfo = &appInfo;

			std::vector<const char*> extensions = GetRequiredExtensions();
			createInfo.enabledExtensionCount = extensions.size();
			createInfo.ppEnabledExtensionNames = extensions.data();

			if (m_bEnableValidationLayers)
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
			if (!m_bEnableValidationLayers)
			{
				return;
			}

			VkDebugReportCallbackCreateInfoEXT createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
			createInfo.flags =
				VK_DEBUG_REPORT_ERROR_BIT_EXT |
				VK_DEBUG_REPORT_WARNING_BIT_EXT |
				VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
				//VK_DEBUG_REPORT_DEBUG_BIT_EXT;
			createInfo.pfnCallback = DebugCallback;

			VK_CHECK_RESULT(CreateDebugReportCallbackEXT(m_Instance, &createInfo, nullptr, m_Callback.replace()));
		}

		void VulkanRenderer::CreateSurface()
		{
			VK_CHECK_RESULT(glfwCreateWindowSurface(m_Instance, ((GLFWWindowWrapper*)g_Window)->GetWindow(), nullptr, m_Surface.replace()));
		}

		//void VulkanRenderer::SetupImGuiWindowData(ImGui_ImplVulkanH_WindowData* data, VkSurfaceKHR surface, i32 width, i32 height)
		//{
		//	data->Surface = surface;

		//	// Check for WSI support
		//	VkBool32 res;
		//	vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, g_QueueFamily, data->Surface, &res);
		//	if (res != VK_TRUE)
		//	{
		//		fprintf(stderr, "Error no WSI support on physical device 0\n");
		//		exit(-1);
		//	}

		//	// Select Surface Format
		//	const VkFormat requestSurfaceImageFormat[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
		//	const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
		//	data->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(g_PhysicalDevice, data->Surface, requestSurfaceImageFormat, (size_t)IM_ARRAYSIZE(requestSurfaceImageFormat), requestSurfaceColorSpace);

		//	// Select Present Mode
		//	// TODO: Use same present mode as main app
		//	std::vector<VkPresentModeKHR> present_modes;
		//	if (m_bVSyncEnabled)
		//	{
		//		present_modes = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };
		//	}
		//	else
		//	{
		//		present_modes = { VK_PRESENT_MODE_FIFO_KHR };
		//	}

		//	data->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(g_PhysicalDevice, data->Surface, &present_modes[0], present_modes.size());

		//	// Create SwapChain, RenderPass, Framebuffer, etc.
		//	ImGui_ImplVulkanH_CreateWindowDataCommandBuffers(g_PhysicalDevice, g_Device, g_QueueFamily, data, g_Allocator);
		//	ImGui_ImplVulkanH_CreateWindowDataSwapChainAndFramebuffer(g_PhysicalDevice, g_Device, data, g_Allocator, width, height);
		//}

		VkPhysicalDevice VulkanRenderer::PickPhysicalDevice()
		{
			VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

			u32 deviceCount = 0;
			vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);

			if (deviceCount == 0)
			{
				throw std::runtime_error("Failed to find GPUs with Vulkan support!");
			}

			std::vector<VkPhysicalDevice> devices(deviceCount);
			vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

			for (const VkPhysicalDevice& device : devices)
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
			VulkanQueueFamilyIndices indices = FindQueueFamilies(m_Surface, physicalDevice);

			std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
			std::set<i32> uniqueQueueFamilies = { indices.graphicsFamily, indices.presentFamily };

			real queuePriority = 1.0f;
			for (i32 queueFamily : uniqueQueueFamilies)
			{
				VkDeviceQueueCreateInfo queueCreateInfo = {};
				queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueCreateInfo.queueFamilyIndex = (u32)queueFamily;
				queueCreateInfo.queueCount = 1;
				queueCreateInfo.pQueuePriorities = &queuePriority;
				queueCreateInfos.push_back(queueCreateInfo);
			}

			VkPhysicalDeviceFeatures deviceFeatures = {};
			deviceFeatures.samplerAnisotropy = VK_TRUE;

			VkDeviceCreateInfo createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

			createInfo.pQueueCreateInfos = queueCreateInfos.data();
			createInfo.queueCreateInfoCount = (u32)queueCreateInfos.size();

			createInfo.pEnabledFeatures = &deviceFeatures;

			createInfo.enabledExtensionCount = m_DeviceExtensions.size();
			createInfo.ppEnabledExtensionNames = m_DeviceExtensions.data();

			if (m_bEnableValidationLayers)
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

			vkGetDeviceQueue(m_VulkanDevice->m_LogicalDevice, (u32)indices.graphicsFamily, 0, &m_GraphicsQueue);
			vkGetDeviceQueue(m_VulkanDevice->m_LogicalDevice, (u32)indices.presentFamily, 0, &m_PresentQueue);
		}

		void VulkanRenderer::RecreateSwapChain()
		{
			const glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
			if (frameBufferSize.x == 0 || frameBufferSize.y == 0)
			{
				return;
			}

			vkDeviceWaitIdle(m_VulkanDevice->m_LogicalDevice);

			CreateSwapChain();
			CreateSwapChainImageViews();

			CreateDepthResources();

			PrepareOffscreenFrameBuffer();
			CreateRenderPass();

			for (u32 i = 0; i < m_RenderObjects.size(); ++i)
			{
				CreateDescriptorSet(i);
				CreateGraphicsPipeline(i, false);
			}

			CreateFramebuffers();
			m_CommandBufferManager.CreateCommandBuffers(m_SwapChainImages.size());
		}

		void VulkanRenderer::CreateSwapChain()
		{
			VulkanSwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(m_VulkanDevice->m_PhysicalDevice);

			VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
			VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
			VkExtent2D extent = ChooseSwapExtent(swapChainSupport.capabilities);

			u32 imageCount = swapChainSupport.capabilities.minImageCount + 1;
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

			VulkanQueueFamilyIndices indices = FindQueueFamilies(m_Surface, m_VulkanDevice->m_PhysicalDevice);
			u32 queueFamilyIndices[] = { (u32)indices.graphicsFamily, (u32)indices.presentFamily };

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

			for (u32 i = 0; i < m_SwapChainImages.size(); ++i)
			{
				VulkanTexture::ImageViewCreateInfo viewCreateInfo = {};
				viewCreateInfo.imageView = m_SwapChainImageViews[i].replace();
				viewCreateInfo.image = &m_SwapChainImages[i];
				viewCreateInfo.format = m_SwapChainImageFormat;
				viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
				viewCreateInfo.aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
				viewCreateInfo.layerCount = 1;
				viewCreateInfo.layerCount = 1;
				viewCreateInfo.mipLevels = 1;

				VulkanTexture::CreateImageView(m_VulkanDevice, viewCreateInfo);
			}
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
			if (!renderObject)
			{
				return;
			}

			VulkanMaterial* material = &m_Materials[renderObject->materialID];

			DescriptorSetCreateInfo createInfo = {};
			createInfo.descriptorSet = &renderObject->descriptorSet;
			createInfo.descriptorSetLayout = &m_DescriptorSetLayouts[material->descriptorSetLayoutIndex];
			createInfo.shaderID = material->material.shaderID;
			createInfo.uniformBuffer = &m_Shaders[m_Materials[renderObject->materialID].material.shaderID].uniformBuffer;
			createInfo.normalTexture = material->normalTexture;
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
				createInfo.frameBufferViews.emplace_back(U_FB_0_SAMPLER + i, (VkImageView*)material->material.frameBuffers[i].second);
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

			if (*createInfo->descriptorSet)
			{
				vkFreeDescriptorSets(m_VulkanDevice->m_LogicalDevice, m_DescriptorPool, 1, createInfo->descriptorSet);
			}
			// TODO: Optimization: Allocate all required descriptor sets in one call rather than 1 at a time
			VK_CHECK_RESULT(vkAllocateDescriptorSets(m_VulkanDevice->m_LogicalDevice, &allocInfo, createInfo->descriptorSet));

			Uniforms constantBufferUniforms = m_Shaders[createInfo->shaderID].shader.constantBufferUniforms;
			Uniforms dynamicBufferUniforms = m_Shaders[createInfo->shaderID].shader.dynamicBufferUniforms;

			struct DescriptorSetInfo
			{
				DescriptorSetInfo(u64 uniform, VkDescriptorType descriptorType, VkBuffer buffer, VkDeviceSize bufferSize, VkImageView imageView = VK_NULL_HANDLE, VkSampler imageSampler = VK_NULL_HANDLE, VkDescriptorImageInfo* imageInfoPtr = nullptr) :
					uniform(uniform),
					descriptorType(descriptorType),
					buffer(buffer),
					bufferSize(bufferSize),
					imageView(imageView),
					imageSampler(imageSampler),
					imageInfoPtr(imageInfoPtr)
				{
				}

				u64 uniform;
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
				{ U_UNIFORM_BUFFER_CONSTANT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				createInfo->uniformBuffer->constantBuffer.m_Buffer, sizeof(VulkanUniformBufferObjectData) },

				{ U_UNIFORM_BUFFER_DYNAMIC, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				createInfo->uniformBuffer->dynamicBuffer.m_Buffer, sizeof(VulkanUniformBufferObjectData) * m_RenderObjects.size() },

				{ U_ALBEDO_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->albedoTexture ? *&createInfo->albedoTexture->imageView : VK_NULL_HANDLE,
				createInfo->albedoTexture ? *&createInfo->albedoTexture->sampler : VK_NULL_HANDLE,
				createInfo->albedoTexture ? &createInfo->albedoTexture->imageInfoDescriptor : nullptr },

				{ U_METALLIC_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->metallicTexture ? *&createInfo->metallicTexture->imageView : VK_NULL_HANDLE,
				createInfo->metallicTexture ? *&createInfo->metallicTexture->sampler : VK_NULL_HANDLE,
				createInfo->metallicTexture ? &createInfo->metallicTexture->imageInfoDescriptor : nullptr },

				{ U_METALLIC_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->roughnessTexture ? *&createInfo->roughnessTexture->imageView : VK_NULL_HANDLE,
				createInfo->roughnessTexture ? *&createInfo->roughnessTexture->sampler : VK_NULL_HANDLE,
				createInfo->roughnessTexture ? &createInfo->roughnessTexture->imageInfoDescriptor : nullptr },

				{ U_AO_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->aoTexture ? *&createInfo->aoTexture->imageView : VK_NULL_HANDLE,
				createInfo->aoTexture ? *&createInfo->aoTexture->sampler : VK_NULL_HANDLE,
				createInfo->aoTexture ? &createInfo->aoTexture->imageInfoDescriptor : nullptr },

				{ U_NORMAL_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->normalTexture ? *&createInfo->normalTexture->imageView : VK_NULL_HANDLE,
				createInfo->normalTexture ? *&createInfo->normalTexture->sampler : VK_NULL_HANDLE,
				createInfo->normalTexture ? &createInfo->normalTexture->imageInfoDescriptor : nullptr },

				{ U_HDR_EQUIRECTANGULAR_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->hdrEquirectangularTexture ? *&createInfo->hdrEquirectangularTexture->imageView : VK_NULL_HANDLE,
				createInfo->hdrEquirectangularTexture ? *&createInfo->hdrEquirectangularTexture->sampler : VK_NULL_HANDLE,
				createInfo->hdrEquirectangularTexture ? &createInfo->hdrEquirectangularTexture->imageInfoDescriptor : nullptr },

				{ U_CUBEMAP_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->cubemapTexture ? *&createInfo->cubemapTexture->imageView : VK_NULL_HANDLE,
				createInfo->cubemapTexture ? *&createInfo->cubemapTexture->sampler : VK_NULL_HANDLE,
				createInfo->cubemapTexture ? &createInfo->cubemapTexture->imageInfoDescriptor : nullptr },

				{ U_BRDF_LUT_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->brdfLUT ? *&createInfo->brdfLUT->imageView : VK_NULL_HANDLE,
				createInfo->brdfLUT ? *&createInfo->brdfLUT->sampler : VK_NULL_HANDLE,
				createInfo->brdfLUT ? &createInfo->brdfLUT->imageInfoDescriptor : nullptr },

				{ U_IRRADIANCE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->irradianceTexture ? *&createInfo->irradianceTexture->imageView : VK_NULL_HANDLE,
				createInfo->irradianceTexture ? *&createInfo->irradianceTexture->sampler : VK_NULL_HANDLE,
				createInfo->irradianceTexture ? &createInfo->irradianceTexture->imageInfoDescriptor : nullptr },

				{ U_PREFILTER_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->prefilterTexture ? *&createInfo->prefilterTexture->imageView : VK_NULL_HANDLE,
				createInfo->prefilterTexture ? *&createInfo->prefilterTexture->sampler : VK_NULL_HANDLE,
				createInfo->prefilterTexture ? &createInfo->prefilterTexture->imageInfoDescriptor : nullptr },

				{ U_HIGH_RES_TEX, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->albedoTexture ? *&createInfo->albedoTexture->imageView : VK_NULL_HANDLE,
				createInfo->albedoTexture ? *&createInfo->albedoTexture->sampler : VK_NULL_HANDLE,
				createInfo->albedoTexture ? &createInfo->albedoTexture->imageInfoDescriptor : nullptr },
			};


			for (auto& frameBufferViewPair : createInfo->frameBufferViews)
			{
				descriptorSets.emplace_back(
					frameBufferViewPair.first, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					VK_NULL_HANDLE, 0,
					frameBufferViewPair.second ? *frameBufferViewPair.second : VK_NULL_HANDLE,
					m_ColorSampler
				);
			}

			std::vector<VkWriteDescriptorSet> writeDescriptorSets;
			writeDescriptorSets.reserve(descriptorSets.size());

			u32 descriptorSetIndex = 0;
			u32 binding = 0;

			for (DescriptorSetInfo& descriptorSetInfo : descriptorSets)
			{
				if (constantBufferUniforms.HasUniform(descriptorSetInfo.uniform) ||
					dynamicBufferUniforms.HasUniform(descriptorSetInfo.uniform))
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
				u64 uniform;
				VkDescriptorType descriptorType;
				VkShaderStageFlags shaderStageFlags;
			};

			static DescriptorSetInfo descriptorSets[] = {
				{ U_UNIFORM_BUFFER_CONSTANT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_UNIFORM_BUFFER_DYNAMIC, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_ALBEDO_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_METALLIC_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_ROUGHNESS_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_AO_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
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

				{ U_FB_2_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ U_HIGH_RES_TEX, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },
			};

			std::vector<VkDescriptorSetLayoutBinding> bindings;

			for (DescriptorSetInfo& descSetInfo : descriptorSets)
			{
				if (shader->shader.constantBufferUniforms.HasUniform(descSetInfo.uniform) ||
					shader->shader.dynamicBufferUniforms.HasUniform(descSetInfo.uniform))
				{
					VkDescriptorSetLayoutBinding descSetLayoutBinding = {};
					descSetLayoutBinding.binding = bindings.size();
					descSetLayoutBinding.descriptorCount = 1;
					descSetLayoutBinding.descriptorType = descSetInfo.descriptorType;
					descSetLayoutBinding.stageFlags = descSetInfo.shaderStageFlags;
					bindings.push_back(descSetLayoutBinding);
				}
			}

			VkDescriptorSetLayoutCreateInfo layoutInfo = {};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = bindings.size();
			layoutInfo.pBindings = bindings.data();

			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_VulkanDevice->m_LogicalDevice, &layoutInfo, nullptr, descriptorSetLayout));
		}

		bool VulkanRenderer::GetMaterialID(const std::string& materialName, MaterialID& materialID)
		{
			for (size_t i = 0; i < m_Materials.size(); ++i)
			{
				if (m_Materials[i].material.name.compare(materialName) == 0)
				{
					materialID = i;
					return true;
				}
			}

			for (auto& materialObj : BaseScene::s_ParsedMaterials)
			{
				if (materialObj.GetString("name").compare(materialName) == 0)
				{
					// Material exists in library, but hasn't been initialized yet
					MaterialCreateInfo matCreateInfo = {};
					Material::ParseJSONObject(materialObj, matCreateInfo);

					materialID = InitializeMaterial(&matCreateInfo);
					g_SceneManager->CurrentScene()->AddMaterialID(materialID);

					return true;
				}
			}

			return false;
		}

		MaterialID VulkanRenderer::GetMaterialID(RenderID renderID)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (renderObject != nullptr)
			{
				return renderObject->materialID;
			}
			return InvalidMaterialID;
		}

		std::vector<Pair<std::string, MaterialID>> VulkanRenderer::GetValidMaterialNames() const
		{
			std::vector<Pair<std::string, MaterialID>> result;

			for (auto& matPair : m_Materials)
			{
				if (!matPair.second.material.engineMaterial)
				{
					result.emplace_back(matPair.second.material.name, matPair.first);
				}
			}

			return result;
		}

		bool VulkanRenderer::GetShaderID(const std::string& shaderName, ShaderID& shaderID)
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
				if (!filePath.empty() && filePath.compare(vulkanTexture->relativeFilePath) == 0)
				{
					return vulkanTexture;
				}
			}

			return nullptr;
		}

		void VulkanRenderer::CreateGraphicsPipeline(RenderID renderID, bool bSetCubemapRenderPass)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject || !renderObject->vertexBufferData)
			{
				return;
			}

			VulkanMaterial* material = &m_Materials[renderObject->materialID];
			VulkanShader& shader = m_Shaders[material->material.shaderID];

			GraphicsPipelineCreateInfo pipelineCreateInfo = {};
			pipelineCreateInfo.shaderID = material->material.shaderID;
			pipelineCreateInfo.vertexAttributes = shader.shader.vertexAttributes;
			pipelineCreateInfo.topology = renderObject->topology;
			pipelineCreateInfo.cullMode = renderObject->cullMode;
			pipelineCreateInfo.descriptorSetLayoutIndex = material->descriptorSetLayoutIndex;
			pipelineCreateInfo.setDynamicStates = false;
			pipelineCreateInfo.enabledColorBlending = shader.shader.translucent;
			pipelineCreateInfo.pipelineLayout = renderObject->pipelineLayout.replace();
			pipelineCreateInfo.grahpicsPipeline = renderObject->graphicsPipeline.replace();
			pipelineCreateInfo.subpass = shader.shader.subpass;
			pipelineCreateInfo.depthWriteEnable = shader.shader.depthWriteEnable ? VK_TRUE : VK_FALSE;
			if (!material->material.renderToCubemap)
			{
				pipelineCreateInfo.renderPass = m_DeferredCombineRenderPass;
			}
			else if (bSetCubemapRenderPass)
			{
				//ShaderID cubemapGBufferShaderID = m_Materials[m_CubemapGBufferMaterialID].material.shaderID;
				pipelineCreateInfo.renderPass = m_CubemapFrameBuffer->renderPass;
				//i32 cubemapGBufferShaderSubpass = m_Shaders[cubemapGBufferShaderID].shader.subpass;
				//pipelineCreateInfo.shaderID = cubemapGBufferShaderID;
				//pipelineCreateInfo.subpass = cubemapGBufferShaderSubpass;
			}
			else if (shader.shader.deferred)
			{
				pipelineCreateInfo.renderPass = m_OffScreenFrameBuf->renderPass;
			}
			else
			{
				pipelineCreateInfo.renderPass = m_DeferredCombineRenderPass;
			}

			VkPushConstantRange pushConstantRange = {};
			if (m_Shaders[material->material.shaderID].shader.bNeedPushConstantBlock)
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
				PrintError("Failed to compile vertex shader located at: %s\n", shader.shader.vertexShaderFilePath.c_str());
			}

			VDeleter<VkShaderModule> fragShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(shader.shader.fragmentShaderCode, fragShaderModule))
			{
				PrintError("Failed to compile fragment shader located at: %s\n", shader.shader.fragmentShaderFilePath.c_str());
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

			const u32 vertexStride = CalculateVertexStride(createInfo->vertexAttributes);
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
			viewport.width = (real)m_SwapChainExtent.width;
			viewport.height = (real)m_SwapChainExtent.height;
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
					colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
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
				vkDestroyPipelineCache(m_VulkanDevice->m_LogicalDevice, *createInfo->pipelineCache, nullptr);

				VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
				pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
				VK_CHECK_RESULT(vkCreatePipelineCache(m_VulkanDevice->m_LogicalDevice, &pipelineCacheCreateInfo, nullptr, createInfo->pipelineCache));
			}

			// TODO: Wrap this pipeline layout in a VDeleter so we can just call .replace()
			vkDestroyPipelineLayout(m_VulkanDevice->m_LogicalDevice, *createInfo->pipelineLayout, nullptr);
			VK_CHECK_RESULT(vkCreatePipelineLayout(m_VulkanDevice->m_LogicalDevice, &pipelineLayoutInfo, nullptr, createInfo->pipelineLayout));

			VkPipelineDepthStencilStateCreateInfo depthStencil = {};
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

			// TODO: Wrap this pipeline in a VDeleter so we can just call .replace()
			//vkDestroyPipeline(m_VulkanDevice->m_LogicalDevice, *createInfo->grahpicsPipeline, nullptr);
			VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_VulkanDevice->m_LogicalDevice, pipelineCache, 1, &pipelineInfo, nullptr, createInfo->grahpicsPipeline));
		}

		void VulkanRenderer::CreateDepthResources()
		{
			VkFormat depthFormat;
			GetSupportedDepthFormat(m_VulkanDevice->m_PhysicalDevice, &depthFormat);

			m_DepthAttachment->format = depthFormat;

			// Swapchain images
			VulkanTexture::ImageCreateInfo depthImageCreateInfo = {};
			depthImageCreateInfo.image = m_DepthAttachment->image.replace();
			depthImageCreateInfo.imageMemory = m_DepthAttachment->mem.replace();
			depthImageCreateInfo.width = m_SwapChainExtent.width;
			depthImageCreateInfo.height = m_SwapChainExtent.height;
			depthImageCreateInfo.format = depthFormat;
			depthImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			depthImageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			depthImageCreateInfo.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			depthImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
			VulkanTexture::CreateImage(m_VulkanDevice, m_GraphicsQueue, depthImageCreateInfo);

			VulkanTexture::ImageViewCreateInfo depthImageViewCreateInfo = {};
			depthImageViewCreateInfo.image = &m_DepthAttachment->image;
			depthImageViewCreateInfo.imageView = m_DepthAttachment->view.replace();
			depthImageViewCreateInfo.aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
			depthImageViewCreateInfo.format = depthFormat;
			VulkanTexture::CreateImageView(m_VulkanDevice, depthImageViewCreateInfo);

			TransitionImageLayout(m_VulkanDevice, m_GraphicsQueue, m_DepthAttachment->image, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1);


			// Cubemap depth
			VulkanTexture::ImageCreateInfo cubemapDepthImageCreateInfo = {};
			cubemapDepthImageCreateInfo.image = m_CubemapDepthAttachment->image.replace();
			cubemapDepthImageCreateInfo.imageMemory = m_CubemapDepthAttachment->mem.replace();
			cubemapDepthImageCreateInfo.width = m_CubemapFrameBuffer->width;
			cubemapDepthImageCreateInfo.height = m_CubemapFrameBuffer->height;
			cubemapDepthImageCreateInfo.format = depthFormat;
			cubemapDepthImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			cubemapDepthImageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			cubemapDepthImageCreateInfo.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			cubemapDepthImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
			cubemapDepthImageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
			cubemapDepthImageCreateInfo.arrayLayers = 6;
			VulkanTexture::CreateImage(m_VulkanDevice, m_GraphicsQueue, cubemapDepthImageCreateInfo);

			VulkanTexture::ImageViewCreateInfo cubemapDepthImageViewCreateInfo = {};
			cubemapDepthImageViewCreateInfo.image = &m_CubemapDepthAttachment->image;
			cubemapDepthImageViewCreateInfo.imageView = m_CubemapDepthAttachment->view.replace();
			cubemapDepthImageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
			cubemapDepthImageViewCreateInfo.aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
			cubemapDepthImageViewCreateInfo.layerCount = 6;
			cubemapDepthImageViewCreateInfo.format = depthFormat;
			VulkanTexture::CreateImageView(m_VulkanDevice, cubemapDepthImageViewCreateInfo);

			TransitionImageLayout(m_VulkanDevice, m_GraphicsQueue, m_CubemapDepthAttachment->image, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1);
		}

		void VulkanRenderer::CreateFramebuffers()
		{
			//
			// Swapchain frame buffers
			//
			m_SwapChainFramebuffers.resize(m_SwapChainImageViews.size(), VDeleter<VkFramebuffer>{ m_VulkanDevice->m_LogicalDevice, vkDestroyFramebuffer });

			for (size_t i = 0; i < m_SwapChainImageViews.size(); ++i)
			{
				std::array<VkImageView, 2> attachments = {
					m_SwapChainImageViews[i],
					m_DepthAttachment->view
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

		void VulkanRenderer::PrepareOffscreenFrameBuffer()
		{
			// TODO: This should be setting up the m_SwapChainFrameBuffers?

			m_OffScreenFrameBuf->width = m_SwapChainExtent.width;
			m_OffScreenFrameBuf->height = m_SwapChainExtent.height;

			// Does *not* include depth attachment
			const size_t frameBufferColorAttachmentCount = m_OffScreenFrameBuf->frameBufferAttachments.size();

			// Color attachments
			for (u32 i = 0; i < frameBufferColorAttachmentCount; ++i)
			{
				FrameBufferAttachment& frameBufferAttachment = m_OffScreenFrameBuf->frameBufferAttachments[i].second;
				CreateAttachment(
					m_VulkanDevice,
					frameBufferAttachment.format,
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
					m_OffScreenFrameBuf->width,
					m_OffScreenFrameBuf->height,
					1,
					VK_IMAGE_VIEW_TYPE_2D,
					0,
					&frameBufferAttachment);
			}

			// Depth attachment

			// Find a suitable depth format
			VkFormat attDepthFormat;
			VkBool32 validDepthFormat = GetSupportedDepthFormat(m_VulkanDevice->m_PhysicalDevice, &attDepthFormat);
			assert(validDepthFormat);

			// Set up separate renderpass with references to the color and depth attachments
			std::vector<VkAttachmentDescription> attachmentDescs(frameBufferColorAttachmentCount + 1); // + 1 for depth attachment

			// Init attachment properties
			for (u32 i = 0; i < frameBufferColorAttachmentCount; ++i)
			{
				attachmentDescs[i].samples = VK_SAMPLE_COUNT_1_BIT;
				attachmentDescs[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				attachmentDescs[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				attachmentDescs[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				attachmentDescs[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				attachmentDescs[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachmentDescs[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				attachmentDescs[i].format = m_OffScreenFrameBuf->frameBufferAttachments[i].second.format;
			}
			attachmentDescs[frameBufferColorAttachmentCount].samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDescs[frameBufferColorAttachmentCount].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachmentDescs[frameBufferColorAttachmentCount].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescs[frameBufferColorAttachmentCount].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescs[frameBufferColorAttachmentCount].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDescs[frameBufferColorAttachmentCount].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachmentDescs[frameBufferColorAttachmentCount].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			attachmentDescs[frameBufferColorAttachmentCount].format = m_DepthAttachment->format;


			std::vector<VkAttachmentReference> colorReferences;
			for (u32 i = 0; i < frameBufferColorAttachmentCount; ++i)
			{
				colorReferences.push_back({ i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
			}

			VkAttachmentReference depthReference = {};
			depthReference.attachment = attachmentDescs.size() - 1;
			depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.pColorAttachments = colorReferences.data();
			subpass.colorAttachmentCount = static_cast<u32>(colorReferences.size());
			subpass.pDepthStencilAttachment = &depthReference;

			// Use subpass dependencies for attachment layout transitions
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
			renderPassInfo.attachmentCount = static_cast<u32>(attachmentDescs.size());
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = dependencies.size();
			renderPassInfo.pDependencies = dependencies.data();

			vkDestroyRenderPass(m_VulkanDevice->m_LogicalDevice, m_OffScreenFrameBuf->renderPass, nullptr);
			VK_CHECK_RESULT(vkCreateRenderPass(m_VulkanDevice->m_LogicalDevice, &renderPassInfo, nullptr, &m_OffScreenFrameBuf->renderPass));

			std::vector<VkImageView> attachments;
			for (u32 i = 0; i < frameBufferColorAttachmentCount; ++i)
			{
				attachments.push_back(m_OffScreenFrameBuf->frameBufferAttachments[i].second.view);
			}
			attachments.push_back(m_DepthAttachment->view);

			VkFramebufferCreateInfo fbufCreateInfo = {};
			fbufCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbufCreateInfo.pNext = NULL;
			fbufCreateInfo.renderPass = m_OffScreenFrameBuf->renderPass;
			fbufCreateInfo.pAttachments = attachments.data();
			fbufCreateInfo.attachmentCount = static_cast<u32>(attachments.size());
			fbufCreateInfo.width = m_OffScreenFrameBuf->width;
			fbufCreateInfo.height = m_OffScreenFrameBuf->height;
			fbufCreateInfo.layers = 1;
			VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &fbufCreateInfo, nullptr, m_OffScreenFrameBuf->frameBuffer.replace()));

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
			samplerCreateInfo.minLod = 0.0f;
			samplerCreateInfo.maxLod = 1.0f;
			samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			VK_CHECK_RESULT(vkCreateSampler(m_VulkanDevice->m_LogicalDevice, &samplerCreateInfo, nullptr, m_ColorSampler.replace()));
		}

		void VulkanRenderer::PrepareCubemapFrameBuffer()
		{
			// Does *not* include depth attachment
			const size_t frameBufferColorAttachmentCount = m_CubemapFrameBuffer->frameBufferAttachments.size();

			// Color attachments
			for (u32 i = 0; i < frameBufferColorAttachmentCount; ++i)
			{
				FrameBufferAttachment& frameBufferAttachment = m_CubemapFrameBuffer->frameBufferAttachments[i].second;
				CreateAttachment(
					m_VulkanDevice,
					frameBufferAttachment.format,
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
					m_CubemapFrameBuffer->width,
					m_CubemapFrameBuffer->height,
					6,
					VK_IMAGE_VIEW_TYPE_CUBE,
					VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
					&frameBufferAttachment);
			}

			// Depth attachment

			// Find a suitable depth format
			VkFormat attDepthFormat;
			VkBool32 validDepthFormat = GetSupportedDepthFormat(m_VulkanDevice->m_PhysicalDevice, &attDepthFormat);
			assert(validDepthFormat);

			// Set up separate renderpass with references to the color and depth attachments
			std::vector<VkAttachmentDescription> attachmentDescs(frameBufferColorAttachmentCount + 1); // + 1 for depth attachment
			// Init attachment properties
			for (u32 i = 0; i < frameBufferColorAttachmentCount; ++i)
			{
				attachmentDescs[i].samples = VK_SAMPLE_COUNT_1_BIT;
				attachmentDescs[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				attachmentDescs[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				attachmentDescs[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				attachmentDescs[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				attachmentDescs[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachmentDescs[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				attachmentDescs[i].format = m_CubemapFrameBuffer->frameBufferAttachments[i].second.format;
			}
			attachmentDescs[frameBufferColorAttachmentCount].samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDescs[frameBufferColorAttachmentCount].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachmentDescs[frameBufferColorAttachmentCount].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescs[frameBufferColorAttachmentCount].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescs[frameBufferColorAttachmentCount].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDescs[frameBufferColorAttachmentCount].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachmentDescs[frameBufferColorAttachmentCount].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			attachmentDescs[frameBufferColorAttachmentCount].format = m_CubemapDepthAttachment->format;


			std::vector<VkAttachmentReference> colorReferences;
			for (u32 i = 0; i < frameBufferColorAttachmentCount; ++i)
			{
				colorReferences.push_back({ i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
			}

			VkAttachmentReference depthReference = {};
			depthReference.attachment = attachmentDescs.size() - 1;
			depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			std::array<VkSubpassDescription, 2> subpasses;
			// Deferred subpass
			subpasses[0] = {};
			subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpasses[0].colorAttachmentCount = colorReferences.size();
			subpasses[0].pColorAttachments = colorReferences.data();
			subpasses[0].pDepthStencilAttachment = &depthReference;

			// Forward subpass
			subpasses[1] = {};
			subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpasses[1].colorAttachmentCount = 1;
			subpasses[1].pColorAttachments = colorReferences.data();
			subpasses[1].pDepthStencilAttachment = &depthReference;

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

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.pAttachments = attachmentDescs.data();
			renderPassInfo.attachmentCount = static_cast<u32>(attachmentDescs.size());
			renderPassInfo.subpassCount = subpasses.size();
			renderPassInfo.pSubpasses = subpasses.data();
			renderPassInfo.dependencyCount = dependencies.size();
			renderPassInfo.pDependencies = dependencies.data();

			VK_CHECK_RESULT(vkCreateRenderPass(m_VulkanDevice->m_LogicalDevice, &renderPassInfo, nullptr, m_CubemapFrameBuffer->renderPass.replace()));

			std::vector<VkImageView> attachments;
			for (u32 i = 0; i < frameBufferColorAttachmentCount; ++i)
			{
				attachments.push_back(m_CubemapFrameBuffer->frameBufferAttachments[i].second.view);
			}
			attachments.push_back(m_CubemapDepthAttachment->view);

			VkFramebufferCreateInfo fbufCreateInfo = {};
			fbufCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbufCreateInfo.pNext = NULL;
			fbufCreateInfo.renderPass = m_CubemapFrameBuffer->renderPass;
			fbufCreateInfo.pAttachments = attachments.data();
			fbufCreateInfo.attachmentCount = static_cast<u32>(attachments.size());
			fbufCreateInfo.width = m_CubemapFrameBuffer->width;
			fbufCreateInfo.height = m_CubemapFrameBuffer->height;
			fbufCreateInfo.layers = 6;
			VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &fbufCreateInfo, nullptr, m_CubemapFrameBuffer->frameBuffer.replace()));

			//// Create sampler to sample from the color attachments
			//VkSamplerCreateInfo samplerCreateInfo = {};
			//samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			//samplerCreateInfo.maxAnisotropy = 1.0f;
			//samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
			//samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
			//samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			//samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			//samplerCreateInfo.addressModeV = samplerCreateInfo.addressModeU;
			//samplerCreateInfo.addressModeW = samplerCreateInfo.addressModeU;
			//samplerCreateInfo.mipLodBias = 0.0f;
			//samplerCreateInfo.minLod = 0.0f;
			//samplerCreateInfo.maxLod = 1.0f;
			//samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			//VK_CHECK_RESULT(vkCreateSampler(m_VulkanDevice->m_LogicalDevice, &samplerCreateInfo, nullptr, m_ColorSampler.replace()));
		}

		void VulkanRenderer::GenerateGBufferVertexBuffer()
		{
			if (m_gBufferQuadVertexBufferData.vertexData == nullptr)
			{
				// TODO: Use full screen tri (see GLRenderer::GenerateGBufferVertexBuffer)
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
				gBufferQuadVertexBufferDataCreateInfo.attributes = (u32)VertexAttribute::POSITION | (u32)VertexAttribute::UV;
				m_gBufferQuadVertexBufferData.Initialize(&gBufferQuadVertexBufferDataCreateInfo);
			}
		}

		// TODO: Unify with GLRenderer
		void VulkanRenderer::GenerateGBuffer()
		{
			if (m_gBufferQuadVertexBufferData.vertexData == nullptr)
			{
				GenerateGBufferVertexBuffer();
			}

			assert(m_SkyBoxMesh != nullptr);
			MaterialID skyboxMaterialID = m_SkyBoxMesh->GetMeshComponent()->GetMaterialID();

			const std::string gBufferMatName = "GBuffer material";
			const std::string gBufferCubeMatName = "GBuffer cubemap material";
			const std::string gBufferQuadName = "GBuffer quad";

			// Remove existing material if present (this will be true when reloading the scene)
			{
				MaterialID existingGBufferQuadMatID = InvalidMaterialID;
				MaterialID existingGBufferCubeMatID = InvalidMaterialID;
				// TODO: Don't rely on material names!
				if (GetMaterialID(gBufferMatName, existingGBufferQuadMatID))
				{
					RemoveMaterial(existingGBufferQuadMatID);
				}
				if (GetMaterialID(gBufferCubeMatName, existingGBufferCubeMatID))
				{
					RemoveMaterial(existingGBufferCubeMatID);
				}

				for (auto iter = m_PersistentObjects.begin(); iter != m_PersistentObjects.end(); ++iter)
				{
					GameObject* gameObject = *iter;
					if (gameObject->GetName().compare(gBufferQuadName) == 0)
					{
						SafeDelete(gameObject);
						m_PersistentObjects.erase(iter);
						break;
					}
				}

				if (m_GBufferQuadRenderID != InvalidRenderID)
				{
					DestroyRenderObject(m_GBufferQuadRenderID);
				}
			}

			{
				MaterialCreateInfo gBufferMaterialCreateInfo = {};
				gBufferMaterialCreateInfo.name = gBufferMatName;
				gBufferMaterialCreateInfo.shaderName = "deferred_combine";
				gBufferMaterialCreateInfo.enableIrradianceSampler = true;
				gBufferMaterialCreateInfo.irradianceSamplerMatID = skyboxMaterialID;
				gBufferMaterialCreateInfo.enablePrefilteredMap = true;
				gBufferMaterialCreateInfo.prefilterMapSamplerMatID = skyboxMaterialID;
				gBufferMaterialCreateInfo.enableBRDFLUT = true;
				gBufferMaterialCreateInfo.renderToCubemap = false;
				gBufferMaterialCreateInfo.engineMaterial = true;
				for (const auto& frameBufferAttachment : m_OffScreenFrameBuf->frameBufferAttachments)
				{
					gBufferMaterialCreateInfo.frameBuffers.emplace_back(
						frameBufferAttachment.first,
						(void*)&frameBufferAttachment.second.view
					);
				}

				MaterialID gBufferMatID = InitializeMaterial(&gBufferMaterialCreateInfo);


				GameObject* gBufferQuadGameObject = new GameObject(gBufferQuadName, GameObjectType::_NONE);
				m_PersistentObjects.push_back(gBufferQuadGameObject);
				// NOTE: G-buffer isn't rendered normally, it is handled separately
				gBufferQuadGameObject->SetVisible(false);

				RenderObjectCreateInfo gBufferQuadCreateInfo = {};
				gBufferQuadCreateInfo.materialID = gBufferMatID;
				gBufferQuadCreateInfo.gameObject = gBufferQuadGameObject;
				gBufferQuadCreateInfo.vertexBufferData = &m_gBufferQuadVertexBufferData;
				gBufferQuadCreateInfo.cullFace = CullFace::NONE;
				gBufferQuadCreateInfo.visibleInSceneExplorer = false;

				m_gBufferQuadIndices = { 0, 1, 2,  2, 1, 3 };
				gBufferQuadCreateInfo.indices = &m_gBufferQuadIndices;

				m_GBufferQuadRenderID = InitializeRenderObject(&gBufferQuadCreateInfo);

				m_gBufferQuadVertexBufferData.DescribeShaderVariables(this, m_GBufferQuadRenderID);
			}

			// Initialize GBuffer cubemap material & mesh
			{
				MaterialCreateInfo gBufferCubemapMaterialCreateInfo = {};
				gBufferCubemapMaterialCreateInfo.name = gBufferCubeMatName;
				gBufferCubemapMaterialCreateInfo.shaderName = "deferred_combine_cubemap";
				gBufferCubemapMaterialCreateInfo.enableIrradianceSampler = true;
				gBufferCubemapMaterialCreateInfo.irradianceSamplerMatID = skyboxMaterialID;
				gBufferCubemapMaterialCreateInfo.enablePrefilteredMap = true;
				gBufferCubemapMaterialCreateInfo.prefilterMapSamplerMatID = skyboxMaterialID;
				gBufferCubemapMaterialCreateInfo.enableBRDFLUT = true;
				gBufferCubemapMaterialCreateInfo.renderToCubemap = false;
				gBufferCubemapMaterialCreateInfo.engineMaterial = true;
				for (const auto& frameBufferAttachment : m_OffScreenFrameBuf->frameBufferAttachments)
				{
					gBufferCubemapMaterialCreateInfo.frameBuffers.emplace_back(
						frameBufferAttachment.first,
						(void*)&frameBufferAttachment.second.view
					);
				}

				m_CubemapGBufferMaterialID = InitializeMaterial(&gBufferCubemapMaterialCreateInfo);
			}
		}

		void VulkanRenderer::RemoveMaterial(MaterialID materialID)
		{
			assert(materialID != InvalidMaterialID);

			m_Materials.erase(materialID);
		}

		void VulkanRenderer::PhysicsDebugRender()
		{
			btDiscreteDynamicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
			physicsWorld->debugDrawWorld();
		}

		void VulkanRenderer::BuildCommandBuffers(const DrawCallInfo& drawCallInfo)
		{
			if (drawCallInfo.bRenderToCubemap)
			{
				if (offScreenCmdBuffer == VK_NULL_HANDLE)
				{
					offScreenCmdBuffer = m_CommandBufferManager.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
				}

				VulkanRenderObject* cubemapRenderObject = GetRenderObject(drawCallInfo.cubemapObjectRenderID);
				VulkanMaterial const & cubemapMaterial = m_Materials[cubemapRenderObject->materialID];

				std::array<VkClearValue, 4> clearValues = {};
				clearValues[0].color = m_ClearColor;
				clearValues[1].color = m_ClearColor;
				clearValues[2].color = m_ClearColor;
				clearValues[3].depthStencil = { 1.0f, 0 };

				VkRenderPassBeginInfo renderPassBeginInfo = {};
				renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				renderPassBeginInfo.renderPass = m_CubemapFrameBuffer->renderPass;
				renderPassBeginInfo.renderArea.offset = { 0, 0 };
				renderPassBeginInfo.renderArea.extent = {
					(uint32_t)cubemapMaterial.material.cubemapSamplerSize.x,
					(uint32_t)cubemapMaterial.material.cubemapSamplerSize.y
				};
				renderPassBeginInfo.clearValueCount = clearValues.size();
				renderPassBeginInfo.pClearValues = clearValues.data();

				VkCommandBuffer& commandBuffer = offScreenCmdBuffer;

				renderPassBeginInfo.framebuffer = m_CubemapFrameBuffer->frameBuffer;

				VkCommandBufferBeginInfo cmdBufferbeginInfo = {};
				cmdBufferbeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				cmdBufferbeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

				VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &cmdBufferbeginInfo));

				vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport = VkViewport{
					0.0f, 1.0f,
					(real)cubemapMaterial.material.cubemapSamplerSize.x,
					(real)cubemapMaterial.material.cubemapSamplerSize.y,
					0.1f, 1000.0f
				};
				vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

				VkRect2D scissor = VkRect2D{
					{ 0u, 0u },
					{ (uint32_t)cubemapMaterial.material.cubemapSamplerSize.x,
					  (uint32_t)cubemapMaterial.material.cubemapSamplerSize.y }
				};
				vkCmdSetScissor(commandBuffer, 0, 1, &scissor);


				//BindDescriptorSet(&m_Shaders[gBufferMaterial->material.shaderID], gBufferObject->renderID, commandBuffer, gBufferObject->pipelineLayout, gBufferObject->descriptorSet);

				//// Final composition as full screen quad (deferred combine)
				//vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gBufferObject->graphicsPipeline);
				//VkDeviceSize offsets[1] = { 0 };
				//vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_VertexIndexBufferPairs[gBufferMaterial->material.shaderID].vertexBuffer->m_Buffer, offsets);
				//vkCmdBindIndexBuffer(commandBuffer, m_VertexIndexBufferPairs[gBufferMaterial->material.shaderID].indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);

				//vkCmdDrawIndexed(commandBuffer, m_VertexIndexBufferPairs[gBufferMaterial->material.shaderID].indexCount, 1, 0, 0, 1);


				vkCmdNextSubpass(commandBuffer, VK_SUBPASS_CONTENTS_INLINE);

				// Forward rendered objects

				for (size_t j = 0; j < m_RenderObjects.size(); ++j)
				{
					VulkanRenderObject* renderObject = GetRenderObject(j);
					if (!renderObject ||
						!renderObject->gameObject->IsVisible() ||
						!renderObject->vertexBufferData ||
						renderObject->vertexBufferData->VertexCount == 0)
					{
						continue;
					}

					VulkanMaterial& renderObjectMat = m_Materials[renderObject->materialID];
					VulkanShader& renderObjectShader = m_Shaders[renderObjectMat.material.shaderID];

					// Only render non-deferred (forward) objects in this pass
					if (renderObjectShader.shader.deferred)
					{
						continue;
					}

					VulkanBuffer* vertBuffer = m_VertexIndexBufferPairs[renderObjectMat.material.shaderID].vertexBuffer;
					VulkanBuffer* indexBuffer = m_VertexIndexBufferPairs[renderObjectMat.material.shaderID].indexBuffer;

					if (vertBuffer->m_Buffer == 0 ||
						(renderObject->bIndexed && indexBuffer->m_Buffer == 0))
					{
						// Invalid object! Might not contain valid material
						continue;
					}

					VkDeviceSize offsets[1] = { 0 };
					vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertBuffer->m_Buffer, offsets);

					if (renderObject->bIndexed)
					{
						vkCmdBindIndexBuffer(commandBuffer, indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
					}

					vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderObject->graphicsPipeline);

					// Push constants
					if (renderObjectShader.shader.bNeedPushConstantBlock)
					{
						// Truncate translation component to center cubemap around viewer
						glm::mat4 view = glm::mat4(glm::mat3(g_CameraManager->CurrentCamera()->GetView()));
						glm::mat4 projection = g_CameraManager->CurrentCamera()->GetProjection();
						renderObjectMat.material.pushConstantBlock.mvp =
							projection * view * renderObject->gameObject->GetTransform()->GetWorldTransform();
						vkCmdPushConstants(commandBuffer, renderObject->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Material::PushConstantBlock), &renderObjectMat.material.pushConstantBlock);
					}

					BindDescriptorSet(&renderObjectShader, j, commandBuffer, renderObject->pipelineLayout, renderObject->descriptorSet);

					if (renderObject->bIndexed)
					{
						vkCmdDrawIndexed(commandBuffer, renderObject->indices->size(), 1, renderObject->indexOffset, renderObject->vertexOffset, 0);
					}
					else
					{
						vkCmdDraw(commandBuffer, renderObject->vertexBufferData->VertexCount, 1, renderObject->vertexOffset, 0);
					}
				}


				{ // Text & editor objects
					SetFont(m_FntSourceCodeProWS);
					real s = g_SecElapsedSinceProgramStart * 3.5f;
					DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(1.0f), 1.0f), glm::vec3(2.0f, 1.5f, 0.0f), QUAT_UNIT, 0.0f);
					DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(0.95f), 1.0f), glm::vec3(2.0f + cos(s * 0.3f + 0.3f * 1) * 0.05f, 1.5f + sin(s + 0.3f * 1) * 0.05f, -0.075f * 1), QUAT_UNIT, 0.0f);
					DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(0.90f), 1.0f), glm::vec3(2.0f + cos(s * 0.3f + 0.3f * 2) * 0.07f, 1.5f + sin(s + 0.3f * 2) * 0.07f, -0.075f * 2), QUAT_UNIT, 0.0f);
					DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(0.85f), 1.0f), glm::vec3(2.0f + cos(s * 0.3f + 0.3f * 3) * 0.10f, 1.5f + sin(s + 0.3f * 3) * 0.10f, -0.075f * 3), QUAT_UNIT, 0.0f);
					DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(0.80f), 1.0f), glm::vec3(2.0f + cos(s * 0.3f + 0.3f * 4) * 0.12f, 1.5f + sin(s + 0.3f * 4) * 0.12f, -0.075f * 4), QUAT_UNIT, 0.0f);
					DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(0.75f), 1.0f), glm::vec3(2.0f + cos(s * 0.3f + 0.3f * 5) * 0.15f, 1.5f + sin(s + 0.3f * 5) * 0.15f, -0.075f * 5), QUAT_UNIT, 0.0f);
					DrawStringWS("THREE DIMENSIONAL TEXT!", glm::vec4(glm::vec3(0.70f), 1.0f), glm::vec3(2.0f + cos(s * 0.3f + 0.3f * 6) * 0.17f, 1.5f + sin(s + 0.3f * 6) * 0.17f, -0.075f * 6), QUAT_UNIT, 0.0f);

					std::vector<TextVertex3D> textVerticesWS;
					UpdateTextBufferWS(textVerticesWS);
					// TODO: Update buffer with textVerticesWS

					DrawTextWS();

					bool bUsingGameplayCam = g_CameraManager->CurrentCamera()->bIsGameplayCam;
					if (g_EngineInstance->IsRenderingEditorObjects() && !bUsingGameplayCam)
					{
						// Depth aware editor objects
						// TODO:

						// Selected object wireframe
						// TODO:

						//glDepthMask(GL_TRUE);
						//glClear(GL_DEPTH_BUFFER_BIT);

						// Depth unaware objects write to a cleared depth buffer so they
						// draw on top of previous geometry but are still eclipsed by other
						// depth unaware objects

						// Depth unaware editor objects
						// TODO:
					}

					DrawScreenSpaceSprites();


					// Screen-space objects
					SetFont(m_FntSourceCodeProSS);
					static const glm::vec4 color(0.95f);
					DrawStringSS("FLEX ENGINE", color, AnchorPoint::TOP_RIGHT, glm::vec2(-0.03f, -0.05f), 0.0f);
					if (g_EngineInstance->IsSimulationPaused())
					{
						DrawStringSS("PAUSED", color, AnchorPoint::TOP_RIGHT, glm::vec2(-0.03f, -0.09f), 0.0f);
					}
					//DrawStringSS("1+/'TEST' \"TEST\"? ABCDEFGHIJKLMNOPQRSTUVWXYZ", glm::vec4(0.95f), AnchorPoint::CENTER, VEC2_ZERO, 1.5f, false);
					//DrawStringSS("#WOWIE# @LIQWIDICE FILE_NAME.ZIP * 17 (0)", glm::vec4(0.95f), AnchorPoint::CENTER, glm::vec2(0.0f, 0.1f), 1.5f, false);
					//DrawStringSS("[2+6=? M,M W.W ~`~ \\/ <A>]", glm::vec4(0.95f), AnchorPoint::CENTER, glm::vec2(0.0f, 0.2f), 1.5f, false);

					// Text stress test:
#if 0
					SetFont(m_FntSourceCodeProSS);
					real yO = -1.0f;
					std::string str;
					for (i32 i = 0; i < 5; ++i)
					{
						str = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";
						DrawStringSS(str, glm::vec4(0.95f), AnchorPoint::CENTER, glm::vec2(0.0f, yO), 3.5f);
						yO += 0.05f;
						str = std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ");
						DrawStringSS(str, glm::vec4(0.95f, 0.6f, 0.95f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, yO), 3.5f);
						yO += 0.05f;
						str = std::string("0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"");
						DrawStringSS(str, glm::vec4(0.8f, 0.9f, 0.1f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, yO), 3.5f);
						yO += 0.05f;
						str = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";
						DrawStringSS(str, glm::vec4(0.95f, 0.1f, 0.5f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, yO), 3.5f);
						yO += 0.05f;
					}

					SetFont(m_FntUbuntuCondensedSS);
					yO = 0.0f;
					for (i32 i = 0; i < 3; ++i)
					{
						str = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";
						DrawStringSS(str, glm::vec4(0.95f, 0.5f, 0.1f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, yO), 6);
						yO += 0.1f;
						str = std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ");
						DrawStringSS(str, glm::vec4(0.55f, 0.6f, 0.95f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, yO), 6);
						yO += 0.1f;
						str = std::string("0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"0123456789 -=!@#$%^&*()_+`~\\|/?<>,.*;:[]{}\'\"");
						DrawStringSS(str, glm::vec4(0.0f, 0.9f, 0.7f, 1.0f), AnchorPoint::CENTER, glm::vec2(0.0f, yO), 6);
						yO += 0.1f;
					}

					//std::string str = std::string("XYZ");
					//DrawStringSS(str, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::TOP_LEFT, VEC2_ZERO, 3, &letterYOffsetsEmpty);
					//DrawStringSS(str, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::TOP, VEC2_ZERO, 3, &letterYOffsetsEmpty);
					//DrawStringSS(str, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::TOP_RIGHT, VEC2_ZERO, 3, &letterYOffsetsEmpty);
					//DrawStringSS(str, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::RIGHT, VEC2_ZERO, 3, &letterYOffsetsEmpty);
					//DrawStringSS(str, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::BOTTOM_RIGHT, VEC2_ZERO, 3, &letterYOffsetsEmpty);
					//DrawStringSS(str, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::BOTTOM, VEC2_ZERO, 3, &letterYOffsetsEmpty);
					//DrawStringSS(str, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::BOTTOM_LEFT, VEC2_ZERO, 3, &letterYOffsetsEmpty);
					//DrawStringSS(str, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::LEFT, VEC2_ZERO, 3, &letterYOffsetsEmpty);
					//DrawStringSS(str, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::CENTER, VEC2_ZERO, 3, &letterYOffsetsEmpty);

					//std::string fxaaEnabledStr = std::string("FXAA: ") + (m_PostProcessSettings.bEnableFXAA ? "1" : "0");
					//DrawStringSS(fxaaEnabledStr, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::TOP_RIGHT, glm::vec2(-0.01f, 0.0f), 5, &letterYOffsetsEmpty);
					//glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
					//std::string resolutionStr = "Frame buffer size: " +  IntToString(frameBufferSize.x) + "x" + IntToString(frameBufferSize.y);
					//DrawStringSS(resolutionStr, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), AnchorPoint::TOP_RIGHT, glm::vec2(-0.01f, 0.04f), 5, &letterYOffsetsEmpty);
#endif

					if (m_EditorStrSecRemaining > 0.0f)
					{
						SetFont(m_FntUbuntuCondensedSS);
						real alpha = glm::clamp(m_EditorStrSecRemaining / (m_EditorStrSecDuration*m_EditorStrFadeDurationPercent),
							0.0f, 1.0f);
						DrawStringSS(m_EditorMessage, glm::vec4(1.0f, 1.0f, 1.0f, alpha), AnchorPoint::CENTER, VEC2_ZERO, 3);
					}

					std::vector<TextVertex2D> textVerticesSS;
					UpdateTextBufferSS(textVerticesSS);
					// TODO: Update buffer with textVerticesSS

					DrawTextSS();
				}

				vkCmdEndRenderPass(commandBuffer);

				VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));
			}
			else
			{
				// Not drawing to cubemap, just draw normally
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
				VulkanMaterial* gBufferMaterial = &m_Materials[gBufferObject->materialID];

				if (g_EngineInstance->IsRenderingImGui())
				{
					ImGui::Render();
				}

				for (size_t i = 0; i < m_CommandBufferManager.m_CommandBuffers.size(); ++i)
				{
					VkCommandBuffer& commandBuffer = m_CommandBufferManager.m_CommandBuffers[i];

					renderPassBeginInfo.framebuffer = m_SwapChainFramebuffers[i];

					i32 numMeshesProcessed = 0;

					VkCommandBufferBeginInfo cmdBufferbeginInfo = {};
					cmdBufferbeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
					cmdBufferbeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

					VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &cmdBufferbeginInfo));

					vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

					VkViewport viewport = VkViewport{ 0.0f, 1.0f, (real)m_SwapChainExtent.width, (real)m_SwapChainExtent.height, 0.1f, 1000.0f };
					vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

					VkRect2D scissor = VkRect2D{ { 0u, 0u },{ m_SwapChainExtent.width, m_SwapChainExtent.height } };
					vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

					BindDescriptorSet(&m_Shaders[gBufferMaterial->material.shaderID], numMeshesProcessed, commandBuffer, gBufferObject->pipelineLayout, gBufferObject->descriptorSet);
					++numMeshesProcessed;

					// Final composition as full screen quad (deferred combine)
					vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gBufferObject->graphicsPipeline);
					VkDeviceSize offsets[1] = { 0 };
					vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_VertexIndexBufferPairs[gBufferMaterial->material.shaderID].vertexBuffer->m_Buffer, offsets);
					vkCmdBindIndexBuffer(commandBuffer, m_VertexIndexBufferPairs[gBufferMaterial->material.shaderID].indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);

					vkCmdDrawIndexed(commandBuffer, gBufferObject->indices->size(), 1, 0, 0, 1);


					vkCmdNextSubpass(commandBuffer, VK_SUBPASS_CONTENTS_INLINE);
					// Forward rendered objects

					// TODO: Batch objects with same materials together like in GL renderer
					for (size_t j = 0; j < m_RenderObjects.size(); ++j)
					{
						++numMeshesProcessed;

						VulkanRenderObject* renderObject = GetRenderObject(j);
						if (!renderObject ||
							!renderObject->gameObject->IsVisible() ||
							!renderObject->vertexBufferData ||
							renderObject->vertexBufferData->VertexCount == 0)
						{
							continue;
						}

						VulkanMaterial& renderObjectMat = m_Materials[renderObject->materialID];
						VulkanShader& renderObjectShader = m_Shaders[renderObjectMat.material.shaderID];

						// Only render non-deferred (forward) objects in this pass
						if (renderObjectShader.shader.deferred)
						{
							continue;
						}

						VulkanBuffer* vertBuffer = m_VertexIndexBufferPairs[renderObjectMat.material.shaderID].vertexBuffer;
						VulkanBuffer* indexBuffer = m_VertexIndexBufferPairs[renderObjectMat.material.shaderID].indexBuffer;

						if (vertBuffer->m_Buffer == 0 ||
							(renderObject->bIndexed && indexBuffer->m_Buffer == 0))
						{
							// Invalid object! Might not contain valid material
							continue;
						}

						vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertBuffer->m_Buffer, offsets);

						if (renderObject->bIndexed)
						{
							vkCmdBindIndexBuffer(commandBuffer, indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
						}

						vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderObject->graphicsPipeline);

						// Push constants
						if (renderObjectShader.shader.bNeedPushConstantBlock)
						{
							glm::mat4 view = glm::mat4(glm::mat3(g_CameraManager->CurrentCamera()->GetView())); // Truncate translation part off to center around viewer
							glm::mat4 projection = g_CameraManager->CurrentCamera()->GetProjection();
							renderObjectMat.material.pushConstantBlock.mvp =
								projection * view * renderObject->gameObject->GetTransform()->GetWorldTransform();
							vkCmdPushConstants(commandBuffer, renderObject->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Material::PushConstantBlock), &renderObjectMat.material.pushConstantBlock);
						}

						BindDescriptorSet(&renderObjectShader, numMeshesProcessed, commandBuffer, renderObject->pipelineLayout, renderObject->descriptorSet);

						if (renderObject->bIndexed)
						{
							vkCmdDrawIndexed(commandBuffer, renderObject->indices->size(), 1, renderObject->indexOffset, renderObject->vertexOffset, 0);
						}
						else
						{
							vkCmdDraw(commandBuffer, renderObject->vertexBufferData->VertexCount, 1, renderObject->vertexOffset, 0);
						}
					}

					if (g_EngineInstance->IsRenderingImGui())
					{
						ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
					}

					vkCmdEndRenderPass(commandBuffer);

					VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));
				}
			}
		}

		void VulkanRenderer::BuildDeferredCommandBuffer(const DrawCallInfo& drawCallInfo)
		{
			// TODO: Remove unused param
			UNREFERENCED_PARAMETER(drawCallInfo);

			// TODO: Add support for cubemap rendering using m_CubemapFrameBuffer

			if (offScreenCmdBuffer == VK_NULL_HANDLE)
			{
				offScreenCmdBuffer = m_CommandBufferManager.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
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
			renderPassBeginInfo.renderPass = m_OffScreenFrameBuf->renderPass;
			renderPassBeginInfo.framebuffer = m_OffScreenFrameBuf->frameBuffer;
			renderPassBeginInfo.renderArea.offset = { 0, 0 };
			renderPassBeginInfo.renderArea.extent.width = m_OffScreenFrameBuf->width;
			renderPassBeginInfo.renderArea.extent.height = m_OffScreenFrameBuf->height;
			renderPassBeginInfo.clearValueCount = clearValues.size();
			renderPassBeginInfo.pClearValues = clearValues.data();

			VkCommandBufferBeginInfo cmdBufferbeginInfo = {};
			cmdBufferbeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			cmdBufferbeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

			VK_CHECK_RESULT(vkBeginCommandBuffer(offScreenCmdBuffer, &cmdBufferbeginInfo));

			vkCmdBeginRenderPass(offScreenCmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			// TODO: Make min and max values members
			// TODO: Move to separate function, SetupDrawState?
			VkViewport viewport = VkViewport{ 0.0f, 1.0f, (real)m_OffScreenFrameBuf->width, (real)m_OffScreenFrameBuf->height, 0.1f, 1000.0f };
			vkCmdSetViewport(offScreenCmdBuffer, 0, 1, &viewport);

			VkRect2D scissor = VkRect2D{ { 0u, 0u },{ m_OffScreenFrameBuf->width, m_OffScreenFrameBuf->height } };
			vkCmdSetScissor(offScreenCmdBuffer, 0, 1, &scissor);

			VkDeviceSize offsets[1] = { 0 };

			// TODO: Batch objects with same materials together like in GL renderer
			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				VulkanRenderObject* renderObject = GetRenderObject(i);
				if (!renderObject ||
					!renderObject->gameObject->IsVisible() ||
					!renderObject->vertexBufferData ||
					renderObject->vertexBufferData->VertexCount == 0)
				{
					continue;
				}

				VulkanMaterial& renderObjectMat = m_Materials[renderObject->materialID];
				VulkanShader& renderObjectShader = m_Shaders[renderObjectMat.material.shaderID];

				// Only render deferred objects in this pass
				if (!renderObjectShader.shader.deferred)
				{
					continue;
				}

				VulkanBuffer* vertBuffer = m_VertexIndexBufferPairs[renderObjectMat.material.shaderID].vertexBuffer;
				VulkanBuffer* indexBuffer = m_VertexIndexBufferPairs[renderObjectMat.material.shaderID].indexBuffer;

				if (vertBuffer->m_Buffer == 0 ||
					(renderObject->bIndexed && indexBuffer->m_Buffer == 0))
				{
					// Invalid object! Might not contain valid material
					continue;
				}

				vkCmdBindVertexBuffers(offScreenCmdBuffer, 0, 1, &vertBuffer->m_Buffer, offsets);

				if (renderObject->bIndexed)
				{
					vkCmdBindIndexBuffer(offScreenCmdBuffer, indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
				}

				vkCmdBindPipeline(offScreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderObject->graphicsPipeline);

				// Push constants
				if (renderObjectShader.shader.bNeedPushConstantBlock)
				{
					glm::mat4 view = glm::mat4(glm::mat3(g_CameraManager->CurrentCamera()->GetView())); // Truncate translation part off to center around viewer
					glm::mat4 projection = g_CameraManager->CurrentCamera()->GetProjection();
					renderObjectMat.material.pushConstantBlock.mvp =
						projection * view * renderObject->gameObject->GetTransform()->GetWorldTransform();
					vkCmdPushConstants(offScreenCmdBuffer, renderObject->pipelineLayout,
						VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Material::PushConstantBlock),
						&renderObjectMat.material.pushConstantBlock);
				}

				BindDescriptorSet(&renderObjectShader, i, offScreenCmdBuffer, renderObject->pipelineLayout, renderObject->descriptorSet);

				if (renderObject->bIndexed)
				{
					vkCmdDrawIndexed(offScreenCmdBuffer, renderObject->indices->size(), 1, renderObject->indexOffset, renderObject->vertexOffset, 0);
				}
				else
				{
					vkCmdDraw(offScreenCmdBuffer, renderObject->vertexBufferData->VertexCount, 1, renderObject->vertexOffset, 0);
				}
			}

			vkCmdEndRenderPass(offScreenCmdBuffer);

			VK_CHECK_RESULT(vkEndCommandBuffer(offScreenCmdBuffer));
		}

		void VulkanRenderer::BindDescriptorSet(VulkanShader* shader, i32 meshIndex, VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, VkDescriptorSet descriptorSet)
		{
			u32 dynamicOffset = meshIndex * static_cast<u32>(m_DynamicAlignment);
			u32* dynamicOffsetPtr = nullptr;
			u32 dynamicOffsetCount = 0;
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

		void VulkanRenderer::CreateStaticVertexBuffers()
		{
			for (size_t i = 0; i < m_VertexIndexBufferPairs.size(); ++i)
			{
				if (m_VertexIndexBufferPairs[i].useStagingBuffer)
				{
					size_t requiredMemory = 0;

					for (VulkanRenderObject* renderObject : m_RenderObjects)
					{
						if (renderObject && renderObject->vertexBufferData && m_Materials[renderObject->materialID].material.shaderID == i)
						{
							requiredMemory += renderObject->vertexBufferData->VertexBufferSize;
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

		u32 VulkanRenderer::CreateStaticVertexBuffer(VulkanBuffer* vertexBuffer, ShaderID shaderID, i32 size)
		{
			void* vertexDataStart = malloc(size);
			if (!vertexDataStart)
			{
				PrintError("Failed to allocate memory for vertex buffer %u! Attempted to allocate %d bytes", shaderID, size);
				return 0;
			}

			void* vertexBufferData = vertexDataStart;

			u32 vertexCount = 0;
			u32 vertexBufferSize = 0;
			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject && renderObject->vertexBufferData && m_Materials[renderObject->materialID].material.shaderID == shaderID)
				{
					renderObject->vertexOffset = vertexCount;

					memcpy(vertexBufferData, renderObject->vertexBufferData->vertexData, renderObject->vertexBufferData->VertexBufferSize);

					vertexCount += renderObject->vertexBufferData->VertexCount;
					vertexBufferSize += renderObject->vertexBufferData->VertexBufferSize;

					vertexBufferData = (char*)vertexBufferData + renderObject->vertexBufferData->VertexBufferSize;
				}
			}

			if (vertexBufferSize == 0 || vertexCount == 0)
			{
				PrintError("Failed to create static vertex buffer (no verts use shader index %u)\n", shaderID);
				return 0;
			}

			CreateStaticVertexBuffer(vertexBuffer, vertexDataStart, vertexBufferSize);
			free(vertexDataStart);

			return vertexCount;
		}

		void VulkanRenderer::CreateDynamicVertexBuffer(VulkanBuffer* vertexBuffer, u32 size, void* initialData)
		{
			VulkanBuffer stagingBuffer(m_VulkanDevice->m_LogicalDevice);
			CreateAndAllocateBuffer(m_VulkanDevice, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
				VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer);

			if (initialData)
			{
				VK_CHECK_RESULT(stagingBuffer.Map(size));
				memcpy(stagingBuffer.m_Mapped, initialData, size);
				stagingBuffer.Unmap();
			}

			CreateAndAllocateBuffer(m_VulkanDevice, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer);

			CopyBuffer(m_VulkanDevice, m_GraphicsQueue, stagingBuffer.m_Buffer, vertexBuffer->m_Buffer, size);
		}

		void VulkanRenderer::CreateStaticVertexBuffer(VulkanBuffer* vertexBuffer, void* vertexBufferData, u32 vertexBufferSize)
		{
			VulkanBuffer stagingBuffer(m_VulkanDevice->m_LogicalDevice);
			CreateAndAllocateBuffer(m_VulkanDevice, vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
				VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer);

			VK_CHECK_RESULT(stagingBuffer.Map(vertexBufferSize));
			memcpy(stagingBuffer.m_Mapped, vertexBufferData, vertexBufferSize);
			stagingBuffer.Unmap();

			CreateAndAllocateBuffer(m_VulkanDevice, vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer);

			CopyBuffer(m_VulkanDevice,m_GraphicsQueue, stagingBuffer.m_Buffer, vertexBuffer->m_Buffer, vertexBufferSize);
		}

		void VulkanRenderer::CreateStaticIndexBuffers()
		{
			for (size_t i = 0; i < m_VertexIndexBufferPairs.size(); ++i)
			{
				m_VertexIndexBufferPairs[i].indexCount = CreateStaticIndexBuffer(m_VertexIndexBufferPairs[i].indexBuffer, i);
			}
		}

		u32 VulkanRenderer::CreateStaticIndexBuffer(VulkanBuffer* indexBuffer, ShaderID shaderID)
		{
			std::vector<u32> indices;

			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject && m_Materials[renderObject->materialID].material.shaderID == shaderID && renderObject->bIndexed)
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

		void VulkanRenderer::CreateStaticIndexBuffer(VulkanBuffer* indexBuffer, const std::vector<u32>& indices)
		{
			const size_t bufferSize = sizeof(indices[0]) * indices.size();

			VulkanBuffer stagingBuffer(m_VulkanDevice->m_LogicalDevice);
			VK_CHECK_RESULT(CreateAndAllocateBuffer(m_VulkanDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer));

			VK_CHECK_RESULT(stagingBuffer.Map(bufferSize));
			memcpy(stagingBuffer.m_Mapped, indices.data(), bufferSize);
			stagingBuffer.Unmap();

			VK_CHECK_RESULT(CreateAndAllocateBuffer(m_VulkanDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer));

			CopyBuffer(m_VulkanDevice,m_GraphicsQueue, stagingBuffer.m_Buffer, indexBuffer->m_Buffer, bufferSize);
		}

		u32 VulkanRenderer::AllocateUniformBuffer(u32 dynamicDataSize, void** data)
		{
			size_t uboAlignment = (size_t)m_VulkanDevice->m_PhysicalDeviceProperties.limits.minUniformBufferOffsetAlignment;
			u32 dynamicAllignment =
				(dynamicDataSize / uboAlignment) * uboAlignment +
				((dynamicDataSize % uboAlignment) > 0 ? uboAlignment : 0);

			if (dynamicAllignment > m_DynamicAlignment)
			{
				u32 newDynamicAllignment = 1;
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

		void VulkanRenderer::PrepareUniformBuffer(VulkanBuffer* buffer, u32 bufferSize,
			VkBufferUsageFlags bufferUseageFlagBits, VkMemoryPropertyFlags memoryPropertyHostFlagBits)
		{
			VK_CHECK_RESULT(CreateAndAllocateBuffer(m_VulkanDevice, bufferSize, bufferUseageFlagBits, memoryPropertyHostFlagBits, buffer));
			VK_CHECK_RESULT(buffer->Map());
		}

		void VulkanRenderer::CreateDescriptorPool()
		{
			// TODO: Don't do this?
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

		void VulkanRenderer::DrawFrame()
		{
			u32 imageIndex;
			VkResult result = vkAcquireNextImageKHR(m_VulkanDevice->m_LogicalDevice,
				m_SwapChain, std::numeric_limits<u64>::max(), m_PresentCompleteSemaphore,
				VK_NULL_HANDLE, &imageIndex);

			if (result == VK_ERROR_OUT_OF_DATE_KHR)
			{
				RecreateSwapChain();
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

			submitInfo.pCommandBuffers = &m_CommandBufferManager.m_CommandBuffers[imageIndex];
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
				RecreateSwapChain();
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
			createInfo.pCode = (u32*)code.data();

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

			for (const VkSurfaceFormatKHR& availableFormat : availableFormats)
			{
				if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
				{
					return availableFormat;
				}
			}

			return availableFormats[0];
		}

		VkPresentModeKHR VulkanRenderer::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const
		{
			const VkPresentModeKHR bestMode = (m_bVSyncEnabled ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR);
			VkPresentModeKHR secondBestMode = bestMode;

			for (const VkPresentModeKHR& availablePresentMode : availablePresentModes)
			{
				if (availablePresentMode == bestMode)
				{
					return availablePresentMode;
				}

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

		VkExtent2D VulkanRenderer::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
		{
			if (capabilities.currentExtent.width != std::numeric_limits<u32>::max())
			{
				return capabilities.currentExtent;
			}
			else
			{
				glm::vec2i size = g_Window->GetFrameBufferSize();
				VkExtent2D actualExtent{ (u32)size.x, (u32)size.y };

				actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
				actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));

				return actualExtent;
			}
		}

		VulkanSwapChainSupportDetails VulkanRenderer::QuerySwapChainSupport(VkPhysicalDevice device) const
		{
			VulkanSwapChainSupportDetails details;

			vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_Surface, &details.capabilities);

			u32 formatCount;
			vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, nullptr);

			if (formatCount != 0)
			{
				details.formats.resize(formatCount);
				vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, details.formats.data());
			}

			u32 presentModeCount;
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
			VulkanQueueFamilyIndices indices = FindQueueFamilies(m_Surface, device);

			bool extensionsSupported = CheckDeviceExtensionSupport(device);

			bool swapChainAdequate = false;
			if (extensionsSupported)
			{
				VulkanSwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(device);
				swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
			}

			VkPhysicalDeviceFeatures supportedFeatures;
			vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

			const bool isSuitable = indices.IsComplete() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy;
			return isSuitable;
		}

		bool VulkanRenderer::CheckDeviceExtensionSupport(VkPhysicalDevice device) const
		{
			u32 extensionCount;
			vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

			std::vector<VkExtensionProperties> availableExtensions(extensionCount);
			vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

			std::set<std::string> requiredExtensions(m_DeviceExtensions.begin(), m_DeviceExtensions.end());

			for (const VkExtensionProperties& extension : availableExtensions)
			{
				requiredExtensions.erase(extension.extensionName);
			}

			return requiredExtensions.empty();
		}

		std::vector<const char*> VulkanRenderer::GetRequiredExtensions() const
		{
			std::vector<const char*> extensions;

			u32 glfwExtensionCount = 0;
			const char** glfwExtensions;
			glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

			for (u32 i = 0; i < glfwExtensionCount; ++i)
			{
				extensions.push_back(glfwExtensions[i]);
			}

			if (m_bEnableValidationLayers)
			{
				extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
			}

			return extensions;
		}

		bool VulkanRenderer::CheckValidationLayerSupport() const
		{
			u32 layerCount;
			vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

			std::vector<VkLayerProperties> availableLayers(layerCount);
			vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

			for (const char* layerName : m_ValidationLayers)
			{
				bool layerFound = false;

				for (const VkLayerProperties& layerProperties : availableLayers)
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

		void VulkanRenderer::UpdateConstantUniformBuffers(UniformOverrides const* overridenUniforms)
		{
			glm::mat4 projection = g_CameraManager->CurrentCamera()->GetProjection();
			glm::mat4 view = g_CameraManager->CurrentCamera()->GetView();
			glm::mat4 viewInv = glm::inverse(view);
			glm::mat4 viewProjection = projection * view;
			glm::vec4 camPos = glm::vec4(g_CameraManager->CurrentCamera()->GetPosition(), 0.0f);
			glm::vec4 sdfData = glm::vec4(0.5f, -0.01f, -0.008f, 0.035f);
			i32 texChannel = 0;

			static DirLightData defaultDirLightData = { VEC3_RIGHT, 0, VEC3_ONE, 0.0f };

			DirLightData* dirLightData = &defaultDirLightData;
			if (m_DirectionalLight)
			{
				dirLightData = &m_DirectionalLight->data;
			}

			struct UniformInfo
			{
				UniformInfo(u64 uniform, void* dataStart, size_t copySize) :
					uniform(uniform),
					dataStart(dataStart),
					copySize(copySize)
				{}

				u64 uniform;
				void* dataStart = nullptr;
				size_t copySize;
			};

			UniformInfo uniformInfos[] = {
				{ U_VIEW, (void*)&view, sizeof(glm::mat4) },
				{ U_VIEW_INV, (void*)&viewInv, sizeof(glm::mat4) },
				{ U_PROJECTION, (void*)&projection, sizeof(glm::mat4) },
				{ U_VIEW_PROJECTION, (void*)&viewProjection, sizeof(glm::mat4) },
				{ U_CAM_POS, (void*)&camPos, sizeof(glm::vec4) },
				{ U_DIR_LIGHT, (void*)dirLightData, sizeof(DirLightData) },
				{ U_POINT_LIGHTS, (void*)m_PointLights, sizeof(PointLightData) * MAX_NUM_POINT_LIGHTS },
				{ U_TIME, (void*)&g_SecElapsedSinceProgramStart, sizeof(real) },
				{ U_SDF_DATA, (void*)&sdfData, sizeof(glm::vec4) },
				{ U_TEX_CHANNEL, (void*)&texChannel, sizeof(i32) },
			};

			for (u32 bufferIndex = 0; bufferIndex < m_Materials.size(); ++bufferIndex)
			{
				if (m_Materials[bufferIndex].material.shaderID == InvalidShaderID)
				{
					continue;
				}

				VulkanMaterial& material = m_Materials[bufferIndex];
				VulkanShader& shader = m_Shaders[material.material.shaderID];

				Uniforms& constantUniforms = shader.shader.constantBufferUniforms;
				VulkanUniformBufferObjectData& constantData = shader.uniformBuffer.constantData;

				if (constantData.data == nullptr || constantData.size == 0)
				{
					continue; // There is no constant data to update
				}

				// Restore values in case they were overriden by the last material
				projection = g_CameraManager->CurrentCamera()->GetProjection();
				view = g_CameraManager->CurrentCamera()->GetView();
				viewInv = glm::inverse(view);
				viewProjection = projection * view;
				camPos = glm::vec4(g_CameraManager->CurrentCamera()->GetPosition(), 0.0f);

				if (overridenUniforms)
				{
					if (overridenUniforms->overridenUniforms.HasUniform(U_PROJECTION))
					{
						projection = overridenUniforms->projection;
					}
					if (overridenUniforms->overridenUniforms.HasUniform(U_VIEW))
					{
						view = overridenUniforms->view;
					}
					if (overridenUniforms->overridenUniforms.HasUniform(U_VIEW_INV))
					{
						viewInv = overridenUniforms->viewInv;
					}
					if (overridenUniforms->overridenUniforms.HasUniform(U_VIEW_PROJECTION))
					{
						viewProjection = overridenUniforms->viewProjection;
					}
					if (overridenUniforms->overridenUniforms.HasUniform(U_CAM_POS))
					{
						camPos = overridenUniforms->camPos;
					}
				}

				size_t index = 0;
				for (UniformInfo& uniformInfo : uniformInfos)
				{
					if (constantUniforms.HasUniform(uniformInfo.uniform))
					{
						memcpy(constantData.data + index, uniformInfo.dataStart, uniformInfo.copySize);
						index += (uniformInfo.copySize / sizeof(real));
					}
				}

				u32 size = constantData.size;

#if  DEBUG
				u32 calculatedSize1 = index * 4;
				calculatedSize1 = GetAlignedUBOSize(calculatedSize1);
				assert(calculatedSize1 == size);
#endif // DEBUG

				memcpy(shader.uniformBuffer.constantBuffer.m_Mapped, constantData.data, size);
			}
		}

		void VulkanRenderer::UpdateDynamicUniformBuffer(RenderID renderID, UniformOverrides const* uniformOverrides)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (renderObject == nullptr)
			{
				return;
			}

			VulkanMaterial& material = m_Materials[renderObject->materialID];
			VulkanShader& shader = m_Shaders[material.material.shaderID];

			UniformBuffer& uniformBuffer = shader.uniformBuffer;
			Uniforms dynamicUniforms = shader.shader.dynamicBufferUniforms;

			if (uniformBuffer.dynamicBuffer.m_Size == 0)
			{
				return; // There are no dynamic uniforms to update
			}

			bool updateMVP = false; // This is set to true when either the view or projection matrix get overridden

			glm::mat4 model = renderObject->gameObject->GetTransform()->GetWorldTransform();
			glm::mat4 modelInvTranspose = glm::transpose(glm::inverse(model));
			glm::mat4 projection = g_CameraManager->CurrentCamera()->GetProjection();
			glm::mat4 view = g_CameraManager->CurrentCamera()->GetView();
			glm::mat4 modelViewProjection = projection * view * model;
			glm::vec4 colorMultiplier = material.material.colorMultiplier;
			u32 enableAlbedoSampler = material.material.enableAlbedoSampler;
			u32 enableMetallicSampler = material.material.enableMetallicSampler;
			u32 enableRoughnessSampler = material.material.enableRoughnessSampler;
			u32 enableAOSampler = material.material.enableAOSampler;
			u32 enableNormalSampler = material.material.enableNormalSampler;
			u32 enableCubemapSampler = material.material.enableCubemapSampler;
			u32 enableIrradianceSampler = material.material.enableIrradianceSampler;
			real textureScale = material.material.textureScale;
			real blendSharpness = material.material.blendSharpness;
			glm::vec2 texSize = material.material.texSize;
			glm::vec4 fontCharData = material.material.fontCharData;

			// TODO: Roll into array?
			if (uniformOverrides)
			{
				if (uniformOverrides->overridenUniforms.HasUniform(U_MODEL))
				{
					model = uniformOverrides->model;
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_MODEL_INV_TRANSPOSE))
				{
					modelInvTranspose = uniformOverrides->modelInvTranspose;
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_PROJECTION))
				{
					projection = uniformOverrides->projection;
					updateMVP = true;
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_VIEW))
				{
					view = uniformOverrides->view;
					updateMVP = true;
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_MODEL_VIEW_PROJ))
				{
					modelViewProjection = uniformOverrides->modelViewProjection;
					updateMVP = false;	// Don't override modelViewProjection value with overriden view/projection matrices
										// if it's being specifically overriden itself
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_ENABLE_ALBEDO_SAMPLER))
				{
					enableAlbedoSampler = uniformOverrides->enableAlbedoSampler;
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_ENABLE_METALLIC_SAMPLER))
				{
					enableMetallicSampler = uniformOverrides->enableMetallicSampler;
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_ENABLE_ROUGHNESS_SAMPLER))
				{
					enableRoughnessSampler = uniformOverrides->enableRoughnessSampler;
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_ENABLE_AO_SAMPLER))
				{
					enableAOSampler = uniformOverrides->enableAOSampler;
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_ENABLE_NORMAL_SAMPLER))
				{
					enableNormalSampler = uniformOverrides->enableNormalSampler;
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_ENABLE_CUBEMAP_SAMPLER))
				{
					enableCubemapSampler = uniformOverrides->enableCubemapSampler;
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_ENABLE_IRRADIANCE_SAMPLER))
				{
					enableIrradianceSampler = uniformOverrides->enableIrradianceSampler;
				}
			}

			if (updateMVP)
			{
				modelViewProjection = projection * view * model;
				modelInvTranspose = glm::transpose(glm::inverse(model));
			}

			u32 offset = 0;
			for (VulkanRenderObject* renderObj : m_RenderObjects)
			{
				if (renderObj &&
					renderObj->renderID != renderObject->renderID &&
					m_Materials[renderObj->materialID].material.shaderID == m_Materials[renderObject->materialID].material.shaderID)
				{
					offset += uniformBuffer.dynamicData.size;
				}
			}

			struct UniformInfo
			{
				UniformInfo(u64 uniform,
					void* dataStart,
					size_t copySize) :
					uniform(uniform),
					dataStart(dataStart),
					copySize(copySize)
				{}

				u64 uniform;
				void* dataStart = nullptr;
				size_t copySize;
			};
			UniformInfo uniformInfos[] = {
				{ U_MODEL, (void*)&model, sizeof(glm::mat4) },
				{ U_MODEL_INV_TRANSPOSE, (void*)&modelInvTranspose, sizeof(glm::mat4) },
				{ U_MODEL_VIEW_PROJ, (void*)&modelViewProjection, sizeof(glm::mat4) },
				// view, viewInv, viewProjection, projection, camPos, dirLight, pointLights should be updated in constant uniform buffer
				{ U_COLOR_MULTIPLIER, (void*)&material.material.colorMultiplier, sizeof(glm::vec4) },
				{ U_CONST_ALBEDO, (void*)&material.material.constAlbedo, sizeof(glm::vec4) },
				{ U_CONST_METALLIC, (void*)&material.material.constMetallic, sizeof(real) },
				{ U_CONST_ROUGHNESS, (void*)&material.material.constRoughness, sizeof(real) },
				{ U_CONST_AO, (void*)&material.material.constAO, sizeof(real) },
				{ U_ENABLE_ALBEDO_SAMPLER, (void*)&enableAlbedoSampler, sizeof(real) },
				{ U_ENABLE_METALLIC_SAMPLER, (void*)&enableMetallicSampler, sizeof(real) },
				{ U_ENABLE_ROUGHNESS_SAMPLER, (void*)&enableRoughnessSampler, sizeof(real) },
				{ U_ENABLE_AO_SAMPLER, (void*)&enableAOSampler, sizeof(real) },
				{ U_ENABLE_NORMAL_SAMPLER, (void*)&enableNormalSampler, sizeof(real) },
				{ U_ENABLE_CUBEMAP_SAMPLER, (void*)&enableCubemapSampler, sizeof(real) },
				{ U_ENABLE_IRRADIANCE_SAMPLER, (void*)&enableIrradianceSampler, sizeof(real) },
				{ U_BLEND_SHARPNESS, (void*)&blendSharpness, sizeof(real) },
				{ U_TEXTURE_SCALE, (void*)&textureScale, sizeof(real) },
				{ U_FONT_CHAR_DATA, (void*)&fontCharData, sizeof(glm::vec4) },
				{ U_TEX_SIZE, (void*)&texSize, sizeof(real) },
			};

			u32 index = 0;
			for (UniformInfo& uniformInfo : uniformInfos)
			{
				if (dynamicUniforms.HasUniform(uniformInfo.uniform))
				{
					memcpy(&uniformBuffer.dynamicData.data[offset + index], uniformInfo.dataStart, uniformInfo.copySize);
					index += uniformInfo.copySize / 4;
				}
			}

			// Aligned offset
			u32 size = uniformBuffer.dynamicData.size;

#if  DEBUG
			u32 calculatedSize1 = index * 4;
			calculatedSize1 = GetAlignedUBOSize(calculatedSize1);
			assert(calculatedSize1 == size);
#endif // DEBUG

			void* firstIndex = uniformBuffer.dynamicBuffer.m_Mapped;
			u64 dest = (u64)firstIndex + (renderID * m_DynamicAlignment);
			memcpy((void*)(dest), &uniformBuffer.dynamicData.data[offset], size);

			// Flush to make changes visible to the host
			VkMappedMemoryRange mappedMemoryRange = {};
			mappedMemoryRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
			mappedMemoryRange.offset = offset;
			mappedMemoryRange.memory = uniformBuffer.dynamicBuffer.m_Memory;
			mappedMemoryRange.size = uniformBuffer.dynamicData.size;
			// TODO: Is this call needed if the buffer was allocated specifying VK_MEMORY_PROPERTY_HOST_COHERENT_BIT?
			vkFlushMappedMemoryRanges(m_VulkanDevice->m_LogicalDevice, 1, &mappedMemoryRange);
		}

		void VulkanRenderer::LoadDefaultShaderCode()
		{
			const std::string shaderDirectory = RESOURCE_LOCATION  "shaders\\spv\\";
			struct ShaderInitInfo
			{
				ShaderInitInfo(const char* name, const char* vertFileName, const char* fragFileName, const char* geomFileName = nullptr) :
					name(name),
					vertFileName(vertFileName),
					fragFileName(fragFileName),
					geomFileName(geomFileName)
				{
				}

				const char* name = nullptr;
				const char* vertFileName = nullptr;
				const char* fragFileName = nullptr;
				const char* geomFileName = nullptr;
			};
			ShaderInitInfo initInfos[] = {
				{ "color", "vk_color_vert.spv","vk_color_frag.spv" },
				{ "pbr", "vk_pbr_vert.spv", "vk_pbr_frag.spv" },
				{ "pbr_ws", "vk_pbr_ws_vert.spv", "vk_pbr_ws_frag.spv" },
				{ "skybox", "vk_skybox_vert.spv", "vk_skybox_frag.spv" },
				{ "equirectangular_to_cube", "vk_skybox_vert.spv", "vk_equirectangular_to_cube_frag.spv" },
				{ "irradiance", "vk_skybox_vert.spv", "vk_irradiance_frag.spv" },
				{ "prefilter", "vk_skybox_vert.spv", "vk_prefilter_frag.spv" },
				{ "brdf", "vk_brdf_vert.spv", "vk_brdf_frag.spv" },
				{ "deferred_combine", "vk_deferred_combine_vert.spv", "vk_deferred_combine_frag.spv" },
				{ "deferred_combine_cubemap", "vk_deferred_combine_cubemap_vert.spv", "vk_deferred_combine_cubemap_frag.spv" },
				{ "compute_sdf", "vk_compute_sdf_vert.spv", "vk_compute_sdf_frag.spv" },
				{ "font_ss", "vk_font_ss_vert.spv", "vk_font_frag.spv", "vk_font_ss_geom.spv" },
				{ "font_ws", "vk_font_ws_vert.spv", "vk_font_frag.spv", "vk_font_ws_geom.spv" },
				//{ "post_process", "vk_post_process_vert.spv", "vk_post_process_frag.spv" },
			};

			m_Shaders.reserve(ARRAY_LENGTH(initInfos));
			for (const ShaderInitInfo& initInfo : initInfos)
			{
				std::string vert = shaderDirectory + initInfo.vertFileName;
				std::string frag = shaderDirectory + initInfo.fragFileName;
				std::string geom;
				if (initInfo.geomFileName)
				{
					geom = shaderDirectory + initInfo.geomFileName;
				}
				m_Shaders.emplace_back(m_VulkanDevice->m_LogicalDevice, initInfo.name, vert, frag, geom);
			}

			ShaderID shaderID = 0;

			// Color
			m_Shaders[shaderID].shader.deferred = false;

			m_Shaders[shaderID].shader.translucent = true;
			m_Shaders[shaderID].shader.subpass = 1;
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_VIEW_PROJECTION);

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_MODEL);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_COLOR_MULTIPLIER);
			++shaderID;

			// PBR
			m_Shaders[shaderID].shader.numAttachments = 3;
			m_Shaders[shaderID].shader.deferred = true;
			m_Shaders[shaderID].shader.subpass = 0;
			m_Shaders[shaderID].shader.bNeedAlbedoSampler = true;
			m_Shaders[shaderID].shader.bNeedMetallicSampler = true;
			m_Shaders[shaderID].shader.bNeedRoughnessSampler = true;
			m_Shaders[shaderID].shader.bNeedAOSampler = true;
			m_Shaders[shaderID].shader.bNeedNormalSampler = true;
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV |
				(u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT |
				(u32)VertexAttribute::TANGENT |
				(u32)VertexAttribute::BITANGENT |
				(u32)VertexAttribute::NORMAL;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_VIEW_PROJECTION);

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_MODEL);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_ENABLE_ALBEDO_SAMPLER);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_ALBEDO_SAMPLER);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_CONST_ALBEDO);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_ENABLE_METALLIC_SAMPLER);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_METALLIC_SAMPLER);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_CONST_METALLIC);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_ENABLE_ROUGHNESS_SAMPLER);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_ROUGHNESS_SAMPLER);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_CONST_ROUGHNESS);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_ENABLE_AO_SAMPLER);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_AO_SAMPLER);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_CONST_AO);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_ENABLE_NORMAL_SAMPLER);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_NORMAL_SAMPLER);
			++shaderID;

			// PBR World Space
			m_Shaders[shaderID].shader.numAttachments = 3;
			m_Shaders[shaderID].shader.deferred = true;
			m_Shaders[shaderID].shader.subpass = 0;
			m_Shaders[shaderID].shader.bNeedAlbedoSampler = true;
			m_Shaders[shaderID].shader.bNeedMetallicSampler = true;
			m_Shaders[shaderID].shader.bNeedRoughnessSampler = true;
			m_Shaders[shaderID].shader.bNeedAOSampler = true;
			m_Shaders[shaderID].shader.bNeedNormalSampler = true;
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV |
				(u32)VertexAttribute::TANGENT |
				(u32)VertexAttribute::BITANGENT |
				(u32)VertexAttribute::NORMAL;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_VIEW_PROJECTION);

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_MODEL);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_ENABLE_ALBEDO_SAMPLER);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_ALBEDO_SAMPLER);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_CONST_ALBEDO);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_ENABLE_METALLIC_SAMPLER);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_METALLIC_SAMPLER);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_CONST_METALLIC);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_ENABLE_ROUGHNESS_SAMPLER);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_ROUGHNESS_SAMPLER);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_CONST_ROUGHNESS);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_ENABLE_AO_SAMPLER);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_AO_SAMPLER);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_CONST_AO);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_ENABLE_NORMAL_SAMPLER);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_NORMAL_SAMPLER);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_BLEND_SHARPNESS);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_TEXTURE_SCALE);
			++shaderID;

			// Skybox
			m_Shaders[shaderID].shader.subpass = 1;
			m_Shaders[shaderID].shader.bNeedCubemapSampler = true;
			m_Shaders[shaderID].shader.bNeedPushConstantBlock = true;
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_TIME);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_CUBEMAP_SAMPLER);

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			++shaderID;

			// Equirectangular to cube
			m_Shaders[shaderID].shader.subpass = 1;
			m_Shaders[shaderID].shader.bNeedHDREquirectangularSampler = true;
			m_Shaders[shaderID].shader.bNeedPushConstantBlock = true;
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_HDR_EQUIRECTANGULAR_SAMPLER);

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			++shaderID;

			// Irradiance
			m_Shaders[shaderID].shader.subpass = 1;
			m_Shaders[shaderID].shader.bNeedCubemapSampler = true;
			m_Shaders[shaderID].shader.bNeedPushConstantBlock = true;
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_CUBEMAP_SAMPLER);

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			++shaderID;

			// Prefilter
			m_Shaders[shaderID].shader.subpass = 1;
			m_Shaders[shaderID].shader.bNeedCubemapSampler = true;
			m_Shaders[shaderID].shader.bNeedPushConstantBlock = true;
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_CUBEMAP_SAMPLER);

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_CONST_ROUGHNESS);
			++shaderID;

			// BRDF
			m_Shaders[shaderID].shader.subpass = 1;
			m_Shaders[shaderID].shader.vertexAttributes = 0;

			m_Shaders[shaderID].shader.constantBufferUniforms = {};

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			++shaderID;

			// Deferred combine (sample gbuffer)
			m_Shaders[shaderID].shader.subpass = 0;
			m_Shaders[shaderID].shader.depthWriteEnable = false; // Disable depth writing
			m_Shaders[shaderID].shader.bNeedBRDFLUT = true;
			m_Shaders[shaderID].shader.bNeedIrradianceSampler = true;
			m_Shaders[shaderID].shader.bNeedPrefilteredMap = true;
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV;

			// TODO: Specify that this buffer is only used in the frag shader here
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_CAM_POS);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_DIR_LIGHT);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_POINT_LIGHTS);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_BRDF_LUT_SAMPLER);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_IRRADIANCE_SAMPLER);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_PREFILTER_MAP);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_FB_0_SAMPLER);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_FB_1_SAMPLER);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_FB_2_SAMPLER);

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_ENABLE_IRRADIANCE_SAMPLER);
			++shaderID;

			// Deferred combine cubemap (sample GBuffer)
			m_Shaders[shaderID].shader.subpass = 0;
			m_Shaders[shaderID].shader.depthWriteEnable = false; // Disable depth writing
			m_Shaders[shaderID].shader.bNeedBRDFLUT = true;
			m_Shaders[shaderID].shader.bNeedIrradianceSampler = true;
			m_Shaders[shaderID].shader.bNeedPrefilteredMap = true;
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION; // Used as 3D texture coord into cubemap

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_CUBEMAP_SAMPLER);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_CAM_POS);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_DIR_LIGHT);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_POINT_LIGHTS);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_BRDF_LUT_SAMPLER);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_IRRADIANCE_SAMPLER);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_PREFILTER_MAP);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_FB_0_SAMPLER);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_FB_1_SAMPLER);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_FB_2_SAMPLER);

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_ENABLE_IRRADIANCE_SAMPLER);
			++shaderID;

			// Compute SDF
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV;

			m_Shaders[shaderID].shader.constantBufferUniforms = {};
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_UNIFORM_BUFFER_CONSTANT);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_SDF_DATA);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_TEX_CHANNEL);
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_HIGH_RES_TEX); // highResTex

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			//m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_SDF_RESOLUTION);
			//m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_HIGH_RES);
			++shaderID;

			// Font SS
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION_2D |
				(u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT |
				(u32)VertexAttribute::UV |
				(u32)VertexAttribute::EXTRA_VEC4 |
				(u32)VertexAttribute::EXTRA_INT;

			m_Shaders[shaderID].shader.constantBufferUniforms = {};
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_ALBEDO_SAMPLER); // in_Texture

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_MODEL);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_FONT_CHAR_DATA);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_TEX_SIZE);
			++shaderID;

			// Font WS
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT |
				(u32)VertexAttribute::UV |
				(u32)VertexAttribute::EXTRA_VEC4 |
				(u32)VertexAttribute::EXTRA_INT;

			m_Shaders[shaderID].shader.constantBufferUniforms = {};
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform(U_ALBEDO_SAMPLER); // in_Texture

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_UNIFORM_BUFFER_DYNAMIC);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_MODEL);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_FONT_CHAR_DATA);
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform(U_TEX_SIZE);
			++shaderID;

			assert(shaderID == ARRAY_LENGTH(initInfos));

			const size_t shaderCount = m_Shaders.size();
			for (size_t i = 0; i < shaderCount; ++i)
			{
				Shader& shader = m_Shaders[i].shader;

#if 1 // Sanity check
				assert(!shader.constantBufferUniforms.HasUniform(U_UNIFORM_BUFFER_DYNAMIC));
				assert(!shader.dynamicBufferUniforms.HasUniform(U_UNIFORM_BUFFER_CONSTANT));

				if (shader.constantBufferUniforms.HasUniform(U_HIGH_RES_TEX))
				{
					assert(!shader.constantBufferUniforms.HasUniform(U_ALBEDO_SAMPLER));
				}
#endif

				std::string vertFileName = shader.vertexShaderFilePath;
				StripLeadingDirectories(vertFileName);
				std::string fragFileName = m_Shaders[i].shader.fragmentShaderFilePath;
				StripLeadingDirectories(fragFileName);
				if (g_bEnableLogging_Loading)
				{
					Print("Loading shaders %s & %s\n", vertFileName.c_str(), fragFileName.c_str());
				}

				if (!ReadFile(shader.vertexShaderFilePath, shader.vertexShaderCode, true))
				{
					PrintError("Could not find vertex shader %s\n", shader.name.c_str());
				}
				if (!ReadFile(shader.fragmentShaderFilePath, shader.fragmentShaderCode, true))
				{
					PrintError("Could not find fragment shader %s\n", shader.name.c_str());
				}
			}
		}

		void VulkanRenderer::GenerateIrradianceMaps()
		{
			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				VulkanRenderObject* renderObject = GetRenderObject(i);
				if (!renderObject)
				{
					continue;
				}

				VulkanMaterial& renderObjectMat = m_Materials[renderObject->materialID];

				if (renderObjectMat.material.generateIrradianceSampler)
				{
					GenerateCubemapFromHDR(renderObject, renderObjectMat.material.environmentMapPath);
					GenerateIrradianceSampler(renderObject);
					GeneratePrefilteredCube(renderObject);
				}
			}

			// Generate graphics pipelines with correct render pass set
			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				//CreateDescriptorSet(i);
				CreateGraphicsPipeline(i, false);
			}
		}

		void VulkanRenderer::CaptureSceneToCubemap(RenderID cubemapRenderID)
		{
			// UNIMPLEMENTED
		}

		void VulkanRenderer::GeneratePrefilteredMapFromCubemap(MaterialID cubemapMaterialID)
		{
			UNREFERENCED_PARAMETER(cubemapMaterialID);
		}

		void VulkanRenderer::GenerateIrradianceSamplerFromCubemap(MaterialID cubemapMaterialID)
		{
			UNREFERENCED_PARAMETER(cubemapMaterialID);
		}

		VKAPI_ATTR VkBool32 VKAPI_CALL VulkanRenderer::DebugCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType,
			u64 obj, size_t location, i32 code, const char* layerPrefix, const char* msg, void* userData)
		{
			UNREFERENCED_PARAMETER(objType);
			UNREFERENCED_PARAMETER(obj);
			UNREFERENCED_PARAMETER(location);
			UNREFERENCED_PARAMETER(code);
			UNREFERENCED_PARAMETER(layerPrefix);
			UNREFERENCED_PARAMETER(userData);

			switch (flags)
			{
			case VkDebugReportFlagBitsEXT::VK_DEBUG_REPORT_INFORMATION_BIT_EXT:
				Print("%s\n", msg);
				break;
			case VkDebugReportFlagBitsEXT::VK_DEBUG_REPORT_WARNING_BIT_EXT:
				PrintWarn("%s\n", msg);
				break;
			case VkDebugReportFlagBitsEXT::VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT:
				PrintWarn("%s\n", msg);
				break;
			case VkDebugReportFlagBitsEXT::VK_DEBUG_REPORT_ERROR_BIT_EXT:
				PrintError("%s\n", msg);
				break;
			case VkDebugReportFlagBitsEXT::VK_DEBUG_REPORT_DEBUG_BIT_EXT:
			default:
				PrintError("%s\n", msg);
				break;
			}

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