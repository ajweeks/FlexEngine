#pragma once

namespace flex
{
	struct JSONObject;

	// 44 bytes
	struct ParticleSimData
	{
		glm::vec4 colour0;	// 0
		glm::vec4 colour1;	// 16
		real dt;			// 32
		real speed;			// 36
		u32 particleCount;	// 40
	};

	class ParticleSystem final
	{
	public:
		ParticleSystem();
		~ParticleSystem() = default;

		void Destroy();

		// Returns true when system can be destroyed
		bool Update();

		void DrawImGuiObjects();

		void ParseTypeUniqueFields(const JSONObject& parentObject);
		void SerializeTypeUniqueFields(JSONObject& parentObject);

		std::string name;
		ParticleSystemID ID;
		ParticleSimData data;
		bool bEnabled = true;
		glm::mat4 objectToWorld;
		real lifetimeRemaining;

		MaterialID simMaterialID = InvalidMaterialID;
		MaterialID renderingMaterialID = InvalidMaterialID;
		ParticleSystemID particleSystemID = InvalidParticleSystemID;

	private:
		struct PropertyCollection* m_PropertyCollection = nullptr;

	};

	class ParticleManager : public System
	{
	public:
		ParticleManager();
		~ParticleManager();

		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void Update() override;

		virtual void DrawImGui() override;

		ParticleSystemID AddParticleSystem(const ParticleSimData& data, const std::string& name, const glm::mat4& objectToWorld, real lifetime);
		void RemoveParticleSystem(ParticleSystemID particleSystemID);

		PropertyCollection* AllocatePropertyCollection();

	private:
		std::map<ParticleSystemID, ParticleSystem*> m_ParticleSystems;

		PoolAllocator<PropertyCollection, 32> m_PropertyCollectionAllocator;
		PoolAllocator<ParticleSystem, 8> m_ParticleSystemAllocator;

	};
} // namespace flex
