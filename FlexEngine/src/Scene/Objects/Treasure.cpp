#include "stdafx.hpp"

#include "Scene/Objects/Treasure.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btCompoundShape.h>
#include <glm/gtx/quaternion.hpp>
IGNORE_WARNINGS_POP

#include "Audio/AudioManager.hpp"
#include "Graphics/Renderer.hpp"
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/Objects/Building.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	std::map<u64, TreasureChest::Site> TreasureChest::s_Sites;
	std::set<u64> TreasureChest::s_OpenedChests;
	i32 TreasureChest::s_SitesSeed = 0;

	namespace
	{
		const real SPAWN_DISTANCE = 350.0f;
		const real DESPAWN_DISTANCE = 500.0f;
		const real MIN_SITE_HEIGHT = 450.0f; // Only mountains (relative to sea level)
		const glm::vec3 CHEST_HALF_EXTENTS(0.55f, 0.35f, 0.38f);

		u32 HashU32(u32 x)
		{
			x ^= x >> 16;
			x *= 0x7feb352du;
			x ^= x >> 15;
			x *= 0x846ca68bu;
			x ^= x >> 16;
			return x;
		}

		real HashToUnit(u32 h)
		{
			return (real)(h & 0xFFFFFF) / (real)0x1000000;
		}

		u64 SiteKey(i32 cellX, i32 cellZ)
		{
			return ((u64)(u32)cellX << 32) | (u64)(u32)cellZ;
		}

		MaterialID GetOrCreateMaterial(const char* name, const glm::vec4& albedo, real roughness, real metallic, const glm::vec4& emissive = VEC4_ZERO)
		{
			MaterialID materialID = g_Renderer->GetMaterialID(name);
			if (materialID == InvalidMaterialID)
			{
				MaterialCreateInfo createInfo = {};
				createInfo.name = name;
				createInfo.shaderName = "pbr";
				// Colours are given in sRGB, materials take linear values
				createInfo.constAlbedo = glm::vec4(glm::pow(glm::vec3(albedo), glm::vec3(2.2f)), albedo.a);
				createInfo.constRoughness = roughness;
				createInfo.constMetallic = metallic;
				createInfo.constEmissive = emissive;
				createInfo.bSerializable = false;
				materialID = g_Renderer->InitializeMaterial(&createInfo);
			}
			return materialID;
		}

		struct LootEntry
		{
			const char* prefabName;
			i32 minCount;
			i32 maxCount;
			real chance;
		};

		// Special items, a few of which are in each chest
		const LootEntry s_LootTable[] =
		{
			{ "dune buggy", 1, 1, 0.35f },
			{ "headlamp", 1, 1, 0.5f },
			{ "solar panel", 1, 2, 0.35f },
			{ "battery", 1, 3, 0.4f },
			{ "lamp", 2, 4, 0.4f },
			{ "mineral deposit quartz", 3, 8, 0.4f },
			{ "mineral deposit aluminum", 3, 8, 0.35f },
			{ "mineral deposit tin", 3, 8, 0.35f },
			{ "mineral deposit olivine", 2, 6, 0.3f },
		};
	} // anonymous namespace

	TreasureChest::TreasureChest(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /* = false */) :
		GameObject(name, TreasureChestSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		m_bInteractable = true;
		// Placed by the world, not saved with the scene
		SetSerializable(false);
	}

	PropertyCollection* TreasureChest::BuildTypeUniquePropertyCollection()
	{
		return GameObject::BuildPropertyCollection();
	}

	void TreasureChest::CreateMeshes()
	{
		const MaterialID woodMaterialID = GetOrCreateMaterial("Treasure chest wood", glm::vec4(0.42f, 0.24f, 0.1f, 1.0f), 0.75f, 0.0f);
		// Gilded bands glint a little so chests catch the eye from a distance
		const MaterialID goldMaterialID = GetOrCreateMaterial("Treasure chest gold", glm::vec4(1.0f, 0.78f, 0.3f, 1.0f), 0.3f, 1.0f, glm::vec4(0.6f, 0.42f, 0.1f, 1.0f));

		auto BuildMesh = [&](GameObject* object, const std::vector<std::pair<glm::vec3, glm::vec3>>& woodBoxes, const std::vector<std::pair<glm::vec3, glm::vec3>>& goldBoxes)
		{
			Mesh* mesh = object->SetMesh(new Mesh(object));
			const std::pair<MaterialID, const std::vector<std::pair<glm::vec3, glm::vec3>>*> parts[] = { { woodMaterialID, &woodBoxes }, { goldMaterialID, &goldBoxes } };
			for (const auto& part : parts)
			{
				VertexBufferDataCreateInfo vertexData = {};
				vertexData.attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(part.first)->shaderID)->vertexAttributes;
				std::vector<u32> indices;
				for (const auto& box : *part.second)
				{
					AppendBoxToVertexData(box.first, box.second, QUAT_IDENTITY, vertexData, indices);
				}
				MeshComponent::LoadFromMemory(mesh, vertexData, indices, part.first);
			}
		};

		const glm::vec3 h = CHEST_HALF_EXTENTS;
		// Base, from the ground up
		BuildMesh(this,
			{ { glm::vec3(0.0f, h.y, 0.0f), h } },
			{
				{ glm::vec3(h.x * 0.6f, h.y, 0.0f), glm::vec3(0.05f, h.y + 0.01f, h.z + 0.01f) },
				{ glm::vec3(-h.x * 0.6f, h.y, 0.0f), glm::vec3(0.05f, h.y + 0.01f, h.z + 0.01f) },
				{ glm::vec3(0.0f, h.y * 1.6f, -h.z - 0.01f), glm::vec3(0.08f, 0.1f, 0.02f) },
			});

		// Lid, hinged along the back (+z) edge
		m_Lid = new GameObject("Chest lid", BaseObjectSID);
		m_Lid->SetSerializable(false);
		m_Lid->SetVisibleInSceneExplorer(false);
		const real lidHeight = 0.22f;
		BuildMesh(m_Lid,
			{ { glm::vec3(0.0f, lidHeight * 0.5f, -h.z), glm::vec3(h.x + 0.02f, lidHeight * 0.5f, h.z + 0.02f) } },
			{
				{ glm::vec3(h.x * 0.6f, lidHeight * 0.5f, -h.z), glm::vec3(0.05f, lidHeight * 0.5f + 0.01f, h.z + 0.03f) },
				{ glm::vec3(-h.x * 0.6f, lidHeight * 0.5f, -h.z), glm::vec3(0.05f, lidHeight * 0.5f + 0.01f, h.z + 0.03f) },
			});
		m_Lid->GetTransform()->SetLocalPosition(glm::vec3(0.0f, h.y * 2.0f, h.z));
		AddChild(m_Lid);
	}

	void TreasureChest::Initialize()
	{
		CreateMeshes();

		if (m_RigidBody == nullptr)
		{
			btCompoundShape* shape = new btCompoundShape();
			shape->addChildShape(btTransform(btQuaternion::getIdentity(), btVector3(0.0f, CHEST_HALF_EXTENTS.y + 0.1f, 0.0f)),
				new btBoxShape(ToBtVec3(CHEST_HALF_EXTENTS + glm::vec3(0.0f, 0.1f, 0.0f))));
			SetCollisionShape(shape);
			RigidBody* rigidBody = SetRigidBody(new RigidBody((u32)CollisionType::STATIC, (u32)CollisionType::DEFAULT & ~(u32)CollisionType::STATIC));
			rigidBody->SetStatic(true);
		}

		GameObject::Initialize();
	}

	void TreasureChest::Destroy(bool bDetachFromParent /* = true */)
	{
		for (auto& pair : s_Sites)
		{
			if (pair.second.chestID == ID)
			{
				pair.second.chestID = InvalidGameObjectID;
			}
		}

		GameObject::Destroy(bDetachFromParent);
	}

	void TreasureChest::Update()
	{
		// Swing the lid open
		const real targetAngle = m_bOpen ? glm::radians(-105.0f) : 0.0f;
		m_LidAngle = glm::mix(m_LidAngle, targetAngle, glm::clamp(g_DeltaTime * 6.0f, 0.0f, 1.0f));
		if (m_Lid != nullptr)
		{
			m_Lid->GetTransform()->SetLocalRotation(glm::angleAxis(m_LidAngle, VEC3_RIGHT));
		}

		GameObject::Update();
	}

	void TreasureChest::Open(Player* player)
	{
		if (m_bOpen)
		{
			return;
		}
		m_bOpen = true;
		m_bInteractable = false;
		s_OpenedChests.insert(m_SiteID);

		// Two to four items, chosen deterministically from the chest's site so a world's chests are always the same
		std::string found;
		u32 h = HashU32((u32)m_SiteID ^ HashU32((u32)(m_SiteID >> 32)) ^ 0x51ed27u);
		u32 itemCount = 0;
		const u32 maxItems = 2 + (HashU32(h) % 3);
		for (u32 attempt = 0; attempt < 32 && itemCount < maxItems; ++attempt)
		{
			h = HashU32(h + attempt);
			const LootEntry& entry = s_LootTable[h % ARRAY_LENGTH(s_LootTable)];
			if (HashToUnit(HashU32(h + 1)) > entry.chance)
			{
				continue;
			}
			const PrefabID prefabID = g_ResourceManager->GetPrefabID(entry.prefabName);
			if (!prefabID.IsValid())
			{
				continue;
			}
			const i32 count = entry.minCount + (i32)(HashU32(h + 2) % (u32)(entry.maxCount - entry.minCount + 1));
			player->AddToInventory(prefabID, count);
			found += (itemCount == 0 ? "" : ", ") + IntToString(count) + "x " + entry.prefabName;
			++itemCount;
		}

		player->ShowNotification("Treasure! Found " + (found.empty() ? std::string("nothing (it was empty)") : found));
		AudioManager::PlaySource(g_ResourceManager->GetOrLoadAudioSourceID(SID("jingle-single-01.wav"), true));
	}

	TreasureChest::Site TreasureChest::ComputeSite(TerrainGenerator* terrainGenerator, i32 cellX, i32 cellZ)
	{
		Site site = {};
		const u32 seed = HashU32((u32)terrainGenerator->GetSeed() ^ 0x7ea5u);
		const u32 h = HashU32((u32)cellX * 0x8da6b343u + (u32)cellZ * 0xd8163841u + seed);
		// Rare: most cells don't have one, & of those that do, only mountainous ones keep it
		if (HashToUnit(h) > 0.45f)
		{
			return site;
		}

		// Find the highest point in the cell's interior, coarsely then finely
		const real margin = SITE_CELL_SIZE * 0.1f;
		const glm::vec2 cellMin((real)cellX * SITE_CELL_SIZE + margin, (real)cellZ * SITE_CELL_SIZE + margin);
		const real extent = SITE_CELL_SIZE - margin * 2.0f;
		glm::vec2 best(0.0f);
		real bestHeight = -FLT_MAX;
		const u32 coarse = 12;
		for (u32 i = 0; i < coarse; ++i)
		{
			for (u32 j = 0; j < coarse; ++j)
			{
				const glm::vec2 p = cellMin + glm::vec2(((real)i + 0.5f) / (real)coarse, ((real)j + 0.5f) / (real)coarse) * extent;
				const terrain::ColumnSample column = terrainGenerator->SampleColumn(p.x, p.y);
				if (column.height > bestHeight)
				{
					bestHeight = column.height;
					best = p;
				}
			}
		}
		const real fineStep = extent / (real)coarse / 4.0f;
		const glm::vec2 coarseBest = best;
		for (i32 i = -3; i <= 3; ++i)
		{
			for (i32 j = -3; j <= 3; ++j)
			{
				const glm::vec2 p = coarseBest + glm::vec2((real)i, (real)j) * fineStep;
				const terrain::ColumnSample column = terrainGenerator->SampleColumn(p.x, p.y);
				if (column.height > bestHeight)
				{
					bestHeight = column.height;
					best = p;
				}
			}
		}

		const terrain::ColumnSample column = terrainGenerator->SampleColumn(best.x, best.y);
		if (bestHeight < MIN_SITE_HEIGHT || column.oceanWeight > 0.5f)
		{
			return site;
		}

		// A little way down from the very top, on the actual (3D) surface
		glm::vec3 hitPos, hitNormal;
		if (!terrainGenerator->Raycast(glm::vec3(best.x, bestHeight + 200.0f, best.y), glm::vec3(0.0f, -1.0f, 0.0f), 600.0f, hitPos, hitNormal))
		{
			return site;
		}
		site.bValid = true;
		site.pos = hitPos;
		return site;
	}

	void TreasureChest::UpdateNearby(TerrainGenerator* terrainGenerator, const glm::vec3& playerPos)
	{
		if (terrainGenerator->GetSeed() != s_SitesSeed)
		{
			DestroyAll();
			s_Sites.clear();
			s_SitesSeed = terrainGenerator->GetSeed();
		}

		const i32 cellX = (i32)glm::floor(playerPos.x / SITE_CELL_SIZE);
		const i32 cellZ = (i32)glm::floor(playerPos.z / SITE_CELL_SIZE);
		// One new site is computed per frame at most, they take a little while
		bool bComputedSite = false;
		for (i32 dz = -1; dz <= 1; ++dz)
		{
			for (i32 dx = -1; dx <= 1; ++dx)
			{
				const u64 key = SiteKey(cellX + dx, cellZ + dz);
				auto iter = s_Sites.find(key);
				if (iter == s_Sites.end())
				{
					if (bComputedSite)
					{
						continue;
					}
					bComputedSite = true;
					iter = s_Sites.emplace(key, ComputeSite(terrainGenerator, cellX + dx, cellZ + dz)).first;
				}

				Site& site = iter->second;
				if (!site.bValid)
				{
					continue;
				}

				const real distance = glm::distance(glm::vec2(site.pos.x, site.pos.z), glm::vec2(playerPos.x, playerPos.z));
				if (distance < SPAWN_DISTANCE && !site.chestID.IsValid())
				{
					BaseScene* scene = g_SceneManager->CurrentScene();
					TreasureChest* chest = new TreasureChest("Treasure chest");
					chest->m_SiteID = key;
					chest->m_bOpen = s_OpenedChests.find(key) != s_OpenedChests.end();
					chest->m_bInteractable = !chest->m_bOpen;
					chest->m_LidAngle = chest->m_bOpen ? glm::radians(-105.0f) : 0.0f;
					chest->GetTransform()->SetWorldPosition(site.pos);
					chest->GetTransform()->SetWorldRotation(glm::angleAxis(HashToUnit(HashU32((u32)key)) * TWO_PI, VEC3_UP));
					scene->AddRootObject(chest);
					site.chestID = chest->ID;
				}
				else if (distance > DESPAWN_DISTANCE && site.chestID.IsValid())
				{
					GameObject* chest = site.chestID.Get();
					site.chestID = InvalidGameObjectID;
					if (chest != nullptr)
					{
						g_SceneManager->CurrentScene()->RemoveObject(chest, true);
					}
				}
			}
		}
	}

	void TreasureChest::DestroyAll()
	{
		if (!g_SceneManager->HasSceneLoaded())
		{
			return;
		}
		for (auto& pair : s_Sites)
		{
			if (pair.second.chestID.IsValid())
			{
				GameObject* chest = pair.second.chestID.Get();
				pair.second.chestID = InvalidGameObjectID;
				if (chest != nullptr)
				{
					g_SceneManager->CurrentScene()->RemoveObject(chest, true);
				}
			}
		}
	}

	void TreasureChest::GetKnownSites(std::vector<glm::vec3>& outPositions, std::vector<bool>& outOpened)
	{
		for (const auto& pair : s_Sites)
		{
			if (pair.second.bValid)
			{
				outPositions.push_back(pair.second.pos);
				outOpened.push_back(s_OpenedChests.find(pair.first) != s_OpenedChests.end());
			}
		}
	}
} // namespace flex
