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
#include "VertexAttribute.h"
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
			m_DeferredCombineRenderPass = { m_VulkanDevice->m_LogicalDevice, vkDestroyRenderPass };
			m_DepthImage = { m_VulkanDevice->m_LogicalDevice, vkDestroyImage };
			m_DepthImageMemory = { m_VulkanDevice->m_LogicalDevice, vkFreeMemory };
			m_DepthImageView = { m_VulkanDevice->m_LogicalDevice, vkDestroyImageView };
			m_DescriptorPool = { m_VulkanDevice->m_LogicalDevice, vkDestroyDescriptorPool };
			m_PresentCompleteSemaphore = { m_VulkanDevice->m_LogicalDevice, vkDestroySemaphore };
			m_RenderCompleteSemaphore = { m_VulkanDevice->m_LogicalDevice, vkDestroySemaphore };
			offScreenFrameBuf = new FrameBuffer(m_VulkanDevice->m_LogicalDevice);

			CreateSwapChain(gameContext.window);
			CreateImageViews();
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

		void VulkanRenderer::PostInitialize(const GameContext& gameContext)
		{
			UNREFERENCED_PARAMETER(gameContext);

			PrepareUniformBuffers();

			CreateDescriptorPool();

			// Generate offscreen quad
			float x = -1.0f;
			float y = -1.0f;

			// TODO: Remove constants (at least use strings)
			const glm::uint deferredCombineShaderIndex = m_Shaders.size() - 1; // Deferred combine shader should be last one loaded
			const glm::uint offscreenShaderIndex = 0; // This shader outputs 3 frame buffers (GBuffer)

			VertexBufferData::CreateInfo offscreenQuadVertexBufferDataCreateInfo = {};
			offscreenQuadVertexBufferDataCreateInfo.positions_3D = {
				{ x + 2.0f, y + 2.0f, 0.0f },
				{ x, y + 2.0f, 0.0f },
				{ x, y, 0.0f },
				{ x + 2.0f, y, 0.0f },
			};
			offscreenQuadVertexBufferDataCreateInfo.texCoords_UV = {
				{ 1.0f, 1.0f },
				{ 0.0f, 1.0f },
				{ 0.0f, 0.0f },
				{ 1.0f, 0.0f },
			};
			offscreenQuadVertexBufferDataCreateInfo.attributes = (glm::uint)VertexAttribute::POSITION | (glm::uint)VertexAttribute::UV;

			VertexBufferData offscreenQuadVertexBufferData = {};
			offscreenQuadVertexBufferData.Initialize(&offscreenQuadVertexBufferDataCreateInfo);

			offscreenQuadVertexIndexBufferPair.vertexCount = offscreenQuadVertexBufferData.VertexCount;

			// Setup indices
			std::vector<glm::uint> indexBuffer = { 0, 1, 2,  2, 3, 0 };
			for (glm::uint i = 0; i < 3; ++i)
			{
				glm::uint indices[6] = { 0, 1, 2,  2, 3, 0 };
				for (auto index : indices)
				{
					indexBuffer.push_back(i * 4 + index);
				}
			}
			offscreenQuadVertexIndexBufferPair.indexCount = static_cast<uint32_t>(indexBuffer.size());

			offscreenQuadVertexIndexBufferPair.vertexBuffer = new Buffer(m_VulkanDevice->m_LogicalDevice);
			offscreenQuadVertexIndexBufferPair.indexBuffer = new Buffer(m_VulkanDevice->m_LogicalDevice);

			CreateStaticVertexBuffer(offscreenQuadVertexIndexBufferPair.vertexBuffer, offscreenQuadVertexBufferData.pDataStart, offscreenQuadVertexBufferData.BufferSize);

			CreateStaticIndexBuffer(offscreenQuadVertexIndexBufferPair.indexBuffer, indexBuffer);

			offscreenQuadVertexBufferData.Destroy();

			for (size_t i = 0; i < m_UniformBuffers.size(); ++i)
			{
				CreateDescriptorSetLayout(i);
			}

			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				CreateDescriptorSet(i);
				CreateGraphicsPipeline(i);
			}



			VertexBufferData deferredQuadVertexBufferData = {};
			deferredQuadVertexBufferData.Attributes = (glm::uint)VertexAttribute::POSITION | (glm::uint)VertexAttribute::UV;
			deferredQuadVertexBufferData.VertexStride = CalculateVertexStride(deferredQuadVertexBufferData.Attributes);

			GraphicsPipelineCreateInfo deferredPipelineCreateInfo = {};
			deferredPipelineCreateInfo.shaderID = deferredCombineShaderIndex;
			deferredPipelineCreateInfo.descriptorSetLayoutIndex = deferredCombineShaderIndex;
			deferredPipelineCreateInfo.vertexAttributes = deferredQuadVertexBufferData.Attributes;
			deferredPipelineCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			deferredPipelineCreateInfo.cullMode = VK_CULL_MODE_NONE;
			deferredPipelineCreateInfo.renderPass = m_DeferredCombineRenderPass;
			deferredPipelineCreateInfo.setDynamicStates = false; // ?
			deferredPipelineCreateInfo.enabledColorBlending = false;
			deferredPipelineCreateInfo.pipelineLayout = &m_DeferredPipelineLayout;
			deferredPipelineCreateInfo.grahpicsPipeline = &m_DeferredPipeline;
			deferredPipelineCreateInfo.depthTestEnable = VK_FALSE;
			deferredPipelineCreateInfo.depthWriteEnable = VK_FALSE;
			// TODO: Use pipeline cache?
			CreateGraphicsPipeline(&deferredPipelineCreateInfo);

			// Offscreen descriptor set
			DescriptorSetCreateInfo offscreenDescriptorSetCreateInfo = {};
			offscreenDescriptorSetCreateInfo.descriptorSetLayoutIndex = deferredCombineShaderIndex;
			offscreenDescriptorSetCreateInfo.uniformBufferIndex = deferredCombineShaderIndex;
			offscreenDescriptorSetCreateInfo.positionFrameBufferView = &offScreenFrameBuf->position.view;
			offscreenDescriptorSetCreateInfo.normalFrameBufferView = &offScreenFrameBuf->normal.view;
			offscreenDescriptorSetCreateInfo.diffuseSpecularFrameBufferView = &offScreenFrameBuf->albedo.view;
			offscreenDescriptorSetCreateInfo.descriptorSet = &m_OffscreenBufferDescriptorSet;
			CreateDescriptorSet(&offscreenDescriptorSetCreateInfo);


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

			m_PresentCompleteSemaphore.replace();
			m_RenderCompleteSemaphore.replace();

			for (size_t i = 0; i < m_VertexIndexBufferPairs.size(); ++i)
			{
				SafeDelete(m_VertexIndexBufferPairs[i].vertexBuffer);
				SafeDelete(m_VertexIndexBufferPairs[i].indexBuffer);
			}



			// Frame buffer

			vkDestroyImageView(m_VulkanDevice->m_LogicalDevice, offScreenFrameBuf->position.view, nullptr);
			vkDestroyImage(m_VulkanDevice->m_LogicalDevice, offScreenFrameBuf->position.image, nullptr);
			vkFreeMemory(m_VulkanDevice->m_LogicalDevice, offScreenFrameBuf->position.mem, nullptr);

			vkDestroyImageView(m_VulkanDevice->m_LogicalDevice, offScreenFrameBuf->normal.view, nullptr);
			vkDestroyImage(m_VulkanDevice->m_LogicalDevice, offScreenFrameBuf->normal.image, nullptr);
			vkFreeMemory(m_VulkanDevice->m_LogicalDevice, offScreenFrameBuf->normal.mem, nullptr);

			vkDestroyImageView(m_VulkanDevice->m_LogicalDevice, offScreenFrameBuf->albedo.view, nullptr);
			vkDestroyImage(m_VulkanDevice->m_LogicalDevice, offScreenFrameBuf->albedo.image, nullptr);
			vkFreeMemory(m_VulkanDevice->m_LogicalDevice, offScreenFrameBuf->albedo.mem, nullptr);

			vkDestroyFramebuffer(m_VulkanDevice->m_LogicalDevice, offScreenFrameBuf->frameBuffer, nullptr);
			vkDestroyRenderPass(m_VulkanDevice->m_LogicalDevice, offScreenFrameBuf->renderPass, nullptr);
			
			SafeDelete(offScreenFrameBuf);

			SafeDelete(offscreenQuadVertexIndexBufferPair.indexBuffer);
			SafeDelete(offscreenQuadVertexIndexBufferPair.vertexBuffer);

			vkDestroySemaphore(m_VulkanDevice->m_LogicalDevice, offscreenSemaphore, nullptr);

			vkDestroySampler(m_VulkanDevice->m_LogicalDevice, colorSampler, nullptr);

			vkDestroyPipeline(m_VulkanDevice->m_LogicalDevice, m_DeferredPipeline, nullptr);
			vkDestroyPipeline(m_VulkanDevice->m_LogicalDevice, m_ImGui_GraphicsPipeline, nullptr);

			vkDestroyPipelineLayout(m_VulkanDevice->m_LogicalDevice, m_DeferredPipelineLayout, nullptr);
			vkDestroyPipelineLayout(m_VulkanDevice->m_LogicalDevice, m_ImGui_PipelineLayout, nullptr);
			
			vkDestroyPipelineCache(m_VulkanDevice->m_LogicalDevice, m_ImGuiPipelineCache, nullptr);

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

			m_DeferredCombineRenderPass.replace();

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

			VulkanMaterial mat = {};
			mat.material = {};

			mat.material.shaderID = createInfo->shaderID;
			mat.material.name = createInfo->name;
			mat.material.diffuseTexturePath = createInfo->diffuseTexturePath;
			mat.material.normalTexturePath = createInfo->normalTexturePath;
			mat.material.specularTexturePath = createInfo->specularTexturePath;
			mat.material.cubeMapFilePaths = createInfo->cubeMapFilePaths;

			mat.descriptorSetLayoutIndex = createInfo->shaderID;

			if (createInfo->diffuseTexturePath == m_BrickDiffuseTexture->filePath)
			{
				mat.diffuseTexture = m_BrickDiffuseTexture;
			}
			else if (createInfo->diffuseTexturePath == m_WorkDiffuseTexture->filePath)
			{
				mat.diffuseTexture = m_WorkDiffuseTexture;
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
					mat.material.useCubemapTexture = true;
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
			renderObject->info.materialName = m_LoadedMaterials[renderObject->materialID].material.name;
			renderObject->info.transform = createInfo->transform;

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
			BuildCommandBuffers();
			BuildDeferredCommandBuffer(); // TODO: Only call this once at startup?

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

		void VulkanRenderer::UpdateTransformMatrix(const GameContext& gameContext, RenderID renderID, const glm::mat4& model)
		{
			UpdateUniformBufferDynamic(gameContext, renderID, model);
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

			// TODO: Remove constant
			const int vertexIndexBufferPairIndex = 2;
			VertexIndexBufferPair& bufferPair = m_VertexIndexBufferPairs[vertexIndexBufferPairIndex];

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
			// TODO: Remove hardcoded value
			const glm::uint shaderIndex = 2;

			m_ImGuiPushConstBlock = { glm::vec2(1.0f, 1.0f), glm::vec2(0.0f, 0.0f) };

			{
				ImGui_CreateFontsTexture(m_GraphicsQueue);

				DescriptorSetCreateInfo createInfo = {};
				createInfo.descriptorSet = &m_ImGuiDescriptorSet;
				createInfo.descriptorSetLayoutIndex = shaderIndex;
				createInfo.diffuseTexture = m_ImGuiFontTexture;
				createInfo.uniformBufferIndex = shaderIndex;

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

			glm::uint descriptorSetLayoutIndex = shaderIndex;

			GraphicsPipelineCreateInfo pipelineCreateInfo = {};

			pipelineCreateInfo.shaderID = shaderIndex;
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

		void VulkanRenderer::PostInitializeRenderObject(RenderID renderID)
		{
			CreateDescriptorSet(renderID);
			CreateGraphicsPipeline(renderID);
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

		std::vector<Renderer::PointLight>& VulkanRenderer::GetAllPointLights()
		{
			return m_PointLights;
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
			io.Fonts->TexID = (void *)(intptr_t)&m_ImGuiFontTexture->image;

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
			subpasses[0] = {};
			subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpasses[0].colorAttachmentCount = 1;
			subpasses[0].pColorAttachments = &colorAttachmentRef;
			subpasses[0].pDepthStencilAttachment = &depthAttachmentRef;

			subpasses[1] = {};
			subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpasses[1].colorAttachmentCount = 1;
			subpasses[1].pColorAttachments = &colorAttachmentRef;
			subpasses[1].pDepthStencilAttachment = &depthAttachmentRef;

			
			std::array<VkSubpassDependency, 3> dependencies;
			dependencies[0] = {};
			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = 0;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			dependencies[1] = {};
			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = 1;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].srcAccessMask = 0;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			dependencies[2] = {};
			dependencies[2].srcSubpass = 1;
			dependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[2].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[2].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;


			/*
			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
			*/


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
			RenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject) return;

			VulkanMaterial* material = &m_LoadedMaterials[renderObject->materialID];

			DescriptorSetCreateInfo createInfo = {};
			createInfo.descriptorSet = &renderObject->descriptorSet;
			createInfo.descriptorSetLayoutIndex = material->descriptorSetLayoutIndex;
			createInfo.uniformBufferIndex = material->material.shaderID;
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


			Uniforms constantBufferUniforms = m_Shaders[createInfo->uniformBufferIndex].constantBufferUniforms;
			Uniforms dynamicBufferUniforms = m_Shaders[createInfo->uniformBufferIndex].dynamicBufferUniforms;

			UniformBuffer& uniformBuffer = m_UniformBuffers[createInfo->uniformBufferIndex];

			struct DescriptorSetInfo
			{
				std::string uniformName;
				VkDescriptorType descriptorType;

				VkBuffer buffer = VK_NULL_HANDLE;
				VkDeviceSize bufferSize = 0;

				VkImageView imageView = VK_NULL_HANDLE;
				VkSampler imageSampler = VK_NULL_HANDLE;

				// These should not be filled in, they are just here so that they are kept around until the call
				// to vkUpdateDescriptorSets, and can not be local to the following for loop
				VkDescriptorBufferInfo bufferInfo;
				VkDescriptorImageInfo imageInfo;
			};

			// TODO: Clean up nullptr checks somehow?
			DescriptorSetInfo descriptorSets[] = {
				{ "uniformBufferConstant", VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				uniformBuffer.constantBuffer.m_Buffer, sizeof(VulkanUniformBufferObjectData) },

				{ "uniformBufferDynamic", VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				uniformBuffer.dynamicBuffer.m_Buffer, sizeof(VulkanUniformBufferObjectData) * m_RenderObjects.size() },

				{ "diffuseSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->diffuseTexture ? createInfo->diffuseTexture->imageView : 0u,
				createInfo->diffuseTexture ? createInfo->diffuseTexture->sampler : 0u },

				{ "normalSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->normalTexture ? createInfo->normalTexture->imageView : 0u,
				createInfo->normalTexture ? createInfo->normalTexture->sampler : 0u },

				{ "specularSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->specularTexture ? createInfo->specularTexture->imageView : 0u,
				createInfo->specularTexture ? createInfo->specularTexture->sampler : 0u },

				{ "cubemapSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->cubemapTexture ? createInfo->cubemapTexture->imageView : 0u,
				createInfo->cubemapTexture ? createInfo->cubemapTexture->sampler : 0u },

				{ "positionFrameBufferSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->positionFrameBufferView ? *createInfo->positionFrameBufferView : 0u,
				colorSampler },

				{ "normalFrameBufferSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->normalFrameBufferView ? *createInfo->normalFrameBufferView : 0u,
				colorSampler },

				{ "diffuseSpecularFrameBufferSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->diffuseSpecularFrameBufferView ? *createInfo->diffuseSpecularFrameBufferView : 0u,
				colorSampler },

				{ "albedoFrameBufferSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->albedoTexture ? createInfo->albedoTexture->imageView : 0u,
				createInfo->albedoTexture ? createInfo->albedoTexture->sampler : 0u },

				{ "metallicFrameBufferSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->metallicTexture ? createInfo->metallicTexture->imageView : 0u,
				createInfo->metallicTexture ? createInfo->metallicTexture->sampler : 0u },

				{ "roughnessFrameBufferSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->roughnessTexture ? createInfo->roughnessTexture->imageView : 0u,
				createInfo->roughnessTexture ? createInfo->roughnessTexture->sampler : 0u },

				{ "aoFrameBufferSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_NULL_HANDLE, 0,
				createInfo->aoTexture ? createInfo->aoTexture->imageView : 0u,
				createInfo->aoTexture ? createInfo->aoTexture->sampler : 0u },
			};
			const size_t descSetCount = sizeof(descriptorSets) / sizeof(descriptorSets[0]);

			std::vector<VkWriteDescriptorSet> writeDescriptorSets;
			writeDescriptorSets.reserve(descSetCount);

			glm::uint descriptorSetIndex = 0;
			glm::uint binding = 0;

			for (size_t i = 0; i < descSetCount; ++i)
			{
				if (constantBufferUniforms.HasUniform(descriptorSets[i].uniformName) ||
					dynamicBufferUniforms.HasUniform(descriptorSets[i].uniformName))
				{
					descriptorSets[i].bufferInfo = {};
					descriptorSets[i].bufferInfo.buffer = descriptorSets[i].buffer;
					descriptorSets[i].bufferInfo.range = descriptorSets[i].bufferSize;

					descriptorSets[i].imageInfo = {};
					descriptorSets[i].imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					if (descriptorSets[i].imageView)
					{
						descriptorSets[i].imageInfo.imageView = descriptorSets[i].imageView;
						descriptorSets[i].imageInfo.sampler = descriptorSets[i].imageSampler;
					}
					else
					{
						if (descriptorSets[i].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER ||
							descriptorSets[i].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
							descriptorSets[i].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
							descriptorSets[i].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
							descriptorSets[i].descriptorType == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
						{
							// If setting a sampler type, image info must be filled in
							descriptorSets[i].imageInfo.imageView = m_BlankTexture->imageView;
							descriptorSets[i].imageInfo.sampler = m_BlankTexture->sampler;
						}
					}

					writeDescriptorSets.push_back({});
					writeDescriptorSets[descriptorSetIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					writeDescriptorSets[descriptorSetIndex].dstSet = *createInfo->descriptorSet;
					writeDescriptorSets[descriptorSetIndex].dstBinding = binding;
					writeDescriptorSets[descriptorSetIndex].dstArrayElement = 0;
					writeDescriptorSets[descriptorSetIndex].descriptorType = descriptorSets[i].descriptorType;
					writeDescriptorSets[descriptorSetIndex].descriptorCount = 1;
					writeDescriptorSets[descriptorSetIndex].pBufferInfo = descriptorSets[i].bufferInfo.buffer ? &descriptorSets[i].bufferInfo : nullptr;
					writeDescriptorSets[descriptorSetIndex].pImageInfo = descriptorSets[i].imageInfo.imageView ? &descriptorSets[i].imageInfo : nullptr;

					++descriptorSetIndex;
					++binding;
				}
			}

			if (!writeDescriptorSets.empty())
			{
				vkUpdateDescriptorSets(m_VulkanDevice->m_LogicalDevice, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, nullptr);
			}
		}

		void VulkanRenderer::CreateDescriptorSetLayout(glm::uint shaderIndex)
		{
			m_DescriptorSetLayouts.push_back(VkDescriptorSetLayout());
			VkDescriptorSetLayout* descriptorSetLayout = &m_DescriptorSetLayouts.back();

			Shader* shader = &m_Shaders[shaderIndex];

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

				{ "diffuseSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "normalSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "specularSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "cubemapSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "positionFrameBufferSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "normalFrameBufferSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "diffuseSpecularFrameBufferSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "albedoFrameBufferSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "metallicFrameBufferSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "roughnessFrameBufferSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },

				{ "aoFrameBufferSampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT },
			};
			const size_t descSetCount = sizeof(descriptorSets) / sizeof(descriptorSets[0]);

			std::vector<VkDescriptorSetLayoutBinding> bindings;
			glm::uint binding = 0;

			for (size_t i = 0; i < descSetCount; ++i)
			{
				if (shader->constantBufferUniforms.HasUniform(descriptorSets[i].uniformName) ||
					shader->dynamicBufferUniforms.HasUniform(descriptorSets[i].uniformName))
				{
					VkDescriptorSetLayoutBinding descSetLayoutBinding = {};
					descSetLayoutBinding.binding = binding;
					descSetLayoutBinding.descriptorCount = 1;
					descSetLayoutBinding.descriptorType = descriptorSets[i].descriptorType;
					descSetLayoutBinding.stageFlags = descriptorSets[i].shaderStageFlags;
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
			VulkanMaterial* material = &m_LoadedMaterials[renderObject->materialID];
			Shader& shader = m_Shaders[material->material.shaderID];

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
			pipelineCreateInfo.renderPass = shader.deferred ? offScreenFrameBuf->renderPass : m_DeferredCombineRenderPass;
			pipelineCreateInfo.subpass = shader.deferred ? 0 : 1;

			CreateGraphicsPipeline(&pipelineCreateInfo);
		}

		void VulkanRenderer::CreateGraphicsPipeline(GraphicsPipelineCreateInfo* createInfo)
		{
			Shader& shader = m_Shaders[createInfo->shaderID];

			VDeleter<VkShaderModule> vertShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(shader.vertexShaderCode, vertShaderModule))
			{
				Logger::LogError("Failed to compile vertex shader located at: " + shader.vertexShaderFilePath);
			}

			VDeleter<VkShaderModule> fragShaderModule{ m_VulkanDevice->m_LogicalDevice, vkDestroyShaderModule };
			if (!CreateShaderModule(shader.fragmentShaderCode, fragShaderModule))
			{
				Logger::LogError("Failed to compile fragment shader located at: " + shader.fragmentShaderFilePath);
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

			VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

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
			for (int i = 0; i < shader.numAttachments; ++i)
			{
				colorBlendAttachments.push_back({});

				colorBlendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

				if (createInfo->enabledColorBlending)
				{
					colorBlendAttachments[i].blendEnable = VK_TRUE;
					colorBlendAttachments[i].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					colorBlendAttachments[i].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					colorBlendAttachments[i].colorBlendOp = VK_BLEND_OP_ADD;
					colorBlendAttachments[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					colorBlendAttachments[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
					colorBlendAttachments[i].alphaBlendOp = VK_BLEND_OP_ADD;
				}
				else
				{
					colorBlendAttachments[i].blendEnable = VK_FALSE;
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
			depthStencil.depthTestEnable = createInfo->depthTestEnable;
			depthStencil.depthWriteEnable = createInfo->depthWriteEnable;
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
			pipelineInfo.renderPass = createInfo->renderPass;
			pipelineInfo.pDynamicState = pDynamicState;
			pipelineInfo.subpass = createInfo->subpass;
			pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

			VkPipelineCache pipelineCache = createInfo->pipelineCache ? *createInfo->pipelineCache : VK_NULL_HANDLE;

			VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_VulkanDevice->m_LogicalDevice, pipelineCache, 1, &pipelineInfo, nullptr, createInfo->grahpicsPipeline));
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

			const glm::vec2i frameBufferSize = window->GetFrameBufferSize();
			offScreenFrameBuf->width = frameBufferSize.x;
			offScreenFrameBuf->height = frameBufferSize.y;

			// Color attachments

			// (World space) Positions
			CreateAttachment(
				m_VulkanDevice,
				VK_FORMAT_R16G16B16A16_SFLOAT,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
				offScreenFrameBuf->width,
				offScreenFrameBuf->height,
				&offScreenFrameBuf->position);

			// (World space) Normals
			CreateAttachment(
				m_VulkanDevice,
				VK_FORMAT_R16G16B16A16_SFLOAT,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
				offScreenFrameBuf->width,
				offScreenFrameBuf->height,
				&offScreenFrameBuf->normal);

			// Albedo (color)
			CreateAttachment(
				m_VulkanDevice,
				VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
				offScreenFrameBuf->width,
				offScreenFrameBuf->height,
				&offScreenFrameBuf->albedo);

			// Depth attachment

			// Find a suitable depth format
			VkFormat attDepthFormat;
			VkBool32 validDepthFormat = GetSupportedDepthFormat(m_VulkanDevice->m_PhysicalDevice, &attDepthFormat);
			assert(validDepthFormat);

			/*CreateAttachment(
				m_VulkanDevice,
				attDepthFormat,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
				offScreenFrameBuf->width,
				offScreenFrameBuf->height,
				&offScreenFrameBuf->depth);*/

			// Set up separate renderpass with references to the color and depth attachments
			std::array<VkAttachmentDescription, 4> attachmentDescs = {};

			// Init attachment properties
			for (uint32_t i = 0; i < attachmentDescs.size(); ++i)
			{
				attachmentDescs[i].samples = VK_SAMPLE_COUNT_1_BIT;
				attachmentDescs[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				attachmentDescs[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				attachmentDescs[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				attachmentDescs[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				if (i == 3)
				{
					attachmentDescs[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					attachmentDescs[i].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				}
				else
				{
					attachmentDescs[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					attachmentDescs[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				}
			}

			// Formats
			attachmentDescs[0].format = offScreenFrameBuf->position.format;
			attachmentDescs[1].format = offScreenFrameBuf->normal.format;
			attachmentDescs[2].format = offScreenFrameBuf->albedo.format;
			attachmentDescs[3].format = m_DepthImageFormat;

			std::vector<VkAttachmentReference> colorReferences;
			colorReferences.push_back({ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
			colorReferences.push_back({ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
			colorReferences.push_back({ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });

			VkAttachmentReference depthReference = {};
			depthReference.attachment = 3;
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

			std::array<VkImageView, 4> attachments;
			attachments[0] = offScreenFrameBuf->position.view;
			attachments[1] = offScreenFrameBuf->normal.view;
			attachments[2] = offScreenFrameBuf->albedo.view;
			attachments[3] = m_DepthImageView;

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

		bool VulkanRenderer::HasStencilComponent(VkFormat format)
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
			CreateImageView(m_DepthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, m_DepthImageView.replace());

			TransitionImageLayout(m_DepthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);


			// Deferred images
			//CreateImage(m_SwapChainExtent.width, m_SwapChainExtent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL,
			//	VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_PREINITIALIZED, //&offScreenFrameBuf->depth.image, &offScreenFrameBuf->depth.mem);
			//CreateImageView(offScreenFrameBuf->depth.image, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, &offScreenFrameBuf->depth.view);
			//
			//TransitionImageLayout(offScreenFrameBuf->depth.image, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, //VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
			
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
			Buffer stagingBuffer(m_VulkanDevice->m_LogicalDevice);

			VkBufferCreateInfo bufferCreateInfo = {};
			bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferCreateInfo.size = totalSize; // TODO: size1?
											   // This buffer is used as a transfer source for the buffer copy
			bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;


			VK_CHECK_RESULT(vkCreateBuffer(m_VulkanDevice->m_LogicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer.m_Buffer));

			// Get memory requirements for the staging buffer (alignment, memory type bits)
			vkGetBufferMemoryRequirements(m_VulkanDevice->m_LogicalDevice, stagingBuffer.m_Buffer, &memReqs);
			memAllocInfo.allocationSize = memReqs.size;
			// Get memory type index for a host visible buffer
			memAllocInfo.memoryTypeIndex = m_VulkanDevice->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			VK_CHECK_RESULT(vkAllocateMemory(m_VulkanDevice->m_LogicalDevice, &memAllocInfo, nullptr, &stagingBuffer.m_Memory));
			stagingBuffer.Bind();

			// Copy texture data into staging buffer
			stagingBuffer.Map(memReqs.size);
			memcpy(stagingBuffer.m_Mapped, pixels, totalSize);
			free(pixels);
			stagingBuffer.Unmap();

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

			// TODO: Remove (no longer needed because using Buffer, which has a destructor)
			// Clean up staging resources
			//vkFreeMemory(m_VulkanDevice->m_LogicalDevice, stagingMemory, nullptr);
			//vkDestroyBuffer(m_VulkanDevice->m_LogicalDevice, stagingBuffer, nullptr);

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
			renderPassBeginInfo.renderPass = m_DeferredCombineRenderPass;
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

				VkRect2D scissor = VkRect2D{ { 0u, 0u },{ m_SwapChainExtent.width, m_SwapChainExtent.height } };
				vkCmdSetScissor(m_CommandBuffers[i], 0, 1, &scissor);


				vkCmdBindDescriptorSets(m_CommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, m_DeferredPipelineLayout, 0, 1, &m_OffscreenBufferDescriptorSet, 0, nullptr);

				// Final composition as full screen quad (deferred combine)
				vkCmdBindPipeline(m_CommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, m_DeferredPipeline);
				VkDeviceSize offsets[1] = { 0 };
				vkCmdBindVertexBuffers(m_CommandBuffers[i], 0, 1, &offscreenQuadVertexIndexBufferPair.vertexBuffer->m_Buffer, offsets);
				vkCmdBindIndexBuffer(m_CommandBuffers[i], offscreenQuadVertexIndexBufferPair.indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(m_CommandBuffers[i], 6, 1, 0, 0, 1);


				vkCmdNextSubpass(m_CommandBuffers[i], VK_SUBPASS_CONTENTS_INLINE);


				// Forward rendered objects

				// TODO: Batch objects with same materials together like in GL renderer
				for (size_t j = 0; j < m_RenderObjects.size(); ++j)
				{
					RenderObject* renderObject = GetRenderObject(j);
					if (!renderObject) continue;

					VulkanMaterial* material = &m_LoadedMaterials[renderObject->materialID];

					// Only render non-deferred (forward) objects in this pass
					if (m_Shaders[material->material.shaderID].deferred) continue;
					
					vkCmdBindVertexBuffers(m_CommandBuffers[i], 0, 1, &m_VertexIndexBufferPairs[material->material.shaderID].vertexBuffer->m_Buffer, offsets);


					if (m_VertexIndexBufferPairs[material->material.shaderID].indexBuffer->m_Size != 0)
					{
						vkCmdBindIndexBuffer(m_CommandBuffers[i], m_VertexIndexBufferPairs[material->material.shaderID].indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
					}

					vkCmdBindPipeline(m_CommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, renderObject->graphicsPipeline);

					uint32_t dynamicOffset = j * static_cast<uint32_t>(m_DynamicAlignment);
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

		void VulkanRenderer::BuildDeferredCommandBuffer()
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
			for (size_t j = 0; j < m_RenderObjects.size(); ++j)
			{
				RenderObject* renderObject = GetRenderObject(j);
				if (!renderObject) continue;

				VulkanMaterial* material = &m_LoadedMaterials[renderObject->materialID];

				// Only render deferred objects in this pass
				if (!m_Shaders[material->material.shaderID].deferred) continue;

				vkCmdBindVertexBuffers(offScreenCmdBuffer, 0, 1, &m_VertexIndexBufferPairs[material->material.shaderID].vertexBuffer->m_Buffer, offsets);

				if (m_VertexIndexBufferPairs[material->material.shaderID].indexBuffer->m_Size != 0)
				{
					vkCmdBindIndexBuffer(offScreenCmdBuffer, m_VertexIndexBufferPairs[material->material.shaderID].indexBuffer->m_Buffer, 0, VK_INDEX_TYPE_UINT32);
				}

				vkCmdBindPipeline(offScreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderObject->graphicsPipeline);

				uint32_t dynamicOffset = j * static_cast<uint32_t>(m_DynamicAlignment);
				vkCmdBindDescriptorSets(offScreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderObject->pipelineLayout,
					0, 1, &renderObject->descriptorSet,
					1, &dynamicOffset);

				if (renderObject->indexed)
				{
					vkCmdDrawIndexed(offScreenCmdBuffer, renderObject->indices->size(), 1, renderObject->indexOffset, 0, 0);
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
				if (renderObject && m_LoadedMaterials[renderObject->materialID].material.shaderID == shaderIndex)
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
				Logger::LogError("Failed to create static vertex buffer (no verts use shader index " + std::to_string(shaderIndex) + "!)");
				return 0;
			}
			
			CreateStaticVertexBuffer(vertexBuffer, vertexDataStart, vertexBufferSize);
			free(vertexDataStart);

			return vertexCount;
		}

		void VulkanRenderer::CreateStaticVertexBuffer(Buffer* vertexBuffer, void* vertexBufferData, glm::uint vertexBufferSize)
		{
			Buffer stagingBuffer(m_VulkanDevice->m_LogicalDevice);
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

		glm::uint VulkanRenderer::CreateStaticIndexBuffer(Buffer* indexBuffer, glm::uint shaderIndex)
		{
			std::vector<glm::uint> indices;

			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				RenderObject* renderObject = GetRenderObject(i);
				if (renderObject && m_LoadedMaterials[renderObject->materialID].material.shaderID == shaderIndex && renderObject->indexed)
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
		
		void VulkanRenderer::CreateStaticIndexBuffer(Buffer* indexBuffer, const std::vector<glm::uint>& indices)
		{
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
		}

		void VulkanRenderer::PrepareUniformBuffers()
		{
			if (!m_UniformBuffers.empty())
			{
				ReleaseUniformBuffers();
			}

			m_UniformBuffers.resize(m_Shaders.size(), { m_VulkanDevice->m_LogicalDevice });

			glm::uint shaderIndex = 0;

			// Deferred Simple
			m_Shaders[shaderIndex].numAttachments = 3;
			m_Shaders[shaderIndex].deferred = true;

			m_Shaders[shaderIndex].constantBufferUniforms.AddUniform("uniformBufferConstant", true);
			m_Shaders[shaderIndex].constantBufferUniforms.AddUniform("viewProjection", true);

			m_Shaders[shaderIndex].dynamicBufferUniforms.AddUniform("uniformBufferDynamic", true);
			m_Shaders[shaderIndex].dynamicBufferUniforms.AddUniform("model", true);
			m_Shaders[shaderIndex].dynamicBufferUniforms.AddUniform("useDiffuseSampler", true);
			m_Shaders[shaderIndex].dynamicBufferUniforms.AddUniform("diffuseSampler", true);
			m_Shaders[shaderIndex].dynamicBufferUniforms.AddUniform("useNormalSampler", true);
			m_Shaders[shaderIndex].dynamicBufferUniforms.AddUniform("normalSampler", true);
			m_Shaders[shaderIndex].dynamicBufferUniforms.AddUniform("useSpecularSampler", true);
			m_Shaders[shaderIndex].dynamicBufferUniforms.AddUniform("specularSampler", true);
			++shaderIndex;

			// Color
			m_Shaders[shaderIndex].deferred = false;
			m_Shaders[shaderIndex].constantBufferUniforms.AddUniform("uniformBufferConstant", true);
			m_Shaders[shaderIndex].constantBufferUniforms.AddUniform("viewProjection", true);
			
			m_Shaders[shaderIndex].dynamicBufferUniforms.AddUniform("uniformBufferDynamic", true);
			m_Shaders[shaderIndex].dynamicBufferUniforms.AddUniform("model", true);
			++shaderIndex;

			// ImGui
			m_Shaders[shaderIndex].deferred = false;
			m_Shaders[shaderIndex].constantBufferUniforms = {};

			m_Shaders[shaderIndex].dynamicBufferUniforms.AddUniform("diffuseSampler", true);
			++shaderIndex;

			// Skybox
			m_Shaders[shaderIndex].deferred = false;
			m_Shaders[shaderIndex].constantBufferUniforms.AddUniform("uniformBufferConstant", true);
			m_Shaders[shaderIndex].constantBufferUniforms.AddUniform("view", true);
			m_Shaders[shaderIndex].constantBufferUniforms.AddUniform("projection", true);
			
			m_Shaders[shaderIndex].dynamicBufferUniforms.AddUniform("uniformBufferDynamic", true);
			m_Shaders[shaderIndex].dynamicBufferUniforms.AddUniform("model", true);
			m_Shaders[shaderIndex].dynamicBufferUniforms.AddUniform("useCubemapSampler", true);
			m_Shaders[shaderIndex].dynamicBufferUniforms.AddUniform("cubemapSampler", true);
			++shaderIndex;

			// Deferred combine (sample gbuffer)
			m_Shaders[shaderIndex].deferred = false; // Sounds strange but this isn't deferred
			// TODO: Specify that this is only used in the frag shader here
			m_Shaders[shaderIndex].constantBufferUniforms.AddUniform("uniformBufferConstant", true);
			m_Shaders[shaderIndex].constantBufferUniforms.AddUniform("camPos", true);
			m_Shaders[shaderIndex].constantBufferUniforms.AddUniform("dirLight", true);
			m_Shaders[shaderIndex].constantBufferUniforms.AddUniform("pointLights", true);
			m_Shaders[shaderIndex].constantBufferUniforms.AddUniform("positionFrameBufferSampler", true);
			m_Shaders[shaderIndex].constantBufferUniforms.AddUniform("normalFrameBufferSampler", true);
			m_Shaders[shaderIndex].constantBufferUniforms.AddUniform("diffuseSpecularFrameBufferSampler", true);

			m_Shaders[shaderIndex].dynamicBufferUniforms = {};
			++shaderIndex;


			for (size_t i = 0; i < m_UniformBuffers.size(); ++i)
			{
				m_UniformBuffers[i].constantData.size = m_Shaders[i].constantBufferUniforms.CalculateSize(m_PointLights.size());
				if (m_UniformBuffers[i].constantData.size > 0)
				{
					m_UniformBuffers[i].constantData.data = (float*)malloc(m_UniformBuffers[i].constantData.size);
					assert(m_UniformBuffers[i].constantData.data);

					PrepareUniformBuffer(&m_UniformBuffers[i].constantBuffer, m_UniformBuffers[i].constantData.size,
						VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				}

				m_UniformBuffers[i].dynamicData.size = m_Shaders[i].dynamicBufferUniforms.CalculateSize(m_PointLights.size());
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

		bool VulkanRenderer::CreateShaderModule(const std::vector<char>& code, VDeleter<VkShaderModule>& shaderModule)
		{
			VkShaderModuleCreateInfo createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			createInfo.codeSize = code.size();
			createInfo.pCode = (uint32_t*)code.data();

			VkResult result = vkCreateShaderModule(m_VulkanDevice->m_LogicalDevice, &createInfo, nullptr, shaderModule.replace());
			VK_CHECK_RESULT(result);

			return (result == VK_SUCCESS);
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

			for (size_t i = 0; i < m_UniformBuffers.size(); ++i)
			{
				glm::uint index = 0;

				Uniforms& constantUniforms = m_Shaders[i].constantBufferUniforms;
				VulkanUniformBufferObjectData& constantData = m_UniformBuffers[i].constantData;

				if (constantUniforms.HasUniform("projection"))
				{
					memcpy(&constantData.data[index], &proj[0][0], sizeof(glm::mat4));
					index += 16;
				}

				if (constantUniforms.HasUniform("view"))
				{
					memcpy(&constantData.data[index], &view[0][0], sizeof(glm::mat4));
					index += 16;
				}

				if (constantUniforms.HasUniform("viewInv"))
				{
					memcpy(&constantData.data[index], &viewInv[0][0], sizeof(glm::mat4));
					index += 16;
				}

				if (constantUniforms.HasUniform("viewProj"))
				{
					memcpy(&constantData.data[index], &viewProj[0][0], sizeof(glm::mat4));
					index += 16;
				}

				if (constantUniforms.HasUniform("model"))
				{
					Logger::LogError("Constant uniform buffer contains model matrix, which should be in the dynamic uniform buffer");
				}

				if (constantUniforms.HasUniform("modelInvTranspose"))
				{
					Logger::LogError("Constant uniform buffer contains modelInvTranspose matrix, which should be in the dynamic uniform buffer");
				}

				if (constantUniforms.HasUniform("modelViewProjection"))
				{
					Logger::LogError("Constant uniform buffer contains MVP matrix, which should be in the dynamic uniform buffer");
				}

				if (constantUniforms.HasUniform("camPos"))
				{
					memcpy(&constantData.data[index], &camPos[0], sizeof(glm::vec4));
					index += 4;
				}

				if (constantUniforms.HasUniform("dirLight"))
				{
					memcpy(&constantData.data[index], &m_DirectionalLight, sizeof(m_DirectionalLight));
					index += sizeof(m_DirectionalLight) / 4;
				}

				if (constantUniforms.HasUniform("pointLights"))
				{
					for (size_t j = 0; j < m_PointLights.size(); ++j)
					{
						memcpy(&constantData.data[index], &m_PointLights[j], sizeof(m_PointLights[j]));
						index += sizeof(m_PointLights[j]) / 4;
					}
				}

				glm::uint size = constantData.size;

#if  _DEBUG
				// All three size calculations should be the same
				glm::uint calculatedSize1 = index * 4;
				glm::uint calculatedSize2 = constantUniforms.CalculateSize(m_PointLights.size());
				assert(calculatedSize1 == calculatedSize2 &&
					calculatedSize1 == size);
#endif // _DEBUG


				memcpy(m_UniformBuffers[i].constantBuffer.m_Mapped, constantData.data, size);
			}
		}

		void VulkanRenderer::UpdateUniformBufferDynamic(const GameContext& gameContext, RenderID renderID, const glm::mat4& model)
		{
			UNREFERENCED_PARAMETER(gameContext);

			RenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject) return;
			VulkanMaterial* material = &m_LoadedMaterials[renderObject->materialID];
			Shader* shader = &m_Shaders[material->material.shaderID];

			const glm::mat4 modelInvTranspose = glm::transpose(glm::inverse(model));
			glm::mat4 proj = gameContext.camera->GetProjection();
			glm::mat4 view = gameContext.camera->GetView();
			glm::mat4 modelViewProjection = proj * view * model;
			glm::uint useDiffuseTexture = material->diffuseTexture == nullptr ? 0 : 1;
			glm::uint useNormalTexture = material->normalTexture == nullptr ? 0 : 1;
			glm::uint useSpecularTexture = material->specularTexture == nullptr ? 0 : 1;
			glm::uint useCubemapTexture = material->material.useCubemapTexture ? 1 : 0;

			const int uniformBufferIndex = material->material.shaderID;
			UniformBuffer& uniformBuffer = m_UniformBuffers[uniformBufferIndex];
			Uniforms dynamicUniforms = shader->dynamicBufferUniforms;

			glm::uint offset = renderID * uniformBuffer.dynamicData.size;
			glm::uint index = 0;

			// TODO: Flatten into single loop over array
			if (dynamicUniforms.HasUniform("model"))
			{
				memcpy(&uniformBuffer.dynamicData.data[offset + index], &model, sizeof(model));
				index += 16;
			}

			if (dynamicUniforms.HasUniform("modelInvTranspose"))
			{
				memcpy(&uniformBuffer.dynamicData.data[offset + index], &modelInvTranspose, sizeof(modelInvTranspose));
				index += 16;
			}

			if (dynamicUniforms.HasUniform("modelViewProjection"))
			{
				memcpy(&uniformBuffer.dynamicData.data[offset + index], &modelViewProjection, sizeof(modelViewProjection));
				index += 16;
			}

			if (dynamicUniforms.HasUniform("useDiffuseSampler"))
			{
				memcpy(&uniformBuffer.dynamicData.data[offset + index], &useDiffuseTexture, sizeof(useDiffuseTexture));
				index += 1;
			}

			if (dynamicUniforms.HasUniform("useNormalSampler"))
			{
				memcpy(&uniformBuffer.dynamicData.data[offset + index], &useNormalTexture, sizeof(useNormalTexture));
				index += 1;
			}

			if (dynamicUniforms.HasUniform("useSpecularSampler"))
			{
				memcpy(&uniformBuffer.dynamicData.data[offset + index], &useSpecularTexture, sizeof(useSpecularTexture));
				index += 1;
			}

			if (dynamicUniforms.HasUniform("useCubemapSampler"))
			{
				memcpy(&uniformBuffer.dynamicData.data[offset + index], &useCubemapTexture, sizeof(useCubemapTexture));
				index += 1;
			}

			// Aligned offset
			glm::uint size = uniformBuffer.dynamicData.size;

#if  _DEBUG
			// All three size calculations should be the same
			glm::uint calculatedSize1 = index * 4;
			glm::uint calculatedSize2 = dynamicUniforms.CalculateSize(m_PointLights.size());
			assert(calculatedSize1 == calculatedSize2 &&
				calculatedSize1 == size);
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
				{ shaderDirectory + "vk_deferred_simple_vert.spv", shaderDirectory + "vk_deferred_simple_frag.spv" },
				{ shaderDirectory + "vk_color_vert.spv", shaderDirectory + "vk_color_frag.spv" },
				{ shaderDirectory + "vk_imgui_vert.spv", shaderDirectory + "vk_imgui_frag.spv" },

				// NOTE: Skybox shader should be kept second last to keep other objects rendering in front
				{ shaderDirectory + "vk_skybox_vert.spv", shaderDirectory + "vk_skybox_frag.spv" },

				// NOTE: Deferred combine pass is kept last to ensure all other objects have been drawn
				{ shaderDirectory + "vk_deferred_combine_vert.spv", shaderDirectory + "vk_deferred_combine_frag.spv" },
			};

			const size_t shaderCount = m_Shaders.size();
			for (size_t i = 0; i < shaderCount; ++i)
			{
				m_Shaders[i].vertexShaderCode = ReadFile(m_Shaders[i].vertexShaderFilePath);
				m_Shaders[i].fragmentShaderCode = ReadFile(m_Shaders[i].fragmentShaderFilePath);
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