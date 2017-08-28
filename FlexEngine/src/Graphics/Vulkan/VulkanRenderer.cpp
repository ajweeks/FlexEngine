#include "stdafx.h"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanRenderer.h"

#include <algorithm>
#include <set>
#include <iostream>
#include <unordered_map>

#include <SOIL.h>

#include "FreeCamera.h"
#include "Helpers.h"
#include "Logger.h"
#include "VertexBufferData.h"

namespace flex
{
	namespace vk
	{
		VulkanRenderer::VulkanRenderer(const GameContext& gameContext)
		{
			CreateInstance(gameContext);
			SetupDebugCallback();
			CreateSurface(gameContext.window);
			VkPhysicalDevice physicalDevice = PickPhysicalDevice();
			CreateLogicalDevice(physicalDevice);

			m_SwapChain = { m_VulkanDevice->m_LogicalDevice, vkDestroySwapchainKHR };
			m_RenderPass = { m_VulkanDevice->m_LogicalDevice, vkDestroyRenderPass };
			m_DepthImage = { m_VulkanDevice->m_LogicalDevice, vkDestroyImage };
			m_DepthImageMemory = { m_VulkanDevice->m_LogicalDevice, vkFreeMemory };
			m_DepthImageView = { m_VulkanDevice->m_LogicalDevice, vkDestroyImageView };
			m_DescriptorPool = { m_VulkanDevice->m_LogicalDevice, vkDestroyDescriptorPool };
			m_ImageAvailableSemaphore = { m_VulkanDevice->m_LogicalDevice, vkDestroySemaphore };
			m_RenderFinishedSemaphore = { m_VulkanDevice->m_LogicalDevice, vkDestroySemaphore };

			CreateSwapChain(gameContext.window);
			CreateImageViews();
			CreateRenderPass();

			CreateCommandPool();
			CreateDepthResources();
			CreateFramebuffers();

			LoadDefaultShaderCode();

			const glm::uint shaderCount = m_LoadedShaderCode.size();
			m_VertexIndexBufferPairs.reserve(shaderCount);
			for (size_t i = 0; i < shaderCount; ++i)
			{
				m_VertexIndexBufferPairs.push_back({
					new Buffer(m_VulkanDevice->m_LogicalDevice), // Vertex buffer
					new Buffer(m_VulkanDevice->m_LogicalDevice)  // Index buffer
				});
			}

			// ImGui buffers are dynamic, they shouldn't use the staging buffer
			m_VertexIndexBufferPairs[2].useStagingBuffer = false;

			CreateVulkanTexture(RESOURCE_LOCATION + "textures/brick_d.png", &m_BrickDiffuseTexture);
			CreateVulkanTexture(RESOURCE_LOCATION + "textures/brick_n.png", &m_BrickNormalTexture);
			CreateVulkanTexture(RESOURCE_LOCATION + "textures/brick_s.png", &m_BrickSpecularTexture);

			CreateVulkanTexture(RESOURCE_LOCATION + "textures/work_d.jpg", &m_WorkDiffuseTexture);
			CreateVulkanTexture(RESOURCE_LOCATION + "textures/work_n.jpg", &m_WorkNormalTexture);
			CreateVulkanTexture(RESOURCE_LOCATION + "textures/work_s.jpg", &m_WorkSpecularTexture);

			CreateVulkanTexture(RESOURCE_LOCATION + "textures/blank.jpg", &m_BlankTexture);
		}

		void VulkanRenderer::PostInitialize()
		{
			PrepareUniformBuffers();

			CreateDescriptorPool();

			for (size_t i = 0; i < m_UniformBuffers.size(); ++i)
			{
				CreateDescriptorSetLayout(i);
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
		}

		VulkanRenderer::~VulkanRenderer()
		{
			ReleaseUniformBuffers();

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

			m_ImageAvailableSemaphore.replace();
			m_RenderFinishedSemaphore.replace();

			for (size_t i = 0; i < m_VertexIndexBufferPairs.size(); ++i)
			{
				SafeDelete(m_VertexIndexBufferPairs[i].vertexBuffer);
				SafeDelete(m_VertexIndexBufferPairs[i].indexBuffer);
			}

			m_DescriptorPool.replace();
			m_DepthImageView.replace();
			m_DepthImageMemory.replace();
			m_DepthImage.replace();

			SafeDelete(m_BlankTexture);
			SafeDelete(m_SkyboxTexture);

			SafeDelete(m_WorkSpecularTexture);
			SafeDelete(m_WorkNormalTexture);
			SafeDelete(m_WorkDiffuseTexture);

			SafeDelete(m_BrickSpecularTexture);
			SafeDelete(m_BrickNormalTexture);
			SafeDelete(m_BrickDiffuseTexture);

			m_RenderPass.replace();

			m_SwapChain.replace();
			m_SwapChainImageViews.clear();
			m_SwapChainFramebuffers.clear();

			vkDeviceWaitIdle(m_VulkanDevice->m_LogicalDevice);

			SafeDelete(m_VulkanDevice);

			glfwTerminate();
		}

		MaterialID VulkanRenderer::InitializeMaterial(const GameContext& gameContext, const MaterialCreateInfo* createInfo)
		{
			UNREFERENCED_PARAMETER(gameContext);

			Material mat = {};

			mat.shaderIndex = createInfo->shaderIndex;
			mat.name = createInfo->name;
			mat.diffuseTexturePath = createInfo->diffuseTexturePath;
			mat.normalTexturePath = createInfo->normalTexturePath;
			mat.specularTexturePath = createInfo->specularTexturePath;
			mat.cubeMapFilePaths = createInfo->cubeMapFilePaths;
		
			if (createInfo->diffuseTexturePath == m_BrickDiffuseTexture->filePath)
			{
				mat.diffuseTexture = m_BrickDiffuseTexture;
			}
			else if (createInfo->diffuseTexturePath == m_WorkDiffuseTexture->filePath)
			{
				mat.diffuseTexture= m_WorkDiffuseTexture;
			}

			if (createInfo->normalTexturePath == m_BrickNormalTexture->filePath)
			{
				mat.normalTexture = m_BrickNormalTexture;
			}
			else if (createInfo->normalTexturePath == m_WorkNormalTexture->filePath)
			{
				mat.normalTexture = m_WorkNormalTexture;
			}

			if (createInfo->specularTexturePath == m_BrickSpecularTexture->filePath)
			{
				mat.specularTexture = m_BrickSpecularTexture;
			}
			else if (createInfo->specularTexturePath == m_WorkSpecularTexture->filePath)
			{
				mat.specularTexture = m_WorkSpecularTexture;
			}

			if (!createInfo->cubeMapFilePaths[0].empty())
			{
				if (m_SkyboxTexture == nullptr)
				{
					CreateVulkanCubemap(createInfo->cubeMapFilePaths, &m_SkyboxTexture);
					mat.cubemapTexture = m_SkyboxTexture;
					mat.useCubemapTexture = true;
				}
				else // A skybox has already been created
				{
					if (createInfo->cubeMapFilePaths[0].compare(m_SkyboxTexture->filePath) == 0)
					{
						mat.cubemapTexture = m_SkyboxTexture;
					}
					else
					{
						Logger::LogError("Only one skybox per scene is allowed by the Vulkan renderer! Memory leaks will occur!");
					}
				}
			}

			m_LoadedMaterials.push_back(mat);

			return m_LoadedMaterials.size() - 1;
		}

		glm::uint VulkanRenderer::InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo)
		{
			UNREFERENCED_PARAMETER(gameContext);

			RenderID renderID = GetFirstAvailableRenderID();
			RenderObject* renderObject = new RenderObject(m_VulkanDevice->m_LogicalDevice, renderID);
			InsertNewRenderObject(renderObject);

			renderObject->vertexBufferData = createInfo->vertexBufferData;
			renderObject->materialID = createInfo->materialID;
			renderObject->cullMode = CullFaceToVkCullMode(createInfo->cullFace);
			renderObject->info.name = createInfo->name;
			renderObject->info.materialName = m_LoadedMaterials[renderObject->materialID].name;

			if (createInfo->indices != nullptr)
			{
				renderObject->indices = createInfo->indices;
				renderObject->indexed = true;
			}

			return renderID;
		}

		void VulkanRenderer::SetTopologyMode(RenderID renderID, TopologyMode topology)
		{
			RenderObject* renderObject = GetRenderObject(renderID);
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
		}

		void VulkanRenderer::Draw(const GameContext& gameContext)
		{
			UNREFERENCED_PARAMETER(gameContext);

			// TODO: Only call this when objects change
			RebuildCommandBuffers();
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

		void VulkanRenderer::Clear(int flags, const GameContext& gameContext)
		{
			UNREFERENCED_PARAMETER(gameContext);
			UNREFERENCED_PARAMETER(flags);
		}

		void VulkanRenderer::SwapBuffers(const GameContext& gameContext)
		{
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

		void VulkanRenderer::UpdateTransformMatrix(const GameContext& gameContext, RenderID renderID, const glm::mat4& model)
		{
			UpdateUniformBufferDynamic(gameContext, renderID, model);
		}

		int VulkanRenderer::GetShaderUniformLocation(glm::uint program, const std::string& uniformName)
		{
			// TODO: Implement
			UNREFERENCED_PARAMETER(program);
			UNREFERENCED_PARAMETER(uniformName);
			return 0;
		}

		void VulkanRenderer::SetUniform1f(int location, float val)
		{
			// TODO: Implement
			UNREFERENCED_PARAMETER(location);
			UNREFERENCED_PARAMETER(val);
		}

		glm::uint VulkanRenderer::GetRenderObjectCount() const
		{
			glm::uint count = 0;

			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				if (m_RenderObjects[i] != nullptr) ++count;
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

		void VulkanRenderer::GetRenderObjectInfos(std::vector<RenderObjectInfo>& vec)
		{
			vec.clear();
			vec.resize(GetRenderObjectCount());

			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				vec[i] = m_RenderObjects[i]->info;
			}
		}

		void VulkanRenderer::ImGui_Init(const GameContext& gameContext)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.UserData = this;

			io.RenderDrawListsFn = NULL;

			glm::vec2i windowSize = gameContext.window->GetSize();
			glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();
			io.DisplaySize = ImVec2((float)windowSize.x, (float)windowSize.y);
			io.DisplayFramebufferScale = ImVec2(
				windowSize.x > 0 ? ((float)frameBufferSize.x / windowSize.x) : 0,
				windowSize.y > 0 ? ((float)frameBufferSize.y / windowSize.y) : 0);

			//io.SetClipboardTextFn = ImGui_SetClipboardText;
			//io.GetClipboardTextFn = ImGui_GetClipboardText;

			//io.ClipboardUserData = g_Window;
	#ifdef _WIN32
			//io.ImeWindowHandle = glfwGetWin32Window(g_Window);
	#endif

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

			VertexIndexBufferPair& bufferPair = m_VertexIndexBufferPairs[2];

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
			vkCmdPushConstants(commandBuffer, m_ImGui_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstBlock), &m_ImGuiPushConstBlock);

			// Render commands
			ImDrawData* imDrawData = ImGui::GetDrawData();
			int32_t vertexOffset = 0;
			int32_t indexOffset = 0;
			for (int32_t i = 0; i < imDrawData->CmdListsCount; i++)
			{
				const ImDrawList* cmd_list = imDrawData->CmdLists[i];
				for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++)
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
				createInfo.descriptorSetLayoutIndex = 2;
				createInfo.diffuseTexture = m_ImGuiFontTexture;
				createInfo.uniformBufferIndex = 2;

				CreateDescriptorSet(&createInfo);
			}

			VkPushConstantRange pushConstants[1] = {};
			pushConstants[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			pushConstants[0].offset = sizeof(float) * 0;
			pushConstants[0].size = sizeof(float) * 4;

			const glm::uint shaderIndex = 2;
			VertexBufferData vertexBufferData = {};
			vertexBufferData.Attributes =
				(glm::uint)VertexBufferData::Attribute::POSITION_2D |
				(glm::uint)VertexBufferData::Attribute::UV |
				(glm::uint)VertexBufferData::Attribute::COLOR_R8G8B8A8_UNORM;
			vertexBufferData.VertexStride = vertexBufferData.CalculateStride();
		
			assert(vertexBufferData.VertexStride == sizeof(ImDrawVert));

			glm::uint descriptorSetLayoutIndex = shaderIndex;

			GraphicsPipelineCreateInfo createInfo = {};

			createInfo.shaderIndex = shaderIndex;
			createInfo.vertexBufferData = &vertexBufferData;
			createInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			createInfo.cullMode = VK_CULL_MODE_NONE;
			createInfo.descriptorSetLayoutIndex = descriptorSetLayoutIndex;
			createInfo.setDynamicStates = true;
			createInfo.enabledColorBlending = true;
			createInfo.pushConstants = pushConstants;
			createInfo.pushConstantRangeCount = 1;
			createInfo.pipelineLayout = &m_ImGui_PipelineLayout;
			createInfo.grahpicsPipeline = &m_ImGui_GraphicsPipeline;
			createInfo.pipelineCache = &m_ImGuiPipelineCache;

			CreateGraphicsPipeline(&createInfo);
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

		void VulkanRenderer::PostInitializeRenderObject(RenderID renderID)
		{
			CreateDescriptorSet(renderID);
			CreateGraphicsPipeline(renderID);
		}

		RenderObject* VulkanRenderer::GetRenderObject(RenderID renderID)
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

			CreateImageView(m_ImGuiFontTexture->image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, &m_ImGuiFontTexture->imageView);

			// Staging buffers for font data upload
			Buffer stagingBuffer(m_VulkanDevice->m_LogicalDevice);

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
				m_ImGuiFontTexture->image,
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
				m_ImGuiFontTexture->image,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

			FlushCommandBuffer(copyCmd, copyQueue, true);

			stagingBuffer.Destroy();

			CreateTextureSampler(m_ImGuiFontTexture, 1.0f, -1000.0f, 1000.0f);

			// Store our identifier
			io.Fonts->TexID = (void *)(intptr_t)m_ImGuiFontTexture->image;

			return true;
		}

		uint32_t VulkanRenderer::ImGui_MemoryType(VkMemoryPropertyFlags properties, uint32_t type_bits)
		{
			// TODO: Merge with existing memory type function
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

		void VulkanRenderer::InsertNewRenderObject(RenderObject* renderObject)
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
			CreateImageViews();
			CreateRenderPass();
			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				CreateGraphicsPipeline(i);
			}
			CreateDepthResources();
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

		void VulkanRenderer::CreateImageViews()
		{
			m_SwapChainImageViews.resize(m_SwapChainImages.size(), VDeleter<VkImageView>{ m_VulkanDevice->m_LogicalDevice, vkDestroyImageView });

			for (uint32_t i = 0; i < m_SwapChainImages.size(); ++i)
			{
				CreateImageView(m_SwapChainImages[i], m_SwapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, m_SwapChainImageViews[i].replace());
			}
		}

		void VulkanRenderer::CreateTextureImageView(VulkanTexture* texture)
		{
			CreateImageView(texture->image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, texture->imageView.replace());
		}

		void VulkanRenderer::CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VkImageView* imageView)
		{
			VkImageViewCreateInfo viewInfo = {};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = image;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = format;
			viewInfo.subresourceRange.aspectMask = aspectFlags;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
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
			depthAttachment.format = FindDepthFormat();
			depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkAttachmentReference depthAttachmentRef = {};
			depthAttachmentRef.attachment = 1;
			depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorAttachmentRef;
			subpass.pDepthStencilAttachment = &depthAttachmentRef;

			VkSubpassDependency dependency = {};
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.srcAccessMask = 0;
			dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = attachments.size();
			renderPassInfo.pAttachments = attachments.data();
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = 1;
			renderPassInfo.pDependencies = &dependency;

			VK_CHECK_RESULT(vkCreateRenderPass(m_VulkanDevice->m_LogicalDevice, &renderPassInfo, nullptr, m_RenderPass.replace()));
		}

		void VulkanRenderer::CreateTextureSampler(VulkanTexture* texture, float maxAnisotropy, float minLod, float maxLod)
		{
			VkSamplerCreateInfo samplerInfo = {};
			samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.magFilter = VK_FILTER_LINEAR;
			samplerInfo.minFilter = VK_FILTER_LINEAR;
			samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			samplerInfo.anisotropyEnable = VK_TRUE;
			samplerInfo.maxAnisotropy = maxAnisotropy;
			samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
			samplerInfo.unnormalizedCoordinates = VK_FALSE;
			samplerInfo.compareEnable = VK_FALSE;
			samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerInfo.mipLodBias = 0.0f;
			samplerInfo.minLod = minLod;
			samplerInfo.maxLod = maxLod;

			VK_CHECK_RESULT(vkCreateSampler(m_VulkanDevice->m_LogicalDevice, &samplerInfo, nullptr, texture->sampler.replace()));
		}

		void VulkanRenderer::CreateGraphicsPipeline(RenderID renderID)
		{
			RenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject) return;
			Material* material = &m_LoadedMaterials[renderObject->materialID];

			VkPipelineLayout* pipelineLayout = renderObject->pipelineLayout.replace();
			VkPipeline* graphicsPipeline = renderObject->graphicsPipeline.replace();

			GraphicsPipelineCreateInfo createInfo = {};
			createInfo.shaderIndex = material->shaderIndex;
			createInfo.vertexBufferData = renderObject->vertexBufferData;
			createInfo.topology = renderObject->topology;
			createInfo.cullMode = renderObject->cullMode;
			createInfo.descriptorSetLayoutIndex = material->descriptorSetLayoutIndex;
			createInfo.setDynamicStates = false;
			createInfo.enabledColorBlending = false;
			createInfo.pipelineLayout = pipelineLayout;
			createInfo.grahpicsPipeline = graphicsPipeline;
		
			CreateGraphicsPipeline(&createInfo);
		}

		void VulkanRenderer::CreateGraphicsPipeline(GraphicsPipelineCreateInfo* createInfo)
		{
			ShaderCodePair shaderCode = m_LoadedShaderCode[createInfo->shaderIndex];
			std::vector<char> vertShaderCode = shaderCode.vertexShaderCode;
			std::vector<char> fragShaderCode = shaderCode.fragmentShaderCode;

			VDeleter<VkShaderModule> vertShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			CreateShaderModule(vertShaderCode, vertShaderModule);

			VDeleter<VkShaderModule> fragShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			CreateShaderModule(fragShaderCode, fragShaderModule);

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

			VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

			VkVertexInputBindingDescription bindingDescription = GetVertexBindingDescription(createInfo->vertexBufferData);
			std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
			GetVertexAttributeDescriptions(createInfo->vertexBufferData, attributeDescriptions);

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

			VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
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

			VkPipelineColorBlendStateCreateInfo colorBlending = {};
			colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlending.logicOpEnable = VK_FALSE;
			colorBlending.logicOp = VK_LOGIC_OP_COPY;
			colorBlending.attachmentCount = 1;
			colorBlending.pAttachments = &colorBlendAttachment;
			colorBlending.blendConstants[0] = 0.0f;
			colorBlending.blendConstants[1] = 0.0f;
			colorBlending.blendConstants[2] = 0.0f;
			colorBlending.blendConstants[3] = 0.0f;

			VkDescriptorSetLayout setLayouts[] = { m_DescriptorSetLayouts[createInfo->descriptorSetLayoutIndex] };
			VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutInfo.setLayoutCount = 1;
			pipelineLayoutInfo.pSetLayouts = setLayouts;
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
			depthStencil.depthTestEnable = VK_TRUE;
			depthStencil.depthWriteEnable = VK_TRUE;
			depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
			depthStencil.depthBoundsTestEnable = VK_FALSE;
			depthStencil.minDepthBounds = 0.0f;
			depthStencil.maxDepthBounds = 1.0f;
			depthStencil.stencilTestEnable = VK_FALSE;
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
			pipelineInfo.stageCount = 2;
			pipelineInfo.pStages = shaderStages;
			pipelineInfo.pVertexInputState = &vertexInputInfo;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.layout = *createInfo->pipelineLayout;
			pipelineInfo.renderPass = m_RenderPass;
			pipelineInfo.pDynamicState = pDynamicState;
			pipelineInfo.subpass = 0;
			pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

			VkPipelineCache pipelineCache = createInfo->pipelineCache ? *createInfo->pipelineCache : VK_NULL_HANDLE;

			VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_VulkanDevice->m_LogicalDevice, pipelineCache, 1, &pipelineInfo, nullptr, createInfo->grahpicsPipeline));
		}

		void VulkanRenderer::CreateFramebuffers()
		{
			m_SwapChainFramebuffers.resize(m_SwapChainImageViews.size(), VDeleter<VkFramebuffer>{m_VulkanDevice->m_LogicalDevice, vkDestroyFramebuffer});

			for (size_t i = 0; i < m_SwapChainImageViews.size(); ++i)
			{
				std::array<VkImageView, 2> attachments = {
					m_SwapChainImageViews[i],
					m_DepthImageView
				};

				VkFramebufferCreateInfo framebufferInfo = {};
				framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				framebufferInfo.renderPass = m_RenderPass;
				framebufferInfo.attachmentCount = attachments.size();
				framebufferInfo.pAttachments = attachments.data();
				framebufferInfo.width = m_SwapChainExtent.width;
				framebufferInfo.height = m_SwapChainExtent.height;
				framebufferInfo.layers = 1;

				VK_CHECK_RESULT(vkCreateFramebuffer(m_VulkanDevice->m_LogicalDevice, &framebufferInfo, nullptr, m_SwapChainFramebuffers[i].replace()));
			}
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

		VkCommandBuffer VulkanRenderer::BeginSingleTimeCommands()
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

		void VulkanRenderer::EndSingleTimeCommands(VkCommandBuffer commandBuffer)
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

		void VulkanRenderer::CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
			VkMemoryPropertyFlags properties, VkImageLayout initialLayout, VkImage* image, VkDeviceMemory* imageMemory, glm::uint arrayLayers, glm::uint mipLevels, VkImageCreateFlags flags)
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

			vkBindImageMemory(m_VulkanDevice->m_LogicalDevice, *image, *imageMemory, 0);
		}

		VkFormat VulkanRenderer::FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
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

		VkFormat VulkanRenderer::FindDepthFormat()
		{
			return FindSupportedFormat(
			{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
				VK_IMAGE_TILING_OPTIMAL,
				VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
			);
		}

		bool VulkanRenderer::HasStencilComponent(VkFormat format)
		{
			return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
		}

		void VulkanRenderer::CreateDepthResources()
		{
			VkFormat depthFormat = FindDepthFormat();

			CreateImage(m_SwapChainExtent.width, m_SwapChainExtent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_PREINITIALIZED, m_DepthImage.replace(), m_DepthImageMemory.replace());
			CreateImageView(m_DepthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, m_DepthImageView.replace());

			TransitionImageLayout(m_DepthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		}

		void VulkanRenderer::CreateVulkanTexture(const std::string& filePath, VulkanTexture** texture)
		{
			CreateTextureImage(filePath, texture);
			if (*texture != nullptr)
			{
				CreateTextureImageView(*texture);
				CreateTextureSampler(*texture);
			}

			(*texture)->filePath = filePath;
		}

		void VulkanRenderer::CreateVulkanCubemap(const std::array<std::string, 6>& filePaths, VulkanTexture** texture)
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

			images.reserve(filePaths.size());
			for (size_t i = 0; i < filePaths.size(); ++i)
			{
				unsigned char* pixels = SOIL_load_image(filePaths[i].c_str(), &textureWidth, &textureHeight, &textureChannels, SOIL_LOAD_RGBA);
				if (!pixels)
				{
					Logger::LogError("SOIL loading error: " + std::string(SOIL_last_result()) + ", image filepath: " + filePaths[i]);
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
			for (size_t i = 0; i < images.size(); ++i)
			{
				memcpy(pixelData, images[i].pixels, images[i].size);
				pixelData += (images[i].size / sizeof(unsigned char));
			}

			for (size_t i = 0; i < images.size(); ++i)
			{
				SOIL_free_image_data(images[i].pixels);
			}

			*texture = new VulkanTexture(m_VulkanDevice->m_LogicalDevice);

			VkFormat imageFormat = VK_FORMAT_R8G8B8A8_UNORM; // What should this be???? VK_FORMAT_R32G32B32A32_UINT? VK_FORMAT_R8G8B8A8_UINT?
			glm::uint mipLevels = 1;

			VkMemoryAllocateInfo memAllocInfo = {};
			memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

			VkMemoryRequirements memReqs;

			// Create a host-visible staging buffer that contains the raw image data
			VkBuffer stagingBuffer;
			VkDeviceMemory stagingMemory;

			VkBufferCreateInfo bufferCreateInfo = {};
			bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferCreateInfo.size = totalSize; // TODO: size1?
											   // This buffer is used as a transfer source for the buffer copy
			bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VK_CHECK_RESULT(vkCreateBuffer(m_VulkanDevice->m_LogicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

			// Get memory requirements for the staging buffer (alignment, memory type bits)
			vkGetBufferMemoryRequirements(m_VulkanDevice->m_LogicalDevice, stagingBuffer, &memReqs);
			memAllocInfo.allocationSize = memReqs.size;
			// Get memory type index for a host visible buffer
			memAllocInfo.memoryTypeIndex = m_VulkanDevice->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			VK_CHECK_RESULT(vkAllocateMemory(m_VulkanDevice->m_LogicalDevice, &memAllocInfo, nullptr, &stagingMemory));
			VK_CHECK_RESULT(vkBindBufferMemory(m_VulkanDevice->m_LogicalDevice, stagingBuffer, stagingMemory, 0));

			// Copy texture data into staging buffer
			uint8_t *data;
			VK_CHECK_RESULT(vkMapMemory(m_VulkanDevice->m_LogicalDevice, stagingMemory, 0, memReqs.size, 0, (void**)&data));
			memcpy(data, pixels, totalSize);
			free(pixels);
			vkUnmapMemory(m_VulkanDevice->m_LogicalDevice, stagingMemory);

			// Create optimal tiled target image
			VkImageCreateInfo imageCreateInfo = {};
			imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = imageFormat;
			imageCreateInfo.mipLevels = mipLevels;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.extent = { (glm::uint)textureWidth, (glm::uint)textureHeight, 1u };
			imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			// Cube faces count as array layers in Vulkan
			imageCreateInfo.arrayLayers = 6;
			// This flag is required for cube map images
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
					bufferCopyRegion.imageExtent.width = textureWidth; // TODO: Use correct values here when using mipmapping
					bufferCopyRegion.imageExtent.height = textureHeight;
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
				(*texture)->image,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);

			// Copy the cube map faces from the staging buffer to the optimal tiled image
			vkCmdCopyBufferToImage(
				copyCmd,
				stagingBuffer,
				(*texture)->image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				static_cast<uint32_t>(bufferCopyRegions.size()),
				bufferCopyRegions.data()
			);

			// Change texture image layout to shader read after all faces have been copied
			(*texture)->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			SetImageLayout(
				copyCmd,
				(*texture)->image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				(*texture)->imageLayout,
				subresourceRange);

			FlushCommandBuffer(copyCmd, m_GraphicsQueue, true);

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
			imageViewCreateInfo.format = imageFormat;
			imageViewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
			imageViewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			// 6 array layers (faces)
			imageViewCreateInfo.subresourceRange.layerCount = 6;
			// Set number of mip levels
			imageViewCreateInfo.subresourceRange.levelCount = mipLevels;
			imageViewCreateInfo.image = (*texture)->image;
			VK_CHECK_RESULT(vkCreateImageView(m_VulkanDevice->m_LogicalDevice, &imageViewCreateInfo, nullptr, &(*texture)->imageView));

			// Clean up staging resources
			vkFreeMemory(m_VulkanDevice->m_LogicalDevice, stagingMemory, nullptr);
			vkDestroyBuffer(m_VulkanDevice->m_LogicalDevice, stagingBuffer, nullptr);

			(*texture)->filePath = filePaths[0];
			(*texture)->width = textureWidth;
			(*texture)->height = textureHeight;
		}

		void VulkanRenderer::CreateTextureImage(const std::string& filePath, VulkanTexture** texture)
		{
			int textureWidth, textureHeight, textureChannels;
			unsigned char* pixels = SOIL_load_image(filePath.c_str(), &textureWidth, &textureHeight, &textureChannels, SOIL_LOAD_RGBA);

			if (!pixels)
			{
				Logger::LogError("SOIL loading error: " + std::string(SOIL_last_result()) + ", image filepath: " + filePath);
				return;
			}

			*texture = new VulkanTexture(m_VulkanDevice->m_LogicalDevice);

			VkDeviceSize imageSize = (VkDeviceSize)(textureWidth * textureHeight * 4);

			Buffer stagingBuffer(m_VulkanDevice->m_LogicalDevice);

			CreateAndAllocateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer);

			void* data;
			vkMapMemory(m_VulkanDevice->m_LogicalDevice, stagingBuffer.m_Memory, 0, imageSize, 0, &data);
			memcpy(data, pixels, (size_t)imageSize);
			vkUnmapMemory(m_VulkanDevice->m_LogicalDevice, stagingBuffer.m_Memory);

			SOIL_free_image_data(pixels);

			CreateImage((glm::uint32)textureWidth, (glm::uint32)textureHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_PREINITIALIZED,
				(*texture)->image.replace(), (*texture)->imageMemory.replace());

			TransitionImageLayout((*texture)->image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
			CopyBufferToImage(stagingBuffer.m_Buffer, (*texture)->image, (glm::uint32)textureWidth, (glm::uint32)textureHeight);
			TransitionImageLayout((*texture)->image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}

		void VulkanRenderer::RebuildCommandBuffers()
		{
			if (!CheckCommandBuffers())
			{
				DestroyCommandBuffers();
				CreateCommandBuffers();
			}
			BuildCommandBuffers();
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

		VkCommandBuffer VulkanRenderer::CreateCommandBuffer(VkCommandBufferLevel level, bool begin)
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

		void VulkanRenderer::BuildCommandBuffers()
		{
			std::array<VkClearValue, 2> clearValues = {};
			clearValues[0].color = m_ClearColor;
			clearValues[1].depthStencil = { 1.0f, 0 };

			VkRenderPassBeginInfo renderPassBeginInfo = {};
			renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassBeginInfo.renderPass = m_RenderPass;
			renderPassBeginInfo.renderArea.offset = { 0, 0 };
			renderPassBeginInfo.renderArea.extent = m_SwapChainExtent;
			renderPassBeginInfo.clearValueCount = clearValues.size();
			renderPassBeginInfo.pClearValues = clearValues.data();


			for (size_t i = 0; i < m_CommandBuffers.size(); ++i)
			{
				renderPassBeginInfo.framebuffer = m_SwapChainFramebuffers[i];

				VkCommandBufferBeginInfo cmdBufferbeginInfo = {};
				cmdBufferbeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				cmdBufferbeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

				VK_CHECK_RESULT(vkBeginCommandBuffer(m_CommandBuffers[i], &cmdBufferbeginInfo));

				vkCmdBeginRenderPass(m_CommandBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport = VkViewport{ 0.0f, 1.0f, (float)m_SwapChainExtent.width, (float)m_SwapChainExtent.height, 0.1f, 1000.0f };
				vkCmdSetViewport(m_CommandBuffers[i], 0, 1, &viewport);

				VkRect2D scissor = VkRect2D{ { 0u, 0u }, { m_SwapChainExtent.width, m_SwapChainExtent.height } };
				vkCmdSetScissor(m_CommandBuffers[i], 0, 1, &scissor);

				for (size_t j = 0; j < m_RenderObjects.size(); ++j)
				{
					RenderObject* renderObject = GetRenderObject(j);
					if (!renderObject) continue;

					uint32_t dynamicOffset = j * static_cast<uint32_t>(m_DynamicAlignment);

					Material* material = &m_LoadedMaterials[renderObject->materialID];
					VkDeviceSize offsets[1] = { 0 };
					vkCmdBindVertexBuffers(m_CommandBuffers[i], 0, 1, &m_VertexIndexBufferPairs[material->shaderIndex].vertexBuffer->m_Buffer, offsets);

					if (m_VertexIndexBufferPairs[material->shaderIndex].indexBuffer->m_Size != 0)
					{
						vkCmdBindIndexBuffer(m_CommandBuffers[i], m_VertexIndexBufferPairs[material->shaderIndex].indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
					}

					vkCmdBindPipeline(m_CommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, renderObject->graphicsPipeline);

					vkCmdBindDescriptorSets(m_CommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, renderObject->pipelineLayout,
						0, 1, &renderObject->descriptorSet,
						1, &dynamicOffset);

					if (renderObject->indexed)
					{
						vkCmdDrawIndexed(m_CommandBuffers[i], renderObject->indices->size(), 1, renderObject->indexOffset, 0, 0);
					}
					else
					{
						vkCmdDraw(m_CommandBuffers[i], renderObject->vertexBufferData->VertexCount, 1, renderObject->vertexOffset, 0);
					}
				}

				ImGui_DrawFrame(m_CommandBuffers[i]);

				vkCmdEndRenderPass(m_CommandBuffers[i]);

				VK_CHECK_RESULT(vkEndCommandBuffer(m_CommandBuffers[i]));
			}
		}

		void VulkanRenderer::ImGui_UpdateBuffers()
		{
			ImDrawData* imDrawData = ImGui::GetDrawData();

			// Note: Alignment is done inside buffer creation
			VkDeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
			VkDeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

			// Update buffers only if vertex or index count has been changed compared to current buffer size

			glm::uint index = 2;
			VertexIndexBufferPair& bufferPair = m_VertexIndexBufferPairs[index];

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

		bool VulkanRenderer::CheckCommandBuffers()
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

		void VulkanRenderer::FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool free)
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

		uint32_t VulkanRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
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

			throw std::runtime_error("failed to find any suitable memory type!");
		}

		void VulkanRenderer::TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout)
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
			barrier.subresourceRange.levelCount = 1;
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

		void VulkanRenderer::CopyImage(VkImage srcImage, VkImage dstImage, uint32_t width, uint32_t height)
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

		void VulkanRenderer::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
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

		void VulkanRenderer::CreateAndAllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, Buffer* buffer)
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

		void VulkanRenderer::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset)
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

					for (size_t j = 0; j < m_RenderObjects.size(); ++j)
					{
						RenderObject* renderObject = GetRenderObject(j);
						if (renderObject && m_LoadedMaterials[renderObject->materialID].shaderIndex == i)
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

		glm::uint VulkanRenderer::CreateStaticVertexBuffer(Buffer* vertexBuffer, glm::uint shaderIndex, int size)
		{
			void* vertexDataStart = malloc(size);
			if (!vertexDataStart)
			{
				Logger::LogError("Failed to allocate memory for vertex buffer " + std::to_string(shaderIndex) + "! Attempted to allocate " + std::to_string(size) + " bytes");
				return 0;
			}

			void* vertexBufferData = vertexDataStart;

			glm::uint vertexCount = 0;
			glm::uint vertexBufferSize = 0;
			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				RenderObject* renderObject = GetRenderObject(i);
				if (renderObject && m_LoadedMaterials[renderObject->materialID].shaderIndex == shaderIndex)
				{
					renderObject->vertexOffset = vertexCount;

					memcpy(vertexBufferData, renderObject->vertexBufferData->pDataStart, renderObject->vertexBufferData->BufferSize);

					vertexCount += renderObject->vertexBufferData->VertexCount;
					vertexBufferSize += renderObject->vertexBufferData->BufferSize;

					vertexBufferData = (char*)vertexBufferData + renderObject->vertexBufferData->BufferSize;
				}
			}

			Logger::Assert(vertexBufferSize != 0);
			Logger::Assert(vertexCount != 0);

			Buffer stagingBuffer(m_VulkanDevice->m_LogicalDevice);
			CreateAndAllocateBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
				VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer);

			stagingBuffer.Map(vertexBufferSize);
			memcpy(stagingBuffer.m_Mapped, vertexDataStart, vertexBufferSize);
			stagingBuffer.Unmap();

			free(vertexDataStart);

			CreateAndAllocateBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer);

			CopyBuffer(stagingBuffer.m_Buffer, vertexBuffer->m_Buffer, vertexBufferSize);

			return vertexCount;
		}

		void VulkanRenderer::CreateStaticIndexBuffers()
		{
			for (size_t i = 0; i < m_VertexIndexBufferPairs.size(); ++i)
			{
				m_VertexIndexBufferPairs[i].indexCount = CreateStaticIndexBuffer(m_VertexIndexBufferPairs[i].indexBuffer, i);
			}
		}

		glm::uint VulkanRenderer::CreateStaticIndexBuffer(Buffer* indexBuffer, glm::uint shaderIndex)
		{
			std::vector<glm::uint> indices;

			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				RenderObject* renderObject = GetRenderObject(i);
				if (renderObject && m_LoadedMaterials[renderObject->materialID].shaderIndex == shaderIndex && renderObject->indexed)
				{
					renderObject->indexOffset = indices.size();
					indices.insert(indices.end(), renderObject->indices->begin(), renderObject->indices->end());
				}
			}

			if (indices.empty()) return 0;
			const size_t bufferSize = sizeof(indices[0]) * indices.size();

			Buffer stagingBuffer(m_VulkanDevice->m_LogicalDevice);
			CreateAndAllocateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer);

			stagingBuffer.Map(bufferSize);
			memcpy(stagingBuffer.m_Mapped, indices.data(), bufferSize);
			stagingBuffer.Unmap();

			CreateAndAllocateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer);

			CopyBuffer(stagingBuffer.m_Buffer, indexBuffer->m_Buffer, bufferSize);

			return indices.size();
		}

		void VulkanRenderer::PrepareUniformBuffers()
		{
			if (!m_UniformBuffers.empty())
			{
				ReleaseUniformBuffers();
			}

			m_UniformBuffers.resize(m_LoadedShaderCode.size(), { m_VulkanDevice->m_LogicalDevice });

			glm::uint shaderIndex = 0;

			// Simple
			m_UniformBuffers[shaderIndex].constantData.elements = Uniform::Type(
				Uniform::Type::UNIFORM_BUFFER_CONSTANT |
				Uniform::Type::PROJECTION_MAT4 |
				Uniform::Type::VIEW_MAT4 |
				Uniform::Type::CAM_POS_VEC4 |
				Uniform::Type::LIGHT_DIR_VEC4 |
				Uniform::Type::AMBIENT_COLOR_VEC4 |
				Uniform::Type::SPECULAR_COLOR_VEC4);

			m_UniformBuffers[shaderIndex].dynamicData.elements = Uniform::Type(
				Uniform::Type::UNIFORM_BUFFER_DYNAMIC |
				Uniform::Type::MODEL_MAT4 |
				Uniform::Type::MODEL_INV_TRANSPOSE_MAT4 |
				Uniform::Type::USE_DIFFUSE_TEXTURE_INT |
				Uniform::Type::DIFFUSE_TEXTURE_SAMPLER |
				Uniform::Type::USE_NORMAL_TEXTURE_INT |
				Uniform::Type::NORMAL_TEXTURE_SAMPLER |
				Uniform::Type::USE_SPECULAR_TEXTURE_INT|
				Uniform::Type::SPECULAR_TEXTURE_SAMPLER);
			++shaderIndex;

			// Color
			m_UniformBuffers[shaderIndex].constantData.elements = Uniform::Type(
				Uniform::Type::UNIFORM_BUFFER_CONSTANT |
				Uniform::Type::VIEW_PROJECTION_MAT4);

			m_UniformBuffers[shaderIndex].dynamicData.elements = Uniform::Type(
				Uniform::Type::UNIFORM_BUFFER_DYNAMIC |
				Uniform::Type::MODEL_MAT4);
			++shaderIndex;

			// ImGui
			m_UniformBuffers[shaderIndex].constantData.elements = Uniform::Type::NONE;

			m_UniformBuffers[shaderIndex].dynamicData.elements = Uniform::Type(
				Uniform::Type::DIFFUSE_TEXTURE_SAMPLER
				);
			++shaderIndex;

			// Skybox
			m_UniformBuffers[shaderIndex].constantData.elements = Uniform::Type(
				Uniform::Type::UNIFORM_BUFFER_CONSTANT |
				Uniform::Type::VIEW_MAT4 |
				Uniform::Type::PROJECTION_MAT4);

			m_UniformBuffers[shaderIndex].dynamicData.elements = Uniform::Type(
				Uniform::Type::UNIFORM_BUFFER_DYNAMIC |
				Uniform::Type::MODEL_MAT4 |
				Uniform::Type::USE_CUBEMAP_TEXTURE_INT |
				Uniform::Type::CUBEMAP_TEXTURE_SAMPLER);
			++shaderIndex;

			for (size_t i = 0; i < m_UniformBuffers.size(); ++i)
			{
				m_UniformBuffers[i].constantData.size = Uniform::CalculateSize(m_UniformBuffers[i].constantData.elements);
				if (m_UniformBuffers[i].constantData.size > 0)
				{
					m_UniformBuffers[i].constantData.data = (float*)malloc(m_UniformBuffers[i].constantData.size);
					assert(m_UniformBuffers[i].constantData.data);

					PrepareUniformBuffer(&m_UniformBuffers[i].constantBuffer, m_UniformBuffers[i].constantData.size,
						VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				}

				m_UniformBuffers[i].dynamicData.size = Uniform::CalculateSize(m_UniformBuffers[i].dynamicData.elements);
				if (m_UniformBuffers[i].dynamicData.size > 0 && m_RenderObjects.size() > 0)
				{
					const size_t dynamicBufferSize = AllocateUniformBuffer(
						m_UniformBuffers[i].dynamicData.size * m_RenderObjects.size(), (void**)&m_UniformBuffers[i].dynamicData.data);
					if (dynamicBufferSize > 0)
					{
						PrepareUniformBuffer(&m_UniformBuffers[i].dynamicBuffer, dynamicBufferSize,
							VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
					}
				}
			}
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

		void VulkanRenderer::PrepareUniformBuffer(Buffer* buffer, glm::uint bufferSize,
			VkBufferUsageFlags bufferUseageFlagBits, VkMemoryPropertyFlags memoryPropertyHostFlagBits)
		{
			CreateAndAllocateBuffer(bufferSize, bufferUseageFlagBits, memoryPropertyHostFlagBits, buffer);

			VK_CHECK_RESULT(vkMapMemory(m_VulkanDevice->m_LogicalDevice, buffer->m_Memory, 0, VK_WHOLE_SIZE, 0, &buffer->m_Mapped));
		}

		void VulkanRenderer::CreateDescriptorPool()
		{
			const size_t descriptorSetCount = 1000; // m_RenderObjects.size();

			std::vector<VkDescriptorPoolSize> poolSizes {
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

			//poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			//poolSizes[1].descriptorCount = descriptorSetCount;

			//poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // Diffuse map
			//poolSizes[2].descriptorCount = descriptorSetCount;

			//poolSizes[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // Normal map
			//poolSizes[3].descriptorCount = descriptorSetCount;

			//poolSizes[4].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // Specular map
			//poolSizes[4].descriptorCount = descriptorSetCount;

			//poolSizes[5].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // Cubemap
			//poolSizes[5].descriptorCount = descriptorSetCount;

			VkDescriptorPoolCreateInfo poolInfo = {};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = poolSizes.size();
			poolInfo.pPoolSizes = poolSizes.data();
			poolInfo.maxSets = descriptorSetCount * 11;
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // Allow descriptor sets to be added/removed often

			VK_CHECK_RESULT(vkCreateDescriptorPool(m_VulkanDevice->m_LogicalDevice, &poolInfo, nullptr, m_DescriptorPool.replace()));
		}

		void VulkanRenderer::CreateDescriptorSet(RenderID renderID)
		{
			RenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject) return;

			Material* material = &m_LoadedMaterials[renderObject->materialID];

			DescriptorSetCreateInfo createInfo = {};
			createInfo.descriptorSet = &renderObject->descriptorSet;
			createInfo.descriptorSetLayoutIndex = material->descriptorSetLayoutIndex;
			createInfo.uniformBufferIndex = material->shaderIndex;
			createInfo.diffuseTexture = m_LoadedMaterials[renderObject->materialID].diffuseTexture;
			createInfo.normalTexture = m_LoadedMaterials[renderObject->materialID].normalTexture;
			createInfo.specularTexture = m_LoadedMaterials[renderObject->materialID].specularTexture;
			createInfo.cubemapTexture = m_LoadedMaterials[renderObject->materialID].cubemapTexture;

			CreateDescriptorSet(&createInfo);
		}

		void VulkanRenderer::CreateDescriptorSet(DescriptorSetCreateInfo* createInfo)
		{
			VkDescriptorSetLayout layouts[] = { m_DescriptorSetLayouts[createInfo->descriptorSetLayoutIndex] };
			VkDescriptorSetAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = m_DescriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = layouts;

			VK_CHECK_RESULT(vkAllocateDescriptorSets(m_VulkanDevice->m_LogicalDevice, &allocInfo, createInfo->descriptorSet));


			std::vector<VkWriteDescriptorSet> writeDescriptorSets;

			Uniform::Type constantBufferElements = m_UniformBuffers[createInfo->uniformBufferIndex].constantData.elements;
			Uniform::Type dynamicBufferElements = m_UniformBuffers[createInfo->uniformBufferIndex].dynamicData.elements;

			glm::uint descriptorSetIndex = 0;
			glm::uint binding = 0;

			if (Uniform::HasUniform(constantBufferElements, Uniform::Type::UNIFORM_BUFFER_CONSTANT) ||
				Uniform::HasUniform(dynamicBufferElements, Uniform::Type::UNIFORM_BUFFER_CONSTANT))
			{
				VkDescriptorBufferInfo uniformBufferInfo = {};
				uniformBufferInfo.buffer = m_UniformBuffers[createInfo->uniformBufferIndex].constantBuffer.m_Buffer;
				uniformBufferInfo.range = sizeof(VulkanUniformBufferObjectData);

				writeDescriptorSets.push_back({});
				writeDescriptorSets[descriptorSetIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[descriptorSetIndex].dstSet = *createInfo->descriptorSet;
				writeDescriptorSets[descriptorSetIndex].dstBinding = binding;
				writeDescriptorSets[descriptorSetIndex].dstArrayElement = 0;
				writeDescriptorSets[descriptorSetIndex].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				writeDescriptorSets[descriptorSetIndex].descriptorCount = 1;
				writeDescriptorSets[descriptorSetIndex].pBufferInfo = &uniformBufferInfo;
				++descriptorSetIndex;
				++binding;
			}

			if (Uniform::HasUniform(constantBufferElements, Uniform::Type::UNIFORM_BUFFER_DYNAMIC) ||
				Uniform::HasUniform(dynamicBufferElements, Uniform::Type::UNIFORM_BUFFER_DYNAMIC))
			{
				VkDescriptorBufferInfo uniformBufferDynamicInfo = {};
				uniformBufferDynamicInfo.buffer = m_UniformBuffers[createInfo->uniformBufferIndex].dynamicBuffer.m_Buffer;
				uniformBufferDynamicInfo.range = sizeof(VulkanUniformBufferObjectData) * m_RenderObjects.size();

				writeDescriptorSets.push_back({});
				writeDescriptorSets[descriptorSetIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[descriptorSetIndex].dstSet = *createInfo->descriptorSet;
				writeDescriptorSets[descriptorSetIndex].dstBinding = binding;
				writeDescriptorSets[descriptorSetIndex].dstArrayElement = 0;
				writeDescriptorSets[descriptorSetIndex].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
				writeDescriptorSets[descriptorSetIndex].descriptorCount = 1;
				writeDescriptorSets[descriptorSetIndex].pBufferInfo = &uniformBufferDynamicInfo;
				++descriptorSetIndex;
				++binding;
			}

			if (Uniform::HasUniform(constantBufferElements, Uniform::Type::DIFFUSE_TEXTURE_SAMPLER) ||
				Uniform::HasUniform(dynamicBufferElements, Uniform::Type::DIFFUSE_TEXTURE_SAMPLER))
			{
				VkDescriptorImageInfo diffuseImageInfo = {};
				diffuseImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				if (createInfo->diffuseTexture)
				{
					diffuseImageInfo.imageView = createInfo->diffuseTexture->imageView;
					diffuseImageInfo.sampler = createInfo->diffuseTexture->sampler;
				}
				else
				{
					diffuseImageInfo.imageView = m_BlankTexture->imageView;
					diffuseImageInfo.sampler = m_BlankTexture->sampler;
				}

				writeDescriptorSets.push_back({});
				writeDescriptorSets[descriptorSetIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[descriptorSetIndex].dstSet = *createInfo->descriptorSet;
				writeDescriptorSets[descriptorSetIndex].dstBinding = binding;
				writeDescriptorSets[descriptorSetIndex].dstArrayElement = 0;
				writeDescriptorSets[descriptorSetIndex].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writeDescriptorSets[descriptorSetIndex].descriptorCount = 1;
				writeDescriptorSets[descriptorSetIndex].pImageInfo = &diffuseImageInfo;
				++descriptorSetIndex;
				++binding;
			}

			if (Uniform::HasUniform(constantBufferElements, Uniform::Type::NORMAL_TEXTURE_SAMPLER) ||
				Uniform::HasUniform(dynamicBufferElements, Uniform::Type::NORMAL_TEXTURE_SAMPLER))
			{
				VkDescriptorImageInfo normalImageInfo = {};
				normalImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				if (createInfo->normalTexture)
				{
					normalImageInfo.imageView = createInfo->normalTexture->imageView;
					normalImageInfo.sampler = createInfo->normalTexture->sampler;
				}
				else
				{
					normalImageInfo.imageView = m_BlankTexture->imageView;
					normalImageInfo.sampler = m_BlankTexture->sampler;
				}

				writeDescriptorSets.push_back({});
				writeDescriptorSets[descriptorSetIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[descriptorSetIndex].dstSet = *createInfo->descriptorSet;
				writeDescriptorSets[descriptorSetIndex].dstBinding = binding;
				writeDescriptorSets[descriptorSetIndex].dstArrayElement = 0;
				writeDescriptorSets[descriptorSetIndex].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writeDescriptorSets[descriptorSetIndex].descriptorCount = 1;
				writeDescriptorSets[descriptorSetIndex].pImageInfo = &normalImageInfo;
				++descriptorSetIndex;
				++binding;
			}

			if (Uniform::HasUniform(constantBufferElements, Uniform::Type::SPECULAR_TEXTURE_SAMPLER) ||
				Uniform::HasUniform(dynamicBufferElements, Uniform::Type::SPECULAR_TEXTURE_SAMPLER))
			{
				VkDescriptorImageInfo specularImageInfo = {};
				specularImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				if (createInfo->specularTexture)
				{
					specularImageInfo.imageView = createInfo->specularTexture->imageView;
					specularImageInfo.sampler = createInfo->specularTexture->sampler;
				}
				else
				{
					specularImageInfo.imageView = m_BlankTexture->imageView;
					specularImageInfo.sampler = m_BlankTexture->sampler;
				}

				writeDescriptorSets.push_back({});
				writeDescriptorSets[descriptorSetIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[descriptorSetIndex].dstSet = *createInfo->descriptorSet;
				writeDescriptorSets[descriptorSetIndex].dstBinding = binding;
				writeDescriptorSets[descriptorSetIndex].dstArrayElement = 0;
				writeDescriptorSets[descriptorSetIndex].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writeDescriptorSets[descriptorSetIndex].descriptorCount = 1;
				writeDescriptorSets[descriptorSetIndex].pImageInfo = &specularImageInfo;
				++descriptorSetIndex;
				++binding;
			}

			if (Uniform::HasUniform(constantBufferElements, Uniform::Type::CUBEMAP_TEXTURE_SAMPLER) ||
				Uniform::HasUniform(dynamicBufferElements, Uniform::Type::CUBEMAP_TEXTURE_SAMPLER))
			{
				VkDescriptorImageInfo cubemapImageInfo = {};
				cubemapImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				if (createInfo->cubemapTexture)
				{
					cubemapImageInfo.imageView = createInfo->cubemapTexture->imageView;
					cubemapImageInfo.sampler = createInfo->cubemapTexture->sampler;
				}
				else
				{
					cubemapImageInfo.imageView = m_BlankTexture->imageView;
					cubemapImageInfo.sampler = m_BlankTexture->sampler;
				}

				writeDescriptorSets.push_back({});
				writeDescriptorSets[descriptorSetIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[descriptorSetIndex].dstSet = *createInfo->descriptorSet;
				writeDescriptorSets[descriptorSetIndex].dstBinding = binding;
				writeDescriptorSets[descriptorSetIndex].dstArrayElement = 0;
				writeDescriptorSets[descriptorSetIndex].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writeDescriptorSets[descriptorSetIndex].descriptorCount = 1;
				writeDescriptorSets[descriptorSetIndex].pImageInfo = &cubemapImageInfo;
				++descriptorSetIndex;
				++binding;
			}

			if (!writeDescriptorSets.empty())
			{
				vkUpdateDescriptorSets(m_VulkanDevice->m_LogicalDevice, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, nullptr);
			}
		}

		void VulkanRenderer::ReleaseUniformBuffers()
		{
			for (size_t i = 0; i < m_UniformBuffers.size(); ++i)
			{
				free(m_UniformBuffers[i].constantData.data);

				if (m_UniformBuffers[i].dynamicData.data)
				{
					_aligned_free(m_UniformBuffers[i].dynamicData.data);
				}
			}
			m_UniformBuffers.clear();
		}

		void VulkanRenderer::CreateDescriptorSetLayout(glm::uint shaderIndex)
		{
			m_DescriptorSetLayouts.push_back(VkDescriptorSetLayout());
			VkDescriptorSetLayout* descriptorSetLayout = &m_DescriptorSetLayouts.back();
	
			UniformBuffer* uniformBuffer = &m_UniformBuffers[shaderIndex];
			std::vector<VkDescriptorSetLayoutBinding> bindings;
			glm::uint binding = 0;
			if (Uniform::HasUniform(uniformBuffer->constantData.elements, Uniform::Type::UNIFORM_BUFFER_CONSTANT) ||
				Uniform::HasUniform(uniformBuffer->dynamicData.elements, Uniform::Type::UNIFORM_BUFFER_CONSTANT))
			{
				VkDescriptorSetLayoutBinding uniformBufferBinding = {};
				uniformBufferBinding.binding = binding;
				uniformBufferBinding.descriptorCount = 1;
				uniformBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				uniformBufferBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
				bindings.push_back(uniformBufferBinding);
				++binding;
			}

			if (Uniform::HasUniform(uniformBuffer->constantData.elements, Uniform::Type::UNIFORM_BUFFER_DYNAMIC) ||
				Uniform::HasUniform(uniformBuffer->dynamicData.elements, Uniform::Type::UNIFORM_BUFFER_DYNAMIC))
			{
				VkDescriptorSetLayoutBinding uniformBufferDynamicBinding = {};
				uniformBufferDynamicBinding.binding = binding;
				uniformBufferDynamicBinding.descriptorCount = 1;
				uniformBufferDynamicBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
				uniformBufferDynamicBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
				bindings.push_back(uniformBufferDynamicBinding);
				++binding;
			}

			if (Uniform::HasUniform(uniformBuffer->constantData.elements, Uniform::Type::DIFFUSE_TEXTURE_SAMPLER) ||
				Uniform::HasUniform(uniformBuffer->dynamicData.elements, Uniform::Type::DIFFUSE_TEXTURE_SAMPLER))
			{
				VkDescriptorSetLayoutBinding diffuseSamplerLayoutBinding = {};
				diffuseSamplerLayoutBinding.binding = binding;
				diffuseSamplerLayoutBinding.descriptorCount = 1;
				diffuseSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				diffuseSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
				bindings.push_back(diffuseSamplerLayoutBinding);
				++binding;
			}

			if (Uniform::HasUniform(uniformBuffer->constantData.elements, Uniform::Type::NORMAL_TEXTURE_SAMPLER) ||
				Uniform::HasUniform(uniformBuffer->dynamicData.elements, Uniform::Type::NORMAL_TEXTURE_SAMPLER))
			{
				VkDescriptorSetLayoutBinding normalSamplerLayoutBinding = {};
				normalSamplerLayoutBinding.binding = binding;
				normalSamplerLayoutBinding.descriptorCount = 1;
				normalSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				normalSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
				bindings.push_back(normalSamplerLayoutBinding);
				++binding;
			}

			if (Uniform::HasUniform(uniformBuffer->constantData.elements, Uniform::Type::SPECULAR_TEXTURE_SAMPLER) ||
				Uniform::HasUniform(uniformBuffer->dynamicData.elements, Uniform::Type::SPECULAR_TEXTURE_SAMPLER))
			{
				VkDescriptorSetLayoutBinding specularSamplerLayoutBinding = {};
				specularSamplerLayoutBinding.binding = binding;
				specularSamplerLayoutBinding.descriptorCount = 1;
				specularSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				specularSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
				bindings.push_back(specularSamplerLayoutBinding);
				++binding;
			}

			if (Uniform::HasUniform(uniformBuffer->constantData.elements, Uniform::Type::CUBEMAP_TEXTURE_SAMPLER) ||
				Uniform::HasUniform(uniformBuffer->dynamicData.elements, Uniform::Type::CUBEMAP_TEXTURE_SAMPLER))
			{
				VkDescriptorSetLayoutBinding cubemapSamplerLayoutBinding = {};
				cubemapSamplerLayoutBinding.binding = binding;
				cubemapSamplerLayoutBinding.descriptorCount = 1;
				cubemapSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				cubemapSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
				bindings.push_back(cubemapSamplerLayoutBinding);
				++binding;
			}

			VkDescriptorSetLayoutCreateInfo layoutInfo = {};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = bindings.size();
			layoutInfo.pBindings = bindings.data();

			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_VulkanDevice->m_LogicalDevice, &layoutInfo, nullptr, descriptorSetLayout));
		}

		void VulkanRenderer::CreateSemaphores()
		{
			VkSemaphoreCreateInfo semaphoreInfo = {};
			semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

			VK_CHECK_RESULT(vkCreateSemaphore(m_VulkanDevice->m_LogicalDevice, &semaphoreInfo, nullptr, m_ImageAvailableSemaphore.replace()));
			VK_CHECK_RESULT(vkCreateSemaphore(m_VulkanDevice->m_LogicalDevice, &semaphoreInfo, nullptr, m_RenderFinishedSemaphore.replace()));
		}

		void VulkanRenderer::DrawFrame(Window* window)
		{
			uint32_t imageIndex;
			VkResult result = vkAcquireNextImageKHR(m_VulkanDevice->m_LogicalDevice, m_SwapChain, std::numeric_limits<uint64_t>::max(), m_ImageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

			if (result == VK_ERROR_OUT_OF_DATE_KHR)
			{
				RecreateSwapChain(window);
				return;
			}
			else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
			{
				throw std::runtime_error("failed to acquire swap chain image!");
			}

			VkSubmitInfo submitInfo = {};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

			VkSemaphore waitSemaphores[] = { m_ImageAvailableSemaphore };
			VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
			submitInfo.waitSemaphoreCount = 1;
			submitInfo.pWaitSemaphores = waitSemaphores;
			submitInfo.pWaitDstStageMask = waitStages;

			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &m_CommandBuffers[imageIndex];

			VkSemaphore signalSemaphores[] = { m_RenderFinishedSemaphore };
			submitInfo.signalSemaphoreCount = 1;
			submitInfo.pSignalSemaphores = signalSemaphores;

			VK_CHECK_RESULT(vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));

			VkPresentInfoKHR presentInfo = {};
			presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

			presentInfo.waitSemaphoreCount = 1;
			presentInfo.pWaitSemaphores = signalSemaphores;

			VkSwapchainKHR swapChains[] = { m_SwapChain };
			presentInfo.swapchainCount = 1;
			presentInfo.pSwapchains = swapChains;

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

		void VulkanRenderer::CreateShaderModule(const std::vector<char>& code, VDeleter<VkShaderModule>& shaderModule)
		{
			VkShaderModuleCreateInfo createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			createInfo.codeSize = code.size();
			createInfo.pCode = (uint32_t*)code.data();

			VK_CHECK_RESULT(vkCreateShaderModule(m_VulkanDevice->m_LogicalDevice, &createInfo, nullptr, shaderModule.replace()));
		}

		VkSurfaceFormatKHR VulkanRenderer::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
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

		VkPresentModeKHR VulkanRenderer::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR> availablePresentModes)
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

		VkExtent2D VulkanRenderer::ChooseSwapExtent(Window* window, const VkSurfaceCapabilitiesKHR& capabilities)
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

		VulkanSwapChainSupportDetails VulkanRenderer::QuerySwapChainSupport(VkPhysicalDevice device)
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

		bool VulkanRenderer::IsDeviceSuitable(VkPhysicalDevice device)
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

		bool VulkanRenderer::CheckDeviceExtensionSupport(VkPhysicalDevice device)
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

		VulkanQueueFamilyIndices VulkanRenderer::FindQueueFamilies(VkPhysicalDevice device)
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

		std::vector<const char*> VulkanRenderer::GetRequiredExtensions()
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

		bool VulkanRenderer::CheckValidationLayerSupport()
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

		void VulkanRenderer::UpdateConstantUniformBuffers(const GameContext& gameContext)
		{
			glm::mat4 proj = gameContext.camera->GetProjection();
			glm::mat4 view = gameContext.camera->GetView();
			glm::mat4 viewInv = glm::inverse(view);
			glm::mat4 viewProj = proj * view;
			glm::vec4 camPos = glm::vec4(gameContext.camera->GetPosition(), 0.0f);
			glm::vec4 viewDir = glm::vec4(gameContext.camera->GetViewDirection(), 0.0f);

			for (size_t i = 0; i < m_UniformBuffers.size(); ++i)
			{
				glm::uint index = 0;

				if (Uniform::HasUniform(m_UniformBuffers[i].constantData.elements, Uniform::Type::PROJECTION_MAT4))
				{
					memcpy(&m_UniformBuffers[i].constantData.data[index], &proj[0][0], sizeof(glm::mat4));
					index += 16;
				}

				if (Uniform::HasUniform(m_UniformBuffers[i].constantData.elements, Uniform::Type::VIEW_MAT4))
				{
					memcpy(&m_UniformBuffers[i].constantData.data[index], &view[0][0], sizeof(glm::mat4));
					index += 16;
				}

				if (Uniform::HasUniform(m_UniformBuffers[i].constantData.elements, Uniform::Type::VIEW_INV_MAT4))
				{
					memcpy(&m_UniformBuffers[i].constantData.data[index], &viewInv[0], sizeof(glm::mat4));
					index += 16;
				}

				if (Uniform::HasUniform(m_UniformBuffers[i].constantData.elements, Uniform::Type::VIEW_PROJECTION_MAT4))
				{
					memcpy(&m_UniformBuffers[i].constantData.data[index], &viewProj[0], sizeof(glm::mat4));
					index += 16;
				}

				if (Uniform::HasUniform(m_UniformBuffers[i].constantData.elements, Uniform::Type::MODEL_MAT4))
				{
					Logger::LogError("Constant uniform buffer contains model matrix, which should be in the dynamic uniform buffer");
				}

				if (Uniform::HasUniform(m_UniformBuffers[i].constantData.elements, Uniform::Type::MODEL_INV_TRANSPOSE_MAT4))
				{
					Logger::LogError("Constant uniform buffer contains modelInvTranspose matrix, which should be in the dynamic uniform buffer");
				}

				if (Uniform::HasUniform(m_UniformBuffers[i].constantData.elements, Uniform::Type::MODEL_VIEW_PROJECTION_MAT4))
				{
					Logger::LogError("Constant uniform buffer contains MVP matrix, which should be in the dynamic uniform buffer");
				}

				if (Uniform::HasUniform(m_UniformBuffers[i].constantData.elements, Uniform::Type::CAM_POS_VEC4))
				{
					memcpy(&m_UniformBuffers[i].constantData.data[index], &camPos[0], sizeof(glm::vec4));
					index += 4;
				}

				if (Uniform::HasUniform(m_UniformBuffers[i].constantData.elements, Uniform::Type::VIEW_DIR_VEC4))
				{
					memcpy(&m_UniformBuffers[i].constantData.data[index], &viewDir[0], sizeof(glm::vec4));
					index += 4;
				}

				if (Uniform::HasUniform(m_UniformBuffers[i].constantData.elements, Uniform::Type::LIGHT_DIR_VEC4))
				{
					memcpy(&m_UniformBuffers[i].constantData.data[index], &m_SceneInfo.m_LightDir[0], sizeof(glm::vec4));
					index += 4;
				}

				if (Uniform::HasUniform(m_UniformBuffers[i].constantData.elements, Uniform::Type::AMBIENT_COLOR_VEC4))
				{
					memcpy(&m_UniformBuffers[i].constantData.data[index], &m_SceneInfo.m_AmbientColor[0], sizeof(glm::vec4));
					index += 4;
				}

				if (Uniform::HasUniform(m_UniformBuffers[i].constantData.elements, Uniform::Type::SPECULAR_COLOR_VEC4))
				{
					memcpy(&m_UniformBuffers[i].constantData.data[index], &m_SceneInfo.m_SpecularColor[0], sizeof(glm::vec4));
					index += 4;
				}

				//glm::uint calculatedSize = Uniform::CalculateSize(m_UniformBuffers[i].constantData.elements);
				glm::uint size = m_UniformBuffers[i].constantData.size;

				memcpy(m_UniformBuffers[i].constantBuffer.m_Mapped, m_UniformBuffers[i].constantData.data, size);
			}
		}

		void VulkanRenderer::UpdateUniformBufferDynamic(const GameContext& gameContext, RenderID renderID, const glm::mat4& model)
		{
			UNREFERENCED_PARAMETER(gameContext);

			RenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject) return;
			Material* material = &m_LoadedMaterials[renderObject->materialID];

			const glm::mat4 modelInvTranspose = glm::transpose(glm::inverse(model));

			glm::uint useDiffuseTexture = material->diffuseTexture == nullptr ? 0 : 1;
			glm::uint useNormalTexture = material->normalTexture == nullptr ? 0 : 1;
			glm::uint useSpecularTexture = material->specularTexture == nullptr ? 0 : 1;
			glm::uint useCubemapTexture = material->useCubemapTexture ? 1 : 0;

			const int uniformBufferIndex = material->shaderIndex;
			UniformBuffer& uniformBuffer = m_UniformBuffers[uniformBufferIndex];

			glm::uint offset = renderID * uniformBuffer.dynamicData.size;
			glm::uint index = 0;

			// TODO: Flatten into single loop over array
			if (Uniform::HasUniform(uniformBuffer.dynamicData.elements, Uniform::Type::MODEL_MAT4))
			{
				memcpy(&uniformBuffer.dynamicData.data[offset + index], &model, sizeof(model));
				index += 16;
			}

			if (Uniform::HasUniform(uniformBuffer.dynamicData.elements, Uniform::Type::MODEL_INV_TRANSPOSE_MAT4))
			{
				memcpy(&uniformBuffer.dynamicData.data[offset + index], &modelInvTranspose, sizeof(modelInvTranspose));
				index += 16;
			}

			if (Uniform::HasUniform(uniformBuffer.dynamicData.elements, Uniform::Type::USE_DIFFUSE_TEXTURE_INT))
			{
				memcpy(&uniformBuffer.dynamicData.data[offset + index], &useDiffuseTexture, sizeof(useDiffuseTexture));
				index += 1;
			}

			if (Uniform::HasUniform(uniformBuffer.dynamicData.elements, Uniform::Type::USE_NORMAL_TEXTURE_INT))
			{
				memcpy(&uniformBuffer.dynamicData.data[offset + index], &useNormalTexture, sizeof(useNormalTexture));
				index += 1;
			}

			if (Uniform::HasUniform(uniformBuffer.dynamicData.elements, Uniform::Type::USE_SPECULAR_TEXTURE_INT))
			{
				memcpy(&uniformBuffer.dynamicData.data[offset + index], &useSpecularTexture, sizeof(useSpecularTexture));
				index += 1;
			}

			if (Uniform::HasUniform(uniformBuffer.dynamicData.elements, Uniform::Type::USE_CUBEMAP_TEXTURE_INT))
			{
				memcpy(&uniformBuffer.dynamicData.data[offset + index], &useCubemapTexture, sizeof(useCubemapTexture));
				index += 1;
			}

			// Aligned offset
			glm::uint size = uniformBuffer.dynamicData.size;
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

			m_ShaderFilePaths = {
				{ shaderDirectory + "vk_simple_vert.spv", shaderDirectory + "vk_simple_frag.spv" },
				{ shaderDirectory + "vk_color_vert.spv", shaderDirectory + "vk_color_frag.spv" },
				{ shaderDirectory + "vk_imgui_vert.spv", shaderDirectory + "vk_imgui_frag.spv" },
				// NOTE: Skybox shader should be kept last to keep other objects rendering in front
				{ shaderDirectory + "vk_skybox_vert.spv", shaderDirectory + "vk_skybox_frag.spv" },
			};

			const size_t shaderCount = m_ShaderFilePaths.size();
			m_LoadedShaderCode.resize(shaderCount);
			for (size_t i = 0; i < shaderCount; ++i)
			{
				m_LoadedShaderCode[i].vertexShaderCode = ReadFile(m_ShaderFilePaths[i].vertexShaderFilePath);
				m_LoadedShaderCode[i].fragmentShaderCode = ReadFile(m_ShaderFilePaths[i].fragmentShaderFilePath);
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
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN