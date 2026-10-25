#pragma once

#include "Scene/GameObject.hpp"
#include "Track/BezierCurve3D.hpp"

// Procedural road meshes following bezier curves
namespace flex
{
	struct RoadSegment
	{
		BezierCurve3D curve;
		real widthStart;
		real widthEnd;
		AABB aabb;
		MeshComponent* mesh = nullptr;

		bool Overlaps(const glm::vec2& point);
		real SignedDistanceTo(const glm::vec3& point, glm::vec3& outClosestPoint);
		void ComputeAABB();
	};

	class Road : public GameObject
	{
	public:
		Road(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual void Initialize() override;
		virtual void PostInitialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;
		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		void RegenerateMesh();

		std::vector<RoadSegment> roadSegments;

	private:
		void GenerateMesh();
		void GenerateSegment(i32 index);
		void GenerateMaterial();
		void GenerateSegmentsThroughPoints(const std::vector<glm::vec3>& points);

		void CreateRigidBody(u32 meshIndex);

		std::vector<RigidBody*> m_RigidBodies;
		std::vector<btTriangleIndexVertexArray*> m_MeshVertexArrays;

		MaterialID m_RoadMaterialID = InvalidMaterialID;

		real m_MaxWidth = 4.0f;

		u32 m_QuadCountPerSegment = 10;

		// Non-serialized fields
		GameObjectID m_TerrainGameObjectID = InvalidGameObjectID;

	};
} // namespace flex
