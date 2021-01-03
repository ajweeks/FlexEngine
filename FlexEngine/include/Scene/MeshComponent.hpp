#pragma once

#include "Graphics/RendererTypes.hpp"
#include "Graphics/VertexAttribute.hpp"
#include "Graphics/VertexBufferData.hpp"
#include "JSONTypes.hpp"
#include "Types.hpp"

struct cgltf_data;
struct cgltf_primitive;

namespace flex
{
	class Mesh;

	enum class PrefabShape
	{
		CUBE,
		GRID,
		WORLD_AXIS_GROUND, // Two lines representing the world X and Z axis
		PLANE,
		UV_SPHERE,
		SKYBOX,

		_NONE
	};

	class MeshComponent
	{
	public:

		MeshComponent(Mesh* owner, MaterialID materialID = InvalidMaterialID, bool bSetRequiredAttributesFromMat = true);
		~MeshComponent();

		void PostInitialize();

		void Update();
		void UpdateDynamicVertexData(const VertexBufferDataCreateInfo& newData, const std::vector<u32>& indexData);

		void Destroy();
		void SetOwner(Mesh* owner);

		/*
		* Call before loading to force certain attributes to be filled/ignored based on shader
		* requirements. Any attribute not set here will be ignored. Any attribute set here will
		* be enforced (filled with default value if not present in mesh)
		*/
		void SetRequiredAttributesFromMaterialID(MaterialID matID);

		static MeshComponent* LoadFromCGLTF(Mesh* owningMesh,
			cgltf_primitive* primitive,
			MaterialID materialID,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr,
			bool bCreateRenderObject = true);

		static MeshComponent* LoadFromCGLTFDynamic(Mesh* owningMesh,
			cgltf_primitive* primitive,
			MaterialID materialID,
			u32 initialMaxVertexCount = u32_max,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr,
			bool bCreateRenderObject = true);

		static MeshComponent* LoadFromMemory(Mesh* owningMesh,
			const VertexBufferDataCreateInfo& vertexBufferCreateInfo,
			const std::vector<u32>& indices,
			MaterialID materialID,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr,
			bool bCreateRenderObject = true);

		static MeshComponent* LoadFromMemoryDynamic(Mesh* owningMesh,
			const VertexBufferDataCreateInfo& vertexBufferCreateInfo,
			const std::vector<u32>& indices,
			MaterialID materialID,
			u32 initialMaxVertexCount,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr,
			bool bCreateRenderObject = true);

		bool LoadPrefabShape(PrefabShape shape,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr,
			bool bCreateRenderObject = true);

		bool CreateProcedural(u32 initialMaxVertCount,
			VertexAttributes attributes,
			TopologyMode topologyMode = TopologyMode::TRIANGLE_LIST,
			RenderObjectCreateInfo* optionalCreateInfo = nullptr);

		MaterialID GetMaterialID() const;
		void SetMaterialID(MaterialID materialID);
		void SetUVScale(real uScale, real vScale);

		bool IsInitialized() const;

		static PrefabShape StringToPrefabShape(const std::string& prefabName);
		static std::string PrefabShapeToString(PrefabShape shape);

		static bool CalculateTangents(VertexBufferDataCreateInfo& createInfo, const std::vector<u32>& indices);

		PrefabShape GetShape() const;
		Mesh* GetOwner();

		real GetScaledBoundingSphereRadius() const;
		glm::vec3 GetBoundingSphereCenterPointWS() const;

		VertexBufferData* GetVertexBufferData();
		std::vector<u32> GetIndexBuffer();

		real GetVertexBufferUsage() const;

		glm::vec3 m_MinPoint;
		glm::vec3 m_MaxPoint;

		real m_BoundingSphereRadius = 0.0f;
		glm::vec3 m_BoundingSphereCenterPoint;

		RenderID renderID = InvalidRenderID;

	private:
		static MeshComponent* LoadFromMemoryInternal(Mesh* owningMesh,
			const VertexBufferDataCreateInfo& vertexBufferCreateInfo,
			const std::vector<u32>& indices,
			MaterialID materialID,
			bool bDyanmic,
			u32 initialMaxDynamicVertexCount,
			RenderObjectCreateInfo* optionalCreateInfo,
			bool bCreateRenderObject);

		static MeshComponent* LoadFromCGLTFInternal(Mesh* owningMesh,
			cgltf_primitive* primitive,
			MaterialID materialID,
			bool bDynamic,
			u32 initialMaxDynamicVertexCount,
			RenderObjectCreateInfo* optionalCreateInfo,
			bool bCreateRenderObject);

		real CalculateBoundingSphereScale() const;

		void CalculateBoundingSphereRadius(const std::vector<glm::vec3>& positions);

		void CopyInOptionalCreateInfo(RenderObjectCreateInfo& createInfo, const RenderObjectCreateInfo& overrides);


		static const real GRID_LINE_SPACING;
		static const u32 GRID_LINE_COUNT;

		static glm::vec4 m_DefaultColour_4;
		static glm::vec3 m_DefaultPosition;
		static glm::vec3 m_DefaultTangent;
		static glm::vec3 m_DefaultNormal;
		static glm::vec2 m_DefaultTexCoord;

		Mesh* m_OwningMesh = nullptr;

		bool m_bInitialized = false;

		MaterialID m_MaterialID = InvalidMaterialID;

		PrefabShape m_Shape = PrefabShape::_NONE;

		glm::vec2 m_UVScale = { 1, 1 };

		VertexAttributes m_RequiredAttributes = (u32)VertexAttribute::_NONE;
		VertexBufferData m_VertexBufferData = {};

		std::vector<u32> m_Indices;

	};
} // namespace flex
