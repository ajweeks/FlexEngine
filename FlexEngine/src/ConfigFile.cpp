#include "stdafx.hpp"

#include "ConfigFile.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtx/euler_angles.hpp>
IGNORE_WARNINGS_POP

#include "ConfigFileManager.hpp"
#include "Helpers.hpp"
#include "JSONParser.hpp"

namespace flex
{
	ConfigFile::ConfigFile(const std::string& name, const std::string& filePath, i32 currentFileVersion) :
		name(name),
		filePath(filePath),
		fileVersion(currentFileVersion),
		currentFileVersion(currentFileVersion)
	{
		g_ConfigFileManager->RegisterConfigFile(this);
	}

	ConfigFile::~ConfigFile()
	{
		g_ConfigFileManager->DeregisterConfigFile(this);
	}

	using EmplaceResult = std::pair<std::map<const char*, ConfigFile::ConfigValue>::iterator, bool>;

	ConfigFile::ConfigValue& ConfigFile::RegisterProperty(const char* propertyName, real* propertyValue)
	{
		EmplaceResult pair = values.emplace(propertyName, ConfigValue(propertyName, propertyValue, ValueType::FLOAT));
		return pair.first->second;
	}

	ConfigFile::ConfigValue& ConfigFile::RegisterProperty(const char* propertyName, i32* propertyValue)
	{
		EmplaceResult pair = values.emplace(propertyName, ConfigValue(propertyName, propertyValue, ValueType::INT));
		return pair.first->second;
	}

	ConfigFile::ConfigValue& ConfigFile::RegisterProperty(const char* propertyName, u32* propertyValue)
	{
		EmplaceResult pair = values.emplace(propertyName, ConfigValue(propertyName, propertyValue, ValueType::UINT));
		return pair.first->second;
	}

	ConfigFile::ConfigValue& ConfigFile::RegisterProperty(const char* propertyName, bool* propertyValue)
	{
		EmplaceResult pair = values.emplace(propertyName, ConfigValue(propertyName, propertyValue, ValueType::BOOL));
		return pair.first->second;
	}

	ConfigFile::ConfigValue& ConfigFile::RegisterProperty(const char* propertyName, glm::vec2* propertyValue)
	{
		EmplaceResult pair = values.emplace(propertyName, ConfigValue(propertyName, propertyValue, ValueType::VEC2));
		return pair.first->second;
	}

	ConfigFile::ConfigValue& ConfigFile::RegisterProperty(const char* propertyName, glm::vec3* propertyValue)
	{
		EmplaceResult pair = values.emplace(propertyName, ConfigValue(propertyName, propertyValue, ValueType::VEC3));
		return pair.first->second;
	}

	ConfigFile::ConfigValue& ConfigFile::RegisterProperty(const char* propertyName, glm::vec4* propertyValue)
	{
		EmplaceResult pair = values.emplace(propertyName, ConfigValue(propertyName, propertyValue, ValueType::VEC4));
		return pair.first->second;
	}

	ConfigFile::ConfigValue& ConfigFile::RegisterProperty(const char* propertyName, glm::quat* propertyValue)
	{
		EmplaceResult pair = values.emplace(propertyName, ConfigValue(propertyName, propertyValue, ValueType::QUAT));
		return pair.first->second;
	}

	ConfigFile::ConfigValue& ConfigFile::RegisterProperty(const char* propertyName, std::string* propertyValue)
	{
		EmplaceResult pair = values.emplace(propertyName, ConfigValue(propertyName, propertyValue, ValueType::STRING));
		return pair.first->second;
	}

	ConfigFile::ConfigValue& ConfigFile::RegisterProperty(const char* propertyName, GUID* propertyValue)
	{
		EmplaceResult pair = values.emplace(propertyName, ConfigValue(propertyName, propertyValue, ValueType::GUID));
		return pair.first->second;
	}

	bool ConfigFile::Serialize()
	{
		fileVersion = currentFileVersion;

		JSONObject rootObject = {};

		rootObject.fields.emplace_back("version", JSONValue(fileVersion));

		for (std::pair<const char* const, ConfigValue>& valuePair : values)
		{
			CHECK_NE(valuePair.second.valuePtr, nullptr);

			rootObject.fields.emplace_back(valuePair.first, JSONValue::FromRawPtr(valuePair.second.valuePtr, valuePair.second.type));
		}

		std::string fileContents = rootObject.ToString();

		if (!WriteFile(filePath, fileContents, false))
		{
			PrintError("Failed to write config file to %s\n", filePath.c_str());
			return false;
		}

		bDirty = false;

		return true;
	}

	bool ConfigFile::Deserialize()
	{
		if (FileExists(filePath))
		{
			std::string fileContents;
			if (ReadFile(filePath, fileContents, false))
			{
				JSONObject rootObject;
				if (JSONParser::Parse(fileContents, rootObject))
				{
					if (!rootObject.TryGetValueOfType("version", &fileVersion, ValueType::INT))
					{
						fileVersion = currentFileVersion;
					}

					for (std::pair<const char* const, ConfigValue>& valuePair : values)
					{
						CHECK_NE(valuePair.second.valuePtr, nullptr);

						rootObject.TryGetValueOfType(valuePair.second.label, valuePair.second.valuePtr, valuePair.second.type);
					}

					if (onDeserializeCallback != nullptr)
					{
						onDeserializeCallback();
					}

					bDirty = false;

					return true;
				}
				else
				{
					PrintError("Failed to parse config file at %s\n", filePath.c_str());
				}
			}
		}

		return false;
	}

	void ConfigFile::SetOnDeserialize(std::function<void()> callback)
	{
		onDeserializeCallback = callback;
	}

	ConfigFile::ConfigValue& ConfigFile::ConfigValue::SetRange(real rangeMin, real rangeMax)
	{
		CHECK(rangeMin < rangeMax);
		valueMin = *(void**)&rangeMin;
		valueMax = *(void**)&rangeMax;
		valueMinSet = 1;
		valueMaxSet = 1;

		return *this;
	}

	ConfigFile::Request ConfigFile::DrawImGuiObjects()
	{
		Request result = Request::NONE;

		if (ImGui::TreeNode(name.c_str()))
		{
			ImGui::Text("Version: %i", fileVersion);

			for (std::pair<const char* const, ConfigValue>& valuePair : values)
			{
				if (valuePair.second.DrawImGui())
				{
					bDirty = true;
				}
			}

			if (ImGui::Button(bDirty ? "Save*" : "Save"))
			{
				result = Request::SERIALIZE;
			}

			ImGui::SameLine();

			if (ImGui::Button("Reload"))
			{
				result = Request::RELOAD;
			}

			ImGui::TreePop();
		}

		return result;
	}

	//
	// ConfigValue
	//

	bool ConfigFile::ConfigValue::DrawImGui()
	{
		return DrawImGuiForValueType(valuePtr, label, type, valueMinSet, valueMaxSet, valueMin, valueMax);
	}

} // namespace flex
