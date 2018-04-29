#include "stdafx.hpp"

#include "Scene/Scenes/BaseScene.hpp"

#include <glm/vec3.hpp>

#include <fstream>

#include "Scene/GameObject.hpp"
#include "Logger.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "JSONParser.hpp"

#include "Physics/PhysicsWorld.hpp"
#include "Physics/PhysicsManager.hpp"
#include "Physics/RigidBody.hpp"
#include "Scene/ReflectionProbe.hpp"
#include "Scene/MeshPrefab.hpp"
#include "Helpers.hpp"
#include "Cameras/CameraManager.hpp"
#include "Cameras/BaseCamera.hpp"

namespace flex
{
	BaseScene::BaseScene(const std::string& name, const std::string& jsonFilePath) :
		m_Name(name),
		m_JSONFilePath(jsonFilePath)
	{
	}

	BaseScene::~BaseScene()
	{
	}

	void BaseScene::Initialize(const GameContext& gameContext)
	{
		JSONObject sceneRootObject;
		if (!JSONParser::Parse(m_JSONFilePath, sceneRootObject))
		{
			Logger::LogError("Failed to parse scene JSON file \"" + m_JSONFilePath + "\"");
			return;
		}

		std::string friendlySceneFilepath = m_JSONFilePath.substr(RESOURCE_LOCATION.length());
		Logger::LogInfo("Loading scene from JSON file: " + friendlySceneFilepath);

		const bool printSceneContentsToConsole = false;
		if (printSceneContentsToConsole)
		{
			Logger::LogInfo("Parsed scene file:");
			Logger::LogInfo(sceneRootObject.Print(0));
		}

		int sceneVersion = sceneRootObject.GetInt("version");
		if (sceneVersion != 1)
		{
			if (sceneRootObject.HasField("version"))
			{
				Logger::LogError("Unhandled scene version! Max handled version: 1, This version: " + std::to_string(sceneVersion));
			}
			else
			{
				Logger::LogError("Scene version missing from scene file. Assuming version 1");
			}
		}

		sceneRootObject.SetStringChecked("name", m_Name);

		// This holds all the entities in the scene which do not have a parent
		const std::vector<JSONObject>& rootEntities = sceneRootObject.GetObjectArray("entities");
		for (const JSONObject& rootEntity : rootEntities)
		{
			AddChild(CreateEntityFromJSON(gameContext, rootEntity));
		}

		if (sceneRootObject.HasField("point lights"))
		{
			const std::vector<JSONObject>& pointLightsArray = sceneRootObject.GetObjectArray("point lights");

			for (const JSONObject& pointLightObj : pointLightsArray)
			{
				PointLight pointLight = {};
				CreatePointLightFromJSON(gameContext, pointLightObj, pointLight);

				m_PointLights.push_back(pointLight);
				gameContext.renderer->InitializePointLight(pointLight);
			}
		}

		if (sceneRootObject.HasField("directional light"))
		{
			const JSONObject& directionalLightObj = sceneRootObject.GetObject("directional light");

			DirectionalLight dirLight = {};
			CreateDirectionalLightFromJSON(gameContext, directionalLightObj, dirLight);
			
			m_DirectionalLight = dirLight;
			gameContext.renderer->InitializeDirectionalLight(dirLight);
		}


		// Physics world
		m_PhysicsWorld = new PhysicsWorld();
		m_PhysicsWorld->Initialize(gameContext);

		m_PhysicsWorld->GetWorld()->setGravity({ 0.0f, -9.81f, 0.0f });


		// Grid
		MaterialCreateInfo gridMatInfo = {};
		gridMatInfo.shaderName = "color";
		gridMatInfo.name = "Color";
		m_GridMaterialID = gameContext.renderer->InitializeMaterial(gameContext, &gridMatInfo);

		m_Grid = new MeshPrefab(m_GridMaterialID, "Grid");
		m_Grid->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::GRID);
		m_Grid->GetTransform()->Translate(0.0f, -0.1f, 0.0f);
		AddChild(m_Grid);

		MaterialCreateInfo worldAxisMatInfo = {};
		worldAxisMatInfo.shaderName = "color";
		worldAxisMatInfo.name = "Color";
		m_WorldAxisMaterialID = gameContext.renderer->InitializeMaterial(gameContext, &worldAxisMatInfo);

		m_WorldOrigin = new MeshPrefab(m_WorldAxisMaterialID, "World origin");
		m_WorldOrigin->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::WORLD_AXIS_GROUND);
		m_WorldOrigin->GetTransform()->Translate(0.0f, -0.09f, 0.0f);
		AddChild(m_WorldOrigin);


		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->Initialize(gameContext);
		}
	}

	void BaseScene::PostInitialize(const GameContext& gameContext)
	{
		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->PostInitialize(gameContext);
		}
	}

	void BaseScene::Destroy(const GameContext& gameContext)
	{
		for (auto child : m_Children)
		{
			if (child)
			{
				child->Destroy(gameContext);
				SafeDelete(child);
			}
		}
		m_Children.clear();

		if (m_PhysicsWorld)
		{
			m_PhysicsWorld->Destroy();
			SafeDelete(m_PhysicsWorld);
		}
	}

	void BaseScene::Update(const GameContext& gameContext)
	{
		if (m_PhysicsWorld)
		{
			m_PhysicsWorld->Update(gameContext.deltaTime);
		}

		BaseCamera* camera = gameContext.cameraManager->CurrentCamera();

		// Fade grid out when far away
		{
			float maxHeightVisible = 350.0f;
			float distCamToGround = camera->GetPosition().y;
			float maxDistVisible = 300.0f;
			float distCamToOrigin = glm::distance(camera->GetPosition(), glm::vec3(0, 0, 0));

			glm::vec4 gridColorMutliplier = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f - glm::clamp(distCamToGround / maxHeightVisible, -1.0f, 1.0f));
			glm::vec4 axisColorMutliplier = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f - glm::clamp(distCamToOrigin / maxDistVisible, -1.0f, 1.0f));;
			gameContext.renderer->GetMaterial(m_WorldAxisMaterialID).colorMultiplier = axisColorMutliplier;
			gameContext.renderer->GetMaterial(m_GridMaterialID).colorMultiplier = gridColorMutliplier;
		}

		for (GameObject* child : m_Children)
		{
			if (child)
			{
				child->Update(gameContext);
			}
		}
	}

	GameObject* BaseScene::CreateEntityFromJSON(const GameContext& gameContext, const JSONObject& obj)
	{
		GameObject* result = nullptr;

		std::string entityTypeStr = obj.GetString("type");
		SerializableType entityType = StringToSerializableType(entityTypeStr);

		std::string objectName = obj.GetString("name");

		Transform transform = Transform::Identity();
		if (obj.HasField("transform"))
		{
			JSONObject transformObj = obj.GetObject("transform");
			transform = JSONParser::ParseTransform(transformObj);
		}

		switch (entityType)
		{
		case SerializableType::MESH:
		{
			JSONObject meshObj = obj.GetObject("mesh");
			std::string meshFilePath = meshObj.GetString("file");
			if (!meshFilePath.empty())
			{
				meshFilePath = RESOURCE_LOCATION + meshFilePath;
			}
			std::string meshPrefabName = meshObj.GetString("prefab");
			bool flipNormalYZ = meshObj.GetBool("flipNormalYZ");
			bool flipZ = meshObj.GetBool("flipZ");
			bool flipU = meshObj.GetBool("flipU");
			bool flipV = meshObj.GetBool("flipV");

			bool visibleInSceneGraph;
			obj.SetBoolChecked("visibleInSceneGraph", visibleInSceneGraph);
			bool visible;
			obj.SetBoolChecked("visible", visible);

			JSONObject material = obj.GetObject("material");
			std::string materialName = material.GetString("name");
			std::string shaderName = material.GetString("shader");

			if (shaderName.empty())
			{
				Logger::LogError("Can not create entity from JSON: shader name not set in material \"" + materialName + '\"');
				return nullptr;
			}

			ShaderID shaderID = InvalidShaderID;
			if (!gameContext.renderer->GetShaderID(shaderName, shaderID))
			{
				Logger::LogError("Shader ID not found in renderer! Shader name: \"" + shaderName + '\"');
				return nullptr;
			}

			Shader& shader = gameContext.renderer->GetShader(shaderID);
			VertexAttributes requiredVertexAttributes = shader.vertexAttributes;

			MaterialCreateInfo matCreateInfo = {};
			ParseMaterialJSONObject(material, matCreateInfo);
			MaterialID matID = gameContext.renderer->InitializeMaterial(gameContext, &matCreateInfo);

			if (!meshFilePath.empty())
			{
				MeshPrefab* mesh = new MeshPrefab(matID, objectName);
				mesh->SetRequiredAttributes(requiredVertexAttributes);

				RenderObjectCreateInfo createInfo = {};
				createInfo.visibleInSceneExplorer = visibleInSceneGraph;

				mesh->LoadFromFile(gameContext, 
					meshFilePath,
					flipNormalYZ,
					flipZ,
					flipU,
					flipV,
					&createInfo);

				result = mesh;
			}
			else if (!meshPrefabName.empty())
			{
				MeshPrefab* mesh = new MeshPrefab(matID, objectName);
				mesh->SetRequiredAttributes(requiredVertexAttributes);

				RenderObjectCreateInfo createInfo = {};
				createInfo.visibleInSceneExplorer = visibleInSceneGraph;

				MeshPrefab::PrefabShape prefabShape = MeshPrefab::StringToPrefabShape(meshPrefabName);
				mesh->LoadPrefabShape(gameContext, prefabShape, &createInfo);

				if (!visible)
				{
					gameContext.renderer->SetRenderObjectVisible(mesh->GetRenderID(), visible);
				}

				result = mesh;
			}
			else
			{
				Logger::LogError("Unhandled mesh object " + objectName);
			}
		} break;
		case SerializableType::SKYBOX:
		{
			JSONObject materialObj = obj.GetObject("material");

			MaterialCreateInfo skyboxMatInfo = {};
			ParseMaterialJSONObject(materialObj, skyboxMatInfo);
			const MaterialID skyboxMatID = gameContext.renderer->InitializeMaterial(gameContext, &skyboxMatInfo);

			gameContext.renderer->SetSkyboxMaterial(skyboxMatID, gameContext);

			glm::vec3 skyboxRotEuler;
			if (obj.SetVec3Checked("rotation", skyboxRotEuler))
			{
				glm::quat skyboxRotation = glm::quat(skyboxRotEuler);
				gameContext.renderer->SetSkyboxRotation(skyboxRotation);
			}
		} break;
		case SerializableType::REFLECTION_PROBE:
		{
			bool visible = true;
			obj.SetBoolChecked("visible", visible);

			glm::vec3 position = glm::vec3(0.0f);
			obj.SetVec3Checked("position", position);

			ReflectionProbe* reflectionProbe = new ReflectionProbe(objectName, visible, position);

			result = reflectionProbe;
		} break;
		case SerializableType::NONE:
			// Assume this object is an empty parent object which holds no data itself but a transform
			result = new GameObject(objectName, SerializableType::NONE);
			break;
		default:
			Logger::LogError("Unhandled entity type encountered when parsing scene file: " + entityTypeStr);
			break;
		}

		if (result)
		{
			result->m_Transform = transform;

			if (obj.HasField("children"))
			{
				std::vector<JSONObject> children = obj.GetObjectArray("children");
				for (const auto& child : children)
				{
					result->AddChild(CreateEntityFromJSON(gameContext, child));
				}
			}
		}

		return result;
	}

	void BaseScene::ParseMaterialJSONObject(const JSONObject& material, MaterialCreateInfo& createInfoOut)
	{
		material.SetStringChecked("name", createInfoOut.name);
		material.SetStringChecked("shader", createInfoOut.shaderName);

		struct FilePathMaterialParam
		{
			std::string* member;
			std::string name;
		};

		std::vector<FilePathMaterialParam> filePathParams =
		{
			{ &createInfoOut.diffuseTexturePath, "diffuse texture filepath" },
			{ &createInfoOut.normalTexturePath, "normal texture filepath" },
			{ &createInfoOut.albedoTexturePath, "albedo texture filepath" },
			{ &createInfoOut.metallicTexturePath, "metallic texture filepath" },
			{ &createInfoOut.roughnessTexturePath, "roughness texture filepath" },
			{ &createInfoOut.aoTexturePath, "ao texture filepath" },
			{ &createInfoOut.hdrEquirectangularTexturePath, "hdr equirectangular texture filepath" },
			{ &createInfoOut.environmentMapPath, "environment map path" },
		};

		for (u32 i = 0; i < filePathParams.size(); ++i)
		{
			if (material.HasField(filePathParams[i].name))
			{
				*filePathParams[i].member = RESOURCE_LOCATION +
					material.GetString(filePathParams[i].name);
			}
		}

		material.SetBoolChecked("generate diffuse sampler", createInfoOut.generateDiffuseSampler);
		material.SetBoolChecked("enable diffuse sampler", createInfoOut.enableDiffuseSampler);
		material.SetBoolChecked("generate normal sampler", createInfoOut.generateNormalSampler);
		material.SetBoolChecked("enable normal sampler", createInfoOut.enableNormalSampler);
		material.SetBoolChecked("generate albedo sampler", createInfoOut.generateAlbedoSampler);
		material.SetBoolChecked("enable albedo sampler", createInfoOut.enableAlbedoSampler);
		material.SetBoolChecked("generate metallic sampler", createInfoOut.generateMetallicSampler);
		material.SetBoolChecked("enable metallic sampler", createInfoOut.enableMetallicSampler);
		material.SetBoolChecked("generate roughness sampler", createInfoOut.generateRoughnessSampler);
		material.SetBoolChecked("enable roughness sampler", createInfoOut.enableRoughnessSampler);
		material.SetBoolChecked("generate ao sampler", createInfoOut.generateAOSampler);
		material.SetBoolChecked("enable ao sampler", createInfoOut.enableAOSampler);
		material.SetBoolChecked("generate hdr equirectangular sampler", createInfoOut.generateHDREquirectangularSampler);
		material.SetBoolChecked("enable hdr equirectangular sampler", createInfoOut.enableHDREquirectangularSampler);
		material.SetBoolChecked("generate hdr cubemap sampler", createInfoOut.generateHDRCubemapSampler);
		material.SetBoolChecked("enable irradiance sampler", createInfoOut.enableIrradianceSampler);
		material.SetBoolChecked("generate irradiance sampler", createInfoOut.generateIrradianceSampler);
		material.SetBoolChecked("enable brdf lut", createInfoOut.enableBRDFLUT);
		material.SetBoolChecked("render to cubemap", createInfoOut.renderToCubemap);
		material.SetBoolChecked("enable cubemap sampler", createInfoOut.enableCubemapSampler);
		material.SetBoolChecked("enable cubemap trilinear filtering", createInfoOut.enableCubemapTrilinearFiltering);
		material.SetBoolChecked("generate cubemap sampler", createInfoOut.generateCubemapSampler);
		material.SetBoolChecked("generate cubemap depth buffers", createInfoOut.generateCubemapDepthBuffers);
		material.SetBoolChecked("generate prefiltered map", createInfoOut.generatePrefilteredMap);
		material.SetBoolChecked("enable prefiltered map", createInfoOut.enablePrefilteredMap);
		material.SetBoolChecked("generate reflection probe maps", createInfoOut.generateReflectionProbeMaps);

		material.SetVec2Checked("generated irradiance cubemap size", createInfoOut.generatedIrradianceCubemapSize);
		material.SetVec2Checked("generated prefiltered map size", createInfoOut.generatedPrefilteredCubemapSize);
		material.SetVec2Checked("generated cubemap size", createInfoOut.generatedCubemapSize);
		material.SetVec4Checked("color multiplier", createInfoOut.colorMultiplier);
		material.SetVec3Checked("const albedo", createInfoOut.constAlbedo);
		material.SetFloatChecked("const metallic", createInfoOut.constMetallic);
		material.SetFloatChecked("cons roughness", createInfoOut.constRoughness);
		material.SetFloatChecked("const ao", createInfoOut.constAO);
	}

	void BaseScene::CreatePointLightFromJSON(const GameContext& gameContext, const JSONObject& obj, PointLight& pointLight)
	{
		std::string posStr = obj.GetString("position");
		pointLight.position = glm::vec4(ParseVec3(posStr), 0);

		obj.SetVec4Checked("color", pointLight.color);

		obj.SetFloatChecked("brightness", pointLight.brightness);

		if (obj.HasField("enabled"))
		{
			pointLight.enabled = obj.GetBool("enabled") ? 1 : 0;
		}

		obj.SetStringChecked("brightness", pointLight.name);

		obj.SetStringChecked("name", pointLight.name);
	}

	void BaseScene::CreateDirectionalLightFromJSON(const GameContext& gameContext, const JSONObject& obj, DirectionalLight& directionalLight)
	{
		std::string dirStr = obj.GetString("direction");
		directionalLight.direction = glm::vec4(ParseVec3(dirStr), 0);

		obj.SetVec4Checked("color", directionalLight.color);

		obj.SetFloatChecked("brightness", directionalLight.brightness);

		if (obj.HasField("enabled"))
		{
			directionalLight.enabled = obj.GetBool("enabled") ? 1 : 0;
		}
	}

	void BaseScene::SerializeToFile(const GameContext& gameContext)
	{
		JSONObject rootSceneObject = {};

		i32 fileVersion = 1;

		rootSceneObject.fields.push_back(JSONField("version", JSONValue(fileVersion)));

		rootSceneObject.fields.push_back(JSONField("name", JSONValue(m_Name)));


		JSONField entitiesField = {};
		entitiesField.label = "entities";

		std::vector<JSONObject> entitiesArray;
		for (GameObject* child : m_Children)
		{
			if (child->IsSerializable())
			{
				entitiesArray.push_back(SerializeObject(child, gameContext));
			}
		}
		entitiesField.value = JSONValue(entitiesArray);

		rootSceneObject.fields.push_back(entitiesField);


		JSONField pointLightsField = {};
		pointLightsField.label = "point lights";

		std::vector<JSONObject> pointLightsArray;
		for (PointLight& pointLight : m_PointLights)
		{
			pointLightsArray.push_back(SerializePointLight(pointLight, gameContext));
		}
		pointLightsField.value = JSONValue(pointLightsArray);

		rootSceneObject.fields.push_back(pointLightsField);


		JSONField directionalLightsField("directional light",
			JSONValue(SerializeDirectionalLight(m_DirectionalLight, gameContext)));

		rootSceneObject.fields.push_back(directionalLightsField);


		std::string fileContents = rootSceneObject.Print(0);

		std::ofstream fileStream;
		std::string fileName = m_JSONFilePath.substr(0, m_JSONFilePath.size() - 5);
		std::string newFilePath = fileName + "_saved.json";
		fileStream.open(newFilePath, std::ofstream::out | std::ofstream::trunc);

		Logger::LogInfo("Serializing scene to " + newFilePath);

		if (fileStream.is_open())
		{
			fileStream.write(fileContents.c_str(), fileContents.size());
		}
		else
		{
			Logger::LogError("Failed to open file for writing: " + m_JSONFilePath + ", Can't serialize scene");
		}

		Logger::LogInfo("Done serializing scene");

		fileStream.close();
	}

	JSONObject BaseScene::SerializeObject(GameObject* gameObject, const GameContext& gameContext)
	{
		assert(gameObject->m_Serializable);

		JSONObject object;
		std::string childName = gameObject->m_Name;

		object.fields.push_back(JSONField("name", JSONValue(childName)));

		SerializableType childType = gameObject->m_SerializableType;
		std::string childTypeStr = SerializableTypeToString(childType);
		object.fields.push_back(JSONField("type", JSONValue(childTypeStr)));

		switch (childType)
		{
		case SerializableType::MESH:
		{
			MeshPrefab* mesh = (MeshPrefab*)gameObject;

			JSONField meshField = {};
			meshField.label = "mesh";

			JSONObject meshValue = {};

			MeshPrefab::MeshType meshType = mesh->GetType();
			if (meshType == MeshPrefab::MeshType::FILE)
			{
				std::string meshFilepath = mesh->GetFilepath().substr(RESOURCE_LOCATION.length());
				meshValue.fields.push_back(JSONField("file", JSONValue(meshFilepath)));
			}
			else if (meshType == MeshPrefab::MeshType::PREFAB)
			{
				std::string prefabShapeStr = MeshPrefab::PrefabShapeToString(mesh->GetShape());
				meshValue.fields.push_back(JSONField("prefab", JSONValue(prefabShapeStr)));
			}
			else
			{
				Logger::LogError("Unhandled mesh prefab type when attempting to serialize scene!");
			}

			RenderID meshRenderID = mesh->GetRenderID();
			RenderObjectCreateInfo renderObjectCreateInfo;
			if (gameContext.renderer->GetRenderObjectCreateInfo(meshRenderID, renderObjectCreateInfo))
			{
				static const bool defaultVisible = true;
				if (renderObjectCreateInfo.visible != defaultVisible)
				{
					object.fields.push_back(JSONField("visible", JSONValue(renderObjectCreateInfo.visible)));
				}

				static const bool defaultVisibleInSceneGraph = true;
				if (renderObjectCreateInfo.visibleInSceneExplorer != defaultVisibleInSceneGraph)
				{
					object.fields.push_back(JSONField("visible in scene graph",
						JSONValue(renderObjectCreateInfo.visibleInSceneExplorer)));
				}

				if (!renderObjectCreateInfo.transform->IsIdentity())
				{
					JSONField transformField;
					if (JSONParser::SerializeTransform(renderObjectCreateInfo.transform, transformField))
					{
						object.fields.push_back(transformField);
					}
				}

				JSONField materialField = {};
				materialField.label = "material";

				JSONObject materialObject = {};

				Material& material = gameContext.renderer->GetMaterial(renderObjectCreateInfo.materialID);
				std::string materialName = material.name;
				materialObject.fields.push_back(JSONField("name", JSONValue(materialName)));

				Shader& shader = gameContext.renderer->GetShader(material.shaderID);
				std::string shaderName = shader.name;
				materialObject.fields.push_back(JSONField("shader", JSONValue(materialName)));
				
				// TODO: Write out const values when texture path is not given?
				static const glm::vec3 defaultConstAlbedo = glm::vec3(1.0f);
				if (((glm::vec3)material.constAlbedo) != defaultConstAlbedo)
				{
					std::string constAlbedoStr = Vec3ToString(material.constAlbedo);
					materialObject.fields.push_back(JSONField("const albedo", 
						JSONValue(constAlbedoStr)));
				}

				static const real defaultConstMetallic = 1.0f;
				if (material.constMetallic != defaultConstMetallic)
				{
					materialObject.fields.push_back(JSONField("const metallic", 
						JSONValue(material.constMetallic)));
				}

				static const real defaultConstRoughness = 1.0f;
				if (material.constRoughness != defaultConstRoughness)
				{
					materialObject.fields.push_back(JSONField("const roughness", 
						JSONValue(material.constRoughness)));
				}

				static const real defaultConstAO = 1.0f;
				if (material.constAO != defaultConstAO)
				{
					materialObject.fields.push_back(JSONField("const ao", 
						JSONValue(material.constAO)));
				}

				static const bool defaultEnableAlbedo = true;
				if (shader.needAlbedoSampler && material.enableAlbedoSampler != defaultEnableAlbedo)
				{
					std::string constAlbedoStr = Vec4ToString(material.constAlbedo);
					materialObject.fields.push_back(JSONField("enable albedo sampler", 
						JSONValue(material.enableAlbedoSampler)));
				}

				static const bool defaultEnableMetallicSampler = true;
				if (shader.needMetallicSampler && material.enableMetallicSampler != defaultEnableMetallicSampler)
				{
					materialObject.fields.push_back(JSONField("enable metallic sampler", 
						JSONValue(material.enableMetallicSampler)));
				}

				static const bool defaultEnableRoughness = true;
				if (shader.needRoughnessSampler && material.enableRoughnessSampler != defaultEnableRoughness)
				{
					materialObject.fields.push_back(JSONField("enable roughness sampler", 
						JSONValue(material.enableRoughnessSampler)));
				}

				static const bool defaultEnableAO = true;
				if (shader.needAOSampler && material.enableAOSampler != defaultEnableAO)
				{
					materialObject.fields.push_back(JSONField("enable ao sampler", 
						JSONValue(material.enableAOSampler)));
				}

				if (shader.needAlbedoSampler && !material.albedoTexturePath.empty())
				{
					std::string albedoTexturePath = material.albedoTexturePath.substr(RESOURCE_LOCATION.length());
					materialObject.fields.push_back(JSONField("albedo texture filepath", 
						JSONValue(albedoTexturePath)));
				}

				if (shader.needMetallicSampler && !material.metallicTexturePath.empty())
				{
					std::string metallicTexturePath = material.metallicTexturePath.substr(RESOURCE_LOCATION.length());
					materialObject.fields.push_back(JSONField("metallic texture filepath",
						JSONValue(metallicTexturePath)));
				}

				if (shader.needRoughnessSampler && !material.roughnessTexturePath.empty())
				{
					std::string roughnessTexturePath = material.roughnessTexturePath.substr(RESOURCE_LOCATION.length());
					materialObject.fields.push_back(JSONField("roughness texture filepath",
						JSONValue(roughnessTexturePath)));
				}

				if (shader.needAOSampler && !material.aoTexturePath.empty())
				{
					std::string aoTexturePath = material.aoTexturePath.substr(RESOURCE_LOCATION.length());
					materialObject.fields.push_back(JSONField("ao texture filepath",
						JSONValue(aoTexturePath)));
				}

				materialField.value = JSONValue(materialObject);
				object.fields.push_back(materialField);
			}
			else
			{
				Logger::LogWarning("BaseScene::SerializeObject failed to retrieve mesh object"
					" create info, serialized data will be incomplete. Invalid render ID: " + 
					std::to_string(meshRenderID));
			}

			meshField.value = JSONValue(meshValue);

			object.fields.push_back(meshField);
		} break;
		case SerializableType::SKYBOX:
		{
			// A skybox is a mesh prefab for now
			MeshPrefab* skyboxMesh = (MeshPrefab*)gameObject;

			glm::vec3 skyboxRotEuler = glm::eulerAngles(skyboxMesh->GetTransform()->GetGlobalRotation());
			object.fields.push_back(JSONField("rotation", JSONValue(Vec3ToString(skyboxRotEuler))));

			JSONField materialField = {};
			materialField.label = "material";

			Material skyboxMat = gameContext.renderer->GetMaterial(skyboxMesh->GetMaterialID());
			Shader skyboxShader = gameContext.renderer->GetShader(skyboxMat.shaderID);

			JSONObject materialObj = {};
			materialObj.fields.push_back(JSONField("name", JSONValue(skyboxMat.name)));
			materialObj.fields.push_back(JSONField("shader", JSONValue(skyboxShader.name)));
			materialObj.fields.push_back(JSONField("generate hdr cubemap sampler", 
				JSONValue(skyboxMat.generateHDRCubemapSampler)));
			materialObj.fields.push_back(JSONField("enable cubemap sampler", 
				JSONValue(skyboxMat.enableCubemapSampler)));
			materialObj.fields.push_back(JSONField("enable cubemap trilinear filtering",
				JSONValue(skyboxMat.enableCubemapTrilinearFiltering)));
			materialObj.fields.push_back(JSONField("generated cubemap size", 
				JSONValue(Vec2ToString(skyboxMat.cubemapSamplerSize))));
			materialObj.fields.push_back(JSONField("generate irradiance sampler", 
				JSONValue(skyboxMat.generateIrradianceSampler)));
			materialObj.fields.push_back(JSONField("generated irradiance cubemap size", 
				JSONValue(Vec2ToString(skyboxMat.irradianceSamplerSize))));
			materialObj.fields.push_back(JSONField("generate prefiltered map", 
				JSONValue(skyboxMat.generatePrefilteredMap)));
			materialObj.fields.push_back(JSONField("generated prefiltered map size", 
				JSONValue(Vec2ToString(skyboxMat.prefilteredMapSize))));

			materialField.value = JSONValue(materialObj);

			object.fields.push_back(materialField);
		} break;
		case SerializableType::REFLECTION_PROBE:
		{
			ReflectionProbe* reflectionProbe = (ReflectionProbe*)gameObject;

			glm::vec3 probePos = reflectionProbe->GetTransform()->GetGlobalPosition();
			object.fields.push_back(JSONField("position", JSONValue(Vec3ToString(probePos))));
			object.fields.push_back(JSONField("visible", JSONValue(reflectionProbe->IsSphereVisible(gameContext))));

			// TODO: Add probe-specific fields here like resolution and influence

		} break;
		case SerializableType::NONE:
		{
			// Assume this type is intentionally non-serializable or just a parent object
		} break;
		default:
		{
			Logger::LogError("Unhandled serializable type encountered in BaseScene::SerializeToFile " + childTypeStr);
		} break;
		}

		const std::vector<Transform*>& gameObjectChildren = gameObject->m_Transform.GetChildren();
		if (!gameObjectChildren.empty())
		{
			JSONField childrenField = {};
			childrenField.label = "children";

			std::vector<JSONObject> children;

			for (Transform* child : gameObjectChildren)
			{
				GameObject* childGameObject = child->GetGameObject();
				if (childGameObject->IsSerializable())
				{
					children.push_back(SerializeObject(childGameObject, gameContext));
				}
			}

			// It's possible that all children are non-serializable
			if (!children.empty())
			{
				childrenField.value = JSONValue(children);
				object.fields.push_back(childrenField);
			}
		}

		return object;
	}

	JSONObject BaseScene::SerializePointLight(PointLight& pointLight, const GameContext& gameContext)
	{
		JSONObject object;

		std::string posStr = Vec3ToString(pointLight.position);
		object.fields.push_back(JSONField("position", JSONValue(posStr)));

		std::string colorStr = Vec3ToString(pointLight.color);
		object.fields.push_back(JSONField("color", JSONValue(colorStr)));

		object.fields.push_back(JSONField("enabled", JSONValue(pointLight.enabled != 0)));
		object.fields.push_back(JSONField("brightness", JSONValue(pointLight.brightness)));
		object.fields.push_back(JSONField("name", JSONValue(pointLight.name)));

		return object;
	}

	JSONObject BaseScene::SerializeDirectionalLight(DirectionalLight& directionalLight, const GameContext& gameContext)
	{
		JSONObject object;

		std::string dirStr = Vec3ToString(directionalLight.direction);
		object.fields.push_back(JSONField("direction", JSONValue(dirStr)));

		std::string colorStr = Vec3ToString(directionalLight.color);
		object.fields.push_back(JSONField("color", JSONValue(colorStr)));

		object.fields.push_back(JSONField("enabled", JSONValue(directionalLight.enabled != 0)));
		object.fields.push_back(JSONField("brightness", JSONValue(directionalLight.brightness)));

		return object;
	}

	void BaseScene::AddChild(GameObject* gameObject)
	{
		if (!gameObject)
		{
			return;
		}

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			if (*iter == gameObject)
			{
				Logger::LogWarning("Attempting to add child to scene again");
				return;
			}
		}

		m_Children.push_back(gameObject);
	}

	void BaseScene::RemoveChild(GameObject* gameObject, bool deleteChild)
	{
		auto iter = m_Children.begin();
		while (iter != m_Children.end())
		{
			if (*iter == gameObject)
			{
				if (deleteChild)
				{
					SafeDelete(*iter);
				}

				iter = m_Children.erase(iter);
				return;
			}
			else
			{
				++iter;
			}
		}

		Logger::LogWarning("Attempting to remove non-existent child from scene");
	}

	void BaseScene::RemoveAllChildren(bool deleteChildren)
	{
		auto iter = m_Children.begin();
		while (iter != m_Children.end())
		{
			if (deleteChildren)
			{
				delete *iter;
			}

			iter = m_Children.erase(iter);
		}
	}

	std::vector<GameObject*>& BaseScene::GetRootObjects()
	{
		return m_Children;
	}

	std::string BaseScene::GetName() const
	{
		return m_Name;
	}

	PhysicsWorld* BaseScene::GetPhysicsWorld()
	{
		return m_PhysicsWorld;
	}

} // namespace flex
