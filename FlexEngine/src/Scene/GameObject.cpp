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
#include "Scene/BaseScene.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

namespace flex
{
	const char* GameObject::s_DefaultNewGameObjectName = "New_Game_Object_00";

	const char* Cart::emptyCartMeshName = "cart-empty.glb";
	const char* EngineCart::engineMeshName = "cart-engine.glb";

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
				int mask = rigidBodyObj.GetInt("mask");
				int group = rigidBodyObj.GetInt("group");

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

		VertexAttributes requiredVertexAttributes = 0;
		if (matID != InvalidMaterialID)
		{
			Material& material = g_Renderer->GetMaterial(matID);
			Shader& shader = g_Renderer->GetShader(material.shaderID);
			requiredVertexAttributes = shader.vertexAttributes;
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

			int shapeType = collisionShape->getShapeType();
			std::string shapeTypeStr = CollisionShapeTypeToString(shapeType);
			colliderObj.fields.emplace_back("shape", JSONValue(shapeTypeStr));

			switch (shapeType)
			{
			case BOX_SHAPE_PROXYTYPE:
			{
				btVector3 btHalfExtents = ((btBoxShape*)collisionShape)->getHalfExtentsWithMargin();
				glm::vec3 halfExtents = ToVec3(btHalfExtents);
				halfExtents /= m_Transform.GetWorldScale();
				std::string halfExtentsStr = Vec3ToString(halfExtents, 3);
				colliderObj.fields.emplace_back("half extents", JSONValue(halfExtentsStr));
			} break;
			case SPHERE_SHAPE_PROXYTYPE:
			{
				real radius = ((btSphereShape*)collisionShape)->getRadius();
				radius /= m_Transform.GetWorldScale().x;
				colliderObj.fields.emplace_back("radius", JSONValue(radius));
			} break;
			case CAPSULE_SHAPE_PROXYTYPE:
			{
				real radius = ((btCapsuleShapeZ*)collisionShape)->getRadius();
				real height = ((btCapsuleShapeZ*)collisionShape)->getHalfHeight() * 2.0f;
				radius /= m_Transform.GetWorldScale().x;
				height /= m_Transform.GetWorldScale().x;
				colliderObj.fields.emplace_back("radius", JSONValue(radius));
				colliderObj.fields.emplace_back("height", JSONValue(height));
			} break;
			case CONE_SHAPE_PROXYTYPE:
			{
				real radius = ((btConeShape*)collisionShape)->getRadius();
				real height = ((btConeShape*)collisionShape)->getHeight();
				radius /= m_Transform.GetWorldScale().x;
				height /= m_Transform.GetWorldScale().x;
				colliderObj.fields.emplace_back("radius", JSONValue(radius));
				colliderObj.fields.emplace_back("height", JSONValue(height));
			} break;
			case CYLINDER_SHAPE_PROXYTYPE:
			{
				btVector3 btHalfExtents = ((btCylinderShape*)collisionShape)->getHalfExtentsWithMargin();
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
				int mask = m_RigidBody->GetMask();
				int group = m_RigidBody->GetMask();

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
			SafeDelete(child);
		}
		m_Children.clear();

		if (m_MeshComponent)
		{
			m_MeshComponent->Destroy();
			SafeDelete(m_MeshComponent);
		}

		if (m_RenderID != InvalidRenderID)
		{
			g_Renderer->DestroyRenderObject(m_RenderID);
			m_RenderID = InvalidRenderID;
		}

		if (m_RigidBody)
		{
			m_RigidBody->Destroy();
			SafeDelete(m_RigidBody);
		}

		// NOTE: SafeDelete does not work on this type
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
		ImGui::Checkbox(objectVisibleLabel.c_str(), &m_bVisible);

		ImGui::Checkbox("Static", &m_bStatic);

		ImGui::Text("Transform");
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

			if (ImGui::Checkbox("u", &m_bUniformScale))
			{
				valueChanged = true;
			}
			if (m_bUniformScale)
			{
				float newScale = scale.x;
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
			g_Renderer->DrawImGuiForRenderID(m_RenderID);
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
				bool bStatic = m_RigidBody->IsStatic();
				if (ImGui::Checkbox("Static##rb", &bStatic))
				{
					m_RigidBody->SetStatic(bStatic);
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
					btBoxShape* boxShape = (btBoxShape*)shape;
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
					btSphereShape* sphereShape = (btSphereShape*)shape;
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
					btCapsuleShapeZ* capsuleShape = (btCapsuleShapeZ*)shape;
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
					btCylinderShape* cylinderShape = (btCylinderShape*)shape;
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
		static const char* renameObjectPopupLabel = "##rename-game-object";
		static const char* renameObjectButtonStr = "Rename";
		static const char* duplicateObjectButtonStr = "Duplicate...";
		static const char* deleteButtonStr = "Delete";

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
			bool bRename = ImGui::InputText(renameObjectPopupLabel,
				(char*)newObjectName.data(),
				maxStrLen,
				ImGuiInputTextFlags_EnterReturnsTrue);

			ImGui::SameLine();

			bRename |= ImGui::Button(renameObjectButtonStr);

			bool bInvalidName = std::string(newObjectName.c_str()).empty();

			if (bRename && !bInvalidName)
			{
				// Remove excess trailing \0 chars
				newObjectName = std::string(newObjectName.c_str());

				m_Name = newObjectName;

				ImGui::CloseCurrentPopup();
			}

			if (DoDuplicateGameObjectButton(duplicateObjectButtonStr))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button(deleteButtonStr))
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
				// NOTE: SafeDelete does not work on this type
				if (m_CollisionShape)
				{
					delete m_CollisionShape;
					m_CollisionShape = nullptr;
				}
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
				newMeshComponent->LoadPrefabShape(shape, &createInfo);
			}
			else if (prefabType == MeshComponent::Type::FILE)
			{
				std::string filePath = m_MeshComponent->GetRelativeFilePath();
				MeshImportSettings importSettings = m_MeshComponent->GetImportSettings();
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
				btVector3 btHalfExtents = ((btBoxShape*)pCollisionShape)->getHalfExtentsWithMargin();
				btHalfExtents = btHalfExtents / btWorldScale;
				newCollisionShape = new btBoxShape(btHalfExtents);
			} break;
			case SPHERE_SHAPE_PROXYTYPE:
			{
				real radius = ((btSphereShape*)pCollisionShape)->getRadius();
				radius /= btWorldScaleX;
				newCollisionShape = new btSphereShape(radius);
			} break;
			case CAPSULE_SHAPE_PROXYTYPE:
			{
				real radius = ((btCapsuleShapeZ*)pCollisionShape)->getRadius();
				real height = ((btCapsuleShapeZ*)pCollisionShape)->getHalfHeight() * 2.0f;
				radius /= btWorldScaleX;
				height /= btWorldScaleX;
				newCollisionShape = new btCapsuleShapeZ(radius, height);
			} break;
			case CONE_SHAPE_PROXYTYPE:
			{
				real radius = ((btConeShape*)pCollisionShape)->getRadius();
				real height = ((btConeShape*)pCollisionShape)->getHeight();
				radius /= btWorldScaleX;
				height /= btWorldScaleX;
				newCollisionShape = new btConeShape(radius, height);
			} break;
			case CYLINDER_SHAPE_PROXYTYPE:
			{
				btVector3 btHalfExtents = ((btCylinderShape*)pCollisionShape)->getHalfExtentsWithMargin();
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

	void GameObject::SetVisible(bool bVisible, bool effectChildren)
	{
		if (m_bVisible != bVisible)
		{
			m_bVisible = bVisible;
			g_Renderer->RenderObjectStateChanged();

			if (effectChildren)
			{
				for (GameObject* child : m_Children)
				{
					child->SetVisible(bVisible, effectChildren);
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
		SafeDelete(m_RigidBody);

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
			SafeDelete(m_MeshComponent);
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
			i32 playerIndex = ((Player*)m_ObjectInteractingWith)->GetIndex();

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
					valve = (Valve*)rootObject;
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
			i32 playerIndex = ((Player*)valve->GetObjectInteractingWith())->GetIndex();
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

	ReflectionProbe::ReflectionProbe(const std::string& name) :
		GameObject(name, GameObjectType::REFLECTION_PROBE)
	{
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

		MeshComponent* sphereMesh = new MeshComponent(matID, this);

		assert(m_MeshComponent == nullptr);
		sphereMesh->LoadFromFile(RESOURCE_LOCATION  "meshes/ico-sphere.glb");
		SetMeshComponent(sphereMesh);

		std::string captureName = m_Name + "_capture";
		GameObject* captureObject = new GameObject(captureName, GameObjectType::_NONE);
		captureObject->SetSerializable(false);
		captureObject->SetVisible(false);
		captureObject->SetVisibleInSceneExplorer(false);

		RenderObjectCreateInfo captureObjectCreateInfo = {};
		captureObjectCreateInfo.vertexBufferData = nullptr;
		captureObjectCreateInfo.materialID = captureMatID;
		captureObjectCreateInfo.gameObject = captureObject;
		captureObjectCreateInfo.visibleInSceneExplorer = false;

		RenderID captureRenderID = g_Renderer->InitializeRenderObject(&captureObjectCreateInfo);
		captureObject->SetRenderID(captureRenderID);

		AddChild(captureObject);

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
		RenderObjectCreateInfo createInfo = {};
		createInfo.cullFace = CullFace::_NONE;
		skyboxMesh->LoadPrefabShape(MeshComponent::PrefabShape::SKYBOX, &createInfo);
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
		GameObject("Directional Light", GameObjectType::DIRECTIONAL_LIGHT)
	{
	}

	DirectionalLight::DirectionalLight(const std::string& name) :
		GameObject(name, GameObjectType::DIRECTIONAL_LIGHT)
	{
	}

	void DirectionalLight::Initialize()
	{
		g_Renderer->RegisterDirectionalLight(this);

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
			ImGui::Checkbox("Enabled", &m_bVisible);

			glm::vec3 pos = m_Transform.GetLocalPosition();
			if (ImGui::DragFloat3("Position", &pos.x, 0.1f))
			{
				m_Transform.SetLocalPosition(pos);
			}
			glm::vec3 dirtyRot = glm::degrees(glm::eulerAngles(m_Transform.GetLocalRotation()));
			glm::vec3 cleanedRot;
			if (DoImGuiRotationDragFloat3("Rotation", dirtyRot, cleanedRot))
			{
				m_Transform.SetLocalRotation(glm::quat(glm::radians(cleanedRot)));
			}
			ImGui::SliderFloat("Brightness", &brightness, 0.0f, 15.0f);
			ImGui::ColorEdit4("Color ", &color.r, colorEditFlags);

			ImGui::Spacing();
			ImGui::Text("Shadow");

			ImGui::Checkbox("Cast shadow", &bCastShadow);
			ImGui::SliderFloat("Shadow darkness", &shadowDarkness, 0.0f, 1.0f);

			ImGui::DragFloat("Near", &shadowMapNearPlane);
			ImGui::DragFloat("Far", &shadowMapFarPlane);
			ImGui::DragFloat("Zoom", &shadowMapZoom);

			if (ImGui::CollapsingHeader("Preview"))
			{
				ImGui::Image((void*)shadowTextureID, ImVec2(256, 256));
			}

			ImGui::TreePop();
		}
	}

	void DirectionalLight::SetPos(const glm::vec3& pos)
	{
		m_Transform.SetLocalPosition(pos);
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
			m_Transform.SetLocalRotation(glm::quat(ParseVec3(dirStr)));

			std::string posStr = directionalLightObj.GetString("pos");
			if (!posStr.empty())
			{
				m_Transform.SetLocalPosition(ParseVec3(posStr));
			}

			directionalLightObj.SetVec4Checked("color", color);

			directionalLightObj.SetFloatChecked("brightness", brightness);

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

		std::string colorStr = Vec3ToString(color, 2);
		dirLightObj.fields.emplace_back("color", JSONValue(colorStr));

		dirLightObj.fields.emplace_back("enabled", JSONValue(m_bVisible != 0));
		dirLightObj.fields.emplace_back("brightness", JSONValue(brightness));

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
			other.m_Transform.GetLocalPosition() == m_Transform.GetLocalPosition() &&
			other.color == color &&
			other.m_bVisible == m_bVisible &&
			other.brightness == brightness;
	}

	void DirectionalLight::SetRot(const glm::quat& rot)
	{
		m_Transform.SetLocalRotation(rot);
	}

	PointLight::PointLight(BaseScene* scene) :
		GameObject(scene->GetUniqueObjectName("PointLight_", 2), GameObjectType::POINT_LIGHT)
	{
	}

	PointLight::PointLight(const std::string& name) :
		GameObject(name, GameObjectType::POINT_LIGHT)
	{
	}

	void PointLight::Initialize()
	{
		g_Renderer->RegisterPointLight(this);

		GameObject::Initialize();
	}

	void PointLight::Destroy()
	{
		g_Renderer->RemovePointLight(this);

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

		if (ImGui::BeginPopupContextItem())
		{
			static const char* removePointLightStr = "Delete";
			if (ImGui::Button(removePointLightStr))
			{
				g_Renderer->RemovePointLight(this);
				bRemovedPointLight = true;
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		if (!bRemovedPointLight && bTreeOpen)
		{
			glm::vec3 pos = m_Transform.GetLocalPosition();
			if (ImGui::DragFloat3("Position", &pos.x, 0.1f))
			{
				m_Transform.SetLocalPosition(pos);
			}
			ImGui::ColorEdit4("Color ", &color.r, colorEditFlags);
			ImGui::SliderFloat("Brightness", &brightness, 0.0f, 1000.0f);
		}

		if (bTreeOpen)
		{
			ImGui::TreePop();
		}
	}

	void PointLight::SetPos(const glm::vec3& pos)
	{
		m_Transform.SetLocalPosition(pos);
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
			m_Transform.SetLocalPosition(glm::vec3(ParseVec3(posStr)));

			pointLightObj.SetVec4Checked("color", color);

			pointLightObj.SetFloatChecked("brightness", brightness);

			if (pointLightObj.HasField("enabled"))
			{
				m_bVisible = pointLightObj.GetBool("enabled") ? 1 : 0;
			}
		}
	}

	void PointLight::SerializeUniqueFields(JSONObject& parentObject) const
	{
		JSONObject pointLightObj = {};

		std::string posStr = Vec3ToString(m_Transform.GetLocalPosition(), 3);
		pointLightObj.fields.emplace_back("pos", JSONValue(posStr));

		std::string colorStr = Vec3ToString(color, 2);
		pointLightObj.fields.emplace_back("color", JSONValue(colorStr));

		pointLightObj.fields.emplace_back("enabled", JSONValue(m_bVisible != 0));
		pointLightObj.fields.emplace_back("brightness", JSONValue(brightness));

		parentObject.fields.emplace_back("point light info", JSONValue(pointLightObj));
	}

	bool PointLight::operator==(const PointLight& other)
	{
		return other.GetTransform()->GetLocalPosition() == m_Transform.GetLocalPosition() &&
			other.color == color &&
			other.m_bVisible == m_bVisible &&
			other.brightness == brightness;
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

	bool TokenContext::CanNextCharBeLabelPart() const
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
		char c = ConsumeNextChar();
		while (keywordPos < (i32)strlen(keywordStr) && c == keywordStr[keywordPos])
		{
			c = ConsumeNextChar();
			++keywordPos;
		}

		if (keywordPos == (i32)strlen(keywordStr))
		{
			return keywordType;
		}

		// Not this keyword, must be a label
		while (CanNextCharBeLabelPart())
		{
			ConsumeNextChar();
		}
		return TokenType::LABEL;
	}

	TokenType TokenContext::AttemptParseKeywords(const char* potentialKeywordStrs[], TokenType potentialKeywordTypes[], i32 keywordPositions[], i32 potentialCount)
	{
		for (i32 i = 0; i < potentialCount; ++i)
		{
			keywordPositions[i] = 1;
		}

		i32 matchedKeywordIndex = -1;
		char c = ConsumeNextChar();
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
							if (!HasNextChar() || isspace(PeekNextChar()))
							{
								matchedKeywordIndex = i;
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

				c = ConsumeNextChar();
			}
			else
			{
				// Doesn't match any keywords, must be a label

				while (CanNextCharBeLabelPart())
				{
					ConsumeNextChar();
				}
				return TokenType::LABEL;
			}
		}

		return TokenType::_NONE;
	}

	Tokenizer::Tokenizer(const std::string& code) :
		str(code)
	{
		context = {};
		context.buffer = context.bufferPtr = (char*)str.c_str();
		context.bufferLen = (i32)code.size();
	}

	TokenString Tokenizer::GetNextToken()
	{
		//char delim[] = " ,.\t\n";
		//char* token = nullptr;
		//char* str_context = nullptr;
		//token = strtok_s(str, delim, &str_context);

		//while (token != nullptr)
		//{
		//	Print("%s\n", (const char*)token);
		//	token = strtok_s(NULL, delim, &str_context);
		//}

		ConsumeWhitespaceAndComments();

		char const* const tokenStart = context.bufferPtr;
		i32 tokenLineNum = context.lineNumber;
		i32 tokenLinePos = context.linePos;

		TokenType nextTokenType = TokenType::_NONE;

		if (context.HasNextChar())
		{
			char c = context.ConsumeNextChar();

			switch (c)
			{
			// Keywords:
			case 't':
				nextTokenType = context.AttemptParseKeyword("true", TokenType::KEY_TRUE);
				break;
			case 'f':
				nextTokenType = context.AttemptParseKeyword("false", TokenType::KEY_FALSE);
				break;
			case 'b':
			{
				const char* potentialKeywords[] = { "bool", "break" };
				TokenType potentialTokenTypes[] = { TokenType::KEY_BOOL, TokenType::KEY_BREAK };
				i32 pos[] = { 0, 0 };
				nextTokenType = context.AttemptParseKeywords(potentialKeywords, potentialTokenTypes, pos, ARRAY_LENGTH(pos));
			} break;
			case 'i':
			{
				const char* potentialKeywords[] = { "int", "if" };
				TokenType potentialTokenTypes[] = { TokenType::KEY_INT, TokenType::KEY_IF };
				i32 pos[] = { 0, 0 };
				nextTokenType = context.AttemptParseKeywords(potentialKeywords, potentialTokenTypes, pos, ARRAY_LENGTH(pos));
			} break;
			case 'e':
			{
				const char* potentialKeywords[] = { "else", "elif" };
				TokenType potentialTokenTypes[] = { TokenType::KEY_ELSE, TokenType::KEY_ELIF };
				i32 pos[] = { 0, 0 };
				nextTokenType = context.AttemptParseKeywords(potentialKeywords, potentialTokenTypes, pos, ARRAY_LENGTH(pos));
			} break;
			case 'd':
				nextTokenType = context.AttemptParseKeyword("do", TokenType::KEY_DO);
				break;
			case 'w':
				nextTokenType = context.AttemptParseKeyword("while", TokenType::KEY_WHILE);
				break;
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
				nextTokenType = IsNextChar(':', TokenType::DOUBLE_COLON, TokenType::SINGLE_COLON);
				break;
			case ';':
				nextTokenType = TokenType::SEMICOLON;
				break;
			case '!':
				nextTokenType = IsNextChar('=', TokenType::NOT_EQUAL_TEST, TokenType::BANG);
				break;
			case '?':
				nextTokenType = TokenType::TERNARY;
				break;
			case '=':
				nextTokenType = IsNextChar('=', TokenType::EQUAL_TEST, TokenType::ASSIGNMENT);
				break;
			case '>':
				nextTokenType = IsNextChar('=', TokenType::GREATER_EQ, TokenType::GREATER);
				break;
			case '<':
				nextTokenType = IsNextChar('=', TokenType::LESS_EQ, TokenType::LESS);
				break;
			case '+':
				nextTokenType = IsNextChar('=', TokenType::ADD_ASSIGN, TokenType::ADD);
				break;
			case '-':
				nextTokenType = IsNextChar('=', TokenType::SUBTRACT_ASSIGN, TokenType::SUBTRACT);
				break;
			case '*':
				nextTokenType = IsNextChar('=', TokenType::MULTIPLY_ASSIGN, TokenType::MULTIPLY);
				break;
			case '/':
				nextTokenType = IsNextChar('=', TokenType::DIVIDE_ASSIGN, TokenType::DIVIDE);
				break;
			case '%':
				nextTokenType = IsNextChar('=', TokenType::MODULO_ASSIGN, TokenType::MODULO);
				break;
			case '&':
				if (context.PeekNextChar() == '=')
				{
					nextTokenType = TokenType::BINARY_AND_ASSIGN;
				}
				else
				{
					nextTokenType = IsNextChar('&', TokenType::BOOLEAN_AND, TokenType::BINARY_AND);
				}
				break;
			case '|':
				if (context.PeekNextChar() == '=')
				{
					nextTokenType = TokenType::BINARY_OR_ASSIGN;
				}
				else
				{
					nextTokenType = IsNextChar('|', TokenType::BOOLEAN_OR, TokenType::BINARY_OR);
				}
				break;
			case '^':
				nextTokenType = IsNextChar('=', TokenType::BINARY_XOR_ASSIGN, TokenType::BINARY_XOR);
				break;
			case '\\':
				if (context.HasNextChar())
				{
					// Escaped char
					context.ConsumeNextChar();
				}
				else
				{
					context.errorReason = "Input ended with a single backslash";
				}
				break;
			case '\'':
				nextTokenType = TokenType::STRING;
				while (context.HasNextChar())
				{
					if (context.ConsumeNextChar() == '\'')
					{
						break;
					}
				}
				break;
			case'\"':
				nextTokenType = TokenType::STRING;
				while (context.HasNextChar())
				{
					if (context.ConsumeNextChar() == '\"')
					{
						break;
					}
				}
				break;
			case '.':
				nextTokenType = TokenType::DOT;
				break;
			case '~':
				nextTokenType = TokenType::TILDE;
				break;
			case '`':
				nextTokenType = TokenType::BACK_QUOTE;
				break;
			default:
				if (isalpha(c) || c == '_')
				{
					nextTokenType = TokenType::LABEL;
					while (context.CanNextCharBeLabelPart())
					{
						context.ConsumeNextChar();
					}
				}
				else if (isdigit(c))
				{
					nextTokenType = TokenType::NUMBER;
					while (isdigit(context.PeekNextChar()))
					{
						context.ConsumeNextChar();
					}
				}
				else
				{
					PrintError("Unrecognized token: %c\n", c);
					nextTokenType = TokenType::_NONE;
				}
				break;
			}
		}

		TokenString tokenString = {};
		tokenString.lineNum = tokenLineNum;
		tokenString.linePos = tokenLinePos;
		tokenString.charPtr = tokenStart;
		tokenString.len = (context.bufferPtr - tokenStart);
		tokenString.type = nextTokenType;
		tokenString.TokenID = nextTokenID++;
		return tokenString;
	}

	void Tokenizer::ConsumeWhitespaceAndComments()
	{
		while (context.GetRemainingLength() > 1)
		{
			char c0 = context.PeekChar(0);
			char c1 = context.PeekChar(1);

			if (isspace(c0))
			{
				context.ConsumeNextChar();
				continue;
			}
			else if (c0 == '/' && c1 == '/')
			{
				// Consume remainder of line
				while (context.HasNextChar())
				{
					char c = context.ConsumeNextChar();
					if (c == '\n')
					{
						break;
					}
				}
			}
			else if (c0 == '/' && c1 == '*')
			{
				context.ConsumeNextChar();
				context.ConsumeNextChar();
				// Consume (potentially nested) block comment(s)
				i32 levelsDeep = 1;
				while (context.HasNextChar())
				{
					char bc0 = context.ConsumeNextChar();
					char bc1 = context.PeekNextChar();
					if (bc0 == '/' && bc1 == '*')
					{
						levelsDeep++;
						context.ConsumeNextChar();
					}
					else if (bc0 == '*' && bc1 == '/')
					{
						levelsDeep--;
						context.ConsumeNextChar();
					}

					if (levelsDeep == 0)
					{
						break;
					}
				}

				if (levelsDeep != 0)
				{
					context.errorReason = "Uneven number of block comment opens/closes";
				}
			}
			else
			{
				return;
			}
		}
		if (context.HasNextChar())
		{
			if (isspace(context.PeekNextChar()))
			{
				context.ConsumeNextChar();
			}
		}
	}

	TokenType Tokenizer::IsNextChar(char c, TokenType ifYes, TokenType ifNo)
	{
		if (context.HasNextChar() && context.PeekNextChar() == c)
		{
			context.ConsumeNextChar();
			return ifYes;
		}
		else
		{
			return ifNo;
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
			BitmapFont* font = g_Renderer->m_FntSourceCodeProWS;
			g_Renderer->SetFont(font);

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
			const real lineHeight = 0.075f;
			const real magic = 0.000183f * font->GetSize();
			const real lineNoWidth = 1.75f * lineHeight;

			if (bRenderCursor)
			{
				real cursorXO = g_Renderer->GetStringWidth(lines[cursor.y].substr(0, cursor.x), font, letterSpacing, false);
				// TODO: Get rid of magic numbers
				cursorXO -= 0.01f;
				glm::vec3 cursorPos = pos;
				cursorPos += right * (cursorXO * -magic) + up * (cursor.y * -lineHeight);
				g_Renderer->DrawStringWS("|", glm::vec4(1.0f), cursorPos, rot, letterSpacing);
			}

			if (bRenderText)
			{
				glm::vec4 lineNumberColor(0.4f, 0.4f, 0.4f, 1.0f);
				glm::vec4 textColor(0.95f, 0.95f, 0.95f, 1.0f);
				for (i32 lineNumber = 0; lineNumber < (i32)lines.size(); ++lineNumber)
				{
					g_Renderer->DrawStringWS(IntToString(lineNumber, 2, ' '), lineNumberColor, pos + right * lineNoWidth, rot, letterSpacing);
					g_Renderer->DrawStringWS(lines[lineNumber], textColor, pos, rot, letterSpacing);
					pos.y -= lineHeight;
				}
			}

			glm::vec3 posBR = posTL - (right * width * 0.92f) - (up * height * 0.92f);
			glm::vec4 col = bParsePassed ? glm::vec4(0.3f, 0.8f, 0.3f, 1.0f) : glm::vec4(0.8f, 0.3f, 0.3f, 1.0f);
			g_Renderer->DrawStringWS("#", col, posBR, rot, letterSpacing);
		}
	}

	void Terminal::DrawImGuiObjects()
	{
		GameObject::DrawImGuiObjects();

		if (ImGui::Begin("Terminal"))
		{
			ImGui::Text("Cursor: %d, %d", cursor.x, cursor.y);
			ImGui::Text("Max x: %d", cursorMaxX);
			ImGui::Text("Lines: %d", lines.size());
			ImGui::Text("Current line len: %d", lines[cursor.y].size());


			//if (ImGui::BeginChild("Variables", ImVec2(0.0f, 220.0f), true))
			//{
			//	for (const Value& var : variables)
			//	{
			//		ImGui::Text("ID: %2d, Type: %d, Name: %s", var.id, var.type, var.name.c_str());
			//	}
			//}
			//ImGui::EndChild();

			//ImGui::Text("Errors: %d", errors.size());
			//if (ImGui::BeginChild("Success", ImVec2(0.0f, 220.0f), true))
			//{
			//	if (errors.empty())
			//	{
			//		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1.0f, 0.5f, 1.0f));
			//		ImGui::Text("Success");
			//		ImGui::PopStyleColor();
			//	}
			//	else
			//	{
			//		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
			//		for (const Error& e : errors)
			//		{
			//			ImGui::Text("%2d, %s", e.lineNumber, e.str.c_str());
			//		}
			//		ImGui::PopStyleColor();
			//	}
			//}
			//ImGui::EndChild();
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
		std::string& curLine = lines[cursor.y];
		if (lines.empty() || (cursor.x == (i32)curLine.size() && cursor.y == (i32)lines.size() - 1))
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
		std::string& curLine = lines[cursor.y];
		if (lines.empty() || (cursor.x == (i32)curLine.size() && cursor.y == (i32)lines.size() - 1))
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

		std::string str;
		for (const std::string& line : lines)
		{
			str.append(line);
			str.push_back('\n');
		}

		Tokenizer tokenizer(str);

		i32 tokenCount = 0;
		while (tokenizer.context.HasNextChar())
		{
			if (!tokenizer.context.errorReason.empty())
			{
				PrintError("Error encountered while parsing code: %s\n", tokenizer.context.errorReason.c_str());
				bParsePassed = false;
				break;
			}
			TokenString tokenStr = tokenizer.GetNextToken();
			if (tokenStr.type != TokenType::_NONE)
			{
				++tokenCount;
			}
			//Print("Type: %d, str: %s\n", (i32)tokenStr.type, tokenStr.ToString().c_str());
		}
		Print("Tokens found in code: %d\n", tokenCount);

		if (!bParsePassed)
		{
			//PrintError("%d errors:\n", errors.size());
			//for (const Error& e : errors)
			//{
			//	PrintError("  %02i  %s\n", e.lineNumber, e.str.c_str());
			//}
		}
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
					return flex::EventReply::CONSUMED;
				}
				if (keyCode == KeyCode::KEY_ESCAPE)
				{
					m_Camera->TransitionOut();
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
					}
					else
					{
						char c = '0' + ((i32)keyCode - (i32)KeyCode::KEY_KP_0);
						TypeChar(c);
					}
					return EventReply::CONSUMED;
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
