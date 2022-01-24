#pragma once

IGNORE_WARNINGS_PUSH
#include <BulletDynamics/Vehicle/btRaycastVehicle.h>
IGNORE_WARNINGS_POP

#include <list>

#include "Audio/AudioCue.hpp"
#include "Audio/AudioManager.hpp"
#include "Callbacks/InputCallbacks.hpp"
#include "Graphics/RendererTypes.hpp"
#include "Graphics/VertexBufferData.hpp" // For VertexBufferDataCreateInfo
#include "Helpers.hpp"
#include "Spring.hpp"
#include "Track/BezierCurve3D.hpp"
#include "Transform.hpp"
#include "Time.hpp"
#include "Timer.hpp"

class btCollisionShape;
class btTriangleIndexVertexArray;

namespace flex
{
	class BaseScene;
	class BezierCurveList;
	class Mesh;
	class MeshComponent;
	class Socket;
	class TerminalCamera;
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

	static constexpr StringID BaseObjectSID = SID("object");
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

		GameObject(const std::string& name, StringID typeID, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);
		virtual ~GameObject();

		static GameObject* CreateObjectFromJSON(
			const JSONObject& obj,
			BaseScene* scene,
			i32 sceneFileVersion,
			const PrefabID& prefabIDLoadedFrom = InvalidPrefabID,
			bool bIsPrefabTemplate = false,
			CopyFlags copyFlags = CopyFlags::ALL);

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
			const PrefabID& prefabLoadedFrom = InvalidPrefabID,
			bool bIsPrefabTemplate = false);

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

		// Overwrite new object's children's IDs with matching previous IDs
		// Child hierarchy is assumed to match exactly
		void OverwritePrefabIDs(GameObject* previousGameObject, GameObject* newGameObject);

		// If bSerializePrefabData is true, we are serializing a prefab template object
		JSONObject Serialize(const BaseScene* scene, bool bIsRoot, bool bSerializePrefabData);

		void RemoveRigidBody();

		void SetParent(GameObject* parent);
		GameObject* GetParent();
		void DetachFromParent();

		// Returns a list of objects, starting with the root, going up to this object
		std::vector<GameObject*> GetParentChain();

		// Walks up the tree to the highest parent
		GameObject* GetRootParent();

		GameObject* AddChild(GameObject* child);
		GameObject* AddChildImmediate(GameObject* child);
		bool RemoveChildImmediate(GameObjectID childID, bool bDestroy);
		bool RemoveChildImmediate(GameObject* child, bool bDestroy);
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

		void UpdateSiblingIndices(i32 myIndex);
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
		void SetNearbyInteractable(GameObject* nearbyInteractable);

		PrefabID GetPrefabIDLoadedFrom() const;
		bool IsPrefabTemplate() const;

		virtual void OnCharge(real chargeAmount);

		ChildIndex ComputeChildIndex() const;
		ChildIndex GetChildIndexWithID(const GameObjectID& gameObjectID) const;
		GameObjectID GetIDAtChildIndex(const ChildIndex& childIndex) const;

		PrefabID Itemize(GameObjectStack::UserData& outUserData);
		static GameObject* Deitemize(PrefabID prefabID, const glm::vec3& positionWS, const glm::quat& rotWS, const GameObjectStack::UserData& userData);

		bool IsItemizable() const;
		bool IsWearable() const;

		static const size_t MaxNameLength = 256;

		// Filled if this object is a trigger
		std::vector<GameObject*> overlappingObjects;

		// Signals that connected objects get sent
		std::vector<i32> outputSignals;
		std::vector<Socket*> sockets;

		GameObjectID ID;

	protected:
		friend BaseScene;
		friend ResourceManager;

		static const char* s_DefaultNewGameObjectName;

		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs);
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData);
		void SerializeField(JSONObject& parentObject, bool bSerializePrefabData, const char* fieldLabel, void* valuePtr, ValueType valueType, u32 precision = 2);
		bool SerializeRegisteredProperties(JSONObject& parentObject, bool bSerializePrefabData);
		void DeserializeRegisteredProperties(JSONObject& parentObject);

		void CopyGenericFields(GameObject* newGameObject, GameObject* parent = nullptr, CopyFlags copyFlags = CopyFlags::ALL);

		void SetOutputSignal(i32 slotIdx, i32 value);

		bool GetChildIndexWithIDRecursive(const GameObjectID& gameObjectID, ChildIndex& outChildIndex) const;
		bool GetIDAtChildIndexRecursive(ChildIndex childIndex, GameObjectID& outGameObjectID) const;

		void GetNewObjectNameAndID(CopyFlags copyFlags, std::string* optionalName, GameObject* parent, std::string& newObjectName, GameObjectID& newGameObjectID);

		PropertyCollection* RegisterPropertyCollection();
		void DeregisterPropertyCollection();
		PropertyCollection* GetPropertyCollection();

		bool ShouldSerializeMesh() const;
		bool ShouldSerializeMaterial() const;

		// Returns a string containing our name with a "_xx" post-fix where xx is the next highest index or 00

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

		// If true, this object will never live in the real world and will only be instantiated
		bool m_bIsTemplate : 1;

		// Bitfield 1
		bool m_bSerializeMaterial : 1;

		// Editor only fields
		bool m_bUniformScale : 1;
		bool m_bItemizable : 1;

		// The PrefabID this object is loaded from if it's a prefab instance, or the PrefabID
		// of the prefab this object defines (when m_bIsTemplate is true), or InvalidPrefabID otherwise.
		PrefabID m_PrefabIDLoadedFrom = InvalidPrefabID;

		// TODO: Remove?
		GameObject* m_NearbyInteractable = nullptr;

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

	static constexpr StringID DroppedItemSID = SID("dropped item");
	class DroppedItem final : public GameObject
	{
	public:
		DroppedItem(const PrefabID& prefabID, i32 stackSize);

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;

		virtual void Update() override;

		bool CanBePickedUp() const;
		void OnPickedUp();

		PrefabID prefabID = InvalidPrefabID;
		i32 stackSize = 0;
		real secondsAlive = 0.0f;
		real bobAmount = 0.0f;
		real turnSpeed = 6.0f;
		glm::vec3 initialVel = VEC3_ZERO;

		static const real MIN_TIME_BEFORE_PICKUP;

	};

	static constexpr StringID DirectionalLightSID = SID("directional light");
	class DirectionalLight final : public GameObject
	{
	public:
		DirectionalLight();
		explicit DirectionalLight(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;
		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;
		virtual void SetVisible(bool bVisible, bool bEffectChildren /* = true */) override;

		bool operator==(const DirectionalLight& other);

		DirLightData data;

		// Editor-only
		glm::vec3 pos = VEC3_ZERO;

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;
	};

	static constexpr StringID PointLightSID = SID("point light");
	class PointLight final : public GameObject
	{
	public:
		explicit PointLight(BaseScene* scene);
		explicit PointLight(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;
		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;
		virtual void SetVisible(bool bVisible, bool bEffectChildren /* = true */) override;

		bool operator==(const PointLight& other);

		PointLightData data;
		PointLightID pointLightID = InvalidPointLightID;

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;
	};

	static constexpr StringID SpotLightSID = SID("spot light");
	class SpotLight final : public GameObject
	{
	public:
		explicit SpotLight(BaseScene* scene);
		explicit SpotLight(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;
		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;
		virtual void SetVisible(bool bVisible, bool bEffectChildren /* = true */) override;

		bool operator==(const SpotLight& other);

		SpotLightData data;
		SpotLightID spotLightID = InvalidSpotLightID;

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;
	};

	static constexpr StringID AreaLightSID = SID("area light");
	class AreaLight final : public GameObject
	{
	public:
		explicit AreaLight(BaseScene* scene);
		explicit AreaLight(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;
		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;
		virtual void SetVisible(bool bVisible, bool bEffectChildren /* = true */) override;

		bool operator==(const AreaLight& other);

		AreaLightData data;
		AreaLightID areaLightID = InvalidAreaLightID;

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

		void UpdatePoints();

	};

	static constexpr StringID ValveSID = SID("valve");
	class Valve final : public GameObject
	{
	public:
		explicit Valve(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		virtual void PostInitialize() override;
		virtual void Update() override;

		// Serialized fields
		glm::vec2 valveRange;

		// Non-serialized fields
		// Multiplied with value retrieved from input manager
		real rotationSpeedScale = 1.0f;

		// 1 = never slow down, 0 = slow down immediately
		real invSlowDownRate = 0.85f;

		real rotationSpeed = 0.0f;
		real pRotationSpeed = 0.0f;

		real rotation = 0.0f;
		real pRotation = 0.0f;

		bool m_bBeingInteractedWith = false;

		static AudioCue s_SqueakySounds;
		static AudioSourceID s_BunkSound;

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

	};

	static constexpr StringID RisingBlockSID = SID("rising block");
	class RisingBlock final : public GameObject
	{
	public:
		explicit RisingBlock(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

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
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

	};

	static constexpr StringID GlassPaneSID = SID("glass pane");
	class GlassPane final : public GameObject
	{
	public:
		explicit GlassPane(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		bool bBroken = false;

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

	};

	static constexpr StringID ReflectionProbeSID = SID("reflection probe");
	class ReflectionProbe final : public GameObject
	{
	public:
		explicit ReflectionProbe(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		virtual void PostInitialize() override;

		MaterialID captureMatID = 0;
	};

	static constexpr StringID SkyboxSID = SID("skybox");
	class Skybox final : public GameObject
	{
	public:
		explicit Skybox(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		void ProcedurallyInitialize(MaterialID matID);

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

		void InternalInit(MaterialID matID);
	};

	static constexpr StringID BaseCartSID = SID("base cart");
	class BaseCart : public GameObject
	{
	public:
		BaseCart(const std::string& name,
			StringID typeID,
			const GameObjectID& gameObjectID = InvalidGameObjectID,
			const char* meshName = emptyCartMeshName,
			const PrefabID& prefabIDLoadedFrom = InvalidPrefabID,
			bool bPrefabTemplate = false);

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent /* = true */) override;
		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;
		virtual real GetDrivePower() const;

		void OnTrackMount(TrackID trackID, real newDistAlongTrack);
		void OnTrackDismount();

		void SetItemHolding(GameObject* obj);
		void RemoveItemHolding();

		// Advances along track, rotates to face correct direction
		void AdvanceAlongTrack(real dT);

		// Returns velocity
		real UpdatePosition();

		CartID cartID = InvalidCartID;

		TrackID currentTrackID = InvalidTrackID;
		real distAlongTrack = -1.0f;
		real velocityT = 1.0f;

		real distToRearNeighbor = -1.0f;

		// Non-serialized fields
		real attachThreshold = 1.5f;

		Spring<real> m_TSpringToCartAhead;

		CartChainID chainID = InvalidCartChainID;

		static const char* emptyCartMeshName;

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

	};

	static constexpr StringID EmptyCartSID = SID("empty cart");
	class EmptyCart : public BaseCart
	{
	public:
		EmptyCart(const std::string& name,
			const GameObjectID& gameObjectID = InvalidGameObjectID,
			const PrefabID& prefabIDLoadedFrom = InvalidPrefabID,
			bool bPrefabTemplate = false);

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

	};


	static constexpr StringID EngineCartSID = SID("engine cart");
	class EngineCart : public BaseCart
	{
	public:
		EngineCart(const std::string& name,
			const GameObjectID& gameObjectID = InvalidGameObjectID,
			const PrefabID& prefabIDLoadedFrom = InvalidPrefabID,
			bool bPrefabTemplate = false);

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		virtual void Update() override;
		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;
		virtual real GetDrivePower() const override;


		real moveDirection = 1.0f; // -1.0f or 1.0f
		real powerRemaining = 1.0f;

		real powerDrainMultiplier = 0.1f;
		real speed = 0.1f;

		static const char* engineMeshName;

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

	};

	static constexpr StringID MobileLiquidBoxSID = SID("mobile liquid box");
	class MobileLiquidBox final : public GameObject
	{
	public:
		MobileLiquidBox();
		explicit MobileLiquidBox(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		virtual void Initialize() override;

		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;

		bool bInCart = false;
		real liquidAmount = 0.0f;

	};

	static constexpr StringID BatterySID = SID("battery");
	class Battery final : public GameObject
	{
	public:
		Battery();
		explicit Battery(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual void Update() override;
		virtual void OnCharge(real amount) override;

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		virtual void OnItemize(GameObjectStack::UserData& outUserData) override;
		virtual void OnDeItemize(const GameObjectStack::UserData& userData) override;

		real chargeAmount = 0.0f;
		real chargeCapacity = 100.0f;
		real flashTimer = 0.0f;
		static const real flashDuration;

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

	};

	static constexpr StringID GerstnerWaveSID = SID("gerstner wave");
	class GerstnerWave final : public GameObject
	{
	public:
		explicit GerstnerWave(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual void Initialize() override;
		virtual void Update() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		void AddWave();
		void RemoveWave(i32 index);

		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;

		struct WaveInfo
		{
			bool enabled = true;
			real a = 0.0f;
			real waveDirTheta = 0.0f;
			real waveLen = 1.0f;

			// Non-serialized, calculated from fields above
			real waveDirCos;
			real waveDirSin;
			real moveSpeed = -1.0f;
			real waveVecMag = -1.0f;
			real accumOffset = 0.0f;
		};

		struct WaveTessellationLOD
		{
			WaveTessellationLOD(real squareDist, u32 vertCountPerAxis) :
				squareDist(squareDist),
				vertCountPerAxis(vertCountPerAxis)
			{}

			real squareDist;
			u32 vertCountPerAxis;
		};

		struct WaveSamplingLOD
		{
			WaveSamplingLOD(real squareDist, real amplitudeCutoff) :
				squareDist(squareDist),
				amplitudeCutoff(amplitudeCutoff)
			{}

			real squareDist;
			real amplitudeCutoff;
		};

		struct WaveChunk
		{
			WaveChunk(const glm::vec2i& index, u32 vertOffset, u32 tessellationLODLevel) :
				index(index),
				vertOffset(vertOffset),
				tessellationLODLevel(tessellationLODLevel)
			{}

			glm::vec2i index;
			u32 vertOffset;
			u32 tessellationLODLevel;
		};

		struct WaveGenData
		{
			// Inputs
			// General
			std::vector<WaveInfo> const* waveContributions;
			std::vector<WaveChunk> const* waveChunks;
			std::vector<WaveSamplingLOD> const* waveSamplingLODs;
			std::vector<WaveTessellationLOD> const* waveTessellationLODs;
			WaveInfo const* soloWave;
			real size;
			u32 chunkIdx;
			bool bDisableLODs;
			// Chunk-specific
			glm::vec3* positions;
			real blendDist;

			// Intermediate values:
			__m128* lodCutoffsAmplitudes_4 = nullptr;
			__m128* lodNextCutoffAmplitudes_4 = nullptr;
			__m128* lodBlendWeights_4 = nullptr;

			// Outputs:
			__m128* positionsx_4 = nullptr;
			__m128* positionsy_4 = nullptr;
			__m128* positionsz_4 = nullptr;
			__m128* lodSelected_4 = nullptr;
			__m128* uvUs_4 = nullptr;
			__m128* uvVs_4 = nullptr;
		};

		using ThreadID = u32;

		//struct Thread
		//{
		//	std::thread thread;
		//	bool bInUse;
		//};

		//struct ThreadData
		//{
		//	WaveGenData waveGenInOut;
		//	ThreadID threadID;
		//};

	private:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

		void UpdateDependentVariables(i32 waveIndex);

		void AllocWorkQueueEntry(u32 workQueueIndex);
		void FreeWorkQueueEntry(u32 workQueueIndex);

		void DiscoverChunks();
		void UpdateWaveVertexData();

		void UpdateWavesLinear();
		void UpdateWavesSIMD();
		glm::vec4 ChooseColourFromLOD(real LOD);
		glm::vec3 QueryHeightFieldFromVerts(const glm::vec3& queryPos) const;
		WaveChunk const* GetChunkAtPos(const glm::vec2& pos) const;
		WaveTessellationLOD const* GetTessellationLOD(u32 lodLevel) const;
		u32 ComputeTesellationLODLevel(const glm::vec2i& chunkIdx);
		void UpdateNormalsForChunk(u32 chunkIdx);
		void SortWaves();
		void SortWaveSamplingLODs();
		void SortWaveTessellationLODs();
		real GetWaveAmplitudeLODCutoffForDistance(real dist) const;

		real size = 30.0f;
		real loadRadius = 35.0f;
		real updateSpeed = 20.0f;
		real blendDist = 10.0f;
		bool bDisableLODs = false;
		u32 maxChunkVertCountPerAxis = 64;
		real reverseWaveAmplitude = 0.5f;

		OceanData oceanData;

		void* criticalSection = nullptr;

		MaterialID m_WaveMaterialID;

		std::vector<WaveTessellationLOD> waveTessellationLODs;
		std::vector<WaveSamplingLOD> waveSamplingLODs;

		std::vector<WaveInfo> waveContributions;
		i32 soloWaveIndex = -1;

		std::vector<WaveChunk> waveChunks;

		bool m_bPinCenter = false;
		glm::vec3 m_PinnedPos;

		VertexBufferDataCreateInfo m_VertexBufferCreateInfo;
		std::vector<u32> m_Indices;

		GameObject* bobber = nullptr;
		Spring<real> bobberTarget;

		u32 DEBUG_lastUsedVertCount = 0;

		WaveThreadData threadUserData;
	};

	bool operator==(const GerstnerWave::WaveInfo& lhs, const GerstnerWave::WaveInfo& rhs);

	void* WaveThreadUpdate(void* inData);

	GerstnerWave::WaveChunk const* GetChunkAtPos(const glm::vec2& pos, const std::vector<GerstnerWave::WaveChunk>& waveChunks, real size);
	GerstnerWave::WaveTessellationLOD const* GetTessellationLOD(u32 lodLevel, const std::vector<GerstnerWave::WaveTessellationLOD>& waveTessellationLODs);
	u32 MapVertIndexAcrossLODs(u32 vertIndex, GerstnerWave::WaveTessellationLOD const* lod0, GerstnerWave::WaveTessellationLOD const* lod1);

	static constexpr StringID BlocksSID = SID("blocks");
	class Blocks final : public GameObject
	{
	public:
		explicit Blocks(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

	protected:

	};

	// Connects together devices to transmit information & power
	static constexpr StringID WireSID = SID("wire");
	class Wire final : public GameObject
	{
	public:
		Wire(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual void Initialize() override;
		virtual void PostInitialize() override;
		virtual void Update() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;
		virtual bool ShouldSerialize() override;

		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

		GameObjectID GetOtherPlug(WirePlug* plug);

		void SetStartTangent(const glm::vec3& tangent);
		void ClearStartTangent();
		void SetEndTangent(const glm::vec3& tangent);
		void ClearEndTangent();

		void UpdateWireMesh();

		void CalculateTangentAtPoint(real t, glm::vec3& outTangent);
		void CalculateBasisAtPoint(real t, glm::vec3& outNormal, glm::vec3& outTangent, glm::vec3& outBitangent);

		static const real DEFAULT_LENGTH;

		GameObjectID plug0ID = InvalidGameObjectID;
		GameObjectID plug1ID = InvalidGameObjectID;

		i32 numPoints = 12;
		i32 numRadialPoints = 10;
		real stiffness = 0.8f;
		real pointInvMass = 1.0f / 15.0f;
		real damping = 0.9f;

		SoftBody* m_SoftBody = nullptr;

	private:
		void DestroyPoints();
		void GeneratePoints();
		void UpdateMesh();
		void UpdateIndices();

		VertexBufferDataCreateInfo m_VertexBufferCreateInfo;
		std::vector<u32> m_Indices;

	};

	// End of wire - the part you actually interact with
	static constexpr StringID WirePlugSID = SID("wire plug");
	class WirePlug final : public GameObject
	{
	public:
		WirePlug(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);
		WirePlug(const std::string& name, Wire* owningWire, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual void Destroy(bool bDetachFromParent /* = true */) override;

		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

		void PlugIn(Socket* socket);
		void Unplug();

		static const real nearbyThreshold;

		GameObjectID wireID = InvalidGameObjectID;
		GameObjectID socketID = InvalidGameObjectID;

	};

	// Connect wires to objects
	static constexpr StringID SocketSID = SID("socket");
	class Socket final : public GameObject
	{
	public:
		Socket(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual void Destroy(bool bDetachFromParent = true) override;

		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

		void OnPlugIn(WirePlug* plug);
		void OnUnPlug();

		GameObjectID connectedPlugID = InvalidGameObjectID;
		i32 slotIdx = 0;
	};

	static constexpr StringID TerminalSID = SID("terminal");
	class Terminal final : public GameObject
	{
	public:
		Terminal();
		explicit Terminal(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual void Initialize() override;
		virtual void PostInitialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;

		bool IsInteractable(Player* player);
		void SetBeingInteractedWith(Player* player);

		void OnScriptChanged();
		void SetCamera(TerminalCamera* camera);
		void DrawImGuiWindow();

		static const i32 MAX_OUTPUT_COUNT = 4;

		std::vector<Wire*> wireSlots;

	protected:
		void TypeChar(char c);
		void DeleteChar(bool bDeleteUpToNextBreak = false); // (backspace)
		void DeleteCharInFront(bool bDeleteUpToNextBreak = false); // (delete)

		void MoveCursorToStart();
		void MoveCursorToStartOfLine();
		void MoveCursorToEnd();
		void MoveCursorToEndOfLine();
		void MoveCursorLeft(bool bSkipToNextBreak = false);
		void MoveCursorRight(bool bSkipToNextBreak = false);
		void MoveCursorUp();
		void MoveCursorDown();
		void ScrollCursorToView();
		void PageUp();
		void PageDown();

		void ClampCursorX();

		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

	private:
		friend TerminalCamera;
		friend TerminalManager;

		EventReply OnKeyEvent(KeyCode keyCode, KeyAction action, i32 modifiers);
		KeyEventCallback<Terminal> m_KeyEventCallback;

		bool SkipOverChar(char c);
		i32 GetIdxOfNextBreak(i32 y, i32 startX);
		i32 GetIdxOfPrevBreak(i32 y, i32 startX);

		void ParseCode();
		void EvaluateCode();

		bool SaveScript();
		void ReloadScript();

		std::string m_ScriptFileName;

		// Non-serialized fields:
		VM::VirtualMachine* m_VM = nullptr;

		std::vector<std::string> lines;
		bool m_bDirtyFlag = false;
		bool m_bShowExternalChangePopup = false;
		real m_DisplayReloadTimeRemaining = -1.0f; // When positive show text indicating reload occurred
		const real m_ReloadPopupInfoDuration = 2.5f;

		real m_LineHeight = 1.0f;
		real m_LetterScale = 0.04f;

		glm::vec2i cursor;
		// Keeps track of the cursor x to be able to position the cursor correctly
		// after moving from a long line, over a short line, onto a longer line again
		i32 cursorMaxX = 0;
		real scrollY = 0.0f;
		i32 screenRowCount = 31;

		TerminalCamera* m_Camera = nullptr;
		//const i32 m_Columns = 45;

		bool m_bBeingInteractedWith = false;

		const sec m_CursorBlinkRate = 0.6f;
		sec m_CursorBlinkTimer = 0.0f;

	};

	static constexpr StringID ParticleSystemSID = SID("particle system");
	class ParticleSystem final : public GameObject
	{
	public:
		explicit ParticleSystem(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;

		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;

		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

		real scale = 1.0f;
		ParticleSimData data;
		bool bEnabled = true;
		MaterialID simMaterialID = InvalidMaterialID;
		MaterialID renderingMaterialID = InvalidMaterialID;
		ParticleSystemID particleSystemID = InvalidParticleSystemID;

	};

	struct NoiseFunction
	{
		enum class Type : u32
		{
			PERLIN,
			FBM, // https://www.iquilezles.org/www/articles/fbm/fbm.htm
			VORONOI, // https://www.iquilezles.org/www/articles/voronoise/voronoise.htm
			SMOOTH_VORONOI, // https://www.iquilezles.org/www/articles/smoothvoronoi/smoothvoronoi.htm

			// NOTE: New entries should also be added to NoiseFunctionTypeNames & NoiseFunction::GenerateDefault

			_NONE
		};

		static Type TypeFromString(const char* str);
		static const char* TypeToString(Type type);

		static NoiseFunction GenerateDefault(Type type);

		Type type;
		real baseFeatureSize;
		i32 numOctaves;
		real H; // controls self-similarity [0, 1]
		real heightScale;
		real lacunarity;
		real wavelength;
		real sharpness;
		i32 isolateOctave = -1;

		// TODO: Seed
	};

	static const char* NoiseFunctionTypeNames[] =
	{
		"Perlin",
		"FBM",
		"Voronoi",
		"Smooth Voronoi",

		"None"
	};

	static_assert(ARRAY_LENGTH(NoiseFunctionTypeNames) == (u32)NoiseFunction::Type::_NONE + 1, "NoiseFunctionTypeNames length must match NoiseFunction::Type enum");

	static constexpr StringID TerrainGeneratorSID = SID("terrain generator");
	class TerrainGenerator final : public GameObject
	{
	public:
		explicit TerrainGenerator(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual void Initialize() override;
		virtual void Update() override;
		virtual void Destroy(bool bDetachFromParent = true) override;

		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;

		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

		void Regenerate();

		struct Biome
		{
			std::string name;
			std::vector<NoiseFunction> noiseFunctions;
		};

		struct TerrainChunkData
		{
			// General info
			real chunkSize;
			real maxHeight;
			real roadBlendDist;
			real roadBlendThreshold;
			u32 vertCountPerChunkAxis;
			i32 isolateNoiseLayer = -1;

			u32 numPointsPerAxis;

			NoiseFunction* biomeNoise = nullptr;
			std::vector<Biome>* biomes = nullptr;
			std::vector<std::vector<glm::vec2>>* randomTables;
			std::map<glm::ivec3, std::vector<RoadSegment*>, iVec3Compare>* roadSegments;

			// Per chunk inputs
			volatile glm::ivec3 chunkIndex;

			// Per chunk outputs
			volatile glm::vec3* positions;
			volatile glm::vec4* colours;
			volatile glm::vec2* uvs;
			volatile u32* indices;
		};

		void FillInTerrainChunkData(volatile TerrainChunkData& outChunkData);
		real Sample(const volatile TerrainChunkData& chunkData, const glm::vec2& pos);

	private:
		struct Chunk;

		friend Road;

		static real SmoothBlend(real t);

		void GenerateGradients();
		void UpdateRoadSegments();

		void DiscoverChunks();
		void GenerateChunks();
		void DestroyChunk(Chunk* chunk);
		void DestroyAllChunks();

		void FillOutConstantData(TerrainGenConstantData& constantData);

		void DestroyChunkRigidBody(Chunk* chunk);
		void CreateChunkRigidBody(Chunk* chunk);

		void AllocWorkQueueEntry(u32 workQueueIndex);
		void FreeWorkQueueEntry(u32 workQueueIndex);

		bool DrawNoiseFunctionImGui(NoiseFunction& noiseFunction, bool& bOutRemoved);

		NoiseFunction ParseNoiseFunction(const JSONObject& noiseFunctionObj);
		JSONObject SerializeNoiseFunction(const NoiseFunction& noiseFunction);

		GameObjectID m_RoadGameObjectID = InvalidGameObjectID;

		MaterialID m_TerrainMatID = InvalidMaterialID;

		struct Chunk
		{
			btTriangleIndexVertexArray* triangleIndexVertexArray = nullptr;
			RigidBody* rigidBody = nullptr;
			MeshComponent* meshComponent = nullptr;
			btCollisionShape* collisionShape = nullptr;
			u32 linearIndex = 0;
		};

		// TODO: Merge somehow while maintaining unique indices for work queue
		std::map<glm::ivec3, Chunk*, iVec3Compare> m_Meshes; // Chunk index to mesh

		void* criticalSection = nullptr;

		real m_LoadedChunkRadius = 100.0f;
		real m_LoadedChunkRigidBodyRadius2 = 100.0f * 100.0f;

		std::set<glm::ivec3, iVec3Compare> m_ChunksToLoad;
		std::set<glm::ivec3, iVec3Compare> m_ChunksToDestroy;
		glm::vec2 m_PreviousCenterPoint;
		real m_PreviousLoadedChunkRadius = 0.0f; // Probably only needed in editor when m_LoadedChunkRadius can change

		const ns m_CreationBudgetPerFrame = Time::ConvertFormatsConstexpr(4.0f, Time::Format::MILLISECOND, Time::Format::NANOSECOND);
		const ns m_DeletionBudgetPerFrame = Time::ConvertFormatsConstexpr(0.5f, Time::Format::MILLISECOND, Time::Format::NANOSECOND);

		bool m_UseManualSeed = true;
		i32 m_ManualSeed = 0;

		real m_RoadBlendDist = 10.0f;
		real m_RoadBlendThreshold = 10.0f;

		bool m_bHighlightGrid = false;
		bool m_bDisplayRandomTables = false;

		bool m_bUseAsyncCompute = true;

		bool m_bPinCenter = false;
		glm::vec3 m_PinnedPos;

		glm::vec3 m_LowCol;
		glm::vec3 m_MidCol;
		glm::vec3 m_HighCol;

		NoiseFunction m_BiomeNoise;
		std::vector<Biome> m_Biomes;
		std::vector<std::vector<glm::vec2>> m_RandomTables;
		u32 m_BasePerlinTableWidth = 128;

		// Map of chunk index to overlapping road segments
		std::map<glm::ivec3, std::vector<RoadSegment*>, iVec3Compare> m_RoadSegments;
		std::array<RoadSegment_GPU, MAX_NUM_ROAD_SEGMENTS> m_RoadSegmentsGPU;

		u32 m_RandomTableTextureLayerCount = 0;
		TextureID m_RandomTableTextureID = InvalidTextureID;

		i32 m_IsolateNoiseLayer = -1;

		u32 m_VertCountPerChunkAxis = 8;
		real m_ChunkSize = 512.0f;
		real m_MaxHeight = 3.0f;

		u32 m_NumPointsPerAxis = 8 + 1;
		real m_IsoLevel = 0.0f;

		u32 m_MaxChunkCount = 16;

		TerrainThreadData threadUserData;

	};


	glm::vec3 SampleTerrainWithRoadBlend(volatile TerrainGenerator::TerrainChunkData* chunkData,
		std::vector<RoadSegment*>* overlappingRoadSegments,
		const glm::vec2& sampleCenter, glm::vec4& outColour, bool bCilpPointsInsideRoad);

	void* TerrainThreadUpdate(void* inData);

	real SampleBiomeTerrain(const TerrainGenerator::Biome& biome, const glm::vec2& pos);
	glm::vec4 SampleTerrain(volatile TerrainGenerator::TerrainChunkData const* chunkData, const glm::vec2& pos);
	real SampleNoiseFunction(volatile TerrainGenerator::TerrainChunkData const* chunkData, const NoiseFunction& noiseFunction, const glm::vec2& pos);
	real SamplePerlinNoise(const std::vector<std::vector<glm::vec2>>& randomTables, const glm::vec2& pos, real octave);

	static constexpr StringID SpringObjectSID = SID("spring");
	class SpringObject final : public GameObject
	{
	public:
		SpringObject(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		virtual void Initialize() override;
		virtual void Update() override;
		virtual void Destroy(bool bDetachFromParent = true) override;

		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;

		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

	private:
		static void CreateMaterials();

		static const char* s_ExtendedMeshFilePath;
		static const char* s_ContractedMeshFilePath;

		static MaterialID s_SpringMatID;
		static MaterialID s_BobberMatID;

		MeshComponent* m_ExtendedMesh = nullptr;
		MeshComponent* m_ContractedMesh = nullptr;

		VertexBufferDataCreateInfo m_DynamicVertexBufferCreateInfo;
		std::vector<u32> m_Indices;

		GameObject* m_Target = nullptr;
		real m_MinLength = 5.0f;
		real m_MaxLength = 10.0f;
		glm::vec3 m_TargetPos = VEC3_ZERO;

		GameObject* m_OriginTransform = nullptr;

		// We manage these objects ourselves rather than adding them to the scene
		bool m_bSimulateTarget = true;
		SoftBody* m_SpringSim = nullptr;
		GameObject* m_Bobber = nullptr;

		std::vector<glm::vec3> extendedPositions;
		std::vector<glm::vec3> extendedNormals;
		std::vector<glm::vec3> extendedTangents;
		std::vector<glm::vec3> contractedPositions;
		std::vector<glm::vec3> contractedNormals;
		std::vector<glm::vec3> contractedTangents;
	};

	struct Point
	{
		Point(glm::vec3 pos, glm::vec3 vel, real invMass) :
			pos(pos),
			vel(vel),
			invMass(invMass)
		{}

		glm::vec3 pos;
		glm::vec3 vel;
		real invMass;
	};

	struct Constraint
	{
		enum class EqualityType
		{
			EQUALITY,
			INEQUALITY,

			_NONE
		};

		enum class Type
		{
			DISTANCE,
			BENDING,

			_NONE
		};

		Constraint(real stiffness, EqualityType equalityType, Type type) :
			stiffness(stiffness),
			equalityType(equalityType),
			type(type)
		{
		}

		real stiffness;
		EqualityType equalityType;
		Type type;
	};

	struct DistanceConstraint : public Constraint
	{
		DistanceConstraint(i32 pointIndex0, i32 pointIndex1, real stiffness, real targetDistance);

		real targetDistance;
		i32 pointIndices[2];
	};

	struct BendingConstraint : public Constraint
	{
		BendingConstraint(i32 pointIndex0, i32 pointIndex1, i32 pointIndex2, i32 pointIndex3, real stiffness, real targetPhi);

		real targetPhi;
		i32 pointIndices[4];
	};

	struct Triangle
	{
		Triangle();
		Triangle(i32 pointIndex0, i32 pointIndex1, i32 pointIndex2);

		i32 pointIndices[3];
	};

	static constexpr StringID SoftBodySID = SID("soft body");
	class SoftBody final : public GameObject
	{
	public:
		SoftBody(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;

		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;

		void SetStiffness(real stiffness);
		void SetDamping(real damping);

		void SetRenderWireframe(bool bRenderWireframe);

		// Add new constraint between index0 & index1 if one doesn't already exist.
		// Returns new constraint count
		u32 AddUniqueDistanceConstraint(i32 index0, i32 index1, u32 atIndex, real stiffness);
		u32 AddUniqueBendingConstraint(i32 index0, i32 index1, i32 index2, i32 index3, u32 atIndex, real stiffness);

		static ms FIXED_UPDATE_TIMESTEP;
		static u32 MAX_UPDATE_COUNT; // Max fixed update steps that can be taken in one frame

		std::vector<Point*> points;
		// TODO: Split constraint types into separate containers
		std::vector<Constraint*> constraints;
		std::vector<Triangle*> triangles;

	private:
		void Draw();

		void LoadFromMesh();

		// Outside vert is vert not on shared edge
		bool GetTriangleSharingEdge(const std::vector<u32>& indexData, i32 edgeIndex0, i32 edgeIndex1, const Triangle& originalTri, Triangle& outTri, i32& outOutsideVertIndex);

		u32 m_SolverIterationCount;
		bool m_bPaused = false;
		bool m_bSingleStep = false;
		bool m_bRenderWireframe = true;

		ms m_MSToSim = 0.0f;
		ms m_UpdateDuration = 0.0f;
		real m_Damping = 0.99f;
		real m_Stiffness = 0.8f;
		real m_BendingStiffness = 0.1f;

		i32 m_ShownBendingIndex = 0;
		i32 m_FirstBendingConstraintIndex = 0;

		std::vector<glm::vec3> initialPositions;

		MeshComponent* m_MeshComponent = nullptr;
		VertexBufferDataCreateInfo m_MeshVertexBufferCreateInfo;
		MaterialID m_MeshMaterialID = InvalidMaterialID;

		std::string m_CurrentMeshFilePath;
		// Editor only
		i32 m_SelectedMeshIndex = -1;
		std::string m_CurrentMeshFileName;

	};

	static constexpr StringID VehicleSID = SID("vehicle");
	class Vehicle final : public GameObject
	{
	public:
		Vehicle(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		virtual void Initialize() override;
		virtual void PostInitialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;

		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;

		virtual void FixupPrefabTemplateIDs(GameObject* newGameObject) override;

		void OnPlayerEnter();
		void OnPlayerExit();

		enum class SoundEffect
		{
			ENGINE,
			BRAKE,
			// NOTE: New entries also need to be added to VehicleSoundEffectNames

			_COUNT
		};

		enum class Tire : u32
		{
			FL = 0,
			FR = 1,
			RL = 2,
			RR = 3,

			_NONE
		};

	private:
		static const i32 m_TireCount = 4;

		void CreateRigidBody();
		void MatchCorrespondingID(const GameObjectID& existingID, GameObject* newGameObject, const char* objectName, GameObjectID& outCorrespondingID);
		void SetSoundEffectSID(SoundEffect soundEffect, StringID soundSID);

		std::array<StringID, (i32)SoundEffect::_COUNT> m_SoundEffectSIDs;

		const real MAX_STEER = 0.5f;
		const real STEERING_SLOW_FACTOR = 4.0f;

		void ResetTransform();

		std::array<SoundClip_LoopingSimple, (u32)SoundEffect::_COUNT> m_SoundEffects;

		GameObjectID m_TireIDs[m_TireCount];
		GameObjectID m_BrakeLightIDs[2];

		real m_MaxEngineForce = 9000.0f;
		real m_EngineForce = 0.0f;
		real m_EngineAccel = 25000.0f;
		real m_MaxBrakeForce = 55.0f;
		real m_BrakeForce = 0.0f;
		real m_EngineForceDecelerationSpeed = 10.0f;
		real m_TurnAccel = 1.8f;
		real m_WheelSlipScreechThreshold = 0.8f; // 0 = no screech, 1 = screech on any traction loss
		real m_Steering = 0.0f;
		real m_RollInfluence = 0.05f;
		real m_WheelFriction = 60.0f;
		real m_WheelRadius = 0.5f;
		real m_WheelWidth = 0.4f;
		real m_SuspensionStiffness = 20.f;
		real m_SuspensionDamping = 2.3f;
		real m_SuspensionCompression = 4.4f;

		MaterialID m_GlassMatID = InvalidMaterialID;
		MaterialID m_CarPaintMatID = InvalidMaterialID;
		MaterialID m_TireMatID = InvalidMaterialID;
		MaterialID m_SpokeMatID = InvalidMaterialID;
		MaterialID m_BrakeLightMatID = InvalidMaterialID;
		MaterialID m_ReverseLightMatID = InvalidMaterialID;

		glm::vec4 m_InitialBrakeLightMatEmissive;
		glm::vec4 m_ActiveBrakeLightMatEmissive;
		glm::vec4 m_InitialReverseLightMatEmissive;
		glm::vec4 m_ActiveReverseLightMatEmissive;

		btAlignedObjectArray<btCollisionShape*> m_collisionShapes;

		btDefaultVehicleRaycaster* m_VehicleRaycaster = nullptr;
		btRaycastVehicle* m_Vehicle = nullptr;

		btRaycastVehicle::btVehicleTuning m_tuning;

		btVector3 m_pLinearVelocity;

		RollingAverage<real> m_WheelSlipHisto;

		sec m_SecUpsideDown = 0.0f;
		const sec SEC_UPSIDE_DOWN_BEFORE_FLIP = 2.0f;

		bool m_bOccupied = false;

		bool m_bFlippingRightSideUp = false;
		sec m_SecFlipppingRightSideUp = 0.0f;
		const sec MAX_FLIPPING_UPRIGHT_TIME = 3.5f;
		glm::quat m_TargetRot;
		const real UPRIGHTING_SPEED = 10.0f;

	};

	static const char* VehicleSoundEffectNames[] =
	{
		"Vehicle engine",
		"Vehicle brake",

		"COUNT"
	};

	static_assert((ARRAY_LENGTH(VehicleSoundEffectNames) - 1) == (i32)Vehicle::SoundEffect::_COUNT, "VehicleSoundEffectNames length does not match the number of entries in the Vehicle::SoundEffect enum");


	struct RoadSegment
	{
		BezierCurve3D curve;
		real widthStart;
		real widthEnd;
		AABB aabb;
		MeshComponent* mesh = nullptr;

		bool Overlaps(const glm::vec2& point);
		real SignedDistanceTo(const glm::vec3& point, glm::vec3& outClosestPoint);
		void ComputeAABB();
	};

	static constexpr StringID RoadSID = SID("road");
	class Road : public GameObject
	{
	public:
		Road(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual void Initialize() override;
		virtual void PostInitialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;
		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		void RegenerateMesh();

		std::vector<RoadSegment> roadSegments;

	private:
		void GenerateMesh();
		void GenerateSegment(i32 index);
		void GenerateMaterial();
		void GenerateSegmentsThroughPoints(const std::vector<glm::vec3>& points);

		void CreateRigidBody(u32 meshIndex);

		std::vector<RigidBody*> m_RigidBodies;
		std::vector<btTriangleIndexVertexArray*> m_MeshVertexArrays;

		MaterialID m_RoadMaterialID = InvalidMaterialID;

		real m_MaxWidth = 4.0f;

		u32 m_QuadCountPerSegment = 10;

		// Non-serialized fields
		GameObjectID m_TerrainGameObjectID = InvalidGameObjectID;

	};

	static constexpr StringID SolarPanelSID = SID("solar panel");
	class SolarPanel : public GameObject
	{
	public:
		SolarPanel(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual void Update() override;

	private:
		real m_ChargeRate = 10.0f;
		real m_Efficiency = 1.0f;

	};

	static constexpr StringID HeadLampSID = SID("head lamp");
	class HeadLamp : public GameObject
	{
	public:
		HeadLamp(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

	};

	static constexpr StringID PowerPoleSID = SID("power pole");
	class PowerPole : public GameObject
	{
	public:
		PowerPole(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual void OnCharge(real chargeAmount) override;

	private:

	};

	enum class MineralType
	{
		// Elements
		IRON, // Fe - Atomic no. 26 (most common element by mass on Earth)
		SILICON, // Si - Atomic no. 14
		ALUMINUM, // Al - Atomic no. 13
		TIN, // Sn - Atomic no. 50

		// Minerals
		STONE,
		QUARTZ, // SiO2 - second most abundant mineral in Earth's crust
		OLIVINE, // Magnesium iron silicate 2SiO 4

		_NONE
	};

	static const char* MineralTypeStrings[] =
	{
		"iron",
		"silicon",
		"aluminum",
		"tin",

		"stone",
		"quartz",
		"olivine",

		"NONE",
	};

	static_assert(ARRAY_LENGTH(MineralTypeStrings) == (u32)MineralType::_NONE + 1, "MineralTypeStrings length must match MineralType enum");


	const char* MineralTypeToString(MineralType type);
	MineralType MineralTypeFromString(const char* typeStr);

	static constexpr StringID MineralDepositSID = SID("mineral deposit");
	class MineralDeposit : public GameObject
	{
	public:
		MineralDeposit(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual void Initialize() override;

		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		u32 GetMineralRemaining() const;
		PrefabID GetMineralPrefabID();

		// Returns the actual amount of mineral mined
		u32 OnMine(real mineAmount);

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

	private:
		void UpdateMesh();

		u32 m_MineralRemaining = 100;
		MineralType m_Type = MineralType::_NONE;

	};

	static constexpr StringID MinerSID = SID("miner");
	class Miner : public GameObject
	{
	public:
		Miner(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual void Update() override;
		virtual void OnCharge(real chargeAmount) override;
		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		GameObjectStack* GetStackFromInventory(i32 slotIndex);
		i32 GetNextFreeInventorySlot();

		bool IsInventoryFull() const;

		// Returns the number of items which didn't fit
		u32 AddToInventory(const PrefabID& prefabID, i32 stackSize, const GameObjectStack::UserData& userData);
		u32 AddToInventory(GameObjectStack* stack);

		static const u32 INVENTORY_SIZE = 5;

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

	private:

		real m_Charge = 0.0f;
		real m_MaxCharge = 10.0f;
		real m_MineRate = 1.0f;
		real m_PowerDraw = 0.1f;
		real m_MineRadius = 2.0f;
		std::array<GameObjectStack, INVENTORY_SIZE> m_Inventory;

		// Non-serialized fields
		GameObjectID m_NearestMineralDepositID = InvalidGameObjectID;

		real m_LaserEmitterHeight;
		Timer m_MineTimer;
		glm::vec3 m_LaserEndPoint;
		glm::vec4 laserColour;
	};

	static constexpr StringID SpeakerSID = SID("speaker");
	class Speaker : public GameObject
	{
	public:
		Speaker(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabID& prefabIDLoadedFrom = InvalidPrefabID, bool bIsPrefabTemplate = false);

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;
		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		void TogglePlaying();

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

	private:
		std::string m_AudioSourceFileName;
		bool m_bPlaying = true;

		// Non-serialized fields
		AudioSourceID m_SourceID = InvalidAudioSourceID;

	};

} // namespace flex
