#include "stdafx.hpp"

#include "Scene/Scenes/Scene_02.hpp"

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
		//
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



		// TODO: Specify "need" variables in renderers (when setting up uniforms) rather than here



		Renderer::MaterialCreateInfo skyboxHDRMatInfo = {};
		skyboxHDRMatInfo.name = "HDR Skybox";
		skyboxHDRMatInfo.shaderName = "background";
		skyboxHDRMatInfo.needCubemapSampler = true;
		skyboxHDRMatInfo.enableCubemapSampler = true;
		skyboxHDRMatInfo.generateCubemapSampler = true;
		skyboxHDRMatInfo.generatedCubemapSize = { 512, 512 };
		skyboxHDRMatInfo.enableCubemapTrilinearFiltering = true;
		skyboxHDRMatInfo.generateIrradianceSampler = true;
		skyboxHDRMatInfo.generatedIrradianceCubemapSize = { 32, 32 };
		skyboxHDRMatInfo.generatePrefilteredMap = true;
		skyboxHDRMatInfo.generatedPrefilterCubemapSize = { 128, 128 };
		skyboxHDRMatInfo.generateBRDFLUT = true;
		skyboxHDRMatInfo.brdfLUTSize = { 512, 512 };
		const MaterialID skyboxHDRMatID = gameContext.renderer->InitializeMaterial(gameContext, &skyboxHDRMatInfo);


		Renderer::MaterialCreateInfo cerebusMatTexturedInfo = {};
		cerebusMatTexturedInfo.name = "Cerebus";
		cerebusMatTexturedInfo.shaderName = "pbr";
		cerebusMatTexturedInfo.needAlbedoSampler = true;
		cerebusMatTexturedInfo.enableAlbedoSampler = true;
		cerebusMatTexturedInfo.albedoTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_A.tga";
		cerebusMatTexturedInfo.needMetallicSampler = true;
		cerebusMatTexturedInfo.enableMetallicSampler = true;
		cerebusMatTexturedInfo.metallicTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_M.tga";
		cerebusMatTexturedInfo.needRoughnessSampler = true;
		cerebusMatTexturedInfo.enableRoughnessSampler = true;
		cerebusMatTexturedInfo.roughnessTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_R.tga";
		cerebusMatTexturedInfo.needAOSampler = true;
		cerebusMatTexturedInfo.enableAOSampler = true;
		cerebusMatTexturedInfo.aoTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Raw/Cerberus_AO.tga";
		cerebusMatTexturedInfo.normalTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_N.tga";
		cerebusMatTexturedInfo.generateIrradianceSampler = false; // Don't generate irradiance sampler, just 
		cerebusMatTexturedInfo.needIrradianceSampler = true;		  // use skybox's irradiance sampler
		cerebusMatTexturedInfo.enableIrradianceSampler = true;		  // use skybox's irradiance sampler
		cerebusMatTexturedInfo.irradianceSamplerMatID = skyboxHDRMatID; // here
		cerebusMatTexturedInfo.needPrefilteredMap = true;
		cerebusMatTexturedInfo.enablePrefilteredMap = true;
		cerebusMatTexturedInfo.prefilterMapSamplerMatID = skyboxHDRMatID;
		cerebusMatTexturedInfo.needBRDFLUT = true;
		cerebusMatTexturedInfo.enableBRDFLUT = true;
		cerebusMatTexturedInfo.brdfLUTSamplerMatID = skyboxHDRMatID;
		const MaterialID cerebusMatID = gameContext.renderer->InitializeMaterial(gameContext, &cerebusMatTexturedInfo);


		Renderer::MaterialCreateInfo pbrMatTexturedInfo = {};
		pbrMatTexturedInfo.shaderName = "pbr";
		pbrMatTexturedInfo.name = "PBR textured";
		pbrMatTexturedInfo.needAlbedoSampler = true;
		pbrMatTexturedInfo.enableAlbedoSampler = true;
		pbrMatTexturedInfo.albedoTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_basecolor.png";
		pbrMatTexturedInfo.needMetallicSampler = true;
		pbrMatTexturedInfo.enableMetallicSampler = true;
		pbrMatTexturedInfo.metallicTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_metallic.png";
		pbrMatTexturedInfo.needRoughnessSampler = true;
		pbrMatTexturedInfo.enableRoughnessSampler = true;
		pbrMatTexturedInfo.roughnessTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_roughness.png";
		pbrMatTexturedInfo.needAOSampler = true;
		pbrMatTexturedInfo.enableAOSampler = true;
		pbrMatTexturedInfo.aoTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_basecolor.png";
		pbrMatTexturedInfo.normalTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_normal.png";
		pbrMatTexturedInfo.generateIrradianceSampler = false; // Don't generate irradiance sampler, just 
		pbrMatTexturedInfo.needIrradianceSampler = true;		  // use skybox's irradiance sampler
		pbrMatTexturedInfo.enableIrradianceSampler = true;		  // use skybox's irradiance sampler
		pbrMatTexturedInfo.irradianceSamplerMatID = skyboxHDRMatID; // here
		pbrMatTexturedInfo.needPrefilteredMap = true;
		pbrMatTexturedInfo.enablePrefilteredMap = true;
		pbrMatTexturedInfo.prefilterMapSamplerMatID = skyboxHDRMatID;
		pbrMatTexturedInfo.needBRDFLUT = true;
		pbrMatTexturedInfo.enableBRDFLUT = true;
		pbrMatTexturedInfo.brdfLUTSamplerMatID = skyboxHDRMatID;
		const MaterialID pbrMatTexturedID = gameContext.renderer->InitializeMaterial(gameContext, &pbrMatTexturedInfo);


		//Renderer::MaterialCreateInfo arisakaMatInfo = {};
		//arisakaMatInfo.shaderName = "pbr";
		//arisakaMatInfo.name = "PBR textured";
		//arisakaMatInfo.useAlbedoSampler = true;
		//arisakaMatInfo.albedoTexturePath = RESOURCE_LOCATION + "textures/arisaka/arisaka_base_color_1.png";
		//arisakaMatInfo.useMetallicSampler= true;
		//arisakaMatInfo.metallicTexturePath = RESOURCE_LOCATION + "textures/arisaka/arisaka_metalness_1.png";
		//arisakaMatInfo.useRoughnessSampler = true;
		//arisakaMatInfo.roughnessTexturePath = RESOURCE_LOCATION + "textures/arisaka/arisaka_roughness_1.png";
		//arisakaMatInfo.useAOSampler = false;
		//arisakaMatInfo.constAO = 1.0f;
		//arisakaMatInfo.normalTexturePath = RESOURCE_LOCATION + "textures/arisaka/arisaka_normal_1.png";
		//const MaterialID arisakaMatID = gameContext.renderer->InitializeMaterial(gameContext, &arisakaMatInfo);


		//Renderer::MaterialCreateInfo brickMatInfo = {};
		//brickMatInfo.shaderName = "deferred_simple";
		//brickMatInfo.name = "Brick";
		//brickMatInfo.diffuseTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_basecolor.png";
		//brickMatInfo.specularTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_metallic.png";
		//brickMatInfo.normalTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_normal.png";
		//const MaterialID brickMatID = gameContext.renderer->InitializeMaterial(gameContext, &brickMatInfo);

		m_Skybox = new MeshPrefab(skyboxHDRMatID, "Skybox");
		m_Skybox->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::SKYBOX);
		AddChild(gameContext, m_Skybox);

		//Renderer::MaterialCreateInfo pbrMatInfo = {};
		//pbrMatInfo.name = "PBR";
		//pbrMatInfo.shaderName = "pbr";
		//pbrMatInfo.constAlbedo = glm::vec3(0.32f, 0.45f, 0.54f);
		//pbrMatInfo.constMetallic = 0.0f;
		//pbrMatInfo.constRoughness = 0.65f;
		//pbrMatInfo.constAO = 1.0f;
		//pbrMatInfo.needIrradianceSampler = true;
		//pbrMatInfo.enableIrradianceSampler = true;
		//pbrMatInfo.irradianceSamplerMatID = skyboxHDRMatID;
		//pbrMatInfo.needPrefilteredMap = true;
		//pbrMatInfo.enablePrefilteredMap = true;
		//pbrMatInfo.prefilterMapSamplerMatID = skyboxHDRMatID;
		//pbrMatInfo.needBRDFLUT = true;
		//pbrMatInfo.enableBRDFLUT = true;
		//pbrMatInfo.brdfLUTSamplerMatID = skyboxHDRMatID;
		//const MaterialID pbrMatID = gameContext.renderer->InitializeMaterial(gameContext, &pbrMatInfo);

		//n_Cerberus = new MeshPrefab(cerebusMatID, "Cerberus");
		//n_Cerberus->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Cerberus_LP_WithB&T.fbx", false, false, false, //true);
		//AddChild(gameContext, n_Cerberus);

		const int sphereCountX = 2;
		const int sphereCountY = 2;
		const float sphereSpacing = 2.5f;
		const glm::vec3 offset = glm::vec3(-sphereCountX / 2 * sphereSpacing, -sphereCountY / 2 * sphereSpacing, 0.0f);
		const size_t sphereCount = sphereCountX * sphereCountY;
		m_Spheres.resize(sphereCount);
		for (size_t i = 0; i < sphereCount; ++i)
		{
			int x = i % sphereCountX;
			int y = int(i / sphereCountY);
		
			const std::string iStr = std::to_string(i);
		
			//Renderer::MaterialCreateInfo pbrMatInfo = {};
			//pbrMatInfo.shaderName = "pbr";
			//pbrMatInfo.name = "PBR simple " + iStr;
			//pbrMatInfo.constAlbedo = glm::vec3(0.175f, 0.12f, 0.8f);
			//pbrMatInfo.constMetallic = float(x) / (sphereCountX - 1);
			//pbrMatInfo.constRoughness = glm::clamp(float(y) / (sphereCountY - 1), 0.05f, 1.0f);
			//pbrMatInfo.constAO = 1.0f;
			//pbrMatInfo.needIrradianceSampler = true;
			//pbrMatInfo.enableIrradianceSampler = true;
			//pbrMatInfo.irradianceSamplerMatID = skyboxHDRMatID;
			//pbrMatInfo.needPrefilteredMap = true;
			//pbrMatInfo.enablePrefilteredMap = true;
			//pbrMatInfo.prefilterMapSamplerMatID = skyboxHDRMatID;
			//pbrMatInfo.needBRDFLUT = true;
			//pbrMatInfo.enableBRDFLUT = true;
			//pbrMatInfo.brdfLUTSamplerMatID = skyboxHDRMatID;
			//const MaterialID pbrMatID = gameContext.renderer->InitializeMaterial(gameContext, &pbrMatInfo);
			
			m_Spheres[i] = new MeshPrefab(pbrMatTexturedID, "Sphere " + iStr);
			m_Spheres[i]->ForceAttributes((glm::uint)VertexAttribute::COLOR_R32G32B32A32_SFLOAT); // Force white
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

	void Scene_02::PostInitialize(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
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
