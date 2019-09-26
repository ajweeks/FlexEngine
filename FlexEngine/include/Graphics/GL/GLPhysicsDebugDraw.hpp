#pragma once
#if COMPILE_OPEN_GL

IGNORE_WARNINGS_PUSH
#include <glad/glad.h>
IGNORE_WARNINGS_POP

#include "Graphics/Renderer.hpp"
#include "Graphics/VertexBufferData.hpp"

namespace flex
{
	namespace gl
	{
		class GLRenderer;

		class GLPhysicsDebugDraw : public PhysicsDebugDrawBase
		{
		public:
			GLPhysicsDebugDraw();
			virtual ~GLPhysicsDebugDraw();

			virtual void Initialize() override;
			virtual void Destroy() override;

			virtual void reportErrorWarning(const char* warningString)  override;
			virtual void draw3dText(const btVector3& location, const char* textString)  override;
			virtual void setDebugMode(int debugMode)  override;
			virtual int	getDebugMode() const override;

			virtual void drawLine(const btVector3& from, const btVector3& to, const btVector3& color) override;
			virtual void drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& color) override;

			virtual void drawSphere(btScalar radius, const btTransform& transform, const btVector3& color) override;
			virtual void drawSphere(const btVector3& p, btScalar radius, const btVector3& color) override;

			virtual void DrawLineWithAlpha(const btVector3& from, const btVector3& to, const btVector4& color) override;

			void DrawContactPointWithAlpha(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector4& color);

		private:
			virtual void Draw() override;

			GLRenderer* m_Renderer = nullptr;

			MaterialID m_MaterialID = InvalidMaterialID;

			GLuint m_VAO = 0;
			GLuint m_VBO = 0;

			i32 m_VertAttribPosLoc = -1;
			i32 m_VertAttribColLoc = -1;

			// Per-frame data
			VertexBufferData m_VertexBufferData;
			VertexBufferDataCreateInfo m_VertexBufferCreateInfo;
		};
	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL