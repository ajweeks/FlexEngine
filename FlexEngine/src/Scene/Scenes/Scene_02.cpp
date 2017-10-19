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

		// Materials

		Renderer::MaterialCreateInfo skyboxHDRMatInfo = {};
		skyboxHDRMatInfo.name = "HDR Skybox";
		skyboxHDRMatInfo.shaderName = "background";
		skyboxHDRMatInfo.generateHDRCubemapSampler = true;
		skyboxHDRMatInfo.enableHDRCubemapSampler = true;
		skyboxHDRMatInfo.generatedHDRCubemapSize = { 512, 512 };
		skyboxHDRMatInfo.generateIrradianceSampler = true;
		skyboxHDRMatInfo.generatedIrradianceCubemapSize = { 32, 32 };
		skyboxHDRMatInfo.generatePrefilteredMap = true;
		skyboxHDRMatInfo.generatedPrefilteredCubemapSize = { 128, 128 };
		skyboxHDRMatInfo.generateBRDFLUT = true;
		skyboxHDRMatInfo.generatedBRDFLUTSize = { 512, 512 };
		const MaterialID skyboxHDRMatID = gameContext.renderer->InitializeMaterial(gameContext, &skyboxHDRMatInfo);


		Renderer::MaterialCreateInfo cerebusMatTexturedInfo = {};
		cerebusMatTexturedInfo.name = "Cerebus";
		cerebusMatTexturedInfo.shaderName = "pbr";
		cerebusMatTexturedInfo.enableAlbedoSampler = true;
		cerebusMatTexturedInfo.generateAlbedoSampler = true;
		cerebusMatTexturedInfo.albedoTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_A.tga";
		cerebusMatTexturedInfo.enableMetallicSampler = true;
		cerebusMatTexturedInfo.generateMetallicSampler = true;
		cerebusMatTexturedInfo.metallicTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_M.tga";
		cerebusMatTexturedInfo.enableRoughnessSampler = true;
		cerebusMatTexturedInfo.generateRoughnessSampler = true;
		cerebusMatTexturedInfo.roughnessTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_R.tga";
		cerebusMatTexturedInfo.enableAOSampler = true;
		cerebusMatTexturedInfo.generateAOSampler = true;
		cerebusMatTexturedInfo.aoTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Raw/Cerberus_AO.tga";
		cerebusMatTexturedInfo.enableNormalSampler = true;
		cerebusMatTexturedInfo.generateNormalSampler = true;
		cerebusMatTexturedInfo.normalTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_N.tga";
		cerebusMatTexturedInfo.enableIrradianceSampler = true;
		// TODO: Don't make user specify all this
		cerebusMatTexturedInfo.irradianceSamplerMatID = skyboxHDRMatID;
		cerebusMatTexturedInfo.enablePrefilteredMap = true;
		cerebusMatTexturedInfo.prefilterMapSamplerMatID = skyboxHDRMatID;
		cerebusMatTexturedInfo.enableBRDFLUT = true;
		cerebusMatTexturedInfo.brdfLUTSamplerMatID = skyboxHDRMatID;
		const MaterialID cerebusMatID = gameContext.renderer->InitializeMaterial(gameContext, &cerebusMatTexturedInfo);


		Renderer::MaterialCreateInfo cerebusMatTexturedInfo2 = {};
		cerebusMatTexturedInfo2.name = "Cerebus";
		cerebusMatTexturedInfo2.shaderName = "pbr";
		cerebusMatTexturedInfo2.enableAlbedoSampler = true;
		cerebusMatTexturedInfo2.generateAlbedoSampler = true;
		cerebusMatTexturedInfo2.albedoTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_A.tga";
		cerebusMatTexturedInfo2.enableMetallicSampler = true;
		cerebusMatTexturedInfo2.generateMetallicSampler = true;
		cerebusMatTexturedInfo2.metallicTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_M.tga";
		cerebusMatTexturedInfo2.enableRoughnessSampler = true;
		cerebusMatTexturedInfo2.generateRoughnessSampler = true;
		cerebusMatTexturedInfo2.roughnessTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_R.tga";
		cerebusMatTexturedInfo2.enableAOSampler = true;
		cerebusMatTexturedInfo2.generateAOSampler = true;
		cerebusMatTexturedInfo2.aoTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Raw/Cerberus_AO.tga";
		cerebusMatTexturedInfo2.enableNormalSampler = true;
		cerebusMatTexturedInfo2.generateNormalSampler = true;
		cerebusMatTexturedInfo2.normalTexturePath = RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Textures/Cerberus_N.tga";
		cerebusMatTexturedInfo2.enableIrradianceSampler = true;
		cerebusMatTexturedInfo2.irradianceSamplerMatID = skyboxHDRMatID;
		cerebusMatTexturedInfo2.enablePrefilteredMap = true;
		cerebusMatTexturedInfo2.prefilterMapSamplerMatID = skyboxHDRMatID;
		cerebusMatTexturedInfo2.enableBRDFLUT = true;
		cerebusMatTexturedInfo2.brdfLUTSamplerMatID = skyboxHDRMatID;
		const MaterialID cerebusMatID2 = gameContext.renderer->InitializeMaterial(gameContext, &cerebusMatTexturedInfo2);


		Renderer::MaterialCreateInfo brickMatInfo = {};
		brickMatInfo.shaderName = "deferred_simple";
		brickMatInfo.name = "Brick";
		brickMatInfo.enableDiffuseSampler = true;
		brickMatInfo.generateDiffuseSampler = true;
		brickMatInfo.diffuseTexturePath = RESOURCE_LOCATION + "textures/brick_d.png";
		brickMatInfo.enableSpecularSampler = true;
		brickMatInfo.generateSpecularSampler = true;
		brickMatInfo.specularTexturePath = RESOURCE_LOCATION + "textures/brick_s.png";
		brickMatInfo.enableNormalSampler = true;
		brickMatInfo.generateNormalSampler = true;
		brickMatInfo.normalTexturePath = RESOURCE_LOCATION + "textures/brick_n.png";
		const MaterialID brickMatID = gameContext.renderer->InitializeMaterial(gameContext, &brickMatInfo);

		//Renderer::MaterialCreateInfo pbrMatTexturedInfo = {};
		//pbrMatTexturedInfo.shaderName = "pbr";
		//pbrMatTexturedInfo.name = "PBR textured";
		//pbrMatTexturedInfo.needAlbedoSampler = true;
		//pbrMatTexturedInfo.enableAlbedoSampler = true;
		//pbrMatTexturedInfo.albedoTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_basecolor.png";
		//pbrMatTexturedInfo.needMetallicSampler = true;
		//pbrMatTexturedInfo.enableMetallicSampler = true;
		//pbrMatTexturedInfo.metallicTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_metallic.png";
		//pbrMatTexturedInfo.needRoughnessSampler = true;
		//pbrMatTexturedInfo.enableRoughnessSampler = true;
		//pbrMatTexturedInfo.roughnessTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_roughness.png";
		//pbrMatTexturedInfo.needAOSampler = true;
		//pbrMatTexturedInfo.enableAOSampler = true;
		//pbrMatTexturedInfo.aoTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_basecolor.png";
		//pbrMatTexturedInfo.normalTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_normal.png";
		//pbrMatTexturedInfo.generateIrradianceSampler = false; // Don't generate irradiance sampler, just 
		//pbrMatTexturedInfo.needIrradianceSampler = true;		  // use skybox's irradiance sampler
		//pbrMatTexturedInfo.enableIrradianceSampler = true;		  // use skybox's irradiance sampler
		//pbrMatTexturedInfo.irradianceSamplerMatID = skyboxHDRMatID; // here
		//pbrMatTexturedInfo.needPrefilteredMap = true;
		//pbrMatTexturedInfo.enablePrefilteredMap = true;
		//pbrMatTexturedInfo.prefilterMapSamplerMatID = skyboxHDRMatID;
		//pbrMatTexturedInfo.needBRDFLUT = true;
		//pbrMatTexturedInfo.enableBRDFLUT = true;
		//pbrMatTexturedInfo.brdfLUTSamplerMatID = skyboxHDRMatID;
		//const MaterialID pbrMatTexturedID = gameContext.renderer->InitializeMaterial(gameContext, &pbrMatTexturedInfo);


		//Renderer::MaterialCreateInfo brickMatInfo = {};
		//brickMatInfo.shaderName = "deferred_simple";
		//brickMatInfo.name = "Brick";
		//brickMatInfo.diffuseTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_basecolor.png";
		//brickMatInfo.specularTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_metallic.png";
		//brickMatInfo.normalTexturePath = RESOURCE_LOCATION + "textures/rusted_iron/rusted_iron_normal.png";
		//const MaterialID brickMatID = gameContext.renderer->InitializeMaterial(gameContext, &brickMatInfo);

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


		m_Skybox = new MeshPrefab(skyboxHDRMatID, "Skybox");
		m_Skybox->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::SKYBOX);
		AddChild(gameContext, m_Skybox);


		m_Cerberus = new MeshPrefab(cerebusMatID, "Cerberus");
		m_Cerberus->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Cerberus_LP_WithB&T.fbx", true, true, false, true);
		AddChild(gameContext, m_Cerberus);
		m_Cerberus->GetTransform().Scale({ 0.075f, 0.075f, 0.075f });
		m_Cerberus->GetTransform().Translate({ 0.0f, 10.0f, 0.0f });


		MeshPrefab* extraCerberus = new MeshPrefab(cerebusMatID2, "Cerberus 2");
		extraCerberus->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/Cerberus_by_Andrew_Maximov/Cerberus_LP_WithB&T.fbx", true, true, false, true);
		AddChild(gameContext, extraCerberus);
		extraCerberus->GetTransform().Scale({ 0.075f, 0.075f, 0.075f });
		extraCerberus->GetTransform().Translate({ 0.0f, 3.0f, 5.0f });


		const int sphereCountX = 8;
		const int sphereCountY = 2;
		const float sphereSpacing = 2.5f;
		const glm::vec3 offset = glm::vec3(-sphereCountX / 2 * sphereSpacing, -sphereCountY / 2 * sphereSpacing, 0.0f);
		const size_t sphereCount = sphereCountX * sphereCountY;
		m_Spheres.resize(sphereCount);
		for (size_t i = 0; i < sphereCount; ++i)
		{
			int x = i % sphereCountX;
			int y = int(i / sphereCountX);
		
			const std::string iStr = std::to_string(i);
		
			MaterialID matID = brickMatID;

			if ((x + y) % 2 == 0)
			{
				Renderer::MaterialCreateInfo pbrMatInfo = {};
				pbrMatInfo.shaderName = "pbr";
				pbrMatInfo.name = "PBR simple " + iStr;
				pbrMatInfo.constAlbedo = glm::vec3(0.25f, 0.75f, 0.14f);
				pbrMatInfo.constMetallic = float(x) / (sphereCountX - 1);
				pbrMatInfo.constRoughness = glm::clamp(float(y) / (sphereCountY - 1), 0.05f, 1.0f);
				pbrMatInfo.constAO = 1.0f;
				matID = gameContext.renderer->InitializeMaterial(gameContext, &pbrMatInfo);
			}

			m_Spheres[i] = new MeshPrefab(matID, "Sphere " + iStr);
			m_Spheres[i]->ForceAttributes((glm::uint)VertexAttribute::COLOR_R32G32B32A32_SFLOAT);
			m_Spheres[i]->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/sphere.fbx", true, true);
			m_Spheres[i]->GetTransform().position = offset + glm::vec3(x * sphereSpacing, y * sphereSpacing, 0.0f);
			AddChild(gameContext, m_Spheres[i]);
		}
		
		//m_Arisaka = new MeshPrefab(arisakaMatID, "Arisaka Type 99");
		//m_Arisaka->SetUVScale(2.0f, 1.0f);
		//m_Arisaka->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/Arisaka_Type_99_gun_low.fbx", true, true);
		//m_Arisaka->GetTransform().Translate({ 0, 0, -10.0f });
		//m_Arisaka->GetTransform().Rotate({ PI, 0, PI });
		//AddChild(m_Arisaka);
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
		//n_Cerberus->GetTransform().Rotate({ 0, gameContext.elapsedTime * 0.00075f, 0 });
	}
} // namespace flex
