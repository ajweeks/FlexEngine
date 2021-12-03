#include "stdafx.hpp"

#include "UIMesh.hpp"

#include "Graphics/Renderer.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "InputManager.hpp"
#include "JSONParser.hpp"
#include "Player.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/SceneManager.hpp"
#include "UI.hpp"
#include "Window/Window.hpp"

namespace flex
{
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
			debugMatCreateInfo.bEditorMaterial = true;
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

		if (NearlyEquals(bottomLeft.x, topRight.x, 0.001f) || NearlyEquals(bottomLeft.y, topRight.y, 0.001f))
		{
			// Degenerate rect
			return;
		}

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

		CHECK_EQ((u32)points.size(), vertCount);
		CHECK_EQ((u32)indices.size(), indexCount);

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

		CHECK_EQ((u32)points.size(), vertCount);
		CHECK_EQ((u32)indices.size(), indexCount);

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
				CHECK_EQ(submeshIndex1, submeshIndex);
				m_DrawData.emplace_back(drawData);
			}
		}
		else
		{
			mesh->GetSubMesh(submeshIndex)->UpdateDynamicVertexData(drawData->vertexBufferCreateInfo, indices);
			m_DrawData[submeshIndex]->uvBlendAmount = uvBlendAmount;
		}
	}

	void UIMesh::Draw()
	{
		PROFILE_AUTO("UIMesh Draw");

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
		i32 usedCount = 0;
		for (DrawData* drawData : m_DrawData)
		{
			if (drawData->bInUse)
			{
				++usedCount;
				drawData->bInUse = false;
			}
		}

		m_LastUsedSubmeshCount = usedCount;
	}

	Mesh* UIMesh::GetMesh()
	{
		return m_Object->GetMesh();
	}

	bool UIMesh::IsSubmeshActive(i32 submeshIndex)
	{
		return m_DrawData[submeshIndex]->bInUse;
	}

	i32 UIMesh::GetSubmeshCount()
	{
		return (i32)m_DrawData.size();
	}

	i32 UIMesh::GetUsedSubmeshCount()
	{
		return m_LastUsedSubmeshCount;
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
