#pragma once

#include "Graphics/RendererTypes.hpp"
#include "Graphics/VertexAttribute.hpp"
#include "Graphics/VertexBufferData.hpp"
#include "JSONTypes.hpp"
#include "Types.hpp"

struct cgltf_data;
enum cgltf_result;

namespace flex
{
	class GameObject;
	struct LoadedMesh;

	class MeshComponent
	{
	public:
		enum class Type
		{
			PREFAB,
			FILE,
			PROCEDURAL,

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

		MeshComponent(GameObject* owner, MaterialID materialID = InvalidMaterialID, bool bSetRequiredAttributesFromMat = true);
		~MeshComponent();

		static void DestroyAllLoadedMeshes();

		static MeshComponent* ParseJSON(const JSONObject& object, GameObject* owner, MaterialID materialID);
		JSONObject Serialize() const;

		void Update();

		void UpdateProceduralData(VertexBufferDataCreateInfo const* newData);

		void Destroy();

		void SetOwner(GameObject* owner);

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

		bool CreateProcedural(u32 initialMaxVertCount,
			VertexAttributes attributes,
			TopologyMode topologyMode = TopologyMode::TRIANGLE_LIST,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr);

		void Reload();

		MaterialID GetMaterialID() const;
		void SetMaterialID(MaterialID materialID);
		void SetUVScale(real uScale, real vScale);

		static PrefabShape StringToPrefabShape(const std::string& prefabName);
		static std::string PrefabShapeToString(PrefabShape shape);

		static bool FindPreLoadedMesh(const std::string& relativeFilePath, LoadedMesh** loadedMesh);
		static LoadedMesh* LoadMesh(const std::string& relativeFilePath, MeshImportSettings* importSettings = nullptr);

		Type GetType() const;

		std::string GetRelativeFilePath() const;
		std::string GetFileName() const;
		PrefabShape GetShape() const;
		MeshImportSettings GetImportSettings() const;

		real GetScaledBoundingSphereRadius() const;
		glm::vec3 GetBoundingSphereCenterPointWS() const;

		VertexBufferData* GetVertexBufferData();

		// First field is relative file path (e.g. RESOURCE_LOCATION  "meshes/cube.glb")
		static std::map<std::string, LoadedMesh*> m_LoadedMeshes;

		glm::vec3 m_MinPoint;
		glm::vec3 m_MaxPoint;

		real m_BoundingSphereRadius = 0.0f;
		glm::vec3 m_BoundingSphereCenterPoint;

	private:
		real CalculateBoundingSphereScale() const;
		bool CalculateTangents(VertexBufferDataCreateInfo& createInfo);

		void CopyInOptionalCreateInfo(RenderObjectCreateInfo& createInfo, const RenderObjectCreateInfo& overrides);

		static bool CheckCGLTFResult(cgltf_result result, std::string& outErrorMessage);

		static const real GRID_LINE_SPACING;
		static const u32 GRID_LINE_COUNT;

		static glm::vec4 m_DefaultColor_4;
		static glm::vec3 m_DefaultPosition;
		static glm::vec3 m_DefaultTangent;
		static glm::vec3 m_DefaultNormal;
		static glm::vec2 m_DefaultTexCoord;

		GameObject* m_OwningGameObject = nullptr;

		bool m_bInitialized = false;

		MaterialID m_MaterialID = InvalidMaterialID;

		PrefabShape m_Shape = PrefabShape::_NONE;

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

	};
} // namespace flex
