#pragma once

namespace flex
{
	struct JSONObject;
	enum class ValueType;

	struct PropertyCollection
	{
		PropertyCollection() = default;
		~PropertyCollection() = default;

		PropertyCollection(const PropertyCollection& other);
		PropertyCollection(const PropertyCollection&& other) noexcept;
		PropertyCollection& operator=(const PropertyCollection& other);
		PropertyCollection& operator=(const PropertyCollection&& other) noexcept;

		void RegisterProperty(i32 versionAdded, const char* propertyName, real* propertyValue);
		void RegisterProperty(i32 versionAdded, const char* propertyName, real* propertyValue, real valueMin, real valueMax);
		void RegisterProperty(i32 versionAdded, const char* propertyName, i32* propertyValue);
		void RegisterProperty(i32 versionAdded, const char* propertyName, u32* propertyValue);
		void RegisterProperty(i32 versionAdded, const char* propertyName, bool* propertyValue);
		void RegisterProperty(i32 versionAdded, const char* propertyName, glm::vec2* propertyValue, u32 precision);
		void RegisterProperty(i32 versionAdded, const char* propertyName, glm::vec3* propertyValue, u32 precision);
		void RegisterProperty(i32 versionAdded, const char* propertyName, glm::vec4* propertyValue, u32 precision);
		void RegisterProperty(i32 versionAdded, const char* propertyName, glm::quat* propertyValue, u32 precision);

		void Serialize(JSONObject& parentObject);
		bool SerializeGameObjectFields(JSONObject& parentObject, const GameObjectID& gameObjectID);
		void Deserialize(const JSONObject& parentObject, i32 fileVersion, bool bWarnForMissingFields, const char* filePath = nullptr);

		bool DrawImGuiObjects();

		struct PropertyValue
		{
			PropertyValue(i32 versionAdded, const char* label, void* valuePtr, ValueType type) :
				label(label),
				valuePtr(valuePtr),
				versionAdded(versionAdded),
				valueMin(nullptr),
				valueMax(nullptr),
				precision(nullptr),
				type(type)
			{}

			PropertyValue(i32 versionAdded, const char* label, void* valuePtr, void* precision, ValueType type) :
				label(label),
				valuePtr(valuePtr),
				versionAdded(versionAdded),
				valueMin(nullptr),
				valueMax(nullptr),
				precision(precision),
				type(type)
			{}

			PropertyValue(i32 versionAdded, const char* label, void* valuePtr, void* valueMin, void* valueMax, ValueType type) :
				label(label),
				valuePtr(valuePtr),
				versionAdded(versionAdded),
				valueMin(valueMin),
				valueMax(valueMax),
				precision(nullptr),
				type(type)
			{}

			bool DrawImGui();

			const char* label;
			void* valuePtr;
			i32 versionAdded;
			void* valueMin;
			void* valueMax;
			void* precision;
			ValueType type;
		};

		// TODO: Make vector
		std::map<const char*, PropertyValue> values;
		std::string name;
		std::function<void()> onDeserializeCallback;
	};

	extern void SerializeGameObjectField(JSONObject& parentObject, const char* label, void* valuePtr, ValueType valueType, GameObjectID gameObjectID, u32 precision = 2);
	extern void SerializePrefabInstanceFieldIfUnique(JSONObject& parentObject, const char* label, void* valuePtr, ValueType valueType, GameObjectID gameObjectID, u32 precision = 2);
	extern void SerializePrefabInstanceFieldIfUnique(JSONObject& parentObject, const char* label, void* valuePtr, ValueType valueType, void* templateField, u32 precision = 2);
} // namespace flex
