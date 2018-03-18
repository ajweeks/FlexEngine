#include "stdafx.hpp"

#include "Scene/Scenes/TestScene.hpp"

#include <glm/vec3.hpp>

#include "Scene/ReflectionProbe.hpp"

namespace flex
{
	TestScene::TestScene(const GameContext& gameContext) :
		BaseScene("TestScene")
	{
		UNREFERENCED_PARAMETER(gameContext);
	}

	TestScene::~TestScene()
	{
	}

	void TestScene::Initialize(const GameContext& gameContext)
	{
		// Lights
		Renderer::PointLight light1 = {};
		light1.position = glm::vec4(-6.0f, -6.0f, -8.0f, 0.0f);
		light1.color = glm::vec4(300.0f, 300.0f, 300.0f, 0.0f);
		m_PointLight1ID = gameContext.renderer->InitializePointLight(light1);

		Renderer::PointLight light2 = {};
		light2.position = glm::vec4(-6.0f, 6.0f, -8.0f, 0.0f);
		light2.color = glm::vec4(10.0f, 300.0f, 10.0f, 0.0f);
		m_PointLight2ID = gameContext.renderer->InitializePointLight(light2);

		Renderer::PointLight light3 = {};
		light3.position = glm::vec4(6.0f, 6.0f, -8.0f, 0.0f);
		light3.color = glm::vec4(300.0f, 10.0f, 10.0f, 0.0f);
		m_PointLight3ID = gameContext.renderer->InitializePointLight(light3);

		Renderer::PointLight light4 = {};
		light4.position = glm::vec4(6.0f, -6.0f, -8.0f, 0.0f);
		light4.color = glm::vec4(40.0f, 10.0f, 300.0f, 0.0f);
		m_PointLight4ID = gameContext.renderer->InitializePointLight(light4);

		// Materials
		Renderer::MaterialCreateInfo skyboxHDRMatInfo = {};
		skyboxHDRMatInfo.name = "HDR Skybox";
		skyboxHDRMatInfo.shaderName = "background";
		skyboxHDRMatInfo.generateHDRCubemapSampler = true;
		skyboxHDRMatInfo.enableCubemapSampler = true;
		skyboxHDRMatInfo.enableCubemapTrilinearFiltering = true;
		skyboxHDRMatInfo.generatedCubemapSize = { 512, 512 };
		skyboxHDRMatInfo.generateIrradianceSampler = true;
		skyboxHDRMatInfo.generatedIrradianceCubemapSize = { 32, 32 };
		skyboxHDRMatInfo.generatePrefilteredMap = true;
		skyboxHDRMatInfo.generatedPrefilteredCubemapSize = { 128, 128 };
		const MaterialID skyboxHDRMatID = gameContext.renderer->InitializeMaterial(gameContext, &skyboxHDRMatInfo);

		//Renderer::MaterialCreateInfo pbrMatInfo = {};
		//pbrMatInfo.shaderName = "pbr";
		//pbrMatInfo.name = "PBR";
		//pbrMatInfo.constAlbedo = glm::vec3(0.95f, 0.25f, 0.35f);
		//pbrMatInfo.constMetallic = 0.0f;
		//pbrMatInfo.constRoughness = 0.5f;
		//pbrMatInfo.constAO = 1.0f;
		//MaterialID pbrMatID = gameContext.renderer->InitializeMaterial(gameContext, &pbrMatInfo);

		//MeshPrefab* sponzaMesh = new MeshPrefab(pbrMatID, "Sponza");
		//sponzaMesh->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/sponza/sponza-optimized.fbx", true, true);
		//sponzaMesh->GetTransform().Scale(0.01f);
		////sponzaMesh->GetTransform().Rotate(0.0f, glm::radians(180.0f), 0.0f);
		//AddChild(gameContext, sponzaMesh);

		MeshPrefab* skybox = new MeshPrefab(skyboxHDRMatID, "Skybox");
		skybox->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::SKYBOX);
		AddChild(gameContext, skybox);

		gameContext.renderer->SetSkyboxMaterial(skyboxHDRMatID);

		// Reflection probes
		// Generate last so it can use generated skybox maps
		m_ReflectionProbe = new ReflectionProbe();
		AddChild(gameContext, m_ReflectionProbe);
	}

	void TestScene::PostInitialize(const GameContext& gameContext)
	{
		m_ReflectionProbe->GetTransform().Translate(0.0f, 10.0f, 0.0f);
		m_ReflectionProbe->GetTransform().Scale(3.5f);
		UNREFERENCED_PARAMETER(gameContext);
	}

	void TestScene::Destroy(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
	}

	void TestScene::Update(const GameContext& gameContext)
	{
		Renderer::PointLight& light1 = gameContext.renderer->GetPointLight(m_PointLight1ID);
		light1.position.x = (cos(gameContext.elapsedTime)) * 12.0f;
		light1.position.y = (cos(gameContext.elapsedTime * 0.6f + 1.5f) * 0.4f + 0.6f) * 10.0f;
		light1.position.z = (sin(gameContext.elapsedTime)) * 20.0f;

		Renderer::PointLight& light2 = gameContext.renderer->GetPointLight(m_PointLight2ID);
		light2.position.x = (sin(gameContext.elapsedTime + PI)) * 12.0f - 15.0f;
		light2.position.y = (cos(gameContext.elapsedTime * 0.23f + 2.51f) * 0.4f + 0.6f) * 5.0f;
		light2.position.z = (cos(gameContext.elapsedTime + PI)) * 12.0f + 15.0f;

		Renderer::PointLight& light3 = gameContext.renderer->GetPointLight(m_PointLight3ID);
		light3.position.x = (cos(gameContext.elapsedTime)) * 5.0f;
		light3.position.y = (cos(gameContext.elapsedTime * 2.56f + 1.2f) * 0.4f + 0.6f) * 12.0f;
		light3.position.z = (sin(gameContext.elapsedTime)) * 25.0f;

		Renderer::PointLight& light4 = gameContext.renderer->GetPointLight(m_PointLight4ID);
		light4.position.x = (sin(gameContext.elapsedTime + glm::three_over_two_pi<real>())) * 10.0f - 5.0f;
		light4.position.y = (cos(gameContext.elapsedTime * 1.1f + 3.6f) * 0.4f + 0.6f) * 10.0f;
		light4.position.z = (cos(gameContext.elapsedTime + glm::three_over_two_pi<real>())) * 10.0f + 5.0f;
	}
} // namespace flex
