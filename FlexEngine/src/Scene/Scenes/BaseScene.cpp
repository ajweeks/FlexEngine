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

		Logger::LogInfo("Loading scene from JSON");

		Logger::LogInfo("Parsed scene file:");
		Logger::LogInfo(sceneRootObject.Print(0));

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

		std::string sceneName = sceneRootObject.GetString("name");
		m_Name = sceneName;

		// This holds all the entities in the scene which do not have a parent
		std::vector<JSONObject> rootEntities = sceneRootObject.GetObjectArray("entities");
		for (const auto& rootEntity : rootEntities)
		{
			AddChild(gameContext, CreateEntityFromJSON(gameContext, rootEntity));
		}

		if (sceneRootObject.HasField("point lights"))
		{
			std::vector<JSONObject> pointLightsArray = sceneRootObject.GetObjectArray("point lights");

			for (JSONObject& pointLightObj : pointLightsArray)
			{
				PointLight pointLight = {};

				std::string posStr = pointLightObj.GetString("position");
				pointLight.position = glm::vec4(ParseVec3(posStr), 0);

				std::string colorStr = pointLightObj.GetString("color");
				if (!colorStr.empty())
				{
					pointLight.color = ParseVec4(colorStr);
				}

				if (pointLightObj.HasField("brightness"))
				{
					real brightness = pointLightObj.GetFloat("brightness");
					pointLight.brightness = brightness;
				}

				if (pointLightObj.HasField("enabled"))
				{
					pointLight.enabled = pointLightObj.GetBool("enabled") ? 1 : 0;
				}

				if (pointLightObj.HasField("name"))
				{
					pointLight.name = pointLightObj.GetString("name");
				}

				m_PointLights.push_back(pointLight);
				gameContext.renderer->InitializePointLight(pointLight);
			}
		}

		if (sceneRootObject.HasField("directional light"))
		{
			JSONObject directionalLightObj = sceneRootObject.GetObject("directional light");

			DirectionalLight dirLight = {};

			std::string dirStr = directionalLightObj.GetString("direction");
			dirLight.direction = glm::vec4(ParseVec3(dirStr), 0);

			std::string colorStr = directionalLightObj.GetString("color");
			if (!colorStr.empty())
			{
				dirLight.color = ParseVec4(colorStr);
			}

			if (directionalLightObj.HasField("brightness"))
			{
				real brightness = directionalLightObj.GetFloat("brightness");
				dirLight.brightness = brightness;
			}

			if (directionalLightObj.HasField("enabled"))
			{
				dirLight.enabled = directionalLightObj.GetBool("enabled") ? 1 : 0;
			}

			m_DirectionalLight = dirLight;
			gameContext.renderer->InitializeDirectionalLight(dirLight);
		}

		m_PhysicsWorld = new PhysicsWorld();
		m_PhysicsWorld->Initialize(gameContext);

		m_PhysicsWorld->GetWorld()->setGravity({ 0.0f, -9.81f, 0.0f });


		MaterialCreateInfo skyboxHDRMatInfo = {};
		skyboxHDRMatInfo.name = "HDR Skybox";
		skyboxHDRMatInfo.shaderName = "background";
		skyboxHDRMatInfo.generateHDRCubemapSampler = true;
		skyboxHDRMatInfo.enableCubemapSampler = true;
		skyboxHDRMatInfo.enableCubemapTrilinearFiltering = true;
		skyboxHDRMatInfo.generatedCubemapSize = { 512, 512 };
		skyboxHDRMatInfo.generateIrradianceSampler = true;
		skyboxHDRMatInfo.generatedIrradianceCubemapSize = { 32, 32 };
		skyboxHDRMatInfo.generatePrefilteredMap = true;
		skyboxHDRMatInfo.generatedPrefilteredCubemapSize = { 128, 128 };
		skyboxHDRMatInfo.environmentMapPath = RESOURCE_LOCATION + "textures/hdri/Milkyway/Milkyway_Light.hdr";
		MaterialID skyboxMatID = gameContext.renderer->InitializeMaterial(gameContext, &skyboxHDRMatInfo);
		gameContext.renderer->SetSkyboxMaterial(skyboxMatID);

		// Generated last so it can use generated skybox maps
		m_ReflectionProbe = new ReflectionProbe("default reflection probe", true);
		AddChild(gameContext, m_ReflectionProbe);
	}

	void BaseScene::PostInitialize(const GameContext& gameContext)
	{
		gameContext.renderer->SetReflectionProbeMaterial(m_ReflectionProbe->GetCaptureMaterialID());
	}

	void BaseScene::Destroy(const GameContext& gameContext)
	{
		for (auto child : m_Children)
		{
			if (child)
			{
				child->RootDestroy(gameContext);
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
		for (auto child : m_Children)
		{
			if (child)
			{
				child->RootUpdate(gameContext);
			}
		}
	}

	void BaseScene::RootInitialize(const GameContext& gameContext)
	{
		Initialize(gameContext);

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->Initialize(gameContext);
		}
	}

	void BaseScene::RootPostInitialize(const GameContext& gameContext)
	{
		PostInitialize(gameContext);

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->PostInitialize(gameContext);
		}
	}

	void BaseScene::RootUpdate(const GameContext& gameContext)
	{
		if (m_PhysicsWorld)
		{
			m_PhysicsWorld->Update(gameContext.deltaTime);
		}

		Update(gameContext);

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->RootUpdate(gameContext);
		}
	}

	void BaseScene::RootDestroy(const GameContext& gameContext)
	{
		Destroy(gameContext);

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->RootDestroy(gameContext);
			SafeDelete(*iter);
		}
	}

	GameObject* BaseScene::CreateEntityFromJSON(const GameContext& gameContext, const JSONObject& obj)
	{
		GameObject* result = nullptr;

		std::string entityTypeStr = obj.GetString("type");
		SerializableType entityType = StringToSerializableType(entityTypeStr);

		switch (entityType)
		{
		case SerializableType::MESH:
		{
			std::string objectName = obj.GetString("name");

			JSONObject meshObj = obj.GetObject("mesh");
			std::string meshFilePath = meshObj.GetString("file");
			std::string meshPrefabName = meshObj.GetString("prefab");
			bool flipNormalYZ = meshObj.GetBool("flipNormalYZ");
			bool flipZ = meshObj.GetBool("flipZ");
			bool flipU = meshObj.GetBool("flipU");
			bool flipV = meshObj.GetBool("flipV");

			bool visibleInSceneGraph = obj.HasField("visibleInSceneGraph") ? obj.GetBool("visibleInSceneGraph") : true;
			bool visible = obj.HasField("visibleInSceneGraph") ? obj.GetBool("visible") : true;

			JSONObject transformObj = obj.GetObject("transform");
			Transform transform = JSONParser::ParseTransform(transformObj);

			JSONObject material = obj.GetObject("material");
			std::string materialName = material.GetString("name");
			std::string shaderName = material.GetString("shader");

			if (shaderName.empty())
			{
				Logger::LogError("Shader name not set in material " + materialName);
				return nullptr;
			}

			ShaderID shaderID = InvalidShaderID;
			if (!gameContext.renderer->GetShaderID(shaderName, shaderID))
			{
				Logger::LogError("Shader ID not found in renderer!" + materialName);
				return nullptr;
			}

			Shader& shader = gameContext.renderer->GetShader(shaderID);
			VertexAttributes requiredVertexAttributes = shader.vertexAttributes;

			MaterialCreateInfo matCreateInfo = {};
			{
				matCreateInfo.name = materialName;
				matCreateInfo.shaderName = shaderName;

				struct FilePathMaterialParam
				{
					std::string* member;
					std::string name;
				};

				std::vector<FilePathMaterialParam> filePathParams =
				{
					{ &matCreateInfo.diffuseTexturePath, "diffuse texture filepath" },
					{ &matCreateInfo.normalTexturePath, "normal texture filepath" },
					{ &matCreateInfo.albedoTexturePath, "albedo texture filepath" },
					{ &matCreateInfo.metallicTexturePath, "metallic texture filepath" },
					{ &matCreateInfo.roughnessTexturePath, "roughness texture filepath" },
					{ &matCreateInfo.aoTexturePath, "ao texture filepath" },
					{ &matCreateInfo.hdrEquirectangularTexturePath, "hdr equirectangular texture filepath" },
					{ &matCreateInfo.environmentMapPath, "environment map filepath" },
				};

				for (u32 i = 0; i < filePathParams.size(); ++i)
				{
					if (material.HasField(filePathParams[i].name))
					{
						*filePathParams[i].member = RESOURCE_LOCATION + material.GetString(filePathParams[i].name);
					}
				}


				struct BoolMaterialParam
				{
					bool* member;
					std::string name;
				};

				std::vector<BoolMaterialParam> boolParams =
				{
					{ &matCreateInfo.generateDiffuseSampler, "generate diffuse sampler" },
					{ &matCreateInfo.enableDiffuseSampler, "enable diffuse sampler" },
					{ &matCreateInfo.generateNormalSampler, "generate normal sampler" },
					{ &matCreateInfo.enableNormalSampler, "enable normal sampler" },
					{ &matCreateInfo.generateAlbedoSampler, "generate albedo sampler" },
					{ &matCreateInfo.enableAlbedoSampler, "enable albedo sampler" },
					{ &matCreateInfo.generateMetallicSampler, "generate metallic sampler" },
					{ &matCreateInfo.enableMetallicSampler, "enable metallic sampler" },
					{ &matCreateInfo.generateRoughnessSampler, "generate roughness sampler" },
					{ &matCreateInfo.enableRoughnessSampler, "enable roughness sampler" },
					{ &matCreateInfo.generateAOSampler, "generate ao sampler" },
					{ &matCreateInfo.enableAOSampler, "enable ao sampler" },
					{ &matCreateInfo.generateHDREquirectangularSampler, "generate hdr equirectangular sampler" },
					{ &matCreateInfo.enableHDREquirectangularSampler, "enable hdr equirectangular sampler" },
					{ &matCreateInfo.generateHDRCubemapSampler, "generate hdr cubemap sampler" },
					{ &matCreateInfo.enableIrradianceSampler, "enable irradiance sampler" },
					{ &matCreateInfo.generateIrradianceSampler, "generate irradiance sampler" },
					{ &matCreateInfo.enableBRDFLUT, "enable brdf lut" },
					{ &matCreateInfo.renderToCubemap, "render to cubemap" },
					{ &matCreateInfo.enableCubemapSampler, "enable cubemap sampler" },
					{ &matCreateInfo.enableCubemapTrilinearFiltering, "enable cubemap trilinear filtering" },
					{ &matCreateInfo.generateCubemapSampler, "generate cubemap sampler" },
					{ &matCreateInfo.generateCubemapDepthBuffers, "generate cubemap depth buffers" },
					{ &matCreateInfo.generatePrefilteredMap, "generate prefiltered map" },
					{ &matCreateInfo.enablePrefilteredMap, "enable prefiltered map" },
					{ &matCreateInfo.generateReflectionProbeMaps, "generate reflection probe maps" },
				};

				for (u32 i = 0; i < boolParams.size(); ++i)
				{
					if (material.HasField(boolParams[i].name))
					{
						*boolParams[i].member = material.GetBool(boolParams[i].name);
					}
				}

				if (material.HasField("color multiplier"))
				{
					std::string colorStr = material.GetString("color multiplier");
					matCreateInfo.colorMultiplier = ParseVec4(colorStr);
				}

				if (material.HasField("generated irradiance cubemap size"))
				{
					std::string irradianceCubemapSizeStr = material.GetString("generated irradiance cubemap size");
					matCreateInfo.generatedIrradianceCubemapSize = ParseVec2(irradianceCubemapSizeStr);
				}

				//std::vector<std::pair<std::string, void*>> frameBuffers; // Pairs of frame buffer names (as seen in shader) and IDs
				//matCreateInfo.irradianceSamplerMatID = InvalidMaterialID; // The id of the material who has an irradiance sampler object (generateIrradianceSampler must be false)
				//matCreateInfo.prefilterMapSamplerMatID = InvalidMaterialID;

				//matCreateInfo.cubeMapFilePaths; // RT, LF, UP, DN, BK, FT

				if (material.HasField("generated cubemap size"))
				{
					std::string generatedCubemapSizeStr = material.GetString("generated cubemap size");
					matCreateInfo.generatedCubemapSize = ParseVec2(generatedCubemapSizeStr);
				}

				if (material.HasField("generated prefiltered cubemap size"))
				{
					std::string generatedPrefilteredCubemapSizeStr = material.GetString("generated prefiltered cubemap size");
					matCreateInfo.generatedPrefilteredCubemapSize = ParseVec2(generatedPrefilteredCubemapSizeStr);
				}

				if (material.HasField("const albedo"))
				{
					std::string albedoStr = material.GetString("const albedo");
					matCreateInfo.constAlbedo = ParseVec3(albedoStr);
				}

				if (material.HasField("const metallic"))
				{
					matCreateInfo.constMetallic = material.GetFloat("const metallic");
				}

				if (material.HasField("cons roughness"))
				{
					matCreateInfo.constRoughness = material.GetFloat("const roughness");
				}

				if (material.HasField("const ao"))
				{
					matCreateInfo.constAO = material.GetFloat("const ao");
				}
			}
			MaterialID matID = gameContext.renderer->InitializeMaterial(gameContext, &matCreateInfo);

			if (!meshFilePath.empty())
			{
				MeshPrefab* mesh = new MeshPrefab(matID, objectName);
				mesh->SetRequiredAttributes(requiredVertexAttributes);

				RenderObjectCreateInfo createInfo = {};
				createInfo.visibleInSceneExplorer = visibleInSceneGraph;

				mesh->LoadFromFile(gameContext, RESOURCE_LOCATION + meshFilePath,
					flipNormalYZ,
					flipZ,
					flipU,
					flipV,
					&createInfo);

				mesh->GetTransform() = transform;

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

				mesh->GetTransform() = transform;

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
			// TODO: UNIMPLEMENTED
			//gameContext.renderer->SetSkyboxMaterial();
		} break;
		case SerializableType::REFLECTION_PROBE:
		{
			// TODO: UNIMPLEMENTED
			//gameContext.renderer->SetReflectionProbeMaterial();
		} break;
		case SerializableType::NONE:
		default:
			Logger::LogError("Unhandled entity type encountered when parsing scene file: " + entityTypeStr);
		}

		if (result && obj.HasField("children"))
		{
			std::vector<JSONObject> children = obj.GetObjectArray("children");
			for (const auto& child : children)
			{
				result->AddChild(CreateEntityFromJSON(gameContext, child));
			}
		}

		return result;
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
			entitiesArray.push_back(SerializeObject(child, gameContext));
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
				meshValue.fields.push_back(JSONField("file", JSONValue(mesh->GetFilepath())));
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
					materialObject.fields.push_back(JSONField("albedo texture filepath", 
						JSONValue(material.albedoTexturePath)));
				}

				if (shader.needMetallicSampler && !material.metallicTexturePath.empty())
				{
					materialObject.fields.push_back(JSONField("metallic texture filepath",
						JSONValue(material.metallicTexturePath)));
				}

				if (shader.needRoughnessSampler && !material.roughnessTexturePath.empty())
				{
					materialObject.fields.push_back(JSONField("roughness texture filepath", 
						JSONValue(material.roughnessTexturePath)));
				}

				if (shader.needAOSampler && !material.aoTexturePath.empty())
				{
					materialObject.fields.push_back(JSONField("ao texture filepath", 
						JSONValue(material.aoTexturePath)));
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
			// TODO: UNIMPLEMENTED
		} break;
		case SerializableType::REFLECTION_PROBE:
		{
			// TODO: UNIMPLEMENTED
		} break;
		case SerializableType::NONE:
		{
			// Assume this type is intentionally non-serializable
		} break;
		default:
		{
			Logger::LogError("Unhandled serializable type encountered in BaseScene::SerializeToFile " + childTypeStr);
		} break;
		}

		if (!gameObject->m_Children.empty())
		{
			JSONField childrenField = {};
			childrenField.label = "children";

			std::vector<JSONObject> children;

			for (GameObject* child : gameObject->m_Children)
			{
				children.push_back(SerializeObject(child, gameContext));
			}

			childrenField.value = JSONValue(children);
			object.fields.push_back(childrenField);
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

	void BaseScene::AddChild(const GameContext& gameContext, GameObject* gameObject)
	{
		UNREFERENCED_PARAMETER(gameContext);

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

	std::string BaseScene::GetName() const
	{
		return m_Name;
	}

	PhysicsWorld* BaseScene::GetPhysicsWorld()
	{
		return m_PhysicsWorld;
	}

} // namespace flex
