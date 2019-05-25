#pragma once

#include "Graphics/RendererTypes.hpp"
#include "Graphics/VertexAttribute.hpp"
#include "Graphics/VertexBufferData.hpp"
#include "JSONTypes.hpp"
#include "Types.hpp"

namespace flex
{
	class GameObject;
	struct LoadedMesh;

	class MeshComponent
	{
	public:
		MeshComponent(GameObject* owner, MaterialID materialID = InvalidMaterialID, bool bSetRequiredAttributesFromMat = true);
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

			_NONE
		};

		enum class PrefabShape
		{
			CUBE,
			GRID,
			WORLD_AXIS_GROUND, // Two lines representing the world X and Z axis
			PLANE,
			UV_SPHERE,
			SKYBOX,
			GERSTNER_PLANE,

			_NONE
		};

		/*
		* Call before loading to force certain attributes to be filled/ignored based on shader
		* requirements. Any attribute not set here will be ignored. Any attribute set here will
		* be enforced (filled with default value if not present in mesh)
		*/
		void SetRequiredAttributesFromMaterialID(MaterialID matID);

		bool LoadFromFile(
			const std::string& relativeFilePath,
			MeshImportSettings* importSettings = nullptr,
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
		MeshImportSettings GetImportSettings() const;

		real GetScaledBoundingSphereRadius() const;
		glm::vec3 GetBoundingSphereCenterPointWS() const;

		VertexBufferData* GetVertexBufferData();

		glm::vec3 m_MinPoint;
		glm::vec3 m_MaxPoint;

		real m_BoundingSphereRadius = 0.0f;
		glm::vec3 m_BoundingSphereCenterPoint;


		// First field is relative file path (e.g. RESOURCE_LOCATION  "meshes/cube.glb")
		static std::map<std::string, LoadedMesh*> m_LoadedMeshes;

		static LoadedMesh* LoadMesh(const std::string& relativeFilePath, MeshImportSettings* importSettings = nullptr);
		static bool FindPreLoadedMesh(const std::string& relativeFilePath, LoadedMesh** loadedMesh);

	private:
		real CalculateBoundingSphereScale() const;
		bool CalculateTangents(VertexBufferData::CreateInfo& createInfo);

		GameObject* m_OwningGameObject = nullptr;

		bool m_bInitialized = false;

		MaterialID m_MaterialID = InvalidMaterialID;

		PrefabShape m_Shape = PrefabShape::_NONE;

		static const real GRID_LINE_SPACING;
		static const u32 GRID_LINE_COUNT;

		Type m_Type = Type::_NONE;
		std::string m_RelativeFilePath;
		std::string m_FileName;

		glm::vec2 m_UVScale = { 1,1 };

		VertexAttributes m_RequiredAttributes = (u32)VertexAttribute::_NONE;
		VertexBufferData m_VertexBufferData = {};

		std::vector<u32> m_Indices;

		// Saved so we can reload meshes and serialize contents to file
		MeshImportSettings m_ImportSettings = {};
		RenderObjectCreateInfo m_OptionalCreateInfo = {};

		static glm::vec4 m_DefaultColor_4;
		static glm::vec3 m_DefaultPosition;
		static glm::vec3 m_DefaultTangent;
		static glm::vec3 m_DefaultNormal;
		static glm::vec2 m_DefaultTexCoord;

	};
} // namespace flex
