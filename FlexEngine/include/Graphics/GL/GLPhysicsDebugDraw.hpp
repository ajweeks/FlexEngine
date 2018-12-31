#pragma once
#if COMPILE_OPEN_GL

#pragma warning(push, 0)
#include <glad/glad.h>

#include "LinearMath/btIDebugDraw.h"
#pragma warning(pop)

#include "Graphics/VertexBufferData.hpp"

namespace flex
{
	namespace gl
	{
		class GLRenderer;

		class GLPhysicsDebugDraw : public btIDebugDraw
		{
		public:
			GLPhysicsDebugDraw();
			virtual ~GLPhysicsDebugDraw();

			void Initialize();
			void Destroy();

			void UpdateDebugMode();

			virtual void reportErrorWarning(const char* warningString)  override;
			virtual void draw3dText(const btVector3& location, const char* textString)  override;
			virtual void setDebugMode(int debugMode)  override;
			virtual int	getDebugMode() const override;

			virtual void drawLine(const btVector3& from, const btVector3& to, const btVector3& color) override;
			virtual void drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& color) override;

			virtual void drawSphere(btScalar radius, const btTransform& transform, const btVector3& color) override;
			virtual void drawSphere(const btVector3& p, btScalar radius, const btVector3& color) override;

			void DrawLineWithAlpha(const btVector3& from, const btVector3& to, const btVector4& color);
			void DrawContactPointWithAlpha(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector4& color);

			virtual void flushLines() override;
			void ClearLines();

		private:
			void Draw();

			struct LineSegment
			{
				btVector3 start;
				btVector3 end;
				btVector4 color;
			};

			// Gets filled each frame by calls to drawLine, then emptied after debugDrawWorld()
			std::vector<LineSegment> m_LineSegments;
			std::vector<LineSegment> m_pLineSegments;

			int m_DebugMode = 0;

			GLRenderer* m_Renderer = nullptr;

			MaterialID m_MaterialID = InvalidMaterialID;

			GLuint m_VAO = 0;
			GLuint m_VBO = 0;

			i32 m_VertAttribPosLoc = -1;
			i32 m_VertAttribColLoc = -1;

			// Per-frame data
			VertexBufferData m_VertexBufferData;
			VertexBufferData::CreateInfo m_VertexBufferCreateInfo;
		};
	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL