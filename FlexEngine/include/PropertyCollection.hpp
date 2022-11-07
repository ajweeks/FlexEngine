#pragma once

#include "JSONTypes.hpp"

namespace flex
{
	struct PropertyValue
	{
		PropertyValue(const char* label, ValueType type, size_t offset) :
			label(label),
			defaultValue{},
			valueMinSet(0),
			valueMaxSet(0),
			precisionSet(0),
			defaultValueSet(0),
			type(type),
			offset(offset)
		{}

		PropertyValue(const PropertyValue& other);
		PropertyValue(const PropertyValue&& other);
		PropertyValue& operator=(const PropertyValue& other);
		PropertyValue& operator=(const PropertyValue&& other);

		// TODO: Rename following to SetX
		PropertyValue& VersionAdded(i32 inVersionAdded)
		{
			CHECK(inVersionAdded >= 0 && inVersionAdded < 10'000);
			versionAdded = inVersionAdded;
			return *this;
		}

		PropertyValue& Precision(u32 inPrecision)
		{
			CHECK(inPrecision < 10);
			precision = *(void**)&inPrecision;
			precisionSet = 1;
			return *this;
		}

		template<typename T>
		PropertyValue& Range(T inValueMin, T inValueMax)
		{
			CHECK(inValueMin < inValueMax);
			valueMin = *(void**)&inValueMin;
			valueMax = *(void**)&inValueMax;
			valueMinSet = 1;
			valueMaxSet = 1;

			return *this;
		}

		void DefaultValue(i32 inDefaultValue)
		{
			CHECK_EQ(type, ValueType::INT);
			defaultValue.intValue = inDefaultValue;
			defaultValueSet = 1;
		}

		void DefaultValue(u32 inDefaultValue)
		{
			CHECK_EQ(type, ValueType::UINT);
			defaultValue.uintValue = inDefaultValue;
			defaultValueSet = 1;
		}

		void DefaultValue(real inDefaultValue)
		{
			CHECK_EQ(type, ValueType::FLOAT);
			defaultValue.realValue = inDefaultValue;
			defaultValueSet = 1;
		}

		void DefaultValue(bool inDefaultValue)
		{
			CHECK_EQ(type, ValueType::BOOL);
			defaultValue.boolValue = inDefaultValue;
			defaultValueSet = 1;
		}

		void DefaultValue(const GUID& inDefaultValue)
		{
			CHECK_EQ(type, ValueType::GUID);
			defaultValue.guidValue.data1 = inDefaultValue.Data1;
			defaultValue.guidValue.data2 = inDefaultValue.Data2;
			defaultValueSet = 1;
		}

		void DefaultValue(const glm::vec2& inDefaultValue)
		{
			CHECK_EQ(type, ValueType::VEC2);
			defaultValue.vec2Value = inDefaultValue;
			defaultValueSet = 1;
		}

		void DefaultValue(const glm::vec3& inDefaultValue)
		{
			CHECK_EQ(type, ValueType::VEC3);
			defaultValue.vec3Value = inDefaultValue;
			defaultValueSet = 1;
		}

		void DefaultValue(const glm::vec4& inDefaultValue)
		{
			CHECK_EQ(type, ValueType::VEC4);
			defaultValue.vec4Value = inDefaultValue;
			defaultValueSet = 1;
		}

		void DefaultValue(const glm::quat& inDefaultValue)
		{
			CHECK_EQ(type, ValueType::QUAT);
			defaultValue.quatValue = inDefaultValue;
			defaultValueSet = 1;
		}

		bool DrawImGui(GameObject* gameObject);

		u32 GetPrecision() const;

		const char* label;
		i32 versionAdded = 0;

		void* valueMin = nullptr;
		void* valueMax = nullptr;
		void* precision = nullptr;
		union
		{
			i32 intValue;
			u32 uintValue;
			real realValue;
			bool boolValue;
			glm::vec2 vec2Value;
			glm::vec3 vec3Value;
			glm::vec4 vec4Value;
			glm::quat quatValue;
			struct
			{
				i64 data1, data2;
			} guidValue;
		} defaultValue;

		u32 valueMinSet : 1;
		u32 valueMaxSet : 1;
		u32 precisionSet : 1;
		u32 defaultValueSet : 1;

		ValueType type;
		size_t offset;

		const char* name;
	};

	// Stores a list of properties for a game object type
	// (De)serialization occurs in PropertyCollectionManager
	struct PropertyCollection
	{
		PropertyCollection() = default;
		PropertyCollection(const char* name);
		~PropertyCollection() = default;

		PropertyCollection(const PropertyCollection& other);
		PropertyCollection(const PropertyCollection&& other) noexcept;
		PropertyCollection& operator=(const PropertyCollection& other);
		PropertyCollection& operator=(const PropertyCollection&& other) noexcept;

		PropertyValue& RegisterProperty(const char* propertyName, size_t offset, ValueType type);
		PropertyValue& RegisterProperty(const char* propertyName, size_t offset, const std::type_info& type_info);

		bool DrawImGuiForObject(GameObject* gameObject);

		// TODO: Make vector
		std::map<const char*, PropertyValue> values;
		std::string name;

		PropertyCollection* childCollection = nullptr;

		static PropertyValue s_EmptyPropertyValue;
	};
} // namespace flex
