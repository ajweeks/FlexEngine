#pragma once

#include "InputTypes.hpp"

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
			return point.x >= minX && point.x < maxX&&
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
		explicit UIElement(UIElementType type) :
			type(type)
		{}

		UIElementType type;
	};

	struct ImageUIElement : public UIElement
	{
		ImageUIElement() :
			UIElement(UIElementType::IMAGE),
			textureID(InvalidTextureID)
		{}

		explicit ImageUIElement(TextureID textureID) :
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
			UIElement(UIElementType::TEXT),
			text(""),
			fontID(InvalidStringID)
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
		IMAGE,
		INVENTORY,
		QUICK_ACCESS,
		WEARABLES,

		_NONE
	};

	static const char* UIContainerTypeStrings[] =
	{
		"Base",
		"Item",
		"Image",
		"Inventory",
		"Quick Access",
		"Wearables",

		"NONE"
	};

	static_assert((ARRAY_LENGTH(UIContainerTypeStrings) - 1) == (u32)UIContainerType::_NONE, "UIContainerTypeStrings length did not match UIContainerType enum");

	UIContainerType UIContainerTypeFromString(const char* str);

	class UIContainer
	{
	public:
		explicit UIContainer(UIContainerType type = UIContainerType::BASE);

		virtual ~UIContainer();

		UIContainer(const UIContainer&) = delete;
		UIContainer(const UIContainer&&) = delete;
		UIContainer& operator=(const UIContainer&) = delete;
		UIContainer& operator=(const UIContainer&&) = delete;

		virtual UIContainer* Duplicate();
		virtual void Initialize();
		virtual void Update(Rect& parentRect, bool bIgnoreCut = false);
		virtual void Draw();
		virtual RectCutResult DrawImGui();

		void SerializeCommon(JSONObject& object);
		static UIContainer* DeserializeCommon(const JSONObject& object);

		static UIContainer* CreateContainer(UIContainerType containerType);

		Rect Cut(Rect* rect);
		glm::vec4 GetColour() const;

		void ClearDirty();

		static const glm::vec4 baseColour;
		static const glm::vec4 hoveredColour;
		static const glm::vec4 highlightedColour;

		RectCutType cutType = RectCutType::_NONE;
		Rect lastCutRect;
		real amount = 0.0f;
		real margin = 0.95f;
		bool bVisible = true;
		bool bRelative = true;
		bool bHighlighted = false;
		bool bHovered = false;
		bool bDirty = false; // Editor only
		std::string tagSourceStr; // Editor only
		StringID tag = InvalidStringID;
		UIContainerType type = UIContainerType::_NONE;

		std::vector<UIContainer*> children;

	protected:
		RectCutResult DrawImGuiBase(bool bTreeOpen);
		void DrawImGuiChildren();

	private:
		virtual void Serialize(JSONObject& rootObject);
		virtual void Deserialize(const JSONObject& rootObject);

	};

	class ItemUIContainer : public UIContainer
	{
	public:
		ItemUIContainer();
		ItemUIContainer(GameObjectStackID stackID, i32 index);

		~ItemUIContainer();

		virtual void Update(Rect& parentRect, bool bIgnoreCut = false) override;
		virtual RectCutResult DrawImGui() override;
		virtual void Serialize(JSONObject& rootObject) override;
		virtual void Deserialize(const JSONObject& rootObject) override;
		virtual UIContainer* Duplicate() override;

		ImageUIElement* imageElement = nullptr;
		u64 lastImageUpdatedStackTypeID = InvalidID;
		TextUIElement* textElement = nullptr;
		i32 lastTextElementUpdateCount = -1;

		GameObjectStackID stackID = InvalidID;
		i32 index = -1;
	private:

	};

	class ImageUIContainer : public UIContainer
	{
	public:
		ImageUIContainer();
		~ImageUIContainer();

		virtual void Update(Rect& parentRect, bool bIgnoreCut = false) override;
		virtual RectCutResult DrawImGui() override;
		virtual void Serialize(JSONObject& rootObject) override;
		virtual void Deserialize(const JSONObject& rootObject) override;
		virtual UIContainer* Duplicate() override;

		const static u32 iconNameBufLen = 64;
		char iconNameBuffer[iconNameBufLen];
		bool bUpdateIconNameBuf = true;
		std::string iconName;
		ImageUIElement* imageElement = nullptr;

		GameObjectStackID stackID = InvalidID;
		i32 index = -1;
	private:

	};

	class InventoryUIContainer : public UIContainer
	{
	public:
		InventoryUIContainer();

		virtual void Initialize() override;
		virtual void Update(Rect& parentRect, bool bIgnoreCut = false) override;
		virtual RectCutResult DrawImGui() override;

		std::vector<ItemUIContainer*> itemContainers;
		ItemUIContainer* dropContainer = nullptr;
		ItemUIContainer* trashContainer = nullptr;

	private:
		void OnLayoutChanged();

	};

	class QuickAccessItemUIContainer : public UIContainer
	{
	public:
		QuickAccessItemUIContainer();

		virtual void Initialize() override;
		virtual void Update(Rect& parentRect, bool bIgnoreCut = false) override;
		virtual RectCutResult DrawImGui() override;

		std::vector<ItemUIContainer*> itemContainers;

	private:
		void OnLayoutChanged();

	};

	class WearablesItemUIContainer : public UIContainer
	{
	public:
		WearablesItemUIContainer();

		virtual void Initialize() override;
		virtual void Update(Rect& parentRect, bool bIgnoreCut = false) override;
		virtual RectCutResult DrawImGui() override;

		std::vector<ItemUIContainer*> itemContainers;

	private:
		void OnLayoutChanged();

	};

	class UIManager
	{
	public:
		UIManager();

		void Initialize();
		void Destroy();
		void Update();
		void DrawImGui(bool* bUIEditorShowing);

		void Deserialize();
		void Serialize();

		bool MoveItemStack(ItemUIContainer* from, ItemUIContainer* to);
		bool MoveSingleItemFromStack(ItemUIContainer* from, ItemUIContainer* to);
		bool DropItemStack(ItemUIContainer* stack, bool bDestroyItem);
		bool DropSingleItemFromStack(ItemUIContainer* stack, bool bDestroyItem);

		void HandleBeginStackDrag(ItemUIContainer* itemContainer, GameObjectStack* stack);
		void BeginItemDrag(ItemUIContainer* draggedItem, MouseButton buttonDown);
		void EndItemDrag();

		void EnqueueImageSprite(TextureID textureID, Rect lastCutRect);

		InventoryUIContainer* playerInventoryUI = nullptr;
		QuickAccessItemUIContainer* playerQuickAccessUI = nullptr;
		WearablesItemUIContainer* wearablesInventoryUI = nullptr;

		ItemUIContainer* draggedUIContainer = nullptr;
		MouseButton mouseButtonDragging = MouseButton::_NONE;

	private:
		bool SerializeUIConfig(const char* filePath, UIContainer* uiContainer);
		UIContainer* ParseUIConfig(const char* filePath);
		bool SerializeUISettings();
		bool ParseUISettings();

		StringID m_ItemPickupSoundSID = InvalidStringID;
		StringID m_ItemDropSoundSID = InvalidStringID;
		StringID m_ItemTrashSoundSID = InvalidStringID;
		real m_ItemSoundGain = 0.5f;

	};

	Rect CutLeft(Rect* rect, real amount, bool bRelative, const glm::vec4& colour);
	Rect CutRight(Rect* rect, real amount, bool bRelative, const glm::vec4& colour);
	Rect CutTop(Rect* rect, real amount, bool bRelative, const glm::vec4& colour);
	Rect CutBottom(Rect* rect, real amount, bool bRelative, const glm::vec4& colour);
}// namespace flex
