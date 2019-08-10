#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanRenderer.hpp"

IGNORE_WARNINGS_PUSH
#include "stb_image.h"

#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>

#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp> // For glm::scale overload

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
#include "Graphics/Vulkan/VulkanInitializers.hpp"
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

		PFN_vkDebugMarkerSetObjectNameEXT VulkanRenderer::m_vkDebugMarkerSetObjectName = nullptr;
		PFN_vkCmdDebugMarkerBeginEXT VulkanRenderer::m_vkCmdDebugMarkerBegin = nullptr;
		PFN_vkCmdDebugMarkerEndEXT VulkanRenderer::m_vkCmdDebugMarkerEnd = nullptr;

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

			FindPresentInstanceExtensions();

			if (m_bEnableDebugMarkers)
			{
				m_vkDebugMarkerSetObjectName = reinterpret_cast<PFN_vkDebugMarkerSetObjectNameEXT>(vkGetInstanceProcAddr(m_Instance, "vkDebugMarkerSetObjectNameEXT"));
				m_vkCmdDebugMarkerBegin = reinterpret_cast<PFN_vkCmdDebugMarkerBeginEXT>(vkGetDeviceProcAddr(m_VulkanDevice->m_LogicalDevice, "vkCmdDebugMarkerBeginEXT"));
				m_vkCmdDebugMarkerEnd = reinterpret_cast<PFN_vkCmdDebugMarkerEndEXT>(vkGetDeviceProcAddr(m_VulkanDevice->m_LogicalDevice, "vkCmdDebugMarkerEndEXT"));
			}

			{
				u32 instanceAPIVersion;
				VK_CHECK_RESULT(vkEnumerateInstanceVersion(&instanceAPIVersion));
				u32 instanceVersionMaj = VK_VERSION_MAJOR(instanceAPIVersion);
				u32 instanceVersionMin = VK_VERSION_MINOR(instanceAPIVersion);
				u32 instanceVersionPatch = VK_VERSION_PATCH(instanceAPIVersion);

				VkPhysicalDeviceProperties& props = m_VulkanDevice->m_PhysicalDeviceProperties;
				u32 deviceVersionMaj = VK_VERSION_MAJOR(props.apiVersion);
				u32 deviceVersionMin = VK_VERSION_MINOR(props.apiVersion);
				u32 deviceVersionPatch = VK_VERSION_PATCH(props.apiVersion);

				Print("Vulkan loaded - instance v%u.%u.%u (device v%u.%u.%u)\n", instanceVersionMaj, instanceVersionMin, instanceVersionPatch, deviceVersionMaj, deviceVersionMin, deviceVersionPatch);
				Print("Vendor ID, Device ID: 0x%u, 0x%u\n", props.vendorID, props.deviceID);
				Print("Device info: %s, ", (const char*)props.deviceName);
				Print("(%s), ", DeviceTypeToString(props.deviceType).c_str());
				Print("driver version: %u\n", props.driverVersion);
			}

			VkPhysicalDeviceMemoryProperties physicalDeviceMemProps;
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &physicalDeviceMemProps);
			Print("%u Memory heap(s) present on device:\n\t", physicalDeviceMemProps.memoryHeapCount);
			for (u32 i = 0; i < physicalDeviceMemProps.memoryHeapCount; ++i)
			{
				const VkMemoryHeap& heap = physicalDeviceMemProps.memoryHeaps[i];
				Print("%.2f GB (%s local)", (real)heap.size / (1024.0f * 1024.0f * 1024.0f), heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT ? "device" : "host");
				if (i != physicalDeviceMemProps.memoryHeapCount - 1)
				{
					Print(", ");
				}
				else
				{
					Print("\n");
				}
			}

			VkFormat depthFormat;
			if (!GetSupportedDepthFormat(m_VulkanDevice->m_PhysicalDevice, &depthFormat))
			{
				PrintWarn("Failed to find suitable depth format\n");
			}

			m_CommandBufferManager = VulkanCommandBufferManager(m_VulkanDevice);

			FrameBufferAttachment::CreateInfo swapChainDepthCreateInfo = {};
			swapChainDepthCreateInfo.bIsDepth = true;
			swapChainDepthCreateInfo.bIsTransferedDst = true;
			swapChainDepthCreateInfo.format = depthFormat;
			m_SwapChainDepthAttachment = new FrameBufferAttachment(m_VulkanDevice, swapChainDepthCreateInfo);

			FrameBufferAttachment::CreateInfo gBufferDepthCreateInfo = {};
			gBufferDepthCreateInfo.bIsDepth = true;
			gBufferDepthCreateInfo.bIsTransferedSrc = true;
			gBufferDepthCreateInfo.bIsSampled = true;
			gBufferDepthCreateInfo.format = depthFormat;
			m_GBufferDepthAttachment = new FrameBufferAttachment(m_VulkanDevice, gBufferDepthCreateInfo);

			FrameBufferAttachment::CreateInfo offscreenDepthCreateInfo = {};
			offscreenDepthCreateInfo.bIsDepth = true;
			// TODO: Double check
			offscreenDepthCreateInfo.bIsTransferedSrc = true;
			offscreenDepthCreateInfo.bIsSampled = true;
			offscreenDepthCreateInfo.format = depthFormat;
			m_OffscreenDepthAttachment0 = new FrameBufferAttachment(m_VulkanDevice, offscreenDepthCreateInfo);
			m_OffscreenDepthAttachment1 = new FrameBufferAttachment(m_VulkanDevice, offscreenDepthCreateInfo);

			FrameBufferAttachment::CreateInfo cubemapDepthCreateInfo = {};
			cubemapDepthCreateInfo.bIsDepth = true;
			cubemapDepthCreateInfo.bIsCubemap = true;
			cubemapDepthCreateInfo.format = depthFormat;
			m_CubemapDepthAttachment = new FrameBufferAttachment(m_VulkanDevice, cubemapDepthCreateInfo);

			m_SwapChain = { m_VulkanDevice->m_LogicalDevice, vkDestroySwapchainKHR };

			m_DeferredCombineRenderPass = { m_VulkanDevice->m_LogicalDevice, vkDestroyRenderPass };
			m_SSAORenderPass = { m_VulkanDevice->m_LogicalDevice, vkDestroyRenderPass };
			m_SSAOBlurHRenderPass = { m_VulkanDevice->m_LogicalDevice, vkDestroyRenderPass };
			m_SSAOBlurVRenderPass = { m_VulkanDevice->m_LogicalDevice, vkDestroyRenderPass };
			m_ForwardRenderPass = { m_VulkanDevice->m_LogicalDevice, vkDestroyRenderPass };
			m_PostProcessRenderPass = { m_VulkanDevice->m_LogicalDevice, vkDestroyRenderPass };
			m_TAAResolveRenderPass = { m_VulkanDevice->m_LogicalDevice, vkDestroyRenderPass };
			m_UIRenderPass = { m_VulkanDevice->m_LogicalDevice, vkDestroyRenderPass };
			m_GammaCorrectRenderPass = { m_VulkanDevice->m_LogicalDevice, vkDestroyRenderPass };

			m_DescriptorPool = { m_VulkanDevice->m_LogicalDevice, vkDestroyDescriptorPool };

			m_PresentCompleteSemaphore = { m_VulkanDevice->m_LogicalDevice, vkDestroySemaphore };
			m_RenderCompleteSemaphore = { m_VulkanDevice->m_LogicalDevice, vkDestroySemaphore };

			m_ColorSampler = { m_VulkanDevice->m_LogicalDevice, vkDestroySampler };
			m_DepthSampler = { m_VulkanDevice->m_LogicalDevice, vkDestroySampler };
			m_SSAOSampler = { m_VulkanDevice->m_LogicalDevice, vkDestroySampler };

			m_ShadowPipelineLayout = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipelineLayout };
			m_ShadowGraphicsPipeline = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipeline };

			m_FontSSPipelineLayout = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipelineLayout };
			m_FontWSPipelineLayout = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipelineLayout };

			m_FontSSGraphicsPipeline = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipeline };
			m_FontWSGraphicsPipeline = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipeline };

			m_SSAOGraphicsPipelineLayout = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipelineLayout };
			m_SSAOBlurGraphicsPipelineLayout = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipelineLayout };

			m_SSAOGraphicsPipeline = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipeline };
			m_SSAOBlurHGraphicsPipeline = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipeline };
			m_SSAOBlurVGraphicsPipeline = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipeline };

			m_SpriteArrGraphicsPipeline = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipeline };
			m_SpriteArrGraphicsPipelineLayout = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipelineLayout };

			m_PostProcessGraphicsPipeline = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipeline };
			m_PostProcessGraphicsPipelineLayout = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipelineLayout };
			m_TAAResolveGraphicsPipeline = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipeline };
			m_TAAResolveGraphicsPipelineLayout = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipelineLayout };
			m_GammaCorrectGraphicsPipeline = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipeline };
			m_GammaCorrectGraphicsPipelineLayout = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipelineLayout };

			m_ShadowImage = { m_VulkanDevice->m_LogicalDevice, vkDestroyImage };
			m_ShadowImageView = { m_VulkanDevice->m_LogicalDevice, vkDestroyImageView };
			m_ShadowImageMemory = { m_VulkanDevice->m_LogicalDevice, vkFreeMemory };
			m_ShadowRenderPass = { m_VulkanDevice->m_LogicalDevice, vkDestroyRenderPass };


			CreateSwapChain();
			CreateSwapChainImageViews();

			m_OffscreenFrameBufferFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

			FrameBufferAttachment::CreateInfo frameBufCreateInfo = {};
			frameBufCreateInfo.format = m_OffscreenFrameBufferFormat;
			frameBufCreateInfo.bIsSampled = true;

			m_GBufferFrameBuf = new FrameBuffer(m_VulkanDevice);
			m_GBufferFrameBuf->frameBufferAttachments = {
				{ "normalRoughness", { m_VulkanDevice, frameBufCreateInfo } },
				{ "albedoMetallic",  { m_VulkanDevice, frameBufCreateInfo } },
			};

			m_OffscreenFrameBuffer0 = new FrameBuffer(m_VulkanDevice);
			m_OffscreenFrameBuffer0->frameBufferAttachments = {
				{ "color", { m_VulkanDevice, frameBufCreateInfo } },
			};
			m_OffscreenFrameBuffer0->frameBufferAttachments[0].second.bIsTransferedSrc = true;

			m_OffscreenFrameBuffer1 = new FrameBuffer(m_VulkanDevice);
			m_OffscreenFrameBuffer1->frameBufferAttachments = {
				{ "color", { m_VulkanDevice, frameBufCreateInfo } },
			};

			m_HistoryBuffer = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "History buffer", m_SwapChainExtent.width, m_SwapChainExtent.height, 4);
			m_HistoryBuffer->imageFormat = m_OffscreenFrameBufferFormat;

			frameBufCreateInfo.bIsCubemap = true;
			m_GBufferCubemapFrameBuffer = new FrameBuffer(m_VulkanDevice);
			m_GBufferCubemapFrameBuffer->frameBufferAttachments = {
				{ "normalRoughness", { m_VulkanDevice, frameBufCreateInfo } },
				{ "albedoMetallic",  { m_VulkanDevice, frameBufCreateInfo } },
			};
			frameBufCreateInfo.bIsCubemap = false;

			frameBufCreateInfo.format = VK_FORMAT_R16_SFLOAT;

			m_SSAOFrameBuf = new FrameBuffer(m_VulkanDevice);
			m_SSAOFrameBuf->frameBufferAttachments = {
				{ "ssao", { m_VulkanDevice, frameBufCreateInfo } },
			};

			m_SSAOBlurHFrameBuf = new FrameBuffer(m_VulkanDevice);
			m_SSAOBlurHFrameBuf->frameBufferAttachments = {
				{ "ssao blur h", { m_VulkanDevice, frameBufCreateInfo } },
			};

			m_SSAOBlurVFrameBuf = new FrameBuffer(m_VulkanDevice);
			m_SSAOBlurVFrameBuf->frameBufferAttachments = {
				{ "ssao blur final", { m_VulkanDevice, frameBufCreateInfo } },
			};

			m_ShadowBufFormat = VK_FORMAT_D16_UNORM;

			for (u32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
			{
				m_ShadowCascades[i] = new Cascade(m_VulkanDevice->m_LogicalDevice);
			}

			// NOTE: This is different from the GLRenderer's capture views
			s_CaptureViews = {
				glm::lookAt(VEC3_ZERO, glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(VEC3_ZERO, glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(VEC3_ZERO, glm::vec3(0.0f,  -1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  -1.0f)),
				glm::lookAt(VEC3_ZERO, glm::vec3(0.0f, 1.0f,  0.0f), glm::vec3(0.0f,  0.0f, 1.0f)),
				glm::lookAt(VEC3_ZERO, glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(VEC3_ZERO, glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
			};

			// TODO: Make variable
			m_GBufferCubemapFrameBuffer->width = 512;
			m_GBufferCubemapFrameBuffer->height = 512;

			CreateRenderPasses();

			m_CommandBufferManager.CreatePool(m_Surface);
			CreateDepthResources();
			CreateFramebuffers();

			PrepareFrameBuffers();
			PrepareCubemapFrameBuffer();

			m_BlankTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, RESOURCE_LOCATION  "textures/blank.jpg", 1, false, false, false);
			m_BlankTexture->CreateFromFile(VK_FORMAT_R8G8B8A8_UNORM);

			m_AlphaBGTextureID = InitializeTexture(RESOURCE_LOCATION  "textures/alpha-bg.png", 4, false, false, false);
			m_LoadingTextureID = InitializeTexture(RESOURCE_LOCATION  "textures/loading_1.png", 4, false, false, false);
			m_WorkTextureID = InitializeTexture(RESOURCE_LOCATION  "textures/work_d.jpg", 4, false, true, false);
			m_PointLightIconID = InitializeTexture(RESOURCE_LOCATION  "textures/icons/point-light-icon-256.png", 4, false, true, false);
			m_DirectionalLightIconID = InitializeTexture(RESOURCE_LOCATION  "textures/icons/directional-light-icon-256.png", 4, false, true, false);

			m_SpritePerspPushConstBlock = new Material::PushConstantBlock(128);
			m_SpriteOrthoPushConstBlock = new Material::PushConstantBlock(128);
			m_SpriteOrthoArrPushConstBlock = new Material::PushConstantBlock(132);

#ifdef DEBUG
			while (!m_ShaderCompiler->TickStatus())
			{
				// Spin lock
			}

			if (m_ShaderCompiler->bSuccess == false)
			{
				PrintError("Failed to compile shader code!\n");
			}

			delete m_ShaderCompiler;
			m_ShaderCompiler = nullptr;
#endif
			LoadShaders();

			const u32 shaderCount = m_Shaders.size();
			m_VertexIndexBufferPairs.reserve(shaderCount);
			for (size_t i = 0; i < shaderCount; ++i)
			{
				m_VertexIndexBufferPairs.emplace_back(
					new VulkanBuffer(m_VulkanDevice->m_LogicalDevice), // Vertex buffer
					new VulkanBuffer(m_VulkanDevice->m_LogicalDevice)  // Index buffer
					);
				if (m_Shaders[i].shader->bDynamic)
				{
					VertexIndexBufferPair& pair = m_VertexIndexBufferPairs[m_VertexIndexBufferPairs.size() - 1];
					pair.useStagingBuffer = false;
				}
			}

			m_FullScreenTriVertexBuffer = new VulkanBuffer(m_VulkanDevice->m_LogicalDevice);

			m_SSAOSpecializationMapEntry = { 0, 0, sizeof(i32) };
			m_SSAOSpecializationInfo.mapEntryCount = 1;
			m_SSAOSpecializationInfo.pMapEntries = &m_SSAOSpecializationMapEntry;
			m_SSAOSpecializationInfo.pData = &m_SSAOKernelSize;
			m_SSAOSpecializationInfo.dataSize = sizeof(i32);

			m_TAASpecializationMapEntry = { 0, 0, sizeof(i32) };
			m_TAAOSpecializationInfo.mapEntryCount = 1;
			m_TAAOSpecializationInfo.pMapEntries = &m_TAASpecializationMapEntry;
			m_TAAOSpecializationInfo.pData = &m_TAASampleCount;
			m_TAAOSpecializationInfo.dataSize = sizeof(i32);

			Renderer::InitializeMaterials();

			VkQueryPoolCreateInfo queryPoolCreateInfo = {};
			queryPoolCreateInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
			queryPoolCreateInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
			queryPoolCreateInfo.queryCount = MAX_TIMESTAMP_QUERIES;
			vkCreateQueryPool(m_VulkanDevice->m_LogicalDevice, &queryPoolCreateInfo, nullptr, &m_TimestampQueryPool);
		}

		void VulkanRenderer::PostInitialize()
		{
			Renderer::PostInitialize();

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
					u32 size = GetAlignedUBOSize(shader.shader->dynamicBufferUniforms.CalculateSizeInBytes());
					u32 dynamicDataSize = size;
					u32 dynamicAllignment =
						(dynamicDataSize / uboAlignment) * uboAlignment +
						((dynamicDataSize % uboAlignment) > 0 ? uboAlignment : 0);

					if (dynamicAllignment > m_DynamicAlignment)
					{
						u32 newDynamicAllignment = 1;
						// TODO: Use better nearest POT calculation!
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

			GenerateBRDFLUT();

			// TODO: (not so EZ): Move to base renderer
			// SSAO Materials
			{
				if (m_SSAOMatID == InvalidMaterialID)
				{
					MaterialCreateInfo ssaoMatCreateInfo = {};
					ssaoMatCreateInfo.name = "ssao";
					ssaoMatCreateInfo.shaderName = "ssao";
					ssaoMatCreateInfo.persistent = true;
					ssaoMatCreateInfo.visibleInEditor = false;
					m_SSAOMatID = InitializeMaterial(&ssaoMatCreateInfo);
				}
				assert(m_SSAOMatID != InvalidMaterialID);

				if (m_SSAOBlurMatID == InvalidMaterialID)
				{
					MaterialCreateInfo ssaoBlurMatCreateInfo = {};
					ssaoBlurMatCreateInfo.name = "ssao blur";
					ssaoBlurMatCreateInfo.shaderName = "ssao_blur";
					ssaoBlurMatCreateInfo.persistent = true;
					ssaoBlurMatCreateInfo.visibleInEditor = false;
					m_SSAOBlurMatID = InitializeMaterial(&ssaoBlurMatCreateInfo);
				}
				assert(m_SSAOBlurMatID != InvalidMaterialID);

				CreateSSAOPipelines();
				CreateSSAODescriptorSets();
			}

			CreateShadowResources();

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
				init_info.PipelineCache = VK_NULL_HANDLE;
				init_info.DescriptorPool = m_DescriptorPool;
				init_info.Allocator = NULL;
				init_info.CheckVkResultFn = NULL;
				ImGui_ImplVulkan_Init(&init_info, m_UIRenderPass);

				{
					// TODO: Use general purpose command buffer manager
					VkCommandBuffer command_buffer = m_CommandBufferManager.m_CommandBuffers[0];

					VkCommandBufferBeginInfo begin_info = vks::commandBufferBeginInfo();
					begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
					VK_CHECK_RESULT(vkBeginCommandBuffer(command_buffer, &begin_info));

					ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

					VkSubmitInfo end_info = vks::submitInfo(1, &command_buffer);
					VK_CHECK_RESULT(vkEndCommandBuffer(command_buffer));
					SetCommandBufferName(m_VulkanDevice, command_buffer, "ImGui create fonts texture command buffer");
					VK_CHECK_RESULT(vkQueueSubmit(m_GraphicsQueue, 1, &end_info, VK_NULL_HANDLE));

					VK_CHECK_RESULT(vkDeviceWaitIdle(*m_VulkanDevice));
					ImGui_ImplVulkan_InvalidateFontUploadObjects();
				}
			}

			CreateStaticVertexBuffers();
			CreateStaticIndexBuffers();
			CreateDynamicVertexBuffers();

			CreateShadowVertexBuffer();
			CreateShadowIndexBuffer();

			void* vertData = malloc_hooked(m_FullScreenTriVertexBufferData.VertexBufferSize);
			memcpy(vertData, m_FullScreenTriVertexBufferData.vertexData, m_FullScreenTriVertexBufferData.VertexBufferSize);
			CreateStaticVertexBuffer(m_FullScreenTriVertexBuffer, vertData, m_FullScreenTriVertexBufferData.VertexBufferSize);
			free_hooked(vertData);

			// Shadow index/vertex offsets
			{
				u32 vertexCount = 0;
				for (VulkanRenderObject* renderObject : m_RenderObjects)
				{
					// TODO: Only account for shadow-casting objects?
					if (renderObject)
					{
						renderObject->shadowVertexOffset = vertexCount;
						vertexCount += renderObject->vertexBufferData->VertexCount;
					}
				}

			}


			CreateSemaphores();

			LoadFonts(false);

			GenerateIrradianceMaps();

			CreatePostProcessingObjects();

			m_bPostInitialized = true;
		}

		void VulkanRenderer::Destroy()
		{
			Renderer::Destroy();

#ifdef DEBUG
			delete m_ShaderCompiler;
			m_ShaderCompiler = nullptr;
#endif

			vkQueueWaitIdle(m_GraphicsQueue);
			vkQueueWaitIdle(m_PresentQueue);
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

			delete m_FullScreenTriVertexBuffer;
			m_FullScreenTriVertexBuffer = nullptr;

			m_SkyBoxMesh = nullptr;

			if (m_PhysicsDebugDrawer)
			{
				m_PhysicsDebugDrawer->Destroy();
				delete m_PhysicsDebugDrawer;
				m_PhysicsDebugDrawer = nullptr;
			}

			for (GameObject* obj : m_PersistentObjects)
			{
				obj->Destroy();
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

			delete m_SpritePerspPushConstBlock;
			m_SpritePerspPushConstBlock = nullptr;

			delete m_SpriteOrthoPushConstBlock;
			m_SpriteOrthoPushConstBlock = nullptr;

			delete m_SpriteOrthoArrPushConstBlock;
			m_SpriteOrthoArrPushConstBlock = nullptr;

			delete m_GBufferFrameBuf;
			m_GBufferFrameBuf = nullptr;

			delete m_OffscreenFrameBuffer0;
			m_OffscreenFrameBuffer0 = nullptr;

			delete m_OffscreenFrameBuffer1;
			m_OffscreenFrameBuffer1 = nullptr;

			delete m_HistoryBuffer;
			m_HistoryBuffer = nullptr;

			delete m_SSAOFrameBuf;
			m_SSAOFrameBuf = nullptr;

			delete m_SSAOBlurHFrameBuf;
			m_SSAOBlurHFrameBuf = nullptr;

			delete m_SSAOBlurVFrameBuf;
			m_SSAOBlurVFrameBuf = nullptr;

			delete m_GBufferCubemapFrameBuffer;
			m_GBufferCubemapFrameBuffer = nullptr;

			delete m_CubemapDepthAttachment;
			m_CubemapDepthAttachment = nullptr;

			delete m_SwapChainDepthAttachment;
			m_SwapChainDepthAttachment = nullptr;

			delete m_GBufferDepthAttachment;
			m_GBufferDepthAttachment = nullptr;

			delete m_OffscreenDepthAttachment0;
			m_OffscreenDepthAttachment0 = nullptr;

			delete m_OffscreenDepthAttachment1;
			m_OffscreenDepthAttachment1 = nullptr;

			for (u32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
			{
				delete m_ShadowCascades[i];
			}

			vkDestroySemaphore(m_VulkanDevice->m_LogicalDevice, m_OffscreenSemaphore, nullptr);

			m_ShadowImage.replace();
			m_ShadowImageView.replace();
			m_ShadowImageMemory.replace();
			m_ShadowRenderPass.replace();

			m_SSAOGraphicsPipeline.replace();
			m_SSAOBlurHGraphicsPipeline.replace();
			m_SSAOBlurVGraphicsPipeline.replace();

			m_SSAOGraphicsPipelineLayout.replace();
			m_SSAOBlurGraphicsPipelineLayout.replace();

			m_SpriteArrGraphicsPipeline.replace();
			m_SpriteArrGraphicsPipelineLayout.replace();

			m_SSAOSampler.replace();

			m_ShadowGraphicsPipeline.replace();
			m_ShadowPipelineLayout.replace();

			m_FontSSPipelineLayout.replace();
			m_FontSSGraphicsPipeline.replace();
			m_FontWSPipelineLayout.replace();
			m_FontWSGraphicsPipeline.replace();

			m_PostProcessGraphicsPipeline.replace();
			m_PostProcessGraphicsPipelineLayout.replace();
			m_TAAResolveGraphicsPipeline.replace();
			m_TAAResolveGraphicsPipelineLayout.replace();
			m_GammaCorrectGraphicsPipeline.replace();
			m_GammaCorrectGraphicsPipelineLayout.replace();

			m_gBufferQuadVertexBufferData.Destroy();
			m_DescriptorPool.replace();
			m_ColorSampler.replace();
			m_DepthSampler.replace();

			delete m_BlankTexture;
			m_BlankTexture = nullptr;

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
			m_SSAORenderPass.replace();
			m_SSAOBlurHRenderPass.replace();
			m_SSAOBlurVRenderPass.replace();
			m_ForwardRenderPass.replace();
			m_PostProcessRenderPass.replace();
			m_TAAResolveRenderPass.replace();
			m_UIRenderPass.replace();
			m_GammaCorrectRenderPass.replace();

			m_SwapChain.replace();
			m_SwapChainImageViews.clear();
			m_SwapChainFramebuffers.clear();

			vkDestroyQueryPool(m_VulkanDevice->m_LogicalDevice, m_TimestampQueryPool, nullptr);

			m_CommandBufferManager.DestroyCommandBuffers();

			vkDeviceWaitIdle(m_VulkanDevice->m_LogicalDevice);

			delete m_VulkanDevice;
			m_VulkanDevice = nullptr;

			glfwTerminate();
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

			mat.material.sampledFrameBuffers = createInfo->sampledFrameBuffers;

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

			mat.material.colorMultiplier = createInfo->colorMultiplier;

			mat.material.enableHDREquirectangularSampler = createInfo->enableHDREquirectangularSampler;
			mat.material.generateHDREquirectangularSampler = createInfo->generateHDREquirectangularSampler;
			mat.material.hdrEquirectangularTexturePath = createInfo->hdrEquirectangularTexturePath;

			mat.material.generateHDRCubemapSampler = createInfo->generateHDRCubemapSampler;

			mat.material.enableIrradianceSampler = createInfo->enableIrradianceSampler;
			mat.material.generateIrradianceSampler = createInfo->generateIrradianceSampler;
			mat.material.irradianceSamplerSize = createInfo->generatedIrradianceCubemapSize;

			if (shader.shader->bNeedPushConstantBlock)
			{
				mat.material.pushConstantBlock = new Material::PushConstantBlock(shader.shader->pushConstantBlockSize);
			}

			if (shader.shader->bNeedIrradianceSampler)
			{
				if (createInfo->irradianceSamplerMatID < m_Materials.size())
				{
					mat.irradianceTexture = m_Materials[createInfo->irradianceSamplerMatID].irradianceTexture;
				}
			}
			if (shader.shader->bNeedBRDFLUT)
			{
				if (!m_BRDFTexture)
				{
					m_BRDFTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "BRDF", m_BRDFSize.x, m_BRDFSize.y, 1);
					m_BRDFTexture->CreateEmpty(VK_FORMAT_R16G16_SFLOAT, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
					m_LoadedTextures.push_back(m_BRDFTexture);
				}
				mat.brdfLUT = m_BRDFTexture;
			}
			if (shader.shader->bNeedPrefilteredMap)
			{
				mat.prefilterTexture = (createInfo->prefilterMapSamplerMatID < m_Materials.size() ?
					m_Materials[createInfo->prefilterMapSamplerMatID].prefilterTexture : nullptr);
			}

			mat.material.enablePrefilteredMap = createInfo->enablePrefilteredMap;
			mat.material.generatePrefilteredMap = createInfo->generatePrefilteredMap;
			mat.material.prefilteredMapSize = createInfo->generatedPrefilteredCubemapSize;

			mat.material.enableBRDFLUT = createInfo->enableBRDFLUT;
			mat.material.renderToCubemap = createInfo->renderToCubemap;

			mat.material.persistent = createInfo->persistent;
			mat.material.visibleInEditor = createInfo->visibleInEditor;

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
				{ createInfo->hdrEquirectangularTexturePath, &mat.hdrEquirectangularTexture, &mat.material.generateHDREquirectangularSampler, VK_FORMAT_R32G32B32A32_SFLOAT, 1, true },
			};
			const size_t textureCount = sizeof(textureInfos) / sizeof(textureInfos[0]);

			// Calculate how many textures need to be allocated to prevent texture vector from resizing
			const size_t usedTextureCount = createInfo->generateAlbedoSampler +
				createInfo->generateCubemapSampler +
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

			if (shader.shader->bNeedCubemapSampler)
			{

			}


			if (shader.shader->bNeedBRDFLUT)
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

			if (shader.shader->bNeedIrradianceSampler)
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

			if (shader.shader->bNeedPrefilteredMap)
			{

			}

			if (shader.shader->constantBufferUniforms.HasUniform(U_NOISE_SAMPLER))
			{
				if (m_NoiseTexture == nullptr)
				{
					std::vector<glm::vec4> ssaoNoise;
					GenerateSSAONoise(ssaoNoise);

					m_NoiseTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "SSAO Noise", SSAO_NOISE_DIM, SSAO_NOISE_DIM, 4);
					void* buffer = ssaoNoise.data();
					u32 bufferSize = ssaoNoise.size() * sizeof(glm::vec4);
					m_NoiseTexture->CreateFromMemory(buffer, bufferSize, VK_FORMAT_R32G32B32A32_SFLOAT, 1, VK_FILTER_NEAREST);
					m_LoadedTextures.push_back(m_NoiseTexture);
				}

				mat.noiseTexture = m_NoiseTexture;
			}

			return matID;
		}

		TextureID VulkanRenderer::InitializeTexture(const std::string& relativeFilePath, i32 channelCount, bool bFlipVertically, bool bGenerateMipMaps, bool bHDR)
		{
			VulkanTexture* newTex = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, relativeFilePath,
				channelCount, bFlipVertically, bGenerateMipMaps, bHDR);
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
			renderObject->bEditorObject = createInfo->bEditorObject;
			if (createInfo->bEditorObject)
			{
				renderObject->gameObject->SetCastsShadow(false);
			}
			renderObject->depthCompareOp = DepthTestFuncToVkCompareOp(createInfo->depthTestReadFunc);
			renderObject->bSetDynamicStates = createInfo->bSetDynamicStates;
			renderObject->renderPassOverride = createInfo->renderPassOverride;

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

		void VulkanRenderer::PostInitializeRenderObject(RenderID renderID)
		{
			UNREFERENCED_PARAMETER(renderID);
		}

		void VulkanRenderer::ClearMaterials(bool bDestroyPersistentMats /* = false */)
		{
			auto iter = m_Materials.begin();
			while (iter != m_Materials.end())
			{
				if (bDestroyPersistentMats || iter->second.material.persistent == false)
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

		void VulkanRenderer::CreateUniformBuffers(VulkanShader* shader)
		{
			if (shader->shader->constantBufferUniforms.HasUniform(U_UNIFORM_BUFFER_CONSTANT))
			{
				shader->uniformBuffer.constantData.size = shader->shader->constantBufferUniforms.CalculateSizeInBytes();
				if (shader->uniformBuffer.constantData.size > 0)
				{
					free_hooked(shader->uniformBuffer.constantData.data);

					shader->uniformBuffer.constantData.size = GetAlignedUBOSize(shader->uniformBuffer.constantData.size);

					shader->uniformBuffer.constantData.data = static_cast<real*>(malloc_hooked(shader->uniformBuffer.constantData.size));
					assert(shader->uniformBuffer.constantData.data);

					PrepareUniformBuffer(&shader->uniformBuffer.constantBuffer, shader->uniformBuffer.constantData.size,
						VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				}
			}

			if (shader->shader->dynamicBufferUniforms.HasUniform(U_UNIFORM_BUFFER_DYNAMIC))
			{
				shader->uniformBuffer.dynamicData.size = shader->shader->dynamicBufferUniforms.CalculateSizeInBytes();
				if (shader->uniformBuffer.dynamicData.size > 0 && !m_RenderObjects.empty())
				{
					if (shader->uniformBuffer.dynamicData.data) aligned_free_hooked(shader->uniformBuffer.dynamicData.data);

					shader->uniformBuffer.dynamicData.size = GetAlignedUBOSize(shader->uniformBuffer.dynamicData.size);

					const size_t dynamicBufferSize = AllocateDynamicUniformBuffer(
						shader->uniformBuffer.dynamicData.size, (void**)&shader->uniformBuffer.dynamicData.data);
					shader->uniformBuffer.fullDynamicBufferSize = dynamicBufferSize;
					if (dynamicBufferSize > 0)
					{
						PrepareUniformBuffer(&shader->uniformBuffer.dynamicBuffer, dynamicBufferSize,
							VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
					}
				}
			}
		}

		void VulkanRenderer::CreatePostProcessingObjects()
		{
			// Post process descriptor set
			{
				ShaderID postProcessShaderID = m_Materials[m_PostProcessMatID].material.shaderID;
				VulkanShader* postProcessShader = &m_Shaders[postProcessShaderID];
				VkDescriptorSetLayout descSetLayout = m_DescriptorSetLayouts[postProcessShaderID];

				DescriptorSetCreateInfo descSetCreateInfo = {};
				descSetCreateInfo.DBG_Name = "Post Process descriptor set";
				descSetCreateInfo.descriptorSet = &m_PostProcessDescriptorSet;
				descSetCreateInfo.descriptorSetLayout = &descSetLayout;
				descSetCreateInfo.shaderID = postProcessShaderID;
				descSetCreateInfo.uniformBuffer = &postProcessShader->uniformBuffer;
				FrameBufferAttachment& sceneFrameBufferAttachment = m_OffscreenFrameBuffer0->frameBufferAttachments[0].second;
				descSetCreateInfo.sceneImageView = sceneFrameBufferAttachment.view;
				descSetCreateInfo.sceneSampler = m_ColorSampler;
				CreateDescriptorSet(&descSetCreateInfo);
			}

			// TAA Resolve descriptor set
			{
				ShaderID taaResolveShaderID = m_Materials[m_TAAResolveMaterialID].material.shaderID;
				VulkanShader* taaResolveShader = &m_Shaders[taaResolveShaderID];
				VkDescriptorSetLayout descSetLayout = m_DescriptorSetLayouts[taaResolveShaderID];

				DescriptorSetCreateInfo descSetCreateInfo = {};
				descSetCreateInfo.DBG_Name = "TAA Resolve descriptor set";
				descSetCreateInfo.descriptorSet = &m_TAAResolveDescriptorSet;
				descSetCreateInfo.descriptorSetLayout = &descSetLayout;
				descSetCreateInfo.shaderID = taaResolveShaderID;
				descSetCreateInfo.uniformBuffer = &taaResolveShader->uniformBuffer;
				FrameBufferAttachment& sceneFrameBufferAttachment = m_OffscreenFrameBuffer1->frameBufferAttachments[0].second;
				descSetCreateInfo.sceneImageView = sceneFrameBufferAttachment.view;
				descSetCreateInfo.sceneSampler = m_ColorSampler;
				descSetCreateInfo.historyBufferImageView = m_HistoryBuffer->imageView;
				descSetCreateInfo.historyBufferSampler = m_ColorSampler;
				CreateDescriptorSet(&descSetCreateInfo);
			}

			// Gamma Correct descriptor set
			{
				ShaderID gammaCorrectShaderID = m_Materials[m_GammaCorrectMaterialID].material.shaderID;
				VulkanShader* gammaCorrectShader = &m_Shaders[gammaCorrectShaderID];
				VkDescriptorSetLayout descSetLayout = m_DescriptorSetLayouts[gammaCorrectShaderID];

				DescriptorSetCreateInfo descSetCreateInfo = {};
				descSetCreateInfo.DBG_Name = "Gamma Correct descriptor set";
				descSetCreateInfo.descriptorSet = &m_GammaCorrectDescriptorSet;
				descSetCreateInfo.descriptorSetLayout = &descSetLayout;
				descSetCreateInfo.shaderID = gammaCorrectShaderID;
				descSetCreateInfo.uniformBuffer = &gammaCorrectShader->uniformBuffer;
				FrameBufferAttachment& sceneFrameBufferAttachment = m_bEnableTAA ? m_OffscreenFrameBuffer0->frameBufferAttachments[0].second : m_OffscreenFrameBuffer1->frameBufferAttachments[0].second;
				descSetCreateInfo.sceneImageView = sceneFrameBufferAttachment.view;
				descSetCreateInfo.sceneSampler = m_ColorSampler;
				CreateDescriptorSet(&descSetCreateInfo);
			}

			// Sprite array pipeline
			{
				VulkanMaterial& spriteArrMat = m_Materials[m_SpriteArrMatID];
				VulkanShader& spriteArrShader = m_Shaders[spriteArrMat.material.shaderID];

				std::array<VkPushConstantRange, 1> pushConstantRanges = {};
				pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
				pushConstantRanges[0].offset = 0;
				pushConstantRanges[0].size = spriteArrShader.shader->pushConstantBlockSize;

				GraphicsPipelineCreateInfo createInfo = {};
				createInfo.DBG_Name = "Sprite array pipeline";
				createInfo.graphicsPipeline = m_SpriteArrGraphicsPipeline.replace();
				createInfo.pipelineLayout = &m_SpriteArrGraphicsPipelineLayout;
				createInfo.renderPass = m_UIRenderPass;
				createInfo.shaderID = spriteArrMat.material.shaderID;
				createInfo.vertexAttributes = m_Quad3DVertexBufferData.Attributes;
				createInfo.descriptorSetLayoutIndex = spriteArrMat.material.shaderID;
				createInfo.bEnableColorBlending = true;
				createInfo.depthTestEnable = VK_FALSE;
				createInfo.depthWriteEnable = VK_FALSE;
				createInfo.pushConstantRangeCount = pushConstantRanges.size();
				createInfo.pushConstants = pushConstantRanges.data();
				CreateGraphicsPipeline(&createInfo);
			}

			// TODO: Check if these need to be recreated on resize or level reload
			// Post process pipeline
			{
				VulkanMaterial& postProcessMat = m_Materials[m_PostProcessMatID];

				GraphicsPipelineCreateInfo createInfo = {};
				createInfo.DBG_Name = "Post process pipeline";
				createInfo.graphicsPipeline = m_PostProcessGraphicsPipeline.replace();
				createInfo.pipelineLayout = m_PostProcessGraphicsPipelineLayout.replace();
				createInfo.renderPass = m_PostProcessRenderPass;
				createInfo.shaderID = postProcessMat.material.shaderID;
				createInfo.vertexAttributes = m_FullScreenTriVertexBufferData.Attributes;
				createInfo.descriptorSetLayoutIndex = postProcessMat.material.shaderID;
				createInfo.bSetDynamicStates = true;
				createInfo.depthTestEnable = VK_FALSE;
				createInfo.depthWriteEnable = VK_FALSE;
				CreateGraphicsPipeline(&createInfo);
			}

			// TAA Resolve pipeline
			{
				VulkanMaterial& taaResolveMat = m_Materials[m_TAAResolveMaterialID];

				GraphicsPipelineCreateInfo createInfo = {};
				createInfo.DBG_Name = "TAA Resolve pipeline";
				createInfo.graphicsPipeline = m_TAAResolveGraphicsPipeline.replace();
				createInfo.pipelineLayout = m_TAAResolveGraphicsPipelineLayout.replace();
				createInfo.renderPass = m_TAAResolveRenderPass;
				createInfo.shaderID = taaResolveMat.material.shaderID;
				createInfo.vertexAttributes = m_FullScreenTriVertexBufferData.Attributes;
				createInfo.descriptorSetLayoutIndex = taaResolveMat.material.shaderID;
				createInfo.bSetDynamicStates = true;
				createInfo.depthTestEnable = VK_FALSE;
				createInfo.depthWriteEnable = VK_FALSE;
				createInfo.fragSpecializationInfo = &m_TAAOSpecializationInfo;
				CreateGraphicsPipeline(&createInfo);
			}

			// Gamma Correct pipeline
			{
				VulkanMaterial& gammaCorrectMat = m_Materials[m_GammaCorrectMaterialID];

				GraphicsPipelineCreateInfo createInfo = {};
				createInfo.DBG_Name = "Gamma Correct pipeline";
				createInfo.graphicsPipeline = m_GammaCorrectGraphicsPipeline.replace();
				createInfo.pipelineLayout = m_GammaCorrectGraphicsPipelineLayout.replace();
				createInfo.renderPass = m_GammaCorrectRenderPass;
				createInfo.shaderID = gammaCorrectMat.material.shaderID;
				createInfo.vertexAttributes = m_FullScreenTriVertexBufferData.Attributes;
				createInfo.descriptorSetLayoutIndex = gammaCorrectMat.material.shaderID;
				createInfo.bSetDynamicStates = true;
				createInfo.depthTestEnable = VK_FALSE;
				createInfo.depthWriteEnable = VK_FALSE;
				CreateGraphicsPipeline(&createInfo);
			}
		}

		void VulkanRenderer::Update()
		{
			Renderer::Update();

#ifdef DEBUG
			if (m_ShaderCompiler != nullptr)
			{
				if (m_ShaderCompiler->TickStatus())
				{
					if (m_ShaderCompiler->bSuccess == false)
					{
						PrintError("Async shader recompile failed\n");
						AddEditorString("Async shader recompile failed");
					}
					else
					{
						Print("Async shader recompile completed successfully!\n");
						AddEditorString("Async shader recompile completed successfully");

						LoadShaders();

						CreateRenderPasses();
						PrepareFrameBuffers();

						// Force font descriptor sets to be regenerated
						for (BitmapFont* font : m_FontsSS)
						{
							font->m_DescriptorSet = VK_NULL_HANDLE;
						}

						for (BitmapFont* font : m_FontsWS)
						{
							font->m_DescriptorSet = VK_NULL_HANDLE;
						}

						for (VulkanShader& shader : m_Shaders)
						{
							shader.renderPass = ResolveRenderPassType(shader.shader->renderPassType, shader.shader->name.c_str());
							CreateUniformBuffers(&shader);
						}

						CreateShadowResources();

						for (u32 i = 0; i < m_RenderObjects.size(); ++i)
						{
							CreateDescriptorSet(i);
							CreateGraphicsPipeline(i, false);
						}

						for (auto& iter : m_SpriteDescSets)
						{
							vkFreeDescriptorSets(m_VulkanDevice->m_LogicalDevice, m_DescriptorPool, 1, &(iter.second.descSet));
							iter.second.descSet = CreateSpriteDescSet(iter.second.shaderID, iter.first, iter.second.textureLayer);
						}

						CreateSSAODescriptorSets();
						CreateSSAOPipelines();

						CreatePostProcessingObjects();
					}

					m_bSwapChainNeedsRebuilding = true; // This is needed to recreate some resources for SSAO, etc.

					delete m_ShaderCompiler;
					m_ShaderCompiler = nullptr;
				}
			}
#endif

			if (m_bSSAOStateChanged)
			{
				m_bSSAOStateChanged = false;
				// Update GBuffer in case blur or ssao entirely was toggled
				CreateDescriptorSet(m_GBufferQuadRenderID);
				CreateGraphicsPipeline(m_GBufferQuadRenderID, false);
				// Update SSAO pipelines in case kernel size changed
				CreateSSAOPipelines();
			}

			if (m_bTAAStateChanged)
			{
				m_bTAAStateChanged = false;

				CreatePostProcessingObjects();
			}

			m_PhysicsDebugDrawer->UpdateDebugMode();

			UpdateConstantUniformBuffers();

			// TODO: Only update when things have changed
			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				UpdateDynamicUniformBuffer(i);
			}

			if (!m_TimestampQueryNames.empty())
			{
				m_TimestampHistograms.resize(m_TimestampQueryNames.size());
				u32 i = 0;
				for (auto iter = m_TimestampQueryNames.begin(); iter != m_TimestampQueryNames.end(); ++iter)
				{
					m_TimestampHistograms[i][m_TimestampHistogramIndex] = GetDurationBetweenTimeStamps(iter->first);
					++i;
				}

				m_TimestampHistogramIndex = (m_TimestampHistogramIndex + 1) % NUM_GPU_TIMINGS;

				m_TimestampQueryNames.clear();
			}

			m_LastFrameViewProj = g_CameraManager->CurrentCamera()->GetViewProjection();
		}

		void VulkanRenderer::Draw()
		{
			glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
			if (m_SwapChainExtent.width == 0u || m_SwapChainExtent.height == 0u ||
				frameBufferSize.x == 0 || frameBufferSize.y == 0)
			{
				return;
			}

			DrawCallInfo drawCallInfo = {};

			if (!m_PhysicsDebuggingSettings.DisableAll)
			{
				PhysicsDebugRender();
			}

			if (m_bRebatchRenderObjects)
			{
				m_bRebatchRenderObjects = false;
				BatchRenderObjects();
			}

			// TODO: Don't build command buffers when m_bRebatchRenderObjects is false (but dynamic UI elements still need to be rebuilt!)
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

		void VulkanRenderer::DrawImGuiWindows()
		{
			Renderer::DrawImGuiWindows();

			if (bGPUTimingsWindowShowing)
			{
				if (ImGui::Begin("GPU Timings", &bGPUTimingsWindowShowing))
				{
					for (auto iter = m_TimestampQueryNames.begin(); iter != m_TimestampQueryNames.end(); ++iter)
					{
						u32 arrayIndex = abs(iter->second / 2);
						if (arrayIndex < m_TimestampHistograms.size())
						{
							char buf[256];
							sprintf_s(buf, 256, "%s: %.2fms", iter->first.c_str(), m_TimestampHistograms[arrayIndex][0]);
							ImGui::PlotLines(buf, m_TimestampHistograms[arrayIndex].data(), NUM_GPU_TIMINGS, m_TimestampHistogramIndex, nullptr, 0.0f, 8.0f);
						}
					}
				}
				ImGui::End();
			}

			if (bUniformBufferWindowShowing)
			{
				if (ImGui::Begin("Dynamic Uniform Buffers", &bUniformBufferWindowShowing))
				{
					ShaderBatch* shaderBatches[] = { &m_DeferredObjectBatches, &m_ForwardObjectBatches };
					const char* shaderBatchNames[] = { "Deferred", "Forward" };
					for (u32 i = 0; i < ARRAY_LENGTH(shaderBatches); ++i)
					{
						ShaderBatch* shaderBatch = shaderBatches[i];

						ImGui::Text("%s", shaderBatchNames[i]);

						for (u32 j = 0; j < shaderBatch->batches.size(); ++j)
						{
							const ShaderBatchPair& shaderBatchPair = shaderBatch->batches[j];
							const VulkanShader& shader = m_Shaders[shaderBatchPair.shaderID];

							if (shader.uniformBuffer.fullDynamicBufferSize == 0)
							{
								continue;
							}

							ImVec2 p = ImGui::GetCursorScreenPos();
							char nodeID0[256];
							memset(nodeID0, 0, 256);
							sprintf_s(nodeID0, 256, "%s##%u",
								m_Shaders[shaderBatchPair.shaderID].shader->name.c_str(),
								shaderBatchPair.shaderID);
							if (ImGui::BeginChild(nodeID0, ImVec2(0, 50), true))
							{
								std::vector<real> dynamicObjects;

								for (u32 k = 0; k < shaderBatchPair.batch.batches.size(); ++k)
								{
									const MaterialBatchPair& matBatchPair = shaderBatchPair.batch.batches[k];
									for (RenderID renderID : matBatchPair.batch.objects)
									{
										VulkanRenderObject* renderObject = GetRenderObject(renderID);
										if (renderObject != nullptr)
										{
											dynamicObjects.push_back(1.0f);
										}
									}
								}

								u32 bufferSlotsTotal = (shader.uniformBuffer.fullDynamicBufferSize / shader.uniformBuffer.dynamicData.size);
								u32 bufferSlotsFree = bufferSlotsTotal - dynamicObjects.size();
								for (u32 s = 0; s < bufferSlotsFree; ++s)
								{
									dynamicObjects.push_back(0.0f);
								}

								char histNodeID[256];
								memset(histNodeID, 0, 256);
								sprintf_s(histNodeID, 256, "%s (%u/%u)##histo%u",
									m_Shaders[shaderBatchPair.shaderID].shader->name.c_str(),
									bufferSlotsTotal - bufferSlotsFree,
									bufferSlotsTotal,
									shaderBatchPair.shaderID);
								ImGui::PlotHistogram(histNodeID, dynamicObjects.data(), dynamicObjects.size(), 0, NULL, 0.0f, 1.0f, ImVec2(0, 0));
							}
							ImGui::EndChild();
						}

						//if (ImGui::TreeNode((void*)(intptr_t)(i), "%s", shaderBatchNames[i]))
						//{
						//	for (u32 j = 0; j < shaderBatch->batches.size(); ++j)
						//	{
						//		const ShaderBatchPair& shaderBatchPair = shaderBatch->batches[j];
						//
						//		if (!shaderBatchPair.batch.batches.empty())
						//		{
						//			if (ImGui::TreeNode((void*)(intptr_t)(shaderBatchPair.shaderID), "Shader: %s, ID: %u (children: %u)",
						//				m_Shaders[shaderBatchPair.shaderID].shader.name.c_str(), shaderBatchPair.shaderID, shaderBatchPair.batch.batches.size()))
						//			{
						//				for (u32 k = 0; k < shaderBatchPair.batch.batches.size(); ++k)
						//				{
						//					const MaterialBatchPair& matBatchPair = shaderBatchPair.batch.batches[k];
						//
						//					if (!matBatchPair.batch.objects.empty())
						//					{
						//						if (ImGui::TreeNode((void*)(intptr_t)(matBatchPair.materialID), "Mat: %s, ID: %u (children: %u)",
						//							m_Materials[matBatchPair.materialID].material.name.c_str(), matBatchPair.materialID, matBatchPair.batch.objects.size()))
						//						{
						//							for (RenderID renderID : matBatchPair.batch.objects)
						//							{
						//								VulkanRenderObject* renderObject = GetRenderObject(renderID);
						//								if (renderObject != nullptr)
						//								{
						//									bool bSelected = g_EngineInstance->IsObjectSelected(renderObject->gameObject);
						//									char nodeID[256];
						//									memset(nodeID, 0, 256);
						//									sprintf_s(nodeID, 256, "%s dyn off: %u, renderID: %u",
						//										renderObject->gameObject->GetName().c_str(),
						//										renderObject->dynamicUBOIndex,
						//										renderObject->renderID);
						//
						//									if (ImGui::Selectable(nodeID, &bSelected))
						//									{
						//										if (bSelected)
						//										{
						//											g_EngineInstance->SetSelectedObject(renderObject->gameObject, false);
						//										}
						//										else
						//										{
						//											g_EngineInstance->DeselectObject(renderObject->gameObject);
						//										}
						//									}
						//								}
						//							}
						//							ImGui::TreePop();
						//						}
						//					}
						//				}
						//				ImGui::TreePop();
						//			}
						//		}
						//	}
						//	ImGui::TreePop();
						//}
					}
				}
				ImGui::End();
			}
		}

		void VulkanRenderer::UpdateVertexData(RenderID renderID, VertexBufferData* vertexBufferData)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			VulkanBuffer* vertexBuffer = m_VertexIndexBufferPairs[m_Materials[renderObject->materialID].material.shaderID].vertexBuffer;
			u32 copySize = std::min(vertexBufferData->VertexBufferSize, (u32)vertexBuffer->m_Size);
			if (copySize < vertexBufferData->VertexBufferSize)
			{
				PrintError("Dynamic vertex buffer is %u bytes too small for data attempting to be copied in\n", vertexBufferData->VertexBufferSize - copySize);
			}
			VK_CHECK_RESULT(vertexBuffer->Map(copySize));
			memcpy(vertexBuffer->m_Mapped, vertexBufferData->vertexData, copySize);
			vertexBuffer->Unmap();
		}

		void VulkanRenderer::DrawImGuiForRenderObject(RenderID renderID)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (renderObject != nullptr)
			{
				ImGui::Text("Mat ID: %u", renderObject->materialID);
				ImGui::Text("Shader ID: %u", GetMaterial(renderObject->materialID).shaderID);
				//ImGui::Text("Dynamic Offset: %u", renderObject->dynamicUBOIndex);
			}
		}

		void VulkanRenderer::ReloadShaders()
		{
#ifdef DEBUG
			if (m_ShaderCompiler == nullptr)
			{
				m_ShaderCompiler = new AsyncVulkanShaderCompiler(false);
			}

			if (m_ShaderCompiler->bComplete)
			{
				AddEditorString("Found no shader changes to recompile");
				delete m_ShaderCompiler;
				m_ShaderCompiler = nullptr;
			}
			else
			{
				AddEditorString("Kicking off async shader recompile");
			}
#endif
		}

		void VulkanRenderer::LoadFonts(bool bForceRender)
		{
			PROFILE_AUTO("Load fonts");

			m_FontsSS.clear();
			m_FontsWS.clear();

			for (auto& pair : m_Fonts)
			{
				FontMetaData& fontMetaData = pair.second;

				if (fontMetaData.bitmapFont != nullptr)
				{
					delete fontMetaData.bitmapFont;
					fontMetaData.bitmapFont = nullptr;
				}

				std::string fontName = fontMetaData.filePath;
				StripLeadingDirectories(fontName);
				StripFileType(fontName);

				LoadFont(fontMetaData, bForceRender);
			}
		}

		void VulkanRenderer::ReloadSkybox(bool bRandomizeTexture)
		{
			UNREFERENCED_PARAMETER(bRandomizeTexture);
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

		void VulkanRenderer::OnWindowSizeChanged(i32 width, i32 height)
		{
			UNREFERENCED_PARAMETER(width);
			UNREFERENCED_PARAMETER(height);

			if (width != 0 && height != 0)
			{
				m_bSwapChainNeedsRebuilding = true;
			}
		}

		// TODO: FIXME: This shouldn't be called on startup!!!
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
				CreateDynamicVertexBuffers();

				CreateShadowVertexBuffer();
				CreateShadowIndexBuffer();

				GenerateIrradianceMaps();
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
					m_Shaders[m_Materials[renderObject->materialID].material.shaderID].shader->bNeedPrefilteredMap)
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
				MaterialID pMatID = renderObject->materialID;
				if (materialID != pMatID)
				{
					u32 pStride = CalculateVertexStride(m_Shaders[m_Materials[pMatID].material.shaderID].shader->vertexAttributes);
					u32 newStride = CalculateVertexStride(m_Shaders[m_Materials[materialID].material.shaderID].shader->vertexAttributes);
					renderObject->materialID = materialID;
					if (newStride != pStride)
					{
						// Regenerate vertex data with new stride
						renderObject->gameObject->GetMeshComponent()->SetRequiredAttributesFromMaterialID(materialID);
						renderObject->gameObject->GetMeshComponent()->Reload();
					}
				}
			}
			else
			{
				PrintError("SetRenderObjectMaterialID couldn't find render object with ID %u\n", renderID);
			}

			m_bRebatchRenderObjects = true;
		}

		Material& VulkanRenderer::GetMaterial(MaterialID materialID)
		{
			return m_Materials[materialID].material;
		}

		Shader& VulkanRenderer::GetShader(ShaderID shaderID)
		{
			return *m_Shaders[shaderID].shader;
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
				renderObject = nullptr;
			}
			m_RenderObjects[renderID] = nullptr;
			m_bRebatchRenderObjects = true;
		}

		VkPhysicalDeviceFeatures VulkanRenderer::GetEnabledFeaturesForDevice(VkPhysicalDevice physicalDevice)
		{
			VkPhysicalDeviceFeatures enabledFeatures = {};

			VkPhysicalDeviceFeatures supportedFeatures;
			vkGetPhysicalDeviceFeatures(physicalDevice, &supportedFeatures);

			if (supportedFeatures.samplerAnisotropy)
			{
				enabledFeatures.samplerAnisotropy = VK_TRUE; // TODO: Remove if not used
			}

			if (supportedFeatures.geometryShader)
			{
				enabledFeatures.geometryShader = VK_TRUE;
			}
			else
			{
				PrintError("Selected GPU does not support geometry shaders!\n");
			}

			return enabledFeatures;
		}

		void VulkanRenderer::NewFrame()
		{
			if (g_Window->GetFrameBufferSize().x == 0)
			{
				return;
			}

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

		PhysicsDebugDrawBase* VulkanRenderer::GetDebugDrawer()
		{
			return m_PhysicsDebugDrawer;
		}

		void VulkanRenderer::DrawStringSS(const std::string& str,
			const glm::vec4& color,
			AnchorPoint anchor,
			const glm::vec2& pos,
			real spacing,
			real scale /* = 1.0f */)
		{
			assert(m_CurrentFont != nullptr);

			TextCache newCache(str, anchor, pos, color, spacing, scale);
			m_CurrentFont->AddTextCache(newCache);
		}

		void VulkanRenderer::DrawStringWS(const std::string& str,
			const glm::vec4& color,
			const glm::vec3& pos,
			const glm::quat& rot,
			real spacing,
			real scale /* = 1.0f */)
		{
			assert(m_CurrentFont != nullptr);

			TextCache newCache(str, pos, rot, color, spacing, scale);
			m_CurrentFont->AddTextCache(newCache);
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
					while (!m_Materials[selectedMaterialID].material.visibleInEditor &&
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

							++i;
						}

						mat.material.enableAlbedoSampler = (albedoTextureIndex > 0);
						mat.material.enableMetallicSampler = (metallicTextureIndex > 0);
						mat.material.enableRoughnessSampler = (roughnessTextureIndex > 0);
						mat.material.enableNormalSampler = (normalTextureIndex > 0);

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
							*out_str = (static_cast<VulkanShader*>(data))[idx].shader->name.c_str();
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
								*out_str = static_cast<VulkanTexture**>(data)[idx - 1]->GetName().c_str();
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

					ImGui::NewLine();

					ImGui::EndColumns();

					if (ImGui::BeginChild("material list", ImVec2(0.0f, 120.0f), true))
					{
						i32 matShortIndex = 0;
						for (i32 i = 0; i < (i32)m_Materials.size(); ++i)
						{
							if (!m_Materials[i].material.visibleInEditor)
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
									createInfo.shaderName = m_Shaders[dupMat.shaderID].shader->name;
									createInfo.constAlbedo = dupMat.constAlbedo;
									createInfo.constRoughness = dupMat.constRoughness;
									createInfo.constMetallic = dupMat.constMetallic;
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
								if (ImGui::Selectable(shader.shader->name.c_str(), &bSelectedShader))
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
							createInfo.shaderName = m_Shaders[newMatShaderIndex].shader->name;
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
					ImGui::Checkbox("Flip U", &selectedMesh->importSettings.bFlipU); ImGui::NextColumn();
					ImGui::Checkbox("Flip V", &selectedMesh->importSettings.bFlipV); ImGui::NextColumn();
					ImGui::Checkbox("Swap Normal YZ", &selectedMesh->importSettings.bSwapNormalYZ); ImGui::NextColumn();
					ImGui::Checkbox("Flip Normal Z", &selectedMesh->importSettings.bFlipNormalZ); ImGui::NextColumn();
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
			assert(textureID < m_LoadedTextures.size());
			return m_LoadedTextures[textureID]->textureID;
		}

		void VulkanRenderer::RenderObjectStateChanged()
		{
			// TODO: Ignore object visibility changes
			m_bRebatchRenderObjects = true;
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
			equirectangularToCubeMatCreateInfo.name = "equirectangular to Cube";
			equirectangularToCubeMatCreateInfo.shaderName = "equirectangular_to_cube";
			equirectangularToCubeMatCreateInfo.enableHDREquirectangularSampler = true;
			equirectangularToCubeMatCreateInfo.generateHDREquirectangularSampler = true;
			equirectangularToCubeMatCreateInfo.hdrEquirectangularTexturePath = environmentMapPath;
			equirectangularToCubeMatCreateInfo.persistent = true;
			equirectangularToCubeMatCreateInfo.visibleInEditor = false;

			bool bRandomizeSkybox = true;
			if (bRandomizeSkybox && !m_AvailableHDRIs.empty())
			{
				equirectangularToCubeMatCreateInfo.hdrEquirectangularTexturePath = PickRandomSkyboxTexture();
			}

			MaterialID equirectangularToCubeMatID = InitializeMaterial(&equirectangularToCubeMatCreateInfo);
			VulkanMaterial& equirectangularToCubeMat = m_Materials[equirectangularToCubeMatID];

			const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
			const u32 dim = (u32)renderObjectMat.material.cubemapSamplerSize.x;
			assert(dim <= MAX_TEXTURE_DIM);
			const u32 mipLevels = static_cast<u32>(floor(log2(dim))) + 1;

			VkRenderPass renderPass;
			CreateRenderPass(&renderPass, format, "Equirectangular to Cubemap render pass");

			// Offscreen framebuffer
			struct {
				VkImage image;
				VkImageView view;
				VkDeviceMemory memory;
				VkFramebuffer framebuffer;
			} offscreen;

			// Color attachment
			VkImageCreateInfo offscreenImageCreateInfo = vks::imageCreateInfo();
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

			VkMemoryRequirements offscreenMemRequirements;
			vkGetImageMemoryRequirements(m_VulkanDevice->m_LogicalDevice, offscreen.image, &offscreenMemRequirements);
			VkMemoryAllocateInfo offscreenMemAlloc = vks::memoryAllocateInfo(offscreenMemRequirements.size);
			offscreenMemAlloc.memoryTypeIndex = FindMemoryType(m_VulkanDevice, offscreenMemRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(m_VulkanDevice->m_LogicalDevice, &offscreenMemAlloc, nullptr, &offscreen.memory));
			VK_CHECK_RESULT(vkBindImageMemory(m_VulkanDevice->m_LogicalDevice, offscreen.image, offscreen.memory, 0));

			VkImageViewCreateInfo colorImageView = vks::imageViewCreateInfo();
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

			VkFramebufferCreateInfo framebufCreateInfo = vks::framebufferCreateInfo(renderPass);
			framebufCreateInfo.attachmentCount = 1;
			framebufCreateInfo.pAttachments = &offscreen.view;
			framebufCreateInfo.width = dim;
			framebufCreateInfo.height = dim;
			VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &framebufCreateInfo, nullptr, &offscreen.framebuffer));

			VkCommandBuffer layoutCmd = m_CommandBufferManager.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			SetImageLayout(
				layoutCmd,
				offscreen.image,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
			m_CommandBufferManager.FlushCommandBuffer(layoutCmd, m_GraphicsQueue, true);


			ShaderID equirectangularToCubeShaderID;
			if (!GetShaderID(equirectangularToCubeMatCreateInfo.shaderName, equirectangularToCubeShaderID))
			{
				PrintError("Failed to find equirectangular_to_cube shader ID!\n");
				return;
			}
			VulkanShader& equirectangularToCubeShader = m_Shaders[equirectangularToCubeShaderID];

			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
			DescriptorSetCreateInfo equirectangularToCubeDescriptorCreateInfo = {};
			equirectangularToCubeDescriptorCreateInfo.DBG_Name = "Equirectangular to cube descriptor set";
			equirectangularToCubeDescriptorCreateInfo.descriptorSet = &descriptorSet;
			equirectangularToCubeDescriptorCreateInfo.descriptorSetLayout = &m_DescriptorSetLayouts[equirectangularToCubeShaderID];
			equirectangularToCubeDescriptorCreateInfo.shaderID = equirectangularToCubeShaderID;
			equirectangularToCubeDescriptorCreateInfo.uniformBuffer = &equirectangularToCubeShader.uniformBuffer;
			equirectangularToCubeDescriptorCreateInfo.hdrEquirectangularTexture = equirectangularToCubeMat.hdrEquirectangularTexture;
			CreateDescriptorSet(&equirectangularToCubeDescriptorCreateInfo);

			std::array<VkPushConstantRange, 1> pushConstantRanges = {};
			pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			pushConstantRanges[0].offset = 0;
			pushConstantRanges[0].size = equirectangularToCubeShader.shader->pushConstantBlockSize;

			VkPipeline pipeline = VK_NULL_HANDLE;
			VkPipelineLayout pipelinelayout = VK_NULL_HANDLE;
			GraphicsPipelineCreateInfo pipelineCreateInfo = {};
			pipelineCreateInfo.DBG_Name = "Equirectangular to cube pipeline";
			pipelineCreateInfo.graphicsPipeline = &pipeline;
			pipelineCreateInfo.pipelineLayout = &pipelinelayout;
			pipelineCreateInfo.renderPass = renderPass;
			pipelineCreateInfo.shaderID = equirectangularToCubeShaderID;
			pipelineCreateInfo.vertexAttributes = equirectangularToCubeShader.shader->vertexAttributes;
			pipelineCreateInfo.topology = skyboxRenderObject->topology;
			pipelineCreateInfo.cullMode = skyboxRenderObject->cullMode;
			pipelineCreateInfo.descriptorSetLayoutIndex = equirectangularToCubeShaderID;
			pipelineCreateInfo.bSetDynamicStates = true;
			pipelineCreateInfo.bEnableAdditiveColorBlending = false;
			pipelineCreateInfo.subpass = 0;
			pipelineCreateInfo.depthTestEnable = VK_FALSE;
			pipelineCreateInfo.depthWriteEnable = VK_FALSE;
			pipelineCreateInfo.pushConstantRangeCount = pushConstantRanges.size();
			pipelineCreateInfo.pushConstants = pushConstantRanges.data();
			CreateGraphicsPipeline(&pipelineCreateInfo);

			// Render

			VkClearValue clearValues[1];
			clearValues[0].color = m_ClearColor;

			VkRenderPassBeginInfo renderPassBeginInfo = vks::renderPassBeginInfo(renderPass);
			renderPassBeginInfo.framebuffer = offscreen.framebuffer;
			renderPassBeginInfo.renderArea.extent = { dim, dim };
			renderPassBeginInfo.clearValueCount = 1;
			renderPassBeginInfo.pClearValues = clearValues;

			VkCommandBuffer cmdBuf = m_CommandBufferManager.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			BeginDebugMarkerRegion(cmdBuf, "Generate Cubemap from HDR");

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
				real viewportSize = static_cast<real>(dim * std::pow(0.5f, mip));
				VkViewport viewport = vks::viewportFlipped(viewportSize, viewportSize, 0.0f, 1.0f);
				vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

				VkRect2D scissor = vks::scissor(0u, 0u, (u32)viewportSize, (u32)viewportSize);
				vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

				for (u32 face = 0; face < 6; ++face)
				{
					vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

					// Push constants
					skyboxMat.pushConstantBlock->SetData(s_CaptureViews[face], glm::perspective(PI_DIV_TWO, 1.0f, 0.1f, (real)dim));
					vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
						skyboxMat.pushConstantBlock->size, skyboxMat.pushConstantBlock->data);

					vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

					BindDescriptorSet(&equirectangularToCubeShader, 0, cmdBuf, pipelinelayout, descriptorSet);

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
					copyRegion.extent.height = static_cast<u32>(abs(viewport.height));
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

			EndDebugMarkerRegion(cmdBuf);

			m_CommandBufferManager.FlushCommandBuffer(cmdBuf, m_GraphicsQueue, true);

			vkDestroyRenderPass(m_VulkanDevice->m_LogicalDevice, renderPass, nullptr);
			vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, offscreen.framebuffer, nullptr);
			vkFreeMemory(m_VulkanDevice->m_LogicalDevice, offscreen.memory, nullptr);
			vkDestroyImageView(m_VulkanDevice->m_LogicalDevice, offscreen.view, nullptr);
			vkDestroyImage(m_VulkanDevice->m_LogicalDevice, offscreen.image, nullptr);
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

			VkRenderPass renderPass;
			CreateRenderPass(&renderPass, format, "Generate Irradiance render pass");

			// Offscreen framebuffer
			struct {
				VkImage image;
				VkImageView view;
				VkDeviceMemory memory;
				VkFramebuffer framebuffer;
			} offscreen;

			// Color attachment
			VkImageCreateInfo offscreenImageCreateInfo = vks::imageCreateInfo();
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

			VkMemoryRequirements offscreenMemRequirements;
			vkGetImageMemoryRequirements(m_VulkanDevice->m_LogicalDevice, offscreen.image, &offscreenMemRequirements);
			VkMemoryAllocateInfo offscreenMemAlloc = vks::memoryAllocateInfo(offscreenMemRequirements.size);
			offscreenMemAlloc.memoryTypeIndex = FindMemoryType(m_VulkanDevice, offscreenMemRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(m_VulkanDevice->m_LogicalDevice, &offscreenMemAlloc, nullptr, &offscreen.memory));
			VK_CHECK_RESULT(vkBindImageMemory(m_VulkanDevice->m_LogicalDevice, offscreen.image, offscreen.memory, 0));

			VkImageViewCreateInfo colorImageView = vks::imageViewCreateInfo();
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

			VkFramebufferCreateInfo framebufCreateInfo = vks::framebufferCreateInfo(renderPass);
			framebufCreateInfo.attachmentCount = 1;
			framebufCreateInfo.pAttachments = &offscreen.view;
			framebufCreateInfo.width = dim;
			framebufCreateInfo.height = dim;
			VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &framebufCreateInfo, nullptr, &offscreen.framebuffer));

			VkCommandBuffer layoutCmd = m_CommandBufferManager.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			SetImageLayout(
				layoutCmd,
				offscreen.image,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
			m_CommandBufferManager.FlushCommandBuffer(layoutCmd, m_GraphicsQueue, true);

			ShaderID irradianceShaderID;
			if (!GetShaderID("irradiance", irradianceShaderID))
			{
				PrintError("Failed to find irradiance shader!\n");
				return;
			}
			VulkanShader& irradianceShader = m_Shaders[irradianceShaderID];

			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
			DescriptorSetCreateInfo irradianceDescriptorCreateInfo = {};
			irradianceDescriptorCreateInfo.DBG_Name = "Irradiance descriptor set";
			irradianceDescriptorCreateInfo.descriptorSet = &descriptorSet;
			irradianceDescriptorCreateInfo.descriptorSetLayout = &m_DescriptorSetLayouts[irradianceShaderID];
			irradianceDescriptorCreateInfo.shaderID = irradianceShaderID;
			irradianceDescriptorCreateInfo.uniformBuffer = &irradianceShader.uniformBuffer;
			irradianceDescriptorCreateInfo.cubemapTexture = renderObjectMat.cubemapTexture;
			CreateDescriptorSet(&irradianceDescriptorCreateInfo);

			renderObjectMat.cubemapTexture->UpdateImageDescriptor();

			std::array<VkPushConstantRange, 1> pushConstantRanges = {};
			pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			pushConstantRanges[0].offset = 0;
			pushConstantRanges[0].size = irradianceShader.shader->pushConstantBlockSize;

			VkPipelineLayout pipelinelayout = VK_NULL_HANDLE;
			VkPipeline pipeline = VK_NULL_HANDLE;
			GraphicsPipelineCreateInfo pipelineCreateInfo = {};
			pipelineCreateInfo.DBG_Name = "Irradiance pipeline";
			pipelineCreateInfo.graphicsPipeline = &pipeline;
			pipelineCreateInfo.pipelineLayout = &pipelinelayout;
			pipelineCreateInfo.renderPass = renderPass;
			pipelineCreateInfo.shaderID = irradianceShaderID;
			pipelineCreateInfo.vertexAttributes = irradianceShader.shader->vertexAttributes;
			pipelineCreateInfo.topology = skyboxRenderObject->topology;
			pipelineCreateInfo.cullMode = skyboxRenderObject->cullMode;
			pipelineCreateInfo.descriptorSetLayoutIndex = irradianceShaderID;
			pipelineCreateInfo.bSetDynamicStates = true;
			pipelineCreateInfo.bEnableAdditiveColorBlending = false;
			pipelineCreateInfo.subpass = 0;
			pipelineCreateInfo.depthTestEnable = VK_FALSE;
			pipelineCreateInfo.depthWriteEnable = VK_FALSE;
			pipelineCreateInfo.pushConstantRangeCount = pushConstantRanges.size();
			pipelineCreateInfo.pushConstants = pushConstantRanges.data();
			CreateGraphicsPipeline(&pipelineCreateInfo);

			// Render

			VkClearValue clearValues[1];
			clearValues[0].color = m_ClearColor;

			VkRenderPassBeginInfo renderPassBeginInfo = vks::renderPassBeginInfo(renderPass);
			renderPassBeginInfo.framebuffer = offscreen.framebuffer;
			renderPassBeginInfo.renderArea.extent = { dim, dim };
			renderPassBeginInfo.clearValueCount = 1;
			renderPassBeginInfo.pClearValues = clearValues;

			VkCommandBuffer cmdBuf = m_CommandBufferManager.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			BeginDebugMarkerRegion(cmdBuf, "Generate Irradiance");

			VkViewport viewport = vks::viewportFlipped((real)dim, (real)dim, 0.0f, 1.0f);
			vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

			VkRect2D scissor = vks::scissor(0u, 0u, dim, dim);
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
				real viewportSize = static_cast<real>(dim * std::pow(0.5f, mip));
				viewport = vks::viewportFlipped(viewportSize, viewportSize, 0.0f, 1.0f);
				vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

				scissor = vks::scissor(0u, 0u, (u32)viewportSize, (u32)viewportSize);
				vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

				for (u32 face = 0; face < 6; ++face)
				{
					vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

					// Push constants
					skyboxMat.pushConstantBlock->SetData(s_CaptureViews[face], glm::perspective(PI_DIV_TWO, 1.0f, 0.1f, (real)dim));
					vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
						skyboxMat.pushConstantBlock->size, skyboxMat.pushConstantBlock->data);

					vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

					BindDescriptorSet(&irradianceShader, 0, cmdBuf, pipelinelayout, descriptorSet);

					VkDeviceSize offsets[1] = { 0 };

					ShaderID shaderID = skyboxMat.shaderID;
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
					copyRegion.extent.height = static_cast<u32>(abs(viewport.height));
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

			// TODO: EZ: Use helper
			SetImageLayout(
				cmdBuf,
				renderObjectMat.irradianceTexture,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				subresourceRange);
			renderObjectMat.irradianceTexture->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			EndDebugMarkerRegion(cmdBuf);

			m_CommandBufferManager.FlushCommandBuffer(cmdBuf, m_GraphicsQueue, true);

			vkDestroyRenderPass(m_VulkanDevice->m_LogicalDevice, renderPass, nullptr);
			vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, offscreen.framebuffer, nullptr);
			vkFreeMemory(m_VulkanDevice->m_LogicalDevice, offscreen.memory, nullptr);
			vkDestroyImageView(m_VulkanDevice->m_LogicalDevice, offscreen.view, nullptr);
			vkDestroyImage(m_VulkanDevice->m_LogicalDevice, offscreen.image, nullptr);
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

			VkRenderPass renderPass;
			CreateRenderPass(&renderPass, format, "Generate Prefiltered Cube render pass");

			struct {
				VkImage image;
				VkImageView view;
				VkDeviceMemory memory;
				VkFramebuffer framebuffer;
			} offscreen;

			// Offscreen framebuffer
			{
				// Color attachment
				VkImageCreateInfo imageCreateInfo = vks::imageCreateInfo();
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

				VkMemoryRequirements memRequirements;
				vkGetImageMemoryRequirements(m_VulkanDevice->m_LogicalDevice, offscreen.image, &memRequirements);
				VkMemoryAllocateInfo memAlloc = vks::memoryAllocateInfo(memRequirements.size);
				memAlloc.memoryTypeIndex = FindMemoryType(m_VulkanDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				VK_CHECK_RESULT(vkAllocateMemory(m_VulkanDevice->m_LogicalDevice, &memAlloc, nullptr, &offscreen.memory));
				VK_CHECK_RESULT(vkBindImageMemory(m_VulkanDevice->m_LogicalDevice, offscreen.image, offscreen.memory, 0));

				VkImageViewCreateInfo colorImageView = vks::imageViewCreateInfo();
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

				VkFramebufferCreateInfo framebufCreateInfo = vks::framebufferCreateInfo(renderPass);
				framebufCreateInfo.attachmentCount = 1;
				framebufCreateInfo.pAttachments = &offscreen.view;
				framebufCreateInfo.width = dim;
				framebufCreateInfo.height = dim;
				VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &framebufCreateInfo, nullptr, &offscreen.framebuffer));

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

			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
			DescriptorSetCreateInfo prefilterDescriptorCreateInfo = {};
			prefilterDescriptorCreateInfo.DBG_Name = "Prefilter descriptor set";
			prefilterDescriptorCreateInfo.descriptorSet = &descriptorSet;
			prefilterDescriptorCreateInfo.descriptorSetLayout = &m_DescriptorSetLayouts[prefilterShaderID];
			prefilterDescriptorCreateInfo.shaderID = prefilterShaderID;
			prefilterDescriptorCreateInfo.uniformBuffer = &prefilterShader.uniformBuffer;
			prefilterDescriptorCreateInfo.cubemapTexture = renderObjectMat.cubemapTexture;
			CreateDescriptorSet(&prefilterDescriptorCreateInfo);

			std::array<VkPushConstantRange, 1> pushConstantRanges = {};
			pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			pushConstantRanges[0].offset = 0;
			pushConstantRanges[0].size = prefilterShader.shader->pushConstantBlockSize;

			VkPipelineLayout pipelinelayout = VK_NULL_HANDLE;
			VkPipeline pipeline = VK_NULL_HANDLE;
			GraphicsPipelineCreateInfo pipelineCreateInfo = {};
			pipelineCreateInfo.DBG_Name = "Prefiltered cube pipeline";
			pipelineCreateInfo.graphicsPipeline = &pipeline;
			pipelineCreateInfo.pipelineLayout = &pipelinelayout;
			pipelineCreateInfo.renderPass = renderPass;
			pipelineCreateInfo.shaderID = prefilterShaderID;
			pipelineCreateInfo.vertexAttributes = prefilterShader.shader->vertexAttributes;
			pipelineCreateInfo.topology = skyboxRenderObject->topology;
			pipelineCreateInfo.cullMode = skyboxRenderObject->cullMode;
			pipelineCreateInfo.descriptorSetLayoutIndex = prefilterShaderID;
			pipelineCreateInfo.bSetDynamicStates = true;
			pipelineCreateInfo.bEnableAdditiveColorBlending = false;
			pipelineCreateInfo.subpass = 0;
			pipelineCreateInfo.depthTestEnable = VK_FALSE;
			pipelineCreateInfo.depthWriteEnable = VK_FALSE;
			pipelineCreateInfo.pushConstantRangeCount = pushConstantRanges.size();
			pipelineCreateInfo.pushConstants = pushConstantRanges.data();
			CreateGraphicsPipeline(&pipelineCreateInfo);

			// Render

			VkClearValue clearValues[1];
			clearValues[0].color = m_ClearColor;

			VkRenderPassBeginInfo renderPassBeginInfo = vks::renderPassBeginInfo(renderPass);
			renderPassBeginInfo.framebuffer = offscreen.framebuffer;
			renderPassBeginInfo.renderArea.extent = { dim, dim };
			renderPassBeginInfo.clearValueCount = 1;
			renderPassBeginInfo.pClearValues = clearValues;

			VkCommandBuffer cmdBuf = m_CommandBufferManager.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			BeginDebugMarkerRegion(cmdBuf, "Generate Prefiltered Cube");

			VkViewport viewport = vks::viewportFlipped((real)dim, (real)dim, 0.0f, 1.0f);
			vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

			VkRect2D scissor = vks::scissor(0u, 0u, dim, dim);
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
			renderObjectMat.prefilterTexture->imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

			for (u32 mip = 0; mip < mipLevels; ++mip)
			{
				real viewportSize = static_cast<real>(dim * std::pow(0.5f, mip));
				viewport = vks::viewportFlipped(viewportSize, viewportSize, 0.0f, 1.0f);
				vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

				scissor = vks::scissor(0u, 0u, (u32)viewportSize, (u32)viewportSize);
				vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

				for (u32 face = 0; face < 6; ++face)
				{
					vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

					// Push constants
					skyboxMat.pushConstantBlock->SetData(s_CaptureViews[face], glm::perspective(PI_DIV_TWO, 1.0f, 0.1f, (real)dim));
					vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
						skyboxMat.pushConstantBlock->size, skyboxMat.pushConstantBlock->data);

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
					copyRegion.extent.height = static_cast<u32>(abs(viewport.height));
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
			renderObjectMat.prefilterTexture->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			EndDebugMarkerRegion(cmdBuf);

			m_CommandBufferManager.FlushCommandBuffer(cmdBuf, m_GraphicsQueue, true);

			vkDestroyRenderPass(m_VulkanDevice->m_LogicalDevice, renderPass, nullptr);
			vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, offscreen.framebuffer, nullptr);
			vkFreeMemory(m_VulkanDevice->m_LogicalDevice, offscreen.memory, nullptr);
			vkDestroyImageView(m_VulkanDevice->m_LogicalDevice, offscreen.view, nullptr);
			vkDestroyImage(m_VulkanDevice->m_LogicalDevice, offscreen.image, nullptr);
			vkDestroyPipeline(m_VulkanDevice->m_LogicalDevice, pipeline, nullptr);
			vkDestroyPipelineLayout(m_VulkanDevice->m_LogicalDevice, pipelinelayout, nullptr);
		}

		void VulkanRenderer::GenerateBRDFLUT()
		{
			if (!bRenderedBRDFLUT)
			{
				const VkFormat format = VK_FORMAT_R16G16_SFLOAT;
				const u32 dim = (u32)m_BRDFSize.x;
				assert(dim <= MAX_TEXTURE_DIM);

				VkRenderPass renderPass;
				CreateRenderPass(&renderPass, format, "Generate BRDF LUT render pass", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

				VkFramebufferCreateInfo framebufferCreateInfo = vks::framebufferCreateInfo(renderPass);
				framebufferCreateInfo.attachmentCount = 1;
				framebufferCreateInfo.pAttachments = &m_BRDFTexture->imageView;
				framebufferCreateInfo.width = dim;
				framebufferCreateInfo.height = dim;

				VkFramebuffer framebuffer = VK_NULL_HANDLE;
				VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &framebufferCreateInfo, nullptr, &framebuffer));

				ShaderID brdfShaderID;
				if (!GetShaderID("brdf", brdfShaderID))
				{
					PrintError("Failed to find brdf shader!\n");
				}
				VulkanShader& brdfShader = m_Shaders[brdfShaderID];

				VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
				DescriptorSetCreateInfo brdfDescriptorCreateInfo = {};
				brdfDescriptorCreateInfo.DBG_Name = "BRDF descriptor set";
				brdfDescriptorCreateInfo.descriptorSet = &descriptorSet;
				brdfDescriptorCreateInfo.descriptorSetLayout = &m_DescriptorSetLayouts[brdfShaderID];
				brdfDescriptorCreateInfo.shaderID = brdfShaderID;
				brdfDescriptorCreateInfo.uniformBuffer = &brdfShader.uniformBuffer;
				CreateDescriptorSet(&brdfDescriptorCreateInfo);

				VkPipelineLayout pipelinelayout = VK_NULL_HANDLE;
				VkPipeline pipeline = VK_NULL_HANDLE;
				GraphicsPipelineCreateInfo pipelineCreateInfo = {};
				pipelineCreateInfo.DBG_Name = "BRDF LUT pipeline";
				pipelineCreateInfo.graphicsPipeline = &pipeline;
				pipelineCreateInfo.pipelineLayout = &pipelinelayout;
				pipelineCreateInfo.renderPass = renderPass;
				pipelineCreateInfo.shaderID = brdfShaderID;
				pipelineCreateInfo.vertexAttributes = brdfShader.shader->vertexAttributes;
				pipelineCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
				pipelineCreateInfo.cullMode = VK_CULL_MODE_NONE;
				pipelineCreateInfo.descriptorSetLayoutIndex = brdfShaderID;
				pipelineCreateInfo.bSetDynamicStates = true;
				pipelineCreateInfo.bEnableAdditiveColorBlending = false;
				pipelineCreateInfo.subpass = 0;
				pipelineCreateInfo.depthTestEnable = VK_FALSE;
				pipelineCreateInfo.depthWriteEnable = VK_FALSE;
				CreateGraphicsPipeline(&pipelineCreateInfo);

				// Render

				VkClearValue clearValues[1];
				clearValues[0].color = m_ClearColor;

				VkRenderPassBeginInfo renderPassBeginInfo = vks::renderPassBeginInfo(renderPass);
				renderPassBeginInfo.renderArea.extent = { dim, dim };
				renderPassBeginInfo.clearValueCount = 1;
				renderPassBeginInfo.pClearValues = clearValues;
				renderPassBeginInfo.framebuffer = framebuffer;

				VkCommandBuffer cmdBuf = m_CommandBufferManager.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

				BeginDebugMarkerRegion(cmdBuf, "Generate BRDF LUT");

				vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport = vks::viewportFlipped((real)dim, (real)dim, 0.0f, 1.0f);
				vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

				VkRect2D scissor = vks::scissor(0, 0, dim, dim);
				vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

				vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
				vkCmdDraw(cmdBuf, 3, 1, 0, 0);
				vkCmdEndRenderPass(cmdBuf);

				EndDebugMarkerRegion(cmdBuf);

				m_CommandBufferManager.FlushCommandBuffer(cmdBuf, m_GraphicsQueue, true);

				vkQueueWaitIdle(m_GraphicsQueue);

				vkDestroyPipeline(m_VulkanDevice->m_LogicalDevice, pipeline, nullptr);
				vkDestroyPipelineLayout(m_VulkanDevice->m_LogicalDevice, pipelinelayout, nullptr);
				vkDestroyRenderPass(m_VulkanDevice->m_LogicalDevice, renderPass, nullptr);
				vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, framebuffer, nullptr);

				bRenderedBRDFLUT = true;
			}
		}

		void VulkanRenderer::CaptureSceneToCubemap(RenderID cubemapRenderID)
		{
			UNREFERENCED_PARAMETER(cubemapRenderID);
		}

		void VulkanRenderer::GeneratePrefilteredMapFromCubemap(MaterialID cubemapMaterialID)
		{
			UNREFERENCED_PARAMETER(cubemapMaterialID);
		}

		void VulkanRenderer::GenerateIrradianceSamplerFromCubemap(MaterialID cubemapMaterialID)
		{
			UNREFERENCED_PARAMETER(cubemapMaterialID);
		}

		void VulkanRenderer::CreateSSAOPipelines()
		{
			VulkanRenderObject* gBufferObject = GetRenderObject(m_GBufferQuadRenderID);

			GraphicsPipelineCreateInfo pipelineCreateInfo = {};
			pipelineCreateInfo.bSetDynamicStates = true;
			pipelineCreateInfo.topology = gBufferObject->topology;
			pipelineCreateInfo.cullMode = gBufferObject->cullMode;
			pipelineCreateInfo.bEnableColorBlending = false;

			VulkanMaterial* ssaoMaterial = &m_Materials[m_SSAOMatID];
			VulkanShader* ssaoShader = &m_Shaders[ssaoMaterial->material.shaderID];

			pipelineCreateInfo.DBG_Name = "SSAO pipeline";
			pipelineCreateInfo.graphicsPipeline = m_SSAOGraphicsPipeline.replace();
			pipelineCreateInfo.pipelineLayout = m_SSAOGraphicsPipelineLayout.replace();
			pipelineCreateInfo.shaderID = ssaoMaterial->material.shaderID;
			pipelineCreateInfo.vertexAttributes = ssaoShader->shader->vertexAttributes;
			pipelineCreateInfo.descriptorSetLayoutIndex = ssaoMaterial->descriptorSetLayoutIndex;
			pipelineCreateInfo.subpass = ssaoShader->shader->subpass;
			pipelineCreateInfo.depthWriteEnable = ssaoShader->shader->bDepthWriteEnable ? VK_TRUE : VK_FALSE;
			pipelineCreateInfo.depthCompareOp = gBufferObject->depthCompareOp;
			pipelineCreateInfo.renderPass = ssaoShader->renderPass;
			pipelineCreateInfo.fragSpecializationInfo = &m_SSAOSpecializationInfo;
			CreateGraphicsPipeline(&pipelineCreateInfo);

			VulkanMaterial* ssaoBlurMaterial = &m_Materials[m_SSAOBlurMatID];
			VulkanShader* ssaoBlurShader = &m_Shaders[ssaoBlurMaterial->material.shaderID];

			pipelineCreateInfo.DBG_Name = "SSAO Blur Horizontal pipeline";
			pipelineCreateInfo.graphicsPipeline = m_SSAOBlurHGraphicsPipeline.replace();
			pipelineCreateInfo.pipelineLayout = m_SSAOBlurGraphicsPipelineLayout.replace();
			pipelineCreateInfo.shaderID = ssaoBlurMaterial->material.shaderID;
			pipelineCreateInfo.vertexAttributes = ssaoBlurShader->shader->vertexAttributes;
			pipelineCreateInfo.descriptorSetLayoutIndex = ssaoBlurMaterial->descriptorSetLayoutIndex;
			pipelineCreateInfo.subpass = 0;
			pipelineCreateInfo.depthWriteEnable = ssaoBlurShader->shader->bDepthWriteEnable ? VK_TRUE : VK_FALSE;
			pipelineCreateInfo.renderPass = ssaoBlurShader->renderPass;
			CreateGraphicsPipeline(&pipelineCreateInfo);

			pipelineCreateInfo.DBG_Name = "SSAO Blur Vertcical pipeline";
			pipelineCreateInfo.graphicsPipeline = m_SSAOBlurVGraphicsPipeline.replace();
			pipelineCreateInfo.pipelineLayout = m_SSAOBlurGraphicsPipelineLayout.replace();
			pipelineCreateInfo.shaderID = ssaoBlurMaterial->material.shaderID;
			pipelineCreateInfo.vertexAttributes = ssaoBlurShader->shader->vertexAttributes;
			pipelineCreateInfo.descriptorSetLayoutIndex = ssaoBlurMaterial->descriptorSetLayoutIndex;
			pipelineCreateInfo.subpass = 0;
			pipelineCreateInfo.depthWriteEnable = ssaoBlurShader->shader->bDepthWriteEnable ? VK_TRUE : VK_FALSE;
			pipelineCreateInfo.renderPass = ssaoBlurShader->renderPass;
			CreateGraphicsPipeline(&pipelineCreateInfo);
		}

		void VulkanRenderer::CreateSSAODescriptorSets()
		{
			VulkanMaterial* ssaoMaterial = &m_Materials[m_SSAOMatID];
			VulkanShader* ssaoShader = &m_Shaders[ssaoMaterial->material.shaderID];

			VkDescriptorSetLayout descSetLayout = m_DescriptorSetLayouts[ssaoMaterial->material.shaderID];

			DescriptorSetCreateInfo descSetCreateInfo = {};
			descSetCreateInfo.DBG_Name = "SSAO descriptor set";
			descSetCreateInfo.descriptorSet = &m_SSAODescSet;
			descSetCreateInfo.descriptorSetLayout = &descSetLayout;
			descSetCreateInfo.shaderID = ssaoMaterial->material.shaderID;
			descSetCreateInfo.uniformBuffer = &ssaoShader->uniformBuffer;
			// TODO: Bring line out to function
			FrameBufferAttachment& normalFrameBufferAttachment = m_GBufferFrameBuf->frameBufferAttachments[0].second;
			// Depth texture is handled for us in CreateDescriptorSet
			descSetCreateInfo.ssaoNormalImageView = normalFrameBufferAttachment.view;
			descSetCreateInfo.ssaoNormalSampler = m_SSAOSampler;
			descSetCreateInfo.noiseTexture = ssaoMaterial->noiseTexture;
			CreateDescriptorSet(&descSetCreateInfo);

			VulkanMaterial* ssaoBlurMaterial = &m_Materials[m_SSAOBlurMatID];
			VulkanShader* ssaoBlurShader = &m_Shaders[ssaoBlurMaterial->material.shaderID];

			descSetLayout = m_DescriptorSetLayouts[ssaoBlurMaterial->material.shaderID];

			descSetCreateInfo = {};
			descSetCreateInfo.descriptorSet = &m_SSAOBlurHDescSet;
			descSetCreateInfo.DBG_Name = "SSAO Blur Horizontal descriptor set";
			descSetCreateInfo.descriptorSetLayout = &descSetLayout;
			descSetCreateInfo.shaderID = ssaoBlurMaterial->material.shaderID;
			descSetCreateInfo.uniformBuffer = &ssaoBlurShader->uniformBuffer;
			FrameBufferAttachment& ssaoFrameBufferAttachment = m_SSAOFrameBuf->frameBufferAttachments[0].second;
			descSetCreateInfo.ssaoImageView = ssaoFrameBufferAttachment.view;
			descSetCreateInfo.ssaoSampler = m_SSAOSampler;
			descSetCreateInfo.ssaoNormalImageView = normalFrameBufferAttachment.view;
			descSetCreateInfo.ssaoNormalSampler = m_SSAOSampler;
			// Depth texture is handled in CreateDescriptorSet
			CreateDescriptorSet(&descSetCreateInfo);

			descSetCreateInfo = {};
			descSetCreateInfo.descriptorSet = &m_SSAOBlurVDescSet;
			descSetCreateInfo.DBG_Name = "SSAO Blur Vertical descriptor set";
			descSetCreateInfo.descriptorSetLayout = &descSetLayout;
			descSetCreateInfo.shaderID = ssaoBlurMaterial->material.shaderID;
			descSetCreateInfo.uniformBuffer = &ssaoBlurShader->uniformBuffer;
			FrameBufferAttachment& ssaoBlurHFrameBufferAttachment = m_SSAOBlurHFrameBuf->frameBufferAttachments[0].second;
			descSetCreateInfo.ssaoImageView = ssaoBlurHFrameBufferAttachment.view;
			descSetCreateInfo.ssaoSampler = m_SSAOSampler;
			descSetCreateInfo.ssaoNormalImageView = normalFrameBufferAttachment.view;
			descSetCreateInfo.ssaoNormalSampler = m_SSAOSampler;
			CreateDescriptorSet(&descSetCreateInfo);
		}

		void VulkanRenderer::CreateRenderPass(VkRenderPass* outPass, VkFormat colorFormat, const char* passName,
			VkImageLayout finalLayout /* = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL */,
			VkImageLayout initialLayout /* = VK_IMAGE_LAYOUT_UNDEFINED */,
			bool bDepth /* = false */,
			VkFormat depthFormat /* = VK_FORMAT_UNDEFINED */,
			VkImageLayout finalDepthLayout /* = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL */)
		{
			// Color attachment
			VkAttachmentDescription colorAttachment = vks::attachmentDescription(colorFormat, finalLayout);
			VkAttachmentDescription depthAttachment = vks::attachmentDescription(depthFormat, finalDepthLayout);

			if (initialLayout != VK_IMAGE_LAYOUT_UNDEFINED)
			{
				colorAttachment.initialLayout = initialLayout;
				colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;

				if (bDepth)
				{
					depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
					depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
				}
			}

			VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
			VkAttachmentReference depthAttachmentRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

			std::array<VkSubpassDescription, 1> subpasses;
			subpasses[0] = {};
			subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpasses[0].colorAttachmentCount = 1;
			subpasses[0].pColorAttachments = &colorReference;
			subpasses[0].pDepthStencilAttachment = bDepth ? &depthAttachmentRef : nullptr;

			// Use subpass dependencies for layout transitions
			std::array<VkSubpassDependency, 2> dependencies;
			dependencies[0] = {};
			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			dependencies[1] = {};
			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			std::vector<VkAttachmentDescription> attachments;
			attachments.push_back(colorAttachment);

			if (bDepth)
			{
				attachments.push_back(depthAttachment);
			}

			// Renderpass
			VkRenderPassCreateInfo renderPassCreateInfo = vks::renderPassCreateInfo();
			renderPassCreateInfo.attachmentCount = attachments.size();
			renderPassCreateInfo.pAttachments = attachments.data();
			renderPassCreateInfo.subpassCount = subpasses.size();
			renderPassCreateInfo.pSubpasses = subpasses.data();
			renderPassCreateInfo.dependencyCount = dependencies.size();
			renderPassCreateInfo.pDependencies = dependencies.data();
			VK_CHECK_RESULT(vkCreateRenderPass(m_VulkanDevice->m_LogicalDevice, &renderPassCreateInfo, nullptr, outPass));

			SetRenderPassName(m_VulkanDevice, *outPass, passName);
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

		bool VulkanRenderer::LoadFont(FontMetaData& fontMetaData, bool bForceRender)
		{
			// TODO: Consolidate with GLRenderer
			FT_Library ft;
			// TODO: Only do once per session?
			if (FT_Init_FreeType(&ft) != FT_Err_Ok)
			{
				PrintError("Failed to initialize FreeType\n");
				return false;
			}

			std::vector<char> fileMemory;
			ReadFile(fontMetaData.filePath, fileMemory, true);

			std::map<i32, FontMetric*> characters;
			std::array<glm::vec2i, 4> maxPos;
			FT_Face face = {};
			if (!LoadFontMetrics(fileMemory, ft, fontMetaData, &characters, &maxPos, &face))
			{
				return false;
			}

			std::string fileName = fontMetaData.filePath;
			StripLeadingDirectories(fileName);

			BitmapFont* newFont = fontMetaData.bitmapFont;

			// TODO: Save in common place
			u32 sampleDensity = 32;
			u32 padding = 1;
			u32 spread = 5;
			//u32 totPadding = padding + spread;

			bool bUsingPreRenderedTexture = false;
			std::string textureName = std::string("Font atlas ") + fileName;
			VkFormat fontTexFormat = VK_FORMAT_R8G8B8A8_UNORM;
			if (!bForceRender)
			{
				if (FileExists(fontMetaData.renderedTextureFilePath))
				{
					VulkanTexture* fontTex = newFont->SetTexture(new VulkanTexture(m_VulkanDevice,
						m_GraphicsQueue, fontMetaData.renderedTextureFilePath, 4, false, false, false));
					fontTex->name = textureName;

					if (fontTex->CreateFromFile(fontTexFormat))
					{
						glm::vec2 fontTexSize((real)fontTex->width, (real)fontTex->height);
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

							metric->texCoord = metric->texCoord / fontTexSize;
						}
					}
					else
					{
						newFont->ClearTexture();
						delete fontTex;
						fontTex = nullptr;
					}
				}
			}

			if (!bUsingPreRenderedTexture)
			{
				// Render to glyph atlas

				glm::vec2i textureSize(
					std::max(std::max(maxPos[0].x, maxPos[1].x), std::max(maxPos[2].x, maxPos[3].x)),
					std::max(std::max(maxPos[0].y, maxPos[1].y), std::max(maxPos[2].y, maxPos[3].y)));

				VulkanTexture* fontTexColAttachment = newFont->SetTexture(new VulkanTexture(m_VulkanDevice, m_GraphicsQueue,
					textureName,textureSize.x, textureSize.y, 4));
				fontTexColAttachment->name = textureName;
				fontTexColAttachment->CreateEmpty(fontTexFormat, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
				fontTexColAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

				if (m_ComputeSDFMatID == InvalidMaterialID)
				{
					MaterialCreateInfo computeSDFMatCreateInfo = {};
					computeSDFMatCreateInfo.name = "compute SDF";
					computeSDFMatCreateInfo.shaderName = "compute_sdf";
					computeSDFMatCreateInfo.persistent = true;
					computeSDFMatCreateInfo.visibleInEditor = false;
					m_ComputeSDFMatID = InitializeMaterial(&computeSDFMatCreateInfo);
				}
				assert(m_ComputeSDFMatID != InvalidMaterialID);

				ShaderID computeSDFShaderID = m_Materials[m_ComputeSDFMatID].material.shaderID;
				VulkanShader& computeSDFShader = m_Shaders[computeSDFShaderID];

				VkRenderPass renderPass;
				CreateRenderPass(&renderPass, fontTexFormat, "Font SDF render pass");

				VkFramebufferCreateInfo framebufCreateInfo = vks::framebufferCreateInfo(renderPass);
				framebufCreateInfo.attachmentCount = 1;
				framebufCreateInfo.pAttachments = &fontTexColAttachment->imageView;
				framebufCreateInfo.width = textureSize.x;
				framebufCreateInfo.height = textureSize.y;
				VkFramebuffer framebuffer = VK_NULL_HANDLE;
				VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &framebufCreateInfo, nullptr, &framebuffer));

				VkCommandBuffer commandBuffer = m_CommandBufferManager.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

				SetImageLayout(
					commandBuffer,
					fontTexColAttachment->image,
					VK_IMAGE_ASPECT_COLOR_BIT,
					VK_IMAGE_LAYOUT_UNDEFINED,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);


				VkRenderPassBeginInfo renderPassBeginInfo = vks::renderPassBeginInfo(renderPass);
				renderPassBeginInfo.framebuffer = framebuffer;
				renderPassBeginInfo.renderArea.offset = { 0, 0 };
				renderPassBeginInfo.renderArea.extent = { (u32)textureSize.x, (u32)textureSize.y };
				renderPassBeginInfo.clearValueCount = 1;
				VkClearValue clearCol = {};
				clearCol.color = { 0.0f, 0.0f, 0.0f, 0.0f };
				renderPassBeginInfo.pClearValues = &clearCol;
				vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				BeginDebugMarkerRegion(commandBuffer, "Generate Font SDF");

				VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
				VkPipeline graphicsPipeline = VK_NULL_HANDLE;

				GraphicsPipelineCreateInfo pipelineCreateInfo = {};
				pipelineCreateInfo.DBG_Name = "Load font pipeline";
				pipelineCreateInfo.graphicsPipeline = &graphicsPipeline;
				pipelineCreateInfo.pipelineLayout = &pipelineLayout;
				pipelineCreateInfo.shaderID = computeSDFShaderID;
				pipelineCreateInfo.vertexAttributes = computeSDFShader.shader->vertexAttributes;
				pipelineCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
				pipelineCreateInfo.cullMode = VK_CULL_MODE_NONE;
				pipelineCreateInfo.descriptorSetLayoutIndex = computeSDFShaderID;
				pipelineCreateInfo.bSetDynamicStates = true;
				pipelineCreateInfo.bEnableAdditiveColorBlending = true;
				pipelineCreateInfo.subpass = 0;
				pipelineCreateInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
				pipelineCreateInfo.depthWriteEnable = VK_FALSE;
				pipelineCreateInfo.renderPass = renderPass;
				CreateGraphicsPipeline(&pipelineCreateInfo);

				VulkanRenderObject* gBufferRenderObject = GetRenderObject(m_GBufferQuadRenderID);
				VulkanMaterial* gBufferMaterial = &m_Materials[gBufferRenderObject->materialID];

				// TODO: Remove/call only once prior to any font load
				UpdateConstantUniformBuffers();

				VulkanRenderObject* gBufferObject = GetRenderObject(m_GBufferQuadRenderID);

				glm::vec2 fontTexSize((real)fontTexColAttachment->width, (real)fontTexColAttachment->height);

				u32 dynamicOffsetIndex = 0;
				std::vector<VulkanTexture*> charTextures;
				std::vector<VkDescriptorSet> descSets;
				for (auto& charPair : characters)
				{
					FontMetric* metric = charPair.second;

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
					if (FT_Bitmap_Convert(ft, &face->glyph->bitmap, &alignedBitmap, 1))
					{
						PrintError("Couldn't align free type bitmap size\n");
						continue;
					}

					u32 width = alignedBitmap.width;
					u32 height = alignedBitmap.rows;

					assert(width != 0 && height != 0);

					VulkanTexture* highResTex = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "High res tex", width, height, 1);
					charTextures.push_back(highResTex);

					++dynamicOffsetIndex;

					highResTex->bSamplerClampToBorder = true;
					highResTex->CreateFromMemory(alignedBitmap.buffer, width * height * sizeof(u8), VK_FORMAT_R8_UNORM, 1);

					glm::vec2i res = glm::vec2i(metric->width - padding * 2, metric->height - padding * 2);
					glm::vec2i viewportTL = glm::vec2i(metric->texCoord) + glm::vec2i(padding);

					VkViewport viewport = {
						(real)viewportTL.x, (real)(viewportTL.y + res.y),
						(real)res.x, -(real)res.y,
						0.1f, 100.0f
					};
					vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

					VkRect2D scissor = vks::scissor((u32)viewportTL.x, (u32)viewportTL.y, (u32)res.x, (u32)res.y);
					vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

					VkDeviceSize offsets[1] = { 0 };
					vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_VertexIndexBufferPairs[gBufferMaterial->material.shaderID].vertexBuffer->m_Buffer, offsets);

					vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

					VkDescriptorSetLayout descSetLayout = m_DescriptorSetLayouts[computeSDFShaderID];
					VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

					DescriptorSetCreateInfo descSetCreateInfo = {};
					descSetCreateInfo.DBG_Name = "Font SDF descriptor set";
					descSetCreateInfo.descriptorSet = &descriptorSet;
					descSetCreateInfo.descriptorSetLayout = &descSetLayout;
					descSetCreateInfo.shaderID = computeSDFShaderID;
					descSetCreateInfo.uniformBuffer = &computeSDFShader.uniformBuffer;
					descSetCreateInfo.albedoTexture = highResTex;
					CreateDescriptorSet(&descSetCreateInfo);
					descSets.push_back(descriptorSet);

					BindDescriptorSet(&m_Shaders[computeSDFShaderID], dynamicOffsetIndex * m_DynamicAlignment, commandBuffer, pipelineLayout, descriptorSet);

					UniformOverrides overrides = {};
					overrides.texChannel = metric->channel;
					overrides.overridenUniforms.AddUniform(U_TEX_CHANNEL);
					overrides.sdfData = glm::vec4((real)res.x, (real)res.y, (real)spread, (real)sampleDensity);
					overrides.overridenUniforms.AddUniform(U_SDF_DATA);
					UpdateDynamicUniformBuffer(m_ComputeSDFMatID, dynamicOffsetIndex * m_DynamicAlignment, MAT4_IDENTITY, &overrides);

					vkCmdDraw(commandBuffer, gBufferObject->vertexBufferData->VertexCount, 1, gBufferObject->vertexOffset, 0);

					metric->texCoord = metric->texCoord / fontTexSize;

					FT_Bitmap_Done(ft, &alignedBitmap);
				}

				vkCmdEndRenderPass(commandBuffer);

				EndDebugMarkerRegion(commandBuffer);

				VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));
				SetCommandBufferName(m_VulkanDevice, commandBuffer, "Load font command buffer");

				VkSubmitInfo submitInfo = vks::submitInfo(1, &commandBuffer);
				VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				submitInfo.pWaitDstStageMask = &waitStages;

				VK_CHECK_RESULT(vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
				VK_CHECK_RESULT(vkQueueWaitIdle(m_GraphicsQueue));

				for (VulkanTexture* highResTex : charTextures)
				{
					highResTex->Destroy();
					delete highResTex;
				}
				charTextures.clear();

				vkFreeDescriptorSets(m_VulkanDevice->m_LogicalDevice, m_DescriptorPool, descSets.size(), descSets.data());
				descSets.clear();

				vkDestroyPipeline(m_VulkanDevice->m_LogicalDevice, graphicsPipeline, nullptr);
				vkDestroyPipelineLayout(m_VulkanDevice->m_LogicalDevice, pipelineLayout, nullptr);
				vkDestroyRenderPass(m_VulkanDevice->m_LogicalDevice, renderPass, nullptr);
				vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, framebuffer, nullptr);

				std::string savedSDFTextureAbsFilePath = RelativePathToAbsolute(fontMetaData.renderedTextureFilePath);
				fontTexColAttachment->SaveToFile(savedSDFTextureAbsFilePath, ImageFormat::PNG);

				fontTexColAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			}

			FT_Done_Face(face);
			FT_Done_FreeType(ft);

			return true;
		}

		void VulkanRenderer::SetShaderCount(u32 shaderCount)
		{
			m_Shaders.clear();
			m_Shaders.reserve(shaderCount);
		}

		bool VulkanRenderer::LoadShaderCode(ShaderID shaderID)
		{
			m_Shaders.emplace_back(m_VulkanDevice->m_LogicalDevice, &m_BaseShaders[shaderID]);

			bool bSuccess = true;

			VulkanShader& vkShader = m_Shaders[shaderID];
			Shader& shader = *vkShader.shader;

			vkShader.renderPass = ResolveRenderPassType(shader.renderPassType, shader.name.c_str());
			assert(vkShader.renderPass != VK_NULL_HANDLE);

			// Sanity check
			if (vkShader.renderPass == VK_NULL_HANDLE)
			{
				PrintError("Shader %s's render pass was not set!\n", shader.name.c_str());
				bSuccess = false;
			}

			if (g_bEnableLogging_Loading)
			{
				std::string vertFileName = shader.vertexShaderFilePath;
				// TODO: EZ: Return value from helper
				StripLeadingDirectories(vertFileName);
				std::string fragFileName = shader.fragmentShaderFilePath;
				StripLeadingDirectories(fragFileName);
				std::string geomFileName = shader.geometryShaderFilePath;
				StripLeadingDirectories(geomFileName);

				Print("Loading shaders %s", vertFileName.c_str());

				if (!fragFileName.empty())
				{
					Print(" & %s", fragFileName.c_str());
				}

				if (!geomFileName.empty())
				{
					Print(" & %s", geomFileName.c_str());
				}

				Print("\n");
			}

			const bool bUseFragmentStage = !shader.fragmentShaderFilePath.empty();
			const bool bUseGeometryStage = !shader.geometryShaderFilePath.empty();

			if (!ReadFile(shader.vertexShaderFilePath, shader.vertexShaderCode, true))
			{
				PrintError("Could not find vertex shader %s\n", shader.name.c_str());
				bSuccess = false;
			}
			if (bUseFragmentStage && !ReadFile(shader.fragmentShaderFilePath, shader.fragmentShaderCode, true))
			{
				PrintError("Could not find fragment shader %s\n", shader.name.c_str());
				bSuccess = false;
			}
			if (bUseGeometryStage && !ReadFile(shader.geometryShaderFilePath, shader.geometryShaderCode, true))
			{
				PrintError("Could not find geometry shader %s\n", shader.name.c_str());
				bSuccess = false;
			}

			if (!CreateShaderModule(shader.vertexShaderCode, vkShader.vertShaderModule.replace()))
			{
				PrintError("Failed to compile vertex shader located at: %s\n", shader.vertexShaderFilePath.c_str());
			}

			if (bUseFragmentStage)
			{
				if (!CreateShaderModule(shader.fragmentShaderCode, vkShader.fragShaderModule.replace()))
				{
					PrintError("Failed to compile fragment shader located at: %s\n", shader.fragmentShaderFilePath.c_str());
				}
			}

			if (bUseGeometryStage)
			{
				if (!CreateShaderModule(shader.geometryShaderCode, vkShader.geomShaderModule.replace()))
				{
					PrintError("Failed to compile geometry shader located at: %s\n", shader.geometryShaderFilePath.c_str());
				}
			}

			return bSuccess;
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

		// TODO: Unify with DrawTextWS?
		void VulkanRenderer::DrawTextSS(VkCommandBuffer commandBuffer)
		{
			// Update dynamic text buffer
			{
				std::vector<TextVertex2D> textVerticesSS;
				UpdateTextBufferSS(textVerticesSS);

				u32 SSTextBufferByteCount = (u32)(textVerticesSS.size() * sizeof(TextVertex2D));

				if (SSTextBufferByteCount > 0)
				{
					const VulkanMaterial& fontMaterial = m_Materials[m_FontMatSSID];
					VulkanBuffer* vertexBuffer = m_VertexIndexBufferPairs[fontMaterial.material.shaderID].vertexBuffer;
					u32 copySize = std::min(SSTextBufferByteCount, (u32)vertexBuffer->m_Size);
					if (copySize < SSTextBufferByteCount)
					{
						PrintError("SS Font vertex buffer is %u bytes too small\n", SSTextBufferByteCount - copySize);
					}
					VK_CHECK_RESULT(vertexBuffer->Map(copySize));
					memcpy(vertexBuffer->m_Mapped, textVerticesSS.data(), copySize);
					vertexBuffer->Unmap();
				}
			}

			if (m_FontMatSSID == InvalidMaterialID)
			{
				return;
			}

			PROFILE_AUTO("DrawTextSS");

			bool bHasText = false;
			for (BitmapFont* font : m_FontsSS)
			{
				if (font->bufferSize > 0)
				{
					bHasText = true;
					break;
				}
			}

			if (!bHasText)
			{
				return;
			}

			VulkanMaterial& fontMaterial = m_Materials[m_FontMatSSID];
			VulkanShader& fontShader = m_Shaders[fontMaterial.material.shaderID];

			glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();

			VkDescriptorSetLayout descSetLayout = m_DescriptorSetLayouts[fontMaterial.material.shaderID];

			if (m_FontSSGraphicsPipeline == VK_NULL_HANDLE)
			{
				GraphicsPipelineCreateInfo pipelineCreateInfo = {};
				pipelineCreateInfo.DBG_Name = "Font SS pipeline";
				pipelineCreateInfo.graphicsPipeline = m_FontSSGraphicsPipeline.replace();
				pipelineCreateInfo.pipelineLayout = m_FontSSPipelineLayout.replace();
				pipelineCreateInfo.shaderID = fontMaterial.material.shaderID;
				pipelineCreateInfo.vertexAttributes = fontShader.shader->vertexAttributes;
				pipelineCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
				pipelineCreateInfo.cullMode = VK_CULL_MODE_NONE;
				pipelineCreateInfo.descriptorSetLayoutIndex = fontMaterial.material.shaderID;
				pipelineCreateInfo.bSetDynamicStates = true;
				pipelineCreateInfo.bEnableColorBlending = true;
				pipelineCreateInfo.subpass = fontShader.shader->subpass;
				pipelineCreateInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
				pipelineCreateInfo.depthWriteEnable = VK_FALSE;
				// NOTE: We ignore the font shader's render pass since we have one font shader, but
				// two passes that fonts are rendered in (Forward pass for 3D, UI pass for 2D)
				pipelineCreateInfo.renderPass = m_UIRenderPass;
				CreateGraphicsPipeline(&pipelineCreateInfo);
			}
			assert(m_FontSSGraphicsPipeline != VK_NULL_HANDLE);
			assert(m_FontSSPipelineLayout != VK_NULL_HANDLE);

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_FontSSGraphicsPipeline);

			VkViewport viewport = vks::viewportFlipped((real)frameBufferSize.x, (real)frameBufferSize.y, 0.0f, 1.0f);
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

			VkRect2D scissor = vks::scissor(0u, 0u, (u32)frameBufferSize.x, (u32)frameBufferSize.y);
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			real aspectRatio = (real)frameBufferSize.x / (real)frameBufferSize.y;
			real r = aspectRatio;
			real t = 1.0f;
			glm::mat4 ortho = glm::ortho(-r, r, -t, t);

			// TODO: Find out how font sizes actually work
			//real scale = ((real)font->GetFontSize()) / 12.0f + sin(g_SecElapsedSinceProgramStart) * 2.0f;
			glm::vec3 scaleVec(1.0f);

			glm::mat4 transformMat = glm::scale(MAT4_IDENTITY, scaleVec) * ortho;

			for (BitmapFont* font : m_FontsSS)
			{
				if (font->bufferSize > 0)
				{
					if (font->m_DescriptorSet == VK_NULL_HANDLE)
					{
						DescriptorSetCreateInfo info;
						info.DBG_Name = "Font SS descriptor set";
						info.descriptorSet = &font->m_DescriptorSet;
						info.descriptorSetLayout = &descSetLayout;
						info.albedoTexture = font->GetTexture();
						info.shaderID = fontMaterial.material.shaderID;
						info.uniformBuffer = &fontShader.uniformBuffer;
						CreateDescriptorSet(&info);
					}

					u32 dynamicOffsetIndex = 0;
					BindDescriptorSet(&fontShader, dynamicOffsetIndex * m_DynamicAlignment, commandBuffer, m_FontSSPipelineLayout, font->m_DescriptorSet);

					VulkanTexture* fontTex = font->GetTexture();
					glm::vec2 texSize(fontTex->width, fontTex->height);

					u32 packedSoftnessOpacity = Pack2FloatToU32(font->metaData.soften, font->metaData.shadowOpacity);
					glm::vec4 fontCharData(font->metaData.threshold, font->metaData.shadowOffset.x, font->metaData.shadowOffset.y, static_cast<real>(packedSoftnessOpacity));

					UniformOverrides overrides = {};
					overrides.overridenUniforms.AddUniform(U_TEX_SIZE);
					overrides.texSize = texSize;
					overrides.overridenUniforms.AddUniform(U_FONT_CHAR_DATA);
					overrides.fontCharData = fontCharData;
					UpdateDynamicUniformBuffer(m_FontMatSSID, dynamicOffsetIndex * m_DynamicAlignment, transformMat, &overrides);

					VkDeviceSize offsets[1] = { 0 };
					vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_VertexIndexBufferPairs[fontMaterial.material.shaderID].vertexBuffer->m_Buffer, offsets);

					vkCmdDraw(commandBuffer, font->bufferSize, 1, font->bufferStart, 0);
				}
			}
		}

		void VulkanRenderer::DrawTextWS(VkCommandBuffer commandBuffer)
		{
			// Update dynamic text buffer
			{
				std::vector<TextVertex3D> textVerticesWS;
				UpdateTextBufferWS(textVerticesWS);

				u32 WSTextBufferByteCount = (u32)(textVerticesWS.size() * sizeof(TextVertex2D));

				if (WSTextBufferByteCount > 0)
				{
					const VulkanMaterial& fontMaterial = m_Materials[m_FontMatWSID];
					VulkanBuffer* vertexBuffer = m_VertexIndexBufferPairs[fontMaterial.material.shaderID].vertexBuffer;
					u32 copySize = std::min(WSTextBufferByteCount, (u32)vertexBuffer->m_Size);
					if (copySize < WSTextBufferByteCount)
					{
						PrintError("SS Font vertex buffer is %u bytes too small\n", WSTextBufferByteCount - copySize);
					}
					VK_CHECK_RESULT(vertexBuffer->Map(copySize));
					memcpy(vertexBuffer->m_Mapped, textVerticesWS.data(), copySize);
					vertexBuffer->Unmap();
				}
			}

			if (m_FontMatWSID == InvalidMaterialID)
			{
				return;
			}

			PROFILE_AUTO("DrawTextWS");

			bool bHasText = false;
			for (BitmapFont* font : m_FontsWS)
			{
				if (font->bufferSize > 0)
				{
					bHasText = true;
					break;
				}
			}

			if (!bHasText)
			{
				return;
			}

			VulkanMaterial& fontMaterial = m_Materials[m_FontMatWSID];
			VulkanShader& fontShader = m_Shaders[fontMaterial.material.shaderID];

			glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();

			VkDescriptorSetLayout descSetLayout = m_DescriptorSetLayouts[fontMaterial.material.shaderID];

			if (m_FontWSGraphicsPipeline == VK_NULL_HANDLE)
			{
				GraphicsPipelineCreateInfo pipelineCreateInfo = {};
				pipelineCreateInfo.DBG_Name = "Font WS pipeline";
				pipelineCreateInfo.graphicsPipeline = m_FontWSGraphicsPipeline.replace();
				pipelineCreateInfo.pipelineLayout = m_FontWSPipelineLayout.replace();
				pipelineCreateInfo.shaderID = fontMaterial.material.shaderID;
				pipelineCreateInfo.vertexAttributes = fontShader.shader->vertexAttributes;
				pipelineCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
				pipelineCreateInfo.cullMode = VK_CULL_MODE_NONE;
				pipelineCreateInfo.descriptorSetLayoutIndex = fontMaterial.material.shaderID;
				pipelineCreateInfo.bSetDynamicStates = true;
				pipelineCreateInfo.bEnableColorBlending = true;
				pipelineCreateInfo.subpass = fontShader.shader->subpass;
				pipelineCreateInfo.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
				pipelineCreateInfo.depthWriteEnable = VK_TRUE;
				// NOTE: We ignore the font shader's render pass since we have one font shader, but
				// two passes that fonts are rendered in (Forward pass for 3D, UI pass for 2D)
				pipelineCreateInfo.renderPass = m_ForwardRenderPass;
				CreateGraphicsPipeline(&pipelineCreateInfo);
			}
			assert(m_FontWSGraphicsPipeline != VK_NULL_HANDLE);
			assert(m_FontWSPipelineLayout != VK_NULL_HANDLE);

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_FontWSGraphicsPipeline);

			VkViewport viewport = vks::viewportFlipped((real)frameBufferSize.x, (real)frameBufferSize.y, 0.0f, 1.0f);
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

			VkRect2D scissor = vks::scissor(0u, 0u, (u32)frameBufferSize.x, (u32)frameBufferSize.y);
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			glm::mat4 transformMat = g_CameraManager->CurrentCamera()->GetViewProjection();

			for (BitmapFont* font : m_FontsWS)
			{
				if (font->bufferSize > 0)
				{
					if (font->m_DescriptorSet == VK_NULL_HANDLE)
					{
						DescriptorSetCreateInfo info;
						info.DBG_Name = "Font WS descriptor set";
						info.descriptorSet = &font->m_DescriptorSet;
						info.descriptorSetLayout = &descSetLayout;
						info.albedoTexture = font->GetTexture();
						info.shaderID = fontMaterial.material.shaderID;
						info.uniformBuffer = &fontShader.uniformBuffer;
						CreateDescriptorSet(&info);
					}

					u32 dynamicOffsetIndex = 0;
					BindDescriptorSet(&fontShader, dynamicOffsetIndex * m_DynamicAlignment, commandBuffer, m_FontWSPipelineLayout, font->m_DescriptorSet);

					VulkanTexture* fontTex = font->GetTexture();
					glm::vec2 texSize(fontTex->width, fontTex->height);

					real threshold = 0.5f;
					glm::vec2 shadow(-0.01f, -0.008f);
					real soften = 0.035f;
					glm::vec4 fontCharData(threshold, shadow.x, shadow.y, soften);

					UniformOverrides overrides = {};
					overrides.overridenUniforms.AddUniform(U_TEX_SIZE);
					overrides.texSize = texSize;
					overrides.overridenUniforms.AddUniform(U_FONT_CHAR_DATA); // TODO: Does this data change per-object?
					overrides.fontCharData = fontCharData;
					UpdateDynamicUniformBuffer(m_FontMatWSID, dynamicOffsetIndex * m_DynamicAlignment, transformMat, &overrides);

					VkDeviceSize offsets[1] = { 0 };
					vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_VertexIndexBufferPairs[fontMaterial.material.shaderID].vertexBuffer->m_Buffer, offsets);

					vkCmdDraw(commandBuffer, font->bufferSize, 1, font->bufferStart, 0);
				}
			}
		}

		void VulkanRenderer::EnqueueScreenSpaceSprites()
		{
			Renderer::EnqueueScreenSpaceSprites();
		}

		// TODO: Move to base renderer
		void VulkanRenderer::EnqueueWorldSpaceSprites()
		{
			Renderer::EnqueueWorldSpaceSprites();

			BaseCamera* cam = g_CameraManager->CurrentCamera();
			if (!cam->bIsGameplayCam)
			{
				glm::vec3 scale(1.0f, -1.0f, 1.0f);

				SpriteQuadDrawInfo drawInfo = {};
				drawInfo.bScreenSpace = false;
				drawInfo.bReadDepth = true;
				drawInfo.bWriteDepth = true;
				drawInfo.scale = scale;
				drawInfo.materialID = m_SpriteMatWSID;

				glm::vec3 camPos = cam->GetPosition();
				glm::vec3 camUp = cam->GetUp();
				for (i32 i = 0; i < m_NumPointLightsEnabled; ++i)
				{
					if (m_PointLights[i].enabled)
					{
						// TODO: Sort back to front? Or clear depth and then enable depth test
						drawInfo.pos = m_PointLights[i].pos;
						drawInfo.color = glm::vec4(m_PointLights[i].color * 1.5f, 1.0f);
						drawInfo.textureID = m_PointLightIconID;
						glm::mat4 rotMat = glm::lookAt(camPos, glm::vec3(m_PointLights[i].pos), camUp);
						drawInfo.rotation = glm::conjugate(glm::toQuat(rotMat));
						EnqueueSprite(drawInfo);
					}
				}

				if (m_DirectionalLight != nullptr && m_DirectionalLight->data.enabled)
				{
					drawInfo.color = glm::vec4(m_DirectionalLight->data.color * 1.5f, 1.0f);
					drawInfo.pos = m_DirectionalLight->pos;
					drawInfo.textureID = m_DirectionalLightIconID;
					glm::mat4 rotMat = glm::lookAt(camPos, (glm::vec3)m_DirectionalLight->pos, camUp);
					drawInfo.rotation = glm::conjugate(glm::toQuat(rotMat));
					EnqueueSprite(drawInfo);

					glm::vec3 dirLightForward = m_DirectionalLight->data.dir;
					m_PhysicsDebugDrawer->drawLine(
						ToBtVec3(m_DirectionalLight->pos),
						ToBtVec3(m_DirectionalLight->pos - dirLightForward * 2.5f),
						btVector3(0.0f, 0.0f, 1.0f));
				}
			}
		}

		void VulkanRenderer::DrawSpriteBatch(const std::vector<SpriteQuadDrawInfo>& batch, VkCommandBuffer commandBuffer)
		{
			if (batch.empty())
			{
				return;
			}

			const glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
			const real aspectRatio = (real)frameBufferSize.x / (real)frameBufferSize.y;

			RenderID spriteRenderID = (batch[0].bScreenSpace ? m_Quad3DSSRenderID : m_Quad3DRenderID);
			VulkanRenderObject* spriteRenderObject = GetRenderObject(spriteRenderID);

			VkDeviceSize offsets[1] = { 0 };

			if (batch[0].bScreenSpace)
			{
				// TODO: Move to on window size changed
				real r = aspectRatio;
				real t = 1.0f;
				m_SpriteOrthoPushConstBlock->SetData(MAT4_IDENTITY, glm::ortho(-r, r, -t, t));
			}
			else
			{
				// TODO: Move to tick
				m_SpritePerspPushConstBlock->SetData(MAT4_IDENTITY, g_CameraManager->CurrentCamera()->GetProjection());
			}

			MaterialID matID = (batch[0].materialID == InvalidMaterialID ? (batch[0].bScreenSpace ? m_SpriteMatSSID : m_SpriteMatWSID) : batch[0].materialID);
			VulkanMaterial& spriteMat = m_Materials[matID];
			VulkanShader& spriteShader = m_Shaders[spriteMat.material.shaderID];

			VulkanBuffer* vertBuffer = m_VertexIndexBufferPairs[spriteMat.material.shaderID].vertexBuffer;

			// TODO: Use instancing!
			if (!m_VertexIndexBufferPairs[spriteMat.material.shaderID].useStagingBuffer)
			{
				// Copy vertex data into device memory for dynamic shaders
				u32 copySize = (u32)vertBuffer->m_Size;
				VK_CHECK_RESULT(vertBuffer->Map(copySize));
				real* dst = (real*)vertBuffer->m_Mapped;
				for (u32 i = 0; i < batch.size(); ++i)
				{
					memcpy(dst, m_Quad3DVertexBufferData.vertexData, m_Quad3DVertexBufferData.VertexBufferSize);
					dst += m_Quad3DVertexBufferData.VertexBufferSize / sizeof(real);
				}
				vertBuffer->Unmap();
			}

			u32 i = 0;
			for (const SpriteQuadDrawInfo& drawInfo : batch)
			{
				glm::vec3 translation = drawInfo.pos;
				glm::quat rotation = drawInfo.rotation;
				glm::vec3 scale = drawInfo.scale;

				if (!drawInfo.bRaw)
				{
					if (drawInfo.bScreenSpace)
					{
						glm::vec2 normalizedTranslation;
						glm::vec2 normalizedScale;
						NormalizeSpritePos(translation, drawInfo.anchor, scale, normalizedTranslation, normalizedScale);

						translation = glm::vec3(normalizedTranslation, 0.0f);
						scale = glm::vec3(normalizedScale, 1.0f);
					}
					else
					{
						translation.x /= aspectRatio;
					}
				}

				glm::mat4 model =
					glm::translate(MAT4_IDENTITY, translation) *
					glm::mat4(rotation) *
					glm::scale(MAT4_IDENTITY, scale);

				u32 dynamicUBOOffset = i * m_DynamicAlignment;

				VkPipeline pipeline = spriteRenderObject->graphicsPipeline;
				VkPipelineLayout pipelineLayout = spriteRenderObject->pipelineLayout;

				Material::PushConstantBlock* pushBlock = nullptr;
				if (drawInfo.bScreenSpace)
				{
					if (spriteShader.shader->bTextureArr)
					{
						real r = aspectRatio;
						real t = 1.0f;
						m_SpriteOrthoArrPushConstBlock->SetData(MAT4_IDENTITY, glm::ortho(-r, r, -t, t), drawInfo.textureLayer);

						pushBlock = m_SpriteOrthoArrPushConstBlock;

						pipeline = m_SpriteArrGraphicsPipeline;
						pipelineLayout = m_SpriteArrGraphicsPipelineLayout;
					}
					else
					{
						pushBlock = m_SpriteOrthoPushConstBlock;
					}
				}
				else
				{
					pushBlock = m_SpritePerspPushConstBlock;
				}


				vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushBlock->size, pushBlock->data);

				UpdateDynamicUniformBuffer(matID, dynamicUBOOffset, model, nullptr);

				vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertBuffer->m_Buffer, offsets);

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

				VkDescriptorSet descSet = VK_NULL_HANDLE;
				if (m_SpriteDescSets.find(drawInfo.textureID) != m_SpriteDescSets.end())
				{
					descSet = m_SpriteDescSets[drawInfo.textureID].descSet;
				}
				else
				{
					descSet = CreateSpriteDescSet(spriteMat.material.shaderID, drawInfo.textureID, drawInfo.textureLayer);
					m_SpriteDescSets.insert({ drawInfo.textureID, { spriteMat.material.shaderID, descSet, drawInfo.textureLayer } });
				}

				BindDescriptorSet(&spriteShader, dynamicUBOOffset, commandBuffer, pipelineLayout, descSet);

				vkCmdDraw(commandBuffer, spriteRenderObject->vertexBufferData->VertexCount, 1, spriteRenderObject->vertexOffset, 0);


				//if (drawInfo.bScreenSpace)
				//{
				//	real r = aspectRatio;
				//	real t = 1.0f;
				//	glm::mat4 viewProjection = glm::ortho(-r, r, -t, t);

				//	glUniformMatrix4fv(spriteMaterial.uniformIDs.viewProjection, 1, GL_FALSE, &viewProjection[0][0]);
				//}
				//else
				//{
				//	glm::mat4 viewProjection = g_CameraManager->CurrentCamera()->GetViewProjection();

				//	glUniformMatrix4fv(spriteMaterial.uniformIDs.viewProjection, 1, GL_FALSE, &viewProjection[0][0]);
				//}

				//if (spriteShader.shader->dynamicBufferUniforms.HasUniform(U_COLOR_MULTIPLIER))
				//{
				//	glUniform4fv(spriteMaterial.uniformIDs.colorMultiplier, 1, &drawInfo.color.r);
				//}

				//bool bEnableAlbedoSampler = (drawInfo.textureHandleID != 0 && drawInfo.bEnableAlbedoSampler);
				//if (spriteShader.shader->dynamicBufferUniforms.HasUniform(U_ALBEDO_SAMPLER))
				//{
				//	// TODO: glUniform1ui vs glUniform1i ?
				//	glUniform1ui(spriteMaterial.uniformIDs.enableAlbedoSampler, bEnableAlbedoSampler ? 1 : 0);
				//}

				//// http://www.graficaobscura.com/matrix/
				//GLint cBSLocation = glGetUniformLocation(spriteShader.program, "contrastBrightnessSaturation");
				//if (cBSLocation != -1)
				//{
				//	glm::mat4 contrastBrightnessSaturation = GetPostProcessingMatrix();
				//	glUniformMatrix4fv(cBSLocation, 1, GL_FALSE, &contrastBrightnessSaturation[0][0]);
				//}

				//glBindFramebuffer(GL_FRAMEBUFFER, drawInfo.FBO);
				//glBindRenderbuffer(GL_RENDERBUFFER, drawInfo.RBO);

				//glBindVertexArray(spriteRenderObject->VAO);
				//glBindBuffer(GL_ARRAY_BUFFER, spriteRenderObject->VBO);

				//if (bEnableAlbedoSampler)
				//{
				//	glActiveTexture(GL_TEXTURE0);
				//	glBindTexture(GL_TEXTURE_2D, drawInfo.textureHandleID);
				//}

				++i;
			}
		}

		VkRenderPass VulkanRenderer::ResolveRenderPassType(RenderPassType renderPassType, const char* shaderName /* = nullptr */)
		{
			switch (renderPassType)
			{
			case RenderPassType::SHADOW: return m_ShadowRenderPass;
			case RenderPassType::DEFERRED: return m_GBufferFrameBuf->renderPass;
			case RenderPassType::DEFERRED_COMBINE: return m_DeferredCombineRenderPass;
			case RenderPassType::FORWARD: return m_ForwardRenderPass;
			case RenderPassType::SSAO: return m_SSAORenderPass;
			case RenderPassType::SSAO_BLUR: return m_SSAOBlurHRenderPass;
			case RenderPassType::POST_PROCESS: return m_PostProcessRenderPass;
			case RenderPassType::TAA_RESOLVE: return m_TAAResolveRenderPass;
			case RenderPassType::UI: return m_UIRenderPass;
			case RenderPassType::GAMMA_CORRECT: return m_GammaCorrectRenderPass;
			default:
				PrintError("Shader's render pass type was not set!\n %s", shaderName ? shaderName: "");
				ENSURE_NO_ENTRY();
			}
			return VK_NULL_HANDLE;
		}

		void VulkanRenderer::CreateShadowResources()
		{
			// Shadow map pipeline
			VulkanMaterial* shadowMaterial = &m_Materials[m_ShadowMaterialID];
			VulkanShader* shadowShader = &m_Shaders[shadowMaterial->material.shaderID];

			VkPushConstantRange pushConstantRange = {};
			pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			pushConstantRange.offset = 0;
			pushConstantRange.size = shadowShader->shader->pushConstantBlockSize;

			GraphicsPipelineCreateInfo pipelineCreateInfo = {};
			pipelineCreateInfo.DBG_Name = "Shadow pipeline";
			pipelineCreateInfo.bSetDynamicStates = true;
			pipelineCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			pipelineCreateInfo.cullMode = VK_CULL_MODE_FRONT_BIT;
			pipelineCreateInfo.bEnableColorBlending = false;

			pipelineCreateInfo.graphicsPipeline = m_ShadowGraphicsPipeline.replace();
			pipelineCreateInfo.pipelineLayout = m_ShadowPipelineLayout.replace();
			pipelineCreateInfo.shaderID = shadowMaterial->material.shaderID;
			pipelineCreateInfo.vertexAttributes = shadowShader->shader->vertexAttributes;
			pipelineCreateInfo.descriptorSetLayoutIndex = shadowMaterial->descriptorSetLayoutIndex;
			pipelineCreateInfo.subpass = shadowShader->shader->subpass;
			pipelineCreateInfo.depthWriteEnable = shadowShader->shader->bDepthWriteEnable ? VK_TRUE : VK_FALSE;
			pipelineCreateInfo.renderPass = shadowShader->renderPass;
			pipelineCreateInfo.pushConstantRangeCount = 1;
			pipelineCreateInfo.pushConstants = &pushConstantRange;
			CreateGraphicsPipeline(&pipelineCreateInfo);

			// Shadow map descriptor set
			VkDescriptorSetLayout descSetLayout = m_DescriptorSetLayouts[shadowMaterial->material.shaderID];

			DescriptorSetCreateInfo descSetCreateInfo = {};
			descSetCreateInfo.DBG_Name = "Shadow descriptor set";
			descSetCreateInfo.descriptorSet = &m_ShadowDescriptorSet;
			descSetCreateInfo.descriptorSetLayout = &descSetLayout;
			descSetCreateInfo.shaderID = shadowMaterial->material.shaderID;
			descSetCreateInfo.uniformBuffer = &shadowShader->uniformBuffer;
			FrameBufferAttachment& normalFrameBufferAttachment = m_GBufferFrameBuf->frameBufferAttachments[0].second;
			// Depth texture is handled for us in CreateDescriptorSet
			descSetCreateInfo.ssaoNormalImageView = normalFrameBufferAttachment.view;
			descSetCreateInfo.ssaoNormalSampler = m_SSAOSampler;
			descSetCreateInfo.noiseTexture = shadowMaterial->noiseTexture;
			CreateDescriptorSet(&descSetCreateInfo);
		}

		VkDescriptorSet VulkanRenderer::CreateSpriteDescSet(ShaderID spriteShaderID, TextureID textureID, u32 layer /* = 0 */)
		{
			assert(textureID != InvalidTextureID);

			VulkanShader& spriteShader = m_Shaders[spriteShaderID];

			VkDescriptorSet descSet = VK_NULL_HANDLE;
			DescriptorSetCreateInfo descSetCreateInfo = {};
			descSetCreateInfo.DBG_Name = "Sprite descriptor set";
			descSetCreateInfo.descriptorSet = &descSet;
			descSetCreateInfo.descriptorSetLayout = &m_DescriptorSetLayouts[spriteShaderID];
			descSetCreateInfo.shaderID = spriteShaderID;
			descSetCreateInfo.uniformBuffer = &spriteShader.uniformBuffer;
			if (spriteShader.shader->bTextureArr)
			{
				descSetCreateInfo.shadowPreviewView = m_ShadowCascades[layer]->imageView;
				descSetCreateInfo.shadowSampler = m_ColorSampler;
			}
			else
			{
				descSetCreateInfo.albedoTexture = m_LoadedTextures[textureID];
			}
			CreateDescriptorSet(&descSetCreateInfo);

			return descSet;
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
			for (u32 i = 0; i < m_RenderObjects.size(); ++i)
			{
				if (m_RenderObjects[i] == nullptr)
				{
					return (RenderID)i;
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

			const u32 requestedVkVersion = VK_MAKE_VERSION(1, 1, 0);

			VkApplicationInfo appInfo = {};
			appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
			std::string applicationName = g_Window->GetTitle();
			appInfo.pApplicationName = applicationName.c_str();
			appInfo.applicationVersion = requestedVkVersion;
			appInfo.pEngineName = "Flex Engine";
			appInfo.engineVersion = FLEX_VERSION(FlexEngine::EngineVersionMajor, FlexEngine::EngineVersionMinor, FlexEngine::EngineVersionPatch);
			appInfo.apiVersion = requestedVkVersion;

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
			VK_CHECK_RESULT(glfwCreateWindowSurface(m_Instance, static_cast<GLFWWindowWrapper*>(g_Window)->GetWindow(), nullptr, m_Surface.replace()));
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

			VkPhysicalDeviceFeatures deviceFeatures = GetEnabledFeaturesForDevice(physicalDevice);

			std::vector<const char*> deviceExtensions;

			for (const char* extStr : m_RequiredDeviceExtensions)
			{
				deviceExtensions.push_back(extStr);
			}

			if (ExtensionSupported(VK_EXT_DEBUG_MARKER_EXTENSION_NAME))
			{
				deviceExtensions.push_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
				m_bEnableDebugMarkers = true;
			}

			VkDeviceCreateInfo createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

			createInfo.pQueueCreateInfos = queueCreateInfos.data();
			createInfo.queueCreateInfoCount = (u32)queueCreateInfos.size();

			createInfo.pEnabledFeatures = &deviceFeatures;

			createInfo.enabledExtensionCount = deviceExtensions.size();
			createInfo.ppEnabledExtensionNames = deviceExtensions.data();

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

		void VulkanRenderer::FindPresentInstanceExtensions()
		{
			u32 extensionCount = 0;
			vkEnumerateInstanceExtensionProperties("VK_LAYER_LUNARG_standard_validation", &extensionCount, nullptr);

			std::vector<VkExtensionProperties> availableExtensions(extensionCount);
			vkEnumerateInstanceExtensionProperties("VK_LAYER_LUNARG_standard_validation", &extensionCount, availableExtensions.data());

			for (VkExtensionProperties& prop : availableExtensions)
			{
				if (strcmp(prop.extensionName, "VK_EXT_debug_report") == 0)
				{
					bDebugUtilsExtensionPresent = true;
				}

				//Print("%s v%d\n", prop.extensionName, prop.specVersion);
			}
		}

		void SetClipboardText(void* userData, const char* text)
		{
			GLFWWindowWrapper* glfwWindow = static_cast<GLFWWindowWrapper*>(userData);
			glfwWindow->SetClipboardText(text);
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
			createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

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

			m_SSAORes = glm::vec2u((u32)(m_SwapChainExtent.width / 2.0f), (u32)(m_SwapChainExtent.height / 2.0f));

			SetSwapchainName(m_VulkanDevice, m_SwapChain, "Default Swapchain");
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

		void VulkanRenderer::CreateRenderPasses()
		{
			// TODO: Create GBuffer & offscreen render passes here too? (Currently in PrepareFrameBuffers)

			VkFormat ssaoFrameBufFormat = m_SSAOFrameBuf->frameBufferAttachments[0].second.format;
			CreateRenderPass(m_SSAORenderPass.replace(), ssaoFrameBufFormat, "SSAO render pass", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			CreateRenderPass(m_SSAOBlurHRenderPass.replace(), ssaoFrameBufFormat, "SSAO Blur Horizontal render pass", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			CreateRenderPass(m_SSAOBlurVRenderPass.replace(), ssaoFrameBufFormat, "SSAO Blur Vertical render pass", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			
			VkFormat depthFormat;
			GetSupportedDepthFormat(m_VulkanDevice->m_PhysicalDevice, &depthFormat);

			// Deferred combine render pass
			// NOTE: We don't need a depth attachment at this point, but we're rendering to the swap chain
			// frame buffers (which are created using the m_ForwardRenderPass which contains two attachments)
			// TODO: Set final depth layout to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL (triggers validation though...)
			CreateRenderPass(m_DeferredCombineRenderPass.replace(), m_OffscreenFrameBufferFormat, "Deferred combine render pass", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_UNDEFINED, true, depthFormat, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

			// Forward render pass
			// After completion FB0 is sampled in post processing pass
			CreateRenderPass(m_ForwardRenderPass.replace(), m_OffscreenFrameBufferFormat, "Forward render pass", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true, depthFormat, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

			// Post process render pass
			// After completion FB1 is sampled in TAA resolve pass
			CreateRenderPass(m_PostProcessRenderPass.replace(), m_OffscreenFrameBufferFormat, "Post Process render pass", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_IMAGE_LAYOUT_UNDEFINED, true, depthFormat, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

			// TAA resolve render pass
			// FB0 is sampled in gamma correct pass
			CreateRenderPass(m_TAAResolveRenderPass.replace(), m_OffscreenFrameBufferFormat, "TAA Resolve render pass", VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true, depthFormat, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

			// Gamma correct render pass
			CreateRenderPass(m_GammaCorrectRenderPass.replace(), m_SwapChainImageFormat, "Gamma correct render pass", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_UNDEFINED, true, depthFormat, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

			// UI render pass
			CreateRenderPass(m_UIRenderPass.replace(), m_SwapChainImageFormat, "UI render pass", VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true, depthFormat, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

		}

		void VulkanRenderer::CreateDescriptorSet(RenderID renderID)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject)
			{
				return;
			}

			VulkanMaterial* material = &m_Materials[renderObject->materialID];
			VulkanShader* shader = &m_Shaders[material->material.shaderID];

			DescriptorSetCreateInfo createInfo = {};

			char debugName[256];
			sprintf_s(debugName, "Render Object %s (render ID %u) descriptor set", renderObject->gameObject ? renderObject->gameObject->GetName().c_str() : "", renderID);
			createInfo.DBG_Name = debugName;
			createInfo.descriptorSet = &renderObject->descriptorSet;
			createInfo.descriptorSetLayout = &m_DescriptorSetLayouts[material->descriptorSetLayoutIndex];
			createInfo.shaderID = material->material.shaderID;
			createInfo.uniformBuffer = &shader->uniformBuffer;
			createInfo.normalTexture = material->normalTexture;
			createInfo.cubemapTexture = material->cubemapTexture;
			createInfo.hdrEquirectangularTexture = material->hdrEquirectangularTexture;
			createInfo.albedoTexture = material->albedoTexture;
			createInfo.metallicTexture = material->metallicTexture;
			createInfo.roughnessTexture = material->roughnessTexture;
			createInfo.irradianceTexture = material->irradianceTexture;
			createInfo.brdfLUT = material->brdfLUT;
			createInfo.prefilterTexture = material->prefilterTexture;
			createInfo.noiseTexture = material->noiseTexture;
			createInfo.bDepthSampler = shader->shader->bNeedDepthSampler;

			if (shader->shader->constantBufferUniforms.HasUniform(U_SHADOW_SAMPLER))
			{
				createInfo.shadowSampler = m_DepthSampler;
				// TODO: Use blank texture array here to appease validation warnings
				createInfo.shadowImageView = m_DirectionalLight ? m_ShadowImageView : m_BlankTexture->imageView;
			}

			if (shader->shader->constantBufferUniforms.HasUniform(U_SSAO_FINAL_SAMPLER))
			{
				VkImageView ssaoView = m_BlankTexture->imageView;
				if (m_SSAOSamplingData.ssaoEnabled)
				{
					if (m_bSSAOBlurEnabled)
					{
						ssaoView = m_SSAOBlurVFrameBuf->frameBufferAttachments[0].second.view;
					}
					else
					{
						ssaoView = m_SSAOFrameBuf->frameBufferAttachments[0].second.view;
					}
				}
				createInfo.ssaoFinalImageView = ssaoView;
				createInfo.ssaoFinalSampler = m_SSAOSampler;
			}

			for (size_t i = 0; i < material->material.sampledFrameBuffers.size(); ++i)
			{
				createInfo.frameBufferViews.emplace_back(static_cast<u32>(U_FB_0_SAMPLER + i), static_cast<VkImageView*>(material->material.sampledFrameBuffers[i].second));
			}

			CreateDescriptorSet(&createInfo);
		}

		void VulkanRenderer::CreateDescriptorSet(DescriptorSetCreateInfo* createInfo)
		{
			VkDescriptorSetLayout layouts[] = { *createInfo->descriptorSetLayout };
			VkDescriptorSetAllocateInfo allocInfo = vks::descriptorSetAllocateInfo(m_DescriptorPool, layouts, 1);

			if (*createInfo->descriptorSet)
			{
				vkFreeDescriptorSets(m_VulkanDevice->m_LogicalDevice, m_DescriptorPool, 1, createInfo->descriptorSet);
			}
			// TODO: Optimization: Allocate all required descriptor sets in one call rather than 1 at a time
			VK_CHECK_RESULT(vkAllocateDescriptorSets(m_VulkanDevice->m_LogicalDevice, &allocInfo, createInfo->descriptorSet));

			Uniforms constantBufferUniforms = m_Shaders[createInfo->shaderID].shader->constantBufferUniforms;
			Uniforms dynamicBufferUniforms = m_Shaders[createInfo->shaderID].shader->dynamicBufferUniforms;

			struct DescriptorSetInfo
			{
				DescriptorSetInfo(u64 uniform,
					VkDescriptorType descriptorType,
					VkBuffer buffer,
					VkDeviceSize bufferSize,
					VkImageView imageView = VK_NULL_HANDLE,
					VkSampler imageSampler = VK_NULL_HANDLE,
					VkDescriptorImageInfo* imageInfoPtr = nullptr) :
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

				VkBuffer buffer;
				VkDeviceSize bufferSize;

				VkImageView imageView = VK_NULL_HANDLE;
				VkSampler imageSampler = VK_NULL_HANDLE;

				// Fill this member in to override the default ImageInfo below
				VkDescriptorImageInfo* imageInfoPtr = nullptr;

				// These should not be filled in, they are just here so that they are kept around until the call
				// to vkUpdateDescriptorSets, and can not be local to the following for loop
				VkDescriptorBufferInfo bufferInfo;
				VkDescriptorImageInfo imageInfo;
			};

			// TODO: Refactor this.
			//		- Rather than require textures, views should be allowed on their own
			//		- Samplers should optional
			//		- Remove info descriptors
			std::vector<DescriptorSetInfo> descriptorSets = {
				{ U_UNIFORM_BUFFER_CONSTANT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				createInfo->uniformBuffer->constantBuffer.m_Buffer, createInfo->uniformBuffer->constantData.size },

				{ U_UNIFORM_BUFFER_DYNAMIC, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				// TODO: Size should be something else... (createInfo->uniformBuffer->fullDynamicBufferSize?)
				createInfo->uniformBuffer->dynamicBuffer.m_Buffer, sizeof(VulkanUniformBufferObjectData) * m_RenderObjects.size() },

				{ U_ALBEDO_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->shadowPreviewView != VK_NULL_HANDLE ? createInfo->shadowPreviewView : (createInfo->albedoTexture ? *&createInfo->albedoTexture->imageView : VK_NULL_HANDLE),
				createInfo->shadowPreviewView != VK_NULL_HANDLE ? createInfo->shadowSampler : (createInfo->albedoTexture ? *&createInfo->albedoTexture->sampler : VK_NULL_HANDLE),
				createInfo->shadowPreviewView != VK_NULL_HANDLE ? nullptr : (createInfo->albedoTexture ? &createInfo->albedoTexture->imageInfoDescriptor : nullptr) },

				{ U_METALLIC_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->metallicTexture ? *&createInfo->metallicTexture->imageView : VK_NULL_HANDLE,
				createInfo->metallicTexture ? *&createInfo->metallicTexture->sampler : VK_NULL_HANDLE,
				createInfo->metallicTexture ? &createInfo->metallicTexture->imageInfoDescriptor : nullptr },

				{ U_ROUGHNESS_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->roughnessTexture ? *&createInfo->roughnessTexture->imageView : VK_NULL_HANDLE,
				createInfo->roughnessTexture ? *&createInfo->roughnessTexture->sampler : VK_NULL_HANDLE,
				createInfo->roughnessTexture ? &createInfo->roughnessTexture->imageInfoDescriptor : nullptr },

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

				{ U_DEPTH_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				*&m_GBufferDepthAttachment->view,
				*&m_DepthSampler,
				nullptr },

				{ U_SSAO_RAW_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				*&createInfo->ssaoImageView,
				*&createInfo->ssaoSampler,
				nullptr },

				{ U_SSAO_FINAL_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				*&createInfo->ssaoFinalImageView,
				*&createInfo->ssaoFinalSampler,
				nullptr },

				{ U_SSAO_NORMAL_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				*&createInfo->ssaoNormalImageView,
				*&createInfo->ssaoNormalSampler,
				nullptr },

				{ U_NOISE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->noiseTexture ? *&createInfo->noiseTexture->imageView : VK_NULL_HANDLE,
				createInfo->noiseTexture ? *&createInfo->noiseTexture->sampler : VK_NULL_HANDLE,
				createInfo->noiseTexture ? &createInfo->noiseTexture->imageInfoDescriptor : nullptr },

				{ U_SHADOW_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				*&createInfo->shadowImageView,
				*&createInfo->shadowSampler,
				nullptr },

				{ U_SCENE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				*&createInfo->sceneImageView,
				*&createInfo->sceneSampler,
				nullptr },

				{ U_HISTORY_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				*&createInfo->historyBufferImageView,
				*&createInfo->historyBufferSampler,
				nullptr },
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

					if (descriptorSetInfo.bufferInfo.buffer != VK_NULL_HANDLE)
					{
						writeDescriptorSets.push_back(vks::writeDescriptorSet(*createInfo->descriptorSet, descriptorSetInfo.descriptorType, binding, &descriptorSetInfo.bufferInfo));
					}
					else
					{
						writeDescriptorSets.push_back(vks::writeDescriptorSet(*createInfo->descriptorSet, descriptorSetInfo.descriptorType, binding, &descriptorSetInfo.imageInfo));
					}

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

			if (createInfo->DBG_Name)
			{
				SetDescriptorSetName(m_VulkanDevice, *createInfo->descriptorSet, createInfo->DBG_Name);
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

			// TODO: Specify stage flags per shader
			static DescriptorSetInfo descriptorSets[] = {
				{ U_UNIFORM_BUFFER_CONSTANT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_GEOMETRY_BIT },

				{ U_UNIFORM_BUFFER_DYNAMIC, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_GEOMETRY_BIT },

				{ U_ALBEDO_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
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
			};

			std::vector<VkDescriptorSetLayoutBinding> bindings;

			for (DescriptorSetInfo& descSetInfo : descriptorSets)
			{
				if (shader->shader->constantBufferUniforms.HasUniform(descSetInfo.uniform) ||
					shader->shader->dynamicBufferUniforms.HasUniform(descSetInfo.uniform))
				{
					VkDescriptorSetLayoutBinding descSetLayoutBinding = {};
					descSetLayoutBinding.binding = bindings.size();
					descSetLayoutBinding.descriptorCount = 1;
					descSetLayoutBinding.descriptorType = descSetInfo.descriptorType;
					descSetLayoutBinding.stageFlags = descSetInfo.shaderStageFlags;
					bindings.push_back(descSetLayoutBinding);
				}
			}

			VkDescriptorSetLayoutCreateInfo layoutInfo = vks::descriptorSetLayoutCreateInfo(bindings);

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

			for (const auto& materialObj : BaseScene::s_ParsedMaterials)
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
				if (matPair.second.material.visibleInEditor)
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
				if (m_Shaders[i].shader->name.compare(shaderName) == 0)
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
			UNREFERENCED_PARAMETER(bSetCubemapRenderPass);

			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject || !renderObject->vertexBufferData)
			{
				return;
			}

			VulkanMaterial* material = &m_Materials[renderObject->materialID];
			VulkanShader& shader = m_Shaders[material->material.shaderID];

			GraphicsPipelineCreateInfo pipelineCreateInfo = {};
			char debugName[256];
			sprintf_s(debugName, "Render Object %s (render ID %u) pipeline", renderObject->gameObject ? renderObject->gameObject->GetName().c_str() : "", renderID);
			pipelineCreateInfo.DBG_Name = debugName;
			pipelineCreateInfo.pipelineLayout = renderObject->pipelineLayout.replace();
			pipelineCreateInfo.graphicsPipeline = renderObject->graphicsPipeline.replace();
			pipelineCreateInfo.shaderID = material->material.shaderID;
			pipelineCreateInfo.vertexAttributes = shader.shader->vertexAttributes;
			pipelineCreateInfo.topology = renderObject->topology;
			pipelineCreateInfo.cullMode = renderObject->cullMode;
			pipelineCreateInfo.descriptorSetLayoutIndex = material->descriptorSetLayoutIndex;
			pipelineCreateInfo.bSetDynamicStates = renderObject->bSetDynamicStates;
			pipelineCreateInfo.bEnableColorBlending = shader.shader->bTranslucent;
			pipelineCreateInfo.subpass = shader.shader->subpass;
			pipelineCreateInfo.depthWriteEnable = shader.shader->bDepthWriteEnable ? VK_TRUE : VK_FALSE;
			pipelineCreateInfo.depthCompareOp = renderObject->depthCompareOp;
			pipelineCreateInfo.renderPass = (renderObject->renderPassOverride == RenderPassType::_NONE ? shader.renderPass : ResolveRenderPassType(renderObject->renderPassOverride));

			VkPushConstantRange pushConstantRange = {};
			if (m_Shaders[material->material.shaderID].shader->bNeedPushConstantBlock)
			{
				pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
				pushConstantRange.offset = 0;
				pushConstantRange.size = material->material.pushConstantBlock->size;
				pipelineCreateInfo.pushConstantRangeCount = 1;
				pipelineCreateInfo.pushConstants = &pushConstantRange;
			}

			CreateGraphicsPipeline(&pipelineCreateInfo);
		}

		void VulkanRenderer::CreateGraphicsPipeline(GraphicsPipelineCreateInfo* createInfo)
		{
			VulkanShader& shader = m_Shaders[createInfo->shaderID];

			std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

			const bool bUseFragmentStage = !shader.shader->fragmentShaderFilePath.empty();
			const bool bUseGeometryStage = !shader.shader->geometryShaderFilePath.empty();

			VkPipelineShaderStageCreateInfo vertShaderStageInfo = vks::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, shader.vertShaderModule);
			shaderStages.push_back(vertShaderStageInfo);

			VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
			if (bUseFragmentStage)
			{
				fragShaderStageInfo = vks::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, shader.fragShaderModule);
				fragShaderStageInfo.pSpecializationInfo = createInfo->fragSpecializationInfo;
				shaderStages.push_back(fragShaderStageInfo);
			}

			VkPipelineShaderStageCreateInfo geomShaderStageInfo = {};
			if (bUseGeometryStage)
			{
				geomShaderStageInfo = vks::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_GEOMETRY_BIT, shader.geomShaderModule);
				shaderStages.push_back(geomShaderStageInfo);
			}

			const u32 vertexStride = CalculateVertexStride(createInfo->vertexAttributes);
			VkVertexInputBindingDescription bindingDescription = vks::vertexInputBindingDescription(0, vertexStride, VK_VERTEX_INPUT_RATE_VERTEX);

			std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
			GetVertexAttributeDescriptions(createInfo->vertexAttributes, attributeDescriptions);

			VkPipelineVertexInputStateCreateInfo vertexInputInfo;
			if (vertexStride > 0)
			{
				vertexInputInfo = vks::pipelineVertexInputStateCreateInfo(1, &bindingDescription, attributeDescriptions.size(), attributeDescriptions.data());
			}
			else
			{
				vertexInputInfo = vks::pipelineVertexInputStateCreateInfo(0, nullptr, 0, nullptr);
			}

			VkPipelineInputAssemblyStateCreateInfo inputAssembly = vks::pipelineInputAssemblyStateCreateInfo(createInfo->topology, 0, VK_FALSE);

			VkViewport viewport = vks::viewportFlipped((real)m_SwapChainExtent.width, (real)m_SwapChainExtent.height, 0.0f, 1.0f);
			VkRect2D scissor = vks::scissor(0, 0, m_SwapChainExtent.width, m_SwapChainExtent.height);

			VkPipelineViewportStateCreateInfo viewportState = vks::pipelineViewportStateCreateInfo(1, 1);
			if (!createInfo->bSetDynamicStates)
			{
				viewportState.pViewports = &viewport;
				viewportState.pScissors = &scissor;
			}

			VkPipelineRasterizationStateCreateInfo rasterizer = vks::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, createInfo->cullMode, VK_FRONT_FACE_CLOCKWISE);

			VkPipelineMultisampleStateCreateInfo multisampling = vks::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);

			std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments = {};
			colorBlendAttachments.resize(shader.shader->numAttachments, {});
			for (VkPipelineColorBlendAttachmentState& colorBlendAttachment : colorBlendAttachments)
			{
				colorBlendAttachment.colorWriteMask = 0xF; // RGBA

				if (createInfo->bEnableColorBlending)
				{
					colorBlendAttachment.blendEnable = VK_TRUE;
					colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
					colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
				}
				else if (createInfo->bEnableAdditiveColorBlending)
				{
					colorBlendAttachment.blendEnable = VK_TRUE;
					colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
					colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
					colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
					colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
					colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
					colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
				}
				else
				{
					colorBlendAttachment.blendEnable = VK_FALSE;
				}
			}

			VkPipelineColorBlendStateCreateInfo colorBlending = vks::pipelineColorBlendStateCreateInfo(colorBlendAttachments);
			colorBlending.logicOpEnable = VK_FALSE;
			colorBlending.logicOp = VK_LOGIC_OP_COPY;
			colorBlending.blendConstants[0] = 0.0f;
			colorBlending.blendConstants[1] = 0.0f;
			colorBlending.blendConstants[2] = 0.0f;
			colorBlending.blendConstants[3] = 0.0f;

			VkDescriptorSetLayout setLayout = m_DescriptorSetLayouts[createInfo->descriptorSetLayoutIndex];
			VkPipelineLayoutCreateInfo pipelineLayoutInfo = vks::pipelineLayoutCreateInfo(1, &setLayout);
			pipelineLayoutInfo.pushConstantRangeCount = createInfo->pushConstantRangeCount;
			pipelineLayoutInfo.pPushConstantRanges = createInfo->pushConstants;

			assert(createInfo->pushConstantRangeCount == 0 || createInfo->pushConstants != nullptr);

			if (createInfo->pipelineCache)
			{
				vkDestroyPipelineCache(m_VulkanDevice->m_LogicalDevice, *createInfo->pipelineCache, nullptr);

				VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
				pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
				VK_CHECK_RESULT(vkCreatePipelineCache(m_VulkanDevice->m_LogicalDevice, &pipelineCacheCreateInfo, nullptr, createInfo->pipelineCache));
			}

			vkDestroyPipelineLayout(m_VulkanDevice->m_LogicalDevice, *createInfo->pipelineLayout, nullptr);
			VK_CHECK_RESULT(vkCreatePipelineLayout(m_VulkanDevice->m_LogicalDevice, &pipelineLayoutInfo, nullptr, createInfo->pipelineLayout));

			VkPipelineDepthStencilStateCreateInfo depthStencil = vks::pipelineDepthStencilStateCreateInfo(createInfo->depthTestEnable, createInfo->depthWriteEnable, createInfo->depthCompareOp, createInfo->stencilTestEnable);

			VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dynamicState = vks::pipelineDynamicStateCreateInfo(2, dynamicStates);
			VkPipelineDynamicStateCreateInfo* pDynamicState = nullptr;
			if (createInfo->bSetDynamicStates)
			{
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

			if (*createInfo->graphicsPipeline != VK_NULL_HANDLE)
			{
				vkDestroyPipeline(m_VulkanDevice->m_LogicalDevice, *createInfo->graphicsPipeline, nullptr);
			}

			VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_VulkanDevice->m_LogicalDevice, pipelineCache, 1, &pipelineInfo, nullptr, createInfo->graphicsPipeline));
			SetPipelineName(m_VulkanDevice, *createInfo->graphicsPipeline, createInfo->DBG_Name);
		}

		void VulkanRenderer::CreateDepthResources()
		{
			m_SwapChainDepthAttachment->CreateImage(m_SwapChainExtent.width, m_SwapChainExtent.height);
			m_SwapChainDepthAttachment->CreateImageView();

			// Depth will be copied from offscreen depth buffer after deferred combine pass
			m_SwapChainDepthAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, m_GraphicsQueue);

			m_GBufferDepthAttachment->CreateImage(m_SwapChainExtent.width, m_SwapChainExtent.height);
			m_GBufferDepthAttachment->CreateImageView();
			m_GBufferDepthAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, m_GraphicsQueue);

			m_OffscreenDepthAttachment0->bIsTransferedDst = true;
			m_OffscreenDepthAttachment0->CreateImage(m_SwapChainExtent.width, m_SwapChainExtent.height);
			m_OffscreenDepthAttachment0->CreateImageView();
			m_OffscreenDepthAttachment0->TransitionToLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, m_GraphicsQueue);

			//m_OffscreenDepthAttachment1->bIsTransferedDst = true;
			m_OffscreenDepthAttachment1->CreateImage(m_SwapChainExtent.width, m_SwapChainExtent.height);
			m_OffscreenDepthAttachment1->CreateImageView();
			m_OffscreenDepthAttachment1->TransitionToLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, m_GraphicsQueue);

			m_CubemapDepthAttachment->CreateImage(m_GBufferCubemapFrameBuffer->width, m_GBufferCubemapFrameBuffer->height);
			m_CubemapDepthAttachment->CreateImageView();
			m_CubemapDepthAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, m_GraphicsQueue);
		}

		void VulkanRenderer::CreateFramebuffers()
		{
			m_SwapChainFramebuffers.resize(m_SwapChainImageViews.size(), VDeleter<VkFramebuffer>{ m_VulkanDevice->m_LogicalDevice, vkDestroyFramebuffer });

			for (u32 i = 0; i < m_SwapChainImageViews.size(); ++i)
			{
				std::array<VkImageView, 2> attachments = {
					m_SwapChainImageViews[i],
					m_SwapChainDepthAttachment->view
				};

				VkFramebufferCreateInfo framebufferInfo =  vks::framebufferCreateInfo(m_GammaCorrectRenderPass);
				framebufferInfo.attachmentCount = attachments.size();
				framebufferInfo.pAttachments = attachments.data();
				framebufferInfo.width = m_SwapChainExtent.width;
				framebufferInfo.height = m_SwapChainExtent.height;
				VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &framebufferInfo, nullptr, m_SwapChainFramebuffers[i].replace()));

				char name[256];
				sprintf_s(name, "Swapchain %u", i);
				SetFramebufferName(m_VulkanDevice, m_SwapChainFramebuffers[i], name);
			}
		}

		void VulkanRenderer::PrepareFrameBuffers()
		{
			m_GBufferFrameBuf->width = m_SwapChainExtent.width;
			m_GBufferFrameBuf->height = m_SwapChainExtent.height;

			m_OffscreenFrameBuffer0->width = m_SwapChainExtent.width;
			m_OffscreenFrameBuffer0->height = m_SwapChainExtent.height;

			m_OffscreenFrameBuffer1->width = m_SwapChainExtent.width;
			m_OffscreenFrameBuffer1->height = m_SwapChainExtent.height;

			m_HistoryBuffer->width = m_SwapChainExtent.width;
			m_HistoryBuffer->height = m_SwapChainExtent.height;

			m_SSAORes = glm::vec2u((u32)(m_SwapChainExtent.width / 2.0f), (u32)(m_SwapChainExtent.height / 2.0f));

			m_SSAOFrameBuf->width = m_SSAORes.x;
			m_SSAOFrameBuf->height = m_SSAORes.y;

			m_SSAOBlurHFrameBuf->width = m_SwapChainExtent.width;
			m_SSAOBlurHFrameBuf->height = m_SwapChainExtent.height;

			m_SSAOBlurVFrameBuf->width = m_SwapChainExtent.width;
			m_SSAOBlurVFrameBuf->height = m_SwapChainExtent.height;

			const size_t frameBufferColorAttachmentCount = m_GBufferFrameBuf->frameBufferAttachments.size();

			//  GBuffer render pass
			{
				// Color attachments
				for (u32 i = 0; i < frameBufferColorAttachmentCount; ++i)
				{
					char dbgImageName[256];
					char dbgImageViewName[256];
					sprintf_s(dbgImageName, "GBuffer %u image", i);
					sprintf_s(dbgImageViewName, "GBuffer %u image view", i);
					CreateAttachment(m_VulkanDevice, m_GBufferFrameBuf, i, dbgImageName, dbgImageViewName);
				}

				// Find a suitable depth format
				VkFormat attDepthFormat;
				VkBool32 validDepthFormat = GetSupportedDepthFormat(m_VulkanDevice->m_PhysicalDevice, &attDepthFormat);
				assert(validDepthFormat);

				std::vector<VkAttachmentDescription> attachmentDescs(frameBufferColorAttachmentCount + 1); // + 1 for depth attachment

				for (u32 i = 0; i < frameBufferColorAttachmentCount; ++i)
				{
					attachmentDescs[i] = vks::attachmentDescription(m_GBufferFrameBuf->frameBufferAttachments[i].second.format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				}
				// Depth is read by SSAO after gbuffer fill
				attachmentDescs[frameBufferColorAttachmentCount] = vks::attachmentDescription(m_GBufferDepthAttachment->format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

				std::vector<VkAttachmentReference> colorReferences;
				for (u32 i = 0; i < frameBufferColorAttachmentCount; ++i)
				{
					colorReferences.push_back({ i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
				}

				VkAttachmentReference depthReference = { frameBufferColorAttachmentCount, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

				VkSubpassDescription subpass = {};
				subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
				subpass.pColorAttachments = colorReferences.data();
				subpass.colorAttachmentCount = static_cast<u32>(colorReferences.size());
				subpass.pDepthStencilAttachment = &depthReference;

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

				VkRenderPassCreateInfo renderPassInfo = vks::renderPassCreateInfo();
				renderPassInfo.pAttachments = attachmentDescs.data();
				renderPassInfo.attachmentCount = static_cast<u32>(attachmentDescs.size());
				renderPassInfo.subpassCount = 1;
				renderPassInfo.pSubpasses = &subpass;
				renderPassInfo.dependencyCount = dependencies.size();
				renderPassInfo.pDependencies = dependencies.data();

				VK_CHECK_RESULT(vkCreateRenderPass(m_VulkanDevice->m_LogicalDevice, &renderPassInfo, nullptr, m_GBufferFrameBuf->renderPass.replace()));
				SetRenderPassName(m_VulkanDevice, m_GBufferFrameBuf->renderPass, "GBuffer render pass");
			}

			//  Offscreen render passes
			{
				// TODO: Don't create these?
				CreateRenderPass(m_OffscreenFrameBuffer0->renderPass.replace(), m_OffscreenFrameBufferFormat, "Offscreen 0 render pass", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					VK_IMAGE_LAYOUT_UNDEFINED, true, m_OffscreenDepthAttachment0->format, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
				CreateRenderPass(m_OffscreenFrameBuffer1->renderPass.replace(), m_OffscreenFrameBufferFormat, "Offscreen 1 render pass", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					VK_IMAGE_LAYOUT_UNDEFINED, true, m_OffscreenDepthAttachment1->format, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
			}

			// TODO: Make render pass helper support depth-only passes
			// Shadow render pass
			{
				// Color attachment
				VkAttachmentDescription depthAttachment = vks::attachmentDescription(m_ShadowBufFormat, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

				VkAttachmentReference depthAttachmentRef = { 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

				std::array<VkSubpassDescription, 1> subpasses;
				subpasses[0] = {};
				subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
				subpasses[0].pDepthStencilAttachment = &depthAttachmentRef;

				std::array<VkSubpassDependency, 2> dependencies;
				dependencies[0] = {};
				dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
				dependencies[0].dstSubpass = 0;
				dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
				dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
				dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

				dependencies[1] = {};
				dependencies[1].srcSubpass = 0;
				dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
				dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
				dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
				dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

				std::vector<VkAttachmentDescription> attachments;
				attachments.push_back(depthAttachment);

				// Renderpass
				VkRenderPassCreateInfo renderPassCreateInfo = vks::renderPassCreateInfo();
				renderPassCreateInfo.attachmentCount = attachments.size();
				renderPassCreateInfo.pAttachments = attachments.data();
				renderPassCreateInfo.subpassCount = subpasses.size();
				renderPassCreateInfo.pSubpasses = subpasses.data();
				renderPassCreateInfo.dependencyCount = dependencies.size();
				renderPassCreateInfo.pDependencies = dependencies.data();
				VK_CHECK_RESULT(vkCreateRenderPass(m_VulkanDevice->m_LogicalDevice, &renderPassCreateInfo, nullptr, m_ShadowRenderPass.replace()));
				SetRenderPassName(m_VulkanDevice, m_ShadowRenderPass, "Shadow render pass");
			}

			// GBuffer frame buffer
			{
				std::vector<VkImageView> attachments;
				for (u32 i = 0; i < frameBufferColorAttachmentCount; ++i)
				{
					attachments.push_back(m_GBufferFrameBuf->frameBufferAttachments[i].second.view);
				}
				attachments.push_back(m_GBufferDepthAttachment->view);

				VkFramebufferCreateInfo gbufferFramebufferCreateInfo = vks::framebufferCreateInfo(m_GBufferFrameBuf->renderPass);
				gbufferFramebufferCreateInfo.pAttachments = attachments.data();
				gbufferFramebufferCreateInfo.attachmentCount = static_cast<u32>(attachments.size());
				gbufferFramebufferCreateInfo.width = m_GBufferFrameBuf->width;
				gbufferFramebufferCreateInfo.height = m_GBufferFrameBuf->height;
				VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &gbufferFramebufferCreateInfo, nullptr, m_GBufferFrameBuf->frameBuffer.replace()));
				SetFramebufferName(m_VulkanDevice, m_GBufferFrameBuf->frameBuffer, "GBuffer frame buffer");
			}

			// Offscreen frame buffers
			{
				assert(m_OffscreenFrameBuffer0->width == m_OffscreenFrameBuffer1->width);
				assert(m_OffscreenFrameBuffer0->height == m_OffscreenFrameBuffer1->height);

				CreateAttachment(m_VulkanDevice, m_OffscreenFrameBuffer0, 0, "Offscreen 0 image", "Offscreen 0 image view");
				CreateAttachment(m_VulkanDevice, m_OffscreenFrameBuffer1, 0, "Offscreen 1 image", "Offscreen 1 image view");

				std::vector<VkImageView> attachments;
				attachments.push_back(m_OffscreenFrameBuffer0->frameBufferAttachments[0].second.view);
				attachments.push_back(m_OffscreenDepthAttachment0->view);

				VkFramebufferCreateInfo offscreenFramebufferCreateInfo = vks::framebufferCreateInfo(m_OffscreenFrameBuffer0->renderPass);
				offscreenFramebufferCreateInfo.pAttachments = attachments.data();
				offscreenFramebufferCreateInfo.attachmentCount = static_cast<u32>(attachments.size());
				offscreenFramebufferCreateInfo.width = m_OffscreenFrameBuffer0->width;
				offscreenFramebufferCreateInfo.height = m_OffscreenFrameBuffer0->height;
				VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &offscreenFramebufferCreateInfo, nullptr, m_OffscreenFrameBuffer0->frameBuffer.replace()));
				SetFramebufferName(m_VulkanDevice, m_OffscreenFrameBuffer0->frameBuffer, "Offscreen 0");

				offscreenFramebufferCreateInfo.renderPass = m_OffscreenFrameBuffer1->renderPass;
				attachments[0] = m_OffscreenFrameBuffer1->frameBufferAttachments[0].second.view;
				attachments[1] = m_OffscreenDepthAttachment1->view;
				VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &offscreenFramebufferCreateInfo, nullptr, m_OffscreenFrameBuffer1->frameBuffer.replace()));
				SetFramebufferName(m_VulkanDevice, m_OffscreenFrameBuffer1->frameBuffer, "Offscreen 1");
			}

			CreateAttachment(
				m_VulkanDevice,
				m_HistoryBuffer->imageFormat,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				m_HistoryBuffer->width,
				m_HistoryBuffer->height,
				1,
				VK_IMAGE_VIEW_TYPE_2D,
				0,
				m_HistoryBuffer->image.replace(),
				m_HistoryBuffer->imageMemory.replace(),
				m_HistoryBuffer->imageView.replace(),
				"History Buffer image",
				"History Buffer image view");
			m_HistoryBuffer->imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			m_HistoryBuffer->TransitionToLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			// Shadow frame buffers
			{
				VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

				VkImageCreateInfo imageCreateInfo = vks::imageCreateInfo();
				imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
				imageCreateInfo.format = m_ShadowBufFormat;
				imageCreateInfo.extent.width = SHADOW_CASCADE_RES;
				imageCreateInfo.extent.height = SHADOW_CASCADE_RES;
				imageCreateInfo.extent.depth = 1;
				imageCreateInfo.mipLevels = 1;
				imageCreateInfo.arrayLayers = NUM_SHADOW_CASCADES;
				imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
				imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
				imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; // VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT?
				imageCreateInfo.flags = 0;

				VK_CHECK_RESULT(vkCreateImage(m_VulkanDevice->m_LogicalDevice, &imageCreateInfo, nullptr, m_ShadowImage.replace()));
				SetImageName(m_VulkanDevice, m_ShadowImage, "Shadow cascade image");
				VkMemoryRequirements memRequirements;
				vkGetImageMemoryRequirements(m_VulkanDevice->m_LogicalDevice, m_ShadowImage, &memRequirements);
				VkMemoryAllocateInfo memAlloc = vks::memoryAllocateInfo(memRequirements.size);
				memAlloc.memoryTypeIndex = m_VulkanDevice->GetMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				VK_CHECK_RESULT(vkAllocateMemory(m_VulkanDevice->m_LogicalDevice, &memAlloc, nullptr, m_ShadowImageMemory.replace()));
				VK_CHECK_RESULT(vkBindImageMemory(m_VulkanDevice->m_LogicalDevice, m_ShadowImage, m_ShadowImageMemory, 0));

				// Full image view (for all layers)
				VkImageViewCreateInfo fullImageView = vks::imageViewCreateInfo();
				fullImageView.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
				fullImageView.format = m_ShadowBufFormat;
				fullImageView.subresourceRange = {};
				fullImageView.subresourceRange.aspectMask = aspectMask;
				fullImageView.subresourceRange.baseMipLevel = 0;
				fullImageView.subresourceRange.levelCount = 1; // Number of mipmap levels
				fullImageView.subresourceRange.baseArrayLayer = 0;
				fullImageView.subresourceRange.layerCount = NUM_SHADOW_CASCADES;
				fullImageView.image = m_ShadowImage;
				fullImageView.flags = 0;
				VK_CHECK_RESULT(vkCreateImageView(m_VulkanDevice->m_LogicalDevice, &fullImageView, nullptr, m_ShadowImageView.replace()));
				SetImageViewName(m_VulkanDevice, m_ShadowImageView, "Shadow cascade image view (main)");

				// One frame buffer & view per cascade
				for (u32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
				{
					VkImageViewCreateInfo imageView = vks::imageViewCreateInfo();
					imageView.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
					imageView.format = m_ShadowBufFormat;
					imageView.subresourceRange = {};
					imageView.subresourceRange.aspectMask = aspectMask;
					imageView.subresourceRange.baseMipLevel = 0;
					imageView.subresourceRange.levelCount = 1; // Number of mipmap levels
					imageView.subresourceRange.baseArrayLayer = i;
					imageView.subresourceRange.layerCount = 1;
					imageView.image = m_ShadowImage;
					imageView.flags = 0;
					VK_CHECK_RESULT(vkCreateImageView(m_VulkanDevice->m_LogicalDevice, &imageView, nullptr, m_ShadowCascades[i]->imageView.replace()));
					char imageViewName[256];
					sprintf_s(imageViewName, "Shadow cascade %u image view", i);
					SetImageViewName(m_VulkanDevice, m_ShadowCascades[i]->imageView, imageViewName);

					VkFramebufferCreateInfo shadowFramebufferCreateInfo = vks::framebufferCreateInfo(m_ShadowRenderPass);
					shadowFramebufferCreateInfo.pAttachments = &m_ShadowCascades[i]->imageView;
					shadowFramebufferCreateInfo.attachmentCount = 1;
					shadowFramebufferCreateInfo.width = SHADOW_CASCADE_RES;
					shadowFramebufferCreateInfo.height = SHADOW_CASCADE_RES;
					VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &shadowFramebufferCreateInfo, nullptr, m_ShadowCascades[i]->frameBuffer.replace()));

					char frameBufferName[256];
					sprintf_s(frameBufferName, "Shadow cascade %u frame buffer", i);
					SetFramebufferName(m_VulkanDevice, m_ShadowCascades[i]->frameBuffer, frameBufferName);
				}
			}

			// SSAO frame buffer
			{
				assert(m_SSAOFrameBuf->frameBufferAttachments.size() == 1);

				CreateAttachment(m_VulkanDevice, m_SSAOFrameBuf, 0, "SSAO image", "SSAO image view");

				std::vector<VkImageView> ssaoAttachments;
				ssaoAttachments.push_back(m_SSAOFrameBuf->frameBufferAttachments[0].second.view);

				VkFramebufferCreateInfo ssaoFramebufferCreateInfo = vks::framebufferCreateInfo(m_SSAORenderPass);
				ssaoFramebufferCreateInfo.pAttachments = ssaoAttachments.data();
				ssaoFramebufferCreateInfo.attachmentCount = static_cast<u32>(ssaoAttachments.size());
				ssaoFramebufferCreateInfo.width = m_SSAOFrameBuf->width;
				ssaoFramebufferCreateInfo.height = m_SSAOFrameBuf->height;
				VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &ssaoFramebufferCreateInfo, nullptr, m_SSAOFrameBuf->frameBuffer.replace()));
				SetFramebufferName(m_VulkanDevice, m_SSAOFrameBuf->frameBuffer, "SSAO frame buffer");
			}

			// SSAO Blur frame buffers
			{
				assert(m_SSAOBlurHFrameBuf->frameBufferAttachments.size() == 1);
				assert(m_SSAOBlurVFrameBuf->frameBufferAttachments.size() == 1);
				assert(m_SSAOBlurHFrameBuf->width == m_SSAOBlurVFrameBuf->width);
				assert(m_SSAOBlurHFrameBuf->height == m_SSAOBlurVFrameBuf->height);

				CreateAttachment(m_VulkanDevice, m_SSAOBlurHFrameBuf, 0, "SSAO Blur H image", "SSAO Blur H image view");
				CreateAttachment(m_VulkanDevice, m_SSAOBlurVFrameBuf, 0, "SSAO Blur V image", "SSAO Blur V image view");

				std::vector<VkImageView> attachments;
				attachments.push_back(m_SSAOBlurHFrameBuf->frameBufferAttachments[0].second.view);

				VkFramebufferCreateInfo frameBufferCreateInfo = vks::framebufferCreateInfo(m_SSAOBlurHRenderPass);
				frameBufferCreateInfo.pAttachments = attachments.data();
				frameBufferCreateInfo.attachmentCount = static_cast<u32>(attachments.size());
				frameBufferCreateInfo.width = m_SSAOBlurHFrameBuf->width;
				frameBufferCreateInfo.height = m_SSAOBlurHFrameBuf->height;
				VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &frameBufferCreateInfo, nullptr, m_SSAOBlurHFrameBuf->frameBuffer.replace()));
				SetFramebufferName(m_VulkanDevice, m_SSAOBlurHFrameBuf->frameBuffer, "SSAO Blur Horizontal");

				attachments[0] = m_SSAOBlurVFrameBuf->frameBufferAttachments[0].second.view;
				frameBufferCreateInfo.renderPass = m_SSAOBlurVRenderPass;
				VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &frameBufferCreateInfo, nullptr, m_SSAOBlurVFrameBuf->frameBuffer.replace()));
				SetFramebufferName(m_VulkanDevice, m_SSAOBlurVFrameBuf->frameBuffer, "SSAO Blur Vertical");
			}

			// TODO: Remove redundant samplers
			VkSamplerCreateInfo ssaoNormalSamplerCreateInfo = vks::samplerCreateInfo();
			ssaoNormalSamplerCreateInfo.magFilter = VK_FILTER_NEAREST;
			ssaoNormalSamplerCreateInfo.minFilter = VK_FILTER_NEAREST;
			ssaoNormalSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			ssaoNormalSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			ssaoNormalSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			ssaoNormalSamplerCreateInfo.mipLodBias = 0.0f;
			ssaoNormalSamplerCreateInfo.minLod = 0.0f;
			ssaoNormalSamplerCreateInfo.maxLod = 1.0f;
			VK_CHECK_RESULT(vkCreateSampler(m_VulkanDevice->m_LogicalDevice, &ssaoNormalSamplerCreateInfo, nullptr, m_SSAOSampler.replace()));
			SetSamplerName(m_VulkanDevice, m_SSAOSampler, "SSAO sampler");

			VkSamplerCreateInfo colSamplerCreateInfo = vks::samplerCreateInfo();
			colSamplerCreateInfo.magFilter = VK_FILTER_LINEAR;
			colSamplerCreateInfo.minFilter = VK_FILTER_LINEAR;
			colSamplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			colSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			colSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			colSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			colSamplerCreateInfo.mipLodBias = 0.0f;
			colSamplerCreateInfo.minLod = 0.0f;
			colSamplerCreateInfo.maxLod = 1.0f;
			colSamplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			VK_CHECK_RESULT(vkCreateSampler(m_VulkanDevice->m_LogicalDevice, &colSamplerCreateInfo, nullptr, m_ColorSampler.replace()));
			SetSamplerName(m_VulkanDevice, m_ColorSampler, "Color sampler");

			VkSamplerCreateInfo depthSamplerCreateInfo = vks::samplerCreateInfo();
			depthSamplerCreateInfo.magFilter = VK_FILTER_LINEAR;
			depthSamplerCreateInfo.minFilter = VK_FILTER_LINEAR;
			depthSamplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			depthSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			depthSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			depthSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			depthSamplerCreateInfo.mipLodBias = 0.0f;
			depthSamplerCreateInfo.minLod = 0.0f;
			depthSamplerCreateInfo.maxLod = 1.0f;
			depthSamplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // TODO: VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK
			VK_CHECK_RESULT(vkCreateSampler(m_VulkanDevice->m_LogicalDevice, &depthSamplerCreateInfo, nullptr, m_DepthSampler.replace()));
			SetSamplerName(m_VulkanDevice, m_DepthSampler, "Depth sampler");
		}

		void VulkanRenderer::PrepareCubemapFrameBuffer()
		{
			const size_t frameBufferColorAttachmentCount = m_GBufferCubemapFrameBuffer->frameBufferAttachments.size();

			// Color attachments
			for (u32 i = 0; i < frameBufferColorAttachmentCount; ++i)
			{
				char dbgImageName[256];
				char dbgImageViewName[256];
				sprintf_s(dbgImageName, "Cubemap %u image", i);
				sprintf_s(dbgImageViewName, "Cubemap %u image view", i);
				CreateAttachment(m_VulkanDevice, m_GBufferCubemapFrameBuffer, i, dbgImageName, dbgImageViewName);
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
				attachmentDescs[i] = vks::attachmentDescription(m_GBufferCubemapFrameBuffer->frameBufferAttachments[i].second.format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			}
			attachmentDescs[frameBufferColorAttachmentCount] = vks::attachmentDescription(m_CubemapDepthAttachment->format, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

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

			VkRenderPassCreateInfo renderPassInfo = vks::renderPassCreateInfo();
			renderPassInfo.pAttachments = attachmentDescs.data();
			renderPassInfo.attachmentCount = static_cast<u32>(attachmentDescs.size());
			renderPassInfo.subpassCount = subpasses.size();
			renderPassInfo.pSubpasses = subpasses.data();
			renderPassInfo.dependencyCount = dependencies.size();
			renderPassInfo.pDependencies = dependencies.data();

			VK_CHECK_RESULT(vkCreateRenderPass(m_VulkanDevice->m_LogicalDevice, &renderPassInfo, nullptr, m_GBufferCubemapFrameBuffer->renderPass.replace()));
			SetRenderPassName(m_VulkanDevice, m_GBufferCubemapFrameBuffer->renderPass, "GBuffer Cubemap render pass");

			std::vector<VkImageView> attachments;
			for (u32 i = 0; i < frameBufferColorAttachmentCount; ++i)
			{
				attachments.push_back(m_GBufferCubemapFrameBuffer->frameBufferAttachments[i].second.view);
			}
			attachments.push_back(m_CubemapDepthAttachment->view);

			VkFramebufferCreateInfo fbufCreateInfo = vks::framebufferCreateInfo(m_GBufferCubemapFrameBuffer->renderPass);
			fbufCreateInfo.pAttachments = attachments.data();
			fbufCreateInfo.attachmentCount = static_cast<u32>(attachments.size());
			fbufCreateInfo.width = m_GBufferCubemapFrameBuffer->width;
			fbufCreateInfo.height = m_GBufferCubemapFrameBuffer->height;
			fbufCreateInfo.layers = 6;
			VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &fbufCreateInfo, nullptr, m_GBufferCubemapFrameBuffer->frameBuffer.replace()));
			SetFramebufferName(m_VulkanDevice, m_GBufferCubemapFrameBuffer->frameBuffer, "GBuffer Cubemap frame buffer");
		}

		void VulkanRenderer::RemoveMaterial(MaterialID materialID)
		{
			assert(materialID != InvalidMaterialID);

			m_Materials.erase(materialID);
		}

		void VulkanRenderer::FillOutGBufferFrameBufferAttachments(std::vector<Pair<std::string, void*>>& outVec)
		{
			for (const auto& frameBufferAttachment : m_GBufferFrameBuf->frameBufferAttachments)
			{
				outVec.emplace_back(
					frameBufferAttachment.first,
					(void*)&frameBufferAttachment.second.view
				);
			}
		}

		void VulkanRenderer::PhysicsDebugRender()
		{
			btDiscreteDynamicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
			physicsWorld->debugDrawWorld();
		}

		void VulkanRenderer::CreateStaticVertexBuffers()
		{
			for (u32 i = 0; i < m_VertexIndexBufferPairs.size(); ++i)
			{
				if (m_VertexIndexBufferPairs[i].useStagingBuffer)
				{
					u32 requiredMemory = 0;

					Shader* shader = m_Shaders[i].shader;
					if (!shader->bGenerateVertexBufferForAll)
					{
						for (VulkanRenderObject* renderObject : m_RenderObjects)
						{
							if (renderObject &&
								renderObject->vertexBufferData &&
								m_Materials[renderObject->materialID].material.shaderID == i)
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
		}

		void VulkanRenderer::CreateDynamicVertexBuffers()
		{
			for (u32 i = 0; i < m_VertexIndexBufferPairs.size(); ++i)
			{
				if (!m_VertexIndexBufferPairs[i].useStagingBuffer)
				{
					u32 requiredMemory = m_Shaders[i].shader->dynamicVertexBufferSize;

					if (requiredMemory > 0)
					{
						CreateDynamicVertexBuffer(
							m_VertexIndexBufferPairs[i].vertexBuffer,
							requiredMemory,
							nullptr);
					}
				}
			}
		}

		u32 VulkanRenderer::CreateStaticVertexBuffer(VulkanBuffer* vertexBuffer, ShaderID shaderID, u32 size)
		{
			void* vertexDataStart = malloc_hooked(size);
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
				if (renderObject &&
					renderObject->vertexBufferData &&
					m_Materials[renderObject->materialID].material.shaderID == shaderID)
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
			free_hooked(vertexDataStart);

			return vertexCount;
		}

		void VulkanRenderer::CreateShadowVertexBuffer()
		{
			VulkanMaterial& shadowMat = m_Materials[m_ShadowMaterialID];
			VulkanShader& shadowShader = m_Shaders[shadowMat.material.shaderID];

			u32 vertexStride = CalculateVertexStride(shadowShader.shader->vertexAttributes);
			u32 size = 0;

			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject &&
					renderObject->vertexBufferData)
				{
					size += renderObject->vertexBufferData->VertexCount * vertexStride;
				}
			}

			void* vertexDataStart = malloc_hooked(size);
			if (!vertexDataStart)
			{
				PrintError("Failed to allocate memory for shadow vertex buffer! Attempted to allocate %d bytes", size);
				return;
			}

			void* vertexBufferData = vertexDataStart;

			u32 vertexCount = 0;
			u32 vertexBufferSize = 0;
			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject &&
					renderObject->vertexBufferData &&
					renderObject->gameObject->CastsShadow())
				{
					renderObject->shadowVertexOffset = vertexCount;

					u32 copySize = renderObject->vertexBufferData->CopyInto(static_cast<real*>(vertexBufferData), shadowShader.shader->vertexAttributes);

					vertexCount += renderObject->vertexBufferData->VertexCount;
					vertexBufferSize += copySize;

					vertexBufferData = (char*)vertexBufferData + copySize;
				}
			}

			if (vertexBufferSize == 0 || vertexCount == 0)
			{
				return;
			}

			VulkanBuffer* vertexBuffer = m_VertexIndexBufferPairs[shadowMat.material.shaderID].vertexBuffer;
			CreateStaticVertexBuffer(vertexBuffer, vertexDataStart, vertexBufferSize);
			free_hooked(vertexDataStart);
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

			CopyBuffer(m_VulkanDevice, m_GraphicsQueue, stagingBuffer.m_Buffer, vertexBuffer->m_Buffer, vertexBufferSize);
		}

		void VulkanRenderer::CreateDynamicVertexBuffer(VulkanBuffer* vertexBuffer, u32 size, void* initialData)
		{
			CreateAndAllocateBuffer(m_VulkanDevice, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vertexBuffer);

			if (initialData != nullptr)
			{
				VulkanBuffer stagingBuffer(m_VulkanDevice->m_LogicalDevice);
				CreateAndAllocateBuffer(m_VulkanDevice, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
					VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer);

				VK_CHECK_RESULT(stagingBuffer.Map(size));
				memcpy(stagingBuffer.m_Mapped, initialData, size);
				stagingBuffer.Unmap();

				CopyBuffer(m_VulkanDevice, m_GraphicsQueue, stagingBuffer.m_Buffer, vertexBuffer->m_Buffer, size);
			}
		}

		void VulkanRenderer::CreateStaticIndexBuffers()
		{
			for (size_t i = 0; i < m_VertexIndexBufferPairs.size(); ++i)
			{
				Shader* shader = m_Shaders[i].shader;
				if (!shader->bGenerateVertexBufferForAll)
				{
					m_VertexIndexBufferPairs[i].indexCount = CreateStaticIndexBuffer(m_VertexIndexBufferPairs[i].indexBuffer, i);
				}
			}
		}

		u32 VulkanRenderer::CreateStaticIndexBuffer(VulkanBuffer* indexBuffer, ShaderID shaderID)
		{
			std::vector<u32> indices;

			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject &&
					renderObject->bIndexed &&
					m_Materials[renderObject->materialID].material.shaderID == shaderID)
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

		void VulkanRenderer::CreateShadowIndexBuffer()
		{
			std::vector<u32> indices;

			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject &&
					renderObject->bIndexed)
				{
					renderObject->shadowIndexOffset = indices.size();
					indices.insert(indices.end(), renderObject->indices->begin(), renderObject->indices->end());
				}
			}

			if (indices.empty())
			{
				return;
			}

			VulkanBuffer* indexBuffer = m_VertexIndexBufferPairs[m_Materials[m_ShadowMaterialID].material.shaderID].indexBuffer;
			CreateStaticIndexBuffer(indexBuffer, indices);
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

		void VulkanRenderer::CreateDescriptorPool()
		{
			std::vector<VkDescriptorPoolSize> poolSizes
			{
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_NUM_DESC_COMBINED_IMAGE_SAMPLERS },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_NUM_DESC_UNIFORM_BUFFERS },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, MAX_NUM_DESC_DYNAMIC_UNIFORM_BUFFERS },
			};

			VkDescriptorPoolCreateInfo poolInfo = vks::descriptorPoolCreateInfo(poolSizes, MAX_NUM_DESC_SETS);
			// TODO: Have additional pool which doesn't have this flag set for constant descriptor sets
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // Allow descriptor sets to be added/removed often

			VK_CHECK_RESULT(vkCreateDescriptorPool(m_VulkanDevice->m_LogicalDevice, &poolInfo, nullptr, m_DescriptorPool.replace()));
		}

		u32 VulkanRenderer::AllocateDynamicUniformBuffer(u32 dynamicDataSize, void** data, i32 maxObjectCount /* = -1 */)
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

			if (maxObjectCount == -1)
			{
				// TODO: FIXME: Use better metric for initial size
				maxObjectCount = (i32)m_RenderObjects.size();
			}
			size_t dynamicBufferSize = maxObjectCount * m_DynamicAlignment;

			(*data) = aligned_malloc_hooked(dynamicBufferSize, m_DynamicAlignment);
			assert(*data);

			return dynamicBufferSize;
		}

		void VulkanRenderer::PrepareUniformBuffer(VulkanBuffer* buffer, u32 bufferSize,
			VkBufferUsageFlags bufferUseageFlagBits, VkMemoryPropertyFlags memoryPropertyHostFlagBits)
		{
			VK_CHECK_RESULT(CreateAndAllocateBuffer(m_VulkanDevice, bufferSize, bufferUseageFlagBits, memoryPropertyHostFlagBits, buffer));
			VK_CHECK_RESULT(buffer->Map());
		}

		void VulkanRenderer::CreateSemaphores()
		{
			VkSemaphoreCreateInfo semaphoreInfo = vks::semaphoreCreateInfo();

			VK_CHECK_RESULT(vkCreateSemaphore(m_VulkanDevice->m_LogicalDevice, &semaphoreInfo, nullptr, m_PresentCompleteSemaphore.replace()));
			VK_CHECK_RESULT(vkCreateSemaphore(m_VulkanDevice->m_LogicalDevice, &semaphoreInfo, nullptr, m_RenderCompleteSemaphore.replace()));
		}

		void VulkanRenderer::BatchRenderObjects()
		{
			// TODO: OPTIMIZE: This isn't always necessary, but it is always expensive!
			CreateStaticVertexBuffers();
			CreateStaticIndexBuffers();
			CreateDynamicVertexBuffers();

			CreateShadowVertexBuffer();
			CreateShadowIndexBuffer();

			const char* blockName = "BatchRenderObjects";
			u32 renderObjBatchCount = 0;
			{
				PROFILE_AUTO(blockName);

				m_DeferredObjectBatches.batches.clear();
				m_ForwardObjectBatches.batches.clear();
				m_ShadowBatch.batches.clear();
				m_DepthAwareEditorObjBatches.batches.clear();
				m_DepthUnawareEditorObjBatches.batches.clear();

				// NOTE: Optimization options:
				//			- Sort materials by shader ID to allow early out on second loop
				//			- Sort render objects based on batching order
				//			- Reuse previous batching, only removing or adding entries

				for (u32 shaderID = 0; shaderID < m_Shaders.size(); ++shaderID)
				{
					VulkanBuffer* vertBuffer = m_VertexIndexBufferPairs[shaderID].vertexBuffer;
					VulkanBuffer* indexBuffer = m_VertexIndexBufferPairs[shaderID].indexBuffer;

					if (vertBuffer->m_Buffer == 0)
					{
						continue;
					}

					ShaderBatch* shaderBatch = (m_Shaders[shaderID].shader->bDeferred ? &m_DeferredObjectBatches : &m_ForwardObjectBatches);

					ShaderBatchPair shaderBatchPair = {};
					shaderBatchPair.shaderID = shaderID;

					ShaderBatchPair depthAwareEditorShaderBatchPair = {};
					depthAwareEditorShaderBatchPair.shaderID = shaderID;

					ShaderBatchPair depthUnawareEditorShaderBatchPair = {};
					depthUnawareEditorShaderBatchPair.shaderID = shaderID;

					i32 dynamicUBOOffset = 0;

					for (auto& matPair : m_Materials)
					{
						if (matPair.second.material.shaderID == shaderID)
						{
							MaterialBatchPair matBatchPair = {};
							matBatchPair.materialID = matPair.first;

							MaterialBatchPair depthAwareEditorMatBatchPair = {};
							depthAwareEditorMatBatchPair.materialID = matPair.first;

							MaterialBatchPair depthUnawareEditorMatBatchPair = {};
							depthUnawareEditorMatBatchPair.materialID = matPair.first;

							for (u32 renderID = 0; renderID < m_RenderObjects.size(); ++renderID)
							{
								VulkanRenderObject* renderObject = GetRenderObject(renderID);

								if (renderObject &&
									renderObject->materialID == matPair.first &&
									(!renderObject->bIndexed || indexBuffer->m_Buffer != 0))
								{
									dynamicUBOOffset += RoundUp(m_Shaders[shaderID].uniformBuffer.dynamicData.size, m_DynamicAlignment);
									renderObject->dynamicUBOOffset = dynamicUBOOffset;

									if (renderObject->gameObject->IsVisible())
									{
										if (renderObject->bEditorObject)
										{
											if (m_Shaders[shaderID].shader->bDepthWriteEnable)
											{
												depthAwareEditorMatBatchPair.batch.objects.push_back(renderID);
											}
											else
											{
												depthUnawareEditorMatBatchPair.batch.objects.push_back(renderID);
											}
										}
										else
										{
											matBatchPair.batch.objects.push_back(renderID);
										}
									}
								}
							}

							if (!matBatchPair.batch.objects.empty())
							{
								++renderObjBatchCount;
								shaderBatchPair.batch.batches.push_back(matBatchPair);
							}
							if (!depthAwareEditorMatBatchPair.batch.objects.empty())
							{
								++renderObjBatchCount;
								depthAwareEditorShaderBatchPair.batch.batches.push_back(depthAwareEditorMatBatchPair);
							}
							if (!depthUnawareEditorMatBatchPair.batch.objects.empty())
							{
								++renderObjBatchCount;
								depthUnawareEditorShaderBatchPair.batch.batches.push_back(depthUnawareEditorMatBatchPair);
							}
						}
					}

					if (!shaderBatchPair.batch.batches.empty())
					{
						shaderBatch->batches.push_back(shaderBatchPair);
					}
					if (!depthAwareEditorShaderBatchPair.batch.batches.empty())
					{
						m_DepthAwareEditorObjBatches.batches.push_back(depthAwareEditorShaderBatchPair);
					}
					if (!depthUnawareEditorShaderBatchPair.batch.batches.empty())
					{
						m_DepthUnawareEditorObjBatches.batches.push_back(depthUnawareEditorShaderBatchPair);
					}
				}
			}

			ShaderBatchPair shadowShaderBatch;
			shadowShaderBatch.batch.batches.resize(1);
			u32 dynamicShadowUBOOffset = 0;
			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				VulkanRenderObject* renderObject = GetRenderObject(i);
				if (renderObject &&
					renderObject->vertexBufferData &&
					renderObject->gameObject->CastsShadow() &&
					renderObject->gameObject->IsVisible())
				{
					dynamicShadowUBOOffset += m_DynamicAlignment;
					renderObject->dynamicShadowUBOOffset = dynamicShadowUBOOffset;
					shadowShaderBatch.batch.batches[0].batch.objects.push_back(i);
				}
			}
			m_ShadowBatch.batches.push_back(shadowShaderBatch);

			ms blockMS = Profiler::GetBlockDuration(blockName);
			Print("Batched %u render objects into %u batches in %.2fms\n", m_RenderObjects.size(), renderObjBatchCount, blockMS);
		}

		void VulkanRenderer::DrawShaderBatch(const ShaderBatchPair& shaderBatch, VkCommandBuffer& commandBuffer, DrawCallInfo* drawCallInfo /* = nullptr */)
		{
			ShaderID shaderID = shaderBatch.shaderID;
			if (drawCallInfo && drawCallInfo->materialIDOverride != InvalidMaterialID)
			{
				shaderID = m_Materials[drawCallInfo->materialIDOverride].material.shaderID;
			}

			VulkanBuffer* vertBuffer = m_VertexIndexBufferPairs[shaderID].vertexBuffer;
			VulkanBuffer* indexBuffer = m_VertexIndexBufferPairs[shaderID].indexBuffer;

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertBuffer->m_Buffer, offsets);
			if (indexBuffer->m_Buffer != VK_NULL_HANDLE)
			{
				vkCmdBindIndexBuffer(commandBuffer, indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
			}

			u32 i = 0;
			for (const MaterialBatchPair& matBatch : shaderBatch.batch.batches)
			{
				MaterialID matID = matBatch.materialID;
				if (drawCallInfo && drawCallInfo->materialIDOverride != InvalidMaterialID)
				{
					matID = drawCallInfo->materialIDOverride;
				}

				VulkanMaterial& mat = m_Materials[matID];
				VulkanShader& shader = m_Shaders[mat.material.shaderID];

				for (RenderID renderID : matBatch.batch.objects)
				{
					VulkanRenderObject* renderObject = GetRenderObject(renderID);

					VkPipeline graphicsPipeline = renderObject->graphicsPipeline;
					VkPipelineLayout pipelineLayout = renderObject->pipelineLayout;
					VkDescriptorSet descriptorSet = renderObject->descriptorSet;
					u32 dynamicUBOOffset = renderObject->dynamicUBOOffset;

					if (drawCallInfo)
					{
						if (drawCallInfo->graphicsPipelineOverride != InvalidID)
						{
							graphicsPipeline = drawCallInfo->graphicsPipelineOverride;
							assert(drawCallInfo->pipelineLayoutOverride != InvalidID);
							pipelineLayout = drawCallInfo->pipelineLayoutOverride;
						}
						if (drawCallInfo->descriptorSetOverride != InvalidID)
						{
							descriptorSet = drawCallInfo->descriptorSetOverride;
						}
						if (drawCallInfo->bRenderingShadows)
						{
							dynamicUBOOffset = renderObject->dynamicShadowUBOOffset;
						}
					}

					vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

					BindDescriptorSet(&shader, dynamicUBOOffset, commandBuffer, pipelineLayout, descriptorSet);

					Material::PushConstantBlock* pushConstantBlock = mat.material.pushConstantBlock;
					if (shader.shader->bNeedPushConstantBlock)
					{
						// Push constants
						if (drawCallInfo && drawCallInfo->pushConstantOverride)
						{
							pushConstantBlock = drawCallInfo->pushConstantOverride;
						}
						else
						{
							if (mat.material.pushConstantBlock->data != nullptr)
							{
							BaseCamera* cam = g_CameraManager->CurrentCamera();
							mat.material.pushConstantBlock->SetData(cam->GetView(), cam->GetProjection());
						}
						}
						if (mat.material.pushConstantBlock->data != nullptr)
						{
						VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
						vkCmdPushConstants(commandBuffer, pipelineLayout, stages, 0, pushConstantBlock->size, pushConstantBlock->data);
					}
					}

					if (renderObject->bIndexed)
					{
						if (drawCallInfo == nullptr ||
							!drawCallInfo->bRenderingShadows)
						{
							vkCmdDrawIndexed(commandBuffer, renderObject->indices->size(), 1, renderObject->indexOffset, renderObject->vertexOffset, 0);
						}
						else
						{
							vkCmdDrawIndexed(commandBuffer, renderObject->indices->size(), 1, renderObject->shadowIndexOffset, renderObject->shadowVertexOffset, 0);
						}
					}
					else
					{
						if (drawCallInfo == nullptr ||
							!drawCallInfo->bRenderingShadows)
						{
							vkCmdDraw(commandBuffer, renderObject->vertexBufferData->VertexCount, 1, renderObject->vertexOffset, 0);
						}
						else
						{
							vkCmdDraw(commandBuffer, renderObject->vertexBufferData->VertexCount, 1, renderObject->shadowVertexOffset, 0);
						}
					}

					++i;
				}
			}
		}

		void VulkanRenderer::RenderFullscreenQuad(VkCommandBuffer commandBuffer, VkRenderPass renderPass, VkFramebuffer framebuffer,
			ShaderID shaderID, VkPipelineLayout pipelineLayout, VkPipeline graphicsPipeline, VkDescriptorSet descriptorSet, bool bFlipViewport)
		{
			// TODO: Begin and end regions here?
			std::array<VkClearValue, 2> clearValues = {};
			clearValues[0].color = m_ClearColor;
			clearValues[1].depthStencil = { 0.0f, 0 };

			VulkanShader* vkShader = &m_Shaders[shaderID];

			VkViewport fullscreenViewport = bFlipViewport ? 
				vks::viewportFlipped((real)m_SwapChainExtent.width, (real)m_SwapChainExtent.height, 0.0f, 1.0f) :
				vks::viewport((real)m_SwapChainExtent.width, (real)m_SwapChainExtent.height, 0.0f, 1.0f);
			VkRect2D fullscreenScissor = vks::scissor(0u, 0u, m_SwapChainExtent.width, m_SwapChainExtent.height);

			VkDeviceSize offsets[1] = { 0 };

			vkCmdSetViewport(commandBuffer, 0, 1, &fullscreenViewport);
			vkCmdSetScissor(commandBuffer, 0, 1, &fullscreenScissor);

			VkRenderPassBeginInfo renderPassBeginInfo = vks::renderPassBeginInfo(renderPass);
			renderPassBeginInfo.renderArea.offset = { 0, 0 };
			renderPassBeginInfo.renderArea.extent = m_SwapChainExtent;
			renderPassBeginInfo.clearValueCount = clearValues.size();
			renderPassBeginInfo.pClearValues = clearValues.data();
			renderPassBeginInfo.framebuffer = framebuffer;
			vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			{
				BindDescriptorSet(vkShader, 0, commandBuffer, pipelineLayout, descriptorSet);

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

				//
				// TODO: Render full screen tri here!
				//
				//vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_VertexIndexBufferPairs[shaderID].vertexBuffer->m_Buffer, offsets);
				vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_FullScreenTriVertexBuffer->m_Buffer, offsets);
				
				vkCmdDraw(commandBuffer, m_FullScreenTriVertexBufferData.VertexCount, 1, 0, 0);
			}
			vkCmdEndRenderPass(commandBuffer);
		}

		void VulkanRenderer::BuildCommandBuffers(const DrawCallInfo& drawCallInfo)
		{
			assert(drawCallInfo.bRenderToCubemap == false); // Unsupported in Vulkan renderer!

			// Offscreen command buffer
			{
				if (m_OffScreenCmdBuffer == VK_NULL_HANDLE)
				{
					m_OffScreenCmdBuffer = m_CommandBufferManager.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
				}

				if (m_OffscreenSemaphore == VK_NULL_HANDLE)
				{
					VkSemaphoreCreateInfo semaphoreCreateInfo = vks::semaphoreCreateInfo();
					VK_CHECK_RESULT(vkCreateSemaphore(m_VulkanDevice->m_LogicalDevice, &semaphoreCreateInfo, nullptr, &m_OffscreenSemaphore));
				}

				VkCommandBufferBeginInfo cmdBufferbeginInfo = vks::commandBufferBeginInfo();
				cmdBufferbeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
				VK_CHECK_RESULT(vkBeginCommandBuffer(m_OffScreenCmdBuffer, &cmdBufferbeginInfo));

				BeginGPUTimeStamp(m_OffScreenCmdBuffer, "Deferred");

				BeginDebugMarkerRegion(m_OffScreenCmdBuffer, "Shadow cascades");

				//
				// Cascaded shadow mapping
				//

				bool bEnableShadows = true;
				if (bEnableShadows && m_DirectionalLight)
				{
					std::array<VkClearValue, 1> shadowClearValues = {};
					shadowClearValues[0].depthStencil = { 0.0f, 0 };

					for (const ShaderBatchPair& shaderBatch : m_ShadowBatch.batches)
					{
						for (RenderID renderID : shaderBatch.batch.batches[0].batch.objects)
						{
							VulkanRenderObject* renderObject = GetRenderObject(renderID);
							UpdateDynamicUniformBuffer(renderID, nullptr, m_ShadowMaterialID, renderObject->dynamicShadowUBOOffset);
						}
					}

					VkViewport viewport = vks::viewportFlipped((real)SHADOW_CASCADE_RES, (real)SHADOW_CASCADE_RES, 0.0f, 1.0f);
					vkCmdSetViewport(m_OffScreenCmdBuffer, 0, 1, &viewport);

					VkRect2D shadowScissor = vks::scissor(0u, 0u, SHADOW_CASCADE_RES, SHADOW_CASCADE_RES);
					vkCmdSetScissor(m_OffScreenCmdBuffer, 0, 1, &shadowScissor);

					Material::PushConstantBlock pushConstantBlock = {};

					for (u32 c = 0; c < NUM_SHADOW_CASCADES; ++c)
					{
						VkRenderPassBeginInfo renderPassBeginInfo = vks::renderPassBeginInfo(m_ShadowRenderPass);
						renderPassBeginInfo.framebuffer = m_ShadowCascades[c]->frameBuffer;
						renderPassBeginInfo.renderArea.offset = { 0, 0 };
						renderPassBeginInfo.renderArea.extent = { SHADOW_CASCADE_RES, SHADOW_CASCADE_RES };
						renderPassBeginInfo.clearValueCount = shadowClearValues.size();
						renderPassBeginInfo.pClearValues = shadowClearValues.data();

						vkCmdBeginRenderPass(m_OffScreenCmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

						DrawCallInfo shadowDrawCallInfo = {};
						shadowDrawCallInfo.materialIDOverride = m_ShadowMaterialID;
						shadowDrawCallInfo.graphicsPipelineOverride = m_ShadowGraphicsPipeline;
						shadowDrawCallInfo.pipelineLayoutOverride = m_ShadowPipelineLayout;
						shadowDrawCallInfo.descriptorSetOverride = m_ShadowDescriptorSet;
						shadowDrawCallInfo.bRenderingShadows = true;

						// TODO: Upload as one draw
						pushConstantBlock.SetData(m_ShadowSamplingData.cascadeViewProjMats[c]);
						shadowDrawCallInfo.pushConstantOverride = &pushConstantBlock;

						for (const ShaderBatchPair& shaderBatch : m_ShadowBatch.batches)
						{
							DrawShaderBatch(shaderBatch, m_OffScreenCmdBuffer, &shadowDrawCallInfo);
						}

						vkCmdEndRenderPass(m_OffScreenCmdBuffer);
					}
				}

				EndDebugMarkerRegion(m_OffScreenCmdBuffer);
				BeginDebugMarkerRegion(m_OffScreenCmdBuffer, "Deferred");

				//
				// G-Buffer fill
				//

				std::array<VkClearValue, 3> gBufClearValues = {};
				gBufClearValues[0].color = m_ClearColor;
				gBufClearValues[1].color = m_ClearColor;
				gBufClearValues[2].depthStencil = { 0.0f, 0 };

				VkRenderPassBeginInfo renderPassBeginInfo = vks::renderPassBeginInfo(m_GBufferFrameBuf->renderPass);
				renderPassBeginInfo.framebuffer = m_GBufferFrameBuf->frameBuffer;
				renderPassBeginInfo.renderArea.offset = { 0, 0 };
				renderPassBeginInfo.renderArea.extent = { m_GBufferFrameBuf->width, m_GBufferFrameBuf->height };
				renderPassBeginInfo.clearValueCount = gBufClearValues.size();
				renderPassBeginInfo.pClearValues = gBufClearValues.data();

				vkCmdBeginRenderPass(m_OffScreenCmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				// TODO: Make min and max values members
				VkViewport fullScreenViewport = vks::viewportFlipped((real)m_GBufferFrameBuf->width, (real)m_GBufferFrameBuf->height, 0.0f, 1.0f);
				vkCmdSetViewport(m_OffScreenCmdBuffer, 0, 1, &fullScreenViewport);

				VkRect2D fullScreenScissor = vks::scissor(0u, 0u, m_GBufferFrameBuf->width, m_GBufferFrameBuf->height);
				vkCmdSetScissor(m_OffScreenCmdBuffer, 0, 1, &fullScreenScissor);

				for (const ShaderBatchPair& shaderBatch : m_DeferredObjectBatches.batches)
				{
					DrawShaderBatch(shaderBatch, m_OffScreenCmdBuffer);
				}

				vkCmdEndRenderPass(m_OffScreenCmdBuffer);

				// NOTE: Layout is auto transitioned from render pass
				// TODO: Somehow handle this automatically?
				m_GBufferDepthAttachment->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				EndDebugMarkerRegion(m_OffScreenCmdBuffer);

				//
				// SSAO generation
				//

				VulkanRenderObject* gBufferObject = GetRenderObject(m_GBufferQuadRenderID);
				VulkanMaterial* gBufferMaterial = &m_Materials[gBufferObject->materialID];

				VertexIndexBufferPair* gBufferVertexIndexBuffer = &m_VertexIndexBufferPairs[gBufferMaterial->material.shaderID];

				VkDeviceSize offsets[1] = { 0 };

				if (m_SSAOSamplingData.ssaoEnabled)
				{
					BeginGPUTimeStamp(m_OffScreenCmdBuffer, "SSAO");

					BeginDebugMarkerRegion(m_OffScreenCmdBuffer, "SSAO");

					std::array<VkClearValue, 1> ssaoClearValues = {};
					ssaoClearValues[0].color = m_ClearColor;

					renderPassBeginInfo.renderPass = m_SSAORenderPass;
					renderPassBeginInfo.framebuffer = m_SSAOFrameBuf->frameBuffer;
					renderPassBeginInfo.renderArea.offset = { 0, 0 };
					renderPassBeginInfo.renderArea.extent = { m_SSAOFrameBuf->width, m_SSAOFrameBuf->height };
					renderPassBeginInfo.clearValueCount = ssaoClearValues.size();
					renderPassBeginInfo.pClearValues = ssaoClearValues.data();

					vkCmdBeginRenderPass(m_OffScreenCmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

					// TODO: EZ: (erradicate all calls to GetShaderID)
					ShaderID ssaoShaderID = InvalidShaderID;
					GetShaderID("ssao", ssaoShaderID);
					assert(ssaoShaderID != InvalidShaderID);

					vkCmdBindPipeline(m_OffScreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SSAOGraphicsPipeline);

					VkViewport ssaoViewport = vks::viewportFlipped((real)m_SSAOFrameBuf->width, (real)m_SSAOFrameBuf->height, 0.0f, 1.0f);
					vkCmdSetViewport(m_OffScreenCmdBuffer, 0, 1, &ssaoViewport);

					VkRect2D ssaoScissor = vks::scissor(0u, 0u, m_SSAOFrameBuf->width, m_SSAOFrameBuf->height);
					vkCmdSetScissor(m_OffScreenCmdBuffer, 0, 1, &ssaoScissor);

					BindDescriptorSet(&m_Shaders[ssaoShaderID], 0, m_OffScreenCmdBuffer, m_SSAOGraphicsPipelineLayout, m_SSAODescSet);

					vkCmdBindVertexBuffers(m_OffScreenCmdBuffer, 0, 1, &gBufferVertexIndexBuffer->vertexBuffer->m_Buffer, offsets);

					vkCmdDraw(m_OffScreenCmdBuffer, gBufferObject->vertexBufferData->VertexCount, 1, gBufferObject->vertexOffset, 0);

					vkCmdEndRenderPass(m_OffScreenCmdBuffer);

					EndDebugMarkerRegion(m_OffScreenCmdBuffer);

					//
					// SSAO blur
					//

					if (m_bSSAOBlurEnabled)
					{
						BeginDebugMarkerRegion(m_OffScreenCmdBuffer, "SSAO Blur");

						renderPassBeginInfo.renderPass = m_SSAOBlurHRenderPass;
						renderPassBeginInfo.framebuffer = m_SSAOBlurHFrameBuf->frameBuffer;
						renderPassBeginInfo.renderArea.extent = { m_SSAOBlurHFrameBuf->width, m_SSAOBlurHFrameBuf->height };

						// Horizontal pass
						vkCmdBeginRenderPass(m_OffScreenCmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

						ShaderID ssaoBlurShaderID = InvalidShaderID;
						GetShaderID("ssao_blur", ssaoBlurShaderID);
						assert(ssaoBlurShaderID != InvalidShaderID);

						vkCmdBindPipeline(m_OffScreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SSAOBlurHGraphicsPipeline);

						VkViewport ssaoBlurViewport = vks::viewportFlipped((real)m_SSAOBlurHFrameBuf->width, (real)m_SSAOBlurHFrameBuf->height, 0.0f, 1.0f);
						vkCmdSetViewport(m_OffScreenCmdBuffer, 0, 1, &ssaoBlurViewport);

						VkRect2D ssaoBlurScissor = vks::scissor(0u, 0u, m_SSAOBlurHFrameBuf->width, m_SSAOBlurHFrameBuf->height);
						vkCmdSetScissor(m_OffScreenCmdBuffer, 0, 1, &ssaoBlurScissor);

						BindDescriptorSet(&m_Shaders[ssaoBlurShaderID], 0 * m_DynamicAlignment, m_OffScreenCmdBuffer, m_SSAOBlurGraphicsPipelineLayout, m_SSAOBlurHDescSet);

						vkCmdBindVertexBuffers(m_OffScreenCmdBuffer, 0, 1, &gBufferVertexIndexBuffer->vertexBuffer->m_Buffer, offsets);

						UniformOverrides overrides = {};
						overrides.overridenUniforms.AddUniform(U_SSAO_BLUR_DATA_DYNAMIC);
						overrides.bSSAOVerticalPass = false;
						UpdateDynamicUniformBuffer(m_SSAOBlurMatID, 0 * m_DynamicAlignment, MAT4_IDENTITY, &overrides);

						vkCmdDraw(m_OffScreenCmdBuffer, gBufferObject->vertexBufferData->VertexCount, 1, gBufferObject->vertexOffset, 0);

						vkCmdEndRenderPass(m_OffScreenCmdBuffer);

						renderPassBeginInfo.renderPass = m_SSAOBlurVRenderPass;
						renderPassBeginInfo.framebuffer = m_SSAOBlurVFrameBuf->frameBuffer;

						// Vertical pass
						vkCmdBeginRenderPass(m_OffScreenCmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

						vkCmdBindPipeline(m_OffScreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SSAOBlurVGraphicsPipeline);

						BindDescriptorSet(&m_Shaders[ssaoBlurShaderID], 1 * m_DynamicAlignment, m_OffScreenCmdBuffer, m_SSAOBlurGraphicsPipelineLayout, m_SSAOBlurVDescSet);

						overrides.bSSAOVerticalPass = true;
						UpdateDynamicUniformBuffer(m_SSAOBlurMatID, 1 * m_DynamicAlignment, MAT4_IDENTITY, &overrides);

						vkCmdDraw(m_OffScreenCmdBuffer, gBufferObject->vertexBufferData->VertexCount, 1, gBufferObject->vertexOffset, 0);

						vkCmdEndRenderPass(m_OffScreenCmdBuffer);

						EndDebugMarkerRegion(m_OffScreenCmdBuffer);
					}

					EndGPUTimeStamp(m_OffScreenCmdBuffer, "SSAO");
				}

				EndGPUTimeStamp(m_OffScreenCmdBuffer, "Deferred");

				VK_CHECK_RESULT(vkEndCommandBuffer(m_OffScreenCmdBuffer));
				SetCommandBufferName(m_VulkanDevice, m_OffScreenCmdBuffer, "Offscreen command buffer");
			}

			if (g_EngineInstance->IsRenderingImGui())
			{
				// Generate ImGui geometry (retrieved later through ImGui::GetDrawData)
				ImGui::Render();
			}

			// TODO: Remove unused cmd buffers
			VkCommandBuffer& commandBuffer = m_CommandBufferManager.m_CommandBuffers[0];
			VkCommandBufferBeginInfo cmdBufferbeginInfo = vks::commandBufferBeginInfo();
			cmdBufferbeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

			VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &cmdBufferbeginInfo));

			BeginGPUTimeStamp(commandBuffer, "Forward");

			{
				BeginDebugMarkerRegion(commandBuffer, "Shade deferred");

				VulkanRenderObject* gBufferObject = GetRenderObject(m_GBufferQuadRenderID);
				ShaderID shaderID = m_Materials[gBufferObject->materialID].material.shaderID;
				RenderFullscreenQuad(commandBuffer, m_DeferredCombineRenderPass, m_OffscreenFrameBuffer0->frameBuffer, shaderID,
					gBufferObject->pipelineLayout, gBufferObject->graphicsPipeline, gBufferObject->descriptorSet, true);

				EndDebugMarkerRegion(commandBuffer);
			}

			{
				BeginDebugMarkerRegion(commandBuffer, "Copy depth");

				// TODO: Remove unnecessary transitions (use render passes where possible)

				// m_GBufferDepthAttachment was being read by SSAO, now needs to be copied to m_SwapChainDepthAttachment for forward rendering

				m_GBufferDepthAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_GraphicsQueue, commandBuffer);

				// m_OffscreenDepthAttachment0 is empty, needs to be copied into from data generated in gbuffer fill pass
				// NOTE: Handled by m_DeferredCombineRenderPass
				m_OffscreenDepthAttachment0->TransitionToLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_GraphicsQueue, commandBuffer);

				// TODO: Blit here instead when supported
				CopyImage(m_VulkanDevice, m_GraphicsQueue, m_GBufferDepthAttachment->image, m_OffscreenDepthAttachment0->image,
					m_SwapChainExtent.width, m_SwapChainExtent.height, commandBuffer, VK_IMAGE_ASPECT_DEPTH_BIT);

				// m_GBufferDepthAttachment has been copied out of, now must be transitioned back to read only for TAA resolve
				m_GBufferDepthAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_GraphicsQueue, commandBuffer);

				// m_OffscreenDepthAttachment0 was copied into, now will be written to in forward pass
				m_OffscreenDepthAttachment0->TransitionToLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, m_GraphicsQueue, commandBuffer);

				EndDebugMarkerRegion(commandBuffer);
			}

			std::array<VkClearValue, 2> clearValues = {};
			clearValues[0].color = m_ClearColor;
			clearValues[1].depthStencil = { 0.0f, 0 };

			VkRenderPassBeginInfo renderPassBeginInfo = vks::renderPassBeginInfo(m_ForwardRenderPass);
			renderPassBeginInfo.renderArea.offset = { 0, 0 };
			renderPassBeginInfo.renderArea.extent = m_SwapChainExtent;
			renderPassBeginInfo.clearValueCount = clearValues.size();
			renderPassBeginInfo.pClearValues = clearValues.data();
			renderPassBeginInfo.framebuffer = m_OffscreenFrameBuffer0->frameBuffer;

			//
			// Forward pass
			//

			vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			{
				{
					BeginDebugMarkerRegion(commandBuffer, "Forward");

					for (const ShaderBatchPair& shaderBatch : m_ForwardObjectBatches.batches)
					{
						DrawShaderBatch(shaderBatch, commandBuffer);
					}

					EndDebugMarkerRegion(commandBuffer);
				}

				{
					BeginDebugMarkerRegion(commandBuffer, "World Space Sprites");

					EnqueueWorldSpaceSprites();
					DrawSpriteBatch(m_QueuedWSSprites, commandBuffer);
					m_QueuedWSSprites.clear();

					EndDebugMarkerRegion(commandBuffer);
					BeginDebugMarkerRegion(commandBuffer, "World Space Text");

					EnqueueWorldSpaceText();
					DrawTextWS(commandBuffer);

					EndDebugMarkerRegion(commandBuffer);
				}

				bool bUsingGameplayCam = g_CameraManager->CurrentCamera()->bIsGameplayCam;
				if (g_EngineInstance->IsRenderingEditorObjects() && !bUsingGameplayCam)
				{
					BeginDebugMarkerRegion(commandBuffer, "Editor objects");

					for (const ShaderBatchPair& shaderBatch : m_DepthAwareEditorObjBatches.batches)
					{
						DrawShaderBatch(shaderBatch, commandBuffer);
					}

					// Selected object wireframe
					// TODO:

					//glDepthMask(GL_TRUE);
					//glClear(GL_DEPTH_BUFFER_BIT);

					// Depth unaware objects write to a cleared depth buffer so they
					// draw on top of previous geometry but are still eclipsed by other
					// depth unaware objects

					for (const ShaderBatchPair& shaderBatch : m_DepthUnawareEditorObjBatches.batches)
					{
						DrawShaderBatch(shaderBatch, commandBuffer);
					}

					EndDebugMarkerRegion(commandBuffer);
				}
			}
			vkCmdEndRenderPass(commandBuffer);

			//
			// Post process pass
			//
			
			{
				BeginGPUTimeStamp(commandBuffer, "Post Process");

				BeginDebugMarkerRegion(commandBuffer, "Post process");

				RenderFullscreenQuad(commandBuffer, m_PostProcessRenderPass, m_OffscreenFrameBuffer1->frameBuffer, m_Materials[m_PostProcessMatID].material.shaderID,
					m_PostProcessGraphicsPipelineLayout, m_PostProcessGraphicsPipeline, m_PostProcessDescriptorSet, true);

				EndDebugMarkerRegion(commandBuffer);

				EndGPUTimeStamp(commandBuffer, "Post Process");
			}

			const FrameBufferAttachment& offscreenBuffer0 = m_OffscreenFrameBuffer0->frameBufferAttachments[0].second;
			const FrameBufferAttachment& offscreenBuffer1 = m_OffscreenFrameBuffer1->frameBufferAttachments[0].second;

			if (m_bEnableTAA)
			{
				BeginGPUTimeStamp(commandBuffer, "TAA");

				// m_OffscreenFrameBuffer0 was being read from by post process, now needs to be written to again
				TransitionImageLayout(m_VulkanDevice, m_GraphicsQueue, offscreenBuffer0.image, offscreenBuffer0.format,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, commandBuffer, false);

				BeginDebugMarkerRegion(commandBuffer, "TAA Resolve");

				RenderFullscreenQuad(commandBuffer, m_TAAResolveRenderPass, m_OffscreenFrameBuffer0->frameBuffer, m_Materials[m_TAAResolveMaterialID].material.shaderID,
					m_TAAResolveGraphicsPipelineLayout, m_TAAResolveGraphicsPipeline, m_TAAResolveDescriptorSet, true);

				EndDebugMarkerRegion(commandBuffer);

				EndGPUTimeStamp(commandBuffer, "TAA");

				m_HistoryBuffer->TransitionToLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, commandBuffer);

				CopyImage(m_VulkanDevice, m_GraphicsQueue, offscreenBuffer0.image, m_HistoryBuffer->image,
					m_SwapChainExtent.width, m_SwapChainExtent.height, commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT);

				TransitionImageLayout(m_VulkanDevice, m_GraphicsQueue, offscreenBuffer0.image, offscreenBuffer0.format,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, commandBuffer, false);

				TransitionImageLayout(m_VulkanDevice, m_GraphicsQueue, offscreenBuffer1.image, offscreenBuffer1.format,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, commandBuffer, false);

				m_HistoryBuffer->TransitionToLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, commandBuffer);
			}

			{
				BeginDebugMarkerRegion(commandBuffer, "Gamma Correct");

				RenderFullscreenQuad(commandBuffer, m_GammaCorrectRenderPass, m_SwapChainFramebuffers[m_CurrentSwapChainBufferIndex], m_Materials[m_GammaCorrectMaterialID].material.shaderID,
					m_GammaCorrectGraphicsPipelineLayout, m_GammaCorrectGraphicsPipeline, m_GammaCorrectDescriptorSet, true);

				EndDebugMarkerRegion(commandBuffer);
			}

			BeginDebugMarkerRegion(commandBuffer, "UI");
			{

				VkRenderPassBeginInfo uiRenderPassBeginInfo = vks::renderPassBeginInfo(m_UIRenderPass);
				uiRenderPassBeginInfo.renderArea.offset = { 0, 0 };
				uiRenderPassBeginInfo.renderArea.extent = m_SwapChainExtent;
				uiRenderPassBeginInfo.clearValueCount = clearValues.size();
				uiRenderPassBeginInfo.pClearValues = clearValues.data();
				uiRenderPassBeginInfo.framebuffer = m_SwapChainFramebuffers[m_CurrentSwapChainBufferIndex];

				vkCmdBeginRenderPass(commandBuffer, &uiRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				{
					BeginDebugMarkerRegion(commandBuffer, "Screen Space Sprites");

					EnqueueScreenSpaceSprites();
					DrawSpriteBatch(m_QueuedSSSprites, commandBuffer);
					m_QueuedSSSprites.clear();
					DrawSpriteBatch(m_QueuedSSArrSprites, commandBuffer);
					m_QueuedSSArrSprites.clear();

					EndDebugMarkerRegion(commandBuffer);
				}

				{
					BeginDebugMarkerRegion(commandBuffer, "Screen Space Text");

					EnqueueScreenSpaceText();
					DrawTextSS(commandBuffer);

					EndDebugMarkerRegion(commandBuffer);
				}

				if (g_EngineInstance->IsRenderingImGui())
				{
					BeginDebugMarkerRegion(commandBuffer, "ImGui");

					ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

					EndDebugMarkerRegion(commandBuffer);
				}

				vkCmdEndRenderPass(commandBuffer);
			}
			
			// GBuffer depth is auto transitioned back to DS optimal for next frame
			// TODO: Track automatically somehow
			m_GBufferDepthAttachment->layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			EndDebugMarkerRegion(commandBuffer);

			EndGPUTimeStamp(commandBuffer, "Forward");

			VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));
			SetCommandBufferName(m_VulkanDevice, commandBuffer, "Forward command buffer");

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
			submitInfo.pSignalSemaphores = &m_OffscreenSemaphore;

			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &m_OffScreenCmdBuffer;

			VK_CHECK_RESULT(vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));

			// Scene rendering

			submitInfo.pWaitSemaphores = &m_OffscreenSemaphore;
			submitInfo.pSignalSemaphores = &m_RenderCompleteSemaphore;

			submitInfo.pCommandBuffers = &m_CommandBufferManager.m_CommandBuffers[0];
			VK_CHECK_RESULT(vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));



			VkPresentInfoKHR presentInfo = {};
			presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

			presentInfo.waitSemaphoreCount = 1;
			presentInfo.pWaitSemaphores = &m_RenderCompleteSemaphore;

			presentInfo.swapchainCount = 1;
			presentInfo.pSwapchains = &m_SwapChain;

			presentInfo.pImageIndices = &m_CurrentSwapChainBufferIndex;

			result = vkQueuePresentKHR(m_PresentQueue, &presentInfo);

			if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
			{
				RecreateSwapChain();
			}
			else
			{
				VK_CHECK_RESULT(result);
			}

			m_CurrentSwapChainBufferIndex = (imageIndex + 1) % m_SwapChainImages.size();

			// TODO: FIXME: BAD!! PLS REMOVE
			VK_CHECK_RESULT(vkQueueWaitIdle(m_PresentQueue));
		}

		void VulkanRenderer::BindDescriptorSet(VulkanShader* shader, u32 dynamicOffset, VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, VkDescriptorSet descriptorSet)
		{
			//u32 dynamicOffset = dynamicOffsetIndex * m_DynamicAlignment;
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

		void VulkanRenderer::RecreateSwapChain()
		{
			m_CurrentSwapChainBufferIndex = 0;

			const glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
			if (frameBufferSize.x == 0 || frameBufferSize.y == 0)
			{
				return;
			}

			vkDeviceWaitIdle(m_VulkanDevice->m_LogicalDevice);

			CreateSwapChain();
			CreateSwapChainImageViews();

			CreateDepthResources();

			CreateRenderPasses();
			PrepareFrameBuffers();

			for (u32 i = 0; i < m_Shaders.size(); ++i)
			{
				m_Shaders[i].renderPass = ResolveRenderPassType(m_Shaders[i].shader->renderPassType, m_Shaders[i].shader->name.c_str());
			}

			for (u32 i = 0; i < m_RenderObjects.size(); ++i)
			{
				CreateDescriptorSet(i);
				CreateGraphicsPipeline(i, false);
			}

			for (auto& iter : m_SpriteDescSets)
			{
				vkFreeDescriptorSets(m_VulkanDevice->m_LogicalDevice, m_DescriptorPool, 1, &(iter.second.descSet));
				iter.second.descSet = CreateSpriteDescSet(iter.second.shaderID, iter.first, iter.second.textureLayer);
			}

			CreateSSAODescriptorSets();
			CreateSSAOPipelines();

			CreatePostProcessingObjects();

			CreateFramebuffers();
			m_CommandBufferManager.CreateCommandBuffers(m_SwapChainImages.size());
		}

		void VulkanRenderer::SetObjectName(VulkanDevice* device, u64 object, VkDebugReportObjectTypeEXT type, const char* name)
		{
			if (name != nullptr && m_vkDebugMarkerSetObjectName != nullptr)
			{
				VkDebugMarkerObjectNameInfoEXT info = {};
				info.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT;
				info.object = object;
				info.objectType = type;
				info.pObjectName = name;
				VK_CHECK_RESULT(m_vkDebugMarkerSetObjectName(device->m_LogicalDevice, &info));
			}
		}

		void VulkanRenderer::SetCommandBufferName(VulkanDevice* device, VkCommandBuffer commandBuffer, const char* name)
		{
			SetObjectName(device, (u64)commandBuffer, VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT, name);
		}

		void VulkanRenderer::SetSwapchainName(VulkanDevice* device, VkSwapchainKHR swapchain, const char* name)
		{
			SetObjectName(device, swapchain, VK_DEBUG_REPORT_OBJECT_TYPE_SWAPCHAIN_KHR_EXT, name);
		}

		void VulkanRenderer::SetDescriptorSetName(VulkanDevice* device, VkDescriptorSet descSet, const char* name)
		{
			SetObjectName(device, descSet, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT, name);
		}

		void VulkanRenderer::SetPipelineName(VulkanDevice* device, VkPipeline pipeline, const char* name)
		{
			SetObjectName(device, pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, name);
		}

		void VulkanRenderer::SetFramebufferName(VulkanDevice* device, VkFramebuffer framebuffer, const char* name)
		{
			SetObjectName(device, framebuffer, VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT, name);
		}

		void VulkanRenderer::SetRenderPassName(VulkanDevice* device, VkRenderPass renderPass, const char* name)
		{
			SetObjectName(device, renderPass, VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT, name);
		}

		void VulkanRenderer::SetImageName(VulkanDevice* device, VkImage image, const char* name)
		{
			SetObjectName(device, image, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, name);
		}

		void VulkanRenderer::SetImageViewName(VulkanDevice* device, VkImageView imageView, const char* name)
		{
			SetObjectName(device, imageView, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT, name);
		}

		void VulkanRenderer::SetSamplerName(VulkanDevice* device, VkSampler sampler, const char* name)
		{
			SetObjectName(device, sampler, VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT, name);
		}

		void VulkanRenderer::SetBufferName(VulkanDevice* device, VkBuffer buffer, const char* name)
		{
			SetObjectName(device, buffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, name);
		}

		void VulkanRenderer::BeginDebugMarkerRegion(VkCommandBuffer cmdBuf, const char* markerName, glm::vec4 color)
		{
			if (m_vkCmdDebugMarkerBegin)
			{
				VkDebugMarkerMarkerInfoEXT markerInfo = {};
				markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
				memcpy(markerInfo.color, &color[0], sizeof(float) * 4);
				markerInfo.pMarkerName = markerName;
				m_vkCmdDebugMarkerBegin(cmdBuf, &markerInfo);
			}
		}

		void VulkanRenderer::EndDebugMarkerRegion(VkCommandBuffer cmdBuf)
		{
			if (m_vkCmdDebugMarkerEnd)
			{
				m_vkCmdDebugMarkerEnd(cmdBuf);
			}
		}

		bool VulkanRenderer::CreateShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule) const
		{
			VkShaderModuleCreateInfo createInfo = vks::shaderModuleCreateInfo();
			createInfo.codeSize = code.size();
			createInfo.pCode = (u32*)code.data();

			VkResult result = vkCreateShaderModule(m_VulkanDevice->m_LogicalDevice, &createInfo, nullptr, shaderModule);
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
			const VkPresentModeKHR bestMode = (m_bVSyncEnabled ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR);
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

		bool VulkanRenderer::IsDeviceSuitable(VkPhysicalDevice device)
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

		bool VulkanRenderer::CheckDeviceExtensionSupport(VkPhysicalDevice device)
		{
			u32 extensionCount;
			vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

			std::vector<VkExtensionProperties> extensions(extensionCount);
			vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());

			m_SupportedDeviceExtenions.clear();
			m_SupportedDeviceExtenions.reserve(extensionCount);
			for (VkExtensionProperties& prop : extensions)
			{
				m_SupportedDeviceExtenions.push_back(prop.extensionName);
			}

			std::set<std::string> requiredExtensions(m_RequiredDeviceExtensions.begin(), m_RequiredDeviceExtensions.end());

			for (const VkExtensionProperties& extension : extensions)
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
			// TODO: Don't call glfw functions directly in renderer
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

		bool VulkanRenderer::ExtensionSupported(const std::string& extStr) const
		{
			return (std::find(m_SupportedDeviceExtenions.begin(), m_SupportedDeviceExtenions.end(), extStr) != m_SupportedDeviceExtenions.end());
		}

		void VulkanRenderer::UpdateConstantUniformBuffers(UniformOverrides const* overridenUniforms)
		{
			BaseCamera* cam = g_CameraManager->CurrentCamera();
			glm::mat4 projection = cam->GetProjection();
			glm::mat4 projectionInv = glm::inverse(projection);
			glm::mat4 view = cam->GetView();
			glm::mat4 viewInv = glm::inverse(view);
			glm::mat4 viewProjection = cam->GetViewProjection();
			glm::vec4 camPos = glm::vec4(cam->GetPosition(), 0.0f);
			real exposure = cam->exposure;
			glm::vec2 m_NearFarPlanes(cam->GetZNear(), cam->GetZFar());

			static DirLightData defaultDirLightData = { VEC3_RIGHT, 0, VEC3_ONE, 0.0f, 0, 0.0f };

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

			if (overridenUniforms)
			{
				if (overridenUniforms->overridenUniforms.HasUniform(U_VIEW))
				{
					view = overridenUniforms->view;
				}
				if (overridenUniforms->overridenUniforms.HasUniform(U_VIEW_PROJECTION))
				{
					viewProjection = overridenUniforms->viewProjection;
				}
				if (overridenUniforms->overridenUniforms.HasUniform(U_PROJECTION))
				{
					projection = overridenUniforms->projection;
				}
				if (overridenUniforms->overridenUniforms.HasUniform(U_CAM_POS))
				{
					camPos = overridenUniforms->camPos;
				}

				viewInv = glm::inverse(view);
				projectionInv = glm::inverse(projection);
			}

			UniformInfo uniformInfos[] = {
				{ U_CAM_POS, (void*)&camPos, US_CAM_POS },
				{ U_VIEW, (void*)&view, US_VIEW },
				{ U_VIEW_INV, (void*)&viewInv, US_VIEW_INV },
				{ U_VIEW_PROJECTION, (void*)&viewProjection, US_VIEW_PROJECTION },
				{ U_PROJECTION, (void*)&projection, US_PROJECTION },
				{ U_PROJECTION_INV, (void*)&projectionInv, US_PROJECTION_INV },
				{ U_LAST_FRAME_VIEWPROJ, (void*)&m_LastFrameViewProj, US_LAST_FRAME_VIEWPROJ },
				{ U_DIR_LIGHT, (void*)dirLightData, US_DIR_LIGHT },
				{ U_POINT_LIGHTS, (void*)m_PointLights, US_POINT_LIGHTS },
				{ U_TIME, (void*)&g_SecElapsedSinceProgramStart, US_TIME },
				{ U_SHADOW_SAMPLING_DATA, (void*)&m_ShadowSamplingData, US_SHADOW_SAMPLING_DATA },
				{ U_SSAO_GEN_DATA, (void*)&m_SSAOGenData, US_SSAO_GEN_DATA },
				{ U_SSAO_BLUR_DATA_CONSTANT, (void*)&m_SSAOBlurDataConstant, US_SSAO_BLUR_DATA_CONSTANT },
				{ U_SSAO_SAMPLING_DATA, (void*)&m_SSAOSamplingData, US_SSAO_SAMPLING_DATA },
				{ U_FXAA_DATA, (void*)&m_FXAAData, US_FXAA_DATA },
				{ U_EXPOSURE, (void*)&exposure, US_EXPOSURE },
				{ U_NEAR_FAR_PLANES, (void*)&m_NearFarPlanes, US_NEAR_FAR_PLANES },
			};

			for (const VulkanShader& shader : m_Shaders)
			{
				const Uniforms& constantUniforms = shader.shader->constantBufferUniforms;
				const VulkanUniformBufferObjectData& constantData = shader.uniformBuffer.constantData;

				if (constantData.data == nullptr || constantData.size == 0)
				{
					continue; // There is no constant data to update
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
#endif

				memcpy(shader.uniformBuffer.constantBuffer.m_Mapped, constantData.data, size);
			}
		}

		void VulkanRenderer::UpdateDynamicUniformBuffer(RenderID renderID, UniformOverrides const* uniformOverrides /* = nullptr */,
			MaterialID materialIDOverride /* = InvalidMaterialID */, u32 dynamicUBOOffsetOverride /* = InvalidID */)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (renderObject == nullptr)
			{
				return;
			}

			MaterialID matID = materialIDOverride == InvalidMaterialID ? renderObject->materialID : materialIDOverride;
			u32 dynamicUBOOffset = dynamicUBOOffsetOverride == InvalidID ? renderObject->dynamicUBOOffset : dynamicUBOOffsetOverride;
			glm::mat4 model = renderObject->gameObject->GetTransform()->GetWorldTransform();
			UpdateDynamicUniformBuffer(matID, dynamicUBOOffset, model, uniformOverrides);
		}

		void VulkanRenderer::UpdateDynamicUniformBuffer(MaterialID materialID, u32 dynamicOffset, const glm::mat4& inModel,
			UniformOverrides const* uniformOverrides /* = nullptr */)
		{
			const VulkanMaterial& material = m_Materials[materialID];
			const VulkanShader& shader = m_Shaders[material.material.shaderID];

			const UniformBuffer& uniformBuffer = shader.uniformBuffer;

			if (uniformBuffer.dynamicBuffer.m_Size == 0)
			{
				return; // There are no dynamic uniforms to update
			}

			glm::mat4 model = inModel;
			glm::mat4 modelInvTranspose = glm::transpose(glm::inverse(model));
			glm::mat4 projection = g_CameraManager->CurrentCamera()->GetProjection();
			glm::mat4 view = g_CameraManager->CurrentCamera()->GetView();
			glm::mat4 viewProj = projection * view;
			glm::vec4 colorMultiplier = material.material.colorMultiplier;
			u32 enableAlbedoSampler = material.material.enableAlbedoSampler;
			u32 enableMetallicSampler = material.material.enableMetallicSampler;
			u32 enableRoughnessSampler = material.material.enableRoughnessSampler;
			u32 enableNormalSampler = material.material.enableNormalSampler;
			u32 enableIrradianceSampler = material.material.enableIrradianceSampler;
			real textureScale = material.material.textureScale;
			real blendSharpness = material.material.blendSharpness;
			glm::vec2 texSize = material.material.texSize;
			glm::vec4 fontCharData = material.material.fontCharData;
			glm::vec4 sdfData(0.5f, -0.01f, -0.008f, 0.035f);
			i32 texChannel = 0;
			glm::mat4 postProcessMat = GetPostProcessingMatrix();

			// TODO: Roll into array?
			if (uniformOverrides)
			{
				if (uniformOverrides->overridenUniforms.HasUniform(U_VIEW_PROJECTION))
				{
					viewProj = uniformOverrides->viewProjection;
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
				if (uniformOverrides->overridenUniforms.HasUniform(U_ENABLE_NORMAL_SAMPLER))
				{
					enableNormalSampler = uniformOverrides->enableNormalSampler;
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_ENABLE_IRRADIANCE_SAMPLER))
				{
					enableIrradianceSampler = uniformOverrides->enableIrradianceSampler;
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_SDF_DATA))
				{
					sdfData = uniformOverrides->sdfData;
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_TEX_SIZE))
				{
					texSize = uniformOverrides->texSize;
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_FONT_CHAR_DATA))
				{
					fontCharData = uniformOverrides->fontCharData;
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_TEX_CHANNEL))
				{
					texChannel = uniformOverrides->texChannel;
				}

				if (uniformOverrides->overridenUniforms.HasUniform(U_SSAO_BLUR_DATA_DYNAMIC))
				{
					if (uniformOverrides->bSSAOVerticalPass)
					{
						m_SSAOBlurDataDynamic.ssaoTexelOffset = glm::vec2(0.0f, (real)m_SSAOBlurSamplePixelOffset / m_GBufferFrameBuf->height);
					}
					else
					{
						m_SSAOBlurDataDynamic.ssaoTexelOffset = glm::vec2((real)m_SSAOBlurSamplePixelOffset / m_GBufferFrameBuf->width, 0.0f);
					}
				}
			}

			struct UniformInfo
			{
				u64 uniform;
				void* dataStart = nullptr;
				size_t copySize;
			};
			UniformInfo uniformInfos[] = {
				{ U_MODEL, (void*)&model, US_MODEL },
				// view, viewProjInv, viewProjection, projection, camPos, dirLight, pointLights should be updated in constant uniform buffer
				{ U_COLOR_MULTIPLIER, (void*)&material.material.colorMultiplier, US_COLOR_MULTIPLIER },
				{ U_CONST_ALBEDO, (void*)&material.material.constAlbedo, US_CONST_ALBEDO },
				{ U_CONST_METALLIC, (void*)&material.material.constMetallic, US_CONST_METALLIC },
				{ U_CONST_ROUGHNESS, (void*)&material.material.constRoughness, US_CONST_ROUGHNESS },
				{ U_ENABLE_ALBEDO_SAMPLER, (void*)&enableAlbedoSampler, US_ENABLE_ALBEDO_SAMPLER },
				{ U_ENABLE_METALLIC_SAMPLER, (void*)&enableMetallicSampler, US_ENABLE_METALLIC_SAMPLER },
				{ U_ENABLE_ROUGHNESS_SAMPLER, (void*)&enableRoughnessSampler, US_ENABLE_ROUGHNESS_SAMPLER },
				{ U_ENABLE_NORMAL_SAMPLER, (void*)&enableNormalSampler, US_ENABLE_NORMAL_SAMPLER },
				{ U_ENABLE_IRRADIANCE_SAMPLER, (void*)&enableIrradianceSampler, US_ENABLE_IRRADIANCE_SAMPLER },
				{ U_BLEND_SHARPNESS, (void*)&blendSharpness, US_BLEND_SHARPNESS },
				{ U_TEXTURE_SCALE, (void*)&textureScale, US_TEXTURE_SCALE },
				{ U_FONT_CHAR_DATA, (void*)&fontCharData, US_FONT_CHAR_DATA },
				{ U_TEX_SIZE, (void*)&texSize, US_TEX_SIZE },
				{ U_SDF_DATA, (void*)&sdfData, US_SDF_DATA },
				{ U_TEX_CHANNEL, (void*)&texChannel, US_TEX_CHANNEL },
				{ U_SSAO_BLUR_DATA_DYNAMIC, (void*)&m_SSAOBlurDataDynamic, US_SSAO_BLUR_DATA_DYNAMIC },
				{ U_POST_PROCESS_MAT, (void*)&postProcessMat, US_POST_PROCESS_MAT },
			};

			u32 index = 0;
			const Uniforms& dynamicUniforms = shader.shader->dynamicBufferUniforms;
			for (UniformInfo& uniformInfo : uniformInfos)
			{
				if (dynamicUniforms.HasUniform(uniformInfo.uniform))
				{
					// TODO: Don't store data twice? (in uniformBuffer.dynamicData.data & uniformBuffer.dynamicBuffer.m_Mapped)
					memcpy(&uniformBuffer.dynamicData.data[dynamicOffset + index], uniformInfo.dataStart, uniformInfo.copySize);
					index += uniformInfo.copySize / 4;
				}
			}

			// Aligned offset
			u32 size = uniformBuffer.dynamicData.size;

#if  DEBUG
			u32 calculatedSize1 = index * 4;
			calculatedSize1 = GetAlignedUBOSize(calculatedSize1);
			assert(calculatedSize1 == size);
#endif

			u64 firstIndex = (u64)uniformBuffer.dynamicBuffer.m_Mapped;
			u64 dest = firstIndex + dynamicOffset;
			memcpy((void*)(dest), &uniformBuffer.dynamicData.data[dynamicOffset], size);
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
				CreateDescriptorSet(i);
				CreateGraphicsPipeline(i, false);
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

		void VulkanRenderer::BeginGPUTimeStamp(VkCommandBuffer commandBuffer, const std::string& name)
		{
			const i32 queryIndex = (i32)(m_TimestampQueryNames.size() * 2);
			m_TimestampQueryNames[name] = queryIndex;

			vkCmdResetQueryPool(commandBuffer, m_TimestampQueryPool, (u32)queryIndex, 2);
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_TimestampQueryPool, (u32)queryIndex);
		}

		void VulkanRenderer::EndGPUTimeStamp(VkCommandBuffer commandBuffer, const std::string& name)
		{
			auto iter = m_TimestampQueryNames.find(name);
			if (iter == m_TimestampQueryNames.end())
			{
				PrintError("Attempted to end GPU timestamp that wasn't begun.\n");
				return;
			}

			const i32 queryIndex = iter->second;

			if (queryIndex < 0)
			{
				PrintError("Attempted to end GPU timestamp more than once.\n");
				return;
			}

			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_TimestampQueryPool, queryIndex + 1);

			iter->second = -queryIndex;
		}

		ms VulkanRenderer::GetDurationBetweenTimeStamps(const std::string& name)
		{
			auto iter = m_TimestampQueryNames.find(name);
			if (iter == m_TimestampQueryNames.end())
			{
				PrintError("Attempted to get duration of GPU timestamp that don't exist.\n");
				return 0.0f;
			}

			i32 queryIndex = iter->second;

			if (queryIndex > 0)
			{
				PrintError("Attempted to get duration of GPU timestamp that hasn't been ended.\n");
				return 0.0f;
			}

			queryIndex = -queryIndex;

			u64 timestamps[2];
			vkGetQueryPoolResults(m_VulkanDevice->m_LogicalDevice, m_TimestampQueryPool, queryIndex, 2, sizeof(u64) * 2, timestamps, sizeof(u64), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
			return (timestamps[1] - timestamps[0]) / 1000000.0f;
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
			case VK_DEBUG_REPORT_INFORMATION_BIT_EXT:
				Print("%s\n", msg);
				break;
			case VK_DEBUG_REPORT_WARNING_BIT_EXT:
				PrintWarn("%s\n", msg);
				break;
			case VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT:
				PrintWarn("%s\n", msg);
				break;
			case VK_DEBUG_REPORT_ERROR_BIT_EXT:
				PrintError("%s\n", msg);
				break;
			case VK_DEBUG_REPORT_DEBUG_BIT_EXT:
			default:
				PrintError("%s\n", msg);
				break;
			}

			return VK_FALSE;
		}

		const char* GetClipboardText(void* userData)
		{
			GLFWWindowWrapper* glfwWindow = static_cast<GLFWWindowWrapper*>(userData);
			return glfwWindow->GetClipboardText();
		}

	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN