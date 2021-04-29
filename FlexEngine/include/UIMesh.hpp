#pragma once

#include <vector>

#include "Graphics/Renderer.hpp"
#include "Graphics/VertexBufferData.hpp"
#include "Transform.hpp"
#include "Types.hpp"

namespace flex
{
	struct JSONObject;

	struct Rect
	{
		real minX, minY, maxX, maxY;
		glm::vec4 colour;
	};

	enum class RectCutType
	{
		LEFT,
		RIGHT,
		TOP,
		BOTTOM,

		_NONE
	};

	static const char* RectCutTypeStrings[] = {
		"Left",
		"Right",
		"Top",
		"Bottom",

		"NONE"
	};

	RectCutType RectCutTypeFromString(const char* cutTypeStr);

	struct UIScreen
	{
		UIScreen()
		{
		}

		UIScreen(RectCutType cutType, real amount, bool bVisible, bool bRelative,
			StringID tag, const std::vector<UIScreen>& children) :
			cutType(cutType),
			amount(amount),
			bVisible(bVisible),
			bRelative(bRelative),
			tag(tag),
			children(children)
		{
		}

		RectCutType cutType = RectCutType::_NONE;
		real amount = 0.0f;
		bool bVisible = true;
		bool bRelative = true;
		bool bHighlighted = false;
		StringID tag = InvalidStringID;

		// TODO: Add text/image elements here

		std::vector<UIScreen> children;

		Rect Cut(Rect* rect, const glm::vec4& normalColour, const glm::vec4& highlightedColour);
		glm::vec4 GetColour(const glm::vec4& normalColour, const glm::vec4& highlightedColour) const;
		JSONObject Serialize() const;
		static UIScreen Deserialize(const JSONObject& object);
	};

	Rect CutLeft(Rect* rect, real amount, bool bRelative, const glm::vec4& colour);
	Rect CutRight(Rect* rect, real amount, bool bRelative, const glm::vec4& colour);
	Rect CutTop(Rect* rect, real amount, bool bRelative, const glm::vec4& colour);
	Rect CutBottom(Rect* rect, real amount, bool bRelative, const glm::vec4& colour);

	class UIMesh final
	{
	public:
		UIMesh();
		~UIMesh();

		void Initialize();
		void Destroy();
		void OnPostSceneChange();

		bool Deserialize();
		void Serialize();

		void DrawRect(const glm::vec2& bottomLeft, const glm::vec2& topRight, const glm::vec4& colour, real cornerRadius);
		// Start angle (0 = right), End angle in radians (TWO_PI is full circle)
		void DrawArc(const glm::vec2& centerPos, real startAngle, real endAngle, real innerRadius, real thickness, i32 segmentsInFullCircle, const glm::vec4& colour);
		void DrawPolygon(const std::vector<glm::vec2>& points,
			const std::vector<glm::vec2>& texCoords,
			const std::vector<u32>& indices,
			const glm::vec4& colour,
			const glm::vec2& uvBlendAmount);

		void Draw();
		void DrawImGui(bool* bShowing);
		void EndFrame();

		Mesh* GetMesh();
		bool IsSubmeshActive(i32 submeshIndex);

		glm::vec2 GetUVBlendAmount(i32 drawIndex);

	private:
		enum class RectCutResult
		{
			REMOVE,
			DUPLICATE,
			DO_NOTHING
		};

		void CreateDebugObject();
		RectCutResult DrawImGuiRectCut(UIScreen& rectCut, const char* optionalTreeNodeName = nullptr);
		void ComputeRects(UIScreen& parent, Rect& rootRect, std::vector<Rect>& outputRects, const glm::vec4& normalColour, const glm::vec4& highlightedColour);

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

		UIScreen m_PlayerScreenUI;
		UIScreen m_PlayerInventoryUI;
		bool m_bUIConfigDirty = false;


		GameObject* m_Object = nullptr;
	};
} // namespace flex

