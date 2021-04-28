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

#include <Vehicles/Hinge2Vehicle.h>
#include <CommonInterfaces/CommonExampleInterface.h>
#include <CommonInterfaces/CommonGUIHelperInterface.h>

#include <LinearMath/btIDebugDraw.h>
#include <LinearMath/btTransform.h>

#define SSE_MATHFUN_WITH_CODE
#include "sse_mathfun.h"

#include <glm/gtx/norm.hpp> // for distance2
#include <glm/gtx/quaternion.hpp> // for rotate
#include <glm/gtx/vector_angle.hpp> // for angle

#include <imgui/imgui_internal.h> // For PushItemFlag

#include <random>
IGNORE_WARNINGS_POP

#include "Scene/GameObject.hpp"
#include "Cameras/CameraManager.hpp"
#include "Cameras/TerminalCamera.hpp"
#include "Editor.hpp"
#include "FlexEngine.hpp"
#include "Graphics/BitmapFont.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "JSONParser.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Platform/Platform.hpp"
#include "Player.hpp"
#include "Profiler.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/LoadedMesh.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "Systems/CartManager.hpp"
#include "Systems/TrackManager.hpp"
#include "Time.hpp"
#include "VirtualMachine/Backend/VirtualMachine.hpp"
#include "VirtualMachine/Diagnostics.hpp"
#include "Window/Window.hpp"

namespace flex
{
	const char* GameObject::s_DefaultNewGameObjectName = "New_Game_Object_00";
	AudioCue GameObject::s_SqueakySounds;
	AudioSourceID GameObject::s_BunkSound;

	const char* Cart::emptyCartMeshName = "cart-empty.glb";
	const char* EngineCart::engineMeshName = "cart-engine.glb";

	ms SoftBody::FIXED_UPDATE_TIMESTEP = 1000.0f / 120.0f;
	u32 SoftBody::MAX_UPDATE_COUNT = 20;

	const char* SpringObject::s_ExtendedMeshFilePath = MESH_DIRECTORY "spring-extended.glb";
	const char* SpringObject::s_ContractedMeshFilePath = MESH_DIRECTORY "spring-contracted.glb";
	MaterialID SpringObject::s_SpringMatID = InvalidMaterialID;
	MaterialID SpringObject::s_BobberMatID = InvalidMaterialID;

	ChildIndex InvalidChildIndex = ChildIndex({});

#define SIMD_WAVES 0

	// Wave generation globals accessed from threads
	static volatile u32 wave_workQueueEntriesCreated = 0;
	static volatile u32 wave_workQueueEntriesClaimed = 0;
	static volatile u32 wave_workQueueEntriesCompleted = 0;
	static ThreadSafeArray<GerstnerWave::WaveGenData>* wave_workQueue = nullptr;

	// Terrain generation globals accessed from threads
	static volatile u32 terrain_workQueueEntriesCreated = 0;
	static volatile u32 terrain_workQueueEntriesClaimed = 0;
	static volatile u32 terrain_workQueueEntriesCompleted = 0;
	static ThreadSafeArray<TerrainGenerator::TerrainChunkData>* terrain_workQueue = nullptr;

	static const char* TireNames[] = { "FL", "FR", "RL", "RR", "None" };
	static const char* BrakeLightNames[] = { "Left", "Right" };
	static_assert((ARRAY_LENGTH(TireNames) - 1) == (i32)Vehicle::Tire::_NONE, "TireNames length does not match number of entires in Tire enum");

	GameObject::GameObject(const std::string& name, StringID typeID, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		m_Name(name),
		m_TypeID(typeID)
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

		if (!s_SqueakySounds.IsInitialized())
		{
			s_SqueakySounds.Initialize(SFX_DIRECTORY "squeak00.wav", 5);

			s_BunkSound = AudioManager::AddAudioSource(SFX_DIRECTORY "bunk.wav");
		}
	}

	GameObject::~GameObject()
	{
	}

	GameObject* GameObject::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		if (parent != nullptr && (parent->GetTypeID() == SID("directional light")))
		{
			PrintError("Attempted to copy directional light\n");
			return nullptr;
		}

		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, newObjectName, newGameObjectID);
		GameObject* newGameObject = CreateObjectOfType(m_TypeID, newObjectName, newGameObjectID);

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	GameObject* GameObject::CreateObjectFromJSON(
		const JSONObject& obj,
		BaseScene* scene,
		i32 sceneFileVersion,
		bool bIsPrefabTemplate /* = false */,
		CopyFlags copyFlags /* = CopyFlags::ALL */)
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
				prefabTemplate = g_ResourceManager->GetPrefabTemplate(prefabName);
				prefabID = g_ResourceManager->GetPrefabID(prefabName);
			}

			if (prefabTemplate != nullptr)
			{
				JSONObject transformObj;
				Transform transform = Transform::Identity();
				if (obj.SetObjectChecked("transform", transformObj))
				{
					transform = Transform::ParseJSON(transformObj);
				}

				GameObject* newPrefabInstance = CreateObjectFromPrefabTemplate(prefabID, gameObjectID, &objectName, nullptr, &transform, copyFlags);

				std::vector<JSONObject> children;
				if (obj.SetObjectArrayChecked("children", children))
				{
					for (JSONObject& child : children)
					{
						newPrefabInstance->AddChildImmediate(GameObject::CreateObjectFromJSON(child, scene, sceneFileVersion, bIsPrefabTemplate, copyFlags));
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

		GameObject* newGameObject = CreateObjectOfType(gameObjectTypeID, objectName, gameObjectID, gameObjectTypeStr.c_str(), bIsPrefabTemplate);

		if (newGameObject != nullptr)
		{
			newGameObject->m_bIsTemplate = bIsPrefabTemplate;
			newGameObject->ParseJSON(obj, scene, sceneFileVersion, InvalidMaterialID, bIsPrefabTemplate, copyFlags);
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
			PrintError("Failed to create game objet from prefab template with ID %s\n", prefabTemplateIDStr.c_str());
			return nullptr;
		}
		GameObject* prefabInstance = prefabTemplate->CopySelf(parent, copyFlags, optionalObjectName, gameObjectID);

		prefabInstance->m_PrefabIDLoadedFrom = prefabID;

		if (optionalTransform != nullptr)
		{
			prefabInstance->m_Transform = *optionalTransform;
		}

		return prefabInstance;
	}

	GameObject* GameObject::CreateObjectOfType(
		StringID gameObjectTypeID,
		const std::string& objectName,
		const GameObjectID& gameObjectID /* = InvalidGameObjectID */,
		const char* optionalTypeStr /* = nullptr */,
		bool bIsPrefabTemplate /* = false */)
	{
		switch (gameObjectTypeID)
		{
		case SID("skybox"): return new Skybox(objectName, gameObjectID);
		case SID("reflection probe"): return new ReflectionProbe(objectName, gameObjectID);
		case SID("valve"): return new Valve(objectName, gameObjectID);
		case SID("rising block"): return new RisingBlock(objectName, gameObjectID);
		case SID("glass pane"): return new GlassPane(objectName, gameObjectID);
		case SID("point light"): return new PointLight(objectName, gameObjectID);
		case SID("spot light"): return new SpotLight(objectName, gameObjectID);
		case SID("area light"): return new AreaLight(objectName, gameObjectID);
		case SID("directional light"): return new DirectionalLight(objectName, gameObjectID);
		case SID("cart"): return GetSystem<CartManager>(SystemType::CART_MANAGER)->CreateCart(objectName, gameObjectID, bIsPrefabTemplate);
		case SID("engine cart"): return GetSystem<CartManager>(SystemType::CART_MANAGER)->CreateEngineCart(objectName, gameObjectID, bIsPrefabTemplate);
		case SID("mobile liquid box"): return new MobileLiquidBox(objectName, gameObjectID);
		case SID("terminal"): return new Terminal(objectName, gameObjectID);
		case SID("gerstner wave"): return new GerstnerWave(objectName, gameObjectID);
		case SID("blocks"): return new Blocks(objectName, gameObjectID);
		case SID("particle system"): return new ParticleSystem(objectName, gameObjectID);
		case SID("terrain generator"): return new TerrainGenerator(objectName, gameObjectID);
		case SID("wire"): return GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->AddWire(gameObjectID);
		case SID("socket"): return GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->AddSocket(objectName, gameObjectID);
		case SID("spring"): return new SpringObject(objectName, gameObjectID);
		case SID("soft body"): return new SoftBody(objectName, gameObjectID);
		case SID("vehicle"): return new Vehicle(objectName, gameObjectID);
		case SID("road"): return new Road(objectName, gameObjectID);
		case SID("object"): return new GameObject(objectName, gameObjectTypeID, gameObjectID);
		case SID("player"):
		{
			PrintError("Player was serialized to scene file!\n");
		} break;
		case InvalidStringID: // Fall through
		default:
		{
			PrintWarn("Unhandled game object type in CreateObjectOfType (Object name: %s, type: %s).\n"
				"Creating placeholder base object.\n",
				objectName.c_str(), (optionalTypeStr == nullptr ? "unknown" : optionalTypeStr));

			return new GameObject(objectName, gameObjectTypeID, gameObjectID);
		} break;
		}

		return nullptr;
	}

	void GameObject::Initialize()
	{
		if (m_RigidBody)
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

		m_Transform.UpdateParentTransform();

		GetChildrenOfType(SID("socket"), false, sockets);
		outputSignals.resize((u32)sockets.size());
	}

	void GameObject::Destroy(bool bDetachFromParent /* = true */)
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

		if (g_Editor->IsObjectSelected(ID))
		{
			g_Editor->DeselectObject(ID);
		}

		if (g_SceneManager->HasSceneLoaded())
		{
			g_SceneManager->CurrentScene()->UnregisterGameObject(ID);
		}
	}

	void GameObject::Update()
	{
		if (m_NearbyInteractable != nullptr)
		{
			btIDebugDraw* debugDrawer = g_Renderer->GetDebugDrawer();
			btVector3 pos = ToBtVec3(m_NearbyInteractable->GetTransform()->GetWorldPosition());
			//real pulse = sin(g_SecElapsedSinceProgramStart * 8.0f);
			//debugDrawer->drawSphere(pos, pulse * 0.1f + 0.35f, btVector3(0.1f, pulse * 0.5f + 0.7f, 0.1f));
			debugDrawer->drawLine(pos + btVector3(-1, 0.1f, 0), pos + btVector3(1, 0.1f, 0), btVector3(0.1f, 0.95f, 0.1f));
			debugDrawer->drawLine(pos + btVector3(0, 0.1f, -1), pos + btVector3(0, 0.1f, 1), btVector3(0.1f, 0.95f, 0.1f));
		}
		else if (m_ObjectInteractingWith != nullptr)
		{
			// TODO: Write real fancy-lookin outline shader instead of drawing a lil cross
			//btIDebugDraw* debugDrawer = g_Renderer->GetDebugDrawer();
			//btVector3 pos = ToBtVec3(m_Transform.GetWorldPosition());
			//debugDrawer->drawLine(pos + btVector3(-1, 0.1f, 0), pos + btVector3(1, 0.1f, 0), btVector3(0.95f, 0.1f, 0.1f));
			//debugDrawer->drawLine(pos + btVector3(0, 0.1f, -1), pos + btVector3(0, 0.1f, 1), btVector3(0.95f, 0.1f, 0.1f));
		}
		else if (m_bInteractable)
		{
			btIDebugDraw* debugDrawer = g_Renderer->GetDebugDrawer();
			btVector3 pos = ToBtVec3(m_Transform.GetWorldPosition());
			debugDrawer->drawLine(pos + btVector3(-1, 0.1f, 0), pos + btVector3(1, 0.1f, 0), btVector3(0.95f, 0.95f, 0.1f));
			debugDrawer->drawLine(pos + btVector3(0, 0.1f, -1), pos + btVector3(0, 0.1f, 1), btVector3(0.95f, 0.95f, 0.1f));
		}

		if (m_Name == "Spinner")
		{
			m_Transform.SetWorldRotation(glm::quat(glm::vec3(0.0f, g_SecElapsedSinceProgramStart, 0.0f)));
		}

		// Clear every frame
		m_NearbyInteractable = nullptr;

		if (m_RigidBody)
		{
			if (m_RigidBody->IsKinematic())
			{
				m_RigidBody->MatchParentTransform();
			}
			else
			{
				// Rigid body will take these changes into account
				m_RigidBody->UpdateParentTransform();
			}
		}

		for (GameObject* child : m_Children)
		{
			child->Update();
		}
	}

	void GameObject::DrawImGuiObjects()
	{
		if (!IsVisibleInSceneExplorer())
		{
			return;
		}

		ImGui::PushID(this);

		DrawImGuiForSelfInternal();

		ImGui::PopID();
	}

	void GameObject::DrawImGuiForSelfInternal()
	{
		// ImGui::PushID will have already been called, making names not need to be quailfied for uniqueness

		bool bAnyPropertyChanged = false;

		const char* typeStr = BaseScene::GameObjectTypeIDToString(m_TypeID);
		ImGui::Text("%s : %s %s", m_Name.c_str(), typeStr, m_PrefabIDLoadedFrom.IsValid() ? "(prefab)" : "");

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

			if (m_PrefabIDLoadedFrom.IsValid())
			{
				std::string prefabLoadedFromFilePath = g_ResourceManager->GetPrefabFileName(m_PrefabIDLoadedFrom);
				ImGui::Text("Prefab source: %s", prefabLoadedFromFilePath.c_str());
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

			glm::vec3 translation = m_Transform.GetLocalPosition();
			glm::vec3 rotation = glm::degrees((glm::eulerAngles(m_Transform.GetLocalRotation())));
			glm::vec3 pScale = m_Transform.GetLocalScale();
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
				m_Transform.SetLocalPosition(translation, false);

				glm::quat rotQuat(glm::quat(glm::radians(cleanedRot)));

				m_Transform.SetLocalRotation(rotQuat, false);
				m_Transform.SetLocalScale(scale, true);
				SetUseUniformScale(m_bUniformScale, false);

				if (m_RigidBody)
				{
					m_RigidBody->MatchParentTransform();
				}

				if (g_Editor->IsObjectSelected(ID))
				{
					g_Editor->CalculateSelectedObjectsCenter();
				}
			}
		}

		if (m_Mesh != nullptr)
		{
			bAnyPropertyChanged = g_Renderer->DrawImGuiForGameObject(this) || bAnyPropertyChanged;
		}
		else
		{
			if (ImGui::Button("Add mesh component"))
			{
				bAnyPropertyChanged = true;
				Mesh* mesh = SetMesh(new Mesh(this));
				mesh->LoadFromFile(MESH_DIRECTORY "cube.glb", g_Renderer->GetPlaceholderMaterialID());
			}
		}

		if (m_RigidBody)
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
						i32 group = m_RigidBody->GetGroup();
						if (ImGui::InputInt("Group", &group, 1, 16))
						{
							bAnyPropertyChanged = true;
							group = glm::clamp(group, -1, 16);
							m_RigidBody->SetGroup(group);
						}

						ImGui::SameLine();

						i32 mask = m_RigidBody->GetMask();
						if (ImGui::InputInt("Mask", &mask, 1, 16))
						{
							bAnyPropertyChanged = true;
							mask = glm::clamp(mask, -1, 16);
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

										BroadphaseNativeTypes collisionShapeType = g_CollisionTypes[selectedColliderShape];
										switch (collisionShapeType)
										{
										case BOX_SHAPE_PROXYTYPE:
										{
											btBoxShape* newShape = new btBoxShape(btVector3(1.0f, 1.0f, 1.0f));
											SetCollisionShape(newShape);
										} break;
										case SPHERE_SHAPE_PROXYTYPE:
										{
											btSphereShape* newShape = new btSphereShape(1.0f);
											SetCollisionShape(newShape);
										} break;
										case CAPSULE_SHAPE_PROXYTYPE:
										{
											btCapsuleShapeZ* newShape = new btCapsuleShapeZ(1.0f, 1.0f);
											SetCollisionShape(newShape);
										} break;
										case CYLINDER_SHAPE_PROXYTYPE:
										{
											btCylinderShape* newShape = new btCylinderShape(btVector3(1.0f, 1.0f, 1.0f));
											SetCollisionShape(newShape);
										} break;
										case CONE_SHAPE_PROXYTYPE:
										{
											btConeShape* newShape = new btConeShape(1.0f, 1.0f);
											SetCollisionShape(newShape);
										} break;
										default:
										{
											PrintError("Unhandled BroadphaseNativeType in GameObject::DrawImGuiObjects: %d\n", (i32)collisionShapeType);
										} break;
										}
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

						real maxExtent = 1000.0f;
						if (ImGui::DragFloat3("Half extents", &halfExtentsG.x, 0.1f, 0.0f, maxExtent))
						{
							bAnyPropertyChanged = true;
							halfExtents = ToBtVec3(halfExtentsG);
							btBoxShape* newShape = new btBoxShape(halfExtents);
							SetCollisionShape(newShape);
						}
					} break;
					case SPHERE_SHAPE_PROXYTYPE:
					{
						btSphereShape* sphereShape = static_cast<btSphereShape*>(shape);
						real radius = sphereShape->getRadius();
						radius /= scale.x;

						real maxExtent = 1000.0f;
						if (ImGui::DragFloat("radius", &radius, 0.1f, 0.0f, maxExtent))
						{
							bAnyPropertyChanged = true;
							btSphereShape* newShape = new btSphereShape(radius);
							SetCollisionShape(newShape);
						}
					} break;
					case CAPSULE_SHAPE_PROXYTYPE:
					{
						btCapsuleShapeZ* capsuleShape = static_cast<btCapsuleShapeZ*>(shape);
						real radius = capsuleShape->getRadius();
						real halfHeight = capsuleShape->getHalfHeight();
						radius /= scale.x;
						halfHeight /= scale.x;

						real maxExtent = 1000.0f;
						bool bUpdateShape = ImGui::DragFloat("radius", &radius, 0.1f, 0.0f, maxExtent);
						bUpdateShape |= ImGui::DragFloat("height", &halfHeight, 0.1f, 0.0f, maxExtent);

						if (bUpdateShape)
						{
							bAnyPropertyChanged = true;
							btCapsuleShapeZ* newShape = new btCapsuleShapeZ(radius, halfHeight * 2.0f);
							SetCollisionShape(newShape);
						}
					} break;
					case CYLINDER_SHAPE_PROXYTYPE:
					{
						btCylinderShape* cylinderShape = static_cast<btCylinderShape*>(shape);
						btVector3 halfExtents = cylinderShape->getHalfExtentsWithMargin();
						glm::vec3 halfExtentsG = ToVec3(halfExtents);
						halfExtentsG /= scale;

						real maxExtent = 1000.0f;
						if (ImGui::DragFloat3("Half extents", &halfExtentsG.x, 0.1f, 0.0f, maxExtent))
						{
							bAnyPropertyChanged = true;
							halfExtents = ToBtVec3(halfExtentsG);
							btCylinderShape* newShape = new btCylinderShape(halfExtents);
							SetCollisionShape(newShape);
						}
					} break;
					default:
					{
						PrintWarn("Unhandled shape type in GameObject::DrawImGuiObjects\n");
					} break;
					}

					glm::vec3 localOffsetPos = m_RigidBody->GetLocalPosition();
					if (ImGui::DragFloat3("Pos offset", &localOffsetPos.x, 0.05f))
					{
						bAnyPropertyChanged = true;
						m_RigidBody->SetLocalPosition(localOffsetPos);
					}
					if (ImGui::IsItemClicked(1))
					{
						bAnyPropertyChanged = true;
						m_RigidBody->SetLocalPosition(VEC3_ZERO);
					}

					glm::vec3 localOffsetRotEuler = glm::degrees(glm::eulerAngles(m_RigidBody->GetLocalRotation()));
					glm::vec3 cleanedRot;
					if (DoImGuiRotationDragFloat3("Rot offset", localOffsetRotEuler, cleanedRot))
					{
						bAnyPropertyChanged = true;
						m_RigidBody->SetLocalRotation(glm::quat(glm::radians(cleanedRot)));
					}

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

					ImGui::Text("Group: %i", m_RigidBody->GetGroup());
					ImGui::Text("Mask: %i", m_RigidBody->GetMask());
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
				btVector3 btHalfExtents(1.0f, 1.0f, 1.0f);
				btBoxShape* boxShape = new btBoxShape(btHalfExtents);

				SetCollisionShape(boxShape);
				rb->Initialize(boxShape, &m_Transform);
				rb->GetRigidBodyInternal()->setUserPointer(this);
			}
		}

		if (!sockets.empty())
		{
			bool bTreeOpen = ImGui::TreeNode("Sockets");
			ImGui::SameLine();
			ImGui::Text("(%i)", (i32)sockets.size());

			if (bTreeOpen)
			{
				for (u32 i = 0; i < (u32)sockets.size(); ++i)
				{
					ImGui::Text("%u: %i", i, outputSignals[i]);
				}
				ImGui::TreePop();
			}
		}

		ImGui::Text("Interactable: %s", m_bInteractable ? "true" : "false");
		ImGui::Text("Serializable: %s", m_bSerializable ? "true" : "false");

		ImGui::Text("Trigger: %s", m_bTrigger ? "true" : "false"); // TODO: Make checkbox

		std::string objectInteractingWithName;
		if (m_ObjectInteractingWith != nullptr)
		{
			objectInteractingWithName = m_ObjectInteractingWith->GetName();
		}
		ImGui::Text("ObjectInteractingWith: %s", objectInteractingWithName.c_str());

		std::string nearbyInteractibleName;
		if (m_NearbyInteractable != nullptr)
		{
			nearbyInteractibleName = m_NearbyInteractable->GetName();
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

		if (bAnyPropertyChanged)
		{
			if (m_PrefabIDLoadedFrom.IsValid())
			{
				g_ResourceManager->SetPrefabDirty(m_PrefabIDLoadedFrom);
			}
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

			if (m_PrefabIDLoadedFrom.IsValid())
			{
				std::string prefabLoadedFromFilePath = g_ResourceManager->GetPrefabFileName(m_PrefabIDLoadedFrom);
				ImGui::Text("Prefab source: %s", prefabLoadedFromFilePath.c_str());
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

			if (ImGui::Button("Save as prefab"))
			{
				SaveAsPrefab();

				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		ImGui::PopID(); // ID.Data2
		ImGui::PopID(); // ID.Data1

		return bDeletedOrDuplicated;
	}

	void GameObject::SaveAsPrefab()
	{
		if (m_PrefabIDLoadedFrom.IsValid())
		{
			CopyFlags copyFlags = (CopyFlags)(
				(CopyFlags::ALL &
					~CopyFlags::ADD_TO_SCENE &
					~CopyFlags::CREATE_RENDER_OBJECT)
				| CopyFlags::COPYING_TO_PREFAB);
			GameObject* previousPrefabTemplate = g_ResourceManager->GetPrefabTemplate(m_PrefabIDLoadedFrom);
			g_SceneManager->CurrentScene()->UnregisterGameObject(previousPrefabTemplate->ID);
			std::string previousPrefabName = previousPrefabTemplate->GetName();
			//GameObjectID previousPrefabID = previousPrefabTemplate->ID;
			GameObject* newPrefabTemplate = CopySelf(nullptr, copyFlags, &previousPrefabName);
			newPrefabTemplate->m_bIsTemplate = true;
			newPrefabTemplate->m_PrefabIDLoadedFrom = InvalidPrefabID;
			newPrefabTemplate->ID = previousPrefabTemplate->ID;
			newPrefabTemplate->m_Name = previousPrefabName;
			OverwritePrefabIDs(previousPrefabTemplate, newPrefabTemplate);
			previousPrefabTemplate->FixupPrefabTemplateIDs(newPrefabTemplate);

			g_ResourceManager->UpdatePrefabData(newPrefabTemplate, m_PrefabIDLoadedFrom);
		}
		else
		{
			m_PrefabIDLoadedFrom = g_ResourceManager->AddNewPrefab(this);
		}
	}

	void GameObject::OverwritePrefabIDs(GameObject* previousGameObject, GameObject* newGameObject)
	{
		if (previousGameObject->m_Name == newGameObject->m_Name)
		{
			newGameObject->ID = previousGameObject->ID;
		}

		for (u32 i = 0; i < (u32)previousGameObject->m_Children.size(); ++i)
		{
			OverwritePrefabIDs(previousGameObject->GetChild(i), newGameObject->GetChild(i));
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
		if (m_RigidBody)
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

	bool GameObject::AllowInteractionWith(GameObject* gameObject)
	{
		FLEX_UNUSED(gameObject);

		return true;
	}

	void GameObject::SetInteractingWith(GameObject* gameObject)
	{
		m_ObjectInteractingWith = gameObject;
	}

	void GameObject::FixupPrefabTemplateIDs(GameObject* newGameObject)
	{
		FLEX_UNUSED(newGameObject);
	}

	bool GameObject::IsBeingInteractedWith() const
	{
		return m_ObjectInteractingWith != nullptr;
	}

	GameObject* GameObject::GetObjectInteractingWith()
	{
		return m_ObjectInteractingWith;
	}

	void GameObject::ParseJSON(
		const JSONObject& obj,
		BaseScene* scene,
		i32 fileVersion,
		MaterialID overriddenMatID /* = InvalidMaterialID */,
		bool bIsPrefabTemplate /* = false */,
		CopyFlags copyFlags /* = CopyFlags::ALL */)
	{
		bool bVisible = true;
		obj.SetBoolChecked("visible", bVisible);
		bool bVisibleInSceneGraph = true;
		obj.SetBoolChecked("visible in scene graph", bVisibleInSceneGraph);

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
		if (obj.SetObjectChecked("transform", transformObj))
		{
			m_Transform = Transform::ParseJSON(transformObj);
		}

		g_ResourceManager->ParseMeshJSON(fileVersion, this, obj, matIDs);

		bool bColliderContainsOffset = false;

		glm::vec3 localPos(0.0f);
		glm::quat localRot(VEC3_ZERO);
		glm::vec3 localScale(1.0f);

		JSONObject colliderObj;
		if (obj.SetObjectChecked("collider", colliderObj))
		{
			std::string shapeStr = colliderObj.GetString("shape");
			BroadphaseNativeTypes shapeType = StringToCollisionShapeType(shapeStr);

			switch (shapeType)
			{
			case BOX_SHAPE_PROXYTYPE:
			{
				glm::vec3 halfExtents;
				colliderObj.SetVec3Checked("half extents", halfExtents);
				btVector3 btHalfExtents(halfExtents.x, halfExtents.y, halfExtents.z);
				btBoxShape* boxShape = new btBoxShape(btHalfExtents);

				SetCollisionShape(boxShape);
			} break;
			case SPHERE_SHAPE_PROXYTYPE:
			{
				real radius = colliderObj.GetFloat("radius");
				btSphereShape* sphereShape = new btSphereShape(radius);

				SetCollisionShape(sphereShape);
			} break;
			case CAPSULE_SHAPE_PROXYTYPE:
			{
				real radius = colliderObj.GetFloat("radius");
				real height = colliderObj.GetFloat("height");
				btCapsuleShapeZ* capsuleShape = new btCapsuleShapeZ(radius, height);

				SetCollisionShape(capsuleShape);
			} break;
			case CONE_SHAPE_PROXYTYPE:
			{
				real radius = colliderObj.GetFloat("radius");
				real height = colliderObj.GetFloat("height");
				btConeShape* coneShape = new btConeShape(radius, height);

				SetCollisionShape(coneShape);
			} break;
			case CYLINDER_SHAPE_PROXYTYPE:
			{
				glm::vec3 halfExtents;
				colliderObj.SetVec3Checked("half extents", halfExtents);
				btVector3 btHalfExtents(halfExtents.x, halfExtents.y, halfExtents.z);
				btCylinderShape* cylinderShape = new btCylinderShape(btHalfExtents);

				SetCollisionShape(cylinderShape);
			} break;
			default:
			{
				PrintWarn("Unhandled BroadphaseNativeType: %s\n", shapeStr.c_str());
			} break;
			}

			if (colliderObj.SetVec3Checked("offset pos", localPos))
			{
				bColliderContainsOffset = true;
			}

			glm::vec3 localRotEuler;
			if (colliderObj.SetVec3Checked("offset rot", localRotEuler))
			{
				localRot = glm::quat(localRotEuler);
				bColliderContainsOffset = true;
			}

			//
			//if (colliderObj.SetVec3Checked("offset scale", localScale))
			//{
			//	bColliderContainsOffset = true;
			//}

			//bool bIsTrigger = colliderObj.GetBool("trigger");
			// TODO: Create btGhostObject to use for trigger
		}

		JSONObject rigidBodyObj;
		if (obj.SetObjectChecked("rigid body", rigidBodyObj))
		{
			if (GetCollisionShape() == nullptr)
			{
				PrintError("Serialized object contains \"rigid body\" field but no collider: %s\n", m_Name.c_str());
			}
			else
			{
				real mass = rigidBodyObj.GetFloat("mass");
				bool bKinematic = rigidBodyObj.GetBool("kinematic");
				bool bStatic = rigidBodyObj.GetBool("static");
				i32 mask = rigidBodyObj.GetInt("mask");
				i32 group = rigidBodyObj.GetInt("group");

				RigidBody* rigidBody = SetRigidBody(new RigidBody(group, mask));
				rigidBody->SetMass(mass);
				rigidBody->SetKinematic(bKinematic);
				// TODO: Use IsStatic() ?
				rigidBody->SetStatic(bStatic);
			}
		}

		// Must happen after rigid body has been created
		if (bColliderContainsOffset)
		{
			m_RigidBody->SetLocalSRT(localScale, localRot, localPos);
		}

		ParseTypeUniqueFields(obj, scene, matIDs);

		if (!bIsPrefabTemplate)
		{
			ParseInstanceUniqueFields(obj, scene, matIDs);
		}

		SetVisible(bVisible, false);
		SetVisibleInSceneExplorer(bVisibleInSceneGraph);

		bool bStatic = false;
		if (obj.SetBoolChecked("static", bStatic))
		{
			SetStatic(bStatic);
		}

		obj.SetBoolChecked("casts shadow", m_bCastsShadow);


		std::vector<JSONObject> children;
		if (obj.SetObjectArrayChecked("children", children))
		{
			for (JSONObject& child : children)
			{
				AddChildImmediate(GameObject::CreateObjectFromJSON(child, scene, fileVersion, bIsPrefabTemplate, copyFlags));
			}
		}
	}

	JSONObject GameObject::Serialize(const BaseScene* scene,
		bool bIsRoot /* = false */,
		bool bSerializePrefabData /* = false */)
	{
		JSONObject object = {};

		if (!m_bSerializable)
		{
			PrintError("Attempted to serialize non-serializable object with name \"%s\"\n", m_Name.c_str());
			return object;
		}

		object.fields.emplace_back("name", JSONValue(m_Name));

		// Added in scene v5
		object.fields.emplace_back("id", JSONValue(ID));

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
			if (m_PrefabIDLoadedFrom.IsValid())
			{
				object.fields.emplace_back("type", JSONValue(std::string("prefab")));
				std::string prefabName = g_ResourceManager->GetPrefabTemplate(m_PrefabIDLoadedFrom)->m_Name;
				object.fields.emplace_back("prefab type", JSONValue(prefabName)); // More of a convenience for source control, GUID is ground truth
				// Added in scene v6
				std::string prefabIDStr = m_PrefabIDLoadedFrom.ToString();
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

		// Don't serialize prefab root's transform data
		if (!bSerializePrefabData || !bIsRoot)
		{
			JSONField transformField = m_Transform.Serialize();
			if (!transformField.value.objectValue.fields.empty())
			{
				object.fields.push_back(transformField);
			}
		}

		// TODO: Handle overrides
		if (!m_PrefabIDLoadedFrom.IsValid() || bSerializePrefabData)
		{
			object.fields.emplace_back("visible", JSONValue(IsVisible()));
			if (!IsVisibleInSceneExplorer())
			{
				object.fields.emplace_back("visible in scene graph", JSONValue(IsVisibleInSceneExplorer()));
			}

			if (IsStatic())
			{
				object.fields.emplace_back("static", JSONValue(true));
			}

			object.fields.emplace_back("casts shadow", JSONValue(m_bCastsShadow));

			if (m_Mesh != nullptr)
			{
				if (m_bSerializeMesh && !m_PrefabIDLoadedFrom.IsValid())
				{
					object.fields.emplace_back(g_ResourceManager->SerializeMesh(m_Mesh));
				}

				if (m_bSerializeMaterial)
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
									materialFields.emplace_back(materialName, JSONValue(""));
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
			if (collisionShape &&
				(!m_PrefabIDLoadedFrom.IsValid() || bSerializePrefabData))
			{
				JSONObject colliderObj;

				i32 shapeType = collisionShape->getShapeType();
				std::string shapeTypeStr = CollisionShapeTypeToString(shapeType);
				colliderObj.fields.emplace_back("shape", JSONValue(shapeTypeStr));

				switch (shapeType)
				{
				case BOX_SHAPE_PROXYTYPE:
				{
					btVector3 btHalfExtents = static_cast<btBoxShape*>(collisionShape)->getHalfExtentsWithMargin();
					glm::vec3 halfExtents = ToVec3(btHalfExtents);
					halfExtents /= m_Transform.GetWorldScale();
					std::string halfExtentsStr = VecToString(halfExtents, 3);
					colliderObj.fields.emplace_back("half extents", JSONValue(halfExtentsStr));
				} break;
				case SPHERE_SHAPE_PROXYTYPE:
				{
					real radius = static_cast<btSphereShape*>(collisionShape)->getRadius();
					radius /= m_Transform.GetWorldScale().x;
					colliderObj.fields.emplace_back("radius", JSONValue(radius));
				} break;
				case CAPSULE_SHAPE_PROXYTYPE:
				{
					real radius = static_cast<btCapsuleShapeZ*>(collisionShape)->getRadius();
					real height = static_cast<btCapsuleShapeZ*>(collisionShape)->getHalfHeight() * 2.0f;
					radius /= m_Transform.GetWorldScale().x;
					height /= m_Transform.GetWorldScale().x;
					colliderObj.fields.emplace_back("radius", JSONValue(radius));
					colliderObj.fields.emplace_back("height", JSONValue(height));
				} break;
				case CONE_SHAPE_PROXYTYPE:
				{
					real radius = static_cast<btConeShape*>(collisionShape)->getRadius();
					real height = static_cast<btConeShape*>(collisionShape)->getHeight();
					radius /= m_Transform.GetWorldScale().x;
					height /= m_Transform.GetWorldScale().x;
					colliderObj.fields.emplace_back("radius", JSONValue(radius));
					colliderObj.fields.emplace_back("height", JSONValue(height));
				} break;
				case CYLINDER_SHAPE_PROXYTYPE:
				{
					btVector3 btHalfExtents = static_cast<btCylinderShape*>(collisionShape)->getHalfExtentsWithMargin();
					glm::vec3 halfExtents = ToVec3(btHalfExtents);
					halfExtents /= m_Transform.GetWorldScale();
					std::string halfExtentsStr = VecToString(halfExtents, 3);
					colliderObj.fields.emplace_back("half extents", JSONValue(halfExtentsStr));
				} break;
				default:
				{
					PrintWarn("Unhandled BroadphaseNativeType: %i\n on: %s in scene: %s\n",
						shapeType, m_Name.c_str(), scene->GetName().c_str());
				} break;
				}

				if (m_RigidBody->GetLocalPosition() != VEC3_ZERO)
				{
					colliderObj.fields.emplace_back("offset pos", JSONValue(VecToString(m_RigidBody->GetLocalPosition(), 3)));
				}

				if (m_RigidBody->GetLocalRotation() != QUAT_IDENTITY)
				{
					glm::vec3 localRotEuler = glm::eulerAngles(m_RigidBody->GetLocalRotation());
					colliderObj.fields.emplace_back("offset rot", JSONValue(VecToString(localRotEuler, 3)));
				}

				if (m_RigidBody->GetLocalScale() != VEC3_ONE)
				{
					colliderObj.fields.emplace_back("offset scale", JSONValue(VecToString(m_RigidBody->GetLocalScale(), 3)));
				}

				//bool bTrigger = false;
				//colliderObj.fields.emplace_back("trigger", JSONValue(bTrigger)));
				// TODO: Handle triggers

				object.fields.emplace_back("collider", JSONValue(colliderObj));
			}

			RigidBody* rigidBody = GetRigidBody();
			if (rigidBody &&
				(!m_PrefabIDLoadedFrom.IsValid() || bSerializePrefabData))
			{
				JSONObject rigidBodyObj = {};

				if (collisionShape == nullptr)
				{
					PrintError("Attempted to serialize object (%s) which has a rigid body but no collider!\n", GetName().c_str());
				}
				else
				{
					real mass = rigidBody->GetMass();
					bool bKinematic = rigidBody->IsKinematic();
					bool bStatic = rigidBody->IsStatic();
					i32 mask = m_RigidBody->GetMask();
					i32 group = m_RigidBody->GetGroup();

					rigidBodyObj.fields.emplace_back("mass", JSONValue(mass));
					rigidBodyObj.fields.emplace_back("kinematic", JSONValue(bKinematic));
					rigidBodyObj.fields.emplace_back("static", JSONValue(bStatic));
					rigidBodyObj.fields.emplace_back("mask", JSONValue(mask));
					rigidBodyObj.fields.emplace_back("group", JSONValue(group));
				}

				object.fields.emplace_back("rigid body", JSONValue(rigidBodyObj));
			}

			SerializeTypeUniqueFields(object);
		}

		if (!bSerializePrefabData)
		{
			SerializeInstanceUniqueFields(object);
		}

		if (!m_Children.empty())
		{
			std::vector<JSONObject> childrenToSerialize;

			for (GameObject* child : m_Children)
			{
				if (child->IsSerializable())
				{
					if (m_PrefabIDLoadedFrom.IsValid())
					{
						if (g_ResourceManager->PrefabTemplateContainsChild(m_PrefabIDLoadedFrom, child))
						{
							continue;
						}
					}

					childrenToSerialize.push_back(child->Serialize(scene, false, bSerializePrefabData));
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

	void GameObject::AddSelfAndChildrenToVec(std::vector<GameObject*>& vec)
	{
		if (!Contains(vec, this))
		{
			vec.push_back(this);
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

	void GameObject::RemoveSelfAndChildrenToVec(std::vector<GameObject*>& vec)
	{
		auto iter = FindIter(vec, this);
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

			child->RemoveSelfAndChildrenToVec(vec);
		}
	}
	void GameObject::AddSelfIDAndChildrenToVec(std::vector<GameObjectID>& vec)
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

	void GameObject::RemoveSelfIDAndChildrenToVec(std::vector<GameObjectID>& vec)
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

			child->RemoveSelfIDAndChildrenToVec(vec);
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

	void GameObject::SetNearbyInteractable(GameObject* nearbyInteractable)
	{
		m_NearbyInteractable = nearbyInteractable;
	}

	bool GameObject::IsTemplate() const
	{
		return m_bIsTemplate;
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

	PrefabID GameObject::Itemize()
	{
		PrefabID prefabID = m_PrefabIDLoadedFrom;

		if (!prefabID.IsValid())
		{
			PrintError("Attempted to itemize object not loaded from prefab\n");
			return prefabID;
		}

		g_SceneManager->CurrentScene()->RemoveObject(this, true);

		return prefabID;
	}

	GameObject* GameObject::Deitemize(PrefabID prefabID, const glm::vec3& positionWS)
	{
		GameObject* newObject = CreateObjectFromPrefabTemplate(prefabID, InvalidGameObjectID);

		newObject->m_Transform.SetWorldPosition(positionWS);

		newObject->Initialize();
		newObject->PostInitialize();

		return newObject;
	}

	bool GameObject::IsItemizable() const
	{
		// Only prefabs are itemizable, we store the prefab ID
		return m_bItemizable && (m_bIsTemplate || m_PrefabIDLoadedFrom.IsValid());
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

	void GameObject::GetNewObjectNameAndID(CopyFlags copyFlags, std::string* optionalName, std::string& newObjectName, GameObjectID& newGameObjectID)
	{
		if (copyFlags & CopyFlags::COPYING_TO_PREFAB)
		{
			newObjectName = m_Name;
			newGameObjectID = ID;
		}
		else
		{
			newObjectName = (optionalName != nullptr ? *optionalName : g_SceneManager->CurrentScene()->GetUniqueObjectName(m_Name));
		}
	}

	//void GameObject::OnConnectionMade(Wire* wire)
	//{
	//	wireConnections.push_back(wire);
	//	outputSignals.resize(wireConnections.size(), -1);
	//}
	//
	//void GameObject::OnConnectionBroke(Wire* wire)
	//{
	//	FLEX_UNUSED(wire);
	//}

	void GameObject::ParseTypeUniqueFields(const JSONObject& /* parentObj */, BaseScene* /* scene */, const std::vector<MaterialID>& /* matIDs */)
	{
		// Generic game objects have no unique fields
	}

	void GameObject::ParseInstanceUniqueFields(const JSONObject& /* parentObj */, BaseScene* /* scene */, const std::vector<MaterialID>& /* matIDs */)
	{
		// Generic game objects have no unique fields
	}

	void GameObject::SerializeTypeUniqueFields(JSONObject& /* parentObject */)
	{
		// Generic game objects have no unique fields
	}

	void GameObject::SerializeInstanceUniqueFields(JSONObject& /* parentObject */) const
	{
		// Generic game objects have no unique fields
	}

	void GameObject::CopyGenericFields(
		GameObject* newGameObject,
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */)
	{
		newGameObject->m_Tags = m_Tags;
		newGameObject->m_Transform = m_Transform;
		newGameObject->m_TypeID = m_TypeID;

		newGameObject->m_bSerializable = m_bSerializable;
		newGameObject->m_bVisible = m_bVisible;
		newGameObject->m_bVisibleInSceneExplorer = m_bVisibleInSceneExplorer;
		newGameObject->m_bStatic = m_bStatic;
		newGameObject->m_bTrigger = m_bTrigger;
		newGameObject->m_bInteractable = m_bInteractable;
		newGameObject->m_PrefabIDLoadedFrom = m_PrefabIDLoadedFrom;
		newGameObject->m_bCastsShadow = m_bCastsShadow;
		newGameObject->m_bUniformScale = m_bUniformScale;

		bool bAddToScene = (copyFlags & CopyFlags::ADD_TO_SCENE);

		if (parent != nullptr)
		{
			parent->AddChild(newGameObject);
		}
		else
		{
			if (m_Parent != nullptr)
			{
				g_SceneManager->CurrentScene()->AddChildObject(m_Parent, newGameObject);
			}
			else
			{
				if (bAddToScene)
				{
					g_SceneManager->CurrentScene()->AddRootObject(newGameObject);
				}
			}
		}

		for (const std::string& tag : m_Tags)
		{
			newGameObject->AddTag(tag);
		}

		bool bCreateRenderObject = (copyFlags & CopyFlags::CREATE_RENDER_OBJECT);

		if ((copyFlags & CopyFlags::MESH) && m_Mesh != nullptr)
		{
			std::vector<MaterialID> matIDs = m_Mesh->GetMaterialIDs();

			Mesh* newMesh = newGameObject->SetMesh(new Mesh(newGameObject));
			Mesh::Type meshType = m_Mesh->GetType();
			switch (meshType)
			{
			case Mesh::Type::PREFAB:
			{
				PrefabShape shape = m_Mesh->GetSubMesh(0)->GetShape();
				newMesh->LoadPrefabShape(shape, matIDs[0], nullptr, bCreateRenderObject);
			} break;
			case Mesh::Type::FILE:
			{
				std::string filePath = m_Mesh->GetRelativeFilePath();
				newMesh->LoadFromFile(filePath, matIDs, false, bCreateRenderObject);
			} break;
			default:
			{
				PrintError("Unhandled mesh component prefab type encountered while duplicating object\n");
			} break;
			}
		}

		if ((copyFlags & CopyFlags::RIGIDBODY) && m_RigidBody != nullptr)
		{
			newGameObject->SetRigidBody(new RigidBody(*m_RigidBody));

			btCollisionShape* pCollisionShape = m_CollisionShape;
			btCollisionShape* newCollisionShape = nullptr;

			btVector3 btWorldScale = ToBtVec3(m_Transform.GetWorldScale());
			real btWorldScaleX = btWorldScale.getX();

			i32 shapeType = pCollisionShape->getShapeType();
			switch (shapeType)
			{
			case BOX_SHAPE_PROXYTYPE:
			{
				btVector3 btHalfExtents = static_cast<btBoxShape*>(pCollisionShape)->getHalfExtentsWithMargin();
				btHalfExtents = btHalfExtents / btWorldScale;
				newCollisionShape = new btBoxShape(btHalfExtents);
			} break;
			case SPHERE_SHAPE_PROXYTYPE:
			{
				real radius = static_cast<btSphereShape*>(pCollisionShape)->getRadius();
				radius /= btWorldScaleX;
				newCollisionShape = new btSphereShape(radius);
			} break;
			case CAPSULE_SHAPE_PROXYTYPE:
			{
				real radius = static_cast<btCapsuleShapeZ*>(pCollisionShape)->getRadius();
				real height = static_cast<btCapsuleShapeZ*>(pCollisionShape)->getHalfHeight() * 2.0f;
				radius /= btWorldScaleX;
				height /= btWorldScaleX;
				newCollisionShape = new btCapsuleShapeZ(radius, height);
			} break;
			case CONE_SHAPE_PROXYTYPE:
			{
				real radius = static_cast<btConeShape*>(pCollisionShape)->getRadius();
				real height = static_cast<btConeShape*>(pCollisionShape)->getHeight();
				radius /= btWorldScaleX;
				height /= btWorldScaleX;
				newCollisionShape = new btConeShape(radius, height);
			} break;
			case CYLINDER_SHAPE_PROXYTYPE:
			{
				btVector3 btHalfExtents = static_cast<btCylinderShape*>(pCollisionShape)->getHalfExtentsWithMargin();
				btHalfExtents = btHalfExtents / btWorldScale;
				newCollisionShape = new btCylinderShape(btHalfExtents);
			} break;
			default:
			{
				PrintWarn("Unhanded shape type in GameObject::CopyGenericFields\n");
			} break;
			}

			newGameObject->SetCollisionShape(newCollisionShape);

			// TODO: Copy over constraints here
		}

		if (copyFlags & CopyFlags::CHILDREN)
		{
			for (GameObject* child : m_Children)
			{
				if (child->IsSerializable())
				{
					GameObject* newChild = child->CopySelf(newGameObject, copyFlags);
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
		if (slotIdx >= (i32)outputSignals.size())
		{
			outputSignals.resize(slotIdx + 1);
		}
		outputSignals[slotIdx] = value;
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

	GameObject* GameObject::AddChildImmediate(GameObject* child)
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
		GameObject* childPParent = child->GetParent();
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

		if (childPParent)
		{
			childTransform->SetWorldTransform(childWorldTransform);
		}

		if (child->GetTypeID() == SID("socket"))
		{
			Socket* socket = (Socket*)child;
			socket->slotIdx = (i32)sockets.size();
			sockets.push_back(socket);
			socket->parent = this;
			outputSignals.resize(sockets.size(), -1);
		}

		if (g_SceneManager->HasSceneLoaded())
		{
			g_SceneManager->CurrentScene()->UpdateRootObjectSiblingIndices();
		}

		return child;
	}

	bool GameObject::RemoveChildImmediate(GameObjectID childID, bool bDestroy)
	{
		GameObject* child = g_SceneManager->CurrentScene()->GetGameObject(childID);
		return RemoveChildImmediate(child, bDestroy);
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
					child->Destroy();
					delete child;
				}
				else
				{
					glm::mat4 childWorldTransform = child->GetTransform()->GetWorldTransform();

					child->SetParent(nullptr);

					child->GetTransform()->SetWorldTransform(childWorldTransform);
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
			return m_Parent->AddChild(child);
		}
		else
		{
			return g_SceneManager->CurrentScene()->AddRootObject(child);
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

	void GameObject::UpdateSiblingIndices(i32 myIndex)
	{
		m_SiblingIndex = myIndex;

		for (i32 i = 0; i < (i32)m_Children.size(); ++i)
		{
			m_Children[i]->UpdateSiblingIndices(i);
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
			assert(thisIter != siblings.end());

			for (auto iter = siblings.begin(); iter != thisIter; ++iter)
			{
				result.push_back(*iter);
			}
		}
		else
		{
			const std::vector<GameObject*>& rootObjects = g_SceneManager->CurrentScene()->GetRootObjects();

			auto thisIter = FindIter(rootObjects, this);
			assert(thisIter != rootObjects.end());

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
			assert(thisIter != siblings.end());

			for (auto iter = thisIter + 1; iter != siblings.end(); ++iter)
			{
				result.push_back(*iter);
			}
		}
		else
		{
			const std::vector<GameObject*>& rootObjects = g_SceneManager->CurrentScene()->GetRootObjects();

			auto thisIter = FindIter(rootObjects, this);
			assert(thisIter != rootObjects.end());

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

	void GameObject::OnTransformChanged()
	{
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
			m_Transform.SetLocalScale(glm::vec3(m_Transform.GetLocalScale().x));
		}
	}

	btCollisionShape* GameObject::SetCollisionShape(btCollisionShape* collisionShape)
	{
		if (m_CollisionShape)
		{
			delete m_CollisionShape;
			m_CollisionShape = nullptr;
		}

		m_CollisionShape = collisionShape;

		if (m_RigidBody && m_RigidBody->GetRigidBodyInternal())
		{
			m_RigidBody->GetRigidBodyInternal()->setCollisionShape(collisionShape);
		}

		return collisionShape;
	}

	btCollisionShape* GameObject::GetCollisionShape() const
	{
		return m_CollisionShape;
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
		m_bCastsShadow = bCastsShadow;
		g_Renderer->SetDirtyFlags(RenderBatchDirtyFlag::SHADOW_DATA);
	}

	void GameObject::OnOverlapBegin(GameObject* other)
	{
		overlappingObjects.push_back(other);

		if (m_TypeID != SID("player"))
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

		if (m_TypeID != SID("player"))
		{
			if (other->HasTag("Player0") ||
				other->HasTag("Player1"))
			{
				m_bInteractable = false;
			}
		}
	}

	Valve::Valve(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("valve"), gameObjectID)
	{
		// TODO: Set m_bSerializeMesh to false here?
	}

	GameObject* Valve::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, newObjectName, newGameObjectID);
		Valve* newGameObject = new Valve(newObjectName, newGameObjectID);

		newGameObject->minRotation = minRotation;
		newGameObject->maxRotation = maxRotation;
		newGameObject->rotationSpeedScale = rotationSpeedScale;
		newGameObject->invSlowDownRate = invSlowDownRate;
		newGameObject->rotationSpeed = rotationSpeed;
		newGameObject->rotation = rotation;

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void Valve::ParseTypeUniqueFields(const JSONObject& parentObj, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		JSONObject valveInfo;
		if (parentObj.SetObjectChecked("valve info", valveInfo))
		{
			glm::vec2 valveRange;
			valveInfo.SetVec2Checked("range", valveRange);
			minRotation = valveRange.x;
			maxRotation = valveRange.y;
			if (glm::abs(maxRotation - minRotation) <= 0.0001f)
			{
				PrintWarn("Valve's rotation range is 0, it will not be able to rotate!\n");
			}
			if (minRotation > maxRotation)
			{
				PrintWarn("Valve's minimum rotation range is greater than its maximum! Undefined behavior\n");
			}
		}
		else
		{
			std::string sceneName = scene->GetName();
			PrintError("Valve's \"valve info\" field missing in scene %s\n", sceneName.c_str());
		}

		if (m_Mesh != nullptr)
		{
			Mesh* valveMesh = new Mesh(this);
			bool bCreateRenderObject = !m_bIsTemplate;
			valveMesh->LoadFromFile(MESH_DIRECTORY "valve.glb", matIDs.empty() ? g_Renderer->GetPlaceholderMaterialID() : matIDs[0], false, bCreateRenderObject);
			assert(m_Mesh == nullptr);
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

	void Valve::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		JSONObject valveInfo = {};

		glm::vec2 valveRange(minRotation, maxRotation);
		valveInfo.fields.emplace_back("range", JSONValue(VecToString(valveRange, 2)));

		parentObject.fields.emplace_back("valve info", JSONValue(valveInfo));
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
		// True when our rotation is changed by another object (rising block)
		bool bRotatedByOtherObject = false;
		real currentAbsAvgRotationSpeed = 0.0f;
		if (m_ObjectInteractingWith != nullptr)
		{
			i32 playerIndex = static_cast<Player*>(m_ObjectInteractingWith)->GetIndex();

			const GamepadState& gamepadState = g_InputManager->GetGamepadState(playerIndex);
			rotationSpeed = (-gamepadState.averageRotationSpeeds.currentAverage) * rotationSpeedScale;
			currentAbsAvgRotationSpeed = glm::abs(gamepadState.averageRotationSpeeds.currentAverage);
		}
		else
		{
			rotationSpeed = (rotation - pRotation) / g_DeltaTime;
			// Not entirely true but needed to trigger sound
			currentAbsAvgRotationSpeed = glm::abs(rotationSpeed);
			bRotatedByOtherObject = (glm::abs(rotation - pRotation) > 0);
		}

		if ((rotationSpeed < 0.0f &&
			rotation <= minRotation) ||
			(rotationSpeed > 0.0f &&
				rotation >= maxRotation))
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
		if (rotation > maxRotation)
		{
			overshoot = rotation - maxRotation;
			rotation = maxRotation;
		}
		else if (rotation < minRotation)
		{
			overshoot = minRotation - rotation;
			rotation = minRotation;
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
		m_RigidBody->UpdateParentTransform();

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

	RisingBlock::RisingBlock(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("rising block"), gameObjectID)
	{
		m_bItemizable = true;
	}

	GameObject* RisingBlock::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, newObjectName, newGameObjectID);
		RisingBlock* newGameObject = new RisingBlock(newObjectName, newGameObjectID);

		newGameObject->valve = valve;
		newGameObject->moveAxis = moveAxis;
		newGameObject->bAffectedByGravity = bAffectedByGravity;
		newGameObject->startingPos = startingPos;

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void RisingBlock::ParseTypeUniqueFields(const JSONObject& parentObj, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		if (!m_Mesh)
		{
			Mesh* cubeMesh = new Mesh(this);
			cubeMesh->LoadFromFile(MESH_DIRECTORY "cube.glb", matIDs[0]);
			SetMesh(cubeMesh);
		}

		if (!m_RigidBody)
		{
			RigidBody* rigidBody = SetRigidBody(new RigidBody());
			rigidBody->SetMass(1.0f);
			rigidBody->SetKinematic(true);
			rigidBody->SetStatic(false);
		}

		std::string valveName;

		JSONObject blockInfo;
		if (parentObj.SetObjectChecked("block info", blockInfo))
		{
			valveName = blockInfo.GetString("valve name");
		}

		if (valveName.empty())
		{
			PrintWarn("Rising block's \"valve name\" field is empty! Can't find matching valve\n");
		}
		else
		{
			const std::vector<GameObject*>& rootObjects = scene->GetRootObjects();
			for (GameObject* rootObject : rootObjects)
			{
				if (rootObject->GetName().compare(valveName) == 0)
				{
					valve = static_cast<Valve*>(rootObject);
					break;
				}
			}
		}

		if (!valve)
		{
			PrintError("Rising block contains invalid valve name: %s - Has that valve been created yet?\n", valveName.c_str());
		}

		blockInfo.SetBoolChecked("affected by gravity", bAffectedByGravity);

		blockInfo.SetVec3Checked("move axis", moveAxis);
		if (moveAxis == VEC3_ZERO)
		{
			PrintWarn("Rising block's move axis is not set! It won't be able to move\n");
		}
	}

	void RisingBlock::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		JSONObject blockInfo = {};

		blockInfo.fields.emplace_back("valve name", JSONValue(valve->GetName()));
		blockInfo.fields.emplace_back("move axis", JSONValue(VecToString(moveAxis, 3)));
		blockInfo.fields.emplace_back("affected by gravity", JSONValue(bAffectedByGravity));

		parentObject.fields.emplace_back("block info", JSONValue(blockInfo));
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

		//btTransform transform = m_RigidBody->GetRigidBodyInternal()->getInterpolationWorldTransform();
		//btFixedConstraint* constraint = new btFixedConstraint(
		//	*m_RigidBody->GetRigidBodyInternal(),
		//	*m_RigidBody->GetRigidBodyInternal(),
		//	transform,
		//	transform);
		////constraint->setAngularLowerLimit(btVector3(0, 0, 0));
		////constraint->setAngularUpperLimit(btVector3(0, 0, 0));
		//m_RigidBody->AddConstraint(constraint);
	}

	void RisingBlock::Update()
	{
		real minDist = valve->minRotation;
		real maxDist = valve->maxRotation;
		//real totalDist = (maxDist - minDist);
		real dist = valve->rotation;

		real playerControlledValveRotationSpeed = 0.0f;
		if (valve->GetObjectInteractingWith())
		{
			i32 playerIndex = static_cast<Player*>(valve->GetObjectInteractingWith())->GetIndex();
			const GamepadState& gamepadState = g_InputManager->GetGamepadState(playerIndex);
			playerControlledValveRotationSpeed = (-gamepadState.averageRotationSpeeds.currentAverage) *
				valve->rotationSpeedScale;
		}

		if (bAffectedByGravity &&
			valve->rotation >=
			valve->minRotation + 0.1f)
		{
			// Apply gravity by rotating valve
			real fallSpeed = 6.0f;
			real distMult = 1.0f - Saturate(playerControlledValveRotationSpeed / 2.0f);
			real dDist = (fallSpeed * g_DeltaTime * distMult);
			dist -= Lerp(pdDistBlockMoved, dDist, 0.1f);
			pdDistBlockMoved = dDist;

			// NOTE: Don't clamp out of bounds rotation here, valve object
			// will handle it and play correct "overshoot" sound
			//dist = glm::clamp(dist, minDist, maxDist);

			valve->rotation = dist;
		}

		glm::vec3 newPos = startingPos + dist * moveAxis;

		if (m_RigidBody)
		{
			m_RigidBody->GetRigidBodyInternal()->activate(true);
			btTransform transform = m_RigidBody->GetRigidBodyInternal()->getInterpolationWorldTransform();
			transform.setOrigin(ToBtVec3(newPos));
			transform.setRotation(btQuaternion::getIdentity());
			m_RigidBody->GetRigidBodyInternal()->setInterpolationWorldTransform(transform);
		}

		btVector3 startPos = ToBtVec3(startingPos);
		g_Renderer->GetDebugDrawer()->drawLine(
			startPos,
			startPos + ToBtVec3(moveAxis * maxDist),
			btVector3(1, 1, 1));
		if (minDist < 0.0f)
		{
			g_Renderer->GetDebugDrawer()->drawLine(
				startPos,
				startPos + ToBtVec3(moveAxis * minDist),
				btVector3(0.99f, 0.6f, 0.6f));
		}

		g_Renderer->GetDebugDrawer()->drawLine(
			startPos,
			startPos + ToBtVec3(moveAxis * dist),
			btVector3(0.3f, 0.3f, 0.5f));

		GameObject::Update();
	}

	GlassPane::GlassPane(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("glass pane"), gameObjectID)
	{
		m_bItemizable = true;
	}

	GameObject* GlassPane::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, newObjectName, newGameObjectID);
		GlassPane* newGameObject = new GlassPane(newObjectName, newGameObjectID);

		newGameObject->bBroken = bBroken;

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void GlassPane::ParseTypeUniqueFields(const JSONObject& parentObj, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(scene);
		FLEX_UNUSED(matIDs);

		JSONObject glassInfo;
		if (parentObj.SetObjectChecked("window info", glassInfo))
		{
			glassInfo.SetBoolChecked("broken", bBroken);

			if (!m_Mesh)
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

		if (!m_RigidBody)
		{
			RigidBody* rigidBody = SetRigidBody(new RigidBody());
			rigidBody->SetMass(1.0f);
			rigidBody->SetKinematic(true);
			rigidBody->SetStatic(false);
		}
	}

	void GlassPane::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		JSONObject windowInfo = {};

		windowInfo.fields.emplace_back("broken", JSONValue(bBroken));

		parentObject.fields.emplace_back("window info", JSONValue(windowInfo));
	}

	ReflectionProbe::ReflectionProbe(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("reflection probe"), gameObjectID)
	{
	}

	GameObject* ReflectionProbe::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, newObjectName, newGameObjectID);
		ReflectionProbe* newGameObject = new ReflectionProbe(newObjectName, newGameObjectID);

		newGameObject->captureMatID = captureMatID;

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void ReflectionProbe::ParseTypeUniqueFields(const JSONObject& parentObj, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(scene);
		FLEX_UNUSED(parentObj);
		FLEX_UNUSED(matIDs);

		// Probe capture material
		//MaterialCreateInfo probeCaptureMatCreateInfo = {};
		//probeCaptureMatCreateInfo.name = "reflection probe capture";
		//probeCaptureMatCreateInfo.shaderName = "deferred_combine_cubemap";
		//probeCaptureMatCreateInfo.generateReflectionProbeMaps = true;
		//probeCaptureMatCreateInfo.generateHDRCubemapSampler = true;
		//probeCaptureMatCreateInfo.generatedCubemapSize = glm::vec2(512.0f, 512.0f); // TODO: Add support for non-512.0f size
		//probeCaptureMatCreateInfo.generateCubemapDepthBuffers = true;
		//probeCaptureMatCreateInfo.generateIrradianceSampler = true;
		//probeCaptureMatCreateInfo.generatedIrradianceCubemapSize = { 32, 32 };
		//probeCaptureMatCreateInfo.enablePrefilteredMap = true;
		//probeCaptureMatCreateInfo.generatePrefilteredMap = true;
		//probeCaptureMatCreateInfo.generatedPrefilteredCubemapSize = { 128, 128 };
		//probeCaptureMatCreateInfo.enableBRDFLUT = true;
		//probeCaptureMatCreateInfo.persistent = true;
		//probeCaptureMatCreateInfo.visibleInEditor = false;
		//probeCaptureMatCreateInfo.sampledFrameBuffers = {
		//	{ "positionMetallicFrameBufferSampler", nullptr },
		//	{ "normalRoughnessFrameBufferSampler", nullptr },
		//	{ "albedoAOFrameBufferSampler", nullptr },
		//};
		//captureMatID = g_Renderer->InitializeMaterial(&probeCaptureMatCreateInfo);

		//MeshComponent* sphereMesh = new MeshComponent(this, matID);

		//assert(m_MeshComponent == nullptr);
		//sphereMesh->LoadFromFile(MESH_DIRECTORY "sphere.glb");
		//SetMeshComponent(sphereMesh);

		//std::string captureName = m_Name + "_capture";
		//GameObject* captureObject = new GameObject(captureName, SID("_NONE);
		//captureObject->SetSerializable(false);
		//captureObject->SetVisible(false);
		//captureObject->SetVisibleInSceneExplorer(false);

		//RenderObjectCreateInfo captureObjectCreateInfo = {};
		//captureObjectCreateInfo.vertexBufferData = nullptr;
		//captureObjectCreateInfo.materialID = captureMatID;
		//captureObjectCreateInfo.gameObject = captureObject;
		//captureObjectCreateInfo.visibleInSceneExplorer = false;

		//RenderID captureRenderID = g_Renderer->InitializeRenderObject(&captureObjectCreateInfo);
		//captureObject->SetRenderID(captureRenderID);

		//AddChild(captureObject);

		//g_Renderer->SetReflectionProbeMaterial(captureMatID);
	}

	void ReflectionProbe::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		FLEX_UNUSED(parentObject);

		// Reflection probes have no unique fields currently
	}

	void ReflectionProbe::PostInitialize()
	{
		GameObject::PostInitialize();

		g_Renderer->SetReflectionProbeMaterial(captureMatID);
	}

	Skybox::Skybox(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("skybox"), gameObjectID)
	{
		SetCastsShadow(false);
		m_bSerializeMesh = false;
	}

	void Skybox::ProcedurallyInitialize(MaterialID matID)
	{
		InternalInit(matID);
	}

	void Skybox::ParseTypeUniqueFields(const JSONObject& parentObj, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(scene);
		assert(matIDs.size() == 1);

		JSONObject skyboxInfo;
		if (parentObj.SetObjectChecked("skybox info", skyboxInfo))
		{
			glm::vec3 rotEuler;
			if (skyboxInfo.SetVec3Checked("rot", rotEuler))
			{
				m_Transform.SetWorldRotation(glm::quat(rotEuler));
			}
		}

		InternalInit(matIDs[0]);
	}

	void Skybox::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		JSONObject skyboxInfo = {};
		glm::quat worldRot = m_Transform.GetWorldRotation();
		if (worldRot != QUAT_IDENTITY)
		{
			std::string eulerRotStr = VecToString(glm::eulerAngles(worldRot), 2);
			skyboxInfo.fields.emplace_back("rot", JSONValue(eulerRotStr));
		}

		parentObject.fields.emplace_back("skybox info", JSONValue(skyboxInfo));
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

	DirectionalLight::DirectionalLight() :
		DirectionalLight("Directional Light")
	{
	}

	DirectionalLight::DirectionalLight(const std::string& name, const glm::vec3& initialPos, const glm::quat& initialOrientation) :
		GameObject(name, SID("directional light"), InvalidGameObjectID)
	{
		m_Transform.SetWorldPosition(initialPos);

		data.enabled = m_bVisible ? 1 : 0;
		data.dir = glm::rotate(initialOrientation, VEC3_RIGHT);
		data.colour = VEC3_ONE;
		data.brightness = 1.0f;
		data.castShadows = 1;
		data.shadowDarkness = 1.0f;
		data.pad[0] = data.pad[1] = 0;
	}

	DirectionalLight::DirectionalLight(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("directional light"), gameObjectID)
	{
		data.enabled = m_bVisible ? 1 : 0;
		data.dir = VEC3_RIGHT;
		data.colour = VEC3_ONE;
		data.brightness = 1.0f;
		data.castShadows = 1;
		data.shadowDarkness = 1.0f;
		data.pad[0] = data.pad[1] = 0;
	}

	void DirectionalLight::Initialize()
	{
		g_Renderer->RegisterDirectionalLight(this);
		data.dir = glm::rotate(m_Transform.GetWorldRotation(), VEC3_RIGHT);

		m_Transform.updateParentOnStateChange = true;

		GameObject::Initialize();
	}

	void DirectionalLight::Destroy(bool bDetachFromParent /* = true */)
	{
		g_Renderer->RemoveDirectionalLight();

		GameObject::Destroy(bDetachFromParent);
	}

	void DirectionalLight::Update()
	{
		if (sockets.size() >= 1)
		{
			i32 receivedSignal = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->GetReceivedSignal(sockets[0]);
			if (receivedSignal != -1)
			{
				m_bVisible = receivedSignal == 1;
				data.enabled = m_bVisible ? 1 : 0;
			}
		}

		if (data.enabled && g_EngineInstance->IsRenderingEditorObjects())
		{
			BaseCamera* cam = g_CameraManager->CurrentCamera();

			if (!cam->bIsGameplayCam)
			{
				// Debug lines
				{
					PhysicsDebugDrawBase* debugDrawer = g_Renderer->GetDebugDrawer();

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
						debugDrawer->drawLine(lightPos, lineEnd, lineColour);
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

	void DirectionalLight::DrawImGuiObjects()
	{
		GameObject::DrawImGuiObjects();

		static const ImGuiColorEditFlags colorEditFlags =
			ImGuiColorEditFlags_NoInputs |
			ImGuiColorEditFlags_Float |
			ImGuiColorEditFlags_RGB |
			ImGuiColorEditFlags_PickerHueWheel |
			ImGuiColorEditFlags_HDR;

		ImGui::Text("Directional Light");

		if (ImGui::Checkbox("Enabled", &m_bVisible))
		{
			data.enabled = m_bVisible ? 1 : 0;
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

	void DirectionalLight::OnTransformChanged()
	{
		pos = m_Transform.GetLocalPosition();
		data.dir = glm::rotate(m_Transform.GetWorldRotation(), -VEC3_FORWARD);
	}

	void DirectionalLight::ParseTypeUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		i32 sceneFileVersion = scene->GetSceneFileVersion();

		JSONObject directionalLightObj;
		if (parentObject.SetObjectChecked("directional light info", directionalLightObj))
		{
			std::string rotStr = directionalLightObj.GetString("rotation");
			if (sceneFileVersion >= 2)
			{
				m_Transform.SetWorldRotation(ParseQuat(rotStr));
				data.dir = glm::rotate(m_Transform.GetWorldRotation(), VEC3_RIGHT);
			}
			else
			{
				glm::quat rot(ParseVec3(rotStr));
				m_Transform.SetWorldRotation(rot);
			}

			std::string posStr = directionalLightObj.GetString("pos");
			if (!posStr.empty())
			{
				m_Transform.SetLocalPosition(ParseVec3(posStr));
				pos = m_Transform.GetWorldPosition();
			}

			directionalLightObj.SetVec3Checked("colour", data.colour);

			directionalLightObj.SetFloatChecked("brightness", data.brightness);

			if (directionalLightObj.HasField("enabled"))
			{
				m_bVisible = directionalLightObj.GetBool("enabled") ? 1 : 0;
			}

			bool bCastShadow = false;
			if (directionalLightObj.SetBoolChecked("cast shadows", bCastShadow))
			{
				data.castShadows = bCastShadow ? 1 : 0;
			}
			directionalLightObj.SetFloatChecked("shadow darkness", data.shadowDarkness);
		}
	}

	void DirectionalLight::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		JSONObject dirLightObj = {};

		std::string dirStr = QuatToString(m_Transform.GetWorldRotation(), 3);
		dirLightObj.fields.emplace_back("rotation", JSONValue(dirStr));

		std::string posStr = VecToString(m_Transform.GetLocalPosition(), 3);
		dirLightObj.fields.emplace_back("pos", JSONValue(posStr));

		std::string colourStr = VecToString(data.colour, 2);
		dirLightObj.fields.emplace_back("colour", JSONValue(colourStr));

		dirLightObj.fields.emplace_back("enabled", JSONValue(m_bVisible != 0));
		dirLightObj.fields.emplace_back("brightness", JSONValue(data.brightness));

		dirLightObj.fields.emplace_back("cast shadows", JSONValue(static_cast<bool>(data.castShadows)));
		dirLightObj.fields.emplace_back("shadow darkness", JSONValue(data.shadowDarkness));

		parentObject.fields.emplace_back("directional light info", JSONValue(dirLightObj));
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

	PointLight::PointLight(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("point light"), gameObjectID)
	{
		data.enabled = 1;
		data.pos = m_Transform.GetWorldPosition();
		data.colour = VEC4_ONE;
		data.brightness = 500.0f;
	}

	GameObject* PointLight::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, newObjectName, newGameObjectID);

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
		pointLightID = g_Renderer->RegisterPointLight(&data);

		m_Transform.updateParentOnStateChange = true;

		GameObject::Initialize();
	}

	void PointLight::Destroy(bool bDetachFromParent /* = true */)
	{
		g_Renderer->RemovePointLight(pointLightID);

		GameObject::Destroy(bDetachFromParent);
	}

	void PointLight::Update()
	{
		if (sockets.size() >= 1)
		{
			i32 receivedSignal = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->GetReceivedSignal(sockets[0]);
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

	void PointLight::DrawImGuiObjects()
	{
		GameObject::DrawImGuiObjects();

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
				if (ImGui::Checkbox("Enabled", &m_bVisible))
				{
					bEditedPointLightData = true;
					data.enabled = m_bVisible ? 1 : 0;
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

	void PointLight::OnTransformChanged()
	{
		data.pos = m_Transform.GetWorldPosition();
		if (pointLightID != InvalidPointLightID)
		{
			g_Renderer->UpdatePointLightData(pointLightID, &data);
		}
	}

	void PointLight::ParseTypeUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(scene);
		FLEX_UNUSED(matIDs);

		JSONObject pointLightObj;
		if (parentObject.SetObjectChecked("point light info", pointLightObj))
		{
			std::string posStr = pointLightObj.GetString("pos");
			glm::vec3 pos = glm::vec3(ParseVec3(posStr));
			m_Transform.SetLocalPosition(pos);
			data.pos = pos;

			pointLightObj.SetVec3Checked("colour", data.colour);

			pointLightObj.SetFloatChecked("brightness", data.brightness);

			if (pointLightObj.HasField("enabled"))
			{
				m_bVisible = pointLightObj.GetBool("enabled") ? 1 : 0;
				data.enabled = m_bVisible ? 1 : 0;
			}
		}
	}

	void PointLight::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		JSONObject pointLightObj = {};

		std::string posStr = VecToString(m_Transform.GetLocalPosition(), 3);
		pointLightObj.fields.emplace_back("pos", JSONValue(posStr));

		std::string colourStr = VecToString(data.colour, 2);
		pointLightObj.fields.emplace_back("colour", JSONValue(colourStr));

		pointLightObj.fields.emplace_back("enabled", JSONValue(m_bVisible != 0));
		pointLightObj.fields.emplace_back("brightness", JSONValue(data.brightness));

		parentObject.fields.emplace_back("point light info", JSONValue(pointLightObj));
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

	SpotLight::SpotLight(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("spot light"), gameObjectID)
	{
		data.enabled = 1;
		data.pos = m_Transform.GetWorldPosition();
		data.colour = VEC4_ONE;
		data.brightness = 500.0f;
		data.dir = VEC3_RIGHT;
		data.angle = 0.1f;
	}

	GameObject* SpotLight::CopySelf(GameObject* parent, CopyFlags copyFlags, std::string* optionalName, const GameObjectID& optionalGameObjectID)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, newObjectName, newGameObjectID);

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
		spotLightID = g_Renderer->RegisterSpotLight(&data);

		m_Transform.updateParentOnStateChange = true;

		GameObject::Initialize();
	}

	void SpotLight::Destroy(bool bDetachFromParent)
	{
		g_Renderer->RemoveSpotLight(spotLightID);

		GameObject::Destroy(bDetachFromParent);
	}

	void SpotLight::Update()
	{
		if (sockets.size() >= 1)
		{
			i32 receivedSignal = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->GetReceivedSignal(sockets[0]);
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

	void SpotLight::DrawImGuiObjects()
	{
		GameObject::DrawImGuiObjects();

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
				if (ImGui::Checkbox("Enabled", &m_bVisible))
				{
					bEditedSpotLightData = true;
					data.enabled = m_bVisible ? 1 : 0;
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
		OnTransformChanged();
	}

	void SpotLight::OnTransformChanged()
	{
		data.dir = glm::rotate(m_Transform.GetWorldRotation(), -VEC3_FORWARD);
		data.pos = m_Transform.GetWorldPosition();
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

	void SpotLight::ParseTypeUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(scene);
		FLEX_UNUSED(matIDs);

		JSONObject spotLightObj;
		if (parentObject.SetObjectChecked("spot light info", spotLightObj))
		{
			std::string posStr = spotLightObj.GetString("pos");
			glm::vec3 pos = glm::vec3(ParseVec3(posStr));
			m_Transform.SetLocalPosition(pos);
			data.pos = pos;

			spotLightObj.SetVec3Checked("colour", data.colour);

			spotLightObj.SetFloatChecked("brightness", data.brightness);

			if (spotLightObj.HasField("enabled"))
			{
				m_bVisible = spotLightObj.GetBool("enabled") ? 1 : 0;
				data.enabled = m_bVisible ? 1 : 0;
			}

			spotLightObj.SetVec3Checked("direction", data.dir);
			spotLightObj.SetFloatChecked("angle", data.angle);
		}
	}

	void SpotLight::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		JSONObject spotLightObj = {};

		std::string posStr = VecToString(m_Transform.GetLocalPosition(), 3);
		spotLightObj.fields.emplace_back("pos", JSONValue(posStr));

		std::string colourStr = VecToString(data.colour, 2);
		spotLightObj.fields.emplace_back("colour", JSONValue(colourStr));

		spotLightObj.fields.emplace_back("enabled", JSONValue(m_bVisible != 0));
		spotLightObj.fields.emplace_back("brightness", JSONValue(data.brightness));

		spotLightObj.fields.emplace_back("direction", JSONValue(VecToString(data.dir)));
		spotLightObj.fields.emplace_back("angle", JSONValue(data.angle));

		parentObject.fields.emplace_back("spot light info", JSONValue(spotLightObj));
	}

	AreaLight::AreaLight(BaseScene* scene) :
		AreaLight(scene->GetUniqueObjectName("AreaLight_", 2))
	{
	}

	AreaLight::AreaLight(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("area light"), gameObjectID)
	{
		data.enabled = 1;
		data.colour = VEC4_ONE;
		data.brightness = 500.0f;
		UpdatePoints();
	}

	GameObject* AreaLight::CopySelf(GameObject* parent, CopyFlags copyFlags, std::string* optionalName, const GameObjectID& optionalGameObjectID)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, newObjectName, newGameObjectID);

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
		areaLightID = g_Renderer->RegisterAreaLight(&data);

		m_Transform.updateParentOnStateChange = true;

		GameObject::Initialize();
	}

	void AreaLight::Destroy(bool bDetachFromParent)
	{
		g_Renderer->RemoveAreaLight(areaLightID);

		GameObject::Destroy(bDetachFromParent);
	}

	void AreaLight::Update()
	{
		if (sockets.size() >= 1)
		{
			i32 receivedSignal = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->GetReceivedSignal(sockets[0]);
			if (receivedSignal != -1)
			{
				m_bVisible = receivedSignal == 1;
				data.enabled = m_bVisible ? 1 : 0;
				g_Renderer->UpdateAreaLightData(areaLightID, &data);
			}
		}

		if (data.enabled)
		{
			UpdatePoints();

			g_Renderer->UpdateAreaLightData(areaLightID, &data);

			if (g_EngineInstance->IsRenderingEditorObjects())
			{
				PhysicsDebugDrawBase* debugDrawer = g_Renderer->GetDebugDrawer();

				btVector3 lineColour(0.1f, 0.8f, 0.1f);
				btVector3 p0(ToBtVec3(data.points[0]));
				btVector3 p1(ToBtVec3(data.points[1]));
				btVector3 p2(ToBtVec3(data.points[2]));
				btVector3 p3(ToBtVec3(data.points[3]));
				btVector3 center(ToBtVec3((data.points[0] + data.points[1] + data.points[2] + data.points[3]) / 4.0f));
				glm::vec3 dir = glm::normalize(glm::cross(glm::vec3(data.points[1]) - glm::vec3(data.points[0]), glm::vec3(data.points[2]) - glm::vec3(data.points[0])));
				btVector3 dirBt = ToBtVec3(dir);
				debugDrawer->drawLine(p0, p1, lineColour);
				debugDrawer->drawLine(p1, p2, lineColour);
				debugDrawer->drawLine(p2, p3, lineColour);
				debugDrawer->drawLine(p3, p0, lineColour);

				debugDrawer->drawLine(center, center + dirBt, lineColour);

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

	void AreaLight::DrawImGuiObjects()
	{
		GameObject::DrawImGuiObjects();

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
				if (ImGui::Checkbox("Enabled", &m_bVisible))
				{
					bEditedAreaLightData = true;
					data.enabled = m_bVisible ? 1 : 0;
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
		OnTransformChanged();
	}

	void AreaLight::OnTransformChanged()
	{
		UpdatePoints();

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

	void AreaLight::ParseTypeUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(scene);
		FLEX_UNUSED(matIDs);

		JSONObject areaLightObj;
		if (parentObject.SetObjectChecked("area light info", areaLightObj))
		{
			areaLightObj.SetVec3Checked("colour", data.colour);

			areaLightObj.SetFloatChecked("brightness", data.brightness);

			if (areaLightObj.HasField("enabled"))
			{
				m_bVisible = areaLightObj.GetBool("enabled");
				data.enabled = m_bVisible ? 1 : 0;
			}
		}
	}

	void AreaLight::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		JSONObject areaLightObj = {};

		std::string colourStr = VecToString(data.colour, 2);
		areaLightObj.fields.emplace_back("colour", JSONValue(colourStr));

		areaLightObj.fields.emplace_back("enabled", JSONValue(m_bVisible != 0));
		areaLightObj.fields.emplace_back("brightness", JSONValue(data.brightness));

		parentObject.fields.emplace_back("area light info", JSONValue(areaLightObj));
	}

	void AreaLight::UpdatePoints()
	{
		const glm::mat4& model = m_Transform.GetWorldTransform();
		data.points[0] = model * glm::vec4(-1.0f, 1.0f, 0.0f, 1.0f);
		data.points[1] = model * glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
		data.points[2] = model * glm::vec4(1.0f, -1.0f, 0.0f, 1.0f);
		data.points[3] = model * glm::vec4(-1.0f, -1.0f, 0.0f, 1.0f);
	}

	Cart::Cart(CartID cartID) :
		Cart(cartID, g_SceneManager->CurrentScene()->GetUniqueObjectName("Cart_", 4))
	{
	}

	Cart::Cart(CartID cartID,
		const std::string& name,
		const GameObjectID& gameObjectID /* = InvalidGameObjectID */,
		StringID typeID /* = SID('cart') */,
		const char* meshName /* = emptyCartMeshName */,
		bool bPrefabTemplate /* = false */) :
		GameObject(name, typeID, gameObjectID),
		cartID(cartID)
	{
		m_bItemizable = true;
		m_bSerializeMesh = false;
		m_bSerializeMaterial = false;

		MaterialID matID;
		if (!g_Renderer->FindOrCreateMaterialByName("pbr grey", matID))
		{
			// :shrug:
			// TODO: Create own material
			matID = 0;
		}
		Mesh* mesh = SetMesh(new Mesh(this));
		std::string meshFilePath = std::string(MESH_DIRECTORY) + std::string(meshName);
		bool bCreateRenderObject = !bPrefabTemplate;
		if (!mesh->LoadFromFile(meshFilePath, matID, false, bCreateRenderObject))
		{
			PrintWarn("Failed to load cart mesh!\n");
		}

		m_TSpringToCartAhead.DR = 1.0f;
	}

	GameObject* Cart::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, newObjectName, newGameObjectID);

		// TODO: FIXME: Get newly generated cart ID! & move allocation into cart manager
		bool bCopyingToPrefabTemplate = copyFlags & CopyFlags::COPYING_TO_PREFAB;
		Cart* newGameObject = new Cart(cartID, newObjectName, newGameObjectID, m_TypeID, emptyCartMeshName, bCopyingToPrefabTemplate);

		newGameObject->currentTrackID = currentTrackID;
		newGameObject->distAlongTrack = distAlongTrack;

		copyFlags = (CopyFlags)(copyFlags & ~CopyFlags::MESH);
		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void Cart::DrawImGuiObjects()
	{
		GameObject::DrawImGuiObjects();

		if (ImGui::TreeNode("Cart"))
		{
			ImGui::Text("track ID: %d", currentTrackID);
			ImGui::Text("dist along track: %.2f", distAlongTrack);

			ImGui::TreePop();
		}
	}

	real Cart::GetDrivePower() const
	{
		return 0.0f;
	}

	void Cart::OnTrackMount(TrackID trackID, real newDistAlongTrack)
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
			assert(newTrackID == trackID);

			distAlongTrack = newDistAlongTrack;
			m_Transform.SetLocalPosition(newPos);

			velocityT = (distAlongTrack > 0.5f ? -1.0f : 1.0f);
		}
	}

	void Cart::OnTrackDismount()
	{
		currentTrackID = InvalidTrackID;
		distAlongTrack = -1.0f;
	}

	void Cart::SetItemHolding(GameObject* obj)
	{
		FLEX_UNUSED(obj);
		// TODO:
	}

	void Cart::RemoveItemHolding()
	{
	}

	void Cart::AdvanceAlongTrack(real dT)
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

	real Cart::UpdatePosition()
	{
		if (currentTrackID != InvalidTrackID)
		{
			if (chainID != InvalidCartChainID)
			{
				TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
				CartManager* cartManager = GetSystem<CartManager>(SystemType::CART_MANAGER);

				//real targetT = trackManager->GetCartTargetDistAlongTrackInChain(chainID, cartID);
				//assert(targetT != -1.0f);

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

	void Cart::ParseTypeUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(scene);
		FLEX_UNUSED(matIDs);

		JSONObject cartInfo = parentObject.GetObject("cart info");
		currentTrackID = (TrackID)cartInfo.GetInt("track ID");
		distAlongTrack = cartInfo.GetFloat("dist along track");
	}

	void Cart::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		JSONObject cartInfo = {};

		cartInfo.fields.emplace_back("track ID", JSONValue(currentTrackID));
		cartInfo.fields.emplace_back("dist along track", JSONValue(distAlongTrack));

		parentObject.fields.emplace_back("cart info", JSONValue(cartInfo));
	}

	EngineCart::EngineCart(CartID cartID) :
		EngineCart(cartID, g_SceneManager->CurrentScene()->GetUniqueObjectName("EngineCart_", 4))
	{
	}

	EngineCart::EngineCart(CartID cartID,
		const std::string& name,
		const GameObjectID& gameObjectID /* = InvalidGameObjectID */,
		bool bPrefabTemplate /* = false */) :
		Cart(cartID, name, gameObjectID, SID("engine cart"), engineMeshName, bPrefabTemplate)
	{
	}

	GameObject* EngineCart::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, newObjectName, newGameObjectID);

		// TODO: FIXME: Get newly generated cart ID! & move allocation into cart manager
		EngineCart* newGameObject = new EngineCart(cartID, newObjectName, newGameObjectID);

		newGameObject->powerRemaining = powerRemaining;

		newGameObject->currentTrackID = currentTrackID;
		newGameObject->distAlongTrack = distAlongTrack;

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void EngineCart::Update()
	{
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

	void EngineCart::DrawImGuiObjects()
	{
		GameObject::DrawImGuiObjects();

		if (ImGui::TreeNode("Engine Cart"))
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

	void EngineCart::ParseTypeUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(scene);
		FLEX_UNUSED(matIDs);

		JSONObject cartInfo = parentObject.GetObject("cart info");
		currentTrackID = (TrackID)cartInfo.GetInt("track ID");
		distAlongTrack = cartInfo.GetFloat("dist along track");

		moveDirection = cartInfo.GetFloat("move direction");
		powerRemaining = cartInfo.GetFloat("power remaining");
	}

	void EngineCart::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		JSONObject cartInfo = {};

		cartInfo.fields.emplace_back("track ID", JSONValue(currentTrackID));
		cartInfo.fields.emplace_back("dist along track", JSONValue(distAlongTrack));

		cartInfo.fields.emplace_back("move direction", JSONValue(moveDirection));
		cartInfo.fields.emplace_back("power remaining", JSONValue(powerRemaining));

		parentObject.fields.emplace_back("cart info", JSONValue(cartInfo));
	}

	MobileLiquidBox::MobileLiquidBox() :
		// TODO: Extract string to m_UniqueObjectNamePrefix
		MobileLiquidBox(g_SceneManager->CurrentScene()->GetUniqueObjectName("MobileLiquidBox_", 4), InvalidGameObjectID)
	{
	}

	MobileLiquidBox::MobileLiquidBox(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("mobile liquid box"), gameObjectID)
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

	GameObject* MobileLiquidBox::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, newObjectName, newGameObjectID);
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
		std::vector<Cart*> carts = scene->GetObjectsOfType<Cart>(SID("cart"));
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

	void MobileLiquidBox::DrawImGuiObjects()
	{
		GameObject::DrawImGuiObjects();

		if (ImGui::TreeNode("Mobile liquid box"))
		{
			ImGui::Text("In cart: %d", bInCart ? 1 : 0);
			ImGui::Text("Liquid amount: %.2f", liquidAmount);

			ImGui::TreePop();
		}
	}

	void MobileLiquidBox::ParseTypeUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(parentObject);
		FLEX_UNUSED(scene);
		FLEX_UNUSED(matIDs);
	}

	void MobileLiquidBox::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		FLEX_UNUSED(parentObject);
	}

	GerstnerWave::GerstnerWave(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("gerstner wave"), gameObjectID)
	{
		m_bSerializeMesh = false;
		m_bSerializeMaterial = false;
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
		matCreateInfo.constAlbedo = glm::vec3(0.4f, 0.5f, 0.8f);
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
		bobber = new GameObject("Bobber", SID("object"));
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

	void GerstnerWave::Initialize()
	{
		m_VertexBufferCreateInfo = {};
		m_VertexBufferCreateInfo.attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(m_WaveMaterialID)->shaderID)->vertexAttributes;

		avgWaveUpdateTime = RollingAverage<ms>(256, SamplingType::CONSTANT);

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

		m_Mesh->LoadFromMemoryDynamic(m_VertexBufferCreateInfo, m_Indices, m_WaveMaterialID, (u32)m_VertexBufferCreateInfo.positions_3D.size());

		GameObject::Initialize();
	}

	void GerstnerWave::Update()
	{
		if (!m_bVisible)
		{
			return;
		}

		PROFILE_AUTO("Gerstner update");

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
				g_Renderer->GetDebugDrawer()->drawSphere(btVector3((chunkIndex.x) * size, 8.0f + 1.5f * j, (chunkIndex.y) * size), 0.5f, btVector4(1, 1, 1, 1));
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

		g_Renderer->SetGlobalUniform(U_OCEAN_DATA, &oceanData, sizeof(oceanData));

		GameObject::Update();
	}

	void GerstnerWave::Destroy(bool bDetachFromParent /* = true */)
	{
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

				//if (g_Renderer->GetDebugDrawer() != nullptr)
				//{
				//	g_Renderer->GetDebugDrawer()->drawSphere(btVector3(chunkCenter.x, 1.0f, chunkCenter.y), 0.5f, btVector3(0.75f, 0.5f, 0.6f));
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

		assert((*wave_workQueue)[workQueueIndex].positionsx_4 == nullptr);

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
		ms waveTime = Profiler::GetBlockDuration("Update waves");
		if (avgWaveUpdateTime.currentAverage == 0.0f)
		{
			avgWaveUpdateTime.Reset(waveTime);
		}
		else
		{
			avgWaveUpdateTime.AddValue(waveTime);
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

						assert(wave_workQueueEntriesClaimed <= wave_workQueueEntriesCreated);
						assert(wave_workQueueEntriesClaimed <= wave_workQueue->Size());
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

		const real quadSize = size / chunkVertCountPerAxis;
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
				normals[vertIdx] = glm::vec3(dX, 2.0f * quadSize, dZ);
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

	void GerstnerWave::DrawImGuiObjects()
	{
		GameObject::DrawImGuiObjects();

		ImGui::Text("Loaded chunks: %d", (u32)waveChunks.size());

		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.95f, 1.0f), "Gerstner");

		{
			ImVec2 p = ImGui::GetCursorScreenPos();

			real width = 300.0f;
			real height = 100.0f;
			real minMS = 0.0f;
			real maxMS = 40.0f;
			p.y += glm::clamp((1.0f - avgWaveUpdateTime.currentAverage / (maxMS - minMS)), 0.0f, 1.0f) * height;
			ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + width, p.y), IM_COL32(240, 220, 20, 255), 1.0f);

			ImGui::PlotLines("", avgWaveUpdateTime.prevValues.data(), (u32)avgWaveUpdateTime.prevValues.size(), 0, 0, minMS, maxMS, ImVec2(width, height));

			ImGui::Text("%.2fms", avgWaveUpdateTime.currentAverage);
		}

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
				ByteCountToString(byteCountStr, 64, (u32)(m_VertexBufferCreateInfo.positions_3D.size() * sizeof(glm::vec3)));

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

	void GerstnerWave::ParseTypeUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject gerstnerWaveObj;
		if (parentObject.SetObjectChecked("gerstner wave", gerstnerWaveObj))
		{
			std::vector<JSONObject> waveObjs;
			if (gerstnerWaveObj.SetObjectArrayChecked("waves", waveObjs))
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

			gerstnerWaveObj.SetUIntChecked("max chunk vert count per axis", maxChunkVertCountPerAxis);
			gerstnerWaveObj.SetFloatChecked("chunk size", size);
			gerstnerWaveObj.SetFloatChecked("chunk load radius", loadRadius);
			gerstnerWaveObj.SetFloatChecked("update speed", updateSpeed);

			gerstnerWaveObj.SetBoolChecked("pin center", m_bPinCenter);
			gerstnerWaveObj.SetVec3Checked("pinned center position", m_PinnedPos);

			gerstnerWaveObj.SetFloatChecked("blend dist", blendDist);

			std::vector<JSONField> waveSamplingLODsArrObj;
			if (gerstnerWaveObj.SetFieldArrayChecked("wave sampling lods", waveSamplingLODsArrObj))
			{
				waveSamplingLODs.clear();
				waveSamplingLODs.reserve(waveSamplingLODsArrObj.size());
				for (u32 i = 0; i < (u32)waveSamplingLODsArrObj.size(); ++i)
				{
					std::string samplingPropertyList = waveSamplingLODsArrObj[i].label;
					std::vector<std::string> strParts = Split(samplingPropertyList, ',');
					if (strParts.size() != 2)
					{
						std::string sceneName = scene->GetFileName();
						PrintError("Invalid wave sampling LOD cutoff pair (%s) in scene %s\n", samplingPropertyList.c_str(), sceneName.c_str());
						continue;
					}
					// TODO: Store as array of objects
					real dist = (real)atof(strParts[0].c_str());
					real amplitude = (real)atof(strParts[1].c_str());
					waveSamplingLODs.emplace_back(dist, amplitude);
				}

				SortWaveSamplingLODs();
			}

			std::vector<JSONField> waveTessellationLODsArrObj;
			if (gerstnerWaveObj.SetFieldArrayChecked("wave tessellation lods", waveTessellationLODsArrObj))
			{
				waveTessellationLODs.clear();
				waveTessellationLODs.reserve(waveTessellationLODsArrObj.size());
				for (u32 i = 0; i < (u32)waveTessellationLODsArrObj.size(); ++i)
				{
					std::string tessellationPropertyList = waveTessellationLODsArrObj[i].label;
					std::vector<std::string> strParts = Split(tessellationPropertyList, ',');
					if (strParts.size() != 2)
					{
						std::string sceneName = scene->GetFileName();
						PrintError("Invalid wave tessellation LOD cutoff pair (%s) in scene %s\n", tessellationPropertyList.c_str(), sceneName.c_str());
						continue;
					}
					// TODO: Store as array of objects
					real sqrDist = (real)atof(strParts[0].c_str());
					u32 vertCount = (u32)atoi(strParts[1].c_str());
					waveTessellationLODs.emplace_back(sqrDist, vertCount);
				}
			}

			if (gerstnerWaveObj.SetVec4Checked("colour top", oceanData.top))
			{
				oceanData.top = glm::pow(oceanData.top, glm::vec4(2.2f));
			}
			if (gerstnerWaveObj.SetVec4Checked("colour mid", oceanData.mid))
			{
				oceanData.mid = glm::pow(oceanData.mid, glm::vec4(2.2f));
			}
			if (gerstnerWaveObj.SetVec4Checked("colour btm", oceanData.btm))
			{
				oceanData.btm = glm::pow(oceanData.btm, glm::vec4(2.2f));
			}
			gerstnerWaveObj.SetFloatChecked("fresnel factor", oceanData.fresnelFactor);
			gerstnerWaveObj.SetFloatChecked("fresnel power", oceanData.fresnelPower);
			gerstnerWaveObj.SetFloatChecked("sky reflection factor", oceanData.skyReflectionFactor);
			gerstnerWaveObj.SetFloatChecked("fog falloff", oceanData.fogFalloff);
			gerstnerWaveObj.SetFloatChecked("fog density", oceanData.fogDensity);
		}

		SortWaves();

		// Init dependent variables
		for (i32 i = 0; i < (i32)waveContributions.size(); ++i)
		{
			UpdateDependentVariables(i);
		}
	}

	void GerstnerWave::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
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
		gerstnerWaveObj.fields.emplace_back("pinned center position", JSONValue(VecToString(m_PinnedPos)));

		gerstnerWaveObj.fields.emplace_back("blend dist", JSONValue(blendDist));

		std::vector<JSONField> waveSamplingLODsArrObj(waveSamplingLODs.size());
		for (u32 i = 0; i < (u32)waveSamplingLODs.size(); ++i)
		{
			waveSamplingLODsArrObj[i] = JSONField(FloatToString(waveSamplingLODs[i].squareDist, 1) + "," + FloatToString(waveSamplingLODs[i].amplitudeCutoff, 6), JSONValue(0));
		}
		gerstnerWaveObj.fields.emplace_back("wave sampling lods", JSONValue(waveSamplingLODsArrObj));

		std::vector<JSONField> waveTessellationLODsArrObj(waveTessellationLODs.size());
		for (u32 i = 0; i < (u32)waveTessellationLODs.size(); ++i)
		{
			waveTessellationLODsArrObj[i] = JSONField(FloatToString(waveTessellationLODs[i].squareDist, 1) + "," + IntToString(waveTessellationLODs[i].vertCountPerAxis), JSONValue(0));
		}
		gerstnerWaveObj.fields.emplace_back("wave tessellation lods", JSONValue(waveTessellationLODsArrObj));

		gerstnerWaveObj.fields.emplace_back("colour top", JSONValue(VecToString(glm::pow(oceanData.top, glm::vec4(1.0f / 2.2f)))));
		gerstnerWaveObj.fields.emplace_back("colour mid", JSONValue(VecToString(glm::pow(oceanData.mid, glm::vec4(1.0f / 2.2f)))));
		gerstnerWaveObj.fields.emplace_back("colour btm", JSONValue(VecToString(glm::pow(oceanData.btm, glm::vec4(1.0f / 2.2f)))));
		gerstnerWaveObj.fields.emplace_back("fresnel factor", JSONValue(oceanData.fresnelFactor));
		gerstnerWaveObj.fields.emplace_back("fresnel power", JSONValue(oceanData.fresnelPower));
		gerstnerWaveObj.fields.emplace_back("sky reflection factor", JSONValue(oceanData.skyReflectionFactor));
		gerstnerWaveObj.fields.emplace_back("fog falloff", JSONValue(oceanData.fogFalloff));
		gerstnerWaveObj.fields.emplace_back("fog density", JSONValue(oceanData.fogDensity));

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

	Blocks::Blocks(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("blocks"), gameObjectID)
	{
		MaterialCreateInfo matCreateInfo = {};
		matCreateInfo.shaderName = "pbr";
		matCreateInfo.constMetallic = 0.0f;
		matCreateInfo.bSerializable = false;

		std::vector<MaterialID> matIDs;
		for (i32 i = 0; i < 10; ++i)
		{
			matCreateInfo.name = "block " + IntToString(i, 2);
			matCreateInfo.constAlbedo = glm::vec3(RandomFloat(0.3f, 0.6f), RandomFloat(0.4f, 0.8f), RandomFloat(0.4f, 0.7f));
			matCreateInfo.constRoughness = RandomFloat(0.0f, 1.0f);
			matIDs.push_back(g_Renderer->InitializeMaterial(&matCreateInfo));
		}

		real blockSize = 1.5f;
		i32 count = 18;
		for (i32 x = 0; x < count; ++x)
		{
			for (i32 z = 0; z < count; ++z)
			{
				GameObject* obj = new GameObject("block", SID("object"));
				obj->SetMesh(new Mesh(obj));
				obj->GetMesh()->LoadFromFile(MESH_DIRECTORY "cube.glb", PickRandomFrom(matIDs));
				AddChild(obj);
				obj->GetTransform()->SetLocalScale(glm::vec3(blockSize));
				obj->GetTransform()->SetLocalPosition(glm::vec3(
					((real)x / (real)count - 0.5f) * (blockSize * count),
					RandomFloat(0.0f, 1.2f),
					((real)z / (real)count - 0.5f) * (blockSize * count)));
			}
		}
	}

	void Blocks::Update()
	{
		GameObject::Update();
	}

	Wire::Wire(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("wire"), gameObjectID)
	{
		m_bInteractable = true;
		startPoint = glm::vec3(-1.0f, 1.0f, 0.0f);
		endPoint = glm::vec3(1.0f, 1.0f, 0.0f);
	}

	void Wire::Destroy(bool bDetachFromParent /* = true */)
	{
		GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->DestroyWire(this);

		GameObject::Destroy(bDetachFromParent);
	}

	void Wire::ParseTypeUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(scene);
		FLEX_UNUSED(matIDs);

		JSONObject obj = parentObject.GetObject("wire");
		obj.SetVec3Checked("startPoint", startPoint);
		obj.SetVec3Checked("endPoint", endPoint);
	}

	void Wire::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		JSONObject obj = {};

		obj.fields.emplace_back("startPoint", JSONValue(VecToString(startPoint)));
		obj.fields.emplace_back("endPoint", JSONValue(VecToString(endPoint)));

		parentObject.fields.emplace_back("wire", JSONValue(obj));
	}

	void Wire::PlugIn(Socket* socket)
	{
		if (!socket0ID.IsValid())
		{
			socket0ID = socket->ID;
		}
		else if (!socket1ID.IsValid())
		{
			socket1ID = socket->ID;
		}
		else
		{
			PrintError("Attempted to plug in to full socket!\n");
		}
	}

	void Wire::Unplug(Socket* socket)
	{
		if (socket0ID == socket->ID)
		{
			socket0ID = InvalidGameObjectID;
		}
		else if (socket1ID == socket->ID)
		{
			socket1ID = InvalidGameObjectID;
		}
		else
		{
			PrintError("Attempted to unplug socket that wasn't connected to wire\n");
		}
	}

	bool Wire::AllowInteractionWith(GameObject* gameObject)
	{
		switch (gameObject->GetTypeID())
		{
		case SID("socket"):
		{
			return true;
		} break;
		}

		return (!socket0ID.IsValid() && !socket1ID.IsValid());
	}

	void Wire::SetInteractingWith(GameObject* gameObject)
	{
		GameObject::SetInteractingWith(gameObject);
	}

	Socket::Socket(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("socket"), gameObjectID)
	{
		m_bInteractable = true;
	}

	void Socket::Destroy(bool bDetachFromParent /* = true */)
	{
		GetSystem<PluggablesSystem>(SystemType::PLUGGABLES)->DestroySocket(this);

		GameObject::Destroy(bDetachFromParent);
	}

	void Socket::ParseTypeUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(scene);
		FLEX_UNUSED(matIDs);

		JSONObject obj = parentObject.GetObject("socket");
		obj.SetIntChecked("slotIdx", slotIdx);

		// TODO: Serialize parent & wire reference once ObjectIDs are in

	}

	void Socket::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		JSONObject obj = {};

		obj.fields.emplace_back("slotIdx", JSONValue(slotIdx));

		// TODO: Serialize parent & wire reference once ObjectIDs are in

		parentObject.fields.emplace_back("socket", JSONValue(obj));
	}

	bool Socket::AllowInteractionWith(GameObject* gameObject)
	{
		switch (gameObject->GetTypeID())
		{
		case SID("player"):
		{
			//Player* player = (Player*)gameObject;
			//if (player->m_HeldItem != nullptr)
			//{
			//	return (player->m_HeldItem->GetTypeID() == SID("wire"));
			//}
			//else
			{
				return (connectedWire != nullptr);
			}
		} break;
		}

		return true;
	}

	void Socket::SetInteractingWith(GameObject* gameObject)
	{
		if (gameObject == nullptr)
		{
			if (connectedWire != nullptr)
			{
				connectedWire->Unplug(this);
				connectedWire = nullptr;
			}
			return;
		}

		switch (gameObject->GetTypeID())
		{
		case SID("player"):
		{
			//Player* player = (Player*)gameObject;
			//if (player->m_HeldItem != nullptr)
			//{
			//	if (player->m_HeldItem->GetTypeID() == SID("wire"))
			//	{
			//		Wire* wire = (Wire*)player->m_HeldItem;
			//		if (connectedWire == nullptr)
			//		{
			//			connectedWire = wire;
			//			wire->PlugIn(this);
			//		}
			//		else
			//		{
			//			wire->Unplug(this);
			//			connectedWire = nullptr;
			//		}
			//	}
			//
			//}
			//else
			//{
			//	if (connectedWire != nullptr)
			//	{
			//		connectedWire->Unplug(this);
			//		connectedWire->SetInteractingWith(player);
			//		player->m_HeldItem = connectedWire;
			//		connectedWire = nullptr;
			//	}
			//}
		} break;
		default:
		{
			GameObject::SetInteractingWith(gameObject);
		} break;
		}
	}

	Terminal::Terminal() :
		Terminal(g_SceneManager->CurrentScene()->GetUniqueObjectName("Terminal_", 2), InvalidGameObjectID)
	{
	}

	Terminal::Terminal(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("terminal"), gameObjectID),
		m_KeyEventCallback(this, &Terminal::OnKeyEvent),
		cursor(0, 0)
	{
		m_bInteractable = true;
		m_bSerializeMesh = false;
		m_bSerializeMaterial = false;

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

		m_Transform.UpdateParentTransform();

		m_VM = new VM::VirtualMachine();
	}

	void Terminal::Initialize()
	{
		g_InputManager->BindKeyEventCallback(&m_KeyEventCallback, 20);
		ParseCode();

		GetSystem<TerminalManager>(SystemType::TERMINAL_MANAGER)->RegisterTerminal(this);

		GameObject::Initialize();
	}

	void Terminal::PostInitialize()
	{
		//std::vector<GameObject*> gameObjects = g_SceneManager->CurrentScene()->GetAllObjects();
		//for (GameObject* gameObject : gameObjects)
		//{
		//	if (gameObject->GetType() == SID("point light"))
		//	{
		//		PointLight* pointLight = static_cast<PointLight*>(obj);
		//		GetSystem<PluggablesSystem>(SystemType::PLUGGABLES_SYSTEM)->AddWire(this, obj);
		//	}
		//}
	}

	void Terminal::Destroy(bool bDetachFromParent /* = true */)
	{
		g_InputManager->UnbindKeyEventCallback(&m_KeyEventCallback);

		if (m_VM != nullptr)
		{
			delete m_VM;
		}

		GetSystem<TerminalManager>(SystemType::TERMINAL_MANAGER)->DeregisterTerminal(this);

		GameObject::Destroy(bDetachFromParent);
	}

	void Terminal::Update()
	{
		if (m_DisplayReloadTimeRemaining != -1.0f)
		{
			m_DisplayReloadTimeRemaining -= g_DeltaTime;
			if (m_DisplayReloadTimeRemaining <= 0.0f)
			{
				m_DisplayReloadTimeRemaining = -1.0f;
			}
		}

		m_CursorBlinkTimer += g_DeltaTime;
		bool bRenderCursor = (m_ObjectInteractingWith != nullptr);
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

		if (m_NearbyInteractable != nullptr)
		{
			const real scale = 2.0f;
			const glm::vec3 right = m_Transform.GetRight();
			const glm::vec3 forward = m_Transform.GetForward();
			glm::vec3 c = m_Transform.GetWorldPosition();
			glm::vec3 v1 = c + (right + forward) * scale;
			glm::vec3 v2 = c + (-right + forward) * scale;
			glm::vec3 v3 = c - (forward * scale);
			btIDebugDraw* debugDrawer = g_Renderer->GetDebugDrawer();
			debugDrawer->drawTriangle(ToBtVec3(v1), ToBtVec3(v2), ToBtVec3(v3), btVector3(0.9f, 0.3f, 0.2f), 1.0f);
			glm::vec3 o(0.0f, sin(g_SecElapsedSinceProgramStart * 2.0f) + 1.0f, 0.0f);
			debugDrawer->drawTriangle(ToBtVec3(v1 + o), ToBtVec3(v2 + o), ToBtVec3(v3 + o), btVector3(0.9f, 0.3f, 0.2f), 1.0f);
			o = glm::vec3(0.0f, 2.0f, 0.0f);
			debugDrawer->drawTriangle(ToBtVec3(v1 + o), ToBtVec3(v2 + o), ToBtVec3(v3 + o), btVector3(0.9f, 0.3f, 0.2f), 1.0f);
		}

		if (m_VM != nullptr && m_VM->IsExecuting())
		{
			u32 outputCount = glm::min((u32)m_VM->terminalOutputs.size(), (u32)outputSignals.size());
			for (u32 i = 0; i < outputCount; ++i)
			{
				SetOutputSignal(i, m_VM->terminalOutputs[i].valInt);
			}
			for (u32 i = outputCount; i < (u32)outputSignals.size(); ++i)
			{
				SetOutputSignal(i, -1);
			}
		}
		else
		{
			for (u32 i = 0; i < (u32)outputSignals.size(); ++i)
			{
				SetOutputSignal(i, -1);
			}
		}

		GameObject::Update();
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
							const VM::Value& registerValue = m_VM->registers[i];
							if (registerValue.type != VM::Value::Type::_NONE)
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
						std::stack<VM::Value> stackCopy = m_VM->stack;
						while (!stackCopy.empty())
						{
							const VM::Value& value = stackCopy.top();
							std::string valStr = value.ToString();
							ImGui::Text("%s", valStr.c_str());
							stackCopy.pop();
						}
					}
					ImGui::EndChildFrame(); // stack
				}

				for (u32 i = 0; i < Terminal::MAX_OUTPUT_COUNT; ++i)
				{
					const VM::Value& terminalVal = m_VM->terminalOutputs[i];
					if (terminalVal.type != VM::Value::Type::_NONE)
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

	bool Terminal::AllowInteractionWith(GameObject* gameObject)
	{
		if (gameObject == nullptr)
		{
			return true;
		}

		if (gameObject->GetTypeID() == SID("player"))
		{
			Player* player = static_cast<Player*>(gameObject);
			//if (player->m_HeldItem == nullptr)
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
			}
		}

		return false;
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

	void Terminal::ParseTypeUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(scene);
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

	void Terminal::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		JSONObject terminalObj = {};

		SaveScript();

		terminalObj.fields.emplace_back("script file path", JSONValue(m_ScriptFileName));

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
		assert(m_VM != nullptr);

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
		}

		PrintError("Failed to save terminal script to %s\n", m_ScriptFileName.c_str());
		return false;
	}

	EventReply Terminal::OnKeyEvent(KeyCode keyCode, KeyAction action, i32 modifiers)
	{
		if (m_Camera != nullptr)
		{
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

	ParticleSystem::ParticleSystem(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("particle system"), gameObjectID)
	{
		m_Transform.updateParentOnStateChange = true;
	}

	void ParticleSystem::Destroy(bool bDetachFromParent /* = true */)
	{
		g_Renderer->RemoveParticleSystem(particleSystemID);
		GameObject::Destroy(bDetachFromParent);
	}

	GameObject* ParticleSystem::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, newObjectName, newGameObjectID);
		ParticleSystem* newParticleSystem = new ParticleSystem(newObjectName, newGameObjectID);

		CopyGenericFields(newParticleSystem, parent, copyFlags);

		newParticleSystem->data.colour0 = data.colour0;
		newParticleSystem->data.colour1 = data.colour1;
		newParticleSystem->data.speed = data.speed;
		newParticleSystem->data.particleCount = data.particleCount;
		newParticleSystem->bEnabled = true;
		newParticleSystem->scale = scale;
		newParticleSystem->model = model;
		g_Renderer->AddParticleSystem(m_Name, newParticleSystem, data.particleCount);

		return newParticleSystem;
	}

	void ParticleSystem::ParseTypeUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);
		FLEX_UNUSED(scene);

		JSONObject particleSystemObj = parentObject.GetObject("particle system info");

		Transform::ParseJSON(particleSystemObj, model);
		m_Transform.SetWorldFromMatrix(model);
		particleSystemObj.SetFloatChecked("scale", scale);
		particleSystemObj.SetBoolChecked("enabled", bEnabled);

		JSONObject systemDataObj = particleSystemObj.GetObject("data");
		data = {};
		systemDataObj.SetVec4Checked("colour0", data.colour0);
		systemDataObj.SetVec4Checked("colour1", data.colour1);
		systemDataObj.SetFloatChecked("speed", data.speed);
		i32 particleCount;
		if (systemDataObj.SetIntChecked("particle count", particleCount))
		{
			data.particleCount = particleCount;
		}

		particleSystemID = g_Renderer->AddParticleSystem(m_Name, this, particleCount);
	}

	void ParticleSystem::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		JSONObject particleSystemObj = {};

		particleSystemObj.fields.emplace_back(Transform::Serialize(model, m_Name.c_str()));
		particleSystemObj.fields.emplace_back("scale", JSONValue(scale));
		particleSystemObj.fields.emplace_back("enabled", JSONValue(bEnabled));


		JSONObject systemDataObj = {};
		systemDataObj.fields.emplace_back("colour0", JSONValue(VecToString(data.colour0, 2)));
		systemDataObj.fields.emplace_back("colour1", JSONValue(VecToString(data.colour1, 2)));
		systemDataObj.fields.emplace_back("speed", JSONValue(data.speed));
		systemDataObj.fields.emplace_back("particle count", JSONValue(data.particleCount));
		particleSystemObj.fields.emplace_back("data", JSONValue(systemDataObj));

		parentObject.fields.emplace_back("particle system info", JSONValue(particleSystemObj));
	}

	void ParticleSystem::DrawImGuiObjects()
	{
		GameObject::DrawImGuiObjects();

		static const ImGuiColorEditFlags colorEditFlags =
			ImGuiColorEditFlags_NoInputs |
			ImGuiColorEditFlags_Float |
			ImGuiColorEditFlags_RGB |
			ImGuiColorEditFlags_PickerHueWheel |
			ImGuiColorEditFlags_HDR;

		ImGui::Text("Particle System");

		ImGui::Checkbox("Enabled", &bEnabled);

		ImGui::ColorEdit4("Colour 0", &data.colour0.r, colorEditFlags);
		ImGui::SameLine();
		ImGui::ColorEdit4("Colour 1", &data.colour1.r, colorEditFlags);
		ImGui::SliderFloat("Speed", &data.speed, -10.0f, 10.0f);
		i32 particleCount = (i32)data.particleCount;
		if (ImGui::SliderInt("Particle count", &particleCount, 0, Renderer::MAX_PARTICLE_COUNT))
		{
			data.particleCount = particleCount;
		}
	}

	void ParticleSystem::OnTransformChanged()
	{
		scale = m_Transform.GetLocalScale().x;
		UpdateModelMatrix();
	}

	void ParticleSystem::UpdateModelMatrix()
	{
		model = glm::scale(m_Transform.GetWorldTransform(), glm::vec3(scale));

		GameObject::Update();
	}

	TerrainGenerator::TerrainGenerator(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("terrain generator"), gameObjectID),
		m_LowCol(0.2f, 0.3f, 0.45f),
		m_MidCol(0.45f, 0.55f, 0.25f),
		m_HighCol(0.65f, 0.67f, 0.69f)
	{
		AddTag("terrain");
		m_bSerializeMesh = false;
		m_bSerializeMaterial = false;
		terrain_workQueue = new ThreadSafeArray<TerrainChunkData>(256);
	}

	void TerrainGenerator::Initialize()
	{
		MaterialCreateInfo matCreateInfo = {};
		matCreateInfo.name = "Terrain";
		matCreateInfo.shaderName = "terrain";
		matCreateInfo.constAlbedo = glm::vec3(1.0f, 0.0f, 0.0f);
		matCreateInfo.constRoughness = 1.0f;
		matCreateInfo.constMetallic = 0.0f;
		matCreateInfo.bSerializable = false;
		m_TerrainMatID = g_Renderer->InitializeMaterial(&matCreateInfo);

		m_Mesh = new Mesh(this);
		m_Mesh->SetTypeToMemory();

		GenerateGradients();

		criticalSection = Platform::InitCriticalSection();

		for (u32 i = 0; i < terrain_workQueue->Size(); ++i)
		{
			AllocWorkQueueEntry(i);
		}

		terrain_workQueueEntriesCompleted = 0;
		terrain_workQueueEntriesCreated = 0;
		WRITE_BARRIER;
		terrain_workQueueEntriesClaimed = 0;
		WRITE_BARRIER;

		// Kick off threads to wait for work
		{
			u32 threadCount = (u32)glm::clamp(((i32)Platform::GetLogicalProcessorCount()) - 1, 1, 16);

			threadUserData = {};
			threadUserData.running = true;
			threadUserData.criticalSection = criticalSection;
			Platform::SpawnThreads(threadCount, &TerrainThreadUpdate, &threadUserData);
		}

		WRITE_BARRIER;

		DiscoverChunks();
		// Wait to generate chunks until road has been generated

		GameObject::Initialize();
	}

	void TerrainGenerator::PostInitialize()
	{
		GameObject::PostInitialize();
	}

	void TerrainGenerator::Update()
	{
		//if (waveChunks.size() > terrain_workQueue->Size())
		//{
		//	for (u32 i = 0; i < terrain_workQueue->Size(); ++i)
		//	{
		//		// TODO: Reuse memory
		//		FreeWorkQueueEntry(i);
		//	}
		//	delete terrain_workQueue;
		//
		//	u32 newSize = (u32)(waveChunks.size() * 1.2f);
		//	terrain_workQueue = new ThreadSafeArray<TerrainChunkData>(newSize);
		//	for (u32 i = 0; i < terrain_workQueue->Size(); ++i)
		//	{
		//		AllocWorkQueueEntry(i);
		//	}
		//}

		terrain_workQueueEntriesCompleted = 0;
		terrain_workQueueEntriesCreated = 0;
		WRITE_BARRIER;
		terrain_workQueueEntriesClaimed = 0;
		WRITE_BARRIER;

		DiscoverChunks();
		// Wait for road gen to call us back to fill out our road segments
		if (!m_RoadGameObjectID.IsValid() || !m_RoadSegments.empty())
		{
			GenerateChunks();
		}

		if (m_bDisplayTables)
		{
			real textureScale = 0.3f;
			real textureY = -0.05f;
			for (u32 i = 0; i < m_TableTextureIDs.size(); ++i)
			{
				SpriteQuadDrawInfo drawInfo = {};
				drawInfo.anchor = AnchorPoint::TOP_RIGHT;
				drawInfo.bScreenSpace = true;
				drawInfo.bReadDepth = false;
				drawInfo.scale = glm::vec3(textureScale);
				drawInfo.pos = glm::vec3(0.0f, textureY, 0.0f);
				drawInfo.textureID = m_TableTextureIDs[i];
				g_Renderer->EnqueueSprite(drawInfo);

				textureY -= (textureScale * 2.0f + 0.01f);
				textureScale /= 2.0f;
			}
		}

		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		if (player != nullptr)
		{
			glm::vec3 playerPos = player->GetTransform()->GetWorldPosition();
			glm::vec2 playerPos2D(playerPos.x, playerPos.z);
			for (auto iter = m_Meshes.begin(); iter != m_Meshes.end(); ++iter)
			{
				glm::vec2i chunkIndex = iter->first;
				glm::vec2 chunkPos = glm::vec2(chunkIndex.x * ChunkSize, chunkIndex.y * ChunkSize);
				real distToPlayer2 = glm::distance2(chunkPos, playerPos2D);

				bool bShouldBeLoaded = distToPlayer2 < m_LoadedChunkRigidBodyRadius2;
				if (bShouldBeLoaded)
				{
					if (iter->second->rigidBody == nullptr)
					{
						CreateChunkRigidBody(chunkIndex);
					}
				}
				else
				{
					if (iter->second->rigidBody != nullptr)
					{
						DestroyChunkRigidBody(chunkIndex);
					}
				}
			}
		}

		GameObject::Update();
	}

	void TerrainGenerator::Destroy(bool bDetachFromParent /* = true */)
	{
		threadUserData.running = false;

		// TODO: Only join our threads!
		Platform::JoinThreads();

		for (auto iter = m_Meshes.begin(); iter != m_Meshes.end(); ++iter)
		{
			// Mesh components will be destroyed by Mesh::Destroy
			if (iter->second->rigidBody != nullptr)
			{
				iter->second->rigidBody->Destroy();
				delete iter->second->rigidBody;
			}
			delete iter->second->triangleIndexVertexArray;

			delete iter->second;
		}
		m_Meshes.clear();

		m_ChunksToDestroy.clear();
		m_ChunksToLoad.clear();

		for (u32 i = 0; i < terrain_workQueue->Size(); ++i)
		{
			FreeWorkQueueEntry(i);
		}
		delete terrain_workQueue;
		terrain_workQueue = nullptr;

		Platform::FreeCriticalSection(criticalSection);
		criticalSection = nullptr;

		m_RoadSegments.clear();

		GameObject::Destroy(bDetachFromParent);
	}

	void TerrainGenerator::DrawImGuiObjects()
	{
		GameObject::DrawImGuiObjects();

		ImGui::Text("Loaded chunks: %u (loading: %u)", (u32)m_Meshes.size(), (u32)m_ChunksToLoad.size());

		bool bRegen = false;

		bRegen = ImGui::Checkbox("Highlight grid", &m_bHighlightGrid) || bRegen;

		ImGui::SameLine();

		ImGui::Checkbox("Display tables", &m_bDisplayTables);

		if (ImGui::Checkbox("Pin center", &m_bPinCenter))
		{
			if (m_bPinCenter)
			{
				m_PinnedPos = g_CameraManager->CurrentCamera()->position;
			}
		}

		bRegen = ImGui::SliderFloat("Base octave scale", &m_BaseOctave, 1.0f, 400.0f) || bRegen;

		bRegen = ImGui::SliderFloat("Octave scale", &m_OctaveScale, 1.0f, 250.0f) || bRegen;
		const u32 maxOctaveCount = (u32)glm::ceil(glm::log(m_BasePerlinTableWidth)) + 1;
		bRegen = ImGuiExt::SliderUInt("Octave count", &m_NumOctaves, 1, maxOctaveCount) || bRegen;

		bRegen = ImGui::SliderInt("Isolate octave", &m_IsolateOctave, -1, m_NumOctaves - 1) || bRegen;

		bRegen = ImGui::SliderFloat("Road blend", &m_RoadBlendDist, 0.0f, 100.0f) || bRegen;
		bRegen = ImGui::SliderFloat("Road blend threshold", &m_RoadBlendThreshold, 0.0f, 50.0f) || bRegen;

		u32 oldtableWidth = m_BasePerlinTableWidth;
		if (ImGuiExt::SliderUInt("Base table width", &m_BasePerlinTableWidth, 1, 512))
		{
			m_BasePerlinTableWidth = NextPowerOfTwo(m_BasePerlinTableWidth);
			if (m_BasePerlinTableWidth != oldtableWidth)
			{
				GenerateGradients();
				bRegen = true;
			}
		}

		bRegen = ImGui::Button("Regen") || bRegen;

		ImGui::SameLine();

		bRegen = ImGui::Checkbox("Use manual seed", &m_UseManualSeed) || bRegen;

		if (m_UseManualSeed)
		{
			bRegen = ImGui::InputInt("Manual seed", &m_ManualSeed) || bRegen;
		}

		const u32 previousVertCountPerChunkAxis = VertCountPerChunkAxis;
		bRegen = ImGuiExt::InputUInt("Verts per chunk", &VertCountPerChunkAxis) || bRegen;

		if (VertCountPerChunkAxis > 2)
		{
			if (ImGui::Button("/2"))
			{
				bRegen = true;
				VertCountPerChunkAxis /= 2;
			}

			ImGui::SameLine();
		}

		if (ImGui::Button("x2"))
		{
			bRegen = true;
			VertCountPerChunkAxis *= 2;
		}

		bRegen = ImGui::SliderFloat("Chunk size", &ChunkSize, 1.0f, 128.0f) || bRegen;

		bRegen = ImGui::SliderFloat("Max height", &MaxHeight, 0.1f, 500.0f) || bRegen;

		bRegen = ImGui::ColorEdit3("low", &m_LowCol.x) || bRegen;
		bRegen = ImGui::ColorEdit3("mid", &m_MidCol.x) || bRegen;
		bRegen = ImGui::ColorEdit3("high", &m_HighCol.x) || bRegen;

		if (bRegen)
		{
			GenerateGradients();
			DestroyAllChunks();

			for (u32 i = 0; i < terrain_workQueue->Size(); ++i)
			{
				(*terrain_workQueue)[i].baseOctave = m_BaseOctave;
				(*terrain_workQueue)[i].chunkSize = ChunkSize;
				(*terrain_workQueue)[i].maxHeight = MaxHeight;
				(*terrain_workQueue)[i].octaveScale = m_OctaveScale;
				(*terrain_workQueue)[i].numOctaves = m_NumOctaves;
				(*terrain_workQueue)[i].vertCountPerChunkAxis = VertCountPerChunkAxis;
				(*terrain_workQueue)[i].isolateOctave = m_IsolateOctave;
			}

			if (VertCountPerChunkAxis != previousVertCountPerChunkAxis)
			{
				for (u32 i = 0; i < terrain_workQueue->Size(); ++i)
				{
					FreeWorkQueueEntry(i);
					AllocWorkQueueEntry(i);
				}
			}
		}

		ImGui::SliderFloat("View radius", &m_LoadedChunkRadius, 10.0f, 3000.0f);
		ImGui::SliderFloat("Rigid body load radius sqr", &m_LoadedChunkRigidBodyRadius2, 10.0f, 9000.0f);

		if (ImGui::TreeNode("Road segments"))
		{
			static glm::vec2i selectedChunkIndex;
			static i32 selectedRoadSegmentIndex = -1;
			static AABB* selectedChunkAABB = nullptr;
			for (auto& iter : m_RoadSegments)
			{
				glm::vec2i chunkIndex = iter.first;

				bool bSelectedSegmentInChunk = chunkIndex == selectedChunkIndex;

				ImGui::PushID((const void*)((u64)((u64)chunkIndex.x + 9999) * 99999 + ((u64)chunkIndex.y + 9999)));

				const std::vector<RoadSegment*>& roadSegments = iter.second;

				if (ImGui::TreeNode("chunk road segments", "%i, %i", chunkIndex.x, chunkIndex.y))
				{
					for (i32 i = 0; i < (i32)roadSegments.size(); ++i)
					{
						RoadSegment* roadSegment = roadSegments[i];
						ImGui::PushID((const void*)(roadSegment));

						bool bSelected = (bSelectedSegmentInChunk && i == selectedRoadSegmentIndex);
						std::string label = "segment " + std::to_string(i);
						if (ImGui::Selectable(label.c_str(), &bSelected))
						{
							selectedChunkIndex = chunkIndex;
							selectedRoadSegmentIndex = i;
							selectedChunkAABB = &roadSegment->aabb;
						}

						ImGui::PopID();
					}

					ImGui::TreePop();
				}

				ImGui::PopID();
			}

			ImGui::TreePop();

			if (selectedChunkAABB != nullptr)
			{
				selectedChunkAABB->DrawDebug(btVector3(0.1f, 0.1f, 0.9f));
			}
		}
	}

	void TerrainGenerator::ParseTypeUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);
		FLEX_UNUSED(scene);

		if (parentObject.HasField("chunk generator info"))
		{
			JSONObject chunkGenInfo = parentObject.GetObject("chunk generator info");
			VertCountPerChunkAxis = (u32)chunkGenInfo.GetInt("vert count per chunk axis");
			ChunkSize = chunkGenInfo.GetFloat("chunk size");
			MaxHeight = chunkGenInfo.GetFloat("max height");
			m_UseManualSeed = chunkGenInfo.GetBool("use manual seed");
			m_ManualSeed = (u32)chunkGenInfo.GetInt("manual seed");

			chunkGenInfo.SetFloatChecked("loaded chunk radius", m_LoadedChunkRadius);
			chunkGenInfo.SetFloatChecked("loaded chunk rigid body square radius", m_LoadedChunkRigidBodyRadius2);
			chunkGenInfo.SetUIntChecked("base table width", m_BasePerlinTableWidth);

			chunkGenInfo.SetVec3Checked("low colour", m_LowCol);
			chunkGenInfo.SetVec3Checked("mid colour", m_MidCol);
			chunkGenInfo.SetVec3Checked("high colour", m_HighCol);

			chunkGenInfo.SetFloatChecked("base octave", m_BaseOctave);
			chunkGenInfo.SetFloatChecked("octave scale", m_OctaveScale);
			chunkGenInfo.SetUIntChecked("num octaves", m_NumOctaves);

			chunkGenInfo.SetBoolChecked("pin center", m_bPinCenter);
			chunkGenInfo.SetVec3Checked("pinned center", m_PinnedPos);
		}
	}

	void TerrainGenerator::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		JSONObject chunkGenInfo = {};

		chunkGenInfo.fields.emplace_back("vert count per chunk axis", JSONValue(VertCountPerChunkAxis));
		chunkGenInfo.fields.emplace_back("chunk size", JSONValue(ChunkSize));
		chunkGenInfo.fields.emplace_back("max height", JSONValue(MaxHeight));
		chunkGenInfo.fields.emplace_back("use manual seed", JSONValue(m_UseManualSeed));
		chunkGenInfo.fields.emplace_back("manual seed", JSONValue(m_ManualSeed));

		chunkGenInfo.fields.emplace_back("loaded chunk radius", JSONValue(m_LoadedChunkRadius));
		chunkGenInfo.fields.emplace_back("loaded chunk rigid body square radius", JSONValue(m_LoadedChunkRigidBodyRadius2));

		chunkGenInfo.fields.emplace_back("base table width", JSONValue(m_BasePerlinTableWidth));

		chunkGenInfo.fields.emplace_back("low colour", JSONValue(VecToString(m_LowCol)));
		chunkGenInfo.fields.emplace_back("mid colour", JSONValue(VecToString(m_MidCol)));
		chunkGenInfo.fields.emplace_back("high colour", JSONValue(VecToString(m_HighCol)));

		chunkGenInfo.fields.emplace_back("base octave", JSONValue(m_BaseOctave));
		chunkGenInfo.fields.emplace_back("octave scale", JSONValue(m_OctaveScale));
		chunkGenInfo.fields.emplace_back("num octaves", JSONValue(m_NumOctaves));

		chunkGenInfo.fields.emplace_back("pin center", JSONValue(m_bPinCenter));
		chunkGenInfo.fields.emplace_back("pinned center", JSONValue(VecToString(m_PinnedPos)));

		parentObject.fields.emplace_back("chunk generator info", JSONValue(chunkGenInfo));
	}

	real TerrainGenerator::Sample(const glm::vec2& pos)
	{
		TerrainGenerator::TerrainChunkData chunkData = {};
		chunkData.baseOctave = m_BaseOctave;
		chunkData.numOctaves = m_NumOctaves;
		chunkData.isolateOctave = m_IsolateOctave;
		chunkData.octaveScale = m_OctaveScale;
		chunkData.randomTables = &m_RandomTables;
		real sample = SampleTerrain(&chunkData, pos);
		return (sample - 0.5f) * MaxHeight + m_Transform.GetWorldPosition().y;
	}

	real TerrainGenerator::SmoothBlend(real t)
	{
		return 6.0f * glm::pow(t, 5.0f) - 15.0f * glm::pow(t, 4.0f) + 10.0f * glm::pow(t, 3.0f);
	}

	void TerrainGenerator::GenerateGradients()
	{
		PROFILE_AUTO("Generate terrain chunk gradient tables");

		m_RandomTables = std::vector<std::vector<glm::vec2>>(m_NumOctaves);

		std::mt19937 m_RandGenerator;
		std::uniform_real_distribution<real> m_RandDistribution;
		m_RandGenerator.seed(m_UseManualSeed ? m_ManualSeed : (u32)Time::CurrentMilliseconds());

		auto dice = std::bind(m_RandDistribution, m_RandGenerator);

		{
			u32 tableWidth = m_BasePerlinTableWidth;
			for (u32 octave = 0; octave < m_NumOctaves; ++octave)
			{
				m_RandomTables[octave] = std::vector<glm::vec2>(tableWidth * tableWidth);

				for (u32 i = 0; i < m_RandomTables[octave].size(); ++i)
				{
					m_RandomTables[octave][i] = glm::normalize(glm::vec2(dice() * 2.0 - 1.0f, dice() * 2.0 - 1.0f));
				}

				tableWidth /= 2;
			}
		}

		for (u32 i = 0; i < m_TableTextureIDs.size(); ++i)
		{
			g_ResourceManager->RemoveLoadedTexture(m_TableTextureIDs[i], true);
		}
		m_TableTextureIDs.resize(m_RandomTables.size());

		for (u32 i = 0; i < m_TableTextureIDs.size(); ++i)
		{
			const u32 tableWidth = (u32)glm::sqrt(m_RandomTables[i].size());
			if (tableWidth < 1)
			{
				break;
			}

			std::vector<glm::vec4> textureMem(m_RandomTables[i].size());
			for (u32 j = 0; j < m_RandomTables[i].size(); ++j)
			{
				textureMem[j] = glm::vec4(m_RandomTables[i][j].x * 0.5f + 0.5f, m_RandomTables[i][j].y * 0.5f + 0.5f, 0.0f, 1.0f);
			}
			m_TableTextureIDs[i] = g_Renderer->InitializeTextureFromMemory(&textureMem[0],
				(u32)(textureMem.size() * sizeof(u32) * 4),
				VK_FORMAT_R32G32B32A32_SFLOAT,
				"Perlin random table", tableWidth, tableWidth, 4, VK_FILTER_NEAREST);
		}
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
			// TODO:  + 0.5f?
			i32 minChunkIndexX = (i32)glm::floor((roadSegment.aabb.minX + pos.x - blendRadiusBoost) / ChunkSize);
			i32 maxChunkIndexX = (i32)glm::ceil((roadSegment.aabb.maxX + pos.x + blendRadiusBoost) / ChunkSize);
			i32 minChunkIndexZ = (i32)glm::floor((roadSegment.aabb.minZ + pos.z - blendRadiusBoost) / ChunkSize);
			i32 maxChunkIndexZ = (i32)glm::ceil((roadSegment.aabb.maxZ + pos.z + blendRadiusBoost) / ChunkSize);
			for (i32 x = minChunkIndexX; x < maxChunkIndexX; ++x)
			{
				for (i32 z = minChunkIndexZ; z < maxChunkIndexZ; ++z)
				{
					glm::vec2i chunkIndex = glm::vec2i(x, z);
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

	void TerrainGenerator::DiscoverChunks()
	{
		static std::set<glm::vec2i, Vec2iCompare> chunksInRadius;
		chunksInRadius.clear();

		glm::vec3 center = m_bPinCenter ? m_PinnedPos : g_CameraManager->CurrentCamera()->position;
		const glm::vec2 centerXZ(center.x, center.z);
		const glm::vec2i camChunkIdx = (glm::vec2i)(glm::vec2(center.x, center.z) / ChunkSize);
		const i32 maxChunkIdxDiff = (i32)glm::ceil(m_LoadedChunkRadius / (real)ChunkSize);
		const real radiusSqr = m_LoadedChunkRadius * m_LoadedChunkRadius;
		for (i32 x = camChunkIdx.x - maxChunkIdxDiff; x < camChunkIdx.x + maxChunkIdxDiff; ++x)
		{
			for (i32 z = camChunkIdx.y - maxChunkIdxDiff; z < camChunkIdx.y + maxChunkIdxDiff; ++z)
			{
				glm::vec2 chunkCenter((x + 0.5f) * ChunkSize, (z + 0.5f) * ChunkSize);
				if (glm::distance2(chunkCenter, centerXZ) < radiusSqr)
				{
					chunksInRadius.emplace(glm::vec2i(x, z));
				}
			}
		}

		// Discover newly unloaded chunks
		for (auto chunkIter = m_Meshes.begin(); chunkIter != m_Meshes.end(); ++chunkIter)
		{
			const glm::vec2i& chunkIdx = chunkIter->first;

			if (!Contains(chunksInRadius, chunkIdx) &&
				!Contains(m_ChunksToDestroy, chunkIdx))
			{
				m_ChunksToDestroy.emplace(chunkIdx);

				Erase(m_ChunksToLoad, chunkIdx);
			}
		}

		//if (m_Meshes.size() < 512)
		{
			// Discover newly loaded chunks
			u32 newChunksToLoadCount = 0;
			for (const glm::vec2i& chunkIdx : chunksInRadius)
			{
				// TODO: Tell renderer to resize terrain dynamic UBO to accommodate all chunks to prevent many resizes

				if (!Contains(m_Meshes, chunkIdx) &&
					!Contains(m_ChunksToLoad, chunkIdx))
				{
					++newChunksToLoadCount;
					m_ChunksToLoad.emplace(chunkIdx);

					Erase(m_ChunksToDestroy, chunkIdx);

					// Never queue more than this many in one frame
					if (newChunksToLoadCount >= 256)
					{
						break;
					}
				}
			}
		}
	}

	void TerrainGenerator::GenerateChunks()
	{
		// Resize work queue if not large enough
		if ((m_Meshes.size() + m_ChunksToLoad.size()) > terrain_workQueue->Size())
		{
			for (u32 i = 0; i < terrain_workQueue->Size(); ++i)
			{
				// TODO: Reuse memory
				FreeWorkQueueEntry(i);
			}
			delete terrain_workQueue;

			u32 newSize = (u32)((m_Meshes.size() + m_ChunksToLoad.size()) * 1.2f);
			terrain_workQueue = new ThreadSafeArray<TerrainChunkData>(newSize);
			for (u32 i = 0; i < terrain_workQueue->Size(); ++i)
			{
				AllocWorkQueueEntry(i);
			}

			WRITE_BARRIER;
		}

		// Destroy far away chunks on the main thread
		{
			PROFILE_AUTO("Destroy terrain chunks");
			ns start = Time::CurrentNanoseconds();
			i32 iterationCount = 0;
			while (m_ChunksToDestroy.size() > 0)
			{
				glm::vec2i chunkIdx = *m_ChunksToDestroy.begin();
				m_ChunksToDestroy.erase(m_ChunksToDestroy.begin());

				auto iter = m_Meshes.find(chunkIdx);
				assert(iter != m_Meshes.end());
				MeshComponent* mesh = iter->second->meshComponent;
				u32 submeshIndex = iter->second->linearIndex;
				m_Mesh->RemoveSubmesh(submeshIndex);
				m_Meshes.erase(iter);
				// TODO:
				/*
					if (iter->second->rigidBody != nullptr)
					{
						iter->second->rigidBody->Destroy();
						delete iter->second->rigidBody;
					}
					delete iter->second->triangleIndexVertexArray;

					delete iter->second;
				*/
				mesh->Destroy();
				delete mesh;

				++iterationCount;

				ns now = Time::CurrentNanoseconds();
				if ((now - start) > m_DeletionBudgetPerFrame)
				{
					break;
				}
			}
			if (iterationCount != 0)
			{
				Print("Destroyed %d chunks (total: %d)\n", iterationCount, (i32)m_Meshes.size());
			}
		}

		PROFILE_BEGIN("generate terrain");

		//glm::vec3* positions = m_VertexBufferCreateInfo.positions_3D.data();
		//glm::vec2* texCoords = m_VertexBufferCreateInfo.texCoords_UV.data();
		//glm::vec4* colours = m_VertexBufferCreateInfo.colours_R32G32B32A32.data();

		//{
		//	auto existingChunkIter = m_Meshes.find(chunkIndex);
		//	if (existingChunkIter != m_Meshes.end())
		//	{
		//		if (existingChunkIter->second->meshComponent != nullptr)
		//		{
		//			existingChunkIter->second->meshComponent->Destroy();
		//			delete existingChunkIter->second;
		//		}
		//		m_Meshes.erase(existingChunkIter);
		//	}
		//}

		// Fill out chunk data for threads to discover
		for (auto chunkToLoadIter = m_ChunksToLoad.begin(); chunkToLoadIter != m_ChunksToLoad.end(); ++chunkToLoadIter)
		{
			glm::vec2i chunkIndex = *chunkToLoadIter;

			assert(m_Meshes.find((glm::vec2i)chunkIndex) == m_Meshes.end());

			Chunk* chunk = new Chunk();
			chunk->meshComponent = nullptr;
			chunk->linearIndex = u32_max; // Temporarily clear linear index, it will be set after chunk creation completes
			// TODO: Reuse slots
			m_Meshes.emplace(chunkIndex, chunk);

			volatile TerrainChunkData* terrainChunkData = &(*terrain_workQueue)[terrain_workQueueEntriesCreated];
			terrainChunkData->chunkIndex.x = chunkIndex.x;
			terrainChunkData->chunkIndex.y = chunkIndex.y;

			WRITE_BARRIER;

			Platform::AtomicIncrement(&terrain_workQueueEntriesCreated);
		}

		Material* terrainMat = g_Renderer->GetMaterial(m_TerrainMatID);
		Shader* terrainShader = g_Renderer->GetShader(terrainMat->shaderID);

		if ((i32)m_Meshes.size() >= terrainShader->maxObjectCount - 1)
		{
			terrainShader->maxObjectCount = (u32)(m_Meshes.size() + 1);
			g_Renderer->SetStaticGeometryBufferDirty(terrainShader->staticVertexBufferIndex);
		}

		// Wait for all threads to complete
		// TODO: Call later in frame
		while (terrain_workQueueEntriesCompleted != terrain_workQueueEntriesCreated)
		{
			// TODO: Handle completed entries as they come in rather than waiting for all
			Platform::YieldProcessor();
		}

		PROFILE_END("generate terrain");

		if (!m_ChunksToLoad.empty())
		{
			Profiler::PrintBlockDuration("generate terrain");

			const u32 vertexCount = VertCountPerChunkAxis * VertCountPerChunkAxis;
			const u32 triCount = ((VertCountPerChunkAxis - 1) * (VertCountPerChunkAxis - 1)) * 2;
			const u32 indexCount = triCount * 3;

			// TODO: Spread creation across multiple frames

			static VertexBufferDataCreateInfo vertexBufferCreateInfo = {};
			vertexBufferCreateInfo.positions_3D.clear();
			vertexBufferCreateInfo.texCoords_UV.clear();
			vertexBufferCreateInfo.colours_R32G32B32A32.clear();
			vertexBufferCreateInfo.normals.clear();

			vertexBufferCreateInfo.attributes = terrainShader->vertexAttributes;
			vertexBufferCreateInfo.positions_3D.resize(vertexCount);
			vertexBufferCreateInfo.texCoords_UV.resize(vertexCount);
			vertexBufferCreateInfo.colours_R32G32B32A32.resize(vertexCount);
			vertexBufferCreateInfo.normals.resize(vertexCount);

			static std::vector<u32> indices;
			indices.resize(indexCount);

			// TODO: Generate all new vertex buffers first, then do post pass to compute normals
			u32 vertexBufferStride = CalculateVertexStride(vertexBufferCreateInfo.attributes);
			i32 workQueueIndex = 0; // Count up to terrain_workQueueEntriesCreated
			for (auto chunkToLoadIter = m_ChunksToLoad.begin(); chunkToLoadIter != m_ChunksToLoad.end(); ++chunkToLoadIter)
			{
				glm::vec2i chunkIndex = *chunkToLoadIter;

				volatile TerrainChunkData* terrainChunkData = &(*terrain_workQueue)[workQueueIndex++];

				memcpy((void*)vertexBufferCreateInfo.positions_3D.data(), (void*)terrainChunkData->positions, sizeof(glm::vec3) * vertexCount);
				memcpy((void*)vertexBufferCreateInfo.texCoords_UV.data(), (void*)terrainChunkData->uvs, sizeof(glm::vec2) * vertexCount);
				memcpy((void*)vertexBufferCreateInfo.colours_R32G32B32A32.data(), (void*)terrainChunkData->colours, sizeof(glm::vec4) * vertexCount);
				memcpy((void*)indices.data(), (void*)terrainChunkData->indices, sizeof(u32) * indexCount);

				auto chunkLeftIter = m_Meshes.find(chunkIndex - glm::vec2i(1, 0));
				auto chunkRightIter = m_Meshes.find(chunkIndex + glm::vec2i(0, 1));
				auto chunkBackIter = m_Meshes.find(chunkIndex - glm::vec2i(0, 1));
				auto chunkForwardIter = m_Meshes.find(chunkIndex + glm::vec2i(1, 0));
				u8* chunkLeftVertBuf = nullptr;
				u8* chunkRightVertBuf = nullptr;
				u8* chunkBackVertBuf = nullptr;
				u8* chunkForwardVertBuf = nullptr;
				if (chunkLeftIter != m_Meshes.end())
				{
					MeshComponent* meshComponent = chunkLeftIter->second->meshComponent;
					if (meshComponent != nullptr)
					{
						chunkLeftVertBuf = (u8*)meshComponent->GetVertexBufferData()->vertexData;
					}
				}
				if (chunkRightIter != m_Meshes.end())
				{
					MeshComponent* meshComponent = chunkRightIter->second->meshComponent;
					if (meshComponent != nullptr)
					{
						chunkRightVertBuf = (u8*)meshComponent->GetVertexBufferData()->vertexData;
					}
				}
				if (chunkBackIter != m_Meshes.end())
				{
					MeshComponent* meshComponent = chunkBackIter->second->meshComponent;
					if (meshComponent != nullptr)
					{
						chunkBackVertBuf = (u8*)meshComponent->GetVertexBufferData()->vertexData;
					}
				}
				if (chunkForwardIter != m_Meshes.end())
				{
					MeshComponent* meshComponent = chunkForwardIter->second->meshComponent;
					if (meshComponent != nullptr)
					{
						chunkForwardVertBuf = (u8*)meshComponent->GetVertexBufferData()->vertexData;
					}
				}

				real quadSize = ChunkSize / VertCountPerChunkAxis;
				u32 vertIndex = 0;
				for (u32 z = 0; z < VertCountPerChunkAxis; ++z)
				{
					for (u32 x = 0; x < VertCountPerChunkAxis; ++x)
					{
						real left = 0.0f;
						if (x > 0)
						{
							left = vertexBufferCreateInfo.positions_3D[vertIndex - 1].y;
						}
						else if (chunkLeftVertBuf != nullptr)
						{
							glm::vec3* pos = (glm::vec3*)&chunkLeftVertBuf[(vertIndex + (VertCountPerChunkAxis - 1)) * vertexBufferStride];
							left = pos->y;
						}

						real right = 0.0f;
						if (x < VertCountPerChunkAxis - 1)
						{
							right = vertexBufferCreateInfo.positions_3D[vertIndex + 1].y;
						}
						else if (chunkRightVertBuf != nullptr)
						{
							glm::vec3* pos = (glm::vec3*)&chunkRightVertBuf[(vertIndex - (VertCountPerChunkAxis - 1)) * vertexBufferStride];
							right = pos->y;
						}

						real back = 0.0f;
						if (z > 0)
						{
							back = vertexBufferCreateInfo.positions_3D[vertIndex - VertCountPerChunkAxis].y;
						}
						else if (chunkBackVertBuf != nullptr)
						{
							glm::vec3* pos = (glm::vec3*)&chunkBackVertBuf[(x + (VertCountPerChunkAxis - 1) * VertCountPerChunkAxis) * vertexBufferStride];
							back = pos->y;
						}

						real forward = 0.0f;
						if (z < VertCountPerChunkAxis - 1)
						{
							forward = vertexBufferCreateInfo.positions_3D[vertIndex + VertCountPerChunkAxis].y;
						}
						else if (chunkForwardVertBuf != nullptr)
						{
							glm::vec3* pos = (glm::vec3*)&chunkForwardVertBuf[x * vertexBufferStride];
							forward = pos->y;
						}

						real dX = left - right;
						real dZ = back - forward;
						vertexBufferCreateInfo.normals[vertIndex] = glm::normalize(glm::vec3(dX, 2.0f * quadSize, dZ));
						//tangents[vertIdx] = glm::normalize(-glm::cross(normals[vertIdx], glm::vec3(0.0f, 0.0f, 1.0f)));

						++vertIndex;
					}
				}

				RenderObjectCreateInfo renderObjectCreateInfo = {};
				i32 submeshIndex;
				MeshComponent* meshComponent = MeshComponent::LoadFromMemory(m_Mesh, vertexBufferCreateInfo, indices, m_TerrainMatID, &renderObjectCreateInfo, true, &submeshIndex);
				if (meshComponent != nullptr)
				{
					m_Meshes[chunkIndex]->meshComponent = meshComponent;
					m_Meshes[chunkIndex]->linearIndex = submeshIndex;
				}
			}

			Print("Loaded %d chunks (total: %d)\n", (i32)m_ChunksToLoad.size(), (i32)m_Meshes.size());
			m_ChunksToLoad.clear();
		}
	}

	void TerrainGenerator::DestroyChunkRigidBody(const glm::vec2i& chunkIndex)
	{
		m_Meshes[chunkIndex]->rigidBody->Destroy();
		delete m_Meshes[chunkIndex]->rigidBody;
		m_Meshes[chunkIndex]->rigidBody = nullptr;
		delete m_Meshes[chunkIndex]->triangleIndexVertexArray;
		m_Meshes[chunkIndex]->triangleIndexVertexArray = nullptr;
	}

	void TerrainGenerator::CreateChunkRigidBody(const glm::vec2i& chunkIndex)
	{
		if (m_Meshes[chunkIndex]->rigidBody != nullptr)
		{
			// Already exists
			return;
		}

		MeshComponent* submesh = m_Meshes[chunkIndex]->meshComponent;

		btBvhTriangleMeshShape* shape;
		submesh->CreateCollisionMesh(&m_Meshes[chunkIndex]->triangleIndexVertexArray, &shape);

		// TODO: Don't even create rb?
		RigidBody* rigidBody = new RigidBody((i32)CollisionType::STATIC, (i32)CollisionType::DEFAULT & ~(i32)CollisionType::STATIC);
		rigidBody->SetStatic(true);
		rigidBody->Initialize(shape, &m_Transform);
		m_Meshes[chunkIndex]->rigidBody = rigidBody;
		m_Meshes[chunkIndex]->rigidBody->GetRigidBodyInternal()->setActivationState(WANTS_DEACTIVATION);
	}

	void TerrainGenerator::DestroyAllChunks()
	{
		for (auto iter = m_Meshes.begin(); iter != m_Meshes.end(); ++iter)
		{
			m_Mesh->RemoveSubmesh(iter->second->linearIndex);
			iter->second->meshComponent->Destroy();
			delete iter->second;
		}
		m_Meshes.clear();
	}

	void TerrainGenerator::AllocWorkQueueEntry(u32 workQueueIndex)
	{
		const u32 vertCountPerChunk = VertCountPerChunkAxis * VertCountPerChunkAxis;
		const u32 triCountPerChunk = ((VertCountPerChunkAxis - 1) * (VertCountPerChunkAxis - 1)) * 2;
		const u32 indexCountPerChunk = triCountPerChunk * 3;

		volatile TerrainChunkData& chunkData = (*terrain_workQueue)[workQueueIndex];

		chunkData.positions = (glm::vec3*)malloc(vertCountPerChunk * sizeof(glm::vec3));
		assert(chunkData.positions != nullptr);
		chunkData.colours = (glm::vec4*)malloc(vertCountPerChunk * sizeof(glm::vec4));
		assert(chunkData.colours != nullptr);
		chunkData.uvs = (glm::vec2*)malloc(vertCountPerChunk * sizeof(glm::vec2));
		assert(chunkData.uvs != nullptr);

		chunkData.indices = (u32*)malloc(indexCountPerChunk * sizeof(u32));
		assert(chunkData.indices != nullptr);

		chunkData.baseOctave = m_BaseOctave;
		chunkData.chunkSize = ChunkSize;
		chunkData.maxHeight = MaxHeight;
		chunkData.octaveScale = m_OctaveScale;
		chunkData.roadBlendDist = m_RoadBlendDist;
		chunkData.roadBlendThreshold = m_RoadBlendThreshold;
		chunkData.numOctaves = m_NumOctaves;
		chunkData.vertCountPerChunkAxis = VertCountPerChunkAxis;
		chunkData.isolateOctave = m_IsolateOctave;

		chunkData.randomTables = &m_RandomTables;
		chunkData.roadSegments = &m_RoadSegments;
	}

	void TerrainGenerator::FreeWorkQueueEntry(u32 workQueueIndex)
	{
		volatile TerrainChunkData& chunkData = (*terrain_workQueue)[workQueueIndex];

		assert(chunkData.positions != nullptr);
		free((void*)chunkData.positions);
		chunkData.positions = nullptr;

		assert(chunkData.colours != nullptr);
		free((void*)chunkData.colours);
		chunkData.colours = nullptr;

		assert(chunkData.uvs != nullptr);
		free((void*)chunkData.uvs);
		chunkData.uvs = nullptr;

		assert(chunkData.indices != nullptr);
		free((void*)chunkData.indices);
		chunkData.indices = nullptr;
	}


	void* TerrainThreadUpdate(void* inData)
	{
		TerrainThreadData* threadData = (TerrainThreadData*)inData;

		while (threadData->running)
		{
			volatile TerrainGenerator::TerrainChunkData* work = nullptr;

			if (terrain_workQueueEntriesClaimed < terrain_workQueueEntriesCreated)
			{
				Platform::EnterCriticalSection(threadData->criticalSection);
				{
					if (terrain_workQueueEntriesClaimed < terrain_workQueueEntriesCreated)
					{
						work = &(*terrain_workQueue)[terrain_workQueueEntriesClaimed];

						terrain_workQueueEntriesClaimed += 1;

						WRITE_BARRIER;

						assert(terrain_workQueueEntriesClaimed <= terrain_workQueueEntriesCreated);
						assert(terrain_workQueueEntriesClaimed <= terrain_workQueue->Size());
					}

				}
				Platform::LeaveCriticalSection(threadData->criticalSection);
			}

			if (work != nullptr)
			{
				// Inputs
				glm::vec2i chunkIndex = glm::vec2i(work->chunkIndex.x, work->chunkIndex.y);
				u32 vertCountPerChunkAxis = work->vertCountPerChunkAxis;
				real chunkSize = work->chunkSize;
				real maxHeight = work->maxHeight;
				real roadBlendDist = work->roadBlendDist;
				real roadBlendThreshold = work->roadBlendThreshold;
				std::map<glm::vec2i, std::vector<RoadSegment*>, Vec2iCompare>* roadSegments = work->roadSegments;

				std::vector<RoadSegment*>* overlappingRoadSegments = nullptr;
				auto roadSegmentIter = roadSegments->find(chunkIndex);
				if (roadSegmentIter != roadSegments->end())
				{
					overlappingRoadSegments = &roadSegmentIter->second;
				}

				// Outputs
				volatile glm::vec3* positions = work->positions;
				volatile glm::vec4* colours = work->colours;
				volatile glm::vec2* uvs = work->uvs;
				volatile u32* indices = work->indices;

				//const u32 vertexCount = vertCountPerChunkAxis * vertCountPerChunkAxis;
				//const u32 triCount = ((vertCountPerChunkAxis - 1) * (vertCountPerChunkAxis - 1)) * 2;
				//const u32 indexCount = triCount * 3;

				u32 posIndex = 0;
				for (u32 z = 0; z < vertCountPerChunkAxis; ++z)
				{
					for (u32 x = 0; x < vertCountPerChunkAxis; ++x)
					{
						glm::vec2 uv(x / (real)(vertCountPerChunkAxis - 1), z / (real)(vertCountPerChunkAxis - 1));

						glm::vec3 vertPosOS = glm::vec3(uv.x * chunkSize, 0.0f, uv.y * chunkSize);
						glm::vec3 vertPosWS = vertPosOS + glm::vec3(chunkIndex.x * chunkSize, 0.0f, chunkIndex.y * chunkSize);

						glm::vec2 sampleCenter(vertPosWS.x, vertPosWS.z);
						real height = SampleTerrain(work, sampleCenter);

						vertPosWS.y = (height - 0.5f) * maxHeight;

						glm::vec4 colour(height);

						//real roadWidth = 0.0f;
						real distToRoad = 99999.0f;
						glm::vec3 roadTangentAtClosestPoint;
						glm::vec3 roadCurvePosAtClosestPoint;
						glm::vec3 closestPointToRoad;
						if (overlappingRoadSegments != nullptr)
						{
							glm::vec3 outClosestPoint(VEC3_ZERO);
							for (RoadSegment* overlappingRoadSegment : *overlappingRoadSegments)
							{
								real d = overlappingRoadSegment->SignedDistanceTo(vertPosWS, outClosestPoint);
								if (d < distToRoad)
								{
									distToRoad = d;
									closestPointToRoad = outClosestPoint;

									real distAlongCurve = overlappingRoadSegment->curve.FindDistanceAlong(outClosestPoint);

									roadCurvePosAtClosestPoint = overlappingRoadSegment->curve.GetPointOnCurve(distAlongCurve);
									if (distToRoad - roadBlendThreshold < roadBlendDist)
									{
										glm::vec3 curveForward = glm::normalize(overlappingRoadSegment->curve.GetFirstDerivativeOnCurve(distAlongCurve));
										roadTangentAtClosestPoint = glm::cross(curveForward, VEC3_UP);
										if (glm::dot(roadTangentAtClosestPoint, closestPointToRoad - roadCurvePosAtClosestPoint) < 0.0f)
										{
											// Negate when point is on left side of road
											roadTangentAtClosestPoint = -roadTangentAtClosestPoint;
										}
										//roadWidth = overlappingRoadSegment->widthStart;
									}
								}
							}
						}

						real vergeLen = roadBlendDist * 0.35f;

						// Epsilon value accounts for verts that are _just_ outside of triangle edge
						// that for some reason aren't picked up by other triangle
						bool bInsideRoad = distToRoad <= 0.1f;
						// zero at edge, grows further out from road (zero inside road)
						//real distanceOutsideRoad = glm::max(distToRoad, 0.0f);
						// [0, 1] from inner verge to outer verge and beyond (zero inside road)
						real distanceAlongVerge = glm::clamp((distToRoad - roadBlendThreshold) / vergeLen, 0.0f, 1.0f);
						//real vergeBlendWeight = 1.0f - glm::abs(distanceAlongVerge * 2.0f - 1.0f);
						//real vergeBlendWeight = glm::pow(glm::sin(distanceAlongVerge * PI), 4.0f);

						// [0, 1] one at road edge, zero at blend dist (zero inside road)
						//real roadBlendWeight = 1.0f - glm::min(distanceOutsideRoad / roadBlendDist, 1.0f);
						real roadBlendWeight = 1.0f - glm::clamp((distToRoad - roadBlendThreshold) / roadBlendDist, 0.0f, 1.0f);

						colour = glm::vec4(height);

						// Distance calc must be slightly off, if this checks for 0.0
						// we get some verts in the road on the edges
						if (bInsideRoad)
						{
							// Clip point by setting to NaN
							vertPosWS.x = vertPosWS.y = vertPosWS.z = (real)nan("");
						}
						else
						{
							if (roadBlendWeight > 0.0f)
							{
								real lerpToRoadPosAlpha = roadBlendWeight;
								lerpToRoadPosAlpha *= lerpToRoadPosAlpha; // Square to get nicer falloff

								vertPosWS = vertPosWS + (closestPointToRoad - vertPosWS) * lerpToRoadPosAlpha;

								// Move points near road closer to "verge" position to give road shoulders
								// and a smoother falloff on hills
								glm::vec3 projectedPoint = closestPointToRoad + roadTangentAtClosestPoint * (distanceAlongVerge * vergeLen);
								vertPosWS = Lerp(vertPosWS, projectedPoint, glm::pow(1.0f - distanceAlongVerge, 0.5f));
							}
						}

						memcpy((void*)&positions[posIndex], (void*)&vertPosWS, sizeof(glm::vec3));
						memcpy((void*)&colours[posIndex], (void*)&colour, sizeof(glm::vec4));
						memcpy((void*)&uvs[posIndex], (void*)&uv, sizeof(glm::vec2));

						//vertexBufferCreateInfo.texCoords_UV.emplace_back(uv);
						//bool bShowEdge = (m_bHighlightGrid && (x == 0 || x == (vertCountPerChunkAxis - 1) || z == 0 || z == (vertCountPerChunkAxis - 1)));
						//glm::vec3 vertCol = (bShowEdge ? glm::vec3(0.75f) : (height <= 0.5f ? Lerp(m_LowCol, m_MidCol, glm::pow(height * 2.0f, 4.0f)) : Lerp(m_MidCol, m_HighCol, glm::pow((height - 0.5f) * 2.0f, 1.0f / 5.0f))));
						//vertexBufferCreateInfo.colours_R32G32B32A32.emplace_back(glm::vec4(vertCol.x, vertCol.y, vertCol.z, 1.0f));
						//vertexBufferCreateInfo.colors_R32G32B32A32.emplace_back(glm::vec4(height, height, height, 1.0f));

						++posIndex;
					}
				}

				for (u32 z = 0; z < vertCountPerChunkAxis - 1; ++z)
				{
					for (u32 x = 0; x < vertCountPerChunkAxis - 1; ++x)
					{
						u32 vertIdx = z * (vertCountPerChunkAxis - 1) + x;
						u32 i = z * vertCountPerChunkAxis + x;
						indices[vertIdx * 6 + 0] = i;
						indices[vertIdx * 6 + 1] = i + 1 + vertCountPerChunkAxis;
						indices[vertIdx * 6 + 2] = i + 1;

						indices[vertIdx * 6 + 3] = i;
						indices[vertIdx * 6 + 4] = i + vertCountPerChunkAxis;
						indices[vertIdx * 6 + 5] = i + 1 + vertCountPerChunkAxis;
					}
				}

				WRITE_BARRIER;

				Platform::AtomicIncrement(&terrain_workQueueEntriesCompleted);
			}
			else
			{
				Platform::Sleep(2);
			}
		}

		return nullptr;
	}

	// TODO: Create SoA style SampleTerrain which fills out a buffer iteratively, sampling each octave in turn

	// Returns a value in [0, 1]
	real SampleTerrain(volatile TerrainGenerator::TerrainChunkData* chunkData, const glm::vec2& pos)
	{
		real result = 0.0f;
		real octave = chunkData->baseOctave;
		i32 numOctaves = chunkData->numOctaves;
		u32 octaveIdx = (i32)chunkData->numOctaves - 1;
		i32 isolateOctave = chunkData->isolateOctave;
		real octaveScale = chunkData->octaveScale;

		for (u32 i = 0; i < (u32)numOctaves; ++i)
		{
			if (isolateOctave == -1 || i == (u32)isolateOctave)
			{
				result += SampleNoise(chunkData, pos, octave, octaveIdx) * (octave / octaveScale);
			}
			octave = octave / 2.0f;
			--octaveIdx;
		}
		return glm::clamp((result / (real)(numOctaves * 2.0f)) + 0.5f, 0.0f, 1.0f);
	}

	// Returns a value in [-1, 1]
	real SampleNoise(volatile TerrainGenerator::TerrainChunkData* chunkData, const glm::vec2& pos, real octave, u32 octaveIdx)
	{
		const glm::vec2 scaledPos = pos / octave;
		glm::vec2 posi = glm::vec2((real)(i32)scaledPos.x, (real)(i32)scaledPos.y);
		if (scaledPos.x < 0.0f)
		{
			posi.x -= 1;
		}
		if (scaledPos.y < 0.0f)
		{
			posi.y -= 1.0f;
		}
		glm::vec2 posf = scaledPos - posi;

		const std::vector<glm::vec2>& randomTables = (*chunkData->randomTables)[octaveIdx];
		const u32 tableEntryCount = (u32)randomTables.size();
		const u32 tableWidth = (u32)std::sqrt(tableEntryCount);

		glm::vec2* data = (glm::vec2*)randomTables.data();
		glm::vec2 r00 = *(data + ((i32)(posi.y * tableWidth + posi.x) % tableEntryCount));
		glm::vec2 r10 = *(data + ((i32)(posi.y * tableWidth + posi.x + 1) % tableEntryCount));
		glm::vec2 r01 = *(data + ((i32)((posi.y + 1) * tableWidth + posi.x) % tableEntryCount));
		glm::vec2 r11 = *(data + ((i32)((posi.y + 1) * tableWidth + posi.x + 1) % tableEntryCount));

		real posfXMinOne = posf.x - 1.0f;
		real posfYMinOne = posf.y - 1.0f;

		//real r00p = glm::dot(posf, r00);
		//real r10p = glm::dot(glm::vec2(posfXMinOne, posf.y), r10);
		//real r01p = glm::dot(glm::vec2(posf.x, posfYMinOne), r01);
		//real r11p = glm::dot(glm::vec2(posfXMinOne, posfYMinOne), r11);

		real r00p = posf.x * r00.x + posf.y * r00.y;
		real r10p = posfXMinOne * r10.x + posf.y * r10.y;
		real r01p = posf.x * r01.x + posfYMinOne * r01.y;
		real r11p = posfXMinOne * r11.x + posfYMinOne * r11.y;

		real xBlend = (posf.x);
		real yBlend = (posf.y);
		real xval0 = Lerp(r00p, r10p, xBlend);
		real xval1 = Lerp(r01p, r11p, xBlend);
		real val = Lerp(xval0, xval1, yBlend);

		return val;
	}

	// ---

	SpringObject::SpringObject(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("spring"), gameObjectID)
	{
	}

	GameObject* SpringObject::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, newObjectName, newGameObjectID);
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
			m_OriginTransform = new GameObject("Spring origin", SID("object"));
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
		m_Mesh->LoadFromMemoryDynamic(m_DynamicVertexBufferCreateInfo, m_Indices, s_SpringMatID, vertCount);

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
			real stiffness = 0.02f;
			m_SpringSim->SetStiffness(stiffness);
			m_SpringSim->SetDamping(0.999f);
			m_SpringSim->AddUniqueDistanceConstraint(0, 1, 0, stiffness);
			m_SpringSim->Initialize();
			m_SpringSim->PostInitialize();

			m_Bobber = new GameObject("Spring bobber", SID("object"));
			m_Bobber->SetSerializable(false);
			m_Bobber->SetVisibleInSceneExplorer(false);
			Mesh* bobberMesh = m_Bobber->SetMesh(new Mesh(m_Bobber));
			bobberMesh->LoadFromFile(MESH_DIRECTORY "sphere.glb", s_BobberMatID);
			m_Bobber->Initialize();
			m_Bobber->PostInitialize();
		}
		else
		{
			m_Target = new GameObject("Spring target", SID("object"));
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
			//g_Renderer->GetDebugDrawer()->drawLine(ToBtVec3(rootPos), ToBtVec3(rootPos + lookDirNorm), btVector3(1.0f, 0.0f, 0.0f), btVector3(1.0f, 0.0f, 0.0f));
			glm::vec3 up = glm::abs(glm::angle(lookDirNorm, VEC3_UP)) < 0.01f ? VEC3_FORWARD : VEC3_UP;
			glm::quat targetRot = glm::quatLookAt(lookDirNorm, up) * offsetQuat;
			if (!glm::any(glm::isnan(targetRot)))
			{
				glm::quat finalRot = glm::slerp(originTransform->GetWorldRotation(), targetRot, g_DeltaTime * 100.0f);
				originTransform->SetWorldRotation(finalRot);
			}
		}

		GameObject::Update();

		g_Renderer->GetDebugDrawer()->DrawAxes(ToBtVec3(originTransform->GetWorldPosition()), ToBtQuaternion(originTransform->GetWorldRotation()), 2.0f);

		m_SpringSim->Update();
		m_Bobber->Update();
	}

	void SpringObject::Destroy(bool bDetachFromParent /* = true */)
	{
		m_ExtendedMesh->Destroy();
		delete m_ExtendedMesh;

		m_ContractedMesh->Destroy();
		delete m_ContractedMesh;

		m_SpringSim->Destroy();
		delete m_SpringSim;
		m_SpringSim = nullptr;

		m_Bobber->Destroy();
		delete m_Bobber;
		m_Bobber = nullptr;

		GameObject::Destroy(bDetachFromParent);
	}

	void SpringObject::DrawImGuiObjects()
	{
		GameObject::DrawImGuiObjects();

		ImGui::Spacing();
		ImGui::Text("Spring");
		m_SpringSim->DrawImGuiObjects();

		ImGui::Spacing();
		ImGui::Text("Bobber");
		m_Bobber->DrawImGuiObjects();

		ImGui::Spacing();
		ImGui::Text("Origin");
		m_Bobber->DrawImGuiObjects();

	}

	void SpringObject::ParseTypeUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(scene);
		FLEX_UNUSED(matIDs);

		JSONObject springObj;
		if (parentObject.SetObjectChecked("spring", springObj))
		{
			m_TargetPos = springObj.GetVec3("end point");
		}
	}

	void SpringObject::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		JSONObject springObj = {};

		springObj.fields.emplace_back("end point", JSONValue(VecToString(m_SpringSim->points[1]->pos)));

		parentObject.fields.emplace_back("spring", JSONValue(springObj));
	}

	void SpringObject::ParseJSON(
		const JSONObject& obj,
		BaseScene* scene,
		i32 fileVersion,
		MaterialID overriddenMatID /* = InvalidMaterialID */,
		bool bIsPrefabTemplate /* = false */,
		CopyFlags copyFlags /* = CopyFlags::ALL */)
	{
		if (s_SpringMatID == InvalidMaterialID)
		{
			CreateMaterials();
		}

		GameObject::ParseJSON(obj, scene, fileVersion, overriddenMatID, bIsPrefabTemplate, copyFlags);
	}

	void SpringObject::CreateMaterials()
	{
		MaterialCreateInfo matCreateInfo = {};
		matCreateInfo.name = "Spring";
		matCreateInfo.shaderName = "pbr";
		matCreateInfo.constAlbedo = glm::vec3(0.8f, 0.05f, 0.04f);
		matCreateInfo.constRoughness = 1.0f;
		matCreateInfo.constMetallic = 0.0f;
		matCreateInfo.bDynamic = true;
		matCreateInfo.bSerializable = false;
		s_SpringMatID = g_Renderer->InitializeMaterial(&matCreateInfo);

		matCreateInfo = {};
		matCreateInfo.name = "Bobber";
		matCreateInfo.shaderName = "pbr";
		matCreateInfo.constAlbedo = glm::vec3(0.2f, 0.2f, 0.24f);
		matCreateInfo.constRoughness = 0.0f;
		matCreateInfo.constMetallic = 1.0f;
		matCreateInfo.bSerializable = false;
		s_BobberMatID = g_Renderer->InitializeMaterial(&matCreateInfo);
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

	SoftBody::SoftBody(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("soft body"), gameObjectID),
		m_SolverIterationCount(4) // Default, gets overridden in ParseUniqueFields
	{
	}

	GameObject* SoftBody::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, newObjectName, newGameObjectID);
		SoftBody* newSoftBody = new SoftBody(newObjectName, newGameObjectID);

		CopyGenericFields(newSoftBody, parent, copyFlags);

		newSoftBody->points.resize(points.size());
		newSoftBody->constraints.resize(constraints.size());
		newSoftBody->initialPositions.resize(initialPositions.size());

		glm::vec3 deltaPos = m_Transform.GetWorldPosition() - initialPositions[m_DragPointIndex];
		for (u32 i = 0; i < (u32)initialPositions.size(); ++i)
		{
			newSoftBody->initialPositions[i] = initialPositions[i] + deltaPos;
		}

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

		newSoftBody->GetTransform()->SetWorldPosition(newSoftBody->points[m_DragPointIndex]->pos);

		return newSoftBody;
	}

	void SoftBody::Initialize()
	{
		m_MSToSim = 0.0f;
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

			points[m_DragPointIndex]->pos = m_Transform.GetWorldPosition();

			u32 fixedUpdateCount = glm::min((u32)(m_MSToSim / FIXED_UPDATE_TIMESTEP), MAX_UPDATE_COUNT);

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

		//PROFILE_END("SoftBody Update");
		//m_UpdateDuration = Profiler::GetBlockDuration("SoftBody Update");

		m_Transform.SetWorldPosition(points[m_DragPointIndex]->pos);

		if (m_bRenderWireframe)
		{
			Draw();
		}
	}

	void SoftBody::Draw()
	{
		PhysicsDebugDrawBase* debugDrawer = g_Renderer->GetDebugDrawer();
		for (Point* point : points)
		{
			debugDrawer->drawSphere(ToBtVec3(point->pos), 0.1f, btVector3(0.8f, 0.2f, 0.1f));
		}

		for (u32 i = 0; i < (u32)constraints.size(); ++i)
		{
			Constraint* constraint = constraints[i];

			switch (constraint->type)
			{
			case Constraint::Type::DISTANCE:
			{
				//DistanceConstraint* distanceConstraint = (DistanceConstraint*)constraint;
				//debugDrawer->drawLine(ToBtVec3(points[distanceConstraint->pointIndices[0]]->pos), ToBtVec3(points[distanceConstraint->pointIndices[1]]->pos),
				//	btVector3(0.5f, 0.4f, 0.1f), btVector3(0.5f, 0.4f, 0.1f));
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

					debugDrawer->drawLine(ToBtVec3(points[bendingConstraint->pointIndices[0]]->pos + normal0 * f), ToBtVec3(points[bendingConstraint->pointIndices[1]]->pos + normal0 * f), col, col);
					debugDrawer->drawLine(ToBtVec3(points[bendingConstraint->pointIndices[1]]->pos + normal0 * f), ToBtVec3(points[bendingConstraint->pointIndices[2]]->pos + normal0 * f), col, col);
					debugDrawer->drawLine(ToBtVec3(points[bendingConstraint->pointIndices[2]]->pos + normal0 * f), ToBtVec3(points[bendingConstraint->pointIndices[0]]->pos + normal0 * f), col, col);
					debugDrawer->drawLine(ToBtVec3(points[bendingConstraint->pointIndices[0]]->pos + normal1 * f), ToBtVec3(points[bendingConstraint->pointIndices[1]]->pos + normal1 * f), col, col);
					debugDrawer->drawLine(ToBtVec3(points[bendingConstraint->pointIndices[0]]->pos + normal1 * f), ToBtVec3(points[bendingConstraint->pointIndices[3]]->pos + normal1 * f), col, col);
					debugDrawer->drawLine(ToBtVec3(points[bendingConstraint->pointIndices[3]]->pos + normal1 * f), ToBtVec3(points[bendingConstraint->pointIndices[1]]->pos + normal1 * f), col, col);

					glm::vec3 tri0Mid = (points[bendingConstraint->pointIndices[0]]->pos + points[bendingConstraint->pointIndices[1]]->pos + points[bendingConstraint->pointIndices[2]]->pos) / 3.0f;
					glm::vec3 tri1Mid = (points[bendingConstraint->pointIndices[0]]->pos + points[bendingConstraint->pointIndices[1]]->pos + points[bendingConstraint->pointIndices[3]]->pos) / 3.0f;

					debugDrawer->drawLine(ToBtVec3(tri0Mid), ToBtVec3(tri0Mid + normal0 * 1.0f), colN, colN);
					debugDrawer->drawLine(ToBtVec3(tri1Mid), ToBtVec3(tri1Mid + normal1 * 1.0f), colN, colN);
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
					(distanceConstraint->pointIndices[1] == index0 && distanceConstraint->pointIndices[1] == index1)))
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
		if (!m_Mesh->LoadFromFile(m_CurrentMeshFilePath, m_MeshMaterialID, true, true, &renderObjectCreateInfo))
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
				m_DragPointIndex = 0;
				glm::vec3 smallestPos(99999.0f);
				for (u32 i = 0; i < (u32)posData.size(); ++i)
				{
					glm::vec3 pos = posData[i];
					if (pos.x < smallestPos.x || (pos.x == smallestPos.x && pos.z < smallestPos.z))
					{
						m_DragPointIndex = i;
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


				points[m_DragPointIndex]->invMass = 0.0f;
				g_Renderer->SetDirtyFlags(RenderBatchDirtyFlag::DYNAMIC_DATA);
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

	void SoftBody::ParseTypeUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);
		FLEX_UNUSED(scene);

		JSONObject softBodyObject;
		if (parentObject.SetObjectChecked("soft body", softBodyObject))
		{
			softBodyObject.SetUIntChecked("solver iteration count", m_SolverIterationCount);

			std::vector<JSONObject> pointsArr;
			if (softBodyObject.SetObjectArrayChecked("points", pointsArr))
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
			if (softBodyObject.SetObjectArrayChecked("constraints", constraintsArr))
			{
				constraints.resize(constraintsArr.size());

				u32 i = 0;
				for (JSONObject& constraintObj : constraintsArr)
				{
					real stiffness = constraintObj.GetFloat("stiffness");
					Constraint::Type type = (Constraint::Type)constraintObj.GetInt("type");
					constraintObj.SetUIntChecked("dragging point index", m_DragPointIndex);

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

			if (softBodyObject.HasField("mesh file path"))
			{
				m_CurrentMeshFilePath = softBodyObject.GetString("mesh file path");
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

			softBodyObject.SetBoolChecked("render wireframe", m_bRenderWireframe);
			softBodyObject.SetFloatChecked("damping", m_Damping);
			softBodyObject.SetFloatChecked("stiffness", m_Stiffness);
			softBodyObject.SetFloatChecked("bending stiffness", m_BendingStiffness);
		}
	}

	void SoftBody::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		JSONObject softBodyObject = JSONObject();

		softBodyObject.fields.emplace_back("solver iteration count", JSONValue(m_SolverIterationCount));

		std::vector<JSONObject> pointsArr(points.size());
		{
			u32 i = 0;
			glm::vec3 parentPos = m_Transform.GetWorldPosition();
			for (const Point* point : points)
			{
				pointsArr[i] = JSONObject();
				pointsArr[i].fields.emplace_back("position", JSONValue(VecToString(point->pos - parentPos)));
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
				constraintsArr[i] = JSONObject();
				constraintsArr[i].fields.emplace_back("stiffness", JSONValue(constraint->stiffness));
				constraintsArr[i].fields.emplace_back("type", JSONValue((i32)constraint->type));
				constraintsArr[i].fields.emplace_back("dragging point index", JSONValue(m_DragPointIndex));

				switch (constraint->type)
				{
				case Constraint::Type::DISTANCE:
				{
					DistanceConstraint* distanceConstraint = (DistanceConstraint*)constraint;

					JSONObject distanceConstraintObj = JSONObject();

					distanceConstraintObj.fields.emplace_back("index 0", JSONValue(distanceConstraint->pointIndices[0]));
					distanceConstraintObj.fields.emplace_back("index 1", JSONValue(distanceConstraint->pointIndices[1]));
					distanceConstraintObj.fields.emplace_back("target distance", JSONValue(distanceConstraint->targetDistance));

					constraintsArr[i].fields.emplace_back("distance constraint", JSONValue(distanceConstraintObj));
				} break;
				case Constraint::Type::BENDING:
				{
					BendingConstraint* bendingConstraint = (BendingConstraint*)constraint;

					JSONObject bendingConstraintObj = JSONObject();

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

		softBodyObject.fields.emplace_back("render wireframe", JSONValue(m_bRenderWireframe));
		softBodyObject.fields.emplace_back("damping", JSONValue(m_Damping));
		softBodyObject.fields.emplace_back("stiffness", JSONValue(m_Stiffness));
		softBodyObject.fields.emplace_back("bending stiffness", JSONValue(m_BendingStiffness));

		parentObject.fields.emplace_back("soft body", JSONValue(softBodyObject));
	}

	void SoftBody::DrawImGuiObjects()
	{
		GameObject::DrawImGuiObjects();

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

			m_Transform.SetWorldPosition(points[m_DragPointIndex]->pos);
		}

		if (ImGui::Button("Single Step"))
		{
			m_bSingleStep = true;
			m_bPaused = true;
		}

		ImGui::Text("%.2fms", m_UpdateDuration);

		if (ImGui::Checkbox("Render wireframe", &m_bRenderWireframe))
		{
			std::vector<SoftBody*> softBodies = g_SceneManager->CurrentScene()->GetObjectsOfType<SoftBody>(SID("soft body"));
			for (SoftBody* softBody : softBodies)
			{
				softBody->m_bRenderWireframe = m_bRenderWireframe;
			}
		}

		if (ImGui::SliderFloat("Damping", &m_Damping, 0.0f, 1.0f))
		{
			std::vector<SoftBody*> softBodies = g_SceneManager->CurrentScene()->GetObjectsOfType<SoftBody>(SID("soft body"));
			for (SoftBody* softBody : softBodies)
			{
				softBody->m_Damping = m_Damping;
			}
		}

		if (ImGui::SliderFloat("Stiffness", &m_Stiffness, 0.0f, 1.0f))
		{
			std::vector<SoftBody*> softBodies = g_SceneManager->CurrentScene()->GetObjectsOfType<SoftBody>(SID("soft body"));
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
			std::vector<SoftBody*> softBodies = g_SceneManager->CurrentScene()->GetObjectsOfType<SoftBody>(SID("soft body"));
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

	Vehicle::Vehicle(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("vehicle"), gameObjectID),
		m_WheelSlipHisto(RollingAverage<real>(256, SamplingType::CONSTANT))
	{
		m_bInteractable = true;

		for (i32 i = 0; i < (i32)SoundEffect::_COUNT; ++i)
		{
			m_SoundEffects[i] = SoundClip_LoopingSimple(VehicleSoundEffectNames[i], InvalidStringID);
			m_SoundEffectSIDs[i] = InvalidStringID;
		}
	}

	GameObject* Vehicle::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, newObjectName, newGameObjectID);
		Vehicle* newGameObject = new Vehicle(newObjectName, newGameObjectID);

		CopyGenericFields(newGameObject, parent, copyFlags);

		// Children now exist
		memset(&newGameObject->m_TireIDs[0], 0, sizeof(GameObjectID) * m_TireCount);
		memset(&newGameObject->m_BrakeLightIDs[0], 0, sizeof(GameObjectID) * 2);

		// Temporarily set sibling indices as if these objects are both root objects (this will
		// be overwritten by the proper values soon)
		UpdateSiblingIndices(0);
		newGameObject->UpdateSiblingIndices(0);

		for (i32 i = 0; i < m_TireCount; ++i)
		{
			MatchCorrespondingID(m_TireIDs[i], newGameObject, TireNames[i], newGameObject->m_TireIDs[i]);
		}

		for (i32 i = 0; i < 2; ++i)
		{
			MatchCorrespondingID(m_BrakeLightIDs[i], newGameObject, BrakeLightNames[i], newGameObject->m_BrakeLightIDs[i]);
		}

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
			brakeLightL->GetTypeID() == SID("spot light") &&
			brakeLightR->GetTypeID() == SID("spot light"))
		{
			((SpotLight*)brakeLightL)->SetVisible(false, true);
			((SpotLight*)brakeLightR)->SetVisible(false, true);
		}
		else
		{
			PrintError("Expected vehicle brake light objects to be spot lights\n");
		}

		m_GlassMatID = GetMesh()->GetSubMesh(0)->GetMaterialID();
		m_CarPaintMatID = GetMesh()->GetSubMesh(1)->GetMaterialID();
		m_BrakeLightMatID = GetMesh()->GetSubMesh(2)->GetMaterialID();
		m_ReverseLightMatID = GetMesh()->GetSubMesh(3)->GetMaterialID();
		m_TireMatID = tireFL->GetMesh()->GetSubMesh(0)->GetMaterialID();
		m_SpokeMatID = tireFL->GetMesh()->GetSubMesh(1)->GetMaterialID();

		m_InitialBrakeLightMatEmissive = g_Renderer->GetMaterial(m_BrakeLightMatID)->constEmissive;
		m_InitialReverseLightMatEmissive = g_Renderer->GetMaterial(m_ReverseLightMatID)->constEmissive;

		m_ActiveBrakeLightMatEmissive = glm::min(m_InitialBrakeLightMatEmissive + glm::vec4(0.2f, 0.0f, 0.0f, 0.0f), VEC4_ONE);
		m_ActiveReverseLightMatEmissive = glm::min(m_InitialReverseLightMatEmissive + glm::vec4(0.4f), VEC4_ONE);

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
		for (i32 i = 0; i < (i32)SoundEffect::_COUNT; ++i)
		{
			if (m_SoundEffectSIDs[i] != InvalidStringID)
			{
				SetSoundEffectSID((SoundEffect)i, m_SoundEffectSIDs[i]);
			}
		}
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
		GameObject::Update();

		btRigidBody* rb = m_RigidBody->GetRigidBodyInternal();
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

		bool bOccupied =
			(m_ObjectInteractingWith != nullptr) &&
			(m_ObjectInteractingWith->GetTypeID() == SID("player")) &&
			g_CameraManager->CurrentCamera()->bIsGameplayCam;

		if (bOccupied)
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

		BaseScene* scene = g_SceneManager->CurrentScene();
		GameObject* brakeLightL = scene->GetGameObject(m_BrakeLightIDs[0]);
		GameObject* brakeLightR = scene->GetGameObject(m_BrakeLightIDs[1]);
		if (brakeLightL != nullptr && brakeLightR != nullptr &&
			brakeLightL->GetTypeID() == SID("spot light") &&
			brakeLightR->GetTypeID() == SID("spot light"))
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
			Transform newWheelTransform = ToTransform(wheelInfo.m_worldTransform);
			scene->GetGameObject(m_TireIDs[i])->GetTransform()->SetWorldRotation(newWheelTransform.GetWorldRotation());

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

	void Vehicle::ParseTypeUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(scene);
		FLEX_UNUSED(matIDs);

		JSONObject vehicleObj;
		if (parentObject.SetObjectChecked("vehicle", vehicleObj))
		{
			std::vector<JSONField> tireIDs;
			if (vehicleObj.SetFieldArrayChecked("tire ids", tireIDs))
			{
				assert(m_TireCount == (i32)tireIDs.size());
				for (i32 i = 0; i < m_TireCount; ++i)
				{
					m_TireIDs[i] = GameObjectID::FromString(tireIDs[i].label);
				}
			}

			std::vector<JSONField> brakeLightIDs;
			if (vehicleObj.SetFieldArrayChecked("brake light ids", brakeLightIDs))
			{
				assert((i32)brakeLightIDs.size() == 2);
				for (i32 i = 0; i < 2; ++i)
				{
					m_BrakeLightIDs[i] = GameObjectID::FromString(brakeLightIDs[i].label);
				}
			}

			std::vector<JSONField> soundEffectSIDs;
			if (vehicleObj.SetFieldArrayChecked("sound effect sids", soundEffectSIDs))
			{
				i32 count = std::min((i32)SoundEffect::_COUNT, (i32)soundEffectSIDs.size());
				for (i32 i = 0; i < count; ++i)
				{
					m_SoundEffectSIDs[i] = ParseULong(soundEffectSIDs[i].label);
				}
			}
		}
	}

	void Vehicle::ParseInstanceUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(parentObject);
		FLEX_UNUSED(scene);
		FLEX_UNUSED(matIDs);
	}

	void Vehicle::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		JSONObject vehicleObj = JSONObject();

		std::vector<JSONField> tireIDs;
		tireIDs.reserve(m_TireCount);
		for (i32 i = 0; i < m_TireCount; ++i)
		{
			JSONField tireIDField = {};
			tireIDField.label = m_TireIDs[i].ToString();
			tireIDs.push_back(tireIDField);
		}

		vehicleObj.fields.emplace_back("tire ids", JSONValue(tireIDs));

		std::vector<JSONField> brakeLightIDs;
		brakeLightIDs.reserve(2);
		for (i32 i = 0; i < 2; ++i)
		{
			JSONField brakeLightIDField = {};
			brakeLightIDField.label = m_BrakeLightIDs[i].ToString();
			brakeLightIDs.push_back(brakeLightIDField);
		}

		vehicleObj.fields.emplace_back("brake light ids", JSONValue(brakeLightIDs));

		std::vector<JSONField> soundEffectSIDs;
		soundEffectSIDs.resize(m_SoundEffectSIDs.size());
		for (i32 i = 0; i < (i32)m_SoundEffectSIDs.size(); ++i)
		{
			soundEffectSIDs[i].label = ULongToString(m_SoundEffectSIDs[i]);
		}
		vehicleObj.fields.emplace_back("sound effect sids", JSONValue(soundEffectSIDs));

		parentObject.fields.emplace_back("vehicle", JSONValue(vehicleObj));
	}

	void Vehicle::SerializeInstanceUniqueFields(JSONObject& parentObject) const
	{
		FLEX_UNUSED(parentObject);
	}

	void Vehicle::DrawImGuiObjects()
	{
		GameObject::DrawImGuiObjects();

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
				Transform newWheelTransform = ToTransform(wheelInfo.m_worldTransform);
				GameObject* tireObject = scene->GetGameObject(m_TireIDs[i]);
				Transform* tireTransform = tireObject->GetTransform();
				tireTransform->SetWorldRotation(newWheelTransform.GetWorldRotation());

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

	bool Vehicle::AllowInteractionWith(GameObject* gameObject)
	{
		return gameObject->GetTypeID() == SID("player");
	}

	void Vehicle::SetInteractingWith(GameObject* gameObject)
	{
		//if (gameObject != nullptr && gameObject->GetTypeID() == SID("player"))
		//{
		//	Player* player = static_cast<Player*>(gameObject);
		//}

		if (gameObject == nullptr)
		{
			// Player has left the vehicle, stop accelerating & braking
			m_EngineForce = 0.0f;
			m_BrakeForce = 0.0f;
		}

		GameObject::SetInteractingWith(gameObject);
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
		m_SoundEffects[(i32)soundEffect] = SoundClip_LoopingSimple(VehicleSoundEffectNames[(i32)soundEffect], soundSID);
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
		m_Transform.SetWorldFromMatrix(MAT4_IDENTITY);
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

	Road::Road(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */) :
		GameObject(name, SID("road"), gameObjectID)
	{
		m_bSerializeMesh = false;
		m_bSerializeMaterial = false;
		AddTag("road");
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
		glm::vec3 start = glm::vec3(4.0f, 0.0f, 0.0f);

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
				start,
				glm::vec3(9.0f, 0.0f, 64.0f),
				glm::vec3(60.0f, 0.0, 84.0f),
				glm::vec3(96.0f, 0.0, 72.0f),
				glm::vec3(104.0f, 0.0, 32.0f),
				glm::vec3(64.0f, 0.0, -24.0f),
				start
			};

			if (terrainGenerator != nullptr)
			{
				real offset = 0.5f;

				for (u32 i = 1; i < (u32)newPoints.size() - 1; ++i)
				{
					newPoints[i].y = terrainGenerator->Sample(glm::vec2(newPoints[i].x, newPoints[i].z)) + offset;
				}
			}

			GenerateSegmentsThroughPoints(newPoints);

			terrainGenerator->UpdateRoadSegments();
		}
		else
		{
			for (u32 i = 0; i < (u32)roadSegments.size(); ++i)
			{
				GenerateSegment(i);
			}
		}
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
		GameObject::Update();

		const PhysicsDebuggingSettings& debuggingSettings = g_Renderer->GetPhysicsDebuggingSettings();
		if (!debuggingSettings.bDisableAll &&
			g_Renderer->GetDebugDrawer()->getDebugMode() != btIDebugDraw::DebugDrawModes::DBG_NoDebug)
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

	void Road::DrawImGuiObjects()
	{
		GameObject::DrawImGuiObjects();

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
			//pos.y += 0.1f;
			glm::vec3 up = VEC3_UP;
			glm::vec3 forward = glm::normalize(curve.GetFirstDerivativeOnCurve(t));
			glm::vec3 tangent = glm::cross(forward, up);

			real xScales[] = { -width, width };
			for (u32 x = 0; x < 2; ++x)
			{
				glm::vec3 vertPosWS = pos + xScales[x] * tangent;
				glm::vec3 normal = up;// glm::normalize(glm::vec3(heightDX, 2.0f * e, heightDZ));
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
			assert(roadSegments[index].mesh == nullptr);
			roadSegments[index].mesh = meshComponent;
		}

		CreateRigidBody(index);
	}

	GameObject* Road::CopySelf(GameObject* parent, CopyFlags copyFlags, std::string* optionalName, const GameObjectID& optionalGameObjectID)
	{
		FLEX_UNUSED(copyFlags);

		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, newObjectName, newGameObjectID);
		Road* newGameObject = new Road(newObjectName, newGameObjectID);

		CopyGenericFields(newGameObject, parent, CopyFlags::ADD_TO_SCENE);

		newGameObject->roadSegments = roadSegments;
		newGameObject->m_RoadMaterialID = m_RoadMaterialID;
		newGameObject->m_QuadCountPerSegment = m_QuadCountPerSegment;

		return newGameObject;
	}

	void Road::GenerateMaterial()
	{
		if (m_RoadMaterialID == InvalidMaterialID)
		{
			// TODO: Add serialized member which points at material ID once support for that is in
			MaterialCreateInfo matCreateInfo = {};
			matCreateInfo.name = "Road surface";
			matCreateInfo.shaderName = "pbr";
			matCreateInfo.bDynamic = false;
			matCreateInfo.constRoughness = 0.95f;
			matCreateInfo.constMetallic = 0.0f;
			matCreateInfo.constAlbedo = glm::vec3(0.25f, 0.25f, 0.28f);
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
		RigidBody* rigidBody = new RigidBody((i32)CollisionType::STATIC, (i32)CollisionType::DEFAULT & ~(i32)CollisionType::STATIC);
		rigidBody->SetStatic(true);
		rigidBody->Initialize(shape, &m_Transform);
		m_RigidBodies[meshIndex] = rigidBody;
	}
} // namespace flex
