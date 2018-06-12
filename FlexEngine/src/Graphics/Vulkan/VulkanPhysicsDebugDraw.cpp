#include "stdafx.hpp"
#if COMPILE_VULKAN

#include "Graphics/Vulkan/VulkanPhysicsDebugDraw.hpp"

#include "Cameras/BaseCamera.hpp"
#include "GameContext.hpp"
#include "Graphics/Renderer.hpp"
#include "Graphics/Vulkan/VulkanHelpers.hpp"
#include "Graphics/Vulkan/VulkanRenderer.hpp"
#include "Logger.hpp"
#include "Scene/GameObject.hpp"
#include "VertexAttribute.hpp"

namespace flex
{
	namespace vk
	{
		VulkanPhysicsDebugDraw::VulkanPhysicsDebugDraw(const GameContext& gameContext) :
			m_GameContext(gameContext)
		{
		}

		VulkanPhysicsDebugDraw::~VulkanPhysicsDebugDraw()
		{
			m_VertexBufferData.Destroy();

			if (m_GameObject)
			{
				m_GameObject->Destroy(m_GameContext);
				SafeDelete(m_GameObject);
			}
		}

		void VulkanPhysicsDebugDraw::Initialize()
		{
			m_Renderer = (VulkanRenderer*)(m_GameContext.renderer);
			if (!m_Renderer->GetMaterialID("Color", m_MaterialID))
			{
				Logger::LogError("Failed to retrieve shader for Vulkan physics debug draw!");
			}

			m_VertexBufferData = {};
			VertexBufferData::CreateInfo createInfo = {};
			createInfo.attributes = (u32)VertexAttribute::POSITION | (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;
			createInfo.positions_3D = {};
			createInfo.colors_R32G32B32A32 = {};
			m_VertexBufferData.Initialize(&createInfo);

			m_GameObject = new GameObject("Physics Debug Draw Object", SerializableType::NONE);

			RenderObjectCreateInfo renderObjectCreateInfo = {};
			renderObjectCreateInfo.vertexBufferData = &m_VertexBufferData;
			renderObjectCreateInfo.gameObject = m_GameObject;
			renderObjectCreateInfo.materialID = m_MaterialID;
			renderObjectCreateInfo.visibleInSceneExplorer = false;

			m_GameObject->SetRenderID(m_GameContext.renderer->InitializeRenderObject(m_GameContext, &renderObjectCreateInfo));
		}

		void VulkanPhysicsDebugDraw::UpdateDebugMode()
		{
			const PhysicsDebuggingSettings& settings = m_GameContext.renderer->GetPhysicsDebuggingSettings();

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
			Logger::LogError("VulkanPhysicsDebugDraw > " + std::string(warningString));
		}

		void VulkanPhysicsDebugDraw::draw3dText(const btVector3& location, const char* textString)
		{
			// TODO: FIXME: UNIMPLEMENTED: Implement me (or don't)
		}

		void VulkanPhysicsDebugDraw::setDebugMode(int debugMode)
		{
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
			// TODO: FIXME: UNIMPLEMENTED: Implement me (or don't)
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
				m_GameObject->SetVisible(false);
				return;
			}
			m_GameObject->SetVisible(true);

			VulkanMaterial* vkMat = &m_Renderer->m_Materials[m_MaterialID];
			VulkanShader* vkShader = &m_Renderer->m_Shaders[vkMat->material.shaderID];
			Shader* shader = &vkShader->shader;

			VertexBufferData::CreateInfo createInfo = {};
			createInfo.attributes = ((u32)VertexAttribute::POSITION | (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT);

			for (LineSegment& line : m_LineSegments)
			{
				createInfo.positions_3D.push_back(BtVec3ToVec3(line.start));
				createInfo.positions_3D.push_back(BtVec3ToVec3(line.end));

				glm::vec4 color = glm::vec4(BtVec3ToVec3(line.color), 1.0f);
				createInfo.colors_R32G32B32A32.push_back(color);
				createInfo.colors_R32G32B32A32.push_back(color);
			}


			m_VertexBufferData.Destroy(); // Destroy previous frame's buffer since it's already been drawn

			m_VertexBufferData.Initialize(&createInfo);

			m_GameContext.renderer->UpdateRenderObjectVertexData(m_GameObject->GetRenderID());

			//glUseProgram(glShader->program);
			//CheckGLErrorMessages();

			//glGenVertexArrays(1, &m_VAO);
			//glBindVertexArray(m_VAO);
			//CheckGLErrorMessages();

			//glGenBuffers(1, &m_VBO);
			//glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
			//glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)m_VertexBufferData.BufferSize, m_VertexBufferData.pDataStart, GL_STATIC_DRAW);
			//CheckGLErrorMessages();


			// Describe shader variables
			//{
			//	real* currentLocation = (real*)0;

			//	GLint location = glGetAttribLocation(glShader->program, "in_Position");
			//	if (location != -1)
			//	{
			//		glEnableVertexAttribArray((GLuint)location);


			//		glVertexAttribPointer((GLuint)location, 3, GL_FLOAT, GL_FALSE, m_VertexBufferData.VertexStride, currentLocation);
			//		CheckGLErrorMessages();

			//		currentLocation += 3;
			//	}

			//	location = glGetAttribLocation(glShader->program, "in_Color");
			//	if (location != -1)
			//	{
			//		glEnableVertexAttribArray((GLuint)location);

			//		glVertexAttribPointer((GLuint)location, 4, GL_FLOAT, GL_FALSE, m_VertexBufferData.VertexStride, currentLocation);
			//		CheckGLErrorMessages();

			//		currentLocation += 4;
			//	}
			//}


			//glm::mat4 model = glm::mat4(1.0f);
			//glm::mat4 proj = m_GameContext.camera->GetProjection();
			//glm::mat4 view = m_GameContext.camera->GetView();
			//glm::mat4 MVP = proj * view * model;
			//glm::vec4 colorMultiplier = glMat->material.colorMultiplier;

			//glUniformMatrix4fv(glMat->uniformIDs.model, 1, false, &model[0][0]);
			//CheckGLErrorMessages();

			//glUniformMatrix4fv(glMat->uniformIDs.view, 1, false, &view[0][0]);
			//CheckGLErrorMessages();

			//glUniformMatrix4fv(glMat->uniformIDs.projection, 1, false, &proj[0][0]);
			//CheckGLErrorMessages();

			//glUniform4fv(glMat->uniformIDs.colorMultiplier, 1, &colorMultiplier[0]);
			//CheckGLErrorMessages();


			//glDepthMask(GL_FALSE);
			//CheckGLErrorMessages();

			//glDisable(GL_BLEND);

			//glDrawArrays(GL_LINES, 0, (GLsizei)m_VertexBufferData.VertexCount);
			//CheckGLErrorMessages();
		}
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN