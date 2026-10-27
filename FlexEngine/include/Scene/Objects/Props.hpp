#pragma once

#include "Scene/GameObject.hpp"
#include "Audio/AudioCue.hpp"

// Simple interactive & static props: valves, rising blocks, glass panes, reflection probes, skyboxes, dropped items
namespace flex
{
	class EditorObject final : public GameObject
	{
	public:
		EditorObject(const std::string& name);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual void Destroy(bool bDetachFromParent = true) override;

	};

	class DroppedItem final : public GameObject
	{
	public:
		DroppedItem(const PrefabID& prefabID, i32 stackSize);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;

		virtual void Update() override;

		bool CanBePickedUp() const;
		void OnPickedUp();
		// Moves the item towards pos (speeding up as it goes). Returns true once it's arrived.
		bool FlyTowards(const glm::vec3& pos);

		PrefabID prefabID = InvalidPrefabID;
		i32 stackSize = 0;
		real secondsAlive = 0.0f;
		real bobAmount = 0.0f;
		real turnSpeed = 6.0f;
		glm::vec3 initialVel = VEC3_ZERO;
		// Seconds before the player can pick it up (so items they drop aren't picked straight back up)
		real pickupDelay = MIN_TIME_BEFORE_PICKUP;
		real flyTime = 0.0f;

		static const real MIN_TIME_BEFORE_PICKUP;

	};

	class Valve final : public GameObject
	{
	public:
		explicit Valve(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

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

	class RisingBlock final : public GameObject
	{
	public:
		explicit RisingBlock(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

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

	class GlassPane final : public GameObject
	{
	public:
		explicit GlassPane(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

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

	class ReflectionProbe final : public GameObject
	{
	public:
		explicit ReflectionProbe(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		virtual void PostInitialize() override;

		MaterialID captureMatID = 0;
	};

	// A placeable bed. Interacting at night sleeps until morning.
	class Bed final : public GameObject
	{
	public:
		Bed(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual void Initialize() override;

		// Sleeps until morning if it's night, otherwise tells the player they aren't tired
		void Sleep(Player* player);

		// Time of day (see BaseScene) the player wakes up at
		static constexpr real MORNING_TIME_OF_DAY = 0.78f;

	private:
		void CreateMeshes();
	};

	class Skybox final : public GameObject
	{
	public:
		explicit Skybox(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		void ProcedurallyInitialize(MaterialID matID);

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

		void InternalInit(MaterialID matID);
	};
} // namespace flex
