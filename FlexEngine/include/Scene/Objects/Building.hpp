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
	// Localized names, as shown to the player (the names above are used in object names)
	const char* BuildPieceDisplayName(BuildPieceType type);
	const char* BuildMaterialDisplayName(BuildMaterial material);

	class BuildPiece final : public GameObject
	{
	public:
		BuildPiece(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		// Must be called before Initialize (when not loaded from a file). size: grid cell size (defaults to GRID_SIZE)
		void Setup(BuildPieceType type, BuildMaterial material, real length, real size = GRID_SIZE);

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;

		BuildPieceType GetPieceType() const { return m_PieceType; }
		BuildMaterial GetBuildMaterial() const { return m_Material; }
		real GetLength() const { return m_Length; }
		// Grid cell size the piece was built for (pieces saved before the grid changed keep their old size)
		real GetSize() const { return m_Size; }

		// Ladders & ropes
		bool IsClimbable() const;
		// Closest point on the climbable's axis to pos, & the axis' extent
		glm::vec3 ClosestPointOnClimbAxis(const glm::vec3& pos) const;
		glm::vec3 GetClimbBottom() const;
		glm::vec3 GetClimbTop() const;
		// Direction from a ladder towards where climbers are (zero for ropes, which can be climbed from any side)
		glm::vec3 GetClimbFacing() const;

		// The nearest climbable within maxDistance (horizontally) of pos, whose height range includes pos
		// reachAbove: how far above pos.y the end of a rope or ladder may be & still be grabbed (reaching up)
		static BuildPiece* FindClimbable(const glm::vec3& pos, real maxDistance, real reachAbove = 0.5f);
		static const std::vector<BuildPiece*>& GetAllPieces() { return s_Pieces; }

		struct Box
		{
			glm::vec3 center;
			glm::vec3 halfExtents;
			glm::quat rotation;
			u32 materialSlot; // 0: main, 1: secondary (e.g. a torch's flame)
		};

		// The boxes making up a piece (local space), used for its mesh & placement previews
		static void GetBoxes(BuildPieceType type, real length, real size, std::vector<Box>& outBoxes);
		// The boxes the piece collides with (local space, empty for torches, ladders & ropes)
		static void GetSolidBoxes(BuildPieceType type, real size, std::vector<Box>& outBoxes);
		// The space a grid piece (floor, wall, roof, stairs) claims, for keeping pieces from overlapping (local space).
		// They're shaped so pieces on neighbouring cells & edges only touch: walls & stairs start above a floor's slab
		// (so they can stand on one in the same cell), walls stop short of the cell's corners, & roofs have no overhang.
		static void GetOccupancyBoxes(BuildPieceType type, real size, std::vector<Box>& outBoxes);
		static bool IsGridPiece(BuildPieceType type);

		static constexpr real GRID_SIZE = 4.0f;
		// Grid size used before it was made coarser: pieces saved without a size were built on it
		static constexpr real LEGACY_GRID_SIZE = 3.0f;
		static constexpr real DEFAULT_LADDER_LENGTH = 4.5f;

		// Chooses which torches get one of the renderer's (limited) point lights: the ones nearest the camera
		static void UpdateTorchLights();
		static void ReleaseTorchLights();

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

	private:
		// Collision for the piece (null for pieces without any)
		static btCollisionShape* CreateCollisionShape(BuildPieceType type, real length, real size);
		void CreateMesh();

		BuildPieceType m_PieceType = BuildPieceType::FLOOR;
		BuildMaterial m_Material = BuildMaterial::WOOD;
		real m_Length = 0.0f; // Ladders & ropes
		real m_Size = GRID_SIZE;

		static std::vector<BuildPiece*> s_Pieces;
	};

	// A grid piece somewhere (placed or not)
	struct BuildPlacement
	{
		BuildPieceType type = BuildPieceType::FLOOR;
		glm::vec3 pos = VEC3_ZERO;
		glm::quat rot = QUAT_IDENTITY;
		real size = BuildPiece::GRID_SIZE;
	};

	// Why a piece can't be placed where it's aimed
	enum class BuildBlockReason : u8
	{
		NONE,
		NO_TARGET,       // Nothing within reach to build on
		OVERLAPS_PIECE,  // Another build piece is in the way
		OVERLAPS_PLAYER, // The player would be stuck inside it
		CEILING,         // Torches can't hang from ceilings
	};

	// How far (in meters) solid shapes may interpenetrate before they count as overlapping
	static constexpr real BUILD_OVERLAP_TOLERANCE = 0.05f;

	// Whether two grid pieces overlap (using their occupancy boxes, so pieces sharing a face or an edge don't). Two
	// walls on the same cell edge (one from each side) also count as overlapping.
	bool BuildPiecesOverlap(const BuildPlacement& a, const BuildPlacement& b);
	// Whether a piece's solid (collision) shape overlaps a world space box, e.g. the player's bounds
	bool BuildPieceOverlapsAABB(const BuildPlacement& piece, const glm::vec3& aabbMin, const glm::vec3& aabbMax);
	// Where another piece of the same type goes to extend `aimed` out from its edge nearest to hitPos (world space):
	// floors extend sideways; walls along their length or upwards; stairs & roofs sideways, or up/down their slope
	// (a roof's high edge gets a mirrored roof, making a ridge). Deterministic for a given hit point.
	BuildPlacement ExtendBuildPiece(const BuildPlacement& aimed, const glm::vec3& hitPos);
	// Overlap & extension checks (run by --test)
	bool RunBuildingSelfTests();

	// Placing & removing build pieces (owned by a player)
	class BuildMode final
	{
	public:
		void Initialize(Player* player);

		void SetActive(bool bActive);
		bool IsActive() const { return m_bActive; }
		void SetSelectedType(BuildPieceType type) { m_SelectedType = type; }

		// Input & placement preview, called every frame
		void Update();
		void DrawHUD() const;

	private:
		// Returns whether the piece can be placed. outHasPosition is set when there's somewhere to show a preview
		// (even if placing there isn't allowed, e.g. the spot is already taken)
		bool ComputePlacement(glm::vec3& outPos, glm::quat& outRot, real& outLength, bool& outHasPosition);
		// Why the grid piece at pos/rot can't be placed (NONE if it can), setting m_BlockingType when a piece is in the way
		BuildBlockReason CheckGridPlacement(const glm::vec3& pos, const glm::quat& rot);
		// Wireframe around the piece right click would remove
		void DrawTargetHighlight(BuildPiece* piece) const;
		// Translucent, emissive copy of the selected piece where it would be placed
		void UpdatePreview(bool bShow);
		void DestroyPreview();
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
		bool m_bPlacementHasPosition = false;
		BuildBlockReason m_BlockReason = BuildBlockReason::NONE;
		BuildPieceType m_BlockingType = BuildPieceType::_COUNT;

		GameObjectID m_PreviewID = InvalidGameObjectID;
		i32 m_PreviewCreatedFrame = 0;
		// What the preview's mesh was last built for, so it's only rebuilt when that changes
		BuildPieceType m_PreviewType = BuildPieceType::_COUNT;
		real m_PreviewLength = -1.0f;
		bool m_bPreviewValid = false;

		real m_Reach = 10.0f;
		AudioSourceID m_PlaceSoundID = InvalidAudioSourceID;
		AudioSourceID m_RemoveSoundID = InvalidAudioSourceID;
	};
} // namespace flex
