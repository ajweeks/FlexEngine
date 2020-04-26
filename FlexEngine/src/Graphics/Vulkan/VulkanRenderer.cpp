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
#include "Profiler.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/LoadedMesh.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "volk/volk.h"
#include "Window/GLFWWindowWrapper.hpp"

namespace flex
{
	namespace vk
	{
		std::array<glm::mat4, 6> VulkanRenderer::s_CaptureViews;

		PFN_vkDebugMarkerSetObjectNameEXT VulkanRenderer::m_vkDebugMarkerSetObjectName = nullptr;
		PFN_vkCmdDebugMarkerBeginEXT VulkanRenderer::m_vkCmdDebugMarkerBegin = nullptr;
		PFN_vkCmdDebugMarkerEndEXT VulkanRenderer::m_vkCmdDebugMarkerEnd = nullptr;

		VulkanRenderer::VulkanRenderer() :
			m_ClearColor({ 1.0f, 0.0f, 1.0f, 1.0f }),
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

#ifdef DEBUG
			m_ShaderCompiler = new AsyncVulkanShaderCompiler(false);
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
			deviceCreateInfo.bEnableValidationLayers = m_bEnableValidationLayers;
			deviceCreateInfo.validationLayers = &m_ValidationLayers;
			m_VulkanDevice = new VulkanDevice(deviceCreateInfo);

			m_bDiagnosticCheckpointsEnabled = m_VulkanDevice->ExtensionEnabled(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);

			vkGetDeviceQueue(m_VulkanDevice->m_LogicalDevice, (u32)m_VulkanDevice->m_QueueFamilyIndices.graphicsFamily, 0, &m_GraphicsQueue);
			vkGetDeviceQueue(m_VulkanDevice->m_LogicalDevice, (u32)m_VulkanDevice->m_QueueFamilyIndices.presentFamily, 0, &m_PresentQueue);

			if (m_VulkanDevice->ExtensionEnabled(VK_EXT_DEBUG_REPORT_EXTENSION_NAME))
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

			m_DescriptorPool = { m_VulkanDevice->m_LogicalDevice, vkDestroyDescriptorPool };

			m_PresentCompleteSemaphore = { m_VulkanDevice->m_LogicalDevice, vkDestroySemaphore };
			m_RenderCompleteSemaphore = { m_VulkanDevice->m_LogicalDevice, vkDestroySemaphore };

			m_LinMipLinSampler = { m_VulkanDevice->m_LogicalDevice, vkDestroySampler };
			m_DepthSampler = { m_VulkanDevice->m_LogicalDevice, vkDestroySampler };
			m_NearestClampEdgeSampler = { m_VulkanDevice->m_LogicalDevice, vkDestroySampler };

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

			m_PostProcessGraphicsPipeline = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipeline };
			m_PostProcessGraphicsPipelineLayout = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipelineLayout };
			m_TAAResolveGraphicsPipeline = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipeline };
			m_TAAResolveGraphicsPipelineLayout = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipelineLayout };
			m_GammaCorrectGraphicsPipeline = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipeline };
			m_GammaCorrectGraphicsPipelineLayout = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipelineLayout };

			m_SpriteArrGraphicsPipeline = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipeline };
			m_SpriteArrGraphicsPipelineLayout = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipelineLayout };

			m_BlitGraphicsPipeline = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipeline };
			m_BlitGraphicsPipelineLayout = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipelineLayout };

			m_ParticleGraphicsPipelineLayout = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipelineLayout };

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

			m_GBufferColorAttachment0 = new FrameBufferAttachment(m_VulkanDevice, frameBufCreateInfo); // RGB: normal A: roughness
			m_GBufferColorAttachment1 = new FrameBufferAttachment(m_VulkanDevice, frameBufCreateInfo); // RGB: albedo A: metallic

			m_OffscreenFB0ColorAttachment0 = new FrameBufferAttachment(m_VulkanDevice, frameBufCreateInfo);

			m_OffscreenFB1ColorAttachment0 = new FrameBufferAttachment(m_VulkanDevice, frameBufCreateInfo);
			m_OffscreenFB1ColorAttachment0->bIsTransferedSrc = true;

			m_HistoryBuffer = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "History buffer", m_SwapChainExtent.width, m_SwapChainExtent.height, 4);
			m_HistoryBuffer->imageFormat = m_OffscreenFrameBufferFormat;

			frameBufCreateInfo.bIsCubemap = true;
			m_GBufferCubemapColorAttachment0 = new FrameBufferAttachment(m_VulkanDevice, frameBufCreateInfo);
			m_GBufferCubemapColorAttachment1 = new FrameBufferAttachment(m_VulkanDevice, frameBufCreateInfo);
			frameBufCreateInfo.bIsCubemap = false;

			frameBufCreateInfo.format = VK_FORMAT_R16_SFLOAT;
			m_SSAOFBColorAttachment0 = new FrameBufferAttachment(m_VulkanDevice, frameBufCreateInfo);
			m_SSAOBlurHFBColorAttachment0 = new FrameBufferAttachment(m_VulkanDevice, frameBufCreateInfo);
			m_SSAOBlurVFBColorAttachment0 = new FrameBufferAttachment(m_VulkanDevice, frameBufCreateInfo);

			m_ShadowBufFormat = VK_FORMAT_D16_UNORM;
			for (u32 i = 0; i < SHADOW_CASCADE_COUNT; ++i)
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

			CreateFrameBufferAttachments();
			CreateDepthResources();
			CreateRenderPasses();

			CreateSwapChainFramebuffers();

			PrepareCubemapFrameBuffer();
			CreateFrameBuffers();

			{
				u32 blankData = 0xFFFFFFFF;
				m_BlankTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "blank", 1, 1, 1);
				m_BlankTexture->CreateFromMemory(&blankData, sizeof(blankData), VK_FORMAT_R8G8B8A8_UNORM, 1);

				m_BlankTextureArr = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "blank_arr", 1, 1, 1);
				m_BlankTextureArr->bIsArray = true;
				m_BlankTextureArr->CreateFromMemory(&blankData, sizeof(blankData), VK_FORMAT_R8G8B8A8_UNORM, 1);
			}

			m_AlphaBGTextureID = InitializeTextureFromFile(RESOURCE_LOCATION "textures/alpha-bg.png", 4, false, false, false);
			m_LoadingTextureID = InitializeTextureFromFile(RESOURCE_LOCATION "textures/loading_1.png", 4, false, false, false);
			m_WorkTextureID = InitializeTextureFromFile(RESOURCE_LOCATION "textures/work_d.jpg", 4, false, true, false);
			m_PointLightIconID = InitializeTextureFromFile(RESOURCE_LOCATION "textures/icons/point-light-icon-256.png", 4, false, true, false);
			m_DirectionalLightIconID = InitializeTextureFromFile(RESOURCE_LOCATION "textures/icons/directional-light-icon-256.png", 4, false, true, false);

			m_SpritePerspPushConstBlock = new Material::PushConstantBlock(128);
			m_SpriteOrthoPushConstBlock = new Material::PushConstantBlock(128);
			m_SpriteOrthoArrPushConstBlock = new Material::PushConstantBlock(132);

			LoadShaders();

			m_ShadowVertexIndexBufferPair = new VertexIndexBufferPair(new VulkanBuffer(m_VulkanDevice), new VulkanBuffer(m_VulkanDevice));

			// Allocate static vertex buffers
			for (u32 shaderID = 0; shaderID < m_Shaders.size(); ++shaderID)
			{
				const u32 stride = CalculateVertexStride(m_Shaders[shaderID].shader->vertexAttributes);
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
				m_Shaders[shaderID].shader->staticVertexBufferIndex = staticVertexBufferIndex;
			}

			// Allocate dynamic vertex buffers
			for (u32 shaderID = 0; shaderID < m_Shaders.size(); ++shaderID)
			{
				const u32 stride = CalculateVertexStride(m_Shaders[shaderID].shader->vertexAttributes);
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

			m_TAA_ks[0] = 2.25f; // KL
			m_TAA_ks[1] = 100.0f; // KH

			CreateDynamicVertexAndIndexBuffers();
		}

		void VulkanRenderer::PostInitialize()
		{
			Renderer::PostInitialize();

			CreateDescriptorPool();

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

			for (u32 i = 0; i < m_Shaders.size(); ++i)
			{
				CreateDescriptorSetLayout(i);
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
					m_SSAOMatID = InitializeMaterial(&ssaoMatCreateInfo);
				}
				assert(m_SSAOMatID != InvalidMaterialID);
				m_SSAOShaderID = m_Materials[m_SSAOMatID].material.shaderID;

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
				m_SSAOBlurShaderID = m_Materials[m_SSAOBlurMatID].material.shaderID;
			}

			for (auto& matPair : m_Materials)
			{
				CreateUniformBuffers(&matPair.second);
			}

			for (u32 i = 0; i < (u32)m_RenderObjects.size(); ++i)
			{
				CreateDescriptorSet(i);
				CreateGraphicsPipeline(i, true);
			}

			GenerateBRDFLUT();

			CreateSSAOPipelines();
			CreateSSAODescriptorSets();

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
				init_info.DescriptorPool = m_DescriptorPool;
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

			CreateStaticVertexBuffers();
			CreateStaticIndexBuffers();

			CreateShadowVertexBuffer();
			CreateShadowIndexBuffer();

			// Fullscreen tri vertex data
			{
				// TODO: Bring out to Mesh class?
				void* vertData = malloc(m_FullScreenTriVertexBufferData.VertexBufferSize);
				memcpy(vertData, m_FullScreenTriVertexBufferData.vertexData, m_FullScreenTriVertexBufferData.VertexBufferSize);
				CreateAndUploadToStaticVertexBuffer(m_FullScreenTriVertexBuffer, vertData, m_FullScreenTriVertexBufferData.VertexBufferSize);
				free(vertData);
			}

			CreateSemaphores();

			InitializeFreeType();
			LoadFonts(false);

			GenerateIrradianceMaps();

			CreatePostProcessingResources();
			CreateFullscreenBlitResources();
			CreateComputeResources();

			UpdateConstantUniformBuffers();

			for (u32 i = 0; i < (u32)m_RenderObjects.size(); ++i)
			{
				UpdateDynamicUniformBuffer(i);
			}

			InitializeAllParticleSystemBuffers();

			// TODO: Move down?
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

			delete m_CascadedShadowMapPushConstantBlock;
			m_CascadedShadowMapPushConstantBlock = nullptr;

			for (const VkDescriptorSetLayout& descriptorSetLayout : m_DescriptorSetLayouts)
			{
				vkDestroyDescriptorSetLayout(m_VulkanDevice->m_LogicalDevice, descriptorSetLayout, nullptr);
			}

			m_PresentCompleteSemaphore.replace();
			m_RenderCompleteSemaphore.replace();

			for (auto& pair : m_DynamicVertexIndexBufferPairs)
			{
				pair.second->Destroy();
				delete pair.second;
			}
			m_DynamicVertexIndexBufferPairs.clear();

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
				PrintError("%u render objects were not destroyed before Vulkan render\n", activeRenderObjectCount);

				for (VulkanRenderObject* renderObject : m_RenderObjects)
				{
					if (renderObject)
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

			m_Shaders.clear();

			ClearMaterials(true);

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

			delete m_GBufferColorAttachment0;
			m_GBufferColorAttachment0 = nullptr;

			delete m_GBufferColorAttachment1;
			m_GBufferColorAttachment1 = nullptr;

			delete m_OffscreenFB0ColorAttachment0;
			m_OffscreenFB0ColorAttachment0 = nullptr;

			delete m_OffscreenFB1ColorAttachment0;
			m_OffscreenFB1ColorAttachment0 = nullptr;

			delete m_HistoryBuffer;
			m_HistoryBuffer = nullptr;

			delete m_SSAOFBColorAttachment0;
			m_SSAOFBColorAttachment0 = nullptr;

			delete m_SSAOBlurHFBColorAttachment0;
			m_SSAOBlurHFBColorAttachment0 = nullptr;

			delete m_SSAOBlurVFBColorAttachment0;
			m_SSAOBlurVFBColorAttachment0 = nullptr;

			delete m_GBufferCubemapColorAttachment0;
			m_GBufferCubemapColorAttachment0 = nullptr;

			delete m_GBufferCubemapColorAttachment1;
			m_GBufferCubemapColorAttachment1 = nullptr;

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

			for (u32 i = 0; i < SHADOW_CASCADE_COUNT; ++i)
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

			m_SSAOGraphicsPipeline.replace();
			m_SSAOBlurHGraphicsPipeline.replace();
			m_SSAOBlurVGraphicsPipeline.replace();

			m_SSAOGraphicsPipelineLayout.replace();
			m_SSAOBlurGraphicsPipelineLayout.replace();

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

			m_SpriteArrGraphicsPipeline.replace();
			m_SpriteArrGraphicsPipelineLayout.replace();

			m_BlitGraphicsPipeline.replace();
			m_BlitGraphicsPipelineLayout.replace();

			m_ParticleGraphicsPipelineLayout.replace();

			m_ParticleSimulationComputePipelineLayout.replace();

			m_gBufferQuadVertexBufferData.Destroy();
			m_DescriptorPool.replace();

			m_LinMipLinSampler.replace();
			m_DepthSampler.replace();
			m_NearestClampEdgeSampler.replace();

			delete m_BlankTextureArr;
			m_BlankTextureArr = nullptr;

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

			m_SwapChain.replace();
			m_SwapChainImageViews.clear();

			DestroyFreeType();

			vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);

			if (m_Callback)
			{
				vkDestroyDebugReportCallbackEXT(m_Instance, m_Callback, nullptr);
				m_Callback = VK_NULL_HANDLE;
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
				m_Materials.insert(std::pair<MaterialID, VulkanMaterial>(matID, VulkanMaterial()));
				m_bRebatchRenderObjects = true;
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

				return m_PlaceholderMaterialID;
			}

			VulkanShader& shader = m_Shaders[mat.material.shaderID];

			mat.material.dynamicVertexIndexBufferIndex = GetDynamicVertexIndexBufferIndex(CalculateVertexStride(shader.shader->vertexAttributes));
			mat.material.bDynamic = createInfo->bDynamic;

			if (shader.shader->constantBufferUniforms.HasUniform(U_UNIFORM_BUFFER_CONSTANT))
			{
				mat.uniformBufferList.Add(m_VulkanDevice, UniformBufferType::STATIC);
			}
			if (shader.shader->dynamicBufferUniforms.HasUniform(U_UNIFORM_BUFFER_DYNAMIC))
			{
				mat.uniformBufferList.Add(m_VulkanDevice, UniformBufferType::DYNAMIC);
			}
			if (shader.shader->additionalBufferUniforms.HasUniform(U_PARTICLE_BUFFER))
			{
				mat.uniformBufferList.Add(m_VulkanDevice, UniformBufferType::PARTICLE_DATA);
			}

			mat.material.normalTexturePath = createInfo->normalTexturePath;
			mat.material.enableNormalSampler = createInfo->enableNormalSampler;

			mat.material.sampledFrameBuffers = createInfo->sampledFrameBuffers;

			mat.material.enableCubemapSampler = createInfo->enableCubemapSampler;
			mat.material.generateCubemapSampler = createInfo->generateCubemapSampler;
			mat.material.cubemapSamplerSize = createInfo->generatedCubemapSize;
			mat.material.cubeMapFilePaths = createInfo->cubeMapFilePaths;

			mat.material.constAlbedo = glm::vec4(createInfo->constAlbedo, 0);
			mat.material.albedoTexturePath = createInfo->albedoTexturePath;
			mat.material.enableAlbedoSampler = createInfo->enableAlbedoSampler;

			mat.material.constMetallic = createInfo->constMetallic;
			mat.material.metallicTexturePath = createInfo->metallicTexturePath;
			mat.material.enableMetallicSampler = createInfo->enableMetallicSampler;

			mat.material.constRoughness = createInfo->constRoughness;
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
			if (shader.shader->bNeedBRDFLUT)
			{
				if (!m_BRDFTexture)
				{
					m_BRDFTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "BRDF", m_BRDFSize.x, m_BRDFSize.y, 1);
					m_BRDFTexture->CreateEmpty(VK_FORMAT_R16G16_SFLOAT, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
					AddLoadedTexture(m_BRDFTexture);
				}
				mat.textures.Add(U_BRDF_LUT_SAMPLER, m_BRDFTexture, "BRDF");
			}
			if (shader.shader->bNeedIrradianceSampler)
			{
				if (createInfo->irradianceSamplerMatID < m_Materials.size())
				{
					mat.textures.Add(U_IRRADIANCE_SAMPLER, m_Materials.at(createInfo->irradianceSamplerMatID).textures[U_IRRADIANCE_SAMPLER], "Irradiance");
				}
			}
			if (shader.shader->bNeedPrefilteredMap)
			{
				VulkanTexture* prefilterTexture = (createInfo->prefilterMapSamplerMatID < m_Materials.size() ? m_Materials.at(createInfo->prefilterMapSamplerMatID).textures[U_PREFILTER_MAP] : nullptr);
				mat.textures.Add(U_PREFILTER_MAP, prefilterTexture, "Prefilter");
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
					u64 textureUniform,
					const std::string& slotName,
					VkFormat format = VK_FORMAT_R8G8B8A8_UNORM,
					u32 mipLevels = 1,
					bool bHDR = false) :
					relativeFilePath(relativeFilePath),
					textureUniform(textureUniform),
					slotName(slotName),
					format(format),
					mipLevels(mipLevels),
					bHDR(bHDR)
				{}

				const std::string relativeFilePath;
				u64 textureUniform;
				const std::string slotName;
				VkFormat format;
				u32 mipLevels;
				bool bHDR;
			};

			TextureInfo textureInfos[] =
			{
				{ createInfo->albedoTexturePath, U_ALBEDO_SAMPLER, "Albedo" },
				{ createInfo->metallicTexturePath, U_METALLIC_SAMPLER , "Metallic"},
				{ createInfo->roughnessTexturePath, U_ROUGHNESS_SAMPLER, "Roughnes" },
				{ createInfo->normalTexturePath, U_NORMAL_SAMPLER, "Normal" },
				{ createInfo->hdrEquirectangularTexturePath, U_HDR_EQUIRECTANGULAR_SAMPLER, "HDR Equirectangular", VK_FORMAT_R32G32B32A32_SFLOAT, 1, true },
			};

			for (TextureInfo& textureInfo : textureInfos)
			{
				if (!textureInfo.relativeFilePath.empty())
				{
					VulkanTexture* texture = GetLoadedTexture(textureInfo.relativeFilePath);

					if (texture == nullptr)
					{
						u32 channelCount = 4;
						bool bFlipVertically = false;
						texture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue,
							textureInfo.relativeFilePath, channelCount, bFlipVertically, textureInfo.mipLevels > 1, textureInfo.bHDR);
						texture->CreateFromFile(textureInfo.format);
						texture->imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

						AddLoadedTexture(texture);
					}

					mat.textures.Add(textureInfo.textureUniform, texture, textureInfo.slotName);
				}
				else
				{
					if (!mat.textures.Contains(textureInfo.textureUniform) && shader.shader->textureUniforms.HasUniform(textureInfo.textureUniform))
					{
						mat.textures.Add(textureInfo.textureUniform, m_BlankTexture, textureInfo.slotName);
					}
				}
			}

			// Cubemaps are treated differently than regular textures because they require 6 filepaths
			if (mat.material.generateCubemapSampler)
			{
				if (createInfo->cubeMapFilePaths[0].empty())
				{
					assert(!mat.textures.Contains(U_CUBEMAP_SAMPLER));

					const u32 mipLevels = static_cast<u32>(floor(log2(createInfo->generatedCubemapSize.x))) + 1;
					u32 channelCount = 4;
					VulkanTexture* cubemapTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "Cubemap", (u32)createInfo->generatedCubemapSize.x,
						(u32)createInfo->generatedCubemapSize.y, channelCount);
					cubemapTexture->CreateCubemapEmpty(VK_FORMAT_R8G8B8A8_UNORM, mipLevels, createInfo->enableCubemapTrilinearFiltering);
					//texture->imageLayout = VK_IMAGE_LAYOUT_UNDEFINED; // TODO:Set this in creation function?

					AddLoadedTexture(cubemapTexture);
					mat.textures.Add(U_CUBEMAP_SAMPLER, cubemapTexture, "Cubemap");
				}
				else
				{
					VulkanTexture* cubemapTexture = GetLoadedTexture(createInfo->cubeMapFilePaths[0]);

					if (cubemapTexture == nullptr)
					{
						u32 channelCount = 4;
						cubemapTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue,
							createInfo->cubeMapFilePaths, channelCount, false, false, false);
						cubemapTexture->CreateCubemapFromTextures(VK_FORMAT_R8G8B8A8_UNORM, createInfo->cubeMapFilePaths, true);
						AddLoadedTexture(cubemapTexture);
					}

					mat.textures.Add(U_CUBEMAP_SAMPLER, cubemapTexture, "Cubemap");
				}
			}
			else if (mat.material.generateHDRCubemapSampler)
			{
				assert(!mat.textures.Contains(U_CUBEMAP_SAMPLER));

				const u32 mipLevels = static_cast<u32>(floor(log2(createInfo->generatedCubemapSize.x))) + 1;
				VulkanTexture* cubemapTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "HDR Cubemap",
					(u32)createInfo->generatedCubemapSize.x, (u32)createInfo->generatedCubemapSize.y, 4);
				cubemapTexture->CreateCubemapEmpty(VK_FORMAT_R32G32B32A32_SFLOAT, mipLevels, false);
				AddLoadedTexture(cubemapTexture);
				mat.textures.Add(U_CUBEMAP_SAMPLER, cubemapTexture, "HDR Cubemap");
			}
			else
			{
				if (!mat.textures.Contains(U_CUBEMAP_SAMPLER) && shader.shader->textureUniforms.HasUniform(U_CUBEMAP_SAMPLER))
				{
					mat.textures.Add(U_CUBEMAP_SAMPLER, m_BlankTextureArr, "Cubemap"); // TODO: Array?
				}
			}

			if (mat.material.generateIrradianceSampler)
			{
				assert(!mat.textures.Contains(U_IRRADIANCE_SAMPLER));

				const u32 mipLevels = static_cast<u32>(floor(log2(createInfo->generatedIrradianceCubemapSize.x))) + 1;
				VulkanTexture* irradianceTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "Irradiance sampler",
					(u32)createInfo->generatedIrradianceCubemapSize.x,
					(u32)createInfo->generatedIrradianceCubemapSize.y, 4);
				irradianceTexture->CreateCubemapEmpty(VK_FORMAT_R32G32B32A32_SFLOAT, mipLevels, false);
				AddLoadedTexture(irradianceTexture);
				mat.textures.Add(U_IRRADIANCE_SAMPLER, irradianceTexture, "Irradiance");
			}
			else
			{
				if (!mat.textures.Contains(U_IRRADIANCE_SAMPLER) && shader.shader->textureUniforms.HasUniform(U_IRRADIANCE_SAMPLER))
				{
					mat.textures.Add(U_IRRADIANCE_SAMPLER, m_BlankTexture, "Irradiance");
				}
			}

			if (mat.material.generatePrefilteredMap)
			{
				assert(!mat.textures.Contains(U_PREFILTER_MAP));

				const u32 mipLevels = static_cast<u32>(floor(log2(createInfo->generatedPrefilteredCubemapSize.x))) + 1;
				VulkanTexture* prefilterTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "Prefiltered map",
					(u32)createInfo->generatedPrefilteredCubemapSize.x,
					(u32)createInfo->generatedPrefilteredCubemapSize.y, 4);
				prefilterTexture->CreateCubemapEmpty(VK_FORMAT_R16G16B16A16_SFLOAT, mipLevels, true);
				AddLoadedTexture(prefilterTexture);
				mat.textures.Add(U_PREFILTER_MAP, prefilterTexture, "Prefilter");
			}
			else
			{
				if (!mat.textures.Contains(U_PREFILTER_MAP) && shader.shader->textureUniforms.HasUniform(U_PREFILTER_MAP))
				{
					mat.textures.Add(U_PREFILTER_MAP, m_BlankTextureArr, "Prefilter"); // TODO: Array?
				}
			}

			if (shader.shader->textureUniforms.HasUniform(U_NOISE_SAMPLER))
			{
				if (m_NoiseTexture == nullptr)
				{
					std::vector<glm::vec4> ssaoNoise;
					GenerateSSAONoise(ssaoNoise);

					m_NoiseTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, "SSAO Noise", SSAO_NOISE_DIM, SSAO_NOISE_DIM, 4);
					void* buffer = ssaoNoise.data();
					u32 bufferSize = (u32)ssaoNoise.size() * sizeof(glm::vec4);
					m_NoiseTexture->CreateFromMemory(buffer, bufferSize, VK_FORMAT_R32G32B32A32_SFLOAT, 1, VK_FILTER_NEAREST);
					AddLoadedTexture(m_NoiseTexture);
				}

				mat.textures.Add(U_NOISE_SAMPLER, m_NoiseTexture, "SSAO Noise");
			}

			if (m_bPostInitialized)
			{
				CreateUniformBuffers(&mat);
			}

			return matID;
		}

		TextureID VulkanRenderer::InitializeTextureFromFile(const std::string& relativeFilePath, i32 channelCount, bool bFlipVertically, bool bGenerateMipMaps, bool bHDR)
		{
			VulkanTexture* newTex = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, relativeFilePath,
				channelCount, bFlipVertically, bGenerateMipMaps, bHDR);
			newTex->CreateFromFile(newTex->CalculateFormat());
			TextureID textureID = AddLoadedTexture(newTex);

			return textureID;
		}

		TextureID VulkanRenderer::InitializeTextureFromMemory(void* data, u32 size, VkFormat inFormat, const std::string& name, u32 width, u32 height, u32 channelCount, VkFilter inFilter)
		{
			VulkanTexture* newTex = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, name, width, height, channelCount);
			newTex->CreateFromMemory(data, size, inFormat, 1, inFilter);
			TextureID textureID = AddLoadedTexture(newTex);

			return textureID;
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
					PrintError("Render object doesn't have its material ID set! Using placeholder material\n");
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

			if (renderObject->vertexBufferData->bDynamic)
			{
				m_DirtyFlagBits |= DirtyFlags::DYNAMIC_DATA;
			}
			else
			{
				m_DirtyFlagBits |= DirtyFlags::STATIC_DATA;
			}

			if (renderObject->gameObject->CastsShadow())
			{
				m_DirtyFlagBits |= DirtyFlags::SHADOW_DATA;
			}

			return renderID;
		}

		void VulkanRenderer::PostInitializeRenderObject(RenderID renderID)
		{
			FLEX_UNUSED(renderID);
			m_bRebatchRenderObjects = true;
		}

		void VulkanRenderer::DestroyTexture(TextureID textureID)
		{
			if (textureID < m_LoadedTextures.size())
			{
				VulkanTexture* texture = GetLoadedTexture(textureID);
				if (texture)
				{
					RemoveLoadedTexture(texture, true);

					auto spriteDescSetIter = m_SpriteDescSets.find(textureID);
					if (spriteDescSetIter != m_SpriteDescSets.end())
					{
						m_SpriteDescSets.erase(spriteDescSetIter);
					}
				}
			}
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

		void VulkanRenderer::CreateUniformBuffers(VulkanMaterial* material)
		{
			VulkanShader* shader = &m_Shaders[material->material.shaderID];

			if (shader->shader->constantBufferUniforms.HasUniform(U_UNIFORM_BUFFER_CONSTANT))
			{
				UniformBuffer* constantBuffer = material->uniformBufferList.Get(UniformBufferType::STATIC);
				constantBuffer->data.size = shader->shader->constantBufferUniforms.CalculateSizeInBytes();
				if (constantBuffer->data.size > 0)
				{
					free(constantBuffer->data.data);

					constantBuffer->data.size = GetAlignedUBOSize(constantBuffer->data.size);

					constantBuffer->data.data = static_cast<real*>(malloc(constantBuffer->data.size));
					assert(constantBuffer->data.data);

					PrepareUniformBuffer(&constantBuffer->buffer, constantBuffer->data.size,
						VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				}
			}

			if (shader->shader->dynamicBufferUniforms.HasUniform(U_UNIFORM_BUFFER_DYNAMIC))
			{
				UniformBuffer* dynamicBuffer = material->uniformBufferList.Get(UniformBufferType::DYNAMIC);
				dynamicBuffer->data.size = shader->shader->dynamicBufferUniforms.CalculateSizeInBytes();
				if (dynamicBuffer->data.size > 0 && !m_RenderObjects.empty())
				{
					flex_aligned_free(dynamicBuffer->data.data);

					dynamicBuffer->data.size = GetAlignedUBOSize(dynamicBuffer->data.size);

					const u32 dynamicBufferSize = AllocateDynamicUniformBuffer(dynamicBuffer->data.size, (void**)&dynamicBuffer->data.data);
					dynamicBuffer->fullDynamicBufferSize = dynamicBufferSize;
					if (dynamicBufferSize > 0)
					{
						PrepareUniformBuffer(&dynamicBuffer->buffer, dynamicBufferSize,
							VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
					}
				}
			}

			if (shader->shader->additionalBufferUniforms.HasUniform(U_PARTICLE_BUFFER))
			{
				UniformBuffer* particleBuffer = material->uniformBufferList.Get(UniformBufferType::PARTICLE_DATA);
				flex_aligned_free(particleBuffer->data.data);

				particleBuffer->data.size = GetAlignedUBOSize(MAX_PARTICLE_COUNT * sizeof(ParticleBufferData));

				particleBuffer->data.data = static_cast<real*>(flex_aligned_malloc(particleBuffer->data.size, m_DynamicAlignment));
				// Will be copied into from staging buffer
				PrepareUniformBuffer(&particleBuffer->buffer, particleBuffer->data.size,
					VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
			}
		}

		void VulkanRenderer::CreatePostProcessingResources()
		{
			// Post process descriptor set
			{
				VulkanMaterial* postProcessMaterial = &m_Materials[m_PostProcessMatID];
				ShaderID postProcessShaderID = postProcessMaterial->material.shaderID;
				VkDescriptorSetLayout descSetLayout = m_DescriptorSetLayouts[postProcessShaderID];

				DescriptorSetCreateInfo descSetCreateInfo = {};
				descSetCreateInfo.DBG_Name = "Post Process descriptor set";
				descSetCreateInfo.descriptorSet = &m_PostProcessDescriptorSet;
				descSetCreateInfo.descriptorSetLayout = &descSetLayout;
				descSetCreateInfo.shaderID = postProcessShaderID;
				descSetCreateInfo.uniformBufferList = &postProcessMaterial->uniformBufferList;
				descSetCreateInfo.imageDescriptors.Add(U_SCENE_SAMPLER, ImageDescriptorInfo{ m_OffscreenFB0ColorAttachment0->view, m_LinMipLinSampler });
				FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
				CreateDescriptorSet(&descSetCreateInfo);
			}

			// Gamma Correct descriptor set
			{
				VulkanMaterial* gammaCorrectMaterial = &m_Materials[m_GammaCorrectMaterialID];
				ShaderID gammaCorrectShaderID = gammaCorrectMaterial->material.shaderID;
				VkDescriptorSetLayout descSetLayout = m_DescriptorSetLayouts[gammaCorrectShaderID];

				DescriptorSetCreateInfo descSetCreateInfo = {};
				descSetCreateInfo.DBG_Name = "Gamma Correct descriptor set";
				descSetCreateInfo.descriptorSet = &m_GammaCorrectDescriptorSet;
				descSetCreateInfo.descriptorSetLayout = &descSetLayout;
				descSetCreateInfo.shaderID = gammaCorrectShaderID;
				descSetCreateInfo.uniformBufferList = &gammaCorrectMaterial->uniformBufferList;
				descSetCreateInfo.imageDescriptors.Add(U_SCENE_SAMPLER, ImageDescriptorInfo{ m_OffscreenFB1ColorAttachment0->view, m_LinMipLinSampler });
				FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
				CreateDescriptorSet(&descSetCreateInfo);
			}

			// TAA Resolve descriptor set
			{
				VulkanMaterial* taaResolveMaterial = &m_Materials[m_TAAResolveMaterialID];
				ShaderID taaResolveShaderID = taaResolveMaterial->material.shaderID;
				VkDescriptorSetLayout descSetLayout = m_DescriptorSetLayouts[taaResolveShaderID];

				DescriptorSetCreateInfo descSetCreateInfo = {};
				descSetCreateInfo.DBG_Name = "TAA Resolve descriptor set";
				descSetCreateInfo.descriptorSet = &m_TAAResolveDescriptorSet;
				descSetCreateInfo.descriptorSetLayout = &descSetLayout;
				descSetCreateInfo.shaderID = taaResolveShaderID;
				descSetCreateInfo.uniformBufferList = &taaResolveMaterial->uniformBufferList;
				descSetCreateInfo.imageDescriptors.Add(U_DEPTH_SAMPLER, ImageDescriptorInfo{ m_GBufferDepthAttachment->view, m_DepthSampler });
				descSetCreateInfo.imageDescriptors.Add(U_SCENE_SAMPLER, ImageDescriptorInfo{ m_OffscreenFB0ColorAttachment0->view, m_LinMipLinSampler });
				descSetCreateInfo.imageDescriptors.Add(U_HISTORY_SAMPLER, ImageDescriptorInfo{ m_HistoryBuffer->imageView, m_LinMipLinSampler });
				FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
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
				createInfo.DBG_Name = "Sprite Array pipeline";
				createInfo.graphicsPipeline = m_SpriteArrGraphicsPipeline.replace();
				createInfo.pipelineLayout = &m_SpriteArrGraphicsPipelineLayout;
				createInfo.renderPass = *m_UIRenderPass;
				createInfo.shaderID = spriteArrMat.material.shaderID;
				createInfo.vertexAttributes = m_Quad3DVertexBufferData.Attributes;
				createInfo.descriptorSetLayoutIndex = spriteArrMat.material.shaderID;
				createInfo.bEnableColorBlending = true;
				createInfo.depthTestEnable = VK_FALSE;
				createInfo.depthWriteEnable = VK_FALSE;
				createInfo.pushConstantRangeCount = (u32)pushConstantRanges.size();
				createInfo.pushConstants = pushConstantRanges.data();
				CreateGraphicsPipeline(&createInfo);
			}

			// Post process pipeline
			{
				VulkanMaterial& postProcessMat = m_Materials[m_PostProcessMatID];

				GraphicsPipelineCreateInfo createInfo = {};
				createInfo.DBG_Name = "Post Process pipeline";
				createInfo.graphicsPipeline = m_PostProcessGraphicsPipeline.replace();
				createInfo.pipelineLayout = m_PostProcessGraphicsPipelineLayout.replace();
				createInfo.renderPass = *m_PostProcessRenderPass;
				createInfo.shaderID = postProcessMat.material.shaderID;
				createInfo.vertexAttributes = m_FullScreenTriVertexBufferData.Attributes;
				createInfo.descriptorSetLayoutIndex = postProcessMat.material.shaderID;
				createInfo.bSetDynamicStates = true;
				createInfo.depthTestEnable = VK_FALSE;
				createInfo.depthWriteEnable = VK_FALSE;
				CreateGraphicsPipeline(&createInfo);
			}

			// Gamma Correct pipeline
			{
				VulkanMaterial& gammaCorrectMat = m_Materials[m_GammaCorrectMaterialID];

				GraphicsPipelineCreateInfo createInfo = {};
				createInfo.DBG_Name = "Gamma Correct pipeline";
				createInfo.graphicsPipeline = m_GammaCorrectGraphicsPipeline.replace();
				createInfo.pipelineLayout = m_GammaCorrectGraphicsPipelineLayout.replace();
				createInfo.renderPass = *m_GammaCorrectRenderPass;
				createInfo.shaderID = gammaCorrectMat.material.shaderID;
				createInfo.vertexAttributes = m_FullScreenTriVertexBufferData.Attributes;
				createInfo.descriptorSetLayoutIndex = gammaCorrectMat.material.shaderID;
				createInfo.bSetDynamicStates = true;
				createInfo.depthTestEnable = VK_FALSE;
				createInfo.depthWriteEnable = VK_FALSE;
				CreateGraphicsPipeline(&createInfo);
			}

			// TAA Resolve pipeline
			{
				VulkanMaterial& taaResolveMat = m_Materials[m_TAAResolveMaterialID];

				std::array<VkPushConstantRange, 1> pushConstantRanges = {};
				pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
				pushConstantRanges[0].offset = 0;
				pushConstantRanges[0].size = m_Shaders[taaResolveMat.material.shaderID].shader->pushConstantBlockSize;

				GraphicsPipelineCreateInfo createInfo = {};
				createInfo.DBG_Name = "TAA Resolve pipeline";
				createInfo.graphicsPipeline = m_TAAResolveGraphicsPipeline.replace();
				createInfo.pipelineLayout = m_TAAResolveGraphicsPipelineLayout.replace();
				createInfo.renderPass = *m_TAAResolveRenderPass;
				createInfo.shaderID = taaResolveMat.material.shaderID;
				createInfo.vertexAttributes = m_FullScreenTriVertexBufferData.Attributes;
				createInfo.descriptorSetLayoutIndex = taaResolveMat.material.shaderID;
				createInfo.bSetDynamicStates = true;
				createInfo.depthTestEnable = VK_FALSE;
				createInfo.depthWriteEnable = VK_FALSE;
				createInfo.fragSpecializationInfo = &m_TAAOSpecializationInfo;
				createInfo.pushConstantRangeCount = (u32)pushConstantRanges.size();
				createInfo.pushConstants = pushConstantRanges.data();
				CreateGraphicsPipeline(&createInfo);
			}
		}

		void VulkanRenderer::CreateFullscreenBlitResources()
		{
			VulkanMaterial* fullscreenBlitMat = &m_Materials[m_FullscreenBlitMatID];
			ShaderID fullscreenShaderID = fullscreenBlitMat->material.shaderID;
			VulkanShader* fullscreenShader = &m_Shaders[fullscreenShaderID];

			{
				VkDescriptorSetLayout descSetLayout = m_DescriptorSetLayouts[fullscreenShaderID];

				DescriptorSetCreateInfo descSetCreateInfo = {};
				descSetCreateInfo.DBG_Name = "Fullscreen blit descriptor set";
				descSetCreateInfo.descriptorSet = &m_FinalFullscreenBlitDescriptorSet;
				descSetCreateInfo.descriptorSetLayout = &descSetLayout;
				descSetCreateInfo.shaderID = fullscreenShaderID;
				descSetCreateInfo.uniformBufferList = &fullscreenBlitMat->uniformBufferList;
				FrameBufferAttachment* sceneFrameBufferAttachment = m_bEnableTAA ? m_OffscreenFB1ColorAttachment0 : m_OffscreenFB0ColorAttachment0;
				descSetCreateInfo.imageDescriptors.Add(U_ALBEDO_SAMPLER, ImageDescriptorInfo{ sceneFrameBufferAttachment->view, m_LinMipLinSampler });
				FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
				CreateDescriptorSet(&descSetCreateInfo);
			}

			{
				GraphicsPipelineCreateInfo pipelineCreateInfo = {};
				pipelineCreateInfo.DBG_Name = "Blit pipeline";
				pipelineCreateInfo.bSetDynamicStates = true;
				pipelineCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
				pipelineCreateInfo.cullMode = VK_CULL_MODE_NONE;
				pipelineCreateInfo.bEnableColorBlending = false;

				pipelineCreateInfo.graphicsPipeline = m_BlitGraphicsPipeline.replace();
				pipelineCreateInfo.pipelineLayout = m_BlitGraphicsPipelineLayout.replace();
				pipelineCreateInfo.shaderID = fullscreenBlitMat->material.shaderID;
				pipelineCreateInfo.vertexAttributes = fullscreenShader->shader->vertexAttributes;
				pipelineCreateInfo.descriptorSetLayoutIndex = fullscreenBlitMat->descriptorSetLayoutIndex;
				pipelineCreateInfo.subpass = fullscreenShader->shader->subpass;
				pipelineCreateInfo.depthWriteEnable = VK_FALSE;
				pipelineCreateInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
				pipelineCreateInfo.renderPass = fullscreenShader->renderPass;
				CreateGraphicsPipeline(&pipelineCreateInfo);
			}
		}

		void VulkanRenderer::CreateComputeResources()
		{
			for (VulkanParticleSystem* particleSystem : m_ParticleSystems)
			{
				if (!particleSystem)
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
				VulkanMaterial* particleSimulationMaterial = &m_Materials.at(particleSystem->system->simMaterialID);
				VulkanShader* particleSimulationShader = &m_Shaders[particleSimulationMaterial->material.shaderID];

				// Particle simulation descriptor set
				VkDescriptorSetLayout descSetLayout = m_DescriptorSetLayouts[particleSimulationMaterial->material.shaderID];

				DescriptorSetCreateInfo descSetCreateInfo = {};
				std::string descSetName = "Particle simulation descriptor set " + idStr;
				descSetCreateInfo.DBG_Name = descSetName.c_str();
				descSetCreateInfo.descriptorSet = &particleSystem->computeDescriptorSet;
				descSetCreateInfo.descriptorSetLayout = &descSetLayout;
				descSetCreateInfo.shaderID = particleSimulationMaterial->material.shaderID;
				descSetCreateInfo.uniformBufferList = &particleSimulationMaterial->uniformBufferList;
				FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
				CreateDescriptorSet(&descSetCreateInfo);

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
				VulkanMaterial* particleRenderingMaterial = &m_Materials.at(particleSystem->system->renderingMaterialID);
				VulkanShader* particleRenderingShader = &m_Shaders[particleRenderingMaterial->material.shaderID];

				VkDescriptorSetLayout descSetLayout = m_DescriptorSetLayouts[particleRenderingMaterial->material.shaderID];

				// Particles descriptor set
				DescriptorSetCreateInfo descSetCreateInfo = {};
				std::string descSetName = "Particle rendering descriptor set " + idStr;
				descSetCreateInfo.DBG_Name = descSetName.c_str();
				descSetCreateInfo.descriptorSet = &particleSystem->renderingDescriptorSet;
				descSetCreateInfo.descriptorSetLayout = &descSetLayout;
				descSetCreateInfo.shaderID = particleRenderingMaterial->material.shaderID;
				descSetCreateInfo.uniformBufferList = &particleRenderingMaterial->uniformBufferList;

				VulkanTexture* texture = GetLoadedTexture(m_AlphaBGTextureID);
				descSetCreateInfo.imageDescriptors.Add(U_ALBEDO_SAMPLER, ImageDescriptorInfo{ texture->imageView, m_LinMipLinSampler });

				FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
				CreateDescriptorSet(&descSetCreateInfo);

				// Particles graphics pipeline
				// Particle simulation compute pipeline
				if (m_ParticleGraphicsPipelineLayout == VK_NULL_HANDLE)
				{
					VkPipelineLayoutCreateInfo pipelineLayoutInfo = vks::pipelineLayoutCreateInfo(1, &descSetLayout);
					VK_CHECK_RESULT(vkCreatePipelineLayout(m_VulkanDevice->m_LogicalDevice, &pipelineLayoutInfo, nullptr, m_ParticleGraphicsPipelineLayout.replace()));
				}

				GraphicsPipelineCreateInfo pipelineCreateInfo = {};
				std::string pipelineName = "Particle rendering graphics pipeline " + idStr;
				pipelineCreateInfo.DBG_Name = pipelineName.c_str();
				pipelineCreateInfo.graphicsPipeline = particleSystem->graphicsPipeline.replace();
				pipelineCreateInfo.pipelineLayout = &m_ParticleGraphicsPipelineLayout;
				pipelineCreateInfo.shaderID = particleRenderingMaterial->material.shaderID;
				pipelineCreateInfo.vertexAttributes = particleRenderingShader->shader->vertexAttributes;
				pipelineCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
				pipelineCreateInfo.cullMode = VK_CULL_MODE_NONE;
				pipelineCreateInfo.descriptorSetLayoutIndex = particleRenderingMaterial->material.shaderID;
				pipelineCreateInfo.bSetDynamicStates = true;
				pipelineCreateInfo.bEnableColorBlending = particleRenderingShader->shader->bTranslucent;
				pipelineCreateInfo.subpass = particleRenderingShader->shader->subpass;
				pipelineCreateInfo.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
				pipelineCreateInfo.depthWriteEnable = particleRenderingShader->shader->bDepthWriteEnable ? VK_TRUE : VK_FALSE;
				pipelineCreateInfo.renderPass = *m_ForwardRenderPass;
				CreateGraphicsPipeline(&pipelineCreateInfo);
			}
		}

		void VulkanRenderer::Update()
		{
			Renderer::Update();

			// NOTE: This doesn't respect TAA jitter!
			m_SpritePerspPushConstBlock->SetData(g_CameraManager->CurrentCamera()->GetView(), g_CameraManager->CurrentCamera()->GetProjection());

#ifdef DEBUG
			if (m_ShaderCompiler && m_ShaderCompiler->TickStatus())
			{
				if (m_ShaderCompiler->bSuccess)
				{
					OnShaderReloadSuccess();
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

				CreatePostProcessingResources();
				CreateFullscreenBlitResources();
			}

			m_PhysicsDebugDrawer->UpdateDebugMode();

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

			if (!m_PhysicsDebuggingSettings.bDisableAll)
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
							VulkanShader& shader = m_Shaders[shaderBatchPair.shaderID];

							for (u32 k = 0; k < shaderBatchPair.batch.batches.size(); ++k)
							{
								MaterialBatchPair& materialBatchPair = shaderBatchPair.batch.batches[k];
								VulkanMaterial& material = m_Materials.at(materialBatchPair.materialID);

								if (material.uniformBufferList.Has(UniformBufferType::DYNAMIC) &&
									material.uniformBufferList.Get(UniformBufferType::DYNAMIC)->fullDynamicBufferSize > 0)
								{
									char nodeID0[256];
									memset(nodeID0, 0, 256);
									snprintf(nodeID0, 256, "%s##%u",
										shader.shader->name.c_str(),
										shaderBatchPair.shaderID);
									if (ImGui::BeginChild(nodeID0, ImVec2(0, 200), true))
									{
										u32 dynamicObjectCount = 0;

										for (u32 l = 0; l < shaderBatchPair.batch.batches.size(); ++l)
										{
											const MaterialBatchPair& matBatchPair = shaderBatchPair.batch.batches[l];
											for (RenderID renderID : matBatchPair.batch.objects)
											{
												VulkanRenderObject* renderObject = GetRenderObject(renderID);
												if (renderObject != nullptr)
												{
													++dynamicObjectCount;
												}
											}
										}

										UniformBuffer* dynamicBuffer = material.uniformBufferList.Get(UniformBufferType::DYNAMIC);
										u32 bufferSlotsTotal = (dynamicBuffer->fullDynamicBufferSize / dynamicBuffer->data.size);
										u32 bufferSlotsFree = bufferSlotsTotal - dynamicObjectCount;

										char histNodeID[256];
										memset(histNodeID, 0, 256);
										snprintf(histNodeID, 256, "%s (%u/%u)##histo%u",
											shader.shader->name.c_str(),
											bufferSlotsTotal - bufferSlotsFree,
											bufferSlotsTotal,
											shaderBatchPair.shaderID);
										real progress = 1.0f - (bufferSlotsFree / (real)bufferSlotsTotal);
										ImGui::ProgressBar(progress, ImVec2(0, 0), histNodeID);
									}
									ImGui::EndChild();
								}
							}
						}
					}

					ImGui::Text("Particle buffers");

					if (m_ParticleSystems.size() == 0)
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
								VulkanMaterial& simMat = m_Materials[system->system->simMaterialID];
								if (simMat.uniformBufferList.Has(UniformBufferType::PARTICLE_DATA))
								{
									UniformBuffer* particleBuffer = simMat.uniformBufferList.Get(UniformBufferType::PARTICLE_DATA);
									u32 bufferSlotsTotal = particleBuffer->data.size;

									char histNodeID[256];
									memset(histNodeID, 0, 256);
									snprintf(histNodeID, 256, "%s %uB##particles_size",
										simMat.material.name.c_str(),
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
		}

		void VulkanRenderer::UpdateVertexData(RenderID renderID, VertexBufferData const* vertexBufferData, const std::vector<u32>& indexData)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);

			VulkanMaterial& mat = m_Materials.at(renderObject->materialID);
			VertexIndexBufferPair* vertexIndexBufferPair = m_DynamicVertexIndexBufferPairs[mat.material.dynamicVertexIndexBufferIndex].second;
			VulkanBuffer* vertexBuffer = vertexIndexBufferPair->vertexBuffer;
			VulkanBuffer* indexBuffer = vertexIndexBufferPair->indexBuffer;

			if (renderObject->dynamicVertexBufferOffset == u64_max)
			{
				// First upload of data
				renderObject->dynamicVertexBufferOffset = vertexBuffer->Alloc((VkDeviceSize)vertexBufferData->VertexBufferSize, true);
				renderObject->dynamicIndexBufferOffset = indexBuffer->Alloc((VkDeviceSize)(indexData.size() * sizeof(u32)), true);
			}

			u32 vertCopySize = std::min(vertexBufferData->VertexBufferSize, (u32)vertexBuffer->m_Size);
			u32 indexCopySize = std::min((u32)(indexData.size() * sizeof(u32)), (u32)indexBuffer->m_Size);
			if (vertCopySize < vertexBufferData->VertexBufferSize)
			{
				PrintError("Dynamic vertex buffer is %u bytes too small for data attempting to be copied in\n", vertexBufferData->VertexBufferSize - vertCopySize);
			}
			VkDeviceSize vertOffset = renderObject->dynamicVertexBufferOffset;
			VkDeviceSize indexOffset = renderObject->dynamicIndexBufferOffset;

			assert((vertOffset + vertCopySize) <= vertexBuffer->m_Size);
			assert((indexOffset + indexCopySize) <= indexBuffer->m_Size);

			VK_CHECK_RESULT(vertexBuffer->Map(vertOffset, vertCopySize));
			VK_CHECK_RESULT(indexBuffer->Map(indexOffset, indexCopySize));
			memcpy(vertexBuffer->m_Mapped, vertexBufferData->vertexData, vertCopySize);
			memcpy(indexBuffer->m_Mapped, indexData.data(), indexCopySize);
			vertexBuffer->Unmap();
			indexBuffer->Unmap();


			renderObject->vertexOffset = (u32)vertOffset;
			renderObject->indexOffset = (u32)indexOffset;

			m_DirtyFlagBits |= DirtyFlags::DYNAMIC_DATA; // TODO: Is this needed?
		}

		void VulkanRenderer::DrawImGuiForRenderObject(RenderID renderID)
		{
			FLEX_UNUSED(renderID);
		}

		void VulkanRenderer::RecompileShaders(bool bForce)
		{
#ifdef DEBUG
			if (m_ShaderCompiler == nullptr)
			{
				m_ShaderCompiler = new AsyncVulkanShaderCompiler(bForce);

				if (m_ShaderCompiler->bSuccess)
				{
					OnShaderReloadSuccess();
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

				const std::string fontName = StripFileType(StripLeadingDirectories(fontMetaData.filePath));
				LoadFont(fontMetaData, bForceRender);
			}
		}

		void VulkanRenderer::ReloadSkybox(bool bRandomizeTexture)
		{
			FLEX_UNUSED(bRandomizeTexture);
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
			FLEX_UNUSED(width);
			FLEX_UNUSED(height);

			if (width != 0 && height != 0)
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
			GenerateGBuffer();

			for (auto& pair : m_DynamicVertexIndexBufferPairs)
			{
				pair.second->Empty();
			}
		}

		void VulkanRenderer::OnPostSceneChange()
		{
			Renderer::OnPostSceneChange();

			if (m_bPostInitialized)
			{
				CreateStaticVertexBuffers();
				CreateStaticIndexBuffers();
				CreateDynamicVertexAndIndexBuffers();

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
			m_bSwapChainNeedsRebuilding = true;
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

			MaterialID skyboxMatierialID = m_SkyBoxMesh->GetSubMeshes()[0]->GetMaterialID();
			if (skyboxMatierialID == InvalidMaterialID)
			{
				PrintError("Skybox doesn't have a valid material! Irradiance textures can't be generated\n");
				return;
			}
			VulkanMaterial& skyboxMaterial = m_Materials.at(skyboxMatierialID);

			for (u32 i = 0; i < m_RenderObjects.size(); ++i)
			{
				VulkanRenderObject* renderObject = GetRenderObject(i);
				if (renderObject)
				{
					auto matIter = m_Materials.find(renderObject->materialID);
					if (matIter != m_Materials.end())
					{
						if (m_Shaders[matIter->second.material.shaderID].shader->bNeedPrefilteredMap)
						{
							VulkanMaterial& renderObjectMat = matIter->second;
							renderObjectMat.textures.Add(U_IRRADIANCE_SAMPLER, skyboxMaterial.textures[U_IRRADIANCE_SAMPLER], "Irradiance");
							renderObjectMat.textures.Add(U_PREFILTER_MAP, skyboxMaterial.textures[U_PREFILTER_MAP]);
						}
					}
					else
					{
						PrintError("Skybox's material doesn't exist!\n");
					}
				}
			}

			m_bRebatchRenderObjects = true;
		}

		void VulkanRenderer::SetRenderObjectMaterialID(RenderID renderID, MaterialID materialID)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (renderObject)
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
							u32 prevStride = CalculateVertexStride(m_Shaders[m_Materials.at(prevMatID).material.shaderID].shader->vertexAttributes);
							u32 newStride = CalculateVertexStride(m_Shaders[m_Materials.at(materialID).material.shaderID].shader->vertexAttributes);
							if (newStride != prevStride)
							{
								submesh->SetRequiredAttributesFromMaterialID(materialID);
							}
							submesh->GetOwner()->Reload();
						}
						else
						{
							PrintWarn("Attempted to set material ID on object with no submesh with renderID %u\n", renderID);
						}
					}
					else
					{
						PrintWarn("Attempted to set material ID on object with no mesh\n");
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
			return m_Materials.at(materialID).material;
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

		void VulkanRenderer::NewFrame()
		{
			if (m_PhysicsDebugDrawer)
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
					static bool bMaterialSelectionChanged = true;
					const i32 MAX_NAME_LEN = 128;
					static i32 selectedMaterialIndexShort = 0; // Index into shortened array
					static MaterialID selectedMaterialID = 0;
					while (!m_Materials.at(selectedMaterialID).material.visibleInEditor &&
						selectedMaterialID < m_Materials.size() - 1)
					{
						++selectedMaterialID;
					}
					static std::string matName = "";
					static i32 selectedShaderIndex = 0;
					// Texture index values of 0 represent no texture, 1 = first index into textures array and so on
					//static i32 albedoTextureIndex = 0;
					static std::vector<i32> selectedTextureIndices; // One for each of the current material's texture slots
					//static bool bUpdateAlbedoTextureMaterial = false;
					VulkanMaterial& mat = m_Materials.at(selectedMaterialID);

					if (bMaterialSelectionChanged)
					{
						bMaterialSelectionChanged = false;

						matName = mat.material.name;
						matName.resize(MAX_NAME_LEN);

						selectedTextureIndices.resize(mat.textures.Count());

						i32 texIndex = 0;
						for (auto& pair : mat.textures)
						{
							for (u32 loadedTexIndex = 0; loadedTexIndex < m_LoadedTextures.size(); ++loadedTexIndex)
							{
								// TODO(AJ): Compare IDs
								if (pair.second == GetLoadedTexture(loadedTexIndex))
								{
									selectedTextureIndices[texIndex] = loadedTexIndex;
								}
							}
							++texIndex;
						}
					}
					else if (bUpdateFields)
					{
						bUpdateFields = false;


						for (u32 texIndex = 0; texIndex < mat.textures.Count(); ++texIndex)
						{
							mat.textures.values[texIndex].second = GetLoadedTexture(selectedTextureIndices[texIndex]);
						}


						i32 i = 0;
						for (VulkanTexture* texture : m_LoadedTextures)
						{
							std::string texturePath = texture->GetRelativeFilePath();
							std::string textureName = StripLeadingDirectories(texturePath);

							//ImGuiUpdateTextureIndexOrMaterial(bUpdateAlbedoTextureMaterial,
							//	texturePath,
							//	mat.material.albedoTexturePath,
							//	texture,
							//	i,
							//	&albedoTextureIndex,
							//	U_ALBEDO_SAMPLER);

							//ImGuiUpdateTextureIndexOrMaterial(bUpdateMetallicTextureMaterial,
							//	texturePath,
							//	mat.material.metallicTexturePath,
							//	texture,
							//	i,
							//	&metallicTextureIndex,
							//	&mat.metallicTexture);

							//ImGuiUpdateTextureIndexOrMaterial(bUpdateRoughessTextureMaterial,
							//	texturePath,
							//	mat.material.roughnessTexturePath,
							//	texture,
							//	i,
							//	&roughnessTextureIndex,
							//	&mat.roughnessTexture);

							//ImGuiUpdateTextureIndexOrMaterial(bUpdateNormalTextureMaterial,
							//	texturePath,
							//	mat.material.normalTexturePath,
							//	texture,
							//	i,
							//	&normalTextureIndex,
							//	&mat.normalTexture);

							++i;
						}

						//mat.material.enableAlbedoSampler = (albedoTextureIndex > 0);
						//mat.material.enableMetallicSampler = (metallicTextureIndex > 0);
						//mat.material.enableRoughnessSampler = (roughnessTextureIndex > 0);
						//mat.material.enableNormalSampler = (normalTextureIndex > 0);

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
					if (ImGui::Combo("Shader", &selectedShaderIndex, &ShaderFunctor::GetShaderName, (void*)m_Shaders.data(), (u32)m_Shaders.size()))
					{
						mat = m_Materials.at(selectedMaterialID);
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

					for (u32 texIndex = 0; texIndex < mat.textures.Count(); ++texIndex)
					{
						// TODO: Pass in reference to mat.textures?
						//VulkanTexture* texture = mat.textures[texIndex];
						std::string texFieldName = mat.textures.slotNames[texIndex] + "##" + std::to_string(texIndex);
						bUpdateFields |= DoTextureSelector(texFieldName.c_str(), textures, &selectedTextureIndices[texIndex]);
					}

					ImGui::NewLine();

					ImGui::EndColumns();

					if (ImGui::BeginChild("material list", ImVec2(0.0f, 120.0f), true))
					{
						i32 matShortIndex = 0;
						for (i32 i = 0; i < (i32)m_Materials.size(); ++i)
						{
							auto matIter = m_Materials.find(i);
							if (matIter == m_Materials.end() || !matIter->second.material.visibleInEditor)
							{
								continue;
							}

							VulkanMaterial& material = matIter->second;

							bool bSelected = (matShortIndex == selectedMaterialIndexShort);
							if (ImGui::Selectable(material.material.name.c_str(), &bSelected))
							{
								if (selectedMaterialIndexShort != matShortIndex)
								{
									selectedMaterialIndexShort = matShortIndex;
									selectedMaterialID = i;
									bUpdateFields = true;
									bMaterialSelectionChanged = true;
								}
							}

							if (ImGui::BeginPopupContextItem())
							{
								if (ImGui::Button("Duplicate"))
								{
									const Material& dupMat = material.material;

									MaterialCreateInfo createInfo = {};
									createInfo.name = GetIncrementedPostFixedStr(dupMat.name, "new material");
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

									ImGui::SetDragDropPayload(MaterialPayloadCStr, data, size);

									ImGui::Text("%s", material.material.name.c_str());

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

							bool bMaterialNameExists = false;
							for (const auto& matPair : m_Materials)
							{
								if (matPair.second.material.name.compare(newMaterialName) == 0)
								{
									bMaterialNameExists = true;
									break;
								}
							}

							if (bMaterialNameExists)
							{
								newMaterialName = GetIncrementedPostFixedStr(newMaterialName, "new material");
							}

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
						bMaterialSelectionChanged = true;
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
							std::string textureName = texture->GetName();
							if (ImGui::Selectable(textureName.c_str(), &bSelected))
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

					if (ImGui::Button("Import Texture"))
					{
						// TODO: Not all textures are directly in this directory! CLEANUP to make more robust
						std::string relativeDirPath = RESOURCE_LOCATION "textures/";
						std::string absoluteDirectoryStr = RelativePathToAbsolute(relativeDirPath);
						std::string selectedAbsFilePath;
						if (Platform::OpenFileDialog("Import texture", absoluteDirectoryStr, selectedAbsFilePath))
						{
							const std::string fileNameAndExtension = StripLeadingDirectories(selectedAbsFilePath);
							std::string relativeFilePath = relativeDirPath + fileNameAndExtension;

							bool bTextureAlreadyImported = false;
							for (VulkanTexture* tex : m_LoadedTextures)
							{
								if (tex->relativeFilePath.compare(relativeFilePath) == 0)
								{
									bTextureAlreadyImported = true;
									break;
								}
							}

							if (bTextureAlreadyImported)
							{
								Print("Texture with path %s already imported\n", relativeFilePath.c_str());
							}
							else
							{
								Print("Importing texture: %s\n", relativeFilePath.c_str());

								VulkanTexture* newTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue, relativeFilePath, 3, false, false, false);
								AddLoadedTexture(newTexture);
							}

							ImGui::CloseCurrentPopup();
						}
					}
				}

				if (ImGui::CollapsingHeader("Meshes"))
				{
					static i32 selectedMeshIndex = 0;

					std::string selectedMeshRelativeFilePath;
					LoadedMesh* selectedMesh = nullptr;
					i32 meshIdx = 0;
					for (const auto& meshPair : Mesh::m_LoadedMeshes)
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
							if (renderObject != nullptr)
							{
								GameObject* owningGameObject = renderObject->gameObject;
								if (owningGameObject)
								{
									Mesh* mesh = owningGameObject->GetMesh();
									if (mesh && mesh->GetRelativeFilePath().compare(selectedMeshRelativeFilePath) == 0)
									{
										MeshImportSettings importSettings = selectedMesh->importSettings;

										mesh->Destroy();
										mesh->SetOwner(owningGameObject);
										mesh->LoadFromFile(selectedMeshRelativeFilePath, mesh->GetMaterialIDs(), &importSettings);
									}
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
						for (const auto& meshIter : Mesh::m_LoadedMeshes)
						{
							bool bSelected = (i == selectedMeshIndex);
							const std::string meshFilePath = meshIter.first;
							const std::string meshFileName = StripLeadingDirectories(meshIter.first);
							if (ImGui::Selectable(meshFileName.c_str(), &bSelected))
							{
								selectedMeshIndex = i;
							}

							if (ImGui::BeginPopupContextItem())
							{
								if (ImGui::Button("Reload"))
								{
									Mesh::LoadMesh(meshIter.second->relativeFilePath);

									for (VulkanRenderObject* renderObject : m_RenderObjects)
									{
										if (renderObject)
										{
											Mesh* mesh = renderObject->gameObject->GetMesh();
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

										ImGui::SetDragDropPayload(MeshPayloadCStr, data, size);

										ImGui::Text("%s", meshFileName.c_str());

										ImGui::EndDragDropSource();
									}
								}
							}

							++i;
						}
					}
					ImGui::EndChild();

					static bool bShowErrorDialogue = false;
					static std::string errorMessage;
					const char* importErrorPopupID = "Mesh import failed";
					if (ImGui::Button("Import Mesh"))
					{
						// TODO: Not all models are directly in this directory! CLEANUP to make more robust
						std::string relativeImportDirPath = RESOURCE_LOCATION "meshes/";
						std::string absoluteImportDirectoryStr = RelativePathToAbsolute(relativeImportDirPath);
						std::string selectedAbsFilePath;
						if (Platform::OpenFileDialog("Import mesh", absoluteImportDirectoryStr, selectedAbsFilePath))
						{
							const std::string absDirectory = ExtractDirectoryString(selectedAbsFilePath);
							if (absDirectory != absoluteImportDirectoryStr)
							{
								bShowErrorDialogue = true;
								errorMessage = "Attempted to import mesh from invalid directory!"
									"\n" + absDirectory +
									"\nMeshes must be imported from " + absoluteImportDirectoryStr + "\n";
								ImGui::OpenPopup(importErrorPopupID);
							}
							else
							{
								const std::string fileNameAndExtension = StripLeadingDirectories(selectedAbsFilePath);
								std::string selectedRelativeFilePath = relativeImportDirPath + fileNameAndExtension;

								bool bMeshAlreadyImported = false;
								for (const auto& meshPair : Mesh::m_LoadedMeshes)
								{
									if (meshPair.first.compare(selectedRelativeFilePath) == 0)
									{
										bMeshAlreadyImported = true;
										break;
									}
								}

								if (bMeshAlreadyImported)
								{
									Print("Mesh with filepath %s already imported\n", selectedAbsFilePath.c_str());
								}
								else
								{
									Print("Importing mesh: %s\n", selectedAbsFilePath.c_str());

									LoadedMesh* existingMesh = nullptr;
									if (Mesh::FindPreLoadedMesh(selectedRelativeFilePath, &existingMesh))
									{
										i32 j = 0;
										for (const auto& meshPair : Mesh::m_LoadedMeshes)
										{
											if (meshPair.first.compare(selectedRelativeFilePath) == 0)
											{
												selectedMeshIndex = j;
												break;
											}

											++j;
										}
									}
									else
									{
										Mesh::LoadMesh(selectedRelativeFilePath);
									}
								}

								ImGui::CloseCurrentPopup();
							}
						}
					}

					ImGui::SetNextWindowSize(ImVec2(380, 120), ImGuiCond_FirstUseEver);
					if (ImGui::BeginPopupModal(importErrorPopupID, &bShowErrorDialogue))
					{
						ImGui::TextWrapped("%s", errorMessage.c_str());
						if (ImGui::Button("Ok"))
						{
							bShowErrorDialogue = false;
							ImGui::CloseCurrentPopup();
						}

						ImGui::EndPopup();
					}
				}
			}

			ImGui::End();
		}

		void VulkanRenderer::RecaptureReflectionProbe()
		{
			// UNIMPLEMENTED
		}

		void VulkanRenderer::RenderObjectStateChanged()
		{
			// TODO: Ignore object visibility changes
			m_bRebatchRenderObjects = true;
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

			system->simMaterialID = CreateParticleSystemSimulationMaterial(name + " sim material");
			system->renderingMaterialID = CreateParticleSystemRenderingMaterial(name + " rendering material");

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
			if (!m_SkyBoxMesh)
			{
				PrintError("Attempted to generate cubemap before skybox object was created!\n");
				return;
			}

			VulkanRenderObject* skyboxRenderObject = GetRenderObject(m_SkyBoxMesh->GetSubMeshes()[0]->renderID);
			VulkanMaterial& skyboxMat = m_Materials.at(renderObject->materialID);
			VulkanMaterial& renderObjectMat = m_Materials.at(renderObject->materialID);

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
			VulkanMaterial& equirectangularToCubeMat = m_Materials.at(equirectangularToCubeMatID);

			const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
			const u32 dim = (u32)renderObjectMat.material.cubemapSamplerSize.x;
			assert(dim <= MAX_TEXTURE_DIM);
			const u32 mipLevels = static_cast<u32>(floor(log2(dim))) + 1;

			VulkanRenderPass renderPass(m_VulkanDevice);
			renderPass.RegisterForColorOnly("Equirectangular to Cubemap render pass", InvalidFrameBufferAttachmentID, {});
			renderPass.bCreateFrameBuffer = false;
			renderPass.m_ColorAttachmentFormat = format;
			renderPass.ManuallySpecifyLayouts({ VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }, { VK_IMAGE_LAYOUT_UNDEFINED });
			renderPass.Create();

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


			ShaderID equirectangularToCubeShaderID = equirectangularToCubeMat.material.shaderID;
			VulkanShader& equirectangularToCubeShader = m_Shaders[equirectangularToCubeShaderID];

			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
			DescriptorSetCreateInfo equirectangularToCubeDescriptorCreateInfo = {};
			equirectangularToCubeDescriptorCreateInfo.DBG_Name = "Equirectangular to cube descriptor set";
			equirectangularToCubeDescriptorCreateInfo.descriptorSet = &descriptorSet;
			equirectangularToCubeDescriptorCreateInfo.descriptorSetLayout = &m_DescriptorSetLayouts[equirectangularToCubeShaderID];
			equirectangularToCubeDescriptorCreateInfo.shaderID = equirectangularToCubeShaderID;
			equirectangularToCubeDescriptorCreateInfo.uniformBufferList = &equirectangularToCubeMat.uniformBufferList;
			equirectangularToCubeDescriptorCreateInfo.imageDescriptors.Add(U_HDR_EQUIRECTANGULAR_SAMPLER, ImageDescriptorInfo{ equirectangularToCubeMat.textures[U_HDR_EQUIRECTANGULAR_SAMPLER]->imageView, m_LinMipLinSampler });
			FillOutBufferDescriptorInfos(&equirectangularToCubeDescriptorCreateInfo.bufferDescriptors, equirectangularToCubeDescriptorCreateInfo.uniformBufferList, equirectangularToCubeDescriptorCreateInfo.shaderID);
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
			pipelineCreateInfo.pushConstantRangeCount = (u32)pushConstantRanges.size();
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
				renderObjectMat.textures[U_CUBEMAP_SAMPLER],
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);

			VulkanBuffer* vertexBuffer = m_StaticVertexBuffers[m_Shaders[skyboxMat.material.shaderID].shader->staticVertexBufferIndex].second;
			VulkanBuffer* indexBuffer = m_StaticIndexBuffer;
			if (vertexBuffer->m_Buffer == VK_NULL_HANDLE)
			{
				PrintError("Attempted to generate cubemap from HDR but vertex buffer has not been generated! (for shader %s)\n", skyboxMat.material.name.c_str());
				return;
			}
			if (skyboxRenderObject->bIndexed &&
				indexBuffer->m_Buffer == VK_NULL_HANDLE)
			{
				PrintError("Attempted to generate cubemap from HDR but index buffer has not been generated! (for shader %s)\n", skyboxMat.material.name.c_str());
				return;
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
					skyboxMat.material.pushConstantBlock->SetData(s_CaptureViews[face], glm::perspective(PI_DIV_TWO, 1.0f, 0.1f, (real)dim));
					vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
						skyboxMat.material.pushConstantBlock->size, skyboxMat.material.pushConstantBlock->data);

					vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

					BindDescriptorSet(&equirectangularToCubeMat, 0, cmdBuf, pipelinelayout, descriptorSet);

					VkDeviceSize offsets[1] = { 0 };

					vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vertexBuffer->m_Buffer, offsets);
					if (skyboxRenderObject->bIndexed)
					{
						vkCmdBindIndexBuffer(cmdBuf, indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
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
						renderObjectMat.textures[U_CUBEMAP_SAMPLER]->image,
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
				renderObjectMat.textures[U_CUBEMAP_SAMPLER],
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				subresourceRange);

			EndDebugMarkerRegion(cmdBuf, "End Generate Cubemap from HDR");

			m_CommandBufferManager.FlushCommandBuffer(cmdBuf, m_GraphicsQueue, true);

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

			VulkanRenderObject* skyboxRenderObject = GetRenderObject(m_SkyBoxMesh->GetSubMeshes()[0]->renderID);
			VulkanMaterial& skyboxMat = m_Materials.at(skyboxRenderObject->materialID);
			VulkanMaterial& renderObjectMat = m_Materials.at(renderObject->materialID);

			const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
			const u32 dim = (u32)renderObjectMat.material.irradianceSamplerSize.x;
			assert(dim <= MAX_TEXTURE_DIM);
			const u32 mipLevels = static_cast<u32>(floor(log2(dim))) + 1;

			VulkanRenderPass renderPass(m_VulkanDevice);
			renderPass.RegisterForColorOnly("Generate Irradiance render pass", InvalidFrameBufferAttachmentID, {});
			renderPass.bCreateFrameBuffer = false;
			renderPass.m_ColorAttachmentFormat = format;
			renderPass.ManuallySpecifyLayouts({ VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }, { VK_IMAGE_LAYOUT_UNDEFINED });
			renderPass.Create();

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

			VulkanMaterial& irradianceMaterial = m_Materials[m_IrradianceMaterialID];
			VulkanShader& irradianceShader = m_Shaders[irradianceMaterial.material.shaderID];

			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
			DescriptorSetCreateInfo irradianceDescriptorCreateInfo = {};
			irradianceDescriptorCreateInfo.DBG_Name = "Irradiance descriptor set";
			irradianceDescriptorCreateInfo.descriptorSet = &descriptorSet;
			irradianceDescriptorCreateInfo.descriptorSetLayout = &m_DescriptorSetLayouts[irradianceMaterial.material.shaderID];
			irradianceDescriptorCreateInfo.shaderID = irradianceMaterial.material.shaderID;
			irradianceDescriptorCreateInfo.uniformBufferList = &irradianceMaterial.uniformBufferList;
			irradianceDescriptorCreateInfo.imageDescriptors.Add(U_CUBEMAP_SAMPLER, ImageDescriptorInfo{ renderObjectMat.textures[U_CUBEMAP_SAMPLER]->imageView, m_LinMipLinSampler });
			FillOutBufferDescriptorInfos(&irradianceDescriptorCreateInfo.bufferDescriptors, irradianceDescriptorCreateInfo.uniformBufferList, irradianceDescriptorCreateInfo.shaderID);
			CreateDescriptorSet(&irradianceDescriptorCreateInfo);

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
			pipelineCreateInfo.shaderID = irradianceMaterial.material.shaderID;
			pipelineCreateInfo.vertexAttributes = irradianceShader.shader->vertexAttributes;
			pipelineCreateInfo.topology = skyboxRenderObject->topology;
			pipelineCreateInfo.cullMode = skyboxRenderObject->cullMode;
			pipelineCreateInfo.descriptorSetLayoutIndex = irradianceMaterial.material.shaderID;
			pipelineCreateInfo.bSetDynamicStates = true;
			pipelineCreateInfo.bEnableAdditiveColorBlending = false;
			pipelineCreateInfo.subpass = 0;
			pipelineCreateInfo.depthTestEnable = VK_FALSE;
			pipelineCreateInfo.depthWriteEnable = VK_FALSE;
			pipelineCreateInfo.pushConstantRangeCount = (u32)pushConstantRanges.size();
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
				renderObjectMat.textures[U_IRRADIANCE_SAMPLER],
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);
			renderObjectMat.textures[U_IRRADIANCE_SAMPLER]->imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

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
					skyboxMat.material.pushConstantBlock->SetData(s_CaptureViews[face], glm::perspective(PI_DIV_TWO, 1.0f, 0.1f, (real)dim));
					vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
						skyboxMat.material.pushConstantBlock->size, skyboxMat.material.pushConstantBlock->data);

					vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

					BindDescriptorSet(&irradianceMaterial, 0, cmdBuf, pipelinelayout, descriptorSet);

					VkDeviceSize offsets[1] = { 0 };

					VulkanShader& skyboxShader = m_Shaders[skyboxMat.material.shaderID];
					VulkanBuffer* vertBuffer = m_StaticVertexBuffers[skyboxShader.shader->staticVertexBufferIndex].second;
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
						renderObjectMat.textures[U_IRRADIANCE_SAMPLER]->image,
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
				renderObjectMat.textures[U_IRRADIANCE_SAMPLER],
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				subresourceRange);
			renderObjectMat.textures[U_IRRADIANCE_SAMPLER]->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			EndDebugMarkerRegion(cmdBuf, "End Generate Irradiance");

			m_CommandBufferManager.FlushCommandBuffer(cmdBuf, m_GraphicsQueue, true);

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

			VulkanRenderObject* skyboxRenderObject = GetRenderObject(m_SkyBoxMesh->GetSubMeshes()[0]->renderID);
			VulkanMaterial& skyboxMat = m_Materials.at(skyboxRenderObject->materialID);
			VulkanMaterial& renderObjectMat = m_Materials.at(renderObject->materialID);

			const VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
			const u32 dim = (u32)renderObjectMat.material.prefilteredMapSize.x;
			assert(dim <= MAX_TEXTURE_DIM);
			const u32 mipLevels = static_cast<u32>(floor(log2(dim))) + 1;

			VulkanRenderPass renderPass(m_VulkanDevice);
			renderPass.RegisterForColorOnly("Generate Prefiltered Cube render pass", InvalidFrameBufferAttachmentID, {});
			renderPass.bCreateFrameBuffer = false;
			renderPass.m_ColorAttachmentFormat = format;
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

			VulkanMaterial& prefilterMaterial = m_Materials[m_PrefilterMaterialID];
			VulkanShader& prefilterShader = m_Shaders[prefilterMaterial.material.shaderID];

			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
			DescriptorSetCreateInfo prefilterDescriptorCreateInfo = {};
			prefilterDescriptorCreateInfo.DBG_Name = "Prefilter descriptor set";
			prefilterDescriptorCreateInfo.descriptorSet = &descriptorSet;
			prefilterDescriptorCreateInfo.descriptorSetLayout = &m_DescriptorSetLayouts[prefilterMaterial.material.shaderID];
			prefilterDescriptorCreateInfo.shaderID = prefilterMaterial.material.shaderID;
			prefilterDescriptorCreateInfo.uniformBufferList = &prefilterMaterial.uniformBufferList;
			prefilterDescriptorCreateInfo.imageDescriptors.Add(U_CUBEMAP_SAMPLER, ImageDescriptorInfo{ renderObjectMat.textures[U_CUBEMAP_SAMPLER]->imageView, m_LinMipLinSampler });
			FillOutBufferDescriptorInfos(&prefilterDescriptorCreateInfo.bufferDescriptors, prefilterDescriptorCreateInfo.uniformBufferList, prefilterDescriptorCreateInfo.shaderID);
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
			pipelineCreateInfo.shaderID = prefilterMaterial.material.shaderID;
			pipelineCreateInfo.vertexAttributes = prefilterShader.shader->vertexAttributes;
			pipelineCreateInfo.topology = skyboxRenderObject->topology;
			pipelineCreateInfo.cullMode = skyboxRenderObject->cullMode;
			pipelineCreateInfo.descriptorSetLayoutIndex = prefilterMaterial.material.shaderID;
			pipelineCreateInfo.bSetDynamicStates = true;
			pipelineCreateInfo.bEnableAdditiveColorBlending = false;
			pipelineCreateInfo.subpass = 0;
			pipelineCreateInfo.depthTestEnable = VK_FALSE;
			pipelineCreateInfo.depthWriteEnable = VK_FALSE;
			pipelineCreateInfo.pushConstantRangeCount = (u32)pushConstantRanges.size();
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
				renderObjectMat.textures[U_PREFILTER_MAP],
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);
			renderObjectMat.textures[U_PREFILTER_MAP]->imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

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
					skyboxMat.material.pushConstantBlock->SetData(s_CaptureViews[face], glm::perspective(PI_DIV_TWO, 1.0f, 0.1f, (real)dim));
					vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
						skyboxMat.material.pushConstantBlock->size, skyboxMat.material.pushConstantBlock->data);

					vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

					BindDescriptorSet(&prefilterMaterial, 0, cmdBuf, pipelinelayout, descriptorSet);

					VkDeviceSize offsets[1] = { 0 };

					vkCmdBindVertexBuffers(cmdBuf, 0, 1, &m_StaticVertexBuffers[m_Shaders[skyboxMat.material.shaderID].shader->staticVertexBufferIndex].second->m_Buffer, offsets);
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
						renderObjectMat.textures[U_PREFILTER_MAP]->image,
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
				renderObjectMat.textures[U_PREFILTER_MAP],
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				subresourceRange);
			renderObjectMat.textures[U_PREFILTER_MAP]->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			EndDebugMarkerRegion(cmdBuf, "End Generate Prefiltered Cube");

			m_CommandBufferManager.FlushCommandBuffer(cmdBuf, m_GraphicsQueue, true);

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
				const u32 dim = (u32)m_BRDFSize.x;
				assert(dim <= MAX_TEXTURE_DIM);

				VulkanRenderPass renderPass(m_VulkanDevice);
				renderPass.RegisterForColorOnly("Generate BRDF LUT render pass", InvalidFrameBufferAttachmentID, {});
				renderPass.bCreateFrameBuffer = false;
				renderPass.m_ColorAttachmentFormat = m_BRDFTexture->imageFormat;
				renderPass.ManuallySpecifyLayouts({ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, { VK_IMAGE_LAYOUT_UNDEFINED });
				renderPass.Create();

				VkFramebufferCreateInfo framebufferCreateInfo = vks::framebufferCreateInfo(renderPass);
				framebufferCreateInfo.attachmentCount = 1;
				framebufferCreateInfo.pAttachments = &m_BRDFTexture->imageView;
				framebufferCreateInfo.width = dim;
				framebufferCreateInfo.height = dim;

				VkFramebuffer framebuffer = VK_NULL_HANDLE;
				VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &framebufferCreateInfo, nullptr, &framebuffer));

				VulkanMaterial& brdfMaterial = m_Materials[m_BRDFMaterialID];
				VulkanShader& brdfShader = m_Shaders[brdfMaterial.material.shaderID];

				VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
				DescriptorSetCreateInfo brdfDescriptorCreateInfo = {};
				brdfDescriptorCreateInfo.DBG_Name = "BRDF descriptor set";
				brdfDescriptorCreateInfo.descriptorSet = &descriptorSet;
				brdfDescriptorCreateInfo.descriptorSetLayout = &m_DescriptorSetLayouts[brdfMaterial.material.shaderID];
				brdfDescriptorCreateInfo.shaderID = brdfMaterial.material.shaderID;
				brdfDescriptorCreateInfo.uniformBufferList = &brdfMaterial.uniformBufferList;
				CreateDescriptorSet(&brdfDescriptorCreateInfo);

				VkPipelineLayout pipelinelayout = VK_NULL_HANDLE;
				VkPipeline pipeline = VK_NULL_HANDLE;
				GraphicsPipelineCreateInfo pipelineCreateInfo = {};
				pipelineCreateInfo.DBG_Name = "BRDF LUT pipeline";
				pipelineCreateInfo.graphicsPipeline = &pipeline;
				pipelineCreateInfo.pipelineLayout = &pipelinelayout;
				pipelineCreateInfo.renderPass = renderPass;
				pipelineCreateInfo.shaderID = brdfMaterial.material.shaderID;
				pipelineCreateInfo.vertexAttributes = brdfShader.shader->vertexAttributes;
				pipelineCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
				pipelineCreateInfo.cullMode = VK_CULL_MODE_NONE;
				pipelineCreateInfo.descriptorSetLayoutIndex = brdfMaterial.material.shaderID;
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

				EndDebugMarkerRegion(cmdBuf, "End Generate BRDF LUT");

				m_CommandBufferManager.FlushCommandBuffer(cmdBuf, m_GraphicsQueue, true);

				vkQueueWaitIdle(m_GraphicsQueue);

				vkDestroyPipeline(m_VulkanDevice->m_LogicalDevice, pipeline, nullptr);
				vkDestroyPipelineLayout(m_VulkanDevice->m_LogicalDevice, pipelinelayout, nullptr);
				vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, framebuffer, nullptr);

				bRenderedBRDFLUT = true;
			}
		}

		void VulkanRenderer::CaptureSceneToCubemap(RenderID cubemapRenderID)
		{
			FLEX_UNUSED(cubemapRenderID);
		}

		void VulkanRenderer::GeneratePrefilteredMapFromCubemap(MaterialID cubemapMaterialID)
		{
			FLEX_UNUSED(cubemapMaterialID);
		}

		void VulkanRenderer::GenerateIrradianceSamplerFromCubemap(MaterialID cubemapMaterialID)
		{
			FLEX_UNUSED(cubemapMaterialID);
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

			VkDescriptorSetLayout descSetLayout = m_DescriptorSetLayouts[ssaoMaterial->material.shaderID];

			DescriptorSetCreateInfo descSetCreateInfo = {};
			descSetCreateInfo.DBG_Name = "SSAO descriptor set";
			descSetCreateInfo.descriptorSet = &m_SSAODescSet;
			descSetCreateInfo.descriptorSetLayout = &descSetLayout;
			descSetCreateInfo.shaderID = ssaoMaterial->material.shaderID;
			descSetCreateInfo.uniformBufferList = &ssaoMaterial->uniformBufferList;
			descSetCreateInfo.imageDescriptors.Add(U_DEPTH_SAMPLER, ImageDescriptorInfo{ m_GBufferDepthAttachment->view, m_DepthSampler });
			descSetCreateInfo.imageDescriptors.Add(U_SSAO_NORMAL_SAMPLER, ImageDescriptorInfo{ m_GBufferColorAttachment0->view, m_NearestClampEdgeSampler });
			descSetCreateInfo.imageDescriptors.Add(U_NOISE_SAMPLER, ImageDescriptorInfo{ ssaoMaterial->textures[U_NOISE_SAMPLER]->imageView, m_NearestClampEdgeSampler });
			FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
			CreateDescriptorSet(&descSetCreateInfo);

			VulkanMaterial* ssaoBlurMaterial = &m_Materials[m_SSAOBlurMatID];

			descSetLayout = m_DescriptorSetLayouts[ssaoBlurMaterial->material.shaderID];

			descSetCreateInfo = {};
			descSetCreateInfo.descriptorSet = &m_SSAOBlurHDescSet;
			descSetCreateInfo.DBG_Name = "SSAO Blur Horizontal descriptor set";
			descSetCreateInfo.descriptorSetLayout = &descSetLayout;
			descSetCreateInfo.shaderID = ssaoBlurMaterial->material.shaderID;
			descSetCreateInfo.uniformBufferList = &ssaoBlurMaterial->uniformBufferList;
			descSetCreateInfo.imageDescriptors.Add(U_DEPTH_SAMPLER, ImageDescriptorInfo{ m_GBufferDepthAttachment->view, m_DepthSampler });
			descSetCreateInfo.imageDescriptors.Add(U_SSAO_RAW_SAMPLER, ImageDescriptorInfo{ m_SSAOFBColorAttachment0->view, m_NearestClampEdgeSampler });
			descSetCreateInfo.imageDescriptors.Add(U_SSAO_NORMAL_SAMPLER, ImageDescriptorInfo{ m_GBufferColorAttachment0->view, m_NearestClampEdgeSampler });
			FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
			CreateDescriptorSet(&descSetCreateInfo);

			descSetCreateInfo = {};
			descSetCreateInfo.descriptorSet = &m_SSAOBlurVDescSet;
			descSetCreateInfo.DBG_Name = "SSAO Blur Vertical descriptor set";
			descSetCreateInfo.descriptorSetLayout = &descSetLayout;
			descSetCreateInfo.shaderID = ssaoBlurMaterial->material.shaderID;
			descSetCreateInfo.uniformBufferList = &ssaoBlurMaterial->uniformBufferList;
			descSetCreateInfo.imageDescriptors.Add(U_DEPTH_SAMPLER, ImageDescriptorInfo{ m_GBufferDepthAttachment->view, m_DepthSampler });
			descSetCreateInfo.imageDescriptors.Add(U_SSAO_RAW_SAMPLER, ImageDescriptorInfo{ m_SSAOBlurHFBColorAttachment0->view, m_NearestClampEdgeSampler });
			descSetCreateInfo.imageDescriptors.Add(U_SSAO_NORMAL_SAMPLER, ImageDescriptorInfo{ m_GBufferColorAttachment0->view, m_NearestClampEdgeSampler });
			FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
			CreateDescriptorSet(&descSetCreateInfo);
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
			std::vector<char> fileMemory;
			ReadFile(fontMetaData.filePath, fileMemory, true);

			std::map<i32, FontMetric*> characters;
			std::array<glm::vec2i, 4> maxPos;
			FT_Face face = {};
			if (!LoadFontMetrics(fileMemory, m_FTLibrary, fontMetaData, &characters, &maxPos, &face))
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
					textureName, textureSize.x, textureSize.y, 4));
				fontTexColAttachment->name = textureName;
				fontTexColAttachment->CreateEmpty(fontTexFormat, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
				fontTexColAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

				VulkanMaterial& computeSDFMaterial = m_Materials[m_ComputeSDFMatID];
				ShaderID computeSDFShaderID = computeSDFMaterial.material.shaderID;
				VulkanShader& computeSDFShader = m_Shaders[computeSDFShaderID];

				VulkanRenderPass renderPass(m_VulkanDevice);
				renderPass.RegisterForColorOnly("Font SDF render pass", InvalidFrameBufferAttachmentID, {});
				renderPass.bCreateFrameBuffer = false;
				renderPass.m_ColorAttachmentFormat = fontTexFormat;
				renderPass.Create();

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

				//VulkanRenderObject* gBufferRenderObject = GetRenderObject(m_GBufferQuadRenderID);
				//VulkanMaterial* gBufferMaterial = &m_Materials.at(gBufferRenderObject->materialID);

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
					if (FT_Bitmap_Convert(m_FTLibrary, &face->glyph->bitmap, &alignedBitmap, 1))
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
					vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_StaticVertexBuffers[computeSDFShader.shader->staticVertexBufferIndex].second->m_Buffer, offsets);

					vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

					VkDescriptorSetLayout descSetLayout = m_DescriptorSetLayouts[computeSDFShaderID];
					VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

					DescriptorSetCreateInfo descSetCreateInfo = {};
					descSetCreateInfo.DBG_Name = "Font SDF descriptor set";
					descSetCreateInfo.descriptorSet = &descriptorSet;
					descSetCreateInfo.descriptorSetLayout = &descSetLayout;
					descSetCreateInfo.shaderID = computeSDFShaderID;
					descSetCreateInfo.uniformBufferList = &computeSDFMaterial.uniformBufferList;
					descSetCreateInfo.imageDescriptors.Add(U_ALBEDO_SAMPLER, ImageDescriptorInfo{ highResTex->imageView, m_LinMipLinSampler });
					FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
					CreateDescriptorSet(&descSetCreateInfo);
					descSets.push_back(descriptorSet);

					BindDescriptorSet(&computeSDFMaterial, dynamicOffsetIndex * m_DynamicAlignment, commandBuffer, pipelineLayout, descriptorSet);

					UniformOverrides overrides = {};
					overrides.texChannel = metric->channel;
					overrides.overridenUniforms.AddUniform(U_TEX_CHANNEL);
					overrides.sdfData = glm::vec4((real)res.x, (real)res.y, (real)spread, (real)sampleDensity);
					overrides.overridenUniforms.AddUniform(U_SDF_DATA);
					UpdateDynamicUniformBuffer(m_ComputeSDFMatID, dynamicOffsetIndex * m_DynamicAlignment, MAT4_IDENTITY, &overrides);

					vkCmdDraw(commandBuffer, gBufferObject->vertexBufferData->VertexCount, 1, gBufferObject->vertexOffset, 0);

					metric->texCoord = metric->texCoord / fontTexSize;

					FT_Bitmap_Done(m_FTLibrary, &alignedBitmap);
				}

				vkCmdEndRenderPass(commandBuffer);

				EndDebugMarkerRegion(commandBuffer, "End Generate Font SDF");

				VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));
				SetCommandBufferName(m_VulkanDevice, commandBuffer, "Load font command buffer");

				VkSubmitInfo submitInfo = vks::submitInfo(1, &commandBuffer);
				VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				submitInfo.pWaitDstStageMask = &waitStages;

				VK_CHECK_RESULT(vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
				VK_CHECK_RESULT(vkQueueWaitIdle(m_GraphicsQueue));

				for (VulkanTexture* highResTex : charTextures)
				{
					delete highResTex;
				}
				charTextures.clear();

				vkFreeDescriptorSets(m_VulkanDevice->m_LogicalDevice, m_DescriptorPool, (u32)descSets.size(), descSets.data());
				descSets.clear();

				vkDestroyPipeline(m_VulkanDevice->m_LogicalDevice, graphicsPipeline, nullptr);
				vkDestroyPipelineLayout(m_VulkanDevice->m_LogicalDevice, pipelineLayout, nullptr);
				vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, framebuffer, nullptr);

				std::string savedSDFTextureAbsFilePath = RelativePathToAbsolute(fontMetaData.renderedTextureFilePath);
				fontTexColAttachment->SaveToFile(savedSDFTextureAbsFilePath, ImageFormat::PNG);

				fontTexColAttachment->TransitionToLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			}

			FT_Done_Face(face);

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

			// Sanity check
			if (!shader.bCompute && vkShader.renderPass == VK_NULL_HANDLE)
			{
				PrintError("Shader %s's render pass was not set!\n", shader.name.c_str());
				bSuccess = false;
			}

			if (g_bEnableLogging_Loading)
			{
				const std::string vertFileName = StripLeadingDirectories(shader.vertexShaderFilePath);
				const std::string fragFileName = StripLeadingDirectories(shader.fragmentShaderFilePath);
				const std::string geomFileName = StripLeadingDirectories(shader.geometryShaderFilePath);
				const std::string computeFileName = StripLeadingDirectories(shader.computeShaderFilePath);

				Print("Loading shader %s (", shader.name.c_str());

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

			const bool bUseVertexStage = !shader.vertexShaderFilePath.empty();
			const bool bUseFragmentStage = !shader.fragmentShaderFilePath.empty();
			const bool bUseGeometryStage = !shader.geometryShaderFilePath.empty();
			const bool bUseComputeStage = !shader.computeShaderFilePath.empty();

			if (bUseVertexStage && !ReadFile(shader.vertexShaderFilePath, shader.vertexShaderCode, true))
			{
				PrintError("Could not find vertex shader %s\n", shader.vertexShaderFilePath.c_str());
				bSuccess = false;
			}

			if (bUseFragmentStage && !ReadFile(shader.fragmentShaderFilePath, shader.fragmentShaderCode, true))
			{
				PrintError("Could not find fragment shader %s\n", shader.fragmentShaderFilePath.c_str());
				bSuccess = false;
			}

			if (bUseGeometryStage && !ReadFile(shader.geometryShaderFilePath, shader.geometryShaderCode, true))
			{
				PrintError("Could not find geometry shader %s\n", shader.geometryShaderFilePath.c_str());
				bSuccess = false;
			}

			if (bUseComputeStage && !ReadFile(shader.computeShaderFilePath, shader.computeShaderCode, true))
			{
				PrintError("Could not find compute shader %s\n", shader.computeShaderFilePath.c_str());
				bSuccess = false;
			}

			if (bUseVertexStage)
			{
				if (shader.vertexShaderCode.size() == 0)
				{
					PrintError("Vertex shader code failed to load %s\n", shader.vertexShaderFilePath.c_str());
					bSuccess = false;
				}
				else if (!CreateShaderModule(shader.vertexShaderCode, vkShader.vertShaderModule.replace()))
				{
					PrintError("Failed to compile vertex shader located at: %s\n", shader.vertexShaderFilePath.c_str());
					bSuccess = false;
				}
				shader.vertexShaderCode.clear();
			}

			if (bUseFragmentStage)
			{
				if (shader.fragmentShaderCode.size() == 0)
				{
					PrintError("Fragment shader code failed to load %s\n", shader.fragmentShaderFilePath.c_str());
					bSuccess = false;
				}
				else if (!CreateShaderModule(shader.fragmentShaderCode, vkShader.fragShaderModule.replace()))
				{
					PrintError("Failed to compile fragment shader located at: %s\n", shader.fragmentShaderFilePath.c_str());
					bSuccess = false;
				}
				shader.fragmentShaderCode.clear();
			}

			if (bUseGeometryStage)
			{
				if (shader.geometryShaderCode.size() == 0)
				{
					PrintError("Geometry shader code failed to load %s\n", shader.geometryShaderFilePath.c_str());
					bSuccess = false;
				}
				else if (!CreateShaderModule(shader.geometryShaderCode, vkShader.geomShaderModule.replace()))
				{
					PrintError("Failed to compile geometry shader located at: %s\n", shader.geometryShaderFilePath.c_str());
					bSuccess = false;
				}
				shader.geometryShaderCode.clear();
			}

			if (bUseComputeStage)
			{
				if (shader.computeShaderCode.size() == 0)
				{
					PrintError("Compute shader code failed to load %s\n", shader.computeShaderFilePath.c_str());
					bSuccess = false;
				}
				else if (!CreateShaderModule(shader.computeShaderCode, vkShader.computeShaderModule.replace()))
				{
					PrintError("Failed to compile compute shader located at: %s\n", shader.computeShaderFilePath.c_str());
					bSuccess = false;
				}
				shader.computeShaderCode.clear();
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
			if (m_FontMatSSID == InvalidMaterialID)
			{
				return;
			}

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

			// Update dynamic text buffer
			{
				std::vector<TextVertex2D> textVerticesSS;
				UpdateTextBufferSS(textVerticesSS);

				u32 SSTextBufferByteCount = (u32)(textVerticesSS.size() * sizeof(TextVertex2D));

				if (SSTextBufferByteCount > 0)
				{
					VulkanBuffer* vertexBuffer = m_StaticVertexBuffers[fontShader.shader->staticVertexBufferIndex].second;
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

			PROFILE_AUTO("DrawTextSS");

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
				pipelineCreateInfo.renderPass = *m_UIRenderPass;
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
						info.shaderID = fontMaterial.material.shaderID;
						info.uniformBufferList = &fontMaterial.uniformBufferList;
						info.imageDescriptors.Add(U_ALBEDO_SAMPLER, ImageDescriptorInfo{ font->GetTexture()->imageView, m_LinMipLinSampler });
						FillOutBufferDescriptorInfos(&info.bufferDescriptors, info.uniformBufferList, info.shaderID);
						CreateDescriptorSet(&info);
					}

					u32 dynamicOffsetIndex = 0;
					BindDescriptorSet(&fontMaterial, dynamicOffsetIndex * m_DynamicAlignment, commandBuffer, m_FontSSPipelineLayout, font->m_DescriptorSet);

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
					vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_StaticVertexBuffers[fontShader.shader->staticVertexBufferIndex].second->m_Buffer, offsets);

					vkCmdDraw(commandBuffer, font->bufferSize, 1, font->bufferStart, 0);
				}
			}
		}

		void VulkanRenderer::DrawTextWS(VkCommandBuffer commandBuffer)
		{
			if (m_FontMatWSID == InvalidMaterialID)
			{
				return;
			}

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

			// Update dynamic text buffer
			{
				std::vector<TextVertex3D> textVerticesWS;
				UpdateTextBufferWS(textVerticesWS);

				u32 WSTextBufferByteCount = (u32)(textVerticesWS.size() * sizeof(TextVertex3D));

				if (WSTextBufferByteCount > 0)
				{
					//const VulkanMaterial& fontMaterial = m_Materials[m_FontMatWSID];
					VulkanBuffer* vertexBuffer = m_StaticVertexBuffers[fontShader.shader->staticVertexBufferIndex].second;
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

			PROFILE_AUTO("DrawTextWS");

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
				pipelineCreateInfo.renderPass = *m_ForwardRenderPass;
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
						info.shaderID = fontMaterial.material.shaderID;
						info.uniformBufferList = &fontMaterial.uniformBufferList;
						info.imageDescriptors.Add(U_ALBEDO_SAMPLER, ImageDescriptorInfo{ font->GetTexture()->imageView, m_LinMipLinSampler });
						FillOutBufferDescriptorInfos(&info.bufferDescriptors, info.uniformBufferList, info.shaderID);
						CreateDescriptorSet(&info);
					}

					u32 dynamicOffsetIndex = 0;
					BindDescriptorSet(&fontMaterial, dynamicOffsetIndex * m_DynamicAlignment, commandBuffer, m_FontWSPipelineLayout, font->m_DescriptorSet);

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
					vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_StaticVertexBuffers[fontShader.shader->staticVertexBufferIndex].second->m_Buffer, offsets);

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
			if (!cam->bIsGameplayCam && g_EngineInstance->IsRenderingEditorObjects())
			{
				glm::vec3 scale(1.0f, -1.0f, 1.0f);

				SpriteQuadDrawInfo drawInfo = {};
				drawInfo.bScreenSpace = false;
				drawInfo.bReadDepth = true;
				drawInfo.bWriteDepth = true;
				drawInfo.scale = scale;
				drawInfo.materialID = m_SpriteMatWSID;

				const real minSpriteDist = 1.5f;
				const real maxSpriteDist = 3.0f;

				glm::vec3 camPos = cam->position;
				glm::vec3 camUp = cam->up;
				for (i32 i = 0; i < m_NumPointLightsEnabled; ++i)
				{
					if (m_PointLights[i].enabled)
					{
						drawInfo.textureID = m_PointLightIconID;
						// TODO: Sort back to front? Or clear depth and then enable depth test
						drawInfo.pos = m_PointLights[i].pos;
						glm::mat4 rotMat = glm::lookAt(m_PointLights[i].pos, camPos, camUp);
						drawInfo.rotation = glm::conjugate(glm::toQuat(rotMat));
						real alpha = Saturate(glm::distance(drawInfo.pos, camPos) / maxSpriteDist - minSpriteDist);
						drawInfo.color = glm::vec4(m_PointLights[i].color * 1.5f, alpha);
						EnqueueSprite(drawInfo);
					}
				}

				if (m_DirectionalLight != nullptr && m_DirectionalLight->data.enabled)
				{
					drawInfo.textureID = m_DirectionalLightIconID;
					drawInfo.pos = m_DirectionalLight->pos;
					glm::mat4 rotMat = glm::lookAt(camPos, m_DirectionalLight->pos, camUp);
					drawInfo.rotation = glm::conjugate(glm::toQuat(rotMat));
					real alpha = Saturate(glm::distance(drawInfo.pos, camPos) / maxSpriteDist - minSpriteDist);
					drawInfo.color = glm::vec4(m_DirectionalLight->data.color * 1.5f, alpha);
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

			MaterialID matID = (batch[0].materialID == InvalidMaterialID ? (batch[0].bScreenSpace ? m_SpriteMatSSID : m_SpriteMatWSID) : batch[0].materialID);
			VulkanMaterial& spriteMat = m_Materials.at(matID);
			VulkanShader& spriteShader = m_Shaders[spriteMat.material.shaderID];

			// TODO: Use instancing!
			VulkanBuffer* vertexBuffer = m_StaticVertexBuffers[spriteShader.shader->staticVertexBufferIndex].second;

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

				UniformOverrides overrides = {};
				overrides.colorMultiplier = drawInfo.color;
				overrides.overridenUniforms.AddUniform(U_COLOR_MULTIPLIER);
				UpdateDynamicUniformBuffer(matID, dynamicUBOOffset, model, &overrides);

				vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer->m_Buffer, offsets);

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

				VkDescriptorSet descSet = GetSpriteDescriptorSet(drawInfo.textureID, matID, drawInfo.textureLayer);

				BindDescriptorSet(&spriteMat, dynamicUBOOffset, commandBuffer, pipelineLayout, descSet);

				vkCmdDraw(commandBuffer, spriteRenderObject->vertexBufferData->VertexCount, 1, spriteRenderObject->vertexOffset, 0);

				++i;
			}
		}

		void VulkanRenderer::DrawParticles(VkCommandBuffer commandBuffer)
		{
			BeginDebugMarkerRegion(commandBuffer, "Particles");

			for (VulkanParticleSystem* particleSystem : m_ParticleSystems)
			{
				if (!particleSystem || !particleSystem->system->IsVisible())
				{
					continue;
				}

				VulkanMaterial& particleSimMat = m_Materials.at(particleSystem->system->simMaterialID);
				VulkanMaterial& particleRenderingMat = m_Materials.at(particleSystem->system->renderingMaterialID);

				u32 dynamicUBOOffset = particleSystem->ID * m_DynamicAlignment;
				UpdateDynamicUniformBuffer(particleSystem->system->renderingMaterialID, dynamicUBOOffset, particleSystem->system->model, nullptr);

				VkDeviceSize offsets[1] = { 0 };
				vkCmdBindVertexBuffers(commandBuffer, 0, 1, &particleSimMat.uniformBufferList.Get(UniformBufferType::PARTICLE_DATA)->buffer.m_Buffer, offsets);

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, particleSystem->graphicsPipeline);

				BindDescriptorSet(&particleRenderingMat, dynamicUBOOffset, commandBuffer, m_ParticleGraphicsPipelineLayout, particleSystem->renderingDescriptorSet);

				vkCmdDraw(commandBuffer, MAX_PARTICLE_COUNT, 1, 0, 0);
			}

			EndDebugMarkerRegion(commandBuffer, "End Particles");
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
			descSetCreateInfo.uniformBufferList = &shadowMaterial->uniformBufferList;
			FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
			CreateDescriptorSet(&descSetCreateInfo);
		}

		VkDescriptorSet VulkanRenderer::CreateSpriteDescSet(MaterialID spriteMaterialID, TextureID textureID, u32 layer /* = 0 */)
		{
			assert(textureID != InvalidTextureID);

			VulkanMaterial& spriteMat = m_Materials.at(spriteMaterialID);
			VulkanShader& spriteShader = m_Shaders[spriteMat.material.shaderID];

			VkDescriptorSet descSet = VK_NULL_HANDLE;
			DescriptorSetCreateInfo descSetCreateInfo = {};
			descSetCreateInfo.DBG_Name = "Sprite descriptor set";
			descSetCreateInfo.descriptorSet = &descSet;
			descSetCreateInfo.descriptorSetLayout = &m_DescriptorSetLayouts[spriteMat.material.shaderID];
			descSetCreateInfo.shaderID = spriteMat.material.shaderID;
			descSetCreateInfo.uniformBufferList = &spriteMat.uniformBufferList;
			if (spriteShader.shader->bTextureArr)
			{
				descSetCreateInfo.imageDescriptors.Add(U_ALBEDO_SAMPLER, ImageDescriptorInfo{ m_ShadowCascades[layer]->imageView, m_LinMipLinSampler });
			}
			else
			{
				VulkanTexture* texture = GetLoadedTexture(textureID);
				descSetCreateInfo.imageDescriptors.Add(U_ALBEDO_SAMPLER, ImageDescriptorInfo{ texture->imageView, texture->sampler });
			}
			FillOutBufferDescriptorInfos(&descSetCreateInfo.bufferDescriptors, descSetCreateInfo.uniformBufferList, descSetCreateInfo.shaderID);
			CreateDescriptorSet(&descSetCreateInfo);

			return descSet;
		}

		void VulkanRenderer::InitializeAllParticleSystemBuffers()
		{
			for (VulkanParticleSystem* particleSystem : m_ParticleSystems)
			{
				if (particleSystem)
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
				particleBufferData[i].color = glm::vec4(1.0f);
				particleBufferData[i].vel = glm::vec3(cos(theta), 0.0f, sin(theta)) * mag;
				particleBufferData[i].extraVec4 = glm::vec4(Lerp(0.6f, 0.2f, mag), 0.0f, 0.0f, 0.0f);
			}

			VulkanMaterial& particleSimMat = m_Materials.at(particleSystem->system->simMaterialID);

			const u32 particleBufferSize = particleSystem->system->data.particleCount * sizeof(particleBufferData[0]);

			VK_CHECK_RESULT(stagingBuffer.Map());
			memcpy(stagingBuffer.m_Mapped, particleBufferData.data(), particleBufferSize);
			memset(((u8*)stagingBuffer.m_Mapped) + particleBufferSize, 0, MAX_PARTICLE_COUNT * sizeof(ParticleBufferData) - particleBufferSize);
			stagingBuffer.Unmap();

			UniformBuffer* particleBuffer = particleSimMat.uniformBufferList.Get(UniformBufferType::PARTICLE_DATA);
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

		TextureID VulkanRenderer::GetNextAvailableTextureID()
		{
			for (u32 i = 0; i < m_LoadedTextures.size(); ++i)
			{
				if (m_LoadedTextures[i] == nullptr)
				{
					return (TextureID)i;
				}
			}
			m_LoadedTextures.push_back(nullptr);
			return (TextureID)(m_LoadedTextures.size() - 1);
		}

		TextureID VulkanRenderer::AddLoadedTexture(VulkanTexture* texture)
		{
			TextureID textureID = GetNextAvailableTextureID();
			m_LoadedTextures[textureID] = texture;
			return textureID;
		}

		VulkanTexture* VulkanRenderer::GetLoadedTexture(TextureID textureID)
		{
			if (textureID < m_LoadedTextures.size())
			{
				return m_LoadedTextures[textureID];
			}
			return nullptr;
		}

		MaterialID VulkanRenderer::GetNextAvailableMaterialID() const
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

			std::vector<const char*> extensions = GetRequiredInstanceExtensions();

			for (u32 i = 0; i < m_OptionalInstanceExtensions.size(); ++i)
			{
				if (InstanceExtensionSupported(m_OptionalInstanceExtensions[i]))
				{
					extensions.push_back(m_OptionalInstanceExtensions[i]);
				}
			}

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

			VK_CHECK_RESULT(vkCreateInstance(&createInfo, nullptr, &m_Instance));

			volkLoadInstance(m_Instance);

			if (m_bEnableValidationLayers && !CheckValidationLayerSupport())
			{
				// TODO: Remove all exceptions
				throw std::runtime_error("validation layers requested, but not available!");
			}
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
			createInfo.pfnCallback = DebugCallback;

			VK_CHECK_RESULT(CreateDebugReportCallbackEXT(m_Instance, &createInfo, nullptr, &m_Callback));
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

		void SetClipboardText(void* userData, const char* text)
		{
			GLFWWindowWrapper* glfwWindow = static_cast<GLFWWindowWrapper*>(userData);
			glfwWindow->SetClipboardText(text);
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

			m_DeferredRenderPass->RegisterForMultiColorAndDepth("Deferred render pass",
				{ m_GBufferColorAttachment0->ID, m_GBufferColorAttachment1->ID }, // Target color attachments
				m_GBufferDepthAttachment->ID, // Target depth attachment
				{} // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_DeferredRenderPass);

			m_SSAORenderPass->RegisterForColorOnly("SSAO render pass",
				m_SSAOFBColorAttachment0->ID, // Target color attachment
				{ m_GBufferDepthAttachment->ID } // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_SSAORenderPass);

			m_SSAOBlurHRenderPass->RegisterForColorOnly("SSAO Blur Horizontal render pass",
				m_SSAOBlurHFBColorAttachment0->ID, // Target color attachment
				{ m_SSAOFBColorAttachment0->ID } // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_SSAOBlurHRenderPass);

			m_SSAOBlurVRenderPass->RegisterForColorOnly("SSAO Blur Vertical render pass",
				m_SSAOBlurVFBColorAttachment0->ID, // Target color attachment
				{ m_SSAOBlurHFBColorAttachment0->ID } // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_SSAOBlurVRenderPass);

			m_DeferredCombineRenderPass->RegisterForColorAndDepth("Deferred combine render pass",
				m_OffscreenFB0ColorAttachment0->ID, // Target color attachment
				m_OffscreenFB0DepthAttachment->ID,  // Target depth attachment
				{ SHADOW_CASCADE_DEPTH_ATTACHMENT_ID, m_GBufferColorAttachment0->ID, m_GBufferColorAttachment1->ID, m_GBufferDepthAttachment->ID, m_SSAOBlurVFBColorAttachment0->ID } // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_DeferredCombineRenderPass);

			// TODO: Denote that GBuffer depth into copied (blit) into Offscreen 0 depth here

			m_ForwardRenderPass->RegisterForColorAndDepth("Forward render pass",
				m_OffscreenFB0ColorAttachment0->ID, // Target color attachment
				m_OffscreenFB0DepthAttachment->ID, // Target depth attachment
				{} // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_ForwardRenderPass);

			m_PostProcessRenderPass->RegisterForColorAndDepth("Post Process render pass",
				m_OffscreenFB1ColorAttachment0->ID, // Target color attachment
				m_OffscreenFB1DepthAttachment->ID, // Target depth attachment
				{ m_OffscreenFB0ColorAttachment0->ID } // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_PostProcessRenderPass);

			m_GammaCorrectRenderPass->RegisterForColorAndDepth("Gamma correct render pass",
				m_OffscreenFB0ColorAttachment0->ID, // Target color attachment
				m_OffscreenFB0DepthAttachment->ID, // Target depth attachment
				{ m_OffscreenFB1ColorAttachment0->ID } // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_GammaCorrectRenderPass);

			m_TAAResolveRenderPass->RegisterForColorAndDepth("TAA Resolve render pass",
				m_OffscreenFB1ColorAttachment0->ID, // Target color attachment
				m_OffscreenFB1DepthAttachment->ID, // Target depth attachment
				{ m_OffscreenFB0ColorAttachment0->ID, m_OffscreenFB0DepthAttachment->ID } // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_TAAResolveRenderPass);

			// TODO: Denote that history buffer is copied into from swap chain
			// TODO: Denote that swap chain is copied into from m_OffscreenFB0ColorAttachment0
			//m_AutoTransitionedRenderPasses.push_back(CopyOperation(SWAP_CHAIN_COLOR_ATTACHMENT_ID, m_OffscreenFB1ColorAttachment0->ID));

			m_UIRenderPass->RegisterForColorAndDepth("UI render pass",
				SWAP_CHAIN_COLOR_ATTACHMENT_ID, // Target color attachment
				SWAP_CHAIN_DEPTH_ATTACHMENT_ID, // Target depth attachment
				{ m_OffscreenFB1ColorAttachment0->ID } // Sampled attachments
			);
			m_AutoTransitionedRenderPasses.push_back(m_UIRenderPass);

			CalculateAutoLayoutTransitions();

			// --------------------------------------------

			struct TEMP_RenderPassImageLayouts
			{
				std::vector<VkImageLayout> colorInitialLayouts;
				std::vector<VkImageLayout> colorFinalLayouts;
				VkImageLayout depthInitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				VkImageLayout depthFinalLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			};
			std::vector<TEMP_RenderPassImageLayouts> autoGeneratedLayouts(m_AutoTransitionedRenderPasses.size());
			for (i32 passIndex = 0; passIndex < (i32)m_AutoTransitionedRenderPasses.size(); ++passIndex)
			{
				VulkanRenderPass* pass = m_AutoTransitionedRenderPasses[passIndex];
				assert(pass->m_TargetColorAttachmentInitialLayouts.size() == pass->m_TargetColorAttachmentFinalLayouts.size());
				const u32 colorAttachmentCount = (u32)pass->m_TargetColorAttachmentInitialLayouts.size();
				autoGeneratedLayouts[passIndex].colorInitialLayouts.resize(colorAttachmentCount, VK_IMAGE_LAYOUT_UNDEFINED);
				autoGeneratedLayouts[passIndex].colorFinalLayouts.resize(colorAttachmentCount, VK_IMAGE_LAYOUT_UNDEFINED);
				for (u32 attachmentIndex = 0; attachmentIndex < colorAttachmentCount; ++attachmentIndex)
				{
					autoGeneratedLayouts[passIndex].colorInitialLayouts[attachmentIndex] = pass->m_TargetColorAttachmentInitialLayouts[attachmentIndex];
					autoGeneratedLayouts[passIndex].colorFinalLayouts[attachmentIndex] = pass->m_TargetColorAttachmentFinalLayouts[attachmentIndex];
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
			m_UIRenderPass->m_ColorAttachmentFormat = m_SwapChainImageFormat;
			m_UIRenderPass->m_DepthAttachmentFormat = m_DepthFormat;
			m_UIRenderPass->Create();

			// Writes to swapchain... TODO: manage swapchain FrameBuffer wrapper in this class and reference it from this pass (and any other that target the swapchain)
			m_UIRenderPass->m_FrameBuffer->width = m_SwapChainExtent.width;
			m_UIRenderPass->m_FrameBuffer->height = m_SwapChainExtent.height;

			const bool bPrintResultsDetailed = false;
			const bool bPrintResults = false;

			if (bPrintResultsDetailed)
			{
				PrintWarn("ColorInit ColorFinal - DepthInit DepthFinal\n");
			}

			i32 successCount = 0;
			for (i32 i = 0; i < (i32)m_AutoTransitionedRenderPasses.size(); ++i)
			{
				VulkanRenderPass* pass = m_AutoTransitionedRenderPasses[i];
				const TEMP_RenderPassImageLayouts& generatedPassLayouts = autoGeneratedLayouts[i];

				const bool bWritesToDepth = pass->m_DepthAttachmentFormat != VK_FORMAT_UNDEFINED;

				if (pass->m_TargetColorAttachmentInitialLayouts != generatedPassLayouts.colorInitialLayouts ||
					pass->m_TargetColorAttachmentFinalLayouts != generatedPassLayouts.colorFinalLayouts ||
					(bWritesToDepth &&
					(pass->m_TargetDepthAttachmentInitialLayout != generatedPassLayouts.depthInitialLayout ||
						pass->m_TargetDepthAttachmentFinalLayout != generatedPassLayouts.depthFinalLayout)))
				{
					if (bPrintResultsDetailed)
					{
						PrintWarn("Unexpected auto generated render pass image layout transitions in \"%s\":\n", pass->m_Name);

						PrintWarn("Actual:   ");
						if (generatedPassLayouts.colorInitialLayouts.size() > 1)
						{
							PrintWarn("{ ");
						}
						for (u32 attachmentIndex = 0; attachmentIndex < generatedPassLayouts.colorInitialLayouts.size(); ++attachmentIndex)
						{
							PrintWarn("%d ", generatedPassLayouts.colorInitialLayouts[attachmentIndex]);
						}
						if (generatedPassLayouts.colorInitialLayouts.size() > 1)
						{
							PrintWarn("} ");
						}
						if (generatedPassLayouts.colorFinalLayouts.size() > 1)
						{
							PrintWarn("{ ");
						}
						for (u32 attachmentIndex = 0; attachmentIndex < generatedPassLayouts.colorFinalLayouts.size(); ++attachmentIndex)
						{
							PrintWarn("%d ", generatedPassLayouts.colorFinalLayouts[attachmentIndex]);
						}
						if (generatedPassLayouts.colorFinalLayouts.size() > 1)
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
						if (pass->m_TargetColorAttachmentInitialLayouts.size() > 1)
						{
							PrintWarn("{ ");
						}
						for (u32 attachmentIndex = 0; attachmentIndex < pass->m_TargetColorAttachmentInitialLayouts.size(); ++attachmentIndex)
						{
							PrintWarn("%d ", pass->m_TargetColorAttachmentInitialLayouts[attachmentIndex]);
						}
						if (pass->m_TargetColorAttachmentInitialLayouts.size() > 1)
						{
							PrintWarn("} ");
						}
						if (pass->m_TargetColorAttachmentFinalLayouts.size() > 1)
						{
							PrintWarn("{ ");
						}
						for (u32 attachmentIndex = 0; attachmentIndex < pass->m_TargetColorAttachmentFinalLayouts.size(); ++attachmentIndex)
						{
							PrintWarn("%d ", pass->m_TargetColorAttachmentFinalLayouts[attachmentIndex]);
						}
						if (pass->m_TargetColorAttachmentFinalLayouts.size() > 1)
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
				VulkanRenderPass* prevPass = m_AutoTransitionedRenderPasses[i - 1];
				VulkanRenderPass* currPass = m_AutoTransitionedRenderPasses[i];

				std::vector<FrameBufferAttachmentID> unresolvedSampledAttachments(currPass->m_SampledAttachmentIDs);

				i32 prevPassIndex = i - 1;
				while (prevPassIndex >= 0 && !unresolvedSampledAttachments.empty())
				{
					prevPass = m_AutoTransitionedRenderPasses[prevPassIndex];

					if (prevPass->m_TargetColorAttachmentIDs.size() > 0)
					{
						auto sampledAttachmentIter = unresolvedSampledAttachments.begin();
						while (sampledAttachmentIter != unresolvedSampledAttachments.end())
						{
							bool bRemovedElement = false;
							std::vector<FrameBufferAttachmentID>::const_iterator targetAttachmentIter;
							do
							{
								targetAttachmentIter = Find(prevPass->m_TargetColorAttachmentIDs, *sampledAttachmentIter);
								if (targetAttachmentIter != prevPass->m_TargetColorAttachmentIDs.end())
								{
									const u32 targetAttachmentIndex = (u32)(targetAttachmentIter - prevPass->m_TargetColorAttachmentIDs.begin());
									prevPass->m_TargetColorAttachmentFinalLayouts[targetAttachmentIndex] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
									sampledAttachmentIter = unresolvedSampledAttachments.erase(sampledAttachmentIter);
									bRemovedElement = true;
									break;
								}
							} while (targetAttachmentIter != prevPass->m_TargetColorAttachmentIDs.end());

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
						auto sampledAttachmentIter = Find(unresolvedSampledAttachments, prevPass->m_TargetDepthAttachmentID);
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

					if (nextPass->m_TargetColorAttachmentIDs.size() > 0)
					{
						auto sampledAttachmentIter = unresolvedSampledAttachments.begin();
						while (sampledAttachmentIter != unresolvedSampledAttachments.end())
						{
							bool bRemovedElement = false;
							std::vector<FrameBufferAttachmentID>::const_iterator targetAttachmentIter;
							do
							{
								targetAttachmentIter = Find(nextPass->m_TargetColorAttachmentIDs, *sampledAttachmentIter);
								if (targetAttachmentIter != nextPass->m_TargetColorAttachmentIDs.end())
								{
									const u32 targetAttachmentIndex = (u32)(targetAttachmentIter - nextPass->m_TargetColorAttachmentIDs.begin());
									nextPass->m_TargetColorAttachmentInitialLayouts[targetAttachmentIndex] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
									sampledAttachmentIter = unresolvedSampledAttachments.erase(sampledAttachmentIter);
									bRemovedElement = true;
									break;
								}
							} while (targetAttachmentIter != nextPass->m_TargetColorAttachmentIDs.end());

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
						auto sampledAttachmentIter = Find(unresolvedSampledAttachments, nextPass->m_TargetDepthAttachmentID);
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

				for (auto nextPassTargetAttachmentIter = nextPass->m_TargetColorAttachmentIDs.begin(); nextPassTargetAttachmentIter != nextPass->m_TargetColorAttachmentIDs.end(); ++nextPassTargetAttachmentIter)
				{
					auto currPassTargetAttachmentIter = Find(currPass->m_TargetColorAttachmentIDs, *nextPassTargetAttachmentIter);
					if (currPassTargetAttachmentIter != currPass->m_TargetColorAttachmentIDs.end())
					{
						if (*nextPassTargetAttachmentIter == *currPassTargetAttachmentIter)
						{
							const size_t currPassTargetAttachmentIndex = currPassTargetAttachmentIter - currPass->m_TargetColorAttachmentIDs.begin();
							const size_t nextPassTargetAttachmentIndex = nextPassTargetAttachmentIter - nextPass->m_TargetColorAttachmentIDs.begin();
							currPass->m_TargetColorAttachmentFinalLayouts[currPassTargetAttachmentIndex] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
							nextPass->m_TargetColorAttachmentInitialLayouts[nextPassTargetAttachmentIndex] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
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
				assert(finalPass->m_TargetColorAttachmentFinalLayouts.size() == 1);
				finalPass->m_TargetColorAttachmentFinalLayouts[0] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
				finalPass->m_TargetDepthAttachmentFinalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			}
		}

		void VulkanRenderer::FillOutBufferDescriptorInfos(ShaderUniformContainer<BufferDescriptorInfo>* descriptors, UniformBufferList* uniformBufferList, ShaderID shaderID)
		{
			VulkanShader* shader = &m_Shaders[shaderID];

			if (shader->shader->constantBufferUniforms.HasUniform(U_UNIFORM_BUFFER_CONSTANT))
			{
				UniformBuffer* constantBuffer = uniformBufferList->Get(UniformBufferType::STATIC);
				descriptors->Add(U_UNIFORM_BUFFER_CONSTANT, BufferDescriptorInfo{ constantBuffer->buffer.m_Buffer, constantBuffer->data.size, UniformBufferType::STATIC });
			}

			if (shader->shader->dynamicBufferUniforms.HasUniform(U_UNIFORM_BUFFER_DYNAMIC))
			{
				UniformBuffer* dynamicBuffer = uniformBufferList->Get(UniformBufferType::DYNAMIC);
				// TODO: FIXME: BAD: CLEANUP:
				const VkDeviceSize dynamicBufferSize = sizeof(VulkanUniformBufferObjectData) * m_RenderObjects.size();
				descriptors->Add(U_UNIFORM_BUFFER_DYNAMIC, BufferDescriptorInfo{ dynamicBuffer->buffer.m_Buffer, dynamicBufferSize, UniformBufferType::DYNAMIC });
			}

			if (shader->shader->additionalBufferUniforms.HasUniform(U_PARTICLE_BUFFER))
			{
				UniformBuffer* particleBuffer = uniformBufferList->Get(UniformBufferType::PARTICLE_DATA);
				descriptors->Add(U_PARTICLE_BUFFER, BufferDescriptorInfo{ particleBuffer->buffer.m_Buffer, particleBuffer->data.size, UniformBufferType::PARTICLE_DATA });
			}
		}

		void VulkanRenderer::CreateDescriptorSet(RenderID renderID)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject)
			{
				return;
			}

			VulkanMaterial* material = &m_Materials.at(renderObject->materialID);
			VulkanShader* shader = &m_Shaders[material->material.shaderID];

			DescriptorSetCreateInfo createInfo = {};

			char debugName[256];
			snprintf(debugName, 256, "Render Object %s (render ID %u) descriptor set", renderObject->gameObject ? renderObject->gameObject->GetName().c_str() : "", renderID);
			createInfo.DBG_Name = debugName;
			createInfo.descriptorSet = &renderObject->descriptorSet;
			createInfo.descriptorSetLayout = &m_DescriptorSetLayouts[material->material.shaderID];
			createInfo.shaderID = material->material.shaderID;
			createInfo.uniformBufferList = &material->uniformBufferList;

			for (auto& pair : material->textures)
			{
				if (shader->shader->textureUniforms.HasUniform(pair.first))
				{
					VulkanTexture* texture = pair.second;
					assert(texture != nullptr);
					createInfo.imageDescriptors.Add(pair.first, ImageDescriptorInfo{ texture->imageView, m_LinMipLinSampler });
				}
			}

			if (shader->shader->textureUniforms.HasUniform(U_DEPTH_SAMPLER))
			{
				createInfo.imageDescriptors.Add(U_DEPTH_SAMPLER, ImageDescriptorInfo{ m_GBufferDepthAttachment->view, m_DepthSampler });
			}

			if (shader->shader->textureUniforms.HasUniform(U_SSAO_FINAL_SAMPLER))
			{
				VkImageView ssaoView = m_BlankTexture->imageView;
				if (m_SSAOSamplingData.ssaoEnabled)
				{
					if (m_bSSAOBlurEnabled)
					{
						ssaoView = m_SSAOBlurVFBColorAttachment0->view;
					}
					else
					{
						ssaoView = m_SSAOFBColorAttachment0->view;
					}
				}
				createInfo.imageDescriptors.Add(U_SSAO_FINAL_SAMPLER, ImageDescriptorInfo{ ssaoView, m_NearestClampEdgeSampler });
			}

			if (shader->shader->textureUniforms.HasUniform(U_SHADOW_SAMPLER))
			{
				VkImageView imageView = (m_DirectionalLight && m_DirectionalLight->data.castShadows) ? m_ShadowImageView : m_BlankTextureArr->imageView;
				createInfo.imageDescriptors.Add(U_SHADOW_SAMPLER, ImageDescriptorInfo{ imageView, m_DepthSampler });
			}

			for (u32 i = 0; i < material->material.sampledFrameBuffers.size(); ++i)
			{
				VkImageView imageView = *static_cast<VkImageView*>(material->material.sampledFrameBuffers[i].second);
				createInfo.imageDescriptors.Add(NextPowerOfTwo(U_FB_0_SAMPLER + i), ImageDescriptorInfo{ imageView, m_LinMipLinSampler });
			}

			FillOutBufferDescriptorInfos(&createInfo.bufferDescriptors, createInfo.uniformBufferList, createInfo.shaderID);

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
			Uniforms additionalBufferUniforms = m_Shaders[createInfo->shaderID].shader->additionalBufferUniforms;
			Uniforms textureUniforms = m_Shaders[createInfo->shaderID].shader->textureUniforms;

			std::vector<VkWriteDescriptorSet> writeDescriptorSets;
			writeDescriptorSets.reserve(createInfo->bufferDescriptors.Count() + createInfo->imageDescriptors.Count());

			u32 binding = 0;

			std::vector<VkDescriptorBufferInfo> bufferInfos(createInfo->bufferDescriptors.Count());
			u32 i = 0;
			for (auto& pair : createInfo->bufferDescriptors)
			{
				const u64 uniform = pair.first;
				assert((pair.second.type == UniformBufferType::DYNAMIC && dynamicBufferUniforms.HasUniform(uniform)) ||
					(pair.second.type == UniformBufferType::PARTICLE_DATA && additionalBufferUniforms.HasUniform(uniform)) ||
					(pair.second.type == UniformBufferType::STATIC && constantBufferUniforms.HasUniform(uniform)));
				assert(pair.second.buffer != VK_NULL_HANDLE);

				VkDescriptorType type;
				switch (pair.second.type)
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
				bufferInfo.buffer = pair.second.buffer;
				bufferInfo.range = pair.second.bufferSize;
				writeDescriptorSets.push_back(vks::writeDescriptorSet(*createInfo->descriptorSet, type, binding, &bufferInfo));

				++binding;
				++i;
			}

			std::vector<VkDescriptorImageInfo> imageInfos(createInfo->imageDescriptors.Count());
			i = 0;
			for (auto& pair : createInfo->imageDescriptors)
			{
				const u64 uniform = pair.first;
				assert(textureUniforms.HasUniform(uniform));

				VkDescriptorImageInfo& imageInfo = imageInfos[i];
				imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				if (pair.second.imageView != VK_NULL_HANDLE)
				{
					imageInfo.imageView = pair.second.imageView;
					if (pair.second.imageSampler != VK_NULL_HANDLE)
					{
						imageInfo.sampler = pair.second.imageSampler;
					}
					else
					{
						imageInfo.sampler = m_LinMipLinSampler;
					}
				}
				else
				{
					imageInfo.imageView = m_BlankTexture->imageView;
					imageInfo.sampler = m_BlankTexture->sampler;
				}
				writeDescriptorSets.push_back(vks::writeDescriptorSet(*createInfo->descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, binding, &imageInfo));

				++binding;
				++i;
			}

			if (!writeDescriptorSets.empty())
			{
				vkUpdateDescriptorSets(m_VulkanDevice->m_LogicalDevice, (u32)writeDescriptorSets.size(), writeDescriptorSets.data(), 0u, nullptr);
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
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_COMPUTE_BIT },

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

				{ U_PARTICLE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
				VK_SHADER_STAGE_COMPUTE_BIT },
			};

			std::vector<VkDescriptorSetLayoutBinding> bindings;

			for (DescriptorSetInfo& descSetInfo : descriptorSets)
			{
				if (shader->shader->constantBufferUniforms.HasUniform(descSetInfo.uniform) ||
					shader->shader->dynamicBufferUniforms.HasUniform(descSetInfo.uniform) ||
					shader->shader->additionalBufferUniforms.HasUniform(descSetInfo.uniform) ||
					shader->shader->textureUniforms.HasUniform(descSetInfo.uniform))
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

			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_VulkanDevice->m_LogicalDevice, &layoutInfo, nullptr, descriptorSetLayout));
		}

		bool VulkanRenderer::FindOrCreateMaterialByName(const std::string& materialName, MaterialID& materialID)
		{
			for (u32 i = 0; i < (u32)m_Materials.size(); ++i)
			{
				auto matIter = m_Materials.find(i);
				if (matIter != m_Materials.end() && matIter->second.material.name.compare(materialName) == 0)
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
			for (u32 i = 0; i < (u32)m_Shaders.size(); ++i)
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
				if (vulkanTexture && !filePath.empty() && filePath.compare(vulkanTexture->relativeFilePath) == 0)
				{
					return vulkanTexture;
				}
			}

			return nullptr;
		}

		bool VulkanRenderer::RemoveLoadedTexture(VulkanTexture* texture, bool bDestroy)
		{
			for (auto iter = m_LoadedTextures.begin(); iter != m_LoadedTextures.end(); ++iter)
			{
				if (*iter == texture)
				{
					if (bDestroy)
					{
						delete* iter;
					}
					m_LoadedTextures[iter - m_LoadedTextures.begin()] = nullptr;
					return true;
				}
			}

			return false;
		}

		void VulkanRenderer::CreateGraphicsPipeline(RenderID renderID, bool bSetCubemapRenderPass)
		{
			FLEX_UNUSED(bSetCubemapRenderPass);

			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject || !renderObject->vertexBufferData)
			{
				return;
			}

			VulkanMaterial* material = &m_Materials.at(renderObject->materialID);
			VulkanShader& shader = m_Shaders[material->material.shaderID];

			GraphicsPipelineCreateInfo pipelineCreateInfo = {};
			char debugName[256];
			snprintf(debugName, 256, "Render Object %s (render ID %u) graphics pipeline", renderObject->gameObject ? renderObject->gameObject->GetName().c_str() : "", renderID);
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

			const bool bUseVertexStage = !shader.shader->vertexShaderFilePath.empty();
			const bool bUseFragmentStage = !shader.shader->fragmentShaderFilePath.empty();
			const bool bUseGeometryStage = !shader.shader->geometryShaderFilePath.empty();

			VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
			if (bUseVertexStage)
			{
				if ((VkShaderModule)shader.vertShaderModule == VK_NULL_HANDLE)
				{
					PrintError("Failed to create graphics pipeline, required vertex shader module is empty\n");
					return;
				}
				else
				{
					vertShaderStageInfo = vks::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, shader.vertShaderModule);
					shaderStages.push_back(vertShaderStageInfo);
				}
			}

			VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
			if (bUseFragmentStage)
			{
				if ((VkShaderModule)shader.fragShaderModule == VK_NULL_HANDLE)
				{
					PrintError("Failed to create graphics pipeline, required fragment shader module is empty\n");
					return;
				}
				else
				{
					fragShaderStageInfo = vks::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, shader.fragShaderModule);
					fragShaderStageInfo.pSpecializationInfo = createInfo->fragSpecializationInfo;
					shaderStages.push_back(fragShaderStageInfo);
				}
			}

			VkPipelineShaderStageCreateInfo geomShaderStageInfo = {};
			if (bUseGeometryStage)
			{
				if ((VkShaderModule)shader.geomShaderModule == VK_NULL_HANDLE)
				{
					PrintError("Failed to create graphics pipeline, required geometry shader module is empty\n");
					return;
				}
				else
				{
					geomShaderStageInfo = vks::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_GEOMETRY_BIT, shader.geomShaderModule);
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
			pipelineInfo.stageCount = (u32)shaderStages.size();
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
			m_GBufferColorAttachment0->width = m_SwapChainExtent.width;
			m_GBufferColorAttachment0->height = m_SwapChainExtent.height;

			m_GBufferColorAttachment1->width = m_SwapChainExtent.width;
			m_GBufferColorAttachment1->height = m_SwapChainExtent.height;

			m_OffscreenFB0ColorAttachment0->width = m_SwapChainExtent.width;
			m_OffscreenFB0ColorAttachment0->height = m_SwapChainExtent.height;

			m_OffscreenFB1ColorAttachment0->width = m_SwapChainExtent.width;
			m_OffscreenFB1ColorAttachment0->height = m_SwapChainExtent.height;

			m_OffscreenFB0DepthAttachment->width = m_SwapChainExtent.width;
			m_OffscreenFB0DepthAttachment->height = m_SwapChainExtent.height;

			m_OffscreenFB1DepthAttachment->width = m_SwapChainExtent.width;
			m_OffscreenFB1DepthAttachment->height = m_SwapChainExtent.height;

			m_HistoryBuffer->width = m_SwapChainExtent.width;
			m_HistoryBuffer->height = m_SwapChainExtent.height;

			m_SSAORes = glm::vec2u(m_SwapChainExtent.width / 2, m_SwapChainExtent.height / 2);

			m_SSAOFBColorAttachment0->width = m_SSAORes.x;
			m_SSAOFBColorAttachment0->height = m_SSAORes.y;

			m_SSAOBlurHFBColorAttachment0->width = m_SwapChainExtent.width;
			m_SSAOBlurHFBColorAttachment0->height = m_SwapChainExtent.height;

			m_SSAOBlurVFBColorAttachment0->width = m_SwapChainExtent.width;
			m_SSAOBlurVFBColorAttachment0->height = m_SwapChainExtent.height;

			// GBuffer frame buffer attachments
			{
				CreateAttachment(m_VulkanDevice, m_GBufferColorAttachment0, "GBuffer image 0", "GBuffer image view 0");
				CreateAttachment(m_VulkanDevice, m_GBufferColorAttachment1, "GBuffer image 1", "GBuffer image view 1");
			}

			// Offscreen frame buffer attachments
			{
				assert(m_OffscreenFB0ColorAttachment0->width == m_OffscreenFB1ColorAttachment0->width);
				assert(m_OffscreenFB0ColorAttachment0->height == m_OffscreenFB1ColorAttachment0->height);

				CreateAttachment(m_VulkanDevice, m_OffscreenFB0ColorAttachment0, "Offscreen 0 image", "Offscreen 0 image view");
				CreateAttachment(m_VulkanDevice, m_OffscreenFB1ColorAttachment0, "Offscreen 1 image", "Offscreen 1 image view");

				// Deferred shading specifies initial layout of color attachment optimal
				m_OffscreenFB0ColorAttachment0->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				// Post process specifies initial layout of color attachment optimal
				m_OffscreenFB1ColorAttachment0->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
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
				CreateAttachment(m_VulkanDevice, m_SSAOFBColorAttachment0, "SSAO image", "SSAO image view");
			}

			// SSAO Blur frame buffers
			{
				assert(m_SSAOBlurHFBColorAttachment0->width == m_SSAOBlurVFBColorAttachment0->width);
				assert(m_SSAOBlurHFBColorAttachment0->height == m_SSAOBlurVFBColorAttachment0->height);

				CreateAttachment(m_VulkanDevice, m_SSAOBlurHFBColorAttachment0, "SSAO Blur Horizontal image", "SSAO Blur Horizontal image view");
				CreateAttachment(m_VulkanDevice, m_SSAOBlurVFBColorAttachment0, "SSAO Blur Vertical image", "SSAO Blur Vertical image view");
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
			linMipLinSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			linMipLinSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			linMipLinSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			linMipLinSamplerCreateInfo.mipLodBias = 0.0f;
			linMipLinSamplerCreateInfo.minLod = 0.0f;
			linMipLinSamplerCreateInfo.maxLod = 1.0f;
			linMipLinSamplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			VK_CHECK_RESULT(vkCreateSampler(m_VulkanDevice->m_LogicalDevice, &linMipLinSamplerCreateInfo, nullptr, m_LinMipLinSampler.replace()));
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
			depthSamplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			VK_CHECK_RESULT(vkCreateSampler(m_VulkanDevice->m_LogicalDevice, &depthSamplerCreateInfo, nullptr, m_DepthSampler.replace()));
			SetSamplerName(m_VulkanDevice, m_DepthSampler, "Depth sampler");
		}

		void VulkanRenderer::CreateFrameBuffers()
		{
			// TODO: Create in shader render pass?
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
				imageCreateInfo.arrayLayers = SHADOW_CASCADE_COUNT;
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
				fullImageView.subresourceRange.layerCount = SHADOW_CASCADE_COUNT;
				fullImageView.image = m_ShadowImage;
				fullImageView.flags = 0;
				VK_CHECK_RESULT(vkCreateImageView(m_VulkanDevice->m_LogicalDevice, &fullImageView, nullptr, m_ShadowImageView.replace()));
				SetImageViewName(m_VulkanDevice, m_ShadowImageView, "Shadow cascade image view (main)");

				// One frame buffer & view per cascade
				for (u32 i = 0; i < SHADOW_CASCADE_COUNT; ++i)
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
					snprintf(imageViewName, 256, "Shadow cascade %u image view", i);
					SetImageViewName(m_VulkanDevice, m_ShadowCascades[i]->imageView, imageViewName);

					VkFramebufferCreateInfo shadowFramebufferCreateInfo = vks::framebufferCreateInfo(*m_ShadowRenderPass);
					shadowFramebufferCreateInfo.pAttachments = &m_ShadowCascades[i]->imageView;
					shadowFramebufferCreateInfo.attachmentCount = 1;
					shadowFramebufferCreateInfo.width = SHADOW_CASCADE_RES;
					shadowFramebufferCreateInfo.height = SHADOW_CASCADE_RES;

					char frameBufferName[256];
					snprintf(frameBufferName, 256, "Shadow cascade %u frame buffer", i);

					m_ShadowCascades[i]->frameBuffer.Create(&shadowFramebufferCreateInfo, m_ShadowRenderPass, frameBufferName);

					FrameBufferAttachment::CreateInfo attachmentCreateInfo = {};
					attachmentCreateInfo.width = SHADOW_CASCADE_RES;
					attachmentCreateInfo.height = SHADOW_CASCADE_RES;
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
		}

		// TODO: Test that this still works
		void VulkanRenderer::PrepareCubemapFrameBuffer()
		{
			// const u32 frameBufferColorAttachmentCount = 2;

			m_GBufferCubemapColorAttachment0->width = m_CubemapFramebufferSize.x;
			m_GBufferCubemapColorAttachment0->height = m_CubemapFramebufferSize.y;

			m_GBufferCubemapColorAttachment1->width = m_CubemapFramebufferSize.x;
			m_GBufferCubemapColorAttachment1->height = m_CubemapFramebufferSize.y;

			// Color attachments
			CreateAttachment(m_VulkanDevice, m_GBufferCubemapColorAttachment0, "GBuffer cubemap color image 0", "GBuffer cubemap color image view 0");
			CreateAttachment(m_VulkanDevice, m_GBufferCubemapColorAttachment1, "GBuffer cubemap color image 1", "GBuffer cubemap color image view 1");

			// TODO: Make PBR cubemap renderpass create this framebuffer

			//// Depth attachment
			//// Set up separate render pass with references to the color and depth attachments
			//std::vector<VkAttachmentDescription> attachmentDescs(frameBufferColorAttachmentCount + 1);
			//attachmentDescs[0] = vks::attachmentDescription(m_GBufferCubemapColorAttachment0->format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			//attachmentDescs[1] = vks::attachmentDescription(m_GBufferCubemapColorAttachment1->format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
			//attachments.push_back(m_GBufferCubemapColorAttachment0->view);
			//attachments.push_back(m_GBufferCubemapColorAttachment1->view);
			//attachments.push_back(m_GBufferCubemapDepthAttachment->view);

			//VkFramebufferCreateInfo fbufCreateInfo = vks::framebufferCreateInfo(m_DeferredCubemapRenderPass);
			//fbufCreateInfo.pAttachments = attachments.data();
			//fbufCreateInfo.attachmentCount = static_cast<u32>(attachments.size());
			//fbufCreateInfo.width = m_GBufferCubemapColorAttachment0->width;
			//fbufCreateInfo.height = m_GBufferCubemapColorAttachment0->height;
			//fbufCreateInfo.layers = 6;

			//VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &fbufCreateInfo, nullptr, frameBuffer.replace()));
			//VulkanRenderer::SetFramebufferName(m_VulkanDevice, frameBuffer, "GBuffer Cubemap frame buffer");
		}

		void VulkanRenderer::RemoveMaterial(MaterialID materialID)
		{
			assert(materialID != InvalidMaterialID);

			m_Materials.erase(materialID);
		}

		void VulkanRenderer::FillOutGBufferFrameBufferAttachments(std::vector<Pair<std::string, void*>>& outVec)
		{
			outVec.emplace_back("GBuffer frame buffer attachment 0", (void*)&m_GBufferColorAttachment0->view);
			outVec.emplace_back("GBuffer frame buffer attachment 1", (void*)&m_GBufferColorAttachment1->view);
		}

		void VulkanRenderer::PhysicsDebugRender()
		{
			btDiscreteDynamicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
			physicsWorld->debugDrawWorld();
		}

		void VulkanRenderer::CreateStaticVertexBuffers()
		{
			auto renderObjectValid = [this](VulkanRenderObject* renderObject, u32 staticVertexBufferIndex)
			{
				return renderObject &&
					renderObject->vertexBufferData &&
					!m_Materials[renderObject->materialID].material.bDynamic &&
					m_Shaders[m_Materials.at(renderObject->materialID).material.shaderID].shader->staticVertexBufferIndex == staticVertexBufferIndex;
			};

			for (u32 staticVertexBufferIndex = 0; staticVertexBufferIndex < m_StaticVertexBuffers.size(); ++staticVertexBufferIndex)
			{
				auto& vertexBufferPair = m_StaticVertexBuffers[staticVertexBufferIndex];
				VulkanBuffer* vertexBuffer = vertexBufferPair.second;

				u32 requiredMemory = 0;

				for (VulkanRenderObject* renderObject : m_RenderObjects)
				{
					if (renderObjectValid(renderObject, staticVertexBufferIndex))
					{
						requiredMemory += renderObject->vertexBufferData->VertexBufferSize;
					}
				}

				if (requiredMemory == 0)
				{
					continue;
				}

				const u32 vertexBufferSize = requiredMemory;

				void* vertexDataStart = malloc(vertexBufferSize);
				if (!vertexDataStart)
				{
					PrintError("Failed to allocate %d bytes for static vertex buffer\n", vertexBufferSize);
					return;
				}

				void* vertexBufferData = vertexDataStart;

				u32 vertexCount = 0;
				for (VulkanRenderObject* renderObject : m_RenderObjects)
				{
					if (renderObjectValid(renderObject, staticVertexBufferIndex))
					{
						renderObject->vertexOffset = vertexCount;

						memcpy(vertexBufferData, renderObject->vertexBufferData->vertexData, renderObject->vertexBufferData->VertexBufferSize);

						vertexCount += renderObject->vertexBufferData->VertexCount;

						vertexBufferData = (char*)vertexBufferData + renderObject->vertexBufferData->VertexBufferSize;
					}
				}

				if (vertexBufferSize == 0 || vertexCount == 0)
				{
					PrintError("Failed to create static vertex buffer\n");
					return;
				}

				assert(vertexBufferData == ((char*)vertexDataStart + vertexBufferSize));

				CreateAndUploadToStaticVertexBuffer(vertexBuffer, vertexDataStart, vertexBufferSize);
				free(vertexDataStart);
			}
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
				Shader* shader = m_Shaders[shaderID].shader;
				if (shader->dynamicVertexBufferSize != 0)
				{
					const u32 stride = CalculateVertexStride(shader->vertexAttributes);
					const u32 dynamicVertexBufferIndex = GetDynamicVertexIndexBufferIndex(stride);
					dynamicVertexBufferSizes[dynamicVertexBufferIndex].vertMemoryReq += shader->dynamicVertexBufferSize;
					u32 vertexCount = (u32)glm::ceil(shader->dynamicVertexBufferSize / (real)stride / sizeof(real));
					// Assume worst case of no shared verts
					dynamicVertexBufferSizes[dynamicVertexBufferIndex].indexMemoryReq += vertexCount * sizeof(u32);
				}
			}

			for (u32 bufferIndex = 0; bufferIndex < m_DynamicVertexIndexBufferPairs.size(); ++bufferIndex)
			{
				if (dynamicVertexBufferSizes[bufferIndex].vertMemoryReq > 0)
				{
					CreateDynamicVertexBuffer(m_DynamicVertexIndexBufferPairs[bufferIndex].second->vertexBuffer, dynamicVertexBufferSizes[bufferIndex].vertMemoryReq);
					CreateDynamicIndexBuffer(m_DynamicVertexIndexBufferPairs[bufferIndex].second->indexBuffer, dynamicVertexBufferSizes[bufferIndex].indexMemoryReq);
				}
			}
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

			if (size == 0)
			{
				return;
			}

			void* vertexDataStart = malloc(size);
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
					renderObject->gameObject->CastsShadow() &&
					renderObject->gameObject->IsVisible())
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

		void VulkanRenderer::CreateStaticIndexBuffers()
		{
			std::vector<u32> indices;

			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject &&
					renderObject->bIndexed &&
					!m_Materials[renderObject->materialID].material.bDynamic)
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

		void VulkanRenderer::CreateShadowIndexBuffer()
		{
			std::vector<u32> indices;

			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject &&
					renderObject->bIndexed)
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

		void VulkanRenderer::CreateDescriptorPool()
		{
			std::vector<VkDescriptorPoolSize> poolSizes
			{
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_NUM_DESC_COMBINED_IMAGE_SAMPLERS },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_NUM_DESC_UNIFORM_BUFFERS },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, MAX_NUM_DESC_DYNAMIC_UNIFORM_BUFFERS },
				{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, MAX_NUM_DESC_DYNAMIC_STORAGE_BUFFERS },
			};

			VkDescriptorPoolCreateInfo poolInfo = vks::descriptorPoolCreateInfo(poolSizes, MAX_NUM_DESC_SETS);
			// TODO: Avoid using this flag at all
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // Allow descriptor sets to be added/removed often

			VK_CHECK_RESULT(vkCreateDescriptorPool(m_VulkanDevice->m_LogicalDevice, &poolInfo, nullptr, m_DescriptorPool.replace()));
		}

		u32 VulkanRenderer::AllocateDynamicUniformBuffer(u32 dynamicDataSize, void** data, i32 maxObjectCount /* = -1 */)
		{
			size_t uboAlignment = (size_t)m_VulkanDevice->m_PhysicalDeviceProperties.limits.minUniformBufferOffsetAlignment;
			size_t dynamicAllignment = (dynamicDataSize / uboAlignment) * uboAlignment + ((dynamicDataSize % uboAlignment) > 0 ? uboAlignment : 0);

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
				// TODO: FIXME: Use better metric for initial size
				maxObjectCount = (i32)m_RenderObjects.size();
			}
			size_t dynamicBufferSize = (size_t)maxObjectCount * m_DynamicAlignment;

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

		void VulkanRenderer::BatchRenderObjects()
		{
			if (m_DirtyFlagBits & DirtyFlags::STATIC_DATA)
			{
				CreateStaticVertexBuffers();
				CreateStaticIndexBuffers();
			}
			if (m_DirtyFlagBits & DirtyFlags::SHADOW_DATA)
			{
				CreateShadowVertexBuffer();
				CreateShadowIndexBuffer();
			}

			m_DirtyFlagBits = DirtyFlags::CLEAN;

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

				// TODO: Iterate over other definition of vertex buffers
				for (u32 shaderID = 0; shaderID < m_Shaders.size(); ++shaderID)
				{
					const bool bDeferred = m_Shaders[shaderID].shader->numAttachments > 1;
					ShaderBatch* shaderBatch = (bDeferred ? &m_DeferredObjectBatches : &m_ForwardObjectBatches);

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

						for (auto& matPair : m_Materials)
						{
							VulkanMaterial& material = matPair.second;

							if (material.material.shaderID == shaderID && ((u32)material.material.bDynamic) == dynamic)
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
										(VkPipeline)renderObject->graphicsPipeline != 0 &&
										renderObject->materialID == matPair.first)
									{
										UniformBuffer* dynamicBuffer = material.uniformBufferList.Get(UniformBufferType::DYNAMIC);
										if (dynamicBuffer)
										{
											dynamicUBOOffset += RoundUp(dynamicBuffer->data.size, m_DynamicAlignment);
										}
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
			}

			ShaderBatchPair shadowShaderBatch;
			shadowShaderBatch.batch.batches.resize(1);
			u32 dynamicShadowUBOOffset = 0;
			for (u32 i = 0; i < (u32)m_RenderObjects.size(); ++i)
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
			Print("Batched %u render objects into %u batches in %.2fms\n", (u32)m_RenderObjects.size(), renderObjBatchCount, blockMS);
		}

		void VulkanRenderer::DrawShaderBatch(const ShaderBatchPair& shaderBatch, VkCommandBuffer& commandBuffer, DrawCallInfo* drawCallInfo /* = nullptr */)
		{
			ShaderID shaderID = shaderBatch.shaderID;
			if (drawCallInfo && drawCallInfo->materialIDOverride != InvalidMaterialID)
			{
				shaderID = m_Materials.at(drawCallInfo->materialIDOverride).material.shaderID;
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
				u32 dynamicVertexIndexBufferIndex = GetDynamicVertexIndexBufferIndex(CalculateVertexStride(m_Shaders[shaderID].shader->vertexAttributes));
				u32 staticVertexBufferIndex = GetStaticVertexIndexBufferIndex(CalculateVertexStride(m_Shaders[shaderID].shader->vertexAttributes));
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
				if (drawCallInfo && drawCallInfo->materialIDOverride != InvalidMaterialID)
				{
					matID = drawCallInfo->materialIDOverride;
				}

				VulkanMaterial& mat = m_Materials.at(matID);
				VulkanShader& shader = m_Shaders[mat.material.shaderID];
				assert(mat.material.shaderID == shaderID);

				assert(mat.material.bDynamic == shaderBatch.bDynamic);

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
					}

					vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

					BindDescriptorSet(&mat, dynamicUBOOffset, commandBuffer, pipelineLayout, descriptorSet);

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
							BaseCamera* cam = g_CameraManager->CurrentCamera();
							mat.material.pushConstantBlock->SetData(cam->GetView(), cam->GetProjection());
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
							vkCmdDraw(commandBuffer, renderObject->vertexBufferData->VertexCount, 1, renderObject->vertexOffset, 0);
						}
						else
						{
							vkCmdDraw(commandBuffer, renderObject->vertexBufferData->VertexCount, 1, renderObject->shadowVertexOffset, 0);
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
			VulkanMaterial& material = m_Materials.at(materialID);

			VkDeviceSize offsets[1] = { 0 };
			BindDescriptorSet(&material, 0, commandBuffer, pipelineLayout, descriptorSet);

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
			clearValues[0].color = m_ClearColor;
			clearValues[1].depthStencil = { 0.0f, 0 };

			VulkanMaterial& material = m_Materials.at(materialID);

			VkViewport fullscreenViewport = bFlipViewport ?
				vks::viewportFlipped((real)m_SwapChainExtent.width, (real)m_SwapChainExtent.height, 0.0f, 1.0f) :
				vks::viewport((real)m_SwapChainExtent.width, (real)m_SwapChainExtent.height, 0.0f, 1.0f);
			VkRect2D fullscreenScissor = vks::scissor(0u, 0u, m_SwapChainExtent.width, m_SwapChainExtent.height);

			VkDeviceSize offsets[1] = { 0 };

			renderPass->Begin(commandBuffer, clearValues.data(), (u32)clearValues.size());

			{
				BindDescriptorSet(&material, 0, commandBuffer, pipelineLayout, descriptorSet);

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

					VkClearValue depthStencilClearValue = { 0.0f, 0 };

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

					if (m_CascadedShadowMapPushConstantBlock == nullptr)
					{
						m_CascadedShadowMapPushConstantBlock = new Material::PushConstantBlock();
					}

					for (u32 c = 0; c < SHADOW_CASCADE_COUNT; ++c)
					{
						m_ShadowRenderPass->Begin_WithFrameBuffer(m_OffScreenCmdBuffer, (VkClearValue*)&depthStencilClearValue, 1, &m_ShadowCascades[c]->frameBuffer);

						DrawCallInfo shadowDrawCallInfo = {};
						shadowDrawCallInfo.materialIDOverride = m_ShadowMaterialID;
						shadowDrawCallInfo.graphicsPipelineOverride = (u64)(VkPipeline)m_ShadowGraphicsPipeline;
						shadowDrawCallInfo.pipelineLayoutOverride = (u64)(VkPipelineLayout)m_ShadowPipelineLayout;
						shadowDrawCallInfo.descriptorSetOverride = (u64)(VkDescriptorSet)m_ShadowDescriptorSet;
						shadowDrawCallInfo.bRenderingShadows = true;

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
				gBufClearValues[0].color = m_ClearColor;
				gBufClearValues[1].color = m_ClearColor;
				gBufClearValues[2].depthStencil = { 0.0f, 0 };

				m_DeferredRenderPass->Begin(m_OffScreenCmdBuffer, gBufClearValues.data(), (u32)gBufClearValues.size());

				VkViewport fullScreenViewport = vks::viewportFlipped((real)m_GBufferColorAttachment0->width, (real)m_GBufferColorAttachment0->height, 0.0f, 1.0f);
				vkCmdSetViewport(m_OffScreenCmdBuffer, 0, 1, &fullScreenViewport);

				VkRect2D fullScreenScissor = vks::scissor(0u, 0u, m_GBufferColorAttachment0->width, m_GBufferColorAttachment0->height);
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

				if (m_SSAOSamplingData.ssaoEnabled)
				{
					BeginGPUTimeStamp(m_OffScreenCmdBuffer, "SSAO");

					BeginDebugMarkerRegion(m_OffScreenCmdBuffer, "SSAO");

					m_SSAORenderPass->Begin(m_OffScreenCmdBuffer, (VkClearValue*)&m_ClearColor, 1);

					assert(m_SSAOShaderID != InvalidShaderID);

					vkCmdBindPipeline(m_OffScreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SSAOGraphicsPipeline);

					VkViewport ssaoViewport = vks::viewportFlipped((real)m_SSAOFBColorAttachment0->width, (real)m_SSAOFBColorAttachment0->height, 0.0f, 1.0f);
					vkCmdSetViewport(m_OffScreenCmdBuffer, 0, 1, &ssaoViewport);

					VkRect2D ssaoScissor = vks::scissor(0u, 0u, m_SSAOFBColorAttachment0->width, m_SSAOFBColorAttachment0->height);
					vkCmdSetScissor(m_OffScreenCmdBuffer, 0, 1, &ssaoScissor);

					RenderFullscreenTri(m_OffScreenCmdBuffer, m_SSAOMatID, m_SSAOGraphicsPipelineLayout, m_SSAODescSet);

					m_SSAORenderPass->End();

					EndDebugMarkerRegion(m_OffScreenCmdBuffer, "End SSAO");

					//
					// SSAO blur
					//

					if (m_bSSAOBlurEnabled)
					{
						BeginDebugMarkerRegion(m_OffScreenCmdBuffer, "SSAO Blur");

						// Horizontal pass
						m_SSAOBlurHRenderPass->Begin(m_OffScreenCmdBuffer, (VkClearValue*)&m_ClearColor, 1);

						assert(m_SSAOBlurShaderID != InvalidShaderID);

						vkCmdBindPipeline(m_OffScreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SSAOBlurHGraphicsPipeline);

						VkViewport ssaoBlurViewport = vks::viewportFlipped((real)m_SSAOBlurHFBColorAttachment0->width, (real)m_SSAOBlurHFBColorAttachment0->height, 0.0f, 1.0f);
						vkCmdSetViewport(m_OffScreenCmdBuffer, 0, 1, &ssaoBlurViewport);

						VkRect2D ssaoBlurScissor = vks::scissor(0u, 0u, m_SSAOBlurHFBColorAttachment0->width, m_SSAOBlurHFBColorAttachment0->height);
						vkCmdSetScissor(m_OffScreenCmdBuffer, 0, 1, &ssaoBlurScissor);

						BindDescriptorSet(&m_Materials[m_SSAOBlurMatID], 0 * m_DynamicAlignment, m_OffScreenCmdBuffer, m_SSAOBlurGraphicsPipelineLayout, m_SSAOBlurHDescSet);

						vkCmdBindVertexBuffers(m_OffScreenCmdBuffer, 0, 1, &m_FullScreenTriVertexBuffer->m_Buffer, offsets);

						UniformOverrides overrides = {};
						overrides.overridenUniforms.AddUniform(U_SSAO_BLUR_DATA_DYNAMIC);
						overrides.bSSAOVerticalPass = false;
						UpdateDynamicUniformBuffer(m_SSAOBlurMatID, 0 * m_DynamicAlignment, MAT4_IDENTITY, &overrides);

						vkCmdDraw(m_OffScreenCmdBuffer, m_FullScreenTriVertexBufferData.VertexCount, 1, 0, 0);

						m_SSAOBlurHRenderPass->End();

						// Vertical pass
						m_SSAOBlurVRenderPass->Begin(m_OffScreenCmdBuffer, (VkClearValue*)&m_ClearColor, 1);

						vkCmdBindPipeline(m_OffScreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SSAOBlurVGraphicsPipeline);

						BindDescriptorSet(&m_Materials[m_SSAOBlurMatID], 1 * m_DynamicAlignment, m_OffScreenCmdBuffer, m_SSAOBlurGraphicsPipelineLayout, m_SSAOBlurVDescSet);

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
			BeginGPUTimeStamp(commandBuffer, "Simulate Particles");
			BeginDebugMarkerRegion(commandBuffer, "Simulate Particles");

			for (VulkanParticleSystem* particleSystem : m_ParticleSystems)
			{
				if (!particleSystem || !particleSystem->system->IsVisible() || !particleSystem->system->bEnabled)
				{
					continue;
				}

				VulkanMaterial& particleSimMat = m_Materials.at(particleSystem->system->simMaterialID);

				UniformBuffer* particleBuffer = particleSimMat.uniformBufferList.Get(UniformBufferType::PARTICLE_DATA);

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

			BeginGPUTimeStamp(commandBuffer, "Forward");

			{
				BeginDebugMarkerRegion(commandBuffer, "Shade deferred");

				VulkanRenderObject* gBufferObject = GetRenderObject(m_GBufferQuadRenderID);

				RenderFullscreenTri(commandBuffer, m_DeferredCombineRenderPass, gBufferObject->materialID,
					gBufferObject->pipelineLayout, gBufferObject->graphicsPipeline, gBufferObject->descriptorSet, true);

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
			clearValues[0].color = m_ClearColor;
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

					EndDebugMarkerRegion(commandBuffer, "End Editor objects");
				}

				{
					BeginDebugMarkerRegion(commandBuffer, "World Space Sprites");

					EnqueueWorldSpaceSprites();
					DrawSpriteBatch(m_QueuedWSSprites, commandBuffer);
					m_QueuedWSSprites.clear();

					EndDebugMarkerRegion(commandBuffer, "End World Space Sprites");
					BeginDebugMarkerRegion(commandBuffer, "World Space Text");

					EnqueueWorldSpaceText();
					DrawTextWS(commandBuffer);

					EndDebugMarkerRegion(commandBuffer, "End World Space Text");
				}
			}
			m_ForwardRenderPass->End();

			// Only needed on the first frame
			m_OffscreenFB0ColorAttachment0->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			//
			// Post process pass
			//

			{
				BeginGPUTimeStamp(commandBuffer, "Post Process");
				BeginDebugMarkerRegion(commandBuffer, "Post process");

				RenderFullscreenTri(commandBuffer, m_PostProcessRenderPass, m_PostProcessMatID,
					m_PostProcessGraphicsPipelineLayout, m_PostProcessGraphicsPipeline, m_PostProcessDescriptorSet, true);

				EndDebugMarkerRegion(commandBuffer, "End Post process");
				EndGPUTimeStamp(commandBuffer, "Post Process");
			}

			// Post process render pass transitioned this to shader read only optimal
			m_OffscreenFB1ColorAttachment0->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			{
				BeginDebugMarkerRegion(commandBuffer, "Gamma Correct");

				RenderFullscreenTri(commandBuffer, m_GammaCorrectRenderPass, m_GammaCorrectMaterialID,
					m_GammaCorrectGraphicsPipelineLayout, m_GammaCorrectGraphicsPipeline, m_GammaCorrectDescriptorSet, true);

				EndDebugMarkerRegion(commandBuffer, "End Gamma Correct");
			}

			if (m_bEnableTAA)
			{
				BeginGPUTimeStamp(commandBuffer, "TAA");

				BeginDebugMarkerRegion(commandBuffer, "TAA Resolve");

				VulkanMaterial& TAAMat = m_Materials[m_TAAResolveMaterialID];
				TAAMat.material.pushConstantBlock->SetData(m_TAA_ks, sizeof(real) * 2);

				VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
				vkCmdPushConstants(commandBuffer, m_TAAResolveGraphicsPipelineLayout, stages, 0, TAAMat.material.pushConstantBlock->size, TAAMat.material.pushConstantBlock->data);

				RenderFullscreenTri(commandBuffer, m_TAAResolveRenderPass, m_TAAResolveMaterialID,
					m_TAAResolveGraphicsPipelineLayout, m_TAAResolveGraphicsPipeline, m_TAAResolveDescriptorSet, true);

				EndDebugMarkerRegion(commandBuffer, "End TAA Resolve");

				EndGPUTimeStamp(commandBuffer, "TAA");

				{
					// Should be auto-transitioned by TAA resolve pass, but isn't??
					m_OffscreenFB1ColorAttachment0->TransitionToLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_GraphicsQueue, commandBuffer);

					m_HistoryBuffer->TransitionToLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, commandBuffer);

					CopyImage(m_VulkanDevice, m_GraphicsQueue, m_OffscreenFB1ColorAttachment0->image, m_HistoryBuffer->image,
						m_SwapChainExtent.width, m_SwapChainExtent.height, commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT);

					// Transition to ready only for gamma correct pass
					m_OffscreenFB1ColorAttachment0->TransitionToLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_GraphicsQueue, commandBuffer);

					m_HistoryBuffer->TransitionToLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, commandBuffer);
				}

			}

			BeginDebugMarkerRegion(commandBuffer, "UI");
			{
				m_UIRenderPass->m_FrameBuffer->frameBuffer = m_SwapChainFramebuffers[m_CurrentSwapChainBufferIndex]->frameBuffer;
				m_UIRenderPass->Begin(commandBuffer, clearValues.data(), (u32)clearValues.size());

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_BlitGraphicsPipeline);

				// Fullscreen blit from offscreen frame buffer onto swap chain
				RenderFullscreenTri(commandBuffer, m_FullscreenBlitMatID, m_BlitGraphicsPipelineLayout, m_FinalFullscreenBlitDescriptorSet);

				{
					BeginDebugMarkerRegion(commandBuffer, "Screen Space Sprites");

					EnqueueScreenSpaceSprites();
					DrawSpriteBatch(m_QueuedSSSprites, commandBuffer);
					m_QueuedSSSprites.clear();
					DrawSpriteBatch(m_QueuedSSArrSprites, commandBuffer);
					m_QueuedSSArrSprites.clear();

					EndDebugMarkerRegion(commandBuffer, "End Screen Space Sprites");
				}

				{
					BeginDebugMarkerRegion(commandBuffer, "Screen Space Text");

					EnqueueScreenSpaceText();
					DrawTextSS(commandBuffer);

					EndDebugMarkerRegion(commandBuffer, "End Screen Space Text");
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
			CreateFrameBuffers();

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
				iter.second.descSet = CreateSpriteDescSet(iter.second.materialID, iter.first, iter.second.textureLayer);
			}

			CreateSSAODescriptorSets();
			CreateSSAOPipelines();

			CreatePostProcessingResources();
			CreateFullscreenBlitResources();
			CreateComputeResources(); // TODO: Try removing

			UpdateDynamicUniformBuffer(m_PostProcessMatID, 0, MAT4_IDENTITY, nullptr);

			// Force font descriptor sets to be regenerated
			for (BitmapFont* font : m_FontsSS)
			{
				font->m_DescriptorSet = VK_NULL_HANDLE;
			}

			for (BitmapFont* font : m_FontsWS)
			{
				font->m_DescriptorSet = VK_NULL_HANDLE;
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
			if (frameBufferAttachmentID == SWAP_CHAIN_COLOR_ATTACHMENT_ID)
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
						vkhpp::PipelineStageFlagBits flagBits = (vkhpp::PipelineStageFlagBits) data[i].stage;
						std::string stageStr = vkhpp::to_string(flagBits);
						PrintError("Checkpoint: %s - %s\n", stageStr.c_str(), (const char*)checkpoint->name);
					}
				}
			}
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
			SetObjectName(device, (u64)swapchain, VK_DEBUG_REPORT_OBJECT_TYPE_SWAPCHAIN_KHR_EXT, name);
		}

		void VulkanRenderer::SetDescriptorSetName(VulkanDevice* device, VkDescriptorSet descSet, const char* name)
		{
			SetObjectName(device, (u64)descSet, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT, name);
		}

		void VulkanRenderer::SetPipelineName(VulkanDevice* device, VkPipeline pipeline, const char* name)
		{
			SetObjectName(device, (u64)pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, name);
		}

		void VulkanRenderer::SetFramebufferName(VulkanDevice* device, VkFramebuffer framebuffer, const char* name)
		{
			SetObjectName(device, (u64)framebuffer, VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT, name);
		}

		void VulkanRenderer::SetRenderPassName(VulkanDevice* device, VkRenderPass renderPass, const char* name)
		{
			SetObjectName(device, (u64)renderPass, VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT, name);
		}

		void VulkanRenderer::SetImageName(VulkanDevice* device, VkImage image, const char* name)
		{
			SetObjectName(device, (u64)image, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, name);
		}

		void VulkanRenderer::SetImageViewName(VulkanDevice* device, VkImageView imageView, const char* name)
		{
			SetObjectName(device, (u64)imageView, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT, name);
		}

		void VulkanRenderer::SetSamplerName(VulkanDevice* device, VkSampler sampler, const char* name)
		{
			SetObjectName(device, (u64)sampler, VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT, name);
		}

		void VulkanRenderer::SetBufferName(VulkanDevice* device, VkBuffer buffer, const char* name)
		{
			SetObjectName(device, (u64)buffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, name);
		}

		void VulkanRenderer::BeginDebugMarkerRegion(VkCommandBuffer cmdBuf, const char* markerName, glm::vec4 color)
		{
			((VulkanRenderer*)g_Renderer)->BeginDebugMarkerRegionInternal(cmdBuf, markerName, color);
		}

		void VulkanRenderer::EndDebugMarkerRegion(VkCommandBuffer cmdBuf, const char* markerName /* = nullptr */)
		{
			((VulkanRenderer*)g_Renderer)->EndDebugMarkerRegionInternal(cmdBuf, markerName);
		}

		void VulkanRenderer::BeginDebugMarkerRegionInternal(VkCommandBuffer cmdBuf, const char* markerName, glm::vec4 color)
		{
			if (m_vkCmdDebugMarkerBegin)
			{
				VkDebugMarkerMarkerInfoEXT markerInfo = {};
				markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
				memcpy(markerInfo.color, &color[0], sizeof(float) * 4);
				markerInfo.pMarkerName = markerName;
				m_vkCmdDebugMarkerBegin(cmdBuf, &markerInfo);
			}

			SetCheckPoint(cmdBuf, markerName);
		}

		void VulkanRenderer::EndDebugMarkerRegionInternal(VkCommandBuffer cmdBuf, const char* markerName /* = nullptr */)
		{
			if (m_vkCmdDebugMarkerEnd)
			{
				m_vkCmdDebugMarkerEnd(cmdBuf);
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
			BaseCamera* cam = g_CameraManager->CurrentCamera();
			glm::mat4 projection = cam->GetProjection();
			glm::mat4 projectionInv; // Calculated below
			glm::mat4 view = cam->GetView();
			glm::mat4 viewInv; // Calculated below
			glm::mat4 viewProjection = cam->GetViewProjection();
			glm::vec4 camPos = glm::vec4(cam->position, 0.0f);
			real exposure = cam->exposure;
			glm::vec2 m_NearFarPlanes(cam->zNear, cam->zFar);

			static DirLightData defaultDirLightData = { VEC3_RIGHT, 0, VEC3_ONE, 0.0f, 0, 0.0f, { 0.0f, 0.0f } };

			DirLightData* dirLightData = &defaultDirLightData;
			if (m_DirectionalLight)
			{
				dirLightData = &m_DirectionalLight->data;
			}

			struct UniformInfo
			{
				UniformInfo(u64 uniform, void* dataStart, u32 copySize) :
					uniform(uniform),
					dataStart(dataStart),
					copySize(copySize)
				{}

				u64 uniform;
				void* dataStart = nullptr;
				u32 copySize;
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

			for (auto& materialPair : m_Materials)
			{
				VulkanMaterial& material = materialPair.second;
				VulkanShader& shader = m_Shaders[material.material.shaderID];
				Uniforms& constantUniforms = shader.shader->constantBufferUniforms;
				UniformBuffer* constantBuffer = material.uniformBufferList.Get(UniformBufferType::STATIC);

				if (constantBuffer == nullptr || constantBuffer->data.data == nullptr || constantBuffer->data.size == 0)
				{
					continue; // There is no constant data to update
				}

				u32 index = 0;
				for (UniformInfo& uniformInfo : uniformInfos)
				{
					if (constantUniforms.HasUniform(uniformInfo.uniform))
					{
						memcpy(constantBuffer->data.data + index, uniformInfo.dataStart, uniformInfo.copySize);
						index += (uniformInfo.copySize / sizeof(real));
					}
				}

				u32 size = constantBuffer->data.size;

#if  DEBUG
				u32 calculatedSize1 = index * 4;
				calculatedSize1 = GetAlignedUBOSize(calculatedSize1);
				assert(calculatedSize1 == size);
#endif

				memcpy(material.uniformBufferList.Get(UniformBufferType::STATIC)->buffer.m_Mapped, constantBuffer->data.data, size);
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
			VulkanMaterial& material = m_Materials.at(materialID);
			VulkanShader& shader = m_Shaders[material.material.shaderID];

			UniformBufferList& uniformBufferList = material.uniformBufferList;
			UniformBuffer* dynamicBuffer = uniformBufferList.Get(UniformBufferType::DYNAMIC);

			if (dynamicBuffer == nullptr || dynamicBuffer->buffer.m_Size == 0)
			{
				return; // There are no dynamic uniforms to update
			}

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
			glm::mat4 postProcessMatrix = GetPostProcessingMatrix();
			ParticleSimData particleSimData = {};

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
						m_SSAOBlurDataDynamic.ssaoTexelOffset = glm::vec2(0.0f, (real)m_SSAOBlurSamplePixelOffset / m_GBufferColorAttachment0->height);
					}
					else
					{
						m_SSAOBlurDataDynamic.ssaoTexelOffset = glm::vec2((real)m_SSAOBlurSamplePixelOffset / m_GBufferColorAttachment0->width, 0.0f);
					}
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_COLOR_MULTIPLIER))
				{
					colorMultiplier = uniformOverrides->colorMultiplier;
				}
				if (uniformOverrides->overridenUniforms.HasUniform(U_PARTICLE_SIM_DATA))
				{
					particleSimData = *uniformOverrides->particleSimData;
				}
			}

			struct UniformInfo
			{
				u64 uniform;
				void* dataStart = nullptr;
				u32 copySize;
			};
			UniformInfo uniformInfos[] = {
				{ U_MODEL, (void*)&model, US_MODEL },
				// view, viewProjInv, viewProjection, projection, camPos, dirLight, pointLights should be updated in constant uniform buffer
				{ U_COLOR_MULTIPLIER, (void*)&colorMultiplier, US_COLOR_MULTIPLIER },
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
				{ U_POST_PROCESS_MAT, (void*)&postProcessMatrix, US_POST_PROCESS_MAT },
				{ U_PARTICLE_SIM_DATA, (void*)&particleSimData, US_PARTICLE_SIM_DATA },
			};

			u32 index = 0;
			const Uniforms& dynamicUniforms = shader.shader->dynamicBufferUniforms;
			for (UniformInfo& uniformInfo : uniformInfos)
			{
				if (dynamicUniforms.HasUniform(uniformInfo.uniform))
				{
					// TODO: Don't store data twice? (in uniformBuffer.dynamicData.data & uniformBuffer.dynamicBuffer.m_Mapped)
					memcpy(&dynamicBuffer->data.data[dynamicOffset + index], uniformInfo.dataStart, uniformInfo.copySize);
					index += uniformInfo.copySize / 4;
				}
			}

			// Aligned offset
			u32 size = dynamicBuffer->data.size;

#if  DEBUG
			u32 calculatedSize1 = index * 4;
			calculatedSize1 = GetAlignedUBOSize(calculatedSize1);
			assert(calculatedSize1 == size);
#endif

			u64 firstIndex = (u64)dynamicBuffer->buffer.m_Mapped;
			u64 dest = firstIndex + dynamicOffset;
			memcpy((void*)(dest), &dynamicBuffer->data.data[dynamicOffset], size);
		}

		void VulkanRenderer::GenerateIrradianceMaps()
		{
			for (u32 i = 0; i < (u32)m_RenderObjects.size(); ++i)
			{
				VulkanRenderObject* renderObject = GetRenderObject(i);
				if (!renderObject)
				{
					continue;
				}

				VulkanMaterial& renderObjectMat = m_Materials.at(renderObject->materialID);

				if (renderObjectMat.material.generateIrradianceSampler)
				{
					GenerateCubemapFromHDR(renderObject, renderObjectMat.material.environmentMapPath);
					GenerateIrradianceSampler(renderObject);
					GeneratePrefilteredCube(renderObject);
				}
			}

			// Generate graphics pipelines with correct render pass set
			for (u32 i = 0; i < (u32)m_RenderObjects.size(); ++i)
			{
				CreateDescriptorSet(i);
				CreateGraphicsPipeline(i, false);
			}
		}

		void VulkanRenderer::OnShaderReloadSuccess()
		{
			AddEditorString("Async shader recompile completed successfully");

			LoadShaders();

			CreateFrameBufferAttachments();
			CreateRenderPasses();
			CreateFrameBuffers();

			for (VulkanShader& shader : m_Shaders)
			{
				shader.renderPass = ResolveRenderPassType(shader.shader->renderPassType, shader.shader->name.c_str());
			}

			for (auto& materialPair : m_Materials)
			{
				CreateUniformBuffers(&materialPair.second);
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
				iter.second.descSet = CreateSpriteDescSet(iter.second.materialID, iter.first, iter.second.textureLayer);
			}

			CreateSSAODescriptorSets();
			CreateSSAOPipelines();

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

		bool VulkanRenderer::DoTextureSelector(const char* label,
			const std::vector<VulkanTexture*>& textures,
			i32* selectedIndex)
		{
			bool bValueChanged = false;

			std::string currentTexName = (*selectedIndex == 0 ? "NONE" : (textures[*selectedIndex - 1]->fileName.c_str()));
			if (ImGui::BeginCombo(label, currentTexName.c_str()))
			{
				for (i32 i = 0; i < (i32)textures.size() + 1; i++)
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
						if (ImGui::Selectable(textures[i - 1]->fileName.c_str(), bTextureSelected))
						{
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
						if (texture)
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
				VulkanTexture* alphaBGTexture = GetLoadedTexture(m_AlphaBGTextureID);
				ImGui::Image((void*)&alphaBGTexture->image, ImVec2(texSize * textureAspectRatio, texSize), uv0, uv1);
			}

			ImGui::SetCursorPos(cursorPos);

			ImGui::Image((void*)&texture->image, ImVec2(texSize * textureAspectRatio, texSize));

			ImGui::EndTooltip();
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

		bool VulkanRenderer::InstanceExtensionSupported(const char* instanceExtensionName)
		{
			u32 instanceExtensionCount;
			vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, nullptr);

			std::vector<VkExtensionProperties> instanceExtensions(instanceExtensionCount);
			vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, instanceExtensions.data());

			for (u32 i = 0; i < instanceExtensionCount; ++i)
			{
				if (strcmp(instanceExtensions[i].extensionName, instanceExtensionName) == 0)
				{
					return true;
				}
			}

			return false;
		}

		VKAPI_ATTR VkBool32 VKAPI_CALL VulkanRenderer::DebugCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType,
			u64 obj, size_t location, i32 code, const char* layerPrefix, const char* msg, void* userData)
		{
			FLEX_UNUSED(objType);
			FLEX_UNUSED(obj);
			FLEX_UNUSED(location);
			FLEX_UNUSED(code);
			FLEX_UNUSED(layerPrefix);
			FLEX_UNUSED(userData);

			std::string msgStr = Replace(msg, " | ", "\n\t");

			if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
			{
				PrintError("%s\n", msgStr.c_str());
			}
			else if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
			{
				PrintWarn("%s\n", msgStr.c_str());
			}
			else
			{
				Print("%s\n", msgStr.c_str());
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