#include "stdafx.hpp"

#include "Scene/Scenes/BaseScene.hpp"

#include <glm/vec3.hpp>

#include "Scene/GameObject.hpp"
#include "Logger.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "JSONParser.hpp"

#include "Physics/PhysicsWorld.hpp"
#include "Physics/PhysicsManager.hpp"
#include "Physics/RigidBody.hpp"
#include "Scene/ReflectionProbe.hpp"

namespace flex
{
	BaseScene::BaseScene(const std::string& name) :
		m_Name(name)
	{
	}

	BaseScene::~BaseScene()
	{
		auto iter = m_Children.begin();
		while (iter != m_Children.end())
		{
			delete *iter;
			iter = m_Children.erase(iter);
		}

		if (m_PhysicsWorld)
		{
			m_PhysicsWorld->Destroy();
			SafeDelete(m_PhysicsWorld);
		}
	}

	void BaseScene::CreateFromJSON(const std::string& jsonFilePath)
	{
		JSONParser::ParsedFile parsedFile;

		JSONParser::Parse(jsonFilePath, parsedFile);

		Logger::LogInfo("Parsed scene file:");
		Logger::LogInfo(JSONParser::Print(parsedFile.rootObject, 0));
	}

	std::string BaseScene::GetName() const
	{
		return m_Name;
	}

	PhysicsWorld* BaseScene::GetPhysicsWorld()
	{
		return m_PhysicsWorld;
	}

	void BaseScene::Initialize(const GameContext& gameContext)
	{
		MaterialCreateInfo colorMatInfo = {};
		colorMatInfo.shaderName = "color";
		colorMatInfo.name = "Color";
		gameContext.renderer->InitializeMaterial(gameContext, &colorMatInfo);

		m_PhysicsWorld = new PhysicsWorld();
		m_PhysicsWorld->Initialize(gameContext);

		m_PhysicsWorld->GetWorld()->setGravity({ 0.0f, -9.81f, 0.0f });


		MaterialCreateInfo skyboxHDRMatInfo = {};
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
		skyboxHDRMatInfo.environmentMapPath = RESOURCE_LOCATION + "textures/hdri/Milkyway/Milkyway_Light.hdr";
		MaterialID skyboxMatID = gameContext.renderer->InitializeMaterial(gameContext, &skyboxHDRMatInfo);
		gameContext.renderer->SetSkyboxMaterial(skyboxMatID);

		// Generated last so it can use generated skybox maps
		m_ReflectionProbe = new ReflectionProbe(true);
		AddChild(gameContext, m_ReflectionProbe);
	}

	void BaseScene::PostInitialize(const GameContext& gameContext)
	{
		gameContext.renderer->SetReflectionProbeMaterial(m_ReflectionProbe->GetCaptureMaterialID());
	}

	void BaseScene::Destroy(const GameContext& gameContext)
	{
	}

	void BaseScene::Update(const GameContext& gameContext)
	{
	}

	void BaseScene::AddChild(const GameContext& gameContext, GameObject* pGameObject)
	{
		UNREFERENCED_PARAMETER(gameContext);

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			if (*iter == pGameObject)
			{
				Logger::LogWarning("Attempting to add child to scene again");
				return;
			}
		}

		m_Children.push_back(pGameObject);
	}

	void BaseScene::RemoveChild(GameObject* gameObject, bool deleteChild)
	{
		auto iter = m_Children.begin();
		while (iter != m_Children.end())
		{
			if (*iter == gameObject)
			{
				if (deleteChild)
				{
					SafeDelete(*iter);
				}

				iter = m_Children.erase(iter);
				return;
			}
			else
			{
				++iter;
			}
		}

		Logger::LogWarning("Attempting to remove non-existent child from scene");
	}

	void BaseScene::RemoveAllChildren(bool deleteChildren)
	{
		auto iter = m_Children.begin();
		while (iter != m_Children.end())
		{
			if (deleteChildren)
			{
				delete *iter;
			}

			iter = m_Children.erase(iter);
		}
	}

	void BaseScene::RootInitialize(const GameContext& gameContext)
	{
		Initialize(gameContext);

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->Initialize(gameContext);
		}
	}

	void BaseScene::RootPostInitialize(const GameContext& gameContext)
	{
		PostInitialize(gameContext);

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->PostInitialize(gameContext);
		}
	}

	void BaseScene::RootUpdate(const GameContext& gameContext)
	{
		if (m_PhysicsWorld)
		{
			m_PhysicsWorld->Update(gameContext.deltaTime);
		}

		Update(gameContext);

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->RootUpdate(gameContext);
		}
	}

	void BaseScene::RootDestroy(const GameContext& gameContext)
	{
		Destroy(gameContext);

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->RootDestroy(gameContext);
		}
	}
} // namespace flex
