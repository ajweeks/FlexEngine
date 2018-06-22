#include "stdafx.hpp"

#include "FlexEngine.hpp"

#include <sstream>
#include <stdlib.h> // For srand, rand
#include <time.h> // For time

#pragma warning(push, 0)
#include <imgui.h>
#include <imgui_internal.h>

#include <BulletDynamics/Dynamics/btDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
#pragma warning(pop)

#include "Audio/AudioManager.hpp"
#include "Cameras/CameraManager.hpp"
#include "Cameras/DebugCamera.hpp"
#include "Cameras/OverheadCamera.hpp"
#include "Helpers.hpp"
#include "Logger.hpp"
#include "Physics/PhysicsManager.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Profiler.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "Time.hpp"

namespace flex
{
	const u32 FlexEngine::EngineVersionMajor = 0;
	const u32 FlexEngine::EngineVersionMinor = 5;
	const u32 FlexEngine::EngineVersionPatch = 0;

	std::string FlexEngine::s_CurrentWorkingDirectory;
	std::vector<AudioSourceID> FlexEngine::s_AudioSourceIDs;
	GameObject* FlexEngine::m_CurrentlySelectedObject = nullptr;

	FlexEngine::FlexEngine()
	{
		// TODO: Add custom seeding for different random systems
		std::srand((u32)time(NULL));

		RetrieveCurrentWorkingDirectory();

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

		Logger::Initialize();

		Logger::LogInfo(std::to_string(m_RendererCount) + " renderer" + (m_RendererCount > 1 ? "s" : "") + " enabled");
		Logger::LogInfo("Current renderer: " + m_RendererName);
		assert(m_RendererCount != 0); // At least one renderer must be enabled! (see stdafx.h)

		//Logger::SetLogWarnings(false);
	}

	FlexEngine::~FlexEngine()
	{
		Destroy();
	}

	void FlexEngine::Initialize()
	{
		m_GameContext = {};
		m_GameContext.engineInstance = this;

		AudioManager::Initialize();

		CreateWindowAndRenderer();

		m_GameContext.inputManager = new InputManager();

		m_GameContext.cameraManager = new CameraManager();

		DebugCamera* debugCamera = new DebugCamera(m_GameContext);
		debugCamera->SetPosition(glm::vec3(20.0f, 8.0f, -16.0f));
		debugCamera->SetYaw(glm::radians(130.0f));
		debugCamera->SetPitch(glm::radians(-10.0f));
		m_GameContext.cameraManager->AddCamera(debugCamera, true);

		OverheadCamera* overheadCamera = new OverheadCamera(m_GameContext);
		m_GameContext.cameraManager->AddCamera(overheadCamera, false);

		InitializeWindowAndRenderer();
		m_GameContext.inputManager->Initialize(m_GameContext);

		m_GameContext.physicsManager = new PhysicsManager();
		m_GameContext.physicsManager->Initialize();

		m_GameContext.sceneManager = new SceneManager();
		m_GameContext.sceneManager->AddFoundScenes();

		m_GameContext.sceneManager->InitializeCurrentScene(m_GameContext);

		// Transform gizmo
		{
			MaterialCreateInfo matCreateInfo = {};
			matCreateInfo.name = "Transform";
			matCreateInfo.shaderName = "color";
			matCreateInfo.constAlbedo = glm::vec3(1.0f);
			matCreateInfo.engineMaterial = true;
			MaterialID transformMatID = m_GameContext.renderer->InitializeMaterial(m_GameContext, &matCreateInfo);

			m_TransformGizmo = new GameObject("Transform gizmo", GameObjectType::NONE);
			MeshComponent* transformMesh = m_TransformGizmo->SetMeshComponent(new MeshComponent(transformMatID, m_TransformGizmo));

			Material& transformGizmoMaterial = m_GameContext.renderer->GetMaterial(transformMatID);
			Shader& transformGizmoShader = m_GameContext.renderer->GetShader(transformGizmoMaterial.shaderID);
			VertexAttributes requiredVertexAttributes = transformGizmoShader.vertexAttributes;

			RenderObjectCreateInfo renderObjectCreateInfo = {};
			renderObjectCreateInfo.depthTestReadFunc = DepthTestFunc::ALWAYS;
			renderObjectCreateInfo.depthWriteEnable = false;

			transformMesh->SetRequiredAttributes(requiredVertexAttributes);
			transformMesh->LoadFromFile(m_GameContext, RESOURCE_LOCATION + "models/transform-gizmo.fbx", nullptr, &renderObjectCreateInfo);
			m_TransformGizmo->Initialize(m_GameContext);
			m_TransformGizmo->GetTransform()->Scale(0.1f);
		}

		m_GameContext.renderer->PostInitialize(m_GameContext);

		m_TransformGizmo->PostInitialize(m_GameContext);

		m_GameContext.sceneManager->PostInitializeCurrentScene(m_GameContext);

		SetupImGuiStyles();

		m_GameContext.cameraManager->Initialize(m_GameContext);

		if (s_AudioSourceIDs.empty())
		{
			s_AudioSourceIDs.push_back(AudioManager::AddAudioSource(RESOURCE_LOCATION + "audio/dud_dud_dud_dud.wav"));
			s_AudioSourceIDs.push_back(AudioManager::AddAudioSource(RESOURCE_LOCATION + "audio/drmapan.wav"));
			s_AudioSourceIDs.push_back(AudioManager::AddAudioSource(RESOURCE_LOCATION + "audio/blip.wav"));
		}
	}

	AudioSourceID FlexEngine::GetAudioSourceID(SoundEffect effect)
	{
		assert((i32)effect >= 0);
		assert((i32)effect < (i32)SoundEffect::LAST_ELEMENT);

		return s_AudioSourceIDs[(i32)effect];
	}

	void FlexEngine::Destroy()
	{
		m_CurrentlySelectedObject = nullptr;

		if (m_TransformGizmo)
		{
			m_TransformGizmo->Destroy(m_GameContext);
			SafeDelete(m_TransformGizmo);
		}

		if (m_GameContext.sceneManager)
		{
			m_GameContext.sceneManager->DestroyAllScenes(m_GameContext);
			SafeDelete(m_GameContext.sceneManager);
		}

		SafeDelete(m_GameContext.inputManager);

		if (m_GameContext.physicsManager)
		{
			m_GameContext.physicsManager->Destroy();
			SafeDelete(m_GameContext.physicsManager);
		}

		SafeDelete(m_GameContext.cameraManager);

		DestroyWindowAndRenderer();
		
		MeshComponent::DestroyAllLoadedMeshes();

		AudioManager::Destroy();

		Logger::Destroy();
	}

	void FlexEngine::CreateWindowAndRenderer()
	{
		// TODO: Determine user's display size before creating a window
		//glm::vec2i desiredWindowSize(500, 300);
		//glm::vec2i desiredWindowPos(300, 300);

		assert(m_GameContext.window == nullptr);
		assert(m_GameContext.renderer == nullptr);

		const std::string titleString = "Flex Engine v" + EngineVersionString();

#if COMPILE_VULKAN
		if (m_RendererIndex == RendererID::VULKAN)
		{
			m_GameContext.window = new vk::VulkanWindowWrapper(titleString, m_GameContext);
		}
#endif
#if COMPILE_OPEN_GL
		if (m_RendererIndex == RendererID::GL)
		{
			m_GameContext.window = new gl::GLWindowWrapper(titleString, m_GameContext);
		}
#endif
		if (m_GameContext.window == nullptr)
		{
			Logger::LogError("Failed to create a window! Are any compile flags set in stdafx.hpp?");
			return;
		}

		m_GameContext.window->Initialize();
		m_GameContext.window->RetrieveMonitorInfo(m_GameContext);

		float desiredAspectRatio = 16.0f / 9.0f;
		float desiredWindowSizeScreenPercetange = 0.6f;

		// What kind of monitor has different scales along each axis?
		assert(m_GameContext.monitor.contentScaleX == m_GameContext.monitor.contentScaleY);

		i32 newWindowSizeY = i32(m_GameContext.monitor.height * desiredWindowSizeScreenPercetange * m_GameContext.monitor.contentScaleY);
		i32 newWindowSizeX = i32(newWindowSizeY * desiredAspectRatio);
		// TODO:
		//m_GameContext.window->SetSize(newWindowSizeX, newWindowSizeY);

		i32 newWindowPosX = i32(newWindowSizeX * 0.1f);
		i32 newWindowPosY = i32(newWindowSizeY * 0.1f);
		//m_GameContext.window->SetPosition(newWindowPosX, newWindowPosY);

		m_GameContext.window->Create(glm::vec2i(newWindowSizeX, newWindowSizeY), glm::vec2i(newWindowPosX, newWindowPosY));


#if COMPILE_VULKAN
		if (m_RendererIndex == RendererID::VULKAN)
		{
			m_GameContext.renderer = new vk::VulkanRenderer(m_GameContext);
		}
#endif
#if COMPILE_OPEN_GL
		if (m_RendererIndex == RendererID::GL)
		{
			m_GameContext.renderer = new gl::GLRenderer(m_GameContext);
		}
#endif
		if (m_GameContext.renderer == nullptr)
		{
			Logger::LogError("Failed to create a renderer!");
			return;
		}
	}

	void FlexEngine::InitializeWindowAndRenderer()
	{
		m_GameContext.window->SetUpdateWindowTitleFrequency(0.5f);
		m_GameContext.window->PostInitialize();

		m_GameContext.renderer->Initialize(m_GameContext);
	}

	void FlexEngine::DestroyWindowAndRenderer()
	{
		if (m_GameContext.renderer)
		{
			m_GameContext.renderer->Destroy();
			SafeDelete(m_GameContext.renderer);
		}

		if (m_GameContext.window)
		{
			m_GameContext.window->Destroy();
			SafeDelete(m_GameContext.window);
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

	void FlexEngine::CycleRenderer()
	{
		// TODO? ??
		//m_GameContext.renderer->InvalidateFontObjects();

		m_CurrentlySelectedObject = nullptr;
		m_GameContext.sceneManager->RemoveScene(m_GameContext.sceneManager->CurrentScene(), m_GameContext);
		DestroyWindowAndRenderer();

		while (true)
		{
			m_RendererIndex = RendererID(((i32)m_RendererIndex + 1) % (i32)RendererID::_LAST_ELEMENT);

#if COMPILE_VULKAN
			if (m_RendererIndex == RendererID::VULKAN)
			{
				break;
			}
#endif
#if COMPILE_OPEN_GL
			if (m_RendererIndex == RendererID::GL)
			{
				break;
			}
#endif
		}
		m_RendererName = RenderIDToString(m_RendererIndex);
		Logger::LogInfo("Current renderer: " + m_RendererName);

		CreateWindowAndRenderer();
		InitializeWindowAndRenderer();

		SetupImGuiStyles();

		m_GameContext.sceneManager->AddFoundScenes();
		m_GameContext.sceneManager->InitializeCurrentScene(m_GameContext);
		m_GameContext.renderer->PostInitialize(m_GameContext);
		m_GameContext.sceneManager->PostInitializeCurrentScene(m_GameContext);
	}

	void FlexEngine::UpdateAndRender()
	{
		m_Running = true;
		sec frameStartTime = Time::CurrentSeconds();
		while (m_Running)
		{
			sec frameEndTime = Time::CurrentSeconds();
			sec dt = frameEndTime - frameStartTime;
			frameStartTime = frameEndTime;

			if (dt < 0.0f) dt = 0.0f;

			m_GameContext.deltaTime = dt;
			m_GameContext.elapsedTime = frameEndTime;

			Profiler::StartFrame();

			m_GameContext.window->PollEvents();

			const glm::vec2i frameBufferSize = m_GameContext.window->GetFrameBufferSize();
			if (frameBufferSize.x == 0 || frameBufferSize.y == 0)
			{
				m_GameContext.inputManager->ClearAllInputs(m_GameContext);
			}

			// Disabled for now since we only support Open GL
#if 0
			if (m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_T))
			{
				m_GameContext.inputManager->Update(m_GameContext);
				m_GameContext.inputManager->PostUpdate();
				m_GameContext.inputManager->ClearAllInputs(m_GameContext);
				CycleRenderer();
				continue;
			}
#endif

			// Call as early as possible in the frame
			// Starts new ImGui frame and clears debug draw lines
			m_GameContext.renderer->NewFrame();

			DrawImGuiObjects();

			// TODO: Bring keybindings out to external file (or at least variables)
			if (m_GameContext.inputManager->GetMouseButtonReleased(InputManager::MouseButton::LEFT))
			{
				glm::vec2 dragDist = m_GameContext.inputManager->GetMouseDragDistance(InputManager::MouseButton::LEFT);
				real maxMoveDist = 1.0f;

				// If mouse hasn't moved then the user clicked on something - select it
				if (glm::length(dragDist) < maxMoveDist)
				{
					glm::vec2 mousePos = m_GameContext.inputManager->GetMousePosition();

					PhysicsWorld* physicsWorld = m_GameContext.sceneManager->CurrentScene()->GetPhysicsWorld();

					btVector3 cameraPos = Vec3ToBtVec3(m_GameContext.cameraManager->CurrentCamera()->GetPosition());
					btVector3 rayStart(cameraPos);
					btVector3 rayEnd = physicsWorld->GenerateRayFromScreenPos(m_GameContext, (i32)mousePos.x, (i32)mousePos.y);

					btRigidBody* pickedBody = physicsWorld->PickBody(rayStart, rayEnd);

					if (pickedBody != nullptr)
					{
						GameObject* pickedGameObject = (GameObject*)(pickedBody->getUserPointer());

						if (pickedGameObject)
						{
							m_CurrentlySelectedObject = pickedGameObject;
							m_GameContext.inputManager->ClearMouseInput(m_GameContext);
						}
						else
						{
							m_CurrentlySelectedObject = nullptr;
						}
					}
					else
					{
						m_CurrentlySelectedObject = nullptr;
					}
				}
			}

			if (m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_V))
			{
				m_GameContext.renderer->SetVSyncEnabled(!m_GameContext.renderer->GetVSyncEnabled());
			}

			if (m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_RIGHT_BRACKET))
			{
				m_CurrentlySelectedObject = nullptr;
				m_GameContext.sceneManager->SetNextSceneActive(m_GameContext);
				m_GameContext.cameraManager->Initialize(m_GameContext);
			}
			else if (m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_LEFT_BRACKET))
			{
				m_CurrentlySelectedObject = nullptr;
				m_GameContext.sceneManager->SetPreviousSceneActive(m_GameContext);
				m_GameContext.cameraManager->Initialize(m_GameContext);
			}

			if (m_GameContext.inputManager->GetKeyDown(InputManager::KeyCode::KEY_LEFT_CONTROL) &&
				m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_R))
			{
				m_GameContext.renderer->ReloadShaders();
			}
			else if (m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_R))
			{
				m_GameContext.inputManager->ClearAllInputs(m_GameContext);

				m_CurrentlySelectedObject = nullptr;
				m_GameContext.sceneManager->ReloadCurrentScene(m_GameContext);
				m_GameContext.cameraManager->Initialize(m_GameContext);
			}

			if (m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_P))
			{
				PhysicsDebuggingSettings& settings = m_GameContext.renderer->GetPhysicsDebuggingSettings();
				settings.DrawWireframe = !settings.DrawWireframe;
			}

			bool altDown = m_GameContext.inputManager->GetKeyDown(InputManager::KeyCode::KEY_LEFT_ALT) ||
				m_GameContext.inputManager->GetKeyDown(InputManager::KeyCode::KEY_RIGHT_ALT);
			if (m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_F11) ||
				(altDown && m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_ENTER)))
			{
				m_GameContext.window->ToggleFullscreen();
			}

			m_GameContext.cameraManager->Update(m_GameContext);

			PROFILE_BEGIN("Scene UpdateAndRender");
			if (m_CurrentlySelectedObject)
			{
				m_TransformGizmo->SetVisible(true);
				m_TransformGizmo->GetTransform()->SetWorldlPosition(m_CurrentlySelectedObject->GetTransform()->GetWorldPosition());
			}
			else
			{
				m_TransformGizmo->SetVisible(false);
			}
			m_GameContext.sceneManager->UpdateAndRender(m_GameContext);
			PROFILE_END("Scene UpdateAndRender");

			
			m_GameContext.window->Update(m_GameContext);

			if (m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_S) &&
				m_GameContext.inputManager->GetKeyDown(InputManager::KeyCode::KEY_LEFT_CONTROL))
			{
				m_GameContext.sceneManager->CurrentScene()->SerializeToFile(m_GameContext);
			}

			bool bWriteProfilingResultsToFile = 
				m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_K);

			m_GameContext.renderer->Update(m_GameContext);

			// TODO: Consolidate functions?
			m_GameContext.inputManager->Update(m_GameContext);
			m_GameContext.inputManager->PostUpdate();

			PROFILE_BEGIN("Render");
			m_GameContext.renderer->Draw(m_GameContext);
			PROFILE_END("Render");

			Profiler::EndFrame(true);

			if (bWriteProfilingResultsToFile)
			{
				Profiler::PrintResultsToFile();
			}
		}
	}

	void FlexEngine::SetupImGuiStyles()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.MouseDrawCursor = false;

		io.FontGlobalScale = m_GameContext.monitor.contentScaleX;

		ImGuiStyle& style = ImGui::GetStyle();
		style.Colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
		style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
		style.Colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.85f);
		style.Colors[ImGuiCol_ChildWindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_PopupBg] = ImVec4(0.05f, 0.05f, 0.10f, 0.90f);
		style.Colors[ImGuiCol_Border] = ImVec4(0.70f, 0.70f, 0.70f, 0.40f);
		style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_FrameBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.30f);
		style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.90f, 0.80f, 0.80f, 0.40f);
		style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.90f, 0.65f, 0.65f, 0.45f);
		style.Colors[ImGuiCol_TitleBg] = ImVec4(0.74f, 0.33f, 0.09f, 0.94f);
		style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.74f, 0.33f, 0.09f, 0.20f);
		style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.87f, 0.15f, 0.02f, 0.94f);
		style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.83f, 0.25f, 0.07f, 0.55f);
		style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.20f, 0.25f, 0.30f, 0.60f);
		style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.80f, 0.75f, 0.40f, 0.40f);
		style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.80f, 0.75f, 0.41f, 0.50f);
		style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.92f, 0.82f, 0.29f, 0.60f);
		style.Colors[ImGuiCol_CheckMark] = ImVec4(0.97f, 0.54f, 0.03f, 1.00f);
		style.Colors[ImGuiCol_SliderGrab] = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
		style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.82f, 0.61f, 0.37f, 1.00f);
		style.Colors[ImGuiCol_Button] = ImVec4(0.95f, 0.53f, 0.22f, 0.60f);
		style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.82f, 0.49f, 0.20f, 1.00f);
		style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.71f, 0.37f, 0.11f, 1.00f);
		style.Colors[ImGuiCol_Header] = ImVec4(0.66f, 0.32f, 0.17f, 0.76f);
		style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.74f, 0.43f, 0.29f, 0.76f);
		style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.60f, 0.23f, 0.07f, 0.80f);
		style.Colors[ImGuiCol_Separator] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
		style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.70f, 0.62f, 0.60f, 1.00f);
		style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.90f, 0.78f, 0.70f, 1.00f);
		style.Colors[ImGuiCol_ResizeGrip] = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
		style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.60f);
		style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
		style.Colors[ImGuiCol_CloseButton] = ImVec4(0.47f, 0.00f, 0.00f, 0.63f);
		style.Colors[ImGuiCol_CloseButtonHovered] = ImVec4(0.90f, 0.17f, 0.17f, 0.60f);
		style.Colors[ImGuiCol_CloseButtonActive] = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
		style.Colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(1.00f, 0.57f, 0.31f, 0.35f);
		style.Colors[ImGuiCol_ModalWindowDarkening] = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
	}

	void FlexEngine::DrawImGuiObjects()
	{
		ImGui::ShowDemoWindow();

		static const std::string titleString = (std::string("Flex Engine v") + EngineVersionString());
		static const char* titleCharStr = titleString.c_str();
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		ImGui::SetNextWindowSize(ImVec2(0.0f, (real)m_GameContext.window->GetFrameBufferSize().y),
								 ImGuiCond_Always);
		if (ImGui::Begin(titleCharStr, nullptr, flags))
		{
			static const std::string rendererNameStringStr = std::string("Current renderer: " + m_RendererName);
			static const char* renderNameStr = rendererNameStringStr.c_str();
			ImGui::TextUnformatted(renderNameStr);

			static const char* rendererSettingsStr = "Renderer settings";
			if (ImGui::TreeNode(rendererSettingsStr))
			{
				bool bVSyncEnabled = m_GameContext.renderer->GetVSyncEnabled();
				static const char* vSyncEnabledStr = "VSync";
				if (ImGui::Checkbox(vSyncEnabledStr, &bVSyncEnabled))
				{
					m_GameContext.renderer->SetVSyncEnabled(bVSyncEnabled);
				}

				static const char* uiScaleStr = "UI Scale";
				ImGui::SliderFloat(uiScaleStr, &ImGui::GetIO().FontGlobalScale, 0.25f, 3.0f);

				static const char* windowModeStr = "##WindowMode";
				static const char* windowModesStr[] = { "Windowed", "Borderless Windowed" };
				static const i32 windowModeCount = 2;
				i32 currentItemIndex = (i32)m_GameContext.window->GetFullscreenMode();
				if (ImGui::ListBox(windowModeStr, &currentItemIndex, windowModesStr, windowModeCount))
				{
					Window::FullscreenMode newFullscreenMode = Window::FullscreenMode(currentItemIndex);
					m_GameContext.window->SetFullscreenMode(newFullscreenMode);
				}

				static const char* physicsDebuggingStr = "Physics debugging";
				if (ImGui::TreeNode(physicsDebuggingStr))
				{
					PhysicsDebuggingSettings& physicsDebuggingSettings = m_GameContext.renderer->GetPhysicsDebuggingSettings();

					static const char* disableAllStr = "Disable All";
					ImGui::Checkbox(disableAllStr, &physicsDebuggingSettings.DisableAll);

					ImGui::Spacing();
					ImGui::Spacing();
					ImGui::Spacing();

					static const char* wireframeStr = "Wireframe";
					ImGui::Checkbox(wireframeStr, &physicsDebuggingSettings.DrawWireframe);

					static const char* aabbStr = "AABB";
					ImGui::Checkbox(aabbStr, &physicsDebuggingSettings.DrawAabb);

					static const char* drawFeaturesTextStr = "Draw Features Text";
					ImGui::Checkbox(drawFeaturesTextStr, &physicsDebuggingSettings.DrawFeaturesText);

					static const char* drawContactPointsStr = "Draw Contact Points";
					ImGui::Checkbox(drawContactPointsStr, &physicsDebuggingSettings.DrawContactPoints);

					static const char* noDeactivationStr = "No Deactivation";
					ImGui::Checkbox(noDeactivationStr, &physicsDebuggingSettings.NoDeactivation);

					static const char* noHelpTextStr = "No Help Text";
					ImGui::Checkbox(noHelpTextStr, &physicsDebuggingSettings.NoHelpText);

					static const char* drawTextStr = "Draw Text";
					ImGui::Checkbox(drawTextStr, &physicsDebuggingSettings.DrawText);

					static const char* profileTimingsStr = "Profile Timings";
					ImGui::Checkbox(profileTimingsStr, &physicsDebuggingSettings.ProfileTimings);

					static const char* satComparisonStr = "Sat Comparison";
					ImGui::Checkbox(satComparisonStr, &physicsDebuggingSettings.EnableSatComparison);

					static const char* disableBulletLCPStr = "Disable Bullet LCP";
					ImGui::Checkbox(disableBulletLCPStr, &physicsDebuggingSettings.DisableBulletLCP);

					static const char* ccdStr = "CCD";
					ImGui::Checkbox(ccdStr, &physicsDebuggingSettings.EnableCCD);

					static const char* drawConstraintsStr = "Draw Constraints";
					ImGui::Checkbox(drawConstraintsStr, &physicsDebuggingSettings.DrawConstraints);

					static const char* drawConstraintLimitsStr = "Draw Constraint Limits";
					ImGui::Checkbox(drawConstraintLimitsStr, &physicsDebuggingSettings.DrawConstraintLimits);

					static const char* fastWireframeStr = "Fast Wireframe";
					ImGui::Checkbox(fastWireframeStr, &physicsDebuggingSettings.FastWireframe);

					static const char* drawNormalsStr = "Draw Normals";
					ImGui::Checkbox(drawNormalsStr, &physicsDebuggingSettings.DrawNormals);

					static const char* drawFramesStr = "Draw Frames";
					ImGui::Checkbox(drawFramesStr, &physicsDebuggingSettings.DrawFrames);

					ImGui::TreePop();
				}

				static const char* postProcessStr = "Post processing";
				if (ImGui::TreeNode(postProcessStr))
				{
					Renderer::PostProcessSettings& postProcessSettings = m_GameContext.renderer->GetPostProcessSettings();

					static const char* fxaaEnabledStr = "FXAA";
					ImGui::Checkbox(fxaaEnabledStr, &postProcessSettings.bEnableFXAA);

					if (postProcessSettings.bEnableFXAA)
					{
						ImGui::Indent();
						static const char* fxaaShowEdgesEnabledStr = "Show edges";
						ImGui::Checkbox(fxaaShowEdgesEnabledStr, &postProcessSettings.bEnableFXAADEBUGShowEdges);
						ImGui::Unindent();
					}

					static const char* brightnessStr = "Brightness (RGB)";
					ImGui::SliderFloat3(brightnessStr, &postProcessSettings.brightness.r, 0.0f, 2.5f);

					static const char* offsetStr = "Offset";
					ImGui::SliderFloat3(offsetStr, &postProcessSettings.offset.r, -0.35f, 0.35f);

					static const char* saturationStr = "Saturation";
					ImGui::SliderFloat(saturationStr, &postProcessSettings.saturation, 0.0f, 2.0f);

					ImGui::TreePop();
				}

				ImGui::TreePop();
			}

			// TODO: Add DrawImGuiItems to camera class and let it handle itself?
			const char* cameraStr = "Camera";
			if (ImGui::TreeNode(cameraStr))
			{
				BaseCamera* currentCamera = m_GameContext.cameraManager->CurrentCamera();

				const i32 cameraCount = m_GameContext.cameraManager->CameraCount();

				if (cameraCount > 1) // Only show arrows if other cameras exist
				{
					static const char* arrowPrevStr = "<";
					static const char* arrowNextStr = ">";

					if (ImGui::Button(arrowPrevStr))
					{
						m_GameContext.cameraManager->SwtichToIndexRelative(m_GameContext, -1, false);
					}

					ImGui::SameLine();

					std::string cameraNameStr = currentCamera->GetName();
					ImGui::TextUnformatted(cameraNameStr.c_str());

					ImGui::SameLine();

					ImGui::PushItemWidth(-1.0f);
					if (ImGui::Button(arrowNextStr))
					{
						m_GameContext.cameraManager->SwtichToIndexRelative(m_GameContext, 1, false);
					}
					ImGui::PopItemWidth();
				}

				static const char* moveSpeedStr = "Move speed";
				float moveSpeed = currentCamera->GetMoveSpeed();
				if (ImGui::SliderFloat(moveSpeedStr, &moveSpeed, 1.0f, 250.0f))
				{
					currentCamera->SetMoveSpeed(moveSpeed);
				}

				static const char* turnSpeedStr = "Turn speed";
				float turnSpeed = glm::degrees(currentCamera->GetRotationSpeed());
				if (ImGui::SliderFloat(turnSpeedStr, &turnSpeed, 0.01f, 0.3f))
				{
					currentCamera->SetRotationSpeed(glm::radians(turnSpeed));
				}

				glm::vec3 camPos = currentCamera->GetPosition();
				if (ImGui::DragFloat3("Position", &camPos.x, 0.1f))
				{
					currentCamera->SetPosition(camPos);
				}

				glm::vec2 camYawPitch;
				camYawPitch[0] = glm::degrees(currentCamera->GetYaw());
				camYawPitch[1] = glm::degrees(currentCamera->GetPitch());
				if (ImGui::DragFloat2("Yaw & Pitch", &camYawPitch.x, 0.05f))
				{
					currentCamera->SetYaw(glm::radians(camYawPitch[0]));
					currentCamera->SetPitch(glm::radians(camYawPitch[1]));
				}

				real camFOV = glm::degrees(currentCamera->GetFOV());
				if (ImGui::DragFloat("FOV", &camFOV, 0.05f, 10.0f, 150.0f))
				{
					currentCamera->SetFOV(glm::radians(camFOV));
				}

				if (ImGui::Button("Reset orientation"))
				{
					currentCamera->ResetOrientation();
				}

				ImGui::SameLine();
				if (ImGui::Button("Reset position"))
				{
					currentCamera->ResetPosition();
				}

				ImGui::TreePop();
			}

			static const char* loggingStr = "Logging";
			if (ImGui::TreeNode(loggingStr))
			{
				bool suppressInfo = Logger::GetSuppressInfo();
				i32 suppressedInfoCount = Logger::GetSuppressedInfoCount();
				const std::string infoStr("Suppress Info (" + std::to_string(suppressedInfoCount) + ")###SUppressedInfo");
				if (ImGui::Checkbox(infoStr.c_str(), &suppressInfo))
				{
					Logger::SetSuppressInfo(suppressInfo);
				}

				bool suppressWarnings = Logger::GetSuppressWarnings();
				i32 suppressedWarningCount = Logger::GetSuppressedWarningCount();
				const std::string warningStr("Suppress Warnings (" + std::to_string(suppressedWarningCount) + ")###SuppressedWarnings");
				if (ImGui::Checkbox(warningStr.c_str(), &suppressWarnings))
				{
					Logger::SetSuppressWarnings(suppressWarnings);
				}

				// TODO: Why can't this be turned on again while errors are being spammed?
				bool suppressErrors = Logger::GetSuppressErrors();
				i32 suppressedErrorCount = Logger::GetSuppressedErrorCount();
				const std::string errorStr("Suppress Errors (" + std::to_string(suppressedErrorCount) + ")###SuppressedErrors");
				if (ImGui::Checkbox(errorStr.c_str(), &suppressErrors))
				{
					Logger::SetSuppressErrors(suppressErrors);
				}

				ImGui::TreePop();
			}

			static const char* scenesStr = "Scenes";
			if (ImGui::TreeNode(scenesStr))
			{
				static const char* arrowPrevStr = "<";
				static const char* arrowNextStr = ">";

				if (ImGui::Button(arrowPrevStr))
				{
					m_GameContext.sceneManager->SetPreviousSceneActive(m_GameContext);
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Previous scene");
					ImGui::EndTooltip();
				}

				ImGui::SameLine();


				const u32 currentSceneIndex = m_GameContext.sceneManager->CurrentSceneIndex() + 1;
				const u32 sceneCount = m_GameContext.sceneManager->GetSceneCount();
				const std::string currentSceneStr(m_GameContext.sceneManager->CurrentScene()->GetName() +
					" (" + std::to_string(currentSceneIndex) + "/" + std::to_string(sceneCount) + ")");
				ImGui::TextUnformatted(currentSceneStr.c_str());

				if (ImGui::IsItemHovered())
				{
					std::string filePath = m_GameContext.sceneManager->CurrentScene()->GetJSONFilePath();
					size_t lastSlash = filePath.rfind('\\');
					if (lastSlash != std::string::npos)
					{
						size_t secondLastSlash = filePath.rfind('\\', lastSlash - 1);
						if (secondLastSlash != std::string::npos)
						{
							ImGui::BeginTooltip();

							std::string fileNameWithOneDir = filePath.substr(secondLastSlash);
							ImGui::TextUnformatted(fileNameWithOneDir.c_str());

							ImGui::EndTooltip();
						}
					}
				}

				ImGui::SameLine();
				if (ImGui::Button(arrowNextStr))
				{
					m_GameContext.sceneManager->SetNextSceneActive(m_GameContext);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					static const char* nextSceneStr = "Next scene";
					ImGui::TextUnformatted(nextSceneStr);
					ImGui::EndTooltip();
				}

				ImGui::TreePop();
			}

			m_GameContext.renderer->DrawImGuiItems(m_GameContext);

			ImGui::End();
		}
	}

	void FlexEngine::Stop()
	{
		m_Running = false;
	}

	GameObject* FlexEngine::GetSelectedObject()
	{
		return m_CurrentlySelectedObject;
	}

	void FlexEngine::SetSelectedObject(GameObject* gameObject)
	{
		m_CurrentlySelectedObject = gameObject;
	}
	
	std::string FlexEngine::EngineVersionString()
	{
		return std::to_string(EngineVersionMajor) + "." +
			std::to_string(EngineVersionMinor) + "." +
			std::to_string(EngineVersionPatch);
	}
} // namespace flex
