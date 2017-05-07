#include "stdafx.h"
#if COMPILE_VULKAN

#include "Graphics/VulkanRenderer.h"
#include "GameContext.h"
#include "Window/Window.h"
#include "Logger.h"
#include "FreeCamera.h"
#include "TechDemo.h"

#include <algorithm>	

#include <glm\gtx\hash.hpp>
#include <glm\gtc\matrix_transform.hpp>

#include <iostream>
#include <stdexcept>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <set>
#include <array>
#include <chrono>
#include <unordered_map>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <SOIL.h>

using namespace glm;

VulkanRenderer::VulkanRenderer(GameContext& gameContext)
{
	//glClearColor(0.08f, 0.13f, 0.2f, 1.0f);
	//
	//glEnable(GL_DEPTH_TEST);
	//glDepthFunc(GL_LESS);
	//
	//glUseProgram(gameContext.program);

	//LoadAndBindGLTexture("resources/images/test2.jpg", m_TextureID);
	//glUniform1i(glGetUniformLocation(m_ProgramID, "texTest"), 0);

	// Vulkan is flipped
	gameContext.flipY = true;

	CreateInstance();
	SetupDebugCallback();
	CreateSurface(gameContext.window);
	PickPhysicalDevice();
	CreateLogicalDevice();
	CreateSwapChain(gameContext.window);
	CreateImageViews();
	CreateRenderPass();
	CreateDescriptorSetLayout();
	CreateGraphicsPipeline();
	CreateCommandPool();
	CreateDepthResources();
	CreateFramebuffers();
	CreateTextureImage();
	CreateTextureImageView();
	CreateTextureSampler();
	
//#if 0
//	LoadModel(MODEL_PATH);
//#else
//	CreateCube(1.0f);
//#endif
}

void VulkanRenderer::PostInitialize()
{
	for (size_t i = 0; i < m_RenderObjects.size(); i++)
	{
		CreateVertexBuffer(i);
		CreateIndexBuffer(i);
	}
	CreateUniformBuffer();
	CreateDescriptorPool();
	CreateDescriptorSet();
	CreateCommandBuffers();
	CreateSemaphores();
}

VulkanRenderer::~VulkanRenderer()
{
	for (size_t i = 0; i < m_RenderObjects.size(); i++)
	{
		delete m_RenderObjects[i];
	}
	m_RenderObjects.clear();
	
	vkDeviceWaitIdle(m_Device);

	glfwTerminate();
}

glm::uint VulkanRenderer::Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices)
{
	size_t renderID = m_RenderObjects.size();
	RenderObject* renderObject = new RenderObject(m_Device);
	m_RenderObjects.push_back(renderObject);

	renderObject->vertices = vertices;

	//const size_t offset = m_Vertices.size();
	//m_Vertices.resize(m_Vertices.size() + vertices->size());
	//for (size_t i = 0; i < vertices->size(); ++i)
	//{
	//	m_Vertices[i + offset] = (*vertices)[i];
	//	m_Vertices[i + offset].pos.y *= -1;
	//}

	//CreateCube(1.0f, vec3(1.5f * count, 0.0f, 0.0f));

	//for (size_t i = 0; i < m_Vertices.size(); i++)
	//{
	//	m_Vertices[i].pos.y = -m_Vertices[i].pos.y;
	//}

	return renderID;
	//const uint renderID = m_RenderObjects.size();
	//
	//RenderObject* object = new RenderObject();
	//object->renderID = renderID;
	//
	//glGenVertexArrays(1, &object->VAO);
	//glBindVertexArray(object->VAO);
	//
	//glGenBuffers(1, &object->VBO);
	//glBindBuffer(GL_ARRAY_BUFFER, object->VBO);
	//glBufferData(GL_ARRAY_BUFFER, sizeof(vertices->at(0)) * vertices->size(), vertices->data(), GL_STATIC_DRAW);
	//
	//object->vertices = vertices;
	//
	//uint posAttrib = glGetAttribLocation(gameContext.program, "in_Position");
	//glEnableVertexAttribArray(posAttrib);
	//glVertexAttribPointer(posAttrib, 3, GL_FLOAT, false, VertexPosCol::stride, 0);
	//
	//object->MVP = glGetUniformLocation(gameContext.program, "in_MVP");
	//
	//m_RenderObjects.push_back(object);
	//
	//glBindVertexArray(0);
	//
	//return renderID;
}

glm::uint VulkanRenderer::Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices, std::vector<glm::uint>* indices)
{
	glm::uint renderID = Initialize(gameContext, vertices);
	
	RenderObject* renderObject = GetRenderObject(renderID);
	renderObject->indices = indices;
	renderObject->indexed = true;

	//m_Indices = *indices;
	//const size_t offset = m_Indices.size() - 1;
	//m_Indices.resize(m_Indices.size() + indices->size());
	//for (size_t i = offset; i < m_Indices.size(); i++)
	//{
	//	m_Indices[i] = (*indices)[i];
	//}

	//const uint renderID = Initialize(gameContext, vertices);
	//
	//RenderObject* object = GetRenderObject(renderID);
	//
	//object->indices = indices;
	//object->indexed = true;
	//
	//glGenBuffers(1, &object->IBO);
	//glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, object->IBO);
	//glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices->at(0)) * indices->size(), indices->data(), GL_STATIC_DRAW);
	//
	//return renderID;
	return renderID;
}

void VulkanRenderer::SetClearColor(float r, float g, float b)
{
	m_ClearColor = { r, g, b, 1.0f };
}

void VulkanRenderer::Draw(const GameContext& gameContext, glm::uint renderID)
{
	//RenderObject* renderObject = GetRenderObject(renderID);
	//
	//glBindVertexArray(renderObject->VAO);
	//
	//glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);
	//
	//if (renderObject->indexed)
	//{
	//	glDrawElements(GL_TRIANGLES, renderObject->indices->size(), GL_UNSIGNED_INT, (void*)renderObject->indices->data());
	//}
	//else
	//{
	//	glDrawArrays(GL_TRIANGLES, 0, renderObject->vertices->size());
	//}
	//
	//glBindVertexArray(0);
}

void VulkanRenderer::SetVSyncEnabled(bool enableVSync)
{
	m_VSyncEnabled = enableVSync;
	//glfwSwapInterval(enableVSync ? 1 : 0);
	// TODO: Set swap interval here
}

void VulkanRenderer::Clear(int flags, const GameContext& gameContext)
{
	//GLbitfield mask = 0;
	//if ((int)flags & (int)ClearFlag::COLOR) mask |= GL_COLOR_BUFFER_BIT;
	//if ((int)flags & (int)ClearFlag::DEPTH) mask |= GL_DEPTH_BUFFER_BIT;
	//glClear(mask);
}

void VulkanRenderer::SwapBuffers(const GameContext& gameContext)
{
	//glfwSwapBuffers(gameContext.window->IsGLFWWindow());

	//UpdateUniformBuffer(gameContext);
	DrawFrame(gameContext.window);
}

void VulkanRenderer::UpdateTransformMatrix(const GameContext& gameContext, glm::uint renderID, const glm::mat4x4& model)
{
	RenderObject* renderObject = GetRenderObject(renderID);
	
	//glm::mat4 MVP = gameContext.camera->GetViewProjection() * model;

	UniformBufferObject ubo = {};
	ubo.model = model;
	ubo.view = gameContext.camera->GetView();
	ubo.proj = gameContext.camera->GetProj();

	//const glm::vec3 eyePos(2.0f, 2.0f, 2.0f);
	//ubo.view = glm::lookAt(eyePos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	//
	//const float FOVdegrees = 45.0f;
	//const float nearPlane = 0.1f;
	//const float farPlane = 10.0f;
	//ubo.proj = glm::perspective(glm::radians(FOVdegrees), swapChainExtent.width / (float)swapChainExtent.height, nearPlane, farPlane);
	//ubo.proj[1][1] *= -1;
	
	void* data;
	vkMapMemory(m_Device, m_UniformStagingBufferMemory, 0, sizeof(ubo), 0, &data);
	memcpy(data, &ubo, sizeof(ubo));
	vkUnmapMemory(m_Device, m_UniformStagingBufferMemory);

	UpdateUniformBuffer(gameContext, ubo);
}

int VulkanRenderer::GetShaderUniformLocation(glm::uint program, const std::string uniformName)
{
	//return glGetUniformLocation(program, uniformName.c_str());
	return 0;
}

void VulkanRenderer::SetUniform1f(glm::uint location, float val)
{
	//glUniform1f(location, val);
}

void VulkanRenderer::DescribeShaderVariable(glm::uint renderID, glm::uint program, const std::string& variableName, int size, Renderer::Type renderType, bool normalized, int stride, void* pointer)
{
	//RenderObject* renderObject = GetRenderObject(renderID);
	//
	//glBindVertexArray(renderObject->VAO);
	//
	//GLuint location = glGetAttribLocation(program, variableName.c_str());
	//glEnableVertexAttribArray(location);
	//GLenum glRenderType = TypeToGLType(renderType);
	//glVertexAttribPointer(location, size, glRenderType, normalized, stride, pointer);
	//
	//glBindVertexArray(0);
}

void VulkanRenderer::Destroy(glm::uint renderID)
{
	for (auto iter = m_RenderObjects.begin(); iter != m_RenderObjects.end(); ++iter)
	{
		if ((*iter)->renderID == renderID)
		{
			m_RenderObjects.erase(iter);
			return;
		}
	}
}

//glm::uint VulkanRenderer::BufferTargetToGLTarget(BufferTarget bufferTarget)
//{
//	GLuint glTarget = 0;
//
//	if (bufferTarget == BufferTarget::ARRAY_BUFFER) glTarget = GL_ARRAY_BUFFER;
//	else if (bufferTarget == BufferTarget::ELEMENT_ARRAY_BUFFER) glTarget = GL_ELEMENT_ARRAY_BUFFER;
//	else Logger::LogError("Unhandled BufferTarget passed to GLRenderer: " + std::to_string((int)bufferTarget));
//
//	return glTarget;
//}
//
//GLenum VulkanRenderer::TypeToGLType(Type type)
//{
//	GLenum glType = 0;
//
//	if (type == Type::BYTE) glType = GL_BYTE;
//	else if (type == Type::UNSIGNED_BYTE) glType = GL_UNSIGNED_BYTE;
//	else if (type == Type::SHORT) glType = GL_SHORT;
//	else if (type == Type::UNSIGNED_SHORT) glType = GL_UNSIGNED_SHORT;
//	else if (type == Type::INT) glType = GL_INT;
//	else if (type == Type::UNSIGNED_INT) glType = GL_UNSIGNED_INT;
//	else if (type == Type::FLOAT) glType = GL_FLOAT;
//	else if (type == Type::DOUBLE) glType = GL_DOUBLE;
//	else Logger::LogError("Unhandled Type passed to GLRenderer: " + std::to_string((int)type));
//
//	return glType;
//}
//
//GLenum VulkanRenderer::UsageFlagToGLUsageFlag(UsageFlag usage)
//{
//	GLenum glUsage = 0;
//
//	if (usage == UsageFlag::STATIC_DRAW) glUsage = GL_STATIC_DRAW;
//	else if (usage == UsageFlag::DYNAMIC_DRAW) glUsage = GL_DYNAMIC_DRAW;
//	else Logger::LogError("Unhandled usage flag passed to GLRenderer: " + std::to_string((int)usage));
//
//	return glUsage;
//}
//
//GLenum VulkanRenderer::ModeToGLMode(Mode mode)
//{
//	GLenum glMode = 0;
//
//	if (mode == Mode::POINTS) glMode = GL_POINTS;
//	else if (mode == Mode::LINES) glMode = GL_LINES;
//	else if (mode == Mode::LINE_LOOP) glMode = GL_LINE_LOOP;
//	else if (mode == Mode::LINE_STRIP) glMode = GL_LINE_STRIP;
//	else if (mode == Mode::TRIANGLES) glMode = GL_TRIANGLES;
//	else if (mode == Mode::TRIANGLE_STRIP) glMode = GL_TRIANGLE_STRIP;
//	else if (mode == Mode::TRIANGLE_FAN) glMode = GL_TRIANGLE_FAN;
//	else if (mode == Mode::QUADS) glMode = GL_QUADS;
//	else Logger::LogError("Unhandled Mode passed to GLRenderer: " + std::to_string((int)mode));
//
//	return glMode;
//}

VulkanRenderer::RenderObject* VulkanRenderer::GetRenderObject(int renderID)
{
	return m_RenderObjects[renderID];
}

// Vertex hash function
namespace std
{
	template<> struct hash<VertexPosColTex>
	{
		size_t operator()(VertexPosColTex const& vertex) const
		{
			return ((hash<glm::vec3>()(vertex.pos) ^
				(hash<glm::vec3>()(vertex.col) << 1)) >> 1) ^
				(hash<glm::vec2>()(vertex.uv) << 1);
		}
	};
}

#pragma region VulkanStuff
// Debug callbacks
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


void VulkanRenderer::CreateInstance()
{
	if (m_EnableValidationLayers && !CheckValidationLayerSupport())
	{
		throw std::runtime_error("validation layers requested, but not available!");
	}

	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Hello Triangle";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "No Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_0;

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

	if (vkCreateInstance(&createInfo, nullptr, m_Instance.replace()) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create instance!");
	}
}

void VulkanRenderer::SetupDebugCallback()
{
	if (!m_EnableValidationLayers) return;

	VkDebugReportCallbackCreateInfoEXT createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
	createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
	createInfo.pfnCallback = DebugCallback;

	if (CreateDebugReportCallbackEXT(m_Instance, &createInfo, nullptr, m_Callback.replace()) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to set up debug callback!");
	}
}

void VulkanRenderer::CreateSurface(Window* window)
{
	if (glfwCreateWindowSurface(m_Instance, ((VulkanWindowWrapper*)window)->GetWindow(), nullptr, surface.replace()) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create window surface!");
	}
}

void VulkanRenderer::PickPhysicalDevice()
{
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);

	if (deviceCount == 0)
	{
		throw std::runtime_error("failed to find GPUs with Vulkan support!");
	}

	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

	for (const auto& device : devices)
	{
		if (IsDeviceSuitable(device))
		{
			m_PhysicalDevice = device;
			break;
		}
	}

	if (m_PhysicalDevice == VK_NULL_HANDLE)
	{
		throw std::runtime_error("failed to find a suitable GPU!");
	}
}

void VulkanRenderer::CreateLogicalDevice()
{
	QueueFamilyIndices indices = FindQueueFamilies(m_PhysicalDevice);

	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	std::set<int> uniqueQueueFamilies = { indices.graphicsFamily, indices.presentFamily };

	float queuePriority = 1.0f;
	for (int queueFamily : uniqueQueueFamilies)
	{
		VkDeviceQueueCreateInfo queueCreateInfo = {};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = queueFamily;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;
		queueCreateInfos.push_back(queueCreateInfo);
	}

	VkPhysicalDeviceFeatures deviceFeatures = {};

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

	if (vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, m_Device.replace()) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create logical device!");
	}

	vkGetDeviceQueue(m_Device, indices.graphicsFamily, 0, &m_GraphicsQueue);
	vkGetDeviceQueue(m_Device, indices.presentFamily, 0, &m_PresentQueue);
}

void VulkanRenderer::RecreateSwapChain(Window* window)
{
	vkDeviceWaitIdle(m_Device);

	CreateSwapChain(window);
	CreateImageViews();
	CreateRenderPass();
	CreateGraphicsPipeline();
	CreateDepthResources();
	CreateFramebuffers();
	CreateCommandBuffers();
}

void VulkanRenderer::CreateSwapChain(Window* window)
{
	SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(m_PhysicalDevice);

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
	createInfo.surface = surface;

	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = surfaceFormat.format;
	createInfo.imageColorSpace = surfaceFormat.colorSpace;
	createInfo.imageExtent = extent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	QueueFamilyIndices indices = FindQueueFamilies(m_PhysicalDevice);
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
	if (vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &newSwapChain) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create swap chain!");
	}

	m_SwapChain = newSwapChain;

	vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &imageCount, nullptr);
	m_SwapChainImages.resize(imageCount);
	vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &imageCount, m_SwapChainImages.data());

	m_SwapChainImageFormat = surfaceFormat.format;
	m_SwapChainExtent = extent;
}

void VulkanRenderer::CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VDeleter<VkImageView>& imageView)
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

	if (vkCreateImageView(m_Device, &viewInfo, nullptr, imageView.replace()) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create texture image view!");
	}
}

void VulkanRenderer::CreateImageViews()
{
	m_SwapChainImageViews.resize(m_SwapChainImages.size(), VDeleter<VkImageView>{ m_Device, vkDestroyImageView });

	for (uint32_t i = 0; i < m_SwapChainImages.size(); i++)
	{
		CreateImageView(m_SwapChainImages[i], m_SwapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, m_SwapChainImageViews[i]);
	}
}

void VulkanRenderer::CreateTextureImageView()
{
	CreateImageView(m_TextureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, m_TextureImageView);
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

	if (vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, m_RenderPass.replace()) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create render pass!");
	}
}

void VulkanRenderer::CreateTextureSampler()
{
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.anisotropyEnable = true;
	samplerInfo.maxAnisotropy = 16;
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = 0.0f;

	if (vkCreateSampler(m_Device, &samplerInfo, nullptr, m_TextureSampler.replace()) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create texture sampler!");
	}
}

void VulkanRenderer::CreateGraphicsPipeline()
{
	auto vertShaderCode = ReadFile("resources/shaders/vk_vertex_pos_col_vert.spv");
	auto fragShaderCode = ReadFile("resources/shaders/vk_vertex_pos_col_frag.spv");

	VDeleter<VkShaderModule> vertShaderModule{ m_Device, vkDestroyShaderModule };
	VDeleter<VkShaderModule> fragShaderModule{ m_Device, vkDestroyShaderModule };
	CreateShaderModule(vertShaderCode, vertShaderModule);
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

	auto bindingDescription = VulkanVertex::GetVertPosColBindingDescription();
	auto attributeDescription = VulkanVertex::GetVertPosColAttributeDescriptions();

	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.vertexAttributeDescriptionCount = attributeDescription.size();
	vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
	vertexInputInfo.pVertexAttributeDescriptions = attributeDescription.data();

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;

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

	VkDescriptorSetLayout setLayouts[] = { m_DescriptorSetLayout };
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = setLayouts;

	if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, m_PipelineLayout.replace()) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create pipeline layout!");
	}

	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	depthStencil.depthBoundsTestEnable = VK_FALSE;
	depthStencil.minDepthBounds = 0.0f;
	depthStencil.maxDepthBounds = 1.0f;
	depthStencil.stencilTestEnable = VK_FALSE;
	depthStencil.front = {};
	depthStencil.back = {};

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
	pipelineInfo.layout = m_PipelineLayout;
	pipelineInfo.renderPass = m_RenderPass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, m_GraphicsPipeline.replace()) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create graphics pipeline!");
	}
}

void VulkanRenderer::CreateFramebuffers()
{
	m_SwapChainFramebuffers.resize(m_SwapChainImageViews.size(), VDeleter<VkFramebuffer>{m_Device, vkDestroyFramebuffer});

	for (size_t i = 0; i < m_SwapChainImageViews.size(); i++)
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

		if (vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr, m_SwapChainFramebuffers[i].replace()) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to create framebuffer!");
		}
	}
}

void VulkanRenderer::CreateCommandPool()
{
	QueueFamilyIndices queueFamilyIndices = FindQueueFamilies(m_PhysicalDevice);

	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;

	if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, m_CommandPool.replace()) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create command pool!");
	}
}

VkCommandBuffer VulkanRenderer::BeginSingleTimeCommands()
{
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = m_CommandPool;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	vkAllocateCommandBuffers(m_Device, &allocInfo, &commandBuffer);

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

	vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &commandBuffer);
}

void VulkanRenderer::CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
	VkMemoryPropertyFlags properties, VDeleter<VkImage>& image, VDeleter<VkDeviceMemory>& imageMemory)
{
	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
	imageInfo.tiling = tiling;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
	imageInfo.usage = usage;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateImage(m_Device, &imageInfo, nullptr, image.replace()) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create image!");
	}

	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(m_Device, image, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

	if (vkAllocateMemory(m_Device, &allocInfo, nullptr, imageMemory.replace()) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to allocate image memory!");
	}

	vkBindImageMemory(m_Device, image, imageMemory, 0);
}

VkFormat VulkanRenderer::FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
{
	for (VkFormat format : candidates)
	{
		VkFormatProperties properties;
		vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, format, &properties);

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
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_DepthImage, m_DepthImageMemory);
	CreateImageView(m_DepthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, m_DepthImageView);

	TransitionImageLayout(m_DepthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}

void VulkanRenderer::CreateTextureImage()
{
	int textureWidth, textureHeight, textureChannels;
	//stbi_uc* pixels = stbi_load(MODEL_TEXTURE_PATH.c_str(), &textureWidth, &textureHeight, &textureChannels, STBI_rgb_alpha);
	unsigned char* pixels = SOIL_load_image(MODEL_TEXTURE_PATH.c_str(), &textureWidth, &textureHeight, &textureChannels, SOIL_LOAD_RGBA);

	if (!pixels)
	{
		throw std::runtime_error("failed to load texture image!");
	}

	VkDeviceSize imageSize = textureWidth * textureHeight * 4;

	VDeleter<VkImage> stagingImage{ m_Device, vkDestroyImage };
	VDeleter<VkDeviceMemory> stagingImageMemory{ m_Device, vkFreeMemory };

	CreateImage(textureWidth, textureHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingImage, stagingImageMemory);

	VkImageSubresource subresource = {};
	subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresource.mipLevel = 0;
	subresource.arrayLayer = 0;

	VkSubresourceLayout stagingImageLayout;
	vkGetImageSubresourceLayout(m_Device, stagingImage, &subresource, &stagingImageLayout);

	void* data;
	vkMapMemory(m_Device, stagingImageMemory, 0, imageSize, 0, &data);

	if (stagingImageLayout.rowPitch == textureWidth * 4)
	{
		memcpy(data, pixels, (size_t)imageSize);
	}
	else
	{
		uint8_t* dataBytes = reinterpret_cast<uint8_t*>(data);

		for (int y = 0; y < textureHeight; y++)
		{
			memcpy(&dataBytes[y * stagingImageLayout.rowPitch],
				&pixels[y * textureWidth * 4],
				textureWidth * 4);
		}
	}

	vkUnmapMemory(m_Device, stagingImageMemory);

	SOIL_free_image_data(pixels);

	CreateImage(textureWidth, textureHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_TextureImage, m_TextureImageMemory);

	TransitionImageLayout(stagingImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	TransitionImageLayout(m_TextureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	CopyImage(stagingImage, m_TextureImage, textureWidth, textureHeight);

	TransitionImageLayout(m_TextureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void VulkanRenderer::CreateCommandBuffers()
{
	for (RenderObject* renderObject : m_RenderObjects)
	{
		if (renderObject->commandBuffers.size() > 0)
		{
			vkFreeCommandBuffers(m_Device, m_CommandPool, renderObject->commandBuffers.size(), renderObject->commandBuffers.data());
		}

		// One command buffer per frame back buffer
		renderObject->commandBuffers.resize(m_SwapChainFramebuffers.size());

		VkCommandBufferAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = m_CommandPool;
		allocInfo.commandBufferCount = (uint32_t)renderObject->commandBuffers.size();

		if (vkAllocateCommandBuffers(m_Device, &allocInfo, renderObject->commandBuffers.data()) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to allocate command buffers!");
		}

		for (size_t i = 0; i < renderObject->commandBuffers.size(); i++)
		{
			VkCommandBufferBeginInfo beginInfo = {};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

			vkBeginCommandBuffer(renderObject->commandBuffers[i], &beginInfo);

			VkRenderPassBeginInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = m_RenderPass;
			renderPassInfo.framebuffer = m_SwapChainFramebuffers[i];
			renderPassInfo.renderArea.offset = { 0, 0 };
			renderPassInfo.renderArea.extent = m_SwapChainExtent;

			std::array<VkClearValue, 2> clearValues = {};
			clearValues[0].color = m_ClearColor;
			clearValues[1].depthStencil = { 1.0f, 0 };

			renderPassInfo.clearValueCount = clearValues.size();
			renderPassInfo.pClearValues = clearValues.data();

			vkCmdBeginRenderPass(renderObject->commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindPipeline(renderObject->commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);

			std::vector<VkBuffer> vertexBuffers = { renderObject->vertexBuffer };

			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(renderObject->commandBuffers[i], 0, 1, vertexBuffers.data(), offsets);

			VkBuffer indexBuffer = renderObject->indexBuffer;
			if (renderObject->indices && !renderObject->indices->empty())
			{
				vkCmdBindIndexBuffer(renderObject->commandBuffers[i], indexBuffer, 0, VK_INDEX_TYPE_UINT32);
			}

			vkCmdBindDescriptorSets(renderObject->commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 1, &m_DescriptorSet, 0, nullptr);

			if (renderObject->indexed)
			{
				vkCmdDrawIndexed(renderObject->commandBuffers[i], renderObject->indices->size(), 1, 0, 0, 0);
			}
			else
			{
				vkCmdDraw(renderObject->commandBuffers[i], renderObject->vertices->size(), 1, 0, 0);
			}

			vkCmdEndRenderPass(renderObject->commandBuffers[i]);

			if (vkEndCommandBuffer(renderObject->commandBuffers[i]) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to record command buffer!");
			}
		}
	}
}

uint32_t VulkanRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties);

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
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

void VulkanRenderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
	VDeleter<VkBuffer>& buffer, VDeleter<VkDeviceMemory>& bufferMemory)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateBuffer(m_Device, &bufferInfo, nullptr, buffer.replace()) != VK_SUCCESS)
	{
		std::runtime_error("failed to create buffer!");
	}

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(m_Device, buffer, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

	if (vkAllocateMemory(m_Device, &allocInfo, nullptr, bufferMemory.replace()) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to allocate buffer memory!");
	}

	vkBindBufferMemory(m_Device, buffer, bufferMemory, 0);
}

void VulkanRenderer::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
	VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

	VkBufferCopy copyRegion = {};
	copyRegion.size = size;
	vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

	EndSingleTimeCommands(commandBuffer);
}

void VulkanRenderer::LoadModel(const std::string& filePath)
{
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string err;

	std::unordered_map<VertexPosColTex, int> uniqueVertices = {};

	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &err, filePath.c_str()))
	{
		throw std::runtime_error(err);
	}

	//for (const auto& shape : shapes)
	//{
	//	for (const auto& index : shape.mesh.indices)
	//	{
	//		Vertex vertex = {};
	//
	//		vertex.pos = {
	//			attrib.vertices[3 * index.vertex_index + 0],
	//			attrib.vertices[3 * index.vertex_index + 1],
	//			attrib.vertices[3 * index.vertex_index + 2]
	//		};
	//
	//		vertex.texCoord = {
	//			attrib.texcoords[2 * index.texcoord_index + 0],
	//			1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
	//		};
	//
	//		vertex.color = { 1.0f, 1.0f, 1.0f };
	//
	//		if (uniqueVertices.count(vertex) == 0)
	//		{
	//			uniqueVertices[vertex] = vertices.size();
	//			vertices.push_back(vertex);
	//		}
	//
	//		indices.push_back(uniqueVertices[vertex]);
	//	}
	//}
}

void VulkanRenderer::CreateVertexBuffer(glm::uint renderID)
{
	RenderObject* renderObject = GetRenderObject(renderID);
	const size_t bufferSize = sizeof(renderObject->vertices[0]) * renderObject->vertices->size();

	VDeleter<VkBuffer> stagingBuffer{ m_Device, vkDestroyBuffer };
	VDeleter<VkDeviceMemory> stagingBufferMemory{ m_Device, vkFreeMemory };
	CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

	void* data;
	vkMapMemory(m_Device, stagingBufferMemory, 0, bufferSize, 0, &data);
	memcpy(data, renderObject->vertices->data(), bufferSize);
	vkUnmapMemory(m_Device, stagingBufferMemory);

	CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, renderObject->vertexBuffer, renderObject->vertexBufferMemory);

	CopyBuffer(stagingBuffer, renderObject->vertexBuffer, bufferSize);
}

void VulkanRenderer::CreateIndexBuffer(glm::uint renderID)
{
	RenderObject* renderObject = GetRenderObject(renderID);

	if (!renderObject->indices || renderObject->indices->empty()) return;
	const size_t bufferSize = sizeof(renderObject->indices[0]) * renderObject->indices->size();

	VDeleter<VkBuffer> stagingBuffer{ m_Device, vkDestroyBuffer };
	VDeleter<VkDeviceMemory> stagingBufferMemory{ m_Device, vkFreeMemory };
	CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

	void* data;
	vkMapMemory(m_Device, stagingBufferMemory, 0, bufferSize, 0, &data);
	memcpy(data, renderObject->indices->data(), bufferSize);
	vkUnmapMemory(m_Device, stagingBufferMemory);

	CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, renderObject->indexBuffer, renderObject->indexBufferMemory);

	CopyBuffer(stagingBuffer, renderObject->indexBuffer, bufferSize);
}

void VulkanRenderer::CreateUniformBuffer()
{
	VkDeviceSize bufferSize = sizeof(UniformBufferObject);

	CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		m_UniformStagingBuffer, m_UniformStagingBufferMemory);
	CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		m_UniformBuffer, m_UniformBufferMemory);
}

void VulkanRenderer::CreateDescriptorPool()
{
	std::array<VkDescriptorPoolSize, 2> poolSizes = {};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[0].descriptorCount = 1;
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[1].descriptorCount = 1;

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = poolSizes.size();
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = 1;

	if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, m_DescriptorPool.replace()) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create descriptor pool!");
	}
}

void VulkanRenderer::CreateDescriptorSet()
{
	VkDescriptorSetLayout layouts[] = { m_DescriptorSetLayout };
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_DescriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = layouts;

	if (vkAllocateDescriptorSets(m_Device, &allocInfo, &m_DescriptorSet) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to allocate descriptor set!");
	}

	VkDescriptorBufferInfo bufferInfo = {};
	bufferInfo.buffer = m_UniformBuffer;
	bufferInfo.offset = 0;
	bufferInfo.range = sizeof(UniformBufferObject);

	VkDescriptorImageInfo imageInfo = {};
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfo.imageView = m_TextureImageView;
	imageInfo.sampler = m_TextureSampler;

	std::array<VkWriteDescriptorSet, 2> descriptorWrites = {};

	descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[0].dstSet = m_DescriptorSet;
	descriptorWrites[0].dstBinding = 0;
	descriptorWrites[0].dstArrayElement = 0;
	descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrites[0].descriptorCount = 1;
	descriptorWrites[0].pBufferInfo = &bufferInfo;

	descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[1].dstSet = m_DescriptorSet;
	descriptorWrites[1].dstBinding = 1;
	descriptorWrites[1].dstArrayElement = 0;
	descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[1].descriptorCount = 1;
	descriptorWrites[1].pImageInfo = &imageInfo;

	vkUpdateDescriptorSets(m_Device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
}

void VulkanRenderer::CreateSemaphores()
{
	VkSemaphoreCreateInfo semaphoreInfo = {};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	if (vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, m_ImageAvailableSemaphore.replace()) != VK_SUCCESS ||
		vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, m_RenderFinishedSemaphore.replace()) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create semaphores!");
	}
}

void VulkanRenderer::CreateDescriptorSetLayout()
{
	VkDescriptorSetLayoutBinding uboLayoutBinding = {};
	uboLayoutBinding.binding = 0;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	uboLayoutBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
	samplerLayoutBinding.binding = 1;
	samplerLayoutBinding.descriptorCount = 1;
	samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerLayoutBinding.pImmutableSamplers = nullptr;
	samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	std::array<VkDescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, samplerLayoutBinding };
	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = bindings.size();
	layoutInfo.pBindings = bindings.data();

	if (vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, m_DescriptorSetLayout.replace()) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create descriptor set layout!");
	}
}

void VulkanRenderer::DrawFrame(Window* window)
{
	uint32_t imageIndex;
	VkResult result = vkAcquireNextImageKHR(m_Device, m_SwapChain, std::numeric_limits<uint64_t>::max(), m_ImageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

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



	// TODO: FIXME: Is this what you do with command buffers??
	// One per object or what?


	submitInfo.commandBufferCount = m_RenderObjects.size();
	std::vector<std::vector<VkCommandBuffer>> commandBuffers;
	for (size_t i = 0; i < m_RenderObjects[0]->commandBuffers.size(); ++i)
	{
		commandBuffers.push_back({});
		for (RenderObject* renderObject : m_RenderObjects)
		{
			commandBuffers[commandBuffers.size() - 1].push_back(renderObject->commandBuffers[i]);
		}
	}
	submitInfo.pCommandBuffers = commandBuffers[imageIndex].data();

	VkSemaphore signalSemaphores[] = { m_RenderFinishedSemaphore };
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;

	if (vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to submit draw command buffer!");
	}

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
	else if (result != VK_SUCCESS)
	{
		throw std::runtime_error("failed to present swap chain image!");
	}
}

void VulkanRenderer::CreateShaderModule(const std::vector<char>& code, VDeleter<VkShaderModule>& shaderModule)
{
	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = code.size();
	createInfo.pCode = (uint32_t*)code.data();

	if (vkCreateShaderModule(m_Device, &createInfo, nullptr, shaderModule.replace()) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create shader module!");
	}
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
	VkPresentModeKHR bestMode = VK_PRESENT_MODE_FIFO_KHR;

	for (const auto& availablePresentMode : availablePresentModes)
	{
		if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
		{
			return availablePresentMode;
		}
		else if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR)
		{
			bestMode = availablePresentMode;
		}
	}

	return bestMode;
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

SwapChainSupportDetails VulkanRenderer::QuerySwapChainSupport(VkPhysicalDevice device)
{
	SwapChainSupportDetails details;

	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

	uint32_t formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

	if (formatCount != 0)
	{
		details.formats.resize(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
	}

	uint32_t presentModeCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

	if (presentModeCount != 0)
	{
		details.presentModes.resize(presentModeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
	}

	return details;
}

bool VulkanRenderer::IsDeviceSuitable(VkPhysicalDevice device)
{
	QueueFamilyIndices indices = FindQueueFamilies(device);

	bool extensionsSupported = CheckDeviceExtensionSupport(device);

	bool swapChainAdequate = false;
	if (extensionsSupported)
	{
		SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(device);
		swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
	}

	return indices.isComplete() && extensionsSupported && swapChainAdequate;
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

QueueFamilyIndices VulkanRenderer::FindQueueFamilies(VkPhysicalDevice device)
{
	QueueFamilyIndices indices;

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

	int i = 0;
	for (const auto& queueFamily : queueFamilies)
	{
		if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			indices.graphicsFamily = i;
		}

		VkBool32 presentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

		if (queueFamily.queueCount > 0 && presentSupport)
		{
			indices.presentFamily = i;
		}

		if (indices.isComplete())
		{
			break;
		}

		i++;
	}

	return indices;
}

std::vector<const char*> VulkanRenderer::GetRequiredExtensions()
{
	std::vector<const char*> extensions;

	unsigned int glfwExtensionCount = 0;
	const char** glfwExtensions;
	glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	for (unsigned int i = 0; i < glfwExtensionCount; i++)
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

void VulkanRenderer::UpdateUniformBuffer(const GameContext& gameContext, const UniformBufferObject& ubo)
{
	//UniformBufferObject ubo = {};
	//float degreesPerSecond = 90.0f;
	//ubo.model = glm::rotate(glm::mat4(), gameContext.elapsedTime * glm::radians(degreesPerSecond), glm::vec3(0.0f, 0.0f, 1.0f));
	//
	//const glm::vec3 eyePos(2.0f, 2.0f, 2.0f);
	//ubo.view = glm::lookAt(eyePos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	//
	//const float FOVdegrees = 45.0f;
	//const float nearPlane = 0.1f;
	//const float farPlane = 10.0f;
	//ubo.proj = glm::perspective(glm::radians(FOVdegrees), swapChainExtent.width / (float)swapChainExtent.height, nearPlane, farPlane);
	//ubo.proj[1][1] *= -1;
	//
	//void* data;
	//vkMapMemory(device, uniformStagingBufferMemory, 0, sizeof(ubo), 0, &data);
	//memcpy(data, &ubo, sizeof(ubo));
	//vkUnmapMemory(device, uniformStagingBufferMemory);

	CopyBuffer(m_UniformStagingBuffer, m_UniformBuffer, sizeof(ubo));
}

std::vector<char> VulkanRenderer::ReadFile(const std::string& filename)
{
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	if (!file.is_open())
	{
		throw std::runtime_error("failed to open file!");
	}

	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);

	file.close();

	return buffer;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanRenderer::DebugCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType,
	uint64_t obj, size_t location, int32_t code, const char* layerPrefix, const char* msg, void* userData)
{
	std::cerr << "validation layer: " << msg << std::endl;

	return VK_FALSE;
}
#pragma endregion

#pragma region Vertex
VkVertexInputBindingDescription VulkanVertex::GetVertPosColTexBindingDescription()
{
	VkVertexInputBindingDescription bindingDesc = {};
	bindingDesc.binding = 0;
	bindingDesc.stride = sizeof(VertexPosColTex);
	bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	return bindingDesc;
}

VkVertexInputBindingDescription VulkanVertex::GetVertPosColBindingDescription()
{
	VkVertexInputBindingDescription bindingDesc = {};
	bindingDesc.binding = 0;
	bindingDesc.stride = sizeof(VertexPosCol);
	bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	return bindingDesc;
}

std::array<VkVertexInputAttributeDescription, 3> VulkanVertex::GetVertPosColTexAttributeDescriptions()
{
	std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions;

	attributeDescriptions[0].binding = 0;
	attributeDescriptions[0].location = 0;
	attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	attributeDescriptions[0].offset = offsetof(VertexPosColTex, pos);

	attributeDescriptions[1].binding = 0;
	attributeDescriptions[1].location = 1;
	attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
	attributeDescriptions[1].offset = offsetof(VertexPosColTex, col);

	attributeDescriptions[2].binding = 0;
	attributeDescriptions[2].location = 2;
	attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
	attributeDescriptions[2].offset = offsetof(VertexPosColTex, uv);

	return attributeDescriptions;
}

std::array<VkVertexInputAttributeDescription, 2> VulkanVertex::GetVertPosColAttributeDescriptions()
{
	std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions;

	attributeDescriptions[0].binding = 0;
	attributeDescriptions[0].location = 0;
	attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	attributeDescriptions[0].offset = offsetof(VertexPosCol, pos);

	attributeDescriptions[1].binding = 0;
	attributeDescriptions[1].location = 1;
	attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
	attributeDescriptions[1].offset = offsetof(VertexPosCol, col);

	return attributeDescriptions;
}

//bool VulkanVertex::operator==(const VulkanVertex& other) const
//{
//	return pos == other.pos && color == other.color && texCoord == other.texCoord;
//}
#pragma endregion // Vertex

#pragma region VDeleter
template<typename T>
VDeleter<T>::VDeleter() :
	VDeleter([](T, VkAllocationCallbacks*) {})
{
}

template<typename T>
VDeleter<T>::VDeleter(std::function<void(T, VkAllocationCallbacks*)> deletef)
{
	this->deleter = [=](T obj) { deletef(obj, nullptr); };
}

template<typename T>
VDeleter<T>::VDeleter(const VDeleter<VkInstance>& instance, std::function<void(VkInstance, T, VkAllocationCallbacks*)> deletef)
{
	this->deleter = [&instance, deletef](T obj) { deletef(instance, obj, nullptr); };
}

template<typename T>
VDeleter<T>::VDeleter(const VDeleter<VkDevice>& device, std::function<void(VkDevice, T, VkAllocationCallbacks*)> deletef)
{
	this->deleter = [&device, deletef](T obj) { deletef(device, obj, nullptr); };
}

template<typename T>
VDeleter<T>::~VDeleter()
{
	cleanup();
}

template<typename T>
const T* VDeleter<T>::operator &() const
{
	return &object;
}

template<typename T>
T* VDeleter<T>::replace()
{
	cleanup();
	return &object;
}

template<typename T>
VDeleter<T>::operator T() const
{
	return object;
}

template<typename T>
void VDeleter<T>::operator=(T rhs)
{
	if (rhs != object)
	{
		cleanup();
		object = rhs;
	}
}

template<typename T>
template<typename V>
inline bool VDeleter<T>::operator==(V rhs)
{
	return false;
}

template<typename T>
void VDeleter<T>::cleanup()
{
	if (object != VK_NULL_HANDLE)
	{
		deleter(object);
	}
	object = VK_NULL_HANDLE;
}
#pragma endregion // VDeleter

#endif // COMPILE_VULKAN