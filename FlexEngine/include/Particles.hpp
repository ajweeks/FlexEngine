#pragma once

#include "JSONTypes.hpp"

namespace flex
{
	struct JSONObject;

	enum class ParticleSampleType
	{
		CONSTANT,
		RANDOM,

		_NONE
	};


	static const char* ParticleSampleTypeStrings[] =
	{
		"constant",
		"random",

		"NONE",
	};

	static_assert(ARRAY_LENGTH(ParticleSampleTypeStrings) == (u32)ParticleSampleType::_NONE + 1, "ParticleSampleTypeStrings length must match ParticleSampleType enum");

	static const char* ParticleSampleTypeToString(ParticleSampleType type);
	static ParticleSampleType ParticleSampleTypeFromString(const char* typeStr);


	enum class ParticleParamterValueType
	{
		FLOAT,
		BOOL,
		INT,
		VEC2,
		VEC3,
		VEC4,

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

		"NONE",
	};

	static_assert(ARRAY_LENGTH(ParticleParamterValueTypeStrings) == (u32)ParticleParamterValueType::_NONE + 1, "ParticleParamterValueTypeStrings length must match ParticleParamterValueType enum");

	static const char* ParticleParamterValueTypeToString(ParticleParamterValueType type);
	static ParticleParamterValueType ParticleParamterValueTypeFromString(const char* typeStr);

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

		real GetReal() const;
		bool GetBool() const;
		i32 GetInt() const;
		void SetReal(real value);
		void SetBool(bool value);
		void SetInt(i32 value);

		JSONObject Serialize(ParticleParamterValueType parentValueType);
		static ParticleParameter Deserialize(const JSONObject& parentObject, ParticleParamterValueType parentValueType);

		void DrawImGuiObjects(const char* label);

		ParticleSampleType type;
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

		bool GetBool(u32 index) const;
		real GetReal(u32 index) const;
		i32 GetInt(u32 index) const;
		glm::vec2 GetVec2() const;
		glm::vec3 GetVec3() const;
		glm::vec4 GetVec4() const;

		void DrawImGuiObjects();

		std::string name;
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

		bool GetBool(StringID parameterName) const;
		real GetReal(StringID parameterName) const;
		i32 GetInt(StringID parameterName) const;
		glm::vec2 GetVec2(StringID parameterName) const;
		glm::vec3 GetVec3(StringID parameterName) const;
		glm::vec4 GetVec4(StringID parameterName) const;

		void DrawImGuiObjects();

		std::map<StringID, ParticleParameterPack> m_Parameters;
	};

	struct ParticleSimData
	{
		glm::vec4 colour0;
		glm::vec4 colour1;
		real dt;
		u32 particleCount;
	};

	class ParticleSystem final
	{
	public:
		ParticleSystem();
		~ParticleSystem() = default;

		ParticleSystem(const ParticleSystem& other) = delete;
		ParticleSystem(ParticleSystem&& other) = delete;
		ParticleSystem& operator=(const ParticleSystem& other) = delete;
		ParticleSystem& operator=(ParticleSystem&& other) = delete;

		void SetParameters(const ParticleParameters& params);
		const ParticleParameters& GetParameters();

		void Destroy();

		// Returns true when system can be destroyed
		bool Update();

		void DrawImGuiObjects();

		void Deserialize();
		void Serialize();

		std::string GetFilePath() const;

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
		ParticleParameters m_Parameters;

	};

	class ParticleManager : public System
	{
	public:
		ParticleManager();
		~ParticleManager();

		ParticleManager(const ParticleManager& other) = delete;
		ParticleManager(ParticleManager&& other) = delete;
		ParticleManager& operator=(const ParticleManager& other) = delete;
		ParticleManager& operator=(ParticleManager&& other) = delete;

		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void Update() override;

		virtual void DrawImGui() override;

		ParticleSystemID AddParticleSystem(StringID particleTemplateNameSID, const ParticleSimData& data, const std::string& name, const glm::mat4& objectToWorld, real lifetime);
		void RemoveParticleSystem(ParticleSystemID particleSystemID);

		void DiscoverParameterTypes();
		void SerializeParameterTypes();

		void SerializeAllTemplates();
		void DiscoverTemplates();

		ParticleParamterValueType GetParameterValueType(const char* paramName);

	private:
		ParticleParameters* GetTemplateParams(StringID particleTemplateSID);

		std::map<ParticleSystemID, ParticleSystem*> m_ParticleSystems;
		PoolAllocator<ParticleSystem, 8> m_ParticleSystemAllocator;

		std::vector<ParticleParameterType> m_ParameterTypes;
		bool m_bParameterTypesDirty = false;

		struct ParticleSystemTemplate
		{
			// Returns true when this template should be deleted
			bool DrawImGuiObjects();

			std::string filePath;
			ParticleParameters params;
		};
		friend ParticleSystemTemplate;

		std::map<StringID, ParticleSystemTemplate> m_ParticleTemplates;

	};
} // namespace flex
