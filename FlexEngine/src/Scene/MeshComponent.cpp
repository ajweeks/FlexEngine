#include "stdafx.hpp"

#include "Scene/MeshComponent.hpp"

// Must be included above tiny_gltf
#include "Scene/LoadedMesh.hpp"

IGNORE_WARNINGS_PUSH
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf/tiny_gltf.h>

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
	glm::vec3 MeshComponent::m_DefaultBitangent(0.0f, 0.0f, 1.0f);
	glm::vec3 MeshComponent::m_DefaultNormal(0.0f, 1.0f, 0.0f);
	glm::vec2 MeshComponent::m_DefaultTexCoord(0.0f, 0.0f);

	MeshComponent::MeshComponent(GameObject* owner) :
		m_OwningGameObject(owner)
	{
	}

	MeshComponent::MeshComponent(MaterialID materialID, GameObject* owner, bool bSetRequiredAttributesFromMat /* = true */) :
		m_OwningGameObject(owner),
		m_MaterialID(materialID),
		m_UVScale(1.0f, 1.0f)
	{
		if (bSetRequiredAttributesFromMat)
		{
			SetRequiredAttributesFromMaterialID(materialID);
		}
	}

	MeshComponent::~MeshComponent()
	{
	}

	void MeshComponent::DestroyAllLoadedMeshes()
	{
		for (auto& loadedMeshPair : m_LoadedMeshes)
		{
			SafeDelete(loadedMeshPair.second);
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
		bool swapNormalYZ = object.GetBool("swapNormalYZ");
		bool flipNormalZ = object.GetBool("flipNormalZ");
		bool flipU = object.GetBool("flipU");
		bool flipV = object.GetBool("flipV");

		if (materialID == InvalidMaterialID)
		{
			PrintError("Mesh component requires material index to be parsed: %s\n", owner->GetName().c_str());
		}
		else
		{
			if (!meshFilePath.empty())
			{
				newMeshComponent = new MeshComponent(materialID, owner);

				MeshImportSettings importSettings = {};
				importSettings.flipU = flipU;
				importSettings.flipV = flipV;
				importSettings.flipNormalZ = flipNormalZ;
				importSettings.swapNormalYZ = swapNormalYZ;

				newMeshComponent->LoadFromFile(meshFilePath, &importSettings);

				owner->SetMeshComponent(newMeshComponent);
			}
			else if (!meshPrefabName.empty())
			{
				newMeshComponent = new MeshComponent(materialID, owner);

				MeshComponent::PrefabShape prefabShape = MeshComponent::StringToPrefabShape(meshPrefabName);
				newMeshComponent->LoadPrefabShape(prefabShape);

				owner->SetMeshComponent(newMeshComponent);
			}
			else
			{
				PrintError("Unhandled mesh field on object: %s\n", owner->GetName().c_str());
			}
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
		meshObject.fields.emplace_back("swapNormalYZ", JSONValue(importSettings.swapNormalYZ));
		meshObject.fields.emplace_back("flipNormalZ", JSONValue(importSettings.flipNormalZ));
		meshObject.fields.emplace_back("flipU", JSONValue(importSettings.flipU));
		meshObject.fields.emplace_back("flipV", JSONValue(importSettings.flipV));

		return meshObject;
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

	bool MeshComponent::GetLoadedMesh(const std::string& relativeFilePath, LoadedMesh** loadedMesh)
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

		std::string fileName = relativeFilePath;
		StripLeadingDirectories(fileName);

		LoadedMesh* newLoadedMesh = nullptr;
		{
			auto existingIter = m_LoadedMeshes.find(relativeFilePath);
			if (existingIter == m_LoadedMeshes.end())
			{
				Print("Loading mesh %s\n", fileName.c_str());
				newLoadedMesh = new LoadedMesh();
				m_LoadedMeshes.emplace(relativeFilePath, newLoadedMesh);
			}
			else
			{
				Print("Reloading mesh %s\n", fileName.c_str());
				newLoadedMesh = existingIter->second;
			}
		}

		// If import settings were passed in, save them in the cached mesh
		if (importSettings)
		{
			newLoadedMesh->importSettings = *importSettings;
		}

		newLoadedMesh->relativeFilePath = relativeFilePath;
		std::string err, warn;
		bool success = false;
		if (EndsWith(relativeFilePath, "glb"))
		{
			success = newLoadedMesh->loader.LoadBinaryFromFile(&newLoadedMesh->model, &err, &warn, relativeFilePath);
		}
		else if (EndsWith(relativeFilePath, "gltf"))
		{
			success = newLoadedMesh->loader.LoadASCIIFromFile(&newLoadedMesh->model, &err, &warn, relativeFilePath);
		}
		else
		{
			PrintWarn("Attempted to load non gltf/glb model! Only those formats are supported! %s\n", relativeFilePath.c_str());
		}

		if (!success)
		{
			PrintWarn("Failed to load gltf/glb file at: %s\nerr: %s\nwarn: %s\n", relativeFilePath.c_str(), err.c_str(), warn.c_str());
		}

		return newLoadedMesh;
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

	bool MeshComponent::CalculateTangents(VertexBufferData::CreateInfo& createInfo)
	{
		if (createInfo.normals.empty())
		{
			PrintError("Can't calculate tangents and bitangents for mesh which contains no normals!\n");
			return false;
		}

		//const real angleEpsilon = 0.9999f;
		const i32 vertCount = (i32)createInfo.normals.size();

		if (!createInfo.tangents.empty() ||
			createInfo.bitangents.empty())
		{
			// Calculate bitangents from cross of normal with tangents!
			createInfo.bitangents.reserve(vertCount);

			for (i32 i = 0; i < vertCount; ++i)
			{
				createInfo.bitangents[i] = glm::normalize(glm::cross(createInfo.normals[i], createInfo.tangents[i]));
			}

			return true;
		}

		ENSURE_NO_ENTRY();

		// TODO: Implement?

		//if (createInfo.texCoords_UV.empty())
		//{
		//	PrintError("Can't calculate tangents and bitangents for mesh which contains no texture coordinates!\n");
		//	return false;
		//}

		//if (!createInfo.tangents.empty() ||
		//	!createInfo.bitangents.empty())
		//{
		//	PrintWarn("Attempted to calculate tangents and bitangents on mesh which already has tangents or bitangents\n");
		//}

		//createInfo.tangents.reserve(vertCount);
		//createInfo.bitangents.reserve(vertCount);

		//const glm::vec3* meshPos = &createInfo.positions_3D[0];
		//const glm::vec3* meshNorm = &createInfo.normals[0];
		//const glm::vec2* meshUV = &createInfo.texCoords_UV[0];
		//glm::vec3* meshTan = &createInfo.tangents[0];
		//glm::vec3* meshBitan = &createInfo.bitangents[0];

		//assert(primitive.mode == TINYGLTF_MODE_TRIANGLES);

		//i32 numFaces = vertCount / 3;
		//for (i32 i = 0; i < numFaces; ++i)
		//{

		//}

		return false;
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

		m_Type = Type::FILE;
		m_Shape = PrefabShape::_NONE;
		m_RelativeFilePath = relativeFilePath;
		m_FileName = m_RelativeFilePath;
		StripLeadingDirectories(m_FileName);

		m_BoundingSphereRadius = 0;
		m_BoundingSphereCenterPoint = VEC3_ZERO;
		m_VertexBufferData.Destroy();

		LoadedMesh* loadedMesh = nullptr;
		if (GetLoadedMesh(relativeFilePath, &loadedMesh))
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

		// Convert data from tinygltf layout into vertex & index buffers
		{
			tinygltf::Model* model = &loadedMesh->model;
			if (model->meshes.empty())
			{
				PrintError("Loaded mesh file has no meshes\n");
				return false;
			}

			VertexBufferData::CreateInfo vertexBufferDataCreateInfo = {};

			//size_t totalVertCount = 0;
			m_MinPoint = glm::vec3(FLT_MAX);
			m_MaxPoint = glm::vec3(FLT_MIN);

			for (const tinygltf::Mesh& mesh : model->meshes)
			{
				for (const tinygltf::Primitive& primitive : mesh.primitives)
				{
					if (primitive.indices < 0)
					{
						continue;
					}

					i32 indexStart = (i32)m_Indices.size();
					//i32 vertexStart = (i32)vertexBufferDataCreateInfo.positions_3D.size();

					const float* posBuffer = nullptr;
					const float* normBuffer = nullptr;
					const float* bitanBuffer = nullptr;
					const float* tanBuffer = nullptr;
					const float* colBuffer = nullptr;
					const float* uv0Buffer = nullptr;

					bool bCalculateTangents = false;

					const std::map<std::string, i32>& attribs = primitive.attributes;
					auto posIter = attribs.find("POSITION");
					assert(posIter != attribs.end());
					const tinygltf::Accessor& posAccessor = model->accessors[posIter->second];
					const tinygltf::BufferView& posView = model->bufferViews[posAccessor.bufferView];
					posBuffer = reinterpret_cast<const float*>(&(model->buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]));
					vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::POSITION;

					glm::vec3 posMin = glm::vec3(posAccessor.minValues[0], posAccessor.minValues[1], posAccessor.minValues[2]);
					glm::vec3 posMax = glm::vec3(posAccessor.maxValues[0], posAccessor.maxValues[1], posAccessor.maxValues[2]);

					m_MinPoint = glm::min(m_MinPoint, posMin);
					m_MaxPoint = glm::max(m_MaxPoint, posMax);

					auto normIter = attribs.find("NORMAL");
					if (normIter != attribs.end())
					{
						const tinygltf::Accessor& normAccessor = model->accessors[normIter->second];
						const tinygltf::BufferView& normView = model->bufferViews[normAccessor.bufferView];
						normBuffer = reinterpret_cast<const float*>(&(model->buffers[normView.buffer].data[normAccessor.byteOffset + normView.byteOffset]));
					}

					auto tanIter = attribs.find("TANGENT");
					if (tanIter != attribs.end())
					{
						const tinygltf::Accessor& tanAccessor = model->accessors[tanIter->second];
						const tinygltf::BufferView& tanView = model->bufferViews[tanAccessor.bufferView];
						tanBuffer = reinterpret_cast<const float*>(&(model->buffers[tanView.buffer].data[tanAccessor.byteOffset + tanView.byteOffset]));
					}
					else
					{
						if (m_RequiredAttributes & (u32)VertexAttribute::TANGENT)
						{
							bCalculateTangents = true;
						}
					}

					auto bitanIter = attribs.find("BITANGENT");
					if (bitanIter != attribs.end())
					{
						const tinygltf::Accessor& bitanAccessor = model->accessors[bitanIter->second];
						const tinygltf::BufferView& bitanView = model->bufferViews[bitanAccessor.bufferView];
						bitanBuffer = reinterpret_cast<const float*>(&(model->buffers[bitanView.buffer].data[bitanAccessor.byteOffset + bitanView.byteOffset]));
					}
					else
					{
						if (m_RequiredAttributes & (u32)VertexAttribute::BITANGENT)
						{
							bCalculateTangents = true;
						}
					}

					auto colIter = attribs.find("COLOR_0");
					if (colIter != attribs.end())
					{
						const tinygltf::Accessor& colAccessor = model->accessors[colIter->second];
						const tinygltf::BufferView& colView = model->bufferViews[colAccessor.bufferView];
						colBuffer = reinterpret_cast<const float*>(&(model->buffers[colView.buffer].data[colAccessor.byteOffset + colView.byteOffset]));
					}

					auto uv0Iter = attribs.find("TEXCOORD_0");
					if (uv0Iter != attribs.end())
					{
						const tinygltf::Accessor& uv0Accessor = model->accessors[uv0Iter->second];
						const tinygltf::BufferView& uv0View = model->bufferViews[uv0Accessor.bufferView];
						uv0Buffer = reinterpret_cast<const float*>(&(model->buffers[uv0View.buffer].data[uv0Accessor.byteOffset + uv0View.byteOffset]));
					}

					//totalVertCount += posAccessor.count;

					// Vertices
					u32 vertCount = posAccessor.count;
					for (u32 i = 0; i < vertCount; ++i)
					{
						// Position
						glm::vec3 pos = glm::make_vec3(&posBuffer[i * 3]);
						vertexBufferDataCreateInfo.positions_3D.push_back(pos);

						// Normal
						if ((m_RequiredAttributes &&
							(m_RequiredAttributes & (u32)VertexAttribute::NORMAL)) ||
							(!m_RequiredAttributes && normBuffer != nullptr))
						{
							vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::NORMAL;

							if (normBuffer == nullptr)
							{
								vertexBufferDataCreateInfo.normals.push_back(m_DefaultNormal);
							}
							else
							{
								glm::vec3 norm = glm::make_vec3(&normBuffer[i * 3]);
								if (importSettings && importSettings->swapNormalYZ)
								{
									std::swap(norm.y, norm.z);
								}
								if (importSettings && importSettings->flipNormalZ)
								{
									norm.z = -norm.z;
								}
								vertexBufferDataCreateInfo.normals.push_back(norm);
							}
						}

						// Tangent
						if ((m_RequiredAttributes &&
							(m_RequiredAttributes & (u32)VertexAttribute::TANGENT)) ||
							(!m_RequiredAttributes && tanBuffer != nullptr))
						{
							vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::TANGENT;

							if (tanBuffer == nullptr)
							{
								vertexBufferDataCreateInfo.tangents.push_back(m_DefaultTangent);
							}
							else
							{
								glm::vec3 tangent = glm::make_vec3(&tanBuffer[i * 3]);
								vertexBufferDataCreateInfo.tangents.push_back(tangent);
							}
						}

						// Bitangent
						if ((m_RequiredAttributes &&
							(m_RequiredAttributes & (u32)VertexAttribute::BITANGENT)) ||
							(!m_RequiredAttributes && bitanBuffer != nullptr))
						{
							vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::BITANGENT;

							if (bitanBuffer == nullptr)
							{
								vertexBufferDataCreateInfo.bitangents.push_back(m_DefaultBitangent);
							}
							else
							{
								glm::vec3 bitangent = glm::make_vec3(&bitanBuffer[i * 3]);
								vertexBufferDataCreateInfo.bitangents.push_back(bitangent);
							}
						}

						// Color
						if ((m_RequiredAttributes &&
							(m_RequiredAttributes & (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT)) ||
							(!m_RequiredAttributes && colBuffer != nullptr))
						{
							vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;

							if (colBuffer == nullptr)
							{
								vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(m_DefaultColor_4);
							}
							else
							{
								glm::vec4 col = glm::make_vec4(&colBuffer[i * 4]);
								vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(col);
							}
						}

						// UV 0
						if ((m_RequiredAttributes &&
							(m_RequiredAttributes & (u32)VertexAttribute::UV)) ||
							(!m_RequiredAttributes && uv0Buffer != nullptr))
						{
							vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::UV;

							if (uv0Buffer == nullptr)
							{
								vertexBufferDataCreateInfo.texCoords_UV.push_back(m_DefaultTexCoord);
							}
							else
							{
								glm::vec2 uv0 = glm::make_vec2(&uv0Buffer[i * 2]);
								uv0 *= m_UVScale;
								if (importSettings && importSettings->flipU)
								{
									uv0.x = 1.0f - uv0.x;
								}
								if (importSettings && importSettings->flipV)
								{
									uv0.y = 1.0f - uv0.y;
								}
								vertexBufferDataCreateInfo.texCoords_UV.push_back(uv0);
							}
						}
					}

					// Indices
					{
						const tinygltf::Accessor& indAccessor = model->accessors[primitive.indices];
						const tinygltf::BufferView& indView = model->bufferViews[indAccessor.bufferView];
						const tinygltf::Buffer& indBuffer = model->buffers[indView.buffer];

						const i32 indexCount = (i32)indAccessor.count;

						switch (indAccessor.componentType)
						{
						case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT:
						{
							const u32* data = (const u32*)&indBuffer.data[indAccessor.byteOffset + indView.byteOffset];
							for (i32 i = 0; i < indexCount; ++i)
							{
								m_Indices.push_back(data[i] + indexStart);
							}
						} break;
						case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT:
						{
							const u16* data = (const u16*)&indBuffer.data[indAccessor.byteOffset + indView.byteOffset];
							for (i32 i = 0; i < indexCount; ++i)
							{
								m_Indices.push_back(data[i] + indexStart);
							}
						} break;
						case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE:
						{
							const u8* data = (const u8*)&indBuffer.data[indAccessor.byteOffset + indView.byteOffset];
							for (i32 i = 0; i < indexCount; ++i)
							{
								m_Indices.push_back(data[i] + indexStart);
							}
						} break;
						default:
						{
							PrintError("Unhandled index component type found in mesh: %d\n", indAccessor.componentType);
						} break;
						}
					}

					if (bCalculateTangents)
					{
						if (!CalculateTangents(vertexBufferDataCreateInfo))
						{
							PrintWarn("Failed to calculate tangents/bitangents for mesh!\n");
						}
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
				if (optionalCreateInfo->materialID != InvalidMaterialID)
				{
					m_MaterialID = optionalCreateInfo->materialID;
					renderObjectCreateInfo.materialID = m_MaterialID;
				}
				renderObjectCreateInfo.visibleInSceneExplorer = optionalCreateInfo->visibleInSceneExplorer;
				renderObjectCreateInfo.cullFace = optionalCreateInfo->cullFace;
				renderObjectCreateInfo.enableCulling = optionalCreateInfo->enableCulling;
				renderObjectCreateInfo.depthTestReadFunc = optionalCreateInfo->depthTestReadFunc;
				renderObjectCreateInfo.depthWriteEnable = optionalCreateInfo->depthWriteEnable;
				renderObjectCreateInfo.editorObject = optionalCreateInfo->editorObject;

				if (optionalCreateInfo->vertexBufferData != nullptr)
				{
					PrintWarn("Attempted to override vertexBufferData in LoadFromFile! Ignoring passed in data\n");
				}
				if (optionalCreateInfo->indices != nullptr)
				{
					PrintWarn("Attempted to override indices in LoadFromFile! Ignoring passed in data\n");
				}
			}

			renderObjectCreateInfo.gameObject = m_OwningGameObject;
			renderObjectCreateInfo.vertexBufferData = &m_VertexBufferData;
			renderObjectCreateInfo.indices = &m_Indices;
			renderObjectCreateInfo.materialID = m_MaterialID;

			RenderID renderID = g_Renderer->InitializeRenderObject(&renderObjectCreateInfo);
			if (m_OwningGameObject->GetRenderID() != InvalidRenderID)
			{
				g_Renderer->DestroyRenderObject(m_OwningGameObject->GetRenderID());
			}
			m_OwningGameObject->SetRenderID(renderID);

			g_Renderer->SetTopologyMode(renderID, TopologyMode::TRIANGLE_LIST);

			m_VertexBufferData.DescribeShaderVariables(g_Renderer, renderID);

			m_bInitialized = true;
		}

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
			if (optionalCreateInfo->materialID != InvalidMaterialID)
			{
				m_MaterialID = optionalCreateInfo->materialID;
				renderObjectCreateInfo.materialID = m_MaterialID;
			}
			renderObjectCreateInfo.visibleInSceneExplorer = optionalCreateInfo->visibleInSceneExplorer;
			renderObjectCreateInfo.cullFace = optionalCreateInfo->cullFace;
			renderObjectCreateInfo.enableCulling = optionalCreateInfo->enableCulling;
			renderObjectCreateInfo.depthTestReadFunc = optionalCreateInfo->depthTestReadFunc;
			renderObjectCreateInfo.depthWriteEnable = optionalCreateInfo->depthWriteEnable;
			renderObjectCreateInfo.editorObject = optionalCreateInfo->editorObject;

			if (optionalCreateInfo->vertexBufferData != nullptr)
			{
				PrintError("Can not override vertexBufferData in LoadPrefabShape! Ignoring passed in data\n");
			}
			if (optionalCreateInfo->indices != nullptr)
			{
				PrintError("Can not override indices in LoadPrefabShape! Ignoring passed in data\n");
			}
		}

		renderObjectCreateInfo.gameObject = m_OwningGameObject;
		renderObjectCreateInfo.materialID = m_MaterialID;

		TopologyMode topologyMode = TopologyMode::TRIANGLE_LIST;

		VertexBufferData::CreateInfo vertexBufferDataCreateInfo = {};

		std::string defaultName;

		switch (shape)
		{
		case MeshComponent::PrefabShape::CUBE:
		{
			const std::array<glm::vec4, 6> colors = { Color::GREEN, Color::RED, Color::BLUE, Color::ORANGE, Color::YELLOW, Color::YELLOW };
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

			vertexBufferDataCreateInfo.colors_R32G32B32A32 =
			{
				// Front
				colors[0],
				colors[0],
				colors[0],

				colors[0],
				colors[0],
				colors[0],

				// Top
				colors[1],
				colors[1],
				colors[1],

				colors[1],
				colors[1],
				colors[1],

				// Back
				colors[2],
				colors[2],
				colors[2],

				colors[2],
				colors[2],
				colors[2],

				// Bottom
				colors[3],
				colors[3],
				colors[3],

				colors[3],
				colors[3],
				colors[3],

				// Right
				colors[4],
				colors[4],
				colors[4],

				colors[4],
				colors[4],
				colors[4],

				// Left
				colors[5],
				colors[5],
				colors[5],

				colors[5],
				colors[5],
				colors[5],
			};
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;

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

			defaultName = "Cube";
		} break;
		case MeshComponent::PrefabShape::GRID:
		{
			const float lineMaxOpacity = 0.5f;
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

				float opacityCenter = glm::pow(1.0f - glm::abs((i / (float)GRID_LINE_COUNT) - 0.5f) * 2.0f, 5.0f);
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

				float opacityCenter = glm::pow(1.0f - glm::abs((i / (float)GRID_LINE_COUNT) - 0.5f) * 2.0f, 5.0f);
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
			defaultName = "Grid";
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

			float opacityCenter = 1.0f;
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
			defaultName = "World Axis Ground Plane";
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

			vertexBufferDataCreateInfo.bitangents =
			{
				{ 1.0f, 0.0f, 0.0f, },
				{ 1.0f, 0.0f, 0.0f, },
				{ 1.0f, 0.0f, 0.0f, },

				{ 1.0f, 0.0f, 0.0f, },
				{ 1.0f, 0.0f, 0.0f, },
				{ 1.0f, 0.0f, 0.0f, },
			};
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::BITANGENT;

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

			defaultName = "Plane";
		} break;
		case MeshComponent::PrefabShape::GERSTNER_PLANE:
		{
			// TODO: Provide as input
			i32 vertCountH = 100;

			i32 vertCount = vertCountH * vertCountH;
			vertexBufferDataCreateInfo.positions_3D.resize(vertCount);
			vertexBufferDataCreateInfo.normals.resize(vertCount);
			vertexBufferDataCreateInfo.tangents.resize(vertCount);
			vertexBufferDataCreateInfo.bitangents.resize(vertCount);
			vertexBufferDataCreateInfo.colors_R32G32B32A32.resize(vertCount);

			for (i32 z = 0; z < vertCountH; ++z)
			{
				for (i32 x = 0; x < vertCountH; ++x)
				{
					i32 vertIdx = z * vertCountH + x;
					// NOTE: Wave generation is implemented in GerstnerWave::Update
					vertexBufferDataCreateInfo.positions_3D[vertIdx] = VEC3_ZERO;

					vertexBufferDataCreateInfo.normals[vertIdx] = glm::vec3(0.0f, 1.0f, 0.0f);
					vertexBufferDataCreateInfo.tangents[vertIdx] = glm::vec3(1.0f, 0.0f, 0.0f);
					vertexBufferDataCreateInfo.bitangents[vertIdx] = glm::vec3(0.0f, 0.0f, 1.0f);
					vertexBufferDataCreateInfo.colors_R32G32B32A32[vertIdx] = VEC4_ONE;
				}
			}
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::POSITION;
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::NORMAL;
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::TANGENT;
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::BITANGENT;
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

			defaultName = "Gerstner Plane";
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

			assert(parallelCount > 0 && meridianCount > 0);
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

			defaultName = "UV Sphere";
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
			defaultName = "Skybox";

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
			m_MaxPoint = glm::vec3(FLT_MIN);

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

	void MeshComponent::Update()
	{
		if (m_Shape == PrefabShape::GRID)
		{
			Transform* transform = m_OwningGameObject->GetTransform();
			glm::vec3 camPos = g_CameraManager->CurrentCamera()->GetPosition();
			glm::vec3 newGridPos = glm::vec3(camPos.x - fmod(
				camPos.x + GRID_LINE_SPACING/2.0f, GRID_LINE_SPACING),
				transform->GetWorldPosition().y,
				camPos.z - fmod(camPos.z + GRID_LINE_SPACING / 2.0f, GRID_LINE_SPACING));
			transform->SetWorldPosition(newGridPos);
		}
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
	}

	MaterialID MeshComponent::GetMaterialID() const
	{
		return m_MaterialID;
	}

	void MeshComponent::SetMaterialID(MaterialID materialID)
	{
		m_MaterialID = materialID;
		if (m_bInitialized && m_OwningGameObject)
		{
			g_Renderer->SetRenderObjectMaterialID(m_OwningGameObject->GetRenderID(), materialID);
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

	MeshComponent::Type MeshComponent::GetType() const
	{
		return m_Type;
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

	std::string MeshComponent::GetRelativeFilePath() const
	{
		return m_RelativeFilePath;
	}

	std::string MeshComponent::GetFileName() const
	{
		return m_FileName;
	}
} // namespace flex
