#include "stdafx.hpp"

#include "PropertyCollection.hpp"

#include "Helpers.hpp"
#include "JSONTypes.hpp"

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

	void PropertyCollection::RegisterProperty(i32 versionAdded, const char* propertyName, real* propertyValue)
	{
		values.emplace(propertyName, PropertyValue(versionAdded, propertyName, propertyValue, ValueType::FLOAT));
	}

	void PropertyCollection::RegisterProperty(i32 versionAdded, const char* propertyName, real* propertyValue, real valueMin, real valueMax)
	{
		values.emplace(propertyName, PropertyValue(versionAdded, propertyName, propertyValue, *(void**)&valueMin, *(void**)&valueMax, ValueType::FLOAT));
	}

	void PropertyCollection::RegisterProperty(i32 versionAdded, const char* propertyName, i32* propertyValue)
	{
		values.emplace(propertyName, PropertyValue(versionAdded, propertyName, propertyValue, ValueType::INT));
	}

	void PropertyCollection::RegisterProperty(i32 versionAdded, const char* propertyName, u32* propertyValue)
	{
		values.emplace(propertyName, PropertyValue(versionAdded, propertyName, propertyValue, ValueType::UINT));
	}

	void PropertyCollection::RegisterProperty(i32 versionAdded, const char* propertyName, bool* propertyValue)
	{
		values.emplace(propertyName, PropertyValue(versionAdded, propertyName, propertyValue, ValueType::BOOL));
	}

	void PropertyCollection::RegisterProperty(i32 versionAdded, const char* propertyName, glm::vec2* propertyValue, u32 precision)
	{
		values.emplace(propertyName, PropertyValue(versionAdded, propertyName, propertyValue, *(void**)&precision, ValueType::VEC2));
	}

	void PropertyCollection::RegisterProperty(i32 versionAdded, const char* propertyName, glm::vec3* propertyValue, u32 precision)
	{
		values.emplace(propertyName, PropertyValue(versionAdded, propertyName, propertyValue, *(void**)&precision, ValueType::VEC3));
	}

	void PropertyCollection::RegisterProperty(i32 versionAdded, const char* propertyName, glm::vec4* propertyValue, u32 precision)
	{
		values.emplace(propertyName, PropertyValue(versionAdded, propertyName, propertyValue, *(void**)&precision, ValueType::VEC4));
	}

	void PropertyCollection::RegisterProperty(i32 versionAdded, const char* propertyName, glm::quat* propertyValue, u32 precision)
	{
		values.emplace(propertyName, PropertyValue(versionAdded, propertyName, propertyValue, *(void**)&precision, ValueType::QUAT));
	}

	void PropertyCollection::Serialize(JSONObject& parentObject)
	{
		for (auto& valuePair : values)
		{
			u32 precision = valuePair.second.precision != nullptr ? *(u32*)&valuePair.second.precision : JSONValue::DEFAULT_FLOAT_PRECISION;
			parentObject.fields.emplace_back(valuePair.first, JSONValue::FromRawPtr(valuePair.second.valuePtr, valuePair.second.type, precision));
		}
	}

	void PropertyCollection::Deserialize(const JSONObject& parentObject, i32 fileVersion, const char* filePath /* = nullptr */)
	{
		for (auto& valuePair : values)
		{
			if (!parentObject.TryGetValueOfType(valuePair.second.label, valuePair.second.valuePtr, valuePair.second.type))
			{
				// Don't warn about missing fields which weren't present in old file version
				if (fileVersion >= valuePair.second.versionAdded)
				{
					PrintError("Failed to get property %s %s\n", valuePair.second.label, (filePath != nullptr ? filePath : ""));
				}
			}
		}
	}

	bool PropertyCollection::PropertyValue::DrawImGui()
	{
		bool bValueChanged = false;

		switch (type)
		{
		case ValueType::FLOAT:
			if (valueMin != nullptr && valueMax != nullptr)
			{
				bValueChanged = ImGui::SliderFloat(label, (real*)valuePtr, *(real*)&valueMin, *(real*)&valueMax) || bValueChanged;
			}
			else
			{
				bValueChanged = ImGui::DragFloat(label, (real*)valuePtr) || bValueChanged;
			}
			break;
		case ValueType::INT:
			if (valueMin != nullptr && valueMax != nullptr)
			{
				bValueChanged = ImGui::SliderInt(label, (i32*)valuePtr, *(i32*)&valueMin, *(i32*)&valueMax) || bValueChanged;
			}
			else
			{
				bValueChanged = ImGui::DragInt(label, (i32*)valuePtr) || bValueChanged;
			}
			break;
		case ValueType::UINT:
			if (valueMin != nullptr && valueMax != nullptr)
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
			if (valueMin != nullptr && valueMax != nullptr)
			{
				bValueChanged = ImGui::SliderFloat2(label, &((glm::vec2*)valuePtr)->x, *(real*)&valueMin, *(real*)&valueMax) || bValueChanged;
			}
			else
			{
				bValueChanged = ImGui::DragFloat2(label, &((glm::vec2*)valuePtr)->x) || bValueChanged;
			}
			break;
		case ValueType::VEC3:
			if (valueMin != nullptr && valueMax != nullptr)
			{
				bValueChanged = ImGui::SliderFloat3(label, &((glm::vec3*)valuePtr)->x, *(real*)&valueMin, *(real*)&valueMax) || bValueChanged;
			}
			else
			{
				bValueChanged = ImGui::DragFloat3(label, &((glm::vec3*)valuePtr)->x) || bValueChanged;
			}
			break;
		case ValueType::VEC4:
			if (valueMin != nullptr && valueMax != nullptr)
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
			if (ImGui::SliderFloat3(label, &rotEuler.x, *(real*)&valueMin, *(real*)&valueMax))
			{
				*(glm::quat*)valuePtr = glm::quat(rotEuler);
				bValueChanged = true;
			}
		} break;
		default:
			PrintError("Unhandled value type in ConfigFile::DrawImGuiObjects\n");
			break;
		}

		return bValueChanged;
	}
} // namespace flex
