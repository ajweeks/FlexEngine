#pragma once

#include "Scene/GameObject.hpp"
#include "Timer.hpp"

// Power & mining objects: batteries, solar panels, head lamps, power poles, mineral deposits, miners, speakers & pickaxes
namespace flex
{
	class Battery final : public GameObject
	{
	public:
		Battery();
		explicit Battery(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

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

	class SolarPanel : public GameObject
	{
	public:
		SolarPanel(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual void Update() override;

	private:
		real m_ChargeRate = 10.0f;
		real m_Efficiency = 1.0f;

	};

	class HeadLamp : public GameObject
	{
	public:
		HeadLamp(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

	};

	class PowerPole : public GameObject
	{
	public:
		PowerPole(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

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

	class MineralDeposit : public GameObject
	{
	public:
		MineralDeposit(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual void Initialize() override;

		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		u32 GetMineralRemaining() const;
		MineralType GetMineralType() const;
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

	class Miner : public GameObject
	{
	public:
		Miner(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual void Update() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
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
		void ComputeNewTargetPos();

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
		Timer m_MineCooldownTimer;
		glm::vec3 m_LaserDirection;
		glm::vec4 laserColour;
		glm::vec3 m_MineTargetLocation;

		ParticleEmitterID m_MiningSparksEmitterID = InvalidParticleEmitterID;
		ParticleEmitterID m_MiningDustEmitterID = InvalidParticleEmitterID;
	};

	class Speaker : public GameObject
	{
	public:
		Speaker(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

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

	class PickAxe : public GameObject
	{
	public:
		PickAxe(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual void Update() override;
		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

	private:
		real m_UsageRemaining = 1.0f;
		MineralType m_Material = MineralType::_NONE;

		static real GetMineSpeed(MineralType material);

		// Non-serialized fields
		AudioSourceID m_SourceID = InvalidAudioSourceID;

	};
} // namespace flex
