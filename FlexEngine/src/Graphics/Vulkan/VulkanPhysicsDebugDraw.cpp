#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanPhysicsDebugDraw.hpp"

#include "Cameras/BaseCamera.hpp"
#include "Graphics/Renderer.hpp"
#include "Graphics/VertexAttribute.hpp"
#include "Graphics/Vulkan/VulkanHelpers.hpp"
#include "Graphics/Vulkan/VulkanRenderer.hpp"
#include "Profiler.hpp"
#include "Scene/GameObject.hpp"

namespace flex
{
	namespace vk
	{
		VulkanPhysicsDebugDraw::VulkanPhysicsDebugDraw()
		{
		}

		VulkanPhysicsDebugDraw::~VulkanPhysicsDebugDraw()
		{
			m_VertexBufferData.Destroy();
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
				debugMatCreateInfo.engineMaterial = true;
				m_MaterialID = g_Renderer->InitializeMaterial(&debugMatCreateInfo);
			}

			m_VertexBufferData = {};
			m_VertexBufferCreateInfo = {};
			m_VertexBufferCreateInfo.attributes = (u32)VertexAttribute::POSITION | (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;
		}

		void VulkanPhysicsDebugDraw::UpdateDebugMode()
		{
			const PhysicsDebuggingSettings& settings = g_Renderer->GetPhysicsDebuggingSettings();

			m_DebugMode =
				(settings.DisableAll ? DBG_NoDebug : 0) |
				(settings.DrawWireframe ? DBG_DrawWireframe : 0) |
				(settings.DrawAabb ? DBG_DrawAabb : 0) |
				(settings.DrawFeaturesText ? DBG_DrawFeaturesText : 0) |
				(settings.DrawContactPoints ? DBG_DrawContactPoints : 0) |
				(settings.NoDeactivation ? DBG_NoDeactivation : 0) |
				(settings.NoHelpText ? DBG_NoHelpText : 0) |
				(settings.DrawText ? DBG_DrawText : 0) |
				(settings.ProfileTimings ? DBG_ProfileTimings : 0) |
				(settings.EnableSatComparison ? DBG_EnableSatComparison : 0) |
				(settings.DisableBulletLCP ? DBG_DisableBulletLCP : 0) |
				(settings.EnableCCD ? DBG_EnableCCD : 0) |
				(settings.DrawConstraints ? DBG_DrawConstraints : 0) |
				(settings.DrawConstraintLimits ? DBG_DrawConstraintLimits : 0) |
				(settings.FastWireframe ? DBG_FastWireframe : 0) |
				(settings.DrawNormals ? DBG_DrawNormals : 0) |
				(settings.DrawFrames ? DBG_DrawFrames : 0);
		}

		void VulkanPhysicsDebugDraw::reportErrorWarning(const char* warningString)
		{
			PrintError("VulkanPhysicsDebugDraw: %s\n", warningString);
		}

		void VulkanPhysicsDebugDraw::draw3dText(const btVector3& location, const char* textString)
		{
			UNREFERENCED_PARAMETER(location);
			UNREFERENCED_PARAMETER(textString);
			// TODO: FIXME: UNIMPLEMENTED: Implement me (or don't)
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
			UNREFERENCED_PARAMETER(from);
			UNREFERENCED_PARAMETER(to);
			UNREFERENCED_PARAMETER(color);
		}

		void VulkanPhysicsDebugDraw::flushLines()
		{
			Draw();
		}

		void VulkanPhysicsDebugDraw::ClearLines()
		{
			m_LineSegments.clear();
		}

		void VulkanPhysicsDebugDraw::Draw()
		{
			if (m_LineSegments.empty())
			{
				return;
			}

			{
				PROFILE_AUTO("PhysicsDebugRender > Destroy previous vertex buffer");
				m_VertexBufferData.Destroy();
			}

			{
				PROFILE_AUTO("PhysicsDebugRender > Initialze vertex buffer");

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

				m_VertexBufferData.Initialize(&m_VertexBufferCreateInfo);
			}

			// TODO: Draw
			//{
			//	PROFILE_AUTO("PhysicsDebugRender > render");

			//	glUseProgram(glShader->program);

			//	glBindVertexArray(m_VAO);

			//	glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
			//	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)m_VertexBufferData.VertexBufferSize, m_VertexBufferData.vertexData, GL_STREAM_DRAW);

			//	// Describe shader variables (TODO: Why can't this be done just once in initialize? glBufferData)
			//	{
			//		real* currentLocation = (real*)0;

			//		glVertexAttribPointer((GLuint)m_VertAttribPosLoc, 3, GL_FLOAT, GL_FALSE, m_VertexBufferData.VertexStride, currentLocation);
			//		currentLocation += 3;

			//		glVertexAttribPointer((GLuint)m_VertAttribColLoc, 4, GL_FLOAT, GL_FALSE, m_VertexBufferData.VertexStride, currentLocation);
			//		currentLocation += 4;
			//	}

			//	glm::mat4 model = MAT4_IDENTITY;
			//	glm::mat4 proj = g_CameraManager->CurrentCamera()->GetProjection();
			//	glm::mat4 view = g_CameraManager->CurrentCamera()->GetView();
			//	glm::vec4 colorMultiplier = glMat->material.colorMultiplier;

			//	glUniformMatrix4fv(glMat->uniformIDs.model, 1, false, &model[0][0]);
			//	glUniformMatrix4fv(glMat->uniformIDs.view, 1, false, &view[0][0]);
			//	glUniformMatrix4fv(glMat->uniformIDs.projection, 1, false, &proj[0][0]);
			//	glUniform4fv(glMat->uniformIDs.colorMultiplier, 1, &colorMultiplier[0]);

			//	glDepthMask(GL_FALSE);

			//	glEnable(GL_BLEND);
			//	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			//	glDrawArrays(GL_LINES, 0, (GLsizei)m_VertexBufferData.VertexCount);
			//}
		}
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN