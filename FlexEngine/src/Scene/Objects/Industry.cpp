#include "stdafx.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>

#include <LinearMath/btIDebugDraw.h>

#include <glm/gtx/norm.hpp> // for distance2
#include <glm/gtx/quaternion.hpp> // for rotate
#include <glm/gtx/vector_angle.hpp> // for angle
#include <glm/ext/matrix_transform.hpp> // for rotate

#include <imgui/imgui_internal.h> // For PushItemFlag

IGNORE_WARNINGS_POP

#include "Scene/Objects/Industry.hpp"
#include "Editor.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "Inventory.hpp"
#include "Particles.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "PropertyCollection.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/Objects/Wires.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	const real Battery::flashDuration = 0.5f;

	Battery::Battery() :
		Battery(g_SceneManager->CurrentScene()->GetUniqueObjectName("Battery_", 2), InvalidGameObjectID)
	{
	}

	Battery::Battery(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, BatterySID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		m_bItemizable = true;
	}

	PropertyCollection* Battery::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("battery info");

		collection->childCollection->RegisterProperty("charge amount", FIELD_OFFSET_AND_TYPE(Battery, chargeAmount))
			.VersionAdded(2)
			.Precision(2)
			.DefaultValue(0.0f);

		return collection;
	}

	void Battery::Update()
	{
		PROFILE_AUTO("Battery Update");

		if (flashTimer > 0.0f)
		{
			flashTimer = glm::max(flashTimer - g_DeltaTime, 0.0f);
		}

		if (m_Mesh != nullptr && m_Mesh->GetSubmeshCount() >= 1)
		{
			MeshComponent* submesh0 = m_Mesh->GetSubMesh(0);
			Material* submesh0Mat = g_Renderer->GetMaterial(submesh0->GetMaterialID());

			real normalizedChargeAmount = chargeAmount / chargeCapacity;
			g_Renderer->AddRenderObjectUniformOverride(submesh0->renderID,
				&U_CHARGE_AMOUNT,
				{ normalizedChargeAmount });
			glm::vec3 constEmissive = submesh0Mat->constEmissive;
			real constEmissiveMultiplier = 1.0f;
			if (flashTimer > 0.0f)
			{
				const real flashIntensity = 1.0f;
				constEmissiveMultiplier += sin(flashTimer / flashDuration * TWO_PI * 2.0f) * (0.5f * flashIntensity);
			}
			g_Renderer->AddRenderObjectUniformOverride(submesh0->renderID,
				&U_CONST_EMISSIVE,
				{ constEmissive * constEmissiveMultiplier });
		}

		GameObject::Update();
	}

	void Battery::OnCharge(real amount)
	{
		real prevChargeAmount = chargeAmount;
		chargeAmount = glm::clamp(chargeAmount + amount, 0.0f, chargeCapacity);
		if (chargeAmount >= chargeCapacity && prevChargeAmount < chargeCapacity)
		{
			// Flash when fully charged
			flashTimer = flashDuration;
		}
	}

	GameObject* Battery::CopySelf(GameObject* parent, CopyFlags copyFlags, std::string* optionalName, const GameObjectID& optionalGameObjectID)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);
		Battery* newGameObject = new Battery(newObjectName, newGameObjectID);

		newGameObject->chargeAmount = chargeAmount;

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void Battery::OnItemize(GameObjectStack::UserData& outUserData)
	{
		GameObject::OnItemize(outUserData);

		outUserData.floatVal = chargeAmount;
	}

	void Battery::OnDeItemize(const GameObjectStack::UserData& userData)
	{
		GameObject::OnDeItemize(userData);

		chargeAmount = glm::clamp(userData.floatVal, 0.0f, chargeCapacity);
	}

	void Battery::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject batteryInfo;
		if (parentObject.TryGetObject("battery info", batteryInfo))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(batteryInfo, collection->childCollection);
		}
	}

	void Battery::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject batteryInfo = {};
		SerializeRegisteredProperties(batteryInfo, collection->childCollection, bSerializePrefabData);
		if (!batteryInfo.fields.empty())
		{
			parentObject.fields.emplace_back("battery info", JSONValue(batteryInfo));
		}
	}

	SolarPanel::SolarPanel(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, SolarPanelSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		m_bItemizable = true;
	}

	PropertyCollection* SolarPanel::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();
		return collection;
	}

	PropertyCollection* MineralDeposit::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("mineral deposit");

		collection->childCollection->RegisterProperty("mineral remaining", FIELD_OFFSET_AND_TYPE(MineralDeposit, m_MineralRemaining))
			.VersionAdded(6);

		return collection;
	}

	void SolarPanel::Update()
	{
		PROFILE_AUTO("SolarPanel Update");

		PluggablesSystem* pluggablesSystem = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES);
		std::vector< SocketData> const* sockets = pluggablesSystem->GetGameObjectSockets(ID);
		if (sockets != nullptr)
		{
			for (const SocketData& socketData : *sockets)
			{
				Socket* socket = (Socket*)socketData.socketID.Get();
				Socket* otherSocket = pluggablesSystem->GetSocketAtOtherEnd(socket);
				if (otherSocket != nullptr)
				{
					real chargeAmount = m_ChargeRate * m_Efficiency * g_DeltaTime;
					otherSocket->GetParent()->OnCharge(chargeAmount);
				}
			}
		}

		GameObject::Update();
	}

	HeadLamp::HeadLamp(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, HeadLampSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		m_bItemizable = true;
	}

	PropertyCollection* HeadLamp::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();
		return collection;
	}

	PowerPole::PowerPole(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, PowerPoleSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
	}

	PropertyCollection* PowerPole::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();
		return collection;
	}

	void PowerPole::OnCharge(real chargeAmount)
	{
		PluggablesSystem* pluggablesSystem = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES);
		std::vector<SocketData> const* sockets = pluggablesSystem->GetGameObjectSockets(ID);
		if (sockets != nullptr)
		{
			std::vector<Socket*> activeSockets;

			for (const SocketData& socketData : *sockets)
			{
				Socket* socket = (Socket*)socketData.socketID.Get();
				Socket* otherSocket = pluggablesSystem->GetSocketAtOtherEnd(socket);
				if (otherSocket != nullptr)
				{
					// TODO: Detect and avoid infinite loops
					otherSocket->GetParent()->OnCharge(chargeAmount);
				}
			}
		}
	}

	const char* MineralTypeToString(MineralType type)
	{
		return MineralTypeStrings[(i32)type];
	}

	MineralType MineralTypeFromString(const char* typeStr)
	{
		for (i32 i = 0; i < (i32)MineralType::_NONE; ++i)
		{
			if (StrCmpCaseInsensitive(MineralTypeStrings[i], typeStr) == 0)
			{
				return (MineralType)i;
			}
		}

		return MineralType::_NONE;
	}

	MineralDeposit::MineralDeposit(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, MineralDepositSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		m_bItemizable = true;
		m_bInteractable = true;
	}

	void MineralDeposit::Initialize()
	{
		GameObject::Initialize();

		UpdateMesh();
	}

	void MineralDeposit::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		bool bAnyPropertyChanged = false;

		if (m_Mesh != nullptr && m_Mesh->GetSubmeshCount() == 1)
		{
			ImGui::Text("Material");
			MeshComponent* meshComponent = m_Mesh->GetSubMesh(0);
			if (meshComponent->DrawImGui(0, bDrawingEditorObjects))
			{
				bAnyPropertyChanged = true;
			}
		}

		const char* mineralTypeStr = MineralTypeToString(m_Type);
		if (ImGui::BeginCombo("Mineral Type", mineralTypeStr))
		{
			for (u32 i = 0; i < (u32)MineralType::_NONE; ++i)
			{
				bool bSelected = (u32)m_Type == i;
				if (ImGui::Selectable(MineralTypeToString((MineralType)i), &bSelected))
				{
					m_Type = (MineralType)i;
					bAnyPropertyChanged = true;
				}
			}

			ImGui::EndCombo();
		}

		if (bAnyPropertyChanged && m_SourcePrefabID.IsValid())
		{
			g_ResourceManager->SetPrefabDirty(m_SourcePrefabID.m_PrefabID);
		}

		ImGui::Text("Mineral remaining: %u", m_MineralRemaining);
	}

	GameObject* MineralDeposit::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);
		MineralDeposit* newGameObject = new MineralDeposit(newObjectName, newGameObjectID);

		CopyGenericFields(newGameObject, parent, copyFlags);

		newGameObject->m_MineralRemaining = m_MineralRemaining;
		newGameObject->m_Type = m_Type;

		return newGameObject;
	}

	u32 MineralDeposit::GetMineralRemaining() const
	{
		return m_MineralRemaining;
	}

	MineralType MineralDeposit::GetMineralType() const
	{
		return m_Type;
	}

	PrefabID MineralDeposit::GetMineralPrefabID()
	{
		std::string prefabName = "mineral deposit " + std::string(MineralTypeToString(m_Type));
		return g_ResourceManager->GetPrefabID(prefabName.c_str());
	}

	u32 MineralDeposit::OnMine(real mineAmount)
	{
		CHECK_GE(mineAmount, 0.0f);

		u32 actualMineAmount = glm::clamp((u32)mineAmount, 0u, m_MineralRemaining);
		m_MineralRemaining -= actualMineAmount;
		UpdateMesh();
		return actualMineAmount;
	}

	void MineralDeposit::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject mineralDepositObj;
		if (parentObject.TryGetObject("mineral deposit", mineralDepositObj))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(mineralDepositObj, collection->childCollection);

			std::string typeStr = mineralDepositObj.GetString("type");
			m_Type = MineralTypeFromString(typeStr.c_str());
			if (m_Type == MineralType::_NONE)
			{
				char buff[33];
				ID.ToString(buff);
				PrintError("Failed to parse mineral's type: %s (for object: %s, ID: %s)\n", typeStr.c_str(), m_Name.c_str(), buff);
			}
		}
	}

	void MineralDeposit::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject mineralDepositObj = {};

		SerializeRegisteredProperties(mineralDepositObj, collection->childCollection, bSerializePrefabData);

		const char* typeStr = MineralTypeToString(m_Type);
		mineralDepositObj.fields.emplace_back("type", JSONValue(typeStr));

		parentObject.fields.emplace_back("mineral deposit", JSONValue(mineralDepositObj));
	}

	void MineralDeposit::UpdateMesh()
	{
		static const char* meshes[] = { "mineral-deposit-100.glb", "mineral-deposit-66.glb", "mineral-deposit-33.glb" };

		const char* newMeshFileName = "";
		if (m_MineralRemaining > 80u)
		{
			newMeshFileName = meshes[0];
		}
		else if (m_MineralRemaining > 50u)
		{
			newMeshFileName = meshes[1];
		}
		else if (m_MineralRemaining > 0u)
		{
			newMeshFileName = meshes[2];
		}

		if (m_Mesh != nullptr && m_Mesh->GetSubmeshCount() == 1)
		{
			std::string meshFileName = m_Mesh->GetFileName();
			if (strcmp(newMeshFileName, "") == 0)
			{
				// Mineral depleted, no need for a mesh anymore
				m_Mesh->Destroy();
			}
			else if (strcmp(meshFileName.c_str(), newMeshFileName))
			{
				MaterialID matID = m_Mesh->GetSubMesh(0)->GetMaterialID();
				m_Mesh->Destroy();
				m_Mesh->SetOwner(this);
				m_Mesh->LoadFromFile(MESH_DIRECTORY + std::string(newMeshFileName), matID);
			}
		}
		else
		{
			if (m_Mesh == nullptr)
			{
				SetMesh(new Mesh(this));
			}

			const char* mineralTypeStr = MineralTypeToString(m_Type);
			MaterialID matID;
			if (!g_Renderer->FindOrCreateMaterialByName("mineral " + std::string(mineralTypeStr), matID))
			{
				PrintWarn("No mineral material found for mineral %s, using placeholder\n", mineralTypeStr);
				matID = g_Renderer->GetPlaceholderMaterialID();
			}
			m_Mesh->Destroy();
			m_Mesh->SetOwner(this);
			m_Mesh->LoadFromFile(MESH_DIRECTORY + std::string(newMeshFileName), matID);
		}
	}

	Miner::Miner(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, MinerSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate),
		m_MineTimer(1.0f),
		m_MineCooldownTimer(0.2f)
	{
		CHECK_GT(m_MineCooldownTimer.duration, 0.0f);

		m_bInteractable = true;
		m_bItemizable = true;

		laserColour = glm::vec4(1.2f, 0.2f, 0.2f, 0.8f);
		m_LaserEmitterHeight = 5.0f;

		ComputeNewTargetPos();
	}

	PropertyCollection* Miner::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("miner");

		collection->childCollection->RegisterProperty("charge", FIELD_OFFSET_AND_TYPE(Miner, m_Charge))
			.VersionAdded(6)
			.DefaultValue(0.0f);
		collection->childCollection->RegisterProperty("max charge", FIELD_OFFSET_AND_TYPE(Miner, m_MaxCharge))
			.VersionAdded(6)
			.Range(0.0f, 100.0f);
		collection->childCollection->RegisterProperty("mine rate", FIELD_OFFSET_AND_TYPE(Miner, m_MineRate))
			.VersionAdded(6)
			.Range(0.0f, 100.0f);
		collection->childCollection->RegisterProperty("power draw", FIELD_OFFSET_AND_TYPE(Miner, m_PowerDraw))
			.VersionAdded(6)
			.Range(0.0f, 10.0f);

		return collection;
	}

	void Miner::Update()
	{
		PROFILE_AUTO("Miner Update");

		// TODO: Re-enable particles once the scene test crash in ParticleSystem::ExtinguishEmitter is fixed
		//ParticleManager* particleManager = GetSystem<ParticleManager>(SystemType::PARTICLE_MANAGER);

		if (!m_NearestMineralDepositID.IsValid())
		{
			// TODO: Only search every second or so rather than every frame
			std::vector<Pair<MineralDeposit*, real>> nearbyDeposits;
			if (g_SceneManager->CurrentScene()->GetObjectsInRadius<MineralDeposit>(m_Transform.GetWorldPosition(), m_MineRadius, MineralDepositSID, nearbyDeposits))
			{
				for (const Pair<MineralDeposit*, real>& mineralDepositPair : nearbyDeposits)
				{
					if (mineralDepositPair.first->GetMineralRemaining() > 0.0f)
					{
						m_NearestMineralDepositID = mineralDepositPair.first->ID;
						break;
					}
				}
			}
		}

		if (m_NearestMineralDepositID.IsValid())
		{
			MineralDeposit* nearestMineralDeposit = (MineralDeposit*)m_NearestMineralDepositID.Get();
			if (nearestMineralDeposit == nullptr)
			{
				m_NearestMineralDepositID = InvalidGameObjectID;
			}
			else
			{
				if (nearestMineralDeposit->GetMineralRemaining() == 0.0f)
				{
					m_NearestMineralDepositID = InvalidGameObjectID;

					//ParticleSystem* sparksParticleSystem = particleManager->GetOrCreateParticleSystem(SID_PAIR("laser sparks"));
					//sparksParticleSystem->ExtinguishEmitter(m_MiningSparksEmitterID);
					m_MiningSparksEmitterID = InvalidParticleEmitterID;
				}

				PrefabID minedMineralPrefabID = nearestMineralDeposit->GetMineralPrefabID();
				if (minedMineralPrefabID != InvalidPrefabID)
				{
					if (!m_MineCooldownTimer.IsComplete() && m_MineCooldownTimer.Update())
					{
						if (nearestMineralDeposit->GetMineralRemaining() == 0.0f)
						{
							m_NearestMineralDepositID = InvalidGameObjectID;
						}
						else
						{
							m_MineTimer.Restart();
							ComputeNewTargetPos();

							PhysicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld();

							glm::vec3 laserOriginWS = m_Transform.GetWorldPosition() + m_Transform.GetUp() * m_LaserEmitterHeight;
							glm::vec3 laserEndWS = laserOriginWS + m_LaserDirection + VEC3_UP * 0.2f;
							btCollisionWorld::ClosestRayResultCallback rayCallback(ToBtVec3(laserOriginWS), ToBtVec3(laserEndWS));
							physicsWorld->GetWorld()->rayTest(ToBtVec3(laserOriginWS), ToBtVec3(laserEndWS), rayCallback);

							if (rayCallback.hasHit())
							{
								m_MineTargetLocation = ToVec3(rayCallback.m_hitPointWorld);
							}
							else
							{
								m_MineTargetLocation = laserEndWS;
							}

							glm::vec3 sparkDir = glm::reflect(m_LaserDirection, VEC3_UP);
							glm::quat rot = glm::quatLookAt(glm::normalize(sparkDir), VEC3_UP) * glm::quat(glm::vec3(PI_DIV_TWO, 0.0f, 0.0f));
							glm::mat4 objectToWorld = glm::mat4(rot);
							objectToWorld[3] = glm::vec4(m_MineTargetLocation, 1.0f);
							//ParticleSystem* particleSystem = particleManager->GetOrCreateParticleSystem(SID_PAIR("laser sparks"));
							//m_MiningSparksEmitterID = particleSystem->SpawnEmitterInstance(objectToWorld);
						}
					}

					if (m_MineCooldownTimer.IsComplete() && m_Charge > m_PowerDraw && !IsInventoryFull())
					{
						if (m_MineTimer.Update())
						{
							//ParticleSystem* sparksParticleSystem = particleManager->GetOrCreateParticleSystem(SID_PAIR("laser sparks"));
							//sparksParticleSystem->ExtinguishEmitter(m_MiningSparksEmitterID);
							m_MiningSparksEmitterID = InvalidParticleEmitterID;

							m_MineCooldownTimer.Restart();

							u32 mineralMined = nearestMineralDeposit->OnMine(m_MineRate);
							m_Charge -= m_PowerDraw;

							if (mineralMined > 0)
							{
								GameObjectStack::UserData userData = {};
								u32 extraItems = AddToInventory(minedMineralPrefabID, mineralMined, userData);
								if (extraItems > 0)
								{
									glm::vec3 pos = m_Transform.GetWorldPosition() +
										m_Transform.GetUp() * 0.5f +
										m_Transform.GetForward() * 1.0f;
									g_SceneManager->CurrentScene()->CreateDroppedItem(minedMineralPrefabID, extraItems, pos, VEC3_ZERO);
								}

								glm::mat4 objectToWorld = glm::translate(MAT4_IDENTITY, m_MineTargetLocation);
								//ParticleSystem* dustParticleSystem = particleManager->GetOrCreateParticleSystem(SID_PAIR("mining dust"));
								//m_MiningDustEmitterID = dustParticleSystem->SpawnEmitterInstance(objectToWorld);
							}

							if (nearestMineralDeposit->GetMineralRemaining() == 0.0f)
							{
								m_MineTimer.Complete();
								m_NearestMineralDepositID = InvalidGameObjectID;

								//sparksParticleSystem->ExtinguishEmitter(m_MiningSparksEmitterID);
								m_MiningSparksEmitterID = InvalidParticleEmitterID;
							}
						}
					}
					else
					{
						if (m_MiningSparksEmitterID != InvalidParticleEmitterID)
						{
							//ParticleSystem* sparksParticleSystem = particleManager->GetOrCreateParticleSystem(SID_PAIR("laser sparks"));
							//sparksParticleSystem->ExtinguishEmitter(m_MiningSparksEmitterID);
							m_MiningSparksEmitterID = InvalidParticleEmitterID;
						}
					}
				}
			}
		}

		if (g_Editor->IsObjectSelected(ID))
		{
			DebugRenderer* debugRenderer = g_Renderer->GetDebugRenderer();
			debugRenderer->drawArc(ToBtVec3(m_Transform.GetWorldPosition() + glm::vec3(0.0f, 0.1f, 0.0f)), ToBtVec3(VEC3_UP), ToBtVec3(VEC3_RIGHT), m_MineRadius, m_MineRadius, 0.0f, TWO_PI - EPSILON, btVector3(0.9f, 0.5f, 0.5f), false);
		}

		if (m_MineCooldownTimer.remaining == 0.0f && m_MineTimer.remaining > 0.0f)
		{
			DebugRenderer* debugRenderer = g_Renderer->GetDebugRenderer();
			glm::vec3 laserOrigin = m_Transform.GetWorldPosition() + m_Transform.GetUp() * m_LaserEmitterHeight;
			debugRenderer->DrawLineWithAlpha(ToBtVec3(laserOrigin), ToBtVec3(laserOrigin + m_LaserDirection), ToBtVec4(laserColour));
		}

		GameObject::Update();
	}

	void Miner::OnCharge(real chargeAmount)
	{
		m_Charge = glm::clamp(m_Charge + chargeAmount, 0.0f, m_MaxCharge);
	}

	void Miner::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		bool bAnyPropertyChanged = false;

		//PropertyCollection* collection = GetPropertyCollection();
		//CHECK_NE(collection, nullptr);
		//bAnyPropertyChanged = collection->DrawImGuiObjects();

		ImGui::Text("Inventory");
		for (const GameObjectStack& stack : m_Inventory)
		{
			if (stack.prefabID.IsValid())
			{
				std::string prefabName = g_ResourceManager->GetPrefabTemplate(stack.prefabID)->GetName();
				ImGui::Text("%s : %i", prefabName.c_str(), stack.count);
			}
			else
			{
				ImGui::Text("Empty");
			}
		}

		if (ImGui::Button("Clear inventory"))
		{
			for (GameObjectStack& stack : m_Inventory)
			{
				stack.Clear();
			}
		}

		ImGui::SameLine();

		if (ImGui::Button("Fill charge"))
		{
			m_Charge = m_MaxCharge;
		}

		if (bAnyPropertyChanged && m_SourcePrefabID.IsValid())
		{
			g_ResourceManager->SetPrefabDirty(m_SourcePrefabID.m_PrefabID);
		}
	}

	GameObject* Miner::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);
		Miner* newGameObject = new Miner(newObjectName, newGameObjectID);

		CopyGenericFields(newGameObject, parent, copyFlags);

		newGameObject->m_Charge = m_Charge;
		newGameObject->m_MaxCharge = m_MaxCharge;
		newGameObject->m_MineRate = m_MineRate;
		newGameObject->m_PowerDraw = m_PowerDraw;
		newGameObject->m_MineRadius = m_MineRadius;
		newGameObject->m_Inventory = m_Inventory;

		return newGameObject;
	}

	void Miner::Destroy(bool bDetachFromParent /* = true */)
	{
		GameObject::Destroy(bDetachFromParent);

		ParticleManager* particleManager = GetSystem<ParticleManager>(SystemType::PARTICLE_MANAGER);
		if (particleManager != nullptr)
		{
			ParticleSystem* sparksParticleSystem = particleManager->GetParticleSystem(SID("laser sparks"));
			if (sparksParticleSystem != nullptr)
			{
				sparksParticleSystem->ExtinguishEmitter(m_MiningSparksEmitterID);
				m_MiningSparksEmitterID = InvalidParticleEmitterID;

				sparksParticleSystem->ExtinguishEmitter(m_MiningDustEmitterID);
				m_MiningDustEmitterID = InvalidParticleEmitterID;
			}
		}
	}

	i32 Miner::GetNextFreeInventorySlot()
	{
		for (i32 i = 0; i < (i32)m_Inventory.size(); ++i)
		{
			if (m_Inventory[i].count == 0)
			{
				return i;
			}
		}
		return -1;
	}

	void Miner::ComputeNewTargetPos()
	{
		real radius = 3.0f;
		m_LaserDirection = glm::vec3(
			RandomFloat(-radius, radius),
			-m_LaserEmitterHeight,
			RandomFloat(-radius, radius));
	}

	GameObjectStack* Miner::GetStackFromInventory(i32 slotIndex)
	{
		if (slotIndex >= 0 && slotIndex < m_Inventory.size())
		{
			return &m_Inventory[slotIndex];
		}
		return nullptr;
	}

	bool Miner::IsInventoryFull() const
	{
		for (const GameObjectStack& stack : m_Inventory)
		{
			if (!stack.prefabID.IsValid())
			{
				return false;
			}

			i32 maxStackSize = (i32)g_ResourceManager->GetMaxStackSize(stack.prefabID);
			if (stack.count < maxStackSize)
			{
				return false;
			}
		}

		return true;
	}

	u32 Miner::AddToInventory(const PrefabID& prefabID, i32 stackSize, const GameObjectStack::UserData& userData)
	{
		i32 maxStackSize = (i32)g_ResourceManager->GetMaxStackSize(prefabID);
		for (GameObjectStack& stack : m_Inventory)
		{
			if (!stack.prefabID.IsValid())
			{
				// Empty slot
				stack.prefabID = prefabID;
				stack.count = stackSize;
				stack.userData = userData;
				return 0;
			}
			else if (stack.prefabID == prefabID)
			{
				i32 deltaToAdd = glm::min(stackSize, maxStackSize - stack.count);
				stack.count += deltaToAdd;
				// TODO: Merge user data
				if (deltaToAdd == stackSize)
				{
					return 0;
				}

				stackSize -= deltaToAdd;
			}
		}
		return stackSize;
	}

	u32 Miner::AddToInventory(GameObjectStack* stack)
	{
		return AddToInventory(stack->prefabID, stack->count, stack->userData);
	}

	void Miner::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject minerObj;
		if (parentObject.TryGetObject("miner", minerObj))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(minerObj, collection->childCollection);

			std::vector<JSONObject> inventory;
			if (minerObj.TryGetObjectArray("inventory", inventory))
			{
				// TODO: Change function to take arrays once custom array class is in use (clang didn't like templatizing over std::array len)
				ParseInventory((GameObjectStack*)&m_Inventory[0], (u32)m_Inventory.size(), inventory);
			}
		}
	}

	void Miner::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject minerObj = {};

		SerializeRegisteredProperties(minerObj, collection->childCollection, bSerializePrefabData);

		std::vector<JSONObject> inventory;
		if (SerializeInventory((GameObjectStack*)&m_Inventory[0], (u32)m_Inventory.size(), inventory))
		{
			if (!inventory.empty())
			{
				minerObj.fields.emplace_back("inventory", JSONValue(inventory));
			}
		}

		parentObject.fields.emplace_back("miner", JSONValue(minerObj));
	}

	Speaker::Speaker(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, SpeakerSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		m_bInteractable = true;
	}

	PropertyCollection* Speaker::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("speaker");

		collection->childCollection->RegisterProperty("playing", FIELD_OFFSET_AND_TYPE(Speaker, m_bPlaying))
			.VersionAdded(6);
		collection->childCollection->RegisterProperty("audio source path", FIELD_OFFSET_AND_TYPE(Speaker, m_AudioSourceFileName))
			.VersionAdded(6);

		return collection;
	}

	void Speaker::Initialize()
	{
		GameObject::Initialize();

		if (!m_AudioSourceFileName.empty())
		{
			m_SourceID = g_ResourceManager->GetOrLoadAudioSourceID(Hash(m_AudioSourceFileName.c_str()), false);

			if (m_SourceID != InvalidAudioSourceID)
			{
				if (m_bPlaying)
				{
					AudioManager::SetSourceLooping(m_SourceID, true);
					AudioManager::PlaySourceAtPosWS(m_SourceID, m_Transform.GetWorldPosition());
				}
			}
		}
	}

	void Speaker::Destroy(bool bDetachFromParent /* = true */)
	{
		GameObject::Destroy(bDetachFromParent);

		if (m_SourceID != InvalidAudioSourceID)
		{
			g_ResourceManager->DestroyAudioSource(m_SourceID);
			m_SourceID = InvalidAudioSourceID;
		}
	}

	void Speaker::Update()
	{
		PROFILE_AUTO("Speaker Update");

		if (m_SourceID != InvalidAudioSourceID && m_bPlaying)
		{
			if (AudioManager::IsSourcePlaying(m_SourceID))
			{
				AudioManager::SetSourcePositionWS(m_SourceID, m_Transform.GetWorldPosition());
			}
			else
			{
				AudioManager::SetSourceLooping(m_SourceID, true);
				AudioManager::PlaySourceAtPosWS(m_SourceID, m_Transform.GetWorldPosition());
			}
		}

		GameObject::Update();
	}

	void Speaker::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		StringID sourceSID = Hash(m_AudioSourceFileName.c_str());
		if (g_ResourceManager->DrawAudioSourceIDImGui("Audio source", sourceSID))
		{
			if (sourceSID != InvalidStringID)
			{
				AudioSourceID newSourceID = g_ResourceManager->GetAudioSourceID(sourceSID);
				if (newSourceID == InvalidAudioSourceID)
				{
					newSourceID = g_ResourceManager->GetOrLoadAudioSourceID(sourceSID, false);
				}

				if (newSourceID == InvalidAudioSourceID)
				{
					PrintError("Speaker set to use invalid audio source\n");
					return;
				}

				if (m_SourceID != InvalidAudioSourceID)
				{
					AudioManager::StopSource(m_SourceID);
				}

				if (AudioManager::GetSource(newSourceID)->b2D)
				{
					PrintWarn("2D sound selected for speaker. Audio will not be localized.\n");
				}

				AudioManager::SetSourceLooping(newSourceID, true);

				if (m_bPlaying)
				{
					AudioManager::PlaySourceAtPosWS(m_SourceID, m_Transform.GetWorldPosition());
				}

				m_SourceID = newSourceID;
				m_AudioSourceFileName = AudioManager::GetSourceFileName(newSourceID);
			}
			else
			{
				if (m_SourceID != InvalidAudioSourceID)
				{
					AudioManager::StopSource(m_SourceID);
				}

				m_SourceID = InvalidAudioSourceID;
				m_AudioSourceFileName.clear();
			}
		}

		if (ImGui::Checkbox("Playing", &m_bPlaying))
		{
			if (m_SourceID != InvalidAudioSourceID)
			{
				if (m_bPlaying)
				{
					if (!AudioManager::PlaySourceAtPosWS(m_SourceID, m_Transform.GetWorldPosition()))
					{
						m_SourceID = InvalidAudioSourceID;
					}
				}
				else
				{
					if (!AudioManager::StopSource(m_SourceID))
					{
						m_SourceID = InvalidAudioSourceID;
					}
				}
			}
		}
	}

	GameObject* Speaker::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);
		Speaker* newGameObject = new Speaker(newObjectName, newGameObjectID);

		CopyGenericFields(newGameObject, parent, copyFlags);

		newGameObject->m_AudioSourceFileName = m_AudioSourceFileName;
		newGameObject->m_bPlaying = m_bPlaying;
		// Leave m_SourceID as invalid so the new object creates its own audio source

		return newGameObject;
	}

	void Speaker::TogglePlaying()
	{
		m_bPlaying = !m_bPlaying;

		if (m_SourceID != InvalidAudioSourceID)
		{
			if (m_bPlaying)
			{
				if (!AudioManager::PlaySourceAtPosWS(m_SourceID, m_Transform.GetWorldPosition()))
				{
					m_SourceID = InvalidAudioSourceID;
				}
			}
			else
			{
				if (!AudioManager::StopSource(m_SourceID))
				{
					m_SourceID = InvalidAudioSourceID;
				}
			}
		}
	}

	void Speaker::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject speakerObj;
		if (parentObject.TryGetObject("speaker", speakerObj))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(speakerObj, collection->childCollection);
		}
	}

	void Speaker::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject speakerObj = {};
		SerializeRegisteredProperties(speakerObj, collection->childCollection, bSerializePrefabData);
		if (!speakerObj.fields.empty())
		{
			parentObject.fields.emplace_back("speaker", JSONValue(speakerObj));
		}
	}

	//
	// PickAxe
	//
	PickAxe::PickAxe(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, PickAxeSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		m_bInteractable = true;
		m_bItemizable = true;
	}

	PropertyCollection* PickAxe::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("pickaxe");

		collection->childCollection->RegisterProperty("usage remaining", FIELD_OFFSET_AND_TYPE(PickAxe, m_UsageRemaining))
			.VersionAdded(6);

		return collection;
	}

	void PickAxe::Update()
	{
		PROFILE_AUTO("PickAxe Update");

		GameObject::Update();
	}

	void PickAxe::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		ImGui::Text("Usage remaining: %.3f", m_UsageRemaining);

		const char* mineralTypeStr = MineralTypeToString(m_Material);
		if (ImGui::BeginCombo("Material", mineralTypeStr))
		{
			for (u32 i = 0; i < (u32)MineralType::_NONE; ++i)
			{
				bool bSelected = (u32)m_Material == i;
				if (ImGui::Selectable(MineralTypeToString((MineralType)i), &bSelected))
				{
					m_Material = (MineralType)i;
				}
			}

			ImGui::EndCombo();
		}
	}

	GameObject* PickAxe::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);
		PickAxe* newGameObject = new PickAxe(newObjectName, newGameObjectID);

		CopyGenericFields(newGameObject, parent, copyFlags);

		newGameObject->m_UsageRemaining = m_UsageRemaining;
		newGameObject->m_Material = m_Material;
		// Leave m_SourceID as invalid so the new object creates its own audio source

		return newGameObject;
	}

	void PickAxe::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject pickaxeObj;
		if (parentObject.TryGetObject("pickaxe", pickaxeObj))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(pickaxeObj, collection->childCollection);

			std::string materialStr = pickaxeObj.GetString("material");
			m_Material = MineralTypeFromString(materialStr.c_str());
			if (m_Material == MineralType::_NONE)
			{
				PrintError("Failed to parse material from pickaxe: \"%s\", falling back to IRON", materialStr.c_str());
				m_Material = MineralType::IRON;
			}
		}
	}

	void PickAxe::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject pickaxeObj = {};
		SerializeRegisteredProperties(pickaxeObj, collection->childCollection, bSerializePrefabData);

		if (m_Material == MineralType::_NONE)
		{
			PrintError("Can not save pickaxe with no material set! Falling back to IRON\n");
			m_Material = MineralType::IRON;
		}
		pickaxeObj.fields.emplace_back("material", JSONValue(MineralTypeToString(m_Material)));

		parentObject.fields.emplace_back("pickaxe", JSONValue(pickaxeObj));
	}

	real PickAxe::GetMineSpeed(MineralType material)
	{
		switch (material)
		{
			// Elements
		case MineralType::IRON:
		case MineralType::SILICON:
		case MineralType::ALUMINUM:
			return 1.0f;
		case MineralType::TIN:
			return 0.5f;

			// Minerals
		case MineralType::STONE:
		case MineralType::QUARTZ:
		case MineralType::OLIVINE:
			return 0.5f;
		}
		ENSURE_NO_ENTRY(); // Unhandled material for mining
		return -1.0f;
	}
} // namespace flex
