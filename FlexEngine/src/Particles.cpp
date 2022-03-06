
#include "stdafx.hpp"

#include "Particles.hpp"

#include "FlexEngine.hpp"
#include "Graphics/Renderer.hpp"
#include "Graphics/RendererTypes.hpp"
#include "ResourceManager.hpp"
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

	bool IsValueTypeFloat(ParticleParamterValueType valueType)
	{
		return valueType == ParticleParamterValueType::FLOAT ||
			valueType == ParticleParamterValueType::VEC2 ||
			valueType == ParticleParamterValueType::VEC3 ||
			valueType == ParticleParamterValueType::VEC4 ||
			valueType == ParticleParamterValueType::COL3 ||
			valueType == ParticleParamterValueType::COL4;
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

	real ParticleParameter::GetRealMin() const
	{
		switch (type)
		{
		case ParticleSampleType::CONSTANT:
			return constantValue;
		case ParticleSampleType::RANDOM:
			return randomRange.first;
		default:
			ENSURE_NO_ENTRY();
			return -1.0f;
		}
	}

	real ParticleParameter::GetRealMax() const
	{
		switch (type)
		{
		case ParticleSampleType::CONSTANT:
			return constantValue;
		case ParticleSampleType::RANDOM:
			return randomRange.second;
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

	i32 ParticleParameter::GetIntMin() const
	{
		switch (type)
		{
		case ParticleSampleType::CONSTANT:
			return (i32)constantValue;
		case ParticleSampleType::RANDOM:
			return (i32)randomRange.first;
		default:
			ENSURE_NO_ENTRY();
			return -1;
		}
	}

	i32 ParticleParameter::GetIntMax() const
	{
		switch (type)
		{
		case ParticleSampleType::CONSTANT:
			return (i32)constantValue;
		case ParticleSampleType::RANDOM:
			return (i32)randomRange.second;
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
			case ParticleParamterValueType::COL3:
			case ParticleParamterValueType::COL4:
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
			case ParticleParamterValueType::COL3:
			case ParticleParamterValueType::COL4:
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
			ENSURE_NO_ENTRY();
			PrintError("Parsed invalid particle parameter type: %s\n", typeStr.c_str());
			break;
		}

		return result;
	}

	bool ParticleParameter::DrawImGuiObjects(const char* label, ParticleParamterValueType parentValueType)
	{
		ImGui::PushID(this);

		bool bValueChanged = false;

		i32 typeInt = (i32)type;
		if (ImGui::Combo("Type", &typeInt, ParticleSampleTypeStrings, ARRAY_LENGTH(ParticleSampleTypeStrings) - 1))
		{
			type = (ParticleSampleType)typeInt;
			bValueChanged = true;
		}

		switch (type)
		{
		case ParticleSampleType::CONSTANT:
		{
			switch (parentValueType)
			{
			case ParticleParamterValueType::INT:
			{
				i32 intValue = GetInt();
				if (ImGui::InputInt(label, &intValue))
				{
					SetInt(intValue);
					bValueChanged = true;
				}
			} break;
			case ParticleParamterValueType::BOOL:
			{
				bool bValue = GetBool();
				if (ImGui::Checkbox(label, &bValue))
				{
					SetBool(bValue);
					bValueChanged = true;
				}
			} break;
			default:
			{
				if (IsValueTypeFloat(parentValueType))
				{
					bValueChanged = ImGui::InputFloat(label, &constantValue) || bValueChanged;
				}
				else
				{
					ENSURE_NO_ENTRY();
				}
			} break;
			}
		} break;
		case ParticleSampleType::RANDOM:
		{
			switch (parentValueType)
			{
			case ParticleParamterValueType::INT:
			{
				i32 intValueMin = (i32)randomRange.first;
				i32 intValueMax = (i32)randomRange.second;
				if (ImGui::DragIntRange2(label, &intValueMin, &intValueMax))
				{
					randomRange.first = (real)intValueMin;
					randomRange.second = (real)intValueMax;
					bValueChanged = true;
				}
			} break;
			default:
			{
				if (IsValueTypeFloat(parentValueType))
				{
					bValueChanged = ImGui::DragFloatRange2(label, &randomRange.first, &randomRange.second) || bValueChanged;
				}
				else
				{
					ENSURE_NO_ENTRY();
				}
			} break;
			}
		} break;
		default:
			ENSURE_NO_ENTRY();
			break;
		}

		ImGui::PopID();

		return bValueChanged;
	}

	//
	// ParticleParameterPack
	//

	ParticleParameterPack::ParticleParameterPack(const std::string& name, ParticleParamterValueType valueType) :
		name(name),
		valueType(valueType)
	{
		nameSID = SID(name.c_str());

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
		case ParticleParamterValueType::COL3:
			params.resize(3, {});
			break;
		case ParticleParamterValueType::VEC4:
		case ParticleParamterValueType::COL4:
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
			nameSID = other.nameSID;
			valueType = other.valueType;
			params = std::vector<ParticleParameter>(other.params);
		}
	}

	ParticleParameterPack::ParticleParameterPack(ParticleParameterPack&& other)
	{
		if (this != &other)
		{
			name = std::move(other.name);
			nameSID = other.nameSID;
			valueType = other.valueType;
			params = std::move(other.params);
		}
	}

	ParticleParameterPack& ParticleParameterPack::operator=(const ParticleParameterPack& other)
	{
		if (this != &other)
		{
			name = other.name;
			nameSID = other.nameSID;
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
			nameSID = other.nameSID;
			valueType = other.valueType;
			params = std::move(other.params);
		}
		return *this;
	}

	ParticleParameterPack ParticleParameterPack::Deserialize(const char* label, const JSONObject& parentObject)
	{
		ParticleParameterPack result;
		result.name = std::string(label);
		result.valueType = g_ResourceManager->GetParticleParameterValueType(label);
		result.nameSID = SID(label);

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

	ParticleSampleType ParticleParameterPack::GetSampleType(u32 index /* = 0 */) const
	{
		return params[index].type;
	}

	bool ParticleParameterPack::GetBool(u32 index /* = 0 */) const
	{
		return params[index].GetBool();
	}

	real ParticleParameterPack::GetReal(u32 index /* = 0 */) const
	{
		return params[index].GetReal();
	}

	real ParticleParameterPack::GetRealMin(u32 index /* = 0 */) const
	{
		return params[index].GetRealMin();
	}

	real ParticleParameterPack::GetRealMax(u32 index /* = 0 */) const
	{
		return params[index].GetRealMax();
	}

	i32 ParticleParameterPack::GetInt(u32 index /* = 0 */) const
	{
		return params[index].GetInt();
	}

	i32 ParticleParameterPack::GetIntMin(u32 index /* = 0 */) const
	{
		return params[index].GetIntMin();
	}

	i32 ParticleParameterPack::GetIntMax(u32 index /* = 0 */) const
	{
		return params[index].GetIntMax();
	}

	glm::vec2 ParticleParameterPack::GetVec2() const
	{
		return glm::vec2(params[0].GetReal(), params[1].GetReal());
	}

	glm::vec2 ParticleParameterPack::GetVec2Min() const
	{
		return glm::vec2(params[0].GetRealMin(), params[1].GetRealMin());
	}

	glm::vec2 ParticleParameterPack::GetVec2Max() const
	{
		return glm::vec2(params[0].GetRealMax(), params[1].GetRealMax());
	}

	glm::vec3 ParticleParameterPack::GetVec3() const
	{
		return glm::vec3(params[0].GetReal(), params[1].GetReal(), params[2].GetReal());
	}

	glm::vec3 ParticleParameterPack::GetVec3Min() const
	{
		return glm::vec3(params[0].GetRealMin(), params[1].GetRealMin(), params[2].GetRealMin());
	}

	glm::vec3 ParticleParameterPack::GetVec3Max() const
	{
		return glm::vec3(params[0].GetRealMax(), params[1].GetRealMax(), params[2].GetRealMax());
	}

	glm::vec4 ParticleParameterPack::GetVec4() const
	{
		return glm::vec4(params[0].GetReal(), params[1].GetReal(), params[2].GetReal(), params[3].GetReal());
	}

	glm::vec4 ParticleParameterPack::GetVec4Min() const
	{
		return glm::vec4(params[0].GetRealMin(), params[1].GetRealMin(), params[2].GetRealMin(), params[3].GetRealMin());
	}

	glm::vec4 ParticleParameterPack::GetVec4Max() const
	{
		return glm::vec4(params[0].GetRealMax(), params[1].GetRealMax(), params[2].GetRealMax(), params[3].GetRealMax());
	}

	bool ParticleParameterPack::DrawImGuiObjects()
	{
		i32 count = (i32)params.size();
		if (count == 1)
		{
			bool bValueChanged = params[0].DrawImGuiObjects(name.c_str(), valueType);
			return bValueChanged;
		}
		else if (count == 2)
		{
			CHECK_EQ(valueType, ParticleParamterValueType::VEC2);

			bool bValueChanged = false;
			bValueChanged = params[0].DrawImGuiObjects("x", valueType) || bValueChanged;
			bValueChanged = params[1].DrawImGuiObjects("y", valueType) || bValueChanged;
			return bValueChanged;
		}
		else if (count == 3)
		{
			CHECK(valueType == ParticleParamterValueType::VEC3 ||
				valueType == ParticleParamterValueType::COL3);

			if (valueType == ParticleParamterValueType::VEC3)
			{
				bool bValueChanged = false;
				bValueChanged = params[0].DrawImGuiObjects("x", valueType) || bValueChanged;
				bValueChanged = params[1].DrawImGuiObjects("y", valueType) || bValueChanged;
				bValueChanged = params[2].DrawImGuiObjects("z", valueType) || bValueChanged;
				return bValueChanged;
			}
			else
			{
				glm::vec3 col(params[0].GetReal(), params[1].GetReal(), params[2].GetReal());
				if (ImGui::ColorEdit3("", &col.x))
				{
					params[0].SetReal(col.x);
					params[1].SetReal(col.y);
					params[2].SetReal(col.z);
					return true;
				}
				return false;
			}
		}
		else if (count == 4)
		{
			CHECK(valueType == ParticleParamterValueType::VEC4 ||
				valueType == ParticleParamterValueType::COL4);

			if (valueType == ParticleParamterValueType::VEC4)
			{
				bool bValueChanged = false;
				bValueChanged = params[0].DrawImGuiObjects("x", valueType) || bValueChanged;
				bValueChanged = params[1].DrawImGuiObjects("y", valueType) || bValueChanged;
				bValueChanged = params[2].DrawImGuiObjects("z", valueType) || bValueChanged;
				bValueChanged = params[3].DrawImGuiObjects("w", valueType) || bValueChanged;
				return bValueChanged;
			}
			else
			{
				glm::vec4 col(params[0].GetReal(), params[1].GetReal(), params[2].GetReal(), params[3].GetReal());
				if (ImGui::ColorEdit4("", &col.x))
				{
					params[0].SetReal(col.x);
					params[1].SetReal(col.y);
					params[2].SetReal(col.z);
					params[3].SetReal(col.w);
					return true;
				}
				return false;
			}
		}
		else
		{
			ENSURE_NO_ENTRY();
		}
		return false;
	}

	//
	// ParticleParameters
	//

	ParticleParameters::ParticleParameters(const ParticleParameters& other)
	{
		if (this != &other)
		{
			m_Parameters = std::vector<ParticleParameterPack>(other.m_Parameters);
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
			m_Parameters = std::vector<ParticleParameterPack>(other.m_Parameters);
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
		for (ParticleParameterPack& paramPack : m_Parameters)
		{
			parentObject.fields.emplace_back(paramPack.name.c_str(), paramPack.Serialize());
		}
	}

	void ParticleParameters::Deserialize(const JSONObject& parentObject)
	{
		m_Parameters.clear();
		for (const JSONField& field : parentObject.fields)
		{
			m_Parameters.emplace_back(ParticleParameterPack::Deserialize(field.label.c_str(), parentObject));
		}
		SortParams();
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

	void ParticleParameters::AddParam(const ParticleParameterPack& paramPack)
	{
		m_Parameters.emplace_back(paramPack);
		SortParams();
	}

	ParticleParameterPack const* ParticleParameters::GetParam(StringID paramNameSID) const
	{
		for (const ParticleParameterPack& paramPack : m_Parameters)
		{
			if (paramPack.nameSID == paramNameSID)
			{
				return &paramPack;
			}
		}
		return nullptr;
	}

	bool ParticleParameters::DrawImGuiObjects()
	{
		ImGui::PushID(this);

		bool bValueChanged = false;

		for (auto iter = m_Parameters.begin(); iter != m_Parameters.end(); ++iter)
		{
			ParticleParameterPack& paramPack = *iter;

			if (ImGui::TreeNode(paramPack.name.c_str()))
			{
				bValueChanged = paramPack.DrawImGuiObjects() || bValueChanged;

				bool bRemoved = false;

				ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColour);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColour);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColour);

				if (ImGui::Button("Remove param"))
				{
					m_Parameters.erase(iter);
					bRemoved = true;
				}

				ImGui::PopStyleColor(3);

				ImGui::TreePop();

				if (bRemoved)
				{
					break;
				}
			}
		}

		ImGui::PopID();

		return bValueChanged;
	}

	void ParticleParameters::FillOutGPUParams(ParticleSpawnParams& particleSpawnParams)
	{
		i32 paramIndex = 0;
		for (ParticleParameterPack& paramPack : m_Parameters)
		{
			ParticleParamGPU& param = particleSpawnParams.params[paramIndex];
			param.sampleType = (u32)paramPack.GetSampleType();
			switch (paramPack.valueType)
			{
			case ParticleParamterValueType::FLOAT:
				param.valueMin = glm::vec4(paramPack.GetRealMin(), 0.0f, 0.0f, 0.0f);
				param.valueMax = glm::vec4(paramPack.GetRealMax(), 0.0f, 0.0f, 0.0f);
				break;
			case ParticleParamterValueType::BOOL:
				param.valueMin = glm::vec4(paramPack.GetBool() ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
				param.valueMax = glm::vec4(paramPack.GetBool() ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
				break;
			case ParticleParamterValueType::INT:
				param.valueMin = glm::vec4((real)paramPack.GetIntMin(), 0.0f, 0.0f, 0.0f);
				param.valueMax = glm::vec4((real)paramPack.GetIntMax(), 0.0f, 0.0f, 0.0f);
				break;
			case ParticleParamterValueType::VEC2:
				param.valueMin = glm::vec4(paramPack.GetVec2Min(), 0.0f, 0.0f);
				param.valueMax = glm::vec4(paramPack.GetVec2Max(), 0.0f, 0.0f);
				break;
			case ParticleParamterValueType::VEC3:
			case ParticleParamterValueType::COL3:
				param.valueMin = glm::vec4(paramPack.GetVec3Min(), 0.0f);
				param.valueMax = glm::vec4(paramPack.GetVec3Max(), 0.0f);
				break;
			case ParticleParamterValueType::VEC4:
			case ParticleParamterValueType::COL4:
				param.valueMin = paramPack.GetVec4Min();
				param.valueMax = paramPack.GetVec4Max();
				break;
			default:
				PrintError("Unhandled type in ParticleParameters::FillOutGPUParams\n");
				ENSURE_NO_ENTRY();
				break;
			}

			++paramIndex;
		}
	}

	void ParticleParameters::SortParams()
	{
		std::sort(m_Parameters.begin(), m_Parameters.end(), [](const ParticleParameterPack& lhs, const ParticleParameterPack& rhs)
		{
			return lhs.name.compare(rhs.name) < 0;
		});
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
		m_Parameters = params;
		ParticleParameterPack const* particleCount = m_Parameters.GetParam(SID("particle count"));
		m_ParticleCount = particleCount != nullptr ? particleCount->GetInt() : 0;
	}

	const ParticleParameters& ParticleSystem::GetParameters()
	{
		return m_Parameters;
	}

	void ParticleSystem::Destroy()
	{
		GetSystem<ParticleManager>(SystemType::PARTICLE_MANAGER)->RemoveParticleSystem(SID(name.c_str()));
	}

	ParticleEmitterID ParticleSystem::SpawnEmitterInstance(const glm::mat4& objectToWorld)
	{
		if ((u32)emitterInstances.size() >= (Renderer::MAX_PARTICLE_EMITTER_INSTANCES_PER_SYSTEM - 1))
		{
			PrintWarn("Failed to spawn particle emitter instance, max number of %d already active\n", Renderer::MAX_PARTICLE_EMITTER_INSTANCES_PER_SYSTEM);
			return InvalidParticleEmitterID;
		}

		ParticleParameterPack const* lifetimeParam = m_Parameters.GetParam(SID("lifetime"));
		real emitterLifetime = lifetimeParam != nullptr ? lifetimeParam->GetRealMax() : -1.0f;
		if (emitterLifetime == -1.0f)
		{
			emitterLifetime = 3.0f;
			PrintWarn("Particle template doesn't specify emitter lifetime, using default value of %.2f\n", emitterLifetime);
		}

		ParticleEmitterID emitterID = m_LastEmitterIndex;
		++m_LastEmitterIndex;

		ParticleEmitterInstance emitter = {};
		emitter.data = {};
		if (m_ParticleCount == -1)
		{
			m_ParticleCount = 256;
			PrintWarn("No particle count set in particle system %s, using default value (%i)", name.c_str(), m_ParticleCount);
		}
		emitter.data.particleCount = m_ParticleCount;
		ParticleParameterPack const* enableSpawningParam = m_Parameters.GetParam(SID("enable spawning"));
		bool bEnableRespawning = enableSpawningParam != nullptr ? enableSpawningParam->GetBool() : false;
		emitter.data.enableSpawning = bEnableRespawning ? 1 : 0;
		emitter.objectToWorld = objectToWorld;
		emitter.lifetimeRemaining = emitterLifetime;
		emitter.ID = emitterID;
		emitter.bufferIndex = u32_max;

		m_Parameters.FillOutGPUParams(m_SpawnParams);
		memcpy(emitter.data.spawnParams.params, m_SpawnParams.params, sizeof(m_SpawnParams.params));

		auto iter = emitterInstances.emplace(emitterID, emitter);

		if (!g_Renderer->AddParticleEmitterInstance(ID, emitterID))
		{
			emitterInstances.erase(iter.first);
		}

		return emitterID;
	}

	void ParticleSystem::ExtinguishEmitter(ParticleEmitterID emitterID)
	{
		auto iter = emitterInstances.find(emitterID);
		if (iter != emitterInstances.end())
		{
			ParticleEmitterInstance& emitter = iter->second;
			emitter.data.enableSpawning = 0;
		}
	}

	void ParticleSystem::Deserialize()
	{
		ParticleParameters::Deserialize(GetFilePath(), m_Parameters);
		ParticleParameterPack const* particleCount = m_Parameters.GetParam(SID("particle count"));
		m_ParticleCount = particleCount != nullptr ? particleCount->GetInt() : 0;
	}

	void ParticleSystem::Serialize()
	{
		ParticleParameters::Serialize(GetFilePath(), m_Parameters);
	}

	std::string ParticleSystem::GetFilePath() const
	{
		return PARTICLE_SYSTEMS_DIRECTORY + name + ".json";
	}

	i32 ParticleSystem::GetEmitterInstanceCount() const
	{
		return (i32)emitterInstances.size();
	}

	void ParticleSystem::Update()
	{
		auto iter = emitterInstances.begin();
		while (iter != emitterInstances.end())
		{
			ParticleEmitterInstance& emitter = iter->second;
			emitter.lifetimeRemaining -= g_DeltaTime;
			if (emitter.lifetimeRemaining <= 0.0f)
			{
				g_Renderer->RemoveParticleEmitterInstance(ID, emitter.ID);
				iter = emitterInstances.erase(iter);
			}
			else
			{
				++iter;
			}
		}
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

		ImGui::Text("Instance count: %i", GetEmitterInstanceCount());
	}

	//
	// Particle Manager
	//

	void ParticleManager::Initialize()
	{
	}

	void ParticleManager::Destroy()
	{
		DestroyAllSystems();
	}

	void ParticleManager::Update()
	{
		for (auto& pair : m_ParticleSystems)
		{
			pair.second->Update();
		}
	}

	void ParticleManager::OnPreSceneChange()
	{
		DestroyAllSystems();
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
					g_ResourceManager->SerializeAllParticleSystemTemplates();
				}

				ImGui::SameLine();

				if (ImGui::Button("Discover templates"))
				{
					g_ResourceManager->DiscoverParticleSystemTemplates();
				}

				static const char* newParticleTemplatePopup = "new particle template";
				if (ImGui::Button("New particle system template"))
				{
					ImGui::OpenPopup(newParticleTemplatePopup);
				}

				if (ImGui::BeginPopupModal(newParticleTemplatePopup))
				{
					static char newParticleSystemNameBuff[256];
					bool bCreate = ImGui::InputText("New particle system template", newParticleSystemNameBuff, 256, ImGuiInputTextFlags_EnterReturnsTrue);

					bCreate = ImGui::Button("Create") || bCreate;

					if (bCreate)
					{
						ParticleSystemTemplate particleTemplate = {};
						std::string newNameStr(newParticleSystemNameBuff);
						particleTemplate.filePath = PARTICLE_SYSTEMS_DIRECTORY + newNameStr + ".json";
						g_ResourceManager->AddNewParticleTemplate(SID(newNameStr.c_str()), particleTemplate);

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

				g_ResourceManager->DrawParticleSystemTemplateImGuiObjects();
				g_ResourceManager->DrawParticleParameterTypesImGui();
			}
			ImGui::End();
		}
	}

	ParticleSystemID ParticleManager::CreateParticleSystem(StringID particleTemplateNameSID, const std::string& name)
	{
		ParticleSystemTemplate particleTemplate;
		if (!g_ResourceManager->GetParticleTemplate(particleTemplateNameSID, particleTemplate))
		{
			PrintError("Invalid particle template name SID: %u\n", (u32)particleTemplateNameSID);
			return InvalidParticleSystemID;
		}

		ParticleSystem* newParticleSystem = m_ParticleSystemAllocator.Alloc();
		newParticleSystem->name = name;

		newParticleSystem->SetParameters(particleTemplate.params);

		ParticleSystemID particleSystemID = g_Renderer->AddParticleSystem(name, newParticleSystem);
		newParticleSystem->ID = particleSystemID;
		m_ParticleSystems.emplace(particleTemplateNameSID, newParticleSystem);
		return particleSystemID;
	}

	ParticleSystem* ParticleManager::GetOrCreateParticleSystem(StringID particleTemplateNameSID, const char* particleTemplateName)
	{
		auto iter = m_ParticleSystems.find(particleTemplateNameSID);
		if (iter != m_ParticleSystems.end())
		{
			return iter->second;
		}

		// Lazily create system on-demand
		ParticleSystemTemplate particleTemplate;
		if (g_ResourceManager->GetParticleTemplate(particleTemplateNameSID, particleTemplate))
		{
			CreateParticleSystem(particleTemplateNameSID, std::string(particleTemplateName));
			return m_ParticleSystems[particleTemplateNameSID];
		}
		return nullptr;
	}

	void ParticleManager::RemoveParticleSystem(StringID particleTemplateNameSID)
	{
		ParticleSystemID particleSystemID = m_ParticleSystems[particleTemplateNameSID]->ID;
		m_ParticleSystems.erase(particleTemplateNameSID);
		g_Renderer->RemoveParticleSystem(particleSystemID);
	}

	void ParticleManager::DestroyAllSystems()
	{
		if (g_Renderer != nullptr)
		{
			auto iter = m_ParticleSystems.begin();
			while (iter != m_ParticleSystems.end())
			{
				g_Renderer->RemoveParticleSystem(iter->second->ID);
				iter = m_ParticleSystems.erase(iter);
			}
		}
		m_ParticleSystems.clear();
	}

	//
	// ParticleSystemTemplate
	//

	ParticleSystemTemplate::ImGuiResult ParticleSystemTemplate::DrawImGuiObjects()
	{
		bool bRemove = false;
		bool bValueChanged = false;

		std::string fileName = StripLeadingDirectories(StripFileType(filePath));
		if (ImGui::TreeNode(this, "%s%s", fileName.c_str(), bDirty ? "*" : ""))
		{
			if (ImGui::Button("Delete"))
			{
				bRemove = true;
			}

			ImGui::SameLine();

			if (ImGui::Button("Save"))
			{
				ParticleParameters::Serialize(filePath, params);
				bDirty = false;
			}

			bValueChanged = params.DrawImGuiObjects() || bValueChanged;

			const char* addParameterPopup = "Add parameter";
			if (ImGui::Button("Add parameter"))
			{
				ImGui::OpenPopup(addParameterPopup);
			}

			if (ImGui::BeginPopupModal(addParameterPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			{
				static i32 paramIndex = 0;
				ImGui::Combo("Parameter", &paramIndex, [](void* data, i32 index, const char** outText)
				{
					ParticleParameterType* type = (((ParticleParameterType*)data) + index);
					*outText = type->name.c_str();
					return true;
				}, g_ResourceManager->particleParameterTypes.data(), (i32)g_ResourceManager->particleParameterTypes.size());

				if (ImGui::Button("Add"))
				{
					ParticleParameterType& paramType = g_ResourceManager->particleParameterTypes[paramIndex];
					params.AddParam(ParticleParameterPack(paramType.name, paramType.valueType));
					bDirty = true;
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			ImGui::TreePop();
		}

		if (bRemove)
		{
			return ImGuiResult::REMOVED;
		}
		else if (bValueChanged)
		{
			bDirty = true;
			return ImGuiResult::CHANGED;
		}
		else
		{
			return ImGuiResult::NOTHING;
		}
	}

} // namespace flex
