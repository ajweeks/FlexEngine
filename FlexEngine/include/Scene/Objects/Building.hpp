#pragma once

#include "Scene/GameObject.hpp"

class btCollisionShape;

// Player built structures: floors, walls, roofs & stairs on a grid, plus torches, ladders & ropes placed freely
namespace flex
{
	class Player;
	struct VertexBufferDataCreateInfo;

	enum class BuildPieceType : u8
	{
		FLOOR,
		WALL,
		ROOF,
		STAIRS,
		TORCH,
		LADDER,
		ROPE,

		_COUNT
	};

	enum class BuildMaterial : u8
	{
		WOOD,
		STONE,

		_COUNT
	};

	const char* BuildPieceTypeName(BuildPieceType type);

	// Appends a (flat shaded) box to vertex data, for procedural meshes
	void AppendBoxToVertexData(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation,
		VertexBufferDataCreateInfo& vertexData, std::vector<u32>& indices);
	const char* BuildMaterialName(BuildMaterial material);

	class BuildPiece final : public GameObject
	{
	public:
		BuildPiece(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		// Must be called before Initialize (when not loaded from a file)
		void Setup(BuildPieceType type, BuildMaterial material, real length);

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;

		BuildPieceType GetPieceType() const { return m_PieceType; }
		BuildMaterial GetBuildMaterial() const { return m_Material; }
		real GetLength() const { return m_Length; }

		// Ladders & ropes
		bool IsClimbable() const;
		// Closest point on the climbable's axis to pos, & the axis' extent
		glm::vec3 ClosestPointOnClimbAxis(const glm::vec3& pos) const;
		glm::vec3 GetClimbBottom() const;
		glm::vec3 GetClimbTop() const;
		// Direction from a ladder towards where climbers are (zero for ropes, which can be climbed from any side)
		glm::vec3 GetClimbFacing() const;

		// The nearest climbable within maxDistance (horizontally) of pos, whose height range includes pos
		static BuildPiece* FindClimbable(const glm::vec3& pos, real maxDistance);
		static const std::vector<BuildPiece*>& GetAllPieces() { return s_Pieces; }

		// Edges of the piece's boxes (local space), used to preview placement
		static void GetOutline(BuildPieceType type, real length, std::vector<std::pair<glm::vec3, glm::vec3>>& outLines);

		static constexpr real GRID_SIZE = 3.0f;
		static constexpr real DEFAULT_LADDER_LENGTH = 4.5f;

		// Chooses which torches get one of the renderer's (limited) point lights: the ones nearest the camera
		static void UpdateTorchLights();
		static void ReleaseTorchLights();

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

	private:
		struct Box
		{
			glm::vec3 center;
			glm::vec3 halfExtents;
			glm::quat rotation;
			u32 materialSlot; // 0: main, 1: secondary (e.g. a torch's flame)
		};

		static void GetBoxes(BuildPieceType type, real length, std::vector<Box>& outBoxes);
		// Collision for the piece (null for pieces without any)
		static btCollisionShape* CreateCollisionShape(BuildPieceType type, real length);
		void CreateMesh();

		BuildPieceType m_PieceType = BuildPieceType::FLOOR;
		BuildMaterial m_Material = BuildMaterial::WOOD;
		real m_Length = 0.0f; // Ladders & ropes

		static std::vector<BuildPiece*> s_Pieces;
	};

	// Placing & removing build pieces (owned by a player)
	class BuildMode final
	{
	public:
		void Initialize(Player* player);

		void SetActive(bool bActive);
		bool IsActive() const { return m_bActive; }

		// Input & placement preview, called every frame
		void Update();
		void DrawHUD() const;

	private:
		bool ComputePlacement(glm::vec3& outPos, glm::quat& outRot, real& outLength) const;
		BuildPiece* GetTargetedPiece() const;
		void Place();
		void RemoveTargeted();

		Player* m_Player = nullptr;
		bool m_bActive = false;
		BuildPieceType m_SelectedType = BuildPieceType::FLOOR;
		BuildMaterial m_SelectedMaterial = BuildMaterial::WOOD;
		i32 m_Rotation = 0; // Quarter turns
		sec m_Cooldown = 0.0f;

		bool m_bPlacementValid = false;
		glm::vec3 m_PlacementPos = VEC3_ZERO;
		glm::quat m_PlacementRot = QUAT_IDENTITY;
		real m_PlacementLength = 0.0f;

		real m_Reach = 10.0f;
		AudioSourceID m_PlaceSoundID = InvalidAudioSourceID;
		AudioSourceID m_RemoveSoundID = InvalidAudioSourceID;
	};
} // namespace flex
