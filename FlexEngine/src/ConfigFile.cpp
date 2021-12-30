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


	bool ConfigFile::Serialize()
	{
		fileVersion = currentFileVersion;

		JSONObject rootObject = {};

		rootObject.fields.emplace_back("version", JSONValue(fileVersion));

		propertyCollection.Serialize(rootObject);

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

					propertyCollection.Deserialize(rootObject);

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

			for (auto& valuePair : propertyCollection.values)
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
} // namespace flex
