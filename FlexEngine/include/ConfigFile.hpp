#pragma once

#include "JSONTypes.hpp"
#include "PropertyCollection.hpp"
#include "Helpers.hpp" // For DEFAULT_FLOAT_PRECISION

namespace flex
{
	struct ConfigFile
	{
		struct ConfigValue;

		ConfigFile(const std::string& name, const std::string& filePath, i32 currentFileVersion);
		~ConfigFile();

		ConfigFile(const ConfigFile& other) = delete;
		ConfigFile(const ConfigFile&& other) noexcept = delete;
		ConfigFile& operator=(const ConfigFile& other) = delete;
		ConfigFile& operator=(const ConfigFile&& other) noexcept = delete;

		ConfigValue& RegisterProperty(const char* propertyName, real* propertyValue);
		ConfigValue& RegisterProperty(const char* propertyName, i32* propertyValue);
		ConfigValue& RegisterProperty(const char* propertyName, u32* propertyValue);
		ConfigValue& RegisterProperty(const char* propertyName, bool* propertyValue);
		ConfigValue& RegisterProperty(const char* propertyName, glm::vec2* propertyValue);
		ConfigValue& RegisterProperty(const char* propertyName, glm::vec3* propertyValue);
		ConfigValue& RegisterProperty(const char* propertyName, glm::vec4* propertyValue);
		ConfigValue& RegisterProperty(const char* propertyName, glm::quat* propertyValue);
		ConfigValue& RegisterProperty(const char* propertyName, std::string* propertyValue);
		ConfigValue& RegisterProperty(const char* propertyName, GUID* propertyValue);

		bool Serialize();
		bool Deserialize();

		void SetOnDeserialize(std::function<void()> callback);

		struct ConfigValue
		{
			ConfigValue(const char* label, void* valuePtr, ValueType type) :
				label(label),
				valuePtr(valuePtr),
				type(type)
			{}

			ConfigValue& SetRange(real rangeMin, real rangeMax);

			const char* label = nullptr;
			void* valuePtr = nullptr;
			void* valueMin = nullptr;
			void* valueMax = nullptr;
			ValueType type;
			u32 valueMinSet : 1;
			u32 valueMaxSet : 1;

		};

		enum class Request
		{
			RELOAD,
			SERIALIZE,
			NONE
		};

		// Returns true when user requested data to be serialized
		Request DrawImGuiObjects();

		std::map<const char*, ConfigValue> values;

		std::string filePath;
		std::string name;
		i32 currentFileVersion;
		i32 fileVersion;
		bool bDirty = false;
		std::function<void()> onDeserializeCallback;

	};
} // namespace flex
