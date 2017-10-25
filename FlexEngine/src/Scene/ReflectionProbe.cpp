
#include "stdafx.hpp"

#include "Scene/ReflectionProbe.hpp"
#include "Scene/MeshPrefab.hpp"

namespace flex
{
	ReflectionProbe::ReflectionProbe()
	{
	}

	ReflectionProbe::~ReflectionProbe()
	{
	}

	void ReflectionProbe::Initialize(const GameContext& gameContext)
	{
		// TODO: Generate reflection probe mat here

		// Reflective chrome ball
		Renderer::MaterialCreateInfo reflectionProbeMaterialCreateInfo = {};
		reflectionProbeMaterialCreateInfo.name = "Reflection probe";
		reflectionProbeMaterialCreateInfo.shaderName = "pbr";
		reflectionProbeMaterialCreateInfo.constAlbedo = glm::vec3(0.75f, 0.75f, 0.75f);
		reflectionProbeMaterialCreateInfo.constMetallic = 1.0f;
		reflectionProbeMaterialCreateInfo.constRoughness = 0.0f;
		reflectionProbeMaterialCreateInfo.constAO = 1.0f;
		reflectionProbeMaterialCreateInfo.generateHDRCubemapSampler = true;
		reflectionProbeMaterialCreateInfo.enableCubemapSampler = true;
		reflectionProbeMaterialCreateInfo.generatedCubemapSize = glm::uvec2(512.0f, 512.0f);
		MaterialID reflectionProbeMaterialID = gameContext.renderer->InitializeMaterial(gameContext, &reflectionProbeMaterialCreateInfo);

		m_MeshPrefab = new MeshPrefab(reflectionProbeMaterialID, "Reflection probe");
		m_MeshPrefab->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/sphere.fbx", true, true);
		AddChild(m_MeshPrefab);

		gameContext.renderer->UpdateTransformMatrix(gameContext, m_MeshPrefab->GetRenderID(), m_MeshPrefab->GetTransform().GetModelMatrix());
	}

	void ReflectionProbe::PostInitialize(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
	}

	void ReflectionProbe::Update(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
	}

	void ReflectionProbe::Destroy(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
	}
}