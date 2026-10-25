#include "stdafx.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btCompoundShape.h>
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
#include "GameSounds.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/Objects/Building.hpp"
#include "Scene/Objects/GerstnerWave.hpp"
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

			// Tail lights glow a dim red, & light up brightly while braking (values above one bloom in HDR)
			g_Renderer->GetMaterial(m_BrakeLightMatID)->constAlbedo = VEC4_ONE;
			m_InitialBrakeLightMatEmissive = glm::vec4(0.55f, 0.015f, 0.01f, 1.0f);
			g_Renderer->GetMaterial(m_BrakeLightMatID)->constEmissive = m_InitialBrakeLightMatEmissive;
			m_InitialReverseLightMatEmissive = g_Renderer->GetMaterial(m_ReverseLightMatID)->constEmissive;

			m_ActiveBrakeLightMatEmissive = glm::vec4(6.0f, 0.12f, 0.06f, 1.0f);
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
			// The material is shared by every vehicle, so each overrides its own brake lights' brightness
			if (m_Mesh->GetSubmeshCount() > 2)
			{
				g_Renderer->AddRenderObjectUniformOverride(m_Mesh->GetSubMesh(2)->renderID, &U_CONST_EMISSIVE,
					{ bBraking ? m_ActiveBrakeLightMatEmissive : m_InitialBrakeLightMatEmissive });
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

		if (m_bOccupied)
		{
			GameSounds::UpdateVehicle(forwardVel, m_EngineForce / m_MaxEngineForce, bBraking, 1.0f - maxWheelSlip);
		}

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

	// ---------------------------------------------------------------------------------------------------------------
	// Row boat
	// ---------------------------------------------------------------------------------------------------------------

	namespace
	{
		// Hull shape (local space, +z forward): a stack of ellipses whose semi-axes grow linearly from the keel up to
		// the gunwale, which rises a little towards the bow & stern (sheer)
		const real HULL_HALF_WIDTH = 0.72f;
		const real HULL_HALF_LENGTH = 2.1f;
		const real HULL_BOTTOM = -0.28f;
		const real HULL_TOP = 0.3f;
		const real HULL_BOTTOM_WIDTH_SCALE = 0.55f;
		const real HULL_BOTTOM_LENGTH_SCALE = 0.8f;
		const real HULL_THICKNESS = 0.05f;
		const real HULL_SHEER = 0.16f;

		glm::vec3 SafeNormalize(const glm::vec3& v, const glm::vec3& fallback)
		{
			const real length = glm::length(v);
			return length > 1e-6f ? v / length : fallback;
		}

		// Where the oars pivot (the right one, the left is mirrored)
		const real OARLOCK_Z = -0.45f;
		const real OAR_REST_SWEEP = 1.1f; // Blades trail behind when not rowing
		const real OAR_SWEEP_AMPLITUDE = 0.6f;
		const real OAR_STROKES_PER_SECOND = 0.75f;

		// Points on the hull bottom at which buoyancy is applied
		const glm::vec3 s_BuoyancyPoints[] =
		{
			glm::vec3(-0.26f, HULL_BOTTOM, 1.25f), glm::vec3(0.26f, HULL_BOTTOM, 1.25f),
			glm::vec3(-0.36f, HULL_BOTTOM, 0.42f), glm::vec3(0.36f, HULL_BOTTOM, 0.42f),
			glm::vec3(-0.36f, HULL_BOTTOM, -0.42f), glm::vec3(0.36f, HULL_BOTTOM, -0.42f),
			glm::vec3(-0.26f, HULL_BOTTOM, -1.25f), glm::vec3(0.26f, HULL_BOTTOM, -1.25f),
		};

		// Water clip volumes of every boat drawn this frame (the inside of their hulls)
		WaterClipData s_WaterClipData = {};
		i32 s_WaterClipUserCount = 0;
		i32 s_WaterClipFrame = -1;

		// Semi-axes (half width, half length) of the hull at the given height, shrunk by inset (for the inner surface)
		glm::vec2 HullSemiAxes(real y, real inset)
		{
			const real t = (y - HULL_BOTTOM) / (HULL_TOP - HULL_BOTTOM);
			return glm::vec2(HULL_HALF_WIDTH * Lerp(HULL_BOTTOM_WIDTH_SCALE, 1.0f, t) - inset,
				HULL_HALF_LENGTH * Lerp(HULL_BOTTOM_LENGTH_SCALE, 1.0f, t) - inset);
		}

		// Point on the hull's surface (outer: inset = 0, inner: inset = thickness). t: [0, 1] from the bottom to the
		// gunwale, theta: angle around the hull (pi/2: bow)
		glm::vec3 HullPoint(real t, real theta, real inset)
		{
			const real bottom = HULL_BOTTOM + inset;
			const real y = Lerp(bottom, HULL_TOP, t);
			const glm::vec2 axes = HullSemiAxes(y, inset);
			const real s = std::sin(theta);
			// Sheer only affects the top of the hull, so the shape near the waterline (which the water clip volume
			// must match) is unaffected
			const real sheer = HULL_SHEER * t * t * t * t * s * s;
			return glm::vec3(axes.x * std::cos(theta), y + sheer, axes.y * s);
		}

		MaterialID GetOrCreateBoatMaterial(const char* name, const glm::vec3& albedoSRGB, real roughness)
		{
			MaterialID materialID = g_Renderer->GetMaterialID(name);
			if (materialID == InvalidMaterialID)
			{
				MaterialCreateInfo createInfo = {};
				createInfo.name = name;
				createInfo.shaderName = "pbr";
				// Colours are given in sRGB, materials take linear values
				createInfo.constAlbedo = glm::vec4(glm::pow(albedoSRGB, glm::vec3(2.2f)), 1.0f);
				createInfo.constRoughness = roughness;
				createInfo.constMetallic = 0.0f;
				createInfo.bSerializable = false;
				materialID = g_Renderer->InitializeMaterial(&createInfo);
			}
			return materialID;
		}

		struct BoatMeshBuilder
		{
			explicit BoatMeshBuilder(MaterialID materialID) :
				materialID(materialID)
			{
				data.attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(materialID)->shaderID)->vertexAttributes;
			}

			u32 AddVertex(const glm::vec3& pos, const glm::vec3& normal, const glm::vec2& uv)
			{
				const glm::vec3 n = SafeNormalize(normal, VEC3_UP);
				data.positions_3D.push_back(pos);
				data.normals.push_back(n);
				data.tangents.push_back(SafeNormalize(glm::cross(glm::abs(n.y) < 0.9f ? VEC3_UP : VEC3_RIGHT, n), VEC3_RIGHT));
				data.texCoords_UV.push_back(uv);
				data.colours_R32G32B32A32.push_back(VEC4_ONE);
				return (u32)data.positions_3D.size() - 1;
			}

			// Wound counter-clockwise seen from the side the given normal faces (the engine's convention)
			void AddTriangle(u32 a, u32 b, u32 c, const glm::vec3& facing)
			{
				const glm::vec3& pa = data.positions_3D[a];
				const glm::vec3 n = glm::cross(data.positions_3D[b] - pa, data.positions_3D[c] - pa);
				if (glm::dot(n, facing) >= 0.0f)
				{
					indices.insert(indices.end(), { a, b, c });
				}
				else
				{
					indices.insert(indices.end(), { a, c, b });
				}
			}

			void AddBox(const glm::vec3& center, const glm::vec3& halfExtents)
			{
				AppendBoxToVertexData(center, halfExtents, QUAT_IDENTITY, data, indices);
			}

			void Finish(Mesh* mesh)
			{
				if (!indices.empty())
				{
					MeshComponent::LoadFromMemory(mesh, data, indices, materialID);
				}
			}

			MaterialID materialID;
			VertexBufferDataCreateInfo data = {};
			std::vector<u32> indices;
		};

		void BuildOarMesh(GameObject* oar, MaterialID materialID, real side)
		{
			BoatMeshBuilder builder(materialID);
			// Pivots at the origin, the handle sticks inboard & the blade outboard
			builder.AddBox(glm::vec3(0.55f * side, 0.0f, 0.0f), glm::vec3(1.1f, 0.022f, 0.022f));
			builder.AddBox(glm::vec3(1.9f * side, 0.0f, 0.0f), glm::vec3(0.27f, 0.012f, 0.08f));
			builder.AddBox(glm::vec3(-0.5f * side, 0.0f, 0.0f), glm::vec3(0.07f, 0.03f, 0.03f));
			builder.Finish(oar->SetMesh(new Mesh(oar)));
		}
	} // anonymous namespace

	RowBoat::RowBoat(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /* = false */) :
		GameObject(name, RowBoatSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		m_bInteractable = true;
		// Picked up into the player's inventory & placed again like vehicles
		m_bItemizable = true;
	}

	PropertyCollection* RowBoat::BuildTypeUniquePropertyCollection()
	{
		return GameObject::BuildPropertyCollection();
	}

	void RowBoat::CreateMeshes()
	{
		const MaterialID paintMaterialID = GetOrCreateBoatMaterial("Row boat paint", glm::vec3(0.16f, 0.4f, 0.44f), 0.55f);
		const MaterialID woodMaterialID = GetOrCreateBoatMaterial("Row boat wood", glm::vec3(0.6f, 0.4f, 0.22f), 0.8f);
		const MaterialID trimMaterialID = GetOrCreateBoatMaterial("Row boat trim", glm::vec3(0.36f, 0.21f, 0.1f), 0.7f);

		Mesh* mesh = SetMesh(new Mesh(this));

		const u32 segmentCount = 48;
		const u32 ringCount = 7;

		auto Theta = [segmentCount](u32 segment) { return TWO_PI * (real)segment / (real)segmentCount; };

		// Side surfaces (outer: painted, inner: wood)
		auto BuildSide = [&](BoatMeshBuilder& builder, real inset, real outward)
		{
			const u32 base = (u32)builder.data.positions_3D.size();
			for (u32 ring = 0; ring < ringCount; ++ring)
			{
				const real t = (real)ring / (real)(ringCount - 1);
				for (u32 segment = 0; segment < segmentCount; ++segment)
				{
					const real theta = Theta(segment);
					const glm::vec3 p = HullPoint(t, theta, inset);
					// Normal from the surface's partial derivatives
					const real e = 0.001f;
					const glm::vec3 dT = HullPoint(glm::min(t + e, 1.0f), theta, inset) - HullPoint(glm::max(t - e, 0.0f), theta, inset);
					const glm::vec3 dTheta = HullPoint(t, theta + e, inset) - HullPoint(t, theta - e, inset);
					glm::vec3 n = glm::normalize(glm::cross(dT, dTheta));
					if (glm::dot(n, glm::vec3(p.x, 0.0f, p.z)) * outward < 0.0f)
					{
						n = -n;
					}
					builder.AddVertex(p, n, glm::vec2(theta * HULL_HALF_LENGTH, p.y));
				}
			}
			for (u32 ring = 0; ring + 1 < ringCount; ++ring)
			{
				for (u32 segment = 0; segment < segmentCount; ++segment)
				{
					const u32 next = (segment + 1) % segmentCount;
					const u32 a = base + ring * segmentCount + segment;
					const u32 b = base + ring * segmentCount + next;
					const u32 c = base + (ring + 1) * segmentCount + next;
					const u32 d = base + (ring + 1) * segmentCount + segment;
					const glm::vec3 facing = builder.data.normals[a] + builder.data.normals[c];
					builder.AddTriangle(a, b, c, facing);
					builder.AddTriangle(a, c, d, facing);
				}
			}
		};

		// Flat ellipse (the bottom outside, & the floor inside)
		auto BuildFlat = [&](BoatMeshBuilder& builder, real inset, const glm::vec3& normal)
		{
			const glm::vec3 centerPos(0.0f, HULL_BOTTOM + inset, 0.0f);
			const u32 center = builder.AddVertex(centerPos, normal, glm::vec2(0.0f));
			const u32 base = (u32)builder.data.positions_3D.size();
			for (u32 segment = 0; segment < segmentCount; ++segment)
			{
				const glm::vec3 p = HullPoint(0.0f, Theta(segment), inset);
				builder.AddVertex(p, normal, glm::vec2(p.x, p.z));
			}
			for (u32 segment = 0; segment < segmentCount; ++segment)
			{
				builder.AddTriangle(center, base + segment, base + (segment + 1) % segmentCount, normal);
			}
		};

		BoatMeshBuilder paint(paintMaterialID);
		BuildSide(paint, 0.0f, 1.0f);
		BuildFlat(paint, 0.0f, -VEC3_UP);
		// Keel
		paint.AddBox(glm::vec3(0.0f, HULL_BOTTOM - 0.03f, 0.0f), glm::vec3(0.04f, 0.035f, HULL_HALF_LENGTH * HULL_BOTTOM_LENGTH_SCALE * 0.95f));

		BoatMeshBuilder wood(woodMaterialID);
		BuildSide(wood, HULL_THICKNESS, -1.0f);
		BuildFlat(wood, HULL_THICKNESS, VEC3_UP);

		BoatMeshBuilder trim(trimMaterialID);
		// Gunwale: joins the top of the outer & inner surfaces
		{
			const u32 base = (u32)trim.data.positions_3D.size();
			for (u32 segment = 0; segment < segmentCount; ++segment)
			{
				const real theta = Theta(segment);
				trim.AddVertex(HullPoint(1.0f, theta, 0.0f), VEC3_UP, glm::vec2(theta, 0.0f));
				trim.AddVertex(HullPoint(1.0f, theta, HULL_THICKNESS), VEC3_UP, glm::vec2(theta, 1.0f));
			}
			for (u32 segment = 0; segment < segmentCount; ++segment)
			{
				const u32 next = (segment + 1) % segmentCount;
				const u32 outerA = base + segment * 2;
				const u32 innerA = outerA + 1;
				const u32 outerB = base + next * 2;
				const u32 innerB = outerB + 1;
				trim.AddTriangle(outerA, outerB, innerB, VEC3_UP);
				trim.AddTriangle(outerA, innerB, innerA, VEC3_UP);
			}
		}

		// Seats (thwarts) spanning the inside of the hull
		const real seatY = 0.06f;
		const real seatZs[] = { 1.2f, -0.05f, -1.35f };
		for (real seatZ : seatZs)
		{
			const glm::vec2 axes = HullSemiAxes(seatY, HULL_THICKNESS);
			const real halfWidth = axes.x * std::sqrt(glm::max(1.0f - (seatZ * seatZ) / (axes.y * axes.y), 0.0f));
			trim.AddBox(glm::vec3(0.0f, seatY, seatZ), glm::vec3(halfWidth, 0.025f, 0.13f));
		}
		// Floor boards
		for (i32 i = -1; i <= 1; ++i)
		{
			trim.AddBox(glm::vec3(0.13f * (real)i, HULL_BOTTOM + HULL_THICKNESS + 0.012f, -0.1f), glm::vec3(0.05f, 0.012f, 1.2f));
		}
		// Oarlocks
		const glm::vec2 topAxes = HullSemiAxes(HULL_TOP, 0.0f);
		const real oarlockX = topAxes.x * std::sqrt(1.0f - (OARLOCK_Z * OARLOCK_Z) / (topAxes.y * topAxes.y)) - HULL_THICKNESS * 0.5f;
		for (real side = -1.0f; side <= 1.0f; side += 2.0f)
		{
			trim.AddBox(glm::vec3(oarlockX * side, HULL_TOP + 0.03f, OARLOCK_Z), glm::vec3(0.03f, 0.04f, 0.03f));
		}

		paint.Finish(mesh);
		wood.Finish(mesh);
		trim.Finish(mesh);

		// Oars (left, right)
		for (i32 i = 0; i < 2; ++i)
		{
			const real side = (i == 0) ? -1.0f : 1.0f;
			GameObject* oar = new GameObject(i == 0 ? "Oar left" : "Oar right", BaseObjectSID);
			oar->SetSerializable(false);
			oar->SetVisibleInSceneExplorer(false);
			BuildOarMesh(oar, trimMaterialID, side);
			oar->GetTransform()->SetLocalPosition(glm::vec3(oarlockX * side, HULL_TOP + 0.07f, OARLOCK_Z));
			AddChild(oar);
			m_Oars[i] = oar;
		}
		UpdateOars();
	}

	void RowBoat::Initialize()
	{
		if (m_Mesh == nullptr)
		{
			CreateMeshes();
		}

		if (m_RigidBody == nullptr)
		{
			btCompoundShape* shape = new btCompoundShape();
			// Bottom & sides, open on top so things (the player) can stand inside
			const real height = HULL_TOP - HULL_BOTTOM;
			const glm::vec2 midAxes = HullSemiAxes((HULL_TOP + HULL_BOTTOM) * 0.5f, 0.0f);
			shape->addChildShape(btTransform(btQuaternion::getIdentity(), btVector3(0.0f, HULL_BOTTOM + 0.06f, 0.0f)),
				new btBoxShape(btVector3(midAxes.x * 0.8f, 0.06f, midAxes.y * 0.85f)));
			for (real side = -1.0f; side <= 1.0f; side += 2.0f)
			{
				shape->addChildShape(btTransform(btQuaternion::getIdentity(), btVector3(side * midAxes.x * 0.78f, HULL_BOTTOM + height * 0.5f, 0.0f)),
					new btBoxShape(btVector3(0.05f, height * 0.5f, midAxes.y * 0.7f)));
				shape->addChildShape(btTransform(btQuaternion::getIdentity(), btVector3(0.0f, HULL_BOTTOM + height * 0.5f, side * midAxes.y * 0.82f)),
					new btBoxShape(btVector3(midAxes.x * 0.55f, height * 0.5f, 0.05f)));
			}
			SetCollisionShape(shape);
			RigidBody* rigidBody = SetRigidBody(new RigidBody((u32)CollisionType::DEFAULT, (u32)CollisionType::DEFAULT | (u32)CollisionType::STATIC));
			rigidBody->SetMass(m_Mass);
		}

		if (!m_bIsTemplate)
		{
			if (s_WaterClipUserCount++ == 0)
			{
				s_WaterClipData = {};
				g_Renderer->SetGlobalUniform(&U_WATER_CLIP_DATA, &s_WaterClipData, sizeof(s_WaterClipData));
			}
		}

		GameObject::Initialize();
	}

	void RowBoat::PostInitialize()
	{
		if (m_RigidBody != nullptr)
		{
			m_RigidBody->SetInterpolation(true, true);
			btRigidBody* rb = m_RigidBody->GetRigidBodyInternal();
			// Buoyancy is applied as forces, which sleeping bodies ignore
			rb->setActivationState(DISABLE_DEACTIVATION);
			rb->setFriction(0.8f);
		}

		GameObject::PostInitialize();
	}

	void RowBoat::Destroy(bool bDetachFromParent /* = true */)
	{
		if (!m_bIsTemplate && s_WaterClipUserCount > 0)
		{
			if (--s_WaterClipUserCount == 0)
			{
				g_Renderer->ClearGlobalUniform(&U_WATER_CLIP_DATA, &s_WaterClipData);
			}
		}

		m_Oars[0] = m_Oars[1] = nullptr;

		GameObject::Destroy(bDetachFromParent);
	}

	bool RowBoat::SampleWaterHeight(const glm::vec3& posWS, real& outHeight)
	{
		GerstnerWave* ocean = GerstnerWave::GetActive();
		if (ocean != nullptr && ocean->GetWaterHeight(posWS, outHeight))
		{
			return true;
		}

		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (terrainGenerator != nullptr && !terrainGenerator->IsWorldChangePending())
		{
			const terrain::ColumnSample column = terrainGenerator->SampleColumn(posWS.x, posWS.z);
			if (column.lakeID != 0 && column.lakeT < 1.0f && column.height < column.waterLevel)
			{
				outHeight = column.waterLevel;
				return true;
			}
		}

		return false;
	}

	void RowBoat::Update()
	{
		PROFILE_AUTO("RowBoat Update");

		m_RowInput = 0.0f;
		m_TurnInput = 0.0f;
		if (m_bOccupied && g_CameraManager->CurrentCamera()->bIsGameplayCam)
		{
			m_RowInput = g_InputManager->GetActionAxisValue(Action::VEHICLE_ACCELERATE) - g_InputManager->GetActionAxisValue(Action::VEHICLE_REVERSE);
			m_TurnInput = g_InputManager->GetActionAxisValue(Action::VEHICLE_TURN_RIGHT) - g_InputManager->GetActionAxisValue(Action::VEHICLE_TURN_LEFT);
		}

		// Turning pulls harder on the outside oar (& backs the inside one when not moving forwards)
		m_OarInput[0] = glm::clamp(m_RowInput + m_TurnInput, -1.0f, 1.0f);
		m_OarInput[1] = glm::clamp(m_RowInput - m_TurnInput, -1.0f, 1.0f);

		for (i32 i = 0; i < 2; ++i)
		{
			if (m_OarInput[i] != 0.0f)
			{
				m_OarPhase[i] = glm::mod(m_OarPhase[i] + g_DeltaTime * TWO_PI * OAR_STROKES_PER_SECOND * m_OarInput[i], TWO_PI);
			}
		}

		UpdateOars();

		GameObject::Update();
	}

	void RowBoat::UpdateOars()
	{
		for (i32 i = 0; i < 2; ++i)
		{
			if (m_Oars[i] == nullptr)
			{
				continue;
			}

			// Positive sweep moves the blade aft. The power stroke (blade in the water) is the first half of the cycle.
			const real phase = m_OarPhase[i];
			const real activity = glm::min(glm::abs(m_OarInput[i]) * 4.0f, 1.0f);
			const real sweep = Lerp(OAR_REST_SWEEP, -OAR_SWEEP_AMPLITUDE * std::cos(phase), activity);
			const real dip = Lerp(0.04f, Lerp(0.03f, 0.3f, glm::smoothstep(-0.25f, 0.25f, std::sin(phase))), activity);
			const real side = (i == 0) ? -1.0f : 1.0f;
			const glm::quat rotation = glm::angleAxis(sweep * side, VEC3_UP) * glm::angleAxis(-dip * side, VEC3_FORWARD);
			m_Oars[i]->GetTransform()->SetLocalRotation(rotation);
		}
	}

	void RowBoat::FixedUpdate()
	{
		if (m_RigidBody == nullptr || m_RigidBody->GetRigidBodyInternal() == nullptr)
		{
			return;
		}

		btRigidBody* rb = m_RigidBody->GetRigidBodyInternal();
		const btTransform& transform = rb->getWorldTransform();
		const glm::vec3 pos = ToVec3(transform.getOrigin());
		const glm::quat rot = ToQuaternion(transform.getRotation());
		const glm::vec3 up = rot * VEC3_UP;
		const glm::vec3 forward = rot * VEC3_FORWARD;
		const btVector3 gravity = rb->getGravity();
		const real g = gravity.length();

		const real totalMass = m_Mass + (m_bOccupied ? m_RowerMass : 0.0f);
		if (m_bOccupied)
		{
			// The rower's weight, on the middle seat
			rb->applyForce(gravity * m_RowerMass, ToBtVec3(rot * glm::vec3(0.0f, 0.0f, -0.05f)));
		}

		// Terrain water is flat, so one sample is enough (the ocean is sampled at each point)
		const bool bOcean = GerstnerWave::GetActive() != nullptr;
		real waterHeight = 0.0f;
		bool bWater = SampleWaterHeight(pos, waterHeight);

		const u32 pointCount = (u32)ARRAY_LENGTH(s_BuoyancyPoints);
		const real columnHeight = HULL_TOP - HULL_BOTTOM;
		// Force per meter of depth at each point
		const real stiffness = m_BuoyancyScale * totalMass * g / ((real)pointCount * columnHeight);
		const real damping = 1.4f * glm::sqrt(stiffness * totalMass / (real)pointCount);

		u32 submergedCount = 0;
		for (u32 i = 0; i < pointCount; ++i)
		{
			const glm::vec3 offset = rot * s_BuoyancyPoints[i];
			const glm::vec3 pointPos = pos + offset;
			real pointWaterHeight = waterHeight;
			if (bOcean && !SampleWaterHeight(pointPos, pointWaterHeight))
			{
				continue;
			}
			if (!bOcean && !bWater)
			{
				continue;
			}

			const real depth = glm::clamp(pointWaterHeight - pointPos.y, 0.0f, columnHeight);
			if (depth <= 0.0f)
			{
				continue;
			}
			++submergedCount;

			const btVector3 relPos = ToBtVec3(offset);
			const real pointVelY = rb->getVelocityInLocalPoint(relPos).getY();
			const real force = stiffness * depth - damping * pointVelY * glm::min(depth / 0.1f, 1.0f);
			rb->applyForce(btVector3(0.0f, force, 0.0f), relPos);
		}

		m_SubmergedFraction = (real)submergedCount / (real)pointCount;
		if (submergedCount == 0)
		{
			return;
		}

		// Water drag: little resistance moving forwards, lots sideways (the keel)
		const glm::vec3 velocity = ToVec3(rb->getLinearVelocity());
		const glm::vec3 localVel = glm::inverse(rot) * velocity;
		const glm::vec3 localDrag(
			-localVel.x * m_LateralDrag,
			0.0f,
			-localVel.z * (m_ForwardDrag + 20.0f * glm::abs(localVel.z)));
		rb->applyCentralForce(ToBtVec3(rot * localDrag * m_SubmergedFraction));

		const glm::vec3 angularVel = ToVec3(rb->getAngularVelocity());
		rb->applyTorque(ToBtVec3(-angularVel * m_AngularDrag * m_SubmergedFraction));

		// Rowing: each oar pushes at its side, mostly during the power stroke
		const glm::vec3 flatForward = SafeNormalize(glm::vec3(forward.x, 0.0f, forward.z), VEC3_FORWARD);
		for (i32 i = 0; i < 2; ++i)
		{
			if (m_OarInput[i] == 0.0f)
			{
				continue;
			}
			const real side = (i == 0) ? -1.0f : 1.0f;
			const real stroke = 0.6f + 0.8f * glm::max(std::sin(m_OarPhase[i]), 0.0f);
			const glm::vec3 thrust = flatForward * (m_OarInput[i] * m_StrokeForce * stroke * m_SubmergedFraction);
			rb->applyForce(ToBtVec3(thrust), ToBtVec3(rot * glm::vec3(side * 0.7f, 0.0f, 0.0f)));
		}
		if (m_TurnInput != 0.0f)
		{
			rb->applyTorque(ToBtVec3(up * (m_TurnInput * m_TurnTorque * m_SubmergedFraction)));
		}
	}

	void RowBoat::Render()
	{
		if (m_bIsTemplate)
		{
			return;
		}

		// Called after the scene has updated, so the transform is the (interpolated) one which gets drawn
		const i32 frame = g_Renderer->GetFramesRenderedCount();
		if (frame != s_WaterClipFrame)
		{
			s_WaterClipFrame = frame;
			s_WaterClipData.count.x = 0.0f;
		}

		const i32 index = (i32)s_WaterClipData.count.x;
		if (index >= MAX_WATER_CLIP_VOLUMES)
		{
			return;
		}

		const glm::mat4 localToWorld = glm::translate(MAT4_IDENTITY, m_Transform.GetWorldPosition()) * glm::mat4_cast(m_Transform.GetWorldRotation());
		const glm::vec2 innerTop = HullSemiAxes(HULL_TOP, HULL_THICKNESS);
		const real innerBottomY = HULL_BOTTOM + HULL_THICKNESS;
		const glm::vec2 innerBottom = HullSemiAxes(innerBottomY, HULL_THICKNESS);

		WaterClipVolume& volume = s_WaterClipData.volumes[index];
		volume.worldToLocal = glm::inverse(localToWorld);
		volume.extents = glm::vec4(innerTop.x, innerTop.y, innerBottomY, HULL_TOP);
		volume.bottomScale = glm::vec4(innerBottom / innerTop, 0.0f, 0.0f);

		s_WaterClipData.count.x = (real)(index + 1);
	}

	void RowBoat::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		ImGui::Text("Submerged: %.0f%%", m_SubmergedFraction * 100.0f);
		ImGui::Text("Row input: %.2f, turn input: %.2f", m_RowInput, m_TurnInput);
		ImGui::SliderFloat("Stroke force", &m_StrokeForce, 0.0f, 2000.0f);
		ImGui::SliderFloat("Turn torque", &m_TurnTorque, 0.0f, 2000.0f);
		ImGui::SliderFloat("Buoyancy", &m_BuoyancyScale, 0.5f, 10.0f);
		ImGui::SliderFloat("Forward drag", &m_ForwardDrag, 0.0f, 500.0f);
		ImGui::SliderFloat("Lateral drag", &m_LateralDrag, 0.0f, 3000.0f);
		ImGui::SliderFloat("Angular drag", &m_AngularDrag, 0.0f, 2000.0f);
	}

	void RowBoat::OnPlayerEnter()
	{
		m_bOccupied = true;
	}

	void RowBoat::OnPlayerExit()
	{
		m_bOccupied = false;
		m_RowInput = 0.0f;
		m_TurnInput = 0.0f;
		m_OarInput[0] = m_OarInput[1] = 0.0f;
	}

	glm::vec3 RowBoat::GetExitPosition() const
	{
		const glm::vec3 pos = m_Transform.GetWorldPosition();
		real waterHeight;
		if (!SampleWaterHeight(pos, waterHeight))
		{
			// Beached
			return pos - m_Transform.GetRight() * 1.8f + VEC3_UP * 1.0f;
		}

		// Afloat: step onto the nearest dry land within reach, or stand up in the boat (between the seats)
		const glm::vec3 inBoat = pos + m_Transform.GetUp() * 0.85f + m_Transform.GetForward() * 0.6f;
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (terrainGenerator == nullptr)
		{
			return inBoat;
		}
		const u32 directionCount = 16;
		for (real radius = 2.0f; radius <= 14.0f; radius += 1.5f)
		{
			for (u32 i = 0; i < directionCount; ++i)
			{
				const real angle = TWO_PI * (real)i / (real)directionCount;
				const glm::vec3 candidate = pos + glm::vec3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
				real candidateWaterHeight;
				if (SampleWaterHeight(candidate, candidateWaterHeight))
				{
					continue;
				}
				glm::vec3 hitPos, hitNormal;
				if (terrainGenerator->Raycast(candidate + VEC3_UP * 30.0f, -VEC3_UP, 60.0f, hitPos, hitNormal) &&
					hitPos.y < pos.y + 4.0f && hitNormal.y > 0.5f)
				{
					return hitPos + VEC3_UP * 1.0f;
				}
			}
		}
		return inBoat;
	}

	namespace Rideable
	{
		bool IsRideable(GameObject* object)
		{
			return object != nullptr && (object->GetTypeID() == VehicleSID || object->GetTypeID() == RowBoatSID);
		}

		void OnPlayerEnter(GameObject* object)
		{
			if (object == nullptr)
			{
				return;
			}
			switch (object->GetTypeID())
			{
			case VehicleSID: ((Vehicle*)object)->OnPlayerEnter(); break;
			case RowBoatSID: ((RowBoat*)object)->OnPlayerEnter(); break;
			default: break;
			}
		}

		void OnPlayerExit(GameObject* object)
		{
			if (object == nullptr)
			{
				return;
			}
			switch (object->GetTypeID())
			{
			case VehicleSID: ((Vehicle*)object)->OnPlayerExit(); break;
			case RowBoatSID: ((RowBoat*)object)->OnPlayerExit(); break;
			default: break;
			}
		}

		glm::vec3 GetExitPosition(GameObject* object)
		{
			if (object->GetTypeID() == RowBoatSID)
			{
				return ((RowBoat*)object)->GetExitPosition();
			}
			Transform* transform = object->GetTransform();
			return transform->GetWorldPosition() - transform->GetRight() * 2.5f + VEC3_UP * 1.0f;
		}
	} // namespace Rideable
} // namespace flex
