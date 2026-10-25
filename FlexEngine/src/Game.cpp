#include "stdafx.hpp"

#include "Game.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletDynamics/Dynamics/btRigidBody.h>
IGNORE_WARNINGS_POP

#include "Audio/AudioManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "FlexEngine.hpp"
#include "Graphics/Renderer.hpp"
#include "InputManager.hpp"
#include "JSONParser.hpp"
#include "Physics/RigidBody.hpp"
#include "Platform/Platform.hpp"
#include "Player.hpp"
#include "PlayerController.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Objects/Building.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/Objects/Treasure.hpp"
#include "Scene/SceneManager.hpp"
#include "Time.hpp"
#include "Window/Window.hpp"

namespace flex
{
	Game* g_Game = nullptr;

	const char* Game::GAMEPLAY_SCENE_NAME = "scene_world.json";

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
				return IntToString(minutes) + " min";
			}
			return IntToString(minutes / 60) + " h " + IntToString(minutes % 60) + " min";
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
			return (i32)(((u64)Time::CurrentMilliseconds() * 2654435761ull) & 0x7FFFFFFF);
		}

		// Untextured quad drawn after the UI (so it's on top of sprites like the map), centered on a pixel position
		void EnqueueOverlayQuad(const glm::vec2& centerPx, const glm::vec2& halfSizePx, const glm::vec4& colour, const glm::vec2& screenSize)
		{
			const real aspectRatio = screenSize.x / screenSize.y;
			SpriteQuadDrawInfo drawInfo = {};
			drawInfo.anchor = AnchorPoint::CENTER;
			drawInfo.bScreenSpace = true;
			drawInfo.bReadDepth = false;
			drawInfo.bEnableAlbedoSampler = false;
			drawInfo.zOrder = 90;
			drawInfo.colour = colour;
			drawInfo.pos = glm::vec3((centerPx.x / screenSize.x * 2.0f - 1.0f) * aspectRatio, 1.0f - centerPx.y / screenSize.y * 2.0f, 0.0f);
			drawInfo.scale = glm::vec3(halfSizePx.x / screenSize.y * 2.0f, halfSizePx.y / screenSize.y * 2.0f, 1.0f);
			g_Renderer->EnqueueSprite(drawInfo);
		}
	} // anonymous namespace

	Game::Game() :
		m_ActionCallback(this, &Game::OnActionEvent),
		m_SettingsFile("Game settings", SAVED_DIRECTORY "game_settings.json", 1)
	{
		m_SettingsFile.RegisterProperty("field of view", &m_Settings.fov).SetRange(50.0f, 110.0f);
		m_SettingsFile.RegisterProperty("mouse sensitivity", &m_Settings.mouseSensitivity).SetRange(0.1f, 4.0f);
		m_SettingsFile.RegisterProperty("invert mouse", &m_Settings.bInvertMouse);
		m_SettingsFile.RegisterProperty("master volume", &m_Settings.masterVolume).SetRange(0.0f, 1.0f);
		m_SettingsFile.RegisterProperty("terrain quality", &m_Settings.terrainQuality);
		m_SettingsFile.RegisterProperty("show minimap", &m_Settings.bShowMinimap);
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

		m_bTour = getenv("FLEX_GAME_TOUR") != nullptr;
		if (const char* overlay = getenv("FLEX_DEBUG_OVERLAY"))
		{
			g_Renderer->SetDebugOverlayID(atoi(overlay));
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

		switch (m_TourStep)
		{
		case 0: if (m_TourTime > 8.0f) { Screenshot("main_menu"); Next(); } break;
		case 1: if (m_TourTime > 0.5f) { m_ScreenBeforeSettings = Screen::MAIN_MENU; SetScreen(Screen::SETTINGS); Next(); } break;
		case 2: if (m_TourTime > 1.0f) { Screenshot("settings"); Next(); } break;
		case 3: if (m_TourTime > 0.5f) { m_NewWorldName = "Tour world"; m_NewWorldSeed = "4242"; SetScreen(Screen::NEW_WORLD); Next(); } break;
		case 4: if (m_TourTime > 1.0f) { Screenshot("new_world"); Next(); } break;
		case 5: if (m_TourTime > 0.5f) { CreateWorld(m_NewWorldName, 4242); Next(); } break;
		case 6: if (m_TourTime > 0.4f) { Screenshot("loading"); Next(); } break;
		case 7: if (m_Screen == Screen::PLAYING && m_TourTime > 6.0f) { Screenshot("playing"); Next(); } break;
		case 8:
		{
			// A little hut in front of the player, with a torch inside, a ladder up the side & a rope off the roof
			Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
			if (player != nullptr && m_TourTime > 0.5f)
			{
				const glm::vec3 look = player->GetLookDirection();
				const glm::vec3 heading = glm::normalize(glm::vec3(look.x, 0.0f, look.z) + glm::vec3(1e-4f, 0.0f, 0.0f));
				const glm::vec3 base = player->GetTransform()->GetWorldPosition() - VEC3_UP * (player->GetHeight() * 0.5f) + heading * 9.0f;
				const real yaw = std::atan2(heading.x, heading.z);
				const glm::quat facing = glm::angleAxis(yaw, VEC3_UP);
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
				Place(BuildPieceType::ROOF, BuildMaterial::WOOD, glm::vec3(0.0f, 3.0f, 0.0f), PI, 0.0f);
				Place(BuildPieceType::STAIRS, BuildMaterial::STONE, glm::vec3(3.0f, 0.0f, 0.0f), 0.0f, 0.0f);
				Place(BuildPieceType::TORCH, BuildMaterial::WOOD, glm::vec3(0.0f, 0.2f, 0.0f), 0.0f, 0.0f);
				Place(BuildPieceType::TORCH, BuildMaterial::WOOD, glm::vec3(-1.8f, 0.0f, -1.8f), 0.0f, 0.0f);
				Place(BuildPieceType::LADDER, BuildMaterial::WOOD, glm::vec3(-1.62f, 0.0f, 0.5f), PI_DIV_TWO, 4.5f);
				Place(BuildPieceType::ROPE, BuildMaterial::WOOD, glm::vec3(1.0f, 6.0f, -2.0f), 0.0f, 6.0f);
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
		case 14: if (m_TourTime > 0.5f) { SetScreen(Screen::MAP); Next(); } break;
		case 15:
		{
			TextureID textureID;
			glm::vec3 center;
			real extent;
			TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
			if ((terrainGenerator != nullptr && terrainGenerator->GetLargeMap(textureID, center, extent) && m_TourTime > 1.0f) || m_TourTime > 20.0f)
			{
				Screenshot("map");
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
		case 20: if (m_TourTime > 1.5f) { Screenshot("worlds"); Next(); } break;
		case 21: if (m_TourTime > 1.0f) { g_EngineInstance->Stop(); Next(); } break;
		default: break;
		}
	}

	void Game::Destroy()
	{
		SaveWorld();
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
			g_EngineInstance->SetSimulationPaused(true);
			g_Window->SetCursorMode(CursorMode::NORMAL);
			break;
		case Screen::SETTINGS:
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

		camera->position = position;
		camera->forward = glm::normalize(target - position);
		camera->CalculateYawAndPitchFromForward();
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
		if (m_bTour)
		{
			UpdateTour();
		}
		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (terrainGenerator != nullptr)
		{
			terrainGenerator->UpdateLargeMap();
		}
		*TerrainGenerator::GetShowMinimapPtr() = m_Settings.bShowMinimap;

		switch (m_Screen)
		{
		case Screen::MAIN_MENU: DrawMainMenu(); break;
		case Screen::WORLDS: DrawWorlds(); break;
		case Screen::NEW_WORLD: DrawNewWorld(); break;
		case Screen::SETTINGS: DrawSettings(); break;
		case Screen::LOADING: DrawLoading(); break;
		case Screen::PAUSED: DrawPauseMenu(); break;
		case Screen::MAP: DrawMap(); break;
		case Screen::PLAYING:
		{
			m_CurrentWorld.playTime += g_DeltaTime;
			m_AutosaveTimer += g_UnpausedDeltaTime;
			if (m_AutosaveTimer > AUTOSAVE_INTERVAL)
			{
				SaveWorld();
			}
			Player* player = scene->GetPlayer(0);
			if (g_InputManager->GetKeyPressed(KeyCode::KEY_M) && player != nullptr && !player->IsAnyInventoryShowing())
			{
				SetScreen(Screen::MAP);
			}
		} break;
		default:
			break;
		}
	}

	void Game::DrawBackground(real opacity)
	{
		const real s = m_UI.Scale();
		// Darkens the edges & bottom, leaving the world visible behind
		m_UI.FillRectGradient(UIRect(0.0f, 0.0f, m_UI.Width(), m_UI.Height()), glm::vec4(0.02f, 0.03f, 0.05f, 0.25f * opacity), glm::vec4(0.02f, 0.03f, 0.05f, 0.75f * opacity));
		m_UI.FillRectGradient(UIRect(0.0f, 0.0f, 760.0f * s, m_UI.Height()), glm::vec4(0.0f, 0.0f, 0.0f, 0.35f * opacity), glm::vec4(0.0f, 0.0f, 0.0f, 0.5f * opacity));
	}

	void Game::DrawTitle(const std::string& title, const std::string& subtitle)
	{
		const real s = m_UI.Scale();
		// Slides in when the screen opens
		const real t = glm::clamp(m_ScreenTime * 3.0f, 0.0f, 1.0f);
		const real slide = (1.0f - t * t * (3.0f - 2.0f * t)) * 40.0f * s;
		m_UI.Text(title, 120.0f * s - slide, 220.0f * s, 80.0f * s, glm::vec4(UITheme::TEXT_MAIN.r, UITheme::TEXT_MAIN.g, UITheme::TEXT_MAIN.b, t), TextAlign::LEFT, true);
		m_UI.FillRect(UIRect(124.0f * s - slide, 290.0f * s, 120.0f * s * t, 5.0f * s), UITheme::ACCENT, 2.5f * s);
		m_UI.Text(subtitle, 124.0f * s - slide, 330.0f * s, 22.0f * s, glm::vec4(UITheme::TEXT_DIM.r, UITheme::TEXT_DIM.g, UITheme::TEXT_DIM.b, t));
	}

	void Game::DrawMainMenu()
	{
		const real s = m_UI.Scale();
		DrawBackground(1.0f);
		DrawTitle("FLEX", "An endless world to explore, climb & build in");

		UIRect column(120.0f * s, 420.0f * s, 440.0f * s, 600.0f * s);
		const real buttonHeight = UITheme::BUTTON_HEIGHT * s;
		const real gap = UITheme::SPACING * s;

		if (!m_Worlds.empty())
		{
			if (m_UI.Button("continue", column.CutTop(buttonHeight, gap), "Continue: " + m_Worlds[0].name, true))
			{
				PlayWorld(m_Worlds[0]);
				return;
			}
		}
		if (m_UI.Button("worlds", column.CutTop(buttonHeight, gap), m_Worlds.empty() ? "New world" : "Worlds", m_Worlds.empty()))
		{
			if (m_Worlds.empty())
			{
				m_NewWorldName = "My world";
				m_NewWorldSeed = IntToString(RandomSeed());
				SetScreen(Screen::NEW_WORLD);
			}
			else
			{
				m_SelectedWorld = 0;
				SetScreen(Screen::WORLDS);
			}
		}
		if (m_UI.Button("settings", column.CutTop(buttonHeight, gap), "Settings"))
		{
			m_ScreenBeforeSettings = Screen::MAIN_MENU;
			SetScreen(Screen::SETTINGS);
		}
		if (m_UI.Button("editor", column.CutTop(buttonHeight, gap), "Engine test scenes"))
		{
			EnterEditor();
			return;
		}
		if (m_UI.Button("quit", column.CutTop(buttonHeight, gap), "Quit"))
		{
			g_EngineInstance->Stop();
		}

		m_UI.Text("[Esc] Back   [WASD] Move   [Space] Jump/glide   [Shift] Sprint/climb   [B] Build   [M] Map", 120.0f * s, m_UI.Height() - 50.0f * s, 15.0f * s, UITheme::TEXT_DIM);
	}

	void Game::DrawWorlds()
	{
		const real s = m_UI.Scale();
		DrawBackground(1.0f);
		DrawTitle("Worlds", "Every world is endless, & made from its seed");

		const UIRect panel(120.0f * s, 400.0f * s, 820.0f * s, 600.0f * s);
		m_UI.Panel(panel);
		UIRect content = panel.Inset(24.0f * s);

		// World list
		UIRect list = content.CutTop(content.h - 80.0f * s, 16.0f * s);
		const real rowHeight = 78.0f * s;
		const u32 visibleRows = (u32)(list.h / (rowHeight + 10.0f * s));
		for (u32 i = 0; i < (u32)m_Worlds.size() && i < visibleRows; ++i)
		{
			const WorldInfo& world = m_Worlds[i];
			const std::string id = "world" + IntToString(i);
			const std::string subtitle = "Seed " + IntToString(world.seed) + "   |   Played " + FormatPlayTime(world.playTime) + "   |   Last played " + FormatDate(world.lastPlayed);
			if (m_UI.ListItem(id.c_str(), list.CutTop(rowHeight, 10.0f * s), world.name, subtitle, (i32)i == m_SelectedWorld))
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
		if (m_UI.Button("play", UIRect(buttons.x, buttons.y, buttonWidth, buttons.h), "Play", true, bSelected))
		{
			PlayWorld(m_Worlds[m_SelectedWorld]);
			return;
		}
		if (m_UI.Button("new", UIRect(buttons.x + (buttonWidth + 12.0f * s), buttons.y, buttonWidth, buttons.h), "New world"))
		{
			m_NewWorldName = "World " + IntToString((i32)m_Worlds.size() + 1);
			m_NewWorldSeed = IntToString(RandomSeed());
			SetScreen(Screen::NEW_WORLD);
		}
		if (m_UI.Button("delete", UIRect(buttons.x + (buttonWidth + 12.0f * s) * 2.0f, buttons.y, buttonWidth, buttons.h), m_bConfirmDelete ? "Really delete?" : "Delete", false, bSelected))
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
		if (m_UI.Button("back", UIRect(buttons.x + (buttonWidth + 12.0f * s) * 3.0f, buttons.y, buttonWidth, buttons.h), "Back"))
		{
			SetScreen(Screen::MAIN_MENU);
		}
	}

	void Game::DrawNewWorld()
	{
		const real s = m_UI.Scale();
		DrawBackground(1.0f);
		DrawTitle("New world", "Name it & pick a seed (or keep the random one)");

		const UIRect panel(120.0f * s, 400.0f * s, 640.0f * s, 400.0f * s);
		m_UI.Panel(panel);
		UIRect content = panel.Inset(28.0f * s);
		const real fieldHeight = 60.0f * s;

		m_UI.Text("World name", content.x, content.y + 12.0f * s, 16.0f * s, UITheme::TEXT_DIM);
		content.CutTop(30.0f * s);
		m_UI.TextField("name", content.CutTop(fieldHeight, 20.0f * s), &m_NewWorldName, 32, "My world");

		m_UI.Text("Seed", content.x, content.y + 12.0f * s, 16.0f * s, UITheme::TEXT_DIM);
		content.CutTop(30.0f * s);
		UIRect seedRow = content.CutTop(fieldHeight, 28.0f * s);
		m_UI.TextField("seed", UIRect(seedRow.x, seedRow.y, seedRow.w - 160.0f * s, seedRow.h), &m_NewWorldSeed, 10, "Random");
		if (m_UI.Button("random", UIRect(seedRow.x + seedRow.w - 148.0f * s, seedRow.y, 148.0f * s, seedRow.h), "Random"))
		{
			m_NewWorldSeed = IntToString(RandomSeed());
		}

		const real buttonWidth = (content.w - 12.0f * s) * 0.5f;
		if (m_UI.Button("create", UIRect(content.x, content.y, buttonWidth, UITheme::BUTTON_HEIGHT * s), "Create & play", true, !m_NewWorldName.empty()))
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
		if (m_UI.Button("back", UIRect(content.x + buttonWidth + 12.0f * s, content.y, buttonWidth, UITheme::BUTTON_HEIGHT * s), "Back"))
		{
			SetScreen(m_Worlds.empty() ? Screen::MAIN_MENU : Screen::WORLDS);
		}
	}

	void Game::DrawSettings()
	{
		const real s = m_UI.Scale();
		if (m_ScreenBeforeSettings == Screen::PAUSED)
		{
			m_UI.FillRect(UIRect(0.0f, 0.0f, m_UI.Width(), m_UI.Height()), glm::vec4(0.0f, 0.0f, 0.0f, 0.55f));
		}
		else
		{
			DrawBackground(1.0f);
		}
		DrawTitle("Settings", "Saved automatically");

		const UIRect panel(120.0f * s, 400.0f * s, 700.0f * s, 560.0f * s);
		m_UI.Panel(panel);
		UIRect content = panel.Inset(24.0f * s);
		const real rowHeight = 62.0f * s;
		const real gap = 10.0f * s;

		bool bChanged = false;
		bChanged |= m_UI.Slider("fov", content.CutTop(rowHeight, gap), "Field of view", &m_Settings.fov, 50.0f, 110.0f, "%.0f deg");
		bChanged |= m_UI.Slider("sensitivity", content.CutTop(rowHeight, gap), "Mouse sensitivity", &m_Settings.mouseSensitivity, 0.1f, 4.0f, "%.2f");
		bChanged |= m_UI.Checkbox("invert", content.CutTop(rowHeight, gap), "Invert mouse", &m_Settings.bInvertMouse);
		real volume = m_Settings.masterVolume * 100.0f;
		if (m_UI.Slider("volume", content.CutTop(rowHeight, gap), "Volume", &volume, 0.0f, 100.0f, "%.0f%%"))
		{
			m_Settings.masterVolume = volume / 100.0f;
			bChanged = true;
		}
		real quality = (real)m_Settings.terrainQuality;
		static const char* qualityNames[] = { "Low", "Medium", "High", "Ultra" };
		if (m_UI.Slider("quality", content.CutTop(rowHeight, gap), std::string("Terrain detail: ") + qualityNames[glm::clamp(m_Settings.terrainQuality, 0, 3)], &quality, 0.0f, 3.0f, ""))
		{
			m_Settings.terrainQuality = glm::clamp((i32)glm::round(quality), 0, 3);
			bChanged = true;
		}
		bChanged |= m_UI.Checkbox("minimap", content.CutTop(rowHeight, gap), "Show minimap", &m_Settings.bShowMinimap);

		if (bChanged)
		{
			ApplySettings();
		}

		content.CutTop(8.0f * s);
		if (m_UI.Button("back", content.CutTop(UITheme::BUTTON_HEIGHT * s), "Back", true))
		{
			m_SettingsFile.Serialize();
			SetScreen(m_ScreenBeforeSettings);
			if (m_ScreenBeforeSettings == Screen::PAUSED)
			{
				g_EngineInstance->SetSimulationPaused(true);
			}
		}
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
		if ((bReady && m_LoadingTime > 1.0f) || m_LoadingTime > 45.0f)
		{
			SetScreen(Screen::PLAYING);
			return;
		}

		m_UI.FillRect(UIRect(0.0f, 0.0f, m_UI.Width(), m_UI.Height()), glm::vec4(0.03f, 0.035f, 0.05f, 1.0f));
		const glm::vec2 center(m_UI.Width() * 0.5f, m_UI.Height() * 0.5f);
		m_UI.Text(m_CurrentWorld.name, center.x, center.y - 60.0f * s, 44.0f * s, UITheme::TEXT_MAIN, TextAlign::CENTER, true);
		m_UI.Text("Generating terrain...", center.x, center.y + 10.0f * s, 18.0f * s, UITheme::TEXT_DIM, TextAlign::CENTER);

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
		m_UI.FillRect(UIRect(0.0f, 0.0f, m_UI.Width(), m_UI.Height()), glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));

		const real panelWidth = 460.0f * s;
		const real buttonHeight = UITheme::BUTTON_HEIGHT * s;
		const real gap = UITheme::SPACING * s;
		// Sized to fit the title, five buttons & padding
		const real panelHeight = 116.0f * s + 5.0f * (buttonHeight + gap) - gap + 56.0f * s;
		const UIRect panel(m_UI.Width() * 0.5f - panelWidth * 0.5f, m_UI.Height() * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight);
		m_UI.Panel(panel);
		UIRect content = panel.Inset(28.0f * s);
		m_UI.Text("Paused", content.x + content.w * 0.5f, content.y + 34.0f * s, 44.0f * s, UITheme::TEXT_MAIN, TextAlign::CENTER, true);
		m_UI.Text(m_CurrentWorld.name + " - " + FormatPlayTime(m_CurrentWorld.playTime) + " played", content.x + content.w * 0.5f, content.y + 84.0f * s, 16.0f * s, UITheme::TEXT_DIM, TextAlign::CENTER);
		content.CutTop(116.0f * s);

		if (m_UI.Button("resume", content.CutTop(buttonHeight, gap), "Resume", true))
		{
			SetScreen(Screen::PLAYING);
			return;
		}
		if (m_UI.Button("map", content.CutTop(buttonHeight, gap), "Map & teleport"))
		{
			SetScreen(Screen::MAP);
			return;
		}
		if (m_UI.Button("settings", content.CutTop(buttonHeight, gap), "Settings"))
		{
			m_ScreenBeforeSettings = Screen::PAUSED;
			SetScreen(Screen::SETTINGS);
			g_EngineInstance->SetSimulationPaused(true);
			return;
		}
		if (m_UI.Button("menu", content.CutTop(buttonHeight, gap), "Save & quit to menu"))
		{
			LeaveWorld();
			return;
		}
		if (m_UI.Button("quit", content.CutTop(buttonHeight, gap), "Save & quit game"))
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

		m_UI.FillRect(UIRect(0.0f, 0.0f, m_UI.Width(), m_UI.Height()), glm::vec4(0.02f, 0.025f, 0.035f, 0.82f));

		const glm::vec3 playerPos = player->GetTransform()->GetWorldPosition();

		// Zoom with the scroll wheel
		if (m_UI.ScrollDelta() != 0.0f)
		{
			m_MapExtent = glm::clamp(m_MapExtent * (m_UI.ScrollDelta() > 0.0f ? 0.8f : 1.25f), 300.0f, 24000.0f);
		}

		// Regenerate when zoomed or moved (generation happens in the background)
		const bool bStale = m_MapRequestExtent != m_MapExtent || glm::distance(glm::vec2(m_MapRequestCenter.x, m_MapRequestCenter.z), glm::vec2(playerPos.x, playerPos.z)) > m_MapExtent * 0.1f;
		if (bStale && !terrainGenerator->IsGeneratingLargeMap())
		{
			m_MapRequestCenter = playerPos;
			m_MapRequestExtent = m_MapExtent;
			terrainGenerator->RequestLargeMap(playerPos, m_MapExtent, 320);
		}

		const real mapSize = glm::min(m_UI.Width(), m_UI.Height()) * 0.8f;
		const UIRect mapRect(m_UI.Width() * 0.5f - mapSize * 0.5f, m_UI.Height() * 0.5f - mapSize * 0.5f + 20.0f * s, mapSize, mapSize);
		m_UI.Shadow(mapRect, 30.0f * s, 6.0f * s, 0.6f);
		m_UI.FillRect(mapRect.Inset(-6.0f * s), UITheme::PANEL_TOP, 8.0f * s);

		m_UI.Text("Map", mapRect.x, mapRect.y - 40.0f * s, 34.0f * s, UITheme::TEXT_MAIN, TextAlign::LEFT, true);
		m_UI.Text("[Scroll] Zoom   [Click] Teleport   [Esc / M] Close", mapRect.x + mapRect.w, mapRect.y - 40.0f * s, 16.0f * s, UITheme::TEXT_DIM, TextAlign::RIGHT);

		TextureID mapTextureID;
		glm::vec3 mapCenter;
		real mapExtent;
		if (!terrainGenerator->GetLargeMap(mapTextureID, mapCenter, mapExtent))
		{
			m_UI.Text("Mapping the area...", mapRect.x + mapRect.w * 0.5f, mapRect.y + mapRect.h * 0.5f, 22.0f * s, UITheme::TEXT_DIM, TextAlign::CENTER);
			if (g_InputManager->GetKeyPressed(KeyCode::KEY_M))
			{
				SetScreen(Screen::PLAYING);
			}
			return;
		}

		const glm::vec2 screenSize(m_UI.Width(), m_UI.Height());
		const real aspectRatio = screenSize.x / screenSize.y;
		{
			SpriteQuadDrawInfo mapSprite = {};
			mapSprite.textureID = mapTextureID;
			mapSprite.anchor = AnchorPoint::CENTER;
			mapSprite.bScreenSpace = true;
			mapSprite.bReadDepth = false;
			mapSprite.zOrder = 80;
			const glm::vec2 centerPx = mapRect.Center();
			mapSprite.pos = glm::vec3((centerPx.x / screenSize.x * 2.0f - 1.0f) * aspectRatio, 1.0f - centerPx.y / screenSize.y * 2.0f, 0.0f);
			mapSprite.scale = glm::vec3(mapSize / screenSize.y, mapSize / screenSize.y, 1.0f);
			g_Renderer->EnqueueSprite(mapSprite);
		}

		// World (xz) <-> map pixels. North (+z) is up.
		auto WorldToMap = [&](const glm::vec3& p)
		{
			return glm::vec2(mapRect.x + (p.x - (mapCenter.x - mapExtent)) / (2.0f * mapExtent) * mapRect.w,
				mapRect.y + ((mapCenter.z + mapExtent) - p.z) / (2.0f * mapExtent) * mapRect.h);
		};
		auto MapToWorld = [&](const glm::vec2& px)
		{
			return glm::vec2(mapCenter.x - mapExtent + (px.x - mapRect.x) / mapRect.w * 2.0f * mapExtent,
				mapCenter.z + mapExtent - (px.y - mapRect.y) / mapRect.h * 2.0f * mapExtent);
		};

		// Treasure chests found so far
		std::vector<glm::vec3> chestPositions;
		std::vector<bool> chestOpened;
		TreasureChest::GetKnownSites(chestPositions, chestOpened);
		for (u32 i = 0; i < (u32)chestPositions.size(); ++i)
		{
			const glm::vec2 px = WorldToMap(chestPositions[i]);
			if (mapRect.Contains(px))
			{
				EnqueueOverlayQuad(px, glm::vec2(7.0f * s), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), screenSize);
				EnqueueOverlayQuad(px, glm::vec2(5.0f * s), chestOpened[i] ? glm::vec4(0.5f, 0.45f, 0.35f, 1.0f) : glm::vec4(1.0f, 0.8f, 0.25f, 1.0f), screenSize);
			}
		}

		// The player & their heading
		{
			const glm::vec2 px = WorldToMap(playerPos);
			const glm::vec3 look = player->GetLookDirection();
			glm::vec2 heading(look.x, -look.z);
			heading = glm::length(heading) > 1e-4f ? glm::normalize(heading) : glm::vec2(0.0f, -1.0f);
			EnqueueOverlayQuad(px, glm::vec2(8.0f * s), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), screenSize);
			EnqueueOverlayQuad(px, glm::vec2(6.0f * s), glm::vec4(1.0f), screenSize);
			EnqueueOverlayQuad(px + heading * 16.0f * s, glm::vec2(4.0f * s), UITheme::ACCENT, screenSize);
		}

		// Hovered point: what's there, & teleporting to it
		const glm::vec2 mouse = m_UI.MousePos();
		if (mapRect.Contains(mouse))
		{
			const glm::vec2 target = MapToWorld(mouse);
			EnqueueOverlayQuad(mouse, glm::vec2(10.0f * s, 1.0f * s), glm::vec4(1.0f, 1.0f, 1.0f, 0.8f), screenSize);
			EnqueueOverlayQuad(mouse, glm::vec2(1.0f * s, 10.0f * s), glm::vec4(1.0f, 1.0f, 1.0f, 0.8f), screenSize);

			const terrain::ColumnSample column = terrainGenerator->SampleColumn(target.x, target.y);
			const std::string description = terrainGenerator->DescribeLocation(glm::vec3(target.x, column.height, target.y));
			const real distance = glm::distance(glm::vec2(playerPos.x, playerPos.z), target);
			char info[256];
			snprintf(info, ARRAY_LENGTH(info), "%s   |   %.0fm high   |   %.1fkm away   |   Click to teleport", description.c_str(), column.height, distance / 1000.0f);
			m_UI.Text(info, mapRect.x + mapRect.w * 0.5f, mapRect.y + mapRect.h + 30.0f * s, 16.0f * s, UITheme::TEXT_MAIN, TextAlign::CENTER);

			if (m_UI.IsMousePressed())
			{
				// Above the surface; the player is placed on the ground below (or the nearest dry land)
				player->TeleportOnTerrain(glm::vec3(target.x, glm::max(column.height, column.waterLevel) + 250.0f, target.y));
				player->ShowNotification("Teleported " + FloatToString(distance / 1000.0f, 1) + "km");
				SetScreen(Screen::PLAYING);
				return;
			}
		}

		if (g_InputManager->GetKeyPressed(KeyCode::KEY_M) && m_ScreenTime > 0.1f)
		{
			SetScreen(Screen::PLAYING);
		}
	}

	void Game::ApplySettings()
	{
		BaseCamera* firstPersonCamera = g_CameraManager->GetCameraByName("first-person");
		if (firstPersonCamera != nullptr)
		{
			firstPersonCamera->FOV = glm::radians(m_Settings.fov);
		}
		AudioManager::SetMasterGain(m_Settings.masterVolume);
		g_Renderer->SetTerrainQuality(m_Settings.terrainQuality);

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

	void Game::RefreshWorlds()
	{
		m_Worlds.clear();

		const std::string worldsDirectory = WorldsDirectory();
		Platform::CreateDirectoryRecursive(worldsDirectory);
		std::vector<std::string> filePaths;
		if (!Platform::FindFilesInDirectory(worldsDirectory, filePaths, "json", true))
		{
			return;
		}

		for (const std::string& filePath : filePaths)
		{
			if (!EndsWith(filePath, "world.json"))
			{
				continue;
			}
			JSONObject worldObj;
			if (!JSONParser::ParseFromFile(filePath, worldObj))
			{
				continue;
			}
			WorldInfo info = {};
			info.directory = ExtractDirectoryString(filePath);
			worldObj.TryGetString("name", info.name);
			worldObj.TryGetInt("seed", info.seed);
			worldObj.TryGetString("last played", info.lastPlayed);
			worldObj.TryGetFloat("play time", info.playTime);
			m_Worlds.push_back(info);
		}

		std::sort(m_Worlds.begin(), m_Worlds.end(), [](const WorldInfo& a, const WorldInfo& b) { return a.lastPlayed > b.lastPlayed; });
	}

	void Game::CreateWorld(const std::string& name, i32 seed)
	{
		// A unique folder for it
		const std::string base = WorldsDirectory() + SanitizeName(name);
		std::string directory = base + "/";
		for (i32 i = 2; FileExists(directory + "world.json"); ++i)
		{
			directory = base + "_" + IntToString(i) + "/";
		}
		Platform::CreateDirectoryRecursive(directory);

		JSONObject worldObj = {};
		worldObj.fields.emplace_back("version", JSONValue(WORLD_FILE_VERSION));
		worldObj.fields.emplace_back("name", JSONValue(name));
		worldObj.fields.emplace_back("seed", JSONValue(seed));
		worldObj.fields.emplace_back("created", JSONValue(Platform::GetDateString_YMDHMS()));
		worldObj.fields.emplace_back("last played", JSONValue(Platform::GetDateString_YMDHMS()));
		if (!WriteFile(directory + "world.json", worldObj.ToString(), false))
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
		if (!JSONParser::ParseFromFile(world.directory + "world.json", worldObj))
		{
			PrintError("Failed to load world from %s\n", world.directory.c_str());
			return;
		}

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
		if (bHasPlayer)
		{
			playerObj.TryGetVec3("position", position);
			playerObj.TryGetQuat("rotation", rotation);
			playerObj.TryGetFloat("pitch", pitch);
		}
		else if (terrainGenerator != nullptr)
		{
			position.y = 2000.0f;
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
		player->TeleportOnTerrain(position);
		player->UpdateIsPossessed();

		JSONObject inventoryObj;
		if (worldObj.TryGetObject("inventory", inventoryObj))
		{
			player->ParseInventory(inventoryObj);
		}
		else
		{
			// A fresh start: just a pickaxe
			player->ResetInventory();
			const PrefabID pickaxeID = g_ResourceManager->GetPrefabID("pickaxe");
			if (pickaxeID.IsValid())
			{
				player->AddToInventory(pickaxeID, 1);
			}
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

		m_CurrentWorld.lastPlayed = Platform::GetDateString_YMDHMS();

		JSONObject worldObj = {};
		worldObj.fields.emplace_back("version", JSONValue(WORLD_FILE_VERSION));
		worldObj.fields.emplace_back("name", JSONValue(m_CurrentWorld.name));
		worldObj.fields.emplace_back("seed", JSONValue(m_CurrentWorld.seed));
		worldObj.fields.emplace_back("last played", JSONValue(m_CurrentWorld.lastPlayed));
		worldObj.fields.emplace_back("play time", JSONValue(m_CurrentWorld.playTime, 1));
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

		std::vector<JSONField> chestFields;
		for (u64 chest : TreasureChest::GetOpenedChests())
		{
			chestFields.emplace_back("", JSONValue(std::to_string(chest)));
		}
		worldObj.fields.emplace_back("opened chests", JSONValue(chestFields));

		if (!WriteFile(m_CurrentWorld.directory + "world.json", worldObj.ToString(), false))
		{
			PrintError("Failed to save world to %s\n", m_CurrentWorld.directory.c_str());
		}

		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		if (terrainGenerator != nullptr)
		{
			terrainGenerator->FlushEdits();
		}
	}

	void Game::LeaveWorld()
	{
		SaveWorld();
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
		std::vector<std::string> filePaths;
		if (Platform::FindFilesInDirectory(world.directory, filePaths, "*", false))
		{
			for (const std::string& filePath : filePaths)
			{
				Platform::DeleteFile(filePath, false);
			}
		}
		Platform::DeleteFile(world.directory + "world.json", false);
		RefreshWorlds();
	}
} // namespace flex
