#include "stdafx.hpp"

IGNORE_WARNINGS_PUSH

#include <LinearMath/btIDebugDraw.h>

#include <glm/gtx/norm.hpp> // for distance2
#include <glm/gtx/quaternion.hpp> // for rotate
#include <glm/gtx/vector_angle.hpp> // for angle
#include <glm/ext/matrix_transform.hpp> // for rotate

#include <imgui/imgui_internal.h> // For PushItemFlag

IGNORE_WARNINGS_POP

#include "Scene/Objects/Lights.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "FlexEngine.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "PropertyCollection.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Objects/Wires.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	namespace
	{
		real FlickerHash(i32 i, u32 seed)
		{
			u32 x = (u32)i * 0x8da6b343u + seed * 0xd8163841u;
			x ^= x >> 16;
			x *= 0x7feb352du;
			x ^= x >> 15;
			x *= 0x846ca68bu;
			x ^= x >> 16;
			return (real)(x & 0xFFFFFF) / (real)0x1000000;
		}

		// Smooth 1D value noise in [0, 1]
		real FlickerNoise(real t, u32 seed)
		{
			const real f = std::floor(t);
			const real u = t - f;
			const real s = u * u * u * (u * (u * 6.0f - 15.0f) + 10.0f);
			const i32 i = (i32)f;
			return glm::mix(FlickerHash(i, seed), FlickerHash(i + 1, seed), s);
		}
	} // anonymous namespace

	real FireFlicker(real time, real seed)
	{
		// (keeps precision for seeds derived from large world positions)
		seed -= std::floor(seed / 1000.0f) * 1000.0f;
		const u32 seedBits = (u32)(i32)(seed * 977.0f);
		// Slow swell, a lively mid frequency & fast jitter
		const real slow = FlickerNoise(time * 1.3f + seed, seedBits);
		const real mid = FlickerNoise(time * 4.7f + seed * 1.7f, seedBits + 1);
		const real fast = FlickerNoise(time * 13.0f + seed * 2.3f, seedBits + 2);
		real flicker = 0.62f + 0.18f * slow + 0.22f * mid + 0.12f * fast;
		// Every so often the flame gutters briefly
		const real gutter = FlickerNoise(time * 0.9f + seed * 3.1f, seedBits + 3);
		flicker -= 0.3f * glm::smoothstep(0.8f, 0.95f, gutter);
		return flicker;
	}

	DirectionalLight::DirectionalLight() :
		DirectionalLight("Directional Light")
	{
	}

	DirectionalLight::DirectionalLight(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, DirectionalLightSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		data.enabled = m_bVisible ? 1 : 0;
		data.dir = VEC3_RIGHT;
		data.colour = VEC3_ONE;
		data.brightness = 1.0f;
		data.castShadows = 1;
		data.shadowDarkness = 1.0f;
		data.pad[0] = data.pad[1] = 0;
	}

	PropertyCollection* DirectionalLight::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("directional light info");

		collection->childCollection->RegisterProperty("colour", FIELD_OFFSET_AND_TYPE(DirectionalLight, data.colour))
			.VersionAdded(2)
			.Precision(2)
			.DefaultValue(VEC3_ONE);
		collection->childCollection->RegisterProperty("brightness", FIELD_OFFSET_AND_TYPE(DirectionalLight, data.brightness))
			.VersionAdded(2)
			.Precision(2)
			.DefaultValue(1.0f);
		collection->childCollection->RegisterProperty("cast shadows", FIELD_OFFSET_AND_TYPE(DirectionalLight, data.castShadows))
			.VersionAdded(2)
			.DefaultValue(1);
		collection->childCollection->RegisterProperty("shadow darkness", FIELD_OFFSET_AND_TYPE(DirectionalLight, data.shadowDarkness))
			.VersionAdded(2)
			.Precision(2)
			.DefaultValue(1.0f);

		return collection;
	}

	void DirectionalLight::Initialize()
	{
		g_Renderer->RegisterDirectionalLight(this);
		data.dir = glm::rotate(m_Transform.GetWorldRotation(), VEC3_RIGHT);

		GameObject::Initialize();
	}

	void DirectionalLight::Destroy(bool bDetachFromParent /* = true */)
	{
		g_Renderer->RemoveDirectionalLight();

		GameObject::Destroy(bDetachFromParent);
	}

	void DirectionalLight::Update()
	{
		PROFILE_AUTO("DirectionalLight Update");

		PluggablesSystem* pluggablesSystem = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES);
		std::vector<SocketData> const* sockets = pluggablesSystem->GetGameObjectSockets(ID);
		if (sockets != nullptr)
		{
			Socket* socket = (Socket*)((*sockets)[0].socketID.Get());
			i32 receivedSignal = pluggablesSystem->GetReceivedSignal(socket);
			if (receivedSignal != -1)
			{
				m_bVisible = receivedSignal == 1;
				data.enabled = m_bVisible ? 1 : 0;
			}
		}

		if (data.enabled && g_EngineInstance->IsRenderingEditorObjects())
		{
			pos = m_Transform.GetWorldPosition();
			data.dir = glm::rotate(m_Transform.GetWorldRotation(), -VEC3_FORWARD);

			BaseCamera* cam = g_CameraManager->CurrentCamera();

			if (!cam->bIsGameplayCam)
			{
				// Debug lines
				{
					DebugRenderer* debugRenderer = g_Renderer->GetDebugRenderer();

					glm::vec3 forward = m_Transform.GetForward();
					glm::vec3 right = m_Transform.GetRight();
					glm::vec3 up = m_Transform.GetUp();
					real lineLength = 2.0f;
					real spacing = 0.75f;
					glm::vec3 scaledForward = forward * lineLength;
					glm::vec3 offsets[] = { -up, up, -right, right, VEC3_ZERO };
					btVector3 lineColour = ToBtVec3(data.colour);
					for (const glm::vec3& offset : offsets)
					{
						glm::vec3 basePos = pos + offset * spacing;
						btVector3 lightPos = ToBtVec3(basePos);
						btVector3 lineEnd = ToBtVec3(basePos + scaledForward);
						debugRenderer->drawLine(lightPos, lineEnd, lineColour);
					}
				}

				// Sprite
				{
					const real minSpriteDist = 1.5f;
					const real maxSpriteDist = 3.0f;

					glm::vec3 scale(1.0f, -1.0f, 1.0f);

					SpriteQuadDrawInfo drawInfo = {};
					drawInfo.bScreenSpace = false;
					drawInfo.bReadDepth = true;
					drawInfo.scale = scale;
					drawInfo.materialID = g_Renderer->m_SpriteMatWSID;

					glm::vec3 camPos = cam->position;
					glm::vec3 camUp = cam->up;
					drawInfo.textureID = g_Renderer->directionalLightIconID;
					drawInfo.pos = pos;
					glm::mat4 rotMat = glm::lookAt(camPos, pos, camUp);
					drawInfo.rotation = glm::conjugate(glm::toQuat(rotMat));
					real alpha = Saturate(glm::distance(drawInfo.pos, camPos) / maxSpriteDist - minSpriteDist);
					drawInfo.colour = glm::vec4(data.colour * 1.5f, alpha);
					g_Renderer->EnqueueSprite(drawInfo);
				}
			}
		}

		GameObject::Update();
	}

	void DirectionalLight::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		static const ImGuiColorEditFlags colorEditFlags =
			ImGuiColorEditFlags_NoInputs |
			ImGuiColorEditFlags_Float |
			ImGuiColorEditFlags_RGB |
			ImGuiColorEditFlags_PickerHueWheel |
			ImGuiColorEditFlags_HDR;

		ImGui::Text("Directional Light");

		bool bVisible = m_bVisible;
		if (ImGui::Checkbox("Enabled", &bVisible))
		{
			m_bVisible = bVisible;
			data.enabled = bVisible ? 1 : 0;
		}

		ImGui::SameLine();
		ImGui::ColorEdit4("Colour ", &data.colour.r, colorEditFlags);
		ImGui::SliderFloat("Brightness", &data.brightness, 0.0f, 15.0f);

		ImGui::Spacing();
		ImGui::Text("Shadow");

		bool bCastShadows = data.castShadows != 0;
		if (ImGui::Checkbox("Cast shadow", &bCastShadows))
		{
			data.castShadows = bCastShadows ? 1 : 0;
		}
		ImGui::SliderFloat("Shadow darkness", &data.shadowDarkness, 0.0f, 1.0f);
	}

	void DirectionalLight::SetVisible(bool bVisible, bool bEffectChildren /* = true */)
	{
		data.enabled = (bVisible ? 1 : 0);
		GameObject::SetVisible(bVisible, bEffectChildren);
	}

	void DirectionalLight::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		i32 fileVersion = g_SceneManager->CurrentScene()->GetSceneFileVersion();

		JSONObject directionalLightObj;
		if (parentObject.TryGetObject("directional light info", directionalLightObj))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(directionalLightObj, collection->childCollection);

			if (fileVersion < 2)
			{
				// Rotation is saved using euler angles in these versions
				glm::vec3 rot = directionalLightObj.GetVec3("rotation");
				m_Transform.SetWorldRotation(glm::quat(rot));
			}
			else if (fileVersion <= 6)
			{
				// Rotation is saved explicitly in these versions
				glm::quat rot = directionalLightObj.GetQuat("rotation");
				m_Transform.SetWorldRotation(rot);
				data.dir = glm::rotate(m_Transform.GetWorldRotation(), VEC3_RIGHT);
			}
			else
			{
				// Rotation is saved simply through the transform in this version
			}

			data.enabled = m_bVisible ? 1 : 0;
		}
	}

	void DirectionalLight::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject dirLightObj = {};
		SerializeRegisteredProperties(dirLightObj, collection->childCollection, bSerializePrefabData);
		if (!dirLightObj.fields.empty())
		{
			parentObject.fields.emplace_back("directional light info", JSONValue(dirLightObj));
		}
	}

	bool DirectionalLight::operator==(const DirectionalLight& other)
	{
		return other.m_Transform.GetLocalRotation() == m_Transform.GetLocalRotation() &&
			other.data.dir == data.dir &&
			other.data.colour == data.colour &&
			other.m_bVisible == m_bVisible &&
			other.data.brightness == data.brightness;
	}

	PointLight::PointLight(BaseScene* scene) :
		PointLight(scene->GetUniqueObjectName("PointLight_", 2))
	{
	}

	PointLight::PointLight(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, PointLightSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		data.enabled = 1;
		data.pos = m_Transform.GetWorldPosition();
		data.colour = VEC3_ONE;
		data.brightness = 500.0f;
	}

	PropertyCollection* PointLight::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("point light info");

		collection->childCollection->RegisterProperty("colour", FIELD_OFFSET_AND_TYPE(PointLight, data.colour))
			.VersionAdded(2)
			.Precision(2)
			.DefaultValue(VEC3_ONE);
		collection->childCollection->RegisterProperty("brightness", FIELD_OFFSET_AND_TYPE(PointLight, data.brightness))
			.VersionAdded(2)
			.Precision(3)
			.DefaultValue(1.0f);
		collection->childCollection->RegisterProperty("enabled", FIELD_OFFSET_AND_TYPE(PointLight, data.enabled))
			.VersionAdded(2)
			.DefaultValue(1);

		return collection;
	}

	GameObject* PointLight::CopySelf(
		GameObject* parent /* = nullptr */,
		CopyFlags copyFlags /* = CopyFlags::ALL */,
		std::string* optionalName /* = nullptr */,
		const GameObjectID& optionalGameObjectID /* = InvalidGameObjectID */)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);

		// Copy it anyway (Initialize disables lights over the limit): callers such as prefab instancing expect every
		// child to be copied, & a missing child (e.g. a vehicle's headlight) broke the copied object
		if (g_Renderer->GetNumPointLights() >= MAX_POINT_LIGHT_COUNT)
		{
			PrintWarn("Duplicating point light past the limit (%i), it will be disabled\n", MAX_POINT_LIGHT_COUNT);
		}

		PointLight* newGameObject = new PointLight(newObjectName, newGameObjectID);

		memcpy(&newGameObject->data, &data, sizeof(PointLightData));
		// newGameObject->pointLightID will be filled out in Initialize

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void PointLight::Initialize()
	{
		if (pointLightID == InvalidPointLightID)
		{
			pointLightID = g_Renderer->RegisterPointLight(&data);
			if (pointLightID == InvalidPointLightID)
			{
				PrintError("More point lights added to scene than possible (%d). Additional lights will be disabled\n", MAX_POINT_LIGHT_COUNT);
				m_bVisible = false;
				data.enabled = false;
			}
		}

		GameObject::Initialize();
	}

	void PointLight::Destroy(bool bDetachFromParent /* = true */)
	{
		if (pointLightID != InvalidPointLightID)
		{
			g_Renderer->RemovePointLight(pointLightID);
			pointLightID = InvalidPointLightID;
		}

		GameObject::Destroy(bDetachFromParent);
	}

	void PointLight::Update()
	{
		PROFILE_AUTO("PointLight Update");

		PluggablesSystem* pluggablesSystem = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES);
		std::vector<SocketData> const* sockets = pluggablesSystem->GetGameObjectSockets(ID);
		if (sockets != nullptr)
		{
			Socket* socket = (Socket*)((*sockets)[0].socketID.Get());
			i32 receivedSignal = pluggablesSystem->GetReceivedSignal(socket);
			if (receivedSignal != -1)
			{
				m_bVisible = receivedSignal == 1;
				data.enabled = m_bVisible ? 1 : 0;
				g_Renderer->UpdatePointLightData(pointLightID, &data);
			}
		}

		if (data.enabled)
		{
			data.pos = m_Transform.GetWorldPosition();
			g_Renderer->UpdatePointLightData(pointLightID, &data);

			if (g_EngineInstance->IsRenderingEditorObjects())
			{
				BaseCamera* cam = g_CameraManager->CurrentCamera();

				if (!cam->bIsGameplayCam)
				{
					const real minSpriteDist = 1.5f;
					const real maxSpriteDist = 3.0f;

					glm::vec3 scale(1.0f, -1.0f, 1.0f);

					SpriteQuadDrawInfo drawInfo = {};
					drawInfo.bScreenSpace = false;
					drawInfo.bReadDepth = true;
					drawInfo.scale = scale;
					drawInfo.materialID = g_Renderer->m_SpriteMatWSID;

					glm::vec3 camPos = cam->position;
					glm::vec3 camUp = cam->up;

					drawInfo.textureID = g_Renderer->pointLightIconID;
					// TODO: Sort back to front? Or clear depth and then enable depth test
					drawInfo.pos = data.pos;
					glm::mat4 rotMat = glm::lookAt(drawInfo.pos, camPos, camUp);
					drawInfo.rotation = glm::conjugate(glm::toQuat(rotMat));
					real alpha = Saturate(glm::distance(drawInfo.pos, camPos) / maxSpriteDist - minSpriteDist);
					drawInfo.colour = glm::vec4(data.colour * 1.5f, alpha);
					g_Renderer->EnqueueSprite(drawInfo);
				}
			}
		}

		GameObject::Update();
	}

	void PointLight::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		if (pointLightID != InvalidPointLightID)
		{
			static const ImGuiColorEditFlags colorEditFlags =
				ImGuiColorEditFlags_NoInputs |
				ImGuiColorEditFlags_Float |
				ImGuiColorEditFlags_RGB |
				ImGuiColorEditFlags_PickerHueWheel |
				ImGuiColorEditFlags_HDR;

			ImGui::Text("Point Light");
			bool bRemovedPointLight = false;
			bool bEditedPointLightData = false;

			if (!bRemovedPointLight)
			{
				bool bVisible = m_bVisible;
				if (ImGui::Checkbox("Enabled", &bVisible))
				{
					m_bVisible = bVisible;
					bEditedPointLightData = true;
					data.enabled = bVisible ? 1 : 0;
				}

				ImGui::SameLine();
				bEditedPointLightData |= ImGui::ColorEdit4("Colour ", &data.colour.r, colorEditFlags);
				bEditedPointLightData |= ImGui::SliderFloat("Brightness", &data.brightness, 0.0f, 1000.0f);

				if (bEditedPointLightData)
				{
					g_Renderer->UpdatePointLightData(pointLightID, &data);
				}
			}
		}
	}

	void PointLight::SetVisible(bool bVisible, bool bEffectChildren /* = true */)
	{
		data.enabled = (bVisible ? 1 : 0);
		GameObject::SetVisible(bVisible, bEffectChildren);

		if (pointLightID != InvalidPointLightID)
		{
			g_Renderer->UpdatePointLightData(pointLightID, &data);
		}
	}

	void PointLight::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject pointLightObj;
		if (parentObject.TryGetObject("point light info", pointLightObj))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(pointLightObj, collection->childCollection);
		}
	}

	void PointLight::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject pointLightObj = {};
		SerializeRegisteredProperties(pointLightObj, collection->childCollection, bSerializePrefabData);
		if (!pointLightObj.fields.empty())
		{
			parentObject.fields.emplace_back("point light info", JSONValue(pointLightObj));
		}
	}

	bool PointLight::operator==(const PointLight& other)
	{
		return other.data.pos == data.pos &&
			other.data.colour == data.colour &&
			other.data.enabled == data.enabled &&
			other.data.brightness == data.brightness;
	}

	SpotLight::SpotLight(BaseScene* scene) :
		SpotLight(scene->GetUniqueObjectName("SpotLight_", 2))
	{
	}

	SpotLight::SpotLight(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, SpotLightSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		data.enabled = 1;
		data.pos = m_Transform.GetWorldPosition();
		data.colour = VEC3_ONE;
		data.brightness = 500.0f;
		data.dir = VEC3_RIGHT;
		data.angle = 0.0f;
	}

	PropertyCollection* SpotLight::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("spot light info");

		collection->childCollection->RegisterProperty("colour", FIELD_OFFSET_AND_TYPE(SpotLight, data.colour))
			.VersionAdded(2)
			.Precision(2)
			.DefaultValue(VEC3_ONE);
		collection->childCollection->RegisterProperty("brightness", FIELD_OFFSET_AND_TYPE(SpotLight, data.brightness))
			.VersionAdded(2)
			.Precision(3)
			.DefaultValue(1.0f);
		collection->childCollection->RegisterProperty("enabled", FIELD_OFFSET_AND_TYPE(SpotLight, data.enabled))
			.VersionAdded(2)
			.DefaultValue(1);
		collection->childCollection->RegisterProperty("angle", FIELD_OFFSET_AND_TYPE(SpotLight, data.angle))
			.VersionAdded(2)
			.Precision(3)
			.DefaultValue(0.0f);

		return collection;
	}

	GameObject* SpotLight::CopySelf(GameObject* parent, CopyFlags copyFlags, std::string* optionalName, const GameObjectID& optionalGameObjectID)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);

		// Copy it anyway (Initialize disables lights over the limit): callers such as prefab instancing expect every
		// child to be copied, & a missing child (e.g. a vehicle's headlight) broke the copied object
		if (g_Renderer->GetNumSpotLights() >= MAX_SPOT_LIGHT_COUNT)
		{
			PrintWarn("Duplicating spot light past the limit (%i), it will be disabled\n", MAX_SPOT_LIGHT_COUNT);
		}

		SpotLight* newGameObject = new SpotLight(newObjectName, newGameObjectID);

		memcpy(&newGameObject->data, &data, sizeof(SpotLightData));
		// newGameObject->spotLightID will be filled out in Initialize

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void SpotLight::Initialize()
	{
		if (spotLightID == InvalidSpotLightID)
		{
			spotLightID = g_Renderer->RegisterSpotLight(&data);
			if (spotLightID == InvalidSpotLightID)
			{
				PrintError("More spot lights added to scene than possible (%d). Additional lights will be disabled\n", MAX_SPOT_LIGHT_COUNT);
				m_bVisible = false;
				data.enabled = 0;
			}
		}

		GameObject::Initialize();
	}

	void SpotLight::Destroy(bool bDetachFromParent)
	{
		if (spotLightID != InvalidSpotLightID)
		{
			g_Renderer->RemoveSpotLight(spotLightID);
			spotLightID = InvalidSpotLightID;
		}

		// (Also for lights disabled for being over the limit: skipping this left them attached to their parent, which
		// then destroyed the deleted child again, crashing when a vehicle was picked up)
		GameObject::Destroy(bDetachFromParent);
	}

	void SpotLight::Update()
	{
		PROFILE_AUTO("SpotLight Update");

		PluggablesSystem* pluggablesSystem = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES);
		std::vector<SocketData> const* sockets = pluggablesSystem->GetGameObjectSockets(ID);
		if (sockets != nullptr)
		{
			Socket* socket = (Socket*)((*sockets)[0].socketID.Get());
			i32 receivedSignal = pluggablesSystem->GetReceivedSignal(socket);
			if (receivedSignal != -1)
			{
				m_bVisible = receivedSignal == 1;
				data.enabled = m_bVisible ? 1 : 0;
				g_Renderer->UpdateSpotLightData(spotLightID, &data);
			}
		}

		if (data.enabled)
		{
			data.pos = m_Transform.GetWorldPosition();
			data.dir = glm::rotate(m_Transform.GetWorldRotation(), -VEC3_FORWARD);
			g_Renderer->UpdateSpotLightData(spotLightID, &data);

			if (g_EngineInstance->IsRenderingEditorObjects())
			{
				BaseCamera* cam = g_CameraManager->CurrentCamera();

				if (!cam->bIsGameplayCam)
				{
					const real minSpriteDist = 1.5f;
					const real maxSpriteDist = 3.0f;

					glm::vec3 scale(1.0f, -1.0f, 1.0f);

					SpriteQuadDrawInfo drawInfo = {};
					drawInfo.bScreenSpace = false;
					drawInfo.bReadDepth = true;
					drawInfo.scale = scale;
					drawInfo.materialID = g_Renderer->m_SpriteMatWSID;

					glm::vec3 camPos = cam->position;
					glm::vec3 camUp = cam->up;

					drawInfo.textureID = g_Renderer->spotLightIconID;
					// TODO: Sort back to front? Or clear depth and then enable depth test
					drawInfo.pos = data.pos;
					glm::mat4 rotMat = glm::lookAt(drawInfo.pos, camPos, camUp);
					drawInfo.rotation = glm::conjugate(glm::toQuat(rotMat));
					real alpha = Saturate(glm::distance(drawInfo.pos, camPos) / maxSpriteDist - minSpriteDist);
					drawInfo.colour = glm::vec4(data.colour * 1.5f, alpha);
					g_Renderer->EnqueueSprite(drawInfo);
				}
			}
		}

		GameObject::Update();
	}

	void SpotLight::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		if (spotLightID != InvalidSpotLightID)
		{
			static const ImGuiColorEditFlags colorEditFlags =
				ImGuiColorEditFlags_NoInputs |
				ImGuiColorEditFlags_Float |
				ImGuiColorEditFlags_RGB |
				ImGuiColorEditFlags_PickerHueWheel |
				ImGuiColorEditFlags_HDR;

			ImGui::Text("Spot Light");
			bool bRemovedSpotLight = false;
			bool bEditedSpotLightData = false;

			if (!bRemovedSpotLight)
			{
				bool bVisible = m_bVisible;
				if (ImGui::Checkbox("Enabled", &bVisible))
				{
					m_bVisible = bVisible;
					bEditedSpotLightData = true;
					data.enabled = bVisible ? 1 : 0;
				}

				ImGui::SameLine();
				bEditedSpotLightData |= ImGui::ColorEdit4("Colour ", &data.colour.r, colorEditFlags);
				bEditedSpotLightData |= ImGui::SliderFloat("Brightness", &data.brightness, 0.0f, 1000.0f);
				bEditedSpotLightData |= ImGui::SliderFloat("Cone angle", &data.angle, 0.0f, 1.0f);

				if (bEditedSpotLightData)
				{
					g_Renderer->UpdateSpotLightData(spotLightID, &data);
				}
			}
		}
	}

	void SpotLight::SetVisible(bool bVisible, bool bEffectChildren)
	{
		data.enabled = (bVisible ? 1 : 0);
		GameObject::SetVisible(bVisible, bEffectChildren);

		if (spotLightID != InvalidSpotLightID)
		{
			g_Renderer->UpdateSpotLightData(spotLightID, &data);
		}
	}

	bool SpotLight::operator==(const SpotLight& other)
	{
		return other.data.pos == data.pos &&
			other.data.colour == data.colour &&
			other.data.enabled == data.enabled &&
			other.data.brightness == data.brightness &&
			other.data.dir == data.dir &&
			other.data.angle == data.angle;
	}

	void SpotLight::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject spotLightObj;
		if (parentObject.TryGetObject("spot light info", spotLightObj))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(spotLightObj, collection->childCollection);
		}
	}

	void SpotLight::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject spotLightObj = {};
		SerializeRegisteredProperties(spotLightObj, collection->childCollection, bSerializePrefabData);
		if (!spotLightObj.fields.empty())
		{
			parentObject.fields.emplace_back("spot light info", JSONValue(spotLightObj));
		}
	}

	AreaLight::AreaLight(BaseScene* scene) :
		AreaLight(scene->GetUniqueObjectName("AreaLight_", 2))
	{
	}

	AreaLight::AreaLight(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, AreaLightSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		data.enabled = 1;
		data.colour = VEC3_ONE;
		data.brightness = 500.0f;
		UpdatePoints();
}

	PropertyCollection* AreaLight::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("area light info");

		collection->childCollection->RegisterProperty("colour", FIELD_OFFSET_AND_TYPE(AreaLight, data.colour))
			.VersionAdded(2)
			.Precision(2)
			.DefaultValue(VEC3_ONE);
		collection->childCollection->RegisterProperty("brightness", FIELD_OFFSET_AND_TYPE(AreaLight, data.brightness))
			.VersionAdded(2)
			.DefaultValue(500.0f);
		collection->childCollection->RegisterProperty("enabled", FIELD_OFFSET_AND_TYPE(AreaLight, data.enabled))
			.VersionAdded(2)
			.DefaultValue(1);

		return collection;
	}

	GameObject* AreaLight::CopySelf(GameObject* parent, CopyFlags copyFlags, std::string* optionalName, const GameObjectID& optionalGameObjectID)
	{
		std::string newObjectName;
		GameObjectID newGameObjectID = optionalGameObjectID;
		GetNewObjectNameAndID(copyFlags, optionalName, parent, newObjectName, newGameObjectID);

		// Copy it anyway (Initialize disables lights over the limit): callers such as prefab instancing expect every
		// child to be copied, & a missing child (e.g. a vehicle's headlight) broke the copied object
		if (g_Renderer->GetNumAreaLights() >= MAX_AREA_LIGHT_COUNT)
		{
			PrintWarn("Duplicating area light past the limit (%i), it will be disabled\n", MAX_AREA_LIGHT_COUNT);
		}

		AreaLight* newGameObject = new AreaLight(newObjectName, newGameObjectID);

		memcpy(&newGameObject->data, &data, sizeof(AreaLightData));
		// newGameObject->areaLightID will be filled out in Initialize

		CopyGenericFields(newGameObject, parent, copyFlags);

		return newGameObject;
	}

	void AreaLight::Initialize()
	{
		if (areaLightID == InvalidAreaLightID)
		{
			areaLightID = g_Renderer->RegisterAreaLight(&data);
			if (areaLightID == InvalidAreaLightID)
			{
				PrintError("More area lights added to scene than possible (%d). Additional lights will be disabled\n", MAX_AREA_LIGHT_COUNT);
				m_bVisible = false;
				data.enabled = false;
			}
		}

		GameObject::Initialize();
	}

	void AreaLight::Destroy(bool bDetachFromParent)
	{
		if (areaLightID != InvalidAreaLightID)
		{
			g_Renderer->RemoveAreaLight(areaLightID);
			areaLightID = InvalidAreaLightID;
		}

		GameObject::Destroy(bDetachFromParent);
	}

	void AreaLight::Update()
	{
		PROFILE_AUTO("AreaLight Update");

		PluggablesSystem* pluggablesSystem = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES);
		std::vector<SocketData> const* sockets = pluggablesSystem->GetGameObjectSockets(ID);
		if (sockets != nullptr)
		{
			Socket* socket = (Socket*)((*sockets)[0].socketID.Get());
			i32 receivedSignal = pluggablesSystem->GetReceivedSignal(socket);
			if (receivedSignal != -1)
			{
				m_bVisible = receivedSignal == 1;
				data.enabled = m_bVisible ? 1 : 0;

				if (areaLightID != InvalidAreaLightID)
				{
					g_Renderer->UpdateAreaLightData(areaLightID, &data);
				}
			}
		}

		if (data.enabled)
		{
			UpdatePoints();

			g_Renderer->UpdateAreaLightData(areaLightID, &data);

			if (g_EngineInstance->IsRenderingEditorObjects())
			{
				DebugRenderer* debugRenderer = g_Renderer->GetDebugRenderer();

				btVector3 lineColour(0.1f, 0.8f, 0.1f);
				btVector3 p0(ToBtVec3(data.points[0]));
				btVector3 p1(ToBtVec3(data.points[1]));
				btVector3 p2(ToBtVec3(data.points[2]));
				btVector3 p3(ToBtVec3(data.points[3]));
				btVector3 center(ToBtVec3((data.points[0] + data.points[1] + data.points[2] + data.points[3]) / 4.0f));
				glm::vec3 dir = glm::normalize(glm::cross(glm::vec3(data.points[1]) - glm::vec3(data.points[0]), glm::vec3(data.points[2]) - glm::vec3(data.points[0])));
				btVector3 dirBt = ToBtVec3(dir);
				debugRenderer->drawLine(p0, p1, lineColour);
				debugRenderer->drawLine(p1, p2, lineColour);
				debugRenderer->drawLine(p2, p3, lineColour);
				debugRenderer->drawLine(p3, p0, lineColour);

				debugRenderer->drawLine(center, center + dirBt, lineColour);

				//BaseCamera* cam = g_CameraManager->CurrentCamera();
				//
				//if (!cam->bIsGameplayCam)
				//{
				//	const real minSpriteDist = 1.5f;
				//	const real maxSpriteDist = 3.0f;
				//
				//	glm::vec3 scale(1.0f, -1.0f, 1.0f);
				//
				//	SpriteQuadDrawInfo drawInfo = {};
				//	drawInfo.bScreenSpace = false;
				//	drawInfo.bReadDepth = true;
				//	drawInfo.scale = scale;
				//	drawInfo.materialID = g_Renderer->m_SpriteMatWSID;
				//
				//	glm::vec3 camPos = cam->position;
				//	glm::vec3 camUp = cam->up;
				//
				//	drawInfo.textureID = g_Renderer->areaLightIconID;
				//	// TODO: Sort back to front? Or clear depth and then enable depth test
				//	drawInfo.pos = m_Transform.GetWorldPosition();
				//	glm::mat4 rotMat = glm::lookAt(drawInfo.pos, camPos, camUp);
				//	drawInfo.rotation = glm::conjugate(glm::toQuat(rotMat));
				//	real alpha = Saturate(glm::distance(drawInfo.pos, camPos) / maxSpriteDist - minSpriteDist);
				//	drawInfo.colour = glm::vec4(data.colour * 1.5f, alpha);
				//	g_Renderer->EnqueueSprite(drawInfo);
				//}
			}
		}

		GameObject::Update();
	}

	void AreaLight::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		if (areaLightID != InvalidAreaLightID)
		{
			static const ImGuiColorEditFlags colorEditFlags =
				ImGuiColorEditFlags_NoInputs |
				ImGuiColorEditFlags_Float |
				ImGuiColorEditFlags_RGB |
				ImGuiColorEditFlags_PickerHueWheel |
				ImGuiColorEditFlags_HDR;

			ImGui::Text("Area Light");
			bool bRemovedAreaLight = false;
			bool bEditedAreaLightData = false;

			if (!bRemovedAreaLight)
			{
				bool bVisible = m_bVisible;
				if (ImGui::Checkbox("Enabled", &bVisible))
				{
					m_bVisible = bVisible;
					bEditedAreaLightData = true;
					data.enabled = bVisible ? 1 : 0;
				}

				ImGui::SameLine();
				bEditedAreaLightData |= ImGui::ColorEdit4("Colour ", &data.colour.r, colorEditFlags);
				bEditedAreaLightData |= ImGui::SliderFloat("Brightness", &data.brightness, 0.0f, 1000.0f);

				if (bEditedAreaLightData)
				{
					g_Renderer->UpdateAreaLightData(areaLightID, &data);
				}
			}
		}
	}

	void AreaLight::SetVisible(bool bVisible, bool bEffectChildren)
	{
		data.enabled = (bVisible ? 1 : 0);
		GameObject::SetVisible(bVisible, bEffectChildren);

		if (areaLightID != InvalidAreaLightID)
		{
			g_Renderer->UpdateAreaLightData(areaLightID, &data);
		}
	}

	bool AreaLight::operator==(const AreaLight& other)
	{
		return other.data.colour == data.colour &&
			other.data.enabled == data.enabled &&
			other.data.brightness == data.brightness;
	}

	void AreaLight::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject areaLightObj;
		if (parentObject.TryGetObject("area light info", areaLightObj))
		{
			PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

			DeserializeRegisteredProperties(areaLightObj, collection->childCollection);
		}
	}

	void AreaLight::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject areaLightObj = {};
		SerializeRegisteredProperties(areaLightObj, collection->childCollection, bSerializePrefabData);
		if (!areaLightObj.fields.empty())
		{
			parentObject.fields.emplace_back("area light info", JSONValue(areaLightObj));
		}
	}

	void AreaLight::UpdatePoints()
	{
		const glm::mat4& model = m_Transform.GetWorldTransform();
		data.points[0] = model * glm::vec4(-1.0f, 1.0f, 0.0f, 1.0f);
		data.points[1] = model * glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
		data.points[2] = model * glm::vec4(1.0f, -1.0f, 0.0f, 1.0f);
		data.points[3] = model * glm::vec4(-1.0f, -1.0f, 0.0f, 1.0f);
	}
} // namespace flex
