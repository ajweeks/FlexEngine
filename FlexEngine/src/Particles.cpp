
#include "stdafx.hpp"

#include "Particles.hpp"

#include "FlexEngine.hpp"
#include "Graphics/Renderer.hpp"
#include "Transform.hpp"
#include "JSONParser.hpp"

namespace flex
{
	//
	// ParticleSampleType
	//

	const char* ParticleSampleTypeToString(ParticleSampleType type)
	{
		return ParticleSampleTypeStrings[(i32)type];
	}

	ParticleSampleType ParticleSampleTypeFromString(const char* typeStr)
	{
		for (i32 i = 0; i < ARRAY_LENGTH(ParticleSampleTypeStrings); ++i)
		{
			if (strcmp(ParticleSampleTypeStrings[i], typeStr) == 0)
			{
				return (ParticleSampleType)i;
			}
		}

		return ParticleSampleType::_NONE;
	}

	//
	// ParticleParamterValueType
	//

	const char* ParticleParamterValueTypeToString(ParticleParamterValueType type)
	{
		return ParticleParamterValueTypeStrings[(i32)type];
	}

	ParticleParamterValueType ParticleParamterValueTypeFromString(const char* typeStr)
	{
		for (i32 i = 0; i < ARRAY_LENGTH(ParticleParamterValueTypeStrings); ++i)
		{
			if (strcmp(ParticleParamterValueTypeStrings[i], typeStr) == 0)
			{
				return (ParticleParamterValueType)i;
			}
		}

		return ParticleParamterValueType::_NONE;
	}

	//
	// ParticleParameter
	//

	ParticleParameter::ParticleParameter(const ParticleParameter& other)
	{
		if (this != &other)
		{
			memcpy(this, &other, sizeof(ParticleParameter));
		}
	}

	ParticleParameter::ParticleParameter(ParticleParameter&& other)
	{
		if (this != &other)
		{
			memcpy(this, &other, sizeof(ParticleParameter));
		}
	}

	ParticleParameter& ParticleParameter::operator=(const ParticleParameter& other)
	{
		if (this != &other)
		{
			memcpy(this, &other, sizeof(ParticleParameter));
		}
		return *this;
	}

	ParticleParameter& ParticleParameter::operator=(ParticleParameter&& other)
	{
		if (this != &other)
		{
			memcpy(this, &other, sizeof(ParticleParameter));
		}
		return *this;
	}

	real ParticleParameter::GetReal() const
	{
		switch (type)
		{
		case ParticleSampleType::CONSTANT:
			return constantValue;
		case ParticleSampleType::RANDOM:
			return RandomFloat(randomRange.first, randomRange.second);
		default:
			ENSURE_NO_ENTRY();
			return -1.0f;
		}
	}

	bool ParticleParameter::GetBool() const
	{
		switch (type)
		{
		case ParticleSampleType::CONSTANT:
			return constantValue != 0.0f;
		case ParticleSampleType::RANDOM:
			return RandomFloat(randomRange.first, randomRange.second) != 0.0f;
		default:
			ENSURE_NO_ENTRY();
			return false;
		}
	}

	i32 ParticleParameter::GetInt() const
	{
		switch (type)
		{
		case ParticleSampleType::CONSTANT:
			return (i32)constantValue;
		case ParticleSampleType::RANDOM:
			return (i32)RandomFloat(randomRange.first, randomRange.second);
		default:
			ENSURE_NO_ENTRY();
			return -1;
		}
	}

	void ParticleParameter::SetReal(real value)
	{
		switch (type)
		{
		case ParticleSampleType::CONSTANT:
			constantValue = value;
			break;
		default:
			ENSURE_NO_ENTRY();
		}
	}

	void ParticleParameter::SetBool(bool bValue)
	{
		switch (type)
		{
		case ParticleSampleType::CONSTANT:
			constantValue = bValue ? 1.0f : 0.0f;
			break;
		default:
			ENSURE_NO_ENTRY();
		}
	}

	void ParticleParameter::SetInt(i32 value)
	{
		switch (type)
		{
		case ParticleSampleType::CONSTANT:
			constantValue = (real)value;
			break;
		default:
			ENSURE_NO_ENTRY();
		}
	}

	JSONObject ParticleParameter::Serialize(ParticleParamterValueType parentValueType)
	{
		JSONObject obj = {};

		const char* typeStr = ParticleSampleTypeToString(type);
		obj.fields.emplace_back("type", JSONValue(typeStr));

		switch (type)
		{
		case ParticleSampleType::CONSTANT:
			switch (parentValueType)
			{
			case ParticleParamterValueType::FLOAT:
			case ParticleParamterValueType::VEC2:
			case ParticleParamterValueType::VEC3:
			case ParticleParamterValueType::VEC4:
				obj.fields.emplace_back("constant", JSONValue(constantValue));
				break;
			case ParticleParamterValueType::BOOL:
				obj.fields.emplace_back("constant", JSONValue(GetBool()));
				break;
			case ParticleParamterValueType::INT:
				obj.fields.emplace_back("constant", JSONValue(GetInt()));
				break;
			default:
				ENSURE_NO_ENTRY();
				break;
			}
			break;
		case ParticleSampleType::RANDOM:
			obj.fields.emplace_back("random min", JSONValue(randomRange.first));
			obj.fields.emplace_back("random max", JSONValue(randomRange.second));
			break;
		default:
			ENSURE_NO_ENTRY();
			break;
		}

		return obj;
	}

	ParticleParameter ParticleParameter::Deserialize(const JSONObject& parentObject, ParticleParamterValueType parentValueType)
	{
		ParticleParameter result = {};

		std::string typeStr = parentObject.GetString("type");
		result.type = ParticleSampleTypeFromString(typeStr.c_str());

		switch (result.type)
		{
		case ParticleSampleType::CONSTANT:
			switch (parentValueType)
			{
			case ParticleParamterValueType::FLOAT:
			case ParticleParamterValueType::VEC2:
			case ParticleParamterValueType::VEC3:
			case ParticleParamterValueType::VEC4:
			{
				parentObject.TryGetFloat("constant", result.constantValue);
			} break;
			case ParticleParamterValueType::BOOL:
			{
				bool boolValue;
				if (parentObject.TryGetBool("constant", boolValue))
				{
					result.SetBool(boolValue);
				}
			} break;
			case ParticleParamterValueType::INT:
			{
				i32 intValue;
				if (parentObject.TryGetInt("constant", intValue))
				{
					result.SetInt(intValue);
				}
			} break;
			default:
				ENSURE_NO_ENTRY();
				break;
			}
			break;
		case ParticleSampleType::RANDOM:
			parentObject.TryGetFloat("random min", result.randomRange.first);
			parentObject.TryGetFloat("random max", result.randomRange.second);
			break;
		default:
			PrintError("Parsed invalid particle parameter type: %s\n", typeStr.c_str());
			break;
		}

		return result;
	}

	void ParticleParameter::DrawImGuiObjects(const char* label)
	{
		ImGui::PushID(this);

		i32 typeInt = (i32)type;
		if (ImGui::Combo("Type", &typeInt, ParticleSampleTypeStrings, ARRAY_LENGTH(ParticleSampleTypeStrings) - 1))
		{
			type = (ParticleSampleType)typeInt;
		}

		switch (type)
		{
		case ParticleSampleType::CONSTANT:
			ImGui::InputFloat(label, &constantValue);
			break;
		case ParticleSampleType::RANDOM:
			ImGui::DragFloatRange2(label, &randomRange.first, &randomRange.second);
			break;
		default:
			ENSURE_NO_ENTRY();
		}

		ImGui::PopID();
	}

	//
	// ParticleParameterPack
	//

	ParticleParameterPack::ParticleParameterPack(const std::string& name, ParticleParamterValueType valueType) :
		name(name),
		valueType(valueType)
	{
		switch (valueType)
		{
		case ParticleParamterValueType::BOOL:
		case ParticleParamterValueType::FLOAT:
		case ParticleParamterValueType::INT:
			params.resize(1, {});
			break;
		case ParticleParamterValueType::VEC2:
			params.resize(2, {});
			break;
		case ParticleParamterValueType::VEC3:
			params.resize(3, {});
			break;
		case ParticleParamterValueType::VEC4:
			params.resize(4, {});
			break;
		default:
			ENSURE_NO_ENTRY();
		}
	}

	ParticleParameterPack::ParticleParameterPack(const ParticleParameterPack& other)
	{
		if (this != &other)
		{
			name = other.name;
			valueType = other.valueType;
			params = std::vector<ParticleParameter>(other.params);
		}
	}

	ParticleParameterPack::ParticleParameterPack(ParticleParameterPack&& other)
	{
		if (this != &other)
		{
			name = std::move(other.name);
			valueType = other.valueType;
			params = std::move(other.params);
		}
	}

	ParticleParameterPack& ParticleParameterPack::operator=(const ParticleParameterPack& other)
	{
		if (this != &other)
		{
			name = other.name;
			valueType = other.valueType;
			params = std::vector<ParticleParameter>(other.params);
		}
		return *this;
	}

	ParticleParameterPack& ParticleParameterPack::operator=(ParticleParameterPack&& other)
	{
		if (this != &other)
		{
			name = std::move(other.name);
			valueType = other.valueType;
			params = std::move(other.params);
		}
		return *this;
	}

	ParticleParameterPack ParticleParameterPack::Deserialize(const char* label, const JSONObject& parentObject)
	{
		ParticleParameterPack result;
		result.name = std::string(label);
		result.valueType = GetSystem<ParticleManager>(SystemType::PARTICLE_MANAGER)->GetParameterValueType(label);

		std::vector<JSONObject> members;
		if (parentObject.TryGetObjectArray(label, members) && !members.empty())
		{
			for (const JSONObject& member : members)
			{
				result.params.push_back(ParticleParameter::Deserialize(member, result.valueType));
			}
		}
		else
		{
			JSONObject member;
			if (parentObject.TryGetObject(label, member))
			{
				result.params.push_back(ParticleParameter::Deserialize(member, result.valueType));
			}
			else
			{
				PrintError("Unable to find particle fields in pack %s\n", label);
			}
		}

		if (result.valueType == ParticleParamterValueType::_NONE)
		{
			PrintError("Invalid particle parameter value in pack %s\n", label);
		}

		return result;
	}

	JSONValue ParticleParameterPack::Serialize()
	{
		if (params.size() > 1)
		{
			std::vector<JSONObject> members;
			for (u32 i = 0; i < (u32)params.size(); ++i)
			{
				members.emplace_back(params[i].Serialize(valueType));
			}
			return JSONValue(members);
		}
		else
		{
			return JSONValue(params[0].Serialize(valueType));
		}
	}

	bool ParticleParameterPack::GetBool(u32 index) const
	{
		return params[index].GetBool();
	}

	real ParticleParameterPack::GetReal(u32 index) const
	{
		return params[index].GetReal();
	}

	i32 ParticleParameterPack::GetInt(u32 index) const
	{
		return params[index].GetInt();
	}

	glm::vec2 ParticleParameterPack::GetVec2() const
	{
		return glm::vec2(params[0].GetReal(), params[1].GetReal());
	}

	glm::vec3 ParticleParameterPack::GetVec3() const
	{
		return glm::vec3(params[0].GetReal(), params[1].GetReal(), params[2].GetReal());
	}

	glm::vec4 ParticleParameterPack::GetVec4() const
	{
		return glm::vec4(params[0].GetReal(), params[1].GetReal(), params[2].GetReal(), params[3].GetReal());
	}

	void ParticleParameterPack::DrawImGuiObjects()
	{
		i32 count = (i32)params.size();
		if (count == 1)
		{
			switch (valueType)
			{
			case ParticleParamterValueType::BOOL:
			{
				bool bValue = params[0].GetBool();
				if (ImGui::Checkbox(name.c_str(), &bValue))
				{
					params[0].SetBool(bValue);
				}
			} break;
			case ParticleParamterValueType::FLOAT:
			{
				real value = params[0].GetReal();
				if (ImGui::InputFloat(name.c_str(), &value))
				{
					params[0].SetReal(value);
				}
			} break;
			case ParticleParamterValueType::INT:
			{
				i32 value = params[0].GetInt();
				if (ImGui::InputInt(name.c_str(), &value))
				{
					params[0].SetInt(value);
				}
			} break;
			default:
				ENSURE_NO_ENTRY();
				break;
			}
		}
		else if (count == 2)
		{
			CHECK_EQ(valueType, ParticleParamterValueType::VEC2);

			params[0].DrawImGuiObjects("x");
			params[1].DrawImGuiObjects("y");
		}
		else if (count == 3)
		{
			CHECK_EQ(valueType, ParticleParamterValueType::VEC3);

			params[0].DrawImGuiObjects("x");
			params[1].DrawImGuiObjects("y");
			params[2].DrawImGuiObjects("z");
		}
		else if (count == 4)
		{
			CHECK_EQ(valueType, ParticleParamterValueType::VEC4);

			params[0].DrawImGuiObjects("x");
			params[1].DrawImGuiObjects("y");
			params[2].DrawImGuiObjects("z");
			params[3].DrawImGuiObjects("w");
		}
		else
		{
			ENSURE_NO_ENTRY();
		}
	}

	//
	// ParticleParameters
	//

	ParticleParameters::ParticleParameters(const ParticleParameters& other)
	{
		if (this != &other)
		{
			m_Parameters = std::map<StringID, ParticleParameterPack>(other.m_Parameters);
		}
	}

	ParticleParameters::ParticleParameters(ParticleParameters&& other)
	{
		if (this != &other)
		{
			m_Parameters = std::move(other.m_Parameters);
		}
	}

	ParticleParameters& ParticleParameters::operator=(const ParticleParameters& other)
	{
		if (this != &other)
		{
			m_Parameters = std::map<StringID, ParticleParameterPack>(other.m_Parameters);
		}
		return *this;
	}

	ParticleParameters& ParticleParameters::operator=(ParticleParameters&& other)
	{
		if (this != &other)
		{
			m_Parameters = std::move(other.m_Parameters);
		}
		return *this;
	}

	void ParticleParameters::Serialize(JSONObject& parentObject)
	{
		for (auto& pair : m_Parameters)
		{
			parentObject.fields.emplace_back(pair.second.name.c_str(), pair.second.Serialize());
		}
	}

	void ParticleParameters::Deserialize(const JSONObject& parentObject)
	{
		m_Parameters.clear();
		for (const JSONField& field : parentObject.fields)
		{
			StringID paramNameSID = Hash(field.label.c_str());
			m_Parameters.emplace(paramNameSID, ParticleParameterPack::Deserialize(field.label.c_str(), parentObject));
		}
	}

	bool ParticleParameters::Deserialize(const std::string& filePath, ParticleParameters& particleParameters)
	{
		if (FileExists(filePath))
		{
			std::string fileContents;
			if (!ReadFile(filePath, fileContents, false))
			{
				PrintError("Failed to read particle system file at %s\n", filePath.c_str());
				return false;
			}

			JSONObject particleSystemObj;
			if (!JSONParser::Parse(fileContents, particleSystemObj))
			{
				PrintError("Failed to parse particle system file at %s\n", filePath.c_str());
				return false;
			}

			particleParameters.Deserialize(particleSystemObj);
			return true;
		}

		return false;
	}

	void ParticleParameters::Serialize(const std::string& filePath, ParticleParameters& particleParameters)
	{
		Platform::CreateDirectoryRecursive(RelativePathToAbsolute(PARTICLE_SYSTEMS_DIRECTORY));

		JSONObject particleSystemObj = {};
		particleParameters.Serialize(particleSystemObj);

		std::string fileContents = particleSystemObj.ToString();
		if (!WriteFile(filePath, fileContents, false))
		{
			PrintError("Failed to write particle system %s to disk\n", filePath.c_str());
		}
	}

	bool ParticleParameters::GetBool(StringID parameterName) const
	{
		auto iter = m_Parameters.find(parameterName);
		if (iter != m_Parameters.end())
		{
			return iter->second.GetBool(0);
		}
		return false;
	}

	real ParticleParameters::GetReal(StringID parameterName) const
	{
		auto iter = m_Parameters.find(parameterName);
		if (iter != m_Parameters.end())
		{
			return iter->second.GetReal(0);
		}
		return -1.0f;
	}

	i32 ParticleParameters::GetInt(StringID parameterName) const
	{
		auto iter = m_Parameters.find(parameterName);
		if (iter != m_Parameters.end())
		{
			return iter->second.GetInt(0);
		}
		return -1;
	}

	glm::vec2 ParticleParameters::GetVec2(StringID parameterName) const
	{
		auto iter = m_Parameters.find(parameterName);
		if (iter != m_Parameters.end())
		{
			return iter->second.GetVec2();
		}
		return VEC2_NEG_ONE;
	}

	glm::vec3 ParticleParameters::GetVec3(StringID parameterName) const
	{
		auto iter = m_Parameters.find(parameterName);
		if (iter != m_Parameters.end())
		{
			return iter->second.GetVec3();
		}
		return VEC3_NEG_ONE;
	}

	glm::vec4 ParticleParameters::GetVec4(StringID parameterName) const
	{
		auto iter = m_Parameters.find(parameterName);
		if (iter != m_Parameters.end())
		{
			return iter->second.GetVec4();
		}
		return VEC4_NEG_ONE;
	}

	void ParticleParameters::DrawImGuiObjects()
	{
		ImGui::PushID(this);

		for (auto iter = m_Parameters.begin(); iter != m_Parameters.end(); ++iter)
		{
			ParticleParameterPack& paramPack = iter->second;

			if (ImGui::TreeNode(paramPack.name.c_str()))
			{
				paramPack.DrawImGuiObjects();

				bool bRemoved = false;
				if (ImGui::Button("Remove param"))
				{
					m_Parameters.erase(iter);
					bRemoved = true;
				}

				ImGui::TreePop();

				if (bRemoved)
				{
					break;
				}
			}
		}

		ImGui::PopID();
	}

	//
	// Particle System
	//

	ParticleSystem::ParticleSystem() :
		m_Parameters({})
	{
	}

	void ParticleSystem::SetParameters(const ParticleParameters& params)
	{
		memcpy(&m_Parameters, &params, sizeof(ParticleParameters));
	}

	const ParticleParameters& ParticleSystem::GetParameters()
	{
		return m_Parameters;
	}

	void ParticleSystem::Destroy()
	{
		GetSystem<ParticleManager>(SystemType::PARTICLE_MANAGER)->RemoveParticleSystem(ID);
	}

	void ParticleSystem::Deserialize()
	{
		ParticleParameters::Deserialize(GetFilePath(), m_Parameters);
	}

	void ParticleSystem::Serialize()
	{
		ParticleParameters::Serialize(GetFilePath(), m_Parameters);
	}

	std::string ParticleSystem::GetFilePath() const
	{
		return PARTICLE_SYSTEMS_DIRECTORY + name + ".json";
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

		ImGui::Text("%s", name.c_str());

		ImGui::Checkbox("Enabled", &bEnabled);

		ImGui::ColorEdit4("Colour 0", &data.colour0.r, colorEditFlags);
		ImGui::SameLine();
		ImGui::ColorEdit4("Colour 1", &data.colour1.r, colorEditFlags);
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
		DiscoverParameterTypes();
		DiscoverTemplates();
	}

	void ParticleManager::Destroy()
	{
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
		bool* bWindowShowing = g_EngineInstance->GetUIWindowOpen(SID_PAIR("particle manager"));
		if (*bWindowShowing)
		{
			if (ImGui::Begin("Particle Manager", bWindowShowing))
			{
				if (ImGui::Button("Serialize all templates"))
				{
					SerializeAllTemplates();
				}

				ImGui::SameLine();

				if (ImGui::Button("Discover templates"))
				{
					DiscoverTemplates();
				}

				static const char* newParticleSystemPopup = "new particle system";
				if (ImGui::Button("New particle system template"))
				{
					ImGui::OpenPopup(newParticleSystemPopup);
				}

				if (ImGui::BeginPopupModal(newParticleSystemPopup))
				{
					static char newParticleSystemNameBuff[256];
					bool bCreateNewSystem = ImGui::InputText("New particle system", newParticleSystemNameBuff, 256, ImGuiInputTextFlags_EnterReturnsTrue);

					bCreateNewSystem = ImGui::Button("Create") || bCreateNewSystem;

					if (bCreateNewSystem)
					{
						ParticleSystemTemplate particleTemplate = {};
						std::string newNameStr(newParticleSystemNameBuff);
						particleTemplate.filePath = PARTICLE_SYSTEMS_DIRECTORY + newNameStr + ".json";
						m_ParticleTemplates.emplace(Hash(newNameStr.c_str()), particleTemplate);

						ImGui::CloseCurrentPopup();
					}

					ImGui::EndPopup();
				}

				ImGui::Separator();

				if (ImGui::TreeNode("Active systems"))
				{
					for (auto& pair : m_ParticleSystems)
					{
						pair.second->DrawImGuiObjects();
					}
					ImGui::TreePop();
				}

				ImGui::Separator();

				if (ImGui::TreeNode("Templates"))
				{
					for (auto iter = m_ParticleTemplates.begin(); iter != m_ParticleTemplates.end(); ++iter)
					{
						if (iter->second.DrawImGuiObjects())
						{
							Platform::DeleteFile(iter->second.filePath);
							m_ParticleTemplates.erase(iter);
							break;
						}
					}
					ImGui::TreePop();
				}

				if (ImGui::TreeNode("Parameter types"))
				{
					if (ImGui::Button(m_bParameterTypesDirty ? "Save to file*" : "Save to file"))
					{
						SerializeParameterTypes();
					}

					ImGui::SameLine();

					if (ImGui::Button("Reload from file"))
					{
						DiscoverParameterTypes();
					}

					for (auto iter = m_ParameterTypes.begin(); iter != m_ParameterTypes.end(); ++iter)
					{
						const char* typeName = iter->name.c_str();
						ImGui::PushID(typeName);

						ImGui::Text("%s", typeName);

						i32 valueTypeIndex = (i32)iter->valueType;
						if (ImGui::Combo("", &valueTypeIndex, ParticleParamterValueTypeStrings, ARRAY_LENGTH(ParticleParamterValueTypeStrings) - 1))
						{
							iter->valueType = (ParticleParamterValueType)valueTypeIndex;
							m_bParameterTypesDirty = true;
						}

						ImGui::SameLine();

						bool bRemoved = false;
						if (ImGui::Button("x"))
						{
							m_ParameterTypes.erase(iter);
							m_bParameterTypesDirty = true;
							bRemoved = true;
						}

						ImGui::PopID();

						if (bRemoved)
						{
							break;
						}
					}

					static const char* addNewTypePopup = "New particle parameter type";
					if (ImGui::Button("Add new type"))
					{
						ImGui::OpenPopup(addNewTypePopup);
					}

					if (ImGui::BeginPopupModal(addNewTypePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
					{
						static char typeNameBuff[256];
						bool bCreateNewType = ImGui::InputText("Name", typeNameBuff, 256, ImGuiInputTextFlags_EnterReturnsTrue);

						static i32 typeIndex = (i32)ParticleParamterValueType::FLOAT;
						ImGui::Combo("Type", &typeIndex, ParticleParamterValueTypeStrings, ARRAY_LENGTH(ParticleParamterValueTypeStrings) - 1);

						bCreateNewType = ImGui::Button("Create") || bCreateNewType;

						if (bCreateNewType && typeIndex != (i32)ParticleParamterValueType::_NONE)
						{
							std::string typeNameStr(typeNameBuff);
							m_ParameterTypes.emplace_back(typeNameStr, (ParticleParamterValueType)typeIndex);
							m_bParameterTypesDirty = true;
							ImGui::CloseCurrentPopup();
						}

						ImGui::EndPopup();
					}

					ImGui::TreePop();
				}
			}
			ImGui::End();
		}
	}

	ParticleSystemID ParticleManager::AddParticleSystem(StringID particleTemplateNameSID, const ParticleSimData& data, const std::string& name, const glm::mat4& objectToWorld, real lifetime)
	{
		ParticleParameters* params = GetTemplateParams(particleTemplateNameSID);
		if (params == nullptr)
		{
			PrintError("Invalid particle template name SID: %u\n", (u32)particleTemplateNameSID);
			return InvalidParticleSystemID;
		}

		ParticleSystem* newParticleSystem = m_ParticleSystemAllocator.Alloc();
		newParticleSystem->name = name;
		newParticleSystem->bEnabled = true;
		newParticleSystem->objectToWorld = objectToWorld;
		newParticleSystem->lifetimeRemaining = lifetime;

		newParticleSystem->SetParameters(*params);

		newParticleSystem->data.colour0 = data.colour0;
		newParticleSystem->data.colour1 = data.colour1;
		newParticleSystem->data.particleCount = data.particleCount;

		ParticleSystemID particleSystemID = g_Renderer->AddParticleSystem(name, newParticleSystem, data.particleCount);
		newParticleSystem->ID = particleSystemID;
		m_ParticleSystems.emplace(particleSystemID, newParticleSystem);
		return particleSystemID;
	}

	void ParticleManager::RemoveParticleSystem(ParticleSystemID particleSystemID)
	{
		m_ParticleSystems.erase(particleSystemID);
		g_Renderer->RemoveParticleSystem(particleSystemID);
	}

	void ParticleManager::DiscoverParameterTypes()
	{
		m_ParameterTypes.clear();

		const char* filePath = PARTICLE_PARAMETER_TYPES_LOCATION;
		if (FileExists(filePath))
		{
			std::string fileContents;
			if (!ReadFile(filePath, fileContents, false))
			{
				PrintError("Failed to read particle parameter types file at %s\n", filePath);
				return;
			}

			JSONObject rootObj;
			if (!JSONParser::Parse(fileContents, rootObj))
			{
				PrintError("Failed to parse particle parameter types file at %s\n", filePath);
				return;
			}

			std::vector<JSONObject> typeObjs = rootObj.GetObjectArray("particle parameter types");
			for (const JSONObject& typeObj : typeObjs)
			{
				std::string label = typeObj.fields[0].label;
				std::string typeStr = typeObj.fields[0].value.AsString();
				ParticleParamterValueType type = ParticleParamterValueTypeFromString(typeStr.c_str());

				if (type != ParticleParamterValueType::_NONE)
				{
					m_ParameterTypes.emplace_back(label, type);
				}
			}

			m_bParameterTypesDirty = false;
		}
	}

	void ParticleManager::SerializeParameterTypes()
	{
		std::vector<JSONObject> parameterTypeObjs;
		for (ParticleParameterType& type : m_ParameterTypes)
		{
			const char* valueStr = ParticleParamterValueTypeToString(type.valueType);
			JSONObject obj = {};
			obj.fields.emplace_back(type.name, JSONValue(valueStr));
			parameterTypeObjs.emplace_back(obj);
		}

		JSONObject particleParameterTypesObj = {};
		particleParameterTypesObj.fields.emplace_back("particle parameter types", JSONValue(parameterTypeObjs));
		std::string fileContents = particleParameterTypesObj.ToString();

		const char* filePath = PARTICLE_PARAMETER_TYPES_LOCATION;
		if (!WriteFile(filePath, fileContents, false))
		{
			PrintError("Failed to write particle parameter types to %s\n", filePath);
		}
		else
		{
			m_bParameterTypesDirty = false;
		}
	}

	void ParticleManager::SerializeAllTemplates()
	{
		for (auto& pair : m_ParticleTemplates)
		{
			ParticleParameters::Serialize(pair.second.filePath, pair.second.params);
		}
	}

	void ParticleManager::DiscoverTemplates()
	{
		m_ParticleTemplates.clear();

		std::string absoluteDirectory = RelativePathToAbsolute(PARTICLE_SYSTEMS_DIRECTORY);
		if (Platform::DirectoryExists(absoluteDirectory))
		{
			std::vector<std::string> filePaths;
			if (Platform::FindFilesInDirectory(absoluteDirectory, filePaths, ".json"))
			{
				for (const std::string& filePath : filePaths)
				{
					ParticleSystemTemplate particleTemplate = {};
					particleTemplate.filePath = filePath;
					if (!ParticleParameters::Deserialize(filePath, particleTemplate.params))
					{
						PrintError("Failed to read particle parameters file at %s\n", filePath.c_str());
						continue;
					}

					std::string fileName = StripLeadingDirectories(StripFileType(filePath));
					StringID particleNameSID = SID(fileName.c_str());
					m_ParticleTemplates.emplace(particleNameSID, particleTemplate);
				}
			}
		}
	}

	ParticleParamterValueType ParticleManager::GetParameterValueType(const char* paramName)
	{
		for (ParticleParameterType& type : m_ParameterTypes)
		{
			if (strcmp(type.name.c_str(), paramName) == 0)
			{
				return type.valueType;
			}
		}
		return ParticleParamterValueType::_NONE;
	}

	ParticleParameters* ParticleManager::GetTemplateParams(StringID particleTemplateSID)
	{
		auto iter = m_ParticleTemplates.find(particleTemplateSID);
		if (iter != m_ParticleTemplates.end())
		{
			return &iter->second.params;
		}
		return nullptr;
	}

	//
	// ParticleSystemTemplate
	//

	bool ParticleManager::ParticleSystemTemplate::DrawImGuiObjects()
	{
		bool bRemove = false;

		std::string fileName = StripLeadingDirectories(StripFileType(filePath));
		if (ImGui::TreeNode(this, "%s", fileName.c_str()))
		{
			if (ImGui::Button("Delete"))
			{
				bRemove = true;
			}

			ImGui::SameLine();

			if (ImGui::Button("Save"))
			{
				ParticleParameters::Serialize(filePath, params);
			}

			params.DrawImGuiObjects();

			const char* addParameterPopup = "Add parameter";
			if (ImGui::Button("Add parameter"))
			{
				ImGui::OpenPopup(addParameterPopup);
			}

			if (ImGui::BeginPopupModal(addParameterPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ParticleManager* particleManager = GetSystem<ParticleManager>(SystemType::PARTICLE_MANAGER);
				static i32 paramIndex = 0;
				ImGui::Combo("Parameter", &paramIndex, [](void* data, i32 index, const char** outText)
				{
					ParticleParameterType* type = (((ParticleParameterType*)data) + index);
					*outText = type->name.c_str();
					return true;
				}, particleManager->m_ParameterTypes.data(), (i32)particleManager->m_ParameterTypes.size());

				if (ImGui::Button("Add"))
				{
					ParticleParameterType& paramType = particleManager->m_ParameterTypes[paramIndex];
					StringID paramNameSID = SID(paramType.name.c_str());
					params.m_Parameters.emplace(paramNameSID, ParticleParameterPack(paramType.name, paramType.valueType));
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			ImGui::TreePop();
		}

		return bRemove;
	}

} // namespace flex
