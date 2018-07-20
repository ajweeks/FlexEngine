#pragma once

#include <vector>

#pragma warning(push, 0)
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#pragma warning(pop)

#include "VertexAttribute.hpp"
#include "VertexBufferData.hpp"
#include "JSONTypes.hpp"

namespace flex
{
	class GameObject;

	class MeshComponent
	{
	public:
		MeshComponent(GameObject* owner);
		MeshComponent(MaterialID materialID, GameObject* owner);
		~MeshComponent();

		static void DestroyAllLoadedMeshes();

		static MeshComponent* ParseJSON(const JSONObject& object, GameObject* owner, MaterialID materialID);

		void Update();
		void Destroy();

		enum class Type
		{
			PREFAB,
			FILE,
			NONE
		};

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

		struct ImportSettings
		{
			/* Whether or not to invert the horizontal texture coordinate */
			bool flipU = false;
			/* Whether or not to invert the vertical texture coordinate */
			bool flipV = false;
			/* Whether or not to invert the Z component (up) of all normals */
			bool flipNormalZ = false;
			/* Whether or not to swap Y and Z components of all normals (converts from Y-up to Z-up) */
			bool swapNormalYZ = false;
		};

		/*
		 * Call before loading to force certain attributes to be filled/ignored based on shader
		 * requirements. Any attribute not set here will be ignored. Any attribute set here will
		 * be enforced (filled with default value if not present in mesh)
		*/
		void SetRequiredAttributes(VertexAttributes requiredAttributes);

		/*
		* Loads a mesh from file
		*/
		bool LoadFromFile(
			const std::string& filePath,
			ImportSettings* importSettings = nullptr,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr);

		/*
		* Loads a predefined shape
		* Optionally pass in createInfo values to be given to the renderer
		* when initializing the render object
		*/
		bool LoadPrefabShape(PrefabShape shape, 
			RenderObjectCreateInfo* optionalCreateInfo = nullptr);

		MaterialID GetMaterialID() const;
		void SetMaterialID(MaterialID materialID);
		void SetUVScale(real uScale, real vScale);

		static PrefabShape StringToPrefabShape(const std::string& prefabName);
		static std::string PrefabShapeToString(PrefabShape shape);

		Type GetType() const;

		std::string GetFilepath() const;
		PrefabShape GetShape() const;
		ImportSettings GetImportSettings() const;

		real GetScaledBoundingSphereRadius() const;
		glm::vec3 GetBoundingSphereCenterPointWS() const;

		glm::vec3 m_MinPoint;
		glm::vec3 m_MaxPoint;

		real m_BoundingSphereRadius = 0.0f;
		glm::vec3 m_BoundingSphereCenterPoint;

	private:
		struct LoadedMesh
		{
			Assimp::Importer importer = {};
			const aiScene* scene = nullptr;
		};
		static bool GetLoadedMesh(const std::string& filePath, const aiScene** scene);
		static std::map<std::string, LoadedMesh*> m_LoadedMeshes;

		real CalculateBoundingSphereScale() const;

		GameObject* m_OwningGameObject = nullptr;

		bool m_Initialized = false;

		MaterialID m_MaterialID = InvalidMaterialID;

		PrefabShape m_Shape = PrefabShape::NONE;

		static const real GRID_LINE_SPACING;
		static const u32 GRID_LINE_COUNT;

		Type m_Type = Type::NONE;
		std::string m_FilePath;

		glm::vec2 m_UVScale = { 1,1 };

		VertexAttributes m_RequiredAttributes = (u32)VertexAttribute::NONE;
		VertexBufferData m_VertexBufferData = {};

		std::vector<u32> m_Indices;

		// Saved so we can reload meshes and serialize contents to file
		ImportSettings m_ImportSettings = {};

		static glm::vec4 m_DefaultColor_4;
		static glm::vec3 m_DefaultPosition;
		static glm::vec3 m_DefaultTangent;
		static glm::vec3 m_DefaultBitangent;
		static glm::vec3 m_DefaultNormal;
		static glm::vec2 m_DefaultTexCoord;

	};
} // namespace flex
