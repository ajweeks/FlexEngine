#include "stdafx.hpp"

#include "UI.hpp"

#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "Graphics/Renderer.hpp"
#include "InputManager.hpp"
#include "JSONParser.hpp"
#include "Platform/Platform.hpp"
#include "Player.hpp"
#include "ResourceManager.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Window/Window.hpp"
#include "UIMesh.hpp"

namespace flex
{
	const glm::vec4 UIContainer::baseColour = glm::vec4(0.1, 0.1f, 0.1f, 1.0f);
	const glm::vec4 UIContainer::hoveredColour = glm::vec4(0.2, 0.2f, 0.2f, 1.0f);
	const glm::vec4 UIContainer::highlightedColour = glm::vec4(0.3, 0.3f, 0.3f, 1.0f);


	UIContainer::UIContainer(UIContainerType type /* = UIContainerType::BASE */) :
		type(type)
	{
		lastCutRect = {};
		cutType = RectCutType::TOP;
		amount = 1.0f;
	}

	UIContainer::~UIContainer()
	{
		for (UIContainer* child : children)
		{
			delete child;
		}
		children.clear();
	}

	UIContainer* UIContainer::Duplicate()
	{
		UIContainer* newUIContainer = CreateContainer(type);

		newUIContainer->cutType = cutType;
		newUIContainer->amount = amount;
		newUIContainer->bVisible = bVisible;
		newUIContainer->bRelative = bRelative;
		newUIContainer->tagSourceStr = tagSourceStr;
		newUIContainer->tag = tag;
		newUIContainer->children = children;

		for (UIContainer* child : children)
		{
			newUIContainer->children.emplace_back(child->Duplicate());
		}

		return newUIContainer;
	}

	void UIContainer::Initialize()
	{
	}

	void UIContainer::Update(Rect& parentRect, bool bIgnoreCut /* = false */)
	{
		if (!bIgnoreCut)
		{
			lastCutRect = Cut(&parentRect);
		}

		if (bHighlighted && bVisible)
		{
			// Grow when highlighted
			real growFactor = 1.05f;
			lastCutRect.Scale(growFactor);
		}

		for (UIContainer* child : children)
		{
			child->Update(lastCutRect, bIgnoreCut);
		}
	}

	void UIContainer::Draw()
	{
		if (lastCutRect.colour.a != 0.0f)
		{
			UIMesh* uiMesh = g_Renderer->GetUIMesh();

			Rect rectToDraw = lastCutRect;
			rectToDraw.Scale(margin);
			uiMesh->DrawRect(
				glm::vec2(rectToDraw.minX, rectToDraw.minY),
				glm::vec2(rectToDraw.maxX, rectToDraw.maxY), rectToDraw.colour, 0.0f);
		}

		for (UIContainer* child : children)
		{
			child->Draw();
		}
	}

	RectCutResult UIContainer::DrawImGui()
	{
		bool bTreeOpen = ImGui::TreeNode("Base");

		RectCutResult result = UIContainer::DrawImGuiBase(bTreeOpen);

		if (bTreeOpen)
		{
			UIContainer::DrawImGuiChildren();

			ImGui::TreePop();
		}

		return result;
	}

	RectCutResult UIContainer::DrawImGuiBase(bool bTreeOpen)
	{
		bool bRemoveSelf = false;
		bool bDuplicateSelf = false;
		bool bChangeSelfType = false;

		bHighlighted = ImGui::IsItemHovered();

		if (ImGui::BeginPopupContextItem(nullptr))
		{
			if (ImGui::Button("Duplicate"))
			{
				bDuplicateSelf = true;
			}
			ImGui::EndPopup();
		}

		if (bTreeOpen)
		{
			if (ImGui::BeginCombo("##type", UIContainerTypeStrings[(i32)type]))
			{
				for (u32 i = 0; i < (u32)ARRAY_LENGTH(UIContainerTypeStrings) - 1; ++i)
				{
					bool bSelected = (i == (u32)type);
					if (ImGui::Selectable(UIContainerTypeStrings[i], &bSelected))
					{
						type = (UIContainerType)i;
						bDirty = true;
						bChangeSelfType = true;
					}

				}
				ImGui::EndCombo();
			}

			if (ImGui::BeginCombo("##cutType", RectCutTypeStrings[(i32)cutType]))
			{
				for (u32 i = 0; i < (u32)ARRAY_LENGTH(RectCutTypeStrings) - 1; ++i)
				{
					bool bSelected = (i == (u32)cutType);
					if (ImGui::Selectable(RectCutTypeStrings[i], &bSelected))
					{
						cutType = (RectCutType)i;
						bDirty = true;
					}

				}
				ImGui::EndCombo();
			}

			bDirty = ImGui::SliderFloat("##amount", &amount, 0.0f, 1.0f) || bDirty;

			tagSourceStr.resize(256);
			if (ImGui::InputText("Tag", (char*)tagSourceStr.c_str(), 256, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				tagSourceStr = std::string(tagSourceStr.c_str());
				tag = Hash(tagSourceStr.c_str());
				bDirty = true;
			}

			bDirty = ImGui::Checkbox("Visible", &bVisible) || bDirty;

			ImGui::SameLine();

			bDirty = ImGui::Checkbox("Relative", &bRelative) || bDirty;

			ImGui::SameLine();

			if (ImGui::Button("-"))
			{
				bRemoveSelf = true;
			}

			ImGui::SameLine();

			if (ImGui::Button("+"))
			{
				children.emplace_back(new UIContainer());
				bDirty = true;
			}
		}

		if (bRemoveSelf)
		{
			return RectCutResult::REMOVE;
		}
		if (bDuplicateSelf)
		{
			return RectCutResult::DUPLICATE;
		}
		if (bChangeSelfType)
		{
			return RectCutResult::CHANGE_SELF_TYPE;
		}

		return RectCutResult::DO_NOTHING;
	}

	void UIContainer::DrawImGuiChildren()
	{
		for (i32 i = 0; i < (i32)children.size(); ++i)
		{
			ImGui::PushID(i);
			RectCutResult result = children[i]->DrawImGui();
			ImGui::PopID();

			if (result == RectCutResult::REMOVE)
			{
				children.erase(children.begin() + i);
				bDirty = true;
				if (i > 0)
				{
					--i;
				}
			}
			else if (result == RectCutResult::DUPLICATE)
			{
				children.emplace(children.begin() + i, children[i]->Duplicate());
				bDirty = true;
			}
			else if (result == RectCutResult::CHANGE_SELF_TYPE)
			{
				UIContainer* oldUIContainer = children[i];

				// type will have already been changed to new value
				UIContainer* newUIContainer = CreateContainer(oldUIContainer->type);
				newUIContainer->children = oldUIContainer->children;
				oldUIContainer->children.clear(); // Prevent container destructor from deleting moved children
				newUIContainer->bVisible = oldUIContainer->bVisible;
				newUIContainer->cutType = oldUIContainer->cutType;
				newUIContainer->amount = oldUIContainer->amount;
				newUIContainer->tagSourceStr = oldUIContainer->tagSourceStr;
				newUIContainer->tag = oldUIContainer->tag;

				children[i] = newUIContainer;
				delete oldUIContainer;
				bDirty = true;
			}

			if (children[i]->bDirty)
			{
				bDirty = true;
			}
		}
	}

	Rect UIContainer::Cut(Rect* rect)
	{
		glm::vec4 colour = GetColour();

		switch (cutType)
		{
		case RectCutType::LEFT: return CutLeft(rect, amount, bRelative, colour);
		case RectCutType::RIGHT: return CutRight(rect, amount, bRelative, colour);
		case RectCutType::TOP: return CutTop(rect, amount, bRelative, colour);
		case RectCutType::BOTTOM: return CutBottom(rect, amount, bRelative, colour);
		}

		PrintError("Unhandled rect cut type\n");
		return {};
	}

	glm::vec4 UIContainer::GetColour() const
	{
		glm::vec4 colour = bHighlighted ? highlightedColour : baseColour;
		if (!bVisible)
		{
			colour.a = bHighlighted ? 0.2f : 0.0f;
		}
		return colour;
	}

	void UIContainer::ClearDirty()
	{
		bDirty = false;

		for (u32 i = 0; i < (u32)children.size(); ++i)
		{
			children[i]->ClearDirty();
		}
	}

	void UIContainer::SerializeCommon(JSONObject& rootObject)
	{
		rootObject.fields.emplace_back("type", JSONValue(UIContainerTypeStrings[(i32)type]));
		rootObject.fields.emplace_back("cut type", JSONValue(RectCutTypeStrings[(i32)cutType]));
		rootObject.fields.emplace_back("amount", JSONValue(amount));
		rootObject.fields.emplace_back("visible", JSONValue(bVisible));
		rootObject.fields.emplace_back("relative", JSONValue(bRelative));
		// TODO: Skip serializing in release builds
		std::string tagSourceStrStripped = std::string(tagSourceStr.c_str());
		if (!tagSourceStrStripped.empty())
		{
			rootObject.fields.emplace_back("tag source string", JSONValue(tagSourceStrStripped));
		}

		if (tag != InvalidStringID)
		{
			rootObject.fields.emplace_back("tag", JSONValue((u64)tag));
		}

		if (!children.empty())
		{
			std::vector<JSONObject> childrenObjects;
			childrenObjects.reserve(children.size());
			for (UIContainer* child : children)
			{
				JSONObject rootChildObject = {};
				child->SerializeCommon(rootChildObject);
				childrenObjects.emplace_back(rootChildObject);
			}
			rootObject.fields.emplace_back("children", JSONValue(childrenObjects));
		}

		Serialize(rootObject);
	}

	UIContainer* UIContainer::CreateContainer(UIContainerType containerType)
	{
		UIContainer* uiContainer;
		switch (containerType)
		{
		case UIContainerType::BASE:
		{
			uiContainer = new UIContainer();
		} break;
		case UIContainerType::ITEM:
		{
			uiContainer = new ItemUIContainer();
		} break;
		case UIContainerType::IMAGE:
		{
			uiContainer = new ImageUIContainer();
		} break;
		case UIContainerType::INVENTORY:
		{
			uiContainer = new InventoryUIContainer();
		} break;
		case UIContainerType::QUICK_ACCESS:
		{
			uiContainer = new QuickAccessItemUIContainer();
		} break;
		case UIContainerType::WEARABLES:
		{
			uiContainer = new WearablesItemUIContainer();
		} break;
		default:
		{
			ENSURE_NO_ENTRY();
			PrintError("Unhandled container type in UIContainer::DeserializeCommon\n");
			return nullptr;
		} break;
		}

		return uiContainer;
	}

	UIContainer* UIContainer::DeserializeCommon(const JSONObject& rootObject)
	{
		UIContainerType containerType = UIContainerType::BASE;

		std::string containerTypeStr;
		if (rootObject.TryGetString("type", containerTypeStr))
		{
			containerType = UIContainerTypeFromString(containerTypeStr.c_str());
		}

		UIContainer* uiContainer = CreateContainer(containerType);

		std::string cutTypeStr;
		if (rootObject.TryGetString("cut type", cutTypeStr))
		{
			uiContainer->cutType = RectCutTypeFromString(cutTypeStr.c_str());
		}
		rootObject.TryGetFloat("amount", uiContainer->amount);
		rootObject.TryGetBool("visible", uiContainer->bVisible);
		rootObject.TryGetBool("relative", uiContainer->bRelative);
		rootObject.TryGetString("tag source string", uiContainer->tagSourceStr);
		rootObject.TryGetStringID("tag", uiContainer->tag);
		if (!uiContainer->tagSourceStr.empty())
		{
			// Use source string when present to allow user edits of string
			uiContainer->tag = Hash(uiContainer->tagSourceStr.c_str());
		}

		std::vector<JSONObject> childrenObjects;
		if (rootObject.TryGetObjectArray("children", childrenObjects))
		{
			uiContainer->children.resize(childrenObjects.size());
			for (u32 i = 0; i < (u32)childrenObjects.size(); ++i)
			{
				uiContainer->children[i] = UIContainer::DeserializeCommon(childrenObjects[i]);
			}
		}

		uiContainer->Deserialize(rootObject);

		return uiContainer;
	}

	void UIContainer::Serialize(JSONObject& rootObject)
	{
		FLEX_UNUSED(rootObject);
	}

	void UIContainer::Deserialize(const JSONObject& rootObject)
	{
		FLEX_UNUSED(rootObject);
	}

	ItemUIContainer::ItemUIContainer() :
		ItemUIContainer(InvalidID, -1)
	{
	}

	ItemUIContainer::ItemUIContainer(GameObjectStackID stackID, i32 index) :
		UIContainer(UIContainerType::ITEM),
		stackID(stackID),
		index(index)
	{
		margin = 0.9f;
	}

	ItemUIContainer::~ItemUIContainer()
	{
		delete imageElement;
		imageElement = nullptr;
		lastImageUpdatedStackTypeID = InvalidID;
		delete textElement;
		textElement = nullptr;
		lastTextElementUpdateCount = -1;
	}

	void ItemUIContainer::Update(Rect& parentRect, bool bIgnoreCut /* = false */)
	{
		UIContainer::Update(parentRect, bIgnoreCut);

		if (stackID != InvalidID)
		{
			Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
			if (player != nullptr)
			{
				InventoryType inventoryType;
				GameObjectStack* stack = player->GetGameObjectStackFromInventory(stackID, inventoryType);

				if (imageElement == nullptr)
				{
					TextureID textureID = InvalidTextureID;

					if (stack != nullptr && stack->prefabID.IsValid())
					{
						GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(stack->prefabID);
						if (prefabTemplate != nullptr)
						{
							StringID stackTypeID = prefabTemplate->GetTypeID();
							textureID = g_ResourceManager->GetOrLoadIcon(stackTypeID);
						}

						if (textureID == InvalidTextureID)
						{
							// No icon exists for type, use placeholder
							textureID = g_ResourceManager->tofuIconID;
						}
					}

					if (textureID != InvalidTextureID)
					{
						imageElement = new ImageUIElement(textureID);
						textElement = new TextUIElement(IntToString(stack->count), SID("editor-01"));
					}
				}
				else
				{
					if (stack == nullptr || (!stack->prefabID.IsValid() || stack->count == 0))
					{
						delete imageElement;
						imageElement = nullptr;
						lastImageUpdatedStackTypeID = InvalidID;
						delete textElement;
						textElement = nullptr;
						lastTextElementUpdateCount = -1;
					}
					else
					{
						// Update text & image elements

						if (textElement != nullptr)
						{
							if (lastTextElementUpdateCount != stack->count)
							{
								textElement->text = IntToString(stack->count);
								lastTextElementUpdateCount = stack->count;
							}
						}

						if (imageElement != nullptr)
						{
							GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(stack->prefabID);
							if (prefabTemplate != nullptr)
							{
								StringID stackTypeID = prefabTemplate->GetTypeID();
								if (lastImageUpdatedStackTypeID != stackTypeID)
								{
									imageElement->textureID = g_ResourceManager->GetOrLoadIcon(stackTypeID);
									lastImageUpdatedStackTypeID = stackTypeID;
								}
							}

							if (imageElement->textureID == InvalidTextureID)
							{
								// No icon exists for type, use placeholder
								imageElement->textureID = g_ResourceManager->tofuIconID;
							}
						}
					}
				}

				if (imageElement != nullptr)
				{
					g_UIManager->EnqueueImageSprite(imageElement->textureID, lastCutRect);
				}

				if (textElement != nullptr)
				{
					real width = (lastCutRect.maxX - lastCutRect.minX);
					real height = (lastCutRect.maxY - lastCutRect.minY);
					glm::vec2 pos = glm::vec2(
						(lastCutRect.minX + width * 0.75f),
						lastCutRect.minY + height * 0.20f);
					real spacing = 0.1f;
					real scale = 0.4f;

					g_Renderer->SetFont(textElement->fontID);
					g_Renderer->DrawStringSS(textElement->text, VEC4_ONE, AnchorPoint::CENTER, pos, spacing, scale);
				}
			}

		}
		else
		{
			if (imageElement != nullptr)
			{
				delete imageElement;
				imageElement = nullptr;
				lastImageUpdatedStackTypeID = InvalidID;
				delete textElement;
				textElement = nullptr;
				lastTextElementUpdateCount = -1;
			}
		}
	}

	RectCutResult ItemUIContainer::DrawImGui()
	{
		bool bTreeOpen = ImGui::TreeNode("Item");

		RectCutResult result = UIContainer::DrawImGuiBase(bTreeOpen);

		if (bTreeOpen)
		{
			ImGui::Text("Index: %i", index);

			if (stackID != InvalidID)
			{
				Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
				if (player != nullptr)
				{
					InventoryType inventoryType;
					GameObjectStack* stack = player->GetGameObjectStackFromInventory(stackID, inventoryType);
					ImGui::Text("Stack count: %d", (stack->count > 0 ? stack->count : 0));
					std::string stackTypeString = stack->prefabID.ToString();
					ImGui::Text("Stack type: %s", stackTypeString.c_str());
				}
			}

			UIContainer::DrawImGuiChildren();

			ImGui::TreePop();
		}

		return result;
	}

	void ItemUIContainer::Serialize(JSONObject& rootObject)
	{
		JSONObject itemObject = {};

		itemObject.fields.emplace_back("index", JSONValue(index));
		// NOTE: stack is not serialized

		rootObject.fields.emplace_back("item", JSONValue(itemObject));
	}

	void ItemUIContainer::Deserialize(const JSONObject& rootObject)
	{
		JSONObject itemObject;
		if (rootObject.TryGetObject("item", itemObject))
		{
			itemObject.TryGetInt("index", index);
			// NOTE: stack is not serialized
		}
	}

	UIContainer* ItemUIContainer::Duplicate()
	{
		ItemUIContainer* result = (ItemUIContainer*)UIContainer::Duplicate();

		if (imageElement != nullptr)
		{
			result->imageElement = new ImageUIElement(imageElement->textureID);
		}

		if (textElement != nullptr)
		{
			result->textElement = new TextUIElement(textElement->text, textElement->fontID);
		}

		result->stackID = stackID;
		result->index = -1;

		return result;
	}

	ImageUIContainer::ImageUIContainer() :
		UIContainer(UIContainerType::IMAGE)
	{
		margin = 0.9f;
	}

	ImageUIContainer::~ImageUIContainer()
	{
		delete imageElement;
		imageElement = nullptr;
	}

	void ImageUIContainer::Update(Rect& parentRect, bool bIgnoreCut /* = false */)
	{
		UIContainer::Update(parentRect, bIgnoreCut);

		if (imageElement == nullptr)
		{
			TextureID textureID = InvalidTextureID;

			if (!iconName.empty())
			{
				textureID = g_ResourceManager->GetOrLoadTexture(ICON_DIRECTORY + iconName + "-icon-256.png");
			}

			if (textureID == InvalidTextureID)
			{
				// Invalid icon name, use placeholder
				textureID = g_ResourceManager->tofuIconID;
			}

			if (textureID != InvalidTextureID)
			{
				imageElement = new ImageUIElement(textureID);
			}
		}

		if (imageElement != nullptr)
		{
			g_UIManager->EnqueueImageSprite(imageElement->textureID, lastCutRect);
		}
	}

	RectCutResult ImageUIContainer::DrawImGui()
	{
		bool bTreeOpen = ImGui::TreeNode("Image");

		RectCutResult result = UIContainer::DrawImGuiBase(bTreeOpen);

		if (bTreeOpen)
		{
			if (bUpdateIconNameBuf)
			{
				bUpdateIconNameBuf = false;
				memset(iconNameBuffer, 0, iconNameBufLen);
				strcpy_s(iconNameBuffer, iconNameBufLen, iconName.c_str());
			}

			if (ImGui::InputText("Icon name", iconNameBuffer, iconNameBufLen, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				iconName = std::string(iconNameBuffer);

				delete imageElement;
				imageElement = nullptr;
			}

			UIContainer::DrawImGuiChildren();

			ImGui::TreePop();
		}

		return result;
	}

	void ImageUIContainer::Serialize(JSONObject& rootObject)
	{
		JSONObject imageObject = {};

		imageObject.fields.emplace_back("icon name", JSONValue(iconName));

		rootObject.fields.emplace_back("image", JSONValue(imageObject));
	}

	void ImageUIContainer::Deserialize(const JSONObject& rootObject)
	{
		JSONObject imageObject;
		if (rootObject.TryGetObject("image", imageObject))
		{
			imageObject.TryGetString("icon name", iconName);
			bUpdateIconNameBuf = true;
		}
	}

	UIContainer* ImageUIContainer::Duplicate()
	{
		ImageUIContainer* result = (ImageUIContainer*)UIContainer::Duplicate();

		if (imageElement != nullptr)
		{
			result->imageElement = new ImageUIElement(imageElement->textureID);
		}

		result->iconName = iconName;
		result->bUpdateIconNameBuf = true;

		return result;
	}

	InventoryUIContainer::InventoryUIContainer() :
		UIContainer(UIContainerType::INVENTORY)
	{
	}

	void InventoryUIContainer::Initialize()
	{
		OnLayoutChanged();
	}

	RectCutResult InventoryUIContainer::DrawImGui()
	{
		bool bTreeOpen = ImGui::TreeNode("Inventory");

		RectCutResult result = UIContainer::DrawImGuiBase(bTreeOpen);

		if (bTreeOpen)
		{
			UIContainer::DrawImGuiChildren();

			ImGui::TreePop();
		}

		return result;
	}

	void InventoryUIContainer::OnLayoutChanged()
	{
		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		if (player != nullptr)
		{
			Print("Inventory children: %u\n", (u32)children.size());

			if (!children.empty())
			{
				// Compute list of all children, breadth-first to find all slots (assuming slots are immediate siblings)
				std::vector<UIContainer*> uiContainers;
				std::vector<UIContainer*> uiContainersToPush;

				uiContainers.push_back(this);
				uiContainersToPush.push_back(this);

				while (!uiContainersToPush.empty())
				{
					UIContainer* uiContainer = uiContainersToPush[0];
					for (UIContainer* child : uiContainer->children)
					{
						uiContainers.push_back(child);
						if (!child->children.empty())
						{
							uiContainersToPush.push_back(child);
						}
					}
					uiContainersToPush.erase(uiContainersToPush.begin());
				}

				itemContainers.clear();
				itemContainers.reserve(Player::INVENTORY_ITEM_COUNT);
				const char* dropTagStr = "drop";
				const char* trashTagStr = "trash";
				// Fill out item containers by finding tags pointing to start of each column of tiles
				for (i32 c = 0; c < Player::INVENTORY_ITEM_COL_COUNT; ++c)
				{
					std::string colTagStr = "column" + IntToString(c);
					for (i32 i = 0; i < (i32)uiContainers.size(); ++i)
					{
						UIContainer* uiContainer = uiContainers[i];
						if (uiContainer->tag == Hash(colTagStr.c_str()))
						{
							if (((i32)uiContainers.size() - i) >= Player::INVENTORY_ITEM_ROW_COUNT)
							{
								for (i32 r = 0; r < Player::INVENTORY_ITEM_ROW_COUNT; ++r)
								{
									ItemUIContainer* itemContainer = (ItemUIContainer*)uiContainers[i + r];
									itemContainer->index = (c * Player::INVENTORY_ITEM_ROW_COUNT) + r;
									itemContainer->stackID = Player::GetGameObjectStackIDForInventory(itemContainer->index);

									if (itemContainer->stackID != InvalidID)
									{
										itemContainers.emplace_back(itemContainer);
									}
								}
							}
							else
							{
								PrintError("Invalid inventory config, expected %d UI containers immediately after container with tag %s\n", Player::INVENTORY_ITEM_ROW_COUNT, colTagStr.c_str());
							}

							break;
						}
					}
				}

				for (i32 i = 0; i < (i32)uiContainers.size(); ++i)
				{
					UIContainer* uiContainer = uiContainers[i];
					if (uiContainer->tag == SID(dropTagStr))
					{
						dropContainer = (ItemUIContainer*)uiContainer;
					}
					else if (uiContainer->tag == SID(trashTagStr))
					{
						trashContainer = (ItemUIContainer*)uiContainer;
					}
				}
			}

			if ((i32)itemContainers.size() != Player::INVENTORY_ITEM_COUNT)
			{
				PrintWarn("Failed to find %i items in InventoryUIContainer::OnLayoutChanged (found %i)\n",
					Player::INVENTORY_ITEM_COUNT, (i32)itemContainers.size());
			}
		}
	}

	void InventoryUIContainer::Update(Rect& parentRect, bool bIgnoreCut /* = false */)
	{
		UIContainer::Update(parentRect, bIgnoreCut);

		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		if (player != nullptr)
		{
			if ((i32)itemContainers.size() == Player::INVENTORY_ITEM_COUNT)
			{
				for (i32 n = 0; n < Player::INVENTORY_ITEM_COUNT; ++n)
				{
					itemContainers[n]->bHighlighted = false;
				}

				if (player->bInventoryShowing && !ImGui::GetIO().WantCaptureMouse)
				{
					glm::vec2i windowSize = g_Window->GetSize();
					glm::vec2 mousePos = g_InputManager->GetMousePosition();
					glm::vec2 mousePosN(mousePos.x / windowSize.x * 2.0f - 1.0f, -(mousePos.y / windowSize.y * 2.0f - 1.0f));

					if ((i32)itemContainers.size() == Player::INVENTORY_ITEM_COUNT)
					{
						for (ItemUIContainer* itemContainer : itemContainers)
						{
							bool bOverlaps = itemContainer->lastCutRect.Overlaps(mousePosN);
							itemContainer->bHovered = bOverlaps;
							itemContainer->bHighlighted = bOverlaps;

							if (bOverlaps)
							{
								itemContainer->lastCutRect.colour = UIContainer::hoveredColour;

								InventoryType inventoryType;
								GameObjectStack* stack = player->GetGameObjectStackFromInventory(itemContainer->stackID, inventoryType);
								assert(inventoryType == InventoryType::INVENTORY);

								if (g_InputManager->IsMouseButtonPressed(MouseButton::LEFT) &&
									stack != nullptr && stack->count > 0)
								{
									g_UIManager->BeginItemDrag(itemContainer);
								}

								if (g_UIManager->draggedUIContainer != nullptr &&
									itemContainer != g_UIManager->draggedUIContainer)
								{
									if (g_InputManager->IsMouseButtonReleased(MouseButton::LEFT))
									{
										if (g_UIManager->MoveItemStack(g_UIManager->draggedUIContainer, itemContainer))
										{
											g_UIManager->EndItemDrag();
										}
									}
								}
							}
						}

						// Drop area
						if (dropContainer != nullptr)
						{
							bool bOverlaps = dropContainer->lastCutRect.Overlaps(mousePosN);
							dropContainer->bHovered = bOverlaps;
							dropContainer->bHighlighted = bOverlaps;

							if (bOverlaps)
							{
								dropContainer->lastCutRect.colour = UIContainer::hoveredColour;

								if (g_UIManager->draggedUIContainer != nullptr &&
									g_InputManager->IsMouseButtonReleased(MouseButton::LEFT))
								{
									if (g_UIManager->DropItemStack(g_UIManager->draggedUIContainer, false))
									{
										g_UIManager->EndItemDrag();
									}
								}
							}
						}

						// Trash area
						if (trashContainer != nullptr)
						{
							bool bOverlaps = trashContainer->lastCutRect.Overlaps(mousePosN);
							trashContainer->bHovered = bOverlaps;
							trashContainer->bHighlighted = bOverlaps;

							if (bOverlaps)
							{
								trashContainer->lastCutRect.colour = UIContainer::hoveredColour;

								if (g_UIManager->draggedUIContainer != nullptr &&
									g_InputManager->IsMouseButtonReleased(MouseButton::LEFT))
								{
									if (g_UIManager->DropItemStack(g_UIManager->draggedUIContainer, true))
									{
										g_UIManager->EndItemDrag();
									}
								}
							}
						}
					}
				}
			}
		}
	}

	QuickAccessItemUIContainer::QuickAccessItemUIContainer() :
		UIContainer(UIContainerType::QUICK_ACCESS)
	{
	}

	void QuickAccessItemUIContainer::Initialize()
	{
		OnLayoutChanged();
	}

	RectCutResult QuickAccessItemUIContainer::DrawImGui()
	{
		bool bTreeOpen = ImGui::TreeNode("Quick access");

		RectCutResult result = UIContainer::DrawImGuiBase(bTreeOpen);

		if (bTreeOpen)
		{
			UIContainer::DrawImGuiChildren();

			ImGui::TreePop();
		}

		return result;
	}

	void QuickAccessItemUIContainer::OnLayoutChanged()
	{
		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		if (player != nullptr)
		{
			Print("Quick access children: %u\n", (u32)children.size());

			if (!children.empty())
			{
				// Compute list of all children, breadth-first to find all slots (assuming slots are immediate siblings)
				std::vector<UIContainer*> uiContainers;
				std::vector<UIContainer*> uiContainersToPush;

				uiContainers.push_back(children[0]);
				uiContainersToPush.push_back(children[0]);

				while (!uiContainersToPush.empty())
				{
					UIContainer* uiContainer = uiContainersToPush[0];
					for (UIContainer* child : uiContainer->children)
					{
						uiContainers.push_back(child);
						if (!child->children.empty())
						{
							uiContainersToPush.push_back(child);
						}
					}
					uiContainersToPush.erase(uiContainersToPush.begin());
				}

				const char* tagStr = "slot0";
				for (i32 i = 0; i < (i32)uiContainers.size(); ++i)
				{
					UIContainer* uiContainer = uiContainers[i];
					if (uiContainer->tag == SID(tagStr))
					{
						if (((i32)uiContainers.size() - i) >= Player::QUICK_ACCESS_ITEM_COUNT)
						{
							itemContainers.clear();
							itemContainers.reserve(Player::QUICK_ACCESS_ITEM_COUNT);
							for (i32 n = 0; n < Player::QUICK_ACCESS_ITEM_COUNT; ++n)
							{
								ItemUIContainer* itemContainer = (ItemUIContainer*)uiContainers[i + n];
								itemContainer->index = n;
								itemContainer->stackID = Player::GetGameObjectStackIDForQuickAccessInventory(n);

								if (itemContainer->stackID != InvalidID)
								{
									itemContainers.emplace_back(itemContainer);
								}
							}
						}
						else
						{
							PrintError("Invalid quick access inventory config, expected %d UI containers immediately after container with tag %s\n", Player::QUICK_ACCESS_ITEM_COUNT, tagStr);
						}

						break;
					}
				}
			}

			if ((i32)itemContainers.size() != Player::QUICK_ACCESS_ITEM_COUNT)
			{
				PrintWarn("Failed to find %i items in QuickAccessItemUIContainer::OnLayoutChanged (found %i)\n",
					Player::QUICK_ACCESS_ITEM_COUNT, (i32)itemContainers.size());
			}
		}
	}

	void QuickAccessItemUIContainer::Update(Rect& parentRect, bool bIgnoreCut /* = false */)
	{
		UIContainer::Update(parentRect, bIgnoreCut);

		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		if (player != nullptr)
		{
			if ((i32)itemContainers.size() == Player::QUICK_ACCESS_ITEM_COUNT)
			{
				for (i32 n = 0; n < Player::QUICK_ACCESS_ITEM_COUNT; ++n)
				{
					itemContainers[n]->bHighlighted = (n == player->heldItemSlot);
				}

				if (player->bInventoryShowing && !ImGui::GetIO().WantCaptureMouse)
				{
					glm::vec2i windowSize = g_Window->GetSize();
					glm::vec2 mousePos = g_InputManager->GetMousePosition();
					glm::vec2 mousePosN(mousePos.x / windowSize.x * 2.0f - 1.0f, -(mousePos.y / windowSize.y * 2.0f - 1.0f));

					for (ItemUIContainer* itemContainer : itemContainers)
					{
						bool bOverlaps = itemContainer->lastCutRect.Overlaps(mousePosN);
						itemContainer->bHovered = bOverlaps;

						if (bOverlaps)
						{
							itemContainer->lastCutRect.colour = UIContainer::hoveredColour;

							if (g_InputManager->IsMouseButtonPressed(MouseButton::LEFT))
							{
								InventoryType inventoryType;
								GameObjectStack* stack = player->GetGameObjectStackFromInventory(itemContainer->stackID, inventoryType);
								assert(inventoryType == InventoryType::QUICK_ACCESS);

								if (stack != nullptr && stack->count > 0)
								{
									g_UIManager->BeginItemDrag(itemContainer);
								}
							}

							if (g_UIManager->draggedUIContainer != nullptr &&
								itemContainer != g_UIManager->draggedUIContainer)
							{
								if (g_InputManager->IsMouseButtonReleased(MouseButton::LEFT))
								{
									if (g_UIManager->MoveItemStack(g_UIManager->draggedUIContainer, itemContainer))
									{
										g_UIManager->EndItemDrag();
									}
								}
							}
						}
					}
				}
			}
		}
	}

	WearablesItemUIContainer::WearablesItemUIContainer() :
		UIContainer(UIContainerType::WEARABLES)
	{
	}

	void WearablesItemUIContainer::Initialize()
	{
		OnLayoutChanged();
	}

	RectCutResult WearablesItemUIContainer::DrawImGui()
	{
		bool bTreeOpen = ImGui::TreeNode("Wearables");

		RectCutResult result = UIContainer::DrawImGuiBase(bTreeOpen);

		if (bTreeOpen)
		{
			UIContainer::DrawImGuiChildren();

			ImGui::TreePop();
		}

		return result;
	}

	void WearablesItemUIContainer::OnLayoutChanged()
	{
		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		if (player != nullptr)
		{
			Print("Wearables inventory children: %u\n", (u32)children.size());

			if (!children.empty())
			{
				// Compute list of all children, breadth-first to find all slots (assuming slots are immediate siblings)
				std::vector<UIContainer*> uiContainers;
				std::vector<UIContainer*> uiContainersToPush;

				uiContainers.push_back(this);
				uiContainersToPush.push_back(this);

				while (!uiContainersToPush.empty())
				{
					UIContainer* uiContainer = uiContainersToPush[0];
					for (UIContainer* child : uiContainer->children)
					{
						uiContainers.push_back(child);
						if (!child->children.empty())
						{
							uiContainersToPush.push_back(child);
						}
					}
					uiContainersToPush.erase(uiContainersToPush.begin());
				}

				itemContainers.clear();
				itemContainers.reserve(Player::WEARABLES_ITEM_COUNT);
				// Fill out item containers by finding tags pointing to start of each column of tiles

				const char* tagStr = "slot0";
				for (i32 i = 0; i < (i32)uiContainers.size(); ++i)
				{
					UIContainer* uiContainer = uiContainers[i];
					if (uiContainer->tag == SID(tagStr))
					{
						if (((i32)uiContainers.size() - i) >= Player::WEARABLES_ITEM_COUNT)
						{
							for (i32 n = 0; n < Player::WEARABLES_ITEM_COUNT; ++n)
							{
								ItemUIContainer* itemContainer = (ItemUIContainer*)uiContainers[i + n];
								itemContainer->index = n;
								itemContainer->stackID = Player::GetGameObjectStackIDForWearablesInventory(n);

								if (itemContainer->stackID != InvalidID)
								{
									itemContainers.emplace_back(itemContainer);
								}
							}
						}
						else
						{
							PrintError("Invalid wearables config, expected %d UI containers immediately after container with tag %s\n", Player::WEARABLES_ITEM_COUNT, tagStr);
						}

						break;
					}
				}
			}

			if ((i32)itemContainers.size() != Player::WEARABLES_ITEM_COUNT)
			{
				PrintWarn("Failed to find %i items in WearablesItemUIContainer::OnLayoutChanged (found %i)\n",
					Player::WEARABLES_ITEM_COUNT, (i32)itemContainers.size());
			}
		}
	}

	void WearablesItemUIContainer::Update(Rect& parentRect, bool bIgnoreCut /* = false */)
	{
		UIContainer::Update(parentRect, bIgnoreCut);

		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		if (player != nullptr)
		{
			if ((i32)itemContainers.size() == Player::WEARABLES_ITEM_COUNT)
			{
				for (i32 n = 0; n < Player::WEARABLES_ITEM_COUNT; ++n)
				{
					itemContainers[n]->bHighlighted = false;
				}

				if (player->bInventoryShowing && !ImGui::GetIO().WantCaptureMouse)
				{
					if ((i32)itemContainers.size() == Player::WEARABLES_ITEM_COUNT)
					{
						glm::vec2i windowSize = g_Window->GetSize();
						glm::vec2 mousePos = g_InputManager->GetMousePosition();
						glm::vec2 mousePosN(mousePos.x / windowSize.x * 2.0f - 1.0f, -(mousePos.y / windowSize.y * 2.0f - 1.0f));

						for (ItemUIContainer* itemContainer : itemContainers)
						{
							bool bOverlaps = itemContainer->lastCutRect.Overlaps(mousePosN);
							itemContainer->bHovered = bOverlaps;
							itemContainer->bHighlighted = bOverlaps;

							if (bOverlaps)
							{
								itemContainer->lastCutRect.colour = UIContainer::hoveredColour;

								InventoryType inventoryType;
								GameObjectStack* stack = player->GetGameObjectStackFromInventory(itemContainer->stackID, inventoryType);
								assert(inventoryType == InventoryType::WEARABLES);

								if (g_InputManager->IsMouseButtonPressed(MouseButton::LEFT) &&
									stack != nullptr && stack->count > 0)
								{
									g_UIManager->BeginItemDrag(itemContainer);
								}

								if (g_UIManager->draggedUIContainer != nullptr &&
									itemContainer != g_UIManager->draggedUIContainer)
								{
									if (g_InputManager->IsMouseButtonReleased(MouseButton::LEFT))
									{
										if (g_UIManager->MoveItemStack(g_UIManager->draggedUIContainer, itemContainer))
										{
											g_UIManager->EndItemDrag();
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	UIManager::UIManager()
	{
	}

	void UIManager::Initialize()
	{
		Deserialize();
	}

	void UIManager::Destroy()
	{
		delete playerInventoryUI;
		playerInventoryUI = nullptr;

		delete playerQuickAccessUI;
		playerQuickAccessUI = nullptr;

		delete wearablesInventoryUI;
		wearablesInventoryUI = nullptr;

	}

	void UIManager::Update()
	{
		if (g_CameraManager->CurrentCamera()->bIsGameplayCam)
		{
			PROFILE_AUTO("UIManager Update");

			Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);

			if (player != nullptr)
			{
				real aspectRatio = g_Window->GetAspectRatio();
				const real targetAspectRatio = 16.0f / 9.0f;
				real x = aspectRatio > targetAspectRatio ? (aspectRatio / targetAspectRatio) : 1.0f;
				real y = aspectRatio > targetAspectRatio ? 1.0f : (aspectRatio / targetAspectRatio);

				{
					Rect rect{ -x, -y, x, y, VEC4_ONE };
					playerQuickAccessUI->Update(rect);
					playerQuickAccessUI->Draw();
				}

				if (player->bInventoryShowing)
				{
					{
						Rect rect{ -x, -y, x, y, VEC4_ONE };
						playerInventoryUI->Update(rect);
						playerInventoryUI->Draw();
					}

					{
						Rect rect{ -x, -y, x, y, VEC4_ONE };
						wearablesInventoryUI->Update(rect);
						wearablesInventoryUI->Draw();
					}
				}

				if (draggedUIContainer != nullptr)
				{
					UIMesh* uiMesh = g_Renderer->GetUIMesh();

					glm::vec2 mousePos = g_InputManager->GetMousePosition();
					glm::vec2i windowSize = g_Window->GetSize();
					glm::vec2 mousePosN(mousePos.x / windowSize.x * 2.0f - 1.0f, -(mousePos.y / windowSize.y * 2.0f - 1.0f));

					Rect rect = draggedUIContainer->lastCutRect;
					real width = rect.maxX - rect.minX;
					real height = rect.maxY - rect.minY;

					rect.minX = mousePosN.x - width * 0.5f;
					rect.minY = mousePosN.y - height * 0.5f;
					rect.maxX = mousePosN.x + width * 0.5f;
					rect.maxY = mousePosN.y + height * 0.5f;

					rect.Scale(0.75f);

					rect.colour = UIContainer::baseColour;
					rect.colour.a = 0.5f;

					draggedUIContainer->lastCutRect = rect;
					draggedUIContainer->Update(rect, true);

					uiMesh->DrawRect(
						glm::vec2(rect.minX, rect.minY),
						glm::vec2(rect.maxX, rect.maxY), rect.colour, 0.0f);
				}

				if (draggedUIContainer != nullptr)
				{
					glm::vec2 mousePos = g_InputManager->GetMousePosition();
					if (!g_InputManager->IsMouseButtonDown(MouseButton::LEFT) ||
						g_InputManager->GetKeyDown(KeyCode::KEY_ESCAPE) ||
						mousePos.x == -1.0f || mousePos.y == -1.0f)
					{
						draggedUIContainer = nullptr;
					}
				}
			}
		}
	}

	void UIManager::DrawImGui(bool* bUIEditorShowing)
	{
		if (*bUIEditorShowing)
		{
			if (ImGui::Begin("UI", bUIEditorShowing))
			{
				if (ImGui::Button("Save"))
				{
					Serialize();
				}

				g_ResourceManager->DrawAudioSourceIDImGui("Item pickup", m_ItemPickupSoundSID);
				g_ResourceManager->DrawAudioSourceIDImGui("Item drop", m_ItemDropSoundSID);

				auto drawButtons = [this](UIContainer** uiContainer, const char* filePath)
				{
					ImGui::PushID(filePath);

					if (ImGui::Button((*uiContainer)->bDirty ? "Save*" : "Save"))
					{
						if (SerializeUIConfig(filePath, *uiContainer))
						{
							(*uiContainer)->ClearDirty();
						}
					}

					ImGui::SameLine();

					{
						ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColour);
						ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColour);
						ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColour);

						if (ImGui::Button("Reload"))
						{
							UIContainer* newUIContainer = ParseUIConfig(filePath);
							if (newUIContainer != nullptr)
							{
								*uiContainer = newUIContainer;
							}
							else
							{
								// There was no file to read, just clear data
								delete (*uiContainer);
								*uiContainer = new UIContainer();
							}

							(*uiContainer)->bDirty = false;
						}

						ImGui::PopStyleColor();
						ImGui::PopStyleColor();
						ImGui::PopStyleColor();
					}

					ImGui::PopID();
				};

				drawButtons((UIContainer**)&playerInventoryUI, UI_PLAYER_INVENTORY_LOCATION);
				playerInventoryUI->DrawImGui();

				drawButtons((UIContainer**)&playerQuickAccessUI, UI_PLAYER_QUICK_ACCESS_LOCATION);
				playerQuickAccessUI->DrawImGui();

				drawButtons((UIContainer**)&wearablesInventoryUI, UI_WEARABLES_INVENTORY_LOCATION);
				wearablesInventoryUI->DrawImGui();
			}

			ImGui::End();
		}
	}

	UIContainer* UIManager::ParseUIConfig(const char* filePath)
	{
		std::string fileContents;
		if (ReadFile(filePath, fileContents, false))
		{
			JSONObject rootObject;
			if (!JSONParser::Parse(fileContents, rootObject))
			{
				PrintError("Failed to parse UI config file at %s\n", filePath);
				return nullptr;
			}

			UIContainer* uiContainer = UIContainer::DeserializeCommon(rootObject);

			if (g_SceneManager->HasSceneLoaded())
			{
				uiContainer->Initialize();
			}

			return uiContainer;
		}
		else
		{
			PrintError("Failed to read UI config file at %s\n", filePath);
		}

		return nullptr;
	}

	bool UIManager::SerializeUISettings()
	{
		JSONObject rootObject = {};

		rootObject.fields.emplace_back("item pickup sound sid", JSONValue(m_ItemPickupSoundSID));
		rootObject.fields.emplace_back("item drop sound sid", JSONValue(m_ItemDropSoundSID));

		std::string directoryString = RelativePathToAbsolute(ExtractDirectoryString(UI_SETTINGS_LOCATION));
		if (!Platform::DirectoryExists(directoryString))
		{
			Platform::CreateDirectoryRecursive(directoryString);
		}

		std::string fileContents = rootObject.ToString();
		bool bSuccess = WriteFile(UI_SETTINGS_LOCATION, fileContents, false);

		return bSuccess;
	}

	bool UIManager::ParseUISettings()
	{
		std::string fileContents;
		if (ReadFile(UI_SETTINGS_LOCATION, fileContents, false))
		{
			JSONObject rootObject;
			if (!JSONParser::Parse(fileContents, rootObject))
			{
				PrintError("Failed to parse UI settings file at %s\n", UI_SETTINGS_LOCATION);
				return false;
			}

			rootObject.TryGetULong("item pickup sound sid", m_ItemPickupSoundSID);
			rootObject.TryGetULong("item drop sound sid", m_ItemDropSoundSID);
			return true;
		}

		return false;
	}

	void UIManager::Deserialize()
	{
		ParseUISettings();

		if (playerInventoryUI != nullptr)
		{
			delete playerInventoryUI;
		}

		playerInventoryUI = (InventoryUIContainer*)ParseUIConfig(UI_PLAYER_INVENTORY_LOCATION);
		if (playerInventoryUI != nullptr)
		{
			if (playerInventoryUI->type != UIContainerType::INVENTORY)
			{
				PrintWarn("Ignoring player inventory UI from file, type did not match expectation of UIContainerType::INVENTORY\n");
				delete playerInventoryUI;
				playerInventoryUI = new InventoryUIContainer();
			}
		}
		else
		{
			PrintError("Failed to read player inventory UI config to %s\n", UI_PLAYER_INVENTORY_LOCATION);
			playerInventoryUI = new InventoryUIContainer();
		}

		if (playerQuickAccessUI != nullptr)
		{
			delete playerQuickAccessUI;
		}

		playerQuickAccessUI = (QuickAccessItemUIContainer*)ParseUIConfig(UI_PLAYER_QUICK_ACCESS_LOCATION);
		if (playerQuickAccessUI != nullptr)
		{
			if (playerQuickAccessUI->type != UIContainerType::QUICK_ACCESS)
			{
				PrintWarn("Ignoring player quick access UI from file, type did not match expectation of UIContainerType::QUICK_ACCESS\n");
				delete playerQuickAccessUI;
				playerQuickAccessUI = new QuickAccessItemUIContainer();
			}
		}
		else
		{
			PrintError("Failed to read player quick access UI config to %s\n", UI_PLAYER_QUICK_ACCESS_LOCATION);
			playerQuickAccessUI = new QuickAccessItemUIContainer();
		}

		if (wearablesInventoryUI != nullptr)
		{
			delete wearablesInventoryUI;
		}

		wearablesInventoryUI = (WearablesItemUIContainer*)ParseUIConfig(UI_WEARABLES_INVENTORY_LOCATION);
		if (wearablesInventoryUI != nullptr)
		{
			if (wearablesInventoryUI->type != UIContainerType::WEARABLES)
			{
				PrintWarn("Ignoring player inventory UI from file, type did not match expectation of UIContainerType::WEARABLES\n");
				delete wearablesInventoryUI;
				wearablesInventoryUI = new WearablesItemUIContainer();
			}
		}
		else
		{
			PrintError("Failed to read wearables UI config to %s\n", UI_WEARABLES_INVENTORY_LOCATION);
			wearablesInventoryUI = new WearablesItemUIContainer();
		}
	}

	bool UIManager::SerializeUIConfig(const char* filePath, UIContainer* uiContainer)
	{
		JSONObject rootObject = {};
		uiContainer->SerializeCommon(rootObject);

		std::string directoryString = RelativePathToAbsolute(ExtractDirectoryString(filePath));
		if (!Platform::DirectoryExists(directoryString))
		{
			Platform::CreateDirectoryRecursive(directoryString);
		}

		std::string fileContents = rootObject.ToString();
		bool bSuccess = WriteFile(filePath, fileContents, false);

		return bSuccess;
	}

	void UIManager::Serialize()
	{
		if (SerializeUIConfig(UI_PLAYER_INVENTORY_LOCATION, playerInventoryUI))
		{
			playerInventoryUI->bDirty = false;
		}
		else
		{
			PrintError("Failed to serialize player inventory UI config to %s\n", UI_PLAYER_INVENTORY_LOCATION);
		}

		if (SerializeUIConfig(UI_PLAYER_QUICK_ACCESS_LOCATION, playerQuickAccessUI))
		{
			playerQuickAccessUI->bDirty = false;
		}
		else
		{
			PrintError("Failed to serialize player quick access UI config to %s\n", UI_PLAYER_QUICK_ACCESS_LOCATION);
		}

		if (SerializeUIConfig(UI_WEARABLES_INVENTORY_LOCATION, wearablesInventoryUI))
		{
			wearablesInventoryUI->bDirty = false;
		}
		else
		{
			PrintError("Failed to serialize wearables UI config to %s\n", UI_WEARABLES_INVENTORY_LOCATION);
		}

		SerializeUISettings();
	}

	bool UIManager::MoveItemStack(ItemUIContainer* from, ItemUIContainer* to)
	{
		if (from != to)
		{
			Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
			if (player != nullptr)
			{
				return player->MoveItemStack(from->stackID, to->stackID);
			}
		}

		return false;
	}

	bool UIManager::DropItemStack(ItemUIContainer* stack, bool bDestroyItem)
	{
		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		if (player != nullptr)
		{
			return player->DropItemStack(stack->stackID, bDestroyItem);
		}

		return false;
	}

	void UIManager::BeginItemDrag(ItemUIContainer* draggedItem)
	{
		if (draggedUIContainer != nullptr)
		{
			PrintWarn("BeginItemDrag was called when draggedUIContainer was non-null!\n");
			return;
		}

		draggedUIContainer = draggedItem;

		if (m_ItemPickupSoundSID != InvalidStringID)
		{
			AudioSourceID sourceID = g_ResourceManager->GetOrLoadAudioID(m_ItemPickupSoundSID);
			AudioManager::SetSourceGain(sourceID, m_ItemSoundGain);
			AudioManager::PlaySource(sourceID);
		}
	}

	void UIManager::EndItemDrag()
	{
		if (draggedUIContainer == nullptr)
		{
			PrintWarn("EndItemDrag was called when draggedUIContainer was null!\n");
			return;
		}

		draggedUIContainer = nullptr;

		if (m_ItemDropSoundSID != InvalidStringID)
		{
			AudioSourceID sourceID = g_ResourceManager->GetOrLoadAudioID(m_ItemDropSoundSID);
			AudioManager::SetSourceGain(sourceID, m_ItemSoundGain);
			AudioManager::PlaySource(sourceID);
		}
	}

	void UIManager::EnqueueImageSprite(TextureID textureID, Rect lastCutRect)
	{
		real aspectRatio = g_Window->GetAspectRatio();

		SpriteQuadDrawInfo spriteInfo = {};
		spriteInfo.textureID = textureID;
		spriteInfo.materialID = g_Renderer->m_SpriteMatSSID;
		real width = (lastCutRect.maxX - lastCutRect.minX);
		real height = (lastCutRect.maxY - lastCutRect.minY);
		spriteInfo.pos = glm::vec3(
			(lastCutRect.minX + width * 0.5f) * aspectRatio,
			lastCutRect.minY + height * 0.5f,
			1.0f);
		spriteInfo.scale = glm::vec3(width * 0.4f, -width * 0.4f, 1.0f);
		spriteInfo.anchor = AnchorPoint::CENTER;
		spriteInfo.bScreenSpace = true;
		spriteInfo.bReadDepth = false;
		spriteInfo.bRaw = true;
		spriteInfo.zOrder = 75;
		g_Renderer->EnqueueSprite(spriteInfo);
	}

	RectCutType RectCutTypeFromString(const char* cutTypeStr)
	{
		for (u32 i = 0; i < (u32)ARRAY_LENGTH(RectCutTypeStrings); ++i)
		{
			if (strcmp(cutTypeStr, RectCutTypeStrings[i]) == 0)
			{
				return (RectCutType)i;
			}
		}

		return RectCutType::_NONE;
	}

	UIContainerType UIContainerTypeFromString(const char* str)
	{
		for (u32 i = 0; i < (u32)UIContainerType::_NONE; ++i)
		{
			if (strcmp(UIContainerTypeStrings[i], str) == 0)
			{
				return (UIContainerType)i;
			}
		}

		return UIContainerType::_NONE;
	}

	Rect CutLeft(Rect* rect, real amount, bool bRelative, const glm::vec4& colour)
	{
		real minX = rect->minX;
		rect->minX = glm::min(rect->maxX, rect->minX + amount * (bRelative ? (rect->maxX - rect->minX) : 2.0f));
		return Rect{ minX, rect->minY, rect->minX, rect->maxY, colour };
	}

	Rect CutRight(Rect* rect, real amount, bool bRelative, const glm::vec4& colour)
	{
		real maxX = rect->maxX;
		rect->maxX = glm::max(rect->minX, rect->maxX - amount * (bRelative ? (rect->maxX - rect->minX) : 2.0f));
		return Rect{ rect->maxX, rect->minY, maxX, rect->maxY, colour };
	}

	Rect CutTop(Rect* rect, real amount, bool bRelative, const glm::vec4& colour)
	{
		real maxY = rect->maxY;
		rect->maxY = glm::max(rect->minY, rect->maxY - amount * (bRelative ? (rect->maxY - rect->minY) : 2.0f));
		return Rect{ rect->minX, rect->maxY, rect->maxX, maxY, colour };
	}

	Rect CutBottom(Rect* rect, real amount, bool bRelative, const glm::vec4& colour)
	{
		real minY = rect->minY;
		rect->minY = glm::min(rect->maxY, rect->minY + amount * (bRelative ? (rect->maxY - rect->minY) : 2.0f));
		return Rect{ rect->minX, minY, rect->maxX, rect->minY, colour };
	}
} // namespace flex
