#include "stdafx.hpp"

#include "FlexEngine.hpp"

#include <sstream>

#include <imgui.h>

#include "FreeCamera.hpp"
#include "Logger.hpp"
#include "Helpers.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/Scenes/Scene_02.hpp"
#include "Scene/Scenes/TestScene.hpp"
#include "Typedefs.hpp"

namespace flex
{
	FlexEngine::FlexEngine() :
		m_ClearColor(1.0f, 0.0, 1.0f),
		m_VSyncEnabled(false)
	{
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

		InitializeWindowAndRenderer();

		m_DefaultCamera = new FreeCamera(m_GameContext);
		m_DefaultCamera->SetPosition(glm::vec3(0.0f, 5.0f, -15.0f));
		m_GameContext.camera = m_DefaultCamera;

		m_GameContext.sceneManager = new SceneManager();

		LoadDefaultScenes();

		m_GameContext.inputManager = new InputManager();

		m_GameContext.renderer->PostInitialize(m_GameContext);

		// TODO: remove this call and move code into renderer's constructor
		m_GameContext.renderer->ImGui_Init(m_GameContext);

		ImGuiIO& io = ImGui::GetIO();
		io.MouseDrawCursor = false;

		// Scale correctly on high DPI monitors
		// TODO: Handle more cleanly
		if (m_GameContext.monitor.width > 1920.0f)
		{
			io.FontGlobalScale = 2.0f;
		}

		ImGuiStyle& style = ImGui::GetStyle();
		style.Colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
		style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
		style.Colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.70f);
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
		style.Colors[ImGuiCol_ComboBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.99f);
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

	void FlexEngine::Destroy()
	{
		m_GameContext.renderer->ImGui_ReleaseRenderObjects();
		ImGui::Shutdown();

		if (m_GameContext.sceneManager) m_GameContext.sceneManager->DestroyAllScenes(m_GameContext);
		SafeDelete(m_GameContext.sceneManager);
		SafeDelete(m_GameContext.inputManager);
		SafeDelete(m_DefaultCamera);

		DestroyWindowAndRenderer();
	}

	void FlexEngine::InitializeWindowAndRenderer()
	{
		const glm::vec2i windowSize(1920, 1080);
		glm::vec2i windowPos(300, 300);

#if COMPILE_VULKAN
		if (m_RendererIndex == RendererID::VULKAN)
		{
			vk::VulkanWindowWrapper* vulkanWindow = new vk::VulkanWindowWrapper("Flex Engine - Vulkan", windowSize, windowPos, m_GameContext);
			m_Window = vulkanWindow;
			vk::VulkanRenderer* vulkanRenderer = new vk::VulkanRenderer(m_GameContext);
			m_GameContext.renderer = vulkanRenderer;
		}
#endif
#if COMPILE_OPEN_GL
		if (m_RendererIndex == RendererID::GL)
		{
			gl::GLWindowWrapper* glWindow = new gl::GLWindowWrapper("Flex Engine - OpenGL", windowSize, windowPos, m_GameContext);
			m_Window = glWindow;
			gl::GLRenderer* glRenderer = new gl::GLRenderer(m_GameContext);
			m_GameContext.renderer = glRenderer;
		}
#endif

		m_Window->SetUpdateWindowTitleFrequency(0.4f);

		m_GameContext.renderer->SetVSyncEnabled(m_VSyncEnabled);
		m_GameContext.renderer->SetClearColor(m_ClearColor.r, m_ClearColor.g, m_ClearColor.b);
	}

	void FlexEngine::DestroyWindowAndRenderer()
	{
		SafeDelete(m_Window);
		SafeDelete(m_GameContext.renderer);
	}

	void FlexEngine::LoadDefaultScenes()
	{
		//TestScene* pDefaultScene = new TestScene(m_GameContext);
		//m_SceneManager->AddScene(pDefaultScene, m_GameContext);

		Scene_02* scene02 = new Scene_02(m_GameContext);
		m_GameContext.sceneManager->AddScene(scene02, m_GameContext);
	}

	std::string FlexEngine::RenderIDToString(RendererID rendererID) const
	{
		switch (rendererID)
		{
		case RendererID::VULKAN: return "Vulkan";
		case RendererID::GL: return "OpenGL";
		case RendererID::_LAST_ELEMENT:  // Fallthrough
		default:
			return "Unknown";
		}
	}

	void FlexEngine::CycleRenderer()
	{
		m_GameContext.renderer->ImGui_ReleaseRenderObjects();
		m_GameContext.sceneManager->RemoveScene(m_GameContext.sceneManager->CurrentScene(), m_GameContext);
		DestroyWindowAndRenderer();

		while (true)
		{
			m_RendererIndex = RendererID(((int)m_RendererIndex + 1) % (int)RendererID::_LAST_ELEMENT);

#if COMPILE_VULKAN
			if (m_RendererIndex == RendererID::VULKAN) break;
#endif
#if COMPILE_OPEN_GL
			if (m_RendererIndex == RendererID::GL) break;
#endif
		}
		m_RendererName = RenderIDToString(m_RendererIndex);
		Logger::LogInfo("Current renderer: " + m_RendererName);

		InitializeWindowAndRenderer();

		LoadDefaultScenes();

		m_GameContext.renderer->PostInitialize(m_GameContext);

		m_GameContext.renderer->ImGui_Init(m_GameContext);
	}

	void FlexEngine::UpdateAndRender()
	{
		m_Running = true;
		float previousTime = (float)m_Window->GetTime();
		while (m_Running)
		{
			float currentTime = (float)m_Window->GetTime();
			float dt = (currentTime - previousTime);
			previousTime = currentTime;
			if (dt < 0.0f) dt = 0.0f;

			m_GameContext.deltaTime = dt;
			m_GameContext.elapsedTime = currentTime;

			m_GameContext.window->PollEvents();

			// Call as early as possible in the frame
			m_GameContext.renderer->ImGui_NewFrame(m_GameContext);

			m_GameContext.inputManager->PostImGuiUpdate(m_GameContext);

			// TODO: Bring keybindings out to external file (or at least variables)
			if (m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_V))
			{
				m_VSyncEnabled = !m_VSyncEnabled;
				m_GameContext.renderer->SetVSyncEnabled(m_VSyncEnabled);
			}

			if (m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_T))
			{
				m_GameContext.inputManager->ClearAllInputs(m_GameContext);
				CycleRenderer();
				continue;
			}

			if (m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_RIGHT_BRACKET) ||
				m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_LEFT_BRACKET))
			{
				const std::string currentSceneName = m_GameContext.sceneManager->CurrentScene()->GetName();
				m_GameContext.sceneManager->DestroyAllScenes(m_GameContext);
				if (currentSceneName.compare("TestScene") == 0)
				{
					Scene_02* newScene = new Scene_02(m_GameContext);
					m_GameContext.sceneManager->AddScene(newScene, m_GameContext);
				}
				else
				{
					TestScene* newScene = new TestScene(m_GameContext);
					m_GameContext.sceneManager->AddScene(newScene, m_GameContext);
				}

				m_GameContext.renderer->PostInitialize(m_GameContext);
			}

			// TODO: Figure out better
			if (m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_R))
			{
				const std::string sceneName = m_GameContext.sceneManager->CurrentScene()->GetName();
				m_GameContext.sceneManager->RemoveScene(m_GameContext.sceneManager->CurrentScene(), m_GameContext);

				DestroyWindowAndRenderer();
				InitializeWindowAndRenderer();

				//m_GameContext.renderer->ReloadShaders(m_GameContext);

				if (sceneName.compare("TestScene") == 0)
				{
					TestScene* newScene = new TestScene(m_GameContext);
					m_GameContext.sceneManager->AddScene(newScene, m_GameContext);
				}
				else
				{
					Scene_02* newScene = new Scene_02(m_GameContext);
					m_GameContext.sceneManager->AddScene(newScene, m_GameContext);
				}

				m_GameContext.renderer->PostInitialize(m_GameContext);
			}

			m_GameContext.camera->Update(m_GameContext);
			m_GameContext.sceneManager->UpdateAndRender(m_GameContext);
			m_GameContext.inputManager->Update();
			m_GameContext.window->Update(m_GameContext);
			m_GameContext.inputManager->PostUpdate();

			bool windowOpen = true;
			ImGui::Begin("", &windowOpen);
			{
				const std::string rendStr("Current renderer: " + m_RendererName);
				ImGui::Text(rendStr.c_str());

				if (ImGui::CollapsingHeader("Renderer settings"))
				{
					std::string vsyncEnabledStr = "VSync " + std::string(m_VSyncEnabled ? "enabled" : "disabled");
					ImGui::Checkbox(vsyncEnabledStr.c_str(), &m_VSyncEnabled);
					m_GameContext.renderer->SetVSyncEnabled(m_VSyncEnabled);

					if (ImGui::TreeNode("Camera"))
					{
						glm::vec3 camPos = m_GameContext.camera->GetPosition();
						ImGui::DragFloat3("Position", &camPos.x, 0.1f);
						m_GameContext.camera->SetPosition(camPos);

						glm::vec2 camYawPitch;
						camYawPitch[0] = m_GameContext.camera->GetYaw();
						camYawPitch[1] = m_GameContext.camera->GetPitch();
						ImGui::DragFloat2("Yaw & Pitch", &camYawPitch.x, 0.01f);
						m_GameContext.camera->SetYaw(camYawPitch[0]);
						m_GameContext.camera->SetPitch(camYawPitch[1]);

						float camFOV = glm::degrees(m_GameContext.camera->GetFOV());
						ImGui::DragFloat("FOV", &camFOV, 0.01f, 10.0f, 150.0f);
						m_GameContext.camera->SetFOV(glm::radians(camFOV));

						if (ImGui::Button("Reset orientation"))
						{
							m_GameContext.camera->ResetOrientation();
						}

						ImGui::SameLine();
						if (ImGui::Button("Reset position"))
						{
							m_GameContext.camera->ResetPosition();
						}

						ImGui::TreePop();
					}

					if (ImGui::TreeNode("Logging"))
					{
						bool suppressInfo = Logger::GetSuppressInfo();
						int suppressedInfoCount = Logger::GetSuppressedInfoCount();
						bool suppressWarnings = Logger::GetSuppressWarnings();
						int suppressedWarningCount = Logger::GetSuppressedWarningCount();
						bool suppressErrors = Logger::GetSuppressErrors();
						int suppressedErrorCount = Logger::GetSuppressedErrorCount();

						ImGui::Checkbox("Suppress Info", &suppressInfo);
						Logger::SetSuppressInfo(suppressInfo);
						ImGui::Text(std::string("Suppressed info count: " + std::to_string(suppressedInfoCount)).c_str());

						ImGui::Checkbox("Suppress Warnings", &suppressWarnings);
						Logger::SetSuppressWarnings(suppressWarnings);
						ImGui::Text(std::string("Suppressed warning count: " + std::to_string(suppressedWarningCount)).c_str());

						ImGui::Checkbox("Suppress Errors", &suppressErrors);
						Logger::SetSuppressErrors(suppressErrors);
						ImGui::Text(std::string("Suppressed error count: " + std::to_string(suppressedErrorCount)).c_str());

						ImGui::TreePop();
					}
				}

				m_GameContext.renderer->DrawImGuiItems(m_GameContext);
			}
			ImGui::End();

			static bool open = true;
			ImGui::ShowTestWindow(&open);

			m_GameContext.renderer->Update(m_GameContext);

			// Call as late in the frame as possible
			m_GameContext.renderer->ImGui_Render();

			m_GameContext.renderer->Draw(m_GameContext);
		}
	}

	void FlexEngine::Stop()
	{
		m_Running = false;
	}
} // namespace flex
