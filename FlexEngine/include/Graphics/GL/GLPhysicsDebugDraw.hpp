#pragma once
#if COMPILE_OPEN_GL

#include <vector>

#pragma warning(push, 0)
#include <glad/glad.h>

#include "LinearMath/btIDebugDraw.h"
#pragma warning(pop)

#include "Types.hpp"
#include "VertexBufferData.hpp"

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

			virtual void flushLines() override;
			void ClearLines();

		private:
			void Draw();

			struct LineSegment
			{
				btVector3 start;
				btVector3 end;
				btVector3 color;
			};

			// Gets filled each frame by calls to drawLine, then emptied after debugDrawWorld()
			std::vector<LineSegment> m_LineSegments;

			int m_DebugMode = 0;

			GLRenderer* m_Renderer = nullptr;

			MaterialID m_MaterialID = InvalidMaterialID;

			GLuint m_VAO = 0;
			GLuint m_VBO = 0;

			// Per-frame data
			VertexBufferData m_VertexBufferData;
		};
	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL