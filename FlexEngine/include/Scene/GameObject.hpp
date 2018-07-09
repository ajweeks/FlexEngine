#pragma once

#include <vector>

#include "Audio/RandomizedAudioSource.hpp"
#include "Helpers.hpp"
#include "Transform.hpp"
#include "JSONTypes.hpp"

class btCollisionShape;

namespace flex
{
	class BaseScene;

	class GameObject
	{
	public:
		GameObject(const std::string& name, GameObjectType type);
		virtual ~GameObject();

		// Returns a new game object which is a direct copy of this object, parented to parent
		// If parent == nullptr then new object will have same parent as this object
		virtual GameObject* CopySelf(GameObject* parent, const std::string& newObjectName, bool bCopyChildren);

		static GameObject* CreateObjectFromJSON(const JSONObject& obj, BaseScene* scene, MaterialID overriddenMatID = InvalidMaterialID);

		JSONObject SerializeToJSON(BaseScene* scene);
		void ParseJSON(const JSONObject& obj, BaseScene* scene, MaterialID overriddenMatID = InvalidMaterialID);

		virtual void Initialize();
		virtual void PostInitialize();
		virtual void Destroy();
		virtual void Update();

		void SetParent(GameObject* parent);
		GameObject* GetParent();
		void DetachFromParent();

		GameObject* AddChild(GameObject* child);
		bool RemoveChild(GameObject* child);
		void RemoveAllChildren();
		const std::vector<GameObject*>& GetChildren() const;

		bool HasChild(GameObject* child, bool bRecurse);

		virtual Transform* GetTransform();
		
		void AddTag(const std::string& tag);
		bool HasTag(const std::string& tag);
		std::vector<std::string> GetTags() const;

		RenderID GetRenderID() const;
		void SetRenderID(RenderID renderID);

		std::string GetName() const;
		void SetName(const std::string& newName);

		bool IsSerializable() const;
		void SetSerializable(bool bSerializable);

		bool IsStatic() const;
		void SetStatic(bool bStatic);

		bool IsVisible() const;
		void SetVisible(bool bVisible, bool effectChildren = true);

		bool IsVisibleInSceneExplorer() const;
		void SetVisibleInSceneExplorer(bool bVisibleInSceneExplorer);

		btCollisionShape* SetCollisionShape(btCollisionShape* collisionShape);
		btCollisionShape* GetCollisionShape() const;

		RigidBody* SetRigidBody(RigidBody* rigidBody);
		RigidBody* GetRigidBody() const;

		MeshComponent* GetMeshComponent();
		MeshComponent* SetMeshComponent(MeshComponent* meshComponent);

		// Called when another object has begun to overlap
		void OnOverlapBegin(GameObject* other);
		// Called when another object is no longer overlapping
		void OnOverlapEnd(GameObject* other);

		// Filled if this object is a trigger
		std::vector<GameObject*> overlappingObjects;

		/*
		* Returns true if we are now interacting, or false
		* if the object passed in is null or equals our already set interacting object
		*/
		bool SetInteractingWith(GameObject* gameObject);

		GameObject* GetObjectInteractingWith();

		GameObjectType GetType() const;

	protected:
		friend class BaseClass;
		friend class BaseScene;

		void CopyGenericFields(GameObject* newGameObject, GameObject* parent, bool bCopyChildren);

		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID);
		virtual void SerializeUniqueFields(JSONObject& parentObject);

		std::string m_Name;

		std::vector<std::string> m_Tags;

		Transform m_Transform;
		RenderID m_RenderID = InvalidRenderID;

		GameObjectType m_Type = GameObjectType::NONE;

		/*
		* If true, this object will be written out to file
		* NOTE: If false, children will also not be serialized
		*/
		bool m_bSerializable = true;

		/*
		* Whether or not this object should be rendered
		* NOTE: Does *not* effect childrens' visibility
		*/
		bool m_bVisible = true;

		/*
		* Whether or not this object should be shown in the scene explorer UI
		* NOTE: Children are also hidden when this if false!
		*/
		bool m_bVisibleInSceneExplorer = true;

		/*
		* True if and only if this object will never move
		* If true, this object will be rendered to reflection probes
		*/
		bool m_bStatic = false;

		/*
		* If true this object will not collide with other game objects
		* Overlapping objects will cause OnOverlapBegin/End to be called
		*/
		bool m_bTrigger = false;

		/*
		* True if this object can currently be interacted with (can be based on
		* player proximity, among other things)
		*/
		bool m_bInteractable = false;

		bool m_bLoadedFromPrefab = false;
		std::string m_PrefabName;

		/*
		* Will point at the player we're interacting with, or the object if we're the player
		*/
		GameObject* m_ObjectInteractingWith = nullptr;

		btCollisionShape* m_CollisionShape = nullptr;
		RigidBody* m_RigidBody = nullptr;

		GameObject* m_Parent = nullptr;
		std::vector<GameObject*> m_Children;

		MeshComponent* m_MeshComponent = nullptr;

		bool bBeingInteractedWith = false;

		static AudioSourceID s_BunkSound;
		static RandomizedAudioSource s_SqueakySounds;

	};

	// Child classes

	class Valve : public GameObject
	{
	public:
		Valve(const std::string& name);

		virtual GameObject* CopySelf(GameObject* parent, const std::string& newObjectName, bool bCopyChildren) override;

		virtual void PostInitialize() override;
		virtual void Update() override;

		// Serialized fields
		real minRotation = 0.0f;
		real maxRotation = 0.0f;

		// Non-serialized fields
		// Multiplied with value retrieved from input manager
		real rotationSpeedScale = 1.0f;

		// 1 = never slow down, 0 = slow down immediately
		real invSlowDownRate = 0.85f;

		real rotationSpeed = 0.0f;
		real pRotationSpeed = 0.0f;

		real rotation = 0.0f;
		real pRotation = 0.0f;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) override;

	};

	class RisingBlock : public GameObject
	{
	public:
		RisingBlock(const std::string& name);

		virtual GameObject* CopySelf(GameObject* parent, const std::string& newObjectName, bool bCopyChildren) override;

		virtual void Initialize() override;
		virtual void PostInitialize() override;
		virtual void Update() override;

		// Serialized fields
		Valve* valve = nullptr; // (object name is serialized)
		glm::vec3 moveAxis;

		// If true this block will "fall" to its minimum 
		// value when a player is not interacting with it
		bool bAffectedByGravity = false;

		// Non-serialized fields
		glm::vec3 startingPos;

		real pdDistBlockMoved = 0.0f;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) override;

	};

	class GlassPane : public GameObject
	{
	public:
		GlassPane(const std::string& name);

		virtual GameObject* CopySelf(GameObject* parent, const std::string& newObjectName, bool bCopyChildren) override;

		bool bBroken = false;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) override;

	};

	class ReflectionProbe : public GameObject
	{
	public:
		ReflectionProbe(const std::string& name);

		virtual GameObject* CopySelf(GameObject* parent, const std::string& newObjectName, bool bCopyChildren) override;

		virtual void PostInitialize() override;

		MaterialID captureMatID = 0;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) override;

	};

	class Skybox : public GameObject
	{
	public:
		Skybox(const std::string& name);

		virtual GameObject* CopySelf(GameObject* parent, const std::string& newObjectName, bool bCopyChildren) override;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) override;

	};

} // namespace flex
