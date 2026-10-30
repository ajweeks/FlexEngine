#include "stdafx.hpp"

#include "Scene/Objects/Animals.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtx/norm.hpp> // For distance2
#include <glm/gtx/quaternion.hpp>

#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
IGNORE_WARNINGS_POP

#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "FlexEngine.hpp"
#include "GameControls.hpp"
#include "GameUI.hpp"
#include "Graphics/Renderer.hpp"
#include "Graphics/VertexBufferData.hpp"
#include "InputManager.hpp"
#include "JSONTypes.hpp"
#include "Localization.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Platform/Platform.hpp"
#include "Player.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/Objects/Building.hpp" // For AppendBoxToVertexData & fence posts
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/TerrainGeneration.hpp"

namespace flex
{
	AnimalRenderer* AnimalRenderer::s_Instance = nullptr;

	namespace
	{
		enum class AnimalState : u8
		{
			IDLE,   // Standing (grazing, pecking...)
			WANDER, // Walking to a goal
			SLEEP,
			FOLLOW, // Tamed, led by the player's lasso
			TIED,   // Tamed, tied to a fence post
			FLY,    // Birds in the air
			FLEE,

			_COUNT
		};

		enum AnimalFlags : u8
		{
			TAMED = 1 << 0,
			AIRBORNE = 1 << 1,
			STAY = 1 << 2, // Tamed & left to wander where it was let go
		};

		struct SpeciesInfo
		{
			const char* name;
			real walkSpeed;
			real runSpeed; // Also flying speed for birds
			real maxSlope; // Rise over run per step
			bool bFlies;
			real sleepSunHeight; // Sleeps while the sun's direction is lower than this (y)
			real radius; // For aiming the lasso
			real neckHeight; // Where the lasso's rope ties on
			real roamRadius;
			u32 groupMin;
			u32 groupMax;
		};

		// Indexed by AnimalSpecies. Chickens & roosters go to bed at dusk, squirrels just after; crows roost once it's
		// dark; cows, goats, horses & gulls sleep later in the night.
		const SpeciesInfo s_Species[] =
		{
			{ "goat",     1.3f, 5.0f, 2.6f, false, -0.12f, 0.8f, 0.85f, 90.0f, 2, 5 },
			{ "cow",      0.9f, 3.5f, 0.7f, false, -0.18f, 1.3f, 1.25f, 70.0f, 2, 4 },
			{ "chicken",  0.6f, 3.0f, 0.8f, false,  0.14f, 0.45f, 0.35f, 30.0f, 3, 5 },
			{ "crow",     0.8f, 9.0f, 1.0f, true,   0.0f, 0.5f, 0.25f, 120.0f, 2, 4 },
			{ "seagull",  0.8f, 10.0f, 1.0f, true, -0.1f, 0.55f, 0.3f, 150.0f, 2, 4 },
			{ "rooster",  0.6f, 3.0f, 0.8f, false,  0.1f, 0.5f, 0.45f, 30.0f, 1, 1 },
			{ "squirrel", 1.6f, 6.0f, 1.6f, false,  0.06f, 0.4f, 0.2f, 40.0f, 1, 3 },
			{ "horse",    1.4f, 9.0f, 0.8f, false, -0.2f, 1.4f, 1.6f, 100.0f, 1, 3 },
		};
		static_assert(ARRAY_LENGTH(s_Species) == (u32)AnimalSpecies::_COUNT, "s_Species must match AnimalSpecies");

		const SpeciesInfo& Info(AnimalSpecies species) { return s_Species[(u32)species]; }

		struct Animal
		{
			u32 id = 0;
			u32 homeCell = 0xFFFFFFFF; // Cell the animal was spawned for (none once tamed)
			AnimalSpecies species = AnimalSpecies::COW;
			AnimalState state = AnimalState::IDLE;
			u8 flags = 0;
			u8 variant = 0; // Colouring

			glm::vec3 pos = VEC3_ZERO;
			real heading = 0.0f; // Radians, forward = (sin, 0, cos)
			glm::vec3 home = VEC3_ZERO; // Centre of its roaming (or the post it's tied to)
			glm::vec3 goal = VEC3_ZERO;

			// The step being taken (pos is interpolated from 'from' to 'to')
			glm::vec3 from = VEC3_ZERO;
			glm::vec3 to = VEC3_ZERO;
			real stepTime = 0.0f;
			real stepDuration = 0.0f;
			real stateTimer = 0.0f;

			real walkPhase = 0.0f;
			real sleepBias = 0.0f; // So a herd doesn't all fall asleep in the same frame
			real lastUpdate = 0.0f; // Sim time of its last update (far animals are updated round-robin)
			std::string name;

			real petTimer = 0.0f; // Being petted: faces the player, hops & wiggles
			real calmTimer = 0.0f; // Wild animals petted recently don't flee
			real heartTimer = 0.0f;
			// Flocking (set each frame from nearby animals of the same species)
			bool bFollower = false;
			bool bLeaderMoving = false;
			u32 leaderID = 0;
			glm::vec3 leaderPos = VEC3_ZERO;
			real leaderHeading = 0.0f;
			real flockSpacing = 2.0f;
		};

		struct Heart
		{
			glm::vec3 pos;
			glm::vec3 vel;
			real age;
		};

		// Everything about the world's animals (kept outside the renderer object, which belongs to the scene)
		struct AnimalWorld
		{
			std::vector<Animal> animals;
			std::vector<Heart> hearts;
			std::unordered_set<u32> cells; // Cells whose animals have been spawned
			u32 nextID = 1;
			real time = 0.0f;
			real spawnTimer = 0.0f;
			real forgetTimer = 0.0f;
			u32 farCursor = 0;
			u32 simulatedCount = 0;

			// Naming dialog
			bool bNaming = false;
			u32 namingID = 0;
			std::string nameText;

			// Aim prompt
			std::string prompt;
		};
		AnimalWorld s_World;

		const real NEAR_RADIUS = 250.0f; // Updated every frame (& drawn) within this
		const real DRAW_RADIUS = 220.0f;
		const real DETAIL_RADIUS = 90.0f;
		const real FORGET_RADIUS = 5000.0f;
		const real LASSO_RANGE = 22.0f;
		const real TIE_ROPE_LENGTH = 2.5f;

		// Deterministic hashing (every random choice derives from the world seed)
		u32 Hash(u32 x)
		{
			x ^= x >> 16;
			x *= 0x7FEB352Du;
			x ^= x >> 15;
			x *= 0x846CA68Bu;
			x ^= x >> 16;
			return x;
		}

		u32 Hash(u32 a, u32 b) { return Hash(a ^ Hash(b + 0x9E3779B9u)); }

		struct Rng
		{
			u32 state;
			explicit Rng(u32 seed) : state(Hash(seed) | 1u) {}
			u32 Next() { state = Hash(state + 0x6D2B79F5u); return state; }
			real Float() { return (real)(Next() & 0xFFFFFF) / (real)0x1000000; }
			real Range(real a, real b) { return a + (b - a) * Float(); }
			u32 RangeU(u32 a, u32 b) { return a + Next() % (b - a + 1); }
		};

		u32 CellKey(i32 cx, i32 cz) { return ((u32)(cx & 0xFFFF) << 16) | (u32)(cz & 0xFFFF); }
		glm::vec2 CellCentre(u32 key)
		{
			const i32 cx = (i32)(i16)(u16)(key >> 16);
			const i32 cz = (i32)(i16)(u16)(key & 0xFFFF);
			return glm::vec2(((real)cx + 0.5f) * Animals::CELL_SIZE, ((real)cz + 0.5f) * Animals::CELL_SIZE);
		}

		glm::vec3 HeadingDir(real heading) { return glm::vec3(std::sin(heading), 0.0f, std::cos(heading)); }

		real WrapAngle(real a)
		{
			while (a > PI) a -= TWO_PI;
			while (a < -PI) a += TWO_PI;
			return a;
		}

		real SunHeight()
		{
			BaseScene* scene = g_SceneManager->CurrentScene();
			return scene != nullptr ? BaseScene::GetSunDirection(scene->GetTimeOfDay()).y : 1.0f;
		}

		bool IsUnderWater(const terrain::ColumnSample& column)
		{
			return column.lakeID != 0 && column.lakeT < 1.0f && column.height < column.waterLevel - 0.15f;
		}

		Player* GetPlayer()
		{
			BaseScene* scene = g_SceneManager->CurrentScene();
			return scene != nullptr ? scene->GetPlayer(0) : nullptr;
		}

		glm::vec3 PlayerFeet(Player* player)
		{
			return player->GetTransform()->GetWorldPosition() - VEC3_UP * (player->GetHeight() * 0.5f);
		}

		// Ground height below pos (including digs & fills near the player, where the terrain's density is ray cast)
		real GroundHeight(TerrainGenerator* terrain, const glm::vec3& pos, bool bPrecise, bool* outUnderWater = nullptr, real* outWaterLevel = nullptr)
		{
			const terrain::ColumnSample column = terrain->SampleColumn(pos.x, pos.z);
			real height = column.height;
			if (outUnderWater != nullptr)
			{
				*outUnderWater = IsUnderWater(column);
			}
			if (outWaterLevel != nullptr)
			{
				*outWaterLevel = column.lakeID != 0 && column.lakeT < 1.0f ? column.waterLevel : -1e9f;
			}
			if (bPrecise)
			{
				glm::vec3 hitPos, hitNormal;
				const glm::vec3 origin(pos.x, glm::max(height, pos.y) + 3.0f, pos.z);
				if (terrain->Raycast(origin, -VEC3_UP, 12.0f, hitPos, hitNormal))
				{
					height = hitPos.y;
				}
			}
			return height;
		}

		// Whether a built structure (wall, fence post...) blocks the way from a to b
		bool IsBlockedByStructure(const glm::vec3& a, const glm::vec3& b)
		{
			BaseScene* scene = g_SceneManager->CurrentScene();
			if (scene == nullptr || scene->GetPhysicsWorld() == nullptr || glm::distance2(a, b) < 1e-4f)
			{
				return false;
			}
			const btVector3 btFrom = ToBtVec3(a);
			const btVector3 btTo = ToBtVec3(b);
			btCollisionWorld::AllHitsRayResultCallback callback(btFrom, btTo);
			scene->GetPhysicsWorld()->GetWorld()->rayTest(btFrom, btTo, callback);
			for (i32 i = 0; i < callback.m_collisionObjects.size(); ++i)
			{
				const GameObject* object = static_cast<const GameObject*>(callback.m_collisionObjects[i]->getUserPointer());
				if (object != nullptr && (object->GetTypeID() == BuildPieceSID || object->GetTypeID() == BedSID))
				{
					return true;
				}
			}
			return false;
		}

		// Picks the next step towards desiredHeading: tries headings around it, taking the first whose end isn't too
		// steep, under water or blocked. Returns false (with outTo = pos) when there's nowhere to go.
		bool PlanStep(TerrainGenerator* terrain, const Animal& animal, real desiredHeading, real length, bool bNear, glm::vec3& outTo, real& outHeading)
		{
			const SpeciesInfo& info = Info(animal.species);
			static const real offsets[] = { 0.0f, 0.6f, -0.6f, 1.2f, -1.2f, 2.0f, -2.0f, PI };
			for (real offset : offsets)
			{
				const real heading = desiredHeading + offset;
				const glm::vec3 dir = HeadingDir(heading);
				glm::vec3 target = animal.pos + dir * length;
				bool bUnderWater = false;
				target.y = GroundHeight(terrain, target, bNear, &bUnderWater);
				if (bUnderWater)
				{
					continue;
				}
				const real slope = (target.y - animal.pos.y) / length;
				if (slope > info.maxSlope || slope < -info.maxSlope * 1.6f)
				{
					continue;
				}
				if (bNear)
				{
					const glm::vec3 up = VEC3_UP * glm::max(info.neckHeight * 0.6f, 0.2f);
					if (IsBlockedByStructure(animal.pos + up, target + up + dir * 0.4f))
					{
						continue;
					}
				}
				outTo = target;
				outHeading = heading;
				return true;
			}
			outTo = animal.pos;
			outHeading = desiredHeading + PI;
			return false;
		}

		bool ShouldSleep(const Animal& animal, real sunHeight)
		{
			if (animal.state == AnimalState::FOLLOW || animal.state == AnimalState::FLEE)
			{
				return false;
			}
			const real threshold = Info(animal.species).sleepSunHeight + animal.sleepBias;
			// (A little hysteresis, so they don't flicker between asleep & awake)
			return animal.state == AnimalState::SLEEP ? sunHeight < threshold + 0.03f : sunHeight < threshold;
		}

		u32 SpawnAnimal(AnimalSpecies species, const glm::vec3& pos, u32 cell, u32 seed)
		{
			Rng rng(seed);
			Animal animal = {};
			animal.id = s_World.nextID++;
			animal.homeCell = cell;
			animal.species = species;
			animal.state = AnimalState::IDLE;
			animal.variant = (u8)(rng.Next() % 3);
			animal.pos = animal.from = animal.to = animal.home = animal.goal = pos;
			animal.heading = rng.Range(-PI, PI);
			animal.stepDuration = rng.Range(0.5f, 3.0f);
			animal.sleepBias = rng.Range(-0.04f, 0.04f);
			animal.walkPhase = rng.Range(0.0f, TWO_PI);
			animal.lastUpdate = s_World.time;
			if (Info(species).bFlies && rng.Float() < 0.5f)
			{
				animal.state = AnimalState::FLY;
				animal.flags |= AIRBORNE;
				animal.pos.y += rng.Range(10.0f, 25.0f);
				animal.from = animal.to = animal.pos;
			}
			s_World.animals.push_back(animal);
			return animal.id;
		}

		// Spawns a cell's animals (deterministic for the world's seed), appropriate to its biome
		void SpawnCell(TerrainGenerator* terrain, u32 key)
		{
			s_World.cells.insert(key);
			terrain::TerrainStreamer* streamer = terrain->GetStreamer();
			if (streamer == nullptr)
			{
				return;
			}
			const std::vector<terrain::BiomeParams>& biomes = streamer->Params().biomes;
			Rng rng(Hash((u32)terrain->GetSeed() ^ 0xA11A15u, key));
			const glm::vec2 centre = CellCentre(key);
			const u32 groupCount = rng.Float() < 0.6f ? 1 : (rng.Float() < 0.5f ? 0 : 2);
			for (u32 group = 0; group < groupCount; ++group)
			{
				const glm::vec2 xz = centre + glm::vec2(rng.Range(-0.45f, 0.45f), rng.Range(-0.45f, 0.45f)) * Animals::CELL_SIZE;
				const terrain::ColumnSample column = terrain->SampleColumn(xz.x, xz.y);
				const bool bWater = IsUnderWater(column);
				const bool bCoast = column.lakeID == terrain::OCEAN_ID && column.oceanWeight > 0.12f;
				terrain::Vegetation vegetation = terrain::Vegetation::BROADLEAF;
				if (column.primaryBiome >= 0 && column.primaryBiome < (i32)biomes.size())
				{
					vegetation = biomes[column.primaryBiome].vegetation;
				}

				AnimalSpecies species = AnimalSpecies::_COUNT;
				const real roll = rng.Float();
				if (bCoast || (bWater && column.lakeID == terrain::OCEAN_ID))
				{
					species = AnimalSpecies::SEAGULL;
				}
				else if (bWater)
				{
					continue;
				}
				else
				{
					using V = terrain::Vegetation;
					switch (vegetation)
					{
					case V::ALPINE:
					case V::TUNDRA:
						species = roll < 0.75f ? AnimalSpecies::GOAT : AnimalSpecies::CROW;
						break;
					case V::BROADLEAF:
					case V::NORDIC:
						species = roll < 0.25f ? AnimalSpecies::COW : roll < 0.42f ? AnimalSpecies::HORSE : roll < 0.62f ? AnimalSpecies::CHICKEN :
							roll < 0.84f ? AnimalSpecies::SQUIRREL : AnimalSpecies::CROW;
						break;
					case V::SAVANNA:
						species = roll < 0.4f ? AnimalSpecies::HORSE : roll < 0.75f ? AnimalSpecies::COW : AnimalSpecies::CROW;
						break;
					case V::TAIGA:
					case V::JUNGLE:
					case V::SWAMP:
						species = roll < 0.55f ? AnimalSpecies::SQUIRREL : roll < 0.8f ? AnimalSpecies::CROW : AnimalSpecies::GOAT;
						break;
					case V::DESERT:
					case V::VOLCANIC:
						species = roll < 0.4f ? AnimalSpecies::CROW : AnimalSpecies::_COUNT;
						break;
					default:
						species = roll < 0.5f ? AnimalSpecies::CROW : AnimalSpecies::_COUNT;
						break;
					}
					// Steep, high ground has goats whatever grows there
					if (column.massifWeight > 0.6f && roll > 0.5f)
					{
						species = AnimalSpecies::GOAT;
					}
				}
				if (species == AnimalSpecies::_COUNT)
				{
					continue;
				}

				const SpeciesInfo& info = Info(species);
				const u32 count = rng.RangeU(info.groupMin, info.groupMax);
				for (u32 i = 0; i < count + (species == AnimalSpecies::CHICKEN ? 1u : 0u); ++i)
				{
					const AnimalSpecies memberSpecies = (species == AnimalSpecies::CHICKEN && i == count) ? AnimalSpecies::ROOSTER : species;
					glm::vec3 pos(xz.x + rng.Range(-6.0f, 6.0f), 0.0f, xz.y + rng.Range(-6.0f, 6.0f));
					bool bUnderWater = false;
					real waterLevel = 0.0f;
					pos.y = GroundHeight(terrain, pos, false, &bUnderWater, &waterLevel);
					if (bUnderWater)
					{
						if (!info.bFlies)
						{
							continue;
						}
						pos.y = waterLevel;
					}
					SpawnAnimal(memberSpecies, pos, key, rng.Next());
				}
			}
		}

		// Where a pack follower aims for, by the pack's pattern (a trailing line, a V or a loose huddle around the leader)
		glm::vec3 FlockSlot(const Animal& animal)
		{
			const u32 pattern = Hash(animal.leaderID, 77u) % 3;
			const real rank = 1.0f + (real)(Hash(animal.id, 5u) % 4);
			const glm::vec3 back = -HeadingDir(animal.leaderHeading);
			const glm::vec3 side = glm::vec3(back.z, 0.0f, -back.x);
			const real spacing = animal.flockSpacing;
			const real sign = (Hash(animal.id, 9u) & 1u) ? 1.0f : -1.0f;
			glm::vec3 offset;
			if (pattern == 0)
			{
				offset = back * spacing * rank;
			}
			else if (pattern == 1)
			{
				offset = back * spacing * rank * 0.8f + side * sign * spacing * rank * 0.7f;
			}
			else
			{
				const real a = (real)(Hash(animal.id, 13u) % 628) * 0.01f;
				offset = glm::vec3(std::sin(a), 0.0f, std::cos(a)) * spacing * 1.6f;
			}
			glm::vec3 result = animal.leaderPos + offset;
			result.y = animal.pos.y;
			return result;
		}

		void StartStep(Animal& animal, const glm::vec3& to, real duration)
		{
			animal.from = animal.pos;
			animal.to = to;
			animal.stepTime = 0.0f;
			animal.stepDuration = glm::max(duration, 0.05f);
		}

		void Think(TerrainGenerator* terrain, Animal& animal, Player* player, bool bNear, real sunHeight)
		{
			const SpeciesInfo& info = Info(animal.species);
			Rng rng(Hash(animal.id, (u32)(s_World.time * 10.0f)));
			const bool bTamed = (animal.flags & TAMED) != 0;
			const bool bAirborne = (animal.flags & AIRBORNE) != 0;
			// Steps are short near the player (so they follow the ground closely) & long far away (cheap)
			const real stepTime = bNear ? 1.0f : 8.0f;

			// Wake up / fall asleep
			if (ShouldSleep(animal, sunHeight))
			{
				if (bAirborne)
				{
					// Land first
					glm::vec3 ground = animal.pos;
					real waterLevel = -1e9f;
					ground.y = glm::max(GroundHeight(terrain, ground, bNear, nullptr, &waterLevel), waterLevel);
					animal.flags &= ~AIRBORNE;
					StartStep(animal, ground, glm::max(glm::distance(ground, animal.pos) / 3.0f, 0.5f));
					animal.state = AnimalState::WANDER;
					return;
				}
				animal.state = AnimalState::SLEEP;
				StartStep(animal, animal.pos, rng.Range(4.0f, 8.0f));
				return;
			}
			if (animal.state == AnimalState::SLEEP)
			{
				animal.state = AnimalState::IDLE;
			}

			const glm::vec3 playerFeet = player != nullptr ? PlayerFeet(player) : animal.pos;
			const real playerDist = glm::distance(glm::vec2(playerFeet.x, playerFeet.z), glm::vec2(animal.pos.x, animal.pos.z));

			// Wild animals keep away from the player
			if (animal.petTimer > 0.0f && !bAirborne)
			{
				// Being petted: turn to the player & stay close
				const glm::vec3 toPlayer = playerFeet - animal.pos;
				animal.heading = std::atan2(toPlayer.x, toPlayer.z);
				if (bTamed && animal.state != AnimalState::TIED && playerDist > 2.2f && playerDist < 20.0f)
				{
					const real heading = animal.heading;
					glm::vec3 to;
					real newHeading;
					if (PlanStep(terrain, animal, heading, glm::min(0.7f, playerDist - 1.8f), true, to, newHeading))
					{
						animal.heading = newHeading;
						animal.state = AnimalState::WANDER;
						StartStep(animal, to, glm::max(glm::distance(to, animal.pos) / (info.walkSpeed * 1.5f), 0.1f));
						return;
					}
				}
				StartStep(animal, animal.pos, 0.3f);
				return;
			}

			if (!bTamed && bNear && animal.calmTimer <= 0.0f && playerDist < (info.bFlies ? 7.0f : 5.0f) && animal.state != AnimalState::FLEE)
			{
				if (info.bFlies)
				{
					animal.state = AnimalState::FLY;
					animal.flags |= AIRBORNE;
				}
				else
				{
					animal.state = AnimalState::FLEE;
					animal.stateTimer = rng.Range(2.5f, 4.0f);
				}
			}

			switch (animal.state)
			{
			case AnimalState::FOLLOW:
			{
				if (playerDist > 80.0f)
				{
					// Left far behind (e.g. the player teleported): catch up
					glm::vec3 pos = playerFeet - HeadingDir(animal.heading) * 3.0f;
					pos.y = GroundHeight(terrain, pos, true);
					animal.pos = animal.from = animal.to = pos;
				}
				if (playerDist < 3.5f)
				{
					StartStep(animal, animal.pos, 0.4f);
					return;
				}
				const real speed = playerDist > 9.0f ? info.runSpeed : info.walkSpeed * 1.6f;
				const glm::vec3 toPlayer = playerFeet - animal.pos;
				const real heading = std::atan2(toPlayer.x, toPlayer.z);
				const real length = glm::min(speed * 0.5f, playerDist - 2.5f);
				glm::vec3 to;
				real newHeading;
				// Led animals can be pulled up steeper slopes than they'd choose
				if (!PlanStep(terrain, animal, heading, glm::max(length, 0.3f), true, to, newHeading))
				{
					to = animal.pos + HeadingDir(heading) * glm::max(length, 0.3f);
					to.y = GroundHeight(terrain, to, true);
					newHeading = heading;
				}
				animal.heading = newHeading;
				StartStep(animal, to, glm::distance(to, animal.pos) / speed);
				return;
			}
			case AnimalState::FLEE:
			{
				animal.stateTimer -= animal.stepDuration;
				if (animal.stateTimer <= 0.0f)
				{
					animal.state = AnimalState::IDLE;
					StartStep(animal, animal.pos, rng.Range(1.0f, 3.0f));
					return;
				}
				const glm::vec3 away = animal.pos - playerFeet;
				const real heading = std::atan2(away.x, away.z) + rng.Range(-0.4f, 0.4f);
				const real length = info.runSpeed * 0.5f;
				glm::vec3 to;
				real newHeading;
				PlanStep(terrain, animal, heading, length, bNear, to, newHeading);
				animal.heading = newHeading;
				StartStep(animal, to, glm::max(glm::distance(to, animal.pos) / info.runSpeed, 0.3f));
				return;
			}
			case AnimalState::FLY:
			{
				// Circle about, then land somewhere
				animal.stateTimer -= animal.stepDuration;
				const real length = info.runSpeed * (bNear ? 1.0f : 6.0f);
				real heading = animal.heading + rng.Range(-0.5f, 0.5f);
				const glm::vec3 toHome = animal.home - animal.pos;
				if (glm::length(glm::vec2(toHome.x, toHome.z)) > info.roamRadius)
				{
					heading = std::atan2(toHome.x, toHome.z);
				}
				glm::vec3 to = animal.pos + HeadingDir(heading) * length;
				real waterLevel = -1e9f;
				const real ground = glm::max(GroundHeight(terrain, to, false, nullptr, &waterLevel), waterLevel);
				const bool bLand = animal.stateTimer < -20.0f && rng.Float() < 0.35f;
				if (bLand)
				{
					to.y = ground;
					animal.flags &= ~AIRBORNE;
					animal.state = AnimalState::IDLE;
					animal.stateTimer = 0.0f;
				}
				else
				{
					to.y = glm::clamp(animal.pos.y + rng.Range(-3.0f, 3.0f), ground + 8.0f, ground + 35.0f);
				}
				animal.heading = heading;
				StartStep(animal, to, glm::distance(to, animal.pos) / info.runSpeed);
				return;
			}
			default:
				break;
			}

			// Idle & wandering about (around home, or the post it's tied to)
			const bool bTied = animal.state == AnimalState::TIED;
			const real roam = bTied ? TIE_ROPE_LENGTH : info.roamRadius;
			const glm::vec3 centre = animal.home;
			if (!bTied && info.bFlies && rng.Float() < 0.15f)
			{
				animal.state = AnimalState::FLY;
				animal.flags |= AIRBORNE;
				animal.stateTimer = 0.0f;
				glm::vec3 to = animal.pos + HeadingDir(animal.heading) * 4.0f + VEC3_UP * 5.0f;
				StartStep(animal, to, 1.0f);
				return;
			}

			const glm::vec3 toGoal = animal.goal - animal.pos;
			const real goalDist = glm::length(glm::vec2(toGoal.x, toGoal.z));
			if (bTied && glm::distance(glm::vec2(animal.pos.x, animal.pos.z), glm::vec2(centre.x, centre.z)) > TIE_ROPE_LENGTH && goalDist > 0.6f)
			{
				// Being pulled towards the post: keep going
			}
			else if (!bTied && animal.bFollower && glm::distance(glm::vec2(animal.pos.x, animal.pos.z), glm::vec2(animal.leaderPos.x, animal.leaderPos.z)) > animal.flockSpacing + 5.0f)
			{
				// Fallen behind the pack: catch up
				animal.goal = FlockSlot(animal);
				animal.state = AnimalState::WANDER;
			}
			else if (animal.state != AnimalState::WANDER || goalDist < 0.6f)
			{
				if (rng.Float() < (animal.bFollower && animal.bLeaderMoving ? 0.1f : 0.5f))
				{
					// Stand about for a while
					animal.state = bTied ? AnimalState::TIED : AnimalState::IDLE;
					StartStep(animal, animal.pos, rng.Range(2.0f, 7.0f) * (bNear ? 1.0f : 2.0f));
					if (bTied)
					{
						animal.state = AnimalState::TIED;
					}
					return;
				}
				const real angle = rng.Range(-PI, PI);
				const real dist = roam * glm::sqrt(rng.Float());
				animal.goal = centre + glm::vec3(std::sin(angle), 0.0f, std::cos(angle)) * dist;
				if (!bTied && animal.bFollower)
				{
					animal.goal = FlockSlot(animal);
				}
				if (!bTied)
				{
					animal.state = AnimalState::WANDER;
				}
			}

			const glm::vec3 dir = animal.goal - animal.pos;
			const real dist = glm::length(glm::vec2(dir.x, dir.z));
			const real heading = std::atan2(dir.x, dir.z);
			real length = glm::min(info.walkSpeed * stepTime, dist);
			if (bTied)
			{
				length = glm::min(length, 1.0f);
			}
			glm::vec3 to;
			real newHeading;
			if (length < 0.1f || !PlanStep(terrain, animal, heading, glm::max(length, 0.1f), bNear, to, newHeading))
			{
				// Nowhere to go that way: pick another goal next time
				animal.goal = animal.pos;
				animal.heading = WrapAngle(animal.heading + rng.Range(1.0f, 2.5f));
				StartStep(animal, animal.pos, 1.0f);
				return;
			}
			const real centreDist = glm::distance(glm::vec2(animal.pos.x, animal.pos.z), glm::vec2(centre.x, centre.z));
			if (bTied && glm::distance(glm::vec2(to.x, to.z), glm::vec2(centre.x, centre.z)) > glm::max(TIE_ROPE_LENGTH, centreDist - 0.05f))
			{
				// (Outside the rope's reach, e.g. just tied: it's pulled in towards the post)
				if (centreDist > TIE_ROPE_LENGTH)
				{
					animal.goal = centre + (animal.pos - centre) * (TIE_ROPE_LENGTH * 0.6f / centreDist);
				}
				animal.goal = animal.pos;
				StartStep(animal, animal.pos, 1.0f);
				return;
			}
			animal.heading = newHeading;
			StartStep(animal, to, glm::distance(to, animal.pos) / info.walkSpeed);
		}

		void Advance(TerrainGenerator* terrain, Animal& animal, real dt, Player* player, bool bNear, real sunHeight)
		{
			// (Far animals may be given a lot of time at once: only a few steps are taken)
			for (u32 iteration = 0; iteration < 4 && dt > 0.0f; ++iteration)
			{
				const real remaining = animal.stepDuration - animal.stepTime;
				const real used = glm::min(remaining, dt);
				animal.stepTime += used;
				dt -= used;
				const real t = animal.stepDuration > 0.0f ? glm::clamp(animal.stepTime / animal.stepDuration, 0.0f, 1.0f) : 1.0f;
				const glm::vec3 prev = animal.pos;
				animal.pos = glm::mix(animal.from, animal.to, t);
				animal.walkPhase += glm::length(glm::vec2(animal.pos.x - prev.x, animal.pos.z - prev.z)) * 4.0f;
				if (animal.stepTime >= animal.stepDuration)
				{
					Think(terrain, animal, player, bNear, sunHeight);
				}
			}
			if (animal.walkPhase > 1000.0f)
			{
				animal.walkPhase -= TWO_PI * 150.0f;
			}
		}

		//
		// Lasso
		//

		bool IsWieldingLasso(Player* player)
		{
			static PrefabID s_LassoID = InvalidPrefabID;
			if (!s_LassoID.IsValid())
			{
				s_LassoID = g_ResourceManager->GetPrefabID("lasso");
			}
			InventoryType inventoryType;
			GameObjectStack* stack = player->GetGameObjectStackFromInventory(Player::GetGameObjectStackIDForQuickAccessInventory((u32)player->GetSelectedQuickAccessItemSlot()), inventoryType);
			return stack != nullptr && stack->count > 0 && s_LassoID.IsValid() && stack->prefabID == s_LassoID;
		}

		Animal* FindAnimal(u32 id)
		{
			for (Animal& animal : s_World.animals)
			{
				if (animal.id == id)
				{
					return &animal;
				}
			}
			return nullptr;
		}

		// The animal under the crosshair (within range)
		Animal* FindAimedAnimal(const glm::vec3& origin, const glm::vec3& dir, real range)
		{
			Animal* best = nullptr;
			real bestT = range;
			for (Animal& animal : s_World.animals)
			{
				const SpeciesInfo& info = Info(animal.species);
				const glm::vec3 centre = animal.pos + VEC3_UP * (info.neckHeight * 0.7f);
				const glm::vec3 toCentre = centre - origin;
				const real t = glm::dot(toCentre, dir);
				if (t < 0.0f || t > bestT)
				{
					continue;
				}
				const real missSq = glm::length2(toCentre - dir * t);
				const real radius = info.radius + 0.25f + t * 0.01f; // A little forgiving further away
				if (missSq < radius * radius)
				{
					best = &animal;
					bestT = t;
				}
			}
			return best;
		}

		BuildPiece* FindAimedFencePost(const glm::vec3& origin, const glm::vec3& dir, real range)
		{
			BuildPiece* best = nullptr;
			real bestT = range;
			for (BuildPiece* piece : BuildPiece::GetAllPieces())
			{
				if (piece->GetPieceType() != BuildPieceType::FENCE_POST)
				{
					continue;
				}
				const glm::vec3 centre = piece->GetTransform()->GetWorldPosition() + VEC3_UP * 0.8f;
				const glm::vec3 toCentre = centre - origin;
				const real t = glm::dot(toCentre, dir);
				if (t < 0.0f || t > bestT)
				{
					continue;
				}
				if (glm::length2(toCentre - dir * t) < 0.6f * 0.6f)
				{
					best = piece;
					bestT = t;
				}
			}
			return best;
		}

		bool FencePostExistsAt(const glm::vec3& pos)
		{
			for (BuildPiece* piece : BuildPiece::GetAllPieces())
			{
				if (piece->GetPieceType() == BuildPieceType::FENCE_POST && glm::distance2(piece->GetTransform()->GetWorldPosition(), pos) < 0.25f)
				{
					return true;
				}
			}
			return false;
		}

		Animal* FindLedAnimal(const glm::vec3& nearPos)
		{
			Animal* best = nullptr;
			real bestDistSq = FLT_MAX;
			for (Animal& animal : s_World.animals)
			{
				const real distSq = glm::distance2(animal.pos, nearPos);
				if (animal.state == AnimalState::FOLLOW && distSq < bestDistSq)
				{
					best = &animal;
					bestDistSq = distSq;
				}
			}
			return best;
		}

		std::string DisplayName(const Animal& animal)
		{
			return animal.name.empty() ? std::string(AnimalSpeciesDisplayName(animal.species)) : animal.name;
		}

		void Tame(Animal& animal, Player* player)
		{
			animal.flags = (u8)((animal.flags | TAMED) & ~AIRBORNE & ~STAY);
			animal.homeCell = 0xFFFFFFFF;
			animal.state = AnimalState::FOLLOW;
			real waterLevel = -1e9f;
			TerrainGenerator* terrain = TerrainGenerator::GetActive();
			if (terrain != nullptr)
			{
				animal.pos.y = glm::max(GroundHeight(terrain, animal.pos, true, nullptr, &waterLevel), waterLevel);
			}
			StartStep(animal, animal.pos, 0.3f);
			if (player != nullptr)
			{
				player->ShowNotification(LOCF(Animal_Tamed, AnimalSpeciesDisplayName(animal.species)));
			}
			s_World.bNaming = true;
			s_World.namingID = animal.id;
			s_World.nameText.clear();
		}

		void TieTo(Animal& animal, const glm::vec3& postPos, Player* player)
		{
			animal.state = AnimalState::TIED;
			animal.home = postPos;
			const real dist = glm::distance(glm::vec2(animal.pos.x, animal.pos.z), glm::vec2(postPos.x, postPos.z));
			animal.goal = dist > TIE_ROPE_LENGTH ? postPos + (animal.pos - postPos) * (TIE_ROPE_LENGTH * 0.6f / dist) : animal.pos;
			StartStep(animal, animal.pos, 0.5f);
			if (player != nullptr)
			{
				player->ShowNotification(LOCF(Animal_Tied, DisplayName(animal).c_str()));
			}
		}

		void UpdateLasso(Player* player)
		{
			s_World.prompt.clear();
			if (player == nullptr || s_World.bNaming || player->IsAnyInventoryShowing() || player->GetBuildMode().IsActive() ||
				g_EngineInstance->IsSimulationPaused() || !IsWieldingLasso(player))
			{
				return;
			}
			BaseCamera* camera = g_CameraManager->CurrentCamera();
			const glm::vec3 origin = camera->position;
			const glm::vec3 dir = camera->forward;
			Animal* aimed = FindAimedAnimal(origin, dir, LASSO_RANGE);
			Animal* led = FindLedAnimal(origin);
			BuildPiece* post = led != nullptr ? FindAimedFencePost(origin, dir, 8.0f) : nullptr;
			const bool bClick = g_InputManager->IsMouseButtonPressed(MouseButton::LEFT);

			if (post != nullptr && (aimed == nullptr || aimed->state == AnimalState::FOLLOW))
			{
				s_World.prompt = LOCF(Animal_PromptTie, DisplayName(*led).c_str());
				if (bClick)
				{
					TieTo(*led, post->GetTransform()->GetWorldPosition(), player);
				}
				return;
			}
			if (aimed == nullptr)
			{
				return;
			}
			const std::string name = DisplayName(*aimed);
			if ((aimed->flags & TAMED) == 0)
			{
				s_World.prompt = LOCF(Animal_PromptLasso, name.c_str());
				if (bClick)
				{
					Tame(*aimed, player);
				}
			}
			else if (aimed->state == AnimalState::FOLLOW)
			{
				s_World.prompt = LOCF(Animal_PromptLetGo, name.c_str());
				if (bClick)
				{
					aimed->state = AnimalState::IDLE;
					aimed->flags |= STAY;
					aimed->home = aimed->pos;
					aimed->goal = aimed->pos;
				}
			}
			else
			{
				s_World.prompt = LOCF(Animal_PromptLead, name.c_str());
				if (bClick)
				{
					aimed->state = AnimalState::FOLLOW;
					aimed->flags = (u8)(aimed->flags & ~STAY & ~AIRBORNE);
					StartStep(*aimed, aimed->pos, 0.2f);
				}
			}
		}

		//
		// Petting
		//

		const real PET_RANGE = 3.2f;

		void Pet(Animal& animal)
		{
			animal.petTimer = 3.0f;
			animal.calmTimer = 30.0f;
			animal.heartTimer = 0.0f;
			animal.stepTime = animal.stepDuration; // Reacts on the next frame
		}

		void SpawnHeart(const Animal& animal, u32 salt)
		{
			Rng rng(Hash(animal.id, salt + (u32)(s_World.time * 100.0f)));
			const SpeciesInfo& info = Info(animal.species);
			Heart heart;
			heart.pos = animal.pos + VEC3_UP * (info.neckHeight + 0.5f) + glm::vec3(rng.Range(-0.3f, 0.3f), 0.0f, rng.Range(-0.3f, 0.3f));
			heart.vel = glm::vec3(rng.Range(-0.15f, 0.15f), rng.Range(0.7f, 1.0f), rng.Range(-0.15f, 0.15f));
			heart.age = 0.0f;
			if (s_World.hearts.size() < 200)
			{
				s_World.hearts.push_back(heart);
			}
		}

		void UpdatePet(Player* player, real dt)
		{
			for (Animal& animal : s_World.animals)
			{
				if (animal.petTimer > 0.0f)
				{
					animal.petTimer -= dt;
					animal.heartTimer -= dt;
					if (animal.heartTimer <= 0.0f)
					{
						animal.heartTimer = 0.28f;
						SpawnHeart(animal, 1u);
					}
				}
				if (animal.calmTimer > 0.0f)
				{
					animal.calmTimer -= dt;
				}
			}
			for (i32 i = (i32)s_World.hearts.size() - 1; i >= 0; --i)
			{
				Heart& heart = s_World.hearts[i];
				heart.age += dt;
				heart.pos += heart.vel * dt;
				heart.vel.y *= glm::exp(-0.6f * dt);
				if (heart.age > 1.8f)
				{
					s_World.hearts[i] = s_World.hearts.back();
					s_World.hearts.pop_back();
				}
			}

			if (player == nullptr || s_World.bNaming || player->IsAnyInventoryShowing() || player->GetBuildMode().IsActive() || g_EngineInstance->IsSimulationPaused())
			{
				return;
			}
			BaseCamera* camera = g_CameraManager->CurrentCamera();
			Animal* aimed = FindAimedAnimal(camera->position, camera->forward, PET_RANGE + 1.5f);
			if (aimed == nullptr || (aimed->flags & AIRBORNE) != 0 || aimed->state == AnimalState::SLEEP ||
				glm::distance(glm::vec2(aimed->pos.x, aimed->pos.z), glm::vec2(player->GetTransform()->GetWorldPosition().x, player->GetTransform()->GetWorldPosition().z)) > PET_RANGE + Info(aimed->species).radius)
			{
				return;
			}
			if (s_World.prompt.empty())
			{
				s_World.prompt = LOCF(Animal_PromptPet, DisplayName(*aimed).c_str());
			}
			if (g_InputManager->GetKeyPressed(GameKeys::PET))
			{
				Pet(*aimed);
				for (u32 n = 0; n < 4; ++n)
				{
					SpawnHeart(*aimed, 100u + n);
				}
			}
		}

		//
		// Flocking & collision
		//

		void ShiftAnimal(Animal& animal, const glm::vec3& delta)
		{
			animal.pos += delta;
			animal.from += delta;
			animal.to += delta;
		}

		// Finds each animal's pack leader (the lowest id of the same species nearby), then pushes animals out of one
		// another & out of the player
		void UpdateFlockingAndCollision(Player* player)
		{
			std::vector<Animal*> list;
			const glm::vec3 playerFeet = PlayerFeet(player);
			for (Animal& animal : s_World.animals)
			{
				if ((animal.flags & AIRBORNE) == 0 && glm::distance2(animal.pos, playerFeet) < 120.0f * 120.0f)
				{
					list.push_back(&animal);
				}
			}
			if (list.size() > 500)
			{
				list.resize(500);
			}

			const real flockRadiusSq = 18.0f * 18.0f;
			for (Animal* a : list)
			{
				a->bFollower = false;
				const bool bFree = a->state != AnimalState::TIED && a->state != AnimalState::FOLLOW && a->state != AnimalState::FLEE && (a->flags & TAMED) == 0;
				if (!bFree)
				{
					continue;
				}
				Animal* leader = a;
				for (Animal* b : list)
				{
					if (b->species == a->species && b->id < leader->id && b->state != AnimalState::TIED && b->state != AnimalState::FOLLOW && (b->flags & TAMED) == 0 &&
						glm::distance2(glm::vec2(a->pos.x, a->pos.z), glm::vec2(b->pos.x, b->pos.z)) < flockRadiusSq)
					{
						leader = b;
					}
				}
				if (leader != a)
				{
					a->bFollower = true;
					a->leaderID = leader->id;
					a->leaderPos = leader->pos;
					a->leaderHeading = leader->heading;
					a->bLeaderMoving = leader->state == AnimalState::WANDER;
					a->flockSpacing = Info(a->species).radius * 1.8f + 0.6f;
				}
			}

			// Animals push each other apart (radii are about half the lasso radius)
			for (u32 i = 0; i < (u32)list.size(); ++i)
			{
				Animal& a = *list[i];
				const real ra = Info(a.species).radius * 0.5f;
				for (u32 j = i + 1; j < (u32)list.size(); ++j)
				{
					Animal& b = *list[j];
					glm::vec2 d(b.pos.x - a.pos.x, b.pos.z - a.pos.z);
					const real minDist = ra + Info(b.species).radius * 0.5f;
					const real distSq = glm::dot(d, d);
					if (distSq >= minDist * minDist || glm::abs(a.pos.y - b.pos.y) > 2.0f)
					{
						continue;
					}
					const real dist = glm::sqrt(distSq);
					d = dist > 1e-4f ? d / dist : glm::vec2(std::cos((real)a.id), std::sin((real)a.id));
					const real overlap = minDist - dist;
					const bool bAFixed = a.state == AnimalState::TIED;
					const bool bBFixed = b.state == AnimalState::TIED;
					const real aShare = bAFixed ? 0.0f : bBFixed ? 1.0f : 0.5f;
					const real bShare = bBFixed ? 0.0f : bAFixed ? 1.0f : 0.5f;
					ShiftAnimal(a, -glm::vec3(d.x, 0.0f, d.y) * overlap * aShare);
					ShiftAnimal(b, glm::vec3(d.x, 0.0f, d.y) * overlap * bShare);
				}
			}

			// The player: the animal gives way, & the player is held back by the larger ones
			RigidBody* playerBody = player->GetRigidBody();
			glm::vec3 playerPush = VEC3_ZERO;
			for (Animal* a : list)
			{
				const SpeciesInfo& info = Info(a->species);
				if (playerFeet.y - a->pos.y > info.neckHeight || a->pos.y - playerFeet.y > 2.0f)
				{
					continue;
				}
				const real minDist = 0.4f + info.radius * 0.5f;
				glm::vec2 d(a->pos.x - playerFeet.x, a->pos.z - playerFeet.z);
				const real distSq = glm::dot(d, d);
				if (distSq >= minDist * minDist)
				{
					continue;
				}
				const real dist = glm::sqrt(distSq);
				d = dist > 1e-4f ? d / dist : glm::vec2(1.0f, 0.0f);
				const real overlap = minDist - dist;
				const real playerShare = glm::clamp(info.radius * 0.5f, 0.15f, 0.7f);
				const bool bFixed = a->state == AnimalState::TIED;
				ShiftAnimal(*a, glm::vec3(d.x, 0.0f, d.y) * overlap * (bFixed ? 0.0f : 1.0f - playerShare));
				playerPush -= glm::vec3(d.x, 0.0f, d.y) * overlap * (bFixed ? 1.0f : playerShare);
			}
			if (playerBody != nullptr && glm::length2(playerPush) > 1e-8f)
			{
				playerBody->SetWorldPosition(player->GetTransform()->GetWorldPosition() + playerPush, false);
				btRigidBody* rb = playerBody->GetRigidBodyInternal();
				const glm::vec3 pushDir = glm::normalize(playerPush);
				const btVector3 v = rb->getLinearVelocity();
				const real into = -(v.x() * pushDir.x + v.z() * pushDir.z); // Velocity towards the animal
				if (into > 0.0f)
				{
					rb->setLinearVelocity(btVector3(v.x() + pushDir.x * into, v.y(), v.z() + pushDir.z * into));
				}
			}
		}

		//
		// Drawing
		//

		enum Colour : u32
		{
			WHITE, CREAM, BLACK, BROWN, DARK_BROWN, CHESTNUT, GREY, DARK_GREY, RED, ORANGE, PINK, RUST, GOLDEN, ROPE, GREEN,
			COLOUR_COUNT
		};

		const glm::vec4 s_Colours[COLOUR_COUNT] =
		{
			glm::vec4(0.92f, 0.91f, 0.88f, 1.0f), // White
			glm::vec4(0.86f, 0.79f, 0.62f, 1.0f), // Cream
			glm::vec4(0.07f, 0.07f, 0.08f, 1.0f), // Black
			glm::vec4(0.47f, 0.3f, 0.16f, 1.0f), // Brown
			glm::vec4(0.2f, 0.12f, 0.07f, 1.0f), // Dark brown
			glm::vec4(0.58f, 0.32f, 0.15f, 1.0f), // Chestnut
			glm::vec4(0.6f, 0.61f, 0.63f, 1.0f), // Grey
			glm::vec4(0.3f, 0.3f, 0.33f, 1.0f), // Dark grey
			glm::vec4(0.82f, 0.12f, 0.1f, 1.0f), // Red
			glm::vec4(0.95f, 0.6f, 0.14f, 1.0f), // Orange
			glm::vec4(0.9f, 0.6f, 0.6f, 1.0f), // Pink
			glm::vec4(0.68f, 0.33f, 0.13f, 1.0f), // Rust
			glm::vec4(0.8f, 0.52f, 0.2f, 1.0f), // Golden
			glm::vec4(0.62f, 0.5f, 0.32f, 1.0f), // Rope
			glm::vec4(0.08f, 0.28f, 0.2f, 1.0f), // Green (tail feathers)
		};

		// Builds an animal's boxes in world space
		struct Builder
		{
			AnimalRenderer* renderer;
			glm::vec3 base;
			glm::quat rot;

			void Box(u32 colour, const glm::vec3& centre, const glm::vec3& halfExtents, const glm::quat& localRot = QUAT_IDENTITY);
			// A box rotated about a pivot (local space), e.g. a swinging leg or a nodding head
			void PivotBox(u32 colour, const glm::vec3& pivot, const glm::quat& pivotRot, const glm::vec3& offset, const glm::vec3& halfExtents)
			{
				Box(colour, pivot + pivotRot * offset, halfExtents, pivotRot);
			}
		};

		void DrawQuadruped(Builder& b, const Animal& animal, bool bDetail, real legLength, const glm::vec3& bodyHalf, const glm::vec3& headHalf, u32 bodyColour, u32 headColour,
			u32 legColour, real legWidth, real headPitch, bool bAsleep, real sway)
		{
			const real bodyY = (bAsleep ? legLength * 0.15f : legLength) + bodyHalf.y;
			b.Box(bodyColour, glm::vec3(0.0f, bodyY, 0.0f), bodyHalf);
			// Head on the front of the body, pitched down when grazing/asleep
			const glm::vec3 neck(0.0f, bodyY + bodyHalf.y * 0.5f, bodyHalf.z * 0.9f);
			const glm::quat nod = glm::angleAxis(headPitch, VEC3_RIGHT) * glm::angleAxis(sway, VEC3_UP);
			b.PivotBox(headColour, neck, nod, glm::vec3(0.0f, headHalf.y * 0.6f, headHalf.z * 0.9f), headHalf);
			if (!bDetail)
			{
				return;
			}
			if (!bAsleep)
			{
				const real swing = std::sin(animal.walkPhase) * 0.5f;
				const glm::vec3 legHalf(legWidth, legLength * 0.5f, legWidth);
				for (u32 i = 0; i < 4; ++i)
				{
					const real side = (i & 1) ? 1.0f : -1.0f;
					const real end = (i & 2) ? 1.0f : -1.0f;
					const real angle = ((i == 0 || i == 3) ? swing : -swing);
					const glm::vec3 hip(side * (bodyHalf.x - legWidth), legLength, end * (bodyHalf.z - legWidth * 1.5f));
					b.PivotBox(legColour, hip, glm::angleAxis(angle, VEC3_RIGHT), glm::vec3(0.0f, -legLength * 0.5f, 0.0f), legHalf);
				}
			}
		}

		void DrawBird(Builder& b, const Animal& animal, bool bDetail, real size, u32 bodyColour, u32 headColour, u32 wingColour, u32 beakColour, u32 legColour, bool bAsleep,
			bool bComb, bool bTail, real pecking)
		{
			const bool bFlying = (animal.flags & AIRBORNE) != 0;
			const real legLength = bFlying ? 0.0f : (bAsleep ? 0.02f : size * 0.55f);
			const glm::vec3 bodyHalf = glm::vec3(0.45f, 0.42f, 0.65f) * size;
			const real bodyY = legLength + bodyHalf.y;
			b.Box(bodyColour, glm::vec3(0.0f, bodyY, 0.0f), bodyHalf, glm::angleAxis(bFlying ? 0.0f : -0.15f, VEC3_RIGHT));
			const glm::vec3 neck(0.0f, bodyY + bodyHalf.y * 0.7f, bodyHalf.z * 0.7f);
			const glm::quat nod = glm::angleAxis(bAsleep ? 0.9f : pecking, VEC3_RIGHT);
			const glm::vec3 headHalf = glm::vec3(0.27f, 0.3f, 0.3f) * size;
			const glm::vec3 headOffset(0.0f, bAsleep ? -headHalf.y : headHalf.y * 0.8f, headHalf.z * 0.5f);
			b.PivotBox(headColour, neck, nod, headOffset, headHalf);
			if (!bDetail)
			{
				return;
			}
			b.PivotBox(beakColour, neck, nod, headOffset + glm::vec3(0.0f, -0.05f * size, headHalf.z + 0.12f * size), glm::vec3(0.08f, 0.06f, 0.14f) * size);
			if (bComb)
			{
				b.PivotBox(RED, neck, nod, headOffset + glm::vec3(0.0f, headHalf.y + 0.08f * size, 0.0f), glm::vec3(0.04f, 0.1f, 0.2f) * size);
				b.PivotBox(RED, neck, nod, headOffset + glm::vec3(0.0f, -headHalf.y - 0.05f * size, headHalf.z), glm::vec3(0.04f, 0.08f, 0.05f) * size);
			}
			if (bTail)
			{
				b.Box(GREEN, glm::vec3(0.0f, bodyY + bodyHalf.y * 0.9f, -bodyHalf.z * 1.1f), glm::vec3(0.06f, 0.35f, 0.2f) * size, glm::angleAxis(-0.5f, VEC3_RIGHT));
			}
			else
			{
				b.Box(wingColour, glm::vec3(0.0f, bodyY + bodyHalf.y * 0.2f, -bodyHalf.z * 1.15f), glm::vec3(0.3f, 0.06f, 0.3f) * size);
			}
			// Wings: folded against the body, or flapping
			const real flap = bFlying ? std::sin(animal.walkPhase * 0.6f + s_World.time * 9.0f) * 0.8f : 0.0f;
			for (real side : { -1.0f, 1.0f })
			{
				if (bFlying)
				{
					const glm::vec3 shoulder(side * bodyHalf.x, bodyY + bodyHalf.y * 0.4f, 0.0f);
					b.PivotBox(wingColour, shoulder, glm::angleAxis(side * flap, VEC3_FORWARD), glm::vec3(side * 0.9f * size, 0.0f, 0.0f), glm::vec3(0.9f, 0.05f, 0.45f) * size);
				}
				else
				{
					b.Box(wingColour, glm::vec3(side * (bodyHalf.x + 0.02f * size), bodyY + 0.05f * size, -0.05f * size), glm::vec3(0.05f, bodyHalf.y * 0.7f, bodyHalf.z * 0.9f));
				}
			}
			if (legLength > 0.05f)
			{
				const real swing = std::sin(animal.walkPhase * 1.5f) * 0.5f;
				for (real side : { -1.0f, 1.0f })
				{
					const glm::vec3 hip(side * 0.18f * size, legLength, 0.0f);
					b.PivotBox(legColour, hip, glm::angleAxis(side * swing, VEC3_RIGHT), glm::vec3(0.0f, -legLength * 0.5f, 0.0f), glm::vec3(0.04f * size, legLength * 0.5f, 0.04f * size));
				}
			}
		}

		void DrawAnimal(AnimalRenderer* renderer, const Animal& animal, bool bDetail)
		{
			glm::vec3 drawPos = animal.pos;
			real drawHeading = animal.heading;
			if (animal.petTimer > 0.0f)
			{
				// Happy: hops & wiggles
				const real strength = glm::min(animal.petTimer, 1.0f);
				drawPos.y += glm::abs(std::sin(s_World.time * 9.0f + (real)animal.id)) * 0.18f * strength;
				drawHeading += std::sin(s_World.time * 14.0f) * 0.14f * strength;
			}
			Builder b = { renderer, drawPos, glm::angleAxis(drawHeading, VEC3_UP) };
			const bool bAsleep = animal.state == AnimalState::SLEEP;
			const bool bGrazing = animal.state == AnimalState::IDLE || (animal.state == AnimalState::TIED && animal.from == animal.to);
			// Grazing animals lower their heads now & then
			const real graze = bGrazing ? glm::max(std::sin(s_World.time * 0.7f + (real)animal.id), 0.0f) : 0.0f;
			const real sway = std::sin(s_World.time * 0.9f + (real)animal.id * 1.7f) * 0.25f * (bAsleep ? 0.0f : 1.0f);
			switch (animal.species)
			{
			case AnimalSpecies::COW:
			{
				const u32 main = animal.variant == 0 ? WHITE : animal.variant == 1 ? BROWN : BLACK;
				DrawQuadruped(b, animal, bDetail, 0.65f, glm::vec3(0.38f, 0.33f, 0.78f), glm::vec3(0.2f, 0.2f, 0.27f), main, main, main, 0.08f, bAsleep ? 0.5f : graze * 1.1f, bAsleep, sway);
				if (bDetail)
				{
					const real bodyY = (bAsleep ? 0.1f : 0.65f) + 0.33f;
					// Patches, udder & tail
					b.Box(main == BLACK ? WHITE : BLACK, glm::vec3(0.0f, bodyY + 0.08f, 0.2f), glm::vec3(0.385f, 0.2f, 0.25f));
					b.Box(PINK, glm::vec3(0.0f, bodyY - 0.36f, -0.35f), glm::vec3(0.12f, 0.06f, 0.12f));
					b.Box(main, glm::vec3(0.0f, bodyY - 0.05f, -0.8f), glm::vec3(0.03f, 0.3f, 0.03f));
				}
			} break;
			case AnimalSpecies::HORSE:
			{
				const u32 main = animal.variant == 0 ? CHESTNUT : animal.variant == 1 ? DARK_BROWN : WHITE;
				DrawQuadruped(b, animal, bDetail, 0.95f, glm::vec3(0.3f, 0.32f, 0.85f), glm::vec3(0.14f, 0.18f, 0.36f), main, main, main, 0.07f,
					bAsleep ? 0.3f : (graze * 1.4f - 0.55f), bAsleep, sway);
				if (bDetail)
				{
					const real bodyY = (bAsleep ? 0.14f : 0.95f) + 0.32f;
					const u32 hair = main == WHITE ? GREY : BLACK;
					// Mane & tail
					b.Box(hair, glm::vec3(0.0f, bodyY + 0.45f, 0.75f), glm::vec3(0.05f, 0.25f, 0.18f), glm::angleAxis(-0.5f, VEC3_RIGHT));
					b.Box(hair, glm::vec3(0.0f, bodyY - 0.05f, -0.92f), glm::vec3(0.06f, 0.38f, 0.06f), glm::angleAxis(0.3f, VEC3_RIGHT));
				}
			} break;
			case AnimalSpecies::GOAT:
			{
				const u32 main = animal.variant == 0 ? WHITE : animal.variant == 1 ? CREAM : GREY;
				DrawQuadruped(b, animal, bDetail, 0.45f, glm::vec3(0.2f, 0.22f, 0.45f), glm::vec3(0.12f, 0.14f, 0.2f), main, main, DARK_GREY, 0.05f,
					bAsleep ? 0.5f : graze * 1.2f - 0.3f, bAsleep, sway);
				if (bDetail)
				{
					const real bodyY = (bAsleep ? 0.07f : 0.45f) + 0.22f;
					// Horns & beard
					for (real side : { -1.0f, 1.0f })
					{
						b.Box(DARK_GREY, glm::vec3(side * 0.07f, bodyY + 0.42f, 0.42f), glm::vec3(0.025f, 0.12f, 0.025f), glm::angleAxis(-0.6f, VEC3_RIGHT));
					}
					b.Box(main, glm::vec3(0.0f, bodyY + 0.15f, -0.48f), glm::vec3(0.04f, 0.06f, 0.04f));
				}
			} break;
			case AnimalSpecies::SQUIRREL:
			{
				const u32 main = animal.variant == 2 ? DARK_GREY : RUST;
				const real hop = (animal.from != animal.to && !bAsleep) ? glm::abs(std::sin(animal.walkPhase * 0.8f)) * 0.12f : 0.0f;
				b.base += VEC3_UP * hop;
				DrawQuadruped(b, animal, bDetail, 0.08f, glm::vec3(0.07f, 0.07f, 0.13f), glm::vec3(0.06f, 0.06f, 0.07f), main, main, main, 0.025f,
					bAsleep ? 0.4f : -0.4f, bAsleep, sway);
				// Big bushy tail curled up over its back
				b.Box(main, glm::vec3(0.0f, bAsleep ? 0.12f : 0.3f, -0.18f), glm::vec3(0.06f, 0.16f, 0.06f), glm::angleAxis(0.3f, VEC3_RIGHT));
				if (bDetail)
				{
					b.Box(main == RUST ? CREAM : GREY, glm::vec3(0.0f, bAsleep ? 0.28f : 0.46f, -0.1f), glm::vec3(0.055f, 0.06f, 0.08f));
				}
			} break;
			case AnimalSpecies::CHICKEN:
			{
				const u32 main = animal.variant == 0 ? WHITE : animal.variant == 1 ? GOLDEN : BROWN;
				const real peck = bGrazing ? glm::max(std::sin(s_World.time * 4.0f + (real)animal.id), 0.0f) * 1.2f : 0.0f;
				DrawBird(b, animal, bDetail, 0.38f, main, main, main, ORANGE, ORANGE, bAsleep, true, false, peck);
			} break;
			case AnimalSpecies::ROOSTER:
			{
				const real peck = bGrazing ? glm::max(std::sin(s_World.time * 3.0f + (real)animal.id), 0.0f) * 0.9f : 0.0f;
				DrawBird(b, animal, bDetail, 0.48f, RUST, GOLDEN, BROWN, ORANGE, ORANGE, bAsleep, true, true, peck);
			} break;
			case AnimalSpecies::CROW:
				DrawBird(b, animal, bDetail, 0.32f, BLACK, BLACK, BLACK, DARK_GREY, DARK_GREY, bAsleep, false, false, bGrazing ? 0.4f : 0.0f);
				break;
			case AnimalSpecies::SEAGULL:
				DrawBird(b, animal, bDetail, 0.38f, WHITE, WHITE, GREY, ORANGE, ORANGE, bAsleep, false, false, 0.0f);
				break;
			default:
				break;
			}
		}

		// Rope between two points (sagging a little)
		void DrawRope(AnimalRenderer* renderer, const glm::vec3& a, const glm::vec3& b, real slack)
		{
			Builder builder = { renderer, VEC3_ZERO, QUAT_IDENTITY };
			const u32 segments = 10;
			glm::vec3 prev = a;
			for (u32 i = 1; i <= segments; ++i)
			{
				const real t = (real)i / (real)segments;
				glm::vec3 p = glm::mix(a, b, t);
				p.y -= std::sin(t * PI) * slack;
				const glm::vec3 delta = p - prev;
				const real length = glm::length(delta);
				if (length > 1e-3f)
				{
					builder.Box(ROPE, (p + prev) * 0.5f, glm::vec3(0.02f, 0.02f, length * 0.5f + 0.01f), glm::rotation(VEC3_FORWARD, delta / length));
				}
				prev = p;
			}
		}

		// FLEX_TEST_ANIMALS=<any>: once playing, lines up one of each animal in front of the player (for screenshots),
		// lassoes the cow, names it, ties it to a new fence post, & checks the animals survive a save/load round trip
		void UpdateAnimalTest(Player* player, TerrainGenerator* terrain)
		{
			static i32 s_State = 0;
			static real s_Timer = 0.0f;
			if (getenv("FLEX_TEST_ANIMALS") == nullptr || player == nullptr || s_State < 0)
			{
				return;
			}
			s_Timer += g_DeltaTime;
			BaseCamera* camera = g_CameraManager->CurrentCamera();
			const glm::vec3 forward = glm::normalize(glm::vec3(camera->forward.x, 0.0f, camera->forward.z) + glm::vec3(1e-4f, 0.0f, 0.0f));
			const glm::vec3 right = glm::normalize(glm::cross(forward, VEC3_UP));
			const glm::vec3 feet = PlayerFeet(player);
			static u32 s_CowID = 0;
			static glm::vec3 s_PostPos;
			if (s_State == 0 && s_Timer > 3.0f)
			{
				// Daytime, so they're awake
				g_SceneManager->CurrentScene()->SetTimeOfDay(0.05f);
				for (u32 i = 0; i < (u32)AnimalSpecies::_COUNT; ++i)
				{
					glm::vec3 pos = feet + forward * 9.0f + right * (((real)i - 3.5f) * 2.2f);
					pos.y = GroundHeight(terrain, pos, true);
					const u32 id = SpawnAnimal((AnimalSpecies)i, pos, 0xFFFFFFFF, 1234u + i);
					Animal* animal = FindAnimal(id);
					animal->flags = (u8)((animal->flags & ~AIRBORNE) | TAMED | STAY); // (Stay put for the screenshot)
					animal->state = AnimalState::IDLE;
					animal->pos = animal->from = animal->to = animal->home = animal->goal = pos;
					animal->heading = std::atan2(-forward.x, -forward.z);
					animal->stepDuration = 30.0f;
					if ((AnimalSpecies)i == AnimalSpecies::COW)
					{
						s_CowID = id;
						animal->flags = 0;
					}
				}
				s_State = 1;
				s_Timer = 0.0f;
			}
			else if (s_State == 1 && s_Timer > 2.0f)
			{
				const std::string directory = RelativePathToAbsolute(SCREENSHOT_DIRECTORY);
				Platform::CreateDirectoryRecursive(directory);
				g_Renderer->RequestScreenshot(directory + "animals_lineup.png");
				s_State = 2;
				s_Timer = 0.0f;
			}
			else if (s_State == 2 && s_Timer > 1.0f)
			{
				Animal* cow = FindAnimal(s_CowID);
				bool bPass = cow != nullptr;
				if (cow != nullptr)
				{
					Tame(*cow, player);
					bPass = bPass && s_World.bNaming && cow->state == AnimalState::FOLLOW;
					s_World.nameText = "Bessie";
					cow->name = s_World.nameText;
					s_World.bNaming = false;
					// A fence post next to the player
					s_PostPos = feet + forward * 4.0f - right * 3.0f;
					s_PostPos.y = GroundHeight(terrain, s_PostPos, true);
					BaseScene* scene = g_SceneManager->CurrentScene();
					BuildPiece* post = new BuildPiece(scene->GetUniqueObjectName("FencePost_", 3));
					post->Setup(BuildPieceType::FENCE_POST, BuildMaterial::WOOD, 0.0f);
					post->GetTransform()->SetWorldPosition(s_PostPos);
					scene->AddRootObject(post);
					TieTo(*cow, s_PostPos, player);
					bPass = bPass && cow->state == AnimalState::TIED;
				}
				Print("Animal test: tame & tie %s\n", bPass ? "PASS" : "FAIL");
				s_State = 3;
				s_Timer = 0.0f;
			}
			else if (s_State == 3 && s_Timer > 4.0f)
			{
				const std::string directory = RelativePathToAbsolute(SCREENSHOT_DIRECTORY);
				g_Renderer->RequestScreenshot(directory + "animals_tied.png");
				Animal* cow = FindAnimal(s_CowID);
				const bool bInRange = cow != nullptr && glm::distance(glm::vec2(cow->pos.x, cow->pos.z), glm::vec2(s_PostPos.x, s_PostPos.z)) <= TIE_ROPE_LENGTH + 0.6f;
				// Round trip through the save format
				const u32 simulatedBefore = s_World.simulatedCount;
				JSONObject worldObj = {};
				Animals::Serialize(worldObj);
				const u32 countBefore = (u32)s_World.animals.size();
				Animals::Load(worldObj);
				Animal* loaded = FindAnimal(s_CowID);
				const bool bLoaded = (u32)s_World.animals.size() == countBefore && loaded != nullptr && loaded->name == "Bessie" &&
					loaded->state == AnimalState::TIED && (loaded->flags & TAMED) != 0;
				Print("Animal test: tied cow stays by its post %s, save/load %s (%u animals, %u simulated, %u cells)\n", bInRange ? "PASS" : "FAIL",
					bLoaded ? "PASS" : "FAIL", countBefore, simulatedBefore, (u32)s_World.cells.size());
				// Then night falls: they should all be asleep soon after
				g_SceneManager->CurrentScene()->SetTimeOfDay(0.5f);
				s_State = 4;
				s_Timer = 0.0f;
			}
			else if (s_State == 4 && s_Timer > 12.0f)
			{
				u32 asleep = 0;
				u32 total = 0;
				for (const Animal& animal : s_World.animals)
				{
					if (glm::distance(animal.pos, feet) < 60.0f)
					{
						++total;
						asleep += animal.state == AnimalState::SLEEP ? 1 : 0;
					}
				}
				Print("Animal test: asleep at midnight %s (%u of %u nearby)\n", asleep * 10 >= total * 8 ? "PASS" : "FAIL", asleep, total);
				const std::string directory = RelativePathToAbsolute(SCREENSHOT_DIRECTORY);
				g_Renderer->RequestScreenshot(directory + "animals_night.png");
				s_State = -1;
			}
		}
	} // anonymous namespace

	const char* AnimalSpeciesName(AnimalSpecies species)
	{
		return (u32)species < (u32)AnimalSpecies::_COUNT ? s_Species[(u32)species].name : "unknown";
	}

	const char* AnimalSpeciesDisplayName(AnimalSpecies species)
	{
		static const LocID ids[] = { LOC_ID(Animal_Goat), LOC_ID(Animal_Cow), LOC_ID(Animal_Chicken), LOC_ID(Animal_Crow), LOC_ID(Animal_Seagull),
			LOC_ID(Animal_Rooster), LOC_ID(Animal_Squirrel), LOC_ID(Animal_Horse) };
		static_assert(ARRAY_LENGTH(ids) == (u32)AnimalSpecies::_COUNT, "ids must match AnimalSpecies");
		return (u32)species < (u32)AnimalSpecies::_COUNT ? Loc::Get(ids[(u32)species]) : "";
	}

	//
	// AnimalRenderer
	//

	// One dynamic mesh per colour, rebuilt every frame (vertices are given relative to the renderer's origin)
	struct AnimalRenderer::ColourMesh
	{
		GameObject* object = nullptr;
		VertexBufferDataCreateInfo vertexData;
		std::vector<u32> indices;
	};

	AnimalRenderer::AnimalRenderer() :
		GameObject("Animals", BaseObjectSID)
	{
		SetSerializable(false);
		SetVisibleInSceneExplorer(false);
	}

	AnimalRenderer* AnimalRenderer::Get()
	{
		if (s_Instance == nullptr && g_SceneManager->HasSceneLoaded())
		{
			s_Instance = new AnimalRenderer();
			g_SceneManager->CurrentScene()->AddRootObject(s_Instance);
		}
		return s_Instance;
	}

	void AnimalRenderer::Initialize()
	{
		for (u32 i = 0; i < COLOUR_COUNT; ++i)
		{
			const std::string name = "Animal colour " + std::to_string(i);
			MaterialID materialID = g_Renderer->GetMaterialID(name);
			if (materialID == InvalidMaterialID)
			{
				MaterialCreateInfo createInfo = {};
				createInfo.name = name;
				createInfo.shaderName = "pbr";
				// Colours are given in sRGB, materials take linear values
				createInfo.constAlbedo = glm::vec4(glm::pow(glm::vec3(s_Colours[i]), glm::vec3(2.2f)), 1.0f);
				createInfo.constRoughness = i == ROPE ? 0.95f : 0.85f;
				createInfo.constMetallic = 0.0f;
				createInfo.bDynamic = true;
				createInfo.bSerializable = false;
				materialID = g_Renderer->InitializeMaterial(&createInfo);
			}

			ColourMesh* colourMesh = new ColourMesh();
			colourMesh->object = new GameObject(name, BaseObjectSID);
			colourMesh->object->SetSerializable(false);
			colourMesh->object->SetVisibleInSceneExplorer(false);
			AddChild(colourMesh->object);
			colourMesh->vertexData.attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(materialID)->shaderID)->vertexAttributes;
			// Buffers can't start empty
			AppendBoxToVertexData(VEC3_ZERO, glm::vec3(0.01f), QUAT_IDENTITY, colourMesh->vertexData, colourMesh->indices);

			RenderObjectCreateInfo renderObjectCreateInfo = {};
			renderObjectCreateInfo.bCastsShadow = true;
			Mesh* mesh = colourMesh->object->SetMesh(new Mesh(colourMesh->object));
			Mesh::CreateInfo meshCreateInfo = {};
			meshCreateInfo.bDynamic = true;
			meshCreateInfo.vertexBufferCreateInfo = &colourMesh->vertexData;
			meshCreateInfo.indices = colourMesh->indices;
			meshCreateInfo.materialIDs = { materialID };
			meshCreateInfo.initialMaxVertexCount = 8192;
			meshCreateInfo.optionalRenderObjectCreateInfo = &renderObjectCreateInfo;
			mesh->LoadFromMemory(meshCreateInfo);
			colourMesh->object->SetVisible(false);
			m_Meshes.push_back(colourMesh);
		}

		GameObject::Initialize();
	}

	void AnimalRenderer::Destroy(bool bDetachFromParent /* = true */)
	{
		if (s_Instance == this)
		{
			s_Instance = nullptr;
		}
		for (ColourMesh* colourMesh : m_Meshes)
		{
			delete colourMesh;
		}
		m_Meshes.clear();
		GameObject::Destroy(bDetachFromParent);
	}

	AnimalRenderer::ColourMesh* AnimalRenderer::GetMesh(u32 colour)
	{
		return colour < (u32)m_Meshes.size() ? m_Meshes[colour] : nullptr;
	}

	void AnimalRenderer::BeginFrame(const glm::vec3& origin)
	{
		// Vertices are made relative to a point near the camera (keeping their precision far from the world origin)
		m_Transform.SetWorldPosition(origin);
		for (ColourMesh* colourMesh : m_Meshes)
		{
			VertexBufferDataCreateInfo& data = colourMesh->vertexData;
			data.positions_3D.clear();
			data.texCoords_UV.clear();
			data.colours_R32G32B32A32.clear();
			data.normals.clear();
			data.tangents.clear();
			colourMesh->indices.clear();
		}
	}

	void AnimalRenderer::EndFrame()
	{
		for (ColourMesh* colourMesh : m_Meshes)
		{
			const bool bVisible = !colourMesh->indices.empty();
			colourMesh->object->SetVisible(bVisible);
			if (bVisible)
			{
				colourMesh->object->GetMesh()->GetSubMesh(0)->UpdateDynamicVertexData(colourMesh->vertexData, colourMesh->indices);
			}
		}
	}

	namespace
	{
		void Builder::Box(u32 colour, const glm::vec3& centre, const glm::vec3& halfExtents, const glm::quat& localRot)
		{
			AnimalRenderer::ColourMesh* colourMesh = renderer->GetMesh(colour);
			if (colourMesh == nullptr)
			{
				return;
			}
			// (Relative to the renderer's origin)
			const glm::vec3 origin = renderer->GetTransform()->GetWorldPosition();
			AppendBoxToVertexData(base - origin + rot * centre, halfExtents, rot * localRot, colourMesh->vertexData, colourMesh->indices);
		}
	} // anonymous namespace

	//
	// Animals
	//

	namespace Animals
	{
		void DrawHearts(AnimalRenderer* renderer, const glm::vec3& cameraPos)
		{
			for (const Heart& heart : s_World.hearts)
			{
				const glm::vec3 toCam = cameraPos - heart.pos;
				Builder b = { renderer, heart.pos, glm::angleAxis(std::atan2(toCam.x, toCam.z), VEC3_UP) };
				const real size = 0.1f * (1.0f - glm::smoothstep(1.2f, 1.8f, heart.age)) * glm::min(heart.age * 6.0f + 0.3f, 1.0f);
				const glm::quat diamond = glm::angleAxis(PI * 0.25f, VEC3_FORWARD);
				b.Box(RED, VEC3_ZERO, glm::vec3(size, size, size * 0.4f), diamond);
				b.Box(RED, glm::vec3(-size * 0.7f, size * 0.7f, 0.0f), glm::vec3(size * 0.75f, size * 0.75f, size * 0.4f), diamond);
				b.Box(RED, glm::vec3(size * 0.7f, size * 0.7f, 0.0f), glm::vec3(size * 0.75f, size * 0.75f, size * 0.4f), diamond);
			}
		}

		void Update()
		{
			PROFILE_AUTO("Animals::Update");

			TerrainGenerator* terrain = TerrainGenerator::GetActive();
			Player* player = GetPlayer();
			if (terrain == nullptr || player == nullptr || terrain->IsWorldChangePending() || terrain->GetStreamer() == nullptr)
			{
				return;
			}
			AnimalRenderer* renderer = AnimalRenderer::Get();
			if (renderer == nullptr)
			{
				return;
			}

			const real dt = glm::min((real)g_DeltaTime, 0.25f);
			s_World.time += dt;
			const glm::vec3 playerFeet = PlayerFeet(player);
			const real sunHeight = SunHeight();

			// Spawn the cells around the player (a few each pass, so it's spread over frames)
			s_World.spawnTimer -= dt;
			if (s_World.spawnTimer <= 0.0f)
			{
				s_World.spawnTimer = 0.25f;
				const i32 pcx = (i32)glm::floor(playerFeet.x / CELL_SIZE);
				const i32 pcz = (i32)glm::floor(playerFeet.z / CELL_SIZE);
				const i32 cellRadius = (i32)glm::ceil(SIM_RADIUS / CELL_SIZE);
				u32 spawned = 0;
				// Nearest rings first
				for (i32 ring = 0; ring <= cellRadius && spawned < 6; ++ring)
				{
					for (i32 dz = -ring; dz <= ring && spawned < 6; ++dz)
					{
						for (i32 dx = -ring; dx <= ring && spawned < 6; ++dx)
						{
							if (glm::max(glm::abs(dx), glm::abs(dz)) != ring)
							{
								continue;
							}
							const u32 key = CellKey(pcx + dx, pcz + dz);
							if (s_World.cells.count(key) != 0)
							{
								continue;
							}
							const glm::vec2 centre = CellCentre(key);
							if (glm::distance(centre, glm::vec2(playerFeet.x, playerFeet.z)) > SIM_RADIUS)
							{
								continue;
							}
							SpawnCell(terrain, key);
							++spawned;
						}
					}
				}
			}

			// Forget wild animals far away (their cells are spawned again from the seed if the player comes back)
			s_World.forgetTimer -= dt;
			if (s_World.forgetTimer <= 0.0f)
			{
				s_World.forgetTimer = 3.0f;
				std::unordered_set<u32> forgotten;
				for (auto iter = s_World.cells.begin(); iter != s_World.cells.end();)
				{
					if (glm::distance(CellCentre(*iter), glm::vec2(playerFeet.x, playerFeet.z)) > FORGET_RADIUS)
					{
						forgotten.insert(*iter);
						iter = s_World.cells.erase(iter);
					}
					else
					{
						++iter;
					}
				}
				if (!forgotten.empty())
				{
					s_World.animals.erase(std::remove_if(s_World.animals.begin(), s_World.animals.end(), [&](const Animal& animal)
					{
						return (animal.flags & TAMED) == 0 && forgotten.count(animal.homeCell) != 0;
					}), s_World.animals.end());
				}
			}

			// Animals tied to posts which have been removed are let go
			static real s_PostCheckTimer = 0.0f;
			s_PostCheckTimer -= dt;
			const bool bCheckPosts = s_PostCheckTimer <= 0.0f;
			if (bCheckPosts)
			{
				s_PostCheckTimer = 1.0f;
			}

			UpdateLasso(player);
			UpdatePet(player, dt);
			UpdateAnimalTest(player, terrain);

			// Simulate: near animals every frame, the rest of those in range round-robin
			const real nearSq = NEAR_RADIUS * NEAR_RADIUS;
			const real simSq = SIM_RADIUS * SIM_RADIUS;
			std::vector<u32> farIndices;
			farIndices.reserve(s_World.animals.size());
			u32 simulated = 0;
			if (dt > 0.0f)
			{
				for (u32 i = 0; i < (u32)s_World.animals.size(); ++i)
				{
					Animal& animal = s_World.animals[i];
					if (bCheckPosts && animal.state == AnimalState::TIED && glm::distance2(animal.pos, playerFeet) < nearSq && !FencePostExistsAt(animal.home))
					{
						animal.state = AnimalState::IDLE;
						animal.flags |= STAY;
						animal.home = animal.pos;
					}
					const real distSq = glm::distance2(glm::vec2(animal.pos.x, animal.pos.z), glm::vec2(playerFeet.x, playerFeet.z));
					if (distSq < nearSq || animal.state == AnimalState::FOLLOW)
					{
						Advance(terrain, animal, s_World.time - animal.lastUpdate, player, true, sunHeight);
						animal.lastUpdate = s_World.time;
						++simulated;
					}
					else if (distSq < simSq)
					{
						farIndices.push_back(i);
						++simulated;
					}
					// (Animals further away are dormant: kept as they are until the player comes back)
				}
				const u32 farBudget = glm::min((u32)farIndices.size(), 48u);
				for (u32 n = 0; n < farBudget; ++n)
				{
					s_World.farCursor = (s_World.farCursor + 1) % (u32)farIndices.size();
					Animal& animal = s_World.animals[farIndices[s_World.farCursor]];
					Advance(terrain, animal, s_World.time - animal.lastUpdate, player, false, sunHeight);
					animal.lastUpdate = s_World.time;
				}
			}
			s_World.simulatedCount = simulated;
			if (dt > 0.0f)
			{
				UpdateFlockingAndCollision(player);
			}

			// Draw those near the camera
			BaseCamera* camera = g_CameraManager->CurrentCamera();
			const glm::vec3 cameraPos = camera->position;
			renderer->BeginFrame(glm::floor(cameraPos / 64.0f) * 64.0f);
			const real drawSq = DRAW_RADIUS * DRAW_RADIUS;
			const real detailSq = DETAIL_RADIUS * DETAIL_RADIUS;
			for (const Animal& animal : s_World.animals)
			{
				const glm::vec3 toAnimal = animal.pos - cameraPos;
				const real distSq = glm::length2(toAnimal);
				if (distSq > drawSq)
				{
					continue;
				}
				// Skip those well behind the camera
				if (distSq > 100.0f && glm::dot(toAnimal, camera->forward) < -0.3f * glm::sqrt(distSq))
				{
					continue;
				}
				DrawAnimal(renderer, animal, distSq < detailSq);

				const glm::vec3 neck = animal.pos + VEC3_UP * Info(animal.species).neckHeight + HeadingDir(animal.heading) * (Info(animal.species).radius * 0.55f);
				if (animal.state == AnimalState::FOLLOW)
				{
					const glm::vec3 hand = cameraPos + camera->right * 0.35f - camera->up * 0.45f + camera->forward * 0.6f;
					DrawRope(renderer, hand, neck, glm::clamp(0.6f - glm::distance(hand, neck) * 0.05f, 0.05f, 0.6f));
				}
				else if (animal.state == AnimalState::TIED || (animal.state == AnimalState::SLEEP && (animal.flags & TAMED) != 0 && FencePostExistsAt(animal.home)))
				{
					DrawRope(renderer, animal.home + VEC3_UP * 1.0f, neck, 0.3f);
				}
			}
			DrawHearts(renderer, cameraPos);
			renderer->EndFrame();
		}

		void DrawHUD(GameUI& ui)
		{
			const real s = ui.Scale();
			if (!s_World.prompt.empty() && !s_World.bNaming)
			{
				ui.Text(s_World.prompt, ui.Width() * 0.5f, ui.Height() * 0.5f + 48.0f * s, UITheme::TEXT_BODY * s, UITheme::TEXT_PRIMARY, TextAlign::CENTER);
			}
			static bool s_bNameFieldInit = false;
			if (!s_World.bNaming)
			{
				s_bNameFieldInit = false;
				return;
			}
			Animal* animal = FindAnimal(s_World.namingID);
			if (animal == nullptr)
			{
				s_World.bNaming = false;
				return;
			}

			const real w = 520.0f * s;
			const real h = 250.0f * s;
			const UIRect panel((ui.Width() - w) * 0.5f, (ui.Height() - h) * 0.5f, w, h);
			ui.Shadow(panel, 24.0f * s, UITheme::RADIUS_L * s);
			ui.Panel(panel);
			const real left = panel.x + 28.0f * s;
			ui.Text(LOCF(Animal_NameTitle, AnimalSpeciesDisplayName(animal->species)), left, panel.y + 44.0f * s, UITheme::TEXT_H2 * s, UITheme::TEXT_PRIMARY, TextAlign::LEFT, true);

			// A text field: click into/out of it (it starts focused)
			const u32 maxLength = 20;
			if (!s_bNameFieldInit)
			{
				ui.Focus("animal_name_field");
				s_bNameFieldInit = true;
			}
			const UIRect field(left, panel.y + 86.0f * s, w - 56.0f * s, UITheme::ROW_HEIGHT * s);
			const bool bFieldSubmitted = ui.TextField("animal_name_field", field, &s_World.nameText, maxLength, LOC(Animal_NamePlaceholder));
			s_World.nameText.erase(std::remove_if(s_World.nameText.begin(), s_World.nameText.end(), [](char c) { return c < 32 || c == ',' || c == ';' || c == '|'; }), s_World.nameText.end());

			const real buttonWidth = (w - 56.0f * s - 12.0f * s) * 0.5f;
			const real buttonY = panel.y + h - (UITheme::BUTTON_HEIGHT + 24.0f) * s;
			const bool bConfirm = ui.Button("animal_name_ok", UIRect(left, buttonY, buttonWidth, UITheme::BUTTON_HEIGHT * s), LOC(Animal_NameConfirm), true, !s_World.nameText.empty()) ||
				(bFieldSubmitted && !s_World.nameText.empty());
			const bool bSkip = ui.Button("animal_name_skip", UIRect(left + buttonWidth + 12.0f * s, buttonY, buttonWidth, UITheme::BUTTON_HEIGHT * s), LOC(Animal_NameSkip));
			if (bConfirm || bSkip)
			{
				if (bConfirm)
				{
					animal->name = s_World.nameText;
					if (Player* player = GetPlayer())
					{
						player->ShowNotification(LOCF(Animal_Named, animal->name.c_str()));
					}
				}
				s_World.bNaming = false;
			}
		}

		bool IsNamingDialogOpen()
		{
			return s_World.bNaming;
		}

		void Serialize(JSONObject& worldObj)
		{
			// Each animal as "species,state,flags,variant,x,y,z,heading,homeX,homeY,homeZ,cell,id,name;" (names can't
			// contain , or ;)
			std::string animals;
			animals.reserve(s_World.animals.size() * 72);
			char buffer[256];
			for (const Animal& animal : s_World.animals)
			{
				// (Mid-flight birds are saved where they'd land, & followers where they are)
				const AnimalState state = animal.state == AnimalState::FLEE ? AnimalState::IDLE : animal.state;
				snprintf(buffer, ARRAY_LENGTH(buffer), "%u,%u,%u,%u,%.2f,%.2f,%.2f,%.3f,%.2f,%.2f,%.2f,%08X,%u,", (u32)animal.species, (u32)state, (u32)animal.flags,
					(u32)animal.variant, animal.pos.x, animal.pos.y, animal.pos.z, animal.heading, animal.home.x, animal.home.y, animal.home.z, animal.homeCell, animal.id);
				animals += buffer;
				animals += animal.name;
				animals += ';';
			}
			worldObj.fields.emplace_back("animals", JSONValue(animals));

			std::string cells;
			cells.reserve(s_World.cells.size() * 8);
			for (u32 cell : s_World.cells)
			{
				snprintf(buffer, ARRAY_LENGTH(buffer), "%08X", cell);
				cells += buffer;
			}
			worldObj.fields.emplace_back("animal cells", JSONValue(cells));
		}

		void Load(const JSONObject& worldObj)
		{
			Clear();
			std::string animals;
			if (worldObj.TryGetString("animals", animals))
			{
				size_t start = 0;
				while (start < animals.size())
				{
					size_t end = animals.find(';', start);
					if (end == std::string::npos)
					{
						end = animals.size();
					}
					const std::string record = animals.substr(start, end - start);
					start = end + 1;

					u32 species, state, flags, variant, cell, id;
					real x, y, z, heading, hx, hy, hz;
					i32 consumed = 0;
					if (sscanf(record.c_str(), "%u,%u,%u,%u,%f,%f,%f,%f,%f,%f,%f,%X,%u,%n", &species, &state, &flags, &variant, &x, &y, &z, &heading, &hx, &hy, &hz, &cell, &id, &consumed) != 13 ||
						species >= (u32)AnimalSpecies::_COUNT || state >= (u32)AnimalState::_COUNT)
					{
						continue;
					}
					Animal animal = {};
					animal.id = id;
					animal.species = (AnimalSpecies)species;
					animal.state = (AnimalState)state;
					animal.flags = (u8)flags;
					animal.variant = (u8)variant;
					animal.pos = animal.from = animal.to = animal.goal = glm::vec3(x, y, z);
					animal.heading = heading;
					animal.home = glm::vec3(hx, hy, hz);
					animal.homeCell = cell;
					animal.stepDuration = 0.5f;
					animal.lastUpdate = s_World.time;
					animal.name = record.substr((size_t)consumed);
					Rng rng(id);
					animal.sleepBias = rng.Range(-0.04f, 0.04f);
					s_World.animals.push_back(animal);
					s_World.nextID = glm::max(s_World.nextID, id + 1);
				}
			}
			std::string cells;
			if (worldObj.TryGetString("animal cells", cells))
			{
				for (size_t i = 0; i + 8 <= cells.size(); i += 8)
				{
					s_World.cells.insert((u32)strtoul(cells.substr(i, 8).c_str(), nullptr, 16));
				}
			}
			u32 tamed = 0;
			for (const Animal& animal : s_World.animals)
			{
				tamed += (animal.flags & TAMED) != 0 ? 1 : 0;
			}
			if (!s_World.animals.empty())
			{
				Print("Loaded %u animals (%u tamed) & %u animal cells\n", (u32)s_World.animals.size(), tamed, (u32)s_World.cells.size());
			}
		}

		void Clear()
		{
			s_World.animals.clear();
			s_World.cells.clear();
			s_World.nextID = 1;
			s_World.bNaming = false;
			s_World.prompt.clear();
			s_World.spawnTimer = 0.0f;
			s_World.simulatedCount = 0;
		}

		u32 GetAnimalCount()
		{
			return (u32)s_World.animals.size();
		}

		u32 GetSimulatedCount()
		{
			return s_World.simulatedCount;
		}
	} // namespace Animals
} // namespace flex
