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
		for (auto iter = values.begin(); iter != values.end(); ++iter)
		{
			rootObject.fields.emplace_back(iter->first, JSONValue::FromRawPtr(iter->second.valuePtr, iter->second.type));
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
					for (auto iter = values.begin(); iter != values.end(); ++iter)
					{
						//JSONValue::type type = Variant::TypeToJSONValueType(iter->second.type);
						if (!rootObject.TryGetValueOfType(iter->second.label, iter->second.valuePtr, iter->second.type))
						{
							PrintError("Failed to get property %s from config file %s\n", iter->second.label, filePath.c_str());
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
			for (auto iter = values.begin(); iter != values.end(); ++iter)
			{
				switch (iter->second.type)
				{
				case ValueType::FLOAT:
					if (iter->second.valueMin != nullptr && iter->second.valueMax != nullptr)
					{
						ImGui::SliderFloat(iter->first, (real*)iter->second.valuePtr, *(real*)&iter->second.valueMin, *(real*)&iter->second.valueMax);
					}
					else
					{
						ImGui::DragFloat(iter->first, (real*)iter->second.valuePtr);
					}
					break;
				case ValueType::INT:
					if (iter->second.valueMin != nullptr && iter->second.valueMax != nullptr)
					{
						ImGui::SliderInt(iter->first, (i32*)iter->second.valuePtr, *(i32*)&iter->second.valueMin, *(i32*)&iter->second.valueMax);
					}
					else
					{
						ImGui::DragInt(iter->first, (i32*)iter->second.valuePtr);
					}
					break;
				case ValueType::UINT:
					if (iter->second.valueMin != nullptr && iter->second.valueMax != nullptr)
					{
						ImGuiExt::SliderUInt(iter->first, (u32*)iter->second.valuePtr, *(u32*)&iter->second.valueMin, *(u32*)&iter->second.valueMax);
					}
					else
					{
						ImGuiExt::DragUInt(iter->first, (u32*)iter->second.valuePtr);
					}
					break;
				case ValueType::BOOL:
					ImGui::Checkbox(iter->first, (bool*)iter->second.valuePtr);
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
