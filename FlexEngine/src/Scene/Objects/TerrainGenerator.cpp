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
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "FlexEngine.hpp"
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
#include "Scene/Objects/Road.hpp"
#include "Scene/SceneManager.hpp"
#include "Time.hpp"
#include "Window/Window.hpp"

namespace flex
{
	TerrainGenerator* TerrainGenerator::s_Active = nullptr;
	bool TerrainGenerator::s_bShowWorldMap = false;
	bool TerrainGenerator::s_bShowMinimap = true;

	// LOD distance factor multipliers for each terrain quality level (see Renderer::GetTerrainQuality)
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

		m_Mesh = new Mesh(this);
		m_Mesh->SetTypeToMemory();

		u32 threadCount = m_ThreadCount;
		if (threadCount == 0)
		{
			// Leave room for the main & render threads
			threadCount = (u32)glm::clamp((i32)std::thread::hardware_concurrency() - 2, 1, 16);
		}

		m_AppliedQuality = g_Renderer->GetTerrainQuality();
		const terrain::WorldParams params = GetEffectiveParams();
		m_Streamer = new terrain::TerrainStreamer();

		std::string snapshotPath;
		if (m_bEnableDiskCache)
		{
			const std::string saveFilePathBase = GetSaveFilePathBase();
			Platform::CreateDirectoryRecursive(RelativePathToAbsolute(SAVED_DIRECTORY "terrain/"));

			// Edits made in previous sessions
			std::vector<terrain::Edit> edits;
			if (terrain::ReadEdits(saveFilePathBase + ".edits", params.seed, edits))
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
		m_LoadStartTime = Time::CurrentNanoseconds();
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
		return params;
	}

	void TerrainGenerator::UpdateSceneFog()
	{
		if (!m_bAutoFog)
		{
			return;
		}

		// Fade out terrain before it reaches the view distance so the cutoff isn't visible
		g_SceneManager->CurrentScene()->SetFogDistance(m_Params.viewDistance * 0.92f);
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

			if (m_bParamsDirty)
			{
				m_bParamsDirty = false;
				const terrain::WorldParams params = GetEffectiveParams();

				// Edits are stored per world (seed)
				if (params.seed != m_LoadedEditsSeed && m_bEnableDiskCache)
				{
					SaveEdits();
					m_LoadedEditsSeed = params.seed;
					std::vector<terrain::Edit> edits;
					terrain::ReadEdits(GetSaveFilePathBase() + ".edits", params.seed, edits);
					m_Streamer->SetEdits(edits);
				}
				m_LoadedEditsSeed = params.seed;
				if (m_bEnableDiskCache)
				{
					m_Streamer->SetSnapshotPath(GetSaveFilePathBase() + ".snapshot");
				}

				m_Streamer->SetParams(params);
				m_QueryDensity.reset();
				m_LoadStartTime = Time::CurrentNanoseconds();
				m_bLoadComplete = false;
				m_MaxApplyTime = 0.0f;
				m_WorldMap.bValid = false;
				UpdateSceneFog();
			}

			const glm::vec3 center = m_bPinCenter ? m_PinnedPos : g_CameraManager->CurrentCamera()->position;
			m_LastCenter = center;
			m_Streamer->Update(center, m_ChunkCommands);

			// Ensure the far plane reaches the view distance
			BaseCamera* camera = g_CameraManager->CurrentCamera();
			if (m_bAutoFog && camera->zFar < m_Params.viewDistance * 1.05f)
			{
				camera->zFar = m_Params.viewDistance * 1.05f;
			}
			ApplyChunkCommands();
			UpdateRigidBodies();
			UpdateEditorBrush();
			UpdateMinimap();

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
					u32 triangleCount = 0;
					for (auto& pair : m_Chunks)
					{
						triangleCount += pair.second.meshComponent != nullptr ? pair.second.meshComponent->GetIndexCount() / 3 : 0;
					}
					const ms loadTime = Time::ConvertFormats(Time::CurrentNanoseconds() - m_LoadStartTime, Time::Format::NANOSECOND, Time::Format::MILLISECOND);
					Print("Terrain loaded in %.0fms (%u chunks, %u non-empty, %u from disk cache, %u triangles, avg %.2fms/chunk on %u threads, avg upload %.2fms/chunk, max upload %.1fms/frame)\n",
						loadTime, stats.displayedChunkCount, stats.displayedNonEmptyChunkCount, stats.snapshotChunkCount, triangleCount,
						stats.averageGenerationTime, stats.threadCount, m_UploadCount > 0 ? m_TotalUploadTime / (real)m_UploadCount : 0.0f, m_MaxApplyTime);
				}
			}
		}

		GameObject::Update();
	}

	TerrainGenerator::Chunk TerrainGenerator::CreateChunk(terrain::ChunkCommand& command, bool bHidden)
	{
		Material* terrainMat = g_Renderer->GetMaterial(m_TerrainMatID);
		Shader* terrainShader = g_Renderer->GetShader(terrainMat->shaderID);

		static VertexBufferDataCreateInfo vertexBufferCreateInfo = {};
		vertexBufferCreateInfo.attributes = terrainShader->vertexAttributes;

		auto CreateMeshComponent = [&](std::vector<glm::vec3>&& positions, std::vector<glm::vec3>&& normals, std::vector<glm::vec4>&& colours,
			std::vector<glm::vec4>&& weights, const std::vector<u32>& indices, bool bCastsShadow, i32* outSubmeshIndex)
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

			MeshComponent* meshComponent = MeshComponent::LoadFromMemoryDynamic(m_Mesh, vertexBufferCreateInfo, indices, m_TerrainMatID,
				vertexCount, &renderObjectCreateInfo, true, outSubmeshIndex);
			if (meshComponent != nullptr)
			{
				meshComponent->UpdateDynamicVertexData(vertexBufferCreateInfo, indices);
			}
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

		if (!mesh.indices.empty())
		{
			chunk.meshComponent = CreateMeshComponent(std::move(mesh.positions), std::move(mesh.normals), std::move(mesh.colours),
				std::move(mesh.weights), mesh.indices, true, &chunk.submeshIndex);
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
				mesh.waterIndices, false, &chunk.waterSubmeshIndex);
		}

		if (bHidden)
		{
			SetChunkHidden(chunk, true);
		}

		return chunk;
	}

	TerrainGenerator::Chunk TerrainGenerator::CreateChunkTimed(terrain::ChunkCommand& command, bool bHidden)
	{
		const ns start = Time::CurrentNanoseconds();
		Chunk chunk = CreateChunk(command, bHidden);
		m_TotalUploadTime += Time::ConvertFormats(Time::CurrentNanoseconds() - start, Time::Format::NANOSECOND, Time::Format::MILLISECOND);
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

		const ns start = Time::CurrentNanoseconds();
		// Uploads are spread over multiple frames to avoid hitches (with a larger budget while the initial load fills the
		// screen, or when falling behind while moving quickly)
		const ms frameBudget = m_bLoadComplete ? glm::mix(4.0f, 8.0f, glm::clamp((real)m_ChunkCommands.size() / 200.0f, 0.0f, 1.0f)) : 12.0f;
		auto IsOverBudget = [start, frameBudget]()
		{
			return Time::ConvertFormats(Time::CurrentNanoseconds() - start, Time::Format::NANOSECOND, Time::Format::MILLISECOND) > frameBudget;
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
		m_ChunkCommands.erase(m_ChunkCommands.begin(), m_ChunkCommands.begin() + head);

		m_LastApplyTime = Time::ConvertFormats(Time::CurrentNanoseconds() - start, Time::Format::NANOSECOND, Time::Format::MILLISECOND);
		m_MaxApplyTime = glm::max(m_MaxApplyTime, m_LastApplyTime);
	}

	void TerrainGenerator::UpdateRigidBodies()
	{
		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		const bool bHasPlayer = player != nullptr;
		const glm::vec3 playerPos = bHasPlayer ? player->GetTransform()->GetWorldPosition() : VEC3_ZERO;

		for (auto& pair : m_Chunks)
		{
			Chunk& chunk = pair.second;
			bool bShouldHaveRigidBody = false;
			if (bHasPlayer)
			{
				const glm::vec3 delta = glm::max(glm::max(chunk.boundsMin - playerPos, playerPos - chunk.boundsMax), VEC3_ZERO);
				bShouldHaveRigidBody = glm::length(delta) < m_RigidBodyLoadRadius;
			}

			if (bShouldHaveRigidBody && chunk.rigidBody == nullptr)
			{
				CreateChunkRigidBody(chunk);
			}
			else if (!bShouldHaveRigidBody && chunk.rigidBody != nullptr)
			{
				DestroyChunkRigidBody(chunk);
			}
		}
	}

	bool TerrainGenerator::Raycast(const glm::vec3& origin, const glm::vec3& dir, real maxDistance, glm::vec3& outHitPos, glm::vec3& outHitNormal) const
	{
		return m_Streamer != nullptr && m_Streamer->Raycast(origin, dir, maxDistance, outHitPos, outHitNormal);
	}

	void TerrainGenerator::Dig(const glm::vec3& center, real radius)
	{
		terrain::Edit edit = {};
		edit.center = center;
		edit.radius = radius;
		edit.type = terrain::EditType::DIG;
		edit.material = terrain::Material::SOIL;
		ApplyEdit(edit);
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

		return false;
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
		if (m_bEnableDiskCache)
		{
			const std::string sceneName = StripFileType(g_SceneManager->CurrentScene()->GetFileName());
			const std::string filePath = RelativePathToAbsolute(SAVED_DIRECTORY "terrain/") + sceneName + "_" + std::to_string(m_LoadedEditsSeed) + ".edits";
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
			if (m_bEnableDiskCache)
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

		ImGui::Text("Features");
		bChanged = ImGui::DragFloat("Overhang amplitude", &biome.overhangAmplitude, 0.5f, 0.0f, 200.0f) || bChanged;
		bChanged = ImGui::SliderFloat("Arch strength", &biome.archStrength, 0.0f, 1.0f) || bChanged;
		bChanged = ImGui::SliderFloat("Cave strength", &biome.caveStrength, 0.0f, 1.0f) || bChanged;
		bChanged = ImGui::SliderFloat("Lake strength", &biome.lakeStrength, 0.0f, 1.0f) || bChanged;

		ImGui::Text("Surface");
		bChanged = ImGui::ColorEdit3("Ground", &biome.groundColour.x) || bChanged;
		bChanged = ImGui::SliderFloat("Grassiness", &biome.grassiness, 0.0f, 1.0f) || bChanged;
		bChanged = ImGui::ColorEdit3("Cliff", &biome.cliffColour.x) || bChanged;
		bChanged = ImGui::ColorEdit3("Peak", &biome.peakColour.x) || bChanged;
		bChanged = ImGui::DragFloat("Peak height", &biome.peakHeight, 5.0f, -1000.0f, 100000.0f) || bChanged;

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

		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		if (player != nullptr)
		{
			if (!player->IsWieldingPickAxe() && ImGui::Button("Give player a pickaxe"))
			{
				const PrefabID pickaxeID = g_ResourceManager->GetPrefabID("pickaxe");
				if (pickaxeID.IsValid())
				{
					player->AddToInventory(pickaxeID, 1);
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Give player a vehicle"))
			{
				const PrefabID vehicleID = g_ResourceManager->GetPrefabID("vehicle");
				if (vehicleID.IsValid())
				{
					player->AddToInventory(vehicleID, 1);
				}
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Place it with the place item key, & pick it up again by holding the pickup key while looking at it");
			}
		}
		else if (ImGui::Button("Spawn player at camera"))
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

	std::future<TerrainGenerator::WorldMap> TerrainGenerator::GenerateMap(std::shared_ptr<const terrain::TerrainDensity> density, const glm::vec3& center, real extent, u32 resolution)
	{
		return std::async(std::launch::async, [density, center, extent, resolution]()
		{
			WorldMap map = {};
			map.center = center;
			map.extent = extent;
			map.resolution = resolution;
			map.colours.resize(resolution * resolution);

			// Rows run from +z (north, top) to -z
			std::vector<terrain::ColumnSample> columns(resolution * resolution);
			const real cellSize = (2.0f * extent) / (real)resolution;
			for (u32 row = 0; row < resolution; ++row)
			{
				for (u32 x = 0; x < resolution; ++x)
				{
					const real px = center.x - extent + ((real)x + 0.5f) * cellSize;
					const real pz = center.z + extent - ((real)row + 0.5f) * cellSize;
					columns[row * resolution + x] = density->SampleColumn(px, pz);
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

		const bool bStale = !m_Minimap.bValid || glm::distance(glm::vec2(pos.x, pos.z), glm::vec2(m_Minimap.center.x, m_Minimap.center.z)) > m_MinimapExtent * 0.15f;
		if (bStale && !m_MinimapFuture.valid())
		{
			m_MinimapFuture = GenerateMap(m_Streamer->Density(), pos, m_MinimapExtent, 96);
		}

		if (m_MinimapTextureID == InvalidTextureID || !m_Minimap.bValid)
		{
			return;
		}

		// Positions are in the sprite's screen space ([-aspect, aspect] x [-1, 1], +y up), anchored at sprite centers
		const glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
		const real aspectRatio = (real)frameBufferSize.x / (real)glm::max(frameBufferSize.y, 1);
		const real halfSize = 0.28f;
		const real margin = 0.05f;
		const glm::vec2 center(aspectRatio - margin - halfSize, 1.0f - margin - halfSize);

		g_Renderer->EnqueueUntexturedQuad(center, AnchorPoint::CENTER, glm::vec2(halfSize + 0.008f), glm::vec4(0.05f, 0.05f, 0.06f, 0.85f));

		SpriteQuadDrawInfo mapSprite = {};
		mapSprite.textureID = m_MinimapTextureID;
		mapSprite.anchor = AnchorPoint::CENTER;
		mapSprite.bScreenSpace = true;
		mapSprite.bReadDepth = false;
		mapSprite.pos = glm::vec3(center, 0.0f);
		mapSprite.scale = glm::vec3(halfSize, halfSize, 1.0f);
		g_Renderer->EnqueueSprite(mapSprite);

		// Player marker & heading
		glm::vec2 offset = glm::vec2(pos.x - m_Minimap.center.x, pos.z - m_Minimap.center.z) / m_Minimap.extent;
		offset = glm::clamp(offset, glm::vec2(-1.0f), glm::vec2(1.0f));
		const glm::vec2 markerPos = center + offset * halfSize;
		glm::vec2 forward(camera->forward.x, camera->forward.z);
		forward = glm::length(forward) > 1e-4f ? glm::normalize(forward) : glm::vec2(0.0f, 1.0f);
		g_Renderer->EnqueueUntexturedQuad(markerPos, AnchorPoint::CENTER, glm::vec2(0.009f), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
		g_Renderer->EnqueueUntexturedQuad(markerPos, AnchorPoint::CENTER, glm::vec2(0.006f), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
		g_Renderer->EnqueueUntexturedQuad(markerPos + forward * 0.022f, AnchorPoint::CENTER, glm::vec2(0.004f), glm::vec4(1.0f, 0.35f, 0.25f, 1.0f));
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
			const real dashOffset = (real)glm::mod(g_SecElapsedSinceProgramStart * 12.0f, 1000.0f);
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
		biomeObj.TryGetFloat("overhang amplitude", biome.overhangAmplitude);
		biomeObj.TryGetFloat("arch strength", biome.archStrength);
		biomeObj.TryGetFloat("cave strength", biome.caveStrength);
		biomeObj.TryGetFloat("lake strength", biome.lakeStrength);
		biomeObj.TryGetVec3("ground colour", biome.groundColour);
		biomeObj.TryGetFloat("grassiness", biome.grassiness);
		biomeObj.TryGetVec3("cliff colour", biome.cliffColour);
		biomeObj.TryGetVec3("peak colour", biome.peakColour);
		biomeObj.TryGetFloat("peak height", biome.peakHeight);
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
		biomeObj.fields.emplace_back("overhang amplitude", JSONValue(biome.overhangAmplitude, 1));
		biomeObj.fields.emplace_back("arch strength", JSONValue(biome.archStrength, 2));
		biomeObj.fields.emplace_back("cave strength", JSONValue(biome.caveStrength, 2));
		biomeObj.fields.emplace_back("lake strength", JSONValue(biome.lakeStrength, 2));
		biomeObj.fields.emplace_back("ground colour", JSONValue(biome.groundColour, 2));
		biomeObj.fields.emplace_back("grassiness", JSONValue(biome.grassiness, 2));
		biomeObj.fields.emplace_back("cliff colour", JSONValue(biome.cliffColour, 2));
		biomeObj.fields.emplace_back("peak colour", JSONValue(biome.peakColour, 2));
		biomeObj.fields.emplace_back("peak height", JSONValue(biome.peakHeight, 1));
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

		std::vector<JSONObject> biomesArr;
		biomesArr.reserve(m_Params.biomes.size());
		for (const terrain::BiomeParams& biome : m_Params.biomes)
		{
			biomesArr.emplace_back(SerializeBiome(biome));
		}
		chunkGenInfo.fields.emplace_back("biomes", JSONValue(biomesArr));

		parentObject.fields.emplace_back("chunk generator info", JSONValue(chunkGenInfo));
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
		if (chunk.rigidBody != nullptr)
		{
			chunk.rigidBody->Destroy();
			delete chunk.rigidBody;
			chunk.rigidBody = nullptr;
		}

		delete chunk.collisionShape;
		chunk.collisionShape = nullptr;

		delete chunk.triangleIndexVertexArray;
		chunk.triangleIndexVertexArray = nullptr;
	}

	void TerrainGenerator::CreateChunkRigidBody(Chunk& chunk)
	{
		if (chunk.rigidBody != nullptr)
		{
			return;
		}

		MeshComponent* submesh = chunk.meshComponent;
		if (submesh == nullptr || submesh->GetIndexCount() == 0)
		{
			return;
		}

		CHECK_EQ(chunk.collisionShape, nullptr);

		submesh->CreateCollisionMesh(&chunk.triangleIndexVertexArray, (btBvhTriangleMeshShape**)&chunk.collisionShape);

		RigidBody* rigidBody = new RigidBody((u32)CollisionType::STATIC, (u32)CollisionType::DEFAULT & ~(u32)CollisionType::STATIC);
		rigidBody->SetStatic(true);
		rigidBody->Initialize(chunk.collisionShape, &m_Transform);
		chunk.rigidBody = rigidBody;
		chunk.rigidBody->GetRigidBodyInternal()->setActivationState(WANTS_DEACTIVATION);
	}

	void TerrainGenerator::DestroyChunk(Chunk& chunk)
	{
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
