#include "stdafx.h"

#include "Graphics/VulkanRenderer.h"
#include "GameContext.h"
#include "Window/Window.h"
#include "Logger.h"
#include "FreeCamera.h"

#include <algorithm>	

#include <glm/gtx/hash.hpp>

using namespace glm;

VulkanRenderer::VulkanRenderer(GameContext& gameContext) :
	Renderer(gameContext)
	//m_Program(gameContext.program)
{
	//glClearColor(0.08f, 0.13f, 0.2f, 1.0f);
	//
	//glEnable(GL_DEPTH_TEST);
	//glDepthFunc(GL_LESS);
	//
	//glUseProgram(gameContext.program);

	//LoadAndBindGLTexture("resources/images/test2.jpg", m_TextureID);
	//glUniform1i(glGetUniformLocation(m_ProgramID, "texTest"), 0);
}

VulkanRenderer::~VulkanRenderer()
{
	//for (size_t i = 0; i < m_RenderObjects.size(); i++)
	//{
	//	glDeleteBuffers(1, &m_RenderObjects[i]->VBO);
	//	if (m_RenderObjects[i]->indexed)
	//	{
	//		glDeleteBuffers(1, &m_RenderObjects[i]->IBO);
	//	}
	//
	//	delete m_RenderObjects[i];
	//}
	//m_RenderObjects.clear();
	//
	//glDeleteProgram(m_Program);
	//glDisableVertexAttribArray(1);
	//glDisableVertexAttribArray(0);
	//glBindBuffer(GL_ARRAY_BUFFER, 0);
	//glBindVertexArray(0);

	glfwTerminate();
}

glm::uint VulkanRenderer::Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices)
{
	return 0;
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
	return 0;
}

void VulkanRenderer::Draw(glm::uint renderID)
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
	glfwSwapInterval(enableVSync ? 1 : 0);
}

void VulkanRenderer::Clear(int flags)
{
	//GLbitfield mask = 0;
	//if ((int)flags & (int)ClearFlag::COLOR) mask |= GL_COLOR_BUFFER_BIT;
	//if ((int)flags & (int)ClearFlag::DEPTH) mask |= GL_DEPTH_BUFFER_BIT;
	//glClear(mask);
}

void VulkanRenderer::SwapBuffers(const GameContext& gameContext)
{
	glfwSwapBuffers(gameContext.window->IsGLFWWindow());
}

void VulkanRenderer::UpdateTransformMatrix(const GameContext& gameContext, glm::uint renderID, const glm::mat4x4& model)
{
	//RenderObject* renderObject = GetRenderObject(renderID);
	//
	//glm::mat4 MVP = gameContext.camera->GetViewProjection() * model;
	//glUniformMatrix4fv(renderObject->MVP, 1, false, &MVP[0][0]);
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
	template<> struct hash<Vertex>
	{
		size_t operator()(Vertex const& vertex) const
		{
			return ((hash<glm::vec3>()(vertex.pos) ^
				(hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
				(hash<glm::vec2>()(vertex.texCoord) << 1);
		}
	};
}

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

#pragma region Vertex
VkVertexInputBindingDescription Vertex::getBindingDescription()
{
	VkVertexInputBindingDescription bindingDesc = {};
	bindingDesc.binding = 0;
	bindingDesc.stride = sizeof(Vertex);
	bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	return bindingDesc;
}

std::array<VkVertexInputAttributeDescription, 3> Vertex::getAttributeDescriptions()
{
	std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions;

	attributeDescriptions[0].binding = 0;
	attributeDescriptions[0].location = 0;
	attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	attributeDescriptions[0].offset = offsetof(Vertex, pos);

	attributeDescriptions[1].binding = 0;
	attributeDescriptions[1].location = 1;
	attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
	attributeDescriptions[1].offset = offsetof(Vertex, color);

	attributeDescriptions[2].binding = 0;
	attributeDescriptions[2].location = 2;
	attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
	attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

	return attributeDescriptions;
}

bool Vertex::operator==(const Vertex& other) const
{
	return pos == other.pos && color == other.color && texCoord == other.texCoord;
}
#pragma endregion

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
#pragma endregion