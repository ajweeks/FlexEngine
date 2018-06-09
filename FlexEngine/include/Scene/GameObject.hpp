#pragma once

#include <vector>

#include "GameContext.hpp"
#include "Transform.hpp"
#include "Helpers.hpp"
#include "Audio/RandomizedAudioSource.hpp"

class btCollisionShape;

namespace flex
{
	class GameObject
	{
	public:
		GameObject(const std::string& name, GameObjectType type);
		virtual ~GameObject();

		virtual void Initialize(const GameContext& gameContext);
		virtual void PostInitialize(const GameContext& gameContext);
		virtual void Destroy(const GameContext& gameContext);
		virtual void Update(const GameContext& gameContext);

		void SetParent(GameObject* parent);
		GameObject* GetParent();

		GameObject* AddChild(GameObject* child);
		bool RemoveChild(GameObject* child);
		void RemoveAllChildren();
		const std::vector<GameObject*>& GetChildren() const;

		virtual Transform* GetTransform();
		
		void AddTag(const std::string& tag);
		bool HasTag(const std::string& tag);

		RenderID GetRenderID() const;
		void SetRenderID(RenderID renderID);

		std::string GetName() const;

		bool IsSerializable() const;
		void SetSerializable(bool bSerializable);

		bool IsStatic() const;
		void SetStatic(bool bStatic);

		bool IsVisible() const;
		void SetVisible(bool visible, bool effectChildren = true);

		bool IsVisibleInSceneExplorer() const;
		void SetVisibleInSceneExplorer(bool visibleInSceneExplorer);

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

	protected:
		friend class BaseClass;
		friend class BaseScene;

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

		/*
		* Will point at the player we're interacting with, or the object if we're the player
		*/
		GameObject* m_ObjectInteractingWith = nullptr;

		btCollisionShape* m_CollisionShape = nullptr;
		RigidBody* m_RigidBody = nullptr;

		GameObject* m_Parent = nullptr;
		std::vector<GameObject*> m_Children;

		MeshComponent* m_MeshComponent = nullptr;

	public:
		// All fields which valves need to know about to do their thing
		struct ValveMembers
		{
			// Serialized fields
			real minRotation = 0.0f;
			real maxRotation = 0.0f;

			// Multiplied with value retrieved from input manager
			real rotationSpeedScale = 1.0f;

			// 1 = never slow down, 0 = slow down immediately
			real invSlowDownRate = 0.85f;

			// Non-serialized fields
			real rotationSpeed = 0.0f;
			real pRotationSpeed = 0.0f;

			real rotation = 0.0f;
		} m_ValveMembers;

		struct RisingBlockMembers
		{
			GameObject* valve = nullptr;
			glm::vec3 moveAxis;

			// Non-serialized fields
			glm::vec3 startingPos;
		} m_RisingBlockMembers;

		private:
			static AudioSourceID s_BunkSound;
			static RandomizedAudioSource s_SqueakySounds;

	};
} // namespace flex
