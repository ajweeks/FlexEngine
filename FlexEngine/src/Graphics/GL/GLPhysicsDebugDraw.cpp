#include "stdafx.hpp"

#include "Graphics\GL\GLPhysicsDebugDraw.hpp"
#include "Graphics\GL\GLHelpers.hpp"
#include "Graphics\GL\GLRenderer.hpp"
#include "VertexAttribute.hpp"

#include "Logger.hpp"
#include "Graphics/Renderer.hpp"
#include "GameContext.hpp"
#include "FreeCamera.hpp"

namespace flex
{
	namespace gl
	{
		GLPhysicsDebugDraw::GLPhysicsDebugDraw(const GameContext& gameContext) :
			m_GameContext(gameContext)
		{
			m_Renderer = (GLRenderer*)(m_GameContext.renderer);
			if (!m_Renderer->GetMaterialID("Color", m_MaterialID))
			{
				Logger::LogError("Failed to retrieve shader for GL physics debug draw!");
			}

			m_VertexBufferData = {};
		}

		GLPhysicsDebugDraw::~GLPhysicsDebugDraw()
		{
			m_VertexBufferData.Destroy();
		}

		void GLPhysicsDebugDraw::reportErrorWarning(const char* warningString)
		{
			Logger::LogError("GLPhysicsDebugDraw > " + std::string(warningString));
		}

		void GLPhysicsDebugDraw::draw3dText(const btVector3& location, const char* textString)
		{
			// TODO: FIXME: UNIMPLEMENTED: Implement me (or don't)
		}

		void GLPhysicsDebugDraw::setDebugMode(int debugMode)
		{
			m_DebugMode = debugMode;
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
		}

		btIDebugDraw::DefaultColors GLPhysicsDebugDraw::getDefaultColors() const
		{
			DefaultColors colors = {};

			colors.m_activeObject.setW(1.0f);
			colors.m_deactivatedObject.setW(1.0f);
			colors.m_wantsDeactivationObject.setW(1.0f);
			colors.m_disabledDeactivationObject.setW(1.0f);
			colors.m_disabledSimulationObject.setW(1.0f);
			colors.m_aabb.setW(1.0f);
			colors.m_contactPoint.setW(1.0f);

			return colors;
		}

		void GLPhysicsDebugDraw::flushLines()
		{
			Draw();
		}

		void GLPhysicsDebugDraw::clearLines()
		{
			m_LineSegments.clear();
		}

		void GLPhysicsDebugDraw::Draw()
		{
			GLMaterial* glMat = &m_Renderer->m_Materials[m_MaterialID];
			GLShader* glShader = &m_Renderer->m_Shaders[glMat->material.shaderID];
			Renderer::Shader* shader = &glShader->shader;

			VertexBufferData::CreateInfo createInfo = {};
			createInfo.attributes = ((u32)VertexAttribute::POSITION | (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT);

			for (LineSegment& line : m_LineSegments)
			{
				createInfo.positions_3D.push_back(FromBtVec3(line.start));
				createInfo.positions_3D.push_back(FromBtVec3(line.end));

				glm::vec4 color = glm::vec4(FromBtVec3(line.color), 1.0f);
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
			glm::mat4 proj = m_GameContext.camera->GetProjection();
			glm::mat4 view = m_GameContext.camera->GetView();
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

			//glDepthFunc(GL_LEQUAL);
			//CheckGLErrorMessages();

			glDepthMask(GL_FALSE);
			CheckGLErrorMessages();

			//if (shader->translucent)
			//{
			//	glEnable(GL_BLEND);
			//	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			//}
			//else
			//{
				glDisable(GL_BLEND);
			//}

			glDrawArrays(GL_LINES, 0, (GLsizei)m_VertexBufferData.VertexCount);
			CheckGLErrorMessages();
		}
	} // namespace gl
} // namespace flex
