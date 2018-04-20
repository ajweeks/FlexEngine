
#include "stdafx.hpp"

#include "Scene/ReflectionProbe.hpp"
#include "Scene/MeshPrefab.hpp"
#include "Scene/GameObject.hpp"

namespace flex
{
	ReflectionProbe::ReflectionProbe(bool visible) :
		m_Visible(visible)
	{
	}

	ReflectionProbe::~ReflectionProbe()
	{
	}

	void ReflectionProbe::Initialize(const GameContext& gameContext)
	{
		// Reflective chrome ball material
		MaterialCreateInfo reflectionProbeMaterialCreateInfo = {};
		reflectionProbeMaterialCreateInfo.name = "Reflection probe ball";
		reflectionProbeMaterialCreateInfo.shaderName = "pbr";
		reflectionProbeMaterialCreateInfo.constAlbedo = glm::vec3(0.75f, 0.75f, 0.75f);
		reflectionProbeMaterialCreateInfo.constMetallic = 1.0f;
		reflectionProbeMaterialCreateInfo.constRoughness = 0.0f;
		reflectionProbeMaterialCreateInfo.constAO = 1.0f;
		MaterialID reflectionProbeMaterialID = gameContext.renderer->InitializeMaterial(gameContext, &reflectionProbeMaterialCreateInfo);

		// Probe capture material
		MaterialCreateInfo probeCaptureMatCreateInfo = {};
		probeCaptureMatCreateInfo.name = "Reflection probe capture";
		probeCaptureMatCreateInfo.shaderName = "deferred_combine_cubemap";
		probeCaptureMatCreateInfo.generateReflectionProbeMaps = true;
		probeCaptureMatCreateInfo.generateHDRCubemapSampler = true;
		probeCaptureMatCreateInfo.generatedCubemapSize = glm::uvec2(512.0f, 512.0f); // TODO: Add support for non-512.0f size
		probeCaptureMatCreateInfo.generateCubemapDepthBuffers = true;
		probeCaptureMatCreateInfo.enableIrradianceSampler = true;
		probeCaptureMatCreateInfo.generateIrradianceSampler = true;
		probeCaptureMatCreateInfo.generatedIrradianceCubemapSize = { 32, 32 };
		probeCaptureMatCreateInfo.enablePrefilteredMap = true;
		probeCaptureMatCreateInfo.generatePrefilteredMap = true;
		probeCaptureMatCreateInfo.generatedPrefilteredCubemapSize = { 128, 128 };
		probeCaptureMatCreateInfo.enableBRDFLUT = true;
		probeCaptureMatCreateInfo.frameBuffers = {
			{ "positionMetallicFrameBufferSampler", nullptr },
			{ "normalRoughnessFrameBufferSampler", nullptr },
			{ "albedoAOFrameBufferSampler", nullptr },
		};
		m_CaptureMatID = gameContext.renderer->InitializeMaterial(gameContext, &probeCaptureMatCreateInfo);



		m_SphereMesh = new MeshPrefab(reflectionProbeMaterialID, "Reflection probe");
		m_SphereMesh->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/sphere.fbx", true, true);
		AddChild(m_SphereMesh);
		m_SphereMesh->GetTransform().Scale(1.5f);
		if (!m_Visible)
		{
			gameContext.renderer->SetRenderObjectVisible(m_SphereMesh->GetRenderID(), m_Visible);
		}

		m_Capture = new GameObject();
		RenderObjectCreateInfo captureObjectCreateInfo = {};
		captureObjectCreateInfo.vertexBufferData = nullptr;
		captureObjectCreateInfo.materialID = m_CaptureMatID;
		captureObjectCreateInfo.name = "Reflection probe capture object";
		captureObjectCreateInfo.transform = &m_SphereMesh->GetTransform();
		captureObjectCreateInfo.visibleInSceneExplorer = false;
		
		RenderID captureRenderID = gameContext.renderer->InitializeRenderObject(gameContext, &captureObjectCreateInfo);
		m_Capture->SetRenderID(captureRenderID);
		gameContext.renderer->SetRenderObjectVisible(captureRenderID, false);

		m_SphereMesh->AddChild(m_Capture);
	}

	void ReflectionProbe::PostInitialize(const GameContext& gameContext)
	{
	}

	void ReflectionProbe::Update(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);
	}

	MaterialID ReflectionProbe::GetCaptureMaterialID() const
	{
		return m_CaptureMatID;
	}

	void ReflectionProbe::SetSphereVisible(bool visible, const GameContext& gameContext)
	{
		m_Visible = visible;
		gameContext.renderer->SetRenderObjectVisible(m_SphereMesh->GetRenderID(), visible);
	}
	Transform& ReflectionProbe::GetTransform()
	{
		return m_SphereMesh->GetTransform();
	}
}