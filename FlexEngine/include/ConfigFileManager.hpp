#pragma once

namespace flex
{
	struct ConfigFile;

	class ConfigFileManager
	{
	public:
		ConfigFileManager();

		void RegisterConfigFile(ConfigFile* configFile);
		void DeregisterConfigFile(ConfigFile* configFile);

		void Update();

	private:
		std::vector<ConfigFile*> m_ConfigFiles;
		DirectoryWatcher m_ConfigDirectoryWatch;
		DirectoryWatcher m_SavedDirectoryWatch;
	};
} // namespace flex
