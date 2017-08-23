#include "stdafx.h"

#include "Scene/Scenes/Scene_02.h"

#include <glm/vec3.hpp>

namespace flex
{
	Scene_02::Scene_02(const GameContext& gameContext) :
		BaseScene("Scene_02")
	{
		UNREFERENCED_PARAMETER(gameContext);
	}

	Scene_02::~Scene_02()
	{
	}

	void Scene_02::Initialize(const GameContext& gameContext)
	{
		m_Grid = new MeshPrefab();
		m_Grid->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::GRID);
		m_Grid->GetTransform().position.y -= 0.05f;
		AddChild(m_Grid);

		m_Teapot = new MeshPrefab();
		m_Teapot->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/teapot.fbx");
		m_Teapot->SetTransform(glm::vec3(-5.0f, 0.0f, 0.0f));
		AddChild(m_Teapot);

		m_Orb = new MeshPrefab();
		m_Orb->SetTextureFilePaths("textures/work_s.jpg", "textures/work_n.jpg", "textures/work_s.jpg");
		m_Orb->SetUsedTextures(false, false, false);
		m_Orb->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/pedestal-object.fbx");
		m_Orb->SetTransform(glm::vec3(5.0f, 0.0f, 0.0f));
		AddChild(m_Orb);

		m_Skybox = new MeshPrefab();
		m_Skybox->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::SKYBOX);
		AddChild(m_Skybox);

		Renderer::SceneInfo& sceneInfo = gameContext.renderer->GetSceneInfo();
		sceneInfo.m_AmbientColor = glm::vec4(0.02f, 0.03f, 0.025f, 1.0f);
		sceneInfo.m_SpecularColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	void Scene_02::Destroy(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
	}

	void Scene_02::Update(const GameContext& gameContext)
	{
		Renderer::SceneInfo& sceneInfo = gameContext.renderer->GetSceneInfo();
		sceneInfo.m_LightDir = glm::vec4(sin(gameContext.elapsedTime), -0.5f, cos(gameContext.elapsedTime * 0.8f) * 0.5f, 0.0f);
	}
} // namespace flex
