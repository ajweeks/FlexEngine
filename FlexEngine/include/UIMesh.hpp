#pragma once

#include <vector>

#include "Graphics/Renderer.hpp"
#include "Graphics/VertexBufferData.hpp"
#include "Transform.hpp"
#include "Types.hpp"

namespace flex
{
	class UIContainer;
	struct Rect;

	class UIMesh final
	{
	public:
		UIMesh();
		~UIMesh();

		void Initialize();
		void Destroy();
		void OnPostSceneChange();

		void DrawRect(const glm::vec2& bottomLeft, const glm::vec2& topRight, const glm::vec4& colour, real cornerRadius);
		// Start angle (0 = right), End angle in radians (TWO_PI is full circle)
		void DrawArc(const glm::vec2& centerPos, real startAngle, real endAngle, real innerRadius, real thickness, i32 segmentsInFullCircle, const glm::vec4& colour);
		void DrawPolygon(const std::vector<glm::vec2>& points,
			const std::vector<glm::vec2>& texCoords,
			const std::vector<u32>& indices,
			const glm::vec4& colour,
			const glm::vec2& uvBlendAmount);

		void Draw();
		void EndFrame();

		Mesh* GetMesh();
		bool IsSubmeshActive(i32 submeshIndex);
		i32 GetSubmeshCount();
		i32 GetUsedSubmeshCount();

		glm::vec2 GetUVBlendAmount(i32 drawIndex);

	private:
		void CreateDebugObject();

		void Clear();

		MaterialID m_MaterialID = InvalidMaterialID;

		struct DrawData
		{
			std::vector<u32> indexBuffer;
			VertexBufferDataCreateInfo vertexBufferCreateInfo;
			bool bInUse = false;
			glm::vec2 uvBlendAmount;
		};

		std::vector<DrawData*> m_DrawData;
		i32 m_LastUsedSubmeshCount = -1;

		GameObject* m_Object = nullptr;
	};
} // namespace flex
