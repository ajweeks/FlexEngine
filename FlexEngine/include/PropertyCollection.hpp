#pragma once

#include "JSONTypes.hpp"

namespace flex
{
	struct PropertyCollection
	{
		struct PropertyValue;

		PropertyCollection() = default;
		~PropertyCollection() = default;

		PropertyCollection(const PropertyCollection& other);
		PropertyCollection(const PropertyCollection&& other) noexcept;
		PropertyCollection& operator=(const PropertyCollection& other);
		PropertyCollection& operator=(const PropertyCollection&& other) noexcept;

		PropertyValue& RegisterProperty(const char* propertyName, real* propertyValue);
		PropertyValue& RegisterProperty(const char* propertyName, i32* propertyValue);
		PropertyValue& RegisterProperty(const char* propertyName, u32* propertyValue);
		PropertyValue& RegisterProperty(const char* propertyName, bool* propertyValue);
		PropertyValue& RegisterProperty(const char* propertyName, glm::vec2* propertyValue);
		PropertyValue& RegisterProperty(const char* propertyName, glm::vec3* propertyValue);
		PropertyValue& RegisterProperty(const char* propertyName, glm::vec4* propertyValue);
		PropertyValue& RegisterProperty(const char* propertyName, glm::quat* propertyValue);
		PropertyValue& RegisterProperty(const char* propertyName, std::string* propertyValue);
		PropertyValue& RegisterProperty(const char* propertyName, GUID* propertyValue);

		void Serialize(JSONObject& parentObject);
		bool SerializeGameObjectFields(JSONObject& parentObject, const GameObjectID& gameObjectID, bool bSerializePrefabData);
		void Deserialize(const JSONObject& parentObject);

		bool DrawImGuiObjects();

		struct PropertyValue
		{
			PropertyValue(const char* label, void* valuePtr, ValueType type) :
				label(label),
				valuePtr(valuePtr),
				defaultValue{},
				valueMinSet(0),
				valueMaxSet(0),
				precisionSet(0),
				defaultValueSet(0),
				type(type)
			{}

			PropertyValue(const PropertyValue& other);
			PropertyValue(const PropertyValue&& other);
			PropertyValue& operator=(const PropertyValue& other);
			PropertyValue& operator=(const PropertyValue&& other);

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

			bool DrawImGui();

			u32 GetPrecision() const;
			bool IsDefaultValue() const;

			const char* label;
			void* valuePtr;
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
		};

		// TODO: Make vector
		std::map<const char*, PropertyValue> values;
		std::string name;
		std::function<void()> onDeserializeCallback;
	};

	extern bool ValuesAreEqual(ValueType valueType, void* value0, void* value1);

	extern void SerializeGameObjectField(JSONObject& parentObject, const char* label, void* valuePtr, ValueType valueType, GameObjectID gameObjectID, u32 precision = 2);
	extern void SerializePrefabInstanceFieldIfUnique(JSONObject& parentObject, const char* label, void* valuePtr, ValueType valueType, void* templateField, u32 precision = 2);
} // namespace flex
