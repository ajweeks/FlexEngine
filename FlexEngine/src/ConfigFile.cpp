#include "stdafx.hpp"

#include "ConfigFile.hpp"
#include "Helpers.hpp"
#include "JSONParser.hpp"

namespace flex
{
	ConfigFile::ConfigFile(const std::string& name, const std::string& filePath) :
		name(name),
		filePath(filePath)
	{
	}

	void ConfigFile::RegisterProperty(const char* propertyName, real* propertyValue)
	{
		values.emplace(propertyName, ConfigValue(propertyName, propertyValue, ValueType::FLOAT));
	}

	void ConfigFile::RegisterProperty(const char* propertyName, real* propertyValue, real valueMin, real valueMax)
	{
		values.emplace(propertyName, ConfigValue(propertyName, propertyValue, *(void**)&valueMin, *(void**)&valueMax, ValueType::FLOAT));
	}

	void ConfigFile::RegisterProperty(const char* propertyName, i32* propertyValue)
	{
		values.emplace(propertyName, ConfigValue(propertyName, propertyValue, ValueType::INT));
	}

	void ConfigFile::RegisterProperty(const char* propertyName, u32* propertyValue)
	{
		values.emplace(propertyName, ConfigValue(propertyName, propertyValue, ValueType::UINT));
	}

	void ConfigFile::RegisterProperty(const char* propertyName, bool* propertyValue)
	{
		values.emplace(propertyName, ConfigValue(propertyName, propertyValue, ValueType::BOOL));
	}

	void ConfigFile::Serialize()
	{
		JSONObject rootObject = {};
		for (auto& valuePair : values)
		{
			rootObject.fields.emplace_back(valuePair.first, JSONValue::FromRawPtr(valuePair.second.valuePtr, valuePair.second.type));
		}

		std::string fileContents = rootObject.ToString();

		if (!WriteFile(filePath, fileContents, false))
		{
			PrintError("Failed to write config file to %s\n", filePath.c_str());
		}
	}

	void ConfigFile::Deserialize()
	{
		if (FileExists(filePath))
		{
			std::string fileContents;
			if (ReadFile(filePath, fileContents, false))
			{
				JSONObject rootObject;
				if (JSONParser::Parse(fileContents, rootObject))
				{
					for (auto& valuePair : values)
					{
						if (!rootObject.TryGetValueOfType(valuePair.second.label, valuePair.second.valuePtr, valuePair.second.type))
						{
							PrintError("Failed to get property %s from config file %s\n", valuePair.second.label, filePath.c_str());
						}
					}
				}
				else
				{
					PrintError("Failed to parse config file at %s\n", filePath.c_str());
				}
			}
		}
	}

	ConfigFile::Request ConfigFile::DrawImGuiObjects()
	{
		Request result = Request::NONE;

		if (ImGui::TreeNode(name.c_str()))
		{
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
