#pragma once

#include "Audio/AudioCue.hpp"
#include "Callbacks/InputCallbacks.hpp"
#include "Graphics/RendererTypes.hpp"
#include "Graphics/VertexBufferData.hpp" // For VertexBufferDataCreateInfo
#include "Helpers.hpp"
#include "Spring.hpp"
#include "Transform.hpp"
#include "Time.hpp"

#include <set>

class btCollisionShape;

namespace flex
{
	class BaseScene;
	class BezierCurveList;
	class Mesh;
	class MeshComponent;
	class Socket;
	class TerminalCamera;
	class Wire;

	namespace VM
	{
		class VirtualMachine;
	}

	class GameObject
	{
	public:
		GameObject(const std::string& name, GameObjectType type);
		virtual ~GameObject();

		// Returns a new game object which is a direct copy of this object, parented to parent
		// If parent == nullptr then new object will have same parent as this object
		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren);

		static GameObject* CreateObjectFromJSON(const JSONObject& obj, BaseScene* scene, i32 fileVersion);

		virtual void Initialize();
		virtual void PostInitialize();
		virtual void Destroy();
		virtual void Update();

		virtual void DrawImGuiObjects();
		// Returns true if this object was deleted or duplicated
		virtual bool DoImGuiContextMenu(bool bActive);
		virtual bool DoDuplicateGameObjectButton(const char* buttonName);

		virtual Transform* GetTransform();
		virtual const Transform* GetTransform() const;

		virtual void OnTransformChanged();

		virtual bool AllowInteractionWith(GameObject* gameObject);
		virtual void SetInteractingWith(GameObject* gameObject);
		bool IsBeingInteractedWith() const;

		GameObject* GetObjectInteractingWith();

		JSONObject Serialize(const BaseScene* scene) const;
		void ParseJSON(const JSONObject& obj, BaseScene* scene, i32 fileVersion, MaterialID overriddenMatID = InvalidMaterialID);

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
		u32 GetChildCountOfType(GameObjectType objType, bool bRecurse);

		template<class T>
		void GetChildrenOfType(GameObjectType objType, bool bRecurse, std::vector<T*>& children)
		{
			if (m_Type == objType)
			{
				children.push_back((T*)this);
			}

			if (bRecurse)
			{
				for (GameObject* child : m_Children)
				{
					child->GetChildrenOfType(objType, bRecurse, children);
				}
			}
		}


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

		std::string GetName() const;
		void SetName(const std::string& newName);

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

		btCollisionShape* SetCollisionShape(btCollisionShape* collisionShape);
		btCollisionShape* GetCollisionShape() const;

		RigidBody* SetRigidBody(RigidBody* rigidBody);
		RigidBody* GetRigidBody() const;

		Mesh* GetMesh();
		Mesh* SetMesh(Mesh* mesh);

		bool CastsShadow() const;
		void SetCastsShadow(bool bCastsShadow);

		// Called when another object has begun to overlap
		void OnOverlapBegin(GameObject* other);
		// Called when another object is no longer overlapping
		void OnOverlapEnd(GameObject* other);

		GameObjectType GetType() const;

		void AddSelfAndChildrenToVec(std::vector<GameObject*>& vec);
		void RemoveSelfAndChildrenToVec(std::vector<GameObject*>& vec);

		void SetNearbyInteractable(GameObject* nearbyInteractable);

		// Filled if this object is a trigger
		std::vector<GameObject*> overlappingObjects;

		// Signals that connected objects get sent
		std::vector<i32> outputSignals;
		std::vector<Socket*> sockets;

	protected:
		friend BaseScene;

		static const char* s_DefaultNewGameObjectName;

		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs);
		virtual void SerializeUniqueFields(JSONObject& parentObject) const;

		void CopyGenericFields(GameObject* newGameObject, GameObject* parent, bool bCopyChildren);

		void SetOutputSignal(i32 slotIdx, i32 value);

		// Returns a string containing our name with a "_xx" post-fix where xx is the next highest index or 00

		std::string m_Name;

		std::vector<std::string> m_Tags;

		Transform m_Transform;

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

		bool m_bCastsShadow = true;

		// Editor only
		bool m_bUniformScale = false;

		std::string m_PrefabName;

		/*
		* Will point at the player we're interacting with, or the object if we're the player
		*/
		GameObject* m_ObjectInteractingWith = nullptr;

		GameObject* m_NearbyInteractable = nullptr;

		i32 m_SiblingIndex = 0;

		btCollisionShape* m_CollisionShape = nullptr;
		RigidBody* m_RigidBody = nullptr;

		GameObject* m_Parent = nullptr;
		std::vector<GameObject*> m_Children;

		Mesh* m_Mesh = nullptr;

		static AudioSourceID s_BunkSound;
		static AudioCue s_SqueakySounds;

	private:
		void DrawImGuiForSelfInternal();

	};

	// Child classes

	class DirectionalLight : public GameObject
	{
	public:
		DirectionalLight();
		explicit DirectionalLight(const std::string& name);

		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void Update() override;
		virtual void DrawImGuiObjects() override;
		virtual void SetVisible(bool bVisible, bool bEffectChildren /* = true */) override;
		virtual void OnTransformChanged() override;

		bool operator==(const DirectionalLight& other);

		void SetPos(const glm::vec3& newPos);
		glm::vec3 GetPos() const;
		void SetRot(const glm::quat& newRot);
		glm::quat GetRot() const;

		DirLightData data;

		// DEBUG:
		glm::vec3 pos = VEC3_ZERO;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;
	};

	class PointLight : public GameObject
	{
	public:
		explicit PointLight(BaseScene* scene);
		explicit PointLight(const std::string& name);

		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void Update() override;
		virtual void DrawImGuiObjects() override;
		virtual void SetVisible(bool bVisible, bool bEffectChildren /* = true */) override;
		virtual void OnTransformChanged() override;

		bool operator==(const PointLight& other);

		void SetPos(const glm::vec3& pos);
		glm::vec3 GetPos() const;

		PointLightData data;
		PointLightID ID = InvalidPointLightID;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;
	};

	class Valve : public GameObject
	{
	public:
		explicit Valve(const std::string& name);

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
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class RisingBlock : public GameObject
	{
	public:
		explicit RisingBlock(const std::string& name);

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
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class GlassPane : public GameObject
	{
	public:
		explicit GlassPane(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		bool bBroken = false;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class ReflectionProbe : public GameObject
	{
	public:
		explicit ReflectionProbe(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		virtual void PostInitialize() override;

		MaterialID captureMatID = 0;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class Skybox : public GameObject
	{
	public:
		explicit Skybox(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		void ProcedurallyInitialize(MaterialID matID);

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

		void InternalInit(MaterialID matID);
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
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class EngineCart : public Cart
	{
	public:
		explicit EngineCart(CartID cartID);
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
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	class MobileLiquidBox : public GameObject
	{
	public:
		MobileLiquidBox();
		explicit MobileLiquidBox(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		virtual void DrawImGuiObjects() override;

		bool bInCart = false;
		real liquidAmount = 0.0f;

	protected:
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	};

	template<class T>
	struct ThreadSafeArray
	{
		ThreadSafeArray()
		{
		}

		explicit ThreadSafeArray<T>(u32 inSize)
		{
			size = inSize;
			t = new T[inSize];
		}

		~ThreadSafeArray()
		{
			delete[] t;
		}

		volatile T& operator[](u32 index) volatile
		{
			return t[index];
		}

		const volatile T& operator[](u32 index) volatile const
		{
			return t[index];
		}

		u32 Size()volatile const
		{
			return size;
		}

		u32 size;
		volatile T* t = nullptr;
	};

	struct ThreadData
	{
		void* criticalSection = nullptr;
		volatile bool running = true;
	};

	class GerstnerWave : public GameObject
	{
	public:
		explicit GerstnerWave(const std::string& name);

		virtual void Initialize() override;
		virtual void Update() override;
		virtual void Destroy() override;
		void AddWave();
		void RemoveWave(i32 index);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		virtual void DrawImGuiObjects() override;

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
			std::vector<WaveInfo> const* waves;
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
		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

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

		OceanData oceanData;

		void* criticalSection = nullptr;

		MaterialID m_WaveMaterialID;

		std::vector<WaveTessellationLOD> waveTessellationLODs;
		std::vector<WaveSamplingLOD> waveSamplingLODs;

		// TODO: Rename to wave contributions?
		std::vector<WaveInfo> waves;
		WaveInfo const* soloWave = nullptr;

		std::vector<WaveChunk> waveChunks;

		bool m_bPinCenter = false;
		glm::vec3 m_PinnedPos;

		VertexBufferDataCreateInfo m_VertexBufferCreateInfo;
		std::vector<u32> m_Indices;

		GameObject* bobber = nullptr;
		Spring<real> bobberTarget;

		RollingAverage<ms> avgWaveUpdateTime;

		u32 DEBUG_lastUsedVertCount = 0;

		ThreadData threadUserData;
	};

	bool operator==(const GerstnerWave::WaveInfo& lhs, const GerstnerWave::WaveInfo& rhs);

	// TODO: MOVE!!
	static volatile u32 workQueueLock = 0;
	static volatile u32 workQueueEntriesCreated = 0;
	static volatile u32 workQueueEntriesClaimed = 0;
	static volatile u32 workQueueEntriesCompleted = 0;

#define SIMD_WAVES 1

	void* ThreadUpdate(void* inData);

	GerstnerWave::WaveChunk const* GetChunkAtPos(const glm::vec2& pos, const std::vector<GerstnerWave::WaveChunk>& waveChunks, real size);
	GerstnerWave::WaveTessellationLOD const* GetTessellationLOD(u32 lodLevel, const std::vector<GerstnerWave::WaveTessellationLOD>& waveTessellationLODs);
	u32 MapVertIndexAcrossLODs(u32 vertIndex, GerstnerWave::WaveTessellationLOD const* lod0, GerstnerWave::WaveTessellationLOD const* lod1);

	class Blocks : public GameObject
	{
	public:
		explicit Blocks(const std::string& name);

		virtual void Update() override;

	protected:

	};

	// Connects terminals to other things to transmit information
	class Wire : public GameObject
	{
	public:
		Wire(const std::string& name);

		virtual void Destroy() override;

		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

		void PlugIn(Socket* socket);
		void Unplug(Socket* socket);

		virtual bool AllowInteractionWith(GameObject* gameObject) override;
		virtual void SetInteractingWith(GameObject* gameObject) override;

		Socket* socket0 = nullptr;
		Socket* socket1 = nullptr;

		glm::vec3 startPoint;
		glm::vec3 endPoint;
	};

	// Connect wires to objects
	class Socket : public GameObject
	{
	public:
		Socket(const std::string& name);

		virtual void Destroy() override;

		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

		virtual bool AllowInteractionWith(GameObject* gameObject) override;
		virtual void SetInteractingWith(GameObject* gameObject) override;

		GameObject* parent = nullptr;
		Wire* connectedWire = nullptr;
		i32 slotIdx = 0;
	};

	// TODO: Add scene base class
	class PluggablesSystem
	{
	public:
		void Initialize();
		void Destroy();

		void Update();

		i32 GetReceivedSignal(Socket* socket);

		Wire* AddWire(Socket* socket0 = nullptr, Socket* socket1 = nullptr);
		bool DestroySocket(Socket* socket);
		bool DestroyWire(Wire* wire);

		Socket* AddSocket(const std::string& name, i32 slotIdx = 0, Wire* connectedWire = nullptr);

		std::vector<Wire*> wires;
		std::vector<Socket*> sockets;

	private:
		bool RemoveSocket(Socket* socket);

		// TODO: Serialization (requires ObjectIDs)
		// TODO: Use WirePool

	};

	class Terminal : public GameObject
	{
	public:
		Terminal();
		explicit Terminal(const std::string& name);

		virtual void Initialize() override;
		virtual void PostInitialize() override;
		virtual void Destroy() override;
		virtual void Update() override;

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		virtual bool AllowInteractionWith(GameObject* gameObject) override;

		void SetCamera(TerminalCamera* camera);

		void DrawTerminalUI();

		static const i32 MAX_OUTPUT_COUNT = 4;

		std::vector<Wire*> wireSlots;

	protected:
		void TypeChar(char c);
		void DeleteChar(bool bDeleteUpToNextBreak = false); // (backspace)
		void DeleteCharInFront(bool bDeleteUpToNextBreak = false); // (delete)
		void Clear();

		void MoveCursorToStart();
		void MoveCursorToStartOfLine();
		void MoveCursorToEnd();
		void MoveCursorToEndOfLine();
		void MoveCursorLeft(bool bSkipToNextBreak = false);
		void MoveCursorRight(bool bSkipToNextBreak = false);
		void MoveCursorUp();
		void MoveCursorDown();

		void ClampCursorX();

		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

	private:
		friend TerminalCamera;

		EventReply OnKeyEvent(KeyCode keyCode, KeyAction action, i32 modifiers);
		KeyEventCallback<Terminal> m_KeyEventCallback;

		bool SkipOverChar(char c);
		i32 GetIdxOfNextBreak(i32 y, i32 startX);
		i32 GetIdxOfPrevBreak(i32 y, i32 startX);

		void ParseCode();
		void EvaluateCode();

		VM::VirtualMachine* m_VM = nullptr;

		std::vector<std::string> lines;

		real m_LineHeight = 1.0f;
		real m_LetterScale = 0.04f;

		glm::vec2i cursor;
		// Keeps track of the cursor x to be able to position the cursor correctly
		// after moving from a long line, over a short line, onto a longer line again
		i32 cursorMaxX = 0;

		// Non-serialized fields:
		TerminalCamera* m_Camera = nullptr;
		const i32 m_CharsWide = 45;

		const sec m_CursorBlinkRate = 0.6f;
		sec m_CursorBlinkTimer = 0.0f;

	};

	class ParticleSystem : public GameObject
	{
	public:
		explicit ParticleSystem(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		virtual void Update() override;
		virtual void Destroy() override;

		virtual void DrawImGuiObjects() override;

		virtual void OnTransformChanged() override;

		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

		glm::mat4 model;
		real scale;
		ParticleSimData data;
		bool bEnabled;
		MaterialID simMaterialID = InvalidMaterialID;
		MaterialID renderingMaterialID = InvalidMaterialID;
		ParticleSystemID ID = InvalidParticleSystemID;

	private:
		void UpdateModelMatrix();

	};

	// TODO: Rename to landscape generator
	class ChunkGenerator : public GameObject
	{
	public:
		explicit ChunkGenerator(const std::string& name);

		virtual GameObject* CopySelfAndAddToScene(GameObject* parent, bool bCopyChildren) override;

		virtual void Initialize() override;
		virtual void PostInitialize() override;
		virtual void Update() override;
		virtual void Destroy() override;

		virtual void DrawImGuiObjects() override;

		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

		u32 VertCountPerChunkAxis = 32;
		real ChunkSize = 16.0f;
		real MaxHeight = 3.0f;

	private:
		void GenerateGradients();
		void GenerateChunk(const glm::ivec2& index);
		void DestroyAllChunks();
		real SampleTerrain(const glm::vec2& pos);
		real SampleNoise(const glm::vec2& pos, real octave, u32 octaveIdx);

		MaterialID m_TerrainMatID = InvalidMaterialID;
		std::map<glm::vec2i, MeshComponent*, Vec2iCompare> m_Meshes; // Chunk index to mesh

		real nscale = 1.0f;
		real m_LoadedChunkRadius = 100.0f;

		std::set<glm::vec2i, Vec2iCompare> m_ChunksToLoad;
		std::set<glm::vec2i, Vec2iCompare> m_ChunksToDestroy;

		const ns m_CreationBudgetPerFrame = Time::ConvertFormatsConstexpr(1.0f, Time::Format::MILLISECOND, Time::Format::NANOSECOND);
		const ns m_DeletionBudgetPerFrame = Time::ConvertFormatsConstexpr(0.5f, Time::Format::MILLISECOND, Time::Format::NANOSECOND);

		bool m_UseManualSeed = true;
		i32 m_ManualSeed = 0;

		real m_OctaveScale = 1.0f;
		real m_BaseOctave = 1.0f;
		u32 m_NumOctaves = 1;

		bool m_bHighlightGrid = false;
		bool m_bDisplayTables = false;

		bool m_bPinCenter = false;
		glm::vec3 m_PinnedPos;

		glm::vec3 m_LowCol;
		glm::vec3 m_MidCol;
		glm::vec3 m_HighCol;

		std::vector<std::vector<glm::vec2>> m_RandomTables;
		u32 m_BasePerlinTableWidth = 16;

		std::vector<TextureID> m_TableTextureIDs;

		i32 m_IsolateOctave = -1;

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
		real invMass = 0.0f;
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

			_NONE
		};

		Constraint(i32 index0, i32 index1, real stiffness, EqualityType equalityType, Type type) :
			stiffness(stiffness),
			equalityType(equalityType),
			type(type)
		{
			pointIndices[0] = index0;
			pointIndices[1] = index1;
		}

		i32 pointIndices[2];
		real stiffness;
		EqualityType equalityType;
		Type type;
	};

	struct DistanceConstraint : public Constraint
	{
		DistanceConstraint(i32 index0, i32 index1, real stiffness, real targetDistance);

		real targetDistance;
	};

	class PBD : public GameObject
	{
	public:
		PBD();

		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void Update() override;

		virtual void ParseUniqueFields(const JSONObject& parentObject, BaseScene* scene, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeUniqueFields(JSONObject& parentObject) const override;

		virtual void DrawImGuiObjects() override;

		static ms TIMESTEP;

	private:
		void Draw();

		u32 m_SolverIterationCount;
		bool m_bPaused = false;
		bool m_bSingleStep = false;

		ms m_LastUpdateTime;
		sec m_AccumulatedSec = 0.0f;
		ms m_UpdateDuration = 0.0f;

		std::vector<Point*> points;
		std::vector<Constraint*> constraints;
		std::vector<glm::vec3> predictedPositions;

		std::vector<glm::vec3> initialPositions;

	};

} // namespace flex
