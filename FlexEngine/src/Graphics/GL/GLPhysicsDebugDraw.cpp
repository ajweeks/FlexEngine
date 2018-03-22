#include "stdafx.hpp"

#include "Graphics\GL\GLPhysicsDebugDraw.hpp"
#include "Graphics\GL\GLHelpers.hpp"
#include "Graphics\GL\GLRenderer.hpp"
#include "VertexAttribute.hpp"

#include "Logger.hpp"

namespace flex
{
	namespace gl
	{
		GLPhysicsDebugDraw::GLPhysicsDebugDraw(GLRenderer* renderer) :
			m_Renderer(renderer)
		{
			if (!renderer->GetMaterialID("Color", m_MaterialID))
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

		void GLPhysicsDebugDraw::flushLines()
		{
			Draw();

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


			m_VertexBufferData.Destroy(); // Destory previous frame's buffer since it's already been drawn

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

			glDepthFunc(GL_LEQUAL);
			CheckGLErrorMessages();

			glDepthMask(GL_TRUE);
			CheckGLErrorMessages();

			if (shader->translucent)
			{
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			}
			else
			{
				glDisable(GL_BLEND);
			}

			glDrawArrays(GL_LINES, 0, (GLsizei)m_VertexBufferData.VertexCount);
			CheckGLErrorMessages();
		}
	} // namespace gl
} // namespace flex
