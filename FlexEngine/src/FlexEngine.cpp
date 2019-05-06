#include "stdafx.hpp"

#include "FlexEngine.hpp"

#include <stdlib.h> // For srand, rand
#include <time.h> // For time

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletDynamics/Dynamics/btDynamicsWorld.h>

#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <glm/gtx/intersect.hpp>

#include <LinearMath/btIDebugDraw.h>

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
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "JSONParser.hpp"
#include "JSONTypes.hpp"
#include "Physics/PhysicsManager.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "PlayerController.hpp"
#include "Profiler.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "Time.hpp"
#include "Window/GLFWWindowWrapper.hpp"
#include "Window/Monitor.hpp"

#if COMPILE_OPEN_GL
#include "Graphics/GL/GLRenderer.hpp"
#endif

#if COMPILE_VULKAN
#include "Graphics/Vulkan/VulkanRenderer.hpp"
#endif

// Specify that we prefer to be run on a discrete card on laptops when available
extern "C"
{
	__declspec(dllexport) DWORD NvOptimusEnablement = 0x01;
	__declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x01;
}

namespace flex
{
	bool FlexEngine::s_bHasGLDebugExtension = false;

	const u32 FlexEngine::EngineVersionMajor = 0;
	const u32 FlexEngine::EngineVersionMinor = 8;
	const u32 FlexEngine::EngineVersionPatch = 2;

	std::string FlexEngine::s_CurrentWorkingDirectory;
	std::vector<AudioSourceID> FlexEngine::s_AudioSourceIDs;


	// Globals declared in stdafx.hpp
	class Window* g_Window = nullptr;
	class CameraManager* g_CameraManager = nullptr;
	class InputManager* g_InputManager = nullptr;
	class Renderer* g_Renderer = nullptr;
	class FlexEngine* g_EngineInstance = nullptr;
	class SceneManager* g_SceneManager = nullptr;
	struct Monitor* g_Monitor = nullptr;
	class PhysicsManager* g_PhysicsManager = nullptr;

	sec g_SecElapsedSinceProgramStart = 0;
	sec g_DeltaTime = 0;


	FlexEngine::FlexEngine() :
		m_MouseButtonCallback(this, &FlexEngine::OnMouseButtonEvent),
		m_MouseMovedCallback(this, &FlexEngine::OnMouseMovedEvent),
		m_KeyEventCallback(this, &FlexEngine::OnKeyEvent),
		m_ActionCallback(this, &FlexEngine::OnActionEvent)
	{
		// TODO: Add custom seeding for different random systems
		std::srand((u32)time(NULL));

		RetrieveCurrentWorkingDirectory();

		{
			std::string configDirAbs = RelativePathToAbsolute(ROOT_LOCATION "config/");
			m_CommonSettingsFileName = "common.ini";
			m_CommonSettingsAbsFilePath = configDirAbs + m_CommonSettingsFileName;
			CreateDirectoryRecursive(configDirAbs);
		}

		{
			std::string bootupDirAbs = RelativePathToAbsolute(SAVED_LOCATION "");
			m_BootupTimesFileName = "bootup-times.log";
			m_BootupTimesAbsFilePath = bootupDirAbs + m_BootupTimesFileName;
			CreateDirectoryRecursive(bootupDirAbs);
		}

		{
			std::string renderDocSettingsDirAbs = RelativePathToAbsolute(ROOT_LOCATION "config/");
			m_RenderDocSettingsFileName = "renderdoc.ini";
			m_RenderDocSettingsAbsFilePath = renderDocSettingsDirAbs + m_RenderDocSettingsFileName;
		}

		RendererID preferredInitialRenderer = RendererID::GL;

		m_RendererIndex = RendererID::_LAST_ELEMENT;
		m_RendererCount = 0;

#if COMPILE_OPEN_GL
		++m_RendererCount;
		if (m_RendererIndex == RendererID::_LAST_ELEMENT || preferredInitialRenderer == RendererID::GL)
		{
			m_RendererIndex = RendererID::GL;
		}
#endif
#if COMPILE_VULKAN
		++m_RendererCount;
		if (m_RendererIndex == RendererID::_LAST_ELEMENT || preferredInitialRenderer == RendererID::VULKAN)
		{
			m_RendererIndex = RendererID::VULKAN;
		}
#endif

		m_RendererName = RenderIDToString(m_RendererIndex);

		InitializeLogger();

#if defined(__clang__)
		m_CompilerName = "Clang";

		m_CompilerVersion =
			IntToString(__clang_major__) + '.' +
			IntToString(__clang_minor__) + '.' +
			IntToString(__clang_patchlevel__);
#elif defined(_MSC_VER)
		m_CompilerName = "MSVC";

	#if _MSC_VER >= 1920 // TODO: Double check this value once Microsoft publishes VS2019 fully
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
			IntToString(__GNUC_MINOR__ ) +
#else
		m_CompilerName = "Unknown";
		m_CompilerVersion = "Unknown";
#endif

#if DEBUG
		const char* configStr = "Debug";
#elif DEVELOPMENT
		const char* configStr = "Development";
#elif SHIPPING
		const char* configStr = "Release";
#else
		assert(false);
#endif

		std::string nowStr = GetDateString_YMDHMS();
		Print("FlexEngine [%s] - Config: [%s x32] - Compiler: [%s %s]\n", nowStr.c_str(), configStr, m_CompilerName.c_str(), m_CompilerVersion.c_str());

		assert(m_RendererCount > 0); // At least one renderer must be enabled! (see stdafx.h)
		Print("%u renderer%s enabled, Current renderer: %s\n",
			m_RendererCount, (m_RendererCount > 1 ? "s" : ""), m_RendererName.c_str());

		DeselectCurrentlySelectedObjects();

		m_LMBDownPos = glm::vec2i(-1);
	}

	FlexEngine::~FlexEngine()
	{
		Destroy();
	}

	void FlexEngine::Initialize()
	{
		const char* profileBlockStr = "Engine initialize";
		PROFILE_BEGIN(profileBlockStr);

#if COMPILE_RENDERDOC_API
		SetupRenderDocAPI();
#endif

		g_EngineInstance = this;

		m_FrameTimes.resize(256);

		AudioManager::Initialize();

		CreateWindowAndRenderer();

		g_InputManager = new InputManager();

		g_CameraManager = new CameraManager();

		DebugCamera* debugCamera = new DebugCamera();
		debugCamera->SetPosition(glm::vec3(20.0f, 8.0f, -16.0f));
		debugCamera->SetYaw(glm::radians(130.0f));
		debugCamera->SetPitch(glm::radians(-10.0f));
		g_CameraManager->AddCamera(debugCamera, false);

		OverheadCamera* overheadCamera = new OverheadCamera();
		g_CameraManager->AddCamera(overheadCamera, false);

		FirstPersonCamera* fpCamera = new FirstPersonCamera();
		g_CameraManager->AddCamera(fpCamera, true);

		TerminalCamera* terminalCamera = new TerminalCamera();
		g_CameraManager->AddCamera(terminalCamera, false);

		InitializeWindowAndRenderer();

		g_PhysicsManager = new PhysicsManager();
		g_PhysicsManager->Initialize();

		// Transform gizmo materials
		{
			MaterialCreateInfo matCreateInfo = {};
			matCreateInfo.name = "Transform";
			matCreateInfo.shaderName = "color";
			matCreateInfo.constAlbedo = VEC3_ONE;
			matCreateInfo.engineMaterial = true;
			m_TransformGizmoMatXID = g_Renderer->InitializeMaterial(&matCreateInfo);
			m_TransformGizmoMatYID = g_Renderer->InitializeMaterial(&matCreateInfo);
			m_TransformGizmoMatZID = g_Renderer->InitializeMaterial(&matCreateInfo);
			m_TransformGizmoMatAllID = g_Renderer->InitializeMaterial(&matCreateInfo);
		}

		BaseScene::ParseFoundMeshFiles();
		BaseScene::ParseFoundMaterialFiles();
		BaseScene::ParseFoundPrefabFiles();

		g_SceneManager = new SceneManager();
		g_SceneManager->AddFoundScenes();

		LoadCommonSettingsFromDisk();

		ImGui::CreateContext();
		SetupImGuiStyles();

#if COMPILE_RENDERDOC_API
		if (m_RenderDocAPI &&
			m_RenderDocAutoCaptureFrameCount != -1 &&
			m_RenderDocAutoCaptureFrameOffset == 0)
		{
			m_bRenderDocCapturingFrame = true;
			m_RenderDocAPI->StartFrameCapture(NULL, NULL);
		}
#endif

		g_SceneManager->InitializeCurrentScene();
		g_Renderer->PostInitialize();
		g_SceneManager->PostInitializeCurrentScene();

		g_InputManager->Initialize();

		g_CameraManager->Initialize();

		g_InputManager->BindMouseButtonCallback(&m_MouseButtonCallback, 99);
		g_InputManager->BindMouseMovedCallback(&m_MouseMovedCallback, 99);
		g_InputManager->BindKeyEventCallback(&m_KeyEventCallback, 10);
		g_InputManager->BindActionCallback(&m_ActionCallback, 10);

		if (s_AudioSourceIDs.empty())
		{
			s_AudioSourceIDs.push_back(AudioManager::AddAudioSource(RESOURCE_LOCATION  "audio/dud_dud_dud_dud.wav"));
			s_AudioSourceIDs.push_back(AudioManager::AddAudioSource(RESOURCE_LOCATION  "audio/drmapan.wav"));
			s_AudioSourceIDs.push_back(AudioManager::AddAudioSource(RESOURCE_LOCATION  "audio/blip.wav"));
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 100.727f));
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 200.068f));
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 300.811f));
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 400.645f));
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 500.099f));
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 600.091f));
			s_AudioSourceIDs.push_back(AudioManager::SynthesizeSound(0.5f, 700.20f));
		}

		i32 springCount = 6;
		m_TestSprings.reserve(springCount);
		for (i32 i = 0; i < springCount; ++i)
		{
			m_TestSprings.emplace_back(Spring<glm::vec3>());
			m_TestSprings[i].DR = 0.9f;
			m_TestSprings[i].UAF = 15.0f;
		}

		PROFILE_END(profileBlockStr);
		Profiler::PrintBlockDuration(profileBlockStr);

		ms blockDuration = Profiler::GetBlockDuration(profileBlockStr);
		std::string bootupTimesEntry = GetDateString_YMDHMS() + "," + FloatToString(blockDuration, 2);
		AppendToBootupTimesFile(bootupTimesEntry);


		ImGuiIO& io = ImGui::GetIO();
		m_ImGuiIniFilepathStr = ROOT_LOCATION "config/imgui.ini";
		io.IniFilename = m_ImGuiIniFilepathStr.c_str();
		m_ImGuiLogFilepathStr = ROOT_LOCATION "config/imgui.log";
		io.LogFilename = m_ImGuiLogFilepathStr.c_str();
		io.DisplaySize = (ImVec2)g_Window->GetFrameBufferSize();
		io.IniSavingRate = 10.0f;


		memset(m_CmdLineStrBuf, 0, MAX_CHARS_CMD_LINE_STR);
		m_ConsoleCommands.emplace_back("reload.scene", []() { g_SceneManager->ReloadCurrentScene(); });
		m_ConsoleCommands.emplace_back("reload.scene.hard", []()
		{
			MeshComponent::DestroyAllLoadedMeshes();
			g_SceneManager->ReloadCurrentScene();
		});
		m_ConsoleCommands.emplace_back("reload.shaders", []() { g_Renderer->ReloadShaders(); });
		m_ConsoleCommands.emplace_back("reload.fontsdfs", []() { g_Renderer->LoadFonts(true); });
		m_ConsoleCommands.emplace_back("reload.skybox", []() { g_Renderer->ReloadSkybox(true); });
	}

	AudioSourceID FlexEngine::GetAudioSourceID(SoundEffect effect)
	{
		assert((i32)effect >= 0);
		assert((i32)effect < (i32)SoundEffect::LAST_ELEMENT);

		return s_AudioSourceIDs[(i32)effect];
	}

	void FlexEngine::Destroy()
	{
		// TODO: Time engine destruction using non-glfw timer
		// TODO: Destroy things in opposite order of their creations

#if COMPILE_RENDERDOC_API
		FreeLibrary(m_RenderDocModule);
#endif

		g_InputManager->UnbindMouseButtonCallback(&m_MouseButtonCallback);
		g_InputManager->UnbindMouseMovedCallback(&m_MouseMovedCallback);
		g_InputManager->UnbindKeyEventCallback(&m_KeyEventCallback);
		g_InputManager->UnbindActionCallback(&m_ActionCallback);

		SaveCommonSettingsToDisk(false);
		g_Window->SaveToConfig();

		DeselectCurrentlySelectedObjects();

		if (m_TransformGizmo)
		{
			m_TransformGizmo->Destroy();
			delete m_TransformGizmo;
		}

		g_SceneManager->DestroyAllScenes();
		delete g_SceneManager;

		g_PhysicsManager->Destroy();
		delete g_PhysicsManager;

		g_CameraManager->Destroy();
		delete g_CameraManager;

		DestroyWindowAndRenderer();

		delete g_Monitor;

		MeshComponent::DestroyAllLoadedMeshes();

		AudioManager::Destroy();

		delete g_InputManager;

		// Reset console color to default
		Print("\n");
	}

	void FlexEngine::CreateWindowAndRenderer()
	{
		assert(g_Window == nullptr);
		assert(g_Renderer == nullptr);

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
		real desiredWindowSizeScreenPercetange = 0.85f;

		// What kind of monitor has different scales along each axis?
		assert(g_Monitor->contentScaleX == g_Monitor->contentScaleY);

		i32 newWindowSizeY = i32(g_Monitor->height * desiredWindowSizeScreenPercetange * g_Monitor->contentScaleY);
		i32 newWindowSizeX = i32(newWindowSizeY * desiredAspectRatio);

		i32 newWindowPosX = i32(newWindowSizeX * 0.1f);
		i32 newWindowPosY = i32(newWindowSizeY * 0.1f);

		g_Window->Create(glm::vec2i(newWindowSizeX, newWindowSizeY), glm::vec2i(newWindowPosX, newWindowPosY));


#if COMPILE_VULKAN
		if (m_RendererIndex == RendererID::VULKAN)
		{
			g_Renderer = new vk::VulkanRenderer();
		}
#endif
#if COMPILE_OPEN_GL
		if (m_RendererIndex == RendererID::GL)
		{
			g_Renderer = new gl::GLRenderer();
		}
#endif
		if (g_Renderer == nullptr)
		{
			PrintError("Failed to create a renderer!\n");
			return;
		}
	}

	void FlexEngine::InitializeWindowAndRenderer()
	{
		g_Window->SetUpdateWindowTitleFrequency(0.5f);
		g_Window->PostInitialize();

		g_Renderer->Initialize();
	}

	void FlexEngine::DestroyWindowAndRenderer()
	{
		if (g_Renderer)
		{
			g_Renderer->Destroy();
			delete g_Renderer;
		}

		if (g_Window)
		{
			g_Window->Destroy();
			delete g_Window;
		}
	}

	std::string FlexEngine::RenderIDToString(RendererID rendererID) const
	{
		switch (rendererID)
		{
		case RendererID::VULKAN: return "Vulkan";
		case RendererID::GL: return "OpenGL";
		case RendererID::_LAST_ELEMENT:  return "Invalid renderer ID";
		default:
			return "Unknown";
		}
	}

	void FlexEngine::PreSceneChange()
	{
		if (m_TransformGizmo)
		{
			m_TransformGizmo->Destroy();
			delete m_TransformGizmo;
		}

		DeselectCurrentlySelectedObjects();
	}

	void FlexEngine::OnSceneChanged()
	{
		SaveCommonSettingsToDisk(false);

		RenderObjectCreateInfo gizmoCreateInfo = {};
		gizmoCreateInfo.depthTestReadFunc = DepthTestFunc::GEQUAL;
		gizmoCreateInfo.bDepthWriteEnable = false;
		gizmoCreateInfo.bEditorObject = true;
		gizmoCreateInfo.cullFace = CullFace::BACK;

		m_TransformGizmo = new GameObject("Transform gizmo", GameObjectType::_NONE);

		// Scene explorer visibility doesn't need to be set because this object isn't a root object

		u32 gizmoRBFlags = ((u32)PhysicsFlag::TRIGGER) | ((u32)PhysicsFlag::UNSELECTABLE);
		i32 gizmoRBGroup = (u32)CollisionType::EDITOR_OBJECT;
		i32 gizmoRBMask = (i32)CollisionType::DEFAULT;

		// Translation gizmo
		{
			real cylinderRadius = 0.3f;
			real cylinderHeight = 1.8f;

			// X Axis
			GameObject* transformXAxis = new GameObject("Translation gizmo x axis", GameObjectType::_NONE);
			transformXAxis->AddTag(m_TranslationGizmoTag);
			MeshComponent* xAxisMesh = transformXAxis->SetMeshComponent(new MeshComponent(m_TransformGizmoMatXID, transformXAxis));

			btCylinderShape* xAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
			transformXAxis->SetCollisionShape(xAxisShape);

			RigidBody* gizmoXAxisRB = transformXAxis->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			gizmoXAxisRB->SetMass(0.0f);
			gizmoXAxisRB->SetKinematic(true);
			gizmoXAxisRB->SetPhysicsFlags(gizmoRBFlags);

			xAxisMesh->SetRequiredAttributesFromMaterialID(m_TransformGizmoMatXID);
			xAxisMesh->LoadFromFile(RESOURCE_LOCATION  "meshes/translation-gizmo-x.glb", nullptr, &gizmoCreateInfo);

			// Y Axis
			GameObject* transformYAxis = new GameObject("Translation gizmo y axis", GameObjectType::_NONE);
			transformYAxis->AddTag(m_TranslationGizmoTag);
			MeshComponent* yAxisMesh = transformYAxis->SetMeshComponent(new MeshComponent(m_TransformGizmoMatYID, transformYAxis));

			btCylinderShape* yAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
			transformYAxis->SetCollisionShape(yAxisShape);

			RigidBody* gizmoYAxisRB = transformYAxis->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			gizmoYAxisRB->SetMass(0.0f);
			gizmoYAxisRB->SetKinematic(true);
			gizmoYAxisRB->SetPhysicsFlags(gizmoRBFlags);

			yAxisMesh->SetRequiredAttributesFromMaterialID(m_TransformGizmoMatYID);
			yAxisMesh->LoadFromFile(RESOURCE_LOCATION  "meshes/translation-gizmo-y.glb", nullptr, &gizmoCreateInfo);

			// Z Axis
			GameObject* transformZAxis = new GameObject("Translation gizmo z axis", GameObjectType::_NONE);
			transformZAxis->AddTag(m_TranslationGizmoTag);

			MeshComponent* zAxisMesh = transformZAxis->SetMeshComponent(new MeshComponent(m_TransformGizmoMatZID, transformZAxis));

			btCylinderShape* zAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
			transformZAxis->SetCollisionShape(zAxisShape);

			RigidBody* gizmoZAxisRB = transformZAxis->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			gizmoZAxisRB->SetMass(0.0f);
			gizmoZAxisRB->SetKinematic(true);
			gizmoZAxisRB->SetPhysicsFlags(gizmoRBFlags);

			zAxisMesh->SetRequiredAttributesFromMaterialID(m_TransformGizmoMatZID);
			zAxisMesh->LoadFromFile(RESOURCE_LOCATION  "meshes/translation-gizmo-z.glb", nullptr, &gizmoCreateInfo);


			gizmoXAxisRB->SetLocalRotation(glm::quat(glm::vec3(0, 0, PI / 2.0f)));
			gizmoXAxisRB->SetLocalPosition(glm::vec3(-cylinderHeight, 0, 0));

			gizmoYAxisRB->SetLocalPosition(glm::vec3(0, cylinderHeight, 0));

			gizmoZAxisRB->SetLocalRotation(glm::quat(glm::vec3(PI / 2.0f, 0, 0)));
			gizmoZAxisRB->SetLocalPosition(glm::vec3(0, 0, cylinderHeight));


			m_TranslationGizmo = new GameObject("Translation gizmo", GameObjectType::_NONE);

			m_TranslationGizmo->AddChild(transformXAxis);
			m_TranslationGizmo->AddChild(transformYAxis);
			m_TranslationGizmo->AddChild(transformZAxis);

			m_TransformGizmo->AddChild(m_TranslationGizmo);
		}

		// Rotation gizmo
		{
			real cylinderRadius = 3.4f;
			real cylinderHeight = 0.2f;

			// X Axis
			GameObject* rotationXAxis = new GameObject("Rotation gizmo x axis", GameObjectType::_NONE);
			rotationXAxis->AddTag(m_RotationGizmoTag);
			MeshComponent* xAxisMesh = rotationXAxis->SetMeshComponent(new MeshComponent(m_TransformGizmoMatXID, rotationXAxis));

			btCylinderShape* xAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
			rotationXAxis->SetCollisionShape(xAxisShape);

			RigidBody* gizmoXAxisRB = rotationXAxis->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			gizmoXAxisRB->SetMass(0.0f);
			gizmoXAxisRB->SetKinematic(true);
			gizmoXAxisRB->SetPhysicsFlags(gizmoRBFlags);
			// TODO: Get this to work (-cylinderHeight / 2.0f?)
			gizmoXAxisRB->SetLocalPosition(glm::vec3(100.0f, 0.0f, 0.0f));

			xAxisMesh->SetRequiredAttributesFromMaterialID(m_TransformGizmoMatXID);
			xAxisMesh->LoadFromFile(RESOURCE_LOCATION  "meshes/rotation-gizmo-flat-x.glb", nullptr, &gizmoCreateInfo);

			// Y Axis
			GameObject* rotationYAxis = new GameObject("Rotation gizmo y axis", GameObjectType::_NONE);
			rotationYAxis->AddTag(m_RotationGizmoTag);
			MeshComponent* yAxisMesh = rotationYAxis->SetMeshComponent(new MeshComponent(m_TransformGizmoMatYID, rotationYAxis));

			btCylinderShape* yAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
			rotationYAxis->SetCollisionShape(yAxisShape);

			RigidBody* gizmoYAxisRB = rotationYAxis->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			gizmoYAxisRB->SetMass(0.0f);
			gizmoYAxisRB->SetKinematic(true);
			gizmoYAxisRB->SetPhysicsFlags(gizmoRBFlags);

			yAxisMesh->SetRequiredAttributesFromMaterialID(m_TransformGizmoMatYID);
			yAxisMesh->LoadFromFile(RESOURCE_LOCATION  "meshes/rotation-gizmo-flat-y.glb", nullptr, &gizmoCreateInfo);

			// Z Axis
			GameObject* rotationZAxis = new GameObject("Rotation gizmo z axis", GameObjectType::_NONE);
			rotationZAxis->AddTag(m_RotationGizmoTag);

			MeshComponent* zAxisMesh = rotationZAxis->SetMeshComponent(new MeshComponent(m_TransformGizmoMatZID, rotationZAxis));

			btCylinderShape* zAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
			rotationZAxis->SetCollisionShape(zAxisShape);

			RigidBody* gizmoZAxisRB = rotationZAxis->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			gizmoZAxisRB->SetMass(0.0f);
			gizmoZAxisRB->SetKinematic(true);
			gizmoZAxisRB->SetPhysicsFlags(gizmoRBFlags);

			zAxisMesh->SetRequiredAttributesFromMaterialID(m_TransformGizmoMatZID);
			zAxisMesh->LoadFromFile(RESOURCE_LOCATION  "meshes/rotation-gizmo-flat-z.glb", nullptr, &gizmoCreateInfo);

			gizmoXAxisRB->SetLocalRotation(glm::quat(glm::vec3(0, 0, PI / 2.0f)));
			gizmoXAxisRB->SetLocalPosition(glm::vec3(-cylinderHeight, 0, 0));

			gizmoYAxisRB->SetLocalPosition(glm::vec3(0, cylinderHeight, 0));

			gizmoZAxisRB->SetLocalRotation(glm::quat(glm::vec3(PI / 2.0f, 0, 0)));
			gizmoZAxisRB->SetLocalPosition(glm::vec3(0, 0, cylinderHeight));


			m_RotationGizmo = new GameObject("Rotation gizmo", GameObjectType::_NONE);

			m_RotationGizmo->AddChild(rotationXAxis);
			m_RotationGizmo->AddChild(rotationYAxis);
			m_RotationGizmo->AddChild(rotationZAxis);

			m_TransformGizmo->AddChild(m_RotationGizmo);

			m_RotationGizmo->SetVisible(false);
		}

		// Scale gizmo
		{
			real boxScale = 0.5f;
			real cylinderRadius = 0.3f;
			real cylinderHeight = 1.8f;

			// X Axis
			GameObject* scaleXAxis = new GameObject("Scale gizmo x axis", GameObjectType::_NONE);
			scaleXAxis->AddTag(m_ScaleGizmoTag);
			MeshComponent* xAxisMesh = scaleXAxis->SetMeshComponent(new MeshComponent(m_TransformGizmoMatXID, scaleXAxis));

			btCylinderShape* xAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
			scaleXAxis->SetCollisionShape(xAxisShape);

			RigidBody* gizmoXAxisRB = scaleXAxis->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			gizmoXAxisRB->SetMass(0.0f);
			gizmoXAxisRB->SetKinematic(true);
			gizmoXAxisRB->SetPhysicsFlags(gizmoRBFlags);

			xAxisMesh->SetRequiredAttributesFromMaterialID(m_TransformGizmoMatXID);
			xAxisMesh->LoadFromFile(RESOURCE_LOCATION  "meshes/scale-gizmo-x.glb", nullptr, &gizmoCreateInfo);

			// Y Axis
			GameObject* scaleYAxis = new GameObject("Scale gizmo y axis", GameObjectType::_NONE);
			scaleYAxis->AddTag(m_ScaleGizmoTag);
			MeshComponent* yAxisMesh = scaleYAxis->SetMeshComponent(new MeshComponent(m_TransformGizmoMatYID, scaleYAxis));

			btCylinderShape* yAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
			scaleYAxis->SetCollisionShape(yAxisShape);

			RigidBody* gizmoYAxisRB = scaleYAxis->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			gizmoYAxisRB->SetMass(0.0f);
			gizmoYAxisRB->SetKinematic(true);
			gizmoYAxisRB->SetPhysicsFlags(gizmoRBFlags);

			yAxisMesh->SetRequiredAttributesFromMaterialID(m_TransformGizmoMatYID);
			yAxisMesh->LoadFromFile(RESOURCE_LOCATION  "meshes/scale-gizmo-y.glb", nullptr, &gizmoCreateInfo);

			// Z Axis
			GameObject* scaleZAxis = new GameObject("Scale gizmo z axis", GameObjectType::_NONE);
			scaleZAxis->AddTag(m_ScaleGizmoTag);

			MeshComponent* zAxisMesh = scaleZAxis->SetMeshComponent(new MeshComponent(m_TransformGizmoMatZID, scaleZAxis));

			btCylinderShape* zAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
			scaleZAxis->SetCollisionShape(zAxisShape);

			RigidBody* gizmoZAxisRB = scaleZAxis->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			gizmoZAxisRB->SetMass(0.0f);
			gizmoZAxisRB->SetKinematic(true);
			gizmoZAxisRB->SetPhysicsFlags(gizmoRBFlags);

			zAxisMesh->SetRequiredAttributesFromMaterialID(m_TransformGizmoMatZID);
			zAxisMesh->LoadFromFile(RESOURCE_LOCATION  "meshes/scale-gizmo-z.glb", nullptr, &gizmoCreateInfo);

			// Center (all axes)
			GameObject* scaleAllAxes = new GameObject("Scale gizmo all axes", GameObjectType::_NONE);
			scaleAllAxes->AddTag(m_ScaleGizmoTag);

			MeshComponent* allAxesMesh = scaleAllAxes->SetMeshComponent(new MeshComponent(m_TransformGizmoMatAllID, scaleAllAxes));

			btBoxShape* allAxesShape = new btBoxShape(btVector3(boxScale, boxScale, boxScale));
			scaleAllAxes->SetCollisionShape(allAxesShape);

			RigidBody* gizmoAllAxesRB = scaleAllAxes->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			gizmoAllAxesRB->SetMass(0.0f);
			gizmoAllAxesRB->SetKinematic(true);
			gizmoAllAxesRB->SetPhysicsFlags(gizmoRBFlags);

			allAxesMesh->SetRequiredAttributesFromMaterialID(m_TransformGizmoMatAllID);
			allAxesMesh->LoadFromFile(RESOURCE_LOCATION  "meshes/scale-gizmo-all.glb", nullptr, &gizmoCreateInfo);


			gizmoXAxisRB->SetLocalRotation(glm::quat(glm::vec3(0, 0, PI / 2.0f)));
			gizmoXAxisRB->SetLocalPosition(glm::vec3(-cylinderHeight, 0, 0));

			gizmoYAxisRB->SetLocalPosition(glm::vec3(0, cylinderHeight, 0));

			gizmoZAxisRB->SetLocalRotation(glm::quat(glm::vec3(PI / 2.0f, 0, 0)));
			gizmoZAxisRB->SetLocalPosition(glm::vec3(0, 0, cylinderHeight));


			m_ScaleGizmo = new GameObject("Scale gizmo", GameObjectType::_NONE);

			m_ScaleGizmo->AddChild(scaleXAxis);
			m_ScaleGizmo->AddChild(scaleYAxis);
			m_ScaleGizmo->AddChild(scaleZAxis);
			m_ScaleGizmo->AddChild(scaleAllAxes);

			m_TransformGizmo->AddChild(m_ScaleGizmo);

			m_ScaleGizmo->SetVisible(false);
		}

		m_TransformGizmo->Initialize();

		m_TransformGizmo->PostInitialize();

		m_CurrentTransformGizmoState = TransformState::TRANSLATE;
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

			sec frameEndTime = now;
			sec dt = frameEndTime - frameStartTime;
			frameStartTime = frameEndTime;

			dt = glm::clamp(dt * m_SimulationSpeed, m_MinDT, m_MaxDT);

			g_DeltaTime = dt;
			g_SecElapsedSinceProgramStart = frameEndTime;

			Profiler::StartFrame();

			if (!m_bSimulationPaused)
			{
				for (i32 i = 1; i < (i32)m_FrameTimes.size(); ++i)
				{
					m_FrameTimes[i - 1] = m_FrameTimes[i];
				}
				m_FrameTimes[m_FrameTimes.size() - 1] = dt;
			}

			PROFILE_BEGIN("Update");
			g_Window->PollEvents();

			const glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
			if (frameBufferSize.x == 0 || frameBufferSize.y == 0)
			{
				g_InputManager->ClearAllInputs();
			}

#if COMPILE_RENDERDOC_API
			if (m_RenderDocAPI)
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

			if (m_RenderDocAPI && m_bRenderDocTriggerCaptureNextFrame)
				{
					m_bRenderDocTriggerCaptureNextFrame = false;
					m_bRenderDocCapturingFrame = true;
					m_RenderDocAPI->StartFrameCapture(NULL, NULL);
				}
			}
#endif

			// Call as early as possible in the frame
			// Starts new ImGui frame and clears debug draw lines
			g_Renderer->NewFrame();

			SecSinceLogSave += dt;
			if (SecSinceLogSave >= LogSaveRate)
			{
				SecSinceLogSave -= LogSaveRate;
				SaveLogBufferToFile();
			}

			if (m_bRenderImGui)
			{
				PROFILE_BEGIN("DrawImGuiObjects");
				DrawImGuiObjects();
				PROFILE_END("DrawImGuiObjects");
			}

			if (g_InputManager->IsMouseButtonDown(MouseButton::LEFT))
			{
				if (!m_bDraggingGizmo)
				{
					m_HoveringAxisIndex = -1;
				}
			}
			else
			{
				m_bDraggingGizmo = false;
				if (!g_CameraManager->CurrentCamera()->bIsGameplayCam &&
					!m_CurrentlySelectedObjects.empty())
				{
#if COMPILE_RENDERDOC_API
					if (m_RenderDocAPI &&
						!m_bRenderDocCapturingFrame)
#endif
					{
						HandleGizmoHover();
					}
				}
			}
			Profiler::Update();

			const bool bSimulateFrame = (!m_bSimulationPaused || m_bSimulateNextFrame);
			m_bSimulateNextFrame = false;

			g_CameraManager->Update();

			if (bSimulateFrame)
			{
				g_CameraManager->CurrentCamera()->Update();

				g_SceneManager->CurrentScene()->Update();
				Player* p0 = g_SceneManager->CurrentScene()->GetPlayer(0);
				if (p0)
				{
					glm::vec3 targetPos = p0->GetTransform()->GetWorldPosition() + p0->GetTransform()->GetForward() * -2.0f;
					m_SpringTimer += g_DeltaTime;
					real amplitude = 1.5f;
					real period = 5.0f;
					if (m_SpringTimer > period)
					{
						m_SpringTimer -= period;
					}
					targetPos.y += pow(sin(glm::clamp(m_SpringTimer - period / 2.0f, 0.0f, PI)), 40.0f) * amplitude;
					glm::vec3 targetVel = ToVec3(p0->GetRigidBody()->GetRigidBodyInternal()->getLinearVelocity());

					for (Spring<glm::vec3>& spring : m_TestSprings)
					{
						spring.SetTargetPos(targetPos);
						spring.SetTargetVel(targetVel);

						targetPos = spring.pos;
						targetVel = spring.vel;
					}
				}
			}

			btIDebugDraw* debugDrawer = g_Renderer->GetDebugDrawer();
			for (i32 i = 0; i < (i32)m_TestSprings.size(); ++i)
			{
				m_TestSprings[i].Tick(g_DeltaTime);
				real t = (real)i / (real)m_TestSprings.size();
				debugDrawer->drawSphere(ToBtVec3(m_TestSprings[i].pos), (1.0f - t + 0.1f) * 0.5f, btVector3(0.5f - 0.3f * t, 0.8f - 0.4f * t, 0.6f - 0.2f * t));
			}

			if (!m_CurrentlySelectedObjects.empty())
			{
				m_TransformGizmo->SetVisible(true);
				UpdateGizmoVisibility();
				m_TransformGizmo->GetTransform()->SetWorldPosition(m_SelectedObjectsCenterPos);
				m_TransformGizmo->GetTransform()->SetWorldRotation(m_SelectedObjectRotation);
			}
			else
			{
				m_TransformGizmo->SetVisible(false);
			}

			g_Window->Update();

			if (bSimulateFrame)
			{
				g_SceneManager->CurrentScene()->LateUpdate();
			}

			g_Renderer->Update();

			// TODO: Consolidate functions?
			g_InputManager->Update();
			g_InputManager->PostUpdate();
			PROFILE_END("Update");

			PROFILE_BEGIN("Render");
			g_Renderer->Draw();
			PROFILE_END("Render");

#if COMPILE_RENDERDOC_API
			if (m_RenderDocAPI && m_bRenderDocCapturingFrame)
			{
				m_bRenderDocCapturingFrame = false;
				m_RenderDocAPI->EndFrameCapture(NULL, NULL);
				u32 bufferSize;
				u32 captureIndex = 0;
				m_RenderDocAPI->GetCapture(captureIndex, nullptr, &bufferSize, nullptr);
				std::string captureFilePath(bufferSize, '\0');
				u32 result = m_RenderDocAPI->GetCapture(captureIndex, (char*)captureFilePath.data(), &bufferSize, nullptr);
				if (result == 0)
				{
					PrintWarn("Failed to retrieve capture with index %d\n", captureIndex);
				}
				else
				{
					g_Renderer->AddEditorString("Captured RenderDoc frame");
					std::string captureFileName = captureFilePath;
					StripLeadingDirectories(captureFileName);
					Print("Captured RenderDoc frame to %s\n", captureFileName.c_str());
					if (m_RenderDocUIPID == -1)
					{
						m_RenderDocUIPID = m_RenderDocAPI->LaunchReplayUI(true, captureFilePath.c_str());
					}
				}
			}
#endif

			if (!m_bRenderImGui)
			{
				// Prevent mouse from being ignored while hovering over invisible ImGui elements
				ImGui::GetIO().WantCaptureMouse = false;
				ImGui::GetIO().WantCaptureKeyboard = false;
				ImGui::GetIO().WantSetMousePos = false;
			}

			m_SecondsSinceLastCommonSettingsFileSave += g_DeltaTime;
			if (m_SecondsSinceLastCommonSettingsFileSave > m_SecondsBetweenCommonSettingsFileSave)
			{
				m_SecondsSinceLastCommonSettingsFileSave = 0.0f;
				SaveCommonSettingsToDisk(false);
				g_Window->SaveToConfig();
			}

			const bool bProfileFrame = (g_Renderer->GetFramesRenderedCount() > 3);
			if (bProfileFrame)
			{
				Profiler::EndFrame(m_bUpdateProfilerFrame);
			}

			m_bUpdateProfilerFrame = false;

			if (m_bWriteProfilerResultsToFile)
			{
				m_bWriteProfilerResultsToFile = false;
				Profiler::PrintResultsToFile();
			}
		}
	}

	void FlexEngine::SetupImGuiStyles()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.MouseDrawCursor = false;

		//io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; // glfwSetCursor overruns buffer somewhere (currently Window::m_FrameBufferSize.y...)

		std::string fontFilePath(RESOURCE_LOCATION u8"fonts/lucon.ttf");
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
		colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
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
		if (m_bDemoWindowShowing)
		{
			ImGui::ShowDemoWindow(&m_bDemoWindowShowing);
		}

		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
#if COMPILE_RENDERDOC_API
				if (m_RenderDocAPI && ImGui::MenuItem("Capture RenderDoc frame"))
				{
					m_bRenderDocTriggerCaptureNextFrame = true;
				}
#endif

				if (ImGui::BeginMenu("Reload"))
				{
					if (ImGui::MenuItem("Scene", "R"))
					{
						g_SceneManager->ReloadCurrentScene();
					}

					if (ImGui::MenuItem("Scene (hard: reload all meshes)"))
					{
						MeshComponent::DestroyAllLoadedMeshes();
						g_SceneManager->ReloadCurrentScene();
					}

					if (ImGui::MenuItem("Shaders", "Ctrl+R"))
					{
						g_Renderer->ReloadShaders();
					}

					if (ImGui::MenuItem("Font textures (render SDFs)"))
					{
						g_Renderer->LoadFonts(true);
					}

					if (ImGui::MenuItem("Player position(s)"))
					{
						BaseScene* currentScene = g_SceneManager->CurrentScene();
						if (currentScene->GetPlayer(0))
						{
							currentScene->GetPlayer(0)->GetController()->ResetTransformAndVelocities();
						}
						if (currentScene->GetPlayer(1))
						{
							currentScene->GetPlayer(1)->GetController()->ResetTransformAndVelocities();
						}
					}

					if (ImGui::MenuItem("Skybox (randomize)"))
					{
						g_Renderer->ReloadSkybox(true);
					}

					ImGui::EndMenu();
				}

				if (ImGui::MenuItem("Capture reflection probe"))
				{
					g_Renderer->RecaptureReflectionProbe();
				}

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Edit"))
			{
				BaseScene* scene = g_SceneManager->CurrentScene();
				Player* player = scene->GetPlayer(0);
				if (player != nullptr && ImGui::BeginMenu("Add to inventory"))
				{
					if (ImGui::MenuItem("Cart"))
					{
						CartManager* cartManager = scene->GetCartManager();
						CartID cartID = cartManager->CreateCart(scene->GetUniqueObjectName("Cart_", 2));
						Cart* cart = cartManager->GetCart(cartID);
						player->AddToInventory(cart);
					}

					if (ImGui::MenuItem("Engine cart"))
					{
						CartManager* cartManager = scene->GetCartManager();
						CartID cartID = cartManager->CreateEngineCart(scene->GetUniqueObjectName("EngineCart_", 2));
						EngineCart* engineCart = static_cast<EngineCart*>(cartManager->GetCart(cartID));
						player->AddToInventory(engineCart);
					}

					if (ImGui::MenuItem("Mobile liquid box"))
					{
						MobileLiquidBox* box = new MobileLiquidBox();
						scene->AddObjectAtEndOFFrame(box);
						player->AddToInventory(box);
					}

					ImGui::EndMenu();
				}

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Window"))
			{
				ImGui::MenuItem("Main Window", NULL, &m_bMainWindowShowing);
				ImGui::MenuItem("Asset Browser", NULL, &m_bAssetBrowserShowing);
				ImGui::MenuItem("Demo Window", NULL, &m_bDemoWindowShowing);
				ImGui::MenuItem("Key Mapper", NULL, &m_bInputMapperShowing);
				ImGui::MenuItem("Uniform Buffers", NULL, &g_Renderer->bUniformBufferWindowShowing);
				ImGui::MenuItem("Font Editor", NULL, &g_Renderer->bFontWindowShowing);

				ImGui::EndMenu();
			}

			ImGui::EndMainMenuBar();
		}

		static auto windowSizeCallbackLambda = [](ImGuiSizeCallbackData* data)
		{
			FlexEngine* engine = static_cast<FlexEngine*>(data->UserData);
			engine->m_ImGuiMainWindowWidth = data->DesiredSize.x;
			engine->m_ImGuiMainWindowWidth = glm::min(engine->m_ImGuiMainWindowWidthMax,
				glm::max(engine->m_ImGuiMainWindowWidth, engine->m_ImGuiMainWindowWidthMin));
		};

		bool bIsMainWindowCollapsed = true;

		glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
		m_ImGuiMainWindowWidthMax = frameBufferSize.x - 100.0f;
		if (m_bMainWindowShowing)
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
			if (ImGui::Begin(titleCharStr, &m_bMainWindowShowing, mainWindowFlags))
			{
				bIsMainWindowCollapsed = ImGui::IsWindowCollapsed();

				if (ImGui::TreeNode("Simulation"))
				{
					ImGui::Checkbox("Paused", &m_bSimulationPaused);

					if (ImGui::Button("Step (F10)"))
					{
						m_bSimulationPaused = true;
						m_bSimulateNextFrame = true;
					}

					if (ImGui::Button("Continue (F5)"))
					{
						m_bSimulationPaused = false;
						m_bSimulateNextFrame = true;
					}

					ImGui::SliderFloat("Speed", &m_SimulationSpeed, 0.0001f, 10.0f);

					ImGui::TreePop();
				}

				if (ImGui::TreeNode("Stats"))
				{
					static const std::string rendererNameStringStr = std::string("Current renderer: " + m_RendererName);
					static const char* renderNameStr = rendererNameStringStr.c_str();
					ImGui::TextUnformatted(renderNameStr);
					ImGui::Text("Number of available renderers: %d", m_RendererCount);
					ImGui::Text("Frames rendered: %d", g_Renderer->GetFramesRenderedCount());
					ImGui::Text("Elapsed time (unpaused): %.2fs", g_SecElapsedSinceProgramStart);
					ImGui::Text("Selected object count: %d", m_CurrentlySelectedObjects.size());
					ImGui::Text("Audio effects loaded: %d", s_AudioSourceIDs.size());

					ImVec2 p = ImGui::GetCursorScreenPos();
					real width = 300.0f;
					real height = 100.0f;
					real minMS = 0.0f;
					real maxMS = 0.1f;
					ImGui::PlotLines("", m_FrameTimes.data(), m_FrameTimes.size(), 0, 0, minMS, maxMS, ImVec2(width, height));
					real targetFrameRate = 60.0f;
					p.y += (1.0f - (1.0f / targetFrameRate) / (maxMS - minMS)) * height;
					ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + width, p.y), IM_COL32(128, 0, 0, 255), 1.0f);

					ImGui::TreePop();
				}

				if (ImGui::TreeNode("Misc settings"))
				{
					ImGui::Checkbox("Log to console", &g_bEnableLogToConsole);
					ImGui::Checkbox("Show profiler", &Profiler::s_bDisplayingFrame);
					ImGui::Checkbox("chrome://tracing recording", &Profiler::s_bRecordingTrace);

					if (ImGui::Button("Display latest frame"))
					{
						m_bUpdateProfilerFrame = true;
						Profiler::s_bDisplayingFrame = true;
					}

					ImGui::TreePop();
				}

				g_Renderer->DrawImGuiSettings();
				g_Window->DrawImGuiObjects();
				g_CameraManager->DrawImGuiObjects();
				g_SceneManager->DrawImGuiObjects();
				AudioManager::DrawImGuiObjects();

				if (ImGui::RadioButton("Translate", GetTransformState() == TransformState::TRANSLATE))
				{
					SetTransformState(TransformState::TRANSLATE);
				}
				ImGui::SameLine();
				if (ImGui::RadioButton("Rotate", GetTransformState() == TransformState::ROTATE))
				{
					SetTransformState(TransformState::ROTATE);
				}
				ImGui::SameLine();
				if (ImGui::RadioButton("Scale", GetTransformState() == TransformState::SCALE))
				{
					SetTransformState(TransformState::SCALE);
				}

				g_Renderer->DrawImGuiRenderObjects();

				ImGui::Spacing();
				ImGui::Spacing();
				ImGui::Spacing();

				ImGui::Text("Debugging");

				g_SceneManager->CurrentScene()->GetTrackManager()->DrawImGuiObjects();
				g_SceneManager->CurrentScene()->GetCartManager()->DrawImGuiObjects();
				g_CameraManager->CurrentCamera()->DrawImGuiObjects();
				g_Renderer->DrawImGuiMisc();

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

		g_Renderer->DrawImGuiWindows();

		if (m_bAssetBrowserShowing)
		{
			g_Renderer->DrawAssetBrowserImGui(&m_bAssetBrowserShowing);
		}

		if (m_bInputMapperShowing)
		{
			g_InputManager->DrawImGuiKeyMapper(&m_bInputMapperShowing);
		}

		if (m_bShowingConsole)
		{
			const real consoleWindowWidth = 350.0f;
			const real consoleWindowHeight = 25.0f;
			const real consoleWindowX = (m_bMainWindowShowing && !bIsMainWindowCollapsed) ? m_ImGuiMainWindowWidth : 0.0f;
			const real consoleWindowY = frameBufferSize.y - consoleWindowHeight;
			ImGui::SetNextWindowPos(ImVec2(consoleWindowX, consoleWindowY), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(consoleWindowWidth, consoleWindowHeight));
			if (ImGui::Begin("Console", &m_bShowingConsole,
				ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove))
			{
				if (m_bShouldFocusKeyboardOnConsole)
				{
					m_bShouldFocusKeyboardOnConsole = false;
					ImGui::SetKeyboardFocusHere();
				}
				if (ImGui::InputTextEx("", m_CmdLineStrBuf, MAX_CHARS_CMD_LINE_STR, ImVec2(consoleWindowWidth - 16.0f, consoleWindowHeight - 8.0f),
					ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_CallbackCharFilter|ImGuiInputTextFlags_CallbackCompletion|ImGuiInputTextFlags_CallbackHistory,
					[](ImGuiInputTextCallbackData *data) { return g_EngineInstance->ImGuiConsoleInputCallback(data); }))
				{
					ToLower(m_CmdLineStrBuf);
					for (const ConsoleCommand& cmd : m_ConsoleCommands)
					{
						if (strcmp(m_CmdLineStrBuf, cmd.name.c_str()) == 0)
						{
							cmd.fun();
							if (m_PreviousCmdLineEntries.empty() ||
								strcmp((m_PreviousCmdLineEntries.end() - 1)->c_str(), m_CmdLineStrBuf) != 0)
							{
								m_PreviousCmdLineEntries.push_back(m_CmdLineStrBuf);
							}
							m_PreviousCmdLineIndex = -1;
							memset(m_CmdLineStrBuf, 0, MAX_CHARS_CMD_LINE_STR);
						}
					}
				}
			}
			ImGui::End();
		}
	}

	i32 FlexEngine::ImGuiConsoleInputCallback(ImGuiInputTextCallbackData *data)
	{
		const i32 cmdHistCount = (i32)m_PreviousCmdLineEntries.size();

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
		else if (data->EventKey == ImGuiKey_Tab)
		{
			// TODO:
		}

		if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
		{
			if (data->EventChar == L'`' ||
				data->EventChar == L'~')
			{
				m_bShowingConsole = false;
				return 1;
			}
		}
		return 0;
	}

	void FlexEngine::Stop()
	{
		m_bRunning = false;
	}

	std::vector<GameObject*> FlexEngine::GetSelectedObjects()
	{
		return m_CurrentlySelectedObjects;
	}

	void FlexEngine::SetSelectedObject(GameObject* gameObject, bool bSelectChildren /* = true */)
	{
		DeselectCurrentlySelectedObjects();

		if (gameObject != nullptr)
		{
			if (bSelectChildren)
			{
				gameObject->AddSelfAndChildrenToVec(m_CurrentlySelectedObjects);
			}
			else
			{
				m_CurrentlySelectedObjects.push_back(gameObject);
			}
		}

		CalculateSelectedObjectsCenter();
	}

	void FlexEngine::ToggleSelectedObject(GameObject* gameObject)
	{
		auto iter = Find(m_CurrentlySelectedObjects, gameObject);
		if (iter == m_CurrentlySelectedObjects.end())
		{
			gameObject->AddSelfAndChildrenToVec(m_CurrentlySelectedObjects);
		}
		else
		{
			m_CurrentlySelectedObjects.erase(iter);

			if (m_CurrentlySelectedObjects.empty())
			{
				DeselectCurrentlySelectedObjects();
			}
		}

		CalculateSelectedObjectsCenter();
	}

	void FlexEngine::AddSelectedObject(GameObject* gameObject)
	{
		auto iter = Find(m_CurrentlySelectedObjects, gameObject);
		if (iter == m_CurrentlySelectedObjects.end())
		{
			gameObject->AddSelfAndChildrenToVec(m_CurrentlySelectedObjects);

			CalculateSelectedObjectsCenter();
		}
	}

	void FlexEngine::DeselectObject(GameObject* gameObject)
	{
		for (GameObject* selectedObj : m_CurrentlySelectedObjects)
		{
			if (selectedObj == gameObject)
			{
				gameObject->RemoveSelfAndChildrenToVec(m_CurrentlySelectedObjects);
				CalculateSelectedObjectsCenter();
				return;
			}
		}

		PrintWarn("Attempted to deselect object which wasn't selected!\n");
	}

	bool FlexEngine::IsObjectSelected(GameObject* gameObject)
	{
		bool bSelected = (Find(m_CurrentlySelectedObjects, gameObject) != m_CurrentlySelectedObjects.end());
		return bSelected;
	}

	glm::vec3 FlexEngine::GetSelectedObjectsCenter()
	{
		return m_SelectedObjectsCenterPos;
	}

	void FlexEngine::DeselectCurrentlySelectedObjects()
	{
		m_CurrentlySelectedObjects.clear();
		m_SelectedObjectsCenterPos = VEC3_ZERO;
		m_SelectedObjectDragStartPos = VEC3_ZERO;
		m_DraggingGizmoScaleLast = VEC3_ZERO;
		m_DraggingGizmoOffset = -1.0f;
		m_DraggingAxisIndex = -1;
		m_bDraggingGizmo = false;
	}

	bool FlexEngine::LoadCommonSettingsFromDisk()
	{
		if (m_CommonSettingsAbsFilePath.empty())
		{
			PrintError("Failed to read common settings to disk: file path is not set!\n");
			return false;
		}

		if (FileExists(m_CommonSettingsAbsFilePath))
		{
			Print("Loading common settings from %s\n", m_CommonSettingsFileName.c_str());

			JSONObject rootObject = {};

			if (JSONParser::Parse(m_CommonSettingsAbsFilePath, rootObject))
			{
				std::string lastOpenedSceneName = rootObject.GetString("last opened scene");
				if (!lastOpenedSceneName.empty())
				{
					// Don't print errors if not found, file might have been deleted since last session
					if (!g_SceneManager->SetCurrentScene(lastOpenedSceneName, false))
					{
						g_SceneManager->SetNextSceneActive();
						g_SceneManager->InitializeCurrentScene();
						g_SceneManager->PostInitializeCurrentScene();
					}
				}

				bool bRenderImGui;
				if (rootObject.SetBoolChecked("render imgui", bRenderImGui))
				{
					m_bRenderImGui = bRenderImGui;
				}

				std::string cameraName;
				if (rootObject.SetStringChecked("last camera type", cameraName))
				{
					if (cameraName.compare("terminal") == 0)
					{
						// Ensure there's a camera to pop back to after exiting the terminal
						g_CameraManager->SetCameraByName("first-person", false);
					}
					g_CameraManager->PushCameraByName(cameraName, true);
				}

				JSONObject cameraTransform;
				if (rootObject.SetObjectChecked("camera transform", cameraTransform))
				{
					BaseCamera* cam = g_CameraManager->CurrentCamera();
					glm::vec3 camPos = ParseVec3(cameraTransform.GetString("position"));
					if (IsNanOrInf(camPos))
					{
						PrintError("Camera pos was saved out as nan or inf, resetting to 0\n");
						camPos = VEC3_ZERO;
					}
					cam->SetPosition(camPos);

					real camPitch = cameraTransform.GetFloat("pitch");
					if (IsNanOrInf(camPitch))
					{
						PrintError("Camera pitch was saved out as nan or inf, resetting to 0\n");
						camPitch = 0.0f;
					}
					cam->SetPitch(camPitch);

					real camYaw = cameraTransform.GetFloat("yaw");
					if (IsNanOrInf(camYaw))
					{
						PrintError("Camera yaw was saved out as nan or inf, resetting to 0\n");
						camYaw = 0.0f;
					}
					cam->SetYaw(camYaw);
				}

				real masterGain;
				if (rootObject.SetFloatChecked("master gain", masterGain))
				{
					AudioManager::SetMasterGain(masterGain);
				}

				return true;
			}
			else
			{
				PrintError("Failed to parse common settings config file\n");
				return false;
			}
		}

		return false;
	}

	void FlexEngine::SaveCommonSettingsToDisk(bool bAddEditorStr)
	{
		if (m_CommonSettingsAbsFilePath.empty())
		{
			PrintError("Failed to save common settings to disk: file path is not set!\n");
			return;
		}

		JSONObject rootObject{};

		std::string lastOpenedSceneName = g_SceneManager->CurrentScene()->GetFileName();
		rootObject.fields.emplace_back("last opened scene", JSONValue(lastOpenedSceneName));

		BaseCamera* cam = g_CameraManager->CurrentCamera();
		rootObject.fields.emplace_back("render imgui", JSONValue(m_bRenderImGui));
		rootObject.fields.emplace_back("last camera type", JSONValue(cam->GetName().c_str()));
		std::string posStr = Vec3ToString(cam->GetPosition(), 3);
		real pitch = cam->GetPitch();
		real yaw = cam->GetYaw();
		JSONObject cameraTransform = {};
		cameraTransform.fields.emplace_back("position", JSONValue(posStr));
		cameraTransform.fields.emplace_back("pitch", JSONValue(pitch));
		cameraTransform.fields.emplace_back("yaw", JSONValue(yaw));
		rootObject.fields.emplace_back("camera transform", JSONValue(cameraTransform));

		real masterGain = AudioManager::GetMasterGain();
		rootObject.fields.emplace_back("master gain", JSONValue(masterGain));

		std::string fileContents = rootObject.Print(0);

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

	glm::vec3 FlexEngine::CalculateRayPlaneIntersectionAlongAxis(const glm::vec3& axis,
		const glm::vec3& rayOrigin,
		const glm::vec3& rayEnd,
		const glm::vec3& planeOrigin,
		const glm::vec3& planeNorm)
	{
		glm::vec3 rayDir = glm::normalize(rayEnd - rayOrigin);
		glm::vec3 cameraForward = g_CameraManager->CurrentCamera()->GetForward();
		glm::vec3 planeN = planeNorm;
		if (glm::dot(planeN, cameraForward) > 0.0f)
		{
			planeN = -planeN;
		}
		real intersectionDistance;
		if (glm::intersectRayPlane(rayOrigin, rayDir, planeOrigin, planeN, intersectionDistance))
		{
			glm::vec3 intersectionPoint = rayOrigin + rayDir * intersectionDistance;
			if (m_DraggingGizmoOffset == -1.0f)
			{
				m_DraggingGizmoOffset = glm::dot(intersectionPoint - m_SelectedObjectDragStartPos, axis);
			}

			glm::vec3 constrainedPoint = planeOrigin + (glm::dot(intersectionPoint - planeOrigin, axis) - m_DraggingGizmoOffset) * axis;
			return constrainedPoint;
		}

		return VEC3_ZERO;
	}

	glm::quat FlexEngine::CalculateDeltaRotationFromGizmoDrag(
		const glm::vec3& axis,
		const glm::vec3& rayOrigin,
		const glm::vec3& rayEnd,
		const glm::quat& pRot)
	{
		glm::vec3 intersectionPoint(0.0f);

		Transform* gizmoTransform = m_TransformGizmo->GetTransform();
		glm::vec3 rayDir = glm::normalize(rayEnd - rayOrigin);
		glm::vec3 planeOrigin = gizmoTransform->GetWorldPosition();
		glm::vec3 cameraForward = g_CameraManager->CurrentCamera()->GetForward();
		glm::vec3 planeN = m_PlaneN;
		bool bFlip = glm::dot(planeN, cameraForward) > 0.0f;
		if (bFlip)
		{
			planeN = -planeN;
		}
		real intersectionDistance;
		if (glm::intersectRayPlane(rayOrigin, rayDir, planeOrigin, planeN, intersectionDistance))
		{
			intersectionPoint = rayOrigin + rayDir * intersectionDistance;
			if (m_DraggingGizmoOffset == -1.0f)
			{
				m_DraggingGizmoOffset = glm::dot(intersectionPoint - m_SelectedObjectDragStartPos, m_AxisProjectedOnto);
			}

			if (m_bFirstFrameDraggingRotationGizmo)
			{
				m_StartPointOnPlane = intersectionPoint;
			}
		}


		glm::vec3 v1 = glm::normalize(m_StartPointOnPlane - planeOrigin);
		glm::vec3 v2L = intersectionPoint - planeOrigin;
		glm::vec3 v2 = glm::normalize(v2L);

		glm::vec3 vecPerp = glm::cross(m_AxisOfRotation, v1);

		real v1ov2 = glm::dot(v1, v2);
		bool dotPos = (glm::dot(v2, vecPerp) > 0.0f);
		bool dot2Pos = (v1ov2 > 0.0f);

		if (dotPos && !m_bLastDotPos)
		{
			if (dot2Pos)
			{
				m_RotationGizmoWrapCount++;
			}
			else
			{
				m_RotationGizmoWrapCount--;
			}
		}
		else if (!dotPos && m_bLastDotPos)
		{
			if (dot2Pos)
			{
				m_RotationGizmoWrapCount--;
			}
			else
			{
				m_RotationGizmoWrapCount++;
			}
		}

		m_bLastDotPos = dotPos;

		real angleRaw = acos(v1ov2);
		real angle = (m_RotationGizmoWrapCount % 2 == 0 ? angleRaw : -angleRaw);

		m_pV1oV2 = v1ov2;

		if (m_bFirstFrameDraggingRotationGizmo)
		{
			m_bFirstFrameDraggingRotationGizmo = false;
			m_LastAngle = angle;
			m_RotationGizmoWrapCount = 0;
		}

		btIDebugDraw* debugDrawer = g_Renderer->GetDebugDrawer();
		debugDrawer->drawLine(
			ToBtVec3(planeOrigin),
			ToBtVec3(planeOrigin + axis * 5.0f),
			btVector3(1.0f, 1.0f, 0.0f));

		debugDrawer->drawLine(
			ToBtVec3(planeOrigin),
			ToBtVec3(planeOrigin + v1),
			btVector3(1.0f, 0.0f, 0.0f));

		debugDrawer->drawArc(
			ToBtVec3(planeOrigin),
			ToBtVec3(m_PlaneN),
			ToBtVec3(v1),
			1.0f,
			1.0f,
			(m_RotationGizmoWrapCount * PI) - angle,
			0.0f,
			btVector3(0.1f, 0.1f, 0.15f),
			true);

		debugDrawer->drawLine(
			ToBtVec3(planeOrigin + v2L),
			ToBtVec3(planeOrigin),
			btVector3(1.0f, 1.0f, 1.0f));

		debugDrawer->drawLine(
			ToBtVec3(planeOrigin),
			ToBtVec3(planeOrigin + vecPerp * 3.0f),
			btVector3(0.5f, 1.0f, 1.0f));

		real dAngle = m_LastAngle - angle;
		glm::quat result(VEC3_ZERO);
		if (!IsNanOrInf(dAngle))
		{
			glm::quat newRot = glm::rotate(pRot, dAngle, m_AxisOfRotation);
			result = newRot - pRot;
		}

		m_LastAngle = angle;

		return result;
	}

	void FlexEngine::UpdateGizmoVisibility()
	{
		if (m_TransformGizmo->IsVisible())
		{
			m_TranslationGizmo->SetVisible(m_CurrentTransformGizmoState == TransformState::TRANSLATE);
			m_RotationGizmo->SetVisible(m_CurrentTransformGizmoState == TransformState::ROTATE);
			m_ScaleGizmo->SetVisible(m_CurrentTransformGizmoState == TransformState::SCALE);
		}
	}

	void FlexEngine::SetTransformState(TransformState state)
	{
		if (state != m_CurrentTransformGizmoState)
		{
			m_CurrentTransformGizmoState = state;

			UpdateGizmoVisibility();
		}
	}

	bool FlexEngine::GetWantRenameActiveElement() const
	{
		return m_bWantRenameActiveElement;
	}

	void FlexEngine::ClearWantRenameActiveElement()
	{
		m_bWantRenameActiveElement = false;
	}

	TransformState FlexEngine::GetTransformState() const
	{
		return m_CurrentTransformGizmoState;
	}

	void FlexEngine::CalculateSelectedObjectsCenter()
	{
		if (m_CurrentlySelectedObjects.empty())
		{
			return;
		}

		glm::vec3 avgPos(0.0f);
		for (GameObject* gameObject : m_CurrentlySelectedObjects)
		{
			avgPos += gameObject->GetTransform()->GetWorldPosition();
		}
		m_SelectedObjectsCenterPos = m_SelectedObjectDragStartPos;
		m_SelectedObjectRotation = m_CurrentlySelectedObjects[m_CurrentlySelectedObjects.size() - 1]->GetTransform()->GetWorldRotation();

		m_SelectedObjectsCenterPos = (avgPos / (real)m_CurrentlySelectedObjects.size());
	}

	void FlexEngine::SelectAll()
	{
		for (GameObject* gameObject : g_SceneManager->CurrentScene()->GetAllObjects())
		{
			m_CurrentlySelectedObjects.push_back(gameObject);
		}
		CalculateSelectedObjectsCenter();
	}

	bool FlexEngine::IsDraggingGizmo() const
	{
		return m_bDraggingGizmo;
	}

	std::string FlexEngine::EngineVersionString()
	{
		return IntToString(EngineVersionMajor) + "." +
			IntToString(EngineVersionMinor) + "." +
			IntToString(EngineVersionPatch);
	}

	EventReply FlexEngine::OnMouseButtonEvent(MouseButton button, KeyAction action)
	{
		if (button == MouseButton::LEFT)
		{
			if (action == KeyAction::PRESS)
			{
				m_LMBDownPos = g_InputManager->GetMousePosition();

				if (m_HoveringAxisIndex != -1)
				{
					HandleGizmoClick();
					return EventReply::CONSUMED;
				}

				return EventReply::UNCONSUMED;
			}
			else if (action == KeyAction::RELEASE)
			{
				if (m_bDraggingGizmo)
				{
					if (m_CurrentlySelectedObjects.empty())
					{
						m_SelectedObjectDragStartPos = VEC3_NEG_ONE;
					}
					else
					{
						CalculateSelectedObjectsCenter();
						m_SelectedObjectDragStartPos = m_SelectedObjectsCenterPos;
					}

					m_bDraggingGizmo = false;
					m_DraggingAxisIndex = -1;
					m_DraggingGizmoScaleLast = VEC3_ZERO;
					m_DraggingGizmoOffset = -1.0f;
				}

				if (m_LMBDownPos != glm::vec2i(-1))
				{
					glm::vec2i releasePos = g_InputManager->GetMousePosition();
					real dPos = glm::length((glm::vec2)(releasePos - m_LMBDownPos));
					if (dPos < 1.0f)
					{
						if (HandleObjectClick())
						{
							m_LMBDownPos = glm::vec2i(-1);
							return EventReply::CONSUMED;
						}
					}

					m_LMBDownPos = glm::vec2i(-1);
				}
			}
		}

		return EventReply::UNCONSUMED;
	}

	EventReply FlexEngine::OnMouseMovedEvent(const glm::vec2& dMousePos)
	{
		UNREFERENCED_PARAMETER(dMousePos);

		if (m_bDraggingGizmo)
		{
			HandleGizmoMovement();
			return EventReply::CONSUMED;
		}
		return EventReply::UNCONSUMED;
	}

	EventReply FlexEngine::OnKeyEvent(KeyCode keyCode, KeyAction action, i32 modifiers)
	{
		const bool bControlDown = (modifiers & (i32)InputModifier::CONTROL) > 0;
		const bool bAltDown = (modifiers & (i32)InputModifier::ALT) > 0;

		if (action == KeyAction::PRESS)
		{
			// Disabled for now since we only support Open GL
#if 0
			if (keyCode == KeyCode::KEY_T)
			{
				g_InputManager->Update();
				g_InputManager->PostUpdate();
				g_InputManager->ClearAllInputs();
				CycleRenderer();
				return EventReply::CONSUMED;
			}
#endif

			if (keyCode == KeyCode::KEY_GRAVE_ACCENT)
			{
				m_bShowingConsole = !m_bShowingConsole;
				if (m_bShowingConsole)
				{
					m_bShouldFocusKeyboardOnConsole = true;
				}
			}

#if COMPILE_RENDERDOC_API
			if (m_RenderDocAPI &&
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

			if (keyCode == KeyCode::KEY_F10)
			{
				m_bSimulationPaused = true;
				m_bSimulateNextFrame = true;
				return EventReply::CONSUMED;
			}

			if (keyCode == KeyCode::KEY_G)
			{
				g_Renderer->ToggleRenderGrid();
				return EventReply::CONSUMED;
			}

			// TODO: Handle elsewhere to handle ignoring ImGuiIO::WantCaptureKeyboard?
			if (keyCode == KeyCode::KEY_F1)
			{
				m_bToggleRenderImGui = true;
				return EventReply::CONSUMED;
			}

			if (keyCode == KeyCode::KEY_DELETE)
			{
				if (!m_CurrentlySelectedObjects.empty())
				{
					g_SceneManager->CurrentScene()->DestroyObjectsAtEndOfFrame(m_CurrentlySelectedObjects);

					DeselectCurrentlySelectedObjects();
					return EventReply::CONSUMED;
				}
			}

			if (keyCode == KeyCode::KEY_R)
			{
				if (bControlDown)
				{
					g_Renderer->ReloadShaders();
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
				settings.DrawWireframe = !settings.DrawWireframe;
				return EventReply::CONSUMED;
			}

			if (keyCode == KeyCode::KEY_F11 ||
				(bAltDown && keyCode == KeyCode::KEY_ENTER))
			{
				g_Window->ToggleFullscreen();
				return EventReply::CONSUMED;
			}

			if (keyCode == KeyCode::KEY_F && !m_CurrentlySelectedObjects.empty())
			{
				// Focus on selected objects

				glm::vec3 minPos(FLT_MAX);
				glm::vec3 maxPos(-FLT_MAX);
				for (GameObject* gameObject : m_CurrentlySelectedObjects)
				{
					MeshComponent* mesh = gameObject->GetMeshComponent();
					if (mesh)
					{
						Transform* transform = gameObject->GetTransform();
						glm::vec3 min = transform->GetWorldTransform() * glm::vec4(mesh->m_MinPoint, 1.0f);
						glm::vec3 max = transform->GetWorldTransform() * glm::vec4(mesh->m_MaxPoint, 1.0f);
						minPos = glm::min(minPos, min);
						maxPos = glm::max(maxPos, max);
					}
				}

				if (minPos.x != FLT_MAX && maxPos.x != -FLT_MAX)
				{
					glm::vec3 sphereCenterWS = minPos + (maxPos - minPos) / 2.0f;
					real sphereRadius = glm::length(maxPos - minPos) / 2.0f;

					BaseCamera* cam = g_CameraManager->CurrentCamera();

					glm::vec3 currentOffset = cam->GetPosition() - sphereCenterWS;
					glm::vec3 newOffset = glm::normalize(currentOffset) * sphereRadius * 2.0f;

					cam->SetPosition(sphereCenterWS + newOffset);
					cam->LookAt(sphereCenterWS);
				}
				return EventReply::CONSUMED;
			}

			if (bControlDown && keyCode == KeyCode::KEY_A)
			{
				SelectAll();
				return EventReply::CONSUMED;
			}

			if (bControlDown && keyCode == KeyCode::KEY_S)
			{
				g_SceneManager->CurrentScene()->SerializeToFile(true);
				return EventReply::CONSUMED;
			}

			if (bControlDown && keyCode == KeyCode::KEY_D)
			{
				if (!m_CurrentlySelectedObjects.empty())
				{
					std::vector<GameObject*> newSelectedGameObjects;

					for (GameObject* gameObject : m_CurrentlySelectedObjects)
					{
						GameObject* duplicatedObject = gameObject->CopySelfAndAddToScene(nullptr, true);

						duplicatedObject->AddSelfAndChildrenToVec(newSelectedGameObjects);
					}

					DeselectCurrentlySelectedObjects();

					m_CurrentlySelectedObjects = newSelectedGameObjects;
					CalculateSelectedObjectsCenter();
				}
				return EventReply::CONSUMED;
			}

			if (keyCode == KeyCode::KEY_4)
			{
				AudioManager::PlaySource(s_AudioSourceIDs[(i32)SoundEffect::synthesized_01]);
				return EventReply::CONSUMED;
			}
			if (keyCode == KeyCode::KEY_5)
			{
				AudioManager::PlaySource(s_AudioSourceIDs[(i32)SoundEffect::synthesized_02]);
				return EventReply::CONSUMED;
			}
			if (keyCode == KeyCode::KEY_6)
			{
				AudioManager::PlaySource(s_AudioSourceIDs[(i32)SoundEffect::synthesized_03]);
				return EventReply::CONSUMED;
			}
			if (keyCode == KeyCode::KEY_7)
			{
				AudioManager::PlaySource(s_AudioSourceIDs[(i32)SoundEffect::synthesized_04]);
				return EventReply::CONSUMED;
			}
			if (keyCode == KeyCode::KEY_8)
			{
				AudioManager::PlaySource(s_AudioSourceIDs[(i32)SoundEffect::synthesized_05]);
				return EventReply::CONSUMED;
			}
			if (keyCode == KeyCode::KEY_9)
			{
				AudioManager::PlaySource(s_AudioSourceIDs[(i32)SoundEffect::synthesized_06]);
				return EventReply::CONSUMED;
			}
			if (keyCode == KeyCode::KEY_0)
			{
				AudioManager::PlaySource(s_AudioSourceIDs[(i32)SoundEffect::synthesized_07]);
				return EventReply::CONSUMED;
			}

			if (keyCode == KeyCode::KEY_K)
			{
				m_bWriteProfilerResultsToFile = true;
				return EventReply::CONSUMED;
			}
		}

		return EventReply::UNCONSUMED;
	}

	EventReply FlexEngine::OnActionEvent(Action action)
	{
		if (action == Action::EDITOR_RENAME_SELECTED)
		{
			m_bWantRenameActiveElement = !m_bWantRenameActiveElement;
			return EventReply::CONSUMED;
		}

		if (action == Action::DBG_ENTER_NEXT_SCENE)
		{
			g_SceneManager->SetNextSceneActive();
			g_SceneManager->InitializeCurrentScene();
			g_SceneManager->PostInitializeCurrentScene();
			return EventReply::CONSUMED;
		}
		else if (action == Action::DBG_ENTER_PREV_SCENE)
		{
			g_SceneManager->SetPreviousSceneActive();
			g_SceneManager->InitializeCurrentScene();
			g_SceneManager->PostInitializeCurrentScene();
			return EventReply::CONSUMED;
		}

		if (action == Action::EDITOR_SELECT_TRANSLATE_GIZMO)
		{
			SetTransformState(TransformState::TRANSLATE);
			return EventReply::CONSUMED;
		}
		if (action == Action::EDITOR_SELECT_ROTATE_GIZMO)
		{
			SetTransformState(TransformState::ROTATE);
			return EventReply::CONSUMED;
		}
		if (action == Action::EDITOR_SELECT_SCALE_GIZMO)
		{
			SetTransformState(TransformState::SCALE);
			return EventReply::CONSUMED;
		}

		if (action == Action::PAUSE)
		{
			if (m_CurrentlySelectedObjects.empty())
			{
				//m_bSimulationPaused = !m_bSimulationPaused;
			}
			else
			{
				DeselectCurrentlySelectedObjects();
			}
			return EventReply::CONSUMED;
		}

		return EventReply::UNCONSUMED;
	}

#if COMPILE_RENDERDOC_API
	void FlexEngine::SetupRenderDocAPI()
	{
		std::string dllDirPath;
		if (FileExists(m_RenderDocSettingsAbsFilePath))
		{
			JSONObject rootObject;
			if (JSONParser::Parse(m_RenderDocSettingsAbsFilePath, rootObject))
			{
				dllDirPath = rootObject.GetString("lib path");
			}
			else
			{
				PrintError("Failed to parse %s\n", m_RenderDocSettingsFileName.c_str());
			}
		}

		if (dllDirPath.empty())
		{
			PrintError("Requested setup of RenderDoc API, but was unable to find renderdoc settings file\n,"
				"please create one in the format: "
						"\"{ \"lib path\" : \"C:\\Path\\To\\RenderDocLibs\\\" }\"\n"
						"And save it at: \"%s\"\n", m_RenderDocSettingsAbsFilePath.c_str());
			return;
		}


		if (!EndsWith(dllDirPath, "\\"))
		{
			dllDirPath += "\\";
		}

		std::string dllPath = dllDirPath + "renderdoc.dll";

		if (FileExists(dllPath))
		{
			m_RenderDocModule = LoadLibraryA(dllPath.c_str());

			if (m_RenderDocModule == NULL)
			{
				PrintWarn("Found render doc dll but failed to load it. Is the bitness correct? (must be x86)\n");
				return;
			}

			pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(m_RenderDocModule, "RENDERDOC_GetAPI");
			int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_3_0, (void**)&m_RenderDocAPI);
			assert(ret == 1);

			i32 major = 0, minor = 0, patch = 0;
			m_RenderDocAPI->GetAPIVersion(&major, &minor, &patch);
			Print("RenderDoc API v%i.%i.%i connected, F9 to capture\n", major, minor, patch);

			if (m_RenderDocAutoCaptureFrameOffset != -1 &&
				m_RenderDocAutoCaptureFrameCount != -1)
			{
				Print("Auto capturing %i frame(s) starting at frame %i\n", m_RenderDocAutoCaptureFrameCount, m_RenderDocAutoCaptureFrameOffset);
			}

			std::string dateStr = GetDateString_YMDHMS();
			std::string captureFilePath = RelativePathToAbsolute(SAVED_LOCATION "RenderDocCaptures/FlexEngine_" + dateStr);
			m_RenderDocAPI->SetCaptureFilePathTemplate(captureFilePath.c_str());

			m_RenderDocAPI->MaskOverlayBits(eRENDERDOC_Overlay_None, 0);
			//m_RenderDocAPI->SetCaptureKeys(nullptr, 0);
			//m_RenderDocAPI->SetFocusToggleKeys(nullptr, 0);

			m_RenderDocAPI->SetCaptureOptionU32(eRENDERDOC_Option_DebugOutputMute, 1);
		}
		else
		{
			PrintError("Unable to find renderdoc dll at %s\n", dllPath.c_str());
		}
	}
#endif

	bool FlexEngine::HandleGizmoHover()
	{
		if (m_bDraggingGizmo == true)
		{
			PrintError("Called HandleGizmoHover when m_bDraggingGizmo == true\n");
			return false;
		}
		if (m_DraggingAxisIndex != -1)
		{
			PrintError("Called HandleGizmoHover when m_DraggingAxisIndex == %i\n", m_DraggingAxisIndex);
			return false;
		}

		if (g_InputManager->GetMousePosition().x == -1.0f)
		{
			return false;
		}

		PhysicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld();

		btVector3 rayStart, rayEnd;
		GenerateRayAtMousePos(rayStart, rayEnd);

		std::string gizmoTag = (
			m_CurrentTransformGizmoState == TransformState::TRANSLATE ? m_TranslationGizmoTag :
			(m_CurrentTransformGizmoState == TransformState::ROTATE ? m_RotationGizmoTag :
				m_ScaleGizmoTag));
		GameObject* pickedTransformGizmo = physicsWorld->PickTaggedBody(rayStart, rayEnd, gizmoTag);

		Material& xMat = g_Renderer->GetMaterial(m_TransformGizmoMatXID);
		Material& yMat = g_Renderer->GetMaterial(m_TransformGizmoMatYID);
		Material& zMat = g_Renderer->GetMaterial(m_TransformGizmoMatZID);
		Material& allMat = g_Renderer->GetMaterial(m_TransformGizmoMatAllID);
		glm::vec4 white(1.0f);
		if (m_DraggingAxisIndex != 0)
		{
			xMat.colorMultiplier = white;
		}
		if (m_DraggingAxisIndex != 1)
		{
			yMat.colorMultiplier = white;
		}
		if (m_DraggingAxisIndex != 2)
		{
			zMat.colorMultiplier = white;
		}
		if (m_DraggingAxisIndex != 3)
		{
			allMat.colorMultiplier = white;
		}

		static const real gizmoHoverMultiplier = 0.6f;
		static const glm::vec4 hoverColor(gizmoHoverMultiplier, gizmoHoverMultiplier, gizmoHoverMultiplier, 1.0f);

		if (pickedTransformGizmo)
		{
			switch (m_CurrentTransformGizmoState)
			{
			case TransformState::TRANSLATE:
			{
				std::vector<GameObject*> translationAxes = m_TranslationGizmo->GetChildren();

				if (pickedTransformGizmo == translationAxes[0]) // X Axis
				{
					m_HoveringAxisIndex = 0;
					xMat.colorMultiplier = hoverColor;
				}
				else if (pickedTransformGizmo == translationAxes[1]) // Y Axis
				{
					m_HoveringAxisIndex = 1;
					yMat.colorMultiplier = hoverColor;
				}
				else if (pickedTransformGizmo == translationAxes[2]) // Z Axis
				{
					m_HoveringAxisIndex = 2;
					zMat.colorMultiplier = hoverColor;
				}
			} break;
			case TransformState::ROTATE:
			{
				std::vector<GameObject*> rotationAxes = m_RotationGizmo->GetChildren();

				if (pickedTransformGizmo == rotationAxes[0]) // X Axis
				{
					m_HoveringAxisIndex = 0;
					xMat.colorMultiplier = hoverColor;
				}
				else if (pickedTransformGizmo == rotationAxes[1]) // Y Axis
				{
					m_HoveringAxisIndex = 1;
					yMat.colorMultiplier = hoverColor;
				}
				else if (pickedTransformGizmo == rotationAxes[2]) // Z Axis
				{
					m_HoveringAxisIndex = 2;
					zMat.colorMultiplier = hoverColor;
				}
			} break;
			case TransformState::SCALE:
			{
				std::vector<GameObject*> scaleAxes = m_ScaleGizmo->GetChildren();

				if (pickedTransformGizmo == scaleAxes[0]) // X Axis
				{
					m_HoveringAxisIndex = 0;
					xMat.colorMultiplier = hoverColor;
				}
				else if (pickedTransformGizmo == scaleAxes[1]) // Y Axis
				{
					m_HoveringAxisIndex = 1;
					yMat.colorMultiplier = hoverColor;
				}
				else if (pickedTransformGizmo == scaleAxes[2]) // Z Axis
				{
					m_HoveringAxisIndex = 2;
					zMat.colorMultiplier = hoverColor;
				}
				else if (pickedTransformGizmo == scaleAxes[3]) // All axes
				{
					m_HoveringAxisIndex = 3;
					allMat.colorMultiplier = hoverColor;
				}
			} break;
			default:
			{
				PrintError("Unhandled transform state in FlexEngine::HandleGizmoHover\n");
			} break;
			}
		}
		else
		{
			m_HoveringAxisIndex = -1;
		}

		// Fade out gizmo parts when facing head-on
		if (m_CurrentTransformGizmoState != TransformState::ROTATE)
		{
			Transform* gizmoTransform = m_TransformGizmo->GetTransform();
			glm::vec3 camForward = g_CameraManager->CurrentCamera()->GetForward();

			real camForDotR = glm::abs(glm::dot(camForward, gizmoTransform->GetRight()));
			real camForDotU = glm::abs(glm::dot(camForward, gizmoTransform->GetUp()));
			real camForDotF = glm::abs(glm::dot(camForward, gizmoTransform->GetForward()));
			real threshold = 0.98f;
			if (camForDotR >= threshold)
			{
				xMat.colorMultiplier.a = Lerp(1.0f, 0.0f, (camForDotR - threshold) / (1.0f - threshold));
			}
			if (camForDotU >= threshold)
			{
				yMat.colorMultiplier.a = Lerp(1.0f, 0.0f, (camForDotU - threshold) / (1.0f - threshold));
			}
			if (camForDotF >= threshold)
			{
				zMat.colorMultiplier.a = Lerp(1.0f, 0.0f, (camForDotF - threshold) / (1.0f - threshold));
			}
		}

		return m_HoveringAxisIndex != -1;
	}

	void FlexEngine::HandleGizmoClick()
	{
		assert(m_bDraggingGizmo == false);
		assert(m_HoveringAxisIndex != -1);

		m_DraggingAxisIndex = m_HoveringAxisIndex;
		m_bDraggingGizmo = true;
		m_SelectedObjectDragStartPos = m_TransformGizmo->GetTransform()->GetWorldPosition();

		real gizmoSelectedMultiplier = 0.4f;
		glm::vec4 selectedColor(gizmoSelectedMultiplier, gizmoSelectedMultiplier, gizmoSelectedMultiplier, 1.0f);

		Material& xMat = g_Renderer->GetMaterial(m_TransformGizmoMatXID);
		Material& yMat = g_Renderer->GetMaterial(m_TransformGizmoMatYID);
		Material& zMat = g_Renderer->GetMaterial(m_TransformGizmoMatZID);
		Material& allMat = g_Renderer->GetMaterial(m_TransformGizmoMatAllID);

		switch (m_CurrentTransformGizmoState)
		{
		case TransformState::TRANSLATE:
		{
			if (m_HoveringAxisIndex == 0) // X Axis
			{
				xMat.colorMultiplier = selectedColor;
			}
			else if (m_HoveringAxisIndex == 1) // Y Axis
			{
				yMat.colorMultiplier = selectedColor;
			}
			else if (m_HoveringAxisIndex == 2) // Z Axis
			{
				zMat.colorMultiplier = selectedColor;
			}
		} break;
		case TransformState::ROTATE:
		{
			if (m_HoveringAxisIndex == 0) // X Axis
			{
				xMat.colorMultiplier = selectedColor;
			}
			else if (m_HoveringAxisIndex == 1) // Y Axis
			{
				yMat.colorMultiplier = selectedColor;
			}
			else if (m_HoveringAxisIndex == 2) // Z Axis
			{
				zMat.colorMultiplier = selectedColor;
			}
		} break;
		case TransformState::SCALE:
		{
			if (m_HoveringAxisIndex == 0) // X Axis
			{
				xMat.colorMultiplier = selectedColor;
			}
			else if (m_HoveringAxisIndex == 1) // Y Axis
			{
				yMat.colorMultiplier = selectedColor;
			}
			else if (m_HoveringAxisIndex == 2) // Z Axis
			{
				zMat.colorMultiplier = selectedColor;
			}
			else if (m_HoveringAxisIndex == 3) // All axes
			{
				allMat.colorMultiplier = selectedColor;
			}
		} break;
		default:
		{
			PrintError("Unhandled transform state in FlexEngine::HandleGizmoClick\n");
		} break;
		}
	}

	void FlexEngine::HandleGizmoMovement()
	{
		assert(m_HoveringAxisIndex != -1);
		assert(m_DraggingAxisIndex != -1);
		assert(m_bDraggingGizmo);
		if (m_CurrentlySelectedObjects.empty())
		{
			DeselectCurrentlySelectedObjects();
			return;
		}

		Transform* gizmoTransform = m_TransformGizmo->GetTransform();
		const glm::vec3 gizmoUp = gizmoTransform->GetUp();
		const glm::vec3 gizmoRight = gizmoTransform->GetRight();
		const glm::vec3 gizmoForward = gizmoTransform->GetForward();

		btVector3 rayStart, rayEnd;
		GenerateRayAtMousePos(rayStart, rayEnd);

		glm::vec3 rayStartG = ToVec3(rayStart);
		glm::vec3 rayEndG = ToVec3(rayEnd);
		glm::vec3 camForward = g_CameraManager->CurrentCamera()->GetForward();
		glm::vec3 planeOrigin = gizmoTransform->GetWorldPosition();

		btIDebugDraw* debugDrawer = g_Renderer->GetDebugDrawer();
		debugDrawer->drawLine(
			ToBtVec3(gizmoTransform->GetWorldPosition()),
			ToBtVec3(gizmoTransform->GetWorldPosition() + gizmoRight * 6.0f),
			btVector3(1.0f, 0.0f, 0.0f));

		debugDrawer->drawLine(
			ToBtVec3(gizmoTransform->GetWorldPosition()),
			ToBtVec3(gizmoTransform->GetWorldPosition() + gizmoUp * 6.0f),
			btVector3(0.0f, 1.0f, 0.0f));

		debugDrawer->drawLine(
			ToBtVec3(gizmoTransform->GetWorldPosition()),
			ToBtVec3(gizmoTransform->GetWorldPosition() + gizmoForward * 6.0f),
			btVector3(0.0f, 0.0f, 1.0f));


		switch (m_CurrentTransformGizmoState)
		{
		case TransformState::TRANSLATE:
		{
			glm::vec3 dPos(0.0f);

			if (g_InputManager->DidMouseWrap())
			{
				m_DraggingGizmoOffset = -1.0f;
				Print("warp\n");
			}

			if (m_DraggingAxisIndex == 0) // X Axis
			{
				glm::vec3 axis = gizmoRight;
				glm::vec3 planeN = gizmoForward;
				if (glm::abs(glm::dot(planeN, camForward)) < 0.5f)
				{
					planeN = gizmoUp;
				}
				glm::vec3 intersectionPont = CalculateRayPlaneIntersectionAlongAxis(axis, rayStartG, rayEndG, planeOrigin, planeN);
				dPos = intersectionPont - planeOrigin;
			}
			else if (m_DraggingAxisIndex == 1) // Y Axis
			{
				glm::vec3 axis = gizmoUp;
				glm::vec3 planeN = gizmoRight;
				if (glm::abs(glm::dot(planeN, camForward)) < 0.5f)
				{
					planeN = gizmoForward;
				}
				glm::vec3 intersectionPont = CalculateRayPlaneIntersectionAlongAxis(axis, rayStartG, rayEndG, planeOrigin, planeN);
				dPos = intersectionPont - planeOrigin;
			}
			else if (m_DraggingAxisIndex == 2) // Z Axis
			{
				glm::vec3 axis = gizmoForward;
				glm::vec3 planeN = gizmoUp;
				if (glm::abs(glm::dot(planeN, camForward)) < 0.5f)
				{
					planeN = gizmoRight;
				}
				glm::vec3 intersectionPont = CalculateRayPlaneIntersectionAlongAxis(axis, rayStartG, rayEndG, planeOrigin, planeN);
				dPos = intersectionPont - planeOrigin;
			}

			Transform* selectedObjectTransform = m_CurrentlySelectedObjects[m_CurrentlySelectedObjects.size() - 1]->GetTransform();

			debugDrawer->drawLine(
				ToBtVec3(m_SelectedObjectDragStartPos),
				ToBtVec3(selectedObjectTransform->GetLocalPosition()),
				(m_DraggingAxisIndex == 0 ? btVector3(1.0f, 0.0f, 0.0f) : m_DraggingAxisIndex == 1 ? btVector3(0.0f, 1.0f, 0.0f) : btVector3(0.1f, 0.1f, 1.0f)));

			for (GameObject* gameObject : m_CurrentlySelectedObjects)
			{
				GameObject* parent = gameObject->GetParent();
				bool bObjectIsntChild = (parent == nullptr) || (Find(m_CurrentlySelectedObjects, parent) == m_CurrentlySelectedObjects.end());
				if (bObjectIsntChild)
				{
					gameObject->GetTransform()->Translate(dPos);
				}
			}
		} break;
		case TransformState::ROTATE:
		{
			glm::quat dRot(VEC3_ZERO);

			static glm::quat pGizmoRot = gizmoTransform->GetLocalRotation();
			Print("%.2f,%.2f,%.2f,%.2f\n", pGizmoRot.x, pGizmoRot.y, pGizmoRot.z, pGizmoRot.w);

			if (m_DraggingAxisIndex == 0) // X Axis
			{
				if (m_bFirstFrameDraggingRotationGizmo)
				{
					m_UnmodifiedAxisProjectedOnto = gizmoUp;
					m_AxisProjectedOnto = m_UnmodifiedAxisProjectedOnto;
					m_AxisOfRotation = gizmoRight;
					m_PlaneN = m_AxisOfRotation;
					if (glm::abs(glm::dot(m_AxisProjectedOnto, camForward)) > 0.5f)
					{
						m_AxisProjectedOnto = gizmoForward;
						m_PlaneN = gizmoUp;
					}
				}

				dRot = CalculateDeltaRotationFromGizmoDrag(m_AxisOfRotation, rayStartG, rayEndG, pGizmoRot);
			}
			else if (m_DraggingAxisIndex == 1) // Y Axis
			{
				if (m_bFirstFrameDraggingRotationGizmo)
				{
					m_UnmodifiedAxisProjectedOnto = gizmoRight;
					m_AxisProjectedOnto = m_UnmodifiedAxisProjectedOnto;
					m_AxisOfRotation = gizmoUp;
					m_PlaneN = m_AxisOfRotation;
					if (glm::abs(glm::dot(m_AxisProjectedOnto, camForward)) > 0.5f)
					{
						m_AxisProjectedOnto = gizmoForward;
						//m_PlaneN = gizmoUp;
					}
				}

				dRot = CalculateDeltaRotationFromGizmoDrag(m_AxisOfRotation, rayStartG, rayEndG, pGizmoRot);
			}
			else if (m_DraggingAxisIndex == 2) // Z Axis
			{
				if (m_bFirstFrameDraggingRotationGizmo)
				{
					m_UnmodifiedAxisProjectedOnto = gizmoUp;
					m_AxisProjectedOnto = m_UnmodifiedAxisProjectedOnto;
					m_AxisOfRotation = gizmoForward;
					m_PlaneN = m_AxisOfRotation;
					if (glm::abs(glm::dot(m_AxisProjectedOnto, camForward)) > 0.5f)
					{
						m_AxisProjectedOnto = gizmoForward;
						m_PlaneN = gizmoRight;
					}
				}

				dRot = CalculateDeltaRotationFromGizmoDrag(m_AxisOfRotation, rayStartG, rayEndG, pGizmoRot);
			}

			pGizmoRot = m_CurrentRot;

			Transform* selectedObjectTransform = m_CurrentlySelectedObjects[m_CurrentlySelectedObjects.size() - 1]->GetTransform();

			debugDrawer->drawLine(
				ToBtVec3(m_SelectedObjectDragStartPos),
				ToBtVec3(selectedObjectTransform->GetLocalPosition()),
				(m_DraggingAxisIndex == 0 ? btVector3(1.0f, 0.0f, 0.0f) : m_DraggingAxisIndex == 1 ? btVector3(0.0f, 1.0f, 0.0f) : btVector3(0.1f, 0.1f, 1.0f)));

			for (GameObject* gameObject : m_CurrentlySelectedObjects)
			{
				GameObject* parent = gameObject->GetParent();
				bool bObjectIsntChild = (parent == nullptr) || (Find(m_CurrentlySelectedObjects, parent) == m_CurrentlySelectedObjects.end());
				if (bObjectIsntChild)
				{
					gameObject->GetTransform()->Rotate(dRot);
				}
			}
		} break;
		case TransformState::SCALE:
		{
			glm::vec3 dScale(1.0f);
			real scale = 0.1f;

			if (m_DraggingAxisIndex == 0) // X Axis
			{
				glm::vec3 axis = gizmoRight;
				glm::vec3 planeN = gizmoForward;
				if (glm::abs(glm::dot(planeN, camForward)) < 0.5f)
				{
					planeN = gizmoUp;
				}
				glm::vec3 intersectionPont = CalculateRayPlaneIntersectionAlongAxis(axis, rayStartG, rayEndG, planeOrigin, planeN);
				glm::vec3 scaleNow = (intersectionPont - planeOrigin);
				dScale += (scaleNow - m_DraggingGizmoScaleLast) * scale;

				m_DraggingGizmoScaleLast = scaleNow;
			}
			else if (m_DraggingAxisIndex == 1) // Y Axis
			{
				glm::vec3 axis = gizmoUp;
				glm::vec3 planeN = gizmoRight;
				if (glm::abs(glm::dot(planeN, camForward)) < 0.5f)
				{
					planeN = gizmoForward;
				}
				glm::vec3 intersectionPont = CalculateRayPlaneIntersectionAlongAxis(axis, rayStartG, rayEndG, planeOrigin, planeN);
				glm::vec3 scaleNow = (intersectionPont - planeOrigin);
				dScale += (scaleNow - m_DraggingGizmoScaleLast) * scale;

				m_DraggingGizmoScaleLast = scaleNow;
			}
			else if (m_DraggingAxisIndex == 2) // Z Axis
			{
				glm::vec3 axis = gizmoForward;
				glm::vec3 planeN = gizmoUp;
				if (glm::abs(glm::dot(planeN, camForward)) < 0.5f)
				{
					planeN = gizmoRight;
				}
				glm::vec3 intersectionPont = CalculateRayPlaneIntersectionAlongAxis(axis, rayStartG, rayEndG, planeOrigin, planeN);
				glm::vec3 scaleNow = (intersectionPont - planeOrigin);
				dScale += (scaleNow - m_DraggingGizmoScaleLast) * scale;

				m_DraggingGizmoScaleLast = scaleNow;
			}
			else if (m_DraggingAxisIndex == 3) // All axes
			{
				glm::vec3 axis = gizmoRight;
				glm::vec3 planeN = gizmoForward;
				if (glm::abs(glm::dot(planeN, camForward)) < 0.5f)
				{
					planeN = gizmoUp;
				}
				glm::vec3 intersectionPont = CalculateRayPlaneIntersectionAlongAxis(axis, rayStartG, rayEndG, planeOrigin, planeN);
				glm::vec3 scaleNow = (intersectionPont - planeOrigin);
				dScale += (scaleNow - m_DraggingGizmoScaleLast) * scale;

				m_DraggingGizmoScaleLast = scaleNow;
			}

			Transform* selectedObjectTransform = m_CurrentlySelectedObjects[m_CurrentlySelectedObjects.size() - 1]->GetTransform();

			dScale = glm::clamp(dScale, 0.01f, 10.0f);

			debugDrawer->drawLine(
				ToBtVec3(m_SelectedObjectDragStartPos),
				ToBtVec3(selectedObjectTransform->GetLocalPosition()),
				(m_DraggingAxisIndex == 0 ? btVector3(1.0f, 0.0f, 0.0f) : m_DraggingAxisIndex == 1 ? btVector3(0.0f, 1.0f, 0.0f) : btVector3(0.1f, 0.1f, 1.0f)));

			for (GameObject* gameObject : m_CurrentlySelectedObjects)
			{
				GameObject* parent = gameObject->GetParent();
				bool bObjectIsntChild = (parent == nullptr) || (Find(m_CurrentlySelectedObjects, parent) == m_CurrentlySelectedObjects.end());
				if (bObjectIsntChild)
				{
					gameObject->GetTransform()->Scale(dScale);
				}
			}
		} break;
		default:
		{
			PrintError("Unhandled transform state in FlexEngine::HandleGizmoMovement\n");
		} break;
		}

		CalculateSelectedObjectsCenter();
	}

	bool FlexEngine::HandleObjectClick()
	{
		PhysicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld();

		btVector3 rayStart, rayEnd;
		GenerateRayAtMousePos(rayStart, rayEnd);

		const btRigidBody* pickedRB = physicsWorld->PickFirstBody(rayStart, rayEnd);
		if (pickedRB)
		{
			GameObject* pickedObject = static_cast<GameObject*>(pickedRB->getUserPointer());

			RigidBody* rb = pickedObject->GetRigidBody();
			if (!(rb->GetPhysicsFlags() & (u32)PhysicsFlag::UNSELECTABLE))
			{
				if (g_InputManager->GetKeyDown(KeyCode::KEY_LEFT_SHIFT))
				{
					ToggleSelectedObject(pickedObject);
				}
				else
				{
					DeselectCurrentlySelectedObjects();
					m_CurrentlySelectedObjects.push_back(pickedObject);
				}
				g_InputManager->ClearMouseInput();
				CalculateSelectedObjectsCenter();

				return true;
			}
			else
			{
				DeselectCurrentlySelectedObjects();
			}
		}

		return false;
	}

	void FlexEngine::GenerateRayAtMousePos(btVector3& outRayStart, btVector3& outRayEnd)
	{
		PhysicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld();

		const real maxDist = 1000.0f;
		outRayStart = ToBtVec3(g_CameraManager->CurrentCamera()->GetPosition());
		glm::vec2 mousePos = g_InputManager->GetMousePosition();
		btVector3 rayDir = physicsWorld->GenerateDirectionRayFromScreenPos((i32)mousePos.x, (i32)mousePos.y);
		outRayEnd = outRayStart + rayDir * maxDist;
	}

	bool FlexEngine::IsSimulationPaused() const
	{
		return m_bSimulationPaused;
	}

} // namespace flex
