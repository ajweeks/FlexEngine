#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanRenderer.hpp"

IGNORE_WARNINGS_PUSH
#include <vulkan/vulkan.hpp>

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
#include "Editor.hpp"
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
#include "Platform/Platform.hpp"
#include "ResourceManager.hpp"
#include "Profiler.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/LoadedMesh.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "UIMesh.hpp"
#include "volk/volk.h"
#include "Window/GLFWWindowWrapper.hpp"

namespace flex
{
	namespace vk
	{
		PFN_vkSetDebugUtilsObjectNameEXT VulkanRenderer::m_vkSetDebugUtilsObjectNameEXT = nullptr;
		PFN_vkCmdBeginDebugUtilsLabelEXT VulkanRenderer::m_vkCmdBeginDebugUtilsLabelEXT = nullptr;
		PFN_vkCmdEndDebugUtilsLabelEXT VulkanRenderer::m_vkCmdEndDebugUtilsLabelEXT = nullptr;
		PFN_vkGetPhysicalDeviceMemoryProperties2 VulkanRenderer::m_vkGetPhysicalDeviceMemoryProperties2 = nullptr;

		VulkanRenderer::VulkanRenderer() :
			m_ClearColour(VkClearColorValue{ 1.0f, 0.0f, 1.0f, 1.0f }),
			m_BRDFSize({ 512, 512 }),
			m_CubemapFramebufferSize({ 512, 512 })
		{
		}

		VulkanRenderer::~VulkanRenderer()
		{
		}

		void VulkanRenderer::Initialize()
		{
			Renderer::Initialize();

#if COMPILE_SHADER_COMPILER
			m_ShaderCompiler = new VulkanShaderCompiler(false);
			delete m_ShaderCompiler;
			m_ShaderCompiler = nullptr;
#endif

			// TODO: Kick off texture load here (most importantly, environment maps)

			LoadSettingsFromDisk();

			m_RenderObjects.resize(MAX_NUM_RENDER_OBJECTS);

			{
				VkResult res = volkInitialize();
				if (res != VK_SUCCESS)
				{
					PrintError("No valid Vulkan loader found on the system. Exiting...\n");
					exit(-1);
					return;
				}
			}

#if COMPILE_VULKAN
			if (!glfwVulkanSupported())
			{
				PrintError("Vulkan is not supported on this platform. Exiting...\n");
			}
#endif

			CreateInstance();

			SetupDebugCallback();
			CreateSurface();
			VkPhysicalDevice physicalDevice = PickPhysicalDevice();
			VulkanDevice::CreateInfo deviceCreateInfo = {};
			deviceCreateInfo.physicalDevice = physicalDevice;
			deviceCreateInfo.surface = m_Surface;
			deviceCreateInfo.requiredExtensions = &m_RequiredDeviceExtensions;
			deviceCreateInfo.optionalExtensions = &m_OptionalDeviceExtensions;
			// TODO: If device creation fails, try again without validation layers enabled
			deviceCreateInfo.bEnableValidationLayers = m_bEnableValidationLayers;
			deviceCreateInfo.validationLayers = &m_ValidationLayers;
			m_VulkanDevice = new VulkanDevice(deviceCreateInfo);

			m_bDiagnosticCheckpointsEnabled = m_VulkanDevice->ExtensionEnabled(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
			m_bMemoryBudgetExtensionEnabled = m_VulkanDevice->ExtensionEnabled(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);

			vkGetDeviceQueue(m_VulkanDevice->m_LogicalDevice, (u32)m_VulkanDevice->m_QueueFamilyIndices.graphicsFamily, 0, &m_GraphicsQueue);
			vkGetDeviceQueue(m_VulkanDevice->m_LogicalDevice, (u32)m_VulkanDevice->m_QueueFamilyIndices.presentFamily, 0, &m_PresentQueue);

			if (Contains(m_EnabledInstanceExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
			{
				m_vkSetDebugUtilsObjectNameEXT = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetInstanceProcAddr(m_Instance, "vkSetDebugUtilsObjectNameEXT"));
				m_vkCmdBeginDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(vkGetDeviceProcAddr(m_VulkanDevice->m_LogicalDevice, "vkCmdBeginDebugUtilsLabelEXT"));
				m_vkCmdEndDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetDeviceProcAddr(m_VulkanDevice->m_LogicalDevice, "vkCmdEndDebugUtilsLabelEXT"));
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

				u32 driverVersionMaj = VK_VERSION_MAJOR(props.driverVersion);
				u32 driverVersionMin = VK_VERSION_MINOR(props.driverVersion);
				u32 driverVersionPatch = VK_VERSION_PATCH(props.driverVersion);

				GPUVendor vendor = GPUVendorFromPCIVendor(props.vendorID);

				if (vendor == GPUVendor::nVidia)
				{
					// NVIDIA's custom version packing:
					//   10 |  8  |        8       |       6
					// major|minor|secondary_branch|tertiary_branch
					driverVersionMaj = ((u32)(props.driverVersion) >> (8 + 8 + 6)) & 0x3ff;
					driverVersionMin = ((u32)(props.driverVersion) >> (8 + 6)) & 0x0ff;

					u32 secondary = ((u32)(props.driverVersion) >> 6) & 0x0ff;
					u32 tertiary = props.driverVersion & 0x03f;

					driverVersionPatch = (secondary << 8) | tertiary;
				}


				Print("Vulkan loaded - instance v%u.%u.%u (device v%u.%u.%u)\n", instanceVersionMaj, instanceVersionMin, instanceVersionPatch, deviceVersionMaj, deviceVersionMin, deviceVersionPatch);
				Print("Vendor ID, Device ID: 0x%u, 0x%u\n", props.vendorID, props.deviceID);
				Print("Device info: %s, ", (const char*)props.deviceName);
				Print("(%s), ", DeviceTypeToString(props.deviceType).c_str());
				Print("driver version: %u.%u patch %u\n", driverVersionMaj, driverVersionMin, driverVersionPatch);
			}

			VkPhysicalDeviceMemoryProperties physicalDeviceMemProps;
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &physicalDeviceMemProps);
			Print("%u Memory heap(s) present on device: ", physicalDeviceMemProps.memoryHeapCount);
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

			if (!GetSupportedDepthFormat(m_VulkanDevice->m_PhysicalDevice, &m_DepthFormat))
			{
				PrintWarn("Failed to find suitable depth format\n");
			}

			m_CommandBufferManager = VulkanCommandBufferManager(m_VulkanDevice);

			FrameBufferAttachment::CreateInfo swapChainDepthCreateInfo = {};
			swapChainDepthCreateInfo.bIsDepth = true;
			swapChainDepthCreateInfo.bIsTransferedDst = true;
			swapChainDepthCreateInfo.format = m_DepthFormat;
			m_SwapChainDepthAttachment = new FrameBufferAttachment(m_VulkanDevice, swapChainDepthCreateInfo);

			FrameBufferAttachment::CreateInfo gBufferDepthCreateInfo = {};
			gBufferDepthCreateInfo.bIsDepth = true;
			gBufferDepthCreateInfo.bIsTransferedSrc = true;
			gBufferDepthCreateInfo.bIsSampled = true;
			gBufferDepthCreateInfo.format = m_DepthFormat;
			m_GBufferDepthAttachment = new FrameBufferAttachment(m_VulkanDevice, gBufferDepthCreateInfo);

			FrameBufferAttachment::CreateInfo offscreenDepthCreateInfo = {};
			offscreenDepthCreateInfo.bIsDepth = true;
			offscreenDepthCreateInfo.bIsTransferedSrc = true;
			offscreenDepthCreateInfo.bIsSampled = true;
			offscreenDepthCreateInfo.format = m_DepthFormat;
			m_OffscreenFB0DepthAttachment = new FrameBufferAttachment(m_VulkanDevice, offscreenDepthCreateInfo);
			m_OffscreenFB1DepthAttachment = new FrameBufferAttachment(m_VulkanDevice, offscreenDepthCreateInfo);

			FrameBufferAttachment::CreateInfo cubemapDepthCreateInfo = {};
			cubemapDepthCreateInfo.bIsDepth = true;
			cubemapDepthCreateInfo.bIsCubemap = true;
			cubemapDepthCreateInfo.format = m_DepthFormat;
			m_GBufferCubemapDepthAttachment = new FrameBufferAttachment(m_VulkanDevice, cubemapDepthCreateInfo);

			m_SwapChain = { m_VulkanDevice->m_LogicalDevice, vkDestroySwapchainKHR };

			for (u32 i = 0; i < ARRAY_SIZE(m_RenderPasses); ++i)
			{
				*(m_RenderPasses[i]) = new VulkanRenderPass(m_VulkanDevice);
			}

			m_PresentCompleteSemaphore = { m_VulkanDevice->m_LogicalDevice, vkDestroySemaphore };
			m_RenderCompleteSemaphore = { m_VulkanDevice->m_LogicalDevice, vkDestroySemaphore };

			m_LinMipLinSampler = { m_VulkanDevice->m_LogicalDevice, vkDestroySampler };
			m_DepthSampler = { m_VulkanDevice->m_LogicalDevice, vkDestroySampler };
			m_NearestClampEdgeSampler = { m_VulkanDevice->m_LogicalDevice, vkDestroySampler };

			m_ParticleSimulationComputePipelineLayout = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipelineLayout };

			m_ShadowImage = { m_VulkanDevice->m_LogicalDevice, vkDestroyImage };
			m_ShadowImageView = { m_VulkanDevice->m_LogicalDevice, vkDestroyImageView };
			m_ShadowImageMemory = { m_VulkanDevice->m_LogicalDevice, vkFreeMemory };

			CreateSwapChain();
			CreateSwapChainImageViews();

			m_OffscreenFrameBufferFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

			FrameBufferAttachment::CreateInfo frameBufCreateInfo = {};
			frameBufCreateInfo.format = m_OffscreenFrameBufferFormat;
			frameBufCreateInfo.bIsSampled = true;

			m_GBufferColourAttachment0 = new FrameBufferAttachment(m_VulkanDevice, frameBufCreateInfo); // RGB: normal A: roughness
			m_GBufferColourAttachment1 = new FrameBufferAttachment(m_VulkanDevice, frameBufCreateInfo); // RGB: albedo A: metallic

			m_OffscreenFB0ColourAttachment0 = new FrameBufferAttachment(m_VulkanDevice, frameBufCreateInfo);

			m_OffscreenFB1ColourAttachment0 = new FrameBufferAttachment(m_VulkanDevice, frameBufCreateInfo);
			m_OffscreenFB1ColourAttachment0->bIsTransferedSrc = true;

			m_HistoryBuffer = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "History buffer");
			m_HistoryBuffer->width = m_SwapChainExtent.width;
			m_HistoryBuffer->height = m_SwapChainExtent.height;
			m_HistoryBuffer->imageFormat = m_OffscreenFrameBufferFormat;

			frameBufCreateInfo.bIsCubemap = true;
			m_GBufferCubemapColourAttachment0 = new FrameBufferAttachment(m_VulkanDevice, frameBufCreateInfo);
			m_GBufferCubemapColourAttachment1 = new FrameBufferAttachment(m_VulkanDevice, frameBufCreateInfo);
			frameBufCreateInfo.bIsCubemap = false;

			frameBufCreateInfo.format = VK_FORMAT_R16_SFLOAT;
			m_SSAOFBColourAttachment0 = new FrameBufferAttachment(m_VulkanDevice, frameBufCreateInfo);
			m_SSAOBlurHFBColourAttachment0 = new FrameBufferAttachment(m_VulkanDevice, frameBufCreateInfo);
			m_SSAOBlurVFBColourAttachment0 = new FrameBufferAttachment(m_VulkanDevice, frameBufCreateInfo);

			m_ShadowBufFormat = VK_FORMAT_D16_UNORM;
			m_ShadowCascades.resize(m_ShadowCascadeCount);
			for (i32 i = 0; i < m_ShadowCascadeCount; ++i)
			{
				m_ShadowCascades[i] = new Cascade(m_VulkanDevice);
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

			// TODO: Create compute pool for creating compute-compatible (if different to graphics family) command buffers
			m_CommandBufferManager.CreatePool();

			// TODO: Always returning null for some reason...
			m_vkGetPhysicalDeviceMemoryProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2>(vkGetDeviceProcAddr(m_VulkanDevice->m_LogicalDevice, "vkGetPhysicalDeviceMemoryProperties2"));

			CreateFrameBufferAttachments();
			CreateDepthResources();
			CreateRenderPasses();

			CreateSwapChainFramebuffers();

			PrepareCubemapFrameBuffer();
			RecreateShadowFrameBuffers();

			{
				u32 blankData = 0xFFFFFFFF;
				m_BlankTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "blank");
				((VulkanTexture*)m_BlankTexture)->CreateFromMemory(&blankData, sizeof(blankData), 1, 1, 4, VK_FORMAT_R8G8B8A8_UNORM, 1);
				blankTextureID = g_ResourceManager->AddLoadedTexture(m_BlankTexture);

				m_BlankTextureArr = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "blank_arr");
				m_BlankTextureArr->bIsArray = true;
				((VulkanTexture*)m_BlankTextureArr)->CreateFromMemory(&blankData, sizeof(blankData), 1, 1, 4, VK_FORMAT_R8G8B8A8_UNORM, 1);
				blankTextureArrID = g_ResourceManager->AddLoadedTexture(m_BlankTextureArr);
			}

			alphaBGTextureID = InitializeTextureFromFile(TEXTURE_DIRECTORY "alpha-bg.png", false, false, false);
			pointLightIconID = g_ResourceManager->GetOrLoadIcon(SID("point light"));
			spotLightIconID = g_ResourceManager->GetOrLoadIcon(SID("spot light"));
			areaLightIconID = g_ResourceManager->GetOrLoadIcon(SID("area light"));
			directionalLightIconID = g_ResourceManager->GetOrLoadIcon(SID("directional light"));

			m_SpritePerspPushConstBlock = new Material::PushConstantBlock(128);
			m_SpriteOrthoPushConstBlock = new Material::PushConstantBlock(128);
			m_SpriteOrthoArrPushConstBlock = new Material::PushConstantBlock(132);

			LoadShaders();

			m_ShadowVertexIndexBufferPair = new VertexIndexBufferPair(new VulkanBuffer(m_VulkanDevice), new VulkanBuffer(m_VulkanDevice));

			// Allocate static vertex buffers
			for (u32 shaderID = 0; shaderID < m_Shaders.size(); ++shaderID)
			{
				const u32 stride = CalculateVertexStride(m_Shaders[shaderID]->vertexAttributes);
				if (stride == 0)
				{
					continue;
				}

				u32 staticVertexBufferIndex = 0;
				bool bExists = false;
				for (u32 bufferIndex = 0; bufferIndex < m_StaticVertexBuffers.size(); ++bufferIndex)
				{
					auto& pair = m_StaticVertexBuffers[bufferIndex];
					if (pair.first == stride)
					{
						bExists = true;
						staticVertexBufferIndex = bufferIndex;
						break;
					}
				}
				if (!bExists)
				{
					m_StaticVertexBuffers.emplace_back(stride, new VulkanBuffer(m_VulkanDevice));
					staticVertexBufferIndex = (u32)(m_StaticVertexBuffers.size() - 1);
				}
				m_Shaders[shaderID]->staticVertexBufferIndex = staticVertexBufferIndex;
			}

			// Allocate dynamic vertex buffers
			for (u32 shaderID = 0; shaderID < m_Shaders.size(); ++shaderID)
			{
				const u32 stride = CalculateVertexStride(m_Shaders[shaderID]->vertexAttributes);
				if (stride == 0)
				{
					continue;
				}

				bool bExists = false;
				for (u32 bufferIndex = 0; bufferIndex < m_DynamicVertexIndexBufferPairs.size(); ++bufferIndex)
				{
					auto& pair = m_DynamicVertexIndexBufferPairs[bufferIndex];
					if (pair.first == stride)
					{
						bExists = true;
						break;
					}
				}
				if (!bExists)
				{
					m_DynamicVertexIndexBufferPairs.emplace_back(stride, new VertexIndexBufferPair(new VulkanBuffer(m_VulkanDevice), new VulkanBuffer(m_VulkanDevice)));
				}
			}

			m_StaticIndexBuffer = new VulkanBuffer(m_VulkanDevice);

			m_FullScreenTriVertexBuffer = new VulkanBuffer(m_VulkanDevice);

			Renderer::InitializeMaterials();

			VkQueryPoolCreateInfo queryPoolCreateInfo = {};
			queryPoolCreateInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
			queryPoolCreateInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
			queryPoolCreateInfo.queryCount = MAX_TIMESTAMP_QUERIES;
			vkCreateQueryPool(m_VulkanDevice->m_LogicalDevice, &queryPoolCreateInfo, nullptr, &m_TimestampQueryPool);

			m_TAA_ks[0] = 2.25f; // KL
			m_TAA_ks[1] = 100.0f; // KH

			CreateAllDynamicVertexAndIndexBuffers();

			m_LTCMatricesID = InitializeTextureFromFile(TEXTURE_DIRECTORY "ltc_mat.hdr", false, false, true);
			m_LTCAmplitudesID = InitializeTextureFromFile(TEXTURE_DIRECTORY "ltc_amp.hdr", false, false, true);

			m_bInitialized = true;
		}

		void VulkanRenderer::PostInitialize()
		{
			Renderer::PostInitialize();

			m_DescriptorPool = new VulkanDescriptorPool(m_VulkanDevice);
			m_DescriptorPoolPersistent = new VulkanDescriptorPool(m_VulkanDevice);

			GenerateGBuffer();

			const glm::vec2i windowSize = g_Window->GetFrameBufferSize();
			OnWindowSizeChanged(windowSize.x, windowSize.y);

			// TODO: Pull out into functions
			assert(m_PhysicsDebugDrawer == nullptr);
			m_PhysicsDebugDrawer = new VulkanPhysicsDebugDraw();
			m_PhysicsDebugDrawer->Initialize();

			btDiscreteDynamicsWorld* world = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
			world->setDebugDrawer(m_PhysicsDebugDrawer);

			// Figure out largest shader uniform buffer to set m_DynamicAlignment correctly
			{
				u32 uboAlignment = (u32)m_VulkanDevice->m_PhysicalDeviceProperties.limits.minUniformBufferOffsetAlignment;

				for (u32 i = 0; i < m_Shaders.size(); ++i)
				{
					VulkanShader* shader = (VulkanShader*)m_Shaders[i];
					u32 size = GetAlignedUBOSize(shader->dynamicBufferUniforms.GetSizeInBytes());
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

			for (u32 i = 0; i < m_Shaders.size(); ++i)
			{
				m_DescriptorPool->CreateDescriptorSetLayout(i);
				m_DescriptorPoolPersistent->CreateDescriptorSetLayout(i);
			}

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
					ssaoMatCreateInfo.bSerializable = false;
					m_SSAOMatID = InitializeMaterial(&ssaoMatCreateInfo);
				}
				assert(m_SSAOMatID != InvalidMaterialID);
				m_SSAOShaderID = m_Materials[m_SSAOMatID]->shaderID;

				if (m_SSAOBlurMatID == InvalidMaterialID)
				{
					MaterialCreateInfo ssaoBlurMatCreateInfo = {};
					ssaoBlurMatCreateInfo.name = "ssao blur";
					ssaoBlurMatCreateInfo.shaderName = "ssao_blur";
					ssaoBlurMatCreateInfo.persistent = true;
					ssaoBlurMatCreateInfo.visibleInEditor = false;
					ssaoBlurMatCreateInfo.bSerializable = false;
					m_SSAOBlurMatID = InitializeMaterial(&ssaoBlurMatCreateInfo);
				}
				assert(m_SSAOBlurMatID != InvalidMaterialID);
				m_SSAOBlurShaderID = m_Materials[m_SSAOBlurMatID]->shaderID;
			}

			for (auto& matPair : m_Materials)
			{
				CreateUniformBuffers((VulkanMaterial*)matPair.second);
			}

			for (u32 i = 0; i < (u32)m_RenderObjects.size(); ++i)
			{
				CreateGraphicsPipeline(i);
			}

			CreateDescriptorSets();

			GenerateBRDFLUT();

			CreateSSAOPipelines();
			CreateSSAODescriptorSets();

			CreateWireframeDescriptorSets();

			CreateShadowResources();

			m_CommandBufferManager.CreateCommandBuffers((u32)m_SwapChainImages.size());

			GLFWWindowWrapper* castedWindow = dynamic_cast<GLFWWindowWrapper*>(g_Window);
			if (castedWindow == nullptr)
			{
				PrintError("VulkanRenderer::PostInitialize expects g_Window to be of type GLFWWindowWrapper!\n");
				return;
			}


			{
				// ImGui
				ImGui_ImplGlfw_InitForVulkan(castedWindow->GetWindow(), false);

				ImGui_ImplVulkan_InitInfo init_info = {};
				init_info.Instance = m_Instance;
				init_info.PhysicalDevice = m_VulkanDevice->m_PhysicalDevice;
				init_info.Device = *m_VulkanDevice;
				init_info.QueueFamily = m_VulkanDevice->m_QueueFamilyIndices.graphicsFamily;
				init_info.Queue = m_GraphicsQueue;
				init_info.PipelineCache = VK_NULL_HANDLE;
				init_info.DescriptorPool = m_DescriptorPoolPersistent->pool;
				init_info.Allocator = NULL;
				init_info.CheckVkResultFn = NULL;
				ImGui_ImplVulkan_Init(&init_info, *m_UIRenderPass);

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

			CreateAllStaticVertexBuffers();
			CreateStaticIndexBuffer();
			// NOTE: CreateAllDynamicVertexAndIndexBuffers will have been called in Initialize

			CreateShadowVertexBuffer();
			CreateShadowIndexBuffer();

			// Fullscreen tri vertex data
			{
				// TODO: Bring out to Mesh class?
				void* vertData = malloc(m_FullScreenTriVertexBufferData.VertexBufferSize);
				assert(vertData != nullptr);
				memcpy(vertData, m_FullScreenTriVertexBufferData.vertexData, m_FullScreenTriVertexBufferData.VertexBufferSize);
				CreateAndUploadToStaticVertexBuffer(m_FullScreenTriVertexBuffer, vertData, m_FullScreenTriVertexBufferData.VertexBufferSize);
				free(vertData);
			}

			CreateSemaphores();


			InitializeFreeType();
			// Needs to be called prior to rendering fonts, call here just in case
			UpdateConstantUniformBuffers();
			g_ResourceManager->LoadFonts(false);

			GenerateIrradianceMaps();

			CreatePostProcessingResources();
			CreateFullscreenBlitResources();
			CreateComputeResources();

			// TODO: Reduce calls to this to a minimum during startup
			UpdateConstantUniformBuffers();

			for (u32 i = 0; i < (u32)m_RenderObjects.size(); ++i)
			{
				UpdateDynamicUniformBuffer(i);
			}

			InitializeAllParticleSystemBuffers();

			m_bPostInitialized = true;

			PrintMemoryUsage();
		}

		void VulkanRenderer::Destroy()
		{
			Renderer::Destroy();

#if COMPILE_SHADER_COMPILER
			delete m_ShaderCompiler;
			m_ShaderCompiler = nullptr;
#endif

			vkQueueWaitIdle(m_GraphicsQueue);
			vkQueueWaitIdle(m_PresentQueue);
			vkDeviceWaitIdle(m_VulkanDevice->m_LogicalDevice);

			ImGui_ImplVulkan_Shutdown();
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();

			delete m_CascadedShadowMapPushConstantBlock;
			m_CascadedShadowMapPushConstantBlock = nullptr;

			m_DescriptorPoolPersistent->Replace();
			delete m_DescriptorPoolPersistent;
			m_DescriptorPoolPersistent = nullptr;

			m_DescriptorPool->Replace();
			delete m_DescriptorPool;
			m_DescriptorPool = nullptr;

			m_PresentCompleteSemaphore.replace();
			m_RenderCompleteSemaphore.replace();

			delete m_FullScreenTriVertexBuffer;
			m_FullScreenTriVertexBuffer = nullptr;

			m_SkyBoxMesh = nullptr;

			u32 activeRenderObjectCount = GetActiveRenderObjectCount();
			if (activeRenderObjectCount > 0)
			{
				PrintError("=====================================================\n");
				PrintError("%u render objects were not destroyed before Vulkan render\n", activeRenderObjectCount);

				for (VulkanRenderObject* renderObject : m_RenderObjects)
				{
					if (renderObject != nullptr)
					{
						if (renderObject->gameObject)
						{
							Print("%s\n", renderObject->gameObject->GetName().c_str());
						}
						DestroyRenderObject(renderObject->renderID);
					}
				}
				PrintError("=====================================================\n");
			}
			m_RenderObjects.clear();

			DestroyAllGraphicsPipelines();

			for (auto& pair : m_DynamicVertexIndexBufferPairs)
			{
				pair.second->Destroy();
				delete pair.second;
			}
			m_DynamicVertexIndexBufferPairs.clear();

			m_DynamicUIVertexIndexBufferPair->Destroy();
			delete m_DynamicUIVertexIndexBufferPair;
			m_DynamicUIVertexIndexBufferPair = nullptr;

			m_ShadowVertexIndexBufferPair->Destroy();
			delete m_ShadowVertexIndexBufferPair;
			m_ShadowVertexIndexBufferPair = nullptr;

			for (auto& pair : m_StaticVertexBuffers)
			{
				pair.second->Destroy();
				delete pair.second;
			}
			m_StaticVertexBuffers.clear();

			m_StaticIndexBuffer->Destroy();
			delete m_StaticIndexBuffer;
			m_StaticIndexBuffer = nullptr;

			for (Shader* shader : m_Shaders)
			{
				delete shader;
			}
			m_Shaders.clear();

			for (auto& materialPair : m_Materials)
			{
				VulkanMaterial* vkMaterial = (VulkanMaterial*)materialPair.second;
				if (vkMaterial->hdrCubemapFramebuffer != VK_NULL_HANDLE)
				{
					vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, vkMaterial->hdrCubemapFramebuffer, nullptr);
				}

				delete vkMaterial;
			}
			m_Materials.clear();

			for (VulkanParticleSystem* particleSystem : m_ParticleSystems)
			{
				delete particleSystem;
			}
			m_ParticleSystems.clear();

			delete m_SpritePerspPushConstBlock;
			m_SpritePerspPushConstBlock = nullptr;

			delete m_SpriteOrthoPushConstBlock;
			m_SpriteOrthoPushConstBlock = nullptr;

			delete m_SpriteOrthoArrPushConstBlock;
			m_SpriteOrthoArrPushConstBlock = nullptr;

			delete m_GBufferColourAttachment0;
			m_GBufferColourAttachment0 = nullptr;

			delete m_GBufferColourAttachment1;
			m_GBufferColourAttachment1 = nullptr;

			delete m_OffscreenFB0ColourAttachment0;
			m_OffscreenFB0ColourAttachment0 = nullptr;

			delete m_OffscreenFB1ColourAttachment0;
			m_OffscreenFB1ColourAttachment0 = nullptr;

			delete m_HistoryBuffer;
			m_HistoryBuffer = nullptr;

			delete m_SSAOFBColourAttachment0;
			m_SSAOFBColourAttachment0 = nullptr;

			delete m_SSAOBlurHFBColourAttachment0;
			m_SSAOBlurHFBColourAttachment0 = nullptr;

			delete m_SSAOBlurVFBColourAttachment0;
			m_SSAOBlurVFBColourAttachment0 = nullptr;

			delete m_GBufferCubemapColourAttachment0;
			m_GBufferCubemapColourAttachment0 = nullptr;

			delete m_GBufferCubemapColourAttachment1;
			m_GBufferCubemapColourAttachment1 = nullptr;

			delete m_GBufferCubemapDepthAttachment;
			m_GBufferCubemapDepthAttachment = nullptr;

			delete m_SwapChainDepthAttachment;
			m_SwapChainDepthAttachment = nullptr;

			delete m_GBufferDepthAttachment;
			m_GBufferDepthAttachment = nullptr;

			delete m_OffscreenFB0DepthAttachment;
			m_OffscreenFB0DepthAttachment = nullptr;

			delete m_OffscreenFB1DepthAttachment;
			m_OffscreenFB1DepthAttachment = nullptr;

			for (i32 i = 0; i < m_ShadowCascadeCount; ++i)
			{
				delete m_ShadowCascades[i];
			}

			for (u32 i = 0; i < ARRAY_SIZE(m_RenderPasses); ++i)
			{
				(*m_RenderPasses[i])->Replace();
				delete* m_RenderPasses[i];
			}

			for (u32 i = 0; i < m_SwapChainFramebuffers.size(); ++i)
			{
				delete m_SwapChainFramebuffers[i];
			}
			m_SwapChainFramebuffers.clear();

			for (u32 i = 0; i < m_SwapChainFramebufferAttachments.size(); ++i)
			{
				delete m_SwapChainFramebufferAttachments[i];
			}
			m_SwapChainFramebufferAttachments.clear();

			vkDestroySemaphore(m_VulkanDevice->m_LogicalDevice, m_OffscreenSemaphore, nullptr);

			m_ShadowImage.replace();
			m_ShadowImageView.replace();
			m_ShadowImageMemory.replace();

			m_WireframeGraphicsPipelines.clear();

			m_ParticleSimulationComputePipelineLayout.replace();

			m_LinMipLinSampler.replace();
			m_DepthSampler.replace();
			m_NearestClampEdgeSampler.replace();

			m_BlankTextureArr = nullptr;
			m_BlankTexture = nullptr;

			for (GameObject* editorObject : m_EditorObjects)
			{
				editorObject->Destroy();
				delete editorObject;
			}
			m_EditorObjects.clear();

			m_SwapChain.replace();
			m_SwapChainImageViews.clear();

			DestroyFreeType();

			vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);

			if (m_DebugUtilsMessengerCallback)
			{
				vkDestroyDebugUtilsMessengerEXT(m_Instance, m_DebugUtilsMessengerCallback, nullptr);
				m_DebugUtilsMessengerCallback = VK_NULL_HANDLE;
			}

			vkDestroyQueryPool(m_VulkanDevice->m_LogicalDevice, m_TimestampQueryPool, nullptr);
			m_TimestampQueryPool = VK_NULL_HANDLE;

			m_CommandBufferManager.DestroyCommandBuffers();

			vkDeviceWaitIdle(m_VulkanDevice->m_LogicalDevice);

			delete m_VulkanDevice;
			m_VulkanDevice = nullptr;

			vkDestroyInstance(m_Instance, nullptr);

			glfwTerminate();
		}

		MaterialID VulkanRenderer::InitializeMaterial(const MaterialCreateInfo* createInfo, MaterialID matToReplace /*= InvalidMaterialID*/)
		{
			MaterialID matID = InvalidMaterialID;
			if (matToReplace != InvalidMaterialID)
			{
				matID = matToReplace;
			}
			else
			{
				matID = GetNextAvailableMaterialID();
				m_Materials.emplace(matID, new VulkanMaterial());
				m_bRebatchRenderObjects = true;
			}

			VulkanMaterial* material = (VulkanMaterial*)m_Materials.at(matID);
			material->name = createInfo->name;

			if (!GetShaderID(createInfo->shaderName, material->shaderID))
			{
				if (createInfo->shaderName.empty())
				{
					PrintError("Material's shader not set! MaterialCreateInfo::shaderName must be filled in\n");
				}
				else
				{
					PrintError("Material's shader not set! Shader name %s not found\n", createInfo->shaderName.c_str());
				}

				return m_PlaceholderMaterialID;
			}

			VulkanShader* shader = (VulkanShader*)m_Shaders[material->shaderID];

			material->dynamicVertexIndexBufferIndex = GetDynamicVertexIndexBufferIndex(CalculateVertexStride(shader->vertexAttributes));
			material->bDynamic = createInfo->bDynamic;
			material->bSerializable = createInfo->bSerializable;

			if (shader->constantBufferUniforms.HasUniform(U_UNIFORM_BUFFER_CONSTANT))
			{
				material->uniformBufferList.Add(m_VulkanDevice, UniformBufferType::STATIC);
			}
			if (shader->dynamicBufferUniforms.HasUniform(U_UNIFORM_BUFFER_DYNAMIC))
			{
				material->uniformBufferList.Add(m_VulkanDevice, UniformBufferType::DYNAMIC);
			}
			if (shader->additionalBufferUniforms.HasUniform(U_PARTICLE_BUFFER))
			{
				material->uniformBufferList.Add(m_VulkanDevice, UniformBufferType::PARTICLE_DATA);
			}

			material->normalTexturePath = createInfo->normalTexturePath;
			material->enableNormalSampler = createInfo->enableNormalSampler;

			material->sampledFrameBuffers = createInfo->sampledFrameBuffers;

			material->enableCubemapSampler = createInfo->enableCubemapSampler;
			material->generateCubemapSampler = createInfo->generateCubemapSampler;
			material->cubemapSamplerSize = createInfo->generatedCubemapSize;

			material->constAlbedo = glm::vec4(createInfo->constAlbedo, 1.0f);
			material->albedoTexturePath = createInfo->albedoTexturePath;
			material->enableAlbedoSampler = createInfo->enableAlbedoSampler;

			material->constEmissive = glm::vec4(createInfo->constEmissive, 1.0f);
			material->emissiveTexturePath = createInfo->emissiveTexturePath;
			material->enableEmissiveSampler = createInfo->enableEmissiveSampler;

			material->constMetallic = createInfo->constMetallic;
			material->metallicTexturePath = createInfo->metallicTexturePath;
			material->enableMetallicSampler = createInfo->enableMetallicSampler;

			material->constRoughness = createInfo->constRoughness;
			material->roughnessTexturePath = createInfo->roughnessTexturePath;
			material->enableRoughnessSampler = createInfo->enableRoughnessSampler;

			material->colourMultiplier = createInfo->colourMultiplier;

			material->enableHDREquirectangularSampler = createInfo->enableHDREquirectangularSampler;
			material->generateHDREquirectangularSampler = createInfo->generateHDREquirectangularSampler;
			material->hdrEquirectangularTexturePath = createInfo->hdrEquirectangularTexturePath;

			material->generateHDRCubemapSampler = createInfo->generateHDRCubemapSampler;

			material->generateIrradianceSampler = createInfo->generateIrradianceSampler;
			material->irradianceSamplerSize = createInfo->generatedIrradianceCubemapSize;

			if (shader->bNeedPushConstantBlock)
			{
				material->pushConstantBlock = new Material::PushConstantBlock(shader->pushConstantBlockSize);
			}
			if (shader->textureUniforms.HasUniform(U_BRDF_LUT_SAMPLER))
			{
				if (!m_BRDFTexture)
				{
					m_BRDFTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "BRDF");
					u32 channelCount = 2;
					m_BRDFTexture->CreateEmpty(m_BRDFSize.x, m_BRDFSize.y, channelCount,
						VK_FORMAT_R16G16_SFLOAT, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
					g_ResourceManager->AddLoadedTexture(m_BRDFTexture);
				}
				material->textures.SetUniform(U_BRDF_LUT_SAMPLER, m_BRDFTexture, "BRDF");
			}
			if (shader->textureUniforms.HasUniform(U_IRRADIANCE_SAMPLER))
			{
				if (createInfo->irradianceSamplerMatID < m_Materials.size())
				{
					material->textures.SetUniform(U_IRRADIANCE_SAMPLER, m_Materials.at(createInfo->irradianceSamplerMatID)->textures[U_IRRADIANCE_SAMPLER], "Irradiance");
				}
			}
			if (shader->textureUniforms.HasUniform(U_PREFILTER_MAP))
			{
				VulkanTexture* prefilterTexture = nullptr;
				if (createInfo->prefilterMapSamplerMatID < m_Materials.size())
				{
					prefilterTexture = (VulkanTexture*)m_Materials.at(createInfo->prefilterMapSamplerMatID)->textures[U_PREFILTER_MAP];
				}
				material->textures.SetUniform(U_PREFILTER_MAP, prefilterTexture, "Prefilter");
			}

			material->enablePrefilteredMap = createInfo->enablePrefilteredMap;
			material->generatePrefilteredMap = createInfo->generatePrefilteredMap;
			material->prefilteredMapSize = createInfo->generatedPrefilteredCubemapSize;

			material->enableBRDFLUT = createInfo->enableBRDFLUT;
			material->renderToCubemap = createInfo->renderToCubemap;

			material->persistent = createInfo->persistent;
			material->visibleInEditor = createInfo->visibleInEditor;

			material->descriptorSetLayoutIndex = material->shaderID;

			struct TextureInfo
			{
				TextureInfo(const std::string& relativeFilePath,
					const Uniform& textureUniform,
					const std::string& slotName,
					VkFormat format = VK_FORMAT_R8G8B8A8_UNORM,
					bool bHDR = false) :
					relativeFilePath(relativeFilePath),
					textureUniform(textureUniform),
					slotName(slotName),
					format(format),
					bHDR(bHDR)
				{}

				const std::string relativeFilePath;
				const Uniform& textureUniform;
				const std::string slotName;
				VkFormat format;
				bool bHDR;
			};

			TextureInfo textureInfos[] =
			{
				{ createInfo->albedoTexturePath, U_ALBEDO_SAMPLER, "Albedo" },
				{ createInfo->emissiveTexturePath, U_EMISSIVE_SAMPLER, "Emissive" },
				{ createInfo->metallicTexturePath, U_METALLIC_SAMPLER , "Metallic"},
				{ createInfo->roughnessTexturePath, U_ROUGHNESS_SAMPLER, "Roughness" },
				{ createInfo->normalTexturePath, U_NORMAL_SAMPLER, "Normal" },
				{ createInfo->hdrEquirectangularTexturePath, U_HDR_EQUIRECTANGULAR_SAMPLER, "HDR Equirectangular", VK_FORMAT_R32G32B32A32_SFLOAT, true },
			};

			for (TextureInfo& textureInfo : textureInfos)
			{
				if (!textureInfo.relativeFilePath.empty())
				{
					VulkanTexture* texture = (VulkanTexture*)g_ResourceManager->FindLoadedTextureWithPath(textureInfo.relativeFilePath);

					if (texture == nullptr)
					{
						texture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue);
						texture->bHDR = textureInfo.bHDR;
						bool bGenerateMipChain = false;
						VkDeviceSize createdTextureSize = texture->CreateFromFile(textureInfo.relativeFilePath, textureInfo.format, bGenerateMipChain);

						if (createdTextureSize != 0)
						{
							texture->imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
							g_ResourceManager->AddLoadedTexture(texture);
						}
						else
						{
							delete texture;
						}
					}

					if (texture != nullptr)
					{
						material->textures.SetUniform(textureInfo.textureUniform, texture, textureInfo.slotName);
					}
					else
					{
						material->textures.SetUniform(textureInfo.textureUniform, m_BlankTexture, textureInfo.slotName);
					}
				}
				else
				{
					if (!material->textures.Contains(textureInfo.textureUniform) && shader->textureUniforms.HasUniform(textureInfo.textureUniform))
					{
						material->textures.SetUniform(textureInfo.textureUniform, m_BlankTexture, textureInfo.slotName);
					}
				}
			}

			// Cubemaps are treated differently than regular textures because they require 6 filepaths
			if (material->generateCubemapSampler)
			{
				assert(!material->textures.Contains(U_CUBEMAP_SAMPLER));

				const u32 mipLevels = static_cast<u32>(floor(log2(createInfo->generatedCubemapSize.x))) + 1;
				u32 channelCount = 4;
				VulkanTexture* cubemapTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "Cubemap");
				cubemapTexture->CreateCubemapEmpty((u32)createInfo->generatedCubemapSize.x, (u32)createInfo->generatedCubemapSize.y,
					channelCount, VK_FORMAT_R8G8B8A8_UNORM, mipLevels, createInfo->enableCubemapTrilinearFiltering);

				//texture->imageLayout = VK_IMAGE_LAYOUT_UNDEFINED; // TODO:Set this in creation function?

				g_ResourceManager->AddLoadedTexture(cubemapTexture);
				material->textures.SetUniform(U_CUBEMAP_SAMPLER, cubemapTexture, "Cubemap");
			}
			else if (material->generateHDRCubemapSampler)
			{
				assert(!material->textures.Contains(U_CUBEMAP_SAMPLER));

				const u32 mipLevels = static_cast<u32>(floor(log2(createInfo->generatedCubemapSize.x))) + 1;
				u32 channelCount = 4;
				VulkanTexture* cubemapTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "HDR Cubemap");
				cubemapTexture->CreateCubemapEmpty((u32)createInfo->generatedCubemapSize.x, (u32)createInfo->generatedCubemapSize.y,
					channelCount, VK_FORMAT_R32G32B32A32_SFLOAT, mipLevels, false);
				g_ResourceManager->AddLoadedTexture(cubemapTexture);
				material->textures.SetUniform(U_CUBEMAP_SAMPLER, cubemapTexture, "HDR Cubemap");
			}
			else
			{
				if (!material->textures.Contains(U_CUBEMAP_SAMPLER) && shader->textureUniforms.HasUniform(U_CUBEMAP_SAMPLER))
				{
					material->textures.SetUniform(U_CUBEMAP_SAMPLER, m_BlankTextureArr, "Cubemap"); // TODO: Array?
				}
			}

			if (material->generateIrradianceSampler)
			{
				assert(!material->textures.Contains(U_IRRADIANCE_SAMPLER));

				const u32 mipLevels = static_cast<u32>(floor(log2(createInfo->generatedIrradianceCubemapSize.x))) + 1;
				u32 channelCount = 4;
				VulkanTexture* irradianceTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "Irradiance sampler");
				irradianceTexture->CreateCubemapEmpty(
					(u32)createInfo->generatedIrradianceCubemapSize.x,
					(u32)createInfo->generatedIrradianceCubemapSize.y,
					channelCount,
					VK_FORMAT_R32G32B32A32_SFLOAT, mipLevels, false);
				g_ResourceManager->AddLoadedTexture(irradianceTexture);
				material->textures.SetUniform(U_IRRADIANCE_SAMPLER, irradianceTexture, "Irradiance");
			}
			else
			{
				if (!material->textures.Contains(U_IRRADIANCE_SAMPLER) && shader->textureUniforms.HasUniform(U_IRRADIANCE_SAMPLER))
				{
					material->textures.SetUniform(U_IRRADIANCE_SAMPLER, m_BlankTexture, "Irradiance");
				}
			}

			if (material->generatePrefilteredMap)
			{
				assert(!material->textures.Contains(U_PREFILTER_MAP));

				const u32 mipLevels = static_cast<u32>(floor(log2(createInfo->generatedPrefilteredCubemapSize.x))) + 1;
				u32 channelCount = 4;
				VulkanTexture* prefilterTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "Prefiltered map");
				prefilterTexture->CreateCubemapEmpty(
					(u32)createInfo->generatedPrefilteredCubemapSize.x,
					(u32)createInfo->generatedPrefilteredCubemapSize.y,
					channelCount,
					VK_FORMAT_R16G16B16A16_SFLOAT, mipLevels, true);
				g_ResourceManager->AddLoadedTexture(prefilterTexture);
				material->textures.SetUniform(U_PREFILTER_MAP, prefilterTexture, "Prefilter");
			}
			else
			{
				if (!material->textures.Contains(U_PREFILTER_MAP) && shader->textureUniforms.HasUniform(U_PREFILTER_MAP))
				{
					material->textures.SetUniform(U_PREFILTER_MAP, m_BlankTextureArr, "Prefilter"); // TODO: Array?
				}
			}

			if (shader->textureUniforms.HasUniform(U_NOISE_SAMPLER))
			{
				if (m_NoiseTexture == nullptr)
				{
					std::vector<glm::vec4> ssaoNoise;
					GenerateSSAONoise(ssaoNoise);

					m_NoiseTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "SSAO Noise");
					void* buffer = ssaoNoise.data();
					u32 bufferSize = (u32)ssaoNoise.size() * sizeof(glm::vec4);
					u32 channelCount = 1;
					((VulkanTexture*)m_NoiseTexture)->CreateFromMemory(buffer, bufferSize, SSAO_NOISE_DIM, SSAO_NOISE_DIM, channelCount,
						VK_FORMAT_R32G32B32A32_SFLOAT, 1, VK_FILTER_NEAREST);
					g_ResourceManager->AddLoadedTexture(m_NoiseTexture);
				}

				material->textures.SetUniform(U_NOISE_SAMPLER, m_NoiseTexture, "SSAO Noise");
			}

			if (m_bPostInitialized)
			{
				CreateUniformBuffers(material);
			}

			return matID;
		}

		TextureID VulkanRenderer::InitializeTextureFromFile(const std::string& relativeFilePath, bool bFlipVertically, bool bGenerateMipMaps, bool bHDR)
		{
			VulkanTexture* newTex = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue);
			newTex->bHDR = bHDR;
			newTex->bFlipVertically = bFlipVertically;
			VkDeviceSize newTexSize = newTex->CreateFromFile(relativeFilePath, VK_FORMAT_UNDEFINED, bGenerateMipMaps);
			if (newTexSize == 0)
			{
				delete newTex;
				return InvalidTextureID;
			}

			TextureID textureID = g_ResourceManager->AddLoadedTexture(newTex);
			return textureID;
		}

		TextureID VulkanRenderer::InitializeTextureFromMemory(void* data, u32 size, VkFormat inFormat, const std::string& name, u32 width, u32 height, u32 channelCount, VkFilter inFilter)
		{
			VulkanTexture* newTex = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, name);
			newTex->CreateFromMemory(data, size, width, height, channelCount, inFormat, 1, inFilter);
			TextureID textureID = g_ResourceManager->AddLoadedTexture(newTex);

			return textureID;
		}

		u32 VulkanRenderer::InitializeRenderObject(const RenderObjectCreateInfo* createInfo)
		{
			const RenderID renderID = GetNextAvailableRenderID();
			VulkanRenderObject* renderObject = new VulkanRenderObject(renderID);

			m_bRebatchRenderObjects = true;

			InsertNewRenderObject(renderObject);
			renderObject->materialID = createInfo->materialID;

			if (renderObject->materialID == InvalidMaterialID)
			{
				if (m_Materials.empty())
				{
					PrintError("Render object created before any Materials have been created! Returning...\n");
					return InvalidRenderID;
				}
				else
				{
					PrintError("Render object doesn't have its Material ID set! Using placeholder Material\n");
					renderObject->materialID = m_PlaceholderMaterialID;
				}
			}

			renderObject->vertexBufferData = createInfo->vertexBufferData;
			renderObject->cullMode = CullFaceToVkCullMode(createInfo->cullFace);
			renderObject->gameObject = createInfo->gameObject;
			renderObject->bEditorObject = createInfo->bEditorObject;
			if (createInfo->bEditorObject)
			{
				renderObject->gameObject->SetCastsShadow(false);
			}
			renderObject->depthCompareOp = DepthTestFuncToVkCompareOp(createInfo->depthTestReadFunc);
			renderObject->bSetDynamicStates = createInfo->bSetDynamicStates;
			renderObject->renderPassOverride = createInfo->renderPassOverride;
			renderObject->bAllowDynamicBufferShrinking = createInfo->bAllowDynamicBufferShrinking;

			if (createInfo->indices != nullptr)
			{
				renderObject->indices = createInfo->indices;
			}
			if ((renderObject->indices != nullptr && !renderObject->indices->empty()) || createInfo->bIndexed)
			{
				renderObject->bIndexed = true;
			}


			// We've already been post initialized, so we need to create a descriptor set and pipeline here
			if (m_bPostInitialized)
			{
				CreateGraphicsPipeline(renderID);
				if (GetMaterial(renderObject->materialID)->persistent)
				{
					m_DescriptorPoolPersistent->CreateDescriptorSet(renderObject->materialID);
				}
				else
				{
					m_DescriptorPool->CreateDescriptorSet(renderObject->materialID);
				}
			}

			Material* gameObjectMat = GetMaterial(renderObject->materialID);
			if (gameObjectMat != nullptr)
			{
				if (renderObject->vertexBufferData != nullptr && renderObject->vertexBufferData->bDynamic)
				{
					Shader* gameObjectShader = GetShader(gameObjectMat->shaderID);
					u32 dynamicVertexIndexBufferIndex = GetDynamicVertexIndexBufferIndex(CalculateVertexStride(gameObjectShader->vertexAttributes));
					SetDynamicGeometryBufferDirty(dynamicVertexIndexBufferIndex);
				}
				else
				{
					SetStaticGeometryBufferDirty(m_Shaders[gameObjectMat->shaderID]->staticVertexBufferIndex);
				}
			}

			if (renderObject->gameObject != nullptr && renderObject->gameObject->CastsShadow())
			{
				m_DirtyFlagBits |= RenderBatchDirtyFlag::SHADOW_DATA;
			}

			return renderID;
		}

		void VulkanRenderer::PostInitializeRenderObject(RenderID renderID)
		{
			FLEX_UNUSED(renderID);
			m_bRebatchRenderObjects = true;
		}

		void VulkanRenderer::OnTextureDestroyed(TextureID textureID)
		{
			auto spriteDescSetIter = m_SpriteDescSets.find(textureID);
			if (spriteDescSetIter != m_SpriteDescSets.end())
			{
				m_SpriteDescSets.erase(spriteDescSetIter);
			}
		}

		void VulkanRenderer::ReplaceMaterialsOnObjects(MaterialID oldMatID, MaterialID newMatID)
		{
			for (RenderID renderID = 0; renderID < MAX_NUM_RENDER_OBJECTS; ++renderID)
			{
				VulkanRenderObject* renderObject = m_RenderObjects[renderID];
				if (renderObject != nullptr)
				{
					if (renderObject->materialID == oldMatID)
					{
						RenderObjectCreateInfo info;
						GetRenderObjectCreateInfo(renderID, info);
						DestroyRenderObject(renderID, renderObject);
						info.materialID = newMatID;
						InitializeRenderObject(&info);
					}
				}
			}
		}

		void VulkanRenderer::CreateUniformBuffers(VulkanMaterial* material)
		{
			CreateStaticUniformBuffer(material);
			CreateDynamicUniformBuffer(material);
			CreateParticleUniformBuffer(material);
		}

		void VulkanRenderer::CreateStaticUniformBuffer(VulkanMaterial* material)
		{
			VulkanShader* shader = (VulkanShader*)m_Shaders[material->shaderID];

			if (shader->constantBufferUniforms.HasUniform(U_UNIFORM_BUFFER_CONSTANT))
			{
				UniformBuffer* constantBuffer = material->uniformBufferList.Get(UniformBufferType::STATIC);
				constantBuffer->data.unitSize = shader->constantBufferUniforms.GetSizeInBytes();
				if (constantBuffer->data.unitSize > 0)
				{
					free(constantBuffer->data.data);
					constantBuffer->data.data = nullptr;

					constantBuffer->data.unitSize = GetAlignedUBOSize(constantBuffer->data.unitSize);

					constantBuffer->data.data = static_cast<u8*>(malloc(constantBuffer->data.unitSize));
					assert(constantBuffer->data.data);

					PrepareUniformBuffer(&constantBuffer->buffer, constantBuffer->data.unitSize,
						VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				}
			}
		}

		void VulkanRenderer::CreateDynamicUniformBuffer(VulkanMaterial* material)
		{
			VulkanShader* shader = (VulkanShader*)m_Shaders[material->shaderID];

			if (shader->dynamicBufferUniforms.HasUniform(U_UNIFORM_BUFFER_DYNAMIC))
			{
				UniformBuffer* dynamicBuffer = material->uniformBufferList.Get(UniformBufferType::DYNAMIC);
				dynamicBuffer->data.unitSize = shader->dynamicBufferUniforms.GetSizeInBytes();
				if (dynamicBuffer->data.unitSize > 0)
				{
					flex_aligned_free(dynamicBuffer->data.data);
					dynamicBuffer->data.data = nullptr;

					dynamicBuffer->data.unitSize = GetAlignedUBOSize(dynamicBuffer->data.unitSize);

					const u32 dynamicBufferSize = AllocateDynamicUniformBuffer(dynamicBuffer->data.unitSize, (void**)&dynamicBuffer->data.data, shader->maxObjectCount);
					dynamicBuffer->fullDynamicBufferSize = dynamicBufferSize;
					if (dynamicBufferSize > 0)
					{
						PrepareUniformBuffer(&dynamicBuffer->buffer, dynamicBufferSize,
							VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
					}
				}
			}
		}

		void VulkanRenderer::CreateParticleUniformBuffer(VulkanMaterial* material)
		{
			VulkanShader* shader = (VulkanShader*)m_Shaders[material->shaderID];

			if (shader->additionalBufferUniforms.HasUniform(U_PARTICLE_BUFFER))
			{
				UniformBuffer* particleBuffer = material->uniformBufferList.Get(UniformBufferType::PARTICLE_DATA);
				flex_aligned_free(particleBuffer->data.data);
				particleBuffer->data.data = nullptr;

				particleBuffer->data.unitSize = GetAlignedUBOSize(MAX_PARTICLE_COUNT * sizeof(ParticleBufferData));

				particleBuffer->data.data = static_cast<u8*>(flex_aligned_malloc(particleBuffer->data.unitSize, m_DynamicAlignment));
				// Will be copied into from staging buffer
				PrepareUniformBuffer(&particleBuffer->buffer, particleBuffer->data.unitSize,
					VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
			}
		}

		void VulkanRenderer::CreatePostProcessingResources()
		{
			// Post process descriptor set
			{
				VulkanMaterial* postProcessMaterial = (VulkanMaterial*)m_Materials[m_PostProcessMatID];
				ShaderID postProcessShaderID = postProcessMaterial->shaderID;
				VkDescriptorSetLayout descSetLayout = m_DescriptorPoolPersistent->descriptorSetLayouts[postProcessShaderID];

				DescriptorSetCreateInfo descSetCreateInfo = {};
				descSetCreateInfo.DBG_Name = "Post Process descriptor set";
				descSetCreateInfo.descriptorSetLayout = &descSetLayout;
				descSetCreateInfo.shaderID = postProcessShaderID;
				descSetCreateInfo.uniformBufferList = &postProcessMaterial->uniformBufferList;
				descSetCreateInfo.imageDescriptors.SetUniform(U_SCENE_SAMPLER, ImageDescriptorInfo{ m_OffscreenFB0ColourAttachment0->view, m_LinMipLinSampler });
				FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
				m_PostProcessDescriptorSet = m_DescriptorPoolPersistent->CreateDescriptorSet(&descSetCreateInfo);
			}

			// Gamma Correct descriptor set
			{
				VulkanMaterial* gammaCorrectMaterial = (VulkanMaterial*)m_Materials[m_GammaCorrectMaterialID];
				ShaderID gammaCorrectShaderID = gammaCorrectMaterial->shaderID;
				VkDescriptorSetLayout descSetLayout = m_DescriptorPoolPersistent->descriptorSetLayouts[gammaCorrectShaderID];

				DescriptorSetCreateInfo descSetCreateInfo = {};
				descSetCreateInfo.DBG_Name = "Gamma Correct descriptor set";
				descSetCreateInfo.descriptorSetLayout = &descSetLayout;
				descSetCreateInfo.shaderID = gammaCorrectShaderID;
				descSetCreateInfo.uniformBufferList = &gammaCorrectMaterial->uniformBufferList;
				descSetCreateInfo.imageDescriptors.SetUniform(U_SCENE_SAMPLER, ImageDescriptorInfo{ m_OffscreenFB1ColourAttachment0->view, m_LinMipLinSampler });
				FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
				m_GammaCorrectDescriptorSet = m_DescriptorPoolPersistent->CreateDescriptorSet(&descSetCreateInfo);
			}

			// TAA Resolve descriptor set
			{
				VulkanMaterial* taaResolveMaterial = (VulkanMaterial*)m_Materials[m_TAAResolveMaterialID];
				ShaderID taaResolveShaderID = taaResolveMaterial->shaderID;
				VkDescriptorSetLayout descSetLayout = m_DescriptorPoolPersistent->descriptorSetLayouts[taaResolveShaderID];

				DescriptorSetCreateInfo descSetCreateInfo = {};
				descSetCreateInfo.DBG_Name = "TAA Resolve descriptor set";
				descSetCreateInfo.descriptorSetLayout = &descSetLayout;
				descSetCreateInfo.shaderID = taaResolveShaderID;
				descSetCreateInfo.uniformBufferList = &taaResolveMaterial->uniformBufferList;
				descSetCreateInfo.imageDescriptors.SetUniform(U_DEPTH_SAMPLER, ImageDescriptorInfo{ m_GBufferDepthAttachment->view, m_DepthSampler });
				descSetCreateInfo.imageDescriptors.SetUniform(U_SCENE_SAMPLER, ImageDescriptorInfo{ m_OffscreenFB0ColourAttachment0->view, m_LinMipLinSampler });
				descSetCreateInfo.imageDescriptors.SetUniform(U_HISTORY_SAMPLER, ImageDescriptorInfo{ m_HistoryBuffer->imageView, m_LinMipLinSampler });
				FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
				m_TAAResolveDescriptorSet = m_DescriptorPoolPersistent->CreateDescriptorSet(&descSetCreateInfo);
			}

			// Sprite array pipeline
			{
				VulkanMaterial* spriteArrMat = (VulkanMaterial*)m_Materials[m_SpriteArrMatID];
				VulkanShader* spriteArrShader = (VulkanShader*)m_Shaders[spriteArrMat->shaderID];

				std::array<VkPushConstantRange, 1> pushConstantRanges = {};
				pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
				pushConstantRanges[0].offset = 0;
				pushConstantRanges[0].size = spriteArrShader->pushConstantBlockSize;

				GraphicsPipelineCreateInfo createInfo = {};
				createInfo.DBG_Name = "Sprite Array pipeline";
				createInfo.renderPass = *m_UIRenderPass;
				createInfo.shaderID = spriteArrMat->shaderID;
				createInfo.vertexAttributes = m_Quad3DVertexBufferData.Attributes;
				createInfo.descriptorSetLayoutIndex = spriteArrMat->shaderID;
				createInfo.bEnableColourBlending = true;
				createInfo.depthTestEnable = VK_FALSE;
				createInfo.depthWriteEnable = VK_FALSE;
				createInfo.pushConstantRangeCount = (u32)pushConstantRanges.size();
				createInfo.pushConstants = pushConstantRanges.data();
				createInfo.bPersistent = true;
				CreateGraphicsPipeline(&createInfo, m_SpriteArrGraphicsPipelineID);
			}

			// Post process pipeline
			{
				VulkanMaterial* postProcessMat = (VulkanMaterial*)m_Materials[m_PostProcessMatID];

				GraphicsPipelineCreateInfo createInfo = {};
				createInfo.DBG_Name = "Post Process pipeline";
				createInfo.renderPass = *m_PostProcessRenderPass;
				createInfo.shaderID = postProcessMat->shaderID;
				createInfo.vertexAttributes = m_FullScreenTriVertexBufferData.Attributes;
				createInfo.descriptorSetLayoutIndex = postProcessMat->shaderID;
				createInfo.bSetDynamicStates = true;
				createInfo.depthTestEnable = VK_FALSE;
				createInfo.depthWriteEnable = VK_FALSE;
				createInfo.bPersistent = true;
				CreateGraphicsPipeline(&createInfo, m_PostProcessGraphicsPipelineID);
			}

			// Gamma Correct pipeline
			{
				VulkanMaterial* gammaCorrectMat = (VulkanMaterial*)m_Materials[m_GammaCorrectMaterialID];

				GraphicsPipelineCreateInfo createInfo = {};
				createInfo.DBG_Name = "Gamma Correct pipeline";
				createInfo.renderPass = *m_GammaCorrectRenderPass;
				createInfo.shaderID = gammaCorrectMat->shaderID;
				createInfo.vertexAttributes = m_FullScreenTriVertexBufferData.Attributes;
				createInfo.descriptorSetLayoutIndex = gammaCorrectMat->shaderID;
				createInfo.bSetDynamicStates = true;
				createInfo.depthTestEnable = VK_FALSE;
				createInfo.depthWriteEnable = VK_FALSE;
				createInfo.bPersistent = true;
				CreateGraphicsPipeline(&createInfo, m_GammaCorrectGraphicsPipelineID);
			}

			// TAA Resolve pipeline
			{
				VulkanMaterial* taaResolveMat = (VulkanMaterial*)m_Materials[m_TAAResolveMaterialID];
				VulkanShader* taaResolveShader = (VulkanShader*)m_Shaders[taaResolveMat->shaderID];

				std::array<VkPushConstantRange, 1> pushConstantRanges = {};
				pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
				pushConstantRanges[0].offset = 0;
				pushConstantRanges[0].size = taaResolveShader->pushConstantBlockSize;

				GraphicsPipelineCreateInfo createInfo = {};
				createInfo.DBG_Name = "TAA Resolve pipeline";
				createInfo.renderPass = *m_TAAResolveRenderPass;
				createInfo.shaderID = taaResolveMat->shaderID;
				createInfo.vertexAttributes = m_FullScreenTriVertexBufferData.Attributes;
				createInfo.descriptorSetLayoutIndex = taaResolveMat->shaderID;
				createInfo.bSetDynamicStates = true;
				createInfo.depthTestEnable = VK_FALSE;
				createInfo.depthWriteEnable = VK_FALSE;
				createInfo.fragSpecializationInfo = taaResolveShader->fragSpecializationInfo;
				createInfo.pushConstantRangeCount = (u32)pushConstantRanges.size();
				createInfo.pushConstants = pushConstantRanges.data();
				createInfo.bPersistent = true;
				CreateGraphicsPipeline(&createInfo, m_TAAResolveGraphicsPipelineID);
			}
		}

		VkSpecializationInfo* VulkanRenderer::GenerateSpecializationInfo(const std::vector<SpecializationConstantCreateInfo>& entries)
		{
			VkSpecializationInfo* result = new VkSpecializationInfo();
			result->mapEntryCount = (u32)entries.size();

			for (const SpecializationConstantCreateInfo& entry : entries)
			{
				result->dataSize += entry.size;
			}

			if (result->dataSize == 0)
			{
				delete result;
				return nullptr;
			}

			result->pData = malloc(result->dataSize);
			assert(result->pData != nullptr);
			{
				u8* data = (u8*)result->pData;
				for (const SpecializationConstantCreateInfo& entry : entries)
				{
					memcpy(data, entry.data, entry.size);
					data += entry.size;
				}
				assert(data == ((u8*)result->pData + result->dataSize));
			}

			VkSpecializationMapEntry* mapEntries = (VkSpecializationMapEntry*)malloc(result->mapEntryCount * sizeof(VkSpecializationMapEntry));
			assert(mapEntries != nullptr);

			u32 offset = 0;
			for (u32 i = 0; i < result->mapEntryCount; ++i)
			{
				const SpecializationConstantCreateInfo& entry = entries[i];
				mapEntries[i].constantID = (u32)entry.constantID;
				mapEntries[i].offset = offset;
				mapEntries[i].size = entry.size;
				offset += entry.size;
			}

			result->pMapEntries = mapEntries;

			return result;
		}

		void VulkanRenderer::CreateFullscreenBlitResources()
		{
			VulkanMaterial* fullscreenBlitMat = (VulkanMaterial*)m_Materials[m_FullscreenBlitMatID];
			ShaderID fullscreenShaderID = fullscreenBlitMat->shaderID;
			VulkanShader* fullscreenShader = (VulkanShader*)m_Shaders[fullscreenShaderID];

			{
				VkDescriptorSetLayout descSetLayout = m_DescriptorPoolPersistent->descriptorSetLayouts[fullscreenShaderID];

				DescriptorSetCreateInfo descSetCreateInfo = {};
				descSetCreateInfo.DBG_Name = "Fullscreen blit descriptor set";
				descSetCreateInfo.descriptorSetLayout = &descSetLayout;
				descSetCreateInfo.shaderID = fullscreenShaderID;
				descSetCreateInfo.uniformBufferList = &fullscreenBlitMat->uniformBufferList;
				FrameBufferAttachment* sceneFrameBufferAttachment = m_bEnableTAA ? m_OffscreenFB1ColourAttachment0 : m_OffscreenFB0ColourAttachment0;
				descSetCreateInfo.imageDescriptors.SetUniform(U_ALBEDO_SAMPLER, ImageDescriptorInfo{ sceneFrameBufferAttachment->view, m_LinMipLinSampler });
				FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
				m_FinalFullscreenBlitDescriptorSet = m_DescriptorPoolPersistent->CreateDescriptorSet(&descSetCreateInfo);
			}

			{
				GraphicsPipelineCreateInfo pipelineCreateInfo = {};
				pipelineCreateInfo.DBG_Name = "Blit pipeline";
				pipelineCreateInfo.bSetDynamicStates = true;
				pipelineCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
				pipelineCreateInfo.cullMode = VK_CULL_MODE_NONE;
				pipelineCreateInfo.bEnableColourBlending = false;

				pipelineCreateInfo.shaderID = fullscreenBlitMat->shaderID;
				pipelineCreateInfo.vertexAttributes = fullscreenShader->vertexAttributes;
				pipelineCreateInfo.descriptorSetLayoutIndex = fullscreenBlitMat->descriptorSetLayoutIndex;
				pipelineCreateInfo.subpass = fullscreenShader->subpass;
				pipelineCreateInfo.depthWriteEnable = VK_FALSE;
				pipelineCreateInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
				pipelineCreateInfo.renderPass = fullscreenShader->renderPass;
				pipelineCreateInfo.bPersistent = true;
				CreateGraphicsPipeline(&pipelineCreateInfo, m_BlitGraphicsPipelineID);
			}
		}

		void VulkanRenderer::CreateComputeResources()
		{
			for (VulkanParticleSystem* particleSystem : m_ParticleSystems)
			{
				if (particleSystem == nullptr)
				{
					continue;
				}

				CreateParticleSystemResources(particleSystem);
			}
		}

		void VulkanRenderer::CreateParticleSystemResources(VulkanParticleSystem* particleSystem)
		{
			const std::string idStr = std::to_string(particleSystem->ID);

			// Compute pipeline
			{
				VulkanMaterial* particleSimulationMaterial = (VulkanMaterial*)m_Materials.at(particleSystem->system->simMaterialID);
				VulkanShader* particleSimulationShader = (VulkanShader*)m_Shaders[particleSimulationMaterial->shaderID];

				// Particle simulation descriptor set
				VkDescriptorSetLayout descSetLayout = m_DescriptorPool->descriptorSetLayouts[particleSimulationMaterial->shaderID];

				DescriptorSetCreateInfo descSetCreateInfo = {};
				std::string descSetName = "Particle simulation descriptor set " + idStr;
				descSetCreateInfo.DBG_Name = descSetName.c_str();
				descSetCreateInfo.descriptorSetLayout = &descSetLayout;
				descSetCreateInfo.shaderID = particleSimulationMaterial->shaderID;
				descSetCreateInfo.uniformBufferList = &particleSimulationMaterial->uniformBufferList;
				FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
				particleSystem->computeDescriptorSet = m_DescriptorPool->CreateDescriptorSet(&descSetCreateInfo);

				// Particle simulation compute pipeline
				if (m_ParticleSimulationComputePipelineLayout == VK_NULL_HANDLE)
				{
					VkPipelineLayoutCreateInfo pipelineLayoutInfo = vks::pipelineLayoutCreateInfo(1, &descSetLayout);
					VK_CHECK_RESULT(vkCreatePipelineLayout(m_VulkanDevice->m_LogicalDevice, &pipelineLayoutInfo, nullptr, m_ParticleSimulationComputePipelineLayout.replace()));
				}

				VkPipelineShaderStageCreateInfo stage = vks::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT, particleSimulationShader->computeShaderModule);

				VkComputePipelineCreateInfo pipelineCreateInfo = vks::computePipelineCreateInfo(m_ParticleSimulationComputePipelineLayout);
				pipelineCreateInfo.stage = stage;
				VK_CHECK_RESULT(vkCreateComputePipelines(m_VulkanDevice->m_LogicalDevice, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, particleSystem->computePipeline.replace()));

				std::string pipelineName = "Particle simulation compute pipeline " + idStr;
				SetPipelineName(m_VulkanDevice, particleSystem->computePipeline, pipelineName.c_str());
			}

			// Graphics pipeline
			{
				VulkanMaterial* particleRenderingMaterial = (VulkanMaterial*)m_Materials.at(particleSystem->system->renderingMaterialID);
				VulkanShader* particleRenderingShader = (VulkanShader*)m_Shaders[particleRenderingMaterial->shaderID];

				VkDescriptorSetLayout descSetLayout = m_DescriptorPool->descriptorSetLayouts[particleRenderingMaterial->shaderID];

				// Particles descriptor set
				DescriptorSetCreateInfo descSetCreateInfo = {};
				std::string descSetName = "Particle rendering descriptor set " + idStr;
				descSetCreateInfo.DBG_Name = descSetName.c_str();
				descSetCreateInfo.descriptorSetLayout = &descSetLayout;
				descSetCreateInfo.shaderID = particleRenderingMaterial->shaderID;
				descSetCreateInfo.uniformBufferList = &particleRenderingMaterial->uniformBufferList;

				VulkanTexture* texture = (VulkanTexture*)g_ResourceManager->GetLoadedTexture(alphaBGTextureID);
				descSetCreateInfo.imageDescriptors.SetUniform(U_ALBEDO_SAMPLER, ImageDescriptorInfo{ texture->imageView, m_LinMipLinSampler });

				FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
				particleSystem->renderingDescriptorSet = m_DescriptorPool->CreateDescriptorSet(&descSetCreateInfo);

				GraphicsPipelineCreateInfo pipelineCreateInfo = {};
				std::string pipelineName = "Particle rendering graphics pipeline " + idStr;
				pipelineCreateInfo.DBG_Name = pipelineName.c_str();
				pipelineCreateInfo.shaderID = particleRenderingMaterial->shaderID;
				pipelineCreateInfo.vertexAttributes = particleRenderingShader->vertexAttributes;
				pipelineCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
				pipelineCreateInfo.cullMode = VK_CULL_MODE_NONE;
				pipelineCreateInfo.descriptorSetLayoutIndex = particleRenderingMaterial->shaderID;
				pipelineCreateInfo.bSetDynamicStates = true;
				pipelineCreateInfo.bEnableColourBlending = particleRenderingShader->bTranslucent;
				pipelineCreateInfo.subpass = particleRenderingShader->subpass;
				pipelineCreateInfo.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
				pipelineCreateInfo.depthWriteEnable = particleRenderingShader->bDepthWriteEnable ? VK_TRUE : VK_FALSE;
				pipelineCreateInfo.renderPass = *m_ForwardRenderPass;
				CreateGraphicsPipeline(&pipelineCreateInfo, particleSystem->graphicsPipelineID);
			}
		}

		void VulkanRenderer::Update()
		{
			Renderer::Update();

			// NOTE: This doesn't respect TAA jitter!
			BaseCamera* cam = g_CameraManager->CurrentCamera();
			m_SpritePerspPushConstBlock->SetData(cam->GetView(), cam->GetProjection());

			if (m_bSSAOStateChanged)
			{
				m_bSSAOStateChanged = false;

				// Update GBuffer pipeline in case blur or SSAO entirely was toggled
				CreateGraphicsPipeline(m_GBufferQuadRenderID);
				m_DescriptorPoolPersistent->CreateDescriptorSet(GetRenderObject(m_GBufferQuadRenderID)->materialID);

				// Update SSAO pipelines in case kernel size changed
				CreateSSAOPipelines();
			}

			if (m_bTAAStateChanged)
			{
				m_bTAAStateChanged = false;

				CreatePostProcessingResources();
				CreateFullscreenBlitResources();
			}

			m_PhysicsDebugDrawer->UpdateDebugMode();

			{
				VulkanMaterial* wireframeMat = (VulkanMaterial*)m_Materials[m_WireframeMatID];
				real f = glm::clamp(sin(g_SecElapsedSinceProgramStart * 4.0f) * 0.2f + 0.55f, 0.0f, 1.0f);
				wireframeMat->colourMultiplier = glm::vec4(f, f * 0.25f, f * 0.75f, 1.0f);
			}

			UpdateConstantUniformBuffers();

			// TODO: Only update when things have changed
			for (u32 i = 0; i < m_RenderObjects.size(); ++i)
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

			m_LastFrameViewProj = cam->GetViewProjection();
		}

		void VulkanRenderer::Draw()
		{
			glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
			if (m_SwapChainExtent.width == 0u || m_SwapChainExtent.height == 0u ||
				frameBufferSize.x == 0 || frameBufferSize.y == 0)
			{
				return;
			}

			m_SpriteDynamicUBOOffset = 0;

			DrawCallInfo drawCallInfo = {};

			if (!m_PhysicsDebuggingSettings.bDisableAll)
			{
				PhysicsDebugRender();
			}

			if (m_bRebatchRenderObjects)
			{
				VK_CHECK_RESULT(vkQueueWaitIdle(m_GraphicsQueue));

				m_bRebatchRenderObjects = false;
				BatchRenderObjects();

				// ?
				for (u32 i = 0; i < m_RenderObjects.size(); ++i)
				{
					UpdateDynamicUniformBuffer(i);
				}
			}

			// TODO: Don't build command buffers when m_bRebatchRenderObjects is false (but dynamic UI elements still need to be rebuilt!)
			BuildCommandBuffers(drawCallInfo);

			if (m_bSwapChainNeedsRebuilding)
			{
				RecreateSwapChain();
			}
			else
			{
				DrawFrame();
			}

			++m_FramesRendered;
		}

		void VulkanRenderer::DrawImGuiRendererInfo()
		{
			if (ImGui::TreeNode("Enabled instance extensions"))
			{
				for (const char* extension : m_EnabledInstanceExtensions)
				{
					ImGui::BulletText("%s", extension);
				}
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Supported instance extensions"))
			{
				for (const VkExtensionProperties& extension : m_SupportedInstanceExtensions)
				{
					ImGui::BulletText("%s", extension.extensionName);
				}
				ImGui::TreePop();
			}

			m_VulkanDevice->DrawImGuiRendererInfo();
		}

		void VulkanRenderer::DrawImGuiWindows()
		{
			//ImGui::SliderFloat("TAA KL", &(m_TAA_ks[0]), 0.0f, 100.0f);
			//ImGui::SliderFloat("TAA KH", &(m_TAA_ks[1]), 0.0f, 100.0f);

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
							snprintf(buf, 256, "%s: %.2fms", iter->first.c_str(), m_TimestampHistograms[arrayIndex][0]);
							ImGui::PlotLines(buf, m_TimestampHistograms[arrayIndex].data(), NUM_GPU_TIMINGS, m_TimestampHistogramIndex, nullptr, 0.0f, 8.0f);
						}
					}
				}
				ImGui::End();
			}

			if (bUniformBufferWindowShowing)
			{
				if (ImGui::Begin("Uniform Buffers", &bUniformBufferWindowShowing))
				{
					ShaderBatch* shaderBatches[] = { &m_DeferredObjectBatches, &m_ForwardObjectBatches };
					const char* shaderBatchNames[] = { "Deferred", "Forward" };
					for (u32 i = 0; i < ARRAY_LENGTH(shaderBatches); ++i)
					{
						ShaderBatch* shaderBatch = shaderBatches[i];

						ImGui::Text("%s", shaderBatchNames[i]);

						for (u32 j = 0; j < shaderBatch->batches.size(); ++j)
						{
							ShaderBatchPair& shaderBatchPair = shaderBatch->batches[j];
							VulkanShader* shader = (VulkanShader*)m_Shaders[shaderBatchPair.shaderID];

							ImGui::PushID(shaderBatchPair.shaderID);

							for (u32 k = 0; k < shaderBatchPair.batch.batches.size(); ++k)
							{
								MaterialBatchPair& matBatchPair = shaderBatchPair.batch.batches[k];
								VulkanMaterial* material = (VulkanMaterial*)m_Materials.at(matBatchPair.materialID);

								if (material->uniformBufferList.Has(UniformBufferType::DYNAMIC) &&
									material->uniformBufferList.Get(UniformBufferType::DYNAMIC)->fullDynamicBufferSize > 0)
								{
									if (ImGui::BeginChild(shader->name.c_str(), ImVec2(0, 200), true))
									{
										u32 dynamicObjectCount = 0;

										for (u32 l = 0; l < shaderBatchPair.batch.batches.size(); ++l)
										{
											for (RenderID renderID : shaderBatchPair.batch.batches[l].batch.objects)
											{
												VulkanRenderObject* renderObject = GetRenderObject(renderID);
												if (renderObject != nullptr)
												{
													++dynamicObjectCount;
												}
											}
										}

										UniformBuffer* dynamicBuffer = material->uniformBufferList.Get(UniformBufferType::DYNAMIC);
										u32 bufferSlotsTotal = (dynamicBuffer->fullDynamicBufferSize / dynamicBuffer->data.unitSize);
										u32 bufferSlotsFree = bufferSlotsTotal - dynamicObjectCount;

										char histNodeID[256];
										memset(histNodeID, 0, 256);
										snprintf(histNodeID, 256, "%s (%u/%u)",
											shader->name.c_str(),
											bufferSlotsTotal - bufferSlotsFree,
											bufferSlotsTotal);
										real progress = 1.0f - (bufferSlotsFree / (real)bufferSlotsTotal);
										ImGui::ProgressBar(progress, ImVec2(0, 0), histNodeID);

										ImGui::SameLine();

										u32 byteCount = dynamicBuffer->fullDynamicBufferSize;
										char byteCoutBuff[64];
										ByteCountToString(byteCoutBuff, 64, byteCount);

										u32 percentUsed = (u32)(progress * 100.0f);
										char percentUsedBuff[64];
										memset(percentUsedBuff, 0, sizeof(percentUsedBuff));
										sprintf(percentUsedBuff, "%u%%", percentUsed);
										ImGui::Text("%s %s", byteCoutBuff, percentUsedBuff);
									}
									ImGui::EndChild();
								}

							}

							ImGui::PopID(); // shaderBatchPair.shaderID
						}
					}

					ImGui::Text("Particle buffers");

					if (m_ParticleSystems.empty())
					{
						ImGui::Text("(None in scene)");
					}
					else
					{
						char nodeID0[256];
						memset(nodeID0, 0, 256);
						if (ImGui::BeginChild("##particles", ImVec2(0, 200), true))
						{
							for (VulkanParticleSystem* system : m_ParticleSystems)
							{
								VulkanMaterial* simMat = (VulkanMaterial*)m_Materials[system->system->simMaterialID];
								if (simMat->uniformBufferList.Has(UniformBufferType::PARTICLE_DATA))
								{
									UniformBuffer* particleBuffer = simMat->uniformBufferList.Get(UniformBufferType::PARTICLE_DATA);
									// TODO: ? u32 bufferSlotsTotal = particleBuffer->data.size;
									u32 bufferSlotsTotal = (particleBuffer->fullDynamicBufferSize / particleBuffer->data.unitSize);

									char histNodeID[256];
									memset(histNodeID, 0, 256);
									snprintf(histNodeID, 256, "%s %uB##particles_size",
										simMat->name.c_str(),
										bufferSlotsTotal);
									real progress = 1.0f;
									ImGui::ProgressBar(progress, ImVec2(0, 0), histNodeID);
								}
							}
						}
						ImGui::EndChild();
					}
				}
				ImGui::End();
			}

#if COMPILE_SHADER_COMPILER
			VulkanShaderCompiler::DrawImGuiShaderErrorsWindow(&m_bShaderErrorWindowShowing);
#endif
		}

		void VulkanRenderer::UpdateDynamicVertexData(RenderID renderID, VertexBufferData const* vertexBufferData, const std::vector<u32>& indexData)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);

			if (!renderObject->bIndexed)
			{
				PrintWarn("Dynamic render object not set to indexed! This workflow is not supported, was the index buffer empty being this object was created?\n");
			}

			VulkanMaterial* material = (VulkanMaterial*)m_Materials.at(renderObject->materialID);
			VertexIndexBufferPair* vertexIndexBufferPair;
			if (material->shaderID == m_UIShaderID)
			{
				vertexIndexBufferPair = m_DynamicUIVertexIndexBufferPair;
			}
			else
			{
				vertexIndexBufferPair = m_DynamicVertexIndexBufferPairs[material->dynamicVertexIndexBufferIndex].second;
			}
			VulkanBuffer* vertexBuffer = vertexIndexBufferPair->vertexBuffer;
			VulkanBuffer* indexBuffer = vertexIndexBufferPair->indexBuffer;

			const u32 newIndexDataSize = (u32)indexData.size() * sizeof(u32);

			// Initial allocation
			if (renderObject->dynamicVertexBufferOffset == u64_max)
			{
				renderObject->dynamicVertexBufferOffset = vertexBuffer->Alloc((VkDeviceSize)vertexBufferData->VertexBufferSize, true);
				renderObject->dynamicIndexBufferOffset = indexBuffer->Alloc((VkDeviceSize)newIndexDataSize, true);
			}

			VkDeviceSize vertSubBufferSize = vertexBuffer->GetAllocationSize(renderObject->dynamicVertexBufferOffset);
			VkDeviceSize indexSubBufferSize = indexBuffer->GetAllocationSize(renderObject->dynamicIndexBufferOffset);
			assert(vertSubBufferSize != (VkDeviceSize)-1);
			assert(indexSubBufferSize != (VkDeviceSize)-1);

			// Resize when growing
			if (vertexBufferData->VertexBufferSize > vertSubBufferSize)
			{
				renderObject->dynamicVertexBufferOffset = vertexBuffer->Realloc(renderObject->dynamicVertexBufferOffset, vertexBufferData->VertexBufferSize, true);
				vertSubBufferSize = vertexBuffer->GetAllocationSize(renderObject->dynamicVertexBufferOffset);
				assert(vertSubBufferSize != ((VkDeviceSize)-1));
			}

			if (newIndexDataSize > (u32)indexSubBufferSize)
			{
				renderObject->dynamicIndexBufferOffset = indexBuffer->Realloc(renderObject->dynamicIndexBufferOffset, newIndexDataSize, true);
				indexSubBufferSize = indexBuffer->GetAllocationSize(renderObject->dynamicIndexBufferOffset);
				assert(indexSubBufferSize != ((VkDeviceSize)-1));
			}

			// Shrink
			if (renderObject->bAllowDynamicBufferShrinking && vertSubBufferSize > 0)
			{
				real vertExcess = 1.0f - (real)vertexBufferData->UsedVertexBufferSize / vertSubBufferSize;
				real indexExcess = 1.0f - (real)newIndexDataSize / indexSubBufferSize;
				if (vertExcess > 0.5f || indexExcess > 0.5f)
				{
					renderObject->dynamicVertexBufferOffset = vertexBuffer->Realloc(renderObject->dynamicVertexBufferOffset, vertexBufferData->UsedVertexBufferSize, true);
					vertSubBufferSize = vertexBuffer->GetAllocationSize(renderObject->dynamicVertexBufferOffset);
					assert(vertSubBufferSize != ((VkDeviceSize)-1));

					renderObject->dynamicIndexBufferOffset = indexBuffer->Realloc(renderObject->dynamicIndexBufferOffset, indexSubBufferSize, true);
					indexSubBufferSize = indexBuffer->GetAllocationSize(renderObject->dynamicIndexBufferOffset);
					assert(indexSubBufferSize != ((VkDeviceSize)-1));

					ShrinkDynamicVertexData(renderID, 0.5f);

					// TODO: Jikes, fix me?
					vkDeviceWaitIdle(m_VulkanDevice->m_LogicalDevice);
				}
			}

			VkDeviceSize vertOffsetBytes = renderObject->dynamicVertexBufferOffset;
			VkDeviceSize indexOffsetBytes = renderObject->dynamicIndexBufferOffset;

			assert((vertOffsetBytes + vertSubBufferSize) <= vertexBuffer->m_Size);
			assert((indexOffsetBytes + indexSubBufferSize) <= indexBuffer->m_Size);

			VK_CHECK_RESULT(vertexBuffer->Map(vertOffsetBytes, vertSubBufferSize));
			VK_CHECK_RESULT(indexBuffer->Map(indexOffsetBytes, indexSubBufferSize));
			memcpy(vertexBuffer->m_Mapped, vertexBufferData->vertexData, vertexBufferData->UsedVertexBufferSize);
			memcpy(indexBuffer->m_Mapped, indexData.data(), newIndexDataSize);
			vertexBuffer->Unmap();
			indexBuffer->Unmap();


			renderObject->vertexOffset = (u32)(vertOffsetBytes / vertexBufferData->VertexStride);
			renderObject->indexOffset = (u32)(indexOffsetBytes / sizeof(u32));

			m_DirtyFlagBits |= RenderBatchDirtyFlag::DYNAMIC_DATA; // TODO: Is this needed?
		}

		void VulkanRenderer::FreeDynamicVertexData(RenderID renderID)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);

			if (renderObject->dynamicVertexBufferOffset != InvalidBufferID ||
				renderObject->dynamicIndexBufferOffset != InvalidBufferID)
			{
				VulkanMaterial* material = (VulkanMaterial*)m_Materials.at(renderObject->materialID);
				VertexIndexBufferPair* vertexIndexBufferPair = m_DynamicVertexIndexBufferPairs[material->dynamicVertexIndexBufferIndex].second;
				VulkanBuffer* vertexBuffer = vertexIndexBufferPair->vertexBuffer;
				VulkanBuffer* indexBuffer = vertexIndexBufferPair->indexBuffer;

				if (renderObject->dynamicVertexBufferOffset != InvalidBufferID)
				{
					vertexBuffer->Free(renderObject->dynamicVertexBufferOffset);
					renderObject->dynamicVertexBufferOffset = InvalidBufferID;
				}

				if (renderObject->dynamicIndexBufferOffset != InvalidBufferID)
				{
					indexBuffer->Free(renderObject->dynamicIndexBufferOffset);
					renderObject->dynamicIndexBufferOffset = InvalidBufferID;
				}
			}
		}

		void VulkanRenderer::ShrinkDynamicVertexData(RenderID renderID, real minUnused /* = 0.0f */)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);

			VulkanMaterial* material = (VulkanMaterial*)m_Materials.at(renderObject->materialID);
			VertexIndexBufferPair* vertexIndexBufferPair = m_DynamicVertexIndexBufferPairs[material->dynamicVertexIndexBufferIndex].second;
			VulkanBuffer* vertexBuffer = vertexIndexBufferPair->vertexBuffer;
			VulkanBuffer* indexBuffer = vertexIndexBufferPair->indexBuffer;

			if (renderObject->dynamicVertexBufferOffset != InvalidBufferID)
			{
				vertexBuffer->Shrink(minUnused);
			}

			if (renderObject->dynamicIndexBufferOffset != InvalidBufferID)
			{
				indexBuffer->Shrink(minUnused);
			}
		}

		u32 VulkanRenderer::GetDynamicVertexBufferSize(RenderID renderID)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);

			VulkanMaterial* material = (VulkanMaterial*)m_Materials.at(renderObject->materialID);
			VertexIndexBufferPair* vertexIndexBufferPair = m_DynamicVertexIndexBufferPairs[material->dynamicVertexIndexBufferIndex].second;
			VulkanBuffer* vertexBuffer = vertexIndexBufferPair->vertexBuffer;

			return (u32)vertexBuffer->m_Size;
		}

		u32 VulkanRenderer::GetDynamicVertexBufferUsedSize(RenderID renderID)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);

			VulkanMaterial* material = (VulkanMaterial*)m_Materials.at(renderObject->materialID);
			VertexIndexBufferPair* vertexIndexBufferPair = m_DynamicVertexIndexBufferPairs[material->dynamicVertexIndexBufferIndex].second;
			VulkanBuffer* vertexBuffer = vertexIndexBufferPair->vertexBuffer;

			return (u32)vertexBuffer->GetAllocationSize(renderObject->dynamicVertexBufferOffset);
		}

		bool VulkanRenderer::DrawImGuiShadersDropdown(i32* selectedShaderIndex, Shader** outSelectedShader /* = nullptr */)
		{
			bool bValueChanged = false;

			ImGui::PushItemWidth(240.0f);
			if (ImGui::BeginCombo("Shaders", m_Shaders[*selectedShaderIndex]->name.c_str()))
			{
				for (i32 i = 0; i < (i32)m_Shaders.size(); ++i)
				{
					ImGui::PushID(i);

					bool bSelected = (i == *selectedShaderIndex);
					if (ImGui::Selectable(m_Shaders[i]->name.c_str(), &bSelected))
					{
						*selectedShaderIndex = i;
						bValueChanged = true;
					}

					ImGui::PopID();
				}

				ImGui::EndCombo();
			}

			if (outSelectedShader != nullptr)
			{
				*outSelectedShader = m_Shaders[*selectedShaderIndex];
			}

			return bValueChanged;
		}

		bool VulkanRenderer::DrawImGuiShadersList(i32* selectedShaderIndex, bool bShowFilter, Shader** outSelectedShader /* = nullptr */)
		{
			bool bValueChanged = false;

			static ImGuiTextFilter shaderFilter;
			if (bShowFilter)
			{
				shaderFilter.Draw("##shader-filter");

				ImGui::SameLine();
				if (ImGui::Button("x"))
				{
					shaderFilter.Clear();
				}
			}

			if (ImGui::BeginChild("Shader list", ImVec2(0, 120), true))
			{
				// TODO: Add option to generate & view shader dissasembly here

				for (i32 i = 0; i < (i32)m_Shaders.size(); ++i)
				{
					VulkanShader* shader = (VulkanShader*)m_Shaders[i];
					if (!bShowFilter || shaderFilter.PassFilter(shader->name.c_str()))
					{
						bool bSelected = (i == *selectedShaderIndex);
						if (ImGui::Selectable(shader->name.c_str(), &bSelected))
						{
							if (bSelected)
							{
								*selectedShaderIndex = i;
								bValueChanged = true;
							}
						}
					}
				}
			}
			ImGui::EndChild(); // Shader list

			if (outSelectedShader != nullptr)
			{
				*outSelectedShader = m_Shaders[*selectedShaderIndex];
			}

			return bValueChanged;
		}

		bool VulkanRenderer::DrawImGuiTextureSelector(const char* label, const std::vector<std::string>& textureNames, i32* selectedIndex)
		{
			bool bValueChanged = false;

			std::string currentTexName = (*selectedIndex == 0 ? "NONE" : textureNames[*selectedIndex]);
			if (ImGui::BeginCombo(label, currentTexName.c_str()))
			{
				for (i32 i = 0; i < (i32)textureNames.size(); i++)
				{
					bool bTextureSelected = (*selectedIndex == i);

					if (i == 0)
					{
						if (ImGui::Selectable("NONE", bTextureSelected))
						{
							*selectedIndex = 0;
							bValueChanged = true;
						}
					}
					else
					{
						std::string texName = textureNames[i];

						if (ImGui::Selectable(texName.c_str(), bTextureSelected))
						{
							*selectedIndex = i;
							bValueChanged = true;
						}

						if (ImGui::IsItemHovered())
						{
							Texture* texture = g_ResourceManager->FindLoadedTextureWithName(texName);
							if (texture != nullptr)
							{
								DrawImGuiTexturePreviewTooltip(texture);
							}
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

		void VulkanRenderer::DrawImGuiShaderErrors()
		{
#if COMPILE_SHADER_COMPILER
			VulkanShaderCompiler::DrawImGuiShaderErrors();
#endif
		}

		void VulkanRenderer::DrawImGuiTexture(TextureID textureID, real texSize, ImVec2 uv0 /* = ImVec2(0, 0) */, ImVec2 uv1 /* = ImVec2(1, 1) */)
		{
			DrawImGuiTexture(g_ResourceManager->GetLoadedTexture(textureID), texSize, uv0, uv1);
		}

		void VulkanRenderer::DrawImGuiTexture(Texture* texture, real texSize, ImVec2 uv0 /* = ImVec2(0, 0) */, ImVec2 uv1 /* = ImVec2(1, 1) */)
		{
			real textureAspectRatio = (real)texture->width / (real)texture->height;
			VkImage textureImage = ((VulkanTexture*)texture)->image;
			ImGui::Image((void*)&textureImage, ImVec2(texSize * textureAspectRatio, texSize), uv0, uv1);
		}

		void VulkanRenderer::ClearShaderHash(const std::string& shaderName)
		{
#if COMPILE_SHADER_COMPILER
			VulkanShaderCompiler::ClearShaderHash(shaderName);
#else
			FLEX_UNUSED(shaderName);
#endif
		}

		void VulkanRenderer::RecompileShaders(bool bForce)
		{
#if COMPILE_SHADER_COMPILER
			if (m_ShaderCompiler == nullptr)
			{
				m_bShaderErrorWindowShowing = true;
				m_ShaderCompiler = new VulkanShaderCompiler(bForce);

				if (m_ShaderCompiler->bSuccess)
				{
					AddEditorString("Shader recompile completed successfully");
					RecreateEverything();
				}
				else
				{
					PrintError("Shader recompile failed\n");
					AddEditorString("Shader recompile failed");
				}

				m_bSwapChainNeedsRebuilding = true; // This is needed to recreate some resources for SSAO, etc.

				delete m_ShaderCompiler;
				m_ShaderCompiler = nullptr;
			}
#else
			FLEX_UNUSED(bForce);
#endif
		}

		void VulkanRenderer::SetTopologyMode(RenderID renderID, TopologyMode topology)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (renderObject == nullptr)
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

		void VulkanRenderer::SetClearColour(real r, real g, real b)
		{
			m_ClearColour = VkClearColorValue{ r, g, b, 1.0f };
		}

		void VulkanRenderer::OnWindowSizeChanged(i32 width, i32 height)
		{
			FLEX_UNUSED(width);
			FLEX_UNUSED(height);

			if (m_bPostInitialized && width != 0 && height != 0)
			{
				m_bSwapChainNeedsRebuilding = true;
			}

			real aspectRatio = (real)width / (real)height;
			real r = aspectRatio;
			real t = 1.0f;
			m_SpriteOrthoPushConstBlock->SetData(MAT4_IDENTITY, glm::ortho(-r, r, -t, t));
		}

		void VulkanRenderer::OnPreSceneChange()
		{
			// TODO: Consolidate all recreation functions
			GenerateGBuffer();

			// Clear non-persistent descriptor pool
			m_DescriptorPool->Reset();

			for (u32 i = 0; i < m_Shaders.size(); ++i)
			{
				m_DescriptorPool->CreateDescriptorSetLayout(i);
			}

			// TODO: CreateDescriptorSets here?

			for (auto& pair : m_DynamicVertexIndexBufferPairs)
			{
				pair.second->Clear();
			}
		}

		void VulkanRenderer::OnPostSceneChange()
		{
			Renderer::OnPostSceneChange();

			if (m_bPostInitialized)
			{
				CreateAllStaticVertexBuffers();
				CreateStaticIndexBuffer();
				CreateAllDynamicVertexAndIndexBuffers();

				CreateShadowVertexBuffer();
				CreateShadowIndexBuffer();

				GenerateIrradianceMaps();

				InitializeAllParticleSystemBuffers();
			}
		}

		bool VulkanRenderer::GetRenderObjectCreateInfo(RenderID renderID, RenderObjectCreateInfo& outInfo)
		{
			outInfo = {};

			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (renderObject == nullptr)
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
			if (m_bInitialized)
			{
				m_bSwapChainNeedsRebuilding = true;
			}
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
			return (u32)m_RenderObjects.size();
		}

		void VulkanRenderer::DescribeShaderVariable(RenderID renderID, const std::string& variableName, i32 size, DataType dataType, bool normalized, i32 stride, void* pointer)
		{
			FLEX_UNUSED(renderID);
			FLEX_UNUSED(variableName);
			FLEX_UNUSED(size);
			FLEX_UNUSED(dataType);
			FLEX_UNUSED(normalized);
			FLEX_UNUSED(stride);
			FLEX_UNUSED(pointer);
		}

		void VulkanRenderer::SetSkyboxMesh(Mesh* skyboxMesh)
		{
			m_SkyBoxMesh = skyboxMesh;

			if (skyboxMesh == nullptr)
			{
				return;
			}

			MaterialID skyboxMaterialID = m_SkyBoxMesh->GetSubMesh(0)->GetMaterialID();
			if (skyboxMaterialID == InvalidMaterialID)
			{
				PrintError("Skybox doesn't have a valid Material! Irradiance textures can't be generated\n");
				return;
			}
			VulkanMaterial* skyboxMaterial = (VulkanMaterial*)m_Materials.at(skyboxMaterialID);
			m_SkyboxShaderID = skyboxMaterial->shaderID;

			for (u32 i = 0; i < m_RenderObjects.size(); ++i)
			{
				VulkanRenderObject* renderObject = GetRenderObject(i);
				if (renderObject != nullptr)
				{
					auto matIter = m_Materials.find(renderObject->materialID);
					if (matIter != m_Materials.end())
					{
						if (m_Shaders[matIter->second->shaderID]->textureUniforms.HasUniform(U_PREFILTER_MAP))
						{
							VulkanMaterial* renderObjectMat = (VulkanMaterial*)matIter->second;
							renderObjectMat->textures.SetUniform(U_IRRADIANCE_SAMPLER, skyboxMaterial->textures[U_IRRADIANCE_SAMPLER], "Irradiance");
							renderObjectMat->textures.SetUniform(U_PREFILTER_MAP, skyboxMaterial->textures[U_PREFILTER_MAP]);
						}
					}
					else
					{
						PrintError("Skybox's Material doesn't exist!\n");
					}
				}
			}

			m_bRebatchRenderObjects = true;
		}

		void VulkanRenderer::SetRenderObjectMaterialID(RenderID renderID, MaterialID materialID)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (renderObject != nullptr)
			{
				MaterialID prevMatID = renderObject->materialID;
				if (materialID != prevMatID)
				{
					renderObject->materialID = materialID;

					// Regenerate vertex data with new stride
					Mesh* mesh = renderObject->gameObject->GetMesh();
					if (mesh != nullptr)
					{
						MeshComponent* submesh = mesh->GetSubMeshWithRenderID(renderID);
						if (submesh != nullptr)
						{
							u32 prevStride = CalculateVertexStride(m_Shaders[m_Materials.at(prevMatID)->shaderID]->vertexAttributes);
							u32 newStride = CalculateVertexStride(m_Shaders[m_Materials.at(materialID)->shaderID]->vertexAttributes);
							if (newStride != prevStride)
							{
								submesh->SetRequiredAttributesFromMaterialID(materialID);
							}
							submesh->GetOwner()->Reload();
						}
						else
						{
							PrintWarn("Attempted to set Material ID on object with no submesh with renderID %u\n", renderID);
						}
					}
					else
					{
						PrintWarn("Attempted to set Material ID on object with no mesh\n");
					}
				}
			}
			else
			{
				PrintError("SetRenderObjectMaterialID couldn't find render object with ID %u\n", renderID);
			}

			m_bRebatchRenderObjects = true;
		}

		bool VulkanRenderer::DestroyRenderObject(RenderID renderID)
		{
			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject != nullptr && renderObject->renderID == renderID)
				{
					DestroyRenderObject(renderID, renderObject);
					return true;
				}
			}
			return false;
		}

		void VulkanRenderer::DestroyRenderObject(RenderID renderID, VulkanRenderObject* renderObject)
		{
			if (renderObject != nullptr)
			{
				FreeDynamicVertexData(renderID);

				GraphicsPipelineConfiguration* graphicsPipelineConfig = GetGraphicsPipeline(renderObject->graphicsPipelineID);
				--graphicsPipelineConfig->usageCount;

				delete renderObject;
				renderObject = nullptr;
			}
			m_RenderObjects[renderID] = nullptr;
			m_bRebatchRenderObjects = true;
		}

		void VulkanRenderer::SetGlobalUniform(const Uniform& uniform, void* data, u32 dataSize)
		{
			auto& pair = m_GlobalUserUniforms[uniform.id];
			pair.first = data;
			pair.second = dataSize;
		}

		void VulkanRenderer::NewFrame()
		{
			Renderer::NewFrame();

			if (m_PhysicsDebugDrawer != nullptr)
			{
				m_PhysicsDebugDrawer->ClearLines();
			}

			if (g_Window->GetFrameBufferSize().x == 0)
			{
				return;
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
			const glm::vec4& colour,
			AnchorPoint anchor,
			const glm::vec2& pos,
			real spacing,
			real scale /* = 1.0f */)
		{
			assert(m_CurrentFont != nullptr);

			TextCache newCache(str, anchor, pos, colour, spacing, scale);
			m_CurrentFont->AddTextCache(newCache);
		}

		void VulkanRenderer::DrawStringWS(const std::string& str,
			const glm::vec4& colour,
			const glm::vec3& pos,
			const glm::quat& rot,
			real spacing,
			real scale /* = 1.0f */)
		{
			assert(m_CurrentFont != nullptr);

			TextCache newCache(str, pos, rot, colour, spacing, scale);
			m_CurrentFont->AddTextCache(newCache);
		}

		void VulkanRenderer::ReloadObjectsWithMesh(const std::string& meshFilePath)
		{
			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject != nullptr)
				{
					Mesh* mesh = renderObject->gameObject->GetMesh();
					if (mesh && mesh->GetRelativeFilePath().compare(meshFilePath) == 0)
					{
						mesh->Reload();
					}
				}
			}
		}

		void VulkanRenderer::RecaptureReflectionProbe()
		{
			// UNIMPLEMENTED
		}

		void VulkanRenderer::RenderObjectMaterialChanged(MaterialID materialID)
		{
			for (u32 i = 0; i < (u32)m_RenderObjects.size(); ++i)
			{
				if (m_RenderObjects[i] != nullptr && m_RenderObjects[i]->materialID == materialID)
				{
					CreateGraphicsPipeline(i);
					m_DescriptorPool->CreateDescriptorSet(m_RenderObjects[i]->materialID);
				}
			}
		}

		void VulkanRenderer::RenderObjectStateChanged()
		{
			// TODO: Ignore object visibility changes
			m_bRebatchRenderObjects = true;
		}

		void VulkanRenderer::RecreateRenderObjectsWithMesh(const std::string& relativeMeshFilePath)
		{
			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject != nullptr)
				{
					GameObject* owningGameObject = renderObject->gameObject;
					if (owningGameObject != nullptr)
					{
						Mesh* mesh = owningGameObject->GetMesh();
						if (mesh && mesh->GetRelativeFilePath().compare(relativeMeshFilePath) == 0)
						{
							mesh->Destroy();
							mesh->SetOwner(owningGameObject);
							mesh->LoadFromFile(relativeMeshFilePath, mesh->GetMaterialIDs(), false);
						}
					}
				}
			}
		}

		ParticleSystemID VulkanRenderer::AddParticleSystem(const std::string& name, ParticleSystem* system, i32 particleCount)
		{
			if ((u32)particleCount > MAX_PARTICLE_COUNT)
			{
				PrintWarn("Attempted to create particle system with more particles than allowed (%d > %d) Only %d will be created\n", particleCount, MAX_PARTICLE_COUNT, MAX_PARTICLE_COUNT);
			}

			VulkanParticleSystem* particleSystem = new VulkanParticleSystem(m_VulkanDevice);
			particleSystem->ID = GetNextAvailableParticleSystemID();
			particleSystem->system = system;

			system->simMaterialID = CreateParticleSystemSimulationMaterial(name + " sim Material");
			system->renderingMaterialID = CreateParticleSystemRenderingMaterial(name + " rendering Material");

			InsertNewParticleSystem(particleSystem);

			if (m_bPostInitialized)
			{
				CreateParticleSystemResources(particleSystem);
				InitializeParticleSystemBuffer(particleSystem);
			}

			return particleSystem->ID;
		}

		bool VulkanRenderer::RemoveParticleSystem(ParticleSystemID particleSystemID)
		{
			if (particleSystemID < m_ParticleSystems.size())
			{
				delete m_ParticleSystems[particleSystemID];
				m_ParticleSystems[particleSystemID] = nullptr;
				return true;
			}

			return false;
		}

		bool VulkanRenderer::InitializeFreeType()
		{
			if (FT_Init_FreeType(&m_FTLibrary) != FT_Err_Ok)
			{
				PrintError("Failed to initialize FreeType\n");
				return false;
			}

			{
				i32 maj, min, pat;
				FT_Library_Version(m_FTLibrary, &maj, &min, &pat);

				Print("Free type v%d.%d.%d\n", maj, min, pat);
			}

			return true;
		}

		void VulkanRenderer::DestroyFreeType()
		{
			FT_Done_FreeType(m_FTLibrary);
		}

		void VulkanRenderer::GenerateCubemapFromHDR(VulkanRenderObject* renderObject, const std::string& environmentMapPath)
		{
			if (m_SkyBoxMesh == nullptr)
			{
				PrintError("Attempted to generate cubemap before skybox object was created!\n");
				return;
			}

			VulkanRenderObject* skyboxRenderObject = GetRenderObject(m_SkyBoxMesh->GetSubMesh(0)->renderID);
			VulkanMaterial* skyboxMat = (VulkanMaterial*)m_Materials.at(skyboxRenderObject->materialID);
			VulkanMaterial* renderObjectMat = (VulkanMaterial*)m_Materials.at(renderObject->materialID);

			MaterialCreateInfo matCreateInfo = {};
			matCreateInfo.name = "equirectangular to Cube";
			matCreateInfo.shaderName = "equirectangular_to_cube";
			matCreateInfo.enableHDREquirectangularSampler = true;
			matCreateInfo.generateHDREquirectangularSampler = true;
			matCreateInfo.hdrEquirectangularTexturePath = environmentMapPath;
			matCreateInfo.persistent = true;
			matCreateInfo.visibleInEditor = false;
			matCreateInfo.bSerializable = false;

			bool bRandomizeSkybox = true;
			if (bRandomizeSkybox && !m_AvailableHDRIs.empty())
			{
				// TODO: Select from current scene!
				matCreateInfo.hdrEquirectangularTexturePath = PickRandomSkyboxTexture();
			}

			MaterialID equirectangularToCubeMatID = InitializeMaterial(&matCreateInfo);
			VulkanMaterial* equirectangularToCubeMat = (VulkanMaterial*)m_Materials.at(equirectangularToCubeMatID);

			const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
			const u32 dim = (u32)renderObjectMat->cubemapSamplerSize.x;
			assert(dim <= MAX_TEXTURE_DIM);
			const u32 mipLevels = static_cast<u32>(floor(log2(dim))) + 1;

			VulkanRenderPass renderPass(m_VulkanDevice);
			renderPass.RegisterForColourOnly("Equirectangular to Cubemap render pass", InvalidFrameBufferAttachmentID, {});
			renderPass.bCreateFrameBuffer = false;
			renderPass.m_ColourAttachmentFormat = format;
			renderPass.ManuallySpecifyLayouts({ VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }, { VK_IMAGE_LAYOUT_UNDEFINED });
			renderPass.Create();

			// TODO: Use FrameBuffer class here
			// Offscreen framebuffer
			struct {
				VkImage image;
				VkImageView view;
				VkDeviceMemory memory;
				VkFramebuffer framebuffer;
			} offscreen;

			// Colour attachment
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

			VkImageViewCreateInfo colourImageView = vks::imageViewCreateInfo();
			colourImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
			colourImageView.format = format;
			colourImageView.flags = 0;
			colourImageView.subresourceRange = {};
			colourImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			colourImageView.subresourceRange.baseMipLevel = 0;
			colourImageView.subresourceRange.levelCount = 1;
			colourImageView.subresourceRange.baseArrayLayer = 0;
			colourImageView.subresourceRange.layerCount = 1;
			colourImageView.image = offscreen.image;
			VK_CHECK_RESULT(vkCreateImageView(m_VulkanDevice->m_LogicalDevice, &colourImageView, nullptr, &offscreen.view));

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


			ShaderID equirectangularToCubeShaderID = equirectangularToCubeMat->shaderID;
			VulkanShader* equirectangularToCubeShader = (VulkanShader*)m_Shaders[equirectangularToCubeShaderID];

			DescriptorSetCreateInfo equirectangularToCubeDescriptorCreateInfo = {};
			equirectangularToCubeDescriptorCreateInfo.DBG_Name = "Equirectangular to cube descriptor set";
			equirectangularToCubeDescriptorCreateInfo.descriptorSetLayout = &m_DescriptorPoolPersistent->descriptorSetLayouts[equirectangularToCubeShaderID];
			equirectangularToCubeDescriptorCreateInfo.shaderID = equirectangularToCubeShaderID;
			equirectangularToCubeDescriptorCreateInfo.uniformBufferList = &equirectangularToCubeMat->uniformBufferList;
			VulkanTexture* equirectTexture = (VulkanTexture*)equirectangularToCubeMat->textures[U_HDR_EQUIRECTANGULAR_SAMPLER];
			equirectangularToCubeDescriptorCreateInfo.imageDescriptors.SetUniform(U_HDR_EQUIRECTANGULAR_SAMPLER, ImageDescriptorInfo{ equirectTexture->imageView, m_LinMipLinSampler });
			FillOutBufferDescriptorInfos(&equirectangularToCubeDescriptorCreateInfo.bufferDescriptors, equirectangularToCubeDescriptorCreateInfo.uniformBufferList, equirectangularToCubeDescriptorCreateInfo.shaderID);
			VkDescriptorSet descriptorSet = m_DescriptorPoolPersistent->CreateDescriptorSet(&equirectangularToCubeDescriptorCreateInfo);

			std::array<VkPushConstantRange, 1> pushConstantRanges = {};
			pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			pushConstantRanges[0].offset = 0;
			pushConstantRanges[0].size = equirectangularToCubeShader->pushConstantBlockSize;

			GraphicsPipelineID pipelineID = InvalidGraphicsPipelineID;
			GraphicsPipelineCreateInfo pipelineCreateInfo = {};
			pipelineCreateInfo.DBG_Name = "Equirectangular to cube pipeline";
			pipelineCreateInfo.renderPass = renderPass;
			pipelineCreateInfo.shaderID = equirectangularToCubeShaderID;
			pipelineCreateInfo.vertexAttributes = equirectangularToCubeShader->vertexAttributes;
			pipelineCreateInfo.topology = skyboxRenderObject->topology;
			pipelineCreateInfo.cullMode = skyboxRenderObject->cullMode;
			pipelineCreateInfo.descriptorSetLayoutIndex = equirectangularToCubeShaderID;
			pipelineCreateInfo.bSetDynamicStates = true;
			pipelineCreateInfo.bEnableAdditiveColourBlending = false;
			pipelineCreateInfo.subpass = 0;
			pipelineCreateInfo.depthTestEnable = VK_FALSE;
			pipelineCreateInfo.depthWriteEnable = VK_FALSE;
			pipelineCreateInfo.pushConstantRangeCount = (u32)pushConstantRanges.size();
			pipelineCreateInfo.pushConstants = pushConstantRanges.data();
			pipelineCreateInfo.bPersistent = true;
			CreateGraphicsPipeline(&pipelineCreateInfo, pipelineID);
			GraphicsPipeline* pipeline = GetGraphicsPipeline(pipelineID)->pipeline;

			// Render

			VulkanTexture* cubemapTexture = (VulkanTexture*)renderObjectMat->textures[U_CUBEMAP_SAMPLER];

			VkClearValue clearValues[1];
			clearValues[0].color = m_ClearColour;

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
				cubemapTexture,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);


			VulkanBuffer* skyboxVertexBuffer = m_StaticVertexBuffers[m_Shaders[skyboxMat->shaderID]->staticVertexBufferIndex].second;
			VulkanBuffer* skyboxIndexBuffer = m_StaticIndexBuffer;
			if (skyboxVertexBuffer->m_Buffer == VK_NULL_HANDLE)
			{
				PrintError("Attempted to generate cubemap from HDR but vertex buffer has not been generated! (for shader %s)\n", skyboxMat->name.c_str());
				return;
			}
			if (skyboxRenderObject->bIndexed &&
				skyboxIndexBuffer->m_Buffer == VK_NULL_HANDLE)
			{
				PrintError("Attempted to generate cubemap from HDR but index buffer has not been generated! (for shader %s)\n", skyboxMat->name.c_str());
				return;
			}

			glm::mat4 perspectiveMat = glm::perspective(PI_DIV_TWO, 1.0f, 0.1f, (real)dim);
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
					skyboxMat->pushConstantBlock->SetData(s_CaptureViews[face], perspectiveMat);
					vkCmdPushConstants(cmdBuf, pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
						skyboxMat->pushConstantBlock->size, skyboxMat->pushConstantBlock->data);

					vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);

					BindDescriptorSet(equirectangularToCubeMat, 0, cmdBuf, pipeline->layout, descriptorSet);

					VkDeviceSize offsets[1] = { 0 };

					vkCmdBindVertexBuffers(cmdBuf, 0, 1, &skyboxVertexBuffer->m_Buffer, offsets);
					if (skyboxRenderObject->bIndexed)
					{
						vkCmdBindIndexBuffer(cmdBuf, skyboxIndexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
						vkCmdDrawIndexed(cmdBuf, (u32)skyboxRenderObject->indices->size(), 1, 0, 0, 0);
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
						cubemapTexture->image,
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						1,
						&copyRegion);

					// Transform framebuffer colour attachment back
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
				cubemapTexture,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				subresourceRange);

			EndDebugMarkerRegion(cmdBuf, "End Generate Cubemap from HDR");

			m_CommandBufferManager.FlushCommandBuffer(cmdBuf, m_GraphicsQueue, true);

			vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, offscreen.framebuffer, nullptr);
			vkFreeMemory(m_VulkanDevice->m_LogicalDevice, offscreen.memory, nullptr);
			vkDestroyImageView(m_VulkanDevice->m_LogicalDevice, offscreen.view, nullptr);
			vkDestroyImage(m_VulkanDevice->m_LogicalDevice, offscreen.image, nullptr);
			DestroyGraphicsPipeline(pipelineID);
		}

		void VulkanRenderer::GenerateIrradianceSampler(VulkanRenderObject* renderObject)
		{
			if (m_SkyBoxMesh == nullptr)
			{
				PrintError("Attempted to generate cubemap before skybox object was created!\n");
				return;
			}

			VulkanRenderObject* skyboxRenderObject = GetRenderObject(m_SkyBoxMesh->GetSubMesh(0)->renderID);
			VulkanMaterial* skyboxMat = (VulkanMaterial*)m_Materials.at(skyboxRenderObject->materialID);
			VulkanMaterial* renderObjectMat = (VulkanMaterial*)m_Materials.at(renderObject->materialID);

			const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
			const u32 dim = (u32)renderObjectMat->irradianceSamplerSize.x;
			assert(dim <= MAX_TEXTURE_DIM);
			const u32 mipLevels = static_cast<u32>(floor(log2(dim))) + 1;

			VulkanRenderPass renderPass(m_VulkanDevice);
			renderPass.RegisterForColourOnly("Generate Irradiance render pass", InvalidFrameBufferAttachmentID, {});
			renderPass.bCreateFrameBuffer = false;
			renderPass.m_ColourAttachmentFormat = format;
			renderPass.ManuallySpecifyLayouts({ VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }, { VK_IMAGE_LAYOUT_UNDEFINED });
			renderPass.Create();

			// Offscreen framebuffer
			struct {
				VkImage image;
				VkImageView view;
				VkDeviceMemory memory;
				VkFramebuffer framebuffer;
			} offscreen;

			// Colour attachment
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

			VkImageViewCreateInfo colourImageView = vks::imageViewCreateInfo();
			colourImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
			colourImageView.format = format;
			colourImageView.flags = 0;
			colourImageView.subresourceRange = {};
			colourImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			colourImageView.subresourceRange.baseMipLevel = 0;
			colourImageView.subresourceRange.levelCount = 1;
			colourImageView.subresourceRange.baseArrayLayer = 0;
			colourImageView.subresourceRange.layerCount = 1;
			colourImageView.image = offscreen.image;
			VK_CHECK_RESULT(vkCreateImageView(m_VulkanDevice->m_LogicalDevice, &colourImageView, nullptr, &offscreen.view));

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

			VulkanMaterial* irradianceMaterial = (VulkanMaterial*)m_Materials[m_IrradianceMaterialID];
			VulkanShader* irradianceShader = (VulkanShader*)m_Shaders[irradianceMaterial->shaderID];

			DescriptorSetCreateInfo irradianceDescriptorCreateInfo = {};
			irradianceDescriptorCreateInfo.DBG_Name = "Irradiance descriptor set";
			irradianceDescriptorCreateInfo.descriptorSetLayout = &m_DescriptorPoolPersistent->descriptorSetLayouts[irradianceMaterial->shaderID];
			irradianceDescriptorCreateInfo.shaderID = irradianceMaterial->shaderID;
			irradianceDescriptorCreateInfo.uniformBufferList = &irradianceMaterial->uniformBufferList;
			VulkanTexture* cubemapTexture = (VulkanTexture*)renderObjectMat->textures[U_CUBEMAP_SAMPLER];
			irradianceDescriptorCreateInfo.imageDescriptors.SetUniform(U_CUBEMAP_SAMPLER, ImageDescriptorInfo{ cubemapTexture->imageView, m_LinMipLinSampler });
			FillOutBufferDescriptorInfos(&irradianceDescriptorCreateInfo.bufferDescriptors, irradianceDescriptorCreateInfo.uniformBufferList, irradianceDescriptorCreateInfo.shaderID);
			VkDescriptorSet descriptorSet = m_DescriptorPoolPersistent->CreateDescriptorSet(&irradianceDescriptorCreateInfo);

			std::array<VkPushConstantRange, 1> pushConstantRanges = {};
			pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			pushConstantRanges[0].offset = 0;
			pushConstantRanges[0].size = irradianceShader->pushConstantBlockSize;

			GraphicsPipelineID pipelineID = InvalidGraphicsPipelineID;
			GraphicsPipelineCreateInfo pipelineCreateInfo = {};
			pipelineCreateInfo.DBG_Name = "Irradiance pipeline";
			pipelineCreateInfo.renderPass = renderPass;
			pipelineCreateInfo.shaderID = irradianceMaterial->shaderID;
			pipelineCreateInfo.vertexAttributes = irradianceShader->vertexAttributes;
			pipelineCreateInfo.topology = skyboxRenderObject->topology;
			pipelineCreateInfo.cullMode = skyboxRenderObject->cullMode;
			pipelineCreateInfo.descriptorSetLayoutIndex = irradianceMaterial->shaderID;
			pipelineCreateInfo.bSetDynamicStates = true;
			pipelineCreateInfo.bEnableAdditiveColourBlending = false;
			pipelineCreateInfo.subpass = 0;
			pipelineCreateInfo.depthTestEnable = VK_FALSE;
			pipelineCreateInfo.depthWriteEnable = VK_FALSE;
			pipelineCreateInfo.pushConstantRangeCount = (u32)pushConstantRanges.size();
			pipelineCreateInfo.pushConstants = pushConstantRanges.data();
			pipelineCreateInfo.bPersistent = true;
			CreateGraphicsPipeline(&pipelineCreateInfo, pipelineID);
			GraphicsPipeline* pipeline = GetGraphicsPipeline(pipelineID)->pipeline;

			// Render

			VulkanTexture* irradianceTexture = (VulkanTexture*)renderObjectMat->textures[U_IRRADIANCE_SAMPLER];

			VkClearValue clearValues[1];
			clearValues[0].color = m_ClearColour;

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
				irradianceTexture,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);
			irradianceTexture->imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

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
					skyboxMat->pushConstantBlock->SetData(s_CaptureViews[face], glm::perspective(PI_DIV_TWO, 1.0f, 0.1f, (real)dim));
					vkCmdPushConstants(cmdBuf, pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
						skyboxMat->pushConstantBlock->size, skyboxMat->pushConstantBlock->data);

					vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);

					BindDescriptorSet(irradianceMaterial, 0, cmdBuf, pipeline->layout, descriptorSet);

					VkDeviceSize offsets[1] = { 0 };

					VulkanShader* skyboxShader = (VulkanShader*)m_Shaders[skyboxMat->shaderID];
					VulkanBuffer* vertBuffer = m_StaticVertexBuffers[skyboxShader->staticVertexBufferIndex].second;
					vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vertBuffer->m_Buffer, offsets);
					if (skyboxRenderObject->bIndexed)
					{
						vkCmdBindIndexBuffer(cmdBuf, m_StaticIndexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
						vkCmdDrawIndexed(cmdBuf, (u32)skyboxRenderObject->indices->size(), 1, skyboxRenderObject->indexOffset, skyboxRenderObject->vertexOffset, 0);
					}
					else
					{
						vkCmdDraw(cmdBuf, skyboxRenderObject->vertexBufferData->VertexCount, 1, skyboxRenderObject->vertexOffset, 0);
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
						irradianceTexture->image,
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						1,
						&copyRegion);

					// Transform framebuffer colour attachment back
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
				irradianceTexture,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				subresourceRange);
			irradianceTexture->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			EndDebugMarkerRegion(cmdBuf, "End Generate Irradiance");

			m_CommandBufferManager.FlushCommandBuffer(cmdBuf, m_GraphicsQueue, true);

			vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, offscreen.framebuffer, nullptr);
			vkFreeMemory(m_VulkanDevice->m_LogicalDevice, offscreen.memory, nullptr);
			vkDestroyImageView(m_VulkanDevice->m_LogicalDevice, offscreen.view, nullptr);
			vkDestroyImage(m_VulkanDevice->m_LogicalDevice, offscreen.image, nullptr);
			DestroyGraphicsPipeline(pipelineID);
		}

		void VulkanRenderer::GeneratePrefilteredCube(VulkanRenderObject* renderObject)
		{
			if (m_SkyBoxMesh == nullptr)
			{
				PrintError("Attempted to generate cubemap before skybox object was created!\n");
				return;
			}

			VulkanRenderObject* skyboxRenderObject = GetRenderObject(m_SkyBoxMesh->GetSubMesh(0)->renderID);
			VulkanMaterial* skyboxMat = (VulkanMaterial*)m_Materials.at(skyboxRenderObject->materialID);
			VulkanMaterial* renderObjectMat = (VulkanMaterial*)m_Materials.at(renderObject->materialID);

			const VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
			const u32 dim = (u32)renderObjectMat->prefilteredMapSize.x;
			assert(dim <= MAX_TEXTURE_DIM);
			const u32 mipLevels = static_cast<u32>(floor(log2(dim))) + 1;

			VulkanRenderPass renderPass(m_VulkanDevice);
			renderPass.RegisterForColourOnly("Generate Prefiltered Cube render pass", InvalidFrameBufferAttachmentID, {});
			renderPass.bCreateFrameBuffer = false;
			renderPass.m_ColourAttachmentFormat = format;
			renderPass.ManuallySpecifyLayouts({ VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }, { VK_IMAGE_LAYOUT_UNDEFINED });
			renderPass.Create();

			struct {
				VkImage image;
				VkImageView view;
				VkDeviceMemory memory;
				VkFramebuffer framebuffer;
			} offscreen;

			// Offscreen framebuffer
			{
				// Colour attachment
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

				VkImageViewCreateInfo colourImageView = vks::imageViewCreateInfo();
				colourImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
				colourImageView.format = format;
				colourImageView.flags = 0;
				colourImageView.subresourceRange = {};
				colourImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				colourImageView.subresourceRange.baseMipLevel = 0;
				colourImageView.subresourceRange.levelCount = 1;
				colourImageView.subresourceRange.baseArrayLayer = 0;
				colourImageView.subresourceRange.layerCount = 1;
				colourImageView.image = offscreen.image;
				VK_CHECK_RESULT(vkCreateImageView(m_VulkanDevice->m_LogicalDevice, &colourImageView, nullptr, &offscreen.view));

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

			VulkanMaterial* prefilterMaterial = (VulkanMaterial*)m_Materials[m_PrefilterMaterialID];
			VulkanShader* prefilterShader = (VulkanShader*)m_Shaders[prefilterMaterial->shaderID];

			DescriptorSetCreateInfo prefilterDescriptorCreateInfo = {};
			prefilterDescriptorCreateInfo.DBG_Name = "Prefilter descriptor set";
			prefilterDescriptorCreateInfo.descriptorSetLayout = &m_DescriptorPoolPersistent->descriptorSetLayouts[prefilterMaterial->shaderID];
			prefilterDescriptorCreateInfo.shaderID = prefilterMaterial->shaderID;
			prefilterDescriptorCreateInfo.uniformBufferList = &prefilterMaterial->uniformBufferList;
			VulkanTexture* cubemapTexture = (VulkanTexture*)renderObjectMat->textures[U_CUBEMAP_SAMPLER];
			prefilterDescriptorCreateInfo.imageDescriptors.SetUniform(U_CUBEMAP_SAMPLER, ImageDescriptorInfo{ cubemapTexture->imageView, m_LinMipLinSampler });
			FillOutBufferDescriptorInfos(&prefilterDescriptorCreateInfo.bufferDescriptors, prefilterDescriptorCreateInfo.uniformBufferList, prefilterDescriptorCreateInfo.shaderID);
			VkDescriptorSet descriptorSet = m_DescriptorPoolPersistent->CreateDescriptorSet(&prefilterDescriptorCreateInfo);

			std::array<VkPushConstantRange, 1> pushConstantRanges = {};
			pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			pushConstantRanges[0].offset = 0;
			pushConstantRanges[0].size = prefilterShader->pushConstantBlockSize;

			GraphicsPipelineID pipelineID = InvalidGraphicsPipelineID;
			GraphicsPipelineCreateInfo pipelineCreateInfo = {};
			pipelineCreateInfo.DBG_Name = "Prefiltered cube pipeline";
			pipelineCreateInfo.renderPass = renderPass;
			pipelineCreateInfo.shaderID = prefilterMaterial->shaderID;
			pipelineCreateInfo.vertexAttributes = prefilterShader->vertexAttributes;
			pipelineCreateInfo.topology = skyboxRenderObject->topology;
			pipelineCreateInfo.cullMode = skyboxRenderObject->cullMode;
			pipelineCreateInfo.descriptorSetLayoutIndex = prefilterMaterial->shaderID;
			pipelineCreateInfo.bSetDynamicStates = true;
			pipelineCreateInfo.bEnableAdditiveColourBlending = false;
			pipelineCreateInfo.subpass = 0;
			pipelineCreateInfo.depthTestEnable = VK_FALSE;
			pipelineCreateInfo.depthWriteEnable = VK_FALSE;
			pipelineCreateInfo.pushConstantRangeCount = (u32)pushConstantRanges.size();
			pipelineCreateInfo.pushConstants = pushConstantRanges.data();
			pipelineCreateInfo.bPersistent = true;
			CreateGraphicsPipeline(&pipelineCreateInfo, pipelineID);
			GraphicsPipeline* pipeline = GetGraphicsPipeline(pipelineID)->pipeline;

			// Render

			VulkanTexture* prefilterTexture = (VulkanTexture*)renderObjectMat->textures[U_PREFILTER_MAP];

			VkClearValue clearValues[1];
			clearValues[0].color = m_ClearColour;

			VkRenderPassBeginInfo renderPassBeginInfo = vks::renderPassBeginInfo(renderPass);
			renderPassBeginInfo.framebuffer = offscreen.framebuffer;
			renderPassBeginInfo.renderArea.extent = { dim, dim };
			renderPassBeginInfo.clearValueCount = 1;
			renderPassBeginInfo.pClearValues = clearValues;

			VkCommandBuffer cmdBuf = m_CommandBufferManager.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			BeginDebugMarkerRegion(cmdBuf, "Generate Prefiltered Cube");

			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = mipLevels;
			subresourceRange.layerCount = 6;

			// Change image layout for all cubemap faces to transfer destination
			SetImageLayout(
				cmdBuf,
				prefilterTexture,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);
			prefilterTexture->imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

			std::vector<Pair<void*, u32>> pushConstants;
			pushConstants.reserve(2);

			glm::mat4 pespectiveMat = glm::perspective(PI_DIV_TWO, 1.0f, 0.1f, (real)dim);

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

					//real roughness = 0.5f;

					// Push constants
					pushConstants.clear();
					pushConstants.emplace_back((void*)&s_CaptureViews[face], (u32)sizeof(glm::mat4));
					pushConstants.emplace_back((void*)&pespectiveMat, (u32)sizeof(glm::mat4));
					skyboxMat->pushConstantBlock->SetData(pushConstants);

					vkCmdPushConstants(cmdBuf, pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
						skyboxMat->pushConstantBlock->size, skyboxMat->pushConstantBlock->data);

					vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);

					BindDescriptorSet(prefilterMaterial, 0, cmdBuf, pipeline->layout, descriptorSet);

					VkDeviceSize offsets[1] = { 0 };

					vkCmdBindVertexBuffers(cmdBuf, 0, 1, &m_StaticVertexBuffers[m_Shaders[skyboxMat->shaderID]->staticVertexBufferIndex].second->m_Buffer, offsets);
					if (skyboxRenderObject->bIndexed)
					{
						vkCmdBindIndexBuffer(cmdBuf, m_StaticIndexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
						vkCmdDrawIndexed(cmdBuf, (u32)skyboxRenderObject->indices->size(), 1, skyboxRenderObject->indexOffset, skyboxRenderObject->vertexOffset, 0);
					}
					else
					{
						vkCmdDraw(cmdBuf, skyboxRenderObject->vertexBufferData->VertexCount, 1, skyboxRenderObject->vertexOffset, 0);
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
						prefilterTexture->image,
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						1,
						&copyRegion);

					// Transform framebuffer colour attachment back
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
				prefilterTexture,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				subresourceRange);
			prefilterTexture->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			EndDebugMarkerRegion(cmdBuf, "End Generate Prefiltered Cube");

			m_CommandBufferManager.FlushCommandBuffer(cmdBuf, m_GraphicsQueue, true);

			vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, offscreen.framebuffer, nullptr);
			vkFreeMemory(m_VulkanDevice->m_LogicalDevice, offscreen.memory, nullptr);
			vkDestroyImageView(m_VulkanDevice->m_LogicalDevice, offscreen.view, nullptr);
			vkDestroyImage(m_VulkanDevice->m_LogicalDevice, offscreen.image, nullptr);
			DestroyGraphicsPipeline(pipelineID);
		}

		void VulkanRenderer::GenerateBRDFLUT()
		{
			if (!bRenderedBRDFLUT)
			{
				const u32 dim = (u32)m_BRDFSize.x;
				assert(dim <= MAX_TEXTURE_DIM);

				VulkanRenderPass renderPass(m_VulkanDevice);
				renderPass.RegisterForColourOnly("Generate BRDF LUT render pass", InvalidFrameBufferAttachmentID, {});
				renderPass.bCreateFrameBuffer = false;
				renderPass.m_ColourAttachmentFormat = m_BRDFTexture->imageFormat;
				renderPass.ManuallySpecifyLayouts({ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, { VK_IMAGE_LAYOUT_UNDEFINED });
				renderPass.Create();

				VkFramebufferCreateInfo framebufferCreateInfo = vks::framebufferCreateInfo(renderPass);
				framebufferCreateInfo.attachmentCount = 1;
				framebufferCreateInfo.pAttachments = &m_BRDFTexture->imageView;
				framebufferCreateInfo.width = dim;
				framebufferCreateInfo.height = dim;

				VkFramebuffer framebuffer = VK_NULL_HANDLE;
				VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &framebufferCreateInfo, nullptr, &framebuffer));

				VulkanMaterial* brdfMaterial = (VulkanMaterial*)m_Materials[m_BRDFMaterialID];
				VulkanShader* brdfShader = (VulkanShader*)m_Shaders[brdfMaterial->shaderID];

				m_DescriptorPoolPersistent->CreateDescriptorSet(m_BRDFMaterialID, "BRDF descriptor set");

				GraphicsPipelineID pipelineID = InvalidGraphicsPipelineID;
				GraphicsPipelineCreateInfo pipelineCreateInfo = {};
				pipelineCreateInfo.DBG_Name = "BRDF LUT pipeline";
				pipelineCreateInfo.renderPass = renderPass;
				pipelineCreateInfo.shaderID = brdfMaterial->shaderID;
				pipelineCreateInfo.vertexAttributes = brdfShader->vertexAttributes;
				pipelineCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
				pipelineCreateInfo.cullMode = VK_CULL_MODE_NONE;
				pipelineCreateInfo.descriptorSetLayoutIndex = brdfMaterial->shaderID;
				pipelineCreateInfo.bSetDynamicStates = true;
				pipelineCreateInfo.bEnableAdditiveColourBlending = false;
				pipelineCreateInfo.subpass = 0;
				pipelineCreateInfo.depthTestEnable = VK_FALSE;
				pipelineCreateInfo.depthWriteEnable = VK_FALSE;
				pipelineCreateInfo.bPersistent = true;
				CreateGraphicsPipeline(&pipelineCreateInfo, pipelineID);
				GraphicsPipeline* pipeline = GetGraphicsPipeline(pipelineID)->pipeline;

				// Render

				VkClearValue clearValues[1];
				clearValues[0].color = m_ClearColour;

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

				vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
				vkCmdDraw(cmdBuf, 3, 1, 0, 0);

				vkCmdEndRenderPass(cmdBuf);

				EndDebugMarkerRegion(cmdBuf, "End Generate BRDF LUT");

				m_CommandBufferManager.FlushCommandBuffer(cmdBuf, m_GraphicsQueue, true);

				VK_CHECK_RESULT(vkQueueWaitIdle(m_GraphicsQueue));

				vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, framebuffer, nullptr);
				DestroyGraphicsPipeline(pipelineID);

				bRenderedBRDFLUT = true;
			}
		}

		void VulkanRenderer::CreateSSAOPipelines()
		{
			VulkanRenderObject* gBufferObject = GetRenderObject(m_GBufferQuadRenderID);

			GraphicsPipelineCreateInfo pipelineCreateInfo = {};
			pipelineCreateInfo.bSetDynamicStates = true;
			pipelineCreateInfo.topology = gBufferObject->topology;
			pipelineCreateInfo.cullMode = gBufferObject->cullMode;
			pipelineCreateInfo.bEnableColourBlending = false;
			pipelineCreateInfo.bPersistent = true;

			VulkanMaterial* ssaoMaterial = (VulkanMaterial*)m_Materials[m_SSAOMatID];
			VulkanShader* ssaoShader = (VulkanShader*)m_Shaders[ssaoMaterial->shaderID];

			pipelineCreateInfo.DBG_Name = "SSAO pipeline";
			pipelineCreateInfo.shaderID = ssaoMaterial->shaderID;
			pipelineCreateInfo.vertexAttributes = ssaoShader->vertexAttributes;
			pipelineCreateInfo.descriptorSetLayoutIndex = ssaoMaterial->descriptorSetLayoutIndex;
			pipelineCreateInfo.subpass = ssaoShader->subpass;
			pipelineCreateInfo.depthWriteEnable = ssaoShader->bDepthWriteEnable ? VK_TRUE : VK_FALSE;
			pipelineCreateInfo.depthCompareOp = gBufferObject->depthCompareOp;
			pipelineCreateInfo.renderPass = ssaoShader->renderPass;
			pipelineCreateInfo.fragSpecializationInfo = ssaoShader->fragSpecializationInfo;
			CreateGraphicsPipeline(&pipelineCreateInfo, m_SSAOGraphicsPipelineID);

			VulkanMaterial* ssaoBlurMaterial = (VulkanMaterial*)m_Materials[m_SSAOBlurMatID];
			VulkanShader* ssaoBlurShader = (VulkanShader*)m_Shaders[ssaoBlurMaterial->shaderID];

			pipelineCreateInfo.DBG_Name = "SSAO Blur Horizontal pipeline";
			pipelineCreateInfo.shaderID = ssaoBlurMaterial->shaderID;
			pipelineCreateInfo.vertexAttributes = ssaoBlurShader->vertexAttributes;
			pipelineCreateInfo.descriptorSetLayoutIndex = ssaoBlurMaterial->descriptorSetLayoutIndex;
			pipelineCreateInfo.subpass = 0;
			pipelineCreateInfo.depthWriteEnable = ssaoBlurShader->bDepthWriteEnable ? VK_TRUE : VK_FALSE;
			pipelineCreateInfo.renderPass = ssaoBlurShader->renderPass;
			CreateGraphicsPipeline(&pipelineCreateInfo, m_SSAOBlurHGraphicsPipelineID);

			pipelineCreateInfo.DBG_Name = "SSAO Blur Vertcical pipeline";
			pipelineCreateInfo.shaderID = ssaoBlurMaterial->shaderID;
			pipelineCreateInfo.vertexAttributes = ssaoBlurShader->vertexAttributes;
			pipelineCreateInfo.descriptorSetLayoutIndex = ssaoBlurMaterial->descriptorSetLayoutIndex;
			pipelineCreateInfo.subpass = 0;
			pipelineCreateInfo.depthWriteEnable = ssaoBlurShader->bDepthWriteEnable ? VK_TRUE : VK_FALSE;
			pipelineCreateInfo.renderPass = ssaoBlurShader->renderPass;
			CreateGraphicsPipeline(&pipelineCreateInfo, m_SSAOBlurVGraphicsPipelineID);
		}

		void VulkanRenderer::CreateSSAODescriptorSets()
		{
			VulkanMaterial* ssaoMaterial = (VulkanMaterial*)m_Materials[m_SSAOMatID];

			VkDescriptorSetLayout descSetLayout = m_DescriptorPoolPersistent->descriptorSetLayouts[ssaoMaterial->shaderID];

			DescriptorSetCreateInfo descSetCreateInfo = {};
			descSetCreateInfo.DBG_Name = "SSAO descriptor set";
			descSetCreateInfo.descriptorSetLayout = &descSetLayout;
			descSetCreateInfo.shaderID = ssaoMaterial->shaderID;
			descSetCreateInfo.uniformBufferList = &ssaoMaterial->uniformBufferList;
			descSetCreateInfo.imageDescriptors.SetUniform(U_DEPTH_SAMPLER, ImageDescriptorInfo{ m_GBufferDepthAttachment->view, m_DepthSampler });
			descSetCreateInfo.imageDescriptors.SetUniform(U_SSAO_NORMAL_SAMPLER, ImageDescriptorInfo{ m_GBufferColourAttachment0->view, m_NearestClampEdgeSampler });
			VulkanTexture* noiseTexture = (VulkanTexture*)ssaoMaterial->textures[U_NOISE_SAMPLER];
			descSetCreateInfo.imageDescriptors.SetUniform(U_NOISE_SAMPLER, ImageDescriptorInfo{ noiseTexture->imageView, m_NearestClampEdgeSampler });
			FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
			m_SSAODescSet = m_DescriptorPoolPersistent->CreateDescriptorSet(&descSetCreateInfo);

			VulkanMaterial* ssaoBlurMaterial = (VulkanMaterial*)m_Materials[m_SSAOBlurMatID];

			descSetLayout = m_DescriptorPoolPersistent->descriptorSetLayouts[ssaoBlurMaterial->shaderID];

			descSetCreateInfo = {};
			descSetCreateInfo.DBG_Name = "SSAO Blur Horizontal descriptor set";
			descSetCreateInfo.descriptorSetLayout = &descSetLayout;
			descSetCreateInfo.shaderID = ssaoBlurMaterial->shaderID;
			descSetCreateInfo.uniformBufferList = &ssaoBlurMaterial->uniformBufferList;
			descSetCreateInfo.imageDescriptors.SetUniform(U_DEPTH_SAMPLER, ImageDescriptorInfo{ m_GBufferDepthAttachment->view, m_DepthSampler });
			descSetCreateInfo.imageDescriptors.SetUniform(U_SSAO_RAW_SAMPLER, ImageDescriptorInfo{ m_SSAOFBColourAttachment0->view, m_NearestClampEdgeSampler });
			descSetCreateInfo.imageDescriptors.SetUniform(U_SSAO_NORMAL_SAMPLER, ImageDescriptorInfo{ m_GBufferColourAttachment0->view, m_NearestClampEdgeSampler });
			FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
			m_SSAOBlurHDescSet = m_DescriptorPoolPersistent->CreateDescriptorSet(&descSetCreateInfo);

			descSetCreateInfo = {};
			descSetCreateInfo.DBG_Name = "SSAO Blur Vertical descriptor set";
			descSetCreateInfo.descriptorSetLayout = &descSetLayout;
			descSetCreateInfo.shaderID = ssaoBlurMaterial->shaderID;
			descSetCreateInfo.uniformBufferList = &ssaoBlurMaterial->uniformBufferList;
			descSetCreateInfo.imageDescriptors.SetUniform(U_DEPTH_SAMPLER, ImageDescriptorInfo{ m_GBufferDepthAttachment->view, m_DepthSampler });
			descSetCreateInfo.imageDescriptors.SetUniform(U_SSAO_RAW_SAMPLER, ImageDescriptorInfo{ m_SSAOBlurHFBColourAttachment0->view, m_NearestClampEdgeSampler });
			descSetCreateInfo.imageDescriptors.SetUniform(U_SSAO_NORMAL_SAMPLER, ImageDescriptorInfo{ m_GBufferColourAttachment0->view, m_NearestClampEdgeSampler });
			FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
			m_SSAOBlurVDescSet = m_DescriptorPoolPersistent->CreateDescriptorSet(&descSetCreateInfo);
		}

		void VulkanRenderer::CreateWireframeDescriptorSets()
		{
			if (m_WireframeDescSet != VK_NULL_HANDLE)
			{
				m_DescriptorPoolPersistent->FreeSet(m_WireframeDescSet);
			}

			VulkanMaterial* wireframeMaterial = (VulkanMaterial*)m_Materials[m_WireframeMatID];

			VkDescriptorSetLayout descSetLayout = m_DescriptorPoolPersistent->descriptorSetLayouts[wireframeMaterial->shaderID];

			DescriptorSetCreateInfo descSetCreateInfo = {};
			descSetCreateInfo.DBG_Name = "Wireframe descriptor set";
			descSetCreateInfo.descriptorSetLayout = &descSetLayout;
			descSetCreateInfo.shaderID = wireframeMaterial->shaderID;
			descSetCreateInfo.uniformBufferList = &wireframeMaterial->uniformBufferList;
			FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
			m_WireframeDescSet = m_DescriptorPoolPersistent->CreateDescriptorSet(&descSetCreateInfo);
		}

		u32 VulkanRenderer::GetActiveRenderObjectCount() const
		{
			u32 capacity = 0;
			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject != nullptr)
				{
					++capacity;
				}
			}
			return capacity;
		}

		bool VulkanRenderer::LoadFont(FontMetaData& fontMetaData, bool bForceRender)
		{
			std::vector<char> fileMemory;
			ReadFile(fontMetaData.filePath, fileMemory, true);

			std::map<i32, FontMetric*> characters;
			std::array<glm::vec2i, 4> maxPos;
			FT_Face face = {};
			if (!g_ResourceManager->LoadFontMetrics(fileMemory, m_FTLibrary, fontMetaData, &characters, &maxPos, &face))
			{
				return false;
			}

			const std::string fileName = StripLeadingDirectories(fontMetaData.filePath);

			BitmapFont* newFont = fontMetaData.bitmapFont;

			// TODO: Save in common place
			u32 sampleDensity = 32;
			u32 padding = 1;
			u32 spread = 5;

			bool bUsingPreRenderedTexture = false;
			std::string textureName = std::string("Font atlas ") + fileName;
			VkFormat fontTexFormat = VK_FORMAT_R8G8B8A8_UNORM;
			if (!bForceRender)
			{
				if (FileExists(fontMetaData.renderedTextureFilePath))
				{
					VulkanTexture* fontTex = (VulkanTexture*)newFont->SetTexture(new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, textureName));
					if (fontTex->CreateFromFile(fontMetaData.renderedTextureFilePath, fontTexFormat) != 0)
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
				Print("Rendering font %s to sdf texture...\n", fileName.c_str());

				glm::vec2i textureSize(
					std::max(std::max(maxPos[0].x, maxPos[1].x), std::max(maxPos[2].x, maxPos[3].x)),
					std::max(std::max(maxPos[0].y, maxPos[1].y), std::max(maxPos[2].y, maxPos[3].y)));

				VulkanTexture* fontTexColAttachment = (VulkanTexture*)newFont->SetTexture(new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, textureName));
				fontTexColAttachment->CreateEmpty(textureSize.x, textureSize.y, 4, fontTexFormat, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
				fontTexColAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

				VulkanMaterial* computeSDFMaterial = (VulkanMaterial*)m_Materials[m_ComputeSDFMatID];
				ShaderID computeSDFShaderID = computeSDFMaterial->shaderID;
				VulkanShader* computeSDFShader = (VulkanShader*)m_Shaders[computeSDFShaderID];

				VulkanRenderPass renderPass(m_VulkanDevice);
				renderPass.RegisterForColourOnly("Font SDF render pass", InvalidFrameBufferAttachmentID, {});
				renderPass.bCreateFrameBuffer = false;
				renderPass.m_ColourAttachmentFormat = fontTexFormat;
				renderPass.Create();

				VkFramebufferCreateInfo framebufCreateInfo = vks::framebufferCreateInfo(renderPass);
				framebufCreateInfo.attachmentCount = 1;
				framebufCreateInfo.pAttachments = &fontTexColAttachment->imageView;
				framebufCreateInfo.width = textureSize.x;
				framebufCreateInfo.height = textureSize.y;
				VkFramebuffer framebuffer = VK_NULL_HANDLE;
				VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &framebufCreateInfo, nullptr, &framebuffer));

				VkCommandBuffer commandBuffer = m_CommandBufferManager.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

				BeginDebugMarkerRegion(commandBuffer, "Generate font SDF");

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
				clearCol.color = VkClearColorValue{ 0.0f, 0.0f, 0.0f, 0.0f };
				renderPassBeginInfo.pClearValues = &clearCol;
				vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				GraphicsPipelineID pipelineID = InvalidGraphicsPipelineID;
				GraphicsPipelineCreateInfo pipelineCreateInfo = {};
				pipelineCreateInfo.DBG_Name = "Load font pipeline";
				pipelineCreateInfo.shaderID = computeSDFShaderID;
				pipelineCreateInfo.vertexAttributes = computeSDFShader->vertexAttributes;
				pipelineCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
				pipelineCreateInfo.cullMode = VK_CULL_MODE_NONE;
				pipelineCreateInfo.descriptorSetLayoutIndex = computeSDFShaderID;
				pipelineCreateInfo.bSetDynamicStates = true;
				pipelineCreateInfo.bEnableAdditiveColourBlending = true;
				pipelineCreateInfo.subpass = 0;
				pipelineCreateInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
				pipelineCreateInfo.depthWriteEnable = VK_FALSE;
				pipelineCreateInfo.renderPass = renderPass;
				pipelineCreateInfo.bPersistent = true;
				CreateGraphicsPipeline(&pipelineCreateInfo, pipelineID);
				GraphicsPipeline* pipeline = GetGraphicsPipeline(pipelineID)->pipeline;

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
					if (FT_Bitmap_Convert(m_FTLibrary, &face->glyph->bitmap, &alignedBitmap, 1))
					{
						PrintError("Couldn't align free type bitmap size\n");
						continue;
					}

					u32 width = alignedBitmap.width;
					u32 height = alignedBitmap.rows;

					assert(width != 0 && height != 0);

					VulkanTexture* highResTex = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "High res tex");
					charTextures.push_back(highResTex);

					++dynamicOffsetIndex;

					highResTex->bSamplerClampToBorder = true;
					// TODO: Pass in command buffer?
					highResTex->CreateFromMemory(alignedBitmap.buffer, width * height * sizeof(u8), width, height, 1, VK_FORMAT_R8_UNORM, 1);

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
					vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_StaticVertexBuffers[computeSDFShader->staticVertexBufferIndex].second->m_Buffer, offsets);

					vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);

					VkDescriptorSetLayout descSetLayout = m_DescriptorPoolPersistent->descriptorSetLayouts[computeSDFShaderID];

					DescriptorSetCreateInfo descSetCreateInfo = {};
					descSetCreateInfo.DBG_Name = "Font SDF descriptor set";
					descSetCreateInfo.descriptorSetLayout = &descSetLayout;
					descSetCreateInfo.shaderID = computeSDFShaderID;
					descSetCreateInfo.uniformBufferList = &computeSDFMaterial->uniformBufferList;
					descSetCreateInfo.imageDescriptors.SetUniform(U_ALBEDO_SAMPLER, ImageDescriptorInfo{ highResTex->imageView, highResTex->sampler });
					FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
					// TODO: Allocate from temporary pool
					VkDescriptorSet descriptorSet = m_DescriptorPoolPersistent->CreateDescriptorSet(&descSetCreateInfo);
					descSets.push_back(descriptorSet);

					BindDescriptorSet(computeSDFMaterial, dynamicOffsetIndex * m_DynamicAlignment, commandBuffer, pipeline->layout, descriptorSet);

					UniformOverrides overrides = {};
					overrides.texChannel = metric->channel;
					overrides.overridenUniforms.AddUniform(U_TEX_CHANNEL);
					overrides.sdfData = glm::vec4((real)res.x, (real)res.y, (real)spread, (real)sampleDensity);
					overrides.overridenUniforms.AddUniform(U_SDF_DATA);
					UpdateDynamicUniformBuffer(m_ComputeSDFMatID, dynamicOffsetIndex * m_DynamicAlignment, MAT4_IDENTITY, &overrides);

					vkCmdDraw(commandBuffer, m_Quad3DVertexBufferData.VertexCount, 1, 0, 0);

					metric->texCoord = metric->texCoord / fontTexSize;

					FT_Bitmap_Done(m_FTLibrary, &alignedBitmap);
				}

				vkCmdEndRenderPass(commandBuffer);

				EndDebugMarkerRegion(commandBuffer, "End generate font SDF");

				VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));
				SetCommandBufferName(m_VulkanDevice, commandBuffer, "Load font command buffer");

				VkSubmitInfo submitInfo = vks::submitInfo(1, &commandBuffer);
				VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				submitInfo.pWaitDstStageMask = &waitStages;

				// TODO: Allow game to progress while this completes
				VK_CHECK_RESULT(vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
				VK_CHECK_RESULT(vkQueueWaitIdle(m_GraphicsQueue));

				for (VulkanTexture* highResTex : charTextures)
				{
					delete highResTex;
				}
				charTextures.clear();

				vkFreeDescriptorSets(m_VulkanDevice->m_LogicalDevice, m_DescriptorPoolPersistent->pool, (u32)descSets.size(), descSets.data());
				descSets.clear();

				DestroyGraphicsPipeline(pipelineID);
				vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, framebuffer, nullptr);

				std::string savedSDFTextureAbsFilePath = RelativePathToAbsolute(fontMetaData.renderedTextureFilePath);
				std::string savedSDFDirectory = ExtractDirectoryString(savedSDFTextureAbsFilePath);
				Platform::CreateDirectoryRecursive(savedSDFDirectory);
				if (!fontTexColAttachment->SaveToFile(savedSDFTextureAbsFilePath, ImageFormat::PNG))
				{
					PrintError("Failed to write generated font SDF to %s\n", savedSDFTextureAbsFilePath.c_str());
				}

				fontTexColAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			}

			FT_Done_Face(face);

			return true;
		}

		void VulkanRenderer::RecreateShadowFrameBuffers()
		{
			VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

			VkImageCreateInfo imageCreateInfo = vks::imageCreateInfo();
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = m_ShadowBufFormat;
			imageCreateInfo.extent.width = m_ShadowMapBaseResolution;
			imageCreateInfo.extent.height = m_ShadowMapBaseResolution;
			imageCreateInfo.extent.depth = 1;
			imageCreateInfo.mipLevels = 1;
			imageCreateInfo.arrayLayers = m_ShadowCascadeCount;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
			fullImageView.subresourceRange.layerCount = m_ShadowCascadeCount;
			fullImageView.image = m_ShadowImage;
			fullImageView.flags = 0;
			VK_CHECK_RESULT(vkCreateImageView(m_VulkanDevice->m_LogicalDevice, &fullImageView, nullptr, m_ShadowImageView.replace()));
			SetImageViewName(m_VulkanDevice, m_ShadowImageView, "Shadow cascade image view (main)");

			if ((i32)m_ShadowCascades.size() < m_ShadowCascadeCount)
			{
				i32 prevSize = (i32)m_ShadowCascades.size();
				m_ShadowCascades.resize(m_ShadowCascadeCount);
				for (i32 i = prevSize; i < (i32)m_ShadowCascadeCount; ++i)
				{
					m_ShadowCascades[i] = new Cascade(m_VulkanDevice);
				}
			}
			else if ((i32)m_ShadowCascades.size() > m_ShadowCascadeCount)
			{
				for (i32 i = (i32)m_ShadowCascades.size() - 1; i >= (i32)m_ShadowCascadeCount; --i)
				{
					delete m_ShadowCascades[i];
				}
				m_ShadowCascades.resize(m_ShadowCascadeCount);
				m_ShadowCascades.shrink_to_fit();
			}

			// One frame buffer & view per cascade
			for (i32 i = 0; i < m_ShadowCascadeCount; ++i)
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
				snprintf(imageViewName, 256, "Shadow cascade %i image view", i);
				SetImageViewName(m_VulkanDevice, m_ShadowCascades[i]->imageView, imageViewName);

				VkFramebufferCreateInfo shadowFramebufferCreateInfo = vks::framebufferCreateInfo(*m_ShadowRenderPass);
				shadowFramebufferCreateInfo.pAttachments = &m_ShadowCascades[i]->imageView;
				shadowFramebufferCreateInfo.attachmentCount = 1;
				shadowFramebufferCreateInfo.width = m_ShadowMapBaseResolution;
				shadowFramebufferCreateInfo.height = m_ShadowMapBaseResolution;

				char frameBufferName[256];
				snprintf(frameBufferName, 256, "Shadow cascade %i frame buffer", i);

				m_ShadowCascades[i]->frameBuffer.Create(&shadowFramebufferCreateInfo, m_ShadowRenderPass, frameBufferName);

				FrameBufferAttachment::CreateInfo attachmentCreateInfo = {};
				attachmentCreateInfo.width = m_ShadowMapBaseResolution;
				attachmentCreateInfo.height = m_ShadowMapBaseResolution;
				attachmentCreateInfo.format = m_ShadowBufFormat;
				if (m_ShadowCascades[i]->attachment == nullptr)
				{
					m_ShadowCascades[i]->attachment = new FrameBufferAttachment(m_VulkanDevice, attachmentCreateInfo);
				}
				m_ShadowCascades[i]->attachment->bOwnsResources = false;
				m_ShadowCascades[i]->attachment->image = m_ShadowImage;
				m_ShadowCascades[i]->attachment->view = m_ShadowCascades[i]->imageView;
			}
		}

		void VulkanRenderer::InitializeShaders(const std::vector<ShaderInfo>& shaderInfos)
		{
			for (u32 i = 0; i < (u32)m_Shaders.size(); ++i)
			{
				delete m_Shaders[i];
			}
			m_Shaders.clear();

			m_Shaders.resize(shaderInfos.size());
			for (u32 i = 0; i < (u32)shaderInfos.size(); ++i)
			{
				m_Shaders[i] = new VulkanShader(m_VulkanDevice->m_LogicalDevice, shaderInfos[i]);
			}
		}

		bool VulkanRenderer::LoadShaderCode(ShaderID shaderID)
		{
			bool bSuccess = true;

			VulkanShader* shader = (VulkanShader*)m_Shaders[shaderID];

			shader->renderPass = ResolveRenderPassType(shader->renderPassType, shader->name.c_str());

			// Sanity check
			if (!shader->bCompute && shader->renderPass == VK_NULL_HANDLE)
			{
				PrintError("Shader %s's render pass was not set!\n", shader->name.c_str());
				bSuccess = false;
			}

			if (g_bEnableLogging_Loading)
			{
				const std::string vertFileName = StripLeadingDirectories(shader->vertexShaderFilePath);
				const std::string fragFileName = StripLeadingDirectories(shader->fragmentShaderFilePath);
				const std::string geomFileName = StripLeadingDirectories(shader->geometryShaderFilePath);
				const std::string computeFileName = StripLeadingDirectories(shader->computeShaderFilePath);

				Print("Loading shader %s (", shader->name.c_str());

				if (!vertFileName.empty())
				{
					Print(" %s", vertFileName.c_str());
				}

				if (!fragFileName.empty())
				{
					Print(" %s", fragFileName.c_str());
				}

				if (!geomFileName.empty())
				{
					Print(" %s", geomFileName.c_str());
				}

				if (!computeFileName.empty())
				{
					Print(" %s", computeFileName.c_str());
				}

				Print(")\n");
			}

			const bool bUseVertexStage = !shader->vertexShaderFilePath.empty();
			const bool bUseFragmentStage = !shader->fragmentShaderFilePath.empty();
			const bool bUseGeometryStage = !shader->geometryShaderFilePath.empty();
			const bool bUseComputeStage = !shader->computeShaderFilePath.empty();

			if (bUseVertexStage && !ReadFile(shader->vertexShaderFilePath, shader->vertexShaderCode, true))
			{
				PrintError("Could not find vertex shader %s\n", shader->vertexShaderFilePath.c_str());
				bSuccess = false;
			}

			if (bUseFragmentStage && !ReadFile(shader->fragmentShaderFilePath, shader->fragmentShaderCode, true))
			{
				PrintError("Could not find fragment shader %s\n", shader->fragmentShaderFilePath.c_str());
				bSuccess = false;
			}

			if (bUseGeometryStage && !ReadFile(shader->geometryShaderFilePath, shader->geometryShaderCode, true))
			{
				PrintError("Could not find geometry shader %s\n", shader->geometryShaderFilePath.c_str());
				bSuccess = false;
			}

			if (bUseComputeStage && !ReadFile(shader->computeShaderFilePath, shader->computeShaderCode, true))
			{
				PrintError("Could not find compute shader %s\n", shader->computeShaderFilePath.c_str());
				bSuccess = false;
			}

			if (bUseVertexStage)
			{
				if (shader->vertexShaderCode.empty())
				{
					PrintError("Vertex shader code failed to load %s\n", shader->vertexShaderFilePath.c_str());
					bSuccess = false;
				}
				else if (!CreateShaderModule(shader->vertexShaderCode, shader->vertShaderModule.replace()))
				{
					PrintError("Failed to compile vertex shader located at: %s\n", shader->vertexShaderFilePath.c_str());
					bSuccess = false;
				}
				shader->vertexShaderCode.clear();
			}

			if (bUseFragmentStage)
			{
				if (shader->fragmentShaderCode.empty())
				{
					PrintError("Fragment shader code failed to load %s\n", shader->fragmentShaderFilePath.c_str());
					bSuccess = false;
				}
				else if (!CreateShaderModule(shader->fragmentShaderCode, shader->fragShaderModule.replace()))
				{
					PrintError("Failed to compile fragment shader located at: %s\n", shader->fragmentShaderFilePath.c_str());
					bSuccess = false;
				}
				shader->fragmentShaderCode.clear();
			}

			if (bUseGeometryStage)
			{
				if (shader->geometryShaderCode.empty())
				{
					PrintError("Geometry shader code failed to load %s\n", shader->geometryShaderFilePath.c_str());
					bSuccess = false;
				}
				else if (!CreateShaderModule(shader->geometryShaderCode, shader->geomShaderModule.replace()))
				{
					PrintError("Failed to compile geometry shader located at: %s\n", shader->geometryShaderFilePath.c_str());
					bSuccess = false;
				}
				shader->geometryShaderCode.clear();
			}

			if (bUseComputeStage)
			{
				if (shader->computeShaderCode.empty())
				{
					PrintError("Compute shader code failed to load %s\n", shader->computeShaderFilePath.c_str());
					bSuccess = false;
				}
				else if (!CreateShaderModule(shader->computeShaderCode, shader->computeShaderModule.replace()))
				{
					PrintError("Failed to compile compute shader located at: %s\n", shader->computeShaderFilePath.c_str());
					bSuccess = false;
				}
				shader->computeShaderCode.clear();
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

		void VulkanRenderer::DrawText(VkCommandBuffer commandBuffer, bool bScreenSpace)
		{
			MaterialID matID = bScreenSpace ? m_FontMatSSID : m_FontMatWSID;

			if (matID == InvalidMaterialID)
			{
				return;
			}

			const VulkanMaterial* fontMaterial = (VulkanMaterial*)m_Materials[matID];
			const VulkanShader* fontShader = (VulkanShader*)m_Shaders[fontMaterial->shaderID];

			VulkanBuffer* fontVertexBuffer = nullptr;

			// Update dynamic text buffer
			{
				static std::vector<TextVertex2D> textVerticesSS;
				static std::vector<TextVertex3D> textVerticesWS;
				u32 textVertCount = bScreenSpace ? UpdateTextBufferSS(textVerticesSS) : UpdateTextBufferWS(textVerticesWS);

				u32 textBufferByteCount = (u32)(textVertCount * (bScreenSpace ? sizeof(TextVertex2D) : sizeof(TextVertex3D)));

				if (textBufferByteCount > 0)
				{
					u32 dynamicVertexBufferIndex = GetDynamicVertexIndexBufferIndex(CalculateVertexStride(fontShader->vertexAttributes));
					std::pair<u32, VertexIndexBufferPair*> vertexBufferPair = m_DynamicVertexIndexBufferPairs[dynamicVertexBufferIndex];
					fontVertexBuffer = vertexBufferPair.second->vertexBuffer;
					u32 copySize = std::min(textBufferByteCount, (u32)fontVertexBuffer->m_Size);
					if (copySize < textBufferByteCount)
					{
						PrintError("Font vertex buffer is %u bytes too small\n", textBufferByteCount - copySize);
					}
					// TODO: HIDDEN BUG: Probably should have an offset into this?
					VK_CHECK_RESULT(fontVertexBuffer->Map(copySize));
					void* dst = bScreenSpace ? (void*)textVerticesSS.data() : (void*)textVerticesWS.data();
					memcpy(fontVertexBuffer->m_Mapped, dst, copySize);
					fontVertexBuffer->Unmap();
				}
				else
				{
					return;
				}
			}

			const std::vector<BitmapFont*>& fonts = bScreenSpace ? g_ResourceManager->fontsScreenSpace : g_ResourceManager->fontsWorldSpace;

			bool bHasText = false;
			for (BitmapFont* font : fonts)
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

			PROFILE_AUTO(bScreenSpace ? "DrawTextSS" : "DrawTextWS");

			glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
			VkDescriptorSetLayout descSetLayout = m_DescriptorPoolPersistent->descriptorSetLayouts[fontMaterial->shaderID];

			CreateFontGraphicsPipelines();

			GraphicsPipelineID pipelineID = bScreenSpace ? m_FontSSGraphicsPipelineID : m_FontWSGraphicsPipelineID;
			GraphicsPipelineConfiguration* graphicsPipeline = GetGraphicsPipeline(pipelineID);

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline->pipeline->pipeline);

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

			glm::mat4 transformMat = bScreenSpace ? (glm::scale(MAT4_IDENTITY, scaleVec) * ortho) : g_CameraManager->CurrentCamera()->GetViewProjection();
			for (BitmapFont* font : fonts)
			{
				if (font->bufferSize > 0)
				{
					VulkanTexture* fontTex = (VulkanTexture*)font->GetTexture();

					if ((VkDescriptorSet)font->userData == VK_NULL_HANDLE)
					{
						DescriptorSetCreateInfo info;
						info.DBG_Name = bScreenSpace ? "Font SS descriptor set" : "Font WS descriptor set";
						info.descriptorSetLayout = &descSetLayout;
						info.shaderID = fontMaterial->shaderID;
						info.uniformBufferList = &fontMaterial->uniformBufferList;
						info.imageDescriptors.SetUniform(U_ALBEDO_SAMPLER, ImageDescriptorInfo{ fontTex->imageView, fontTex->sampler });
						FillOutBufferDescriptorInfos(&info.bufferDescriptors, info.uniformBufferList, info.shaderID);
						VkDescriptorSet* descSet = (VkDescriptorSet*)&font->userData;
						*descSet = m_DescriptorPoolPersistent->CreateDescriptorSet(&info);
					}

					u32 dynamicOffsetIndex = 0;
					BindDescriptorSet(fontMaterial, dynamicOffsetIndex * m_DynamicAlignment, commandBuffer, graphicsPipeline->pipeline->layout, (VkDescriptorSet)font->userData);

					glm::vec2 texSize(fontTex->width, fontTex->height);

					u32 packedSoftnessOpacity = Pack2FloatToU32(font->metaData.soften, font->metaData.shadowOpacity);
					glm::vec4 fontCharData(font->metaData.threshold, font->metaData.shadowOffset.x, font->metaData.shadowOffset.y, static_cast<real>(packedSoftnessOpacity));

					UniformOverrides overrides = {};
					overrides.overridenUniforms.AddUniform(U_TEX_SIZE);
					overrides.texSize = texSize;
					overrides.overridenUniforms.AddUniform(U_FONT_CHAR_DATA);
					overrides.fontCharData = fontCharData;
					UpdateDynamicUniformBuffer(matID, dynamicOffsetIndex * m_DynamicAlignment, transformMat, &overrides);

					VkDeviceSize offsets[1] = { 0 };
					vkCmdBindVertexBuffers(commandBuffer, 0, 1, &fontVertexBuffer->m_Buffer, offsets);

					vkCmdDraw(commandBuffer, font->bufferSize, 1, font->bufferStart, 0);
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

			MaterialID matID = (batch[0].materialID == InvalidMaterialID ? (batch[0].bScreenSpace ? m_SpriteMatSSID : m_SpriteMatWSID) : batch[0].materialID);
			VulkanMaterial* spriteMat = (VulkanMaterial*)m_Materials.at(matID);
			VulkanShader* spriteShader = (VulkanShader*)m_Shaders[spriteMat->shaderID];

			// TODO: Use instancing!
			VulkanBuffer* vertexBuffer = m_StaticVertexBuffers[spriteShader->staticVertexBufferIndex].second;

			if ((i32)batch.size() > spriteShader->maxObjectCount)
			{
				UpdateShaderMaxObjectCount(spriteMat->shaderID, (i32)batch.size());
				// TODO: Flush GPU queues here?
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
				}

				glm::mat4 model =
					glm::translate(MAT4_IDENTITY, translation) *
					glm::mat4(rotation) *
					glm::scale(MAT4_IDENTITY, scale);

				u32 dynamicUBOOffset = m_SpriteDynamicUBOOffset;
				m_SpriteDynamicUBOOffset += m_DynamicAlignment;

				GraphicsPipeline* graphicsPipeline = GetGraphicsPipeline(spriteRenderObject->graphicsPipelineID)->pipeline;
				VkPipeline pipeline = graphicsPipeline->pipeline;
				VkPipelineLayout pipelineLayout = graphicsPipeline->layout;

				Material::PushConstantBlock* pushBlock = nullptr;
				if (drawInfo.bScreenSpace)
				{
					if (spriteShader->bTextureArr)
					{
						real r = aspectRatio;
						real t = 1.0f;
						m_SpriteOrthoArrPushConstBlock->SetData(MAT4_IDENTITY, glm::ortho(-r, r, -t, t), drawInfo.textureLayer);

						pushBlock = m_SpriteOrthoArrPushConstBlock;

						graphicsPipeline = GetGraphicsPipeline(m_SpriteArrGraphicsPipelineID)->pipeline;
						pipeline = graphicsPipeline->pipeline;
						pipelineLayout = graphicsPipeline->layout;
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

				UniformOverrides overrides = {};
				overrides.colourMultiplier = drawInfo.colour;
				overrides.overridenUniforms.AddUniform(U_COLOUR_MULTIPLIER);
				UpdateDynamicUniformBuffer(matID, dynamicUBOOffset, model, &overrides);

				vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer->m_Buffer, offsets);

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

				VkDescriptorSet descSet = GetSpriteDescriptorSet(drawInfo.textureID, matID, drawInfo.textureLayer);

				BindDescriptorSet(spriteMat, dynamicUBOOffset, commandBuffer, pipelineLayout, descSet);

				vkCmdDraw(commandBuffer, spriteRenderObject->vertexBufferData->VertexCount, 1, spriteRenderObject->vertexOffset, 0);

				++i;
			}
		}

		void VulkanRenderer::DrawUIMesh(UIMesh* uiMesh, VkCommandBuffer commandBuffer)
		{
			Mesh* mesh = uiMesh->GetMesh();
			u32 submeshCount = mesh->GetSubmeshCount();
			if (submeshCount == 0)
			{
				return;
			}

			//const glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
			//const real aspectRatio = (real)frameBufferSize.x / (real)frameBufferSize.y;

			VkDeviceSize offsets[1] = { 0 };

			MaterialID matID = mesh->GetMaterialID(0);
			VulkanMaterial* uiMat = (VulkanMaterial*)m_Materials.at(matID);
			//VulkanShader* uiShader = (VulkanShader*)m_Shaders[uiMat->shaderID];

			VkDescriptorSet descSet = m_DescriptorPoolPersistent->descriptorSets[matID];

			VertexIndexBufferPair* vertexIndexBufferPair = m_DynamicUIVertexIndexBufferPair;
			VulkanBuffer* vertexBuffer = vertexIndexBufferPair->vertexBuffer;
			VulkanBuffer* indexBuffer = vertexIndexBufferPair->indexBuffer;

			//if ((i32)batch.size() > uiShader->maxObjectCount)
			//{
			//	UpdateShaderMaxObjectCount(uiMat->shaderID, (i32)batch.size());
			//	// TODO: Flush GPU queues here?
			//}

			for (u32 i = 0; i < submeshCount; ++i)
			{
				if (!uiMesh->IsSubmeshActive(i))
				{
					continue;
				}

				MeshComponent* meshComponent = mesh->GetSubMesh(i);

				//glm::vec3 translation = VEC3_ZERO;
				//glm::quat rotation = QUAT_IDENTITY;
				//glm::vec3 scale = VEC3_ONE;

				//if (!drawInfo.bRaw)
				//{
				//	if (drawInfo.bScreenSpace)
				//	{
				//		glm::vec2 normalizedTranslation;
				//		glm::vec2 normalizedScale;
				//		NormalizeSpritePos(translation, drawInfo.anchor, scale, normalizedTranslation, normalizedScale);
				//
				//		translation = glm::vec3(normalizedTranslation, 0.0f);
				//		scale = glm::vec3(normalizedScale, 1.0f);
				//	}
				//}

				glm::mat4 model = MAT4_IDENTITY;
					//glm::translate(MAT4_IDENTITY, translation) *
					//glm::mat4(rotation) *
					//glm::scale(MAT4_IDENTITY, scale);

				u32 dynamicUBOOffset = i * m_DynamicAlignment;

				RenderID renderID = meshComponent->renderID;
				VulkanRenderObject* renderObject = GetRenderObject(renderID);

				GraphicsPipeline* graphicsPipeline = GetGraphicsPipeline(renderObject->graphicsPipelineID)->pipeline;
				VkPipeline pipeline = graphicsPipeline->pipeline;
				VkPipelineLayout pipelineLayout = graphicsPipeline->layout;

				//Material::PushConstantBlock* pushBlock = nullptr;
				//if (drawInfo.bScreenSpace)
				//{
				//	if (spriteShader->bTextureArr)
				//	{
				//		real r = aspectRatio;
				//		real t = 1.0f;
				//		m_SpriteOrthoArrPushConstBlock->SetData(MAT4_IDENTITY, glm::ortho(-r, r, -t, t), drawInfo.textureLayer);
				//
				//		pushBlock = m_SpriteOrthoArrPushConstBlock;
				//
				//		graphicsPipeline = GetGraphicsPipeline(m_SpriteArrGraphicsPipelineID)->pipeline;
				//		pipeline = graphicsPipeline->pipeline;
				//		pipelineLayout = graphicsPipeline->layout;
				//	}
				//	else
				//	{
				//		pushBlock = m_SpriteOrthoPushConstBlock;
				//	}
				//}
				//else
				//{
				//	pushBlock = m_SpritePerspPushConstBlock;
				//}
				//
				//vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushBlock->size, pushBlock->data);

				UniformOverrides overrides = {};
				overrides.colourMultiplier = VEC4_ONE;
				overrides.overridenUniforms.AddUniform(U_COLOUR_MULTIPLIER);
				overrides.uvBlendAmount = uiMesh->GetUVBlendAmount(i);
				overrides.overridenUniforms.AddUniform(U_UV_BLEND_AMOUNT);
				UpdateDynamicUniformBuffer(matID, dynamicUBOOffset, model, &overrides);

				vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer->m_Buffer, offsets);
				vkCmdBindIndexBuffer(commandBuffer, indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

				BindDescriptorSet(uiMat, dynamicUBOOffset, commandBuffer, pipelineLayout, descSet);

				vkCmdDrawIndexed(commandBuffer, (u32)renderObject->indices->size(), 1, renderObject->indexOffset, renderObject->vertexOffset, 0);
			}
		}

		void VulkanRenderer::DrawParticles(VkCommandBuffer commandBuffer)
		{
			if (!m_ParticleSystems.empty())
			{
				BeginDebugMarkerRegion(commandBuffer, "Particles");

				for (VulkanParticleSystem* particleSystem : m_ParticleSystems)
				{
					if (!particleSystem || !particleSystem->system->IsVisible())
					{
						continue;
					}

					VulkanMaterial* particleSimMat = (VulkanMaterial*)m_Materials.at(particleSystem->system->simMaterialID);
					VulkanMaterial* particleRenderingMat = (VulkanMaterial*)m_Materials.at(particleSystem->system->renderingMaterialID);

					GraphicsPipeline* pipeline = GetGraphicsPipeline(particleSystem->graphicsPipelineID)->pipeline;

					u32 dynamicUBOOffset = particleSystem->ID * m_DynamicAlignment;
					UpdateDynamicUniformBuffer(particleSystem->system->renderingMaterialID, dynamicUBOOffset, particleSystem->system->model, nullptr);

					VkDeviceSize offsets[1] = { 0 };
					const VkBuffer* particleBuffer = &particleSimMat->uniformBufferList.Get(UniformBufferType::PARTICLE_DATA)->buffer.m_Buffer;
					vkCmdBindVertexBuffers(commandBuffer, 0, 1, particleBuffer, offsets);

					vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);

					BindDescriptorSet(particleRenderingMat, dynamicUBOOffset, commandBuffer, pipeline->layout, particleSystem->renderingDescriptorSet);

					vkCmdDraw(commandBuffer, MAX_PARTICLE_COUNT, 1, 0, 0);
				}

				EndDebugMarkerRegion(commandBuffer, "End Particles");
			}
		}

		VkDescriptorSet VulkanRenderer::GetSpriteDescriptorSet(TextureID textureID, MaterialID spriteMaterialID, u32 textureLayer)
		{
			VkDescriptorSet descSet = VK_NULL_HANDLE;
			if (m_SpriteDescSets.find(textureID) != m_SpriteDescSets.end())
			{
				descSet = m_SpriteDescSets[textureID].descSet;
			}
			else
			{
				descSet = CreateSpriteDescSet(spriteMaterialID, textureID, textureLayer);
				m_SpriteDescSets.insert({ textureID, { spriteMaterialID, descSet, textureLayer } });
			}
			return descSet;
		}

		VkRenderPass VulkanRenderer::ResolveRenderPassType(RenderPassType renderPassType, const char* shaderName /* = nullptr */)
		{
			switch (renderPassType)
			{
			case RenderPassType::SHADOW: return *m_ShadowRenderPass;
			case RenderPassType::DEFERRED: return *m_DeferredRenderPass;
			case RenderPassType::DEFERRED_COMBINE: return *m_DeferredCombineRenderPass;
			case RenderPassType::FORWARD: return *m_ForwardRenderPass;
			case RenderPassType::SSAO: return *m_SSAORenderPass;
			case RenderPassType::SSAO_BLUR: return *m_SSAOBlurHRenderPass;
			case RenderPassType::POST_PROCESS: return *m_PostProcessRenderPass;
			case RenderPassType::TAA_RESOLVE: return *m_TAAResolveRenderPass;
			case RenderPassType::GAMMA_CORRECT: return *m_GammaCorrectRenderPass;
			case RenderPassType::UI: return *m_UIRenderPass;
			case RenderPassType::COMPUTE_PARTICLES: return VK_NULL_HANDLE;
			default:
				PrintError("Shader's render pass type was not set!\n %s", shaderName ? shaderName : "");
				ENSURE_NO_ENTRY();
			}
			return VK_NULL_HANDLE;
		}

		void VulkanRenderer::CreateShadowResources()
		{
			// Shadow map pipeline
			VulkanMaterial* shadowMaterial = (VulkanMaterial*)m_Materials[m_ShadowMaterialID];
			VulkanShader* shadowShader = (VulkanShader*)m_Shaders[shadowMaterial->shaderID];

			VkPushConstantRange pushConstantRange = {};
			pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			pushConstantRange.offset = 0;
			pushConstantRange.size = shadowShader->pushConstantBlockSize;

			GraphicsPipelineCreateInfo pipelineCreateInfo = {};
			pipelineCreateInfo.DBG_Name = "Shadow pipeline";
			pipelineCreateInfo.bSetDynamicStates = true;
			pipelineCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			pipelineCreateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
			pipelineCreateInfo.bEnableColourBlending = false;
			pipelineCreateInfo.shaderID = shadowMaterial->shaderID;
			pipelineCreateInfo.vertexAttributes = shadowShader->vertexAttributes;
			pipelineCreateInfo.descriptorSetLayoutIndex = shadowMaterial->descriptorSetLayoutIndex;
			pipelineCreateInfo.subpass = shadowShader->subpass;
			pipelineCreateInfo.depthWriteEnable = shadowShader->bDepthWriteEnable ? VK_TRUE : VK_FALSE;
			pipelineCreateInfo.renderPass = shadowShader->renderPass;
			pipelineCreateInfo.pushConstantRangeCount = 1;
			pipelineCreateInfo.pushConstants = &pushConstantRange;
			pipelineCreateInfo.bPersistent = true;
			CreateGraphicsPipeline(&pipelineCreateInfo, m_ShadowGraphicsPipelineID);

			// Shadow map descriptor set
			VkDescriptorSetLayout descSetLayout = m_DescriptorPoolPersistent->descriptorSetLayouts[shadowMaterial->shaderID];

			DescriptorSetCreateInfo descSetCreateInfo = {};
			descSetCreateInfo.DBG_Name = "Shadow descriptor set";
			descSetCreateInfo.descriptorSetLayout = &descSetLayout;
			descSetCreateInfo.shaderID = shadowMaterial->shaderID;
			descSetCreateInfo.uniformBufferList = &shadowMaterial->uniformBufferList;
			FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
			m_ShadowDescriptorSet = m_DescriptorPoolPersistent->CreateDescriptorSet(&descSetCreateInfo);
		}

		VkDescriptorSet VulkanRenderer::CreateSpriteDescSet(MaterialID spriteMaterialID, TextureID textureID, u32 layer /* = 0 */)
		{
			assert(textureID != InvalidTextureID);

			VulkanMaterial* spriteMat = (VulkanMaterial*)m_Materials.at(spriteMaterialID);
			VulkanShader* spriteShader = (VulkanShader*)m_Shaders[spriteMat->shaderID];

			DescriptorSetCreateInfo descSetCreateInfo = {};
			descSetCreateInfo.DBG_Name = "Sprite descriptor set";
			descSetCreateInfo.descriptorSetLayout = &m_DescriptorPoolPersistent->descriptorSetLayouts[spriteMat->shaderID];
			descSetCreateInfo.shaderID = spriteMat->shaderID;
			descSetCreateInfo.uniformBufferList = &spriteMat->uniformBufferList;
			if (spriteShader->bTextureArr)
			{
				descSetCreateInfo.imageDescriptors.SetUniform(U_ALBEDO_SAMPLER, ImageDescriptorInfo{ m_ShadowCascades[layer]->imageView, m_LinMipLinSampler });
			}
			else
			{
				VulkanTexture* texture = (VulkanTexture*)g_ResourceManager->GetLoadedTexture(textureID);
				descSetCreateInfo.imageDescriptors.SetUniform(U_ALBEDO_SAMPLER, ImageDescriptorInfo{ texture->imageView, texture->sampler });
			}
			FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
			VkDescriptorSet descSet = m_DescriptorPoolPersistent->CreateDescriptorSet(&descSetCreateInfo);

			return descSet;
		}

		void VulkanRenderer::InitializeAllParticleSystemBuffers()
		{
			for (VulkanParticleSystem* particleSystem : m_ParticleSystems)
			{
				if (particleSystem != nullptr)
				{
					InitializeParticleSystemBuffer(particleSystem);
				}
			}
		}

		void VulkanRenderer::InitializeParticleSystemBuffer(VulkanParticleSystem* particleSystem)
		{
			const u32 maxParticleBufferSize = MAX_PARTICLE_COUNT * sizeof(ParticleBufferData);

			VulkanBuffer stagingBuffer(m_VulkanDevice);
			stagingBuffer.Create(
				maxParticleBufferSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			std::vector<ParticleBufferData> particleBufferData(MAX_PARTICLE_COUNT);

			const u32 dim = (u32)glm::pow((real)particleBufferData.size(), 1.0f / 3.0f);
			const real invDim = 1.0f / (real)dim;
			for (u32 i = 0; i < particleSystem->system->data.particleCount; ++i)
			{
				real x = (i % dim) * invDim - 0.5f;
				real y = (i / dim % dim) * invDim - 0.5f;
				real z = (i / (dim * dim) % dim) * invDim - 0.5f;

				real mag = glm::length(glm::vec3(x, y, z));

				real dx = x * 2.0f;
				real dz = z * 2.0f;

				real theta = atan2(dz, dx) - PI_DIV_TWO;

				particleBufferData[i].pos = glm::vec3(x, y, z) * particleSystem->system->scale;
				particleBufferData[i].colour = glm::vec4(1.0f);
				particleBufferData[i].vel = glm::vec3(cos(theta), 0.0f, sin(theta)) * mag;
				particleBufferData[i].extraVec4 = glm::vec4(Lerp(0.6f, 0.2f, mag), 0.0f, 0.0f, 0.0f);
			}

			VulkanMaterial* particleSimMat = (VulkanMaterial*)m_Materials.at(particleSystem->system->simMaterialID);

			const u32 particleBufferSize = particleSystem->system->data.particleCount * sizeof(particleBufferData[0]);

			VK_CHECK_RESULT(stagingBuffer.Map());
			memcpy(stagingBuffer.m_Mapped, particleBufferData.data(), particleBufferSize);
			memset(((u8*)stagingBuffer.m_Mapped) + particleBufferSize, 0, MAX_PARTICLE_COUNT * sizeof(ParticleBufferData) - particleBufferSize);
			stagingBuffer.Unmap();

			UniformBuffer* particleBuffer = particleSimMat->uniformBufferList.Get(UniformBufferType::PARTICLE_DATA);
			CopyBuffer(m_VulkanDevice, m_GraphicsQueue, stagingBuffer.m_Buffer, particleBuffer->buffer.m_Buffer, particleBuffer->buffer.m_Size);
		}

		u32 VulkanRenderer::GetStaticVertexIndexBufferIndex(u32 stride)
		{
			if (stride == 0)
			{
				return u32_max;
			}

			for (u32 bufferIndex = 0; bufferIndex < m_StaticVertexBuffers.size(); ++bufferIndex)
			{
				if (m_StaticVertexBuffers[bufferIndex].first == stride)
				{
					return bufferIndex;
				}
			}

			m_StaticVertexBuffers.emplace_back(stride, new VulkanBuffer(m_VulkanDevice));
			return (u32)m_StaticVertexBuffers.size() - 1;
		}

		u32 VulkanRenderer::GetDynamicVertexIndexBufferIndex(u32 stride)
		{
			if (stride == 0)
			{
				return u32_max;
			}

			for (u32 bufferIndex = 0; bufferIndex < m_DynamicVertexIndexBufferPairs.size(); ++bufferIndex)
			{
				if (m_DynamicVertexIndexBufferPairs[bufferIndex].first == stride)
				{
					return bufferIndex;
				}
			}

			m_DynamicVertexIndexBufferPairs.emplace_back(stride, new VertexIndexBufferPair(new VulkanBuffer(m_VulkanDevice), new VulkanBuffer(m_VulkanDevice)));
			return (u32)m_DynamicVertexIndexBufferPairs.size() - 1;
		}

		void VulkanRenderer::UpdateShaderMaxObjectCount(ShaderID shaderID, i32 newMax)
		{
			VulkanShader* shader = (VulkanShader*)GetShader(shaderID);

			i32 oldMax = (i32)shader->maxObjectCount;
			shader->maxObjectCount = newMax;
			Print("Growing dynamic UBO max object count of all materials who use shader \"%s\" from %i to %i\n", shader->name.c_str(), oldMax, shader->maxObjectCount);
#if 1
			// Efficient:
			// Any materials that use this shader need their buffers grown too
			// otherwise we'll think they have new max object count worth of space
			for (auto& materialPair : m_Materials)
			{
				if (materialPair.second->shaderID == shaderID)
				{
					CreateDynamicUniformBuffer((VulkanMaterial*)materialPair.second);
				}
			}

			for (u32 i = 0; i < (u32)m_RenderObjects.size(); ++i)
			{
				if (m_RenderObjects[i] != nullptr)
				{
					Material* objMat = GetMaterial(m_RenderObjects[i]->materialID);
					if (objMat->shaderID == shaderID)
					{
						CreateGraphicsPipeline(i);
						m_DescriptorPool->CreateDescriptorSet(m_RenderObjects[i]->materialID);
					}
				}
			}
#else
			// Naive:
			for (auto& materialPair : m_Materials)
			{
				CreateDynamicUniformBuffer((VulkanMaterial*)materialPair.second);
			}

			for (u32 i = 0; i < (u32)m_RenderObjects.size(); ++i)
			{
				if (m_RenderObjects[i] != nullptr)
				{
					CreateGraphicsPipeline(i);
				}
			}
#endif

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

			return (u32)m_RenderObjects.size();
		}

		ParticleSystemID VulkanRenderer::GetNextAvailableParticleSystemID() const
		{
			for (u32 i = 0; i < m_ParticleSystems.size(); ++i)
			{
				if (m_ParticleSystems[i] == nullptr)
				{
					return (ParticleSystemID)i;
				}
			}

			return (u32)m_ParticleSystems.size();
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
				m_RenderObjects.emplace_back(renderObject);
			}

			assert(m_RenderObjects[renderObject->renderID] == renderObject);
		}

		void VulkanRenderer::InsertNewParticleSystem(VulkanParticleSystem* particleSystem)
		{
			if (particleSystem->ID < m_ParticleSystems.size())
			{
				assert(m_ParticleSystems[particleSystem->ID] == nullptr);
				m_ParticleSystems[particleSystem->ID] = particleSystem;
			}
			else
			{
				m_ParticleSystems.emplace_back(particleSystem);
			}

			assert(m_ParticleSystems[particleSystem->ID] == particleSystem);
		}

		void VulkanRenderer::CreateInstance()
		{
			assert(m_Instance == VK_NULL_HANDLE);

			const u32 requestedVkVersion = VK_MAKE_VERSION(1, 2, 0);

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

			std::vector<const char*> extensions = GetRequiredInstanceExtensions();

			// Optional instance extensions
			{
				u32 instanceExtensionCount;
				vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, nullptr);

				m_SupportedInstanceExtensions.resize(instanceExtensionCount);
				vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, m_SupportedInstanceExtensions.data());

				for (const char* optionalExtName : m_OptionalInstanceExtensions)
				{
					for (VkExtensionProperties& properties : m_SupportedInstanceExtensions)
					{
						if (strcmp(properties.extensionName, optionalExtName) == 0)
						{
							extensions.push_back(optionalExtName);
						}
					}
				}
			}

			m_EnabledInstanceExtensions = extensions;

			createInfo.enabledExtensionCount = (u32)extensions.size();
			createInfo.ppEnabledExtensionNames = extensions.data();

			if (m_bEnableValidationLayers)
			{
				createInfo.enabledLayerCount = (u32)m_ValidationLayers.size();
				createInfo.ppEnabledLayerNames = m_ValidationLayers.data();
			}
			else
			{
				createInfo.enabledLayerCount = 0;
			}

			std::vector<VkValidationFeatureEnableEXT> validationFeatureEnables;
			if (m_bEnableGPUAssistanceValidationFeature)
			{
				validationFeatureEnables.push_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);

			}
			if (m_bEnableBestPracticesValidationFeature)
			{
				validationFeatureEnables.push_back(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
			}
			VkValidationFeaturesEXT features = {};
			features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
			features.enabledValidationFeatureCount = (u32)validationFeatureEnables.size();
			features.pEnabledValidationFeatures = validationFeatureEnables.data();
			createInfo.pNext = &features;

			// TODO: PERFORMANCE: Call on separate thread? (taking 10% of bootup time!)
			VK_CHECK_RESULT(vkCreateInstance(&createInfo, nullptr, &m_Instance));

			volkLoadInstance(m_Instance);

			if (m_bEnableValidationLayers && !CheckValidationLayerSupport())
			{
				PrintError("Validation layers requested, but not available!");
			}
		}

		void VulkanRenderer::SetupDebugCallback()
		{
			if (!m_bEnableValidationLayers)
			{
				return;
			}

			VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
			createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			createInfo.flags = 0;
			createInfo.pfnUserCallback = DebugCallback;

			VK_CHECK_RESULT(vkCreateDebugUtilsMessengerEXT(m_Instance, &createInfo, nullptr, &m_DebugUtilsMessengerCallback));
		}

		void VulkanRenderer::CreateSurface()
		{
			assert(m_Surface == VK_NULL_HANDLE);
			VK_CHECK_RESULT(glfwCreateWindowSurface(m_Instance, static_cast<GLFWWindowWrapper*>(g_Window)->GetWindow(), nullptr, &m_Surface));
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
				PrintError("Failed to find GPUs with Vulkan support!\n");
				ENSURE_NO_ENTRY();
				return VK_NULL_HANDLE;
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
				PrintError("Failed to find a suitable GPU which supports all required extensions\n");
				return VK_NULL_HANDLE;
			}

			return physicalDevice;
		}

		void VulkanRenderer::CreateSwapChain()
		{
			m_bSwapChainNeedsRebuilding = false;

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

			VulkanQueueFamilyIndices indices = m_VulkanDevice->m_QueueFamilyIndices;
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

			for (u32 i = 0; i < m_SwapChainFramebuffers.size(); ++i)
			{
				m_SwapChainFramebuffers[i]->width = m_SwapChainExtent.width;
				m_SwapChainFramebuffers[i]->height = m_SwapChainExtent.height;
			}

			SetSwapchainName(m_VulkanDevice, m_SwapChain, "Default Swapchain");
		}

		void VulkanRenderer::CreateSwapChainImageViews()
		{
			for (u32 i = 0; i < m_SwapChainFramebufferAttachments.size(); ++i)
			{
				delete m_SwapChainFramebufferAttachments[i];
			}
			m_SwapChainFramebufferAttachments.clear();

			m_SwapChainImageViews.resize(m_SwapChainImages.size(), VDeleter<VkImageView>{ m_VulkanDevice->m_LogicalDevice, vkDestroyImageView });
			m_SwapChainFramebufferAttachments.resize(m_SwapChainImages.size());

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

				FrameBufferAttachment::CreateInfo attachmentCreateInfo = {};
				attachmentCreateInfo.width = m_SwapChainExtent.width;
				attachmentCreateInfo.height = m_SwapChainExtent.height;
				attachmentCreateInfo.format = m_SwapChainImageFormat;
				m_SwapChainFramebufferAttachments[i] = new FrameBufferAttachment(m_VulkanDevice, attachmentCreateInfo);
				m_SwapChainFramebufferAttachments[i]->bOwnsResources = false;
				m_SwapChainFramebufferAttachments[i]->image = m_SwapChainImages[i];
				m_SwapChainFramebufferAttachments[i]->view = m_SwapChainImageViews[i];
			}
		}

		void VulkanRenderer::CreateRenderPasses()
		{
			//
			// See `FlexEngine/screenshots/FlexRenderPasses_2019-10-02.jpg` for
			// a detailed breakdown of render passes and their dependencies
			//

			m_ShadowRenderPass->RegisterForDepthOnly("Shadow render pass",
				SHADOW_CASCADE_DEPTH_ATTACHMENT_ID, // Target depth attachment
				{} // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_ShadowRenderPass);

			m_DeferredRenderPass->RegisterForMultiColourAndDepth("Deferred render pass",
				{ m_GBufferColourAttachment0->ID, m_GBufferColourAttachment1->ID }, // Target colour attachments
				m_GBufferDepthAttachment->ID, // Target depth attachment
				{} // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_DeferredRenderPass);

			m_SSAORenderPass->RegisterForColourOnly("SSAO render pass",
				m_SSAOFBColourAttachment0->ID, // Target colour attachment
				{ m_GBufferDepthAttachment->ID } // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_SSAORenderPass);

			m_SSAOBlurHRenderPass->RegisterForColourOnly("SSAO Blur Horizontal render pass",
				m_SSAOBlurHFBColourAttachment0->ID, // Target colour attachment
				{ m_SSAOFBColourAttachment0->ID } // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_SSAOBlurHRenderPass);

			m_SSAOBlurVRenderPass->RegisterForColourOnly("SSAO Blur Vertical render pass",
				m_SSAOBlurVFBColourAttachment0->ID, // Target colour attachment
				{ m_SSAOBlurHFBColourAttachment0->ID } // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_SSAOBlurVRenderPass);

			m_DeferredCombineRenderPass->RegisterForColourAndDepth("Deferred combine render pass",
				m_OffscreenFB0ColourAttachment0->ID, // Target colour attachment
				m_OffscreenFB0DepthAttachment->ID,  // Target depth attachment
				{ SHADOW_CASCADE_DEPTH_ATTACHMENT_ID, m_GBufferColourAttachment0->ID, m_GBufferColourAttachment1->ID, m_GBufferDepthAttachment->ID, m_SSAOBlurVFBColourAttachment0->ID } // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_DeferredCombineRenderPass);

			// TODO: Denote that GBuffer depth into copied (blit) into Offscreen 0 depth here

			m_ForwardRenderPass->RegisterForColourAndDepth("Forward render pass",
				m_OffscreenFB0ColourAttachment0->ID, // Target colour attachment
				m_OffscreenFB0DepthAttachment->ID, // Target depth attachment
				{} // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_ForwardRenderPass);

			m_PostProcessRenderPass->RegisterForColourAndDepth("Post Process render pass",
				m_OffscreenFB1ColourAttachment0->ID, // Target colour attachment
				m_OffscreenFB1DepthAttachment->ID, // Target depth attachment
				{ m_OffscreenFB0ColourAttachment0->ID } // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_PostProcessRenderPass);

			m_GammaCorrectRenderPass->RegisterForColourAndDepth("Gamma correct render pass",
				m_OffscreenFB0ColourAttachment0->ID, // Target colour attachment
				m_OffscreenFB0DepthAttachment->ID, // Target depth attachment
				{ m_OffscreenFB1ColourAttachment0->ID } // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_GammaCorrectRenderPass);

			m_TAAResolveRenderPass->RegisterForColourAndDepth("TAA Resolve render pass",
				m_OffscreenFB1ColourAttachment0->ID, // Target colour attachment
				m_OffscreenFB1DepthAttachment->ID, // Target depth attachment
				{ m_OffscreenFB0ColourAttachment0->ID, m_OffscreenFB0DepthAttachment->ID } // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_TAAResolveRenderPass);

			// TODO: Denote that history buffer is copied into from swap chain
			// TODO: Denote that swap chain is copied into from m_OffscreenFB0ColourAttachment0
			//m_AutoTransitionedRenderPasses.push_back(CopyOperation(SWAP_CHAIN_COLOUR_ATTACHMENT_ID, m_OffscreenFB1ColourAttachment0->ID));

			m_UIRenderPass->RegisterForColourAndDepth("UI render pass",
				SWAP_CHAIN_COLOUR_ATTACHMENT_ID, // Target colour attachment
				SWAP_CHAIN_DEPTH_ATTACHMENT_ID, // Target depth attachment
				{ m_OffscreenFB1ColourAttachment0->ID } // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_UIRenderPass);

			CalculateAutoLayoutTransitions();

			// --------------------------------------------

			struct TEMP_RenderPassImageLayouts
			{
				std::vector<VkImageLayout> colourInitialLayouts;
				std::vector<VkImageLayout> colourFinalLayouts;
				VkImageLayout depthInitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				VkImageLayout depthFinalLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			};
			std::vector<TEMP_RenderPassImageLayouts> autoGeneratedLayouts(m_AutoTransitionedRenderPasses.size());
			for (i32 passIndex = 0; passIndex < (i32)m_AutoTransitionedRenderPasses.size(); ++passIndex)
			{
				VulkanRenderPass* pass = m_AutoTransitionedRenderPasses[passIndex];
				assert(pass->m_TargetColourAttachmentInitialLayouts.size() == pass->m_TargetColourAttachmentFinalLayouts.size());
				const u32 colourAttachmentCount = (u32)pass->m_TargetColourAttachmentInitialLayouts.size();
				autoGeneratedLayouts[passIndex].colourInitialLayouts.resize(colourAttachmentCount, VK_IMAGE_LAYOUT_UNDEFINED);
				autoGeneratedLayouts[passIndex].colourFinalLayouts.resize(colourAttachmentCount, VK_IMAGE_LAYOUT_UNDEFINED);
				for (u32 attachmentIndex = 0; attachmentIndex < colourAttachmentCount; ++attachmentIndex)
				{
					autoGeneratedLayouts[passIndex].colourInitialLayouts[attachmentIndex] = pass->m_TargetColourAttachmentInitialLayouts[attachmentIndex];
					autoGeneratedLayouts[passIndex].colourFinalLayouts[attachmentIndex] = pass->m_TargetColourAttachmentFinalLayouts[attachmentIndex];
				}
				autoGeneratedLayouts[passIndex].depthInitialLayout = pass->m_TargetDepthAttachmentInitialLayout;
				autoGeneratedLayouts[passIndex].depthFinalLayout = pass->m_TargetDepthAttachmentFinalLayout;
			}

			// --------------------------------------------

			// TODO: Remove these at some point
			m_ShadowRenderPass->ManuallySpecifyLayouts({}, {}, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED);
			m_DeferredRenderPass->ManuallySpecifyLayouts({ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, { VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED }, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED);
			m_SSAORenderPass->ManuallySpecifyLayouts({ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, { VK_IMAGE_LAYOUT_UNDEFINED });
			m_SSAOBlurHRenderPass->ManuallySpecifyLayouts({ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, { VK_IMAGE_LAYOUT_UNDEFINED });
			m_SSAOBlurVRenderPass->ManuallySpecifyLayouts({ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, { VK_IMAGE_LAYOUT_UNDEFINED });
			m_DeferredCombineRenderPass->ManuallySpecifyLayouts({ VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }, { VK_IMAGE_LAYOUT_UNDEFINED }, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED);
			m_ForwardRenderPass->ManuallySpecifyLayouts({ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, { VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			m_PostProcessRenderPass->ManuallySpecifyLayouts({ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, { VK_IMAGE_LAYOUT_UNDEFINED }, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED);
			m_GammaCorrectRenderPass->ManuallySpecifyLayouts({ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED);
			m_TAAResolveRenderPass->ManuallySpecifyLayouts({ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
			m_UIRenderPass->ManuallySpecifyLayouts({ VK_IMAGE_LAYOUT_PRESENT_SRC_KHR }, { VK_IMAGE_LAYOUT_UNDEFINED }, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED);

			m_ShadowRenderPass->bCreateFrameBuffer = false; // Uses special shadow frame buffer
			m_ShadowRenderPass->m_DepthAttachmentFormat = m_ShadowBufFormat;
			m_ShadowRenderPass->Create();
			m_DeferredRenderPass->Create();
			m_SSAORenderPass->Create();
			m_SSAOBlurHRenderPass->Create();
			m_SSAOBlurVRenderPass->Create();
			m_DeferredCombineRenderPass->Create();
			m_ForwardRenderPass->Create();
			m_PostProcessRenderPass->Create();
			m_GammaCorrectRenderPass->Create();
			m_TAAResolveRenderPass->Create();
			m_UIRenderPass->bCreateFrameBuffer = false; // Uses the swapchain frame buffer
			m_UIRenderPass->m_ColourAttachmentFormat = m_SwapChainImageFormat;
			m_UIRenderPass->m_DepthAttachmentFormat = m_DepthFormat;
			m_UIRenderPass->Create();

			// Writes to swapchain... TODO: manage swapchain FrameBuffer wrapper in this class and reference it from this pass (and any other that target the swapchain)
			m_UIRenderPass->m_FrameBuffer->width = m_SwapChainExtent.width;
			m_UIRenderPass->m_FrameBuffer->height = m_SwapChainExtent.height;

			const bool bPrintResultsDetailed = false;
			const bool bPrintResults = false;

			if (bPrintResultsDetailed)
			{
				PrintWarn("ColourInit ColourFinal - DepthInit DepthFinal\n");
			}

			i32 successCount = 0;
			for (i32 i = 0; i < (i32)m_AutoTransitionedRenderPasses.size(); ++i)
			{
				VulkanRenderPass* pass = m_AutoTransitionedRenderPasses[i];
				const TEMP_RenderPassImageLayouts& generatedPassLayouts = autoGeneratedLayouts[i];

				const bool bWritesToDepth = pass->m_DepthAttachmentFormat != VK_FORMAT_UNDEFINED;

				if (pass->m_TargetColourAttachmentInitialLayouts != generatedPassLayouts.colourInitialLayouts ||
					pass->m_TargetColourAttachmentFinalLayouts != generatedPassLayouts.colourFinalLayouts ||
					(bWritesToDepth &&
						(pass->m_TargetDepthAttachmentInitialLayout != generatedPassLayouts.depthInitialLayout ||
							pass->m_TargetDepthAttachmentFinalLayout != generatedPassLayouts.depthFinalLayout)))
				{
					if (bPrintResultsDetailed)
					{
						PrintWarn("Unexpected auto generated render pass image layout transitions in \"%s\":\n", pass->m_Name);

						PrintWarn("Actual:   ");
						if (generatedPassLayouts.colourInitialLayouts.size() > 1)
						{
							PrintWarn("{ ");
						}
						for (u32 attachmentIndex = 0; attachmentIndex < generatedPassLayouts.colourInitialLayouts.size(); ++attachmentIndex)
						{
							PrintWarn("%d ", generatedPassLayouts.colourInitialLayouts[attachmentIndex]);
						}
						if (generatedPassLayouts.colourInitialLayouts.size() > 1)
						{
							PrintWarn("} ");
						}
						if (generatedPassLayouts.colourFinalLayouts.size() > 1)
						{
							PrintWarn("{ ");
						}
						for (u32 attachmentIndex = 0; attachmentIndex < generatedPassLayouts.colourFinalLayouts.size(); ++attachmentIndex)
						{
							PrintWarn("%d ", generatedPassLayouts.colourFinalLayouts[attachmentIndex]);
						}
						if (generatedPassLayouts.colourFinalLayouts.size() > 1)
						{
							PrintWarn("} ");
						}
						if (bWritesToDepth)
						{
							PrintWarn("- %d %d\n", generatedPassLayouts.depthInitialLayout, generatedPassLayouts.depthFinalLayout);
						}
						else
						{
							PrintWarn("-\n");
						}

						PrintWarn("Expected: ");
						if (pass->m_TargetColourAttachmentInitialLayouts.size() > 1)
						{
							PrintWarn("{ ");
						}
						for (u32 attachmentIndex = 0; attachmentIndex < pass->m_TargetColourAttachmentInitialLayouts.size(); ++attachmentIndex)
						{
							PrintWarn("%d ", pass->m_TargetColourAttachmentInitialLayouts[attachmentIndex]);
						}
						if (pass->m_TargetColourAttachmentInitialLayouts.size() > 1)
						{
							PrintWarn("} ");
						}
						if (pass->m_TargetColourAttachmentFinalLayouts.size() > 1)
						{
							PrintWarn("{ ");
						}
						for (u32 attachmentIndex = 0; attachmentIndex < pass->m_TargetColourAttachmentFinalLayouts.size(); ++attachmentIndex)
						{
							PrintWarn("%d ", pass->m_TargetColourAttachmentFinalLayouts[attachmentIndex]);
						}
						if (pass->m_TargetColourAttachmentFinalLayouts.size() > 1)
						{
							PrintWarn("} ");
						}
						if (bWritesToDepth)
						{
							PrintWarn("- %d %d\n", pass->m_TargetDepthAttachmentInitialLayout, pass->m_TargetDepthAttachmentFinalLayout);
						}
						else
						{
							PrintWarn("-\n");
						}
					}
				}
				else
				{
					++successCount;

					if (bPrintResultsDetailed)
					{
						Print("Successful automatic render pass image layout transition calculation in %s!\n", pass->m_Name);
					}
				}
			}
			if (bPrintResults)
			{
				Print("Successful automatic transition calculations: %d/%u\n", successCount, (u32)m_AutoTransitionedRenderPasses.size());
			}
		}

		void VulkanRenderer::CalculateAutoLayoutTransitions()
		{
			// Handle passes which sample a FB which was previously written to
			for (i32 i = 1; i < (i32)m_AutoTransitionedRenderPasses.size(); ++i)
			{
				VulkanRenderPass* currPass = m_AutoTransitionedRenderPasses[i];

				std::vector<FrameBufferAttachmentID> unresolvedSampledAttachments(currPass->m_SampledAttachmentIDs);

				VulkanRenderPass* prevPass = nullptr;
				i32 prevPassIndex = i - 1;
				while (prevPassIndex >= 0 && !unresolvedSampledAttachments.empty())
				{
					prevPass = m_AutoTransitionedRenderPasses[prevPassIndex];

					if (!prevPass->m_TargetColourAttachmentIDs.empty())
					{
						auto sampledAttachmentIter = unresolvedSampledAttachments.begin();
						while (sampledAttachmentIter != unresolvedSampledAttachments.end())
						{
							bool bRemovedElement = false;
							std::vector<FrameBufferAttachmentID>::const_iterator targetAttachmentIter;
							do
							{
								targetAttachmentIter = FindIter(prevPass->m_TargetColourAttachmentIDs, *sampledAttachmentIter);
								if (targetAttachmentIter != prevPass->m_TargetColourAttachmentIDs.end())
								{
									const u32 targetAttachmentIndex = (u32)(targetAttachmentIter - prevPass->m_TargetColourAttachmentIDs.begin());
									prevPass->m_TargetColourAttachmentFinalLayouts[targetAttachmentIndex] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
									sampledAttachmentIter = unresolvedSampledAttachments.erase(sampledAttachmentIter);
									bRemovedElement = true;
									break;
								}
							} while (targetAttachmentIter != prevPass->m_TargetColourAttachmentIDs.end());

							if (sampledAttachmentIter == unresolvedSampledAttachments.end())
							{
								break;
							}

							if (!bRemovedElement)
							{
								++sampledAttachmentIter;
							}
						}
					}

					if (prevPass->m_TargetDepthAttachmentID != InvalidFrameBufferAttachmentID)
					{
						auto sampledAttachmentIter = FindIter(unresolvedSampledAttachments, prevPass->m_TargetDepthAttachmentID);
						if (sampledAttachmentIter != unresolvedSampledAttachments.end())
						{
							prevPass->m_TargetDepthAttachmentFinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
							unresolvedSampledAttachments.erase(sampledAttachmentIter);
						}
					}

					--prevPassIndex;
				}
			}

			// Handle passes which target FBs which were previously sampled
			for (i32 i = 0; i < (i32)m_AutoTransitionedRenderPasses.size() - 1; ++i)
			{
				VulkanRenderPass* currPass = m_AutoTransitionedRenderPasses[i];
				VulkanRenderPass* nextPass = m_AutoTransitionedRenderPasses[i + 1];

				std::vector<FrameBufferAttachmentID> unresolvedSampledAttachments(currPass->m_SampledAttachmentIDs);

				i32 nextPassIndex = i + 1;
				while (nextPassIndex < (i32)m_AutoTransitionedRenderPasses.size() && !unresolvedSampledAttachments.empty())
				{
					nextPass = m_AutoTransitionedRenderPasses[nextPassIndex];

					if (nextPass->m_TargetColourAttachmentIDs.size() > 0)
					{
						auto sampledAttachmentIter = unresolvedSampledAttachments.begin();
						while (sampledAttachmentIter != unresolvedSampledAttachments.end())
						{
							bool bRemovedElement = false;
							std::vector<FrameBufferAttachmentID>::const_iterator targetAttachmentIter;
							do
							{
								targetAttachmentIter = FindIter(nextPass->m_TargetColourAttachmentIDs, *sampledAttachmentIter);
								if (targetAttachmentIter != nextPass->m_TargetColourAttachmentIDs.end())
								{
									const u32 targetAttachmentIndex = (u32)(targetAttachmentIter - nextPass->m_TargetColourAttachmentIDs.begin());
									nextPass->m_TargetColourAttachmentInitialLayouts[targetAttachmentIndex] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
									sampledAttachmentIter = unresolvedSampledAttachments.erase(sampledAttachmentIter);
									bRemovedElement = true;
									break;
								}
							} while (targetAttachmentIter != nextPass->m_TargetColourAttachmentIDs.end());

							if (sampledAttachmentIter == unresolvedSampledAttachments.end())
							{
								break;
							}

							if (!bRemovedElement)
							{
								++sampledAttachmentIter;
							}
						}
					}

					if (nextPass->m_TargetDepthAttachmentID != InvalidFrameBufferAttachmentID)
					{
						auto sampledAttachmentIter = FindIter(unresolvedSampledAttachments, nextPass->m_TargetDepthAttachmentID);
						if (sampledAttachmentIter != unresolvedSampledAttachments.end())
						{
							nextPass->m_TargetDepthAttachmentInitialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
							unresolvedSampledAttachments.erase(sampledAttachmentIter);
						}
					}

					++nextPassIndex;
				}
			}

			// Handle passes which sequentially target the same FB
			for (i32 i = 0; i < (i32)m_AutoTransitionedRenderPasses.size() - 1; ++i)
			{
				VulkanRenderPass* currPass = m_AutoTransitionedRenderPasses[i];
				VulkanRenderPass* nextPass = m_AutoTransitionedRenderPasses[i + 1];

				for (auto nextPassTargetAttachmentIter = nextPass->m_TargetColourAttachmentIDs.begin(); nextPassTargetAttachmentIter != nextPass->m_TargetColourAttachmentIDs.end(); ++nextPassTargetAttachmentIter)
				{
					auto currPassTargetAttachmentIter = FindIter(currPass->m_TargetColourAttachmentIDs, *nextPassTargetAttachmentIter);
					if (currPassTargetAttachmentIter != currPass->m_TargetColourAttachmentIDs.end())
					{
						if (*nextPassTargetAttachmentIter == *currPassTargetAttachmentIter)
						{
							const size_t currPassTargetAttachmentIndex = currPassTargetAttachmentIter - currPass->m_TargetColourAttachmentIDs.begin();
							const size_t nextPassTargetAttachmentIndex = nextPassTargetAttachmentIter - nextPass->m_TargetColourAttachmentIDs.begin();
							currPass->m_TargetColourAttachmentFinalLayouts[currPassTargetAttachmentIndex] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
							nextPass->m_TargetColourAttachmentInitialLayouts[nextPassTargetAttachmentIndex] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						}
					}
				}
				if (nextPass->m_TargetDepthAttachmentID == currPass->m_TargetDepthAttachmentID)
				{
					currPass->m_TargetDepthAttachmentFinalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
					nextPass->m_TargetDepthAttachmentInitialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				}

				// Final pass must target swapchain
				VulkanRenderPass* finalPass = m_AutoTransitionedRenderPasses[m_AutoTransitionedRenderPasses.size() - 1];
				assert(finalPass->m_TargetColourAttachmentFinalLayouts.size() == 1);
				finalPass->m_TargetColourAttachmentFinalLayouts[0] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
				finalPass->m_TargetDepthAttachmentFinalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			}
		}

		void VulkanRenderer::FillOutImageDescriptorInfos(ShaderUniformContainer<ImageDescriptorInfo>* imageDescriptors, MaterialID materialID)
		{
			VulkanMaterial* material = (VulkanMaterial*)m_Materials.at(materialID);
			VulkanShader* shader = (VulkanShader*)m_Shaders[material->shaderID];

			for (const auto& texturePair : material->textures)
			{
				if (shader->textureUniforms.HasUniform(texturePair.uniformID))
				{
					VulkanTexture* texture = (VulkanTexture*)texturePair.object;
					assert(texture != nullptr);
					imageDescriptors->SetUniform(texturePair.uniformID, ImageDescriptorInfo{ texture->imageView, texture->sampler });
				}
			}

			if (shader->textureUniforms.HasUniform(U_DEPTH_SAMPLER))
			{
				imageDescriptors->SetUniform(U_DEPTH_SAMPLER, ImageDescriptorInfo{ m_GBufferDepthAttachment->view, m_DepthSampler });
			}

			if (shader->textureUniforms.HasUniform(U_SSAO_FINAL_SAMPLER))
			{
				VkImageView ssaoView = ((VulkanTexture*)m_BlankTexture)->imageView;
				if (m_SSAOSamplingData.enabled)
				{
					if (m_bSSAOBlurEnabled)
					{
						ssaoView = m_SSAOBlurVFBColourAttachment0->view;
					}
					else
					{
						ssaoView = m_SSAOFBColourAttachment0->view;
					}
				}
				imageDescriptors->SetUniform(U_SSAO_FINAL_SAMPLER, ImageDescriptorInfo{ ssaoView, m_NearestClampEdgeSampler });
			}

			if (shader->textureUniforms.HasUniform(U_SHADOW_SAMPLER))
			{
				VkImageView imageView = (m_DirectionalLight && m_DirectionalLight->data.castShadows) ? m_ShadowImageView : ((VulkanTexture*)m_BlankTextureArr)->imageView;
				imageDescriptors->SetUniform(U_SHADOW_SAMPLER, ImageDescriptorInfo{ imageView, m_DepthSampler });
			}

			if (shader->textureUniforms.HasUniform(U_FB_0_SAMPLER))
			{
				VkImageView imageView = *static_cast<VkImageView*>(material->sampledFrameBuffers[0].second);
				imageDescriptors->SetUniform(U_FB_0_SAMPLER, ImageDescriptorInfo{ imageView, m_LinMipLinSampler });
			}
			if (shader->textureUniforms.HasUniform(U_FB_1_SAMPLER))
			{
				VkImageView imageView = *static_cast<VkImageView*>(material->sampledFrameBuffers[1].second);
				imageDescriptors->SetUniform(U_FB_1_SAMPLER, ImageDescriptorInfo{ imageView, m_LinMipLinSampler });
			}

			if (shader->textureUniforms.HasUniform(U_LTC_SAMPLER_0))
			{
				assert(shader->textureUniforms.HasUniform(U_LTC_SAMPLER_1));
				VulkanTexture* ltcMatrices = (VulkanTexture*)g_ResourceManager->GetLoadedTexture(m_LTCMatricesID);
				VulkanTexture* ltcAmplitudes = (VulkanTexture*)g_ResourceManager->GetLoadedTexture(m_LTCAmplitudesID);
				imageDescriptors->SetUniform(U_LTC_SAMPLER_0, ImageDescriptorInfo{ ltcMatrices->imageView, ltcMatrices->sampler });
				imageDescriptors->SetUniform(U_LTC_SAMPLER_1, ImageDescriptorInfo{ ltcAmplitudes->imageView, ltcAmplitudes->sampler });
			}
		}

		void VulkanRenderer::FillOutBufferDescriptorInfos(ShaderUniformContainer<BufferDescriptorInfo>* descriptors, UniformBufferList const* uniformBufferList, ShaderID shaderID)
		{
			VulkanShader* shader = (VulkanShader*)m_Shaders[shaderID];

			if (shader->constantBufferUniforms.HasUniform(U_UNIFORM_BUFFER_CONSTANT))
			{
				UniformBuffer const* constantBuffer = uniformBufferList->Get(UniformBufferType::STATIC);
				descriptors->SetUniform(U_UNIFORM_BUFFER_CONSTANT, BufferDescriptorInfo{ constantBuffer->buffer.m_Buffer, constantBuffer->data.unitSize, UniformBufferType::STATIC });
			}

			if (shader->dynamicBufferUniforms.HasUniform(U_UNIFORM_BUFFER_DYNAMIC))
			{
				UniformBuffer const* dynamicBuffer = uniformBufferList->Get(UniformBufferType::DYNAMIC);
				descriptors->SetUniform(U_UNIFORM_BUFFER_DYNAMIC, BufferDescriptorInfo{ dynamicBuffer->buffer.m_Buffer, dynamicBuffer->data.unitSize, UniformBufferType::DYNAMIC });
			}

			if (shader->additionalBufferUniforms.HasUniform(U_PARTICLE_BUFFER))
			{
				UniformBuffer const* particleBuffer = uniformBufferList->Get(UniformBufferType::PARTICLE_DATA);
				descriptors->SetUniform(U_PARTICLE_BUFFER, BufferDescriptorInfo{ particleBuffer->buffer.m_Buffer, particleBuffer->data.unitSize, UniformBufferType::PARTICLE_DATA });
			}
		}

		void VulkanRenderer::CreateDescriptorSets()
		{
			for (auto iter = m_Materials.begin(); iter != m_Materials.end(); ++iter)
			{
				if (iter->second->persistent)
				{
					m_DescriptorPoolPersistent->CreateDescriptorSet(iter->first);
				}
				else
				{
					m_DescriptorPool->CreateDescriptorSet(iter->first);
				}
			}
		}

		MaterialID VulkanRenderer::GetRenderObjectMaterialID(RenderID renderID)
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
				if (matPair.second->visibleInEditor)
				{
					result.emplace_back(matPair.second->name, matPair.first);
				}
			}

			return result;
		}

		void VulkanRenderer::CreateGraphicsPipeline(RenderID renderID)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (renderObject == nullptr || renderObject->vertexBufferData == nullptr)
			{
				return;
			}

			VulkanMaterial* material = (VulkanMaterial*)m_Materials.at(renderObject->materialID);
			VulkanShader* shader = (VulkanShader*)m_Shaders[material->shaderID];

			GraphicsPipelineCreateInfo pipelineCreateInfo = {};
			char debugName[256];
			snprintf(debugName, 256, "Render Object %s (render ID %u) graphics pipeline", renderObject->gameObject ? renderObject->gameObject->GetName().c_str() : "", renderID);
			pipelineCreateInfo.DBG_Name = debugName;
			pipelineCreateInfo.shaderID = material->shaderID;
			pipelineCreateInfo.vertexAttributes = shader->vertexAttributes;
			pipelineCreateInfo.topology = renderObject->topology;
			pipelineCreateInfo.cullMode = renderObject->cullMode;
			pipelineCreateInfo.descriptorSetLayoutIndex = material->descriptorSetLayoutIndex;
			pipelineCreateInfo.bSetDynamicStates = renderObject->bSetDynamicStates;
			pipelineCreateInfo.bEnableColourBlending = shader->bTranslucent;
			pipelineCreateInfo.subpass = shader->subpass;
			pipelineCreateInfo.depthWriteEnable = shader->bDepthWriteEnable ? VK_TRUE : VK_FALSE;
			pipelineCreateInfo.depthCompareOp = renderObject->depthCompareOp;
			pipelineCreateInfo.renderPass = (renderObject->renderPassOverride == RenderPassType::_NONE ? shader->renderPass : ResolveRenderPassType(renderObject->renderPassOverride));

			VkPushConstantRange pushConstantRange = {};
			if (shader->bNeedPushConstantBlock)
			{
				pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
				pushConstantRange.offset = 0;
				pushConstantRange.size = material->pushConstantBlock->size;
				pipelineCreateInfo.pushConstantRangeCount = 1;
				pipelineCreateInfo.pushConstants = &pushConstantRange;
			}

			pipelineCreateInfo.fragSpecializationInfo = shader->fragSpecializationInfo;

			CreateGraphicsPipeline(&pipelineCreateInfo, renderObject->graphicsPipelineID);
		}

		void VulkanRenderer::CreateGraphicsPipeline(GraphicsPipelineCreateInfo* createInfo,
			GraphicsPipelineID& outPipelineID)
		{
			const u64 pipelineHash = createInfo->Hash();

			// Check for existing pipelines that are identical
			auto iter = m_GraphicsPipelineHashes.find(pipelineHash);
			if (iter != m_GraphicsPipelineHashes.end())
			{
				GraphicsPipelineID pipelineID = iter->second;
				GraphicsPipelineConfiguration* pipelineConfig = m_GraphicsPipelines[pipelineID];

				outPipelineID = pipelineConfig->pipelineID;

				++pipelineConfig->usageCount;

				// Set debug name on first sharing
				if (pipelineConfig->usageCount == 2)
				{
					char reusedDebugName[256];
					snprintf(reusedDebugName, 256, "Graphics pipeline ID %u", pipelineID);
					SetPipelineName(m_VulkanDevice, pipelineConfig->pipeline->pipeline, reusedDebugName);
				}

				return;
			}

			GraphicsPipeline* newPipeline = new GraphicsPipeline(m_VulkanDevice->m_LogicalDevice);

			VulkanShader* shader = (VulkanShader*)m_Shaders[createInfo->shaderID];

			std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

			const bool bUseVertexStage = !shader->vertexShaderFilePath.empty();
			const bool bUseFragmentStage = !shader->fragmentShaderFilePath.empty();
			const bool bUseGeometryStage = !shader->geometryShaderFilePath.empty();

			VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
			if (bUseVertexStage)
			{
				if ((VkShaderModule)shader->vertShaderModule == VK_NULL_HANDLE)
				{
					PrintError("Failed to create graphics pipeline, required vertex shader module is empty\n");
					delete newPipeline;
					return;
				}
				else
				{
					vertShaderStageInfo = vks::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, shader->vertShaderModule);
					shaderStages.push_back(vertShaderStageInfo);
				}
			}

			VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
			if (bUseFragmentStage)
			{
				if ((VkShaderModule)shader->fragShaderModule == VK_NULL_HANDLE)
				{
					PrintError("Failed to create graphics pipeline, required fragment shader module is empty\n");
					delete newPipeline;
					return;
				}
				else
				{
					fragShaderStageInfo = vks::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, shader->fragShaderModule);
					fragShaderStageInfo.pSpecializationInfo = createInfo->fragSpecializationInfo;
					shaderStages.push_back(fragShaderStageInfo);
				}
			}

			VkPipelineShaderStageCreateInfo geomShaderStageInfo = {};
			if (bUseGeometryStage)
			{
				if ((VkShaderModule)shader->geomShaderModule == VK_NULL_HANDLE)
				{
					PrintError("Failed to create graphics pipeline, required geometry shader module is empty\n");
					delete newPipeline;
					return;
				}
				else
				{
					geomShaderStageInfo = vks::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_GEOMETRY_BIT, shader->geomShaderModule);
					shaderStages.push_back(geomShaderStageInfo);
				}
			}

			const u32 vertexStride = CalculateVertexStride(createInfo->vertexAttributes);
			VkVertexInputBindingDescription bindingDescription = vks::vertexInputBindingDescription(0, vertexStride, VK_VERTEX_INPUT_RATE_VERTEX);

			std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
			GetVertexAttributeDescriptions(createInfo->vertexAttributes, attributeDescriptions);

			VkPipelineVertexInputStateCreateInfo vertexInputInfo;
			if (vertexStride > 0)
			{
				vertexInputInfo = vks::pipelineVertexInputStateCreateInfo(1, &bindingDescription, (u32)attributeDescriptions.size(), attributeDescriptions.data());
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

			std::vector<VkPipelineColorBlendAttachmentState> colourBlendAttachments = {};
			colourBlendAttachments.resize(shader->numAttachments, {});
			for (VkPipelineColorBlendAttachmentState& colorBlendAttachment : colourBlendAttachments)
			{
				colorBlendAttachment.colorWriteMask = 0xF; // RGBA

				if (createInfo->bEnableColourBlending)
				{
					colorBlendAttachment.blendEnable = VK_TRUE;
					colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
					colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
				}
				else if (createInfo->bEnableAdditiveColourBlending)
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

			VkPipelineColorBlendStateCreateInfo colorBlending = vks::pipelineColorBlendStateCreateInfo(colourBlendAttachments);
			colorBlending.logicOpEnable = VK_FALSE;
			colorBlending.logicOp = VK_LOGIC_OP_COPY;
			colorBlending.blendConstants[0] = 0.0f;
			colorBlending.blendConstants[1] = 0.0f;
			colorBlending.blendConstants[2] = 0.0f;
			colorBlending.blendConstants[3] = 0.0f;

			VkDescriptorSetLayout setLayout;
			if (createInfo->bPersistent)
			{
				setLayout = m_DescriptorPoolPersistent->descriptorSetLayouts[createInfo->descriptorSetLayoutIndex];
			}
			else
			{
				setLayout = m_DescriptorPool->descriptorSetLayouts[createInfo->descriptorSetLayoutIndex];
			}
			VkPipelineLayoutCreateInfo pipelineLayoutInfo = vks::pipelineLayoutCreateInfo(1, &setLayout);
			pipelineLayoutInfo.pushConstantRangeCount = createInfo->pushConstantRangeCount;
			pipelineLayoutInfo.pPushConstantRanges = createInfo->pushConstants;

			assert(createInfo->pushConstantRangeCount == 0 || createInfo->pushConstants != nullptr);

			//if (createInfo->pipelineCache != nullptr)
			//{
			//	vkDestroyPipelineCache(m_VulkanDevice->m_LogicalDevice, *createInfo->pipelineCache, nullptr);
			//
			//	VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
			//	pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
			//	VK_CHECK_RESULT(vkCreatePipelineCache(m_VulkanDevice->m_LogicalDevice, &pipelineCacheCreateInfo, nullptr, createInfo->pipelineCache));
			//}

			VK_CHECK_RESULT(vkCreatePipelineLayout(m_VulkanDevice->m_LogicalDevice, &pipelineLayoutInfo, nullptr, newPipeline->layout.replace()));

			VkPipelineDepthStencilStateCreateInfo depthStencil = vks::pipelineDepthStencilStateCreateInfo(createInfo->depthTestEnable, createInfo->depthWriteEnable, createInfo->depthCompareOp, createInfo->stencilTestEnable);

			VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dynamicState = vks::pipelineDynamicStateCreateInfo(ARRAY_LENGTH(dynamicStates), dynamicStates);
			VkPipelineDynamicStateCreateInfo* pDynamicState = nullptr;
			if (createInfo->bSetDynamicStates)
			{
				pDynamicState = &dynamicState;
			}

			VkGraphicsPipelineCreateInfo pipelineInfo = {};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = (u32)shaderStages.size();
			pipelineInfo.pStages = shaderStages.data();
			pipelineInfo.pVertexInputState = &vertexInputInfo;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.layout = newPipeline->layout;
			pipelineInfo.renderPass = createInfo->renderPass;
			pipelineInfo.pDynamicState = pDynamicState;
			pipelineInfo.subpass = createInfo->subpass;
			pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

			VkPipelineCache pipelineCache = VK_NULL_HANDLE;

			VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_VulkanDevice->m_LogicalDevice, pipelineCache, 1, &pipelineInfo, nullptr, &newPipeline->pipeline));
			SetPipelineName(m_VulkanDevice, newPipeline->pipeline, createInfo->DBG_Name);

			GraphicsPipelineID newPipelineID = (GraphicsPipelineID)m_LastGraphicsPipelineID++;
			bool bPersistent = createInfo->bPersistent;
			GraphicsPipelineConfiguration* newPipelineConfig = new GraphicsPipelineConfiguration(newPipelineID, newPipeline, bPersistent, createInfo->DBG_Name);
			m_GraphicsPipelines.emplace(newPipelineID, newPipelineConfig);
			m_GraphicsPipelineHashes.emplace(pipelineHash, newPipelineID);
			outPipelineID = newPipelineID;

			assert(m_GraphicsPipelineHashes.size() == m_GraphicsPipelines.size());
		}

		void VulkanRenderer::DestroyAllGraphicsPipelines()
		{
			for (auto& pipelineConfigPair : m_GraphicsPipelines)
			{
				GraphicsPipelineConfiguration* pipelineConfig = pipelineConfigPair.second;
				delete pipelineConfig;
			}
			m_GraphicsPipelineHashes.clear();
			m_GraphicsPipelines.clear();
			m_WireframeGraphicsPipelines.clear();
		}

		void VulkanRenderer::DestroyNonPersistentGraphicsPipelines()
		{
			auto iter = m_GraphicsPipelines.begin();
			while (iter != m_GraphicsPipelines.end())
			{
				GraphicsPipelineConfiguration* pipelineConfig = iter->second;
				if (!pipelineConfig->bPersistent)
				{
					GraphicsPipelineID pipelineID = pipelineConfig->pipelineID;

					delete pipelineConfig;
					iter = m_GraphicsPipelines.erase(iter);

					bool bFoundHash = false;
					for (auto iter2 = m_GraphicsPipelineHashes.begin(); iter2 != m_GraphicsPipelineHashes.end(); ++iter2)
					{
						if (iter2->second == pipelineID)
						{
							m_GraphicsPipelineHashes.erase(iter2);
							bFoundHash = true;
							break;
						}
					}

					for (auto iter3 = m_WireframeGraphicsPipelines.begin(); iter3 != m_WireframeGraphicsPipelines.end(); ++iter3)
					{
						if (iter3->second == pipelineID)
						{
							m_WireframeGraphicsPipelines.erase(iter3);
							break;
						}
					}

					assert(bFoundHash);
				}
				else
				{
					++iter;
				}
			}
		}

		void VulkanRenderer::DestroyGraphicsPipeline(GraphicsPipelineID pipelineID)
		{
			bool bFoundHash = false;
			bool bFoundPipeline = false;
			for (auto iter = m_GraphicsPipelineHashes.begin(); iter != m_GraphicsPipelineHashes.end(); ++iter)
			{
				if (iter->second == pipelineID)
				{
					m_GraphicsPipelineHashes.erase(iter);
					bFoundHash = true;
					break;
				}
			}

			for (auto iter = m_GraphicsPipelines.begin(); iter != m_GraphicsPipelines.end(); ++iter)
			{
				GraphicsPipelineConfiguration* pipelineConfig = iter->second;
				if (pipelineConfig->pipelineID == pipelineID)
				{
					// TODO: Look for usages when pipelineConfig->usageCount > 1
					delete pipelineConfig;
					m_GraphicsPipelines.erase(iter);
					bFoundPipeline = true;
					break;
				}
			}

			for (auto iter = m_WireframeGraphicsPipelines.begin(); iter != m_WireframeGraphicsPipelines.end(); ++iter)
			{
				if (iter->second == pipelineID)
				{
					m_WireframeGraphicsPipelines.erase(iter);
					break;
				}
			}

			assert(!bFoundHash || (bFoundHash && bFoundPipeline));
		}

		bool VulkanRenderer::IsGraphicsPipelineValid(GraphicsPipelineID pipelineID) const
		{
			GraphicsPipeline* pipeline = GetGraphicsPipeline(pipelineID)->pipeline;
			return pipeline != nullptr && pipeline->pipeline != nullptr;
		}

		GraphicsPipelineConfiguration* VulkanRenderer::GetGraphicsPipeline(GraphicsPipelineID pipelineID) const
		{
			if (pipelineID == InvalidGraphicsPipelineID)
			{
				return nullptr;
			}

			auto iter = m_GraphicsPipelines.find(pipelineID);
			if (iter != m_GraphicsPipelines.end())
			{
				return iter->second;
			}

			return nullptr;
		}

		void VulkanRenderer::CreateDepthResources()
		{
			VkCommandBuffer transitionCmdBuffer = BeginSingleTimeCommands(m_VulkanDevice);

			BeginDebugMarkerRegion(transitionCmdBuffer, "Create Depth Resources");

			m_GBufferCubemapDepthAttachment->width = m_CubemapFramebufferSize.x;
			m_GBufferCubemapDepthAttachment->height = m_CubemapFramebufferSize.y;

			m_SwapChainDepthAttachment->CreateImage(m_SwapChainExtent.width, m_SwapChainExtent.height, "Swap Chain Depth attachment");
			m_SwapChainDepthAttachment->CreateImageView("Swap Chain Depth image view");
			// Depth will be copied from offscreen depth buffer after deferred combine pass
			m_SwapChainDepthAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, m_GraphicsQueue, transitionCmdBuffer);
			RegisterFramebufferAttachment(m_SwapChainDepthAttachment);

			m_GBufferDepthAttachment->CreateImage(m_SwapChainExtent.width, m_SwapChainExtent.height, "GBuffer Depth attachment");
			m_GBufferDepthAttachment->CreateImageView("GBuffer Depth image view");
			m_GBufferDepthAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, m_GraphicsQueue, transitionCmdBuffer);
			RegisterFramebufferAttachment(m_GBufferDepthAttachment);

			m_OffscreenFB0DepthAttachment->bIsTransferedDst = true;
			m_OffscreenFB0DepthAttachment->CreateImage(m_SwapChainExtent.width, m_SwapChainExtent.height, "Offscreen Depth attachment 0");
			m_OffscreenFB0DepthAttachment->CreateImageView("Offscreen Depth 0 image view");
			m_OffscreenFB0DepthAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, m_GraphicsQueue, transitionCmdBuffer);
			RegisterFramebufferAttachment(m_OffscreenFB0DepthAttachment);

			//m_OffscreenDepthAttachment1->bIsTransferedDst = true;
			m_OffscreenFB1DepthAttachment->CreateImage(m_SwapChainExtent.width, m_SwapChainExtent.height, "Offscreen Depth attachment 1");
			m_OffscreenFB1DepthAttachment->CreateImageView("Offscreen Depth 1 image view");
			m_OffscreenFB1DepthAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, m_GraphicsQueue, transitionCmdBuffer);
			RegisterFramebufferAttachment(m_OffscreenFB1DepthAttachment);

			m_GBufferCubemapDepthAttachment->CreateImage(m_GBufferCubemapDepthAttachment->width, m_GBufferCubemapDepthAttachment->height, "Cube Depth attachment");
			m_GBufferCubemapDepthAttachment->CreateImageView("Cubemap Depth image view");
			m_GBufferCubemapDepthAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, m_GraphicsQueue, transitionCmdBuffer);
			RegisterFramebufferAttachment(m_GBufferCubemapDepthAttachment);

			EndDebugMarkerRegion(transitionCmdBuffer, "End Create Depth Resources");

			EndSingleTimeCommands(m_VulkanDevice, m_GraphicsQueue, transitionCmdBuffer);
		}

		void VulkanRenderer::CreateSwapChainFramebuffers()
		{
			for (u32 i = 0; i < m_SwapChainFramebuffers.size(); ++i)
			{
				delete m_SwapChainFramebuffers[i];
			}

			m_SwapChainFramebuffers.resize(m_SwapChainImageViews.size());

			for (u32 i = 0; i < m_SwapChainFramebuffers.size(); ++i)
			{
				m_SwapChainFramebuffers[i] = new FrameBuffer(m_VulkanDevice);
			}

			for (u32 i = 0; i < m_SwapChainImageViews.size(); ++i)
			{
				std::array<VkImageView, 2> attachments = {
					m_SwapChainImageViews[i],
					m_SwapChainDepthAttachment->view
				};

				VkFramebufferCreateInfo framebufferInfo = vks::framebufferCreateInfo(*m_UIRenderPass);
				framebufferInfo.attachmentCount = (u32)attachments.size();
				framebufferInfo.pAttachments = attachments.data();
				framebufferInfo.width = m_SwapChainExtent.width;
				framebufferInfo.height = m_SwapChainExtent.height;
				m_SwapChainFramebuffers[i]->width = m_SwapChainExtent.width;
				m_SwapChainFramebuffers[i]->height = m_SwapChainExtent.height;
				VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &framebufferInfo, nullptr, m_SwapChainFramebuffers[i]->Replace()));

				char name[256];
				snprintf(name, 256, "Swapchain %u", i);
				SetFramebufferName(m_VulkanDevice, m_SwapChainFramebuffers[i]->frameBuffer, name);
			}
		}

		void VulkanRenderer::CreateFrameBufferAttachments()
		{
			m_GBufferColourAttachment0->width = m_SwapChainExtent.width;
			m_GBufferColourAttachment0->height = m_SwapChainExtent.height;

			m_GBufferColourAttachment1->width = m_SwapChainExtent.width;
			m_GBufferColourAttachment1->height = m_SwapChainExtent.height;

			m_OffscreenFB0ColourAttachment0->width = m_SwapChainExtent.width;
			m_OffscreenFB0ColourAttachment0->height = m_SwapChainExtent.height;

			m_OffscreenFB1ColourAttachment0->width = m_SwapChainExtent.width;
			m_OffscreenFB1ColourAttachment0->height = m_SwapChainExtent.height;

			m_OffscreenFB0DepthAttachment->width = m_SwapChainExtent.width;
			m_OffscreenFB0DepthAttachment->height = m_SwapChainExtent.height;

			m_OffscreenFB1DepthAttachment->width = m_SwapChainExtent.width;
			m_OffscreenFB1DepthAttachment->height = m_SwapChainExtent.height;

			m_HistoryBuffer->width = m_SwapChainExtent.width;
			m_HistoryBuffer->height = m_SwapChainExtent.height;

			m_SSAORes = glm::vec2u(m_SwapChainExtent.width / 2, m_SwapChainExtent.height / 2);

			m_SSAOFBColourAttachment0->width = m_SSAORes.x;
			m_SSAOFBColourAttachment0->height = m_SSAORes.y;

			m_SSAOBlurHFBColourAttachment0->width = m_SwapChainExtent.width;
			m_SSAOBlurHFBColourAttachment0->height = m_SwapChainExtent.height;

			m_SSAOBlurVFBColourAttachment0->width = m_SwapChainExtent.width;
			m_SSAOBlurVFBColourAttachment0->height = m_SwapChainExtent.height;

			// GBuffer frame buffer attachments
			{
				CreateAttachment(m_VulkanDevice, m_GBufferColourAttachment0, "GBuffer image 0", "GBuffer image view 0");
				CreateAttachment(m_VulkanDevice, m_GBufferColourAttachment1, "GBuffer image 1", "GBuffer image view 1");
			}

			// Offscreen frame buffer attachments
			{
				assert(m_OffscreenFB0ColourAttachment0->width == m_OffscreenFB1ColourAttachment0->width);
				assert(m_OffscreenFB0ColourAttachment0->height == m_OffscreenFB1ColourAttachment0->height);

				CreateAttachment(m_VulkanDevice, m_OffscreenFB0ColourAttachment0, "Offscreen 0 image", "Offscreen 0 image view");
				CreateAttachment(m_VulkanDevice, m_OffscreenFB1ColourAttachment0, "Offscreen 1 image", "Offscreen 1 image view");

				// Deferred shading specifies initial layout of colour attachment optimal
				m_OffscreenFB0ColourAttachment0->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				// Post process specifies initial layout of colour attachment optimal
				m_OffscreenFB1ColourAttachment0->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
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

			// SSAO frame buffer
			{
				CreateAttachment(m_VulkanDevice, m_SSAOFBColourAttachment0, "SSAO image", "SSAO image view");
			}

			// SSAO Blur frame buffers
			{
				assert(m_SSAOBlurHFBColourAttachment0->width == m_SSAOBlurVFBColourAttachment0->width);
				assert(m_SSAOBlurHFBColourAttachment0->height == m_SSAOBlurVFBColourAttachment0->height);

				CreateAttachment(m_VulkanDevice, m_SSAOBlurHFBColourAttachment0, "SSAO Blur Horizontal image", "SSAO Blur Horizontal image view");
				CreateAttachment(m_VulkanDevice, m_SSAOBlurVFBColourAttachment0, "SSAO Blur Vertical image", "SSAO Blur Vertical image view");
			}

			VkSamplerCreateInfo nearestClampEdgeSamplerCreateInfo = vks::samplerCreateInfo();
			nearestClampEdgeSamplerCreateInfo.magFilter = VK_FILTER_NEAREST;
			nearestClampEdgeSamplerCreateInfo.minFilter = VK_FILTER_NEAREST;
			nearestClampEdgeSamplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			nearestClampEdgeSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			nearestClampEdgeSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			nearestClampEdgeSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			nearestClampEdgeSamplerCreateInfo.mipLodBias = 0.0f;
			nearestClampEdgeSamplerCreateInfo.minLod = 0.0f;
			nearestClampEdgeSamplerCreateInfo.maxLod = 1.0f;
			VK_CHECK_RESULT(vkCreateSampler(m_VulkanDevice->m_LogicalDevice, &nearestClampEdgeSamplerCreateInfo, nullptr, m_NearestClampEdgeSampler.replace()));
			SetSamplerName(m_VulkanDevice, m_NearestClampEdgeSampler, "Nearest clamp edge sampler");

			VkSamplerCreateInfo linMipLinSamplerCreateInfo = vks::samplerCreateInfo();
			linMipLinSamplerCreateInfo.magFilter = VK_FILTER_LINEAR;
			linMipLinSamplerCreateInfo.minFilter = VK_FILTER_LINEAR;
			linMipLinSamplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			linMipLinSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			linMipLinSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			linMipLinSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			linMipLinSamplerCreateInfo.mipLodBias = 0.0f;
			linMipLinSamplerCreateInfo.minLod = 0.0f;
			linMipLinSamplerCreateInfo.maxLod = 1.0f;
			linMipLinSamplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			VK_CHECK_RESULT(vkCreateSampler(m_VulkanDevice->m_LogicalDevice, &linMipLinSamplerCreateInfo, nullptr, &m_LinMipLinSampler));
			SetSamplerName(m_VulkanDevice, m_LinMipLinSampler, "Lin Mip Lin sampler");

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
			depthSamplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			VK_CHECK_RESULT(vkCreateSampler(m_VulkanDevice->m_LogicalDevice, &depthSamplerCreateInfo, nullptr, m_DepthSampler.replace()));
			SetSamplerName(m_VulkanDevice, m_DepthSampler, "Depth sampler");
		}

		// TODO: Test that this still works
		void VulkanRenderer::PrepareCubemapFrameBuffer()
		{
			m_GBufferCubemapColourAttachment0->width = m_CubemapFramebufferSize.x;
			m_GBufferCubemapColourAttachment0->height = m_CubemapFramebufferSize.y;

			m_GBufferCubemapColourAttachment1->width = m_CubemapFramebufferSize.x;
			m_GBufferCubemapColourAttachment1->height = m_CubemapFramebufferSize.y;

			// Colour attachments
			CreateAttachment(m_VulkanDevice, m_GBufferCubemapColourAttachment0, "GBuffer cubemap colour image 0", "GBuffer cubemap colour image view 0");
			CreateAttachment(m_VulkanDevice, m_GBufferCubemapColourAttachment1, "GBuffer cubemap colour image 1", "GBuffer cubemap colour image view 1");

			// TODO: Make PBR cubemap renderpass create this framebuffer

			//// Depth attachment
			//// Set up separate render pass with references to the colour and depth attachments
			//std::vector<VkAttachmentDescription> attachmentDescs(frameBufferColorAttachmentCount + 1);
			//attachmentDescs[0] = vks::attachmentDescription(m_GBufferCubemapColourAttachment0->format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			//attachmentDescs[1] = vks::attachmentDescription(m_GBufferCubemapColourAttachment1->format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			//attachmentDescs[frameBufferColorAttachmentCount] = vks::attachmentDescription(m_GBufferCubemapDepthAttachment->format, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

			//std::vector<VkAttachmentReference> colorReferences;
			//for (u32 i = 0; i < frameBufferColorAttachmentCount; ++i)
			//{
			//	colorReferences.push_back({ i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
			//}

			//VkAttachmentReference depthReference = {};
			//depthReference.attachment = attachmentDescs.size() - 1;
			//depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			//std::array<VkSubpassDescription, 2> subpasses;
			//// Deferred subpass
			//subpasses[0] = {};
			//subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			//subpasses[0].colorAttachmentCount = colorReferences.size();
			//subpasses[0].pColorAttachments = colorReferences.data();
			//subpasses[0].pDepthStencilAttachment = &depthReference;

			//// Forward subpass
			//subpasses[1] = {};
			//subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			//subpasses[1].colorAttachmentCount = 1;
			//subpasses[1].pColorAttachments = colorReferences.data();
			//subpasses[1].pDepthStencilAttachment = &depthReference;

			//std::array<VkSubpassDependency, 3> dependencies;
			//// Deferred subpass
			//dependencies[0] = {};
			//dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			//dependencies[0].dstSubpass = 0;
			//dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			//dependencies[0].srcAccessMask = 0;
			//dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			//dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			//// Forward subpass
			//dependencies[1] = {};
			//dependencies[1].srcSubpass = 0;
			//dependencies[1].dstSubpass = 1;
			//dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			//dependencies[1].srcAccessMask = 0;
			//dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			//dependencies[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			//// Final transition
			//dependencies[2] = {};
			//dependencies[2].srcSubpass = 1;
			//dependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
			//dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			//dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			//dependencies[2].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			//dependencies[2].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

			//VkRenderPassCreateInfo renderPassInfo = vks::renderPassCreateInfo();
			//renderPassInfo.pAttachments = attachmentDescs.data();
			//renderPassInfo.attachmentCount = static_cast<u32>(attachmentDescs.size());
			//renderPassInfo.subpassCount = subpasses.size();
			//renderPassInfo.pSubpasses = subpasses.data();
			//renderPassInfo.dependencyCount = dependencies.size();
			//renderPassInfo.pDependencies = dependencies.data();

			//// m_GBufferCubemapFrameBuffer->ID
			//m_DeferredCubemapRenderPass.Create("GBuffer Cubemap render pass", &renderPassInfo);

			//std::vector<VkImageView> attachments(frameBufferColorAttachmentCount + 1);
			//attachments.push_back(m_GBufferCubemapColourAttachment0->view);
			//attachments.push_back(m_GBufferCubemapColourAttachment1->view);
			//attachments.push_back(m_GBufferCubemapDepthAttachment->view);

			//VkFramebufferCreateInfo fbufCreateInfo = vks::framebufferCreateInfo(m_DeferredCubemapRenderPass);
			//fbufCreateInfo.pAttachments = attachments.data();
			//fbufCreateInfo.attachmentCount = static_cast<u32>(attachments.size());
			//fbufCreateInfo.width = m_GBufferCubemapColourAttachment0->width;
			//fbufCreateInfo.height = m_GBufferCubemapColourAttachment0->height;
			//fbufCreateInfo.layers = 6;

			//VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &fbufCreateInfo, nullptr, frameBuffer.replace()));
			//VulkanRenderer::SetFramebufferName(m_VulkanDevice, frameBuffer, "GBuffer Cubemap frame buffer");
		}

		void VulkanRenderer::FillOutGBufferFrameBufferAttachments(std::vector<Pair<std::string, void*>>& outVec)
		{
			outVec.emplace_back("GBuffer frame buffer attachment 0", (void*)&m_GBufferColourAttachment0->view);
			outVec.emplace_back("GBuffer frame buffer attachment 1", (void*)&m_GBufferColourAttachment1->view);
		}

		void VulkanRenderer::PhysicsDebugRender()
		{
			btDiscreteDynamicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
			physicsWorld->debugDrawWorld();
		}

		void VulkanRenderer::CreateStaticVertexBuffers()
		{
			auto RenderObjectUsesStaticBuffer = [this](VulkanRenderObject* renderObject, u32 staticVertexBufferIndex) -> bool
			{
				return renderObject != nullptr &&
					renderObject->vertexBufferData != nullptr &&
					!m_Materials[renderObject->materialID]->bDynamic &&
					m_Shaders[m_Materials.at(renderObject->materialID)->shaderID]->staticVertexBufferIndex == staticVertexBufferIndex;
			};

			auto iter = m_DirtyStaticVertexBufferIndices.begin();
			while (iter != m_DirtyStaticVertexBufferIndices.end())
			{
				u32 staticVertexBufferIndex = *iter;
				const std::pair<u32, VulkanBuffer*>& vertexBufferPair = m_StaticVertexBuffers[staticVertexBufferIndex];
				VulkanBuffer* vertexBuffer = vertexBufferPair.second;

				u32 requiredMemory = 0;

				for (VulkanRenderObject* renderObject : m_RenderObjects)
				{
					if (RenderObjectUsesStaticBuffer(renderObject, staticVertexBufferIndex))
					{
						requiredMemory += renderObject->vertexBufferData->VertexBufferSize;
					}
				}

				if (requiredMemory > 0)
				{
					const u32 vertexBufferSize = requiredMemory;

					void* vertexDataStart = malloc(vertexBufferSize);
					if (vertexDataStart != nullptr)
					{
						void* vertexBufferData = vertexDataStart;

						u32 vertexCount = 0;
						for (VulkanRenderObject* renderObject : m_RenderObjects)
						{
							if (RenderObjectUsesStaticBuffer(renderObject, staticVertexBufferIndex))
							{
								renderObject->vertexOffset = vertexCount;

								memcpy(vertexBufferData, renderObject->vertexBufferData->vertexData, renderObject->vertexBufferData->VertexBufferSize);

								vertexCount += renderObject->vertexBufferData->VertexCount;

								vertexBufferData = (char*)vertexBufferData + renderObject->vertexBufferData->VertexBufferSize;
							}
						}

						if (vertexBufferSize > 0 && vertexCount > 0)
						{
							assert(vertexBufferData == ((char*)vertexDataStart + vertexBufferSize));

							CreateAndUploadToStaticVertexBuffer(vertexBuffer, vertexDataStart, vertexBufferSize);
						}
						else
						{
							PrintError("Failed to create static vertex buffer\n");
						}

						free(vertexDataStart);
					}
					else
					{
						PrintError("Failed to allocate %d bytes for static vertex buffer\n", vertexBufferSize);
					}
				}

				iter = m_DirtyStaticVertexBufferIndices.erase(iter);
			}
		}

		void VulkanRenderer::CreateAllStaticVertexBuffers()
		{
			for (u32 i = 0; i < (u32)m_StaticVertexBuffers.size(); ++i)
			{
				if (!Contains(m_DirtyStaticVertexBufferIndices, i))
				{
					m_DirtyStaticVertexBufferIndices.push_back(i);
				}
			}

			CreateStaticVertexBuffers();
		}

		void VulkanRenderer::CreateDynamicVertexAndIndexBuffers()
		{
			struct SizePair
			{
				u32 vertMemoryReq;
				u32 indexMemoryReq;
			};
			std::vector<SizePair> dynamicVertexBufferSizes(m_DynamicVertexIndexBufferPairs.size());
			for (u32 shaderID = 0; shaderID < m_Shaders.size(); ++shaderID)
			{
				Shader* shader = m_Shaders[shaderID];
				if (shader->dynamicVertexBufferSize != 0)
				{
					const u32 stride = CalculateVertexStride(shader->vertexAttributes);
					if (stride == 0)
					{
						PrintError("Shader with non-zero dynamic vertex buffer size has stride of 0: %s\n", shader->name.c_str());
					}
					else
					{
						const u32 dynamicVertexBufferIndex = GetDynamicVertexIndexBufferIndex(stride);
						dynamicVertexBufferSizes[dynamicVertexBufferIndex].vertMemoryReq += shader->dynamicVertexBufferSize;
						u32 vertexCount = (u32)glm::ceil(shader->dynamicVertexBufferSize / (real)stride / sizeof(real));
						// Assume worst case of no shared verts
						dynamicVertexBufferSizes[dynamicVertexBufferIndex].indexMemoryReq += vertexCount * sizeof(u32);
					}
				}
			}

			auto iter = m_DirtyDynamicVertexAndIndexBufferIndices.begin();
			while (iter != m_DirtyDynamicVertexAndIndexBufferIndices.end())
			{
				u32 bufferIndex = *iter;
				const std::pair<u32, VertexIndexBufferPair*>& vertexIndexBufferPair = m_DynamicVertexIndexBufferPairs[bufferIndex];
				const SizePair& sizePair = dynamicVertexBufferSizes[bufferIndex];

				if (sizePair.vertMemoryReq > 0)
				{
					CreateDynamicVertexBuffer(vertexIndexBufferPair.second->vertexBuffer, sizePair.vertMemoryReq);
					CreateDynamicIndexBuffer(vertexIndexBufferPair.second->indexBuffer, sizePair.indexMemoryReq);
				}

				iter = m_DirtyDynamicVertexAndIndexBufferIndices.erase(iter);
			}
		}

		void VulkanRenderer::CreateAllDynamicVertexAndIndexBuffers()
		{
			for (u32 bufferIndex = 0; bufferIndex < m_DynamicVertexIndexBufferPairs.size(); ++bufferIndex)
			{
				if (!Contains(m_DirtyDynamicVertexAndIndexBufferIndices, bufferIndex))
				{
					m_DirtyDynamicVertexAndIndexBufferIndices.push_back(bufferIndex);
				}
			}

			CreateDynamicVertexAndIndexBuffers();

			// UI Shader
			{
				ShaderID uiShaderID;
				GetShaderID("ui", uiShaderID);
				VulkanShader* uiShader = (VulkanShader*)GetShader(uiShaderID);
				const u32 stride = CalculateVertexStride(uiShader->vertexAttributes);

				m_DynamicUIVertexIndexBufferPair = new VertexIndexBufferPair(new VulkanBuffer(m_VulkanDevice), new VulkanBuffer(m_VulkanDevice));

				u32 vertMemoryReq = uiShader->dynamicVertexBufferSize;
				u32 vertexCount = (u32)glm::ceil(uiShader->dynamicVertexBufferSize / (real)stride / sizeof(real));
				u32 indexMemoryReq = vertexCount * sizeof(u32);
				CreateDynamicVertexBuffer(m_DynamicUIVertexIndexBufferPair->vertexBuffer, vertMemoryReq);
				CreateDynamicIndexBuffer(m_DynamicUIVertexIndexBufferPair->indexBuffer, indexMemoryReq);
			}
		}

		void VulkanRenderer::CreateStaticIndexBuffer()
		{
			std::vector<u32> indices;

			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject &&
					renderObject->bIndexed &&
					!m_Materials[renderObject->materialID]->bDynamic)
				{
					renderObject->indexOffset = (u32)indices.size();
					indices.insert(indices.end(), renderObject->indices->begin(), renderObject->indices->end());
				}
			}

			if (indices.empty())
			{
				// No indexed render objects use specified shader
				return;
			}

			CreateAndUploadToStaticIndexBuffer(m_StaticIndexBuffer, indices);
		}

		void VulkanRenderer::CreateShadowVertexBuffer()
		{
			VulkanMaterial* shadowMat = (VulkanMaterial*)m_Materials[m_ShadowMaterialID];
			VulkanShader* shadowShader = (VulkanShader*)m_Shaders[shadowMat->shaderID];

			u32 vertexStride = CalculateVertexStride(shadowShader->vertexAttributes);
			u32 size = 0;

			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject != nullptr &&
					renderObject->vertexBufferData != nullptr)
				{
					size += renderObject->vertexBufferData->VertexCount * vertexStride;
				}
			}

			if (size == 0)
			{
				return;
			}

			void* vertexDataStart = malloc(size);
			if (vertexDataStart == nullptr)
			{
				PrintError("Failed to allocate memory for shadow vertex buffer! Attempted to allocate %d bytes", size);
				return;
			}

			void* vertexBufferData = vertexDataStart;

			u32 vertexCount = 0;
			u32 vertexBufferSize = 0;
			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject != nullptr &&
					renderObject->vertexBufferData != nullptr &&
					renderObject->gameObject->CastsShadow() &&
					renderObject->gameObject->IsVisible())
				{
					renderObject->shadowVertexOffset = vertexCount;

					u32 copySize = renderObject->vertexBufferData->CopyInto(static_cast<real*>(vertexBufferData), shadowShader->vertexAttributes);

					vertexCount += renderObject->vertexBufferData->VertexCount;
					vertexBufferSize += copySize;

					vertexBufferData = (char*)vertexBufferData + copySize;
				}
			}

			if (vertexBufferSize == 0 || vertexCount == 0)
			{
				free(vertexDataStart);
				return;
			}

			CreateAndUploadToStaticVertexBuffer(m_ShadowVertexIndexBufferPair->vertexBuffer, vertexDataStart, vertexBufferSize);
			free(vertexDataStart);
		}

		void VulkanRenderer::CreateAndUploadToStaticVertexBuffer(VulkanBuffer* vertexBuffer, void* vertexBufferData, u32 vertexBufferSize)
		{
			VulkanBuffer stagingBuffer(m_VulkanDevice);
			stagingBuffer.Create(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			VK_CHECK_RESULT(stagingBuffer.Map(vertexBufferSize));
			memcpy(stagingBuffer.m_Mapped, vertexBufferData, vertexBufferSize);
			stagingBuffer.Unmap();

			vertexBuffer->Create(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			CopyBuffer(m_VulkanDevice, m_GraphicsQueue, stagingBuffer.m_Buffer, vertexBuffer->m_Buffer, vertexBufferSize);
		}

		void VulkanRenderer::CreateDynamicVertexBuffer(VulkanBuffer* vertexBuffer, u32 size)
		{
			vertexBuffer->Create(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		}

		void VulkanRenderer::CreateDynamicIndexBuffer(VulkanBuffer* indexBuffer, u32 size)
		{
			indexBuffer->Create(size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		}

		void VulkanRenderer::CreateShadowIndexBuffer()
		{
			std::vector<u32> indices;

			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject != nullptr &&
					renderObject->bIndexed &&
					renderObject->gameObject->CastsShadow() &&
					renderObject->gameObject->IsVisible())
				{
					renderObject->shadowIndexOffset = (u32)indices.size();
					indices.insert(indices.end(), renderObject->indices->begin(), renderObject->indices->end());
				}
			}

			if (indices.empty())
			{
				return;
			}

			CreateAndUploadToStaticIndexBuffer(m_ShadowVertexIndexBufferPair->indexBuffer, indices);
			m_ShadowVertexIndexBufferPair->indexCount = (u32)indices.size();
		}

		void VulkanRenderer::CreateAndUploadToStaticIndexBuffer(VulkanBuffer* indexBuffer, const std::vector<u32>& indices)
		{
			const u32 bufferSize = sizeof(indices[0]) * (u32)indices.size();

			VulkanBuffer stagingBuffer(m_VulkanDevice);
			stagingBuffer.Create(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			VK_CHECK_RESULT(stagingBuffer.Map(bufferSize));
			memcpy(stagingBuffer.m_Mapped, indices.data(), bufferSize);
			stagingBuffer.Unmap();

			indexBuffer->Create(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			CopyBuffer(m_VulkanDevice, m_GraphicsQueue, stagingBuffer.m_Buffer, indexBuffer->m_Buffer, bufferSize);
		}

		u32 VulkanRenderer::AllocateDynamicUniformBuffer(u32 bufferUnitSize, void** data, i32 maxObjectCount /* = -1 */)
		{
			size_t uboAlignment = (size_t)m_VulkanDevice->m_PhysicalDeviceProperties.limits.minUniformBufferOffsetAlignment;
			size_t dynamicAllignment = (bufferUnitSize / uboAlignment) * uboAlignment + ((bufferUnitSize % uboAlignment) > 0 ? uboAlignment : 0);

			if (dynamicAllignment > m_DynamicAlignment)
			{
				size_t newDynamicAllignment = 1;
				while (newDynamicAllignment < dynamicAllignment)
				{
					newDynamicAllignment <<= 1;
				}
				m_DynamicAlignment = (u32)newDynamicAllignment;
			}

			if (maxObjectCount == -1)
			{
				maxObjectCount = MAX_NUM_RENDER_OBJECTS;
			}

			size_t dynamicBufferSize = (size_t)maxObjectCount * m_DynamicAlignment;

			assert(*data == nullptr);
			(*data) = flex_aligned_malloc(dynamicBufferSize, m_DynamicAlignment);
			assert(*data);

			return (u32)dynamicBufferSize;
		}

		void VulkanRenderer::PrepareUniformBuffer(VulkanBuffer* buffer, u32 bufferSize,
			VkBufferUsageFlags bufferUseageFlagBits, VkMemoryPropertyFlags memoryPropertyHostFlagBits, bool bMap /* = true */)
		{
			buffer->Create(bufferSize, bufferUseageFlagBits, memoryPropertyHostFlagBits);
			if (bMap)
			{
				VK_CHECK_RESULT(buffer->Map());
			}
		}

		void VulkanRenderer::CreateSemaphores()
		{
			VkSemaphoreCreateInfo semaphoreInfo = vks::semaphoreCreateInfo();

			VK_CHECK_RESULT(vkCreateSemaphore(m_VulkanDevice->m_LogicalDevice, &semaphoreInfo, nullptr, m_PresentCompleteSemaphore.replace()));
			VK_CHECK_RESULT(vkCreateSemaphore(m_VulkanDevice->m_LogicalDevice, &semaphoreInfo, nullptr, m_RenderCompleteSemaphore.replace()));
		}

		void VulkanRenderer::FillOutShaderBatches(const std::vector<RenderID>& renderIDs, i32* inOutDynamicUBOOffset,
			MaterialBatchPair& matBatchPair, MaterialBatchPair& depthAwareEditorMatBatchPair, MaterialBatchPair& depthUnawareEditorMatBatchPair,
			MaterialID matID, bool bWriteUBOOffsets /* = true */)
		{
			auto RenderObjectFilter = [this, matID](VulkanRenderObject* renderObject) -> bool
			{
				return (renderObject != nullptr &&
					IsGraphicsPipelineValid(renderObject->graphicsPipelineID) &&
					renderObject->materialID == matID);
			};

			VulkanMaterial* material = (VulkanMaterial*)m_Materials[matID];
			VulkanShader* shader = (VulkanShader*)m_Shaders[material->shaderID];

			const UniformBuffer* dynamicBuffer = material->uniformBufferList.Get(UniformBufferType::DYNAMIC);

			u32 dynamicObjectCount = 0;

			// Build up list of batches
			for (u32 renderID : renderIDs)
			{
				VulkanRenderObject* renderObject = GetRenderObject(renderID);

				if (RenderObjectFilter(renderObject))
				{
					if (dynamicBuffer != nullptr)
					{
						// TODO: Check bWriteUBOOffsets ?
						++dynamicObjectCount;
					}

					if (renderObject->gameObject->IsVisible())
					{
						if (renderObject->bEditorObject)
						{
							if (shader->bDepthWriteEnable)
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
							// TODO: Sort translucents front to back here
							matBatchPair.batch.objects.push_back(renderID);
						}
					}
				}
			}

			// Check if dynamic buffer needs to grow
			if (dynamicBuffer != nullptr)
			{
				u32 newUsedSize = *inOutDynamicUBOOffset + (dynamicObjectCount * dynamicBuffer->data.unitSize);
				if (newUsedSize > dynamicBuffer->fullDynamicBufferSize)
				{
					// TODO: We may need to do this to be fully safe, once buffered resources are in it
					// definitely won't be necessary though
					//VK_CHECK_RESULT(vkQueueWaitIdle(m_GraphicsQueue));
					//VK_CHECK_RESULT(vkQueueWaitIdle(m_PresentQueue));

					real growthRate = 1.5f;
					i32 newMax = (i32)glm::ceil((real)newUsedSize / m_DynamicAlignment * growthRate);
					UpdateShaderMaxObjectCount(material->shaderID, newMax);
				}
			}

			// Update dynamic UBO offsets
			if (bWriteUBOOffsets && dynamicBuffer != nullptr)
			{
				for (u32 renderID : renderIDs)
				{
					VulkanRenderObject* renderObject = GetRenderObject(renderID);

					if (RenderObjectFilter(renderObject))
					{
						// TODO: Move down
						renderObject->dynamicUBOOffset = *inOutDynamicUBOOffset;
						*inOutDynamicUBOOffset += RoundUp(dynamicBuffer->data.unitSize - 1, m_DynamicAlignment);
					}
				}
			}
		}

		void VulkanRenderer::BatchRenderObjects()
		{
			if (!m_DirtyStaticVertexBufferIndices.empty())
			{
				CreateAllStaticVertexBuffers();
				CreateStaticIndexBuffer();
			}
			if (m_DirtyFlagBits & RenderBatchDirtyFlag::SHADOW_DATA)
			{
				CreateShadowVertexBuffer();
				CreateShadowIndexBuffer();
			}

			m_DirtyFlagBits = RenderBatchDirtyFlag::CLEAN;

			const char* blockName = "BatchRenderObjects";
			u32 renderObjBatchCount = 0;
			{
				PROFILE_AUTO(blockName);

				m_DeferredObjectBatches.batches.clear();
				m_ForwardObjectBatches.batches.clear();
				m_ShadowBatch.batches.clear();
				m_DepthAwareEditorObjBatches.batches.clear();
				m_DepthUnawareEditorObjBatches.batches.clear();

				std::vector<RenderID> renderIDs;
				renderIDs.reserve(m_RenderObjects.size());
				for (u32 renderID = 0; renderID < (u32)m_RenderObjects.size(); ++renderID)
				{
					if (m_RenderObjects[renderID])
					{
						renderIDs.push_back(renderID);
					}
				}

				// NOTE: Optimization options:
				//			- Sort Materials by shader ID to allow early out on second loop
				//			- Sort render objects based on batching order
				//			- Reuse previous batching, only removing or adding entries

				// TODO: Iterate over other definition of vertex buffers
				for (u32 shaderID = 0; shaderID < m_Shaders.size(); ++shaderID)
				{
					const bool bDeferred = m_Shaders[shaderID]->numAttachments > 1;
					ShaderBatch* shaderBatch = (bDeferred ? &m_DeferredObjectBatches : &m_ForwardObjectBatches);

					// Blocklist certain shaders
					if (m_Shaders[shaderID]->name == "ui")
					{
						continue;
					}

					for (u32 dynamic = 0; dynamic <= 1; ++dynamic)
					{
						ShaderBatchPair shaderBatchPair = {};
						shaderBatchPair.shaderID = shaderID;
						shaderBatchPair.bDynamic = (dynamic == 1);

						ShaderBatchPair depthAwareEditorShaderBatchPair = {};
						depthAwareEditorShaderBatchPair.shaderID = shaderID;
						depthAwareEditorShaderBatchPair.bDynamic = (dynamic == 1);

						ShaderBatchPair depthUnawareEditorShaderBatchPair = {};
						depthUnawareEditorShaderBatchPair.shaderID = shaderID;
						depthUnawareEditorShaderBatchPair.bDynamic = (dynamic == 1);

						i32 dynamicUBOOffset = 0;

						for (const auto& matPair : m_Materials)
						{
							MaterialID matID = matPair.first;
							const VulkanMaterial* material = (VulkanMaterial*)matPair.second;

							if (material->shaderID == shaderID && ((u32)material->bDynamic) == dynamic)
							{
								MaterialBatchPair matBatchPair = {};
								matBatchPair.materialID = matID;

								MaterialBatchPair depthAwareEditorMatBatchPair = {};
								depthAwareEditorMatBatchPair.materialID = matID;

								MaterialBatchPair depthUnawareEditorMatBatchPair = {};
								depthUnawareEditorMatBatchPair.materialID = matID;

								FillOutShaderBatches(renderIDs, &dynamicUBOOffset, matBatchPair, depthAwareEditorMatBatchPair, depthUnawareEditorMatBatchPair, matID);

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
			}

			ShaderBatchPair shadowShaderBatch;
			shadowShaderBatch.batch.batches.resize(1);
			u32 dynamicShadowUBOOffset = 0;
			for (u32 i = 0; i < (u32)m_RenderObjects.size(); ++i)
			{
				VulkanRenderObject* renderObject = GetRenderObject(i);
				if (renderObject != nullptr &&
					renderObject->vertexBufferData != nullptr &&
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
			Print("Batched %u render objects into %u batches in %.2fms\n", (u32)m_RenderObjects.size(), renderObjBatchCount, blockMS);
		}

		void VulkanRenderer::DrawShaderBatch(const ShaderBatchPair& shaderBatch, VkCommandBuffer& commandBuffer, DrawCallInfo* drawCallInfo /* = nullptr */)
		{
			ShaderID shaderID = shaderBatch.shaderID;
			if (drawCallInfo && drawCallInfo->materialIDOverride != InvalidMaterialID)
			{
				shaderID = m_Materials.at(drawCallInfo->materialIDOverride)->shaderID;
			}

			VulkanBuffer* indexBuffer;
			VulkanBuffer* vertBuffer;
			if (drawCallInfo && drawCallInfo->bRenderingShadows)
			{
				vertBuffer = m_ShadowVertexIndexBufferPair->vertexBuffer;
				indexBuffer = m_ShadowVertexIndexBufferPair->indexBuffer;
			}
			else
			{
				u32 dynamicVertexIndexBufferIndex = GetDynamicVertexIndexBufferIndex(CalculateVertexStride(m_Shaders[shaderID]->vertexAttributes));
				u32 staticVertexBufferIndex = GetStaticVertexIndexBufferIndex(CalculateVertexStride(m_Shaders[shaderID]->vertexAttributes));
				vertBuffer = shaderBatch.bDynamic ? m_DynamicVertexIndexBufferPairs[dynamicVertexIndexBufferIndex].second->vertexBuffer : m_StaticVertexBuffers[staticVertexBufferIndex].second;
				indexBuffer = shaderBatch.bDynamic ? m_DynamicVertexIndexBufferPairs[dynamicVertexIndexBufferIndex].second->indexBuffer : m_StaticIndexBuffer;
			}

			if (indexBuffer->m_Buffer != VK_NULL_HANDLE)
			{
				vkCmdBindIndexBuffer(commandBuffer, indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
			}

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertBuffer->m_Buffer, offsets);

			for (const MaterialBatchPair& matBatch : shaderBatch.batch.batches)
			{
				MaterialID matID = matBatch.materialID;
				if (drawCallInfo && drawCallInfo->bWireframe)
				{
					matID = m_WireframeMatID;
				}
				else if (drawCallInfo && drawCallInfo->materialIDOverride != InvalidMaterialID)
				{
					matID = drawCallInfo->materialIDOverride;
				}

				// TODO: Check persistence

				VulkanMaterial* material = (VulkanMaterial*)m_Materials.at(matID);
				VulkanShader* shader = (VulkanShader*)m_Shaders[material->shaderID];

				VkDescriptorSet descriptorSet = material->persistent ?
					m_DescriptorPoolPersistent->descriptorSets[matID] :
					m_DescriptorPool->descriptorSets[matID];

				for (RenderID renderID : matBatch.batch.objects)
				{
					VulkanRenderObject* renderObject = GetRenderObject(renderID);

					GraphicsPipeline* pipeline = GetGraphicsPipeline(renderObject->graphicsPipelineID)->pipeline;
					VkPipeline graphicsPipeline = pipeline->pipeline;
					VkPipelineLayout pipelineLayout = pipeline->layout;
					u32 dynamicUBOOffset = renderObject->dynamicUBOOffset;

					if (drawCallInfo)
					{
						if (drawCallInfo->graphicsPipelineOverride != InvalidID)
						{
							graphicsPipeline = (VkPipeline)drawCallInfo->graphicsPipelineOverride;
							assert(drawCallInfo->pipelineLayoutOverride != InvalidID);
							pipelineLayout = (VkPipelineLayout)drawCallInfo->pipelineLayoutOverride;
						}
						if (drawCallInfo->descriptorSetOverride != InvalidID)
						{
							descriptorSet = (VkDescriptorSet)drawCallInfo->descriptorSetOverride;
						}
						if (drawCallInfo->bRenderingShadows)
						{
							dynamicUBOOffset = renderObject->dynamicShadowUBOOffset;
						}
						if (drawCallInfo->bWireframe)
						{
							VulkanMaterial* objectMaterial = (VulkanMaterial*)m_Materials[matBatch.materialID];
							VulkanShader* objectShader = (VulkanShader*)m_Shaders[objectMaterial->shaderID];

							auto pipelineIter = m_WireframeGraphicsPipelines.find(objectShader->vertexAttributes);
							if (pipelineIter != m_WireframeGraphicsPipelines.end())
							{
								GraphicsPipelineID pipelineID = pipelineIter->second;
								pipeline = GetGraphicsPipeline(pipelineID)->pipeline;
							}
							else
							{
								// Create wireframe pipeline for given vertex attributes
								VulkanMaterial* wireframeMaterial = (VulkanMaterial*)m_Materials[m_WireframeMatID];
								VulkanShader* wireframeShader = (VulkanShader*)m_Shaders[wireframeMaterial->shaderID];

								GraphicsPipelineID pipelineID = InvalidGraphicsPipelineID;
								GraphicsPipelineCreateInfo pipelineCreateInfo = {};
								pipelineCreateInfo.DBG_Name = "Wireframe pipeline";
								pipelineCreateInfo.shaderID = wireframeMaterial->shaderID;
								pipelineCreateInfo.vertexAttributes = objectShader->vertexAttributes;
								pipelineCreateInfo.descriptorSetLayoutIndex = wireframeMaterial->descriptorSetLayoutIndex;
								pipelineCreateInfo.bEnableColourBlending = wireframeShader->bTranslucent;
								pipelineCreateInfo.subpass = 0;
								pipelineCreateInfo.cullMode = VK_CULL_MODE_NONE;
								pipelineCreateInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
								pipelineCreateInfo.depthTestEnable = false;
								pipelineCreateInfo.depthWriteEnable = wireframeShader->bDepthWriteEnable ? VK_TRUE : VK_FALSE;
								pipelineCreateInfo.renderPass = wireframeShader->renderPass;
								pipelineCreateInfo.bPersistent = true;
								CreateGraphicsPipeline(&pipelineCreateInfo, pipelineID);
								GraphicsPipelineConfiguration* newWireframePipeline = GetGraphicsPipeline(pipelineID);

								m_WireframeGraphicsPipelines[objectShader->vertexAttributes] = pipelineID;
								pipeline = newWireframePipeline->pipeline;
							}

							graphicsPipeline = pipeline->pipeline;
							pipelineLayout = pipeline->layout;
							descriptorSet = m_WireframeDescSet;
						}
						if (drawCallInfo->bCalculateDynamicUBOOffset)
						{
							dynamicUBOOffset = drawCallInfo->dynamicUBOOffset;

							const UniformBuffer* wireframeDynamicBuffer = material->uniformBufferList.Get(UniformBufferType::DYNAMIC);
							drawCallInfo->dynamicUBOOffset += RoundUp(wireframeDynamicBuffer->data.unitSize - 1, m_DynamicAlignment);
						}
					}

					vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

					BindDescriptorSet(material, dynamicUBOOffset, commandBuffer, pipelineLayout, descriptorSet);

					Material::PushConstantBlock* pushConstantBlock = material->pushConstantBlock;
					if (shader->bNeedPushConstantBlock)
					{
						// Push constants
						if (drawCallInfo && drawCallInfo->pushConstantOverride)
						{
							pushConstantBlock = drawCallInfo->pushConstantOverride;
						}
						else
						{
							BaseCamera* cam = g_CameraManager->CurrentCamera();
							material->pushConstantBlock->SetData(cam->GetView(), cam->GetProjection());
						}

						if (pushConstantBlock->data != nullptr)
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
							vkCmdDrawIndexed(commandBuffer, (u32)renderObject->indices->size(), 1, renderObject->indexOffset, renderObject->vertexOffset, 0);
						}
						else
						{
							vkCmdDrawIndexed(commandBuffer, (u32)renderObject->indices->size(), 1, renderObject->shadowIndexOffset, renderObject->shadowVertexOffset, 0);
						}
					}
					else
					{
						if (drawCallInfo == nullptr ||
							!drawCallInfo->bRenderingShadows)
						{
							vkCmdDraw(commandBuffer, renderObject->vertexBufferData->UsedVertexCount, 1, renderObject->vertexOffset, 0);
						}
						else
						{
							vkCmdDraw(commandBuffer, renderObject->vertexBufferData->UsedVertexCount, 1, renderObject->shadowVertexOffset, 0);
						}
					}
				}
			}
		}

		void VulkanRenderer::RenderFullscreenTri(
			VkCommandBuffer commandBuffer,
			MaterialID materialID,
			VkPipelineLayout pipelineLayout,
			VkDescriptorSet descriptorSet)
		{
			VulkanMaterial* material = (VulkanMaterial*)m_Materials.at(materialID);

			VkDeviceSize offsets[1] = { 0 };
			BindDescriptorSet(material, 0, commandBuffer, pipelineLayout, descriptorSet);

			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_FullScreenTriVertexBuffer->m_Buffer, offsets);

			vkCmdDraw(commandBuffer, m_FullScreenTriVertexBufferData.VertexCount, 1, 0, 0);
		}

		void VulkanRenderer::RenderFullscreenTri(
			VkCommandBuffer commandBuffer,
			VulkanRenderPass* renderPass,
			MaterialID materialID,
			VkPipelineLayout pipelineLayout,
			VkPipeline graphicsPipeline,
			VkDescriptorSet descriptorSet,
			bool bFlipViewport)
		{
			std::vector<VkClearValue> clearValues(2);
			clearValues[0].color = m_ClearColour;
			clearValues[1].depthStencil = { 0.0f, 0 };

			VulkanMaterial* material = (VulkanMaterial*)m_Materials.at(materialID);

			VkViewport fullscreenViewport = bFlipViewport ?
				vks::viewportFlipped((real)m_SwapChainExtent.width, (real)m_SwapChainExtent.height, 0.0f, 1.0f) :
				vks::viewport((real)m_SwapChainExtent.width, (real)m_SwapChainExtent.height, 0.0f, 1.0f);
			VkRect2D fullscreenScissor = vks::scissor(0u, 0u, m_SwapChainExtent.width, m_SwapChainExtent.height);

			VkDeviceSize offsets[1] = { 0 };

			renderPass->Begin(commandBuffer, clearValues.data(), (u32)clearValues.size());

			{
				BindDescriptorSet(material, 0, commandBuffer, pipelineLayout, descriptorSet);

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

				vkCmdSetViewport(commandBuffer, 0, 1, &fullscreenViewport);
				vkCmdSetScissor(commandBuffer, 0, 1, &fullscreenScissor);

				vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_FullScreenTriVertexBuffer->m_Buffer, offsets);

				vkCmdDraw(commandBuffer, m_FullScreenTriVertexBufferData.VertexCount, 1, 0, 0);
			}

			renderPass->End();
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

				SetLineWidthForCmdBuffer(m_OffScreenCmdBuffer);

				//
				// Cascaded shadow mapping
				//

				bool bEnableShadows = true;
				if (bEnableShadows && m_DirectionalLight && m_DirectionalLight->data.castShadows)
				{
					BeginDebugMarkerRegion(m_OffScreenCmdBuffer, "Shadow cascades");

					VkClearValue depthStencilClearValue = VkClearValue { 0.0f, 0 };

					for (const ShaderBatchPair& shaderBatch : m_ShadowBatch.batches)
					{
						for (RenderID renderID : shaderBatch.batch.batches[0].batch.objects)
						{
							VulkanRenderObject* renderObject = GetRenderObject(renderID);
							UpdateDynamicUniformBuffer(renderID, nullptr, m_ShadowMaterialID, renderObject->dynamicShadowUBOOffset);
						}
					}

					VkViewport viewport = vks::viewportFlipped((real)m_ShadowCascades[0]->frameBuffer.width, (real)m_ShadowCascades[0]->frameBuffer.height, 0.0f, 1.0f);
					vkCmdSetViewport(m_OffScreenCmdBuffer, 0, 1, &viewport);

					VkRect2D shadowScissor = vks::scissor(0u, 0u, m_ShadowCascades[0]->frameBuffer.width, m_ShadowCascades[0]->frameBuffer.height);
					vkCmdSetScissor(m_OffScreenCmdBuffer, 0, 1, &shadowScissor);

					if (m_CascadedShadowMapPushConstantBlock == nullptr)
					{
						m_CascadedShadowMapPushConstantBlock = new Material::PushConstantBlock();
					}

					GraphicsPipeline* pipeline = GetGraphicsPipeline(m_ShadowGraphicsPipelineID)->pipeline;

					DrawCallInfo shadowDrawCallInfo = {};
					shadowDrawCallInfo.materialIDOverride = m_ShadowMaterialID;
					shadowDrawCallInfo.graphicsPipelineOverride = (u64)(VkPipeline)pipeline->pipeline;
					shadowDrawCallInfo.pipelineLayoutOverride = (u64)(VkPipelineLayout)pipeline->layout;
					shadowDrawCallInfo.descriptorSetOverride = (u64)(VkDescriptorSet)m_ShadowDescriptorSet;
					shadowDrawCallInfo.bRenderingShadows = true;

					for (i32 c = 0; c < m_ShadowCascadeCount; ++c)
					{
						m_ShadowRenderPass->Begin_WithFrameBuffer(m_OffScreenCmdBuffer, &depthStencilClearValue, 1, &m_ShadowCascades[c]->frameBuffer);

						// TODO: Upload as one draw
						m_CascadedShadowMapPushConstantBlock->SetData(m_ShadowSamplingData.cascadeViewProjMats[c]);
						shadowDrawCallInfo.pushConstantOverride = m_CascadedShadowMapPushConstantBlock;

						for (const ShaderBatchPair& shaderBatch : m_ShadowBatch.batches)
						{
							DrawShaderBatch(shaderBatch, m_OffScreenCmdBuffer, &shadowDrawCallInfo);
						}

						m_ShadowRenderPass->End();
					}

					EndDebugMarkerRegion(m_OffScreenCmdBuffer, "End Shadow cascades");
				}

				BeginDebugMarkerRegion(m_OffScreenCmdBuffer, "Deferred");

				//
				// G-Buffer fill
				//

				std::vector<VkClearValue> gBufClearValues(3);
				gBufClearValues[0].color = m_ClearColour;
				gBufClearValues[1].color = m_ClearColour;
				gBufClearValues[2].depthStencil = { 0.0f, 0 };

				m_DeferredRenderPass->Begin(m_OffScreenCmdBuffer, gBufClearValues.data(), (u32)gBufClearValues.size());

				VkViewport fullScreenViewport = vks::viewportFlipped((real)m_GBufferColourAttachment0->width, (real)m_GBufferColourAttachment0->height, 0.0f, 1.0f);
				vkCmdSetViewport(m_OffScreenCmdBuffer, 0, 1, &fullScreenViewport);

				VkRect2D fullScreenScissor = vks::scissor(0u, 0u, m_GBufferColourAttachment0->width, m_GBufferColourAttachment0->height);
				vkCmdSetScissor(m_OffScreenCmdBuffer, 0, 1, &fullScreenScissor);

				for (const ShaderBatchPair& shaderBatch : m_DeferredObjectBatches.batches)
				{
					DrawShaderBatch(shaderBatch, m_OffScreenCmdBuffer);
				}

				m_DeferredRenderPass->End();

				// NOTE: Only needed on the first frame
				m_GBufferDepthAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_GraphicsQueue);

				EndDebugMarkerRegion(m_OffScreenCmdBuffer, "End Deferred");

				//
				// SSAO
				//

				VkDeviceSize offsets[1] = { 0 };

				if (m_SSAOSamplingData.enabled)
				{
					BeginGPUTimeStamp(m_OffScreenCmdBuffer, "SSAO");

					BeginDebugMarkerRegion(m_OffScreenCmdBuffer, "SSAO");

					m_SSAORenderPass->Begin(m_OffScreenCmdBuffer, (VkClearValue*)&m_ClearColour, 1);

					assert(m_SSAOShaderID != InvalidShaderID);

					GraphicsPipeline* pipeline = GetGraphicsPipeline(m_SSAOGraphicsPipelineID)->pipeline;
					vkCmdBindPipeline(m_OffScreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);

					VkViewport ssaoViewport = vks::viewportFlipped((real)m_SSAOFBColourAttachment0->width, (real)m_SSAOFBColourAttachment0->height, 0.0f, 1.0f);
					vkCmdSetViewport(m_OffScreenCmdBuffer, 0, 1, &ssaoViewport);

					VkRect2D ssaoScissor = vks::scissor(0u, 0u, m_SSAOFBColourAttachment0->width, m_SSAOFBColourAttachment0->height);
					vkCmdSetScissor(m_OffScreenCmdBuffer, 0, 1, &ssaoScissor);

					RenderFullscreenTri(m_OffScreenCmdBuffer, m_SSAOMatID, pipeline->layout, m_SSAODescSet);

					m_SSAORenderPass->End();

					EndDebugMarkerRegion(m_OffScreenCmdBuffer, "End SSAO");

					//
					// SSAO blur
					//

					if (m_bSSAOBlurEnabled)
					{
						BeginDebugMarkerRegion(m_OffScreenCmdBuffer, "SSAO Blur");

						VulkanMaterial* ssaoBlurMat = (VulkanMaterial*)m_Materials[m_SSAOBlurMatID];

						// Horizontal pass
						m_SSAOBlurHRenderPass->Begin(m_OffScreenCmdBuffer, (VkClearValue*)&m_ClearColour, 1);

						assert(m_SSAOBlurShaderID != InvalidShaderID);

						GraphicsPipeline* blurHPipeline = GetGraphicsPipeline(m_SSAOBlurHGraphicsPipelineID)->pipeline;
						vkCmdBindPipeline(m_OffScreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blurHPipeline->pipeline);

						VkViewport ssaoBlurViewport = vks::viewportFlipped((real)m_SSAOBlurHFBColourAttachment0->width, (real)m_SSAOBlurHFBColourAttachment0->height, 0.0f, 1.0f);
						vkCmdSetViewport(m_OffScreenCmdBuffer, 0, 1, &ssaoBlurViewport);

						VkRect2D ssaoBlurScissor = vks::scissor(0u, 0u, m_SSAOBlurHFBColourAttachment0->width, m_SSAOBlurHFBColourAttachment0->height);
						vkCmdSetScissor(m_OffScreenCmdBuffer, 0, 1, &ssaoBlurScissor);

						BindDescriptorSet(ssaoBlurMat, 0, m_OffScreenCmdBuffer, blurHPipeline->layout, m_SSAOBlurHDescSet);

						vkCmdBindVertexBuffers(m_OffScreenCmdBuffer, 0, 1, &m_FullScreenTriVertexBuffer->m_Buffer, offsets);

						UniformOverrides overrides = {};
						overrides.overridenUniforms.AddUniform(U_SSAO_BLUR_DATA_DYNAMIC);
						overrides.bSSAOVerticalPass = false;
						UpdateDynamicUniformBuffer(m_SSAOBlurMatID, 0 * m_DynamicAlignment, MAT4_IDENTITY, &overrides);

						vkCmdDraw(m_OffScreenCmdBuffer, m_FullScreenTriVertexBufferData.VertexCount, 1, 0, 0);

						m_SSAOBlurHRenderPass->End();

						// Vertical pass
						m_SSAOBlurVRenderPass->Begin(m_OffScreenCmdBuffer, (VkClearValue*)&m_ClearColour, 1);

						GraphicsPipeline* blurVPipeline = GetGraphicsPipeline(m_SSAOBlurVGraphicsPipelineID)->pipeline;
						vkCmdBindPipeline(m_OffScreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blurVPipeline->pipeline);

						BindDescriptorSet(ssaoBlurMat, 0, m_OffScreenCmdBuffer, blurVPipeline->layout, m_SSAOBlurVDescSet);

						overrides.bSSAOVerticalPass = true;
						UpdateDynamicUniformBuffer(m_SSAOBlurMatID, 1 * m_DynamicAlignment, MAT4_IDENTITY, &overrides);

						vkCmdDraw(m_OffScreenCmdBuffer, m_FullScreenTriVertexBufferData.VertexCount, 1, 0, 0);

						m_SSAOBlurVRenderPass->End();

						EndDebugMarkerRegion(m_OffScreenCmdBuffer, "End SSAO Blur");
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

			SetLineWidthForCmdBuffer(commandBuffer);

			// Particle simulation
			if (!m_ParticleSystems.empty())
			{
				BeginGPUTimeStamp(commandBuffer, "Simulate Particles");
				BeginDebugMarkerRegion(commandBuffer, "Simulate Particles");

				for (VulkanParticleSystem* particleSystem : m_ParticleSystems)
				{
					if (!particleSystem || !particleSystem->system->IsVisible() || !particleSystem->system->bEnabled)
					{
						continue;
					}

					VulkanMaterial* particleSimMat = (VulkanMaterial*)m_Materials.at(particleSystem->system->simMaterialID);

					UniformBuffer* particleBuffer = particleSimMat->uniformBufferList.Get(UniformBufferType::PARTICLE_DATA);

					VkBufferMemoryBarrier bufferBarrier = vks::bufferMemoryBarrier();
					bufferBarrier.buffer = particleBuffer->buffer.m_Buffer;
					bufferBarrier.size = particleBuffer->buffer.m_Size;
					bufferBarrier.srcAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
					bufferBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
					// Compute and graphics queue may have different queue families
					// For the barrier to work across different queues, we need to set their family indices
					bufferBarrier.srcQueueFamilyIndex = m_VulkanDevice->m_QueueFamilyIndices.graphicsFamily;
					bufferBarrier.dstQueueFamilyIndex = m_VulkanDevice->m_QueueFamilyIndices.computeFamily;
					vkCmdPipelineBarrier(
						commandBuffer,
						VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						0,
						0, nullptr,
						1, &bufferBarrier,
						0, nullptr);

					vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, particleSystem->computePipeline);

					u32 dynamicUBOOffset = particleSystem->ID * m_DynamicAlignment;

					UniformOverrides overrides = {};
					overrides.overridenUniforms.AddUniform(U_PARTICLE_SIM_DATA);
					particleSystem->system->data.dt = g_DeltaTime;
					overrides.particleSimData = &particleSystem->system->data;
					// TODO: Only do once/on edit
					UpdateDynamicUniformBuffer(particleSystem->system->simMaterialID, dynamicUBOOffset, MAT4_IDENTITY, &overrides);

					u32 dynamicOffsets[2] = { dynamicUBOOffset, 0 };
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_ParticleSimulationComputePipelineLayout, 0, 1, &particleSystem->computeDescriptorSet, ARRAY_LENGTH(dynamicOffsets), dynamicOffsets);

					vkCmdDispatch(commandBuffer, MAX_PARTICLE_COUNT / PARTICLES_PER_DISPATCH, 1, 1);

					// Add memory barrier to ensure that compute shader has finished writing to the buffer
					// Without this the (rendering) vertex shader may display incomplete results (partial data from last frame)
					bufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
					bufferBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
					bufferBarrier.srcQueueFamilyIndex = m_VulkanDevice->m_QueueFamilyIndices.computeFamily;
					bufferBarrier.dstQueueFamilyIndex = m_VulkanDevice->m_QueueFamilyIndices.graphicsFamily;
					vkCmdPipelineBarrier(
						commandBuffer,
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
						0,
						0, nullptr,
						1, &bufferBarrier,
						0, nullptr);
				}

				EndDebugMarkerRegion(commandBuffer, "End Simulate Particles");
				EndGPUTimeStamp(commandBuffer, "Simulate Particles");
			}

			BeginGPUTimeStamp(commandBuffer, "Forward");

			{
				BeginDebugMarkerRegion(commandBuffer, "Shade deferred");

				VulkanRenderObject* gBufferObject = GetRenderObject(m_GBufferQuadRenderID);

				VkDescriptorSet descriptorSet = m_DescriptorPoolPersistent->descriptorSets[gBufferObject->materialID];
				GraphicsPipeline* pipeline = GetGraphicsPipeline(gBufferObject->graphicsPipelineID)->pipeline;
				RenderFullscreenTri(commandBuffer, m_DeferredCombineRenderPass, gBufferObject->materialID,
					pipeline->layout, pipeline->pipeline, descriptorSet, true);

				EndDebugMarkerRegion(commandBuffer, "End Shade deferred");
			}

			{
				BeginDebugMarkerRegion(commandBuffer, "Copy depth");

				// TODO: Remove unnecessary transitions (use render passes where possible)

				// m_GBufferDepthAttachment was being read by SSAO, now needs to be copied to m_SwapChainDepthAttachment for forward rendering

				m_GBufferDepthAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_GraphicsQueue, commandBuffer);

				// m_OffscreenDepthAttachment0 is empty, needs to be copied into from data generated in gbuffer fill pass
				// NOTE: Handled by m_DeferredCombineRenderPass
				m_OffscreenFB0DepthAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_GraphicsQueue, commandBuffer);

				// TODO: Blit here instead when supported
				CopyImage(m_VulkanDevice, m_GraphicsQueue, m_GBufferDepthAttachment->image, m_OffscreenFB0DepthAttachment->image,
					m_SwapChainExtent.width, m_SwapChainExtent.height, commandBuffer, VK_IMAGE_ASPECT_DEPTH_BIT);

				// m_GBufferDepthAttachment has been copied out of, now must be transitioned back to read only for TAA resolve
				m_GBufferDepthAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_GraphicsQueue, commandBuffer);

				// m_OffscreenDepthAttachment0 was copied into, now will be written to in forward pass
				m_OffscreenFB0DepthAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, m_GraphicsQueue, commandBuffer);

				EndDebugMarkerRegion(commandBuffer, "End Copy depth");
			}

			std::array<VkClearValue, 2> clearValues = {};
			clearValues[0].color = m_ClearColour;
			clearValues[1].depthStencil = { 0.0f, 0 };

			//
			// Forward pass
			//

			m_ForwardRenderPass->Begin(commandBuffer, clearValues.data(), (u32)clearValues.size());
			{
				{
					BeginDebugMarkerRegion(commandBuffer, "Forward");

					for (const ShaderBatchPair& shaderBatch : m_ForwardObjectBatches.batches)
					{
						DrawShaderBatch(shaderBatch, commandBuffer);
					}

					EndDebugMarkerRegion(commandBuffer, "End Forward");
				}

				DrawParticles(commandBuffer);

				//bool bUsingGameplayCam = g_CameraManager->CurrentCamera()->bIsGameplayCam;
				if (g_EngineInstance->IsRenderingEditorObjects())// && !bUsingGameplayCam)
				{
					BeginDebugMarkerRegion(commandBuffer, "Editor objects");

					for (const ShaderBatchPair& shaderBatch : m_DepthAwareEditorObjBatches.batches)
					{
						DrawShaderBatch(shaderBatch, commandBuffer);
					}

					bool bDrawSelectedObjectWireframe = !g_Editor->HasSelectedObject();
					if (bDrawSelectedObjectWireframe || m_bEnableWireframeOverlay || m_bEnableSelectionWireframe)
					{
						BeginDebugMarkerRegion(commandBuffer, "Wireframe");

						if (m_bEnableWireframeOverlay)
						{
							// All objects wireframe

							VulkanMaterial* wireframeMaterial = (VulkanMaterial*)m_Materials[m_WireframeMatID];
							const UniformBuffer* wireframeDynamicBuffer = wireframeMaterial->uniformBufferList.Get(UniformBufferType::DYNAMIC);

							ShaderBatch* batches[] = { &m_ForwardObjectBatches, &m_DeferredObjectBatches };
							u32 dynamicUBOOffset = 0;
							for (ShaderBatch* batch : batches)
							{
								for (const ShaderBatchPair& shaderBatch : batch->batches)
								{
									if (shaderBatch.shaderID == m_SkyboxShaderID)
									{
										continue;
									}
									for (const MaterialBatchPair& matBatch : shaderBatch.batch.batches)
									{
										for (RenderID renderID : matBatch.batch.objects)
										{
											UpdateDynamicUniformBuffer(renderID, nullptr, m_WireframeMatID, dynamicUBOOffset);
											dynamicUBOOffset += RoundUp(wireframeDynamicBuffer->data.unitSize - 1, m_DynamicAlignment);
										}
									}
								}
							}

							DrawCallInfo wireframeDrawCallInfo = {};
							wireframeDrawCallInfo.bWireframe = true;
							wireframeDrawCallInfo.bCalculateDynamicUBOOffset = true;
							wireframeDrawCallInfo.dynamicUBOOffset = 0;

							for (ShaderBatch* batch : batches)
							{
								for (const ShaderBatchPair& shaderBatch : batch->batches)
								{
									if (shaderBatch.shaderID == m_SkyboxShaderID)
									{
										continue;
									}
									DrawShaderBatch(shaderBatch, commandBuffer, &wireframeDrawCallInfo);
								}
							}
						}
						else if (m_bEnableSelectionWireframe)
						{
							std::vector<GameObjectID> selectedObjectIDs = g_Editor->GetSelectedObjectIDs(true);
							std::vector<RenderID> renderIDs;
							renderIDs.reserve(selectedObjectIDs.size());

							BaseScene* currentScene = g_SceneManager->CurrentScene();
							for (const GameObjectID& selectedObjID : selectedObjectIDs)
							{
								GameObject* selectedObj = currentScene->GetGameObject(selectedObjID);
								Mesh* mesh = selectedObj->GetMesh();
								if (mesh != nullptr)
								{
									std::vector<MeshComponent*> meshes = mesh->GetSubMeshes();
									for (MeshComponent* meshComponent : meshes)
									{
										renderIDs.push_back(meshComponent->renderID);
									}
								}
							}

							ShaderBatch selectedObjectBatch = {};
							for (u32 shaderID = 0; shaderID < m_Shaders.size(); ++shaderID)
							{
								for (u32 dynamic = 0; dynamic <= 1; ++dynamic)
								{
									ShaderBatchPair shaderBatchPair = {};
									shaderBatchPair.shaderID = shaderID;
									shaderBatchPair.bDynamic = (dynamic == 1);

									ShaderBatchPair depthAwareEditorShaderBatchPair = {};
									depthAwareEditorShaderBatchPair.shaderID = shaderID;
									depthAwareEditorShaderBatchPair.bDynamic = (dynamic == 1);

									ShaderBatchPair depthUnawareEditorShaderBatchPair = {};
									depthUnawareEditorShaderBatchPair.shaderID = shaderID;
									depthUnawareEditorShaderBatchPair.bDynamic = (dynamic == 1);

									i32 inOutDynamicUBOOffset = 0;

									for (const auto& matPair : m_Materials)
									{
										MaterialID matID = matPair.first;
										const VulkanMaterial* material = (VulkanMaterial*)matPair.second;

										if (material->shaderID == shaderID && ((u32)material->bDynamic) == dynamic)
										{
											MaterialBatchPair matBatchPair = {};
											matBatchPair.materialID = matID;

											MaterialBatchPair depthAwareEditorMatBatchPair = {};
											depthAwareEditorMatBatchPair.materialID = matID;

											MaterialBatchPair depthUnawareEditorMatBatchPair = {};
											depthUnawareEditorMatBatchPair.materialID = matID;

											FillOutShaderBatches(renderIDs, &inOutDynamicUBOOffset, matBatchPair, depthAwareEditorMatBatchPair, depthUnawareEditorMatBatchPair, matID, false);

											if (!matBatchPair.batch.objects.empty())
											{
												shaderBatchPair.batch.batches.push_back(matBatchPair);
											}
											if (!depthAwareEditorMatBatchPair.batch.objects.empty())
											{
												depthAwareEditorShaderBatchPair.batch.batches.push_back(depthAwareEditorMatBatchPair);
											}
											if (!depthUnawareEditorMatBatchPair.batch.objects.empty())
											{
												depthUnawareEditorShaderBatchPair.batch.batches.push_back(depthUnawareEditorMatBatchPair);
											}
										}
									}

									if (!shaderBatchPair.batch.batches.empty())
									{
										selectedObjectBatch.batches.push_back(shaderBatchPair);
									}
									if (!depthAwareEditorShaderBatchPair.batch.batches.empty())
									{
										selectedObjectBatch.batches.push_back(depthAwareEditorShaderBatchPair);
									}
									if (!depthUnawareEditorShaderBatchPair.batch.batches.empty())
									{
										selectedObjectBatch.batches.push_back(depthUnawareEditorShaderBatchPair);
									}
								}
							}

							VulkanMaterial* wireframeMaterial = (VulkanMaterial*)m_Materials[m_WireframeMatID];
							const UniformBuffer* wireframeDynamicBuffer = wireframeMaterial->uniformBufferList.Get(UniformBufferType::DYNAMIC);
							u32 dynamicUBOOffset = 0;
							for (const ShaderBatchPair& shaderBatch : selectedObjectBatch.batches)
							{
								for (const MaterialBatchPair& matBatchPair : shaderBatch.batch.batches)
								{
									for (RenderID renderID : matBatchPair.batch.objects)
									{
										UpdateDynamicUniformBuffer(renderID, nullptr, m_WireframeMatID, dynamicUBOOffset);
										dynamicUBOOffset += RoundUp(wireframeDynamicBuffer->data.unitSize - 1, m_DynamicAlignment);
									}
								}
							}

							DrawCallInfo wireframeDrawCallInfo = {};
							wireframeDrawCallInfo.bWireframe = true;
							wireframeDrawCallInfo.bCalculateDynamicUBOOffset = true;
							wireframeDrawCallInfo.dynamicUBOOffset = 0;

							for (const ShaderBatchPair& shaderBatch : selectedObjectBatch.batches)
							{
								DrawShaderBatch(shaderBatch, commandBuffer, &wireframeDrawCallInfo);
							}
						}

						EndDebugMarkerRegion(commandBuffer, "End Wireframe");
					}

					// Depth unaware objects write to a cleared depth buffer so they
					// draw on top of previous geometry but are still eclipsed by other
					// depth unaware objects

					for (const ShaderBatchPair& shaderBatch : m_DepthUnawareEditorObjBatches.batches)
					{
						DrawShaderBatch(shaderBatch, commandBuffer);
					}

					EndDebugMarkerRegion(commandBuffer, "End Editor objects");
				}

				EnqueueWorldSpaceSprites();
				if (!m_QueuedWSSprites.empty())
				{
					BeginDebugMarkerRegion(commandBuffer, "World Space Sprites");

					DrawSpriteBatch(m_QueuedWSSprites, commandBuffer);
					m_QueuedWSSprites.clear();

					EndDebugMarkerRegion(commandBuffer, "End World Space Sprites");
				}

				EnqueueWorldSpaceText();
				{
					BeginDebugMarkerRegion(commandBuffer, "World Space Text");

					DrawText(commandBuffer, false);

					EndDebugMarkerRegion(commandBuffer, "End World Space Text");
				}
			}
			m_ForwardRenderPass->End();

			// Only needed on the first frame
			m_OffscreenFB0ColourAttachment0->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			//
			// Post process pass
			//

			{
				BeginGPUTimeStamp(commandBuffer, "Post Process");
				BeginDebugMarkerRegion(commandBuffer, "Post process");

				GraphicsPipeline* pipeline = GetGraphicsPipeline(m_PostProcessGraphicsPipelineID)->pipeline;
				RenderFullscreenTri(commandBuffer, m_PostProcessRenderPass, m_PostProcessMatID,
					pipeline->layout, pipeline->pipeline, m_PostProcessDescriptorSet, true);

				EndDebugMarkerRegion(commandBuffer, "End Post process");
				EndGPUTimeStamp(commandBuffer, "Post Process");
			}

			// Post process render pass transitioned this to shader read only optimal
			m_OffscreenFB1ColourAttachment0->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			{
				BeginDebugMarkerRegion(commandBuffer, "Gamma Correct");

				GraphicsPipeline* pipeline = GetGraphicsPipeline(m_GammaCorrectGraphicsPipelineID)->pipeline;
				RenderFullscreenTri(commandBuffer, m_GammaCorrectRenderPass, m_GammaCorrectMaterialID,
					pipeline->layout, pipeline->pipeline, m_GammaCorrectDescriptorSet, true);

				EndDebugMarkerRegion(commandBuffer, "End Gamma Correct");
			}

			if (m_bEnableTAA)
			{
				BeginGPUTimeStamp(commandBuffer, "TAA");

				BeginDebugMarkerRegion(commandBuffer, "TAA Resolve");

				VulkanMaterial* TAAMat = (VulkanMaterial*)m_Materials[m_TAAResolveMaterialID];
				TAAMat->pushConstantBlock->SetData(m_TAA_ks, sizeof(real) * 2);

				GraphicsPipeline* pipeline = GetGraphicsPipeline(m_TAAResolveGraphicsPipelineID)->pipeline;
				VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
				vkCmdPushConstants(commandBuffer, pipeline->layout, stages, 0, TAAMat->pushConstantBlock->size, TAAMat->pushConstantBlock->data);

				RenderFullscreenTri(commandBuffer, m_TAAResolveRenderPass, m_TAAResolveMaterialID,
					pipeline->layout, pipeline->pipeline, m_TAAResolveDescriptorSet, true);

				EndDebugMarkerRegion(commandBuffer, "End TAA Resolve");

				EndGPUTimeStamp(commandBuffer, "TAA");

				{
					// Should be auto-transitioned by TAA resolve pass, but isn't??
					m_OffscreenFB1ColourAttachment0->TransitionToLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_GraphicsQueue, commandBuffer);

					m_HistoryBuffer->TransitionToLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, commandBuffer);

					CopyImage(m_VulkanDevice, m_GraphicsQueue, m_OffscreenFB1ColourAttachment0->image, m_HistoryBuffer->image,
						m_SwapChainExtent.width, m_SwapChainExtent.height, commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT);

					// Transition to ready only for gamma correct pass
					m_OffscreenFB1ColourAttachment0->TransitionToLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_GraphicsQueue, commandBuffer);

					m_HistoryBuffer->TransitionToLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, commandBuffer);
				}

			}

			BeginDebugMarkerRegion(commandBuffer, "UI");
			{
				m_UIRenderPass->m_FrameBuffer->frameBuffer = m_SwapChainFramebuffers[m_CurrentSwapChainBufferIndex]->frameBuffer;
				m_UIRenderPass->Begin(commandBuffer, clearValues.data(), (u32)clearValues.size());

				GraphicsPipeline* blitPipeline = GetGraphicsPipeline(m_BlitGraphicsPipelineID)->pipeline;
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blitPipeline->pipeline);

				// Fullscreen blit from offscreen frame buffer onto swap chain
				RenderFullscreenTri(commandBuffer, m_FullscreenBlitMatID, blitPipeline->layout, m_FinalFullscreenBlitDescriptorSet);

				EnqueueScreenSpaceSprites();
				EnqueueScreenSpaceText();

				if (!m_QueuedSSPreUISprites.empty() || !m_QueuedSSArrSprites.empty())
				{
					BeginDebugMarkerRegion(commandBuffer, "Screen Space Sprites (Pre UI)");

					DrawSpriteBatch(m_QueuedSSPreUISprites, commandBuffer);
					m_QueuedSSPreUISprites.clear();
					DrawSpriteBatch(m_QueuedSSArrSprites, commandBuffer);
					m_QueuedSSArrSprites.clear();

					EndDebugMarkerRegion(commandBuffer, "End Screen Space Sprites (Pre UI)");
				}

				{
					BeginDebugMarkerRegion(commandBuffer, "Screen Space Text");

					DrawText(commandBuffer, true);

					EndDebugMarkerRegion(commandBuffer, "End Screen Space Text");
				}

				{
					BeginDebugMarkerRegion(commandBuffer, "UI Mesh");

					DrawUIMesh(m_UIMesh, commandBuffer);

					EndDebugMarkerRegion(commandBuffer, "UI Mesh");
				}

				if (!m_QueuedSSPostUISprites.empty())
				{
					BeginDebugMarkerRegion(commandBuffer, "Screen Space Sprites (Post UI)");

					DrawSpriteBatch(m_QueuedSSPostUISprites, commandBuffer);
					m_QueuedSSPostUISprites.clear();

					EndDebugMarkerRegion(commandBuffer, "End Screen Space Sprites (Post UI)");
				}

				if (g_EngineInstance->IsRenderingImGui())
				{
					BeginDebugMarkerRegion(commandBuffer, "ImGui");

					ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

					EndDebugMarkerRegion(commandBuffer, "End ImGui");
				}

				m_UIRenderPass->End();
			}

			EndDebugMarkerRegion(commandBuffer, "End UI");

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
				PrintError("Failed to acquire swap chain image!");
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

			if (m_bDiagnosticCheckpointsEnabled)
			{
				m_CheckPointAllocator.ReleaseAll(); // TODO: Test?
			}
		}

		void VulkanRenderer::BindDescriptorSet(const VulkanMaterial* material, u32 dynamicOffset, VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, VkDescriptorSet descriptorSet) const
		{
			//u32 dynamicOffset = dynamicOffsetIndex * m_DynamicAlignment;
			u32* dynamicOffsetPtr = nullptr;
			u32 dynamicOffsetCount = 0;
			const UniformBuffer* dynamicBuffer = material->uniformBufferList.Get(UniformBufferType::DYNAMIC);
			if (dynamicBuffer && dynamicBuffer->buffer.m_Size != 0)
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
			m_bSwapChainNeedsRebuilding = false;

			m_CurrentSwapChainBufferIndex = 0;

			// Avoid stalls waiting on queries that will never complete
			m_TimestampQueryNames.clear();

			const glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
			if (frameBufferSize.x == 0 || frameBufferSize.y == 0)
			{
				return;
			}

			vkDeviceWaitIdle(m_VulkanDevice->m_LogicalDevice);

			CreateSwapChain();
			CreateSwapChainImageViews();

			CreateDepthResources();

			CreateFrameBufferAttachments();
			CreateRenderPasses();
			RecreateShadowFrameBuffers();

			for (u32 i = 0; i < m_Shaders.size(); ++i)
			{
				VulkanShader* shader = (VulkanShader*)m_Shaders[i];
				shader->renderPass = ResolveRenderPassType(m_Shaders[i]->renderPassType, m_Shaders[i]->name.c_str());
			}

			for (u32 i = 0; i < m_RenderObjects.size(); ++i)
			{
				CreateGraphicsPipeline(i);
			}

			for (auto& iter : m_SpriteDescSets)
			{
				m_DescriptorPoolPersistent->FreeSet(iter.second.descSet);
				iter.second.descSet = CreateSpriteDescSet(iter.second.materialID, iter.first, iter.second.textureLayer);
			}

			CreateDescriptorSets();

			CreateSSAODescriptorSets();
			CreateSSAOPipelines();

			CreateWireframeDescriptorSets();

			CreatePostProcessingResources();
			CreateFullscreenBlitResources();
			CreateComputeResources(); // TODO: Try removing

			UpdateDynamicUniformBuffer(m_PostProcessMatID, 0, MAT4_IDENTITY, nullptr);

			// Force font descriptor sets to be regenerated
			for (BitmapFont* font : g_ResourceManager->fontsScreenSpace)
			{
				*(VkDescriptorSet*)&font->userData = VK_NULL_HANDLE;
			}

			for (BitmapFont* font : g_ResourceManager->fontsWorldSpace)
			{
				*(VkDescriptorSet*)&font->userData = VK_NULL_HANDLE;
			}

			CreateSwapChainFramebuffers();
			m_CommandBufferManager.CreateCommandBuffers((u32)m_SwapChainImages.size());
		}

		void VulkanRenderer::RegisterFramebufferAttachment(FrameBufferAttachment* frameBufferAttachment)
		{
			m_FrameBufferAttachments.emplace(frameBufferAttachment->ID, frameBufferAttachment);
		}

		FrameBufferAttachment* VulkanRenderer::GetFrameBufferAttachment(FrameBufferAttachmentID frameBufferAttachmentID) const
		{
			if (frameBufferAttachmentID == SWAP_CHAIN_COLOUR_ATTACHMENT_ID)
			{
				return m_SwapChainFramebufferAttachments[m_CurrentSwapChainBufferIndex];
			}
			if (frameBufferAttachmentID == SWAP_CHAIN_DEPTH_ATTACHMENT_ID)
			{
				return m_SwapChainDepthAttachment;
			}
			else if (frameBufferAttachmentID == SHADOW_CASCADE_DEPTH_ATTACHMENT_ID)
			{
				return m_ShadowCascades[0]->attachment;
			}
			return m_FrameBufferAttachments.at(frameBufferAttachmentID);
		}

		void VulkanRenderer::PrintMemoryUsage()
		{
			if (m_bMemoryBudgetExtensionEnabled && m_vkGetPhysicalDeviceMemoryProperties2 != nullptr)
			{
				VkPhysicalDeviceMemoryProperties2 memoryProperties;
				m_vkGetPhysicalDeviceMemoryProperties2(m_VulkanDevice->m_PhysicalDevice, &memoryProperties);
				VkPhysicalDeviceMemoryBudgetPropertiesEXT* memoryPropertiesEXT = (VkPhysicalDeviceMemoryBudgetPropertiesEXT*)memoryProperties.pNext;

				Print("%d heaps\n", VK_MAX_MEMORY_HEAPS);
				for (u32 i = 0; i < (i32)VK_MAX_MEMORY_HEAPS; ++i)
				{
					char heapBudgetBuf[64];
					char heapUsageBuf[64];
					ByteCountToString(heapBudgetBuf, 64, (u32)memoryPropertiesEXT->heapBudget[i]);
					ByteCountToString(heapUsageBuf, 64, (u32)memoryPropertiesEXT->heapUsage[i]);
					Print("Heap budget: %s\n", heapBudgetBuf);
					Print("Heap usage: %s (%.2f%%)\n", heapUsageBuf,
						(real)memoryPropertiesEXT->heapUsage[i] / memoryPropertiesEXT->heapBudget[i] * 100.0f);
				}
			}
		}

		void VulkanRenderer::GetCheckPointData()
		{
			if (m_bDiagnosticCheckpointsEnabled)
			{
				u32 checkpointCount = 0;
				vkGetQueueCheckpointDataNV(m_GraphicsQueue, &checkpointCount, nullptr);

				std::vector<VkCheckpointDataNV> data(checkpointCount);
				vkGetQueueCheckpointDataNV(m_GraphicsQueue, &checkpointCount, data.data());

				for (u32 i = 0; i < checkpointCount; ++i)
				{
					DeviceDiagnosticCheckpoint* checkpoint = (DeviceDiagnosticCheckpoint*)(data[i].pCheckpointMarker);
					if (checkpoint)
					{
						vkhpp::PipelineStageFlagBits flagBits = (vkhpp::PipelineStageFlagBits)data[i].stage;
						std::string stageStr = vkhpp::to_string(flagBits);
						PrintError("Checkpoint: %s - %s\n", stageStr.c_str(), (const char*)checkpoint->name);
					}
				}
			}
		}

		void VulkanRenderer::SetObjectName(VulkanDevice* device, u64 object, VkObjectType type, const char* name)
		{
			if (name != nullptr && m_vkSetDebugUtilsObjectNameEXT != nullptr)
			{
				VkDebugUtilsObjectNameInfoEXT info = {};
				info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
				info.objectHandle = object;
				info.objectType = type;
				info.pObjectName = name;
				VK_CHECK_RESULT(m_vkSetDebugUtilsObjectNameEXT(device->m_LogicalDevice, &info));
			}
		}

		void VulkanRenderer::SetCommandBufferName(VulkanDevice* device, VkCommandBuffer commandBuffer, const char* name)
		{
			SetObjectName(device, (u64)commandBuffer, VK_OBJECT_TYPE_COMMAND_BUFFER, name);
		}

		void VulkanRenderer::SetSwapchainName(VulkanDevice* device, VkSwapchainKHR swapchain, const char* name)
		{
			SetObjectName(device, (u64)swapchain, VK_OBJECT_TYPE_SWAPCHAIN_KHR, name);
		}

		void VulkanRenderer::SetDescriptorSetName(VulkanDevice* device, VkDescriptorSet descSet, const char* name)
		{
			SetObjectName(device, (u64)descSet, VK_OBJECT_TYPE_DESCRIPTOR_SET, name);
		}

		void VulkanRenderer::SetPipelineName(VulkanDevice* device, VkPipeline pipeline, const char* name)
		{
			SetObjectName(device, (u64)pipeline, VK_OBJECT_TYPE_PIPELINE, name);
		}

		void VulkanRenderer::SetFramebufferName(VulkanDevice* device, VkFramebuffer framebuffer, const char* name)
		{
			SetObjectName(device, (u64)framebuffer, VK_OBJECT_TYPE_FRAMEBUFFER, name);
		}

		void VulkanRenderer::SetRenderPassName(VulkanDevice* device, VkRenderPass renderPass, const char* name)
		{
			SetObjectName(device, (u64)renderPass, VK_OBJECT_TYPE_RENDER_PASS, name);
		}

		void VulkanRenderer::SetImageName(VulkanDevice* device, VkImage image, const char* name)
		{
			SetObjectName(device, (u64)image, VK_OBJECT_TYPE_IMAGE, name);
		}

		void VulkanRenderer::SetImageViewName(VulkanDevice* device, VkImageView imageView, const char* name)
		{
			SetObjectName(device, (u64)imageView, VK_OBJECT_TYPE_IMAGE_VIEW, name);
		}

		void VulkanRenderer::SetSamplerName(VulkanDevice* device, VkSampler sampler, const char* name)
		{
			SetObjectName(device, (u64)sampler, VK_OBJECT_TYPE_SAMPLER, name);
		}

		void VulkanRenderer::SetBufferName(VulkanDevice* device, VkBuffer buffer, const char* name)
		{
			SetObjectName(device, (u64)buffer, VK_OBJECT_TYPE_BUFFER, name);
		}

		void VulkanRenderer::BeginDebugMarkerRegion(VkCommandBuffer cmdBuf, const char* markerName, glm::vec4 colour /* =  = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f) */)
		{
			((VulkanRenderer*)g_Renderer)->BeginDebugMarkerRegionInternal(cmdBuf, markerName, colour);
		}

		void VulkanRenderer::EndDebugMarkerRegion(VkCommandBuffer cmdBuf, const char* markerName /* = nullptr */)
		{
			((VulkanRenderer*)g_Renderer)->EndDebugMarkerRegionInternal(cmdBuf, markerName);
		}

		void VulkanRenderer::BeginDebugMarkerRegionInternal(VkCommandBuffer cmdBuf, const char* markerName, const glm::vec4& colour)
		{
			if (m_vkCmdBeginDebugUtilsLabelEXT)
			{
				VkDebugUtilsLabelEXT labelInfo = {};
				labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
				memcpy(labelInfo.color, &colour[0], sizeof(float) * 4);
				labelInfo.pLabelName = markerName;
				m_vkCmdBeginDebugUtilsLabelEXT(cmdBuf, &labelInfo);
			}

			SetCheckPoint(cmdBuf, markerName);
		}

		void VulkanRenderer::EndDebugMarkerRegionInternal(VkCommandBuffer cmdBuf, const char* markerName /* = nullptr */)
		{
			if (m_vkCmdEndDebugUtilsLabelEXT)
			{
				m_vkCmdEndDebugUtilsLabelEXT(cmdBuf);
			}

			if (markerName != nullptr)
			{
				SetCheckPoint(cmdBuf, markerName);
			}
		}

		void VulkanRenderer::SetCheckPoint(VkCommandBuffer cmdBuf, const char* checkPointName)
		{
			if (m_bDiagnosticCheckpointsEnabled)
			{
				DeviceDiagnosticCheckpoint* checkpointData = m_CheckPointAllocator.Alloc();
				memset(checkpointData->name, 0, ARRAY_LENGTH(checkpointData->name));
				strncpy(checkpointData->name, checkPointName, std::min(strlen(checkPointName), ARRAY_LENGTH(checkpointData->name) - 1));

				vkCmdSetCheckpointNV(cmdBuf, checkpointData);
			}
		}

		bool VulkanRenderer::CreateShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule) const
		{
			VkShaderModuleCreateInfo createInfo = vks::shaderModuleCreateInfo();
			createInfo.codeSize = code.size();
			createInfo.pCode = (u32*)code.data();

			if (createInfo.codeSize == 0)
			{
				return false;
			}

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

			bool extensionsSupported = VulkanDevice::CheckDeviceSupportsExtensions(device, m_RequiredDeviceExtensions);

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

		std::vector<const char*> VulkanRenderer::GetRequiredInstanceExtensions() const
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

			// TODO: Mark up debug-only extensions to only be enabled in debug
			for (u32 i = 0; i < m_RequiredInstanceExtensions.size(); ++i)
			{
				extensions.push_back(m_RequiredInstanceExtensions[i]);
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
			BaseScene* scene = g_SceneManager->CurrentScene();
			BaseCamera* cam = g_CameraManager->CurrentCamera();
			glm::mat4 projection = cam->GetProjection();
			glm::mat4 projectionInv; // Calculated below
			glm::mat4 view = cam->GetView();
			glm::mat4 viewInv; // Calculated below
			glm::mat4 viewProjection = cam->GetViewProjection();
			glm::vec4 camPos = glm::vec4(cam->position, 0.0f);
			real exposure = cam->exposure;
			glm::vec2 nearFarPlanes(cam->zNear, cam->zFar);
			const SkyboxData& skyboxData = g_SceneManager->CurrentScene()->GetSkyboxData();
			glm::vec4 time(g_SecElapsedSinceProgramStart, scene->GetTimeOfDay(), 0.0f, 0.0f);

			static OceanData defaultOceanData = { glm::vec4(1, 0, 0, 0), glm::vec4(0, 1, 0, 0), glm::vec4(0, 0, 1, 0), 1.0f, 3.0f, 1.0f, 1.0f, 1.0f, { } };
			static DirLightData defaultDirLightData = { VEC3_RIGHT, 0, VEC3_ONE, 0.0f, 0, 0.0f, { 0.0f, 0.0f } };

			DirLightData* dirLightData = &defaultDirLightData;
			if (m_DirectionalLight != nullptr)
			{
				dirLightData = &m_DirectionalLight->data;
			}

			struct UniformInfo
			{
				UniformInfo(const Uniform& uniform, void* dataStart) :
					uniform(uniform),
					dataStart(dataStart)
				{}

				const Uniform& uniform;
				void* dataStart = nullptr;
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
			}

			viewInv = glm::inverse(view);
			projectionInv = glm::inverse(projection);

			UniformInfo uniformInfos[] = {
				{ U_CAM_POS, (void*)&camPos },
				{ U_VIEW, (void*)&view },
				{ U_VIEW_INV, (void*)&viewInv },
				{ U_VIEW_PROJECTION, (void*)&viewProjection },
				{ U_PROJECTION, (void*)&projection },
				{ U_PROJECTION_INV, (void*)&projectionInv },
				{ U_LAST_FRAME_VIEWPROJ, (void*)&m_LastFrameViewProj },
				{ U_DIR_LIGHT, (void*)dirLightData },
				{ U_LIGHTS, (void*)m_LightData },
				{ U_OCEAN_DATA, (void*)&defaultOceanData },
				{ U_SKYBOX_DATA, (void*)&skyboxData },
				{ U_TIME, (void*)&time },
				{ U_SHADOW_SAMPLING_DATA, (void*)&m_ShadowSamplingData },
				{ U_SSAO_GEN_DATA, (void*)&m_SSAOGenData },
				{ U_SSAO_BLUR_DATA_CONSTANT, (void*)&m_SSAOBlurDataConstant },
				{ U_SSAO_SAMPLING_DATA, (void*)&m_SSAOSamplingData },
				{ U_EXPOSURE, (void*)&exposure },
				{ U_NEAR_FAR_PLANES, (void*)&nearFarPlanes },
			};

			for (UniformInfo& info : uniformInfos)
			{
				auto iter = m_GlobalUserUniforms.find(info.uniform.id);
				if (iter != m_GlobalUserUniforms.end())
				{
					info.dataStart = iter->second.first;
				}
			}

			for (auto& MaterialPair : m_Materials)
			{
				VulkanMaterial* material = (VulkanMaterial*)MaterialPair.second;
				VulkanShader* shader = (VulkanShader*)m_Shaders[material->shaderID];
				UniformList& constantUniforms = shader->constantBufferUniforms;
				UniformBuffer* constantBuffer = material->uniformBufferList.Get(UniformBufferType::STATIC);

				if (constantBuffer == nullptr || constantBuffer->data.data == nullptr || constantBuffer->data.unitSize == 0)
				{
					continue; // There is no constant data to update
				}

				u32 index = 0;
				memset(constantBuffer->data.data, 0, constantBuffer->data.unitSize);
				for (UniformInfo& uniformInfo : uniformInfos)
				{
					if (constantUniforms.HasUniform(uniformInfo.uniform))
					{
						assert(uniformInfo.uniform.size != 0);

						memcpy(constantBuffer->data.data + index, uniformInfo.dataStart, uniformInfo.uniform.size);
						index += uniformInfo.uniform.size;
					}
				}

				u32 bufferUnitSize = constantBuffer->data.unitSize;

#ifdef DEBUG
				u32 calculatedUnitSize = GetAlignedUBOSize(index);
				assert(calculatedUnitSize == bufferUnitSize);
#endif

				memcpy(material->uniformBufferList.Get(UniformBufferType::STATIC)->buffer.m_Mapped, constantBuffer->data.data, bufferUnitSize);
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

		void VulkanRenderer::UpdateDynamicUniformBuffer(MaterialID materialID, u32 dynamicOffset, const glm::mat4& model,
			UniformOverrides const* uniformOverrides /* = nullptr */)
		{
			VulkanMaterial* material = (VulkanMaterial*)m_Materials.at(materialID);
			VulkanShader* shader = (VulkanShader*)m_Shaders[material->shaderID];

			UniformBufferList& uniformBufferList = material->uniformBufferList;
			UniformBuffer* dynamicBuffer = uniformBufferList.Get(UniformBufferType::DYNAMIC);

			if (dynamicBuffer == nullptr || dynamicBuffer->buffer.m_Size == 0)
			{
				return; // There are no dynamic uniforms to update
			}

			glm::vec4 colourMultiplier = material->colourMultiplier;
			u32 enableAlbedoSampler = material->enableAlbedoSampler;
			u32 enableEmissiveSampler = material->enableEmissiveSampler;
			u32 enableMetallicSampler = material->enableMetallicSampler;
			u32 enableRoughnessSampler = material->enableRoughnessSampler;
			u32 enableNormalSampler = material->enableNormalSampler;
			real textureScale = material->textureScale;
			real blendSharpness = material->blendSharpness;
			glm::vec2 texSize = material->texSize;
			glm::vec4 fontCharData = material->fontCharData;
			glm::vec4 sdfData(0.5f, -0.01f, -0.008f, 0.035f);
			i32 texChannel = 0;
			glm::vec2 uvBlendAmount = VEC2_ZERO;
			glm::mat4 postProcessMatrix = GetPostProcessingMatrix();
			ParticleSimData particleSimData = {};

			if (uniformOverrides != nullptr)
			{
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
						m_SSAOBlurDataDynamic.ssaoTexelOffset = glm::vec2(0.0f, (real)m_SSAOBlurSamplePixelOffset / m_GBufferColourAttachment0->height);
					}
					else
					{
						m_SSAOBlurDataDynamic.ssaoTexelOffset = glm::vec2((real)m_SSAOBlurSamplePixelOffset / m_GBufferColourAttachment0->width, 0.0f);
					}
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_COLOUR_MULTIPLIER))
				{
					colourMultiplier = uniformOverrides->colourMultiplier;
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_PARTICLE_SIM_DATA))
				{
					particleSimData = *uniformOverrides->particleSimData;
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_UV_BLEND_AMOUNT))
				{
					uvBlendAmount = uniformOverrides->uvBlendAmount;
				}
			}

			struct UniformInfo
			{
				const Uniform& uniform;
				void* dataStart = nullptr;
			};
			UniformInfo uniformInfos[] = {
				{ U_MODEL, (void*)&model },
				{ U_COLOUR_MULTIPLIER, (void*)&colourMultiplier },
				{ U_CONST_ALBEDO, (void*)&material->constAlbedo },
				{ U_CONST_EMISSIVE, (void*)&material->constEmissive },
				{ U_CONST_METALLIC, (void*)&material->constMetallic },
				{ U_CONST_ROUGHNESS, (void*)&material->constRoughness },
				{ U_ENABLE_ALBEDO_SAMPLER, (void*)&enableAlbedoSampler },
				{ U_ENABLE_EMISSIVE_SAMPLER, (void*)&enableEmissiveSampler },
				{ U_ENABLE_METALLIC_SAMPLER, (void*)&enableMetallicSampler },
				{ U_ENABLE_ROUGHNESS_SAMPLER, (void*)&enableRoughnessSampler },
				{ U_ENABLE_NORMAL_SAMPLER, (void*)&enableNormalSampler },
				{ U_BLEND_SHARPNESS, (void*)&blendSharpness },
				{ U_TEXTURE_SCALE, (void*)&textureScale },
				{ U_FONT_CHAR_DATA, (void*)&fontCharData },
				{ U_TEX_SIZE, (void*)&texSize  },
				{ U_SDF_DATA, (void*)&sdfData },
				{ U_TEX_CHANNEL, (void*)&texChannel },
				{ U_SSAO_BLUR_DATA_DYNAMIC, (void*)&m_SSAOBlurDataDynamic },
				{ U_POST_PROCESS_MAT, (void*)&postProcessMatrix },
				{ U_PARTICLE_SIM_DATA, (void*)&particleSimData },
				{ U_UV_BLEND_AMOUNT, (void*)&uvBlendAmount },
			};

			u32 index = 0;
			const UniformList& dynamicUniforms = shader->dynamicBufferUniforms;
			for (const UniformInfo& uniformInfo : uniformInfos)
			{
				if (dynamicUniforms.HasUniform(uniformInfo.uniform))
				{
					assert(uniformInfo.uniform.size != 0);

					// Resize buffer is not large enough
					if (dynamicOffset + index > dynamicBuffer->fullDynamicBufferSize)
					{
						VK_CHECK_RESULT(vkQueueWaitIdle(m_GraphicsQueue));
						VK_CHECK_RESULT(vkQueueWaitIdle(m_PresentQueue));

						// TODO: Untested path! May need GPU flush/dynamic UBO update here
						real growthRate = 1.5f;
						u32 newUsedSize = (u32)(glm::max(dynamicBuffer->fullDynamicBufferSize, 2u) * growthRate);
						i32 newMax = (i32)glm::ceil((real)newUsedSize / m_DynamicAlignment);
						UpdateShaderMaxObjectCount(material->shaderID, newMax);
					}

					memcpy(&dynamicBuffer->data.data[dynamicOffset + index], uniformInfo.dataStart, uniformInfo.uniform.size);
					index += uniformInfo.uniform.size;
				}
			}

			u32 bufferUnitSize = dynamicBuffer->data.unitSize;

#ifdef DEBUG
			u32 calculatedUnitSize = GetAlignedUBOSize(index);
			assert(calculatedUnitSize == bufferUnitSize);
#endif

			u64 firstIndex = (u64)dynamicBuffer->buffer.m_Mapped;
			u64 dest = firstIndex + dynamicOffset;
			memcpy((void*)(dest), &dynamicBuffer->data.data[dynamicOffset], bufferUnitSize);
		}

		void VulkanRenderer::CreateFontGraphicsPipelines()
		{
			if (m_FontSSGraphicsPipelineID == InvalidGraphicsPipelineID)
			{
				VulkanMaterial* fontMaterial = (VulkanMaterial*)m_Materials[m_FontMatSSID];
				VulkanShader* fontShader = (VulkanShader*)m_Shaders[fontMaterial->shaderID];

				GraphicsPipelineCreateInfo pipelineCreateInfo = {};
				pipelineCreateInfo.DBG_Name = "Font SS pipeline";
				pipelineCreateInfo.shaderID = fontMaterial->shaderID;
				pipelineCreateInfo.vertexAttributes = fontShader->vertexAttributes;
				pipelineCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
				pipelineCreateInfo.cullMode = VK_CULL_MODE_NONE;
				pipelineCreateInfo.descriptorSetLayoutIndex = fontMaterial->shaderID;
				pipelineCreateInfo.bSetDynamicStates = true;
				pipelineCreateInfo.bEnableColourBlending = true;
				pipelineCreateInfo.subpass = fontShader->subpass;
				pipelineCreateInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
				pipelineCreateInfo.depthWriteEnable = VK_FALSE;
				// NOTE: We ignore the font shader's render pass since we have one font shader, but
				// two passes that fonts are rendered in (Forward pass for 3D, UI pass for 2D)
				pipelineCreateInfo.renderPass = *m_UIRenderPass;
				pipelineCreateInfo.bPersistent = true;
				CreateGraphicsPipeline(&pipelineCreateInfo, m_FontSSGraphicsPipelineID);
			}

			if (m_FontWSGraphicsPipelineID == InvalidGraphicsPipelineID)
			{
				VulkanMaterial* fontMaterial = (VulkanMaterial*)m_Materials[m_FontMatWSID];
				VulkanShader* fontShader = (VulkanShader*)m_Shaders[fontMaterial->shaderID];

				GraphicsPipelineCreateInfo pipelineCreateInfo = {};
				pipelineCreateInfo.DBG_Name = "Font WS pipeline";
				pipelineCreateInfo.shaderID = fontMaterial->shaderID;
				pipelineCreateInfo.vertexAttributes = fontShader->vertexAttributes;
				pipelineCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
				pipelineCreateInfo.cullMode = VK_CULL_MODE_NONE;
				pipelineCreateInfo.descriptorSetLayoutIndex = fontMaterial->shaderID;
				pipelineCreateInfo.bSetDynamicStates = true;
				pipelineCreateInfo.bEnableColourBlending = true;
				pipelineCreateInfo.subpass = fontShader->subpass;
				pipelineCreateInfo.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
				pipelineCreateInfo.depthWriteEnable = VK_TRUE;
				// NOTE: We ignore the font shader's render pass since we have one font shader, but
				// two passes that fonts are rendered in (Forward pass for 3D, UI pass for 2D)
				pipelineCreateInfo.renderPass = *m_ForwardRenderPass;
				pipelineCreateInfo.bPersistent = true;
				CreateGraphicsPipeline(&pipelineCreateInfo, m_FontWSGraphicsPipelineID);
			}
		}

		void VulkanRenderer::GenerateIrradianceMaps()
		{
			for (u32 i = 0; i < (u32)m_RenderObjects.size(); ++i)
			{
				VulkanRenderObject* renderObject = GetRenderObject(i);
				if (renderObject == nullptr)
				{
					continue;
				}

				VulkanMaterial* renderObjectMat = (VulkanMaterial*)m_Materials.at(renderObject->materialID);

				if (renderObjectMat->generateIrradianceSampler)
				{
					GenerateCubemapFromHDR(renderObject, renderObjectMat->environmentMapPath);
					GenerateIrradianceSampler(renderObject);
					GeneratePrefilteredCube(renderObject);
				}
			}
		}

		void VulkanRenderer::RecreateEverything()
		{
			LoadShaders();

			// Recreate specialization infos
			{
				ShaderID deferredCombineShaderID = InvalidShaderID;
				GetShaderID("deferred_combine", deferredCombineShaderID);
				VulkanShader* deferredCombineShader = (VulkanShader*)m_Shaders[deferredCombineShaderID];
				deferredCombineShader->fragSpecializationInfo = GenerateSpecializationInfo({
						{ m_ShaderQualityLevelSpecializationID, sizeof(i32), (void*)&m_ShaderQualityLevel },
						{ m_ShadowCascadeCountSpecializationID, sizeof(i32), (void*)&m_ShadowCascadeCount }
					});

				ShaderID taaShaderID = m_Materials[m_TAAResolveMaterialID]->shaderID;
				VulkanShader* taaShader = (VulkanShader*)m_Shaders[taaShaderID];
				taaShader->fragSpecializationInfo = GenerateSpecializationInfo({
						{ m_TAASampleCountSpecializationID, sizeof(i32), (void*)&m_TAASampleCount }
					});

				ShaderID ssaoShaderID = m_Materials[m_SSAOMatID]->shaderID;
				VulkanShader* ssaoShader = (VulkanShader*)m_Shaders[ssaoShaderID];
				ssaoShader->fragSpecializationInfo = GenerateSpecializationInfo({
						{m_SSAOKernelSizeSpecializationID, sizeof(i32), &m_SSAOKernelSize}
					});
			}

			CreateFrameBufferAttachments();
			CreateRenderPasses();
			RecreateShadowFrameBuffers();

			for (u32 i = 0; i < m_Shaders.size(); ++i)
			{
				VulkanShader* shader = (VulkanShader*)m_Shaders[i];
				shader->renderPass = ResolveRenderPassType(shader->renderPassType, shader->name.c_str());
			}

			for (auto& materialPair : m_Materials)
			{
				CreateUniformBuffers((VulkanMaterial*)materialPair.second);
			}

			CreateShadowResources();

			DestroyNonPersistentGraphicsPipelines();

			for (u32 i = 0; i < m_RenderObjects.size(); ++i)
			{
				CreateGraphicsPipeline(i);
			}

			for (auto& iter : m_SpriteDescSets)
			{
				m_DescriptorPoolPersistent->FreeSet(iter.second.descSet);
				iter.second.descSet = CreateSpriteDescSet(iter.second.materialID, iter.first, iter.second.textureLayer);
			}

			CreateDescriptorSets();

			CreateSSAODescriptorSets();
			CreateSSAOPipelines();

			m_WireframeGraphicsPipelines.clear();
			CreateWireframeDescriptorSets();

			InitializeAllParticleSystemBuffers();

			CreatePostProcessingResources();
			CreateFullscreenBlitResources();
			CreateComputeResources();
		}

		void VulkanRenderer::SetLineWidthForCmdBuffer(VkCommandBuffer cmdBuffer, real requestedWidth /* = 3.0f */)
		{
			if (m_VulkanDevice->m_PhysicalDeviceFeatures.wideLines)
			{
				VkPhysicalDeviceLimits limits = m_VulkanDevice->m_PhysicalDeviceProperties.limits;
				real lineWidth = glm::clamp(requestedWidth - fmod(requestedWidth, limits.lineWidthGranularity), limits.lineWidthRange[0], limits.lineWidthRange[1]);
				vkCmdSetLineWidth(cmdBuffer, lineWidth);
			}
		}

		void VulkanRenderer::ImGuiUpdateTextureIndexOrMaterial(bool bUpdateTextureMaterial,
			const std::string& texturePath,
			std::string& matTexturePath,
			VulkanTexture* texture,
			i32 i,
			i32* textureIndex,
			VulkanTexture** textureToUpdate)
		{
			if (bUpdateTextureMaterial)
			{
				if (textureToUpdate != nullptr)
				{
					if (*textureIndex == 0)
					{
						matTexturePath = "";
						*textureToUpdate = nullptr;
					}
					else if (i == *textureIndex - 1)
					{
						matTexturePath = texturePath;
						if (texture != nullptr)
						{
							*textureToUpdate = texture;
						}
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

		void VulkanRenderer::BeginGPUTimeStamp(VkCommandBuffer commandBuffer, const std::string& name)
		{
			// Start counting at 1 because 0 is the default value
			const i32 queryIndex = (i32)(m_TimestampQueryNames.size() * 2) + 1;
			assert(queryIndex < (i32)MAX_TIMESTAMP_QUERIES - 2);
			m_TimestampQueryNames[name] = queryIndex;

			vkCmdResetQueryPool(commandBuffer, m_TimestampQueryPool, (u32)(queryIndex - 1), 2);
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_TimestampQueryPool, (u32)(queryIndex - 1));
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

			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_TimestampQueryPool, queryIndex);

#if THOROUGH_CHECKS
			iter->second = -queryIndex;
#endif
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

#if THOROUGH_CHECKS
			if (queryIndex > 0)
			{
				PrintError("Attempted to get duration of GPU timestamp that hasn't been ended.\n");
				return 0.0f;
			}

			if (queryIndex == 0)
			{
				PrintError("Attempted to query timestamp that hasn't been started yet.\n");
				return 0.0f;
			}

			queryIndex = -queryIndex;
#endif

			--queryIndex;

			u64 timestamps[2];
			vkGetQueryPoolResults(m_VulkanDevice->m_LogicalDevice, m_TimestampQueryPool, queryIndex, 2, sizeof(u64) * 2, timestamps, sizeof(u64), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
			return (timestamps[1] - timestamps[0]) / 1000000.0f;
		}

		VKAPI_ATTR VkBool32 VKAPI_CALL VulkanRenderer::DebugCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
			VkDebugUtilsMessageTypeFlagsEXT                  messageTypes,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void* pUserData)
		{
			FLEX_UNUSED(messageTypes);
			FLEX_UNUSED(pCallbackData);
			FLEX_UNUSED(pUserData);

			std::string msgStr = Replace(pCallbackData->pMessage, " | ", "\n\t");

			if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
			{
				PrintError("%s\n", msgStr.c_str());
			}
			else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
			{
				PrintWarn("%s\n", msgStr.c_str());
			}
			else
			{
				if (!(messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT))
				{
					Print("%s\n", msgStr.c_str());
				}
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
