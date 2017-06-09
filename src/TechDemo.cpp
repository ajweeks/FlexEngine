#include "stdafx.h"

#include "TechDemo.h"
#include "Logger.h"
#include "FreeCamera.h"
#include "GameContext.h"
#include "InputManager.h"
#include "Scene/SceneManager.h"
#include "Scene/TestScene.h"
#include "Typedefs.h"

using namespace glm;

TechDemo::TechDemo() :
	m_RendererIndex(RendererID::GL),
	m_RendererCount(3),
	m_ClearColor(0.08f, 0.13f, 0.2f),
	m_VSyncEnabled(false)
{
}

TechDemo::~TechDemo()
{
	Destroy();
}

void TechDemo::Initialize()
{
	m_GameContext = {};
	m_GameContext.mainApp = this;

	InitializeWindowAndRenderer();

	m_SceneManager = new SceneManager();
	TestScene* pDefaultScene = new TestScene(m_GameContext);
	m_SceneManager->AddScene(pDefaultScene);
	pDefaultScene->Initialize(m_GameContext);

	m_DefaultCamera = new FreeCamera(m_GameContext);
	m_DefaultCamera->SetPosition(vec3(0.0f, 0.0f, -8.0f));
	m_GameContext.camera = m_DefaultCamera;

	m_GameContext.inputManager = new InputManager();

	m_GameContext.renderer->PostInitialize();
}

void TechDemo::Destroy()
{
	if (m_SceneManager) m_SceneManager->DestroyAllScenes(m_GameContext);
	SafeDelete(m_SceneManager);
	SafeDelete(m_GameContext.inputManager);
	SafeDelete(m_DefaultCamera);
	
	DestroyWindowAndRenderer();
}

void TechDemo::InitializeWindowAndRenderer()
{
	const vec2i windowSize = vec2i(1920, 1080);
	vec2i windowPos = vec2i(300, 300);

#if COMPILE_VULKAN
	if (m_RendererIndex == RendererID::VULKAN)
	{
		VulkanWindowWrapper* vulkanWindow = new VulkanWindowWrapper("Tech Demo - Vulkan", windowSize, windowPos, m_GameContext);
		m_Window = vulkanWindow;
		VulkanRenderer* vulkanRenderer = new VulkanRenderer(m_GameContext);
		m_GameContext.renderer = vulkanRenderer;
	}
#endif
#if COMPILE_OPEN_GL
	if (m_RendererIndex == RendererID::GL)
	{
		GLWindowWrapper* glWindow = new GLWindowWrapper("Tech Demo - OpenGL", windowSize, windowPos, m_GameContext);
		m_Window = glWindow;
		GLRenderer* glRenderer = new GLRenderer(m_GameContext);
		m_GameContext.renderer = glRenderer;
	}
#endif
#if COMPILE_D3D
	if (m_RendererIndex == RendererID::D3D)
	{
		D3DWindowWrapper* d3dWindow = new D3DWindowWrapper("Tech Demo - Direct3D", windowSize, windowPos, m_GameContext);
		m_Window = d3dWindow;
		D3DRenderer* d3dRenderer = new D3DRenderer(m_GameContext);
		m_GameContext.renderer = d3dRenderer;
	}
#endif

	m_Window->SetUpdateWindowTitleFrequency(0.4f);

	m_GameContext.renderer->SetVSyncEnabled(m_VSyncEnabled);
	m_GameContext.renderer->SetClearColor(m_ClearColor.r, m_ClearColor.g, m_ClearColor.b);
}

void TechDemo::DestroyWindowAndRenderer()
{
	SafeDelete(m_Window);
	SafeDelete(m_GameContext.renderer);
}

void TechDemo::CycleRenderer()
{
	m_SceneManager->RemoveScene(m_SceneManager->CurrentScene(), m_GameContext);
	DestroyWindowAndRenderer();

	m_RendererIndex = RendererID(((int)m_RendererIndex + 1) % m_RendererCount);

	InitializeWindowAndRenderer();

	TestScene* pDefaultScene = new TestScene(m_GameContext);
	m_SceneManager->AddScene(pDefaultScene);
	pDefaultScene->Initialize(m_GameContext);

	m_GameContext.renderer->PostInitialize();
}

void TechDemo::UpdateAndRender()
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
	
		m_GameContext.inputManager->Update();
		m_GameContext.camera->Update(m_GameContext);
		m_GameContext.renderer->Clear((int)Renderer::ClearFlag::COLOR | (int)Renderer::ClearFlag::DEPTH | (int)Renderer::ClearFlag::STENCIL, m_GameContext);
	
		m_GameContext.window->Update(m_GameContext);
	
		m_SceneManager->UpdateAndRender(m_GameContext);

		if (m_GameContext.inputManager->GetKeyDown(InputManager::KeyCode::KEY_T))
		{
			m_GameContext.inputManager->ClearAllInputs(m_GameContext);
			CycleRenderer();
			continue;
		}
	
		m_GameContext.inputManager->PostUpdate();
	
		m_GameContext.renderer->SwapBuffers(m_GameContext);
	}
}

void TechDemo::Stop()
{
	m_Running = false;
}
