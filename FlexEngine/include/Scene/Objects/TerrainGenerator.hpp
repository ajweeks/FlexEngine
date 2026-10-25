#pragma once

#include "Scene/GameObject.hpp"
#include "Scene/TerrainGeneration.hpp"

namespace flex
{
	class Road;

	// Streams an infinite volumetric terrain around the camera (see Scene/TerrainGeneration.hpp)
	class TerrainGenerator final : public GameObject
	{
	public:
		explicit TerrainGenerator(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual void Initialize() override;
		virtual void Update() override;
		virtual void Destroy(bool bDetachFromParent = true) override;

		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;

		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

		// Replaces all chunks with ones generated using the current settings
		void Regenerate();

		// Returns the height of the terrain's (heightfield) surface at the given world position,
		// not taking into account caves, overhangs, etc.
		real SampleHeight(const glm::vec2& pos);

		// Marches along the ray against the terrain (including edits). Returns true on hit.
		bool Raycast(const glm::vec3& origin, const glm::vec3& dir, real maxDistance, glm::vec3& outHitPos, glm::vec3& outHitNormal) const;
		void Dig(const glm::vec3& center, real radius);
		void Fill(const glm::vec3& center, real radius, terrain::Material material);
		// Number of edit jobs in flight (edits are applied a few frames after being made)
		bool IsApplyingEdits() const;

		// Density (including edits) at a world position (< 0: inside terrain, roughly in meters)
		real SampleDensity(const glm::vec3& pos) const;
		// Surface attributes (procedural texture weights: x: grass, y: rock, z: granular, w: snow) at a surface point
		terrain::SurfaceAttributes SampleSurface(const glm::vec3& pos, const glm::vec3& normal) const;
		// Column info (biome, water, etc.) at a world position
		terrain::ColumnSample SampleColumn(real x, real z) const;

		// Finds a point on the ground (out of water) near pos for something to stand on. When bKeepHeight is set & pos
		// is in open air (e.g. in a cave or above the ground), the ground directly below pos is used if there is any.
		bool FindStandingPoint(const glm::vec3& pos, bool bKeepHeight, glm::vec3& outPos) const;
		// Whether physics collision has been created for the terrain directly below pos (collision is only created
		// near the player, a little while after chunks load)
		bool IsCollisionLoadedBelow(const glm::vec3& pos) const;

		i32 GetSeed() const { return m_Params.seed; }

		// The terrain generator in the current scene (if any)
		static TerrainGenerator* GetActive() { return s_Active; }
		// Debug windows (world map), drawn each frame regardless of selection
		static void DrawActiveImGuiWindows();

		static bool* GetShowWorldMapPtr() { return &s_bShowWorldMap; }
		static bool* GetShowMinimapPtr() { return &s_bShowMinimap; }

		// Information about the terrain at a world position (biome name, etc.)
		std::string DescribeLocation(const glm::vec3& pos) const;

	private:
		friend Road;

		struct Chunk
		{
			MeshComponent* meshComponent = nullptr;
			i32 submeshIndex = -1;
			MeshComponent* waterMeshComponent = nullptr;
			i32 waterSubmeshIndex = -1;
			terrain::ChunkKey key;
			glm::vec3 boundsMin;
			glm::vec3 boundsMax;
			btTriangleIndexVertexArray* triangleIndexVertexArray = nullptr;
			RigidBody* rigidBody = nullptr;
			btCollisionShape* collisionShape = nullptr;
		};

		void UpdateRoadSegments();

		terrain::WorldParams GetEffectiveParams() const;
		void ApplyChunkCommands();
		Chunk CreateChunk(terrain::ChunkCommand& command, bool bHidden);
		Chunk CreateChunkTimed(terrain::ChunkCommand& command, bool bHidden);
		void SetChunkHidden(Chunk& chunk, bool bHidden);
		void DestroyChunk(Chunk& chunk);
		void DestroyAllChunks();
		void CreateChunkRigidBody(Chunk& chunk);
		void DestroyChunkRigidBody(Chunk& chunk);
		void UpdateRigidBodies();
		void UpdateEditorBrush();
		void ApplyEdit(const terrain::Edit& edit);
		void SaveEdits();
		std::string GetSaveFilePathBase() const;
		void UpdateSceneFog();
		void DrawWorldMapWindow();
		void UpdateMinimap();
		void DestroyMinimapTextures(bool bAll);
		void UpdateQuality();

		bool DrawBiomeImGui(terrain::BiomeParams& biome);

		terrain::BiomeParams ParseBiome(const JSONObject& biomeObj);
		JSONObject SerializeBiome(const terrain::BiomeParams& biome);

		terrain::WorldParams m_Params;
		terrain::TerrainStreamer* m_Streamer = nullptr;
		std::vector<terrain::ChunkCommand> m_ChunkCommands;
		std::unordered_map<u64, Chunk> m_Chunks; // Keyed by chunk handle
		// Chunks uploaded (hidden) ahead of a swap which must happen all at once (LOD changes & edits)
		std::unordered_map<u64, Chunk> m_StagedChunks;

		// Used to answer height queries before the streamer has been started
		std::shared_ptr<const terrain::TerrainDensity> m_QueryDensity;

		GameObjectID m_RoadGameObjectID = InvalidGameObjectID;
		MaterialID m_TerrainMatID = InvalidMaterialID;

		// Map of chunk index to overlapping road segments
		std::map<glm::ivec3, std::vector<RoadSegment*>, iVec3Compare> m_RoadSegments;
		real m_RoadBlendDist = 10.0f;
		real m_RoadBlendThreshold = 10.0f;
		real m_RoadChunkSize = 512.0f;


		bool m_bPinCenter = false;
		glm::vec3 m_PinnedPos;
		glm::vec3 m_LastCenter = VEC3_ZERO; // Center chunks were last streamed around

		real m_RigidBodyLoadRadius = 250.0f;
		u32 m_ThreadCount = 0; // 0 = automatic

		ms m_LastApplyTime = 0.0f;
		ms m_TotalUploadTime = 0.0f;
		u32 m_UploadCount = 0;
		bool m_bParamsDirty = false;
		bool m_bEditPending = false;

		ns m_LoadStartTime = 0.0f;
		bool m_bLoadComplete = false;
		ms m_MaxApplyTime = 0.0f;

		// Edits are saved shortly after being made, & chunks around the camera are cached on exit
		bool m_bEditsDirty = false;
		i32 m_LoadedEditsSeed = 0;
		sec m_EditsDirtyTime = 0.0f;
		bool m_bEnableDiskCache = true;

		// Sets the scene's fog to hide the end of the view distance
		bool m_bAutoFog = true;

		// Editor brush (edit terrain with the mouse while the terrain is selected)
		bool m_bEditorBrush = false;
		real m_BrushRadius = 3.0f;
		i32 m_BrushMaterialIndex = 0;
		sec m_BrushCooldown = 0.0f;

		// Shadows are rendered out to this distance while this terrain is active (overriding the renderer setting)
		real m_ShadowDistance = 1500.0f;

		// Applied quality level (see Renderer::GetTerrainQuality), scales the LOD distance factor
		i32 m_AppliedQuality = -1;

		// World map debug window & minimap
		struct WorldMap
		{
			glm::vec3 center = VEC3_ZERO;
			real extent = 0.0f;
			u32 resolution = 0;
			std::vector<u32> colours; // ABGR, row major (z, x), +z first
			bool bValid = false;
		};
		static std::future<WorldMap> GenerateMap(std::shared_ptr<const terrain::TerrainDensity> density, const glm::vec3& center, real extent, u32 resolution);
		WorldMap m_WorldMap;
		std::future<WorldMap> m_WorldMapFuture;
		real m_WorldMapZoom = 1.0f;
		bool m_bWorldMapShowChunks = true;

		WorldMap m_Minimap;
		std::future<WorldMap> m_MinimapFuture;
		TextureID m_MinimapTextureID = InvalidTextureID;
		// Replaced textures are destroyed once the GPU is no longer using them
		std::vector<Pair<TextureID, u32>> m_RetiredMinimapTextures;
		real m_MinimapExtent = 1500.0f; // Half width (meters)

		static TerrainGenerator* s_Active;
		static bool s_bShowWorldMap;
		static bool s_bShowMinimap;
	};
} // namespace flex
