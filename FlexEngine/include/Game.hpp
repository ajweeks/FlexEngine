#pragma once

#include "Callbacks/InputCallbacks.hpp"
#include "ConfigFile.hpp"
#include "GameUI.hpp"

// The game: main menu, worlds (each a seed with its own saved progress), settings, the pause menu & the map. Engine
// test scenes are reached from the main menu (or by launching with --editor).
namespace flex
{
	class Game final
	{
	public:
		Game();
		~Game();

		void Initialize();
		void Destroy();

		// Called after the camera has updated (positions the menu camera)
		void LateUpdate();
		// Called every frame after the scene has updated, draws menus & screens
		void Update();

		bool IsPlaying() const { return m_Screen == Screen::PLAYING; }
		// Whether a menu is taking input instead of the player
		bool IsMenuOpen() const { return m_Screen != Screen::PLAYING && m_Screen != Screen::NONE; }

		// Saves the current world (if one is being played)
		void SaveWorld();

		static const char* GAMEPLAY_SCENE_NAME;

	private:
		enum class Screen
		{
			NONE, // Engine test scenes (the editor)
			MAIN_MENU,
			WORLDS,
			NEW_WORLD,
			SETTINGS,
			LOADING,
			PLAYING,
			PAUSED,
			MAP,
		};

		struct WorldInfo
		{
			std::string directory; // Absolute, ending in a slash
			std::string name;
			i32 seed = 0;
			std::string lastPlayed; // YYYY-MM-DD_HH-MM-SS, sorts chronologically
			real playTime = 0.0f;
		};

		struct Settings
		{
			real fov = 70.0f;
			real mouseSensitivity = 1.0f;
			bool bInvertMouse = false;
			real masterVolume = 0.5f;
			i32 terrainQuality = 1;
			bool bShowMinimap = true;
		};

		EventReply OnActionEvent(Action action, ActionEvent actionEvent);
		ActionCallback<Game> m_ActionCallback;

		void SetScreen(Screen screen);
		void DrawMainMenu();
		void DrawWorlds();
		void DrawNewWorld();
		void DrawSettings();
		void DrawLoading();
		void DrawPauseMenu();
		void DrawMap();
		void DrawBackground(real opacity);
		void DrawTitle(const std::string& title, const std::string& subtitle);

		void RefreshWorlds();
		void CreateWorld(const std::string& name, i32 seed);
		void PlayWorld(const WorldInfo& world);
		void LeaveWorld(); // Saves & returns to the main menu
		void DeleteWorld(const WorldInfo& world);
		void ClearWorldObjects();
		void ApplySettings();
		void EnterEditor();

		GameUI m_UI;
		Screen m_Screen = Screen::NONE;
		Screen m_ScreenBeforeSettings = Screen::MAIN_MENU;
		real m_ScreenTime = 0.0f;

		std::vector<WorldInfo> m_Worlds;
		i32 m_SelectedWorld = -1;
		bool m_bConfirmDelete = false;
		std::string m_NewWorldName;
		std::string m_NewWorldSeed;

		// The world being played
		bool m_bWorldLoaded = false;
		WorldInfo m_CurrentWorld;
		sec m_AutosaveTimer = 0.0f;
		sec m_LoadingTime = 0.0f;
		// Root objects which belong to the scene itself (everything else was placed in a world, & is saved with it)
		std::vector<GameObjectID> m_SceneObjectIDs;

		// Map
		real m_MapExtent = 2000.0f; // Half width shown (meters)
		glm::vec3 m_MapRequestCenter = glm::vec3(0.0f);
		real m_MapRequestExtent = 0.0f;

		// Main menu camera
		real m_MenuCameraAngle = 0.0f;

		// Automated walk through every screen, saving screenshots (enabled with the FLEX_GAME_TOUR environment variable)
		void UpdateTour();
		bool m_bTour = false;
		i32 m_TourStep = 0;
		sec m_TourTime = 0.0f;
		glm::vec3 m_TourCavePos = glm::vec3(0.0f);

		Settings m_Settings;
		ConfigFile m_SettingsFile;
	};

	extern Game* g_Game;
} // namespace flex
