#pragma once

#include "Scene/GameObject.hpp"

#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <assimp/scene.h>
#include <assimp/Importer.hpp>


#include "VertexAttribute.hpp"
#include "VertexBufferData.hpp"

namespace flex
{
	class MeshPrefab : public GameObject
	{
	public:
		MeshPrefab(const std::string& name = "");
		MeshPrefab(MaterialID materialID, const std::string& name = "");
		virtual ~MeshPrefab();

		static void Shutdown();

		enum class PrefabShape
		{
			CUBE,
			GRID,
			WORLD_AXIS_GROUND, // Two lines representing the world X and Z axis
			PLANE,
			UV_SPHERE,
			SKYBOX,
			NONE
		};

		void ForceAttributes(VertexAttributes attributes); // Call this before loading to force certain attributes to be filled
		void IgnoreAttributes(VertexAttributes attributes); // Call this before loading to ignore certain attributes

		/*
		* Loads a mesh from file
		*/
		bool LoadFromFile(const GameContext& gameContext, const std::string& filepath, bool flipNormalYZ = false, bool flipZ = false, bool flipU = false, bool flipV = false, RenderObjectCreateInfo* optionalCreateInfo = nullptr);

		/*
		* Loads a predefined shape
		* Optionally pass in createInfo values to be given to the renderer
		* when initializing the render object
		*/
		bool LoadPrefabShape(const GameContext& gameContext, PrefabShape shape, RenderObjectCreateInfo* optionalCreateInfo = nullptr);

		virtual void Initialize(const GameContext& gameContext) override;
		virtual void Update(const GameContext& gameContext) override;

		void SetMaterialID(MaterialID materialID, const GameContext& gameContext);
		void SetUVScale(real uScale, real vScale);

		static PrefabShape PrefabShapeFromString(const std::string& prefabName);

	private:
		struct LoadedMesh
		{
			LoadedMesh();

			Assimp::Importer importer = {};
			const aiScene* scene = nullptr;
		};
		static bool GetLoadedMesh(const std::string& filePath, const aiScene** scene);
		static std::map<std::string, LoadedMesh*> m_LoadedMeshes;

		bool m_Initialized = false;

		MaterialID m_MaterialID = InvalidMaterialID;

		static std::string m_DefaultName;
		std::string m_Name = "";

		PrefabShape m_Shape = PrefabShape::NONE;

		static const real GRID_LINE_SPACING;
		static const u32 GRID_LINE_COUNT;


		glm::vec2 m_UVScale = { 1,1 };

		VertexAttributes m_ForcedAttributes = (u32)VertexAttribute::NONE;
		VertexAttributes m_IgnoredAttributes = (u32)VertexAttribute::NONE;
		VertexBufferData m_VertexBufferData = {};

		std::vector<u32> m_Indices;

		static glm::vec4 m_DefaultColor_4;
		static glm::vec3 m_DefaultPosition;
		static glm::vec3 m_DefaultTangent;
		static glm::vec3 m_DefaultBitangent;
		static glm::vec3 m_DefaultNormal;
		static glm::vec2 m_DefaultTexCoord;

	};
} // namespace flex
