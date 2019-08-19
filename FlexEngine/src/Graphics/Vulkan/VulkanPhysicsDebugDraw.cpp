#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanPhysicsDebugDraw.hpp"

#include "Cameras/BaseCamera.hpp"
#include "Graphics/Renderer.hpp"
#include "Graphics/VertexAttribute.hpp"
#include "Graphics/Vulkan/VulkanHelpers.hpp"
#include "Graphics/Vulkan/VulkanRenderer.hpp"
#include "Profiler.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	namespace vk
	{
		VulkanPhysicsDebugDraw::VulkanPhysicsDebugDraw()
		{
		}

		VulkanPhysicsDebugDraw::~VulkanPhysicsDebugDraw()
		{
		}

		void VulkanPhysicsDebugDraw::Initialize()
		{
			m_Renderer = (VulkanRenderer*)(g_Renderer);
			const std::string debugMatName = "Debug";
			if (!m_Renderer->GetMaterialID(debugMatName, m_MaterialID))
			{
				MaterialCreateInfo debugMatCreateInfo = {};
				debugMatCreateInfo.shaderName = "color";
				debugMatCreateInfo.name = debugMatName;
				debugMatCreateInfo.persistent = true;
				debugMatCreateInfo.visibleInEditor = true;
				m_MaterialID = g_Renderer->InitializeMaterial(&debugMatCreateInfo);
			}

			RenderObjectCreateInfo createInfo = {};
			createInfo.materialID = m_MaterialID;
			createInfo.bEditorObject = true;
			createInfo.bDepthWriteEnable = false;
			m_Object = new GameObject("Vk Physics Debug Draw", GameObjectType::_NONE);
			m_ObjectMesh = m_Object->SetMeshComponent(new MeshComponent(m_Object, m_MaterialID));
			const VertexAttributes vertexAttributes = (u32)VertexAttribute::POSITION | (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;
			if (!m_ObjectMesh->CreateProcedural(8192, vertexAttributes, TopologyMode::LINE_LIST))
			{
				PrintWarn("Vulkan physics debug renderer failed to initialize vertex buffer");
			}
			g_SceneManager->CurrentScene()->AddRootObject(m_Object);
		}

		void VulkanPhysicsDebugDraw::Destroy()
		{
		}

		void VulkanPhysicsDebugDraw::reportErrorWarning(const char* warningString)
		{
			PrintError("VulkanPhysicsDebugDraw: %s\n", warningString);
		}

		void VulkanPhysicsDebugDraw::draw3dText(const btVector3& location, const char* textString)
		{
			UNREFERENCED_PARAMETER(location);
			UNREFERENCED_PARAMETER(textString);
		}

		void VulkanPhysicsDebugDraw::setDebugMode(int debugMode)
		{
			UNREFERENCED_PARAMETER(debugMode);
			// NOTE: Call UpdateDebugMode instead of this
		}

		int VulkanPhysicsDebugDraw::getDebugMode() const
		{
			return m_DebugMode;
		}

		void VulkanPhysicsDebugDraw::drawLine(const btVector3& from, const btVector3& to, const btVector3& color)
		{
			m_LineSegments.push_back(LineSegment{ from, to, color });
		}

		void VulkanPhysicsDebugDraw::drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& color)
		{
			UNREFERENCED_PARAMETER(PointOnB);
			UNREFERENCED_PARAMETER(normalOnB);
			UNREFERENCED_PARAMETER(distance);
			UNREFERENCED_PARAMETER(lifeTime);
			UNREFERENCED_PARAMETER(color);
			// TODO: FIXME: UNIMPLEMENTED: Implement me (or don't)
		}

		void VulkanPhysicsDebugDraw::DrawLineWithAlpha(const btVector3& from, const btVector3& to, const btVector4& color)
		{
			m_LineSegments.push_back(LineSegment{ from, to, color });
		}

		void VulkanPhysicsDebugDraw::Draw()
		{
			if (m_LineSegments.empty())
			{
				return;
			}

			{
				PROFILE_AUTO("PhysicsDebugRender > Update vertex buffer");

				m_VertexBufferCreateInfo.positions_3D.clear();
				m_VertexBufferCreateInfo.colors_R32G32B32A32.clear();

				m_VertexBufferCreateInfo.positions_3D.resize(m_LineSegments.size() * 2);
				m_VertexBufferCreateInfo.colors_R32G32B32A32.resize(m_LineSegments.size() * 2);

				i32 i = 0;
				for (LineSegment& line : m_LineSegments)
				{
					*(m_VertexBufferCreateInfo.positions_3D.data() + i) = (ToVec3(line.start));
					*(m_VertexBufferCreateInfo.positions_3D.data() + i + 1) = (ToVec3(line.end));

					glm::vec4 color(ToVec3(line.color), 1.0f);
					*(m_VertexBufferCreateInfo.colors_R32G32B32A32.data() + i) = (color);
					*(m_VertexBufferCreateInfo.colors_R32G32B32A32.data() + i + 1) = (color);

					i += 2;
				}

				m_ObjectMesh->UpdateProceduralData(&m_VertexBufferCreateInfo);
			}
		}
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN