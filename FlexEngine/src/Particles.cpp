
#include "stdafx.hpp"

#include "Particles.hpp"

#include "Graphics/Renderer.hpp"
#include "PropertyCollection.hpp"
#include "Transform.hpp"
#include "JSONTypes.hpp"

namespace flex
{
	//
	// Particle System
	//

	ParticleSystem::ParticleSystem()
	{
		m_PropertyCollection = GetSystem<ParticleManager>(SystemType::PARTICLE_MANAGER)->AllocatePropertyCollection();
		m_PropertyCollection->RegisterProperty("enabled", &bEnabled)
			.VersionAdded(6)
			.DefaultValue(true);
	}

	void ParticleSystem::Destroy()
	{
		GetSystem<ParticleManager>(SystemType::PARTICLE_MANAGER)->RemoveParticleSystem(ID);
	}

	void ParticleSystem::ParseTypeUniqueFields(const JSONObject& parentObject)
	{
		JSONObject particleSystemObj;
		if (parentObject.TryGetObject("particle system info", particleSystemObj))
		{
			particleSystemObj.TryGetMat4("object to world", objectToWorld);

			m_PropertyCollection->Deserialize(particleSystemObj);

			JSONObject systemDataObj = particleSystemObj.GetObject("data");
			data = {};
			systemDataObj.TryGetVec4("colour0", data.colour0);
			systemDataObj.TryGetVec4("colour1", data.colour1);
			systemDataObj.TryGetFloat("speed", data.speed);
			i32 particleCount;
			if (systemDataObj.TryGetInt("particle count", particleCount))
			{
				data.particleCount = particleCount;
			}

			particleSystemID = g_Renderer->AddParticleSystem(name, this, particleCount);
		}
	}

	void ParticleSystem::SerializeTypeUniqueFields(JSONObject& parentObject)
	{
		JSONObject particleSystemObj = {};

		particleSystemObj.fields.emplace_back("object to world", JSONValue(objectToWorld));

		m_PropertyCollection->Serialize(particleSystemObj);

		JSONObject systemDataObj = {};
		systemDataObj.fields.emplace_back("colour0", JSONValue(data.colour0, 2));
		systemDataObj.fields.emplace_back("colour1", JSONValue(data.colour1, 2));
		systemDataObj.fields.emplace_back("speed", JSONValue(data.speed));
		systemDataObj.fields.emplace_back("particle count", JSONValue(data.particleCount));
		particleSystemObj.fields.emplace_back("data", JSONValue(systemDataObj));

		parentObject.fields.emplace_back("particle system info", JSONValue(particleSystemObj));
	}

	bool ParticleSystem::Update()
	{
		lifetimeRemaining -= g_DeltaTime;
		return lifetimeRemaining <= 0.0f;
	}

	void ParticleSystem::DrawImGuiObjects()
	{
		static const ImGuiColorEditFlags colorEditFlags =
			ImGuiColorEditFlags_NoInputs |
			ImGuiColorEditFlags_Float |
			ImGuiColorEditFlags_RGB |
			ImGuiColorEditFlags_PickerHueWheel |
			ImGuiColorEditFlags_HDR;

		ImGui::Text("Particle System");

		ImGui::Checkbox("Enabled", &bEnabled);

		ImGui::ColorEdit4("Colour 0", &data.colour0.r, colorEditFlags);
		ImGui::SameLine();
		ImGui::ColorEdit4("Colour 1", &data.colour1.r, colorEditFlags);
		ImGui::SliderFloat("Speed", &data.speed, -10.0f, 10.0f);
		i32 particleCount = (i32)data.particleCount;
		if (ImGui::SliderInt("Particle count", &particleCount, 0, Renderer::MAX_PARTICLE_COUNT))
		{
			data.particleCount = particleCount;
		}
	}

	//
	// Particle Manager
	//

	ParticleManager::ParticleManager()
	{
	}

	ParticleManager::~ParticleManager()
	{
	}

	void ParticleManager::Initialize()
	{
	}

	void ParticleManager::Destroy()
	{
		m_PropertyCollectionAllocator.ReleaseAll();
	}

	void ParticleManager::Update()
	{
		std::vector<ParticleSystemID> systemsToRemove;
		for (auto& pair : m_ParticleSystems)
		{
			if (pair.second->Update())
			{
				systemsToRemove.push_back(pair.second->ID);
			}
		}

		for (ParticleSystemID particleSystemID : systemsToRemove)
		{
			RemoveParticleSystem(particleSystemID);
		}
	}

	void ParticleManager::DrawImGui()
	{
		for (auto& pair : m_ParticleSystems)
		{
			pair.second->DrawImGuiObjects();
		}
	}

	PropertyCollection* ParticleManager::AllocatePropertyCollection()
	{
		return m_PropertyCollectionAllocator.Alloc();
	}

	ParticleSystemID ParticleManager::AddParticleSystem(const ParticleSimData& data, const std::string& name, const glm::mat4& objectToWorld, real lifetime)
	{
		ParticleSystem* newParticleSystem = m_ParticleSystemAllocator.Alloc();
		newParticleSystem->name = name;
		newParticleSystem->bEnabled = true;
		newParticleSystem->objectToWorld = objectToWorld;
		newParticleSystem->lifetimeRemaining = lifetime;

		newParticleSystem->data.colour0 = data.colour0;
		newParticleSystem->data.colour1 = data.colour1;
		newParticleSystem->data.speed = data.speed;
		newParticleSystem->data.particleCount = data.particleCount;

		ParticleSystemID particleSystemID = g_Renderer->AddParticleSystem(name, newParticleSystem, data.particleCount);
		newParticleSystem->ID = particleSystemID;
		m_ParticleSystems[particleSystemID] = newParticleSystem;
		return particleSystemID;
	}

	void ParticleManager::RemoveParticleSystem(ParticleSystemID particleSystemID)
	{
		m_ParticleSystems.erase(particleSystemID);
		g_Renderer->RemoveParticleSystem(particleSystemID);
	}

} // namespace flex
