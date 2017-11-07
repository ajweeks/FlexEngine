#pragma once

#include "Scene/GameObject.hpp"

#include <vector>

#include <glm/integer.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <assimp/scene.h>
#include <assimp/Importer.hpp>

#include "Typedefs.hpp"
#include "VertexAttribute.hpp"
#include "VertexBufferData.hpp"

namespace flex
{
	class MeshPrefab : public GameObject
	{
	public:
		MeshPrefab(const std::string& name = "");
		MeshPrefab(MaterialID materialID, const std::string& name = "");
		~MeshPrefab();

		enum class PrefabShape
		{
			CUBE,
			GRID,
			PLANE,
			UV_SPHERE,
			SKYBOX
		};

		void ForceAttributes(VertexAttributes attributes); // Call this before loading to force certain attributes to be filled
		void IgnoreAttributes(VertexAttributes attributes); // Call this before loading to ignore certain attributes

		bool LoadFromFile(const GameContext& gameContext, const std::string& filepath, bool flipNormalYZ = false, bool flipZ = false, bool flipU = false, bool flipV = false);
		bool LoadPrefabShape(const GameContext& gameContext, PrefabShape shape);

		virtual void Initialize(const GameContext& gameContext) override;
		virtual void PostInitialize(const GameContext& gameContext) override;
		virtual void Update(const GameContext& gameContext) override;
		virtual void Destroy(const GameContext& gameContext) override;

		void SetMaterialID(MaterialID materialID, const GameContext& gameContext);
		void SetUVScale(float uScale, float vScale);

	private:
		struct LoadedMesh
		{
			Assimp::Importer importer;
			const aiScene* scene;
		};
		static bool GetLoadedMesh(const std::string& filePath, const aiScene** scene);
		static std::map<std::string, LoadedMesh> m_LoadedMeshes;

		bool m_Initialized = false;

		MaterialID m_MaterialID;

		static std::string m_DefaultName;
		std::string m_Name;

		glm::vec2 m_UVScale;

		VertexAttributes m_ForcedAttributes = (glm::uint)VertexAttribute::NONE;
		VertexAttributes m_IgnoredAttributes = (glm::uint)VertexAttribute::NONE;
		VertexBufferData m_VertexBufferData;

		std::vector<glm::uint> m_Indices;

		static glm::vec4 m_DefaultColor_4;
		static glm::vec3 m_DefaultPosition;
		static glm::vec3 m_DefaultTangent;
		static glm::vec3 m_DefaultBitangent;
		static glm::vec3 m_DefaultNormal;
		static glm::vec2 m_DefaultTexCoord;

	};
} // namespace flex
