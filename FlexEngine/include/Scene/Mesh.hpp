#pragma once

#include "Graphics/RendererTypes.hpp" // For TopologyMode

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

		struct CreateInfo
		{
			// Common fields
			std::vector<MaterialID> materialIDs;
			bool bDynamic = false;
			bool bCreateRenderObject = true;
			RenderObjectCreateInfo* optionalRenderObjectCreateInfo = nullptr;

			// Load from file fields
			bool bForceLoadMesh = false;
			std::string relativeFilePath;

			// Load from memory fields
			VertexBufferDataCreateInfo* vertexBufferCreateInfo = nullptr;
			u32 initialMaxVertexCount = 0;
			std::vector<u32> indices;
		};

		Mesh(GameObject* owner);

		void PostInitialize();

		void Destroy();
		void DestroyAllSubmeshes();
		void Reload();

		void RemoveSubmesh(u32 index);

		bool LoadFromFile(CreateInfo& createInfo);
		bool LoadFromFile(const std::string& filePath, MaterialID matID);
		bool LoadFromFile(const std::string& filePath, const std::vector<MaterialID>& matIDs);

		bool LoadFromMemory(const CreateInfo& createInfo);

		bool LoadPrefabShape(PrefabShape shape,
			MaterialID materialID,
			RenderObjectCreateInfo* optionalRenderObjectCreateInfo = nullptr,
			bool bCreateRenderObject = true);

		bool CreateProcedural(u32 initialMaxVertCount,
			VertexAttributes attributes,
			MaterialID materialID,
			TopologyMode topologyMode = TopologyMode::TRIANGLE_LIST,
			RenderObjectCreateInfo* optionalRenderObjectCreateInfo = nullptr);

		i32 AddSubMesh(MeshComponent* meshComponent);

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
		std::vector<MeshComponent*> GetSubMeshesCopy() const;
		MeshComponent* GetSubMesh(u32 index) const;

		GameObject* GetOwningGameObject() const;

		// Gets called when any mesh file gets modified
		void OnExternalMeshChange(const std::string& meshFilePath);

		static LoadedMesh* LoadMesh(const std::string& relativeFilePath);

		glm::vec3 m_MinPoint;
		glm::vec3 m_MaxPoint;

		real m_BoundingSphereRadius = 0.0f;
		glm::vec3 m_BoundingSphereCenterPoint;

		std::vector<MeshComponent*> m_Meshes;
	private:
		void CalculateBounds();

		Type m_Type = Type::_NONE;

		std::string m_RelativeFilePath;
		std::string m_FileName;

		bool m_bInitialized = false;

		GameObject* m_OwningGameObject = nullptr;

	};
} // namespace flex
