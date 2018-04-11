#include "stdafx.hpp"

#include "FlexEngine.hpp"

#include <sstream>

#include <imgui.h>
#include <imgui_internal.h>

#include "BulletDynamics/Dynamics/btDynamicsWorld.h"

#include "FreeCamera.hpp"
#include "Logger.hpp"
#include "Helpers.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/Scenes/Scene_02.hpp"
#include "Scene/Scenes/TestScene.hpp"
#include "Physics/PhysicsManager.hpp"
#include "Physics/PhysicsWorld.hpp"

#include "Time.hpp"

namespace flex
{
	const u32 FlexEngine::EngineVersionMajor = 0;
	const u32 FlexEngine::EngineVersionMinor = 4;
	const u32 FlexEngine::EngineVersionPatch = 0;

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

		m_GameContext.inputManager = new InputManager();

		m_DefaultCamera = new FreeCamera(m_GameContext);
		m_DefaultCamera->SetPosition(glm::vec3(20.0f, 8.0f, -16.0f));
		m_DefaultCamera->SetYaw(glm::radians(130.0f));
		m_DefaultCamera->SetPitch(glm::radians(-10.0f));
		m_GameContext.camera->Update(m_GameContext); // Update to set initial values
		m_GameContext.camera = m_DefaultCamera;

		m_GameContext.physicsManager = new PhysicsManager();
		m_GameContext.physicsManager->Initialize();

		m_GameContext.sceneManager = new SceneManager();
		LoadDefaultScenes();

		m_GameContext.renderer->PostInitialize(m_GameContext);

		SetupImGuiStyles();
	}

	void FlexEngine::Destroy()
	{
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

		SafeDelete(m_DefaultCamera);

		DestroyWindowAndRenderer();
		MeshPrefab::Shutdown();
		Logger::Shutdown();
	}

	void FlexEngine::InitializeWindowAndRenderer()
	{
		// TODO: Determine user's display size before creating a window
		glm::vec2i desiredWindowSize(1920, 1080);
		glm::vec2i desiredWindowPos(300, 300);

		const std::string titleString = "Flex Engine v" + EngineVersionString();

#if COMPILE_VULKAN
		if (m_RendererIndex == RendererID::VULKAN)
		{
			m_GameContext.window = new vk::VulkanWindowWrapper(titleString + " - Vulkan", desiredWindowSize, desiredWindowPos, m_GameContext);
		}
#endif
#if COMPILE_OPEN_GL
		if (m_RendererIndex == RendererID::GL)
		{
			m_GameContext.window = new gl::GLWindowWrapper(titleString + " - OpenGL", desiredWindowSize, desiredWindowPos, m_GameContext);
		}
#endif
		if (m_GameContext.window == nullptr)
		{
			Logger::LogError("Failed to create a window! Are any compile flags set in stdafx.hpp?");
			return;
		}

		m_GameContext.window->Initialize();
		m_GameContext.window->RetrieveMonitorInfo(m_GameContext);

		i32 newWindowSizeY = i32(m_GameContext.monitor.height * 0.75f);
		i32 newWindowSizeX = i32(newWindowSizeY * 16.0f / 9.0f);
		m_GameContext.window->SetSize(newWindowSizeX, newWindowSizeY);

		i32 newWindowPosX = i32(newWindowSizeX * 0.1f);
		i32 newWindowPosY = i32(newWindowSizeY * 0.1f);
		m_GameContext.window->SetPosition(newWindowPosX, newWindowPosY);

		m_GameContext.window->Create();


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

		m_GameContext.window->SetUpdateWindowTitleFrequency(0.4f);

		m_GameContext.renderer->SetVSyncEnabled(m_VSyncEnabled);
		m_GameContext.renderer->SetClearColor(m_ClearColor.r, m_ClearColor.g, m_ClearColor.b);
	}

	void FlexEngine::DestroyWindowAndRenderer()
	{
		SafeDelete(m_GameContext.window);
		SafeDelete(m_GameContext.renderer);
	}

	void FlexEngine::LoadDefaultScenes()
	{
		Scene_02* scene02 = new Scene_02(m_GameContext);
		m_GameContext.sceneManager->AddScene(scene02, m_GameContext);

		//TestScene* pDefaultScene = new TestScene(m_GameContext);
		//m_GameContext.sceneManager->AddScene(pDefaultScene, m_GameContext);
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

		InitializeWindowAndRenderer();

		SetupImGuiStyles();

		LoadDefaultScenes();

		m_GameContext.renderer->PostInitialize(m_GameContext);
	}

	void FlexEngine::UpdateAndRender()
	{
		m_Running = true;
		sec frameStartTime = Time::Now();
		while (m_Running)
		{
			sec frameEndTime = Time::Now();
			sec dt = frameEndTime - frameStartTime;
			frameStartTime = frameEndTime;

			if (dt < 0.0f) dt = 0.0f;

			m_GameContext.deltaTime = dt;
			m_GameContext.elapsedTime = frameEndTime;

			m_GameContext.window->PollEvents();

			const glm::vec2i frameBufferSize = m_GameContext.window->GetFrameBufferSize();
			if (frameBufferSize.x == 0 || frameBufferSize.y == 0)
			{
				m_GameContext.inputManager->ClearAllInputs(m_GameContext);
			}

			// TODO: Bring keybindings out to external file (or at least variables)
			if (m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_V))
			{
				m_VSyncEnabled = !m_VSyncEnabled;
				m_GameContext.renderer->SetVSyncEnabled(m_VSyncEnabled);
			}

			if (m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_T))
			{
				m_GameContext.inputManager->Update();
				m_GameContext.inputManager->PostUpdate();
				m_GameContext.inputManager->ClearAllInputs(m_GameContext);
				CycleRenderer();
				continue;
			}

			// Call as early as possible in the frame
			m_GameContext.renderer->ImGuiNewFrame();

			m_GameContext.inputManager->PostImGuiUpdate(m_GameContext);
			if (m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_RIGHT_BRACKET))
			{
				m_GameContext.sceneManager->SetNextSceneActive();
			
				//m_GameContext.renderer->PostInitialize(m_GameContext);
			}
			else if (m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_LEFT_BRACKET))
			{
				m_GameContext.sceneManager->SetPreviousSceneActive();
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

			if (m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_P))
			{
				PhysicsDebuggingSettings& settings = m_GameContext.renderer->GetPhysicsDebuggingSettings();
				settings.DrawWireframe = !settings.DrawWireframe;
			}

			m_GameContext.camera->Update(m_GameContext);
			m_GameContext.sceneManager->UpdateAndRender(m_GameContext);
			m_GameContext.window->Update(m_GameContext);

			static bool windowOpen = true;
			if (m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_F1))
			{
				windowOpen = !windowOpen;
			}
			static const std::string titleString = (std::string("Flex Engine v") + EngineVersionString());
			static const char* titleCharStr = titleString.c_str();
			if (ImGui::Begin(titleCharStr, &windowOpen))
			{
				static const std::string rendererStr = std::string("Current renderer: " + m_RendererName);
				static const char* rendCharStr = rendererStr.c_str();
				ImGui::Text(rendCharStr);

				static const char* rendererSettingsStr = "Renderer settings";
				if (ImGui::TreeNode(rendererSettingsStr))
				{
					static const char* vsyncEnabledStr = "VSync";
					if (ImGui::Checkbox(vsyncEnabledStr, &m_VSyncEnabled))
					{
						m_GameContext.renderer->SetVSyncEnabled(m_VSyncEnabled);
					}

					static const char* physicsDebuggingStr = "Physics debugging";
					if (ImGui::TreeNode(physicsDebuggingStr))
					{
						PhysicsDebuggingSettings& physicsDebuggingSettings =  m_GameContext.renderer->GetPhysicsDebuggingSettings();

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

					ImGui::TreePop();
				}

				static const char* cameraStr = "Camera";
				if (ImGui::TreeNode(cameraStr))
				{
					static const char* moveSpeedStr = "Move speed";
					float moveSpeed = m_GameContext.camera->GetMoveSpeed();
					if (ImGui::SliderFloat(moveSpeedStr, &moveSpeed, 1.0f, 250.0f))
					{
						m_GameContext.camera->SetMoveSpeed(moveSpeed);
					}

					static const char* turnSpeedStr = "Turn speed";
					float turnSpeed = glm::degrees(m_GameContext.camera->GetRotationSpeed());
					if (ImGui::SliderFloat(turnSpeedStr, &turnSpeed, 0.01f, 0.3f))
					{
						m_GameContext.camera->SetRotationSpeed(glm::radians(turnSpeed));
					}

					glm::vec3 camPos = m_GameContext.camera->GetPosition();
					if (ImGui::DragFloat3("Position", &camPos.x, 0.1f))
					{
						m_GameContext.camera->SetPosition(camPos);
					}

					glm::vec2 camYawPitch;
					camYawPitch[0] = glm::degrees(m_GameContext.camera->GetYaw());
					camYawPitch[1] = glm::degrees(m_GameContext.camera->GetPitch());
					if (ImGui::DragFloat2("Yaw & Pitch", &camYawPitch.x, 0.05f))
					{
						m_GameContext.camera->SetYaw(glm::radians(camYawPitch[0]));
						m_GameContext.camera->SetPitch(glm::radians(camYawPitch[1]));
					}

					real camFOV = glm::degrees(m_GameContext.camera->GetFOV());
					if (ImGui::DragFloat("FOV", &camFOV, 0.05f, 10.0f, 150.0f))
					{
						m_GameContext.camera->SetFOV(glm::radians(camFOV));
					}

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
						m_GameContext.sceneManager->SetPreviousSceneActive();
					}

					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Previous scene");
						ImGui::EndTooltip();
					}

					ImGui::SameLine();
					
					const u32 currentSceneIndex = m_GameContext.sceneManager->CurrentSceneIndex() + 1;
					const u32 sceneCount = m_GameContext.sceneManager->GetSceneCount();
					const std::string currentSceneStr(m_GameContext.sceneManager->CurrentScene()->GetName() + 
						" (" + std::to_string(currentSceneIndex) + "/" + std::to_string(sceneCount) + ")");
					ImGui::Text(currentSceneStr.c_str());

					ImGui::SameLine();
					if (ImGui::Button(arrowNextStr))
					{
						m_GameContext.sceneManager->SetNextSceneActive();
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						static const char* nextSceneStr = "Next scene";
						ImGui::Text(nextSceneStr);
						ImGui::EndTooltip();
					}

					ImGui::TreePop();
				}

				m_GameContext.renderer->DrawImGuiItems(m_GameContext);
			}
			ImGui::End();

			// TODO: Consolidate functions?
			m_GameContext.inputManager->Update();
			m_GameContext.inputManager->PostUpdate();

			m_GameContext.renderer->Update(m_GameContext);

			m_GameContext.renderer->Draw(m_GameContext);
		}
	}

	void FlexEngine::SetupImGuiStyles()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.MouseDrawCursor = false;


		// Scale correctly on high DPI monitors
		// TODO: Handle more cleanly
		//if (m_GameContext.monitor.width > 1920.0f)
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

	void FlexEngine::Stop()
	{
		m_Running = false;
	}
	
	std::string FlexEngine::EngineVersionString()
	{
		return std::to_string(EngineVersionMajor) + "." +
			std::to_string(EngineVersionMinor) + "." +
			std::to_string(EngineVersionPatch);
	}
} // namespace flex
