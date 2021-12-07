#pragma once

#include "JSONTypes.hpp"
#include "PropertyCollection.hpp"

namespace flex
{
	struct ConfigFile
	{
		ConfigFile(const std::string& name, const std::string& filePath, i32 currentFileVersion);
		~ConfigFile();

		void RegisterProperty(i32 versionAdded, const char* propertyName, real* propertyValue);
		void RegisterProperty(i32 versionAdded, const char* propertyName, real* propertyValue, real valueMin, real valueMax);
		void RegisterProperty(i32 versionAdded, const char* propertyName, i32* propertyValue);
		void RegisterProperty(i32 versionAdded, const char* propertyName, u32* propertyValue);
		void RegisterProperty(i32 versionAdded, const char* propertyName, bool* propertyValue);
		void RegisterProperty(i32 versionAdded, const char* propertyName, glm::vec2* propertyValue, u32 precision);
		void RegisterProperty(i32 versionAdded, const char* propertyName, glm::vec3* propertyValue, u32 precision);
		void RegisterProperty(i32 versionAdded, const char* propertyName, glm::vec4* propertyValue, u32 precision);
		void RegisterProperty(i32 versionAdded, const char* propertyName, glm::quat* propertyValue, u32 precision);

		bool Serialize();
		bool Deserialize();

		void SetOnDeserialize(std::function<void()> callback);

		enum class Request
		{
			RELOAD,
			SERIALIZE,
			NONE
		};

		// Returns true when user requested data to be serialized
		Request DrawImGuiObjects();

		PropertyCollection propertyCollection;
		std::string filePath;
		std::string name;
		i32 currentFileVersion;
		i32 fileVersion;
		bool bDirty = false;
		std::function<void()> onDeserializeCallback;
	};
} // namespace flex
