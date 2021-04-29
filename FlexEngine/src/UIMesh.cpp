#include "stdafx.hpp"

#include "UIMesh.hpp"

#include "Graphics/Renderer.hpp"
#include "Profiler.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "JSONParser.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

namespace flex
{
	UIContainer* UIContainer::Duplicate()
	{
		UIContainer* newUIContainer;

		switch (type)
		{
		case UIContainerType::BASE:
		{
			newUIContainer = new UIContainer(cutType, amount, bVisible, bRelative, tag, children);
		} break;
		case UIContainerType::ITEM:
		{
			newUIContainer = new ItemUIContainer(cutType, amount, bVisible, bRelative, tag, children, ((ItemUIContainer*)this)->stack);
		} break;
		default:
		{
			ENSURE_NO_ENTRY();
			return nullptr;
		} break;
		}

		newUIContainer->tagSourceStr = tagSourceStr;

		if (uiElement != nullptr)
		{
			switch (uiElement->type)
			{
			case UIElementType::IMAGE:
			{
				ImageUIElement* imageElement = (ImageUIElement*)uiElement;
				newUIContainer->uiElement = new ImageUIElement(imageElement->textureID);
			} break;
			case UIElementType::TEXT:
			{
				TextUIElement* textElement = (TextUIElement*)uiElement;
				newUIContainer->uiElement = new TextUIElement(textElement->text, textElement->fontID);
			} break;
			default:
			{
				PrintError("Unhandled UI element type in UIContainer::Duplicate\n");
			}
			}
		}

		for (UIContainer* child : children)
		{
			newUIContainer->children.emplace_back(child->Duplicate());
		}

		return newUIContainer;
	}

	void UIContainer::Update()
	{
	}

	RectCutResult UIContainer::DrawImGui(bool& bDirtyFlag, const char* optionalTreeNodeName /* = nullptr */)
	{
		bool bRemoveSelf = false;
		bool bDuplicateSelf = false;
		bool bChangeSelfType = false;

		bool bTreeOpen = ImGui::TreeNode(optionalTreeNodeName != nullptr ? optionalTreeNodeName : "UIContainer");

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
						bDirtyFlag = true;
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
						bDirtyFlag = true;
					}

				}
				ImGui::EndCombo();
			}

			bDirtyFlag = ImGui::SliderFloat("##amount", &amount, 0.0f, 1.0f) || bDirtyFlag;

			bDirtyFlag = ImGui::Checkbox("Visible", &bVisible) || bDirtyFlag;

			ImGui::SameLine();

			bDirtyFlag = ImGui::Checkbox("Relative", &bRelative) || bDirtyFlag;

			ImGui::SameLine();

			if (ImGui::Button("-"))
			{
				bRemoveSelf = true;
			}

			ImGui::SameLine();

			if (ImGui::Button("+"))
			{
				children.emplace_back(new UIContainer(RectCutType::TOP, 0.5f, false, true, InvalidStringID, {}));
				bDirtyFlag = true;
			}

			tagSourceStr.resize(256);
			if (ImGui::InputText("Tag", (char*)tagSourceStr.c_str(), 256, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				tagSourceStr = std::string(tagSourceStr.c_str());
				tag = Hash(tagSourceStr.c_str());
				bDirtyFlag = true;
			}

			for (i32 i = 0; i < (i32)children.size(); ++i)
			{
				ImGui::PushID(i);
				RectCutResult result = children[i]->DrawImGui(bDirtyFlag);
				ImGui::PopID();

				if (result == RectCutResult::REMOVE)
				{
					children.erase(children.begin() + i);
					bDirtyFlag = true;
					--i;
				}
				else if (result == RectCutResult::DUPLICATE)
				{
					children.emplace(children.begin() + i, children[i]->Duplicate());
					bDirtyFlag = true;
				}
				else if (result == RectCutResult::CHANGE_SELF_TYPE)
				{
					UIContainer* oldUIContainer = children[i];

					// type will have already been changed to new value
					UIContainer* newUIContainer = oldUIContainer->Duplicate();

					children[i] = newUIContainer;
					delete oldUIContainer;
				}
			}
			ImGui::TreePop();
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

	Rect UIContainer::Cut(Rect* rect, const glm::vec4& normalColour, const glm::vec4& highlightedColour)
	{
		glm::vec4 colour = GetColour(normalColour, highlightedColour);

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

	glm::vec4 UIContainer::GetColour(const glm::vec4& normalColour, const glm::vec4& highlightedColour) const
	{
		glm::vec4 colour = bHighlighted ? highlightedColour : normalColour;
		if (!bVisible)
		{
			colour.a = bHighlighted ? 0.2f : 0.0f;
		}
		return colour;
	}

	JSONObject UIContainer::Serialize() const
	{
		JSONObject result = {};

		result.fields.emplace_back("type", JSONValue(UIContainerTypeStrings[(i32)type]));
		result.fields.emplace_back("cut type", JSONValue(RectCutTypeStrings[(i32)cutType]));
		result.fields.emplace_back("amount", JSONValue(amount));
		result.fields.emplace_back("visible", JSONValue(bVisible));
		result.fields.emplace_back("relative", JSONValue(bRelative));
		// TODO: Skip serializing in release builds
		std::string tagSourceStrStripped = std::string(tagSourceStr.c_str());
		if (!tagSourceStrStripped.empty())
		{
			result.fields.emplace_back("tag source string", JSONValue(tagSourceStrStripped));
		}

		if (tag != InvalidStringID)
		{
			result.fields.emplace_back("tag", JSONValue((u64)tag));
		}

		if (!children.empty())
		{
			std::vector<JSONObject> childrenObjects;
			childrenObjects.reserve(children.size());
			for (UIContainer* child : children)
			{
				childrenObjects.emplace_back(child->Serialize());
			}
			result.fields.emplace_back("children", JSONValue(childrenObjects));
		}

		return result;
	}

	UIContainer* UIContainer::Deserialize(const JSONObject& object)
	{
		UIContainer* uiContainer;

		UIContainerType containerType = UIContainerType::BASE;

		std::string containerTypeStr;
		if (object.TryGetString("type", containerTypeStr))
		{
			containerType = UIContainerTypeFromString(containerTypeStr.c_str());
		}

		if (containerType == UIContainerType::BASE)
		{
			uiContainer = new UIContainer(containerType);
		}
		else if (containerType == UIContainerType::ITEM)
		{
			uiContainer = new ItemUIContainer();
		}
		else
		{
			ENSURE_NO_ENTRY();
			return nullptr;
		}

		std::string cutTypeStr;
		if (object.TryGetString("cut type", cutTypeStr))
		{
			uiContainer->cutType = RectCutTypeFromString(cutTypeStr.c_str());
		}
		object.TryGetFloat("amount", uiContainer->amount);
		object.TryGetBool("visible", uiContainer->bVisible);
		object.TryGetBool("relative", uiContainer->bRelative);
		object.TryGetString("tag source string", uiContainer->tagSourceStr);
		object.TryGetStringID("tag", uiContainer->tag);

		std::vector<JSONObject> childrenObjects;
		if (object.TryGetObjectArray("children", childrenObjects))
		{
			uiContainer->children.resize(childrenObjects.size());
			for (u32 i = 0; i < (u32)childrenObjects.size(); ++i)
			{
				uiContainer->children[i] = UIContainer::Deserialize(childrenObjects[i]);
			}
		}

		return uiContainer;
	}

	void ItemUIContainer::Update()
	{
		UIContainer::Update();

		if (stack != nullptr && stack->count > 0)
		{
			if (uiElement == nullptr)
			{
				uiElement = new ImageUIElement(g_Renderer->alphaBGTextureID);
			}
		}
		else
		{
			if (uiElement != nullptr)
			{
				delete uiElement;
				uiElement = nullptr;
			}
		}
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
		rect->minX = glm::min(rect->maxX, rect->minX + amount * (bRelative ? ((rect->maxX - rect->minX) ) : 2.0f));
		return Rect{ minX, rect->minY, rect->minX, rect->maxY, colour };
	}

	Rect CutRight(Rect* rect, real amount, bool bRelative, const glm::vec4& colour)
	{
		real maxX = rect->maxX;
		rect->maxX = glm::max(rect->minX, rect->maxX - amount * (bRelative ? ((rect->maxX - rect->minX) ) : 2.0f));
		return Rect{ rect->maxX, rect->minY, maxX, rect->maxY, colour };
	}

	Rect CutTop(Rect* rect, real amount, bool bRelative, const glm::vec4& colour)
	{
		real maxY = rect->maxY;
		rect->maxY = glm::max(rect->minY, rect->maxY - amount * (bRelative ? ((rect->maxY - rect->minY) ) : 2.0f));
		return Rect{ rect->minX, rect->maxY, rect->maxX, maxY, colour };
	}

	Rect CutBottom(Rect* rect, real amount, bool bRelative, const glm::vec4& colour)
	{
		real minY = rect->minY;
		rect->minY = glm::min(rect->maxY, rect->minY + amount * (bRelative ? ((rect->maxY - rect->minY) ) : 2.0f));
		return Rect{ rect->minX, minY, rect->maxX, rect->minY, colour };
	}

	UIMesh::UIMesh()
	{
	}

	UIMesh::~UIMesh()
	{
	}

	void UIMesh::Initialize()
	{
		const std::string debugMatName = "UI Mesh";
		if (!g_Renderer->FindOrCreateMaterialByName(debugMatName, m_MaterialID))
		{
			MaterialCreateInfo debugMatCreateInfo = {};
			debugMatCreateInfo.shaderName = "ui";
			debugMatCreateInfo.name = debugMatName;
			debugMatCreateInfo.persistent = true;
			debugMatCreateInfo.visibleInEditor = true;
			debugMatCreateInfo.bDynamic = true;
			debugMatCreateInfo.bSerializable = false;
			m_MaterialID = g_Renderer->InitializeMaterial(&debugMatCreateInfo);
		}

		CreateDebugObject();
	}

	void UIMesh::Destroy()
	{
		Clear();
	}

	void UIMesh::OnPostSceneChange()
	{
		Clear();
		CreateDebugObject();
	}

	void UIMesh::DrawRect(const glm::vec2& bottomLeft, const glm::vec2& topRight, const glm::vec4& colour, real cornerRadius)
	{
		FLEX_UNUSED(cornerRadius);

		if (bottomLeft.x >= topRight.x || bottomLeft.y >= topRight.y)
		{
			PrintWarn("Invalid rect parameters, (bottom left, top right = (%.2f, %.2f), (%.2f,%.2f)\n",
				bottomLeft.x, bottomLeft.y, topRight.x, topRight.y);
			return;
		}

		glm::vec2i windowSize = g_Window->GetSize();
		real invAspectRatio = windowSize.y / (real)windowSize.x;

		real widthN = (topRight.x - bottomLeft.x) * invAspectRatio;
		real heightN = (topRight.y - bottomLeft.y);
		real width = (widthN * windowSize.x);
		real height = (heightN * windowSize.y);

		u32 vertCount = 4;
		u32 triCount = 2;
		u32 indexCount = triCount * 3;

		std::vector<glm::vec2> points;
		std::vector<glm::vec2> texCoords;
		std::vector<u32> indices;

		points.reserve(vertCount);
		texCoords.reserve(vertCount);
		indices.reserve(indexCount);

		{
			points.emplace_back(glm::vec2(bottomLeft.x, bottomLeft.y));
			points.emplace_back(glm::vec2(topRight.x, topRight.y));
			points.emplace_back(glm::vec2(bottomLeft.x, topRight.y));
			points.emplace_back(glm::vec2(topRight.x, bottomLeft.y));

			texCoords.emplace_back(glm::vec2(0.0f, 0.0f));
			texCoords.emplace_back(glm::vec2(1.0f, 1.0f));
			texCoords.emplace_back(glm::vec2(0.0f, 1.0f));
			texCoords.emplace_back(glm::vec2(1.0f, 0.0f));
		}

		{
			indices.emplace_back(0);
			indices.emplace_back(2);
			indices.emplace_back(1);

			indices.emplace_back(0);
			indices.emplace_back(1);
			indices.emplace_back(3);
		}

		assert((u32)points.size() == vertCount);
		assert((u32)indices.size() == indexCount);

		glm::vec2 uvBlendAmount(0.5f / width, 0.5f / height);

		DrawPolygon(points, texCoords, indices, colour, uvBlendAmount);
	}

	void UIMesh::DrawArc(const glm::vec2& centerPos, real startAngle, real endAngle, real innerRadius, real thickness, i32 segmentsInFullCircle, const glm::vec4& colour)
	{
		glm::vec2i windowSize = g_Window->GetSize();
		real invAspectRatio = windowSize.y / (real)windowSize.x;

		real totalAngle = glm::abs(endAngle - startAngle);
		real segmentsPerRadian = segmentsInFullCircle / TWO_PI;
		real radiansPerSegment = copysign(1.0f, totalAngle) * TWO_PI / segmentsInFullCircle;
		u32 quadCount = glm::min((u32)glm::ceil(totalAngle * segmentsPerRadian), 8192u);
		u32 vertCount = quadCount * 2 + 2;
		u32 triCount = quadCount * 2;
		u32 indexCount = triCount * 3;

		real outerRadius = innerRadius + thickness;

		std::vector<glm::vec2> points;
		std::vector<glm::vec2> texCoords;
		std::vector<u32> indices;

		points.reserve(vertCount);
		texCoords.reserve(vertCount);
		indices.reserve(indexCount);

		for (u32 i = 0; i <= quadCount; ++i)
		{
			real angle = startAngle + radiansPerSegment * i;

			// Interpolate final segment for smoother animations
			if (i == quadCount)
			{
				angle = endAngle;
			}

			glm::vec2 radialPos(glm::cos(angle) * invAspectRatio, glm::sin(angle));
			points.emplace_back(centerPos + innerRadius * radialPos);
			points.emplace_back(centerPos + outerRadius * radialPos);

			real alpha = (i / (real)quadCount);
			texCoords.emplace_back(glm::vec2(0.0f, alpha));
			texCoords.emplace_back(glm::vec2(1.0f, alpha));
		}

		for (u32 i = 0; i < quadCount; ++i)
		{
			u32 indexStart = i * 2;
			if (radiansPerSegment < 0.0f)
			{
				indices.emplace_back(indexStart + 0);
				indices.emplace_back(indexStart + 1);
				indices.emplace_back(indexStart + 3);

				indices.emplace_back(indexStart + 0);
				indices.emplace_back(indexStart + 3);
				indices.emplace_back(indexStart + 2);
			}
			else
			{
				indices.emplace_back(indexStart + 0);
				indices.emplace_back(indexStart + 3);
				indices.emplace_back(indexStart + 1);

				indices.emplace_back(indexStart + 0);
				indices.emplace_back(indexStart + 2);
				indices.emplace_back(indexStart + 3);
			}
		}

		assert((u32)points.size() == vertCount);
		assert((u32)indices.size() == indexCount);

		real width = thickness * windowSize.x; // strip width
		real height = totalAngle * (innerRadius + thickness * 0.5f) * windowSize.y; // circumference
		glm::vec2 uvBlendAmount(4.0f / width, 4.0f / height);

		// When drawing a full circle, don't blend ends to ensure we get a clean continuous loop
		if (glm::abs(std::fmod(endAngle - startAngle, TWO_PI)) < 0.001f)
		{
			// Non-zero small value to prevent divide by zero
			uvBlendAmount.y = 0.000001f;
		}

		DrawPolygon(points, texCoords, indices, colour, uvBlendAmount);
	}

	void UIMesh::DrawPolygon(const std::vector<glm::vec2>& points,
		const std::vector<glm::vec2>& texCoords,
		const std::vector<u32>& indices,
		const glm::vec4& colour,
		const glm::vec2& uvBlendAmount)
	{
		Mesh* mesh = m_Object->GetMesh();

		u32 vertCount = (u32)points.size();

		bool bCreateNewDrawData = true;
		i32 submeshIndex = -1;
		DrawData* drawData = nullptr;

		for (u32 i = 0; i < (u32)m_DrawData.size(); ++i)
		{
			if (!m_DrawData[i]->bInUse)
			{
				submeshIndex = (i32)i;
				drawData = m_DrawData[i];
				bCreateNewDrawData = false;
				break;
			}
		}

		if (bCreateNewDrawData)
		{
			drawData = new DrawData();
			submeshIndex = (i32)m_DrawData.size();
		}

		drawData->bInUse = true;
		drawData->indexBuffer = indices;
		drawData->vertexBufferCreateInfo.positions_2D = points;
		drawData->vertexBufferCreateInfo.texCoords_UV = texCoords;
		drawData->vertexBufferCreateInfo.colours_R32G32B32A32.resize(vertCount);

		for (u32 i = 0; i < vertCount; ++i)
		{
			drawData->vertexBufferCreateInfo.colours_R32G32B32A32[i] = colour;
		}

		if (bCreateNewDrawData)
		{
			RenderObjectCreateInfo createInfo = {};
			createInfo.materialID = m_MaterialID;
			createInfo.bEditorObject = true;
			createInfo.bIndexed = true;
			createInfo.bAllowDynamicBufferShrinking = false;
			createInfo.indices = &drawData->indexBuffer;

			Material* mat = g_Renderer->GetMaterial(m_MaterialID);
			const VertexAttributes vertexAttributes = g_Renderer->GetShader(mat->shaderID)->vertexAttributes;
			MeshComponent* newSubMesh = new MeshComponent(mesh, m_MaterialID, true);
			if (!newSubMesh->CreateProcedural(vertCount, vertexAttributes, TopologyMode::TRIANGLE_LIST, &createInfo))
			{
				PrintWarn("UIMesh failed to create new submesh\n");
			}
			else
			{
				i32 submeshIndex1 = mesh->AddSubMesh(newSubMesh);
				assert(submeshIndex1 == submeshIndex);
				m_DrawData.emplace_back(drawData);
			}
		}
		else
		{
			mesh->GetSubMesh(submeshIndex)->UpdateDynamicVertexData(drawData->vertexBufferCreateInfo, indices);
			m_DrawData[submeshIndex]->uvBlendAmount = uvBlendAmount;
		}
	}

	void UIMesh::ComputeRects(UIContainer* parent, Rect& rootRect, std::vector<Rect>& outputRects, const glm::vec4& normalColour, const glm::vec4& highlightedColour)
	{
		outputRects.push_back(parent->Cut(&rootRect, normalColour, highlightedColour));
		i32 newRectIndex = (i32)outputRects.size() - 1;

		if (parent->bHighlighted && parent->bVisible)
		{
			// Grow when highlighted
			real growFactor = 1.05f;
			outputRects[newRectIndex].Scale(growFactor);
		}

		parent->Update();

		if (parent->uiElement != nullptr)
		{
			switch (parent->uiElement->type)
			{
			case UIElementType::IMAGE:
			{
				ImageUIElement* imageElement = (ImageUIElement*)parent->uiElement;

				real aspectRatio = g_Window->GetAspectRatio();

				SpriteQuadDrawInfo spriteInfo = {};
				spriteInfo.textureID = imageElement->textureID;
				spriteInfo.materialID = g_Renderer->m_SpriteMatSSID;
				real width = (outputRects[newRectIndex].maxX - outputRects[newRectIndex].minX);
				real height = (outputRects[newRectIndex].maxY - outputRects[newRectIndex].minY);
				spriteInfo.pos = glm::vec3(
					(outputRects[newRectIndex].minX  + width * 0.5f) * aspectRatio,
					outputRects[newRectIndex].minY  + height * 0.5f,
					1.0f);
				spriteInfo.scale = glm::vec3(width * 0.4f, width * 0.4f, 1.0f);
				spriteInfo.anchor = AnchorPoint::CENTER;
				spriteInfo.bScreenSpace = true;
				spriteInfo.bReadDepth = false;
				spriteInfo.bRaw = true;
				spriteInfo.zOrder = 75;
				g_Renderer->EnqueueSprite(spriteInfo);
			} break;
			case UIElementType::TEXT:
			{
				TextUIElement* textElement = (TextUIElement*)parent->uiElement;

				real aspectRatio = g_Window->GetAspectRatio();

				real width = (outputRects[newRectIndex].maxX - outputRects[newRectIndex].minX);
				real height = (outputRects[newRectIndex].maxY - outputRects[newRectIndex].minY);
				glm::vec2 pos = glm::vec2(
					(outputRects[newRectIndex].minX  + width * 0.5f) * aspectRatio,
					outputRects[newRectIndex].minY  + height * 0.5f);
				real spacing = 0.1f;
				real scale = 0.4f;

				g_Renderer->SetFont(textElement->fontID);
				g_Renderer->DrawStringSS(textElement->text, VEC4_ONE, AnchorPoint::CENTER, pos, spacing, scale);
			} break;
			}
		}

		for (i32 i = 0; i < (i32)parent->children.size(); ++i)
		{
			// newRect can't be cached by ref because vec resize is likely to occur
			ComputeRects(parent->children[i], outputRects[newRectIndex], outputRects, normalColour, highlightedColour);
		}
	}

	void UIMesh::Draw()
	{
		PROFILE_AUTO("UIMesh > Draw");

		Mesh* mesh = m_Object->GetMesh();

		for (u32 i = 0; i < (u32)m_DrawData.size(); ++i)
		{
			DrawData const* drawData = m_DrawData[i];
			if (drawData->bInUse)
			{
				mesh->GetSubMesh(i)->UpdateDynamicVertexData(drawData->vertexBufferCreateInfo, drawData->indexBuffer);
			}
		}
	}

	void UIMesh::EndFrame()
	{
		for (u32 i = 0; i < (u32)m_DrawData.size(); ++i)
		{
			DrawData* drawData = m_DrawData[i];

			if (drawData->bInUse)
			{
				drawData->bInUse = false;
			}
		}
	}

	Mesh* UIMesh::GetMesh()
	{
		return m_Object->GetMesh();
	}

	bool UIMesh::IsSubmeshActive(i32 submeshIndex)
	{
		return m_DrawData[submeshIndex]->bInUse;
	}

	glm::vec2 UIMesh::GetUVBlendAmount(i32 drawIndex)
	{
		return m_DrawData[drawIndex]->uvBlendAmount;
	}

	void UIMesh::CreateDebugObject()
	{
		if (m_Object != nullptr)
		{
			// Object will have been destroyed by scene?
			m_Object = nullptr;
		}

		m_Object = new GameObject("UI Mesh", SID("object"));
		m_Object->SetSerializable(false);
		m_Object->SetVisibleInSceneExplorer(false);
		m_Object->SetCastsShadow(false);
		m_Object->SetMesh(new Mesh(m_Object));
	}

	void UIMesh::Clear()
	{
		for (u32 i = 0; i < (u32)m_DrawData.size(); ++i)
		{
			delete m_DrawData[i];
		}
		m_DrawData.clear();
		m_Object->Destroy(false);
		delete m_Object;
		m_Object = nullptr;
	}
} // namespace flex
