#include "stdafx.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <LinearMath/btIDebugDraw.h>
#include <LinearMath/btTransform.h>

#include <glm/gtx/norm.hpp> // for distance2
#include <glm/gtx/quaternion.hpp> // for rotate
#include <glm/gtx/vector_angle.hpp> // for angle
#include <glm/ext/matrix_transform.hpp> // for rotate

#include <imgui/imgui_internal.h> // For PushItemFlag

IGNORE_WARNINGS_POP

#include "Scene/Objects/Props.hpp"
#include "Editor.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "Physics/RigidBody.hpp"
#include "PropertyCollection.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/Objects/Industry.hpp" // For BuildHandToolMesh
#include "Scene/SceneManager.hpp"

namespace flex
{
	AudioCue Valve::s_SqueakySounds;
	AudioSourceID Valve::s_BunkSound;
	const real DroppedItem::MIN_TIME_BEFORE_PICKUP = 3.0f;

	Valve::Valve(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, ValveSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		if (!s_SqueakySounds.IsInitialized())
		{
			s_SqueakySounds.Initialize("squeak00.wav", 5, true);
		}
		if (!s_SqueakySounds.IsInitialized())
		{
			s_BunkSound = g_ResourceManager->GetOrLoadAudioSourceID(SID("bunk.wav"), true);
		}
	}

	PropertyCollection* Valve::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("valve info");

		collection->childCollection->RegisterProperty("range", FIELD_OFFSET_AND_TYPE(Valve, valveRange))
			.VersionAdded(6)
			.Precision(3);

		return collection;
	}

	GameObject* Valve::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);
		Valve* newGameObject = new Valve(newObjectName, newGameObjectID);

		newGameObject->valveRange = valveRange;
		newGameObject->rotationSpeedScale = rotationSpeedScale;
		newGameObject->invSlowDownRate = invSlowDownRate;
		newGameObject->rotationSpeed = rotationSpeed;
		newGameObject->rotation = rotation;

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void Valve::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		JSONObject valveInfo;
		if (parentObject.TryGetObject("valve info", valveInfo))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(valveInfo, collection->childCollection);

			if (glm::abs(valveRange.y - valveRange.x) <= 0.0001f)
			{
				PrintWarn("Valve's rotation range is 0, it will not be able to rotate!\n");
			}
			if (valveRange.x > valveRange.y)
			{
				PrintWarn("Valve's minimum rotation range is greater than its maximum! Undefined behavior\n");
			}
		}
		else
		{
			std::string sceneName = g_SceneManager->CurrentScene()->GetName();
			PrintError("Valve's \"valve info\" field missing in scene %s\n", sceneName.c_str());
		}

		if (m_Mesh != nullptr)
		{
			Mesh* valveMesh = new Mesh(this);
			bool bCreateRenderObject = !m_bIsTemplate;
			Mesh::CreateInfo meshCreateInfo = {};
			meshCreateInfo.relativeFilePath = MESH_DIRECTORY "valve.glb";
			meshCreateInfo.materialIDs = matIDs;
			meshCreateInfo.bCreateRenderObject = bCreateRenderObject;
			valveMesh->LoadFromFile(meshCreateInfo);
			CHECK_EQ(m_Mesh, nullptr);
			SetMesh(valveMesh);
		}

		if (m_CollisionShape != nullptr)
		{
			btVector3 btHalfExtents(1.5f, 1.0f, 1.5f);
			btCylinderShape* cylinderShape = new btCylinderShape(btHalfExtents);

			SetCollisionShape(cylinderShape);
		}

		if (m_RigidBody != nullptr)
		{
			RigidBody* rigidBody = SetRigidBody(new RigidBody());
			rigidBody->SetMass(1.0f);
			rigidBody->SetKinematic(false);
			rigidBody->SetStatic(false);
		}
	}

	void Valve::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject valveInfo = {};
		SerializeRegisteredProperties(valveInfo, collection->childCollection, bSerializePrefabData);
		if (!valveInfo.fields.empty())
		{
			parentObject.fields.emplace_back("valve info", JSONValue(valveInfo));
		}
	}

	void Valve::PostInitialize()
	{
		GameObject::PostInitialize();

		m_RigidBody->SetPhysicsFlags((u32)PhysicsFlag::TRIGGER);
		btRigidBody* rbInternal = m_RigidBody->GetRigidBodyInternal();
		rbInternal->setAngularFactor(btVector3(0, 1, 0));
		// Mark as trigger
		rbInternal->setCollisionFlags(rbInternal->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
		rbInternal->setGravity(btVector3(0, 0, 0));
	}

	void Valve::Update()
	{
		PROFILE_AUTO("Valve Update");

		// True when our rotation is changed by another object (rising block)
		bool bRotatedByOtherObject = false;
		real currentAbsAvgRotationSpeed = 0.0f;
		if (!m_bBeingInteractedWith)
		{
			rotationSpeed = (rotation - pRotation) / g_DeltaTime;
			// Not entirely true but needed to trigger sound
			currentAbsAvgRotationSpeed = glm::abs(rotationSpeed);
			bRotatedByOtherObject = (glm::abs(rotation - pRotation) > 0);
		}

		if ((rotationSpeed < 0.0f && rotation <= valveRange.x) ||
			(rotationSpeed > 0.0f && rotation >= valveRange.y))
		{
			rotationSpeed = 0.0f;
			pRotationSpeed = 0.0f;
		}
		else
		{
			if (rotationSpeed == 0.0f)
			{
				pRotationSpeed *= invSlowDownRate;
			}
			else
			{
				pRotationSpeed = rotationSpeed;
			}
		}

		if (!bRotatedByOtherObject)
		{
			rotation += g_DeltaTime * pRotationSpeed;
		}

		real overshoot = 0.0f;
		if (rotation > valveRange.y)
		{
			overshoot = rotation - valveRange.y;
			rotation = valveRange.y;
		}
		else if (rotation < valveRange.x)
		{
			overshoot = valveRange.x - rotation;
			rotation = valveRange.x;
		}

		pRotation = rotation;

		if (overshoot != 0.0f &&
			currentAbsAvgRotationSpeed > 0.01f)
		{
			real gain = glm::abs(overshoot) * 8.0f;
			gain = Saturate(gain);
			AudioManager::SetSourceGain(s_BunkSound, gain);
			AudioManager::PlaySource(s_BunkSound, true);
			//Print(std::to_string(overshoot) + ", " + std::to_string(gain));
			rotationSpeed = 0.0f;
			pRotationSpeed = 0.0f;
		}


		/*

		[x] Don't serialize prefab fields other than prefab type
		[x] Right click save as prefab
		[x] Colour prefabs differently in hierarchy
		[ ] Test nested prefabs

		*/


		m_RigidBody->GetRigidBodyInternal()->activate(true);
		m_Transform.SetLocalRotation(glm::quat(glm::vec3(0, rotation, 0)));

		if (glm::abs(rotationSpeed) > 0.2f)
		{
			bool updateGain = !s_SqueakySounds.IsPlaying();

			s_SqueakySounds.Play(false);

			if (updateGain)
			{
				s_SqueakySounds.SetGain(glm::abs(rotationSpeed) * 2.0f - 0.2f);
			}
		}

		GameObject::Update();
	}

	RisingBlock::RisingBlock(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, RisingBlockSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		m_bItemizable = true;
	}

	PropertyCollection* RisingBlock::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("block info");

		collection->childCollection->RegisterProperty("move axis", FIELD_OFFSET_AND_TYPE(RisingBlock, moveAxis))
			.VersionAdded(6)
			.Precision(3);
		collection->childCollection->RegisterProperty("affected by gravity", FIELD_OFFSET_AND_TYPE(RisingBlock, bAffectedByGravity))
			.VersionAdded(6);

		return collection;
	}

	GameObject* RisingBlock::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);
		RisingBlock* newGameObject = new RisingBlock(newObjectName, newGameObjectID);

		newGameObject->valve = valve;
		newGameObject->moveAxis = moveAxis;
		newGameObject->bAffectedByGravity = bAffectedByGravity;
		newGameObject->startingPos = startingPos;

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void RisingBlock::Initialize()
	{
		startingPos = m_Transform.GetWorldPosition();

		GameObject::Initialize();
	}

	void RisingBlock::PostInitialize()
	{
		GameObject::PostInitialize();

		btRigidBody* rbInternal = m_RigidBody->GetRigidBodyInternal();
		rbInternal->setGravity(btVector3(0, 0, 0));
	}

	void RisingBlock::Update()
	{
		PROFILE_AUTO("RisingBlock Update");

		real minDist = valve->valveRange.x;
		real maxDist = valve->valveRange.y;
		real dist = valve->rotation;

		if (bAffectedByGravity &&
			valve->rotation >= valve->valveRange.x + 0.1f)
		{
			// Apply gravity by rotating valve
			real fallSpeed = 6.0f;
			real distMult = 1.0f;// -Saturate(playerControlledValveRotationSpeed / 2.0f);
			real dDist = (fallSpeed * g_DeltaTime * distMult);
			dist -= Lerp(pdDistBlockMoved, dDist, 0.1f);
			pdDistBlockMoved = dDist;

			// NOTE: Don't clamp out of bounds rotation here, valve object
			// will handle it and play correct "overshoot" sound
			//dist = glm::clamp(dist, minDist, maxDist);

			valve->rotation = dist;
		}

		glm::vec3 newPos = startingPos + dist * moveAxis;

		if (m_RigidBody != nullptr)
		{
			m_RigidBody->GetRigidBodyInternal()->activate(true);
			btTransform transform = m_RigidBody->GetRigidBodyInternal()->getInterpolationWorldTransform();
			transform.setOrigin(ToBtVec3(newPos));
			transform.setRotation(btQuaternion::getIdentity());
			m_RigidBody->GetRigidBodyInternal()->setInterpolationWorldTransform(transform);
		}

		btVector3 startPos = ToBtVec3(startingPos);
		g_Renderer->GetDebugRenderer()->drawLine(
			startPos,
			startPos + ToBtVec3(moveAxis * maxDist),
			btVector3(1, 1, 1));
		if (minDist < 0.0f)
		{
			g_Renderer->GetDebugRenderer()->drawLine(
				startPos,
				startPos + ToBtVec3(moveAxis * minDist),
				btVector3(0.99f, 0.6f, 0.6f));
		}

		g_Renderer->GetDebugRenderer()->drawLine(
			startPos,
			startPos + ToBtVec3(moveAxis * dist),
			btVector3(0.3f, 0.3f, 0.5f));

		GameObject::Update();
	}

	void RisingBlock::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		if (m_Mesh == nullptr)
		{
			Mesh* cubeMesh = new Mesh(this);
			cubeMesh->LoadFromFile(MESH_DIRECTORY "cube.glb", matIDs[0]);
			SetMesh(cubeMesh);
		}

		if (m_RigidBody == nullptr)
		{
			RigidBody* rigidBody = SetRigidBody(new RigidBody());
			rigidBody->SetMass(1.0f);
			rigidBody->SetKinematic(true);
			rigidBody->SetStatic(false);
		}

		std::string valveName;

		JSONObject blockInfo;
		if (parentObject.TryGetObject("block info", blockInfo))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(blockInfo, collection->childCollection);

			valveName = blockInfo.GetString("valve name");
		}

		if (valveName.empty())
		{
			PrintWarn("Rising block's \"valve name\" field is empty! Can't find matching valve\n");
		}
		else
		{
			const std::vector<GameObject*>& rootObjects = g_SceneManager->CurrentScene()->GetRootObjects();
			for (GameObject* rootObject : rootObjects)
			{
				if (rootObject->GetName().compare(valveName) == 0)
				{
					valve = static_cast<Valve*>(rootObject);
					break;
				}
			}
		}

		if (valve == nullptr)
		{
			PrintError("Rising block contains invalid valve name: %s - Has that valve been created yet?\n", valveName.c_str());
		}
	}

	void RisingBlock::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject blockInfo = {};
		SerializeRegisteredProperties(blockInfo, collection->childCollection, bSerializePrefabData);
		std::string valveName = valve->GetName();
		SerializeField(blockInfo, bSerializePrefabData, "valve name", &valveName, ValueType::STRING);
		if (!blockInfo.fields.empty())
		{
			parentObject.fields.emplace_back("block info", JSONValue(blockInfo));
		}
	}

	GlassPane::GlassPane(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, GlassPaneSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		m_bItemizable = true;
	}

	PropertyCollection* GlassPane::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("window info");

		collection->childCollection->RegisterProperty("broken", FIELD_OFFSET_AND_TYPE(GlassPane, bBroken))
			.VersionAdded(6);

		return collection;
	}

	GameObject* GlassPane::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);
		GlassPane* newGameObject = new GlassPane(newObjectName, newGameObjectID);

		newGameObject->bBroken = bBroken;

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void GlassPane::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		JSONObject glassInfo;
		if (parentObject.TryGetObject("window info", glassInfo))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(glassInfo, collection->childCollection);

			if (m_Mesh == nullptr)
			{
				Mesh* windowMesh = new Mesh(this);
				const char* filePath;
				if (bBroken)
				{
					filePath = MESH_DIRECTORY "glass-window-broken.glb";
				}
				else
				{
					filePath = MESH_DIRECTORY "glass-window-whole.glb";
				}
				windowMesh->LoadFromFile(filePath, matIDs);
				SetMesh(windowMesh);
			}
		}

		if (m_RigidBody == nullptr)
		{
			RigidBody* rigidBody = SetRigidBody(new RigidBody());
			rigidBody->SetMass(1.0f);
			rigidBody->SetKinematic(true);
			rigidBody->SetStatic(false);
		}
	}

	void GlassPane::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject windowInfo = {};
		SerializeRegisteredProperties(windowInfo, collection->childCollection, bSerializePrefabData);
		if (!windowInfo.fields.empty())
		{
			parentObject.fields.emplace_back("window info", JSONValue(windowInfo));
		}
	}

	ReflectionProbe::ReflectionProbe(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, ReflectionProbeSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
	}

	PropertyCollection* ReflectionProbe::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();
		return collection;
	}

	GameObject* ReflectionProbe::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);
		ReflectionProbe* newGameObject = new ReflectionProbe(newObjectName, newGameObjectID);

		newGameObject->captureMatID = captureMatID;

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void ReflectionProbe::PostInitialize()
	{
		GameObject::PostInitialize();

		g_Renderer->SetReflectionProbeMaterial(captureMatID);
	}

	Skybox::Skybox(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, SkyboxSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		SetCastsShadow(false);
	}

	PropertyCollection* Skybox::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();
		return collection;
	}

	void Skybox::ProcedurallyInitialize(MaterialID matID)
	{
		InternalInit(matID);
	}

	void Skybox::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		JSONObject skyboxInfo;
		if (parentObject.TryGetObject("skybox info", skyboxInfo))
		{
			glm::quat rot;
			if (skyboxInfo.TryGetQuat("rot", rot))
			{
				m_Transform.SetWorldRotation(rot);
			}
		}

		InternalInit(matIDs[0]);
	}

	void Skybox::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		JSONObject skyboxInfo = {};
		glm::vec3 eulerRot = glm::eulerAngles(m_Transform.GetWorldRotation());
		if (!NearlyEquals(eulerRot, VEC3_ZERO, 0.001f))
		{
			SerializeField(skyboxInfo, bSerializePrefabData, "rot", &eulerRot, ValueType::VEC3, 2);
		}
		if (!skyboxInfo.fields.empty())
		{
			parentObject.fields.emplace_back("skybox info", JSONValue(skyboxInfo));
		}
	}

	void Skybox::InternalInit(MaterialID matID)
	{
		if (m_Mesh != nullptr)
		{
			return;
		}

		Mesh* skyboxMesh = new Mesh(this);
		skyboxMesh->LoadPrefabShape(PrefabShape::SKYBOX, matID);
		SetMesh(skyboxMesh);

		g_Renderer->SetSkyboxMesh(m_Mesh);
	}

	//
	// EditorObject
	//

	EditorObject::EditorObject(const std::string& name) :
		GameObject(name, EditorObjectSID, InvalidGameObjectID, InvalidPrefabIDPair, false)
	{
	}

	void EditorObject::Destroy(bool bDetachFromParent /* = true */)
	{
		if (bDetachFromParent)
		{
			DetachFromParent();
		}

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

		if (g_Editor->IsObjectSelected(ID))
		{
			g_Editor->DeselectObject(ID);
		}

		// ID will not be valid on objects that are being saved as a prefab template
		if (g_SceneManager->HasSceneLoaded() && ID.IsValid())
		{
			g_SceneManager->CurrentScene()->UnregisterEditorObject((EditorObjectID*)&ID);
		}
	}

	//
	// DroppedItem
	//

	DroppedItem::DroppedItem(const PrefabID& prefabID, i32 stackSize) :
		GameObject("", DroppedItemSID),
		prefabID(prefabID),
		stackSize(stackSize)
	{
		m_bSerializable = false;
	}

	PropertyCollection* DroppedItem::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();
		return collection;
	}

	void DroppedItem::Initialize()
	{
		GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(prefabID);

		GameObject* child = new GameObject("dropped item child", BaseObjectSID);
		child->SetSerializable(false);

		if (prefabTemplate->GetMesh() != nullptr)
		{
			prefabTemplate->GetMesh()->CloneSelf(child, true);
		}
		else if (BuildHandToolMesh(child, prefabTemplate->GetTypeID()))
		{
			// Procedural tool mesh
		}
		else
		{
			Mesh* mesh = child->SetMesh(new Mesh(child));
			mesh->LoadPrefabShape(PrefabShape::CUBE, g_Renderer->GetPlaceholderMaterialID());
		}

		if (prefabTemplate->GetRigidBody() != nullptr)
		{
			RigidBody* rb = SetRigidBody(new RigidBody(*prefabTemplate->GetRigidBody()));
			// TODO: Figure out how to get dropped items to collide with everything but themselves
			if (rb->IsStatic())
			{
				rb->SetStatic(false);
				rb->SetMass(1.0f);
			}
			rb->SetKinematic(true);
			SetCollisionShape(CloneCollisionShape(VEC3_ONE, prefabTemplate->GetCollisionShape()));
		}

		AddChild(child);

		GameObject::Initialize();

		if (m_RigidBody != nullptr)
		{
			m_RigidBody->SetAngularFactor(btVector3(0.0f, 0.0f, 0.0f)); // Prevent rotation
			m_RigidBody->GetRigidBodyInternal()->setLinearVelocity(ToBtVec3(initialVel));
		}
	}

	void DroppedItem::Destroy(bool bDetachFromParent /* = true */)
	{
		g_SceneManager->CurrentScene()->OnDroppedItemDestroyed(this);

		GameObject::Destroy(bDetachFromParent);
	}

	void DroppedItem::Update()
	{
		PROFILE_AUTO("DroppedItem Update");

		secondsAlive += g_DeltaTime;

		GameObject* child = GetChild(0);
		if (child != nullptr)
		{
			Transform* childTransform = child->GetTransform();

			bobAmount = (sin(g_SecElapsedSinceProgramStart * 7.0f) * 0.5f + 0.5f) * 0.6f;

			glm::vec3 newPos(0.0f, bobAmount, 0.0f);

			glm::quat newRot = childTransform->GetLocalRotation();
			newRot *= glm::quat(glm::vec3(0.0f, turnSpeed * g_DeltaTime, 0.0f));

			childTransform->SetLocalPosition(newPos, false);
			childTransform->SetLocalRotation(newRot, true);
		}

		GameObject::Update();
	}

	bool DroppedItem::CanBePickedUp() const
	{
		return secondsAlive > MIN_TIME_BEFORE_PICKUP;
	}

	void DroppedItem::OnPickedUp()
	{
		AudioManager::PlaySource(BaseScene::s_PickupAudioID);
	}
} // namespace flex
