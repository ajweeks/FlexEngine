#include "stdafx.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <LinearMath/btIDebugDraw.h>
#include <LinearMath/btTransform.h>

#include <glm/gtx/norm.hpp> // for distance2
#include <glm/gtx/quaternion.hpp> // for rotate
#include <glm/gtx/vector_angle.hpp> // for angle
#include <glm/ext/matrix_transform.hpp> // for rotate

#include <imgui/imgui_internal.h> // For PushItemFlag

IGNORE_WARNINGS_POP

#include "Scene/GameObject.hpp"

#include "Editor.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "Inventory.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Platform/Platform.hpp"
#include "Player.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/Objects/Carts.hpp"
#include "Scene/Objects/Industry.hpp"
#include "Scene/Objects/Props.hpp"
#include "Scene/Objects/Wires.hpp"
#include "Scene/SceneManager.hpp"
#include "StringBuilder.hpp"

namespace flex
{
	const char* GameObject::s_DefaultNewGameObjectName = "New_Game_Object_00";

	ChildIndex InvalidChildIndex = ChildIndex({});

	GameObjectStack::GameObjectStack() :
		prefabID(InvalidPrefabID),
		count(0),
		userData({})
	{
	}

	GameObjectStack::GameObjectStack(const PrefabID& prefabID, i32 count) :
		prefabID(prefabID),
		count(count),
		userData({})
	{
	}

	void GameObjectStack::Clear()
	{
		prefabID = InvalidPrefabID;
		count = 0;
		userData.floatVal = 0.0f;
	}

	void GameObjectStack::SerializeToJSON(JSONObject& parentObject) const
	{
		parentObject.fields.emplace_back("prefab id", JSONValue(prefabID));
		parentObject.fields.emplace_back("count", JSONValue(count));
		if (userData.floatVal != 0.0f)
		{
			parentObject.fields.emplace_back("user data", JSONValue(userData.floatVal));
		}
	}

	void GameObjectStack::ParseFromJSON(const JSONObject& parentObject)
	{
		prefabID = parentObject.GetPrefabID("prefab id");
		count = parentObject.GetInt("count");
		parentObject.TryGetFloat("user data", userData.floatVal);
	}

	void ActiveItem::Create(PrefabID sourcePrefabID)
	{
		m_SourcePrefabID = sourcePrefabID;

		GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(sourcePrefabID);
		if (prefabTemplate == nullptr)
		{
			// e.g. an inventory loaded from a save which refers to a prefab that no longer exists (shown as a cube)
			std::string prefabIDStr = sourcePrefabID.ToString();
			PrintError("Active item refers to an unknown prefab (%s)\n", prefabIDStr.c_str());
		}
		m_TypeID = prefabTemplate != nullptr ? prefabTemplate->GetTypeID() : InvalidID;

		m_ItemProxyObject = GameObject::CreateObjectOfType(BaseObjectSID, "Active item");
		m_ItemProxyObject->SetSerializable(false);
		m_ItemProxyObject->SetVisibleInSceneExplorer(false);
		// Mineral deposit meshes are world-sized (~20m across), shrink them down to be hand-held
		m_ItemProxyObject->GetTransform()->Scale(m_TypeID == MineralDepositSID ? 0.04f : 0.5f);
		const bool bScaleToHand = m_TypeID != MineralDepositSID && !IsHandToolType(m_TypeID);

		if (prefabTemplate != nullptr && prefabTemplate->GetMesh() != nullptr)
		{
			prefabTemplate->GetMesh()->CloneSelf(m_ItemProxyObject, true);
		}
		else if (BuildHandToolMesh(m_ItemProxyObject, m_TypeID))
		{
			// Procedural tool mesh
		}
		else
		{
			// Prefab template has no mesh; attempt type-specific fallback
			Mesh* mesh = m_ItemProxyObject->SetMesh(new Mesh(m_ItemProxyObject));
			bool bLoadedFallback = false;

			if (m_TypeID == MineralDepositSID)
			{
				MineralDeposit* mineralDeposit = static_cast<MineralDeposit*>(prefabTemplate);
				const char* mineralTypeStr = MineralTypeToString(mineralDeposit->GetMineralType());
				MaterialID matID;
				if (g_Renderer->FindOrCreateMaterialByName("mineral " + std::string(mineralTypeStr), matID))
				{
					bLoadedFallback = mesh->LoadFromFile(MESH_DIRECTORY "mineral-deposit-100.glb", matID);
				}
			}

			if (!bLoadedFallback)
			{
				mesh->LoadPrefabShape(PrefabShape::CUBE, g_Renderer->GetPlaceholderMaterialID());
			}
		}

		Mesh* proxyMesh = m_ItemProxyObject->GetMesh();
		if (bScaleToHand && proxyMesh != nullptr)
		{
			// Other items are shown at a size which fits in the hand (whatever their size in the world)
			const glm::vec3 size = proxyMesh->m_MaxPoint - proxyMesh->m_MinPoint;
			const real largest = glm::max(glm::max(size.x, size.y), size.z);
			if (largest > 1e-3f && largest < 1e6f)
			{
				const real heldSize = 0.38f;
				m_ItemProxyObject->GetTransform()->SetLocalScale(glm::vec3(heldSize / largest));
			}
		}

		g_SceneManager->CurrentScene()->AddRootObject(m_ItemProxyObject);
		m_ItemProxyID = m_ItemProxyObject->ID;
	}

	void ActiveItem::Clear()
	{
		if (m_ItemProxyObject != nullptr)
		{
			g_SceneManager->CurrentScene()->RemoveObject(m_ItemProxyID, true);
			m_ItemProxyObject = nullptr;
			m_ItemProxyID = InvalidGameObjectID;
		}
		m_TypeID = InvalidID;
		m_SourcePrefabID = InvalidPrefabID;
	}

	//
	// GameObject
	//

	GameObject::GameObject(const std::string& name, StringID typeID) :
		 GameObject(name, typeID, InvalidGameObjectID, InvalidPrefabIDPair, false)
	{
	}

	GameObject::GameObject(const std::string& name, StringID typeID, const GameObjectID& gameObjectID, const PrefabIDPair& sourcePrefabID, bool bIsPrefabTemplate) :
		m_Name(name),
		m_TypeID(typeID),
		m_SourcePrefabID(sourcePrefabID),
		m_bSerializable(true),
		m_bVisible(true),
		m_bVisibleInSceneExplorer(true),
		m_bStatic(false),
		m_bTrigger(false),
		m_bInteractable(false),
		m_bCastsShadow(true),
		m_bIsTemplate(bIsPrefabTemplate),
		m_bOwnedInternally(false),
		m_bUniformScale(false),
		m_bItemizable(false)
	{
		if (gameObjectID.IsValid())
		{
			ID = gameObjectID;
		}
		else
		{
			ID = Platform::GenerateGUID();
		}

		m_Transform.SetAsIdentity();
		m_Transform.SetGameObject(this);
	}

	GameObject* GameObject::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		if (parent != nullptr && (parent->GetTypeID() == DirectionalLightSID))
		{
			PrintError("Attempted to copy directional light\n");
			return nullptr;
		}

		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);
		const char* typeStr = BaseScene::GameObjectTypeIDToString(m_TypeID);
		// TODO:
		bool bPrefabTemplate = (copyFlags & CopyFlags::COPYING_TO_PREFAB);
		GameObject* newGameObject = CreateObjectOfType(m_TypeID, newObjectName, newGameObjectID, typeStr, m_SourcePrefabID, bPrefabTemplate);

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	GameObject* GameObject::CreateObjectFromPrefabTemplate(
		const PrefabID& prefabID,
		const GameObjectID& gameObjectID,
		std::string* optionalObjectName /* = nullptr */,
		GameObject* parent /* = nullptr */,
		Transform* optionalTransform /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */)
	{
		GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(prefabID);
		if (prefabTemplate == nullptr)
		{
			std::string prefabTemplateIDStr = prefabID.ToString();
			PrintError("Failed to create game object from prefab template with ID %s\n", prefabTemplateIDStr.c_str());
			return nullptr;
		}
		GameObject* prefabInstance = prefabTemplate->CopySelf(parent, copyFlags, optionalObjectName, gameObjectID);

		CHECK(prefabInstance->m_SourcePrefabID.m_PrefabID.IsValid());
		CHECK(prefabInstance->m_SourcePrefabID.m_SubGameObjectID.IsValid());

		if (optionalTransform != nullptr)
		{
			prefabInstance->m_Transform.CloneFrom(*optionalTransform);
		}

		return prefabInstance;
	}

	GameObject* GameObject::CreateObjectOfType(
		StringID gameObjectTypeID,
		const std::string& objectName,
		const GameObjectID& gameObjectID /* = InvalidGameObjectID */,
		const char* optionalTypeStr /* = nullptr */,
		const PrefabIDPair& sourcePrefabIDPair /* = InvalidPrefabIDPair */,
		bool bIsPrefabTemplate /* = false */)
	{
		if (const GameObjectTypeInfo* typeInfo = FindGameObjectType(gameObjectTypeID))
		{
			return typeInfo->construct(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		}

		if (gameObjectTypeID == PlayerSID)
		{
			PrintError("Player was serialized to scene file!\n");
			return nullptr;
		}

		if (gameObjectTypeID != BaseObjectSID)
		{
			PrintWarn("Unhandled game object type in CreateObjectOfType (Object name: %s, type: %s).\n"
				"Creating placeholder base object.\n",
				objectName.c_str(), (optionalTypeStr == nullptr ? "unknown" : optionalTypeStr));
		}

		return new GameObject(objectName, gameObjectTypeID, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
	}

	void GameObject::Initialize()
	{
		if (m_RigidBody != nullptr)
		{
			if (!m_CollisionShape)
			{
				PrintError("Game object contains rigid body but no collision shape! Must call SetCollisionShape before Initialize\n");
			}
			else
			{
				m_RigidBody->Initialize(m_CollisionShape, &m_Transform);
			}
		}

		for (GameObject* child : m_Children)
		{
			child->Initialize();
		}
	}

	void GameObject::PostInitialize()
	{
		RigidBody* rb = GetRigidBody();

		if (m_Mesh != nullptr)
		{
			m_Mesh->PostInitialize();
		}

		if (rb != nullptr)
		{
			rb->GetRigidBodyInternal()->setUserPointer(this);
		}

		for (GameObject* child : m_Children)
		{
			child->PostInitialize();
		}

		GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->CacheGameObjectSockets(ID);
	}

	void GameObject::Destroy(bool bDetachFromParent /* = true */)
	{
		if (bDetachFromParent)
		{
			DetachFromParent();
		}

		GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->RemoveGameObjectSockets(ID);

		// Handle m_Children being modified during loop (from call to DetachFromParent)
		for (GameObject* child : m_Children)
		{
			child->Destroy(false);
			delete child;
		}
		m_Children.clear();

		if (m_Mesh != nullptr)
		{
			m_Mesh->Destroy();
			delete m_Mesh;
			m_Mesh = nullptr;
		}

		if (m_RigidBody != nullptr)
		{
			m_RigidBody->Destroy();
			delete m_RigidBody;
			m_RigidBody = nullptr;
		}

		if (m_CollisionShape != nullptr)
		{
			delete m_CollisionShape;
			m_CollisionShape = nullptr;
		}

		if (!m_bIsTemplate)
		{
			if (g_Editor->IsObjectSelected(ID))
			{
				g_Editor->DeselectObject(ID);
			}

			// ID will not be valid on objects that are being saved as a prefab template
			if (g_SceneManager->HasSceneLoaded() && ID.IsValid() && !m_bOwnedInternally)
			{
				g_SceneManager->CurrentScene()->UnregisterGameObject(ID, /* bAssertSuccess: */ true);
			}
		}
	}

	void GameObject::Update()
	{
		PROFILE_AUTO("GameObject Update");

		if (m_NearbyInteractableID.IsValid())
		{
			DebugRenderer* debugRenderer = g_Renderer->GetDebugRenderer();
			btVector3 pos = ToBtVec3(m_NearbyInteractableID.Get()->GetTransform()->GetWorldPosition());
			//real pulse = sin(g_SecElapsedSinceProgramStart * 8.0f);
			//debugRenderer->drawSphere(pos, pulse * 0.1f + 0.35f, btVector3(0.1f, pulse * 0.5f + 0.7f, 0.1f));
			debugRenderer->drawLine(pos + btVector3(-1, 0.1f, 0), pos + btVector3(1, 0.1f, 0), btVector3(0.1f, 0.95f, 0.1f));
			debugRenderer->drawLine(pos + btVector3(0, 0.1f, -1), pos + btVector3(0, 0.1f, 1), btVector3(0.1f, 0.95f, 0.1f));
		}
		else if (m_bInteractable)
		{
			DebugRenderer* debugRenderer = g_Renderer->GetDebugRenderer();
			btVector3 pos = ToBtVec3(m_Transform.GetWorldPosition());
			debugRenderer->drawLine(pos + btVector3(-1, 0.1f, 0), pos + btVector3(1, 0.1f, 0), btVector3(0.95f, 0.95f, 0.1f));
			debugRenderer->drawLine(pos + btVector3(0, 0.1f, -1), pos + btVector3(0, 0.1f, 1), btVector3(0.95f, 0.95f, 0.1f));
		}

		for (GameObject* child : m_Children)
		{
			child->Update();
		}
	}

	void GameObject::FixedUpdate()
	{
	}

	void GameObject::Render()
	{
	}

	void GameObject::ChangeAllIDs()
	{
		GameObjectID oldID = ID;
		GameObjectID newID = (GameObjectID)Platform::GenerateGUID();
		OnIDChanged(oldID, newID);

		ID = newID;

		if (m_bIsTemplate)
		{
			m_SourcePrefabID.m_SubGameObjectID = newID;
		}

		for (GameObject* child : m_Children)
		{
			child->ChangeAllIDs();
		}
	}

	void GameObject::SetSourcePrefabID(PrefabID sourcePrefabID)
	{
		m_SourcePrefabID.m_PrefabID = sourcePrefabID;

		for (GameObject* child : m_Children)
		{
			child->SetSourcePrefabID(sourcePrefabID);
		}
	}

	void GameObject::OverwritePrefabIDs(GameObject* previousGameObject, GameObject* newGameObject)
	{
		if (previousGameObject->m_Name == newGameObject->m_Name)
		{
			newGameObject->ID = previousGameObject->ID;
		}

		// Children may have been added, removed, or reordered, so match by name rather than index.
		// Each previous child is matched at most once to avoid duplicate IDs.
		std::vector<bool> previousChildMatched(previousGameObject->m_Children.size(), false);
		for (GameObject* newChild : newGameObject->m_Children)
		{
			for (u32 i = 0; i < (u32)previousGameObject->m_Children.size(); ++i)
			{
				GameObject* previousChild = previousGameObject->m_Children[i];
				if (!previousChildMatched[i] && previousChild->m_Name == newChild->m_Name)
				{
					previousChildMatched[i] = true;
					OverwritePrefabIDs(previousChild, newChild);
					break;
				}
			}
		}
	}

	void GameObject::RemoveRigidBody()
	{
		if (m_RigidBody != nullptr)
		{
			btDiscreteDynamicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
			physicsWorld->removeRigidBody(m_RigidBody->GetRigidBodyInternal());
			SetRigidBody(nullptr);

			if (m_CollisionShape)
			{
				delete m_CollisionShape;
				m_CollisionShape = nullptr;
			}
		}
	}

	void GameObject::Filter(std::function<bool(GameObject*)> callback, std::vector<GameObject*>& outList)
	{
		if (callback(this))
		{
			outList.emplace_back(this);
		}

		for (GameObject* child : m_Children)
		{
			child->Filter(callback, outList);
		}
	}

	void GameObject::FilterID(std::function<bool(GameObject*)> callback, std::vector<GameObjectID>& outList)
	{
		if (callback(this))
		{
			outList.emplace_back(ID);
		}

		for (GameObject* child : m_Children)
		{
			child->FilterID(callback, outList);
		}
	}

	u32 GameObject::FilterCount(std::function<bool(GameObject*)> callback)
	{
		return FilterCountInternal(callback, 0);
	}

	u32 GameObject::FilterCountInternal(std::function<bool(GameObject*)> callback, u32 count)
	{
		if (callback(this))
		{
			++count;
		}

		for (GameObject* child : m_Children)
		{
			count += child->FilterCountInternal(callback, count);
		}

		return count;
	}

	GameObject* GameObject::FilterFirst(std::function<bool(GameObject*)> callback)
	{
		if (callback(this))
		{
			return this;
		}

		for (GameObject* child : m_Children)
		{
			GameObject* result = child->FilterFirst(callback);
			if (result != nullptr)
			{
				return result;
			}
		}

		return nullptr;
	}
	void GameObject::FixupPrefabTemplateIDs(GameObject* newGameObject)
	{
		FLEX_UNUSED(newGameObject);
	}

	void GameObject::OnIDChanged(const GameObjectID& oldID, const GameObjectID& newID)
	{
		for (GameObject* child : m_Children)
		{
			child->OnIDChanged(oldID, newID);
		}
	}

	void GameObject::OnItemize(GameObjectStack::UserData& outUserData)
	{
		FLEX_UNUSED(outUserData);

		// Check if anything is plugged into this object, and if so unplug it
		PluggablesSystem* pluggablesSystem = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES);
		std::vector<SocketData> const* sockets = pluggablesSystem->GetGameObjectSockets(ID);
		if (sockets != nullptr)
		{
			for (const SocketData& data : *sockets)
			{
				const Socket* socket = (Socket*)data.socketID.Get();
				if (socket->connectedPlugID.IsValid())
				{
					GameObject* gameObject = socket->connectedPlugID.Get();
					CHECK_EQ(gameObject->GetTypeID(), WirePlugSID);
					Wire* wire = (Wire*)((WirePlug*)gameObject)->wireID.Get();
					pluggablesSystem->UnregisterWire(wire);
					GameObject* plug0 = wire->plug0ID.Get();
					GameObject* plug1 = wire->plug1ID.Get();
					if (plug0) g_SceneManager->CurrentScene()->RemoveObject(plug0, true);
					if (plug1) g_SceneManager->CurrentScene()->RemoveObject(plug1, true);
					g_SceneManager->CurrentScene()->RemoveObject(wire, true);
				}
			}
		}
	}

	void GameObject::OnDeItemize(const GameObjectStack::UserData& userData)
	{
		FLEX_UNUSED(userData);
	}

	StringID GameObject::GetTypeID() const
	{
		return m_TypeID;
	}

	void GameObject::AddSelfAndChildrenToVec(std::vector<GameObject*>& vec) const
	{
		if (!Contains<GameObject*>(vec, const_cast<GameObject*>(this)))
		{
			vec.push_back(const_cast<GameObject*>(this));
		}

		for (GameObject* child : m_Children)
		{
			if (!Contains(vec, child))
			{
				vec.push_back(child);
			}

			child->AddSelfAndChildrenToVec(vec);
		}
	}

	void GameObject::RemoveSelfAndChildrenFromVec(std::vector<GameObject*>& vec) const
	{
		auto iter = FindIter(vec, const_cast<GameObject*>(this));
		if (iter != vec.end())
		{
			vec.erase(iter);
		}

		for (GameObject* child : m_Children)
		{
			auto childIter = FindIter(vec, child);
			if (childIter != vec.end())
			{
				vec.erase(childIter);
			}

			child->RemoveSelfAndChildrenFromVec(vec);
		}
	}
	void GameObject::AddSelfIDAndChildrenToVec(std::vector<GameObjectID>& vec) const
	{
		if (!Contains(vec, ID))
		{
			vec.push_back(ID);
		}

		for (GameObject* child : m_Children)
		{
			if (!Contains(vec, child->ID))
			{
				vec.push_back(child->ID);
			}

			child->AddSelfIDAndChildrenToVec(vec);
		}
	}

	void GameObject::RemoveSelfIDAndChildrenFromVec(std::vector<GameObjectID>& vec) const
	{
		auto iter = FindIter(vec, ID);
		if (iter != vec.end())
		{
			vec.erase(iter);
		}

		for (GameObject* child : m_Children)
		{
			auto childIter = FindIter(vec, child->ID);
			if (childIter != vec.end())
			{
				vec.erase(childIter);
			}

			child->RemoveSelfIDAndChildrenFromVec(vec);
		}
	}

	bool GameObject::SelfOrChildIsSelected() const
	{
		if (g_Editor->IsObjectSelected(ID))
		{
			return true;
		}

		for (GameObject* child : m_Children)
		{
			if (child->SelfOrChildIsSelected())
			{
				return true;
			}
		}

		return false;
	}

	bool GameObject::IsWearable() const
	{
		return m_TypeID == HeadLampSID;
	}

	void GameObject::RegisterPropertyCollections()
	{
		PropertyCollectionManager* propertyCollectionManager = GetSystem<PropertyCollectionManager>(SystemType::PROPERTY_COLLECTION_MANAGER);

		propertyCollectionManager->RegisterType(BaseObjectSID, GameObject::BuildPropertyCollection()); // TODO?
		propertyCollectionManager->RegisterType(BaseCartSID, BaseCart::BuildTypeUniquePropertyCollection()); // TODO?
		propertyCollectionManager->RegisterType(PlayerSID, Player::BuildTypeUniquePropertyCollection());

		u32 typeCount;
		const GameObjectTypeInfo* types = GetGameObjectTypes(typeCount);
		for (u32 i = 0; i < typeCount; ++i)
		{
			propertyCollectionManager->RegisterType(types[i].typeID, types[i].buildPropertyCollection());
		}
	}

	void GameObject::UpdateHeldItem(GameObjectID heldItemID)
	{
		GameObject* gameObject = heldItemID.Get();
		if (!gameObject) return;

		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		CHECK(player->GetHeldItem(Hand::LEFT) == heldItemID ||
			player->GetHeldItem(Hand::RIGHT) == heldItemID);

		switch (gameObject->GetTypeID())
		{
		case WirePlugSID:
		{
			WirePlug* wirePlug = (WirePlug*)gameObject;
			GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->UpdatePlugTransform(wirePlug);
		} break;
		default:
		{
			Transform* playerTransform = player->GetTransform();

			glm::vec3 playerRight = playerTransform->GetRight();
			const bool bLeftHand = player->GetHeldItem(Hand::LEFT) == heldItemID;
			glm::vec3 newPos = player->GetHeldItemPosWS(bLeftHand ? Hand::LEFT : Hand::RIGHT);
			gameObject->GetTransform()->SetWorldPosition(newPos);
		} break;
		}
	}

	void GameObject::UpdateActiveItem(const ActiveItem& activeItem)
	{
		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		Transform* playerTransform = player->GetTransform();

		glm::vec3 posWS = playerTransform->GetWorldPosition() +
			player->GetLookDirection() * 2.0f +
			playerTransform->GetRight() * 1.0f;
		glm::quat rotWS = player->GetLookRotation();

		switch (activeItem.m_TypeID)
		{
		case PickAxeSID:
		case AxeSID:
		case ShovelSID:
		{
			// Turned so the head is seen side on (the axe's blade facing left, towards the center of view)
			rotWS = rotWS * glm::angleAxis(glm::radians(activeItem.m_TypeID == ShovelSID ? -20.0f : -65.0f), VEC3_UP);
			real timer = player->GetItemPickingTimer();
			const real swing01 = player->GetToolSwing01();
			if (activeItem.m_TypeID == PickAxeSID && timer >= 0.0f)
			{
				// Bias so first frame
				real timerAdjusted = timer + (PI - player->GetItemPickingDuration());
				const real mineAnimSpeed = 20.0f;
				const real mineAnimArc = glm::radians(30.0f);
				rotWS = glm::angleAxis(-sin(timerAdjusted * mineAnimSpeed) * mineAnimArc, playerTransform->GetRight()) * rotWS;
			}
			else if (swing01 >= 0.0f)
			{
				// Chopping/digging: a quick swing forwards & down, then back up
				const real swingArc = glm::radians(activeItem.m_TypeID == ShovelSID ? 25.0f : 45.0f);
				const real swing = std::sin(swing01 * PI);
				rotWS = glm::angleAxis(swing * swingArc, playerTransform->GetRight()) * rotWS;
				posWS += player->GetLookDirection() * (swing * 0.3f);
			}
		} break;
		case MineralDepositSID:
		{
			// Hold slightly below center of view, tilting the top face (+Y) back towards the player
			posWS = playerTransform->GetWorldPosition() +
				player->GetLookDirection() * 1.5f +
				playerTransform->GetUp() * -0.4f;
			const real faceAngle = glm::radians(-60.0f);
			rotWS = rotWS * glm::angleAxis(faceAngle, VEC3_RIGHT);
		} break;
		default:
		{
			// Held in front of the player, a little below & to the right of the center of view. (Previously this was 2m
			// along the look direction from the body's center like tools are, which buried items like wood in the
			// ground when looking down, & left them mostly below the screen otherwise, with only their shadow visible.)
			const glm::vec3 viewUp = rotWS * VEC3_UP;
			posWS = player->GetEyePosition() +
				player->GetLookDirection() * 1.2f +
				playerTransform->GetRight() * 0.45f -
				viewUp * 0.38f;
			// Turned a little, so more than one face shows
			rotWS = rotWS * glm::angleAxis(glm::radians(-25.0f), VEC3_UP);
		} break;
		}

		activeItem.m_ItemProxyObject->GetTransform()->SetWorldPosition(posWS, false);
		activeItem.m_ItemProxyObject->GetTransform()->SetWorldRotation(rotWS, true);
	}

	PropertyCollection* GameObject::BuildPropertyCollection()
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->AllocateCollection("base object");

		collection->RegisterProperty("name", FIELD_OFFSET_AND_TYPE(GameObject, m_Name));

		// Added in scene v5
		collection->RegisterProperty("id", FIELD_OFFSET_AND_TYPE(GameObject, ID));

		return collection;
	}

	void GameObject::SetNearbyInteractable(GameObjectID nearbyInteractableID)
	{
		m_NearbyInteractableID = nearbyInteractableID;
	}

	const PrefabIDPair& GameObject::GetSourcePrefabIDPair() const
	{
		return m_SourcePrefabID;
	}

	bool GameObject::IsPrefabTemplate() const
	{
		return m_bIsTemplate;
	}

	void GameObject::ClearNearbyInteractable()
	{
		m_NearbyInteractableID = InvalidGameObjectID;

		for (GameObject* child : m_Children)
		{
			child->ClearNearbyInteractable();
		}
	}

	void GameObject::OnCharge(real chargeAmount)
	{
		FLEX_UNUSED(chargeAmount);
	}

	ChildIndex GameObject::ComputeChildIndex() const
	{
		// NOTE: Sibling indices must have been calculated before calling this!

		std::list<u32> siblingIndices;
		siblingIndices.push_back(m_SiblingIndex);

		GameObject* parent = m_Parent;
		while (parent != nullptr)
		{
			if (parent->m_Parent != nullptr)
			{
				siblingIndices.emplace_front(parent->m_SiblingIndex);
			}
			parent = parent->m_Parent;
		}

		ChildIndex result(siblingIndices);
		return result;
	}

	ChildIndex GameObject::GetChildIndexWithID(const GameObjectID& gameObjectID) const
	{
		ChildIndex result = InvalidChildIndex;
		GetChildIndexWithIDRecursive(gameObjectID, result);
		return result;
	}

	bool GameObject::GetChildIndexWithIDRecursive(const GameObjectID& gameObjectID, ChildIndex& outChildIndex) const
	{
		for (u32 siblingIndex = 0; siblingIndex < (u32)m_Children.size(); ++siblingIndex)
		{
			if (m_Children[siblingIndex]->ID == gameObjectID)
			{
				outChildIndex = m_Children[siblingIndex]->ComputeChildIndex();
				return true;
			}

			// Depth-first search
			if (m_Children[siblingIndex]->GetChildIndexWithIDRecursive(gameObjectID, outChildIndex))
			{
				return true;
			}
		}

		return false;
	}

	GameObjectID GameObject::GetIDAtChildIndex(const ChildIndex& childIndex) const
	{
		GameObjectID result = InvalidGameObjectID;
		GetIDAtChildIndexRecursive(childIndex, result);
		return result;
	}

	PrefabID GameObject::Itemize(GameObjectStack::UserData& outUserData)
	{
		PrefabID prefabID = m_SourcePrefabID.m_PrefabID;

		if (!prefabID.IsValid())
		{
			PrintError("Attempted to itemize object not loaded from prefab\n");
			return prefabID;
		}

		OnItemize(outUserData);

		g_SceneManager->CurrentScene()->RemoveObject(this, true);

		return prefabID;
	}

	GameObject* GameObject::Deitemize(PrefabID prefabID, const glm::vec3& positionWS, const glm::quat& rotWS, const GameObjectStack::UserData& userData)
	{
		GameObject* newObject = CreateObjectFromPrefabTemplate(prefabID, InvalidGameObjectID, nullptr, nullptr, nullptr,
			(CopyFlags)((u32)CopyFlags::ALL & ~(u32)CopyFlags::ADD_TO_SCENE));

		newObject->m_Transform.SetWorldPosition(positionWS, false);
		newObject->m_Transform.SetWorldRotation(rotWS, true);

		newObject->OnDeItemize(userData);

		return newObject;
	}

	bool GameObject::IsItemizable() const
	{
		// Only prefabs are itemizable, we store the prefab ID
		return m_bItemizable && (m_bIsTemplate || m_SourcePrefabID.IsValid());
	}

	bool GameObject::GetIDAtChildIndexRecursive(ChildIndex childIndex, GameObjectID& outGameObjectID) const
	{
		if (!childIndex.siblingIndices.empty() && childIndex.siblingIndices.front() < m_Children.size())
		{
			if (childIndex.siblingIndices.size() == 1)
			{
				outGameObjectID = m_Children[childIndex.siblingIndices.front()]->ID;
				return true;
			}
			else
			{
				ChildIndex subChildIndex(childIndex.siblingIndices);
				u32 siblingIndex = subChildIndex.Pop();
				return m_Children[siblingIndex]->GetIDAtChildIndexRecursive(subChildIndex, outGameObjectID);
			}
		}

		return false;
	}

	void GameObject::GetNewObjectNameAndID(CopyFlags copyFlags, std::string* optionalName, GameObject* parent, std::string& newObjectName, GameObjectID& newGameObjectID)
	{
		if (copyFlags & CopyFlags::COPYING_TO_PREFAB)
		{
			newObjectName = m_Name;
			newGameObjectID = ID;
		}
		else
		{
			newObjectName = (optionalName != nullptr ? *optionalName : g_SceneManager->CurrentScene()->GetUniqueObjectName(m_Name, parent));
		}
	}

	void GameObject::CopyGenericFields(
		GameObject* newGameObject,
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */)
	{
		newGameObject->m_Tags = m_Tags;
		newGameObject->m_Transform.CloneFrom(m_Transform);
		newGameObject->m_TypeID = m_TypeID;

		newGameObject->m_bSerializable = m_bSerializable;
		newGameObject->m_bVisible = m_bVisible;
		newGameObject->m_bVisibleInSceneExplorer = m_bVisibleInSceneExplorer;
		newGameObject->m_bStatic = m_bStatic;
		newGameObject->m_bTrigger = m_bTrigger;
		newGameObject->m_bInteractable = m_bInteractable;
		newGameObject->m_SourcePrefabID = m_SourcePrefabID;
		newGameObject->m_bCastsShadow = m_bCastsShadow;
		newGameObject->m_bUniformScale = m_bUniformScale;
		if (copyFlags & CopyFlags::COPYING_TO_PREFAB)
		{
			newGameObject->m_bIsTemplate = true;
		}

		bool bAddToScene = (copyFlags & CopyFlags::ADD_TO_SCENE);

		if (bAddToScene)
		{
			if (parent != nullptr)
			{
				parent->AddChild(newGameObject);
			}
			else
			{
				if (m_Parent != nullptr)
				{
					g_SceneManager->CurrentScene()->AddChildObjectImmediate(m_Parent, newGameObject);
				}
				else
				{
					g_SceneManager->CurrentScene()->AddRootObjectImmediate(newGameObject);
				}
			}
		}
		else
		{
			if (parent != nullptr)
			{
				parent->AddChildImmediate(newGameObject, /* bRegisterChild: */ false);
			}
		}

		for (const std::string& tag : m_Tags)
		{
			newGameObject->AddTag(tag);
		}

		bool bCreateRenderObject = (copyFlags & CopyFlags::CREATE_RENDER_OBJECT);

		bool bSerializeMesh = ShouldSerializeMesh();
		if ((copyFlags & CopyFlags::MESH) && m_Mesh != nullptr && bSerializeMesh)
		{
			if (newGameObject->GetMesh() != nullptr)
			{
				const char* typeStr = g_ResourceManager->TypeIDToString(newGameObject->GetTypeID());
				PrintError("New object %s already has mesh, perhaps its type (%s) should not be set to serialize mesh?\n", newGameObject->m_Name.c_str(), typeStr);
			}
			else
			{
				m_Mesh->CloneSelf(newGameObject, bCreateRenderObject);
			}
		}

		if ((copyFlags & CopyFlags::RIGIDBODY) && m_RigidBody != nullptr)
		{
			newGameObject->SetRigidBody(new RigidBody(*m_RigidBody));
			newGameObject->SetCollisionShape(CloneCollisionShape(m_Transform.GetWorldScale(), m_CollisionShape));
		}

		if (copyFlags & CopyFlags::CHILDREN)
		{
			for (GameObject* child : m_Children)
			{
				if (child->IsSerializable())
				{
					GameObject* newChild = child->CopySelf(newGameObject, (CopyFlags)((i32)copyFlags & ~(i32)CopyFlags::ADD_TO_SCENE));
					if (newChild != nullptr)
					{
						newGameObject->AddChildImmediate(newChild);
					}
				}
			}
		}
	}

	void GameObject::SetOutputSignal(i32 slotIdx, i32 value)
	{
		GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->SetGameObjectOutputSignal(ID, slotIdx, value);
	}

	i32 GameObject::GetOutputSignal(i32 slotIdx)
	{
		return GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->GetGameObjectOutputSignal(ID, slotIdx);
	}

	GameObject* GameObject::GetParent()
	{
		return m_Parent;
	}

	void GameObject::DetachFromParent()
	{
		if (m_Parent)
		{
			m_Parent->RemoveChildImmediate(this, false);
		}
	}

	std::vector<GameObject*> GameObject::GetParentChain()
	{
		std::vector<GameObject*> result;

		result.push_back(this);

		if (m_Parent)
		{
			GameObject* parent = m_Parent;
			while (parent)
			{
				result.push_back(parent);
				parent = parent->m_Parent;
			}
		}

		std::reverse(result.begin(), result.end());

		return result;
	}

	void GameObject::SetParent(GameObject* parent)
	{
		if (parent == this)
		{
			PrintError("Attempted to set parent as self on %s\n", m_Name.c_str());
			return;
		}

		m_Parent = parent;
	}

	GameObject* GameObject::GetRootParent()
	{
		if (m_Parent == nullptr)
		{
			return this;
		}

		GameObject* parent = m_Parent;
		while (parent->m_Parent)
		{
			parent = parent->m_Parent;
		}

		return parent;
	}

	GameObject* GameObject::AddChild(GameObject* child)
	{
		if (child == nullptr)
		{
			return nullptr;
		}

		if (child == this)
		{
			PrintError("Attempted to add self as child on %s\n", m_Name.c_str());
			return nullptr;
		}

		return g_SceneManager->CurrentScene()->AddChildObject(this, child);
	}

	GameObject* GameObject::AddChildImmediate(GameObject* child, bool bRegisterChild /* = true */)
	{
		AddChildImmediateCommon(child);

		// Prefab templates aren't registered in the scene
		if (bRegisterChild && !m_bIsTemplate)
		{
			g_SceneManager->CurrentScene()->RegisterGameObject(child);
		}

		if (g_SceneManager->HasSceneLoaded())
		{
			g_SceneManager->CurrentScene()->UpdateRootObjectSiblingIndices();
		}

		if (!m_bIsTemplate)
		{
			if (child->GetTypeID() == SocketSID)
			{
				GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->OnSocketChildAdded(ID, child);
			}
		}

		return child;
	}

	EditorObject* GameObject::AddEditorChildImmediate(EditorObject* child)
	{
		AddChildImmediateCommon(child);

		g_SceneManager->CurrentScene()->RegisterEditorObject(child);

		return child;
	}

	GameObject* GameObject::AddChildImmediateCommon(GameObject* child)
	{
		if (child == nullptr)
		{
			return nullptr;
		}

		if (child == this)
		{
			PrintError("Attempted to add self as child on %s\n", m_Name.c_str());
			return nullptr;
		}

		Transform* childTransform = child->GetTransform();
		GameObject* childPrevParent = child->GetParent();
		glm::mat4 childWorldTransform = childTransform->GetWorldTransform();

		if (child == m_Parent)
		{
			DetachFromParent();
		}

		for (GameObject* c : m_Children)
		{
			if (c == child)
			{
				// Don't add the same child twice
				return nullptr;
			}
		}

		m_Children.push_back(child);

		child->SetParent(this);
		child->GetTransform()->MarkDirty(true, true, true);

		if (childPrevParent != nullptr)
		{
			childTransform->SetWorldTransform(childWorldTransform);
		}

		return child;
	}

	bool GameObject::RemoveChildImmediate(GameObjectID childID, bool bDestroy)
	{
		return RemoveChildImmediate(childID.Get(), bDestroy);
	}

	bool GameObject::RemoveChildImmediate(GameObject* child, bool bDestroy)
	{
		if (child == nullptr)
		{
			return false;
		}

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			if ((*iter)->ID == child->ID)
			{
				if (bDestroy)
				{
					child->Destroy(false);
					delete child;
				}
				else
				{
					glm::mat4 childWorldTransform = child->GetTransform()->GetWorldTransform();

					child->SetParent(nullptr);
					child->GetTransform()->MarkDirty(true, true, true);

					Transform* childTransform = child->GetTransform();

					childTransform->SetWorldTransform(childWorldTransform);
				}

				if (g_SceneManager->HasSceneLoaded())
				{
					g_SceneManager->CurrentScene()->UpdateRootObjectSiblingIndices();
				}

				m_Children.erase(iter);

				return true;
			}
		}

		return false;
	}

	void GameObject::MoveChild(GameObjectID childID, i32 newSiblingIndex)
	{
		MoveChild(childID.Get(), newSiblingIndex);
	}

	void GameObject::MoveChild(GameObject* child, i32 newSiblingIndex)
	{
		i32 previousSiblingIndex = child->GetSiblingIndex();
		ReorderItemInList(m_Children, previousSiblingIndex, newSiblingIndex);

		for (i32 i = 0; i < (i32)m_Children.size(); ++i)
		{
			m_Children[i]->UpdateSiblingIndices(i);
		}
	}

	const std::vector<GameObject*>& GameObject::GetChildren() const
	{
		return m_Children;
	}

	u32 GameObject::GetChildCountOfType(StringID objTypeID, bool bRecurse)
	{
		u32 count = 0;

		if (m_TypeID == objTypeID)
		{
			++count;
		}

		if (bRecurse)
		{
			for (GameObject* child : m_Children)
			{
				count += child->GetChildCountOfType(objTypeID, bRecurse);
			}
		}

		return count;
	}

	u32 GameObject::GetChildCount() const
	{
		return (u32)m_Children.size();
	}

	GameObject* GameObject::AddSibling(GameObject* child)
	{
		if (m_Parent != nullptr)
		{
			g_SceneManager->CurrentScene()->AddChildObject(m_Parent, child);
			return child;
		}
		else
		{
			g_SceneManager->CurrentScene()->AddRootObject(child);
			return child;
		}
	}

	GameObject* GameObject::AddSiblingImmediate(GameObject* child)
	{
		if (m_Parent != nullptr)
		{
			return m_Parent->AddChildImmediate(child);
		}
		else
		{
			return g_SceneManager->CurrentScene()->AddRootObjectImmediate(child);
		}
	}

	bool GameObject::HasChild(GameObject* child, bool bCheckChildrensChildren)
	{
		for (GameObject* c : m_Children)
		{
			if (c == child)
			{
				return true;
			}

			if (bCheckChildrensChildren &&
				c->HasChild(child, bCheckChildrensChildren))
			{
				return true;
			}
		}
		return false;
	}

	GameObject* GameObject::GetChild(u32 childIndex)
	{
		if (childIndex < (u32)m_Children.size())
		{
			return m_Children[childIndex];
		}
		return nullptr;
	}

	void GameObject::UpdateSiblingIndices(i32 newIndex)
	{
		m_SiblingIndex = newIndex;

		for (i32 i = 0; i < (i32)m_Children.size(); ++i)
		{
			m_Children[i]->UpdateSiblingIndices(i);
		}
	}

	void GameObject::SetSiblingIndex(i32 newIndex)
	{
		if (m_SiblingIndex == newIndex)
		{
			return;
		}

		if (m_Parent != nullptr)
		{
			m_Parent->MoveChild(this, newIndex);
		}
		else
		{
			g_SceneManager->CurrentScene()->SetRootObjectIndex(this, newIndex);
		}
	}

	i32 GameObject::GetSiblingIndex() const
	{
		return m_SiblingIndex;
	}

	std::vector<GameObject*> GameObject::GetAllSiblings()
	{
		std::vector<GameObject*> result;

		if (m_Parent)
		{
			const std::vector<GameObject*>& siblings = m_Parent->GetChildren();
			for (GameObject* sibling : siblings)
			{
				if (sibling != this)
				{
					result.push_back(sibling);
				}
			}
		}
		else
		{
			const std::vector<GameObject*>& rootObjects = g_SceneManager->CurrentScene()->GetRootObjects();
			for (GameObject* rootObject : rootObjects)
			{
				if (rootObject != this)
				{
					result.push_back(rootObject);
				}
			}
		}

		return result;
	}

	std::vector<GameObject*> GameObject::GetEarlierSiblings()
	{
		std::vector<GameObject*> result;

		if (m_Parent)
		{
			const std::vector<GameObject*>& siblings = m_Parent->GetChildren();

			auto thisIter = FindIter(siblings, this);
			CHECK(thisIter != siblings.end());

			for (auto iter = siblings.begin(); iter != thisIter; ++iter)
			{
				result.push_back(*iter);
			}
		}
		else
		{
			const std::vector<GameObject*>& rootObjects = g_SceneManager->CurrentScene()->GetRootObjects();

			auto thisIter = FindIter(rootObjects, this);
			CHECK(thisIter != rootObjects.end());

			for (auto iter = rootObjects.begin(); iter != thisIter; ++iter)
			{
				result.push_back(*iter);
			}
		}

		return result;
	}

	std::vector<GameObject*> GameObject::GetLaterSiblings()
	{
		std::vector<GameObject*> result;

		if (m_Parent)
		{
			const std::vector<GameObject*>& siblings = m_Parent->GetChildren();

			auto thisIter = FindIter(siblings, this);
			CHECK(thisIter != siblings.end());

			for (auto iter = thisIter + 1; iter != siblings.end(); ++iter)
			{
				result.push_back(*iter);
			}
		}
		else
		{
			const std::vector<GameObject*>& rootObjects = g_SceneManager->CurrentScene()->GetRootObjects();

			auto thisIter = FindIter(rootObjects, this);
			CHECK(thisIter != rootObjects.end());

			for (auto iter = thisIter + 1; iter != rootObjects.end(); ++iter)
			{
				result.push_back(*iter);
			}
		}

		return result;
	}

	Transform* GameObject::GetTransform()
	{
		return &m_Transform;
	}

	const Transform* GameObject::GetTransform() const
	{
		return &m_Transform;
	}

	void GameObject::AddTag(const std::string& tag)
	{
		if (std::find(m_Tags.begin(), m_Tags.end(), tag) == m_Tags.end())
		{
			m_Tags.push_back(tag);
		}
	}

	bool GameObject::HasTag(const std::string& tag)
	{
		auto iter = std::find(m_Tags.begin(), m_Tags.end(), tag);
		return (iter != m_Tags.end());
	}

	std::vector<std::string> GameObject::GetTags() const
	{
		return m_Tags;
	}

	std::string GameObject::GetName() const
	{
		return m_Name;
	}

	void GameObject::SetName(const std::string& newName)
	{
		m_Name = newName;
	}

	void GameObject::GetFullyPathedName(StringBuilder& stringBuilder)
	{
		if (m_Parent != nullptr)
		{
			m_Parent->GetFullyPathedName(stringBuilder);
			stringBuilder.Append('/');
		}

		stringBuilder.Append(m_Name);
	}

	bool GameObject::IsSerializable() const
	{
		return m_bSerializable;
	}

	void GameObject::SetSerializable(bool bSerializable)
	{
		m_bSerializable = bSerializable;
	}

	bool GameObject::IsStatic() const
	{
		return m_bStatic;
	}

	void GameObject::SetStatic(bool bStatic)
	{
		m_bStatic = bStatic;
	}

	bool GameObject::IsVisible() const
	{
		return m_bVisible;
	}

	void GameObject::SetVisible(bool bVisible, bool bEffectChildren)
	{
		if (m_bVisible != bVisible)
		{
			m_bVisible = bVisible;
			g_Renderer->RenderObjectStateChanged();

			if (bEffectChildren)
			{
				for (GameObject* child : m_Children)
				{
					child->SetVisible(bVisible, bEffectChildren);
				}
			}
		}
	}

	bool GameObject::IsVisibleInSceneExplorer(bool bIncludingChildren /* = false */) const
	{
		if (m_bVisibleInSceneExplorer)
		{
			return true;
		}

		if (bIncludingChildren)
		{
			for (GameObject* child : m_Children)
			{
				if (child->IsVisibleInSceneExplorer(bIncludingChildren))
				{
					return true;
				}
			}
		}

		return false;
	}

	void GameObject::SetOwnedInternally(bool bOwnedInternally)
	{
		m_bOwnedInternally = bOwnedInternally;
	}

	void GameObject::SetVisibleInSceneExplorer(bool bVisibleInSceneExplorer)
	{
		if (m_bVisibleInSceneExplorer != bVisibleInSceneExplorer)
		{
			m_bVisibleInSceneExplorer = bVisibleInSceneExplorer;
		}
	}

	bool GameObject::HasUniformScale() const
	{
		return m_bUniformScale;
	}

	void GameObject::SetUseUniformScale(bool bUseUniformScale, bool bEnforceImmediately)
	{
		m_bUniformScale = bUseUniformScale;
		if (m_bUniformScale && bEnforceImmediately)
		{
			EnforceUniformScale();
		}
	}

	void GameObject::EnforceUniformScale()
	{
		glm::vec3 localScale = m_Transform.GetLocalScale();
		real scale;
		if (localScale.x == localScale.y && localScale.x != localScale.z)
		{
			scale = localScale.z;
		}
		else if (localScale.y == localScale.z && localScale.y != localScale.x)
		{
			scale = localScale.x;
		}
		else
		{
			scale = localScale.y;
		}

		m_Transform.SetLocalScale(glm::vec3(scale));
	}

	btCollisionShape* GameObject::SetCollisionShape(btCollisionShape* collisionShape)
	{
		if (m_CollisionShape != nullptr)
		{
			delete m_CollisionShape;
			m_CollisionShape = nullptr;
		}

		m_CollisionShape = collisionShape;

		if (m_RigidBody != nullptr && m_RigidBody->GetRigidBodyInternal() != nullptr)
		{
			m_RigidBody->GetRigidBodyInternal()->setCollisionShape(collisionShape);
			m_RigidBody->GetRigidBodyInternal()->activate(true);
		}

		return collisionShape;
	}

	btCollisionShape* GameObject::GetCollisionShape() const
	{
		return m_CollisionShape;
	}

	void GameObject::OnExternalMeshChange(const std::string& meshFilePath)
	{
		if (m_Mesh != nullptr)
		{
			m_Mesh->OnExternalMeshChange(meshFilePath);
		}
	}

	bool GameObject::GetCollisionAABB(AABB& outAABB)
	{
		if (m_CollisionShape != nullptr)
		{
			btTransform tr;
			tr.setIdentity();
			btVector3 aabbMin, aabbMax;

			m_CollisionShape->getAabb(tr, aabbMin, aabbMax);
			outAABB = { aabbMin.x(), aabbMax.x(),aabbMin.z(), aabbMax.z(), aabbMin.y(), aabbMax.y() };
			return true;
		}
		return false;
	}

	RigidBody* GameObject::SetRigidBody(RigidBody* rigidBody)
	{
		if (m_RigidBody != nullptr)
		{
			m_RigidBody->Destroy();
			delete m_RigidBody;
		}

		m_RigidBody = rigidBody;

		if (rigidBody && rigidBody->GetRigidBodyInternal())
		{
			rigidBody->GetRigidBodyInternal()->setUserPointer(this);
		}

		return rigidBody;
	}

	RigidBody* GameObject::GetRigidBody() const
	{
		return m_RigidBody;
	}

	Mesh* GameObject::GetMesh()
	{
		return m_Mesh;
	}

	Mesh* GameObject::SetMesh(Mesh* mesh)
	{
		if (m_Mesh)
		{
			m_Mesh->Destroy();
			delete m_Mesh;
		}

		m_Mesh = mesh;
		g_Renderer->RenderObjectStateChanged();
		return mesh;
	}

	bool GameObject::CastsShadow() const
	{
		return m_bCastsShadow;
	}

	void GameObject::SetCastsShadow(bool bCastsShadow)
	{
		// Shadow casters are filtered at draw time, no need to notify the renderer
		m_bCastsShadow = bCastsShadow;
	}

	void GameObject::OnOverlapBegin(GameObject* other)
	{
		overlappingObjects.push_back(other);

		if (m_TypeID != PlayerSID)
		{
			if (other->HasTag("Player0") ||
				other->HasTag("Player1"))
			{
				m_bInteractable = true;
			}
		}
	}

	void GameObject::OnOverlapEnd(GameObject* other)
	{
		for (auto iter = overlappingObjects.begin(); iter != overlappingObjects.end(); /* */)
		{
			if (*iter == other)
			{
				iter = overlappingObjects.erase(iter);
			}
			else
			{
				++iter;
			}
		}

		if (m_TypeID != PlayerSID)
		{
			if (other->HasTag("Player0") ||
				other->HasTag("Player1"))
			{
				m_bInteractable = false;
			}
		}
	}

} // namespace flex
