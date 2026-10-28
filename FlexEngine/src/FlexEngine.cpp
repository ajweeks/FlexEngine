#include "stdafx.hpp"

#include "FlexEngine.hpp"

#include <stdlib.h> // For srand, rand
#include <time.h> // For timed

IGNORE_WARNINGS_PUSH
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <glm/gtx/intersect.hpp>

#if COMPILE_RENDERDOC_API
#include "renderdoc/api/app/renderdoc_app.h"
#endif

#if COMPILE_IMGUI
#include "imgui_internal.h" // For something & InputTextEx
#endif
IGNORE_WARNINGS_POP

#include "Audio/AudioManager.hpp"
#include "Cameras/CameraManager.hpp"
#include "Cameras/DebugCamera.hpp"
#include "Cameras/FirstPersonCamera.hpp"
#include "Cameras/OverheadCamera.hpp"
#include "ConfigFileManager.hpp"
#include "Editor.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "JSONParser.hpp"
#include "JSONTypes.hpp"
#include "Particles.hpp"
#include "Physics/PhysicsManager.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Platform/Platform.hpp"
#include "Player.hpp"
#include "PlayerController.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "SceneTestRunner.hpp"
#include "Game.hpp"
#include "GameSounds.hpp"
#include "Systems/CartManager.hpp"
#include "Systems/TrackManager.hpp"
#include "Time.hpp"
#include "UI.hpp"
#include "Window/GLFWWindowWrapper.hpp"
#include "Window/Monitor.hpp"
#include "VirtualMachine/Backend/FunctionBindings.hpp"

#if COMPILE_VULKAN
#include "Graphics/Vulkan/VulkanRenderer.hpp"
#include "Scene/Objects/Industry.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/Objects/Vehicle.hpp"
#endif

// TODO: Find out equivalent for nix systems
#ifdef _WINDOWS
// Specify that we prefer to be run on a discrete card on laptops when available
extern "C"
{
	__declspec(dllexport) DWORD NvOptimusEnablement = 0x01;
	__declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x01;
}
#endif // _WINDOWS

namespace flex
{
	namespace StartupTimer
	{
		namespace
		{
			// Initialized during static initialization, which is as close to process start as we can easily get
			const std::chrono::steady_clock::time_point s_ProcessStart = std::chrono::steady_clock::now();

			struct Phase
			{
				std::string name;
				ms start;
				ms end;
				i32 depth;
			};

			std::mutex s_Mutex;
			std::vector<Phase> s_Phases;
			std::vector<std::string> s_Milestones;
			i32 s_Depth = 0;
		}

		ms Elapsed()
		{
			return std::chrono::duration<ms, std::milli>(std::chrono::steady_clock::now() - s_ProcessStart).count();
		}

		void BeginPhase(const char* name)
		{
			FLEX_MUTEX_LOCK(s_Mutex);
			s_Phases.push_back({ name, Elapsed(), -1.0f, s_Depth++ });
		}

		void EndPhase(const char* name)
		{
			FLEX_MUTEX_LOCK(s_Mutex);
			for (auto iter = s_Phases.rbegin(); iter != s_Phases.rend(); ++iter)
			{
				if (iter->end < 0.0f && iter->name == name)
				{
					iter->end = Elapsed();
					break;
				}
			}
			s_Depth = glm::max(s_Depth - 1, 0);
		}

		void AddPhase(const char* name, ms start, ms end)
		{
			FLEX_MUTEX_LOCK(s_Mutex);
			s_Phases.push_back({ name, start, end, 1 });
		}

		void Milestone(const char* name, const char* extraInfo /* = nullptr */)
		{
			{
				FLEX_MUTEX_LOCK(s_Mutex);
				if (Contains(s_Milestones, std::string(name)))
				{
					return;
				}
				s_Milestones.emplace_back(name);
			}
			Print("[Startup] %s at %.0f ms%s%s\n", name, Elapsed(), extraInfo != nullptr ? " " : "", extraInfo != nullptr ? extraInfo : "");
		}

		void PrintPhases()
		{
			FLEX_MUTEX_LOCK(s_Mutex);
			// (Phases from other threads are added when they end)
			std::stable_sort(s_Phases.begin(), s_Phases.end(), [](const Phase& a, const Phase& b) { return a.start < b.start; });
			Print("[Startup] Phase timings (start offset, duration):\n");
			for (const Phase& phase : s_Phases)
			{
				const std::string indent(phase.depth * 2, ' ');
				Print("[Startup]   %7.1f ms %7.1f ms  %s%s\n", phase.start, phase.end >= 0.0f ? phase.end - phase.start : -1.0f, indent.c_str(), phase.name.c_str());
			}
			s_Phases.clear();
		}
	} // namespace StartupTimer

	bool FlexEngine::s_bHasGLDebugExtension = false;

	const u32 FlexEngine::EngineVersionMajor = 0;
	const u32 FlexEngine::EngineVersionMinor = 8;
	const u32 FlexEngine::EngineVersionPatch = 9;

	std::string FlexEngine::s_CurrentWorkingDirectory;
	std::string FlexEngine::s_ExecutablePath;
	std::vector<AudioSourceID> FlexEngine::s_AudioSourceIDs;

	FlexEngine::FlexEngine() :
		m_MouseButtonCallback(this, &FlexEngine::OnMouseButtonEvent),
		m_KeyEventCallback(this, &FlexEngine::OnKeyEvent),
		m_ActionCallback(this, &FlexEngine::OnActionEvent)
	{
		plInitAndStart("Flex Engine"); // NOTE: USE_PL must be defined to 1 for profiling to be enabled

		std::srand((u32)time(NULL));

		Platform::RetrievePathToExecutable();
		Platform::RetrieveCurrentWorkingDirectory();

		memset(m_CmdLineStrBuf, 0, MAX_CHARS_CMD_LINE_STR);

		{
			std::string configPathAbs = RelativePathToAbsolute(COMMON_CONFIG_LOCATION);
			m_CommonSettingsAbsFilePath = configPathAbs;
			m_CommonSettingsFileName = StripLeadingDirectories(configPathAbs);
		}

		m_BootupTimesAbsFilePath = RelativePathToAbsolute(BOOTUP_TIMES_LOCATION);
		m_RenderDocSettingsAbsFilePath = RelativePathToAbsolute(RENDERDOC_LOCATION);

		{
			// Default, can be overridden in common.json
			m_ShaderEditorPath = "C:/Program Files/Sublime Text 3/sublime_text.exe";
		}

		m_CommonSettingsSaveTimer = Timer(10.0f);
		m_LogSaveTimer = Timer(5.0f);
		m_UIWindowCacheSaveTimer = Timer(20.0f);

#if COMPILE_RENDERDOC_API
		m_RenderDocAPICheckTimer = Timer(3.0f);
#endif

#if COMPILE_VULKAN
		m_RendererName = "Vulkan";
#endif

#if defined(__clang__)
		m_CompilerName = "Clang";

		m_CompilerVersion =
			IntToString(__clang_major__) + '.' +
			IntToString(__clang_minor__) + '.' +
			IntToString(__clang_patchlevel__);
#elif defined(_MSC_VER)
		m_CompilerName = "MSVC";

#if _MSC_VER >= 1920
		m_CompilerVersion = "2019";
#elif _MSC_VER >= 1910
		m_CompilerVersion = "2017";
#elif _MSC_VER >= 1900
		m_CompilerVersion = "2015";
#elif _MSC_VER >= 1800
		m_CompilerVersion = "2013";
#else
		m_CompilerVersion = "Unknown";
#endif
#elif defined(__GNUC__)
		m_CompilerName = "GCC";

		m_CompilerVersion =
			IntToString(__GNUC__) + '.' +
			IntToString(__GNUC_MINOR__) + '.' +
			IntToString(__GNUC_PATCHLEVEL__);
#else
		m_CompilerName = "Unknown";
		m_CompilerVersion = "Unknown";
#endif

#ifdef DEBUG
		const char* configStr = "Debug";
#elif defined(PROFILE)
		const char* configStr = "Profile";
#elif defined(RELEASE)
#if defined(SYMBOLS)
		const char* configStr = "Shipping (with symbols)";
#else
		const char* configStr = "Shipping";
#endif
#else
		static_assert(false);
#endif

#if defined(FLEX_64)
		const char* targetStr = "x64";
#elif defined(FLEX_32)
		const char* targetStr = "x32";
#endif

#if defined(_WINDOWS)
		const char* platformStr = "Windows";
#elif defined(linux)
		const char* platformStr = "Linux";
#endif

		Print("[%s]\n", Platform::GetDateString_YMDHMS().c_str());

		Print("FlexEngine v%u.%u.%u - Config: [%s %s, %s] - Compiler: [%s %s]\n", EngineVersionMajor, EngineVersionMinor, EngineVersionPatch, configStr, targetStr, platformStr, m_CompilerName.c_str(), m_CompilerVersion.c_str());

#if USE_PL
		Print("Connected to palanteer\n");
#endif
	}

	FlexEngine::~FlexEngine()
	{
		Destroy();
	}

	void FlexEngine::Initialize()
	{
		PROFILE_BEGIN("FlexEngine Initialize");
		sec startTime = Time::CurrentSeconds();

#if COMPILE_RENDERDOC_API
		SetupRenderDocAPI();
#endif

		g_EngineInstance = this;

		m_FrameTimes = RollingAverage<real>(256);

		STARTUP_PHASE("FlexEngine::Initialize");
		// FLEX_STARTUP_PROFILE=1 prints every profiled scope's total time from startup to the first frame
		m_bStartupProfile = getenv("FLEX_STARTUP_PROFILE") != nullptr && m_SceneTestRunner == nullptr && !Profiler::IsEnabled();
		if (m_bStartupProfile)
		{
			Profiler::SetEnabled(true);
		}

		{
			STARTUP_PHASE("Platform, config & job system");
			Platform::Init();

			g_ConfigFileManager = new ConfigFileManager();

			JobSystem::Initialize();
		}

		g_Systems[(i32)SystemType::PROPERTY_COLLECTION_MANAGER] = new PropertyCollectionManager();
		g_Systems[(i32)SystemType::PROPERTY_COLLECTION_MANAGER]->Initialize();

#if COMPILE_VULKAN
		vk::VulkanRenderer* renderer = nullptr;
		{
			PROFILE_AUTO("Create Renderer");
			renderer = new vk::VulkanRenderer();
			// Vulkan is loaded & its instance is created while GLFW initializes & creates the window (both are slow)
			renderer->BeginCreateInstance(WindowTitle());
		}
#endif

		{
			STARTUP_PHASE("Create window");
			CreateWindow();
		}

#if COMPILE_VULKAN
		// (Only set once the window exists, as before, since window callbacks notify the renderer when it's set)
		g_Renderer = renderer;
#endif

		{
			STARTUP_PHASE("ResourceManager::Initialize");
			g_ResourceManager = new ResourceManager();
			g_ResourceManager->Initialize();
		}

		g_UIManager = new UIManager();
		g_Editor = new Editor();
		g_InputManager = new InputManager();
		g_CameraManager = new CameraManager();

		{
			STARTUP_PHASE("Renderer::Initialize");
			InitializeWindowAndRenderer();
		}

		{
			STARTUP_PHASE("AudioManager::Initialize");
			AudioManager::Initialize();
			// Synthesizes the game's sounds on worker threads
			GameSounds::Initialize();
		}

		Print("Bullet v%d\n", btGetVersion());

#if COMPILE_RENDERDOC_API
		if (m_RenderDocAPI != nullptr &&
			m_RenderDocAutoCaptureFrameCount != -1 &&
			m_RenderDocAutoCaptureFrameOffset == 0)
		{
			RenderDocStartCapture();
		}
#endif

		StartupTimer::BeginPhase("Physics, editor & systems");
		g_PhysicsManager = new PhysicsManager();
		g_PhysicsManager->Initialize();

		g_Editor->Initialize();

		g_Systems[(i32)SystemType::PLUGGABLES] = new PluggablesSystem();
		g_Systems[(i32)SystemType::PLUGGABLES]->Initialize();

		g_Systems[(i32)SystemType::ROAD_MANAGER] = new RoadManager();
		g_Systems[(i32)SystemType::ROAD_MANAGER]->Initialize();

		g_Systems[(i32)SystemType::TERMINAL_MANAGER] = new TerminalManager(m_bInstallTerminalDirectoryWatch);
		g_Systems[(i32)SystemType::TERMINAL_MANAGER]->Initialize();

		g_Systems[(i32)SystemType::TRACK_MANAGER] = new TrackManager();

		g_Systems[(i32)SystemType::CART_MANAGER] = new CartManager();

		g_Systems[(i32)SystemType::PARTICLE_MANAGER] = new ParticleManager();
		g_Systems[(i32)SystemType::PARTICLE_MANAGER]->Initialize();

		StartupTimer::EndPhase("Physics, editor & systems");

		{
			STARTUP_PHASE("Discover meshes, parse materials & fonts");
			g_ResourceManager->DiscoverMeshes();
			g_ResourceManager->ParseMaterialsFiles();
			g_ResourceManager->ParseFontFile();
		}

		g_SceneManager = new SceneManager();

		{
			STARTUP_PHASE("Load scene");
			if (!LoadCommonSettingsFromDisk())
			{
				// Common file doesn't exist or is corrupt, load first present scene
				g_SceneManager->SetNextSceneActive();
				// Ensure config file will be written to disk this frame
				m_CommonSettingsSaveTimer.Complete();
			}
		}

		StartupTimer::BeginPhase("UI & ImGui");
		g_UIManager->Initialize();

		{
			PROFILE_AUTO("ImGui CreateContext");
			ImGui::CreateContext();
		}
		SetupImGuiStyles();
		StartupTimer::EndPhase("UI & ImGui");

		{
			STARTUP_PHASE("ResourceManager::PostInitialize");
			g_ResourceManager->PostInitialize();
		}

		g_Editor->PostInitialize();

		{
			STARTUP_PHASE("Renderer::PostInitialize");
			g_Renderer->PostInitialize();
		}

		g_InputManager->Initialize();

		g_InputManager->BindMouseButtonCallback(&m_MouseButtonCallback, 99);
		g_InputManager->BindKeyEventCallback(&m_KeyEventCallback, 10);
		g_InputManager->BindActionCallback(&m_ActionCallback, 10);

		// Must be called post scene load
		g_Systems[(i32)SystemType::TRACK_MANAGER]->Initialize();
		g_Systems[(i32)SystemType::CART_MANAGER]->Initialize();

		if (s_AudioSourceIDs.empty())
		{
			PROFILE_AUTO("Initialize audio sources");
			STARTUP_PHASE("Initialize audio sources");

			s_AudioSourceIDs.push_back(g_ResourceManager->GetOrLoadAudioSourceID(SID("dud_dud_dud_dud.wav"), true));
			s_AudioSourceIDs.push_back(g_ResourceManager->GetOrLoadAudioSourceID(SID("drmapan.wav"), true));
			s_AudioSourceIDs.push_back(g_ResourceManager->GetOrLoadAudioSourceID(SID("blip.wav"), true));
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 523.25f)); // C5
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 587.33f)); // D5
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 659.25f)); // E5
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 698.46f)); // F5
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 783.99f)); // G5
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 880.00f)); // A5
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 987.77f)); // B5
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 1046.50f)); // C6
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 1174.66f)); // D6
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 1318.51f)); // E6
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 1396.91f)); // F6
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 1567.98f)); // G6
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 1760.00f)); // A6
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 1975.53f)); // B6
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 2093.00f)); // C7
			for (u32 i = (u32)SoundEffect::synthesized_00; i <= (u32)SoundEffect::synthesized_14; ++i)
			{
				AudioManager::SetSourceGain(s_AudioSourceIDs[i], 0.5f);
			}
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeMelody()); // melody
			AudioManager::SetSourceGain(s_AudioSourceIDs[(i32)SoundEffect::melody_0], 0.5f);
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeMelody(500.0f)); // melody fast
			AudioManager::SetSourceGain(s_AudioSourceIDs[(i32)SoundEffect::melody_0_fast], 0.5f);
		}

		ImGuiIO& io = ImGui::GetIO();
		m_ImGuiIniFilepathStr = IMGUI_INI_LOCATION;
		io.IniFilename = g_bHeadless ? nullptr : m_ImGuiIniFilepathStr.c_str();
		m_ImGuiLogFilepathStr = IMGUI_LOG_LOCATION;
		io.LogFilename = m_ImGuiLogFilepathStr.c_str();
		io.DisplaySize = (ImVec2)g_Window->GetFrameBufferSize();
		io.IniSavingRate = 10.0f;

		memset(m_CmdLineStrBuf, 0, MAX_CHARS_CMD_LINE_STR);

		m_ConsoleCommands.emplace_back(FunctionBindings::BindV("?",
			[]() { g_EngineInstance->PrintAllConsoleCommands(); }));
		// TODO: Support autofill of possible arguments (in this case scene names)
		m_ConsoleCommands.emplace_back(FunctionBindings::BindP("scene.load",
			[](const Variant& newSceneNameStr) {
				{
					if (newSceneNameStr.type == Variant::Type::STRING)
					{
						std::string newSceneName(newSceneNameStr.AsString());
						ToLower(newSceneName);
						g_SceneManager->SetCurrentScene(newSceneName);
					}
				}
		}, Variant::Type::STRING));
		m_ConsoleCommands.emplace_back(FunctionBindings::BindV("scene.reload",
			[]() { g_SceneManager->ReloadCurrentScene(); }));
		m_ConsoleCommands.emplace_back(FunctionBindings::BindV("scene.reload.hard",
			[]()
		{
			g_ResourceManager->DestroyAllLoadedMeshes();
			g_SceneManager->ReloadCurrentScene();
		}));
		m_ConsoleCommands.emplace_back(FunctionBindings::BindV("reload.shaders",
			[]() { g_Renderer->RecompileShaders(false); }));
		m_ConsoleCommands.emplace_back(FunctionBindings::BindV("reload.shaders.force",
			[]() { g_Renderer->RecompileShaders(true); }));
		m_ConsoleCommands.emplace_back(FunctionBindings::BindV("reload.fontsdfs",
			[]() { g_ResourceManager->LoadFonts(true); }));

		m_ConsoleCommands.emplace_back(FunctionBindings::BindV("select.all",
			[]() { g_Editor->SelectAll(); }));
		m_ConsoleCommands.emplace_back(FunctionBindings::BindV("select.none",
			[]() { g_Editor->SelectNone(); }));
		m_ConsoleCommands.emplace_back(FunctionBindings::BindV("exit",
			[]() { g_EngineInstance->Stop(); }));

		m_ConsoleCommands.emplace_back(FunctionBindings::BindP("rendering.aa",
			[](const Variant& mode)
		{
			if (mode.type == Variant::Type::STRING)
			{
				std::string modeStr = Trim(mode.AsString());
				if (modeStr.compare("taa") == 0)
				{
					g_Renderer->SetTAAEnabled(true);
				}
				else if (modeStr.compare("off") == 0)
				{
					g_Renderer->SetTAAEnabled(false);
				}
			}
			else if (Variant::IsIntegral(mode.type) || mode.type == Variant::Type::BOOL)
			{
				i32 enabled = mode.AsInt();
				if (enabled != -1)
				{
					g_Renderer->SetTAAEnabled(enabled == 1);
				}
			}
		}, Variant::Type::VOID_));
		m_ConsoleCommands.emplace_back(FunctionBindings::BindV("rendering.aa.taa",
			[]() { g_Renderer->SetTAAEnabled(true); }));
		m_ConsoleCommands.emplace_back(FunctionBindings::Bind("rendering.aa.get_enabled",
			[]() { return Variant(g_Renderer->IsTAAEnabled()); }));
		m_ConsoleCommands.emplace_back(FunctionBindings::BindV("rendering.wireframe.toggle",
			[]() { g_Renderer->ToggleWireframeOverlay(); }));
		m_ConsoleCommands.emplace_back(FunctionBindings::BindV("rendering.wireframe_selection.toggle",
			[]() { g_Renderer->ToggleWireframeSelectionOverlay(); }));

		m_ConsoleCommands.emplace_back(FunctionBindings::BindV("rendering.debug_overlay.clear",
			[]() { g_Renderer->SetDebugOverlayID(0); }));
		m_ConsoleCommands.emplace_back(FunctionBindings::BindP("rendering.debug_overlay",
			[](const Variant& mode)
		{
			if (mode.type == Variant::Type::STRING)
			{
				std::string modeStr = mode.AsString();
				i32 newModeID = DebugOverlayNameToID(modeStr.c_str());
				if (newModeID != -1)
				{
					g_Renderer->SetDebugOverlayID(newModeID);
				}
			}
			else
			{
				i32 newModeID = mode.AsInt();
				g_Renderer->SetDebugOverlayID(newModeID);
			}
		}, Variant::Type::VOID_ /* either int or string */));

		m_ConsoleCommands.emplace_back(FunctionBindings::BindV("rendering.fog.toggle",
			[]() {g_Renderer->ToggleFogEnabled(); }));

		m_ConsoleCommands.emplace_back(FunctionBindings::BindP("rendering.shader_quality_level",
			[](const Variant& shaderQualityLevel)
		{
			i32 shaderQualityLevelInt = shaderQualityLevel.AsInt();
			if (shaderQualityLevelInt != -1)
			{
				g_Renderer->SetShaderQualityLevel(shaderQualityLevelInt);
			}
		}, Variant::Type::STRING));

		m_ConsoleCommands.emplace_back(FunctionBindings::Bind("time_of_day.get",
			[]() { return Variant(g_SceneManager->CurrentScene()->GetTimeOfDay()); }));
		m_ConsoleCommands.emplace_back(FunctionBindings::BindP("time_of_day.set",
			[](const Variant& time) { g_SceneManager->CurrentScene()->SetTimeOfDay(time.AsFloat()); }, Variant::Type::FLOAT));

		m_ConsoleCommands.emplace_back(FunctionBindings::Bind("time_of_day.paused.get",
			[]() { return Variant(g_SceneManager->CurrentScene()->GetTimeOfDayPaused()); }));
		m_ConsoleCommands.emplace_back(FunctionBindings::BindP("time_of_day.paused.set",
			[](const Variant& timePaused) { g_SceneManager->CurrentScene()->SetTimeOfDayPaused(timePaused.AsBool()); }, Variant::Type::BOOL));

		m_ConsoleCommands.emplace_back(FunctionBindings::Bind("time_of_day.seconds_per_day.get",
			[]() { return Variant(g_SceneManager->CurrentScene()->GetTimeOfDay()); }));
		m_ConsoleCommands.emplace_back(FunctionBindings::BindP("time_of_day.seconds_per_day.set",
			[](const Variant& secPerDay) { g_SceneManager->CurrentScene()->SetSecondsPerDay(secPerDay.AsFloat()); }, Variant::Type::FLOAT));

		m_ConsoleCommands.emplace_back(FunctionBindings::Bind("simulation.paused.get",
			[]() { return Variant(g_EngineInstance->IsSimulationPaused()); }));
		m_ConsoleCommands.emplace_back(FunctionBindings::BindP("simulation.paused.set",
			[](const Variant& simPaused) { g_EngineInstance->SetSimulationPaused(simPaused.AsBool()); }, Variant::Type::BOOL));

		m_ConsoleCommands.emplace_back(FunctionBindings::Bind("simulation.speed.get",
			[]() { return Variant(g_EngineInstance->GetSimulationSpeed()); }));
		m_ConsoleCommands.emplace_back(FunctionBindings::BindP("simulation.speed.set",
			[](const Variant& simSpeed) { g_EngineInstance->SetSimulationSpeed(simSpeed.AsFloat()); }, Variant::Type::FLOAT));

		m_ConsoleCommands.emplace_back(FunctionBindings::BindV("simulation.step_frame",
			[]() { g_EngineInstance->StepSimulationFrame(); }));

		// TODO: Add usage helper (e.g. "inventory.add ["item_name"] [count]")
		m_ConsoleCommands.emplace_back(FunctionBindings::BindP("inventory.add",
			[](const Variant& item, const Variant& count) {
				{
					i32 countInt = count.AsInt();

					const char* itemName = item.AsString();
					PrefabID prefabID = g_ResourceManager->GetPrefabID(itemName);

					if (prefabID.IsValid())
					{
						Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
						if (player != nullptr)
						{
							player->AddToInventory(prefabID, countInt);
						}
					}
					else
					{
						PrintError("Invalid prefab name \"%s\"\n", itemName);
					}
				}
		}, Variant::Type::STRING, Variant::Type::INT));

		// Usage: player.teleport [x] [z] (onto the ground there, or the nearest dry land)
		m_ConsoleCommands.emplace_back(FunctionBindings::BindP("player.teleport",
			[](const Variant& x, const Variant& z)
		{
			Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
			TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
			if (player == nullptr || terrainGenerator == nullptr)
			{
				PrintError("Teleporting needs a player & terrain\n");
				return;
			}
			const terrain::ColumnSample column = terrainGenerator->SampleColumn(x.AsFloat(), z.AsFloat());
			player->TeleportOnTerrain(glm::vec3(x.AsFloat(), glm::max(column.height, column.waterLevel) + 250.0f, z.AsFloat()));
		}, Variant::Type::FLOAT, Variant::Type::FLOAT));

		m_ConsoleCommands.emplace_back(FunctionBindings::BindP("open",
			[](const Variant& windowName)
		{
			if (!g_EngineInstance->ToggleUIWindow(windowName.AsString()))
			{
				PrintError("Failed to find UI window with name %s\n", windowName.AsString());
			}
		}, Variant::Type::STRING));

		m_ConsoleCommands.emplace_back(FunctionBindings::BindV("audio.toggle_muted",
			[]() { AudioManager::ToggleMuted(); }));

		m_ConsoleCommands.emplace_back(FunctionBindings::BindP("audio.set_master_gain",
			[](const Variant& gain) { AudioManager::SetMasterGain(gain.AsFloat()); }, Variant::Type::FLOAT));

		m_ConsoleCommands.emplace_back(FunctionBindings::BindP("audio.play_note",
			[](const Variant& note, const Variant& length)
		{
			AudioManager::PlayNote(note.AsFloat(), length.AsFloat(), 1.0f);
		}, Variant::Type::FLOAT, Variant::Type::FLOAT));

		ParseUIWindowCache();

		if (m_SceneTestRunner != nullptr)
		{
			m_SceneTestRunner->OnEngineInitialized();
		}
		else if (m_bGameMode)
		{
			STARTUP_PHASE("Game::Initialize");
			g_Game = new Game();
			g_Game->Initialize();
		}

		sec durationSec = Time::CurrentSeconds() - startTime;
		PROFILE_END("FlexEngine Initialize");

		ms blockDuration = Time::ConvertFormats(durationSec, Time::Format::SECOND, Time::Format::MILLISECOND);
		if (blockDuration != -1.0f && blockDuration < 20000) // Exceptionally long times are almost always due to hitting breakpoints
		{
			std::string bootupTimesEntry = Platform::GetDateString_YMDHMS() + "," + FloatToString(blockDuration, 2);
			AppendToBootupTimesFile(bootupTimesEntry);
		}
	}

	AudioSourceID FlexEngine::GetAudioSourceID(SoundEffect effect)
	{
		CHECK_GE((i32)effect, 0);
		CHECK_LT((i32)effect, (i32)SoundEffect::LAST_ELEMENT);

		return s_AudioSourceIDs[(i32)effect];
	}

	void FlexEngine::Destroy()
	{
		// TODO: Time engine destruction using non-glfw timer

		SerializeUIWindowCache();

#if COMPILE_RENDERDOC_API
		FreeLibrary(m_RenderDocModule);
#endif

		g_InputManager->UnbindMouseButtonCallback(&m_MouseButtonCallback);
		g_InputManager->UnbindKeyEventCallback(&m_KeyEventCallback);
		g_InputManager->UnbindActionCallback(&m_ActionCallback);

		if (g_Game != nullptr)
		{
			g_Game->Destroy();
		}
		// (The log is otherwise only saved periodically, which misses what was printed while shutting down)
		SaveLogBufferToFile();

		SaveCommonSettingsToDisk(false);
		g_Window->SaveToConfig();

		GameSounds::Destroy();
		JobSystem::Destroy();

		g_Editor->Destroy();
		g_CameraManager->Destroy();
		g_ResourceManager->Destroy();
		g_UIManager->Destroy();
		g_SceneManager->Destroy();
		g_PhysicsManager->Destroy();
		DestroyWindowAndRenderer();
		g_ResourceManager->DestroyAllLoadedMeshes();

		AudioManager::Destroy();

		for (System* system : g_Systems)
		{
			system->Destroy();
			delete system;
		}

		for (IFunction* func : m_ConsoleCommands)
		{
			delete func;
		}
		m_ConsoleCommands.clear();

		delete g_SceneManager;
		g_SceneManager = nullptr;

		delete g_PhysicsManager;
		g_PhysicsManager = nullptr;

		delete g_ResourceManager;
		g_ResourceManager = nullptr;

		delete g_ConfigFileManager;
		g_ConfigFileManager = nullptr;

		delete g_CameraManager;
		g_CameraManager = nullptr;

		delete g_Editor;
		g_Editor = nullptr;

		delete g_Monitor;
		g_Monitor = nullptr;

		delete g_InputManager;
		g_InputManager = nullptr;

		plStopAndUninit();

		// Reset console colour to default
		Print("\n");
	}

	std::string FlexEngine::WindowTitle()
	{
		// The game has its own name (the editor & test scenes show the engine's)
		if (g_EngineInstance != nullptr && g_EngineInstance->m_bGameMode)
		{
			return Game::GAME_NAME;
		}
		return "Flex Engine v" + EngineVersionString();
	}

	void FlexEngine::CreateWindow()
	{
		PROFILE_AUTO("CreateWindow");

		CHECK_EQ(g_Window, nullptr);

		const std::string titleString = WindowTitle();

		if (g_Window == nullptr)
		{
			g_Window = new GLFWWindowWrapper(titleString);
		}
		if (g_Window == nullptr)
		{
			PrintError("Failed to create a window! Are any compile flags set in stdafx.hpp?\n");
			return;
		}

		g_Window->Initialize();
		g_Monitor = new Monitor();
		g_Window->RetrieveMonitorInfo();

		real desiredAspectRatio = 16.0f / 9.0f;
		real desiredWindowSizeScreenPercentange = 0.85f;

		// What kind of monitor has different scales along each axis?
		CHECK_EQ(g_Monitor->contentScaleX, g_Monitor->contentScaleY);

		i32 newWindowSizeY = i32(g_Monitor->height * desiredWindowSizeScreenPercentange * glm::max(g_Monitor->contentScaleY, 1.0f))	;
		i32 newWindowSizeX = i32(newWindowSizeY * desiredAspectRatio);

		i32 newWindowPosX = i32(newWindowSizeX * 0.1f);
		i32 newWindowPosY = i32(newWindowSizeY * 0.1f);

		g_Window->Create(glm::vec2i(newWindowSizeX, newWindowSizeY), glm::vec2i(newWindowPosX, newWindowPosY));
	}

	void FlexEngine::InitializeWindowAndRenderer()
	{
		PROFILE_AUTO("InitializeWindowAndRenderer");

		g_Window->SetUpdateWindowTitleFrequency(0.5f);
		g_Window->PostInitialize();

		g_Renderer->Initialize();
	}

	void FlexEngine::DestroyWindowAndRenderer()
	{
		if (g_Renderer != nullptr)
		{
			g_Renderer->Destroy();
			delete g_Renderer;
			g_Renderer = nullptr;
		}

		if (g_Window != nullptr)
		{
			g_Window->Destroy();
			delete g_Window;
			g_Window = nullptr;
		}
	}

	void FlexEngine::OnSceneChanged()
	{
		SaveCommonSettingsToDisk(false);
	}

	bool FlexEngine::IsRenderingImGui() const
	{
		return m_bRenderImGui;
	}

	bool FlexEngine::IsRenderingEditorObjects() const
	{
		return m_bRenderEditorObjects;
	}

	void FlexEngine::SetRenderingEditorObjects(bool bRenderingEditorObjects)
	{
		m_bRenderEditorObjects = bRenderingEditorObjects;
	}

	void FlexEngine::UpdateAndRender()
	{
		m_bRunning = true;
		// Kept as doubles: the absolute time as a float loses precision over long sessions (~2ms resolution after
		// 4.6 hours, ~8ms after a day), which would quantize the delta time
		double frameStartTime = glfwGetTime();
		while (m_bRunning)
		{
			Profiler::EndFrame();

			if (m_SceneTestRunner != nullptr && !m_SceneTestRunner->Update())
			{
				break;
			}

			double now = glfwGetTime();

			if (m_bToggleRenderImGui)
			{
				m_bToggleRenderImGui = false;
				m_bRenderImGui = !m_bRenderImGui;
			}

			PROFILE_BEGIN("PollEvents");
			g_Window->PollEvents();
			PROFILE_END("PollEvents");

			double frameEndTime = now;

			if (m_FramesToFakeDT > 0)
			{
				--m_FramesToFakeDT;
				// Pretend this frame started m_FakeDT seconds ago to prevent spike
				frameStartTime = frameEndTime - m_FakeDT;
			}

			sec secondsElapsed = (sec)(frameEndTime - frameStartTime);
			frameStartTime = frameEndTime;

			g_UnpausedDeltaTime = glm::clamp(secondsElapsed, m_MinDT, m_MaxDT);
			g_SecElapsedSinceProgramStart = frameEndTime;

			// TODO: Pause audio when editor is paused
			if (m_bSimulationPaused && !m_bSimulateNextFrame)
			{
				g_DeltaTime = 0.0f;
			}
			else
			{
				g_DeltaTime = glm::clamp(secondsElapsed * m_SimulationSpeed, m_MinDT, m_MaxDT);
			}

			const bool bSimulateFrame = (!m_bSimulationPaused || m_bSimulateNextFrame);
			m_bSimulateNextFrame = false;

			if (!m_bSimulationPaused)
			{
				m_FrameTimes.AddValue(g_UnpausedDeltaTime * 1000.0f);
			}

			{
				PROFILE_AUTO("Update");

				g_SceneManager->UpdateSaveAllScenes();

				UPDATE_TWEAKABLES();

				g_ConfigFileManager->Update();

				const glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
				if (frameBufferSize.x == 0 || frameBufferSize.y == 0)
				{
					PROFILE_AUTO("InputManager ClearAllInputs");
					g_InputManager->ClearAllInputs();
				}

#if COMPILE_RENDERDOC_API
				if (m_RenderDocAPI != nullptr)
				{
					if (!m_bRenderDocCapturingFrame &&
						m_RenderDocAutoCaptureFrameOffset != -1 &&
						m_RenderDocAutoCaptureFrameCount != -1)
					{
						i32 frameIndex = g_Renderer->GetFramesRenderedCount();
						if (frameIndex >= m_RenderDocAutoCaptureFrameOffset &&
							frameIndex < m_RenderDocAutoCaptureFrameOffset + m_RenderDocAutoCaptureFrameCount)
						{
							m_bRenderDocTriggerCaptureNextFrame = true;
						}
					}

					if (m_bRenderDocTriggerCaptureNextFrame)
					{
						m_bRenderDocTriggerCaptureNextFrame = false;
						RenderDocStartCapture();
					}
				}
#endif

				// Call as early as possible in the frame
				// Starts new ImGui frame and clears debug draw lines
				g_Renderer->NewFrame();

				if (m_UIWindowCacheSaveTimer.Update())
				{
					PROFILE_AUTO("SerializeUIWindowCache");
					m_UIWindowCacheSaveTimer.Restart();
					SerializeUIWindowCache();
				}

				if (m_LogSaveTimer.Update())
				{
					PROFILE_AUTO("SaveLogBufferToFile");
					m_LogSaveTimer.Restart();
					SaveLogBufferToFile();
				}

				if (m_bRenderImGui)
				{
					DrawImGuiObjects();
				}

				g_Editor->EarlyUpdate();

				g_CameraManager->Update();

				BaseScene* currentScene = g_SceneManager->CurrentScene();
				BaseCamera* currentCamera = g_CameraManager->CurrentCamera();

				if (bSimulateFrame)
				{
					currentCamera->Update();

					currentScene->Update();

					currentCamera->LateUpdate();

					if (Player* testPlayer = currentScene->GetPlayer(0))
					{
						testPlayer->UpdateHeldJitterTest();
					}

					if (g_Game != nullptr)
					{
						g_Game->LateUpdate();
					}

					AudioManager::Update();
				}
				else
				{
					// Simulation is paused
					if (!currentCamera->bIsGameplayCam)
					{
						currentCamera->Update();
					}
				}

				GameSounds::Update();

				currentScene->Render();

				g_Window->Update();

				g_ResourceManager->Update();

				g_Editor->LateUpdate();

				if (bSimulateFrame)
				{
					PROFILE_BEGIN("Update systems");
					for (System* system : g_Systems)
					{
						system->Update();
					}
					PROFILE_END("Update systems");

					GetSystem<TrackManager>(SystemType::TRACK_MANAGER)->DrawDebug();

					currentScene->LateUpdate();
				}

				g_UIManager->Update();

				if (g_Game != nullptr)
				{
					g_Game->Update();
				}

				g_Renderer->Update();

				g_InputManager->Update();

				g_InputManager->PostUpdate();
			}

			g_Renderer->Draw();

			// Memory freed this frame (e.g. by unloaded terrain chunks)
			JobSystem::FlushBackgroundFrees();

#if COMPILE_RENDERDOC_API
			if (m_RenderDocAPI != nullptr && m_bRenderDocCapturingFrame)
			{
				RenderDocEndCapture();
			}
#endif

			if (!m_bRenderImGui)
			{
				// Prevent mouse from being ignored while hovering over invisible ImGui elements
				ImGui::GetIO().WantCaptureMouse = false;
				ImGui::GetIO().WantCaptureKeyboard = false;
				ImGui::GetIO().WantSetMousePos = false;
			}

			if (m_CommonSettingsSaveTimer.Update())
			{
				PROFILE_AUTO("Save common settings to disk");
				m_CommonSettingsSaveTimer.Restart();
				SaveCommonSettingsToDisk(false);
				g_Window->SaveToConfig();
			}

			g_Renderer->EndOfFrame();

			if (!m_bFirstFrameDrawn)
			{
				m_bFirstFrameDrawn = true;
				StartupTimer::Milestone("First frame drawn");
				StartupTimer::PrintPhases();
				if (m_bStartupProfile)
				{
					Profiler::EndFrame();
					Profiler::PrintReport(true);
					Profiler::Reset();
					// Continues while a world loads (printed by the game once it's playable)
					Profiler::SetEnabled(g_Game != nullptr && g_Game->IsLoadingWorld());
				}
			}
		}
	}

	void FlexEngine::SetupImGuiStyles()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.MouseDrawCursor = false;

		//io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; // glfwSetCursor overruns buffer somewhere (currently Window::m_FrameBufferSize.y...)

		std::string fontFilePath(FONT_DIRECTORY "lucon.ttf");
		io.Fonts->AddFontFromFileTTF(fontFilePath.c_str(), 13);

		io.FontGlobalScale = g_Monitor->contentScaleX;

		ImVec4* colors = ImGui::GetStyle().Colors;
		colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
		colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
		colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.85f);
		colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.05f, 0.05f, 0.10f, 0.90f);
		colors[ImGuiCol_Border] = ImVec4(0.70f, 0.70f, 0.70f, 0.40f);
		colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.30f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.90f, 0.80f, 0.80f, 0.40f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.90f, 0.65f, 0.65f, 0.45f);
		colors[ImGuiCol_TitleBg] = ImVec4(0.73f, 0.34f, 0.00f, 0.94f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.76f, 0.46f, 0.19f, 1.00f);
		colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.74f, 0.33f, 0.09f, 0.20f);
		colors[ImGuiCol_MenuBarBg] = ImVec4(0.60f, 0.18f, 0.04f, 0.55f);
		colors[ImGuiCol_ScrollbarBg] = ImVec4(0.20f, 0.25f, 0.30f, 0.60f);
		colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.80f, 0.75f, 0.40f, 0.40f);
		colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.80f, 0.75f, 0.41f, 0.50f);
		colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.92f, 0.82f, 0.29f, 0.60f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.97f, 0.54f, 0.03f, 1.00f);
		colors[ImGuiCol_SliderGrab] = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.82f, 0.61f, 0.37f, 1.00f);
		colors[ImGuiCol_Button] = ImVec4(0.95f, 0.53f, 0.22f, 0.60f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.82f, 0.49f, 0.20f, 1.00f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.71f, 0.37f, 0.11f, 1.00f);
		colors[ImGuiCol_Header] = ImVec4(0.66f, 0.32f, 0.17f, 0.76f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.74f, 0.43f, 0.29f, 0.76f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.60f, 0.23f, 0.07f, 0.80f);
		colors[ImGuiCol_Separator] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
		colors[ImGuiCol_SeparatorHovered] = ImVec4(0.70f, 0.62f, 0.60f, 1.00f);
		colors[ImGuiCol_SeparatorActive] = ImVec4(0.90f, 0.78f, 0.70f, 1.00f);
		colors[ImGuiCol_ResizeGrip] = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
		colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.60f);
		colors[ImGuiCol_ResizeGripActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
		colors[ImGuiCol_Tab] = ImVec4(0.59f, 0.32f, 0.09f, 1.00f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.76f, 0.45f, 0.19f, 1.00f);
		colors[ImGuiCol_TabActive] = ImVec4(0.70f, 0.41f, 0.04f, 1.00f);
		colors[ImGuiCol_TabUnfocused] = ImVec4(0.50f, 0.28f, 0.08f, 1.00f);
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.62f, 0.33f, 0.08f, 1.00f);
		//colors[ImGuiCol_DockingPreview] = ImVec4(0.83f, 0.44f, 0.11f, 0.70f);
		//colors[ImGuiCol_DockingBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
		colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
		colors[ImGuiCol_PlotHistogram] = ImVec4(0.039f, 0.238f, 0.616f, 1.000f);
		colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
		colors[ImGuiCol_TextSelectedBg] = ImVec4(1.00f, 0.57f, 0.31f, 0.35f);
		colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 1.00f, 0.60f);
		colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
		colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
		colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
	}

	GameObject* FlexEngine::SpawnVehicleInFrontOfCamera(const char* prefabName /* = "vehicle" */)
	{
		const PrefabID vehicleID = g_ResourceManager->GetPrefabID(prefabName);
		if (!vehicleID.IsValid())
		{
			PrintError("Failed to spawn %s, prefab not found\n", prefabName);
			return nullptr;
		}

		BaseCamera* camera = g_CameraManager->CurrentCamera();
		glm::vec3 forward = glm::vec3(camera->forward.x, 0.0f, camera->forward.z);
		forward = glm::length(forward) > 1e-4f ? glm::normalize(forward) : VEC3_FORWARD;
		glm::vec3 pos = camera->position + forward * 8.0f;

		// Place on the ground (terrain or any other collider)
		TerrainGenerator* terrain = TerrainGenerator::GetActive();
		glm::vec3 hitPos, hitNormal;
		if (terrain != nullptr && terrain->Raycast(pos + glm::vec3(0.0f, 20.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 2000.0f, hitPos, hitNormal))
		{
			pos = hitPos + glm::vec3(0.0f, 1.5f, 0.0f);
		}
		else
		{
			PhysicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld();
			btVector3 rayStart = ToBtVec3(pos + glm::vec3(0.0f, 20.0f, 0.0f));
			btVector3 rayEnd = ToBtVec3(pos - glm::vec3(0.0f, 500.0f, 0.0f));
			btCollisionWorld::ClosestRayResultCallback rayCallback(rayStart, rayEnd);
			physicsWorld->GetWorld()->rayTest(rayStart, rayEnd, rayCallback);
			if (rayCallback.hasHit())
			{
				pos = ToVec3(rayCallback.m_hitPointWorld) + glm::vec3(0.0f, 1.5f, 0.0f);
			}
		}

		// Float on water rather than sinking to its bed
		real waterHeight;
		if (RowBoat::SampleWaterHeight(pos, waterHeight) && waterHeight + 0.5f > pos.y)
		{
			pos.y = waterHeight + 0.5f;
		}

		const glm::quat rotation = glm::angleAxis(atan2(forward.x, forward.z), VEC3_UP);
		GameObject* vehicle = GameObject::Deitemize(vehicleID, pos, rotation, GameObjectStack::UserData{});
		if (vehicle != nullptr)
		{
			g_SceneManager->CurrentScene()->AddRootObject(vehicle);
		}
		return vehicle;
	}

	namespace
	{
		// Whether moving the player to the debug camera (F8) switches back to the player's camera afterwards
		bool s_bPossessPlayerAfterTeleport = true;
		// The camera to return to when moving the player to the debug camera (the last one which possessed the player)
		std::string s_LastPlayerCameraName = "first-person";

		bool ImGuiWarningButton(const char* label)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColour);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColour);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColour);
			const bool bPressed = ImGui::Button(label);
			ImGui::PopStyleColor(3);
			return bPressed;
		}

		void ImGuiTooltip(const char* text)
		{
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", text);
			}
		}

		void ImGuiSectionLabel(const char* text)
		{
			ImGui::Spacing();
			ImGui::TextDisabled("%s", text);
		}

		// Lists every prefab (grouped by kind, from the loaded prefab templates) with buttons to add it to the player's inventory
		void DrawImGuiGiveItems(Player* player)
		{
			static char filter[64] = {};
			static i32 giveCount = 1;

			if (ImGuiWarningButton("Clear inventory"))
			{
				player->ResetInventory();
			}
			ImGuiTooltip("Empties every inventory slot (unequipping anything worn)");
			ImGui::SameLine();
			if (ImGui::Button("Give starter kit"))
			{
				player->AddStarterKit();
			}
			ImGuiTooltip("The tools a new world starts with (pickaxe, axe & shovel)");

			ImGui::PushItemWidth(100.0f);
			if (ImGui::InputInt("Quantity", &giveCount))
			{
				giveCount = glm::clamp(giveCount, 1, 9999);
			}
			ImGui::PopItemWidth();
			ImGui::SameLine();
			ImGui::PushItemWidth(-1.0f);
			ImGui::InputText("##item filter", filter, ARRAY_LENGTH(filter));
			ImGui::PopItemWidth();
			ImGuiTooltip("Filter by name");

			std::string filterLower(filter);
			ToLower(filterLower);

			enum Category
			{
				TOOLS,
				WEARABLES,
				VEHICLES,
				PLACEABLES,
				OTHER,
				_COUNT
			};
			static const char* categoryNames[] = { "Tools", "Wearables", "Vehicles & boats", "Placeables & machines", "Resources & other prefabs" };
			static_assert(ARRAY_LENGTH(categoryNames) == (u32)Category::_COUNT, "categoryNames must match Category");

			std::vector<ResourceManager::PrefabTemplateInfo*> categoryPrefabs[Category::_COUNT];
			for (ResourceManager::PrefabTemplateInfo& prefabInfo : g_ResourceManager->prefabTemplates)
			{
				GameObject* templateObject = prefabInfo.templateObject;
				if (templateObject == nullptr || !prefabInfo.prefabID.IsValid())
				{
					continue;
				}

				if (!filterLower.empty())
				{
					std::string nameLower = templateObject->GetName();
					ToLower(nameLower);
					if (nameLower.find(filterLower) == std::string::npos)
					{
						continue;
					}
				}

				const StringID typeID = templateObject->GetTypeID();
				Category category;
				if (IsHandToolType(typeID))
				{
					category = TOOLS;
				}
				else if (templateObject->IsWearable())
				{
					category = WEARABLES;
				}
				else if (typeID == VehicleSID || typeID == RowBoatSID)
				{
					category = VEHICLES;
				}
				else if (templateObject->IsItemizable())
				{
					category = PLACEABLES;
				}
				else
				{
					category = OTHER;
				}
				categoryPrefabs[category].push_back(&prefabInfo);
			}

			real widestName = 0.0f;
			for (const std::vector<ResourceManager::PrefabTemplateInfo*>& prefabs : categoryPrefabs)
			{
				for (ResourceManager::PrefabTemplateInfo* prefabInfo : prefabs)
				{
					widestName = glm::max(widestName, ImGui::CalcTextSize(prefabInfo->templateObject->GetName().c_str()).x);
				}
			}
			const real buttonColumnX = ImGui::GetCursorPosX() + ImGui::GetTreeNodeToLabelSpacing() + widestName + 12.0f;
			for (i32 c = 0; c < (i32)Category::_COUNT; ++c)
			{
				std::vector<ResourceManager::PrefabTemplateInfo*>& prefabs = categoryPrefabs[c];
				if (prefabs.empty())
				{
					continue;
				}

				std::sort(prefabs.begin(), prefabs.end(), [](ResourceManager::PrefabTemplateInfo* a, ResourceManager::PrefabTemplateInfo* b)
				{
					return a->templateObject->GetName() < b->templateObject->GetName();
				});

				if (ImGui::TreeNodeEx(categoryNames[c], ImGuiTreeNodeFlags_DefaultOpen, "%s (%u)", categoryNames[c], (u32)prefabs.size()))
				{
					if (c == OTHER)
					{
						ImGuiTooltip("Prefabs which aren't flagged as items. They can still be carried & placed");
					}

					for (ResourceManager::PrefabTemplateInfo* prefabInfo : prefabs)
					{
						ImGui::PushID(prefabInfo);

						ImGui::AlignTextToFramePadding();
						ImGui::Text("%s", prefabInfo->templateObject->GetName().c_str());
						ImGui::SameLine(buttonColumnX);

						char giveLabel[32];
						snprintf(giveLabel, ARRAY_LENGTH(giveLabel), "+%d", giveCount);
						if (ImGui::Button(giveLabel))
						{
							player->AddToInventory(prefabInfo->prefabID, giveCount);
						}

						ImGui::SameLine();
						const u32 maxStackSize = g_ResourceManager->GetMaxStackSize(prefabInfo->prefabID);
						char stackLabel[32];
						snprintf(stackLabel, ARRAY_LENGTH(stackLabel), "+%u (stack)", maxStackSize);
						if (ImGui::Button(stackLabel))
						{
							player->AddToInventory(prefabInfo->prefabID, (i32)maxStackSize);
						}

						ImGui::PopID();
					}

					ImGui::TreePop();
				}
			}

			ImGui::TextDisabled("Items which don't fit in the inventory are discarded");
		}
	} // namespace

	void FlexEngine::MoveDebugCameraToPlayer()
	{
		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		BaseCamera* debugCamera = g_CameraManager->GetCameraByName("debug");
		if (player == nullptr || debugCamera == nullptr)
		{
			PrintWarn("Moving the debug camera to the player needs a player & a debug camera\n");
			return;
		}

		BaseCamera* currentCamera = g_CameraManager->CurrentCamera();
		if (currentCamera->bPossessPlayer)
		{
			s_LastPlayerCameraName = currentCamera->GetName();
		}

		debugCamera->position = player->GetEyePosition();
		debugCamera->forward = player->GetLookDirection();
		debugCamera->CalculateYawAndPitchFromForward();
		debugCamera->CalculateAxisVectorsFromPitchAndYaw();

		if (currentCamera != debugCamera)
		{
			g_CameraManager->SetCamera(debugCamera, false);
		}
	}

	void FlexEngine::MovePlayerToDebugCamera(bool bExact, bool bPossessPlayer)
	{
		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		BaseCamera* debugCamera = g_CameraManager->GetCameraByName("debug");
		if (player == nullptr || debugCamera == nullptr)
		{
			PrintWarn("Moving the player to the debug camera needs a player & a debug camera\n");
			return;
		}

		if (player->GetRidingVehicleID().IsValid())
		{
			player->SetRidingVehicle(nullptr);
		}

		// Face the way the camera faces
		const glm::vec3 camForward = glm::normalize(debugCamera->forward);
		glm::vec2 forward2D(camForward.x, camForward.z);
		if (glm::length(forward2D) > 1e-4f)
		{
			forward2D = glm::normalize(forward2D);
			player->GetTransform()->SetWorldRotation(glm::angleAxis(atan2(forward2D.x, forward2D.y), VEC3_UP));
		}
		const real pitch = asin(glm::clamp(camForward.y, -1.0f, 1.0f));
		player->SetPitch(pitch);
		if ((player->GetLookDirection().y > 0.0f) != (camForward.y > 0.0f))
		{
			player->SetPitch(-pitch);
		}

		// With the player's eyes at the camera
		const glm::vec3 eyeOffset = player->GetEyePosition() - player->GetTransform()->GetWorldPosition();
		const glm::vec3 bodyPos = debugCamera->position - eyeOffset;
		if (TerrainGenerator::GetActive() != nullptr)
		{
			// The shared teleport path (fades from black until landed). Exact keeps the position if it's clear & standing
			// on something, otherwise (& when not exact) the player is put on the ground below (or the nearest dry land)
			player->TeleportOnTerrain(bodyPos, bExact);
		}
		else
		{
			player->GetRigidBody()->SetWorldPosition(bodyPos);
			player->GetTransform()->SetWorldPosition(bodyPos);
			player->GetRigidBody()->GetRigidBodyInternal()->setLinearVelocity(btVector3(0, 0, 0));
		}

		if (bPossessPlayer && !g_CameraManager->CurrentCamera()->bPossessPlayer)
		{
			if (g_CameraManager->GetCameraByName(s_LastPlayerCameraName) == nullptr)
			{
				s_LastPlayerCameraName = "first-person";
			}
			g_CameraManager->SetCameraByName(s_LastPlayerCameraName, false);
		}
	}

	void FlexEngine::DrawImGuiMainToolbar()
	{
		// Simulation
		const ImVec2 buttonSize(84.0f, 0.0f);
		if (m_bSimulationPaused)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.3f, 0.05f, 1.0f));
			if (ImGui::Button("Resume", buttonSize))
			{
				m_bSimulationPaused = false;
				m_bSimulateNextFrame = true;
			}
			ImGui::PopStyleColor();
			ImGuiTooltip("Simulation is paused (F5 resumes)");
		}
		else if (ImGui::Button("Pause", buttonSize))
		{
			m_bSimulationPaused = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Step"))
		{
			StepSimulationFrame();
		}
		ImGuiTooltip("Simulate one frame, then pause (F10)");
		ImGui::SameLine();
		ImGui::PushItemWidth(-1.0f);
		ImGui::SliderFloat("##simulation speed", &m_SimulationSpeed, 0.0001f, 10.0f, "Speed %.2fx", 3.0f);
		ImGui::PopItemWidth();
		if (ImGui::IsItemClicked(1))
		{
			m_SimulationSpeed = 1.0f;
		}
		ImGuiTooltip("Simulation speed (right click to reset)");

		// Most used debug toggles
		PhysicsDebuggingSettings& physicsDebuggingSettings = g_Renderer->GetPhysicsDebuggingSettings();
		bool bPhysicsWireframe = physicsDebuggingSettings.bDrawWireframe && !physicsDebuggingSettings.bDisableAll;
		if (ImGui::Checkbox("Physics wireframe (P)", &bPhysicsWireframe))
		{
			physicsDebuggingSettings.bDrawWireframe = bPhysicsWireframe;
			if (bPhysicsWireframe)
			{
				physicsDebuggingSettings.bDisableAll = false;
			}
		}
		ImGuiTooltip("Draws collision shapes near the camera (more options in the Physics tab)");
		ImGui::SameLine();
		bool bRenderEditorObjs = IsRenderingEditorObjects();
		if (ImGui::Checkbox("Editor objects", &bRenderEditorObjs))
		{
			SetRenderingEditorObjects(bRenderEditorObjs);
		}

		// Cameras & teleports
		BaseCamera* currentCamera = g_CameraManager->CurrentCamera();
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Camera: %s", currentCamera->GetName().c_str());
		ImGui::SameLine();
		if (ImGui::Button("Next"))
		{
			g_CameraManager->CycleCamera(1, false);
		}
		ImGuiTooltip("Cycle cameras");

		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		if (player != nullptr)
		{
			if (ImGui::Button("Camera to player (F6)"))
			{
				MoveDebugCameraToPlayer();
			}
			ImGuiTooltip("Move the debug camera to the player & switch to it (F6)");
			ImGui::SameLine();
			if (ImGui::Button("Player to camera (F8)"))
			{
				MovePlayerToDebugCamera(false, s_bPossessPlayerAfterTeleport);
			}
			ImGuiTooltip("Move the player onto the ground below the debug camera (F8, Shift + F8 to place exactly)");
		}
		else
		{
			ImGui::SameLine();
			if (ImGui::Button("Spawn player"))
			{
				g_SceneManager->CurrentScene()->SpawnPlayer(currentCamera->position, currentCamera->forward);
			}
			ImGuiTooltip("Spawn a player at the camera");
		}

		// Frame time
		static ms frameTimeMean = 0.0f;
		static u32 framesSinceUpdate = 0;
		if ((framesSinceUpdate++ % 20) == 0)
		{
			frameTimeMean = m_FrameTimes.ComputeMean(20);
		}
		ImGui::TextDisabled("%.2f ms (%u FPS)  |  %s", frameTimeMean, frameTimeMean > 0.0f ? (u32)(1000.0f / frameTimeMean) : 0u,
			g_SceneManager->CurrentScene()->GetName().c_str());
	}

	void FlexEngine::DrawImGuiSceneTab()
	{
		BaseScene* currentScene = g_SceneManager->CurrentScene();

		if (ImGui::Button("Save scene"))
		{
			currentScene->SerializeToFile(false);
		}
		ImGuiTooltip("Ctrl + S");
		ImGui::SameLine();
		if (ImGui::Button("Reload scene"))
		{
			g_SceneManager->ReloadCurrentScene();
		}
		ImGuiTooltip("R");

		g_SceneManager->DrawImGuiObjects();

		ImGuiSectionLabel("Transform gizmo");
		if (ImGui::RadioButton("Translate", g_Editor->GetTransformState() == TransformState::TRANSLATE))
		{
			g_Editor->SetTransformState(TransformState::TRANSLATE);
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Rotate", g_Editor->GetTransformState() == TransformState::ROTATE))
		{
			g_Editor->SetTransformState(TransformState::ROTATE);
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Scale", g_Editor->GetTransformState() == TransformState::SCALE))
		{
			g_Editor->SetTransformState(TransformState::SCALE);
		}

		ImGuiSectionLabel("Selected object & hierarchy");
		currentScene->DrawImGuiForSelectedObjectsAndSceneHierarchy();
	}

	void FlexEngine::DrawImGuiRenderTab()
	{
		if (ImGui::Button("Reload shaders"))
		{
			g_Renderer->RecompileShaders(false);
		}
		ImGuiTooltip("Ctrl + R");
		ImGui::SameLine();
		if (ImGui::Button("Recapture reflection probe"))
		{
			g_Renderer->RecaptureReflectionProbe();
		}

		bool bPreviewShadows = g_Renderer->GetDisplayShadowCascadePreview();
		if (ImGui::Checkbox("Shadow cascade preview", &bPreviewShadows))
		{
			g_Renderer->SetDisplayShadowCascadePreview(bPreviewShadows);
		}

		g_Renderer->DrawImGuiSettings();
		g_Window->DrawImGuiObjects();
		g_CameraManager->DrawImGuiObjects();

		ImGuiSectionLabel("Asset windows");
		ImGui::Checkbox("Materials", GetUIWindowOpen(SID_PAIR("materials")));
		ImGui::SameLine();
		ImGui::Checkbox("Shaders", GetUIWindowOpen(SID_PAIR("shaders")));
		ImGui::SameLine();
		ImGui::Checkbox("Textures", GetUIWindowOpen(SID_PAIR("textures")));
		ImGui::SameLine();
		ImGui::Checkbox("Meshes", GetUIWindowOpen(SID_PAIR("meshes")));
	}

	void FlexEngine::DrawImGuiPhysicsTab()
	{
		ImGui::TextWrapped("The collision wireframe draws every Bullet collision shape near the camera. Toggle it with P.");
		ImGui::Spacing();

		g_Renderer->DrawImGuiDebugObjects();

		ImGuiSectionLabel("Simulation");
		ImGui::Checkbox("Paused", &m_bSimulationPaused);
		ImGui::SameLine();
		if (ImGui::Button("Step (F10)"))
		{
			StepSimulationFrame();
		}
		ImGui::SameLine();
		if (ImGui::Button("Continue (F5)"))
		{
			m_bSimulationPaused = false;
			m_bSimulateNextFrame = true;
		}
		ImGui::SliderFloat("Speed", &m_SimulationSpeed, 0.0001f, 10.0f);
		if (ImGui::IsItemClicked(1))
		{
			m_SimulationSpeed = 1.0f;
		}
		ImGuiTooltip("Right click to reset");
	}

	void FlexEngine::DrawImGuiPlayerTab()
	{
		BaseScene* currentScene = g_SceneManager->CurrentScene();
		Player* player = currentScene->GetPlayer(0);
		if (player == nullptr)
		{
			ImGui::TextWrapped("This scene has no player.");
			if (ImGui::Button("Spawn player at camera"))
			{
				BaseCamera* camera = g_CameraManager->CurrentCamera();
				currentScene->SpawnPlayer(camera->position, camera->forward);
			}
			return;
		}

		if (ImGui::CollapsingHeader("Teleport", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::Button("Debug camera to player (F6)"))
			{
				MoveDebugCameraToPlayer();
			}
			ImGuiTooltip("Move the debug camera to the player's eyes & switch to it");

			if (ImGui::Button("Player to camera: ground below (F8)"))
			{
				MovePlayerToDebugCamera(false, s_bPossessPlayerAfterTeleport);
			}
			ImGuiTooltip("Onto the ground below the debug camera (or the nearest dry land)");

			if (ImGui::Button("Player to camera: exact (Shift+F8)"))
			{
				MovePlayerToDebugCamera(true, s_bPossessPlayerAfterTeleport);
			}
			ImGuiTooltip("Keeps the camera's position if the player fits there & is standing on something, otherwise drops to the ground below");

			ImGui::Checkbox("Then switch to the player's camera", &s_bPossessPlayerAfterTeleport);

			if (ImGui::Button("Reset player position"))
			{
				player->GetController()->ResetTransformAndVelocities();
			}

			const glm::vec3 playerPos = player->GetTransform()->GetWorldPosition();
			ImGui::TextDisabled("Position: %.1f, %.1f, %.1f", playerPos.x, playerPos.y, playerPos.z);
		}

		if (ImGui::CollapsingHeader("Give items", ImGuiTreeNodeFlags_DefaultOpen))
		{
			DrawImGuiGiveItems(player);
		}

		if (ImGui::CollapsingHeader("Inventory file"))
		{
			if (ImGui::Button("Load from file"))
			{
				player->ParseInventoryFile();
			}
			ImGui::SameLine();
			if (ImGui::Button("Save to file"))
			{
				player->SerializeInventoryToFile();
			}
		}

		if (ImGui::CollapsingHeader("Spawn in front of camera"))
		{
			if (ImGui::Button("Vehicle"))
			{
				SpawnVehicleInFrontOfCamera();
			}
			ImGui::SameLine();
			if (ImGui::Button("Row boat"))
			{
				SpawnVehicleInFrontOfCamera("row boat");
			}
		}

		if (ImGui::CollapsingHeader("Player object"))
		{
			player->DrawImGuiObjects(false);
		}
	}

	void FlexEngine::DrawImGuiWorldTab()
	{
		BaseScene* currentScene = g_SceneManager->CurrentScene();

		ImGuiSectionLabel("Time of day");
		bool bTimeOfDayPaused = currentScene->GetTimeOfDayPaused();
		if (ImGui::Checkbox("Pause time of day", &bTimeOfDayPaused))
		{
			currentScene->SetTimeOfDayPaused(bTimeOfDayPaused);
		}
		real timeOfDay = currentScene->GetTimeOfDay();
		if (ImGui::SliderFloat("Time of day", &timeOfDay, 0.0f, 0.999f))
		{
			currentScene->SetTimeOfDay(glm::clamp(timeOfDay, 0.0f, 0.999f));
		}
		ImGuiTooltip("0 = noon, 0.25 = sunset, 0.5 = midnight, 0.75 = sunrise");
		struct TimePreset { const char* name; real time; };
		static const TimePreset timePresets[] = { { "Noon", 0.0f }, { "Sunset", 0.25f }, { "Midnight", 0.5f }, { "Sunrise", 0.75f } };
		for (i32 i = 0; i < (i32)ARRAY_LENGTH(timePresets); ++i)
		{
			if (i > 0)
			{
				ImGui::SameLine();
			}
			if (ImGui::Button(timePresets[i].name))
			{
				currentScene->SetTimeOfDay(timePresets[i].time);
			}
		}
		real secondsPerDay = currentScene->GetSecondsPerDay();
		if (ImGui::SliderFloat("Seconds per day", &secondsPerDay, 1.0f, 6000.0f, "%.0f", 3.0f))
		{
			currentScene->SetSecondsPerDay(secondsPerDay);
		}

		TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
		ImGuiSectionLabel("Terrain");
		if (terrainGenerator != nullptr)
		{
			ImGui::Checkbox("World map", TerrainGenerator::GetShowWorldMapPtr());
			ImGui::SameLine();
			ImGui::Checkbox("Minimap", TerrainGenerator::GetShowMinimapPtr());
			if (ImGui::Button("Select terrain object"))
			{
				g_Editor->SetSelectedObject(terrainGenerator->ID);
			}
			ImGuiTooltip("Its settings (generation, editing brush, streaming) show in the Scene tab");
		}
		else
		{
			ImGui::TextDisabled("This scene has no terrain");
		}

		ImGuiSectionLabel("Systems");
		for (System* system : g_Systems)
		{
			system->DrawImGui();
		}
	}

	void FlexEngine::DrawImGuiAudioTab()
	{
		AudioManager::DrawImGuiObjects();

		ImGuiSectionLabel("Test sounds");
		struct TestSound { const char* name; SoundEffect effect; };
		static const TestSound testSounds[] = {
			{ "Blip", SoundEffect::blip },
			{ "Drmapan", SoundEffect::drmapan },
			{ "Dud dud", SoundEffect::dud_dud_dud_dud },
			{ "Melody (O)", SoundEffect::melody_0 },
			{ "Melody fast", SoundEffect::melody_0_fast },
		};
		for (i32 i = 0; i < (i32)ARRAY_LENGTH(testSounds); ++i)
		{
			if (i > 0)
			{
				ImGui::SameLine();
			}
			if (ImGui::Button(testSounds[i].name))
			{
				AudioManager::PlaySource(s_AudioSourceIDs[(i32)testSounds[i].effect]);
			}
		}

		ImGui::TextDisabled("Tones (these used to be on the number keys)");
		const i32 toneCount = (i32)SoundEffect::synthesized_14 - (i32)SoundEffect::synthesized_00 + 1;
		for (i32 i = 0; i < toneCount; ++i)
		{
			if (i % 8 != 0)
			{
				ImGui::SameLine();
			}
			char label[8];
			snprintf(label, ARRAY_LENGTH(label), "%d", i);
			if (ImGui::Button(label, ImVec2(24.0f, 0.0f)))
			{
				AudioManager::PlaySource(s_AudioSourceIDs[(i32)SoundEffect::synthesized_00 + i]);
			}
		}
		ImGui::Text("Audio effects loaded: %u", (u32)s_AudioSourceIDs.size());
	}

	void FlexEngine::DrawImGuiStatsTab()
	{
		static const std::string titleString = (std::string("Flex Engine v") + EngineVersionString());
		ImGui::Text("%s", titleString.c_str());

		ImGui::Text("Current renderer: %s", m_RendererName.c_str());
		static ms frameTimeMean = 0.0f;
		static ms frameTimeVariance = 0.0f;
		static u32 framesSinceUpdate = 0;
		const u32 updateFrequency = 20;
		const u32 framesToAverage = 20;
		// Update variables only periodically to avoid being unreadable
		if ((framesSinceUpdate++ % updateFrequency) == 0)
		{
			frameTimeMean = m_FrameTimes.ComputeMean(framesToAverage);
			frameTimeVariance = m_FrameTimes.ComputeVariance(framesToAverage, frameTimeMean);
		}

		ImVec2 p = ImGui::GetCursorScreenPos();
		real width = 300.0f;
		real height = 100.0f;
		real minMS = 0.0f;
		real maxMS = 100.0f;
		ImGui::PlotLines("", m_FrameTimes.GetData(), m_FrameTimes.GetNumSamples(), m_FrameTimes.currentIndex, 0, minMS, maxMS, ImVec2(width, height));
		real targetFrameRate = 60.0f;
		p.y += (1.0f - (1000.0f / targetFrameRate) / (maxMS - minMS)) * height;
		ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + width, p.y), IM_COL32(128, 0, 0, 255), 1.0f);

		ImGui::Text("Frame time: %.1fms (variance: %.2f)", frameTimeMean, frameTimeVariance);
		ImGui::Text("FPS: %u", frameTimeMean > 0.0f ? (u32)((1.0f / frameTimeMean) * 1000) : 0u);
		ImGui::Text("Frames rendered: %d", g_Renderer->GetFramesRenderedCount());
		ImGui::Text("Unpaused elapsed time: %.2fs", g_SecElapsedSinceProgramStart);

		ImGuiSectionLabel("CPU profiler");
		bool bProfilerEnabled = Profiler::IsEnabled();
		if (ImGui::Checkbox("Enabled", &bProfilerEnabled))
		{
			Profiler::SetEnabled(bProfilerEnabled);
		}
		ImGui::SameLine();
		static bool bHitchLogging = false;
		if (ImGui::Checkbox("Log hitches", &bHitchLogging))
		{
			Profiler::SetHitchLogging(bHitchLogging);
		}
		ImGuiTooltip("Logs frames taking over twice the recent median, with their slowest scopes");
		if (ImGui::Button("Print report"))
		{
			Profiler::PrintReport();
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset"))
		{
			Profiler::Reset();
		}

		ImGuiSectionLabel("Windows");
		ImGui::Checkbox("GPU timings", GetUIWindowOpen(SID_PAIR("gpu timings")));
		ImGui::SameLine();
		ImGui::Checkbox("CPU stats", GetUIWindowOpen(SID_PAIR("cpu stats")));
		ImGui::SameLine();
		ImGui::Checkbox("Memory", GetUIWindowOpen(SID_PAIR("memory stats")));
		ImGui::Checkbox("Uniform buffers", GetUIWindowOpen(SID_PAIR("uniform buffers")));
		ImGui::SameLine();
		ImGui::Checkbox("Jitter detector", GetUIWindowOpen(SID_PAIR("jitter detector")));

		g_Editor->DrawImGuiObjects();

		ImGuiSectionLabel("Renderer");
		g_Renderer->DrawImGuiRendererInfo();

		ImGuiSectionLabel("Misc");
		ImGui::Checkbox("Log to console", &g_bEnableLogToConsole);
	}

	void FlexEngine::DrawImGuiHelpPopup(bool* bShowHelpWindow)
	{
		if (*bShowHelpWindow)
		{
			ImGui::OpenPopup("Help");
		}

		if (ImGui::BeginPopupModal("Help", bShowHelpWindow))
		{
			ImVec4 headerTextColor(0.7f, 0.7f, 0.7f, 1.0f);
			ImGui::Text("Show hide debug UI: Shift + 1 (after closing this window)");
			ImGui::Text("Pause/regain mouse control: ESC");
			ImGui::NewLine();
			ImGui::PushStyleColor(ImGuiCol_Text, headerTextColor);
			ImGui::Text("Cameras");
			ImGui::PopStyleColor();
			ImGui::Text("\tCycle active camera: -/+");
			ImGui::Text("\tCycle active scene: [/]");
			ImGui::Text("\tMove debug camera to player: F6");
			ImGui::Text("\tMove player to debug camera: F8 (onto the ground), Shift + F8 (exactly)");
			ImGui::NewLine();
			ImGui::PushStyleColor(ImGuiCol_Text, headerTextColor);
			ImGui::Text("Editor");
			ImGui::PopStyleColor();
			ImGui::Text("\tMove camera (debug camera): WASD + QE");
			ImGui::Text("\tMove camera (first person): arrow keys/mouse");
			ImGui::Text("\tOpen debug console: ` (backtick/tilde)");
			ImGui::Text("\tTranslate/rotate/scale gizmo: 1/2/3 (debug camera)");
			ImGui::NewLine();
			ImGui::PushStyleColor(ImGuiCol_Text, headerTextColor);
			ImGui::Text("Debugging");
			ImGui::PopStyleColor();
			ImGui::Text("\tPhysics (collision) wireframe: P");
			ImGui::Text("\tPause/continue simulation: F5, step one frame: F10");
			ImGui::Text("\tFullscreen: F11 or Alt + Enter");
			ImGui::NewLine();
			ImGui::PushStyleColor(ImGuiCol_Text, headerTextColor);
			ImGui::Text("Player controls (first/third person camera)");
			ImGui::PopStyleColor();
			ImGui::Text("\tSelect quick access slot: 1-9 & 0, or scroll wheel");
			ImGui::Text("\tInteract: E");
			ImGui::Text("\tView inventory: Tab");
			ImGui::Text("\tRun: Shift");
			ImGui::Text("\tPickup item: Click and hold");
			ImGui::Text("\tPlace item: Space");

			ImGui::EndPopup();
		}
	}

	void FlexEngine::DrawImGuiObjects()
	{
		PROFILE_AUTO("FlexEngine DrawImGuiObjects");

		glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
		if (frameBufferSize.x == 0 || frameBufferSize.y == 0)
		{
			return;
		}

		bool* bImGuiDemoWindowShowing = GetUIWindowOpen(SID_PAIR("imgui demo"));
		if (*bImGuiDemoWindowShowing)
		{
			ImGui::ShowDemoWindow(bImGuiDemoWindowShowing);
		}

		static bool bShowHelpWindow = false;

		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
#if COMPILE_RENDERDOC_API
				if (m_RenderDocAPI != nullptr && ImGui::MenuItem("Capture RenderDoc frame", "F9"))
				{
					m_bRenderDocTriggerCaptureNextFrame = true;
				}
#endif

				if (ImGui::MenuItem("Save scene", "Ctrl+S"))
				{
					g_SceneManager->CurrentScene()->SerializeToFile(false);
				}

				if (ImGui::MenuItem("Save all scenes"))
				{
					g_SceneManager->SaveAllScenes();
				}

				if (ImGui::MenuItem("Save all prefabs"))
				{
					g_ResourceManager->SerializeAllPrefabTemplates();
				}

				if (ImGui::MenuItem("Save all materials"))
				{
					g_ResourceManager->SerializeAllMaterials();
				}

				if (ImGui::BeginMenu("Reload"))
				{
					if (ImGui::MenuItem("Scene", "R"))
					{
						g_SceneManager->ReloadCurrentScene();
					}

					if (ImGui::MenuItem("Scene (hard: reload all meshes)"))
					{
						g_ResourceManager->DestroyAllLoadedMeshes();
						g_SceneManager->ReloadCurrentScene();
					}

					if (ImGui::MenuItem("Shaders", "Ctrl+R"))
					{
						g_Renderer->RecompileShaders(false);
					}

					if (ImGui::MenuItem("Shaders (force)"))
					{
						g_Renderer->RecompileShaders(true);
					}

					if (ImGui::MenuItem("Font textures (render SDFs)"))
					{
						g_ResourceManager->LoadFonts(true);
					}

					BaseScene* currentScene = g_SceneManager->CurrentScene();
					if (currentScene->HasPlayers() && ImGui::MenuItem("Player position(s)"))
					{
						if (currentScene->GetPlayer(0))
						{
							currentScene->GetPlayer(0)->GetController()->ResetTransformAndVelocities();
						}
						if (currentScene->GetPlayer(1))
						{
							currentScene->GetPlayer(1)->GetController()->ResetTransformAndVelocities();
						}
					}

					ImGui::EndMenu();
				}

				if (ImGui::MenuItem("Capture reflection probe"))
				{
					g_Renderer->RecaptureReflectionProbe();
				}

				ImGui::EndMenu();
			}

			const u32 buffSize = 256;
			const char* shaderEditorPopup = "Shader editor path##popup";
			static char shaderEditorBuf[buffSize];
			bool bOpenShaderEditorPathPopup = false;

#if COMPILE_RENDERDOC_API
			const char* renderDocDLLEditorPopup = "Renderdoc DLL path##popup";
			static char renderDocDLLBuf[buffSize];
			bool bOpenRenderDocDLLPathPopup = false;
#endif
			if (ImGui::BeginMenu("Edit"))
			{
				if (ImGui::MenuItem("Shader editor path"))
				{
					bOpenShaderEditorPathPopup = true;
					strncpy(shaderEditorBuf, m_ShaderEditorPath.c_str(), buffSize - 1);
					shaderEditorBuf[buffSize - 1] = '\0';
				}

#if COMPILE_RENDERDOC_API
				if (ImGui::MenuItem("Renderdoc DLL path"))
				{
					bOpenRenderDocDLLPathPopup = true;
					std::string renderDocDLLPath;
					ReadRenderDocSettingsFileFromDisk(renderDocDLLPath);
					strncpy(renderDocDLLBuf, renderDocDLLPath.c_str(), buffSize - 1);
					renderDocDLLBuf[buffSize - 1] = '\0';
				}
#endif

				ImGui::EndMenu();
			}

			if (bOpenShaderEditorPathPopup)
			{
				ImGui::OpenPopup(shaderEditorPopup);
			}

			ImGui::SetNextWindowSize(ImVec2(500.0f, 80.0f), ImGuiCond_Appearing);
			if (ImGui::BeginPopupModal(shaderEditorPopup, NULL))
			{
				bool bUpdate = false;
				if (ImGui::InputText("", shaderEditorBuf, buffSize, ImGuiInputTextFlags_EnterReturnsTrue))
				{
					bUpdate = true;
				}

				if (ImGui::Button("Cancel"))
				{
					ImGui::CloseCurrentPopup();
				}

				ImGui::SameLine();

				if (ImGui::Button("Confirm"))
				{
					bUpdate = true;
				}

				if (bUpdate)
				{
					m_ShaderEditorPath = std::string(shaderEditorBuf);
					SaveCommonSettingsToDisk(false);
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

#if COMPILE_RENDERDOC_API
			static bool bInvalidDLLLocation = false;
			if (bOpenRenderDocDLLPathPopup)
			{
				ImGui::OpenPopup(renderDocDLLEditorPopup);
				bInvalidDLLLocation = false;
			}

			ImGui::SetNextWindowSize(ImVec2(500.0f, 160.0f), ImGuiCond_Appearing);
			if (ImGui::BeginPopupModal(renderDocDLLEditorPopup, NULL))
			{
				bool bUpdate = false;
				if (ImGui::InputText("", renderDocDLLBuf, buffSize, ImGuiInputTextFlags_EnterReturnsTrue))
				{
					bUpdate = true;
				}

				if (ImGui::Button("Cancel"))
				{
					ImGui::CloseCurrentPopup();
				}

				ImGui::SameLine();

				if (ImGui::Button("Confirm"))
				{
					bUpdate = true;
				}

				if (bInvalidDLLLocation)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
					ImGui::TextWrapped("Invalid path - couldn't find renderdoc.dll at location provided.");
					ImGui::PopStyleColor();
				}

				if (bUpdate)
				{
					bInvalidDLLLocation = false;
					std::string dllPathRaw = std::string(renderDocDLLBuf);
					std::string dllPathClean = dllPathRaw;

					dllPathClean = ReplaceBackSlashesWithForward(dllPathClean);

					if (!dllPathClean.empty() && dllPathClean.find('.') == std::string::npos && *(dllPathClean.end() - 1) != '/')
					{
						dllPathClean += "/";
					}

					dllPathClean = ExtractDirectoryString(dllPathClean);

					if (!dllPathClean.empty() && *(dllPathClean.end() - 1) != '/')
					{
						dllPathClean += "/";
					}

					if (!FileExists(dllPathClean + "renderdoc.dll"))
					{
						bInvalidDLLLocation = true;
					}

					if (!bInvalidDLLLocation)
					{
						SaveRenderDocSettingsFileToDisk(dllPathClean);
						ImGui::CloseCurrentPopup();
					}
				}

				ImGui::EndPopup();
			}
#endif

			if (ImGui::BeginMenu("Player"))
			{
				BaseScene* scene = g_SceneManager->CurrentScene();
				Player* player = scene->GetPlayer(0);

				if (player == nullptr && ImGui::MenuItem("Spawn player at camera"))
				{
					BaseCamera* camera = g_CameraManager->CurrentCamera();
					player = scene->SpawnPlayer(camera->position, camera->forward);
				}

				if (ImGui::MenuItem("Move debug camera to player", "F6", false, player != nullptr))
				{
					MoveDebugCameraToPlayer();
				}
				if (ImGui::MenuItem("Move player to debug camera (ground)", "F8", false, player != nullptr))
				{
					MovePlayerToDebugCamera(false, s_bPossessPlayerAfterTeleport);
				}
				if (ImGui::MenuItem("Move player to debug camera (exact)", "Shift+F8", false, player != nullptr))
				{
					MovePlayerToDebugCamera(true, s_bPossessPlayerAfterTeleport);
				}
				if (ImGui::MenuItem("Reset player position", nullptr, false, player != nullptr))
				{
					player->GetController()->ResetTransformAndVelocities();
					Player* player1 = scene->GetPlayer(1);
					if (player1 != nullptr)
					{
						player1->GetController()->ResetTransformAndVelocities();
					}
				}

				ImGui::Separator();

				ImGui::MenuItem("Give items window", nullptr, GetUIWindowOpen(SID_PAIR("give items")), player != nullptr);

				if (player != nullptr && ImGui::BeginMenu("Inventory"))
				{
					if (ImGui::MenuItem("Give starter kit"))
					{
						player->AddStarterKit();
					}

					if (ImGui::MenuItem("Parse from file"))
					{
						player->ParseInventoryFile();
					}

					if (ImGui::MenuItem("Save to file"))
					{
						player->SerializeInventoryToFile();
					}

					ImGui::Separator();

					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
					if (ImGui::MenuItem("Clear"))
					{
						player->ResetInventory();
					}
					ImGui::PopStyleColor();

					ImGui::EndMenu();
				}

				ImGui::Separator();

				if (ImGui::MenuItem("Spawn vehicle in front of camera"))
				{
					SpawnVehicleInFrontOfCamera();
				}

				if (ImGui::MenuItem("Spawn row boat in front of camera"))
				{
					SpawnVehicleInFrontOfCamera("row boat");
				}

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Window"))
			{
				ImGui::MenuItem("Main Window", nullptr, GetUIWindowOpen(SID_PAIR("main")));
				ImGui::MenuItem("Give Items", nullptr, GetUIWindowOpen(SID_PAIR("give items")));
				ImGui::Separator();
				ImGui::TextDisabled("Profiling");
				ImGui::MenuItem("GPU Timings", nullptr, GetUIWindowOpen(SID_PAIR("gpu timings")));
				ImGui::MenuItem("CPU Stats", nullptr, GetUIWindowOpen(SID_PAIR("cpu stats")));
				ImGui::MenuItem("Memory Stats", nullptr, GetUIWindowOpen(SID_PAIR("memory stats")));
				ImGui::MenuItem("Uniform Buffers", nullptr, GetUIWindowOpen(SID_PAIR("uniform buffers")));
				ImGui::MenuItem("Jitter Detector", nullptr, GetUIWindowOpen(SID_PAIR("jitter detector")));
				ImGui::Separator();
				ImGui::TextDisabled("Assets");
				ImGui::MenuItem("Materials", nullptr, GetUIWindowOpen(SID_PAIR("materials")));
				ImGui::MenuItem("Shaders", nullptr, GetUIWindowOpen(SID_PAIR("shaders")));
				ImGui::MenuItem("Textures", nullptr, GetUIWindowOpen(SID_PAIR("textures")));
				ImGui::MenuItem("Meshes", nullptr, GetUIWindowOpen(SID_PAIR("meshes")));
				ImGui::MenuItem("Prefabs", nullptr, GetUIWindowOpen(SID_PAIR("prefabs")));
				ImGui::MenuItem("Sounds", nullptr, GetUIWindowOpen(SID_PAIR("audio")));
				ImGui::MenuItem("Font Editor", nullptr, GetUIWindowOpen(SID_PAIR("fonts")));
				ImGui::Separator();
				ImGui::TextDisabled("Tools");
				ImGui::MenuItem("UI Editor", nullptr, GetUIWindowOpen(SID_PAIR("ui editor")));
				ImGui::MenuItem("Particle manager", nullptr, GetUIWindowOpen(SID_PAIR("particle manager")));
				ImGui::MenuItem("Input Bindings", nullptr, GetUIWindowOpen(SID_PAIR("input bindings")));
#if COMPILE_RENDERDOC_API
				ImGui::MenuItem("Render Doc Captures", nullptr, GetUIWindowOpen(SID_PAIR("render doc")));
#endif
				ImGui::MenuItem("ImGui Demo Window", nullptr, GetUIWindowOpen(SID_PAIR("imgui demo")));

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("View"))
			{
				PhysicsDebuggingSettings& physicsDebuggingSettings = g_Renderer->GetPhysicsDebuggingSettings();
				bool bPhysicsWireframe = physicsDebuggingSettings.bDrawWireframe && !physicsDebuggingSettings.bDisableAll;
				if (ImGui::MenuItem("Physics wireframe", "P", &bPhysicsWireframe))
				{
					physicsDebuggingSettings.bDrawWireframe = bPhysicsWireframe;
					if (bPhysicsWireframe)
					{
						physicsDebuggingSettings.bDisableAll = false;
					}
				}

				bool bDisplayBoundingVolumes = g_Renderer->IsDisplayBoundingVolumesEnabled();
				if (ImGui::MenuItem("Bounding volumes", nullptr, &bDisplayBoundingVolumes))
				{
					g_Renderer->SetDisplayBoundingVolumesEnabled(bDisplayBoundingVolumes);
				}

				bool bRenderEditorObjs = IsRenderingEditorObjects();
				if (ImGui::MenuItem("Editor objects", nullptr, &bRenderEditorObjs))
				{
					SetRenderingEditorObjects(bRenderEditorObjs);
				}

				bool bShowGrid = g_Editor->IsShowingGrid();
				if (ImGui::MenuItem("Grid", nullptr, &bShowGrid))
				{
					g_Editor->SetShowGrid(bShowGrid);
				}

				bool bPreviewShadows = g_Renderer->GetDisplayShadowCascadePreview();
				if (ImGui::MenuItem("Shadow cascades", nullptr, &bPreviewShadows))
				{
					g_Renderer->SetDisplayShadowCascadePreview(bPreviewShadows);
				}

				ImGui::Separator();

				const bool bHasTerrain = TerrainGenerator::GetActive() != nullptr;
				ImGui::MenuItem("Terrain map", nullptr, TerrainGenerator::GetShowWorldMapPtr(), bHasTerrain);
				ImGui::MenuItem("Terrain minimap", nullptr, TerrainGenerator::GetShowMinimapPtr(), bHasTerrain);

				ImGui::Separator();

				if (ImGui::MenuItem("Help (?)", nullptr, &bShowHelpWindow))
				{
					bShowHelpWindow = true;
				}

				ImGui::EndMenu();
			}

			if (m_bSimulationPaused)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "  PAUSED (F5)");
			}

			ImGui::EndMainMenuBar();
		}

		DrawImGuiHelpPopup(&bShowHelpWindow);

		static auto windowSizeCallbackLambda = [](ImGuiSizeCallbackData* data)
		{
			FlexEngine* engine = static_cast<FlexEngine*>(data->UserData);
			engine->m_ImGuiMainWindowWidth = data->DesiredSize.x;
			engine->m_ImGuiMainWindowWidth = glm::clamp(
				engine->m_ImGuiMainWindowWidth,
				engine->m_ImGuiMainWindowWidthMin,
				engine->m_ImGuiMainWindowWidthMax);
		};

		bool bIsMainWindowCollapsed = true;

		m_ImGuiMainWindowWidthMax = frameBufferSize.x - 100.0f;
		bool* bShowMainWindow = GetUIWindowOpen(SID_PAIR("main"));
		if (*bShowMainWindow)
		{
			static const std::string titleString = (std::string("Flex Engine v") + EngineVersionString());
			static const char* titleCharStr = titleString.c_str();
			ImGuiWindowFlags mainWindowFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavInputs;
			ImGui::SetNextWindowSizeConstraints(ImVec2(350, 300),
				ImVec2((real)frameBufferSize.x, (real)frameBufferSize.y),
				windowSizeCallbackLambda, this);
			real menuHeight = 20.0f;
			ImGui::SetNextWindowPos(ImVec2(0.0f, menuHeight), ImGuiCond_Once);
			real frameBufferHeight = (real)frameBufferSize.y;
			ImGui::SetNextWindowSize(ImVec2(m_ImGuiMainWindowWidth, frameBufferHeight - menuHeight),
				ImGuiCond_Always);
			if (ImGui::Begin(titleCharStr, bShowMainWindow, mainWindowFlags))
			{
				bIsMainWindowCollapsed = ImGui::IsWindowCollapsed();

				DrawImGuiMainToolbar();

				ImGui::Spacing();

				if (ImGui::BeginTabBar("main window tabs", ImGuiTabBarFlags_FittingPolicyScroll))
				{
					struct Tab
					{
						const char* name;
						void (FlexEngine::*draw)();
					};
					static const Tab tabs[] = {
						{ "Scene", &FlexEngine::DrawImGuiSceneTab },
						{ "Render", &FlexEngine::DrawImGuiRenderTab },
						{ "Physics", &FlexEngine::DrawImGuiPhysicsTab },
						{ "Player", &FlexEngine::DrawImGuiPlayerTab },
						{ "World", &FlexEngine::DrawImGuiWorldTab },
						{ "Audio", &FlexEngine::DrawImGuiAudioTab },
						{ "Stats", &FlexEngine::DrawImGuiStatsTab },
					};
					// FLEX_DEBUG_TAB=<name> opens a tab at startup (for screenshots)
					static const char* startupTabName = getenv("FLEX_DEBUG_TAB");
					for (const Tab& tab : tabs)
					{
						ImGuiTabItemFlags tabFlags = 0;
						if (startupTabName != nullptr && strcmp(startupTabName, tab.name) == 0 && g_Renderer->GetFramesRenderedCount() < 60)
						{
							tabFlags |= ImGuiTabItemFlags_SetSelected;
						}
						if (ImGui::BeginTabItem(tab.name, nullptr, tabFlags))
						{
							ImGui::BeginChild(tab.name);
							(this->*tab.draw)();
							ImGui::EndChild();
							ImGui::EndTabItem();
						}
					}
					ImGui::EndTabBar();
				}
			}
			ImGui::End();
		}

		bool* bShowGiveItemsWindow = GetUIWindowOpen(SID_PAIR("give items"));
		if (*bShowGiveItemsWindow)
		{
			ImGui::SetNextWindowSize(ImVec2(420.0f, 520.0f), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Give Items", bShowGiveItemsWindow))
			{
				Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
				if (player != nullptr)
				{
					DrawImGuiGiveItems(player);
				}
				else
				{
					ImGui::TextDisabled("This scene has no player");
				}
			}
			ImGui::End();
		}

		g_SceneManager->DrawImGuiModals();

		g_Renderer->DrawImGuiWindows();

		g_ResourceManager->DrawImGuiWindows();

		TerrainGenerator::DrawActiveImGuiWindows();

		bool* bShowInputBindingsWindow = GetUIWindowOpen(SID_PAIR("input bindings"));
		if (*bShowInputBindingsWindow)
		{
			g_InputManager->DrawImGuiBindings(bShowInputBindingsWindow);
		}

		if (m_bShowingConsole)
		{
			const real consoleWindowWidth = 350.0f;
			float fontScale = ImGui::GetIO().FontGlobalScale;
			real consoleWindowHeight = 28.0f + m_CmdAutoCompletions.size() * 16.0f * fontScale;
			const real consoleWindowX = (*bShowMainWindow && !bIsMainWindowCollapsed) ? m_ImGuiMainWindowWidth : 0.0f;
			const real consoleWindowY = frameBufferSize.y - consoleWindowHeight;
			ImGui::SetNextWindowPos(ImVec2(consoleWindowX, consoleWindowY), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(consoleWindowWidth, consoleWindowHeight));
			if (ImGui::Begin("Console", &m_bShowingConsole,
				ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove))
			{
				if (m_bShouldFocusKeyboardOnConsole)
				{
					m_bShouldFocusKeyboardOnConsole = false;
					ImGui::SetKeyboardFocusHere();
				}
				const bool bWasInvalid = m_bInvalidCmdLine;
				if (bWasInvalid)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));
				}
				bool bFocusTextBox = false;
				for (u32 i = 0; i < (u32)m_CmdAutoCompletions.size(); ++i)
				{
					const std::string& str = m_CmdAutoCompletions[i];
					if (ImGui::Selectable(str.c_str(), i == (u32)m_SelectedCmdLineAutoCompleteIndex))
					{
						m_SelectedCmdLineAutoCompleteIndex = (i32)i;
						strncpy(m_CmdLineStrBuf, m_CmdAutoCompletions[i].c_str(), MAX_CHARS_CMD_LINE_STR - 1);
						m_CmdLineStrBuf[MAX_CHARS_CMD_LINE_STR - 1] = '\0';
						bFocusTextBox = true;
					}
				}

				char cmdLineStrBufCopy[MAX_CHARS_CMD_LINE_STR];
				memcpy(cmdLineStrBufCopy, m_CmdLineStrBuf, MAX_CHARS_CMD_LINE_STR);
				if (ImGui::InputTextEx("", m_CmdLineStrBuf, MAX_CHARS_CMD_LINE_STR, ImVec2(consoleWindowWidth - 16.0f, consoleWindowHeight - 8.0f),
					ImGuiInputTextFlags_EnterReturnsTrue |
					ImGuiInputTextFlags_CallbackAlways |
					ImGuiInputTextFlags_CallbackHistory |
					ImGuiInputTextFlags_CallbackCompletion |
					ImGuiInputTextFlags_CallbackCharFilter |
					ImGuiInputTextFlags_DeleteCallback,
					[](ImGuiInputTextCallbackData* data) { return g_EngineInstance->ImGuiConsoleInputCallback(data); }))
				{
					m_bInvalidCmdLine = false;
					std::vector<std::string> cmdLineParts = SplitHandleStrings(m_CmdLineStrBuf, ' ');
					if (!cmdLineParts.empty())
					{
						bool bMatched = false;
						for (IFunction* func : m_ConsoleCommands)
						{
							if (strcmp(cmdLineParts[0].c_str(), func->name.c_str()) == 0)
							{
								bMatched = true;
								m_CmdAutoCompletions.clear();
								m_bShowingConsole = false;
								bool bExecuted = false;
								Variant result;
								u32 argCount = (u32)func->argTypes.size();
								if (argCount == 0 && cmdLineParts.size() == 1)
								{
									result = func->Execute();
									bExecuted = true;
								}
								else if ((argCount + 1) == cmdLineParts.size())
								{
									std::vector<Variant> args;
									args.reserve(argCount);
									bool bValid = true;
									for (u32 i = 0; i < argCount; ++i)
									{
										std::string& argStr = cmdLineParts[i + 1];
										if (argStr.empty())
										{
											bValid = false;
											break;
										}

										// TODO: Do some type checking using func->argTypes
										ValueType argType = JSONValue::TypeFromChar(argStr[0], argStr.substr(1));
										switch (argType)
										{
										case ValueType::INT:
											args.emplace_back(Variant(ParseInt(argStr)));
											break;
										case ValueType::UINT:
											args.emplace_back(Variant(ParseUInt(argStr)));
											break;
										case ValueType::LONG:
											args.emplace_back(Variant(ParseLong(argStr)));
											break;
										case ValueType::ULONG:
											args.emplace_back(Variant(ParseULong(argStr)));
											break;
										case ValueType::FLOAT:
											args.emplace_back(Variant(ParseFloat(argStr)));
											break;
										case ValueType::BOOL:
											args.emplace_back(Variant(ParseBool(argStr)));
											break;
										case ValueType::STRING:
											argStr = Erase(argStr, '\"');
											if (argStr.empty())
											{
												bValid = false;
												break;
											}

											args.emplace_back(Variant(argStr.c_str()));
											break;
										default:
											// Assume string
											args.emplace_back(Variant(argStr.c_str()));
											break;
										}
									}
									if (bValid)
									{
										result = func->Execute(args);
										bExecuted = true;
									}
								}

								if (bExecuted)
								{
									if (m_PreviousCmdLineEntries.empty() ||
										strcmp((m_PreviousCmdLineEntries.end() - 1)->c_str(), m_CmdLineStrBuf) != 0)
									{
										m_PreviousCmdLineEntries.emplace_back(m_CmdLineStrBuf);
									}
									m_PreviousCmdLineIndex = -1;
									memset(m_CmdLineStrBuf, 0, MAX_CHARS_CMD_LINE_STR);

									if (result.IsValid())
									{
										std::string resultStr = result.ToString();
										Print("Result: %s\n", resultStr.c_str());
									}
								}
								break;
							}
						}
						if (memcmp(cmdLineStrBufCopy, m_CmdLineStrBuf, MAX_CHARS_CMD_LINE_STR))
						{
							m_bInvalidCmdLine = false;
						}
						if (!bMatched)
						{
							m_bInvalidCmdLine = true;
							m_bShouldFocusKeyboardOnConsole = true;
						}
					}
				}
				if (bFocusTextBox)
				{
					ImGui::SetKeyboardFocusHere();
				}
				if (bWasInvalid)
				{
					ImGui::PopStyleColor();
				}
			}
			ImGui::End();
		}

#if COMPILE_RENDERDOC_API
		bool* bShowRenderDocWindow = GetUIWindowOpen(SID_PAIR("render doc"));
		if (*bShowRenderDocWindow)
		{
			if (ImGui::Begin("RenderDoc", bShowRenderDocWindow))
			{
				ImGui::Text("RenderDoc connected - API v%i.%i.%i", m_RenderDocAPIVerionMajor, m_RenderDocAPIVerionMinor, m_RenderDocAPIVerionPatch);
				if (ImGui::Button("Trigger capture (F9)"))
				{
					m_bRenderDocTriggerCaptureNextFrame = true;
				}

				if (m_bRenderDocTriggerCaptureNextFrame || m_bRenderDocCapturingFrame)
				{
					ImGui::Text("Capturing frame...");
				}

				if (m_RenderDocUIPID == -1)
				{
					if (ImGui::Button("Launch QRenderDoc"))
					{
						std::string captureFilePath;
						GetLatestRenderDocCaptureFilePath(captureFilePath);

						CheckForRenderDocUIRunning();

						if (m_RenderDocUIPID == -1)
						{
							m_RenderDocUIPID = m_RenderDocAPI->LaunchReplayUI(1, captureFilePath.c_str());
						}
					}
				}
				else
				{
					ImGui::Text("QRenderDoc is already running");

					// Only update periodically
					if (m_RenderDocAPICheckTimer.Update())
					{
						m_RenderDocAPICheckTimer.Restart();
						CheckForRenderDocUIRunning();
					}
				}
			}
			ImGui::End();
		}
#endif

		bool* bShowMemoryStatsWindow = GetUIWindowOpen(SID_PAIR("memory stats"));
		if (*bShowMemoryStatsWindow)
		{
			if (ImGui::Begin("Memory", bShowMemoryStatsWindow))
			{
				ImGui::Text("Memory allocated:       %.3fMB", g_TotalTrackedAllocatedMemory / 1'000'000.0f);
				ImGui::Text("Delta allocation count: %i", (i32)g_TrackedAllocationCount - (i32)g_TrackedDeallocationCount);
			}
			ImGui::End();
		}

		bool* bShowCPUStatsWindow = GetUIWindowOpen(SID_PAIR("cpu stats"));
		if (*bShowCPUStatsWindow)
		{
			if (ImGui::Begin("CPU Stats", bShowCPUStatsWindow))
			{
				ImGui::Text("Logical processor count: %u", Platform::cpuInfo.logicalProcessorCount);
				ImGui::Text("Physical core count: %u", Platform::cpuInfo.physicalCoreCount);
				ImGui::Separator();
				ImGui::Text("L1 cache count: %u", Platform::cpuInfo.l1CacheCount);
				ImGui::Text("L2 cache count: %u", Platform::cpuInfo.l2CacheCount);
				ImGui::Text("L3 cache count: %u", Platform::cpuInfo.l3CacheCount);
			}
			ImGui::End();
		}

		bool* bShowUIEditorWindow = GetUIWindowOpen(SID_PAIR("ui editor"));
		if (*bShowUIEditorWindow)
		{
			g_UIManager->DrawImGui(bShowUIEditorWindow);
		}
	}

	bool FlexEngine::ToggleUIWindow(const std::string& windowName)
	{
		std::string nameLower = windowName;
		ToLower(nameLower);
		StringID windowNameSID = Hash(nameLower.c_str());
		auto iter = m_UIWindows.find(windowNameSID);
		if (iter != m_UIWindows.end())
		{
			iter->second.bOpen = !iter->second.bOpen;
			return true;
		}
		return false;
	}

	i32 FlexEngine::ImGuiConsoleInputCallback(ImGuiInputTextCallbackData* data)
	{
		const i32 cmdHistCount = (i32)m_PreviousCmdLineEntries.size();
		if (data->EventFlag != ImGuiInputTextFlags_CallbackAlways)
		{
			m_bInvalidCmdLine = false;
		}

		auto FillOutAutoCompletions = [this](const char* buffer)
		{
			m_PreviousCmdLineIndex = -1;

			m_CmdAutoCompletions.clear();

			if (!m_bShowAllConsoleCommands && strlen(buffer) == 0)
			{
				// Don't show suggestions with empty buffer to allow navigating history
				return;
			}

			for (IFunction* func : m_ConsoleCommands)
			{
				if (StartsWith(func->name, buffer))
				{
					m_CmdAutoCompletions.push_back(func->name);
				}
			}
		};

		if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
		{
			if (m_CmdAutoCompletions.empty())
			{
				if (data->EventKey == ImGuiKey_UpArrow)
				{
					if (cmdHistCount == 0)
					{
						return -1;
					}

					if (m_PreviousCmdLineIndex == cmdHistCount - 1)
					{
						return 0;
					}

					data->DeleteChars(0, data->BufTextLen);
					++m_PreviousCmdLineIndex;
					data->InsertChars(0, m_PreviousCmdLineEntries[cmdHistCount - m_PreviousCmdLineIndex - 1].data());
				}
				else if (data->EventKey == ImGuiKey_DownArrow)
				{
					if (cmdHistCount == 0)
					{
						return -1;
					}

					if (m_PreviousCmdLineIndex == -1)
					{
						return 0;
					}

					data->DeleteChars(0, data->BufTextLen);
					--m_PreviousCmdLineIndex;
					if (m_PreviousCmdLineIndex != -1) // -1 leaves console cleared
					{
						data->InsertChars(0, m_PreviousCmdLineEntries[cmdHistCount - m_PreviousCmdLineIndex - 1].data());
					}
				}
				return 0;
			}
			else // Select auto completion entry
			{
				m_PreviousCmdLineIndex = -1;

				if (data->EventKey == ImGuiKey_UpArrow)
				{
					if (m_SelectedCmdLineAutoCompleteIndex == -1)
					{
						m_SelectedCmdLineAutoCompleteIndex = (i32)m_CmdAutoCompletions.size() - 1;
					}
					else
					{
						--m_SelectedCmdLineAutoCompleteIndex;
						m_SelectedCmdLineAutoCompleteIndex = glm::max(m_SelectedCmdLineAutoCompleteIndex, 0);
					}
				}
				else if (data->EventKey == ImGuiKey_DownArrow)
				{
					if (m_SelectedCmdLineAutoCompleteIndex != -1)
					{
						++m_SelectedCmdLineAutoCompleteIndex;
						if (m_SelectedCmdLineAutoCompleteIndex >= ((i32)m_CmdAutoCompletions.size()))
						{
							m_SelectedCmdLineAutoCompleteIndex = -1;
						}
					}
				}
			}
		}
		else if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
		{
			if (data->BufTextLen > 0)
			{
				if (m_SelectedCmdLineAutoCompleteIndex != -1)
				{
					const std::string& cmdStr = m_CmdAutoCompletions[m_SelectedCmdLineAutoCompleteIndex];
					data->DeleteChars(0, data->BufTextLen);
					data->InsertChars(0, cmdStr.data());
				}
				else
				{
					for (IFunction* func : m_ConsoleCommands)
					{
						if (StartsWith(func->name, data->Buf) && strcmp(func->name.c_str(), data->Buf) != 0)
						{
							data->DeleteChars(0, data->BufTextLen);
							data->InsertChars(0, func->name.data());
							break;
						}
					}
				}

				FillOutAutoCompletions(data->Buf);
			}
			return 0;
		}
		else if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
		{
			if (data->EventChar == L'`' ||
				data->EventChar == L'~')
			{
				m_bShowingConsole = false;
				m_SelectedCmdLineAutoCompleteIndex = -1;
				return 1;
			}

			char eventChar = (char)data->EventChar;
			// TODO: Insert at data->CursorPos
			char* cmdLine = strncat(m_CmdLineStrBuf, &eventChar, 1);

			if (m_SelectedCmdLineAutoCompleteIndex != -1)
			{
				if (!StartsWith(m_CmdAutoCompletions[m_SelectedCmdLineAutoCompleteIndex], cmdLine))
				{
					m_SelectedCmdLineAutoCompleteIndex = -1;
				}
			}

			FillOutAutoCompletions(cmdLine);
			if (m_SelectedCmdLineAutoCompleteIndex == -1)
			{
				m_SelectedCmdLineAutoCompleteIndex = 0;
			}
		}
		else if (data->EventFlag == ImGuiInputTextFlags_DeleteCallback)
		{
			char cmdLine[128];
			strncpy(cmdLine, m_CmdLineStrBuf, ARRAY_LENGTH(cmdLine) - 1);
			cmdLine[ARRAY_LENGTH(cmdLine) - 1] = '\0';

			// Delete char
			if (strlen(m_CmdLineStrBuf) == 1)
			{
				memset(cmdLine, 0, ARRAY_LENGTH(cmdLine));
			}
			else
			{
				memmove(&cmdLine[data->CursorPos + 1], &cmdLine[data->CursorPos + 2], strlen(m_CmdLineStrBuf) - data->CursorPos);
			}

			FillOutAutoCompletions(cmdLine);
			if (strlen(cmdLine) == 0)
			{
				m_SelectedCmdLineAutoCompleteIndex = -1;
			}
			else if (m_SelectedCmdLineAutoCompleteIndex == -1)
			{
				m_SelectedCmdLineAutoCompleteIndex = 0;
			}
		}
		else
		{
			if (m_PreviousCmdLineIndex == -1)
			{
				FillOutAutoCompletions(m_CmdLineStrBuf);
			}
		}

		if (m_SelectedCmdLineAutoCompleteIndex > ((i32)m_CmdAutoCompletions.size()) - 1)
		{
			m_SelectedCmdLineAutoCompleteIndex = -1;
		}

		return 0;
	}

	void FlexEngine::Stop()
	{
		m_bRunning = false;
	}

	void FlexEngine::SetSceneTestRunner(SceneTestRunner* sceneTestRunner)
	{
		m_SceneTestRunner = sceneTestRunner;
	}

	void FlexEngine::PrintAllConsoleCommands()
	{
		for (IFunction* command : m_ConsoleCommands)
		{
			Print("%s(", command->name.c_str());

			for (u32 i = 0; i < (u32)command->argTypes.size(); ++i)
			{
				Print("%s", Variant::TypeToString(command->argTypes[i]));
				if (i + 1 < (u32)command->argTypes.size())
				{
					Print(", ");
				}
			}

			Print(")");

			if (command->returnType != Variant::Type::_NONE &&
				command->returnType != Variant::Type::VOID_)
			{
				Print(" -> %s", Variant::TypeToString(command->returnType));
			}

			Print("\n");
		}
	}

	bool FlexEngine::LoadCommonSettingsFromDisk()
	{
		PROFILE_AUTO("LoadCommonSettingsFromDisk");

		if (m_CommonSettingsAbsFilePath.empty())
		{
			PrintError("Failed to read common settings to disk: file path is not set!\n");
			return false;
		}

		if (FileExists(m_CommonSettingsAbsFilePath))
		{
			if (g_bEnableLogging_Loading)
			{
				Print("Loading common settings from %s\n", m_CommonSettingsFileName.c_str());
			}

			JSONObject rootObject = {};

			if (JSONParser::ParseFromFile(m_CommonSettingsAbsFilePath, rootObject))
			{
				std::string lastOpenedSceneName = rootObject.GetString("last opened scene");
				if (m_bGameMode)
				{
					// The game starts in its own scene, the editor's is kept for next time
					m_EditorSceneName = lastOpenedSceneName;
					lastOpenedSceneName = Game::GAMEPLAY_SCENE_NAME;
				}
				if (!lastOpenedSceneName.empty())
				{
					// Don't print errors if not found, file might have been deleted since last session
					if (!g_SceneManager->SetCurrentScene(lastOpenedSceneName, false))
					{
						g_SceneManager->SetNextSceneActive();
					}
				}

				bool bRenderImGui;
				if (rootObject.TryGetBool("render imgui", bRenderImGui))
				{
					m_bRenderImGui = bRenderImGui;
					m_bEditorRenderImGui = bRenderImGui;
				}

				real masterGain;
				if (rootObject.TryGetFloat("master gain", masterGain))
				{
					AudioManager::SetMasterGain(masterGain);
				}

				bool bMuted;
				if (rootObject.TryGetBool("muted", bMuted))
				{
					AudioManager::SetMuted(bMuted);
				}

				rootObject.TryGetBool("install shader directory watch", m_bInstallShaderDirectoryWatch);

				std::string shaderEditorPath;
				if (rootObject.TryGetString("shader editor path", shaderEditorPath))
				{
					m_ShaderEditorPath = shaderEditorPath;
				}

				rootObject.TryGetBool("install terminal directory watch", m_bInstallTerminalDirectoryWatch);

				return true;
			}
			else
			{
				PrintError("Failed to parse common settings config file %s\n\terror: %s\n", m_CommonSettingsAbsFilePath.c_str(), JSONParser::GetErrorString());
				return false;
			}
		}

		return false;
	}

	// TODO: EZ: Add config window to set common settings
	void FlexEngine::SaveCommonSettingsToDisk(bool bAddEditorStr)
	{
		PROFILE_AUTO("SaveCommonSettingsToDisk");

		if (g_bHeadless)
		{
			return;
		}

		if (m_CommonSettingsAbsFilePath.empty())
		{
			PrintError("Failed to save common settings to disk: file path is not set!\n");
			return;
		}

		JSONObject rootObject = {};

		std::string lastOpenedSceneName = StripFileType(g_SceneManager->CurrentScene()->GetFileName());
		bool bRenderImGui = m_bRenderImGui;
		if (g_Game != nullptr && (g_Game->IsPlaying() || g_Game->IsMenuOpen()) && !m_EditorSceneName.empty())
		{
			// Playing the game doesn't change what the editor opens
			lastOpenedSceneName = m_EditorSceneName;
			bRenderImGui = m_bEditorRenderImGui;
		}
		rootObject.fields.emplace_back("last opened scene", JSONValue(lastOpenedSceneName));

		rootObject.fields.emplace_back("render imgui", JSONValue(bRenderImGui));

		rootObject.fields.emplace_back("master gain", JSONValue(AudioManager::GetMasterGain()));
		rootObject.fields.emplace_back("muted", JSONValue(AudioManager::IsMuted()));

		rootObject.fields.emplace_back("install shader directory watch", JSONValue(m_bInstallShaderDirectoryWatch));
		rootObject.fields.emplace_back("install terminal directory watch", JSONValue(m_bInstallTerminalDirectoryWatch));

		rootObject.fields.emplace_back("shader editor path", JSONValue(m_ShaderEditorPath));

		std::string fileContents = rootObject.ToString();

		if (!WriteFileIfChanged(m_CommonSettingsAbsFilePath, fileContents))
		{
			PrintError("Failed to write common settings config file\n");
		}

		if (bAddEditorStr)
		{
			g_Renderer->AddEditorString("Saved common settings");
		}
	}

	void FlexEngine::AppendToBootupTimesFile(const std::string& entry)
	{
		if (g_bHeadless)
		{
			return;
		}

		std::string newFileContents;
		if (FileExists(m_BootupTimesAbsFilePath))
		{
			if (!ReadFile(m_BootupTimesAbsFilePath, newFileContents, false))
			{
				PrintWarn("Failed to read bootup times file: %s\n", m_BootupTimesAbsFilePath.c_str());
				return;
			}
		}

		newFileContents += entry + std::string("\n");

		if (!WriteFile(m_BootupTimesAbsFilePath, newFileContents, false))
		{
			PrintWarn("Failed to write bootup times file: %s\n", m_BootupTimesAbsFilePath.c_str());
		}
	}

	void FlexEngine::SetFramesToFakeDT(i32 frameCount)
	{
		m_FramesToFakeDT = frameCount;
	}

	real FlexEngine::GetSimulationSpeed() const
	{
		return m_SimulationSpeed;
	}

	void FlexEngine::SetSimulationSpeed(real speed)
	{
		m_SimulationSpeed = glm::clamp(speed, 0.0f, 10.0f);
	}

	void FlexEngine::StepSimulationFrame()
	{
		m_bSimulationPaused = true;
		m_bSimulateNextFrame = true;
	}

	bool* FlexEngine::GetUIWindowOpen(StringID windowNameSID, const char* windowName)
	{
		auto iter = m_UIWindows.find(windowNameSID);
		if (iter == m_UIWindows.end())
		{
			// Default main window to being open
			bool bOpen = windowNameSID == SID("main");
			m_UIWindows[windowNameSID] = UIWindow{ bOpen, std::string(windowName) };
			return &m_UIWindows[windowNameSID].bOpen;
		}
		return &iter->second.bOpen;
	}

	void FlexEngine::ParseUIWindowCache()
	{
		PROFILE_AUTO("ParseUIWindowCache");

		if (FileExists(UI_WINDOW_CACHE_LOCATION))
		{
			std::string fileContents;
			if (ReadFile(UI_WINDOW_CACHE_LOCATION, fileContents, false))
			{
				JSONObject rootObject;
				if (JSONParser::Parse(fileContents, rootObject))
				{
					JSONObject uiWIndowsOpenObj = rootObject.GetObject("ui_windows_open");
					for (const JSONField& field : uiWIndowsOpenObj.fields)
					{
						m_UIWindows[Hash(field.label.c_str())] = UIWindow{ field.value.AsBool(), field.label };

					}
				}
			}
		}
	}

	void FlexEngine::SerializeUIWindowCache()
	{
		PROFILE_AUTO("ParseUIWindowCache");

		if (g_bHeadless)
		{
			return;
		}

		JSONObject uiWindowsOpenObj = {};
		for (auto& pair : m_UIWindows)
		{
			uiWindowsOpenObj.fields.emplace_back(pair.second.name, JSONValue(pair.second.bOpen));
		}

		JSONObject rootObject = {};
		rootObject.fields.emplace_back("ui_windows_open", JSONValue(uiWindowsOpenObj));

		std::string fileContents = rootObject.ToString();
		if (!WriteFileIfChanged(UI_WINDOW_CACHE_LOCATION, fileContents))
		{
			PrintError("Failed to serialize ui window cache to %s\n", UI_WINDOW_CACHE_LOCATION);
		}
	}

	bool FlexEngine::GetInstallTerminalWatch() const
	{
		return m_bInstallTerminalDirectoryWatch;
	}

	void FlexEngine::SetInstallTerminalWatch(bool bInstallTerminalWatch)
	{
		m_bInstallTerminalDirectoryWatch = bInstallTerminalWatch;

		GetSystem<TerminalManager>(SystemType::TERMINAL_MANAGER)->SetInstallDirectoryWatch(m_bInstallTerminalDirectoryWatch);
	}

	std::string FlexEngine::EngineVersionString()
	{
		return IntToString(EngineVersionMajor) + "." +
			IntToString(EngineVersionMinor) + "." +
			IntToString(EngineVersionPatch);
	}

	std::string FlexEngine::GetShaderEditorPath()
	{
		return m_ShaderEditorPath;
	}

	void FlexEngine::CreateCameraInstances()
	{
		PROFILE_AUTO("CreateCameraInstances");

		DebugCamera* debugCamera = new DebugCamera();
		debugCamera->position = glm::vec3(20.0f, 8.0f, -16.0f);
		debugCamera->yaw = glm::radians(130.0f);
		debugCamera->pitch = glm::radians(-10.0f);
		g_CameraManager->AddCamera(debugCamera, /* switchTo: */ true);

		FirstPersonCamera* fpCamera = new FirstPersonCamera();
		g_CameraManager->AddCamera(fpCamera, false);

		OverheadCamera* overheadCamera = new OverheadCamera();
		g_CameraManager->AddCamera(overheadCamera, false);
	}

#if COMPILE_RENDERDOC_API
	void FlexEngine::RenderDocStartCapture()
	{
		PROFILE_AUTO("RenderDocStartCapture");

		Print("Capturing RenderDoc frame...\n");
		m_RenderDocAPI->StartFrameCapture(NULL, NULL);
		m_bRenderDocCapturingFrame = true;
	}

	void FlexEngine::RenderDocEndCapture()
	{
		PROFILE_AUTO("RenderDocEndCapture");

		m_RenderDocAPI->EndFrameCapture(NULL, NULL);
		m_bRenderDocCapturingFrame = false;

		std::string captureFilePath;
		if (GetLatestRenderDocCaptureFilePath(captureFilePath))
		{
			g_Renderer->AddEditorString("Captured RenderDoc frame");

			const std::string captureFileName = StripLeadingDirectories(captureFilePath);
			Print("Captured RenderDoc frame to %s\n", captureFileName.c_str());

			CheckForRenderDocUIRunning();

			if (m_RenderDocUIPID == -1)
			{
				const std::string& cmdLineArgs = captureFilePath;
				// Auto open RenderDoc UI if not already running
				m_RenderDocUIPID = m_RenderDocAPI->LaunchReplayUI(1, cmdLineArgs.c_str());
			}
		}
	}

	void FlexEngine::TriggerRenderDocCaptureOnNextFrame()
	{
		m_bRenderDocTriggerCaptureNextFrame = true;
	}
#endif

	EventReply FlexEngine::OnMouseButtonEvent(MouseButton button, KeyAction action)
	{
		FLEX_UNUSED(button);
		FLEX_UNUSED(action);
		return EventReply::UNCONSUMED;
	}

	EventReply FlexEngine::OnKeyEvent(KeyCode keyCode, KeyAction action, i32 modifiers)
	{
		const bool bAltDown = (modifiers & (i32)InputModifier::ALT) > 0;
		const bool bShiftDown = (modifiers & (i32)InputModifier::SHIFT) > 0;

		if (action == KeyAction::KEY_PRESS)
		{
			if (keyCode == KeyCode::KEY_GRAVE_ACCENT)
			{
				m_bShowingConsole = !m_bShowingConsole;
				m_bShowAllConsoleCommands = bShiftDown;
				m_CmdAutoCompletions.clear();
				m_PreviousCmdLineIndex = -1;
				m_SelectedCmdLineAutoCompleteIndex = -1;
				if (m_bShowingConsole)
				{
					m_bShouldFocusKeyboardOnConsole = true;
				}
			}

#if COMPILE_RENDERDOC_API
			if (m_RenderDocAPI != nullptr &&
				!m_bRenderDocCapturingFrame &&
				!m_bRenderDocTriggerCaptureNextFrame &&
				keyCode == KeyCode::KEY_F9)
			{
				m_bRenderDocTriggerCaptureNextFrame = true;
				return EventReply::CONSUMED;
			}
#endif

			if (keyCode == KeyCode::KEY_F5)
			{
				m_bSimulationPaused = false;
				m_bSimulateNextFrame = true;
				return EventReply::CONSUMED;
			}

			// TODO: Handle elsewhere to handle ignoring ImGuiIO::WantCaptureKeyboard?
			if (bShiftDown && keyCode == KeyCode::KEY_1)
			{
				m_bToggleRenderImGui = true;
				return EventReply::CONSUMED;
			}

			if (keyCode == KeyCode::KEY_P)
			{
				PhysicsDebuggingSettings& settings = g_Renderer->GetPhysicsDebuggingSettings();
				settings.bDrawWireframe = !settings.bDrawWireframe;
				if (settings.bDrawWireframe)
				{
					settings.bDisableAll = false;
				}
				return EventReply::CONSUMED;
			}

			// Debug teleports (only while the debug UI is showing in the game)
			if ((keyCode == KeyCode::KEY_F6 || keyCode == KeyCode::KEY_F8) && (!m_bGameMode || m_bRenderImGui))
			{
				if (keyCode == KeyCode::KEY_F6)
				{
					MoveDebugCameraToPlayer();
				}
				else
				{
					MovePlayerToDebugCamera(bShiftDown, s_bPossessPlayerAfterTeleport);
				}
				return EventReply::CONSUMED;
			}

			if (keyCode == KeyCode::KEY_F11 ||
				(bAltDown && keyCode == KeyCode::KEY_ENTER))
			{
				g_Window->ToggleFullscreen();
				return EventReply::CONSUMED;
			}

			// (Number keys select the player's quick access slots, see PlayerController. The test tones they used to
			// play are in the debug window's Audio tab)

			if (keyCode == KeyCode::KEY_O)
			{
				AudioManager::PlaySource(s_AudioSourceIDs[bShiftDown ? (i32)SoundEffect::melody_0_fast : (i32)SoundEffect::melody_0]);
				return EventReply::CONSUMED;
			}

			if (keyCode == KeyCode::KEY_K)
			{
				m_bWriteProfilerResultsToFile = true;
				return EventReply::CONSUMED;
			}
		}

		if (action == KeyAction::KEY_PRESS || action == KeyAction::KEY_REPEAT)
		{
			if (keyCode == KeyCode::KEY_F10)
			{
				StepSimulationFrame();
				return EventReply::CONSUMED;
			}
		}

		return EventReply::UNCONSUMED;
	}

	EventReply FlexEngine::OnActionEvent(Action action, ActionEvent actionEvent)
	{
		// Gameplay scenes are for playing, not jumping between test scenes
		const bool bGameplayScene = g_SceneManager->CurrentScene()->IsGameplayScene();
		if (actionEvent == ActionEvent::ACTION_TRIGGER)
		{
			if (action == Action::DBG_ENTER_NEXT_SCENE && !bGameplayScene)
			{
				g_SceneManager->SetNextSceneActive();
				return EventReply::CONSUMED;
			}
			else if (action == Action::DBG_ENTER_PREV_SCENE && !bGameplayScene)
			{
				g_SceneManager->SetPreviousSceneActive();
				return EventReply::CONSUMED;
			}
			else if (action == Action::TAKE_SCREENSHOT)
			{
				g_Renderer->RequestScreenshot();
				return EventReply::CONSUMED;
			}
		}

		return EventReply::UNCONSUMED;
	}

#if COMPILE_RENDERDOC_API
	void FlexEngine::SetupRenderDocAPI()
	{
		std::string dllDirPath;

		if (!ReadRenderDocSettingsFileFromDisk(dllDirPath))
		{
			PrintError("Unable to setup RenderDoc API - settings file missing.\n"
				"Set path to DLL under Edit > Renderdoc DLL path\n");
			return;
		}

		if (!EndsWith(dllDirPath, "/"))
		{
			dllDirPath += "/";
		}

		std::string dllPath = dllDirPath + "renderdoc.dll";

		if (FileExists(dllPath))
		{
			m_RenderDocModule = LoadLibraryA(dllPath.c_str());

			if (m_RenderDocModule == NULL)
			{
#if defined(FLEX_64)
				const char* targetStr = "x64";
#elif defined(FLEX_32)
				const char* targetStr = "x32";
#endif

				PrintWarn("Found render doc dll but failed to load it. Is the bitness correct? (must be %s)\n", targetStr);
				return;
			}

			pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(m_RenderDocModule, "RENDERDOC_GetAPI");
			i32 ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_4_0, (void**)&m_RenderDocAPI);
			CHECK_EQ(ret, 1);

			m_RenderDocAPI->GetAPIVersion(&m_RenderDocAPIVerionMajor, &m_RenderDocAPIVerionMinor, &m_RenderDocAPIVerionPatch);
			Print("### RenderDoc API v%i.%i.%i connected, F9 to capture ###\n", m_RenderDocAPIVerionMajor, m_RenderDocAPIVerionMinor, m_RenderDocAPIVerionPatch);

			if (m_RenderDocAutoCaptureFrameOffset != -1 &&
				m_RenderDocAutoCaptureFrameCount != -1)
			{
				Print("Auto capturing %i frame(s) starting at frame %i\n", m_RenderDocAutoCaptureFrameCount, m_RenderDocAutoCaptureFrameOffset);
			}

			const std::string dateStr = Platform::GetDateString_YMDHMS();
			const std::string captureFilePath = RelativePathToAbsolute(SAVED_DIRECTORY "RenderDocCaptures/FlexEngine_" + dateStr);
			m_RenderDocAPI->SetCaptureFilePathTemplate(captureFilePath.c_str());

			m_RenderDocAPI->MaskOverlayBits(eRENDERDOC_Overlay_None, 0);
			m_RenderDocAPI->SetCaptureKeys(nullptr, 0);
			m_RenderDocAPI->SetFocusToggleKeys(nullptr, 0);

			m_RenderDocAPI->SetCaptureOptionU32(eRENDERDOC_Option_DebugOutputMute, 1);
		}
		else
		{
			PrintError("Unable to find renderdoc dll at %s\n", dllPath.c_str());
		}
	}

	void FlexEngine::CheckForRenderDocUIRunning()
	{
		if (m_RenderDocUIPID != -1)
		{
			if (!Platform::IsProcessRunning(m_RenderDocUIPID))
			{
				m_RenderDocUIPID = -1;
			}
		}
	}

	bool FlexEngine::GetLatestRenderDocCaptureFilePath(std::string& outFilePath)
	{
		u32 bufferSize;
		u32 captureIndex = 0;
		m_RenderDocAPI->GetCapture(captureIndex, nullptr, &bufferSize, nullptr);
		std::string captureFilePath(bufferSize, '\0');
		u32 result = m_RenderDocAPI->GetCapture(captureIndex, (char*)captureFilePath.data(), &bufferSize, nullptr);
		if (result != 0 && bufferSize != 0)
		{
			captureFilePath.pop_back(); // Remove trailing null terminator
			outFilePath = captureFilePath;
			return true;
		}

		return false;
	}

	bool FlexEngine::ReadRenderDocSettingsFileFromDisk(std::string& dllDirPathOut)
	{
		if (FileExists(m_RenderDocSettingsAbsFilePath))
		{
			JSONObject rootObject;
			if (JSONParser::ParseFromFile(m_RenderDocSettingsAbsFilePath, rootObject))
			{
				dllDirPathOut = rootObject.GetString("lib path");
				return true;
			}
			else
			{
				PrintError("Failed to parse %s\n\terror: %s\n", m_RenderDocSettingsFileName.c_str(), JSONParser::GetErrorString());
			}
		}
		return false;
	}

	void FlexEngine::SaveRenderDocSettingsFileToDisk(const std::string& dllDir)
	{
		JSONObject rootObject;
		rootObject.fields.emplace_back("lib path", JSONValue(dllDir));
		WriteFile(m_RenderDocSettingsAbsFilePath, rootObject.ToString(), false);
	}
#endif // COMPILE_RENDERDOC_API

	glm::vec3 FlexEngine::CalculateRayPlaneIntersectionAlongAxis(
		const glm::vec3& axis,
		const glm::vec3& rayOrigin,
		const glm::vec3& rayEnd,
		const glm::vec3& planeOrigin,
		const glm::vec3& planeNorm,
		const glm::vec3& startPos,
		const glm::vec3& cameraForward,
		real& inOutOffset,
		bool bRecalculateOffset,
		glm::vec3& inOutPrevIntersectionPoint)
	{
		real intersectionDistance = CalculateRayPlaneIntersection(rayOrigin, rayEnd, planeOrigin, planeNorm, cameraForward);

		if (intersectionDistance >= 0.0f)
		{
			glm::vec3 rayDir = glm::normalize(rayEnd - rayOrigin);

			glm::vec3 intersectionPoint = rayOrigin + rayDir * intersectionDistance;

			if (bRecalculateOffset) // Mouse was clicked or wrapped
			{
				inOutOffset = glm::dot(intersectionPoint - startPos, axis);
			}
			inOutPrevIntersectionPoint = intersectionPoint;

			glm::vec3 constrainedPoint = planeOrigin + (glm::dot(intersectionPoint - planeOrigin, axis) - inOutOffset) * axis;
			return constrainedPoint;
		}

		return VEC3_ZERO;
	}

	glm::vec3 FlexEngine::CalculateRayPlaneIntersection(
		const glm::vec3& rayOrigin,
		const glm::vec3& rayEnd,
		const glm::vec3& planeOrigin,
		const glm::vec3& planeNorm,
		const glm::vec3& planeTan,
		const glm::vec3& planeBitan,
		const glm::vec3& startPos,
		const glm::vec3& cameraForward,
		glm::vec2& inOutOffset2D,
		bool bRecalculateOffset,
		glm::vec3& inOutPrevIntersectionPoint)
	{
		real intersectionDistance = CalculateRayPlaneIntersection(rayOrigin, rayEnd, planeOrigin, planeNorm, cameraForward);

		if (intersectionDistance >= 0.0f)
		{
			glm::vec3 rayDir = glm::normalize(rayEnd - rayOrigin);

			glm::vec3 intersectionPoint = rayOrigin + rayDir * intersectionDistance;

			if (bRecalculateOffset) // Mouse was clicked or wrapped
			{
				inOutOffset2D = glm::vec2(
					glm::dot(intersectionPoint - startPos, planeTan),
					glm::dot(intersectionPoint - startPos, planeBitan));
			}
			inOutPrevIntersectionPoint = intersectionPoint;

			return planeOrigin +
				(glm::dot(intersectionPoint - planeOrigin, planeTan) - inOutOffset2D.x) * planeTan +
				(glm::dot(intersectionPoint - planeOrigin, planeBitan) - inOutOffset2D.y) * planeBitan;
		}

		return VEC3_ZERO;
	}

	real FlexEngine::CalculateRayPlaneIntersection(
		const glm::vec3& rayOrigin,
		const glm::vec3& rayEnd,
		const glm::vec3& planeOrigin,
		const glm::vec3& planeNorm,
		const glm::vec3& cameraForward)
	{
		glm::vec3 rayDir = glm::normalize(rayEnd - rayOrigin);
		glm::vec3 planeN = planeNorm;
		if (glm::dot(planeN, cameraForward) > 0.0f)
		{
			planeN = -planeN;
		}

		real intersectionDistance;
		if (glm::intersectRayPlane(rayOrigin, rayDir, planeOrigin, planeN, intersectionDistance))
		{
			return intersectionDistance;
		}

		return -1.0f;
	}

	void FlexEngine::GenerateRayAtMousePos(btVector3& outRayStart, btVector3& outRayEnd)
	{
		PhysicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld();

		const real maxDist = 1000.0f;
		outRayStart = ToBtVec3(g_CameraManager->CurrentCamera()->position);
		glm::vec2 mousePos = g_InputManager->GetMousePosition();
		btVector3 rayDir = physicsWorld->GenerateDirectionRayFromScreenPos((i32)mousePos.x, (i32)mousePos.y);
		outRayEnd = outRayStart + rayDir * maxDist;
	}

	void FlexEngine::GenerateRayAtScreenCenter(btVector3& outRayStart, btVector3& outRayEnd, real maxDist)
	{
		BaseCamera* cam = g_CameraManager->CurrentCamera();
		outRayStart = ToBtVec3(cam->position);
		btVector3 rayDir = ToBtVec3(cam->forward);
		outRayEnd = outRayStart + rayDir * maxDist;
	}

	bool FlexEngine::IsSimulationPaused() const
	{
		return m_bSimulationPaused;
	}

	void FlexEngine::SetSimulationPaused(bool bPaused)
	{
		m_bSimulationPaused = bPaused;
	}

	bool FlexEngine::InstallShaderDirectoryWatch() const
	{
#if COMPILE_SHADER_COMPILER
		return m_bInstallShaderDirectoryWatch;
#else
		return false;
#endif
	}

} // namespace flex
