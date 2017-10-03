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
		// Materials
		//Renderer::MaterialCreateInfo skyboxMatInfo = {};
		//skyboxMatInfo.name = "Skybox";
		//skyboxMatInfo.shaderName = "skybox";

		//const std::string directory = RESOURCE_LOCATION + "textures/skyboxes/ame_starfield/";
		//const std::string fileName = "ame_starfield";
		//const std::string extension = ".tga";
		//
		//skyboxMatInfo.cubeMapFilePaths = {
		//	directory + fileName + "_r" + extension,
		//	directory + fileName + "_l" + extension,
		//	directory + fileName + "_u" + extension,
		//	directory + fileName + "_d" + extension,
		//	directory + fileName + "_b" + extension,
		//	directory + fileName + "_f" + extension,
		//};
		//const MaterialID skyboxMatID = gameContext.renderer->InitializeMaterial(gameContext, &skyboxMatInfo);


		Renderer::MaterialCreateInfo skyboxHDRMatInfo = {};
		skyboxHDRMatInfo.name = "HDR Skybox";
		skyboxHDRMatInfo.shaderName = "background"; // renders once using equirectangular shader to generate cubemap, then every frame using background shader
		skyboxHDRMatInfo.useCubemapSampler = true;

		const MaterialID skyboxHDRMatID = gameContext.renderer->InitializeMaterial(gameContext, &skyboxHDRMatInfo);


		//Renderer::MaterialCreateInfo pbrMatTexturedInfo = {};
		//pbrMatTexturedInfo.shaderID = 3;
		//pbrMatTexturedInfo.name = "PBR textured";
		//pbrMatTexturedInfo.useAlbedoSampler = true;
		//pbrMatTexturedInfo.albedoTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_basecolor.png";
		//pbrMatTexturedInfo.useMetallicSampler= true;
		//pbrMatTexturedInfo.metallicTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_metallic.png";
		//pbrMatTexturedInfo.useRoughnessSampler = true;
		//pbrMatTexturedInfo.roughnessTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_roughness.png";
		//pbrMatTexturedInfo.useAOSampler = true;
		//pbrMatTexturedInfo.aoTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_ao.png";
		//pbrMatTexturedInfo.normalTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_normal.png";
		//const MaterialID pbrMatTexturedID = gameContext.renderer->InitializeMaterial(gameContext, &pbrMatTexturedInfo);


		Renderer::MaterialCreateInfo arisakaMatInfo = {};
		arisakaMatInfo.shaderName = "pbr";
		arisakaMatInfo.name = "PBR textured";
		arisakaMatInfo.useAlbedoSampler = true;
		arisakaMatInfo.albedoTexturePath = RESOURCE_LOCATION + "textures/arisaka/arisaka_base_color_1.png";
		arisakaMatInfo.useMetallicSampler= true;
		arisakaMatInfo.metallicTexturePath = RESOURCE_LOCATION + "textures/arisaka/arisaka_metalness_1.png";
		arisakaMatInfo.useRoughnessSampler = true;
		arisakaMatInfo.roughnessTexturePath = RESOURCE_LOCATION + "textures/arisaka/arisaka_roughness_1.png";
		arisakaMatInfo.useAOSampler = false;
		arisakaMatInfo.constAO = 1.0f;
		arisakaMatInfo.normalTexturePath = RESOURCE_LOCATION + "textures/arisaka/arisaka_normal_1.png";
		const MaterialID arisakaMatID = gameContext.renderer->InitializeMaterial(gameContext, &arisakaMatInfo);


		const int sphereCountX = 7;
		const int sphereCountY = 7;
		const float sphereSpacing = 2.5f;
		const glm::vec3 offset = glm::vec3(-sphereCountX / 2 * sphereSpacing, -sphereCountY / 2 * sphereSpacing, 0.0f);
		const size_t sphereCount = sphereCountX * sphereCountY;
		m_Spheres.resize(sphereCount);
		for (size_t i = 0; i < sphereCount; ++i)
		{
			int x = i % sphereCountX;
			int y = int(i / sphereCountY);
		
			const std::string iStr = std::to_string(i);
		
			Renderer::MaterialCreateInfo pbrMatInfo = {};
			pbrMatInfo.shaderName = "pbr";
			pbrMatInfo.name = "PBR simple " + iStr;
			pbrMatInfo.constAlbedo = glm::vec3(0.1f, 0.1f, 0.5f);
			pbrMatInfo.constMetallic = float(x) / (sphereCountX - 1);
			pbrMatInfo.constRoughness = glm::clamp(float(y) / (sphereCountY - 1), 0.05f, 1.0f);
			pbrMatInfo.constAO = 1.0f;
			const MaterialID pbrMatID = gameContext.renderer->InitializeMaterial(gameContext, &pbrMatInfo);
		
		
			m_Spheres[i] = new MeshPrefab(pbrMatID, "PBR sphere " + iStr);
			m_Spheres[i]->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/sphere.fbx", true, true);
			m_Spheres[i]->GetTransform().position = offset + glm::vec3(x * sphereSpacing, y * sphereSpacing, 0.0f);
			AddChild(gameContext, m_Spheres[i]);
		}
		
		//m_Arisaka = new MeshPrefab(arisakaMatID, "Arisaka Type 99");
		//m_Arisaka->SetUVScale(2.0f, 1.0f);
		//m_Arisaka->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/Arisaka_Type_99_gun_low.fbx", true, true);
		//m_Arisaka->GetTransform().Translate({ 0, 0, -10.0f });
		//m_Arisaka->GetTransform().Rotate({ glm::half_pi<float>(), 0, glm::pi<float>() });
		//AddChild(m_Arisaka);

		
		m_Skybox = new MeshPrefab(skyboxHDRMatID);
		m_Skybox->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::SKYBOX);
		AddChild(gameContext, m_Skybox);


		// Lights
		Renderer::PointLight light1 = {};
		light1.position = glm::vec4(-6.0f, -6.0f, -8.0f, 0.0f);
		light1.color = glm::vec4(300.0f, 300.0f, 300.0f, 0.0f);
		gameContext.renderer->InitializePointLight(light1);

		Renderer::PointLight light2 = {};
		light2.position = glm::vec4(-6.0f, 6.0f, -8.0f, 0.0f);
		light2.color = glm::vec4(300.0f, 300.0f, 300.0f, 0.0f);
		gameContext.renderer->InitializePointLight(light2);

		Renderer::PointLight light3 = {};
		light3.position = glm::vec4(6.0f, 6.0f, -8.0f, 0.0f);
		light3.color = glm::vec4(300.0f, 300.0f, 300.0f, 0.0f);
		gameContext.renderer->InitializePointLight(light3);

		Renderer::PointLight light4 = {};
		light4.position = glm::vec4(6.0f, -6.0f, -8.0f, 0.0f);
		light4.color = glm::vec4(300.0f, 300.0f, 300.0f, 0.0f);
		gameContext.renderer->InitializePointLight(light4);

	}

	void Scene_02::Destroy(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
	}

	void Scene_02::Update(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
	}
} // namespace flex
