#include "stdafx.h"

#include "Scene/MeshPrefab.h"

#include <assimp/Importer.hpp>
#include <assimp/vector3.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <glm/gtc/matrix_transform.hpp>

#include "Colors.h"
#include "GameContext.h"
#include "Helpers.h"
#include "Logger.h"
#include "Typedefs.h"

namespace flex
{
	std::string MeshPrefab::m_DefaultName = "Game Object";
	glm::vec4 MeshPrefab::m_DefaultColor(1.0f, 1.0f, 1.0f, 1.0f);
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
		m_MaterialID(materialID),
		m_Name(name)
	{
		if (m_Name.empty()) m_Name = m_DefaultName;
	}

	MeshPrefab::~MeshPrefab()
	{
		for (size_t i = 0; i < m_VertexBuffers.size(); ++i)
		{
			m_VertexBuffers[i].Destroy();
		}
	}

	bool MeshPrefab::LoadFromFile(const GameContext& gameContext, const std::string& filepath)
	{
		Assimp::Importer importer;

		const aiScene* pScene = importer.ReadFile(filepath,
			aiProcess_FindInvalidData |
			aiProcess_GenNormals
		);

		if (!pScene)
		{
			Logger::LogError(importer.GetErrorString());
			return false;
		}

		if (!pScene->HasMeshes())
		{
			Logger::LogWarning("Loaded mesh file has no meshes! " + filepath);
			return false;
		}

		aiMesh* mesh = pScene->mMeshes[0];

		std::string meshName(mesh->mName.C_Str());
		if (!meshName.empty()) m_Name = meshName;

		const size_t numVerts = mesh->mNumVertices;

		for (size_t i = 0; i < numVerts; ++i)
		{
			// Position
			glm::vec3 pos = ToVec3(mesh->mVertices[i]);
			pos = glm::vec3(pos.x, pos.z, -pos.y); // Rotate +90 deg around x axis
			m_Positions.push_back(pos);
			m_Attributes |= (glm::uint)VertexBufferData::Attribute::POSITION;

			// Color
			glm::vec4 col;
			if (mesh->HasVertexColors(0))
			{
				col = ToVec4(mesh->mColors[0][i]);
			}
			else
			{
				// Force color even on models which don't contain it
				col = m_DefaultColor;
			}
			m_Colors.push_back(col);
			m_Attributes |= (glm::uint)VertexBufferData::Attribute::COLOR_R32G32B32A32_SFLOAT;

			// Tangent & Bitangent
			if (mesh->HasTangentsAndBitangents())
			{
				glm::vec3 tangent = ToVec3(mesh->mTangents[i]);
				m_Tangents.push_back(tangent);
				m_Attributes |= (glm::uint)VertexBufferData::Attribute::TANGENT;

				glm::vec3 bitangent = ToVec3(mesh->mBitangents[i]);
				m_Bitangents.push_back(bitangent);
				m_Attributes |= (glm::uint)VertexBufferData::Attribute::BITANGENT;
			}

			// Normal
			if (mesh->HasNormals())
			{
				glm::vec3 norm = ToVec3(mesh->mNormals[i]);
				m_Normals.push_back(norm);
				m_Attributes |= (glm::uint)VertexBufferData::Attribute::NORMAL;
			}

			// TexCoord
			if (mesh->HasTextureCoords(0))
			{
				// Truncate w component
				glm::vec2 texCoord = (glm::vec2)(ToVec3(mesh->mTextureCoords[0][i]));
				m_TexCoords.push_back(texCoord);
				m_Attributes |= (glm::uint)VertexBufferData::Attribute::UV;
			}
		}

		Renderer* renderer = gameContext.renderer;

		m_VertexBuffers.push_back({});
		VertexBufferData* vertexBufferData = m_VertexBuffers.data() + (m_VertexBuffers.size() - 1);

		if (m_MaterialID == 1)
		{
			m_Attributes = (glm::uint)VertexBufferData::Attribute::POSITION | (glm::uint)VertexBufferData::Attribute::COLOR_R32G32B32A32_SFLOAT;
		}

		CreateVertexBuffer(vertexBufferData);

		Renderer::RenderObjectCreateInfo createInfo = {};
		createInfo.vertexBufferData = vertexBufferData;
		createInfo.materialID = m_MaterialID;
		createInfo.name = m_Name;

		m_RenderID = renderer->InitializeRenderObject(gameContext, &createInfo);

		renderer->SetTopologyMode(m_RenderID, Renderer::TopologyMode::TRIANGLE_LIST);

		DescribeShaderVariables(gameContext, vertexBufferData);

		return true;
	}

	bool MeshPrefab::LoadPrefabShape(const GameContext& gameContext, PrefabShape shape)
	{
		Renderer* renderer = gameContext.renderer;

		m_VertexBuffers.push_back({});
		VertexBufferData* vertexBufferData = m_VertexBuffers.data() + (m_VertexBuffers.size() - 1);
		Renderer::RenderObjectCreateInfo renderObjectCreateInfo = {};
		renderObjectCreateInfo.materialID = 0;

		Renderer::TopologyMode topologyMode = Renderer::TopologyMode::TRIANGLE_LIST;

		switch (shape)
		{
		case MeshPrefab::PrefabShape::CUBE:
		{
			const std::array<glm::vec4, 6> colors = { Color::RED, Color::RED, Color::RED, Color::RED, Color::RED, Color::RED };
			m_Positions =
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
			m_Attributes |= (glm::uint)VertexBufferData::Attribute::POSITION;

			m_Colors =
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
			m_Attributes |= (glm::uint)VertexBufferData::Attribute::COLOR_R32G32B32A32_SFLOAT;

			m_Normals =
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
			m_Attributes |= (glm::uint)VertexBufferData::Attribute::NORMAL;

			m_TexCoords =
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
			m_Attributes |= (glm::uint)VertexBufferData::Attribute::UV;

			renderObjectCreateInfo.materialID = 1;
			renderObjectCreateInfo.name = "Cube";
		} break;
		case MeshPrefab::PrefabShape::GRID:
		{
			float rowWidth = 10.0f;
			glm::uint lineCount = 15;

			const glm::vec4 lineColor = Color::GRAY;
			const glm::vec4 centerLineColor = Color::LIGHT_GRAY;

			const size_t vertexCount = lineCount * 2 * 2;
			m_Positions.reserve(vertexCount);
			m_Colors.reserve(vertexCount);
			m_TexCoords.reserve(vertexCount);
			m_Normals.reserve(vertexCount);

			m_Attributes |= (glm::uint)VertexBufferData::Attribute::POSITION;
			m_Attributes |= (glm::uint)VertexBufferData::Attribute::COLOR_R32G32B32A32_SFLOAT;

			float halfWidth = (rowWidth * (lineCount - 1)) / 2.0f;

			// Horizontal lines
			for (glm::uint i = 0; i < lineCount; ++i)
			{
				m_Positions.push_back({ i * rowWidth - halfWidth, 0.0f, -halfWidth });
				m_Positions.push_back({ i * rowWidth - halfWidth, 0.0f, halfWidth });

				const glm::vec4 color = (i == lineCount / 2 ? centerLineColor : lineColor);
				m_Colors.push_back(color);
				m_Colors.push_back(color);
			}

			// Vertical lines
			for (glm::uint i = 0; i < lineCount; ++i)
			{
				m_Positions.push_back({ -halfWidth, 0.0f, i * rowWidth - halfWidth });
				m_Positions.push_back({ halfWidth, 0.0f, i * rowWidth - halfWidth });

				const glm::vec4 color = (i == lineCount / 2 ? centerLineColor : lineColor);
				m_Colors.push_back(color);
				m_Colors.push_back(color);
			}

			topologyMode = Renderer::TopologyMode::LINE_LIST;
			renderObjectCreateInfo.materialID = 1;
			renderObjectCreateInfo.name = "Grid";
		} break;
		case MeshPrefab::PrefabShape::PLANE:
		{
			m_Positions =
			{
				{ -50.0f, 0.0f, -50.0f, },
				{ -50.0f, 0.0f,  50.0f, },
				{ 50.0f,  0.0f,  50.0f, },

				{ -50.0f, 0.0f, -50.0f, },
				{ 50.0f,  0.0f,  50.0f, },
				{ 50.0f,  0.0f, -50.0f, },
			};
			m_Attributes |= (glm::uint)VertexBufferData::Attribute::POSITION;

			m_Normals =
			{
				{ 0.0f, 1.0f, 0.0f, },
				{ 0.0f, 1.0f, 0.0f, },
				{ 0.0f, 1.0f, 0.0f, },

				{ 0.0f, 1.0f, 0.0f, },
				{ 0.0f, 1.0f, 0.0f, },
				{ 0.0f, 1.0f, 0.0f, },
			};
			m_Attributes |= (glm::uint)VertexBufferData::Attribute::NORMAL;

			m_Colors =
			{
				{ 1.0f, 1.0f, 1.0f, 1.0f },
				{ 1.0f, 1.0f, 1.0f, 1.0f },
				{ 1.0f, 1.0f, 1.0f, 1.0f },

				{ 1.0f, 1.0f, 1.0f, 1.0f },
				{ 1.0f, 1.0f, 1.0f, 1.0f },
				{ 1.0f, 1.0f, 1.0f, 1.0f },
			};
			m_Attributes |= (glm::uint)VertexBufferData::Attribute::COLOR_R32G32B32A32_SFLOAT;

			m_TexCoords =
			{
				{ 0.0f, 0.0f },
				{ 0.0f, 1.0f },
				{ 1.0f, 1.0f },

				{ 0.0f, 0.0f },
				{ 1.0f, 1.0f },
				{ 1.0f, 0.0f },
			};
			m_Attributes |= (glm::uint)VertexBufferData::Attribute::UV;

			renderObjectCreateInfo.materialID = 1;
			renderObjectCreateInfo.name = "Plane";
		} break;
		case MeshPrefab::PrefabShape::UV_SPHERE:
		{
			// Vertices
			glm::vec3 v1(0.0f, 1.0f, 0.0f); // Top vertex
			m_Positions.push_back(v1);
			m_Colors.push_back(Color::RED);
			m_TexCoords.push_back({ 0.0f, 0.0f });
			m_Normals.push_back({ 0.0f, 1.0f, 0.0f });

			m_Attributes |= (glm::uint)VertexBufferData::Attribute::POSITION;
			m_Attributes |= (glm::uint)VertexBufferData::Attribute::COLOR_R32G32B32A32_SFLOAT;
			m_Attributes |= (glm::uint)VertexBufferData::Attribute::UV;
			m_Attributes |= (glm::uint)VertexBufferData::Attribute::NORMAL;

			glm::uint parallelCount = 10;
			glm::uint meridianCount = 5;

			assert(parallelCount > 0 && meridianCount > 0);

			const float PI = glm::pi<float>();
			const float TWO_PI = glm::two_pi<float>();

			for (glm::uint j = 0; j < parallelCount - 1; ++j)
			{
				float polar = PI * float(j + 1) / (float)parallelCount;
				float sinP = sin(polar);
				float cosP = cos(polar);
				for (glm::uint i = 0; i < meridianCount; ++i)
				{
					float azimuth = TWO_PI * (float)i / (float)meridianCount;
					float sinA = sin(azimuth);
					float cosA = cos(azimuth);
					glm::vec3 point(sinP * cosA, cosP, sinP * sinA);
					const glm::vec4 color =
						(i % 2 == 0 ? j % 2 == 0 ? Color::ORANGE : Color::PURPLE : j % 2 == 0 ? Color::WHITE : Color::YELLOW);

					m_Positions.push_back(point);
					m_Colors.push_back(color);
					m_TexCoords.push_back({ 0.0f, 0.0f });
					m_Normals.push_back({ 1.0f, 0.0f, 0.0f });
				}
			}

			glm::vec3 vF(0.0f, -1.0f, 0.0f); // Bottom vertex
			m_Positions.push_back(vF);
			m_Colors.push_back(Color::YELLOW);
			m_TexCoords.push_back({ 0.0f, 0.0f });
			m_Normals.push_back({ 0.0f, -1.0f, 0.0f });

			const glm::uint numVerts = m_Positions.size();


			// Indices
			m_Indices.clear();

			// Top triangles
			for (size_t i = 0; i < meridianCount; ++i)
			{
				glm::uint a = i + 1;
				glm::uint b = (i + 1) % meridianCount + 1;
				m_Indices.push_back(0);
				m_Indices.push_back(b);
				m_Indices.push_back(a);
			}

			// Center quads
			for (size_t j = 0; j < parallelCount - 2; ++j)
			{
				glm::uint aStart = j * meridianCount + 1;
				glm::uint bStart = (j + 1) * meridianCount + 1;
				for (size_t i = 0; i < meridianCount; ++i)
				{
					glm::uint a = aStart + i;
					glm::uint a1 = aStart + (i + 1) % meridianCount;
					glm::uint b = bStart + i;
					glm::uint b1 = bStart + (i + 1) % meridianCount;
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
				glm::uint a = i + meridianCount * (parallelCount - 2) + 1;
				glm::uint b = (i + 1) % meridianCount + meridianCount * (parallelCount - 2) + 1;
				m_Indices.push_back(numVerts - 1);
				m_Indices.push_back(a);
				m_Indices.push_back(b);
			}

			renderObjectCreateInfo.name = "UV Sphere";
		} break;
		case MeshPrefab::PrefabShape::SKYBOX:
		{
			m_Positions =
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
			m_Attributes |= (glm::uint)VertexBufferData::Attribute::POSITION;

			// TODO: At *least* use strings rather than indices
			renderObjectCreateInfo.materialID = 3;
			renderObjectCreateInfo.cullFace = Renderer::CullFace::FRONT;
			renderObjectCreateInfo.name = "Skybox";

		} break;
		default:
		{
			Logger::LogWarning("Unhandled prefab shape passed to MeshPrefab::LoadPrefabShape: " + std::to_string((int)shape));
			return false;
		} break;
		}

		CreateVertexBuffer(vertexBufferData);
		renderObjectCreateInfo.vertexBufferData = vertexBufferData;
		renderObjectCreateInfo.materialID = m_MaterialID;
		if (!m_Name.empty() && m_Name.compare(m_DefaultName) != 0) renderObjectCreateInfo.name = m_Name;

		m_RenderID = renderer->InitializeRenderObject(gameContext, &renderObjectCreateInfo);

		renderer->SetTopologyMode(m_RenderID, topologyMode);
		DescribeShaderVariables(gameContext, vertexBufferData);

		return true;
	}

	void MeshPrefab::Initialize(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
	}

	void MeshPrefab::Update(const GameContext& gameContext)
	{
		Renderer* renderer = gameContext.renderer;

		glm::mat4 model = m_Transform.GetModelMatrix();
		renderer->UpdateTransformMatrix(gameContext, m_RenderID, model);
	}

	void MeshPrefab::Destroy(const GameContext& gameContext)
	{
		gameContext.renderer->Destroy(m_RenderID);
	}

	void MeshPrefab::SetMaterialID(MaterialID materialID)
	{
		m_MaterialID = materialID;
	}

	RenderID MeshPrefab::GetRenderID() const
	{
		return m_RenderID;
	}

	void MeshPrefab::DescribeShaderVariables(const GameContext& gameContext, VertexBufferData* vertexBufferData)
	{
		Renderer* renderer = gameContext.renderer;

		struct VertexType
		{
			std::string name;
			int size;
		};

		VertexType vertexTypes[] = { 
			{ "in_Position", 3 },
			{ "in_Position2D", 2 },
			{ "in_TexCoord", 2 },
			{ "in_TexCoord_UVW", 3 },
			{ "in_Color_32", 1	},
			{ "in_Color", 4 },
			{ "in_Tangent", 3 },
			{ "in_Bitangent", 3	},
			{ "in_Normal", 3 },
		};

		const size_t vertexTypeCount = sizeof(vertexTypes) / sizeof(vertexTypes[0]);
		float* currentLocation = (float*)0;
		for (size_t i = 0; i < vertexTypeCount; ++i)
		{
			VertexBufferData::Attribute vertexType = VertexBufferData::Attribute(1 << i);
			if (m_Attributes & (int)vertexType)
			{
				renderer->DescribeShaderVariable(m_RenderID, vertexTypes[i].name, vertexTypes[i].size, Renderer::Type::FLOAT, false,
					(int)vertexBufferData->VertexStride, currentLocation);
				currentLocation += vertexTypes[i].size;
			}
		}
	}

	// TODO: Move to helper file? (TODO: Handle pos_2d and uvw)
	void MeshPrefab::CreateVertexBuffer(VertexBufferData* vertexBufferData)
	{
		vertexBufferData->VertexCount = m_Positions.size();
		vertexBufferData->Attributes = m_Attributes;
		vertexBufferData->VertexStride = vertexBufferData->CalculateStride();
		vertexBufferData->BufferSize = vertexBufferData->VertexCount * vertexBufferData->VertexStride;

		const std::string errorMsg = "Unqeual number of vertex components in MeshPrefab!";
		if (!m_Colors.empty()) Logger::Assert(m_Colors.size() == vertexBufferData->VertexCount, errorMsg);
		if (!m_Tangents.empty()) Logger::Assert(m_Tangents.size() == vertexBufferData->VertexCount, errorMsg);
		if (!m_Bitangents.empty()) Logger::Assert(m_Bitangents.size() == vertexBufferData->VertexCount, errorMsg);
		if (!m_Normals.empty()) Logger::Assert(m_Normals.size() == vertexBufferData->VertexCount, errorMsg);
		if (!m_TexCoords.empty()) Logger::Assert(m_TexCoords.size() == vertexBufferData->VertexCount, errorMsg);

		void *pDataLocation = malloc(vertexBufferData->BufferSize);
		if (pDataLocation == nullptr)
		{
			Logger::LogWarning("MeshPrefab::LoadPrefabShape failed to allocate memory required for vertex buffer data");
			return;
		}

		vertexBufferData->pDataStart = pDataLocation;

		for (UINT i = 0; i < vertexBufferData->VertexCount; ++i)
		{
			if (m_Attributes & (glm::uint)VertexBufferData::Attribute::POSITION)
			{
				memcpy(pDataLocation, &m_Positions[i], sizeof(glm::vec3));
				pDataLocation = (float*)pDataLocation + 3;
			}

			if (m_Attributes & (glm::uint)VertexBufferData::Attribute::UV)
			{
				memcpy(pDataLocation, &m_TexCoords[i], sizeof(glm::vec2));
				pDataLocation = (float*)pDataLocation + 2;
			}

			if (m_Attributes & (glm::uint)VertexBufferData::Attribute::UVW)
			{
				memcpy(pDataLocation, &m_TexCoords[i], sizeof(glm::vec3));
				pDataLocation = (float*)pDataLocation + 3;
			}

			if (m_Attributes & (glm::uint)VertexBufferData::Attribute::COLOR_R32G32B32A32_SFLOAT)
			{
				memcpy(pDataLocation, &m_Colors[i], sizeof(glm::vec4));
				pDataLocation = (float*)pDataLocation + 4;
			}

			if (m_Attributes & (glm::uint)VertexBufferData::Attribute::TANGENT)
			{
				memcpy(pDataLocation, &m_Tangents[i], sizeof(glm::vec3));
				pDataLocation = (float*)pDataLocation + 3;
			}

			if (m_Attributes & (glm::uint)VertexBufferData::Attribute::BITANGENT)
			{
				memcpy(pDataLocation, &m_Bitangents[i], sizeof(glm::vec3));
				pDataLocation = (float*)pDataLocation + 3;
			}

			if (m_Attributes & (glm::uint)VertexBufferData::Attribute::NORMAL)
			{
				memcpy(pDataLocation, &m_Normals[i], sizeof(glm::vec3));
				pDataLocation = (float*)pDataLocation + 3;
			}

		}
	}
} // namespace flex
