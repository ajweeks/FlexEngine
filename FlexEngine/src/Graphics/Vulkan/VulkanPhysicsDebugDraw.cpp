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
		VulkanPhysicsDebugDraw::VulkanPhysicsDebugDraw() :
			m_VertexBufferCreateInfo({})
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
			FLEX_UNUSED(location);
			FLEX_UNUSED(textString);
		}

		void VulkanPhysicsDebugDraw::setDebugMode(int debugMode)
		{
			FLEX_UNUSED(debugMode);
			// NOTE: Call UpdateDebugMode instead of this
		}

		int VulkanPhysicsDebugDraw::getDebugMode() const
		{
			return m_DebugMode;
		}

		void VulkanPhysicsDebugDraw::drawLine(const btVector3& from, const btVector3& to, const btVector3& color)
		{
			drawLine(from, to, color, color);
		}

		void VulkanPhysicsDebugDraw::drawLine(const btVector3& from, const btVector3& to, const btVector3& colorFrom, const btVector3& colorTo)
		{
			if (m_LineSegmentIndex < MAX_NUM_LINE_SEGMENTS)
			{
				m_LineSegments[m_LineSegmentIndex++] = { from, to, colorFrom, colorTo };
			}
			else
			{
				PrintWarn("Max number of debug draw lines reached (%d)\n", MAX_NUM_LINE_SEGMENTS);
			}
		}

		void VulkanPhysicsDebugDraw::drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& color)
		{
			FLEX_UNUSED(PointOnB);
			FLEX_UNUSED(normalOnB);
			FLEX_UNUSED(distance);
			FLEX_UNUSED(lifeTime);
			FLEX_UNUSED(color);
		}

		void VulkanPhysicsDebugDraw::DrawLineWithAlpha(const btVector3& from, const btVector3& to, const btVector4& color)
		{
			DrawLineWithAlpha(from, to, color, color);
		}

		void VulkanPhysicsDebugDraw::DrawLineWithAlpha(const btVector3& from, const btVector3& to, const btVector4& colorFrom, const btVector4& colorTo)
		{
			if (m_LineSegmentIndex < MAX_NUM_LINE_SEGMENTS)
			{
				m_LineSegments[m_LineSegmentIndex++] = { from, to, colorFrom, colorTo };
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
				indexBuffer.clear();

				u32 numVerts = m_LineSegmentIndex * 2;

				if (m_VertexBufferCreateInfo.positions_3D.capacity() < numVerts)
				{
					m_VertexBufferCreateInfo.positions_3D.resize(numVerts * 2);
					m_VertexBufferCreateInfo.colors_R32G32B32A32.resize(numVerts * 2);
					indexBuffer.resize(numVerts * 2);
				}
				else
				{
					m_VertexBufferCreateInfo.positions_3D.resize(numVerts);
					m_VertexBufferCreateInfo.colors_R32G32B32A32.resize(numVerts);
					indexBuffer.resize(numVerts);
				}

				i32 i = 0;
				glm::vec3* posData = m_VertexBufferCreateInfo.positions_3D.data();
				glm::vec4* colData = m_VertexBufferCreateInfo.colors_R32G32B32A32.data();
				u32* idxData = indexBuffer.data();
				for (u32 li = 0; li < m_LineSegmentIndex; ++li)
				{
					memcpy(posData + i, m_LineSegments[li].start, sizeof(real) * 3);
					memcpy(posData + i + 1, m_LineSegments[li].end, sizeof(real) * 3);

					memcpy(colData + i, m_LineSegments[li].colorFrom, sizeof(real) * 4);
					memcpy(colData + i + 1, m_LineSegments[li].colorTo, sizeof(real) * 4);

					u32 idx0 = li * 2;
					u32 idx1 = li * 2 + 1;
					memcpy(idxData + i, &idx0, sizeof(u32));
					memcpy(idxData + i + 1, &idx1, sizeof(u32));

					i += 2;
				}

				m_ObjectMesh->GetSubMeshes()[0]->UpdateProceduralData(&m_VertexBufferCreateInfo, indexBuffer);
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
			if (!m_ObjectMesh->CreateProcedural(256, vertexAttributes, m_MaterialID, TopologyMode::LINE_LIST, &createInfo))
			{
				PrintWarn("Vulkan physics debug renderer failed to initialize vertex buffer");
			}
			g_SceneManager->CurrentScene()->AddRootObject(m_Object);
		}
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN