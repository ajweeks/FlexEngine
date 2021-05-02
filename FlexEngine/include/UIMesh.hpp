#pragma once

#include <vector>

#include "Graphics/Renderer.hpp"
#include "Graphics/VertexBufferData.hpp"
#include "Transform.hpp"
#include "Types.hpp"

namespace flex
{
	struct JSONObject;
	struct GameObjectStack;

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

		bool Overlaps(const glm::vec2& point)
		{
			return point.x >= minX && point.x < maxX &&
				point.y >= minY && point.y < maxY;
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

	static const char* RectCutTypeStrings[] =
	{
		"Left",
		"Right",
		"Top",
		"Bottom",

		"NONE"
	};

	static_assert((ARRAY_LENGTH(RectCutTypeStrings) - 1) == (u32)RectCutType::_NONE, "RectCutTypeStrings length did not match RectCutType enum");

	RectCutType RectCutTypeFromString(const char* cutTypeStr);

	enum class RectCutResult
	{
		REMOVE,
		DUPLICATE,
		CHANGE_SELF_TYPE,
		DO_NOTHING
	};

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

	enum class UIContainerType
	{
		BASE,
		ITEM,

		_NONE
	};

	static const char* UIContainerTypeStrings[] =
	{
		"Base",
		"Item",

		"NONE"
	};

	static_assert((ARRAY_LENGTH(UIContainerTypeStrings) - 1) == (u32)UIContainerType::_NONE, "UIContainerTypeStrings length did not match UIContainerType enum");

	UIContainerType UIContainerTypeFromString(const char* str);

	struct UIContainer
	{
		UIContainer(UIContainerType type = UIContainerType::BASE) :
			type(type)
		{
		}

		UIContainer(RectCutType cutType,
			real amount,
			bool bVisible,
			bool bRelative,
			StringID tag,
			const std::vector<UIContainer*>& children,
			UIContainerType type = UIContainerType::BASE) :
			cutType(cutType),
			amount(amount),
			bVisible(bVisible),
			bRelative(bRelative),
			tag(tag),
			children(children),
			type(type)
		{
		}

		virtual ~UIContainer()
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

		virtual void Update();

		RectCutResult DrawImGui(bool& bDirtyFlag, const char* optionalTreeNodeName = nullptr);

		RectCutType cutType = RectCutType::_NONE;
		real amount = 0.0f;
		bool bVisible = true;
		bool bRelative = true;
		bool bHighlighted = false;
		std::string tagSourceStr; // Editor only
		StringID tag = InvalidStringID;
		UIContainerType type = UIContainerType::_NONE;

		UIElement* uiElement = nullptr;

		std::vector<UIContainer*> children;

		Rect Cut(Rect* rect, const glm::vec4& normalColour, const glm::vec4& highlightedColour);
		glm::vec4 GetColour(const glm::vec4& normalColour, const glm::vec4& highlightedColour) const;
		JSONObject Serialize() const;
		static UIContainer* Deserialize(const JSONObject& object);
	};

	struct ItemUIContainer : public UIContainer
	{
		ItemUIContainer() :
			UIContainer(UIContainerType::ITEM)
		{
		}

		ItemUIContainer(RectCutType cutType, real amount, bool bVisible, bool bRelative,
			StringID tag, const std::vector<UIContainer*>& children, GameObjectStack* stack) :
			UIContainer(cutType, amount, bVisible, bRelative, tag, children, UIContainerType::ITEM),
			stack(stack)
		{
		}

		virtual void Update() override;

		GameObjectStack* stack = nullptr;
		bool bHovered = false;
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

