#include "stdafx.hpp"

#include "SceneTestRunner.hpp"

#include <csignal>

IGNORE_WARNINGS_PUSH
#include <BulletDynamics/Dynamics/btRigidBody.h>
IGNORE_WARNINGS_POP

#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "Pair.hpp"
#include "Physics/PhysicsDebuggingSettings.hpp"
#include "Physics/RigidBody.hpp"
#include "Platform/Platform.hpp"
#include "Player.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/SceneManager.hpp"
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
				DONE
			};

			static const char* StepName(Step step)
			{
				static const char* names[] = { "wait for ground", "jump", "glide", "wingsuit", "land", "thermal", "relocate", "grapple", "climb", "ski", "done" };
				return names[(u32)step];
			}

			Step GetStep() const { return m_Step; }

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
					else if (m_Frame == 2)
					{
						Release(KeyCode::KEY_SPACE);
					}
					m_Max = glm::max(m_Max, pos.y);
					if (m_StepTime > 1.2f)
					{
						Print("Traversal test: jump height %.2fm\n", m_Max - m_StartPos.y);
						if (m_Max - m_StartPos.y < 1.0f)
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
						Press(KeyCode::KEY_LEFT_CONTROL);
						Look(player, player->GetTransform()->GetForward(), -35.0f);
					}
					if (m_StepTime > 4.0f && m_Max == 0.0f)
					{
						// Pull up out of the dive
						m_Max = glm::length(velocity);
						m_StartPos = velocity;
						Look(player, player->GetTransform()->GetForward(), 5.0f);
					}
					if (m_StepTime > 6.0f)
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
					else if (m_StepTime >= 5.5f)
					{
						if (m_SubStep == 1)
						{
							g_CameraManager->SetCameraByName("first-person", false);
						}
						Release(KeyCode::KEY_A);
						Release(KeyCode::KEY_S);
						const glm::vec3 average = Average();
						Print("Traversal test: in a thermal (%.1fm/s lift at the start), the glider climbed at %.2fm/s\n", m_Max, average.y);
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
						if (!FindTarget(terrainGenerator, pos, false, m_TargetDir))
						{
							Print("Traversal test: no grapple target in range, skipping\n");
							NextStep(Step::CLIMB);
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
							NextStep(Step::CLIMB);
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
							NextStep(Step::CLIMB);
						}
					}
				} break;
				case Step::CLIMB:
				{
					if (m_Frame == 0)
					{
						glm::vec3 wallPos, wallNormal;
						if (!FindWall(terrainGenerator, pos, wallPos, wallNormal))
						{
							Print("Traversal test: no steep wall nearby, skipping climbing\n");
							NextStep(Step::SKI);
							break;
						}
						Teleport(player, wallPos + wallNormal * 1.3f);
						Look(player, -glm::normalize(glm::vec3(wallNormal.x, 0.0f, wallNormal.z)), 0.0f);
						m_StartPos = wallPos + wallNormal * 1.3f;
						Press(KeyCode::KEY_LEFT_SHIFT);
					}
					else if (m_Frame == 3)
					{
						if (traversal->GetMode() != Traversal::Mode::CLIMBING)
						{
							Fail("didn't grab onto a steep wall");
						}
						Press(KeyCode::KEY_W);
					}
					else if (m_StepTime > 2.5f)
					{
						Release(KeyCode::KEY_W);
						Release(KeyCode::KEY_LEFT_SHIFT);
						Print("Traversal test: climbed %.1fm in 2.5s (stamina left: %.0f%%)\n", pos.y - m_StartPos.y, traversal->GetStamina() * 100.0f);
						if (pos.y - m_StartPos.y < 2.0f)
						{
							Fail("climbing didn't move the player up the wall");
						}
						NextStep(Step::SKI);
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
						Print("Traversal test: skiing at %.1fm/s\n", glm::length(velocity));
						if (traversal->GetMode() != Traversal::Mode::SKIING)
						{
							Fail("holding ctrl on the ground didn't start skiing");
						}
						Release(KeyCode::KEY_LEFT_CONTROL);
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
						if (terrainGenerator->Raycast(pos, dir, 120.0f, hitPos, hitNormal) && glm::distance(pos, hitPos) > 15.0f)
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
			sec m_StepTime = 0.0f;
			u32 m_Frame = 0;
			real m_Max = 0.0f;
			glm::vec3 m_StartPos = VEC3_ZERO;
			glm::vec3 m_TargetDir = VEC3_ZERO;
			glm::vec3 m_Sum = VEC3_ZERO;
			u32 m_SampleCount = 0;
		};

		TraversalTestScript* s_TraversalTest = nullptr;
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
			if (m_bProfile && m_FramesRemaining == m_FramesPerScene / 2)
			{
				// Skip the first half, while things load
				Profiler::Reset();
				Profiler::SetEnabled(true);
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

		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (terrainGenerator != nullptr)
		{
			bool* bShowMap = TerrainGenerator::GetShowWorldMapPtr();
			m_bPrevShowTerrainMap = *bShowMap;
			*bShowMap = !m_bProfile;

			// Spawn the player deep underground, they should end up standing on the surface
			BaseScene* scene = g_SceneManager->CurrentScene();
			if (scene->GetPlayer(0) == nullptr)
			{
				BaseCamera* camera = g_CameraManager->CurrentCamera();
				const real surfaceHeight = terrainGenerator->SampleHeight(glm::vec2(camera->position.x, camera->position.z));
				scene->SpawnPlayer(glm::vec3(camera->position.x, surfaceHeight - 60.0f, camera->position.z), camera->forward);
			}

			// Profile what the player sees
			if (m_bProfile)
			{
				g_CameraManager->SetCameraByName("first-person", false);
			}

			// The traversal abilities need terrain streamed in around the player, which takes a while
			if (m_FramesPerScene >= 1500 && !m_bProfile)
			{
				s_bScreenshots = m_bCaptureScreenshots;
				// Debug lines would clutter the screenshots
				physicsDebuggingSettings.bDrawWireframe = physicsDebuggingSettings.bDrawWireframe && !m_bCaptureScreenshots;
				s_TraversalTest = new TraversalTestScript();
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
