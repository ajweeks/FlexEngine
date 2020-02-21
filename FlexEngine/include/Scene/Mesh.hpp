#pragma once

#include "Graphics/RendererTypes.hpp" // For TopologyMode

#include "cgltf/cgltf.h" // for cgltf_result

namespace flex
{
	struct LoadedMesh;
	struct RenderObjectCreateInfo;

	class MeshComponent;
	enum class PrefabShape;

	class Mesh
	{
	public:
		enum class Type
		{
			PREFAB,
			FILE,
			PROCEDURAL,

			_NONE
		};

		Mesh(GameObject* owner);

		void PostInitialize();

		void Destroy();
		void Reload();

		bool LoadFromFile(
			const std::string& relativeFilePath,
			MaterialID materialID,
			MeshImportSettings* importSettings = nullptr,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr);

		bool LoadFromFile(
			const std::string& relativeFilePath,
			const std::vector<MaterialID>& inMaterialIDs,
			MeshImportSettings* importSettings = nullptr,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr);

		bool LoadPrefabShape(PrefabShape shape,
			MaterialID materialID,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr);

		bool CreateProcedural(u32 initialMaxVertCount,
			VertexAttributes attributes,
			MaterialID materialID,
			TopologyMode topologyMode = TopologyMode::TRIANGLE_LIST,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr);

		void SetOwner(GameObject* owner);

		void DrawImGui();

		std::vector<MaterialID> GetMaterialIDs() const;

		static Mesh* ParseJSON(const JSONObject& object, GameObject* owner, const std::vector<MaterialID>& inMaterialIDs);
		JSONObject Serialize() const;

		Type GetType() const;
		u32 GetSubmeshCount() const;

		MaterialID GetMaterialID(u32 slotIndex);
		RenderID GetRenderID(u32 slotIndex);

		std::string GetRelativeFilePath() const;
		std::string GetFileName() const;

		MeshImportSettings GetImportSettings() const;

		MeshComponent* GetSubMeshWithRenderID(RenderID renderID) const;
		std::vector<MeshComponent*> GetSubMeshes() const;

		GameObject* GetOwningGameObject() const;

		static bool FindPreLoadedMesh(const std::string& relativeFilePath, LoadedMesh** loadedMesh);
		static LoadedMesh* LoadMesh(const std::string& relativeFilePath, MeshImportSettings* importSettings = nullptr);

		// First field is relative file path (e.g. RESOURCE_LOCATION "meshes/cube.glb")
		static std::map<std::string, LoadedMesh*> m_LoadedMeshes;

		glm::vec3 m_MinPoint;
		glm::vec3 m_MaxPoint;

		real m_BoundingSphereRadius = 0.0f;
		glm::vec3 m_BoundingSphereCenterPoint;

	private:
		void CalculateBounds();
		static bool CheckCGLTFResult(cgltf_result result, std::string& outErrorMessage);

		std::vector<MeshComponent*> m_Meshes;

		Type m_Type = Type::_NONE;

		std::string m_RelativeFilePath;
		std::string m_FileName;

		// Saved so we can reload meshes and serialize contents to file
		MeshImportSettings m_ImportSettings = {};

		bool m_bInitialized = false;

		GameObject* m_OwningGameObject = nullptr;

	};
} // namespace flex
