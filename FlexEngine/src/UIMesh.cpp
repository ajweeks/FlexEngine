#include "stdafx.hpp"

#include "UIMesh.hpp"

#include "Graphics/Renderer.hpp"
#include "Profiler.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "JSONParser.hpp"
#include "Player.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

namespace flex
{
	Rect UIScreen::Cut(Rect* rect, const glm::vec4& normalColour, const glm::vec4& highlightedColour)
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

	glm::vec4 UIScreen::GetColour(const glm::vec4& normalColour, const glm::vec4& highlightedColour) const
	{
		glm::vec4 colour = bHighlighted ? highlightedColour : normalColour;
		if (!bVisible)
		{
			colour.a = bHighlighted ? 0.2f : 0.0f;
		}
		return colour;
	}

	JSONObject UIScreen::Serialize() const
	{
		JSONObject result = {};

		result.fields.emplace_back("cut type", JSONValue(RectCutTypeStrings[(i32)cutType]));
		result.fields.emplace_back("amount", JSONValue(amount));
		result.fields.emplace_back("visible", JSONValue(bVisible));
		result.fields.emplace_back("relative", JSONValue(bRelative));

		if (!children.empty())
		{
			std::vector<JSONObject> childrenObjects;
			childrenObjects.reserve(children.size());
			for (const UIScreen& child : children)
			{
				childrenObjects.emplace_back(child.Serialize());
			}
			result.fields.emplace_back("children", JSONValue(childrenObjects));
		}

		return result;
	}

	UIScreen UIScreen::Deserialize(const JSONObject& object)
	{
		UIScreen result = {};

		std::string cutTypeStr;
		if (object.TryGetString("cut type", cutTypeStr))
		{
			result.cutType = RectCutTypeFromString(cutTypeStr.c_str());
		}
		object.TryGetFloat("amount", result.amount);
		object.TryGetBool("visible", result.bVisible);
		object.TryGetBool("relative", result.bRelative);

		std::vector<JSONObject> childrenObjects;
		if (object.TryGetObjectArray("children", childrenObjects))
		{
			for (const JSONObject& childObj : childrenObjects)
			{
				result.children.emplace_back(UIScreen::Deserialize(childObj));
			}
		}

		return result;
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

	Rect CutLeft(Rect* rect, real amount, bool bRelative, const glm::vec4& colour)
	{
		real minX = rect->minX;
		rect->minX = glm::min(rect->maxX, rect->minX + amount * (bRelative ? (rect->maxX - rect->minX) : 1.0f));
		return Rect{ minX, rect->minY, rect->minX, rect->maxY, colour };
	}

	Rect CutRight(Rect* rect, real amount, bool bRelative, const glm::vec4& colour)
	{
		real maxX = rect->maxX;
		rect->maxX = glm::max(rect->minX, rect->maxX - amount * (bRelative ? (rect->maxX - rect->minX) : 1.0f));
		return Rect{ rect->maxX, rect->minY, maxX, rect->maxY, colour };
	}

	Rect CutTop(Rect* rect, real amount, bool bRelative, const glm::vec4& colour)
	{
		real maxY = rect->maxY;
		rect->maxY = glm::max(rect->minY, rect->maxY - amount * (bRelative ? (rect->maxY - rect->minY) : 1.0f));
		return Rect{ rect->minX, rect->maxY, rect->maxX, maxY, colour };
	}

	Rect CutBottom(Rect* rect, real amount, bool bRelative, const glm::vec4& colour)
	{
		real minY = rect->minY;
		rect->minY = glm::min(rect->maxY, rect->minY + amount * (bRelative ? (rect->maxY - rect->minY) : 1.0f));
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

		Deserialize();
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

	bool UIMesh::Deserialize()
	{
		std::string fileContents;
		if (ReadFile(UI_CONFIG_FILE_LOCATION, fileContents, false))
		{
			JSONObject rootObject;
			if (!JSONParser::Parse(fileContents, rootObject))
			{
				PrintError("Failed to parse UI config file at %s\n", UI_CONFIG_FILE_LOCATION);
				return false;
			}

			JSONObject playerScreenObject;
			if (rootObject.TryGetObject("player screen", playerScreenObject))
			{
				m_PlayerScreenUI = UIScreen::Deserialize(playerScreenObject);
			}

			JSONObject playerInventoryScreenObject;
			if (rootObject.TryGetObject("player inventory screen", playerInventoryScreenObject))
			{
				m_PlayerInventoryUI = UIScreen::Deserialize(playerInventoryScreenObject);
			}

			m_bUIConfigDirty = false;

			return true;
		}

		return false;
	}

	void UIMesh::Serialize()
	{
		JSONObject rootObject = {};

		rootObject.fields.emplace_back("player screen", JSONValue(m_PlayerScreenUI.Serialize()));
		rootObject.fields.emplace_back("player inventory screen", JSONValue(m_PlayerInventoryUI.Serialize()));

		std::string fileContents = rootObject.ToString();
		if (!WriteFile(UI_CONFIG_FILE_LOCATION, fileContents, false))
		{
			PrintError("Failed to write UI config file to %s\n", UI_CONFIG_FILE_LOCATION);
		}
		else
		{
			m_bUIConfigDirty = false;
		}
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

	void UIMesh::ComputeRects(UIScreen& parent, Rect& rootRect, std::vector<Rect>& outputRects, const glm::vec4& normalColour, const glm::vec4& highlightedColour)
	{
		outputRects.push_back(parent.Cut(&rootRect, normalColour, highlightedColour));
		i32 newRectIndex = (i32)outputRects.size() - 1;

		for (i32 i = 0; i < (i32)parent.children.size(); ++i)
		{
			// newRect can't be cached by ref because vec resize is likely to occur
			ComputeRects(parent.children[i], outputRects[newRectIndex], outputRects, normalColour, highlightedColour);
		}
	}

	void UIMesh::Draw()
	{
		PROFILE_AUTO("UIMesh > Draw");

		if (g_CameraManager->CurrentCamera()->bIsGameplayCam)
		{
			Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);

			if (player != nullptr)
			{
				// TODO: Preallocate room here for rect count
				std::vector<Rect> rects;

				static const glm::vec4 normalColour(0.9f, 0.9f, 0.9f, 1.0f);
				static const glm::vec4 highlightedColour(1.0f, 1.0f, 1.0f, 1.0f);
				static const glm::vec4 darkenedColour(0.3f, 0.3f, 0.3f, 0.5f);
				static const glm::vec4 darkenedHighlightedColour(0.4f, 0.4f, 0.4f, 0.5f);

				if (m_PlayerScreenUI.cutType != RectCutType::_NONE)
				{
					Rect rect{ 0.0f, 0.0f, 1.0f, 1.0f, VEC4_ONE };
					ComputeRects(m_PlayerScreenUI, rect, rects, normalColour, highlightedColour);
				}

				if (m_PlayerInventoryUI.cutType != RectCutType::_NONE &&
					player->bInventoryShowing)
				{
					Rect rect{ 0.0f, 0.0f, 1.0f, 1.0f, VEC4_ONE };
					i32 inventoryUIRectIndex = (i32)rects.size();
					ComputeRects(m_PlayerInventoryUI, rect, rects, normalColour, highlightedColour);
					if (inventoryUIRectIndex < (i32)rects.size())
					{
						rects[inventoryUIRectIndex].colour = m_PlayerInventoryUI.GetColour(darkenedColour, darkenedHighlightedColour);
					}
				}

				// TODO: Allow configurable margins (or use hidden rects)
				real shrinkFactor = 0.01f;
				for (i32 i = 0; i < (i32)rects.size(); ++i)
				{
					glm::vec4 colour = rects[i].colour;
					if (colour.a != 0.0f)
					{
						DrawRect(
							glm::vec2(rects[i].minX + shrinkFactor, rects[i].minY + shrinkFactor) * 2.0f - 1.0f,
							glm::vec2(rects[i].maxX - shrinkFactor, rects[i].maxY - shrinkFactor) * 2.0f - 1.0f, colour, 0.0f);
					}
				}
			}
		}

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

	UIMesh::RectCutResult UIMesh::DrawImGuiRectCut(UIScreen& rectCut, const char* optionalTreeNodeName /* = nullptr */)
	{
		bool bRemoveSelf = false;
		bool bDuplicateSelf = false;

		bool bTreeOpen = ImGui::TreeNode(optionalTreeNodeName != nullptr ? optionalTreeNodeName : "UIScreen");

		rectCut.bHighlighted = ImGui::IsItemHovered();

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
			if (ImGui::BeginCombo("##cutType", RectCutTypeStrings[(i32)rectCut.cutType]))
			{
				for (u32 i = 0; i < (u32)ARRAY_LENGTH(RectCutTypeStrings); ++i)
				{
					bool bSelected = (i == (u32)rectCut.cutType);
					if (ImGui::Selectable(RectCutTypeStrings[i], &bSelected))
					{
						rectCut.cutType = (RectCutType)i;
						m_bUIConfigDirty = true;
					}

				}
				ImGui::EndCombo();
			}

			m_bUIConfigDirty = ImGui::SliderFloat("##amount", &rectCut.amount, 0.0f, 1.0f) || m_bUIConfigDirty;

			m_bUIConfigDirty = ImGui::Checkbox("Visible", &rectCut.bVisible) || m_bUIConfigDirty;

			ImGui::SameLine();

			m_bUIConfigDirty = ImGui::Checkbox("Relative", &rectCut.bRelative) || m_bUIConfigDirty;

			ImGui::SameLine();

			if (ImGui::Button("-"))
			{
				bRemoveSelf = true;
			}

			ImGui::SameLine();

			if (ImGui::Button("+"))
			{
				rectCut.children.emplace_back(UIScreen(RectCutType::TOP, 0.5f, false, true, false, {}));
				m_bUIConfigDirty = true;
			}

			for (i32 i = 0; i < (i32)rectCut.children.size(); ++i)
			{
				ImGui::PushID(i);
				RectCutResult result = DrawImGuiRectCut(rectCut.children[i]);
				ImGui::PopID();

				if (result == RectCutResult::REMOVE)
				{
					rectCut.children.erase(rectCut.children.begin() + i);
					m_bUIConfigDirty = true;
					--i;
				}
				else if (result == RectCutResult::DUPLICATE)
				{
					rectCut.children.emplace(rectCut.children.begin() + i, rectCut.children[i]);
					m_bUIConfigDirty = true;
				}
			}
			ImGui::TreePop();
		}

		return bRemoveSelf ? RectCutResult::REMOVE : (bDuplicateSelf ? RectCutResult::DUPLICATE : RectCutResult::DO_NOTHING);
	}

	void UIMesh::DrawImGui(bool* bShowing)
	{
		if (ImGui::Begin("UI", bShowing))
		{
			if (ImGui::Button(m_bUIConfigDirty ? "Save*" : "Save"))
			{
				Serialize();
			}

			ImGui::SameLine();

			{
				ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColour);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColour);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColour);

				if (ImGui::Button("Reload"))
				{
					if (!Deserialize())
					{
						// There was no file to read, just clear data
						m_PlayerScreenUI = {};
						m_PlayerInventoryUI = {};
					}
				}

				ImGui::PopStyleColor();
				ImGui::PopStyleColor();
				ImGui::PopStyleColor();
			}

			DrawImGuiRectCut(m_PlayerScreenUI, "Player screen");
			DrawImGuiRectCut(m_PlayerInventoryUI, "Player inventory");
		}

		ImGui::End();
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
