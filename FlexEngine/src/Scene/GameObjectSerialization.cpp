#include "stdafx.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionShapes/btCollisionShape.h>

#include <glm/gtx/norm.hpp> // for distance2
#include <glm/gtx/quaternion.hpp> // for rotate
#include <glm/gtx/vector_angle.hpp> // for angle
#include <glm/ext/matrix_transform.hpp> // for rotate

#include <imgui/imgui_internal.h> // For PushItemFlag

IGNORE_WARNINGS_POP

#include "Scene/GameObject.hpp"

#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "Physics/RigidBody.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/Objects/Carts.hpp"
#include "Scene/SceneManager.hpp"

// Loading & saving game objects (scene files & prefabs)
namespace flex
{
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

	bool GameObject::ShouldSerialize()
	{
		return true;
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

			const GameObjectTypeInfo* typeInfo = FindGameObjectType(m_TypeID);
			const bool bSerializePhysics = typeInfo == nullptr || (typeInfo->flags & GameObjectTypeInfo::NO_SERIALIZED_PHYSICS) == 0;
			btCollisionShape* collisionShape = bSerializePhysics ? GetCollisionShape() : nullptr;
			if (collisionShape != nullptr && (!m_SourcePrefabID.IsValid() || bSerializePrefabData))
			{
				JSONObject colliderObj;
				if (SerializeCollider(collisionShape, m_Transform.GetWorldScale(), colliderObj))
				{
					object.fields.emplace_back("collider", JSONValue(colliderObj));
				}
			}

			RigidBody* rigidBody = bSerializePhysics ? GetRigidBody() : nullptr;
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

	bool GameObject::ShouldSerializeMesh() const
	{
		const GameObjectTypeInfo* typeInfo = FindGameObjectType(m_TypeID);
		return m_TypeID != BaseCartSID && (typeInfo == nullptr || (typeInfo->flags & GameObjectTypeInfo::NO_SERIALIZED_MESH) == 0);
	}

	bool GameObject::ShouldSerializeMaterial() const
	{
		const GameObjectTypeInfo* typeInfo = FindGameObjectType(m_TypeID);
		return m_TypeID != BaseCartSID && (typeInfo == nullptr || (typeInfo->flags & GameObjectTypeInfo::NO_SERIALIZED_MATERIAL) == 0);
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
} // namespace flex
