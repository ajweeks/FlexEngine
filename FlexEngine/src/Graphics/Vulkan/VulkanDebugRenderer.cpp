#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanDebugRenderer.hpp"

#include "Cameras/BaseCamera.hpp"
#include "Graphics/Renderer.hpp"
#include "Graphics/VertexAttribute.hpp"
#include "Graphics/Vulkan/VulkanHelpers.hpp"
#include "Graphics/Vulkan/VulkanRenderer.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	namespace vk
	{
		VulkanDebugRenderer::VulkanDebugRenderer() :
			m_VertexBufferCreateInfo({})
		{
		}

		VulkanDebugRenderer::~VulkanDebugRenderer()
		{
		}

		void VulkanDebugRenderer::Initialize()
		{
			PROFILE_AUTO("VulkanDebugRenderer Initialize");

			const std::string debugMatName = "Debug";
			if (!g_Renderer->FindOrCreateMaterialByName(debugMatName, m_MaterialID))
			{
				MaterialCreateInfo debugMatCreateInfo = {};
				debugMatCreateInfo.shaderName = "colour";
				debugMatCreateInfo.name = debugMatName;
				debugMatCreateInfo.persistent = true;
				debugMatCreateInfo.bEditorMaterial = true;
				debugMatCreateInfo.bDynamic = true;
				debugMatCreateInfo.bSerializable = false;
				m_MaterialID = g_Renderer->InitializeMaterial(&debugMatCreateInfo);
			}
		}

		void VulkanDebugRenderer::PostInitialize()
		{
			PROFILE_AUTO("VulkanDebugRenderer PostInitialize");

			CreateDebugObject();
		}

		void VulkanDebugRenderer::Destroy()
		{
			m_Object = nullptr;
		}

		void VulkanDebugRenderer::OnPreSceneChange()
		{
			m_Object = nullptr;
		}

		void VulkanDebugRenderer::OnPostSceneChange()
		{
			Clear();
			CreateDebugObject();
		}

		void VulkanDebugRenderer::reportErrorWarning(const char* warningString)
		{
			PrintError("VulkanDebugRenderer: %s\n", warningString);
		}

		void VulkanDebugRenderer::draw3dText(const btVector3& location, const char* textString)
		{
			FLEX_UNUSED(location);
			FLEX_UNUSED(textString);
		}

		void VulkanDebugRenderer::setDebugMode(int debugMode)
		{
			FLEX_UNUSED(debugMode);
			// NOTE: Call UpdateDebugMode instead of this
		}

		int VulkanDebugRenderer::getDebugMode() const
		{
			return m_DebugMode;
		}

		void VulkanDebugRenderer::drawLine(const btVector3& from, const btVector3& to, const btVector3& colour)
		{
			drawLine(from, to, colour, colour);
		}

		void VulkanDebugRenderer::drawLine(const btVector3& from, const btVector3& to, const btVector3& colourFrom, const btVector3& colourTo)
		{
			if (m_LineSegmentIndex < MAX_NUM_LINE_SEGMENTS)
			{
				m_LineSegments[m_LineSegmentIndex++] = { from, to, colourFrom, colourTo };
			}
			else
			{
				PrintWarn("Max number of debug draw lines reached (%d)\n", MAX_NUM_LINE_SEGMENTS);
			}
		}

		void VulkanDebugRenderer::drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& colour)
		{
			FLEX_UNUSED(PointOnB);
			FLEX_UNUSED(normalOnB);
			FLEX_UNUSED(distance);
			FLEX_UNUSED(lifeTime);
			FLEX_UNUSED(colour);
		}

		void VulkanDebugRenderer::DrawLineWithAlpha(const btVector3& from, const btVector3& to, const btVector4& colour)
		{
			DrawLineWithAlpha(from, to, colour, colour);
		}

		void VulkanDebugRenderer::DrawLineWithAlpha(const btVector3& from, const btVector3& to, const btVector4& colourFrom, const btVector4& colourTo)
		{
			if (m_LineSegmentIndex < MAX_NUM_LINE_SEGMENTS)
			{
				m_LineSegments[m_LineSegmentIndex++] = { from, to, colourFrom, colourTo };
			}
			else
			{
				PrintWarn("Max number of debug draw lines reached (%d)\n", MAX_NUM_LINE_SEGMENTS);
			}
		}

		void VulkanDebugRenderer::DrawBasis(const btVector3& origin, const btVector3& right, const btVector3& up, const btVector3& forward)
		{
			drawLine(origin, origin + right, btVector3(1.0f, 0.0f, 0.0f));
			drawLine(origin, origin + up, btVector3(0.0f, 1.0f, 0.0f));
			drawLine(origin, origin + forward, btVector3(0.0f, 0.0f, 1.0f));
		}

		void VulkanDebugRenderer::Draw()
		{
			const u32 lineCount = m_LineSegmentIndex;

			{
				PROFILE_AUTO("PhysicsDebugRender > Update vertex buffer");

				PROFILE_BEGIN("Hash vertex buffer");
				u32 oldHash = HashVertexBufferDataCreateInfo(m_VertexBufferCreateInfo);
				PROFILE_END("Hash vertex buffer");

				m_VertexBufferCreateInfo.positions_3D.clear();
				m_VertexBufferCreateInfo.colours_R32G32B32A32.clear();
				indexBuffer.clear();

				if (lineCount > 0)
				{
					u32 numVerts = lineCount * 2;

					if (m_VertexBufferCreateInfo.positions_3D.capacity() < numVerts)
					{
						if (!m_VertexBufferCreateInfo.positions_3D.empty())
						{
							// Double in size when growing
							numVerts = numVerts * 2;
						}
					}

					m_VertexBufferCreateInfo.positions_3D.resize(numVerts);
					m_VertexBufferCreateInfo.colours_R32G32B32A32.resize(numVerts);
					indexBuffer.resize(numVerts);

					i32 i = 0;
					glm::vec3* posData = m_VertexBufferCreateInfo.positions_3D.data();
					glm::vec4* colData = m_VertexBufferCreateInfo.colours_R32G32B32A32.data();
					u32* idxData = indexBuffer.data();
					for (u32 li = 0; li < lineCount; ++li)
					{
						memcpy(posData + i, m_LineSegments[li].start, sizeof(real) * 3);
						memcpy(posData + i + 1, m_LineSegments[li].end, sizeof(real) * 3);

						memcpy(colData + i, m_LineSegments[li].colourFrom, sizeof(real) * 4);
						memcpy(colData + i + 1, m_LineSegments[li].colourTo, sizeof(real) * 4);

						u32 idx0 = li * 2;
						u32 idx1 = li * 2 + 1;
						memcpy(idxData + i, &idx0, sizeof(u32));
						memcpy(idxData + i + 1, &idx1, sizeof(u32));

						i += 2;
					}
				}

				PROFILE_BEGIN("Hash vertex buffer");
				u32 newHash = HashVertexBufferDataCreateInfo(m_VertexBufferCreateInfo);
				PROFILE_END("Hash vertex buffer");

				if (newHash != oldHash && !m_VertexBufferCreateInfo.positions_3D.empty())
				{
					m_Object->GetMesh()->GetSubMesh(0)->UpdateDynamicVertexData(m_VertexBufferCreateInfo, indexBuffer);
				}
			}
		}

		void VulkanDebugRenderer::CreateDebugObject()
		{
			PROFILE_AUTO("CreateDebugObject");

			CHECK_EQ(m_Object, nullptr);

			RenderObjectCreateInfo createInfo = {};
			createInfo.materialID = m_MaterialID;
			createInfo.bEditorObject = true;
			createInfo.bIndexed = true;
			createInfo.indices = &indexBuffer;
			m_Object = new GameObject("Vk Physics Debug Draw", BaseObjectSID);
			m_Object->SetSerializable(false);
			m_Object->SetVisibleInSceneExplorer(false);
			m_Object->SetCastsShadow(false);
			Mesh* mesh = m_Object->SetMesh(new Mesh(m_Object));
			Material* mat = g_Renderer->GetMaterial(m_MaterialID);
			const VertexAttributes vertexAttributes = g_Renderer->GetShader(mat->shaderID)->vertexAttributes;
			if (!mesh->CreateProcedural(8912, vertexAttributes, m_MaterialID, TopologyMode::LINE_LIST, &createInfo))
			{
				PrintWarn("Vulkan physics debug renderer failed to initialize vertex buffer\n");
			}
			g_SceneManager->CurrentScene()->AddRootObject(m_Object);
		}

		void VulkanDebugRenderer::Clear()
		{
			m_VertexBufferCreateInfo.positions_3D.clear();
			m_VertexBufferCreateInfo.colours_R32G32B32A32.clear();
			indexBuffer.clear();
		}
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN