#pragma once

#include "Scene/GameObject.hpp"

// Position based dynamics soft bodies & springs
namespace flex
{
	class SpringObject final : public GameObject
	{
	public:
		SpringObject(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		virtual void Initialize() override;
		virtual void Update() override;
		virtual void Destroy(bool bDetachFromParent = true) override;

		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;

	private:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

		static void CreateMaterials();

		static const char* s_ExtendedMeshFilePath;
		static const char* s_ContractedMeshFilePath;

		static MaterialID s_SpringMatID;
		static MaterialID s_BobberMatID;

		MeshComponent* m_ExtendedMesh = nullptr;
		MeshComponent* m_ContractedMesh = nullptr;

		VertexBufferDataCreateInfo m_DynamicVertexBufferCreateInfo;
		std::vector<u32> m_Indices;

		GameObject* m_Target = nullptr;
		real m_MinLength = 5.0f;
		real m_MaxLength = 10.0f;
		glm::vec3 m_TargetPos = VEC3_ZERO;

		GameObject* m_OriginTransform = nullptr;

		// We manage these objects ourselves rather than adding them to the scene
		bool m_bSimulateTarget = true;
		SoftBody* m_SpringSim = nullptr;
		GameObject* m_Bobber = nullptr;

		std::vector<glm::vec3> extendedPositions;
		std::vector<glm::vec3> extendedNormals;
		std::vector<glm::vec3> extendedTangents;
		std::vector<glm::vec3> contractedPositions;
		std::vector<glm::vec3> contractedNormals;
		std::vector<glm::vec3> contractedTangents;
	};

	struct Point
	{
		Point(glm::vec3 pos, glm::vec3 vel, real invMass) :
			pos(pos),
			vel(vel),
			invMass(invMass)
		{}

		glm::vec3 pos;
		glm::vec3 vel;
		real invMass;
	};

	struct Constraint
	{
		enum class EqualityType
		{
			EQUALITY,
			INEQUALITY,

			_NONE
		};

		enum class Type
		{
			DISTANCE,
			BENDING,

			_NONE
		};

		Constraint(real stiffness, EqualityType equalityType, Type type) :
			stiffness(stiffness),
			equalityType(equalityType),
			type(type)
		{
		}

		virtual ~Constraint() = default;

		real stiffness;
		EqualityType equalityType;
		Type type;
	};

	struct DistanceConstraint : public Constraint
	{
		DistanceConstraint(i32 pointIndex0, i32 pointIndex1, real stiffness, real targetDistance);

		real targetDistance;
		i32 pointIndices[2];
	};

	struct BendingConstraint : public Constraint
	{
		BendingConstraint(i32 pointIndex0, i32 pointIndex1, i32 pointIndex2, i32 pointIndex3, real stiffness, real targetPhi);

		real targetPhi;
		i32 pointIndices[4];
	};

	struct Triangle
	{
		Triangle();
		Triangle(i32 pointIndex0, i32 pointIndex1, i32 pointIndex2);

		i32 pointIndices[3];
	};

	class SoftBody final : public GameObject
	{
	public:
		SoftBody(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;

		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;

		void SetStiffness(real stiffness);
		void SetDamping(real damping);

		void SetRenderWireframe(bool bRenderWireframe);

		// Add new constraint between index0 & index1 if one doesn't already exist.
		// Returns new constraint count
		u32 AddUniqueDistanceConstraint(i32 index0, i32 index1, u32 atIndex, real stiffness);
		u32 AddUniqueBendingConstraint(i32 index0, i32 index1, i32 index2, i32 index3, u32 atIndex, real stiffness);

		static ms FIXED_UPDATE_TIMESTEP;
		static u32 MAX_UPDATE_COUNT; // Max fixed update steps that can be taken in one frame

		std::vector<Point*> points;
		// TODO: Split constraint types into separate containers
		std::vector<Constraint*> constraints;
		std::vector<Triangle*> triangles;

	private:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

		void Draw();

		void LoadFromMesh();

		// Outside vert is vert not on shared edge
		bool GetTriangleSharingEdge(const std::vector<u32>& indexData, i32 edgeIndex0, i32 edgeIndex1, const Triangle& originalTri, Triangle& outTri, i32& outOutsideVertIndex);

		u32 m_SolverIterationCount;
		bool m_bPaused = false;
		bool m_bSingleStep = false;
		bool m_bRenderWireframe = true;

		ms m_MSToSim = 0.0f;
		ms m_UpdateDuration = 0.0f;
		real m_Damping = 0.99f;
		real m_Stiffness = 0.8f;
		real m_BendingStiffness = 0.1f;

		i32 m_ShownBendingIndex = 0;
		i32 m_FirstBendingConstraintIndex = 0;

		std::vector<glm::vec3> initialPositions;

		MeshComponent* m_MeshComponent = nullptr;
		VertexBufferDataCreateInfo m_MeshVertexBufferCreateInfo;
		MaterialID m_MeshMaterialID = InvalidMaterialID;

		std::string m_CurrentMeshFilePath;
		// Editor only
		i32 m_SelectedMeshIndex = -1;
		std::string m_CurrentMeshFileName;

	};
} // namespace flex
