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
			bool bCreateRenderObject = true,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr);

		bool LoadFromFile(
			const std::string& relativeFilePath,
			const std::vector<MaterialID>& inMaterialIDs,
			bool bDynamic = false,
			bool bCreateRenderObject = true,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr);

		bool LoadFromMemory(const VertexBufferDataCreateInfo& vertexBufferCreateInfo,
			const std::vector<u32>& indices,
			MaterialID matID,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr,
			bool bCreateRenderObject = true);

		bool LoadFromMemoryDynamic(const VertexBufferDataCreateInfo& vertexBufferCreateInfo,
			const std::vector<u32>& indices,
			MaterialID matID,
			u32 initialMaxVertexCount,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr,
			bool bCreateRenderObject = true);

		bool LoadPrefabShape(PrefabShape shape,
			MaterialID materialID,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr,
			bool bCreateRenderObject = true);

		bool CreateProcedural(u32 initialMaxVertCount,
			VertexAttributes attributes,
			MaterialID materialID,
			TopologyMode topologyMode = TopologyMode::TRIANGLE_LIST,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr);

		void SetOwner(GameObject* owner);

		// Returns true if any property changed
		bool DrawImGui();

		void SetTypeToMemory();

		std::vector<MaterialID> GetMaterialIDs() const;

		static Mesh* ImportFromFile(const std::string& meshFilePath, GameObject* owner, bool bCreateRenderObject = true);
		static Mesh* ImportFromFile(const std::string& meshFilePath, GameObject* owner, const std::vector<MaterialID>& materialIDs, bool bCreateRenderObject = true);
		static Mesh* ImportFromPrefab(const std::string& prefabName, GameObject* owner, const std::vector<MaterialID>& materialIDs, bool bCreateRenderObject = true);

		Type GetType() const;
		u32 GetSubmeshCount() const;

		MaterialID GetMaterialID(u32 slotIndex);
		RenderID GetRenderID(u32 slotIndex);

		std::string GetRelativeFilePath() const;
		std::string GetFileName() const;

		MeshComponent* GetSubMeshWithRenderID(RenderID renderID) const;
		std::vector<MeshComponent*> GetSubMeshes() const;
		MeshComponent* GetSubMesh(u32 index) const;

		GameObject* GetOwningGameObject() const;

		static LoadedMesh* LoadMesh(const std::string& relativeFilePath);

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
			RenderObjectCreateInfo* optionalCreateInfo,
			bool bCreateRenderObject);


		Type m_Type = Type::_NONE;

		std::string m_RelativeFilePath;
		std::string m_FileName;

		bool m_bInitialized = false;

		GameObject* m_OwningGameObject = nullptr;

	};
} // namespace flex
