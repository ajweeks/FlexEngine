#pragma once

IGNORE_WARNINGS_PUSH
#include <BulletDynamics/Vehicle/btRaycastVehicle.h>
IGNORE_WARNINGS_POP

#include "Scene/GameObject.hpp"

// Drivable raycast vehicle
namespace flex
{
	class Vehicle final : public GameObject
	{
	public:
		Vehicle(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual GameObject* CopySelf(
			GameObject* parent = nullptr,
			CopyFlags copyFlags = CopyFlags::ALL,
			std::string* optionalName = nullptr,
			const GameObjectID& optionalGameObjectID = InvalidGameObjectID) override;

		virtual void Initialize() override;
		virtual void PostInitialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;

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
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

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
} // namespace flex
