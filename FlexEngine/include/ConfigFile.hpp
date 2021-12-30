#pragma once

#include "JSONTypes.hpp"
#include "PropertyCollection.hpp"

namespace flex
{
	struct ConfigFile
	{
		ConfigFile(const std::string& name, const std::string& filePath, i32 currentFileVersion);
		~ConfigFile();

		template<typename T>
		PropertyCollection::PropertyValue& RegisterProperty(const char* propertyName, T* propertyValue)
		{
			return propertyCollection.RegisterProperty(propertyName, propertyValue);
		}

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
