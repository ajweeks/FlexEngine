#include "stdafx.hpp"

#include "Scene/MeshComponent.hpp"

#include "Scene/LoadedMesh.hpp"

IGNORE_WARNINGS_PUSH
#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp> // For make_vec3
IGNORE_WARNINGS_POP

#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "Colors.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "Scene/GameObject.hpp"

namespace flex
{
	const real MeshComponent::GRID_LINE_SPACING = 1.0f;
	const u32 MeshComponent::GRID_LINE_COUNT = 151; // Keep odd to align with origin

	std::map<std::string, LoadedMesh*> MeshComponent::m_LoadedMeshes;

	glm::vec4 MeshComponent::m_DefaultColor_4(1.0f, 1.0f, 1.0f, 1.0f);
	glm::vec3 MeshComponent::m_DefaultPosition(0.0f, 0.0f, 0.0f);
	glm::vec3 MeshComponent::m_DefaultTangent(1.0f, 0.0f, 0.0f);
	glm::vec3 MeshComponent::m_DefaultNormal(0.0f, 1.0f, 0.0f);
	glm::vec2 MeshComponent::m_DefaultTexCoord(0.0f, 0.0f);

	MeshComponent::MeshComponent(GameObject* owner, MaterialID materialID /* = InvalidMaterialID */, bool bSetRequiredAttributesFromMat /* = true */) :
		m_OwningGameObject(owner),
		m_MaterialID(materialID),
		m_UVScale(1.0f, 1.0f)
	{
		if (m_MaterialID == InvalidMaterialID)
		{
			m_MaterialID = g_Renderer->GetPlaceholderMaterialID();
		}

		if (bSetRequiredAttributesFromMat)
		{
			SetRequiredAttributesFromMaterialID(m_MaterialID);
		}
	}

	MeshComponent::~MeshComponent()
	{
	}

	void MeshComponent::DestroyAllLoadedMeshes()
	{
		for (auto& loadedMeshPair : m_LoadedMeshes)
		{
			cgltf_free(loadedMeshPair.second->data);
			delete loadedMeshPair.second;
		}
		m_LoadedMeshes.clear();
	}

	MeshComponent* MeshComponent::ParseJSON(const JSONObject& object, GameObject* owner, MaterialID materialID)
	{
		MeshComponent* newMeshComponent = nullptr;

		std::string meshFilePath = object.GetString("file");
		if (!meshFilePath.empty())
		{
			meshFilePath = RESOURCE_LOCATION  "meshes/" + meshFilePath;
		}
		std::string meshPrefabName = object.GetString("prefab");
		bool bSwapNormalYZ = object.GetBool("swapNormalYZ");
		bool bFlipNormalZ = object.GetBool("flipNormalZ");
		bool bFlipU = object.GetBool("flipU");
		bool bFlipV = object.GetBool("flipV");

		if (materialID == InvalidMaterialID)
		{
			PrintWarn("Mesh component requires material index to be parsed: %s\n", owner->GetName().c_str());
			materialID = g_Renderer->GetPlaceholderMaterialID();
		}

		if (!meshFilePath.empty())
		{
			newMeshComponent = new MeshComponent(owner, materialID);

			MeshImportSettings importSettings = {};
			importSettings.bFlipU = bFlipU;
			importSettings.bFlipV = bFlipV;
			importSettings.bFlipNormalZ = bFlipNormalZ;
			importSettings.bSwapNormalYZ = bSwapNormalYZ;

			newMeshComponent->LoadFromFile(meshFilePath, &importSettings);

			owner->SetMeshComponent(newMeshComponent);
		}
		else if (!meshPrefabName.empty())
		{
			newMeshComponent = new MeshComponent(owner, materialID);

			MeshComponent::PrefabShape prefabShape = MeshComponent::StringToPrefabShape(meshPrefabName);
			newMeshComponent->LoadPrefabShape(prefabShape);

			owner->SetMeshComponent(newMeshComponent);
		}
		else
		{
			PrintError("Unhandled mesh field on object: %s\n", owner->GetName().c_str());
		}

		return newMeshComponent;
	}

	JSONObject MeshComponent::Serialize() const
	{
		JSONObject meshObject = {};

		if (m_Type == MeshComponent::Type::FILE)
		{
			std::string prefixStr = RESOURCE_LOCATION  "meshes/";
			std::string meshFilepath = GetRelativeFilePath().substr(prefixStr.length());
			meshObject.fields.emplace_back("file", JSONValue(meshFilepath));
		}
		// TODO: CLEANUP: Remove "prefab" meshes entirely (always load from file)
		else if (m_Type == MeshComponent::Type::PREFAB)
		{
			std::string prefabShapeStr = MeshComponent::PrefabShapeToString(m_Shape);
			meshObject.fields.emplace_back("prefab", JSONValue(prefabShapeStr));
		}
		else
		{
			PrintError("Unhandled mesh prefab type when attempting to serialize scene!\n");
		}

		MeshImportSettings importSettings = m_ImportSettings;
		meshObject.fields.emplace_back("swapNormalYZ", JSONValue(importSettings.bSwapNormalYZ));
		meshObject.fields.emplace_back("flipNormalZ", JSONValue(importSettings.bFlipNormalZ));
		meshObject.fields.emplace_back("flipU", JSONValue(importSettings.bFlipU));
		meshObject.fields.emplace_back("flipV", JSONValue(importSettings.bFlipV));

		return meshObject;
	}

	void MeshComponent::Update()
	{
		// TODO: Move elsewhere
		if (m_Shape == PrefabShape::GRID)
		{
			Transform* transform = m_OwningGameObject->GetTransform();
			glm::vec3 camPos = g_CameraManager->CurrentCamera()->GetPosition();
			glm::vec3 newGridPos = glm::vec3(camPos.x - fmod(
				camPos.x + GRID_LINE_SPACING / 2.0f, GRID_LINE_SPACING),
				transform->GetWorldPosition().y,
				camPos.z - fmod(camPos.z + GRID_LINE_SPACING / 2.0f, GRID_LINE_SPACING));
			transform->SetWorldPosition(newGridPos);
		}
	}

	void MeshComponent::UpdateProceduralData(VertexBufferDataCreateInfo const* newData)
	{
		m_VertexBufferData.UpdateData(newData);
		g_Renderer->UpdateVertexData(m_OwningGameObject->GetRenderID(), &m_VertexBufferData);
	}

	void MeshComponent::Destroy()
	{
		m_VertexBufferData.Destroy();
		m_OwningGameObject = nullptr;
		m_bInitialized = false;
	}

	void MeshComponent::SetOwner(GameObject* owner)
	{
		m_OwningGameObject = owner;
	}

	void MeshComponent::SetRequiredAttributesFromMaterialID(MaterialID matID)
	{
		assert(matID != InvalidMaterialID);

		Material& mat = g_Renderer->GetMaterial(matID);
		Shader& shader = g_Renderer->GetShader(mat.shaderID);
		m_RequiredAttributes = shader.vertexAttributes;
	}

	bool MeshComponent::LoadFromFile(
		const std::string& relativeFilePath,
		MeshImportSettings* importSettings /* = nullptr */,
		RenderObjectCreateInfo* optionalCreateInfo /* = nullptr */)
	{
		if (m_bInitialized)
		{
			PrintError("Attempted to load mesh after already initialized! If reloading, first call Destroy\n");
			return false;
		}

		assert(m_RequiredAttributes != (u32)VertexAttribute::_NONE);

		m_Type = Type::FILE;
		m_Shape = PrefabShape::_NONE;
		m_RelativeFilePath = relativeFilePath;
		m_FileName = StripLeadingDirectories(m_RelativeFilePath);

		m_BoundingSphereRadius = 0;
		m_BoundingSphereCenterPoint = VEC3_ZERO;
		m_VertexBufferData.Destroy();

		LoadedMesh* loadedMesh = nullptr;
		if (FindPreLoadedMesh(relativeFilePath, &loadedMesh))
		{
			// If no import settings have been passed in, grab any from the cached mesh
			if (importSettings == nullptr)
			{
				importSettings = &loadedMesh->importSettings;
			}
		}
		else
		{
			// Mesh hasn't been loaded before, load it now
			loadedMesh = LoadMesh(relativeFilePath, importSettings);
		}

		if (loadedMesh == nullptr)
		{
			PrintError("Failed to load mesh at %s\n", relativeFilePath.c_str());
			return false;
		}

		m_ImportSettings = loadedMesh->importSettings;

		cgltf_data* data = loadedMesh->data;
		if (data == nullptr)
		{
			PrintError("Failed to load mesh at %s\n", relativeFilePath.c_str());
			return false;
		}

		if (data->meshes_count == 0)
		{
			PrintError("Loaded mesh file has no meshes\n");
			return false;
		}

		VertexBufferDataCreateInfo vertexBufferDataCreateInfo = {};

		//size_t totalVertCount = 0;
		m_MinPoint = glm::vec3(FLT_MAX);
		m_MaxPoint = glm::vec3(-FLT_MAX);

		//assert(data->meshes_count == 1);
		for (i32 i = 0; i < (i32)1; ++i)
		{
			cgltf_mesh* mesh = &(data->meshes[i]);

			bool bCalculateTangents = false;

			for (i32 j = 0; j < (i32)mesh->primitives_count; ++j)
			{
				cgltf_primitive* primitive = &(mesh->primitives[j]);

				if (primitive->indices == nullptr)
				{
					continue;
				}

				i32 vertexStart = (i32)vertexBufferDataCreateInfo.positions_3D.size();
				i32 indexStart = m_Indices.size();

				i32 posAttribIndex = -1;
				i32 normAttribIndex = -1;
				i32 tanAttribIndex = -1;
				i32 colAttribIndex = -1;
				i32 uvAttribIndex = -1;
				for (i32 k = 0; k < (i32)primitive->attributes_count; ++k)
				{
					if (strcmp(primitive->attributes[k].name, "POSITION") == 0)
					{
						posAttribIndex = k;
						continue;
					}
					if (strcmp(primitive->attributes[k].name, "NORMAL") == 0)
					{
						normAttribIndex = k;
						continue;
					}
					if (strcmp(primitive->attributes[k].name, "TANGENT") == 0)
					{
						tanAttribIndex = k;
						continue;
					}
					if (strcmp(primitive->attributes[k].name, "COLOR_0") == 0)
					{
						colAttribIndex = k;
						continue;
					}
					if (strcmp(primitive->attributes[k].name, "TEXCOORD_0") == 0)
					{
						uvAttribIndex = k;
						continue;
					}
				}

				assert(posAttribIndex != -1);
				cgltf_accessor* posAccessor = primitive->attributes[posAttribIndex].data;
				assert(primitive->attributes[posAttribIndex].type == cgltf_attribute_type_position);
				assert(posAccessor->component_type == cgltf_component_type_r_32f);
				assert(posAccessor->type == cgltf_type_vec3);
				u32 vertCount = posAccessor->count;

				vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::POSITION;

				assert(posAccessor->has_min);
				assert(posAccessor->has_max);
				glm::vec3 posMin = glm::make_vec3(&posAccessor->min[0]);
				glm::vec3 posMax = glm::make_vec3(&posAccessor->max[0]);

				m_MinPoint = glm::min(m_MinPoint, posMin);
				m_MaxPoint = glm::max(m_MaxPoint, posMax);


				if (tanAttribIndex == -1 && (m_RequiredAttributes & (u32)VertexAttribute::TANGENT))
				{
					bCalculateTangents = true;

					if (uvAttribIndex == -1)
					{
						PrintError("Can't generate tangents for mesh which has no tex coords: %s\n", m_FileName.c_str());
					}
				}

				{
					if (m_RequiredAttributes & (u32)VertexAttribute::POSITION) vertexBufferDataCreateInfo.positions_3D.resize(vertexBufferDataCreateInfo.positions_3D.size() + vertCount);
					if (m_RequiredAttributes & (u32)VertexAttribute::POSITION2) vertexBufferDataCreateInfo.positions_2D.resize(vertexBufferDataCreateInfo.positions_2D.size() + vertCount);
					if (m_RequiredAttributes & (u32)VertexAttribute::NORMAL) vertexBufferDataCreateInfo.normals.resize(vertexBufferDataCreateInfo.normals.size() + vertCount);
					if (m_RequiredAttributes & (u32)VertexAttribute::TANGENT) vertexBufferDataCreateInfo.tangents.resize(vertexBufferDataCreateInfo.tangents.size() + vertCount);
					if (m_RequiredAttributes & (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT) vertexBufferDataCreateInfo.colors_R32G32B32A32.resize(vertexBufferDataCreateInfo.colors_R32G32B32A32.size() + vertCount);
					if (m_RequiredAttributes & (u32)VertexAttribute::COLOR_R8G8B8A8_UNORM) vertexBufferDataCreateInfo.colors_R8G8B8A8.resize(vertexBufferDataCreateInfo.colors_R8G8B8A8.size() + vertCount);
					if (m_RequiredAttributes & (u32)VertexAttribute::UV) vertexBufferDataCreateInfo.texCoords_UV.resize(vertexBufferDataCreateInfo.texCoords_UV.size() + vertCount);
					if (m_RequiredAttributes & (u32)VertexAttribute::EXTRA_VEC4) vertexBufferDataCreateInfo.extraVec4s.resize(vertexBufferDataCreateInfo.extraVec4s.size() + vertCount);
					if (m_RequiredAttributes & (u32)VertexAttribute::EXTRA_INT) vertexBufferDataCreateInfo.extraInts.resize(vertexBufferDataCreateInfo.extraInts.size() + vertCount);
				}

				// Vertices
				for (u32 vi = 0; vi < vertCount; ++vi)
				{
					u32 v = vertexStart + vi;

					// Position
					glm::vec3 pos;
					cgltf_accessor_read_float(posAccessor, vi, &pos.x, 3);
					vertexBufferDataCreateInfo.positions_3D[v] = pos;

					// Normal
					if (m_RequiredAttributes & (u32)VertexAttribute::NORMAL)
					{
						vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::NORMAL;

						if (normAttribIndex == -1)
						{
							vertexBufferDataCreateInfo.normals[v] = m_DefaultNormal;
						}
						else
						{
							cgltf_accessor* normAccessor = primitive->attributes[normAttribIndex].data;
							assert(primitive->attributes[normAttribIndex].type == cgltf_attribute_type_normal);
							assert(normAccessor->component_type == cgltf_component_type_r_32f);
							assert(normAccessor->type == cgltf_type_vec3);

							glm::vec3 norm;
							cgltf_accessor_read_float(normAccessor, vi, &norm.x, 3);
							if (importSettings && importSettings->bSwapNormalYZ)
							{
								std::swap(norm.y, norm.z);
							}
							if (importSettings && importSettings->bFlipNormalZ)
							{
								norm.z = -norm.z;
							}
							vertexBufferDataCreateInfo.normals[v] = norm;
						}
					}

					// Tangent
					if (m_RequiredAttributes & (u32)VertexAttribute::TANGENT)
					{
						vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::TANGENT;

						if (tanAttribIndex == -1)
						{
							vertexBufferDataCreateInfo.tangents[v] = m_DefaultTangent;
						}
						else
						{
							cgltf_accessor* tanAccessor = primitive->attributes[tanAttribIndex].data;
							assert(primitive->attributes[tanAttribIndex].type == cgltf_attribute_type_tangent);
							assert(tanAccessor->component_type == cgltf_component_type_r_32f);
							//assert(tanAccessor->type == cgltf_type_vec3);

							glm::vec4 tangent;
							cgltf_accessor_read_float(tanAccessor, vi, &tangent.x, 4);
							vertexBufferDataCreateInfo.tangents[v] = tangent;
						}
					}

					// Color
					if (m_RequiredAttributes & (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT)
					{
						vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;

						if (colAttribIndex == -1)
						{
							vertexBufferDataCreateInfo.colors_R32G32B32A32[v] = m_DefaultColor_4;
						}
						else
						{
							cgltf_accessor* colAccessor = primitive->attributes[colAttribIndex].data;
							assert(primitive->attributes[colAttribIndex].type == cgltf_attribute_type_color);
							assert(colAccessor->component_type == cgltf_component_type_r_32f);
							assert(colAccessor->type == cgltf_type_vec4);

							glm::vec4 col;
							cgltf_accessor_read_float(colAccessor, vi, &col.x, 4);
							vertexBufferDataCreateInfo.colors_R32G32B32A32[v] = col;
						}
					}

					// UV 0
					if (m_RequiredAttributes & (u32)VertexAttribute::UV)
					{
						vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::UV;

						if (uvAttribIndex == -1)
						{
							vertexBufferDataCreateInfo.texCoords_UV[v] = m_DefaultTexCoord;
						}
						else
						{
							cgltf_accessor* uvAccessor = primitive->attributes[uvAttribIndex].data;
							assert(primitive->attributes[uvAttribIndex].type == cgltf_attribute_type_texcoord);
							assert(uvAccessor->component_type == cgltf_component_type_r_32f);
							assert(uvAccessor->type == cgltf_type_vec2);

							glm::vec2 uv0;
							cgltf_accessor_read_float(uvAccessor, vi, &uv0.x, 2);

							uv0 *= m_UVScale;
							if (importSettings && importSettings->bFlipU)
							{
								uv0.x = 1.0f - uv0.x;
							}
							if (importSettings && importSettings->bFlipV)
							{
								uv0.y = 1.0f - uv0.y;
							}
							vertexBufferDataCreateInfo.texCoords_UV[v] = uv0;
						}
					}
				}

				// Indices
				{
					assert(primitive->indices->type == cgltf_type_scalar);
					const i32 indexCount = (i32)primitive->indices->count;
					m_Indices.resize(m_Indices.size() + indexCount);

					//assert(primitive->indices->buffer_view->type == cgltf_buffer_view_type_indices);
					assert(primitive->indices->component_type == cgltf_component_type_r_8u ||
						primitive->indices->component_type == cgltf_component_type_r_16u ||
						primitive->indices->component_type == cgltf_component_type_r_32u);

					for (i32 l = 0; l < indexCount; ++l)
					{
						m_Indices[indexStart + l] = vertexStart + cgltf_accessor_read_index(primitive->indices, l);
					}
				}
			}

			if (bCalculateTangents)
			{
				if (!CalculateTangents(vertexBufferDataCreateInfo))
				{
					PrintWarn("Failed to calculate tangents for mesh!\n");
				}
			}
		}

		// Calculate bounding sphere radius
		if (!vertexBufferDataCreateInfo.positions_3D.empty())
		{
			m_BoundingSphereCenterPoint = m_MinPoint + (m_MaxPoint - m_MinPoint) / 2.0f;

			for (const glm::vec3& pos : vertexBufferDataCreateInfo.positions_3D)
			{
				real posMagnitude = glm::length(pos - m_BoundingSphereCenterPoint);
				if (posMagnitude > m_BoundingSphereRadius)
				{
					m_BoundingSphereRadius = posMagnitude;
				}
			}
			if (m_BoundingSphereRadius == 0.0f)
			{
				PrintError("Mesh's bounding sphere's radius is 0, do any valid vertices exist?\n");
			}
		}

		m_VertexBufferData.Initialize(&vertexBufferDataCreateInfo);

		RenderObjectCreateInfo renderObjectCreateInfo = {};

		if (optionalCreateInfo)
		{
			CopyInOptionalCreateInfo(renderObjectCreateInfo, *optionalCreateInfo);
		}

		renderObjectCreateInfo.gameObject = m_OwningGameObject;
		renderObjectCreateInfo.vertexBufferData = &m_VertexBufferData;
		renderObjectCreateInfo.indices = &m_Indices;
		renderObjectCreateInfo.materialID = m_MaterialID;

		if (m_OwningGameObject->GetRenderID() != InvalidRenderID)
		{
			g_Renderer->DestroyRenderObject(m_OwningGameObject->GetRenderID());
		}
		RenderID renderID = g_Renderer->InitializeRenderObject(&renderObjectCreateInfo);
		m_OwningGameObject->SetRenderID(renderID);

		g_Renderer->SetTopologyMode(renderID, TopologyMode::TRIANGLE_LIST);

		m_VertexBufferData.DescribeShaderVariables(g_Renderer, renderID);

		m_bInitialized = true;

		return true;
	}

	bool MeshComponent::LoadPrefabShape(PrefabShape shape, RenderObjectCreateInfo* optionalCreateInfo)
	{
		if (m_bInitialized)
		{
			PrintError("Attempted to load mesh after already initialized! If reloading, first call Destroy\n");
			return false;
		}

		m_Type = Type::PREFAB;
		m_Shape = shape;

		m_VertexBufferData.Destroy();

		// TODO: Move to game object?
		m_OwningGameObject->GetTransform()->SetGameObject(m_OwningGameObject);

		RenderObjectCreateInfo renderObjectCreateInfo = {};

		if (optionalCreateInfo)
		{
			CopyInOptionalCreateInfo(renderObjectCreateInfo, *optionalCreateInfo);
		}

		renderObjectCreateInfo.gameObject = m_OwningGameObject;
		renderObjectCreateInfo.materialID = m_MaterialID;

		TopologyMode topologyMode = TopologyMode::TRIANGLE_LIST;

		VertexBufferDataCreateInfo vertexBufferDataCreateInfo = {};

		switch (shape)
		{
		case MeshComponent::PrefabShape::CUBE:
		{
			vertexBufferDataCreateInfo.positions_3D =
			{
				// Front
				{ -5.0f, -5.0f, -5.0f, },
				{ -5.0f,  5.0f, -5.0f, },
				{ 5.0f,  5.0f, -5.0f, },

				{ -5.0f, -5.0f, -5.0f, },
				{ 5.0f,  5.0f, -5.0f, },
				{ 5.0f, -5.0f, -5.0f, },

				// Top
				{ -5.0f, 5.0f, -5.0f, },
				{ -5.0f, 5.0f,  5.0f, },
				{ 5.0f,  5.0f,  5.0f, },

				{ -5.0f, 5.0f, -5.0f, },
				{ 5.0f,  5.0f,  5.0f, },
				{ 5.0f,  5.0f, -5.0f, },

				// Back
				{ 5.0f, -5.0f, 5.0f, },
				{ 5.0f,  5.0f, 5.0f, },
				{ -5.0f,  5.0f, 5.0f, },

				{ 5.0f, -5.0f, 5.0f, },
				{ -5.0f, 5.0f, 5.0f, },
				{ -5.0f, -5.0f, 5.0f, },

				// Bottom
				{ -5.0f, -5.0f, -5.0f, },
				{ 5.0f, -5.0f, -5.0f, },
				{ 5.0f,  -5.0f,  5.0f, },

				{ -5.0f, -5.0f, -5.0f, },
				{ 5.0f, -5.0f,  5.0f, },
				{ -5.0f, -5.0f,  5.0f, },

				// Right
				{ 5.0f, -5.0f, -5.0f, },
				{ 5.0f,  5.0f, -5.0f, },
				{ 5.0f,  5.0f,  5.0f, },

				{ 5.0f, -5.0f, -5.0f, },
				{ 5.0f,  5.0f,  5.0f, },
				{ 5.0f, -5.0f,  5.0f, },

				// Left
				{ -5.0f, -5.0f, -5.0f, },
				{ -5.0f,  5.0f,  5.0f, },
				{ -5.0f,  5.0f, -5.0f, },

				{ -5.0f, -5.0f, -5.0f, },
				{ -5.0f, -5.0f,  5.0f, },
				{ -5.0f,  5.0f,  5.0f, },
			};
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::POSITION;

			vertexBufferDataCreateInfo.normals =
			{
				// Front
				{ 0.0f, 0.0f, -1.0f, },
				{ 0.0f, 0.0f, -1.0f, },
				{ 0.0f, 0.0f, -1.0f, },

				{ 0.0f, 0.0f, -1.0f, },
				{ 0.0f, 0.0f, -1.0f, },
				{ 0.0f, 0.0f, -1.0f, },

				// Top
				{ 0.0f, -1.0f, 0.0f, },
				{ 0.0f, -1.0f, 0.0f, },
				{ 0.0f, -1.0f, 0.0f, },

				{ 0.0f, -1.0f, 0.0f, },
				{ 0.0f, -1.0f, 0.0f, },
				{ 0.0f, -1.0f, 0.0f, },

				// Back
				{ 0.0f, 0.0f, 1.0f, },
				{ 0.0f, 0.0f, 1.0f, },
				{ 0.0f, 0.0f, 1.0f, },

				{ 0.0f, 0.0f, 1.0f, },
				{ 0.0f, 0.0f, 1.0f, },
				{ 0.0f, 0.0f, 1.0f, },

				// Bottom
				{ 0.0f, -1.0f, 0.0f, },
				{ 0.0f, -1.0f, 0.0f, },
				{ 0.0f, -1.0f, 0.0f, },

				{ 0.0f, -1.0f, 0.0f, },
				{ 0.0f, -1.0f, 0.0f, },
				{ 0.0f, -1.0f, 0.0f, },

				// Right
				{ 1.0f, 0.0f, 0.0f, },
				{ 1.0f, 0.0f, 0.0f, },
				{ 1.0f, 0.0f, 0.0f, },

				{ 1.0f, 0.0f, 0.0f, },
				{ 1.0f, 0.0f, 0.0f, },
				{ 1.0f, 0.0f, 0.0f, },

				// Left
				{ -1.0f, 0.0f, 0.0f, },
				{ -1.0f, 0.0f, 0.0f, },
				{ -1.0f, 0.0f, 0.0f, },

				{ -1.0f, 0.0f, 0.0f, },
				{ -1.0f, 0.0f, 0.0f, },
				{ -1.0f, 0.0f, 0.0f, },
			};
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::NORMAL;

			vertexBufferDataCreateInfo.texCoords_UV =
			{
				// Front
				{ 0.0f, 0.0f },
				{ 0.0f, 1.0f },
				{ 1.0f, 1.0f },

				{ 0.0f, 0.0f },
				{ 1.0f, 1.0f },
				{ 1.0f, 0.0f },

				// Top
				{ 0.0f, 0.0f },
				{ 0.0f, 1.0f },
				{ 1.0f, 1.0f },

				{ 0.0f, 0.0f },
				{ 1.0f, 1.0f },
				{ 1.0f, 0.0f },

				// Back
				{ 0.0f, 0.0f },
				{ 0.0f, 1.0f },
				{ 1.0f, 1.0f },

				{ 0.0f, 0.0f },
				{ 1.0f, 1.0f },
				{ 1.0f, 0.0f },

				// Bottom
				{ 0.0f, 0.0f },
				{ 0.0f, 1.0f },
				{ 1.0f, 1.0f },

				{ 0.0f, 0.0f },
				{ 1.0f, 1.0f },
				{ 1.0f, 0.0f },

				// Right
				{ 0.0f, 0.0f },
				{ 0.0f, 1.0f },
				{ 1.0f, 1.0f },

				{ 0.0f, 0.0f },
				{ 1.0f, 1.0f },
				{ 1.0f, 0.0f },

				// Left
				{ 0.0f, 0.0f },
				{ 0.0f, 1.0f },
				{ 1.0f, 1.0f },

				{ 0.0f, 0.0f },
				{ 1.0f, 1.0f },
				{ 1.0f, 0.0f },
			};
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::UV;
		} break;
		case MeshComponent::PrefabShape::GRID:
		{
			const real lineMaxOpacity = 0.5f;
			glm::vec4 lineColor = Color::GRAY;
			lineColor.a = lineMaxOpacity;

			const size_t vertexCount = GRID_LINE_COUNT * 2 * 4; // 4 verts per line (to allow for fading) *------**------*
			vertexBufferDataCreateInfo.positions_3D.reserve(vertexCount);
			vertexBufferDataCreateInfo.colors_R32G32B32A32.reserve(vertexCount);

			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::POSITION;
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;

			real halfWidth = (GRID_LINE_SPACING * (GRID_LINE_COUNT - 1)) / 2.0f;

			// TODO: Don't generate center lines
			// Horizontal lines
			for (u32 i = 0; i < GRID_LINE_COUNT; ++i)
			{
				vertexBufferDataCreateInfo.positions_3D.emplace_back(i * GRID_LINE_SPACING - halfWidth, 0.0f, -halfWidth);
				vertexBufferDataCreateInfo.positions_3D.emplace_back(i * GRID_LINE_SPACING - halfWidth, 0.0f, 0.0f);
				vertexBufferDataCreateInfo.positions_3D.emplace_back(i * GRID_LINE_SPACING - halfWidth, 0.0f, 0.0f);
				vertexBufferDataCreateInfo.positions_3D.emplace_back(i * GRID_LINE_SPACING - halfWidth, 0.0f, halfWidth);

				real opacityCenter = glm::pow(1.0f - glm::abs((i / (real)GRID_LINE_COUNT) - 0.5f) * 2.0f, 5.0f);
				glm::vec4 colorCenter = lineColor;
				colorCenter.a = opacityCenter;
				glm::vec4 colorEnds = colorCenter;
				colorEnds.a = 0.0f;
				vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(colorEnds);
				vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(colorCenter);
				vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(colorCenter);
				vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(colorEnds);
			}

			// Vertical lines
			for (u32 i = 0; i < GRID_LINE_COUNT; ++i)
			{
				vertexBufferDataCreateInfo.positions_3D.emplace_back(-halfWidth, 0.0f, i * GRID_LINE_SPACING - halfWidth);
				vertexBufferDataCreateInfo.positions_3D.emplace_back(0.0f, 0.0f, i * GRID_LINE_SPACING - halfWidth);
				vertexBufferDataCreateInfo.positions_3D.emplace_back(0.0f, 0.0f, i * GRID_LINE_SPACING - halfWidth);
				vertexBufferDataCreateInfo.positions_3D.emplace_back(halfWidth, 0.0f, i * GRID_LINE_SPACING - halfWidth);

				real opacityCenter = glm::pow(1.0f - glm::abs((i / (real)GRID_LINE_COUNT) - 0.5f) * 2.0f, 5.0f);
				glm::vec4 colorCenter = lineColor;
				colorCenter.a = opacityCenter;
				glm::vec4 colorEnds = colorCenter;
				colorEnds.a = 0.0f;
				vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(colorEnds);
				vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(colorCenter);
				vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(colorCenter);
				vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(colorEnds);
			}

			// Make sure we didn't allocate too much data
			assert(vertexBufferDataCreateInfo.positions_3D.capacity() == vertexBufferDataCreateInfo.positions_3D.size());
			assert(vertexBufferDataCreateInfo.colors_R32G32B32A32.capacity() == vertexBufferDataCreateInfo.colors_R32G32B32A32.size());

			topologyMode = TopologyMode::LINE_LIST;
		} break;
		case MeshComponent::PrefabShape::WORLD_AXIS_GROUND:
		{
			glm::vec4 centerLineColorX = Color::RED;
			glm::vec4 centerLineColorZ = Color::BLUE;

			const size_t vertexCount = 4 * 2; // 4 verts per line (to allow for fading) *------**------*
			vertexBufferDataCreateInfo.positions_3D.reserve(vertexCount);
			vertexBufferDataCreateInfo.colors_R32G32B32A32.reserve(vertexCount);

			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::POSITION;
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;

			real halfWidth = (GRID_LINE_SPACING * (GRID_LINE_COUNT - 1)); // extend longer than normal grid lines

			vertexBufferDataCreateInfo.positions_3D.emplace_back(0.0f, 0.0f, -halfWidth);
			vertexBufferDataCreateInfo.positions_3D.emplace_back(0.0f, 0.0f, 0.0f);
			vertexBufferDataCreateInfo.positions_3D.emplace_back(0.0f, 0.0f, 0.0f);
			vertexBufferDataCreateInfo.positions_3D.emplace_back(0.0f, 0.0f, halfWidth);

			real opacityCenter = 1.0f;
			glm::vec4 colorCenter = centerLineColorZ;
			colorCenter.a = opacityCenter;
			glm::vec4 colorEnds = colorCenter;
			colorEnds.a = 0.0f;
			vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(colorEnds);
			vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(colorCenter);
			vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(colorCenter);
			vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(colorEnds);

			vertexBufferDataCreateInfo.positions_3D.emplace_back(-halfWidth, 0.0f, 0.0f);
			vertexBufferDataCreateInfo.positions_3D.emplace_back(0.0f, 0.0f, 0.0f);
			vertexBufferDataCreateInfo.positions_3D.emplace_back(0.0f, 0.0f, 0.0f);
			vertexBufferDataCreateInfo.positions_3D.emplace_back(halfWidth, 0.0f, 0.0f);

			colorCenter = centerLineColorX;
			colorCenter.a = opacityCenter;
			colorEnds = colorCenter;
			colorEnds.a = 0.0f;
			vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(colorEnds);
			vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(colorCenter);
			vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(colorCenter);
			vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(colorEnds);

			// Make sure we didn't allocate too much data
			assert(vertexBufferDataCreateInfo.positions_3D.capacity() == vertexBufferDataCreateInfo.positions_3D.size());
			assert(vertexBufferDataCreateInfo.colors_R32G32B32A32.capacity() == vertexBufferDataCreateInfo.colors_R32G32B32A32.size());

			topologyMode = TopologyMode::LINE_LIST;
		} break;
		case MeshComponent::PrefabShape::PLANE:
		{
			vertexBufferDataCreateInfo.positions_3D =
			{
				{ -50.0f, 0.0f, -50.0f, },
				{ -50.0f, 0.0f,  50.0f, },
				{ 50.0f,  0.0f,  50.0f, },

				{ -50.0f, 0.0f, -50.0f, },
				{ 50.0f,  0.0f,  50.0f, },
				{ 50.0f,  0.0f, -50.0f, },
			};
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::POSITION;

			vertexBufferDataCreateInfo.normals =
			{
				{ 0.0f, 1.0f, 0.0f, },
				{ 0.0f, 1.0f, 0.0f, },
				{ 0.0f, 1.0f, 0.0f, },

				{ 0.0f, 1.0f, 0.0f, },
				{ 0.0f, 1.0f, 0.0f, },
				{ 0.0f, 1.0f, 0.0f, },
			};
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::NORMAL;

			vertexBufferDataCreateInfo.tangents =
			{
				{ 0.0f, 0.0f, 1.0f, },
				{ 0.0f, 0.0f, 1.0f, },
				{ 0.0f, 0.0f, 1.0f, },

				{ 0.0f, 0.0f, 1.0f, },
				{ 0.0f, 0.0f, 1.0f, },
				{ 0.0f, 0.0f, 1.0f, },
			};
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::TANGENT;

			vertexBufferDataCreateInfo.colors_R32G32B32A32 =
			{
				{ 1.0f, 1.0f, 1.0f, 1.0f },
				{ 1.0f, 1.0f, 1.0f, 1.0f },
				{ 1.0f, 1.0f, 1.0f, 1.0f },

				{ 1.0f, 1.0f, 1.0f, 1.0f },
				{ 1.0f, 1.0f, 1.0f, 1.0f },
				{ 1.0f, 1.0f, 1.0f, 1.0f },
			};
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;

			vertexBufferDataCreateInfo.texCoords_UV =
			{
				{ 0.0f, 0.0f },
				{ 0.0f, 1.0f },
				{ 1.0f, 1.0f },

				{ 0.0f, 0.0f },
				{ 1.0f, 1.0f },
				{ 1.0f, 0.0f },
			};
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::UV;
		} break;
		case MeshComponent::PrefabShape::GERSTNER_PLANE:
		{
			// TODO: Provide as input
			i32 vertCountH = 100;

			i32 vertCount = vertCountH * vertCountH;
			vertexBufferDataCreateInfo.positions_3D.resize(vertCount);
			vertexBufferDataCreateInfo.normals.resize(vertCount);
			vertexBufferDataCreateInfo.tangents.resize(vertCount);
			vertexBufferDataCreateInfo.colors_R32G32B32A32.resize(vertCount);

			// NOTE: Wave generation is implemented in GerstnerWave::Update

			// Init two values so bounding sphere radius isn't zero
			vertexBufferDataCreateInfo.positions_3D[0] = VEC3_NEG_ONE;
			vertexBufferDataCreateInfo.positions_3D[1] = VEC3_ONE;

			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::POSITION;
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::NORMAL;
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::TANGENT;
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;

			i32 indexCount = 6 * vertCount;
			m_Indices.resize(indexCount);
			i32 i = 0;
			for (i32 z = 0; z < vertCountH - 1; ++z)
			{
				for (i32 x = 0; x < vertCountH - 1; ++x)
				{
					i32 vertIdx = z * vertCountH + x;
					m_Indices[i++] = vertIdx;
					m_Indices[i++] = vertIdx + vertCountH;
					m_Indices[i++] = vertIdx + 1;

					vertIdx = vertIdx + 1 + vertCountH;
					m_Indices[i++] = vertIdx;
					m_Indices[i++] = vertIdx - vertCountH;
					m_Indices[i++] = vertIdx - 1;
				}
			}
		} break;
		case MeshComponent::PrefabShape::UV_SPHERE:
		{
			// Vertices
			glm::vec3 v1(0.0f, 1.0f, 0.0f); // Top vertex
			vertexBufferDataCreateInfo.positions_3D.push_back(v1);
			vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(Color::RED);
			vertexBufferDataCreateInfo.texCoords_UV.emplace_back(0.0f, 0.0f);
			vertexBufferDataCreateInfo.normals.emplace_back(0.0f, 1.0f, 0.0f);

			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::POSITION;
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::UV;
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::NORMAL;

			u32 parallelCount = 10;
			u32 meridianCount = 5;

			for (u32 j = 0; j < parallelCount - 1; ++j)
			{
				real polar = PI * real(j + 1) / (real)parallelCount;
				real sinP = sin(polar);
				real cosP = cos(polar);
				for (u32 i = 0; i < meridianCount; ++i)
				{
					real azimuth = TWO_PI * (real)i / (real)meridianCount;
					real sinA = sin(azimuth);
					real cosA = cos(azimuth);
					glm::vec3 point(sinP * cosA, cosP, sinP * sinA);
					const glm::vec4 color =
						(i % 2 == 0 ? j % 2 == 0 ? Color::ORANGE : Color::PURPLE : j % 2 == 0 ? Color::WHITE : Color::YELLOW);

					vertexBufferDataCreateInfo.positions_3D.push_back(point);
					vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(color);
					vertexBufferDataCreateInfo.texCoords_UV.emplace_back(0.0f, 0.0f);
					vertexBufferDataCreateInfo.normals.emplace_back(1.0f, 0.0f, 0.0f);
				}
			}

			glm::vec3 vF(0.0f, -1.0f, 0.0f); // Bottom vertex
			vertexBufferDataCreateInfo.positions_3D.push_back(vF);
			vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(Color::YELLOW);
			vertexBufferDataCreateInfo.texCoords_UV.emplace_back(0.0f, 0.0f);
			vertexBufferDataCreateInfo.normals.emplace_back(0.0f, -1.0f, 0.0f);

			const u32 numVerts = vertexBufferDataCreateInfo.positions_3D.size();

			// Indices
			m_Indices.clear();

			// Top triangles
			for (size_t i = 0; i < meridianCount; ++i)
			{
				u32 a = i + 1;
				u32 b = (i + 1) % meridianCount + 1;
				m_Indices.push_back(0);
				m_Indices.push_back(b);
				m_Indices.push_back(a);
			}

			// Center quads
			for (size_t j = 0; j < parallelCount - 2; ++j)
			{
				u32 aStart = j * meridianCount + 1;
				u32 bStart = (j + 1) * meridianCount + 1;
				for (size_t i = 0; i < meridianCount; ++i)
				{
					u32 a = aStart + i;
					u32 a1 = aStart + (i + 1) % meridianCount;
					u32 b = bStart + i;
					u32 b1 = bStart + (i + 1) % meridianCount;
					m_Indices.push_back(a);
					m_Indices.push_back(a1);
					m_Indices.push_back(b1);

					m_Indices.push_back(a);
					m_Indices.push_back(b1);
					m_Indices.push_back(b);
				}
			}

			// Bottom triangles
			for (size_t i = 0; i < meridianCount; ++i)
			{
				u32 a = i + meridianCount * (parallelCount - 2) + 1;
				u32 b = (i + 1) % meridianCount + meridianCount * (parallelCount - 2) + 1;
				m_Indices.push_back(numVerts - 1);
				m_Indices.push_back(a);
				m_Indices.push_back(b);
			}
		} break;
		case MeshComponent::PrefabShape::SKYBOX:
		{
			// TODO: Move to separate class?
			// FIXME
			//m_SerializableType = SerializableType::SKYBOX;
			vertexBufferDataCreateInfo.positions_3D =
			{
				// Front
				{ -0.5f, -0.5f, -0.5f, },
				{ -0.5f,  0.5f, -0.5f, },
				{ 0.5f,  0.5f, -0.5f, },

				{ -0.5f, -0.5f, -0.5f, },
				{ 0.5f,  0.5f, -0.5f, },
				{ 0.5f, -0.5f, -0.5f, },

				// Top
				{ -0.5f, 0.5f, -0.5f, },
				{ -0.5f, 0.5f,  0.5f, },
				{ 0.5f,  0.5f,  0.5f, },

				{ -0.5f, 0.5f, -0.5f, },
				{ 0.5f,  0.5f,  0.5f, },
				{ 0.5f,  0.5f, -0.5f, },

				// Back
				{ 0.5f, -0.5f, 0.5f, },
				{ 0.5f,  0.5f, 0.5f, },
				{ -0.5f,  0.5f, 0.5f, },

				{ 0.5f, -0.5f, 0.5f, },
				{ -0.5f, 0.5f, 0.5f, },
				{ -0.5f, -0.5f, 0.5f, },

				// Bottom
				{ -0.5f, -0.5f, -0.5f, },
				{ 0.5f, -0.5f, -0.5f, },
				{ 0.5f,  -0.5f,  0.5f, },

				{ -0.5f, -0.5f, -0.5f, },
				{ 0.5f, -0.5f,  0.5f, },
				{ -0.5f, -0.5f,  0.5f, },

				// Right
				{ 0.5f, -0.5f, -0.5f, },
				{ 0.5f,  0.5f, -0.5f, },
				{ 0.5f,  0.5f,  0.5f, },

				{ 0.5f, -0.5f, -0.5f, },
				{ 0.5f,  0.5f,  0.5f, },
				{ 0.5f, -0.5f,  0.5f, },

				// Left
				{ -0.5f, -0.5f, -0.5f, },
				{ -0.5f,  0.5f,  0.5f, },
				{ -0.5f,  0.5f, -0.5f, },

				{ -0.5f, -0.5f, -0.5f, },
				{ -0.5f, -0.5f,  0.5f, },
				{ -0.5f,  0.5f,  0.5f, },
			};
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::POSITION;

			renderObjectCreateInfo.cullFace = CullFace::FRONT;
		} break;
		default:
		{
			PrintWarn("Unhandled prefab shape passed to MeshComponent::LoadPrefabShape: %i\n", (i32)shape);
			return false;
		} break;
		}

		if (!vertexBufferDataCreateInfo.positions_3D.empty())
		{
			m_MinPoint = glm::vec3(FLT_MAX);
			m_MaxPoint = glm::vec3(-FLT_MAX);

			for (const glm::vec3& pos : vertexBufferDataCreateInfo.positions_3D)
			{
				m_MinPoint = glm::min(m_MinPoint, pos);
				m_MaxPoint = glm::max(m_MaxPoint, pos);
			}

			m_BoundingSphereCenterPoint = m_MinPoint + (m_MaxPoint - m_MinPoint) / 2.0f;

			for (const glm::vec3& pos : vertexBufferDataCreateInfo.positions_3D)
			{
				real posMagnitude = glm::length(pos - m_BoundingSphereCenterPoint);
				if (posMagnitude > m_BoundingSphereRadius)
				{
					m_BoundingSphereRadius = posMagnitude;
				}
			}
			if (m_BoundingSphereRadius == 0.0f)
			{
				PrintWarn("Mesh's bounding sphere's radius is 0, do any valid vertices exist?\n");
			}
		}

		m_VertexBufferData.Initialize(&vertexBufferDataCreateInfo);

		renderObjectCreateInfo.vertexBufferData = &m_VertexBufferData;
		renderObjectCreateInfo.indices = &m_Indices;

		RenderID renderID = g_Renderer->InitializeRenderObject(&renderObjectCreateInfo);
		if (m_OwningGameObject->GetRenderID() != InvalidRenderID)
		{
			g_Renderer->DestroyRenderObject(m_OwningGameObject->GetRenderID());
		}
		m_OwningGameObject->SetRenderID(renderID);

		g_Renderer->SetTopologyMode(renderID, topologyMode);
		m_VertexBufferData.DescribeShaderVariables(g_Renderer, renderID);

		m_bInitialized = true;

		return true;
	}

	bool MeshComponent::CreateProcedural(u32 initialMaxVertCount,
		VertexAttributes attributes,
		TopologyMode topologyMode /* = TopologyMode::TRIANGLE_LIST */,
		RenderObjectCreateInfo* optionalCreateInfo /* = nullptr */)
	{
		assert(m_VertexBufferData.vertexData == nullptr);

		m_VertexBufferData.InitializeDynamic(attributes, initialMaxVertCount);

		RenderObjectCreateInfo renderObjectCreateInfo = {};

		if (optionalCreateInfo)
		{
			CopyInOptionalCreateInfo(renderObjectCreateInfo, *optionalCreateInfo);
		}

		renderObjectCreateInfo.gameObject = m_OwningGameObject;
		renderObjectCreateInfo.vertexBufferData = &m_VertexBufferData;
		renderObjectCreateInfo.indices = &m_Indices;
		renderObjectCreateInfo.materialID = m_MaterialID;

		if (m_OwningGameObject->GetRenderID() != InvalidRenderID)
		{
			g_Renderer->DestroyRenderObject(m_OwningGameObject->GetRenderID());
		}
		RenderID renderID = g_Renderer->InitializeRenderObject(&renderObjectCreateInfo);
		m_OwningGameObject->SetRenderID(renderID);

		g_Renderer->SetTopologyMode(renderID, topologyMode);

		m_VertexBufferData.DescribeShaderVariables(g_Renderer, renderID);

		m_bInitialized = true;

		return true;
	}

	void MeshComponent::Reload()
	{
		if (m_Type != Type::FILE)
		{
			PrintWarn("Prefab reloading is unsupported\n");
			return;
		}

		GameObject* owningGameObject = m_OwningGameObject;

		RenderObjectCreateInfo renderObjectCreateInfo;
		g_Renderer->GetRenderObjectCreateInfo(m_OwningGameObject->GetRenderID(), renderObjectCreateInfo);

		// These fields can't be passed in, make it clear we aren't trying to
		renderObjectCreateInfo.vertexBufferData = nullptr;
		renderObjectCreateInfo.indices = nullptr;

		Destroy();

		SetOwner(owningGameObject);

		if (!LoadFromFile(m_RelativeFilePath, &m_ImportSettings, &renderObjectCreateInfo))
		{
			PrintError("Failed to reload mesh at %s\n", m_RelativeFilePath.c_str());
		}

		g_Renderer->RenderObjectStateChanged();
	}

	MaterialID MeshComponent::GetMaterialID() const
	{
		return m_MaterialID;
	}

	void MeshComponent::SetMaterialID(MaterialID materialID)
	{
		if (m_MaterialID != materialID)
		{
			m_MaterialID = materialID;
			if (m_bInitialized && m_OwningGameObject)
			{
				g_Renderer->SetRenderObjectMaterialID(m_OwningGameObject->GetRenderID(), materialID);
			}
			g_Renderer->RenderObjectStateChanged();
		}
	}

	void MeshComponent::SetUVScale(real uScale, real vScale)
	{
		m_UVScale = glm::vec2(uScale, vScale);
	}

	MeshComponent::PrefabShape MeshComponent::StringToPrefabShape(const std::string& prefabName)
	{
		if (prefabName.compare("cube") == 0)
		{
			return PrefabShape::CUBE;
		}
		else if (prefabName.compare("grid") == 0)
		{
			return PrefabShape::GRID;
		}
		else if (prefabName.compare("world axis ground") == 0)
		{
			return PrefabShape::WORLD_AXIS_GROUND;
		}
		else if (prefabName.compare("plane") == 0)
		{
		return PrefabShape::PLANE;
		}
		else if (prefabName.compare("uv sphere") == 0)
		{
		return PrefabShape::UV_SPHERE;
		}
		else if (prefabName.compare("skybox") == 0)
		{
		return PrefabShape::SKYBOX;
		}
		else
		{
		PrintError("Unhandled prefab shape string: %s\n", prefabName.c_str());
		return PrefabShape::_NONE;
		}

	}

	// TODO: Move to string array
	std::string MeshComponent::PrefabShapeToString(PrefabShape shape)
	{
		switch (shape)
		{
		case MeshComponent::PrefabShape::CUBE:				return "cube";
		case MeshComponent::PrefabShape::GRID:				return "grid";
		case MeshComponent::PrefabShape::WORLD_AXIS_GROUND:	return "world axis ground";
		case MeshComponent::PrefabShape::PLANE:				return "plane";
		case MeshComponent::PrefabShape::UV_SPHERE:			return "uv sphere";
		case MeshComponent::PrefabShape::SKYBOX:			return "skybox";
		case MeshComponent::PrefabShape::GERSTNER_PLANE:	return "gerstner plane";
		case MeshComponent::PrefabShape::_NONE:				return "NONE";
		default:											return "UNHANDLED PREFAB SHAPE";
		}
	}

	bool MeshComponent::FindPreLoadedMesh(const std::string& relativeFilePath, LoadedMesh** loadedMesh)
	{
		auto iter = m_LoadedMeshes.find(relativeFilePath);
		if (iter == m_LoadedMeshes.end())
		{
			return false;
		}
		else
		{
			*loadedMesh = iter->second;
			return true;
		}
	}

	LoadedMesh* MeshComponent::LoadMesh(const std::string& relativeFilePath, MeshImportSettings* importSettings /* = nullptr */)
	{
		if (relativeFilePath.find(':') != std::string::npos)
		{
			PrintError("Called MeshComponent::LoadMesh with an absolute file path! Filepath must be relative. %s\n", relativeFilePath.c_str());
			return nullptr;
		}

		if (!EndsWith(relativeFilePath, "gltf") && !EndsWith(relativeFilePath, "glb"))
		{
			PrintWarn("Attempted to load non gltf/glb model! Only those formats are supported! %s\n", relativeFilePath.c_str());
			return nullptr;
		}

		const std::string fileName = StripLeadingDirectories(relativeFilePath);

		LoadedMesh* mesh = nullptr;
		bool bNewMesh = true;
		{
			auto existingIter = m_LoadedMeshes.find(relativeFilePath);
			if (existingIter == m_LoadedMeshes.end())
			{
				if (g_bEnableLogging_Loading)
				{
					Print("Loading mesh %s\n", fileName.c_str());
				}
				mesh = new LoadedMesh();
			}
			else
			{
				if (g_bEnableLogging_Loading)
				{
					Print("Reloading mesh %s\n", fileName.c_str());
				}
				mesh = existingIter->second;
				bNewMesh = false;
			}
		}

		// If import settings were passed in, save them in the cached mesh
		if (importSettings)
		{
			mesh->importSettings = *importSettings;
		}

		mesh->relativeFilePath = relativeFilePath;

		auto cleanup = [&](const std::string& errorMessage)
		{
			Print("%s", errorMessage.c_str());
			auto existingIter = m_LoadedMeshes.find(relativeFilePath);
			if (existingIter != m_LoadedMeshes.end())
			{
				m_LoadedMeshes.erase(existingIter);
			}
			delete mesh;
		};

		std::string outErrorMessage;

		cgltf_options ops = {};
		ops.type = cgltf_file_type_invalid; // auto detect gltf or glb
		cgltf_data* data = nullptr;

		cgltf_result result = cgltf_parse_file(&ops, relativeFilePath.c_str(), &data);
		if (!CheckCGLTFResult(result, outErrorMessage))
		{
			cleanup("Failed to parse gltf/glb file at " + relativeFilePath + " with error:\n\t" + outErrorMessage + "\n");
			return nullptr;
		}
		
		result = cgltf_load_buffers(&ops, data, relativeFilePath.c_str());
		if (!CheckCGLTFResult(result, outErrorMessage))
		{
			cleanup("Failed to load gltf/glb file at " + relativeFilePath + " with error:\n\t" + outErrorMessage + "\n");
			return nullptr;
		}
		
		result = cgltf_validate(data);
		if (!CheckCGLTFResult(result, outErrorMessage))
		{
			cleanup("Failed to validate gltf/glb file at " + relativeFilePath + " with error:\n\t" + outErrorMessage + "\n");
			return nullptr;
		}

		else
		{
			mesh->data = data;

			if (bNewMesh)
			{
				m_LoadedMeshes.emplace(relativeFilePath, mesh);
			}
		}

		return mesh;
	}

	bool MeshComponent::CheckCGLTFResult(cgltf_result result, std::string& outErrorMessage)
	{
		if (result != cgltf_result_success)
		{
			switch (result)
			{
			case cgltf_result_data_too_short:	outErrorMessage = "Data too short"; break;
			case cgltf_result_unknown_format:	outErrorMessage = "Unknown format"; break;
			case cgltf_result_invalid_json:		outErrorMessage = "Invalid json"; break;
			case cgltf_result_invalid_gltf:		outErrorMessage = "Invalid gltf"; break;
			case cgltf_result_invalid_options:	outErrorMessage = "Invalid options"; break;
			case cgltf_result_file_not_found:	outErrorMessage = "File not found"; break;
			case cgltf_result_io_error:			outErrorMessage = "IO error"; break;
			case cgltf_result_out_of_memory:	outErrorMessage = "Out of memory"; break;
			default:							outErrorMessage = ""; break;
			}
			return false;
		}
		return true;
	}

	MeshComponent::Type MeshComponent::GetType() const
	{
		return m_Type;
	}

	std::string MeshComponent::GetRelativeFilePath() const
	{
		return m_RelativeFilePath;
	}

	std::string MeshComponent::GetFileName() const
	{
		return m_FileName;
	}

	MeshComponent::PrefabShape MeshComponent::GetShape() const
	{
		return m_Shape;
	}

	MeshImportSettings MeshComponent::GetImportSettings() const
	{
		return m_ImportSettings;
	}

	real MeshComponent::GetScaledBoundingSphereRadius() const
	{
		if (!(m_VertexBufferData.Attributes & (i32)VertexAttribute::POSITION))
		{
			PrintError("Attempted to get bounding sphere radius of mesh component which contains no 3D vertices!"
				"Radius will always be 0\n");
		}
		real sphereScale = CalculateBoundingSphereScale();
		return m_BoundingSphereRadius * sphereScale;
	}

	glm::vec3 MeshComponent::GetBoundingSphereCenterPointWS() const
	{
		if (!(m_VertexBufferData.Attributes & (i32)VertexAttribute::POSITION))
		{
			PrintError("Attempted to get bounding sphere center point of mesh component which contains no 3D vertices!"
				"Center point will always be 0\n");
		}

		Transform* transform = m_OwningGameObject->GetTransform();
		glm::vec3 transformedCenter = transform->GetWorldTransform() * glm::vec4(m_BoundingSphereCenterPoint, 1.0f);
		return transformedCenter;
	}

	VertexBufferData* MeshComponent::GetVertexBufferData()
	{
		return &m_VertexBufferData;
	}

	real MeshComponent::CalculateBoundingSphereScale() const
	{
		Transform* transform = m_OwningGameObject->GetTransform();
		glm::vec3 scale = transform->GetWorldScale();
		glm::vec3 scaledMin = scale * m_MinPoint;
		glm::vec3 scaledMax = scale * m_MaxPoint;

		real sphereScale = (glm::max(glm::max(glm::abs(scaledMax.x), glm::abs(scaledMin.x)),
			glm::max(glm::max(glm::abs(scaledMax.y), glm::abs(scaledMin.y)),
				glm::max(glm::abs(scaledMax.z), glm::abs(scaledMin.z)))));
		return sphereScale;
	}

	bool MeshComponent::CalculateTangents(VertexBufferDataCreateInfo& createInfo)
	{
		if (createInfo.normals.empty())
		{
			PrintError("Can't calculate tangents for mesh which contains no normal data!\n");
			return false;
		}
		if (createInfo.positions_3D.empty())
		{
			PrintError("Can't calculate tangents for mesh which contains no position data!\n");
			return false;
		}
		if (createInfo.texCoords_UV.empty())
		{
			// TODO: Handle this case
			PrintError("Can't calculate tangents for mesh which contains no tex coord data!\n");
			return false;
		}

		for (u32 i = 0; i < createInfo.positions_3D.size() - 2; i += 3)
		{
			glm::vec3 p0 = createInfo.positions_3D[i + 0];
			glm::vec3 p1 = createInfo.positions_3D[i + 1];
			glm::vec3 p2 = createInfo.positions_3D[i + 2];

			glm::vec2 uv0 = createInfo.texCoords_UV[i + 0];
			glm::vec2 uv1 = createInfo.texCoords_UV[i + 1];
			glm::vec2 uv2 = createInfo.texCoords_UV[i + 2];

			glm::vec3 dPos0 = p1 - p0;
			glm::vec3 dPos1 = p2 - p0;

			glm::vec2 dUV0 = uv1 - uv0;
			glm::vec2 dUV1 = uv2 - uv0;

			real r = 1.0f / (dUV1.x * dUV0.y - dUV1.y * dUV0.x);
			glm::vec3 tangent = glm::normalize((dPos0 * dUV0.y - dPos1 * dUV1.y) * r);
			//glm::vec3 bitangent = (dPos1 * dUV1.x - dPos0 * dUV0.x) * r;

			createInfo.tangents[i + 0] = tangent;
			createInfo.tangents[i + 1] = tangent;
			createInfo.tangents[i + 2] = tangent;
		}

		return true;
	}

	void MeshComponent::CopyInOptionalCreateInfo(RenderObjectCreateInfo& createInfo, const RenderObjectCreateInfo& overrides)
	{
		if (overrides.materialID != InvalidMaterialID)
		{
			m_MaterialID = overrides.materialID;
		}
		createInfo.visibleInSceneExplorer = overrides.visibleInSceneExplorer;
		createInfo.cullFace = overrides.cullFace;
		createInfo.depthTestReadFunc = overrides.depthTestReadFunc;
		createInfo.bDepthWriteEnable = overrides.bDepthWriteEnable;
		createInfo.bEditorObject = overrides.bEditorObject;
		createInfo.bSetDynamicStates = overrides.bSetDynamicStates;

		if (overrides.vertexBufferData != nullptr)
		{
			PrintWarn("Attempted to override vertexBufferData in LoadFromFile! Ignoring passed in data\n");
		}
		if (overrides.indices != nullptr)
		{
			PrintWarn("Attempted to override indices in LoadFromFile! Ignoring passed in data\n");
		}
	}
} // namespace flex
