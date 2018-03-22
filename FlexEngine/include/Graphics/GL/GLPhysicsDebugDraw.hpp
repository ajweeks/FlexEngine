#pragma once

#include "LinearMath\btIDebugDraw.h"

#include "Types.hpp"

#include <glad\glad.h>

#include <vector>

namespace flex
{
	namespace gl
	{
		class GLRenderer;

		class GLPhysicsDebugDraw : public btIDebugDraw
		{
		public:
			GLPhysicsDebugDraw(GLRenderer* renderer);
			virtual ~GLPhysicsDebugDraw();

			virtual void reportErrorWarning(const char* warningString)  override;
			virtual void draw3dText(const btVector3& location, const char* textString)  override;
			virtual void setDebugMode(int debugMode)  override;
			virtual int	getDebugMode() const override;

			virtual void drawLine(const btVector3& from, const btVector3& to, const btVector3& color) override;
			virtual void drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& color) override;
			virtual void flushLines() override;
			
			
			void Draw();

		private:
			struct LineSegment
			{
				btVector3 start;
				btVector3 end;
				btVector3 color;
			};

			// Gets filled each frame by calls to drawLine, then emptied after debugDrawWorld()
			std::vector<LineSegment> m_LineSegments;

			int m_DebugMode = DBG_DrawWireframe | DBG_DrawAabb;

			GLRenderer* m_Renderer = nullptr;

			MaterialID m_MaterialID = InvalidMaterialID;

			GLuint m_VAO = 0;
			GLuint m_VBO = 0;

			// Per-frame data
			VertexBufferData m_VertexBufferData;
		};
	} // namespace gl
} // namespace flex
