#include "stdafx.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionShapes/btTriangleIndexVertexArray.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <LinearMath/btIDebugDraw.h>

#include <glm/gtx/norm.hpp> // for distance2
#include <glm/gtx/quaternion.hpp> // for rotate
#include <glm/gtx/vector_angle.hpp> // for angle
#include <glm/ext/matrix_transform.hpp> // for rotate

#include <imgui/imgui_internal.h> // For PushItemFlag

IGNORE_WARNINGS_POP

#include "Scene/Objects/TerrainGenerator.hpp"
#include "SceneTestRunner.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "FlexEngine.hpp"
#include "GameUI.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "Inventory.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Platform/Platform.hpp"
#include "Player.hpp"
#include "PropertyCollection.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/Objects/Props.hpp" // For DroppedItem
#include "Scene/Objects/Road.hpp"
#include "Scene/SceneManager.hpp"
#include "Systems/JobSystem.hpp"
#include "Time.hpp"
#include "Window/Window.hpp"

namespace flex
{
	TerrainGenerator* TerrainGenerator::s_Active = nullptr;
	void (*TerrainGenerator::s_OnPlantFelled)(const terrain::PlantInfo& plant, const glm::vec3& fallDirection) = nullptr;
	void (*TerrainGenerator::s_OnTerrainDug)(const glm::vec3& center, real radius) = nullptr;
	bool TerrainGenerator::s_bShowWorldMap = false;
	bool TerrainGenerator::s_bShowMinimap = true;

	// LOD distance factor multipliers for each terrain quality level (see Renderer::GetTerrainQuality)
	// FLEX_TERRAIN_VIEW_PRIORITY=0: chunks are generated nearest first, wherever the camera's looking (for comparisons)
	static const bool s_bDisableViewPriority = getenv("FLEX_TERRAIN_VIEW_PRIORITY") != nullptr && atoi(getenv("FLEX_TERRAIN_VIEW_PRIORITY")) == 0;
	static const real s_TerrainQualityLodScales[] = { 0.9f, 1.0f, 1.3f, 1.65f };
	static const char* s_TerrainQualityNames[] = { "Low", "Medium", "High", "Ultra" };

	TerrainGenerator::TerrainGenerator(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, TerrainGeneratorSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate),
		m_Params(terrain::WorldParams::Default()),
		m_PinnedPos(VEC3_ZERO)
	{
		AddTag("terrain");
	}

	PropertyCollection* TerrainGenerator::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("chunk generator info");

		// All world generation (biomes, oceans, mountains, lakes, caves, arches, ...) derives from this one seed
		collection->childCollection->RegisterProperty("seed", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.seed));

		collection->childCollection->RegisterProperty("voxel size", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.voxelSize))
			.Precision(2);
		collection->childCollection->RegisterProperty("cells per chunk axis", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.cellsPerChunkAxis));
		collection->childCollection->RegisterProperty("max lod level", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.maxLodLevel));
		collection->childCollection->RegisterProperty("lod distance factor", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.lodDistanceFactor))
			.Precision(2);
		collection->childCollection->RegisterProperty("view distance", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.viewDistance))
			.Precision(1);

		collection->childCollection->RegisterProperty("biome cell size", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.biomeCellSize))
			.Precision(1);
		collection->childCollection->RegisterProperty("biome blend distance", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.biomeBlendDistance))
			.Precision(1);
		collection->childCollection->RegisterProperty("continent wavelength", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.continentWavelength))
			.Precision(1);
		collection->childCollection->RegisterProperty("continent amplitude", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.continentAmplitude))
			.Precision(1);
		collection->childCollection->RegisterProperty("overhang wavelength", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.overhangWavelength))
			.Precision(1);
		collection->childCollection->RegisterProperty("arch cell size", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.archCellSize))
			.Precision(1);
		collection->childCollection->RegisterProperty("arch min radius", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.archMinRadius))
			.Precision(1);
		collection->childCollection->RegisterProperty("arch max radius", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.archMaxRadius))
			.Precision(1);
		collection->childCollection->RegisterProperty("cave wavelength", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.caveWavelength))
			.Precision(1);
		collection->childCollection->RegisterProperty("tunnel radius", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.tunnelRadius))
			.Precision(2);
		collection->childCollection->RegisterProperty("cave max depth", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.caveMaxDepth))
			.Precision(1);
		collection->childCollection->RegisterProperty("cavern wavelength", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.cavernWavelength))
			.Precision(1);
		collection->childCollection->RegisterProperty("cavern chance", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.cavernChance))
			.Precision(2);
		collection->childCollection->RegisterProperty("lake cell size", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.lakeCellSize))
			.Precision(1);
		collection->childCollection->RegisterProperty("lake chance", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.lakeChance))
			.Precision(2);
		collection->childCollection->RegisterProperty("lake min radius", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.lakeMinRadius))
			.Precision(1);
		collection->childCollection->RegisterProperty("lake max radius", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.lakeMaxRadius))
			.Precision(1);
		collection->childCollection->RegisterProperty("lake max relief", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.lakeMaxRelief))
			.Precision(1);
		collection->childCollection->RegisterProperty("massif cell size", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.massifCellSize))
			.Precision(1);
		collection->childCollection->RegisterProperty("massif chance", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.massifChance))
			.Precision(2);
		collection->childCollection->RegisterProperty("massif min height", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.massifMinHeight))
			.Precision(1);
		collection->childCollection->RegisterProperty("massif max height", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.massifMaxHeight))
			.Precision(1);
		collection->childCollection->RegisterProperty("climate wavelength", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.climateWavelength))
			.Precision(1);
		collection->childCollection->RegisterProperty("sea level", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.seaLevel))
			.Precision(1);
		collection->childCollection->RegisterProperty("ocean wavelength", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.oceanWavelength))
			.Precision(1);
		collection->childCollection->RegisterProperty("ocean coverage", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.oceanCoverage))
			.Precision(2);
		collection->childCollection->RegisterProperty("ocean depth", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_Params.oceanDepth))
			.Precision(1);
		collection->childCollection->RegisterProperty("shadow distance", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_ShadowDistance))
			.Precision(1);

		collection->childCollection->RegisterProperty("rigid body load radius", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_RigidBodyLoadRadius))
			.Precision(1);
		collection->childCollection->RegisterProperty("thread count", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_ThreadCount));
		collection->childCollection->RegisterProperty("enable disk cache", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_bEnableDiskCache));
		collection->childCollection->RegisterProperty("auto fog", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_bAutoFog));

		collection->childCollection->RegisterProperty("pin center", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_bPinCenter))
			.VersionAdded(6);
		collection->childCollection->RegisterProperty("pinned center", FIELD_OFFSET_AND_TYPE(TerrainGenerator, m_PinnedPos))
			.VersionAdded(6)
			.Precision(2);

		return collection;
	}

	std::string TerrainGenerator::GetSaveFilePathBase() const
	{
		if (!m_SaveDirectory.empty())
		{
			return m_SaveDirectory + "terrain_" + std::to_string(GetEffectiveParams().seed);
		}
		const std::string sceneName = StripFileType(g_SceneManager->CurrentScene()->GetFileName());
		return RelativePathToAbsolute(SAVED_DIRECTORY "terrain/") + sceneName + "_" + std::to_string(GetEffectiveParams().seed);
	}

	void TerrainGenerator::Initialize()
	{
		const char* materialName = "Terrain";
		m_TerrainMatID = g_Renderer->GetMaterialID(materialName);
		if (m_TerrainMatID == InvalidMaterialID)
		{
			MaterialCreateInfo matCreateInfo = {};
			matCreateInfo.name = materialName;
			matCreateInfo.shaderName = "terrain";
			matCreateInfo.constAlbedo = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
			matCreateInfo.constRoughness = 1.0f;
			matCreateInfo.constMetallic = 0.0f;
			matCreateInfo.bSerializable = false;
			// Chunks are streamed in & out constantly, so use sub-allocated dynamic buffers
			// rather than the static buffers which are rebuilt entirely on every change
			matCreateInfo.bDynamic = true;
			m_TerrainMatID = g_Renderer->InitializeMaterial(&matCreateInfo);
		}

		const char* waterMaterialName = "Terrain water";
		m_WaterMatID = g_Renderer->GetMaterialID(waterMaterialName);
		if (m_WaterMatID == InvalidMaterialID)
		{
			MaterialCreateInfo matCreateInfo = {};
			matCreateInfo.name = waterMaterialName;
			matCreateInfo.shaderName = "terrain_water";
			matCreateInfo.constAlbedo = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
			matCreateInfo.constRoughness = 1.0f;
			matCreateInfo.constMetallic = 0.0f;
			matCreateInfo.bSerializable = false;
			matCreateInfo.bDynamic = true;
			m_WaterMatID = g_Renderer->InitializeMaterial(&matCreateInfo);
		}

		m_Mesh = new Mesh(this);
		m_Mesh->SetTypeToMemory();

		u32 threadCount = m_ThreadCount;
		if (threadCount == 0)
		{
			// Leave room for the main & render threads
			threadCount = (u32)glm::clamp((i32)std::thread::hardware_concurrency() - 2, 1, 16);
		}

		m_AppliedQuality = g_Renderer->GetTerrainQuality();
		m_AppliedDrawDistanceScale = g_Renderer->GetDrawDistanceScale();
		const terrain::WorldParams params = GetEffectiveParams();
		m_Streamer = new terrain::TerrainStreamer();

		std::string snapshotPath;
		if (m_bEnableDiskCache)
		{
			const std::string saveFilePathBase = GetSaveFilePathBase();
			Platform::CreateDirectoryRecursive(RelativePathToAbsolute(SAVED_DIRECTORY "terrain/"));

			// Edits made in previous sessions
			std::vector<terrain::Edit> edits;
			if (PersistsEdits() && terrain::ReadEdits(saveFilePathBase + ".edits", params.seed, edits))
			{
				m_Streamer->SetEdits(edits);
			}

			// Chunks displayed at the end of the last session are shown immediately
			snapshotPath = saveFilePathBase + ".snapshot";
		}
		m_LoadedEditsSeed = params.seed;

		m_Streamer->Start(params, threadCount, snapshotPath);
		m_QueryDensity.reset();
		m_bParamsDirty = false;
		m_LoadStartTime = Time::NowTicks();
		m_bLoadComplete = false;
		m_MaxApplyTime = 0.0f;

		s_Active = this;
		UpdateSceneFog();

		GameObject::Initialize();
	}

	terrain::WorldParams TerrainGenerator::GetEffectiveParams() const
	{
		terrain::WorldParams params = m_Params;
		const i32 quality = glm::clamp(g_Renderer->GetTerrainQuality(), 0, (i32)ARRAY_LENGTH(s_TerrainQualityLodScales) - 1);
		params.lodDistanceFactor = glm::max(params.lodDistanceFactor * s_TerrainQualityLodScales[quality], 1.8f);
		// The draw distance setting (never below a few km, so the world doesn't look like a small island)
		params.viewDistance = glm::max(params.viewDistance * g_Renderer->GetDrawDistanceScale(), glm::min(params.viewDistance, 4000.0f));
		return params;
	}

	void TerrainGenerator::UpdateSceneFog()
	{
		if (!m_bAutoFog)
		{
			return;
		}

		// Fade out terrain before it reaches the view distance so the cutoff isn't visible
		g_SceneManager->CurrentScene()->SetFogDistance(GetEffectiveParams().viewDistance * 0.92f);
	}

	void TerrainGenerator::UpdateQuality()
	{
		const i32 quality = g_Renderer->GetTerrainQuality();
		if (quality != m_AppliedQuality && m_Streamer != nullptr)
		{
			m_AppliedQuality = quality;
			// Only changes which chunks are selected, so existing chunks are kept where possible
			m_Streamer->SetLodDistanceFactor(GetEffectiveParams().lodDistanceFactor);
		}
		const real drawDistanceScale = g_Renderer->GetDrawDistanceScale();
		if (drawDistanceScale != m_AppliedDrawDistanceScale && m_Streamer != nullptr)
		{
			m_AppliedDrawDistanceScale = drawDistanceScale;
			m_Streamer->SetViewDistance(GetEffectiveParams().viewDistance);
			UpdateSceneFog();
		}
	}

	void TerrainGenerator::ApplyParamsIfDirty()
	{
		if (!m_bParamsDirty || m_Streamer == nullptr)
		{
			return;
		}

		m_bParamsDirty = false;
		const terrain::WorldParams params = GetEffectiveParams();

		// Edits are stored per world (seed)
		const bool bWorldChanged = params.seed != m_LoadedEditsSeed || m_bReloadEdits;
		if (bWorldChanged && m_bEnableDiskCache)
		{
			m_bReloadEdits = false;
			SaveEdits();
			m_LoadedEditsSeed = params.seed;
			std::vector<terrain::Edit> edits;
			if (PersistsEdits())
			{
				terrain::ReadEdits(GetSaveFilePathBase() + ".edits", params.seed, edits);
			}
			m_Streamer->SetEdits(edits);
		}
		m_LoadedEditsSeed = params.seed;
		if (m_bEnableDiskCache)
		{
			m_Streamer->SetSnapshotPath(GetSaveFilePathBase() + ".snapshot");
		}

		m_Streamer->SetParams(params);
		if (m_bEnableDiskCache && bWorldChanged)
		{
			// Each world has its own snapshot of the chunks displayed when it was last left
			m_Streamer->LoadSnapshotAsync();
		}
		m_QueryDensity.reset();
		m_LoadStartTime = Time::NowTicks();
		m_bLoadComplete = false;
		m_MaxApplyTime = 0.0f;
		m_WorldMap.bValid = false;
		UpdateSceneFog();
	}

	void TerrainGenerator::Update()
	{
		PROFILE_AUTO("TerrainGenerator Update");

		if (m_Streamer != nullptr)
		{
			UpdateQuality();
			if (s_Active == this)
			{
				// Terrain is visible for many kilometers, so shadows need to reach further than in small scenes
				g_Renderer->SetShadowDistanceOverride(m_ShadowDistance);
			}

			ApplyParamsIfDirty();

			const glm::vec3 center = m_bPinCenter ? m_PinnedPos : g_CameraManager->CurrentCamera()->position;
			m_LastCenter = center;
			{
				// Chunks in view are generated first (see TerrainStreamer::SetViewDirection)
				BaseCamera* viewCamera = g_CameraManager->CurrentCamera();
				const glm::mat4 projection = viewCamera->GetProjection();
				const real tanX = 1.0f / glm::max(glm::abs(projection[0][0]), 1e-4f);
				const real tanY = 1.0f / glm::max(glm::abs(projection[1][1]), 1e-4f);
				const real cosHalfAngle = 1.0f / glm::sqrt(1.0f + tanX * tanX + tanY * tanY);
				m_Streamer->SetViewDirection(m_bPinCenter ? glm::vec3(0.0f) : glm::normalize(viewCamera->forward), cosHalfAngle, !s_bDisableViewPriority);
			}
			m_Streamer->Update(center, m_ChunkCommands);
			if (!m_bLoadComplete && m_InViewLoadTime < 0.0f && m_Streamer->GetPendingInViewCount() == 0 && m_Streamer->GetStats().displayedChunkCount > 0)
			{
				// (Chunks are displayed once their neighbours are ready too, so this is when generation in view finished)
				m_InViewLoadTime = Time::MillisecondsSince(m_LoadStartTime);
			}

			// Ensure the far plane reaches the view distance
			BaseCamera* camera = g_CameraManager->CurrentCamera();
			const real viewDistance = GetEffectiveParams().viewDistance;
			if (m_bAutoFog && camera->zFar < viewDistance * 1.05f)
			{
				camera->zFar = viewDistance * 1.05f;
			}
			ApplyChunkCommands();
			{
				PROFILE_AUTO("TerrainGenerator UpdateRigidBodies");
				UpdateRigidBodies();
			}
			UpdateEditorBrush();
			if (s_Active == this)
			{
				UpdateVegetationTour();
			}
			{
				PROFILE_AUTO("TerrainGenerator UpdateMinimap");
				UpdateMinimap();
			}

			if (m_bEditsDirty && g_SecElapsedSinceProgramStart - m_EditsDirtyTime > 2.0f)
			{
				SaveEdits();
			}

			if (!m_bLoadComplete)
			{
				terrain::TerrainStreamer::Stats stats = m_Streamer->GetStats();
				if (stats.bSettled && stats.displayedChunkCount > 0 && m_ChunkCommands.empty() && m_StagedChunks.empty())
				{
					m_bLoadComplete = true;
					// Make room for chunks streamed in during play now, while loading, since growing the buffers later
					// would stall for a while. (The amount in use varies as different terrain streams in, & the
					// buffers' free space gets fragmented.)
					g_Renderer->ReserveDynamicVertexData(m_TerrainMatID, 1.75f);
					u32 triangleCount = 0;
					for (auto& pair : m_Chunks)
					{
						triangleCount += pair.second.meshComponent != nullptr ? pair.second.meshComponent->GetIndexCount() / 3 : 0;
					}
					const ms loadTime = Time::MillisecondsSince(m_LoadStartTime);
					if (m_InViewLoadTime >= 0.0f)
					{
						Print("Terrain in view generated in %.0fms\n", m_InViewLoadTime);
					}
					m_InViewLoadTime = -1.0f;
					Print("Terrain loaded in %.0fms (%u chunks, %u non-empty, %u from disk cache, %u triangles, avg %.2fms/chunk on %u threads, avg upload %.2fms/chunk, max upload %.1fms/frame)\n",
						loadTime, stats.displayedChunkCount, stats.displayedNonEmptyChunkCount, stats.snapshotChunkCount, triangleCount,
						stats.averageGenerationTime, stats.threadCount, m_UploadCount > 0 ? m_TotalUploadTime / (real)m_UploadCount : 0.0f, m_MaxApplyTime);
				}
			}
		}

		GameObject::Update();
	}

	void TerrainGenerator::UpdateVegetationTour()
	{
		struct TourStop
		{
			std::string name;
			glm::vec3 pos;
		};
		static i32 s_Step = getenv("FLEX_VEGETATION_TOUR") != nullptr ? 0 : -1;
		static real s_Time = 0.0f;
		static std::vector<TourStop> s_Stops;
		static u32 s_StopIndex = 0;
		static terrain::PlantInfo s_Cactus;
		if (s_Step < 0)
		{
			return;
		}

		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		if (player == nullptr || !m_bLoadComplete || player->IsAwaitingTerrainCollision())
		{
			return;
		}
		s_Time += g_UnpausedDeltaTime;
		BaseScene* scene = g_SceneManager->CurrentScene();

		auto Settled = [this]()
		{
			const terrain::TerrainStreamer::Stats stats = m_Streamer->GetStats();
			return stats.bSettled && m_ChunkCommands.empty() && m_StagedChunks.empty();
		};
		auto Screenshot = [](const std::string& name)
		{
			const std::string directory = RelativePathToAbsolute(SCREENSHOT_DIRECTORY);
			Platform::CreateDirectoryRecursive(directory);
			g_Renderer->RequestScreenshot(directory + "vegetation_" + name + ".png");
		};
		auto Aim = [player](const glm::vec3& target)
		{
			const glm::vec3 delta = target - player->GetEyePosition();
			player->GetTransform()->SetWorldRotation(glm::angleAxis(std::atan2(delta.x, delta.z), VEC3_UP));
			player->SetPitch(-std::atan2(delta.y, glm::length(glm::vec2(delta.x, delta.z))));
		};
		auto CountPlants = [this](const glm::vec3& center, real extent, std::string* outSummary)
		{
			std::vector<terrain::PlantInfo> plants;
			FindPlants(center - glm::vec3(extent, 400.0f, extent), center + glm::vec3(extent, 400.0f, extent), plants);
			if (outSummary != nullptr)
			{
				static const char* speciesNames[] = { "none", "spruce", "pine", "birch", "oak", "willow", "acacia", "saguaro", "charred", "kapok", "palm", "giant mushroom" };
				u32 counts[ARRAY_LENGTH(speciesNames)] = {};
				for (const terrain::PlantInfo& plant : plants)
				{
					++counts[glm::min((u32)plant.species, (u32)ARRAY_LENGTH(speciesNames) - 1)];
				}
				for (u32 i = 1; i < ARRAY_LENGTH(speciesNames); ++i)
				{
					if (counts[i] > 0)
					{
						*outSummary += std::string(speciesNames[i]) + ": " + std::to_string(counts[i]) + " ";
					}
				}
			}
			return (u32)plants.size();
		};

		switch (s_Step)
		{
		case 0:
		{
			// The nearest well vegetated spot well inside each biome
			const std::vector<terrain::BiomeParams>& biomes = m_Params.biomes;
			const glm::vec3 origin = player->GetTransform()->GetWorldPosition();
			std::vector<std::vector<glm::vec3>> candidates(biomes.size());
			const i32 halfCount = 90;
			const real spacing = 500.0f;
			for (i32 ring = 0; ring <= halfCount; ++ring)
			{
				for (i32 iz = -ring; iz <= ring; ++iz)
				{
					for (i32 ix = -ring; ix <= ring; ++ix)
					{
						if (glm::max(glm::abs(ix), glm::abs(iz)) != ring)
						{
							continue;
						}
						const real x = origin.x + (real)ix * spacing;
						const real z = origin.z + (real)iz * spacing;
						const terrain::ColumnSample column = SampleColumn(x, z);
						if (column.primaryBiome < 0 || column.primaryBiome >= (i32)biomes.size() || column.primaryBiomeShare < 0.95f ||
							column.oceanWeight > 0.05f || column.massifWeight > 0.1f || (column.lakeID != 0 && column.lakeT < 1.5f) ||
							candidates[column.primaryBiome].size() >= 6)
						{
							continue;
						}
						candidates[column.primaryBiome].push_back(glm::vec3(x, column.height, z));
					}
				}
			}
			for (u32 b = 0; b < (u32)biomes.size(); ++b)
			{
				u32 bestCount = 0;
				glm::vec3 best(0.0f);
				for (const glm::vec3& candidate : candidates[b])
				{
					const u32 count = CountPlants(candidate, 60.0f, nullptr);
					if (count >= bestCount)
					{
						bestCount = count;
						best = candidate;
					}
				}
				if (candidates[b].empty())
				{
					Print("Vegetation tour: no %s found\n", biomes[b].name.c_str());
					continue;
				}
				s_Stops.push_back({ biomes[b].name, best });
			}
			scene->SetTimeOfDay(0.1f);
			scene->SetTimeOfDayPaused(true);
			s_StopIndex = 0;
			s_Step = s_Stops.empty() ? 10 : 1;
			s_Time = 0.0f;
		} break;
		case 1:
		{
			const TourStop& stop = s_Stops[s_StopIndex];
			player->TeleportOnTerrain(stop.pos + glm::vec3(0.0f, 2.0f, 0.0f));
			s_Step = 2;
			s_Time = 0.0f;
		} break;
		case 2:
		{
			const TourStop& stop = s_Stops[s_StopIndex];
			// Look towards the plants nearby
			std::vector<terrain::PlantInfo> plants;
			const glm::vec3 eye = player->GetEyePosition();
			FindPlants(eye - glm::vec3(60.0f, 200.0f, 60.0f), eye + glm::vec3(60.0f, 200.0f, 60.0f), plants);
			glm::vec3 target = eye + glm::vec3(0.0f, -2.0f, 30.0f);
			if (!plants.empty())
			{
				glm::vec3 sum(0.0f);
				for (const terrain::PlantInfo& plant : plants)
				{
					sum += plant.base;
				}
				target = sum / (real)plants.size();
				if (glm::distance(glm::vec2(target.x, target.z), glm::vec2(eye.x, eye.z)) < 8.0f)
				{
					target = plants[0].base;
				}
				target.y += 2.0f;
			}
			Aim(target);
			if (s_Time > 4.0f && Settled())
			{
				std::string summary;
				const u32 count = CountPlants(eye, 100.0f, &summary);
				const terrain::ColumnSample column = SampleColumn(eye.x, eye.z);
				Print("Vegetation tour: %s at (%.0f, %.0f, %.0f), %s: %u plants within 100m (%s)\n", stop.name.c_str(), eye.x, eye.y, eye.z,
					terrain::VegetationName(m_Params.biomes[glm::clamp(column.primaryBiome, 0, (i32)m_Params.biomes.size() - 1)].vegetation), count, summary.c_str());
				std::string fileName = std::to_string(s_StopIndex) + "_" + stop.name;
				for (char& c : fileName)
				{
					c = c == ' ' ? '_' : (char)tolower(c);
				}
				Screenshot(fileName);
				s_Step = 3;
				s_Time = 0.0f;
			}
		} break;
		case 3:
			if (s_Time > 0.5f)
			{
				++s_StopIndex;
				s_Step = s_StopIndex < (u32)s_Stops.size() ? 1 : 10;
				s_Time = 0.0f;
			}
			break;
		case 10:
		{
			// Fell the nearest cactus (or burnt tree, with FLEX_VEGETATION_TOUR=snag), & check what it drops
			const char* tourMode = getenv("FLEX_VEGETATION_TOUR");
			const terrain::PlantKind fellKind = tourMode != nullptr && strcmp(tourMode, "snag") == 0 ? terrain::PlantKind::SNAG : terrain::PlantKind::CACTUS;
			bool bFound = false;
			const glm::vec3 origin = player->GetTransform()->GetWorldPosition();
			for (const TourStop& stop : s_Stops)
			{
				std::vector<terrain::PlantInfo> plants;
				FindPlants(stop.pos - glm::vec3(150.0f, 400.0f, 150.0f), stop.pos + glm::vec3(150.0f, 400.0f, 150.0f), plants);
				for (const terrain::PlantInfo& plant : plants)
				{
					if (plant.kind == fellKind)
					{
						s_Cactus = plant;
						bFound = true;
						break;
					}
				}
				if (bFound)
				{
					break;
				}
			}
			FLEX_UNUSED(origin);
			if (!bFound)
			{
				Print("Vegetation tour: no %s found\n", fellKind == terrain::PlantKind::SNAG ? "burnt tree" : "cactus");
				s_Step = 100;
				break;
			}
			player->TeleportOnTerrain(s_Cactus.base + glm::vec3(4.0f, 1.5f, 2.0f));
			s_Step = 11;
			s_Time = 0.0f;
		} break;
		case 11:
			Aim(s_Cactus.base + VEC3_UP * 1.0f);
			if (s_Time > 3.0f && Settled())
			{
				Screenshot(std::string(s_Cactus.kind == terrain::PlantKind::SNAG ? "snag_" : "cactus_") + "1_before");
				FellPlant(s_Cactus, VEC3_ZERO);
				s_Step = 12;
				s_Time = 0.0f;
			}
			break;
		case 12:
			Aim(s_Cactus.base + VEC3_UP * 0.5f);
			if (s_Time > 1.0f && s_Time - g_UnpausedDeltaTime <= 1.0f)
			{
				Screenshot(std::string(s_Cactus.kind == terrain::PlantKind::SNAG ? "snag_" : "cactus_") + "2_falling");
			}
			if (s_Time > 10.0f)
			{
				std::vector<DroppedItem*> items;
				scene->GetDroppedItemsInRadius(s_Cactus.base, 20.0f, items);
				Print("Vegetation tour: felled a %s (height %.1f), %u dropped items nearby:\n", s_Cactus.kind == terrain::PlantKind::SNAG ? "burnt tree" : "cactus", s_Cactus.height, (u32)items.size());
				for (DroppedItem* item : items)
				{
					GameObject* prefab = g_ResourceManager->GetPrefabTemplate(item->prefabID);
					const glm::vec3 pos = item->GetTransform()->GetWorldPosition();
					Print("  %s x%d at (%.1f, %.1f, %.1f)\n", prefab != nullptr ? prefab->GetName().c_str() : "?", item->stackSize, pos.x, pos.y, pos.z);
				}
				Screenshot(std::string(s_Cactus.kind == terrain::PlantKind::SNAG ? "snag_" : "cactus_") + "3_dropped");
				s_Step = 100;
				s_Time = 0.0f;
			}
			break;
		case 100:
			if (s_Time > 1.0f)
			{
				Print("Vegetation tour: done\n");
				g_EngineInstance->Stop();
				s_Step = 101;
			}
			break;
		default:
			break;
		}
	}

	namespace
	{
		// Chunk mesh arrays which are no longer needed. Freeing large blocks can take a while (see
		// JobSystem::FreeInBackground), which adds up when many chunks are created or unloaded at once.
		std::vector<terrain::ChunkMeshData>* s_ChunkMeshGarbage = nullptr;

		void AddChunkMeshGarbage(terrain::ChunkMeshData&& mesh)
		{
			if (s_ChunkMeshGarbage == nullptr)
			{
				s_ChunkMeshGarbage = new std::vector<terrain::ChunkMeshData>();
			}
			s_ChunkMeshGarbage->push_back(std::move(mesh));
		}

		void FreeChunkMeshGarbage()
		{
			if (s_ChunkMeshGarbage != nullptr)
			{
				JobSystem::DeleteInBackground(s_ChunkMeshGarbage);
				s_ChunkMeshGarbage = nullptr;
			}
		}
	} // namespace

	TerrainGenerator::Chunk TerrainGenerator::CreateChunk(terrain::ChunkCommand& command, bool bHidden)
	{
		PROFILE_AUTO("TerrainGenerator CreateChunk");
		Material* terrainMat = g_Renderer->GetMaterial(m_TerrainMatID);
		Shader* terrainShader = g_Renderer->GetShader(terrainMat->shaderID);

		static VertexBufferDataCreateInfo vertexBufferCreateInfo = {};
		vertexBufferCreateInfo.attributes = terrainShader->vertexAttributes;

		auto CreateMeshComponent = [&](std::vector<glm::vec3>&& positions, std::vector<glm::vec3>&& normals, std::vector<glm::vec4>&& colours,
			std::vector<glm::vec4>&& weights, const std::vector<u32>& indices, bool bCastsShadow, i32* outSubmeshIndex, MaterialID materialID)
		{
			const u32 vertexCount = (u32)positions.size();

			glm::vec3 boundsMin(FLT_MAX);
			glm::vec3 boundsMax(-FLT_MAX);
			for (const glm::vec3& pos : positions)
			{
				boundsMin = glm::min(boundsMin, pos);
				boundsMax = glm::max(boundsMax, pos);
			}

			vertexBufferCreateInfo.positions_3D = std::move(positions);
			vertexBufferCreateInfo.normals = std::move(normals);
			vertexBufferCreateInfo.colours_R32G32B32A32 = std::move(colours);
			vertexBufferCreateInfo.extraVec4s = std::move(weights);

			RenderObjectCreateInfo renderObjectCreateInfo = {};
			renderObjectCreateInfo.cullFace = CullFace::NONE;
			// Chunk meshes never change after creation
			renderObjectCreateInfo.bAllowDynamicBufferShrinking = false;
			renderObjectCreateInfo.bCastsShadow = bCastsShadow;
			renderObjectCreateInfo.cullSphere = glm::vec4((boundsMin + boundsMax) * 0.5f, glm::length(boundsMax - boundsMin) * 0.5f);

			MeshComponent* meshComponent = MeshComponent::LoadFromMemoryDynamic(m_Mesh, vertexBufferCreateInfo, indices, materialID,
				vertexCount, &renderObjectCreateInfo, true, outSubmeshIndex);
			if (meshComponent != nullptr)
			{
				meshComponent->UpdateDynamicVertexData(vertexBufferCreateInfo, indices);
			}

			// The source arrays have been copied into the mesh, & are freed in the background (see FreeChunkMeshGarbage)
			terrain::ChunkMeshData garbage;
			garbage.positions = std::move(vertexBufferCreateInfo.positions_3D);
			garbage.normals = std::move(vertexBufferCreateInfo.normals);
			garbage.colours = std::move(vertexBufferCreateInfo.colours_R32G32B32A32);
			garbage.weights = std::move(vertexBufferCreateInfo.extraVec4s);
			AddChunkMeshGarbage(std::move(garbage));
			return meshComponent;
		};

		terrain::ChunkMeshData& mesh = command.mesh;

		Chunk chunk = {};
		chunk.key = command.key;
		chunk.boundsMin = glm::vec3(FLT_MAX);
		chunk.boundsMax = glm::vec3(-FLT_MAX);
		for (const glm::vec3& pos : mesh.positions)
		{
			chunk.boundsMin = glm::min(chunk.boundsMin, pos);
			chunk.boundsMax = glm::max(chunk.boundsMax, pos);
		}

		chunk.collisionIndexCount = mesh.collisionIndexCount;
		if (!mesh.indices.empty())
		{
			chunk.meshComponent = CreateMeshComponent(std::move(mesh.positions), std::move(mesh.normals), std::move(mesh.colours),
				std::move(mesh.weights), mesh.indices, true, &chunk.submeshIndex, m_TerrainMatID);
		}

		if (!mesh.waterIndices.empty())
		{
			const u32 waterVertexCount = (u32)mesh.waterPositions.size();
			for (const glm::vec3& pos : mesh.waterPositions)
			{
				chunk.boundsMin = glm::min(chunk.boundsMin, pos);
				chunk.boundsMax = glm::max(chunk.boundsMax, pos);
			}
			// Negative alpha marks water surfaces in the terrain shader
			static const glm::vec4 waterColour(0.06f, 0.20f, 0.28f, -1.0f);
			chunk.waterMeshComponent = CreateMeshComponent(std::move(mesh.waterPositions),
				std::vector<glm::vec3>(waterVertexCount, VEC3_UP),
				std::vector<glm::vec4>(waterVertexCount, waterColour),
				std::vector<glm::vec4>(waterVertexCount, VEC4_ZERO),
				mesh.waterIndices, false, &chunk.waterSubmeshIndex, m_WaterMatID != InvalidMaterialID ? m_WaterMatID : m_TerrainMatID);
		}

		if (bHidden)
		{
			SetChunkHidden(chunk, true);
		}

		return chunk;
	}

	TerrainGenerator::Chunk TerrainGenerator::CreateChunkTimed(terrain::ChunkCommand& command, bool bHidden)
	{
		const u64 start = Time::NowTicks();
		Chunk chunk = CreateChunk(command, bHidden);
		m_TotalUploadTime += Time::MillisecondsSince(start);
		++m_UploadCount;
		return chunk;
	}

	void TerrainGenerator::SetChunkHidden(Chunk& chunk, bool bHidden)
	{
		if (chunk.meshComponent != nullptr)
		{
			g_Renderer->SetRenderObjectHidden(chunk.meshComponent->renderID, bHidden);
		}
		if (chunk.waterMeshComponent != nullptr)
		{
			g_Renderer->SetRenderObjectHidden(chunk.waterMeshComponent->renderID, bHidden);
		}
	}

	void TerrainGenerator::ApplyChunkCommands()
	{
		if (m_ChunkCommands.empty())
		{
			return;
		}

		PROFILE_AUTO("TerrainGenerator ApplyChunkCommands");

		// (Float timestamps lose precision over long sessions, which would make the budget check unreliable)
		const u64 start = Time::NowTicks();
		// Uploads are spread over multiple frames to avoid hitches (with a larger budget while the initial load fills the
		// screen, or when falling behind while moving quickly)
		const ms frameBudget = m_bLoadComplete ? glm::mix(4.0f, 8.0f, glm::clamp((real)m_ChunkCommands.size() / 200.0f, 0.0f, 1.0f)) : 12.0f;
		auto IsOverBudget = [start, frameBudget]()
		{
			return Time::MillisecondsSince(start) > frameBudget;
		};

		// Commands are consumed from the front, then erased all at once (erasing one at a time is quadratic)
		size_t head = 0;
		const size_t commandCount = m_ChunkCommands.size();
		u32 uploadCount = 0;
		while (head < commandCount)
		{
			// Commands which must be applied in the same frame (so neighboring chunks always match)
			size_t groupEnd = head + 1;
			while (groupEnd < commandCount && m_ChunkCommands[groupEnd].bAtomicWithPrevious)
			{
				++groupEnd;
			}

			if (groupEnd == head + 1)
			{
				terrain::ChunkCommand& command = m_ChunkCommands[head];
				if (command.type == terrain::ChunkCommand::Type::HIDE)
				{
					auto iter = m_Chunks.find(command.handle);
					if (iter != m_Chunks.end())
					{
						DestroyChunk(iter->second);
						m_Chunks.erase(iter);
					}
				}
				else
				{
					if (uploadCount > 0 && IsOverBudget())
					{
						break;
					}
					m_Chunks.emplace(command.handle, CreateChunkTimed(command, false));
					++uploadCount;
				}
				++head;
				continue;
			}

			// Upload the group's new chunks hidden (over as many frames as needed), then swap them in all at once
			bool bAllStaged = true;
			for (size_t i = head; i < groupEnd; ++i)
			{
				terrain::ChunkCommand& command = m_ChunkCommands[i];
				if (command.type != terrain::ChunkCommand::Type::SHOW || m_StagedChunks.find(command.handle) != m_StagedChunks.end())
				{
					continue;
				}
				if (uploadCount > 0 && IsOverBudget())
				{
					bAllStaged = false;
					break;
				}
				m_StagedChunks.emplace(command.handle, CreateChunkTimed(command, true));
				++uploadCount;
			}

			if (!bAllStaged)
			{
				break;
			}

			for (size_t i = head; i < groupEnd; ++i)
			{
				terrain::ChunkCommand& command = m_ChunkCommands[i];
				if (command.type == terrain::ChunkCommand::Type::HIDE)
				{
					auto iter = m_Chunks.find(command.handle);
					if (iter != m_Chunks.end())
					{
						DestroyChunk(iter->second);
						m_Chunks.erase(iter);
					}
				}
				else
				{
					auto stagedIter = m_StagedChunks.find(command.handle);
					if (stagedIter != m_StagedChunks.end())
					{
						SetChunkHidden(stagedIter->second, false);
						m_Chunks.emplace(command.handle, stagedIter->second);
						m_StagedChunks.erase(stagedIter);
					}
				}
			}
			head = groupEnd;
		}
		for (size_t i = 0; i < head; ++i)
		{
			AddChunkMeshGarbage(std::move(m_ChunkCommands[i].mesh));
		}
		m_ChunkCommands.erase(m_ChunkCommands.begin(), m_ChunkCommands.begin() + head);
		FreeChunkMeshGarbage();

		m_LastApplyTime = Time::MillisecondsSince(start);
		m_MaxApplyTime = glm::max(m_MaxApplyTime, m_LastApplyTime);
	}

	// Collision built from a copy of the chunk's triangles (in a background job, the chunk may be unloaded meanwhile).
	// The triangles are relative to the chunk's origin (the body is placed there): with world space vertices, Bullet's
	// narrowphase & ray tests ran on coordinates in the hundreds of km far from the world origin, where floats are only
	// precise to centimeters, & e.g. rays fell through the cracks between triangles.
	struct ChunkCollisionBuild
	{
		u32 id = 0;
		u64 handle = 0;
		// (A whole number of meters, so subtracting it from the vertices is exact)
		glm::vec3 origin = VEC3_ZERO;
		std::vector<glm::vec3> positions;
		std::vector<u32> indices;
		u32 collisionIndexCount = 0;
		btTriangleIndexVertexArray* triangleIndexVertexArray = nullptr;
		btBvhTriangleMeshShape* collisionShape = nullptr;
		btTriangleIndexVertexArray* foliageTriangleIndexVertexArray = nullptr;
		btBvhTriangleMeshShape* foliageCollisionShape = nullptr;
		std::atomic<bool> bDone{ false };

		void CopyTriangles(const glm::vec3& chunkBoundsMin, MeshComponent* submesh, u32 chunkCollisionIndexCount)
		{
			VertexBufferData* vertexBufferData = submesh->GetVertexBufferData();
			const u32 indexCount = submesh->GetIndexCount();
			collisionIndexCount = glm::min(chunkCollisionIndexCount, indexCount);
			origin = glm::floor(chunkBoundsMin);
			// Positions are at the start of each vertex
			positions.resize(vertexBufferData->UsedVertexCount);
			const u8* src = (const u8*)vertexBufferData->vertexData;
			for (u32 v = 0; v < vertexBufferData->UsedVertexCount; ++v)
			{
				glm::vec3 position;
				memcpy(&position, src + (size_t)v * vertexBufferData->VertexStride, sizeof(glm::vec3));
				positions[v] = position - origin;
			}
			const u32* indices = submesh->GetIndexBufferDataPtr();
			this->indices.assign(indices, indices + indexCount);
		}

		void Build()
		{
			auto MakeShape = [this](u32 firstIndex, u32 indexCount, btTriangleIndexVertexArray*& outArray, btBvhTriangleMeshShape*& outShape)
			{
				btIndexedMesh part = {};
				part.m_vertexBase = (const unsigned char*)positions.data();
				part.m_vertexStride = sizeof(glm::vec3);
				part.m_numVertices = (i32)positions.size();
				part.m_triangleIndexBase = (const unsigned char*)(indices.data() + firstIndex);
				part.m_triangleIndexStride = sizeof(u32) * 3;
				part.m_numTriangles = (i32)(indexCount / 3);
				outArray = new btTriangleIndexVertexArray();
				outArray->addIndexedMesh(part, PHY_INTEGER);
				outShape = new btBvhTriangleMeshShape(outArray, true);
			};

			MakeShape(0, collisionIndexCount, triangleIndexVertexArray, collisionShape);
			if (collisionIndexCount < (u32)indices.size())
			{
				MakeShape(collisionIndexCount, (u32)indices.size() - collisionIndexCount, foliageTriangleIndexVertexArray, foliageCollisionShape);
			}
			bDone = true;
		}

		void DeleteShapes()
		{
			delete collisionShape;
			delete triangleIndexVertexArray;
			delete foliageCollisionShape;
			delete foliageTriangleIndexVertexArray;
			collisionShape = nullptr;
			triangleIndexVertexArray = nullptr;
			foliageCollisionShape = nullptr;
			foliageTriangleIndexVertexArray = nullptr;
		}
	};

	void TerrainGenerator::FinishCollisionBuilds()
	{
		for (u32 i = 0; i < (u32)m_CollisionBuilds.size();)
		{
			std::shared_ptr<ChunkCollisionBuild> build = m_CollisionBuilds[i];
			if (!build->bDone.load())
			{
				++i;
				continue;
			}
			m_CollisionBuilds[i] = m_CollisionBuilds.back();
			m_CollisionBuilds.pop_back();

			auto iter = m_Chunks.find(build->handle);
			if (iter != m_Chunks.end() && iter->second.collisionBuildID == build->id && iter->second.rigidBody == nullptr && iter->second.collisionShape == nullptr)
			{
				Chunk& chunk = iter->second;
				chunk.collisionBuildID = 0;
				chunk.triangleIndexVertexArray = build->triangleIndexVertexArray;
				chunk.collisionShape = build->collisionShape;
				chunk.foliageTriangleIndexVertexArray = build->foliageTriangleIndexVertexArray;
				chunk.foliageCollisionShape = build->foliageCollisionShape;
				// Owned by the chunk now
				build->triangleIndexVertexArray = nullptr;
				build->collisionShape = nullptr;
				build->foliageTriangleIndexVertexArray = nullptr;
				build->foliageCollisionShape = nullptr;
				chunk.collisionData = build;
				CreateChunkRigidBody(chunk);
			}
			else
			{
				// The chunk was unloaded (or moved out of range, or built immediately) in the meantime
				build->DeleteShapes();
			}
		}
	}

	void TerrainGenerator::DiscardCollisionBuilds()
	{
		// (Background jobs only stop running once the job system shuts down)
		while (JobSystem::IsBusy(m_CollisionBuildContext) && JobSystem::GetThreadCount() > 0)
		{
			std::this_thread::yield();
		}
		for (const std::shared_ptr<ChunkCollisionBuild>& build : m_CollisionBuilds)
		{
			if (build->bDone.load())
			{
				build->DeleteShapes();
			}
		}
		m_CollisionBuilds.clear();
	}

	void TerrainGenerator::UpdateRigidBodies()
	{
		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		const bool bHasPlayer = player != nullptr;
		const glm::vec3 playerPos = bHasPlayer ? player->GetTransform()->GetWorldPosition() : VEC3_ZERO;

		FinishCollisionBuilds();

		// Building collision BVHs takes a few ms per chunk, so (unless they're close to the player) it's done in the
		// background rather than stalling the frame whenever many chunks come into range
		static std::vector<Chunk*> toCreate;
		static std::vector<std::pair<real, std::pair<u64, Chunk*>>> toBuildInBackground;
		toCreate.clear();
		toBuildInBackground.clear();
		for (auto& pair : m_Chunks)
		{
			Chunk& chunk = pair.second;
			bool bShouldHaveRigidBody = false;
			real distance = 0.0f;
			if (bHasPlayer)
			{
				const glm::vec3 delta = glm::max(glm::max(chunk.boundsMin - playerPos, playerPos - chunk.boundsMax), VEC3_ZERO);
				distance = glm::length(delta);
				bShouldHaveRigidBody = distance < m_RigidBodyLoadRadius;
			}

			if (bShouldHaveRigidBody && chunk.rigidBody == nullptr)
			{
				// Chunks without any collidable triangles (e.g. only foliage) never get a body
				if (chunk.meshComponent == nullptr || chunk.meshComponent->GetIndexCount() == 0 || chunk.collisionIndexCount == 0)
				{
					continue;
				}

				if (distance < m_RigidBodyImmediateRadius)
				{
					// (Any build in flight is discarded once it finishes)
					chunk.collisionBuildID = 0;
					toCreate.push_back(&chunk);
				}
				else if (chunk.collisionBuildID == 0)
				{
					toBuildInBackground.push_back({ distance, { pair.first, &chunk } });
				}
			}
			else if (!bShouldHaveRigidBody)
			{
				chunk.collisionBuildID = 0;
				if (chunk.rigidBody != nullptr)
				{
					DestroyChunkRigidBody(chunk);
				}
			}
		}

		if (!toBuildInBackground.empty() && (u32)m_CollisionBuilds.size() < glm::max(JobSystem::GetThreadCount(), 2u))
		{
			PROFILE_AUTO("TerrainGenerator start collision builds");
			// Nearest first. Only a few are queued at a time so that chunks which come closer meanwhile aren't stuck
			// behind far away ones.
			std::sort(toBuildInBackground.begin(), toBuildInBackground.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
			const u32 maxInFlight = glm::max(JobSystem::GetThreadCount(), 2u);
			// (Copying each chunk's triangles takes a moment, so only a few are started per frame)
			const u32 maxStartsPerFrame = glm::max(JobSystem::GetThreadCount() / 2, 2u);
			u32 startCount = 0;
			for (const auto& entry : toBuildInBackground)
			{
				if ((u32)m_CollisionBuilds.size() >= maxInFlight || startCount++ >= maxStartsPerFrame)
				{
					break;
				}

				Chunk& chunk = *entry.second.second;
				std::shared_ptr<ChunkCollisionBuild> build = std::make_shared<ChunkCollisionBuild>();
				build->id = m_NextCollisionBuildID++;
				build->handle = entry.second.first;
				build->CopyTriangles(chunk.boundsMin, chunk.meshComponent, chunk.collisionIndexCount);

				chunk.collisionBuildID = build->id;
				m_CollisionBuilds.push_back(build);
				JobSystem::ExecuteBackground(m_CollisionBuildContext, [build]() { build->Build(); });
			}
		}

		if (toCreate.size() > 1)
		{
			// Building the collision BVHs is the expensive part (many chunks at once when a world loads), & is
			// independent per chunk. Bodies are added to the physics world afterwards, on this thread.
			PROFILE_AUTO("TerrainGenerator BuildChunkCollisionShapes");
			JobSystem::Context context;
			JobSystem::Dispatch(context, (u32)toCreate.size(), 1, [](JobSystem::JobArgs args) { BuildChunkCollisionShapes(*toCreate[args.jobIndex]); });
			JobSystem::Wait(context);
		}
		for (Chunk* chunk : toCreate)
		{
			PROFILE_AUTO("TerrainGenerator CreateChunkRigidBody");
			CreateChunkRigidBody(*chunk);
		}
	}

	bool TerrainGenerator::Raycast(const glm::vec3& origin, const glm::vec3& dir, real maxDistance, glm::vec3& outHitPos, glm::vec3& outHitNormal) const
	{
		return m_Streamer != nullptr && m_Streamer->Raycast(origin, dir, maxDistance, outHitPos, outHitNormal);
	}

	void TerrainGenerator::Dig(const glm::vec3& center, real radius, bool bRemoveFragments /* = true */)
	{
		terrain::Edit edit = {};
		edit.center = center;
		edit.radius = radius;
		edit.type = terrain::EditType::DIG;
		edit.material = terrain::Material::SOIL;

		std::vector<terrain::Edit> edits = { edit };
		if (bRemoveFragments && m_Streamer != nullptr)
		{
			FindFloatingFragments(center, radius + m_Streamer->Params().voxelSize * 2.0f, edits);
		}

		if (m_Streamer == nullptr)
		{
			return;
		}

		// Plants whose roots are dug out fall over (found before the dig changes where the ground is)
		std::vector<terrain::PlantInfo> plants;
		const glm::vec3 plantSearchExtent(radius + 1.5f, radius + 6.0f, radius + 1.5f);
		m_Streamer->FindPlants(center - plantSearchExtent, center + plantSearchExtent, plants);
		std::vector<terrain::PlantInfo> undermined;
		for (const terrain::PlantInfo& plant : plants)
		{
			// The ground just below the trunk's base
			const glm::vec3 root = plant.base - glm::vec3(0.0f, 0.3f, 0.0f);
			if (glm::distance(root, center) < radius + plant.trunkRadius * 0.5f + 0.3f)
			{
				undermined.push_back(plant);
				edits.push_back(terrain::MakeRemovePlantEdit(plant));
			}
		}

		m_Streamer->ApplyEdits(edits.data(), (u32)edits.size());
		MarkEditsDirty();

		for (const terrain::PlantInfo& plant : undermined)
		{
			// Topple away from the hole
			glm::vec3 away = plant.base - center;
			away.y = 0.0f;
			if (s_OnPlantFelled != nullptr)
			{
				s_OnPlantFelled(plant, glm::length(away) > 1e-3f ? glm::normalize(away) : VEC3_ZERO);
			}
		}

		if (s_OnTerrainDug != nullptr)
		{
			s_OnTerrainDug(center, radius);
		}
	}

	void TerrainGenerator::FindBerryBushes(const glm::vec3& boxMin, const glm::vec3& boxMax, std::vector<terrain::BerryBushInfo>& outBushes) const
	{
		if (m_Streamer != nullptr)
		{
			m_Streamer->FindBerryBushes(boxMin, boxMax, outBushes);
		}
	}

	void TerrainGenerator::FindPlants(const glm::vec3& boxMin, const glm::vec3& boxMax, std::vector<terrain::PlantInfo>& outPlants) const
	{
		if (m_Streamer != nullptr)
		{
			m_Streamer->FindPlants(boxMin, boxMax, outPlants);
		}
	}

	void TerrainGenerator::BuildPlantMesh(const terrain::PlantInfo& plant, terrain::ChunkMeshData& outMesh) const
	{
		if (m_Streamer != nullptr)
		{
			terrain::BuildPlantMesh(*m_Streamer->Density(), plant, outMesh);
		}
	}

	void TerrainGenerator::FellPlant(const terrain::PlantInfo& plant, const glm::vec3& fallDirection)
	{
		if (m_Streamer == nullptr)
		{
			return;
		}

		ApplyEdit(terrain::MakeRemovePlantEdit(plant));
		if (s_OnPlantFelled != nullptr)
		{
			s_OnPlantFelled(plant, fallDirection);
		}
	}

	bool TerrainGenerator::PersistsEdits() const
	{
		// Scene tests (which dig, fill & build cliffs) start from the generated terrain each run, rather than piling
		// up the previous runs' edits. Worlds (with their own save directory) always keep theirs.
		return !m_SaveDirectory.empty() || !SceneTestRunner::IsRunning();
	}

	void TerrainGenerator::RemovePlant(const terrain::PlantInfo& plant)
	{
		if (m_Streamer != nullptr)
		{
			ApplyEdit(terrain::MakeRemovePlantEdit(plant));
		}
	}

	void TerrainGenerator::FindFloatingFragments(const glm::vec3& center, real extent, std::vector<terrain::Edit>& inOutEdits) const
	{
		// Samples the density at the grid points chunks are meshed from (level 0, which is what's shown around the
		// player), & flood fills the solid ones from the edges of the region. Solid points which can't be reached are
		// cut off from everything around them: small groups are dug away, large ones are left (they're more
		// noticeable to remove than to leave floating).
		PROFILE_AUTO("TerrainGenerator FindFloatingFragments");

		const terrain::TerrainDensity& density = *m_Streamer->Density();
		const real voxel = density.Params().voxelSize;
		const glm::ivec3 lo = glm::ivec3(glm::floor((center - extent) / voxel));
		const glm::ivec3 hi = glm::ivec3(glm::ceil((center + extent) / voxel));
		const glm::ivec3 dims = hi - lo + glm::ivec3(1);
		const i32 pointCount = dims.x * dims.y * dims.z;
		if (pointCount <= 0 || pointCount > 32 * 32 * 32)
		{
			return;
		}

		std::vector<terrain::Edit> edits;
		const glm::vec3 boxMin = glm::vec3(lo) * voxel - density.EditBand();
		const glm::vec3 boxMax = glm::vec3(hi) * voxel + density.EditBand();
		m_Streamer->Edits().Gather(boxMin, boxMax, edits);
		edits.insert(edits.end(), inOutEdits.begin(), inOutEdits.end());
		terrain::EditList editList;
		editList.edits = edits.data();
		editList.count = (u32)edits.size();

		auto Index = [&](i32 x, i32 y, i32 z) { return (z * dims.y + y) * dims.x + x; };

		std::vector<u8> solid(pointCount);
		for (i32 z = 0; z < dims.z; ++z)
		{
			for (i32 x = 0; x < dims.x; ++x)
			{
				const real wx = (real)(lo.x + x) * voxel;
				const real wz = (real)(lo.z + z) * voxel;
				const terrain::ColumnSample column = density.SampleColumn(wx, wz);
				for (i32 y = 0; y < dims.y; ++y)
				{
					const glm::vec3 pos(wx, (real)(lo.y + y) * voxel, wz);
					solid[Index(x, y, z)] = density.Sample(pos, column, editList) < 0.0f ? 1 : 0;
				}
			}
		}

		// 0: unvisited, 1: connected to the region's edges, 2: floating
		std::vector<u8> state(pointCount, 0);
		std::vector<glm::ivec3> stack;
		auto Flood = [&](const glm::ivec3& start, u8 value, std::vector<glm::ivec3>* outPoints)
		{
			stack.clear();
			stack.push_back(start);
			state[Index(start.x, start.y, start.z)] = value;
			while (!stack.empty())
			{
				const glm::ivec3 p = stack.back();
				stack.pop_back();
				if (outPoints != nullptr)
				{
					outPoints->push_back(p);
				}
				// Marching tetrahedra can connect diagonal neighbors, so use all 26 to be safe
				for (i32 dz = -1; dz <= 1; ++dz)
				{
					for (i32 dy = -1; dy <= 1; ++dy)
					{
						for (i32 dx = -1; dx <= 1; ++dx)
						{
							const glm::ivec3 n = p + glm::ivec3(dx, dy, dz);
							if (n.x < 0 || n.y < 0 || n.z < 0 || n.x >= dims.x || n.y >= dims.y || n.z >= dims.z)
							{
								continue;
							}
							const i32 ni = Index(n.x, n.y, n.z);
							if (solid[ni] != 0 && state[ni] == 0)
							{
								state[ni] = value;
								stack.push_back(n);
							}
						}
					}
				}
			}
		};

		for (i32 z = 0; z < dims.z; ++z)
		{
			for (i32 y = 0; y < dims.y; ++y)
			{
				for (i32 x = 0; x < dims.x; ++x)
				{
					const bool bEdge = x == 0 || y == 0 || z == 0 || x == dims.x - 1 || y == dims.y - 1 || z == dims.z - 1;
					const i32 i = Index(x, y, z);
					if (bEdge && solid[i] != 0 && state[i] == 0)
					{
						Flood(glm::ivec3(x, y, z), 1, nullptr);
					}
				}
			}
		}

		const u32 maxFragmentPoints = 24;
		std::vector<glm::ivec3> fragment;
		u32 removedCount = 0;
		for (i32 i = 0; i < pointCount; ++i)
		{
			if (solid[i] == 0 || state[i] != 0)
			{
				continue;
			}
			const i32 x = i % dims.x;
			const i32 y = (i / dims.x) % dims.y;
			const i32 z = i / (dims.x * dims.y);
			fragment.clear();
			Flood(glm::ivec3(x, y, z), 2, &fragment);
			if (fragment.size() > maxFragmentPoints)
			{
				continue;
			}
			for (const glm::ivec3& point : fragment)
			{
				terrain::Edit edit = {};
				edit.center = glm::vec3(lo + point) * voxel;
				// Just enough to clear this point (barely touching its neighbors)
				edit.radius = voxel * 0.6f;
				edit.type = terrain::EditType::DIG;
				edit.material = terrain::Material::SOIL;
				inOutEdits.push_back(edit);
			}
			++removedCount;
		}

		if (removedCount > 0)
		{
			Print("Removed %u floating terrain fragment%s\n", removedCount, removedCount == 1 ? "" : "s");
		}
	}

	void TerrainGenerator::Fill(const glm::vec3& center, real radius, terrain::Material material)
	{
		terrain::Edit edit = {};
		edit.center = center;
		edit.radius = radius;
		edit.type = terrain::EditType::FILL;
		edit.material = material;
		ApplyEdit(edit);
	}

	void TerrainGenerator::FlattenArea(const glm::vec3& center, const glm::vec3& normal, real radius, terrain::Material material /* = SOIL */, real height /* = 0.0f */)
	{
		ApplyPlaneEdits({ terrain::MakePlaneEdit(terrain::EditType::FLATTEN, center, normal, radius, height, material) });
	}

	void TerrainGenerator::LowerArea(const glm::vec3& center, const glm::vec3& normal, real radius, real height /* = 0.0f */)
	{
		ApplyPlaneEdits({ terrain::MakePlaneEdit(terrain::EditType::LOWER, center, normal, radius, height, terrain::Material::SOIL) });
	}

	void TerrainGenerator::RaiseArea(const glm::vec3& center, const glm::vec3& normal, real radius, terrain::Material material, real height /* = 0.0f */)
	{
		ApplyPlaneEdits({ terrain::MakePlaneEdit(terrain::EditType::RAISE, center, normal, radius, height, material) });
	}

	void TerrainGenerator::FlattenPath(const std::vector<glm::vec3>& points, real radius, terrain::Material material /* = GRAVEL */, real height /* = 0.0f */)
	{
		if (points.empty() || radius <= 0.0f)
		{
			return;
		}

		// Discs close enough together that their flat middles overlap
		const real spacing = radius * 0.4f;
		std::vector<terrain::Edit> edits;
		auto AddDisc = [&](const glm::vec3& pos, const glm::vec3& tangent)
		{
			// Tilted along the path, level across it
			glm::vec3 normal = VEC3_UP - tangent * glm::dot(VEC3_UP, tangent);
			normal = glm::length(normal) > 1e-3f ? glm::normalize(normal) : VEC3_UP;
			edits.push_back(terrain::MakePlaneEdit(terrain::EditType::FLATTEN, pos, normal, radius, height, material));
		};

		if (points.size() == 1)
		{
			AddDisc(points[0], VEC3_ZERO);
		}
		for (size_t i = 0; i + 1 < points.size(); ++i)
		{
			const glm::vec3 a = points[i];
			const glm::vec3 b = points[i + 1];
			const real length = glm::distance(a, b);
			if (length < 1e-4f)
			{
				continue;
			}
			const glm::vec3 tangent = (b - a) / length;
			const i32 steps = glm::max((i32)std::ceil(length / spacing), 1);
			// (each segment's last disc is the next one's first)
			const bool bLast = i + 2 == points.size();
			for (i32 step = 0; step < steps + (bLast ? 1 : 0); ++step)
			{
				AddDisc(a + (b - a) * ((real)step / (real)steps), tangent);
			}
		}
		ApplyPlaneEdits(edits);
	}

	void TerrainGenerator::ApplyPlaneEdits(const std::vector<terrain::Edit>& planeEdits)
	{
		if (m_Streamer == nullptr || planeEdits.empty())
		{
			return;
		}

		glm::vec3 boundsMin(1e30f);
		glm::vec3 boundsMax(-1e30f);
		for (const terrain::Edit& edit : planeEdits)
		{
			glm::vec3 editMin, editMax;
			terrain::GetEditBounds(edit, editMin, editMax);
			boundsMin = glm::min(boundsMin, editMin);
			boundsMax = glm::max(boundsMax, editMax);
		}

		// Plants whose roots are cut away fall over, & ones buried are removed (found before the edit changes where the
		// ground is)
		std::vector<terrain::PlantInfo> plants;
		m_Streamer->FindPlants(boundsMin - glm::vec3(1.5f, 6.0f, 1.5f), boundsMax + glm::vec3(1.5f, 6.0f, 1.5f), plants);
		std::vector<terrain::Edit> edits = planeEdits;
		std::vector<terrain::PlantInfo> undermined;
		if (!plants.empty())
		{
			const terrain::TerrainDensity& density = *m_Streamer->Density();
			std::vector<terrain::Edit> after;
			m_Streamer->Edits().Gather(boundsMin - glm::vec3(8.0f), boundsMax + glm::vec3(8.0f), after);
			after.insert(after.end(), planeEdits.begin(), planeEdits.end());
			terrain::EditList afterList;
			afterList.edits = after.data();
			afterList.count = (u32)after.size();
			for (const terrain::PlantInfo& plant : plants)
			{
				const glm::vec3 root = plant.base - glm::vec3(0.0f, 0.3f, 0.0f);
				const bool bUndermined = density.Sample(root, afterList) > 0.0f;
				const bool bBuried = !bUndermined && density.Sample(plant.base + glm::vec3(0.0f, 1.2f, 0.0f), afterList) < 0.0f;
				if (bUndermined || bBuried)
				{
					edits.push_back(terrain::MakeRemovePlantEdit(plant));
				}
				if (bUndermined)
				{
					undermined.push_back(plant);
				}
			}
		}

		m_Streamer->ApplyEdits(edits.data(), (u32)edits.size());
		MarkEditsDirty();

		const glm::vec3 boundsCenter = (boundsMin + boundsMax) * 0.5f;
		for (const terrain::PlantInfo& plant : undermined)
		{
			glm::vec3 away = plant.base - boundsCenter;
			away.y = 0.0f;
			if (s_OnPlantFelled != nullptr)
			{
				s_OnPlantFelled(plant, glm::length(away) > 1e-3f ? glm::normalize(away) : VEC3_ZERO);
			}
		}

		// Lowering (or flattening) can cut away the ground below things too
		if (s_OnTerrainDug != nullptr)
		{
			for (const terrain::Edit& edit : planeEdits)
			{
				if (edit.type != terrain::EditType::RAISE)
				{
					s_OnTerrainDug(edit.center, edit.radius);
				}
			}
		}
	}

	void TerrainGenerator::RemoveFloatingFragments(const glm::vec3& center, real extent)
	{
		if (m_Streamer == nullptr)
		{
			return;
		}
		std::vector<terrain::Edit> edits;
		FindFloatingFragments(center, extent, edits);
		if (!edits.empty())
		{
			m_Streamer->ApplyEdits(edits.data(), (u32)edits.size());
			MarkEditsDirty();
		}
	}

	glm::vec3 TerrainGenerator::SampleSmoothNormal(const glm::vec3& pos, real radius) const
	{
		if (m_Streamer == nullptr)
		{
			return VEC3_UP;
		}

		// Density gradients over a couple of large steps: each difference spans the surface around pos, so small bumps
		// cancel out
		glm::vec3 sum = VEC3_ZERO;
		for (real scale : { 0.5f, 1.0f })
		{
			const real e = glm::max(radius * scale, 0.1f);
			glm::vec3 gradient(
				SampleDensity(pos + glm::vec3(e, 0, 0)) - SampleDensity(pos - glm::vec3(e, 0, 0)),
				SampleDensity(pos + glm::vec3(0, e, 0)) - SampleDensity(pos - glm::vec3(0, e, 0)),
				SampleDensity(pos + glm::vec3(0, 0, e)) - SampleDensity(pos - glm::vec3(0, 0, e)));
			const real length = glm::length(gradient);
			if (length > 1e-6f)
			{
				sum += gradient / length;
			}
		}
		const real length = glm::length(sum);
		return length > 1e-6f ? sum / length : VEC3_UP;
	}

	bool TerrainGenerator::IsApplyingEdits() const
	{
		return m_Streamer != nullptr && m_Streamer->GetStats().editJobCount > 0;
	}

	real TerrainGenerator::SampleDensity(const glm::vec3& pos) const
	{
		return m_Streamer != nullptr ? m_Streamer->SampleDensity(pos) : 1.0f;
	}

	terrain::SurfaceAttributes TerrainGenerator::SampleSurface(const glm::vec3& pos, const glm::vec3& normal) const
	{
		return m_Streamer != nullptr ? m_Streamer->SampleSurface(pos, normal) : terrain::SurfaceAttributes{};
	}

	u32 TerrainGenerator::GetEditCount() const
	{
		return m_Streamer != nullptr ? (u32)m_Streamer->Edits().Edits().size() : 0;
	}

	bool TerrainGenerator::FindPlacedMaterial(const glm::vec3& pos, terrain::Material& outMaterial) const
	{
		if (m_Streamer == nullptr)
		{
			return false;
		}

		const real tolerance = m_Params.voxelSize * 0.5f;
		std::vector<terrain::Edit> edits;
		m_Streamer->Edits().Gather(pos - glm::vec3(tolerance), pos + glm::vec3(tolerance), edits);
		// The latest edit containing the point wins (like TerrainDensity::ComputeSurface)
		for (auto iter = edits.rbegin(); iter != edits.rend(); ++iter)
		{
			if (terrain::IsPlaneEdit(iter->type))
			{
				if (terrain::PlaneEditWeight(*iter, pos - iter->center) <= 0.0f)
				{
					continue;
				}
				// Placed where there was air before the edit
				terrain::EditList before;
				before.edits = edits.data();
				before.count = (u32)(edits.rend() - iter) - 1;
				if (iter->type != terrain::EditType::LOWER && m_Streamer->Density()->Sample(pos, before) > tolerance)
				{
					outMaterial = iter->material;
					return true;
				}
				if (iter->type == terrain::EditType::RAISE)
				{
					continue;
				}
				return false;
			}
			if (iter->type == terrain::EditType::REMOVE_PLANT || glm::distance(pos, iter->center) > iter->radius + tolerance)
			{
				continue;
			}
			if (iter->type == terrain::EditType::FILL)
			{
				outMaterial = iter->material;
				return true;
			}
			return false;
		}
		return false;
	}

	terrain::ColumnSample TerrainGenerator::SampleColumn(real x, real z) const
	{
		if (m_Streamer == nullptr)
		{
			return {};
		}
		return m_Streamer->Density()->SampleColumn(x, z);
	}

	bool TerrainGenerator::FindStandingPoint(const glm::vec3& pos, bool bKeepHeight, glm::vec3& outPos) const
	{
		if (m_Streamer == nullptr)
		{
			return false;
		}

		const terrain::TerrainDensity& density = *m_Streamer->Density();
		const glm::vec3 down(0.0f, -1.0f, 0.0f);
		glm::vec3 hitPos, hitNormal;

		auto IsUnderWater = [&](const glm::vec3& point)
		{
			const terrain::ColumnSample column = density.SampleColumn(point.x, point.z);
			return column.lakeID != 0 && column.lakeT < 1.0f && point.y < column.waterLevel + 0.5f;
		};

		// Stay at this height if there's ground below (in a cave, or dropping from the sky)
		if (bKeepHeight && m_Streamer->SampleDensity(pos) > 0.5f &&
			Raycast(pos, down, 5000.0f, hitPos, hitNormal) &&
			!IsUnderWater(hitPos))
		{
			outPos = hitPos;
			return true;
		}

		// Otherwise use the top surface, searching outwards in rings for dry land
		static const real ringRadii[] = { 0.0f, 40.0f, 100.0f, 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f, 6400.0f };
		for (real radius : ringRadii)
		{
			const u32 sampleCount = radius == 0.0f ? 1 : 16;
			for (u32 i = 0; i < sampleCount; ++i)
			{
				const real angle = TWO_PI * (real)i / (real)sampleCount;
				const real x = pos.x + std::cos(angle) * radius;
				const real z = pos.z + std::sin(angle) * radius;
				const terrain::ColumnSample column = density.SampleColumn(x, z);
				if (column.lakeID != 0 && column.lakeT < 1.0f && column.height < column.waterLevel + 0.5f)
				{
					continue;
				}

				// Start above anything the 3D features (overhangs, arches) could add on top of the heightfield
				const glm::vec3 top(x, column.height + density.MaxExtentAbove() + 10.0f, z);
				const real rayLength = density.MaxExtentAbove() + density.MaxExtentBelow() + 20.0f;
				if (Raycast(top, down, rayLength, hitPos, hitNormal) && !IsUnderWater(hitPos))
				{
					outPos = hitPos;
					return true;
				}
			}
		}

		// No dry land nearby (e.g. far out in an ocean): the bottom of the water, rather than failing (which left the
		// player falling without waiting for collision to load, through the sea floor & on forever)
		const terrain::ColumnSample column = density.SampleColumn(pos.x, pos.z);
		const glm::vec3 top(pos.x, glm::max(column.height + density.MaxExtentAbove(), pos.y) + 10.0f, pos.z);
		const real rayLength = top.y - column.height + density.MaxExtentBelow() + 20.0f;
		if (Raycast(top, down, rayLength, hitPos, hitNormal))
		{
			outPos = hitPos;
			return true;
		}

		return false;
	}

	bool TerrainGenerator::IsFallenThrough(const glm::vec3& pos) const
	{
		if (m_Streamer == nullptr)
		{
			return false;
		}
		// (Holes dug deeper than the generated features are open air, so the density check keeps those)
		const terrain::TerrainDensity& density = *m_Streamer->Density();
		const terrain::ColumnSample column = density.SampleColumn(pos.x, pos.z);
		return pos.y < column.height - density.MaxExtentBelow() - 50.0f && m_Streamer->SampleDensity(pos) < 0.0f;
	}

	bool TerrainGenerator::IsCollisionLoadedBelow(const glm::vec3& pos) const
	{
		glm::vec3 groundPos, groundNormal;
		if (!Raycast(pos, glm::vec3(0.0f, -1.0f, 0.0f), 500.0f, groundPos, groundNormal))
		{
			// No terrain below to wait for
			return true;
		}

		// Collision meshes are coarser than the density, so allow some slack past the expected hit
		const btVector3 rayStart = ToBtVec3(pos);
		const btVector3 rayEnd = ToBtVec3(groundPos - glm::vec3(0.0f, 3.0f, 0.0f));
		btCollisionWorld::ClosestRayResultCallback rayCallback(rayStart, rayEnd);
		rayCallback.m_collisionFilterMask = (i32)CollisionType::STATIC;
		g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld()->rayTest(rayStart, rayEnd, rayCallback);
		return rayCallback.hasHit();
	}

	void TerrainGenerator::ApplyEdit(const terrain::Edit& edit)
	{
		if (m_Streamer == nullptr)
		{
			return;
		}

		m_Streamer->ApplyEdit(edit);
		MarkEditsDirty();
	}

	void TerrainGenerator::MarkEditsDirty()
	{
		if (!m_bEditsDirty)
		{
			m_bEditsDirty = true;
			m_EditsDirtyTime = g_SecElapsedSinceProgramStart;
		}
	}

	void TerrainGenerator::SaveEdits()
	{
		if (!m_bEditsDirty || m_Streamer == nullptr)
		{
			return;
		}

		m_bEditsDirty = false;
		if (m_bEnableDiskCache && PersistsEdits())
		{
			// (the seed the edits were made in, which may differ from the current one while switching)
			std::string filePath;
			if (!m_SaveDirectory.empty())
			{
				filePath = m_SaveDirectory + "terrain_" + std::to_string(m_LoadedEditsSeed) + ".edits";
			}
			else
			{
				const std::string sceneName = StripFileType(g_SceneManager->CurrentScene()->GetFileName());
				filePath = RelativePathToAbsolute(SAVED_DIRECTORY "terrain/") + sceneName + "_" + std::to_string(m_LoadedEditsSeed) + ".edits";
			}
			if (!terrain::WriteEdits(filePath, m_LoadedEditsSeed, m_Streamer->Edits().Edits()))
			{
				PrintError("Failed to save terrain edits to %s\n", filePath.c_str());
			}
		}
	}

	void TerrainGenerator::UpdateEditorBrush()
	{
		if (!m_bEditorBrush || m_Streamer == nullptr)
		{
			return;
		}

		m_BrushCooldown -= g_DeltaTime;

		btVector3 rayStart, rayEnd;
		FlexEngine::GenerateRayAtMousePos(rayStart, rayEnd);
		const glm::vec3 origin = ToVec3(rayStart);
		const glm::vec3 dir = glm::normalize(ToVec3(rayEnd) - origin);

		glm::vec3 hitPos, hitNormal;
		if (!Raycast(origin, dir, 2000.0f, hitPos, hitNormal))
		{
			return;
		}

		g_Renderer->GetDebugRenderer()->drawSphere(ToBtVec3(hitPos), m_BrushRadius, btVector3(0.9f, 0.6f, 0.1f));

		// LMB: dig, Shift + LMB: fill
		if (g_InputManager->IsMouseButtonDown(MouseButton::LEFT) && m_BrushCooldown <= 0.0f)
		{
			m_BrushCooldown = 0.12f;
			if (g_InputManager->GetKeyDown(KeyCode::KEY_LEFT_SHIFT) != 0)
			{
				const i32 materialCount = (i32)ARRAY_LENGTH(terrain::FILL_MATERIALS);
				Fill(hitPos + hitNormal * m_BrushRadius * 0.5f, m_BrushRadius, terrain::FILL_MATERIALS[glm::clamp(m_BrushMaterialIndex, 0, materialCount - 1)]);
			}
			else
			{
				Dig(hitPos, m_BrushRadius);
			}
		}
	}

	void TerrainGenerator::Destroy(bool bDetachFromParent /* = true */)
	{
		if (s_Active == this)
		{
			s_Active = nullptr;
			g_Renderer->SetShadowDistanceOverride(0.0f);
		}

		if (m_WorldMapFuture.valid())
		{
			m_WorldMapFuture.wait();
			m_WorldMapFuture = {};
		}
		if (m_MinimapFuture.valid())
		{
			m_MinimapFuture.wait();
			m_MinimapFuture = {};
		}
		DestroyMinimapTextures(true);

		if (m_Streamer != nullptr)
		{
			SaveEdits();
			if (m_bEnableDiskCache && !m_bSnapshotFinal)
			{
				// (Cameras may have already been destroyed)
				m_Streamer->SaveSnapshot(m_LastCenter);
			}
			// Waits for the snapshot to finish writing
			m_Streamer->Stop();
			delete m_Streamer;
			m_Streamer = nullptr;
		}
		m_ChunkCommands.clear();
		m_QueryDensity.reset();

		DiscardCollisionBuilds();
		DestroyAllChunks();

		m_RoadSegments.clear();

		GameObject::Destroy(bDetachFromParent);
	}

	bool TerrainGenerator::DrawBiomeImGui(terrain::BiomeParams& biome)
	{
		bool bChanged = false;

		static char nameBuffer[128];
		strncpy(nameBuffer, biome.name.c_str(), ARRAY_LENGTH(nameBuffer) - 1);
		nameBuffer[ARRAY_LENGTH(nameBuffer) - 1] = '\0';
		if (ImGui::InputText("Name", nameBuffer, ARRAY_LENGTH(nameBuffer)))
		{
			biome.name = nameBuffer;
		}

		bChanged = ImGui::SliderFloat("Frequency", &biome.frequency, 0.0f, 4.0f) || bChanged;
		bChanged = ImGui::SliderFloat("Temperature", &biome.temperature, -1.0f, 1.0f) || bChanged;
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Preferred climate (-1: polar, 1: desert)");
		}

		ImGui::Text("Heightfield");
		bChanged = ImGui::DragFloat("Base height", &biome.baseHeight, 1.0f, -2000.0f, 4000.0f) || bChanged;
		bChanged = ImGui::DragFloat("Height amplitude", &biome.heightAmplitude, 1.0f, 0.0f, 4000.0f) || bChanged;
		bChanged = ImGui::DragFloat("Wavelength", &biome.wavelength, 5.0f, 10.0f, 20000.0f) || bChanged;
		bChanged = ImGui::SliderFloat("Ridgedness", &biome.ridgedness, 0.0f, 1.0f) || bChanged;
		bChanged = ImGui::SliderFloat("Terrace strength", &biome.terraceStrength, 0.0f, 1.0f) || bChanged;
		bChanged = ImGui::DragFloat("Terrace height", &biome.terraceHeight, 0.5f, 1.0f, 500.0f) || bChanged;
		bChanged = ImGui::SliderFloat("Roughness", &biome.roughness, 0.2f, 0.65f) || bChanged;
		bChanged = ImGui::SliderFloat("Dune strength", &biome.duneStrength, 0.0f, 1.0f) || bChanged;
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Sand dunes: height amplitude sets their height, wavelength their spacing");
		}
		bChanged = ImGui::SliderFloat("Canyon strength", &biome.canyonStrength, 0.0f, 1.0f) || bChanged;
		bChanged = ImGui::DragFloat("Canyon depth", &biome.canyonDepth, 1.0f, 0.0f, 1000.0f) || bChanged;
		bChanged = ImGui::SliderFloat("Fjord strength", &biome.fjordStrength, 0.0f, 1.0f) || bChanged;
		bChanged = ImGui::SliderFloat("Spire strength", &biome.spireStrength, 0.0f, 1.0f) || bChanged;
		bChanged = ImGui::DragFloat("Spire height", &biome.spireHeight, 1.0f, 0.0f, 600.0f) || bChanged;
		bChanged = ImGui::SliderFloat("Coastal", &biome.coastal, 0.0f, 1.0f) || bChanged;
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("How strongly this biome prefers coastlines");
		}

		ImGui::Text("Features");
		bChanged = ImGui::DragFloat("Overhang amplitude", &biome.overhangAmplitude, 0.5f, 0.0f, 200.0f) || bChanged;
		bChanged = ImGui::SliderFloat("Arch strength", &biome.archStrength, 0.0f, 1.0f) || bChanged;
		bChanged = ImGui::SliderFloat("Cave strength", &biome.caveStrength, 0.0f, 1.0f) || bChanged;
		bChanged = ImGui::SliderFloat("Lake strength", &biome.lakeStrength, 0.0f, 1.0f) || bChanged;

		ImGui::Text("Surface");
		bChanged = ImGui::ColorEdit3("Ground", &biome.groundColour.x) || bChanged;
		bChanged = ImGui::SliderFloat("Grassiness", &biome.grassiness, 0.0f, 1.0f) || bChanged;
		bChanged = ImGui::SliderFloat("Sandy shores", &biome.sandyShores, 0.0f, 1.0f) || bChanged;
bChanged = ImGui::ColorEdit3("Cliff", &biome.cliffColour.x) || bChanged;
		bChanged = ImGui::ColorEdit3("Peak", &biome.peakColour.x) || bChanged;
		bChanged = ImGui::DragFloat("Peak height", &biome.peakHeight, 5.0f, -1000.0f, 100000.0f) || bChanged;

		ImGui::Text("Vegetation");
		if (ImGui::BeginCombo("Type", terrain::VegetationName(biome.vegetation)))
		{
			for (u32 i = 0; i < (u32)terrain::Vegetation::_COUNT; ++i)
			{
				if (ImGui::Selectable(terrain::VegetationName((terrain::Vegetation)i), (u32)biome.vegetation == i))
				{
					biome.vegetation = (terrain::Vegetation)i;
					bChanged = true;
				}
			}
			ImGui::EndCombo();
		}
		bChanged = ImGui::SliderFloat("Tree density", &biome.treeDensity, 0.0f, 3.0f) || bChanged;
		bChanged = ImGui::SliderFloat("Bush density", &biome.bushDensity, 0.0f, 3.0f) || bChanged;

		return bChanged;
	}

	void TerrainGenerator::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		if (m_Streamer != nullptr)
		{
			terrain::TerrainStreamer::Stats stats = m_Streamer->GetStats();
			ImGui::Text("Chunks: %u displayed (%u non-empty), %u desired", stats.displayedChunkCount, stats.displayedNonEmptyChunkCount, stats.desiredChunkCount);
			ImGui::Text("Pending: %u (queued: %u), retiring: %u, edits in flight: %u", stats.pendingChunkCount, stats.queuedJobCount, stats.retiringChunkCount, stats.editJobCount);
			ImGui::Text("Generated: %llu, avg %.2fms/chunk on %u threads", (unsigned long long)stats.totalGenerated, stats.averageGenerationTime, stats.threadCount);
			ImGui::Text("Upload: %.2fms (max %.2fms, avg %.2fms/chunk), queued commands: %u", m_LastApplyTime, m_MaxApplyTime,
				m_UploadCount > 0 ? m_TotalUploadTime / (real)m_UploadCount : 0.0f, (u32)m_ChunkCommands.size());
			ImGui::Text("Camera speed: %.0f m/s, batch latency: %.2fs, swap groups: %u/%u", glm::length(stats.velocity), stats.batchLatency,
				stats.pendingSwapGroupCount, stats.swapGroupCount);
			char packedSizeStr[32];
			ByteCountToString(packedSizeStr, (u32)ARRAY_LENGTH(packedSizeStr), (u64)stats.packedBytes);
			ImGui::Text("Edits: %u, cached chunk data: %s (%u loaded from disk)", (u32)m_Streamer->Edits().Edits().size(), packedSizeStr, stats.snapshotChunkCount);

			if (ImGui::TreeNode("Chunks per LOD level"))
			{
				for (u32 level = 0; level <= m_Params.maxLodLevel && level < ARRAY_LENGTH(stats.chunksPerLevel); ++level)
				{
					ImGui::Text("Level %u (%.0fm): %u", level, m_Params.ChunkSize(level), stats.chunksPerLevel[level]);
				}
				ImGui::TreePop();
			}

			u32 vertexCount = 0;
			u32 triangleCount = 0;
			u32 rigidBodyCount = 0;
			for (auto& pair : m_Chunks)
			{
				if (pair.second.meshComponent != nullptr)
				{
					vertexCount += pair.second.meshComponent->GetVertexBufferData()->UsedVertexCount;
					triangleCount += pair.second.meshComponent->GetIndexCount() / 3;
				}
				if (pair.second.rigidBody != nullptr)
				{
					++rigidBodyCount;
				}
			}
			const std::string vertexCountStr = PrettifyLargeNumber((u64)vertexCount, 2);
			const std::string triangleCountStr = PrettifyLargeNumber((u64)triangleCount, 2);
			ImGui::Text("Verts: %s, tris: %s, rigid bodies: %u", vertexCountStr.c_str(), triangleCountStr.c_str(), rigidBodyCount);
		}

		bool bChanged = false;

		if (ImGui::Button("Regenerate"))
		{
			Regenerate();
		}

		ImGui::SameLine();

		if (ImGui::Button(s_bShowWorldMap ? "Hide world map" : "Show world map"))
		{
			s_bShowWorldMap = !s_bShowWorldMap;
		}

		ImGui::SameLine();

		if (ImGui::Checkbox("Pin center", &m_bPinCenter))
		{
			if (m_bPinCenter)
			{
				m_PinnedPos = g_CameraManager->CurrentCamera()->position;
			}
		}

		if (m_bPinCenter)
		{
			ImGui::SameLine();
			ImGui::Text("(%.2f, %.2f, %.2f)", m_PinnedPos.x, m_PinnedPos.y, m_PinnedPos.z);
		}

		bChanged = ImGui::InputInt("Seed", &m_Params.seed) || bChanged;
		ImGui::SameLine();
		if (ImGui::Button("Randomize"))
		{
			m_Params.seed = (i32)((u64)Time::CurrentMilliseconds() & 0x7FFFFFFF);
			bChanged = true;
		}

		ImGui::Separator();

		ImGui::Checkbox("Edit with mouse", &m_bEditorBrush);
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("LMB: dig, Shift + LMB: fill with the selected material");
		}
		if (m_bEditorBrush)
		{
			ImGui::SliderFloat("Brush radius", &m_BrushRadius, 0.5f, 20.0f);
			const i32 materialCount = (i32)ARRAY_LENGTH(terrain::FILL_MATERIALS);
			m_BrushMaterialIndex = glm::clamp(m_BrushMaterialIndex, 0, materialCount - 1);
			if (ImGui::BeginCombo("Fill material", terrain::MaterialName(terrain::FILL_MATERIALS[m_BrushMaterialIndex])))
			{
				for (i32 i = 0; i < materialCount; ++i)
				{
					if (ImGui::Selectable(terrain::MaterialName(terrain::FILL_MATERIALS[i]), i == m_BrushMaterialIndex))
					{
						m_BrushMaterialIndex = i;
					}
				}
				ImGui::EndCombo();
			}
		}

		// (Items, vehicles & boats are given to the player from the debug window's Player tab)
		if (g_SceneManager->CurrentScene()->GetPlayer(0) == nullptr && ImGui::Button("Spawn player at camera"))
		{
			BaseCamera* camera = g_CameraManager->CurrentCamera();
			g_SceneManager->CurrentScene()->SpawnPlayer(camera->position, camera->forward);
		}

		if (m_Streamer != nullptr && !m_Streamer->Edits().Edits().empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColour);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColour);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColour);
			if (ImGui::Button("Clear all edits"))
			{
				m_Streamer->SetEdits({});
				m_bEditsDirty = true;
				SaveEdits();
			}
			ImGui::PopStyleColor(3);
		}

		ImGui::Checkbox("Disk cache", &m_bEnableDiskCache);
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Saves edits, and the chunks around the camera on exit (so startup is instant)");
		}
		ImGui::SameLine();
		if (ImGui::Checkbox("Auto fog", &m_bAutoFog))
		{
			UpdateSceneFog();
		}

		ImGui::Separator();

		{
			i32 quality = g_Renderer->GetTerrainQuality();
			if (ImGui::Combo("Quality", &quality, s_TerrainQualityNames, (i32)ARRAY_LENGTH(s_TerrainQualityNames)))
			{
				// Stored in the renderer settings, since it's a user preference rather than part of the scene
				g_Renderer->SetTerrainQuality(quality);
				g_Renderer->SaveSettingsToDisk(false);
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Higher quality uses detailed chunks further from the camera (more chunks)");
			}
		}
		ImGui::SliderFloat("Shadow distance", &m_ShadowDistance, 50.0f, 10000.0f, "%.0f", 2.0f);
		bChanged = ImGui::DragFloat("View distance", &m_Params.viewDistance, 50.0f, 100.0f, 200000.0f) || bChanged;
		bChanged = ImGui::SliderFloat("LOD distance factor", &m_Params.lodDistanceFactor, 1.8f, 6.0f) || bChanged;
		bChanged = ImGuiExt::SliderUInt("Max LOD level", &m_Params.maxLodLevel, 0, 14) || bChanged;
		bChanged = ImGuiExt::SliderUInt("Cells per chunk axis", &m_Params.cellsPerChunkAxis, 4, 64) || bChanged;
		bChanged = ImGui::DragFloat("Voxel size", &m_Params.voxelSize, 0.05f, 0.25f, 64.0f) || bChanged;
		ImGui::Text("Chunk size: %.1fm (largest: %.1fm)", m_Params.ChunkSize(0), m_Params.ChunkSize(m_Params.maxLodLevel));
		ImGui::SliderFloat("Rigid body load radius", &m_RigidBodyLoadRadius, 0.0f, 2000.0f);

		if (ImGui::TreeNode("World shape"))
		{
			bChanged = ImGui::DragFloat("Biome cell size", &m_Params.biomeCellSize, 10.0f, 100.0f, 100000.0f) || bChanged;
			bChanged = ImGui::DragFloat("Biome blend distance", &m_Params.biomeBlendDistance, 10.0f, 1.0f, 50000.0f) || bChanged;
			bChanged = ImGui::DragFloat("Continent wavelength", &m_Params.continentWavelength, 10.0f, 100.0f, 200000.0f) || bChanged;
			bChanged = ImGui::DragFloat("Continent amplitude", &m_Params.continentAmplitude, 1.0f, 0.0f, 5000.0f) || bChanged;
			bChanged = ImGui::DragFloat("Overhang wavelength", &m_Params.overhangWavelength, 1.0f, 2.0f, 2000.0f) || bChanged;
			bChanged = ImGui::DragFloat("Arch cell size", &m_Params.archCellSize, 1.0f, 20.0f, 5000.0f) || bChanged;
			bChanged = ImGui::DragFloat("Arch min radius", &m_Params.archMinRadius, 0.5f, 1.0f, 200.0f) || bChanged;
			bChanged = ImGui::DragFloat("Arch max radius", &m_Params.archMaxRadius, 0.5f, 1.0f, 200.0f) || bChanged;
			bChanged = ImGui::DragFloat("Cave wavelength", &m_Params.caveWavelength, 1.0f, 2.0f, 2000.0f) || bChanged;
			bChanged = ImGui::SliderFloat("Tunnel radius", &m_Params.tunnelRadius, 0.0f, 20.0f) || bChanged;
			bChanged = ImGui::DragFloat("Cave max depth", &m_Params.caveMaxDepth, 1.0f, 0.0f, 2000.0f) || bChanged;
			bChanged = ImGui::DragFloat("Cavern wavelength", &m_Params.cavernWavelength, 1.0f, 10.0f, 5000.0f) || bChanged;
			bChanged = ImGui::SliderFloat("Cavern chance", &m_Params.cavernChance, 0.0f, 1.0f) || bChanged;
			bChanged = ImGui::DragFloat("Lake cell size", &m_Params.lakeCellSize, 10.0f, 100.0f, 50000.0f) || bChanged;
			bChanged = ImGui::SliderFloat("Lake chance", &m_Params.lakeChance, 0.0f, 1.0f) || bChanged;
			bChanged = ImGui::DragFloat("Lake min radius", &m_Params.lakeMinRadius, 1.0f, 10.0f, 5000.0f) || bChanged;
			bChanged = ImGui::DragFloat("Lake max radius", &m_Params.lakeMaxRadius, 1.0f, 10.0f, 5000.0f) || bChanged;
			bChanged = ImGui::DragFloat("Lake max relief", &m_Params.lakeMaxRelief, 1.0f, 0.0f, 1000.0f) || bChanged;
			bChanged = ImGui::DragFloat("Massif cell size", &m_Params.massifCellSize, 100.0f, 1000.0f, 500000.0f) || bChanged;
			bChanged = ImGui::SliderFloat("Massif chance", &m_Params.massifChance, 0.0f, 1.0f) || bChanged;
			bChanged = ImGui::DragFloat("Massif min height", &m_Params.massifMinHeight, 10.0f, 0.0f, 10000.0f) || bChanged;
			bChanged = ImGui::DragFloat("Massif max height", &m_Params.massifMaxHeight, 10.0f, 0.0f, 10000.0f) || bChanged;
			bChanged = ImGui::DragFloat("Climate wavelength", &m_Params.climateWavelength, 100.0f, 1000.0f, 1000000.0f) || bChanged;
			bChanged = ImGui::DragFloat("Sea level", &m_Params.seaLevel, 1.0f, -1000.0f, 1000.0f) || bChanged;
			bChanged = ImGui::DragFloat("Ocean wavelength", &m_Params.oceanWavelength, 100.0f, 1000.0f, 1000000.0f) || bChanged;
			bChanged = ImGui::SliderFloat("Ocean coverage", &m_Params.oceanCoverage, 0.0f, 1.0f) || bChanged;
			bChanged = ImGui::DragFloat("Ocean depth", &m_Params.oceanDepth, 1.0f, 0.0f, 5000.0f) || bChanged;
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Biomes"))
		{
			for (i32 biomeIndex = 0; biomeIndex < (i32)m_Params.biomes.size(); ++biomeIndex)
			{
				terrain::BiomeParams& biome = m_Params.biomes[biomeIndex];

				ImGui::PushID(biomeIndex);
				if (ImGui::TreeNode("biome", "%s", biome.name.c_str()))
				{
					bChanged = DrawBiomeImGui(biome) || bChanged;

					ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColour);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColour);
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColour);
					const bool bRemove = m_Params.biomes.size() > 1 && ImGui::Button("Remove biome");
					ImGui::PopStyleColor(3);

					ImGui::TreePop();

					if (bRemove)
					{
						m_Params.biomes.erase(m_Params.biomes.begin() + biomeIndex);
						bChanged = true;
						ImGui::PopID();
						break;
					}
				}
				ImGui::PopID();
			}

			if (m_Params.biomes.size() < terrain::MAX_BIOME_COUNT && ImGui::Button("Add biome"))
			{
				terrain::BiomeParams biome = {};
				biome.name = "Biome " + std::to_string(m_Params.biomes.size());
				m_Params.biomes.push_back(biome);
				bChanged = true;
			}

			ImGui::SameLine();

			if (ImGui::Button("Reset to defaults"))
			{
				m_Params.biomes = terrain::WorldParams::Default().biomes;
				bChanged = true;
			}

			ImGui::TreePop();
		}

		// Regenerating is expensive, so wait until dragging finishes
		if (bChanged)
		{
			m_bEditPending = true;
		}
		if (m_bEditPending && !ImGui::IsAnyItemActive())
		{
			m_bEditPending = false;
			Regenerate();
		}
	}

	void TerrainGenerator::DrawActiveImGuiWindows()
	{
		if (s_Active != nullptr && s_bShowWorldMap)
		{
			s_Active->DrawWorldMapWindow();
		}
	}

	std::future<TerrainGenerator::WorldMap> TerrainGenerator::GenerateMap(std::shared_ptr<const terrain::TerrainDensity> density, const glm::vec3& center, real extent, u32 resolution, bool bParallel /* = false */)
	{
		return std::async(std::launch::async, [density, center, extent, resolution, bParallel]()
		{
			WorldMap map = {};
			map.center = center;
			map.extent = extent;
			map.resolution = resolution;
			map.colours.resize(resolution * resolution);

			// Rows run from +z (north, top) to -z
			std::vector<terrain::ColumnSample> columns(resolution * resolution);
			const real cellSize = (2.0f * extent) / (real)resolution;
			auto SampleRow = [&](u32 row)
			{
				for (u32 x = 0; x < resolution; ++x)
				{
					const real px = center.x - extent + ((real)x + 0.5f) * cellSize;
					const real pz = center.z + extent - ((real)row + 0.5f) * cellSize;
					columns[row * resolution + x] = density->SampleColumn(px, pz);
				}
			};
			if (bParallel)
			{
				// Column sampling dominates (~2.5us each, so a 384^2 map takes ~350ms on one thread)
				JobSystem::Context context;
				JobSystem::Dispatch(context, resolution, 4, [&](JobSystem::JobArgs args) { SampleRow(args.jobIndex); });
				JobSystem::Wait(context);
			}
			else
			{
				for (u32 row = 0; row < resolution; ++row)
				{
					SampleRow(row);
				}
			}

			for (u32 row = 0; row < resolution; ++row)
			{
				for (u32 x = 0; x < resolution; ++x)
				{
					const terrain::ColumnSample& column = columns[row * resolution + x];
					glm::vec3 colour;
					if (column.lakeID != 0 && column.lakeT < 1.0f && column.height < column.waterLevel)
					{
						const real depth = column.waterLevel - column.height;
						if (column.lakeID == terrain::OCEAN_ID)
						{
							colour = glm::mix(glm::vec3(0.22f, 0.46f, 0.58f), glm::vec3(0.05f, 0.14f, 0.32f), glm::smoothstep(0.0f, 150.0f, depth));
						}
						else
						{
							colour = glm::mix(glm::vec3(0.20f, 0.42f, 0.58f), glm::vec3(0.10f, 0.28f, 0.50f), glm::smoothstep(0.0f, 15.0f, depth));
						}
					}
					else
					{
						colour = glm::mix(column.groundColour, column.cliffColour, 0.3f);
						if (column.lakeID == terrain::OCEAN_ID && column.height - column.waterLevel < 5.0f)
						{
							colour = glm::vec3(0.78f, 0.70f, 0.52f);
						}
						if (column.height > column.peakHeight)
						{
							colour = column.peakColour;
						}

						// Hill shading, lit from the north west
						const real hx = columns[row * resolution + glm::min(x + 1, resolution - 1)].height - columns[row * resolution + (x > 0 ? x - 1 : 0)].height;
						const real hz = columns[(row > 0 ? row - 1 : 0) * resolution + x].height - columns[glm::min(row + 1, resolution - 1) * resolution + x].height;
						const glm::vec3 normal = glm::normalize(glm::vec3(-hx, 2.0f * cellSize, -hz));
						const real shade = glm::clamp(glm::dot(normal, glm::normalize(glm::vec3(-1.0f, 1.5f, 1.0f))) * 1.1f, 0.25f, 1.2f);
						colour *= shade;
					}
					colour = glm::clamp(colour, VEC3_ZERO, VEC3_ONE);
					map.colours[row * resolution + x] = IM_COL32((u32)(colour.r * 255.0f), (u32)(colour.g * 255.0f), (u32)(colour.b * 255.0f), 255);
				}
			}

			map.bValid = true;
			return map;
		});
	}

	std::string TerrainGenerator::DescribeLocation(const glm::vec3& pos) const
	{
		if (m_Streamer == nullptr)
		{
			return "";
		}

		std::shared_ptr<const terrain::TerrainDensity> density = m_Streamer->Density();
		const terrain::ColumnSample column = density->SampleColumn(pos.x, pos.z);
		const std::vector<terrain::BiomeParams>& biomes = density->Params().biomes;
		std::string result = column.primaryBiome >= 0 && column.primaryBiome < (i32)biomes.size() ? biomes[column.primaryBiome].name : "?";

		const real temperature = column.temperature;
		const char* climate = temperature < -0.6f ? "polar" : temperature < -0.2f ? "cold" : temperature < 0.3f ? "temperate" : temperature < 0.65f ? "warm" : "hot";
		result += std::string(" (") + climate + ")";

		const bool bUnderWater = column.lakeID != 0 && column.lakeT < 1.0f && column.height < column.waterLevel;
		if (column.lakeID == terrain::OCEAN_ID && column.oceanWeight > 0.15f)
		{
			result += bUnderWater ? ", ocean" : ", coast";
		}
		else if (bUnderWater)
		{
			result += ", lake";
		}
		if (column.massifWeight > 0.1f)
		{
			result += ", mountain massif";
		}
		return result;
	}

	void TerrainGenerator::DestroyMinimapTextures(bool bAll)
	{
		for (auto iter = m_RetiredMinimapTextures.begin(); iter != m_RetiredMinimapTextures.end();)
		{
			// Wait a few frames before destroying, since in flight frames may still be using the texture
			if (bAll || ++iter->second > 4)
			{
				g_ResourceManager->RemoveLoadedTexture(iter->first, true);
				iter = m_RetiredMinimapTextures.erase(iter);
			}
			else
			{
				++iter;
			}
		}

		if (bAll && m_MinimapTextureID != InvalidTextureID)
		{
			g_ResourceManager->RemoveLoadedTexture(m_MinimapTextureID, true);
			m_MinimapTextureID = InvalidTextureID;
		}
		if (bAll && m_LargeMapTextureID != InvalidTextureID)
		{
			g_ResourceManager->RemoveLoadedTexture(m_LargeMapTextureID, true);
			m_LargeMapTextureID = InvalidTextureID;
		}
		if (bAll && m_MapArrowTextureID != InvalidTextureID)
		{
			g_ResourceManager->RemoveLoadedTexture(m_MapArrowTextureID, true);
			m_MapArrowTextureID = InvalidTextureID;
		}
	}

	void TerrainGenerator::CreateMapArrowTexture()
	{
		// A navigation arrow (a dart with a notched tail), pointing up. Coordinates in [-1, 1], y up, matching the
		// sprite's orientation (sprite textures are displayed with their first row at the bottom)
		const glm::vec2 points[] = { glm::vec2(0.0f, 0.92f), glm::vec2(0.68f, -0.8f), glm::vec2(0.0f, -0.4f), glm::vec2(-0.68f, -0.8f) };
		const u32 pointCount = ARRAY_LENGTH(points);
		const u32 resolution = 64;
		const real outlineWidth = 0.17f;
		const real pixelSize = 2.0f / (real)resolution;
		auto ToBytes = [](const glm::vec4& colour)
		{
			const glm::vec4 c = glm::clamp(colour, 0.0f, 1.0f) * 255.0f + 0.5f;
			return (u32)c.r | ((u32)c.g << 8) | ((u32)c.b << 16) | ((u32)c.a << 24);
		};

		std::vector<u32> pixels(resolution * resolution);
		for (u32 y = 0; y < resolution; ++y)
		{
			for (u32 x = 0; x < resolution; ++x)
			{
				const glm::vec2 p((x + 0.5f) * pixelSize - 1.0f, (y + 0.5f) * pixelSize - 1.0f);
				// Signed distance to the polygon (negative inside)
				real dist = FLT_MAX;
				bool bInside = false;
				for (u32 i = 0, j = pointCount - 1; i < pointCount; j = i++)
				{
					const glm::vec2 a = points[j];
					const glm::vec2 b = points[i];
					const glm::vec2 ab = b - a;
					const real t = glm::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
					dist = glm::min(dist, glm::distance(p, a + ab * t));
					if ((a.y > p.y) != (b.y > p.y) && p.x < a.x + (p.y - a.y) / (b.y - a.y) * (b.x - a.x))
					{
						bInside = !bInside;
					}
				}
				const real signedDist = (bInside ? -dist : dist) / pixelSize;
				const real coverage = glm::clamp(0.5f - signedDist, 0.0f, 1.0f);
				const real fill = glm::clamp(0.5f - (signedDist + outlineWidth / pixelSize), 0.0f, 1.0f);
				glm::vec4 colour = glm::mix(UITheme::INK, UITheme::TEXT_PRIMARY, fill);
				colour.a = coverage;
				pixels[y * resolution + x] = ToBytes(colour);
			}
		}

		m_MapArrowTextureID = g_Renderer->InitializeTextureFromMemory(pixels.data(), (u32)(pixels.size() * sizeof(u32)), TextureFormat::R8G8B8A8_UNORM,
			"Map player arrow", resolution, resolution, 4, g_Renderer->GetSamplerLinearClampToEdge(), VK_FILTER_LINEAR);
	}

	TerrainGenerator::MapWaypoint* TerrainGenerator::GetMapWaypoints()
	{
		static MapWaypoint waypoints[MAX_MAP_WAYPOINTS];
		return waypoints;
	}

	glm::vec4 TerrainGenerator::WaypointColour(u32 index)
	{
		// Nine colours that are easy to tell apart (& from the gold treasure markers)
		static const u32 colours[MAX_MAP_WAYPOINTS] = { 0xE0614F, 0x6CB6D9, 0x86C07A, 0xD96CC8, 0xFF8A3D, 0x9A7BE8, 0x4FD6C9, 0xFFA8C0, 0xC8E04F };
		return UITheme::Hex(colours[index % MAX_MAP_WAYPOINTS]);
	}

	void TerrainGenerator::DrawMapWaypoint(const glm::vec2& centerPx, u32 index, real sizePx, u32 zOrder)
	{
		const glm::vec2i windowSize = g_Window->GetSize();
		const glm::vec2 screenSize((real)glm::max(windowSize.x, 1), (real)glm::max(windowSize.y, 1));
		const real aspectRatio = screenSize.x / screenSize.y;
		// (A square turned 45 degrees: dark outline, then the colour)
		for (u32 layer = 0; layer < 2; ++layer)
		{
			SpriteQuadDrawInfo drawInfo = {};
			drawInfo.anchor = AnchorPoint::CENTER;
			drawInfo.bScreenSpace = true;
			drawInfo.bReadDepth = false;
			drawInfo.bEnableAlbedoSampler = false;
			drawInfo.zOrder = zOrder + layer;
			drawInfo.colour = layer == 0 ? UITheme::INK : WaypointColour(index);
			drawInfo.pos = glm::vec3((centerPx.x / screenSize.x * 2.0f - 1.0f) * aspectRatio, 1.0f - centerPx.y / screenSize.y * 2.0f, 0.0f);
			drawInfo.rotation = glm::angleAxis(PI * 0.25f, VEC3_FORWARD);
			const real size = (layer == 0 ? sizePx : sizePx * 0.7f) / screenSize.y;
			drawInfo.scale = glm::vec3(size, size, 1.0f);
			g_Renderer->EnqueueSprite(drawInfo);
		}
	}

	void TerrainGenerator::DrawMapPlayerArrow(const glm::vec2& centerPx, const glm::vec2& heading, real sizePx, u32 zOrder)
	{
		if (m_MapArrowTextureID == InvalidTextureID)
		{
			CreateMapArrowTexture();
		}

		const glm::vec2i windowSize = g_Window->GetSize();
		const glm::vec2 screenSize((real)glm::max(windowSize.x, 1), (real)glm::max(windowSize.y, 1));
		const real aspectRatio = screenSize.x / screenSize.y;
		// Rotates the arrow's up (0, 1) to the heading (y up)
		const glm::vec2 dir = glm::length(heading) > 1e-4f ? glm::normalize(heading) : glm::vec2(0.0f, -1.0f);
		const real angle = std::atan2(-dir.x, -dir.y);

		SpriteQuadDrawInfo drawInfo = {};
		drawInfo.textureID = m_MapArrowTextureID;
		drawInfo.anchor = AnchorPoint::CENTER;
		drawInfo.bScreenSpace = true;
		drawInfo.bReadDepth = false;
		drawInfo.zOrder = zOrder;
		drawInfo.pos = glm::vec3((centerPx.x / screenSize.x * 2.0f - 1.0f) * aspectRatio, 1.0f - centerPx.y / screenSize.y * 2.0f, 0.0f);
		drawInfo.rotation = glm::angleAxis(angle, VEC3_FORWARD);
		drawInfo.scale = glm::vec3(glm::vec2(sizePx / screenSize.y), 1.0f);
		g_Renderer->EnqueueSprite(drawInfo);
	}

	void TerrainGenerator::UpdateMinimap()
	{
		DestroyMinimapTextures(false);

		if (m_MinimapFuture.valid() && m_MinimapFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			m_Minimap = m_MinimapFuture.get();

			// Sprite textures are displayed with their first row at the bottom, so flip (rows are stored north first)
			const u32 resolution = m_Minimap.resolution;
			std::vector<u32> pixels(resolution * resolution);
			for (u32 row = 0; row < resolution; ++row)
			{
				memcpy(&pixels[(resolution - 1 - row) * resolution], &m_Minimap.colours[row * resolution], resolution * sizeof(u32));
			}

			const TextureID textureID = g_Renderer->InitializeTextureFromMemory(pixels.data(), (u32)(pixels.size() * sizeof(u32)), TextureFormat::R8G8B8A8_UNORM,
				"Terrain minimap", resolution, resolution, 4, g_Renderer->GetSamplerLinearClampToEdge(), VK_FILTER_LINEAR);
			if (m_MinimapTextureID != InvalidTextureID)
			{
				m_RetiredMinimapTextures.emplace_back(m_MinimapTextureID, 0u);
			}
			m_MinimapTextureID = textureID;
		}

		if (!s_bShowMinimap || m_Streamer == nullptr || s_Active != this)
		{
			return;
		}

		// Only shown while playing (rather than flying the debug camera around)
		BaseCamera* camera = g_CameraManager->CurrentCamera();
		if (camera == nullptr || !camera->bPossessPlayer)
		{
			return;
		}

		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		const glm::vec3 pos = player != nullptr ? player->GetTransform()->GetWorldPosition() : camera->position;

		// Regenerated (in the background, ~60ms) whenever the player moves a little, so they stay near the center
		const bool bStale = !m_Minimap.bValid || m_Minimap.extent != m_MinimapExtent ||
			glm::distance(glm::vec2(pos.x, pos.z), glm::vec2(m_Minimap.center.x, m_Minimap.center.z)) > m_MinimapExtent * 0.02f;
		if (bStale && !m_MinimapFuture.valid())
		{
			m_MinimapFuture = GenerateMap(m_Streamer->Density(), pos, m_MinimapExtent, MINIMAP_RESOLUTION);
		}

		if (m_MinimapTextureID == InvalidTextureID || !m_Minimap.bValid)
		{
			return;
		}

		// A plate in the top right corner (see docs/visual_design.md), in pixels from the top left. The plate is UI,
		// the map & markers are sprites drawn after the UI.
		const glm::vec2i windowSize = g_Window->GetSize();
		const glm::vec2 screenSize((real)glm::max(windowSize.x, 1), (real)glm::max(windowSize.y, 1));
		const real scale = screenSize.y / 1080.0f;
		const real mapSizePx = 280.0f * scale;
		const real padding = 6.0f * scale;
		const glm::vec2 mapCenterPx(screenSize.x - (UITheme::HUD_MARGIN + padding) * scale - mapSizePx * 0.5f, (UITheme::HUD_MARGIN + padding) * scale + mapSizePx * 0.5f);
		const UIRect plate(mapCenterPx.x - mapSizePx * 0.5f - padding, mapCenterPx.y - mapSizePx * 0.5f - padding, mapSizePx + padding * 2.0f, mapSizePx + padding * 2.0f);
		GameUI::DrawRoundedRect(plate, UITheme::SURFACE_TOP, UITheme::SURFACE, UITheme::RADIUS_L * scale);

		const real aspectRatio = screenSize.x / screenSize.y;
		auto PxToSprite = [&](const glm::vec2& px) { return glm::vec3((px.x / screenSize.x * 2.0f - 1.0f) * aspectRatio, 1.0f - px.y / screenSize.y * 2.0f, 0.0f); };

		SpriteQuadDrawInfo mapSprite = {};
		mapSprite.textureID = m_MinimapTextureID;
		mapSprite.anchor = AnchorPoint::CENTER;
		mapSprite.bScreenSpace = true;
		mapSprite.bReadDepth = false;
		mapSprite.zOrder = Z_ORDER_UI + 10;
		mapSprite.pos = PxToSprite(mapCenterPx);
		mapSprite.scale = glm::vec3(glm::vec2(mapSizePx / screenSize.y), 1.0f);
		g_Renderer->EnqueueSprite(mapSprite);

		// The player, as an arrow pointing the way they're facing
		glm::vec2 offset = glm::vec2(pos.x - m_Minimap.center.x, -(pos.z - m_Minimap.center.z)) / m_Minimap.extent;
		offset = glm::clamp(offset, glm::vec2(-0.95f), glm::vec2(0.95f));
		const glm::vec2 markerPx = mapCenterPx + offset * mapSizePx * 0.5f;
		const glm::vec3 look = player != nullptr ? player->GetLookDirection() : camera->forward;
		// Waypoints (pinned to the edge of the map when they're further away)
		const MapWaypoint* waypoints = GetMapWaypoints();
		for (u32 i = 0; i < MAX_MAP_WAYPOINTS; ++i)
		{
			if (!waypoints[i].bUsed)
			{
				continue;
			}
			glm::vec2 waypointOffset = glm::vec2(waypoints[i].pos.x - m_Minimap.center.x, -(waypoints[i].pos.y - m_Minimap.center.z)) / m_Minimap.extent;
			const real maxComponent = glm::max(glm::abs(waypointOffset.x), glm::abs(waypointOffset.y));
			if (maxComponent > 0.92f)
			{
				waypointOffset *= 0.92f / maxComponent;
			}
			DrawMapWaypoint(mapCenterPx + waypointOffset * mapSizePx * 0.5f, i, 16.0f * scale, Z_ORDER_UI + 11);
		}

		DrawMapPlayerArrow(markerPx, glm::vec2(look.x, -look.z), 26.0f * scale, Z_ORDER_UI + 10);
	}

	// Draws a rectangle outline with dashes moving around it (used to show chunks being loaded)
	static void DrawDashedRect(ImDrawList* drawList, const ImVec2& a, const ImVec2& b, ImU32 colour, real dashLength, real offset)
	{
		const ImVec2 corners[5] = { a, ImVec2(b.x, a.y), b, ImVec2(a.x, b.y), a };
		const real period = dashLength * 2.0f;
		real perimeterPos = offset;
		for (u32 edge = 0; edge < 4; ++edge)
		{
			const ImVec2 start = corners[edge];
			const ImVec2 end = corners[edge + 1];
			const real length = glm::length(glm::vec2(end.x - start.x, end.y - start.y));
			if (length <= 0.0f)
			{
				continue;
			}

			real t = 0.0f;
			while (t < length)
			{
				const real phase = glm::mod(perimeterPos + t, period);
				if (phase < dashLength)
				{
					const real segmentEnd = glm::min(length, t + (dashLength - phase));
					const ImVec2 p0(start.x + (end.x - start.x) * (t / length), start.y + (end.y - start.y) * (t / length));
					const ImVec2 p1(start.x + (end.x - start.x) * (segmentEnd / length), start.y + (end.y - start.y) * (segmentEnd / length));
					drawList->AddLine(p0, p1, colour, 1.5f);
					t = segmentEnd;
				}
				else
				{
					t += period - phase;
				}
			}
			perimeterPos += length;
		}
	}

	void TerrainGenerator::DrawWorldMapWindow()
	{
		ImGui::SetNextWindowSize(ImVec2(540.0f, 760.0f), ImGuiCond_FirstUseEver);
		// Scrolling zooms the map rather than scrolling the window
		if (!ImGui::Begin("Terrain map", &s_bShowWorldMap, ImGuiWindowFlags_NoScrollWithMouse))
		{
			ImGui::End();
			return;
		}

		BaseCamera* camera = g_CameraManager->CurrentCamera();
		const glm::vec3 cameraPos = camera->position;

		if (m_WorldMapFuture.valid() && m_WorldMapFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			m_WorldMap = m_WorldMapFuture.get();
		}

		const real desiredExtent = m_Params.viewDistance / glm::max(m_WorldMapZoom, 0.01f);
		const bool bMapStale = !m_WorldMap.bValid ||
			glm::distance(glm::vec2(m_WorldMap.center.x, m_WorldMap.center.z), glm::vec2(cameraPos.x, cameraPos.z)) > m_WorldMap.extent * 0.1f ||
			glm::abs(m_WorldMap.extent - desiredExtent) > desiredExtent * 0.01f;
		if (bMapStale && m_Streamer != nullptr && !m_WorldMapFuture.valid())
		{
			m_WorldMapFuture = GenerateMap(m_Streamer->Density(), cameraPos, desiredExtent, 128);
		}

		// Seed
		{
			i32 seed = GetEffectiveParams().seed;
			ImGui::PushItemWidth(140.0f);
			if (ImGui::InputInt("Seed", &seed, 1, 100, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				m_Params.seed = seed;
				Regenerate();
			}
			ImGui::PopItemWidth();
			ImGui::SameLine();
			if (ImGui::Button("Previous"))
			{
				m_Params.seed = seed - 1;
				Regenerate();
			}
			ImGui::SameLine();
			if (ImGui::Button("Next"))
			{
				m_Params.seed = seed + 1;
				Regenerate();
			}
			ImGui::SameLine();
			if (ImGui::Button("Random"))
			{
				m_Params.seed = (i32)((u64)Time::CurrentMilliseconds() & 0x7FFFFFFF);
				Regenerate();
			}
		}

		ImGui::PushItemWidth(160.0f);
		ImGui::SliderFloat("Zoom", &m_WorldMapZoom, 0.05f, 64.0f, "%.2f", 3.0f);
		ImGui::PopItemWidth();
		ImGui::SameLine();
		ImGui::Checkbox("Chunks", &m_bWorldMapShowChunks);
		ImGui::SameLine();
		ImGui::Checkbox("Minimap", &s_bShowMinimap);
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Show a minimap in the corner of the screen while playing");
		}

		static const ImU32 levelColours[] =
		{
			IM_COL32(255, 60, 60, 255), IM_COL32(255, 150, 40, 255), IM_COL32(250, 230, 50, 255), IM_COL32(120, 230, 60, 255),
			IM_COL32(40, 220, 200, 255), IM_COL32(60, 140, 255, 255), IM_COL32(150, 90, 255, 255), IM_COL32(240, 80, 220, 255),
			IM_COL32(250, 250, 250, 255), IM_COL32(150, 150, 150, 255), IM_COL32(90, 90, 90, 255), IM_COL32(40, 40, 40, 255),
		};
		const u32 levelColourCount = (u32)ARRAY_LENGTH(levelColours);

		std::vector<terrain::TerrainStreamer::DisplayedChunkInfo> chunks;
		u32 chunksPerLevel[32] = {};
		u32 pendingPerLevel[32] = {};
		if (m_Streamer != nullptr)
		{
			m_Streamer->GetDisplayedChunks(chunks);
			for (const terrain::TerrainStreamer::DisplayedChunkInfo& chunk : chunks)
			{
				if (chunk.key.level >= 0 && chunk.key.level < 32)
				{
					if (chunk.bPending)
					{
						++pendingPerLevel[chunk.key.level];
					}
					else if (!chunk.bEmpty)
					{
						++chunksPerLevel[chunk.key.level];
					}
				}
			}
		}

		// Legend
		for (u32 level = 0; level <= m_Params.maxLodLevel && level < 32; ++level)
		{
			if (level % 6 != 0)
			{
				ImGui::SameLine();
			}
			const ImVec4 colour = ImGui::ColorConvertU32ToFloat4(levelColours[level % levelColourCount]);
			ImGui::ColorButton("##level", colour, ImGuiColorEditFlags_NoTooltip, ImVec2(10.0f, 10.0f));
			ImGui::SameLine();
			if (pendingPerLevel[level] > 0)
			{
				ImGui::Text("L%u: %u (+%u)", level, chunksPerLevel[level], pendingPerLevel[level]);
			}
			else
			{
				ImGui::Text("L%u: %u", level, chunksPerLevel[level]);
			}
		}

		const real footerHeight = ImGui::GetTextLineHeightWithSpacing() * 5.0f;
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const real canvasSize = glm::max(glm::min(available.x, available.y - footerHeight), 64.0f);
		const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("map", ImVec2(canvasSize, canvasSize));
		const bool bHovered = ImGui::IsItemHovered();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize, canvasPos.y + canvasSize), true);
		drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize, canvasPos.y + canvasSize), IM_COL32(20, 20, 24, 255));

		// The view is centered on the camera, with +x to the right & +z up
		const real viewExtent = desiredExtent;
		auto WorldToCanvas = [&](real x, real z)
		{
			return ImVec2(canvasPos.x + ((x - cameraPos.x) / (2.0f * viewExtent) + 0.5f) * canvasSize,
				canvasPos.y + (0.5f - (z - cameraPos.z) / (2.0f * viewExtent)) * canvasSize);
		};

		if (m_WorldMap.bValid)
		{
			const real cellSize = (2.0f * m_WorldMap.extent) / (real)m_WorldMap.resolution;
			for (u32 row = 0; row < m_WorldMap.resolution; ++row)
			{
				for (u32 x = 0; x < m_WorldMap.resolution; ++x)
				{
					const real px = m_WorldMap.center.x - m_WorldMap.extent + (real)x * cellSize;
					const real pzTop = m_WorldMap.center.z + m_WorldMap.extent - (real)row * cellSize;
					const ImVec2 a = WorldToCanvas(px, pzTop);
					const ImVec2 b = WorldToCanvas(px + cellSize, pzTop - cellSize);
					drawList->AddRectFilled(a, ImVec2(b.x + 0.5f, b.y + 0.5f), m_WorldMap.colours[row * m_WorldMap.resolution + x]);
				}
			}
		}

		if (m_bWorldMapShowChunks)
		{
			// Coarse chunks first so finer ones are drawn on top
			std::sort(chunks.begin(), chunks.end(), [](const terrain::TerrainStreamer::DisplayedChunkInfo& a, const terrain::TerrainStreamer::DisplayedChunkInfo& b)
			{
				return a.key.level > b.key.level;
			});
			const real dashOffset = (real)std::fmod(g_SecElapsedSinceProgramStart * 12.0, 1000.0);
			for (const terrain::TerrainStreamer::DisplayedChunkInfo& chunk : chunks)
			{
				if (chunk.bEmpty)
				{
					continue;
				}
				const real size = m_Params.ChunkSize((u32)chunk.key.level);
				const real x0 = (real)chunk.key.index.x * size;
				const real z0 = (real)chunk.key.index.z * size;
				const ImVec2 a = WorldToCanvas(x0, z0 + size);
				const ImVec2 b = WorldToCanvas(x0 + size, z0);
				if (b.x - a.x < 1.5f)
				{
					continue;
				}
				const ImU32 colour = levelColours[chunk.key.level % levelColourCount];
				if (chunk.bPending)
				{
					// Being generated: moving dashed outline
					DrawDashedRect(drawList, a, b, (colour & 0x00FFFFFF) | 0xE0000000, 4.0f, dashOffset);
				}
				else
				{
					drawList->AddRectFilled(a, b, (colour & 0x00FFFFFF) | 0x28000000);
					drawList->AddRect(a, b, (colour & 0x00FFFFFF) | 0xC0000000);
				}
			}
		}

		// View distance & camera
		const ImVec2 cameraCanvasPos = WorldToCanvas(cameraPos.x, cameraPos.z);
		drawList->AddCircle(cameraCanvasPos, m_Params.viewDistance / (2.0f * viewExtent) * canvasSize, IM_COL32(255, 255, 255, 120), 64);
		const glm::vec2 forward = glm::length(glm::vec2(camera->forward.x, camera->forward.z)) > 1e-4f ? glm::normalize(glm::vec2(camera->forward.x, camera->forward.z)) : glm::vec2(0.0f, 1.0f);
		drawList->AddLine(cameraCanvasPos, ImVec2(cameraCanvasPos.x + forward.x * 14.0f, cameraCanvasPos.y - forward.y * 14.0f), IM_COL32(255, 255, 255, 255), 2.0f);
		drawList->AddCircleFilled(cameraCanvasPos, 4.0f, IM_COL32(255, 255, 255, 255));

		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		if (player != nullptr)
		{
			const glm::vec3 playerPos = player->GetTransform()->GetWorldPosition();
			drawList->AddCircleFilled(WorldToCanvas(playerPos.x, playerPos.z), 3.5f, IM_COL32(255, 90, 60, 255));
		}

		drawList->PopClipRect();

		// Hover info, teleporting & zooming
		if (bHovered && m_Streamer != nullptr)
		{
			const ImVec2 mousePos = ImGui::GetIO().MousePos;
			const real worldX = cameraPos.x + ((mousePos.x - canvasPos.x) / canvasSize - 0.5f) * 2.0f * viewExtent;
			const real worldZ = cameraPos.z + (0.5f - (mousePos.y - canvasPos.y) / canvasSize) * 2.0f * viewExtent;
			const terrain::ColumnSample column = m_Streamer->Density()->SampleColumn(worldX, worldZ);
			ImGui::SetTooltip("(%.0f, %.0f)\nHeight: %.1fm\n%s\nDouble click to teleport", worldX, worldZ, column.height,
				DescribeLocation(glm::vec3(worldX, 0.0f, worldZ)).c_str());

			if (ImGui::IsMouseDoubleClicked(0))
			{
				real height = column.height;
				if (column.lakeID != 0 && column.lakeT < 1.0f)
				{
					height = glm::max(height, column.waterLevel);
				}
				if (player != nullptr && camera->bPossessPlayer)
				{
					player->TeleportOnTerrain(glm::vec3(worldX, height + 40.0f, worldZ));
				}
				else
				{
					camera->position = glm::vec3(worldX, height + 40.0f, worldZ);
				}
			}

			const real wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0.0f)
			{
				m_WorldMapZoom = glm::clamp(m_WorldMapZoom * glm::pow(1.25f, wheel), 0.05f, 64.0f);
			}
		}

		// Stats
		if (m_Streamer != nullptr)
		{
			terrain::TerrainStreamer::Stats stats = m_Streamer->GetStats();

			u64 vertexCount = 0;
			u64 triangleCount = 0;
			for (auto& pair : m_Chunks)
			{
				if (pair.second.meshComponent != nullptr)
				{
					vertexCount += pair.second.meshComponent->GetVertexBufferData()->UsedVertexCount;
					triangleCount += pair.second.meshComponent->GetIndexCount() / 3;
				}
			}

			ImGui::Text("Chunks: %u (%u with geometry), vertices: %s, triangles: %s", stats.displayedChunkCount, stats.displayedNonEmptyChunkCount,
				PrettifyLargeNumber(vertexCount, 1).c_str(), PrettifyLargeNumber(triangleCount, 1).c_str());

			const u32 loadingCount = stats.pendingChunkCount + (u32)m_ChunkCommands.size();
			if (loadingCount > 0)
			{
				const real progress = (real)stats.displayedChunkCount / (real)glm::max(stats.displayedChunkCount + stats.pendingChunkCount, 1u);
				char label[64];
				snprintf(label, ARRAY_LENGTH(label), "Generating %u, uploading %u", stats.pendingChunkCount, (u32)m_ChunkCommands.size());
				ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), label);
			}
			else
			{
				ImGui::Text("All chunks loaded");
			}

			ImGui::Text("Here: %s", DescribeLocation(cameraPos).c_str());
			ImGui::Text("Camera: (%.0f, %.0f, %.0f), %.0f m/s", cameraPos.x, cameraPos.y, cameraPos.z, glm::length(stats.velocity));
		}

		ImGui::End();
	}

	terrain::BiomeParams TerrainGenerator::ParseBiome(const JSONObject& biomeObj)
	{
		terrain::BiomeParams biome = {};
		biomeObj.TryGetString("name", biome.name);
		biomeObj.TryGetFloat("frequency", biome.frequency);
		biomeObj.TryGetFloat("temperature", biome.temperature);
		biomeObj.TryGetFloat("base height", biome.baseHeight);
		biomeObj.TryGetFloat("height amplitude", biome.heightAmplitude);
		biomeObj.TryGetFloat("wavelength", biome.wavelength);
		biomeObj.TryGetFloat("ridgedness", biome.ridgedness);
		biomeObj.TryGetFloat("terrace strength", biome.terraceStrength);
		biomeObj.TryGetFloat("terrace height", biome.terraceHeight);
		biomeObj.TryGetFloat("roughness", biome.roughness);
		biomeObj.TryGetFloat("dune strength", biome.duneStrength);
		biomeObj.TryGetFloat("canyon strength", biome.canyonStrength);
		biomeObj.TryGetFloat("canyon depth", biome.canyonDepth);
		biomeObj.TryGetFloat("fjord strength", biome.fjordStrength);
		biomeObj.TryGetFloat("spire strength", biome.spireStrength);
		biomeObj.TryGetFloat("spire height", biome.spireHeight);
		biomeObj.TryGetFloat("coastal", biome.coastal);
		biomeObj.TryGetFloat("sandy shores", biome.sandyShores);
biomeObj.TryGetFloat("overhang amplitude", biome.overhangAmplitude);
		biomeObj.TryGetFloat("arch strength", biome.archStrength);
		biomeObj.TryGetFloat("cave strength", biome.caveStrength);
		biomeObj.TryGetFloat("lake strength", biome.lakeStrength);
		biomeObj.TryGetVec3("ground colour", biome.groundColour);
		biomeObj.TryGetFloat("grassiness", biome.grassiness);
		biomeObj.TryGetVec3("cliff colour", biome.cliffColour);
		biomeObj.TryGetVec3("peak colour", biome.peakColour);
		biomeObj.TryGetFloat("peak height", biome.peakHeight);

		// Biomes saved before vegetation types take the matching default biome's (or one suiting their climate)
		terrain::Vegetation defaultVegetation = biome.temperature < -0.7f ? terrain::Vegetation::TUNDRA :
			biome.temperature < -0.2f ? terrain::Vegetation::TAIGA :
			biome.temperature < 0.05f ? terrain::Vegetation::NORDIC :
			biome.temperature < 0.5f ? terrain::Vegetation::BROADLEAF :
			biome.grassiness > 0.12f ? terrain::Vegetation::SAVANNA : terrain::Vegetation::DESERT;
		for (const terrain::BiomeParams& defaultBiome : terrain::WorldParams::Default().biomes)
		{
			if (defaultBiome.name == biome.name)
			{
				defaultVegetation = defaultBiome.vegetation;
				biome.treeDensity = defaultBiome.treeDensity;
				biome.bushDensity = defaultBiome.bushDensity;
				break;
			}
		}
		std::string vegetationName;
		biome.vegetation = biomeObj.TryGetString("vegetation", vegetationName) ? terrain::ParseVegetation(vegetationName, defaultVegetation) : defaultVegetation;
		biomeObj.TryGetFloat("tree density", biome.treeDensity);
		biomeObj.TryGetFloat("bush density", biome.bushDensity);
		return biome;
	}

	JSONObject TerrainGenerator::SerializeBiome(const terrain::BiomeParams& biome)
	{
		JSONObject biomeObj = {};
		biomeObj.fields.emplace_back("name", JSONValue(biome.name));
		biomeObj.fields.emplace_back("frequency", JSONValue(biome.frequency, 2));
		biomeObj.fields.emplace_back("temperature", JSONValue(biome.temperature, 2));
		biomeObj.fields.emplace_back("base height", JSONValue(biome.baseHeight, 1));
		biomeObj.fields.emplace_back("height amplitude", JSONValue(biome.heightAmplitude, 1));
		biomeObj.fields.emplace_back("wavelength", JSONValue(biome.wavelength, 1));
		biomeObj.fields.emplace_back("ridgedness", JSONValue(biome.ridgedness, 2));
		biomeObj.fields.emplace_back("terrace strength", JSONValue(biome.terraceStrength, 2));
		biomeObj.fields.emplace_back("terrace height", JSONValue(biome.terraceHeight, 1));
		biomeObj.fields.emplace_back("roughness", JSONValue(biome.roughness, 2));
		biomeObj.fields.emplace_back("dune strength", JSONValue(biome.duneStrength, 2));
		biomeObj.fields.emplace_back("canyon strength", JSONValue(biome.canyonStrength, 2));
		biomeObj.fields.emplace_back("canyon depth", JSONValue(biome.canyonDepth, 1));
		biomeObj.fields.emplace_back("fjord strength", JSONValue(biome.fjordStrength, 2));
		biomeObj.fields.emplace_back("spire strength", JSONValue(biome.spireStrength, 2));
		biomeObj.fields.emplace_back("spire height", JSONValue(biome.spireHeight, 1));
		biomeObj.fields.emplace_back("coastal", JSONValue(biome.coastal, 2));
		biomeObj.fields.emplace_back("sandy shores", JSONValue(biome.sandyShores, 2));
biomeObj.fields.emplace_back("overhang amplitude", JSONValue(biome.overhangAmplitude, 1));
		biomeObj.fields.emplace_back("arch strength", JSONValue(biome.archStrength, 2));
		biomeObj.fields.emplace_back("cave strength", JSONValue(biome.caveStrength, 2));
		biomeObj.fields.emplace_back("lake strength", JSONValue(biome.lakeStrength, 2));
		biomeObj.fields.emplace_back("ground colour", JSONValue(biome.groundColour, 2));
		biomeObj.fields.emplace_back("grassiness", JSONValue(biome.grassiness, 2));
		biomeObj.fields.emplace_back("cliff colour", JSONValue(biome.cliffColour, 2));
		biomeObj.fields.emplace_back("peak colour", JSONValue(biome.peakColour, 2));
		biomeObj.fields.emplace_back("peak height", JSONValue(biome.peakHeight, 1));
		biomeObj.fields.emplace_back("vegetation", JSONValue(std::string(terrain::VegetationName(biome.vegetation))));
		biomeObj.fields.emplace_back("tree density", JSONValue(biome.treeDensity, 2));
		biomeObj.fields.emplace_back("bush density", JSONValue(biome.bushDensity, 2));
		return biomeObj;
	}

	void TerrainGenerator::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject chunkGenInfo;
		if (parentObject.TryGetObject("chunk generator info", chunkGenInfo))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(chunkGenInfo, collection->childCollection);

			// Scenes from before there was a single seed
			if (!chunkGenInfo.HasField("seed"))
			{
				chunkGenInfo.TryGetInt("manual seed", m_Params.seed);
			}

			// (Scenes using the default biomes don't save them, so they pick up changes to the defaults)
			std::vector<JSONObject> biomesArr;
			if (chunkGenInfo.TryGetObjectArray("biomes", biomesArr))
			{
				std::vector<terrain::BiomeParams> biomes;
				biomes.reserve(biomesArr.size());
				for (const JSONObject& biomeObj : biomesArr)
				{
					// Biomes from before volumetric generation (lists of noise functions) aren't compatible
					if (biomeObj.HasField("noise functions"))
					{
						continue;
					}
					biomes.emplace_back(ParseBiome(biomeObj));
				}

				if (!biomes.empty())
				{
					m_Params.biomes = biomes;
				}
			}
		}
	}

	void TerrainGenerator::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject chunkGenInfo = {};

		SerializeRegisteredProperties(chunkGenInfo, collection->childCollection, bSerializePrefabData);

		// Only custom biomes are saved, so scenes using the defaults pick up changes to them
		terrain::WorldParams defaultBiomeParams = m_Params;
		defaultBiomeParams.biomes = terrain::WorldParams::Default().biomes;
		if (defaultBiomeParams.ContentHash() != m_Params.ContentHash())
		{
			std::vector<JSONObject> biomesArr;
			biomesArr.reserve(m_Params.biomes.size());
			for (const terrain::BiomeParams& biome : m_Params.biomes)
			{
				biomesArr.emplace_back(SerializeBiome(biome));
			}
			chunkGenInfo.fields.emplace_back("biomes", JSONValue(biomesArr));
		}

		parentObject.fields.emplace_back("chunk generator info", JSONValue(chunkGenInfo));
	}

	glm::vec3 TerrainGenerator::FindSpawnPoint() const
	{
		const u64 start = Time::NowTicks();
		const terrain::TerrainDensity density(m_Params);
		const terrain::SpawnSearchResult result = terrain::FindSpawnPoint(density);
		Print("Spawn point for seed %d: (%.1f, %.1f, %.1f), %.0fm from the origin%s (%u samples, %.1f ms)\n", m_Params.seed,
			result.pos.x, result.pos.y, result.pos.z, glm::length(glm::vec2(result.pos.x, result.pos.z)),
			result.bIdeal ? "" : ", no ideal spot found", result.samples, Time::MillisecondsSince(start));
		return result.pos;
	}

	void TerrainGenerator::SetWorld(i32 seed, const std::string& saveDirectory)
	{
		// The previous world's edits are saved to its own directory before switching
		FlushEdits();
		m_SaveDirectory = saveDirectory;
		if (!m_SaveDirectory.empty())
		{
			Platform::CreateDirectoryRecursive(m_SaveDirectory);
		}
		m_Params.seed = seed;
		m_bReloadEdits = true;
		m_bSnapshotFinal = false;
		m_LargeMap.bValid = false;
		m_Minimap.bValid = false;
		Regenerate();
		// Applied now rather than on the next update, so the new world's density (& saved terrain) is used right away
		ApplyParamsIfDirty();
	}

	void TerrainGenerator::SaveSnapshot(bool bFinal /* = false */)
	{
		if (m_Streamer != nullptr && m_bEnableDiskCache && !m_bParamsDirty && !m_bSnapshotFinal)
		{
			m_Streamer->SaveSnapshot(m_LastCenter);
		}
		m_bSnapshotFinal = m_bSnapshotFinal || bFinal;
	}

	void TerrainGenerator::FlushEdits()
	{
		if (m_bEditsDirty)
		{
			SaveEdits();
		}
	}

	TextureID TerrainGenerator::CreateMapTexture(const WorldMap& map, const char* name)
	{
		// Sprite textures are displayed with their first row at the bottom, so flip (rows are stored north first)
		const u32 resolution = map.resolution;
		std::vector<u32> pixels(resolution * resolution);
		for (u32 row = 0; row < resolution; ++row)
		{
			memcpy(&pixels[(resolution - 1 - row) * resolution], &map.colours[row * resolution], resolution * sizeof(u32));
		}
		return g_Renderer->InitializeTextureFromMemory(pixels.data(), (u32)(pixels.size() * sizeof(u32)), TextureFormat::R8G8B8A8_UNORM,
			name, resolution, resolution, 4, g_Renderer->GetSamplerLinearClampToEdge(), VK_FILTER_LINEAR);
	}

	void TerrainGenerator::RequestLargeMap(const glm::vec3& center, real extent, u32 resolution)
	{
		if (m_Streamer == nullptr || m_LargeMapFuture.valid())
		{
			return;
		}
		m_LargeMapFuture = GenerateMap(m_Streamer->Density(), center, extent, resolution, true);
	}

	void TerrainGenerator::UpdateLargeMap()
	{
		if (m_LargeMapFuture.valid() && m_LargeMapFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			m_LargeMap = m_LargeMapFuture.get();
			const TextureID textureID = CreateMapTexture(m_LargeMap, "Terrain map");
			if (m_LargeMapTextureID != InvalidTextureID)
			{
				m_RetiredMinimapTextures.emplace_back(m_LargeMapTextureID, 0u);
			}
			m_LargeMapTextureID = textureID;
		}
	}

	bool TerrainGenerator::GetLargeMap(TextureID& outTextureID, glm::vec3& outCenter, real& outExtent) const
	{
		if (m_LargeMapTextureID == InvalidTextureID || !m_LargeMap.bValid)
		{
			return false;
		}
		outTextureID = m_LargeMapTextureID;
		outCenter = m_LargeMap.center;
		outExtent = m_LargeMap.extent;
		return true;
	}

	void TerrainGenerator::Regenerate()
	{
		// Applied on the next update. Existing chunks remain visible until their replacements are ready.
		m_bParamsDirty = true;
		m_QueryDensity.reset();
	}

	real TerrainGenerator::SampleHeight(const glm::vec2& pos)
	{
		std::shared_ptr<const terrain::TerrainDensity> density = (m_Streamer != nullptr && !m_bParamsDirty) ? m_Streamer->Density() : nullptr;
		if (density == nullptr)
		{
			if (m_QueryDensity == nullptr)
			{
				m_QueryDensity = std::make_shared<const terrain::TerrainDensity>(GetEffectiveParams());
			}
			density = m_QueryDensity;
		}

		return density->SampleColumn(pos.x, pos.y).height;
	}

	void TerrainGenerator::UpdateRoadSegments()
	{
		m_RoadSegments.clear();

		Road* road = (Road*)m_RoadGameObjectID.Get();
		if (road == nullptr)
		{
			m_RoadGameObjectID = g_SceneManager->CurrentScene()->FirstObjectWithTag("road");
			road = (Road*)m_RoadGameObjectID.Get();
		}

		if (road == nullptr)
		{
			return;
		}

		// Amount of extra distance blending can affect, must be taken into account
		// here to ensure there are no discontinuities between chunks where there is falloff
		real blendRadiusBoost = m_RoadBlendDist + m_RoadBlendThreshold;
		glm::vec3 pos = m_Transform.GetWorldPosition();
		for (const RoadSegment& roadSegment : road->roadSegments)
		{
			i32 minChunkIndexX = (i32)glm::floor((roadSegment.aabb.minX + pos.x - blendRadiusBoost) / m_RoadChunkSize);
			i32 maxChunkIndexX = (i32)glm::ceil((roadSegment.aabb.maxX + pos.x + blendRadiusBoost) / m_RoadChunkSize);
			i32 minChunkIndexY = (i32)glm::floor((roadSegment.aabb.minY + pos.y - blendRadiusBoost) / m_RoadChunkSize);
			i32 maxChunkIndexY = (i32)glm::ceil((roadSegment.aabb.maxY + pos.y + blendRadiusBoost) / m_RoadChunkSize);
			i32 minChunkIndexZ = (i32)glm::floor((roadSegment.aabb.minZ + pos.z - blendRadiusBoost) / m_RoadChunkSize);
			i32 maxChunkIndexZ = (i32)glm::ceil((roadSegment.aabb.maxZ + pos.z + blendRadiusBoost) / m_RoadChunkSize);
			for (i32 x = minChunkIndexX; x < maxChunkIndexX; ++x)
			{
				for (i32 y = minChunkIndexY; y < maxChunkIndexY; ++y)
				{
					for (i32 z = minChunkIndexZ; z < maxChunkIndexZ; ++z)
					{
						glm::ivec3 chunkIndex = glm::ivec3(x, y, z);
						auto iter = m_RoadSegments.find(chunkIndex);
						if (iter == m_RoadSegments.end())
						{
							m_RoadSegments.emplace(chunkIndex, std::vector<RoadSegment*>{ (RoadSegment*)&roadSegment });
						}
						else
						{
							iter->second.emplace_back((RoadSegment*)&roadSegment);
						}
					}
				}
			}
		}
	}

	void TerrainGenerator::DestroyChunkRigidBody(Chunk& chunk)
	{
		PROFILE_AUTO("TerrainGenerator DestroyChunkRigidBody");
		if (chunk.rigidBody != nullptr)
		{
			chunk.rigidBody->Destroy();
			delete chunk.rigidBody;
			chunk.rigidBody = nullptr;
		}

		if (chunk.foliageRigidBody != nullptr)
		{
			chunk.foliageRigidBody->Destroy();
			delete chunk.foliageRigidBody;
			chunk.foliageRigidBody = nullptr;
		}

		// Nothing references the shapes now that the bodies are out of the physics world. Freeing their trees takes a
		// while, which adds up when many chunks are unloaded at once, so it's done in the background.
		struct CollisionGarbage
		{
			~CollisionGarbage()
			{
				delete collisionShape;
				delete triangleIndexVertexArray;
				delete foliageCollisionShape;
				delete foliageTriangleIndexVertexArray;
				// (After the shapes referencing its triangles)
				collisionData.reset();
			}

			btCollisionShape* collisionShape;
			btTriangleIndexVertexArray* triangleIndexVertexArray;
			btCollisionShape* foliageCollisionShape;
			btTriangleIndexVertexArray* foliageTriangleIndexVertexArray;
			std::shared_ptr<ChunkCollisionBuild> collisionData;
		};
		if (chunk.collisionShape != nullptr || chunk.triangleIndexVertexArray != nullptr || chunk.foliageCollisionShape != nullptr ||
			chunk.foliageTriangleIndexVertexArray != nullptr || chunk.collisionData != nullptr)
		{
			JobSystem::DeleteInBackground(new CollisionGarbage{ chunk.collisionShape, chunk.triangleIndexVertexArray, chunk.foliageCollisionShape,
				chunk.foliageTriangleIndexVertexArray, std::move(chunk.collisionData) });
		}
		chunk.collisionShape = nullptr;
		chunk.triangleIndexVertexArray = nullptr;
		chunk.foliageCollisionShape = nullptr;
		chunk.foliageTriangleIndexVertexArray = nullptr;
		chunk.collisionData.reset();
	}

	void TerrainGenerator::BuildChunkCollisionShapes(Chunk& chunk)
	{
		MeshComponent* submesh = chunk.meshComponent;
		if (chunk.rigidBody != nullptr || chunk.collisionShape != nullptr || submesh == nullptr || submesh->GetIndexCount() == 0 ||
			chunk.collisionIndexCount == 0)
		{
			return;
		}

		// (Foliage, after collisionIndexCount, gets its own shape which only rays hit)
		std::shared_ptr<ChunkCollisionBuild> build = std::make_shared<ChunkCollisionBuild>();
		build->CopyTriangles(chunk.boundsMin, submesh, chunk.collisionIndexCount);
		build->Build();
		chunk.triangleIndexVertexArray = build->triangleIndexVertexArray;
		chunk.collisionShape = build->collisionShape;
		chunk.foliageTriangleIndexVertexArray = build->foliageTriangleIndexVertexArray;
		chunk.foliageCollisionShape = build->foliageCollisionShape;
		// Owned by the chunk now
		build->triangleIndexVertexArray = nullptr;
		build->collisionShape = nullptr;
		build->foliageTriangleIndexVertexArray = nullptr;
		build->foliageCollisionShape = nullptr;
		chunk.collisionData = build;
	}

	void TerrainGenerator::CreateChunkRigidBody(Chunk& chunk)
	{
		if (chunk.rigidBody != nullptr)
		{
			return;
		}

		MeshComponent* submesh = chunk.meshComponent;
		if (submesh == nullptr || submesh->GetIndexCount() == 0 || chunk.collisionIndexCount == 0)
		{
			return;
		}

		// (Usually already built, in parallel)
		BuildChunkCollisionShapes(chunk);

		// The triangles are relative to this (see ChunkCollisionBuild)
		const glm::vec3 origin = chunk.collisionData != nullptr ? chunk.collisionData->origin : VEC3_ZERO;
		auto PlaceAtOrigin = [origin](RigidBody* body)
		{
			btRigidBody* rb = body->GetRigidBodyInternal();
			const btTransform transform(btQuaternion::getIdentity(), ToBtVec3(origin));
			rb->setWorldTransform(transform);
			rb->setInterpolationWorldTransform(transform);
			g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld()->updateSingleAabb(rb);
		};

		RigidBody* rigidBody = new RigidBody((u32)CollisionType::STATIC, (u32)CollisionType::DEFAULT & ~(u32)CollisionType::STATIC);
		rigidBody->SetStatic(true);
		rigidBody->Initialize(chunk.collisionShape, &m_Transform);
		PlaceAtOrigin(rigidBody);
		chunk.rigidBody = rigidBody;
		chunk.rigidBody->GetRigidBodyInternal()->setActivationState(WANTS_DEACTIVATION);

		// Foliage gets a separate body which only rays asking for it hit (nothing collides with it), so trees can be
		// targeted (e.g. by the grappling hook)
		if (chunk.foliageCollisionShape != nullptr)
		{
			RigidBody* foliageRigidBody = new RigidBody((u32)CollisionType::RAYCAST_ONLY, (u32)CollisionType::RAYCAST_ONLY);
			foliageRigidBody->SetStatic(true);
			foliageRigidBody->Initialize(chunk.foliageCollisionShape, &m_Transform);
			PlaceAtOrigin(foliageRigidBody);
			btRigidBody* rb = foliageRigidBody->GetRigidBodyInternal();
			rb->setCollisionFlags(rb->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
			rb->setActivationState(WANTS_DEACTIVATION);
			chunk.foliageRigidBody = foliageRigidBody;
		}
	}

	void TerrainGenerator::DestroyChunk(Chunk& chunk)
	{
		PROFILE_AUTO("TerrainGenerator DestroyChunk");
		DestroyChunkRigidBody(chunk);

		auto DestroyMeshComponent = [this](MeshComponent*& meshComponent, i32& submeshIndex)
		{
			if (meshComponent != nullptr)
			{
				if (m_Mesh != nullptr && submeshIndex >= 0)
				{
					m_Mesh->RemoveSubmesh((u32)submeshIndex);
				}
				meshComponent->Destroy();
				delete meshComponent;
				meshComponent = nullptr;
				submeshIndex = -1;
			}
		};

		DestroyMeshComponent(chunk.meshComponent, chunk.submeshIndex);
		DestroyMeshComponent(chunk.waterMeshComponent, chunk.waterSubmeshIndex);
	}

	void TerrainGenerator::DestroyAllChunks()
	{
		for (auto& pair : m_Chunks)
		{
			DestroyChunk(pair.second);
		}
		m_Chunks.clear();

		for (auto& pair : m_StagedChunks)
		{
			DestroyChunk(pair.second);
		}
		m_StagedChunks.clear();
	}

	// ---
} // namespace flex
