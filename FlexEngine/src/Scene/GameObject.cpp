#include "stdafx.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletCollision/CollisionShapes/btCompoundShape.h>
#include <BulletCollision/CollisionShapes/btConeShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletCollision/CollisionShapes/btTriangleIndexVertexArray.h>
#include <BulletDynamics/ConstraintSolver/btFixedConstraint.h>
#include <BulletDynamics/ConstraintSolver/btHingeConstraint.h>
#include <BulletDynamics/ConstraintSolver/btHinge2Constraint.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <LinearMath/btIDebugDraw.h>
#include <LinearMath/btTransform.h>

#define SSE_MATHFUN_WITH_CODE
#include "sse_mathfun.h"

#include <glm/gtx/norm.hpp> // for distance2
#include <glm/gtx/quaternion.hpp> // for rotate
#include <glm/gtx/vector_angle.hpp> // for angle
#include <glm/ext/matrix_transform.hpp> // for rotate

#include <imgui/imgui_internal.h> // For PushItemFlag

#include <random>
IGNORE_WARNINGS_POP

#include "Scene/GameObject.hpp"
#include "Cameras/CameraManager.hpp"
#include "Cameras/TerminalCamera.hpp"
#include "Editor.hpp"
#include "FlexEngine.hpp"
#include "Graphics/BitmapFont.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "Inventory.hpp"
#include "JSONParser.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Particles.hpp"
#include "Platform/Platform.hpp"
#include "Player.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/LoadedMesh.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "StringBuilder.hpp"
#include "Systems/CartManager.hpp"
#include "Systems/TrackManager.hpp"
#include "Time.hpp"
#include "VirtualMachine/Backend/VirtualMachine.hpp"
#include "VirtualMachine/Diagnostics.hpp"
#include "Window/Window.hpp"

namespace flex
{
	const char* GameObject::s_DefaultNewGameObjectName = "New_Game_Object_00";
	AudioCue Valve::s_SqueakySounds;
	AudioSourceID Valve::s_BunkSound;

	const real DroppedItem::MIN_TIME_BEFORE_PICKUP = 3.0f;

	const char* BaseCart::emptyCartMeshName = "cart-empty.glb";
	const char* EngineCart::engineMeshName = "cart-engine.glb";

	ms SoftBody::FIXED_UPDATE_TIMESTEP = 1000.0f / 120.0f;
	u32 SoftBody::MAX_UPDATE_COUNT = 20;

	const char* SpringObject::s_ExtendedMeshFilePath = MESH_DIRECTORY "spring-extended.glb";
	const char* SpringObject::s_ContractedMeshFilePath = MESH_DIRECTORY "spring-contracted.glb";
	MaterialID SpringObject::s_SpringMatID = InvalidMaterialID;
	MaterialID SpringObject::s_BobberMatID = InvalidMaterialID;

	ChildIndex InvalidChildIndex = ChildIndex({});

	const real WirePlug::nearbyThreshold = 3.0f;

	const real Wire::DEFAULT_LENGTH = 2.0f;

	const real Battery::flashDuration = 0.5f;

#define SIMD_WAVES 0

	// Wave generation globals accessed from threads
	static volatile u32 wave_workQueueEntriesCreated = 0;
	static volatile u32 wave_workQueueEntriesClaimed = 0;
	static volatile u32 wave_workQueueEntriesCompleted = 0;
	static ThreadSafeArray<GerstnerWave::WaveGenData>* wave_workQueue = nullptr;

	static const char* TireNames[] = { "FL", "FR", "RL", "RR", "None" };
	static const char* BrakeLightNames[] = { "Left", "Right" };
	static_assert((ARRAY_LENGTH(TireNames) - 1) == (i32)Vehicle::Tire::_NONE, "TireNames length does not match number of entires in Tire enum");

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
		m_TypeID = prefabTemplate->GetTypeID();

		m_ItemProxyObject = GameObject::CreateObjectOfType(BaseObjectSID, "Active item");
		m_ItemProxyObject->SetSerializable(false);
		m_ItemProxyObject->SetVisibleInSceneExplorer(false);
		// Mineral deposit meshes are world-sized (~20m across), shrink them down to be hand-held
		m_ItemProxyObject->GetTransform()->Scale(m_TypeID == MineralDepositSID ? 0.04f : 0.5f);

		if (prefabTemplate->GetMesh() != nullptr)
		{
			prefabTemplate->GetMesh()->CloneSelf(m_ItemProxyObject, true);
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

		g_SceneManager->CurrentScene()->AddRootObject(m_ItemProxyObject);
	}

	void ActiveItem::Clear()
	{
		if (m_ItemProxyObject != nullptr)
		{
			g_SceneManager->CurrentScene()->RemoveObject(m_ItemProxyObject, true);
			m_ItemProxyObject = nullptr;
		}
		m_TypeID = InvalidID;
		m_SourcePrefabID = InvalidPrefabID;
	}


#define FIELD_OFFSET_AND_TYPE(type, field) offsetof(type, field), typeid(decltype(field))


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

	GameObject* GameObject::CreateObjectFromJSON(
		const JSONObject& obj,
		BaseScene* scene,
		i32 sceneFileVersion,
		const PrefabIDPair& sourcePrefabIDPair,
		bool bIsPrefabTemplate,
		CopyFlags copyFlags)
	{
		std::string gameObjectTypeStr = obj.GetString("type");
		std::string objectName = obj.GetString("name");

		GameObjectID gameObjectID;
		if (sceneFileVersion >= 5)
		{
			gameObjectID = obj.GetGameObjectID("id");
		}
		else
		{
			gameObjectID = InvalidGameObjectID;
		}

		if (gameObjectTypeStr.compare("prefab") == 0)
		{
			GameObject* prefabTemplate = nullptr;

			std::string prefabName = obj.GetString("prefab type");
			std::string prefabIDStr;
			PrefabID prefabID;

			if (sceneFileVersion >= 6)
			{
				// Added in v6
				prefabIDStr = obj.GetString("prefab id");
				prefabID = GUID::FromString(prefabIDStr);
				prefabTemplate = g_ResourceManager->GetPrefabTemplate(prefabID);

				if (prefabTemplate == nullptr)
				{
					PrintError("Invalid prefab id: %s\n", prefabIDStr.c_str());
					return nullptr;
				}

				// Check for matching GUID but mismatched names
				if (prefabTemplate->GetName().compare(prefabName) != 0)
				{
					std::string prefabTemplateName = prefabTemplate->GetName();
					std::string idStr = prefabID.ToString();
					PrintWarn("Prefabs with matching GUIDs have differing names between scene file & prefab file\n(\"%s\" != \"%s\", GUID: %s)\n",
						prefabTemplateName.c_str(), prefabName.c_str(), idStr.c_str());
				}
			}
			else
			{
				prefabTemplate = g_ResourceManager->GetPrefabTemplate(prefabName.c_str());
				prefabID = g_ResourceManager->GetPrefabID(prefabName.c_str());
			}

			if (prefabTemplate != nullptr)
			{
				JSONObject transformObj;
				Transform transform = Transform::Identity;
				if (obj.TryGetObject("transform", transformObj))
				{
					Transform::ParseJSON(transformObj, transform);
				}

				std::string* optionalObjectName = objectName.length() > 0 ? &objectName : nullptr;
				GameObject* newPrefabInstance = CreateObjectFromPrefabTemplate(prefabID, gameObjectID, optionalObjectName, nullptr, &transform, copyFlags);

				std::vector<MaterialID> matIDs = Material::ParseMaterialArrayJSON(obj, sceneFileVersion);
				if (matIDs.empty())
				{
					matIDs.push_back(g_Renderer->GetPlaceholderMaterialID());
				}

				// Apply prefab overrides that may exist in scene
				newPrefabInstance->ParseTypeUniqueFields(obj, matIDs);

				std::vector<JSONObject> children;
				if (obj.TryGetObjectArray("children", children))
				{
					for (JSONObject& child : children)
					{
						PrefabIDPair idPair;
						idPair.m_PrefabID = sourcePrefabIDPair.m_PrefabID;
						idPair.m_SubGameObjectID = child.GetGUID("id");
						newPrefabInstance->AddChildImmediate(GameObject::CreateObjectFromJSON(child, scene, sceneFileVersion, idPair, bIsPrefabTemplate, copyFlags));
					}
				}

				return newPrefabInstance;
			}
			else
			{
				PrintError("Invalid prefab type: %s (ID: %s)\n", prefabName.c_str(), prefabIDStr.c_str());
				return nullptr;
			}
		}

		StringID gameObjectTypeID = Hash(gameObjectTypeStr.c_str());

		GameObject* newGameObject = CreateObjectOfType(gameObjectTypeID, objectName, gameObjectID, gameObjectTypeStr.c_str(), sourcePrefabIDPair, bIsPrefabTemplate);

		if (newGameObject != nullptr)
		{
			newGameObject->ParseJSON(obj, scene, sceneFileVersion, InvalidMaterialID, copyFlags);
		}

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
		switch (gameObjectTypeID)
		{
		case SkyboxSID: return new Skybox(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case ReflectionProbeSID: return new ReflectionProbe(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case ValveSID: return new Valve(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case RisingBlockSID: return new RisingBlock(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case GlassPaneSID: return new GlassPane(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case PointLightSID: return new PointLight(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case SpotLightSID: return new SpotLight(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case AreaLightSID: return new AreaLight(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case DirectionalLightSID: return new DirectionalLight(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case EmptyCartSID: return new EmptyCart(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case EngineCartSID: return new EngineCart(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case MobileLiquidBoxSID: return new MobileLiquidBox(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case BatterySID: return new Battery(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case TerminalSID: return new Terminal(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case GerstnerWaveSID: return new GerstnerWave(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case TerrainGeneratorSID: return new TerrainGenerator(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case WireSID: return new Wire(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case WirePlugSID: return new WirePlug(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case SocketSID: return new Socket(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case SpringObjectSID: return new SpringObject(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case SoftBodySID: return new SoftBody(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case VehicleSID: return new Vehicle(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case RoadSID: return new Road(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case SolarPanelSID: return new SolarPanel(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case HeadLampSID: return new HeadLamp(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case PowerPoleSID: return new PowerPole(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case MineralDepositSID: return new MineralDeposit(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case MinerSID: return new Miner(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case SpeakerSID: return new Speaker(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case PickAxeSID: return new PickAxe(objectName, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case BaseObjectSID: return new GameObject(objectName, gameObjectTypeID, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		//case ParticleSystemSID: return GetSystem<ParticleManager>(SystemType::PARTICLE_MANAGER)->CreateParticleSystem(objectName, gameObjectTypeID, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		case PlayerSID:
		{
			PrintError("Player was serialized to scene file!\n");
		} break;
		case InvalidStringID: // Fall through
		default:
		{
			PrintWarn("Unhandled game object type in CreateObjectOfType (Object name: %s, type: %s).\n"
				"Creating placeholder base object.\n",
				objectName.c_str(), (optionalTypeStr == nullptr ? "unknown" : optionalTypeStr));

			return new GameObject(objectName, gameObjectTypeID, gameObjectID, sourcePrefabIDPair, bIsPrefabTemplate);
		} break;
		}

		return nullptr;
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

	void GameObject::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		if (!IsVisibleInSceneExplorer())
		{
			return;
		}

		ImGui::PushID(this);

		DrawImGuiForSelfInternal(bDrawingEditorObjects);

		ImGui::PopID();
	}

	void GameObject::DrawImGuiForSelfInternal(bool bDrawingEditorObjects)
	{
		// ImGui::PushID will have already been called, making names not need to be quailfied for uniqueness

		bool bAnyPropertyChanged = false;

		const char* typeStr = BaseScene::GameObjectTypeIDToString(m_TypeID);
		ImGui::Text("%s : %s %s", m_Name.c_str(), typeStr, m_SourcePrefabID.IsValid() ? "(prefab)" : "");

		if (m_SourcePrefabID.IsValid())
		{
			char prefabIDStrBuff[33];
			m_SourcePrefabID.m_PrefabID.ToString(prefabIDStrBuff);
			std::string prefabLoadedFromFilePath = g_ResourceManager->GetPrefabFileName(m_SourcePrefabID.m_PrefabID);
			ImGui::Text("Source prefab: %s (%s)", prefabLoadedFromFilePath.c_str(), prefabIDStrBuff);
		}

		if (DoImGuiContextMenu(true))
		{
			// Early return if object was just deleted
			return;
		}

		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();

			char buffer[33];
			ID.ToString(buffer);
			ImGui::Text("%s", buffer);

			if (m_SourcePrefabID.IsValid())
			{
				char prefabIDStrBuff[33];
				m_SourcePrefabID.m_PrefabID.ToString(prefabIDStrBuff);
				std::string prefabLoadedFromFilePath = g_ResourceManager->GetPrefabFileName(m_SourcePrefabID.m_PrefabID);
				ImGui::Text("Prefab source: %s (%s)", prefabLoadedFromFilePath.c_str(), prefabIDStrBuff);
			}

			ImGui::EndTooltip();
		}

		const std::string objectVisibleLabel("Visible");
		bool bVisible = m_bVisible;
		if (ImGui::Checkbox(objectVisibleLabel.c_str(), &bVisible))
		{
			bAnyPropertyChanged = true;
			SetVisible(bVisible);
		}

		ImGui::SameLine();

		bool bStatic = m_bStatic;
		if (ImGui::Checkbox("Static", &bStatic))
		{
			bAnyPropertyChanged = true;
			SetStatic(bStatic);
		}

		PropertyCollection* propertyCollection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);
		if (propertyCollection != nullptr)
		{
			bAnyPropertyChanged = propertyCollection->DrawImGuiForObject(this) || bAnyPropertyChanged;
		}

		// Transform
		ImGui::Text("Transform");
		{
			// TODO: Add local/global switch
			// TODO: EZ: Ensure precision is retained
			if (ImGui::BeginPopupContextItem("transform context menu"))
			{
				if (ImGui::Button("Copy"))
				{
					CopyTransformToClipboard(&m_Transform);
					ImGui::CloseCurrentPopup();
				}

				ImGui::SameLine();

				if (ImGui::Button("Paste"))
				{
					bAnyPropertyChanged = true;
					PasteTransformFromClipboard(&m_Transform);
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			static bool bWorldSpace = false;
			ImGui::Checkbox("World Space", &bWorldSpace);

			glm::vec3 translation = bWorldSpace ? m_Transform.GetWorldPosition() : m_Transform.GetLocalPosition();
			glm::vec3 rotation = glm::degrees((glm::eulerAngles(bWorldSpace ? m_Transform.GetWorldRotation() : m_Transform.GetLocalRotation())));
			glm::vec3 pScale = bWorldSpace ? m_Transform.GetWorldScale() : m_Transform.GetLocalScale();
			glm::vec3 scale = pScale;

			bool bValueChanged = false;

			bValueChanged = ImGui::DragFloat3("Position", &translation[0], 0.1f) || bValueChanged;
			if (ImGui::IsItemClicked(1))
			{
				translation = VEC3_ZERO;
				bValueChanged = true;
			}

			glm::vec3 cleanedRot;
			bValueChanged = DoImGuiRotationDragFloat3("Rotation", rotation, cleanedRot) || bValueChanged;

			bValueChanged = ImGui::DragFloat3("Scale", &scale[0], 0.01f) || bValueChanged;
			if (ImGui::IsItemClicked(1))
			{
				scale = VEC3_ONE;
				bValueChanged = true;
			}

			ImGui::SameLine();

			bool bUseUniformScale = m_bUniformScale;
			if (ImGui::Checkbox("u", &bUseUniformScale))
			{
				SetUseUniformScale(bUseUniformScale, true);
				bValueChanged = true;
			}
			if (m_bUniformScale)
			{
				real newScale = scale.x;
				if (scale.y != pScale.y)
				{
					newScale = scale.y;
				}
				else if (scale.z != pScale.z)
				{
					newScale = scale.z;
				}
				scale = glm::vec3(newScale);
			}

			if (bValueChanged)
			{
				bAnyPropertyChanged = true;

				glm::quat rotQuat(glm::quat(glm::radians(cleanedRot)));

				if (bWorldSpace)
				{
					m_Transform.SetWorldPosition(translation, false);
					m_Transform.SetWorldRotation(rotQuat, false);
					m_Transform.SetWorldScale(scale, true);
				}
				else
				{
					m_Transform.SetLocalPosition(translation, false);
					m_Transform.SetLocalRotation(rotQuat, false);
					m_Transform.SetLocalScale(scale, true);
				}

				if (m_RigidBody != nullptr)
				{
					m_RigidBody->GetRigidBodyInternal()->activate(true);
				}

				SetUseUniformScale(m_bUniformScale, false);

				//if (m_RigidBody != nullptr)
				//{
				//	m_RigidBody->MatchTransform();
				//}

				if (g_Editor->IsObjectSelected(ID))
				{
					g_Editor->CalculateSelectedObjectsCenter();
				}
			}
		}

		if (m_Mesh != nullptr)
		{
			bAnyPropertyChanged = g_Renderer->DrawImGuiForGameObject(this, bDrawingEditorObjects) || bAnyPropertyChanged;
		}
		else
		{
			if (ImGui::Button("Add mesh component"))
			{
				bAnyPropertyChanged = true;
				Mesh* mesh = SetMesh(new Mesh(this));
				mesh->LoadFromFile(MESH_DIRECTORY "cube.glb", g_Renderer->GetPlaceholderMaterialID());
			}

			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(Editor::MeshPayloadCStr);

				if (payload != nullptr && payload->Data != nullptr)
				{
					bAnyPropertyChanged = true;
					Mesh* mesh = SetMesh(new Mesh(this));
					std::string relativeFilePath = std::string((const char*)payload->Data, payload->DataSize);
					mesh->LoadFromFile(MESH_DIRECTORY + relativeFilePath, g_Renderer->GetPlaceholderMaterialID());

					ImGui::EndDragDropTarget();
				}
			}
		}

		if (m_RigidBody != nullptr)
		{
			ImGui::Spacing();

			btRigidBody* rbInternal = m_RigidBody->GetRigidBodyInternal();

			bool bShowTree = ImGui::TreeNode("Rigid body");

			if (ImGui::BeginPopupContextItem("rb context menu"))
			{
				if (ImGui::Button("Remove rigid body"))
				{
					bAnyPropertyChanged = true;
					RemoveRigidBody();
				}

				ImGui::EndPopup();
			}

			if (bShowTree)
			{
				if (m_RigidBody != nullptr)
				{
					bool bRBStatic = m_RigidBody->IsStatic();
					if (ImGui::Checkbox("Static##rb", &bRBStatic))
					{
						bAnyPropertyChanged = true;
						m_RigidBody->SetStatic(bRBStatic);
					}

					bool bKinematic = m_RigidBody->IsKinematic();
					if (ImGui::Checkbox("Kinematic", &bKinematic))
					{
						bAnyPropertyChanged = true;
						m_RigidBody->SetKinematic(bKinematic);
					}

					ImGui::PushItemWidth(80.0f);
					{
						u32 group = m_RigidBody->GetGroup();
						if (ImGuiExt::InputUInt("Group", &group, 1u, 16u))
						{
							bAnyPropertyChanged = true;
							group = glm::clamp(group, 0u, 16u);
							m_RigidBody->SetGroup(group);
						}

						ImGui::SameLine();

						u32 mask = m_RigidBody->GetMask();
						if (ImGuiExt::InputUInt("Mask", &mask, 1u, 16u))
						{
							bAnyPropertyChanged = true;
							mask = glm::clamp(mask, 0u, 16u);
							m_RigidBody->SetMask(mask);
						}
					}
					ImGui::PopItemWidth();

					// TODO: Array of buttons for each category
					i32 flags = m_RigidBody->GetPhysicsFlags();
					if (ImGui::SliderInt("Flags", &flags, 0, 16))
					{
						bAnyPropertyChanged = true;
						m_RigidBody->SetPhysicsFlags(flags);
					}

					real mass = m_RigidBody->GetMass();
					if (ImGui::SliderFloat("Mass", &mass, 0.0f, 1000.0f))
					{
						bAnyPropertyChanged = true;
						m_RigidBody->SetMass(mass);
					}

					real friction = m_RigidBody->GetFriction();
					if (ImGui::SliderFloat("Friction", &friction, 0.0f, 1.0f))
					{
						bAnyPropertyChanged = true;
						m_RigidBody->SetFriction(friction);
					}

					ImGui::Spacing();

					btCollisionShape* shape = m_RigidBody->GetRigidBodyInternal()->getCollisionShape();
					std::string shapeTypeStr = CollisionShapeTypeToString(shape->getShapeType());

					if (ImGui::BeginCombo("Shape", shapeTypeStr.c_str()))
					{
						i32 selectedColliderShape = -1;
						for (i32 i = 0; i < (i32)ARRAY_LENGTH(g_CollisionTypes); ++i)
						{
							if (g_CollisionTypes[i] == shape->getShapeType())
							{
								selectedColliderShape = i;
								break;
							}
						}

						if (selectedColliderShape == -1)
						{
							PrintError("Failed to find collider shape in array!\n");
						}
						else
						{
							for (i32 i = 0; i < (i32)ARRAY_LENGTH(g_CollisionTypes); ++i)
							{
								bool bSelected = (i == selectedColliderShape);
								const char* colliderShapeName = g_CollisionTypeStrs[i];
								if (ImGui::Selectable(colliderShapeName, &bSelected))
								{
									if (selectedColliderShape != i)
									{
										bAnyPropertyChanged = true;

										selectedColliderShape = i;

										btCollisionShape* collisionShape = CreateCollisionShape(g_CollisionTypes[selectedColliderShape]);
										SetCollisionShape(collisionShape);
									}
								}
							}
						}

						ImGui::EndCombo();
					}

					glm::vec3 scale = m_Transform.GetWorldScale();
					switch (shape->getShapeType())
					{
					case BOX_SHAPE_PROXYTYPE:
					{
						btBoxShape* boxShape = static_cast<btBoxShape*>(shape);
						btVector3 halfExtents = boxShape->getHalfExtentsWithMargin();
						glm::vec3 halfExtentsG = ToVec3(halfExtents);
						halfExtentsG /= scale;

						if (ImGui::DragFloat3("Half extents", &halfExtentsG.x, 0.01f, 0.0f, 1000.0f))
						{
							halfExtentsG *= scale;
							bAnyPropertyChanged = true;
							btCollisionShape* collisionShape = CreateCollisionShape(BOX_SHAPE_PROXYTYPE, halfExtentsG.x, halfExtentsG.y, halfExtentsG.z);
							SetCollisionShape(collisionShape);
						}
					} break;
					case SPHERE_SHAPE_PROXYTYPE:
					{
						btSphereShape* sphereShape = static_cast<btSphereShape*>(shape);
						real radius = sphereShape->getRadius();
						radius /= scale.x;

						if (ImGui::DragFloat("radius", &radius, 0.1f, 0.0f, 1000.0f))
						{
							radius *= scale.x;
							bAnyPropertyChanged = true;
							btCollisionShape* collisionShape = CreateCollisionShape(SPHERE_SHAPE_PROXYTYPE, radius);
							SetCollisionShape(collisionShape);
						}
					} break;
					case CAPSULE_SHAPE_PROXYTYPE:
					{
						btCapsuleShapeZ* capsuleShape = static_cast<btCapsuleShapeZ*>(shape);
						real radius = capsuleShape->getRadius();
						real halfHeight = capsuleShape->getHalfHeight();
						radius /= scale.x;
						halfHeight /= scale.x;

						bool bUpdateShape = ImGui::DragFloat("radius", &radius, 0.1f, 0.0f, 1000.0f);
						bUpdateShape |= ImGui::DragFloat("height", &halfHeight, 0.1f, 0.0f, 1000.0f);

						if (bUpdateShape)
						{
							radius *= scale.x;
							halfHeight *= scale.x;
							bAnyPropertyChanged = true;
							btCollisionShape* collisionShape = CreateCollisionShape(SPHERE_SHAPE_PROXYTYPE, radius, halfHeight * 2.0f);
							SetCollisionShape(collisionShape);
						}
					} break;
					case CYLINDER_SHAPE_PROXYTYPE:
					{
						btCylinderShape* cylinderShape = static_cast<btCylinderShape*>(shape);
						btVector3 halfExtents = cylinderShape->getHalfExtentsWithMargin();
						glm::vec3 halfExtentsG = ToVec3(halfExtents);
						halfExtentsG /= scale;

						if (ImGui::DragFloat3("Half extents", &halfExtentsG.x, 0.1f, 0.0f, 1000.0f))
						{
							halfExtentsG *= scale;
							bAnyPropertyChanged = true;
							btCollisionShape* collisionShape = CreateCollisionShape(SPHERE_SHAPE_PROXYTYPE, halfExtentsG.x, halfExtentsG.y, halfExtentsG.z);
							SetCollisionShape(collisionShape);
						}
					} break;
					default:
					{
						PrintWarn("Unhandled shape type in GameObject::DrawImGuiObjects\n");
					} break;
					}

					//glm::vec3 localOffsetPos = m_RigidBody->GetRigidBodyInternal()->getCollisionShape();
					//if (ImGui::DragFloat3("Pos offset", &localOffsetPos.x, 0.05f))
					//{
					//	bAnyPropertyChanged = true;
					//	m_RigidBody->SetLocalPosition(localOffsetPos);
					//}
					//if (ImGui::IsItemClicked(1))
					//{
					//	bAnyPropertyChanged = true;
					//	m_RigidBody->SetLocalPosition(VEC3_ZERO);
					//}

					//glm::vec3 localOffsetRotEuler = glm::degrees(glm::eulerAngles(m_RigidBody->GetLocalRotation()));
					//glm::vec3 cleanedRot;
					//if (DoImGuiRotationDragFloat3("Rot offset", localOffsetRotEuler, cleanedRot))
					//{
					//	bAnyPropertyChanged = true;
					//	m_RigidBody->SetLocalRotation(glm::quat(glm::radians(cleanedRot)));
					//}

					ImGui::Spacing();

					glm::vec3 linearVel = ToVec3(m_RigidBody->GetRigidBodyInternal()->getLinearVelocity());
					if (ImGui::DragFloat3("linear vel", &linearVel.x, 0.05f))
					{
						bAnyPropertyChanged = true;
						rbInternal->setLinearVelocity(ToBtVec3(linearVel));
					}
					if (ImGui::IsItemClicked(1))
					{
						bAnyPropertyChanged = true;
						rbInternal->setLinearVelocity(btVector3(0.0f, 0.0f, 0.0f));
					}

					glm::vec3 angularVel = ToVec3(m_RigidBody->GetRigidBodyInternal()->getAngularVelocity());
					if (ImGui::DragFloat3("angular vel", &angularVel.x, 0.05f))
					{
						bAnyPropertyChanged = true;
						rbInternal->setAngularVelocity(ToBtVec3(angularVel));
					}
					if (ImGui::IsItemClicked(1))
					{
						bAnyPropertyChanged = true;
						rbInternal->setAngularVelocity(btVector3(0.0f, 0.0f, 0.0f));
					}

					ImGui::Text("Activation state: %i", rbInternal->getActivationState());

					ImGui::Text("Group: %u", m_RigidBody->GetGroup());
					ImGui::Text("Mask: %u", m_RigidBody->GetMask());
				}

				ImGui::TreePop();
			}
		}
		else
		{
			if (ImGui::Button("Add rigid body"))
			{
				bAnyPropertyChanged = true;
				RigidBody* rb = SetRigidBody(new RigidBody());
				btCollisionShape* collisionShape = CreateCollisionShape(BOX_SHAPE_PROXYTYPE);
				SetCollisionShape(collisionShape);

				rb->Initialize(collisionShape, &m_Transform);
				rb->GetRigidBodyInternal()->setUserPointer(this);
			}
		}

		std::vector<SocketData> const* sockets = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->GetGameObjectSockets(ID);
		if (sockets != nullptr)
		{
			bool bTreeOpen = ImGui::TreeNode("Sockets");
			ImGui::SameLine();
			ImGui::Text("(%i)", (i32)sockets->size());

			if (bTreeOpen)
			{
				for (u32 i = 0; i < (u32)sockets->size(); ++i)
				{
					ImGui::Text("%u: %i", i, (*sockets)[i].outputSignal);
				}
				ImGui::TreePop();
			}
		}

		ImGui::Text("Interactable: %s", m_bInteractable ? "true" : "false");
		ImGui::Text("Serializable: %s", m_bSerializable ? "true" : "false");

		ImGui::Text("Trigger: %s", m_bTrigger ? "true" : "false"); // TODO: Make checkbox

		std::string nearbyInteractibleName;
		if (m_NearbyInteractableID.IsValid())
		{
			nearbyInteractibleName = m_NearbyInteractableID.Get()->GetName();
		}
		ImGui::Text("NearbyInteractible: %s", nearbyInteractibleName.c_str());
		ImGui::Text("Sibling index: %i", m_SiblingIndex);

		std::string parentName;
		if (m_Parent != nullptr)
		{
			parentName = m_Parent->GetName();
		}
		ImGui::Text("Parent: %s", parentName.c_str());
		ImGui::Text("Num children: %u", (u32)m_Children.size());

		if (bAnyPropertyChanged && m_SourcePrefabID.IsValid())
		{
			g_ResourceManager->SetPrefabDirty(m_SourcePrefabID.m_PrefabID);
		}
	}

	bool GameObject::DoImGuiContextMenu(bool bActive)
	{
		bool bDeletedOrDuplicated = false;

		ImGui::PushID(&ID.Data1);
		ImGui::PushID(&ID.Data2);

		const char* contextMenuID = "game object context window";
		static std::string newObjectName = m_Name;
		const size_t maxStrLen = 256;

		bool bRefreshNameField = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
			ImGui::IsMouseClicked(1);

		if (bActive && g_Editor->GetWantRenameActiveElement())
		{
			ImGui::OpenPopup(contextMenuID);
			g_Editor->ClearWantRenameActiveElement();
			bRefreshNameField = true;
		}
		if (bRefreshNameField)
		{
			newObjectName = m_Name;
			newObjectName.resize(maxStrLen);
		}

		if (ImGui::BeginPopupContextItem(contextMenuID))
		{
			if (ImGui::IsWindowAppearing())
			{
				ImGui::SetKeyboardFocusHere();
			}

			bool bRename = ImGui::InputText("##rename-game-object",
				(char*)newObjectName.data(),
				maxStrLen,
				ImGuiInputTextFlags_EnterReturnsTrue);

			ImGui::SameLine();

			bRename |= ImGui::Button("Rename");

			bool bInvalidName = std::string(newObjectName.c_str()).empty();

			if (bRename && !bInvalidName)
			{
				// Remove excess trailing \0 chars
				newObjectName = std::string(newObjectName.c_str());

				m_Name = newObjectName;

				ImGui::CloseCurrentPopup();
			}

			// ID
			{
				char buffer[33];
				ID.ToString(buffer);
				char data0Buffer[17];
				char data1Buffer[17];
				memcpy(data0Buffer, buffer, 16);
				data0Buffer[16] = 0;
				memcpy(data1Buffer, buffer + 16, 16);
				data1Buffer[16] = 0;
				ImGui::Text("GUID: %s-%s", data0Buffer, data1Buffer);

				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();

					ImGui::Text("Data1, Data2: %lu, %lu", ID.Data1, ID.Data2);

					ImGui::EndTooltip();
				}

				ImGui::SameLine();

				if (ImGui::Button("Copy"))
				{
					g_Window->SetClipboardText(buffer);
				}
			}

			if (m_SourcePrefabID.IsValid())
			{
				std::string prefabLoadedFromFilePath = g_ResourceManager->GetPrefabFileName(m_SourcePrefabID.m_PrefabID);
				ImGui::Text("Prefab source: %s", prefabLoadedFromFilePath.c_str());
			}

			BaseScene* currentScene = g_SceneManager->CurrentScene();

			const char* typeStr = BaseScene::GameObjectTypeIDToString(m_TypeID);
			StringID newTypeID;
			std::string newTypeStr;
			if (currentScene->DoGameObjectTypeList(typeStr, newTypeID, newTypeStr))
			{
				std::vector<GameObjectID> selectedObjectIDs = g_Editor->GetSelectedObjectIDs();

				std::string name = m_Name;
				GameObject* parent = m_Parent;
				GameObject* newObject = CreateObjectOfType(newTypeID, m_Name, ID, typeStr, m_SourcePrefabID, m_bIsTemplate);
				CopyGenericFields(newObject, parent, (CopyFlags)((i32)CopyFlags::ALL & ~(i32)CopyFlags::ADD_TO_SCENE));
				newObject->m_TypeID = newTypeID; // Overwrite type ID again
				currentScene->RemoveObjectImmediate(this, true);
				if (parent != nullptr)
				{
					parent->AddChildImmediate(newObject);
				}
				else
				{
					currentScene->AddRootObjectImmediate(newObject);
				}
				newObject->Initialize();
				newObject->PostInitialize();

				g_Editor->SetSelectedObjects(selectedObjectIDs);

				bDeletedOrDuplicated = true;
			}

			if (DrawImGuiDuplicateGameObjectButton())
			{
				ImGui::CloseCurrentPopup();
				bDeletedOrDuplicated = true;
			}

			ImGui::SameLine();

			if (ImGui::Button("Delete"))
			{
				g_SceneManager->CurrentScene()->RemoveObject(this, true);
				bDeletedOrDuplicated = true;

				ImGui::CloseCurrentPopup();
			}

			bool bDisableSaveAsPrefab = !IsStringLower(m_Name);
			if (bDisableSaveAsPrefab)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.1f, 0.1f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
			}
			bool bDoSaveAsPrefab = ImGui::Button("Save as prefab");
			bool bDoForceSaveAsPrefab = ImGui::Button("Save as prefab (force new)");
			if (bDisableSaveAsPrefab)
			{
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Prefab names must be all lowercase");
				ImGui::PopStyleColor(3);
			}

			if (bDoSaveAsPrefab && !bDisableSaveAsPrefab)
			{
				SaveAsPrefab();
				bDeletedOrDuplicated = true;

				ImGui::CloseCurrentPopup();
			}

			if (bDoForceSaveAsPrefab && !bDisableSaveAsPrefab)
			{
				m_SourcePrefabID.Clear();
				SaveAsPrefab();
				bDeletedOrDuplicated = true;

				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		ImGui::PopID(); // ID.Data2
		ImGui::PopID(); // ID.Data1

		return bDeletedOrDuplicated;
	}

	bool GameObject::SaveAsPrefab()
	{
		BaseScene* currentScene = g_SceneManager->CurrentScene();

		if (!IsStringLower(m_Name))
		{
			PrintError("Prefab names must be all lowercase, given name is not: %s\n", m_Name.c_str());
			return false;
		}

		if (m_bIsTemplate)
		{
			// Save a prefab over an existing prefab
			return g_ResourceManager->WriteExistingPrefabToDisk(this);
		}
		else if (m_SourcePrefabID.IsValid())
		{
			// Save an instance over an existing prefab

			GameObject* previousPrefabTemplate = g_ResourceManager->GetPrefabTemplate(m_SourcePrefabID);
			if (previousPrefabTemplate == nullptr)
			{
				PrintError("Failed to find source prefab for object %s!\n", m_Name.c_str());
				return false;
			}

			currentScene->UnregisterGameObjectRecursive(ID, /* bAssertSuccess: */ true);

			CopyFlags copyFlags = (CopyFlags)(
				(CopyFlags::ALL &
					~CopyFlags::ADD_TO_SCENE &
					~CopyFlags::CREATE_RENDER_OBJECT)
				| CopyFlags::COPYING_TO_PREFAB);

			GameObject* newPrefabTemplate = CopySelf(nullptr, copyFlags);

			// Use previous game object IDs where possible to prevent unnecessary changes in file
			OverwritePrefabIDs(previousPrefabTemplate, newPrefabTemplate);
			previousPrefabTemplate->FixupPrefabTemplateIDs(newPrefabTemplate);

			g_ResourceManager->UpdatePrefabData(newPrefabTemplate, m_SourcePrefabID.m_PrefabID);

			return true;
		}
		else
		{
			// Save as a new prefab

			char fileNameStrBuff[256];
			snprintf(fileNameStrBuff, ARRAY_LENGTH(fileNameStrBuff), "%s.json", m_Name.c_str());

			PrefabID newPrefabID = g_ResourceManager->CreateNewPrefab(this, fileNameStrBuff);
			if (!newPrefabID.IsValid())
			{
				PrintError("Failed to create new prefab from object %s\n", m_Name.c_str());
				return false;
			}

			GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(newPrefabID);
			CHECK_NE(prefabTemplate, nullptr);

			// This object's GameObjectID will match that of the existing object in
			// the scene, but that shouldn't pose any issues.

			ResourceManager::PrefabTemplateInfo prefabTemplateInfo(prefabTemplate, newPrefabID, std::string(fileNameStrBuff));
			bool bResult = g_ResourceManager->WritePrefabToDisk(prefabTemplateInfo);

			if (!bResult)
			{
				return false;
			}

			//i32 siblingIndex = GetSiblingIndex();

			//currentScene->UnregisterGameObjectRecursive(ID, true);

			//GameObject* newInstance = CreateObjectFromPrefabTemplate(newPrefabID, ID, &m_Name, m_Parent, &m_Transform, CopyFlags::ALL);
			//CHECK_NE(newInstance, nullptr);

			//g_SceneManager->CurrentScene()->RemoveObjectImmediate(this, true);

			//g_ResourceManager->DiscoverPrefabs();

			currentScene->ReinstantiateFromPrefab(newPrefabID, this);
			// NOTE: !! At this point, `this` is deleted, proceed with caution! !!

			//newInstance->SetSiblingIndex(siblingIndex);

			return true;
		}
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

	bool GameObject::DrawImGuiDuplicateGameObjectButton()
	{
		if (ImGui::Button("Duplicate..."))
		{
			GameObject* newGameObject = CopySelf();
			if (newGameObject != nullptr)
			{
				g_Editor->SetSelectedObject(newGameObject->ID);
			}

			return true;
		}

		return false;
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

	bool GameObject::ShouldSerialize()
	{
		return true;
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

	void GameObject::ParseJSON(
		const JSONObject& obj,
		BaseScene* scene,
		i32 fileVersion,
		MaterialID overriddenMatID /* = InvalidMaterialID */,
		CopyFlags copyFlags /* = CopyFlags::ALL */)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		DeserializeRegisteredProperties(obj, collection);

		bool bVisible = true;
		obj.TryGetBool("visible", bVisible);
		bool bVisibleInSceneGraph = true;
		obj.TryGetBool("visible in scene graph", bVisibleInSceneGraph);

		std::vector<MaterialID> matIDs;
		if (overriddenMatID != InvalidMaterialID)
		{
			matIDs = { overriddenMatID };
		}
		else
		{
			matIDs = Material::ParseMaterialArrayJSON(obj, fileVersion);
		}

		if (matIDs.empty())
		{
			matIDs.push_back(g_Renderer->GetPlaceholderMaterialID());
		}

		JSONObject transformObj;
		if (obj.TryGetObject("transform", transformObj))
		{
			Transform::ParseJSON(transformObj, m_Transform);
		}

		bool bCreateRenderObject = (copyFlags & (u32)CopyFlags::CREATE_RENDER_OBJECT) != 0;
		g_ResourceManager->ParseMeshJSON(fileVersion, this, obj, matIDs, bCreateRenderObject);

		JSONObject colliderObj;
		if (obj.TryGetObject("collider", colliderObj))
		{
			SetCollisionShape(ParseCollider(colliderObj));
		}

		JSONObject rigidBodyObj;
		if (obj.TryGetObject("rigid body", rigidBodyObj))
		{
			if (GetCollisionShape() == nullptr)
			{
				PrintError("Serialized object contains \"rigid body\" field but no collider: %s\n", m_Name.c_str());
			}
			else
			{
				SetRigidBody(RigidBody::ParseFromJSON(rigidBodyObj));
			}
		}

		ParseTypeUniqueFields(obj, matIDs);

		SetVisible(bVisible, false);
		SetVisibleInSceneExplorer(bVisibleInSceneGraph);

		bool bStatic = false;
		if (obj.TryGetBool("static", bStatic))
		{
			SetStatic(bStatic);
		}

		bool bCastsShadow = m_bCastsShadow;
		if (obj.TryGetBool("casts shadow", bCastsShadow))
		{
			m_bCastsShadow = bCastsShadow;
		}

		std::vector<JSONObject> children;
		if (obj.TryGetObjectArray("children", children))
		{
			for (const JSONObject& child : children)
			{
				// TODO: Handle nested prefabs
				PrefabIDPair idPair;
				idPair.m_PrefabID = m_SourcePrefabID.m_PrefabID;
				idPair.m_SubGameObjectID = child.GetGUID("id");
				AddChildImmediate(GameObject::CreateObjectFromJSON(child, scene, fileVersion, idPair, m_bIsTemplate, copyFlags));
			}
		}
	}

	JSONObject GameObject::Serialize(const BaseScene* scene, bool bIsRoot, bool bSerializePrefabData)
	{
		JSONObject object = {};

		if (!m_bSerializable)
		{
			PrintError("Attempted to serialize non-serializable object with name \"%s\"\n", m_Name.c_str());
			return object;
		}

		if (!ShouldSerialize())
		{
			return object;
		}

		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		SerializeRegisteredProperties(object, collection, bSerializePrefabData);

		if (bSerializePrefabData)
		{
			std::string typeIDStr = BaseScene::GameObjectTypeIDToString(m_TypeID);
			if (typeIDStr.empty())
			{
				PrintWarn("Prefab type not serialized, unknown typeID: %lu\n", m_TypeID);
			}
			object.fields.emplace_back("type", JSONValue(typeIDStr));
		}
		else
		{
			if (m_SourcePrefabID.IsValid())
			{
				object.fields.emplace_back("type", JSONValue("prefab"));
				std::string prefabName = g_ResourceManager->GetPrefabTemplate(m_SourcePrefabID)->m_Name;
				object.fields.emplace_back("prefab type", JSONValue(prefabName)); // More of a convenience for source control, GUID is ground truth
				// Added in scene v6
				std::string prefabIDStr = m_SourcePrefabID.m_PrefabID.ToString();
				object.fields.emplace_back("prefab id", JSONValue(prefabIDStr));
			}
			else
			{
				std::string typeIDStr = BaseScene::GameObjectTypeIDToString(m_TypeID);
				if (typeIDStr.empty())
				{
					PrintWarn("Object type not serialized, unknown typeID: %lu\n", m_TypeID);
				}
				object.fields.emplace_back("type", JSONValue(typeIDStr));
			}
		}

		// Prefab root's transform is always assumed to be at the origin
		bool bIsPrefabRoot = bSerializePrefabData && bIsRoot;
		if (!bIsPrefabRoot)
		{
			JSONObject transformObj = m_Transform.Serialize();
			if (!transformObj.fields.empty())
			{
				object.fields.emplace_back("transform", JSONValue(transformObj));
			}
		}

		// TODO: Handle overrides
		if (!m_SourcePrefabID.IsValid() || bSerializePrefabData)
		{
			if (!m_bVisible)
			{
				object.fields.emplace_back("visible", JSONValue(m_bVisible));
			}

			if (!m_bVisibleInSceneExplorer)
			{
				object.fields.emplace_back("visible in scene graph", JSONValue(m_bVisibleInSceneExplorer));
			}

			if (m_bStatic)
			{
				object.fields.emplace_back("static", JSONValue(m_bStatic));
			}

			if (!m_bCastsShadow)
			{
				object.fields.emplace_back("casts shadow", JSONValue(m_bCastsShadow));
			}

			if (m_Mesh != nullptr)
			{
				bool bSerializeMesh = ShouldSerializeMesh();
				if (bSerializeMesh && (!m_SourcePrefabID.IsValid() || m_bIsTemplate))
				{
					object.fields.emplace_back(g_ResourceManager->SerializeMesh(m_Mesh));
				}

				bool bSerializeMaterial = ShouldSerializeMaterial();
				if (bSerializeMaterial)
				{
					std::vector<JSONField> materialFields;
					for (u32 slotIndex = 0; slotIndex < m_Mesh->GetSubmeshCount(); ++slotIndex)
					{
						MaterialID matID = InvalidMaterialID;
						if (m_Mesh)
						{
							matID = m_Mesh->GetMaterialID(slotIndex);
						}

						if (matID != InvalidMaterialID)
						{
							Material* material = g_Renderer->GetMaterial(matID);
							if (material->bSerializable)
							{
								std::string materialName = material->name;
								if (!materialName.empty())
								{
									materialFields.emplace_back("", JSONValue(materialName));
								}
								else
								{
									PrintWarn("Game object contains material with empty material name!\n");
								}
							}
						}
					}

					if (!materialFields.empty())
					{
						object.fields.emplace_back("materials", JSONValue(materialFields));
					}
				}
			}

			btCollisionShape* collisionShape = GetCollisionShape();
			if (collisionShape != nullptr && (!m_SourcePrefabID.IsValid() || bSerializePrefabData))
			{
				JSONObject colliderObj;
				if (SerializeCollider(collisionShape, m_Transform.GetWorldScale(), colliderObj))
				{
					object.fields.emplace_back("collider", JSONValue(colliderObj));
				}
			}

			RigidBody* rigidBody = GetRigidBody();
			if (rigidBody != nullptr && (!m_SourcePrefabID.IsValid() || bSerializePrefabData))
			{
				if (collisionShape == nullptr)
				{
					PrintError("Attempted to serialize object (%s) which has a rigid body but no collider!\n", GetName().c_str());
				}
				else
				{
					object.fields.emplace_back("rigid body", JSONValue(rigidBody->SerializeToJSON()));
				}
			}
		}

		SerializeTypeUniqueFields(object, bSerializePrefabData);

		if (!m_Children.empty())
		{
			std::vector<JSONObject> childrenToSerialize;

			for (GameObject* child : m_Children)
			{
				if (child->IsSerializable())
				{
					if (m_SourcePrefabID.IsValid() && !m_bIsTemplate)
					{
						// Don't serialize children if this is a prefab instance and
						// the children are present in the prefab definition
						if (g_ResourceManager->PrefabTemplateContainsChild(m_SourcePrefabID.m_PrefabID, child))
						{
							continue;
						}
					}

					JSONObject childObject = child->Serialize(scene, false, bSerializePrefabData);
					if (!childObject.fields.empty())
					{
						childrenToSerialize.push_back(childObject);
					}
				}
			}

			// It's possible that all children are non-serializable
			if (!childrenToSerialize.empty())
			{
				object.fields.emplace_back("children", JSONValue(childrenToSerialize));
			}
		}

		return object;
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
		propertyCollectionManager->RegisterType(DirectionalLightSID, DirectionalLight::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(PointLightSID,PointLight::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(SpotLightSID, SpotLight::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(AreaLightSID, AreaLight::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(ValveSID, Valve::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(RisingBlockSID, RisingBlock::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(GlassPaneSID, GlassPane::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(ReflectionProbeSID, ReflectionProbe::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(SkyboxSID, Skybox::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(BaseCartSID, BaseCart::BuildTypeUniquePropertyCollection()); // TODO?
		propertyCollectionManager->RegisterType(EmptyCartSID, EmptyCart::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(EngineCartSID, EngineCart::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(MobileLiquidBoxSID, MobileLiquidBox::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(BatterySID, Battery::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(GerstnerWaveSID, GerstnerWave::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(WireSID, Wire::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(WirePlugSID, WirePlug::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(SocketSID, Socket::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(TerminalSID, Terminal::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(TerrainGeneratorSID, TerrainGenerator::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(SpringObjectSID, SpringObject::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(SoftBodySID, SoftBody::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(VehicleSID, Vehicle::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(RoadSID, Road::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(SolarPanelSID, SolarPanel::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(HeadLampSID, HeadLamp::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(PowerPoleSID, PowerPole::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(MineralDepositSID, MineralDeposit::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(MinerSID, Miner::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(SpeakerSID, Speaker::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(PickAxeSID, PickAxe::BuildTypeUniquePropertyCollection());
		propertyCollectionManager->RegisterType(PlayerSID, Player::BuildTypeUniquePropertyCollection());
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
		{
			real timer = player->GetItemPickingTimer();
			if (timer >= 0.0f)
			{
				// Bias so first frame
				real timerAdjusted = timer + (PI - player->GetItemPickingDuration());
				const real mineAnimSpeed = 20.0f;
				const real mineAnimArc = glm::radians(30.0f);
				rotWS = glm::angleAxis(-sin(timerAdjusted * mineAnimSpeed) * mineAnimArc, playerTransform->GetRight()) * rotWS;
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
			// Nothing additional
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

	bool GameObject::ShouldSerializeMesh() const
	{
		switch (m_TypeID)
		{
		case MineralDepositSID:
		case SkyboxSID:
		case GerstnerWaveSID:
		case BaseCartSID:
		case EmptyCartSID:
		case EngineCartSID:
		case WireSID:
		case TerminalSID:
		case TerrainGeneratorSID:
		case SpringObjectSID:
		case RoadSID:
			return false;
		}

		return true;
	}

	bool GameObject::ShouldSerializeMaterial() const
	{
		switch (m_TypeID)
		{
		case BaseCartSID:
		case EmptyCartSID:
		case EngineCartSID:
		case GerstnerWaveSID:
		case WireSID:
		case TerminalSID:
		case TerrainGeneratorSID:
		case SpringObjectSID:
		case RoadSID:
			return false;
		}

		return true;
	}

	void GameObject::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		// Generic game objects have no unique fields
		FLEX_UNUSED(parentObject);
		FLEX_UNUSED(matIDs);
	}

	void GameObject::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		// Generic game objects have no unique fields
		FLEX_UNUSED(parentObject);
		FLEX_UNUSED(bSerializePrefabData);
	}

	void GameObject::SerializeField(JSONObject& parentObject, bool bSerializePrefabData, const char* fieldLabel, void* valuePtr, ValueType valueType, u32 precision /* = 2 */)
	{
		if (!m_SourcePrefabID.IsValid() || bSerializePrefabData)
		{
			// Object isn't loaded from a prefab or we're saving it as a prefab template, ignore overrides
			parentObject.fields.emplace_back(fieldLabel, JSONValue::FromRawPtr(valuePtr, valueType, precision));
			return;
		}

		GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(m_SourcePrefabID);
		if (prefabTemplate == nullptr)
		{
			PrintWarn("Attempted to serialize prefab instance which wasn't loaded from a valid prefab in GameObject::SerializeField (%s)\n", m_Name.c_str());
			return;
		}

		u32 fieldOffset = (u32)((u64)valuePtr - (u64)this);
		void* templateField = ((u8*)prefabTemplate + fieldOffset);

		SerializePrefabInstanceFieldIfUnique(parentObject, fieldLabel, valuePtr, templateField, valueType, precision);
	}

	bool GameObject::SerializeRegisteredProperties(JSONObject& parentObject, PropertyCollection* collection, bool bSerializePrefabData)
	{
		if (m_bIsTemplate)
		{
			return GetPropertyCollectionManager()->SerializePrefabTemplate(m_SourcePrefabID, collection, m_Name.c_str(), parentObject);
		}
		else
		{
			return GetPropertyCollectionManager()->SerializeGameObject(ID, collection, m_Name.c_str(), parentObject, bSerializePrefabData);
		}
	}

	void GameObject::DeserializeRegisteredProperties(const JSONObject& parentObject, PropertyCollection* collection)
	{
		if (m_bIsTemplate)
		{
			GetPropertyCollectionManager()->DeserializePrefabTemplate(this, collection, parentObject);
		}
		else
		{
			GetPropertyCollectionManager()->DeserializeGameObject(this, collection, parentObject);
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
		else
		{
			Mesh* mesh = SetMesh(new Mesh(child));
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

	DirectionalLight::DirectionalLight() :
		DirectionalLight("Directional Light")
	{
	}

	DirectionalLight::DirectionalLight(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, DirectionalLightSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		data.enabled = m_bVisible ? 1 : 0;
		data.dir = VEC3_RIGHT;
		data.colour = VEC3_ONE;
		data.brightness = 1.0f;
		data.castShadows = 1;
		data.shadowDarkness = 1.0f;
		data.pad[0] = data.pad[1] = 0;
	}

	PropertyCollection* DirectionalLight::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("directional light info");

		collection->childCollection->RegisterProperty("colour", FIELD_OFFSET_AND_TYPE(DirectionalLight, data.colour))
			.VersionAdded(2)
			.Precision(2)
			.DefaultValue(VEC3_ONE);
		collection->childCollection->RegisterProperty("brightness", FIELD_OFFSET_AND_TYPE(DirectionalLight, data.brightness))
			.VersionAdded(2)
			.Precision(2)
			.DefaultValue(1.0f);
		collection->childCollection->RegisterProperty("cast shadows", FIELD_OFFSET_AND_TYPE(DirectionalLight, data.castShadows))
			.VersionAdded(2)
			.DefaultValue(1);
		collection->childCollection->RegisterProperty("shadow darkness", FIELD_OFFSET_AND_TYPE(DirectionalLight, data.shadowDarkness))
			.VersionAdded(2)
			.Precision(2)
			.DefaultValue(1.0f);

		return collection;
	}

	void DirectionalLight::Initialize()
	{
		g_Renderer->RegisterDirectionalLight(this);
		data.dir = glm::rotate(m_Transform.GetWorldRotation(), VEC3_RIGHT);

		GameObject::Initialize();
	}

	void DirectionalLight::Destroy(bool bDetachFromParent /* = true */)
	{
		g_Renderer->RemoveDirectionalLight();

		GameObject::Destroy(bDetachFromParent);
	}

	void DirectionalLight::Update()
	{
		PROFILE_AUTO("DirectionalLight Update");

		PluggablesSystem* pluggablesSystem = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES);
		std::vector<SocketData> const* sockets = pluggablesSystem->GetGameObjectSockets(ID);
		if (sockets != nullptr)
		{
			Socket* socket = (Socket*)((*sockets)[0].socketID.Get());
			i32 receivedSignal = pluggablesSystem->GetReceivedSignal(socket);
			if (receivedSignal != -1)
			{
				m_bVisible = receivedSignal == 1;
				data.enabled = m_bVisible ? 1 : 0;
			}
		}

		if (data.enabled && g_EngineInstance->IsRenderingEditorObjects())
		{
			pos = m_Transform.GetWorldPosition();
			data.dir = glm::rotate(m_Transform.GetWorldRotation(), -VEC3_FORWARD);

			BaseCamera* cam = g_CameraManager->CurrentCamera();

			if (!cam->bIsGameplayCam)
			{
				// Debug lines
				{
					DebugRenderer* debugRenderer = g_Renderer->GetDebugRenderer();

					glm::vec3 forward = m_Transform.GetForward();
					glm::vec3 right = m_Transform.GetRight();
					glm::vec3 up = m_Transform.GetUp();
					real lineLength = 2.0f;
					real spacing = 0.75f;
					glm::vec3 scaledForward = forward * lineLength;
					glm::vec3 offsets[] = { -up, up, -right, right, VEC3_ZERO };
					btVector3 lineColour = ToBtVec3(data.colour);
					for (const glm::vec3& offset : offsets)
					{
						glm::vec3 basePos = pos + offset * spacing;
						btVector3 lightPos = ToBtVec3(basePos);
						btVector3 lineEnd = ToBtVec3(basePos + scaledForward);
						debugRenderer->drawLine(lightPos, lineEnd, lineColour);
					}
				}

				// Sprite
				{
					const real minSpriteDist = 1.5f;
					const real maxSpriteDist = 3.0f;

					glm::vec3 scale(1.0f, -1.0f, 1.0f);

					SpriteQuadDrawInfo drawInfo = {};
					drawInfo.bScreenSpace = false;
					drawInfo.bReadDepth = true;
					drawInfo.scale = scale;
					drawInfo.materialID = g_Renderer->m_SpriteMatWSID;

					glm::vec3 camPos = cam->position;
					glm::vec3 camUp = cam->up;
					drawInfo.textureID = g_Renderer->directionalLightIconID;
					drawInfo.pos = pos;
					glm::mat4 rotMat = glm::lookAt(camPos, pos, camUp);
					drawInfo.rotation = glm::conjugate(glm::toQuat(rotMat));
					real alpha = Saturate(glm::distance(drawInfo.pos, camPos) / maxSpriteDist - minSpriteDist);
					drawInfo.colour = glm::vec4(data.colour * 1.5f, alpha);
					g_Renderer->EnqueueSprite(drawInfo);
				}
			}
		}

		GameObject::Update();
	}

	void DirectionalLight::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		static const ImGuiColorEditFlags colorEditFlags =
			ImGuiColorEditFlags_NoInputs |
			ImGuiColorEditFlags_Float |
			ImGuiColorEditFlags_RGB |
			ImGuiColorEditFlags_PickerHueWheel |
			ImGuiColorEditFlags_HDR;

		ImGui::Text("Directional Light");

		bool bVisible = m_bVisible;
		if (ImGui::Checkbox("Enabled", &bVisible))
		{
			m_bVisible = bVisible;
			data.enabled = bVisible ? 1 : 0;
		}

		ImGui::SameLine();
		ImGui::ColorEdit4("Colour ", &data.colour.r, colorEditFlags);
		ImGui::SliderFloat("Brightness", &data.brightness, 0.0f, 15.0f);

		ImGui::Spacing();
		ImGui::Text("Shadow");

		bool bCastShadows = data.castShadows != 0;
		if (ImGui::Checkbox("Cast shadow", &bCastShadows))
		{
			data.castShadows = bCastShadows ? 1 : 0;
		}
		ImGui::SliderFloat("Shadow darkness", &data.shadowDarkness, 0.0f, 1.0f);
	}

	void DirectionalLight::SetVisible(bool bVisible, bool bEffectChildren /* = true */)
	{
		data.enabled = (bVisible ? 1 : 0);
		GameObject::SetVisible(bVisible, bEffectChildren);
	}

	void DirectionalLight::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		i32 fileVersion = g_SceneManager->CurrentScene()->GetSceneFileVersion();

		JSONObject directionalLightObj;
		if (parentObject.TryGetObject("directional light info", directionalLightObj))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(directionalLightObj, collection->childCollection);

			if (fileVersion < 2)
			{
				// Rotation is saved using euler angles in these versions
				glm::vec3 rot = directionalLightObj.GetVec3("rotation");
				m_Transform.SetWorldRotation(glm::quat(rot));
			}
			else if (fileVersion <= 6)
			{
				// Rotation is saved explicitly in these versions
				glm::quat rot = directionalLightObj.GetQuat("rotation");
				m_Transform.SetWorldRotation(rot);
				data.dir = glm::rotate(m_Transform.GetWorldRotation(), VEC3_RIGHT);
			}
			else
			{
				// Rotation is saved simply through the transform in this version
			}

			data.enabled = m_bVisible ? 1 : 0;
		}
	}

	void DirectionalLight::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject dirLightObj = {};
		SerializeRegisteredProperties(dirLightObj, collection->childCollection, bSerializePrefabData);
		if (!dirLightObj.fields.empty())
		{
			parentObject.fields.emplace_back("directional light info", JSONValue(dirLightObj));
		}
	}

	bool DirectionalLight::operator==(const DirectionalLight& other)
	{
		return other.m_Transform.GetLocalRotation() == m_Transform.GetLocalRotation() &&
			other.data.dir == data.dir &&
			other.data.colour == data.colour &&
			other.m_bVisible == m_bVisible &&
			other.data.brightness == data.brightness;
	}

	PointLight::PointLight(BaseScene* scene) :
		PointLight(scene->GetUniqueObjectName("PointLight_", 2))
	{
	}

	PointLight::PointLight(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, PointLightSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		data.enabled = 1;
		data.pos = m_Transform.GetWorldPosition();
		data.colour = VEC3_ONE;
		data.brightness = 500.0f;
	}

	PropertyCollection* PointLight::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("point light info");

		collection->childCollection->RegisterProperty("colour", FIELD_OFFSET_AND_TYPE(PointLight, data.colour))
			.VersionAdded(2)
			.Precision(2)
			.DefaultValue(VEC3_ONE);
		collection->childCollection->RegisterProperty("brightness", FIELD_OFFSET_AND_TYPE(PointLight, data.brightness))
			.VersionAdded(2)
			.Precision(3)
			.DefaultValue(1.0f);
		collection->childCollection->RegisterProperty("enabled", FIELD_OFFSET_AND_TYPE(PointLight, data.enabled))
			.VersionAdded(2)
			.DefaultValue(1);

		return collection;
	}

	GameObject* PointLight::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);

		if (g_Renderer->GetNumPointLights() >= MAX_POINT_LIGHT_COUNT)
		{
			PrintError("Failed to duplicate point light, max number already in scene (%i)\n", MAX_POINT_LIGHT_COUNT);
			return nullptr;
		}

		PointLight* newGameObject = new PointLight(newObjectName, newGameObjectID);

		memcpy(&newGameObject->data, &data, sizeof(PointLightData));
		// newGameObject->pointLightID will be filled out in Initialize

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void PointLight::Initialize()
	{
		if (pointLightID == InvalidPointLightID)
		{
			pointLightID = g_Renderer->RegisterPointLight(&data);
			if (pointLightID == InvalidPointLightID)
			{
				PrintError("More point lights added to scene than possible (%d). Additional lights will be disabled\n", MAX_POINT_LIGHT_COUNT);
				m_bVisible = false;
				data.enabled = false;
			}
		}

		GameObject::Initialize();
	}

	void PointLight::Destroy(bool bDetachFromParent /* = true */)
	{
		if (pointLightID != InvalidPointLightID)
		{
			g_Renderer->RemovePointLight(pointLightID);
			pointLightID = InvalidPointLightID;
		}

		GameObject::Destroy(bDetachFromParent);
	}

	void PointLight::Update()
	{
		PROFILE_AUTO("PointLight Update");

		PluggablesSystem* pluggablesSystem = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES);
		std::vector<SocketData> const* sockets = pluggablesSystem->GetGameObjectSockets(ID);
		if (sockets != nullptr)
		{
			Socket* socket = (Socket*)((*sockets)[0].socketID.Get());
			i32 receivedSignal = pluggablesSystem->GetReceivedSignal(socket);
			if (receivedSignal != -1)
			{
				m_bVisible = receivedSignal == 1;
				data.enabled = m_bVisible ? 1 : 0;
				g_Renderer->UpdatePointLightData(pointLightID, &data);
			}
		}

		if (data.enabled)
		{
			data.pos = m_Transform.GetWorldPosition();
			g_Renderer->UpdatePointLightData(pointLightID, &data);

			if (g_EngineInstance->IsRenderingEditorObjects())
			{
				BaseCamera* cam = g_CameraManager->CurrentCamera();

				if (!cam->bIsGameplayCam)
				{
					const real minSpriteDist = 1.5f;
					const real maxSpriteDist = 3.0f;

					glm::vec3 scale(1.0f, -1.0f, 1.0f);

					SpriteQuadDrawInfo drawInfo = {};
					drawInfo.bScreenSpace = false;
					drawInfo.bReadDepth = true;
					drawInfo.scale = scale;
					drawInfo.materialID = g_Renderer->m_SpriteMatWSID;

					glm::vec3 camPos = cam->position;
					glm::vec3 camUp = cam->up;

					drawInfo.textureID = g_Renderer->pointLightIconID;
					// TODO: Sort back to front? Or clear depth and then enable depth test
					drawInfo.pos = data.pos;
					glm::mat4 rotMat = glm::lookAt(drawInfo.pos, camPos, camUp);
					drawInfo.rotation = glm::conjugate(glm::toQuat(rotMat));
					real alpha = Saturate(glm::distance(drawInfo.pos, camPos) / maxSpriteDist - minSpriteDist);
					drawInfo.colour = glm::vec4(data.colour * 1.5f, alpha);
					g_Renderer->EnqueueSprite(drawInfo);
				}
			}
		}

		GameObject::Update();
	}

	void PointLight::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		if (pointLightID != InvalidPointLightID)
		{
			static const ImGuiColorEditFlags colorEditFlags =
				ImGuiColorEditFlags_NoInputs |
				ImGuiColorEditFlags_Float |
				ImGuiColorEditFlags_RGB |
				ImGuiColorEditFlags_PickerHueWheel |
				ImGuiColorEditFlags_HDR;

			ImGui::Text("Point Light");
			bool bRemovedPointLight = false;
			bool bEditedPointLightData = false;

			if (!bRemovedPointLight)
			{
				bool bVisible = m_bVisible;
				if (ImGui::Checkbox("Enabled", &bVisible))
				{
					m_bVisible = bVisible;
					bEditedPointLightData = true;
					data.enabled = bVisible ? 1 : 0;
				}

				ImGui::SameLine();
				bEditedPointLightData |= ImGui::ColorEdit4("Colour ", &data.colour.r, colorEditFlags);
				bEditedPointLightData |= ImGui::SliderFloat("Brightness", &data.brightness, 0.0f, 1000.0f);

				if (bEditedPointLightData)
				{
					g_Renderer->UpdatePointLightData(pointLightID, &data);
				}
			}
		}
	}

	void PointLight::SetVisible(bool bVisible, bool bEffectChildren /* = true */)
	{
		data.enabled = (bVisible ? 1 : 0);
		GameObject::SetVisible(bVisible, bEffectChildren);

		if (pointLightID != InvalidPointLightID)
		{
			g_Renderer->UpdatePointLightData(pointLightID, &data);
		}
	}

	void PointLight::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject pointLightObj;
		if (parentObject.TryGetObject("point light info", pointLightObj))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(pointLightObj, collection->childCollection);
		}
	}

	void PointLight::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject pointLightObj = {};
		SerializeRegisteredProperties(pointLightObj, collection->childCollection, bSerializePrefabData);
		if (!pointLightObj.fields.empty())
		{
			parentObject.fields.emplace_back("point light info", JSONValue(pointLightObj));
		}
	}

	bool PointLight::operator==(const PointLight& other)
	{
		return other.data.pos == data.pos &&
			other.data.colour == data.colour &&
			other.data.enabled == data.enabled &&
			other.data.brightness == data.brightness;
	}

	SpotLight::SpotLight(BaseScene* scene) :
		SpotLight(scene->GetUniqueObjectName("SpotLight_", 2))
	{
	}

	SpotLight::SpotLight(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, SpotLightSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		data.enabled = 1;
		data.pos = m_Transform.GetWorldPosition();
		data.colour = VEC3_ONE;
		data.brightness = 500.0f;
		data.dir = VEC3_RIGHT;
		data.angle = 0.0f;
	}

	PropertyCollection* SpotLight::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("spot light info");

		collection->childCollection->RegisterProperty("colour", FIELD_OFFSET_AND_TYPE(SpotLight, data.colour))
			.VersionAdded(2)
			.Precision(2)
			.DefaultValue(VEC3_ONE);
		collection->childCollection->RegisterProperty("brightness", FIELD_OFFSET_AND_TYPE(SpotLight, data.brightness))
			.VersionAdded(2)
			.Precision(3)
			.DefaultValue(1.0f);
		collection->childCollection->RegisterProperty("enabled", FIELD_OFFSET_AND_TYPE(SpotLight, data.enabled))
			.VersionAdded(2)
			.DefaultValue(1);
		collection->childCollection->RegisterProperty("angle", FIELD_OFFSET_AND_TYPE(SpotLight, data.angle))
			.VersionAdded(2)
			.Precision(3)
			.DefaultValue(0.0f);

		return collection;
	}

	GameObject* SpotLight::CopySelf(GameObject* parent, CopyFlags copyFlags, std::string* optionalName, const GameObjectID& optionalGameObjectID)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);

		if (g_Renderer->GetNumSpotLights() >= MAX_SPOT_LIGHT_COUNT)
		{
			PrintError("Failed to duplicate spot light, max number already in scene (%i)\n", MAX_SPOT_LIGHT_COUNT);
			return nullptr;
		}

		SpotLight* newGameObject = new SpotLight(newObjectName, newGameObjectID);

		memcpy(&newGameObject->data, &data, sizeof(SpotLightData));
		// newGameObject->spotLightID will be filled out in Initialize

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void SpotLight::Initialize()
	{
		if (spotLightID == InvalidSpotLightID)
		{
			spotLightID = g_Renderer->RegisterSpotLight(&data);
			if (spotLightID == InvalidSpotLightID)
			{
				PrintError("More spot lights added to scene than possible (%d). Additional lights will be disabled\n", MAX_SPOT_LIGHT_COUNT);
				m_bVisible = false;
				data.enabled = 0;
			}
		}

		GameObject::Initialize();
	}

	void SpotLight::Destroy(bool bDetachFromParent)
	{
		if (spotLightID != InvalidSpotLightID)
		{
			g_Renderer->RemoveSpotLight(spotLightID);
			spotLightID = InvalidSpotLightID;

			GameObject::Destroy(bDetachFromParent);
		}
	}

	void SpotLight::Update()
	{
		PROFILE_AUTO("SpotLight Update");

		PluggablesSystem* pluggablesSystem = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES);
		std::vector<SocketData> const* sockets = pluggablesSystem->GetGameObjectSockets(ID);
		if (sockets != nullptr)
		{
			Socket* socket = (Socket*)((*sockets)[0].socketID.Get());
			i32 receivedSignal = pluggablesSystem->GetReceivedSignal(socket);
			if (receivedSignal != -1)
			{
				m_bVisible = receivedSignal == 1;
				data.enabled = m_bVisible ? 1 : 0;
				g_Renderer->UpdateSpotLightData(spotLightID, &data);
			}
		}

		if (data.enabled)
		{
			data.pos = m_Transform.GetWorldPosition();
			data.dir = glm::rotate(m_Transform.GetWorldRotation(), -VEC3_FORWARD);
			g_Renderer->UpdateSpotLightData(spotLightID, &data);

			if (g_EngineInstance->IsRenderingEditorObjects())
			{
				BaseCamera* cam = g_CameraManager->CurrentCamera();

				if (!cam->bIsGameplayCam)
				{
					const real minSpriteDist = 1.5f;
					const real maxSpriteDist = 3.0f;

					glm::vec3 scale(1.0f, -1.0f, 1.0f);

					SpriteQuadDrawInfo drawInfo = {};
					drawInfo.bScreenSpace = false;
					drawInfo.bReadDepth = true;
					drawInfo.scale = scale;
					drawInfo.materialID = g_Renderer->m_SpriteMatWSID;

					glm::vec3 camPos = cam->position;
					glm::vec3 camUp = cam->up;

					drawInfo.textureID = g_Renderer->spotLightIconID;
					// TODO: Sort back to front? Or clear depth and then enable depth test
					drawInfo.pos = data.pos;
					glm::mat4 rotMat = glm::lookAt(drawInfo.pos, camPos, camUp);
					drawInfo.rotation = glm::conjugate(glm::toQuat(rotMat));
					real alpha = Saturate(glm::distance(drawInfo.pos, camPos) / maxSpriteDist - minSpriteDist);
					drawInfo.colour = glm::vec4(data.colour * 1.5f, alpha);
					g_Renderer->EnqueueSprite(drawInfo);
				}
			}
		}

		GameObject::Update();
	}

	void SpotLight::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		if (spotLightID != InvalidSpotLightID)
		{
			static const ImGuiColorEditFlags colorEditFlags =
				ImGuiColorEditFlags_NoInputs |
				ImGuiColorEditFlags_Float |
				ImGuiColorEditFlags_RGB |
				ImGuiColorEditFlags_PickerHueWheel |
				ImGuiColorEditFlags_HDR;

			ImGui::Text("Spot Light");
			bool bRemovedSpotLight = false;
			bool bEditedSpotLightData = false;

			if (!bRemovedSpotLight)
			{
				bool bVisible = m_bVisible;
				if (ImGui::Checkbox("Enabled", &bVisible))
				{
					m_bVisible = bVisible;
					bEditedSpotLightData = true;
					data.enabled = bVisible ? 1 : 0;
				}

				ImGui::SameLine();
				bEditedSpotLightData |= ImGui::ColorEdit4("Colour ", &data.colour.r, colorEditFlags);
				bEditedSpotLightData |= ImGui::SliderFloat("Brightness", &data.brightness, 0.0f, 1000.0f);
				bEditedSpotLightData |= ImGui::SliderFloat("Cone angle", &data.angle, 0.0f, 1.0f);

				if (bEditedSpotLightData)
				{
					g_Renderer->UpdateSpotLightData(spotLightID, &data);
				}
			}
		}
	}

	void SpotLight::SetVisible(bool bVisible, bool bEffectChildren)
	{
		data.enabled = (bVisible ? 1 : 0);
		GameObject::SetVisible(bVisible, bEffectChildren);

		if (spotLightID != InvalidSpotLightID)
		{
			g_Renderer->UpdateSpotLightData(spotLightID, &data);
		}
	}

	bool SpotLight::operator==(const SpotLight& other)
	{
		return other.data.pos == data.pos &&
			other.data.colour == data.colour &&
			other.data.enabled == data.enabled &&
			other.data.brightness == data.brightness &&
			other.data.dir == data.dir &&
			other.data.angle == data.angle;
	}

	void SpotLight::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject spotLightObj;
		if (parentObject.TryGetObject("spot light info", spotLightObj))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(spotLightObj, collection->childCollection);
		}
	}

	void SpotLight::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject spotLightObj = {};
		SerializeRegisteredProperties(spotLightObj, collection->childCollection, bSerializePrefabData);
		if (!spotLightObj.fields.empty())
		{
			parentObject.fields.emplace_back("spot light info", JSONValue(spotLightObj));
		}
	}

	AreaLight::AreaLight(BaseScene* scene) :
		AreaLight(scene->GetUniqueObjectName("AreaLight_", 2))
	{
	}

	AreaLight::AreaLight(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, AreaLightSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		data.enabled = 1;
		data.colour = VEC3_ONE;
		data.brightness = 500.0f;
		UpdatePoints();
}

	PropertyCollection* AreaLight::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("area light info");

		collection->childCollection->RegisterProperty("colour", FIELD_OFFSET_AND_TYPE(AreaLight, data.colour))
			.VersionAdded(2)
			.Precision(2)
			.DefaultValue(VEC3_ONE);
		collection->childCollection->RegisterProperty("brightness", FIELD_OFFSET_AND_TYPE(AreaLight, data.brightness))
			.VersionAdded(2)
			.DefaultValue(500.0f);
		collection->childCollection->RegisterProperty("enabled", FIELD_OFFSET_AND_TYPE(AreaLight, data.enabled))
			.VersionAdded(2)
			.DefaultValue(1);

		return collection;
	}

	GameObject* AreaLight::CopySelf(GameObject* parent, CopyFlags copyFlags, std::string* optionalName, const GameObjectID& optionalGameObjectID)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);

		if (g_Renderer->GetNumAreaLights() >= MAX_AREA_LIGHT_COUNT)
		{
			PrintError("Failed to duplicate area light, max number already in scene (%i)\n", MAX_AREA_LIGHT_COUNT);
			return nullptr;
		}

		AreaLight* newGameObject = new AreaLight(newObjectName, newGameObjectID);

		memcpy(&newGameObject->data, &data, sizeof(AreaLightData));
		// newGameObject->areaLightID will be filled out in Initialize

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void AreaLight::Initialize()
	{
		if (areaLightID == InvalidAreaLightID)
		{
			areaLightID = g_Renderer->RegisterAreaLight(&data);
			if (areaLightID == InvalidAreaLightID)
			{
				PrintError("More area lights added to scene than possible (%d). Additional lights will be disabled\n", MAX_AREA_LIGHT_COUNT);
				m_bVisible = false;
				data.enabled = false;
			}
		}

		GameObject::Initialize();
	}

	void AreaLight::Destroy(bool bDetachFromParent)
	{
		if (areaLightID != InvalidAreaLightID)
		{
			g_Renderer->RemoveAreaLight(areaLightID);
			areaLightID = InvalidAreaLightID;
		}

		GameObject::Destroy(bDetachFromParent);
	}

	void AreaLight::Update()
	{
		PROFILE_AUTO("AreaLight Update");

		PluggablesSystem* pluggablesSystem = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES);
		std::vector<SocketData> const* sockets = pluggablesSystem->GetGameObjectSockets(ID);
		if (sockets != nullptr)
		{
			Socket* socket = (Socket*)((*sockets)[0].socketID.Get());
			i32 receivedSignal = pluggablesSystem->GetReceivedSignal(socket);
			if (receivedSignal != -1)
			{
				m_bVisible = receivedSignal == 1;
				data.enabled = m_bVisible ? 1 : 0;

				if (areaLightID != InvalidAreaLightID)
				{
					g_Renderer->UpdateAreaLightData(areaLightID, &data);
				}
			}
		}

		if (data.enabled)
		{
			UpdatePoints();

			g_Renderer->UpdateAreaLightData(areaLightID, &data);

			if (g_EngineInstance->IsRenderingEditorObjects())
			{
				DebugRenderer* debugRenderer = g_Renderer->GetDebugRenderer();

				btVector3 lineColour(0.1f, 0.8f, 0.1f);
				btVector3 p0(ToBtVec3(data.points[0]));
				btVector3 p1(ToBtVec3(data.points[1]));
				btVector3 p2(ToBtVec3(data.points[2]));
				btVector3 p3(ToBtVec3(data.points[3]));
				btVector3 center(ToBtVec3((data.points[0] + data.points[1] + data.points[2] + data.points[3]) / 4.0f));
				glm::vec3 dir = glm::normalize(glm::cross(glm::vec3(data.points[1]) - glm::vec3(data.points[0]), glm::vec3(data.points[2]) - glm::vec3(data.points[0])));
				btVector3 dirBt = ToBtVec3(dir);
				debugRenderer->drawLine(p0, p1, lineColour);
				debugRenderer->drawLine(p1, p2, lineColour);
				debugRenderer->drawLine(p2, p3, lineColour);
				debugRenderer->drawLine(p3, p0, lineColour);

				debugRenderer->drawLine(center, center + dirBt, lineColour);

				//BaseCamera* cam = g_CameraManager->CurrentCamera();
				//
				//if (!cam->bIsGameplayCam)
				//{
				//	const real minSpriteDist = 1.5f;
				//	const real maxSpriteDist = 3.0f;
				//
				//	glm::vec3 scale(1.0f, -1.0f, 1.0f);
				//
				//	SpriteQuadDrawInfo drawInfo = {};
				//	drawInfo.bScreenSpace = false;
				//	drawInfo.bReadDepth = true;
				//	drawInfo.scale = scale;
				//	drawInfo.materialID = g_Renderer->m_SpriteMatWSID;
				//
				//	glm::vec3 camPos = cam->position;
				//	glm::vec3 camUp = cam->up;
				//
				//	drawInfo.textureID = g_Renderer->areaLightIconID;
				//	// TODO: Sort back to front? Or clear depth and then enable depth test
				//	drawInfo.pos = m_Transform.GetWorldPosition();
				//	glm::mat4 rotMat = glm::lookAt(drawInfo.pos, camPos, camUp);
				//	drawInfo.rotation = glm::conjugate(glm::toQuat(rotMat));
				//	real alpha = Saturate(glm::distance(drawInfo.pos, camPos) / maxSpriteDist - minSpriteDist);
				//	drawInfo.colour = glm::vec4(data.colour * 1.5f, alpha);
				//	g_Renderer->EnqueueSprite(drawInfo);
				//}
			}
		}

		GameObject::Update();
	}

	void AreaLight::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		if (areaLightID != InvalidAreaLightID)
		{
			static const ImGuiColorEditFlags colorEditFlags =
				ImGuiColorEditFlags_NoInputs |
				ImGuiColorEditFlags_Float |
				ImGuiColorEditFlags_RGB |
				ImGuiColorEditFlags_PickerHueWheel |
				ImGuiColorEditFlags_HDR;

			ImGui::Text("Area Light");
			bool bRemovedAreaLight = false;
			bool bEditedAreaLightData = false;

			if (!bRemovedAreaLight)
			{
				bool bVisible = m_bVisible;
				if (ImGui::Checkbox("Enabled", &bVisible))
				{
					m_bVisible = bVisible;
					bEditedAreaLightData = true;
					data.enabled = bVisible ? 1 : 0;
				}

				ImGui::SameLine();
				bEditedAreaLightData |= ImGui::ColorEdit4("Colour ", &data.colour.r, colorEditFlags);
				bEditedAreaLightData |= ImGui::SliderFloat("Brightness", &data.brightness, 0.0f, 1000.0f);

				if (bEditedAreaLightData)
				{
					g_Renderer->UpdateAreaLightData(areaLightID, &data);
				}
			}
		}
	}

	void AreaLight::SetVisible(bool bVisible, bool bEffectChildren)
	{
		data.enabled = (bVisible ? 1 : 0);
		GameObject::SetVisible(bVisible, bEffectChildren);

		if (areaLightID != InvalidAreaLightID)
		{
			g_Renderer->UpdateAreaLightData(areaLightID, &data);
		}
	}

	bool AreaLight::operator==(const AreaLight& other)
	{
		return other.data.colour == data.colour &&
			other.data.enabled == data.enabled &&
			other.data.brightness == data.brightness;
	}

	void AreaLight::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject areaLightObj;
		if (parentObject.TryGetObject("area light info", areaLightObj))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(areaLightObj, collection->childCollection);
		}
	}

	void AreaLight::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject areaLightObj = {};
		SerializeRegisteredProperties(areaLightObj, collection->childCollection, bSerializePrefabData);
		if (!areaLightObj.fields.empty())
		{
			parentObject.fields.emplace_back("area light info", JSONValue(areaLightObj));
		}
	}

	void AreaLight::UpdatePoints()
	{
		const glm::mat4& model = m_Transform.GetWorldTransform();
		data.points[0] = model * glm::vec4(-1.0f, 1.0f, 0.0f, 1.0f);
		data.points[1] = model * glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
		data.points[2] = model * glm::vec4(1.0f, -1.0f, 0.0f, 1.0f);
		data.points[3] = model * glm::vec4(-1.0f, -1.0f, 0.0f, 1.0f);
	}

	BaseCart::BaseCart(const std::string& name,
		StringID typeID,
		const GameObjectID& gameObjectID,
		const char* meshName,
		const PrefabIDPair& sourcePrefabID,
		bool bPrefabTemplate) :
		GameObject(name, typeID, gameObjectID, sourcePrefabID, bPrefabTemplate)
	{
		m_bItemizable = true;

		MaterialID matID;
		if (!g_Renderer->FindOrCreateMaterialByName("pbr grey", matID))
		{
			// :shrug:
			// TODO: Create own material
			matID = 0;
		}
		Mesh* mesh = SetMesh(new Mesh(this));
		Mesh::CreateInfo meshCreateInfo = {};
		meshCreateInfo.relativeFilePath = std::string(MESH_DIRECTORY) + std::string(meshName);
		meshCreateInfo.materialIDs = { matID };
		meshCreateInfo.bCreateRenderObject = !bPrefabTemplate;
		if (!mesh->LoadFromFile(meshCreateInfo))
		{
			PrintWarn("Failed to load cart mesh!\n");
		}

		m_TSpringToCartAhead.DR = 1.0f;
	}

	PropertyCollection* BaseCart::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("cart info");

		collection->childCollection->RegisterProperty("track ID", FIELD_OFFSET_AND_TYPE(BaseCart, currentTrackID))
			.VersionAdded(6)
			.DefaultValue(InvalidTrackID);
		collection->childCollection->RegisterProperty("dist along track", FIELD_OFFSET_AND_TYPE(BaseCart, distAlongTrack))
			.VersionAdded(6)
			.DefaultValue(-1.0f);

		return collection;
	}

	void BaseCart::Initialize()
	{
		GetSystem<CartManager>(SystemType::CART_MANAGER)->RegisterCart(this);

		GameObject::Initialize();
	}

	void BaseCart::Destroy(bool bDetachFromParent)
	{
		GetSystem<CartManager>(SystemType::CART_MANAGER)->DeregisterCart(this);

		GameObject::Destroy(bDetachFromParent);
	}

	void BaseCart::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		if (ImGui::TreeNode("BaseCart"))
		{
			ImGui::Text("track ID: %d", currentTrackID);
			ImGui::Text("dist along track: %.2f", distAlongTrack);

			ImGui::TreePop();
		}
	}

	real BaseCart::GetDrivePower() const
	{
		return 0.0f;
	}

	void BaseCart::OnTrackMount(TrackID trackID, real newDistAlongTrack)
	{
		if (trackID == InvalidTrackID)
		{
			PrintWarn("Attempted to attach cart to track with invalid ID!\n");
		}
		else
		{
			currentTrackID = trackID;

			TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
			i32 curveIndex, juncIndex;
			TrackID newTrackID = trackID;
			TrackState trackState;
			glm::vec3 newPos = trackManager->GetPointOnTrack(currentTrackID, newDistAlongTrack, newDistAlongTrack,
				LookDirection::CENTER, false, &newTrackID, &newDistAlongTrack, &juncIndex, &curveIndex, &trackState, false);
			CHECK_EQ(newTrackID, trackID);

			distAlongTrack = newDistAlongTrack;
			m_Transform.SetLocalPosition(newPos);

			velocityT = (distAlongTrack > 0.5f ? -1.0f : 1.0f);
		}
	}

	void BaseCart::OnTrackDismount()
	{
		currentTrackID = InvalidTrackID;
		distAlongTrack = -1.0f;
	}

	void BaseCart::SetItemHolding(GameObject* obj)
	{
		FLEX_UNUSED(obj);
		// TODO:
	}

	void BaseCart::RemoveItemHolding()
	{
	}

	void BaseCart::AdvanceAlongTrack(real dT)
	{
		if (currentTrackID != InvalidTrackID)
		{
			TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);

			real pDistAlongTrack = distAlongTrack;
			distAlongTrack = trackManager->AdvanceTAlongTrack(currentTrackID, dT, distAlongTrack);

			real newDistAlongTrack;
			i32 curveIndex;
			i32 juncIndex;
			TrackID newTrackID = InvalidTrackID;
			TrackState trackState;
			glm::vec3 newPos = trackManager->GetPointOnTrack(currentTrackID, distAlongTrack, pDistAlongTrack,
				LookDirection::CENTER, false, &newTrackID, &newDistAlongTrack, &juncIndex, &curveIndex, &trackState, false);

			bool bSwitchedTracks = (newTrackID != InvalidTrackID) && (currentTrackID != newTrackID);
			if (bSwitchedTracks)
			{
				currentTrackID = newTrackID;
				distAlongTrack = newDistAlongTrack;

				velocityT = (distAlongTrack > 0.5f ? -1.0f : 1.0f);
			}

			if (currentTrackID != InvalidTrackID)
			{
				glm::vec3 trackF = trackManager->GetTrack(currentTrackID)->GetCurveDirectionAt(distAlongTrack);
				glm::quat pRot = m_Transform.GetWorldRotation();
				glm::quat newRot = glm::quatLookAt(trackF, VEC3_UP);
				// TODO: Make frame-rate independent
				m_Transform.SetWorldRotation(glm::slerp(pRot, newRot, 0.5f));
			}

			m_Transform.SetWorldPosition(newPos);
		}
	}

	real BaseCart::UpdatePosition()
	{
		if (currentTrackID != InvalidTrackID)
		{
			if (chainID != InvalidCartChainID)
			{
				TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
				CartManager* cartManager = GetSystem<CartManager>(SystemType::CART_MANAGER);

				//real targetT = trackManager->GetCartTargetDistAlongTrackInChain(chainID, cartID);
				//CHECK_NE(targetT, -1.0f);

				//m_TSpringToCartAhead.targetPos = targetT;
				//m_TSpringToCartAhead.Tick(g_DeltaTime);
				//real dT = (m_TSpringToCartAhead.pos - distAlongTrack);

				//real newDistAlongTrack = trackManager->AdvanceTAlongTrack(currentTrackID, g_DeltaTime*0.1f, distAlongTrack);
				//real dT = newDistAlongTrack - distAlongTrack;

				//real minDist = 0.05f;
				//real d = (targetT - distAlongTrack);
				//real stepSize = 0.08f;
				//if (velocityT < 0.0f && targetT > 0.75f && distAlongTrack < 0.25f)
				//{
				//	// Target has crossed 0.0->1.0
				//	d = (targetT - (distAlongTrack + 1.0f));

				//}
				//else if (velocityT > 0.0f && targetT < 0.25f && distAlongTrack > 0.75f)
				//{
				//	// Target has crossed 1.0->0.0
				//	d = (targetT - (distAlongTrack - 1.0f));
				//}

				//{
				//	if (glm::abs(d) <= minDist)
				//	{
				//		targetT = d >= 0.0f ? targetT - minDist : targetT + minDist;
				//		d = (targetT - distAlongTrack);
				//	}
				//	if (d < 0.0f)
				//	{
				//		stepSize = -stepSize;
				//	}
				//}
				//real dA = d >= 0.0f ? std::min(d, stepSize) : -std::min(-d, -stepSize);
				//Print("%.2f, %.2f, %.2f\n", targetT, distAlongTrack, dA);

				//velocityT += dA * g_DeltaTime;
				//real dT = velocityT * g_DeltaTime;

				//real pDistAlongTrack = distAlongTrack;
				//real newDistAlongTrack = trackManager->AdvanceTAlongTrack(currentTrackID, g_DeltaTime*chainDrivePower, distAlongTrack);

				//TrackID newTrackID;
				//real newDistAlongTrack;
				//i32 newJunctionIdx;
				//i32 newCurveIdx;
				//TrackState newTrackState;
				//trackManager->GetPointOnTrack(currentTrackID, newDistAlongTrack, distAlongTrack, LookDirection::CENTER, false, &newTrackID, &newDistAlongTrack, &newJunctionIdx, &newCurveIdx, &newTrackState, true);

				//if (newTrackID != currentTrackID)
				//{
				//	currentTrackID = newTrackID;
				//}

				//distAlongTrack = newDistAlongTrack;


				const CartChain* chain = cartManager->GetCartChain(chainID);
				i32 cartInChainIndex = chain->GetCartIndex(cartID);

				//real pDistToRearNeighbor = distToRearNeighbor;
				if (cartInChainIndex < (i32)chain->carts.size() - 1)
				{
					// Not at end
					distToRearNeighbor = trackManager->GetCartTargetDistAlongTrackInChain(chainID, chain->carts[cartInChainIndex + 1]);
				}

				//real neighborDT = distToRearNeighbor - pDistToRearNeighbor;

				real chainDrivePower = cartManager->GetChainDrivePower(chainID);

				if (distToRearNeighbor > -1.0f && distToRearNeighbor < 0.2f)
				{
					chainDrivePower += 1.0f;
				}


				real dT = g_DeltaTime * chainDrivePower * velocityT;
				AdvanceAlongTrack(dT);
				return dT;
			}
		}

		return 0.0f;
	}

	void BaseCart::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject cartInfo;
		if (parentObject.TryGetObject("cart info", cartInfo))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(cartInfo, collection->childCollection);
		}
	}

	void BaseCart::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject cartInfo = {};
		SerializeRegisteredProperties(cartInfo, collection->childCollection, bSerializePrefabData);
		if (!cartInfo.fields.empty())
		{
			parentObject.fields.emplace_back("cart info", JSONValue(cartInfo));
		}
	}

	EmptyCart::EmptyCart(const std::string& name,
		const GameObjectID& gameObjectID,
		const PrefabIDPair& sourcePrefabID,
		bool bPrefabTemplate) :
		BaseCart(name, EmptyCartSID, gameObjectID, BaseCart::emptyCartMeshName, sourcePrefabID, bPrefabTemplate)
	{
	}

	PropertyCollection* EmptyCart::BuildTypeUniquePropertyCollection()
	{
		return BaseCart::BuildTypeUniquePropertyCollection();
	}

	GameObject* EmptyCart::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);

		// TODO: FIXME: Get newly generated cart ID! & move allocation into cart manager
		bool bCopyingToPrefabTemplate = copyFlags & CopyFlags::COPYING_TO_PREFAB;
		EmptyCart* newGameObject = new EmptyCart(newObjectName, newGameObjectID, m_SourcePrefabID, bCopyingToPrefabTemplate);

		newGameObject->currentTrackID = currentTrackID;
		newGameObject->distAlongTrack = distAlongTrack;

		copyFlags = (CopyFlags)(copyFlags & ~CopyFlags::MESH);
		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	EngineCart::EngineCart(const std::string& name,
		const GameObjectID& gameObjectID,
		const PrefabIDPair& sourcePrefabID,
		bool bPrefabTemplate) :
		BaseCart(name, EngineCartSID, gameObjectID, engineMeshName, sourcePrefabID, bPrefabTemplate)
	{
		// TODO: Serialize and expose in ImGui
		m_bItemizable = true;
	}

	PropertyCollection* EngineCart::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = BaseCart::BuildTypeUniquePropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("engine cart info");

		// BaseCart already registered trackID & dist along track fields
		collection->childCollection->RegisterProperty("move direction", FIELD_OFFSET_AND_TYPE(EngineCart, moveDirection))
			.VersionAdded(6)
			.Precision(1)
			.DefaultValue(1.0f);
		collection->childCollection->RegisterProperty("power remaining", FIELD_OFFSET_AND_TYPE(EngineCart, powerRemaining))
			.VersionAdded(6)
			.DefaultValue(1.0f);

		return collection;
	}

	GameObject* EngineCart::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);

		bool bCopyingToPrefabTemplate = copyFlags & CopyFlags::COPYING_TO_PREFAB;
		// TODO: FIXME: Get newly generated cart ID! & move allocation into cart manager
		EngineCart* newGameObject = new EngineCart(newObjectName, newGameObjectID, m_SourcePrefabID, bCopyingToPrefabTemplate);

		newGameObject->powerRemaining = powerRemaining;

		newGameObject->currentTrackID = currentTrackID;
		newGameObject->distAlongTrack = distAlongTrack;

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void EngineCart::Update()
	{
		PROFILE_AUTO("EngineCart Update");

		powerRemaining -= powerDrainMultiplier * g_DeltaTime;
		powerRemaining = glm::max(powerRemaining, 0.0f);

		if (chainID == InvalidCartChainID)
		{
			real dT = g_DeltaTime * GetDrivePower();
			AdvanceAlongTrack(dT);
		}

		if (currentTrackID != InvalidTrackID && powerRemaining > 0.0f)
		{
			TrackID pTrackID = currentTrackID;

			bool bSwitchedTracks = (currentTrackID != pTrackID);
			if (bSwitchedTracks)
			{
				if (distAlongTrack > 0.5f)
				{
					moveDirection = -1.0f;
				}
				else
				{
					moveDirection = 1.0f;
				}
			}
			else if (distAlongTrack == -1.0f ||
				(distAlongTrack == 1.0f && moveDirection > 0.0f) ||
				(distAlongTrack == 0.0f && moveDirection < 0.0f))
			{
				moveDirection = -moveDirection;
			}
		}

		GameObject::Update();
	}

	void EngineCart::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		if (ImGui::TreeNode("Engine BaseCart"))
		{
			ImGui::Text("track ID: %d", currentTrackID);
			ImGui::Text("dist along track: %.2f", distAlongTrack);
			ImGui::Text("move direction: %.2f", moveDirection);
			ImGui::Text("power remaining: %.2f", powerRemaining);

			ImGui::TreePop();
		}
	}

	real EngineCart::GetDrivePower() const
	{
		return (1.0f - glm::pow(1.0f - powerRemaining, 5.0f)) * moveDirection * speed;
	}

	void EngineCart::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject cartInfo;
		// TODO: Also call BaseCart::PartTypeUniqueFields?
		if (parentObject.TryGetObject("engine cart info", cartInfo))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(cartInfo, collection->childCollection);
		}
	}

	void EngineCart::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject cartInfo = {};
		// TODO: Also call BaseCart::SerializeTypeUniqueFields?
		SerializeRegisteredProperties(cartInfo, collection->childCollection, bSerializePrefabData);
		if (!cartInfo.fields.empty())
		{
			parentObject.fields.emplace_back("engine cart info", JSONValue(cartInfo));
		}
	}

	MobileLiquidBox::MobileLiquidBox() :
		// TODO: Extract string to m_UniqueObjectNamePrefix
		MobileLiquidBox(g_SceneManager->CurrentScene()->GetUniqueObjectName("MobileLiquidBox_", 4), InvalidGameObjectID)
	{
	}

	MobileLiquidBox::MobileLiquidBox(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, MobileLiquidBoxSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		MaterialID matID;
		if (!g_Renderer->FindOrCreateMaterialByName("pbr white", matID))
		{
			// TODO: Create own material
			matID = 0;
		}
		Mesh* mesh = SetMesh(new Mesh(this));
		if (!mesh->LoadFromFile(MESH_DIRECTORY "mobile-liquid-box.glb", matID))
		{
			PrintWarn("Failed to load mobile-liquid-box mesh!\n");
		}
	}

	PropertyCollection* MobileLiquidBox::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();
		return collection;
	}

	GameObject* MobileLiquidBox::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);
		MobileLiquidBox* newGameObject = new MobileLiquidBox(newObjectName, newGameObjectID);

		newGameObject->bInCart = bInCart;
		newGameObject->liquidAmount = liquidAmount;

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void MobileLiquidBox::Initialize()
	{
		// TODO: Query from CartManager?
		BaseScene* scene = g_SceneManager->CurrentScene();
		std::vector<BaseCart*> carts = scene->GetObjectsOfType<BaseCart>(BaseCartSID);
		Player* player = scene->GetPlayer(0);
		if (player != nullptr)
		{
			glm::vec3 playerPos = player->GetTransform()->GetWorldPosition();
			real threshold = 8.0f;
			real closestCartDist = threshold;
			i32 closestCartIdx = -1;
			for (i32 i = 0; i < (i32)carts.size(); ++i)
			{
				real d = glm::distance(carts[i]->GetTransform()->GetWorldPosition(), playerPos);
				if (d <= closestCartDist)
				{
					closestCartDist = d;
					closestCartIdx = i;
				}
			}

			if (closestCartIdx != -1)
			{
				carts[closestCartIdx]->AddChild(this);
				m_Transform.SetLocalPosition(glm::vec3(0.0f, 1.5f, 0.0f));
				SetVisible(true);
			}
		}
	}

	void MobileLiquidBox::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		if (ImGui::TreeNode("Mobile liquid box"))
		{
			ImGui::Text("In cart: %d", bInCart ? 1 : 0);
			ImGui::Text("Liquid amount: %.2f", liquidAmount);

			ImGui::TreePop();
		}
	}

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

	GerstnerWave::GerstnerWave(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, GerstnerWaveSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		wave_workQueue = new ThreadSafeArray<WaveGenData>(32);

		// Defaults to use if not set in file
		oceanData = {};
		oceanData.top = glm::vec4(0.1f, 0.3f, 0.6f, 0.0f);
		oceanData.mid = glm::vec4(0.1f, 0.2f, 0.5f, 0.0f);
		oceanData.btm = glm::vec4(0.05f, 0.15f, 0.4f, 0.0f);
		oceanData.fresnelFactor = 0.8f;
		oceanData.fresnelPower = 7.0f;
		oceanData.skyReflectionFactor = 0.8f;
		oceanData.fogFalloff = 1.2f;
		oceanData.fogDensity = 1.2f;

		MaterialCreateInfo matCreateInfo = {};
		matCreateInfo.name = "gerstner";
		matCreateInfo.shaderName = "water";
		matCreateInfo.constAlbedo = glm::vec4(0.4f, 0.5f, 0.8f, 1.0f);
		matCreateInfo.constMetallic = 0.8f;
		matCreateInfo.constRoughness = 0.01f;
		matCreateInfo.bDynamic = true;
		// TODO: Define material in material editor
		matCreateInfo.albedoTexturePath = TEXTURE_DIRECTORY "wave-n-2.png";
		matCreateInfo.enableAlbedoSampler = true;
		matCreateInfo.bSerializable = false;

		m_WaveMaterialID = g_Renderer->InitializeMaterial(&matCreateInfo);

		bobberTarget = Spring<real>(0.0f);
		bobberTarget.DR = 2.5f;
		bobberTarget.UAF = 40.0f;
		bobber = new GameObject("Bobber", BaseObjectSID);
		bobber->SetSerializable(false);
		MaterialID bobberMatID = InvalidMaterialID;
		if (!g_Renderer->FindOrCreateMaterialByName("pbr red", bobberMatID))
		{
			PrintError("Failed to find material for bobber!\n");
		}
		else
		{
			bobberMatID = g_Renderer->GetPlaceholderMaterialID();
		}
		Mesh* mesh = bobber->SetMesh(new Mesh(bobber));
		if (!mesh->LoadFromFile(MESH_DIRECTORY "sphere.glb", bobberMatID))
		{
			PrintError("Failed to load bobber mesh\n");
		}
		g_SceneManager->CurrentScene()->AddRootObject(bobber);
	}

	PropertyCollection* GerstnerWave::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("gerstner wave");

		collection->childCollection->RegisterProperty("fresnel factor", FIELD_OFFSET_AND_TYPE(GerstnerWave, oceanData.fresnelFactor))
			.VersionAdded(6)
			.Precision(2);
		collection->childCollection->RegisterProperty("fresnel power", FIELD_OFFSET_AND_TYPE(GerstnerWave, oceanData.fresnelPower))
			.VersionAdded(2)
			.Precision(2);
		collection->childCollection->RegisterProperty("sky reflection factor", FIELD_OFFSET_AND_TYPE(GerstnerWave, oceanData.skyReflectionFactor))
			.VersionAdded(6)
			.Precision(2);
		collection->childCollection->RegisterProperty("fog falloff", FIELD_OFFSET_AND_TYPE(GerstnerWave, oceanData.fogFalloff))
			.VersionAdded(6)
			.Precision(2);
		collection->childCollection->RegisterProperty("fog density", FIELD_OFFSET_AND_TYPE(GerstnerWave, oceanData.fogDensity))
			.VersionAdded(6)
			.Precision(2);

		return collection;
	}

	void GerstnerWave::Initialize()
	{
		m_VertexBufferCreateInfo = {};
		m_VertexBufferCreateInfo.attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(m_WaveMaterialID)->shaderID)->vertexAttributes;

		criticalSection = Platform::InitCriticalSection();

		for (u32 i = 0; i < wave_workQueue->Size(); ++i)
		{
			// TODO: Allocate differently sized chunks so lower LODs aren't wasting memory
			AllocWorkQueueEntry(i);
		}

		WRITE_BARRIER;

		u32 threadCount = (u32)glm::clamp(((i32)Platform::GetLogicalProcessorCount()) - 1, 1, 16);

		threadUserData = {};
		threadUserData.running = true;
		threadUserData.criticalSection = criticalSection;
		Platform::SpawnThreads(threadCount, &WaveThreadUpdate, &threadUserData);

		SortWaveSamplingLODs();
		SortWaveTessellationLODs();

		DiscoverChunks();
		UpdateWaveVertexData();

		SetMesh(new Mesh(this));
		Mesh::CreateInfo meshCreateInfo = {};
		meshCreateInfo.bDynamic = true;
		meshCreateInfo.vertexBufferCreateInfo = &m_VertexBufferCreateInfo;
		meshCreateInfo.indices = m_Indices;
		meshCreateInfo.materialIDs = { m_WaveMaterialID };
		meshCreateInfo.initialMaxVertexCount = (u32)m_VertexBufferCreateInfo.positions_3D.size();
		m_Mesh->LoadFromMemory(meshCreateInfo);

		GameObject::Initialize();
	}

	void GerstnerWave::Update()
	{
		if (!m_bVisible)
		{
			return;
		}

		PROFILE_AUTO("GerstnerWave Update");

		for (WaveInfo& waveInfo : waveContributions)
		{
			if (waveInfo.enabled)
			{
				waveInfo.accumOffset += (waveInfo.moveSpeed * g_DeltaTime);
			}
		}

#if 0
		for (u32 i = 0; i < (u32)waveChunks.size(); ++i)
		{
			const glm::vec2i& chunkIndex = waveChunks[i].index;
			for (u32 j = 0; j < i; ++j)
			{
				g_Renderer->GetDebugRenderer()->drawSphere(btVector3((chunkIndex.x) * size, 8.0f + 1.5f * j, (chunkIndex.y) * size), 0.5f, btVector4(1, 1, 1, 1));
			}
		}
#endif

		DiscoverChunks();
		UpdateWaveVertexData();

		MeshComponent* meshComponent = m_Mesh->GetSubMesh(0);
		if (!m_Indices.empty())
		{
			meshComponent->UpdateDynamicVertexData(m_VertexBufferCreateInfo, m_Indices);
			g_Renderer->ShrinkDynamicVertexData(meshComponent->renderID, 0.5f);
		}

		// Bobber
		if (!m_Indices.empty())
		{
			const glm::vec3 wavePos = m_Transform.GetWorldPosition();
			glm::vec3 bobberTargetPos = QueryHeightFieldFromVerts(m_bPinCenter ? m_PinnedPos : g_CameraManager->CurrentCamera()->position);
			bobberTarget.SetTargetPos(bobberTargetPos.y);
			bobberTarget.Tick(g_DeltaTime);
			const real bobberYOffset = 0.2f;
			glm::vec3 newPos = wavePos + glm::vec3(bobberTargetPos.x, bobberTarget.pos + bobberYOffset, bobberTargetPos.z);
			bobber->GetTransform()->SetWorldPosition(newPos);
		}

		g_Renderer->SetGlobalUniform(&U_OCEAN_DATA, &oceanData, sizeof(oceanData));

		GameObject::Update();
	}

	void GerstnerWave::Destroy(bool bDetachFromParent /* = true */)
	{
		g_Renderer->ClearGlobalUniform(&U_OCEAN_DATA, &oceanData);

		threadUserData.running = false;

		// TODO: Only join our threads!
		Platform::JoinThreads();

		for (u32 i = 0; i < wave_workQueue->Size(); ++i)
		{
			FreeWorkQueueEntry(i);
		}

		delete wave_workQueue;
		wave_workQueue = nullptr;

		Platform::FreeCriticalSection(criticalSection);
		criticalSection = nullptr;

		GameObject::Destroy(bDetachFromParent);
	}

	GerstnerWave::WaveChunk const* GerstnerWave::GetChunkAtPos(const glm::vec2& pos) const
	{
		return flex::GetChunkAtPos(pos, waveChunks, size);
	}

	GerstnerWave::WaveTessellationLOD const* GerstnerWave::GetTessellationLOD(u32 lodLevel) const
	{
		return flex::GetTessellationLOD(lodLevel, waveTessellationLODs);
	}

	u32 GerstnerWave::ComputeTesellationLODLevel(const glm::vec2i& chunkIdx)
	{
		if (waveTessellationLODs.empty())
		{
			waveTessellationLODs.emplace_back(0.0f, maxChunkVertCountPerAxis);
		}

		glm::vec3 center = m_bPinCenter ? m_PinnedPos : g_CameraManager->CurrentCamera()->position;
		real sqrDist = glm::distance2(glm::vec2(center.x, center.z), glm::vec2(chunkIdx) * size);

		for (u32 i = 0; i < (u32)waveTessellationLODs.size(); ++i)
		{
			if (sqrDist > waveTessellationLODs[i].squareDist)
			{
				return i;
			}
		}

		return (u32)(waveTessellationLODs.size() - 1);
	}

	glm::vec3 GerstnerWave::QueryHeightFieldFromVerts(const glm::vec3& queryPos) const
	{
		WaveChunk const* chunk = GetChunkAtPos(queryPos);
		if (chunk)
		{
			glm::vec2i chunkIdx = chunk->index;
			WaveTessellationLOD const* tessellationLOD = GetTessellationLOD(chunk->tessellationLODLevel);
			u32 chunkVertCountPerAxis = tessellationLOD->vertCountPerAxis;
			const u32 vertsPerChunk = chunkVertCountPerAxis * chunkVertCountPerAxis;
			glm::vec2 chunkMin((chunkIdx.x - 0.5f) * size, (chunkIdx.y - 0.5f) * size);
			glm::vec2 chunkUV = Saturate(glm::vec2((queryPos.x - chunkMin.x) / size, (queryPos.z - chunkMin.y) / size));
			u32 chunkLocalVertexIndex = (u32)(chunkUV.x * (chunkVertCountPerAxis - 1)) + ((u32)(chunkUV.y * ((real)chunkVertCountPerAxis - 1.0f)) * chunkVertCountPerAxis);
			const u32 idxMax = vertsPerChunk - 1;
			glm::vec3 A(m_VertexBufferCreateInfo.positions_3D[chunk->vertOffset + glm::min(chunkLocalVertexIndex + 0u, idxMax)]);
			glm::vec3 B(m_VertexBufferCreateInfo.positions_3D[chunk->vertOffset + glm::min(chunkLocalVertexIndex + 1u, idxMax)]);
			glm::vec3 C(m_VertexBufferCreateInfo.positions_3D[chunk->vertOffset + glm::min(chunkLocalVertexIndex + chunkVertCountPerAxis, idxMax)]);
			glm::vec3 D(m_VertexBufferCreateInfo.positions_3D[chunk->vertOffset + glm::min(chunkLocalVertexIndex + chunkVertCountPerAxis + 1u, idxMax)]);
			glm::vec2 vertexUV = Saturate(glm::vec2((queryPos.x - A.x) / (B.x - A.x), (queryPos.z - B.z) / (D.z - B.z)));
			glm::vec3 result = Lerp(Lerp(A, B, vertexUV.x), Lerp(C, D, vertexUV.x), vertexUV.y);

			return result;
		}

		// No chunk corresponds with query pos
		return VEC3_ZERO;
	}

	void GerstnerWave::DiscoverChunks()
	{
		std::vector<WaveChunk> chunksInRadius;

		glm::vec3 center = m_bPinCenter ? m_PinnedPos : g_CameraManager->CurrentCamera()->position;
		const glm::vec2 centerXZ(center.x, center.z);
		const glm::vec2i camChunkIdx = (glm::vec2i)(glm::vec2(center.x, center.z) / size);
		const i32 maxChunkIdxDiff = (i32)glm::ceil(loadRadius / (real)size);
		const real radiusSqr = loadRadius * loadRadius;
		u32 vertOffset = 0;
		for (i32 x = camChunkIdx.x - maxChunkIdxDiff; x < camChunkIdx.x + maxChunkIdxDiff; ++x)
		{
			for (i32 z = camChunkIdx.y - maxChunkIdxDiff; z < camChunkIdx.y + maxChunkIdxDiff; ++z)
			{
				glm::vec2 chunkCenter(x * size, z * size);

				//if (g_Renderer->GetDebugRenderer() != nullptr)
				//{
				//	g_Renderer->GetDebugRenderer()->drawSphere(btVector3(chunkCenter.x, 1.0f, chunkCenter.y), 0.5f, btVector3(0.75f, 0.5f, 0.6f));
				//}

				if (glm::distance2(chunkCenter, centerXZ) < radiusSqr)
				{
					u32 lodLevel = ComputeTesellationLODLevel(glm::vec2i(x, z));
					chunksInRadius.emplace_back(glm::vec2i(x, z), vertOffset, lodLevel);
					vertOffset += waveTessellationLODs[lodLevel].vertCountPerAxis * waveTessellationLODs[lodLevel].vertCountPerAxis;
				}
			}
		}

		waveChunks = chunksInRadius;
	}

	void GerstnerWave::AllocWorkQueueEntry(u32 workQueueIndex)
	{
		const u32 vertCountPerChunk = maxChunkVertCountPerAxis * maxChunkVertCountPerAxis;
		const u32 vertCountPerChunkDiv4 = vertCountPerChunk / 4;

		CHECK_EQ((*wave_workQueue)[workQueueIndex].positionsx_4, nullptr);

		(*wave_workQueue)[workQueueIndex].positionsx_4 = (__m128*)_mm_malloc(vertCountPerChunkDiv4 * sizeof(__m128), 16);
		(*wave_workQueue)[workQueueIndex].positionsy_4 = (__m128*)_mm_malloc(vertCountPerChunkDiv4 * sizeof(__m128), 16);
		(*wave_workQueue)[workQueueIndex].positionsz_4 = (__m128*)_mm_malloc(vertCountPerChunkDiv4 * sizeof(__m128), 16);
		(*wave_workQueue)[workQueueIndex].lodSelected_4 = (__m128*)_mm_malloc(vertCountPerChunkDiv4 * sizeof(__m128), 16);
		(*wave_workQueue)[workQueueIndex].uvUs_4 = (__m128*)_mm_malloc(vertCountPerChunkDiv4 * sizeof(__m128), 16);
		(*wave_workQueue)[workQueueIndex].uvVs_4 = (__m128*)_mm_malloc(vertCountPerChunkDiv4 * sizeof(__m128), 16);

		(*wave_workQueue)[workQueueIndex].lodCutoffsAmplitudes_4 = (__m128*)_mm_malloc(vertCountPerChunkDiv4 * sizeof(__m128), 16);
		(*wave_workQueue)[workQueueIndex].lodNextCutoffAmplitudes_4 = (__m128*)_mm_malloc(vertCountPerChunkDiv4 * sizeof(__m128), 16);
		(*wave_workQueue)[workQueueIndex].lodBlendWeights_4 = (__m128*)_mm_malloc(vertCountPerChunkDiv4 * sizeof(__m128), 16);
	}

	void GerstnerWave::FreeWorkQueueEntry(u32 workQueueIndex)
	{
		_mm_free((*wave_workQueue)[workQueueIndex].positionsx_4);
		_mm_free((*wave_workQueue)[workQueueIndex].positionsy_4);
		_mm_free((*wave_workQueue)[workQueueIndex].positionsz_4);
		_mm_free((*wave_workQueue)[workQueueIndex].lodSelected_4);
		_mm_free((*wave_workQueue)[workQueueIndex].uvUs_4);
		_mm_free((*wave_workQueue)[workQueueIndex].uvVs_4);

		_mm_free((*wave_workQueue)[workQueueIndex].lodCutoffsAmplitudes_4);
		_mm_free((*wave_workQueue)[workQueueIndex].lodNextCutoffAmplitudes_4);
		_mm_free((*wave_workQueue)[workQueueIndex].lodBlendWeights_4);
	}

	void GerstnerWave::UpdateWaveVertexData()
	{
		i32 vertCount = 0;
		i32 indexCount = 0;
		for (u32 chunkIdx = 0; chunkIdx < (u32)waveChunks.size(); ++chunkIdx)
		{
			WaveChunk& waveChunk = waveChunks[chunkIdx];
			WaveTessellationLOD const* tessellationLOD = GetTessellationLOD(waveChunk.tessellationLODLevel);
			vertCount += tessellationLOD->vertCountPerAxis * tessellationLOD->vertCountPerAxis;
			indexCount += 6 * (tessellationLOD->vertCountPerAxis - 1) * (tessellationLOD->vertCountPerAxis - 1);
		}

		DEBUG_lastUsedVertCount = vertCount;

		// Resize & regenerate index buffer
		if ((i32)m_Indices.size() != indexCount)
		{
			m_Indices.resize(indexCount);
			i32 i = 0;
			for (u32 chunkIdx = 0; chunkIdx < (u32)waveChunks.size(); ++chunkIdx)
			{
				WaveChunk& waveChunk = waveChunks[chunkIdx];
				WaveTessellationLOD const* tessellationLOD = GetTessellationLOD(waveChunk.tessellationLODLevel);
				for (u32 z = 0; z < tessellationLOD->vertCountPerAxis - 1; ++z)
				{
					for (u32 x = 0; x < tessellationLOD->vertCountPerAxis - 1; ++x)
					{
						u32 vertIdx = z * tessellationLOD->vertCountPerAxis + x + waveChunk.vertOffset;
						m_Indices[i++] = vertIdx;
						m_Indices[i++] = vertIdx + tessellationLOD->vertCountPerAxis;
						m_Indices[i++] = vertIdx + 1;

						vertIdx = vertIdx + 1 + tessellationLOD->vertCountPerAxis;
						m_Indices[i++] = vertIdx;
						m_Indices[i++] = vertIdx - tessellationLOD->vertCountPerAxis;
						m_Indices[i++] = vertIdx - 1;
					}
				}
			}
		}

		// Resize vertex buffers
		if (vertCount > (i32)m_VertexBufferCreateInfo.positions_3D.size())
		{
			m_VertexBufferCreateInfo.positions_3D.resize(vertCount);
			m_VertexBufferCreateInfo.texCoords_UV.resize(vertCount);
			m_VertexBufferCreateInfo.normals.resize(vertCount);
			m_VertexBufferCreateInfo.tangents.resize(vertCount);
			m_VertexBufferCreateInfo.colours_R32G32B32A32.resize(vertCount);
		}
		else
		{
			// Shrink vertex buffers once they have > 50% unused space
			real excess = (real)(m_VertexBufferCreateInfo.positions_3D.size() - vertCount) / m_VertexBufferCreateInfo.positions_3D.size();
			if (excess > 0.5f)
			{
				m_VertexBufferCreateInfo.positions_3D.resize(vertCount);
				m_VertexBufferCreateInfo.positions_3D.shrink_to_fit();
				m_VertexBufferCreateInfo.texCoords_UV.resize(vertCount);
				m_VertexBufferCreateInfo.texCoords_UV.shrink_to_fit();
				m_VertexBufferCreateInfo.normals.resize(vertCount);
				m_VertexBufferCreateInfo.normals.shrink_to_fit();
				m_VertexBufferCreateInfo.tangents.resize(vertCount);
				m_VertexBufferCreateInfo.tangents.shrink_to_fit();
				m_VertexBufferCreateInfo.colours_R32G32B32A32.resize(vertCount);
				m_VertexBufferCreateInfo.colours_R32G32B32A32.shrink_to_fit();
			}
		}

		{
			PROFILE_AUTO("Update waves");
#if SIMD_WAVES
			UpdateWavesSIMD();
#else
			UpdateWavesLinear();
#endif
		}
	}

	void GerstnerWave::UpdateWavesLinear()
	{
		glm::vec3* positions = m_VertexBufferCreateInfo.positions_3D.data();

		// TODO: Add LOD blending

		for (u32 chunkIdx = 0; chunkIdx < (u32)waveChunks.size(); ++chunkIdx)
		{
			WaveChunk& waveChunk = waveChunks[chunkIdx];
			WaveTessellationLOD const* tessellationLOD = GetTessellationLOD(waveChunk.tessellationLODLevel);

			const glm::vec3 chunkCenter((waveChunks[chunkIdx].index.x - 0.5f) * size, 0.0f, (waveChunks[chunkIdx].index.y - 0.5f) * size);

			real chunkDist = glm::distance(glm::vec3(chunkCenter.x, 0.0f, chunkCenter.y), g_CameraManager->CurrentCamera()->position);
			real amplitudeLODCutoff = GetWaveAmplitudeLODCutoffForDistance(chunkDist);

			for (u32 z = 0; z < tessellationLOD->vertCountPerAxis; ++z)
			{
				for (u32 x = 0; x < tessellationLOD->vertCountPerAxis; ++x)
				{
					const u32 vertIdx = z * tessellationLOD->vertCountPerAxis + x + waveChunk.vertOffset;
					positions[vertIdx] = chunkCenter + glm::vec3(
						size * ((real)x / (tessellationLOD->vertCountPerAxis - 1)),
						0.0f,
						size * ((real)z / (tessellationLOD->vertCountPerAxis - 1)));
				}
			}

			for (i32 i = 0; i < (i32)waveContributions.size(); ++i)
			{
				const WaveInfo& waveInfo = waveContributions[i];

				if (waveInfo.a < amplitudeLODCutoff)
				{
					break;
				}

				if (soloWaveIndex != -1 && soloWaveIndex != i)
				{
					continue;
				}

				if (waveInfo.enabled)
				{
					const glm::vec2 waveVec = glm::vec2(waveInfo.waveDirCos, waveInfo.waveDirSin) * waveInfo.waveVecMag;
					const glm::vec2 waveVecN = glm::normalize(waveVec);

					for (u32 z = 0; z < tessellationLOD->vertCountPerAxis; ++z)
					{
						for (u32 x = 0; x < tessellationLOD->vertCountPerAxis; ++x)
						{
							const u32 vertIdx = z * tessellationLOD->vertCountPerAxis + x + waveChunk.vertOffset;

							real d = waveVec.x * positions[vertIdx].x + waveVec.y * positions[vertIdx].z; // Inline dot
							real c = cos(d + waveInfo.accumOffset);
							real s = sin(d + waveInfo.accumOffset);
							positions[vertIdx] += glm::vec3(
								-waveVecN.x * waveInfo.a * s,
								waveInfo.a * c,
								-waveVecN.y * waveInfo.a * s);

							real reversePhaseOffset = fmod(abs(waveVec.x + 1.0f) * 29193.123456f, 1.0f);
							glm::vec2 waveVecR(-waveVec);
							real dr = waveVecR.x * positions[vertIdx].x + waveVecR.y * positions[vertIdx].z; // Inline dot
							real cr = cos(dr + waveInfo.accumOffset + reversePhaseOffset);
							real sr = sin(dr + waveInfo.accumOffset + reversePhaseOffset);
							positions[vertIdx] += glm::vec3(
								-waveVecN.x * waveInfo.a * sr * reverseWaveAmplitude,
								waveInfo.a * cr * reverseWaveAmplitude,
								-waveVecN.y * waveInfo.a * sr * reverseWaveAmplitude);
						}
					}
				}
			}

#if 0

			// Ripple
			glm::vec3 ripplePos = VEC3_ZERO;
			real rippleAmp = 0.8f;
			real rippleLen = 0.6f;
			real rippleFadeOut = 12.0f;
			for (u32 z = 0; z < tessellationLOD->vertCountPerAxis; ++z)
			{
				for (u32 x = 0; x < tessellationLOD->vertCountPerAxis; ++x)
				{
					const u32 vertIdx = z * tessellationLOD->vertCountPerAxis + x + waveChunk.vertOffset;

					glm::vec3 diff = (ripplePos - positions[vertIdx]);
					real d = glm::length(diff);
					diff = diff / d * rippleLen;
					real c = cos(g_SecElapsedSinceProgramStart * 1.8f - d);
					real s = sin(g_SecElapsedSinceProgramStart * 1.5f - d);
					real a = Lerp(0.0f, rippleAmp, 1.0f - Saturate(d / rippleFadeOut));
					positions[vertIdx] += glm::vec3(
						-diff.x * a * s,
						a * c,
						-diff.z * a * s);
				}
			}
#endif

			UpdateNormalsForChunk(chunkIdx);
		}
	}

	void GerstnerWave::UpdateWavesSIMD()
	{
		// Resize work queue if not large enough
		if (waveChunks.size() > wave_workQueue->Size())
		{
			for (u32 i = 0; i < wave_workQueue->Size(); ++i)
			{
				// TODO: Reuse memory
				FreeWorkQueueEntry(i);
			}
			delete wave_workQueue;

			u32 newSize = (u32)(waveChunks.size() * 1.2f);
			wave_workQueue = new ThreadSafeArray<WaveGenData>(newSize);
			for (u32 i = 0; i < wave_workQueue->Size(); ++i)
			{
				AllocWorkQueueEntry(i);
			}
		}

		wave_workQueueEntriesCompleted = 0;
		wave_workQueueEntriesCreated = 0;
		WRITE_BARRIER;
		wave_workQueueEntriesClaimed = 0;
		WRITE_BARRIER;

		for (u32 chunkIdx = 0; chunkIdx < (u32)waveChunks.size(); ++chunkIdx)
		{
			volatile WaveGenData* waveGenData = &(*wave_workQueue)[chunkIdx];

			waveGenData->waveContributions = &waveContributions;
			waveGenData->waveChunks = &waveChunks;
			waveGenData->waveSamplingLODs = &waveSamplingLODs;
			waveGenData->waveTessellationLODs = &waveTessellationLODs;
			waveGenData->soloWave = soloWaveIndex != -1 ? &waveContributions[soloWaveIndex] : nullptr;
			waveGenData->size = size;
			waveGenData->chunkIdx = chunkIdx;
			waveGenData->bDisableLODs = bDisableLODs;
			waveGenData->blendDist = blendDist;

			waveGenData->positions = nullptr;

			WRITE_BARRIER;

			Platform::AtomicIncrement(&wave_workQueueEntriesCreated);
		}

		// Wait for all threads to complete
		// TODO: Call later in frame
		while (wave_workQueueEntriesCompleted != wave_workQueueEntriesCreated)
		{
			Platform::YieldProcessor();
		}

		glm::vec3* positions = m_VertexBufferCreateInfo.positions_3D.data();
		glm::vec2* texCoords = m_VertexBufferCreateInfo.texCoords_UV.data();
		glm::vec4* colours = m_VertexBufferCreateInfo.colours_R32G32B32A32.data();

		// Read back SIMD vars into standard format
		for (u32 chunkIdx = 0; chunkIdx < (u32)waveChunks.size(); ++chunkIdx)
		{
			WaveChunk& waveChunk = waveChunks[chunkIdx];
			WaveTessellationLOD const* tessellationLOD = GetTessellationLOD(waveChunk.tessellationLODLevel);

			for (u32 z = 0; z < tessellationLOD->vertCountPerAxis; ++z)
			{
				for (u32 x = 0; x < tessellationLOD->vertCountPerAxis; x += 4)
				{
					const u32 vertIdx = z * tessellationLOD->vertCountPerAxis + x + waveChunk.vertOffset;
					const u32 chunkLocalVertIdx = z * tessellationLOD->vertCountPerAxis + x;
					const u32 chunkLocalVertIdxDiv4 = chunkLocalVertIdx / 4;

					glm::vec4 xs, ys, zs, lodSelections, uvUs, uvVs;
					_mm_store_ps(&xs.x, (*wave_workQueue)[chunkIdx].positionsx_4[chunkLocalVertIdxDiv4]);
					_mm_store_ps(&ys.x, (*wave_workQueue)[chunkIdx].positionsy_4[chunkLocalVertIdxDiv4]);
					_mm_store_ps(&zs.x, (*wave_workQueue)[chunkIdx].positionsz_4[chunkLocalVertIdxDiv4]);
					_mm_store_ps(&lodSelections.x, (*wave_workQueue)[chunkIdx].lodSelected_4[chunkLocalVertIdxDiv4]);
					_mm_store_ps(&uvUs.x, (*wave_workQueue)[chunkIdx].uvUs_4[chunkLocalVertIdxDiv4]);
					_mm_store_ps(&uvVs.x, (*wave_workQueue)[chunkIdx].uvVs_4[chunkLocalVertIdxDiv4]);

					positions[vertIdx + 0] = glm::vec3(xs.x, ys.x, zs.x);
					positions[vertIdx + 1] = glm::vec3(xs.y, ys.y, zs.y);
					positions[vertIdx + 2] = glm::vec3(xs.z, ys.z, zs.z);
					positions[vertIdx + 3] = glm::vec3(xs.w, ys.w, zs.w);

					texCoords[vertIdx + 0] = glm::vec2(uvUs.x, uvVs.x);
					texCoords[vertIdx + 1] = glm::vec2(uvUs.y, uvVs.y);
					texCoords[vertIdx + 2] = glm::vec2(uvUs.z, uvVs.z);
					texCoords[vertIdx + 3] = glm::vec2(uvUs.w, uvVs.w);

					colours[vertIdx + 0] = ChooseColourFromLOD(lodSelections.x);
					colours[vertIdx + 1] = ChooseColourFromLOD(lodSelections.y);
					colours[vertIdx + 2] = ChooseColourFromLOD(lodSelections.z);
					colours[vertIdx + 3] = ChooseColourFromLOD(lodSelections.w);
				}
			}

			UpdateNormalsForChunk(chunkIdx);
		}
	}

	glm::vec4 GerstnerWave::ChooseColourFromLOD(real LOD)
	{
		static const glm::vec4 COLOURS[] = { glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), glm::vec4(0.0f, 1.0f, 0.0f, 1.0f), glm::vec4(0.0f, 0.0f, 1.0f, 1.0f), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) };
		//static const glm::vec4 COLOURS[] = { glm::vec4(0.8f, 0.4f, 0.2f, 1.0f), glm::vec4(0.1f, 0.8f, 0.3f, 1.0f), glm::vec4(0.2f, 0.4f, 0.8f, 1.0f), glm::vec4(0.5f, 0.7f, 0.5f, 1.0f) };
		static const u32 colourCount = ARRAY_SIZE(COLOURS);

		u32 lodLevel = (u32)LOD;
		real blend = LOD - lodLevel;

		return blend == 0.0f ? COLOURS[lodLevel] : (blend == 1.0f ? COLOURS[glm::clamp(lodLevel + 1, 0u, colourCount - 1)] : Lerp(COLOURS[lodLevel], COLOURS[glm::clamp(lodLevel + 1, 0u, colourCount - 1)], blend));
	}

	void* WaveThreadUpdate(void* inData)
	{
		WaveThreadData* threadData = (WaveThreadData*)inData;

		while (threadData->running)
		{
			volatile GerstnerWave::WaveGenData* work = nullptr;

			if (wave_workQueueEntriesClaimed < wave_workQueueEntriesCreated)
			{
				Platform::EnterCriticalSection(threadData->criticalSection);
				{
					if (wave_workQueueEntriesClaimed < wave_workQueueEntriesCreated)
					{
						work = &(*wave_workQueue)[wave_workQueueEntriesClaimed];

						wave_workQueueEntriesClaimed += 1;

						WRITE_BARRIER;

						CHECK_LE(wave_workQueueEntriesClaimed, wave_workQueueEntriesCreated);
						CHECK_LE(wave_workQueueEntriesClaimed, wave_workQueue->Size());
					}

				}
				Platform::LeaveCriticalSection(threadData->criticalSection);
			}

			if (work != nullptr)
			{
				// Inputs
				const std::vector<GerstnerWave::WaveInfo>& waveContributions = *work->waveContributions;
				const std::vector<GerstnerWave::WaveChunk>& waveChunks = *work->waveChunks;
				const std::vector<GerstnerWave::WaveSamplingLOD>& waveSamplingLODs = *work->waveSamplingLODs;
				const std::vector<GerstnerWave::WaveTessellationLOD>& waveTessellationLODs = *work->waveTessellationLODs;
				GerstnerWave::WaveInfo const* soloWave = work->soloWave;

				GerstnerWave::WaveTessellationLOD const* tessellationLOD = GetTessellationLOD(waveChunks[work->chunkIdx].tessellationLODLevel, waveTessellationLODs);

				real size = work->size;
				i32 chunkVertCountPerAxis = tessellationLOD->vertCountPerAxis;
				u32 chunkIdx = work->chunkIdx;
				bool bDisableLODs = work->bDisableLODs;
				real blendDist = work->blendDist;

				__m128* lodCutoffsAmplitudes_4 = work->lodCutoffsAmplitudes_4;
				__m128* lodNextCutoffAmplitudes_4 = work->lodNextCutoffAmplitudes_4;
				__m128* lodBlendWeights_4 = work->lodBlendWeights_4;

				// Outputs
				__m128* positionsx_4 = work->positionsx_4;
				__m128* positionsy_4 = work->positionsy_4;
				__m128* positionsz_4 = work->positionsz_4;
				__m128* lodSelected_4 = work->lodSelected_4;
				__m128* uvUs = work->uvUs_4;
				__m128* uvVs = work->uvVs_4;

				__m128 blendDist_4 = _mm_set_ps1(blendDist);

				__m128 vertCountMin1_4 = _mm_set_ps1((real)(chunkVertCountPerAxis - 1));
				__m128 size_4 = _mm_set_ps1(size);

				const glm::vec2 chunkCenter((waveChunks[chunkIdx].index.x - 0.5f) * size, (waveChunks[chunkIdx].index.y - 0.5f) * size);
				__m128 chunkCenterX_4 = _mm_set_ps1(chunkCenter.x);
				__m128 chunkCenterY_4 = _mm_setzero_ps();
				__m128 chunkCenterZ_4 = _mm_set_ps1(chunkCenter.y);

				// TODO: Work out max LOD in chunk
				glm::vec3 camPos = g_CameraManager->CurrentCamera()->position;
				__m128 camPosx_4 = _mm_set_ps1(camPos.x);
				__m128 camPosz_4 = _mm_set_ps1(camPos.z);

				__m128 zero_4 = _mm_set_ps1(0.0f);
				__m128 one_4 = _mm_set_ps1(1.0f);

				// Clear out intermediate data
				for (i32 z = 0; z < chunkVertCountPerAxis; ++z)
				{
					for (i32 x = 0; x < chunkVertCountPerAxis; x += 4)
					{
						const i32 chunkLocalVertIdx = z * chunkVertCountPerAxis + x;
						const i32 chunkLocalVertIdxDiv4 = chunkLocalVertIdx / 4;
						lodCutoffsAmplitudes_4[chunkLocalVertIdxDiv4] = _mm_setzero_ps();
						lodNextCutoffAmplitudes_4[chunkLocalVertIdxDiv4] = _mm_setzero_ps();
						lodSelected_4[chunkLocalVertIdxDiv4] = _mm_setzero_ps();
						lodBlendWeights_4[chunkLocalVertIdxDiv4] = one_4;
					}
				}

				// Positions verts on flat plane
				for (i32 z = 0; z < chunkVertCountPerAxis; ++z)
				{
					__m128 zIdx_4 = _mm_set_ps1((real)z);

					__m128 uvV_4 = _mm_div_ps(zIdx_4, vertCountMin1_4);
					__m128 posZ_4 = _mm_add_ps(chunkCenterZ_4, _mm_mul_ps(size_4, uvV_4));

					for (i32 x = 0; x < chunkVertCountPerAxis; x += 4)
					{
						const i32 chunkLocalVertIdx = z * chunkVertCountPerAxis + x;
						__m128 xIdx_4 = _mm_set_ps((real)(x + 3), (real)(x + 2), (real)(x + 1), (real)(x + 0));
						const i32 chunkLocalVertIdxDiv4 = chunkLocalVertIdx / 4;
						__m128 uvU_4 = _mm_div_ps(xIdx_4, vertCountMin1_4);
						positionsx_4[chunkLocalVertIdxDiv4] = _mm_add_ps(chunkCenterX_4, _mm_mul_ps(size_4, uvU_4));
						positionsy_4[chunkLocalVertIdxDiv4] = chunkCenterY_4;
						positionsz_4[chunkLocalVertIdxDiv4] = posZ_4;

						uvUs[chunkLocalVertIdxDiv4] = uvU_4;
						uvVs[chunkLocalVertIdxDiv4] = uvV_4;

						__m128 xr = _mm_sub_ps(positionsx_4[chunkLocalVertIdxDiv4], camPosx_4);
						__m128 zr = _mm_sub_ps(positionsz_4[chunkLocalVertIdxDiv4], camPosz_4);
						__m128 vertSqrDist = _mm_add_ps(_mm_mul_ps(xr, xr), _mm_mul_ps(zr, zr));

						if (!bDisableLODs)
						{
							__m128 lodNextCutoffDistances_4 = zero_4;
							for (u32 i = 0; i < (u32)waveSamplingLODs.size(); ++i)
							{
								__m128 i_4 = _mm_set_ps1((real)i);
								__m128 waveSqrDistCutoff_4 = _mm_set_ps1(waveSamplingLODs[i].squareDist);
								__m128 waveAmplitudeCutoff_4 = _mm_set_ps1(waveSamplingLODs[i].amplitudeCutoff);
								__m128 mask0_4 = _mm_cmpge_ps(vertSqrDist, waveSqrDistCutoff_4);
								__m128 eqZ = _mm_cmpeq_ps(lodCutoffsAmplitudes_4[chunkLocalVertIdxDiv4], zero_4);
								__m128 cmpMask = _mm_blendv_ps(zero_4, mask0_4, eqZ);
								lodCutoffsAmplitudes_4[chunkLocalVertIdxDiv4] = _mm_blendv_ps(lodCutoffsAmplitudes_4[chunkLocalVertIdxDiv4], waveAmplitudeCutoff_4, cmpMask);

								bool bLastCutoff = (i == waveSamplingLODs.size() - 1);
								lodNextCutoffDistances_4 = _mm_blendv_ps(lodNextCutoffDistances_4, _mm_set_ps1(bLastCutoff ? FLT_MAX : waveSamplingLODs[i + 1].squareDist), cmpMask);
								lodNextCutoffAmplitudes_4[chunkLocalVertIdxDiv4] = _mm_blendv_ps(lodNextCutoffAmplitudes_4[chunkLocalVertIdxDiv4], _mm_set_ps1(bLastCutoff ? FLT_MAX : waveSamplingLODs[i + 1].amplitudeCutoff), cmpMask);

								__m128 cmpMask3 = _mm_and_ps(_mm_cmpeq_ps(cmpMask, zero_4), _mm_cmpeq_ps(i_4, zero_4));
								// Set next on verts in first LOD level
								lodNextCutoffDistances_4 = _mm_blendv_ps(lodNextCutoffDistances_4, _mm_set_ps1(waveSamplingLODs[0].squareDist), cmpMask3);
								lodNextCutoffAmplitudes_4[chunkLocalVertIdxDiv4] = _mm_blendv_ps(lodNextCutoffAmplitudes_4[chunkLocalVertIdxDiv4], _mm_set_ps1(waveSamplingLODs[0].amplitudeCutoff), cmpMask3);

								__m128 cmp2Mask = _mm_or_ps(_mm_cmpeq_ps(lodBlendWeights_4[chunkLocalVertIdxDiv4], zero_4), _mm_cmpeq_ps(lodNextCutoffDistances_4, zero_4));
								__m128 delta = _mm_max_ps(_mm_sub_ps(_mm_sqrt_ps(lodNextCutoffDistances_4), _mm_sqrt_ps(vertSqrDist)), zero_4);
								// 0 at edge, 1 at blend dist inward, blend between
								lodBlendWeights_4[chunkLocalVertIdxDiv4] = _mm_blendv_ps(_mm_min_ps(_mm_div_ps(delta, blendDist_4), one_4), lodBlendWeights_4[chunkLocalVertIdxDiv4], cmp2Mask);
								lodSelected_4[chunkLocalVertIdxDiv4] = _mm_blendv_ps(_mm_add_ps(lodBlendWeights_4[chunkLocalVertIdxDiv4], _mm_set_ps1((real)i - 1)), lodSelected_4[chunkLocalVertIdxDiv4], cmp2Mask);

								__m128 cmpMask4 = _mm_and_ps(mask0_4, _mm_cmpeq_ps(i_4, _mm_set_ps1((real)(waveSamplingLODs.size() - 1))));
								lodSelected_4[chunkLocalVertIdxDiv4] = _mm_blendv_ps(_mm_add_ps(lodBlendWeights_4[chunkLocalVertIdxDiv4], i_4), lodSelected_4[chunkLocalVertIdxDiv4], cmpMask4);
							}
						}
					}
				}

				// Modulate based on waves
				for (const GerstnerWave::WaveInfo& waveInfo : waveContributions)
				{
					// TODO: Early out once wave amplitude isn't used by any vert in chunk

					if (waveInfo.enabled && (soloWave == nullptr || soloWave == &waveInfo))
					{
						const glm::vec2 waveVec = glm::vec2(waveInfo.waveDirCos, waveInfo.waveDirSin) * waveInfo.waveVecMag;
						const glm::vec2 waveVecN = glm::normalize(waveVec);

						__m128 accumOffset_4 = _mm_set_ps1(waveInfo.accumOffset);
						__m128 negWaveVecNX_4 = _mm_set_ps1(-waveVecN.x);
						__m128 negWaveVecNZ_4 = _mm_set_ps1(-waveVecN.y);
						__m128 waveA_4 = _mm_set_ps1(waveInfo.a);

						__m128 waveVecX_4 = _mm_set_ps1(waveVec.x);
						__m128 waveVecZ_4 = _mm_set_ps1(waveVec.y);

						//u32 countMinOne = chunkVertCountPerAxis - 1;

						for (i32 z = 0; z < chunkVertCountPerAxis; ++z)
						{
							//__m128 zIdxMin_4 = _mm_set_ps1((real)z);
							//__m128 zIdxMax_4 = _mm_set_ps1((real)(countMinOne - z));

							for (i32 x = 0; x < chunkVertCountPerAxis; x += 4)
							{
								const i32 chunkLocalVertIdx = z * chunkVertCountPerAxis + x;
								const i32 chunkLocalVertIdxDiv4 = chunkLocalVertIdx / 4;

								/*
								real blendFactor = glm::min(
									glm::min(
										bRightBlend ? glm::clamp((real)(chunkVertCountPerAxis - x - 1) / blendVertCount, 0.0f, 1.0f) : 1.0f,
										bLeftBlend ? glm::clamp((real)x / blendVertCount, 0.0f, 1.0f) : 1.0f),
										glm::min(
											bForwardBlend ? glm::clamp((real)z / blendVertCount, 0.0f, 1.0f) : 1.0f,
											bBackBlend ? glm::clamp(1.0f - (real)z / blendVertCount, 0.0f, 1.0f) : 1.0f));
								*/

								__m128 blendMask_4 = _mm_cmplt_ps(waveA_4, lodNextCutoffAmplitudes_4[chunkLocalVertIdxDiv4]);
								__m128 blendFactor_4 = _mm_blendv_ps(
									_mm_blendv_ps(one_4, lodBlendWeights_4[chunkLocalVertIdxDiv4], blendMask_4),
									zero_4, _mm_cmplt_ps(waveA_4, lodCutoffsAmplitudes_4[chunkLocalVertIdxDiv4]));

								/*
									positions[vertIdx] = chunkCenter + glm::vec3(
										size * ((real)x / (chunkVertCountPerAxis - 1)),
										0.0f,
										size * ((real)z / (chunkVertCountPerAxis - 1)));

									real d = waveVec.x * positions[vertIdx].x + waveVec.y * positions[vertIdx].z;
									real c = cos(d + waveInfo.accumOffset);
									real s = sin(d + waveInfo.accumOffset);
									positions[vertIdx] += glm::vec3(
										-waveVecN.x * waveInfo.a * s,
										waveInfo.a * c,
										-waveVecN.y * waveInfo.a * s);
								*/


								__m128 d = _mm_add_ps(_mm_mul_ps(positionsx_4[chunkLocalVertIdxDiv4], waveVecX_4), _mm_mul_ps(positionsz_4[chunkLocalVertIdxDiv4], waveVecZ_4));

								__m128 totalAccum = _mm_add_ps(d, accumOffset_4);

								__m128 c = cos_ps(totalAccum);
								__m128 s = sin_ps(totalAccum);

								__m128 as = _mm_mul_ps(waveA_4, s);

								positionsx_4[chunkLocalVertIdxDiv4] = _mm_add_ps(positionsx_4[chunkLocalVertIdxDiv4], _mm_mul_ps(blendFactor_4, _mm_mul_ps(negWaveVecNX_4, as)));
								positionsy_4[chunkLocalVertIdxDiv4] = _mm_add_ps(positionsy_4[chunkLocalVertIdxDiv4], _mm_mul_ps(blendFactor_4, _mm_mul_ps(waveA_4, c)));
								positionsz_4[chunkLocalVertIdxDiv4] = _mm_add_ps(positionsz_4[chunkLocalVertIdxDiv4], _mm_mul_ps(blendFactor_4, _mm_mul_ps(negWaveVecNZ_4, as)));
							}
						}
					}
				}

#if 0

				// Ripple
				glm::vec3 ripplePos = VEC3_ZERO;
				real rippleAmp = 0.8f;
				real rippleLen = 0.6f;
				real rippleFadeOut = 12.0f;
				for (i32 z = 0; z < chunkVertCountPerAxis; ++z)
				{
					for (i32 x = 0; x < chunkVertCountPerAxis; ++x)
					{
						const i32 vertIdx = z * chunkVertCountPerAxis + x + waveChunk->vertOffset;

						glm::vec3 diff = (ripplePos - positions[vertIdx]);
						real d = glm::length(diff);
						diff = diff / d * rippleLen;
						real c = cos(g_SecElapsedSinceProgramStart * 1.8f - d);
						real s = sin(g_SecElapsedSinceProgramStart * 1.5f - d);
						real a = Lerp(0.0f, rippleAmp, 1.0f - Saturate(d / rippleFadeOut));
						positions[vertIdx] += glm::vec3(
							-diff.x * a * s,
							a * c,
							-diff.z * a * s);
					}
				}
#endif

				WRITE_BARRIER;

				Platform::AtomicIncrement(&wave_workQueueEntriesCompleted);
			}
			else
			{
				Platform::Sleep(2);
			}
		}

		return nullptr;
	}

	GerstnerWave::WaveChunk const* GetChunkAtPos(const glm::vec2& pos, const std::vector<GerstnerWave::WaveChunk>& waveChunks, real size)
	{
		glm::vec2i queryPosInt(ceil(pos.x / size + 0.5f), ceil(pos.y / size + 0.5f));
		for (u32 chunkIdx = 0; chunkIdx < (u32)waveChunks.size(); ++chunkIdx)
		{
			if (waveChunks[chunkIdx].index == queryPosInt)
			{
				return &waveChunks[chunkIdx];
			}
		}

		return nullptr;
	}

	GerstnerWave::WaveTessellationLOD const* GetTessellationLOD(u32 lodLevel, const std::vector<GerstnerWave::WaveTessellationLOD>& waveTessellationLODs)
	{
		if (lodLevel < (u32)waveTessellationLODs.size())
		{
			return &waveTessellationLODs[lodLevel];
		}
		return nullptr;
	}

	void GerstnerWave::UpdateNormalsForChunk(u32 chunkIdx)
	{
		WaveChunk const* waveChunk = &waveChunks[chunkIdx];
		WaveTessellationLOD const* tessellationLOD = GetTessellationLOD(waveChunk->tessellationLODLevel);

		glm::vec3* positions = m_VertexBufferCreateInfo.positions_3D.data();
		glm::vec3* normals = m_VertexBufferCreateInfo.normals.data();
		glm::vec3* tangents = m_VertexBufferCreateInfo.tangents.data();

		const u32 chunkVertCountPerAxis = tessellationLOD->vertCountPerAxis;

		const glm::vec2 chunkCenter((waveChunk->index.x - 0.5f) * size, (waveChunk->index.y - 0.5f) * size);

		WaveChunk const* chunkLeft = GetChunkAtPos(chunkCenter - glm::vec2(size, 0.0f));
		WaveChunk const* chunkRight = GetChunkAtPos(chunkCenter + glm::vec2(size, 0.0f));
		WaveChunk const* chunkBack = GetChunkAtPos(chunkCenter - glm::vec2(0.0f, size));
		WaveChunk const* chunkFor = GetChunkAtPos(chunkCenter + glm::vec2(0.0f, size));
		WaveTessellationLOD const* LODLeft = chunkLeft ? GetTessellationLOD(chunkLeft->tessellationLODLevel) : nullptr;
		WaveTessellationLOD const* LODRight = chunkRight ? GetTessellationLOD(chunkRight->tessellationLODLevel) : nullptr;
		WaveTessellationLOD const* LODBack = chunkBack ? GetTessellationLOD(chunkBack->tessellationLODLevel) : nullptr;
		WaveTessellationLOD const* LODFor = chunkFor ? GetTessellationLOD(chunkFor->tessellationLODLevel) : nullptr;

		for (u32 z = 0; z < chunkVertCountPerAxis; ++z)
		{
			for (u32 x = 0; x < chunkVertCountPerAxis; ++x)
			{
				const u32 localIdx = z * chunkVertCountPerAxis + x;
				const u32 vertIdx = localIdx + waveChunk->vertOffset;

				// glm::vec3 planePos = glm::vec3(
					// chunkCenter.x + size * ((real)x / (chunkVertCountPerAxis - 1)),
					// 0.0f,
					// chunkCenter.y + size * ((real)z / (chunkVertCountPerAxis - 1)));

				real left = 0.0f;
				real right = 0.0f;
				real back = 0.0f;
				real forward = 0.0f;
				if (x >= 1)
				{
					left = positions[vertIdx - 1].y;
				}
				else if (chunkLeft)
				{
					u32 zz = MapVertIndexAcrossLODs(z, tessellationLOD, LODLeft);
					left = positions[(zz * LODLeft->vertCountPerAxis + LODLeft->vertCountPerAxis - 2) + chunkLeft->vertOffset].y;
				}
				if (x < chunkVertCountPerAxis - 1)
				{
					right = positions[vertIdx + 1].y;
				}
				else if (chunkRight)
				{
					u32 zz = MapVertIndexAcrossLODs(z, tessellationLOD, LODRight);
					right = positions[(zz * LODRight->vertCountPerAxis + 1) + chunkRight->vertOffset].y;
				}
				if (z >= 1)
				{
					back = positions[vertIdx - chunkVertCountPerAxis].y;
				}
				else if (chunkBack)
				{
					u32 xx = MapVertIndexAcrossLODs(x, tessellationLOD, LODBack);
					back = positions[(LODBack->vertCountPerAxis - 2) * LODBack->vertCountPerAxis + xx + chunkBack->vertOffset].y;
				}
				if (z < chunkVertCountPerAxis - 1)
				{
					forward = positions[vertIdx + chunkVertCountPerAxis].y;
				}
				else if (chunkFor)
				{
					u32 xx = MapVertIndexAcrossLODs(x, tessellationLOD, LODFor);
					forward = positions[LODFor->vertCountPerAxis + xx + chunkFor->vertOffset].y;
				}

				real dX = left - right;
				real dZ = back - forward;
				normals[vertIdx] = glm::vec3(dX, 2.0f, dZ);
				tangents[vertIdx] = glm::normalize(-glm::cross(normals[vertIdx], glm::vec3(0.0f, 0.0f, 1.0f)));
			}
		}
	}

	u32 MapVertIndexAcrossLODs(u32 vertIndex, GerstnerWave::WaveTessellationLOD const* lod0, GerstnerWave::WaveTessellationLOD const* lod1)
	{
		if (lod0->vertCountPerAxis == lod1->vertCountPerAxis)
		{
			return vertIndex;
		}

		u32 result = (u32)((real)vertIndex / lod0->vertCountPerAxis * lod1->vertCountPerAxis);
		return result;
	}

	void GerstnerWave::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		ImGui::Text("Loaded chunks: %d", (u32)waveChunks.size());


		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.95f, 1.0f), "Gerstner");

		ImGui::DragFloat("Loaded distance", &loadRadius, 0.01f);
		if (ImGui::DragFloat("Update speed", &updateSpeed, 0.1f))
		{
			updateSpeed = glm::clamp(updateSpeed, 0.1f, 10000.0f);
			for (i32 i = 0; i < (i32)waveContributions.size(); ++i)
			{
				UpdateDependentVariables(i);
			}
		}

		ImGui::SliderFloat("Blend dist", &blendDist, 0.0f, size);

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

		ImGui::SliderFloat("Reverse wave amp", &reverseWaveAmplitude, 0.0f, 2.0f);

		// TODO: Remove
		ImGuiExt::ColorEdit3Gamma("Top", &oceanData.top.x);
		ImGuiExt::ColorEdit3Gamma("Mid", &oceanData.mid.x);
		ImGuiExt::ColorEdit3Gamma("Bottom", &oceanData.btm.x);
		ImGui::SliderFloat("Fresnel factor", &oceanData.fresnelFactor, 0.0f, 1.0f);
		ImGui::DragFloat("Fresnel power", &oceanData.fresnelPower, 0.01f, 0.0f, 75.0f);
		ImGui::SliderFloat("Sky reflection", &oceanData.skyReflectionFactor, 0.0f, 1.0f);
		ImGui::DragFloat("Fog falloff", &oceanData.fogFalloff, 0.01f, 0.0f, 3.0f);
		ImGui::DragFloat("Fog density", &oceanData.fogDensity, 0.01f, 0.0f, 3.0f);

		if (ImGuiExt::SliderUInt("Max chunk vert count", &maxChunkVertCountPerAxis, 4u, 256u))
		{
			maxChunkVertCountPerAxis = glm::clamp(maxChunkVertCountPerAxis - (maxChunkVertCountPerAxis % 4), 4u, 256u);
		}

		if (ImGui::SliderFloat("Chunk size", &size, 32.0f, 1024.0f))
		{
			size = glm::clamp(size, 32.0f, 1024.0f);
		}

		ImGui::Checkbox("Disable LODs", &bDisableLODs);

		glm::vec3 bobberPosWS = bobber->GetTransform()->GetWorldPosition();
		ImGui::DragFloat3("Bobber", &bobberPosWS.x);

		ImGui::PushItemWidth(30.0f);
		ImGui::DragFloat("Bobber DR", &bobberTarget.DR, 0.01f);
		ImGui::SameLine();
		ImGui::DragFloat("UAF", &bobberTarget.UAF, 0.01f);
		ImGui::PopItemWidth();

		// Vertex buffer size
		{
			ImGui::NewLine();

			MeshComponent* meshComponent = m_Mesh->GetSubMesh(0);

			{
				char byteCountStr[64];
				ByteCountToString(byteCountStr, 64, (u64)(m_VertexBufferCreateInfo.positions_3D.size() * sizeof(glm::vec3)));

				char nameBuf[256];
				snprintf(nameBuf, 256, "CPU Vertex buffer size %s", byteCountStr);

				real usage = (real)DEBUG_lastUsedVertCount / meshComponent->GetVertexBufferData()->VertexCount;
				ImGui::ProgressBar(usage, ImVec2(0, 0), "");
				ImGui::Text("%s", nameBuf);
			}

			{
				u32 gpuSize = meshComponent->GetVertexBufferData()->UsedVertexBufferSize;
				u32 usedGPUSize = g_Renderer->GetDynamicVertexBufferUsedSize(meshComponent->renderID);

				char byteCountStr[64];
				ByteCountToString(byteCountStr, 64, gpuSize);

				char nameBuf[256];
				snprintf(nameBuf, 256, "GPU Vertex buffer size %s", byteCountStr);

				real usage = (real)usedGPUSize / gpuSize;
				ImGui::ProgressBar(usage, ImVec2(0, 0), "");
				ImGui::Text("%s", nameBuf);
			}

			ImGui::NewLine();
		}

		if (ImGui::TreeNode("Sampling LODs"))
		{
			static bool bNeedsSort = false;

			for (i32 i = 0; i < (i32)waveSamplingLODs.size(); ++i)
			{
				std::string childName = "##samp " + IntToString(i, 2);
				std::string removeStr = "-" + childName;
				if (ImGui::Button(removeStr.c_str()))
				{
					waveSamplingLODs.erase(waveSamplingLODs.begin() + i);
					bNeedsSort = true;
					break;
				}

				ImGui::SameLine();

				std::string distStr = "distance" + childName;
				real dist = glm::sqrt(waveSamplingLODs[i].squareDist);
				if (ImGui::DragFloat(distStr.c_str(), &dist, 1.0f, 0.0f, 10000.0f))
				{
					waveSamplingLODs[i].squareDist = dist * dist;
					bNeedsSort = true;
				}

				std::string amplitudeStr = "amplitude" + childName;
				ImGui::DragFloat(amplitudeStr.c_str(), &waveSamplingLODs[i].amplitudeCutoff, 0.0001f, 0.0f, 10.0f);
			}

			if (ImGui::Button("+"))
			{
				real sqrDist = waveSamplingLODs.empty() ? 100.0f : waveSamplingLODs[waveSamplingLODs.size() - 1].squareDist + 10.0f;
				real amplitude = waveSamplingLODs.empty() ? 1.0f : waveSamplingLODs[waveSamplingLODs.size() - 1].amplitudeCutoff + 1.0f;
				waveSamplingLODs.emplace_back(sqrDist, amplitude);
				bNeedsSort = true;
			}

			ImGui::TreePop();

			if (bNeedsSort && ImGui::IsMouseReleased(0))
			{
				bNeedsSort = false;
				SortWaveSamplingLODs();
			}
		}

		if (ImGui::TreeNode("Tessellation LODs"))
		{
			static bool bNeedsSort = false;

			for (i32 i = 0; i < (i32)waveTessellationLODs.size(); ++i)
			{
				std::string childName = "##tess " + IntToString(i, 2);
				std::string removeStr = "-" + childName;
				if (ImGui::Button(removeStr.c_str()))
				{
					waveTessellationLODs.erase(waveTessellationLODs.begin() + i);
					bNeedsSort = true;
					break;
				}

				ImGui::SameLine();

				std::string distStr = "distance" + childName;
				real dist = glm::sqrt(waveTessellationLODs[i].squareDist);
				if (ImGui::DragFloat(distStr.c_str(), &dist, 1.0f, 0.0f, 10000.0f))
				{
					waveTessellationLODs[i].squareDist = dist * dist;
					bNeedsSort = true;
				}
				std::string vertCountStr = "vert count per axis" + childName;
				if (ImGuiExt::SliderUInt(vertCountStr.c_str(), &waveTessellationLODs[i].vertCountPerAxis, 4u, maxChunkVertCountPerAxis))
				{
					waveTessellationLODs[i].vertCountPerAxis = glm::clamp(waveTessellationLODs[i].vertCountPerAxis - (waveTessellationLODs[i].vertCountPerAxis % 4), 4u, maxChunkVertCountPerAxis);
				}
			}

			if (ImGui::Button("+"))
			{
				real sqrDist = waveTessellationLODs.empty() ? 100.0f : waveTessellationLODs[waveTessellationLODs.size() - 1].squareDist + 10.0f;
				u32 vertCount = 16;
				waveTessellationLODs.emplace_back(sqrDist, vertCount);
				bNeedsSort = true;
			}

			ImGui::TreePop();

			if (bNeedsSort && ImGui::IsMouseReleased(0))
			{
				bNeedsSort = false;
				SortWaveTessellationLODs();
			}
		}

		if (ImGui::TreeNode("Wave factors"))
		{
			for (i32 i = 0; i < (i32)waveContributions.size(); ++i)
			{
				const bool bWasDisabled = (soloWaveIndex != -1) && (soloWaveIndex != i);
				if (bWasDisabled)
				{
					ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
					ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
				}

				std::string childName = "##wave " + IntToString(i, 2);

				std::string removeStr = "-" + childName;
				if (ImGui::Button(removeStr.c_str()))
				{
					RemoveWave(i);
					break;
				}

				ImGui::SameLine();

				static bool bNeedUpdate = false;
				static bool bNeedSort = false;

				std::string enabledStr = "Enabled" + childName;

				ImGui::Checkbox(enabledStr.c_str(), &waveContributions[i].enabled);

				ImGui::SameLine();

				bool bSolo = soloWaveIndex == i;
				std::string soloStr = "Solo" + childName;
				if (ImGui::Checkbox(soloStr.c_str(), &bSolo))
				{
					if (bSolo)
					{
						soloWaveIndex = i;
					}
					else
					{
						soloWaveIndex = -1;
					}
				}

				std::string aStr = "amplitude" + childName;
				bNeedUpdate |= ImGui::DragFloat(aStr.c_str(), &waveContributions[i].a, 0.01f);
				bNeedSort |= bNeedUpdate;
				std::string waveLenStr = "wave len" + childName;
				bNeedUpdate |= ImGui::DragFloat(waveLenStr.c_str(), &waveContributions[i].waveLen, 0.01f);
				bNeedSort |= bNeedUpdate;
				std::string dirStr = "dir" + childName;
				if (ImGui::DragFloat(dirStr.c_str(), &waveContributions[i].waveDirTheta, 0.001f, 0.0f, 10.0f))
				{
					bNeedUpdate = true;
					waveContributions[i].waveDirTheta = fmod(waveContributions[i].waveDirTheta, TWO_PI);
				}

				if (bNeedSort && ImGui::IsMouseReleased(0))
				{
					bNeedSort = false;
					SortWaves();
				}

				if (bNeedUpdate)
				{
					bNeedUpdate = false;
					UpdateDependentVariables(i);
				}

				if (bWasDisabled)
				{
					ImGui::PopItemFlag();
					ImGui::PopStyleVar();
				}

				ImGui::Separator();
			}

			if (ImGui::Button("+"))
			{
				AddWave();
			}

			ImGui::TreePop();
		}
	}

	bool operator==(const GerstnerWave::WaveInfo& lhs, const GerstnerWave::WaveInfo& rhs)
	{
		return memcmp(&lhs, &rhs, sizeof(GerstnerWave::WaveInfo)) == 0;
	}

	void GerstnerWave::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject gerstnerWaveObj;
		if (parentObject.TryGetObject("gerstner wave", gerstnerWaveObj))
		{
			std::vector<JSONObject> waveObjs;
			if (gerstnerWaveObj.TryGetObjectArray("waves", waveObjs))
			{
				for (const JSONObject& waveObj : waveObjs)
				{
					WaveInfo waveInfo = {};
					waveInfo.enabled = waveObj.GetBool("enabled");
					waveInfo.a = waveObj.GetFloat("amplitude");
					waveInfo.waveDirTheta = waveObj.GetFloat("waveDir");
					waveInfo.waveLen = waveObj.GetFloat("waveLen");
					waveContributions.push_back(waveInfo);
				}
			}

			gerstnerWaveObj.TryGetUInt("max chunk vert count per axis", maxChunkVertCountPerAxis);
			gerstnerWaveObj.TryGetFloat("chunk size", size);
			gerstnerWaveObj.TryGetFloat("chunk load radius", loadRadius);
			gerstnerWaveObj.TryGetFloat("update speed", updateSpeed);

			gerstnerWaveObj.TryGetBool("pin center", m_bPinCenter);
			gerstnerWaveObj.TryGetVec3("pinned center position", m_PinnedPos);

			gerstnerWaveObj.TryGetFloat("blend dist", blendDist);

			std::vector<JSONObject> waveSamplingLODsArrObj;
			if (gerstnerWaveObj.TryGetObjectArray("wave sampling lods", waveSamplingLODsArrObj))
			{
				waveSamplingLODs.clear();
				waveSamplingLODs.reserve(waveSamplingLODsArrObj.size());
				for (u32 i = 0; i < (u32)waveSamplingLODsArrObj.size(); ++i)
				{
					real sqrDist;
					waveSamplingLODsArrObj[i].TryGetFloat("square dist", sqrDist);
					real amplitude;
					waveSamplingLODsArrObj[i].TryGetFloat("amplitude cutoff", amplitude);

					waveSamplingLODs.emplace_back(sqrDist, amplitude);
				}

				SortWaveSamplingLODs();
			}
			else
			{
				// Set good default value
				waveSamplingLODs.clear();
				waveSamplingLODs.reserve(1);
				waveSamplingLODs.emplace_back(35721.0f, 0.0f);
			}

			std::vector<JSONObject> waveTessellationLODsArrObj;
			if (gerstnerWaveObj.TryGetObjectArray("wave tessellation lods", waveTessellationLODsArrObj))
			{
				waveTessellationLODs.clear();
				waveTessellationLODs.reserve(waveTessellationLODsArrObj.size());
				for (u32 i = 0; i < (u32)waveTessellationLODsArrObj.size(); ++i)
				{
					real sqrDist;
					waveTessellationLODsArrObj[i].TryGetFloat("square dist", sqrDist);
					u32 vertCount;
					waveTessellationLODsArrObj[i].TryGetUInt("vert count per axis", vertCount);

					waveTessellationLODs.emplace_back(sqrDist, vertCount);
				}
			}
			else
			{
				// Set good default value
				waveTessellationLODs.clear();
				waveTessellationLODs.reserve(4);
				waveTessellationLODs.emplace_back(37636.0f, 8);
				waveTessellationLODs.emplace_back(14438.9f, 28);
				waveTessellationLODs.emplace_back(4116.8f, 52);
				waveTessellationLODs.emplace_back(0.0f, 80);
			}

			if (gerstnerWaveObj.TryGetVec4("colour top", oceanData.top))
			{
				oceanData.top = glm::pow(oceanData.top, glm::vec4(2.2f));
			}
			if (gerstnerWaveObj.TryGetVec4("colour mid", oceanData.mid))
			{
				oceanData.mid = glm::pow(oceanData.mid, glm::vec4(2.2f));
			}
			if (gerstnerWaveObj.TryGetVec4("colour btm", oceanData.btm))
			{
				oceanData.btm = glm::pow(oceanData.btm, glm::vec4(2.2f));
			}
		}

		SortWaves();

		// Init dependent variables
		for (i32 i = 0; i < (i32)waveContributions.size(); ++i)
		{
			UpdateDependentVariables(i);
		}
	}

	void GerstnerWave::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject gerstnerWaveObj = {};

		std::vector<JSONObject> waveObjs;
		waveObjs.resize(waveContributions.size());
		for (i32 i = 0; i < (i32)waveContributions.size(); ++i)
		{
			JSONObject& waveObj = waveObjs[i];
			waveObj.fields.emplace_back("enabled", JSONValue(waveContributions[i].enabled));
			waveObj.fields.emplace_back("amplitude", JSONValue(waveContributions[i].a));
			waveObj.fields.emplace_back("waveDir", JSONValue(waveContributions[i].waveDirTheta));
			waveObj.fields.emplace_back("waveLen", JSONValue(waveContributions[i].waveLen));
		}

		gerstnerWaveObj.fields.emplace_back("waves", JSONValue(waveObjs));

		gerstnerWaveObj.fields.emplace_back("max chunk vert count per axis", JSONValue(maxChunkVertCountPerAxis));
		gerstnerWaveObj.fields.emplace_back("chunk size", JSONValue(size));
		gerstnerWaveObj.fields.emplace_back("chunk load radius", JSONValue(loadRadius));
		gerstnerWaveObj.fields.emplace_back("update speed", JSONValue(updateSpeed));

		gerstnerWaveObj.fields.emplace_back("pin center", JSONValue(m_bPinCenter));
		gerstnerWaveObj.fields.emplace_back("pinned center position", JSONValue(m_PinnedPos, 2));

		gerstnerWaveObj.fields.emplace_back("blend dist", JSONValue(blendDist));

		std::vector<JSONObject> waveSamplingLODsArrObj(waveSamplingLODs.size());
		for (u32 i = 0; i < (u32)waveSamplingLODs.size(); ++i)
		{
			JSONObject samplingLOD = {};
			samplingLOD.fields.emplace_back("square dist", JSONValue(waveSamplingLODs[i].squareDist, 1));
			samplingLOD.fields.emplace_back("amplitude cutoff", JSONValue(waveSamplingLODs[i].amplitudeCutoff));
			waveSamplingLODsArrObj[i] = samplingLOD;
		}
		gerstnerWaveObj.fields.emplace_back("wave sampling lods", JSONValue(waveSamplingLODsArrObj));

		std::vector<JSONObject> waveTessellationLODsArrObj(waveTessellationLODs.size());
		for (u32 i = 0; i < (u32)waveTessellationLODs.size(); ++i)
		{
			JSONObject tessellationLOD = {};
			tessellationLOD.fields.emplace_back("square dist", JSONValue(waveTessellationLODs[i].squareDist, 1));
			tessellationLOD.fields.emplace_back("vert count per axis", JSONValue(waveTessellationLODs[i].vertCountPerAxis));
			waveTessellationLODsArrObj[i] = tessellationLOD;
		}
		gerstnerWaveObj.fields.emplace_back("wave tessellation lods", JSONValue(waveTessellationLODsArrObj));

		gerstnerWaveObj.fields.emplace_back("colour top", JSONValue(glm::pow(oceanData.top, glm::vec4(1.0f / 2.2f)), 2));
		gerstnerWaveObj.fields.emplace_back("colour mid", JSONValue(glm::pow(oceanData.mid, glm::vec4(1.0f / 2.2f)), 2));
		gerstnerWaveObj.fields.emplace_back("colour btm", JSONValue(glm::pow(oceanData.btm, glm::vec4(1.0f / 2.2f)), 2));

		SerializeRegisteredProperties(gerstnerWaveObj, collection->childCollection, bSerializePrefabData);

		parentObject.fields.emplace_back("gerstner wave", JSONValue(gerstnerWaveObj));
	}

	void GerstnerWave::UpdateDependentVariables(i32 waveIndex)
	{
		if (waveIndex >= 0 && waveIndex < (i32)waveContributions.size())
		{
			waveContributions[waveIndex].waveVecMag = TWO_PI / waveContributions[waveIndex].waveLen;
			waveContributions[waveIndex].moveSpeed = glm::sqrt(updateSpeed * waveContributions[waveIndex].waveVecMag);

			waveContributions[waveIndex].waveDirCos = cos(waveContributions[waveIndex].waveDirTheta);
			waveContributions[waveIndex].waveDirSin = sin(waveContributions[waveIndex].waveDirTheta);
		}
	}

	void GerstnerWave::AddWave()
	{
		waveContributions.push_back({});
		UpdateDependentVariables((u32)waveContributions.size() - 1);
		SortWaves();
	}

	void GerstnerWave::RemoveWave(i32 index)
	{
		if (index >= 0 && index < (i32)waveContributions.size())
		{
			waveContributions.erase(waveContributions.begin() + index);
			SortWaves();
		}
	}

	void GerstnerWave::SortWaves()
	{
		WaveInfo soloWaveCopy;
		if (soloWaveIndex != -1)
		{
			soloWaveCopy = waveContributions[soloWaveIndex];
		}
		std::sort(waveContributions.begin(), waveContributions.end(),
			[](const WaveInfo& waveA, const WaveInfo& waveB)
		{
			return abs(waveA.a) > abs(waveB.a);
		});
		if (soloWaveIndex != -1)
		{
			for (i32 i = 0; i < (i32)waveContributions.size(); ++i)
			{
				const WaveInfo& waveInfo = waveContributions[i];
				if (waveInfo == soloWaveCopy)
				{
					soloWaveIndex = i;
					break;
				}
			}
		}
	}

	void GerstnerWave::SortWaveSamplingLODs()
	{
		std::sort(waveSamplingLODs.begin(), waveSamplingLODs.end(),
			[](const WaveSamplingLOD& lodA, const WaveSamplingLOD& lodB)
		{
			return abs(lodA.squareDist) > abs(lodB.squareDist);
		});
	}

	void GerstnerWave::SortWaveTessellationLODs()
	{
		std::sort(waveTessellationLODs.begin(), waveTessellationLODs.end(),
			[](const WaveTessellationLOD& lodA, const WaveTessellationLOD& lodB)
		{
			return abs(lodA.squareDist) > abs(lodB.squareDist);
		});
	}

	real GerstnerWave::GetWaveAmplitudeLODCutoffForDistance(real dist) const
	{
		if (bDisableLODs)
		{
			return 0.0f;
		}

		for (u32 i = 0; i < (u32)waveSamplingLODs.size(); ++i)
		{
			if (dist >= waveSamplingLODs[i].squareDist)
			{
				return waveSamplingLODs[i].amplitudeCutoff;
			}
		}
		return 0.0f;
	}

	Wire::Wire(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, WireSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		if (!m_bIsTemplate)
		{
			GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->RegisterWire(this);
		}
	}

	PropertyCollection* Wire::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("wire");

		collection->childCollection->RegisterProperty("plug 0 id", FIELD_OFFSET_AND_TYPE(Wire, plug0ID))
			.VersionAdded(6);
		collection->childCollection->RegisterProperty("plug 1 id", FIELD_OFFSET_AND_TYPE(Wire, plug1ID))
			.VersionAdded(6);

		return collection;
	}

	void Wire::Initialize()
	{
		GeneratePoints();

		MaterialID wireMatID;
		if (!g_Renderer->FindOrCreateMaterialByName("wire", wireMatID))
		{
			PrintWarn("Failed to find wire material, using placeholder\n");
			wireMatID = g_Renderer->GetPlaceholderMaterialID();
		}
		m_VertexBufferCreateInfo = {};
		m_VertexBufferCreateInfo.attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(wireMatID)->shaderID)->vertexAttributes;

		UpdateIndices();

		SetMesh(new Mesh(this));
		Mesh::CreateInfo meshCreateInfo = {};
		meshCreateInfo.bDynamic = true;
		meshCreateInfo.vertexBufferCreateInfo = &m_VertexBufferCreateInfo;
		meshCreateInfo.indices = m_Indices;
		meshCreateInfo.materialIDs = { wireMatID };
		meshCreateInfo.initialMaxVertexCount = numRadialPoints * numPoints;
		m_Mesh->LoadFromMemory(meshCreateInfo);

		GameObject::Initialize();
	}

	void Wire::PostInitialize()
	{
		if (plug0ID.IsValid())
		{
			WirePlug* plug0 = (WirePlug*)plug0ID.Get();
			if (!plug0->wireID.IsValid())
			{
				plug0->wireID = ID;
			}
			else
			{
				if (plug0->wireID != ID)
				{
					StringBuilder plugPath;
					plug0->GetFullyPathedName(plugPath);
					PrintError("Wire plug \"%s\" references unexpected wire\n", plugPath.ToCString());
				}
			}
		}

		if (plug1ID.IsValid())
		{
			WirePlug* plug1 = (WirePlug*)plug1ID.Get();
			if (!plug1->wireID.IsValid())
			{
				plug1->wireID = ID;
			}
			else
			{
				if (plug1->wireID != ID)
				{
					StringBuilder plugPath;
					plug1->GetFullyPathedName(plugPath);
					PrintError("Wire plug \"%s\" references unexpected wire\n", plugPath.ToCString());
				}
			}
		}


		GameObject::PostInitialize();
	}

	void Wire::Update()
	{
		PROFILE_AUTO("Wire Update");

		UpdateIndices();
		UpdateMesh();

		MeshComponent* meshComponent = m_Mesh->GetSubMesh(0);
		if (!m_Indices.empty())
		{
			meshComponent->UpdateDynamicVertexData(m_VertexBufferCreateInfo, m_Indices);
			g_Renderer->ShrinkDynamicVertexData(meshComponent->renderID, 0.5f);
		}

		GameObject::Update();
	}

	void Wire::Destroy(bool bDetachFromParent /* = true */)
	{
		if (!m_bIsTemplate)
		{
			GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->UnregisterWire(this);
		}

		DestroyPoints();

		GameObject::Destroy(bDetachFromParent);
	}

	void Wire::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		bool bRegenPoints = false;

		if (ImGui::SliderInt("num points", &numPoints, 4, 24))
		{
			bRegenPoints = true;
			numPoints = glm::clamp(numPoints, 4, 24);
		}
		bRegenPoints = ImGui::SliderFloat("point inv mass", &pointInvMass, 0.0f, 0.1f) || bRegenPoints;

		if (ImGui::SliderFloat("stiffness", &stiffness, 0.0f, 1.0f))
		{
			m_SoftBody->SetStiffness(stiffness);
		}

		if (ImGui::SliderFloat("damping", &damping, 0.0f, 1.0f))
		{
			m_SoftBody->SetDamping(damping);
		}

		if (ImGui::Button("Reset points") || bRegenPoints)
		{
			DestroyPoints();
			GeneratePoints();
			UpdateIndices();
			UpdateMesh();
		}
	}

	bool Wire::ShouldSerialize()
	{
		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		if (player != nullptr)
		{
			if (player->IsHolding(plug0ID.Get()) || player->IsHolding(plug1ID.Get()))
			{
				// Don't serialize wires being held
				return false;
			}
		}

		return true;
	}

	void Wire::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject wireInfo;
		if (parentObject.TryGetObject("wire", wireInfo))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(wireInfo, collection->childCollection);
		}
	}

	void Wire::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject wireInfo = {};
		SerializeRegisteredProperties(wireInfo, collection->childCollection, bSerializePrefabData);
		if (!wireInfo.fields.empty())
		{
			parentObject.fields.emplace_back("wire", JSONValue(wireInfo));
		}
	}

	GameObjectID Wire::GetOtherPlug(WirePlug* plug)
	{
		if (plug->ID == plug0ID)
		{
			return plug1ID;
		}
		else if (plug->ID == plug1ID)
		{
			return plug0ID;
		}

		return InvalidGameObjectID;
	}

	void Wire::SetStartTangent(const glm::vec3& tangent)
	{
		real dist = 2.0f / numPoints;
		m_SoftBody->points[1]->pos = m_SoftBody->points[0]->pos + tangent * dist;
		m_SoftBody->points[1]->invMass = 0.0f;
	}

	void Wire::ClearStartTangent()
	{
		m_SoftBody->points[1]->invMass = m_SoftBody->points[2]->invMass;
	}

	void Wire::SetEndTangent(const glm::vec3& tangent)
	{
		real dist = 2.0f / numPoints;
		m_SoftBody->points[numPoints - 2]->pos = m_SoftBody->points[numPoints - 1]->pos + tangent * dist;
		m_SoftBody->points[numPoints - 2]->invMass = 0.0f;
	}

	void Wire::ClearEndTangent()
	{
		m_SoftBody->points[numPoints - 2]->invMass = m_SoftBody->points[numPoints - 3]->invMass;
	}

	void Wire::UpdateWireMesh()
	{
		CHECK_NE(m_SoftBody, nullptr);
		WirePlug* plug0 = (WirePlug*)plug0ID.Get();
		Transform* plug0Transform = plug0->GetTransform();
		WirePlug* plug1 = (WirePlug*)plug1ID.Get();
		Transform* plug1Transform = plug1->GetTransform();

		m_SoftBody->points[0]->pos = plug0Transform->GetWorldPosition();
		m_SoftBody->points[m_SoftBody->points.size() - 1]->pos = plug1Transform->GetWorldPosition();

		m_SoftBody->Update();
	}

	glm::vec3 Wire::CalculateTangentAtPoint(real t)
	{
		CHECK_NE(m_SoftBody, nullptr);
		CHECK(t >= 0.0f && t <= 1.0f);
		if (t == 1.0f)
		{
			return glm::normalize(m_SoftBody->points[numPoints - 1]->pos - m_SoftBody->points[numPoints - 2]->pos);
		}
		else
		{
			u32 i0 = (u32)(t * numPoints);
			u32 i1 = (u32)(t * numPoints + 1.0f);
			return glm::normalize(m_SoftBody->points[i1]->pos - m_SoftBody->points[i0]->pos);
		}
	}

	void Wire::CalculateBasisAtPoint(real t, glm::vec3& outNormal, glm::vec3& outTangent, glm::vec3& outBitangent)
	{
		CHECK_NE(m_SoftBody, nullptr);
		CHECK(t >= 0.0f && t <= 1.0f);
		if (t == 1.0f)
		{
			outTangent = glm::normalize(m_SoftBody->points[numPoints - 1]->pos - m_SoftBody->points[numPoints - 2]->pos);
		}
		else
		{
			u32 i0 = (u32)(t * numPoints);
			u32 i1 = (u32)(t * numPoints + 1.0f);
			outTangent = glm::normalize(m_SoftBody->points[i1]->pos - m_SoftBody->points[i0]->pos);
		}
		// TODO: Take tangent with VEC3_RIGHT when UP is similar to tangent
		outBitangent = glm::normalize(glm::cross(VEC3_UP, outTangent));
		outNormal = glm::normalize(glm::cross(outTangent, outBitangent));
		outBitangent = glm::normalize(glm::cross(outNormal, outTangent));
	}

	void Wire::DestroyPoints()
	{
		if (m_SoftBody != nullptr)
		{
			m_SoftBody->Destroy(true);
			delete m_SoftBody;
			m_SoftBody = nullptr;
		}
	}

	void Wire::GeneratePoints()
	{
		CHECK_EQ(m_SoftBody, nullptr);

		WirePlug* plug0 = (WirePlug*)plug0ID.Get();
		Transform* plug0Transform = plug0->GetTransform();
		WirePlug* plug1 = (WirePlug*)plug1ID.Get();
		Transform* plug1Transform = plug1->GetTransform();

		Point* startPoint = new Point(plug0Transform->GetWorldPosition(), VEC3_ZERO, 0.0f);
		Point* endPoint = new Point(plug1Transform->GetWorldPosition(), VEC3_ZERO, 0.0f);

		m_SoftBody = new SoftBody("Wire simulation");
		m_SoftBody->SetRenderWireframe(false);

		m_SoftBody->points.reserve(numPoints);
		for (i32 i = 0; i < numPoints; ++i)
		{
			real invMass = (i == 0 || i == (numPoints - 1)) ? 0.0f : pointInvMass;
			real t = (real)i / (numPoints - 1);
			glm::vec3 pos = startPoint->pos + (endPoint->pos - startPoint->pos) * t;
			m_SoftBody->points.push_back(new Point(pos, VEC3_ZERO, invMass));
		}

		for (i32 i = 0; i < numPoints - 1; ++i)
		{
			m_SoftBody->AddUniqueDistanceConstraint(i, i + 1, i, stiffness);
		}

		m_SoftBody->SetSerializable(false);
		m_SoftBody->SetVisibleInSceneExplorer(false);
		m_SoftBody->SetOwnedInternally(true);
		m_SoftBody->SetStiffness(stiffness);
		m_SoftBody->SetDamping(damping);
		m_SoftBody->Initialize();
		m_SoftBody->PostInitialize();
	}

	void Wire::UpdateMesh()
	{
		WirePlug* plug0 = (WirePlug*)plug0ID.Get();
		Transform* plug0Transform = plug0->GetTransform();
		glm::vec3 plug0Pos = plug0Transform->GetWorldPosition();

		WirePlug* plug1 = (WirePlug*)plug1ID.Get();
		Transform* plug1Transform = plug1->GetTransform();
		glm::vec3 plug1Pos = plug1Transform->GetWorldPosition();

		DebugRenderer* debugRenderer = g_Renderer->GetDebugRenderer();
		debugRenderer->drawLine(ToBtVec3(plug0Pos), ToBtVec3(plug0Pos + VEC3_RIGHT), btVector3(1, 1, 1));
		debugRenderer->drawLine(ToBtVec3(plug0Pos), ToBtVec3(plug0Pos + VEC3_UP), btVector3(1, 1, 1));
		debugRenderer->drawLine(ToBtVec3(plug0Pos), ToBtVec3(plug0Pos + VEC3_FORWARD), btVector3(1, 1, 1));

		debugRenderer->drawLine(ToBtVec3(plug1Pos), ToBtVec3(plug1Pos + VEC3_RIGHT), btVector3(1, 0.5f, 1));
		debugRenderer->drawLine(ToBtVec3(plug1Pos), ToBtVec3(plug1Pos + VEC3_UP), btVector3(1, 0.5f, 1));
		debugRenderer->drawLine(ToBtVec3(plug1Pos), ToBtVec3(plug1Pos + VEC3_FORWARD), btVector3(1, 0.5f, 1));

		glm::vec3 startToEnd = plug1Pos - plug0Pos;

		u32 numVerts = numPoints * numRadialPoints;

		m_VertexBufferCreateInfo.positions_3D.clear();
		m_VertexBufferCreateInfo.texCoords_UV.clear();
		m_VertexBufferCreateInfo.colours_R32G32B32A32.clear();
		m_VertexBufferCreateInfo.normals.clear();
		m_VertexBufferCreateInfo.tangents.clear();

		m_VertexBufferCreateInfo.positions_3D.resize(numVerts);
		m_VertexBufferCreateInfo.texCoords_UV.resize(numVerts);
		m_VertexBufferCreateInfo.colours_R32G32B32A32.resize(numVerts);
		m_VertexBufferCreateInfo.normals.resize(numVerts);
		m_VertexBufferCreateInfo.tangents.resize(numVerts);

		glm::vec3 wirePosWS = m_Transform.GetWorldPosition();

		u32 vertIndex = 0;
		real radius = 0.05f;
		glm::vec4 color(0.3f, 0.3f, 0.35f, 1.0f);
		for (i32 i = 0; i < numPoints; ++i)
		{
			real t = i / (real)(numPoints - 1);
			glm::vec3 centerPos = m_SoftBody->points[i]->pos - wirePosWS;
			glm::vec3 wireNormalWS, wireTangentWS, wireBitangentWS;
			CalculateBasisAtPoint(t, wireNormalWS, wireTangentWS, wireBitangentWS);
			for (i32 j = 0; j < numRadialPoints; ++j)
			{
				real theta = j / (real)numRadialPoints;
				real cosTheta = cos(theta * TWO_PI);
				real sinTheta = sin(theta * TWO_PI);
				glm::vec3 pos = centerPos +
					(cosTheta * radius) * wireBitangentWS +
					(sinTheta * radius) * wireNormalWS;

				glm::vec3 normal = glm::normalize(cosTheta * wireBitangentWS + sinTheta * wireNormalWS);
				// TODO: Take cross product with VEC3_UP when normal is similar to VEC3_RIGHT
				glm::vec3 tangent = glm::normalize(glm::cross(normal, VEC3_RIGHT));

				m_VertexBufferCreateInfo.positions_3D[vertIndex] = pos;
				m_VertexBufferCreateInfo.texCoords_UV[vertIndex] = glm::vec2(theta, t);
				m_VertexBufferCreateInfo.colours_R32G32B32A32[vertIndex] = color;
				m_VertexBufferCreateInfo.normals[vertIndex] = normal;
				m_VertexBufferCreateInfo.tangents[vertIndex] = tangent;

				++vertIndex;
			}
		}

		MeshComponent* meshComponent = m_Mesh->GetSubMesh(0);
		meshComponent->UpdateDynamicVertexData(m_VertexBufferCreateInfo, m_Indices);
		g_Renderer->ShrinkDynamicVertexData(meshComponent->renderID, 0.5f);
	}

	void Wire::UpdateIndices()
	{
		u32 numIndices = (numPoints - 1) * numRadialPoints * 6;

		m_Indices.clear();
		m_Indices.resize(numIndices);

		i32 indexIndex = 0;
		for (i32 i = 0; i < numPoints - 1; ++i)
		{
			for (i32 j = 0; j < numRadialPoints; ++j)
			{
				m_Indices[indexIndex++] = i * numRadialPoints + j;
				m_Indices[indexIndex++] = (i + 1) * numRadialPoints + (j + 1) % numRadialPoints;
				m_Indices[indexIndex++] = (i + 1) * numRadialPoints + j;

				m_Indices[indexIndex++] = i * numRadialPoints + j;
				m_Indices[indexIndex++] = i * numRadialPoints + (j + 1) % numRadialPoints;
				m_Indices[indexIndex++] = (i + 1) * numRadialPoints + (j + 1) % numRadialPoints;
			}
		}
	}

	WirePlug::WirePlug(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, WirePlugSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		m_bInteractable = true;

		if (!m_bIsTemplate)
		{
			GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->RegisterWirePlug(this);
		}
	}

	PropertyCollection* WirePlug::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("wire plug");

		collection->childCollection->RegisterProperty("wire id", FIELD_OFFSET_AND_TYPE(WirePlug, wireID))
			.VersionAdded(6)
			.DefaultValue(InvalidGameObjectID);
		collection->childCollection->RegisterProperty("socket id", FIELD_OFFSET_AND_TYPE(WirePlug, socketID))
			.VersionAdded(6)
			.DefaultValue(InvalidGameObjectID);

		return collection;
	}

	WirePlug::WirePlug(const std::string& name, Wire* owningWire, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		WirePlug(name, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		wireID = owningWire->ID;
	}

	void WirePlug::Destroy(bool bDetachFromParent)
	{
		if (!m_bIsTemplate)
		{
			GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->UnregisterWirePlug(this);
		}

		GameObject::Destroy(bDetachFromParent);
	}

	void WirePlug::PlugIn(Socket* socket)
	{
		if (!socketID.IsValid())
		{
			socketID = socket->ID;
		}
		else
		{
			PrintError("Attempted to plug in to full socket!\n");
		}
	}

	void WirePlug::Unplug()
	{
		if (socketID.IsValid())
		{
			socketID = InvalidGameObjectID;
		}
		else
		{
			PrintError("Attempted to unplug socket that wasn't plugged in\n");
		}
	}

	void WirePlug::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject wireObj;
		if (parentObject.TryGetObject("wire plug", wireObj))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(wireObj, collection->childCollection);
		}
	}

	void WirePlug::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject wirePlugObj = {};
		SerializeRegisteredProperties(wirePlugObj, collection->childCollection, bSerializePrefabData);
		if (!wirePlugObj.fields.empty())
		{
			parentObject.fields.emplace_back("wire plug", JSONValue(wirePlugObj));
		}
	}

	Socket::Socket(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, SocketSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		m_bInteractable = true;

		if (!m_bIsTemplate)
		{
			GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->RegisterSocket(this);
		}
	}

	PropertyCollection* Socket::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("socket");

		collection->childCollection->RegisterProperty("slotIdx", FIELD_OFFSET_AND_TYPE(Socket, slotIdx))
			.VersionAdded(6);
		//	.CustomSerializeCallback([](SerializationContext& context, u32 value)
		//{
		//	if (value > 0)
		//	{
		//		context.Write("foo", value);
		//	}
		//});
		collection->childCollection->RegisterProperty("connected plug id", FIELD_OFFSET_AND_TYPE(Socket, connectedPlugID))
			.VersionAdded(6)
			.DefaultValue(InvalidGameObjectID);

		return collection;
	}

	void Socket::Destroy(bool bDetachFromParent /* = true */)
	{
		if (!m_bIsTemplate)
		{
			GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->UnregisterSocket(this);
		}

		GameObject::Destroy(bDetachFromParent);
	}

	void Socket::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject socketObj;
		if (parentObject.TryGetObject("socket", socketObj))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(socketObj, collection->childCollection);
		}
	}

	void Socket::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject socketObj = {};
		SerializeRegisteredProperties(socketObj, collection->childCollection, bSerializePrefabData);
		if (!socketObj.fields.empty())
		{
			parentObject.fields.emplace_back("socket", JSONValue(socketObj));
		}
	}

	void Socket::OnPlugIn(WirePlug* plug)
	{
		if (!connectedPlugID.IsValid())
		{
			connectedPlugID = plug->ID;
		}
		else
		{
			PrintError("Attempted to plug socket in when already full\n");
		}
	}

	void Socket::OnUnPlug()
	{
		if (connectedPlugID.IsValid())
		{
			connectedPlugID = InvalidGameObjectID;
		}
		else
		{
			PrintError("Attempted to unplug from socket which had nothing plugged in\n");
		}
	}

	Terminal::Terminal() :
		Terminal(g_SceneManager->CurrentScene()->GetUniqueObjectName("Terminal_", 2), InvalidGameObjectID)
	{
	}

	Terminal::Terminal(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, TerminalSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate),
		m_KeyEventCallback(this, &Terminal::OnKeyEvent),
		cursor(0, 0)
	{
		m_bInteractable = true;

		MaterialID matID;
		// TODO: Don't rely on material names!
		if (!g_Renderer->FindOrCreateMaterialByName("terminal copper", matID))
		{
			// TODO: Create own material
			matID = 0;
		}
		Mesh* mesh = SetMesh(new Mesh(this));
		if (!mesh->LoadFromFile(MESH_DIRECTORY "terminal-copper.glb", matID))
		{
			PrintWarn("Failed to load terminal mesh!\n");
		}

		m_VM = new VM::VirtualMachine();
	}

	PropertyCollection* Terminal::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();
		return collection;
	}

	void Terminal::Initialize()
	{
		g_InputManager->BindKeyEventCallback(&m_KeyEventCallback, 20);
		ParseCode();

		GetSystem<TerminalManager>(SystemType::TERMINAL_MANAGER)->RegisterTerminal(this);

		GameObject::Initialize();
	}

	void Terminal::Destroy(bool bDetachFromParent /* = true */)
	{
		g_InputManager->UnbindKeyEventCallback(&m_KeyEventCallback);

		if (m_VM != nullptr)
		{
			delete m_VM;
			m_VM = nullptr;
		}

		GetSystem<TerminalManager>(SystemType::TERMINAL_MANAGER)->DeregisterTerminal(this);

		GameObject::Destroy(bDetachFromParent);
	}

	void Terminal::Update()
	{
		PROFILE_AUTO("Terminal Update");

		PluggablesSystem* pluggablesSystem = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES);

		if (m_NearbyInteractableID.IsValid())
		{
			const real scale = 2.0f;
			const glm::vec3 right = m_Transform.GetRight();
			const glm::vec3 forward = m_Transform.GetForward();
			glm::vec3 c = m_Transform.GetWorldPosition();
			glm::vec3 v1 = c + (right + forward) * scale;
			glm::vec3 v2 = c + (-right + forward) * scale;
			glm::vec3 v3 = c - (forward * scale);
			DebugRenderer* debugRenderer = g_Renderer->GetDebugRenderer();
			debugRenderer->drawTriangle(ToBtVec3(v1), ToBtVec3(v2), ToBtVec3(v3), btVector3(0.9f, 0.3f, 0.2f), 1.0f);
			glm::vec3 o(0.0f, sin(g_SecElapsedSinceProgramStart * 2.0f) + 1.0f, 0.0f);
			debugRenderer->drawTriangle(ToBtVec3(v1 + o), ToBtVec3(v2 + o), ToBtVec3(v3 + o), btVector3(0.9f, 0.3f, 0.2f), 1.0f);
			o = glm::vec3(0.0f, 2.0f, 0.0f);
			debugRenderer->drawTriangle(ToBtVec3(v1 + o), ToBtVec3(v2 + o), ToBtVec3(v3 + o), btVector3(0.9f, 0.3f, 0.2f), 1.0f);
		}

		std::vector<SocketData> const* sockets = pluggablesSystem->GetGameObjectSockets(ID);
		u32 socketCount = sockets != nullptr ? (u32)sockets->size() : 0;

		if (m_VM != nullptr && m_VM->IsExecuting())
		{
			u32 outputCount = glm::min((u32)m_VM->terminalOutputs.size(), socketCount);
			for (u32 i = 0; i < outputCount; ++i)
			{
				pluggablesSystem->SetGameObjectOutputSignal(ID, i, m_VM->terminalOutputs[i].valInt);
			}
			for (u32 i = outputCount; i < socketCount; ++i)
			{
				pluggablesSystem->SetGameObjectOutputSignal(ID, i, -1);
			}
		}
		else
		{
			for (u32 i = 0; i < socketCount; ++i)
			{
				pluggablesSystem->SetGameObjectOutputSignal(ID, i, -1);
			}
		}

		GameObject::Update();
	}

	void Terminal::Render()
	{
		PROFILE_AUTO("Terminal Render");

		if (m_DisplayReloadTimeRemaining != -1.0f)
		{
			m_DisplayReloadTimeRemaining -= g_DeltaTime;
			if (m_DisplayReloadTimeRemaining <= 0.0f)
			{
				m_DisplayReloadTimeRemaining = -1.0f;
			}
		}

		m_CursorBlinkTimer += g_DeltaTime;
		bool bRenderCursor = m_bBeingInteractedWith;
		if (fmod(m_CursorBlinkTimer, m_CursorBlinkRate) > m_CursorBlinkRate / 2.0f)
		{
			bRenderCursor = false;
		}
		if (!g_Window->HasFocus())
		{
			bRenderCursor = false;
		}
		bool bRenderText = !lines.empty();

		if (bRenderCursor || bRenderText)
		{
			BitmapFont* font = g_Renderer->SetFont(SID("editor-02-ws"));

			const real letterSpacing = 0.9f;
			const glm::vec3 right = m_Transform.GetRight();
			const glm::vec3 up = m_Transform.GetUp();
			const glm::vec3 forward = m_Transform.GetForward();

			const real width = 1.4f;
			const real height = 1.65f;
			const glm::vec3 posTL = m_Transform.GetWorldPosition() +
				right * (width / 2.0f) +
				up * height +
				forward * 1.05f;

			const glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
			const real frameBufferScale = glm::max(1.0f / (real)frameBufferSize.x, 1.0f / (real)frameBufferSize.y);

			glm::vec3 pos = posTL;

			const glm::quat rot = m_Transform.GetWorldRotation();
			real charHeight = g_Renderer->GetStringHeight("W", font, false) * frameBufferScale * font->metaData.size * m_LetterScale;
			const real lineHeight = charHeight * m_LineHeight;
			real charWidth = g_Renderer->GetStringWidth("W", font, letterSpacing, false) * frameBufferScale * font->metaData.size * m_LetterScale;
			const real lineNoWidth = 3.0f * charWidth;

			if (bRenderCursor)
			{
				glm::vec3 cursorPos = pos;
				cursorPos += (right * (charWidth * (-cursor.x + 0.5f))) + up * ((cursor.y - scrollY) * -lineHeight);
				g_Renderer->DrawStringWS("|", VEC4_ONE, cursorPos, rot, letterSpacing, m_LetterScale);
			}

			if (bRenderText)
			{
				// TODO: EZ: Define in hot-reloadable config file
				static const glm::vec4 lineNumberColour(0.4f, 0.4f, 0.4f, 1.0f);
				static const glm::vec4 lineNumberColourActive(0.65f, 0.65f, 0.65f, 1.0f);
				static const glm::vec4 textColour(0.85f, 0.81f, 0.80f, 1.0f);
				static const glm::vec4 errorColour(0.65f, 0.12f, 0.13f, 1.0f);
				static const glm::vec4 stepColour(0.24f, 0.65f, 0.23f, 1.0f);
				static const glm::vec4 commentColour(0.35f, 0.35f, 0.35f, 1.0f);

				glm::vec3 firstLinePos = pos;
				i32 lineCount = (i32)lines.size();
				i32 firstRenderedLineIndex = (i32)scrollY;
				i32 lastRenderedLineIndex = firstRenderedLineIndex + glm::min(lineCount - (i32)scrollY, screenRowCount);
				for (i32 lineNumber = firstRenderedLineIndex; lineNumber < lastRenderedLineIndex; ++lineNumber)
				{
					std::string line = lines[lineNumber];
					size_t lineCommentIdx = line.find("//");
					glm::vec4 lineNoCol = (lineNumber == cursor.y ? lineNumberColourActive : lineNumberColour);
					g_Renderer->DrawStringWS(IntToString(lineNumber + 1, 2, ' '), lineNoCol, pos + right * lineNoWidth, rot, letterSpacing, m_LetterScale);
					if (lineCommentIdx == std::string::npos)
					{
						g_Renderer->DrawStringWS(lines[lineNumber], textColour, pos, rot, letterSpacing, m_LetterScale);
					}
					else
					{
						glm::vec3 tPos = pos;
						if (lineCommentIdx > 0)
						{
							std::string codeStr = lines[lineNumber].substr(0, lineCommentIdx);
							g_Renderer->DrawStringWS(codeStr, textColour, pos, rot, letterSpacing, m_LetterScale);
							tPos += -right * (g_Renderer->GetStringWidth(codeStr, font, letterSpacing, false) * frameBufferScale * font->metaData.size * m_LetterScale);
						}

						g_Renderer->DrawStringWS(lines[lineNumber].substr(lineCommentIdx), commentColour, tPos, rot, letterSpacing, m_LetterScale);
					}
					pos.y -= lineHeight;
				}

				if (m_VM != nullptr)
				{
					const std::vector<Diagnostic>& diagnostics = m_VM->GetDiagnosticContainer()->diagnostics;
					if (!diagnostics.empty())
					{
						for (u32 i = 0; i < (u32)diagnostics.size(); ++i)
						{
							const Span& span = diagnostics[i].span;
							pos = firstLinePos;
							pos.y -= lineHeight * diagnostics[i].span.lineNumber;
							g_Renderer->DrawStringWS("!", errorColour, pos + right * (charWidth * 1.f), rot, letterSpacing, m_LetterScale);
							u32 spanLen = glm::min(60u, glm::max(0u, (u32)span.Length()));
							std::string underlineStr = std::string(diagnostics[i].span.columnIndex, ' ') + std::string(spanLen, '_');
							pos.y -= lineHeight * 0.2f;
							g_Renderer->DrawStringWS(underlineStr, errorColour, pos, rot, letterSpacing, m_LetterScale);
						}
					}

					const bool bVMExecuting = m_VM->IsExecuting();
					i32 vmLineNumber = m_VM->CurrentLineNumber();
					if (bVMExecuting && vmLineNumber != -1)
					{
						pos = firstLinePos;
						pos.y -= lineHeight * vmLineNumber;
						u32 spanLen = glm::min(60u, glm::max(0u, (u32)lines[vmLineNumber].size()));
						std::string underlineStr = std::string(spanLen, '_');
						pos.y -= lineHeight * 0.2f;
						g_Renderer->DrawStringWS(underlineStr, stepColour, pos, rot, letterSpacing, m_LetterScale);
					}
				}
			}
		}
	}

	void Terminal::DrawImGuiWindow()
	{
		ImGui::Begin("Terminal");
		{
			ImGui::TextWrapped("Hit F5 to compile and evaluate code.");

			ImGui::Separator();

			if (m_ScriptFileName.empty())
			{
				ImGui::Text("Unsaved");
			}
			else
			{
				ImGui::Text("%s", m_ScriptFileName.c_str());
			}

			if (ImGui::Button(m_bDirtyFlag ? "Save *" : "Save"))
			{
				if (m_ScriptFileName.empty())
				{
					TerminalManager::OpenSavePopup(ID);
				}
				else
				{
					SaveScript();
				}
			}

			if (!m_ScriptFileName.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColour);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColour);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColour);

				ImGui::SameLine();
				if (ImGui::Button("Reload"))
				{
					ReloadScript();
				}

				ImGui::PopStyleColor(3);
			}

			ImGui::SameLine();

			bool bInstallTerminalWatch = g_EngineInstance->GetInstallTerminalWatch();
			if (ImGui::Checkbox("Auto-reload from disk", &bInstallTerminalWatch))
			{
				g_EngineInstance->SetInstallTerminalWatch(bInstallTerminalWatch);

			}

			ImGui::Separator();

			//ImGui::DragFloat("Line height", &m_LineHeight, 0.01f);
			//ImGui::DragFloat("Scale", &m_LetterScale, 0.01f);

			//ImGui::Text("Variables");
			//if (ImGui::BeginChild("", ImVec2(0.0f, 220.0f), true))
			//{
			//	for (i32 i = 0; i < lexer->context->variableCount; ++i)
			//	{
			//		const Context::InstantiatedIdentifier& var = lexer->context->instantiatedIdentifiers[i];
			//		std::string valStr = var.value->ToString();
			//		const char* typeNameCStr = g_TypeNameStrings[(i32)ValueTypeToTypeName(var.value->type)];
			//		ImGui::Text("%s = %s", var.name.c_str(), valStr.c_str());
			//		ImGui::SameLine();
			//		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
			//		ImGui::Text("(%s)", typeNameCStr);
			//		ImGui::PopStyleColor();
			//	}
			//}
			//ImGui::EndChild();

			bool bSuccess = m_VM->diagnosticContainer->diagnostics.empty();

			if (m_DisplayReloadTimeRemaining != -1.0f)
			{
				real alpha = glm::clamp(m_DisplayReloadTimeRemaining / m_ReloadPopupInfoDuration * 2.0f, 0.0f, 1.0f);
				ImVec4 textCol = ImVec4(0.6f, 0.9f, 0.66f, alpha);
				ImGui::PushStyleColor(ImGuiCol_Text, textCol);
				ImGui::Text("Reloaded code");
				ImGui::PopStyleColor();
			}
			else
			{
				ImGui::NewLine();
			}

			if (!bSuccess)
			{

				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
				for (const Diagnostic& diagnostic : m_VM->diagnosticContainer->diagnostics)
				{
					ImGui::TextWrapped("L%d: %s", diagnostic.span.lineNumber + 1, diagnostic.message.c_str());
				}
				ImGui::PopStyleColor();
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1.0f, 0.5f, 1.0f));
				ImGui::Text("Success");
				ImGui::PopStyleColor();
			}

			{
				{
					ImVec2 regionAvail = ImGui::GetContentRegionAvail();
					real runtimeChildWindowHeight = glm::min(regionAvail.y, 200.0f);
					ImGui::BeginChildFrame(ImGui::GetID("registers"), ImVec2(regionAvail.x / 2.0f - 2, runtimeChildWindowHeight));
					{
						ImGui::Text("Registers");
						ImGui::BeginColumns("registers-columns", 2, ImGuiColumnsFlags_NoResize);
						real colWidth = regionAvail.x / 4.0f - 2.0f;
						ImGui::SetColumnWidth(0, colWidth);
						ImGui::SetColumnWidth(1, colWidth);
						for (u32 i = 0; i < (u32)m_VM->registers.size(); ++i)
						{
							const Variant& registerValue = m_VM->registers[i];
							if (registerValue.type != Variant::Type::_NONE)
							{
								std::string regValStr = registerValue.ToString();
								ImGui::Text("r%i = %s", i, regValStr.c_str());
							}
							else
							{
								ImGui::Text("r%i", i);
							}

							if (i == (u32)(m_VM->registers.size() / 2 - 1))
							{
								ImGui::NextColumn();
							}
						}
						ImGui::EndColumns(); // registers-columns
					}
					ImGui::EndChildFrame(); // runtime state

					ImGui::SameLine();

					ImGui::BeginChildFrame(ImGui::GetID("stack"), ImVec2(regionAvail.x / 2.0f - 2, runtimeChildWindowHeight));
					{
						ImGui::Text("Stack");
						// Copy of stack that we can pop elements off of to see contents
						std::stack<Variant> stackCopy = m_VM->stack;
						while (!stackCopy.empty())
						{
							const Variant& value = stackCopy.top();
							std::string valStr = value.ToString();
							ImGui::Text("%s", valStr.c_str());
							stackCopy.pop();
						}
					}
					ImGui::EndChildFrame(); // stack
				}

				for (u32 i = 0; i < Terminal::MAX_OUTPUT_COUNT; ++i)
				{
					const Variant& terminalVal = m_VM->terminalOutputs[i];
					if (terminalVal.type != Variant::Type::_NONE)
					{
						std::string terminalValStr = terminalVal.ToString();
						ImGui::Text("out_%i = %s", i, terminalValStr.c_str());
					}
					else
					{
						ImGui::NewLine();
					}
				}

				ImGui::Text("zf: %d sf: %d", m_VM->ZeroFlagSet() ? 1 : 0, m_VM->SignFlagSet() ? 1 : 0);

				ImGui::Text("Inst index: %d", m_VM->m_RunningState.instructionIdx);
				ImGui::SameLine();
				ImGui::Text("%s", (m_VM->m_RunningState.terminated ? "terminated" : ""));

				ImVec2 regionAvail = ImGui::GetContentRegionAvail();
				if (ImGui::BeginChild("source representations", ImVec2(regionAvail.x - 2, glm::max(regionAvail.y, 50.0f)), true))
				{
					if (!m_VM->instructionStr.empty())
					{
						ImGui::Text("Instructions");

						if (!m_VM->IsExecuting())
						{
							ImGui::SameLine();
							if (ImGui::Button("Start (F10)"))
							{
								m_VM->Execute(true);
							}
						}
						else
						{
							ImGui::SameLine();
							if (ImGui::Button("Step (F10)"))
							{
								m_VM->Execute(true);
							}
							ImGui::SameLine();
							if (ImGui::Button("Reset"))
							{
								m_VM->ClearRuntimeState();
							}
						}

						const ImVec2 preCursorPos = ImGui::GetCursorPos();
						ImGui::Text("%s", m_VM->instructionStr.c_str());
						const ImVec2 postCursorPos = ImGui::GetCursorPos();

						if (m_VM->IsExecuting())
						{
							i32 instIdx = m_VM->InstructionIndex();
							std::string underlineStr = instIdx == 0 ? "" : std::string(instIdx, '\n');
							underlineStr += "____________________";
							ImGui::SetCursorPos(ImVec2(preCursorPos.x, preCursorPos.y + 1.0f));
							ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.25f, 0.7f, 1.0f));
							ImGui::Text("%s", underlineStr.c_str());
							ImGui::PopStyleColor();

							ImGui::SetCursorPos(postCursorPos);
						}
					}

					if (!m_VM->unpatchedInstructionStr.empty())
					{
						ImGui::Separator();
						ImGui::Text("Instructions (unpatched)");
						const ImVec2 preCursorPos = ImGui::GetCursorPos();
						ImGui::Text("%s", m_VM->unpatchedInstructionStr.c_str());
						const ImVec2 postCursorPos = ImGui::GetCursorPos();

						if (m_VM->IsExecuting())
						{
							i32 strLineNum = 0;
							bool bFound = false;
							i32 instIdx = m_VM->InstructionIndex();
							for (i32 i = 0; i < (i32)m_VM->state->instructionBlocks.size(); ++i)
							{
								i32 startOffset = (i32)m_VM->state->instructionBlocks[i].startOffset;
								i32 lineCountInBlock = (i32)m_VM->state->instructionBlocks[i].instructions.size();
								if (instIdx >= startOffset &&
									instIdx < (startOffset + lineCountInBlock))
								{
									bFound = true;
									strLineNum += (instIdx - startOffset);
									break;
								}
								strLineNum += lineCountInBlock + 2; // Two additional lines for "label: {\n" & "\n}"
							}

							if (bFound)
							{
								strLineNum += 1;
								std::string underlineStr = strLineNum == 0 ? "" : std::string(strLineNum, '\n');
								underlineStr += "____________________";
								ImGui::SetCursorPos(ImVec2(preCursorPos.x, preCursorPos.y + 1.0f));
								ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.25f, 0.7f, 1.0f));
								ImGui::Text("%s", underlineStr.c_str());
								ImGui::PopStyleColor();

								ImGui::SetCursorPos(postCursorPos);
							}
						}
					}

					if (!m_VM->irStr.empty())
					{
						ImGui::Separator();
						ImGui::Text("IR");
						ImGui::Text("%s", m_VM->irStr.c_str());
					}

					if (!m_VM->astStr.empty())
					{
						ImGui::Separator();
						ImGui::Text("AST");
						ImGui::Text("%s", m_VM->astStr.c_str());
					}
				}
				ImGui::EndChild();
			}
		}
		ImGui::End();

		const char* externalScriptChangeStr = "Script changed externally";
		if (m_bShowExternalChangePopup)
		{
			m_bShowExternalChangePopup = false;
			ImGui::OpenPopup(externalScriptChangeStr);
		}

		if (ImGui::BeginPopupModal(externalScriptChangeStr, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Script %s was modified on disk, do you want to reload?", m_ScriptFileName.c_str());
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.2f, 1.0f));
			ImGui::Text("Warning: you will lose any changes you've made!");
			ImGui::PopStyleColor();

			if (ImGui::Button("Ignore external change"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button("Reload"))
			{
				m_bDirtyFlag = false;
				OnScriptChanged();

				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	bool Terminal::IsInteractable(Player* player)
	{
		Transform* playerTransform = player->GetTransform();
		glm::vec3 dPos = m_Transform.GetWorldPosition() - playerTransform->GetWorldPosition();
		real FoP = glm::dot(m_Transform.GetForward(), glm::normalize(dPos));
		real FoF = glm::dot(m_Transform.GetForward(), playerTransform->GetForward());
		// Ensure player is looking our direction and in front of us
		if (FoF < -0.15f && FoP < -0.35f)
		{
			return true;
		}

		return false;
	}

	void Terminal::SetBeingInteractedWith(Player* player)
	{
		m_bBeingInteractedWith = player != nullptr;
	}

	void Terminal::OnScriptChanged()
	{
		if (m_bDirtyFlag)
		{
			m_bShowExternalChangePopup = true;
			return;
		}

		cursor.x = cursor.y = cursorMaxX = 0;
		scrollY = 0.0f;
		m_CursorBlinkTimer = 0.0f;
		GetSystem<TerminalManager>(SystemType::TERMINAL_MANAGER)->LoadScript(m_ScriptFileName, lines);
		ParseCode();
		m_DisplayReloadTimeRemaining = m_ReloadPopupInfoDuration;
	}

	void Terminal::SetCamera(TerminalCamera* camera)
	{
		m_Camera = camera;
	}

	void Terminal::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject terminalObj = parentObject.GetObject("terminal");

		m_ScriptFileName = terminalObj.GetString("script file path");

		if (!m_ScriptFileName.empty())
		{
			if (!GetSystem<TerminalManager>(SystemType::TERMINAL_MANAGER)->LoadScript(m_ScriptFileName, lines))
			{
				PrintWarn("Failed to load terminal script from \"%s\"\n", m_ScriptFileName.c_str());
				lines.emplace_back("// Failed to load from script");
			}
		}

		if (lines.empty())
		{
			lines.emplace_back("");
		}

		MoveCursorToStart();
	}

	void Terminal::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		JSONObject terminalObj = {};

		SaveScript();

		SerializeField(terminalObj, bSerializePrefabData, "script file path", &m_ScriptFileName, ValueType::STRING);

		parentObject.fields.emplace_back("terminal", JSONValue(terminalObj));
	}

	void Terminal::TypeChar(char c)
	{
		m_CursorBlinkTimer = 0.0f;

		if (lines.empty())
		{
			lines.emplace_back("");
		}

		std::string& curLine = lines[cursor.y];

		if (c == '\n')
		{
			// Move remainder of line onto newline
			std::string remainderOfLine = curLine.substr(cursor.x);
			curLine.erase(curLine.begin() + cursor.x, curLine.end());
			lines.insert(lines.begin() + cursor.y + 1, remainderOfLine);
			m_CursorBlinkTimer = 0.0f;
			cursor.y += 1;
			cursor.x = 0;
			ClampCursorX();
			ScrollCursorToView();
			cursorMaxX = cursor.x;
		}
		else
		{
			if (cursor.x == (i32)curLine.size())
			{
				curLine.push_back(c);
			}
			else
			{
				curLine.insert(curLine.begin() + cursor.x, c);
			}
			MoveCursorRight();
			cursorMaxX = cursor.x;
		}
		m_bDirtyFlag = true;
		ParseCode();
	}

	void Terminal::DeleteChar(bool bDeleteUpToNextBreak /* = false */)
	{
		m_CursorBlinkTimer = 0.0f;
		if (lines.empty() || (cursor.x == 0 && cursor.y == 0))
		{
			return;
		}

		std::string& curLine = lines[cursor.y];

		if (cursor.x == 0)
		{
			if (cursor.y > 0)
			{
				// Move current line up to previous
				const i32 prevLinePLen = (i32)lines[cursor.y - 1].size();
				lines[cursor.y - 1].append(curLine);
				// TODO: Enforce screen width
				lines.erase(lines.begin() + cursor.y);
				cursor.x = prevLinePLen;
				MoveCursorUp();
				cursorMaxX = cursor.x;
			}
		}
		else
		{
			if (bDeleteUpToNextBreak)
			{
				i32 newX = glm::max(GetIdxOfPrevBreak(cursor.y, cursor.x), 0);
				curLine.erase(curLine.begin() + newX, curLine.begin() + cursor.x);
				cursor.x = newX;
			}
			else
			{
				curLine.erase(curLine.begin() + cursor.x - 1);
				cursor.x -= 1;
			}
			cursorMaxX = cursor.x;
			ClampCursorX();
		}
		m_bDirtyFlag = true;
		ParseCode();
	}

	void Terminal::DeleteCharInFront(bool bDeleteUpToNextBreak /* = false */)
	{
		m_CursorBlinkTimer = 0.0f;
		if (lines.empty())
		{
			return;
		}
		std::string& curLine = lines[cursor.y];
		if (cursor.x == (i32)curLine.size() && cursor.y == (i32)lines.size() - 1)
		{
			return;
		}

		if (cursor.x == (i32)curLine.size())
		{
			if (cursor.y < (i32)lines.size() - 1)
			{
				// Move next line up into current
				curLine.append(lines[cursor.y + 1]);
				lines.erase(lines.begin() + cursor.y + 1);
				// TODO: Enforce screen width
			}
		}
		else
		{

			if (bDeleteUpToNextBreak)
			{
				i32 newX = GetIdxOfNextBreak(cursor.y, cursor.x);
				curLine.erase(curLine.begin() + cursor.x, curLine.begin() + newX);
			}
			else
			{
				curLine.erase(curLine.begin() + cursor.x);
			}
		}
		m_bDirtyFlag = true;
		ParseCode();
	}

	void Terminal::MoveCursorToStart()
	{
		m_CursorBlinkTimer = 0.0f;
		cursor.y = 0;
		cursor.x = 0;
		cursorMaxX = cursor.x;
		scrollY = 0.0f;
	}

	void Terminal::MoveCursorToStartOfLine()
	{
		m_CursorBlinkTimer = 0.0f;
		cursor.x = 0;
		cursorMaxX = cursor.x;
	}

	void Terminal::MoveCursorToEnd()
	{
		m_CursorBlinkTimer = 0.0f;
		if (!lines.empty())
		{
			cursor.y = (i32)lines.size() - 1;
			cursor.x = (i32)lines[cursor.y].size();
		}
		else
		{
			cursor.x = cursor.y = 0;
		}
		cursorMaxX = cursor.x;

		i32 lineCount = (i32)lines.size();
		scrollY = (real)(lineCount - glm::min(lineCount, (i32)screenRowCount));
	}

	void Terminal::MoveCursorToEndOfLine()
	{
		m_CursorBlinkTimer = 0.0f;
		cursor.x = (i32)lines[cursor.y].size();
		cursorMaxX = cursor.x;
	}

	void Terminal::MoveCursorLeft(bool bSkipToNextBreak /* = false */)
	{
		m_CursorBlinkTimer = 0.0f;
		if (lines.empty() || (cursor.x == 0 && cursor.y == 0))
		{
			return;
		}

		if (bSkipToNextBreak)
		{
			if (cursor.x == 0 && cursor.y > 0)
			{
				cursor.x = INT_MAX;
				MoveCursorUp();
			}
			else
			{
				cursor.x = glm::max(GetIdxOfPrevBreak(cursor.y, cursor.x), 0);
			}
		}
		else
		{
			cursor.x -= 1;
			if (cursor.x < 0 && cursor.y > 0)
			{
				cursor.x = INT_MAX;
				MoveCursorUp();
			}
		}
		cursorMaxX = cursor.x;
	}

	void Terminal::MoveCursorRight(bool bSkipToNextBreak /* = false */)
	{
		m_CursorBlinkTimer = 0.0f;
		if (lines.empty())
		{
			return;
		}
		std::string& curLine = lines[cursor.y];
		if (cursor.x == (i32)curLine.size() && cursor.y == (i32)lines.size() - 1)
		{
			return;
		}

		if (bSkipToNextBreak)
		{
			if (cursor.x == (i32)curLine.size() && cursor.y < (i32)lines.size() - 1)
			{
				cursor.x = 0;
				cursorMaxX = cursor.x;
				MoveCursorDown();
			}
			else
			{
				cursor.x = GetIdxOfNextBreak(cursor.y, cursor.x);
			}
		}
		else
		{
			cursor.x += 1;
			if (cursor.x > (i32)curLine.size() && cursor.y < (i32)lines.size() - 1)
			{
				cursor.x = 0;
				cursorMaxX = 0;
				MoveCursorDown();
			}
		}
		cursorMaxX = cursor.x;
	}

	void Terminal::MoveCursorUp()
	{
		m_CursorBlinkTimer = 0.0f;
		if (cursor.y == 0)
		{
			cursor.x = 0;
			cursorMaxX = cursor.x;
		}
		else
		{
			cursor.y -= 1;
			cursor.x = glm::min(glm::max(cursor.x, cursorMaxX), (i32)lines[cursor.y].size());
			ScrollCursorToView();
		}
	}

	void Terminal::MoveCursorDown()
	{
		m_CursorBlinkTimer = 0.0f;
		if (cursor.y == (i32)lines.size() - 1)
		{
			cursor.x = (i32)lines[cursor.y].size();
			cursorMaxX = cursor.x;
		}
		else
		{
			cursor.y += 1;
			cursor.x = glm::min(glm::max(cursor.x, cursorMaxX), (i32)lines[cursor.y].size());
			ScrollCursorToView();
		}
	}

	void Terminal::ScrollCursorToView()
	{
		i32 cursorYInScreen = (i32)(cursor.y - scrollY);
		if (cursorYInScreen < 0)
		{
			// Scrolled up past top of screen
			scrollY = (real)cursor.y;

			// We've deleted the bottom-most line, so only one will be visible now,
			// Scroll up a (nearly) full page length
			if ((i32)scrollY == ((i32)lines.size() - 1))
			{
				scrollY = glm::max(0.0f, scrollY - (real)(screenRowCount - 1));
			}
		}
		else if (cursorYInScreen >= screenRowCount)
		{
			// Scrolled down past bottom of screen
			scrollY = (real)(cursor.y - screenRowCount + 1);
		}
	}

	void Terminal::PageUp()
	{
		if (cursor.y < screenRowCount)
		{
			MoveCursorToStart();
		}
		else
		{
			cursor.y -= screenRowCount;
			cursor.x = glm::min(glm::max(cursor.x, cursorMaxX), (i32)lines[cursor.y].size());
			scrollY = glm::max(0.0f, scrollY - (real)screenRowCount);
		}
	}

	void Terminal::PageDown()
	{
		i32 lineCount = (i32)lines.size();
		if (cursor.y >= (lineCount - screenRowCount - 1))
		{
			MoveCursorToEnd();
		}
		else
		{
			cursor.y += screenRowCount;
			cursor.x = glm::min(glm::max(cursor.x, cursorMaxX), (i32)lines[cursor.y].size());
			scrollY = glm::min((real)(lineCount - screenRowCount - 1), scrollY + (real)screenRowCount);
		}
	}

	void Terminal::ClampCursorX()
	{
		cursor.x = glm::clamp(cursor.x, 0, (i32)lines[cursor.y].size());
	}

	bool Terminal::SkipOverChar(char c)
	{
		if (isalpha(c) || isdigit(c))
		{
			return true;
		}

		return false;
	}

	i32 Terminal::GetIdxOfNextBreak(i32 y, i32 startX)
	{
		std::string& line = lines[y];

		i32 result = startX;
		while (result < (i32)line.size() && !SkipOverChar(line.at(result)))
		{
			result += 1;
		}

		while (result < (i32)line.size() && SkipOverChar(line.at(result)))
		{
			result += 1;
		}

		return result;
	}

	i32 Terminal::GetIdxOfPrevBreak(i32 y, i32 startX)
	{
		std::string& line = lines[y];

		i32 result = startX;
		while (result > 0 && !SkipOverChar(line.at(result - 1)))
		{
			result -= 1;
		}

		while (result > 0 && SkipOverChar(line.at(result - 1)))
		{
			result -= 1;
		}

		return result - 1;
	}

	void Terminal::ParseCode()
	{
		CHECK_NE(m_VM, nullptr);

		std::string str;
		for (const std::string& line : lines)
		{
			str.append(line);
			str.push_back('\n');
		}

		m_VM->GenerateFromSource(str.c_str());
	}

	void Terminal::EvaluateCode()
	{
		if (m_VM->IsCompiled())
		{
			m_VM->ClearRuntimeState();
			m_VM->Execute();
		}
	}

	bool Terminal::SaveScript()
	{
		if (!m_ScriptFileName.empty())
		{
			if (GetSystem<TerminalManager>(SystemType::TERMINAL_MANAGER)->SaveScript(m_ScriptFileName, lines))
			{
				m_bDirtyFlag = false;
				return true;
			}
			else
			{
				PrintError("Failed to save terminal script to %s\n", m_ScriptFileName.c_str());
			}
		}
		else
		{
			PrintError("Failed to save terminal script, file name not set\n");
		}

		return false;
	}

	void Terminal::ReloadScript()
	{
		if (!m_ScriptFileName.empty())
		{
			OnScriptChanged();
		}
		else
		{
			PrintError("Failed to reload terminal script, file name not set\n");
		}
	}

	EventReply Terminal::OnKeyEvent(KeyCode keyCode, KeyAction action, i32 modifiers)
	{
		if (m_Camera != nullptr)
		{
			if (m_Camera->IsTransitioningOut())
			{
				// Don't respond to input while camera is transitioning out
				return EventReply::UNCONSUMED;
			}

			if (action == KeyAction::KEY_PRESS || action == KeyAction::KEY_REPEAT)
			{
				const bool bCapsLock = (modifiers & (i32)InputModifier::CAPS_LOCK) > 0;
				const bool bShiftDown = (modifiers & (i32)InputModifier::SHIFT) > 0;
				const bool bCtrlDown = (modifiers & (i32)InputModifier::CONTROL) > 0;
				if (keyCode == KeyCode::KEY_PAGE_UP)
				{
					PageUp();
					return EventReply::CONSUMED;
				}
				else if (keyCode == KeyCode::KEY_PAGE_DOWN)
				{
					PageDown();
					return EventReply::CONSUMED;
				}
				else if (keyCode == KeyCode::KEY_F7)
				{
					ParseCode();
					return EventReply::CONSUMED;
				}
				else if (keyCode == KeyCode::KEY_F5)
				{
					ParseCode();
					EvaluateCode();
					return EventReply::CONSUMED;
				}
				else if (keyCode == KeyCode::KEY_F10)
				{
					m_VM->Execute(true);
					return EventReply::CONSUMED;
				}
				else if (keyCode == KeyCode::KEY_ESCAPE)
				{
					m_Camera->TransitionOut();
					return EventReply::CONSUMED;
				}
				else if (keyCode == KeyCode::KEY_SLASH)
				{
					if (bCtrlDown) // Comment line
					{
						i32 pCursorPosX = cursor.x;
						cursor.x = 0;
						std::string strippedLine = TrimLeadingWhitespace(lines[cursor.y]);
						i32 leadingWhitespaceCount = (i32)(strippedLine.size() - lines[cursor.y].size());
						if (strippedLine.size() < 2)
						{
							TypeChar('/');
							TypeChar('/');
							cursor.x = pCursorPosX + 2;
						}
						else
						{
							cursor.x = leadingWhitespaceCount;
							if (strippedLine[0] == '/' && strippedLine[1] == '/')
							{
								DeleteCharInFront(false);
								DeleteCharInFront(false);
								cursor.x = pCursorPosX - 2;
							}
							else
							{

								TypeChar('/');
								TypeChar('/');
								cursor.x = pCursorPosX + 2;
							}
						}
					}
					else if (bShiftDown)
					{
						TypeChar(InputManager::GetShiftModifiedKeyCode('/'));
					}
					else
					{
						TypeChar('/');
					}
					return EventReply::CONSUMED;
				}

				// Most letters, numbers, & characters
				if ((i32)keyCode >= (i32)KeyCode::KEY_APOSTROPHE && (i32)keyCode <= (i32)KeyCode::KEY_RIGHT_BRACKET)
				{
					char c = KeyCodeStrings[(i32)keyCode][0];
					if (isalpha(c))
					{
						if (bCtrlDown)
						{
							return EventReply::UNCONSUMED;
						}

						if (!bShiftDown && !bCapsLock)
						{
							c = (char)tolower(c);
						}
					}
					else
					{
						if (bShiftDown)
						{
							c = InputManager::GetShiftModifiedKeyCode(c);
						}
					}
					TypeChar(c);
					return EventReply::CONSUMED;
				}
				if (keyCode >= KeyCode::KEY_KP_0 && keyCode <= KeyCode::KEY_KP_9)
				{
					if (bShiftDown)
					{
						if (keyCode == KeyCode::KEY_KP_1)
						{
							// End
							if (bCtrlDown)
							{
								MoveCursorToEnd();
							}
							else
							{
								MoveCursorToEndOfLine();
							}
						}
						if (keyCode == KeyCode::KEY_KP_3)
						{
							// TODO: Pg Down
						}
						if (keyCode == KeyCode::KEY_KP_7)
						{
							// Home
							if (bCtrlDown)
							{
								MoveCursorToStart();
							}
							else
							{
								MoveCursorToStartOfLine();
							}
						}
						if (keyCode == KeyCode::KEY_KP_9)
						{
							// TODO: Pg Up
						}
						if (keyCode == KeyCode::KEY_KP_2)
						{
							MoveCursorDown();
						}
						if (keyCode == KeyCode::KEY_KP_4)
						{
							MoveCursorLeft();
						}
						if (keyCode == KeyCode::KEY_KP_6)
						{
							MoveCursorRight();
						}
						if (keyCode == KeyCode::KEY_KP_8)
						{
							MoveCursorUp();
						}

						return EventReply::CONSUMED;
					}
					else
					{
						if (!bCtrlDown)
						{
							char c = (char)('0' + ((i32)keyCode - (i32)KeyCode::KEY_KP_0));
							TypeChar(c);
							return EventReply::CONSUMED;
						}
					}
					return EventReply::UNCONSUMED;
				}
				if (keyCode == KeyCode::KEY_KP_MULTIPLY)
				{
					TypeChar('*');
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_KP_DIVIDE)
				{
					TypeChar('/');
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_KP_ADD)
				{
					TypeChar('+');
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_KP_SUBTRACT)
				{
					TypeChar('-');
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_KP_DECIMAL)
				{
					if (bShiftDown)
					{
						DeleteCharInFront(bCtrlDown);
					}
					else
					{
						TypeChar('.');
					}
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_SPACE)
				{
					TypeChar(' ');
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_TAB)
				{
					TypeChar(' ');
					TypeChar(' ');
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_ENTER ||
					keyCode == KeyCode::KEY_KP_ENTER)
				{
					TypeChar('\n');
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_BACKSPACE)
				{
					DeleteChar(bCtrlDown);
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_DELETE)
				{
					DeleteCharInFront(bCtrlDown);
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_HOME)
				{
					if (bCtrlDown)
					{
						MoveCursorToStart();
					}
					else
					{
						MoveCursorToStartOfLine();
					}
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_END)
				{
					if (bCtrlDown)
					{
						MoveCursorToEnd();
					}
					else
					{
						MoveCursorToEndOfLine();
					}
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_LEFT)
				{
					MoveCursorLeft(bCtrlDown);
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_RIGHT)
				{
					MoveCursorRight(bCtrlDown);
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_UP)
				{
					MoveCursorUp();
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_DOWN)
				{
					MoveCursorDown();
					return EventReply::CONSUMED;
				}
			}
		}

		return EventReply::UNCONSUMED;
	}

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
			renderObjectCreateInfo.shadowCullSphere = glm::vec4((boundsMin + boundsMax) * 0.5f, glm::length(boundsMax - boundsMin) * 0.5f);

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

		u32 uploadCount = 0;
		while (!m_ChunkCommands.empty())
		{
			// Commands which must be applied in the same frame (so neighboring chunks always match)
			size_t groupEnd = 1;
			while (groupEnd < m_ChunkCommands.size() && m_ChunkCommands[groupEnd].bAtomicWithPrevious)
			{
				++groupEnd;
			}

			if (groupEnd == 1)
			{
				terrain::ChunkCommand& command = m_ChunkCommands.front();
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
				m_ChunkCommands.erase(m_ChunkCommands.begin());
				continue;
			}

			// Upload the group's new chunks hidden (over as many frames as needed), then swap them in all at once
			bool bAllStaged = true;
			for (size_t i = 0; i < groupEnd; ++i)
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

			for (size_t i = 0; i < groupEnd; ++i)
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
			m_ChunkCommands.erase(m_ChunkCommands.begin(), m_ChunkCommands.begin() + groupEnd);
		}

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
				camera->position = glm::vec3(worldX, height + 40.0f, worldZ);
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

	SpringObject::SpringObject(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, SpringObjectSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
	}

	PropertyCollection* SpringObject::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("spring");

		collection->childCollection->RegisterProperty("end point", FIELD_OFFSET_AND_TYPE(SpringObject, m_TargetPos))
			.VersionAdded(6)
			.Precision(2)
			.DefaultValue(VEC3_ZERO);

		return collection;
	}

	GameObject* SpringObject::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);
		SpringObject* newObject = new SpringObject(newObjectName, newGameObjectID);

		// Ensure mesh & rigid body isn't copied
		copyFlags = (CopyFlags)(copyFlags & ~CopyFlags::MESH);
		copyFlags = (CopyFlags)(copyFlags & ~CopyFlags::RIGIDBODY);
		CopyGenericFields(newObject, parent, copyFlags);

		return newObject;
	}

	void SpringObject::Initialize()
	{
		if (s_SpringMatID == InvalidMaterialID)
		{
			CreateMaterials();
		}

		{
			LoadedMesh* extendedMesh = Mesh::LoadMesh(s_ExtendedMeshFilePath);
			cgltf_mesh* extendedMeshCGLTF = &(extendedMesh->data->meshes[0]);
			m_ExtendedMesh = MeshComponent::LoadFromCGLTF(nullptr, &extendedMeshCGLTF->primitives[0], s_SpringMatID, nullptr, false);

			LoadedMesh* contractedMesh = Mesh::LoadMesh(s_ContractedMeshFilePath);
			cgltf_mesh* contractedMeshCGLTF = &(contractedMesh->data->meshes[0]);
			m_ContractedMesh = MeshComponent::LoadFromCGLTF(nullptr, &contractedMeshCGLTF->primitives[0], s_SpringMatID, nullptr, false);
		}

		if (m_bSimulateTarget)
		{
			m_OriginTransform = new GameObject("Spring origin", BaseObjectSID);
			m_OriginTransform->SetSerializable(false);
			m_OriginTransform->SetVisibleInSceneExplorer(false);
			AddChild(m_OriginTransform);
			m_OriginTransform->Initialize();
			m_OriginTransform->PostInitialize();
		}

		VertexBufferData* extendedVertBufferData = m_ExtendedMesh->GetVertexBufferData();
		VertexBufferData* contractedVertBufferData = m_ContractedMesh->GetVertexBufferData();
		u32 vertCount = extendedVertBufferData->VertexCount;

		m_DynamicVertexBufferCreateInfo = {};
		m_DynamicVertexBufferCreateInfo.attributes = extendedVertBufferData->Attributes;

		m_Mesh = new Mesh(m_OriginTransform);
		m_Indices = m_ExtendedMesh->GetIndexBufferCopy();
		Mesh::CreateInfo meshCreateInfo = {};
		meshCreateInfo.bDynamic = true;
		meshCreateInfo.vertexBufferCreateInfo = &m_DynamicVertexBufferCreateInfo;
		meshCreateInfo.indices = m_Indices;
		meshCreateInfo.materialIDs = { s_SpringMatID };
		meshCreateInfo.initialMaxVertexCount = vertCount;
		m_Mesh->LoadFromMemory(meshCreateInfo);

		m_DynamicVertexBufferCreateInfo.positions_3D.resize(vertCount);
		m_DynamicVertexBufferCreateInfo.texCoords_UV.resize(vertCount);
		m_DynamicVertexBufferCreateInfo.colours_R32G32B32A32.resize(vertCount);
		m_DynamicVertexBufferCreateInfo.normals.resize(vertCount);
		m_DynamicVertexBufferCreateInfo.tangents.resize(vertCount);

		extendedPositions.resize(vertCount);
		extendedNormals.resize(vertCount);
		extendedTangents.resize(vertCount);

		contractedPositions.resize(vertCount);
		contractedNormals.resize(vertCount);
		contractedTangents.resize(vertCount);

		extendedVertBufferData->CopyInto((real*)extendedPositions.data(), (VertexAttributes)VertexAttribute::POSITION);
		extendedVertBufferData->CopyInto((real*)extendedNormals.data(), (VertexAttributes)VertexAttribute::NORMAL);
		extendedVertBufferData->CopyInto((real*)extendedTangents.data(), (VertexAttributes)VertexAttribute::TANGENT);

		contractedVertBufferData->CopyInto((real*)contractedPositions.data(), (VertexAttributes)VertexAttribute::POSITION);
		contractedVertBufferData->CopyInto((real*)contractedNormals.data(), (VertexAttributes)VertexAttribute::NORMAL);
		contractedVertBufferData->CopyInto((real*)contractedTangents.data(), (VertexAttributes)VertexAttribute::TANGENT);

		// Unchanging attributes, just copy extended data in once
		extendedVertBufferData->CopyInto((real*)m_DynamicVertexBufferCreateInfo.texCoords_UV.data(), (VertexAttributes)VertexAttribute::UV);
		extendedVertBufferData->CopyInto((real*)m_DynamicVertexBufferCreateInfo.colours_R32G32B32A32.data(), (VertexAttributes)VertexAttribute::COLOUR_R32G32B32A32_SFLOAT);

		glm::vec3 initialRootPos = m_Transform.GetWorldPosition();
		glm::vec3 initialTargetPos;
		if (m_TargetPos == VEC3_ZERO)
		{
			initialTargetPos = initialRootPos + m_Transform.GetUp() * m_MinLength;
		}
		else
		{
			initialTargetPos = m_TargetPos;
		}

		//m_Target->GetTransform()->SetWorldPosition(initialTargetPos);

		if (m_bSimulateTarget)
		{
			m_SpringSim = new SoftBody("Spring sim");
			m_SpringSim->points = std::vector<Point*>{ new Point(initialRootPos, VEC3_ZERO, 0.0f), new Point(initialTargetPos, VEC3_ZERO, 1.0f / 20.0f) };
			m_SpringSim->SetSerializable(false);
			m_SpringSim->SetVisibleInSceneExplorer(false);
			m_SpringSim->SetOwnedInternally(true);
			real stiffness = 0.02f;
			m_SpringSim->SetStiffness(stiffness);
			m_SpringSim->SetDamping(0.999f);
			m_SpringSim->AddUniqueDistanceConstraint(0, 1, 0, stiffness);
			m_SpringSim->Initialize();
			m_SpringSim->PostInitialize();

			m_Bobber = new GameObject("Spring bobber", BaseObjectSID);
			m_Bobber->SetSerializable(false);
			m_Bobber->SetVisibleInSceneExplorer(false);
			m_Bobber->SetOwnedInternally(true);
			Mesh* bobberMesh = m_Bobber->SetMesh(new Mesh(m_Bobber));
			bobberMesh->LoadFromFile(MESH_DIRECTORY "sphere.glb", s_BobberMatID);
			m_Bobber->Initialize();
			m_Bobber->PostInitialize();
		}
		else
		{
			m_Target = new GameObject("Spring target", BaseObjectSID);
			m_Target->SetSerializable(false);
			m_Target->GetTransform()->SetWorldPosition(initialTargetPos);
			AddSibling(m_Target);
			m_Target->Initialize();
			m_Target->PostInitialize();
		}

		GameObject::Initialize();
	}

	void SpringObject::Update()
	{
		PROFILE_AUTO("SpringObject Update");

		Transform* originTransform = m_OriginTransform->GetTransform();
		glm::vec3 rootPos = originTransform->GetWorldPosition();
		glm::vec3 targetPos;
		if (m_bSimulateTarget)
		{
			m_SpringSim->GetTransform()->SetWorldPosition(rootPos);
			targetPos = m_SpringSim->points[1]->pos;
			m_Bobber->GetTransform()->SetWorldPosition(targetPos);
		}
		else
		{
			targetPos = m_Target->GetTransform()->GetWorldPosition();
		}

		real delta = (glm::dot(targetPos - rootPos, originTransform->GetUp()) - m_MinLength) / (m_MaxLength - m_MinLength);
		real t = glm::clamp(delta, 0.0f, 1.0f);

		for (u32 i = 0; i < (u32)m_DynamicVertexBufferCreateInfo.positions_3D.size(); ++i)
		{
			m_DynamicVertexBufferCreateInfo.positions_3D[i] = Lerp(contractedPositions[i], extendedPositions[i], t);
			m_DynamicVertexBufferCreateInfo.normals[i] = Lerp(contractedNormals[i], extendedNormals[i], t);
			m_DynamicVertexBufferCreateInfo.tangents[i] = Lerp(contractedTangents[i], extendedTangents[i], t);
		}
		m_Mesh->GetSubMesh(0)->UpdateDynamicVertexData(m_DynamicVertexBufferCreateInfo, m_Indices);

		glm::vec3 lookDir = rootPos - targetPos;
		real lookLen = glm::length(lookDir);
		glm::vec3 lookDirNorm = lookDir / lookLen;
		if (lookLen > 0.0001f)
		{
			static glm::quat offsetQuat = glm::quat(glm::vec3(PI_DIV_TWO, PI, 0.0f));
			//g_Renderer->GetDebugRenderer()->drawLine(ToBtVec3(rootPos), ToBtVec3(rootPos + lookDirNorm), btVector3(1.0f, 0.0f, 0.0f), btVector3(1.0f, 0.0f, 0.0f));
			glm::vec3 up = glm::abs(glm::angle(lookDirNorm, VEC3_UP)) < 0.01f ? VEC3_FORWARD : VEC3_UP;
			glm::quat targetRot = glm::quatLookAt(lookDirNorm, up) * offsetQuat;
			if (!glm::any(glm::isnan(targetRot)))
			{
				glm::quat finalRot = glm::slerp(originTransform->GetWorldRotation(), targetRot, g_DeltaTime * 100.0f);
				originTransform->SetWorldRotation(finalRot);
			}
		}

		GameObject::Update();

		g_Renderer->GetDebugRenderer()->DrawAxes(ToBtVec3(originTransform->GetWorldPosition()), ToBtQuaternion(originTransform->GetWorldRotation()), 2.0f);

		m_SpringSim->Update();
		m_Bobber->Update();
	}

	void SpringObject::Destroy(bool bDetachFromParent /* = true */)
	{
		m_ExtendedMesh->Destroy();
		delete m_ExtendedMesh;
		m_ExtendedMesh = nullptr;

		m_ContractedMesh->Destroy();
		delete m_ContractedMesh;
		m_ContractedMesh = nullptr;

		m_SpringSim->Destroy();
		delete m_SpringSim;
		m_SpringSim = nullptr;

		m_Bobber->Destroy();
		delete m_Bobber;
		m_Bobber = nullptr;

		GameObject::Destroy(bDetachFromParent);
	}

	void SpringObject::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		ImGui::Spacing();
		ImGui::Text("Spring");
		m_SpringSim->DrawImGuiObjects(bDrawingEditorObjects);

		ImGui::Spacing();
		ImGui::Text("Bobber");
		m_Bobber->DrawImGuiObjects(bDrawingEditorObjects);

		ImGui::Spacing();
		ImGui::Text("Origin");
		m_Bobber->DrawImGuiObjects(bDrawingEditorObjects);

	}

	void SpringObject::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject springObj;
		if (parentObject.TryGetObject("spring", springObj))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(springObj, collection->childCollection);
		}
	}

	void SpringObject::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject springObj = {};

		m_TargetPos = m_SpringSim->points[1]->pos;
		SerializeRegisteredProperties(springObj, collection->childCollection, bSerializePrefabData);
		if (!springObj.fields.empty())
		{
			parentObject.fields.emplace_back("spring", JSONValue(springObj));
		}
	}

	void SpringObject::CreateMaterials()
	{
		s_SpringMatID = g_Renderer->GetMaterialID("Spring");
		if (s_SpringMatID == InvalidMaterialID)
		{
			MaterialCreateInfo matCreateInfo = {};
			matCreateInfo.name = "Spring";
			matCreateInfo.shaderName = "pbr";
			matCreateInfo.constAlbedo = glm::vec4(0.8f, 0.05f, 0.04f, 1.0f);
			matCreateInfo.constRoughness = 1.0f;
			matCreateInfo.constMetallic = 0.0f;
			matCreateInfo.bDynamic = true;
			matCreateInfo.bSerializable = false;
			s_SpringMatID = g_Renderer->InitializeMaterial(&matCreateInfo);
		}

		s_BobberMatID = g_Renderer->GetMaterialID("Bobber");
		if (s_BobberMatID == InvalidMaterialID)
		{
			MaterialCreateInfo matCreateInfo = {};
			matCreateInfo.name = "Bobber";
			matCreateInfo.shaderName = "pbr";
			matCreateInfo.constAlbedo = glm::vec4(0.2f, 0.2f, 0.24f, 1.0f);
			matCreateInfo.constRoughness = 0.0f;
			matCreateInfo.constMetallic = 1.0f;
			matCreateInfo.bSerializable = false;
			s_BobberMatID = g_Renderer->InitializeMaterial(&matCreateInfo);
		}
	}

	DistanceConstraint::DistanceConstraint(i32 pointIndex0, i32 pointIndex1, real stiffness, real targetDistance) :
		Constraint(stiffness, Constraint::EqualityType::EQUALITY, Constraint::Type::DISTANCE),
		targetDistance(targetDistance)
	{
		pointIndices[0] = pointIndex0;
		pointIndices[1] = pointIndex1;
	}

	BendingConstraint::BendingConstraint(i32 pointIndex0, i32 pointIndex1, i32 pointIndex2, i32 pointIndex3, real stiffness, real targetPhi) :
		Constraint(stiffness, Constraint::EqualityType::EQUALITY, Constraint::Type::BENDING),
		targetPhi(targetPhi)
	{
		pointIndices[0] = pointIndex0;
		pointIndices[1] = pointIndex1;
		pointIndices[2] = pointIndex2;
		pointIndices[3] = pointIndex3;
	}

	Triangle::Triangle()
	{
		pointIndices[0] = 0;
		pointIndices[1] = 0;
		pointIndices[2] = 0;
	}

	Triangle::Triangle(i32 pointIndex0, i32 pointIndex1, i32 pointIndex2)
	{
		pointIndices[0] = pointIndex0;
		pointIndices[1] = pointIndex1;
		pointIndices[2] = pointIndex2;
	}

	SoftBody::SoftBody(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, SoftBodySID, gameObjectID, sourcePrefabID, bIsPrefabTemplate),
		m_SolverIterationCount(4) // Default, gets overridden in ParseUniqueFields
	{
	}

	PropertyCollection* SoftBody::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("soft body");

		collection->childCollection->RegisterProperty("solver iteration count", FIELD_OFFSET_AND_TYPE(SoftBody, m_SolverIterationCount))
			.VersionAdded(6);
		collection->childCollection->RegisterProperty("render wireframe", FIELD_OFFSET_AND_TYPE(SoftBody, m_bRenderWireframe))
			.VersionAdded(6);
		collection->childCollection->RegisterProperty("damping", FIELD_OFFSET_AND_TYPE(SoftBody, m_Damping))
			.VersionAdded(6)
			.Precision(2);
		collection->childCollection->RegisterProperty("stiffness", FIELD_OFFSET_AND_TYPE(SoftBody, m_Stiffness))
			.VersionAdded(6)
			.Precision(2);
		collection->childCollection->RegisterProperty("bending stiffness", FIELD_OFFSET_AND_TYPE(SoftBody, m_BendingStiffness))
			.VersionAdded(6)
			.Precision(2);

		return collection;
	}

	GameObject* SoftBody::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);
		SoftBody* newSoftBody = new SoftBody(newObjectName, newGameObjectID);

		CopyGenericFields(newSoftBody, parent, copyFlags);

		newSoftBody->points.resize(points.size());
		newSoftBody->constraints.resize(constraints.size());
		newSoftBody->initialPositions.resize(initialPositions.size());

		for (u32 i = 0; i < (u32)points.size(); ++i)
		{
			newSoftBody->points[i] = new Point(initialPositions[i], VEC3_ZERO, points[i]->invMass);
		}

		for (u32 i = 0; i < (u32)constraints.size(); ++i)
		{
			switch (constraints[i]->type)
			{
			case Constraint::Type::DISTANCE:
			{
				DistanceConstraint* distanceConstraint = (DistanceConstraint*)constraints[i];
				newSoftBody->constraints[i] = new DistanceConstraint(distanceConstraint->pointIndices[0], distanceConstraint->pointIndices[1], constraints[i]->stiffness, distanceConstraint->targetDistance);
			} break;
			case Constraint::Type::BENDING:
			{
				BendingConstraint* bendingConstraint = (BendingConstraint*)constraints[i];
				newSoftBody->constraints[i] = new BendingConstraint(
					bendingConstraint->pointIndices[0],
					bendingConstraint->pointIndices[1],
					bendingConstraint->pointIndices[2],
					bendingConstraint->pointIndices[3],
					constraints[i]->stiffness, bendingConstraint->targetPhi);
			} break;
			default:
			{
				PrintError("Unhandled constraint type in SoftBody::CopySelf\n");
			} break;
			}
		}

		newSoftBody->m_SolverIterationCount = m_SolverIterationCount;
		newSoftBody->m_bRenderWireframe = m_bRenderWireframe;
		newSoftBody->m_Damping = m_Damping;
		newSoftBody->m_Stiffness = m_Stiffness;
		newSoftBody->m_BendingStiffness = m_BendingStiffness;

		if (m_Mesh != nullptr)
		{
			newSoftBody->m_CurrentMeshFileName = m_CurrentMeshFileName;
			newSoftBody->m_CurrentMeshFilePath = m_CurrentMeshFilePath;
			newSoftBody->m_SelectedMeshIndex = m_SelectedMeshIndex;
			newSoftBody->LoadFromMesh();
		}

		return newSoftBody;
	}

	void SoftBody::Initialize()
	{
		m_MSToSim = 0.0f;

		GameObject::Initialize();
	}

	void SoftBody::Destroy(bool bDetachFromParent /* = true */)
	{
		for (Point* point : points)
		{
			delete point;
		}
		points.clear();

		for (Constraint* constraint : constraints)
		{
			delete constraint;
		}
		constraints.clear();

		for (Triangle* triangle : triangles)
		{
			delete triangle;
		}
		triangles.clear();

		if (m_Mesh != nullptr)
		{
			m_Mesh->Destroy();
			m_Mesh = nullptr;
			m_MeshComponent = nullptr;
		}

		GameObject::Destroy(bDetachFromParent);
	}

	void SoftBody::Update()
	{
		PROFILE_AUTO("SoftBody Update");

		GameObject::Update();

		//PROFILE_BEGIN("SoftBody Update");

		if (!m_bPaused || m_bSingleStep)
		{
			if (m_bSingleStep)
			{
				m_MSToSim = FIXED_UPDATE_TIMESTEP;
			}
			else
			{
				m_MSToSim += g_DeltaTime * 1000.0f;
			}

			m_bSingleStep = false;

			u32 fixedUpdateCount = glm::min((u32)(m_MSToSim / FIXED_UPDATE_TIMESTEP), MAX_UPDATE_COUNT);

			// TODO: Make member/store in manager
			std::vector<glm::vec3> predictedPositions(points.size());

			for (u32 updateIteration = 0; updateIteration < fixedUpdateCount; ++updateIteration)
			{
				const sec dt = FIXED_UPDATE_TIMESTEP / 1000.0f;

				std::vector<Constraint*> collisionConstraints;

				glm::vec3 globalExternalForces = glm::vec3(0.0f, -9.81f, 0.0f); // Just gravity for now

				// Apply external forces
				for (Point* point : points)
				{
					if (point->invMass != 0.0f)
					{
						point->vel += (dt / point->invMass) * globalExternalForces;
					}
				}

				// Damp velocities
				for (Point* point : points)
				{
					point->vel *= m_Damping;
				}

				// Explicit Euler step
				{
					u32 i = 0;
					for (Point* point : points)
					{
						// Predict next position
						predictedPositions[i++] = point->pos + point->vel * dt;
					}
				}

				// Generate collision constraints
				for (u32 i = 0; i < (u32)points.size(); ++i)
				{
					//glm::vec3 deltaPos = predictedPositions[i] - points[i]->pos;

				}


				// Project constraints (update predicted positions)
				for (u32 iteration = 0; iteration < m_SolverIterationCount; ++iteration)
				{
					std::vector<Constraint*> constraintLists[] = { constraints, collisionConstraints };
					for (std::vector<Constraint*>& constraintList : constraintLists)
					{
						for (Constraint* constraint : constraintList)
						{
							switch (constraint->type)
							{
							case Constraint::Type::DISTANCE:
							{
								DistanceConstraint* distanceConstraint = (DistanceConstraint*)constraint;
								Point* point0 = points[distanceConstraint->pointIndices[0]];
								Point* point1 = points[distanceConstraint->pointIndices[1]];

								if (point0->invMass == 0.0f && point1->invMass == 0.0f)
								{
									continue;
								}

								glm::vec3 pos0 = predictedPositions[distanceConstraint->pointIndices[0]];
								glm::vec3 pos1 = predictedPositions[distanceConstraint->pointIndices[1]];

								real k = distanceConstraint->stiffness;
								// Make stiffness scale linearly with iteration count
								real kPrime = 1.0f - glm::pow(1.0f - k, 1.0f / m_SolverIterationCount);

								glm::vec3 posDiff = pos1 - pos0;
								real posDiffDist = glm::length(posDiff);
								glm::vec3 posDiffN = posDiff / posDiffDist;
								real gradient = posDiffDist - distanceConstraint->targetDistance;
								glm::vec3 dp0 = ((point0->invMass / (point0->invMass + point1->invMass)) * gradient * kPrime) * posDiffN;
								glm::vec3 dp1 = ((point1->invMass / (point0->invMass + point1->invMass)) * gradient * kPrime) * -posDiffN;

								predictedPositions[distanceConstraint->pointIndices[0]] += dp0;
								predictedPositions[distanceConstraint->pointIndices[1]] += dp1;
							} break;
							case Constraint::Type::BENDING:
							{
								/*
								Points must be in the following order: (describing adjacent triangles)
									  1
									  *
									 /|\
									/ | \
								 2 /  |  \ 3
								  *   |   *
								   \  |  /
									\ | /
									 \|/
									  *
									  0
								*/

								BendingConstraint* bendingConstraint = (BendingConstraint*)constraint;
								// NOTE: Differing notation used here than in [Muller06] ([0,3] rather than [1,4])
								Point* point0 = points[bendingConstraint->pointIndices[0]];
								Point* point1 = points[bendingConstraint->pointIndices[1]];
								Point* point2 = points[bendingConstraint->pointIndices[2]];
								Point* point3 = points[bendingConstraint->pointIndices[3]];

								glm::vec3 pos0 = VEC3_ZERO;
								glm::vec3 pos1 = point1->pos - point0->pos;
								glm::vec3 pos2 = point2->pos - point0->pos;
								glm::vec3 pos3 = point3->pos - point0->pos;

								glm::vec3 normal0 = glm::normalize(glm::cross(pos1 - pos0, pos2 - pos0));
								glm::vec3 normal1 = glm::normalize(glm::cross(pos1 - pos0, pos3 - pos0));

								real d = glm::dot(normal0, normal1);
								real phi = glm::acos(d) - bendingConstraint->targetPhi;

								glm::vec3 q2 = (glm::cross(pos1, normal1) + glm::cross(normal0, pos1) * d) / glm::length(glm::cross(pos1, pos2));
								glm::vec3 q3 = (glm::cross(pos1, normal0) + glm::cross(normal1, pos1) * d) / glm::length(glm::cross(pos1, pos3));
								glm::vec3 q1 = -(glm::cross(pos2, normal1) + glm::cross(normal0, pos2) * d) / glm::length(glm::cross(pos1, pos2)) -
									(glm::cross(pos3, normal0) + glm::cross(normal1, pos3) * d) / glm::length(glm::cross(pos1, pos3));
								glm::vec3 q0 = -q1 - q2 - q3;
								real q0Sqlen = glm::dot(q0, q0);
								real q1Sqlen = glm::dot(q1, q1);
								real q2Sqlen = glm::dot(q2, q2);
								real q3Sqlen = glm::dot(q3, q3);
								real qSum = q0Sqlen + q1Sqlen + q2Sqlen + q3Sqlen;

								if (IsNanOrInf(q0) || IsNanOrInf(q1) || IsNanOrInf(q2) || IsNanOrInf(q3))
								{
									continue;
								}

								real invMassSum = point0->invMass + point1->invMass + point2->invMass + point3->invMass;

								real denom = (invMassSum * qSum);
								if (abs(denom) <= 0.0001f)
								{
									continue;
								}
								real deltaPBase = glm::sqrt(1.0f - d * d) * phi;
								if (glm::dot(glm::cross(normal0, normal1), pos3 - pos1) > 0.0f)
								{
									deltaPBase *= -1.0f;
								}

								glm::vec3 deltaP0 = -(point0->invMass * deltaPBase) / denom * q0;
								glm::vec3 deltaP1 = -(point1->invMass * deltaPBase) / denom * q1;
								glm::vec3 deltaP2 = -(point2->invMass * deltaPBase) / denom * q2;
								glm::vec3 deltaP3 = -(point3->invMass * deltaPBase) / denom * q3;

								real k = bendingConstraint->stiffness;
								// Make stiffness scale linearly with iteration count
								real kPrime = 1.0f - glm::pow(1.0f - k, 1.0f / m_SolverIterationCount);

								//Print("%.2f, %.2f\n", d, phi);
								//Print("%.2f, %.2f, %.2f, %.2f\n", q0.x, q1.x, q2.x, q3.x);
								//Print("%.2f, %.2f, %.2f\n", deltaP0.x, deltaP0.y, deltaP0.z);
								//Print("%.2f, %.2f, %.2f\n", deltaP1.x, deltaP1.y, deltaP1.z);
								//Print("%.2f, %.2f, %.2f\n", deltaP2.x, deltaP2.y, deltaP2.z);
								//Print("%.2f, %.2f, %.2f\n", deltaP3.x, deltaP3.y, deltaP3.z);
								//Print("\n");

								predictedPositions[bendingConstraint->pointIndices[0]] += deltaP0 * kPrime;
								predictedPositions[bendingConstraint->pointIndices[1]] += deltaP1 * kPrime;
								predictedPositions[bendingConstraint->pointIndices[2]] += deltaP2 * kPrime;
								predictedPositions[bendingConstraint->pointIndices[3]] += deltaP3 * kPrime;
							} break;
							default:
							{
								PrintError("Unhandled constraint type\n");
							} break;
							}
						}
					}
				}

				// Integrate
				{
					u32 i = 0;
					for (Point* point : points)
					{
						point->vel = (predictedPositions[i] - point->pos) / dt;
						point->pos = predictedPositions[i++];
					}
				}

				// Update velocities (friction, restitution)
				// Skip for now


				for (Constraint* collisionConstraint : collisionConstraints)
				{
					delete collisionConstraint;
				}
			}


			m_MSToSim -= fixedUpdateCount * FIXED_UPDATE_TIMESTEP;
		}

		if (m_MeshComponent != nullptr)
		{
			if (m_MeshVertexBufferCreateInfo.positions_3D.size() != points.size())
			{
				m_MeshVertexBufferCreateInfo.positions_3D.resize(points.size());
			}
			glm::vec3 worldPos = m_Transform.GetWorldPosition();
			for (u32 i = 0; i < (u32)points.size(); ++i)
			{
				m_MeshVertexBufferCreateInfo.positions_3D[i] = points[i]->pos - worldPos;
			}

			std::vector<u32> indices = m_MeshComponent->GetIndexBufferCopy();
			MeshComponent::CalculateTangents(m_MeshVertexBufferCreateInfo, indices);

			//for (u32 i = 0; i <(u32)m_MeshVertexBufferCreateInfo.normals.size(); ++i)
			//{
			//	glm::mat4 worldMat = glm::mat4(m_Transform.GetLocalRotation()) * glm::scale(MAT4_IDENTITY, m_Transform.GetWorldScale());
			//	m_MeshVertexBufferCreateInfo.normals[i] = (glm::vec3)(worldMat * glm::vec4(m_MeshVertexBufferCreateInfo.normals[i], 1.0f));
			//	m_MeshVertexBufferCreateInfo.tangents[i] = (glm::vec3)(worldMat * glm::vec4(m_MeshVertexBufferCreateInfo.tangents[i], 1.0f));
			//}

			m_MeshComponent->UpdateDynamicVertexData(m_MeshVertexBufferCreateInfo, indices);
		}

		if (m_bRenderWireframe)
		{
			Draw();
		}
	}

	void SoftBody::Draw()
	{
		DebugRenderer* debugRenderer = g_Renderer->GetDebugRenderer();
		for (Point* point : points)
		{
			debugRenderer->drawSphere(ToBtVec3(point->pos), 0.1f, btVector3(0.8f, 0.2f, 0.1f));
		}

		for (u32 i = 0; i < (u32)constraints.size(); ++i)
		{
			Constraint* constraint = constraints[i];

			switch (constraint->type)
			{
			case Constraint::Type::DISTANCE:
			{
				DistanceConstraint* distanceConstraint = (DistanceConstraint*)constraint;
				debugRenderer->drawLine(
					ToBtVec3(points[distanceConstraint->pointIndices[0]]->pos),
					ToBtVec3(points[distanceConstraint->pointIndices[1]]->pos),
					btVector3(0.5f, 0.4f, 0.1f), btVector3(0.5f, 0.4f, 0.1f));
			} break;
			case Constraint::Type::BENDING:
			{
				BendingConstraint* bendingConstraint = (BendingConstraint*)constraint;

				if (i == (u32)m_ShownBendingIndex)
				{
					Point* point0 = points[bendingConstraint->pointIndices[0]];
					Point* point1 = points[bendingConstraint->pointIndices[1]];
					Point* point2 = points[bendingConstraint->pointIndices[2]];
					Point* point3 = points[bendingConstraint->pointIndices[3]];

					glm::vec3 normal0 = glm::normalize(glm::cross(point1->pos - point0->pos, point2->pos - point0->pos));
					glm::vec3 normal1 = glm::normalize(glm::cross(point1->pos - point0->pos, point3->pos - point0->pos));

					btVector3 col = btVector3(0.1f, 0.9f, 0.1f);
					btVector3 colN = btVector3(0.9f, 0.1f, 0.1f);
					real f = 0.01f;

					debugRenderer->drawLine(ToBtVec3(points[bendingConstraint->pointIndices[0]]->pos + normal0 * f), ToBtVec3(points[bendingConstraint->pointIndices[1]]->pos + normal0 * f), col, col);
					debugRenderer->drawLine(ToBtVec3(points[bendingConstraint->pointIndices[1]]->pos + normal0 * f), ToBtVec3(points[bendingConstraint->pointIndices[2]]->pos + normal0 * f), col, col);
					debugRenderer->drawLine(ToBtVec3(points[bendingConstraint->pointIndices[2]]->pos + normal0 * f), ToBtVec3(points[bendingConstraint->pointIndices[0]]->pos + normal0 * f), col, col);
					debugRenderer->drawLine(ToBtVec3(points[bendingConstraint->pointIndices[0]]->pos + normal1 * f), ToBtVec3(points[bendingConstraint->pointIndices[1]]->pos + normal1 * f), col, col);
					debugRenderer->drawLine(ToBtVec3(points[bendingConstraint->pointIndices[0]]->pos + normal1 * f), ToBtVec3(points[bendingConstraint->pointIndices[3]]->pos + normal1 * f), col, col);
					debugRenderer->drawLine(ToBtVec3(points[bendingConstraint->pointIndices[3]]->pos + normal1 * f), ToBtVec3(points[bendingConstraint->pointIndices[1]]->pos + normal1 * f), col, col);

					glm::vec3 tri0Mid = (points[bendingConstraint->pointIndices[0]]->pos + points[bendingConstraint->pointIndices[1]]->pos + points[bendingConstraint->pointIndices[2]]->pos) / 3.0f;
					glm::vec3 tri1Mid = (points[bendingConstraint->pointIndices[0]]->pos + points[bendingConstraint->pointIndices[1]]->pos + points[bendingConstraint->pointIndices[3]]->pos) / 3.0f;

					debugRenderer->drawLine(ToBtVec3(tri0Mid), ToBtVec3(tri0Mid + normal0 * 1.0f), colN, colN);
					debugRenderer->drawLine(ToBtVec3(tri1Mid), ToBtVec3(tri1Mid + normal1 * 1.0f), colN, colN);
				}
			} break;
			}
		}
	}

	u32 SoftBody::AddUniqueDistanceConstraint(i32 index0, i32 index1, u32 atIndex, real stiffness)
	{
		for (Constraint* constraint : constraints)
		{
			if (constraint != nullptr &&
				constraint->type == Constraint::Type::DISTANCE)
			{
				DistanceConstraint* distanceConstraint = (DistanceConstraint*)constraint;
				if (((distanceConstraint->pointIndices[0] == index0 && distanceConstraint->pointIndices[1] == index1) ||
					(distanceConstraint->pointIndices[1] == index0 && distanceConstraint->pointIndices[0] == index1)))
				{
					return atIndex != u32_max ? atIndex : (u32)constraints.size();
				}
			}
		}

		if (atIndex != u32_max)
		{
			if (atIndex >= (u32)constraints.size())
			{
				constraints.resize(atIndex + 1);
			}

			constraints[atIndex] = new DistanceConstraint(index0, index1, stiffness, glm::distance(points[index0]->pos, points[index1]->pos));
			return atIndex + 1;
		}
		else
		{
			constraints.push_back(new DistanceConstraint(index0, index1, stiffness, glm::distance(points[index0]->pos, points[index1]->pos)));
			return (u32)constraints.size();
		}
	}

	u32 SoftBody::AddUniqueBendingConstraint(i32 index0, i32 index1, i32 index2, i32 index3, u32 atIndex, real stiffness)
	{
		for (Constraint* constraint : constraints)
		{
			if (constraint != nullptr &&
				constraint->type == Constraint::Type::BENDING)
			{
				BendingConstraint* bendingConstraint = (BendingConstraint*)constraint;
				if ((bendingConstraint->pointIndices[0] == index0 && bendingConstraint->pointIndices[1] == index1) &&
					(bendingConstraint->pointIndices[2] == index2 && bendingConstraint->pointIndices[3] == index3))
				{
					return atIndex != u32_max ? atIndex : (u32)constraints.size();
				}
			}
		}

		glm::vec3 normal0 = glm::normalize(glm::cross(points[index1]->pos - points[index0]->pos, points[index2]->pos - points[index0]->pos));
		glm::vec3 normal1 = glm::normalize(glm::cross(points[index1]->pos - points[index0]->pos, points[index3]->pos - points[index0]->pos));

		real initialPhi = glm::acos(glm::dot(normal0, normal1));

		if (atIndex != u32_max)
		{
			if (atIndex >= (u32)constraints.size())
			{
				constraints.resize(atIndex + 1);
			}

			constraints[atIndex] = new BendingConstraint(index0, index1, index2, index3, stiffness, initialPhi);
			return atIndex + 1;
		}
		else
		{
			constraints.push_back(new BendingConstraint(index0, index1, index2, index3, stiffness, initialPhi));
			return (u32)constraints.size();
		}
	}

	void SoftBody::LoadFromMesh()
	{
		if (m_Mesh != nullptr)
		{
			m_Mesh->Destroy();
			delete m_Mesh;
			m_MeshComponent = nullptr;
		}

		if (m_MeshMaterialID == InvalidMaterialID)
		{
			MaterialCreateInfo matCreateInfo = {};
			matCreateInfo.name = "Soft Body Material";
			matCreateInfo.shaderName = "pbr";
			matCreateInfo.bDynamic = true;
			matCreateInfo.constRoughness = 0.95f;
			matCreateInfo.bSerializable = false;

			m_MeshMaterialID = g_Renderer->InitializeMaterial(&matCreateInfo);
		}

		m_Mesh = new Mesh(this);

		RenderObjectCreateInfo renderObjectCreateInfo = {};
		renderObjectCreateInfo.cullFace = CullFace::NONE;

		Mesh::CreateInfo meshCreateInfo = {};
		meshCreateInfo.relativeFilePath = m_CurrentMeshFilePath;
		meshCreateInfo.materialIDs = { m_MeshMaterialID };
		meshCreateInfo.bDynamic = true;
		meshCreateInfo.optionalRenderObjectCreateInfo = &renderObjectCreateInfo;
		if (!m_Mesh->LoadFromFile(meshCreateInfo))
		{
			PrintError("Failed to load mesh\n");
			m_Mesh->Destroy();
			delete m_Mesh;
			m_Mesh = nullptr;
		}
		else
		{
			m_MeshVertexBufferCreateInfo = {};
			m_MeshVertexBufferCreateInfo.attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(m_MeshMaterialID)->shaderID)->vertexAttributes;

			m_MeshComponent = m_Mesh->GetSubMesh(0);
			VertexBufferData* vertexBufferData = m_MeshComponent->GetVertexBufferData();
			std::vector<u32> indexData = m_MeshComponent->GetIndexBufferCopy();
			std::vector<glm::vec3> posData(vertexBufferData->VertexCount);
			std::vector<glm::vec2> uvData(vertexBufferData->VertexCount);
			VertexBufferData::ResizeForPresentAttributes(m_MeshVertexBufferCreateInfo, vertexBufferData->VertexCount);
			bool bCopySuccess = vertexBufferData->CopyInto((real*)posData.data(), (VertexAttributes)VertexAttribute::POSITION) == (posData.size() * sizeof(glm::vec3));
			bCopySuccess = bCopySuccess && vertexBufferData->CopyInto((real*)uvData.data(), (VertexAttributes)VertexAttribute::UV) == (uvData.size() * sizeof(glm::vec2));
			if (!bCopySuccess)
			{
				PrintError("Something bad happened in soft body mesh conversion\n");
			}
			else
			{
				for (Point* point : points)
				{
					delete point;
				}
				points.clear();

				for (Constraint* constraint : constraints)
				{
					delete constraint;
				}
				triangles.clear();

				for (Triangle* triangle : triangles)
				{
					delete triangle;
				}
				triangles.clear();

				points.resize(vertexBufferData->VertexCount);
				initialPositions.resize(vertexBufferData->VertexCount);
				glm::vec3 smallestPos(99999.0f);
				for (u32 i = 0; i < (u32)posData.size(); ++i)
				{
					glm::vec3 pos = posData[i];
					if (pos.x < smallestPos.x || (pos.x == smallestPos.x && pos.z < smallestPos.z))
					{
						smallestPos = pos;
					}
					initialPositions[i] = pos;

					m_MeshVertexBufferCreateInfo.texCoords_UV[i] = uvData[i];
					points[i] = new Point(pos, VEC3_ZERO, 1.0f);
				}

				triangles.resize(indexData.size() / 3);
				for (u32 i = 0; i < (u32)triangles.size(); ++i)
				{
					triangles[i] = new Triangle(indexData[i * 3], indexData[i * 3 + 1], indexData[i * 3 + 2]);
				}

				constraints.resize(indexData.size() / 3);
				u32 constraintIndex = 0;
				for (u32 i = 0; i < (u32)indexData.size() - 1; i += 3)
				{
					constraintIndex = AddUniqueDistanceConstraint(indexData[i + 0], indexData[i + 1], constraintIndex, 0.995f);
					constraintIndex = AddUniqueDistanceConstraint(indexData[i + 1], indexData[i + 2], constraintIndex, 0.995f);
					constraintIndex = AddUniqueDistanceConstraint(indexData[i + 2], indexData[i + 0], constraintIndex, 0.995f);
				}

				m_FirstBendingConstraintIndex = (i32)constraints.size();
				m_ShownBendingIndex = m_FirstBendingConstraintIndex;

				for (u32 i = 0; i < (u32)indexData.size() - 1; i += 3)
				{
					i32 index0 = indexData[i + 0];
					i32 index1 = indexData[i + 1];
					i32 index2 = indexData[i + 2];

					Triangle tri{ index0, index1, index2 };

					// edge0 = 0 -> 1, edge1 = 1 -> 2, edge2 = 2 -> 0
					Triangle sharedTri;
					i32 outOutsideVertIndex;
					if (GetTriangleSharingEdge(indexData, index0, index1, tri, sharedTri, outOutsideVertIndex))
					{
						i32 index3 = sharedTri.pointIndices[outOutsideVertIndex];
						constraintIndex = AddUniqueBendingConstraint(index0, index1, index2, index3, constraintIndex, 0.995f);
					}
					if (GetTriangleSharingEdge(indexData, index1, index2, tri, sharedTri, outOutsideVertIndex))
					{
						i32 index3 = sharedTri.pointIndices[outOutsideVertIndex];
						constraintIndex = AddUniqueBendingConstraint(index1, index2, index0, index3, constraintIndex, 0.995f);
					}
					if (GetTriangleSharingEdge(indexData, index2, index0, tri, sharedTri, outOutsideVertIndex))
					{
						i32 index3 = sharedTri.pointIndices[outOutsideVertIndex];
						constraintIndex = AddUniqueBendingConstraint(index2, index0, index1, index3, constraintIndex, 0.995f);
					}
				}
			}
		}
	}

	bool SoftBody::GetTriangleSharingEdge(const std::vector<u32>& indexData, i32 edgeIndex0, i32 edgeIndex1, const Triangle& originalTri, Triangle& outTri, i32& outOutsideVertIndex)
	{
		outOutsideVertIndex = -1;

		for (u32 i = 0; i < (u32)indexData.size() - 1; i += 3)
		{
			i32 index0 = indexData[i + 0];
			i32 index1 = indexData[i + 1];
			i32 index2 = indexData[i + 2];

			if (memcmp(originalTri.pointIndices, &indexData[i], sizeof(i32) * 3) == 0)
			{
				// Skip original tri
				continue;
			}

			if (index0 == edgeIndex0 && (index1 == edgeIndex1 || index2 == edgeIndex1))
			{
				outOutsideVertIndex = (index1 == edgeIndex1) ? 2 : 1;
			}
			if (index1 == edgeIndex0 && (index0 == edgeIndex1 || index2 == edgeIndex1))
			{
				outOutsideVertIndex = (index0 == edgeIndex1) ? 2 : 0;
			}
			if (index2 == edgeIndex0 && (index0 == edgeIndex1 || index1 == edgeIndex1))
			{
				outOutsideVertIndex = (index0 == edgeIndex1) ? 1 : 0;
			}
			if (outOutsideVertIndex != -1)
			{
				outTri.pointIndices[0] = index0;
				outTri.pointIndices[1] = index1;
				outTri.pointIndices[2] = index2;
				return true;
			}
		}
		return false;
	}

	void SoftBody::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject softBodyObject;
		if (parentObject.TryGetObject("soft body", softBodyObject))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(softBodyObject, collection->childCollection);

			std::vector<JSONObject> pointsArr;
			if (softBodyObject.TryGetObjectArray("points", pointsArr))
			{
				points.resize(pointsArr.size());
				initialPositions.resize(pointsArr.size());

				u32 i = 0;
				for (JSONObject& pointObj : pointsArr)
				{
					glm::vec3 pos = pointObj.GetVec3("position");
					glm::vec3 vel = VEC3_ZERO;
					real invMass = pointObj.GetFloat("inverse mass");
					points[i] = new Point(pos, vel, invMass);
					initialPositions[i] = points[i]->pos;

					i++;
				}
			}

			std::vector<JSONObject> constraintsArr;
			if (softBodyObject.TryGetObjectArray("constraints", constraintsArr))
			{
				constraints.resize(constraintsArr.size());

				u32 i = 0;
				for (JSONObject& constraintObj : constraintsArr)
				{
					real stiffness = constraintObj.GetFloat("stiffness");
					Constraint::Type type = (Constraint::Type)constraintObj.GetInt("type");

					switch (type)
					{
					case Constraint::Type::DISTANCE:
					{
						JSONObject distanceConstraintObj = constraintObj.GetObject("distance constraint");

						i32 index0 = distanceConstraintObj.GetInt("index 0");
						i32 index1 = distanceConstraintObj.GetInt("index 1");
						real targetDistance = distanceConstraintObj.GetFloat("target distance");

						constraints[i] = new DistanceConstraint(index0, index1, stiffness, targetDistance);
					} break;
					case Constraint::Type::BENDING:
					{
						JSONObject bendingConstraintObj = constraintObj.GetObject("bending constraint");

						i32 index0 = bendingConstraintObj.GetInt("index 0");
						i32 index1 = bendingConstraintObj.GetInt("index 1");
						i32 index2 = bendingConstraintObj.GetInt("index 2");
						i32 index3 = bendingConstraintObj.GetInt("index 3");
						real targetPhi = bendingConstraintObj.GetFloat("target phi");

						constraints[i] = new BendingConstraint(index0, index1, index2, index3, stiffness, targetPhi);
					} break;
					default:
					{
						PrintError("Unhandled constraint type in SoftBody::ParseUniqueFields\n");
						constraints[i] = nullptr;
					} break;
					}

					i++;
				}
			}

			if (softBodyObject.TryGetString("mesh file path", m_CurrentMeshFilePath))
			{
				m_CurrentMeshFileName = StripLeadingDirectories(m_CurrentMeshFilePath);
				for (i32 i = 0; i < (i32)g_ResourceManager->discoveredMeshes.size(); ++i)
				{
					if (g_ResourceManager->discoveredMeshes[i] == m_CurrentMeshFilePath)
					{
						m_SelectedMeshIndex = i;
						break;
					}
				}
				if (m_SelectedMeshIndex == -1)
				{
					m_CurrentMeshFilePath = "";
					m_CurrentMeshFileName = "";
					PrintWarn("SoftBody's previously saved mesh (%s) might have been moved or renamed, clearing field.\n", m_CurrentMeshFileName.c_str());
				}
				else
				{
					LoadFromMesh();
				}
			}
		}
	}

	void SoftBody::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject softBodyObject = {};

		SerializeRegisteredProperties(softBodyObject, collection->childCollection, bSerializePrefabData);

		std::vector<JSONObject> pointsArr(points.size());
		{
			// Points are simulated & parsed in world space
			u32 i = 0;
			for (const Point* point : points)
			{
				pointsArr[i].fields.emplace_back("position", JSONValue(point->pos));
				pointsArr[i].fields.emplace_back("inverse mass", JSONValue(point->invMass));

				i++;
			}
		}
		softBodyObject.fields.emplace_back("points", JSONValue(pointsArr));

		std::vector<JSONObject> constraintsArr(constraints.size());
		{
			u32 i = 0;
			for (const Constraint* constraint : constraints)
			{
				constraintsArr[i].fields.emplace_back("stiffness", JSONValue(constraint->stiffness));
				constraintsArr[i].fields.emplace_back("type", JSONValue((i32)constraint->type));

				switch (constraint->type)
				{
				case Constraint::Type::DISTANCE:
				{
					DistanceConstraint* distanceConstraint = (DistanceConstraint*)constraint;

					JSONObject distanceConstraintObj = {};

					distanceConstraintObj.fields.emplace_back("index 0", JSONValue(distanceConstraint->pointIndices[0]));
					distanceConstraintObj.fields.emplace_back("index 1", JSONValue(distanceConstraint->pointIndices[1]));
					distanceConstraintObj.fields.emplace_back("target distance", JSONValue(distanceConstraint->targetDistance));

					constraintsArr[i].fields.emplace_back("distance constraint", JSONValue(distanceConstraintObj));
				} break;
				case Constraint::Type::BENDING:
				{
					BendingConstraint* bendingConstraint = (BendingConstraint*)constraint;

					JSONObject bendingConstraintObj = {};

					bendingConstraintObj.fields.emplace_back("index 0", JSONValue(bendingConstraint->pointIndices[0]));
					bendingConstraintObj.fields.emplace_back("index 1", JSONValue(bendingConstraint->pointIndices[1]));
					bendingConstraintObj.fields.emplace_back("index 2", JSONValue(bendingConstraint->pointIndices[2]));
					bendingConstraintObj.fields.emplace_back("index 3", JSONValue(bendingConstraint->pointIndices[3]));
					bendingConstraintObj.fields.emplace_back("target phi", JSONValue(bendingConstraint->targetPhi));

					constraintsArr[i].fields.emplace_back("bending constraint", JSONValue(bendingConstraintObj));
				} break;
				default:
				{
					PrintError("Unhandled type in SoftBody::SerializeUniqueFields\n");
				} break;
				}

				i++;
			}
		}
		softBodyObject.fields.emplace_back("constraints", JSONValue(constraintsArr));

		if (m_SelectedMeshIndex != -1)
		{
			softBodyObject.fields.emplace_back("mesh file path", JSONValue(m_CurrentMeshFilePath));
		}

		parentObject.fields.emplace_back("soft body", JSONValue(softBodyObject));
	}

	void SoftBody::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		ImGuiExt::SliderUInt("Solver iteration count", &m_SolverIterationCount, 1, 60);

		ImGui::SameLine();

		if (ImGui::Button("Reset"))
		{
			u32 i = 0;
			for (Point* point : points)
			{
				point->pos = initialPositions[i++];
				point->vel = VEC3_ZERO;
			}
		}

		if (ImGui::Button("Single Step"))
		{
			m_bSingleStep = true;
			m_bPaused = true;
		}

		ImGui::Text("%.2fms", m_UpdateDuration);

		if (ImGui::Checkbox("Render wireframe", &m_bRenderWireframe))
		{
			std::vector<SoftBody*> softBodies = g_SceneManager->CurrentScene()->GetObjectsOfType<SoftBody>(SoftBodySID);
			for (SoftBody* softBody : softBodies)
			{
				softBody->m_bRenderWireframe = m_bRenderWireframe;
			}
		}

		if (ImGui::SliderFloat("Damping", &m_Damping, 0.0f, 1.0f))
		{
			std::vector<SoftBody*> softBodies = g_SceneManager->CurrentScene()->GetObjectsOfType<SoftBody>(SoftBodySID);
			for (SoftBody* softBody : softBodies)
			{
				softBody->m_Damping = m_Damping;
			}
		}

		if (ImGui::SliderFloat("Stiffness", &m_Stiffness, 0.0f, 1.0f))
		{
			std::vector<SoftBody*> softBodies = g_SceneManager->CurrentScene()->GetObjectsOfType<SoftBody>(SoftBodySID);
			for (SoftBody* softBody : softBodies)
			{
				for (Constraint* constraint : softBody->constraints)
				{
					if (constraint->type == Constraint::Type::DISTANCE)
					{
						constraint->stiffness = m_Stiffness;
					}
				}
			}
		}

		if (ImGui::SliderFloat("Bending stiffness", &m_BendingStiffness, 0.0f, 1.0f))
		{
			std::vector<SoftBody*> softBodies = g_SceneManager->CurrentScene()->GetObjectsOfType<SoftBody>(SoftBodySID);
			for (SoftBody* softBody : softBodies)
			{
				for (Constraint* constraint : softBody->constraints)
				{
					if (constraint->type == Constraint::Type::BENDING)
					{
						constraint->stiffness = m_BendingStiffness;
					}
				}
			}
		}

		if (ImGui::BeginCombo("##meshcombo", m_CurrentMeshFileName.c_str()))
		{
			for (i32 i = 0; i < (i32)g_ResourceManager->discoveredMeshes.size(); ++i)
			{
				ImGui::PushID(i);
				const std::string& meshFilePath = g_ResourceManager->discoveredMeshes[i];
				bool bSelected = (i == m_SelectedMeshIndex);
				const std::string meshFileNameShort = StripLeadingDirectories(meshFilePath);
				if (ImGui::Selectable(meshFileNameShort.c_str(), &bSelected))
				{
					if (m_SelectedMeshIndex != i)
					{
						m_SelectedMeshIndex = i;
						m_CurrentMeshFilePath = meshFilePath;
						m_CurrentMeshFileName = meshFileNameShort;
					}
				}

				ImGui::PopID();
			}

			ImGui::EndCombo();
		}

		ImGui::InputInt("Selected bending constraint", &m_ShownBendingIndex);

		if (m_SelectedMeshIndex != -1)
		{
			ImGui::SameLine();

			if (ImGui::Button("Load mesh"))
			{
				LoadFromMesh();
			}
		}
	}

	void SoftBody::SetStiffness(real stiffness)
	{
		m_Stiffness = stiffness;
	}

	void SoftBody::SetDamping(real damping)
	{
		m_Damping = damping;
	}

	void SoftBody::SetRenderWireframe(bool bRenderWireframe)
	{
		m_bRenderWireframe = bRenderWireframe;
	}

	Vehicle::Vehicle(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, VehicleSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate),
		m_WheelSlipHisto(RollingAverage<real>(256, SamplingType::CONSTANT))
	{
		m_bInteractable = true;
		// Vehicles can be picked up into the player's inventory & placed again
		m_bItemizable = true;

		for (i32 i = 0; i < (i32)SoundEffect::_COUNT; ++i)
		{
			m_SoundEffects[i] = SoundClip_LoopingSimple(VehicleSoundEffectNames[i], InvalidStringID, false);
			m_SoundEffectSIDs[i] = InvalidStringID;
		}
	}

	PropertyCollection* Vehicle::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();
		return collection;
	}

	GameObject* Vehicle::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);
		Vehicle* newGameObject = new Vehicle(newObjectName, newGameObjectID);

		CopyGenericFields(newGameObject, parent, copyFlags);

		// Children now exist
		for (i32 i = 0; i < m_TireCount; ++i)
		{
			newGameObject->m_TireIDs[i] = GameObjectID();
		}

		for (i32 i = 0; i < 2; ++i)
		{
			newGameObject->m_BrakeLightIDs[i] = GameObjectID();
		}

		// Temporarily set sibling indices as if these objects are both root objects (this will
		// be overwritten by the proper values soon)
		UpdateSiblingIndices(0);
		newGameObject->UpdateSiblingIndices(0);

		FixupPrefabTemplateIDs(newGameObject);

		for (i32 i = 0; i < (i32)SoundEffect::_COUNT; ++i)
		{
			newGameObject->m_SoundEffectSIDs[i] = m_SoundEffectSIDs[i];
			// m_SoundEffects array will be set in PostInitialize
		}

		return newGameObject;
	}

	void Vehicle::Initialize()
	{
		GameObject::Initialize();

		m_pLinearVelocity = ToBtVec3(VEC3_ZERO);

		BaseScene* scene = g_SceneManager->CurrentScene();
		GameObject* tireFL = scene->GetGameObject(m_TireIDs[(u32)Tire::FL]);
		GameObject* brakeLightL = scene->GetGameObject(m_BrakeLightIDs[0]);
		GameObject* brakeLightR = scene->GetGameObject(m_BrakeLightIDs[1]);
		if (brakeLightL != nullptr && brakeLightR != nullptr &&
			brakeLightL->GetTypeID() == SpotLightSID &&
			brakeLightR->GetTypeID() == SpotLightSID)
		{
			((SpotLight*)brakeLightL)->SetVisible(false, true);
			((SpotLight*)brakeLightR)->SetVisible(false, true);
		}
		else
		{
			PrintError("Expected vehicle brake light objects to be spot lights\n");
		}

		if (m_Mesh != nullptr)
		{
			m_GlassMatID = m_Mesh->GetSubMesh(0)->GetMaterialID();
			m_CarPaintMatID = m_Mesh->GetSubMesh(1)->GetMaterialID();
			m_BrakeLightMatID = m_Mesh->GetSubMesh(2)->GetMaterialID();
			m_ReverseLightMatID = m_Mesh->GetSubMesh(3)->GetMaterialID();
			if (tireFL != nullptr)
			{
				m_TireMatID = tireFL->GetMesh()->GetSubMesh(0)->GetMaterialID();
				m_SpokeMatID = tireFL->GetMesh()->GetSubMesh(1)->GetMaterialID();
			}

			m_InitialBrakeLightMatEmissive = g_Renderer->GetMaterial(m_BrakeLightMatID)->constEmissive;
			m_InitialReverseLightMatEmissive = g_Renderer->GetMaterial(m_ReverseLightMatID)->constEmissive;

			m_ActiveBrakeLightMatEmissive = glm::min(m_InitialBrakeLightMatEmissive + glm::vec4(0.2f, 0.0f, 0.0f, 0.0f), VEC4_ONE);
			m_ActiveReverseLightMatEmissive = glm::min(m_InitialReverseLightMatEmissive + glm::vec4(0.4f), VEC4_ONE);
		}

		CreateRigidBody();
	}

	void Vehicle::CreateRigidBody()
	{
		if (m_RigidBody != nullptr)
		{
			BaseScene* scene = g_SceneManager->CurrentScene();
			GameObject* tireFL = scene->GetGameObject(m_TireIDs[(u32)Tire::FL]);
			GameObject* tireFR = scene->GetGameObject(m_TireIDs[(u32)Tire::FR]);
			GameObject* tireRL = scene->GetGameObject(m_TireIDs[(u32)Tire::RL]);
			GameObject* tireRR = scene->GetGameObject(m_TireIDs[(u32)Tire::RR]);

			m_tuning = {};

			btDiscreteDynamicsWorld* dynamicsWorld = scene->GetPhysicsWorld()->GetWorld();

			btRigidBody* chassisRB = m_RigidBody->GetRigidBodyInternal();

			m_VehicleRaycaster = new btDefaultVehicleRaycaster(dynamicsWorld);
			m_Vehicle = new btRaycastVehicle(m_tuning, chassisRB, m_VehicleRaycaster);

			chassisRB->setActivationState(DISABLE_DEACTIVATION);

			dynamicsWorld->addVehicle(m_Vehicle);

			m_Vehicle->setCoordinateSystem(0, 1, 2);

			btScalar suspensionRestLength(0.6f);
			btVector3 wheelDirectionCS0(0, -1, 0);
			btVector3 wheelAxleCS(-1, 0, 0);

			btVector3 connecitonPoint = ToBtVec3(tireFL->GetTransform()->GetLocalPosition());
			bool bIsFrontWheel = true;
			m_Vehicle->addWheel(connecitonPoint, wheelDirectionCS0, wheelAxleCS, suspensionRestLength, m_WheelRadius, m_tuning, bIsFrontWheel);
			connecitonPoint = ToBtVec3(tireFR->GetTransform()->GetLocalPosition());
			m_Vehicle->addWheel(connecitonPoint, wheelDirectionCS0, wheelAxleCS, suspensionRestLength, m_WheelRadius, m_tuning, bIsFrontWheel);
			bIsFrontWheel = false;
			connecitonPoint = ToBtVec3(tireRL->GetTransform()->GetLocalPosition());
			m_Vehicle->addWheel(connecitonPoint, wheelDirectionCS0, wheelAxleCS, suspensionRestLength, m_WheelRadius, m_tuning, bIsFrontWheel);
			connecitonPoint = ToBtVec3(tireRR->GetTransform()->GetLocalPosition());
			m_Vehicle->addWheel(connecitonPoint, wheelDirectionCS0, wheelAxleCS, suspensionRestLength, m_WheelRadius, m_tuning, bIsFrontWheel);

			for (i32 i = 0; i < m_Vehicle->getNumWheels(); ++i)
			{
				btWheelInfo& wheel = m_Vehicle->getWheelInfo(i);
				wheel.m_suspensionStiffness = m_SuspensionStiffness;
				wheel.m_wheelsDampingRelaxation = m_SuspensionDamping;
				wheel.m_wheelsDampingCompression = m_SuspensionCompression;
				wheel.m_frictionSlip = m_WheelFriction;
				wheel.m_rollInfluence = m_RollInfluence;
			}
		}
	}

	void Vehicle::PostInitialize()
	{
		if (m_RigidBody != nullptr)
		{
			m_RigidBody->SetInterpolation(true, true);
		}

		for (i32 i = 0; i < (i32)SoundEffect::_COUNT; ++i)
		{
			if (m_SoundEffectSIDs[i] != InvalidStringID)
			{
				SetSoundEffectSID((SoundEffect)i, m_SoundEffectSIDs[i]);
			}
		}

		GameObject::PostInitialize();
	}

	void Vehicle::Destroy(bool bDetachFromParent /* = true */)
	{
		if (g_SceneManager->HasSceneLoaded())
		{
			BaseScene* scene = g_SceneManager->CurrentScene();
			btDiscreteDynamicsWorld* dynamicsWorld = scene->GetPhysicsWorld()->GetWorld();
			dynamicsWorld->removeVehicle(m_Vehicle);
		}

		GameObject::Destroy(bDetachFromParent);
	}

	void Vehicle::Update()
	{
		PROFILE_AUTO("Vehicle Update");

		GameObject::Update();

		if (m_RigidBody == nullptr)
		{
			return;
		}

		btRigidBody* rb = m_RigidBody->GetRigidBodyInternal();
		if (rb == nullptr)
		{
			return;
		}

		const btVector3 linearVel = rb->getLinearVelocity();
		const btVector3 linearAccel = (linearVel - m_pLinearVelocity) / g_DeltaTime;
		const real forwardVel = glm::dot(m_Transform.GetForward(), ToVec3(linearVel));
		//const real forwardAccel = glm::dot(m_Transform.GetForward(), ToVec3(linearVel));
		// How much our acceleration opposes our current velocity
		real slowingVelocity = glm::clamp(1.0f - glm::min(glm::dot(ToVec3(linearAccel), ToVec3(linearVel)) / linearVel.length(), 0.0f), 0.0f, 1.0f);

		//btVector3 force = btVector3(0.0f, 0.0f, 0.0f);

		real engineForceSlowScale = (1.0f - g_DeltaTime * m_EngineForceDecelerationSpeed);
		real steeringSlowScale = (1.0f - g_DeltaTime * STEERING_SLOW_FACTOR);

		real maxSteerVel = 30.0f;
		real steeringScale = Lerp(0.45f, 1.0f, 1.0f - glm::clamp(forwardVel / maxSteerVel, 0.0f, 1.0f));

		if (m_bOccupied && g_CameraManager->CurrentCamera()->bIsGameplayCam)
		{
			// The faster we get, the slower we accelerate
			real accelFactor = 1.0f - glm::pow(glm::clamp(m_EngineForce / m_MaxEngineForce, 0.0f, 1.0f), 5.0f);

			real accelerateInput = g_InputManager->GetActionAxisValue(Action::VEHICLE_ACCELERATE);
			real reverseInput = g_InputManager->GetActionAxisValue(Action::VEHICLE_REVERSE);
			real brakeInput = g_InputManager->GetActionAxisValue(Action::VEHICLE_BRAKE);
			real turnLeftInput = g_InputManager->GetActionAxisValue(Action::VEHICLE_TURN_LEFT);
			real turnRightInput = g_InputManager->GetActionAxisValue(Action::VEHICLE_TURN_RIGHT);
			if (accelerateInput != 0.0f)
			{
				m_EngineForce += accelerateInput * m_EngineAccel * g_DeltaTime * accelFactor;
			}
			if (reverseInput != 0.0f)
			{
				m_EngineForce -= reverseInput * m_EngineAccel * g_DeltaTime * accelFactor;
			}
			if (turnLeftInput != 0.0f)
			{
				m_Steering -= turnLeftInput * m_TurnAccel * g_DeltaTime * steeringScale;
				m_Steering = glm::clamp(m_Steering, -MAX_STEER, MAX_STEER);
			}
			if (turnRightInput != 0.0f)
			{
				m_Steering += turnRightInput * m_TurnAccel * g_DeltaTime * steeringScale;
				m_Steering = glm::clamp(m_Steering, -MAX_STEER, MAX_STEER);
			}
			if (brakeInput != 0.0f)
			{
				m_BrakeForce = m_MaxBrakeForce * brakeInput;
				engineForceSlowScale = 0.0f;
			}
			else
			{
				m_BrakeForce = 0.0f;
			}
		}

		bool bBraking = m_BrakeForce > 0.0f;
		bool bReversing = m_EngineForce < 0.0f;

		if (m_Mesh != nullptr)
		{
			if (bBraking)
			{
				g_Renderer->GetMaterial(m_BrakeLightMatID)->constEmissive = m_ActiveBrakeLightMatEmissive;
			}
			else
			{
				g_Renderer->GetMaterial(m_BrakeLightMatID)->constEmissive = m_InitialBrakeLightMatEmissive;
			}

			if (bReversing)
			{
				g_Renderer->GetMaterial(m_ReverseLightMatID)->constEmissive = m_ActiveReverseLightMatEmissive;
			}
			else
			{
				g_Renderer->GetMaterial(m_ReverseLightMatID)->constEmissive = m_InitialReverseLightMatEmissive;
			}
		}

		BaseScene* scene = g_SceneManager->CurrentScene();
		GameObject* brakeLightL = scene->GetGameObject(m_BrakeLightIDs[0]);
		GameObject* brakeLightR = scene->GetGameObject(m_BrakeLightIDs[1]);
		if (brakeLightL != nullptr && brakeLightR != nullptr &&
			brakeLightL->GetTypeID() == SpotLightSID &&
			brakeLightR->GetTypeID() == SpotLightSID)
		{
			((SpotLight*)brakeLightL)->SetVisible(bBraking, true);
			((SpotLight*)brakeLightR)->SetVisible(bBraking, true);
		}

		m_Steering *= glm::clamp(steeringSlowScale, 0.0f, 1.0f);
		m_EngineForce *= glm::clamp(engineForceSlowScale, 0.0f, 1.0f);
		if (abs(m_EngineForce) < 0.1f)
		{
			m_EngineForce = 0.0f;
		}

		if (m_bFlippingRightSideUp)
		{
			m_Transform.SetWorldRotation(glm::slerp(m_Transform.GetWorldRotation(), m_TargetRot, glm::clamp(g_DeltaTime * UPRIGHTING_SPEED, 0.0f, 1.0f)));
			real uprightedness = glm::dot(m_Transform.GetUp(), VEC3_UP);
			m_SecFlipppingRightSideUp += g_DeltaTime;
			bool bRightSideUp = uprightedness > 0.9f;
			if (bRightSideUp || m_SecFlipppingRightSideUp > MAX_FLIPPING_UPRIGHT_TIME)
			{
				m_SecFlipppingRightSideUp = 0.0f;
				m_bFlippingRightSideUp = false;
			}
		}
		else
		{
			// Check if flipped upside down and stuck
			glm::vec3 chassisUp = m_Transform.GetUp();
			real uprightedness = glm::dot(chassisUp, VEC3_UP);
			// TODO: Also check num wheels on ground
			bool bUpsideDown = uprightedness < 0.25f;
			if (bUpsideDown)
			{
				m_SecUpsideDown += g_DeltaTime;

				if (m_SecUpsideDown > SEC_UPSIDE_DOWN_BEFORE_FLIP)
				{
					glm::vec3 chassisForward = m_Transform.GetForward();
					glm::vec3 chassisRight = m_Transform.GetRight();

					Print("Flip!\n");
					m_bFlippingRightSideUp = true;
					real w = sqrt(1.0f + uprightedness);

					if (abs(uprightedness) < 0.99f)
					{
						if (abs(dot(chassisForward, VEC3_UP)) > abs(dot(chassisRight, VEC3_UP)))
						{
							m_TargetRot = glm::normalize(glm::quat(chassisRight.x, chassisRight.y, chassisRight.z, w));
						}
						else
						{
							m_TargetRot = glm::normalize(glm::quat(chassisForward.x, chassisForward.y, chassisForward.z, w));
						}
					}
					else
					{
						// Vectors are nearly parallel, just pick the forward axis to rotate about
						m_TargetRot = glm::quat(chassisForward.x, chassisForward.y, chassisForward.z, w);
					}
					m_SecFlipppingRightSideUp = 0.0f;
					m_SecUpsideDown = 0.0f;
					m_Transform.SetWorldPosition(m_Transform.GetWorldPosition() + VEC3_UP * 3.0f);
				}
			}
			else
			{
				m_SecUpsideDown = 0.0f;
			}
		}

		// TODO: Check in player class? Store min level in scene
		// Check if fell below the level
		if (m_Transform.GetWorldPosition().y < -100.0f)
		{
			ResetTransform();
		}

		real maxWheelSlip = 1.0f;

		for (i32 i = 0; i < m_TireCount; i++)
		{
			//synchronize the wheels with the (interpolated) chassis world transform
			m_Vehicle->updateWheelTransform(i, true);

			const btWheelInfo& wheelInfo = m_Vehicle->getWheelInfo(i);
			glm::quat newWheelRotation = ToQuaternion(wheelInfo.m_worldTransform.getRotation());
			scene->GetGameObject(m_TireIDs[i])->GetTransform()->SetWorldRotation(newWheelRotation);

			maxWheelSlip = glm::min(maxWheelSlip, wheelInfo.m_skidInfo);
		}

		m_WheelSlipHisto.AddValue(maxWheelSlip);

		{
			i32 wheelIndex = 2;
			m_Vehicle->applyEngineForce(m_EngineForce, wheelIndex);
			m_Vehicle->setBrake(m_BrakeForce, wheelIndex);
			wheelIndex = 3;
			m_Vehicle->applyEngineForce(m_EngineForce, wheelIndex);
			m_Vehicle->setBrake(m_BrakeForce, wheelIndex);

			wheelIndex = 0;
			m_Vehicle->setSteeringValue(m_Steering, wheelIndex);
			wheelIndex = 1;
			m_Vehicle->setSteeringValue(m_Steering, wheelIndex);
		}

		real motorPitch = glm::clamp(forwardVel / 35.0f + 0.75f, 0.95f, 1.5f);
		real motorGain = glm::clamp(m_EngineForce / m_MaxEngineForce * 3.0f, 0.0f, 1.0f);

		motorGain = motorGain + glm::clamp(forwardVel / 35.0f, 0.0f, 1.0f - motorGain);

		real brakeGainMultiplier = slowingVelocity;
		brakeGainMultiplier *= m_WheelSlipHisto.currentAverage * m_WheelSlipHisto.currentAverage;

		if (motorGain < 0.1f)
		{
			m_SoundEffects[(u32)SoundEffect::ENGINE].FadeOut();
		}
		else
		{
			m_SoundEffects[(u32)SoundEffect::ENGINE].FadeIn();
		}

		if (abs(maxWheelSlip) > m_WheelSlipScreechThreshold)
		{
			m_SoundEffects[(u32)SoundEffect::BRAKE].FadeOut();
		}
		else
		{
			m_SoundEffects[(u32)SoundEffect::BRAKE].FadeIn();
		}

		m_SoundEffects[(u32)SoundEffect::BRAKE].SetGainMultiplier(brakeGainMultiplier);

		for (u32 i = 0; i < (u32)SoundEffect::_COUNT; ++i)
		{
			m_SoundEffects[i].Update();
		}

		m_SoundEffects[(u32)SoundEffect::ENGINE].SetPitch(motorPitch);
		m_SoundEffects[(u32)SoundEffect::ENGINE].SetGainMultiplier(motorGain);

		m_pLinearVelocity = linearVel;
	}

	void Vehicle::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject vehicleObj;
		if (parentObject.TryGetObject("vehicle", vehicleObj))
		{
			// If this object was copied from a prefab, it will already have filled out IDs
			if (!m_TireIDs[0].IsValid())
			{
				std::vector<JSONField> tireIDs;
				if (vehicleObj.TryGetFieldArray("tire ids", tireIDs))
				{
					if (m_TireCount == (i32)tireIDs.size())
					{
						for (i32 i = 0; i < m_TireCount; ++i)
						{
							m_TireIDs[i] = GameObjectID::FromString(tireIDs[i].value.AsString());
						}
					}
					else
					{
						PrintError("Failed to parse tire IDs, incorrect number present (%i found, %i expected)", (i32)tireIDs.size(), m_TireCount);
					}
				}
			}

			if (!m_BrakeLightIDs[0].IsValid())
			{
				std::vector<JSONField> brakeLightIDs;
				if (vehicleObj.TryGetFieldArray("brake light ids", brakeLightIDs))
				{
					if ((i32)brakeLightIDs.size() == 2)
					{
						for (i32 i = 0; i < 2; ++i)
						{
							m_BrakeLightIDs[i] = GameObjectID::FromString(brakeLightIDs[i].value.AsString());
						}
					}
					else
					{
						PrintError("Failed to parse brake light IDs, incorrect number present (%i found, %i expected)", (i32)brakeLightIDs.size(), 2);
					}
				}
			}

			if (m_SoundEffectSIDs[0] != InvalidStringID)
			{
				std::vector<JSONField> soundEffectSIDs;
				if (vehicleObj.TryGetFieldArray("sound effect sids", soundEffectSIDs))
				{
					i32 count = std::min((i32)SoundEffect::_COUNT, (i32)soundEffectSIDs.size());
					for (i32 i = 0; i < count; ++i)
					{
						m_SoundEffectSIDs[i] = soundEffectSIDs[i].value.AsULong();
					}
				}
			}
		}
	}

	void Vehicle::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		JSONObject vehicleObj = {};

		// Prefab instances don't serialize their children (they're recreated with new IDs on load),
		// and the IDs get fixed up from the prefab template (see FixupPrefabTemplateIDs)
		bool bSerializeChildIDs = !m_SourcePrefabID.IsValid() || bSerializePrefabData;
		if (bSerializeChildIDs)
		{
			std::vector<JSONField> tireIDs;
			tireIDs.reserve(m_TireCount);
			for (GameObjectID& TireID : m_TireIDs)
			{
				tireIDs.emplace_back("", JSONValue(TireID.ToString()));
			}

			vehicleObj.fields.emplace_back("tire ids", JSONValue(tireIDs));

			std::vector<JSONField> brakeLightIDs;
			brakeLightIDs.reserve(2);
			for (GameObjectID& BrakeLightID : m_BrakeLightIDs)
			{
				brakeLightIDs.emplace_back("", JSONValue(BrakeLightID.ToString()));
			}

			vehicleObj.fields.emplace_back("brake light ids", JSONValue(brakeLightIDs));
		}

		std::vector<JSONField> soundEffectSIDs;
		soundEffectSIDs.reserve(m_SoundEffectSIDs.size());
		for (StringID SoundEffectSID : m_SoundEffectSIDs)
		{
			soundEffectSIDs.emplace_back("", JSONValue(SoundEffectSID));
		}
		vehicleObj.fields.emplace_back("sound effect sids", JSONValue(soundEffectSIDs));

		parentObject.fields.emplace_back("vehicle", JSONValue(vehicleObj));
	}

	void Vehicle::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		BaseScene* scene = g_SceneManager->CurrentScene();

		for (i32 i = 0; i < m_TireCount; ++i)
		{
			char buf[32];
			memcpy(buf, TireNames[i], strlen(TireNames[i]));
			buf[strlen(TireNames[i])] = ':';
			buf[strlen(TireNames[i]) + 1] = 0;
			scene->DrawImGuiGameObjectIDField(buf, m_TireIDs[i]);
		}

		for (i32 i = 0; i < 2; ++i)
		{
			char buf[32];
			memcpy(buf, BrakeLightNames[i], strlen(BrakeLightNames[i]));
			buf[strlen(BrakeLightNames[i])] = ':';
			buf[strlen(BrakeLightNames[i]) + 1] = 0;
			scene->DrawImGuiGameObjectIDField(buf, m_BrakeLightIDs[i]);
		}

		//AudioManager::AudioFileNameSIDField("engine", )


		ImGui::SliderFloat("Max engine force", &m_MaxEngineForce, 0.0f, 20000.0f);
		ImGui::SliderFloat("Engine accel", &m_EngineAccel, 0.0f, 30000.0f);
		ImGui::Text("Engine force: %.2f", m_EngineForce);
		ImGui::SliderFloat("Max brake force", &m_MaxBrakeForce, 0.0f, 200.0f);
		ImGui::Text("Braking force: %.2f", m_BrakeForce);
		ImGui::SliderFloat("Turn acceleration", &m_TurnAccel, 0.0f, 5.0f);
		ImGui::SliderFloat("Deceleration speed", &m_EngineForceDecelerationSpeed, 0.0f, 30.0f);
		ImGui::Text("Steering: %.2f", m_Steering);
		ImGui::SliderFloat("Screech threshold", &m_WheelSlipScreechThreshold, 0.0f, 1.0f);
		if (m_bFlippingRightSideUp)
		{
			ImGui::ProgressBar(m_SecFlipppingRightSideUp / MAX_FLIPPING_UPRIGHT_TIME, ImVec2(-1, 0), "Flipping right side up");
		}
		else
		{
			ImGui::ProgressBar(m_SecUpsideDown / SEC_UPSIDE_DOWN_BEFORE_FLIP, ImVec2(-1, 0), "Sec upside down");
		}

		// Tires
		if (m_Vehicle != nullptr)
		{
			real tireWidth = 20.0f;
			real tireHeight = 45.0f;
			real suspWidth = 8.0f;
			real suspHeight = 16.0f;
			ImU32 tireColGrounded = IM_COL32(250, 200, 200, 255);
			ImU32 tireColInAir = IM_COL32(200, 250, 200, 255);
			ImVec2 cursor = ImGui::GetCursorScreenPos();
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			real spacingX = 50.0f;
			real spacingY = 110.0f;
			real lowestWheelSlip = 1.0f;
			for (i32 i = 0; i < m_TireCount; i++)
			{
				// Assume this is called in update loop:
				m_Vehicle->updateWheelTransform(i, true);

				const btWheelInfo& wheelInfo = m_Vehicle->getWheelInfo(i);
				glm::quat newWheelRotation = ToQuaternion(wheelInfo.m_worldTransform.getRotation());
				GameObject* tireObject = scene->GetGameObject(m_TireIDs[i]);
				Transform* tireTransform = tireObject->GetTransform();
				tireTransform->SetWorldRotation(newWheelRotation);

				real x = (real)(i % 2);
				real y = (real)(i / 2);
				ImVec2 rectStart = cursor + ImVec2(20.0f + x * spacingX, y * spacingY);
				// NOTE: TODO: wheelInfo.m_raycastInfo.m_isInContact isn't returning true ever...
				drawList->AddRect(rectStart, rectStart + ImVec2(tireWidth, tireHeight), wheelInfo.m_raycastInfo.m_isInContact ? tireColGrounded : tireColInAir);
				rectStart += ImVec2(x == 0 ? -10.0f - suspWidth : tireWidth + 10.0f, 0.0f);
				drawList->AddRect(rectStart, rectStart + ImVec2(suspWidth, suspHeight), tireColGrounded);
				real suspensionLen = wheelInfo.m_raycastInfo.m_suspensionLength;
				rectStart += ImVec2(1.0f, 1.0f);
				drawList->AddRectFilled(rectStart, rectStart + ImVec2(suspWidth - 2.0f, suspensionLen * (suspHeight - 2.0f)), tireColInAir);

				lowestWheelSlip = glm::min(lowestWheelSlip, wheelInfo.m_skidInfo);
			}
			ImGui::SetCursorScreenPos(cursor + ImVec2(0, spacingY + tireHeight + +20.0f));
			ImGui::Text("Lowest wheel slip: %.2f", lowestWheelSlip);
		}

		ImGui::SliderFloat("Wheel friction", &m_WheelFriction, 0.0f, 10.0f);
		ImGui::SliderFloat("Roll influence", &m_RollInfluence, 0.0f, 1.0f);
		ImGui::SliderFloat("Wheel radius", &m_WheelRadius, 0.0f, 2.0f);
		ImGui::SliderFloat("Wheel width", &m_WheelWidth, 0.0f, 2.0f);

		if (ImGui::Button("Recreate rigid body"))
		{
			if (m_RigidBody != nullptr)
			{
				btDiscreteDynamicsWorld* dynamicsWorld = scene->GetPhysicsWorld()->GetWorld();
				dynamicsWorld->removeVehicle(m_Vehicle);
				m_Vehicle = nullptr;
			}

			CreateRigidBody();
		}

		ImGui::Spacing();

		ImGui::SliderFloat("Sus. stiffness", &m_tuning.m_suspensionStiffness, 0.0f, 10.0f);
		ImGui::SliderFloat("Sus. compression", &m_tuning.m_suspensionCompression, 0.0f, 5.0f);
		ImGui::SliderFloat("Sus. damping", &m_tuning.m_suspensionDamping, 0.0f, 1.0f);
		ImGui::SliderFloat("Sus. max travel cm", &m_tuning.m_maxSuspensionTravelCm, 0.0f, 1000.0f);
		ImGui::SliderFloat("Friction slip", &m_tuning.m_frictionSlip, 0.0f, 50.0f);
		ImGui::SliderFloat("Sus. max force", &m_tuning.m_maxSuspensionForce, 0.0f, 10'000.0f);


		if (ImGui::BeginChild("Sound clips", ImVec2(300, 300), true))
		{
			for (i32 i = 0; i < (i32)SoundEffect::_COUNT; ++i)
			{
				if (m_SoundEffects[i].DrawImGui())
				{
					SetSoundEffectSID((SoundEffect)i, m_SoundEffects[i].loopSID);
					m_SoundEffectSIDs[i] = m_SoundEffects[i].loopSID;
				}
			}
		}
		ImGui::EndChild();
	}

	void Vehicle::OnPlayerEnter()
	{
		m_bOccupied = true;
	}

	void Vehicle::OnPlayerExit()
	{
		// Player has left the vehicle, stop accelerating & braking
		m_EngineForce = 0.0f;
		m_BrakeForce = 0.0f;
		m_bOccupied = false;
	}

	void Vehicle::MatchCorrespondingID(const GameObjectID& existingID, GameObject* newGameObject, const char* objectName, GameObjectID& outCorrespondingID)
	{
		ChildIndex childIndex = GetChildIndexWithID(existingID);
		bool bSuccess = childIndex.IsValid();

		if (bSuccess)
		{
			GameObjectID correspondingID = newGameObject->GetIDAtChildIndex(childIndex);
			bSuccess = (correspondingID != InvalidGameObjectID);

			if (bSuccess)
			{
				outCorrespondingID = correspondingID;
			}
		}

		if (!bSuccess)
		{
			std::string idStr = existingID.ToString();
			PrintError("Failed to find corresponding game object ID for child %s (%s)\n", objectName, idStr.c_str());
		}
	}

	void Vehicle::SetSoundEffectSID(SoundEffect soundEffect, StringID soundSID)
	{
		m_SoundEffects[(i32)soundEffect] = SoundClip_LoopingSimple(VehicleSoundEffectNames[(i32)soundEffect], soundSID, false);
		if (soundSID != InvalidStringID && soundEffect == SoundEffect::ENGINE)
		{
			// TODO: Save this metadata somewhere in a file
			AudioManager::SetSourceLooping(m_SoundEffects[(i32)soundEffect].loop, true);
		}
	}

	void Vehicle::ResetTransform()
	{
		m_RigidBody->GetRigidBodyInternal()->setAngularVelocity(btVector3(0.0f, 0.0f, 0.0f));
		m_RigidBody->GetRigidBodyInternal()->setLinearVelocity(btVector3(0.0f, 0.0f, 0.0f));
		m_Transform.CloneFrom(Transform::Identity);
		m_Transform.Translate(0.0f, 2.0f, 0.0f);
		m_SecUpsideDown = 0.0f;
		m_bFlippingRightSideUp = false;
		m_SecFlipppingRightSideUp = 0.0f;
		m_EngineForce = 0.0f;
		m_BrakeForce = 0.0f;
		m_Steering = 0.0f;
	}

	void Vehicle::FixupPrefabTemplateIDs(GameObject* newGameObject)
	{
		Vehicle* newVehicle = ((Vehicle*)newGameObject);

		for (i32 i = 0; i < m_TireCount; ++i)
		{
			MatchCorrespondingID(m_TireIDs[i], newGameObject, TireNames[i], newVehicle->m_TireIDs[i]);
		}

		for (i32 i = 0; i < 2; ++i)
		{
			MatchCorrespondingID(m_BrakeLightIDs[i], newGameObject, BrakeLightNames[i], newVehicle->m_BrakeLightIDs[i]);
		}
	}

	bool RoadSegment::Overlaps(const glm::vec2& point)
	{
		VertexBufferData* vertexBufferData = mesh->GetVertexBufferData();
		u32* indexBuffer = mesh->GetIndexBufferUnsafePtr();
		u32 indexCount = mesh->GetIndexCount();
		u8* vertexData = (u8*)vertexBufferData->vertexData;
		u32 vertexStride = vertexBufferData->VertexStride;

		for (u32 i = 0; i < indexCount - 2; i += 3)
		{
			glm::vec3 tri0(*(glm::vec3*)(vertexData + vertexStride * indexBuffer[i + 0]));
			glm::vec3 tri1(*(glm::vec3*)(vertexData + vertexStride * indexBuffer[i + 1]));
			glm::vec3 tri2(*(glm::vec3*)(vertexData + vertexStride * indexBuffer[i + 2]));

			if (PointOverlapsTriangle(point, glm::vec2(tri0.x, tri0.z), glm::vec2(tri1.x, tri1.z), glm::vec2(tri2.x, tri2.z)))
			{
				return true;
			}
		}

		return false;
	}

	real RoadSegment::SignedDistanceTo(const glm::vec3& point, glm::vec3& outClosestPoint)
	{
		VertexBufferData* vertexBufferData = mesh->GetVertexBufferData();
		u32* indexBuffer = mesh->GetIndexBufferUnsafePtr();
		u32 indexCount = mesh->GetIndexCount();
		u8* vertexData = (u8*)vertexBufferData->vertexData;
		u32 vertexStride = vertexBufferData->VertexStride;

		glm::vec3 closestPoint;
		real dist = 99999.0f;
		for (u32 i = 0; i < indexCount - 2; i += 3)
		{
			glm::vec3 a(*(glm::vec3*)(vertexData + vertexStride * indexBuffer[i + 0]));
			glm::vec3 b(*(glm::vec3*)(vertexData + vertexStride * indexBuffer[i + 1]));
			glm::vec3 c(*(glm::vec3*)(vertexData + vertexStride * indexBuffer[i + 2]));

			real d = SignedDistanceToTriangle(point, a, b, c, closestPoint);
			if (d < dist)
			{
				dist = d;
				outClosestPoint = closestPoint;
			}
		}

		return dist;
	}

	void RoadSegment::ComputeAABB()
	{
		aabb.minX = aabb.minY = aabb.minZ = 99999.0f;
		aabb.maxX = aabb.maxY = aabb.maxZ = -99999.0f;
		const u32 sampleCount = 10;
		real maxWidth = glm::max(widthStart, widthEnd);
		for (u32 i = 0; i < sampleCount; ++i)
		{
			real t = ((real)i / (sampleCount - 1));
			glm::vec3 point = curve.GetPointOnCurve(t);
			aabb.minX = glm::min(aabb.minX, point.x - maxWidth);
			aabb.minY = glm::min(aabb.minY, point.y - maxWidth);
			aabb.minZ = glm::min(aabb.minZ, point.z - maxWidth);
			aabb.maxX = glm::max(aabb.maxX, point.x + maxWidth);
			aabb.maxY = glm::max(aabb.maxY, point.y + maxWidth);
			aabb.maxZ = glm::max(aabb.maxZ, point.z + maxWidth);
		}
	}

	Road::Road(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, RoadSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		AddTag("road");
	}

	PropertyCollection* Road::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();
		return collection;
	}

	void Road::Initialize()
	{
		GameObject::Initialize();

		m_Mesh = new Mesh(this);
		m_Mesh->SetTypeToMemory();

		m_QuadCountPerSegment = 25;

		GetSystem<RoadManager>(SystemType::ROAD_MANAGER)->RegisterRoad(this);
	}

	void Road::PostInitialize()
	{
		GenerateMesh();
	}

	void Road::Destroy(bool bDetachFromParent /* = true */)
	{
		GetSystem<RoadManager>(SystemType::ROAD_MANAGER)->DeregisterRoad(this);

		GameObject::Destroy(bDetachFromParent);

		// Mesh components will have already been destroyed by Mesh Destroy
		roadSegments.clear();

		for (u32 i = 0; i < (u32)m_RigidBodies.size(); ++i)
		{
			m_RigidBodies[i]->Destroy();
			delete m_RigidBodies[i];
			delete m_MeshVertexArrays[i];
		}
		m_RigidBodies.clear();
		m_MeshVertexArrays.clear();
	}

	void Road::Update()
	{
		PROFILE_AUTO("Road Update");

		GameObject::Update();

		const PhysicsDebuggingSettings& debuggingSettings = g_Renderer->GetPhysicsDebuggingSettings();
		if (!debuggingSettings.bDisableAll &&
			g_Renderer->GetDebugRenderer()->getDebugMode() != btIDebugDraw::DebugDrawModes::DBG_NoDebug)
		{
			for (u32 i = 0; i < (u32)roadSegments.size(); ++i)
			{
				RoadSegment& segment = roadSegments[i];

				// TOOD: move curve segment by our transform/offset it
				segment.curve.DrawDebug(false, btVector4(0.0f, 1.0f, 0.0f, 1.0f), btVector4(1.0f, 1.0f, 1.0f, 1.0f));

				if (debuggingSettings.bDrawAabb)
				{
					segment.aabb.DrawDebug(btVector3(1.0f, 0.5f, 0.0f));
				}
			}
		}
	}

	void Road::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		if (ImGuiExt::DragUInt("Quad count per segment", &m_QuadCountPerSegment, 0.1f, 1u, 32u))
		{
			for (u32 i = 0; i < (u32)roadSegments.size(); ++i)
			{
				GenerateSegment(i);
			}
		}
	}

	void Road::GenerateSegmentsThroughPoints(const std::vector<glm::vec3>& points)
	{
		const i32 startingCurveCount = (i32)roadSegments.size();

		roadSegments.reserve(roadSegments.size() + points.size());

		// First pass: set control points without yet setting handle points
		{
			RoadSegment segment = {};
			segment.widthStart = m_MaxWidth;
			segment.widthEnd = m_MaxWidth;
			for (u32 i = 1; i < (u32)points.size(); ++i)
			{
				segment.curve = BezierCurve3D(points[i - 1], VEC3_ZERO, VEC3_ZERO, points[i]);
				roadSegments.push_back(segment);
			}
		}

		// Second pass: generate tangents so segments are continuous
		{
			real tangentScale = 0.25f;

			for (u32 i = 1; i < (u32)points.size() - 1; ++i)
			{
				i32 prevPointIndex = i - 1;
				RoadSegment& prevSeg = roadSegments[startingCurveCount + prevPointIndex];
				RoadSegment& currSeg = roadSegments[startingCurveCount + i];

				glm::vec3 pointDiff = (currSeg.curve.points[3] - prevSeg.curve.points[0]) * tangentScale;

				prevSeg.curve.points[2] = prevSeg.curve.points[3] - pointDiff;
				currSeg.curve.points[1] = currSeg.curve.points[0] + pointDiff;
			}

			// Set final one (or two if looping) tangent point(s)
			i32 finalSegmentIndex = (i32)(startingCurveCount + points.size() - 2);
			if (NearlyEquals(roadSegments[finalSegmentIndex].curve.points[3], roadSegments[0].curve.points[0], 0.1f))
			{
				// Loop
				glm::vec3 tangent = (roadSegments[0].curve.points[3] - roadSegments[finalSegmentIndex].curve.points[0]) * tangentScale;
				roadSegments[0].curve.points[1] = roadSegments[0].curve.points[0] + tangent;
				roadSegments[finalSegmentIndex].curve.points[2] = roadSegments[finalSegmentIndex].curve.points[3] - tangent;
			}
			else
			{
				roadSegments[finalSegmentIndex].curve.points[2] = roadSegments[finalSegmentIndex].curve.points[3] - roadSegments[finalSegmentIndex].curve.points[0];
			}
		}

		// Third pass: actually generate meshes
		for (u32 i = 0; i < (u32)points.size() - 1; ++i)
		{
			GenerateSegment(startingCurveCount + i);
		}
	}

	void Road::GenerateSegment(i32 index)
	{
		if (index >= (i32)roadSegments.size())
		{
			PrintError("Attempted to generate road segment without curve defined!\n");
			return;
		}

		PROFILE_AUTO("Generate road segment");

		if (index < (i32)roadSegments.size() && roadSegments[index].mesh != nullptr)
		{
			roadSegments[index].mesh->Destroy();
			roadSegments[index].mesh = nullptr;
		}

		if (index >= (i32)roadSegments.size())
		{
			roadSegments.resize(index + 1);
		}

		GenerateMaterial();

		Material* roadMaterial = g_Renderer->GetMaterial(m_RoadMaterialID);
		Shader* roadShader = g_Renderer->GetShader(roadMaterial->shaderID);

		RoadSegment& segment = roadSegments[index];
		BezierCurve3D& curve = segment.curve;

		segment.ComputeAABB();

		//if (roadSegments.size() >= terrainShader->maxObjectCount - 1)
		//{
		//	terrainShader->maxObjectCount = (u32)(roadSegments.size() + 1);
		//	g_Renderer->SetStaticGeometryBufferDirty(terrainShader->staticVertexBufferIndex);
		//}

		const u32 vertexCount = m_QuadCountPerSegment * 2 + 2;
		const u32 triCount = m_QuadCountPerSegment * 2;
		const u32 indexCount = triCount * 3;

		VertexBufferDataCreateInfo vertexBufferCreateInfo = {};
		vertexBufferCreateInfo.positions_3D.clear();
		vertexBufferCreateInfo.texCoords_UV.clear();
		vertexBufferCreateInfo.colours_R32G32B32A32.clear();
		vertexBufferCreateInfo.normals.clear();
		vertexBufferCreateInfo.tangents.clear();

		vertexBufferCreateInfo.attributes = roadShader->vertexAttributes;
		vertexBufferCreateInfo.positions_3D.reserve(vertexCount);
		vertexBufferCreateInfo.texCoords_UV.reserve(vertexCount);
		vertexBufferCreateInfo.colours_R32G32B32A32.reserve(vertexCount);
		vertexBufferCreateInfo.normals.reserve(vertexCount);
		vertexBufferCreateInfo.tangents.reserve(vertexCount);

		std::vector<u32> indices(indexCount);

		for (u32 z = 0; z < m_QuadCountPerSegment + 1; ++z)
		{
			real t = (real)z / m_QuadCountPerSegment;
			real width = Lerp(segment.widthStart, segment.widthEnd, t);
			glm::vec3 pos = curve.GetPointOnCurve(t);

			glm::vec3 posF = curve.GetPointOnCurveUnclamped(t + 0.01f);
			glm::vec3 posB = curve.GetPointOnCurveUnclamped(t - 0.01f);
			glm::vec3 normal = glm::normalize(glm::vec3(posF.x - posB.x, 2.0f, posF.z - posB.z));

			glm::vec3 forward = glm::normalize(curve.GetFirstDerivativeOnCurve(t));
			glm::vec3 tangent = glm::cross(forward, normal);

			real xScales[] = { -width, width };
			for (u32 x = 0; x < 2; ++x)
			{
				glm::vec3 vertPosWS = pos + xScales[x] * tangent;
				glm::vec2 uv((real)x, t);

				vertexBufferCreateInfo.positions_3D.emplace_back(vertPosWS);
				vertexBufferCreateInfo.texCoords_UV.emplace_back(uv);
				vertexBufferCreateInfo.colours_R32G32B32A32.emplace_back(VEC4_ONE);
				vertexBufferCreateInfo.normals.emplace_back(normal);
				vertexBufferCreateInfo.tangents.emplace_back(tangent);
			}
		}

		i32 i = 0;
		for (u32 z = 0; z < m_QuadCountPerSegment; ++z)
		{
			u32 vertIdx = z * 6;
			indices[vertIdx + 0] = i + 0;
			indices[vertIdx + 1] = i + 1;
			indices[vertIdx + 2] = i + 3;

			indices[vertIdx + 3] = i + 0;
			indices[vertIdx + 4] = i + 3;
			indices[vertIdx + 5] = i + 2;

			i += 2;
		}

		RenderObjectCreateInfo renderObjectCreateInfo = {};
		MeshComponent* meshComponent = MeshComponent::LoadFromMemory(m_Mesh, vertexBufferCreateInfo, indices, m_RoadMaterialID, &renderObjectCreateInfo);
		if (meshComponent != nullptr)
		{
			CHECK_EQ(roadSegments[index].mesh, nullptr);
			roadSegments[index].mesh = meshComponent;
		}

		CreateRigidBody(index);
	}

	GameObject* Road::CopySelf(GameObject* parent, CopyFlags copyFlags, std::string* optionalName, const GameObjectID& optionalGameObjectID)
	{
		FLEX_UNUSED(copyFlags);

		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);
		Road* newGameObject = new Road(newObjectName, newGameObjectID);

		CopyGenericFields(newGameObject, parent, CopyFlags::ADD_TO_SCENE);

		newGameObject->roadSegments = roadSegments;
		newGameObject->m_RoadMaterialID = m_RoadMaterialID;
		newGameObject->m_QuadCountPerSegment = m_QuadCountPerSegment;

		return newGameObject;
	}

	void Road::RegenerateMesh()
	{
		m_Mesh->DestroyAllSubmeshes();

		roadSegments.clear();
		GenerateMesh();
	}

	void Road::GenerateMesh()
	{
		if (roadSegments.empty())
		{
			TerrainGenerator* terrainGenerator = (TerrainGenerator*)m_TerrainGameObjectID.Get();
			if (terrainGenerator == nullptr)
			{
				m_TerrainGameObjectID = g_SceneManager->CurrentScene()->FirstObjectWithTag("terrain");
				terrainGenerator = (TerrainGenerator*)m_TerrainGameObjectID.Get();
			}

			std::vector<glm::vec3> newPoints =
			{
				glm::vec3(4.0f, 0.0f, 0.0f),
				glm::vec3(9.0f, 0.0f, 64.0f),
				glm::vec3(60.0f, 0.0f, 84.0f),
				glm::vec3(96.0f, 0.0f, 72.0f),
				glm::vec3(104.0f, 0.0f, 32.0f),
				glm::vec3(64.0f, 0.0f, -24.0f),
				glm::vec3(4.0f, 0.0f, 0.0f)
			};

			if (terrainGenerator != nullptr)
			{
				real offset = 0.5f;

				for (u32 i = 0; i < (u32)newPoints.size(); ++i)
				{
					newPoints[i].y = terrainGenerator->SampleHeight(glm::vec2(newPoints[i].x, newPoints[i].z)) + offset;
				}
			}

			GenerateSegmentsThroughPoints(newPoints);

			if (terrainGenerator != nullptr)
			{
				terrainGenerator->UpdateRoadSegments();
			}
		}
		else
		{
			for (u32 i = 0; i < (u32)roadSegments.size(); ++i)
			{
				GenerateSegment(i);
			}
		}
	}

	void Road::GenerateMaterial()
	{
		m_RoadMaterialID = g_Renderer->GetMaterialID("Road surface");
		if (m_RoadMaterialID == InvalidMaterialID)
		{
			// TODO: Add serialized member which points at material ID once support for that is in
			MaterialCreateInfo matCreateInfo = {};
			matCreateInfo.name = "Road surface";
			matCreateInfo.shaderName = "pbr";
			matCreateInfo.bDynamic = false;
			matCreateInfo.constRoughness = 0.95f;
			matCreateInfo.constMetallic = 0.0f;
			matCreateInfo.constAlbedo = glm::vec4(0.25f, 0.25f, 0.28f, 1.0f);
			matCreateInfo.albedoTexturePath = TEXTURE_DIRECTORY "road-albedo.png";
			matCreateInfo.enableAlbedoSampler = true;
			matCreateInfo.bSerializable = false;

			m_RoadMaterialID = g_Renderer->InitializeMaterial(&matCreateInfo, m_RoadMaterialID);
		}
	}

	void Road::CreateRigidBody(u32 meshIndex)
	{
		if (roadSegments.empty())
		{
			return;
		}

		if (meshIndex < (u32)m_RigidBodies.size() && m_RigidBodies[meshIndex] != nullptr)
		{
			m_RigidBodies[meshIndex]->Destroy();
			delete m_RigidBodies[meshIndex];
			m_RigidBodies[meshIndex] = nullptr;
		}

		if (meshIndex >= (u32)m_RigidBodies.size())
		{
			m_RigidBodies.resize(meshIndex + 1, nullptr);
			m_MeshVertexArrays.resize(meshIndex + 1, nullptr);
		}

		MeshComponent* submesh = roadSegments[meshIndex].mesh;

		btBvhTriangleMeshShape* shape;
		submesh->CreateCollisionMesh(&m_MeshVertexArrays[meshIndex], &shape);

		// TODO: Don't even create rb?
		RigidBody* rigidBody = new RigidBody((u32)CollisionType::STATIC, (u32)CollisionType::DEFAULT & ~(u32)CollisionType::STATIC);
		rigidBody->SetStatic(true);
		rigidBody->Initialize(shape, &m_Transform);
		m_RigidBodies[meshIndex] = rigidBody;
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
