#include "stdafx.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <glm/gtx/norm.hpp> // for distance2
#include <glm/gtx/quaternion.hpp> // for rotate
#include <glm/gtx/vector_angle.hpp> // for angle
#include <glm/ext/matrix_transform.hpp> // for rotate

#include <imgui/imgui_internal.h> // For PushItemFlag

IGNORE_WARNINGS_POP

#include "Scene/Objects/Vehicle.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/Objects/Lights.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	static const char* TireNames[] = { "FL", "FR", "RL", "RR", "None" };
	static const char* BrakeLightNames[] = { "Left", "Right" };
	static_assert((ARRAY_LENGTH(TireNames) - 1) == (i32)Vehicle::Tire::_NONE, "TireNames length does not match number of entires in Tire enum");

	Vehicle::Vehicle(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, VehicleSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate),
		m_WheelSlipHisto(RollingAverage<real>(256, SamplingType::CONSTANT))
	{
		m_bInteractable = true;
		// Vehicles can be picked up into the player's inventory & placed again
		m_bItemizable = true;

		for (i32 i = 0; i < (i32)SoundEffect::_COUNT; ++i)
		{
			m_SoundEffects[i] = SoundClip_LoopingSimple(VehicleSoundEffectNames[i], InvalidStringID, false);
			m_SoundEffectSIDs[i] = InvalidStringID;
		}
	}

	PropertyCollection* Vehicle::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();
		return collection;
	}

	GameObject* Vehicle::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);
		Vehicle* newGameObject = new Vehicle(newObjectName, newGameObjectID);

		CopyGenericFields(newGameObject, parent, copyFlags);

		// Children now exist
		for (i32 i = 0; i < m_TireCount; ++i)
		{
			newGameObject->m_TireIDs[i] = GameObjectID();
		}

		for (i32 i = 0; i < 2; ++i)
		{
			newGameObject->m_BrakeLightIDs[i] = GameObjectID();
		}

		// Temporarily set sibling indices as if these objects are both root objects (this will
		// be overwritten by the proper values soon)
		UpdateSiblingIndices(0);
		newGameObject->UpdateSiblingIndices(0);

		FixupPrefabTemplateIDs(newGameObject);

		for (i32 i = 0; i < (i32)SoundEffect::_COUNT; ++i)
		{
			newGameObject->m_SoundEffectSIDs[i] = m_SoundEffectSIDs[i];
			// m_SoundEffects array will be set in PostInitialize
		}

		newGameObject->m_MaxEngineForce = m_MaxEngineForce;
		newGameObject->m_EngineAccel = m_EngineAccel;
		newGameObject->m_SuspensionStiffness = m_SuspensionStiffness;
		newGameObject->m_SuspensionDamping = m_SuspensionDamping;
		newGameObject->m_SuspensionCompression = m_SuspensionCompression;
		newGameObject->m_SuspensionRestLength = m_SuspensionRestLength;
		newGameObject->m_WheelFriction = m_WheelFriction;
		newGameObject->m_RollInfluence = m_RollInfluence;

		return newGameObject;
	}

	void Vehicle::Initialize()
	{
		GameObject::Initialize();

		m_pLinearVelocity = ToBtVec3(VEC3_ZERO);

		BaseScene* scene = g_SceneManager->CurrentScene();
		GameObject* tireFL = scene->GetGameObject(m_TireIDs[(u32)Tire::FL]);
		GameObject* brakeLightL = scene->GetGameObject(m_BrakeLightIDs[0]);
		GameObject* brakeLightR = scene->GetGameObject(m_BrakeLightIDs[1]);
		if (brakeLightL != nullptr && brakeLightR != nullptr &&
			brakeLightL->GetTypeID() == SpotLightSID &&
			brakeLightR->GetTypeID() == SpotLightSID)
		{
			((SpotLight*)brakeLightL)->SetVisible(false, true);
			((SpotLight*)brakeLightR)->SetVisible(false, true);
		}
		else
		{
			PrintError("Expected vehicle brake light objects to be spot lights\n");
		}

		if (m_Mesh != nullptr)
		{
			m_GlassMatID = m_Mesh->GetSubMesh(0)->GetMaterialID();
			m_CarPaintMatID = m_Mesh->GetSubMesh(1)->GetMaterialID();
			m_BrakeLightMatID = m_Mesh->GetSubMesh(2)->GetMaterialID();
			m_ReverseLightMatID = m_Mesh->GetSubMesh(3)->GetMaterialID();
			if (tireFL != nullptr)
			{
				m_TireMatID = tireFL->GetMesh()->GetSubMesh(0)->GetMaterialID();
				m_SpokeMatID = tireFL->GetMesh()->GetSubMesh(1)->GetMaterialID();
			}

			m_InitialBrakeLightMatEmissive = g_Renderer->GetMaterial(m_BrakeLightMatID)->constEmissive;
			m_InitialReverseLightMatEmissive = g_Renderer->GetMaterial(m_ReverseLightMatID)->constEmissive;

			m_ActiveBrakeLightMatEmissive = glm::min(m_InitialBrakeLightMatEmissive + glm::vec4(0.2f, 0.0f, 0.0f, 0.0f), VEC4_ONE);
			m_ActiveReverseLightMatEmissive = glm::min(m_InitialReverseLightMatEmissive + glm::vec4(0.4f), VEC4_ONE);
		}

		CreateRigidBody();
	}

	void Vehicle::CreateRigidBody()
	{
		if (m_RigidBody != nullptr)
		{
			BaseScene* scene = g_SceneManager->CurrentScene();
			GameObject* tireFL = scene->GetGameObject(m_TireIDs[(u32)Tire::FL]);
			GameObject* tireFR = scene->GetGameObject(m_TireIDs[(u32)Tire::FR]);
			GameObject* tireRL = scene->GetGameObject(m_TireIDs[(u32)Tire::RL]);
			GameObject* tireRR = scene->GetGameObject(m_TireIDs[(u32)Tire::RR]);

			m_tuning = {};

			btDiscreteDynamicsWorld* dynamicsWorld = scene->GetPhysicsWorld()->GetWorld();

			btRigidBody* chassisRB = m_RigidBody->GetRigidBodyInternal();

			m_VehicleRaycaster = new btDefaultVehicleRaycaster(dynamicsWorld);
			m_Vehicle = new btRaycastVehicle(m_tuning, chassisRB, m_VehicleRaycaster);

			chassisRB->setActivationState(DISABLE_DEACTIVATION);

			dynamicsWorld->addVehicle(m_Vehicle);

			m_Vehicle->setCoordinateSystem(0, 1, 2);

			btScalar suspensionRestLength(m_SuspensionRestLength);
			btVector3 wheelDirectionCS0(0, -1, 0);
			btVector3 wheelAxleCS(-1, 0, 0);

			btVector3 connecitonPoint = ToBtVec3(tireFL->GetTransform()->GetLocalPosition());
			bool bIsFrontWheel = true;
			m_Vehicle->addWheel(connecitonPoint, wheelDirectionCS0, wheelAxleCS, suspensionRestLength, m_WheelRadius, m_tuning, bIsFrontWheel);
			connecitonPoint = ToBtVec3(tireFR->GetTransform()->GetLocalPosition());
			m_Vehicle->addWheel(connecitonPoint, wheelDirectionCS0, wheelAxleCS, suspensionRestLength, m_WheelRadius, m_tuning, bIsFrontWheel);
			bIsFrontWheel = false;
			connecitonPoint = ToBtVec3(tireRL->GetTransform()->GetLocalPosition());
			m_Vehicle->addWheel(connecitonPoint, wheelDirectionCS0, wheelAxleCS, suspensionRestLength, m_WheelRadius, m_tuning, bIsFrontWheel);
			connecitonPoint = ToBtVec3(tireRR->GetTransform()->GetLocalPosition());
			m_Vehicle->addWheel(connecitonPoint, wheelDirectionCS0, wheelAxleCS, suspensionRestLength, m_WheelRadius, m_tuning, bIsFrontWheel);

			for (i32 i = 0; i < m_Vehicle->getNumWheels(); ++i)
			{
				btWheelInfo& wheel = m_Vehicle->getWheelInfo(i);
				wheel.m_suspensionStiffness = m_SuspensionStiffness;
				wheel.m_wheelsDampingRelaxation = m_SuspensionDamping;
				wheel.m_wheelsDampingCompression = m_SuspensionCompression;
				wheel.m_frictionSlip = m_WheelFriction;
				wheel.m_rollInfluence = m_RollInfluence;
			}
		}
	}

	void Vehicle::PostInitialize()
	{
		if (m_RigidBody != nullptr)
		{
			m_RigidBody->SetInterpolation(true, true);
		}

		for (i32 i = 0; i < (i32)SoundEffect::_COUNT; ++i)
		{
			if (m_SoundEffectSIDs[i] != InvalidStringID)
			{
				SetSoundEffectSID((SoundEffect)i, m_SoundEffectSIDs[i]);
			}
		}

		GameObject::PostInitialize();
	}

	void Vehicle::Destroy(bool bDetachFromParent /* = true */)
	{
		if (g_SceneManager->HasSceneLoaded())
		{
			BaseScene* scene = g_SceneManager->CurrentScene();
			btDiscreteDynamicsWorld* dynamicsWorld = scene->GetPhysicsWorld()->GetWorld();
			dynamicsWorld->removeVehicle(m_Vehicle);
		}

		GameObject::Destroy(bDetachFromParent);
	}

	void Vehicle::Update()
	{
		PROFILE_AUTO("Vehicle Update");

		GameObject::Update();

		if (m_RigidBody == nullptr)
		{
			return;
		}

		btRigidBody* rb = m_RigidBody->GetRigidBodyInternal();
		if (rb == nullptr)
		{
			return;
		}

		const btVector3 linearVel = rb->getLinearVelocity();
		const btVector3 linearAccel = (linearVel - m_pLinearVelocity) / g_DeltaTime;
		const real forwardVel = glm::dot(m_Transform.GetForward(), ToVec3(linearVel));
		//const real forwardAccel = glm::dot(m_Transform.GetForward(), ToVec3(linearVel));
		// How much our acceleration opposes our current velocity
		real slowingVelocity = glm::clamp(1.0f - glm::min(glm::dot(ToVec3(linearAccel), ToVec3(linearVel)) / linearVel.length(), 0.0f), 0.0f, 1.0f);

		//btVector3 force = btVector3(0.0f, 0.0f, 0.0f);

		real engineForceSlowScale = (1.0f - g_DeltaTime * m_EngineForceDecelerationSpeed);
		real steeringSlowScale = (1.0f - g_DeltaTime * STEERING_SLOW_FACTOR);

		real maxSteerVel = 30.0f;
		real steeringScale = Lerp(0.45f, 1.0f, 1.0f - glm::clamp(forwardVel / maxSteerVel, 0.0f, 1.0f));

		if (m_bOccupied && g_CameraManager->CurrentCamera()->bIsGameplayCam)
		{
			// The faster we get, the slower we accelerate
			real accelFactor = 1.0f - glm::pow(glm::clamp(m_EngineForce / m_MaxEngineForce, 0.0f, 1.0f), 5.0f);

			real accelerateInput = g_InputManager->GetActionAxisValue(Action::VEHICLE_ACCELERATE);
			real reverseInput = g_InputManager->GetActionAxisValue(Action::VEHICLE_REVERSE);
			real brakeInput = g_InputManager->GetActionAxisValue(Action::VEHICLE_BRAKE);
			real turnLeftInput = g_InputManager->GetActionAxisValue(Action::VEHICLE_TURN_LEFT);
			real turnRightInput = g_InputManager->GetActionAxisValue(Action::VEHICLE_TURN_RIGHT);
			if (accelerateInput != 0.0f)
			{
				m_EngineForce += accelerateInput * m_EngineAccel * g_DeltaTime * accelFactor;
			}
			if (reverseInput != 0.0f)
			{
				m_EngineForce -= reverseInput * m_EngineAccel * g_DeltaTime * accelFactor;
			}
			if (turnLeftInput != 0.0f)
			{
				m_Steering -= turnLeftInput * m_TurnAccel * g_DeltaTime * steeringScale;
				m_Steering = glm::clamp(m_Steering, -MAX_STEER, MAX_STEER);
			}
			if (turnRightInput != 0.0f)
			{
				m_Steering += turnRightInput * m_TurnAccel * g_DeltaTime * steeringScale;
				m_Steering = glm::clamp(m_Steering, -MAX_STEER, MAX_STEER);
			}
			if (brakeInput != 0.0f)
			{
				m_BrakeForce = m_MaxBrakeForce * brakeInput;
				engineForceSlowScale = 0.0f;
			}
			else
			{
				m_BrakeForce = 0.0f;
			}
		}

		bool bBraking = m_BrakeForce > 0.0f;
		bool bReversing = m_EngineForce < 0.0f;

		if (m_Mesh != nullptr)
		{
			if (bBraking)
			{
				g_Renderer->GetMaterial(m_BrakeLightMatID)->constEmissive = m_ActiveBrakeLightMatEmissive;
			}
			else
			{
				g_Renderer->GetMaterial(m_BrakeLightMatID)->constEmissive = m_InitialBrakeLightMatEmissive;
			}

			if (bReversing)
			{
				g_Renderer->GetMaterial(m_ReverseLightMatID)->constEmissive = m_ActiveReverseLightMatEmissive;
			}
			else
			{
				g_Renderer->GetMaterial(m_ReverseLightMatID)->constEmissive = m_InitialReverseLightMatEmissive;
			}
		}

		BaseScene* scene = g_SceneManager->CurrentScene();
		GameObject* brakeLightL = scene->GetGameObject(m_BrakeLightIDs[0]);
		GameObject* brakeLightR = scene->GetGameObject(m_BrakeLightIDs[1]);
		if (brakeLightL != nullptr && brakeLightR != nullptr &&
			brakeLightL->GetTypeID() == SpotLightSID &&
			brakeLightR->GetTypeID() == SpotLightSID)
		{
			((SpotLight*)brakeLightL)->SetVisible(bBraking, true);
			((SpotLight*)brakeLightR)->SetVisible(bBraking, true);
		}

		m_Steering *= glm::clamp(steeringSlowScale, 0.0f, 1.0f);
		m_EngineForce *= glm::clamp(engineForceSlowScale, 0.0f, 1.0f);
		if (abs(m_EngineForce) < 0.1f)
		{
			m_EngineForce = 0.0f;
		}

		if (m_bFlippingRightSideUp)
		{
			m_Transform.SetWorldRotation(glm::slerp(m_Transform.GetWorldRotation(), m_TargetRot, glm::clamp(g_DeltaTime * UPRIGHTING_SPEED, 0.0f, 1.0f)));
			real uprightedness = glm::dot(m_Transform.GetUp(), VEC3_UP);
			m_SecFlipppingRightSideUp += g_DeltaTime;
			bool bRightSideUp = uprightedness > 0.9f;
			if (bRightSideUp || m_SecFlipppingRightSideUp > MAX_FLIPPING_UPRIGHT_TIME)
			{
				m_SecFlipppingRightSideUp = 0.0f;
				m_bFlippingRightSideUp = false;
			}
		}
		else
		{
			// Check if flipped upside down and stuck
			glm::vec3 chassisUp = m_Transform.GetUp();
			real uprightedness = glm::dot(chassisUp, VEC3_UP);
			// TODO: Also check num wheels on ground
			bool bUpsideDown = uprightedness < 0.25f;
			if (bUpsideDown)
			{
				m_SecUpsideDown += g_DeltaTime;

				if (m_SecUpsideDown > SEC_UPSIDE_DOWN_BEFORE_FLIP)
				{
					glm::vec3 chassisForward = m_Transform.GetForward();
					glm::vec3 chassisRight = m_Transform.GetRight();

					Print("Flip!\n");
					m_bFlippingRightSideUp = true;
					real w = sqrt(1.0f + uprightedness);

					if (abs(uprightedness) < 0.99f)
					{
						if (abs(dot(chassisForward, VEC3_UP)) > abs(dot(chassisRight, VEC3_UP)))
						{
							m_TargetRot = glm::normalize(glm::quat(chassisRight.x, chassisRight.y, chassisRight.z, w));
						}
						else
						{
							m_TargetRot = glm::normalize(glm::quat(chassisForward.x, chassisForward.y, chassisForward.z, w));
						}
					}
					else
					{
						// Vectors are nearly parallel, just pick the forward axis to rotate about
						m_TargetRot = glm::quat(chassisForward.x, chassisForward.y, chassisForward.z, w);
					}
					m_SecFlipppingRightSideUp = 0.0f;
					m_SecUpsideDown = 0.0f;
					m_Transform.SetWorldPosition(m_Transform.GetWorldPosition() + VEC3_UP * 3.0f);
				}
			}
			else
			{
				m_SecUpsideDown = 0.0f;
			}
		}

		// TODO: Check in player class? Store min level in scene
		// Check if fell below the level
		// (streamed terrain extends far below this, so only scenes without it reset)
		if (TerrainGenerator::GetActive() == nullptr && m_Transform.GetWorldPosition().y < -100.0f)
		{
			ResetTransform();
		}

		real maxWheelSlip = 1.0f;

		for (i32 i = 0; i < m_TireCount; i++)
		{
			//synchronize the wheels with the (interpolated) chassis world transform
			m_Vehicle->updateWheelTransform(i, true);

			const btWheelInfo& wheelInfo = m_Vehicle->getWheelInfo(i);
			glm::quat newWheelRotation = ToQuaternion(wheelInfo.m_worldTransform.getRotation());
			scene->GetGameObject(m_TireIDs[i])->GetTransform()->SetWorldRotation(newWheelRotation);

			maxWheelSlip = glm::min(maxWheelSlip, wheelInfo.m_skidInfo);
		}

		m_WheelSlipHisto.AddValue(maxWheelSlip);

		{
			i32 wheelIndex = 2;
			m_Vehicle->applyEngineForce(m_EngineForce, wheelIndex);
			m_Vehicle->setBrake(m_BrakeForce, wheelIndex);
			wheelIndex = 3;
			m_Vehicle->applyEngineForce(m_EngineForce, wheelIndex);
			m_Vehicle->setBrake(m_BrakeForce, wheelIndex);

			wheelIndex = 0;
			m_Vehicle->setSteeringValue(m_Steering, wheelIndex);
			wheelIndex = 1;
			m_Vehicle->setSteeringValue(m_Steering, wheelIndex);
		}

		real motorPitch = glm::clamp(forwardVel / 35.0f + 0.75f, 0.95f, 1.5f);
		real motorGain = glm::clamp(m_EngineForce / m_MaxEngineForce * 3.0f, 0.0f, 1.0f);

		motorGain = motorGain + glm::clamp(forwardVel / 35.0f, 0.0f, 1.0f - motorGain);

		real brakeGainMultiplier = slowingVelocity;
		brakeGainMultiplier *= m_WheelSlipHisto.currentAverage * m_WheelSlipHisto.currentAverage;

		if (motorGain < 0.1f)
		{
			m_SoundEffects[(u32)SoundEffect::ENGINE].FadeOut();
		}
		else
		{
			m_SoundEffects[(u32)SoundEffect::ENGINE].FadeIn();
		}

		if (abs(maxWheelSlip) > m_WheelSlipScreechThreshold)
		{
			m_SoundEffects[(u32)SoundEffect::BRAKE].FadeOut();
		}
		else
		{
			m_SoundEffects[(u32)SoundEffect::BRAKE].FadeIn();
		}

		m_SoundEffects[(u32)SoundEffect::BRAKE].SetGainMultiplier(brakeGainMultiplier);

		for (u32 i = 0; i < (u32)SoundEffect::_COUNT; ++i)
		{
			m_SoundEffects[i].Update();
		}

		m_SoundEffects[(u32)SoundEffect::ENGINE].SetPitch(motorPitch);
		m_SoundEffects[(u32)SoundEffect::ENGINE].SetGainMultiplier(motorGain);

		m_pLinearVelocity = linearVel;
	}

	void Vehicle::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject vehicleObj;
		if (parentObject.TryGetObject("vehicle", vehicleObj))
		{
			// Handling (optional, so variants like the dune buggy can differ from the defaults)
			vehicleObj.TryGetFloat("max engine force", m_MaxEngineForce);
			vehicleObj.TryGetFloat("engine accel", m_EngineAccel);
			vehicleObj.TryGetFloat("suspension stiffness", m_SuspensionStiffness);
			vehicleObj.TryGetFloat("suspension damping", m_SuspensionDamping);
			vehicleObj.TryGetFloat("suspension compression", m_SuspensionCompression);
			vehicleObj.TryGetFloat("suspension rest length", m_SuspensionRestLength);
			vehicleObj.TryGetFloat("wheel friction", m_WheelFriction);
			vehicleObj.TryGetFloat("roll influence", m_RollInfluence);

			// If this object was copied from a prefab, it will already have filled out IDs
			if (!m_TireIDs[0].IsValid())
			{
				std::vector<JSONField> tireIDs;
				if (vehicleObj.TryGetFieldArray("tire ids", tireIDs))
				{
					if (m_TireCount == (i32)tireIDs.size())
					{
						for (i32 i = 0; i < m_TireCount; ++i)
						{
							m_TireIDs[i] = GameObjectID::FromString(tireIDs[i].value.AsString());
						}
					}
					else
					{
						PrintError("Failed to parse tire IDs, incorrect number present (%i found, %i expected)", (i32)tireIDs.size(), m_TireCount);
					}
				}
			}

			if (!m_BrakeLightIDs[0].IsValid())
			{
				std::vector<JSONField> brakeLightIDs;
				if (vehicleObj.TryGetFieldArray("brake light ids", brakeLightIDs))
				{
					if ((i32)brakeLightIDs.size() == 2)
					{
						for (i32 i = 0; i < 2; ++i)
						{
							m_BrakeLightIDs[i] = GameObjectID::FromString(brakeLightIDs[i].value.AsString());
						}
					}
					else
					{
						PrintError("Failed to parse brake light IDs, incorrect number present (%i found, %i expected)", (i32)brakeLightIDs.size(), 2);
					}
				}
			}

			if (m_SoundEffectSIDs[0] != InvalidStringID)
			{
				std::vector<JSONField> soundEffectSIDs;
				if (vehicleObj.TryGetFieldArray("sound effect sids", soundEffectSIDs))
				{
					i32 count = std::min((i32)SoundEffect::_COUNT, (i32)soundEffectSIDs.size());
					for (i32 i = 0; i < count; ++i)
					{
						m_SoundEffectSIDs[i] = soundEffectSIDs[i].value.AsULong();
					}
				}
			}
		}
	}

	void Vehicle::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		JSONObject vehicleObj = {};

		// Prefab instances don't serialize their children (they're recreated with new IDs on load),
		// and the IDs get fixed up from the prefab template (see FixupPrefabTemplateIDs)
		bool bSerializeChildIDs = !m_SourcePrefabID.IsValid() || bSerializePrefabData;
		if (bSerializeChildIDs)
		{
			std::vector<JSONField> tireIDs;
			tireIDs.reserve(m_TireCount);
			for (GameObjectID& TireID : m_TireIDs)
			{
				tireIDs.emplace_back("", JSONValue(TireID.ToString()));
			}

			vehicleObj.fields.emplace_back("tire ids", JSONValue(tireIDs));

			std::vector<JSONField> brakeLightIDs;
			brakeLightIDs.reserve(2);
			for (GameObjectID& BrakeLightID : m_BrakeLightIDs)
			{
				brakeLightIDs.emplace_back("", JSONValue(BrakeLightID.ToString()));
			}

			vehicleObj.fields.emplace_back("brake light ids", JSONValue(brakeLightIDs));
		}

		vehicleObj.fields.emplace_back("max engine force", JSONValue(m_MaxEngineForce, 1));
		vehicleObj.fields.emplace_back("engine accel", JSONValue(m_EngineAccel, 1));
		vehicleObj.fields.emplace_back("suspension stiffness", JSONValue(m_SuspensionStiffness, 2));
		vehicleObj.fields.emplace_back("suspension damping", JSONValue(m_SuspensionDamping, 2));
		vehicleObj.fields.emplace_back("suspension compression", JSONValue(m_SuspensionCompression, 2));
		vehicleObj.fields.emplace_back("suspension rest length", JSONValue(m_SuspensionRestLength, 2));
		vehicleObj.fields.emplace_back("wheel friction", JSONValue(m_WheelFriction, 2));
		vehicleObj.fields.emplace_back("roll influence", JSONValue(m_RollInfluence, 2));

		std::vector<JSONField> soundEffectSIDs;
		soundEffectSIDs.reserve(m_SoundEffectSIDs.size());
		for (StringID SoundEffectSID : m_SoundEffectSIDs)
		{
			soundEffectSIDs.emplace_back("", JSONValue(SoundEffectSID));
		}
		vehicleObj.fields.emplace_back("sound effect sids", JSONValue(soundEffectSIDs));

		parentObject.fields.emplace_back("vehicle", JSONValue(vehicleObj));
	}

	void Vehicle::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		BaseScene* scene = g_SceneManager->CurrentScene();

		for (i32 i = 0; i < m_TireCount; ++i)
		{
			char buf[32];
			memcpy(buf, TireNames[i], strlen(TireNames[i]));
			buf[strlen(TireNames[i])] = ':';
			buf[strlen(TireNames[i]) + 1] = 0;
			scene->DrawImGuiGameObjectIDField(buf, m_TireIDs[i]);
		}

		for (i32 i = 0; i < 2; ++i)
		{
			char buf[32];
			memcpy(buf, BrakeLightNames[i], strlen(BrakeLightNames[i]));
			buf[strlen(BrakeLightNames[i])] = ':';
			buf[strlen(BrakeLightNames[i]) + 1] = 0;
			scene->DrawImGuiGameObjectIDField(buf, m_BrakeLightIDs[i]);
		}

		//AudioManager::AudioFileNameSIDField("engine", )


		ImGui::SliderFloat("Max engine force", &m_MaxEngineForce, 0.0f, 20000.0f);
		ImGui::SliderFloat("Engine accel", &m_EngineAccel, 0.0f, 30000.0f);
		ImGui::Text("Engine force: %.2f", m_EngineForce);
		ImGui::SliderFloat("Max brake force", &m_MaxBrakeForce, 0.0f, 200.0f);
		ImGui::Text("Braking force: %.2f", m_BrakeForce);
		ImGui::SliderFloat("Turn acceleration", &m_TurnAccel, 0.0f, 5.0f);
		ImGui::SliderFloat("Deceleration speed", &m_EngineForceDecelerationSpeed, 0.0f, 30.0f);
		ImGui::Text("Steering: %.2f", m_Steering);
		ImGui::SliderFloat("Screech threshold", &m_WheelSlipScreechThreshold, 0.0f, 1.0f);
		if (m_bFlippingRightSideUp)
		{
			ImGui::ProgressBar(m_SecFlipppingRightSideUp / MAX_FLIPPING_UPRIGHT_TIME, ImVec2(-1, 0), "Flipping right side up");
		}
		else
		{
			ImGui::ProgressBar(m_SecUpsideDown / SEC_UPSIDE_DOWN_BEFORE_FLIP, ImVec2(-1, 0), "Sec upside down");
		}

		// Tires
		if (m_Vehicle != nullptr)
		{
			real tireWidth = 20.0f;
			real tireHeight = 45.0f;
			real suspWidth = 8.0f;
			real suspHeight = 16.0f;
			ImU32 tireColGrounded = IM_COL32(250, 200, 200, 255);
			ImU32 tireColInAir = IM_COL32(200, 250, 200, 255);
			ImVec2 cursor = ImGui::GetCursorScreenPos();
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			real spacingX = 50.0f;
			real spacingY = 110.0f;
			real lowestWheelSlip = 1.0f;
			for (i32 i = 0; i < m_TireCount; i++)
			{
				// Assume this is called in update loop:
				m_Vehicle->updateWheelTransform(i, true);

				const btWheelInfo& wheelInfo = m_Vehicle->getWheelInfo(i);
				glm::quat newWheelRotation = ToQuaternion(wheelInfo.m_worldTransform.getRotation());
				GameObject* tireObject = scene->GetGameObject(m_TireIDs[i]);
				Transform* tireTransform = tireObject->GetTransform();
				tireTransform->SetWorldRotation(newWheelRotation);

				real x = (real)(i % 2);
				real y = (real)(i / 2);
				ImVec2 rectStart = cursor + ImVec2(20.0f + x * spacingX, y * spacingY);
				// NOTE: TODO: wheelInfo.m_raycastInfo.m_isInContact isn't returning true ever...
				drawList->AddRect(rectStart, rectStart + ImVec2(tireWidth, tireHeight), wheelInfo.m_raycastInfo.m_isInContact ? tireColGrounded : tireColInAir);
				rectStart += ImVec2(x == 0 ? -10.0f - suspWidth : tireWidth + 10.0f, 0.0f);
				drawList->AddRect(rectStart, rectStart + ImVec2(suspWidth, suspHeight), tireColGrounded);
				real suspensionLen = wheelInfo.m_raycastInfo.m_suspensionLength;
				rectStart += ImVec2(1.0f, 1.0f);
				drawList->AddRectFilled(rectStart, rectStart + ImVec2(suspWidth - 2.0f, suspensionLen * (suspHeight - 2.0f)), tireColInAir);

				lowestWheelSlip = glm::min(lowestWheelSlip, wheelInfo.m_skidInfo);
			}
			ImGui::SetCursorScreenPos(cursor + ImVec2(0, spacingY + tireHeight + +20.0f));
			ImGui::Text("Lowest wheel slip: %.2f", lowestWheelSlip);
		}

		ImGui::SliderFloat("Wheel friction", &m_WheelFriction, 0.0f, 10.0f);
		ImGui::SliderFloat("Roll influence", &m_RollInfluence, 0.0f, 1.0f);
		ImGui::SliderFloat("Wheel radius", &m_WheelRadius, 0.0f, 2.0f);
		ImGui::SliderFloat("Wheel width", &m_WheelWidth, 0.0f, 2.0f);

		if (ImGui::Button("Recreate rigid body"))
		{
			if (m_RigidBody != nullptr)
			{
				btDiscreteDynamicsWorld* dynamicsWorld = scene->GetPhysicsWorld()->GetWorld();
				dynamicsWorld->removeVehicle(m_Vehicle);
				m_Vehicle = nullptr;
			}

			CreateRigidBody();
		}

		ImGui::Spacing();

		ImGui::SliderFloat("Sus. stiffness", &m_tuning.m_suspensionStiffness, 0.0f, 10.0f);
		ImGui::SliderFloat("Sus. compression", &m_tuning.m_suspensionCompression, 0.0f, 5.0f);
		ImGui::SliderFloat("Sus. damping", &m_tuning.m_suspensionDamping, 0.0f, 1.0f);
		ImGui::SliderFloat("Sus. max travel cm", &m_tuning.m_maxSuspensionTravelCm, 0.0f, 1000.0f);
		ImGui::SliderFloat("Friction slip", &m_tuning.m_frictionSlip, 0.0f, 50.0f);
		ImGui::SliderFloat("Sus. max force", &m_tuning.m_maxSuspensionForce, 0.0f, 10'000.0f);


		if (ImGui::BeginChild("Sound clips", ImVec2(300, 300), true))
		{
			for (i32 i = 0; i < (i32)SoundEffect::_COUNT; ++i)
			{
				if (m_SoundEffects[i].DrawImGui())
				{
					SetSoundEffectSID((SoundEffect)i, m_SoundEffects[i].loopSID);
					m_SoundEffectSIDs[i] = m_SoundEffects[i].loopSID;
				}
			}
		}
		ImGui::EndChild();
	}

	void Vehicle::OnPlayerEnter()
	{
		m_bOccupied = true;
	}

	void Vehicle::OnPlayerExit()
	{
		// Player has left the vehicle, stop accelerating & braking
		m_EngineForce = 0.0f;
		m_BrakeForce = 0.0f;
		m_bOccupied = false;
	}

	void Vehicle::MatchCorrespondingID(const GameObjectID& existingID, GameObject* newGameObject, const char* objectName, GameObjectID& outCorrespondingID)
	{
		ChildIndex childIndex = GetChildIndexWithID(existingID);
		bool bSuccess = childIndex.IsValid();

		if (bSuccess)
		{
			GameObjectID correspondingID = newGameObject->GetIDAtChildIndex(childIndex);
			bSuccess = (correspondingID != InvalidGameObjectID);

			if (bSuccess)
			{
				outCorrespondingID = correspondingID;
			}
		}

		if (!bSuccess)
		{
			std::string idStr = existingID.ToString();
			PrintError("Failed to find corresponding game object ID for child %s (%s)\n", objectName, idStr.c_str());
		}
	}

	void Vehicle::SetSoundEffectSID(SoundEffect soundEffect, StringID soundSID)
	{
		m_SoundEffects[(i32)soundEffect] = SoundClip_LoopingSimple(VehicleSoundEffectNames[(i32)soundEffect], soundSID, false);
		if (soundSID != InvalidStringID && soundEffect == SoundEffect::ENGINE)
		{
			// TODO: Save this metadata somewhere in a file
			AudioManager::SetSourceLooping(m_SoundEffects[(i32)soundEffect].loop, true);
		}
	}

	void Vehicle::ResetTransform()
	{
		m_RigidBody->GetRigidBodyInternal()->setAngularVelocity(btVector3(0.0f, 0.0f, 0.0f));
		m_RigidBody->GetRigidBodyInternal()->setLinearVelocity(btVector3(0.0f, 0.0f, 0.0f));
		m_Transform.CloneFrom(Transform::Identity);
		m_Transform.Translate(0.0f, 2.0f, 0.0f);
		m_SecUpsideDown = 0.0f;
		m_bFlippingRightSideUp = false;
		m_SecFlipppingRightSideUp = 0.0f;
		m_EngineForce = 0.0f;
		m_BrakeForce = 0.0f;
		m_Steering = 0.0f;
	}

	void Vehicle::FixupPrefabTemplateIDs(GameObject* newGameObject)
	{
		Vehicle* newVehicle = ((Vehicle*)newGameObject);

		for (i32 i = 0; i < m_TireCount; ++i)
		{
			MatchCorrespondingID(m_TireIDs[i], newGameObject, TireNames[i], newVehicle->m_TireIDs[i]);
		}

		for (i32 i = 0; i < 2; ++i)
		{
			MatchCorrespondingID(m_BrakeLightIDs[i], newGameObject, BrakeLightNames[i], newVehicle->m_BrakeLightIDs[i]);
		}
	}
} // namespace flex
