#pragma once

#include "Scene/GameObject.hpp"
#include "Spring.hpp"

// Track carts & the liquid boxes they carry
namespace flex
{
	class BaseCart : public GameObject
	{
	public:
		BaseCart(const std::string& name,
			StringID typeID,
			const GameObjectID& gameObjectID,
			const char* meshName,
			const PrefabIDPair& sourcePrefabID,
			bool bPrefabTemplate);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual void Initialize() override;
		virtual void Update() override;
		virtual void Destroy(bool bDetachFromParent /* = true */) override;
		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;
		virtual real GetDrivePower() const;

		void OnTrackMount(TrackID trackID, real newDistAlongTrack);
		void OnTrackDismount();
		// Comes off the track (e.g. when it's broken), falling to the ground
		void Derail();
		bool IsDerailed() const { return m_bDerailed; }

		void SetItemHolding(GameObject* obj);
		void RemoveItemHolding();

		// Advances along track, rotates to face correct direction
		void AdvanceAlongTrack(real dT);

		// Returns velocity
		real UpdatePosition();

		CartID cartID = InvalidCartID;

		TrackID currentTrackID = InvalidTrackID;
		real distAlongTrack = -1.0f;
		real velocityT = 1.0f; // Sign is the direction of travel along the track (+1 towards increasing t)

		real distToRearNeighbor = -1.0f;

		// Non-serialized fields
		real attachThreshold = 1.5f;

		Spring<real> m_TSpringToCartAhead;

		CartChainID chainID = InvalidCartChainID;

		static const char* emptyCartMeshName;

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

		// Falling after derailing
		bool m_bDerailed = false;
		real m_FallSpeed = 0.0f;

	};

	class EmptyCart : public BaseCart
	{
	public:
		EmptyCart(const std::string& name,
			const GameObjectID& gameObjectID,
			const PrefabIDPair& sourcePrefabID,
			bool bPrefabTemplate);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

	};

	class EngineCart : public BaseCart
	{
	public:
		EngineCart(const std::string& name,
			const GameObjectID& gameObjectID,
			const PrefabIDPair& sourcePrefabID,
			bool bPrefabTemplate);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

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

	class MobileLiquidBox final : public GameObject
	{
	public:
		MobileLiquidBox();
		explicit MobileLiquidBox(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

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
} // namespace flex
