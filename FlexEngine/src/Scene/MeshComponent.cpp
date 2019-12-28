#include "stdafx.hpp"

#include "Scene/MeshComponent.hpp"

#include "Scene/LoadedMesh.hpp"
#include "Scene/Mesh.hpp"

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
#include "Transform.hpp"

namespace flex
{
	const real MeshComponent::GRID_LINE_SPACING = 1.0f;
	const u32 MeshComponent::GRID_LINE_COUNT = 151; // Keep odd to align with origin

	glm::vec4 MeshComponent::m_DefaultColor_4(1.0f, 1.0f, 1.0f, 1.0f);
	glm::vec3 MeshComponent::m_DefaultPosition(0.0f, 0.0f, 0.0f);
	glm::vec3 MeshComponent::m_DefaultTangent(1.0f, 0.0f, 0.0f);
	glm::vec3 MeshComponent::m_DefaultNormal(0.0f, 1.0f, 0.0f);
	glm::vec2 MeshComponent::m_DefaultTexCoord(0.0f, 0.0f);

	MeshComponent::MeshComponent(Mesh* owner, MaterialID materialID /* = InvalidMaterialID */, bool bSetRequiredAttributesFromMat /* = true */) :
		m_OwningMesh(owner),
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
		for (auto& loadedMeshPair : Mesh::m_LoadedMeshes)
		{
			cgltf_free(loadedMeshPair.second->data);
			delete loadedMeshPair.second;
		}
		Mesh::m_LoadedMeshes.clear();
	}

	void MeshComponent::PostInitialize()
	{
		g_Renderer->PostInitializeRenderObject(renderID);
	}

	void MeshComponent::Update()
	{
		// TODO: Move elsewhere
		if (m_Shape == PrefabShape::GRID)
		{
			Transform* transform = m_OwningMesh->GetOwningGameObject()->GetTransform();
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
		g_Renderer->UpdateVertexData(renderID, &m_VertexBufferData);
	}

	void MeshComponent::Destroy()
	{
		g_Renderer->DestroyRenderObject(renderID);
		m_VertexBufferData.Destroy();
		m_OwningMesh = nullptr;
		m_bInitialized = false;
	}

	void MeshComponent::SetOwner(Mesh* owner)
	{
		m_OwningMesh = owner;
	}

	void MeshComponent::SetRequiredAttributesFromMaterialID(MaterialID matID)
	{
		assert(matID != InvalidMaterialID);

		Material& mat = g_Renderer->GetMaterial(matID);
		Shader& shader = g_Renderer->GetShader(mat.shaderID);
		m_RequiredAttributes = shader.vertexAttributes;
	}

	MeshComponent* MeshComponent::LoadFromCGLTF(Mesh* owningMesh,
		cgltf_primitive* primitive,
		MaterialID materialID,
		MeshImportSettings* importSettings /* = nullptr */,
		RenderObjectCreateInfo* optionalCreateInfo /* = nullptr */)
	{
		if (primitive->indices == nullptr)
		{
			return nullptr;
		}

		MeshComponent* newMeshComponent = new MeshComponent(owningMesh, materialID);

		newMeshComponent->m_MinPoint = glm::vec3(FLT_MAX);
		newMeshComponent->m_MaxPoint = glm::vec3(-FLT_MAX);

		VertexBufferDataCreateInfo vertexBufferDataCreateInfo = {};

		bool bCalculateTangents = false;
		bool bMissingTexCoords = false;

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

		newMeshComponent->m_MinPoint = glm::min(newMeshComponent->m_MinPoint, posMin);
		newMeshComponent->m_MaxPoint = glm::max(newMeshComponent->m_MaxPoint, posMax);


		if (tanAttribIndex == -1 && (newMeshComponent->m_RequiredAttributes & (u32)VertexAttribute::TANGENT))
		{
			bCalculateTangents = true;

			if (uvAttribIndex == -1)
			{
				bMissingTexCoords = true;
			}
		}

		{
			if (newMeshComponent->m_RequiredAttributes & (u32)VertexAttribute::POSITION) vertexBufferDataCreateInfo.positions_3D.resize(vertexBufferDataCreateInfo.positions_3D.size() + vertCount);
			if (newMeshComponent->m_RequiredAttributes & (u32)VertexAttribute::POSITION2) vertexBufferDataCreateInfo.positions_2D.resize(vertexBufferDataCreateInfo.positions_2D.size() + vertCount);
			if (newMeshComponent->m_RequiredAttributes & (u32)VertexAttribute::NORMAL) vertexBufferDataCreateInfo.normals.resize(vertexBufferDataCreateInfo.normals.size() + vertCount);
			if (newMeshComponent->m_RequiredAttributes & (u32)VertexAttribute::TANGENT) vertexBufferDataCreateInfo.tangents.resize(vertexBufferDataCreateInfo.tangents.size() + vertCount);
			if (newMeshComponent->m_RequiredAttributes & (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT) vertexBufferDataCreateInfo.colors_R32G32B32A32.resize(vertexBufferDataCreateInfo.colors_R32G32B32A32.size() + vertCount);
			if (newMeshComponent->m_RequiredAttributes & (u32)VertexAttribute::COLOR_R8G8B8A8_UNORM) vertexBufferDataCreateInfo.colors_R8G8B8A8.resize(vertexBufferDataCreateInfo.colors_R8G8B8A8.size() + vertCount);
			if (newMeshComponent->m_RequiredAttributes & (u32)VertexAttribute::UV) vertexBufferDataCreateInfo.texCoords_UV.resize(vertexBufferDataCreateInfo.texCoords_UV.size() + vertCount);
			if (newMeshComponent->m_RequiredAttributes & (u32)VertexAttribute::EXTRA_VEC4) vertexBufferDataCreateInfo.extraVec4s.resize(vertexBufferDataCreateInfo.extraVec4s.size() + vertCount);
			if (newMeshComponent->m_RequiredAttributes & (u32)VertexAttribute::EXTRA_INT) vertexBufferDataCreateInfo.extraInts.resize(vertexBufferDataCreateInfo.extraInts.size() + vertCount);
		}

		// Vertices
		for (u32 vi = 0; vi < vertCount; ++vi)
		{
			// Position
			glm::vec3 pos;
			cgltf_accessor_read_float(posAccessor, vi, &pos.x, 3);
			vertexBufferDataCreateInfo.positions_3D[vi] = pos;

			// Normal
			if (newMeshComponent->m_RequiredAttributes & (u32)VertexAttribute::NORMAL)
			{
				vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::NORMAL;

				if (normAttribIndex == -1)
				{
					vertexBufferDataCreateInfo.normals[vi] = m_DefaultNormal;
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
					vertexBufferDataCreateInfo.normals[vi] = norm;
				}
			}

			// Tangent
			if (newMeshComponent->m_RequiredAttributes & (u32)VertexAttribute::TANGENT)
			{
				vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::TANGENT;

				if (tanAttribIndex == -1)
				{
					vertexBufferDataCreateInfo.tangents[vi] = m_DefaultTangent;
				}
				else
				{
					cgltf_accessor* tanAccessor = primitive->attributes[tanAttribIndex].data;
					assert(primitive->attributes[tanAttribIndex].type == cgltf_attribute_type_tangent);
					assert(tanAccessor->component_type == cgltf_component_type_r_32f);
					//assert(tanAccessor->type == cgltf_type_vec3);

					glm::vec4 tangent;
					cgltf_accessor_read_float(tanAccessor, vi, &tangent.x, 4);
					vertexBufferDataCreateInfo.tangents[vi] = tangent;
				}
			}

			// Color
			if (newMeshComponent->m_RequiredAttributes & (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT)
			{
				vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;

				if (colAttribIndex == -1)
				{
					vertexBufferDataCreateInfo.colors_R32G32B32A32[vi] = m_DefaultColor_4;
				}
				else
				{
					cgltf_accessor* colAccessor = primitive->attributes[colAttribIndex].data;
					assert(primitive->attributes[colAttribIndex].type == cgltf_attribute_type_color);
					assert(colAccessor->component_type == cgltf_component_type_r_32f);
					assert(colAccessor->type == cgltf_type_vec4);

					glm::vec4 col;
					cgltf_accessor_read_float(colAccessor, vi, &col.x, 4);
					vertexBufferDataCreateInfo.colors_R32G32B32A32[vi] = col;
				}
			}

			// UV 0
			if (newMeshComponent->m_RequiredAttributes & (u32)VertexAttribute::UV)
			{
				vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::UV;

				if (uvAttribIndex == -1)
				{
					vertexBufferDataCreateInfo.texCoords_UV[vi] = m_DefaultTexCoord;
				}
				else
				{
					cgltf_accessor* uvAccessor = primitive->attributes[uvAttribIndex].data;
					assert(primitive->attributes[uvAttribIndex].type == cgltf_attribute_type_texcoord);
					assert(uvAccessor->component_type == cgltf_component_type_r_32f);
					assert(uvAccessor->type == cgltf_type_vec2);

					glm::vec2 uv0;
					cgltf_accessor_read_float(uvAccessor, vi, &uv0.x, 2);

					uv0 *= newMeshComponent->m_UVScale;
					if (importSettings && importSettings->bFlipU)
					{
						uv0.x = 1.0f - uv0.x;
					}
					if (importSettings && importSettings->bFlipV)
					{
						uv0.y = 1.0f - uv0.y;
					}
					vertexBufferDataCreateInfo.texCoords_UV[vi] = uv0;
				}
			}
		}

		// Indices
		{
			assert(primitive->indices->type == cgltf_type_scalar);
			const i32 indexCount = (i32)primitive->indices->count;
			newMeshComponent->m_Indices.resize(newMeshComponent->m_Indices.size() + indexCount);

			//assert(primitive->indices->buffer_view->type == cgltf_buffer_view_type_indices);
			assert(primitive->indices->component_type == cgltf_component_type_r_8u ||
				primitive->indices->component_type == cgltf_component_type_r_16u ||
				primitive->indices->component_type == cgltf_component_type_r_32u);

			for (i32 l = 0; l < indexCount; ++l)
			{
				newMeshComponent->m_Indices[l] = cgltf_accessor_read_index(primitive->indices, l);
			}
		}

		if (bCalculateTangents)
		{
			if (!newMeshComponent->CalculateTangents(vertexBufferDataCreateInfo, bMissingTexCoords))
			{
				PrintWarn("Failed to calculate tangents for mesh!\n");
			}
		}

		newMeshComponent->CalculateBoundingSphereRadius(vertexBufferDataCreateInfo.positions_3D);

		newMeshComponent->m_VertexBufferData.Initialize(&vertexBufferDataCreateInfo);

		RenderObjectCreateInfo renderObjectCreateInfo = {};

		if (optionalCreateInfo)
		{
			newMeshComponent->CopyInOptionalCreateInfo(renderObjectCreateInfo, *optionalCreateInfo);
		}

		renderObjectCreateInfo.gameObject = owningMesh->GetOwningGameObject();
		renderObjectCreateInfo.vertexBufferData = &newMeshComponent->m_VertexBufferData;
		renderObjectCreateInfo.indices = &newMeshComponent->m_Indices;
		renderObjectCreateInfo.materialID = materialID;

		if (newMeshComponent->renderID != InvalidRenderID)
		{
			g_Renderer->DestroyRenderObject(newMeshComponent->renderID);
		}

		newMeshComponent->renderID = g_Renderer->InitializeRenderObject(&renderObjectCreateInfo);

		g_Renderer->SetTopologyMode(newMeshComponent->renderID, TopologyMode::TRIANGLE_LIST);

		newMeshComponent->m_VertexBufferData.DescribeShaderVariables(g_Renderer, newMeshComponent->renderID);
		newMeshComponent->CalculateBoundingSphereScale();

		newMeshComponent->m_bInitialized = true;

		return newMeshComponent;
	}

	bool MeshComponent::LoadPrefabShape(PrefabShape shape, RenderObjectCreateInfo* optionalCreateInfo)
	{
		if (m_bInitialized)
		{
			PrintError("Attempted to load mesh after already initialized! If reloading, first call Destroy\n");
			return false;
		}

		m_Shape = shape;

		m_VertexBufferData.Destroy();

		RenderObjectCreateInfo renderObjectCreateInfo = {};

		if (optionalCreateInfo)
		{
			CopyInOptionalCreateInfo(renderObjectCreateInfo, *optionalCreateInfo);
		}

		renderObjectCreateInfo.gameObject = m_OwningMesh->GetOwningGameObject();
		renderObjectCreateInfo.materialID = m_MaterialID;

		TopologyMode topologyMode = TopologyMode::TRIANGLE_LIST;

		VertexBufferDataCreateInfo vertexBufferDataCreateInfo = {};

		switch (shape)
		{
		case PrefabShape::CUBE:
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
		case PrefabShape::GRID:
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
		case PrefabShape::WORLD_AXIS_GROUND:
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
		case PrefabShape::PLANE:
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
		case PrefabShape::GERSTNER_PLANE:
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
		case PrefabShape::UV_SPHERE:
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
		case PrefabShape::SKYBOX:
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

		CalculateBoundingSphereRadius(vertexBufferDataCreateInfo.positions_3D);

		m_VertexBufferData.Initialize(&vertexBufferDataCreateInfo);

		renderObjectCreateInfo.vertexBufferData = &m_VertexBufferData;
		renderObjectCreateInfo.indices = &m_Indices;

		renderID = g_Renderer->InitializeRenderObject(&renderObjectCreateInfo);

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

		renderObjectCreateInfo.gameObject = m_OwningMesh->GetOwningGameObject();
		renderObjectCreateInfo.vertexBufferData = &m_VertexBufferData;
		renderObjectCreateInfo.indices = &m_Indices;
		renderObjectCreateInfo.materialID = m_MaterialID;

		renderID = g_Renderer->InitializeRenderObject(&renderObjectCreateInfo);

		g_Renderer->SetTopologyMode(renderID, topologyMode);

		m_VertexBufferData.DescribeShaderVariables(g_Renderer, renderID);

		m_bInitialized = true;

		return true;
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
			if (m_bInitialized)
			{
				g_Renderer->SetRenderObjectMaterialID(renderID, materialID);
			}
			g_Renderer->RenderObjectStateChanged();
		}
	}

	void MeshComponent::SetUVScale(real uScale, real vScale)
	{
		m_UVScale = glm::vec2(uScale, vScale);
	}

	bool MeshComponent::IsInitialized() const
	{
		return m_bInitialized;
	}

	PrefabShape MeshComponent::StringToPrefabShape(const std::string& prefabName)
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
		case PrefabShape::CUBE:				return "cube";
		case PrefabShape::GRID:				return "grid";
		case PrefabShape::WORLD_AXIS_GROUND:	return "world axis ground";
		case PrefabShape::PLANE:				return "plane";
		case PrefabShape::UV_SPHERE:			return "uv sphere";
		case PrefabShape::SKYBOX:			return "skybox";
		case PrefabShape::GERSTNER_PLANE:	return "gerstner plane";
		case PrefabShape::_NONE:				return "NONE";
		default:											return "UNHANDLED PREFAB SHAPE";
		}
	}

	PrefabShape MeshComponent::GetShape() const
	{
		return m_Shape;
	}

	Mesh* MeshComponent::GetOwner()
	{
		return m_OwningMesh;
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

		Transform* transform = m_OwningMesh->GetOwningGameObject()->GetTransform();
		glm::vec3 transformedCenter = transform->GetWorldTransform() * glm::vec4(m_BoundingSphereCenterPoint, 1.0f);
		return transformedCenter;
	}

	VertexBufferData* MeshComponent::GetVertexBufferData()
	{
		return &m_VertexBufferData;
	}

	real MeshComponent::CalculateBoundingSphereScale() const
	{
		Transform* transform = m_OwningMesh->GetOwningGameObject()->GetTransform();
		glm::vec3 scale = transform->GetWorldScale();
		glm::vec3 scaledMin = scale * m_MinPoint;
		glm::vec3 scaledMax = scale * m_MaxPoint;

		real sphereScale = (glm::max(glm::max(glm::abs(scaledMax.x), glm::abs(scaledMin.x)),
			glm::max(glm::max(glm::abs(scaledMax.y), glm::abs(scaledMin.y)),
				glm::max(glm::abs(scaledMax.z), glm::abs(scaledMin.z)))));
		return sphereScale;
	}

	bool MeshComponent::CalculateTangents(VertexBufferDataCreateInfo& createInfo, bool bMissingTexCoords)
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

		if (!bMissingTexCoords)
		{
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
		}
		else
		{
			for (u32 i = 0; i < createInfo.positions_3D.size() - 2; i += 3)
			{
				glm::vec3 p0 = createInfo.positions_3D[i + 0];
				glm::vec3 p1 = createInfo.positions_3D[i + 1];
				glm::vec3 p2 = createInfo.positions_3D[i + 2];

				glm::vec3 dPos0 = p1 - p0;
				glm::vec3 dPos1 = p2 - p0;

				glm::vec3 tangent = glm::normalize(dPos0 - dPos1);
				//glm::vec3 bitangent = (dPos1 * dUV1.x - dPos0 * dUV0.x) * r;

				createInfo.tangents[i + 0] = tangent;
				createInfo.tangents[i + 1] = tangent;
				createInfo.tangents[i + 2] = tangent;
			}
		}

		return true;
	}

	void MeshComponent::CalculateBoundingSphereRadius(const std::vector<glm::vec3>& positions)
	{
		if (!positions.empty())
		{
			m_MinPoint = glm::vec3(FLT_MAX);
			m_MaxPoint = glm::vec3(-FLT_MAX);

			for (const glm::vec3& pos : positions)
			{
				m_MinPoint = glm::min(m_MinPoint, pos);
				m_MaxPoint = glm::max(m_MaxPoint, pos);
			}

			m_BoundingSphereCenterPoint = m_MinPoint + (m_MaxPoint - m_MinPoint) / 2.0f;

			for (const glm::vec3& pos : positions)
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
			PrintWarn("Attempted to override vertexBufferData! Ignoring passed in data\n");
		}
		if (overrides.indices != nullptr)
		{
			PrintWarn("Attempted to override indices! Ignoring passed in data\n");
		}
	}
} // namespace flex
