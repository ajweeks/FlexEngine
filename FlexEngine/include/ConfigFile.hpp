#pragma once

#include "JSONTypes.hpp"

namespace flex
{
	struct ConfigFile
	{
		ConfigFile(const std::string& name, const std::string& filePath, i32 currentFileVersion);

		void RegisterProperty(i32 versionAdded, const char* propertyName, real* propertyValue);
		void RegisterProperty(i32 versionAdded, const char* propertyName, real* propertyValue, real valueMin, real valueMax);
		void RegisterProperty(i32 versionAdded, const char* propertyName, i32* propertyValue);
		void RegisterProperty(i32 versionAdded, const char* propertyName, u32* propertyValue);
		void RegisterProperty(i32 versionAdded, const char* propertyName, bool* propertyValue);
		void RegisterProperty(i32 versionAdded, const char* propertyName, glm::vec2* propertyValue, real precision);
		void RegisterProperty(i32 versionAdded, const char* propertyName, glm::vec3* propertyValue, real precision);
		void RegisterProperty(i32 versionAdded, const char* propertyName, glm::vec4* propertyValue, real precision);
		void RegisterProperty(i32 versionAdded, const char* propertyName, glm::quat* propertyValue, real precision);

		bool Serialize();
		bool Deserialize();

		enum class Request
		{
			RELOAD,
			SERIALIZE,
			NONE
		};

		// Returns true when user requested data to be serialized
		Request DrawImGuiObjects();

		struct ConfigValue
		{
			ConfigValue(i32 versionAdded, const char* label, void* valuePtr, ValueType type) :
				label(label),
				valuePtr(valuePtr),
				versionAdded(versionAdded),
				valueMin(nullptr),
				valueMax(nullptr),
				precision(nullptr),
				type(type)
			{}

			ConfigValue(i32 versionAdded, const char* label, void* valuePtr, void* precision, ValueType type) :
				label(label),
				valuePtr(valuePtr),
				versionAdded(versionAdded),
				valueMin(nullptr),
				valueMax(nullptr),
				precision(precision),
				type(type)
			{}

			ConfigValue(i32 versionAdded, const char* label, void* valuePtr, void* valueMin, void* valueMax, ValueType type) :
				label(label),
				valuePtr(valuePtr),
				versionAdded(versionAdded),
				valueMin(valueMin),
				valueMax(valueMax),
				precision(nullptr),
				type(type)
			{}

			const char* label;
			void* valuePtr;
			i32 versionAdded;
			void* valueMin;
			void* valueMax;
			void* precision;
			ValueType type;
		};

		std::map<const char*, ConfigValue> values;
		std::string filePath;
		std::string name;
		i32 currentFileVersion;
		i32 fileVersion;
	};
} // namespace flex
