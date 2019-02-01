#pragma once

#include "Audio/RandomizedAudioSource.hpp"
#include "Spring.hpp"
#include "Transform.hpp"

class btCollisionShape;

namespace flex
{
	class BaseScene;
	class MeshComponent;
	class BezierCurveList;

	class GameObject
	{
	public:
		GameObject(const std::string& name, GameObjectType type);
		virtual ~GameObject();

		// Returns a new game object which is a direct copy of this object, parented to parent
		// If parent == nullptr then new object will have same parent as this object
		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren);

		static GameObject* CreateObjectFromJSON(const JSONObject& obj, BaseScene* scene, MaterialID overriddenMatID = InvalidMaterialID);

		virtual void Initialize();
		virtual void PostInitialize();
		virtual void Destroy();
		virtual void Update();

		virtual void DrawImGuiObjects();
		virtual void DoImGuiContextMenu(bool bActive);
		virtual bool DoDuplicateGameObjectButton(const char* buttonName);

		virtual Transform* GetTransform();
		virtual const Transform* GetTransform() const;

		/*
		* Returns true if we desire an interaction with the given object
		*/
		virtual bool SetInteractingWith(GameObject* gameObject);
		bool IsBeingInteractedWith() const;

		GameObject* GetObjectInteractingWith();

		JSONObject Serialize(const BaseScene* scene) const;
		void ParseJSON(const JSONObject& obj, BaseScene* scene, MaterialID overriddenMatID = InvalidMaterialID);

		void RemoveRigidBody();

		void SetParent(GameObject* parent);
		GameObject* GetParent();
		void DetachFromParent();

		// Returns a list of objects, starting with the root, going up to this object
		std::vector<GameObject*> GetParentChain();

		// Walks up the tree to the highest parent
		GameObject* GetRootParent();

		GameObject* AddChild(GameObject* child);
		bool RemoveChild(GameObject* child);
		const std::vector<GameObject*>& GetChildren() const;

		bool HasChild(GameObject* child, bool bCheckChildrensChildren);

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

		// If bIncludingChildren is true, true will be returned if this or any children are visible in scene explorer
		bool IsVisibleInSceneExplorer(bool bIncludingChildren = false) const;
		void SetVisibleInSceneExplorer(bool bVisibleInSceneExplorer);

		bool HasUniformScale() const;
		void SetUseUniformScale(bool bUseUniformScale, bool bEnforceImmediately);

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

		GameObjectType GetType() const;

		void AddSelfAndChildrenToVec(std::vector<GameObject*>& vec);
		void RemoveSelfAndChildrenToVec(std::vector<GameObject*>& vec);

		// Filled if this object is a trigger
		std::vector<GameObject*> overlappingObjects;

	protected:
		friend BaseScene;

		static const char* s_DefaultNewGameObjectName;

		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID);
		virtual void SerializeUniqueFields(JSONObject& parentObject) const;

		void CopyGenericFields(GameObject* newGameObject, GameObject* parent, bool bCopyChildren);

		// Returns a string containing our name with a "_xx" post-fix where xx is the next highest index or 00

		std::string m_Name;

		std::vector<std::string> m_Tags;

		Transform m_Transform;
		RenderID m_RenderID = InvalidRenderID;

		GameObjectType m_Type = GameObjectType::_NONE;

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

		bool m_bBeingInteractedWith = false;

		// Editor only
		bool m_bUniformScale = false;

		std::string m_PrefabName;

		/*
		* Will point at the player we're interacting with, or the object if we're the player
		*/
		GameObject* m_ObjectInteractingWith = nullptr;

		i32 m_SiblingIndex = 0;

		btCollisionShape* m_CollisionShape = nullptr;
		RigidBody* m_RigidBody = nullptr;

		GameObject* m_Parent = nullptr;
		std::vector<GameObject*> m_Children;

		MeshComponent* m_MeshComponent = nullptr;

		static AudioSourceID s_BunkSound;
		static RandomizedAudioSource s_SqueakySounds;

	};

	// Child classes

	class DirectionalLight : public GameObject
	{
	public:
		DirectionalLight();
		DirectionalLight(const std::string& name);

		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void DrawImGuiObjects() override;

		bool operator==(const DirectionalLight& other);

		void SetPos(const glm::vec3& pos);
		glm::vec3 GetPos() const;
		void SetRot(const glm::quat& rot);
		glm::quat GetRot() const;

		glm::vec4 color = VEC4_ONE;
		real brightness = 1.0f;

		real shadowDarkness = 1.0f;
		bool bCastShadow = true;
		real shadowMapNearPlane = -80.0f;
		real shadowMapFarPlane = 100.0f;
		real shadowMapZoom = 30.0f;

		// DEBUG: (just for preview in ImGui)
		u32 shadowTextureID = 0;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;
	};

	class PointLight : public GameObject
	{
	public:
		PointLight(BaseScene* scene);
		PointLight(const std::string& name);

		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void DrawImGuiObjects() override;

		bool operator==(const PointLight& other);

		void SetPos(const glm::vec3& pos);
		glm::vec3 GetPos() const;

		glm::vec4 color = VEC4_ONE;
		real brightness = 500.0f;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;
	};

	class Valve : public GameObject
	{
	public:
		Valve(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

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
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class RisingBlock : public GameObject
	{
	public:
		RisingBlock(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

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
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class GlassPane : public GameObject
	{
	public:
		GlassPane(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		bool bBroken = false;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class ReflectionProbe : public GameObject
	{
	public:
		ReflectionProbe(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		virtual void PostInitialize() override;

		MaterialID captureMatID = 0;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class Skybox : public GameObject
	{
	public:
		Skybox(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class EngineCart;

	class Cart : public GameObject
	{
	public:
		Cart(CartID cartID, GameObjectType type = GameObjectType::CART);
		Cart(CartID cartID, const std::string& name, GameObjectType type = GameObjectType::CART, const char* meshName = emptyCartMeshName);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		virtual void DrawImGuiObjects() override;
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
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class EngineCart : public Cart
	{
	public:
		EngineCart(CartID cartID);
		EngineCart(CartID cartID, const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		virtual void Update() override;
		virtual void DrawImGuiObjects() override;
		virtual real GetDrivePower() const override;


		real moveDirection = 1.0f; // -1.0f or 1.0f
		real powerRemaining = 1.0f;

		real powerDrainMultiplier = 0.1f;
		real speed = 0.1f;

		static const char* engineMeshName;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class MobileLiquidBox : public GameObject
	{
	public:
		MobileLiquidBox();
		MobileLiquidBox(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		virtual void DrawImGuiObjects() override;

		bool bInCart = false;
		real liquidAmount = 0.0f;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class Terminal : public GameObject
	{
	public:
		Terminal();
		Terminal(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		virtual void Update() override;
		virtual bool SetInteractingWith(GameObject* gameObject) override;

		std::string str;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, MaterialID matID) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

} // namespace flex
