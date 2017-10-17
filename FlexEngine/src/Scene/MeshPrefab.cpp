#include "stdafx.hpp"

#include "Scene/MeshPrefab.hpp"

#include <assimp/Importer.hpp>
#include <assimp/vector3.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <glm/gtc/matrix_transform.hpp>

#include "Colors.hpp"
#include "GameContext.hpp"
#include "Helpers.hpp"
#include "Logger.hpp"

namespace flex
{
	std::string MeshPrefab::m_DefaultName = "Game Object";
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
		m_MaterialID(materialID),
		m_Name(name),
		m_UVScale(1.0f, 1.0f)
	{
		if (name.empty()) m_Name = m_DefaultName;
	}

	MeshPrefab::~MeshPrefab()
	{
		m_VertexBufferData.Destroy();
	}

	void MeshPrefab::ForceAttributes(VertexAttributes attributes)
	{
		m_ForcedAttributes |= attributes;
	}

	void MeshPrefab::IgnoreAttributes(VertexAttributes attributes)
	{
		m_IgnoredAttributes |= attributes;
	}

	// TODO: Add option to force certain components (Bitangents, UVs, ...)
	bool MeshPrefab::LoadFromFile(const GameContext& gameContext, const std::string& filepath, bool flipNormalYZ, bool flipZ, bool flipU, bool flipV)
	{
		VertexBufferData::CreateInfo vertexBufferDataCreateInfo = {};

		Assimp::Importer importer;

		std::string fileName = filepath;
		StripLeadingDirectories(fileName);
		Logger::LogInfo("Loading mesh " + fileName);

		const aiScene* pScene = importer.ReadFile(filepath,
			aiProcess_FindInvalidData |
			aiProcess_GenNormals |
			aiProcess_CalcTangentSpace
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
			vertexBufferDataCreateInfo.positions_3D.push_back(pos);
			vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::POSITION;

			// Color
			if (mesh->HasVertexColors(0))
			{
				glm::vec4 col = ToVec4(mesh->mColors[0][i]);
				vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(col);
				vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;
			}
			else if (m_ForcedAttributes & (glm::uint)VertexAttribute::COLOR_R32G32B32A32_SFLOAT)
			{
				vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(m_DefaultColor_4);
				vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;
			}

			// Tangent & Bitangent
			if (mesh->HasTangentsAndBitangents())
			{
				glm::vec3 tangent = ToVec3(mesh->mTangents[i]);
				vertexBufferDataCreateInfo.tangents.push_back(tangent);
				vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::TANGENT;
			
				glm::vec3 bitangent = ToVec3(mesh->mBitangents[i]);
				vertexBufferDataCreateInfo.bitangents.push_back(bitangent);
				vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::BITANGENT;
			}
			else 
			{
				if (m_ForcedAttributes & (glm::uint)VertexAttribute::TANGENT)
				{
					vertexBufferDataCreateInfo.tangents.push_back(m_DefaultTangent);
					vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::TANGENT;
				}
				if (m_ForcedAttributes & (glm::uint)VertexAttribute::BITANGENT)
				{
					vertexBufferDataCreateInfo.bitangents.push_back(m_DefaultBitangent);
					vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::BITANGENT;
				}
			}

			// Normal
			if (mesh->HasNormals())
			{
				glm::vec3 norm = ToVec3(mesh->mNormals[i]);
				if (flipNormalYZ) std::swap(norm.y, norm.z);
				if (flipZ) norm.z = -norm.z;
				vertexBufferDataCreateInfo.normals.push_back(norm);
				vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::NORMAL;
			}
			else if (m_ForcedAttributes & (glm::uint)VertexAttribute::NORMAL)
			{
				vertexBufferDataCreateInfo.normals.push_back(m_DefaultNormal);
				vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::NORMAL;
			}

			// TexCoord
			if (mesh->HasTextureCoords(0))
			{
				// Truncate w component
				glm::vec2 texCoord = (glm::vec2)(ToVec3(mesh->mTextureCoords[0][i]));
				texCoord *= m_UVScale;
				if (flipU) texCoord.x = 1.0f - texCoord.x;
				if (flipV) texCoord.y = 1.0f - texCoord.y;
				vertexBufferDataCreateInfo.texCoords_UV.push_back(texCoord);
				vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::UV;
			}
			else if (m_ForcedAttributes & (glm::uint)VertexAttribute::UV)
			{
				vertexBufferDataCreateInfo.texCoords_UV.push_back(m_DefaultTexCoord);
				vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::UV;
			}
		}
		
		m_VertexBufferData.Initialize(&vertexBufferDataCreateInfo);

		Renderer::RenderObjectCreateInfo createInfo = {};
		createInfo.vertexBufferData = &m_VertexBufferData;
		createInfo.materialID = m_MaterialID;
		createInfo.name = m_Name;
		createInfo.transform = &m_Transform;

		m_RenderID = gameContext.renderer->InitializeRenderObject(gameContext, &createInfo);

		gameContext.renderer->SetTopologyMode(m_RenderID, Renderer::TopologyMode::TRIANGLE_LIST);

		m_VertexBufferData.DescribeShaderVariables(gameContext.renderer, m_RenderID);

		return true;
	}

	bool MeshPrefab::LoadPrefabShape(const GameContext& gameContext, PrefabShape shape)
	{
		Renderer::RenderObjectCreateInfo renderObjectCreateInfo = {};
		renderObjectCreateInfo.materialID = m_MaterialID;
		renderObjectCreateInfo.transform = &m_Transform;

		Renderer::TopologyMode topologyMode = Renderer::TopologyMode::TRIANGLE_LIST;

		VertexBufferData::CreateInfo vertexBufferDataCreateInfo = {};

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
			vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::POSITION;

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
			vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;

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
			vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::NORMAL;

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
			vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::UV;

			renderObjectCreateInfo.name = "Cube";
		} break;
		case MeshPrefab::PrefabShape::GRID:
		{
			float rowWidth = 10.0f;
			glm::uint lineCount = 15;

			const glm::vec4 lineColor = Color::GRAY;
			const glm::vec4 centerLineColor = Color::LIGHT_GRAY;

			const size_t vertexCount = lineCount * 2 * 2;
			vertexBufferDataCreateInfo.positions_3D.reserve(vertexCount);
			vertexBufferDataCreateInfo.colors_R32G32B32A32.reserve(vertexCount);

			vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::POSITION;
			vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;

			float halfWidth = (rowWidth * (lineCount - 1)) / 2.0f;

			// Horizontal lines
			for (glm::uint i = 0; i < lineCount; ++i)
			{
				vertexBufferDataCreateInfo.positions_3D.push_back({ i * rowWidth - halfWidth, 0.0f, -halfWidth });
				vertexBufferDataCreateInfo.positions_3D.push_back({ i * rowWidth - halfWidth, 0.0f, halfWidth });

				const glm::vec4 color = (i == lineCount / 2 ? centerLineColor : lineColor);
				vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(color);
				vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(color);
			}

			// Vertical lines
			for (glm::uint i = 0; i < lineCount; ++i)
			{
				vertexBufferDataCreateInfo.positions_3D.push_back({ -halfWidth, 0.0f, i * rowWidth - halfWidth });
				vertexBufferDataCreateInfo.positions_3D.push_back({ halfWidth, 0.0f, i * rowWidth - halfWidth });

				const glm::vec4 color = (i == lineCount / 2 ? centerLineColor : lineColor);
				vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(color);
				vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(color);
			}

			topologyMode = Renderer::TopologyMode::LINE_LIST;
			renderObjectCreateInfo.name = "Grid";
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
			vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::POSITION;

			vertexBufferDataCreateInfo.normals =
			{
				{ 0.0f, 1.0f, 0.0f, },
				{ 0.0f, 1.0f, 0.0f, },
				{ 0.0f, 1.0f, 0.0f, },

				{ 0.0f, 1.0f, 0.0f, },
				{ 0.0f, 1.0f, 0.0f, },
				{ 0.0f, 1.0f, 0.0f, },
			};
			vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::NORMAL;

			vertexBufferDataCreateInfo.bitangents =
			{
				{ 1.0f, 0.0f, 0.0f, },
				{ 1.0f, 0.0f, 0.0f, },
				{ 1.0f, 0.0f, 0.0f, },

				{ 1.0f, 0.0f, 0.0f, },
				{ 1.0f, 0.0f, 0.0f, },
				{ 1.0f, 0.0f, 0.0f, },
			};
			vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::BITANGENT;

			vertexBufferDataCreateInfo.tangents =
			{
				{ 0.0f, 0.0f, 1.0f, },
				{ 0.0f, 0.0f, 1.0f, },
				{ 0.0f, 0.0f, 1.0f, },

				{ 0.0f, 0.0f, 1.0f, },
				{ 0.0f, 0.0f, 1.0f, },
				{ 0.0f, 0.0f, 1.0f, },
			};
			vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::TANGENT;

			vertexBufferDataCreateInfo.colors_R32G32B32A32 =
			{
				{ 1.0f, 1.0f, 1.0f, 1.0f },
				{ 1.0f, 1.0f, 1.0f, 1.0f },
				{ 1.0f, 1.0f, 1.0f, 1.0f },

				{ 1.0f, 1.0f, 1.0f, 1.0f },
				{ 1.0f, 1.0f, 1.0f, 1.0f },
				{ 1.0f, 1.0f, 1.0f, 1.0f },
			};
			vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;

			vertexBufferDataCreateInfo.texCoords_UV =
			{
				{ 0.0f, 0.0f },
				{ 0.0f, 1.0f },
				{ 1.0f, 1.0f },

				{ 0.0f, 0.0f },
				{ 1.0f, 1.0f },
				{ 1.0f, 0.0f },
			};
			vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::UV;

			renderObjectCreateInfo.name = "Plane";
		} break;
		case MeshPrefab::PrefabShape::UV_SPHERE:
		{
			// Vertices
			glm::vec3 v1(0.0f, 1.0f, 0.0f); // Top vertex
			vertexBufferDataCreateInfo.positions_3D.push_back(v1);
			vertexBufferDataCreateInfo.colors_R32G32B32A32.push_back(Color::RED);
			vertexBufferDataCreateInfo.texCoords_UV.push_back({ 0.0f, 0.0f });
			vertexBufferDataCreateInfo.normals.push_back({ 0.0f, 1.0f, 0.0f });

			vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::POSITION;
			vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;
			vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::UV;
			vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::NORMAL;

			glm::uint parallelCount = 10;
			glm::uint meridianCount = 5;

			assert(parallelCount > 0 && meridianCount > 0);
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

			const glm::uint numVerts = vertexBufferDataCreateInfo.positions_3D.size();

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
			vertexBufferDataCreateInfo.attributes |= (glm::uint)VertexAttribute::POSITION;

			renderObjectCreateInfo.cullFace = Renderer::CullFace::FRONT;
			renderObjectCreateInfo.name = "Skybox";

		} break;
		default:
		{
			Logger::LogWarning("Unhandled prefab shape passed to MeshPrefab::LoadPrefabShape: " + std::to_string((int)shape));
			return false;
		} break;
		}

		m_VertexBufferData.Initialize(&vertexBufferDataCreateInfo);

		renderObjectCreateInfo.vertexBufferData = &m_VertexBufferData;
		if (!m_Name.empty() && m_Name.compare(m_DefaultName) != 0) renderObjectCreateInfo.name = m_Name;

		m_RenderID = gameContext.renderer->InitializeRenderObject(gameContext, &renderObjectCreateInfo);

		gameContext.renderer->SetTopologyMode(m_RenderID, topologyMode);
		m_VertexBufferData.DescribeShaderVariables(gameContext.renderer, m_RenderID);

		return true;
	}

	void MeshPrefab::Initialize(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
	}

	void MeshPrefab::PostInitialize(const GameContext& gameContext)
	{
		gameContext.renderer->PostInitializeRenderObject(gameContext, m_RenderID);
	}

	void MeshPrefab::Update(const GameContext& gameContext)
	{
		glm::mat4 model = m_Transform.GetModelMatrix();
		gameContext.renderer->UpdateTransformMatrix(gameContext, m_RenderID, model);
	}

	void MeshPrefab::Destroy(const GameContext& gameContext)
	{
		gameContext.renderer->Destroy(m_RenderID);
	}

	void MeshPrefab::SetMaterialID(MaterialID materialID)
	{
		m_MaterialID = materialID;
	}

	void MeshPrefab::SetUVScale(float uScale, float vScale)
	{
		m_UVScale = glm::vec2(uScale, vScale);
	}
} // namespace flex
