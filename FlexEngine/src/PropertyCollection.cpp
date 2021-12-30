#include "stdafx.hpp"

#include "PropertyCollection.hpp"

#include "Helpers.hpp"
#include "JSONTypes.hpp"
#include "Scene/GameObject.hpp"
#include "ResourceManager.hpp"

namespace flex
{
	PropertyCollection::PropertyCollection(const PropertyCollection& other)
	{
		values = std::map<const char*, PropertyValue>(other.values);
		name = other.name;
		onDeserializeCallback = other.onDeserializeCallback;
	}

	PropertyCollection::PropertyCollection(const PropertyCollection&& other) noexcept
	{
		values = std::map<const char*, PropertyValue>(other.values);
		name = other.name;
		onDeserializeCallback = other.onDeserializeCallback;
	}

	PropertyCollection& PropertyCollection::operator=(const PropertyCollection& other)
	{
		if (this != &other)
		{
			values = std::map<const char*, PropertyValue>(other.values);
			name = other.name;
			onDeserializeCallback = other.onDeserializeCallback;
		}
		return *this;
	}

	PropertyCollection& PropertyCollection::operator=(const PropertyCollection&& other) noexcept
	{
		if (this != &other)
		{
			values = std::map<const char*, PropertyValue>(other.values);
			name = other.name;
			onDeserializeCallback = other.onDeserializeCallback;
		}
		return *this;
	}

	PropertyCollection::PropertyValue::PropertyValue(const PropertyValue& other)
	{
		memcpy(this, &other, sizeof(PropertyValue));
	}

	PropertyCollection::PropertyValue::PropertyValue(const PropertyValue&& other)
	{
		memcpy(this, &other, sizeof(PropertyValue));
	}

	PropertyCollection::PropertyValue& PropertyCollection::PropertyValue::operator=(const PropertyValue& other)
	{
		memcpy(this, &other, sizeof(PropertyValue));
		return *this;
	}

	PropertyCollection::PropertyValue& PropertyCollection::PropertyValue::operator=(const PropertyValue&& other)
	{
		memcpy(this, &other, sizeof(PropertyValue));
		return *this;
	}

	PropertyCollection::PropertyValue& PropertyCollection::RegisterProperty(const char* propertyName, real* propertyValue)
	{
		auto pair = values.emplace(propertyName, PropertyValue(propertyName, propertyValue, ValueType::FLOAT));
		return pair.first->second;
	}

	PropertyCollection::PropertyValue& PropertyCollection::RegisterProperty(const char* propertyName, i32* propertyValue)
	{
		auto pair = values.emplace(propertyName, PropertyValue(propertyName, propertyValue, ValueType::INT));
		return pair.first->second;
	}

	PropertyCollection::PropertyValue& PropertyCollection::RegisterProperty(const char* propertyName, u32* propertyValue)
	{
		auto pair = values.emplace(propertyName, PropertyValue(propertyName, propertyValue, ValueType::UINT));
		return pair.first->second;
	}

	PropertyCollection::PropertyValue& PropertyCollection::RegisterProperty(const char* propertyName, bool* propertyValue)
	{
		auto pair = values.emplace(propertyName, PropertyValue(propertyName, propertyValue, ValueType::BOOL));
		return pair.first->second;
	}

	PropertyCollection::PropertyValue& PropertyCollection::RegisterProperty(const char* propertyName, glm::vec2* propertyValue)
	{
		auto pair = values.emplace(propertyName, PropertyValue(propertyName, propertyValue, ValueType::VEC2));
		return pair.first->second;
	}

	PropertyCollection::PropertyValue& PropertyCollection::RegisterProperty(const char* propertyName, glm::vec3* propertyValue)
	{
		auto pair = values.emplace(propertyName, PropertyValue(propertyName, propertyValue,  ValueType::VEC3));
		return pair.first->second;
	}

	PropertyCollection::PropertyValue& PropertyCollection::RegisterProperty(const char* propertyName, glm::vec4* propertyValue)
	{
		auto pair = values.emplace(propertyName, PropertyValue(propertyName, propertyValue,  ValueType::VEC4));
		return pair.first->second;
	}

	PropertyCollection::PropertyValue& PropertyCollection::RegisterProperty(const char* propertyName, glm::quat* propertyValue)
	{
		auto pair = values.emplace(propertyName, PropertyValue(propertyName, propertyValue,  ValueType::QUAT));
		return pair.first->second;
	}

	PropertyCollection::PropertyValue& PropertyCollection::RegisterProperty(const char* propertyName, std::string* propertyValue)
	{
		auto pair = values.emplace(propertyName, PropertyValue(propertyName, propertyValue, ValueType::STRING));
		return pair.first->second;
	}

	PropertyCollection::PropertyValue& PropertyCollection::RegisterProperty(const char* propertyName, GUID* propertyValue)
	{
		auto pair = values.emplace(propertyName, PropertyValue(propertyName, propertyValue, ValueType::GUID));
		return pair.first->second;
	}

	void PropertyCollection::Serialize(JSONObject& parentObject)
	{
		for (auto& valuePair : values)
		{
			if (!valuePair.second.IsDefaultValue())
			{
				parentObject.fields.emplace_back(valuePair.first, JSONValue::FromRawPtr(valuePair.second.valuePtr, valuePair.second.type, valuePair.second.GetPrecision()));
			}
		}
	}

	bool PropertyCollection::SerializeGameObjectFields(JSONObject& parentObject, const GameObjectID& gameObjectID, bool bSerializePrefabData)
	{
		GameObject* gameObject = gameObjectID.Get();

		if (gameObject == nullptr)
		{
			PrintError("Attempted to serialize missing game object\n");
			return false;
		}

		PrefabID prefabIDLoadedFrom = gameObject->GetPrefabIDLoadedFrom();
		GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(prefabIDLoadedFrom);

		if (prefabTemplate == nullptr || bSerializePrefabData)
		{
			// No need to worry about overrides if this object isn't a prefab instance
			Serialize(parentObject);
			return true;
		}

		for (auto& valuePair : values)
		{
			u32 fieldOffset = (u32)((u64)valuePair.second.valuePtr - (u64)gameObject);
			CHECK_LT(fieldOffset, 1'024u);
			void* templateField = ((u8*)prefabTemplate + fieldOffset);

			SerializePrefabInstanceFieldIfUnique(parentObject, valuePair.first, valuePair.second.valuePtr, valuePair.second.type, templateField, valuePair.second.GetPrecision());
		}

		return true;
	}

	void PropertyCollection::Deserialize(const JSONObject& parentObject)
	{
		for (auto& valuePair : values)
		{
			parentObject.TryGetValueOfType(valuePair.second.label, valuePair.second.valuePtr, valuePair.second.type);
		}
	}

	bool PropertyCollection::DrawImGuiObjects()
	{
		bool bValueChanged = false;
		for (auto& valuePair : values)
		{
			if (valuePair.second.DrawImGui())
			{
				bValueChanged = true;
			}
		}

		return bValueChanged;
	}

	bool PropertyCollection::PropertyValue::DrawImGui()
	{
		bool bValueChanged = false;

		switch (type)
		{
		case ValueType::FLOAT:
			if (valueMinSet != 0 && valueMaxSet != 0)
			{
				bValueChanged = ImGui::SliderFloat(label, (real*)valuePtr, *(real*)&valueMin, *(real*)&valueMax) || bValueChanged;
			}
			else
			{
				bValueChanged = ImGui::DragFloat(label, (real*)valuePtr) || bValueChanged;
			}
			break;
		case ValueType::INT:
			if (valueMinSet != 0 && valueMaxSet != 0)
			{
				bValueChanged = ImGui::SliderInt(label, (i32*)valuePtr, *(i32*)&valueMin, *(i32*)&valueMax) || bValueChanged;
			}
			else
			{
				bValueChanged = ImGui::DragInt(label, (i32*)valuePtr) || bValueChanged;
			}
			break;
		case ValueType::UINT:
			if (valueMinSet != 0 && valueMaxSet != 0)
			{
				bValueChanged = ImGuiExt::SliderUInt(label, (u32*)valuePtr, *(u32*)&valueMin, *(u32*)&valueMax) || bValueChanged;
			}
			else
			{
				bValueChanged = ImGuiExt::DragUInt(label, (u32*)valuePtr) || bValueChanged;
			}
			break;
		case ValueType::BOOL:
			bValueChanged = ImGui::Checkbox(label, (bool*)valuePtr) || bValueChanged;
			break;
		case ValueType::VEC2:
			if (valueMinSet != 0 && valueMaxSet != 0)
			{
				bValueChanged = ImGui::SliderFloat2(label, &((glm::vec2*)valuePtr)->x, *(real*)&valueMin, *(real*)&valueMax) || bValueChanged;
			}
			else
			{
				bValueChanged = ImGui::DragFloat2(label, &((glm::vec2*)valuePtr)->x) || bValueChanged;
			}
			break;
		case ValueType::VEC3:
			if (valueMinSet != 0 && valueMaxSet != 0)
			{
				bValueChanged = ImGui::SliderFloat3(label, &((glm::vec3*)valuePtr)->x, *(real*)&valueMin, *(real*)&valueMax) || bValueChanged;
			}
			else
			{
				bValueChanged = ImGui::DragFloat3(label, &((glm::vec3*)valuePtr)->x) || bValueChanged;
			}
			break;
		case ValueType::VEC4:
			if (valueMinSet != 0 && valueMaxSet != 0)
			{
				bValueChanged = ImGui::SliderFloat4(label, &((glm::vec4*)valuePtr)->x, *(real*)&valueMin, *(real*)&valueMax) || bValueChanged;
			}
			else
			{
				bValueChanged = ImGui::DragFloat4(label, &((glm::vec4*)valuePtr)->x) || bValueChanged;
			}
			break;
		case ValueType::QUAT:
		{
			glm::vec3 rotEuler = glm::eulerAngles(*(glm::quat*)valuePtr);

			real valueMinReal = -TWO_PI;
			real valueMaxReal = TWO_PI;
			if (valueMinSet != 0 && valueMaxSet != 0)
			{
				valueMinReal = *(real*)&valueMin;
				valueMaxReal = *(real*)&valueMax;
			}

			if (ImGui::SliderFloat3(label, &rotEuler.x, valueMinReal, valueMaxReal))
			{
				*(glm::quat*)valuePtr = glm::quat(rotEuler);
				bValueChanged = true;
			}
		} break;
		case ValueType::STRING:
		{
			ImGui::Text("%s", ((std::string*)valuePtr)->c_str());
		} break;
		case ValueType::GUID:
		{
			std::string guidStr = ((GUID*)valuePtr)->ToString();
			ImGui::Text("%s", guidStr.c_str());
		} break;
		default:
			PrintError("Unhandled value type in ConfigFile::DrawImGuiObjects\n");
			break;
		}

		return bValueChanged;
	}

	u32 PropertyCollection::PropertyValue::GetPrecision() const
	{
		return precisionSet != 0 ? *(u32*)&precision : JSONValue::DEFAULT_FLOAT_PRECISION;
	}

	bool PropertyCollection::PropertyValue::IsDefaultValue() const
	{
		if (defaultValueSet != 0)
		{
			switch (type)
			{
			case ValueType::INT:
				return ValuesAreEqual(type, valuePtr, (void*)&defaultValue.intValue);
			case ValueType::UINT:
				return ValuesAreEqual(type, valuePtr, (void*)&defaultValue.uintValue);
			case ValueType::FLOAT:
				return ValuesAreEqual(type, valuePtr, (void*)&defaultValue.realValue);
			case ValueType::BOOL:
				return ValuesAreEqual(type, valuePtr, (void*)&defaultValue.boolValue);
			case ValueType::GUID:
				return ValuesAreEqual(type, valuePtr, (void*)&defaultValue.guidValue);
			case ValueType::VEC2:
				return ValuesAreEqual(type, valuePtr, (void*)&defaultValue.vec2Value);
			case ValueType::VEC3:
				return ValuesAreEqual(type, valuePtr, (void*)&defaultValue.vec3Value);
			case ValueType::VEC4:
				return ValuesAreEqual(type, valuePtr, (void*)&defaultValue.vec4Value);
			case ValueType::QUAT:
				return ValuesAreEqual(type, valuePtr, (void*)&defaultValue.quatValue);
			default:
				ENSURE_NO_ENTRY();
			}
		}

		return false;
	}

	bool ValuesAreEqual(ValueType valueType, void* value0, void* value1)
	{
		const real threshold = 0.0001f;

		switch (valueType)
		{
		case ValueType::STRING:
			return *(std::string*)value0 == *(std::string*)value1;
		case ValueType::INT:
			return *(i32*)value0 == *(i32*)value1;
		case ValueType::UINT:
			return *(u32*)value0 == *(u32*)value1;
		case ValueType::LONG:
			return *(i64*)value0 == *(i64*)value1;
		case ValueType::ULONG:
			return *(u64*)value0 == *(u64*)value1;
		case ValueType::FLOAT:
			return NearlyEquals(*(real*)value0, *(real*)value1, threshold);
		case ValueType::BOOL:
			return *(bool*)value0 == *(bool*)value1;
		case ValueType::VEC2:
			return NearlyEquals(*(glm::vec2*)value0, *(glm::vec2*)value1, threshold);
		case ValueType::VEC3:
			return NearlyEquals(*(glm::vec3*)value0, *(glm::vec3*)value1, threshold);
		case ValueType::VEC4:
			return NearlyEquals(*(glm::vec4*)value0, *(glm::vec4*)value1, threshold);
		case ValueType::QUAT:
			return NearlyEquals(*(glm::quat*)value0, *(glm::quat*)value1, threshold);
		case ValueType::GUID:
			return *(GUID*)value0 == *(GUID*)value1;
		default:
			ENSURE_NO_ENTRY();
			return false;
		}
	}

	void SerializePrefabInstanceFieldIfUnique(JSONObject& parentObject, const char* label, void* valuePtr, ValueType valueType, void* templateField, u32 precision /* = 2 */)
	{
		if (!ValuesAreEqual(valueType, valuePtr, templateField))
		{
			parentObject.fields.emplace_back(label, JSONValue::FromRawPtr(valuePtr, valueType, precision));
		}
	}
} // namespace flex
