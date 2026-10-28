#include "stdafx.hpp"

#include "Game.hpp"
#include "Harvest.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletDynamics/Dynamics/btRigidBody.h>
IGNORE_WARNINGS_POP

#include "Audio/AudioManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "FlexEngine.hpp"
#include "GameConsole.hpp"
#include "GameControls.hpp"
#include "GameSounds.hpp"
#include "Graphics/Renderer.hpp"
#include "InputManager.hpp"
#include "JSONParser.hpp"
#include "Localization.hpp"
#include "Physics/RigidBody.hpp"
#include "Platform/Platform.hpp"
#include "Profiler.hpp"
#include "SaveFile.hpp"
#include "Player.hpp"
#include "PlayerController.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Objects/Animals.hpp"
#include "Scene/Objects/Building.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/Objects/Treasure.hpp"
#include "Scene/SceneManager.hpp"
#include "Systems/TrackManager.hpp"
#include "Time.hpp"
#include "ResourceManager.hpp"
#include "Traversal.hpp"
#include "Window/Window.hpp"

namespace flex
{
	Game* g_Game = nullptr;

	const char* Game::GAMEPLAY_SCENE_NAME = "scene_world.json";
	const char* Game::GAME_NAME = "Fira";
	const char* Game::GAME_VERSION = "0.1.0";
	bool Game::s_bAutoPlay = false;
	std::string Game::s_AutoPlayWorldName;

	namespace
	{
		const i32 WORLD_FILE_VERSION = 1;
		const sec AUTOSAVE_INTERVAL = 90.0f;

		std::string WorldsDirectory()
		{
			return RelativePathToAbsolute(SAVED_DIRECTORY "worlds/");
		}

		// Folder names are derived from world names, keeping only safe characters
		std::string SanitizeName(const std::string& name)
		{
			std::string result;
			for (char c : name)
			{
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
				{
					result += c;
				}
				else if (c == ' ' || c == '-' || c == '_')
				{
					result += '_';
				}
			}
			return result.empty() ? std::string("world") : result;
		}

		std::string FormatPlayTime(real seconds)
		{
			const i32 minutes = (i32)(seconds / 60.0f);
			if (minutes < 60)
			{
				return LOCF(Units_Minutes, minutes);
			}
			return LOCF(Units_HoursMinutes, minutes / 60, minutes % 60);
		}

		// "2026-10-25_08-16-38" -> "2026-10-25 08:16"
		std::string FormatDate(const std::string& date)
		{
			if (date.size() < 16)
			{
				return date;
			}
			std::string result = date.substr(0, 10) + " " + date.substr(11, 5);
			result[13] = ':';
			return result;
		}

		i32 RandomSeed()
		{
			return (i32)((Time::NowTicks() * 2654435761ull) >> 16 & 0x7FFFFFFF);
		}

		// For measuring frame times & hitches while terrain streams in: FLEX_PLAY_BENCHMARK=<seconds> (with --play) walks,
		// then glides & dives in a wingsuit in a fixed direction using injected input, then prints the profile (including
		// the frame time distribution & every hitch) and quits. FLEX_HITCH_LOG=1 only logs hitches during regular play.
		class PlayBenchmark
		{
		public:
			// Quits if the benchmark hasn't finished long after it should have (e.g. stuck on a screen other than
			// gameplay), so scripted runs can't hang
			void Watchdog()
			{
				const char* durationStr = getenv("FLEX_PLAY_BENCHMARK");
				if (durationStr != nullptr && !m_bDone && g_SecElapsedSinceProgramStart > atof(durationStr) + 300.0)
				{
					m_bDone = true;
					PrintError("Play benchmark didn't finish in time, quitting\n");
					g_EngineInstance->Stop();
				}
			}

			void Update(Player* player)
			{
				if (player == nullptr || m_bDone || (!m_bStarted && player->IsAwaitingTerrainCollision()))
				{
					return;
				}

				const char* durationStr = getenv("FLEX_PLAY_BENCHMARK");
				if (!m_bStarted)
				{
					m_bStarted = true;
					Profiler::Reset();
					Profiler::SetEnabled(true);
					Profiler::SetHitchLogging(true);
					if (durationStr == nullptr)
					{
						// Hitch logging only
						m_bDone = true;
						return;
					}
					m_Duration = glm::max((real)atof(durationStr), 10.0f);
					g_InputManager->SetIgnoreWindowFocus(true);
					m_StartPos = player->GetTransform()->GetWorldPosition();
					Print("Play benchmark: %.0fs from (%.1f, %.1f, %.1f)\n", m_Duration, m_StartPos.x, m_StartPos.y, m_StartPos.z);
				}

				// Real time, so a slow frame doesn't shorten the distance covered
				m_Time += g_UnpausedDeltaTime;
				const glm::vec3 dir = glm::normalize(glm::vec3(1.0f, 0.0f, 0.35f));
				const glm::vec3 pos = player->GetTransform()->GetWorldPosition();
				Traversal* traversal = player->GetTraversal();
				TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
				const real groundHeight = terrainGenerator != nullptr ? terrainGenerator->SampleHeight(glm::vec2(pos.x, pos.z)) : pos.y;

				if (m_Time < m_Duration * 0.25f)
				{
					// Sprint
					if (!m_bWalking)
					{
						m_bWalking = true;
						Look(player, dir, 0.0f);
						Press(KeyCode::KEY_W);
						Press(KeyCode::KEY_LEFT_SHIFT);
					}
				}
				else if (m_Time < m_Duration)
				{
					if (m_bWalking)
					{
						m_bWalking = false;
						Release(KeyCode::KEY_W);
						Release(KeyCode::KEY_LEFT_SHIFT);
					}
					// Fly: start high up, glide for a while, then dive in the wingsuit (much faster), climbing back up whenever
					// too close to the ground
					m_FlightTime += g_UnpausedDeltaTime;
					if (pos.y - groundHeight < 40.0f && m_FlightTime > 1.0f)
					{
						Release(KeyCode::KEY_LEFT_CONTROL);
						player->GetTransform()->SetWorldPosition(glm::vec3(pos.x, glm::max(pos.y, groundHeight) + 180.0f, pos.z));
						player->GetRigidBody()->GetRigidBodyInternal()->setLinearVelocity(btVector3(0.0f, 0.0f, 0.0f));
						Look(player, dir, 0.0f);
						m_FlightTime = 0.0f;
					}
					if (m_FlightTime > 0.3f && traversal->GetMode() == Traversal::Mode::NONE && !player->IsGrounded())
					{
						// Opens the glider (released again next frame)
						Press(KeyCode::KEY_SPACE);
						m_bSpaceHeld = true;
					}
					else if (m_bSpaceHeld)
					{
						Release(KeyCode::KEY_SPACE);
						m_bSpaceHeld = false;
					}
					if (m_FlightTime > 4.0f && traversal->GetMode() == Traversal::Mode::GLIDING)
					{
						Press(KeyCode::KEY_LEFT_CONTROL);
						Look(player, dir, -20.0f);
					}
				}
				else
				{
					m_bDone = true;
					Release(KeyCode::KEY_LEFT_CONTROL);
					Release(KeyCode::KEY_SPACE);
					const glm::vec3 endPos = player->GetTransform()->GetWorldPosition();
					Print("Play benchmark: travelled %.0fm horizontally\n", glm::length(glm::vec2(endPos.x - m_StartPos.x, endPos.z - m_StartPos.z)));
					Profiler::PrintReport();
					Profiler::SetEnabled(false);
					g_EngineInstance->Stop();
				}
			}

		private:
			static void Press(KeyCode key)
			{
				g_InputManager->KeyCallback(key, KeyAction::KEY_PRESS, 0);
			}

			static void Release(KeyCode key)
			{
				g_InputManager->KeyCallback(key, KeyAction::KEY_RELEASE, 0);
			}

			// Points the player along a horizontal direction, pitched up (+) or down (-) by the given degrees
			static void Look(Player* player, const glm::vec3& horizontalDir, real pitchDegrees)
			{
				const real yaw = std::atan2(glm::cross(VEC3_FORWARD, horizontalDir).y, glm::dot(VEC3_FORWARD, horizontalDir));
				player->GetTransform()->SetWorldRotation(glm::angleAxis(yaw, VEC3_UP));
				player->AddToPitch(-player->GetPitch() + 0.1f);
				const real upSign = player->GetLookDirection().y > 0.0f ? 1.0f : -1.0f;
				player->AddToPitch(-player->GetPitch() + upSign * glm::radians(pitchDegrees));
			}

			bool m_bStarted = false;
			bool m_bDone = false;
			bool m_bWalking = false;
			bool m_bSpaceHeld = false;
			real m_Duration = 0.0f;
			sec m_Time = 0.0f;
			sec m_FlightTime = 0.0f;
			glm::vec3 m_StartPos;
		};
		PlayBenchmark s_PlayBenchmark;

		// Untextured quad drawn after the UI (so it's on top of sprites like the map), centered on a pixel position
		void EnqueueOverlayQuad(const glm::vec2& centerPx, const glm::vec2& halfSizePx, const glm::vec4& colour, const glm::vec2& screenSize, u32 zOrder = 90)
		{
			const real aspectRatio = screenSize.x / screenSize.y;
			SpriteQuadDrawInfo drawInfo = {};
			drawInfo.anchor = AnchorPoint::CENTER;
			drawInfo.bScreenSpace = true;
			drawInfo.bReadDepth = false;
			drawInfo.bEnableAlbedoSampler = false;
			drawInfo.zOrder = zOrder;
			drawInfo.colour = colour;
			drawInfo.pos = glm::vec3((centerPx.x / screenSize.x * 2.0f - 1.0f) * aspectRatio, 1.0f - centerPx.y / screenSize.y * 2.0f, 0.0f);
			drawInfo.scale = glm::vec3(halfSizePx.x / screenSize.y * 2.0f, halfSizePx.y / screenSize.y * 2.0f, 1.0f);
			g_Renderer->EnqueueSprite(drawInfo);
		}
	} // anonymous namespace

	WorldStats* GetCurrentWorldStats()
	{
		return (g_Game != nullptr && g_Game->m_bWorldLoaded) ? &g_Game->m_CurrentWorld.stats : nullptr;
	}

	void WorldStats::Serialize(JSONObject& parentObject) const
	{
		JSONObject statsObj = {};
		statsObj.fields.emplace_back("torches placed", JSONValue(torchesPlaced));
		statsObj.fields.emplace_back("pieces built", JSONValue(piecesBuilt));
		statsObj.fields.emplace_back("tracks laid", JSONValue(tracksLaid));
		statsObj.fields.emplace_back("rail laid", JSONValue(railLaid, 1));
		statsObj.fields.emplace_back("distance travelled", JSONValue(distanceTravelled, 0));
		statsObj.fields.emplace_back("chests opened", JSONValue(chestsOpened));
		statsObj.fields.emplace_back("explored cells", JSONValue(exploredCells));
		parentObject.fields.emplace_back("stats", JSONValue(statsObj));
	}

	void WorldStats::Parse(const JSONObject& parentObject)
	{
		*this = {};
		JSONObject statsObj;
		if (!parentObject.TryGetObject("stats", statsObj))
		{
			return;
		}
		statsObj.TryGetUInt("torches placed", torchesPlaced);
		statsObj.TryGetUInt("pieces built", piecesBuilt);
		statsObj.TryGetUInt("tracks laid", tracksLaid);
		statsObj.TryGetFloat("rail laid", railLaid);
		statsObj.TryGetFloat("distance travelled", distanceTravelled);
		statsObj.TryGetUInt("chests opened", chestsOpened);
		statsObj.TryGetUInt("explored cells", exploredCells);
	}

	Game::Game() :
		m_ActionCallback(this, &Game::OnActionEvent),
		m_SettingsFile("Game settings", SAVED_DIRECTORY "game_settings.json", 1)
	{
		m_SettingsFile.RegisterProperty("field of view", &m_Settings.fov).SetRange(50.0f, 110.0f);
		m_SettingsFile.RegisterProperty("mouse sensitivity", &m_Settings.mouseSensitivity).SetRange(0.1f, 4.0f);
		m_SettingsFile.RegisterProperty("invert mouse", &m_Settings.bInvertMouse);
		m_SettingsFile.RegisterProperty("master volume", &m_Settings.masterVolume).SetRange(0.0f, 1.0f);
		m_SettingsFile.RegisterProperty("music volume", &m_Settings.musicVolume).SetRange(0.0f, 1.0f);
		m_SettingsFile.RegisterProperty("show fps", &m_Settings.bShowFps);
		m_SettingsFile.RegisterProperty("terrain quality", &m_Settings.terrainQuality);
		m_SettingsFile.RegisterProperty("shadow quality", &m_Settings.shadowQuality);
		m_SettingsFile.RegisterProperty("draw distance", &m_Settings.drawDistance);
		m_SettingsFile.RegisterProperty("anti-aliasing", &m_Settings.antiAliasing);
		m_SettingsFile.RegisterProperty("ambient occlusion", &m_Settings.bAmbientOcclusion);
		m_SettingsFile.RegisterProperty("effects quality", &m_Settings.effectsQuality);
		m_SettingsFile.RegisterProperty("v-sync", &m_Settings.bVSync);
		m_SettingsFile.RegisterProperty("show minimap", &m_Settings.bShowMinimap);
		m_SettingsFile.RegisterProperty("language", &m_Settings.language);
		m_SettingsFile.RegisterProperty("key bindings", &m_Settings.keyBindings);
	}

	Game::~Game()
	{
	}

	void Game::Initialize()
	{
		if (!m_SettingsFile.Deserialize())
		{
			m_SettingsFile.Serialize();
		}
		LoadBindings();
		RegisterConsoleCommands();

		// Before the player controller, so the pause key opens the pause menu
		g_InputManager->BindActionCallback(&m_ActionCallback, 30);

		BaseScene* scene = g_SceneManager->CurrentScene();
		if (scene == nullptr || scene->GetFileName() != GAMEPLAY_SCENE_NAME)
		{
			g_SceneManager->SetCurrentScene(GAMEPLAY_SCENE_NAME);
			scene = g_SceneManager->CurrentScene();
		}

		m_SceneObjectIDs.clear();
		for (GameObject* rootObject : scene->GetRootObjects())
		{
			m_SceneObjectIDs.push_back(rootObject->ID);
		}

		g_EngineInstance->SetRenderImGui(false);
		// Light gizmos etc. would show over the menu's backdrop
		g_EngineInstance->SetRenderingEditorObjects(false);
		g_CameraManager->SetCameraByName("debug", false);
		ApplySettings();
		RefreshWorlds();
		SetScreen(Screen::MAIN_MENU);
		// FLEX_PLAY_DELAY=<seconds> waits on the main menu first (to test loading a world while the menu's terrain is shown)
		if (s_bAutoPlay && getenv("FLEX_PLAY_DELAY") == nullptr)
		{
			AutoPlay();
		}

		m_bTour = getenv("FLEX_GAME_TOUR") != nullptr;
		m_bBuildTest = getenv("FLEX_BUILD_TEST") != nullptr;
		m_bTour = m_bTour || m_bBuildTest;
		if (const char* overlay = getenv("FLEX_DEBUG_OVERLAY"))
		{
			g_Renderer->SetDebugOverlayID(atoi(overlay));
		}
	}

	void Game::BuildTourHut(const glm::vec3& base, real yaw)
	{
		const real grid = BuildPiece::GRID_SIZE;
		const glm::quat facing = glm::angleAxis(yaw, VEC3_UP);
		m_BuildTestBase = base;
		m_BuildTestYaw = yaw;
		auto Place = [&](BuildPieceType type, BuildMaterial material, const glm::vec3& offset, real extraYaw, real length)
		{
			BaseScene* scene = g_SceneManager->CurrentScene();
			BuildPiece* piece = new BuildPiece(scene->GetUniqueObjectName(std::string(BuildPieceTypeName(type)) + "_", 3));
			piece->Setup(type, material, length);
			piece->GetTransform()->SetWorldPosition(base + facing * offset);
			piece->GetTransform()->SetWorldRotation(facing * glm::angleAxis(extraYaw, VEC3_UP));
			scene->AddRootObject(piece);
		};
		Place(BuildPieceType::FLOOR, BuildMaterial::STONE, glm::vec3(0.0f), 0.0f, 0.0f);
		Place(BuildPieceType::WALL, BuildMaterial::WOOD, glm::vec3(0.0f), 0.0f, 0.0f);
		Place(BuildPieceType::WALL, BuildMaterial::WOOD, glm::vec3(0.0f), PI_DIV_TWO, 0.0f);
		Place(BuildPieceType::WALL, BuildMaterial::WOOD, glm::vec3(0.0f), -PI_DIV_TWO, 0.0f);
		Place(BuildPieceType::ROOF, BuildMaterial::WOOD, glm::vec3(0.0f, grid, 0.0f), PI, 0.0f);
		Place(BuildPieceType::STAIRS, BuildMaterial::STONE, glm::vec3(grid, 0.0f, 0.0f), 0.0f, 0.0f);
		Place(BuildPieceType::TORCH, BuildMaterial::WOOD, glm::vec3(0.0f, 0.2f, 0.0f), 0.0f, 0.0f);
		Place(BuildPieceType::TORCH, BuildMaterial::WOOD, glm::vec3(-grid * 0.6f, 0.0f, -grid * 0.6f), 0.0f, 0.0f);
		Place(BuildPieceType::LADDER, BuildMaterial::WOOD, glm::vec3(-grid * 0.5f - 0.12f, 0.0f, 0.5f), PI_DIV_TWO, grid * 1.5f);
		Place(BuildPieceType::ROPE, BuildMaterial::WOOD, glm::vec3(1.0f, grid * 2.0f, -grid * 0.6f), 0.0f, grid * 2.0f);
	}

	void Game::UpdateBuildTest()
	{
		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (player == nullptr || terrainGenerator == nullptr)
		{
			return;
		}
		auto Screenshot = [](const std::string& name)
		{
			const std::string directory = RelativePathToAbsolute(SCREENSHOT_DIRECTORY);
			Platform::CreateDirectoryRecursive(directory);
			g_Renderer->RequestScreenshot(directory + "build_" + name + ".png");
		};
		auto Next = [this]() { ++m_BuildTestStep; m_TourTime = 0.0f; };
		// Views are from the (free) debug camera, so hills can't get in the way. Build mode aims with the current camera.
		auto SetCamera = [player](const glm::vec3& position, const glm::vec3& target, bool bPossessPlayer)
		{
			g_CameraManager->SetCameraByName("debug", false);
			BaseCamera* camera = g_CameraManager->CurrentCamera();
			// Build mode needs a possessed player (test only, the debug camera isn't used in game)
			if (camera->bPossessPlayer != bPossessPlayer)
			{
				camera->bPossessPlayer = bPossessPlayer;
				player->UpdateIsPossessed();
			}
			camera->position = position;
			camera->forward = glm::normalize(target - position);
			camera->CalculateYawAndPitchFromForward();
			camera->CalculateAxisVectorsFromPitchAndYaw();
			camera->RecalculateViewProjection();
		};

		const real grid = BuildPiece::GRID_SIZE;
		const glm::quat facing = glm::angleAxis(m_BuildTestYaw, VEC3_UP);
		const glm::vec3 hutCenter = m_BuildTestBase + VEC3_UP * grid * 0.6f;
		if (m_BuildTestStep == 0)
		{
			// The flattest spot nearby (above the sea) to build the hut on, so it can be seen from all sides
			glm::vec3 start = player->GetTransform()->GetWorldPosition();
			// (FLEX_TEST_WORLD_POS=<x>,<z> builds there instead, e.g. far from the world origin)
			real worldX, worldZ;
			if (getenv("FLEX_TEST_WORLD_POS") != nullptr && sscanf(getenv("FLEX_TEST_WORLD_POS"), "%f,%f", &worldX, &worldZ) == 2)
			{
				start = glm::vec3(worldX, terrainGenerator->SampleColumn(worldX, worldZ).height, worldZ);
			}
			real bestBumpiness = FLT_MAX;
			glm::vec3 base = start;
			for (i32 i = 0; i < 160; ++i)
			{
				const real angle = (real)i * 2.39996f;
				const glm::vec3 candidate = start + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * (glm::sqrt((real)i) * 25.0f);
				real minY = FLT_MAX, maxY = -FLT_MAX, centerY = 0.0f;
				bool bAllHit = true;
				for (i32 s = 0; s < 25 && bAllHit; ++s)
				{
					const glm::vec3 offset((real)(s % 5 - 2) * grid * 1.5f, 0.0f, (real)(s / 5 - 2) * grid * 1.5f);
					glm::vec3 hitPos, hitNormal;
					bAllHit = terrainGenerator->Raycast(candidate + offset + VEC3_UP * 300.0f, -VEC3_UP, 600.0f, hitPos, hitNormal) && hitPos.y > 3.0f;
					minY = glm::min(minY, hitPos.y);
					maxY = glm::max(maxY, hitPos.y);
					centerY = s == 12 ? hitPos.y : centerY;
				}
				if (bAllHit && maxY - minY < bestBumpiness)
				{
					bestBumpiness = maxY - minY;
					base = glm::vec3(candidate.x, centerY, candidate.z);
				}
			}
			Print("Build test: hut at %.1f, %.1f, %.1f (ground height range %.2fm)\n", base.x, base.y, base.z, bestBumpiness);
			m_BuildTestBase = base;
			player->TeleportOnTerrain(base + glm::vec3(0.0f, 2.0f, -grid * 3.0f));
			Next();
			return;
		}
		if (m_BuildTestStep == 1)
		{
			if (m_TourTime > 4.0f && !player->IsAwaitingTerrainCollision())
			{
				glm::vec3 hitPos, hitNormal;
				glm::vec3 base = m_BuildTestBase;
				if (terrainGenerator->Raycast(base + VEC3_UP * 30.0f, -VEC3_UP, 60.0f, hitPos, hitNormal))
				{
					base = hitPos;
				}
				BuildTourHut(base, 0.0f);
				Next();
			}
			return;
		}

		// Views around the hut (the last from inside), then placement previews in the cell beside it
		const i32 firstStep = 2;
		const i32 step = m_BuildTestStep - firstStep;
		const i32 viewCount = 5;
		const BuildPieceType previewTypes[] = { BuildPieceType::FLOOR, BuildPieceType::WALL, BuildPieceType::STAIRS, BuildPieceType::ROOF };
		const i32 previewCount = (i32)ARRAY_LENGTH(previewTypes);
		if (step < viewCount)
		{
			glm::vec3 viewPos;
			if (step < 4)
			{
				viewPos = hutCenter + facing * (glm::angleAxis(PI_DIV_TWO * (real)step + 0.6f, VEC3_UP) * glm::vec3(0.0f, grid * 1.2f, -grid * 3.0f));
			}
			else
			{
				viewPos = m_BuildTestBase + facing * glm::vec3(grid * 0.3f, grid * 0.45f, -grid * 0.3f);
			}
			SetCamera(viewPos, step < 4 ? hutCenter : m_BuildTestBase + facing * glm::vec3(-grid * 0.5f, grid * 0.3f, grid * 0.5f), false);
			if (m_TourTime > 1.5f)
			{
				Screenshot("view_" + IntToString(step));
				Next();
			}
		}
		else if (step < viewCount + previewCount)
		{
			const i32 previewIndex = step - viewCount;
			// The ground in the cell in front of the hut
			glm::vec3 target = m_BuildTestBase + facing * glm::vec3(0.0f, 0.0f, -grid);
			glm::vec3 hitPos, hitNormal;
			if (terrainGenerator->Raycast(target + VEC3_UP * 30.0f, -VEC3_UP, 60.0f, hitPos, hitNormal))
			{
				target = hitPos;
			}
			SetCamera(target + facing * glm::vec3(grid * 0.6f, grid * 1.3f, -grid * 1.8f), target + facing * glm::vec3(0.0f, 0.0f, grid * 0.15f), true);
			player->GetBuildMode().SetActive(true);
			player->GetBuildMode().SetSelectedType(previewTypes[previewIndex]);
			if (m_TourTime > 1.5f)
			{
				Screenshot(std::string("preview_") + BuildPieceTypeName(previewTypes[previewIndex]));
				Next();
			}
		}
		else if (m_TourTime > 1.0f)
		{
			player->GetBuildMode().SetActive(false);
			g_CameraManager->CurrentCamera()->bPossessPlayer = false;
			g_EngineInstance->Stop();
			m_bBuildTest = false;
			m_bTour = false;
		}
	}

	void Game::UpdateTour()
	{
		m_TourTime += g_UnpausedDeltaTime;
		auto Screenshot = [](const char* name)
		{
			const std::string directory = RelativePathToAbsolute(SCREENSHOT_DIRECTORY);
			Platform::CreateDirectoryRecursive(directory);
			g_Renderer->RequestScreenshot(directory + "tour_" + name + ".png");
		};
		auto Next = [this]() { ++m_TourStep; m_TourTime = 0.0f; };

		if (m_bBuildTest && m_TourStep >= 8)
		{
			UpdateBuildTest();
			return;
		}

		switch (m_TourStep)
		{
		case 0:
		{
			// FLEX_TOUR_RESIZE=<w>x<h> resizes the window at runtime first, to check the UI follows size changes
			static bool bResized = false;
			const char* resize = getenv("FLEX_TOUR_RESIZE");
			i32 w = 0, h = 0;
			if (!bResized && resize != nullptr && m_TourTime > 2.0f && sscanf(resize, "%dx%d", &w, &h) == 2 && w > 0 && h > 0)
			{
				g_Window->SetSize(w, h);
				bResized = true;
			}
			// FLEX_TOUR_WINDOW=1: resizes, minimizes, restores, enters & leaves fullscreen & maximizes (screenshotting the
			// menu after each), logging "WINDOW TEST" lines & checking the swap chain follows the window size
			static i32 s_WindowStage = 0;
			static sec s_WindowStageTime = 0.0f;
			static bool bWindowTest = getenv("FLEX_TOUR_WINDOW") != nullptr;
			if (bWindowTest && s_WindowStage < 8)
			{
				s_WindowStageTime += g_UnpausedDeltaTime;
				auto Log = [](const char* what)
				{
					const glm::vec2 size = g_Window->GetSize();
					Print("WINDOW TEST: %s -> %dx%d iconified=%d maximized=%d mode=%s\n", what, (i32)size.x, (i32)size.y,
						(i32)g_Window->IsIconified(), (i32)g_Window->IsMaximized(), g_Window->WindowModeToStr(g_Window->GetWindowMode()));
				};
				auto StageNext = [&]() { ++s_WindowStage; s_WindowStageTime = 0.0f; };
				if (m_TourTime > 2.0f && s_WindowStageTime > 1.0f)
				{
					switch (s_WindowStage)
					{
					case 0: g_Window->SetSize(1000, 600); Log("resized"); StageNext(); break;
					case 1: Screenshot("window_resized"); g_Window->Iconify(); StageNext(); break;
					case 2: Log("minimized"); g_Window->Restore(); StageNext(); break;
					case 3: Log("restored"); g_Window->SetWindowMode(WindowMode::FULLSCREEN); StageNext(); break;
					case 4: Log("fullscreen"); Screenshot("window_fullscreen"); StageNext(); break;
					case 5: g_Window->ToggleFullscreen(); StageNext(); break;
					case 6: Log("left fullscreen"); Screenshot("window_windowed"); g_Window->Maximize(); StageNext(); break;
					case 7: Log("maximized"); Screenshot("window_maximized"); StageNext(); break;
					}
				}
				return;
			}
			if (m_TourTime > 8.0f) { Screenshot("main_menu"); Next(); }
		} break;
		case 1: if (m_TourTime > 0.5f) { m_ScreenBeforeSettings = Screen::MAIN_MENU; SetScreen(Screen::SETTINGS); Next(); } break;
		case 2: if (m_TourTime > 1.0f) { Screenshot("settings"); Next(); } break;
		case 3: if (m_TourTime > 0.5f) { m_NewWorldName = "Tour world"; m_NewWorldSeed = "4242"; SetScreen(Screen::NEW_WORLD); Next(); } break;
		case 4: if (m_TourTime > 1.0f) { Screenshot("new_world"); Next(); } break;
		case 5: if (m_TourTime > 0.5f) { CreateWorld(m_NewWorldName, 4242); Next(); } break;
		case 6: if (m_TourTime > 0.4f) { m_TourWorldDirectory = m_CurrentWorld.directory; Screenshot("loading"); Next(); } break;
		case 7:
		{
			// FLEX_TOUR_CONSOLE=1: runs some console commands, & screenshots the console with a half typed command
			static i32 s_ConsoleStage = 0;
			GameConsole& console = GameConsole::Get();
			if (getenv("FLEX_TOUR_CONSOLE") != nullptr && s_ConsoleStage == 0 && m_Screen == Screen::PLAYING && m_TourTime > 6.0f)
			{
				SetConsoleOpen(true);
				for (const char* line : { "tod 12:20", "tp 10,80,20", "time", "grant wood", "grnat wood 4", "bogus", "fps on" })
				{
					console.Log(std::string("> ") + line);
					const std::string output = console.Execute(line);
					console.Log(output[0] == '!' ? output.substr(1) : output, output[0] == '!');
				}
				console.SetInput("gra");
				s_ConsoleStage = 1;
				m_TourTime = 0.0f;
			}
			else if (s_ConsoleStage == 1 && m_TourTime > 1.0f)
			{
				Screenshot("console");
				console.SetInput("tp 5,");
				s_ConsoleStage = 2;
				m_TourTime = 0.0f;
			}
			else if (s_ConsoleStage == 2 && m_TourTime > 1.0f)
			{
				Screenshot("console_args");
				SetConsoleOpen(false);
				s_ConsoleStage = 3;
				m_TourTime = 6.1f;
			}
			else if (s_ConsoleStage != 1 && s_ConsoleStage != 2 && m_Screen == Screen::PLAYING && m_TourTime > 6.0f)
			{
				Screenshot("playing");
				Next();
			}
		} break;
		case 8:
		{
			// A little hut in front of the player, with a torch inside, a ladder up the side & a rope off the roof
			Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
			if (player != nullptr && m_TourTime > 0.5f)
			{
				const glm::vec3 look = player->GetLookDirection();
				const glm::vec3 heading = glm::normalize(glm::vec3(look.x, 0.0f, look.z) + glm::vec3(1e-4f, 0.0f, 0.0f));
				const glm::vec3 base = player->GetTransform()->GetWorldPosition() - VEC3_UP * (player->GetHeight() * 0.5f) + heading * (BuildPiece::GRID_SIZE * 3.0f);
				BuildTourHut(base, std::atan2(heading.x, heading.z));
				Next();
			}
		} break;
		case 9: if (m_TourTime > 2.0f) { Screenshot("building"); Next(); } break;
		case 10:
		{
			// Night: the moon, stars & torch light
			BaseScene* scene = g_SceneManager->CurrentScene();
			scene->SetTimeOfDay(0.47f);
			scene->SetTimeOfDayPaused(true);
			if (m_TourTime > 3.0f)
			{
				Screenshot("night");
				Next();
			}
		} break;
		case 11:
		{
			// Looking up at the night sky
			Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
			if (player != nullptr)
			{
				player->SetPitch(glm::radians(-50.0f));
			}
			if (m_TourTime > 1.0f)
			{
				Screenshot("night_sky");
				Next();
			}
		} break;
		case 12:
		{
			// (a frame after the screenshot was requested, so it's still of the night)
			Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
			if (player != nullptr)
			{
				player->SetPitch(0.0f);
			}
			g_SceneManager->CurrentScene()->SetTimeOfDay(0.08f);
			g_SceneManager->CurrentScene()->SetTimeOfDayPaused(false);
			SetScreen(Screen::PAUSED);
			Next();
		} break;
		case 13: if (m_TourTime > 1.0f) { Screenshot("pause"); Next(); } break;
		case 14:
		{
			// The controls screen (from the pause menu), then the map. A screenshot is of the frame drawn after this
			// update, so the screen changes a moment after it's requested.
			static bool s_bControlsCaptured = false;
			if (!s_bControlsCaptured && m_Screen == Screen::PAUSED && m_TourTime > 0.5f)
			{
				m_ScreenBeforeSettings = Screen::PAUSED;
				SetScreen(Screen::CONTROLS);
				m_TourTime = 0.0f;
			}
			else if (!s_bControlsCaptured && m_Screen == Screen::CONTROLS && m_TourTime > 1.0f)
			{
				Screenshot("controls");
				s_bControlsCaptured = true;
				m_TourTime = 0.0f;
			}
			else if (s_bControlsCaptured && getenv("FLEX_UI_TEST") != nullptr && m_Screen == Screen::CONTROLS && TourRebindTest())
			{
				m_TourTime = 0.0f;
			}
			else if (s_bControlsCaptured && m_TourTime > 0.25f)
			{
				SetScreen(Screen::MAP);
				Next();
			}
		} break;
		case 15:
		{
			TextureID textureID;
			glm::vec3 center;
			real extent;
			TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
			const bool bMapReady = (terrainGenerator != nullptr && terrainGenerator->GetLargeMap(textureID, center, extent) && m_TourTime > 1.0f) || m_TourTime > 20.0f;
			if (m_bMapFollowPlayer && !m_bTourMapCaptured && bMapReady && getenv("FLEX_UI_TEST") != nullptr && !m_bTourWaypointsPlaced)
			{
				// Waypoints around the player, & the mouse over the first
				m_bTourWaypointsPlaced = true;
				const glm::vec3 pos = g_SceneManager->CurrentScene()->GetPlayer(0)->GetTransform()->GetWorldPosition();
				ToggleWaypoint(glm::vec2(pos.x + 400.0f, pos.z), 1.0f);
				ToggleWaypoint(glm::vec2(pos.x - 300.0f, pos.z + 500.0f), 1.0f);
				ToggleWaypoint(glm::vec2(pos.x - 900.0f, pos.z - 700.0f), 1.0f);
				const real mapSize = glm::min(m_UI.Width(), m_UI.Height()) * 0.78f;
				const glm::vec2 mapCenter(m_UI.Width() * 0.5f, m_UI.Height() * 0.5f + 10.0f * m_UI.Scale());
				{ const glm::vec2 mousePos = mapCenter + glm::vec2(400.0f / (2.0f * m_MapExtent) * mapSize, 0.0f); g_InputManager->CursorPosCallback(mousePos.x, mousePos.y); }
				m_TourTime = 0.0f;
			}
			else if (m_bMapFollowPlayer && !m_bTourMapCaptured && bMapReady && (getenv("FLEX_UI_TEST") == nullptr || m_TourTime > 0.5f))
			{
				// The screenshot is of the frame rendered after this update, so the pan below waits until later
				Screenshot("map");
				m_bTourMapCaptured = true;
				m_TourTime = 0.0f;
			}
			else if (m_bMapFollowPlayer && m_bTourMapCaptured && m_TourTime > 0.25f)
			{
				// Then pan (as if dragged with the middle mouse button) so the player is off the view's left edge
				m_bMapFollowPlayer = false;
				m_MapViewCenter.x += m_MapExtent * 1.3f;
				m_TourTime = 0.0f;
			}
			else if (!m_bMapFollowPlayer && m_TourTime > 1.5f)
			{
				Screenshot("map_panned");
				Next();
			}
		} break;
		case 16:
		{
			// Into a cave (with a torch), to check caves are dark
			SetScreen(Screen::PLAYING);
			Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
			TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
			if (player == nullptr || terrainGenerator == nullptr)
			{
				Next();
				break;
			}
			const glm::vec3 origin = player->GetTransform()->GetWorldPosition();
			bool bFound = false;
			glm::vec3 floor(0.0f);
			for (i32 ring = 0; ring < 12 && !bFound; ++ring)
			{
				for (i32 i = 0; i < 8 && !bFound; ++i)
				{
					const real angle = TWO_PI * (real)i / 8.0f + (real)ring;
					const real x = origin.x + std::cos(angle) * (real)ring * 40.0f;
					const real z = origin.z + std::sin(angle) * (real)ring * 40.0f;
					const real surface = terrainGenerator->SampleColumn(x, z).height;
					for (real y = surface - 25.0f; y > surface - 160.0f && !bFound; y -= 2.0f)
					{
						const glm::vec3 p(x, y, z);
						if (terrainGenerator->SampleDensity(p) > 2.0f && terrainGenerator->SampleDensity(p - VEC3_UP * 3.0f) < 0.0f &&
							terrainGenerator->SampleDensity(p + VEC3_UP * 2.0f) > 1.0f)
						{
							floor = p;
							bFound = true;
						}
					}
				}
			}
			if (!bFound)
			{
				Print("Tour: no cave found nearby\n");
				Next();
				Next();
				break;
			}
			player->TeleportOnTerrain(floor + VEC3_UP * 1.5f);
			m_TourCavePos = floor;
			Next();
		} break;
		case 17:
		{
			Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
			if (m_TourTime > 1.0f && player != nullptr && !player->IsAwaitingTerrainCollision())
			{
				// A torch a little way ahead
				const glm::vec3 look = player->GetLookDirection();
				const glm::vec3 heading = glm::normalize(glm::vec3(look.x, 0.0f, look.z) + glm::vec3(1e-4f, 0.0f, 0.0f));
				glm::vec3 hitPos, hitNormal;
				TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
				if (terrainGenerator->Raycast(player->GetEyePosition() + heading * 4.0f, glm::vec3(0.0f, -1.0f, 0.0f), 20.0f, hitPos, hitNormal))
				{
					BaseScene* scene = g_SceneManager->CurrentScene();
					BuildPiece* torch = new BuildPiece(scene->GetUniqueObjectName("Torch_", 3));
					torch->Setup(BuildPieceType::TORCH, BuildMaterial::WOOD, 0.0f);
					torch->GetTransform()->SetWorldPosition(hitPos);
					scene->AddRootObject(torch);
				}
				player->SetPitch(glm::radians(-15.0f));
				Next();
			}
			else if (m_TourTime > 20.0f)
			{
				Next();
			}
		} break;
		case 18: if (m_TourTime > 3.0f) { Screenshot("cave"); Next(); } break;
		case 19: if (m_TourTime > 0.5f) { LeaveWorld(); m_SelectedWorld = 0; SetScreen(Screen::WORLDS); Next(); } break;
		case 20: if (m_TourTime > 1.5f) { Screenshot("worlds"); Print("Tour: a new world would be named \"%s\"\n", NextWorldName().c_str()); Next(); } break;
		case 21:
		{
			if (m_TourTime > 1.0f)
			{
				// The tour's world isn't kept
				for (const WorldInfo& world : m_Worlds)
				{
					if (!m_TourWorldDirectory.empty() && world.directory == m_TourWorldDirectory)
					{
						DeleteWorld(world);
						break;
					}
				}
				g_EngineInstance->Stop();
				Next();
			}
		} break;
		default: break;
		}
	}

	void Game::Destroy()
	{
		SaveWorld();
		save::WaitForPendingWrites();
		g_InputManager->UnbindActionCallback(&m_ActionCallback);
	}

	void Game::SetScreen(Screen screen)
	{
		m_Screen = screen;
		m_ScreenTime = 0.0f;
		m_UI.ClearFocus();
		m_bConfirmDelete = false;

		switch (screen)
		{
		case Screen::PLAYING:
			g_EngineInstance->SetSimulationPaused(false);
			g_Window->SetCursorMode(CursorMode::DISABLED);
			break;
		case Screen::PAUSED:
		case Screen::MAP:
		case Screen::CONSOLE:
			g_EngineInstance->SetSimulationPaused(true);
			g_Window->SetCursorMode(CursorMode::NORMAL);
			// The map opens centered on the player
			m_bMapFollowPlayer = true;
			m_bMapPanning = false;
			break;
		case Screen::SETTINGS:
		case Screen::CONTROLS:
			// (Stays paused if opened from the pause menu)
			g_Window->SetCursorMode(CursorMode::NORMAL);
			break;
		case Screen::NONE:
			g_EngineInstance->SetSimulationPaused(false);
			g_Window->SetCursorMode(CursorMode::NORMAL);
			break;
		default:
			// Menus (the world keeps going in the background)
			g_EngineInstance->SetSimulationPaused(false);
			g_Window->SetCursorMode(CursorMode::NORMAL);
			break;
		}
	}

	EventReply Game::OnActionEvent(Action action, ActionEvent actionEvent)
	{
		if (m_Screen == Screen::CONTROLS && m_RebindIndex >= 0 && action == Action::PAUSE)
		{
			// Waiting for a key on the Controls screen: whatever pause is bound to is a candidate (Esc cancels, see DrawControls)
			return EventReply::CONSUMED;
		}
		if (action == Action::PAUSE && GameConsole::Get().IsOpen())
		{
			// Closes the console (only on the press: the release shouldn't also pause)
			if (actionEvent == ActionEvent::ACTION_TRIGGER)
			{
				SetConsoleOpen(false);
			}
			return EventReply::CONSUMED;
		}
		if (m_Screen == Screen::NONE || action != Action::PAUSE || actionEvent != ActionEvent::ACTION_TRIGGER)
		{
			return EventReply::UNCONSUMED;
		}

		switch (m_Screen)
		{
		case Screen::PLAYING:
			SetScreen(Screen::PAUSED);
			break;
		case Screen::PAUSED:
		case Screen::MAP:
			SetScreen(Screen::PLAYING);
			break;
		case Screen::SETTINGS:
		case Screen::CONTROLS:
			m_SettingsFile.Serialize();
			SetScreen(m_ScreenBeforeSettings);
			if (m_ScreenBeforeSettings == Screen::PAUSED)
			{
				g_EngineInstance->SetSimulationPaused(true);
			}
			break;
		case Screen::WORLDS:
		case Screen::NEW_WORLD:
			SetScreen(Screen::MAIN_MENU);
			break;
		default:
			break;
		}
		return EventReply::CONSUMED;
	}

	void Game::LateUpdate()
	{
		if (m_bWorldLoaded && g_SceneManager->HasSceneLoaded())
		{
			if (Player* player = g_SceneManager->CurrentScene()->GetPlayer(0))
			{
				player->LateUpdateHints();
			}
		}

		if (m_Screen == Screen::NONE || m_bWorldLoaded)
		{
			return;
		}

		// The menu's backdrop: a slow flight around the world's origin
		BaseCamera* camera = g_CameraManager->CurrentCamera();
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (camera == nullptr || camera->bPossessPlayer || terrainGenerator == nullptr)
		{
			return;
		}

		m_MenuCameraAngle += g_UnpausedDeltaTime * 0.012f;
		const real radius = 900.0f;
		const glm::vec3 target(0.0f, glm::max(terrainGenerator->SampleHeight(glm::vec2(0.0f)), 0.0f) + 60.0f, 0.0f);
		glm::vec3 position = target + glm::vec3(std::cos(m_MenuCameraAngle) * radius, 0.0f, std::sin(m_MenuCameraAngle) * radius);
		position.y = glm::max(terrainGenerator->SampleHeight(glm::vec2(position.x, position.z)), 0.0f) + 160.0f;

		// Fully overrides the (debug) camera's own update: it reacts to mouse drags (e.g. on a slider) by turning &
		// rolling, & its right & up vectors would keep that roll unless they're recalculated here
		camera->position = position;
		camera->forward = glm::normalize(target - position);
		camera->CalculateYawAndPitchFromForward();
		camera->CalculateAxisVectorsFromPitchAndYaw();
		camera->RecalculateViewProjection();
	}

	void Game::Update()
	{
		BaseScene* scene = g_SceneManager->CurrentScene();
		const bool bGameplayScene = scene != nullptr && scene->GetFileName() == GAMEPLAY_SCENE_NAME;
		if (!bGameplayScene && m_Screen != Screen::NONE)
		{
			// Another scene was opened from the editor
			m_bWorldLoaded = false;
			m_Screen = Screen::NONE;
		}
		if (m_Screen == Screen::NONE)
		{
			return;
		}

		m_UI.BeginFrame();
		m_ScreenTime += g_UnpausedDeltaTime;
		s_PlayBenchmark.Watchdog();
		if (m_bTour)
		{
			UpdateTour();
		}
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (m_Screen == Screen::MAIN_MENU && !m_bWorldLoaded)
		{
			if (s_bAutoPlay && m_ScreenTime > (real)atof(getenv("FLEX_PLAY_DELAY") != nullptr ? getenv("FLEX_PLAY_DELAY") : "0"))
			{
				AutoPlay();
			}
			StartupTimer::Milestone("Main menu shown");
			if (terrainGenerator == nullptr || terrainGenerator->IsLoadComplete())
			{
				StartupTimer::Milestone("Main menu backdrop terrain loaded");
				const char* exitAt = getenv("FLEX_STARTUP_EXIT");
				if (exitAt != nullptr && strcmp(exitAt, "menu") == 0)
				{
					g_EngineInstance->Stop();
				}
			}
		}
		if (terrainGenerator != nullptr)
		{
			terrainGenerator->UpdateLargeMap();
		}
		*TerrainGenerator::GetShowMinimapPtr() = m_Settings.bShowMinimap;

		// The console (tilde) opens over any screen; while playing it pauses the game (so typing doesn't move the player)
		GameConsole& console = GameConsole::Get();
		if (g_InputManager->GetKeyPressed(GameKeys::CONSOLE, true) && m_Screen != Screen::LOADING)
		{
			SetConsoleOpen(!console.IsOpen());
		}

		switch (m_Screen)
		{
		case Screen::CONSOLE: break;
		case Screen::MAIN_MENU: DrawMainMenu(); break;
		case Screen::WORLDS: DrawWorlds(); break;
		case Screen::NEW_WORLD: DrawNewWorld(); break;
		case Screen::SETTINGS: DrawSettings(); break;
		case Screen::CONTROLS: DrawControls(); break;
		case Screen::LOADING: DrawLoading(); break;
		case Screen::PAUSED: DrawPauseMenu(); break;
		case Screen::MAP: DrawMap(); break;
		case Screen::PLAYING:
		{
			Animals::Update();
			Animals::DrawHUD(m_UI);
			// Adding each frame's dt directly to the (float) total loses precision once it's large (after ~3 days of play
			// it would stop increasing), so whole seconds are moved over instead (exact for ~190 days)
			m_PlayTimeRemainder += g_DeltaTime;
			if (m_PlayTimeRemainder >= 1.0f)
			{
				const real wholeSeconds = glm::floor(m_PlayTimeRemainder);
				m_CurrentWorld.playTime += wholeSeconds;
				m_PlayTimeRemainder -= wholeSeconds;
			}
			UpdateExplored();
			m_AutosaveTimer += g_UnpausedDeltaTime;
			if (m_AutosaveTimer > AUTOSAVE_INTERVAL)
			{
				SaveWorld();
			}
			// For testing world loading: FLEX_PLAY_SCREENSHOT=<seconds> saves play_<world>.png once the world has been playable
			// that long, then quits
			static i32 s_PlayScreenshotState = 0;
			if (const char* screenshotDelay = getenv("FLEX_PLAY_SCREENSHOT"))
			{
				if (s_PlayScreenshotState == 0 && m_ScreenTime > (real)atof(screenshotDelay))
				{
					const std::string directory = RelativePathToAbsolute(SCREENSHOT_DIRECTORY);
					Platform::CreateDirectoryRecursive(directory);
					g_Renderer->RequestScreenshot(directory + "play_" + SanitizeName(m_CurrentWorld.name) + ".png");
					s_PlayScreenshotState = 1;
				}
				else if (s_PlayScreenshotState > 0 && ++s_PlayScreenshotState > 5)
				{
					g_EngineInstance->Stop();
				}
			}
			Player* player = scene->GetPlayer(0);
			if (getenv("FLEX_PLAY_BENCHMARK") != nullptr || getenv("FLEX_HITCH_LOG") != nullptr)
			{
				s_PlayBenchmark.Update(player);
			}
			if (g_InputManager->GetKeyPressed(GameKeys::MAP) && player != nullptr && !player->IsAnyInventoryShowing())
			{
				SetScreen(Screen::MAP);
			}
			else if (g_InputManager->GetKeyPressed(GameKeys::WAYPOINT) && player != nullptr && !player->IsAnyInventoryShowing())
			{
				const glm::vec3 playerPos = player->GetTransform()->GetWorldPosition();
				ToggleWaypoint(glm::vec2(playerPos.x, playerPos.z), 20.0f);
			}
		} break;
		default:
			break;
		}

		console.Draw(m_UI);
		if (m_Settings.bShowFps)
		{
			// Smoothed over about half a second
			static real s_SmoothedDelta = 0.016f;
			s_SmoothedDelta = glm::mix(s_SmoothedDelta, glm::clamp((real)g_UnpausedDeltaTime, 0.0001f, 1.0f), 0.05f);
			char fpsText[64];
			snprintf(fpsText, ARRAY_LENGTH(fpsText), "%.0f fps  %.1f ms", 1.0f / s_SmoothedDelta, s_SmoothedDelta * 1000.0f);
			const real s = m_UI.Scale();
			m_UI.Text(fpsText, m_UI.Width() - 24.0f * s, 24.0f * s, UITheme::TEXT_SMALL * s, UITheme::WithAlpha(UITheme::TEXT_PRIMARY, 0.85f), TextAlign::RIGHT);
		}
	}

	void Game::SetConsoleOpen(bool bOpen)
	{
		GameConsole& console = GameConsole::Get();
		if (console.IsOpen() == bOpen)
		{
			return;
		}
		console.SetOpen(bOpen);
		if (bOpen && m_Screen == Screen::PLAYING)
		{
			SetScreen(Screen::CONSOLE);
		}
		else if (!bOpen && m_Screen == Screen::CONSOLE)
		{
			SetScreen(Screen::PLAYING);
		}
	}

	void Game::RegisterConsoleCommands()
	{
		GameConsole& console = GameConsole::Get();

		console.Register({ "grant", "give" }, "<item> [count=1]", "Gives items to the player",
			[](const std::vector<std::string>& args) -> std::string
			{
				const i32 count = glm::max(ParseInt(args[1]), 1);
				std::string message;
				if (GameConsole::Get().Grant(args[0], count, message))
				{
					return message;
				}
				const std::vector<std::string> names = g_ResourceManager->GetPrefabNames();
				const i32 index = GameConsole::BestMatch(args[0], names, true);
				Player* player = g_SceneManager->HasSceneLoaded() ? g_SceneManager->CurrentScene()->GetPlayer(0) : nullptr;
				if (index < 0 || player == nullptr)
				{
					return std::string("!") + LOCF(Console_NoItem, args[0].c_str());
				}
				const PrefabID prefabID = g_ResourceManager->GetPrefabID(names[index].c_str());
				player->AddToInventory(prefabID, count);
				return LOCF(Console_Granted, count, names[index].c_str());
			},
			[](u32 argIndex) -> std::vector<std::string>
			{
				std::vector<std::string> names;
				if (argIndex == 0)
				{
					names = GameConsole::Get().GetGrantableNames();
					for (const std::string& name : g_ResourceManager->GetPrefabNames()) { names.push_back(name); }
				}
				return names;
			});

		console.Register({ "teleport", "tp" }, "<x> <y> <z>", "Teleports the player (meters)",
			[](const std::vector<std::string>& args) -> std::string
			{
				Player* player = g_SceneManager->HasSceneLoaded() ? g_SceneManager->CurrentScene()->GetPlayer(0) : nullptr;
				if (player == nullptr)
				{
					return std::string("!") + LOC(Console_NoPlayer);
				}
				const glm::vec3 pos(ParseFloat(args[0]), ParseFloat(args[1]), ParseFloat(args[2]));
				player->TeleportOnTerrain(pos);
				return LOCF(Console_Teleported, pos.x, pos.y, pos.z);
			});

		console.Register({ "tod", "time", "settime" }, "[time]", "Sets the time of day (e.g. 12:20, 6, midnight), or shows it",
			[](const std::vector<std::string>& args) -> std::string
			{
				if (!g_SceneManager->HasSceneLoaded())
				{
					return std::string("!") + LOC(Console_NoPlayer);
				}
				BaseScene* scene = g_SceneManager->CurrentScene();
				// 0 is noon, 0.5 midnight
				auto ToHours = [](real t) { return glm::mod(t * 24.0f + 12.0f, 24.0f); };
				if (!args[0].empty())
				{
					real hours = 0.0f;
					const std::string arg = args[0];
					if (arg == "noon" || arg == "midday") { hours = 12.0f; }
					else if (arg == "midnight") { hours = 0.0f; }
					else if (arg == "dawn" || arg == "morning" || arg == "sunrise") { hours = 6.0f; }
					else if (arg == "dusk" || arg == "evening" || arg == "sunset") { hours = 18.0f; }
					else
					{
						const size_t colon = arg.find(':');
						hours = ParseFloat(colon == std::string::npos ? arg : arg.substr(0, colon));
						if (colon != std::string::npos)
						{
							hours += ParseFloat(arg.substr(colon + 1)) / 60.0f;
						}
						if (hours < 0.0f || hours >= 24.0f)
						{
							return std::string("!") + LOC(Console_BadTime);
						}
					}
					scene->SetTimeOfDay(glm::mod((hours - 12.0f) / 24.0f + 1.0f, 1.0f));
				}
				const real hours = ToHours(scene->GetTimeOfDay());
				return LOCF(Console_TimeIs, (i32)hours, (i32)((hours - glm::floor(hours)) * 60.0f));
			});

		console.Register({ "fov" }, "[degrees]", "Sets (or shows) the field of view",
			[this](const std::vector<std::string>& args) -> std::string
			{
				if (!args[0].empty())
				{
					m_Settings.fov = glm::clamp(ParseFloat(args[0]), 50.0f, 110.0f);
					ApplySettings();
					m_SettingsFile.Serialize();
				}
				return LOCF(Console_FovIs, m_Settings.fov);
			});

		console.Register({ "fps", "showfps" }, "[on|off]", "Shows or hides the FPS & frame time",
			[this](const std::vector<std::string>& args) -> std::string
			{
				m_Settings.bShowFps = args[0].empty() ? !m_Settings.bShowFps : (args[0] == "on" || args[0] == "1" || args[0] == "true");
				m_SettingsFile.Serialize();
				return m_Settings.bShowFps ? LOC(Console_FpsOn) : LOC(Console_FpsOff);
			},
			[](u32) { return std::vector<std::string>{ "on", "off" }; });
	}

	void Game::UpdateExplored()
	{
		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		if (player == nullptr || player->IsAwaitingTerrainCollision())
		{
			m_bHaveLastPlayerPos = false;
			return;
		}

		const glm::vec3 pos = player->GetTransform()->GetWorldPosition();
		if (m_bHaveLastPlayerPos)
		{
			// (Teleports aren't counted)
			const real moved = glm::distance(pos, m_LastPlayerPos);
			if (moved < 50.0f)
			{
				m_CurrentWorld.stats.distanceTravelled += moved;
			}
		}

		// Cells within sight radius of the player count as explored (only checked on entering a new cell)
		const i32 cellX = (i32)glm::floor(pos.x / EXPLORED_CELL_SIZE);
		const i32 cellZ = (i32)glm::floor(pos.z / EXPLORED_CELL_SIZE);
		const i32 lastCellX = (i32)glm::floor(m_LastPlayerPos.x / EXPLORED_CELL_SIZE);
		const i32 lastCellZ = (i32)glm::floor(m_LastPlayerPos.z / EXPLORED_CELL_SIZE);
		if (!m_bHaveLastPlayerPos || cellX != lastCellX || cellZ != lastCellZ)
		{
			const real radius = 256.0f;
			const i32 cellRadius = (i32)glm::ceil(radius / EXPLORED_CELL_SIZE);
			for (i32 z = -cellRadius; z <= cellRadius; ++z)
			{
				for (i32 x = -cellRadius; x <= cellRadius; ++x)
				{
					const glm::vec2 cellCenter(((real)(cellX + x) + 0.5f) * EXPLORED_CELL_SIZE, ((real)(cellZ + z) + 0.5f) * EXPLORED_CELL_SIZE);
					if (glm::distance(cellCenter, glm::vec2(pos.x, pos.z)) <= radius)
					{
						// 16 bits for each coordinate (+-4000km)
						m_ExploredCells.insert(((u32)(u16)(i16)(cellX + x) << 16) | (u32)(u16)(i16)(cellZ + z));
					}
				}
			}
			m_CurrentWorld.stats.exploredCells = (u32)m_ExploredCells.size();
		}
		m_LastPlayerPos = pos;
		m_bHaveLastPlayerPos = true;
	}

	void Game::DrawBackground(real opacity)
	{
		const real s = m_UI.Scale();
		// Darkens the bottom & fades in from the left behind the content, leaving the world visible on the right
		m_UI.FillRectGradient(UIRect(0.0f, 0.0f, m_UI.Width(), m_UI.Height()), UITheme::WithAlpha(UITheme::INK, 0.15f * opacity), UITheme::WithAlpha(UITheme::INK, 0.6f * opacity));
		m_UI.FillRectGradientHorizontal(UIRect(0.0f, 0.0f, 1150.0f * s, m_UI.Height()), UITheme::WithAlpha(UITheme::INK, 0.72f * opacity), UITheme::WithAlpha(UITheme::INK, 0.0f));
	}

	void Game::DrawTitle(const std::string& title, const std::string& subtitle)
	{
		const real s = m_UI.Scale();
		// Slides in when the screen opens
		const real t = glm::clamp(m_ScreenTime * 3.0f, 0.0f, 1.0f);
		const real slide = (1.0f - t * t * (3.0f - 2.0f * t)) * 40.0f * s;
		const real left = UITheme::SCREEN_MARGIN * s - slide;
		m_UI.Text(title, left, 220.0f * s, UITheme::TEXT_DISPLAY * s, UITheme::WithAlpha(UITheme::TEXT_PRIMARY, t), TextAlign::LEFT, true);
		m_UI.FillRect(UIRect(left + 4.0f * s, 290.0f * s, 64.0f * s * t, 4.0f * s), UITheme::EMBER, 2.0f * s);
		m_UI.Text(subtitle, left + 4.0f * s, 330.0f * s, UITheme::TEXT_BODY_LARGE * s, UITheme::WithAlpha(UITheme::TEXT_SECONDARY, t));
	}

	void Game::DrawMainMenu()
	{
		const real s = m_UI.Scale();
		DrawBackground(1.0f);
		DrawTitle(GAME_NAME, LOC(MainMenu_Tagline));

		UIRect column(UITheme::SCREEN_MARGIN * s, 420.0f * s, 440.0f * s, 600.0f * s);
		const real buttonHeight = UITheme::BUTTON_HEIGHT * s;
		const real gap = UITheme::SPACING * s;

		if (!m_Worlds.empty())
		{
			if (m_UI.Button("continue", column.CutTop(buttonHeight, gap), LOCF(MainMenu_Continue, m_Worlds[0].name.c_str()), true))
			{
				PlayWorld(m_Worlds[0]);
				return;
			}
		}
		if (m_UI.Button("worlds", column.CutTop(buttonHeight, gap), m_Worlds.empty() ? LOC(MainMenu_NewWorld) : LOC(MainMenu_Worlds), m_Worlds.empty()))
		{
			if (m_Worlds.empty())
			{
				m_NewWorldName = LOC(Worlds_FirstWorldName);
				m_NewWorldSeed = IntToString(RandomSeed());
				SetScreen(Screen::NEW_WORLD);
			}
			else
			{
				m_SelectedWorld = 0;
				SetScreen(Screen::WORLDS);
			}
		}
		if (m_UI.Button("settings", column.CutTop(buttonHeight, gap), LOC(MainMenu_Settings)))
		{
			m_ScreenBeforeSettings = Screen::MAIN_MENU;
			SetScreen(Screen::SETTINGS);
		}
		if (m_UI.Button("controls", column.CutTop(buttonHeight, gap), LOC(MainMenu_Controls)))
		{
			m_ScreenBeforeSettings = Screen::MAIN_MENU;
			SetScreen(Screen::CONTROLS);
		}
		if (m_UI.Button("editor", column.CutTop(buttonHeight, gap), LOC(MainMenu_Editor)))
		{
			EnterEditor();
			return;
		}
		if (m_UI.Button("quit", column.CutTop(buttonHeight, gap), LOC(MainMenu_Quit)))
		{
			g_EngineInstance->Stop();
		}

		// The version, subtly in the bottom right corner
		m_UI.Text(LOCF(MainMenu_Version, GAME_NAME, GAME_VERSION), m_UI.Width() - UITheme::SPACE_XL * s, m_UI.Height() - UITheme::SPACE_XL * s,
			UITheme::TEXT_CAPTION * s, UITheme::WithAlpha(UITheme::TEXT_SECONDARY, 0.7f), TextAlign::RIGHT);
	}

	void Game::DrawWorlds()
	{
		const real s = m_UI.Scale();
		DrawBackground(1.0f);
		DrawTitle(LOC(Worlds_Title), LOC(Worlds_Subtitle));

		const UIRect panel(120.0f * s, 400.0f * s, 820.0f * s, 600.0f * s);
		m_UI.Panel(panel);
		UIRect content = panel.Inset(24.0f * s);

		// World list (scrolled a whole row at a time, with the wheel or the scrollbar on the right)
		UIRect list = content.CutTop(content.h - 80.0f * s, 16.0f * s);
		const real rowHeight = 78.0f * s;
		const real rowGap = 10.0f * s;
		const u32 visibleRows = glm::max((u32)((list.h + rowGap) / (rowHeight + rowGap)), 1u);
		const u32 worldCount = (u32)m_Worlds.size();
		const real maxScroll = (real)(worldCount > visibleRows ? worldCount - visibleRows : 0);
		if (maxScroll > 0.0f)
		{
			const real scrollbarWidth = 10.0f * s;
			const UIRect scrollbar(list.x + list.w - scrollbarWidth, list.y, scrollbarWidth, list.h - rowGap);
			list.w -= scrollbarWidth + UITheme::SPACE_M * s;
			if (m_UI.ScrollDelta() != 0.0f && (m_UI.IsMouseOver(list) || m_UI.IsMouseOver(scrollbar)))
			{
				m_WorldListScroll -= m_UI.ScrollDelta() > 0.0f ? 1.0f : -1.0f;
			}
			m_UI.Scrollbar("world_scroll", scrollbar, (real)worldCount, (real)visibleRows, &m_WorldListScroll);
		}
		m_WorldListScroll = glm::clamp(m_WorldListScroll, 0.0f, maxScroll);
		const u32 firstRow = (u32)glm::round(m_WorldListScroll);
		for (u32 i = firstRow; i < worldCount && i < firstRow + visibleRows; ++i)
		{
			const WorldInfo& world = m_Worlds[i];
			const std::string id = "world" + IntToString(i);
			const std::string subtitle = LOCF(Worlds_ListSubtitle, world.seed, FormatPlayTime(world.playTime).c_str(), FormatDate(world.lastPlayed).c_str());
			if (m_UI.ListItem(id.c_str(), list.CutTop(rowHeight, rowGap), world.name, subtitle, (i32)i == m_SelectedWorld))
			{
				if (m_SelectedWorld == (i32)i && m_ScreenTime > 0.3f)
				{
					// Second click plays
					PlayWorld(world);
					return;
				}
				m_SelectedWorld = (i32)i;
				m_bConfirmDelete = false;
			}
		}

		// Buttons along the bottom
		const real buttonWidth = (content.w - 3.0f * 12.0f * s) / 4.0f;
		UIRect buttons = content;
		const bool bSelected = m_SelectedWorld >= 0 && m_SelectedWorld < (i32)m_Worlds.size();
		if (bSelected)
		{
			DrawWorldStats(UIRect(panel.x + panel.w + UITheme::SPACE_XL * s, panel.y, 440.0f * s, panel.h), m_Worlds[m_SelectedWorld]);
		}
		if (m_UI.Button("play", UIRect(buttons.x, buttons.y, buttonWidth, buttons.h), LOC(Worlds_Play), true, bSelected))
		{
			PlayWorld(m_Worlds[m_SelectedWorld]);
			return;
		}
		if (m_UI.Button("new", UIRect(buttons.x + (buttonWidth + 12.0f * s), buttons.y, buttonWidth, buttons.h), LOC(Worlds_New)))
		{
			m_NewWorldName = NextWorldName();
			m_NewWorldSeed = IntToString(RandomSeed());
			SetScreen(Screen::NEW_WORLD);
		}
		if (m_UI.Button("delete", UIRect(buttons.x + (buttonWidth + 12.0f * s) * 2.0f, buttons.y, buttonWidth, buttons.h), m_bConfirmDelete ? LOC(Worlds_ConfirmDelete) : LOC(Worlds_Delete), false, bSelected))
		{
			if (m_bConfirmDelete)
			{
				DeleteWorld(m_Worlds[m_SelectedWorld]);
				m_bConfirmDelete = false;
				m_SelectedWorld = m_Worlds.empty() ? -1 : 0;
			}
			else
			{
				m_bConfirmDelete = true;
			}
		}
		if (m_UI.Button("back", UIRect(buttons.x + (buttonWidth + 12.0f * s) * 3.0f, buttons.y, buttonWidth, buttons.h), LOC(Common_Back)))
		{
			SetScreen(Screen::MAIN_MENU);
		}
	}

	void Game::DrawWorldStats(const UIRect& panel, const WorldInfo& world)
	{
		const real s = m_UI.Scale();
		m_UI.Panel(panel);
		UIRect content = panel.Inset(UITheme::SPACE_XL * s);
		m_UI.Text(world.name, content.x, content.y + 20.0f * s, UITheme::TEXT_H2 * s, UITheme::TEXT_PRIMARY, TextAlign::LEFT, true);
		content.CutTop(56.0f * s);
		m_UI.FillRect(UIRect(content.x, content.y, content.w, 1.0f * s), UITheme::OUTLINE);
		content.CutTop(UITheme::SPACE_M * s);

		const WorldStats& stats = world.stats;
		auto Row = [&](const std::string& label, const std::string& value)
		{
			const UIRect row = content.CutTop(42.0f * s);
			m_UI.Text(label, row.x, row.y + row.h * 0.5f, UITheme::TEXT_BODY * s, UITheme::TEXT_SECONDARY);
			m_UI.Text(value, row.x + row.w, row.y + row.h * 0.5f, UITheme::TEXT_BODY * s, UITheme::TEXT_PRIMARY, TextAlign::RIGHT);
		};
		auto Km = [&](real meters)
		{
			return LOCF(Units_Km, meters / 1000.0f);
		};

		Row(LOC(Stats_Created), world.created.empty() ? std::string(LOC(Stats_Unknown)) : FormatDate(world.created));
		Row(LOC(Stats_LastPlayed), FormatDate(world.lastPlayed));
		Row(LOC(Stats_TimeInWorld), FormatPlayTime(world.playTime));
		Row(LOC(Stats_AreaExplored), LOCF(Units_SquareKm, stats.ExploredAreaKm2()));
		Row(LOC(Stats_DistanceTravelled), Km(stats.distanceTravelled));
		Row(LOC(Stats_RailLaid), Km(stats.railLaid));
		Row(LOC(Stats_TorchesPlaced), IntToString((i32)stats.torchesPlaced));
		Row(LOC(Stats_PiecesBuilt), IntToString((i32)stats.piecesBuilt));
		Row(LOC(Stats_ChestsOpened), IntToString((i32)stats.chestsOpened));
		Row(LOC(Stats_Seed), IntToString(world.seed));
	}

	void Game::DrawNewWorld()
	{
		const real s = m_UI.Scale();
		DrawBackground(1.0f);
		DrawTitle(LOC(NewWorld_Title), LOC(NewWorld_Subtitle));

		const UIRect panel(UITheme::SCREEN_MARGIN * s, 400.0f * s, 640.0f * s, 344.0f * s);
		m_UI.Panel(panel);
		UIRect content = panel.Inset(28.0f * s);
		const real fieldHeight = 60.0f * s;

		m_UI.Text(LOC(NewWorld_Name), content.x, content.y + 12.0f * s, UITheme::TEXT_SMALL * s, UITheme::TEXT_SECONDARY);
		content.CutTop(30.0f * s);
		m_UI.TextField("name", content.CutTop(fieldHeight, 20.0f * s), &m_NewWorldName, 32, LOC(Worlds_FirstWorldName));

		m_UI.Text(LOC(NewWorld_Seed), content.x, content.y + 12.0f * s, UITheme::TEXT_SMALL * s, UITheme::TEXT_SECONDARY);
		content.CutTop(30.0f * s);
		UIRect seedRow = content.CutTop(fieldHeight, 28.0f * s);
		m_UI.TextField("seed", UIRect(seedRow.x, seedRow.y, seedRow.w - 160.0f * s, seedRow.h), &m_NewWorldSeed, 10, LOC(NewWorld_SeedPlaceholder));
		if (m_UI.Button("random", UIRect(seedRow.x + seedRow.w - 148.0f * s, seedRow.y, 148.0f * s, seedRow.h), LOC(NewWorld_Random)))
		{
			m_NewWorldSeed = IntToString(RandomSeed());
		}

		const real buttonWidth = (content.w - 12.0f * s) * 0.5f;
		if (m_UI.Button("create", UIRect(content.x, content.y, buttonWidth, UITheme::BUTTON_HEIGHT * s), LOC(NewWorld_Create), true, !m_NewWorldName.empty()))
		{
			// Seeds can be any text, which is hashed if it isn't a number
			i32 seed = 0;
			bool bNumber = !m_NewWorldSeed.empty() && m_NewWorldSeed.size() <= 9;
			for (char c : m_NewWorldSeed)
			{
				bNumber = bNumber && c >= '0' && c <= '9';
			}
			if (m_NewWorldSeed.empty())
			{
				seed = RandomSeed();
			}
			else if (bNumber)
			{
				seed = ParseInt(m_NewWorldSeed);
			}
			else
			{
				seed = (i32)(Hash(m_NewWorldSeed.c_str()) & 0x7FFFFFFF);
			}
			CreateWorld(m_NewWorldName, seed);
			return;
		}
		if (m_UI.Button("back", UIRect(content.x + buttonWidth + 12.0f * s, content.y, buttonWidth, UITheme::BUTTON_HEIGHT * s), LOC(Common_Back)))
		{
			SetScreen(m_Worlds.empty() ? Screen::MAIN_MENU : Screen::WORLDS);
		}
	}

	void Game::DrawSettings()
	{
		const real s = m_UI.Scale();
		if (m_ScreenBeforeSettings == Screen::PAUSED)
		{
			m_UI.FillRect(UIRect(0.0f, 0.0f, m_UI.Width(), m_UI.Height()), UITheme::WithAlpha(UITheme::INK, 0.6f));
		}
		else
		{
			DrawBackground(1.0f);
		}
		DrawTitle(LOC(Settings_Title), LOC(Settings_Subtitle));

		const UIRect panel(120.0f * s, 400.0f * s, 700.0f * s, 630.0f * s);
		m_UI.Panel(panel);
		UIRect content = panel.Inset(24.0f * s);
		const real rowHeight = UITheme::ROW_HEIGHT * s;
		const real gap = UITheme::SPACE_S * s;

		// The back button sits below a scrolling list of options. To add an option, add a Row(...) below (rows that
		// are scrolled out of view aren't drawn).
		UIRect backRect = UIRect(content.x, content.y + content.h - UITheme::BUTTON_HEIGHT * s, content.w, UITheme::BUTTON_HEIGHT * s);
		UIRect list(content.x, content.y, content.w - 18.0f * s, content.h - backRect.h - 8.0f * s);
		const real totalHeight = m_SettingsContentHeight;
		if (m_UI.ScrollDelta() != 0.0f && m_UI.IsMouseOver(panel))
		{
			m_SettingsScroll -= m_UI.ScrollDelta() * (rowHeight + gap);
		}
		m_UI.Scrollbar("settings_scroll", UIRect(list.x + list.w + 6.0f * s, list.y, 8.0f * s, list.h), totalHeight, list.h, &m_SettingsScroll);
		m_SettingsScroll = glm::clamp(m_SettingsScroll, 0.0f, glm::max(totalHeight - list.h, 0.0f));
		real rowY = list.y - m_SettingsScroll;
		auto Row = [&](auto&& draw)
		{
			const UIRect rect(list.x, rowY, list.w, rowHeight);
			rowY += rowHeight + gap;
			if (rect.y >= list.y - 1.0f && rect.y + rect.h <= list.y + list.h + 1.0f)
			{
				draw(rect);
			}
		};

		bool bChanged = false;
		Row([&](const UIRect& r) { bChanged |= m_UI.Slider("fov", r, LOC(Settings_Fov), &m_Settings.fov, 50.0f, 110.0f, LOC(Settings_FovValue)); });
		Row([&](const UIRect& r) { bChanged |= m_UI.Slider("sensitivity", r, LOC(Settings_MouseSensitivity), &m_Settings.mouseSensitivity, 0.1f, 4.0f, "%.2f"); });
		Row([&](const UIRect& r) { bChanged |= m_UI.Checkbox("invert", r, LOC(Settings_InvertMouse), &m_Settings.bInvertMouse); });
		Row([&](const UIRect& r)
		{
			real volume = m_Settings.masterVolume * 100.0f;
			if (m_UI.Slider("volume", r, LOC(Settings_Volume), &volume, 0.0f, 100.0f, "%.0f%%"))
			{
				m_Settings.masterVolume = volume / 100.0f;
				bChanged = true;
			}
			if (m_UI.WasReleased("volume"))
			{
				// Lets the new volume be heard (the master gain was applied while dragging)
				m_UI.PlayConfirm();
			}
		});
		Row([&](const UIRect& r)
		{
			real volume = m_Settings.musicVolume * 100.0f;
			if (m_UI.Slider("music_volume", r, LOC(Settings_MusicVolume), &volume, 0.0f, 100.0f, "%.0f%%"))
			{
				m_Settings.musicVolume = volume / 100.0f;
				bChanged = true;
			}
		});
		// Graphics: a preset sets all of the options below it (which show it as custom once changed individually)
		const char* qualityNames[] = { LOC(Settings_QualityLow), LOC(Settings_QualityMedium), LOC(Settings_QualityHigh), LOC(Settings_QualityUltra) };
		// A slider through count named steps
		auto Stepped = [&](const char* id, const UIRect& r, const std::string& label, i32* value, i32 count) -> bool
		{
			real v = (real)*value;
			if (m_UI.Slider(id, r, label, &v, 0.0f, (real)(count - 1), ""))
			{
				const i32 newValue = glm::clamp((i32)glm::round(v), 0, count - 1);
				if (newValue != *value)
				{
					*value = newValue;
					return true;
				}
			}
			return false;
		};
		Row([&](const UIRect& r)
		{
			m_UI.Text(LOC(Settings_GraphicsHeader), r.x, r.y + r.h * 0.7f, UITheme::TEXT_BODY_LARGE * s, UITheme::EMBER, TextAlign::LEFT, true);
		});
		Row([&](const UIRect& r)
		{
			const i32 preset = GetGraphicsPreset();
			// Custom sits past the end, so dragging from it picks a preset
			i32 presetIndex = preset >= 0 ? preset : GRAPHICS_PRESET_COUNT;
			const std::string label = LOCF(Settings_Preset, preset >= 0 ? qualityNames[preset] : LOC(Settings_PresetCustom));
			if (Stepped("preset", r, label, &presetIndex, GRAPHICS_PRESET_COUNT + 1) && presetIndex < GRAPHICS_PRESET_COUNT)
			{
				ApplyGraphicsPreset(presetIndex);
				bChanged = true;
			}
		});
		Row([&](const UIRect& r)
		{
			const char* names[] = { LOC(Settings_Off), qualityNames[0], qualityNames[1], qualityNames[2], qualityNames[3] };
			bChanged |= Stepped("shadow_quality", r, LOCF(Settings_ShadowQuality, names[glm::clamp(m_Settings.shadowQuality, 0, 4)]), &m_Settings.shadowQuality, 5);
		});
		Row([&](const UIRect& r)
		{
			const char* names[] = { LOC(Settings_DrawNear), LOC(Settings_DrawMedium), LOC(Settings_DrawFar), LOC(Settings_DrawMax) };
			bChanged |= Stepped("draw_distance", r, LOCF(Settings_DrawDistance, names[glm::clamp(m_Settings.drawDistance, 0, 3)]), &m_Settings.drawDistance, 4);
		});
		Row([&](const UIRect& r)
		{
			bChanged |= Stepped("quality", r, LOCF(Settings_TerrainDetail, qualityNames[glm::clamp(m_Settings.terrainQuality, 0, 3)]), &m_Settings.terrainQuality, 4);
		});
		Row([&](const UIRect& r)
		{
			const char* names[] = { LOC(Settings_Off), "FXAA", "TAA" };
			bChanged |= Stepped("anti_aliasing", r, LOCF(Settings_AntiAliasing, names[glm::clamp(m_Settings.antiAliasing, 0, 2)]), &m_Settings.antiAliasing, 3);
		});
		Row([&](const UIRect& r)
		{
			const char* names[] = { qualityNames[0], qualityNames[2] };
			bChanged |= Stepped("effects_quality", r, LOCF(Settings_EffectsQuality, names[glm::clamp(m_Settings.effectsQuality, 0, 1)]), &m_Settings.effectsQuality, 2);
		});
		Row([&](const UIRect& r) { bChanged |= m_UI.Checkbox("ssao", r, LOC(Settings_AmbientOcclusion), &m_Settings.bAmbientOcclusion); });
		Row([&](const UIRect& r) { bChanged |= m_UI.Checkbox("vsync", r, LOC(Settings_VSync), &m_Settings.bVSync); });
		Row([&](const UIRect& r) { bChanged |= m_UI.Checkbox("minimap", r, LOC(Settings_ShowMinimap), &m_Settings.bShowMinimap); });
		Row([&](const UIRect& r) { bChanged |= m_UI.Checkbox("show_fps", r, LOC(Settings_ShowFps), &m_Settings.bShowFps); });
		Row([&](const UIRect& r)
		{
			// Clicking cycles through the languages
			u32 languageIndex = 0;
			for (u32 i = 0; i < Loc::GetLanguageCount(); ++i)
			{
				languageIndex = m_Settings.language == Loc::GetLanguageCode(i) ? i : languageIndex;
			}
			if (m_UI.Button("language", r, LOCF(Settings_Language, Loc::GetLanguageName(languageIndex))))
			{
				m_Settings.language = Loc::GetLanguageCode((languageIndex + 1) % Loc::GetLanguageCount());
				bChanged = true;
			}
		});
		m_SettingsContentHeight = rowY + m_SettingsScroll - list.y;

		if (bChanged)
		{
			ApplySettings();
		}

		if (m_UI.Button("back", backRect, LOC(Common_Back), true))
		{
			m_SettingsFile.Serialize();
			SetScreen(m_ScreenBeforeSettings);
			if (m_ScreenBeforeSettings == Screen::PAUSED)
			{
				g_EngineInstance->SetSimulationPaused(true);
			}
		}
	}

	namespace
	{
		// Something on the Controls screen that can be rebound: a GameKeys entry or an Action
		struct BindRef
		{
			const GameKeys::Entry* entry = nullptr;
			Action action = Action::_NONE;
			Action mirrorAction = Action::_NONE; // Rebound together (the vehicle's controls follow walking)
			i32 actionShareTag = 0;

			bool IsValid() const { return entry != nullptr || action != Action::_NONE; }
			bool AcceptsKey() const { return entry == nullptr || entry->key != nullptr; }
			bool AcceptsMouse() const { return entry == nullptr || entry->mouse != nullptr; }
			i32 Tag() const { return entry != nullptr ? entry->shareTag : actionShareTag; }
			bool SameAs(const BindRef& other) const { return entry == other.entry && action == other.action; }
			KeyCode GetKey() const
			{
				if (entry != nullptr)
				{
					return entry->key != nullptr ? entry->key->code : KeyCode::_NONE;
				}
				return g_InputManager->GetActionBinding(action).keyCode;
			}
			MouseButton GetMouse() const
			{
				if (entry != nullptr)
				{
					return entry->mouse != nullptr ? entry->mouse->button : MouseButton::_NONE;
				}
				return g_InputManager->GetActionBinding(action).mouseButton;
			}
			void Set(KeyCode key, MouseButton mouse) const
			{
				if (entry != nullptr)
				{
					if (entry->key != nullptr) entry->key->code = key;
					if (entry->mouse != nullptr) entry->mouse->button = mouse;
					return;
				}
				g_InputManager->SetActionKeyAndMouse(action, key, mouse);
				if (mirrorAction != Action::_NONE)
				{
					g_InputManager->SetActionKeyAndMouse(mirrorAction, key, mouse);
				}
			}
		};

		// Actions the Controls screen can rebind (with the ones that follow them)
		const Action REBINDABLE_ACTIONS[][2] =
		{
			{ Action::MOVE_FORWARD, Action::VEHICLE_ACCELERATE }, { Action::MOVE_LEFT, Action::VEHICLE_TURN_LEFT },
			{ Action::MOVE_BACKWARD, Action::VEHICLE_REVERSE }, { Action::MOVE_RIGHT, Action::VEHICLE_TURN_RIGHT },
			{ Action::PLACE_ITEM, Action::_NONE }, { Action::SHOW_INVENTORY, Action::_NONE },
			{ Action::INTERACT_RIGHT_HAND, Action::_NONE }, { Action::PICKUP_ITEM, Action::_NONE },
			{ Action::DROP_ITEM, Action::_NONE }, { Action::ENTER_TRACK_BUILD_MODE, Action::_NONE },
			{ Action::PAUSE, Action::_NONE }, { Action::TAKE_SCREENSHOT, Action::_NONE },
		};
	} // anonymous namespace

	// FLEX_UI_TEST=1: while the tour is on the Controls screen, rebinds some controls with injected key presses & checks the
	// result (printing UITEST lines). Returns true while it still has steps to do.
	bool Game::TourRebindTest()
	{
		static i32 stage = 0;
		static real stageTime = 0.0f;
		stageTime += g_UnpausedDeltaTime;
		auto Wait = [&](real t) { return stageTime > t; };
		// (Released a frame later, so it counts as pressed for a frame)
		static KeyCode s_PendingRelease = KeyCode::_NONE;
		static MouseButton s_PendingMouseRelease = MouseButton::_NONE;
		if (s_PendingRelease != KeyCode::_NONE)
		{
			g_InputManager->KeyCallback(s_PendingRelease, KeyAction::KEY_RELEASE, 0);
			s_PendingRelease = KeyCode::_NONE;
		}
		if (s_PendingMouseRelease != MouseButton::_NONE)
		{
			g_InputManager->MouseButtonCallback(s_PendingMouseRelease, KeyAction::KEY_RELEASE, 0);
			s_PendingMouseRelease = MouseButton::_NONE;
		}
		auto Press = [&](KeyCode key) { g_InputManager->KeyCallback(key, KeyAction::KEY_PRESS, 0); s_PendingRelease = key; };
		auto Advance = [&]() { ++stage; stageTime = 0.0f; };
		auto Screenshot = [](const char* name)
		{
			const std::string directory = RelativePathToAbsolute(SCREENSHOT_DIRECTORY);
			Platform::CreateDirectoryRecursive(directory);
			g_Renderer->RequestScreenshot(directory + "tour_" + name + ".png");
		};
		switch (stage)
		{
		case 0: m_RebindIndex = 0; Advance(); break; // Move forward
		case 1: if (Wait(0.5f)) { Screenshot("controls_rebind"); Advance(); } break;
		case 2: if (Wait(0.3f)) { Press(KeyCode::KEY_J); Advance(); } break;
		case 3:
			if (Wait(0.3f))
			{
				Print("UITEST: move forward = %s (expect J), bindings \"%s\"\n", KeyCodeStrings[(i32)g_InputManager->GetActionBinding(Action::MOVE_FORWARD).keyCode], m_Settings.keyBindings.c_str());
				m_RebindIndex = 5; // Sprint (Shift) -> A, which Move left has
				Advance();
			}
			break;
		case 4: if (Wait(0.3f)) { Press(KeyCode::KEY_A); Advance(); } break;
		case 5:
			if (Wait(0.3f))
			{
				Print("UITEST: sprint = %s (expect A), move left = %s (expect L Shift), message \"%s\"\n", KeyCodeStrings[(i32)GameKeys::SPRINT.code],
					KeyCodeStrings[(i32)g_InputManager->GetActionBinding(Action::MOVE_LEFT).keyCode], m_RebindMessage.c_str());
				Screenshot("controls_swapped");
				m_RebindIndex = 5; // Mouse button on a key control: rejected
				g_InputManager->MouseButtonCallback(MouseButton::MIDDLE, KeyAction::KEY_PRESS, 0);
				s_PendingMouseRelease = MouseButton::MIDDLE;
				Advance();
			}
			break;
		case 6:
			if (Wait(0.3f))
			{
				Print("UITEST: after a mouse button on sprint: still waiting = %d (expect 1), message \"%s\"\n", (i32)(m_RebindIndex >= 0), m_RebindMessage.c_str());
				Press(KeyCode::KEY_ESCAPE);
				Advance();
			}
			break;
		case 7:
			if (Wait(0.3f))
			{
				Print("UITEST: after Esc: waiting = %d (expect 0), screen is controls = %d (expect 1)\n", (i32)(m_RebindIndex >= 0), (i32)(m_Screen == Screen::CONTROLS));
				ResetBindings();
				Print("UITEST: reset: sprint = %s, move left = %s, move forward = %s, bindings \"%s\"\n", KeyCodeStrings[(i32)GameKeys::SPRINT.code],
					KeyCodeStrings[(i32)g_InputManager->GetActionBinding(Action::MOVE_LEFT).keyCode], KeyCodeStrings[(i32)g_InputManager->GetActionBinding(Action::MOVE_FORWARD).keyCode], m_Settings.keyBindings.c_str());
				// Left rebound for the settings round trip below
				GameKeys::CAMPFIRE.code = KeyCode::KEY_K;
				SaveBindings();
				GameKeys::CAMPFIRE.code = KeyCode::KEY_H;
				LoadBindings();
				Print("UITEST: reloaded campfire = %s (expect K)\n", KeyCodeStrings[(i32)GameKeys::CAMPFIRE.code]);
				ResetBindings();
				Advance();
			}
			break;
		default:
			return false;
		}
		return true;
	}

	// Saved in the settings as "id=key,mouse;..." for each binding that differs from its default (Actions have "action:" in front)
	void Game::SaveBindings()
	{
		std::string saved;
		auto Append = [&](const std::string& id, KeyCode key, MouseButton mouse)
		{
			saved += id + "=" + std::to_string((i32)key) + "," + std::to_string((i32)mouse) + ";";
		};
		for (const GameKeys::Entry& entry : GameKeys::Registry())
		{
			const KeyCode key = entry.key != nullptr ? entry.key->code : KeyCode::_NONE;
			const MouseButton mouse = entry.mouse != nullptr ? entry.mouse->button : MouseButton::_NONE;
			if (key != entry.defaultKey || mouse != entry.defaultMouse)
			{
				Append(entry.id, key, mouse);
			}
		}
		for (const Action(&pair)[2] : REBINDABLE_ACTIONS)
		{
			const InputBinding& binding = g_InputManager->GetActionBinding(pair[0]);
			const auto defaultIter = m_DefaultActionBindings.find((i32)pair[0]);
			if (defaultIter != m_DefaultActionBindings.end() && (binding.keyCode != defaultIter->second.first || binding.mouseButton != defaultIter->second.second))
			{
				Append(std::string("action:") + ActionStrings[(i32)pair[0]], binding.keyCode, binding.mouseButton);
			}
		}
		m_Settings.keyBindings = saved;
		m_SettingsFile.Serialize();
	}

	// Remembers the Actions' default bindings & applies the saved ones
	void Game::LoadBindings()
	{
		if (m_DefaultActionBindings.empty())
		{
			for (const Action(&pair)[2] : REBINDABLE_ACTIONS)
			{
				const InputBinding& binding = g_InputManager->GetActionBinding(pair[0]);
				m_DefaultActionBindings[(i32)pair[0]] = { binding.keyCode, binding.mouseButton };
			}
		}

		size_t start = 0;
		while (start < m_Settings.keyBindings.size())
		{
			size_t end = m_Settings.keyBindings.find(';', start);
			if (end == std::string::npos)
			{
				end = m_Settings.keyBindings.size();
			}
			const std::string item = m_Settings.keyBindings.substr(start, end - start);
			start = end + 1;
			const size_t equals = item.find('=');
			const size_t comma = item.find(',');
			if (equals == std::string::npos || comma == std::string::npos || comma < equals)
			{
				continue;
			}
			const std::string id = item.substr(0, equals);
			const i32 key = atoi(item.substr(equals + 1, comma - equals - 1).c_str());
			const i32 mouse = atoi(item.substr(comma + 1).c_str());
			if (key < 0 || key > (i32)KeyCode::_NONE || mouse < 0 || mouse > (i32)MouseButton::_NONE)
			{
				continue;
			}
			if (id.compare(0, 7, "action:") == 0)
			{
				for (const Action(&pair)[2] : REBINDABLE_ACTIONS)
				{
					if (id.compare(7, std::string::npos, ActionStrings[(i32)pair[0]]) == 0)
					{
						g_InputManager->SetActionKeyAndMouse(pair[0], (KeyCode)key, (MouseButton)mouse);
						if (pair[1] != Action::_NONE)
						{
							g_InputManager->SetActionKeyAndMouse(pair[1], (KeyCode)key, (MouseButton)mouse);
						}
					}
				}
			}
			else
			{
				for (const GameKeys::Entry& entry : GameKeys::Registry())
				{
					if (id == entry.id)
					{
						if (entry.key != nullptr) entry.key->code = (KeyCode)key;
						if (entry.mouse != nullptr) entry.mouse->button = (MouseButton)mouse;
					}
				}
			}
		}
	}

	void Game::ResetBindings()
	{
		for (const GameKeys::Entry& entry : GameKeys::Registry())
		{
			if (entry.key != nullptr) entry.key->code = entry.defaultKey;
			if (entry.mouse != nullptr) entry.mouse->button = entry.defaultMouse;
		}
		for (const Action(&pair)[2] : REBINDABLE_ACTIONS)
		{
			const auto defaultIter = m_DefaultActionBindings.find((i32)pair[0]);
			if (defaultIter != m_DefaultActionBindings.end())
			{
				g_InputManager->SetActionKeyAndMouse(pair[0], defaultIter->second.first, defaultIter->second.second);
				if (pair[1] != Action::_NONE)
				{
					g_InputManager->SetActionKeyAndMouse(pair[1], defaultIter->second.first, defaultIter->second.second);
				}
			}
		}
		SaveBindings();
	}

	void Game::DrawControls()
	{
		const real s = m_UI.Scale();
		if (m_ScreenBeforeSettings == Screen::PAUSED)
		{
			m_UI.FillRect(UIRect(0.0f, 0.0f, m_UI.Width(), m_UI.Height()), UITheme::WithAlpha(UITheme::INK, 0.6f));
		}
		else
		{
			DrawBackground(1.0f);
		}
		DrawTitle(LOC(Controls_Title), LOC(Controls_Subtitle));

		// Keys come from the input bindings (Actions) & GameKeys (keys the game checks directly), so this stays right
		// when either changes. Clicking a row rebinds it (see ApplyBindings for how they're saved).
		using Keys = std::vector<std::string>;
		auto KeyName = [](KeyCode keyCode) { return std::string(KeyCodeStrings[(u32)keyCode]); };
		auto MouseName = [](MouseButton button) { return std::string(MouseButtonStrings[(u32)button]); };
		auto Bound = [&](std::initializer_list<Action> actions)
		{
			Keys keys;
			for (Action action : actions)
			{
				const InputBinding& binding = g_InputManager->GetActionBinding(action);
				if (binding.keyCode != KeyCode::_NONE)
				{
					keys.push_back(KeyName(binding.keyCode));
				}
				if (binding.mouseButton != MouseButton::_NONE)
				{
					keys.push_back(MouseName(binding.mouseButton));
				}
				if (binding.mouseAxis == MouseAxis::SCROLL_X || binding.mouseAxis == MouseAxis::SCROLL_Y)
				{
					keys.push_back(LOC(Controls_KeyScroll));
				}
			}
			// (Pairs of actions, like scrolling forward & back, can share a binding)
			keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
			if (keys.empty())
			{
				keys.push_back(LOC(Input_Unbound));
			}
			return keys;
		};

		// Quick access slots are selected with the number keys, from 1 (0 is the 10th)
		const i32 slotKeyCount = glm::min((i32)Player::QUICK_ACCESS_ITEM_COUNT, 10);
		const std::string slotKeys = KeyName(KeyCode::KEY_1) + "-" + KeyName(slotKeyCount == 10 ? KeyCode::KEY_0 : (KeyCode)((i32)KeyCode::KEY_1 + slotKeyCount - 1));
		Keys selectItemKeys = Bound({ Action::CYCLE_SELECTED_ITEM_FORWARD, Action::CYCLE_SELECTED_ITEM_BACKWARD });
		selectItemKeys.push_back(slotKeys);

		struct Row
		{
			const char* label;
			Keys keys; // Used when bind isn't valid (fixed controls)
			BindRef bind;
		};
		struct Section
		{
			const char* title;
			std::vector<Row> rows;
		};
		auto G = [](const char* id)
		{
			BindRef ref;
			for (const GameKeys::Entry& entry : GameKeys::Registry())
			{
				if (strcmp(entry.id, id) == 0)
				{
					ref.entry = &entry;
				}
			}
			return ref;
		};
		auto A = [](Action action, Action mirror = Action::_NONE, i32 shareTag = 0)
		{
			BindRef ref;
			ref.action = action;
			ref.mirrorAction = mirror;
			ref.actionShareTag = shareTag;
			return ref;
		};
		// One column each, except the last column which holds the last two sections
		const Section sections[] =
		{
			{ LOC(Controls_Movement), {
				{ LOC(Controls_MoveForward), {}, A(Action::MOVE_FORWARD, Action::VEHICLE_ACCELERATE) },
				{ LOC(Controls_MoveLeft), {}, A(Action::MOVE_LEFT, Action::VEHICLE_TURN_LEFT) },
				{ LOC(Controls_MoveBackward), {}, A(Action::MOVE_BACKWARD, Action::VEHICLE_REVERSE) },
				{ LOC(Controls_MoveRight), {}, A(Action::MOVE_RIGHT, Action::VEHICLE_TURN_RIGHT) },
				{ LOC(Controls_Look), { LOC(Controls_KeyMouse) }, {} },
				{ LOC(Controls_Jump), {}, A(Action::PLACE_ITEM) },
				{ LOC(Controls_Sprint), {}, G("sprint") },
				{ LOC(Controls_Climb), {}, G("climb") },
				{ LOC(Controls_Crawl), {}, G("crawl") },
				{ LOC(Controls_Crouch), {}, G("crouch") },
				{ LOC(Controls_GrapplingHook), {}, G("grappling hook") },
			} },
			{ LOC(Controls_Items), {
				{ LOC(Controls_UseItem), {}, G("use item") },
				{ LOC(Controls_UseItemSecondary), {}, G("use item secondary") },
				{ LOC(Controls_FlattenTerrain), {}, G("terrain flatten") },
				{ LOC(Controls_SelectItem), selectItemKeys, {} },
				{ LOC(Controls_Inventory), {}, A(Action::SHOW_INVENTORY) },
				{ LOC(Controls_Interact), {}, A(Action::INTERACT_RIGHT_HAND, Action::_NONE, 3) },
				{ LOC(Controls_PickUp), {}, A(Action::PICKUP_ITEM) },
				{ LOC(Controls_Drop), {}, A(Action::DROP_ITEM) },
			} },
			{ LOC(Controls_Building), {
				{ LOC(Controls_BuildMode), {}, G("build mode") },
				{ LOC(Controls_BuildRotate), {}, G("build rotate") },
				{ LOC(Controls_BuildMaterial), {}, G("build material") },
				{ LOC(Controls_Campfire), {}, G("campfire") },
				{ LOC(Controls_LightCampfire), {}, G("light campfire") },
				{ LOC(Controls_Crafting), {}, G("crafting") },
				{ LOC(Controls_UseStation), {}, G("use station") },
				{ LOC(Controls_TerrainMaterial), {}, G("terrain material") },
				{ LOC(Controls_TrackBuild), {}, A(Action::ENTER_TRACK_BUILD_MODE) },
			} },
			{ LOC(Controls_General), {
				{ LOC(Controls_Map), {}, G("map") },
				{ LOC(Controls_MapRecenter), {}, G("map recenter") },
				{ LOC(Controls_Waypoint), {}, G("waypoint") },
				{ LOC(Controls_MapWaypoint), {}, G("map waypoint") },
				{ LOC(Controls_Pause), {}, A(Action::PAUSE) },
				{ LOC(Controls_Screenshot), {}, A(Action::TAKE_SCREENSHOT) },
				{ LOC(Controls_Console), {}, G("console") },
			} },
		};

		// Every row that can be rebound, in order (m_RebindIndex is an index into this)
		std::vector<const Row*> rebindable;
		for (const Section& section : sections)
		{
			for (const Row& row : section.rows)
			{
				if (row.bind.IsValid())
				{
					rebindable.push_back(&row);
				}
			}
		}
		if (m_RebindIndex >= (i32)rebindable.size())
		{
			m_RebindIndex = -1;
		}

		// Waiting for a key: the next key or mouse button pressed is the new binding (Esc cancels)
		if (m_RebindIndex >= 0)
		{
			const BindRef& target = rebindable[m_RebindIndex]->bind;
			KeyCode newKey = KeyCode::_NONE;
			MouseButton newMouse = MouseButton::_NONE;
			bool bCancel = false;
			if (g_InputManager->GetKeyPressed(KeyCode::KEY_ESCAPE))
			{
				bCancel = true;
			}
			else
			{
				for (i32 i = 0; i < (i32)KeyCode::_NONE; ++i)
				{
					if (g_InputManager->GetKeyPressed((KeyCode)i))
					{
						newKey = (KeyCode)i;
						break;
					}
				}
				for (i32 i = 0; i < (i32)MouseButton::COUNT && newKey == KeyCode::_NONE; ++i)
				{
					if (g_InputManager->IsMouseButtonPressed((MouseButton)i))
					{
						newMouse = (MouseButton)i;
						break;
					}
				}
			}

			if (bCancel)
			{
				m_RebindIndex = -1;
			}
			else if (newKey != KeyCode::_NONE || newMouse != MouseButton::_NONE)
			{
				const bool bNumberKey = newKey >= KeyCode::KEY_0 && newKey <= KeyCode::KEY_9; // (Select item)
				if (bNumberKey || (newKey != KeyCode::_NONE && !target.AcceptsKey()) || (newMouse != MouseButton::_NONE && !target.AcceptsMouse()))
				{
					m_RebindMessage = LOC(Controls_RebindInvalid);
					m_RebindMessageTime = m_ScreenTime;
				}
				else
				{
					const KeyCode oldKey = target.GetKey();
					const MouseButton oldMouse = target.GetMouse();
					m_RebindMessage.clear();
					// Whatever else used this key gets this row's old one (shared keys, like Shift for sprint &
					// climb, are meant to overlap)
					for (const Row* other : rebindable)
					{
						if (other->bind.SameAs(target) || (target.Tag() != 0 && other->bind.Tag() == target.Tag()))
						{
							continue;
						}
						const bool bKeyClash = newKey != KeyCode::_NONE && other->bind.GetKey() == newKey;
						const bool bMouseClash = newMouse != MouseButton::_NONE && other->bind.GetMouse() == newMouse;
						if (bKeyClash || bMouseClash)
						{
							other->bind.Set(bKeyClash ? (other->bind.AcceptsKey() ? oldKey : KeyCode::_NONE) : other->bind.GetKey(),
								bMouseClash ? (other->bind.AcceptsMouse() ? oldMouse : MouseButton::_NONE) : other->bind.GetMouse());
							m_RebindMessage = LOCF(Controls_RebindSwapped, other->label);
							m_RebindMessageTime = m_ScreenTime;
						}
					}
					target.Set(newKey, newMouse);
					m_RebindIndex = -1;
					SaveBindings();
				}
			}
		}

		const real rowHeight = 34.0f * s;
		const real headingHeight = 50.0f * s;
		const real columnGap = UITheme::SPACE_XXL * s;
		const real panelWidth = glm::min(1440.0f * s, m_UI.Width() - 2.0f * UITheme::SCREEN_MARGIN * s);
		const real sectionGap = UITheme::SPACE_M * s;
		real columnHeights[3] = {};
		for (u32 sectionIndex = 0; sectionIndex < ARRAY_LENGTH(sections); ++sectionIndex)
		{
			real& height = columnHeights[glm::min(sectionIndex, 2u)];
			height += (height > 0.0f ? sectionGap : 0.0f) + headingHeight + (real)sections[sectionIndex].rows.size() * rowHeight;
		}
		const real panelHeight = 2.0f * UITheme::SPACE_L * s + glm::max(columnHeights[0], glm::max(columnHeights[1], columnHeights[2]));
		const UIRect panel(UITheme::SCREEN_MARGIN * s, 330.0f * s, panelWidth, panelHeight);
		m_UI.Panel(panel);
		const UIRect content = panel.Inset(UITheme::SPACE_L * s);
		const real columnWidth = (content.w - 2.0f * columnGap) / 3.0f;

		const real keyTextSize = UITheme::TEXT_CAPTION * s;
		const real keyHeight = 26.0f * s;
		const real keyPadding = 10.0f * s;
		UIRect column;
		i32 rebindIndex = 0;
		for (u32 sectionIndex = 0; sectionIndex < ARRAY_LENGTH(sections); ++sectionIndex)
		{
			const u32 columnIndex = glm::min(sectionIndex, 2u);
			if (sectionIndex == columnIndex)
			{
				column = UIRect(content.x + (columnWidth + columnGap) * (real)columnIndex, content.y, columnWidth, content.h);
			}
			else
			{
				column.CutTop(sectionGap);
			}

			const Section& section = sections[sectionIndex];
			const UIRect heading = column.CutTop(headingHeight);
			m_UI.Text(section.title, heading.x, heading.y + heading.h * 0.4f, UITheme::TEXT_BODY_LARGE * s, UITheme::EMBER, TextAlign::LEFT, true);
			m_UI.FillRect(UIRect(heading.x, heading.y + heading.h - 10.0f * s, heading.w, 1.0f * s), UITheme::OUTLINE);

			for (const Row& row : section.rows)
			{
				const UIRect rect = column.CutTop(rowHeight);
				const real centerY = rect.y + rect.h * 0.5f;
				Keys keys = row.keys;
				const bool bRebindable = row.bind.IsValid();
				const bool bWaiting = bRebindable && rebindIndex == m_RebindIndex;
				if (bRebindable)
				{
					const UIRect hitRect(rect.x - 6.0f * s, rect.y, rect.w + 12.0f * s, rect.h);
					if (!bWaiting && m_RebindIndex < 0 && m_UI.IsMouseOver(hitRect))
					{
						m_UI.FillRect(hitRect, UITheme::WithAlpha(UITheme::RAISED_HOVER, 0.6f), UITheme::RADIUS_S * s);
						if (m_UI.IsMousePressed())
						{
							m_RebindIndex = rebindIndex;
							m_RebindMessage.clear();
						}
					}
					const KeyCode key = row.bind.GetKey();
					const MouseButton mouse = row.bind.GetMouse();
					keys.clear();
					if (key != KeyCode::_NONE)
					{
						keys.push_back(KeyName(key));
					}
					if (mouse != MouseButton::_NONE)
					{
						keys.push_back(MouseName(mouse));
					}
					if (keys.empty())
					{
						keys.push_back(LOC(Input_Unbound));
					}
					if (bWaiting)
					{
						keys = { LOC(Controls_PressKey) };
					}
					++rebindIndex;
				}
				m_UI.Text(row.label, rect.x, centerY, UITheme::TEXT_BODY * s, bWaiting ? UITheme::TEXT_PRIMARY : UITheme::TEXT_SECONDARY);
				// Key caps, right aligned
				real right = rect.x + rect.w;
				for (auto iter = keys.rbegin(); iter != keys.rend(); ++iter)
				{
					const real width = glm::max(m_UI.MeasureText(*iter, keyTextSize) + keyPadding * 2.0f, keyHeight);
					const UIRect cap(right - width, centerY - keyHeight * 0.5f, width, keyHeight);
					if (bWaiting)
					{
						m_UI.FillRect(cap, UITheme::EMBER_DEEP, UITheme::RADIUS_S * s);
					}
					else
					{
						m_UI.FillRectGradient(cap, UITheme::RAISED_HOVER, UITheme::RAISED, UITheme::RADIUS_S * s);
					}
					m_UI.Text(*iter, cap.x + cap.w * 0.5f, centerY, keyTextSize, bWaiting ? UITheme::TEXT_ON_ACCENT : UITheme::TEXT_PRIMARY, TextAlign::CENTER);
					right -= width + UITheme::SPACE_XS * s * 1.5f;
				}
			}
		}

		const UIRect back(panel.x, panel.y + panel.h + UITheme::SPACE_L * s, 440.0f * s, UITheme::BUTTON_HEIGHT * s);
		if (m_UI.Button("back", back, LOC(Common_Back), true))
		{
			m_RebindIndex = -1;
			SetScreen(m_ScreenBeforeSettings);
			if (m_ScreenBeforeSettings == Screen::PAUSED)
			{
				g_EngineInstance->SetSimulationPaused(true);
			}
		}
		const UIRect reset(back.x + back.w + UITheme::SPACE_L * s, back.y, 360.0f * s, back.h);
		if (m_UI.Button("reset", reset, LOC(Controls_ResetDefaults)))
		{
			m_RebindIndex = -1;
			ResetBindings();
			m_RebindMessage = LOC(Controls_RebindReset);
			m_RebindMessageTime = m_ScreenTime;
		}

		// Hint, or what the last change did
		const bool bShowMessage = !m_RebindMessage.empty() && m_ScreenTime - m_RebindMessageTime < 5.0f;
		const std::string hint = m_RebindIndex >= 0 ? LOC(Controls_RebindWaiting) : (bShowMessage ? m_RebindMessage : std::string(LOC(Controls_RebindHint)));
		m_UI.Text(hint, reset.x + reset.w + UITheme::SPACE_XL * s, back.y + back.h * 0.5f, UITheme::TEXT_SMALL * s,
			(m_RebindIndex >= 0 || bShowMessage) ? UITheme::EMBER : UITheme::TEXT_SECONDARY);
	}

	void Game::DrawLoading()
	{
		const real s = m_UI.Scale();
		m_LoadingTime += g_UnpausedDeltaTime;
		g_Window->SetCursorMode(CursorMode::DISABLED);

		BaseScene* scene = g_SceneManager->CurrentScene();
		Player* player = scene->GetPlayer(0);
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		const bool bReady = player != nullptr && !player->IsAwaitingTerrainCollision() &&
			(terrainGenerator == nullptr || terrainGenerator->IsLoadComplete());
		if (player != nullptr)
		{
			// Mouse movement (& the jump when the cursor is captured) mustn't turn the restored view
			player->GetTransform()->SetWorldRotation(m_LoadRotation);
			player->SetPitch(m_LoadPitch);
			if (player->GetController() != nullptr)
			{
				player->GetController()->ResetLookInput();
			}
		}
		if (terrainGenerator != nullptr && terrainGenerator->IsLoadComplete())
		{
			StartupTimer::Milestone("World terrain loaded");
		}
		if (player != nullptr && !player->IsAwaitingTerrainCollision())
		{
			StartupTimer::Milestone("Player placed on terrain");
		}
		// (Shown briefly at least, so it doesn't just flash)
		if ((bReady && m_LoadingTime > 0.3f) || m_LoadingTime > 45.0f)
		{
			// (Only the first world loaded is a startup milestone)
			const std::string loadInfo = "(world load took " + FloatToString(StartupTimer::Elapsed() - m_WorldLoadStartMs, 0) + " ms" + (bReady ? "" : ", timed out") + ")";
			StartupTimer::Milestone("In world (playable)", loadInfo.c_str());
			Print("World \"%s\" loaded in %.0f ms%s\n", m_CurrentWorld.name.c_str(), StartupTimer::Elapsed() - m_WorldLoadStartMs, bReady ? "" : " (timed out)");
			if (player != nullptr)
			{
				const glm::vec3 playerPos = player->GetTransform()->GetWorldPosition();
				const glm::vec3 forward = player->GetTransform()->GetForward();
				Print("Player at (%.2f, %.2f, %.2f), heading %.1f deg, pitch %.1f deg\n", playerPos.x, playerPos.y, playerPos.z,
					glm::degrees(std::atan2(forward.x, forward.z)), glm::degrees(player->GetPitch()));
			}
			if (getenv("FLEX_STARTUP_PROFILE") != nullptr && Profiler::IsEnabled())
			{
				Print("Main thread while loading the world:");
				Profiler::PrintReport(true);
				Profiler::SetEnabled(false);
				Profiler::Reset();
			}
			// For measuring startup times: FLEX_STARTUP_EXIT=world quits as soon as the world is playable
			if (const char* exitAt = getenv("FLEX_STARTUP_EXIT"))
			{
				if (strcmp(exitAt, "world") == 0)
				{
					g_EngineInstance->Stop();
				}
			}
			SetScreen(Screen::PLAYING);
			return;
		}

		m_UI.FillRectGradient(UIRect(0.0f, 0.0f, m_UI.Width(), m_UI.Height()), UITheme::WithAlpha(UITheme::SURFACE, 1.0f / UITheme::SURFACE.a), UITheme::INK);
		const glm::vec2 center(m_UI.Width() * 0.5f, m_UI.Height() * 0.5f);
		m_UI.Text(m_CurrentWorld.name, center.x, center.y - 60.0f * s, UITheme::TEXT_H1 * s, UITheme::TEXT_PRIMARY, TextAlign::CENTER, true);
		m_UI.Text(LOC(Loading_Terrain), center.x, center.y + 10.0f * s, UITheme::TEXT_BODY * s, UITheme::TEXT_SECONDARY, TextAlign::CENTER);

		// Spinner: dots around a circle, the leading ones brightest
		const u32 dotCount = 10;
		for (u32 i = 0; i < dotCount; ++i)
		{
			const real angle = TWO_PI * (real)i / (real)dotCount;
			const real phase = glm::fract(m_LoadingTime * 0.9f - (real)i / (real)dotCount);
			const glm::vec2 pos = center + glm::vec2(std::cos(angle), std::sin(angle)) * 26.0f * s + glm::vec2(0.0f, 90.0f * s);
			glm::vec4 colour = UITheme::ACCENT;
			colour.a = 0.2f + 0.8f * (1.0f - phase);
			m_UI.Circle(pos, 4.5f * s, colour);
		}
	}

	void Game::DrawPauseMenu()
	{
		const real s = m_UI.Scale();
		m_UI.FillRect(UIRect(0.0f, 0.0f, m_UI.Width(), m_UI.Height()), UITheme::WithAlpha(UITheme::INK, 0.55f));

		const real panelWidth = 460.0f * s;
		const real buttonHeight = UITheme::BUTTON_HEIGHT * s;
		const real gap = UITheme::SPACING * s;
		// Sized to fit the title, six buttons & padding
		const real panelHeight = 116.0f * s + 6.0f * (buttonHeight + gap) - gap + 56.0f * s;
		const UIRect panel(m_UI.Width() * 0.5f - panelWidth * 0.5f, m_UI.Height() * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight);
		m_UI.Panel(panel);
		UIRect content = panel.Inset(28.0f * s);
		m_UI.Text(LOC(Pause_Title), content.x + content.w * 0.5f, content.y + 34.0f * s, UITheme::TEXT_H1 * s, UITheme::TEXT_PRIMARY, TextAlign::CENTER, true);
		m_UI.Text(LOCF(Pause_Played, m_CurrentWorld.name.c_str(), FormatPlayTime(m_CurrentWorld.playTime).c_str()), content.x + content.w * 0.5f, content.y + 84.0f * s, UITheme::TEXT_SMALL * s, UITheme::TEXT_SECONDARY, TextAlign::CENTER);
		content.CutTop(116.0f * s);

		if (m_UI.Button("resume", content.CutTop(buttonHeight, gap), LOC(Pause_Resume), true))
		{
			SetScreen(Screen::PLAYING);
			return;
		}
		if (m_UI.Button("map", content.CutTop(buttonHeight, gap), LOC(Pause_Map)))
		{
			SetScreen(Screen::MAP);
			return;
		}
		if (m_UI.Button("settings", content.CutTop(buttonHeight, gap), LOC(Pause_Settings)))
		{
			m_ScreenBeforeSettings = Screen::PAUSED;
			SetScreen(Screen::SETTINGS);
			g_EngineInstance->SetSimulationPaused(true);
			return;
		}
		if (m_UI.Button("controls", content.CutTop(buttonHeight, gap), LOC(Pause_Controls)))
		{
			m_ScreenBeforeSettings = Screen::PAUSED;
			SetScreen(Screen::CONTROLS);
			return;
		}
		if (m_UI.Button("menu", content.CutTop(buttonHeight, gap), LOC(Pause_QuitToMenu)))
		{
			LeaveWorld();
			return;
		}
		if (m_UI.Button("quit", content.CutTop(buttonHeight, gap), LOC(Pause_QuitGame)))
		{
			SaveWorld();
			g_EngineInstance->Stop();
		}
	}

	void Game::DrawMap()
	{
		const real s = m_UI.Scale();
		BaseScene* scene = g_SceneManager->CurrentScene();
		Player* player = scene->GetPlayer(0);
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (player == nullptr || terrainGenerator == nullptr)
		{
			SetScreen(Screen::PLAYING);
			return;
		}

		const glm::vec3 playerPos = player->GetTransform()->GetWorldPosition();
		const glm::vec2 screenSize(m_UI.Width(), m_UI.Height());
		const real mapSize = glm::min(m_UI.Width(), m_UI.Height()) * 0.78f;
		const UIRect mapRect(m_UI.Width() * 0.5f - mapSize * 0.5f, m_UI.Height() * 0.5f - mapSize * 0.5f + 10.0f * s, mapSize, mapSize);
		const glm::vec2 mouse = m_UI.MousePos();

		// Zoom with the scroll wheel (about the map's center)
		if (m_UI.ScrollDelta() != 0.0f)
		{
			m_MapExtent = glm::clamp(m_MapExtent * (m_UI.ScrollDelta() > 0.0f ? 0.8f : 1.25f), 300.0f, 24000.0f);
		}

		// Pan by dragging with the middle mouse button, C re-centers on the player
		if (g_InputManager->IsMouseButtonPressed(MouseButton::MIDDLE) && mapRect.Contains(mouse))
		{
			m_bMapPanning = true;
			m_MapPanStartMouse = mouse;
			m_MapPanStartCenter = m_MapViewCenter;
		}
		if (m_bMapPanning)
		{
			if (!g_InputManager->IsMouseButtonDown(MouseButton::MIDDLE))
			{
				m_bMapPanning = false;
			}
			else
			{
				const glm::vec2 delta = mouse - m_MapPanStartMouse;
				const real metersPerPixel = 2.0f * m_MapExtent / mapSize;
				// Screen +y is south (-z)
				m_MapViewCenter = m_MapPanStartCenter + glm::vec2(-delta.x, delta.y) * metersPerPixel;
				if (glm::length(delta) > 2.0f * s)
				{
					m_bMapFollowPlayer = false;
				}
			}
		}
		if (g_InputManager->GetKeyPressed(GameKeys::MAP_RECENTER))
		{
			m_bMapFollowPlayer = true;
			m_bMapPanning = false;
		}
		if (m_bMapFollowPlayer && !m_bMapPanning)
		{
			m_MapViewCenter = glm::vec2(playerPos.x, playerPos.z);
		}
		const glm::vec2 viewCenter = m_MapViewCenter;
		const real viewExtent = m_MapExtent;

		// Regenerate (in the background, ~40ms) when the view changes. Until then the last map is shown moved & scaled
		// to where it belongs, so panning & zooming respond immediately.
		const bool bViewChanged = m_MapRequestExtent != viewExtent ||
			glm::distance(glm::vec2(m_MapRequestCenter.x, m_MapRequestCenter.z), viewCenter) > viewExtent * 0.005f;
		if (bViewChanged && !terrainGenerator->IsGeneratingLargeMap())
		{
			m_MapRequestCenter = glm::vec3(viewCenter.x, playerPos.y, viewCenter.y);
			m_MapRequestExtent = viewExtent;
			terrainGenerator->RequestLargeMap(m_MapRequestCenter, viewExtent, 384);
		}

		// World (xz) <-> screen pixels for the current view. North (+z) is up.
		auto WorldToMap = [&](const glm::vec2& p)
		{
			return glm::vec2(mapRect.x + (p.x - (viewCenter.x - viewExtent)) / (2.0f * viewExtent) * mapRect.w,
				mapRect.y + ((viewCenter.y + viewExtent) - p.y) / (2.0f * viewExtent) * mapRect.h);
		};
		auto MapToWorld = [&](const glm::vec2& px)
		{
			return glm::vec2(viewCenter.x - viewExtent + (px.x - mapRect.x) / mapRect.w * 2.0f * viewExtent,
				viewCenter.y + viewExtent - (px.y - mapRect.y) / mapRect.h * 2.0f * viewExtent);
		};

		// The map texture is drawn before the UI, & the backdrop (drawn around it with a hole for the map) hides any
		// part of it outside the view
		const real aspectRatio = screenSize.x / screenSize.y;
		auto PxToSprite = [&](const glm::vec2& px) { return glm::vec3((px.x / screenSize.x * 2.0f - 1.0f) * aspectRatio, 1.0f - px.y / screenSize.y * 2.0f, 0.0f); };
		{
			SpriteQuadDrawInfo base = {};
			base.anchor = AnchorPoint::CENTER;
			base.bScreenSpace = true;
			base.bReadDepth = false;
			base.bEnableAlbedoSampler = false;
			base.zOrder = 10;
			base.colour = UITheme::Hex(0x23272E);
			base.pos = PxToSprite(mapRect.Center());
			base.scale = glm::vec3(mapSize / screenSize.y, mapSize / screenSize.y, 1.0f);
			g_Renderer->EnqueueSprite(base);
		}
		TextureID mapTextureID;
		glm::vec3 mapCenter;
		real mapExtent;
		const bool bHaveMap = terrainGenerator->GetLargeMap(mapTextureID, mapCenter, mapExtent);
		if (bHaveMap)
		{
			SpriteQuadDrawInfo mapSprite = {};
			mapSprite.textureID = mapTextureID;
			mapSprite.anchor = AnchorPoint::CENTER;
			mapSprite.bScreenSpace = true;
			mapSprite.bReadDepth = false;
			mapSprite.zOrder = 11;
			mapSprite.pos = PxToSprite(WorldToMap(glm::vec2(mapCenter.x, mapCenter.z)));
			const real halfSizePx = mapSize * 0.5f * mapExtent / viewExtent;
			mapSprite.scale = glm::vec3(halfSizePx * 2.0f / screenSize.y, halfSizePx * 2.0f / screenSize.y, 1.0f);
			g_Renderer->EnqueueSprite(mapSprite);
		}

		// Backdrop (opaque, the overlapping pieces leave no seams) & the map's frame
		const real border = 6.0f * s;
		const UIRect frame = mapRect.Inset(-border);
		const glm::vec4 backdrop = UITheme::INK;
		m_UI.FillRect(UIRect(0.0f, 0.0f, m_UI.Width(), frame.y + 1.0f), backdrop);
		m_UI.FillRect(UIRect(0.0f, frame.y + frame.h - 1.0f, m_UI.Width(), m_UI.Height() - (frame.y + frame.h) + 1.0f), backdrop);
		m_UI.FillRect(UIRect(0.0f, frame.y - 1.0f, frame.x + 1.0f, frame.h + 2.0f), backdrop);
		m_UI.FillRect(UIRect(frame.x + frame.w - 1.0f, frame.y - 1.0f, m_UI.Width() - (frame.x + frame.w) + 1.0f, frame.h + 2.0f), backdrop);
		const glm::vec4 frameColour = UITheme::WithAlpha(UITheme::SURFACE_TOP, 1.0f / UITheme::SURFACE_TOP.a);
		m_UI.FillRect(UIRect(frame.x, frame.y, frame.w, border + 1.0f), frameColour, UITheme::RADIUS_S * s);
		m_UI.FillRect(UIRect(frame.x, mapRect.y + mapRect.h - 1.0f, frame.w, border + 1.0f), frameColour, UITheme::RADIUS_S * s);
		m_UI.FillRect(UIRect(frame.x, frame.y, border + 1.0f, frame.h), frameColour, UITheme::RADIUS_S * s);
		m_UI.FillRect(UIRect(mapRect.x + mapRect.w - 1.0f, frame.y, border + 1.0f, frame.h), frameColour, UITheme::RADIUS_S * s);

		m_UI.Text(LOC(Map_Title), frame.x, frame.y - 34.0f * s, UITheme::TEXT_H2 * s, UITheme::TEXT_PRIMARY, TextAlign::LEFT, true);
		m_UI.Text(LOC(Map_Controls), frame.x + frame.w, frame.y - 34.0f * s,
			UITheme::TEXT_CAPTION * s, UITheme::TEXT_SECONDARY, TextAlign::RIGHT);

		// Scale: the distance across the view
		{
			m_UI.Text(LOCF(Map_Scale, 2.0f * viewExtent / 1000.0f), frame.x, frame.y + frame.h + 30.0f * s, UITheme::TEXT_CAPTION * s, UITheme::TEXT_SECONDARY);
		}

		// Legend (left of the map, when there's room)
		if (frame.x > 220.0f * s)
		{
			const real iconX = frame.x - 28.0f * s;
			const real textX = iconX - 20.0f * s;
			real y = frame.y + frame.h - 8.0f * s;
			const real rowHeight = 30.0f * s;
			// (Text is positioned by its baseline, icons by their centers)
			auto Row = [&](const std::string& label)
			{
				m_UI.Text(label, textX, y, UITheme::TEXT_CAPTION * s, UITheme::TEXT_SECONDARY, TextAlign::RIGHT);
				const glm::vec2 iconPx(iconX, y - 6.0f * s);
				y -= rowHeight;
				return iconPx;
			};
			const glm::vec2 openedPx = Row(LOC(Map_LegendOpened));
			EnqueueOverlayQuad(openedPx, glm::vec2(7.0f * s), UITheme::INK, screenSize);
			EnqueueOverlayQuad(openedPx, glm::vec2(5.0f * s), UITheme::Hex(0x8C8273), screenSize);
			const glm::vec2 treasurePx = Row(LOC(Map_LegendTreasure));
			EnqueueOverlayQuad(treasurePx, glm::vec2(7.0f * s), UITheme::INK, screenSize);
			EnqueueOverlayQuad(treasurePx, glm::vec2(5.0f * s), UITheme::GOLD, screenSize);
			terrainGenerator->DrawMapWaypoint(Row(LOC(Map_LegendWaypoint)), 0, 18.0f * s, 90);
			terrainGenerator->DrawMapPlayerArrow(Row(LOC(Map_LegendYou)), glm::vec2(0.0f, -1.0f), 26.0f * s, 90);
		}

		if (!bHaveMap)
		{
			m_UI.Text(LOC(Map_Mapping), mapRect.x + mapRect.w * 0.5f, mapRect.y + mapRect.h * 0.5f, UITheme::TEXT_BODY_LARGE * s, UITheme::TEXT_SECONDARY, TextAlign::CENTER);
		}

		bool bWaypointClick = false;
		// Treasure chests found so far
		std::vector<glm::vec3> chestPositions;
		std::vector<bool> chestOpened;
		TreasureChest::GetKnownSites(chestPositions, chestOpened);
		// What the mouse is over (the closest marker within reach), for the tooltip
		std::string hoverTip;
		real hoverTipDistance = 12.0f * s;
		auto ConsiderTip = [&](const glm::vec2& px, const std::string& text)
		{
			const real d = glm::distance(px, mouse);
			if (d < hoverTipDistance && mapRect.Contains(mouse))
			{
				hoverTipDistance = d;
				hoverTip = text;
			}
		};
		for (u32 i = 0; i < (u32)chestPositions.size(); ++i)
		{
			const glm::vec2 px = WorldToMap(glm::vec2(chestPositions[i].x, chestPositions[i].z));
			if (mapRect.Inset(4.0f * s).Contains(px))
			{
				EnqueueOverlayQuad(px, glm::vec2(7.0f * s), UITheme::INK, screenSize);
				EnqueueOverlayQuad(px, glm::vec2(5.0f * s), chestOpened[i] ? UITheme::Hex(0x8C8273) : UITheme::GOLD, screenSize);
				const real chestDistance = glm::distance(glm::vec2(playerPos.x, playerPos.z), glm::vec2(chestPositions[i].x, chestPositions[i].z));
				ConsiderTip(px, LOCF(Map_TipChest, chestOpened[i] ? LOC(Map_LegendOpened) : LOC(Map_LegendTreasure), chestDistance / 1000.0f));
			}
		}

		// Waypoints (right click places one at the cursor, or removes the one under it)
		{
			TerrainGenerator::MapWaypoint* waypoints = TerrainGenerator::GetMapWaypoints();
			for (u32 i = 0; i < TerrainGenerator::MAX_MAP_WAYPOINTS; ++i)
			{
				if (!waypoints[i].bUsed)
				{
					continue;
				}
				const glm::vec2 px = WorldToMap(waypoints[i].pos);
				if (mapRect.Inset(4.0f * s).Contains(px))
				{
					terrainGenerator->DrawMapWaypoint(px, i, 20.0f * s, 90);
					m_UI.Text(std::to_string(i + 1), px.x + 13.0f * s, px.y - 13.0f * s, UITheme::TEXT_CAPTION * s, UITheme::TEXT_PRIMARY);
					ConsiderTip(px, LOCF(Map_TipWaypoint, (i32)(i + 1), glm::distance(glm::vec2(playerPos.x, playerPos.z), waypoints[i].pos) / 1000.0f));
				}
			}

			const bool bClick = g_InputManager->IsMouseButtonPressed(GameKeys::MAP_WAYPOINT) || g_InputManager->GetKeyPressed(GameKeys::WAYPOINT);
			if (bClick && m_ScreenTime > 0.1f && mapRect.Contains(mouse) && !m_bMapPanning)
			{
				// (Removing is for the waypoint under the cursor)
				ToggleWaypoint(MapToWorld(mouse), 2.0f * 12.0f * s * viewExtent / mapRect.w);
				bWaypointClick = true;
			}
		}

		ConsiderTip(WorldToMap(glm::vec2(playerPos.x, playerPos.z)), LOC(Map_LegendYou));

		// The player & their heading (pinned to the edge when outside the view)
		{
			const glm::vec2 actual = WorldToMap(glm::vec2(playerPos.x, playerPos.z));
			const UIRect inner = mapRect.Inset(12.0f * s);
			const glm::vec2 px = glm::clamp(actual, glm::vec2(inner.x, inner.y), glm::vec2(inner.x + inner.w, inner.y + inner.h));
			const bool bInView = px == actual;
			const glm::vec3 look = player->GetLookDirection();
			glm::vec2 heading(look.x, -look.z);
			heading = glm::length(heading) > 1e-4f ? glm::normalize(heading) : glm::vec2(0.0f, -1.0f);
			const real size = bInView ? 1.0f : 0.75f;
			terrainGenerator->DrawMapPlayerArrow(px, heading, 30.0f * s * size, 90);
		}

		// Hovered point: what's there, & teleporting to it
		if (mapRect.Contains(mouse) && !m_bMapPanning)
		{
			const glm::vec2 target = MapToWorld(mouse);
			EnqueueOverlayQuad(mouse, glm::vec2(10.0f * s, 1.0f * s), UITheme::WithAlpha(UITheme::TEXT_PRIMARY, 0.8f), screenSize);
			EnqueueOverlayQuad(mouse, glm::vec2(1.0f * s, 10.0f * s), UITheme::WithAlpha(UITheme::TEXT_PRIMARY, 0.8f), screenSize);

			const terrain::ColumnSample column = terrainGenerator->SampleColumn(target.x, target.y);
			const std::string description = terrainGenerator->DescribeLocation(glm::vec3(target.x, column.height, target.y));
			const real distance = glm::distance(glm::vec2(playerPos.x, playerPos.z), target);
			m_UI.Text(LOCF(Map_Hover, description.c_str(), column.height, distance / 1000.0f), frame.x + frame.w, frame.y + frame.h + 30.0f * s, UITheme::TEXT_SMALL * s, UITheme::TEXT_PRIMARY, TextAlign::RIGHT);

			if (m_UI.IsMousePressed() && !bWaypointClick)
			{
				// Above the surface; the player is placed on the ground below (or the nearest dry land)
				player->TeleportOnTerrain(glm::vec3(target.x, glm::max(column.height, column.waterLevel) + 250.0f, target.y));
				player->ShowNotification(LOCF(Map_Teleported, distance / 1000.0f));
				SetScreen(Screen::PLAYING);
				return;
			}
		}

		if (!hoverTip.empty())
		{
			m_UI.Tooltip(hoverTip);
		}
		if (!m_MapMessage.empty() && m_ScreenTime - m_MapMessageTime < 3.0f)
		{
			m_UI.Text(m_MapMessage, frame.x + frame.w * 0.3f, frame.y + frame.h + 30.0f * s, UITheme::TEXT_SMALL * s, UITheme::EMBER, TextAlign::CENTER);
		}

		if (g_InputManager->GetKeyPressed(GameKeys::MAP) && m_ScreenTime > 0.1f)
		{
			SetScreen(Screen::PLAYING);
		}
	}

	// Places a waypoint in the first free slot, or removes the one within removeRadius (metres) of pos
	void Game::ToggleWaypoint(const glm::vec2& pos, real removeRadius)
	{
		TerrainGenerator::MapWaypoint* waypoints = TerrainGenerator::GetMapWaypoints();
		i32 closest = -1;
		real closestDistance = removeRadius;
		for (u32 i = 0; i < TerrainGenerator::MAX_MAP_WAYPOINTS; ++i)
		{
			const real d = glm::distance(waypoints[i].pos, pos);
			if (waypoints[i].bUsed && d <= closestDistance)
			{
				closestDistance = d;
				closest = (i32)i;
			}
		}

		std::string message;
		if (closest >= 0)
		{
			waypoints[closest].bUsed = false;
			message = LOCF(Waypoint_Removed, closest + 1);
		}
		else
		{
			i32 freeSlot = -1;
			for (u32 i = 0; i < TerrainGenerator::MAX_MAP_WAYPOINTS && freeSlot < 0; ++i)
			{
				if (!waypoints[i].bUsed)
				{
					freeSlot = (i32)i;
				}
			}
			if (freeSlot < 0)
			{
				message = LOC(Waypoint_Full);
			}
			else
			{
				waypoints[freeSlot].bUsed = true;
				waypoints[freeSlot].pos = pos;
				message = LOCF(Waypoint_Placed, freeSlot + 1);
			}
		}

		m_MapMessage = message;
		m_MapMessageTime = m_ScreenTime;
		if (m_Screen == Screen::PLAYING)
		{
			Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
			if (player != nullptr)
			{
				player->ShowNotification(message);
			}
		}
	}

	// Low, medium, high, ultra
	static const struct GraphicsPreset
	{
		i32 shadowQuality;
		i32 drawDistance;
		i32 terrainQuality;
		i32 antiAliasing;
		bool bAmbientOcclusion;
		i32 effectsQuality;
	} s_GraphicsPresets[4] = {
		{ 1, 1, 0, 1, false, 0 },
		{ 2, 2, 0, 1, true, 1 },
		{ 3, 3, 1, 1, true, 1 }, // The defaults
		{ 4, 3, 3, 2, true, 1 },
	};

	i32 Game::GetGraphicsPreset() const
	{
		for (i32 i = 0; i < GRAPHICS_PRESET_COUNT; ++i)
		{
			const GraphicsPreset& p = s_GraphicsPresets[i];
			if (p.shadowQuality == m_Settings.shadowQuality && p.drawDistance == m_Settings.drawDistance && p.terrainQuality == m_Settings.terrainQuality &&
				p.antiAliasing == m_Settings.antiAliasing && p.bAmbientOcclusion == m_Settings.bAmbientOcclusion && p.effectsQuality == m_Settings.effectsQuality)
			{
				return i;
			}
		}
		return -1;
	}

	void Game::ApplyGraphicsPreset(i32 preset)
	{
		const GraphicsPreset& p = s_GraphicsPresets[glm::clamp(preset, 0, GRAPHICS_PRESET_COUNT - 1)];
		m_Settings.shadowQuality = p.shadowQuality;
		m_Settings.drawDistance = p.drawDistance;
		m_Settings.terrainQuality = p.terrainQuality;
		m_Settings.antiAliasing = p.antiAliasing;
		m_Settings.bAmbientOcclusion = p.bAmbientOcclusion;
		m_Settings.effectsQuality = p.effectsQuality;
	}

	void Game::ApplySettings()
	{
		BaseCamera* firstPersonCamera = g_CameraManager->GetCameraByName("first-person");
		if (firstPersonCamera != nullptr)
		{
			firstPersonCamera->FOV = glm::radians(m_Settings.fov);
			// The camera doesn't update while the simulation is paused, so the new FOV is applied now (as the slider is dragged)
			firstPersonCamera->RecalculateViewProjection();
		}
		AudioManager::SetMasterGain(m_Settings.masterVolume);
		GameSounds::SetMusicVolume(m_Settings.musicVolume);
		if (!Loc::SetLanguage(m_Settings.language))
		{
			m_Settings.language = Loc::GetLanguage();
		}
		g_Renderer->SetTerrainQuality(m_Settings.terrainQuality);
		m_Settings.drawDistance = glm::clamp(m_Settings.drawDistance, 0, (i32)ARRAY_LENGTH(DRAW_DISTANCE_SCALES) - 1);
		g_Renderer->SetDrawDistanceScale(DRAW_DISTANCE_SCALES[m_Settings.drawDistance]);
		g_Renderer->SetShadowQuality(m_Settings.shadowQuality);
		g_Renderer->SetAntiAliasing(m_Settings.antiAliasing);
		g_Renderer->SetSSAOEnabled(m_Settings.bAmbientOcclusion);
		if (g_Renderer->GetShaderQualityLevel() != m_Settings.effectsQuality)
		{
			g_Renderer->SetShaderQualityLevel(glm::clamp(m_Settings.effectsQuality, 0, 1));
		}
		if (g_Renderer->IsVSyncEnabled() != m_Settings.bVSync)
		{
			g_Renderer->SetVSyncEnabled(m_Settings.bVSync);
		}

		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		if (player != nullptr)
		{
			player->GetController()->SetMouseSensitivity(m_Settings.mouseSensitivity, m_Settings.bInvertMouse);
		}
	}

	void Game::EnterEditor()
	{
		g_EngineInstance->SetRenderImGui(true);
		g_EngineInstance->SetRenderingEditorObjects(true);
		SetScreen(Screen::NONE);
	}

	void Game::RequestAutoPlay(const std::string& worldName)
	{
		s_bAutoPlay = true;
		s_AutoPlayWorldName = worldName;
	}

	void Game::AutoPlay()
	{
		s_bAutoPlay = false;

		// Matches world names or folder names, ignoring case
		auto Lower = [](std::string str) { return ToLower(str); };
		const std::string wanted = Lower(s_AutoPlayWorldName);
		const std::string wantedFolder = Lower(SanitizeName(s_AutoPlayWorldName));
		for (const WorldInfo& world : m_Worlds)
		{
			std::string folderName = world.directory;
			while (!folderName.empty() && (folderName.back() == '/' || folderName.back() == '\\'))
			{
				folderName.pop_back();
			}
			folderName = Lower(folderName.substr(folderName.find_last_of("/\\") + 1));
			if (wanted.empty() || Lower(world.name) == wanted || folderName == wanted || folderName == wantedFolder)
			{
				Print("Playing world \"%s\" (%s)\n", world.name.c_str(), world.directory.c_str());
				PlayWorld(world);
				return;
			}
		}

		if (!wanted.empty())
		{
			// Doesn't exist yet: create it
			Print("No world named \"%s\", creating it\n", s_AutoPlayWorldName.c_str());
			// FLEX_PLAY_SEED=<seed> makes runs comparable (e.g. benchmarks of a newly created world)
			CreateWorld(s_AutoPlayWorldName, getenv("FLEX_PLAY_SEED") != nullptr ? atoi(getenv("FLEX_PLAY_SEED")) : RandomSeed());
		}
		else if (m_Worlds.empty())
		{
			Print("No worlds to play, creating one\n");
			CreateWorld(NextWorldName(), RandomSeed());
		}
	}

	std::string Game::NextWorldName() const
	{
		// Finds the highest n among worlds named like the default name (e.g. "World %d"). A first world with another
		// name counts as 1, so the next is "World 2".
		const std::string pattern = std::string(LOC(Worlds_DefaultName)) + "%n";
		i32 highest = m_Worlds.empty() ? 0 : 1;
		for (const WorldInfo& world : m_Worlds)
		{
			i32 n = 0;
			i32 length = -1;
			if (sscanf(world.name.c_str(), pattern.c_str(), &n, &length) == 1 && length == (i32)world.name.size())
			{
				highest = glm::max(highest, n);
			}
		}
		return LOCF(Worlds_DefaultName, highest + 1);
	}

	void Game::RefreshWorlds()
	{
		m_Worlds.clear();

		const std::string worldsDirectory = WorldsDirectory();
		Platform::CreateDirectoryRecursive(worldsDirectory);
		std::vector<std::string> filePaths;
		if (!Platform::FindFilesInDirectory(worldsDirectory, filePaths, "*", true))
		{
			return;
		}

		// Each world's folder has a world.bin (or its backup), or only a world.json if it hasn't been saved since
		// the binary format was added
		const u64 listStart = save::Ticks();
		std::set<std::string> worldDirectories;
		for (const std::string& filePath : filePaths)
		{
			if (EndsWith(filePath, "/world.bin") || EndsWith(filePath, "/world.bin.bak") || EndsWith(filePath, "/world.json"))
			{
				worldDirectories.insert(ExtractDirectoryString(filePath));
			}
		}

		for (const std::string& worldDirectory : worldDirectories)
		{
			JSONObject worldObj;
			if (save::ReadWorldFile(worldDirectory, worldObj, true) == save::WorldFileSource::NONE)
			{
				continue;
			}
			WorldInfo info = {};
			info.directory = worldDirectory;
			worldObj.TryGetString("name", info.name);
			worldObj.TryGetInt("seed", info.seed);
			worldObj.TryGetString("last played", info.lastPlayed);
			worldObj.TryGetFloat("play time", info.playTime);
			worldObj.TryGetString("created", info.created);
			worldObj.TryGetInt("terrain version", info.terrainVersion);
			info.stats.Parse(worldObj);
			m_Worlds.push_back(info);
		}

		std::sort(m_Worlds.begin(), m_Worlds.end(), [](const WorldInfo& a, const WorldInfo& b) { return a.lastPlayed > b.lastPlayed; });
		Print("Listed %u worlds in %.2f ms\n", (u32)m_Worlds.size(), save::MsSince(listStart));
	}

	void Game::CreateWorld(const std::string& name, i32 seed)
	{
		// A unique folder for it
		const std::string base = WorldsDirectory() + SanitizeName(name);
		std::string directory = base + "/";
		for (i32 i = 2; save::WorldFileExists(directory); ++i)
		{
			directory = base + "_" + IntToString(i) + "/";
		}
		Platform::CreateDirectoryRecursive(directory);

		JSONObject worldObj = {};
		worldObj.fields.emplace_back("version", JSONValue(WORLD_FILE_VERSION));
		worldObj.fields.emplace_back("name", JSONValue(name));
		worldObj.fields.emplace_back("seed", JSONValue(seed));
		worldObj.fields.emplace_back("terrain version", JSONValue((i32)terrain::GENERATOR_VERSION));
		worldObj.fields.emplace_back("created", JSONValue(Platform::GetDateString_YMDHMS()));
		worldObj.fields.emplace_back("last played", JSONValue(Platform::GetDateString_YMDHMS()));
		if (!save::WriteWorldFile(directory, worldObj))
		{
			PrintError("Failed to create world at %s\n", directory.c_str());
			return;
		}

		RefreshWorlds();
		for (const WorldInfo& world : m_Worlds)
		{
			if (world.directory == directory)
			{
				PlayWorld(world);
				return;
			}
		}
	}

	void Game::ClearWorldObjects()
	{
		// (also removes the tracks' meshes from the scene)
		GetSystem<TrackManager>(SystemType::TRACK_MANAGER)->ClearTracks();

		BaseScene* scene = g_SceneManager->CurrentScene();
		std::vector<GameObject*> toRemove;
		for (GameObject* rootObject : scene->GetRootObjects())
		{
			if (rootObject->GetTypeID() == PlayerSID || Contains(m_SceneObjectIDs, rootObject->ID))
			{
				continue;
			}
			toRemove.push_back(rootObject);
		}
		scene->RemoveObjects(toRemove, true);
		TreasureChest::DestroyAll();
	}

	void Game::PlayWorld(const WorldInfo& world)
	{
		if (m_bWorldLoaded)
		{
			SaveWorld();
			ClearWorldObjects();
		}

		BaseScene* scene = g_SceneManager->CurrentScene();
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();

		JSONObject worldObj;
		save::WorldFileStats fileStats;
		const save::WorldFileSource source = save::ReadWorldFile(world.directory, worldObj, false, &fileStats);
		if (source == save::WorldFileSource::NONE)
		{
			PrintError("Failed to load world from %s\n", world.directory.c_str());
			return;
		}
		Print("Read world file (%s, %u bytes) in %.2f ms\n", save::WorldFileSourceToString(source), (u32)fileStats.sizeBytes, fileStats.ioMs);

		m_WorldLoadStartMs = StartupTimer::Elapsed();
		m_CurrentWorld = world;
		m_bWorldLoaded = true;
		m_AutosaveTimer = 0.0f;
		m_LoadingTime = 0.0f;
		m_MapRequestExtent = 0.0f;

		if (terrainGenerator != nullptr)
		{
			terrainGenerator->SetWorld(world.seed, world.directory);
		}

		real timeOfDay = 0.08f; // Late morning
		worldObj.TryGetFloat("time of day", timeOfDay);
		scene->SetTimeOfDay(timeOfDay);
		scene->SetTimeOfDayPaused(false);

		// Animals (tamed ones, & the wild ones near where the player was)
		Animals::Load(worldObj);

		{
			std::string harvested;
			worldObj.TryGetString("harvested", harvested);
			harvest::Deserialize(harvested);
		}

		// Chests already looted
		std::set<u64>& openedChests = TreasureChest::GetOpenedChests();
		openedChests.clear();
		std::vector<JSONField> chestFields;
		if (worldObj.TryGetFieldArray("opened chests", chestFields))
		{
			for (const JSONField& field : chestFields)
			{
				openedChests.insert((u64)std::stoull(field.value.AsString()));
			}
		}

		{
			TerrainGenerator::MapWaypoint* waypoints = TerrainGenerator::GetMapWaypoints();
			for (u32 i = 0; i < TerrainGenerator::MAX_MAP_WAYPOINTS; ++i)
			{
				waypoints[i].bUsed = false;
			}
			std::string waypointStr;
			worldObj.TryGetString("waypoints", waypointStr);
			const char* c = waypointStr.c_str();
			u32 slot;
			float x, z;
			i32 consumed = 0;
			while (sscanf(c, "%u,%f,%f;%n", &slot, &x, &z, &consumed) == 3)
			{
				c += consumed;
				if (slot < TerrainGenerator::MAX_MAP_WAYPOINTS)
				{
					waypoints[slot].bUsed = true;
					waypoints[slot].pos = glm::vec2(x, z);
				}
			}
		}

		// Stats (parsed again, the list's copy may be older)
		m_CurrentWorld.stats.Parse(worldObj);
		m_ExploredCells.clear();
		m_bHaveLastPlayerPos = false;
		std::string explored;
		if (worldObj.TryGetString("explored", explored))
		{
			for (size_t i = 0; i + 8 <= explored.size(); i += 8)
			{
				m_ExploredCells.insert((u32)strtoul(explored.substr(i, 8).c_str(), nullptr, 16));
			}
		}

		// Tracks (before the objects, as carts refer to them)
		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
		JSONObject tracksObj;
		if (worldObj.TryGetObject("tracks", tracksObj))
		{
			trackManager->InitializeFromJSON(tracksObj);
		}
		else
		{
			trackManager->ClearTracks();
		}

		// Everything built & placed in the world
		std::vector<JSONObject> objects;
		if (worldObj.TryGetObjectArray("objects", objects))
		{
			using CopyFlags = GameObject::CopyFlags;
			const CopyFlags copyFlags = (CopyFlags)(CopyFlags::ALL & ~CopyFlags::ADD_TO_SCENE);
			for (const JSONObject& objectJSON : objects)
			{
				GameObject* object = GameObject::CreateObjectFromJSON(objectJSON, scene, scene->GetSceneFileVersion(), InvalidPrefabIDPair, false, copyFlags);
				if (object != nullptr)
				{
					scene->AddRootObject(object);
				}
			}
		}

		// The player
		glm::vec3 position(0.0f, 600.0f, 0.0f);
		glm::quat rotation = QUAT_IDENTITY;
		real pitch = 0.0f;
		JSONObject playerObj;
		const bool bHasPlayer = worldObj.TryGetObject("player", playerObj);
		bool bHasPosition = false;
		if (bHasPlayer)
		{
			bHasPosition = playerObj.TryGetVec3("position", position);
			// Saved as a quaternion (x, y, z, w). JSONObject::GetQuat expects euler angles (as transforms are saved),
			// so this is read directly.
			for (const JSONField& field : playerObj.fields)
			{
				if (field.label == "rotation" && field.value.type == ValueType::VEC4)
				{
					const glm::vec4& v = field.value.vecValue;
					const glm::vec3 forward = glm::quat(v.w, v.x, v.y, v.z) * VEC3_FORWARD;
					// Only the yaw (the player is always upright)
					if (glm::length(glm::vec2(forward.x, forward.z)) > 1e-4f)
					{
						rotation = glm::angleAxis(std::atan2(forward.x, forward.z), VEC3_UP);
					}
				}
			}
			playerObj.TryGetFloat("pitch", pitch);
		}
		if (!bHasPosition && terrainGenerator != nullptr)
		{
			// A new world (or one saved without a position): start on flat, low ground near the origin, found from
			// the density alone. The player is held there until the terrain's collision below has loaded.
			position = terrainGenerator->FindSpawnPoint() + VEC3_UP * 2.0f;
		}

		g_CameraManager->SetCameraByName("first-person", false);
		Player* player = scene->GetPlayer(0);
		if (player == nullptr)
		{
			player = scene->SpawnPlayer(position, rotation * VEC3_FORWARD);
		}
		if (player->GetRidingVehicleID().IsValid())
		{
			player->SetRidingVehicle(nullptr);
		}
		player->SetVisible(true);
		player->GetRigidBody()->SetWorldPosition(position);
		player->GetTransform()->SetWorldPosition(position);
		player->GetTransform()->SetWorldRotation(rotation);
		player->SetPitch(pitch);
		m_LoadRotation = rotation;
		m_LoadPitch = pitch;
		// A saved position is kept exactly where it's clear of the terrain (it may be on top of something built)
		player->TeleportOnTerrain(position, bHasPosition);
		player->UpdateIsPossessed();

		JSONObject inventoryObj;
		if (worldObj.TryGetObject("inventory", inventoryObj))
		{
			player->ParseInventory(inventoryObj);
		}
		else
		{
			// A fresh start: just the basic tools (in the first quick access slots)
			player->ResetInventory();
			player->AddStarterKit();
		}

		ApplySettings();
		SetScreen(Screen::LOADING);
		g_Window->SetCursorMode(CursorMode::DISABLED);
	}

	void Game::SaveWorld()
	{
		if (!m_bWorldLoaded)
		{
			return;
		}
		m_AutosaveTimer = 0.0f;

		BaseScene* scene = g_SceneManager->CurrentScene();
		if (scene == nullptr || scene->GetFileName() != GAMEPLAY_SCENE_NAME)
		{
			return;
		}

		const u64 saveStart = save::Ticks();
		m_CurrentWorld.lastPlayed = Platform::GetDateString_YMDHMS();

		JSONObject worldObj = {};
		worldObj.fields.emplace_back("version", JSONValue(WORLD_FILE_VERSION));
		worldObj.fields.emplace_back("name", JSONValue(m_CurrentWorld.name));
		worldObj.fields.emplace_back("seed", JSONValue(m_CurrentWorld.seed));
		worldObj.fields.emplace_back("terrain version", JSONValue(m_CurrentWorld.terrainVersion));
		worldObj.fields.emplace_back("last played", JSONValue(m_CurrentWorld.lastPlayed));
		worldObj.fields.emplace_back("play time", JSONValue(m_CurrentWorld.playTime, 1));
		if (!m_CurrentWorld.created.empty())
		{
			worldObj.fields.emplace_back("created", JSONValue(m_CurrentWorld.created));
		}
		worldObj.fields.emplace_back("time of day", JSONValue(scene->GetTimeOfDay(), 4));

		Player* player = scene->GetPlayer(0);
		if (player != nullptr)
		{
			JSONObject playerObj = {};
			glm::vec3 position = player->GetTransform()->GetWorldPosition();
			if (player->GetRidingVehicleID().IsValid())
			{
				// Saved standing next to the vehicle
				position += VEC3_UP * 2.0f;
			}
			playerObj.fields.emplace_back("position", JSONValue(VecToString(position, 3)));
			playerObj.fields.emplace_back("rotation", JSONValue(QuatToString(player->GetTransform()->GetWorldRotation(), 4)));
			playerObj.fields.emplace_back("pitch", JSONValue(player->GetPitch(), 3));
			worldObj.fields.emplace_back("player", JSONValue(playerObj));
			worldObj.fields.emplace_back("inventory", JSONValue(player->SerializeInventoryToJSON()));
		}

		std::vector<JSONObject> objects;
		for (GameObject* rootObject : scene->GetRootObjects())
		{
			if (rootObject->GetTypeID() == PlayerSID || !rootObject->IsSerializable() || Contains(m_SceneObjectIDs, rootObject->ID))
			{
				continue;
			}
			objects.push_back(rootObject->Serialize(scene, true, false));
		}
		worldObj.fields.emplace_back("objects", JSONValue(objects));
		worldObj.fields.emplace_back("tracks", JSONValue(GetSystem<TrackManager>(SystemType::TRACK_MANAGER)->Serialize()));

		std::vector<JSONField> chestFields;
		for (u64 chest : TreasureChest::GetOpenedChests())
		{
			chestFields.emplace_back("", JSONValue(std::to_string(chest)));
		}
		worldObj.fields.emplace_back("opened chests", JSONValue(chestFields));
		worldObj.fields.emplace_back("harvested", JSONValue(harvest::Serialize()));
		Animals::Serialize(worldObj);
		{
			// Waypoints, as "slot,x,z;" for each one placed
			std::string waypointStr;
			const TerrainGenerator::MapWaypoint* waypoints = TerrainGenerator::GetMapWaypoints();
			for (u32 i = 0; i < TerrainGenerator::MAX_MAP_WAYPOINTS; ++i)
			{
				if (waypoints[i].bUsed)
				{
					char buffer[96];
					snprintf(buffer, ARRAY_LENGTH(buffer), "%u,%.2f,%.2f;", i, waypoints[i].pos.x, waypoints[i].pos.y);
					waypointStr += buffer;
				}
			}
			worldObj.fields.emplace_back("waypoints", JSONValue(waypointStr));
		}

		m_CurrentWorld.stats.chestsOpened = (u32)TreasureChest::GetOpenedChests().size();
		m_CurrentWorld.stats.exploredCells = (u32)m_ExploredCells.size();
		m_CurrentWorld.stats.Serialize(worldObj);
		{
			// Explored cells, as 8 hex digits each (see UpdateExplored)
			std::string explored;
			explored.reserve(m_ExploredCells.size() * 8);
			char cellStr[16];
			for (u32 cell : m_ExploredCells)
			{
				snprintf(cellStr, ARRAY_LENGTH(cellStr), "%08X", cell);
				explored += cellStr;
			}
			worldObj.fields.emplace_back("explored", JSONValue(explored));
		}

		save::WorldFileStats fileStats;
		// (Written on another thread: flushing it to disk can take 10+ ms)
		if (!save::WriteWorldFile(m_CurrentWorld.directory, worldObj, &fileStats, true))
		{
			PrintError("Failed to save world to %s\n", m_CurrentWorld.directory.c_str());
		}

		const u64 editsStart = save::Ticks();
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (terrainGenerator != nullptr)
		{
			terrainGenerator->FlushEdits();
		}
		const ms editsMs = save::MsSince(editsStart);
		const ms worldMs = (ms)((double)(editsStart - saveStart) / 1e6);
		Print("Saved world (%u bytes, %u objects) in %.2f ms (gather %.2f, encode %.2f, start write %.2f, terrain edits %.2f)\n",
			(u32)fileStats.sizeBytes, (u32)objects.size(), worldMs + editsMs, worldMs - fileStats.encodeMs - fileStats.ioMs, fileStats.encodeMs, fileStats.ioMs, editsMs);
	}

	void Game::LeaveWorld()
	{
		SaveWorld();
		if (TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive())
		{
			// So the world shows up immediately next time it's played (& not overwritten by terrain around the menu's
			// camera on shutdown)
			terrainGenerator->SaveSnapshot(true);
		}
		ClearWorldObjects();
		m_bWorldLoaded = false;

		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		if (player != nullptr)
		{
			if (player->GetRidingVehicleID().IsValid())
			{
				player->SetRidingVehicle(nullptr);
			}
			player->SetVisible(false);
		}
		g_CameraManager->SetCameraByName("debug", false);
		if (player != nullptr)
		{
			player->UpdateIsPossessed();
		}

		RefreshWorlds();
		SetScreen(Screen::MAIN_MENU);
	}

	void Game::DeleteWorld(const WorldInfo& world)
	{
		save::WaitForPendingWrites();
		std::vector<std::string> filePaths;
		if (Platform::FindFilesInDirectory(world.directory, filePaths, "*", true))
		{
			for (const std::string& filePath : filePaths)
			{
				Platform::DeleteFile(filePath, false);
			}
		}
		Platform::DeleteFile(world.directory + "world.json", false);
		Platform::DeleteEmptyDirectory(world.directory);
		RefreshWorlds();
	}
} // namespace flex
