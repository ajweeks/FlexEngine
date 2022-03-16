#pragma once

#include "JSONTypes.hpp"
#include "Graphics/RendererTypes.hpp"

namespace flex
{
	struct JSONObject;
	struct ParticleSystemTemplate;

	enum class ParticleSampleType
	{
		CONSTANT = 0,
		RANDOM = 1,

		_NONE
	};


	static const char* ParticleSampleTypeStrings[] =
	{
		"constant",
		"random",

		"NONE",
	};

	static_assert(ARRAY_LENGTH(ParticleSampleTypeStrings) == (u32)ParticleSampleType::_NONE + 1, "ParticleSampleTypeStrings length must match ParticleSampleType enum");

	const char* ParticleSampleTypeToString(ParticleSampleType type);
	ParticleSampleType ParticleSampleTypeFromString(const char* typeStr);


	enum class ParticleParamterValueType
	{
		FLOAT,
		BOOL,
		INT,
		VEC2,
		VEC3,
		VEC4,
		COL3,
		COL4,

		_NONE
	};

	static const char* ParticleParamterValueTypeStrings[] =
	{
		"float",
		"bool",
		"int",
		"vec2",
		"vec3",
		"vec4",
		"col3",
		"col4",

		"NONE",
	};

	static_assert(ARRAY_LENGTH(ParticleParamterValueTypeStrings) == (u32)ParticleParamterValueType::_NONE + 1, "ParticleParamterValueTypeStrings length must match ParticleParamterValueType enum");

	const char* ParticleParamterValueTypeToString(ParticleParamterValueType type);
	ParticleParamterValueType ParticleParamterValueTypeFromString(const char* typeStr);

	bool IsValueTypeFloat(ParticleParamterValueType valueType);


	struct ParticleParameterType
	{
		ParticleParameterType(const std::string& name, ParticleParamterValueType valueType) :
			name(name),
			valueType(valueType)
		{}

		std::string name;
		ParticleParamterValueType valueType;
	};

	struct ParticleParameter
	{
		ParticleParameter() = default;

		ParticleParameter(const ParticleParameter& other);
		ParticleParameter(ParticleParameter&& other);
		ParticleParameter& operator=(const ParticleParameter& other);
		ParticleParameter& operator=(ParticleParameter&& other);

		static ParticleParameter GetDefault();

		real GetReal() const;
		real GetRealMin() const;
		real GetRealMax() const;
		bool GetBool() const;
		i32 GetInt() const;
		i32 GetIntMin() const;
		i32 GetIntMax() const;
		void SetReal(real value);
		void SetBool(bool value);
		void SetInt(i32 value);

		JSONObject Serialize(ParticleParamterValueType parentValueType);
		static ParticleParameter Deserialize(const JSONObject& parentObject, ParticleParamterValueType parentValueType);

		bool DrawImGuiObjects(const char* label, ParticleParamterValueType parentValueType);

		ParticleSampleType type = ParticleSampleType::_NONE;
		union
		{
			real constantValue;
			Pair<real, real> randomRange;
		};
	};

	struct ParticleParameterPack
	{
		ParticleParameterPack() = default;
		ParticleParameterPack(const std::string& name, ParticleParamterValueType valueType);

		ParticleParameterPack(const ParticleParameterPack& other);
		ParticleParameterPack(ParticleParameterPack&& other);
		ParticleParameterPack& operator=(const ParticleParameterPack& other);
		ParticleParameterPack& operator=(ParticleParameterPack&& other);

		static ParticleParameterPack Deserialize(const char* label, const JSONObject& parentObject);
		JSONValue Serialize();

		ParticleSampleType GetSampleType(u32 index = 0) const;
		bool GetBool(u32 index = 0) const;
		real GetReal(u32 index = 0) const;
		real GetRealMin(u32 index = 0) const;
		real GetRealMax(u32 index = 0) const;
		i32 GetInt(u32 index = 0) const;
		i32 GetIntMin(u32 index = 0) const;
		i32 GetIntMax(u32 index = 0) const;
		glm::vec2 GetVec2() const;
		glm::vec2 GetVec2Min() const;
		glm::vec2 GetVec2Max() const;
		glm::vec3 GetVec3() const;
		glm::vec3 GetVec3Min() const;
		glm::vec3 GetVec3Max() const;
		glm::vec4 GetVec4() const;
		glm::vec4 GetVec4Min() const;
		glm::vec4 GetVec4Max() const;

		bool DrawImGuiObjects();

		std::string name;
		StringID nameSID = InvalidStringID;
		ParticleParamterValueType valueType = ParticleParamterValueType::_NONE;
		std::vector<ParticleParameter> params;
	};

	struct ParticleParameters
	{
		ParticleParameters() = default;
		~ParticleParameters() = default;

		ParticleParameters(const ParticleParameters& other);
		ParticleParameters(ParticleParameters&& other);
		ParticleParameters& operator=(const ParticleParameters& other);
		ParticleParameters& operator=(ParticleParameters&& other);

		void Serialize(JSONObject& parentObject);
		void Deserialize(const JSONObject& parentObject);

		static bool Deserialize(const std::string& filePath, ParticleParameters& particleParameters);
		static void Serialize(const std::string& filePath, ParticleParameters& particleParameters);

		void AddParam(const ParticleParameterPack& paramPack);
		ParticleParameterPack const* GetParam(StringID paramNameSID) const;
		bool HasParam(StringID paramNameSID) const;

		bool DrawImGuiObjects();

		void FillOutGPUParams(struct ParticleSpawnParams& particleSpawnParams);

	private:
		void SortParams();

		std::vector<ParticleParameterPack> m_Parameters;
	};

	struct ParticleEmitterInstance
	{
		ParticleSimData data;
		glm::mat4 objectToWorld;
		real lifetimeRemaining = 0.0f;
		real particleMaxLifetime = 0.0f;
		ParticleEmitterID ID = InvalidParticleEmitterID;
		u32 bufferIndex = u32_max;
		bool bEnableRespawning = false;
	};

	class ParticleSystem final
	{
	public:
		ParticleSystem();
		ParticleSystem(StringID templateNameSID, const ParticleParameters& params);
		~ParticleSystem() = default;

		ParticleSystem(const ParticleSystem& other) = delete;
		ParticleSystem(ParticleSystem&& other) = delete;
		ParticleSystem& operator=(const ParticleSystem& other) = delete;
		ParticleSystem& operator=(ParticleSystem&& other) = delete;

		void OnTemplateUpdated(const ParticleSystemTemplate& newTemplate);
		const ParticleParameters& GetParameters();

		void Destroy();

		ParticleEmitterID SpawnEmitterInstance(const glm::mat4& objectToWorld);
		void ExtinguishEmitter(ParticleEmitterID emitterID);

		// Returns true when system can be destroyed
		void Update();
		i32 GetEmitterInstanceCount() const;

		void DrawImGuiObjects();

		void Deserialize();
		void Serialize();

		std::string GetFilePath() const;

		StringID GetParticleSystemTemplateNameSID() const;

		std::string name;
		ParticleSystemID ID = InvalidParticleSystemID;
		bool bEnabled = true;

		MaterialID simMaterialID = InvalidMaterialID;
		MaterialID renderingMaterialID = InvalidMaterialID;

		std::unordered_map<ParticleEmitterID, ParticleEmitterInstance> emitterInstances;
		i32 m_ParticleCount = 0;

	private:
		void CacheParticleCount();

		StringID m_TemplateNameSID = InvalidStringID;
		ParticleParameters m_Parameters;
		ParticleSpawnParams m_SpawnParams;

		ParticleEmitterID m_LastEmitterIndex = 0;

	};

	struct ParticleSystemTemplate
	{
		enum class ImGuiResult
		{
			REMOVED,
			CHANGED,
			NOTHING
		};

		// Returns true when this template should be deleted
		ImGuiResult DrawImGuiObjects();

		StringID nameSID = InvalidStringID;
		std::string filePath;
		ParticleParameters params;
		bool bDirty = false;
	};

	class ParticleManager : public System
	{
	public:
		ParticleManager() = default;
		~ParticleManager() = default;

		ParticleManager(const ParticleManager& other) = delete;
		ParticleManager(ParticleManager&& other) = delete;
		ParticleManager& operator=(const ParticleManager& other) = delete;
		ParticleManager& operator=(ParticleManager&& other) = delete;

		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void Update() override;
		virtual void OnPreSceneChange() override;

		virtual void DrawImGui() override;

		ParticleSystemID CreateParticleSystem(StringID particleTemplateNameSID, const std::string& name);
		ParticleSystem* GetParticleSystem(StringID particleTemplateNameSID);
		ParticleSystem* GetOrCreateParticleSystem(StringID particleTemplateNameSID, const char* particleTemplateName);
		void RemoveParticleSystem(StringID particleTemplateNameSID);

		void DiscoverParameterTypes();
		void SerializeParameterTypes();

	private:
		friend ParticleSystemTemplate;

		void DestroyAllSystems();

		std::unordered_map<StringID, ParticleSystem*> m_ParticleSystems;
		PoolAllocator<ParticleSystem, 8> m_ParticleSystemAllocator;


	};
} // namespace flex
