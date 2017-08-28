#include "stdafx.h"

#include "FlexEngine.h"

#include <imgui.h>

#include "FreeCamera.h"
#include "Logger.h"
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

#if COMPILE_D3D
		++m_RendererCount;
		if (m_RendererIndex == RendererID::_LAST_ELEMENT || preferredInitialRenderer == RendererID::D3D)
		{
			m_RendererIndex = RendererID::D3D;
		}
#endif
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

		ImGuiStyle& imGuiStyle = ImGui::GetStyle();
		imGuiStyle.Colors[ImGuiCol_TitleBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.6f);
		imGuiStyle.Colors[ImGuiCol_TitleBgActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
		imGuiStyle.Colors[ImGuiCol_MenuBarBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
		imGuiStyle.Colors[ImGuiCol_Header] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
		imGuiStyle.Colors[ImGuiCol_CheckMark] = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
	}

	void FlexEngine::Destroy()
	{
		m_GameContext.renderer->ImGui_Shutdown();

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
#if COMPILE_D3D
		if (m_RendererIndex == RendererID::D3D)
		{
			D3DWindowWrapper* d3dWindow = new D3DWindowWrapper("Flex Engine - Direct3D", windowSize, windowPos, m_GameContext);
			m_Window = d3dWindow;
			D3DRenderer* d3dRenderer = new D3DRenderer(m_GameContext);
			m_GameContext.renderer = d3dRenderer;
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
		case RendererID::D3D: return "D3D";
		case RendererID::GL: return "Open GL";
		case RendererID::_LAST_ELEMENT:  // Fallthrough
		default:
			return "Unknown";
		}
	}

	void FlexEngine::CycleRenderer()
	{
		m_SceneManager->RemoveScene(m_SceneManager->CurrentScene(), m_GameContext);
		DestroyWindowAndRenderer();

		while (true)
		{
			m_RendererIndex = RendererID(((int)m_RendererIndex + 1) % (int)RendererID::_LAST_ELEMENT);

#if COMPILE_VULKAN
			if (m_RendererIndex == RendererID::VULKAN) break;
#endif
#if COMPILE_D3D
			if (m_RendererIndex == RendererID::D3D) break;
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
				if (ImGui::TreeNode("Scene info"))
				{
					const std::string sceneCountStr("Scene count: " + std::to_string(m_SceneManager->GetSceneCount()));
					ImGui::Text(sceneCountStr.c_str());
					const std::string currentSceneStr("Current scene: " + m_SceneManager->CurrentScene()->GetName());
					ImGui::Text(currentSceneStr.c_str());
					const glm::uint objectCount = m_GameContext.renderer->GetRenderObjectCount();
					const glm::uint objectCapacity = m_GameContext.renderer->GetRenderObjectCapacity();
					const std::string objectCountStr("Object count/capacity: " + std::to_string(objectCount) + "/" + std::to_string(objectCapacity));
					ImGui::Text(objectCountStr.c_str());

					if (ImGui::TreeNode("Objects"))
					{
						std::vector<Renderer::RenderObjectInfo> renderObjectInfos;
						m_GameContext.renderer->GetRenderObjectInfos(renderObjectInfos);
						Logger::Assert(renderObjectInfos.size() == objectCount);
						for (size_t i = 0; i < objectCount; ++i)
						{
							const std::string objectName(renderObjectInfos[i].name + "##" + std::to_string(i));
							if (ImGui::TreeNode(objectName.c_str()))
							{
								const std::string materialName("Mat: " + renderObjectInfos[i].materialName);
								ImGui::Text(materialName.c_str());
								ImGui::TreePop();
							}
						}

						ImGui::TreePop();
					}

					ImGui::TreePop();
				}
			}
			ImGui::End();

			//static bool open = true;
			//ImGui::ShowTestWindow(&open);

			m_GameContext.renderer->Update(m_GameContext);

			// Call as late in the frame as possible
			m_GameContext.renderer->ImGui_Render();

			m_GameContext.renderer->Draw(m_GameContext);

			m_GameContext.renderer->SwapBuffers(m_GameContext);
		}
	}

	void FlexEngine::Stop()
	{
		m_Running = false;
	}
} // namespace flex
