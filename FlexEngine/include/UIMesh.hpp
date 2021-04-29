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
		// [-1, 1]
		real minX, minY, maxX, maxY;
		glm::vec4 colour;

		void Scale(real scale)
		{
			real halfWidth = (maxX - minX) * 0.5f;
			real halfHeight = (maxY - minY) * 0.5f;
			glm::vec2 center(minX + halfWidth, minY + halfHeight);
			minX = center.x - halfWidth * scale;
			minY = center.y - halfHeight * scale;
			maxX = center.x + halfWidth * scale;
			maxY = center.y + halfHeight * scale;
		}
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

	enum class UIElementType
	{
		IMAGE,
		TEXT,

		_NONE
	};

	struct UIElement
	{
		UIElement(UIElementType type) :
			type(type)
		{}

		UIElementType type;
	};

	struct ImageUIElement : public UIElement
	{
		ImageUIElement() :
			UIElement(UIElementType::IMAGE)
		{}

		ImageUIElement(TextureID textureID) :
			UIElement(UIElementType::IMAGE),
			textureID(textureID)
		{}

		ImageUIElement(const ImageUIElement&) = delete;
		ImageUIElement(const ImageUIElement&&) = delete;
		ImageUIElement& operator=(const ImageUIElement&) = delete;
		ImageUIElement& operator=(const ImageUIElement&&) = delete;

		TextureID textureID;
	};

	struct TextUIElement : public UIElement
	{
		TextUIElement() :
			UIElement(UIElementType::TEXT)
		{}

		TextUIElement(const std::string& text, StringID fontID) :
			UIElement(UIElementType::TEXT),
			text(text),
			fontID(fontID)
		{}

		TextUIElement(const TextUIElement&) = delete;
		TextUIElement(const TextUIElement&&) = delete;
		TextUIElement& operator=(const TextUIElement&) = delete;
		TextUIElement& operator=(const TextUIElement&&) = delete;

		std::string text;
		StringID fontID;
	};

	struct UIContainer
	{
		UIContainer()
		{
		}

		UIContainer(RectCutType cutType, real amount, bool bVisible, bool bRelative,
			StringID tag, const std::vector<UIContainer*>& children) :
			cutType(cutType),
			amount(amount),
			bVisible(bVisible),
			bRelative(bRelative),
			tag(tag),
			children(children)
		{
		}

		~UIContainer()
		{
			delete uiElement;
			uiElement = nullptr;

			for (UIContainer* child : children)
			{
				delete child;
			}
			children.clear();
		}

		UIContainer(const UIContainer&) = delete;
		UIContainer(const UIContainer&&) = delete;
		UIContainer& operator=(const UIContainer&) = delete;
		UIContainer& operator=(const UIContainer&&) = delete;

		UIContainer* Duplicate();

		RectCutType cutType = RectCutType::_NONE;
		real amount = 0.0f;
		bool bVisible = true;
		bool bRelative = true;
		bool bHighlighted = false;
		std::string tagSourceStr; // Editor only
		StringID tag = InvalidStringID;

		UIElement* uiElement = nullptr;

		std::vector<UIContainer*> children;

		Rect Cut(Rect* rect, const glm::vec4& normalColour, const glm::vec4& highlightedColour);
		glm::vec4 GetColour(const glm::vec4& normalColour, const glm::vec4& highlightedColour) const;
		JSONObject Serialize() const;
		static void Deserialize(const JSONObject& object, UIContainer* uiContainer);
	};

	Rect CutLeft(Rect* rect, real amount, bool bRelative, const glm::vec4& colour);
	Rect CutRight(Rect* rect, real amount, bool bRelative, const glm::vec4& colour);
	Rect CutTop(Rect* rect, real amount, bool bRelative, const glm::vec4& colour);
	Rect CutBottom(Rect* rect, real amount, bool bRelative, const glm::vec4& colour);

	enum class RectCutResult
	{
		REMOVE,
		DUPLICATE,
		DO_NOTHING
	};

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

		glm::vec2 GetUVBlendAmount(i32 drawIndex);

		RectCutResult DrawImGuiRectCut(UIContainer* rectCut, bool& bDirtyFlag, const char* optionalTreeNodeName = nullptr);

		static void ComputeRects(UIContainer* parent, Rect& rootRect, std::vector<Rect>& outputRects, const glm::vec4& normalColour, const glm::vec4& highlightedColour);

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

		GameObject* m_Object = nullptr;
	};
} // namespace flex

