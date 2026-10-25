#pragma once

#include <list>

#include "Audio/AudioManager.hpp"
#include "Callbacks/InputCallbacks.hpp"
#include "Graphics/RendererTypes.hpp"
#include "Graphics/VertexBufferData.hpp" // For VertexBufferDataCreateInfo
#include "Helpers.hpp"
#include "Scene/GameObjectTypes.hpp"
#include "Transform.hpp"
#include "Time.hpp"

class btCollisionShape;
class btTriangleIndexVertexArray;

namespace flex
{
	class EditorObject;
	class BaseScene;
	class BezierCurveList;
	class DroppedItem;
	class Mesh;
	class MeshComponent;
	class MineralDeposit;
	class Socket;
	class Terminal;
	class TerminalCamera;
	class Vehicle;
	class Wire;
	class WirePlug;
	class SoftBody;
	struct RoadSegment;
	class Player;

	namespace VM
	{
		class VirtualMachine;
	}

	struct ChildIndex
	{
		ChildIndex(const std::list<u32>& siblingIndices) :
			siblingIndices(siblingIndices)
		{}

		void Add(u32 siblingIndex)
		{
			siblingIndices.emplace_back(siblingIndex);
		}

		u32 Pop()
		{
			u32 result = siblingIndices.front();
			siblingIndices.pop_front();
			return result;
		}

		bool IsValid()
		{
			return !siblingIndices.empty();
		}

		// Stores sibling index for each level down the hierarchy to reach this child
		// (0th index = index under root, 1st = index under first child, etc.)
		std::list<u32> siblingIndices;
	};

	struct ActiveItem
	{
		void Create(PrefabID sourcePrefabID);
		void Clear();

		bool IsValid() const { return m_ItemProxyObject != nullptr; }

		GameObject* m_ItemProxyObject = nullptr; // Just the mesh representation of the object
		StringID m_TypeID = InvalidID;
		PrefabID m_SourcePrefabID = InvalidPrefabID;
	};

	extern ChildIndex InvalidChildIndex;

	struct GameObjectStack
	{
		GameObjectStack();
		GameObjectStack(const PrefabID& prefabID, i32 count);

		void Clear();

		void SerializeToJSON(JSONObject& parentObject) const;
		void ParseFromJSON(const JSONObject& parentObject);

		PrefabID prefabID;
		i32 count;

		union UserData
		{
			real floatVal;
		} userData;
	};

	class GameObject
	{
	public:
		// Non enum class so we can use bit operators without a cast
		enum CopyFlags : u32
		{
			CHILDREN				= (1 << 0),
			MESH					= (1 << 1),
			RIGIDBODY				= (1 << 2),
			ADD_TO_SCENE			= (1 << 3),
			CREATE_RENDER_OBJECT	= (1 << 4),
			COPYING_TO_PREFAB		= (1 << 5),

			ALL = 0xFFFFFFFF & ~COPYING_TO_PREFAB,
			_NONE = 0
		};

		GameObject(const std::string& name, StringID typeID);
		GameObject(const std::string& name, StringID typeID, const GameObjectID& gameObjectID, const PrefabIDPair& sourcePrefabID, bool bIsPrefabTemplate);
		virtual ~GameObject() = default;

		static GameObject* CreateObjectFromJSON(
			const JSONObject& obj,
			BaseScene* scene,
			i32 sceneFileVersion,
			const PrefabIDPair& sourcePrefabIDPair,
			bool bIsPrefabTemplate,
			CopyFlags copyFlags);

		static GameObject* CreateObjectFromPrefabTemplate(
			const PrefabID& prefabID,
			const GameObjectID& gameObjectID,
			std::string* optionalObjectName = nullptr,
			GameObject* parent = nullptr,
			Transform* optionalTransform = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL);

		static GameObject* CreateObjectOfType(
			StringID gameObjectTypeID,
			const std::string& objectName,
			const GameObjectID& gameObjectID = InvalidGameObjectID,
			const char* optionalTypeStr = nullptr,
			const PrefabIDPair& sourcePrefabIDPair = InvalidPrefabIDPair,
			bool bIsPrefabTemplate = false);

		static void RegisterPropertyCollections();

		static void UpdateHeldItem(GameObjectID heldItemID);
		static void UpdateActiveItem(const ActiveItem& activeItem);

		// Returns a new game object which is a direct copy of this object, parented to parent
		// If parent == nullptr then new object will have same parent as this object
		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID);

		virtual void Initialize();
		virtual void PostInitialize();
		virtual void Destroy(bool bDetachFromParent = true);
		virtual void Update();
		virtual void FixedUpdate();
		virtual void Render();

		virtual void DrawImGuiObjects(bool bDrawingEditorObjects);

		// TODO: Rename to Deserialize
		void ParseJSON(
			const JSONObject& obj,
			BaseScene* scene,
			i32 fileVersion,
			MaterialID overriddenMatID = InvalidMaterialID,
			CopyFlags copyFlags = CopyFlags::ALL);

		virtual void FixupPrefabTemplateIDs(GameObject* newGameObject);
		virtual void OnIDChanged(const GameObjectID& oldID, const GameObjectID& newID);

		virtual bool ShouldSerialize();

		virtual void OnItemize(GameObjectStack::UserData& outUserData);
		virtual void OnDeItemize(const GameObjectStack::UserData& userData);

		// Returns true if this object was deleted or duplicated
		bool DoImGuiContextMenu(bool bActive);

		Transform* GetTransform();
		const Transform* GetTransform() const;

		bool DrawImGuiDuplicateGameObjectButton();

		// Create new/overwrite existing prefab from this object
		bool SaveAsPrefab();

		// Gives this object and all its children new GameObjectIDs
		void ChangeAllIDs();

		void SetSourcePrefabID(PrefabID sourcePrefabID);

		// Overwrite new object's children's IDs with matching previous IDs
		// Child hierarchy is assumed to match exactly
		void OverwritePrefabIDs(GameObject* previousGameObject, GameObject* newGameObject);

		// If bSerializePrefabData is true, we are serializing a prefab template object
		JSONObject Serialize(const BaseScene* scene, bool bIsRoot, bool bSerializePrefabData);

		void RemoveRigidBody();

		// Fills the given vector with game objects that pass the filter
		template<typename T>
		void FilterType(std::function<bool(GameObject*)> callback, std::vector<T*>& outList)
		{
			if (callback(this))
			{
				outList.emplace_back((T*)this);
			}

			for (GameObject* child : m_Children)
			{
				child->FilterType(callback, outList);
			}
		}

		// Fills the given vector with game objects that pass the filter
		void Filter(std::function<bool(GameObject*)> callback, std::vector<GameObject*>& outList);
		// Fills the given vector with game object IDs that pass the filter
		void FilterID(std::function<bool(GameObject*)> callback, std::vector<GameObjectID>& outList);
		// Returns the number of objects which pass the filter
		u32 FilterCount(std::function<bool(GameObject*)> callback);
		u32 FilterCountInternal(std::function<bool(GameObject*)> callback, u32 count);
		// Returns the first found object that passes the filter
		GameObject* FilterFirst(std::function<bool(GameObject*)> callback);

		void SetParent(GameObject* parent);
		GameObject* GetParent();
		void DetachFromParent();

		// Returns a list of objects, starting with the root, going up to this object
		std::vector<GameObject*> GetParentChain();

		// Walks up the tree to the highest parent
		GameObject* GetRootParent();

		GameObject* AddChild(GameObject* child);
		GameObject* AddChildImmediate(GameObject* child, bool bRegisterChild = true);
		EditorObject* AddEditorChildImmediate(EditorObject* child);
		bool RemoveChildImmediate(GameObjectID childID, bool bDestroy);
		bool RemoveChildImmediate(GameObject* child, bool bDestroy);
		void MoveChild(GameObjectID childID, i32 newSiblingIndex);
		void MoveChild(GameObject* child, i32 newSiblingIndex);
		const std::vector<GameObject*>& GetChildren() const;
		u32 GetChildCountOfType(StringID objTypeID, bool bRecurse);
		u32 GetChildCount() const;

		GameObject* AddSibling(GameObject* child);
		GameObject* AddSiblingImmediate(GameObject* child);

		template<class T>
		void GetChildrenOfType(StringID objTypeID, bool bRecurse, std::vector<T*>& children)
		{
			for (GameObject* child : m_Children)
			{
				child->GetChildrenOfTypeInternal(objTypeID, bRecurse, children);
			}
		}

		template<class T>
		void GetChildrenOfTypeInternal(StringID objTypeID, bool bRecurse, std::vector<T*>& children)
		{
			if (m_TypeID == objTypeID)
			{
				children.push_back((T*)this);
			}

			if (bRecurse)
			{
				for (GameObject* child : m_Children)
				{
					child->GetChildrenOfType(objTypeID, bRecurse, children);
				}
			}
		}

		bool HasChild(GameObject* child, bool bCheckChildrensChildren);
		GameObject* GetChild(u32 childIndex);

		// Moves this object to the given index in its parents list of children
		void SetSiblingIndex(i32 newIndex);
		i32 GetSiblingIndex() const;

		// Returns all objects who share our parent
		std::vector<GameObject*> GetAllSiblings();
		// Returns all objects who share our parent and have a larger sibling index
		std::vector<GameObject*> GetEarlierSiblings();
		// Returns all objects who share our parent and have a smaller sibling index
		std::vector<GameObject*> GetLaterSiblings();

		void AddTag(const std::string& tag);
		bool HasTag(const std::string& tag);
		std::vector<std::string> GetTags() const;

		std::string GetName() const;
		void SetName(const std::string& newName);

		void GetFullyPathedName(StringBuilder& stringBuilder);

		bool IsSerializable() const;
		void SetSerializable(bool bSerializable);

		// Objects owned directly by another object (e.g. internal simulation objects) are never
		// added to the scene, so shouldn't expect to be registered with it on destruction
		void SetOwnedInternally(bool bOwnedInternally);

		bool IsStatic() const;
		void SetStatic(bool bStatic);

		bool IsVisible() const;
		virtual void SetVisible(bool bVisible, bool bEffectChildren = true);

		// If bIncludingChildren is true, true will be returned if this or any children are visible in scene explorer
		bool IsVisibleInSceneExplorer(bool bIncludingChildren = false) const;
		void SetVisibleInSceneExplorer(bool bVisibleInSceneExplorer);

		bool HasUniformScale() const;
		void SetUseUniformScale(bool bUseUniformScale, bool bEnforceImmediately);
		void EnforceUniformScale();

		btCollisionShape* SetCollisionShape(btCollisionShape* collisionShape);
		btCollisionShape* GetCollisionShape() const;
		bool GetCollisionAABB(AABB& outAABB);

		RigidBody* SetRigidBody(RigidBody* rigidBody);
		RigidBody* GetRigidBody() const;

		Mesh* GetMesh();
		Mesh* SetMesh(Mesh* mesh);

		// Editor-only
		void OnExternalMeshChange(const std::string& meshFilePath);

		bool CastsShadow() const;
		void SetCastsShadow(bool bCastsShadow);

		// Called when another object has begun to overlap
		void OnOverlapBegin(GameObject* other);
		// Called when another object is no longer overlapping
		void OnOverlapEnd(GameObject* other);

		StringID GetTypeID() const;

		void AddSelfAndChildrenToVec(std::vector<GameObject*>& vec) const;
		void RemoveSelfAndChildrenFromVec(std::vector<GameObject*>& vec) const;

		void AddSelfIDAndChildrenToVec(std::vector<GameObjectID>& vec) const;
		void RemoveSelfIDAndChildrenFromVec(std::vector<GameObjectID>& vec) const;

		bool SelfOrChildIsSelected() const;

		void ClearNearbyInteractable();
		void SetNearbyInteractable(GameObjectID nearbyInteractableID);

		const PrefabIDPair& GetSourcePrefabIDPair() const;
		bool IsPrefabTemplate() const;

		virtual void OnCharge(real chargeAmount);

		ChildIndex ComputeChildIndex() const;
		ChildIndex GetChildIndexWithID(const GameObjectID& gameObjectID) const;
		GameObjectID GetIDAtChildIndex(const ChildIndex& childIndex) const;

		PrefabID Itemize(GameObjectStack::UserData& outUserData);
		static GameObject* Deitemize(PrefabID prefabID, const glm::vec3& positionWS, const glm::quat& rotWS, const GameObjectStack::UserData& userData);

		bool IsItemizable() const;
		bool IsWearable() const;

		i32 GetOutputSignal(i32 slotIdx);

		static const size_t MaxNameLength = 256;

		// Filled if this object is a trigger
		std::vector<GameObject*> overlappingObjects;

		GameObjectID ID;

	protected:
		friend BaseScene;
		friend ResourceManager;

		static const char* s_DefaultNewGameObjectName;

		static PropertyCollection* BuildPropertyCollection();

		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs);
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData);
		void SerializeField(JSONObject& parentObject, bool bSerializePrefabData, const char* fieldLabel, void* valuePtr, ValueType valueType, u32 precision = 2);
		bool SerializeRegisteredProperties(JSONObject& parentObject, PropertyCollection* collection, bool bSerializePrefabData);
		void DeserializeRegisteredProperties(const JSONObject& parentObject, PropertyCollection* collection);

		void CopyGenericFields(GameObject* newGameObject, GameObject* parent = nullptr, CopyFlags copyFlags = CopyFlags::ALL);

		// Internal - used to update indices recursively
		void UpdateSiblingIndices(i32 newIndex);

		void SetOutputSignal(i32 slotIdx, i32 value);

		bool GetChildIndexWithIDRecursive(const GameObjectID& gameObjectID, ChildIndex& outChildIndex) const;
		bool GetIDAtChildIndexRecursive(ChildIndex childIndex, GameObjectID& outGameObjectID) const;

		void GetNewObjectNameAndID(CopyFlags copyFlags, std::string* optionalName, GameObject* parent, std::string& newObjectName, GameObjectID& newGameObjectID);

		bool ShouldSerializeMesh() const;
		bool ShouldSerializeMaterial() const;

		GameObject* AddChildImmediateCommon(GameObject* child);

		std::string m_Name;

		std::vector<std::string> m_Tags;

		Transform m_Transform;

		StringID m_TypeID = InvalidStringID;

		// Bitfield 0
		// If true, this object will be written out to file
		// NOTE: If false, children will also not be serialized
		bool m_bSerializable : 1;

		// Whether or not this object should be rendered
		// NOTE: Does *not* effect childrens' visibility
		bool m_bVisible : 1;

		// Whether or not this object should be shown in the scene explorer UI
		// NOTE: Children are also hidden when this if false!
		bool m_bVisibleInSceneExplorer : 1;

		// True if and only if this object will never move
		// NOTE: If true, this object will be rendered to reflection probes
		bool m_bStatic : 1;

		// If true this object will not collide with other game objects
		// Overlapping objects will cause OnOverlapBegin/End to be called
		bool m_bTrigger : 1;

		// True if this object can currently be interacted with (can be based on
		// player proximity, among other things)
		bool m_bInteractable : 1;

		bool m_bCastsShadow : 1;

		// If true, this object will never be added to a scene, but will only be instantiated (copied from)
		bool m_bIsTemplate : 1;

		// If true, this object is owned by another object and never registered with the scene
		bool m_bOwnedInternally : 1;

		// Bitfield 1
		bool m_bSerializeMaterial : 1;

		// Editor only fields
		bool m_bUniformScale : 1;
		bool m_bItemizable : 1;

		// The prefab ID pair corresponding to the prefab this object was loaded from
		PrefabIDPair m_SourcePrefabID;

		GameObjectID m_NearbyInteractableID = InvalidGameObjectID;

		// Index into list of siblings in parent
		i32 m_SiblingIndex = 0;

		btCollisionShape* m_CollisionShape = nullptr;
		RigidBody* m_RigidBody = nullptr;

		GameObject* m_Parent = nullptr;
		std::vector<GameObject*> m_Children;

		Mesh* m_Mesh = nullptr;

	private:
		void DrawImGuiForSelfInternal(bool bDrawingEditorObjects);

	};

	//
	// Child classes
	//
} // namespace flex
