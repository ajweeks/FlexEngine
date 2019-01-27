#pragma once

#include <vector>
#include <map>

#pragma warning(push, 0)
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf/tiny_gltf.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#pragma warning(pop)

#include "Graphics/RendererTypes.hpp"
#include "JSONTypes.hpp"
#include "Graphics/VertexAttribute.hpp"
#include "Graphics/VertexBufferData.hpp"

namespace flex
{
	class GameObject;

	class MeshComponent
	{
	public:
		MeshComponent(GameObject* owner);
		MeshComponent(MaterialID materialID, GameObject* owner, bool bSetRequiredAttributesFromMat = true);
		~MeshComponent();

		static void DestroyAllLoadedMeshes();

		static MeshComponent* ParseJSON(const JSONObject& object, GameObject* owner, MaterialID materialID);
		JSONObject Serialize() const;

		void Update();
		void Destroy();

		void SetOwner(GameObject* owner);

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
		void SetRequiredAttributesFromMaterialID(MaterialID matID);

		bool LoadFromFile(
			const std::string& relativeFilePath,
			ImportSettings* importSettings = nullptr,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr);

		bool LoadPrefabShape(PrefabShape shape,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr);

		void Reload();

		MaterialID GetMaterialID() const;
		void SetMaterialID(MaterialID materialID);
		void SetUVScale(real uScale, real vScale);

		static PrefabShape StringToPrefabShape(const std::string& prefabName);
		static std::string PrefabShapeToString(PrefabShape shape);

		Type GetType() const;

		std::string GetRelativeFilePath() const;
		std::string GetFileName() const;
		PrefabShape GetShape() const;
		ImportSettings GetImportSettings() const;

		real GetScaledBoundingSphereRadius() const;
		glm::vec3 GetBoundingSphereCenterPointWS() const;

		glm::vec3 m_MinPoint;
		glm::vec3 m_MaxPoint;

		real m_BoundingSphereRadius = 0.0f;
		glm::vec3 m_BoundingSphereCenterPoint;

		struct LoadedMesh
		{
			std::string relativeFilePath;
			ImportSettings importSettings;
			tinygltf::Model model;
			tinygltf::TinyGLTF loader;
		};
		// First field is relative file path (e.g. RESOURCE_LOCATION  "meshes/cube.glb")
		static std::map<std::string, LoadedMesh*> m_LoadedMeshes;

		static bool GetLoadedMesh(const std::string& relativeFilePath, LoadedMesh** loadedMesh);

		static LoadedMesh* LoadMesh(const std::string& relativeFilePath, ImportSettings* importSettings = nullptr);

	private:
		real CalculateBoundingSphereScale() const;

		bool CalculateTangents(VertexBufferData::CreateInfo& createInfo, const tinygltf::Primitive& primitive);

		GameObject* m_OwningGameObject = nullptr;

		bool m_bInitialized = false;

		MaterialID m_MaterialID = InvalidMaterialID;

		PrefabShape m_Shape = PrefabShape::NONE;

		static const real GRID_LINE_SPACING;
		static const u32 GRID_LINE_COUNT;

		Type m_Type = Type::NONE;
		std::string m_RelativeFilePath;
		std::string m_FileName;

		glm::vec2 m_UVScale = { 1,1 };

		VertexAttributes m_RequiredAttributes = (u32)VertexAttribute::NONE;
		VertexBufferData m_VertexBufferData = {};

		std::vector<u32> m_Indices;

		// Saved so we can reload meshes and serialize contents to file
		ImportSettings m_ImportSettings = {};
		RenderObjectCreateInfo m_OptionalCreateInfo = {};

		static glm::vec4 m_DefaultColor_4;
		static glm::vec3 m_DefaultPosition;
		static glm::vec3 m_DefaultTangent;
		static glm::vec3 m_DefaultBitangent;
		static glm::vec3 m_DefaultNormal;
		static glm::vec2 m_DefaultTexCoord;

	};
} // namespace flex
