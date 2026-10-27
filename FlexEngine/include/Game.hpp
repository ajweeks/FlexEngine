#pragma once

#include "Callbacks/InputCallbacks.hpp"
#include "ConfigFile.hpp"
#include "GameUI.hpp"
#include "WorldStats.hpp"

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
		bool IsLoadingWorld() const { return m_Screen == Screen::LOADING; }
		// Whether a menu is taking input instead of the player
		bool IsMenuOpen() const { return m_Screen != Screen::PLAYING && m_Screen != Screen::NONE; }
		// False in the editor (engine test scenes, reached from the main menu)
		bool IsActive() const { return m_Screen != Screen::NONE; }

		// Saves the current world (if one is being played)
		void SaveWorld();

		static const char* GAMEPLAY_SCENE_NAME;
		// Shown in the window's title & the main menu
		static const char* GAME_NAME;
		// The game's version (separate from the engine's), shown on the main menu
		static const char* GAME_VERSION;

		// Call before Initialize: skips the main menu & loads straight into a world (--play[=<world name>]). An
		// empty name means the most recently played world (or a new one if there are none).
		static void RequestAutoPlay(const std::string& worldName);

	private:
		enum class Screen
		{
			NONE, // Engine test scenes (the editor)
			MAIN_MENU,
			WORLDS,
			NEW_WORLD,
			SETTINGS,
			CONTROLS,
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
			std::string created; // YYYY-MM-DD_HH-MM-SS (empty for worlds made before it was saved)
			WorldStats stats;
		};

		struct Settings
		{
			real fov = 70.0f;
			real mouseSensitivity = 1.0f;
			bool bInvertMouse = false;
			real masterVolume = 0.5f;
			i32 terrainQuality = 1;
			bool bShowMinimap = true;
			std::string language = "en"; // A Language code (see Localization.hpp)
		};

		EventReply OnActionEvent(Action action, ActionEvent actionEvent);
		ActionCallback<Game> m_ActionCallback;

		void SetScreen(Screen screen);
		void DrawMainMenu();
		void DrawWorlds();
		void DrawNewWorld();
		void DrawSettings();
		void DrawControls();
		void DrawLoading();
		void DrawPauseMenu();
		void DrawMap();
		void DrawBackground(real opacity);
		void DrawTitle(const std::string& title, const std::string& subtitle);
		void DrawWorldStats(const UIRect& panel, const WorldInfo& world);

		// Counts the explored area & distance travelled around the player (while playing)
		void UpdateExplored();

		void RefreshWorlds();
		void CreateWorld(const std::string& name, i32 seed);
		// "World <n>", one more than the highest numbered world
		std::string NextWorldName() const;
		void PlayWorld(const WorldInfo& world);
		void LeaveWorld(); // Saves & returns to the main menu
		void DeleteWorld(const WorldInfo& world);
		void ClearWorldObjects();
		void ApplySettings();
		void EnterEditor();

		void AutoPlay(); // Handles RequestAutoPlay

		static bool s_bAutoPlay;
		static std::string s_AutoPlayWorldName;

		GameUI m_UI;
		Screen m_Screen = Screen::NONE;
		Screen m_ScreenBeforeSettings = Screen::MAIN_MENU; // Where the settings & controls screens go back to
		real m_ScreenTime = 0.0f;

		std::vector<WorldInfo> m_Worlds;
		i32 m_SelectedWorld = -1;
		bool m_bConfirmDelete = false;
		real m_WorldListScroll = 0.0f; // In rows
		std::string m_NewWorldName;
		std::string m_NewWorldSeed;

		// The world being played
		bool m_bWorldLoaded = false;
		WorldInfo m_CurrentWorld;
		sec m_AutosaveTimer = 0.0f;
		sec m_PlayTimeRemainder = 0.0f; // Play time not yet added to m_CurrentWorld.playTime (see Update)
		sec m_LoadingTime = 0.0f;
		ms m_WorldLoadStartMs = 0.0f;
		friend WorldStats* GetCurrentWorldStats();
		// Packed cell coordinates (see UpdateExplored) of the current world's explored area
		std::unordered_set<u32> m_ExploredCells;
		glm::vec3 m_LastPlayerPos = glm::vec3(0.0f);
		bool m_bHaveLastPlayerPos = false;
		// The view direction restored when loading (kept while the loading screen is up)
		glm::quat m_LoadRotation = QUAT_IDENTITY;
		real m_LoadPitch = 0.0f;
		// Root objects which belong to the scene itself (everything else was placed in a world, & is saved with it)
		std::vector<GameObjectID> m_SceneObjectIDs;

		// Map
		real m_MapExtent = 2000.0f; // Half width shown (meters)
		glm::vec3 m_MapRequestCenter = glm::vec3(0.0f);
		real m_MapRequestExtent = 0.0f;
		glm::vec2 m_MapViewCenter = glm::vec2(0.0f); // World xz at the map's center
		bool m_bMapFollowPlayer = true; // Until panned (re-centered with C)
		bool m_bMapPanning = false; // Middle mouse drag
		glm::vec2 m_MapPanStartMouse = glm::vec2(0.0f);
		glm::vec2 m_MapPanStartCenter = glm::vec2(0.0f);

		// Main menu camera
		real m_MenuCameraAngle = 0.0f;

		// Automated walk through every screen, saving screenshots (enabled with the FLEX_GAME_TOUR environment variable)
		void UpdateTour();
		bool m_bTour = false;
		i32 m_TourStep = 0;
		sec m_TourTime = 0.0f;
		glm::vec3 m_TourCavePos = glm::vec3(0.0f);
		bool m_bTourMapCaptured = false; // The map screenshot was requested (the pan waits a frame, so it isn't in the shot)
		std::string m_TourWorldDirectory; // Deleted at the end of the tour
		// FLEX_BUILD_TEST: after the tour builds its hut, views it from several angles & shows placement previews
		void UpdateBuildTest();
		// A small hut with one of every build piece
		void BuildTourHut(const glm::vec3& base, real yaw);
		bool m_bBuildTest = false;
		i32 m_BuildTestStep = 0;
		glm::vec3 m_BuildTestBase = glm::vec3(0.0f);
		real m_BuildTestYaw = 0.0f;

		Settings m_Settings;
		ConfigFile m_SettingsFile;
	};

	extern Game* g_Game;
} // namespace flex
