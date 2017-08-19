#include "stdafx.h"

#include "FlexEngine.h"

#include "FreeCamera.h"
#include "Logger.h"
#include "Scene/SceneManager.h"
#include "Scene/TestScene.h"
#include "Typedefs.h"

FlexEngine::FlexEngine() :
	m_ClearColor(0.08f, 0.13f, 0.2f),
	m_VSyncEnabled(false)
{
	RendererID preferredInitialRenderer = RendererID::VULKAN;

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

	Logger::LogInfo(std::to_string(m_RendererCount) + " renderers enabled");
	Logger::LogInfo("Current renderer: " + RenderIDToString(m_RendererIndex));
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
	TestScene* pDefaultScene = new TestScene(m_GameContext);
	m_SceneManager->AddScene(pDefaultScene, m_GameContext);

	m_DefaultCamera = new FreeCamera(m_GameContext);
	m_DefaultCamera->SetPosition(glm::vec3(0.0f, 5.0f, -15.0f));
	m_GameContext.camera = m_DefaultCamera;

	m_GameContext.inputManager = new InputManager();

	m_GameContext.renderer->PostInitialize();
}

void FlexEngine::Destroy()
{
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
		VulkanWindowWrapper* vulkanWindow = new VulkanWindowWrapper("Flex Engine - Vulkan", windowSize, windowPos, m_GameContext);
		m_Window = vulkanWindow;
		VulkanRenderer* vulkanRenderer = new VulkanRenderer(m_GameContext);
		m_GameContext.renderer = vulkanRenderer;
	}
#endif
#if COMPILE_OPEN_GL
	if (m_RendererIndex == RendererID::GL)
	{
		GLWindowWrapper* glWindow = new GLWindowWrapper("Flex Engine - OpenGL", windowSize, windowPos, m_GameContext);
		m_Window = glWindow;
		GLRenderer* glRenderer = new GLRenderer(m_GameContext);
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
	Logger::LogInfo("Current renderer: " + RenderIDToString(m_RendererIndex));

	InitializeWindowAndRenderer();

	TestScene* pDefaultScene = new TestScene(m_GameContext);
	m_SceneManager->AddScene(pDefaultScene, m_GameContext);

	m_GameContext.renderer->PostInitialize();
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
		m_GameContext.renderer->Clear((int)Renderer::ClearFlag::COLOR | (int)Renderer::ClearFlag::DEPTH | (int)Renderer::ClearFlag::STENCIL, m_GameContext);
		m_SceneManager->UpdateAndRender(m_GameContext);
		m_GameContext.inputManager->Update();
		m_GameContext.window->Update(m_GameContext);
		m_GameContext.inputManager->PostUpdate();

		m_GameContext.renderer->Update(m_GameContext);
		m_GameContext.renderer->Draw(m_GameContext);
		m_GameContext.renderer->SwapBuffers(m_GameContext);
	}
}

void FlexEngine::Stop()
{
	m_Running = false;
}
