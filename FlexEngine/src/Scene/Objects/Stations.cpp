#include "stdafx.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btCompoundShape.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <glm/gtx/norm.hpp> // For distance2
IGNORE_WARNINGS_POP

#include "Scene/Objects/Stations.hpp"

#include "Crafting.hpp"
#include "Graphics/Renderer.hpp"
#include "Graphics/VertexBufferData.hpp"
#include "JSONTypes.hpp"
#include "Physics/RigidBody.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObjectTypes.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/Objects/Building.hpp" // For AppendBoxToVertexData
#include "Scene/Objects/Campfire.hpp" // For woodcraft::GetOrCreateMaterial
#include "Scene/SceneManager.hpp"

namespace flex
{
	std::vector<CraftingStation*> CraftingStation::s_Stations;
	real Furnace::s_SpeedMultiplier = 1.0f;

	namespace
	{
		using BoxList = std::vector<std::pair<glm::vec3, glm::vec3>>; // center, half extents (local space)

		void AddBoxesMesh(GameObject* object, const BoxList& boxes, MaterialID materialID)
		{
			if (boxes.empty())
			{
				return;
			}
			Mesh* mesh = object->GetMesh();
			if (mesh == nullptr)
			{
				mesh = object->SetMesh(new Mesh(object));
			}
			VertexBufferDataCreateInfo vertexData = {};
			vertexData.attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(materialID)->shaderID)->vertexAttributes;
			std::vector<u32> indices;
			for (const auto& box : boxes)
			{
				AppendBoxToVertexData(box.first, box.second, QUAT_IDENTITY, vertexData, indices);
			}
			MeshComponent::LoadFromMemory(mesh, vertexData, indices, materialID);
		}

		// Self illuminated colour for a static mesh (the woodcraft glow materials are dynamic)
		MaterialID GetOrCreateStaticGlowMaterial(const char* name, const glm::vec4& colour)
		{
			MaterialID materialID = g_Renderer->GetMaterialID(name);
			if (materialID == InvalidMaterialID)
			{
				MaterialCreateInfo createInfo = {};
				createInfo.name = name;
				createInfo.shaderName = "colour";
				createInfo.colourMultiplier = colour;
				createInfo.bDynamic = false;
				createInfo.bSerializable = false;
				materialID = g_Renderer->InitializeMaterial(&createInfo);
			}
			return materialID;
		}

		GameObject* CreateChildObject(GameObject* parent, const char* name)
		{
			GameObject* object = new GameObject(name, BaseObjectSID);
			object->SetSerializable(false);
			object->SetVisibleInSceneExplorer(false);
			parent->AddChild(object);
			return object;
		}
	} // anonymous namespace

	CraftingStation::CraftingStation(const std::string& name, StringID typeID, const GameObjectID& gameObjectID, const PrefabIDPair& sourcePrefabID,
		bool bIsPrefabTemplate, u32 slotCount) :
		GameObject(name, typeID, gameObjectID, sourcePrefabID, bIsPrefabTemplate),
		m_Slots(slotCount)
	{
	}

	void CraftingStation::Initialize()
	{
		if (!IsPrefabTemplate())
		{
			CreateMeshes();

			if (m_RigidBody == nullptr)
			{
				const glm::vec3 halfExtents = GetHalfExtents();
				btCompoundShape* shape = new btCompoundShape();
				shape->addChildShape(btTransform(btQuaternion::getIdentity(), btVector3(0.0f, halfExtents.y, 0.0f)), new btBoxShape(ToBtVec3(halfExtents)));
				SetCollisionShape(shape);
				RigidBody* rigidBody = SetRigidBody(new RigidBody((u32)CollisionType::STATIC, (u32)CollisionType::DEFAULT & ~(u32)CollisionType::STATIC));
				rigidBody->SetStatic(true);
			}
			s_Stations.push_back(this);
		}

		GameObject::Initialize();
	}

	void CraftingStation::Destroy(bool bDetachFromParent /* = true */)
	{
		s_Stations.erase(std::remove(s_Stations.begin(), s_Stations.end(), this), s_Stations.end());
		GameObject::Destroy(bDetachFromParent);
	}

	bool CraftingStation::IsEmpty() const
	{
		for (const GameObjectStack& stack : m_Slots)
		{
			if (stack.count > 0)
			{
				return false;
			}
		}
		return true;
	}

	i32 CraftingStation::AddItems(const PrefabID& prefabID, i32 count, u32 firstSlot /* = 0 */, u32 slotCount /* = 0xFFFFFFFF */)
	{
		const u32 end = (u32)glm::min((u64)m_Slots.size(), (u64)firstSlot + (u64)slotCount);
		const i32 maxStack = glm::max((i32)g_ResourceManager->GetMaxStackSize(prefabID), 1);
		// Onto matching stacks first, then into empty slots
		for (i32 pass = 0; pass < 2 && count > 0; ++pass)
		{
			for (u32 i = firstSlot; i < end && count > 0; ++i)
			{
				GameObjectStack& stack = m_Slots[i];
				const bool bMatches = stack.count > 0 && stack.prefabID == prefabID;
				const bool bEmpty = stack.count <= 0;
				if ((pass == 0 && bMatches) || (pass == 1 && bEmpty))
				{
					if (bEmpty)
					{
						stack = GameObjectStack(prefabID, 0);
					}
					const i32 moved = glm::min(maxStack - stack.count, count);
					if (moved > 0)
					{
						stack.count += moved;
						count -= moved;
					}
				}
			}
		}
		return count;
	}

	CraftingStation* CraftingStation::FindNearest(const glm::vec3& pos, real maxDistance)
	{
		CraftingStation* nearest = nullptr;
		real nearestDist2 = maxDistance * maxDistance;
		for (CraftingStation* station : s_Stations)
		{
			const real dist2 = glm::distance2(station->m_Transform.GetWorldPosition(), pos);
			if (dist2 < nearestDist2)
			{
				nearestDist2 = dist2;
				nearest = station;
			}
		}
		return nearest;
	}

	CraftingStation* CraftingStation::Build(StringID typeID, const glm::vec3& pos, real yaw)
	{
		BaseScene* scene = g_SceneManager->CurrentScene();
		CraftingStation* station = nullptr;
		if (typeID == ChestSID)
		{
			station = new Chest(scene->GetUniqueObjectName("Chest_", 3));
		}
		else if (typeID == FurnaceSID)
		{
			station = new Furnace(scene->GetUniqueObjectName("Furnace_", 3));
		}
		if (station == nullptr)
		{
			return nullptr;
		}
		station->GetTransform()->SetWorldPosition(pos);
		station->GetTransform()->SetWorldRotation(glm::angleAxis(yaw, VEC3_UP));
		scene->AddRootObject(station);
		return station;
	}

	void CraftingStation::BuildPreviewGeometry(StringID typeID, PlacementPreview::Geometry& geometry)
	{
		BoxList a, b;
		if (typeID == ChestSID)
		{
			Chest::BuildBoxes(a, b);
		}
		else
		{
			Furnace::BuildBoxes(a, b);
		}
		for (const BoxList* list : { &a, &b })
		{
			for (const auto& box : *list)
			{
				geometry.Box(box.first, box.second + glm::vec3(0.01f), QUAT_IDENTITY);
			}
		}
	}

	void CraftingStation::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject stationObj;
		if (!parentObject.TryGetObject("station", stationObj))
		{
			return;
		}
		std::vector<JSONObject> slots;
		stationObj.TryGetObjectArray("slots", slots);
		for (const JSONObject& slot : slots)
		{
			i32 index = -1;
			slot.TryGetInt("index", index);
			if (index < 0 || index >= (i32)m_Slots.size())
			{
				continue;
			}
			GameObjectStack stack;
			stack.ParseFromJSON(slot);
			if (stack.count > 0 && g_ResourceManager->IsPrefabIDValid(stack.prefabID))
			{
				m_Slots[index] = stack;
			}
		}
		ParseStationFields(stationObj);
	}

	void CraftingStation::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		FLEX_UNUSED(bSerializePrefabData);

		JSONObject stationObj = {};
		std::vector<JSONObject> slots;
		for (u32 i = 0; i < (u32)m_Slots.size(); ++i)
		{
			if (m_Slots[i].count > 0)
			{
				JSONObject slot = {};
				m_Slots[i].SerializeToJSON(slot);
				slot.fields.emplace_back("index", JSONValue((i32)i));
				slots.push_back(slot);
			}
		}
		stationObj.fields.emplace_back("slots", JSONValue(slots));
		SerializeStationFields(stationObj);
		parentObject.fields.emplace_back("station", JSONValue(stationObj));
	}

	// Chest

	Chest::Chest(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /* = false */) :
		CraftingStation(name, ChestSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate, SLOT_COUNT)
	{
	}

	PropertyCollection* Chest::BuildTypeUniquePropertyCollection()
	{
		return GameObject::BuildPropertyCollection();
	}

	void Chest::BuildBoxes(std::vector<std::pair<glm::vec3, glm::vec3>>& outWood, std::vector<std::pair<glm::vec3, glm::vec3>>& outTrim)
	{
		const glm::vec3 h = HalfExtents();
		const real lidHeight = 0.16f;
		const real bodyHalfHeight = h.y - lidHeight * 0.5f;
		// Body & a slightly wider lid
		outWood.push_back({ glm::vec3(0.0f, bodyHalfHeight, 0.0f), glm::vec3(h.x - 0.02f, bodyHalfHeight, h.z - 0.02f) });
		outWood.push_back({ glm::vec3(0.0f, h.y * 2.0f - lidHeight * 0.5f, 0.0f), glm::vec3(h.x, lidHeight * 0.5f, h.z) });
		// Iron bands, corners & a lock on the front (+z)
		for (real x : { -h.x + 0.12f, h.x - 0.12f })
		{
			outTrim.push_back({ glm::vec3(x, h.y, 0.0f), glm::vec3(0.035f, h.y + 0.01f, h.z + 0.01f) });
		}
		outTrim.push_back({ glm::vec3(0.0f, h.y * 2.0f - lidHeight - 0.02f, h.z + 0.01f), glm::vec3(0.06f, 0.07f, 0.02f) });
	}

	void Chest::CreateMeshes()
	{
		BoxList wood, trim;
		BuildBoxes(wood, trim);
		AddBoxesMesh(this, wood, woodcraft::GetOrCreateMaterial("Chest wood", glm::vec4(0.52f, 0.34f, 0.18f, 1.0f), 0.8f, false));
		AddBoxesMesh(this, trim, woodcraft::GetOrCreateMaterial("Chest iron", glm::vec4(0.22f, 0.22f, 0.23f, 1.0f), 0.5f, false));
	}

	// Furnace

	Furnace::Furnace(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /* = false */) :
		CraftingStation(name, FurnaceSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate, Slot::_COUNT)
	{
	}

	PropertyCollection* Furnace::BuildTypeUniquePropertyCollection()
	{
		return GameObject::BuildPropertyCollection();
	}

	void Furnace::BuildBoxes(std::vector<std::pair<glm::vec3, glm::vec3>>& outStone, std::vector<std::pair<glm::vec3, glm::vec3>>& outMouth)
	{
		const glm::vec3 h = HalfExtents();
		// A stone block with an opening at the front (+z) & a chimney
		const real mouthHalf = 0.2f;
		const real mouthBottom = 0.25f;
		const real mouthTop = 0.6f;
		// Back & sides
		outStone.push_back({ glm::vec3(0.0f, 0.4f, -0.1f), glm::vec3(h.x, 0.4f, h.z - 0.1f) });
		outStone.push_back({ glm::vec3(-(h.x + mouthHalf) * 0.5f, 0.4f, h.z - 0.1f), glm::vec3((h.x - mouthHalf) * 0.5f, 0.4f, 0.1f) });
		outStone.push_back({ glm::vec3((h.x + mouthHalf) * 0.5f, 0.4f, h.z - 0.1f), glm::vec3((h.x - mouthHalf) * 0.5f, 0.4f, 0.1f) });
		// Below & above the mouth
		outStone.push_back({ glm::vec3(0.0f, mouthBottom * 0.5f, h.z - 0.1f), glm::vec3(mouthHalf, mouthBottom * 0.5f, 0.1f) });
		outStone.push_back({ glm::vec3(0.0f, (mouthTop + 0.8f) * 0.5f, h.z - 0.1f), glm::vec3(mouthHalf, (0.8f - mouthTop) * 0.5f, 0.1f) });
		// Tapered top & chimney
		outStone.push_back({ glm::vec3(0.0f, 0.9f, 0.0f), glm::vec3(h.x - 0.08f, 0.1f, h.z - 0.08f) });
		outStone.push_back({ glm::vec3(0.0f, 1.1f, -0.1f), glm::vec3(0.14f, 0.1f, 0.14f) });
		// Back of the mouth (glows while burning)
		outMouth.push_back({ glm::vec3(0.0f, (mouthBottom + mouthTop) * 0.5f, h.z - 0.21f), glm::vec3(mouthHalf, (mouthTop - mouthBottom) * 0.5f, 0.01f) });
	}

	void Furnace::CreateMeshes()
	{
		BoxList stone, mouth;
		BuildBoxes(stone, mouth);
		AddBoxesMesh(this, stone, woodcraft::GetOrCreateMaterial("Furnace stone", glm::vec4(0.5f, 0.48f, 0.45f, 1.0f), 0.92f, false));
		m_Mouth = CreateChildObject(this, "Furnace mouth");
		AddBoxesMesh(m_Mouth, mouth, woodcraft::GetOrCreateMaterial("Furnace soot", glm::vec4(0.05f, 0.045f, 0.04f, 1.0f), 1.0f, false));
		m_Glow = CreateChildObject(this, "Furnace glow");
		AddBoxesMesh(m_Glow, mouth, GetOrCreateStaticGlowMaterial("Furnace fire", glm::vec4(1.0f, 0.5f, 0.12f, 1.0f)));
	}

	real Furnace::GetProgress() const
	{
		const crafting::Recipe* recipe = m_Slots[INPUT].count > 0 ? crafting::FindFurnaceRecipe(m_Slots[INPUT].prefabID) : nullptr;
		return recipe != nullptr ? glm::clamp(m_Progress / glm::max(recipe->time, 0.01f), 0.0f, 1.0f) : 0.0f;
	}

	void Furnace::Update()
	{
		GameObject::Update();
		if (IsPrefabTemplate())
		{
			return;
		}

		const sec dt = g_DeltaTime * s_SpeedMultiplier;
		GameObjectStack& input = m_Slots[INPUT];
		GameObjectStack& fuel = m_Slots[FUEL];
		GameObjectStack& output = m_Slots[OUTPUT];

		const crafting::Recipe* recipe = input.count > 0 ? crafting::FindFurnaceRecipe(input.prefabID) : nullptr;
		bool bOutputFits = false;
		if (recipe != nullptr && recipe->output.prefabID.IsValid())
		{
			const i32 maxStack = (i32)g_ResourceManager->GetMaxStackSize(recipe->output.prefabID);
			bOutputFits = output.count <= 0 || (output.prefabID == recipe->output.prefabID && output.count + recipe->output.count <= maxStack);
		}
		const bool bWorking = recipe != nullptr && bOutputFits && input.count >= recipe->inputs[0].count;

		// Lights the next fuel item when there's work to do
		if (m_FuelRemaining <= 0.0f && bWorking && fuel.count > 0)
		{
			const sec burnTime = crafting::FuelBurnTime(fuel.prefabID);
			if (burnTime > 0.0f)
			{
				m_FuelRemaining = m_FuelTotal = burnTime;
				if (--fuel.count <= 0)
				{
					fuel.Clear();
				}
			}
		}

		if (m_FuelRemaining > 0.0f)
		{
			m_FuelRemaining = glm::max(m_FuelRemaining - dt, 0.0f);
			if (bWorking)
			{
				m_Progress += dt;
				if (m_Progress >= recipe->time)
				{
					m_Progress = 0.0f;
					input.count -= recipe->inputs[0].count;
					if (input.count <= 0)
					{
						input.Clear();
					}
					if (output.count <= 0)
					{
						output = GameObjectStack(recipe->output.prefabID, 0);
					}
					output.count += recipe->output.count;
				}
			}
		}
		if (!bWorking)
		{
			m_Progress = 0.0f;
		}

		if (m_Glow != nullptr)
		{
			const bool bBurning = IsBurning();
			if (m_bGlowShown != bBurning || m_bMouthShown == bBurning)
			{
				m_bGlowShown = bBurning;
				m_bMouthShown = !bBurning;
				m_Glow->SetVisible(bBurning);
				m_Mouth->SetVisible(!bBurning);
			}
		}
	}

	void Furnace::ParseStationFields(const JSONObject& stationObj)
	{
		stationObj.TryGetFloat("fuel remaining", m_FuelRemaining);
		stationObj.TryGetFloat("fuel total", m_FuelTotal);
		stationObj.TryGetFloat("progress", m_Progress);
	}

	void Furnace::SerializeStationFields(JSONObject& stationObj) const
	{
		stationObj.fields.emplace_back("fuel remaining", JSONValue(m_FuelRemaining, 2));
		stationObj.fields.emplace_back("fuel total", JSONValue(m_FuelTotal, 2));
		stationObj.fields.emplace_back("progress", JSONValue(m_Progress, 2));
	}
} // namespace flex
