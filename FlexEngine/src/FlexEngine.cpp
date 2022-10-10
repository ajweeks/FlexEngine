// NOTE: These defines must be above the stdafx include
#define PL_IMPLEMENTATION 1
#define PL_IMPL_COLLECTION_BUFFER_BYTE_QTY 25'000'000

#include "stdafx.hpp"

#include "FlexEngine.hpp"

#include <stdlib.h> // For srand, rand
#include <time.h> // For time

IGNORE_WARNINGS_PUSH
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
#include "Cameras/TerminalCamera.hpp"
#include "Cameras/VehicleCamera.hpp"
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
#include "Systems/CartManager.hpp"
#include "Systems/TrackManager.hpp"
#include "Time.hpp"
#include "UI.hpp"
#include "Window/GLFWWindowWrapper.hpp"
#include "Window/Monitor.hpp"
#include "VirtualMachine/Backend/FunctionBindings.hpp"

#if COMPILE_VULKAN
#include "Graphics/Vulkan/VulkanRenderer.hpp"
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
	bool FlexEngine::s_bHasGLDebugExtension = false;

	const u32 FlexEngine::EngineVersionMajor = 0;
	const u32 FlexEngine::EngineVersionMinor = 8;
	const u32 FlexEngine::EngineVersionPatch = 8;

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

		m_FrameTimes.resize(256);

		Platform::Init();

		g_ConfigFileManager = new ConfigFileManager();
		g_PropertyCollectionManager = new PropertyCollectionManager();

		CreateWindow();

#if COMPILE_VULKAN
		{
			PROFILE_AUTO("Create Renderer");
			g_Renderer = new vk::VulkanRenderer();
		}
#endif

		g_ResourceManager = new ResourceManager();
		g_ResourceManager->Initialize();

		g_UIManager = new UIManager();
		g_Editor = new Editor();
		g_InputManager = new InputManager();
		g_CameraManager = new CameraManager();

		InitializeWindowAndRenderer();

		AudioManager::Initialize();

		Print("Bullet v%d\n", btGetVersion());

#if COMPILE_RENDERDOC_API
		if (m_RenderDocAPI != nullptr &&
			m_RenderDocAutoCaptureFrameCount != -1 &&
			m_RenderDocAutoCaptureFrameOffset == 0)
		{
			RenderDocStartCapture();
		}
#endif

		g_PhysicsManager = new PhysicsManager();
		g_PhysicsManager->Initialize();

		g_Editor->Initialize();

		g_Systems[(i32)SystemType::PLUGGABLES] = new PluggablesSystem();
		g_Systems[(i32)SystemType::PLUGGABLES]->Initialize();

		g_Systems[(i32)SystemType::ROAD_MANAGER] = new RoadManager();
		g_Systems[(i32)SystemType::ROAD_MANAGER]->Initialize();

		g_Systems[(i32)SystemType::TERMINAL_MANAGER] = new TerminalManager();
		g_Systems[(i32)SystemType::TERMINAL_MANAGER]->Initialize();

		g_Systems[(i32)SystemType::TRACK_MANAGER] = new TrackManager();

		g_Systems[(i32)SystemType::CART_MANAGER] = new CartManager();

		g_Systems[(i32)SystemType::PARTICLE_MANAGER] = new ParticleManager();
		g_Systems[(i32)SystemType::PARTICLE_MANAGER]->Initialize();

		g_ResourceManager->DiscoverMeshes();
		g_ResourceManager->ParseMaterialsFiles();
		g_ResourceManager->ParseFontFile();

		g_SceneManager = new SceneManager();

		if (!LoadCommonSettingsFromDisk())
		{
			// Common file doesn't exist or is corrupt, load first present scene
			g_SceneManager->SetNextSceneActive();
			// Ensure config file will be written to disk this frame
			m_CommonSettingsSaveTimer.Complete();
		}

		g_UIManager->Initialize();

		ImGui::CreateContext();
		SetupImGuiStyles();

		g_ResourceManager->PostInitialize();

		g_Editor->PostInitialize();

		g_Renderer->PostInitialize();

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

		i32 springCount = 6;
		m_TestSprings.reserve(springCount);
		for (i32 i = 0; i < springCount; ++i)
		{
			m_TestSprings.emplace_back(Spring<glm::vec3>());
			m_TestSprings[i].DR = 0.9f;
			m_TestSprings[i].UAF = 15.0f;
		}

		sec durationSec = Time::CurrentSeconds() - startTime;
		PROFILE_END("FlexEngine Initialize");

		ms blockDuration = Time::ConvertFormats(durationSec, Time::Format::SECOND, Time::Format::MILLISECOND);
		if (blockDuration != -1.0f && blockDuration < 20000) // Exceptionally long times are almost always due to hitting breakpoints
		{
			std::string bootupTimesEntry = Platform::GetDateString_YMDHMS() + "," + FloatToString(blockDuration, 2);
			AppendToBootupTimesFile(bootupTimesEntry);
		}

		ImGuiIO& io = ImGui::GetIO();
		m_ImGuiIniFilepathStr = IMGUI_INI_LOCATION;
		io.IniFilename = m_ImGuiIniFilepathStr.c_str();
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

		SaveCommonSettingsToDisk(false);
		g_Window->SaveToConfig();

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

		delete g_PropertyCollectionManager;
		g_PropertyCollectionManager = nullptr;

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

	void FlexEngine::CreateWindow()
	{
		PROFILE_AUTO("CreateWindow");

		CHECK_EQ(g_Window, nullptr);
		CHECK_EQ(g_Renderer, nullptr);

		const std::string titleString = "Flex Engine v" + EngineVersionString();

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
		sec frameStartTime = Time::CurrentSeconds();
		while (m_bRunning)
		{
			sec now = Time::CurrentSeconds();

			if (m_bToggleRenderImGui)
			{
				m_bToggleRenderImGui = false;
				m_bRenderImGui = !m_bRenderImGui;
			}

			g_Window->PollEvents();

			sec frameEndTime = now;

			if (m_FramesToFakeDT > 0)
			{
				--m_FramesToFakeDT;
				// Pretend this frame started m_FakeDT seconds ago to prevent spike
				frameStartTime = frameEndTime - m_FakeDT;
			}

			sec secondsElapsed = frameEndTime - frameStartTime;
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
				for (i32 i = 1; i < (i32)m_FrameTimes.size(); ++i)
				{
					m_FrameTimes[i - 1] = m_FrameTimes[i];
				}
				m_FrameTimes[m_FrameTimes.size() - 1] = g_UnpausedDeltaTime * 1000.0f;
			}

			{
				PROFILE_AUTO("Update");

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

					Player* player = currentScene->GetPlayer(0);
					if (player)
					{
						Transform* playerTransform = player->GetTransform();
						glm::vec3 targetPos = playerTransform->GetWorldPosition() + playerTransform->GetForward() * -2.0f;
						m_SpringTimer += g_DeltaTime;
						real amplitude = 1.5f;
						real period = 5.0f;
						if (m_SpringTimer > period)
						{
							m_SpringTimer -= period;
						}
						targetPos.y += pow(sin(glm::clamp(m_SpringTimer - period / 2.0f, 0.0f, PI)), 40.0f) * amplitude;
						glm::vec3 targetVel = ToVec3(player->GetRigidBody()->GetRigidBodyInternal()->getLinearVelocity());

						for (Spring<glm::vec3>& spring : m_TestSprings)
						{
							spring.SetTargetPos(targetPos);
							spring.SetTargetVel(targetVel);

							targetPos = spring.pos;
							targetVel = spring.vel;
						}
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

#if 0
				btIDebugDraw* debugDrawer = g_Renderer->GetDebugRenderer();
				for (i32 i = 0; i < (i32)m_TestSprings.size(); ++i)
				{
					m_TestSprings[i].Tick(g_DeltaTime);
					real t = (real)i / (real)m_TestSprings.size();
					debugDrawer->drawSphere(ToBtVec3(m_TestSprings[i].pos), (1.0f - t + 0.1f) * 0.5f, btVector3(0.5f - 0.3f * t, 0.8f - 0.4f * t, 0.6f - 0.2f * t));
				}
#endif

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

				g_Renderer->Update();

				g_InputManager->Update();

				g_InputManager->PostUpdate();
			}

			g_Renderer->Draw();

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

	void FlexEngine::DrawImGuiObjects()
	{
		PROFILE_AUTO("DrawImGuiObjects");

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

				if (ImGui::MenuItem("Save all prefabs"))
				{
					g_ResourceManager->SerializeAllPrefabTemplates();
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
					strncpy(shaderEditorBuf, m_ShaderEditorPath.c_str(), buffSize);
				}

#if COMPILE_RENDERDOC_API
				if (ImGui::MenuItem("Renderdoc DLL path"))
				{
					bOpenRenderDocDLLPathPopup = true;
					std::string renderDocDLLPath;
					ReadRenderDocSettingsFileFromDisk(renderDocDLLPath);
					strncpy(renderDocDLLBuf, renderDocDLLPath.c_str(), buffSize);
				}
#endif

				ImGui::Separator();

				BaseScene* scene = g_SceneManager->CurrentScene();
				Player* player = scene->GetPlayer(0);
				if (player != nullptr)
				{
					if (ImGui::BeginMenu("Player inventory"))
					{
						if (ImGui::BeginMenu("Add to inventory"))
						{
							g_ResourceManager->DrawImGuiMenuItemizableItems();

							ImGui::EndMenu();
						}

						ImGui::Separator();

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
							player->ClearInventory();
						}

						ImGui::PopStyleColor();

						ImGui::EndMenu();
					}
				}

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

			if (ImGui::BeginMenu("Window"))
			{
				ImGui::MenuItem("Main Window", nullptr, &m_UIWindows[SID("main")].bOpen);
				ImGui::MenuItem("GPU Timings", nullptr, &m_UIWindows[SID("gpu timings")].bOpen);
				ImGui::MenuItem("Memory Stats", nullptr, &m_UIWindows[SID("memory stats")].bOpen);
				ImGui::MenuItem("CPU Stats", nullptr, &m_UIWindows[SID("cpu stats")].bOpen);
				ImGui::MenuItem("Uniform Buffers", nullptr, &m_UIWindows[SID("uniform buffers")].bOpen);
				ImGui::MenuItem("UI Editor", nullptr, &m_UIWindows[SID("ui editor")].bOpen);
				ImGui::MenuItem("Jitter Detector", nullptr, &m_UIWindows[SID("jitter detector")].bOpen);
				ImGui::MenuItem("Particle manager", nullptr, &m_UIWindows[SID("particle manager")].bOpen);
				ImGui::Separator();
				ImGui::MenuItem("Materials", nullptr, &m_UIWindows[SID("materials")].bOpen);
				ImGui::MenuItem("Shaders", nullptr, &m_UIWindows[SID("shaders")].bOpen);
				ImGui::MenuItem("Textures", nullptr, &m_UIWindows[SID("textures")].bOpen);
				ImGui::MenuItem("Meshes", nullptr, &m_UIWindows[SID("meshes")].bOpen);
				ImGui::MenuItem("Prefabs", nullptr, &m_UIWindows[SID("prefabs")].bOpen);
				ImGui::MenuItem("Sounds", nullptr, &m_UIWindows[SID("audio")].bOpen);
				ImGui::Separator();
				ImGui::MenuItem("Input Bindings", nullptr, &m_UIWindows[SID("input bindings")].bOpen);
				ImGui::MenuItem("Font Editor", nullptr, &m_UIWindows[SID("fonts")].bOpen);
#if COMPILE_RENDERDOC_API
				ImGui::MenuItem("Render Doc Captures", nullptr, &m_UIWindows[SID("render doc")].bOpen);
#endif
				ImGui::Separator();
				ImGui::MenuItem("ImGui Demo Window", nullptr, &m_UIWindows[SID("imgui demo")].bOpen);

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("View"))
			{
				bool bPreviewShadows = g_Renderer->GetDisplayShadowCascadePreview();
				if (ImGui::MenuItem("Shadow cascades", nullptr, &bPreviewShadows))
				{
					g_Renderer->SetDisplayShadowCascadePreview(bPreviewShadows);
				}

				ImGui::EndMenu();
			}

			ImGui::EndMainMenuBar();
		}

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

				if (ImGui::TreeNode("Simulation"))
				{
					ImGui::Checkbox("Paused", &m_bSimulationPaused);

					if (ImGui::Button("Step (F10)"))
					{
						StepSimulationFrame();
					}

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

					ImGui::TreePop();
				}

				if (ImGui::TreeNode("Stats"))
				{
					static const std::string rendererNameStringStr = std::string("Current renderer: " + m_RendererName);
					static const char* renderNameStr = rendererNameStringStr.c_str();
					ImGui::TextUnformatted(renderNameStr);
					static ms latestFrameTime;
					static u32 framesSinceUpdate = 0;
					if (framesSinceUpdate++ % 10 == 0)
					{
						latestFrameTime = m_FrameTimes[m_FrameTimes.size() - 1];
					}
					ImGui::Text("Frames time: %.1fms (%d FPS)", latestFrameTime, (u32)((1.0f / latestFrameTime) * 1000));
					ImGui::NewLine();
					ImGui::Text("Frames rendered: %d", g_Renderer->GetFramesRenderedCount());
					ImGui::Text("Unpaused elapsed time: %.2fs", g_SecElapsedSinceProgramStart);
					ImGui::Text("Audio effects loaded: %u", (u32)s_AudioSourceIDs.size());

					ImVec2 p = ImGui::GetCursorScreenPos();
					real width = 300.0f;
					real height = 100.0f;
					real minMS = 0.0f;
					real maxMS = 100.0f;
					ImGui::PlotLines("", m_FrameTimes.data(), (u32)m_FrameTimes.size(), 0, 0, minMS, maxMS, ImVec2(width, height));
					real targetFrameRate = 60.0f;
					p.y += (1.0f - (1000.0f / targetFrameRate) / (maxMS - minMS)) * height;
					ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + width, p.y), IM_COL32(128, 0, 0, 255), 1.0f);

					g_Renderer->DrawImGuiRendererInfo();

					ImGui::TreePop();
				}

				if (ImGui::TreeNode("Misc settings"))
				{
					ImGui::Checkbox("Log to console", &g_bEnableLogToConsole);
					ImGui::TreePop();
				}

				g_Renderer->DrawImGuiSettings();
				g_Window->DrawImGuiObjects();
				g_CameraManager->DrawImGuiObjects();
				g_SceneManager->DrawImGuiObjects();
				AudioManager::DrawImGuiObjects();
				g_Editor->DrawImGuiObjects();

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

				BaseScene* currentScene = g_SceneManager->CurrentScene();

				currentScene->DrawImGuiForSelectedObjectsAndSceneHierarchy();

				ImGui::Spacing();
				ImGui::Spacing();
				ImGui::Spacing();

				ImGui::Text("Debugging");

				for (System* system : g_Systems)
				{
					system->DrawImGui();
				}

				if (ImGui::TreeNode("Spring"))
				{
					real* DR = &m_TestSprings[0].DR;
					real* UAF = &m_TestSprings[0].UAF;

					ImGui::SliderFloat("DR", DR, 0.0f, 2.0f);
					ImGui::SliderFloat("UAF", UAF, 0.0f, 20.0f);

					for (Spring<glm::vec3>& spring : m_TestSprings)
					{
						spring.DR = *DR;
						spring.UAF = *UAF;
					}

					ImGui::TreePop();
				}
			}
			ImGui::End();
		}

		g_SceneManager->DrawImGuiModals();

		g_Renderer->DrawImGuiWindows();

		g_ResourceManager->DrawImGuiWindows();

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
						strncpy(m_CmdLineStrBuf, m_CmdAutoCompletions[i].c_str(), m_CmdAutoCompletions[i].size());
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
			strncpy(cmdLine, m_CmdLineStrBuf, ARRAY_LENGTH(cmdLine));

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
		if (m_CommonSettingsAbsFilePath.empty())
		{
			PrintError("Failed to save common settings to disk: file path is not set!\n");
			return;
		}

		JSONObject rootObject = {};

		std::string lastOpenedSceneName = StripFileType(g_SceneManager->CurrentScene()->GetFileName());
		rootObject.fields.emplace_back("last opened scene", JSONValue(lastOpenedSceneName));

		rootObject.fields.emplace_back("render imgui", JSONValue(m_bRenderImGui));

		rootObject.fields.emplace_back("master gain", JSONValue(AudioManager::GetMasterGain()));
		rootObject.fields.emplace_back("muted", JSONValue(AudioManager::IsMuted()));

		rootObject.fields.emplace_back("install shader directory watch", JSONValue(m_bInstallShaderDirectoryWatch));

		rootObject.fields.emplace_back("shader editor path", JSONValue(m_ShaderEditorPath));

		std::string fileContents = rootObject.ToString();

		if (!WriteFile(m_CommonSettingsAbsFilePath, fileContents, false))
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
			m_UIWindows[windowNameSID] = UIWindow{ false, std::string(windowName) };
			return &m_UIWindows[windowNameSID].bOpen;
		}
		return &iter->second.bOpen;
	}

	void FlexEngine::ParseUIWindowCache()
	{
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
		JSONObject uiWindowsOpenObj = {};
		for (auto& pair : m_UIWindows)
		{
			uiWindowsOpenObj.fields.emplace_back(pair.second.name, JSONValue(pair.second.bOpen));
		}

		JSONObject rootObject = {};
		rootObject.fields.emplace_back("ui_windows_open", JSONValue(uiWindowsOpenObj));

		std::string fileContents = rootObject.ToString();
		if (!WriteFile(UI_WINDOW_CACHE_LOCATION, fileContents, false))
		{
			PrintError("Failed to serialize ui window cache to %s\n", UI_WINDOW_CACHE_LOCATION);
		}
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

		FirstPersonCamera* fpCamera = new FirstPersonCamera();
		g_CameraManager->AddCamera(fpCamera, true);

		DebugCamera* debugCamera = new DebugCamera();
		debugCamera->position = glm::vec3(20.0f, 8.0f, -16.0f);
		debugCamera->yaw = glm::radians(130.0f);
		debugCamera->pitch = glm::radians(-10.0f);
		g_CameraManager->AddCamera(debugCamera, false);

		OverheadCamera* overheadCamera = new OverheadCamera();
		g_CameraManager->AddCamera(overheadCamera, false);

		TerminalCamera* terminalCamera = new TerminalCamera();
		g_CameraManager->AddCamera(terminalCamera, false);

		VehicleCamera* vehicleCamera = new VehicleCamera();
		g_CameraManager->AddCamera(vehicleCamera, false);
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
		const bool bControlDown = (modifiers & (i32)InputModifier::CONTROL) > 0;
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

			if (keyCode == KeyCode::KEY_G)
			{
				g_Renderer->ToggleRenderGrid();
				return EventReply::CONSUMED;
			}

			// TODO: Handle elsewhere to handle ignoring ImGuiIO::WantCaptureKeyboard?
			if (bShiftDown && keyCode == KeyCode::KEY_1)
			{
				m_bToggleRenderImGui = true;
				return EventReply::CONSUMED;
			}

			if (keyCode == KeyCode::KEY_R)
			{
				if (bControlDown)
				{
					g_Renderer->RecompileShaders(false);
					return EventReply::CONSUMED;
				}
				else
				{
					g_InputManager->ClearAllInputs();

					g_SceneManager->ReloadCurrentScene();
					return EventReply::CONSUMED;
				}
			}

			if (keyCode == KeyCode::KEY_P)
			{
				PhysicsDebuggingSettings& settings = g_Renderer->GetPhysicsDebuggingSettings();
				settings.bDrawWireframe = !settings.bDrawWireframe;
				return EventReply::CONSUMED;
			}

			if (keyCode == KeyCode::KEY_F11 ||
				(bAltDown && keyCode == KeyCode::KEY_ENTER))
			{
				g_Window->ToggleFullscreen();
				return EventReply::CONSUMED;
			}

			if (keyCode == KeyCode::KEY_4)
			{
				i32 audioSourceIndex = (i32)SoundEffect::synthesized_00 + (bShiftDown ? 7 : 0);
				AudioManager::PlaySource(s_AudioSourceIDs[audioSourceIndex]);
				return EventReply::CONSUMED;
			}
			if (keyCode == KeyCode::KEY_5)
			{
				i32 audioSourceIndex = (i32)SoundEffect::synthesized_01 + (bShiftDown ? 7 : 0);
				AudioManager::PlaySource(s_AudioSourceIDs[audioSourceIndex]);
				return EventReply::CONSUMED;
			}
			if (keyCode == KeyCode::KEY_6)
			{
				i32 audioSourceIndex = (i32)SoundEffect::synthesized_02 + (bShiftDown ? 7 : 0);
				AudioManager::PlaySource(s_AudioSourceIDs[audioSourceIndex]);
				return EventReply::CONSUMED;
			}
			if (keyCode == KeyCode::KEY_7)
			{
				i32 audioSourceIndex = (i32)SoundEffect::synthesized_03 + (bShiftDown ? 7 : 0);
				AudioManager::PlaySource(s_AudioSourceIDs[audioSourceIndex]);
				return EventReply::CONSUMED;
			}
			if (keyCode == KeyCode::KEY_8)
			{
				i32 audioSourceIndex = (i32)SoundEffect::synthesized_04 + (bShiftDown ? 7 : 0);
				AudioManager::PlaySource(s_AudioSourceIDs[audioSourceIndex]);
				return EventReply::CONSUMED;
			}
			if (keyCode == KeyCode::KEY_9)
			{
				i32 audioSourceIndex = (i32)SoundEffect::synthesized_05 + (bShiftDown ? 7 : 0);
				AudioManager::PlaySource(s_AudioSourceIDs[audioSourceIndex]);
				return EventReply::CONSUMED;
			}
			if (keyCode == KeyCode::KEY_0)
			{
				i32 audioSourceIndex = (i32)SoundEffect::synthesized_06 + (bShiftDown ? 7 : 0);
				AudioManager::PlaySource(s_AudioSourceIDs[audioSourceIndex]);
				return EventReply::CONSUMED;
			}

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
		if (actionEvent == ActionEvent::ACTION_TRIGGER)
		{
			if (action == Action::DBG_ENTER_NEXT_SCENE)
			{
				g_SceneManager->SetNextSceneActive();
				return EventReply::CONSUMED;
			}
			else if (action == Action::DBG_ENTER_PREV_SCENE)
			{
				g_SceneManager->SetPreviousSceneActive();
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
		glm::vec3 rayDir = glm::normalize(rayEnd - rayOrigin);
		glm::vec3 planeN = planeNorm;
		if (glm::dot(planeN, cameraForward) > 0.0f)
		{
			planeN = -planeN;
		}
		real intersectionDistance;
		if (glm::intersectRayPlane(rayOrigin, rayDir, planeOrigin, planeN, intersectionDistance))
		{
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
