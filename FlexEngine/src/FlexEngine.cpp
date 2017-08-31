#include "stdafx.h"

#include "FlexEngine.h"

#include <sstream>

#include <imgui.h>

#include "FreeCamera.h"
#include "Logger.h"
#include "Helpers.h"
#include "Scene/SceneManager.h"
#include "Scene/Scenes/Scene_02.h"
#include "Scene/Scenes/TestScene.h"
#include "Typedefs.h"

namespace flex
{
	FlexEngine::FlexEngine() :
		m_ClearColor(0.08f, 0.13f, 0.2f),
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

		Logger::LogInfo(std::to_string(m_RendererCount) + " renderer" + (m_RendererCount > 1 ? "s" : "") + " enabled");
		Logger::LogInfo("Current renderer: " + m_RendererName);
		Logger::Assert(m_RendererCount != 0, "At least one renderer must be enabled! (see stdafx.h)");
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

		m_SceneManager = new SceneManager();

		LoadDefaultScenes();

		m_DefaultCamera = new FreeCamera(m_GameContext);
		m_DefaultCamera->SetPosition(glm::vec3(0.0f, 5.0f, -15.0f));
		m_GameContext.camera = m_DefaultCamera;

		m_GameContext.inputManager = new InputManager();

		m_GameContext.renderer->PostInitialize();

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

		if (m_SceneManager) m_SceneManager->DestroyAllScenes(m_GameContext);
		SafeDelete(m_SceneManager);
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
		TestScene* pDefaultScene = new TestScene(m_GameContext);
		m_SceneManager->AddScene(pDefaultScene, m_GameContext);

		//Scene_02* scene02 = new Scene_02(m_GameContext);
		//m_SceneManager->AddScene(scene02, m_GameContext);
	}

	std::string FlexEngine::RenderIDToString(RendererID rendererID) const
	{
		switch (rendererID)
		{
		case RendererID::VULKAN: return "Vulkan";
		case RendererID::GL: return "Open GL";
		case RendererID::_LAST_ELEMENT:  // Fallthrough
		default:
			return "Unknown";
		}
	}

	void FlexEngine::CycleRenderer()
	{
		m_GameContext.renderer->ImGui_ReleaseRenderObjects();
		m_SceneManager->RemoveScene(m_SceneManager->CurrentScene(), m_GameContext);
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

		m_GameContext.renderer->PostInitialize();

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
				const std::string currentSceneName = m_SceneManager->CurrentScene()->GetName();
				m_SceneManager->DestroyAllScenes(m_GameContext);
				if (currentSceneName.compare("TestScene") == 0)
				{
					Scene_02* newScene = new Scene_02(m_GameContext);
					m_SceneManager->AddScene(newScene, m_GameContext);

					m_GameContext.renderer->PostInitialize();
				}
				else
				{
					TestScene* newScene = new TestScene(m_GameContext);
					m_SceneManager->AddScene(newScene, m_GameContext);

					m_GameContext.renderer->PostInitialize();
				}
			}

			// TODO: Figure out better
			if (m_GameContext.inputManager->GetKeyPressed(InputManager::KeyCode::KEY_R))
			{
				m_SceneManager->RemoveScene(m_SceneManager->CurrentScene(), m_GameContext);

				DestroyWindowAndRenderer();
				InitializeWindowAndRenderer();

				//m_GameContext.renderer->ReloadShaders(m_GameContext);

				TestScene* pDefaultScene = new TestScene(m_GameContext);
				m_SceneManager->AddScene(pDefaultScene, m_GameContext);

				m_GameContext.renderer->PostInitialize();
			}

			m_GameContext.camera->Update(m_GameContext);
			static constexpr int clearFlags = (int)Renderer::ClearFlag::COLOR | (int)Renderer::ClearFlag::DEPTH | (int)Renderer::ClearFlag::STENCIL;
			m_GameContext.renderer->Clear(clearFlags, m_GameContext);
			m_SceneManager->UpdateAndRender(m_GameContext);
			m_GameContext.inputManager->Update();
			m_GameContext.window->Update(m_GameContext);
			m_GameContext.inputManager->PostUpdate();

			bool windowOpen = true;
			ImGui::Begin("", &windowOpen);
			{
				const std::string rendStr("Current renderer: " + m_RendererName);
				ImGui::Text(rendStr.c_str());
				if (ImGui::CollapsingHeader("Scene info"))
				{
					const std::string sceneCountStr("Scene count: " + std::to_string(m_SceneManager->GetSceneCount()));
					ImGui::Text(sceneCountStr.c_str());
					const std::string currentSceneStr("Current scene: " + m_SceneManager->CurrentScene()->GetName());
					ImGui::Text(currentSceneStr.c_str());
					const glm::uint objectCount = m_GameContext.renderer->GetRenderObjectCount();
					const glm::uint objectCapacity = m_GameContext.renderer->GetRenderObjectCapacity();
					const std::string objectCountStr("Object count/capacity: " + std::to_string(objectCount) + "/" + std::to_string(objectCapacity));
					ImGui::Text(objectCountStr.c_str());

					if (ImGui::TreeNode("Render Objects"))
					{
						std::vector<Renderer::RenderObjectInfo> renderObjectInfos;
						m_GameContext.renderer->GetRenderObjectInfos(renderObjectInfos);
						Logger::Assert(renderObjectInfos.size() == objectCount);
						for (size_t i = 0; i < objectCount; ++i)
						{
							const std::string objectName(renderObjectInfos[i].name + "##" + std::to_string(i));
							if (ImGui::TreeNode(objectName.c_str()))
							{
								ImGui::Text("Transform");

								ImGui::DragFloat3("Translation", &renderObjectInfos[i].transform->position.x, 0.1f);
								glm::vec3 rot = glm::eulerAngles(renderObjectInfos[i].transform->rotation);
								ImGui::DragFloat3("Rotation", &rot.x, 0.01f);
								renderObjectInfos[i].transform->rotation = glm::quat(rot);
								ImGui::DragFloat3("Scale", &renderObjectInfos[i].transform->scale.x, 0.01f);

								ImGui::TreePop();
							}
						}

						ImGui::TreePop();
					}

					if (ImGui::TreeNode("Lights"))
					{
						ImGui::AlignFirstTextHeightToWidgets();


						ImGuiColorEditFlags colorEditFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_RGB | ImGuiColorEditFlags_PickerHueWheel;

						Renderer::DirectionalLight& dirLight = m_GameContext.renderer->GetDirectionalLight(0);
						std::vector<Renderer::PointLight>& pointLights = m_GameContext.renderer->GetAllPointLights();

						ImGui::Checkbox("##dir-light-enabled", &dirLight.enabled);
						ImGui::SameLine();
						if (ImGui::TreeNode("Directional Light"))
						{
							CopyableColorEdit3("Diffuse ", dirLight.diffuseCol, "c##diffuse", "p##diffuse", colorEditFlags);
							CopyableColorEdit3("Specular", dirLight.specularCol, "c##specular", "p##specular", colorEditFlags);
							CopyableColorEdit3("Ambient ", dirLight.ambientCol, "c##ambient", "p##ambient", colorEditFlags);

							ImGui::TreePop();
						}

						for (size_t i = 0; i < pointLights.size(); ++i)
						{

							const std::string iStr = std::to_string(i);
							const std::string objectName("Point Light##" + iStr);

							ImGui::Checkbox(std::string("##enabled" + iStr).c_str(), &pointLights[i].enabled);
							ImGui::SameLine();
							if (ImGui::TreeNode(objectName.c_str()))
							{
								CopyableColorEdit3("Diffuse ", pointLights[i].diffuseCol, "c##diffuse", "p##diffuse", colorEditFlags);
								CopyableColorEdit3("Specular", pointLights[i].specularCol, "c##specular", "p##specular", colorEditFlags);
								CopyableColorEdit3("Ambient ", pointLights[i].ambientCol, "c##ambient", "p##ambient", colorEditFlags);

								ImGui::PushItemWidth(150);
								ImGui::SliderFloat("Linear", &pointLights[i].linear, 0.0014f, 0.7f);
								ImGui::SameLine();
								ImGui::SliderFloat("Quadratic", &pointLights[i].quadratic, 0.000007f, 1.8f);
								ImGui::PopItemWidth();

								ImGui::TreePop();
							}
						}

						ImGui::TreePop();
					}
				}
			}
			ImGui::End();

			static bool open = true;
			ImGui::ShowTestWindow(&open);

			m_GameContext.renderer->Update(m_GameContext);

			// Call as late in the frame as possible
			m_GameContext.renderer->ImGui_Render();

			m_GameContext.renderer->Draw(m_GameContext);

			m_GameContext.renderer->SwapBuffers(m_GameContext);
		}
	}

	void FlexEngine::CopyableColorEdit3(const char* label, glm::vec3& col, const char* copyBtnLabel, const char* pasteBtnLabel, ImGuiColorEditFlags flags)
	{
		ImGui::ColorEdit3(label, &col.r, flags);
		ImGui::SameLine(); if (ImGui::Button(copyBtnLabel)) CopyColorToClipboard(col);
		ImGui::SameLine(); if (ImGui::Button(pasteBtnLabel)) col = PasteColor3FromClipboard();
	}

	void FlexEngine::Stop()
	{
		m_Running = false;
	}
} // namespace flex
