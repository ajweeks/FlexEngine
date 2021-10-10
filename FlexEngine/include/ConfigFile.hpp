#pragma once

#include "JSONTypes.hpp"

namespace flex
{
	struct ConfigFile
	{
		ConfigFile(const std::string& name, const std::string& filePath);

		void RegisterProperty(const char* propertyName, real* propertyValue);
		void RegisterProperty(const char* propertyName, real* propertyValue, real valueMin, real valueMax);
		void RegisterProperty(const char* propertyName, i32* propertyValue);
		void RegisterProperty(const char* propertyName, u32* propertyValue);
		void RegisterProperty(const char* propertyName, bool* propertyValue);

		void Serialize();
		void Deserialize();

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
			ConfigValue(const char* label, void* valuePtr, void* valueMin, void* valueMax, ValueType type) :
				label(label),
				valuePtr(valuePtr),
				valueMin(valueMin),
				valueMax(valueMax),
				type(type)
			{}

			ConfigValue(const char* label, void* valuePtr, ValueType type) :
				label(label),
				valuePtr(valuePtr),
				valueMin(nullptr),
				valueMax(nullptr),
				type(type)
			{}

			const char* label;
			void* valuePtr;
			void* valueMin;
			void* valueMax;
			ValueType type;
		};

		std::map<const char*, ConfigValue> values;
		std::string filePath;
		std::string name;
	};
} // namespace flex
