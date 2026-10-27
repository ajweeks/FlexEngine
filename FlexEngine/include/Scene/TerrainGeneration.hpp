#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

// Infinite, seeded, volumetric terrain:
//  - TerrainDensity is a pure function of world position (and the world params/seed). Below the surface
//    the density is negative, above it's positive. It's composed of a 2D heightfield (biome-blended, with
//    a Voronoi biome layout chosen by climate, oceans, rare mountain massifs & lakes) plus 3D overhangs, arches,
//    tunnels & caverns.
//  - Players can dig & fill terrain. Edits are spheres applied on top of the generated density (in order), so they
//    work at every LOD and are tiny to store.
//  - Chunks form an octree: a level L chunk ("super chunk") covers 2^L x 2^L x 2^L level 0 chunks, at the same
//    cell count. Chunks are selected by distance to the camera such that neighbors differ by at most one level.
//  - Meshes are extracted with marching tetrahedra. Cells on a face/edge shared with a finer neighbor are
//    subdivided to exactly match the finer neighbor's triangulation, so chunks of different LODs connect
//    without cracks or skirts.
//  - Chunks are generated on worker threads. Changes in LOD are applied atomically once all replacement chunks
//    are ready, so there are no holes while moving.
//  - The chunks around the camera are saved to disk on exit so the next startup can show them immediately.
namespace flex
{
	namespace terrain
	{
		static const u32 MAX_BIOME_COUNT = 16;

		// Bump whenever generation output changes to invalidate cached chunks
		static const u32 GENERATOR_VERSION = 16;

		enum class Material : u8
		{
			GRASS,
			SOIL,
			ROCK,
			SNOW,
			SAND,
			GRANITE,
			SANDSTONE,
			MARBLE,
			BASALT,
			CLAY,
			GRAVEL,

			_COUNT
		};

		const char* MaterialName(Material material);

		// Materials players can place
		static const Material FILL_MATERIALS[] =
		{
			Material::SOIL, Material::SNOW, Material::GRANITE, Material::SANDSTONE, Material::MARBLE,
			Material::BASALT, Material::CLAY, Material::GRAVEL, Material::SAND,
		};

		enum class EditType : u8
		{
			DIG,
			FILL,
			// A plant (tree or cactus) which has been cut down: center is its base. Doesn't change the density.
			REMOVE_PLANT,
			// Plane brushes: a disc (center, normal, radius) which moves the surface onto the plane through center, within
			// height of it (along the normal). The effect fades out smoothly towards the disc's rim (see PlaneEditWeight),
			// so results have gentle, walkable slopes instead of the creases left by spheres.
			LOWER, // Removes material above the plane (a flat bottomed hole, or a terrace cut into a slope)
			RAISE, // Adds material below the plane (a flat topped mound, or a terrace built out from a slope)
			FLATTEN, // Both

			_COUNT
		};

		struct Edit
		{
			glm::vec3 center;
			real radius;
			EditType type;
			Material material;
			// Plane edits only
			glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
			real height = 0.0f;
		};

		inline bool IsPlaneEdit(EditType type)
		{
			return type == EditType::LOWER || type == EditType::RAISE || type == EditType::FLATTEN;
		}

		// World space box containing everything the edit affects (other than the density's edit band)
		void GetEditBounds(const Edit& edit, glm::vec3& outMin, glm::vec3& outMax);
		// [0, 1] strength of a plane edit at offset (from its center): 1 within the inner part of the disc, fading to 0
		// at its rim & at height from the plane
		real PlaneEditWeight(const Edit& edit, const glm::vec3& offset);
		// A plane edit (see EditType). height <= 0 picks a default which reaches past the surface of most slopes.
		Edit MakePlaneEdit(EditType type, const glm::vec3& center, const glm::vec3& normal, real radius, real height, Material material);

		// The plant life a biome grows: a dominant tree species (with a little variety), its density & the bushes
		// beneath. Chosen per biome so forests look uniform.
		enum class Vegetation : u8
		{
			NONE,
			TAIGA, // Dense dark green spruces, the odd birch, berry bushes
			NORDIC, // Light green pines & birches, berry bushes
			BROADLEAF, // Light green deciduous trees in open woods, leafy bushes
			SWAMP, // Drooping willows, leafy bushes
			TUNDRA, // Low shrubs & the odd stunted spruce
			ALPINE, // Sparse pines, low shrubs
			SAVANNA, // Scattered flat topped acacias & dry scrub
			DESERT, // Cacti & dry scrub
			VOLCANIC, // No living trees: the odd charred snag or stump (chopped for charcoal) & sparse dark scrub

			_COUNT
		};

		const char* VegetationName(Vegetation vegetation);
		Vegetation ParseVegetation(const std::string& name, Vegetation fallback);

		struct BiomeParams
		{
			std::string name;
			real frequency = 1.0f; // Relative likelihood of a biome cell being this biome
			real temperature = 0.0f; // Preferred climate [-1: polar, 1: desert]. Biomes are more likely to appear in their climate.

			// Heightfield
			real baseHeight = 0.0f;
			real heightAmplitude = 100.0f;
			real wavelength = 1000.0f;
			real ridgedness = 0.0f; // [0, 1], 0: rolling fbm, 1: sharp ridges
			real terraceStrength = 0.0f; // [0, 1], creates mesas & stepped cliffs
			real terraceHeight = 40.0f;

			// Volumetric features
			real overhangAmplitude = 0.0f; // Meters, strength of 3D noise displacement (creates overhangs & cliffs)
			real archStrength = 0.0f; // [0, 1], likelihood of natural rock arches
			real caveStrength = 0.0f; // [0, 1]
			real lakeStrength = 1.0f; // [0, 1], likelihood of lakes

			glm::vec3 groundColour = glm::vec3(0.3f, 0.45f, 0.15f);
			glm::vec3 cliffColour = glm::vec3(0.4f, 0.37f, 0.33f);
			glm::vec3 peakColour = glm::vec3(0.9f, 0.92f, 0.95f);
			real peakHeight = 100000.0f; // World height above which peakColour is used
			real grassiness = 1.0f; // [0, 1], 0: ground is bare soil/sand, 1: ground is grass

			Vegetation vegetation = Vegetation::BROADLEAF;
			real treeDensity = 1.0f; // Multiplier of the vegetation type's tree density
			real bushDensity = 1.0f; // Multiplier of the vegetation type's bush density
		};

		struct WorldParams
		{
			i32 seed = 1234;

			real voxelSize = 2.0f; // Size of a level 0 cell (meters)
			u32 cellsPerChunkAxis = 24;
			u32 maxLodLevel = 10;
			real lodDistanceFactor = 2.0f; // Larger = higher detail further away (min 1.8 to ensure neighbors differ by at most one level)
			real viewDistance = 40000.0f;

			real biomeCellSize = 12000.0f;
			real biomeBlendDistance = 3000.0f;

			real continentWavelength = 14000.0f;
			real continentAmplitude = 200.0f;

			real overhangWavelength = 90.0f;

			// Natural arches (half tori of rock standing on the ground)
			real archCellSize = 400.0f; // At most one arch per cell
			real archMinRadius = 15.0f;
			real archMaxRadius = 40.0f;

			// Caves: tunnels (which entrances lead to) & occasional large caverns
			real caveWavelength = 300.0f; // Larger = longer, straighter tunnels
			real tunnelRadius = 5.5f; // Meters (tunnels are a little flatter than they are wide)
			real caveMaxDepth = 160.0f;
			real cavernWavelength = 160.0f;
			real cavernChance = 0.5f; // [0, 1]

			// Lakes (flat water surfaces in carved basins)
			real lakeCellSize = 3000.0f; // At most one lake per cell
			real lakeChance = 0.55f;
			real lakeMinRadius = 150.0f;
			real lakeMaxRadius = 750.0f;
			real lakeMaxRelief = 90.0f; // Lakes are only placed where the surrounding terrain is flat enough

			// Rare, very large mountain ranges
			real massifCellSize = 32000.0f; // At most one massif per cell
			real massifChance = 0.35f;
			real massifMinHeight = 2000.0f;
			real massifMaxHeight = 4200.0f;

			// Climate (temperature) varies over this distance, so similar biomes are found near each other
			real climateWavelength = 80000.0f;

			// Oceans: large regions of deep water at sea level
			real seaLevel = 0.0f;
			real oceanWavelength = 60000.0f;
			real oceanCoverage = 0.3f; // [0, 1], approximate fraction of the world covered by ocean
			real oceanDepth = 260.0f;

			std::vector<BiomeParams> biomes;

			static WorldParams Default();
			real ChunkSize(u32 level) const { return voxelSize * (real)cellsPerChunkAxis * (real)(1u << level); }
			real CellSize(u32 level) const { return voxelSize * (real)(1u << level); }
			// Hash of all values affecting chunk mesh contents
			u64 ContentHash() const;
		};

		// Values derived from the 2D (xz) position
		struct ColumnSample
		{
			real height;
			real baseHeight; // Height before lakes were carved
			real overhangAmplitude;
			real archStrength;
			real caveStrength;
			real cavernStrength; // [0, 1], caverns only occur in some regions (where this is > 0)
			real caveEntrance; // [0, 1], where tunnels may reach the surface
			real lakeStrength;
			glm::vec3 groundColour;
			glm::vec3 cliffColour;
			glm::vec3 peakColour;
			real peakHeight;
			real grassiness;
			real massifWeight;
			real oceanWeight; // [0, 1], 0: inland, 1: open ocean
			real temperature;
			real primaryBiomeWeight;
			i32 primaryBiome;
			// The next most prominent biome (-1: none) & primary / (primary + secondary) weights (vegetation blends
			// between them near borders)
			i32 secondaryBiome;
			real primaryBiomeShare;

			// Lake or ocean (lakeID == 0: none, OCEAN_ID: ocean)
			u32 lakeID;
			real waterLevel;
			real lakeT; // Distance from the lake center / lake radius (< 1: under water)

			// Arch (archRadius == 0: none)
			real archRadius;
			real archThickness;
			glm::vec3 archCenter;
			real archDirX;
			real archDirZ;
		};

		static const u32 OCEAN_ID = 2; // Lake IDs are always odd

		struct EditList
		{
			const Edit* edits = nullptr;
			u32 count = 0;
		};

		struct SurfaceAttributes
		{
			glm::vec4 colour; // rgb: albedo (sRGB), a: marble vein strength
			glm::vec4 weights; // Procedural texture weights: x: grass, y: layered rock, z: granular, w: snow
		};

		struct ChunkKey
		{
			i32 level;
			glm::ivec3 index;

			bool operator==(const ChunkKey& other) const { return level == other.level && index == other.index; }
			bool operator!=(const ChunkKey& other) const { return !(*this == other); }
			ChunkKey Parent() const;
		};

		struct ChunkKeyHash
		{
			size_t operator()(const ChunkKey& key) const;
		};

		// Column samples of chunk grids, shared between vertically stacked chunks (whose columns are identical)
		class ColumnGridCache
		{
		public:
			std::shared_ptr<const std::vector<ColumnSample>> Find(i32 level, i32 x, i32 z);
			void Insert(i32 level, i32 x, i32 z, const std::shared_ptr<const std::vector<ColumnSample>>& grid);

		private:
			static const u32 CAPACITY = 512;

			std::mutex m_Mutex;
			std::unordered_map<ChunkKey, std::shared_ptr<const std::vector<ColumnSample>>, ChunkKeyHash> m_Grids;
			std::deque<ChunkKey> m_Order;
		};

		class TerrainDensity
		{
		public:
			explicit TerrainDensity(const WorldParams& params);

			ColumnSample SampleColumn(real x, real z) const;
			// Returns a signed value, roughly in meters (< 0: inside terrain, > 0: in air)
			real Sample(const glm::vec3& pos, const ColumnSample& column, const EditList& edits = {}) const;
			real Sample(const glm::vec3& pos, const EditList& edits = {}) const;

			SurfaceAttributes ComputeSurface(const glm::vec3& pos, const glm::vec3& normal, const ColumnSample& column, const EditList& edits) const;

			// Conservative bounds on how far the surface can deviate from the column height
			real MaxExtentAbove() const { return m_MaxExtentAbove; }
			real MaxExtentBelow() const { return m_MaxExtentBelow; }

			const WorldParams& Params() const { return m_Params; }
			u32 Seed() const { return m_Seed; }

			// Height of the topmost surface (including edits) at (x, z) between yMin & yMax, if there is one with open sky
			// above it (not a cave floor)
			bool FindTopSurface(real x, real z, real yMin, real yMax, const ColumnSample& column, const EditList& edits, real& outY) const;

			// Climate at a world position [-1: polar, 1: desert]
			real Temperature(real x, real z) const;
			// [0, 1], 0: inland, 1: open ocean
			real OceanWeight(real x, real z) const;

			// Distance beyond an edit's radius within which it affects the density
			real EditBand() const { return m_Params.voxelSize * 2.0f; }

			ColumnGridCache& ColumnCache() const { return *m_ColumnCache; }

		private:
			struct LakeInfo
			{
				bool bValid;
				real level;
			};

			struct ArchInfo
			{
				bool bValid;
				real baseHeight;
			};

			ColumnSample SampleBaseColumn(real x, real z) const;
			real BiomeHeight(u32 biomeIndex, real x, real z) const;
			real MassifHeight(real x, real z, real& outWeight) const;
			LakeInfo GetLakeInfo(i32 cellX, i32 cellZ, real centerX, real centerZ, real radius) const;
			ArchInfo GetArchInfo(i32 cellX, i32 cellZ, real centerX, real centerZ) const;
			void ApplyLake(real x, real z, ColumnSample& column) const;
			void ApplyArch(real x, real z, ColumnSample& column) const;
			void ApplyOcean(real x, real z, ColumnSample& column) const;
			// Low frequency noise defining where oceans are, (u, v) in units of the ocean wavelength
			real OceanNoise(real u, real v) const;

			WorldParams m_Params;
			u32 m_Seed;
			u32 m_InstanceID; // Unique per instance, used to key thread local caches
			real m_TotalBiomeFrequency;
			real m_MaxExtentAbove;
			real m_MaxExtentBelow;
			std::vector<u32> m_BiomeOctaves;
			u32 m_MassifOctaves;
			real m_OceanThreshold; // Ocean noise values below this are ocean
			std::unique_ptr<ColumnGridCache> m_ColumnCache;
		};

		// Bits 0-5: faces (-X, +X, -Y, +Y, -Z, +Z) touching a finer neighbor
		// Bits 6-17: edges touching a finer neighbor. Edge index = axis * 4 + (lowerAxisSide) + (upperAxisSide * 2),
		// where the "lower/upper" axes are the two other axes in increasing order
		using TransitionMask = u32;

		inline u32 FaceBit(u32 axis, u32 side) { return 1u << (axis * 2 + side); }
		inline u32 EdgeBit(u32 axis, u32 side0, u32 side1) { return 1u << (6 + axis * 4 + side0 + side1 * 2); }

		struct ChunkMeshData
		{
			std::vector<glm::vec3> positions;
			std::vector<glm::vec3> normals;
			std::vector<glm::vec4> colours;
			// xyz: grass, layered rock & granular texture weights (snow is the remainder), w: sky visibility [0: deep
			// underground, 1: open to the sky]
			std::vector<glm::vec4> weights;
			std::vector<u32> indices;
			// Only the first indices are collidable (the terrain & tree trunks, not foliage). ~0u: all of them.
			u32 collisionIndexCount = ~0u;

			// Flat lake surfaces
			std::vector<glm::vec3> waterPositions;
			std::vector<u32> waterIndices;

			bool Empty() const { return indices.empty() && waterIndices.empty(); }
			void Clear();
		};

		// Compact serialized form of a chunk's mesh (~24 bytes per vertex) kept for disk caching
		using PackedChunk = std::vector<u8>;
		void PackChunkMesh(const ChunkMeshData& mesh, PackedChunk& outPacked);
		bool UnpackChunkMesh(const u8* data, size_t size, ChunkMeshData& outMesh);

		struct ChunkJob
		{
			ChunkKey key;
			TransitionMask mask = 0;
			u64 jobID = 0;
			real priority = 0.0f; // Lower = sooner
			bool bEdit = false; // Regenerating a displayed chunk after an edit (survives SetJobs)
			std::shared_ptr<const TerrainDensity> density;
			std::vector<Edit> edits;
		};

		struct ChunkResult
		{
			ChunkKey key;
			TransitionMask mask;
			u64 jobID;
			ChunkMeshData mesh;
			std::shared_ptr<const PackedChunk> packed;
			ms generationTime;
		};

		void GenerateChunkMesh(const TerrainDensity& density, const ChunkKey& key, TransitionMask mask, const EditList& edits, ChunkMeshData& outMesh);

		// Trees & cacti, which are generated as part of the terrain's chunks (deterministically from the seed)
		enum class PlantKind : u8
		{
			NONE,
			CONIFER,
			DECIDUOUS,
			CACTUS,
			SNAG, // Dead & burnt: no foliage, yields charcoal
		};

		// Visual variants of the plant kinds
		enum class PlantSpecies : u8
		{
			NONE,
			SPRUCE, // Conifer: dark, tall & narrow
			PINE, // Conifer: lighter, bare lower trunk & rounded tiers
			BIRCH, // Deciduous: slender pale trunk, small light canopy
			OAK, // Deciduous: wide, light green canopy
			WILLOW, // Deciduous: drooping dark canopy
			ACACIA, // Deciduous: flat topped canopy on a leaning trunk
			SAGUARO, // Cactus
			CHARRED, // Snag: a blackened trunk with broken branch stubs (or just a stump)
		};

		struct PlantInfo
		{
			PlantKind kind = PlantKind::NONE;
			PlantSpecies species = PlantSpecies::NONE;
			glm::vec3 base = glm::vec3(0.0f); // Where the trunk meets the ground
			real height = 0.0f; // Overall height
			real trunkHeight = 0.0f; // Height of the collidable trunk (from the base)
			real trunkRadius = 0.0f; // At the base
			u32 hash = 0; // Unique per plant, the source of its variation
			real size01 = 0.0f;
			real scale = 1.0f; // Of the whole plant (stunted trees are smaller)
		};

		// Plants whose bases lie within the box (as shown at full detail), excluding removed ones. Edits must include
		// those overlapping the box.
		void FindPlants(const TerrainDensity& density, const EditList& edits, const glm::vec3& boxMin, const glm::vec3& boxMax, std::vector<PlantInfo>& outPlants);
		// A single plant's (full detail) mesh, in world space. Trunk triangles come first, outMesh.collisionIndexCount
		// of them (as in chunks).
		void BuildPlantMesh(const TerrainDensity& density, const PlantInfo& plant, ChunkMeshData& outMesh);
		// An edit removing the given plant (so chunks are generated without it)
		Edit MakeRemovePlantEdit(const PlantInfo& plant);

		struct SpawnSearchResult
		{
			glm::vec3 pos = glm::vec3(0.0f); // On the ground
			bool bIdeal = false; // false: no flat, dry & low ground was found in range, pos is the best compromise
			u32 samples = 0; // Columns evaluated
		};

		// Where to start a new world: dry, gently sloping land a little above sea level (not in a cave, lake or the
		// sea) as close to the origin as possible. Searches outwards from the origin using the density alone (no
		// chunks need to have been generated) & is deterministic per seed.
		SpawnSearchResult FindSpawnPoint(const TerrainDensity& density, real maxRadius = 20000.0f);

		class TerrainWorkerPool
		{
		public:
			~TerrainWorkerPool();

			void Start(u32 threadCount);
			void Stop();

			// Replaces all queued (not yet started) jobs, except edit jobs & those in retainedJobPriorities (which are
			// kept with updated priorities)
			void SetJobs(std::vector<ChunkJob>&& jobs, const std::unordered_map<u64, real>& retainedJobPriorities = {});
			void AddJobs(std::vector<ChunkJob>&& jobs);
			void TakeResults(std::vector<ChunkResult>& outResults);

			u32 ThreadCount() const { return (u32)m_Threads.size(); }
			u32 QueuedJobCount();

		private:
			void ThreadMain();
			void SortQueue();

			std::vector<std::thread> m_Threads;
			std::mutex m_Mutex;
			std::condition_variable m_CV;
			std::vector<ChunkJob> m_Queue; // Sorted, back = highest priority
			std::vector<ChunkResult> m_Results;
			bool m_bRunning = false;
		};

		// Instructions to the renderer side of the terrain
		struct ChunkCommand
		{
			enum class Type
			{
				SHOW,
				HIDE
			};

			Type type;
			u64 handle;
			ChunkKey key;
			ChunkMeshData mesh; // Only set for SHOW
			// Whether this command must be applied in the same frame as the previous one (LOD swaps & edits)
			bool bAtomicWithPrevious = false;
		};

		// Spatial index of edits
		class EditStore
		{
		public:
			// Replaces the last edit instead when the new one contains it (see the definition)
			void Add(const Edit& edit);
			void Clear();
			const std::vector<Edit>& Edits() const { return m_Edits; }
			// Appends edits overlapping the box (in order)
			void Gather(const glm::vec3& boxMin, const glm::vec3& boxMax, std::vector<Edit>& outEdits) const;
			bool Overlaps(const glm::vec3& boxMin, const glm::vec3& boxMax) const;
			u64 Hash() const { return m_Hash; }

		private:
			static constexpr real CELL_SIZE = 32.0f;

			struct CellHash
			{
				size_t operator()(const glm::ivec3& cell) const;
			};

			std::vector<Edit> m_Edits;
			std::unordered_map<glm::ivec3, std::vector<u32>, CellHash> m_Cells;
			glm::vec3 m_BoundsMin = glm::vec3(1e30f);
			glm::vec3 m_BoundsMax = glm::vec3(-1e30f);
			u64 m_Hash = 0;
			u64 m_HashBeforeLast = 0; // (before the last edit was added, for replacing it)
		};

		bool WriteEdits(const std::string& filePath, i32 seed, const std::vector<Edit>& edits);
		bool ReadEdits(const std::string& filePath, i32 seed, std::vector<Edit>& outEdits);

		class TerrainStreamer
		{
		public:
			TerrainStreamer();
			~TerrainStreamer();

			// snapshotPath: Where displayed chunks are cached between sessions (empty to disable)
			void Start(const WorldParams& params, u32 threadCount, const std::string& snapshotPath = "");
			void Stop();

			// Discards all chunks & regenerates with the given params
			void SetParams(const WorldParams& params);
			// Changes the LOD distance factor (quality) without discarding chunks
			void SetLodDistanceFactor(real lodDistanceFactor);

			// Returns show/hide commands to be applied this frame
			void Update(const glm::vec3& cameraPos, std::vector<ChunkCommand>& outCommands);

			// Hides all displayed chunks (call before destroying renderer side resources)
			void HideAll(std::vector<ChunkCommand>& outCommands);

			void ApplyEdit(const Edit& edit);
			void ApplyEdits(const Edit* edits, u32 count);
			void SetEdits(const std::vector<Edit>& edits);
			const EditStore& Edits() const { return m_Edits; }

			// Plants (not yet removed) whose bases lie within the box
			void FindPlants(const glm::vec3& boxMin, const glm::vec3& boxMax, std::vector<PlantInfo>& outPlants) const;

			// Marches along a ray against the density (including edits). Returns true on hit.
			bool Raycast(const glm::vec3& origin, const glm::vec3& dir, real maxDistance, glm::vec3& outHitPos, glm::vec3& outHitNormal) const;

			// Density (including edits) at a point (< 0: inside terrain)
			real SampleDensity(const glm::vec3& pos) const;
			// Surface attributes (including edits) at a point on the surface
			SurfaceAttributes SampleSurface(const glm::vec3& pos, const glm::vec3& normal) const;

			// Writes the displayed chunks to the snapshot path (on a background thread)
			void SaveSnapshot(const glm::vec3& cameraPos);
			// Snapshots are per world, so the path should change along with the seed
			void SetSnapshotPath(const std::string& snapshotPath) { m_SnapshotPath = snapshotPath; }
			void WaitForSnapshotWrite();
			// Loads the snapshot at the snapshot path in the background (call after changing world params & edits).
			// If it matches, it replaces everything displayed at once, rather than regenerating all chunks.
			void LoadSnapshotAsync();

			std::shared_ptr<const TerrainDensity> Density() const { return m_Density; }
			const WorldParams& Params() const { return m_Params; }

			struct Stats
			{
				u32 displayedChunkCount = 0;
				u32 displayedNonEmptyChunkCount = 0;
				u32 pendingChunkCount = 0;
				u32 queuedJobCount = 0;
				u32 retiringChunkCount = 0;
				u32 desiredChunkCount = 0;
				u32 editJobCount = 0;
				u32 chunksPerLevel[32] = {};
				u64 totalGenerated = 0;
				ms averageGenerationTime = 0.0f;
				u32 threadCount = 0;
				u32 snapshotChunkCount = 0;
				bool bSnapshotLoading = false;
				size_t packedBytes = 0;
				u32 swapGroupCount = 0;
				u32 pendingSwapGroupCount = 0;
				glm::vec3 velocity = glm::vec3(0.0f);
				real batchLatency = 0.0f;
				bool bSettled = false; // Nothing left to select, generate or swap
			};
			Stats GetStats();

			// For the world map debug view
			struct DisplayedChunkInfo
			{
				ChunkKey key;
				TransitionMask mask;
				bool bEmpty;
				bool bPending; // Not yet displayed (being generated)
			};
			// Displayed chunks, followed by chunks being generated
			void GetDisplayedChunks(std::vector<DisplayedChunkInfo>& outChunks) const;

			// For testing: checks that every displayed chunk's transition mask matches the levels of the displayed chunks
			// around it (a mismatch leaves cracks). Returns the number of mismatched faces & edges.
			u32 ValidateDisplayedMasks(std::string* outReport = nullptr) const;

			// Exposed for testing
			void ComputeDesiredChunks(const glm::vec3& cameraPos, std::unordered_map<ChunkKey, TransitionMask, ChunkKeyHash>& outDesired);
			i32 LeafLevelAt(const glm::vec3& cameraPos, const glm::vec3& pos) const;

		private:
			struct Pending
			{
				TransitionMask mask;
				u64 jobID;
				bool bDone;
				ChunkMeshData mesh;
				std::shared_ptr<const PackedChunk> packed;
				u32 group = 0;
			};

			// Chunks which must be swapped in together (to keep neighbors consistent): new chunks which touch each
			// other, along with the displayed chunks they replace. Independent groups are applied as soon as they're
			// ready, so nearby (quick to generate) changes don't wait on distant ones.
			struct SwapGroup
			{
				std::vector<ChunkKey> retiring;
				std::vector<ChunkKey> pending;
				bool bApplied = false;
			};

			struct Displayed
			{
				TransitionMask mask;
				u64 handle;
				bool bEmpty;
				std::shared_ptr<const PackedChunk> packed;
				// Generated with previous params or edits (e.g. the previous world), so it must be replaced even if it
				// matches a desired chunk. Kept until then, since a batch can be superseded before it completes.
				bool bStale = false;
			};

			struct DesiredChunks
			{
				glm::vec3 cameraPos;
				std::unordered_map<ChunkKey, TransitionMask, ChunkKeyHash> chunks;
			};

			struct Snapshot
			{
				bool bValid = false;
				ms loadTime = 0.0f; // Reading & unpacking
				ms readTime = 0.0f;
				glm::vec3 cameraPos;
				std::vector<std::pair<ChunkKey, TransitionMask>> keys;
				std::vector<ChunkMeshData> meshes;
				std::vector<std::shared_ptr<const PackedChunk>> packed;
			};

			bool ShouldSplit(const glm::vec3& cameraPos, i32 level, const glm::ivec3& index) const;
			bool IsRootInView(const glm::vec3& cameraPos, const glm::ivec3& rootIndex) const;
			void CollectLeaves(const glm::vec3& cameraPos, i32 level, const glm::ivec3& index, std::vector<ChunkKey>& outLeaves);
			bool ColumnMayContainSurface(i32 level, const glm::ivec3& index);
			TransitionMask ComputeTransitionMask(const glm::vec3& cameraPos, const ChunkKey& key) const;
			void CancelDesiredComputation();
			void StartBatch(DesiredChunks&& desired);
			void BuildSwapGroups(const std::vector<ChunkKey>& retiring);
			void ApplyReadySwapGroups(std::vector<ChunkCommand>& outCommands);
			// Checks that the displayed chunks are exactly those of the last batch (see the definition)
			void VerifyBatchResult();
			// Job priority (lower = sooner), favoring chunks the camera is heading towards
			real ChunkPriority(const ChunkKey& key) const;
			void UpdateVelocity(const glm::vec3& cameraPos);
			// bGroupStarted: Whether the commands emitted must be applied in the same frame as earlier commands
			void Show(const ChunkKey& key, Pending&& pending, std::vector<ChunkCommand>& outCommands, bool& bGroupStarted);
			void Hide(const ChunkKey& key, std::vector<ChunkCommand>& outCommands, bool& bGroupStarted);
			ChunkJob MakeJob(const ChunkKey& key, TransitionMask mask, real priority);
			void GetChunkBounds(const ChunkKey& key, glm::vec3& outMin, glm::vec3& outMax) const;
			void ApplySnapshot(Snapshot&& snapshot, std::vector<ChunkCommand>& outCommands);
			static Snapshot LoadSnapshot(const std::string& path, u64 contentHash, u64 editsHash, std::shared_ptr<std::atomic<bool>> cancelled);

			WorldParams m_Params;
			std::shared_ptr<const TerrainDensity> m_Density;
			TerrainWorkerPool m_Pool;
			EditStore m_Edits;

			std::unordered_map<ChunkKey, Displayed, ChunkKeyHash> m_Displayed;
			std::unordered_map<ChunkKey, Pending, ChunkKeyHash> m_Pending;
			std::vector<SwapGroup> m_SwapGroups;
			u32 m_RetiringCount = 0;

			// Regenerated (displayed) chunks following edits, swapped in all at once
			std::unordered_map<ChunkKey, Pending, ChunkKeyHash> m_EditPending;

			// Approximate surface height range per (level, x, z) chunk column, used to skip empty chunks
			std::unordered_map<ChunkKey, glm::vec2, ChunkKeyHash> m_ColumnBounds;

			// LOD selection runs off the main thread, writing to m_ColumnBounds while in flight
			std::future<DesiredChunks> m_DesiredFuture;
			// Edited regions, copied for use by LOD selection
			std::vector<glm::vec3> m_EditBoundsForSelection;

			std::string m_SnapshotPath;
			std::future<Snapshot> m_SnapshotFuture;
			// Loads which were superseded (e.g. the menu's terrain when loading straight into a world), kept until they
			// finish since destroying an async future blocks
			std::vector<std::future<Snapshot>> m_DiscardedSnapshotFutures;
			// Whether the loading snapshot replaces chunks already displayed (from a previous world)
			bool m_bSnapshotReplacesDisplayed = false;
			std::shared_ptr<std::atomic<bool>> m_SnapshotCancelled;
			std::future<void> m_SnapshotWriteFuture;
			u32 m_SnapshotChunkCount = 0;

			glm::vec3 m_BatchCameraPos;
			// The chunks (& transition masks) the last batch selected
			std::unordered_map<ChunkKey, TransitionMask, ChunkKeyHash> m_BatchDesired;
			u32 m_BatchMismatchCount = 0;
			bool m_bBatchInFlight = false;

			// Smoothed camera velocity, used to prioritize chunks ahead of the camera
			glm::vec3 m_LastCameraPos = glm::vec3(0.0f);
			glm::vec3 m_SmoothedVelocity = glm::vec3(0.0f);
			std::chrono::steady_clock::time_point m_LastUpdateTime;
			bool m_bHasLastCameraPos = false;
			glm::vec3 m_PriorityCameraPos = glm::vec3(0.0f);
			glm::vec3 m_PriorityVelocity = glm::vec3(0.0f);
			// Time from requesting a batch until it's ready (smoothed)
			std::chrono::steady_clock::time_point m_BatchStartTime;
			real m_BatchLatency = 0.3f;
			bool m_bForceRefresh = true;
			bool m_bRetireAllOnNextBatch = false;
			// Marks every displayed chunk as stale (see Displayed::bStale) & retires them all in the next batch
			void RetireAllDisplayed();
			u64 m_NextJobID = 1;
			u64 m_NextHandle = 1;

			u64 m_TotalGenerated = 0;
			ms m_TotalGenerationTime = 0.0f;
			u32 m_DesiredCount = 0;
		};
	} // namespace terrain
} // namespace flex
