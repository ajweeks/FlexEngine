#include "stdafx.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletCollision/CollisionShapes/btConeShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletDynamics/ConstraintSolver/btFixedConstraint.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <LinearMath/btIDebugDraw.h>
#include <LinearMath/btTransform.h>

#include <glm/gtx/quaternion.hpp> // for rotate
#include <glm/gtx/norm.hpp> // for distance2

#include <imgui/imgui_internal.h> // For PushItemFlag

#include <random>
IGNORE_WARNINGS_POP

#include "Scene/GameObject.hpp"
#include "Audio/AudioManager.hpp"
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
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "Time.hpp"
#include "VirtualMachine.hpp"
#include "Window/Window.hpp"

namespace flex
{
	const char* GameObject::s_DefaultNewGameObjectName = "New_Game_Object_00";

	const char* Cart::emptyCartMeshName = "cart-empty.glb";
	const char* EngineCart::engineMeshName = "cart-engine.glb";

	AudioCue GameObject::s_SqueakySounds;
	AudioSourceID GameObject::s_BunkSound;

	GameObject::GameObject(const std::string& name, GameObjectType type) :
		m_Name(name),
		m_Type(type)
	{
		m_Transform.SetAsIdentity();
		m_Transform.SetGameObject(this);

		if (!s_SqueakySounds.IsInitialized())
		{
			s_SqueakySounds.Initialize(SFX_LOCATION "squeak00.wav", 5);

			s_BunkSound = AudioManager::AddAudioSource(SFX_LOCATION "bunk.wav");
		}
	}

	GameObject::~GameObject()
	{
	}

	GameObject* GameObject::CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren)
	{
		GameObject* newGameObject = new GameObject(GetIncrementedPostFixedStr(m_Name, s_DefaultNewGameObjectName), m_Type);

		CopyGenericFields(newGameObject, parent, bCopyChildren);

		return newGameObject;
	}

	GameObject* GameObject::CreateObjectFromJSON(const JSONObject& obj, BaseScene* scene, i32 fileVersion)
	{
		GameObject* newGameObject = nullptr;

		std::string gameObjectTypeStr = obj.GetString("type");

		if (gameObjectTypeStr.compare("prefab") == 0)
		{
			std::string prefabTypeStr = obj.GetString("prefab type");
			JSONObject* prefab = nullptr;

			for (JSONObject& parsedPrefab : scene->s_ParsedPrefabs)
			{
				if (parsedPrefab.GetString("name").compare(prefabTypeStr) == 0)
				{
					prefab = &parsedPrefab;
				}
			}

			if (prefab == nullptr)
			{
				PrintError("Invalid prefab type: %s\n", prefabTypeStr.c_str());

				return nullptr;
			}
			else
			{
				std::string name = obj.GetString("name");

				GameObject* prefabInstance = GameObject::CreateObjectFromJSON(*prefab, scene, fileVersion);
				prefabInstance->m_bLoadedFromPrefab = true;
				prefabInstance->m_PrefabName = prefabInstance->m_Name;

				prefabInstance->m_Name = name;

				bool bVisible = true;
				obj.SetBoolChecked("visible", bVisible);
				prefabInstance->SetVisible(bVisible, false);

				JSONObject transformObj;
				if (obj.SetObjectChecked("transform", transformObj))
				{
					prefabInstance->m_Transform = Transform::ParseJSON(transformObj);
				}

				prefabInstance->ParseUniqueFields(obj, scene, prefabInstance->m_Mesh->GetMaterialIDs());

				return prefabInstance;
			}
		}

		GameObjectType gameObjectType = StringToGameObjectType(gameObjectTypeStr.c_str());

		std::string objectName = obj.GetString("name");

		// TODO: Use managers here to spawn objects!
		switch (gameObjectType)
		{
		case GameObjectType::PLAYER:
			PrintError("Player was serialized to scene file!\n");
			break;
		case GameObjectType::SKYBOX:
			newGameObject = new Skybox(objectName);
			break;
		case GameObjectType::REFLECTION_PROBE:
			newGameObject = new ReflectionProbe(objectName);
			break;
		case GameObjectType::VALVE:
			newGameObject = new Valve(objectName);
			break;
		case GameObjectType::RISING_BLOCK:
			newGameObject = new RisingBlock(objectName);
			break;
		case GameObjectType::GLASS_PANE:
			newGameObject = new GlassPane(objectName);
			break;
		case GameObjectType::POINT_LIGHT:
			newGameObject = new PointLight(objectName);
			break;
		case GameObjectType::DIRECTIONAL_LIGHT:
			newGameObject = new DirectionalLight(objectName);
			break;
		case GameObjectType::CART:
		{
			CartManager* cartManager = g_SceneManager->CurrentScene()->GetCartManager();
			CartID newCartID = cartManager->CreateCart(objectName);
			newGameObject = cartManager->GetCart(newCartID);
		} break;
		case GameObjectType::MOBILE_LIQUID_BOX:
		{
			newGameObject = new MobileLiquidBox(objectName);
		} break;
		case GameObjectType::TERMINAL:
		{
			newGameObject = new Terminal(objectName);
		} break;
		case GameObjectType::GERSTNER_WAVE:
		{
			newGameObject = new GerstnerWave(objectName);
		} break;
		case GameObjectType::BLOCKS:
		{
			newGameObject = new Blocks(objectName);
		} break;
		case GameObjectType::PARTICLE_SYSTEM:
		{
			newGameObject = new ParticleSystem(objectName);
		} break;
		case GameObjectType::CHUNK_GENERATOR:
		{
			newGameObject = new ChunkGenerator(objectName);
		} break;
		case GameObjectType::OBJECT: // Fall through
		case GameObjectType::_NONE:
			newGameObject = new GameObject(objectName, gameObjectType);
			break;
		default:
			PrintError("Unhandled game object type in CreateGameObjectFromJSON\n");
			ENSURE_NO_ENTRY();
		}

		if (newGameObject != nullptr)
		{
			newGameObject->ParseJSON(obj, scene, fileVersion);
		}

		return newGameObject;
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

		if (rb)
		{
			rb->GetRigidBodyInternal()->setUserPointer(this);
		}

		for (GameObject* child : m_Children)
		{
			child->PostInitialize();
		}

		m_Transform.UpdateParentTransform();
	}

	void GameObject::Destroy()
	{
		for (GameObject* child : m_Children)
		{
			child->Destroy();
			delete child;
		}
		m_Children.clear();

		if (m_Mesh)
		{
			m_Mesh->Destroy();
			delete m_Mesh;
			m_Mesh = nullptr;
		}

		if (m_RigidBody)
		{
			m_RigidBody->Destroy();
			delete m_RigidBody;
			m_RigidBody = nullptr;
		}

		if (m_CollisionShape)
		{
			delete m_CollisionShape;
			m_CollisionShape = nullptr;
		}

		if (g_Editor->IsObjectSelected(this))
		{
			g_Editor->DeselectObject(this);
		}
	}

	void GameObject::Update()
	{
		if (m_ObjectInteractingWith)
		{
			// TODO: Write real fancy-lookin outline shader instead of drawing a lil cross
			btIDebugDraw* debugDrawer = g_Renderer->GetDebugDrawer();
			btVector3 pos = ToBtVec3(m_Transform.GetWorldPosition());
			debugDrawer->drawLine(pos + btVector3(-1, 0.1f, 0), pos + btVector3(1, 0.1f, 0), btVector3(0.95f, 0.1f, 0.1f));
			debugDrawer->drawLine(pos + btVector3(0, 0.1f, -1), pos + btVector3(0, 0.1f, 1), btVector3(0.95f, 0.1f, 0.1f));
		}
		else if (m_bInteractable)
		{
			btIDebugDraw* debugDrawer = g_Renderer->GetDebugDrawer();
			btVector3 pos = ToBtVec3(m_Transform.GetWorldPosition());
			debugDrawer->drawLine(pos + btVector3(-1, 0.1f, 0), pos + btVector3(1, 0.1f, 0), btVector3(0.95f, 0.95f, 0.1f));
			debugDrawer->drawLine(pos + btVector3(0, 0.1f, -1), pos + btVector3(0, 0.1f, 1), btVector3(0.95f, 0.95f, 0.1f));
		}

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
		//GLRenderObject* renderObject = nullptr;
		std::string objectID = "##";
		//if (m_RenderID != InvalidRenderID)
		//{
		//	//renderObject = GetRenderObject(m_RenderID);
		//	objectID += std::to_string(renderObject->renderID);

		//	if (!gameObject->IsVisibleInSceneExplorer())
		//	{
		//		return;
		//	}
		//}

		ImGui::Text("%s : %s", m_Name.c_str(), GameObjectTypeStrings[(i32)m_Type]);

		if (DoImGuiContextMenu(true))
		{
			// Early return if object was just deleted
			return;
		}

		const std::string objectVisibleLabel("Visible" + objectID + m_Name);
		bool bVisible = m_bVisible;
		if (ImGui::Checkbox(objectVisibleLabel.c_str(), &bVisible))
		{
			SetVisible(bVisible);
		}

		ImGui::SameLine();

		bool bStatic = m_bStatic;
		if (ImGui::Checkbox("Static", &bStatic))
		{
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
				m_Transform.SetLocalPosition(translation, false);

				glm::quat rotQuat(glm::quat(glm::radians(cleanedRot)));

				m_Transform.SetLocalRotation(rotQuat, false);
				m_Transform.SetLocalScale(scale, true);
				SetUseUniformScale(m_bUniformScale, false);

				if (m_RigidBody)
				{
					m_RigidBody->MatchParentTransform();
				}

				if (g_Editor->IsObjectSelected(this))
				{
					g_Editor->CalculateSelectedObjectsCenter();
				}
			}
		}

		if (m_Mesh != nullptr)
		{
			g_Renderer->DrawImGuiForGameObject(this);
		}
		else
		{
			if (ImGui::Button("Add mesh component"))
			{
				Mesh* mesh = SetMesh(new Mesh(this));
				mesh->LoadFromFile(MESH_DIRECTORY "cube.glb", g_Renderer->GetPlaceholderMaterialID());
			}
		}

		if (m_RigidBody)
		{
			ImGui::Spacing();

			btRigidBody* rbInternal = m_RigidBody->GetRigidBodyInternal();

			ImGui::Text("Rigid body");

			if (ImGui::BeginPopupContextItem("rb context menu"))
			{
				if (ImGui::Button("Remove rigid body"))
				{
					RemoveRigidBody();
				}

				ImGui::EndPopup();
			}

			if (m_RigidBody != nullptr)
			{
				bool bRBStatic = m_RigidBody->IsStatic();
				if (ImGui::Checkbox("Static##rb", &bRBStatic))
				{
					m_RigidBody->SetStatic(bRBStatic);
				}

				bool bKinematic = m_RigidBody->IsKinematic();
				if (ImGui::Checkbox("Kinematic", &bKinematic))
				{
					m_RigidBody->SetKinematic(bKinematic);
				}

				ImGui::PushItemWidth(80.0f);
				{
					i32 group = m_RigidBody->GetGroup();
					if (ImGui::InputInt("Group", &group, 1, 16))
					{
						group = glm::clamp(group, -1, 16);
						m_RigidBody->SetGroup(group);
					}

					ImGui::SameLine();

					i32 mask = m_RigidBody->GetMask();
					if (ImGui::InputInt("Mask", &mask, 1, 16))
					{
						mask = glm::clamp(mask, -1, 16);
						m_RigidBody->SetMask(mask);
					}
				}
				ImGui::PopItemWidth();

				// TODO: Array of buttons for each category
				i32 flags = m_RigidBody->GetPhysicsFlags();
				if (ImGui::SliderInt("Flags", &flags, 0, 16))
				{
					m_RigidBody->SetPhysicsFlags(flags);
				}

				real mass = m_RigidBody->GetMass();
				if (ImGui::SliderFloat("Mass", &mass, 0.0f, 1000.0f))
				{
					m_RigidBody->SetMass(mass);
				}

				real friction = m_RigidBody->GetFriction();
				if (ImGui::SliderFloat("Friction", &friction, 0.0f, 1.0f))
				{
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
					m_RigidBody->SetLocalPosition(localOffsetPos);
				}
				if (ImGui::IsItemClicked(1))
				{
					m_RigidBody->SetLocalPosition(VEC3_ZERO);
				}

				glm::vec3 localOffsetRotEuler = glm::degrees(glm::eulerAngles(m_RigidBody->GetLocalRotation()));
				glm::vec3 cleanedRot;
				if (DoImGuiRotationDragFloat3("Rot offset", localOffsetRotEuler, cleanedRot))
				{
					m_RigidBody->SetLocalRotation(glm::quat(glm::radians(cleanedRot)));
				}

				ImGui::Spacing();

				glm::vec3 linearVel = ToVec3(m_RigidBody->GetRigidBodyInternal()->getLinearVelocity());
				if (ImGui::DragFloat3("linear vel", &linearVel.x, 0.05f))
				{
					rbInternal->setLinearVelocity(ToBtVec3(linearVel));
				}
				if (ImGui::IsItemClicked(1))
				{
					rbInternal->setLinearVelocity(btVector3(0.0f, 0.0f, 0.0f));
				}

				glm::vec3 angularVel = ToVec3(m_RigidBody->GetRigidBodyInternal()->getAngularVelocity());
				if (ImGui::DragFloat3("angular vel", &angularVel.x, 0.05f))
				{
					rbInternal->setAngularVelocity(ToBtVec3(angularVel));
				}
				if (ImGui::IsItemClicked(1))
				{
					rbInternal->setAngularVelocity(btVector3(0.0f, 0.0f, 0.0f));
				}


				//glm::vec3 localOffsetScale = m_RigidBody->GetLocalScale();
				//if (ImGui::DragFloat3("Scale offset", &localOffsetScale.x, 0.01f))
				//{
				//	real epsilon = 0.001f;
				//	localOffsetScale.x = glm::max(localOffsetScale.x, epsilon);
				//	localOffsetScale.y = glm::max(localOffsetScale.y, epsilon);
				//	localOffsetScale.z = glm::max(localOffsetScale.z, epsilon);

				//	m_RigidBody->SetLocalScale(localOffsetScale);
				//}

			}
		}
		else
		{
			if (ImGui::Button("Add rigid body"))
			{
				RigidBody* rb = SetRigidBody(new RigidBody());
				btVector3 btHalfExtents(1.0f, 1.0f, 1.0f);
				btBoxShape* boxShape = new btBoxShape(btHalfExtents);

				SetCollisionShape(boxShape);
				rb->Initialize(boxShape, &m_Transform);
				rb->GetRigidBodyInternal()->setUserPointer(this);
			}
		}
	}

	bool GameObject::DoImGuiContextMenu(bool bActive)
	{
		bool bDeletedOrDuplicated = false;

		// TODO: Prevent name collisions
		std::string contextMenuIDStr = "context window game object " + m_Name;
		static std::string newObjectName = m_Name;
		const size_t maxStrLen = 256;

		bool bRefreshNameField = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
			ImGui::IsMouseClicked(1);

		if (bActive && g_Editor->GetWantRenameActiveElement())
		{
			ImGui::OpenPopup(contextMenuIDStr.c_str());
			g_Editor->ClearWantRenameActiveElement();
			bRefreshNameField = true;
		}
		if (bRefreshNameField)
		{
			newObjectName = m_Name;
			newObjectName.resize(maxStrLen);
		}

		if (ImGui::BeginPopupContextItem(contextMenuIDStr.c_str()))
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

			if (DoDuplicateGameObjectButton("Duplicate..."))
			{
				ImGui::CloseCurrentPopup();
				bDeletedOrDuplicated = true;
			}

			ImGui::SameLine();

			if (ImGui::Button("Delete"))
			{
				if (g_SceneManager->CurrentScene()->DestroyGameObject(this, true))
				{
					bDeletedOrDuplicated = true;
				}
				else
				{
					PrintWarn("Failed to delete game object: %s\n", m_Name.c_str());
				}
			}

			ImGui::EndPopup();
		}

		return bDeletedOrDuplicated;
	}

	bool GameObject::DoDuplicateGameObjectButton(const char* buttonName)
	{
		if (ImGui::Button(buttonName))
		{
			GameObject* newGameObject = CopySelfAndAddToScene(nullptr, true);

			g_Editor->SetSelectedObject(newGameObject);

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
		m_bBeingInteractedWith = (gameObject != nullptr);
	}

	bool GameObject::IsBeingInteractedWith() const
	{
		return m_bBeingInteractedWith;
	}

	GameObject* GameObject::GetObjectInteractingWith()
	{
		return m_ObjectInteractingWith;
	}

	void GameObject::ParseJSON(const JSONObject& obj, BaseScene* scene, i32 fileVersion, MaterialID overriddenMatID /* = InvalidMaterialID */)
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
			matIDs = scene->RetrieveMaterialIDsFromJSON(obj, fileVersion);
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

		std::string meshName;
		if (obj.SetStringChecked("mesh", meshName))
		{
			bool bFound = false;
			for (const JSONObject& parsedMeshObj : BaseScene::s_ParsedMeshes)
			{
				std::string fileName = StripFileType(StripLeadingDirectories(parsedMeshObj.GetString("file")));

				if (fileName.compare(meshName) == 0)
				{
					Mesh::ParseJSON(parsedMeshObj, this, matIDs);
					bFound = true;
					break;
				}
			}

			if (!bFound)
			{
				PrintWarn("Failed to find mesh with name %s in BaseScene::s_ParsedMeshes\n", meshName.c_str());
			}
		}

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

		ParseUniqueFields(obj, scene, matIDs);

		SetVisible(bVisible, false);
		SetVisibleInSceneExplorer(bVisibleInSceneGraph);

		bool bStatic = false;
		if (obj.SetBoolChecked("static", bStatic))
		{
			SetStatic(bStatic);
		}

		if (obj.HasField("children"))
		{
			std::vector<JSONObject> children = obj.GetObjectArray("children");
			for (JSONObject& child : children)
			{
				AddChild(GameObject::CreateObjectFromJSON(child, scene, fileVersion));
			}
		}
	}

	void GameObject::ParseUniqueFields(const JSONObject& /* parentObj */, BaseScene* /* scene */, const std::vector<MaterialID>& /* matIDs */)
	{
		// Generic game objects have no unique fields
	}

	JSONObject GameObject::Serialize(const BaseScene* scene) const
	{
		JSONObject object = {};

		if (!m_bSerializable)
		{
			PrintError("Attempted to serialize non-serializable object with name \"%s\"\n", m_Name.c_str());
			return object;
		}


		// Non-basic objects shouldn't serialize certain fields which are constant across all instances
		// TODO: Save out overridden prefab fields
		bool bIsBasicObject = (m_Type == GameObjectType::OBJECT ||
			m_Type == GameObjectType::_NONE);


		object.fields.emplace_back("name", JSONValue(m_Name));

		if (m_bLoadedFromPrefab)
		{
			object.fields.emplace_back("type", JSONValue(std::string("prefab")));
			object.fields.emplace_back("prefab type", JSONValue(m_PrefabName));
		}
		else
		{
			object.fields.emplace_back("type", JSONValue(GameObjectTypeToString(m_Type)));
		}

		object.fields.emplace_back("visible", JSONValue(IsVisible()));
		// TODO: Only save/read this value when editor is included in build
		if (!IsVisibleInSceneExplorer())
		{
			object.fields.emplace_back("visible in scene graph", JSONValue(IsVisibleInSceneExplorer()));
		}

		if (IsStatic())
		{
			object.fields.emplace_back("static", JSONValue(true));
		}

		object.fields.push_back(m_Transform.Serialize());

		if (m_Mesh &&
			bIsBasicObject &&
			!m_bLoadedFromPrefab)
		{
			const std::string meshName = StripFileType(StripLeadingDirectories(m_Mesh->GetRelativeFilePath()));
			object.fields.emplace_back("mesh", JSONValue(meshName));
		}

		if (m_Mesh)
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
					const Material& material = g_Renderer->GetMaterial(matID);
					std::string materialName = material.name;
					if (materialName.empty())
					{
						PrintWarn("Game object contains material with empty material name!\n");
					}
					else
					{
						materialFields.emplace_back(materialName, JSONValue(""));
					}
				}
			}

			object.fields.emplace_back("materials", JSONValue(materialFields));
		}

		btCollisionShape* collisionShape = GetCollisionShape();
		if (collisionShape &&
			!m_bLoadedFromPrefab)
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
			!m_bLoadedFromPrefab)
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

		SerializeUniqueFields(object);

		if (!m_Children.empty())
		{
			std::vector<JSONObject> childrenToSerialize;

			for (GameObject* child : m_Children)
			{
				if (child->IsSerializable())
				{
					childrenToSerialize.push_back(child->Serialize(scene));
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

	void GameObject::SerializeUniqueFields(JSONObject& /* parentObject */) const
	{
		// Generic game objects have no unique fields
	}

	void GameObject::AddSelfAndChildrenToVec(std::vector<GameObject*>& vec)
	{
		if (Find(vec, this) == vec.end())
		{
			vec.push_back(this);
		}

		for (GameObject* child : m_Children)
		{
			if (Find(vec, child) == vec.end())
			{
				vec.push_back(child);
			}

			child->AddSelfAndChildrenToVec(vec);
		}
	}

	void GameObject::RemoveSelfAndChildrenToVec(std::vector<GameObject*>& vec)
	{
		auto iter = Find(vec, this);
		if (iter != vec.end())
		{
			vec.erase(iter);
		}

		for (GameObject* child : m_Children)
		{
			auto childIter = Find(vec, child);
			if (childIter != vec.end())
			{
				vec.erase(childIter);
			}

			child->RemoveSelfAndChildrenToVec(vec);
		}
	}

	GameObjectType GameObject::GetType() const
	{
		return m_Type;
	}

	void GameObject::CopyGenericFields(GameObject* newGameObject, GameObject* parent, bool bCopyChildren)
	{
		RenderObjectCreateInfo* createInfoPtr = nullptr;
		std::vector<MaterialID> matIDs;
		if (m_Mesh)
		{
			matIDs = m_Mesh->GetMaterialIDs();
		}

		*newGameObject->GetTransform() = m_Transform;

		if (parent)
		{
			parent->AddChild(newGameObject);
		}
		else
		{
			if (m_Parent)
			{
				m_Parent->AddChild(newGameObject);
			}
			else
			{
				g_SceneManager->CurrentScene()->AddRootObject(newGameObject);
			}
		}

		for (const std::string& tag : m_Tags)
		{
			newGameObject->AddTag(tag);
		}

		if (m_Mesh)
		{
			Mesh* newMesh = newGameObject->SetMesh(new Mesh(newGameObject));
			Mesh::Type prefabType = m_Mesh->GetType();
			if (prefabType == Mesh::Type::PREFAB)
			{
				PrefabShape shape = m_Mesh->GetSubMeshes()[0]->GetShape();
				newMesh->LoadPrefabShape(shape, matIDs[0], createInfoPtr);
			}
			else if (prefabType == Mesh::Type::FILE)
			{
				std::string filePath = m_Mesh->GetRelativeFilePath();
				MeshImportSettings importSettings = m_Mesh->GetImportSettings();
				newMesh->LoadFromFile(filePath, matIDs, &importSettings, createInfoPtr);
			}
			else
			{
				PrintError("Unhandled mesh component prefab type encountered while duplicating object\n");
			}
		}

		if (m_RigidBody)
		{
			newGameObject->SetRigidBody(new RigidBody(*m_RigidBody));

			btCollisionShape* pCollisionShape = m_RigidBody->GetRigidBodyInternal()->getCollisionShape();
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

		newGameObject->Initialize();
		newGameObject->PostInitialize();

		if (bCopyChildren)
		{
			for (GameObject* child : m_Children)
			{
				std::string newChildName = child->GetName();
				GameObject* newChild = child->CopySelfAndAddToScene(newGameObject, bCopyChildren);
				newGameObject->AddChild(newChild);
			}
		}
	}

	GameObject* GameObject::GetParent()
	{
		return m_Parent;
	}

	void GameObject::DetachFromParent()
	{
		if (m_Parent)
		{
			m_Parent->RemoveChild(this);
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
		if (!child)
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

		g_SceneManager->CurrentScene()->UpdateRootObjectSiblingIndices();

		return child;
	}

	bool GameObject::RemoveChild(GameObject* child)
	{
		if (!child)
		{
			return false;
		}

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			if (*iter == child)
			{
				glm::mat4 childWorldTransform = child->GetTransform()->GetWorldTransform();

				child->SetParent(nullptr);

				child->GetTransform()->SetWorldTransform(childWorldTransform);

				g_SceneManager->CurrentScene()->UpdateRootObjectSiblingIndices();

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

			auto thisIter = Find(siblings, this);
			assert(thisIter != siblings.end());

			for (auto iter = siblings.begin(); iter != thisIter; ++iter)
			{
				result.push_back(*iter);
			}
		}
		else
		{
			const std::vector<GameObject*>& rootObjects = g_SceneManager->CurrentScene()->GetRootObjects();

			auto thisIter = Find(rootObjects, this);
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

			auto thisIter = Find(siblings, this);
			assert(thisIter != siblings.end());

			for (auto iter = thisIter + 1; iter != siblings.end(); ++iter)
			{
				result.push_back(*iter);
			}
		}
		else
		{
			const std::vector<GameObject*>& rootObjects = g_SceneManager->CurrentScene()->GetRootObjects();

			auto thisIter = Find(rootObjects, this);
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

	bool GameObject::IsVisibleInSceneExplorer(bool bIncludingChildren) const
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
		delete m_RigidBody;

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
	}

	void GameObject::OnOverlapBegin(GameObject* other)
	{
		overlappingObjects.push_back(other);

		if (m_Type != GameObjectType::PLAYER)
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

		if (m_Type != GameObjectType::PLAYER)
		{
			if (other->HasTag("Player0") ||
				other->HasTag("Player1"))
			{
				m_bInteractable = false;
			}
		}
	}

	Valve::Valve(const std::string& name) :
		GameObject(name, GameObjectType::VALVE)
	{
	}

	GameObject* Valve::CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren)
	{
		Valve* newGameObject = new Valve(GetIncrementedPostFixedStr(m_Name, s_DefaultNewGameObjectName));

		newGameObject->minRotation = minRotation;
		newGameObject->maxRotation = maxRotation;
		newGameObject->rotationSpeedScale = rotationSpeedScale;
		newGameObject->invSlowDownRate = invSlowDownRate;
		newGameObject->rotationSpeed = rotationSpeed;
		newGameObject->rotation = rotation;

		CopyGenericFields(newGameObject, parent, bCopyChildren);

		return newGameObject;
	}

	void Valve::ParseUniqueFields(const JSONObject& parentObj, BaseScene* scene, const std::vector<MaterialID>& matIDs)
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

			if (!m_Mesh)
			{
				Mesh* valveMesh = new Mesh(this);
				valveMesh->LoadFromFile(MESH_DIRECTORY "valve.glb", matIDs[0]);
				assert(m_Mesh == nullptr);
				SetMesh(valveMesh);
			}

			if (!m_CollisionShape)
			{
				btVector3 btHalfExtents(1.5f, 1.0f, 1.5f);
				btCylinderShape* cylinderShape = new btCylinderShape(btHalfExtents);

				SetCollisionShape(cylinderShape);
			}

			if (!m_RigidBody)
			{
				RigidBody* rigidBody = SetRigidBody(new RigidBody());
				rigidBody->SetMass(1.0f);
				rigidBody->SetKinematic(false);
				rigidBody->SetStatic(false);
			}
		}
		else
		{
			PrintWarn("Valve's \"valve info\" field missing in scene %s\n", scene->GetName().c_str());
		}
	}

	void Valve::SerializeUniqueFields(JSONObject& parentObject) const
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
		if (m_ObjectInteractingWith)
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

	RisingBlock::RisingBlock(const std::string& name) :
		GameObject(name, GameObjectType::RISING_BLOCK)
	{
	}

	GameObject* RisingBlock::CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren)
	{
		RisingBlock* newGameObject = new RisingBlock(GetIncrementedPostFixedStr(m_Name, s_DefaultNewGameObjectName));

		newGameObject->valve = valve;
		newGameObject->moveAxis = moveAxis;
		newGameObject->bAffectedByGravity = bAffectedByGravity;
		newGameObject->startingPos = startingPos;

		CopyGenericFields(newGameObject, parent, bCopyChildren);

		return newGameObject;
	}

	void RisingBlock::ParseUniqueFields(const JSONObject& parentObj, BaseScene* scene, const std::vector<MaterialID>& matIDs)
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

	void RisingBlock::SerializeUniqueFields(JSONObject& parentObject) const
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

	GlassPane::GlassPane(const std::string& name) :
		GameObject(name, GameObjectType::GLASS_PANE)
	{
	}

	GameObject* GlassPane::CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren)
	{
		GlassPane* newGameObject = new GlassPane(GetIncrementedPostFixedStr(m_Name, s_DefaultNewGameObjectName));

		newGameObject->bBroken = bBroken;

		CopyGenericFields(newGameObject, parent, bCopyChildren);

		return newGameObject;
	}

	void GlassPane::ParseUniqueFields(const JSONObject& parentObj, BaseScene* scene, const std::vector<MaterialID>& matIDs)
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

	void GlassPane::SerializeUniqueFields(JSONObject& parentObject) const
	{
		JSONObject windowInfo = {};

		windowInfo.fields.emplace_back("broken", JSONValue(bBroken));

		parentObject.fields.emplace_back("window info", JSONValue(windowInfo));
	}

	ReflectionProbe::ReflectionProbe(const std::string& name) :
		GameObject(name, GameObjectType::REFLECTION_PROBE)
	{
	}

	GameObject* ReflectionProbe::CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren)
	{
		ReflectionProbe* newGameObject = new ReflectionProbe(GetIncrementedPostFixedStr(m_Name, s_DefaultNewGameObjectName));

		newGameObject->captureMatID = captureMatID;

		CopyGenericFields(newGameObject, parent, bCopyChildren);

		return newGameObject;
	}

	void ReflectionProbe::ParseUniqueFields(const JSONObject& parentObj, BaseScene* scene, const std::vector<MaterialID>& matIDs)
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
		//probeCaptureMatCreateInfo.enableIrradianceSampler = true;
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
		//GameObject* captureObject = new GameObject(captureName, GameObjectType::_NONE);
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

	void ReflectionProbe::SerializeUniqueFields(JSONObject& parentObject) const
	{
		FLEX_UNUSED(parentObject);

		// Reflection probes have no unique fields currently
	}

	void ReflectionProbe::PostInitialize()
	{
		GameObject::PostInitialize();

		g_Renderer->SetReflectionProbeMaterial(captureMatID);
	}

	Skybox::Skybox(const std::string& name) :
		GameObject(name, GameObjectType::SKYBOX)
	{
		SetCastsShadow(false);
	}

	GameObject* Skybox::CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren)
	{
		Skybox* newGameObject = new Skybox(GetIncrementedPostFixedStr(m_Name, s_DefaultNewGameObjectName));

		CopyGenericFields(newGameObject, parent, bCopyChildren);

		return newGameObject;
	}

	void Skybox::ProcedurallyInitialize(MaterialID matID)
	{
		InternalInit(matID);
	}

	void Skybox::ParseUniqueFields(const JSONObject& parentObj, BaseScene* scene, const std::vector<MaterialID>& matIDs)
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

	void Skybox::SerializeUniqueFields(JSONObject& parentObject) const
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
		assert(m_Mesh == nullptr);
		assert(matID != InvalidMaterialID);
		Mesh* skyboxMesh = new Mesh(this);
		skyboxMesh->LoadPrefabShape(PrefabShape::SKYBOX, matID);
		SetMesh(skyboxMesh);

		g_Renderer->SetSkyboxMesh(m_Mesh);
	}

	DirectionalLight::DirectionalLight() :
		DirectionalLight("Directional Light")
	{
	}

	DirectionalLight::DirectionalLight(const std::string& name) :
		GameObject(name, GameObjectType::DIRECTIONAL_LIGHT)
	{
		data.enabled = m_bVisible ? 1 : 0;
		data.dir = VEC3_RIGHT;
		data.color = VEC3_ONE;
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

	void DirectionalLight::Destroy()
	{
		g_Renderer->RemoveDirectionalLight();

		GameObject::Destroy();
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
		ImGui::ColorEdit4("Color ", &data.color.r, colorEditFlags);
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
		data.dir = glm::rotate(m_Transform.GetWorldRotation(), VEC3_RIGHT);
	}

	void DirectionalLight::SetPos(const glm::vec3& newPos)
	{
		m_Transform.SetLocalPosition(newPos);
	}

	glm::vec3 DirectionalLight::GetPos() const
	{
		return m_Transform.GetWorldPosition();
	}

	glm::quat DirectionalLight::GetRot() const
	{
		return m_Transform.GetWorldRotation();
	}

	void DirectionalLight::ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
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

			directionalLightObj.SetVec3Checked("color", data.color);

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

	void DirectionalLight::SerializeUniqueFields(JSONObject& parentObject) const
	{
		JSONObject dirLightObj = {};

		std::string dirStr = QuatToString(m_Transform.GetWorldRotation(), 3);
		dirLightObj.fields.emplace_back("rotation", JSONValue(dirStr));

		std::string posStr = VecToString(m_Transform.GetLocalPosition(), 3);
		dirLightObj.fields.emplace_back("pos", JSONValue(posStr));

		std::string colorStr = VecToString(data.color, 2);
		dirLightObj.fields.emplace_back("color", JSONValue(colorStr));

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
			other.data.color == data.color &&
			other.m_bVisible == m_bVisible &&
			other.data.brightness == data.brightness;
	}

	void DirectionalLight::SetRot(const glm::quat& newRot)
	{
		m_Transform.SetWorldRotation(newRot);
		data.dir = glm::rotate(newRot, VEC3_RIGHT);
	}

	PointLight::PointLight(BaseScene* scene) :
		PointLight(scene->GetUniqueObjectName("PointLight_", 2))
	{
	}

	PointLight::PointLight(const std::string& name) :
		GameObject(name, GameObjectType::POINT_LIGHT)
	{
		data.enabled = 1;
		data.pos = VEC4_ZERO;
		data.color = VEC4_ONE;
		data.brightness = 500.0f;
	}

	void PointLight::Initialize()
	{
		ID = g_Renderer->RegisterPointLight(&data);

		m_Transform.updateParentOnStateChange = true;

		GameObject::Initialize();
	}

	void PointLight::Destroy()
	{
		g_Renderer->RemovePointLight(ID);

		GameObject::Destroy();
	}

	void PointLight::DrawImGuiObjects()
	{
		GameObject::DrawImGuiObjects();

		if (ID != InvalidPointLightID)
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
				bool bEnabled = (data.enabled == 1);
				if (ImGui::Checkbox("Enabled", &bEnabled))
				{
					bEditedPointLightData = true;
					data.enabled = bEnabled ? 1 : 0;
					m_bVisible = bEnabled;
				}

				ImGui::SameLine();
				bEditedPointLightData |= ImGui::ColorEdit4("Color ", &data.color.r, colorEditFlags);
				bEditedPointLightData |= ImGui::SliderFloat("Brightness", &data.brightness, 0.0f, 1000.0f);

				if (bEditedPointLightData)
				{
					g_Renderer->UpdatePointLightData(ID, &data);
				}
			}
		}
	}

	void PointLight::SetVisible(bool bVisible, bool bEffectChildren /* = true */)
	{
		data.enabled = (bVisible ? 1 : 0);
		GameObject::SetVisible(bVisible, bEffectChildren);
		if (ID != InvalidPointLightID)
		{
			g_Renderer->UpdatePointLightData(ID, &data);
		}
	}

	void PointLight::OnTransformChanged()
	{
		data.pos = m_Transform.GetLocalPosition();
		if (ID != InvalidPointLightID)
		{
			g_Renderer->UpdatePointLightData(ID, &data);
		}
	}

	void PointLight::SetPos(const glm::vec3& pos)
	{
		m_Transform.SetLocalPosition(pos);
		data.pos = pos;
		if (ID != InvalidPointLightID)
		{
			g_Renderer->UpdatePointLightData(ID, &data);
		}
	}

	glm::vec3 PointLight::GetPos() const
	{
		return m_Transform.GetWorldPosition();
	}

	void PointLight::ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
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

			pointLightObj.SetVec3Checked("color", data.color);

			pointLightObj.SetFloatChecked("brightness", data.brightness);

			if (pointLightObj.HasField("enabled"))
			{
				m_bVisible = pointLightObj.GetBool("enabled") ? 1 : 0;
				data.enabled = m_bVisible ? 1 : 0;
			}
		}
	}

	void PointLight::SerializeUniqueFields(JSONObject& parentObject) const
	{
		JSONObject pointLightObj = {};

		std::string posStr = VecToString(m_Transform.GetLocalPosition(), 3);
		pointLightObj.fields.emplace_back("pos", JSONValue(posStr));

		std::string colorStr = VecToString(data.color, 2);
		pointLightObj.fields.emplace_back("color", JSONValue(colorStr));

		pointLightObj.fields.emplace_back("enabled", JSONValue(m_bVisible != 0));
		pointLightObj.fields.emplace_back("brightness", JSONValue(data.brightness));

		parentObject.fields.emplace_back("point light info", JSONValue(pointLightObj));
	}

	bool PointLight::operator==(const PointLight& other)
	{
		return other.data.pos == data.pos &&
			other.data.color == data.color &&
			other.data.enabled == data.enabled &&
			other.data.brightness == data.brightness;
	}

	Cart::Cart(CartID cartID, GameObjectType type /* = GameObjectType::CART */) :
		Cart(cartID, g_SceneManager->CurrentScene()->GetUniqueObjectName("Cart_", 4), type)
	{
	}

	Cart::Cart(CartID cartID,
		const std::string& name,
		GameObjectType type /* = GameObjectType::CART */,
		const char* meshName /* = "emptyCartMeshName" */) :
		GameObject(name, type),
		cartID(cartID)
	{
		MaterialID matID;
		if (!g_Renderer->FindOrCreateMaterialByName("pbr grey", matID))
		{
			// :shrug:
			// TODO: Create own material
			matID = 0;
		}
		Mesh* mesh = SetMesh(new Mesh(this));
		std::string meshFilePath = std::string(MESH_DIRECTORY) + std::string(meshName);
		if (!mesh->LoadFromFile(meshFilePath, matID))
		{
			PrintWarn("Failed to load cart mesh!\n");
		}

		m_TSpringToCartAhead.DR = 1.0f;
	}

	GameObject* Cart::CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren)
	{
		// TODO: FIXME: Get newly generated cart ID! & move allocation into cart manager
		Cart* newGameObject = new Cart(cartID);

		newGameObject->currentTrackID = currentTrackID;
		newGameObject->distAlongTrack = distAlongTrack;

		CopyGenericFields(newGameObject, parent, bCopyChildren);

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

			TrackManager* trackManager = g_SceneManager->CurrentScene()->GetTrackManager();
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
			TrackManager* trackManager = g_SceneManager->CurrentScene()->GetTrackManager();

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
				BaseScene* baseScene = g_SceneManager->CurrentScene();
				TrackManager* trackManager = baseScene->GetTrackManager();

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

				CartManager* cartManager = baseScene->GetCartManager();

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

	void Cart::ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(scene);
		FLEX_UNUSED(matIDs);

		JSONObject cartInfo = parentObject.GetObject("cart info");
		currentTrackID = (TrackID)cartInfo.GetInt("track ID");
		distAlongTrack = cartInfo.GetFloat("dist along track");
	}

	void Cart::SerializeUniqueFields(JSONObject& parentObject) const
	{
		JSONObject cartInfo = {};

		cartInfo.fields.emplace_back("track ID", JSONValue((i32)currentTrackID));
		cartInfo.fields.emplace_back("dist along track", JSONValue(distAlongTrack));

		parentObject.fields.emplace_back("cart info", JSONValue(cartInfo));
	}

	EngineCart::EngineCart(CartID cartID) :
		EngineCart(cartID, g_SceneManager->CurrentScene()->GetUniqueObjectName("EngineCart_", 4))
	{
	}

	EngineCart::EngineCart(CartID cartID, const std::string& name) :
		Cart(cartID, name, GameObjectType::ENGINE_CART, engineMeshName)
	{
	}

	GameObject* EngineCart::CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren)
	{
		// TODO: FIXME: Get newly generated cart ID! & move allocation into cart manager
		EngineCart* newGameObject = new EngineCart(cartID);

		newGameObject->powerRemaining = powerRemaining;

		newGameObject->currentTrackID = currentTrackID;
		newGameObject->distAlongTrack = distAlongTrack;

		CopyGenericFields(newGameObject, parent, bCopyChildren);

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

	void EngineCart::ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(scene);
		FLEX_UNUSED(matIDs);

		JSONObject cartInfo = parentObject.GetObject("cart info");
		currentTrackID = (TrackID)cartInfo.GetInt("track ID");
		distAlongTrack = cartInfo.GetFloat("dist along track");

		moveDirection = cartInfo.GetFloat("move direction");
		powerRemaining = cartInfo.GetFloat("power remaining");
	}

	void EngineCart::SerializeUniqueFields(JSONObject& parentObject) const
	{
		JSONObject cartInfo = {};

		cartInfo.fields.emplace_back("track ID", JSONValue((i32)currentTrackID));
		cartInfo.fields.emplace_back("dist along track", JSONValue(distAlongTrack));

		cartInfo.fields.emplace_back("move direction", JSONValue(moveDirection));
		cartInfo.fields.emplace_back("power remaining", JSONValue(powerRemaining));

		parentObject.fields.emplace_back("cart info", JSONValue(cartInfo));
	}

	MobileLiquidBox::MobileLiquidBox() :
		MobileLiquidBox(g_SceneManager->CurrentScene()->GetUniqueObjectName("MobileLiquidBox_", 4))
	{
	}

	MobileLiquidBox::MobileLiquidBox(const std::string& name) :
		GameObject(name, GameObjectType::MOBILE_LIQUID_BOX)
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

	GameObject* MobileLiquidBox::CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren)
	{
		GameObject* newGameObject = new MobileLiquidBox();

		CopyGenericFields(newGameObject, parent, bCopyChildren);

		return newGameObject;
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

	void MobileLiquidBox::ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(parentObject);
		FLEX_UNUSED(scene);
		FLEX_UNUSED(matIDs);
	}

	void MobileLiquidBox::SerializeUniqueFields(JSONObject& parentObject) const
	{
		FLEX_UNUSED(parentObject);
	}

	GerstnerWave::GerstnerWave(const std::string& name) :
		GameObject(name, GameObjectType::GERSTNER_WAVE)
	{
		workQueue = new ThreadSafeArray<WaveGenData>(32);

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
		matCreateInfo.albedoTexturePath = TEXTURE_LOCATION "wave-n-2.png";
		matCreateInfo.enableAlbedoSampler = true;

		m_WaveMaterialID = g_Renderer->InitializeMaterial(&matCreateInfo);

		bobberTarget = Spring<real>(0.0f);
		bobberTarget.DR = 2.5f;
		bobberTarget.UAF = 40.0f;
		bobber = new GameObject("Bobber", GameObjectType::_NONE);
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
		m_VertexBufferCreateInfo.attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(m_WaveMaterialID).shaderID).vertexAttributes;

		avgWaveUpdateTime = RollingAverage<ms>(256, SamplingType::CONSTANT);

		criticalSection = Platform::InitCriticalSection();

		for (u32 i = 0; i < workQueue->Size(); ++i)
		{
			AllocWorkQueueEntry(i);
		}

		WRITE_BARRIER;

		u32 threadCount = (u32)glm::clamp(((i32)Platform::GetLogicalProcessorCount()) - 1, 1, 16);

		threadUserData = {};
		threadUserData.running = true;
		threadUserData.criticalSection = criticalSection;
		Platform::SpawnThreads(threadCount, &ThreadUpdate, &threadUserData);

		SortWaveSamplingLODs();
		SortWaveTessellationLODs();

		DiscoverChunks();
		UpdateWaveVertexData();

		SetMesh(new Mesh(this));

		m_Mesh->LoadFromMemoryDynamic(m_VertexBufferCreateInfo, m_Indices, m_WaveMaterialID, (u32)m_VertexBufferCreateInfo.positions_3D.size());
	}

	void GerstnerWave::Update()
	{
		if (!m_bVisible)
		{
			return;
		}

		PROFILE_AUTO("Gerstner update");

		for (WaveInfo& wave : waves)
		{
			if (wave.enabled)
			{
				wave.accumOffset += (wave.moveSpeed * g_DeltaTime);
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

		MeshComponent* meshComponent = m_Mesh->GetSubMeshes()[0];
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
	}

	void GerstnerWave::Destroy()
	{
		GameObject::Destroy();

		threadUserData.running = false;

		Platform::JoinThreads();

		for (u32 i = 0; i < workQueue->Size(); ++i)
		{
			FreeWorkQueueEntry(i);
		}

		delete workQueue;

		Platform::FreeCriticalSection(criticalSection);
		criticalSection = nullptr;
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
			glm::vec2 chunkUV = glm::saturate(glm::vec2((queryPos.x - chunkMin.x) / size, (queryPos.z - chunkMin.y) / size));
			u32 chunkLocalVertexIndex = (u32)(chunkUV.x * (chunkVertCountPerAxis - 1)) + ((u32)(chunkUV.y * ((real)chunkVertCountPerAxis - 1.0f)) * chunkVertCountPerAxis);
			const u32 idxMax = vertsPerChunk - 1;
			glm::vec3 A(m_VertexBufferCreateInfo.positions_3D[chunk->vertOffset + glm::min(chunkLocalVertexIndex + 0u, idxMax)]);
			glm::vec3 B(m_VertexBufferCreateInfo.positions_3D[chunk->vertOffset + glm::min(chunkLocalVertexIndex + 1u, idxMax)]);
			glm::vec3 C(m_VertexBufferCreateInfo.positions_3D[chunk->vertOffset + glm::min(chunkLocalVertexIndex + chunkVertCountPerAxis, idxMax)]);
			glm::vec3 D(m_VertexBufferCreateInfo.positions_3D[chunk->vertOffset + glm::min(chunkLocalVertexIndex + chunkVertCountPerAxis + 1u, idxMax)]);
			glm::vec2 vertexUV = glm::saturate(glm::vec2((queryPos.x - A.x) / (B.x - A.x), (queryPos.z - B.z) / (D.z - B.z)));
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

		assert((*workQueue)[workQueueIndex].positionsx_4 == nullptr);

		(*workQueue)[workQueueIndex].positionsx_4 = (__m128*)_mm_malloc(vertCountPerChunkDiv4 * sizeof(__m128), 16);
		(*workQueue)[workQueueIndex].positionsy_4 = (__m128*)_mm_malloc(vertCountPerChunkDiv4 * sizeof(__m128), 16);
		(*workQueue)[workQueueIndex].positionsz_4 = (__m128*)_mm_malloc(vertCountPerChunkDiv4 * sizeof(__m128), 16);
		(*workQueue)[workQueueIndex].lodSelected_4 = (__m128*)_mm_malloc(vertCountPerChunkDiv4 * sizeof(__m128), 16);
		(*workQueue)[workQueueIndex].uvUs_4 = (__m128*)_mm_malloc(vertCountPerChunkDiv4 * sizeof(__m128), 16);
		(*workQueue)[workQueueIndex].uvVs_4 = (__m128*)_mm_malloc(vertCountPerChunkDiv4 * sizeof(__m128), 16);

		(*workQueue)[workQueueIndex].lodCutoffsAmplitudes_4 = (__m128*)_mm_malloc(vertCountPerChunkDiv4 * sizeof(__m128), 16);
		(*workQueue)[workQueueIndex].lodNextCutoffAmplitudes_4 = (__m128*)_mm_malloc(vertCountPerChunkDiv4 * sizeof(__m128), 16);
		(*workQueue)[workQueueIndex].lodBlendWeights_4 = (__m128*)_mm_malloc(vertCountPerChunkDiv4 * sizeof(__m128), 16);
	}

	void GerstnerWave::FreeWorkQueueEntry(u32 workQueueIndex)
	{
		_mm_free((*workQueue)[workQueueIndex].positionsx_4);
		_mm_free((*workQueue)[workQueueIndex].positionsy_4);
		_mm_free((*workQueue)[workQueueIndex].positionsz_4);
		_mm_free((*workQueue)[workQueueIndex].lodSelected_4);
		_mm_free((*workQueue)[workQueueIndex].uvUs_4);
		_mm_free((*workQueue)[workQueueIndex].uvVs_4);

		_mm_free((*workQueue)[workQueueIndex].lodCutoffsAmplitudes_4);
		_mm_free((*workQueue)[workQueueIndex].lodNextCutoffAmplitudes_4);
		_mm_free((*workQueue)[workQueueIndex].lodBlendWeights_4);
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
			m_VertexBufferCreateInfo.colors_R32G32B32A32.resize(vertCount);
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
				m_VertexBufferCreateInfo.colors_R32G32B32A32.resize(vertCount);
				m_VertexBufferCreateInfo.colors_R32G32B32A32.shrink_to_fit();
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

			for (const WaveInfo& wave : waves)
			{
				if (wave.a < amplitudeLODCutoff)
				{
					break;
				}

				if (wave.enabled)
				{
					const glm::vec2 waveVec = glm::vec2(wave.waveDirCos, wave.waveDirSin) * wave.waveVecMag;
					const glm::vec2 waveVecN = glm::normalize(waveVec);

					for (u32 z = 0; z < tessellationLOD->vertCountPerAxis; ++z)
					{
						for (u32 x = 0; x < tessellationLOD->vertCountPerAxis; ++x)
						{
							const u32 vertIdx = z * tessellationLOD->vertCountPerAxis + x + waveChunk.vertOffset;

							real d = waveVec.x * positions[vertIdx].x + waveVec.y * positions[vertIdx].z; // Inline dot
							real c = cos(d + wave.accumOffset);
							real s = sin(d + wave.accumOffset);
							positions[vertIdx] += glm::vec3(
								-waveVecN.x * wave.a * s,
								wave.a * c,
								-waveVecN.y * wave.a * s);
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
		if (waveChunks.size() > workQueue->Size())
		{
			for (u32 i = 0; i < workQueue->Size(); ++i)
			{
				// TODO: Reuse memory
				FreeWorkQueueEntry(i);
			}
			delete workQueue;

			u32 newSize = (u32)(waveChunks.size() * 1.2f);
			workQueue = new ThreadSafeArray<WaveGenData>(newSize);
			for (u32 i = 0; i < workQueue->Size(); ++i)
			{
				AllocWorkQueueEntry(i);
			}
		}

		workQueueEntriesCompleted = 0;
		workQueueEntriesCreated = 0;
		WRITE_BARRIER;
		workQueueEntriesClaimed = 0;
		WRITE_BARRIER;

		glm::vec3* positions = m_VertexBufferCreateInfo.positions_3D.data();
		glm::vec2* texCoords = m_VertexBufferCreateInfo.texCoords_UV.data();
		glm::vec4* colours = m_VertexBufferCreateInfo.colors_R32G32B32A32.data();

		for (u32 chunkIdx = 0; chunkIdx < (u32)waveChunks.size(); ++chunkIdx)
		{
			volatile WaveGenData* waveGenData = &(*workQueue)[chunkIdx];

			waveGenData->waves = &waves;
			waveGenData->waveChunks = &waveChunks;
			waveGenData->waveSamplingLODs = &waveSamplingLODs;
			waveGenData->waveTessellationLODs = &waveTessellationLODs;
			waveGenData->soloWave = soloWave;
			waveGenData->size = size;
			waveGenData->chunkIdx = chunkIdx;
			waveGenData->bDisableLODs = bDisableLODs;
			waveGenData->blendDist = blendDist;

			waveGenData->positions = nullptr;

			WRITE_BARRIER;

			Platform::AtomicIncrement(&workQueueEntriesCreated);
		}

		// Wait for all threads to complete
		// TODO: Call later in frame
		while (workQueueEntriesCompleted != workQueueEntriesCreated)
		{
			Platform::YieldProcessor();
		}

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
					_mm_store_ps(&xs.x, (*workQueue)[chunkIdx].positionsx_4[chunkLocalVertIdxDiv4]);
					_mm_store_ps(&ys.x, (*workQueue)[chunkIdx].positionsy_4[chunkLocalVertIdxDiv4]);
					_mm_store_ps(&zs.x, (*workQueue)[chunkIdx].positionsz_4[chunkLocalVertIdxDiv4]);
					_mm_store_ps(&lodSelections.x, (*workQueue)[chunkIdx].lodSelected_4[chunkLocalVertIdxDiv4]);
					_mm_store_ps(&uvUs.x, (*workQueue)[chunkIdx].uvUs_4[chunkLocalVertIdxDiv4]);
					_mm_store_ps(&uvVs.x, (*workQueue)[chunkIdx].uvVs_4[chunkLocalVertIdxDiv4]);

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

	u32 ThreadUpdate(void* inData)
	{
		ThreadData* threadData = (ThreadData*)inData;

		while (threadData->running)
		{
			volatile GerstnerWave::WaveGenData* work = nullptr;

			if (workQueueEntriesClaimed < workQueueEntriesCreated)
			{
				Platform::EnterCriticalSection(threadData->criticalSection);
				{
					if (workQueueEntriesClaimed < workQueueEntriesCreated)
					{
						work = &(*workQueue)[workQueueEntriesClaimed];

						workQueueEntriesClaimed += 1;

						WRITE_BARRIER;

						assert(workQueueEntriesClaimed <= workQueueEntriesCreated);
						assert(workQueueEntriesClaimed <= workQueue->Size());
					}

				}
				Platform::LeaveCriticalSection(threadData->criticalSection);
			}

			if (work)
			{
				// Inputs
				const std::vector<GerstnerWave::WaveInfo>& waves = *work->waves;
				const std::vector<GerstnerWave::WaveChunk>& waveChunks = *work->waveChunks;
				const std::vector<GerstnerWave::WaveSamplingLOD>& waveSamplingLODs = *work->waveSamplingLODs;
				const std::vector<GerstnerWave::WaveTessellationLOD>& waveTessellationLODs = *work->waveTessellationLODs;
				GerstnerWave::WaveInfo const * soloWave = work->soloWave;

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
				__m128 blendDistSqr_4 = _mm_set_ps1(blendDist * blendDist);

				const i32 vertCountPerChunk = chunkVertCountPerAxis * chunkVertCountPerAxis;

				__m128 vertCountMin1_4 = _mm_set_ps1((real)(chunkVertCountPerAxis - 1));
				__m128 size_4 = _mm_set_ps1(size);

				const glm::vec2 chunkCenter((waveChunks[chunkIdx].index.x - 0.5f) * size, (waveChunks[chunkIdx].index.y - 0.5f) * size);
				__m128 chunkCenterX_4 = _mm_set_ps1(chunkCenter.x);
				__m128 chunkCenterY_4 = _mm_setzero_ps();
				__m128 chunkCenterZ_4 = _mm_set_ps1(chunkCenter.y);

				glm::vec2 chunkRight = chunkCenter + glm::vec2(size, 0.0f);
				glm::vec2 chunkLeft = chunkCenter - glm::vec2(size, 0.0f);
				glm::vec2 chunkForward = chunkCenter + glm::vec2(0.0f, size);
				glm::vec2 chunkBack = chunkCenter - glm::vec2(0.0f, size);

				// TODO: Work out max LOD in chunk
				glm::vec3 camPos = g_CameraManager->CurrentCamera()->position;
				__m128 camPosx_4 = _mm_set_ps1(camPos.x);
				__m128 camPosz_4 = _mm_set_ps1(camPos.z);

				__m128 zero_4 = _mm_set_ps1(0.0f);
				__m128 one_4 = _mm_set_ps1(1.0f);
				__m128 two_4 = _mm_set_ps1(2.0f);

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

								__m128 pushedDist = _mm_add_ps(vertSqrDist, blendDistSqr_4);
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
				for (const GerstnerWave::WaveInfo& wave : waves)
				{
					// TODO: Early out once wave amplitude isn't used by any vert in chunk

					if (wave.enabled && (soloWave == nullptr || soloWave == &wave))
					{
						const glm::vec2 waveVec = glm::vec2(wave.waveDirCos, wave.waveDirSin) * wave.waveVecMag;
						const glm::vec2 waveVecN = glm::normalize(waveVec);

						__m128 accumOffset_4 = _mm_set_ps1(wave.accumOffset);
						__m128 negWaveVecNX_4 = _mm_set_ps1(-waveVecN.x);
						__m128 negWaveVecNZ_4 = _mm_set_ps1(-waveVecN.y);
						__m128 waveA_4 = _mm_set_ps1(wave.a);

						__m128 waveVecX_4 = _mm_set_ps1(waveVec.x);
						__m128 waveVecZ_4 = _mm_set_ps1(waveVec.y);

						u32 countMinOne = chunkVertCountPerAxis - 1;

						for (i32 z = 0; z < chunkVertCountPerAxis; ++z)
						{
							__m128 zIdxMin_4 = _mm_set_ps1((real)z);
							__m128 zIdxMax_4 = _mm_set_ps1((real)(countMinOne - z));

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
									real c = cos(d + wave.accumOffset);
									real s = sin(d + wave.accumOffset);
									positions[vertIdx] += glm::vec3(
										-waveVecN.x * wave.a * s,
										wave.a * c,
										-waveVecN.y * wave.a * s);
								*/


								__m128 d = _mm_add_ps(_mm_mul_ps(positionsx_4[chunkLocalVertIdxDiv4], waveVecX_4), _mm_mul_ps(positionsz_4[chunkLocalVertIdxDiv4], waveVecZ_4));

								__m128 totalAccum = _mm_add_ps(d, accumOffset_4);


								__m128 c = _mm_cos_ps(totalAccum);
								__m128 s = _mm_sin_ps(totalAccum);

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

				Platform::AtomicIncrement(&workQueueEntriesCompleted);
			}
			else
			{
				Sleep(2);
			}
		}

		return 0;
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

		const i32 vertCountPerChunk = chunkVertCountPerAxis * chunkVertCountPerAxis;

		const glm::vec2 chunkCenter((waveChunk->index.x - 0.5f) * size, (waveChunk->index.y - 0.5f) * size);

		WaveChunk const* chunkLeft = GetChunkAtPos(chunkCenter - glm::vec2(size, 0.0f));
		WaveChunk const* chunkRight = GetChunkAtPos(chunkCenter + glm::vec2(size, 0.0f));
		WaveChunk const* chunkBack = GetChunkAtPos(chunkCenter - glm::vec2(0.0f, size));
		WaveChunk const* chunkFor = GetChunkAtPos(chunkCenter + glm::vec2(0.0f, size));
		WaveTessellationLOD const* LODLeft = chunkLeft ? GetTessellationLOD(chunkLeft->tessellationLODLevel) : nullptr;
		WaveTessellationLOD const* LODRight = chunkRight ? GetTessellationLOD(chunkRight->tessellationLODLevel) : nullptr;
		WaveTessellationLOD const* LODBack = chunkBack ? GetTessellationLOD(chunkBack->tessellationLODLevel) : nullptr;
		WaveTessellationLOD const* LODFor = chunkFor ? GetTessellationLOD(chunkFor->tessellationLODLevel) : nullptr;

		const real cellSize = size / chunkVertCountPerAxis;
		for (u32 z = 0; z < chunkVertCountPerAxis; ++z)
		{
			for (u32 x = 0; x < chunkVertCountPerAxis; ++x)
			{
				const u32 localIdx = z * chunkVertCountPerAxis + x;
				const u32 vertIdx = localIdx + waveChunk->vertOffset;

				glm::vec3 planePos = glm::vec3(
					chunkCenter.x + size * ((real)x / (chunkVertCountPerAxis - 1)),
					0.0f,
					chunkCenter.y + size * ((real)z / (chunkVertCountPerAxis - 1)));

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
				normals[vertIdx] = glm::vec3(dX, 2.0f * cellSize, dZ);
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

	GameObject* GerstnerWave::CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren)
	{
		GerstnerWave* newGameObject = new GerstnerWave(GetIncrementedPostFixedStr(m_Name, "Gerstner Wave"));
		CopyGenericFields(newGameObject, parent, bCopyChildren);
		return newGameObject;
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
			for (i32 i = 0; i < (i32)waves.size(); ++i)
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

			MeshComponent* meshComponent = m_Mesh->GetSubMeshes()[0];

			{
				char byteCountStr[64];
				ByteCountToString(byteCountStr, 64, (u32)(m_VertexBufferCreateInfo.positions_3D.size() * sizeof(glm::vec3)));

				char nameBuf[256];
				sprintf_s(nameBuf, "CPU Vertex buffer size %s", byteCountStr);

				real usage = (real)DEBUG_lastUsedVertCount / meshComponent->GetVertexBufferData()->VertexCount;
				ImGui::ProgressBar(usage, ImVec2(0, 0), "");
				ImGui::Text(nameBuf);
			}

			{
				u32 gpuSize = meshComponent->GetVertexBufferData()->UsedVertexBufferSize;
				u32 usedGPUSize = g_Renderer->GetDynamicVertexBufferUsedSize(meshComponent->renderID);

				char byteCountStr[64];
				ByteCountToString(byteCountStr, 64, gpuSize);

				char nameBuf[256];
				sprintf_s(nameBuf, "GPU Vertex buffer size %s", byteCountStr);

				real usage = (real)usedGPUSize / gpuSize;
				ImGui::ProgressBar(usage, ImVec2(0, 0), "");
				ImGui::Text(nameBuf);
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
			for (i32 i = 0; i < (i32)waves.size(); ++i)
			{
				const bool bWasDisabled = soloWave != nullptr && soloWave != &waves[i];
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

				ImGui::Checkbox(enabledStr.c_str(), &waves[i].enabled);

				ImGui::SameLine();

				bool bSolo = soloWave == &waves[i];
				std::string soloStr = "Solo" + childName;
				if (ImGui::Checkbox(soloStr.c_str(), &bSolo))
				{
					if (bSolo)
					{
						soloWave = &waves[i];
					}
					else
					{
						soloWave = nullptr;
					}
				}

				std::string aStr = "amplitude" + childName;
				bNeedUpdate |= ImGui::DragFloat(aStr.c_str(), &waves[i].a, 0.01f);
				bNeedSort |= bNeedUpdate;
				std::string waveLenStr = "wave len" + childName;
				bNeedUpdate |= ImGui::DragFloat(waveLenStr.c_str(), &waves[i].waveLen, 0.01f);
				bNeedSort |= bNeedUpdate;
				std::string dirStr = "dir" + childName;
				if (ImGui::DragFloat(dirStr.c_str(), &waves[i].waveDirTheta, 0.001f, 0.0f, 10.0f))
				{
					bNeedUpdate = true;
					waves[i].waveDirTheta = fmod(waves[i].waveDirTheta, TWO_PI);
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

	void GerstnerWave::ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
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
					WaveInfo wave = {};
					wave.enabled = waveObj.GetBool("enabled");
					wave.a = waveObj.GetFloat("amplitude");
					wave.waveDirTheta = waveObj.GetFloat("waveDir");
					wave.waveLen = waveObj.GetFloat("waveLen");
					waves.push_back(wave);
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
		for (i32 i = 0; i < (i32)waves.size(); ++i)
		{
			UpdateDependentVariables(i);
		}
	}

	void GerstnerWave::SerializeUniqueFields(JSONObject& parentObject) const
	{
		JSONObject gerstnerWaveObj = {};

		std::vector<JSONObject> waveObjs;
		waveObjs.resize(waves.size());
		for (i32 i = 0; i < (i32)waves.size(); ++i)
		{
			JSONObject& waveObj = waveObjs[i];
			waveObj.fields.emplace_back("enabled", JSONValue(waves[i].enabled));
			waveObj.fields.emplace_back("amplitude", JSONValue(waves[i].a));
			waveObj.fields.emplace_back("waveDir", JSONValue(waves[i].waveDirTheta));
			waveObj.fields.emplace_back("waveLen", JSONValue(waves[i].waveLen));
		}

		gerstnerWaveObj.fields.emplace_back("waves", JSONValue(waveObjs));

		// TODO: Add uint support
		gerstnerWaveObj.fields.emplace_back("max chunk vert count per axis", JSONValue((i32)maxChunkVertCountPerAxis));
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
		if (waveIndex >= 0 && waveIndex < (i32)waves.size())
		{
			waves[waveIndex].waveVecMag = TWO_PI / waves[waveIndex].waveLen;
			waves[waveIndex].moveSpeed = glm::sqrt(updateSpeed * waves[waveIndex].waveVecMag);

			waves[waveIndex].waveDirCos = cos(waves[waveIndex].waveDirTheta);
			waves[waveIndex].waveDirSin = sin(waves[waveIndex].waveDirTheta);
		}
	}

	void GerstnerWave::AddWave()
	{
		waves.push_back({});
		UpdateDependentVariables((u32)waves.size() - 1);
		SortWaves();
	}

	void GerstnerWave::RemoveWave(i32 index)
	{
		if (index >= 0 && index < (i32)waves.size())
		{
			waves.erase(waves.begin() + index);
			SortWaves();
		}
	}

	void GerstnerWave::SortWaves()
	{
		WaveInfo soloWaveCopy;
		if (soloWave)
		{
			soloWaveCopy = *soloWave;
		}
		std::sort(waves.begin(), waves.end(),
			[](const WaveInfo& waveA, const WaveInfo& waveB)
		{
			return abs(waveA.a) > abs(waveB.a);
		});
		if (soloWave)
		{
			for (const WaveInfo& waveInfo : waves)
			{
				if (waveInfo == soloWaveCopy)
				{
					soloWave = &waveInfo;
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

	Blocks::Blocks(const std::string& name) :
		GameObject(name, GameObjectType::BLOCKS)
	{
		MaterialCreateInfo matCreateInfo = {};
		matCreateInfo.name = "block";
		matCreateInfo.shaderName = "pbr";
		matCreateInfo.constMetallic = 0.0f;

		std::vector<MaterialID> matIDs;
		for (i32 i = 0; i < 10; ++i)
		{
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
				GameObject* obj = new GameObject("block", GameObjectType::OBJECT);
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
	}

	Terminal::Terminal() :
		Terminal(g_SceneManager->CurrentScene()->GetUniqueObjectName("Terminal_", 2))
	{
	}

	Terminal::Terminal(const std::string& name) :
		GameObject(name, GameObjectType::TERMINAL),
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

		m_Transform.UpdateParentTransform();

		tokenizer = new Tokenizer();
		ast = new AST(tokenizer);
	}

	void Terminal::Initialize()
	{
		g_InputManager->BindKeyEventCallback(&m_KeyEventCallback, 20);
		ParseCode();

		GameObject::Initialize();
	}

	void Terminal::Destroy()
	{
		g_InputManager->UnbindKeyEventCallback(&m_KeyEventCallback);

		if (ast != nullptr)
		{
			ast->Destroy();
			delete ast;
			ast = nullptr;
		}

		delete tokenizer;
		tokenizer = nullptr;

		GameObject::Destroy();
	}

	void Terminal::Update()
	{
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
				forward * 0.992f;

			glm::vec3 pos = posTL;

			const glm::quat rot = m_Transform.GetWorldRotation();
			real charHeight = g_Renderer->GetStringHeight("W", font, false) * m_LetterScale;
			const real lineHeight = charHeight * (m_LineHeight / 1000.0f);
			real charWidth = g_Renderer->GetStringWidth("W", font, letterSpacing, false) / 1000.0f;
			const real lineNoWidth = 24.0f * charWidth * m_LetterScale;

			if (bRenderCursor)
			{
				glm::vec3 cursorPos = pos;
				cursorPos += (right * charWidth) + up * (cursor.y * -lineHeight);
				std::string spaces(cursor.x, ' ');
				g_Renderer->DrawStringWS(spaces + "|", VEC4_ONE, cursorPos, rot, letterSpacing, m_LetterScale);
			}

			if (bRenderText)
			{
				static const glm::vec4 lineNumberColor(0.4f, 0.4f, 0.4f, 1.0f);
				static const glm::vec4 lineNumberColorActive(0.5f, 0.5f, 0.5f, 1.0f);
				static const glm::vec4 textColor(0.85f, 0.81f, 0.80f, 1.0f);
				static const glm::vec4 errorColor(0.65f, 0.12f, 0.13f, 1.0f);
				glm::vec3 firstLinePos = pos;
				for (i32 lineNumber = 0; lineNumber < (i32)lines.size(); ++lineNumber)
				{
					glm::vec4 lineNoCol = (lineNumber == cursor.y ? lineNumberColorActive : lineNumberColor);
					g_Renderer->DrawStringWS(IntToString(lineNumber + 1, 2, ' '), lineNoCol, pos + right * lineNoWidth, rot, letterSpacing, m_LetterScale);
					g_Renderer->DrawStringWS(lines[lineNumber], textColor, pos, rot, letterSpacing, m_LetterScale);
					pos.y -= lineHeight;
				}

				if (ast != nullptr)
				{
					glm::vec2i lastErrorPos = ast->lastErrorTokenLocation;
					if (lastErrorPos.x != -1)
					{
						pos = firstLinePos;
						pos.y -= lineHeight * lastErrorPos.y;
						g_Renderer->DrawStringWS("!", errorColor, pos + right * (charWidth * 1.7f), rot, letterSpacing, m_LetterScale);
						std::string underlineStr = std::string(lastErrorPos.x, ' ') + std::string(ast->lastErrorTokenLen, '_');
						pos.y -= lineHeight * 0.2f;
						g_Renderer->DrawStringWS(underlineStr, errorColor, pos, rot, letterSpacing, m_LetterScale);
					}
				}
			}
		}
	}

	void Terminal::DrawTerminalUI()
	{
		ImGui::Begin("Terminal");
		{
			ImGui::TextWrapped("Hit F5 to compile and evaluate code.");

			ImGui::Separator();

			//ImGui::DragFloat("Line height", &m_LineHeight, 0.01f);
			//ImGui::DragFloat("Scale", &m_LetterScale, 0.01f);

			ImGui::Text("Variables");
			if (ImGui::BeginChild("Variables", ImVec2(0.0f, 220.0f), true))
			{
				for (i32 i = 0; i < tokenizer->context->variableCount; ++i)
				{
					const TokenContext::InstantiatedIdentifier& var = tokenizer->context->instantiatedIdentifiers[i];
					std::string valStr = var.value->ToString();
					const char* typeNameCStr = g_TypeNameStrings[(i32)ValueTypeToTypeName(var.value->type)];
					ImGui::Text("%s = %s", var.name.c_str(), valStr.c_str());
					ImGui::SameLine();
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
					ImGui::Text("(%s)", typeNameCStr);
					ImGui::PopStyleColor();
				}
			}
			ImGui::EndChild();

			if (tokenizer->context->errors.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1.0f, 0.5f, 1.0f));
				ImGui::Text("Success");
				ImGui::PopStyleColor();
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
				for (const Error& e : tokenizer->context->errors)
				{
					ImGui::Text("L%d: %s", e.lineNumber + 1, e.str.c_str());
				}
				ImGui::PopStyleColor();
			}
		}
		ImGui::End();
	}

	GameObject* Terminal::CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren)
	{
		GameObject* newGameObject = new Terminal();

		CopyGenericFields(newGameObject, parent, bCopyChildren);

		return newGameObject;
	}

	bool Terminal::AllowInteractionWith(GameObject* gameObject)
	{
		if (gameObject == nullptr)
		{
			return true;
		}

		Player* player = dynamic_cast<Player*>(gameObject);
		if (player != nullptr)
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

		return false;
	}

	void Terminal::SetCamera(TerminalCamera* camera)
	{
		m_Camera = camera;
	}

	void Terminal::ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(scene);
		FLEX_UNUSED(matIDs);

		JSONObject obj = parentObject.GetObject("terminal");
		std::string str = obj.GetString("str");

		lines = Split(str, '\n');

		MoveCursorToEnd();
	}

	void Terminal::SerializeUniqueFields(JSONObject& parentObject) const
	{
		JSONObject terminalObj = {};

		std::string str = "";
		for (i32 i = 0; i < (i32)lines.size(); ++i)
		{
			str += lines[i];
			if (i < (i32)lines.size() - 1)
			{
				str.push_back('\n');
			}
		}

		terminalObj.fields.emplace_back("str", JSONValue(str));

		parentObject.fields.emplace_back("terminal", JSONValue(terminalObj));
	}

	void Terminal::TypeChar(char c)
	{
		m_CursorBlinkTimer = 0.0f;
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
			cursorMaxX = cursor.x;
		}
		else
		{
			// TODO: Enforce screen width
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
		ParseCode();
	}

	void Terminal::Clear()
	{
		m_CursorBlinkTimer = 0.0f;
		lines.clear();
		cursor.x = cursor.y = cursorMaxX = 0;
	}

	void Terminal::MoveCursorToStart()
	{
		m_CursorBlinkTimer = 0.0f;
		cursor.y = 0;
		cursor.x = 0;
		cursorMaxX = cursor.x;
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
		cursor.y = (i32)lines.size() - 1;
		cursor.x = (i32)lines[cursor.y].size();
		cursorMaxX = cursor.x;
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
			ClampCursorX();
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
			ClampCursorX();
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
		assert(tokenizer != nullptr);
		assert(ast != nullptr);

		std::string str;
		for (const std::string& line : lines)
		{
			str.append(line);
			str.push_back('\n');
		}

		ast->Destroy();

		tokenizer->SetCodeStr(str);
		ast->Generate();
	}

	void Terminal::EvaluateCode()
	{
		ast->Evaluate();
	}

	EventReply Terminal::OnKeyEvent(KeyCode keyCode, KeyAction action, i32 modifiers)
	{
		if (m_Camera != nullptr)
		{
			if (action == KeyAction::PRESS || action == KeyAction::REPEAT)
			{
				const bool bCapsLock = (modifiers & (i32)InputModifier::CAPS_LOCK) > 0;
				const bool bShiftDown = (modifiers & (i32)InputModifier::SHIFT) > 0;
				const bool bCtrlDown = (modifiers & (i32)InputModifier::CONTROL) > 0;
				if (keyCode == KeyCode::KEY_F7)
				{
					ParseCode();
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_F5)
				{
					ParseCode();
					EvaluateCode();
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_ESCAPE)
				{
					m_Camera->TransitionOut();
					return EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_SLASH)
				{
					if (bCtrlDown)
					{
						i32 pCursorPosX = cursor.x;
						cursor.x = 0;
						if (lines[cursor.y].size() < 2)
						{
							// TODO: Check line length
							TypeChar('/');
							TypeChar('/');
							cursor.x = pCursorPosX + 2;
						}
						else
						{
							if (lines[cursor.y][0] == '/' && lines[cursor.y][1] == '/')
							{
								DeleteCharInFront(false);
								DeleteCharInFront(false);
								cursor.x = pCursorPosX - 2;
							}
							else
							{
								// TODO: Check line length
								TypeChar('/');
								TypeChar('/');
								cursor.x = pCursorPosX + 2;
							}
						}
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

	ParticleSystem::ParticleSystem(const std::string& name) :
		GameObject(name, GameObjectType::PARTICLE_SYSTEM)
	{
		m_Transform.updateParentOnStateChange = true;
	}

	void ParticleSystem::Update()
	{
	}

	void ParticleSystem::Destroy()
	{
		g_Renderer->RemoveParticleSystem(ID);
		GameObject::Destroy();
	}

	GameObject* ParticleSystem::CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren)
	{
		ParticleSystem* newParticleSystem = new ParticleSystem(GetIncrementedPostFixedStr(m_Name, "Particle System"));

		CopyGenericFields(newParticleSystem, parent, bCopyChildren);

		newParticleSystem->data.color0 = data.color0;
		newParticleSystem->data.color1 = data.color1;
		newParticleSystem->data.speed = data.speed;
		newParticleSystem->data.particleCount = data.particleCount;
		newParticleSystem->bEnabled = true;
		newParticleSystem->scale = scale;
		newParticleSystem->model = model;
		g_Renderer->AddParticleSystem(m_Name, newParticleSystem, data.particleCount);

		return newParticleSystem;
	}

	void ParticleSystem::ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
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
		systemDataObj.SetVec4Checked("color0", data.color0);
		systemDataObj.SetVec4Checked("color1", data.color1);
		systemDataObj.SetFloatChecked("speed", data.speed);
		i32 particleCount;
		if (systemDataObj.SetIntChecked("particle count", particleCount))
		{
			data.particleCount = particleCount;
		}

		ID = g_Renderer->AddParticleSystem(m_Name, this, particleCount);
	}

	void ParticleSystem::SerializeUniqueFields(JSONObject& parentObject) const
	{
		JSONObject particleSystemObj = {};

		particleSystemObj.fields.emplace_back(Transform::Serialize(model, m_Name.c_str()));
		particleSystemObj.fields.emplace_back("scale", JSONValue(scale));
		particleSystemObj.fields.emplace_back("enabled", JSONValue(bEnabled));


		JSONObject systemDataObj = {};
		systemDataObj.fields.emplace_back("color0", JSONValue(VecToString(data.color0, 2)));
		systemDataObj.fields.emplace_back("color1", JSONValue(VecToString(data.color1, 2)));
		systemDataObj.fields.emplace_back("speed", JSONValue(data.speed));
		systemDataObj.fields.emplace_back("particle count", JSONValue((i32)data.particleCount));
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

		ImGui::ColorEdit4("Color 0", &data.color0.r, colorEditFlags);
		ImGui::SameLine();
		ImGui::ColorEdit4("Color 1", &data.color1.r, colorEditFlags);
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
	}

	ChunkGenerator::ChunkGenerator(const std::string& name) :
		GameObject(name, GameObjectType::CHUNK_GENERATOR),
		m_LowCol(0.2f, 0.3f, 0.45f),
		m_MidCol(0.45f, 0.55f, 0.25f),
		m_HighCol(0.65f, 0.67f, 0.69f)
	{
	}

	GameObject* ChunkGenerator::CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren)
	{
		FLEX_UNUSED(parent);
		FLEX_UNUSED(bCopyChildren);
		return nullptr;
	}

	void ChunkGenerator::Initialize()
	{
		MaterialCreateInfo matCreateInfo = {};
		matCreateInfo.name = "Terrain";
		matCreateInfo.shaderName = "terrain";
		matCreateInfo.constAlbedo = glm::vec3(1.0f, 0.0f, 0.0f);
		matCreateInfo.constRoughness = 1.0f;
		matCreateInfo.constMetallic = 0.0f;
		matCreateInfo.enableIrradianceSampler = false;
		m_TerrainMatID = g_Renderer->InitializeMaterial(&matCreateInfo);

		m_Mesh = new Mesh(this);
		m_Mesh->SetTypeToMemory();

		GenerateGradients();
	}

	void ChunkGenerator::DestroyAllChunks()
	{
		for (auto iter = m_Meshes.begin(); iter != m_Meshes.end(); ++iter)
		{
			iter->second->Destroy();
		}
		m_Meshes.clear();
	}

	void ChunkGenerator::GenerateChunk(const glm::ivec2& chunkIndex)
	{
		PROFILE_AUTO("Generate terrain chunk");

		{
			auto existingChunkIter = m_Meshes.find(chunkIndex);
			if (existingChunkIter != m_Meshes.end())
			{
				existingChunkIter->second->Destroy();
				m_Meshes.erase(existingChunkIter);
			}
		}

		ShaderID shaderID = g_Renderer->GetMaterial(m_TerrainMatID).shaderID;

		const u32 vertexCount = VertCountPerChunkAxis * VertCountPerChunkAxis;
		const u32 triCount = ((VertCountPerChunkAxis - 1) * (VertCountPerChunkAxis - 1)) * 2;
		const u32 indexCount = triCount * 3;

		VertexBufferDataCreateInfo vertexBufferCreateInfo = {};
		vertexBufferCreateInfo.attributes = g_Renderer->GetShader(shaderID).vertexAttributes;
		vertexBufferCreateInfo.positions_3D.reserve(vertexCount);
		vertexBufferCreateInfo.texCoords_UV.reserve(vertexCount);
		vertexBufferCreateInfo.colors_R32G32B32A32.reserve(vertexCount);
		vertexBufferCreateInfo.normals.reserve(vertexCount);

		std::vector<u32> indices(indexCount);

		for (u32 z = 0; z < VertCountPerChunkAxis; ++z)
		{
			for (u32 x = 0; x < VertCountPerChunkAxis; ++x)
			{
				glm::vec2 uv(x / (real)(VertCountPerChunkAxis - 1), z / (real)(VertCountPerChunkAxis - 1));

				glm::vec3 pos = glm::vec3(uv.x * ChunkSize, 0.0f, uv.y * ChunkSize);

				glm::vec3 vertPosWS = pos + glm::vec3(chunkIndex.x * ChunkSize, 0.0f, chunkIndex.y * ChunkSize);
				glm::vec2 sampleCenter(vertPosWS.x, vertPosWS.z);
				real height = SampleTerrain(sampleCenter);

				vertPosWS.y = height * MaxHeight;

				const real e = 0.01f;
				real heightDX = (SampleTerrain(sampleCenter - glm::vec2(e, 0.0f)) - SampleTerrain(sampleCenter + glm::vec2(e, 0.0f))) * MaxHeight;
				real heightDZ = (SampleTerrain(sampleCenter - glm::vec2(0.0f, e)) - SampleTerrain(sampleCenter + glm::vec2(0.0f, e))) * MaxHeight;

				glm::vec3 normal = glm::normalize(glm::vec3(heightDX * nscale, 2.0f * e, heightDZ * nscale));

				vertexBufferCreateInfo.positions_3D.emplace_back(vertPosWS);
				vertexBufferCreateInfo.texCoords_UV.emplace_back(uv);
				bool bShowEdge = (m_bHighlightGrid && (x == 0 || x == (VertCountPerChunkAxis - 1) || z == 0 || z == (VertCountPerChunkAxis - 1)));
				glm::vec3 vertCol = (bShowEdge ? glm::vec3(0.75f) : (height <= 0.5f ? Lerp(m_LowCol, m_MidCol, glm::pow(height * 2.0f, 4.0f)) : Lerp(m_MidCol, m_HighCol, glm::pow((height - 0.5f) * 2.0f, 1.0f / 5.0f))));
				vertexBufferCreateInfo.colors_R32G32B32A32.emplace_back(glm::vec4(vertCol.x, vertCol.y, vertCol.z, 1.0f));
				//vertexBufferCreateInfo.colors_R32G32B32A32.emplace_back(glm::vec4(height, height, height, 1.0f));
				vertexBufferCreateInfo.normals.emplace_back(normal);
			}
		}

		for (u32 z = 0; z < VertCountPerChunkAxis - 1; ++z)
		{
			for (u32 x = 0; x < VertCountPerChunkAxis - 1; ++x)
			{
				u32 vertIdx = z * (VertCountPerChunkAxis - 1) + x;
				u32 i = z * VertCountPerChunkAxis + x;
				indices[vertIdx * 6 + 0] = i;
				indices[vertIdx * 6 + 1] = i + 1 + VertCountPerChunkAxis;
				indices[vertIdx * 6 + 2] = i + 1;

				indices[vertIdx * 6 + 3] = i;
				indices[vertIdx * 6 + 4] = i + VertCountPerChunkAxis;
				indices[vertIdx * 6 + 5] = i + 1 + VertCountPerChunkAxis;
			}
		}

		RenderObjectCreateInfo renderObjectCreateInfo = {};
		MeshComponent* meshComponent = MeshComponent::LoadFromMemory(m_Mesh, vertexBufferCreateInfo, indices, m_TerrainMatID, &renderObjectCreateInfo);
		if (meshComponent)
		{
			assert(m_Meshes[chunkIndex] == nullptr);
			m_Meshes[chunkIndex] = meshComponent;
		}
	}

	void ChunkGenerator::GenerateGradients()
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
			g_Renderer->DestroyTexture(m_TableTextureIDs[i]);
		}
		m_TableTextureIDs.resize(m_RandomTables.size());
		for (u32 i = 0; i < m_TableTextureIDs.size(); ++i)
		{
			const u32 tableWidth = (u32)glm::sqrt(m_RandomTables[i].size());
			if (tableWidth < 1) break;
			std::vector<glm::vec4> textureMem(m_RandomTables[i].size());
			for (u32 j = 0; j < m_RandomTables[i].size(); ++j)
			{
				textureMem[j] = glm::vec4(m_RandomTables[i][j].x * 0.5f + 0.5f, m_RandomTables[i][j].y * 0.5f + 0.5f, 0.0f, 1.0f);
			}
			m_TableTextureIDs[i] = g_Renderer->InitializeTextureFromMemory(&textureMem[0], (u32)(textureMem.size() * sizeof(u32) * 4), VK_FORMAT_R32G32B32A32_SFLOAT, "Perlin random table", tableWidth, tableWidth, 2, VK_FILTER_NEAREST);
		}
	}

	// TODO: Create SoA style SampleTerrain which fills out a buffer iteratively, sampling each octave in turn

	// Returns a value in [0, 1]
	real ChunkGenerator::SampleTerrain(const glm::vec2& pos)
	{
		real result = 0.0f;
		real octave = m_BaseOctave;
		u32 octaveIdx = m_NumOctaves - 1;
		for (u32 i = 0; i < m_NumOctaves; ++i)
		{
			if (m_IsolateOctave == -1 || i == (u32)m_IsolateOctave)
			{
				result += SampleNoise(pos, octave, octaveIdx) * (octave / m_OctaveScale);
			}
			octave = octave / 2.0f;
			--octaveIdx;
		}
		return glm::clamp((result / (real)(m_NumOctaves * 2.0f)) + 0.5f, 0.0f, 1.0f);
	}

	real SmoothBlend(real t)
	{
		return 6.0f * glm::pow(t, 5.0f) - 15.0f * glm::pow(t, 4.0f) + 10.0f * glm::pow(t, 3.0f);
	}

	// Returns a value in [-1, 1]
	real ChunkGenerator::SampleNoise(const glm::vec2& pos, real octave, u32 octaveIdx)
	{
		const glm::vec2 scaledPos = pos / octave;
		glm::vec2 posi = glm::vec2(std::floor(scaledPos.x), std::floor(scaledPos.y));
		glm::vec2 posf = scaledPos - posi;

		const std::vector<glm::vec2>& randomTables = m_RandomTables[octaveIdx];
		const u32 tableEntryCount = (u32)randomTables.size();
		const u32 tableWidth = (u32)std::sqrt(tableEntryCount);

		glm::vec2 r00 = randomTables[(u32)((i32)(posi.y * tableWidth + posi.x) % tableEntryCount)];
		glm::vec2 r10 = randomTables[(u32)((i32)(posi.y * tableWidth + posi.x + 1) % tableEntryCount)];
		glm::vec2 r01 = randomTables[(u32)((i32)((posi.y + 1) * tableWidth + posi.x) % tableEntryCount)];
		glm::vec2 r11 = randomTables[(u32)((i32)((posi.y + 1) * tableWidth + posi.x + 1) % tableEntryCount)];

		real r00p = glm::dot(posf, r00);
		real r10p = glm::dot(glm::vec2(posf.x - 1.0f, posf.y), r10);
		real r01p = glm::dot(glm::vec2(posf.x, posf.y - 1.0f), r01);
		real r11p = glm::dot(glm::vec2(posf.x - 1.0f, posf.y - 1.0f), r11);

		real xBlend = SmoothBlend(posf.x);
		real yBlend = SmoothBlend(posf.y);
		real xval0 = Lerp(r00p, r10p, xBlend);
		real xval1 = Lerp(r01p, r11p, xBlend);
		real val = Lerp(xval0, xval1, yBlend);

		return val;
	}

	void ChunkGenerator::PostInitialize()
	{
	}

	void ChunkGenerator::Update()
	{
		std::vector<glm::vec2i> chunksInRadius(m_Meshes.size()); // Likely to be same size as m_LoadedChunks

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
					chunksInRadius.push_back(glm::vec2i(x, z));
				}
			}
		}

		// Freshly unloaded chunks
		for (auto chunkIter = m_Meshes.begin(); chunkIter != m_Meshes.end(); ++chunkIter)
		{
			const glm::vec2i& chunkIdx = chunkIter->first;
			if (Find(chunksInRadius, chunkIdx) == chunksInRadius.end() && m_ChunksToDestroy.find(chunkIdx) == m_ChunksToDestroy.end())
			{
				m_ChunksToDestroy.emplace(chunkIdx);

				if (m_ChunksToLoad.find(chunkIdx) != m_ChunksToLoad.end())
				{
					m_ChunksToLoad.erase(chunkIdx);
				}
			}
		}

		// Freshly loaded chunks
		for (const glm::vec2i& chunkIdx : chunksInRadius)
		{
			auto meshIter = m_Meshes.find(chunkIdx);
			if (meshIter == m_Meshes.end() && m_ChunksToLoad.find(chunkIdx) == m_ChunksToLoad.end())
			{
				m_ChunksToLoad.emplace(chunkIdx);

				if (m_ChunksToDestroy.find(chunkIdx) != m_ChunksToDestroy.end())
				{
					m_ChunksToDestroy.erase(chunkIdx);
				}
			}
		}

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
				MeshComponent* mesh = iter->second;
				m_Meshes.erase(iter);
				mesh->Destroy();
				delete mesh;

				++iterationCount;

				ns now = Time::CurrentNanoseconds();
				if ((now - start) > m_CreationBudgetPerFrame)
				{
					break;
				}
			}
		}

		{
			PROFILE_AUTO("Generate terrain chunks");
			ns start = Time::CurrentNanoseconds();
			i32 iterationCount = 0;
			while (m_ChunksToLoad.size() > 0)
			{
				GenerateChunk(*m_ChunksToLoad.begin());
				m_ChunksToLoad.erase(m_ChunksToLoad.begin());

				++iterationCount;

				ns now = Time::CurrentNanoseconds();
				if ((now - start) > m_CreationBudgetPerFrame)
				{
					break;
				}
			}
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
				drawInfo.bWriteDepth = false;
				drawInfo.bReadDepth = false;
				drawInfo.scale = glm::vec3(textureScale);
				drawInfo.pos = glm::vec3(0.0f, textureY, 0.0f);
				drawInfo.textureID = m_TableTextureIDs[i];
				g_Renderer->EnqueueSprite(drawInfo);

				textureY -= (textureScale * 2.0f + 0.01f);
				textureScale /= 2.0f;
			}
		}
	}

	void ChunkGenerator::Destroy()
	{
		for (auto iter = m_Meshes.begin(); iter != m_Meshes.end(); ++iter)
		{
			iter->second->Destroy();
		}
		m_Meshes.clear();

		GameObject::Destroy();
	}

	void ChunkGenerator::DrawImGuiObjects()
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

		bRegen = ImGui::SliderFloat("Octave scale", &m_OctaveScale, 1.0f, 250.0f) || bRegen;
		const u32 maxOctaveCount = (u32)glm::ceil(glm::log(m_BasePerlinTableWidth)) + 1;
		bRegen = ImGuiExt::SliderUInt("Octave count", &m_NumOctaves, 1, maxOctaveCount) || bRegen;

		bRegen = ImGui::SliderInt("Isolate octave", &m_IsolateOctave, -1, m_NumOctaves - 1) || bRegen;

		u32 oldtableWidth = m_BasePerlinTableWidth;
		if (ImGuiExt::InputUInt("Base table width", &m_BasePerlinTableWidth, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue))
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

		bRegen = ImGui::InputFloat("Chunk size", &ChunkSize, 0.1f, 1.0f, "%.0f", ImGuiInputTextFlags_EnterReturnsTrue) || bRegen;

		bRegen = ImGui::SliderFloat("Max height", &MaxHeight, 0.1f, 512.0f) || bRegen;

		bRegen = ImGui::SliderFloat("nscale", &nscale, 0.01f, 2.0f) || bRegen;

		bRegen = ImGui::SliderFloat("octave", &m_BaseOctave, 1.0f, 2048.0f) || bRegen;

		bRegen = ImGui::ColorEdit3("low", &m_LowCol.x) || bRegen;
		bRegen = ImGui::ColorEdit3("mid", &m_MidCol.x) || bRegen;
		bRegen = ImGui::ColorEdit3("high", &m_HighCol.x) || bRegen;

		if (bRegen)
		{
			GenerateGradients();
			DestroyAllChunks();
		}

		ImGui::SliderFloat("View radius", &m_LoadedChunkRadius, 0.01f, 8192.0f);
	}

	void ChunkGenerator::ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs)
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

	void ChunkGenerator::SerializeUniqueFields(JSONObject& parentObject) const
	{
		JSONObject chunkGenInfo = {};

		chunkGenInfo.fields.emplace_back("vert count per chunk axis", JSONValue((i32)VertCountPerChunkAxis));
		chunkGenInfo.fields.emplace_back("chunk size", JSONValue(ChunkSize));
		chunkGenInfo.fields.emplace_back("max height", JSONValue(MaxHeight));
		chunkGenInfo.fields.emplace_back("use manual seed", JSONValue(m_UseManualSeed));
		chunkGenInfo.fields.emplace_back("manual seed", JSONValue((i32)m_ManualSeed));

		chunkGenInfo.fields.emplace_back("loaded chunk radius", JSONValue(m_LoadedChunkRadius));

		chunkGenInfo.fields.emplace_back("base table width", JSONValue((i32)m_BasePerlinTableWidth));

		chunkGenInfo.fields.emplace_back("low colour", JSONValue(VecToString(m_LowCol)));
		chunkGenInfo.fields.emplace_back("mid colour", JSONValue(VecToString(m_MidCol)));
		chunkGenInfo.fields.emplace_back("high colour", JSONValue(VecToString(m_HighCol)));

		chunkGenInfo.fields.emplace_back("base octave", JSONValue(m_BaseOctave));
		chunkGenInfo.fields.emplace_back("octave scale", JSONValue(m_OctaveScale));
		chunkGenInfo.fields.emplace_back("num octaves", JSONValue((i32)m_NumOctaves));

		chunkGenInfo.fields.emplace_back("pin center", JSONValue(m_bPinCenter));
		chunkGenInfo.fields.emplace_back("pinned center", JSONValue(VecToString(m_PinnedPos)));

		parentObject.fields.emplace_back("chunk generator info", JSONValue(chunkGenInfo));
	}
} // namespace flex
