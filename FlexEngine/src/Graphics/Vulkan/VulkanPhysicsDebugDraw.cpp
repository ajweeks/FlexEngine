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
#include "Scene/Mesh.hpp"
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
			if (!m_Renderer->FindOrCreateMaterialByName(debugMatName, m_MaterialID))
			{
				MaterialCreateInfo debugMatCreateInfo = {};
				debugMatCreateInfo.shaderName = "color";
				debugMatCreateInfo.name = debugMatName;
				debugMatCreateInfo.persistent = true;
				debugMatCreateInfo.visibleInEditor = true;
				debugMatCreateInfo.bDynamic = true;
				m_MaterialID = g_Renderer->InitializeMaterial(&debugMatCreateInfo);
			}

			CreateDebugObject();
		}

		void VulkanPhysicsDebugDraw::Destroy()
		{
		}

		void VulkanPhysicsDebugDraw::OnPostSceneChange()
		{
			CreateDebugObject();
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
			if (m_LineSegmentIndex < MAX_NUM_LINE_SEGMENTS)
			{
				m_LineSegments[m_LineSegmentIndex++] = { from, to, color };
			}
			else
			{
				PrintWarn("Max number of debug draw lines reached (%d)\n", MAX_NUM_LINE_SEGMENTS);
			}
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
			if (m_LineSegmentIndex < MAX_NUM_LINE_SEGMENTS)
			{
				m_LineSegments[m_LineSegmentIndex++] = { from, to, color };
			}
			else
			{
				PrintWarn("Max number of debug draw lines reached (%d)\n", MAX_NUM_LINE_SEGMENTS);
			}
		}

		void VulkanPhysicsDebugDraw::Draw()
		{
			if (m_LineSegmentIndex == 0)
			{
				return;
			}

			{
				PROFILE_AUTO("PhysicsDebugRender > Update vertex buffer");

				m_VertexBufferCreateInfo.positions_3D.clear();
				m_VertexBufferCreateInfo.colors_R32G32B32A32.clear();

				u32 numVerts = m_LineSegmentIndex * 2;

				if (m_VertexBufferCreateInfo.positions_3D.capacity() < numVerts)
				{
					m_VertexBufferCreateInfo.positions_3D.resize(numVerts * 2);
					m_VertexBufferCreateInfo.colors_R32G32B32A32.resize(numVerts * 2);
				}

				i32 i = 0;
				glm::vec3* posData = m_VertexBufferCreateInfo.positions_3D.data();
				glm::vec4* colData = m_VertexBufferCreateInfo.colors_R32G32B32A32.data();
				for (u32 li = 0; li < m_LineSegmentIndex; ++li)
				{
					memcpy(posData + i, m_LineSegments[li].start, sizeof(real) * 3);
					memcpy(posData + i + 1, m_LineSegments[li].end, sizeof(real) * 3);

					memcpy(colData + i, m_LineSegments[li].color, sizeof(real) * 4);
					memcpy(colData + i + 1, m_LineSegments[li].color, sizeof(real) * 4);

					i += 2;
				}

				m_ObjectMesh->GetSubMeshes()[0]->UpdateProceduralData(&m_VertexBufferCreateInfo);
			}
		}

		void VulkanPhysicsDebugDraw::CreateDebugObject()
		{
			if (m_Object != nullptr)
			{
				// Object will have been destroyed by scene?
				m_Object = nullptr;
				m_ObjectMesh = nullptr;
			}

			RenderObjectCreateInfo createInfo = {};
			createInfo.materialID = m_MaterialID;
			createInfo.bEditorObject = true;
			createInfo.bDepthWriteEnable = false;
			m_Object = new GameObject("Vk Physics Debug Draw", GameObjectType::_NONE);
			m_Object->SetSerializable(false);
			m_Object->SetVisibleInSceneExplorer(false);
			m_ObjectMesh = m_Object->SetMesh(new Mesh(m_Object));
			const VertexAttributes vertexAttributes = (u32)VertexAttribute::POSITION | (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;
			if (!m_ObjectMesh->CreateProcedural(16384 * 4, vertexAttributes, m_MaterialID, TopologyMode::LINE_LIST, &createInfo))
			{
				PrintWarn("Vulkan physics debug renderer failed to initialize vertex buffer");
			}
			g_SceneManager->CurrentScene()->AddRootObject(m_Object);
		}
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN