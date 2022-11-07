#include "stdafx.hpp"

#include "PropertyCollection.hpp"

#include "Helpers.hpp"
#include "JSONTypes.hpp"
#include "Scene/GameObject.hpp"
#include "ResourceManager.hpp"

namespace flex
{
	PropertyValue PropertyCollection::s_EmptyPropertyValue = PropertyValue("", ValueType::UNINITIALIZED, 0);

	PropertyCollection::PropertyCollection(const char* name) :
		name(name)
	{
	}

	PropertyCollection::PropertyCollection(const PropertyCollection& other)
	{
		values = std::map<const char*, PropertyValue>(other.values);
		name = other.name;
	}

	PropertyCollection::PropertyCollection(const PropertyCollection&& other) noexcept
	{
		values = std::map<const char*, PropertyValue>(other.values);
		name = other.name;
	}

	PropertyCollection& PropertyCollection::operator=(const PropertyCollection& other)
	{
		if (this != &other)
		{
			values = std::map<const char*, PropertyValue>(other.values);
			name = other.name;
		}
		return *this;
	}

	PropertyCollection& PropertyCollection::operator=(const PropertyCollection&& other) noexcept
	{
		if (this != &other)
		{
			values = std::map<const char*, PropertyValue>(other.values);
			name = other.name;
		}
		return *this;
	}

	PropertyValue::PropertyValue(const PropertyValue& other)
	{
		memcpy(this, &other, sizeof(PropertyValue));
	}

	PropertyValue::PropertyValue(const PropertyValue&& other)
	{
		memcpy(this, &other, sizeof(PropertyValue));
	}

	PropertyValue& PropertyValue::operator=(const PropertyValue& other)
	{
		memcpy(this, &other, sizeof(PropertyValue));
		return *this;
	}

	PropertyValue& PropertyValue::operator=(const PropertyValue&& other)
	{
		memcpy(this, &other, sizeof(PropertyValue));
		return *this;
	}

	PropertyValue& PropertyCollection::RegisterProperty(const char* propertyName, size_t offset, const std::type_info& type_info)
	{
		if (type_info == typeid(real)) return RegisterProperty(propertyName, offset, ValueType::FLOAT);
		if (type_info == typeid(i32)) return RegisterProperty(propertyName, offset, ValueType::INT);
		if (type_info == typeid(u32)) return RegisterProperty(propertyName, offset, ValueType::UINT);
		if (type_info == typeid(bool)) return RegisterProperty(propertyName, offset, ValueType::BOOL);
		if (type_info == typeid(glm::vec2)) return RegisterProperty(propertyName, offset, ValueType::VEC2);
		if (type_info == typeid(glm::vec3)) return RegisterProperty(propertyName, offset, ValueType::VEC3);
		if (type_info == typeid(glm::vec4)) return RegisterProperty(propertyName, offset, ValueType::VEC4);
		if (type_info == typeid(glm::quat)) return RegisterProperty(propertyName, offset, ValueType::QUAT);
		if (type_info == typeid(std::string)) return RegisterProperty(propertyName, offset, ValueType::STRING);
		if (type_info == typeid(GUID)) return RegisterProperty(propertyName, offset, ValueType::GUID);
		if (type_info == typeid(GameObjectID)) return RegisterProperty(propertyName, offset, ValueType::GUID);
		if (type_info == typeid(EditorObjectID)) return RegisterProperty(propertyName, offset, ValueType::GUID);
		ENSURE_NO_ENTRY();
		return s_EmptyPropertyValue;
	}

	PropertyValue& PropertyCollection::RegisterProperty(const char* propertyName, size_t offset, ValueType type)
	{
		auto pair = values.emplace(propertyName, PropertyValue(propertyName, type, offset));
		return pair.first->second;
	}

	bool PropertyCollection::DrawImGuiForObject(GameObject* gameObject)
	{
		bool bValueChanged = false;
		for (auto& valuePair : values)
		{
			if (valuePair.second.DrawImGui(gameObject))
			{
				bValueChanged = true;
			}
		}

		return bValueChanged;
	}

	bool PropertyValue::DrawImGui(GameObject* gameObject)
	{
		bool bValueChanged = false;

		u8* valuePtr = ((u8*)gameObject) + offset;

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

	u32 PropertyValue::GetPrecision() const
	{
		return precisionSet != 0 ? *(u32*)&precision : JSONValue::DEFAULT_FLOAT_PRECISION;
	}

} // namespace flex
