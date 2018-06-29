#include "stdafx.hpp"
#if COMPILE_OPEN_GL

#include "Graphics/GL/GLPhysicsDebugDraw.hpp"

#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "GameContext.hpp"
#include "Graphics/GL/GLHelpers.hpp"
#include "Graphics/GL/GLRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "Logger.hpp"
#include "VertexAttribute.hpp"

namespace flex
{
	namespace gl
	{
		GLPhysicsDebugDraw::GLPhysicsDebugDraw(const GameContext& gameContext) :
			m_GameContext(gameContext)
		{
		}

		GLPhysicsDebugDraw::~GLPhysicsDebugDraw()
		{
		}

		void GLPhysicsDebugDraw::Initialize()
		{
			m_Renderer = (GLRenderer*)(m_GameContext.renderer);
			const std::string debugMatName = "Debug";
			if (!m_Renderer->GetMaterialID(debugMatName, m_MaterialID))
			{
				MaterialCreateInfo debugMatCreateInfo = {};
				debugMatCreateInfo.shaderName = "color";
				debugMatCreateInfo.name = debugMatName;
				debugMatCreateInfo.engineMaterial = true;
				m_MaterialID = m_GameContext.renderer->InitializeMaterial(&debugMatCreateInfo);
			}

			m_VertexBufferData = {};
		}
		
		void GLPhysicsDebugDraw::Destroy()
		{
			m_VertexBufferData.Destroy();
		}

		void GLPhysicsDebugDraw::UpdateDebugMode()
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

		void GLPhysicsDebugDraw::reportErrorWarning(const char* warningString)
		{
			Logger::LogError("GLPhysicsDebugDraw > " + std::string(warningString));
		}

		void GLPhysicsDebugDraw::draw3dText(const btVector3& location, const char* textString)
		{
			// TODO: FIXME: UNIMPLEMENTED: Implement me (or don't)
			UNREFERENCED_PARAMETER(location);
			UNREFERENCED_PARAMETER(textString);
		}

		void GLPhysicsDebugDraw::setDebugMode(int debugMode)
		{
			// NOTE: Call UpdateDebugMode instead of this
			// This stub needs to exist to override pure virtual function
			UNREFERENCED_PARAMETER(debugMode);
		}

		int GLPhysicsDebugDraw::getDebugMode() const
		{
			return m_DebugMode;
		}

		void GLPhysicsDebugDraw::drawLine(const btVector3& from, const btVector3& to, const btVector3& color)
		{
			m_LineSegments.push_back(LineSegment{ from, to, color });
		}

		void GLPhysicsDebugDraw::drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& color)
		{
			// TODO: FIXME: UNIMPLEMENTED: Implement me (or don't)
			UNREFERENCED_PARAMETER(PointOnB);
			UNREFERENCED_PARAMETER(normalOnB);
			UNREFERENCED_PARAMETER(distance);
			UNREFERENCED_PARAMETER(lifeTime);
			UNREFERENCED_PARAMETER(color);
		}
		
		void GLPhysicsDebugDraw::flushLines()
		{
			Draw();
		}

		void GLPhysicsDebugDraw::ClearLines()
		{
			m_LineSegments.clear();
		}

		void GLPhysicsDebugDraw::Draw()
		{
			if (m_LineSegments.empty())
			{
				return;
			}

			if (m_MaterialID == InvalidMaterialID)
			{
				Logger::LogError("Attempted to draw GLPhysicsDebug objects before material has been set");
				return;
			}

			GLMaterial* glMat = &m_Renderer->m_Materials[m_MaterialID];
			GLShader* glShader = &m_Renderer->m_Shaders[glMat->material.shaderID];

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

			glUseProgram(glShader->program);
			CheckGLErrorMessages();

			glGenVertexArrays(1, &m_VAO);
			glBindVertexArray(m_VAO);
			CheckGLErrorMessages();

			glGenBuffers(1, &m_VBO);
			glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
			glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)m_VertexBufferData.BufferSize, m_VertexBufferData.pDataStart, GL_STATIC_DRAW);
			CheckGLErrorMessages();


			// Describe shader variables
			{
				real* currentLocation = (real*)0;

				GLint location = glGetAttribLocation(glShader->program, "in_Position");
				if (location != -1)
				{
					glEnableVertexAttribArray((GLuint)location);


					glVertexAttribPointer((GLuint)location, 3, GL_FLOAT, GL_FALSE, m_VertexBufferData.VertexStride, currentLocation);
					CheckGLErrorMessages();

					currentLocation += 3;
				}

				location = glGetAttribLocation(glShader->program, "in_Color");
				if (location != -1)
				{
					glEnableVertexAttribArray((GLuint)location);

					glVertexAttribPointer((GLuint)location, 4, GL_FLOAT, GL_FALSE, m_VertexBufferData.VertexStride, currentLocation);
					CheckGLErrorMessages();

					currentLocation += 4;
				}
			}


			glm::mat4 model = glm::mat4(1.0f);
			glm::mat4 proj = m_GameContext.cameraManager->CurrentCamera()->GetProjection();
			glm::mat4 view = m_GameContext.cameraManager->CurrentCamera()->GetView();
			glm::mat4 MVP = proj * view * model;
			glm::vec4 colorMultiplier = glMat->material.colorMultiplier;

			glUniformMatrix4fv(glMat->uniformIDs.model, 1, false, &model[0][0]);
			CheckGLErrorMessages();

			glUniformMatrix4fv(glMat->uniformIDs.view, 1, false, &view[0][0]);
			CheckGLErrorMessages();

			glUniformMatrix4fv(glMat->uniformIDs.projection, 1, false, &proj[0][0]);
			CheckGLErrorMessages();

			glUniform4fv(glMat->uniformIDs.colorMultiplier, 1, &colorMultiplier[0]);
			CheckGLErrorMessages();


			glDepthMask(GL_FALSE);
			CheckGLErrorMessages();

			glDisable(GL_BLEND);

			glDrawArrays(GL_LINES, 0, (GLsizei)m_VertexBufferData.VertexCount);
			CheckGLErrorMessages();
		}
	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL