#include "stdafx.hpp"

#include "Water.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
IGNORE_WARNINGS_POP

#include "Helpers.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/GameObjectTypes.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/Objects/Vehicle.hpp"
#include "Scene/TerrainGeneration.hpp"

namespace flex
{
	namespace water
	{
		bool SampleSurface(const glm::vec3& pos, real& outSurfaceHeight)
		{
			return RowBoat::SampleWaterHeight(pos, outSurfaceHeight);
		}

		real GetDensity(StringID typeID, const std::string& name)
		{
			// Mostly metal (or full of it): sinks
			switch (typeID)
			{
			case BatterySID: return 2600.0f;
			case VehicleSID: return 2500.0f;
			case MinerSID: return 3000.0f;
			case MineralDepositSID: return 2700.0f;
			case PickAxeSID: return 2200.0f;
			case AxeSID: return 1800.0f;
			case ShovelSID: return 1800.0f;
			case SolarPanelSID: return 2000.0f;
			case HeadLampSID: return 1500.0f;
			case SpeakerSID: return 1300.0f;
			case WirePlugSID: return 1500.0f;
			case SocketSID: return 1500.0f;
			case EngineCartSID: return 3000.0f;
			case EmptyCartSID: return 2500.0f;
			case BaseCartSID: return 2500.0f;
			case MobileLiquidBoxSID: return 1500.0f;
			case RowBoatSID: return 300.0f;
			case BedSID: return 500.0f;
			case PowerPoleSID: return 700.0f;
			case CampfireSID: return 600.0f;
			case TreasureChestSID: return 900.0f;
			default: break;
			}

			std::string lower = name;
			for (char& c : lower)
			{
				c = (char)tolower(c);
			}
			auto Has = [&lower](const char* s) { return lower.find(s) != std::string::npos; };
			if (Has("battery") || Has("vehicle") || Has("buggy") || Has("car") || Has("iron") || Has("tin") ||
				Has("alumin") || Has("stone") || Has("rock") || Has("quartz") || Has("silicon") || Has("olivine") ||
				Has("mineral") || Has("ore") || Has("metal") || Has("lamp") || Has("pickaxe") || Has("shovel"))
			{
				return 2500.0f;
			}
			if (Has("wood") || Has("log") || Has("plank") || Has("stick") || Has("boat"))
			{
				return 600.0f;
			}
			if (Has("charcoal"))
			{
				return 400.0f;
			}
			return DEFAULT_OBJECT_DENSITY;
		}

		real GetDensity(GameObject* object)
		{
			if (object == nullptr)
			{
				return DEFAULT_OBJECT_DENSITY;
			}
			return GetDensity(object->GetTypeID(), object->GetName());
		}

		real GetPrefabDensity(const PrefabID& prefabID)
		{
			GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(prefabID);
			return GetDensity(prefabTemplate);
		}

		void FixedUpdateBuoyancy(BaseScene* scene)
		{
			PROFILE_AUTO("Water buoyancy");

			PhysicsWorld* physicsWorld = scene != nullptr ? scene->GetPhysicsWorld() : nullptr;
			if (physicsWorld == nullptr || physicsWorld->GetWorld() == nullptr)
			{
				return;
			}

			btDiscreteDynamicsWorld* world = physicsWorld->GetWorld();
			const sec dt = g_FixedDeltaTime;
			btAlignedObjectArray<btRigidBody*>& bodies = world->getNonStaticRigidBodies();
			for (i32 i = 0; i < bodies.size(); ++i)
			{
				btRigidBody* body = bodies[i];
				if (body == nullptr || body->isKinematicObject() || body->getInvMass() <= 0.0f || !body->isActive())
				{
					continue;
				}
				GameObject* object = static_cast<GameObject*>(body->getUserPointer());
				if (object == nullptr)
				{
					continue;
				}
				// These handle water themselves
				const StringID typeID = object->GetTypeID();
				if (typeID == PlayerSID || typeID == RowBoatSID || typeID == DroppedItemSID)
				{
					continue;
				}

				btVector3 aabbMin, aabbMax;
				body->getAabb(aabbMin, aabbMax);
				const glm::vec3 center = ToVec3((aabbMin + aabbMax) * 0.5f);
				real surface;
				if (!SampleSurface(center, surface) || surface <= aabbMin.getY())
				{
					continue;
				}

				const real height = glm::max(aabbMax.getY() - aabbMin.getY(), 0.05f);
				const real submerged = glm::clamp((surface - aabbMin.getY()) / height, 0.0f, 1.0f);
				const real mass = 1.0f / body->getInvMass();
				const real volume = mass / GetDensity(object);
				// Archimedes: the weight of the water displaced
				const real buoyancy = DENSITY * GRAVITY_ACCEL * volume * submerged;
				body->applyCentralForce(btVector3(0.0f, buoyancy, 0.0f));

				// Water drag (much stronger than air)
				const btVector3 velocity = body->getLinearVelocity();
				body->setLinearVelocity(velocity * (1.0f - glm::min(1.5f * submerged * dt, 1.0f)));
				body->setAngularVelocity(body->getAngularVelocity() * (1.0f - glm::min(1.0f * submerged * dt, 1.0f)));
			}
		}

		void UpdateDespawning(BaseScene* scene)
		{
			if (scene == nullptr)
			{
				return;
			}

			// Nothing falls 600m in a fraction of a second, so only check now & then
			static sec s_Timer = 0.0f;
			s_Timer += g_DeltaTime;
			if (s_Timer < 0.5f)
			{
				return;
			}
			s_Timer = 0.0f;

			const real threshold = TerrainGenerator::GetActive() != nullptr ? terrain::DESPAWN_HEIGHT : scene->GetPlayerMinHeight() - 10.0f;
			std::vector<GameObject*> toRemove;
			for (GameObject* object : scene->GetRootObjects())
			{
				if (object == nullptr || object->GetTypeID() == PlayerSID)
				{
					continue;
				}
				RigidBody* rb = object->GetRigidBody();
				const bool bMovable = object->GetTypeID() == DroppedItemSID || (rb != nullptr && !rb->IsStatic() && !rb->IsKinematic());
				if (bMovable && object->GetTransform()->GetWorldPosition().y < threshold)
				{
					toRemove.push_back(object);
				}
			}

			for (GameObject* object : toRemove)
			{
				const glm::vec3 pos = object->GetTransform()->GetWorldPosition();
				Print("Despawning %s (%s), it fell below the world (y = %.1f, limit %.1f)\n", object->GetName().c_str(),
					BaseScene::GameObjectTypeIDToString(object->GetTypeID()), pos.y, threshold);
			}
			if (!toRemove.empty())
			{
				scene->RemoveObjects(toRemove, true);
			}
		}
	} // namespace water
} // namespace flex
