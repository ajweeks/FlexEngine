#pragma once

#include <vector>

#include "Graphics/Renderer.hpp"
#include "Graphics/VertexBufferData.hpp"
#include "Transform.hpp"
#include "Types.hpp"

namespace flex
{
	class UIMesh final
	{
	public:
		UIMesh();
		~UIMesh();

		void Initialize();
		void Destroy();
		void OnPostSceneChange();

		void DrawArc(const glm::vec2& centerPos, real startAngle, real endAngle, real innerRadius, real thickness, i32 segmentsInFullCircle, const glm::vec4& colour);
		void DrawPolygon(const std::vector<glm::vec2>& points,
			const std::vector<glm::vec2>& texCoords,
			const std::vector<u32>& indices,
			const glm::vec4& colour);

		void Draw();
		void EndFrame();

		Mesh* GetMesh();
		bool IsSubmeshActive(i32 submeshIndex);

	private:
		void CreateDebugObject();

		void Clear();

		MaterialID m_MaterialID = InvalidMaterialID;

		struct DrawData
		{
			std::vector<u32> indexBuffer;
			VertexBufferDataCreateInfo vertexBufferCreateInfo;
			bool bInUse = false;
		};

		std::vector<DrawData*> m_DrawData;

		GameObject* m_Object = nullptr;
	};
} // namespace flex

