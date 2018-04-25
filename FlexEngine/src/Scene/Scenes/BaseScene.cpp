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
					{ &matCreateInfo.diffuseTexturePath, "diffuseTexturePath" },
					{ &matCreateInfo.normalTexturePath, "normalTexturePath" },
					{ &matCreateInfo.albedoTexturePath, "albedoTexturePath" },
					{ &matCreateInfo.metallicTexturePath, "metallicTexturePath" },
					{ &matCreateInfo.roughnessTexturePath, "roughnessTexturePath" },
					{ &matCreateInfo.aoTexturePath, "aoTexturePath" },
					{ &matCreateInfo.hdrEquirectangularTexturePath, "hdrEquirectangularTexturePath" },
					{ &matCreateInfo.environmentMapPath, "environmentMapPath" },
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
					{ &matCreateInfo.generateDiffuseSampler, "generateDiffuseSampler" },
					{ &matCreateInfo.enableDiffuseSampler, "enableDiffuseSampler" },
					{ &matCreateInfo.generateNormalSampler, "generateNormalSampler" },
					{ &matCreateInfo.enableNormalSampler, "enableNormalSampler" },
					{ &matCreateInfo.generateAlbedoSampler, "generateAlbedoSampler" },
					{ &matCreateInfo.enableAlbedoSampler, "enableAlbedoSampler" },
					{ &matCreateInfo.generateMetallicSampler, "generateMetallicSampler" },
					{ &matCreateInfo.enableMetallicSampler, "enableMetallicSampler" },
					{ &matCreateInfo.generateRoughnessSampler, "generateRoughnessSampler" },
					{ &matCreateInfo.enableRoughnessSampler, "enableRoughnessSampler" },
					{ &matCreateInfo.generateAOSampler, "generateAOSampler" },
					{ &matCreateInfo.enableAOSampler, "enableAOSampler" },
					{ &matCreateInfo.generateHDREquirectangularSampler, "generateHDREquirectangularSampler" },
					{ &matCreateInfo.enableHDREquirectangularSampler, "enableHDREquirectangularSampler" },
					{ &matCreateInfo.generateHDRCubemapSampler, "generateHDRCubemapSampler" },
					{ &matCreateInfo.enableIrradianceSampler, "enableIrradianceSampler" },
					{ &matCreateInfo.generateIrradianceSampler, "generateIrradianceSampler" },
					{ &matCreateInfo.enableBRDFLUT, "enableBRDFLUT" },
					{ &matCreateInfo.renderToCubemap, "renderToCubemap" },
					{ &matCreateInfo.enableCubemapSampler, "enableCubemapSampler" },
					{ &matCreateInfo.enableCubemapTrilinearFiltering, "enableCubemapTrilinearFiltering" },
					{ &matCreateInfo.generateCubemapSampler, "generateCubemapSampler" },
					{ &matCreateInfo.generateCubemapDepthBuffers, "generateCubemapDepthBuffers" },
					{ &matCreateInfo.generatePrefilteredMap, "generatePrefilteredMap" },
					{ &matCreateInfo.enablePrefilteredMap, "enablePrefilteredMap" },
					{ &matCreateInfo.generateReflectionProbeMaps, "generateReflectionProbeMaps" },
				};

				for (u32 i = 0; i < boolParams.size(); ++i)
				{
					if (material.HasField(boolParams[i].name))
					{
						*boolParams[i].member = material.GetBool(boolParams[i].name);
					}
				}

				if (material.HasField("colorMultiplier"))
				{
					std::string colorStr = material.GetString("colorMultiplier");
					matCreateInfo.colorMultiplier = ParseVec4(colorStr);
				}

				if (material.HasField("generatedIrradianceCubemapSize"))
				{
					std::string irradianceCubemapSizeStr = material.GetString("generatedIrradianceCubemapSize");
					matCreateInfo.generatedIrradianceCubemapSize = ParseVec2(irradianceCubemapSizeStr);
				}

				//std::vector<std::pair<std::string, void*>> frameBuffers; // Pairs of frame buffer names (as seen in shader) and IDs
				//matCreateInfo.irradianceSamplerMatID = InvalidMaterialID; // The id of the material who has an irradiance sampler object (generateIrradianceSampler must be false)
				//matCreateInfo.prefilterMapSamplerMatID = InvalidMaterialID;

				//matCreateInfo.cubeMapFilePaths; // RT, LF, UP, DN, BK, FT

				if (material.HasField("generatedCubemapSize"))
				{
					std::string generatedCubemapSizeStr = material.GetString("generatedCubemapSize");
					matCreateInfo.generatedCubemapSize = ParseVec2(generatedCubemapSizeStr);
				}

				if (material.HasField("generatedPrefilteredCubemapSize"))
				{
					std::string generatedPrefilteredCubemapSizeStr = material.GetString("generatedPrefilteredCubemapSize");
					matCreateInfo.generatedPrefilteredCubemapSize = ParseVec2(generatedPrefilteredCubemapSizeStr);
				}

				if (material.HasField("constAlbedo"))
				{
					std::string albedoStr = material.GetString("constAlbedo");
					matCreateInfo.constAlbedo = ParseVec3(albedoStr);
				}

				if (material.HasField("constMetallic"))
				{
					matCreateInfo.constMetallic = material.GetFloat("constMetallic");
				}

				if (material.HasField("constRoughness"))
				{
					matCreateInfo.constRoughness = material.GetFloat("constRoughness");
				}

				if (material.HasField("constAO"))
				{
					matCreateInfo.constAO = material.GetFloat("constAO");
				}
			}
			MaterialID matID = gameContext.renderer->InitializeMaterial(gameContext, &matCreateInfo);

			if (!meshFilePath.empty())
			{
				MeshPrefab* mesh = new MeshPrefab(matID, objectName);
				mesh->SetRequiredAttributes(requiredVertexAttributes);

				RenderObjectCreateInfo createInfo = {};
				createInfo.visibleInSceneExplorer = visibleInSceneGraph;

				if (meshObj.HasField("cullFace"))
				{
					std::string cullFaceStr = meshObj.GetString("cullFace");
					CullFace cullFace = StringToCullFace(cullFaceStr);
					createInfo.cullFace = cullFace;
				}
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

				if (meshObj.HasField("cullFace"))
				{
					std::string cullFaceStr = meshObj.GetString("cullFace");
					CullFace cullFace = StringToCullFace(cullFaceStr);
					createInfo.cullFace = cullFace;
				}
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
		case SerializableType::POINT_LIGHT:
		{
			PointLight pointLight = {};

			std::string posStr = obj.GetString("position");
			pointLight.position = glm::vec4(ParseVec3(posStr), 0);

			std::string colorStr = obj.GetString("color");
			if (!colorStr.empty())
			{
				pointLight.color = ParseVec4(colorStr);
			}

			if (obj.HasField("brightness"))
			{
				real brightness = obj.GetFloat("brightness");
				pointLight.brightness = brightness;
			}

			if (obj.HasField("enabled"))
			{
				pointLight.enabled = obj.GetBool("enabled") ? 1 : 0;
			}

			gameContext.renderer->InitializePointLight(pointLight);
		} break;
		case SerializableType::DIRECTIONAL_LIGHT:
		{
			DirectionalLight dirLight = {};

			std::string dirStr = obj.GetString("direction");
			dirLight.direction = glm::vec4(ParseVec3(dirStr), 0);

			std::string colorStr = obj.GetString("color");
			if (!colorStr.empty())
			{
				dirLight.color = ParseVec4(colorStr);
			}

			if (obj.HasField("brightness"))
			{
				real brightness = obj.GetFloat("brightness");
				//pointLight.brightness = ParseFloat(brightnessStr);
			}

			if (obj.HasField("enabled"))
			{
				dirLight.enabled = obj.GetBool("enabled") ? 1 : 0;
			}

			gameContext.renderer->InitializeDirectionalLight(dirLight);
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

		JSONField versionField = {};
		versionField.label = "version";
		versionField.value = JSONValue(fileVersion);
		rootSceneObject.fields.push_back(versionField);

		JSONField nameField = {};
		nameField.label = "name";
		nameField.value = JSONValue(m_Name);
		rootSceneObject.fields.push_back(nameField);

		JSONField childrenField = {};
		childrenField.label = "entities";

		std::vector<JSONObject> childrenArray;
		for (auto child : m_Children)
		{
			childrenArray.push_back(SerializeObject(child, gameContext));
		}
		childrenField.value = JSONValue(childrenArray);

		rootSceneObject.fields.push_back(childrenField);

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

		JSONField childNameField = {};
		childNameField.label = "name";
		childNameField.value = JSONValue(childName);
		object.fields.push_back(childNameField);

		SerializableType childType = gameObject->m_SerializableType;
		std::string childTypeStr = SerializableTypeToString(childType);

		JSONField childTypeField = {};
		childNameField.label = "type";
		childNameField.value = JSONValue(childTypeStr);
		object.fields.push_back(childNameField);

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
				JSONField filePathField = {};
				filePathField.label = "file";
				filePathField.value = JSONValue(mesh->GetFilepath());

				meshValue.fields.push_back(filePathField);
			}
			else if (meshType == MeshPrefab::MeshType::PREFAB)
			{
				JSONField prefabField = {};
				prefabField.label = "prefab";
				prefabField.value = JSONValue(MeshPrefab::PrefabShapeToString(mesh->GetShape()));

				meshValue.fields.push_back(prefabField);
			}
			else
			{
				Logger::LogError("Unhandled mesh prefab type when attempting to serialize scene!");
			}

			JSONField cullFaceField = {};
			cullFaceField.label = "cull face";
			RenderID meshRenderID = mesh->GetRenderID();
			CullFace renderObjectCullFace = gameContext.renderer->GetRenderObjectCullFace(meshRenderID);
			std::string cullFaceStr = CullFaceToString(renderObjectCullFace);
			cullFaceField.value = JSONValue(cullFaceStr);

			meshValue.fields.push_back(cullFaceField);

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
		case SerializableType::POINT_LIGHT:
		{
		} break;
		case SerializableType::DIRECTIONAL_LIGHT:
		{
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

		// TODO: Also serialize children
		//child->m_Children;

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
