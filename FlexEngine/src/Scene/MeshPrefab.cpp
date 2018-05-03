#include "stdafx.hpp"

#include "Scene/MeshPrefab.hpp"

#include <assimp/vector3.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <glm/gtc/matrix_transform.hpp>

#include <string>

#include "Cameras/CameraManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Colors.hpp"
#include "GameContext.hpp"
#include "Helpers.hpp"
#include "Logger.hpp"

namespace flex
{
	const real MeshPrefab::GRID_LINE_SPACING = 1.0f;
	const u32 MeshPrefab::GRID_LINE_COUNT = 150;

	std::map<std::string, MeshPrefab::LoadedMesh*> MeshPrefab::m_LoadedMeshes;

	glm::vec4 MeshPrefab::m_DefaultColor_4(1.0f, 1.0f, 1.0f, 1.0f);
	glm::vec3 MeshPrefab::m_DefaultPosition(0.0f, 0.0f, 0.0f);
	glm::vec3 MeshPrefab::m_DefaultTangent(1.0f, 0.0f, 0.0f);
	glm::vec3 MeshPrefab::m_DefaultBitangent(0.0f, 0.0f, 1.0f);
	glm::vec3 MeshPrefab::m_DefaultNormal(0.0f, 1.0f, 0.0f);
	glm::vec2 MeshPrefab::m_DefaultTexCoord(0.0f, 0.0f);

	MeshPrefab::MeshPrefab(const std::string& name) :
		MeshPrefab(0, name)
	{
	}

	MeshPrefab::MeshPrefab(MaterialID materialID, const std::string& name) :
		GameObject(name, SerializableType::MESH),
		m_MaterialID(materialID),
		m_UVScale(1.0f, 1.0f)
	{
	}

	MeshPrefab::~MeshPrefab()
	{
		m_VertexBufferData.Destroy();
	}

	void MeshPrefab::Shutdown()
	{
		for (auto iter = m_LoadedMeshes.begin(); iter != m_LoadedMeshes.end(); /**/)
		{
			SafeDelete(iter->second);
			iter = m_LoadedMeshes.erase(iter);
		}
	}

	void MeshPrefab::SetRequiredAttributes(VertexAttributes requiredAttributes)
	{
		m_RequiredAttributes |= requiredAttributes;
	}

	bool MeshPrefab::GetLoadedMesh(const std::string& filePath, const aiScene** scene)
	{
		auto location = m_LoadedMeshes.find(filePath);
		if (location == m_LoadedMeshes.end())
		{
			return false;
		}
		else
		{
			*scene = location->second->scene;
			return true;
		}
	}

	bool MeshPrefab::LoadFromFile(const GameContext& gameContext,
		const std::string& filepath,
		ImportSettings* importSettings /* = nullptr */,
		RenderObjectCreateInfo* optionalCreateInfo /* = nullptr */)
	{
		m_Type = Type::FILE;
		m_Shape = PrefabShape::NONE;
		m_Filepath = filepath;

		m_VertexBufferData.Destroy();

		m_Transform.SetGameObject(this);

		VertexBufferData::CreateInfo vertexBufferDataCreateInfo = {};

		const aiScene* scene = nullptr;
		if (!GetLoadedMesh(filepath, &scene))
		{
			// Mesh hasn't been loaded before, load it now

			std::string fileName = filepath;
			StripLeadingDirectories(fileName);
			Logger::LogInfo("Loading mesh " + fileName);

			auto meshObj = m_LoadedMeshes.emplace(filepath, new LoadedMesh());
			LoadedMesh* loadedMesh = meshObj.first->second;

			loadedMesh->scene = loadedMesh->importer.ReadFile(filepath,
				aiProcess_FindInvalidData |
				aiProcess_GenNormals |
				aiProcess_CalcTangentSpace
			);

			scene = loadedMesh->scene;

			if (!scene)
			{
				Logger::LogError(loadedMesh->importer.GetErrorString());
				return false;
			}
		}

		if (!scene)
		{
			Logger::LogError("Failed to load mesh " + filepath);
			return false;
		}


		if (!scene->HasMeshes())
		{
			Logger::LogWarning("Loaded mesh file has no meshes! " + filepath);
			return false;
		}

		std::vector<aiMesh*> meshes(scene->mNumMeshes);
		for (size_t i = 0; i < scene->mNumMeshes; ++i)
		{
			meshes[i] = scene->mMeshes[i];
		}

		if (m_Name.empty())
		{
			m_Name = meshes[0]->mName.C_Str();

			if (m_Name.empty())
			{
				auto filePathParts = Split(filepath, '.');
				std::string friendlyFileName = filePathParts[filePathParts.size() - 1];
				StripLeadingDirectories(friendlyFileName);

				m_Name = "Mesh prefab - " + friendlyFileName;
			}
		}

		size_t totalVertCount = 0;

		for (aiMesh* mesh : meshes)
		{
			const size_t numMeshVerts = mesh->mNumVertices;
			totalVertCount += numMeshVerts;

			// Cached bools per-mesh
			const bool meshHasVertexColors0 = mesh->HasVertexColors(0);
			const bool meshHasTangentsAndBitangents = mesh->HasTangentsAndBitangents();
			const bool meshHasNormals = mesh->HasNormals();
			const bool meshHasTexCoord0 = mesh->HasTextureCoords(0);

			// All meshes need positions
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::POSITION;

			for (size_t i = 0; i < numMeshVerts; ++i)
			{
				// Position
				glm::vec3 pos = ToVec3(mesh->mVertices[i]);
				vertexBufferDataCreateInfo.positions_3D.push_back(pos);

				// Color
				if ((m_RequiredAttributes && 
					(m_RequiredAttributes & (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT)) ||
					(!m_RequiredAttributes && meshHasVertexColors0))
				{
					vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;

					if (meshHasVertexColors0)
					{
						glm::vec4 col = ToVec4(mesh->mColors[0][i]);
						vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(col);
					}
					else
					{
						vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(m_DefaultColor_4);
					}
				}


				// Tangent
				if ((m_RequiredAttributes &&
					(m_RequiredAttributes & (u32)VertexAttribute::TANGENT)) ||
					(!m_RequiredAttributes && meshHasTangentsAndBitangents))
				{
					vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::TANGENT;

					if (meshHasTangentsAndBitangents)
					{
						glm::vec3 tangent = ToVec3(mesh->mTangents[i]);
						vertexBufferDataCreateInfo.tangents.push_back(tangent);
					}
					else
					{
						vertexBufferDataCreateInfo.tangents.push_back(m_DefaultTangent);
					}
				}

				// Bitangent
				if ((m_RequiredAttributes &&
					(m_RequiredAttributes & (u32)VertexAttribute::BITANGENT)) ||
					(!m_RequiredAttributes && meshHasTangentsAndBitangents))
				{
					vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::BITANGENT;

					if (meshHasTangentsAndBitangents)
					{
						glm::vec3 bitangent = ToVec3(mesh->mBitangents[i]);
						vertexBufferDataCreateInfo.bitangents.push_back(bitangent);
					}
					else
					{
						vertexBufferDataCreateInfo.bitangents.push_back(m_DefaultBitangent);
					}
				}

				// Normal
				if ((m_RequiredAttributes &&
					(m_RequiredAttributes & (u32)VertexAttribute::BITANGENT)) ||
					(!m_RequiredAttributes && meshHasNormals))
				{
					vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::NORMAL;

					if (meshHasNormals)
					{
						glm::vec3 norm = ToVec3(mesh->mNormals[i]);
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
					else
					{
						vertexBufferDataCreateInfo.normals.push_back(m_DefaultNormal);
					}
				}

				// TexCoord
				if ((m_RequiredAttributes &&
					(m_RequiredAttributes & (u32)VertexAttribute::BITANGENT)) ||
					(!m_RequiredAttributes && meshHasTexCoord0))
				{
					vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::UV;

					if (meshHasTexCoord0)
					{
						// Truncate w component
						glm::vec2 texCoord = (glm::vec2)(ToVec3(mesh->mTextureCoords[0][i]));
						texCoord *= m_UVScale;
						if (importSettings && importSettings->flipU)
						{
							texCoord.x = 1.0f - texCoord.x;
						}
						if (importSettings && importSettings->flipV)
						{
							texCoord.y = 1.0f - texCoord.y;
						}
						vertexBufferDataCreateInfo.texCoords_UV.push_back(texCoord);
					}
					else
					{
						vertexBufferDataCreateInfo.texCoords_UV.push_back(m_DefaultTexCoord);
					}
				}
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

			if (optionalCreateInfo->vertexBufferData != nullptr)
			{
				Logger::LogError("Can not override vertexBufferData in LoadFromFile! Ignoring passed in data");
			}
			if (optionalCreateInfo->indices != nullptr)
			{
				Logger::LogError("Can not override vertexBufferData in LoadFromFile! Ignoring passed in data");
			}
		}

		renderObjectCreateInfo.gameObject = this;
		renderObjectCreateInfo.vertexBufferData = &m_VertexBufferData;
		renderObjectCreateInfo.materialID = m_MaterialID;

		SetRenderID(gameContext.renderer->InitializeRenderObject(gameContext, &renderObjectCreateInfo));

		gameContext.renderer->SetTopologyMode(m_RenderID, TopologyMode::TRIANGLE_LIST);

		m_VertexBufferData.DescribeShaderVariables(gameContext.renderer, m_RenderID);

		m_Initialized = true;

		return true;
	}

	bool MeshPrefab::LoadPrefabShape(const GameContext& gameContext, PrefabShape shape, RenderObjectCreateInfo* optionalCreateInfo)
	{
		m_Type = Type::PREFAB;
		m_Shape = shape;

		m_VertexBufferData.Destroy();

		m_Transform.SetGameObject(this);

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

			if (optionalCreateInfo->vertexBufferData != nullptr)
			{
				Logger::LogError("Can not override vertexBufferData in LoadPrefabShape! Ignoring passed in data");
			}
			if (optionalCreateInfo->indices != nullptr)
			{
				Logger::LogError("Can not override vertexBufferData in LoadPrefabShape! Ignoring passed in data");
			}
		}

		renderObjectCreateInfo.gameObject = this;
		renderObjectCreateInfo.materialID = m_MaterialID;

		TopologyMode topologyMode = TopologyMode::TRIANGLE_LIST;

		VertexBufferData::CreateInfo vertexBufferDataCreateInfo = {};

		std::string defaultName;

		switch (shape)
		{
		case MeshPrefab::PrefabShape::CUBE:
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
		case MeshPrefab::PrefabShape::GRID:
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
				vertexBufferDataCreateInfo.positions_3D.push_back({ i * GRID_LINE_SPACING - halfWidth, 0.0f, -halfWidth });
				vertexBufferDataCreateInfo.positions_3D.push_back({ i * GRID_LINE_SPACING - halfWidth, 0.0f, 0.0f });
				vertexBufferDataCreateInfo.positions_3D.push_back({ i * GRID_LINE_SPACING - halfWidth, 0.0f, 0.0f });
				vertexBufferDataCreateInfo.positions_3D.push_back({ i * GRID_LINE_SPACING - halfWidth, 0.0f, halfWidth });

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
				vertexBufferDataCreateInfo.positions_3D.push_back({ -halfWidth, 0.0f, i * GRID_LINE_SPACING - halfWidth });
				vertexBufferDataCreateInfo.positions_3D.push_back({ 0.0f, 0.0f, i * GRID_LINE_SPACING - halfWidth });
				vertexBufferDataCreateInfo.positions_3D.push_back({ 0.0f, 0.0f, i * GRID_LINE_SPACING - halfWidth });
				vertexBufferDataCreateInfo.positions_3D.push_back({ halfWidth, 0.0f, i * GRID_LINE_SPACING - halfWidth });

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
		case MeshPrefab::PrefabShape::WORLD_AXIS_GROUND:
		{
			glm::vec4 centerLineColorX = Color::RED;
			glm::vec4 centerLineColorY = Color::GREEN;

			const size_t vertexCount = 4 * 2; // 4 verts per line (to allow for fading) *------**------*
			vertexBufferDataCreateInfo.positions_3D.reserve(vertexCount);
			vertexBufferDataCreateInfo.colors_R32G32B32A32.reserve(vertexCount);

			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::POSITION;
			vertexBufferDataCreateInfo.attributes |= (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;

			real halfWidth = (GRID_LINE_SPACING * (GRID_LINE_COUNT - 1)); // extend longer than normal grid lines

			vertexBufferDataCreateInfo.positions_3D.push_back({ 0.0f, 0.0f, -halfWidth });
			vertexBufferDataCreateInfo.positions_3D.push_back({ 0.0f, 0.0f, 0.0f });
			vertexBufferDataCreateInfo.positions_3D.push_back({ 0.0f, 0.0f, 0.0f });
			vertexBufferDataCreateInfo.positions_3D.push_back({ 0.0f, 0.0f, halfWidth });

			float opacityCenter = 1.0f;
			glm::vec4 colorCenter = centerLineColorX;
			colorCenter.a = opacityCenter;
			glm::vec4 colorEnds = colorCenter;
			colorEnds.a = 0.0f;
			vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(colorEnds);
			vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(colorCenter);
			vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(colorCenter);
			vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(colorEnds);

			vertexBufferDataCreateInfo.positions_3D.push_back({ -halfWidth, 0.0f, 0.0f });
			vertexBufferDataCreateInfo.positions_3D.push_back({ 0.0f, 0.0f, 0.0f });
			vertexBufferDataCreateInfo.positions_3D.push_back({ 0.0f, 0.0f, 0.0f });
			vertexBufferDataCreateInfo.positions_3D.push_back({ halfWidth, 0.0f, 0.0f });

			colorCenter = centerLineColorY;
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
		case MeshPrefab::PrefabShape::PLANE:
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
		case MeshPrefab::PrefabShape::UV_SPHERE:
		{
			// Vertices
			glm::vec3 v1(0.0f, 1.0f, 0.0f); // Top vertex
			vertexBufferDataCreateInfo.positions_3D.push_back(v1);
			vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(Color::RED);
			vertexBufferDataCreateInfo.texCoords_UV.push_back({ 0.0f, 0.0f });
			vertexBufferDataCreateInfo.normals.push_back({ 0.0f, 1.0f, 0.0f });

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
					vertexBufferDataCreateInfo.texCoords_UV.push_back({ 0.0f, 0.0f });
					vertexBufferDataCreateInfo.normals.push_back({ 1.0f, 0.0f, 0.0f });
				}
			}

			glm::vec3 vF(0.0f, -1.0f, 0.0f); // Bottom vertex
			vertexBufferDataCreateInfo.positions_3D.push_back(vF);
			vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(Color::YELLOW);
			vertexBufferDataCreateInfo.texCoords_UV.push_back({ 0.0f, 0.0f });
			vertexBufferDataCreateInfo.normals.push_back({ 0.0f, -1.0f, 0.0f });

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
		case MeshPrefab::PrefabShape::SKYBOX:
		{
			// TODO: Move to separate class?
			m_SerializableType = SerializableType::SKYBOX;
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
			Logger::LogWarning("Unhandled prefab shape passed to MeshPrefab::LoadPrefabShape: " + std::to_string((i32)shape));
			return false;
		} break;
		}

		m_VertexBufferData.Initialize(&vertexBufferDataCreateInfo);

		renderObjectCreateInfo.vertexBufferData = &m_VertexBufferData;
		if (m_Name.empty() || defaultName.empty())
		{
			m_Name = defaultName;
		}

		SetRenderID(gameContext.renderer->InitializeRenderObject(gameContext, &renderObjectCreateInfo));

		gameContext.renderer->SetTopologyMode(m_RenderID, topologyMode);
		m_VertexBufferData.DescribeShaderVariables(gameContext.renderer, m_RenderID);

		m_Initialized = true;

		return true;
	}

	void MeshPrefab::Update(const GameContext& gameContext)
	{
		GameObject::Update(gameContext);

		if (m_Shape == PrefabShape::GRID)
		{
			glm::vec3 camPos = gameContext.cameraManager->CurrentCamera()->GetPosition();
			glm::vec3 newGridPos = glm::vec3(camPos.x - fmod(
				camPos.x + GRID_LINE_SPACING/2.0f, GRID_LINE_SPACING), 
				m_Transform.GetGlobalPosition().y,
				camPos.z - fmod(camPos.z + GRID_LINE_SPACING / 2.0f, GRID_LINE_SPACING));
			m_Transform.SetGlobalPosition(newGridPos);
		}
	}

	MaterialID MeshPrefab::GetMaterialID() const
	{
		return m_MaterialID;
	}

	void MeshPrefab::SetMaterialID(MaterialID materialID, const GameContext& gameContext)
	{
		m_MaterialID = materialID;
		if (m_Initialized)
		{
			gameContext.renderer->SetRenderObjectMaterialID(m_RenderID, materialID);
		}
	}

	void MeshPrefab::SetUVScale(real uScale, real vScale)
	{
		m_UVScale = glm::vec2(uScale, vScale);
	}

	MeshPrefab::PrefabShape MeshPrefab::StringToPrefabShape(const std::string& prefabName)
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
			Logger::LogError("Unhandled prefab shape string: " + prefabName);
			return PrefabShape::NONE;
		}

	}

	std::string MeshPrefab::PrefabShapeToString(PrefabShape shape)
	{
		switch (shape)
		{
		case MeshPrefab::PrefabShape::CUBE:					return "cube";
		case MeshPrefab::PrefabShape::GRID:					return "grid";
		case MeshPrefab::PrefabShape::WORLD_AXIS_GROUND:	return "world axis ground";
		case MeshPrefab::PrefabShape::PLANE:				return "plane";
		case MeshPrefab::PrefabShape::UV_SPHERE:			return "uv sphere";
		case MeshPrefab::PrefabShape::SKYBOX:				return "skybox";
		case MeshPrefab::PrefabShape::NONE:					return "NONE";
		default:											return "UNHANDLED PREFAB SHAPE";
		}
	}

	MeshPrefab::Type MeshPrefab::GetType() const
	{
		return m_Type;
	}

	MeshPrefab::PrefabShape MeshPrefab::GetShape() const
	{
		return m_Shape;
	}

	std::string MeshPrefab::GetFilepath() const
	{
		return m_Filepath;
	}
} // namespace flex
