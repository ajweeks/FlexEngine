#pragma once

#include "Graphics/RendererTypes.hpp" // For TopologyMode

#include "cgltf/cgltf.h" // for cgltf_result

namespace flex
{
	struct LoadedMesh;
	struct RenderObjectCreateInfo;
	struct VertexBufferDataCreateInfo;

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
			MEMORY,

			_NONE
		};

		Mesh(GameObject* owner);

		void PostInitialize();

		void Destroy();
		void Reload();

		bool LoadFromFile(
			const std::string& relativeFilePath,
			MaterialID materialID,
			bool bDynamic = false,
			MeshImportSettings* importSettings = nullptr,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr);

		bool LoadFromFile(
			const std::string& relativeFilePath,
			const std::vector<MaterialID>& inMaterialIDs,
			bool bDynamic = false,
			MeshImportSettings* importSettings = nullptr,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr);

		bool LoadFromMemory(const VertexBufferDataCreateInfo& vertexBufferCreateInfo,
			const std::vector<u32>& indices,
			MaterialID matID,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr);

		bool LoadFromMemoryDynamic(const VertexBufferDataCreateInfo& vertexBufferCreateInfo,
			const std::vector<u32>& indices,
			MaterialID matID,
			u32 initialMaxVertexCount,
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

		void SetTypeToMemory();

		std::vector<MaterialID> GetMaterialIDs() const;

		static Mesh* ParseJSON(const JSONObject& object, GameObject* owner, const std::vector<MaterialID>& inMaterialIDs);
		static Mesh* ImportFromFile(const std::string& meshFilePath, GameObject* owner);
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

		static LoadedMesh* LoadMesh(const std::string& relativeFilePath, MeshImportSettings* importSettings = nullptr);

		glm::vec3 m_MinPoint;
		glm::vec3 m_MaxPoint;

		real m_BoundingSphereRadius = 0.0f;
		glm::vec3 m_BoundingSphereCenterPoint;

		std::vector<MeshComponent*> m_Meshes;
	private:
		static bool CheckCGLTFResult(cgltf_result result, std::string& outErrorMessage);

		void CalculateBounds();

		bool LoadFromMemoryInternal(const VertexBufferDataCreateInfo& vertexBufferCreateInfo,
			const std::vector<u32>& indices,
			MaterialID matID,
			bool bDynamic,
			u32 initialMaxVertexCount,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr);


		Type m_Type = Type::_NONE;

		std::string m_RelativeFilePath;
		std::string m_FileName;

		// Saved so we can reload meshes and serialize contents to file
		MeshImportSettings m_ImportSettings = {};

		bool m_bInitialized = false;

		GameObject* m_OwningGameObject = nullptr;

	};
} // namespace flex
