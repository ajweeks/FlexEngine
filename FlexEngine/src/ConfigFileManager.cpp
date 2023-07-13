#include "stdafx.hpp"

#include "ConfigFileManager.hpp"

#include "ConfigFile.hpp"
#include "Helpers.hpp"

namespace flex
{
	ConfigFileManager::ConfigFileManager() :
		m_ConfigDirectoryWatch(CONFIG_DIRECTORY, false),
		m_SavedDirectoryWatch(SAVED_DIRECTORY, false)
	{
	}

	void ConfigFileManager::RegisterConfigFile(ConfigFile* configFile)
	{
		m_ConfigFiles.push_back(configFile);
	}

	void ConfigFileManager::DeregisterConfigFile(ConfigFile* configFile)
	{
		auto iter = std::find(m_ConfigFiles.begin(), m_ConfigFiles.end(), configFile);
		if (iter != m_ConfigFiles.end())
		{
			m_ConfigFiles.erase(iter);
		}
	}

	void ConfigFileManager::Update()
	{
		bool bConfigDirectoryChanged = m_ConfigDirectoryWatch.Update();
		bool bSavedDirectoryChanged = m_SavedDirectoryWatch.Update();
		if (bConfigDirectoryChanged || bSavedDirectoryChanged)
		{
			for (ConfigFile* configFile : m_ConfigFiles)
			{
				if ((bConfigDirectoryChanged && Contains(m_ConfigDirectoryWatch.modifiedFilePaths, configFile->filePath)) ||
					(bSavedDirectoryChanged && Contains(m_SavedDirectoryWatch.modifiedFilePaths, configFile->filePath)))
				{
					Print("Reloading config file at %s\n", configFile->filePath.c_str());
					configFile->Deserialize();
				}
			}
		}
	}
} // namespace flex
