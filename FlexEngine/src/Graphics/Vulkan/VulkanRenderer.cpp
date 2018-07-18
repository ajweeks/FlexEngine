#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanRenderer.hpp"

#include <algorithm>
#include <set>
#include <unordered_map>
#include <functional>

#pragma warning(push, 0) // Don't generate warnings for third party code
#include "stb_image.h"

#include <glm/gtc/matrix_transform.hpp>

#include "imgui.h"
#include "ImGui/imgui_impl_glfw_vulkan.h"

#include "BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h"
#pragma warning(pop)

#include "Cameras/CameraManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "FlexEngine.hpp"
#include "Helpers.hpp"
#include "VertexAttribute.hpp"
#include "VertexBufferData.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/BaseScene.hpp"
#include "Graphics/Vulkan/VulkanPhysicsDebugDraw.hpp"
#include "Physics/PhysicsWorld.hpp"

namespace flex
{
	namespace vk
	{
		std::array<glm::mat4, 6> VulkanRenderer::m_CaptureViews;

		VulkanRenderer::VulkanRenderer()
		{
			m_ClearColor = { 1.0f, 0.0f, 1.0f, 1.0f };
			m_BRDFSize = { 512, 512 };
			m_CubemapFramebufferSize = { 512, 512 };

			m_Materials.reserve(MAT_CAPACITY);

			CreateInstance();
			SetupDebugCallback();
			CreateSurface(g_Window);
			VkPhysicalDevice physicalDevice = PickPhysicalDevice();
			CreateLogicalDevice(physicalDevice);

			m_CommandBufferManager = VulkanCommandBufferManager(m_VulkanDevice);
			m_SwapChain = { m_VulkanDevice->m_LogicalDevice, vkDestroySwapchainKHR };
			m_DeferredCombineRenderPass = { m_VulkanDevice->m_LogicalDevice, vkDestroyRenderPass };
			m_DescriptorPool = { m_VulkanDevice->m_LogicalDevice, vkDestroyDescriptorPool };
			m_PresentCompleteSemaphore = { m_VulkanDevice->m_LogicalDevice, vkDestroySemaphore };
			m_RenderCompleteSemaphore = { m_VulkanDevice->m_LogicalDevice, vkDestroySemaphore };
			m_ColorSampler = { m_VulkanDevice->m_LogicalDevice, vkDestroySampler };
			m_PipelineCache = { m_VulkanDevice->m_LogicalDevice, vkDestroyPipelineCache };
		}

		VulkanRenderer::~VulkanRenderer()
		{
			// TODO: Is this needed?
			vkDeviceWaitIdle(m_VulkanDevice->m_LogicalDevice);

			ImGui_ImplGlfwVulkan_Shutdown();
			ImGui::DestroyContext();

			//{
			//	auto iter = m_RenderObjects.begin();
			//	while (iter != m_RenderObjects.end())
			//	{
			//		SafeDelete(*iter);
			//		iter = m_RenderObjects.erase(iter);
			//	}
			//	m_RenderObjects.clear();
			//}

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
			m_VertexIndexBufferPairs.clear();

			if (m_SkyBoxMesh)
			{
				DestroyRenderObject(m_SkyBoxMesh->GetRenderID());
				SafeDelete(m_SkyBoxMesh);
			}

			if (m_gBufferCubemapMesh)
			{
				DestroyRenderObject(m_gBufferCubemapMesh->GetRenderID());
				SafeDelete(m_gBufferCubemapMesh);
			}

			DestroyRenderObject(m_GBufferQuadRenderID);

			SafeDelete(m_PhysicsDebugDrawer);

			for (GameObject* obj : m_PersistentObjects)
			{
				if (obj->GetRenderID() != InvalidRenderID)
				{
					DestroyRenderObject(obj->GetRenderID());
				}
				SafeDelete(obj);
			}
			m_PersistentObjects.clear();

			u32 activeRenderObjectCount = 0;
			for (RenderObjectIter iter = m_RenderObjects.begin(); iter != m_RenderObjects.end(); ++iter)
			{
				if (*iter)
				{
					activeRenderObjectCount++;
				}
			}

			if (activeRenderObjectCount)
			{
				PrintError("Not all render objects were destroyed!");

				for (RenderObjectIter iter = m_RenderObjects.begin(); iter != m_RenderObjects.end(); ++iter)
				{
					if (*iter)
					{
						PrintError("Render object " + (*iter)->gameObject->GetName() + " was not destroyed");
						DestroyRenderObject((*iter)->renderID, *iter);
					}
				}
			}
			m_RenderObjects.clear();

			m_Shaders.clear();

			SafeDelete(m_OffScreenFrameBuf);
			vkDestroySemaphore(m_VulkanDevice->m_LogicalDevice, offscreenSemaphore, nullptr);
			
			SafeDelete(m_CubemapFrameBuffer);
			SafeDelete(m_CubemapDepthAttachment);

			SafeDelete(m_DepthAttachment);

			m_gBufferQuadVertexBufferData.Destroy();

			m_PipelineCache.replace();

			m_DescriptorPool.replace();

			m_ColorSampler.replace();

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

			m_CommandBufferManager.DestroyCommandBuffers();

			vkDeviceWaitIdle(m_VulkanDevice->m_LogicalDevice);

			SafeDelete(m_VulkanDevice);

			// TODO: Move to different function
			glfwTerminate();
		}

		void VulkanRenderer::Initialize()
		{
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

			/*VkFormat depthFormat;
			GetSupportedDepthFormat(m_VulkanDevice->m_PhysicalDevice, &depthFormat);
			m_CubemapDepthAttachment = new FrameBufferAttachment(m_VulkanDevice->m_LogicalDevice, depthFormat);*/

			// NOTE: This is different from the GLRenderer's capture views
			m_CaptureViews = {
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  -1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  -1.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f,  0.0f), glm::vec3(0.0f,  0.0f, 1.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
			};

			// TODO: Make variable
			m_CubemapFrameBuffer->width = 512;
			m_CubemapFrameBuffer->height = 512;

			CreateSwapChain(g_Window);
			CreateSwapChainImageViews();
			CreateRenderPass();

			m_CommandBufferManager.CreatePool(m_Surface);
			CreateDepthResources();
			CreateFramebuffers();

			PrepareOffscreenFrameBuffer(g_Window);
			PrepareCubemapFrameBuffer();

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

			m_BlankTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue);
			m_BlankTexture->CreateFromTexture(RESOURCE_LOCATION + "textures/blank.jpg", VK_FORMAT_R8G8B8A8_UNORM, false);

			//CreateInstance();
			//SetupDebugCallback();
			//CreateSurface(g_Window);
			//VkPhysicalDevice physicalDevice = PickPhysicalDevice();
			//CreateLogicalDevice(physicalDevice);

			ImGui::CreateContext();
		}

		void VulkanRenderer::PostInitialize()
		{
			GLFWWindowWrapper* castedWindow = dynamic_cast<GLFWWindowWrapper*>(g_Window);
			if (castedWindow == nullptr)
			{
				PrintError("VulkanRenderer::PostInitialize expected g_Window to be of type GLFWWindowWrapper!");
				return;
			}

			CreateDescriptorPool();

			ShaderID deferredCombineShaderID;
			if (!GetShaderID("deferred_combine", deferredCombineShaderID))
			{
				PrintError("Failed to find deferred_combine shader!");
			}

			assert(m_SkyBoxMesh);
			MaterialID skyboxMaterialID = m_SkyBoxMesh->GetMaterialID();

			// Initialize GBuffer material & mesh
			{
				MaterialCreateInfo gBufferMaterialCreateInfo = {};
				gBufferMaterialCreateInfo.name = "GBuffer material";
				gBufferMaterialCreateInfo.shaderName = "deferred_combine";
				gBufferMaterialCreateInfo.enableIrradianceSampler = true;
				gBufferMaterialCreateInfo.irradianceSamplerMatID = skyboxMaterialID;
				gBufferMaterialCreateInfo.enablePrefilteredMap = true;
				gBufferMaterialCreateInfo.prefilterMapSamplerMatID = skyboxMaterialID;
				gBufferMaterialCreateInfo.enableBRDFLUT = true;
				gBufferMaterialCreateInfo.renderToCubemap = false;
				for (size_t i = 0; i < m_OffScreenFrameBuf->frameBufferAttachments.size(); ++i)
				{
					gBufferMaterialCreateInfo.frameBuffers.push_back({
						m_OffScreenFrameBuf->frameBufferAttachments[i].first, (void*)&m_OffScreenFrameBuf->frameBufferAttachments[i].second.view
					});
				}

				MaterialID gBufferMatID = InitializeMaterial(&gBufferMaterialCreateInfo);

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


				GameObject* gBufferQuadGameObject = new GameObject("GBuffer Quad", SerializableType::NONE);
				m_PersistentObjects.push_back(gBufferQuadGameObject);
				// Don't render the g buffer normally, we'll render it separately
				gBufferQuadGameObject->SetVisible(false);

				RenderObjectCreateInfo gBufferQuadCreateInfo = {};
				gBufferQuadCreateInfo.materialID = gBufferMatID;
				gBufferQuadCreateInfo.gameObject = gBufferQuadGameObject;
				gBufferQuadCreateInfo.vertexBufferData = &m_gBufferQuadVertexBufferData;
				gBufferQuadCreateInfo.enableCulling = false;
				gBufferQuadCreateInfo.visibleInSceneExplorer = false;

				m_gBufferQuadIndices = { 0, 1, 2,  2, 1, 3 };
				gBufferQuadCreateInfo.indices = &m_gBufferQuadIndices;

				m_GBufferQuadRenderID = InitializeRenderObject(&gBufferQuadCreateInfo);

				m_gBufferQuadVertexBufferData.DescribeShaderVariables(this, m_GBufferQuadRenderID);

				VulkanRenderObject* gBufferRenderObject = GetRenderObject(m_GBufferQuadRenderID);
			}

			// Initialize GBuffer cubemap material & mesh
			{
				MaterialCreateInfo gBufferCubemapMaterialCreateInfo = {};
				gBufferCubemapMaterialCreateInfo.name = "GBuffer cubemap material";
				gBufferCubemapMaterialCreateInfo.shaderName = "deferred_combine_cubemap";
				gBufferCubemapMaterialCreateInfo.enableIrradianceSampler = true;
				gBufferCubemapMaterialCreateInfo.irradianceSamplerMatID = skyboxMaterialID;
				gBufferCubemapMaterialCreateInfo.enablePrefilteredMap = true;
				gBufferCubemapMaterialCreateInfo.prefilterMapSamplerMatID = skyboxMaterialID;
				gBufferCubemapMaterialCreateInfo.enableBRDFLUT = true;
				gBufferCubemapMaterialCreateInfo.renderToCubemap = false;
				for (size_t i = 0; i < m_OffScreenFrameBuf->frameBufferAttachments.size(); ++i)
				{
					gBufferCubemapMaterialCreateInfo.frameBuffers.push_back({
						m_OffScreenFrameBuf->frameBufferAttachments[i].first,
						(void*)&m_OffScreenFrameBuf->frameBufferAttachments[i].second.view
					});
				}

				m_CubemapGBufferMaterialID = InitializeMaterial(&gBufferCubemapMaterialCreateInfo);

				// TODO: Find out why a pointer to this variable gets set to null when passing in to LoadPrefabShape
				RenderObjectCreateInfo gBufferCubemapCreateInfoOverrides = {};
				gBufferCubemapCreateInfoOverrides.visibleInSceneExplorer = false;

				m_gBufferCubemapMesh = new MeshComponent(m_CubemapGBufferMaterialID, "GBuffer cubemap");
				if (!m_gBufferCubemapMesh->LoadPrefabShape(MeshComponent::PrefabShape::SKYBOX))
				{
					PrintError("Failed to create GBuffer cubemap mesh prefab!");
				}
				else
				{
					// Don't render the g buffer cubemap normally, we'll render it separately
					m_gBufferCubemapMesh->SetVisible(false);
				}

				RenderID gBufferCubemapRenderID = m_gBufferCubemapMesh->GetRenderID();

				VulkanRenderObject* gBufferCubemapRenderObject = GetRenderObject(gBufferCubemapRenderID);


				assert(m_PhysicsDebugDrawer == nullptr);
				m_PhysicsDebugDrawer = new VulkanPhysicsDebugDraw();
				m_PhysicsDebugDrawer->Initialize();

				btDiscreteDynamicsWorld* world = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
				world->setDebugDrawer(m_PhysicsDebugDrawer);
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

			ImGui_ImplGlfwVulkan_Init_Data initData = {};
			initData.allocator = VK_NULL_HANDLE;
			initData.gpu = m_VulkanDevice->m_PhysicalDevice;
			initData.device = *m_VulkanDevice;
			initData.render_pass = m_DeferredCombineRenderPass;
			initData.pipeline_cache = m_PipelineCache;
			initData.descriptor_pool = m_DescriptorPool;
			initData.check_vk_result = VK_NULL_HANDLE;
			initData.subpass = 1;

			ImGui_ImplGlfwVulkan_Init(castedWindow->GetWindow(), &initData);

			CreateStaticVertexBuffers();
			CreateStaticIndexBuffers();

			m_CommandBufferManager.CreateCommandBuffers(m_SwapChainImages.size());
			CreateSemaphores();

			// Upload ImGui fonts
			VK_CHECK_RESULT(vkResetCommandPool(m_VulkanDevice->m_LogicalDevice, m_VulkanDevice->m_CommandPool, 0));
			VkCommandBufferBeginInfo begin_info = {};
			begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			VK_CHECK_RESULT(vkBeginCommandBuffer(m_CommandBufferManager.m_CommandBuffers[0], &begin_info));

			ImGui_ImplGlfwVulkan_CreateFontsTexture(m_CommandBufferManager.m_CommandBuffers[0]);

			VkSubmitInfo end_info = {};
			end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			end_info.commandBufferCount = 1;
			end_info.pCommandBuffers = &m_CommandBufferManager.m_CommandBuffers[0];
			VK_CHECK_RESULT(vkEndCommandBuffer(m_CommandBufferManager.m_CommandBuffers[0]));
			VK_CHECK_RESULT(vkQueueSubmit(m_GraphicsQueue, 1, &end_info, VK_NULL_HANDLE));

			VK_CHECK_RESULT(vkDeviceWaitIdle(m_VulkanDevice->m_LogicalDevice));


			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				VulkanRenderObject* renderObject = GetRenderObject(i);
				if (!renderObject)
				{
					continue;
				}
				
				VulkanMaterial& renderObjectMat = m_Materials[renderObject->materialID];

				if (renderObjectMat.material.generateReflectionProbeMaps)
				{
					Print("Capturing reflection probe");
					CaptureSceneToCubemap(i);
					GenerateIrradianceSamplerFromCubemap(renderObject->materialID);
					GeneratePrefilteredMapFromCubemap(renderObject->materialID);
					Print("Done");

					// Capture again to use just generated irradiance + prefilter sampler (TODO: Remove soon)
					Print("Capturing reflection probe");
					CaptureSceneToCubemap(i);
					GenerateIrradianceSamplerFromCubemap(renderObject->materialID);
					GeneratePrefilteredMapFromCubemap(renderObject->materialID);
					Print("Done");

					// Display captured cubemap as skybox (GL code)
					//m_LoadedMaterials[m_RenderObjects[cubemapID]->materialID].cubemapSamplerID =
					//	m_LoadedMaterials[m_RenderObjects[renderID]->materialID].cubemapSamplerID;
				}
				else if (renderObjectMat.material.generateIrradianceSampler)
				{
					GenerateCubemapFromHDR(renderObject);
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


			m_PostInitialized = true;
			Print("Ready!\n");
		}

		void VulkanRenderer::GenerateCubemapFromHDR(VulkanRenderObject* renderObject)
		{
			VulkanRenderObject* skyboxRenderObject = GetRenderObject(m_SkyBoxMesh->GetRenderID());
			Material& skyboxMat = m_Materials[renderObject->materialID].material;
			VulkanMaterial& renderObjectMat = m_Materials[renderObject->materialID];

			MaterialCreateInfo equirectangularToCubeMatCreateInfo = {};
			equirectangularToCubeMatCreateInfo.name = "Equirectangular to Cube";
			equirectangularToCubeMatCreateInfo.shaderName = "equirectangular_to_cube";
			equirectangularToCubeMatCreateInfo.enableHDREquirectangularSampler = true;
			equirectangularToCubeMatCreateInfo.generateHDREquirectangularSampler = true;
			// TODO: Make cyclable at runtime
			equirectangularToCubeMatCreateInfo.hdrEquirectangularTexturePath =
				//RESOURCE_LOCATION + "textures/hdri/Arches_E_PineTree/Arches_E_PineTree_3k.hdr";
				RESOURCE_LOCATION + "textures/hdri/Factory_Catwalk/Factory_Catwalk_2k.hdr";
				//RESOURCE_LOCATION + "textures/hdri/Ice_Lake/Ice_Lake_Ref.hdr";
				//RESOURCE_LOCATION + "textures/hdri/Protospace_B/Protospace_B_Ref.hdr";
			MaterialID equirectangularToCubeMatID = InitializeMaterial(&equirectangularToCubeMatCreateInfo);

			const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
			const u32 dim = (u32)renderObjectMat.material.cubemapSamplerSize.x;
			assert(dim <= Renderer::MAX_TEXTURE_DIM);
			
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
				PrintError("Failed to find equirectangular_to_cube shader ID!");
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
				PrintError("Failed to compile vertex shader located at: " + equirectangularToCubeShader.shader.vertexShaderFilePath);
			}

			VDeleter<VkShaderModule> fragShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(equirectangularToCubeShader.shader.fragmentShaderCode, fragShaderModule))
			{
				PrintError("Failed to compile fragment shader located at: " + equirectangularToCubeShader.shader.fragmentShaderFilePath);
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
				PrintError("Attempted to generate cubemap from HDR but vertex buffer has not been generated! (for shader " + skyboxMat.name + ")");
			}
			if (skyboxRenderObject->indexed && 
				vertexIndexBufferPair.indexBuffer->m_Buffer == VK_NULL_HANDLE)
			{
				PrintError("Attempted to generate cubemap from HDR but index buffer has not been generated! (for shader " + skyboxMat.name + ")");
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
						glm::perspective(PI_DIV_TWO, 1.0f, 0.1f, (real)dim) * m_CaptureViews[face];
					vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Material::PushConstantBlock), &skyboxMat.pushConstantBlock);

					vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

					BindDescriptorSet(&m_Shaders[skyboxMat.shaderID], skyboxRenderObject->renderID, cmdBuf, pipelinelayout, descriptorSet);

					VkDeviceSize offsets[1] = { 0 };

					vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vertexIndexBufferPair.vertexBuffer->m_Buffer, offsets);
					if (skyboxRenderObject->indexed)
					{
						vkCmdBindIndexBuffer(cmdBuf, vertexIndexBufferPair.indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
						vkCmdDrawIndexed(cmdBuf, vertexIndexBufferPair.indexCount, 1, 0, 0, 0);
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
			VulkanRenderObject* skyboxRenderObject = GetRenderObject(m_SkyBoxMesh->GetRenderID());
			Material& skyboxMat = m_Materials[skyboxRenderObject->materialID].material;
			VulkanMaterial& renderObjectMat = m_Materials[renderObject->materialID];

			const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
			const u32 dim = (u32)renderObjectMat.material.irradianceSamplerSize.x;
			assert(dim <= Renderer::MAX_TEXTURE_DIM);
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
				PrintError("Failed to find irradiance shader!");
				return;
			}
			VulkanShader& irradianceShader = m_Shaders[irradianceShaderID];

			VDeleter<VkShaderModule> vertShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(irradianceShader.shader.vertexShaderCode, vertShaderModule))
			{
				PrintError("Failed to compile vertex shader located at: " + irradianceShader.shader.vertexShaderFilePath);
			}

			VDeleter<VkShaderModule> fragShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(irradianceShader.shader.fragmentShaderCode, fragShaderModule))
			{
				PrintError("Failed to compile fragment shader located at: " + irradianceShader.shader.fragmentShaderFilePath);
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
						glm::perspective(PI_DIV_TWO, 1.0f, 0.1f, (real)dim) * m_CaptureViews[face];
					vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Material::PushConstantBlock),
						&skyboxMat.pushConstantBlock);

					vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

					BindDescriptorSet(&m_Shaders[skyboxMat.shaderID], skyboxRenderObject->renderID, cmdBuf, pipelinelayout, descriptorSet);

					VkDeviceSize offsets[1] = { 0 };

					const ShaderID shaderID = skyboxMat.shaderID;
					vkCmdBindVertexBuffers(cmdBuf, 0, 1, &m_VertexIndexBufferPairs[shaderID].vertexBuffer->m_Buffer, offsets);
					if (skyboxRenderObject->indexed)
					{
						vkCmdBindIndexBuffer(cmdBuf, m_VertexIndexBufferPairs[shaderID].indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
						vkCmdDrawIndexed(cmdBuf, m_VertexIndexBufferPairs[shaderID].indexCount, 1, 0, 0, 0);
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
			VulkanRenderObject* skyboxRenderObject = GetRenderObject(m_SkyBoxMesh->GetRenderID());
			Material& skyboxMat = m_Materials[skyboxRenderObject->materialID].material;
			VulkanMaterial& renderObjectMat = m_Materials[renderObject->materialID];

			const VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
			const u32 dim = renderObjectMat.material.prefilteredMapSize.x;
			assert(dim <= Renderer::MAX_TEXTURE_DIM);
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
				PrintError("Failed to find prefilter shader!");
			}
			VulkanShader& prefilterShader = m_Shaders[prefilterShaderID];

			VDeleter<VkShaderModule> vertShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(prefilterShader.shader.vertexShaderCode, vertShaderModule))
			{
				PrintError("Failed to compile vertex shader located at: " + prefilterShader.shader.vertexShaderFilePath);
			}

			VDeleter<VkShaderModule> fragShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(prefilterShader.shader.fragmentShaderCode, fragShaderModule))
			{
				PrintError("Failed to compile fragment shader located at: " + prefilterShader.shader.fragmentShaderFilePath);
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
						glm::perspective(PI_DIV_TWO, 1.0f, 0.1f, (real)dim) * m_CaptureViews[face];
					vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Material::PushConstantBlock), &skyboxMat.pushConstantBlock);

					vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

					BindDescriptorSet(&prefilterShader, skyboxRenderObject->renderID, cmdBuf, pipelinelayout, descriptorSet);

					VkDeviceSize offsets[1] = { 0 };

					ShaderID shaderID = skyboxMat.shaderID;
					vkCmdBindVertexBuffers(cmdBuf, 0, 1, &m_VertexIndexBufferPairs[skyboxMat.shaderID].vertexBuffer->m_Buffer, offsets);
					if (skyboxRenderObject->indexed)
					{
						vkCmdBindIndexBuffer(cmdBuf, m_VertexIndexBufferPairs[shaderID].indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
						vkCmdDrawIndexed(cmdBuf, m_VertexIndexBufferPairs[shaderID].indexCount, 1, 0, 0, 0);
					}
					else
					{
						vkCmdDraw(cmdBuf, m_VertexIndexBufferPairs[shaderID].vertexCount, 1, 0, 0);
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
			assert(dim <= Renderer::MAX_TEXTURE_DIM);

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

			ShaderID brdfShaderID;
			if (!GetShaderID("brdf", brdfShaderID))
			{
				PrintError("Failed to find brdf shader!");
			}
			VulkanShader& brdfShader = m_Shaders[brdfShaderID];

			VDeleter<VkShaderModule> vertShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(brdfShader.shader.vertexShaderCode, vertShaderModule))
			{
				PrintError("Failed to compile vertex shader located at: " + brdfShader.shader.vertexShaderFilePath);
			}

			VDeleter<VkShaderModule> fragShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(brdfShader.shader.fragmentShaderCode, fragShaderModule))
			{
				PrintError("Failed to compile fragment shader located at: " + brdfShader.shader.fragmentShaderFilePath);
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

		MaterialID VulkanRenderer::InitializeMaterial(const MaterialCreateInfo* createInfo)
		{
			VulkanMaterial mat = {};
			mat.material = {};

			if (!GetShaderID(createInfo->shaderName, mat.material.shaderID))
			{
				if (createInfo->shaderName.empty())
				{
					PrintError("Material's shader not set! MaterialCreateInfo::shaderName must be filled in");
				}
				else
				{
					PrintError("Material's shader not set! Shader name " + createInfo->shaderName + " not found");
				}

				// TODO: Handle more gracefully (use placeholder shader)
				return InvalidMaterialID;
			}
			VulkanShader& shader = m_Shaders[mat.material.shaderID];

			mat.material.name = createInfo->name;

			mat.material.diffuseTexturePath = createInfo->diffuseTexturePath;
			mat.material.generateDiffuseSampler = createInfo->generateDiffuseSampler;
			mat.material.enableDiffuseSampler = createInfo->enableDiffuseSampler;

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
			
			mat.material.generateReflectionProbeMaps = createInfo->generateReflectionProbeMaps;

			if (shader.shader.needIrradianceSampler)
			{
				mat.irradianceTexture = (createInfo->irradianceSamplerMatID < m_Materials.size() ?
					m_Materials[createInfo->irradianceSamplerMatID].irradianceTexture : nullptr);
			}
			if (shader.shader.needBRDFLUT)
			{
				if (!m_BRDFTexture)
				{
					Print("Generating BRDF LUT");
					m_BRDFTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue);
					m_BRDFTexture->CreateEmpty(VK_FORMAT_R16G16_SFLOAT, m_BRDFSize.x, m_BRDFSize.y, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
					m_LoadedTextures.push_back(m_BRDFTexture);
					GenerateBRDFLUT(m_BRDFTexture);
				}
				mat.brdfLUT = m_BRDFTexture;
			}
			if (shader.shader.needPrefilteredMap)
			{
				mat.prefilterTexture = (createInfo->prefilterMapSamplerMatID < m_Materials.size() ?
					m_Materials[createInfo->prefilterMapSamplerMatID].prefilterTexture : nullptr);
			}

			mat.material.enablePrefilteredMap = createInfo->enablePrefilteredMap;
			mat.material.generatePrefilteredMap = createInfo->generatePrefilteredMap;
			mat.material.prefilteredMapSize = createInfo->generatedPrefilteredCubemapSize;

			mat.material.enableBRDFLUT = createInfo->enableBRDFLUT;
			mat.material.renderToCubemap = createInfo->renderToCubemap;

			mat.descriptorSetLayoutIndex = mat.material.shaderID;

			struct TextureInfo
			{
				TextureInfo(const std::string& filePath, 
					VulkanTexture** texture, 
					bool* generate, 
					VkFormat format = VK_FORMAT_R8G8B8A8_UNORM, 
					u32 mipLevels = 1,
					bool hdr = false) :
					filePath(filePath),
					texture(texture),
					generate(generate),
					format(format),
					mipLevels(mipLevels),
					hdr(hdr)
				{}

				const std::string filePath;
				VulkanTexture** texture = nullptr;
				bool* generate = nullptr;
				VkFormat format;
				u32 mipLevels;
				bool hdr = false;
			};

			TextureInfo textureInfos[] =
			{
				{ createInfo->diffuseTexturePath, &mat.diffuseTexture, &mat.material.generateDiffuseSampler },
				{ createInfo->normalTexturePath, &mat.normalTexture, &mat.material.generateNormalSampler },
				{ createInfo->albedoTexturePath, &mat.albedoTexture, &mat.material.generateAlbedoSampler },
				{ createInfo->metallicTexturePath, &mat.metallicTexture, &mat.material.generateMetallicSampler },
				{ createInfo->roughnessTexturePath, &mat.roughnessTexture, &mat.material.generateRoughnessSampler },
				{ createInfo->aoTexturePath, &mat.aoTexture, &mat.material.generateAOSampler },
				{ createInfo->hdrEquirectangularTexturePath, &mat.hdrEquirectangularTexture, &mat.material.generateHDREquirectangularSampler, VK_FORMAT_R32G32B32A32_SFLOAT, 1, true },
			};
			const size_t textureCount = sizeof(textureInfos) / sizeof(textureInfos[0]);

			// Calculate how many textures need to be allocated to prevent texture vector from resizing
			const size_t usedTextureCount = createInfo->generateAlbedoSampler + createInfo->generateAOSampler + createInfo->generateCubemapSampler + createInfo->generateDiffuseSampler + createInfo->generateIrradianceSampler + createInfo->generateMetallicSampler + createInfo->generateNormalSampler + createInfo->generatePrefilteredMap + createInfo->generateRoughnessSampler + createInfo->generateHDREquirectangularSampler; 
			m_LoadedTextures.reserve(usedTextureCount);

			for (TextureInfo& textureInfo : textureInfos)
			{
				if (!textureInfo.filePath.empty() && textureInfo.generate)
				{
					*textureInfo.texture = GetLoadedTexture(textureInfo.filePath);

					if (*textureInfo.texture == nullptr)
					{
						*textureInfo.texture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue);
						// Texture hasn't been loaded yet, load it now
						(*textureInfo.texture)->CreateFromTexture(textureInfo.filePath, textureInfo.format, textureInfo.hdr, textureInfo.mipLevels);
						m_LoadedTextures.push_back(*textureInfo.texture);

						(*textureInfo.texture)->imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
						(*textureInfo.texture)->UpdateImageDescriptor();
					}
				}
			}

			if (createInfo->generateReflectionProbeMaps)
			{
				mat.cubemapSamplerGBuffersIDs = {
					{ 0, "positionMetallicFrameBufferSampler", VK_FORMAT_R16G16B16_SFLOAT },
					{ 0, "normalRoughnessFrameBufferSampler", VK_FORMAT_R16G16B16_SFLOAT },
					{ 0, "albedoAOFrameBufferSampler", VK_FORMAT_R16G16B16_SFLOAT },

					//{ 0, "positionMetallicFrameBufferSampler", GL_RGBA16F, GL_RGBA },
					//{ 0, "normalRoughnessFrameBufferSampler", GL_RGBA16F, GL_RGBA },
					//{ 0, "albedoAOFrameBufferSampler", GL_RGBA, GL_RGBA },
				};

				//GLCubemapCreateInfo cubemapCreateInfo = {};
				//cubemapCreateInfo.program = shader.program;
				//cubemapCreateInfo.textureID = &mat.cubemapSamplerID;
				//cubemapCreateInfo.textureGBufferIDs = &mat.cubemapSamplerGBuffersIDs;
				//cubemapCreateInfo.depthTextureID = &mat.cubemapDepthSamplerID;
				//cubemapCreateInfo.HDR = true;
				//cubemapCreateInfo.enableTrilinearFiltering = createInfo->enableCubemapTrilinearFiltering;
				//cubemapCreateInfo.generateMipmaps = false;
				//cubemapCreateInfo.textureSize = createInfo->generatedCubemapSize;
				//cubemapCreateInfo.generateDepthBuffers = createInfo->generateCubemapDepthBuffers;

				//GenerateVulkanCubemap(cubemapCreateInfo);
			}

			// Cubemaps are treated differently than regular textures because they require 6 filepaths
			if (mat.material.generateCubemapSampler)
			{
				if (createInfo->cubeMapFilePaths[0].empty())
				{
					assert(mat.cubemapTexture == nullptr);

					const u32 mipLevels = static_cast<u32>(floor(log2(createInfo->generatedCubemapSize.x))) + 1;
					mat.cubemapTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue);
					mat.cubemapTexture->CreateCubemapEmpty(VK_FORMAT_R8G8B8A8_UNORM, createInfo->generatedCubemapSize.x, createInfo->generatedCubemapSize.y, 4, mipLevels, createInfo->enableCubemapTrilinearFiltering);
					m_LoadedTextures.push_back(mat.cubemapTexture);
				}
				else
				{
					mat.cubemapTexture = GetLoadedTexture(createInfo->cubeMapFilePaths[0]);

					if (mat.cubemapTexture == nullptr)
					{
						mat.cubemapTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue);
						mat.cubemapTexture->CreateCubemapFromTextures(VK_FORMAT_R8G8B8A8_UNORM, createInfo->cubeMapFilePaths, true);
						m_LoadedTextures.push_back(mat.cubemapTexture);
					}
				}
			}

			if (mat.material.generateHDRCubemapSampler)
			{
				assert(mat.cubemapTexture == nullptr);

				const u32 mipLevels = static_cast<u32>(floor(log2(createInfo->generatedCubemapSize.x))) + 1;
				mat.cubemapTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue);
				mat.cubemapTexture->CreateCubemapEmpty(VK_FORMAT_R32G32B32A32_SFLOAT, createInfo->generatedCubemapSize.x, createInfo->generatedCubemapSize.y, 4, mipLevels, false);
				m_LoadedTextures.push_back(mat.cubemapTexture);
			}

			if (shader.shader.needCubemapSampler)
			{
				
			}


			if (shader.shader.needBRDFLUT)
			{
				
			}

			if (mat.material.generateIrradianceSampler)
			{
				assert(mat.irradianceTexture == nullptr);

				const u32 mipLevels = static_cast<u32>(floor(log2(createInfo->generatedIrradianceCubemapSize.x))) + 1;
				mat.irradianceTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue);
				mat.irradianceTexture->CreateCubemapEmpty(VK_FORMAT_R32G32B32A32_SFLOAT, createInfo->generatedIrradianceCubemapSize.x, createInfo->generatedIrradianceCubemapSize.y, 4, mipLevels, false);
				m_LoadedTextures.push_back(mat.irradianceTexture);
			}

			if (shader.shader.needIrradianceSampler)
			{
				
			}

			if (mat.material.generatePrefilteredMap)
			{
				assert(mat.prefilterTexture == nullptr);

				const u32 mipLevels = static_cast<u32>(floor(log2(createInfo->generatedPrefilteredCubemapSize.x))) + 1;
				mat.prefilterTexture = new VulkanTexture(m_VulkanDevice, m_GraphicsQueue);
				mat.prefilterTexture->CreateCubemapEmpty(VK_FORMAT_R16G16B16A16_SFLOAT, createInfo->generatedPrefilteredCubemapSize.x, createInfo->generatedPrefilteredCubemapSize.y, 4, mipLevels, true);
				m_LoadedTextures.push_back(mat.prefilterTexture);
			}

			if (shader.shader.needPrefilteredMap)
			{
				
			}

			size_t prevMatCapacity = m_Materials.capacity();
			m_Materials.push_back(mat);
			size_t newMatCapacity = m_Materials.capacity();

			if (prevMatCapacity != newMatCapacity)
			{
				PrintError("VulkanRenderer::m_LoadedMaterials was reallocated! Local references will become invalid! New high water line: " + std::to_string(newMatCapacity) + " (previous was " + std::to_string(prevMatCapacity) + ")");
			}


			return m_Materials.size() - 1;
		}

		u32 VulkanRenderer::InitializeRenderObject(const RenderObjectCreateInfo* createInfo)
		{
			RenderID renderID = GetFirstAvailableRenderID();
			VulkanRenderObject* renderObject = new VulkanRenderObject(m_VulkanDevice->m_LogicalDevice, renderID);

			InsertNewRenderObject(renderObject);
			renderObject->materialID = createInfo->materialID;

			if (renderObject->materialID == InvalidMaterialID)
			{
				if (m_Materials.empty())
				{
					PrintError("Render object created before any materials have been created! Returning...");
					return InvalidRenderID;
				}
				else
				{
					PrintError("Render object doesn't have its material ID set! Using first available material");
					renderObject->materialID = 0;
				}
			}

			renderObject->vertexBufferData = createInfo->vertexBufferData;
			renderObject->cullMode = CullFaceToVkCullMode(createInfo->cullFace);
			renderObject->enableCulling = createInfo->enableCulling;
			renderObject->materialName = m_Materials[renderObject->materialID].material.name;
			renderObject->gameObject = createInfo->gameObject;

			if (createInfo->indices != nullptr)
			{
				renderObject->indices = createInfo->indices;
				renderObject->indexed = true;
			}

			// We've already been post initialized, so we need to create a descriptor set and pipeline here
			if (m_PostInitialized)
			{
				CreateDescriptorSet(renderID);
				CreateGraphicsPipeline(renderID, true);
			}

			return renderID;
		}

		void VulkanRenderer::CreateUniformBuffers(VulkanShader* shader)
		{
			shader->uniformBuffer.constantData.size = shader->shader.constantBufferUniforms.CalculateSize(m_PointLights.size());
			if (shader->uniformBuffer.constantData.size > 0)
			{
				free(shader->uniformBuffer.constantData.data);
				
				shader->uniformBuffer.constantData.data = (real*)malloc(shader->uniformBuffer.constantData.size);
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
			if (!renderObject)
			{
				return;
			}

			VkPrimitiveTopology vkTopology = TopologyModeToVkPrimitiveTopology(topology);

			if (vkTopology == VK_PRIMITIVE_TOPOLOGY_MAX_ENUM)
			{
				PrintError("Unsupported TopologyMode passed to VulkanRenderer::SetTopologyMode: " + (i32)topology);
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

			if (g_InputManager->GetKeyDown(InputManager::KeyCode::KEY_U))
			{
				for (auto iter = m_RenderObjects.begin(); iter != m_RenderObjects.end(); ++iter)
				{
					VulkanRenderObject* renderObject = *iter;
					if (renderObject && m_Materials[renderObject->materialID].material.generateReflectionProbeMaps)
					{
						Print("Capturing reflection probe");
						CaptureSceneToCubemap(renderObject->renderID);
						GenerateIrradianceSamplerFromCubemap(renderObject->materialID);
						GeneratePrefilteredMapFromCubemap(renderObject->materialID);
						Print("Done");
					}
				}
			}

			// Update uniform buffer
			UpdateConstantUniformBuffers();

			// TODO: Only update when things have changed
			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				UpdateDynamicUniformBuffer(i);
			}

			// Update g-buffer uniforms
			UpdateDynamicUniformBuffer(m_GBufferQuadRenderID);
		}

		void VulkanRenderer::Draw()
		{
			DrawCallInfo drawCallInfo = {};

			if (!m_PhysicsDebuggingSettings.DisableAll)
			{
				PhysicsDebugRender();
			}

			BuildDeferredCommandBuffer(drawCallInfo); // TODO: Only call this when objects change
			BuildCommandBuffers(drawCallInfo); // TODO: Only call this when objects change

			if (m_SwapChainNeedsRebuilding)
			{
				m_SwapChainNeedsRebuilding = false;
				RecreateSwapChain(g_Window);
			}
			else
			{
				DrawFrame(g_Window);
			}
		}

		void VulkanRenderer::DrawImGuiItems()
		{
			if (ImGui::CollapsingHeader("Scene info"))
			{
				if (ImGui::TreeNode("Render Objects"))
				{
					std::vector<GameObject*>& rootObjects = g_SceneManager->CurrentScene()->GetRootObjects();
					for (size_t i = 0; i < rootObjects.size(); ++i)
					{
						DrawImGuiForRenderObjectAndChildren(rootObjects[i]);
					}

					ImGui::TreePop();
				}

				DrawImGuiLights();
			}
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
				if (renderObject)
				{
					const std::string renderIDStr = "renderID: " + std::to_string(renderObject->renderID);
					ImGui::TextUnformatted(renderIDStr.c_str());
				}

				DrawImGuiForRenderObjectCommon(gameObject);

				if (renderObject)
				{
					VulkanMaterial& material = m_Materials[renderObject->materialID];
					VulkanShader& shader = m_Shaders[material.material.shaderID];

					std::string matNameStr = "Material: " + material.material.name;
					std::string shaderNameStr = "Shader: " + shader.shader.name;
					ImGui::TextUnformatted(matNameStr.c_str());
					ImGui::TextUnformatted(shaderNameStr.c_str());

					if (shader.shader.needIrradianceSampler)
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

		void VulkanRenderer::ReloadShaders()
		{
			// TODO: Implement
		}

		void VulkanRenderer::OnWindowSizeChanged(i32 width, i32 height)
		{
			UNREFERENCED_PARAMETER(width);
			UNREFERENCED_PARAMETER(height);

			m_SwapChainNeedsRebuilding = true;
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
			outInfo.enableCulling = renderObject->enableCulling;
			//outInfo.depthTestReadFunc =  // TODO: ?
			//outInfo.depthWriteEnable = // TODO: ?

			return true;
		}

		void VulkanRenderer::SetVSyncEnabled(bool enableVSync)
		{
			m_VSyncEnabled = enableVSync;
		}

		bool VulkanRenderer::GetVSyncEnabled()
		{
			return m_VSyncEnabled;
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

		void VulkanRenderer::SetSkyboxMesh(MeshComponent* skyboxMesh)
		{
			m_SkyBoxMesh = skyboxMesh;

			if (skyboxMesh == nullptr)
			{
				return;
			}

			MaterialID skyboxMatierialID = m_SkyBoxMesh->GetMaterialID();
			if (skyboxMatierialID == InvalidMaterialID)
			{
				PrintError("Skybox doesn't have a valid material! Irradiance textures can't be generated");
				return;
			}

			for (u32 i = 0; i < m_RenderObjects.size(); ++i)
			{
				VulkanRenderObject* renderObject = GetRenderObject(i);
				if (renderObject &&
					m_Shaders[m_Materials[renderObject->materialID].material.shaderID].shader.needPrefilteredMap)
				{
					VulkanMaterial* mat = &m_Materials[renderObject->materialID];
					mat->irradianceTexture = m_Materials[skyboxMatierialID].irradianceTexture;
					mat->prefilterTexture = m_Materials[skyboxMatierialID].prefilterTexture;
				}
			}
		}

		MeshComponent* VulkanRenderer::GetSkyboxMesh()
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
				PrintError("SetRenderObjectMaterialID couldn't find render object with ID " + std::to_string(renderID));
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
			for (auto iter = m_RenderObjects.begin(); iter != m_RenderObjects.end(); ++iter)
			{
				if (*iter && (*iter)->renderID == renderID)
				{
					DestroyRenderObject(renderID, *iter);
					return;
				}
			}
		}

		void VulkanRenderer::DestroyRenderObject(RenderID renderID, VulkanRenderObject* renderObject)
		{
			if (renderObject)
			{
				vkFreeDescriptorSets(m_VulkanDevice->m_LogicalDevice, m_DescriptorPool, 1, &(renderObject->descriptorSet));
				SafeDelete(renderObject);
			}
			m_RenderObjects[renderID] = nullptr;
		}

		void VulkanRenderer::NewFrame()
		{
			if (m_PhysicsDebugDrawer)
			{
				m_PhysicsDebugDrawer->ClearLines();
			}
			ImGui_ImplGlfwVulkan_NewFrame();
		}

		btIDebugDraw* VulkanRenderer::GetDebugDrawer()
		{
			return m_PhysicsDebugDrawer;
		}

		void VulkanRenderer::PostInitializeRenderObject(RenderID renderID)
		{
			UNREFERENCED_PARAMETER(renderID);
		}

		void VulkanRenderer::ClearRenderObjects()
		{
			for (VulkanRenderObject* renderObject : m_RenderObjects)
			{
				if (renderObject)
				{
					DestroyRenderObject(renderObject->renderID, renderObject);
				}
			}
			m_RenderObjects.clear();
		}

		void VulkanRenderer::ClearMaterials()
		{
			m_Materials.clear();
		}

		VulkanRenderObject* VulkanRenderer::GetRenderObject(RenderID renderID)
		{
#if _DEBUG
			if (renderID > m_RenderObjects.size() ||
				renderID == InvalidRenderID)
			{
				PrintError("Invalid renderID passed to GetRenderObject: " + std::to_string(renderID));
				return nullptr;
			}
#endif 

			return m_RenderObjects[renderID];
		}

		void VulkanRenderer::UpdateRenderObjectVertexData(RenderID renderID)
		{
			//RenderObject renderObject = GetRenderObject(renderID);
			
			//CreateDescriptorSet(renderID);
			CreateGraphicsPipeline(renderID, false);
		}
		
		RenderID VulkanRenderer::GetFirstAvailableRenderID() const
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
			if (m_EnableValidationLayers && !CheckValidationLayerSupport())
			{
				throw std::runtime_error("validation layers requested, but not available!");
			}

			VkApplicationInfo appInfo = {};
			appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
			std::string applicationName = g_Window->GetTitle();
			appInfo.pApplicationName = applicationName.c_str();
			appInfo.applicationVersion = VK_API_VERSION_1_0;
			appInfo.pEngineName = "Flex Engine";
			appInfo.engineVersion = VK_MAKE_VERSION(FlexEngine::EngineVersionMajor, FlexEngine::EngineVersionMinor, FlexEngine::EngineVersionPatch);
			appInfo.apiVersion = VK_API_VERSION_1_0;

			Print("Vulkan Version: 1.1.70.0");

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
			if (!m_EnableValidationLayers)
			{
				return;
			}

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

			u32 deviceCount = 0;
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

			vkGetDeviceQueue(m_VulkanDevice->m_LogicalDevice, (u32)indices.graphicsFamily, 0, &m_GraphicsQueue);
			vkGetDeviceQueue(m_VulkanDevice->m_LogicalDevice, (u32)indices.presentFamily, 0, &m_PresentQueue);
		}

		void VulkanRenderer::RecreateSwapChain(Window* window)
		{
			vkDeviceWaitIdle(m_VulkanDevice->m_LogicalDevice);

			CreateSwapChain(window);
			CreateSwapChainImageViews();

			CreateDepthResources();

			PrepareOffscreenFrameBuffer(window);
			CreateRenderPass();

			for (u32 i = 0; i < m_RenderObjects.size(); ++i)
			{
				CreateDescriptorSet(i);
				CreateGraphicsPipeline(i, false);
			}

			CreateFramebuffers();
			m_CommandBufferManager.CreateCommandBuffers(m_SwapChainImages.size());
		}

		void VulkanRenderer::CreateSwapChain(Window* window)
		{
			VulkanSwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(m_VulkanDevice->m_PhysicalDevice);

			VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
			VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
			VkExtent2D extent = ChooseSwapExtent(window, swapChainSupport.capabilities);

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
			createInfo.diffuseTexture = material->diffuseTexture;
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
				DescriptorSetInfo(const std::string& uniformName, VkDescriptorType descriptorType, VkBuffer buffer, VkDeviceSize bufferSize, VkImageView imageView = VK_NULL_HANDLE, VkSampler imageSampler = VK_NULL_HANDLE, VkDescriptorImageInfo* imageInfoPtr = nullptr) :
					uniformName(uniformName),
					descriptorType(descriptorType),
					buffer(buffer),
					bufferSize(bufferSize),
					imageView(imageView),
					imageSampler(imageSampler),
					imageInfoPtr(imageInfoPtr)
				{

				}

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
				createInfo->albedoTexture ? *&createInfo->albedoTexture->imageView : VK_NULL_HANDLE,
				createInfo->albedoTexture ? *&createInfo->albedoTexture->sampler : VK_NULL_HANDLE,
				createInfo->albedoTexture ? &createInfo->albedoTexture->imageInfoDescriptor : nullptr },

				{ "metallicSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->metallicTexture ? *&createInfo->metallicTexture->imageView : VK_NULL_HANDLE,
				createInfo->metallicTexture ? *&createInfo->metallicTexture->sampler : VK_NULL_HANDLE,
				createInfo->metallicTexture ? &createInfo->metallicTexture->imageInfoDescriptor : nullptr },

				{ "roughnessSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->roughnessTexture ? *&createInfo->roughnessTexture->imageView : VK_NULL_HANDLE,
				createInfo->roughnessTexture ? *&createInfo->roughnessTexture->sampler : VK_NULL_HANDLE,
				createInfo->roughnessTexture ? &createInfo->roughnessTexture->imageInfoDescriptor : nullptr },

				{ "aoSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->aoTexture ? *&createInfo->aoTexture->imageView : VK_NULL_HANDLE,
				createInfo->aoTexture ? *&createInfo->aoTexture->sampler : VK_NULL_HANDLE,
				createInfo->aoTexture ? &createInfo->aoTexture->imageInfoDescriptor : nullptr },

				{ "diffuseSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->diffuseTexture ? *&createInfo->diffuseTexture->imageView : VK_NULL_HANDLE,
				createInfo->diffuseTexture ? *&createInfo->diffuseTexture->sampler : VK_NULL_HANDLE,
				createInfo->diffuseTexture ? &createInfo->diffuseTexture->imageInfoDescriptor : nullptr },

				{ "normalSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->normalTexture ? *&createInfo->normalTexture->imageView : VK_NULL_HANDLE,
				createInfo->normalTexture ? *&createInfo->normalTexture->sampler : VK_NULL_HANDLE,
				createInfo->normalTexture ? &createInfo->normalTexture->imageInfoDescriptor : nullptr },

				{ "hdrEquirectangularSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->hdrEquirectangularTexture ? *&createInfo->hdrEquirectangularTexture->imageView : VK_NULL_HANDLE,
				createInfo->hdrEquirectangularTexture ? *&createInfo->hdrEquirectangularTexture->sampler : VK_NULL_HANDLE,
				createInfo->hdrEquirectangularTexture ? &createInfo->hdrEquirectangularTexture->imageInfoDescriptor : nullptr },

				{ "cubemapSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->cubemapTexture ? *&createInfo->cubemapTexture->imageView : VK_NULL_HANDLE,
				createInfo->cubemapTexture ? *&createInfo->cubemapTexture->sampler : VK_NULL_HANDLE,
				createInfo->cubemapTexture ? &createInfo->cubemapTexture->imageInfoDescriptor : nullptr },

				{ "brdfLUT", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->brdfLUT ? *&createInfo->brdfLUT->imageView : VK_NULL_HANDLE,
				createInfo->brdfLUT ? *&createInfo->brdfLUT->sampler : VK_NULL_HANDLE,
				createInfo->brdfLUT ? &createInfo->brdfLUT->imageInfoDescriptor : nullptr },

				{ "irradianceSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->irradianceTexture ? *&createInfo->irradianceTexture->imageView : VK_NULL_HANDLE,
				createInfo->irradianceTexture ? *&createInfo->irradianceTexture->sampler : VK_NULL_HANDLE,
				createInfo->irradianceTexture ? &createInfo->irradianceTexture->imageInfoDescriptor : nullptr },

				{ "prefilterMap", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->prefilterTexture ? *&createInfo->prefilterTexture->imageView : VK_NULL_HANDLE,
				createInfo->prefilterTexture ? *&createInfo->prefilterTexture->sampler : VK_NULL_HANDLE,
				createInfo->prefilterTexture ? &createInfo->prefilterTexture->imageInfoDescriptor : nullptr },
			};


			for (auto& frameBufferViewPair : createInfo->frameBufferViews)
			{
				descriptorSets.push_back({
					frameBufferViewPair.first, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					VK_NULL_HANDLE, 0,
					frameBufferViewPair.second ? *frameBufferViewPair.second : VK_NULL_HANDLE,
					m_ColorSampler
				});
			}

			std::vector<VkWriteDescriptorSet> writeDescriptorSets;
			writeDescriptorSets.reserve(descriptorSets.size());

			u32 descriptorSetIndex = 0;
			u32 binding = 0;

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
			u32 binding = 0;

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

			return false;
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
				if (!filePath.empty() && filePath.compare(vulkanTexture->filePath) == 0)
				{
					return vulkanTexture;
				}
			}

			return nullptr;
		}

		void VulkanRenderer::CreateGraphicsPipeline(RenderID renderID, bool setCubemapRenderPass)
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
			pipelineCreateInfo.vertexAttributes = renderObject->vertexBufferData->Attributes;
			pipelineCreateInfo.topology = renderObject->topology;
			pipelineCreateInfo.cullMode = renderObject->cullMode;
			pipelineCreateInfo.enableCulling = renderObject->enableCulling;
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
			else if (setCubemapRenderPass)
			{
				//ShaderID cubemapGBufferShaderID = m_LoadedMaterials[m_CubemapGBufferMaterialID].material.shaderID;
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
				PrintError("Failed to compile vertex shader located at: " + shader.shader.vertexShaderFilePath);
			}

			VDeleter<VkShaderModule> fragShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(shader.shader.fragmentShaderCode, fragShaderModule))
			{
				PrintError("Failed to compile fragment shader located at: " + shader.shader.fragmentShaderFilePath);
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
			rasterizer.cullMode = createInfo->enableCulling ? createInfo->cullMode : VK_CULL_MODE_NONE;
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

		void VulkanRenderer::PrepareOffscreenFrameBuffer(Window* window)
		{
			// TODO: This should be setting up the m_SwapChainFrameBuffers?

			const glm::vec2i frameBufferSize = window->GetFrameBufferSize();
			m_OffScreenFrameBuf->width = frameBufferSize.x;
			m_OffScreenFrameBuf->height = frameBufferSize.y;

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

		void VulkanRenderer::PhysicsDebugRender()
		{
			btDiscreteDynamicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
			physicsWorld->debugDrawWorld();
		}

		void VulkanRenderer::BuildCommandBuffers(const DrawCallInfo& drawCallInfo)
		{
			if (drawCallInfo.renderToCubemap)
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


				// This needed?
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

					// Only render non-deferred (forward) objects in this pass
					if (m_Shaders[renderObjectMat.material.shaderID].shader.deferred)
					{
						continue;
					}

					VkDeviceSize offsets[1] = { 0 };
					vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_VertexIndexBufferPairs[renderObjectMat.material.shaderID].vertexBuffer->m_Buffer, offsets);

					if (m_VertexIndexBufferPairs[renderObjectMat.material.shaderID].indexBuffer->m_Size != 0)
					{
						vkCmdBindIndexBuffer(commandBuffer, m_VertexIndexBufferPairs[renderObjectMat.material.shaderID].indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
					}

					vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderObject->graphicsPipeline);

					// Push constants
					if (m_Shaders[renderObjectMat.material.shaderID].shader.needPushConstantBlock)
					{
						// Truncate translation component to center cubemap around viewer
						glm::mat4 view = glm::mat4(glm::mat3(g_CameraManager->CurrentCamera()->GetView()));
						glm::mat4 projection = g_CameraManager->CurrentCamera()->GetProjection();
						renderObjectMat.material.pushConstantBlock.mvp =
							projection * view * renderObject->gameObject->GetTransform()->GetModelMatrix();
						vkCmdPushConstants(commandBuffer, renderObject->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Material::PushConstantBlock), &renderObjectMat.material.pushConstantBlock);
					}

					BindDescriptorSet(&m_Shaders[renderObjectMat.material.shaderID], renderObject->renderID, commandBuffer, renderObject->pipelineLayout, renderObject->descriptorSet);

					if (renderObject->indexed)
					{
						vkCmdDrawIndexed(commandBuffer, m_VertexIndexBufferPairs[renderObjectMat.material.shaderID].indexCount, 1, renderObject->indexOffset, 0, 0);
					}
					else
					{
						vkCmdDraw(commandBuffer, renderObject->vertexBufferData->VertexCount, 1, renderObject->vertexOffset, 0);
					}
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

				for (size_t i = 0; i < m_CommandBufferManager.m_CommandBuffers.size(); ++i)
				{
					ImGui_ImplGlfwVulkan_Render();

					VkCommandBuffer& commandBuffer = m_CommandBufferManager.m_CommandBuffers[i];

					renderPassBeginInfo.framebuffer = m_SwapChainFramebuffers[i];

					VkCommandBufferBeginInfo cmdBufferbeginInfo = {};
					cmdBufferbeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
					cmdBufferbeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

					VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &cmdBufferbeginInfo));

					vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

					VkViewport viewport = VkViewport{ 0.0f, 1.0f, (real)m_SwapChainExtent.width, (real)m_SwapChainExtent.height, 0.1f, 1000.0f };
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
						if (!renderObject || 
							!renderObject->gameObject->IsVisible() ||
							!renderObject->vertexBufferData ||
							renderObject->vertexBufferData->VertexCount == 0)
						{
							continue;
						}

						VulkanMaterial& renderObjectMat = m_Materials[renderObject->materialID];

						// Only render non-deferred (forward) objects in this pass
						if (m_Shaders[renderObjectMat.material.shaderID].shader.deferred)
						{
							continue;
						}

						vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_VertexIndexBufferPairs[renderObjectMat.material.shaderID].vertexBuffer->m_Buffer, offsets);

						if (m_VertexIndexBufferPairs[renderObjectMat.material.shaderID].indexBuffer->m_Size != 0)
						{
							vkCmdBindIndexBuffer(commandBuffer, m_VertexIndexBufferPairs[renderObjectMat.material.shaderID].indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
						}

						vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderObject->graphicsPipeline);

						// Push constants
						if (m_Shaders[renderObjectMat.material.shaderID].shader.needPushConstantBlock)
						{
							glm::mat4 view = glm::mat4(glm::mat3(g_CameraManager->CurrentCamera()->GetView())); // Truncate translation part off to center around viewer
							glm::mat4 projection = g_CameraManager->CurrentCamera()->GetProjection();
							renderObjectMat.material.pushConstantBlock.mvp =
								projection * view * renderObject->gameObject->GetTransform()->GetModelMatrix();
							vkCmdPushConstants(commandBuffer, renderObject->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Material::PushConstantBlock), &renderObjectMat.material.pushConstantBlock);
						}

						BindDescriptorSet(&m_Shaders[renderObjectMat.material.shaderID], renderObject->renderID, commandBuffer, renderObject->pipelineLayout, renderObject->descriptorSet);

						if (renderObject->indexed)
						{
							vkCmdDrawIndexed(commandBuffer, m_VertexIndexBufferPairs[renderObjectMat.material.shaderID].indexCount, 1, renderObject->indexOffset, 0, 0);
						}
						else
						{
							vkCmdDraw(commandBuffer, renderObject->vertexBufferData->VertexCount, 1, renderObject->vertexOffset, 0);
						}
					}

					ImGui_ImplGlfwVulkan_DrawFrame(commandBuffer);

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

				// Only render deferred objects in this pass
				if (!m_Shaders[renderObjectMat.material.shaderID].shader.deferred) continue;

				vkCmdBindVertexBuffers(offScreenCmdBuffer, 0, 1, &m_VertexIndexBufferPairs[renderObjectMat.material.shaderID].vertexBuffer->m_Buffer, offsets);

				if (m_VertexIndexBufferPairs[renderObjectMat.material.shaderID].indexBuffer->m_Size != 0)
				{
					vkCmdBindIndexBuffer(offScreenCmdBuffer, m_VertexIndexBufferPairs[renderObjectMat.material.shaderID].indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
				}

				vkCmdBindPipeline(offScreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderObject->graphicsPipeline);

				// Push constants
				if (m_Shaders[renderObjectMat.material.shaderID].shader.needPushConstantBlock)
				{
					glm::mat4 view = glm::mat4(glm::mat3(g_CameraManager->CurrentCamera()->GetView())); // Truncate translation part off to center around viewer
					glm::mat4 projection = g_CameraManager->CurrentCamera()->GetProjection();
					renderObjectMat.material.pushConstantBlock.mvp =
						projection * view * glm::mat4(1.0f); // renderObject->model; TODO
					vkCmdPushConstants(offScreenCmdBuffer, renderObject->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Material::PushConstantBlock), &renderObjectMat.material.pushConstantBlock);
				}

				BindDescriptorSet(&m_Shaders[renderObjectMat.material.shaderID], renderObject->renderID, offScreenCmdBuffer, renderObject->pipelineLayout, renderObject->descriptorSet);

				if (renderObject->indexed)
				{
					vkCmdDrawIndexed(offScreenCmdBuffer, m_VertexIndexBufferPairs[renderObjectMat.material.shaderID].indexCount, 1, renderObject->indexOffset, 0, 0);
				}
				else
				{
					vkCmdDraw(offScreenCmdBuffer, renderObject->vertexBufferData->VertexCount, 1, renderObject->vertexOffset, 0);
				}
			}

			vkCmdEndRenderPass(offScreenCmdBuffer);

			VK_CHECK_RESULT(vkEndCommandBuffer(offScreenCmdBuffer));
		}

		void VulkanRenderer::BindDescriptorSet(VulkanShader* shader, RenderID renderID, VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, VkDescriptorSet descriptorSet)
		{
			u32 dynamicOffset = renderID * static_cast<u32>(m_DynamicAlignment);
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

		u32 VulkanRenderer::CreateStaticVertexBuffer(VulkanBuffer* vertexBuffer, ShaderID shaderID, i32 size)
		{
			void* vertexDataStart = malloc(size);
			if (!vertexDataStart)
			{
				PrintError("Failed to allocate memory for vertex buffer " + std::to_string(shaderID) + "! Attempted to allocate " + std::to_string(size) + " bytes");
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

					memcpy(vertexBufferData, renderObject->vertexBufferData->pDataStart, renderObject->vertexBufferData->BufferSize);

					vertexCount += renderObject->vertexBufferData->VertexCount;
					vertexBufferSize += renderObject->vertexBufferData->BufferSize;

					vertexBufferData = (char*)vertexBufferData + renderObject->vertexBufferData->BufferSize;
				}
			}

			if (vertexBufferSize == 0 || vertexCount == 0)
			{
				PrintError("Failed to create static vertex buffer (no verts use shader index " + std::to_string(shaderID) + "!)");
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
				stagingBuffer.Map(size);
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

			stagingBuffer.Map(vertexBufferSize);
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
				if (renderObject && m_Materials[renderObject->materialID].material.shaderID == shaderID && renderObject->indexed)
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
			CreateAndAllocateBuffer(m_VulkanDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer);

			stagingBuffer.Map(bufferSize);
			memcpy(stagingBuffer.m_Mapped, indices.data(), bufferSize);
			stagingBuffer.Unmap();

			CreateAndAllocateBuffer(m_VulkanDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer);

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
			CreateAndAllocateBuffer(m_VulkanDevice, bufferSize, bufferUseageFlagBits, memoryPropertyHostFlagBits, buffer);

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
			u32 imageIndex;
			VkResult result = vkAcquireNextImageKHR(m_VulkanDevice->m_LogicalDevice, m_SwapChain, std::numeric_limits<u64>::max(), m_PresentCompleteSemaphore, VK_NULL_HANDLE, &imageIndex);

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

			for (const auto& availableFormat : availableFormats)
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
			const VkPresentModeKHR bestMode = (m_VSyncEnabled ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR);
			VkPresentModeKHR secondBestMode = bestMode;

			for (const auto& availablePresentMode : availablePresentModes)
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

		VkExtent2D VulkanRenderer::ChooseSwapExtent(Window* window, const VkSurfaceCapabilitiesKHR& capabilities) const
		{
			if (capabilities.currentExtent.width != std::numeric_limits<u32>::max())
			{
				return capabilities.currentExtent;
			}
			else
			{
				i32 width, height;
				glfwGetWindowSize(((VulkanWindowWrapper*)window)->GetWindow(), &width, &height);

				VkExtent2D actualExtent = { (u32)width, (u32)height };

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

			for (const auto& extension : availableExtensions)
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

			if (m_EnableValidationLayers)
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

		void VulkanRenderer::UpdateConstantUniformBuffers(UniformOverrides const* overridenUniforms)
		{
			for (size_t i = 0; i < m_Materials.size(); ++i)
			{
				UpdateConstantUniformBuffer(overridenUniforms, i);
			}
		}

		void VulkanRenderer::UpdateConstantUniformBuffer(UniformOverrides const* overridenUniforms, size_t bufferIndex)
		{
			VulkanMaterial& material = m_Materials[bufferIndex];
			VulkanShader& shader = m_Shaders[material.material.shaderID];

			Uniforms& constantUniforms = shader.shader.constantBufferUniforms;
			VulkanUniformBufferObjectData& constantData = shader.uniformBuffer.constantData;

			if (!constantData.data)
			{
				return; // There is no constant data
			}

			glm::mat4 projection = g_CameraManager->CurrentCamera()->GetProjection();
			glm::mat4 view = g_CameraManager->CurrentCamera()->GetView();
			glm::mat4 viewInv = glm::inverse(view);
			glm::mat4 viewProjection = projection * view;
			glm::vec4 camPos = glm::vec4(g_CameraManager->CurrentCamera()->GetPosition(), 0.0f);

			if (overridenUniforms)
			{
				if (overridenUniforms->overridenUniforms.HasUniform("projection"))
				{
					projection = overridenUniforms->projection;
				}
				if (overridenUniforms->overridenUniforms.HasUniform("view"))
				{
					view = overridenUniforms->view;
				}
				if (overridenUniforms->overridenUniforms.HasUniform("viewInv"))
				{
					viewInv = overridenUniforms->viewInv;
				}
				if (overridenUniforms->overridenUniforms.HasUniform("viewProjection"))
				{
					viewProjection = overridenUniforms->viewProjection;
				}
				if (overridenUniforms->overridenUniforms.HasUniform("camPos"))
				{
					camPos = overridenUniforms->camPos;
				}
			}

			void* PointLightsDataStart = nullptr;
			size_t PointLightsSize = 0;
			size_t PointLightsMoveInBytes = 0;
			if (!m_PointLights.empty())
			{
				PointLightsDataStart = (void*)&m_PointLights[0];
				PointLightsSize = sizeof(m_PointLights[0]) * m_PointLights.size();
				PointLightsMoveInBytes = PointLightsSize / sizeof(real);
			}

			struct UniformInfo
			{
				UniformInfo(const std::string& uniformName, 
					void* dataStart,
					size_t copySize,
					size_t moveInBytes) :
					uniformName(uniformName),
					dataStart(dataStart),
					copySize(copySize),
					moveInBytes(moveInBytes)
				{}

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
				{ "dirLight", (void*)&m_DirectionalLight, sizeof(m_DirectionalLight), sizeof(m_DirectionalLight) / sizeof(real) },
				{ "pointLights", (void*)PointLightsDataStart, PointLightsSize, PointLightsMoveInBytes },
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

			u32 size = constantData.size;

#if  _DEBUG
			u32 calculatedSize1 = index * 4;
			assert(calculatedSize1 == size);
#endif // _DEBUG

			memcpy(shader.uniformBuffer.constantBuffer.m_Mapped, constantData.data, size);
		}
			
		void VulkanRenderer::UpdateDynamicUniformBuffer(RenderID renderID, UniformOverrides const* uniformOverrides)
		{
			VulkanRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject)
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

			glm::mat4 model = renderObject->gameObject->GetTransform()->GetModelMatrix();
			glm::mat4 modelInvTranspose = glm::transpose(glm::inverse(model));
			glm::mat4 projection = g_CameraManager->CurrentCamera()->GetProjection();
			glm::mat4 view = g_CameraManager->CurrentCamera()->GetView();
			glm::mat4 modelViewProjection = projection * view * model;
			glm::vec4 colorMultiplier = material.material.colorMultiplier;
			u32 enableAlbedoSampler = material.material.enableAlbedoSampler;
			u32 enableMetallicSampler = material.material.enableMetallicSampler;
			u32 enableRoughnessSampler = material.material.enableRoughnessSampler;
			u32 enableAOSampler = material.material.enableAOSampler;
			u32 enableDiffuseSampler = material.material.enableDiffuseSampler;
			u32 enableNormalSampler = material.material.enableNormalSampler;
			u32 enableCubemapSampler = material.material.enableCubemapSampler;
			u32 enableIrradianceSampler = material.material.enableIrradianceSampler;

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

			u32 offset = 0;
			for (VulkanRenderObject* renderObj : m_RenderObjects)
			{
				if (renderObj->renderID != renderObject->renderID &&
					m_Materials[renderObj->materialID].material.shaderID == m_Materials[renderObject->materialID].material.shaderID)
				{
					offset += uniformBuffer.dynamicData.size;
				}
			}
			u32 index = 0;

			struct UniformInfo
			{
				UniformInfo(const std::string& uniformName,
					void* dataStart,
					size_t copySize,
					size_t moveInBytes) :
					uniformName(uniformName),
					dataStart(dataStart),
					copySize(copySize),
					moveInBytes(moveInBytes)
				{}

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
				{ "colorMultiplier", (void*)&material.material.colorMultiplier, 16, 4 },
				{ "constAlbedo", (void*)&material.material.constAlbedo, 16, 4 },
				{ "constMetallic", (void*)&material.material.constMetallic, 4, 1 },
				{ "constRoughness", (void*)&material.material.constRoughness, 4, 1 },
				{ "constAO", (void*)&material.material.constAO, 4, 1 },
				{ "enableAlbedoSampler", (void*)&enableAlbedoSampler, 4, 1 },
				{ "enableMetallicSampler", (void*)&enableMetallicSampler, 4, 1 },
				{ "enableRoughnessSampler", (void*)&enableRoughnessSampler, 4, 1 },
				{ "enableAOSampler", (void*)&enableAOSampler, 4, 1 },
				{ "enableDiffuseSampler", (void*)&enableDiffuseSampler, 4, 1 },
				{ "enableNormalSampler", (void*)&enableNormalSampler, 4, 1 },
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
			u32 size = uniformBuffer.dynamicData.size;

#if  _DEBUG
			u32 calculatedSize1 = index * 4;
			assert(calculatedSize1 == size);
#endif // _DEBUG

			void* firstIndex = uniformBuffer.dynamicBuffer.m_Mapped;
			u64 dest = (u64)firstIndex + (renderID * m_DynamicAlignment);
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
				{ "pbr", shaderDirectory + "vk_pbr_vert.spv", shaderDirectory + "vk_pbr_frag.spv", m_VulkanDevice->m_LogicalDevice },
				{ "skybox", shaderDirectory + "vk_skybox_vert.spv", shaderDirectory + "vk_skybox_frag.spv", m_VulkanDevice->m_LogicalDevice },
				{ "equirectangular_to_cube", shaderDirectory + "vk_skybox_vert.spv", shaderDirectory + "vk_equirectangular_to_cube_frag.spv", m_VulkanDevice->m_LogicalDevice },
				{ "irradiance", shaderDirectory + "vk_skybox_vert.spv", shaderDirectory + "vk_irradiance_frag.spv", m_VulkanDevice->m_LogicalDevice },
				{ "prefilter", shaderDirectory + "vk_skybox_vert.spv", shaderDirectory + "vk_prefilter_frag.spv", m_VulkanDevice->m_LogicalDevice },
				{ "brdf", shaderDirectory + "vk_brdf_vert.spv", shaderDirectory + "vk_brdf_frag.spv", m_VulkanDevice->m_LogicalDevice },
				{ "background", shaderDirectory + "vk_background_vert.spv", shaderDirectory + "vk_background_frag.spv", m_VulkanDevice->m_LogicalDevice },
				{ "deferred_combine", shaderDirectory + "vk_deferred_combine_vert.spv", shaderDirectory + "vk_deferred_combine_frag.spv", m_VulkanDevice->m_LogicalDevice },
				{ "deferred_combine_cubemap", shaderDirectory + "vk_deferred_combine_cubemap_vert.spv", shaderDirectory + "vk_deferred_combine_cubemap_frag.spv", m_VulkanDevice->m_LogicalDevice },
				//{ "post_process", shaderDirectory + "vk_post_process_vert.spv", shaderDirectory + "vk_post_process_frag.spv", m_VulkanDevice->m_LogicalDevice },
			};

			// Specify shader uniforms
			ShaderID shaderID = 0;

			// TODO: Remove material?
			// Deferred Simple
			m_Shaders[shaderID].shader.numAttachments = 3;
			m_Shaders[shaderID].shader.deferred = true;
			m_Shaders[shaderID].shader.subpass = 0;
			m_Shaders[shaderID].shader.needDiffuseSampler = true;
			m_Shaders[shaderID].shader.needNormalSampler = true;
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV |
				(u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT |
				(u32)VertexAttribute::TANGENT |
				(u32)VertexAttribute::BITANGENT |
				(u32)VertexAttribute::NORMAL;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("uniformBufferConstant");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("viewProjection");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("uniformBufferDynamic");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("modelInvTranspose");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableDiffuseSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("diffuseSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableNormalSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("normalSampler");
			++shaderID;

			// Color
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.translucent = true;
			m_Shaders[shaderID].shader.subpass = 1;
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("uniformBufferConstant");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("viewProjection");
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("uniformBufferDynamic");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("colorMultiplier");
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
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV | 
				(u32)VertexAttribute::TANGENT |
				(u32)VertexAttribute::BITANGENT | 
				(u32)VertexAttribute::NORMAL;

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
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION;

			m_Shaders[shaderID].shader.constantBufferUniforms = {};

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("cubemapSampler");
			++shaderID;

			// Equirectangular to cube
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.subpass = 1;
			m_Shaders[shaderID].shader.needHDREquirectangularSampler = true;
			m_Shaders[shaderID].shader.needPushConstantBlock = true;
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("hdrEquirectangularSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			++shaderID;

			// Irradiance
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.subpass = 1;
			m_Shaders[shaderID].shader.needCubemapSampler = true;
			m_Shaders[shaderID].shader.needPushConstantBlock = true;
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("cubemapSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			++shaderID;

			// Prefilter
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.subpass = 1;
			m_Shaders[shaderID].shader.needCubemapSampler = true;
			m_Shaders[shaderID].shader.needPushConstantBlock = true;
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("cubemapSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("uniformBufferDynamic");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("roughness");
			++shaderID;

			// BRDF
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.subpass = 1;
			m_Shaders[shaderID].shader.vertexAttributes = 0; // No vertex attributes! Not even position (vertex index is used)

			m_Shaders[shaderID].shader.constantBufferUniforms = {};

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			++shaderID;

			// Background
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.subpass = 1;
			m_Shaders[shaderID].shader.needCubemapSampler = true;
			m_Shaders[shaderID].shader.needPushConstantBlock = true;
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION;

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
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION |
				(u32)VertexAttribute::UV;

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

			// Deferred combine cubemap (sample gbuffer)
			m_Shaders[shaderID].shader.deferred = false; // Sounds strange but this isn't deferred
			m_Shaders[shaderID].shader.subpass = 0;
			m_Shaders[shaderID].shader.depthWriteEnable = false; // Disable depth writing
			m_Shaders[shaderID].shader.needBRDFLUT = true;
			m_Shaders[shaderID].shader.needIrradianceSampler = true;
			m_Shaders[shaderID].shader.needPrefilteredMap = true;
			m_Shaders[shaderID].shader.vertexAttributes =
				(u32)VertexAttribute::POSITION; // Used as 3D texture coord into cubemap

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("cubemapSampler");
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
				Shader& shader = m_Shaders[i].shader;

				std::string vertFileName = shader.vertexShaderFilePath;
				StripLeadingDirectories(vertFileName);
				std::string fragFileName = m_Shaders[i].shader.fragmentShaderFilePath;
				StripLeadingDirectories(fragFileName);
				Print("Loading shaders " + vertFileName + " & " + fragFileName);

				if (!ReadFile(shader.vertexShaderFilePath, shader.vertexShaderCode))
				{
					PrintError("Could not find vertex shader " + shader.name);
				}
				if (!ReadFile(shader.fragmentShaderFilePath, shader.fragmentShaderCode))
				{
					PrintError("Could not find fragment shader " + shader.name);
				}
			}
		}

		void VulkanRenderer::CaptureSceneToCubemap(RenderID cubemapRenderID)
		{
			// TODO: Finish implementing this function
			return;

			DrawCallInfo drawCallInfo = {};
			drawCallInfo.renderToCubemap = true;
			drawCallInfo.cubemapObjectRenderID = cubemapRenderID;

			VulkanRenderObject* cubemapRenderObject = GetRenderObject(drawCallInfo.cubemapObjectRenderID);
			VulkanMaterial* cubemapMaterial = &m_Materials[cubemapRenderObject->materialID];

			glm::vec2 cubemapSize = cubemapMaterial->material.cubemapSamplerSize;

			for (size_t face = 0; face < 6; ++face)
			{
				// Clear all gbuffers
				//if (!cubemapMaterial->cubemapSamplerGBuffersIDs.empty())
				//{
				//	for (size_t i = 0; i < cubemapMaterial->cubemapSamplerGBuffersIDs.size(); ++i)
				//	{
				//		// Bind color CUBE_MAP_POSITIVE_X + face to cubemapMaterial->cubemapSamplerGBuffersIDs[i].id
				//		// Clear
				//	}
				//}

				// Clear base cubemap framebuffer + depth buffer
				// Bind color CUBE_MAP_POSITIVE_X + face to cubemapMaterial->cubemapSamplerID
				// Bind depth CUBE_MAP_POSITIVE_X + face to cubemapMaterial->cubemapDepthSamplerID
				// Clear
			}












			// TODO: Maybe not?





			//drawCallInfo.deferred = true;
			//BuildDeferredCommandBuffer(drawCallInfo);
			drawCallInfo.deferred = false;
			//DrawGBufferQuad(drawCallInfo);
			BuildCommandBuffers(drawCallInfo);
		}

		//void VulkanRenderer::GenerateCubemapFromHDREquirectangular(MaterialID cubemapMaterialID, const std::string& environmentMapPath)
		//{
		//	UNREFERENCED_PARAMETER(cubemapMaterialID);
		//	UNREFERENCED_PARAMETER(environmentMapPath);
		//}

		void VulkanRenderer::GeneratePrefilteredMapFromCubemap(MaterialID cubemapMaterialID)
		{
			UNREFERENCED_PARAMETER(cubemapMaterialID);
		}

		void VulkanRenderer::GenerateIrradianceSamplerFromCubemap(MaterialID cubemapMaterialID)
		{
			UNREFERENCED_PARAMETER(cubemapMaterialID);
		}

		//void VulkanRenderer::GenerateBRDFLUT(u32 brdfLUTTextureID, glm::vec2 BRDFLUTSize)
		//{
		//	UNREFERENCED_PARAMETER(brdfLUTTextureID);
		//	UNREFERENCED_PARAMETER(BRDFLUTSize);
		//}

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
				Print(msg);
				break;
			case VkDebugReportFlagBitsEXT::VK_DEBUG_REPORT_WARNING_BIT_EXT:
				PrintWarn(msg);
				break;
			case VkDebugReportFlagBitsEXT::VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT:
				PrintWarn(msg);
				break;
			case VkDebugReportFlagBitsEXT::VK_DEBUG_REPORT_ERROR_BIT_EXT:
				PrintError(msg);
				break;
			case VkDebugReportFlagBitsEXT::VK_DEBUG_REPORT_DEBUG_BIT_EXT:
				// Fall through
			default:
				PrintError(msg);
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