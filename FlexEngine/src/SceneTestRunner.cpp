#include "stdafx.hpp"

#include "SceneTestRunner.hpp"

#include <csignal>

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
IGNORE_WARNINGS_POP

#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "Cameras/VehicleCamera.hpp"
#include "FlexEngine.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "Pair.hpp"
#include "Physics/PhysicsDebuggingSettings.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Platform/Platform.hpp"
#include "Player.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "ResourceManager.hpp"
#include "Scene/Objects/Building.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/Objects/Vehicle.hpp"
#include "Scene/SceneManager.hpp"
#include "Systems/TrackManager.hpp"
#include "Time.hpp"
#include "Traversal.hpp"

namespace flex
{
	namespace
	{
		// Whether scene tests are saving screenshots (the traversal test saves some of its own)
		bool s_bScreenshots = false;

		// Drives the player through each traversal ability using injected input, checking that each behaves
		// plausibly (errors fail the scene test) & printing what was measured
		class TraversalTestScript
		{
		public:
			enum class Step
			{
				WAIT_FOR_GROUND,
				JUMP,
				GLIDE,
				WINGSUIT,
				LAND,
				THERMAL,
				RELOCATE,
				GRAPPLE,
				CLIMB,
				SKI,
				DIG,
				TREE_GRAPPLE,
				FLAT_GROUND,
				MANTLE,
				ROPE,
				SLOPE,
				DONE
			};

			static const char* StepName(Step step)
			{
				static const char* names[] = { "wait for ground", "jump", "glide", "wingsuit", "land", "thermal", "relocate", "grapple", "climb", "ski", "dig", "tree grapple", "flat ground", "mantle", "rope", "slope", "done" };
				return names[(u32)step];
			}

			Step GetStep() const { return m_Step; }

			TraversalTestScript()
			{
				// e.g. FLEX_TRAVERSAL_ONLY=slope runs only that step (after landing), which is much quicker
				if (const char* only = getenv("FLEX_TRAVERSAL_ONLY"))
				{
					for (u32 i = (u32)Step::JUMP; i < (u32)Step::DONE; ++i)
					{
						if (strcmp(only, StepName((Step)i)) == 0)
						{
							m_OnlyStep = (Step)i;
						}
					}
				}
			}

			void Update()
			{
				if (m_Step == Step::DONE)
				{
					return;
				}

				Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
				TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
				if (player == nullptr || terrainGenerator == nullptr)
				{
					return;
				}
				Traversal* traversal = player->GetTraversal();
				const glm::vec3 pos = player->GetTransform()->GetWorldPosition();
				const glm::vec3 velocity = ToVec3(player->GetRigidBody()->GetRigidBodyInternal()->getLinearVelocity());
				m_StepTime += g_DeltaTime;
				// FLEX_TRAVERSAL_DEBUG=<any>: prints the traversal step state each frame, and mantle details
				if (getenv("FLEX_TRAVERSAL_DEBUG") != nullptr)
				{
					Print("[%s %u %.2f] pos (%.1f, %.1f, %.1f) vel (%.1f, %.1f, %.1f) grounded %d awaiting %d mode %s possessed %d\n", StepName(m_Step), m_Frame, m_StepTime,
						pos.x, pos.y, pos.z, velocity.x, velocity.y, velocity.z, player->IsGrounded(), player->IsAwaitingTerrainCollision(), Traversal::ModeName(traversal->GetMode()), (i32)g_CameraManager->CurrentCamera()->bPossessPlayer);
				}

				switch (m_Step)
				{
				case Step::WAIT_FOR_GROUND:
				{
					if (m_StepTime < g_DeltaTime * 1.5f)
					{
						g_InputManager->SetIgnoreWindowFocus(true);
						g_CameraManager->SetCameraByName("first-person", false);
					}
					if (!player->IsAwaitingTerrainCollision() && player->IsGrounded() && m_StepTime > 1.0f)
					{
						m_SpawnPos = pos;
						NextStep(Step::JUMP);
					}
					else if (m_StepTime > 60.0f)
					{
						Fail("player never landed on the terrain");
					}
				} break;
				case Step::JUMP:
				{
					if (m_Frame == 0)
					{
						m_StartPos = pos;
						m_Max = pos.y;
						Press(KeyCode::KEY_SPACE);
					}
					else if (m_StepTime > 0.4f && m_StepTime - g_DeltaTime <= 0.4f)
					{
						// Held for a full height jump (tapping gives a short hop)
						Release(KeyCode::KEY_SPACE);
					}
					m_Max = glm::max(m_Max, pos.y);
					if (m_StepTime > 1.2f)
					{
						Print("Traversal test: jump height %.2fm\n", m_Max - m_StartPos.y);
						if (m_Max - m_StartPos.y < 0.9f)
						{
							Fail("jumping didn't leave the ground");
						}
						NextStep(Step::GLIDE);
					}
				} break;
				case Step::GLIDE:
				{
					if (m_Frame == 0)
					{
						// Start high above the ground
						Teleport(player, pos + VEC3_UP * 300.0f);
						Look(player, player->GetTransform()->GetForward(), 0.0f);
					}
					else if (m_StepTime > 0.4f && m_StepTime - g_DeltaTime <= 0.4f)
					{
						Press(KeyCode::KEY_SPACE);
					}
					else if (m_StepTime > 0.5f && m_StepTime - g_DeltaTime <= 0.5f)
					{
						Release(KeyCode::KEY_SPACE);
					}
					else if (m_StepTime > 2.5f && m_StepTime < 6.0f)
					{
						Accumulate(velocity);
					}
					else if (m_StepTime >= 6.0f)
					{
						const glm::vec3 average = Average();
						Print("Traversal test: gliding at %.1fm/s horizontally, %.2fm/s vertically (lift %.2fm/s)\n",
							glm::length(glm::vec2(average.x, average.z)), average.y, traversal->ComputeLift(pos));
						if (traversal->GetMode() != Traversal::Mode::GLIDING)
						{
							Fail("glider didn't deploy in the air");
						}
						else if (average.y < -5.0f || glm::length(glm::vec2(average.x, average.z)) < 5.0f)
						{
							Fail("glider fell too fast or flew too slowly");
						}
						NextStep(Step::WINGSUIT);
					}
				} break;
				case Step::WINGSUIT:
				{
					if (m_Frame == 0)
					{
						if (player->IsGrounded())
						{
							// Running only this step (FLEX_TRAVERSAL_ONLY=wingsuit): start high above the ground
							Teleport(player, pos + VEC3_UP * 300.0f);
						}
						Press(KeyCode::KEY_LEFT_CONTROL);
						Look(player, player->GetTransform()->GetForward(), -35.0f);
					}
					if (s_bScreenshots && m_StepTime > 4.4f && m_SubStep < 5 && m_Frame >= m_ShotFrame + 6)
					{
						// Pulling out of the dive: the wingsuit & air streaks, from the third person camera & from behind &
						// beside. Saving a screenshot hitches (& the next frame's long step moves the player a long way), so
						// this waits a few frames after each (the readback completes a couple of frames later).
						const glm::vec3 dir = glm::length(velocity) > 1.0f ? glm::normalize(velocity) : VEC3_FORWARD;
						const glm::vec3 heading = glm::normalize(glm::vec3(velocity.x, 0.0f, velocity.z) + glm::vec3(1e-4f, 0.0f, 0.0f));
						BaseCamera* camera = g_CameraManager->CurrentCamera();
						if (m_SubStep == 0)
						{
							g_CameraManager->SetCameraByName("overhead", false);
							m_ShotTime = m_StepTime;
							m_SubStep = 1;
						}
						else if (m_SubStep == 1 && m_StepTime > m_ShotTime + 0.4f)
						{
							Print("Traversal test: wingsuit at %.1fm/s, openness %.2f, %u air streaks, pitched %.0f degrees, banked %.0f degrees\n", glm::length(velocity),
								traversal->GetWingsuitOpenness(), traversal->GetAirStreakCount(), glm::degrees(traversal->GetBodyPitch()), glm::degrees(traversal->GetBodyBank()));
							RequestScreenshot("traversal_wingsuit_3p");
							m_ShotFrame = m_Frame;
							m_SubStep = 2;
						}
						else if (m_SubStep == 2)
						{
							g_CameraManager->SetCameraByName("debug", false);
							camera = g_CameraManager->CurrentCamera();
							camera->position = pos - dir * 6.0f + VEC3_UP * 2.0f;
							const glm::vec3 toPlayer = glm::normalize(pos - camera->position);
							camera->SetViewDirection(std::atan2(toPlayer.z, toPlayer.x), std::asin(toPlayer.y));
							RequestScreenshot("traversal_wingsuit");
							m_ShotFrame = m_Frame;
							m_SubStep = 3;
						}
						else if (m_SubStep == 3)
						{
							const glm::vec3 right = glm::cross(heading, VEC3_UP);
							// (where the player will be when this frame is rendered)
							const glm::vec3 renderPos = pos + velocity * g_DeltaTime;
							camera->position = renderPos + right * 8.0f + VEC3_UP * 4.0f + heading * 2.0f;
							const glm::vec3 toPlayer = glm::normalize(renderPos - camera->position);
							camera->SetViewDirection(std::atan2(toPlayer.z, toPlayer.x), std::asin(toPlayer.y));
							RequestScreenshot("traversal_wingsuit_side");
							m_ShotFrame = m_Frame;
							m_SubStep = 4;
						}
						else if (m_SubStep == 4)
						{
							g_CameraManager->SetCameraByName("first-person", false);
							m_SubStep = 5;
						}
						// Switching cameras can drop held keys
						Press(KeyCode::KEY_LEFT_CONTROL);
					}
					if (m_StepTime > 4.0f && m_Max == 0.0f)
					{
						// Pull up out of the dive
						m_Max = glm::length(velocity);
						m_StartPos = velocity;
						Look(player, player->GetTransform()->GetForward(), 5.0f);
					}
					if (m_StepTime > 6.0f && (!s_bScreenshots || m_SubStep >= 5))
					{
						Print("Traversal test: wingsuit dive reached %.1fm/s (descending at %.1fm/s), after pulling up: %.1fm/s (vertical %.1fm/s)\n",
							m_Max, -m_StartPos.y, glm::length(velocity), velocity.y);
						if (traversal->GetMode() != Traversal::Mode::WINGSUIT)
						{
							Fail("wingsuit wasn't active while holding ctrl in the air");
						}
						else if (m_Max < 25.0f)
						{
							Fail("wingsuit dive didn't pick up speed");
						}
						else if (velocity.y < m_StartPos.y + 5.0f)
						{
							Fail("pulling up didn't slow the descent");
						}
						if (m_SubStep >= 1 && m_SubStep < 5)
						{
							g_CameraManager->SetCameraByName("first-person", false);
						}
						Release(KeyCode::KEY_LEFT_CONTROL);
						NextStep(Step::LAND);
					}
				} break;
				case Step::LAND:
				{
					if (m_Frame == 1)
					{
						if (traversal->GetMode() != Traversal::Mode::GLIDING)
						{
							Fail("releasing ctrl didn't reopen the glider");
						}
						Press(KeyCode::KEY_SPACE);
					}
					else if (m_Frame == 3)
					{
						Release(KeyCode::KEY_SPACE);
					}
					if (m_Frame > 3 && player->IsGrounded())
					{
						Print("Traversal test: landed after %.1fs\n", m_StepTime);
						if (traversal->GetMode() != Traversal::Mode::NONE)
						{
							Fail("still flying after landing");
						}
						NextStep(Step::THERMAL);
					}
					else if (m_StepTime > 40.0f)
					{
						Fail("never landed after stowing the glider");
						NextStep(Step::THERMAL);
					}
				} break;
				case Step::THERMAL:
				{
					// Glide in the strongest nearby thermal, which should carry the glider upwards
					if (m_Frame == 0)
					{
						std::vector<Traversal::Thermal> thermals;
						traversal->GetThermalsNear(glm::vec2(pos.x, pos.z), 1500.0f, thermals);
						const Traversal::Thermal* best = nullptr;
						for (const Traversal::Thermal& thermal : thermals)
						{
							const terrain::ColumnSample column = terrainGenerator->SampleColumn(thermal.center.x, thermal.center.y);
							const bool bWater = column.lakeID != 0 && column.lakeT < 1.0f && column.height < column.waterLevel;
							if (!bWater && column.temperature > 0.0f && (best == nullptr || thermal.strength > best->strength))
							{
								best = &thermal;
							}
						}
						if (best == nullptr)
						{
							Print("Traversal test: no thermal nearby, skipping\n");
							NextStep(Step::RELOCATE);
							break;
						}
						const real groundHeight = terrainGenerator->SampleColumn(best->center.x, best->center.y).height;
						m_StartPos = glm::vec3(best->center.x, groundHeight + 120.0f, best->center.y);
						// Thermals drift downwind with height, so aim for where it is at this height
						m_Max = traversal->ComputeLift(m_StartPos);
						Teleport(player, m_StartPos);
						Look(player, VEC3_FORWARD, 0.0f);
					}
					else if (m_StepTime > 0.4f && m_StepTime - g_DeltaTime <= 0.4f)
					{
						// Once falling (no longer grounded), the jump button deploys the glider
						Press(KeyCode::KEY_SPACE);
					}
					else if (m_StepTime > 0.5f && m_StepTime - g_DeltaTime <= 0.5f)
					{
						Release(KeyCode::KEY_SPACE);
						if (traversal->GetMode() != Traversal::Mode::GLIDING)
						{
							Fail("glider didn't deploy");
						}
						// Circle to stay in the thermal
						Press(KeyCode::KEY_A);
						Press(KeyCode::KEY_S);
					}
					else if (m_StepTime > 2.0f && m_StepTime < 5.5f)
					{
						Accumulate(velocity);
						// Turn continuously so the glider circles
						Look(player, glm::vec3(std::cos(m_StepTime * 0.6f), 0.0f, std::sin(m_StepTime * 0.6f)), -5.0f);
					}
					else if (m_StepTime >= 5.5f && s_bScreenshots && m_SubStep == 0)
					{
						// Look at the glider from behind (with the measurements already taken)
						m_SubStep = 1;
						g_CameraManager->SetCameraByName("debug", false);
						BaseCamera* camera = g_CameraManager->CurrentCamera();
						const glm::vec3 heading = glm::normalize(glm::vec3(velocity.x, 0.0f, velocity.z) + glm::vec3(1e-4f, 0.0f, 0.0f));
						camera->position = pos - heading * 16.0f + VEC3_UP * 6.0f;
						const glm::vec3 toPlayer = glm::normalize(pos + VEC3_UP * 3.0f - camera->position);
						camera->SetViewDirection(std::atan2(toPlayer.z, toPlayer.x), std::asin(toPlayer.y));
						RequestScreenshot("traversal_glider");
					}
					else if (m_StepTime >= 5.5f && m_SubStep == 1)
					{
						// & from ahead, off to the side the glider is leaning towards (the inside of the turn)
						m_SubStep = 2;
						BaseCamera* camera = g_CameraManager->CurrentCamera();
						const glm::vec3 heading = glm::normalize(glm::vec3(velocity.x, 0.0f, velocity.z) + glm::vec3(1e-4f, 0.0f, 0.0f));
						const glm::vec3 right = glm::cross(heading, VEC3_UP);
						const real side = traversal->GetBodyBank() >= 0.0f ? 1.0f : -1.0f;
						camera->position = pos + heading * 9.0f + right * (side * 9.0f) + VEC3_UP * 1.0f;
						const glm::vec3 toPlayer = glm::normalize(pos + VEC3_UP * 2.5f - camera->position);
						camera->SetViewDirection(std::atan2(toPlayer.z, toPlayer.x), std::asin(toPlayer.y));
						RequestScreenshot("traversal_glider_side");
					}
					else if (m_StepTime >= 5.5f)
					{
						if (m_SubStep >= 1)
						{
							g_CameraManager->SetCameraByName("first-person", false);
						}
						Release(KeyCode::KEY_A);
						Release(KeyCode::KEY_S);
						const glm::vec3 average = Average();
						Print("Traversal test: in a thermal (%.1fm/s lift at the start), the glider climbed at %.2fm/s, banked %.0f degrees & pitched %.0f degrees\n", m_Max, average.y,
							glm::degrees(traversal->GetBodyBank()), glm::degrees(traversal->GetBodyPitch()));
						if (glm::abs(traversal->GetBodyBank()) < glm::radians(5.0f))
						{
							Fail("glider didn't lean into a turn");
						}
						if (m_Max > 1.5f && average.y < 0.0f)
						{
							Fail("glider didn't climb in a thermal");
						}
						Press(KeyCode::KEY_SPACE);
						NextStep(Step::RELOCATE);
					}
				} break;
				case Step::RELOCATE:
				{
					// Grappling & climbing need steep terrain, so move somewhere hilly
					if (m_Frame == 0)
					{
						Release(KeyCode::KEY_SPACE);
						glm::vec3 steepPos;
						if (FindSteepArea(terrainGenerator, pos, steepPos))
						{
							player->TeleportOnTerrain(steepPos);
						}
					}
					else if (m_Frame > 2 && !player->IsAwaitingTerrainCollision() && player->IsGrounded())
					{
						NextStep(Step::GRAPPLE);
					}
					else if (m_StepTime > 60.0f)
					{
						Fail("never landed after relocating");
						NextStep(Step::GRAPPLE);
					}
				} break;
				case Step::GRAPPLE:
				{
					if (m_Frame == 0)
					{
						// From where the hook is fired from (the camera)
						if (!FindTarget(terrainGenerator, g_CameraManager->CurrentCamera()->position, false, m_TargetDir))
						{
							Print("Traversal test: no grapple target in range, skipping\n");
							NextStep(Step::SKI);
							break;
						}
						const glm::vec3 horizontal = glm::normalize(glm::vec3(m_TargetDir.x, 0.0f, m_TargetDir.z));
						Look(player, horizontal, glm::degrees(std::asin(glm::clamp(m_TargetDir.y, -1.0f, 1.0f))));
						Press(KeyCode::KEY_F);
					}
					else if (m_Frame == 2)
					{
						Release(KeyCode::KEY_F);
					}
					else if (m_StepTime > 1.5f && m_Max == 0.0f)
					{
						if (!traversal->IsHookAttached())
						{
							Fail("grappling hook didn't attach to the terrain");
							NextStep(Step::SKI);
							break;
						}
						m_Max = traversal->GetRopeLength();
						if (s_bScreenshots)
						{
							RequestScreenshot("traversal_grapple");
						}
						Press(KeyCode::KEY_SPACE);
					}
					else if (m_StepTime > 3.5f && m_Max > 0.0f && m_StartPos.x == 0.0f)
					{
						Release(KeyCode::KEY_SPACE);
						m_StartPos.x = 1.0f;
						const real distance = glm::distance(pos, traversal->GetHookPos());
						Print("Traversal test: grappled with %.1fm of rope, reeled in to %.1fm (%.1fm from the anchor)\n", m_Max, traversal->GetRopeLength(), distance);
						if (traversal->GetRopeLength() > m_Max - 5.0f && m_Max > 10.0f)
						{
							Fail("reeling in didn't shorten the rope");
						}
						Press(KeyCode::KEY_F);
					}
					else if (m_StartPos.x == 1.0f)
					{
						Release(KeyCode::KEY_F);
						if (player->IsGrounded() || m_StepTime > 30.0f)
						{
							if (traversal->IsGrappling() && traversal->IsHookAttached())
							{
								Fail("grappling hook didn't release");
							}
							NextStep(Step::SKI);
						}
					}
				} break;
				case Step::CLIMB:
				{
					// A tall pillar of rock with a rounded top (built from terrain edits, so there's always one), climbed
					// from the bottom & mantled onto at the top
					if (m_Frame == 0)
					{
						const glm::vec3 heading = glm::normalize(glm::vec3(player->GetLookDirection().x, 0.0f, player->GetLookDirection().z) + glm::vec3(1e-4f, 0.0f, 0.0f));
						glm::vec3 groundPos;
						if (!terrainGenerator->FindStandingPoint(pos + heading * 10.0f, true, groundPos))
						{
							Fail("couldn't find somewhere to build a pillar to climb");
							m_AfterFlatGround = Step::MANTLE; NextStep(Step::FLAT_GROUND);
							break;
						}
						const real pillarRadius = 3.5f;
						for (i32 i = 0; i < 5; ++i)
						{
							terrainGenerator->Fill(groundPos + VEC3_UP * ((real)i * 4.0f - 2.0f), pillarRadius, terrain::Material::GRANITE);
						}
						m_TargetDir = heading;
						// Standing on the ground beside it (which may be higher or lower than at the pillar)
						glm::vec3 standPos = groundPos - heading * (pillarRadius + 0.7f);
						glm::vec3 besidePos;
						if (terrainGenerator->FindStandingPoint(standPos + VEC3_UP * 30.0f, true, besidePos) && glm::distance(glm::vec2(besidePos.x, besidePos.z), glm::vec2(standPos.x, standPos.z)) < 1.0f)
						{
							standPos.y = besidePos.y;
						}
						m_StartPos = standPos + VEC3_UP * (player->GetHeight() * 0.5f + 0.3f);
						m_Max = groundPos.y + 4.0f * 4.0f; // Top of the pillar (roughly)
						Teleport(player, m_StartPos);
						Look(player, heading, 0.0f);
					}
					else if (m_SubStep == 0 && m_StepTime > 1.5f && !terrainGenerator->IsApplyingEdits())
					{
						// Edits have been applied (& collision updated) by now
						Teleport(player, m_StartPos);
						Look(player, m_TargetDir, 0.0f);
						m_SubStep = 1;
						m_SubStepTime = m_StepTime;
					}
					else if (m_SubStep == 1 && m_StepTime > m_SubStepTime + 0.3f && !player->IsAwaitingTerrainCollision())
					{
						Press(KeyCode::KEY_LEFT_SHIFT);
						m_SubStep = 2;
						m_SubStepTime = m_StepTime;
					}
					else if (m_SubStep == 2)
					{
						// Holding shift grabs on (walking into the pillar too, if it's not within reach)
						if (m_StepTime > m_SubStepTime + 0.15f)
						{
							Press(KeyCode::KEY_W);
						}
						if (traversal->GetMode() == Traversal::Mode::CLIMBING)
						{
							Press(KeyCode::KEY_W);
							m_SubStep = 3;
						}
						else if (m_StepTime > m_SubStepTime + 2.0f)
						{
							Fail("didn't grab onto the pillar");
							m_SubStep = 3;
						}
					}
					else if (m_SubStep == 3 && (m_StepTime > 14.0f || (player->IsGrounded() && pos.y > m_StartPos.y + 5.0f)))
					{
						Release(KeyCode::KEY_W);
						Release(KeyCode::KEY_LEFT_SHIFT);
						const real feetY = pos.y - player->GetHeight() * 0.5f;
						Print("Traversal test: climbed %.1fm up a pillar (stamina left: %.0f%%), %s\n", pos.y - m_StartPos.y, traversal->GetStamina() * 100.0f,
							feetY > m_Max - 1.0f && player->IsGrounded() ? "standing on top" : "not on top");
						if (feetY < m_Max - 1.0f)
						{
							Fail("didn't climb up & over the top of the pillar");
						}
						m_SubStep = 0;
						m_AfterFlatGround = Step::MANTLE; NextStep(Step::FLAT_GROUND);
					}
				} break;
				case Step::SKI:
				{
					if (m_Frame == 0)
					{
						m_Max = 0.0f;
					}
					if (!player->IsGrounded() && m_Max == 0.0f)
					{
						// Wait to land (after climbing)
						if (m_StepTime > 30.0f)
						{
							Fail("never landed after climbing");
							NextStep(Step::DONE);
						}
						break;
					}
					if (m_Max == 0.0f)
					{
						m_Max = 1.0f;
						m_StepTime = 0.0f;
						Press(KeyCode::KEY_LEFT_CONTROL);
					}
					else if (m_StepTime > 2.0f)
					{
						// Skiing is currently disabled, so ctrl on the ground should do nothing
						Print("Traversal test: holding ctrl on the ground moved at %.1fm/s\n", glm::length(velocity));
						if (traversal->GetMode() == Traversal::Mode::SKIING)
						{
							Fail("skiing started although it's disabled");
						}
						Release(KeyCode::KEY_LEFT_CONTROL);
						NextStep(Step::DIG);
					}
				} break;
				case Step::DIG:
				{
					if (m_Frame == 0)
					{
						// Dig into the ground ahead from a fixed viewpoint three times: each dig should find terrain at the
						// back of the previous hole (rather than looking straight through it)
						const glm::vec3 eye = player->GetEyePosition();
						const glm::vec3 heading = glm::normalize(glm::vec3(player->GetLookDirection().x, 0.0f, player->GetLookDirection().z) + glm::vec3(1e-4f, 0.0f, 0.0f));
						const glm::vec3 dir = glm::normalize(heading - VEC3_UP * 0.6f);
						u32 digCount = 0;
						for (u32 i = 0; i < 3; ++i)
						{
							glm::vec3 hitPos, hitNormal;
							// Same reach & clamping as the pickaxe (see Player::UpdateTerrainTool)
							const real reach = 8.0f;
							const real radius = 1.6f;
							if (terrainGenerator->Raycast(eye, dir, reach + radius * 0.9f, hitPos, hitNormal))
							{
								if (glm::distance(eye, hitPos) > reach)
								{
									hitPos = eye + dir * reach;
								}
								terrainGenerator->Dig(hitPos, radius);
								++digCount;
							}
						}
						Print("Traversal test: dug the same spot %u/3 times\n", digCount);
						if (digCount < 3)
						{
							Fail("repeated digging didn't keep finding terrain");
						}

						// A small blob of terrain floating in the air beside a dig should be removed by it
						glm::vec3 groundPos;
						if (terrainGenerator->FindStandingPoint(pos + heading * 20.0f, true, groundPos))
						{
							// Centered on a point of the grid the terrain is meshed from (2m), so it's a single solid point
							m_StartPos = glm::round((groundPos + VEC3_UP * 6.0f) / 2.0f) * 2.0f;
							terrainGenerator->Fill(m_StartPos, 1.1f, terrain::Material::SOIL);
							if (terrainGenerator->SampleDensity(m_StartPos) >= 0.0f)
							{
								Fail("filling didn't create terrain");
							}
							// Close enough that the blob is well inside the region checked for fragments
							terrainGenerator->Dig(m_StartPos - VEC3_UP * 3.0f, 1.6f);
							const bool bRemoved = terrainGenerator->SampleDensity(m_StartPos) > 0.0f;
							Print("Traversal test: floating fragment %s\n", bRemoved ? "removed" : "left behind");
							if (!bRemoved)
							{
								Fail("digging didn't remove a nearby floating fragment");
							}
						}
					}
					if (m_StepTime > 1.0f)
					{
						NextStep(Step::TREE_GRAPPLE);
					}
				} break;
				case Step::FLAT_GROUND:
				{
					// Somewhere flat to build things on, near where the test started (a different spot each time, so
					// structures built by earlier steps aren't in the way)
					if (m_Frame == 0)
					{
						const real angle = 2.4f * (real)m_FlatGroundCount;
						const glm::vec3 searchPos = m_SpawnPos + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * (60.0f * (real)m_FlatGroundCount);
						++m_FlatGroundCount;
						glm::vec3 flatPos;
						if (FindFlatGround(terrainGenerator, searchPos, flatPos))
						{
							player->TeleportOnTerrain(flatPos);
						}
						else
						{
							Print("Traversal test: no flat ground nearby, using the spawn point\n");
							player->TeleportOnTerrain(m_SpawnPos + VEC3_UP * 2.0f);
						}
					}
					else if (m_Frame > 2 && m_StepTime > 1.5f && !player->IsAwaitingTerrainCollision() && player->IsGrounded())
					{
						NextStep(m_AfterFlatGround);
					}
					else if (m_StepTime > 30.0f)
					{
						Fail("never landed on flat ground");
						NextStep(m_AfterFlatGround);
					}
				} break;
				case Step::MANTLE:
				{
					// A steep, rounded lip too high to jump onto (like the top of a steep hill): walking into it shouldn't
					// get the player up, jumping into it should (by pulling up over the lip)
					if (m_Frame == 0)
					{
						const glm::vec3 heading = HorizontalLook(player);
						glm::vec3 groundPos;
						if (!terrainGenerator->FindStandingPoint(pos + heading * 12.0f, true, groundPos))
						{
							Fail("couldn't find somewhere to build a ledge");
							NextStep(Step::ROPE);
							break;
						}
						const real radius = 3.0f;
						BuildBlock(terrainGenerator, groundPos + VEC3_UP * 0.3f, heading, radius, 1);
						m_TargetDir = heading;
						m_TargetPos = groundPos;
						m_StartPos = StandingPointNear(terrainGenerator, groundPos - heading * (radius + 2.5f)) + VEC3_UP * (player->GetHeight() * 0.5f + 0.3f);
						Teleport(player, m_StartPos);
					}
					else if (m_SubStep == 0 && m_StepTime > 1.5f && !terrainGenerator->IsApplyingEdits())
					{
						glm::vec3 hitPos, hitNormal;
						m_TopY = terrainGenerator->Raycast(m_TargetPos + VEC3_UP * 20.0f, -VEC3_UP, 30.0f, hitPos, hitNormal) ? hitPos.y : m_TargetPos.y;
						Teleport(player, m_StartPos);
						Look(player, m_TargetDir, 0.0f);
						m_Count = traversal->GetMantleCount();
						m_SubStep = 1;
						m_SubStepTime = m_StepTime;
					}
					else if (m_SubStep == 1 && m_StepTime > m_SubStepTime + 0.3f && !player->IsAwaitingTerrainCollision())
					{
						Press(KeyCode::KEY_W);
						m_SubStep = 2;
						m_SubStepTime = m_StepTime;
					}
					else if (m_SubStep >= 2)
					{
						// Walking into it (stepping up a low lip), or jumping into it after a while, should get the player on
						// top by pulling up over the lip. Input stops once there, so they don't walk off the far side.
						const real groundY = m_StartPos.y - player->GetHeight() * 0.5f - 0.3f;
						const real feetY = pos.y - player->GetHeight() * 0.5f;
						const bool bOnTop = player->IsGrounded() && feetY > groundY + 1.6f && traversal->GetMode() == Traversal::Mode::NONE;
						if (bOnTop || m_StepTime > m_SubStepTime + 6.0f)
						{
							Release(KeyCode::KEY_W);
							Release(KeyCode::KEY_SPACE);
							Print("Traversal test: %s a %.1fm high rounded ledge %s (%u mantles, feet %.1fm above the ground)\n", m_SubStep == 2 ? "walking into" : "jumping into",
								m_TopY - groundY, bOnTop ? "got on top" : "didn't get on top", traversal->GetMantleCount() - m_Count, feetY - groundY);
							if (!bOnTop)
							{
								Fail("didn't pull up over the ledge");
							}
							else if (traversal->GetMantleCount() == m_Count)
							{
								// The terrain's meshing can round the lip off enough to walk up
								Print("Traversal test: the ledge was walkable (no mantling needed)\n");
							}
							NextStep(Step::ROPE);
						}
						else if (m_SubStep == 2 && m_StepTime > m_SubStepTime + 1.8f)
						{
							Press(KeyCode::KEY_SPACE);
							m_SubStep = 3;
						}
						else if (m_SubStep == 3 && m_StepTime > m_SubStepTime + 2.1f)
						{
							Release(KeyCode::KEY_SPACE);
							m_SubStep = 4;
						}
					}
				} break;
				case Step::TREE_GRAPPLE:
				{
					if (m_Frame == 0)
					{
						m_Candidates.clear();
						FindForests(terrainGenerator, pos, m_Candidates);
						m_Count = 0;
						m_ForestTime = 0.0f;
						m_SubStep = 1;
						m_SubStepTime = -10.0f;
					}
					if (m_SubStep == 1 && m_StepTime > m_SubStepTime + 1.0f)
					{
						// Look for a tree from here, then from forests nearby (waiting for their collision to load)
						m_SubStepTime = m_StepTime;
						if (!player->IsAwaitingTerrainCollision() && FindTree(player, m_TargetDir, m_TargetPos))
						{
							Print("Traversal test: found a tree %.0fm away (aiming %.1fm above the ground)\n", glm::distance(g_CameraManager->CurrentCamera()->position, m_TargetPos),
								m_TargetPos.y - terrainGenerator->SampleColumn(m_TargetPos.x, m_TargetPos.z).height);
							const glm::vec3 horizontal = glm::normalize(glm::vec3(m_TargetDir.x, 0.0f, m_TargetDir.z) + glm::vec3(1e-5f, 0.0f, 0.0f));
							Look(player, horizontal, glm::degrees(std::asin(glm::clamp(m_TargetDir.y, -1.0f, 1.0f))));
							m_SubStep = 2;
						}
						else if (m_Count == 0 || m_StepTime > m_ForestTime + 15.0f)
						{
							if (m_Count >= (u32)m_Candidates.size() || m_Count >= 4)
							{
								Fail("couldn't find a tree to grapple to");
								m_AfterFlatGround = Step::CLIMB;
								NextStep(Step::FLAT_GROUND);
								break;
							}
							player->TeleportOnTerrain(m_Candidates[m_Count]);
							++m_Count;
							m_ForestTime = m_StepTime;
						}
					}
					else if (m_SubStep == 2)
					{
						// Aimed (the camera follows the new look direction a frame later)
						Press(KeyCode::KEY_F);
						m_SubStep = 3;
						m_SubStepTime = m_StepTime;
					}
					else if (m_SubStep == 3)
					{
						Release(KeyCode::KEY_F);
						if (m_StepTime > m_SubStepTime + 1.2f)
						{
							const real error = glm::distance(traversal->GetHookPos(), m_TargetPos);
							Print("Traversal test: grappling hook %s the tree (%.1fm from the aimed point)\n", traversal->IsHookAttached() ? "attached to" : "didn't attach to", error);
							if (!traversal->IsHookAttached() || error > 3.0f)
							{
								Fail("grappling hook didn't attach to a tree");
							}
							else if (s_bScreenshots)
							{
								RequestScreenshot("traversal_tree_grapple");
							}
							Press(KeyCode::KEY_F);
							m_SubStep = 4;
							m_SubStepTime = m_StepTime;
						}
					}
					else if (m_SubStep == 4)
					{
						Release(KeyCode::KEY_F);
						if (m_StepTime > m_SubStepTime + 0.5f && (player->IsGrounded() || m_StepTime > m_SubStepTime + 10.0f))
						{
							m_AfterFlatGround = Step::CLIMB;
							NextStep(Step::FLAT_GROUND);
						}
					}
				} break;
				case Step::ROPE:
				{
					// A cliff with a rounded top: toss a rope up onto it, then climb the rope & over the top
					if (m_Frame == 0)
					{
						const glm::vec3 heading = HorizontalLook(player);
						glm::vec3 groundPos;
						if (!terrainGenerator->FindStandingPoint(pos + heading * 14.0f, true, groundPos))
						{
							Fail("couldn't find somewhere to build a cliff");
							NextStep(Step::DONE);
							break;
						}
						const real radius = 3.5f;
						BuildBlock(terrainGenerator, groundPos + VEC3_UP * 1.0f, heading, radius, 2);
						m_TargetDir = heading;
						m_TargetPos = groundPos;
						m_StartPos = StandingPointNear(terrainGenerator, groundPos - heading * (radius + 4.0f)) + VEC3_UP * (player->GetHeight() * 0.5f + 0.3f);
						Teleport(player, m_StartPos);
					}
					else if (m_SubStep == 0 && m_StepTime > 1.5f && !terrainGenerator->IsApplyingEdits())
					{
						glm::vec3 hitPos, hitNormal;
						m_TopY = terrainGenerator->Raycast(m_TargetPos + VEC3_UP * 30.0f, -VEC3_UP, 40.0f, hitPos, hitNormal) ? hitPos.y : m_TargetPos.y;
						Teleport(player, m_StartPos);
						m_SubStep = 1;
						m_SubStepTime = m_StepTime;
					}
					else if (m_SubStep == 1 && m_StepTime > m_SubStepTime + 0.5f && !player->IsAwaitingTerrainCollision())
					{
						// Aim a little above the middle of the top
						const glm::vec3 aim = glm::vec3(m_TargetPos.x, m_TopY + 1.0f, m_TargetPos.z) - g_CameraManager->CurrentCamera()->position;
						const glm::vec3 horizontal = glm::normalize(glm::vec3(aim.x, 0.0f, aim.z) + glm::vec3(1e-5f, 0.0f, 0.0f));
						Look(player, horizontal, glm::degrees(std::atan2(aim.y, glm::length(glm::vec2(aim.x, aim.z)))));
						m_SubStep = 2;
					}
					else if (m_SubStep == 2)
					{
						m_PrevRopeID = traversal->GetLastTossedRopeID();
						if (!traversal->TossRope())
						{
							Fail("couldn't toss a rope");
						}
						m_SubStep = 3;
						m_SubStepTime = m_StepTime;
					}
					else if (m_SubStep == 3 && !traversal->IsTossingRope())
					{
						BuildPiece* rope = (BuildPiece*)traversal->GetLastTossedRopeID().Get();
						if (rope == nullptr || traversal->GetLastTossedRopeID() == m_PrevRopeID)
						{
							Print("Traversal test: rope toss: %s\n", traversal->GetRopeTossFailure().c_str());
							Fail("tossed rope didn't catch on the cliff");
							NextStep(Step::DONE);
							break;
						}
						u32 pieceCount = 0;
						real ropeTop = -1e30f;
						for (BuildPiece* piece : BuildPiece::GetAllPieces())
						{
							if (piece->GetPieceType() == BuildPieceType::ROPE && glm::distance(glm::vec2(piece->GetClimbBottom().x, piece->GetClimbBottom().z), glm::vec2(m_TargetPos.x, m_TargetPos.z)) < 12.0f)
							{
								++pieceCount;
								ropeTop = glm::max(ropeTop, piece->GetClimbTop().y);
							}
						}
						const glm::vec3 bottom = rope->GetClimbBottom();
						Print("Traversal test: tossed rope caught %.1fm below the top of a %.1fm cliff, in %u piece(s), reaching %.1fm above the ground\n",
							m_TopY - ropeTop, m_TopY - m_TargetPos.y, pieceCount, bottom.y - m_TargetPos.y);
						if (s_bScreenshots)
						{
							RequestScreenshot("traversal_rope");
						}
						// Walk into the rope & climb it
						const glm::vec3 toRope = bottom - pos;
						Look(player, glm::normalize(glm::vec3(toRope.x, 0.0f, toRope.z) + glm::vec3(1e-5f, 0.0f, 0.0f)), 0.0f);
						Press(KeyCode::KEY_W);
						// A rope end hanging out of standing reach is grabbed by jumping up to it
						if (bottom.y - pos.y > 2.0f)
						{
							Press(KeyCode::KEY_SPACE);
						}
						m_Count = 0;
						m_SubStep = 4;
						m_SubStepTime = m_StepTime;
					}
					else if (m_SubStep == 3 && m_StepTime > m_SubStepTime + 8.0f)
					{
						Fail("tossed rope never landed");
						NextStep(Step::DONE);
					}
					else if (m_SubStep == 4)
					{
						if (traversal->GetMode() == Traversal::Mode::CLIMBING && !traversal->IsMantling())
						{
							++m_Count;
						}
						const real feetY = pos.y - player->GetHeight() * 0.5f;
						const bool bOnTop = player->IsGrounded() && feetY > m_TopY - 1.5f && traversal->GetMode() == Traversal::Mode::NONE;
						if (bOnTop || m_StepTime > m_SubStepTime + 25.0f)
						{
							Release(KeyCode::KEY_W);
							Release(KeyCode::KEY_SPACE);
							Print("Traversal test: climbing the rope %s (climbed for %u frames, feet %.1fm below the top)\n", bOnTop ? "got on top of the cliff" : "didn't get on top of the cliff", m_Count, m_TopY - feetY);
							if (!bOnTop || m_Count == 0)
							{
								Fail("didn't climb the tossed rope onto the cliff");
							}
							NextStep(Step::SLOPE);
						}
					}
				} break;
				case Step::SLOPE:
				{
					// Standing still on a walkable slope with no input mustn't creep downhill, while a slope too steep
					// to stand on is still slid down
					if (m_Frame == 0)
					{
						if (FindSlope(terrainGenerator, m_SpawnPos, 0.75f, 1.2f, m_TargetPos))
						{
							player->TeleportOnTerrain(m_TargetPos);
						}
						else
						{
							Print("Traversal test: no walkable slope nearby, skipping\n");
							m_SubStep = 2;
						}
					}
					else if (m_SubStep == 0 && m_StepTime > 2.0f && !player->IsAwaitingTerrainCollision() && player->IsGrounded())
					{
						m_StartPos = pos;
						m_SubStepTime = m_StepTime;
						m_SubStep = 1;
						m_Count = 0;
					}
					else if (m_SubStep == 1 && m_StepTime <= m_SubStepTime + 5.0f)
					{
						// Only movement while resting on the ground counts: the terrain's collision can be rebuilt
						// underneath the player as it streams in (more detail), dropping or lifting them a little
						const glm::vec3 step = pos - m_StartPos;
						if (player->IsGrounded() && glm::abs(step.y) < 0.05f && glm::length(step) < 0.2f)
						{
							Accumulate(glm::vec3(glm::length(glm::vec2(step.x, step.z)), step.y, 0.0f));
						}
						else
						{
							++m_Count;
						}
						m_StartPos = pos;
					}
					else if (m_SubStep == 0 && m_StepTime > 30.0f)
					{
						Fail("never landed on the slope");
						m_SubStep = 2;
					}
					else if (m_SubStep == 1 && m_StepTime > m_SubStepTime + 5.0f)
					{
						const real horizontalDrift = m_Sum.x;
						Print("Traversal test: standing still for 5s on a slope (ground normal y %.2f) drifted %.3fm horizontally, %.3fm vertically (%u frames resting, %u frames skipped)\n",
							player->GetGroundNormal().y, horizontalDrift, m_Sum.y, m_SampleCount, m_Count);
						if (horizontalDrift > 0.1f || glm::abs(m_Sum.y) > 0.1f || m_SampleCount < 60)
						{
							Fail("player slid down a walkable slope while standing still");
						}
						m_SubStep = 2;
					}
					else if (m_SubStep == 2)
					{
						if (FindSlope(terrainGenerator, m_SpawnPos, 1.45f, 4.0f, m_TargetPos))
						{
							player->TeleportOnTerrain(m_TargetPos);
							m_SubStepTime = m_StepTime;
							m_SubStep = 3;
						}
						else
						{
							Print("Traversal test: no slope too steep to stand on nearby, skipping\n");
							NextStep(Step::DONE);
						}
					}
					else if (m_SubStep == 3 && m_StepTime > m_SubStepTime + 1.0f && !player->IsAwaitingTerrainCollision())
					{
						m_StartPos = pos;
						m_SubStepTime = m_StepTime;
						m_SubStep = 4;
					}
					else if (m_SubStep == 3 && m_StepTime > m_SubStepTime + 30.0f)
					{
						Fail("never landed on the steep slope");
						NextStep(Step::DONE);
					}
					else if (m_SubStep == 4 && m_StepTime > m_SubStepTime + 2.0f)
					{
						const real moved = glm::distance(pos, m_StartPos);
						const glm::vec3 normal = player->GetGroundNormal();
						Print("Traversal test: on a steep slope (ground normal y %.2f) the player slid %.2fm in 2s\n", normal.y, moved);
						if (moved < 0.3f && normal.y < Player::WALKABLE_NORMAL_Y - 0.05f && traversal->GetMode() == Traversal::Mode::NONE)
						{
							Fail("player stuck to a slope too steep to stand on");
						}
						NextStep(Step::DONE);
					}
				} break;
				default:
					break;
				}

				// Steps start at frame 0 on the next update
				if (m_bStepChanged)
				{
					m_bStepChanged = false;
				}
				else
				{
					++m_Frame;
				}
			}

			void Finish()
			{
				for (KeyCode key : { KeyCode::KEY_SPACE, KeyCode::KEY_LEFT_CONTROL, KeyCode::KEY_LEFT_SHIFT, KeyCode::KEY_F, KeyCode::KEY_W, KeyCode::KEY_A, KeyCode::KEY_S })
				{
					Release(key);
				}
				g_InputManager->SetIgnoreWindowFocus(false);
			}

		private:
			void NextStep(Step step)
			{
				if (m_OnlyStep != Step::DONE && step != m_OnlyStep)
				{
					// Only running one step: go straight to it after landing, & finish after it
					step = m_Step == Step::WAIT_FOR_GROUND ? m_OnlyStep : Step::DONE;
				}
				m_bStepChanged = true;
				m_SubStep = 0;
				m_Step = step;
				m_StepTime = 0.0f;
				m_Frame = 0;
				m_Max = 0.0f;
				m_StartPos = VEC3_ZERO;
				m_Sum = VEC3_ZERO;
				m_SampleCount = 0;
			}

			void Fail(const char* reason)
			{
				PrintError("Traversal test (%s): %s\n", StepName(m_Step), reason);
			}

			static void RequestScreenshot(const char* name)
			{
				const std::string directory = RelativePathToAbsolute(SCREENSHOT_DIRECTORY);
				Platform::CreateDirectoryRecursive(directory);
				g_Renderer->RequestScreenshot(directory + "test_" + name + ".png");
			}

			static void Press(KeyCode key)
			{
				g_InputManager->KeyCallback(key, KeyAction::KEY_PRESS, 0);
			}

			static void Release(KeyCode key)
			{
				g_InputManager->KeyCallback(key, KeyAction::KEY_RELEASE, 0);
			}

			static void Teleport(Player* player, const glm::vec3& pos)
			{
				player->GetTransform()->SetWorldPosition(pos);
				player->GetRigidBody()->GetRigidBodyInternal()->setLinearVelocity(btVector3(0.0f, 0.0f, 0.0f));
			}

			// Points the player along a horizontal direction, pitched up (+) or down (-) by the given degrees
			static void Look(Player* player, const glm::vec3& horizontalDir, real pitchDegrees)
			{
				const real yaw = std::atan2(glm::cross(VEC3_FORWARD, horizontalDir).y, glm::dot(VEC3_FORWARD, horizontalDir));
				player->GetTransform()->SetWorldRotation(glm::angleAxis(yaw, VEC3_UP));
				// The pitch's sign convention isn't assumed: find which way makes the view look up
				player->AddToPitch(-player->GetPitch() + 0.1f);
				const real upSign = player->GetLookDirection().y > 0.0f ? 1.0f : -1.0f;
				player->AddToPitch(-player->GetPitch() + upSign * glm::radians(pitchDegrees));
			}

			void Accumulate(const glm::vec3& value)
			{
				m_Sum += value;
				++m_SampleCount;
			}

			glm::vec3 Average() const
			{
				return m_SampleCount > 0 ? m_Sum / (real)m_SampleCount : VEC3_ZERO;
			}

			static bool FindTarget(TerrainGenerator* terrainGenerator, const glm::vec3& pos, bool bSteep, glm::vec3& outDir)
			{
				FLEX_UNUSED(bSteep);
				for (real pitch : { 20.0f, 35.0f, 50.0f, 8.0f })
				{
					for (u32 i = 0; i < 24; ++i)
					{
						const real yaw = TWO_PI * (real)i / 24.0f;
						const glm::vec3 dir(std::cos(yaw) * std::cos(glm::radians(pitch)), std::sin(glm::radians(pitch)), std::sin(yaw) * std::cos(glm::radians(pitch)));
						glm::vec3 hitPos, hitNormal;
						// Facing the ray (rather than grazing a crest, which a slightly different ray could miss)
						if (terrainGenerator->Raycast(pos, dir, 120.0f, hitPos, hitNormal) && glm::distance(pos, hitPos) > 15.0f &&
							glm::dot(glm::normalize(hitNormal), -dir) > 0.4f)
						{
							outDir = dir;
							return true;
						}
					}
				}
				return false;
			}

			// The steepest dry land within a few kilometers
			static bool FindSteepArea(TerrainGenerator* terrainGenerator, const glm::vec3& center, glm::vec3& outPos)
			{
				const i32 halfCount = 20;
				const real spacing = 100.0f;
				const real e = 15.0f;
				real steepest = 0.0f;
				for (i32 iz = -halfCount; iz <= halfCount; ++iz)
				{
					for (i32 ix = -halfCount; ix <= halfCount; ++ix)
					{
						const real x = center.x + (real)ix * spacing;
						const real z = center.z + (real)iz * spacing;
						const terrain::ColumnSample column = terrainGenerator->SampleColumn(x, z);
						if (column.lakeID != 0 && column.lakeT < 1.0f && column.height < column.waterLevel + 2.0f)
						{
							continue;
						}
						const real dx = terrainGenerator->SampleColumn(x + e, z).height - terrainGenerator->SampleColumn(x - e, z).height;
						const real dz = terrainGenerator->SampleColumn(x, z + e).height - terrainGenerator->SampleColumn(x, z - e).height;
						const real slope = glm::length(glm::vec2(dx, dz)) / (2.0f * e);
						if (slope > steepest)
						{
							steepest = slope;
							outPos = glm::vec3(x, column.height + 40.0f, z);
						}
					}
				}
				Print("Traversal test: steepest slope nearby: %.2f\n", steepest);
				return steepest > 0.0f;
			}

			// Dry land sloping evenly (gradient between minGradient & maxGradient) near center, nearest first. Returns a
			// point above the surface to place the player at.
			static bool FindSlope(TerrainGenerator* terrainGenerator, const glm::vec3& center, real minGradient, real maxGradient, glm::vec3& outPos)
			{
				for (i32 ring = 1; ring < 60; ++ring)
				{
					const u32 count = 8 + (u32)ring * 2;
					for (u32 i = 0; i < count; ++i)
					{
						const real angle = TWO_PI * (real)i / (real)count;
						const real x = center.x + std::cos(angle) * (real)ring * 12.0f;
						const real z = center.z + std::sin(angle) * (real)ring * 12.0f;
						const terrain::ColumnSample column = terrainGenerator->SampleColumn(x, z);
						if ((column.lakeID != 0 && column.lakeT < 1.2f) || column.height < column.waterLevel + 2.0f)
						{
							continue;
						}
						if (terrainGenerator->SampleDensity(glm::vec3(x, column.height + 1.5f, z)) <= 0.0f ||
							terrainGenerator->SampleDensity(glm::vec3(x, column.height - 1.5f, z)) >= 0.0f)
						{
							continue;
						}
						// The same gradient at two scales (a plane, rather than a bump or crease)
						glm::vec2 gradients[2];
						bool bValid = true;
						for (u32 scale = 0; scale < 2 && bValid; ++scale)
						{
							const real e = scale == 0 ? 1.0f : 3.0f;
							const real hx0 = terrainGenerator->SampleColumn(x - e, z).height;
							const real hx1 = terrainGenerator->SampleColumn(x + e, z).height;
							const real hz0 = terrainGenerator->SampleColumn(x, z - e).height;
							const real hz1 = terrainGenerator->SampleColumn(x, z + e).height;
							gradients[scale] = glm::vec2(hx1 - hx0, hz1 - hz0) / (2.0f * e);
							const real gradient = glm::length(gradients[scale]);
							bValid = gradient >= minGradient && gradient <= maxGradient;
						}
						if (!bValid || glm::distance(gradients[0], gradients[1]) > 0.15f)
						{
							continue;
						}
						outPos = glm::vec3(x, column.height + 1.5f, z);
						return true;
					}
				}
				return false;
			}

			static glm::vec3 HorizontalLook(Player* player)
			{
				const glm::vec3 look = player->GetLookDirection();
				return glm::normalize(glm::vec3(look.x, 0.0f, look.z) + glm::vec3(1e-4f, 0.0f, 0.0f));
			}

			static glm::vec3 StandingPointNear(TerrainGenerator* terrainGenerator, const glm::vec3& pos)
			{
				glm::vec3 standPos;
				if (terrainGenerator->FindStandingPoint(pos + VEC3_UP * 30.0f, true, standPos) && glm::distance(glm::vec2(standPos.x, standPos.z), glm::vec2(pos.x, pos.z)) < 1.0f)
				{
					return standPos;
				}
				return pos;
			}

			// A wall of rock (overlapping spheres, rounded on top) across the given direction, layers high
			static void BuildBlock(TerrainGenerator* terrainGenerator, const glm::vec3& base, const glm::vec3& heading, real radius, u32 layers)
			{
				// Clear any trees from the block & the ground around it first: trunks inside or beside the block would
				// catch ropes & block the player, and which ones exist depends on the LOD loaded when they're tested
				const real clearRadius = radius * 2.5f + 8.0f;
				std::vector<terrain::PlantInfo> plants;
				terrainGenerator->FindPlants(base - glm::vec3(clearRadius, 60.0f, clearRadius), base + glm::vec3(clearRadius, 60.0f, clearRadius), plants);
				for (const terrain::PlantInfo& plant : plants)
				{
					terrainGenerator->RemovePlant(plant);
				}

				const glm::vec3 right = glm::cross(heading, VEC3_UP);
				for (u32 layer = 0; layer < layers; ++layer)
				{
					for (i32 i = -1; i <= 1; ++i)
					{
						terrainGenerator->Fill(base + right * ((real)i * radius) + VEC3_UP * ((real)layer * radius), radius, terrain::Material::GRANITE);
					}
				}
			}

			// Dry land without slopes or bumps, nearest first
			static bool FindFlatGround(TerrainGenerator* terrainGenerator, const glm::vec3& center, glm::vec3& outPos)
			{
				for (i32 ring = 0; ring < 14; ++ring)
				{
					const u32 count = ring == 0 ? 1 : 16;
					for (u32 i = 0; i < count; ++i)
					{
						const real angle = TWO_PI * (real)i / (real)count;
						const real x = center.x + std::cos(angle) * (real)ring * 25.0f;
						const real z = center.z + std::sin(angle) * (real)ring * 25.0f;
						const terrain::ColumnSample column = terrainGenerator->SampleColumn(x, z);
						if ((column.lakeID != 0 && column.lakeT < 1.2f) || column.height < column.waterLevel + 2.0f)
						{
							continue;
						}
						// The heightfield must match the actual surface (no overhangs, caves or arches)
						if (terrainGenerator->SampleDensity(glm::vec3(x, column.height + 1.5f, z)) <= 0.0f ||
							terrainGenerator->SampleDensity(glm::vec3(x, column.height - 1.5f, z)) >= 0.0f)
						{
							continue;
						}
						real minHeight = column.height;
						real maxHeight = column.height;
						for (u32 j = 0; j < 12; ++j)
						{
							const real a = TWO_PI * (real)j / 12.0f;
							for (real r : { 6.0f, 14.0f })
							{
								const glm::vec3 p(x + std::cos(a) * r, 0.0f, z + std::sin(a) * r);
								const real h = terrainGenerator->SampleColumn(p.x, p.z).height;
								if (terrainGenerator->SampleDensity(glm::vec3(p.x, h + 2.0f, p.z)) <= 0.0f)
								{
									maxHeight = 1e30f;
								}
								minHeight = glm::min(minHeight, h);
								maxHeight = glm::max(maxHeight, h);
							}
						}
						if (maxHeight - minHeight < 2.5f)
						{
							outPos = glm::vec3(x, column.height + 2.0f, z);
							return true;
						}
					}
				}
				return false;
			}

			// Dry, grassy, temperate land (where trees grow), nearest first
			static void FindForests(TerrainGenerator* terrainGenerator, const glm::vec3& center, std::vector<glm::vec3>& outPositions)
			{
				std::vector<Pair<real, glm::vec3>> candidates;
				const i32 halfCount = 15;
				const real spacing = 160.0f;
				for (i32 iz = -halfCount; iz <= halfCount; ++iz)
				{
					for (i32 ix = -halfCount; ix <= halfCount; ++ix)
					{
						const real x = center.x + (real)ix * spacing;
						const real z = center.z + (real)iz * spacing;
						const terrain::ColumnSample column = terrainGenerator->SampleColumn(x, z);
						if (column.lakeID != 0 && column.lakeT < 1.2f)
						{
							continue;
						}
						if (column.grassiness > 0.7f && column.temperature > -0.3f && column.temperature < 0.5f && column.height > column.waterLevel + 3.0f)
						{
							candidates.push_back({ (real)(ix * ix + iz * iz), glm::vec3(x, column.height + 20.0f, z) });
						}
					}
				}
				std::sort(candidates.begin(), candidates.end(), [](const Pair<real, glm::vec3>& a, const Pair<real, glm::vec3>& b) { return a.first < b.first; });
				for (const Pair<real, glm::vec3>& candidate : candidates)
				{
					outPositions.push_back(candidate.second);
				}
			}

			// A tree (trunk or foliage) in view of the camera, which the grappling hook could reach
			static bool FindTree(Player* player, glm::vec3& outDir, glm::vec3& outHitPos)
			{
				struct TreeRayCallback final : public btCollisionWorld::ClosestRayResultCallback
				{
					TreeRayCallback(const btVector3& from, const btVector3& to, const btCollisionObject* ignored) :
						ClosestRayResultCallback(from, to),
						ignored(ignored)
					{
						m_collisionFilterGroup = (i32)CollisionType::DEFAULT | (i32)CollisionType::RAYCAST_ONLY;
						m_collisionFilterMask = (i32)CollisionType::STATIC | (i32)CollisionType::DEFAULT | (i32)CollisionType::RAYCAST_ONLY;
					}

					virtual bool needsCollision(btBroadphaseProxy* proxy) const override
					{
						return proxy->m_clientObject != ignored && ClosestRayResultCallback::needsCollision(proxy);
					}

					const btCollisionObject* ignored;
				};

				const glm::vec3 origin = g_CameraManager->CurrentCamera()->position;
				btDiscreteDynamicsWorld* world = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
				real bestDistance = 1e30f;
				for (real pitch : { 2.0f, 8.0f, 15.0f, 25.0f })
				{
					for (u32 i = 0; i < 72; ++i)
					{
						const real yaw = TWO_PI * (real)i / 72.0f;
						const glm::vec3 dir(std::cos(yaw) * std::cos(glm::radians(pitch)), std::sin(glm::radians(pitch)), std::sin(yaw) * std::cos(glm::radians(pitch)));
						const btVector3 from = ToBtVec3(origin);
						const btVector3 to = ToBtVec3(origin + dir * 90.0f);
						TreeRayCallback callback(from, to, player->GetRigidBody()->GetRigidBodyInternal());
						world->rayTest(from, to, callback);
						if (!callback.hasHit() || callback.m_collisionObject->getBroadphaseHandle() == nullptr ||
							(callback.m_collisionObject->getBroadphaseHandle()->m_collisionFilterGroup & (i32)CollisionType::RAYCAST_ONLY) == 0)
						{
							continue;
						}
						const glm::vec3 hitPos = ToVec3(callback.m_hitPointWorld);
						const real distance = glm::distance(origin, hitPos);
						// Up in the foliage (which nothing else collides with), not the trunk
						const real heightAboveGround = hitPos.y - TerrainGenerator::GetActive()->SampleColumn(hitPos.x, hitPos.z).height;
						if (distance > 8.0f && distance < bestDistance && heightAboveGround > 3.0f)
						{
							bestDistance = distance;
							outDir = dir;
							outHitPos = hitPos;
						}
					}
				}
				return bestDistance < 1e29f;
			}

			static bool FindWall(TerrainGenerator* terrainGenerator, const glm::vec3& pos, glm::vec3& outPos, glm::vec3& outNormal)
			{
				// Search outwards & upwards for a steep face
				for (real height : { 0.0f, 10.0f, 25.0f })
				{
					for (u32 i = 0; i < 32; ++i)
					{
						const real yaw = TWO_PI * (real)i / 32.0f;
						const glm::vec3 dir(std::cos(yaw), 0.0f, std::sin(yaw));
						const glm::vec3 origin = pos + VEC3_UP * height;
						if (terrainGenerator->SampleDensity(origin) <= 0.0f)
						{
							continue;
						}
						glm::vec3 hitPos, hitNormal;
						if (terrainGenerator->Raycast(origin, dir, 150.0f, hitPos, hitNormal) && hitNormal.y < 0.5f)
						{
							outPos = hitPos;
							outNormal = hitNormal;
							return true;
						}
					}
				}
				return false;
			}

			Step m_Step = Step::WAIT_FOR_GROUND;
			bool m_bStepChanged = false;
			u32 m_SubStep = 0;
			real m_ShotTime = 0.0f;
			u32 m_ShotFrame = 0;
			Step m_OnlyStep = Step::DONE;
			sec m_SubStepTime = 0.0f; // Step time when the current sub step began
			sec m_StepTime = 0.0f;
			u32 m_Frame = 0;
			real m_Max = 0.0f;
			glm::vec3 m_StartPos = VEC3_ZERO;
			glm::vec3 m_TargetDir = VEC3_ZERO;
			glm::vec3 m_TargetPos = VEC3_ZERO;
			real m_TopY = 0.0f;
			u32 m_Count = 0;
			sec m_ForestTime = 0.0f;
			std::vector<glm::vec3> m_Candidates;
			GameObjectID m_PrevRopeID;
			Step m_AfterFlatGround = Step::MANTLE;
			glm::vec3 m_SpawnPos = VEC3_ZERO;
			u32 m_FlatGroundCount = 1;
			glm::vec3 m_Sum = VEC3_ZERO;
			u32 m_SampleCount = 0;
		};

		TraversalTestScript* s_TraversalTest = nullptr;

		// FLEX_VEHICLE_TEST=car|buggy|boat|lineup: drives a vehicle (or rows a boat on the nearest water) with injected input,
		// printing camera smoothness measurements & saving screenshots (brake lights, camera pitch, boat interior).
		// buggy: the dune buggy at full throttle, printing how far it pitched. lineup: places every vehicle & a battery
		// (as the inventory does) on flat ground & photographs them.
		class VehicleTestScript
		{
		public:
			explicit VehicleTestScript(const char* mode) :
				m_bBoat(strcmp(mode, "boat") == 0),
				m_bBuggy(strcmp(mode, "buggy") == 0),
				m_bLineup(strcmp(mode, "lineup") == 0)
			{
			}

			bool IsDone() const { return m_State == State::DONE; }

			void Update()
			{
				if (m_State == State::DONE)
				{
					return;
				}

				BaseScene* scene = g_SceneManager->CurrentScene();
				Player* player = scene->GetPlayer(0);
				TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
				if (player == nullptr || terrainGenerator == nullptr)
				{
					return;
				}

				m_Time += g_DeltaTime;
				const char* prefix = m_bBoat ? "Boat test" : "Vehicle test";

				switch (m_State)
				{
				case State::WAIT_FOR_GROUND:
				{
					if (m_Time < g_DeltaTime * 1.5f)
					{
						g_InputManager->SetIgnoreWindowFocus(true);
						g_CameraManager->SetCameraByName("first-person", false);
					}
					if (!player->IsAwaitingTerrainCollision() && player->IsGrounded() && m_Time > 1.0f && !m_bBoat && !m_bMovedToFlatGround)
					{
						// Drive somewhere flat & open, so the camera measurements aren't of the car crashing into hills
						m_bMovedToFlatGround = true;
						if (FindFlatGround(terrainGenerator, player->GetTransform()->GetWorldPosition(), m_DriveStart))
						{
							Print("Vehicle test: driving from (%.1f, %.1f, %.1f)\n", m_DriveStart.x, m_DriveStart.y, m_DriveStart.z);
							player->TeleportOnTerrain(m_DriveStart - VEC3_RIGHT * 6.0f + VEC3_UP * 2.0f);
							m_Time = 0.0f;
						}
					}
					else if (!player->IsAwaitingTerrainCollision() && player->IsGrounded() && m_Time > 2.0f)
					{
						if (m_bLineup)
						{
							SpawnLineup(terrainGenerator);
							NextState(State::LINEUP);
							break;
						}
						GameObject* vehicle = m_bBoat ? SpawnBoatOnWater(terrainGenerator, player) : SpawnCar(terrainGenerator);
						if (vehicle == nullptr)
						{
							PrintError("%s: failed to spawn\n", prefix);
							m_State = State::DONE;
							return;
						}
						m_VehicleID = vehicle->ID;
						NextState(State::SETTLE);
					}
					else if (m_Time > 60.0f)
					{
						PrintError("%s: player never landed on the terrain\n", prefix);
						m_State = State::DONE;
					}
				} break;
				case State::SETTLE:
				{
					GameObject* vehicle = m_VehicleID.Get();
					if (vehicle == nullptr)
					{
						PrintError("%s: vehicle disappeared\n", prefix);
						m_State = State::DONE;
						return;
					}
					if (m_Time > 0.5f && !player->GetRidingVehicleID().IsValid())
					{
						player->SetRidingVehicle(vehicle);
					}
					// Boats are spawned far away, wait for the terrain (& water) to stream in around them
					if (m_Time > (m_bBoat ? 8.0f : 2.0f))
					{
						PrintStatus(vehicle, "settled");
						SetKey(KeyCode::KEY_W, true);
						NextState(State::DRIVE);
					}
				} break;
				case State::DRIVE:
				{
					GameObject* vehicle = m_VehicleID.Get();
					if (vehicle == nullptr)
					{
						m_State = State::DONE;
						return;
					}

					// Cruise at a steady speed, so the measurements are of the camera rather than of the car crashing
					// (the buggy is held at full throttle, to check it doesn't wheelie or tip over)
					if (m_bBuggy)
					{
						const glm::vec3 forward = vehicle->GetTransform()->GetForward();
						const glm::vec3 up = vehicle->GetTransform()->GetUp();
						m_MaxPitch = glm::max(m_MaxPitch, std::asin(glm::clamp(forward.y, -1.0f, 1.0f)));
						m_MinPitch = glm::min(m_MinPitch, std::asin(glm::clamp(forward.y, -1.0f, 1.0f)));
						m_MinUp = glm::min(m_MinUp, up.y);
						m_MaxSpeed = glm::max(m_MaxSpeed, glm::length(ToVec3(vehicle->GetRigidBody()->GetRigidBodyInternal()->getLinearVelocity())));
						SetKey(KeyCode::KEY_W, true);
						if (glm::mod(m_Time, 0.5f) < g_DeltaTime)
						{
							const glm::vec3 right = vehicle->GetTransform()->GetRight();
							Print("Vehicle test (buggy) t=%.1f: speed %.1fm/s pitch %.1f roll %.1f up %.2f\n", m_Time,
								glm::length(ToVec3(vehicle->GetRigidBody()->GetRigidBodyInternal()->getLinearVelocity())),
								glm::degrees(std::asin(glm::clamp(forward.y, -1.0f, 1.0f))), glm::degrees(std::asin(glm::clamp(right.y, -1.0f, 1.0f))), up.y);
						}
					}
					else if (!m_bBoat)
					{
						const real speed = glm::length(ToVec3(vehicle->GetRigidBody()->GetRigidBodyInternal()->getLinearVelocity()));
						SetKey(KeyCode::KEY_W, speed < 14.0f);
					}

					// Gentle turns, alternating directions
					const real cycle = glm::mod(m_Time, 6.0f);
					const real turnTime = m_bBoat ? 1.0f : 0.3f;
					SetKey(KeyCode::KEY_D, cycle > 2.0f && cycle < 2.0f + turnTime);
					SetKey(KeyCode::KEY_A, cycle > 5.0f && cycle < 5.0f + turnTime);

					if (Crossed(2.0f))
					{
						VehicleCamera::SetStatsEnabled(true);
					}
					if (Crossed(6.0f) || Crossed(10.0f))
					{
						PrintStatus(vehicle, "driving");
					}
					if (Crossed(11.0f))
					{
						RequestScreenshot(m_bBoat ? "boat_rowing" : "vehicle_drive");
					}
					if (Crossed(14.0f))
					{
						if (m_bBuggy)
						{
							Print("Vehicle test (buggy): max pitch %.1f deg (nose up), min pitch %.1f deg, min up %.2f, max speed %.1fm/s%s\n",
								glm::degrees(m_MaxPitch), glm::degrees(m_MinPitch), m_MinUp, m_MaxSpeed, (m_MinUp < 0.5f || m_MaxPitch > glm::radians(25.0f)) ? " TIPPED" : "");
						}
						VehicleCamera::PrintStats(m_bBoat ? "boat" : "car");
						VehicleCamera::SetStatsEnabled(false);
						SetKey(KeyCode::KEY_W, false);
						SetKey(KeyCode::KEY_A, false);
						SetKey(KeyCode::KEY_D, false);
						if (!m_bBoat)
						{
							Press(KeyCode::KEY_SPACE);
						}
						NextState(State::BRAKE);
					}
				} break;
				case State::BRAKE:
				{
					if (Crossed(0.4f))
					{
						RequestScreenshot(m_bBoat ? "boat_stopping" : "vehicle_brake");
					}
					if (Crossed(1.5f))
					{
						Release(KeyCode::KEY_SPACE);
					}
					if (Crossed(2.5f))
					{
						RequestScreenshot(m_bBoat ? "boat_idle" : "vehicle_no_brake");
					}
					if (Crossed(3.0f))
					{
						NextState(State::PITCH);
					}
				} break;
				case State::PITCH:
				{
					BaseCamera* currentCamera = g_CameraManager->CurrentCamera();
					VehicleCamera* camera = currentCamera->type == CameraType::VEHICLE ? (VehicleCamera*)currentCamera : nullptr;
					if (camera == nullptr)
					{
						PrintError("%s: not using the vehicle camera\n", prefix);
						NextState(State::EXIT);
						return;
					}
					if (Crossed(0.1f))
					{
						camera->AddPitchOffset(-10.0f);
						Print("%s: pitch offset (min) %.2f\n", prefix, camera->GetPitchOffset());
					}
					if (Crossed(0.6f))
					{
						RequestScreenshot(m_bBoat ? "boat_pitch_up" : "vehicle_pitch_up");
					}
					if (Crossed(1.0f))
					{
						camera->AddPitchOffset(10.0f);
						Print("%s: pitch offset (max) %.2f\n", prefix, camera->GetPitchOffset());
					}
					if (Crossed(1.5f))
					{
						RequestScreenshot(m_bBoat ? "boat_pitch_down" : "vehicle_pitch_down");
					}
					if (Crossed(2.0f))
					{
						camera->AddPitchOffset(-camera->GetPitchOffset());
						NextState(State::EXIT);
					}
				} break;
				case State::EXIT:
				{
					GameObject* vehicle = m_VehicleID.Get();
					if (Crossed(0.1f) && player->GetRidingVehicleID().IsValid())
					{
						player->SetRidingVehicle(nullptr);
						const glm::vec3 pos = player->GetTransform()->GetWorldPosition();
						real waterHeight;
						const bool bInWater = RowBoat::SampleWaterHeight(pos, waterHeight) && pos.y < waterHeight + 0.5f;
						Print("%s: exited at (%.1f, %.1f, %.1f), %.1fm from the vehicle%s\n", prefix, pos.x, pos.y, pos.z,
							vehicle != nullptr ? glm::distance(pos, vehicle->GetTransform()->GetWorldPosition()) : -1.0f, bInWater ? " (in the water)" : "");
					}
					if (Crossed(2.0f))
					{
						RequestScreenshot(m_bBoat ? "boat_exit" : "vehicle_exit");
					}
					if (Crossed(2.5f))
					{
						if (m_bBoat)
						{
							// Place another boat from the inventory, onto the water
							NextState(State::PLACE);
							break;
						}
						m_State = State::DONE;
					}
				} break;
				case State::PLACE:
				{
					if (Crossed(0.1f))
					{
						const PrefabID boatID = g_ResourceManager->GetPrefabID("row boat");
						const i32 slot = player->GetNextFreeQuickAccessInventorySlot();
						player->AddToInventory(boatID, 1, GameObjectStack::UserData{}, InventoryType::QUICK_ACCESS);
						if (slot >= 0)
						{
							player->SetSelectedQuickAccessItemSlot(slot);
						}
						// Face the open water
						glm::vec3 toWater = m_WaterPos - player->GetTransform()->GetWorldPosition();
						toWater.y = 0.0f;
						if (glm::length(toWater) < 1.0f)
						{
							toWater = VEC3_FORWARD;
						}
						player->GetTransform()->SetWorldRotation(glm::angleAxis(std::atan2(toWater.x, toWater.z), VEC3_UP));
						for (GameObject* object : g_SceneManager->CurrentScene()->GetRootObjects())
						{
							m_ExistingBoatIDs.insert(object->ID);
						}
						Press(KeyCode::KEY_SPACE);
					}
					if (Crossed(0.5f))
					{
						// The hull outline preview, from behind the player
						RequestScreenshot("boat_place_preview");
					}
					if (Crossed(0.6f))
					{
						Release(KeyCode::KEY_SPACE);
					}
					if (Crossed(4.5f))
					{
						RequestScreenshot("boat_placed");
					}
					if (Crossed(5.0f))
					{
						GameObject* placed = nullptr;
						for (GameObject* object : g_SceneManager->CurrentScene()->GetRootObjects())
						{
							if (object->GetTypeID() == RowBoatSID && m_ExistingBoatIDs.count(object->ID) == 0)
							{
								placed = object;
							}
						}
						if (placed == nullptr)
						{
							PrintError("Boat test: placing a boat from the inventory failed\n");
						}
						else
						{
							PrintStatus(placed, "placed from inventory");
							if (((RowBoat*)placed)->GetSubmergedFraction() <= 0.0f)
							{
								PrintError("Boat test: the placed boat isn't floating\n");
							}
						}
						// Back to dry land (the scene check expects the player to be standing on the terrain)
						player->TeleportOnTerrain(m_SearchCenter);
						m_State = State::DONE;
					}
				} break;
				case State::LINEUP:
				{
					// Let them settle, then look at them from a few sides
					const glm::vec3 center = m_DriveStart + VEC3_RIGHT * 9.0f;
					// The last view is a close-up of the battery & tools, from the side at ground level
					const glm::vec3 viewOffsets[] = { glm::vec3(0.0f, 2.5f, -11.0f), glm::vec3(0.0f, 2.5f, 11.0f), glm::vec3(-3.0f, 9.0f, -6.0f), glm::vec3(0.0f, 1.2f, 8.0f) };
					// One view after another, each shown for some frames (time can jump when the camera moves far)
					const i32 viewIndex = m_LineupShots;
					if (Crossed(4.0f))
					{
						for (const GameObjectID& id : m_LineupIDs)
						{
							if (GameObject* object = id.Get())
							{
								const glm::vec3 pos = object->GetTransform()->GetWorldPosition();
								real groundHeight = 0.0f;
								glm::vec3 hitPos, hitNormal;
								if (terrainGenerator->Raycast(pos + VEC3_UP * 20.0f, -VEC3_UP, 60.0f, hitPos, hitNormal))
								{
									groundHeight = hitPos.y;
								}
								Print("Lineup: %s at (%.1f, %.1f, %.1f), %.2fm above the ground, up %.2f\n", object->GetName().c_str(), pos.x, pos.y, pos.z,
									pos.y - groundHeight, object->GetTransform()->GetUp().y);
								// Collider vs mesh (local, scaled), to catch colliders which don't fit
								if (object->GetMesh() != nullptr && object->GetRigidBody() != nullptr && object->GetRigidBody()->GetRigidBodyInternal()->getCollisionShape() != nullptr)
								{
									const glm::vec3 scale = object->GetTransform()->GetWorldScale();
									const glm::vec3 meshMin = object->GetMesh()->m_MinPoint * scale;
									const glm::vec3 meshMax = object->GetMesh()->m_MaxPoint * scale;
									btVector3 shapeMin, shapeMax;
									object->GetRigidBody()->GetRigidBodyInternal()->getCollisionShape()->getAabb(btTransform::getIdentity(), shapeMin, shapeMax);
									Print("Lineup: %s mesh (%.2f, %.2f, %.2f)..(%.2f, %.2f, %.2f) collider (%.2f, %.2f, %.2f)..(%.2f, %.2f, %.2f)\n", object->GetName().c_str(),
										meshMin.x, meshMin.y, meshMin.z, meshMax.x, meshMax.y, meshMax.z,
										shapeMin.getX(), shapeMin.getY(), shapeMin.getZ(), shapeMax.getX(), shapeMax.getY(), shapeMax.getZ());
								}
							}
						}
					}
					if (Crossed(4.0f))
					{
						for (const GameObjectID& id : m_FarIDs)
						{
							if (GameObject* object = id.Get())
							{
								const glm::vec3 pos = object->GetTransform()->GetWorldPosition();
								const real height = terrainGenerator->SampleHeight(glm::vec2(pos.x, pos.z));
								Print("Lineup: far %s is %.2fm above the terrain\n", object->GetName().c_str(), pos.y - height);
								if (pos.y < height - 1.0f)
								{
									PrintError("Lineup: %s fell through the terrain away from the player\n", object->GetName().c_str());
								}
							}
						}
					}
					if (m_Time >= 4.0f && viewIndex < (i32)ARRAY_LENGTH(viewOffsets))
					{
						if (g_CameraManager->CurrentCamera()->GetName() != "debug")
						{
							g_CameraManager->SetCameraByName("debug", false);
						}
						BaseCamera* camera = g_CameraManager->CurrentCamera();
						const glm::vec3 target = (viewIndex == 3) ? m_DriveStart + VEC3_RIGHT * 27.0f + VEC3_UP * 0.8f : center;
						camera->position = target + viewOffsets[viewIndex];
						const glm::vec3 dir = glm::normalize(target - camera->position);
						camera->SetViewDirection(std::atan2(dir.z, dir.x), std::asin(dir.y));
						g_EngineInstance->SetRenderImGui(false);
						if (++m_LineupViewFrames == 30)
						{
							RequestScreenshot(("lineup_" + std::to_string(viewIndex)).c_str());
						}
						else if (m_LineupViewFrames > 31)
						{
							m_LineupViewFrames = 0;
							++m_LineupShots;
						}
					}
					if (m_LineupShots >= (i32)ARRAY_LENGTH(viewOffsets))
					{
						g_EngineInstance->SetRenderImGui(true);
						g_CameraManager->SetCameraByName("first-person", false);
						m_State = State::DONE;
					}
				} break;
				default: break;
				}
			}

			void Finish()
			{
				for (KeyCode key : { KeyCode::KEY_W, KeyCode::KEY_A, KeyCode::KEY_D, KeyCode::KEY_SPACE })
				{
					Release(key);
				}
				VehicleCamera::SetStatsEnabled(false);
				g_InputManager->SetIgnoreWindowFocus(false);
			}

		private:
			enum class State
			{
				WAIT_FOR_GROUND,
				SETTLE,
				DRIVE,
				BRAKE,
				PITCH,
				EXIT,
				PLACE,
				LINEUP,
				DONE
			};

			void NextState(State state)
			{
				m_State = state;
				m_Time = 0.0f;
			}

			// Whether the state's time passed t this frame
			bool Crossed(real t) const
			{
				return m_Time >= t && m_Time - g_DeltaTime < t;
			}

			void SetKey(KeyCode key, bool bDown)
			{
				const bool bWasDown = m_KeysDown.count(key) != 0;
				if (bDown && !bWasDown)
				{
					Press(key);
					m_KeysDown.insert(key);
				}
				else if (!bDown && bWasDown)
				{
					Release(key);
					m_KeysDown.erase(key);
				}
			}

			static void Press(KeyCode key)
			{
				g_InputManager->KeyCallback(key, KeyAction::KEY_PRESS, 0);
			}

			static void Release(KeyCode key)
			{
				g_InputManager->KeyCallback(key, KeyAction::KEY_RELEASE, 0);
			}

			void RequestScreenshot(const char* name)
			{
				if (!s_bScreenshots)
				{
					return;
				}
				const std::string directory = RelativePathToAbsolute(SCREENSHOT_DIRECTORY);
				Platform::CreateDirectoryRecursive(directory);
				g_Renderer->RequestScreenshot(directory + "test_" + name + ".png");
			}

			void PrintStatus(GameObject* vehicle, const char* label)
			{
				const glm::vec3 pos = vehicle->GetTransform()->GetWorldPosition();
				const glm::vec3 vel = ToVec3(vehicle->GetRigidBody()->GetRigidBodyInternal()->getLinearVelocity());
				real waterHeight = 0.0f;
				const bool bWater = RowBoat::SampleWaterHeight(pos, waterHeight);
				std::string extra;
				if (vehicle->GetTypeID() == RowBoatSID)
				{
					RowBoat* boat = (RowBoat*)vehicle;
					extra = " submerged " + FloatToString(boat->GetSubmergedFraction() * 100.0f, 0) + "%";
				}
				Print("%s (%s): pos (%.1f, %.1f, %.1f) speed %.2fm/s up %.2f water %s%.2f%s\n", m_bBoat ? "Boat test" : "Vehicle test", label,
					pos.x, pos.y, pos.z, glm::length(vel), vehicle->GetTransform()->GetUp().y,
					bWater ? "height " : "none ", bWater ? waterHeight : 0.0f, extra.c_str());
			}

			// Finds dry ground which is flat for a long way along +x
			static bool FindFlatGround(TerrainGenerator* terrainGenerator, const glm::vec3& center, glm::vec3& outPos)
			{
				real best = FLT_MAX;
				const real spacing = 60.0f;
				for (i32 iz = -15; iz <= 15; ++iz)
				{
					for (i32 ix = -15; ix <= 15; ++ix)
					{
						const real x = center.x + (real)ix * spacing;
						const real z = center.z + (real)iz * spacing;
						const terrain::ColumnSample start = terrainGenerator->SampleColumn(x, z);
						real deviation = 0.0f;
						for (real d = 0.0f; d <= 160.0f && deviation < best; d += 10.0f)
						{
							for (real lateral : { -8.0f, 0.0f, 8.0f })
							{
								const terrain::ColumnSample column = terrainGenerator->SampleColumn(x + d, z + lateral);
								const bool bWater = column.lakeID != 0 && column.lakeT < 1.05f && column.height < column.waterLevel + 1.0f;
								deviation = glm::max(deviation, bWater ? 1000.0f : glm::abs(column.height - start.height));
							}
						}
						if (deviation < best)
						{
							best = deviation;
							outPos = glm::vec3(x, start.height, z);
						}
					}
				}
				Print("Vehicle test: flattest ground nearby varies by %.1fm\n", best);
				return best < 100.0f;
			}

			GameObject* SpawnCar(TerrainGenerator* terrainGenerator)
			{
				if (!m_bMovedToFlatGround || m_DriveStart == VEC3_ZERO)
				{
					return g_EngineInstance->SpawnVehicleInFrontOfCamera("vehicle");
				}
				const PrefabID carID = g_ResourceManager->GetPrefabID(m_bBuggy ? "dune buggy" : "vehicle");
				glm::vec3 pos = m_DriveStart + VEC3_UP * 3.0f;
				glm::vec3 hitPos, hitNormal;
				if (terrainGenerator->Raycast(m_DriveStart + VEC3_UP * 30.0f, -VEC3_UP, 100.0f, hitPos, hitNormal))
				{
					pos = hitPos + VEC3_UP * 1.5f;
				}
				// Facing +x
				GameObject* car = GameObject::Deitemize(carID, pos, glm::angleAxis(PI_DIV_TWO, VEC3_UP), GameObjectStack::UserData{});
				if (car != nullptr)
				{
					g_SceneManager->CurrentScene()->AddRootObject(car);
				}
				return car;
			}

			// Every vehicle & a battery in a row along +x (placed the way the inventory places items)
			void SpawnLineup(TerrainGenerator* terrainGenerator)
			{
				const char* prefabNames[] = { "vehicle", "dune buggy", "row boat", "battery", "axe", "pickaxe", "shovel" };
				for (i32 i = 0; i < (i32)ARRAY_LENGTH(prefabNames); ++i)
				{
					const PrefabID prefabID = g_ResourceManager->GetPrefabID(prefabNames[i]);
					if (!prefabID.IsValid())
					{
						PrintError("Lineup: no %s prefab\n", prefabNames[i]);
						continue;
					}
					GameObject* templateObject = g_ResourceManager->GetPrefabTemplate(prefabID);
					Print("Lineup: %s template has %s\n", prefabNames[i], (templateObject != nullptr && templateObject->GetMesh() != nullptr) ? "a mesh" : "no mesh");
					const glm::vec3 start = m_DriveStart + VEC3_RIGHT * (real)i * 6.0f;
					glm::vec3 pos = start + VEC3_UP * 2.0f;
					glm::vec3 hitPos, hitNormal;
					if (terrainGenerator->Raycast(start + VEC3_UP * 30.0f, -VEC3_UP, 100.0f, hitPos, hitNormal))
					{
						pos = hitPos + VEC3_UP * 1.5f;
					}
					GameObject* object = GameObject::Deitemize(prefabID, pos, QUAT_IDENTITY, GameObjectStack::UserData{});
					if (object != nullptr)
					{
						g_SceneManager->CurrentScene()->AddRootObject(object);
						m_LineupIDs.push_back(object->ID);
					}
				}

				// A battery & a boat far from the player, where the terrain has no collision yet: they must wait for it
				// rather than fall through the world
				const char* farNames[] = { "row boat", "battery" }; // (another car would exceed the spot light limit)
				for (i32 i = 0; i < 2; ++i)
				{
					const glm::vec3 farPos = m_DriveStart + glm::vec3(900.0f, 0.0f, 10.0f * (real)i);
					const real height = terrainGenerator->SampleHeight(glm::vec2(farPos.x, farPos.z));
					GameObject* object = GameObject::Deitemize(g_ResourceManager->GetPrefabID(farNames[i]), glm::vec3(farPos.x, height + 3.0f, farPos.z),
						QUAT_IDENTITY, GameObjectStack::UserData{});
					if (object != nullptr)
					{
						g_SceneManager->CurrentScene()->AddRootObject(object);
						m_FarIDs.push_back(object->ID);
					}
				}
			}

			// The nearest open water (a few meters deep, away from the shore), with a boat placed on it
			GameObject* SpawnBoatOnWater(TerrainGenerator* terrainGenerator, Player* player)
			{
				const glm::vec3 center = player->GetTransform()->GetWorldPosition();
				m_SearchCenter = center;
				const real spacing = 25.0f;
				const i32 maxRing = 100;
				for (i32 ring = 0; ring <= maxRing; ++ring)
				{
					for (i32 iz = -ring; iz <= ring; ++iz)
					{
						for (i32 ix = -ring; ix <= ring; ++ix)
						{
							if (glm::max(glm::abs(ix), glm::abs(iz)) != ring)
							{
								continue;
							}
							const real x = center.x + (real)ix * spacing;
							const real z = center.z + (real)iz * spacing;
							const terrain::ColumnSample column = terrainGenerator->SampleColumn(x, z);
							m_LakeSamples += column.lakeID != 0 ? 1 : 0;
							m_DeepSamples += (column.lakeID != 0 && column.lakeT < 1.0f && column.waterLevel - column.height > 0.0f) ? 1 : 0;
							if (column.lakeID == 0 || column.lakeT > 0.6f || column.waterLevel - column.height < 2.0f)
							{
								continue;
							}
							// Open water all around
							bool bOpen = true;
							for (u32 i = 0; i < 8 && bOpen; ++i)
							{
								const real angle = TWO_PI * (real)i / 8.0f;
								const terrain::ColumnSample neighbour = terrainGenerator->SampleColumn(x + std::cos(angle) * 15.0f, z + std::sin(angle) * 15.0f);
								bOpen = neighbour.lakeID != 0 && neighbour.lakeT < 1.0f && neighbour.height < neighbour.waterLevel - 0.5f;
							}
							if (!bOpen)
							{
								continue;
							}

							const PrefabID boatID = g_ResourceManager->GetPrefabID("row boat");
							if (!boatID.IsValid())
							{
								PrintError("Boat test: no row boat prefab\n");
								return nullptr;
							}
							const glm::vec3 pos(x, column.waterLevel + 0.3f, z);
							m_WaterPos = pos;
							Print("Boat test: water %.0fm away at (%.1f, %.1f, %.1f), depth %.1fm\n", glm::distance(center, pos), pos.x, pos.y, pos.z, column.waterLevel - column.height);
							GameObject* boat = GameObject::Deitemize(boatID, pos, QUAT_IDENTITY, GameObjectStack::UserData{});
							if (boat != nullptr)
							{
								g_SceneManager->CurrentScene()->AddRootObject(boat);
							}
							return boat;
						}
					}
				}
				PrintError("Boat test: no water within %.0fm (%u samples near a lake, %u under water)\n", spacing * maxRing, m_LakeSamples, m_DeepSamples);
				return nullptr;
			}

			bool m_bBoat;
			bool m_bBuggy;
			bool m_bLineup;
			real m_MaxPitch = -PI;
			real m_MinPitch = PI;
			real m_MinUp = 1.0f;
			real m_MaxSpeed = 0.0f;
			std::vector<GameObjectID> m_LineupIDs;
			std::vector<GameObjectID> m_FarIDs;
			i32 m_LineupShots = 0;
			i32 m_LineupViewFrames = 0;
			u32 m_LakeSamples = 0;
			u32 m_DeepSamples = 0;
			State m_State = State::WAIT_FOR_GROUND;
			sec m_Time = 0.0f;
			GameObjectID m_VehicleID = InvalidGameObjectID;
			std::set<KeyCode> m_KeysDown;
			bool m_bMovedToFlatGround = false;
			glm::vec3 m_DriveStart = VEC3_ZERO;
			glm::vec3 m_SearchCenter = VEC3_ZERO;
			glm::vec3 m_WaterPos = VEC3_ZERO;
			std::set<GameObjectID> m_ExistingBoatIDs;
		};

		VehicleTestScript* s_VehicleTest = nullptr;
		// See BeginTrackTest
		bool s_bTrackTest = false;
	} // anonymous namespace

	SceneTestRunner* SceneTestRunner::s_Instance = nullptr;

	SceneTestRunner::SceneTestRunner(const std::vector<std::string>& sceneFileNames, u32 framesPerScene, bool bCaptureScreenshots, bool bProfile) :
		m_FramesPerScene(glm::max(framesPerScene, 1u)),
		m_bCaptureScreenshots(bCaptureScreenshots),
		m_bProfile(bProfile),
		m_Queue(sceneFileNames)
	{
		AddLogSink(this);

		s_Instance = this;
		std::signal(SIGSEGV, OnCrashSignal);
		std::signal(SIGFPE, OnCrashSignal);
		std::signal(SIGILL, OnCrashSignal);

		// Anything logged before the first scene test begins is attributed to startup
		BeginEntry("<startup>");
		m_StartupBeginTime = Time::CurrentMilliseconds();
	}

	SceneTestRunner::~SceneTestRunner()
	{
		std::signal(SIGSEGV, SIG_DFL);
		std::signal(SIGFPE, SIG_DFL);
		std::signal(SIGILL, SIG_DFL);
		s_Instance = nullptr;

		RemoveLogSink(this);
	}

	void SceneTestRunner::OnEngineInitialized()
	{
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Results.back().loadDuration = Time::CurrentMilliseconds() - m_StartupBeginTime;
		}

		if (m_Queue.empty())
		{
			m_Queue = g_SceneManager->GetSceneFileNames();
		}
	}

	bool SceneTestRunner::Update()
	{
		if (m_FramesRemaining > 0)
		{
			if (!m_bSceneChecksActive)
			{
				BeginSceneChecks();
			}
			if (s_TraversalTest != nullptr)
			{
				s_TraversalTest->Update();
			}
			if (s_VehicleTest != nullptr)
			{
				s_VehicleTest->Update();
			}
			if (s_bTrackTest)
			{
				UpdateTrackTest();
			}
			// FLEX_TEST_SPAWN_OBJECTS=<count>: spawns that many cubes in front of the camera, a few per frame, with runtime-made materials (dynamic UBO growth stress test)
			if (const char* spawnCountStr = getenv("FLEX_TEST_SPAWN_OBJECTS"))
			{
				// Stress test for dynamic uniform buffer growth: spawn objects (some with materials created at runtime)
				// a few at a time, like gameplay does (e.g. chopping plants)
				static u32 s_Spawned = 0;
				const u32 spawnCount = (u32)atoi(spawnCountStr);
				BaseCamera* camera = g_CameraManager->CurrentCamera();
				for (u32 i = 0; i < 16 && s_Spawned < spawnCount; ++i, ++s_Spawned)
				{
					MaterialID matID = g_Renderer->GetPlaceholderMaterialID();
					if (s_Spawned % 3 != 0)
					{
						const std::string matName = "Spawn test " + IntToString((i32)(s_Spawned % 7));
						matID = g_Renderer->GetMaterialID(matName);
						if (matID == InvalidMaterialID)
						{
							MaterialCreateInfo createInfo = {};
							createInfo.name = matName;
							createInfo.shaderName = (s_Spawned % 7) < 4 ? "pbr" : "colour";
							createInfo.constAlbedo = glm::vec4((s_Spawned % 7) / 7.0f, 0.5f, 0.3f, 1.0f);
							createInfo.colourMultiplier = createInfo.constAlbedo;
							createInfo.bDynamic = (s_Spawned % 2) == 0;
							createInfo.bSerializable = false;
							matID = g_Renderer->InitializeMaterial(&createInfo);
						}
					}
					GameObject* obj = new GameObject("Spawn test " + IntToString((i32)s_Spawned), BaseObjectSID);
					obj->SetSerializable(false);
					Mesh* mesh = obj->SetMesh(new Mesh(obj));
					mesh->LoadFromFile(MESH_DIRECTORY "cube.glb", matID);
					const real angle = s_Spawned * 0.37f;
					const glm::vec3 pos = camera->position + camera->forward * 8.0f +
						glm::vec3(glm::cos(angle), 0.0f, glm::sin(angle)) * (1.0f + s_Spawned * 0.01f) + glm::vec3(0.0f, (s_Spawned % 10) * 0.3f - 1.5f, 0.0f);
					obj->GetTransform()->SetWorldPosition(pos);
					obj->GetTransform()->SetLocalScale(glm::vec3(0.15f));
					g_SceneManager->CurrentScene()->AddRootObject(obj);
				}
			}
			// FLEX_TEST_TORCHES=<count>[,<x>,<z>]: builds a village of open huts (three walls & a floor) around x,z (default: the
			// camera), each with a torch inside & one in front, plus lone torches, for testing many point lights & their shadows
			if (const char* torchesStr = getenv("FLEX_TEST_TORCHES"))
			{
				static bool s_bTorchesBuilt = false;
				if (!s_bTorchesBuilt)
				{
					s_bTorchesBuilt = true;
					i32 torchCount = 0;
					real cx = g_CameraManager->CurrentCamera()->position.x;
					real cz = g_CameraManager->CurrentCamera()->position.z;
					sscanf(torchesStr, "%d,%f,%f", &torchCount, &cx, &cz);
					BaseScene* scene = g_SceneManager->CurrentScene();
					TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
					auto GroundHeight = [terrainGenerator](real x, real z)
					{
						return terrainGenerator != nullptr ? terrainGenerator->SampleHeight(glm::vec2(x, z)) : 0.0f;
					};
					auto AddPiece = [scene](BuildPieceType type, const glm::vec3& pos, const glm::quat& rot)
					{
						BuildPiece* piece = new BuildPiece(scene->GetUniqueObjectName(std::string(BuildPieceTypeName(type)) + "_", 3));
						piece->Setup(type, BuildMaterial::WOOD, 0.0f);
						piece->SetSerializable(false);
						piece->GetTransform()->SetWorldPosition(pos);
						piece->GetTransform()->SetWorldRotation(rot);
						scene->AddRootObject(piece);
					};
					const i32 hutCount = torchCount / 3;
					const i32 gridWidth = glm::max((i32)glm::ceil(glm::sqrt((real)hutCount)), 1);
					const real spacing = 11.0f;
					i32 placed = 0;
					for (i32 h = 0; h < hutCount; ++h)
					{
						const real x = cx + ((real)(h % gridWidth) - (gridWidth - 1) * 0.5f) * spacing;
						const real z = cz + ((real)(h / gridWidth) - (gridWidth - 1) * 0.5f) * spacing;
						// Open towards a different side each time
						const glm::quat facing = glm::angleAxis(glm::radians(90.0f * (real)(h % 4)), VEC3_UP);
						const real y = GroundHeight(x, z) - 0.15f;
						const glm::vec3 center(x, y, z);
						AddPiece(BuildPieceType::FLOOR, center, facing);
						for (i32 w = 0; w < 3; ++w)
						{
							AddPiece(BuildPieceType::WALL, center + VEC3_UP * 0.2f, facing * glm::angleAxis(glm::radians(90.0f * (real)w), VEC3_UP));
						}
						// One inside, one outside in front of the open side (-x of the hut's rotated frame)
						AddPiece(BuildPieceType::TORCH, center + VEC3_UP * 0.2f + facing * glm::vec3(0.6f, 0.0f, 0.6f), QUAT_IDENTITY);
						const glm::vec3 outside = center + facing * glm::vec3(-3.6f, 0.0f, -1.0f);
						AddPiece(BuildPieceType::TORCH, glm::vec3(outside.x, GroundHeight(outside.x, outside.z) - 0.1f, outside.z), QUAT_IDENTITY);
						placed += 2;
					}
					// The rest stand alone, in a ring
					for (i32 i = placed; i < torchCount; ++i)
					{
						const real angle = TWO_PI * (real)i / (real)glm::max(torchCount - placed, 1);
						const real radius = (gridWidth * 0.5f + 0.8f) * spacing;
						const real x = cx + glm::cos(angle) * radius;
						const real z = cz + glm::sin(angle) * radius;
						AddPiece(BuildPieceType::TORCH, glm::vec3(x, GroundHeight(x, z) - 0.1f, z), QUAT_IDENTITY);
					}
					Print("Built %d torches in %d huts around (%.1f, %.1f)\n", torchCount, hutCount, cx, cz);
				}
			}
			// FLEX_TEST_SHOW_INVENTORY=<any>: opens the player inventory in a test scene
			if (getenv("FLEX_TEST_SHOW_INVENTORY") != nullptr)
			{
				Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
				if (player != nullptr)
				{
					player->SetInventoryShowing(true);
					static bool bFilled = false;
					// FLEX_TEST_FILL_INVENTORY=<any>: with FLEX_TEST_SHOW_INVENTORY, copies the quick access items into the main inventory so their icons can be checked
					if (!bFilled && getenv("FLEX_TEST_FILL_INVENTORY") != nullptr)
					{
						// Copy the quick access items into the main inventory so its icons can be checked too
						bFilled = true;
						for (u32 i = 0; i < Player::QUICK_ACCESS_ITEM_COUNT; ++i)
						{
							InventoryType inventoryType;
							GameObjectStack* stack = player->GetGameObjectStackFromInventory(Player::GetGameObjectStackIDForQuickAccessInventory(i), inventoryType);
							if (stack != nullptr && stack->count > 0)
							{
								player->AddToInventory(stack->prefabID, stack->count, stack->userData, InventoryType::PLAYER_INVENTORY);
							}
						}
					}
				}
			}
			if (const char* views = getenv("FLEX_TEST_VIEWS"))
			{
				// Fixed views for rendering checks: FLEX_TEST_VIEWS="x,y,z,yaw,pitch,timeOfDay;..." shows each view (from
				// the debug camera, with the time of day paused & no UI or debug drawing) for FLEX_TEST_VIEW_FRAMES frames
				// (default 300), then saves saved/screenshots/test_view_<i>.png. With terrain, y is the height above it.
				static std::vector<std::array<real, 6>> s_Views;
				static bool bParsed = false;
				if (!bParsed)
				{
					bParsed = true;
					for (const std::string& view : Split(views, ';'))
					{
						std::array<real, 6> v = {};
						if (sscanf(view.c_str(), "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6)
						{
							s_Views.push_back(v);
						}
					}
				}
				const u32 viewFrames = getenv("FLEX_TEST_VIEW_FRAMES") != nullptr ? (u32)glm::max(atoi(getenv("FLEX_TEST_VIEW_FRAMES")), 2) : 300u;
				const u32 frame = m_FramesPerScene - 1 - m_FramesRemaining;
				const u32 viewIndex = frame / viewFrames;
				if (viewIndex < (u32)s_Views.size())
				{
					const std::array<real, 6>& v = s_Views[viewIndex];
					if (g_CameraManager->CurrentCamera()->GetName() != "debug")
					{
						g_CameraManager->SetCameraByName("debug", false);
					}
					BaseCamera* camera = g_CameraManager->CurrentCamera();
					TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
					const real groundHeight = terrainGenerator != nullptr ? glm::max(terrainGenerator->SampleHeight(glm::vec2(v[0], v[2])), 0.0f) : 0.0f;
					camera->position = glm::vec3(v[0], groundHeight + v[1], v[2]);
					g_Renderer->GetPhysicsDebuggingSettings().bDrawWireframe = false;
					*TerrainGenerator::GetShowWorldMapPtr() = false;
					g_EngineInstance->SetRenderImGui(false);
					camera->SetViewDirection(v[3], v[4]);
					// For checking shadow shimmer as the sun moves: FLEX_TEST_VIEW_TIME_STEP=<time of day per frame> advances the
					// time of day by that much every frame (e.g. 0.0000028 is a 6000s day at 60fps), & FLEX_TEST_VIEW_SEQUENCE=<n>
					// also saves each view's last n frames (test_view_<i>_<frame>.png) to compare consecutive frames
					static const real s_TimeStep = getenv("FLEX_TEST_VIEW_TIME_STEP") != nullptr ? (real)atof(getenv("FLEX_TEST_VIEW_TIME_STEP")) : 0.0f;
					static const u32 s_SequenceLength = getenv("FLEX_TEST_VIEW_SEQUENCE") != nullptr ? (u32)glm::max(atoi(getenv("FLEX_TEST_VIEW_SEQUENCE")), 0) : 0u;
					g_SceneManager->CurrentScene()->SetTimeOfDay(glm::fract(v[5] + s_TimeStep * (real)(frame % viewFrames)));
					g_SceneManager->CurrentScene()->SetTimeOfDayPaused(true);
					if (s_SequenceLength > 0 && frame % viewFrames >= viewFrames - 1 - glm::min(s_SequenceLength, viewFrames - 1) && frame % viewFrames < viewFrames - 1)
					{
						const std::string directory = RelativePathToAbsolute(SCREENSHOT_DIRECTORY);
						Platform::CreateDirectoryRecursive(directory);
						g_Renderer->RequestScreenshot(directory + "test_view_" + IntToString((i32)viewIndex) + "_" + IntToString((i32)(frame % viewFrames)) + ".png");
					}
					if (frame % viewFrames == viewFrames - 1)
					{
						const std::string directory = RelativePathToAbsolute(SCREENSHOT_DIRECTORY);
						Platform::CreateDirectoryRecursive(directory);
						g_Renderer->RequestScreenshot(directory + "test_view_" + IntToString((i32)viewIndex) + ".png");
						if (terrainGenerator != nullptr && terrainGenerator->GetStreamer() != nullptr)
						{
							std::string report;
							const u32 maskErrors = terrainGenerator->GetStreamer()->ValidateDisplayedMasks(&report);
							const terrain::TerrainStreamer::Stats stats = terrainGenerator->GetStreamer()->GetStats();
							Print("View %u: %u transition mask errors, %u chunks displayed, settled: %d, exposure %.2f\n%s", viewIndex, maskErrors, stats.displayedChunkCount, (i32)stats.bSettled,
								g_SceneManager->CurrentScene()->GetSkyboxData().sun.w, report.c_str());
						}
					}
				}
			}
			if (m_bProfile && m_FramesRemaining == m_FramesPerScene / 2)
			{
				// Skip the first half, while things load
				Profiler::Reset();
				Profiler::SetEnabled(true);
				Profiler::SetHitchLogging(true);
			}
			if (m_FramesRemaining == 1)
			{
				EndSceneChecks();
			}
			--m_FramesRemaining;
			return true;
		}

		if (m_Queue.empty())
		{
			// Don't capture anything logged during shutdown
			StopRecording();
			return false;
		}

		std::string sceneFileName = m_Queue.front();
		m_Queue.erase(m_Queue.begin());

		BeginEntry(sceneFileName);
		ms loadBeginTime = Time::CurrentMilliseconds();
		bool bLoaded = g_SceneManager->SetCurrentScene(sceneFileName);
		ms loadDuration = Time::CurrentMilliseconds() - loadBeginTime;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Results.back().bLoaded = bLoaded;
			m_Results.back().loadDuration = loadDuration;
		}
		// The current frame counts as the first frame
		m_FramesRemaining = bLoaded ? (m_FramesPerScene - 1) : 0;

		return true;
	}

	void SceneTestRunner::BeginSceneChecks()
	{
		m_bSceneChecksActive = true;

		// Debug drawing of the whole physics world (including terrain collision). Skipped when profiling, which
		// should measure regular frames.
		PhysicsDebuggingSettings& physicsDebuggingSettings = g_Renderer->GetPhysicsDebuggingSettings();
		m_bPrevDrawPhysicsWireframe = physicsDebuggingSettings.bDrawWireframe;
		physicsDebuggingSettings.bDrawWireframe = !m_bProfile;

		// e.g. FLEX_DEBUG_OVERLAY=11 shows SSAO (see config/debug_overlay_names.json)
		if (const char* overlay = getenv("FLEX_DEBUG_OVERLAY"))
		{
			g_Renderer->SetDebugOverlayID(atoi(overlay));
		}

		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (terrainGenerator != nullptr)
		{
			bool* bShowMap = TerrainGenerator::GetShowWorldMapPtr();
			m_bPrevShowTerrainMap = *bShowMap;
			*bShowMap = !m_bProfile;

			BaseScene* scene = g_SceneManager->CurrentScene();
			// FLEX_TEST_WORLD_POS=<x>,<z>: moves the test (camera & player) there first, e.g. far from the world origin to
			// check physics & gameplay precision (the traversal & vehicle tests then run there)
			if (const char* worldPos = getenv("FLEX_TEST_WORLD_POS"))
			{
				real x, z;
				if (sscanf(worldPos, "%f,%f", &x, &z) == 2)
				{
					BaseCamera* camera = g_CameraManager->CurrentCamera();
					const terrain::ColumnSample column = terrainGenerator->SampleColumn(x, z);
					camera->position = glm::vec3(x, glm::max(column.height, column.waterLevel) + 20.0f, z);
					if (Player* player = scene->GetPlayer(0))
					{
						player->TeleportOnTerrain(glm::vec3(x, glm::max(column.height, column.waterLevel) + 2.0f, z));
					}
					Print("Test moved to (%.1f, %.1f, %.1f)\n", camera->position.x, camera->position.y, camera->position.z);
				}
			}

			// Spawn the player deep underground, they should end up standing on the surface
			if (scene->GetPlayer(0) == nullptr)
			{
				BaseCamera* camera = g_CameraManager->CurrentCamera();
				const real surfaceHeight = terrainGenerator->SampleHeight(glm::vec2(camera->position.x, camera->position.z));
				scene->SpawnPlayer(glm::vec3(camera->position.x, surfaceHeight - 60.0f, camera->position.z), camera->forward);
			}

			// Profile what the player sees
			if (const char* overlay = getenv("FLEX_DEBUG_OVERLAY"))
			{
				g_Renderer->SetDebugOverlayID(atoi(overlay));
			}
			// FLEX_TEST_FIRST_PERSON=<any>: switches to the first-person camera and ignores window focus (--profile does the same)
			if (m_bProfile || getenv("FLEX_TEST_FIRST_PERSON") != nullptr)
			{
				g_CameraManager->SetCameraByName("first-person", false);
				// FLEX_TEST_CAMERA=<name>: camera to use instead of first-person, when FLEX_TEST_FIRST_PERSON or --profile is set
				if (const char* cameraName = getenv("FLEX_TEST_CAMERA"))
				{
					g_CameraManager->SetCameraByName(cameraName, false);
				}
				g_InputManager->SetIgnoreWindowFocus(true);
			}

			// The traversal abilities need terrain streamed in around the player, which takes a while
			if (m_FramesPerScene >= 1500 && !m_bProfile)
			{
				s_bScreenshots = m_bCaptureScreenshots;
				// Debug lines would clutter the screenshots
				physicsDebuggingSettings.bDrawWireframe = physicsDebuggingSettings.bDrawWireframe && !m_bCaptureScreenshots;
				if (getenv("FLEX_TRACK_TEST") != nullptr)
				{
					s_bTrackTest = true;
					BeginTrackTest(s_bScreenshots);
				}
				else if (const char* vehicleTest = getenv("FLEX_VEHICLE_TEST"))
				{
					s_VehicleTest = new VehicleTestScript(vehicleTest);
				}
				else
				{
					s_TraversalTest = new TraversalTestScript();
				}
			}
		}
	}

	void SceneTestRunner::EndSceneChecks()
	{
		m_bSceneChecksActive = false;

		if (Profiler::IsEnabled())
		{
			Print("Profile of %s:", m_CurrentEntryName.c_str());
			Profiler::PrintReport();
			Profiler::SetEnabled(false);
		}

		if (m_bCaptureScreenshots)
		{
			const std::string directory = RelativePathToAbsolute(SCREENSHOT_DIRECTORY);
			Platform::CreateDirectoryRecursive(directory);
			g_Renderer->RequestScreenshot(directory + "test_" + StripFileType(m_CurrentEntryName) + ".png");
		}

		g_Renderer->GetPhysicsDebuggingSettings().bDrawWireframe = m_bPrevDrawPhysicsWireframe;

		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (terrainGenerator == nullptr)
		{
			return;
		}

		*TerrainGenerator::GetShowWorldMapPtr() = m_bPrevShowTerrainMap;

		if (s_bTrackTest)
		{
			s_bTrackTest = false;
			if (!EndTrackTest())
			{
				PrintError("Track test didn't finish, run with more frames\n");
			}
		}

		if (s_VehicleTest != nullptr)
		{
			if (!s_VehicleTest->IsDone())
			{
				PrintError("Vehicle test didn't finish, run with more frames\n");
			}
			s_VehicleTest->Finish();
			delete s_VehicleTest;
			s_VehicleTest = nullptr;
		}

		if (s_TraversalTest != nullptr)
		{
			if (s_TraversalTest->GetStep() != TraversalTestScript::Step::DONE)
			{
				PrintError("Traversal test didn't finish (stuck at step: %s), run with more frames\n", TraversalTestScript::StepName(s_TraversalTest->GetStep()));
			}
			s_TraversalTest->Finish();
			delete s_TraversalTest;
			s_TraversalTest = nullptr;
		}

		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		if (player == nullptr)
		{
			PrintError("Scene check: failed to spawn player in terrain scene\n");
			return;
		}

		const glm::vec3 pos = player->GetTransform()->GetWorldPosition();
		glm::vec3 groundPos, groundNormal;
		if (terrainGenerator->SampleDensity(pos) <= 0.0f)
		{
			PrintError("Scene check: player is inside the terrain at (%.1f, %.1f, %.1f)\n", pos.x, pos.y, pos.z);
		}
		else if (!terrainGenerator->Raycast(pos, glm::vec3(0.0f, -1.0f, 0.0f), 50.0f, groundPos, groundNormal) || pos.y - groundPos.y > 8.0f)
		{
			PrintError("Scene check: player isn't standing on the terrain at (%.1f, %.1f, %.1f)\n", pos.x, pos.y, pos.z);
		}
		else if (m_FramesPerScene >= 600 && !terrainGenerator->IsCollisionLoadedBelow(pos))
		{
			// Streaming takes a while, so collision is only expected after running for long enough
			PrintError("Scene check: no terrain collision below the player at (%.1f, %.1f, %.1f)\n", pos.x, pos.y, pos.z);
		}
	}

	i32 SceneTestRunner::GetFailureCount() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);

		i32 failureCount = 0;
		for (const Result& result : m_Results)
		{
			if (!result.bLoaded || !result.issues.empty())
			{
				++failureCount;
			}
		}
		return failureCount;
	}

	void SceneTestRunner::PrintSummary() const
	{
		using Colour = Platform::ConsoleColour;

		i32 failureCount = GetFailureCount();

		std::vector<Pair<Colour, std::string>> lines;
		std::vector<Pair<Colour, std::string>> failureLines;
		u32 issueCount = 0;

		{
			std::lock_guard<std::mutex> lock(m_Mutex);

			lines.emplace_back(Colour::DEFAULT, "\n=== Scene test results (" + UIntToString(m_FramesPerScene) + " frames per scene) ===\n");

			ms totalLoadDuration = 0.0f;
			for (const Result& result : m_Results)
			{
				totalLoadDuration += result.loadDuration;
				const std::string durationStr = " [" + IntToString((i32)result.loadDuration) + " ms]";

				u32 errorCount = 0;
				for (const Issue& issue : result.issues)
				{
					errorCount += issue.bError ? 1 : 0;
				}
				u32 warningCount = (u32)result.issues.size() - errorCount;

				if (!result.bLoaded)
				{
					lines.emplace_back(Colour::ERROR, "  FAIL  " + result.sceneFileName + " (failed to load)" + durationStr + "\n");
					failureLines.emplace_back(Colour::ERROR, "  " + result.sceneFileName + ": failed to load\n");
					++issueCount;
				}
				else if (!result.issues.empty())
				{
					lines.emplace_back(Colour::ERROR, "  FAIL  " + result.sceneFileName + " (" + UIntToString(warningCount) + " warnings, " + UIntToString(errorCount) + " errors)" + durationStr + "\n");
				}
				else
				{
					lines.emplace_back(Colour::SUCCESS, "  PASS  " + result.sceneFileName + durationStr + "\n");
				}

				// Only use the first line of each message, and collapse consecutive duplicates
				for (u32 i = 0; i < (u32)result.issues.size();)
				{
					const Issue& issue = result.issues[i];
					u32 repeatCount = 1;
					while (i + repeatCount < (u32)result.issues.size() &&
						result.issues[i + repeatCount].bError == issue.bError &&
						result.issues[i + repeatCount].message == issue.message)
					{
						++repeatCount;
					}
					i += repeatCount;

					std::string message = TrimLeadingWhitespace(issue.message);
					message = Trim(message.substr(0, message.find('\n')));
					if (message.empty())
					{
						continue;
					}

					std::string line = "  " + result.sceneFileName + ": " + (issue.bError ? "error: " : "warning: ") + message;
					if (repeatCount > 1)
					{
						line += " (x" + UIntToString(repeatCount) + ")";
					}
					failureLines.emplace_back(issue.bError ? Colour::ERROR : Colour::WARNING, line + "\n");
					issueCount += repeatCount;
				}
			}

			lines.emplace_back(failureCount == 0 ? Colour::SUCCESS : Colour::ERROR,
				IntToString((i32)m_Results.size() - failureCount) + "/" + UIntToString((u32)m_Results.size()) + " passed (" + IntToString((i32)totalLoadDuration) + " ms total load time)\n");
		}

		if (failureCount > 0)
		{
			lines.emplace_back(Colour::ERROR, "\n" + IntToString(failureCount) + " failed, " + UIntToString(issueCount) + " issues:\n");
			lines.insert(lines.end(), failureLines.begin(), failureLines.end());
		}

		// Print outside of m_Mutex, as printing calls back into OnLog
		for (const Pair<Colour, std::string>& line : lines)
		{
			PrintColouredLong(line.first, line.second.c_str());
		}
		Platform::SetConsoleTextColour(Colour::DEFAULT);
	}

	void SceneTestRunner::OnLog(LogLevel level, const char* message)
	{
		if (level == LogLevel::MESSAGE)
		{
			return;
		}

		std::lock_guard<std::mutex> lock(m_Mutex);

		if (m_bRecording)
		{
			m_Results.back().issues.push_back({ level == LogLevel::ERROR, std::string(message) });
		}
	}

	void SceneTestRunner::BeginEntry(const std::string& name)
	{
		m_CurrentEntryName = name;

		std::lock_guard<std::mutex> lock(m_Mutex);

		Result result = {};
		result.sceneFileName = name;
		// Startup has nothing to load
		result.bLoaded = m_Results.empty();
		m_Results.push_back(result);
		m_bRecording = true;
	}

	void SceneTestRunner::StopRecording()
	{
		m_CurrentEntryName = "<shutdown>";

		std::lock_guard<std::mutex> lock(m_Mutex);

		m_bRecording = false;
	}

	void SceneTestRunner::OnCrashSignal(i32 signal)
	{
		// Not async-signal-safe, but the process is going down anyway. Print is used (rather than
		// PrintError) since OnLog ignores messages and so won't try to take m_Mutex, which may be held.
		const char* entryName = s_Instance != nullptr ? s_Instance->m_CurrentEntryName.c_str() : "unknown";
		Print("\n=== Scene test crashed (signal %d) while testing %s ===\n", signal, entryName);
		Platform::PrintStackTrace();

		std::signal(signal, SIG_DFL);
		std::raise(signal);
	}
} // namespace flex
