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
IGNORE_WARNINGS_POP

#include "Scene/GameObject.hpp"
#include "Audio/AudioManager.hpp"
#include "Cameras/TerminalCamera.hpp"
#include "FlexEngine.hpp"
#include "Graphics/BitmapFont.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "JSONParser.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "Profiler.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

namespace flex
{
	const char* GameObject::s_DefaultNewGameObjectName = "New_Game_Object_00";

	const char* Cart::emptyCartMeshName = "cart-empty.glb";
	const char* EngineCart::engineMeshName = "cart-engine.glb";

	struct Token g_EmptyToken = Token();

	RandomizedAudioSource GameObject::s_SqueakySounds;
	AudioSourceID GameObject::s_BunkSound;

	GameObject::GameObject(const std::string& name, GameObjectType type) :
		m_Name(name),
		m_Type(type)
	{
		m_Transform.SetAsIdentity();
		m_Transform.SetGameObject(this);

		if (!s_SqueakySounds.IsInitialized())
		{
			s_SqueakySounds.Initialize(RESOURCE_LOCATION  "audio/squeak00.wav", 5);

			s_BunkSound = AudioManager::AddAudioSource(RESOURCE_LOCATION  "audio/bunk.wav");
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

	GameObject* GameObject::CreateObjectFromJSON(const JSONObject& obj, BaseScene* scene, MaterialID overriddenMatID /* = InvalidMaterialID */)
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

				MaterialID matID = scene->FindMaterialIDByName(obj);

				GameObject* prefabInstance = GameObject::CreateObjectFromJSON(*prefab, scene, matID);
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

				prefabInstance->ParseUniqueFields(obj, scene, matID);

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
			newGameObject->ParseJSON(obj, scene, overriddenMatID);
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

		if (m_RenderID != InvalidRenderID)
		{
			g_Renderer->PostInitializeRenderObject(m_RenderID);
		}

		if (rb)
		{
			rb->GetRigidBodyInternal()->setUserPointer(this);
		}

		for (GameObject* child : m_Children)
		{
			child->PostInitialize();
		}
	}

	void GameObject::Destroy()
	{
		for (GameObject* child : m_Children)
		{
			child->Destroy();
			delete child;
		}
		m_Children.clear();

		if (m_MeshComponent)
		{
			m_MeshComponent->Destroy();
			delete m_MeshComponent;
			m_MeshComponent = nullptr;
		}

		if (m_RenderID != InvalidRenderID)
		{
			g_Renderer->DestroyRenderObject(m_RenderID);
			m_RenderID = InvalidRenderID;
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

		ImGui::Text("%s", m_Name.c_str());

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
		ImGui::Text("");
		{
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

			static glm::vec3 sRot = glm::degrees((glm::eulerAngles(m_Transform.GetLocalRotation())));

			if (!ImGui::IsMouseDown(0))
			{
				sRot = glm::degrees((glm::eulerAngles(m_Transform.GetLocalRotation())));
			}

			glm::vec3 translation = m_Transform.GetLocalPosition();
			glm::vec3 rotation = sRot;
			glm::vec3 pScale = m_Transform.GetLocalScale();
			glm::vec3 scale = pScale;

			bool valueChanged = false;

			valueChanged = ImGui::DragFloat3("Position", &translation[0], 0.1f) || valueChanged;
			if (ImGui::IsItemClicked(1))
			{
				translation = VEC3_ZERO;
				valueChanged = true;
			}

			glm::vec3 cleanedRot;
			valueChanged = DoImGuiRotationDragFloat3("Rotation", rotation, cleanedRot) || valueChanged;

			valueChanged = ImGui::DragFloat3("Scale", &scale[0], 0.01f) || valueChanged;
			if (ImGui::IsItemClicked(1))
			{
				scale = VEC3_ONE;
				valueChanged = true;
			}

			ImGui::SameLine();

			bool bUseUniformScale = m_bUniformScale;
			if (ImGui::Checkbox("u", &bUseUniformScale))
			{
				SetUseUniformScale(bUseUniformScale, true);
				valueChanged = true;
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

			if (valueChanged)
			{
				m_Transform.SetLocalPosition(translation, false);

				sRot = rotation;

				glm::quat rotQuat(glm::quat(glm::radians(cleanedRot)));

				m_Transform.SetLocalRotation(rotQuat, false);
				m_Transform.SetLocalScale(scale, true);
				SetUseUniformScale(m_bUniformScale, false);

				if (m_RigidBody)
				{
					m_RigidBody->MatchParentTransform();
				}

				if (g_EngineInstance->IsObjectSelected(this))
				{
					g_EngineInstance->CalculateSelectedObjectsCenter();
				}
			}
		}

		if (m_RenderID != InvalidRenderID)
		{
			g_Renderer->DrawImGuiForGameObjectWithValidRenderID(this);
		}
		else
		{
			if (ImGui::Button("Add mesh component"))
			{
				MaterialID matID = InvalidMaterialID;
				// TODO: Don't rely on material names!
				g_Renderer->GetMaterialID("pbr chrome", matID);

				MeshComponent* mesh = SetMeshComponent(new MeshComponent(matID, this));
				mesh->LoadFromFile(RESOURCE_LOCATION  "meshes/cube.glb");
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
					PrintWarn("Unhandled shape type in GameObject::DrawImGuiObjects");
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
		bool bDeletedSelf = false;

		// TODO: Prevent name collisions
		std::string contextMenuIDStr = "context window game object " + m_Name;
		static std::string newObjectName = m_Name;
		const size_t maxStrLen = 256;

		bool bRefreshNameField = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
			ImGui::IsMouseClicked(1);

		if (bActive && g_EngineInstance->GetWantRenameActiveElement())
		{
			ImGui::OpenPopup(contextMenuIDStr.c_str());
			g_EngineInstance->ClearWantRenameActiveElement();
			bRefreshNameField = true;
		}
		if (bRefreshNameField)
		{
			newObjectName = m_Name;
			newObjectName.resize(maxStrLen);
		}

		if (ImGui::BeginPopupContextItem(contextMenuIDStr.c_str()))
		{
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
			}

			ImGui::SameLine();

			if (ImGui::Button("Delete"))
			{
				if (g_SceneManager->CurrentScene()->DestroyGameObject(this, true))
				{
					bDeletedSelf = true;
				}
				else
				{
					PrintWarn("Failed to delete game object: %s\n", m_Name.c_str());
				}
			}

			ImGui::EndPopup();
		}

		return bDeletedSelf;
	}

	bool GameObject::DoDuplicateGameObjectButton(const char* buttonName)
	{
		if (ImGui::Button(buttonName))
		{
			GameObject* newGameObject = CopySelfAndAddToScene(nullptr, true);

			g_EngineInstance->SetSelectedObject(newGameObject);

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
		UNREFERENCED_PARAMETER(gameObject);

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

	void GameObject::ParseJSON(const JSONObject& obj, BaseScene* scene, MaterialID overriddenMatID /* = InvalidMaterialID */)
	{
		bool bVisible = true;
		obj.SetBoolChecked("visible", bVisible);
		bool bVisibleInSceneGraph = true;
		obj.SetBoolChecked("visible in scene graph", bVisibleInSceneGraph);

		MaterialID matID = InvalidMaterialID;
		if (overriddenMatID != InvalidMaterialID)
		{
			matID = overriddenMatID;
		}
		else
		{
			matID = scene->FindMaterialIDByName(obj);
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
				std::string fileName = parsedMeshObj.GetString("file");
				StripLeadingDirectories(fileName);
				StripFileType(fileName);

				if (fileName.compare(meshName) == 0)
				{
					MeshComponent::ParseJSON(parsedMeshObj, this, matID);
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

		ParseUniqueFields(obj, scene, matID);

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
				AddChild(GameObject::CreateObjectFromJSON(child, scene));
			}
		}
	}

	void GameObject::ParseUniqueFields(const JSONObject& /* parentObj */, BaseScene* /* scene */, MaterialID /* matID*/)
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
			object.fields.emplace_back("visible in scene graph",
				JSONValue(IsVisibleInSceneExplorer()));
		}

		if (IsStatic())
		{
			object.fields.emplace_back("static", JSONValue(true));
		}

		object.fields.push_back(m_Transform.Serialize());

		if (m_MeshComponent &&
			bIsBasicObject &&
			!m_bLoadedFromPrefab)
		{
			std::string meshName = m_MeshComponent->GetRelativeFilePath();
			StripLeadingDirectories(meshName);
			StripFileType(meshName);

			object.fields.emplace_back("mesh", JSONValue(meshName));
		}

		{
			MaterialID matID = InvalidMaterialID;
			RenderObjectCreateInfo renderObjectCreateInfo;
			RenderID renderID = GetRenderID();
			if (m_MeshComponent)
			{
				matID = m_MeshComponent->GetMaterialID();
			}
			else if (renderID != InvalidRenderID && g_Renderer->GetRenderObjectCreateInfo(renderID, renderObjectCreateInfo))
			{
				matID = renderObjectCreateInfo.materialID;
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
					object.fields.emplace_back("material", JSONValue(materialName));
				}
			}
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
				std::string halfExtentsStr = Vec3ToString(halfExtents, 3);
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
				std::string halfExtentsStr = Vec3ToString(halfExtents, 3);
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
				colliderObj.fields.emplace_back("offset pos", JSONValue(Vec3ToString(m_RigidBody->GetLocalPosition(), 3)));
			}

			if (m_RigidBody->GetLocalRotation() != QUAT_UNIT)
			{
				glm::vec3 localRotEuler = glm::eulerAngles(m_RigidBody->GetLocalRotation());
				colliderObj.fields.emplace_back("offset rot", JSONValue(Vec3ToString(localRotEuler, 3)));
			}

			if (m_RigidBody->GetLocalScale() != VEC3_ONE)
			{
				colliderObj.fields.emplace_back("offset scale", JSONValue(Vec3ToString(m_RigidBody->GetLocalScale(), 3)));
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
		RenderObjectCreateInfo createInfo = {};
		g_Renderer->GetRenderObjectCreateInfo(m_RenderID, createInfo);

		// Make it clear we aren't copying vertex or index data directly
		createInfo.vertexBufferData = nullptr;
		createInfo.indices = nullptr;

		MaterialID matID = createInfo.materialID;
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

		if (m_MeshComponent)
		{
			MeshComponent* newMeshComponent = newGameObject->SetMeshComponent(new MeshComponent(matID, newGameObject, false));
			MeshComponent::Type prefabType = m_MeshComponent->GetType();
			if (prefabType == MeshComponent::Type::PREFAB)
			{
				MeshComponent::PrefabShape shape = m_MeshComponent->GetShape();
				newMeshComponent->SetRequiredAttributesFromMaterialID(matID);
				newMeshComponent->LoadPrefabShape(shape, &createInfo);
			}
			else if (prefabType == MeshComponent::Type::FILE)
			{
				std::string filePath = m_MeshComponent->GetRelativeFilePath();
				MeshImportSettings importSettings = m_MeshComponent->GetImportSettings();
				newMeshComponent->SetRequiredAttributesFromMaterialID(matID);
				newMeshComponent->LoadFromFile(filePath, &importSettings, &createInfo);
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

	RenderID GameObject::GetRenderID() const
	{
		return m_RenderID;
	}

	std::string GameObject::GetName() const
	{
		return m_Name;
	}

	void GameObject::SetName(const std::string& newName)
	{
		m_Name = newName;
	}

	void GameObject::SetRenderID(RenderID renderID)
	{
		m_RenderID = renderID;
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

	MeshComponent* GameObject::GetMeshComponent()
	{
		return m_MeshComponent;
	}

	MeshComponent* GameObject::SetMeshComponent(MeshComponent* meshComponent)
	{
		if (m_MeshComponent)
		{
			g_Renderer->DestroyRenderObject(m_RenderID);
			m_RenderID = InvalidRenderID;
			m_MeshComponent->Destroy();
			delete m_MeshComponent;
		}

		m_MeshComponent = meshComponent;
		g_Renderer->RenderObjectStateChanged();
		return meshComponent;
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

	void Valve::ParseUniqueFields(const JSONObject& parentObj, BaseScene* scene, MaterialID matID)
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

			if (!m_MeshComponent)
			{
				MeshComponent* valveMesh = new MeshComponent(matID, this);
				valveMesh->LoadFromFile(RESOURCE_LOCATION  "meshes/valve.glb");
				assert(GetMeshComponent() == nullptr);
				SetMeshComponent(valveMesh);
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
		valveInfo.fields.emplace_back("range", JSONValue(Vec2ToString(valveRange, 2)));

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
			gain = glm::clamp(gain, 0.0f, 1.0f);
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

	void RisingBlock::ParseUniqueFields(const JSONObject& parentObj, BaseScene* scene, MaterialID matID)
	{
		if (!m_MeshComponent)
		{
			MeshComponent* cubeMesh = new MeshComponent(matID, this);
			cubeMesh->LoadFromFile(RESOURCE_LOCATION  "meshes/cube.glb");
			SetMeshComponent(cubeMesh);
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
		blockInfo.fields.emplace_back("move axis", JSONValue(Vec3ToString(moveAxis, 3)));
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
			real distMult = 1.0f - glm::clamp(playerControlledValveRotationSpeed / 2.0f, 0.0f, 1.0f);
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

	void GlassPane::ParseUniqueFields(const JSONObject& parentObj, BaseScene* scene, MaterialID matID)
	{
		UNREFERENCED_PARAMETER(scene);

		JSONObject glassInfo;
		if (parentObj.SetObjectChecked("window info", glassInfo))
		{
			glassInfo.SetBoolChecked("broken", bBroken);

			if (!m_MeshComponent)
			{
				MeshComponent* windowMesh = new MeshComponent(matID, this);
				const char* filePath;
				if (bBroken)
				{
					filePath = RESOURCE("meshes/glass-window-broken.glb");
				}
				else
				{
					filePath = RESOURCE("meshes/glass-window-whole.glb");
				}
				windowMesh->LoadFromFile(filePath);
				SetMeshComponent(windowMesh);
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

	void ReflectionProbe::ParseUniqueFields(const JSONObject& parentObj, BaseScene* scene, MaterialID matID)
	{
		UNREFERENCED_PARAMETER(scene);
		UNREFERENCED_PARAMETER(parentObj);
		UNREFERENCED_PARAMETER(matID);

		// Probe capture material
		MaterialCreateInfo probeCaptureMatCreateInfo = {};
		probeCaptureMatCreateInfo.name = "Reflection probe capture";
		probeCaptureMatCreateInfo.shaderName = "deferred_combine_cubemap";
		probeCaptureMatCreateInfo.generateReflectionProbeMaps = true;
		probeCaptureMatCreateInfo.generateHDRCubemapSampler = true;
		probeCaptureMatCreateInfo.generatedCubemapSize = glm::vec2(512.0f, 512.0f); // TODO: Add support for non-512.0f size
		probeCaptureMatCreateInfo.generateCubemapDepthBuffers = true;
		probeCaptureMatCreateInfo.enableIrradianceSampler = true;
		probeCaptureMatCreateInfo.generateIrradianceSampler = true;
		probeCaptureMatCreateInfo.generatedIrradianceCubemapSize = { 32, 32 };
		probeCaptureMatCreateInfo.enablePrefilteredMap = true;
		probeCaptureMatCreateInfo.generatePrefilteredMap = true;
		probeCaptureMatCreateInfo.generatedPrefilteredCubemapSize = { 128, 128 };
		probeCaptureMatCreateInfo.enableBRDFLUT = true;
		probeCaptureMatCreateInfo.engineMaterial = true;
		probeCaptureMatCreateInfo.frameBuffers = {
			{ "positionMetallicFrameBufferSampler", nullptr },
			{ "normalRoughnessFrameBufferSampler", nullptr },
			{ "albedoAOFrameBufferSampler", nullptr },
		};
		captureMatID = g_Renderer->InitializeMaterial(&probeCaptureMatCreateInfo);

		//MeshComponent* sphereMesh = new MeshComponent(matID, this);

		//assert(m_MeshComponent == nullptr);
		//sphereMesh->LoadFromFile(RESOURCE_LOCATION  "meshes/sphere.glb");
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

		g_Renderer->SetReflectionProbeMaterial(captureMatID);
	}

	void ReflectionProbe::SerializeUniqueFields(JSONObject& parentObject) const
	{
		UNREFERENCED_PARAMETER(parentObject);

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
	}

	GameObject* Skybox::CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren)
	{
		Skybox* newGameObject = new Skybox(GetIncrementedPostFixedStr(m_Name, s_DefaultNewGameObjectName));

		CopyGenericFields(newGameObject, parent, bCopyChildren);

		return newGameObject;
	}

	void Skybox::ParseUniqueFields(const JSONObject& parentObj, BaseScene* scene, MaterialID matID)
	{
		UNREFERENCED_PARAMETER(scene);

		assert(m_MeshComponent == nullptr);
		assert(matID != InvalidMaterialID);
		MeshComponent* skyboxMesh = new MeshComponent(matID, this, false);
		skyboxMesh->LoadPrefabShape(MeshComponent::PrefabShape::SKYBOX);
		SetMeshComponent(skyboxMesh);

		JSONObject skyboxInfo;
		if (parentObj.SetObjectChecked("skybox info", skyboxInfo))
		{
			glm::vec3 rotEuler;
			if (skyboxInfo.SetVec3Checked("rot", rotEuler))
			{
				m_Transform.SetWorldRotation(glm::quat(rotEuler));
			}
		}

		g_Renderer->SetSkyboxMesh(this);
	}

	void Skybox::SerializeUniqueFields(JSONObject& parentObject) const
	{
		JSONObject skyboxInfo = {};
		glm::quat worldRot = m_Transform.GetWorldRotation();
		if (worldRot != QUAT_UNIT)
		{
			std::string eulerRotStr = Vec3ToString(glm::eulerAngles(worldRot), 2);
			skyboxInfo.fields.emplace_back("rot", JSONValue(eulerRotStr));
		}

		parentObject.fields.emplace_back("skybox info", JSONValue(skyboxInfo));
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
	}

	void DirectionalLight::Initialize()
	{
		g_Renderer->RegisterDirectionalLight(this);
		data.dir = glm::eulerAngles(m_Transform.GetLocalRotation());

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

		if (ImGui::TreeNode("Directional Light"))
		{
			if (ImGui::Checkbox("Enabled", &m_bVisible))
			{
				data.enabled = m_bVisible ? 1 : 0;
			}

			glm::vec3 position = m_Transform.GetLocalPosition();
			if (ImGui::DragFloat3("Position", &position.x, 0.1f))
			{
				m_Transform.SetLocalPosition(position);
			}
			glm::vec3 dirtyRot = glm::degrees(glm::eulerAngles(m_Transform.GetLocalRotation()));
			glm::vec3 cleanedRot;
			if (DoImGuiRotationDragFloat3("Rotation", dirtyRot, cleanedRot))
			{
				m_Transform.SetLocalRotation(glm::quat(glm::radians(cleanedRot)));
				data.dir = cleanedRot;
			}
			ImGui::SliderFloat("Brightness", &data.brightness, 0.0f, 15.0f);
			ImGui::ColorEdit4("Color ", &data.color.r, colorEditFlags);

			ImGui::Spacing();
			ImGui::Text("Shadow");

			ImGui::Checkbox("Cast shadow", &bCastShadow);
			ImGui::SliderFloat("Shadow darkness", &shadowDarkness, 0.0f, 1.0f);

			ImGui::DragFloat("Near", &shadowMapNearPlane);
			ImGui::DragFloat("Far", &shadowMapFarPlane);
			ImGui::DragFloat("Zoom", &shadowMapZoom);

			if (ImGui::CollapsingHeader("Preview"))
			{
				ImGui::Image((void*)shadowTextureID, ImVec2(256, 256), ImVec2(0, 1), ImVec2(1, 0));
			}

			ImGui::TreePop();
		}
	}

	void DirectionalLight::SetVisible(bool bVisible, bool bEffectChildren /* = true */)
	{
		data.enabled = (bVisible ? 1 : 0);
		GameObject::SetVisible(bVisible, bEffectChildren);
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

	void DirectionalLight::ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID)
	{
		UNREFERENCED_PARAMETER(scene);
		UNREFERENCED_PARAMETER(matID);

		JSONObject directionalLightObj;
		if (parentObject.SetObjectChecked("directional light info", directionalLightObj))
		{
			std::string dirStr = directionalLightObj.GetString("rotation");
			glm::quat rot(ParseVec3(dirStr));
			m_Transform.SetLocalRotation(rot);
			data.dir = glm::eulerAngles(rot);

			std::string posStr = directionalLightObj.GetString("pos");
			if (!posStr.empty())
			{
				m_Transform.SetLocalPosition(ParseVec3(posStr));
			}

			directionalLightObj.SetVec3Checked("color", data.color);

			directionalLightObj.SetFloatChecked("brightness", data.brightness);

			if (directionalLightObj.HasField("enabled"))
			{
				m_bVisible = directionalLightObj.GetBool("enabled") ? 1 : 0;
			}

			directionalLightObj.SetBoolChecked("cast shadows", bCastShadow);
			directionalLightObj.SetFloatChecked("shadow darkness", shadowDarkness);

			if (directionalLightObj.HasField("shadow map near"))
			{
				directionalLightObj.SetFloatChecked("shadow map near", shadowMapNearPlane);
			}

			if (directionalLightObj.HasField("shadow map far"))
			{
				directionalLightObj.SetFloatChecked("shadow map far", shadowMapFarPlane);
			}

			if (directionalLightObj.HasField("shadow map zoom"))
			{
				directionalLightObj.SetFloatChecked("shadow map zoom", shadowMapZoom);
			}
		}
	}

	void DirectionalLight::SerializeUniqueFields(JSONObject& parentObject) const
	{
		JSONObject dirLightObj = {};

		glm::vec3 dirLightDir = glm::eulerAngles(m_Transform.GetLocalRotation());
		std::string dirStr = Vec3ToString(dirLightDir, 3);
		dirLightObj.fields.emplace_back("rotation", JSONValue(dirStr));

		std::string posStr = Vec3ToString(m_Transform.GetLocalPosition(), 3);
		dirLightObj.fields.emplace_back("pos", JSONValue(posStr));

		std::string colorStr = Vec3ToString(data.color, 2);
		dirLightObj.fields.emplace_back("color", JSONValue(colorStr));

		dirLightObj.fields.emplace_back("enabled", JSONValue(m_bVisible != 0));
		dirLightObj.fields.emplace_back("brightness", JSONValue(data.brightness));

		dirLightObj.fields.emplace_back("cast shadows", JSONValue(bCastShadow));
		dirLightObj.fields.emplace_back("shadow darkness", JSONValue(shadowDarkness));
		dirLightObj.fields.emplace_back("shadow map near", JSONValue(shadowMapNearPlane));
		dirLightObj.fields.emplace_back("shadow map far", JSONValue(shadowMapFarPlane));
		dirLightObj.fields.emplace_back("shadow map zoom", JSONValue(shadowMapZoom));

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
		m_Transform.SetLocalRotation(newRot);
		data.dir = glm::eulerAngles(newRot);
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
		data.brightness = 1.0f;
	}

	void PointLight::Initialize()
	{
		ID = g_Renderer->RegisterPointLight(&data);

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

		static const ImGuiColorEditFlags colorEditFlags =
			ImGuiColorEditFlags_NoInputs |
			ImGuiColorEditFlags_Float |
			ImGuiColorEditFlags_RGB |
			ImGuiColorEditFlags_PickerHueWheel |
			ImGuiColorEditFlags_HDR;

		const std::string objectName("Point Light##" + m_Name);
		const bool bTreeOpen = ImGui::TreeNode(objectName.c_str());
		bool bRemovedPointLight = false;
		bool bEditedPointLightData = false;

		if (ImGui::BeginPopupContextItem())
		{
			static const char* removePointLightStr = "Delete";
			if (ImGui::Button(removePointLightStr))
			{
				g_Renderer->RemovePointLight(ID);
				bRemovedPointLight = true;
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		if (!bRemovedPointLight && bTreeOpen)
		{
			bool bEnabled = (data.enabled == 1);
			if (ImGui::Checkbox("Enabled", &bEnabled))
			{
				bEditedPointLightData = true;
				data.enabled = bEnabled ? 1 : 0;
				m_bVisible = bEnabled;
			}

			glm::vec3 position = m_Transform.GetLocalPosition();
			if (ImGui::DragFloat3("Position", &position.x, 0.1f))
			{
				bEditedPointLightData = true;
				SetPos(position);
			}
			bEditedPointLightData |= ImGui::ColorEdit4("Color ", &data.color.r, colorEditFlags);
			bEditedPointLightData |= ImGui::SliderFloat("Brightness", &data.brightness, 0.0f, 1000.0f);
		}

		if (bEditedPointLightData)
		{
			g_Renderer->UpdatePointLightData(ID, &data);
		}

		if (bTreeOpen)
		{
			ImGui::TreePop();
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

	void PointLight::ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID)
	{
		UNREFERENCED_PARAMETER(scene);
		UNREFERENCED_PARAMETER(matID);

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

		std::string posStr = Vec3ToString(m_Transform.GetLocalPosition(), 3);
		pointLightObj.fields.emplace_back("pos", JSONValue(posStr));

		std::string colorStr = Vec3ToString(data.color, 2);
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
		if (!g_Renderer->GetMaterialID("pbr grey", matID))
		{
			// :shrug:
			// TODO: Create own material
			matID = 0;
		}
		MeshComponent* mesh = SetMeshComponent(new MeshComponent(matID, this));
		std::string meshFilePath = std::string(RESOURCE("meshes/")) + std::string(meshName);
		if (!mesh->LoadFromFile(meshFilePath))
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
		UNREFERENCED_PARAMETER(obj);
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

	void Cart::ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID)
	{
		UNREFERENCED_PARAMETER(scene);
		UNREFERENCED_PARAMETER(matID);

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

	void EngineCart::ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID)
	{
		UNREFERENCED_PARAMETER(scene);
		UNREFERENCED_PARAMETER(matID);

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
		if (!g_Renderer->GetMaterialID("pbr white", matID))
		{
			// TODO: Create own material
			matID = 0;
		}
		MeshComponent* mesh = SetMeshComponent(new MeshComponent(matID, this));
		if (!mesh->LoadFromFile(RESOURCE("meshes/mobile-liquid-box.glb")))
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

	void MobileLiquidBox::ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID)
	{
		UNREFERENCED_PARAMETER(parentObject);
		UNREFERENCED_PARAMETER(scene);
		UNREFERENCED_PARAMETER(matID);
	}

	void MobileLiquidBox::SerializeUniqueFields(JSONObject& parentObject) const
	{
		UNREFERENCED_PARAMETER(parentObject);
	}

	GerstnerWave::GerstnerWave(const std::string& name) :
		GameObject(name, GameObjectType::GERSTNER_WAVE)
	{
		MaterialCreateInfo matCreateInfo = {};
		matCreateInfo.name = "Gerstner";
		matCreateInfo.shaderName = "pbr";
		matCreateInfo.constAlbedo = glm::vec3(0.4f, 0.5f, 0.8f);
		matCreateInfo.constMetallic = 0.8f;
		matCreateInfo.constRoughness = 0.01f;
		matCreateInfo.constAO = 1.0f;

		MaterialID planeMatID = g_Renderer->InitializeMaterial(&matCreateInfo);

		MeshComponent* planeMesh = SetMeshComponent(new MeshComponent(planeMatID, this));
		planeMesh->LoadPrefabShape(MeshComponent::PrefabShape::GERSTNER_PLANE);

		i32 vertCount = vertSideCount * vertSideCount;
		bufferInfo.positions_3D.resize(vertCount);
		bufferInfo.normals.resize(vertCount);
		bufferInfo.tangents.resize(vertCount);
		bufferInfo.bitangents.resize(vertCount);
		bufferInfo.colors_R32G32B32A32.resize(vertCount);

		for (i32 z = 0; z < vertSideCount; ++z)
		{
			for (i32 x = 0; x < vertSideCount; ++x)
			{
				i32 vertIdx = z * vertSideCount + x;

				bufferInfo.normals[vertIdx] = glm::vec3(0.0f, 1.0f, 0.0f);
				bufferInfo.tangents[vertIdx] = glm::vec3(1.0f, 0.0f, 0.0f);
				bufferInfo.bitangents[vertIdx] = glm::vec3(0.0f, 0.0f, 1.0f);
				bufferInfo.colors_R32G32B32A32[vertIdx] = VEC4_ONE;
			}
		}

		for (i32 i = 0; i < (i32)waves.size(); ++i)
		{
			UpdateDependentVariables(i);
		}

		bobberTarget = Spring<real>(0.0f);
		bobberTarget.DR = 2.5f;
		bobberTarget.UAF = 40.0f;
		bobber = new GameObject("Bobber", GameObjectType::_NONE);
		bobber->SetSerializable(false);
		MaterialID matID = InvalidMaterialID;
		if (!g_Renderer->GetMaterialID("pbr red", matID))
		{
			PrintError("Failed to find material for bobber!\n");
		}
		MeshComponent* mesh = bobber->SetMeshComponent(new MeshComponent(matID, bobber));
		if (!mesh->LoadFromFile(RESOURCE("meshes/sphere.glb")))
		{
			PrintError("Failed to load bobber mesh\n");
		}
		g_SceneManager->CurrentScene()->AddRootObject(bobber);
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

		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.95f, 1.0f), "Gerstner");

		if (ImGui::Button("+"))
		{
			AddWave();
		}

		ImGui::PushItemWidth(30.0f);
		ImGui::DragFloat("Bobber DR", &bobberTarget.DR, 0.01f);
		ImGui::SameLine();
		ImGui::DragFloat("UAF", &bobberTarget.UAF, 0.01f);
		ImGui::PopItemWidth();

		for (i32 i = 0; i < (i32)waves.size(); ++i)
		{
			std::string childName = "##wave " + IntToString(i, 2);

			std::string removeStr = "-" + childName;
			if (ImGui::Button(removeStr.c_str()))
			{
				RemoveWave(i);
				break;
			}

			ImGui::SameLine();

			bool bNeedUpdate = false;

			std::string enabledStr = "enabled" + childName;
			ImGui::Checkbox(enabledStr.c_str(), &waves[i].enabled);

			std::string aStr = "amplitude" + childName;
			ImGui::DragFloat(aStr.c_str(), &waves[i].a, 0.01f);
			std::string waveLenStr = "wave len" + childName;
			bNeedUpdate |= ImGui::DragFloat(waveLenStr.c_str(), &waves[i].waveLen, 0.01f);
			std::string dirStr = "dir" + childName;
			bNeedUpdate |= ImGui::DragFloat(dirStr.c_str(), &waves[i].waveDirTheta, 0.004f);

			if (bNeedUpdate)
			{
				UpdateDependentVariables(i);
			}

			ImGui::Separator();
		}
	}

	void GerstnerWave::ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID)
	{
		UNREFERENCED_PARAMETER(scene);
		UNREFERENCED_PARAMETER(matID);

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
		}

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

		parentObject.fields.emplace_back("gerstner wave", JSONValue(gerstnerWaveObj));
	}

	void GerstnerWave::UpdateDependentVariables(i32 waveIndex)
	{
		if (waveIndex >= 0 && waveIndex < (i32)waves.size())
		{
			waves[waveIndex].waveVecMag = TWO_PI / waves[waveIndex].waveLen;
			waves[waveIndex].moveSpeed = glm::sqrt(9.81f * waves[waveIndex].waveVecMag);

			waves[waveIndex].waveDirCos = cos(waves[waveIndex].waveDirTheta);
			waves[waveIndex].waveDirSin = sin(waves[waveIndex].waveDirTheta);
		}
	}

	void GerstnerWave::Update()
	{
		PROFILE_AUTO("Gerstner update");

		const glm::vec3 startPos(-size / 2.0f, 0.0f, -size / 2.0f);

		std::vector<glm::vec3>& positions = bufferInfo.positions_3D;
		std::vector<glm::vec3>& normals = bufferInfo.normals;
		std::vector<glm::vec3>& tangents = bufferInfo.tangents;
		std::vector<glm::vec3>& bitangents = bufferInfo.bitangents;

		// Clear positions and normals
		for (i32 z = 0; z < vertSideCount; ++z)
		{
			for (i32 x = 0; x < vertSideCount; ++x)
			{
				positions[z * vertSideCount + x] = startPos + glm::vec3(
					size * ((real)x / (vertSideCount - 1)),
					0.0f,
					size * ((real)z / (vertSideCount - 1)));
				normals[z * vertSideCount + x] = VEC3_UP;
			}
		}

		// Calculate positions
		for (WaveInfo& wave : waves)
		{
			if (wave.enabled)
			{
				const glm::vec3 waveVec = glm::vec3(wave.waveDirCos, 0.0f, wave.waveDirSin) * wave.waveVecMag;
				const glm::vec3 waveVecN = glm::normalize(waveVec);

				wave.accumOffset += (wave.moveSpeed * g_DeltaTime);

				for (i32 z = 0; z < vertSideCount; ++z)
				{
					for (i32 x = 0; x < vertSideCount; ++x)
					{
						i32 vertIdx = z * vertSideCount + x;

						real d = glm::dot(waveVec, positions[vertIdx]);
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

		// Ripple
		glm::vec3 ripplePos = VEC3_ZERO;
		real rippleAmp = 0.8f;
		real rippleLen = 0.6f;
		real rippleFadeOut = 12.0f;
		for (i32 z = 0; z < vertSideCount; ++z)
		{
			for (i32 x = 0; x < vertSideCount; ++x)
			{
				i32 vertIdx = z * vertSideCount + x;

				glm::vec3 diff = (ripplePos - positions[vertIdx]);
				real d = glm::length(diff);
				diff = glm::normalize(diff) * rippleLen;
				real c = cos(g_SecElapsedSinceProgramStart * 1.8f - d);
				real s = sin(g_SecElapsedSinceProgramStart * 1.5f - d);
				real a = Lerp(0.0f, rippleAmp, 1.0f - glm::clamp(d / rippleFadeOut, 0.0f, 1.0f));
				positions[vertIdx] += glm::vec3(
					-diff.x * a * s,
					a * c,
					-diff.z * a * s);
			}
		}

		// Calculate normals
		for (i32 z = 1; z < vertSideCount - 1; ++z)
		{
			for (i32 x = 1; x < vertSideCount - 1; ++x)
			{
				i32 vertIdx = z * vertSideCount + x;
				bufferInfo.tangents[vertIdx] = glm::normalize((positions[vertIdx + 1] - positions[vertIdx]) + (positions[vertIdx] - positions[vertIdx - 1]));
				bufferInfo.bitangents[vertIdx] = glm::normalize((positions[vertIdx + vertSideCount] - positions[vertIdx]) + (positions[vertIdx] - positions[vertIdx - vertSideCount]));
				bufferInfo.normals[vertIdx] = glm::cross(bitangents[vertIdx], tangents[vertIdx]);
			}
		}

		VertexBufferData* vertexBuffer = m_MeshComponent->GetVertexBufferData();
		bufferInfo.positions_3D = positions;
		bufferInfo.normals = normals;
		bufferInfo.tangents = tangents;
		bufferInfo.bitangents = bitangents;
		vertexBuffer->UpdateData(&bufferInfo);
		g_Renderer->UpdateVertexData(m_RenderID, vertexBuffer);


		const glm::vec3 wavePos = m_Transform.GetWorldPosition();
		const glm::vec3 waveScale = m_Transform.GetWorldScale();
		glm::vec3 surfacePos = positions[positions.size() / 2 + vertSideCount / 2];
		bobberTarget.SetTargetPos(surfacePos.y);
		bobberTarget.Tick(g_DeltaTime);
		real vOffset = 0.2f;
		glm::vec3 newPos = wavePos + glm::vec3(surfacePos.x, bobberTarget.pos + vOffset, surfacePos.z);
		bobber->GetTransform()->SetWorldPosition(newPos);

		//btVector3 targetPosBT = btVector3(wavePos.x + surfacePos.x, wavePos.y + bobberTarget.targetPos, wavePos.z + surfacePos.z);
		//g_Renderer->GetDebugDrawer()->drawSphere(targetPosBT, 1.0f, btVector3(1.0f, 0.0f, 0.1f));
		//btVector3 posBT = btVector3(wavePos.x + surfacePos.x, wavePos.y + bobberTarget.pos, wavePos.z + surfacePos.z);
		//g_Renderer->GetDebugDrawer()->drawSphere(posBT, 0.7f, btVector3(0.75f, 0.5f, 0.6f));
	}

	void GerstnerWave::AddWave()
	{
		waves.push_back({});
		UpdateDependentVariables(waves.size() - 1);
	}

	void GerstnerWave::RemoveWave(i32 index)
	{
		if (index >= 0 && index < (i32)waves.size())
		{
			waves.erase(waves.begin() + index);
		}
	}

	OperatorType Operator::Parse(Tokenizer& tokenizer)
	{
		Token token = tokenizer.GetNextToken();
		switch (token.type)
		{
		case TokenType::ADD: return OperatorType::ADD;
		case TokenType::SUBTRACT: return OperatorType::SUB;
		case TokenType::MULTIPLY: return OperatorType::MUL;
		case TokenType::DIVIDE: return OperatorType::DIV;
		case TokenType::MODULO: return OperatorType::MOD;
		case TokenType::BINARY_AND: return OperatorType::BIN_AND;
		case TokenType::BINARY_OR: return OperatorType::BIN_OR;
		case TokenType::BINARY_XOR: return OperatorType::BIN_XOR;
		case TokenType::EQUAL_TEST: return OperatorType::EQUAL;
		case TokenType::NOT_EQUAL_TEST: return OperatorType::NOT_EQUAL;
		case TokenType::GREATER: return OperatorType::GREATER;
		case TokenType::GREATER_EQUAL: return OperatorType::GREATER_EQUAL;
		case TokenType::LESS: return OperatorType::LESS;
		case TokenType::LESS_EQUAL: return OperatorType::LESS_EQUAL;
		case TokenType::BOOLEAN_AND: return OperatorType::BOOLEAN_AND;
		case TokenType::BOOLEAN_OR: return OperatorType::BOOLEAN_OR;
		default:
		{
			tokenizer.context->errorReason = "Unexpected operator";
			tokenizer.context->errorToken = token;
			return OperatorType::_NONE;
		}
		}
	}

	std::string Token::ToString() const
	{
		return std::string(charPtr, len);
	}

	TokenContext::TokenContext()
	{
		Reset();
	}

	TokenContext::~TokenContext()
	{
		if (instantiatedIdentifiers != nullptr)
		{
			for (i32 i = 0; i < variableCount; ++i)
			{
				assert(instantiatedIdentifiers[i].value != nullptr);
				delete instantiatedIdentifiers[i].value;
			}
			delete[] instantiatedIdentifiers;
		}
		instantiatedIdentifiers = nullptr;
		variableCount = 0;
	}

	void TokenContext::Reset()
	{
		buffer = nullptr;
		bufferPtr = nullptr;
		bufferLen = -1;
		errorReason = "";
		errorToken = g_EmptyToken;
		linePos = 0;
		lineNumber = 0;

		errors.clear();

		if (instantiatedIdentifiers != nullptr)
		{
			for (i32 i = 0; i < variableCount; ++i)
			{
				assert(instantiatedIdentifiers[i].value != nullptr);
				delete instantiatedIdentifiers[i].value;
			}
			delete[] instantiatedIdentifiers;
		}
		variableCount = 0;
		instantiatedIdentifiers = new InstantiatedIdentifier[MAX_VARS];
		tokenNameToInstantiatedIdentifierIdx.clear();
	}

	bool TokenContext::HasNextChar() const
	{
		return GetRemainingLength() > 0;
	}

	char TokenContext::ConsumeNextChar()
	{
		assert((bufferPtr + 1 - buffer) <= bufferLen);

		char nextChar = bufferPtr[0];
		bufferPtr++;
		linePos++;
		if (nextChar == '\n')
		{
			linePos = 0;
			lineNumber++;
		}
		return nextChar;
	}

	char TokenContext::PeekNextChar() const
	{
		assert((bufferPtr - buffer) <= bufferLen);

		return bufferPtr[0];
	}

	char TokenContext::PeekChar(i32 index) const
	{
		assert(index >= 0 && index < GetRemainingLength());

		return bufferPtr[index];

	}

	glm::i32 TokenContext::GetRemainingLength() const
	{
		return bufferLen - (bufferPtr - buffer);
	}

	bool TokenContext::CanNextCharBeIdentifierPart() const
	{
		if (!HasNextChar())
		{
			return false;
		}

		char next = PeekNextChar();
		if (isalpha(next) || isdigit(next) || next == '_')
		{
			return true;
		}

		return false;
	}

	TokenType TokenContext::AttemptParseKeyword(const char* keywordStr, TokenType keywordType)
	{
		i32 keywordPos = 1; // Assume first letter is already consumed
		char c = PeekNextChar();
		if (!(isalpha(c) || isdigit(c) || c == '_'))
		{
			// Next char is delimiter, token must be one char long
			return TokenType::IDENTIFIER;
		}

		while (keywordPos < (i32)strlen(keywordStr) && PeekNextChar() == keywordStr[keywordPos])
		{
			c = ConsumeNextChar();
			++keywordPos;
		}

		char nc = PeekNextChar();
		const bool bCIsDelimiter = !(isalpha(nc) || isdigit(nc) || nc == '_');
		if (keywordPos == (i32)strlen(keywordStr) && bCIsDelimiter)
		{
			return keywordType;
		}

		// Not this keyword, must be a identifier
		while (CanNextCharBeIdentifierPart())
		{
			ConsumeNextChar();
		}

		return TokenType::IDENTIFIER;
	}

	TokenType TokenContext::AttemptParseKeywords(const char* potentialKeywordStrs[], TokenType potentialKeywordTypes[], i32 keywordPositions[], i32 potentialCount)
	{
		char c = PeekNextChar();
		if (!(isalpha(c) || isdigit(c) || c == '_'))
		{
			// Next char is delimiter, token must be one char long
			return TokenType::IDENTIFIER;
		}

		for (i32 i = 0; i < potentialCount; ++i)
		{
			keywordPositions[i] = 1;
		}

		i32 matchedKeywordIndex = -1;
		bool bMatched = true;

		while (bMatched)
		{
			bMatched = false;
			for (i32 i = 0; i < potentialCount; ++i)
			{
				if (keywordPositions[i] != -1)
				{
					if (c == potentialKeywordStrs[i][keywordPositions[i]])
					{
						bMatched = true;
						if (++keywordPositions[i] >= (i32)strlen(potentialKeywordStrs[i]))
						{
							bool bLastChar = (GetRemainingLength() == 1);
							char nc = bLastChar ? ' ' : PeekChar(1);
							if (bLastChar || !(isalpha(nc) || isdigit(nc) || nc == '_'))
							{
								matchedKeywordIndex = i;
								ConsumeNextChar();
								break;
							}
							else
							{
								keywordPositions[i] = -1;
								bMatched = false;
							}
						}
					}
					else
					{
						keywordPositions[i] = -1;
					}
				}
			}

			if (matchedKeywordIndex != -1)
			{
				return potentialKeywordTypes[matchedKeywordIndex];
			}

			if (bMatched)
			{
				ConsumeNextChar();
				c = PeekNextChar();
			}
			else
			{
				// Doesn't match any keywords, must be a identifier

				while (CanNextCharBeIdentifierPart())
				{
					ConsumeNextChar();
				}
				return TokenType::IDENTIFIER;
			}
		}

		return TokenType::_NONE;
	}

	Value* TokenContext::InstantiateIdentifier(const Token& identifierToken, TypeName typeName)
	{
		const std::string tokenName = identifierToken.ToString();

		if (tokenNameToInstantiatedIdentifierIdx.find(tokenName) != tokenNameToInstantiatedIdentifierIdx.end())
		{
			errorReason = "Redeclaration of type";
			errorToken = identifierToken;
			return nullptr;
		}

		const i32 nextIndex = variableCount++;
		tokenNameToInstantiatedIdentifierIdx.emplace(tokenName, nextIndex);

		assert(nextIndex >= 0 && nextIndex < MAX_VARS);

		assert(instantiatedIdentifiers[nextIndex].index == 0);
		assert(instantiatedIdentifiers[nextIndex].value == nullptr);

		switch (typeName)
		{
		case TypeName::INT:
			instantiatedIdentifiers[nextIndex].value = new Value(0, false);
			break;
		case TypeName::FLOAT:
			instantiatedIdentifiers[nextIndex].value = new Value(0.0f, false);
			break;
		case TypeName::BOOL:
			instantiatedIdentifiers[nextIndex].value = new Value(false, false);
			break;
		default:
			errorReason = "Unhandled identifier typename encountered in InstantiateIdentifier";
			errorToken = identifierToken;
			return nullptr;
		}
		// NOTE: Enforce a copy of the string
		instantiatedIdentifiers[nextIndex].name = tokenName;
		instantiatedIdentifiers[nextIndex].index = nextIndex;
		return instantiatedIdentifiers[nextIndex].value;
	}

	Value* TokenContext::GetVarInstanceFromToken(const Token& token)
	{
		assert(token.tokenID >= 0 && token.tokenID < MAX_VARS);
		std::string tokenName = token.ToString();
		if (tokenNameToInstantiatedIdentifierIdx.find(tokenName) == tokenNameToInstantiatedIdentifierIdx.end())
		{
			errorReason = "Use of undefined type";
			errorToken = token;
			return nullptr;
		}
		i32 idx = tokenNameToInstantiatedIdentifierIdx[tokenName];
		assert(idx >= 0 && idx < variableCount);
		return instantiatedIdentifiers[idx].value;
	}

	bool TokenContext::IsKeyword(const char* str)
	{
		for (const char* keyword : g_KeywordStrings)
		{
			if (strcmp(str, keyword) == 0)
			{
				return true;
			}
		}
		return false;
	}

	bool TokenContext::IsKeyword(TokenType type)
	{
		return (i32)type > (i32)TokenType::KEYWORDS_START &&
			   (i32)type < (i32)TokenType::KEYWORDS_END;
	}

	Tokenizer::Tokenizer()
	{
		assert(context == nullptr);

		context = new TokenContext();
		SetCodeStr("");
	}

	Tokenizer::Tokenizer(const std::string& codeStr)
	{
		assert(context == nullptr);

		context = new TokenContext();
		SetCodeStr(codeStr);
	}

	Tokenizer::~Tokenizer()
	{
		delete context;
		context = nullptr;
		codeStrCopy = "";
	}

	void Tokenizer::SetCodeStr(const std::string& newCodeStr)
	{
		assert(context != nullptr);

		codeStrCopy = newCodeStr;

		context->Reset();
		context->buffer = context->bufferPtr = (char*)codeStrCopy.c_str();
		context->bufferLen = (i32)codeStrCopy.size();
		nextTokenID = 0;
		bConsumedLastParsedToken = true;
		lastParsedToken = g_EmptyToken;
	}

	Token Tokenizer::PeekNextToken()
	{
		if (bConsumedLastParsedToken)
		{
			lastParsedToken = GetNextToken();
			bConsumedLastParsedToken = false;
		}
		return lastParsedToken;
	}

	Token Tokenizer::GetNextToken()
	{
		if (!bConsumedLastParsedToken)
		{
			bConsumedLastParsedToken = true;
			return lastParsedToken;
		}

		ConsumeWhitespaceAndComments();

		char const* const tokenStart = context->bufferPtr;
		i32 tokenLineNum = context->lineNumber;
		i32 tokenLinePos = context->linePos;

		TokenType nextTokenType = TokenType::_NONE;

		if (context->HasNextChar())
		{
			char c = context->ConsumeNextChar();

			switch (c)
			{
			// Keywords:
			case 't':
				nextTokenType = context->AttemptParseKeyword("true", TokenType::KEY_TRUE);
				break;
			case 'f':
				nextTokenType = context->AttemptParseKeyword("false", TokenType::KEY_FALSE);
				break;
			case 'b':
			{
				const char* potentialKeywords[] = { "bool", "break" };
				TokenType potentialTokenTypes[] = { TokenType::KEY_BOOL, TokenType::KEY_BREAK };
				i32 pos[] = { 0, 0 };
				nextTokenType = context->AttemptParseKeywords(potentialKeywords, potentialTokenTypes, pos, ARRAY_LENGTH(pos));
			} break;
			case 'i':
			{
				const char* potentialKeywords[] = { "int", "if" };
				TokenType potentialTokenTypes[] = { TokenType::KEY_INT, TokenType::KEY_IF };
				i32 pos[] = { 0, 0 };
				nextTokenType = context->AttemptParseKeywords(potentialKeywords, potentialTokenTypes, pos, ARRAY_LENGTH(pos));
			} break;
			case 'e':
			{
				const char* potentialKeywords[] = { "else", "elif" };
				TokenType potentialTokenTypes[] = { TokenType::KEY_ELSE, TokenType::KEY_ELIF };
				i32 pos[] = { 0, 0 };
				nextTokenType = context->AttemptParseKeywords(potentialKeywords, potentialTokenTypes, pos, ARRAY_LENGTH(pos));
			} break;
			case 'd':
				nextTokenType = context->AttemptParseKeyword("do", TokenType::KEY_DO);
				break;
			case 'w':
				nextTokenType = context->AttemptParseKeyword("while", TokenType::KEY_WHILE);
				break;

			// Non-keywords
			case '{':
				nextTokenType = TokenType::OPEN_BRACKET;
				break;
			case '}':
				nextTokenType = TokenType::CLOSE_BRACKET;
				break;
			case '(':
				nextTokenType = TokenType::OPEN_PAREN;
				break;
			case ')':
				nextTokenType = TokenType::CLOSE_PAREN;
				break;
			case '[':
				nextTokenType = TokenType::OPEN_SQUARE_BRACKET;
				break;
			case ']':
				nextTokenType = TokenType::CLOSE_SQUARE_BRACKET;
				break;
			case ':':
				nextTokenType = Type1IfNextCharIsCElseType2(':', TokenType::DOUBLE_COLON, TokenType::SINGLE_COLON);
				break;
			case ';':
				nextTokenType = TokenType::SEMICOLON;
				break;
			case '!':
				nextTokenType = Type1IfNextCharIsCElseType2('=', TokenType::NOT_EQUAL_TEST, TokenType::BANG);
				break;
			case '?':
				nextTokenType = TokenType::TERNARY;
				break;
			case '=':
				nextTokenType = Type1IfNextCharIsCElseType2('=', TokenType::EQUAL_TEST, TokenType::ASSIGNMENT);
				break;
			case '>':
				nextTokenType = Type1IfNextCharIsCElseType2('=', TokenType::GREATER_EQUAL, TokenType::GREATER);
				break;
			case '<':
				nextTokenType = Type1IfNextCharIsCElseType2('=', TokenType::LESS_EQUAL, TokenType::LESS);
				break;
			case '+':
				nextTokenType = Type1IfNextCharIsCElseType2('=', TokenType::ADD_ASSIGN, TokenType::ADD);
				break;
			case '-':
				nextTokenType = Type1IfNextCharIsCElseType2('=', TokenType::SUBTRACT_ASSIGN, TokenType::SUBTRACT);
				break;
			case '*':
				nextTokenType = Type1IfNextCharIsCElseType2('=', TokenType::MULTIPLY_ASSIGN, TokenType::MULTIPLY);
				break;
			case '/':
				nextTokenType = Type1IfNextCharIsCElseType2('=', TokenType::DIVIDE_ASSIGN, TokenType::DIVIDE);
				break;
			case '%':
				nextTokenType = Type1IfNextCharIsCElseType2('=', TokenType::MODULO_ASSIGN, TokenType::MODULO);
				break;
			case '&':
				if (context->PeekNextChar() == '=')
				{
					nextTokenType = TokenType::BINARY_AND_ASSIGN;
				}
				else
				{
					nextTokenType = Type1IfNextCharIsCElseType2('&', TokenType::BOOLEAN_AND, TokenType::BINARY_AND);
				}
				break;
			case '|':
				if (context->PeekNextChar() == '=')
				{
					nextTokenType = TokenType::BINARY_OR_ASSIGN;
				}
				else
				{
					nextTokenType = Type1IfNextCharIsCElseType2('|', TokenType::BOOLEAN_OR, TokenType::BINARY_OR);
				}
				break;
			case '^':
				nextTokenType = Type1IfNextCharIsCElseType2('=', TokenType::BINARY_XOR_ASSIGN, TokenType::BINARY_XOR);
				break;
			case '\\':
				if (context->HasNextChar())
				{
					// Escaped char (unhandled)
					context->ConsumeNextChar();
				}
				else
				{
					context->errorReason = "Input ended with a single backslash";
				}
				break;
			case '\'':
			{
				nextTokenType = TokenType::STRING;
				while (context->HasNextChar())
				{
					char cn = context->ConsumeNextChar();
					if (cn == '\\' || cn == '\'')
					{
						break;
					}
				}
			} break;
			case'\"':
			{
				nextTokenType = TokenType::STRING;
				while (context->HasNextChar())
				{
					char cn = context->ConsumeNextChar();
					if (cn == '\\' || cn == '\"')
					{
						break;
					}
				}
			} break;
			case '~':
				nextTokenType = TokenType::TILDE;
				break;
			case '`':
				nextTokenType = TokenType::BACK_QUOTE;
				break;
			default:
				if (isalpha(c) || c == '_')
				{
					nextTokenType = TokenType::IDENTIFIER;
					while (context->CanNextCharBeIdentifierPart())
					{
						context->ConsumeNextChar();
					}
				}
				else if (isdigit(c) || c == '-' || c == '.')
				{
					// TODO: Handle "0x" prefix

					bool bConsumedDigit = isdigit(c);
					bool bConsumedDecimal = c == '.';
					bool bConsumedF = false;
					bool bValidNum = bConsumedDigit;

					if (context->HasNextChar())
					{
						bool bCharIsValid = true;
						char nc;
						do
						{
							nc = context->PeekNextChar();
							bCharIsValid = ValidDigitChar(nc);

							if (isdigit(nc))
							{
								bConsumedDigit = true;
								bValidNum = true;
							}

							if (nc == 'f')
							{
								// Ensure f comes last if at all
								bConsumedF = true;
								if (context->HasNextChar())
								{
									char nnc = context->PeekChar(1);
									if (ValidDigitChar(nnc))
									{
										context->errorReason = "Incorrectly formatted number, 'f' must be final character";
										bValidNum = false;
										break;
									}
								}
							}

							// Ensure . appears only once if at all
							if (nc == '.')
							{
								if (bConsumedDecimal)
								{
									context->errorReason = "Incorrectly formatted number";
									bValidNum = false;
								}
								else
								{
									bConsumedDecimal = true;
								}
							}

							if (bCharIsValid)
							{
								context->ConsumeNextChar();
							}
						} while (bValidNum && context->HasNextChar() && bCharIsValid && !bConsumedF);
					}

					if (bConsumedDigit)
					{
						if (bConsumedDecimal || bConsumedF)
						{
							nextTokenType = TokenType::FLOAT_LITERAL;
						}
						else
						{
							nextTokenType = TokenType::INT_LITERAL;
						}
					}
				}
				break;
			}
		}

		Token token = {};
		token.lineNum = tokenLineNum;
		token.linePos = tokenLinePos;
		token.charPtr = tokenStart;
		token.len = (context->bufferPtr - tokenStart);
		token.type = nextTokenType;
		token.tokenID = nextTokenID++;

		lastParsedToken = token;

		return token;
	}

	void Tokenizer::ConsumeWhitespaceAndComments()
	{
		while (context->GetRemainingLength() > 1)
		{
			char c0 = context->PeekChar(0);
			char c1 = context->PeekChar(1);

			if (isspace(c0))
			{
				context->ConsumeNextChar();
				continue;
			}
			else if (c0 == '/' && c1 == '/')
			{
				// Consume remainder of line
				while (context->HasNextChar())
				{
					char c = context->ConsumeNextChar();
					if (c == '\n')
					{
						break;
					}
				}
			}
			else if (c0 == '/' && c1 == '*')
			{
				context->ConsumeNextChar();
				context->ConsumeNextChar();
				// Consume (potentially nested) block comment(s)
				i32 levelsDeep = 1;
				while (context->HasNextChar())
				{
					char bc0 = context->ConsumeNextChar();
					char bc1 = context->PeekNextChar();
					if (bc0 == '/' && bc1 == '*')
					{
						levelsDeep++;
						context->ConsumeNextChar();
					}
					else if (bc0 == '*' && bc1 == '/')
					{
						levelsDeep--;
						context->ConsumeNextChar();
					}

					if (levelsDeep == 0)
					{
						break;
					}
				}

				if (levelsDeep != 0)
				{
					context->errorReason = "Uneven number of block comment opens/closes";
				}
			}
			else
			{
				return;
			}
		}
		if (context->HasNextChar())
		{
			if (isspace(context->PeekNextChar()))
			{
				context->ConsumeNextChar();
			}
		}
	}

	TokenType Tokenizer::Type1IfNextCharIsCElseType2(char c, TokenType ifYes, TokenType ifNo)
	{
		if (context->HasNextChar() && context->PeekNextChar() == c)
		{
			context->ConsumeNextChar();
			return ifYes;
		}
		else
		{
			return ifNo;
		}
	}

	bool Tokenizer::ValidDigitChar(char c)
	{
		return (isdigit(c) || c == 'f' || c == 'F' || c == '.');
	}

	TypeName Type::GetTypeNameFromStr(const std::string& str)
	{
		const char* tokenCStr = str.c_str();
		for (i32 i = 0; i < (i32)TypeName::_NONE; ++i)
		{
			if (strcmp(g_TypeNameStrings[i], tokenCStr) == 0)
			{
				return (TypeName)i;
			}
		}
		return TypeName::_NONE;
	}

	TypeName Type::Parse(Tokenizer& tokenizer)
	{
		Token token = tokenizer.GetNextToken();
		std::string tokenStr = token.ToString();
		TypeName result = GetTypeNameFromStr(tokenStr);
		if (result != TypeName::_NONE)
		{
			return result;
		}

		tokenizer.context->errorReason = "Expected typename";
		tokenizer.context->errorToken = token;
		return TypeName::_NONE;
	}

	Node::Node(const Token& token) :
		token(token)
	{
	}

	// TODO: Support implicit casting
	bool ValidOperatorOnType(OperatorType op, TypeName type)
	{
		switch (op)
		{
		case OperatorType::ADD:
		case OperatorType::SUB:
		case OperatorType::MUL:
		case OperatorType::DIV:
		case OperatorType::MOD:
		case OperatorType::BIN_AND:
		case OperatorType::BIN_OR:
		case OperatorType::BIN_XOR:
		case OperatorType::GREATER:
		case OperatorType::GREATER_EQUAL:
		case OperatorType::LESS:
		case OperatorType::LESS_EQUAL:
		case OperatorType::NEGATE:
			return (type == TypeName::INT || type == TypeName::FLOAT);
		case OperatorType::ASSIGN:
		case OperatorType::EQUAL:
		case OperatorType::NOT_EQUAL:
			return true;
		case OperatorType::BOOLEAN_AND:
		case OperatorType::BOOLEAN_OR:
			return (type == TypeName::BOOL);
		default:
			PrintError("Unhandled operator type in ValidOperatorOnType: %d\n", (i32)op);
			return false;
		}
	}

	TypeName ValueTypeToTypeName(ValueType valueType)
	{
		switch (valueType)
		{
		case ValueType::INT_RAW: return TypeName::INT;
		case ValueType::FLOAT_RAW: return TypeName::FLOAT;
		case ValueType::BOOL_RAW: return TypeName::BOOL;
		default: return TypeName::_NONE;
		}
	}

	Identifier::Identifier(const Token& token, const std::string& identifierStr, TypeName type) :
		Node(token),
		identifierStr(identifierStr),
		type(type)
	{
	}

	Identifier* Identifier::Parse(Tokenizer& tokenizer, TypeName type)
	{
		Token token = tokenizer.GetNextToken();
		if (token.type != TokenType::IDENTIFIER)
		{
			tokenizer.context->errorReason = "Expected identifier";
			tokenizer.context->errorToken = token;
			return nullptr;
		}

		return new Identifier(token, token.ToString(), type);
	}

	Operation::Operation(const Token& token, Expression* lhs, OperatorType op, Expression* rhs) :
		Node(token),
		lhs(lhs),
		op(op),
		rhs(rhs)
	{
	}

	Operation::~Operation()
	{
		delete lhs;
		lhs = nullptr;
		delete rhs;
		rhs = nullptr;
	}

	template<class T>
	Value* EvaluateUnaryOperation(T* val, OperatorType op)
	{
		switch (op)
		{
		case OperatorType::NEGATE:			return new Value(-(*val), true);
		default:							return nullptr;
		}
	}

	template<class T>
	Value* EvaluateOperation(T* lhs, T* rhs, OperatorType op)
	{
		switch (op)
		{
		case OperatorType::ASSIGN:
			*lhs = *rhs;
			return nullptr;
		case OperatorType::ADD:				return new Value(*lhs + *rhs, true);
		case OperatorType::SUB:				return new Value(*lhs - *rhs, true);
		case OperatorType::MUL:				return new Value(*lhs * *rhs, true);
		case OperatorType::DIV:
			// :grimmacing:
			if (typeid(*lhs) == typeid(i32))
			{
				return new Value((i32)*lhs / (i32)*rhs, true);
			}
			else if (typeid(*lhs) == typeid(real))
			{
				return new Value((real)*lhs / (real)*rhs, true);
			}
			else
			{
				return nullptr;
			}
		case OperatorType::MOD:
			if (typeid(*lhs) == typeid(real))
			{
				return new Value(fmod((real)*lhs, (real)*rhs), true);
			}
			else
			{
				return new Value((i32)*lhs % (i32)*rhs, true);
			}
		case OperatorType::BIN_AND:			return new Value((bool)*lhs & (bool)*rhs, true);
		case OperatorType::BIN_OR:			return new Value((bool)*lhs | (bool)*rhs, true);
		case OperatorType::BIN_XOR:			return new Value((bool)*lhs ^ (bool)*rhs, true);
		case OperatorType::EQUAL:			return new Value(*lhs == *rhs, true);
		case OperatorType::NOT_EQUAL:		return new Value(*lhs != *rhs, true);
		case OperatorType::GREATER:			return new Value(*lhs > *rhs, true);
		case OperatorType::GREATER_EQUAL:	return new Value(*lhs >= *rhs, true);
		case OperatorType::LESS:			return new Value(*lhs < *rhs, true);
		case OperatorType::LESS_EQUAL:		return new Value(*lhs <= *rhs, true);
		case OperatorType::BOOLEAN_AND:		return new Value(*lhs && *rhs, true);
		case OperatorType::BOOLEAN_OR:		return new Value(*lhs || *rhs, true);
		default:							return nullptr;
		}
	}

	Value* Operation::Evaluate(TokenContext& context)
	{
		Value* newVal = nullptr;

		Value* rhsVar = rhs->Evaluate(context);
		if (rhsVar == nullptr)
		{
			return nullptr;
		}

		if (lhs == nullptr)
		{
			switch (rhsVar->type)
			{
			case ValueType::INT_RAW:
				newVal = EvaluateUnaryOperation(&rhsVar->val.intRaw, op);
				break;
			case ValueType::FLOAT_RAW:
				newVal = EvaluateUnaryOperation(&rhsVar->val.floatRaw, op);
				break;
			case ValueType::BOOL_RAW:
				context.errorReason = "Only unary operation currently supported is negation, which is invalid on type bool";
				context.errorToken = rhs->token;
				return nullptr;
				//newVal = EvaluateUnaryOperation(&rhsVar->val.boolRaw, op);
				//break;
			default:
				context.errorReason = "Unexpected type name in operation";
				context.errorToken = rhs->token;
				break;
			}

			if (rhsVar->bIsTemporary)
			{
				delete rhsVar;
				rhsVar = nullptr;
			}

			if (newVal == nullptr)
			{
				context.errorReason = "Malformed unary operation";
				context.errorToken = rhs->token;
				return nullptr;
			}

			return newVal;
		}

		Value* lhsVar = lhs->Evaluate(context);

		// TODO: Handle implicit conversions
		if (lhsVar->type != rhsVar->type)
		{
			context.errorReason = "Operation on different types";
			context.errorToken = lhs->token;
			if (lhsVar->bIsTemporary)
			{
				delete lhsVar;
				lhsVar = nullptr;
			}
			if (rhsVar->bIsTemporary)
			{
				delete rhsVar;
			}
			return nullptr;
		}

		switch (lhsVar->type)
		{
		case ValueType::INT_RAW:
			newVal = EvaluateOperation(&lhsVar->val.intRaw, &rhsVar->val.intRaw, op);
			break;
		case ValueType::FLOAT_RAW:
			newVal = EvaluateOperation(&lhsVar->val.floatRaw, &rhsVar->val.floatRaw, op);
			break;
		case ValueType::BOOL_RAW:
			newVal = EvaluateOperation(&lhsVar->val.boolRaw, &rhsVar->val.boolRaw, op);
			break;
		default:
			context.errorReason = "Unexpected type name in operation";
			context.errorToken = lhs->token;
			break;
		}

		if (lhsVar->bIsTemporary)
		{
			delete lhsVar;
			lhsVar = nullptr;
		}
		if (rhsVar->bIsTemporary)
		{
			delete rhsVar;
			rhsVar = nullptr;
		}

		if (newVal == nullptr)
		{
			context.errorReason = "Malformed operation";
			context.errorToken = token;
			return nullptr;
		}

		return newVal;
	}

	Operation* Operation::Parse(Tokenizer& tokenizer)
	{
		Token token = tokenizer.GetNextToken();

		Expression* lhs = Expression::Parse(tokenizer);
		if (lhs == nullptr)
		{
			return nullptr;
		}
		OperatorType op = Operator::Parse(tokenizer);
		if (op == OperatorType::_NONE)
		{
			delete lhs;
			return nullptr;
		}
		Expression* rhs = Expression::Parse(tokenizer);
		if (rhs == nullptr)
		{
			delete lhs;
			return nullptr;
		}
		return new Operation(token, lhs, op, rhs);
	}

	Value::Value() :
		type(ValueType::NONE),
		bIsTemporary(true),
		val()
	{
	}

	Value::Value(Operation* opearation) :
		type(ValueType::OPERATION),
		bIsTemporary(false),
		val(opearation)
	{
	}

	Value::Value(Identifier* identifier) :
		type(ValueType::IDENTIFIER),
		bIsTemporary(false),
		val(identifier)
	{
	}

	Value::Value(i32 intRaw, bool bTemporary) :
		type(ValueType::INT_RAW),
		bIsTemporary(bTemporary),
		val(intRaw)
	{
	}

	Value::Value(real floatRaw, bool bTemporary) :
		type(ValueType::FLOAT_RAW),
		bIsTemporary(bTemporary),
		val(floatRaw)
	{
	}

	Value::Value(bool boolRaw, bool bTemporary) :
		type(ValueType::BOOL_RAW),
		bIsTemporary(bTemporary),
		val(boolRaw)
	{
	}

	Value::~Value()
	{
		switch (type)
		{
		case ValueType::INT_RAW:
		case ValueType::FLOAT_RAW:
		case ValueType::BOOL_RAW:
			// No memory to free
			break;
		case ValueType::IDENTIFIER:
			delete val.identifier;
			val.identifier = nullptr;
			break;
		case ValueType::OPERATION:
			delete val.operation;
			val.operation = nullptr;
			break;
		default:
			PrintError("Unhandled statement type in ~Value(): %d\n", (i32)type);
			break;
		}
	}

	std::string Value::ToString() const
	{
		switch (type)
		{
		case ValueType::INT_RAW: return IntToString(val.intRaw);
		case ValueType::FLOAT_RAW: return FloatToString(val.floatRaw, 2);
		case ValueType::BOOL_RAW: return BoolToString(val.boolRaw);
		default: return EMPTY_STRING;
		}
	}

	Expression::Expression(const Token& token, Operation* operation) :
		Node(token),
		value(operation)
	{
	}

	Expression::Expression(const Token& token, Identifier* identifier) :
		Node(token),
		value(identifier)
	{
	}

	Expression::Expression(const Token& token, i32 intRaw) :
		Node(token),
		value(intRaw, false)
	{
	}

	Expression::Expression(const Token& token, real floatRaw) :
		Node(token),
		value(floatRaw, false)
	{
	}

	Expression::Expression(const Token& token, bool boolRaw) :
		Node(token),
		value(boolRaw, false)
	{
	}

	Expression::~Expression()
	{
	}

	Value* Expression::Evaluate(TokenContext& context)
	{
		switch (value.type)
		{
		case ValueType::OPERATION:
			return value.val.operation->Evaluate(context);
		case ValueType::IDENTIFIER:
			// TODO: Apply implicit casting here when necessary
			return context.GetVarInstanceFromToken(token);
		case ValueType::INT_RAW:
		case ValueType::FLOAT_RAW:
		case ValueType::BOOL_RAW:
			return &value;
		}

		context.errorReason = "Unexpected value type";
		context.errorToken = token;
		return nullptr;
	}

	template<class T>
	bool CompareExpression(T* lhs, T* rhs, OperatorType op, TokenContext& context)
	{
		switch (op)
		{
		case OperatorType::EQUAL:			return *lhs == *rhs;
		case OperatorType::NOT_EQUAL:		return *lhs != *rhs;
		case OperatorType::GREATER:			return *lhs > *rhs;
		case OperatorType::GREATER_EQUAL:	return *lhs >= *rhs;
		case OperatorType::LESS:			return *lhs < *rhs;
		case OperatorType::LESS_EQUAL:		return *lhs <= *rhs;
		case OperatorType::BOOLEAN_AND:		return *lhs && *rhs;
		case OperatorType::BOOLEAN_OR:		return *lhs || *rhs;
		default:
			context.errorReason = "Unexpected operator on int in expression";
			context.errorToken = token;
		}
	}

	//bool Expression::Compare(TokenContext& context, Expression* other, OperatorType op)
	//{
		//if (value.type == ValueType::IDENTIFIER)
		//{

		//	context.GetVarInstanceFromToken(token)->val.identifier->;
		//}

		//if (value.type == ValueType::OPERATION)
		//{
		//	Value* newVal = value.val.operation->Evaluate(context);
		//	if (newVal->type == ValueType::BOOL_RAW)
		//	{
		//		bool bResult = newVal->val.boolRaw;
		//		if (newVal->bIsTemporary)
		//		{
		//			delete newVal);
		//		}
		//		return bResult;
		//	}
		//	else
		//	{
		//		context.errorReason = "Operation expression didn't evaluate to bool value";
		//		context.errorToken = token;
		//		return nullptr;
		//	}
		//}

	//	return false;
	//}

	Expression* Expression::Parse(Tokenizer& tokenizer)
	{
		Token token = tokenizer.PeekNextToken();
		if (token.type == TokenType::SUBTRACT)
		{
			tokenizer.GetNextToken();
			return new Expression(token, new Operation(token, nullptr, OperatorType::NEGATE, Expression::Parse(tokenizer)));
		}

		if (!tokenizer.context->HasNextChar())
		{
			return nullptr;
		}

		if (token.type == TokenType::IDENTIFIER)
		{
			Identifier* identifier = Identifier::Parse(tokenizer, TypeName::_NONE);
			if (identifier == nullptr)
			{
				return nullptr;
			}

			Token nextToken = tokenizer.PeekNextToken();
			if (nextToken.type == TokenType::SEMICOLON)
			{
				return new Expression(token, identifier);
			}
			else
			{
				OperatorType op = Operator::Parse(tokenizer);
				if (op == OperatorType::_NONE)
				{
					tokenizer.context->errorReason = "Expected operator";
					tokenizer.context->errorToken = token;
					delete identifier;
					return nullptr;
				}
				else
				{
					Expression* rhs = Expression::Parse(tokenizer);
					if (rhs == nullptr)
					{
						delete identifier;
						return nullptr;
					}
					Expression* lhs = new Expression(token, identifier);
					Operation* operation = new Operation(token, lhs, op, rhs);
					if (operation == nullptr)
					{
						delete identifier;
						delete lhs;
						delete rhs;
						return nullptr;
					}
					return new Expression(token, operation);
				}
			}
		}
		if (token.type == TokenType::INT_LITERAL)
		{
			i32 intRaw = ParseInt(tokenizer.GetNextToken().ToString());

			Token nextToken = tokenizer.PeekNextToken();
			if (nextToken.type == TokenType::SEMICOLON)
			{
				// TODO: Check able to be ended here
				tokenizer.GetNextToken();
				return new Expression(token, intRaw);
			}
			else
			{
				OperatorType op = OperatorType::_NONE;
				if (ExpectOperator(tokenizer, token, &op))
				{
					if (!ValidOperatorOnType(op, TypeName::INT))
					{
						tokenizer.context->errorReason = "Invalid operator on type int";
						tokenizer.context->errorToken = token;
						return nullptr;
					}
					Expression* rhs = Expression::Parse(tokenizer);
					if (rhs == nullptr)
					{
						return nullptr;
					}
					Expression* lhs = new Expression(token, intRaw);
					Operation* operation = new Operation(token, lhs, op, rhs);
					return new Expression(token, operation);
				}
				else
				{
					return nullptr;
				}
			}
		}
		if (token.type == TokenType::FLOAT_LITERAL)
		{
			real floatRaw = ParseFloat(tokenizer.GetNextToken().ToString());

			Token nextToken = tokenizer.PeekNextToken();
			if (nextToken.type == TokenType::SEMICOLON)
			{
				// TODO: Check able to be ended here
				tokenizer.GetNextToken();
				return new Expression(token, floatRaw);
			}
			else
			{
				OperatorType op = OperatorType::_NONE;
				if (ExpectOperator(tokenizer, token, &op))
				{
					if (!ValidOperatorOnType(op, TypeName::FLOAT))
					{
						tokenizer.context->errorReason = "Invalid operator on type float";
						tokenizer.context->errorToken = token;
						return nullptr;
					}
					Expression* rhs = Expression::Parse(tokenizer);
					if (rhs == nullptr)
					{
						return nullptr;
					}
					Expression* lhs = new Expression(token, floatRaw);
					Operation* operation = new Operation(token, lhs, op, rhs);
					return new Expression(token, operation);
				}
				else
				{
					return nullptr;
				}
			}
		}
		if (token.type == TokenType::KEY_TRUE || token.type == TokenType::KEY_FALSE)
		{
			bool boolRaw = ParseBool(tokenizer.GetNextToken().ToString());

			Token nextToken = tokenizer.PeekNextToken();
			if (nextToken.type == TokenType::CLOSE_PAREN)
			{
				return new Expression(token, boolRaw);
			}
			if (nextToken.type == TokenType::SEMICOLON)
			{
				// TODO: Check able to be ended here
				tokenizer.GetNextToken();
				return new Expression(token, boolRaw);
			}
			else
			{
				OperatorType op = OperatorType::_NONE;
				if (ExpectOperator(tokenizer, token, &op))
				{
					if (!ValidOperatorOnType(op, TypeName::BOOL))
					{
						tokenizer.context->errorReason = "Invalid operator on type bool";
						tokenizer.context->errorToken = token;
						return nullptr;
					}
					Expression* rhs = Expression::Parse(tokenizer);
					if (rhs == nullptr)
					{
						return nullptr;
					}
					Expression* lhs = new Expression(token, boolRaw);
					Operation* operation = new Operation(token, lhs, op, rhs);
					return new Expression(token, operation);
				}
				else
				{
					return nullptr;
				}
			}
		}

		TypeName typeName = Type::Parse(tokenizer);
		if (typeName != TypeName::_NONE)
		{
			Identifier* identifier = Identifier::Parse(tokenizer, typeName);
			if (identifier == nullptr)
			{
				tokenizer.context->errorReason = "Unexpected identifier after typename";
				tokenizer.context->errorToken = token;
				return nullptr;
			}
			return new Expression(token, identifier);
		}

		tokenizer.context->errorReason = "Unexpected expression type";
		tokenizer.context->errorToken = token;
		return nullptr;
	}

	bool Expression::ExpectOperator(Tokenizer &tokenizer, Token token, OperatorType* outOp)
	{
		*outOp = Operator::Parse(tokenizer);
		if (*outOp == OperatorType::_NONE)
		{
			tokenizer.context->errorToken = token;
			tokenizer.context->errorReason = "Expected '=' or ';' after int declaration";
			return false;
		}
		return true;
	}

	Assignment::Assignment(const Token& token,
		Identifier* identifier,
		Expression* rhs,
		TypeName typeName /* = TypeName::_NONE */) :
		Node(token),
		identifier(identifier),
		rhs(rhs),
		typeName(typeName)
	{
	}

	Assignment::~Assignment()
	{
		delete identifier;
		identifier = nullptr;
		delete rhs;
		rhs = nullptr;
	}

	void Assignment::Evaluate(TokenContext& context)
	{
		TypeName varTypeName = TypeName::_NONE;
		void* varVal = nullptr;
		{
			Value* var = nullptr;
			if (typeName == TypeName::_NONE)
			{
				var = context.GetVarInstanceFromToken(identifier->token);
				if (var == nullptr)
				{
					return;
				}
				varTypeName = var->val.identifier->type;
			}
			else
			{
				var = context.InstantiateIdentifier(identifier->token, typeName);
				if (var == nullptr)
				{
					return;
				}
				varTypeName = typeName;
			}

			if (var != nullptr)
			{
				switch (varTypeName)
				{
				case TypeName::INT:
					varVal = static_cast<void*>(&var->val.intRaw);
					break;
				case TypeName::FLOAT:
					varVal = static_cast<void*>(&var->val.floatRaw);
					break;
				case TypeName::BOOL:
					varVal = static_cast<void*>(&var->val.boolRaw);
					break;
				default:
					context.errorReason = "Unexpected variable type name";
					context.errorToken = token;
					return;
				}
			}
		}

		if (varVal == nullptr)
		{
			return;
		}

		if (rhs != nullptr)
		{
			switch (varTypeName)
			{
			case TypeName::INT:
			{
				Value* rhsVal = rhs->Evaluate(context);
				if (rhsVal == nullptr)
				{
					return;
				}
				if (rhsVal->type == ValueType::INT_RAW)
				{
					*static_cast<i32*>(varVal) = rhsVal->val.intRaw;
				}
				else if (rhsVal->type == ValueType::FLOAT_RAW)
				{
					*static_cast<i32*>(varVal) = (i32)rhsVal->val.floatRaw;
				}
				else
				{
					context.errorReason = "Invalid value type assigned to int";
					context.errorToken = token;
				}
				if (rhsVal->bIsTemporary)
				{
					delete rhsVal;
					rhsVal = nullptr;
				}
			} break;
			case TypeName::FLOAT:
			{
				Value* rhsVal = rhs->Evaluate(context);
				if (rhsVal == nullptr)
				{
					return;
				}
				if (rhsVal->type == ValueType::INT_RAW)
				{
					*static_cast<real*>(varVal) = (real)rhsVal->val.intRaw;
				}
				else if (rhsVal->type == ValueType::FLOAT_RAW)
				{
					*static_cast<real*>(varVal) = rhsVal->val.floatRaw;
				}
				else
				{
					context.errorReason = "Invalid value type assigned to float";
					context.errorToken = token;
				}
				if (rhsVal->bIsTemporary)
				{
					delete rhsVal;
					rhsVal = nullptr;
				}
			} break;
			case TypeName::BOOL:
			{
				Value* rhsVal = rhs->Evaluate(context);
				if (rhsVal == nullptr)
				{
					return;
				}
				if (rhsVal->type == ValueType::BOOL_RAW)
				{
					*static_cast<bool*>(varVal) = rhsVal->val.boolRaw;
				}
				else
				{
					context.errorReason = "Invalid value type assigned to bool";
					context.errorToken = token;
				}
				if (rhsVal->bIsTemporary)
				{
					delete rhsVal;
					rhsVal = nullptr;
				}
			} break;
			default:
			{
				context.errorReason = "Unexpected typename encountered in assignment";
				context.errorToken = token;
			} break;
			}
		}
	}

	Assignment* Assignment::Parse(Tokenizer& tokenizer)
	{
		Token token = tokenizer.PeekNextToken();

		TypeName typeName = TypeName::_NONE;
		if (Type::GetTypeNameFromStr(token.ToString()) != TypeName::_NONE)
		{
			typeName = Type::Parse(tokenizer);
		}

		Identifier* lhs = Identifier::Parse(tokenizer, typeName);
		if (lhs == nullptr)
		{
			return nullptr;
		}

		Token nextToken = tokenizer.GetNextToken();
		if (nextToken.type == TokenType::SEMICOLON)
		{
			delete lhs;
			tokenizer.context->errorReason = "Uninitialized variables are not supported. Add default value";
			tokenizer.context->errorToken = token;
			return nullptr;
		}
		if (nextToken.type != TokenType::ASSIGNMENT)
		{
			delete lhs;
			tokenizer.context->errorReason = "Expected '=' after identifier";
			tokenizer.context->errorToken = token;
			return nullptr;
		}

		Expression* rhs = Expression::Parse(tokenizer);
		if (rhs == nullptr)
		{
			delete lhs;
			return nullptr;
		}
		return new Assignment(token, lhs, rhs, typeName);
	}

	Statement::Statement(const Token& token,
		Assignment* assignment) :
		Node(token),
		type(StatementType::ASSIGNMENT),
		contents(assignment)
	{
	}

	Statement::Statement(const Token& token,
		StatementType type,
		IfStatement* ifStatement) :
		Node(token),
		type(type),
		contents(ifStatement)
	{
		assert(type == StatementType::IF || type == StatementType::ELIF);
	}

	Statement::Statement(const Token& token,
		Statement* elseStatement) :
		Node(token),
		type(StatementType::ELSE),
		contents(elseStatement)
	{
	}

	Statement::Statement(const Token& token,
		WhileStatement* whileStatement) :
		Node(token),
		type(StatementType::WHILE),
		contents(whileStatement)
	{
	}

	Statement::~Statement()
	{
		switch (type)
		{
		case StatementType::ASSIGNMENT:
			delete contents.assignment;
			contents.assignment = nullptr;
			break;
		case StatementType::IF:
		case StatementType::ELIF:
			delete contents.ifStatement;
			contents.ifStatement = nullptr;
			break;
		case StatementType::ELSE:
			delete contents.elseStatement;
			contents.elseStatement = nullptr;
			break;
		case StatementType::WHILE:
			delete contents.whileStatement;
			contents.whileStatement = nullptr;
			break;
		default:
			PrintError("Unhandled statement type in ~Statement(): %d\n", (i32)type);
			break;
		}
	}

	void Statement::Evaluate(TokenContext& context)
	{
		switch (type)
		{
		case StatementType::ASSIGNMENT:
			contents.assignment->Evaluate(context);
			break;
		case StatementType::IF:
			contents.ifStatement->Evaluate(context);
			break;
		case StatementType::ELIF:
		case StatementType::ELSE:
			assert(false);
			break;
		case StatementType::WHILE:
			contents.whileStatement->Evaluate(context);
		default:
			break;
		}
	}

	Statement* Statement::Parse(Tokenizer& tokenizer)
	{
		StatementType statementType = StatementType::_NONE;

		IfStatement* ifStatement = nullptr;
		Statement* elseStatement = nullptr;
		WhileStatement* whileStatement = nullptr;
		Assignment* assignmentStatement = nullptr;

		Token token = tokenizer.PeekNextToken();

		if (token.len == 0)
		{
			// Likely reached EOF
			return nullptr;
		}

		if (token.type == TokenType::SEMICOLON)
		{
			// Empty statement
			return nullptr;
		}
		if (token.type == TokenType::KEY_IF)
		{
			statementType = StatementType::IF;
			ifStatement = IfStatement::Parse(tokenizer);
			if (ifStatement == nullptr)
			{
				return nullptr;
			}
		}
		else if (token.type == TokenType::KEY_ELIF)
		{
			// elif statements should only be parsed in IfStatement::Parse
			tokenizer.context->errorReason = "Found elif with no matching if";
			tokenizer.context->errorToken = token;
		}
		else if (token.type == TokenType::KEY_ELSE)
		{
			tokenizer.GetNextToken(); // Consume 'else'

			{
				Token nextToken = tokenizer.GetNextToken();
				if (nextToken.type != TokenType::OPEN_BRACKET)
				{
					tokenizer.context->errorReason = "Expected '{' after else";
					tokenizer.context->errorToken = token;
					return nullptr;
				}
			}

			statementType = StatementType::ELSE;
			elseStatement = Statement::Parse(tokenizer);
			if (elseStatement == nullptr)
			{
				return nullptr;
			}

			{
				Token nextToken = tokenizer.GetNextToken();
				if (nextToken.type != TokenType::CLOSE_BRACKET)
				{
					tokenizer.context->errorReason = "Expected '}' after else body";
					tokenizer.context->errorToken = token;
					return nullptr;
				}
			}

		}
		else if (token.type == TokenType::KEY_WHILE)
		{
			statementType = StatementType::WHILE;
			whileStatement = WhileStatement::Parse(tokenizer);
			if (whileStatement == nullptr)
			{
				return nullptr;
			}
		}
		else
		{
			// Assigning to existing instance of var
			statementType = StatementType::ASSIGNMENT;
			assignmentStatement = Assignment::Parse(tokenizer);
			if (assignmentStatement == nullptr)
			{
				return nullptr;
			}
		}

		if (statementType == StatementType::_NONE)
		{
			if (tokenizer.context->errorReason.empty())
			{
				tokenizer.context->errorReason = "Expected statement";
				tokenizer.context->errorToken = token;
			}
			return nullptr;
		}

		Token nextToken = tokenizer.PeekNextToken();
		if (nextToken.type == TokenType::SEMICOLON)
		{
			tokenizer.GetNextToken();
		}

		switch (statementType)
		{
		case StatementType::ASSIGNMENT:
		{
			assert(assignmentStatement != nullptr);
			return new Statement(token, assignmentStatement);
		} break;
		case StatementType::IF:
		{
			assert(ifStatement != nullptr);
			return new Statement(token, statementType, ifStatement);
		} break;
		case StatementType::ELIF:
		{
			assert(false);
			return nullptr;
		} break;
		case StatementType::ELSE:
		{
			assert(elseStatement != nullptr);
			return new Statement(token, elseStatement);
		} break;
		case StatementType::WHILE:
		{
			assert(whileStatement != nullptr);
			return new Statement(token, whileStatement);
		} break;
		}

		tokenizer.context->errorReason = "Expected statement";
		tokenizer.context->errorToken = token;
		return nullptr;
	}

	IfStatement::IfStatement(const Token& token,
		Expression* condition,
		Statement* body,
		IfStatement* elseIfStatement) :
		Node(token),
		ifFalseAction(IfFalseAction::ELIF),
		condition(condition),
		body(body),
		ifFalseStatement(elseIfStatement)
	{
	}

	IfStatement::IfStatement(const Token& token,
		Expression* condition,
		Statement* body,
		Statement* elseStatement) :
		Node(token),
		ifFalseAction(IfFalseAction::ELSE),
		condition(condition),
		body(body),
		ifFalseStatement(elseStatement)
	{
	}

	IfStatement::IfStatement(const Token& token,
		Expression* condition,
		Statement* body) :
		Node(token),
		ifFalseAction(IfFalseAction::NONE),
		condition(condition),
		body(body),
		ifFalseStatement()
	{
	}

	IfStatement::~IfStatement()
	{
		delete condition;
		condition = nullptr;
		delete body;
		body = nullptr;
	}

	void IfStatement::Evaluate(TokenContext& context)
	{
		Value* bConditionResult = condition->Evaluate(context);
		if (bConditionResult->val.boolRaw)
		{
			body->Evaluate(context);
		}
		else
		{
			switch (ifFalseAction)
			{
			case IfFalseAction::NONE:
				break;
			case IfFalseAction::ELSE:
				ifFalseStatement.elseStatement->Evaluate(context);
				break;
			case IfFalseAction::ELIF:
				ifFalseStatement.elseIfStatement->Evaluate(context);
				break;
			default:
				context.errorReason = "Unhandled if false action";
				context.errorToken = token;
				break;
			}
		}

		if (bConditionResult->bIsTemporary)
		{
			delete bConditionResult;
		}
	}

	IfStatement* IfStatement::Parse(Tokenizer& tokenizer)
	{
		Token token = tokenizer.GetNextToken();
		if (token.type != TokenType::KEY_IF && token.type != TokenType::KEY_ELIF)
		{
			tokenizer.context->errorReason = "Expected if or elif";
			tokenizer.context->errorToken = token;
			return nullptr;
		}

		{
			Token nextToken = tokenizer.GetNextToken();
			if (nextToken.type != TokenType::OPEN_PAREN)
			{
				tokenizer.context->errorReason = "Expected '(' after if";
				tokenizer.context->errorToken = token;
				return nullptr;
			}
		}

		Expression* condition = Expression::Parse(tokenizer);
		if (condition == nullptr)
		{
			return nullptr;
		}

		{
			Token nextToken = tokenizer.GetNextToken();
			if (nextToken.type != TokenType::CLOSE_PAREN)
			{
				tokenizer.context->errorReason = "Expected ')' after if statement condition";
				tokenizer.context->errorToken = token;
				return nullptr;
			}
		}

		{
			Token nextToken = tokenizer.GetNextToken();
			if (nextToken.type != TokenType::OPEN_BRACKET)
			{
				tokenizer.context->errorReason = "Expected '{' after if statement condition";
				tokenizer.context->errorToken = token;
				delete condition;
				return nullptr;
			}
		}

		Statement* body = Statement::Parse(tokenizer);
		if (body == nullptr)
		{
			return nullptr;
		}

		{
			Token nextToken = tokenizer.GetNextToken();
			if (nextToken.type != TokenType::CLOSE_BRACKET)
			{
				tokenizer.context->errorReason = "Expected '}' after if statement body";
				tokenizer.context->errorToken = token;
				delete condition;
				delete body;
				return nullptr;
			}
		}

		IfStatement* elseIfStatement = nullptr;
		Statement* elseStatement = nullptr;

		Token nextToken = tokenizer.PeekNextToken();
		if (nextToken.type == TokenType::KEY_ELIF)
		{
			elseIfStatement = IfStatement::Parse(tokenizer);
			if (elseIfStatement == nullptr)
			{
				return nullptr;
			}
		}
		else if (nextToken.type == TokenType::KEY_ELSE)
		{
			elseStatement = Statement::Parse(tokenizer);
			if (elseStatement == nullptr)
			{
				return nullptr;
			}
		}

		if (elseIfStatement != nullptr)
		{
			return new IfStatement(token, condition, body, elseIfStatement);
		}
		else if (elseStatement != nullptr)
		{
			return new IfStatement(token, condition, body, elseStatement);
		}
		else
		{
			return new IfStatement(token, condition, body);
		}
	}

	WhileStatement::WhileStatement(const Token& token, Expression* condition, Statement* body) :
		Node(token),
		condition(condition),
		body(body)
	{
	}

	WhileStatement::~WhileStatement()
	{
		delete condition;
		condition = nullptr;
		delete body;
		body = nullptr;
	}

	void WhileStatement::Evaluate(TokenContext& context)
	{
		Value* result = condition->Evaluate(context);
		i32 iterationCount = 0;
		while (result->val.boolRaw)
		{
			body->Evaluate(context);
			Value* pResult = result;
			result = condition->Evaluate(context);

			assert(pResult->bIsTemporary);
			delete pResult;

			if (result == nullptr)
			{
				return;
			}

			if (++iterationCount > 4'194'303)
			{
				context.errorReason = "Exceeded max number of iterations in while loop";
				context.errorToken = token;
				return;
			}
		}
		assert(result->bIsTemporary);
		delete result;
	}

	WhileStatement* WhileStatement::Parse(Tokenizer& tokenizer)
	{
		Token token = tokenizer.GetNextToken();
		if (token.type != TokenType::KEY_WHILE)
		{
			tokenizer.context->errorReason = "Expected while";
			tokenizer.context->errorToken = token;
			return nullptr;
		}

		Expression* condition = Expression::Parse(tokenizer);
		if (condition == nullptr)
		{
			return nullptr;
		}

		// TODO: Handle single line while loops
		{
			Token nextToken = tokenizer.GetNextToken();
			if (nextToken.type != TokenType::OPEN_BRACKET)
			{
				tokenizer.context->errorReason = "Expected '{' after while statement";
				tokenizer.context->errorToken = token;
				delete condition;
				return nullptr;
			}
		}

		Statement* body = Statement::Parse(tokenizer);
		if (body == nullptr)
		{
			return nullptr;
		}

		{
			Token nextToken = tokenizer.GetNextToken();
			if (nextToken.type != TokenType::CLOSE_BRACKET)
			{
				tokenizer.context->errorReason = "Expected '}' after while statement body";
				tokenizer.context->errorToken = token;
				delete condition;
				delete body;
				return nullptr;
			}
		}

		return new WhileStatement(token, condition, body);
	}

	RootItem::RootItem(Statement* statement, RootItem* nextItem) :
		statement(statement),
		nextItem(nextItem)
	{
	}

	RootItem::~RootItem()
	{
		delete statement;
		statement = nullptr;
		delete nextItem;
		nextItem = nullptr;
	}

	void RootItem::Evaluate(TokenContext& context)
	{
		statement->Evaluate(context);

		if (!context.errorReason.empty())
		{
			return;
		}

		if (nextItem)
		{
			nextItem->Evaluate(context);
		}
	}

	RootItem* RootItem::Parse(Tokenizer& tokenizer)
	{
		Statement* rootStatement = Statement::Parse(tokenizer);
		if (rootStatement == nullptr ||
			!tokenizer.context->errorReason.empty())
		{
			return nullptr;
		}

		RootItem* nextItem = nullptr;

		if (tokenizer.context->HasNextChar())
		{
			nextItem = RootItem::Parse(tokenizer);
			if (nextItem == nullptr ||
				!tokenizer.context->errorReason.empty())
			{
				delete rootStatement;
				return nullptr;
			}
		}

		return new RootItem(rootStatement, nextItem);
	}

	AST::AST(Tokenizer* tokenizer) :
		tokenizer(tokenizer)
	{
	}

	void AST::Destroy()
	{
		delete rootItem;
		rootItem = nullptr;
		bValid = false;
	}

	void AST::Generate()
	{
		delete rootItem;

		bValid = false;

		rootItem = RootItem::Parse(*tokenizer);

		if (!tokenizer->context->errorReason.empty())
		{
			PrintError("Creation of AST failed\n");
			PrintError("Error reason: %s\n", tokenizer->context->errorReason.c_str());
			PrintError("Error token: %s\n", tokenizer->context->errorToken.ToString().c_str());
			lastErrorTokenLocation = glm::vec2i(tokenizer->context->errorToken.linePos, tokenizer->context->errorToken.lineNum);
			tokenizer->context->errors = { Error(tokenizer->context->errorToken.lineNum, tokenizer->context->errorReason) };
			return;
		}

		bValid = true;

		lastErrorTokenLocation = glm::vec2i(-1);
	}

	void AST::Evaluate()
	{
		if (bValid == false)
		{
			return;
		}

		bool bSuccess = true;

		RootItem* currentItem = rootItem;
		while (currentItem != nullptr)
		{
			//Print("Type: %d\n", currentItem->statement->type);

			currentItem->statement->Evaluate(*tokenizer->context);
			if (tokenizer->context->errorReason.empty())
			{
				currentItem = currentItem->nextItem;
			}
			else
			{
				PrintError("Evaluation of AST failed\n");
				PrintError("Error reason: %s\n", tokenizer->context->errorReason.c_str());
				PrintError("Error token: %s\n", tokenizer->context->errorToken.ToString().c_str());
				lastErrorTokenLocation = glm::vec2i(tokenizer->context->errorToken.linePos, tokenizer->context->errorToken.lineNum);
				bSuccess = false;
				tokenizer->context->errors = { Error(tokenizer->context->errorToken.lineNum, tokenizer->context->errorReason) };
				break;
			}
		}

		if (bSuccess)
		{
			lastErrorTokenLocation = glm::vec2i(-1);
		}
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
		if (!g_Renderer->GetMaterialID("Terminal Copper", matID))
		{
			// TODO: Create own material
			matID = 0;
		}
		MeshComponent* mesh = SetMeshComponent(new MeshComponent(matID, this));
		if (!mesh->LoadFromFile(RESOURCE("meshes/terminal-copper.glb")))
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

			const real width = 1.3f;
			const real height = 1.65f;
			const glm::vec3 posTL = m_Transform.GetWorldPosition() +
				right * (width / 2.0f) +
				up * height +
				forward * 0.992f;

			glm::vec3 pos = posTL;

			const glm::quat rot = m_Transform.GetWorldRotation();
			real charHeight = g_Renderer->GetStringHeight("W", font, false);
			// TODO: Get rid of magic numbers
			const real lineHeight = charHeight * (magicY/1000.0f); // fontSize / 215.0f;
			real charWidth = (magicX / 1000.0f) * g_Renderer->GetStringWidth("W", font, letterSpacing, false);
			const real lineNoWidth = 3.0f * charWidth;

			if (bRenderCursor)
			{
				// TODO: Get rid of magic numbers
				glm::vec3 cursorPos = pos;
				cursorPos += (-right * charWidth * (real)cursor.x) + up * (cursor.y * -lineHeight);
				g_Renderer->DrawStringWS("|", VEC4_ONE, cursorPos, rot, letterSpacing);
			}

			if (bRenderText)
			{
				static const glm::vec4 lineNumberColor(0.4f, 0.4f, 0.4f, 1.0f);
				static const glm::vec4 lineNumberColorActive(0.5f, 0.5f, 0.5f, 1.0f);
				static const glm::vec4 textColor(0.85f, 0.81f, 0.80f, 1.0f);
				static const glm::vec4 errorColor(0.84f, 0.25f, 0.25f, 1.0f);
				glm::vec3 firstLinePos = pos;
				for (i32 lineNumber = 0; lineNumber < (i32)lines.size(); ++lineNumber)
				{
					glm::vec4 lineNoCol = (lineNumber == cursor.y ? lineNumberColorActive : lineNumberColor);
					g_Renderer->DrawStringWS(IntToString(lineNumber + 1, 2, ' '), lineNoCol, pos + right * lineNoWidth, rot, letterSpacing);
					g_Renderer->DrawStringWS(lines[lineNumber], textColor, pos, rot, letterSpacing);
					pos.y -= lineHeight;
				}

				if (ast != nullptr)
				{
					glm::vec2i lastErrorPos = ast->lastErrorTokenLocation;
					if (lastErrorPos.x != -1)
					{
						pos = firstLinePos;
						pos.y -= lineHeight * lastErrorPos.y;
						g_Renderer->DrawStringWS("!", errorColor, pos - right * charWidth, rot, letterSpacing);
					}
				}
			}
		}
	}

	void Terminal::DrawImGuiObjects()
	{
		GameObject::DrawImGuiObjects();

		ImGui::Begin("Terminal");
		{
			ImGui::DragFloat("Magic X", &magicX, 0.01f);
			ImGui::DragFloat("Magic Y", &magicY, 0.01f);

			ImGui::Text("Variables");
			if (ImGui::BeginChild("Variables", ImVec2(0.0f, 220.0f), true))
			{
				for (i32 i = 0; i < tokenizer->context->variableCount; ++i)
				{
					const TokenContext::InstantiatedIdentifier& var = tokenizer->context->instantiatedIdentifiers[i];
					std::string valStr = var.value->ToString();
					const char* typeNameCStr = g_TypeNameStrings[(i32)ValueTypeToTypeName(var.value->type)];
					ImGui::Text("%s %s = %s", typeNameCStr, var.name.c_str(), valStr.c_str());
				}
			}
			ImGui::EndChild();

			ImGui::Text("Errors: %d", tokenizer->context->errors.size());
			{
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
						ImGui::Text("%2d, %s", e.lineNumber, e.str.c_str());
					}
					ImGui::PopStyleColor();
				}
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

	void Terminal::ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID)
	{
		UNREFERENCED_PARAMETER(scene);
		UNREFERENCED_PARAMETER(matID);

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
			if (cursor.x == 0  && cursor.y > 0)
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
		bParsePassed = true;

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

} // namespace flex
