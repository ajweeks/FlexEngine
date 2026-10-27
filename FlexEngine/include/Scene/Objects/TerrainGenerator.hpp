#pragma once

#include "Scene/GameObject.hpp"
#include "Scene/TerrainGeneration.hpp"
#include "Systems/JobSystem.hpp"

namespace flex
{
	class Road;
	struct ChunkCollisionBuild;

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
		// (for tests)
		terrain::TerrainStreamer* GetStreamer() { return m_Streamer; }

		// Where a new world's player starts: flat, dry, low ground near the origin (see terrain::FindSpawnPoint).
		// Uses the current params (& seed), so it works straight after SetWorld, before any chunks have loaded.
		glm::vec3 FindSpawnPoint() const;

		// Marches along the ray against the terrain (including edits). Returns true on hit.
		bool Raycast(const glm::vec3& origin, const glm::vec3& dir, real maxDistance, glm::vec3& outHitPos, glm::vec3& outHitNormal) const;
		// Removes small fragments left floating by the dig (unless bRemoveFragments is false)
		void Dig(const glm::vec3& center, real radius, bool bRemoveFragments = true);
		void Fill(const glm::vec3& center, real radius, terrain::Material material);

		// Plane brushes (see terrain::EditType): move the surface within radius of center onto the plane through center
		// (perpendicular to normal), fading out smoothly over the outer half of the radius so the result has gentle,
		// walkable slopes. FlattenArea both cuts & fills, LowerArea only cuts, RaiseArea only fills (with material).
		// height: how far from the plane terrain is changed (<= 0: a default reaching the surface of most slopes
		// around the disc). Plants whose roots are cut away fall over, & ones which are buried are removed. Saved with
		// the world's terrain edits like digs & fills. Each call re-meshes the chunks around it a few frames later.
		void FlattenArea(const glm::vec3& center, const glm::vec3& normal, real radius, terrain::Material material = terrain::Material::SOIL, real height = 0.0f);
		void LowerArea(const glm::vec3& center, const glm::vec3& normal, real radius, real height = 0.0f);
		void RaiseArea(const glm::vec3& center, const glm::vec3& normal, real radius, terrain::Material material, real height = 0.0f);
		// Flattens a strip of terrain (e.g. under a road or track) along a path through points, radius to either side.
		// Each disc's plane is tilted to follow the path's slope (but not rolled sideways), so the strip is smooth along
		// its length. All discs are applied at once.
		void FlattenPath(const std::vector<glm::vec3>& points, real radius, terrain::Material material = terrain::Material::GRAVEL, real height = 0.0f);
		// Applies a plane edit made with terrain::MakePlaneEdit (see FlattenArea)
		void ApplyPlaneEdits(const std::vector<terrain::Edit>& planeEdits);
		// Removes small pieces of terrain left floating near center (e.g. once a series of edits is finished)
		void RemoveFloatingFragments(const glm::vec3& center, real extent);
		// Average terrain normal around a point on its surface, over roughly radius (more stable than the normal at one
		// point, so good for aligning things with the ground)
		glm::vec3 SampleSmoothNormal(const glm::vec3& pos, real radius) const;

		// Trees & cacti (generated as part of the terrain) whose bases lie within the box. Plants are found from the
		// density, so they're available whether or not their chunk has loaded.
		void FindPlants(const glm::vec3& boxMin, const glm::vec3& boxMax, std::vector<terrain::PlantInfo>& outPlants) const;
		// Removes a plant for good (saved with the terrain's edits), regenerating its chunks without it, & has it fall
		// over (dropping wood). fallDirection: horizontal direction to topple towards (zero: away from the player).
		void FellPlant(const terrain::PlantInfo& plant, const glm::vec3& fallDirection);
		// Removes a plant for good (saved with the terrain's edits) without felling it (no falling tree or drops)
		void RemovePlant(const terrain::PlantInfo& plant);
		// Whether edits are loaded from & saved to disk (not in scene tests, unless in a world's directory)
		bool PersistsEdits() const;
// A plant's (full detail, world space) mesh, as shown in the terrain
		void BuildPlantMesh(const terrain::PlantInfo& plant, terrain::ChunkMeshData& outMesh) const;
		// Called whenever plants are felled (by FellPlant, or by digging out the ground below them)
		static void (*s_OnPlantFelled)(const terrain::PlantInfo& plant, const glm::vec3& fallDirection);
		// Called after every Dig (e.g. so tracks whose support was dug out can break)
		static void (*s_OnTerrainDug)(const glm::vec3& center, real radius);
		// Number of edit jobs in flight (edits are applied a few frames after being made)
		bool IsApplyingEdits() const;

		// Density (including edits) at a world position (< 0: inside terrain, roughly in meters)
		real SampleDensity(const glm::vec3& pos) const;
		// Surface attributes (procedural texture weights: x: grass, y: rock, z: granular, w: snow) at a surface point
		terrain::SurfaceAttributes SampleSurface(const glm::vec3& pos, const glm::vec3& normal) const;
		// Column info (biome, water, etc.) at a world position
		terrain::ColumnSample SampleColumn(real x, real z) const;
		// The material most recently placed (filled) at a point (in or on the surface of placed material). Returns false
		// for naturally generated terrain.
		bool FindPlacedMaterial(const glm::vec3& pos, terrain::Material& outMaterial) const;
		// Number of edits made to the terrain (digs, fills & felled plants)
		u32 GetEditCount() const;

		// Finds a point on the ground (out of water) near pos for something to stand on. When bKeepHeight is set & pos
		// is in open air (e.g. in a cave or above the ground), the ground directly below pos is used if there is any.
		bool FindStandingPoint(const glm::vec3& pos, bool bKeepHeight, glm::vec3& outPos) const;
		// Whether physics collision has been created for the terrain directly below pos (collision is only created
		// near the player, a little while after chunks load)
		bool IsCollisionLoadedBelow(const glm::vec3& pos) const;
		// Whether pos is deep inside solid ground, below anything the terrain could have generated there (e.g. something
		// which fell through the world before its collision loaded)
		bool IsFallenThrough(const glm::vec3& pos) const;

		i32 GetSeed() const { return m_Params.seed; }

		// Switches to another world: its seed, & where its edits & cached chunks are saved (empty: the default, per
		// scene & seed)
		void SetWorld(i32 seed, const std::string& saveDirectory);
		// Writes any unsaved edits to disk now
		void FlushEdits();
		// Writes the displayed chunks to disk (in the background), so they're shown immediately when this world is
		// next loaded. Call after FlushEdits (snapshots are only used with matching edits). bFinal prevents saving
		// again (e.g. on shutdown) until the world changes, for when the camera is about to leave the player.
		void SaveSnapshot(bool bFinal = false);
		// Applies world param changes (restarting generation) if there are any
		void ApplyParamsIfDirty();
		// Whether the chunks around the camera have all been generated since the last regeneration
		bool IsLoadComplete() const { return m_bLoadComplete && !m_bParamsDirty; }
		// True between SetWorld/Regenerate & the next Update, while sampling functions still use the previous world
		bool IsWorldChangePending() const { return m_bParamsDirty; }

		// A map of a large area (for the in game map screen), generated in the background. UpdateLargeMap must be
		// called every frame while it's in use (even while the game is paused).
		void RequestLargeMap(const glm::vec3& center, real extent, u32 resolution);
		void UpdateLargeMap();
		// Returns false until a map has been generated
		bool GetLargeMap(TextureID& outTextureID, glm::vec3& outCenter, real& outExtent) const;
		bool IsGeneratingLargeMap() const { return m_LargeMapFuture.valid(); }

		// The terrain generator in the current scene (if any)
		static TerrainGenerator* GetActive() { return s_Active; }
		// Debug windows (world map), drawn each frame regardless of selection
		static void DrawActiveImGuiWindows();

		static bool* GetShowWorldMapPtr() { return &s_bShowWorldMap; }
		static bool* GetShowMinimapPtr() { return &s_bShowMinimap; }

		// Waypoints the player has placed (see Game, which places, removes & saves them). Slot i is shown on the maps with
		// WaypointColour(i) & the number i + 1.
		struct MapWaypoint
		{
			bool bUsed = false;
			glm::vec2 pos = glm::vec2(0.0f); // World x & z
		};
		static const u32 MAX_MAP_WAYPOINTS = 9;
		static MapWaypoint* GetMapWaypoints();
		static glm::vec4 WaypointColour(u32 index);
		// Draws a waypoint's marker (a coloured diamond with a dark outline) at a pixel position, over the UI
		void DrawMapWaypoint(const glm::vec2& centerPx, u32 index, real sizePx, u32 zOrder);

		// Information about the terrain at a world position (biome name, etc.)
		std::string DescribeLocation(const glm::vec3& pos) const;

		// Draws the player's marker on a map (a white arrow with a dark outline pointing along heading, which is in
		// pixels, y down) as a screen space sprite centered on centerPx
		void DrawMapPlayerArrow(const glm::vec2& centerPx, const glm::vec2& heading, real sizePx, u32 zOrder);

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
			u32 collisionIndexCount = ~0u;
			// Foliage (after collisionIndexCount), which nothing collides with but rays can hit (see
			// CollisionType::RAYCAST_ONLY), so trees can be aimed at
			btTriangleIndexVertexArray* foliageTriangleIndexVertexArray = nullptr;
			RigidBody* foliageRigidBody = nullptr;
			btCollisionShape* foliageCollisionShape = nullptr;
			// Non-zero while collision is being built in the background (see UpdateRigidBodies)
			u32 collisionBuildID = 0;
			// Owns the triangles the collision shapes reference when they were built in the background (otherwise
			// they reference the mesh's data)
			std::shared_ptr<ChunkCollisionBuild> collisionData;
		};

		void UpdateRoadSegments();
		// FLEX_VEGETATION_TOUR=1: visits each biome, screenshotting its vegetation, then fells a cactus (to check what it drops). FLEX_VEGETATION_TOUR=snag fells a burnt tree instead.
		void UpdateVegetationTour();

		terrain::WorldParams GetEffectiveParams() const;
		void ApplyChunkCommands();
		Chunk CreateChunk(terrain::ChunkCommand& command, bool bHidden);
		Chunk CreateChunkTimed(terrain::ChunkCommand& command, bool bHidden);
		void SetChunkHidden(Chunk& chunk, bool bHidden);
		void DestroyChunk(Chunk& chunk);
		void DestroyAllChunks();
		void CreateChunkRigidBody(Chunk& chunk);
		// The expensive part of CreateChunkRigidBody, which can run on any thread
		static void BuildChunkCollisionShapes(Chunk& chunk);
		void DestroyChunkRigidBody(Chunk& chunk);
		void UpdateRigidBodies();
		void UpdateEditorBrush();
		void ApplyEdit(const terrain::Edit& edit);
		void MarkEditsDirty();
		// Appends dig edits removing small pieces of terrain near center which aren't connected to anything (after
		// applying inOutEdits)
		void FindFloatingFragments(const glm::vec3& center, real extent, std::vector<terrain::Edit>& inOutEdits) const;
		void SaveEdits();
		std::string GetSaveFilePathBase() const;
		void UpdateSceneFog();
		void DrawWorldMapWindow();
		void UpdateMinimap();
		void DestroyMinimapTextures(bool bAll);
		void CreateMapArrowTexture();
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
		// Water surfaces use a variant of the terrain shader which hides water inside boats
		MaterialID m_WaterMatID = InvalidMaterialID;

		// Map of chunk index to overlapping road segments
		std::map<glm::ivec3, std::vector<RoadSegment*>, iVec3Compare> m_RoadSegments;
		real m_RoadBlendDist = 10.0f;
		real m_RoadBlendThreshold = 10.0f;
		real m_RoadChunkSize = 512.0f;


		bool m_bPinCenter = false;
		glm::vec3 m_PinnedPos;
		glm::vec3 m_LastCenter = VEC3_ZERO; // Center chunks were last streamed around

		real m_RigidBodyLoadRadius = 250.0f;
		// Collision for chunks within this distance of the player is built immediately (e.g. after digging, or when an
		// LOD change replaces the chunk below the player), further chunks' in background jobs
		real m_RigidBodyImmediateRadius = 48.0f;
		std::vector<std::shared_ptr<ChunkCollisionBuild>> m_CollisionBuilds;
		JobSystem::Context m_CollisionBuildContext;
		u32 m_NextCollisionBuildID = 1;
		// Adds bodies for finished background builds (or discards them if they're no longer needed)
		void FinishCollisionBuilds();
		void DiscardCollisionBuilds();
		u32 m_ThreadCount = 0; // 0 = automatic

		ms m_LastApplyTime = 0.0f;
		ms m_TotalUploadTime = 0.0f;
		u32 m_UploadCount = 0;
		bool m_bParamsDirty = false;
		bool m_bEditPending = false;

		u64 m_LoadStartTime = 0; // From Time::NowTicks
		bool m_bLoadComplete = false;
		ms m_MaxApplyTime = 0.0f;

		// Edits are saved shortly after being made, & chunks around the camera are cached on exit
		bool m_bEditsDirty = false;
		i32 m_LoadedEditsSeed = 0;
		double m_EditsDirtyTime = 0.0;
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
		// bParallel spreads the work over the job system (for maps the player is waiting on)
		static std::future<WorldMap> GenerateMap(std::shared_ptr<const terrain::TerrainDensity> density, const glm::vec3& center, real extent, u32 resolution, bool bParallel = false);
		WorldMap m_WorldMap;
		std::future<WorldMap> m_WorldMapFuture;
		real m_WorldMapZoom = 1.0f;
		bool m_bWorldMapShowChunks = true;

		TextureID CreateMapTexture(const WorldMap& map, const char* name);

		WorldMap m_LargeMap;
		std::future<WorldMap> m_LargeMapFuture;
		TextureID m_LargeMapTextureID = InvalidTextureID;
		std::string m_SaveDirectory;
		bool m_bReloadEdits = false;
		bool m_bSnapshotFinal = false; // See SaveSnapshot

		WorldMap m_Minimap;
		std::future<WorldMap> m_MinimapFuture;
		TextureID m_MinimapTextureID = InvalidTextureID;
		TextureID m_MapArrowTextureID = InvalidTextureID;
		// Replaced textures are destroyed once the GPU is no longer using them
		std::vector<Pair<TextureID, u32>> m_RetiredMinimapTextures;
		real m_MinimapExtent = 800.0f; // Half width (meters)
		static const u32 MINIMAP_RESOLUTION = 160;

		static TerrainGenerator* s_Active;
		static bool s_bShowWorldMap;
		static bool s_bShowMinimap;
	};
} // namespace flex
