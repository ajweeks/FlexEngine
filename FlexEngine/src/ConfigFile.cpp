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

	void ConfigFile::RegisterProperty(i32 versionAdded, const char* propertyName, real* propertyValue)
	{
		values.emplace(propertyName, ConfigValue(versionAdded, propertyName, propertyValue, ValueType::FLOAT));
	}

	void ConfigFile::RegisterProperty(i32 versionAdded, const char* propertyName, real* propertyValue, real valueMin, real valueMax)
	{
		values.emplace(propertyName, ConfigValue(versionAdded, propertyName, propertyValue, *(void**)&valueMin, *(void**)&valueMax, ValueType::FLOAT));
	}

	void ConfigFile::RegisterProperty(i32 versionAdded, const char* propertyName, i32* propertyValue)
	{
		values.emplace(propertyName, ConfigValue(versionAdded, propertyName, propertyValue, ValueType::INT));
	}

	void ConfigFile::RegisterProperty(i32 versionAdded, const char* propertyName, u32* propertyValue)
	{
		values.emplace(propertyName, ConfigValue(versionAdded, propertyName, propertyValue, ValueType::UINT));
	}

	void ConfigFile::RegisterProperty(i32 versionAdded, const char* propertyName, bool* propertyValue)
	{
		values.emplace(propertyName, ConfigValue(versionAdded, propertyName, propertyValue, ValueType::BOOL));
	}

	void ConfigFile::RegisterProperty(i32 versionAdded, const char* propertyName, glm::vec2* propertyValue, u32 precision)
	{
		values.emplace(propertyName, ConfigValue(versionAdded, propertyName, propertyValue, *(void**)&precision, ValueType::VEC2));
	}

	void ConfigFile::RegisterProperty(i32 versionAdded, const char* propertyName, glm::vec3* propertyValue, u32 precision)
	{
		values.emplace(propertyName, ConfigValue(versionAdded, propertyName, propertyValue, *(void**)&precision, ValueType::VEC3));
	}

	void ConfigFile::RegisterProperty(i32 versionAdded, const char* propertyName, glm::vec4* propertyValue, u32 precision)
	{
		values.emplace(propertyName, ConfigValue(versionAdded, propertyName, propertyValue, *(void**)&precision, ValueType::VEC4));
	}

	void ConfigFile::RegisterProperty(i32 versionAdded, const char* propertyName, glm::quat* propertyValue, u32 precision)
	{
		values.emplace(propertyName, ConfigValue(versionAdded, propertyName, propertyValue, *(void**)&precision, ValueType::QUAT));
	}

	bool ConfigFile::Serialize()
	{
		fileVersion = currentFileVersion;

		JSONObject rootObject = {};

		rootObject.fields.emplace_back("version", JSONValue(fileVersion));

		for (auto& valuePair : values)
		{
			u32 precision = valuePair.second.precision != nullptr ? *(u32*)&valuePair.second.precision : JSONValue::DEFAULT_FLOAT_PRECISION;
			rootObject.fields.emplace_back(valuePair.first, JSONValue::FromRawPtr(valuePair.second.valuePtr, valuePair.second.type, precision));
		}

		std::string fileContents = rootObject.ToString();

		if (!WriteFile(filePath, fileContents, false))
		{
			PrintError("Failed to write config file to %s\n", filePath.c_str());
			return false;
		}

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

					for (auto& valuePair : values)
					{
						if (!rootObject.TryGetValueOfType(valuePair.second.label, valuePair.second.valuePtr, valuePair.second.type))
						{
							// Don't warn about missing fields which weren't present in old file version
							if (fileVersion >= valuePair.second.versionAdded)
							{
								PrintError("Failed to get property %s from config file %s\n", valuePair.second.label, filePath.c_str());
							}
						}
					}

					if (onDeserializeCallback)
					{
						onDeserializeCallback();
					}

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

	ConfigFile::Request ConfigFile::DrawImGuiObjects()
	{
		Request result = Request::NONE;

		if (ImGui::TreeNode(name.c_str()))
		{
			ImGui::Text("Version: %i", fileVersion);

			for (auto& valuePair : values)
			{
				switch (valuePair.second.type)
				{
				case ValueType::FLOAT:
					if (valuePair.second.valueMin != nullptr && valuePair.second.valueMax != nullptr)
					{
						ImGui::SliderFloat(valuePair.first, (real*)valuePair.second.valuePtr, *(real*)&valuePair.second.valueMin, *(real*)&valuePair.second.valueMax);
					}
					else
					{
						ImGui::DragFloat(valuePair.first, (real*)valuePair.second.valuePtr);
					}
					break;
				case ValueType::INT:
					if (valuePair.second.valueMin != nullptr && valuePair.second.valueMax != nullptr)
					{
						ImGui::SliderInt(valuePair.first, (i32*)valuePair.second.valuePtr, *(i32*)&valuePair.second.valueMin, *(i32*)&valuePair.second.valueMax);
					}
					else
					{
						ImGui::DragInt(valuePair.first, (i32*)valuePair.second.valuePtr);
					}
					break;
				case ValueType::UINT:
					if (valuePair.second.valueMin != nullptr && valuePair.second.valueMax != nullptr)
					{
						ImGuiExt::SliderUInt(valuePair.first, (u32*)valuePair.second.valuePtr, *(u32*)&valuePair.second.valueMin, *(u32*)&valuePair.second.valueMax);
					}
					else
					{
						ImGuiExt::DragUInt(valuePair.first, (u32*)valuePair.second.valuePtr);
					}
					break;
				case ValueType::BOOL:
					ImGui::Checkbox(valuePair.first, (bool*)valuePair.second.valuePtr);
					break;
				case ValueType::VEC2:
					if (valuePair.second.valueMin != nullptr && valuePair.second.valueMax != nullptr)
					{
						ImGui::SliderFloat2(valuePair.first, &((glm::vec2*)valuePair.second.valuePtr)->x, *(real*)&valuePair.second.valueMin, *(real*)&valuePair.second.valueMax);
					}
					else
					{
						ImGui::DragFloat2(valuePair.first, &((glm::vec2*)valuePair.second.valuePtr)->x);
					}
					break;
				case ValueType::VEC3:
					if (valuePair.second.valueMin != nullptr && valuePair.second.valueMax != nullptr)
					{
						ImGui::SliderFloat3(valuePair.first, &((glm::vec3*)valuePair.second.valuePtr)->x, *(real*)&valuePair.second.valueMin, *(real*)&valuePair.second.valueMax);
					}
					else
					{
						ImGui::DragFloat3(valuePair.first, &((glm::vec3*)valuePair.second.valuePtr)->x);
					}
					break;
				case ValueType::VEC4:
					if (valuePair.second.valueMin != nullptr && valuePair.second.valueMax != nullptr)
					{
						ImGui::SliderFloat4(valuePair.first, &((glm::vec4*)valuePair.second.valuePtr)->x, *(real*)&valuePair.second.valueMin, *(real*)&valuePair.second.valueMax);
					}
					else
					{
						ImGui::DragFloat4(valuePair.first, &((glm::vec4*)valuePair.second.valuePtr)->x);
					}
					break;
				case ValueType::QUAT:
				{
					glm::vec3 rotEuler = glm::eulerAngles(*(glm::quat*)valuePair.second.valuePtr);
					if (ImGui::SliderFloat3(valuePair.first, &rotEuler.x, *(real*)&valuePair.second.valueMin, *(real*)&valuePair.second.valueMax))
					{
						*(glm::quat*)valuePair.second.valuePtr = glm::quat(rotEuler);
					}
				} break;
				default:
					PrintError("Unhandled value type in ConfigFile::DrawImGuiObjects\n");
					break;
				}
			}

			if (ImGui::Button("Save"))
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
} // namespace flex
