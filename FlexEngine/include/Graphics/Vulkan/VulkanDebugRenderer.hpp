#pragma once
#if COMPILE_VULKAN

#include <vector>

#include "Graphics/DebugRenderer.hpp"

#include "Graphics/VertexBufferData.hpp"
#include "Types.hpp"

namespace flex
{
	class Mesh;

	namespace vk
	{
		class VulkanDebugRenderer final : public DebugRenderer
		{
		public:
			VulkanDebugRenderer();
			virtual ~VulkanDebugRenderer();

			virtual void Initialize() override;
			virtual void PostInitialize() override;
			virtual void Destroy() override;

			virtual void OnPreSceneChange() override;
			virtual void OnPostSceneChange() override;

			virtual void reportErrorWarning(const char* warningString)  override;
			virtual void draw3dText(const btVector3& location, const char* textString)  override;
			virtual void setDebugMode(int debugMode)  override;
			virtual int	getDebugMode() const override;

			virtual void drawLine(const btVector3& from, const btVector3& to, const btVector3& colour) override;
			virtual void drawLine(const btVector3& from, const btVector3& to, const btVector3& colourFrom, const btVector3& colourTo) override;
			virtual void drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& colour) override;

			virtual void DrawLineWithAlpha(const btVector3& from, const btVector3& to, const btVector4& colour) override;
			virtual void DrawLineWithAlpha(const btVector3& from, const btVector3& to, const btVector4& colourFrom, const btVector4& colourTo) override;

		private:
			virtual void Draw() override;

			void CreateDebugObject();

			void Clear();

			MaterialID m_MaterialID = InvalidMaterialID;

			// Per-frame data
			std::vector<u32> indexBuffer;
			VertexBufferDataCreateInfo m_VertexBufferCreateInfo;

			GameObject* m_Object = nullptr;
		};
	} // namespace vk
} // namespace flex

#endif // COMPILE_VULKAN