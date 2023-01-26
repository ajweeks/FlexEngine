#include "stdafx.hpp"

#include "ResourceManager.hpp"

IGNORE_WARNINGS_PUSH
#if COMPILE_IMGUI
#include "imgui_internal.h" // for columns API
#endif

#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/fttypes.h>
#include <freetype/fterrors.h>
IGNORE_WARNINGS_POP

#include "Graphics/RendererTypes.hpp"
#include "Graphics/Renderer.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "Editor.hpp"
#include "FlexEngine.hpp"
#include "InputManager.hpp"
#include "JSONParser.hpp"
#include "Platform/Platform.hpp"
#include "Particles.hpp"
#include "Player.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/LoadedMesh.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/BaseScene.hpp"
#include "StringBuilder.hpp"
#include "Window/Monitor.hpp"
#include "Window/Window.hpp"

namespace flex
{
	// TODO: Support DDS
	const char* ResourceManager::s_SupportedTextureFormats[] = { "jpg", "jpeg", "png", "tga", "bmp", "gif", "hdr", "pic" };

	ResourceManager::ResourceManager() :
		m_FontsFilePathAbs(RelativePathToAbsolute(FONT_DEFINITION_LOCATION))
	{
	}

	ResourceManager::~ResourceManager()
	{
	}

	void ResourceManager::Initialize()
	{
		PROFILE_AUTO("ResourceManager Initialize");

		m_AudioDirectoryWatcher = new DirectoryWatcher(SFX_DIRECTORY, false);
		m_PrefabDirectoryWatcher = new DirectoryWatcher(PREFAB_DIRECTORY, false);
		m_MeshDirectoryWatcher = new DirectoryWatcher(MESH_DIRECTORY, false);
		m_TextureDirectoryWatcher = new DirectoryWatcher(TEXTURE_DIRECTORY, true);

		DiscoverTextures();
		DiscoverAudioFiles();
		ParseDebugOverlayNamesFile();
		ParseGameObjectTypesFile();
		DiscoverParticleParameterTypes();
		DiscoverParticleSystemTemplates();

		m_NonDefaultStackSizes[SID("battery")] = 1;
	}

	void ResourceManager::PostInitialize()
	{
		tofuIconID = GetOrLoadTexture(ICON_DIRECTORY "tofu-icon-256.png");
	}

	void ResourceManager::Update()
	{
		PROFILE_AUTO("ResourceManager Update");

		if (m_AudioRefreshFrameCountdown != -1)
		{
			--m_AudioRefreshFrameCountdown;
			if (m_AudioRefreshFrameCountdown <= -1)
			{
				m_AudioRefreshFrameCountdown = -1;
				DiscoverAudioFiles();
			}
		}

		if (m_AudioDirectoryWatcher->Update())
		{
			// Delay discovery to allow temporary files to be resolved.
			// Audacity for example, when replacing an existing file "file.wav", will
			// first write to "file0.wav", then delete "file.wav", then rename "file0.wav"
			// to "file.wav". This delay prevents us from trying to load the temporary file.
			m_AudioRefreshFrameCountdown = 1;
		}

		if (m_PrefabDirectoryWatcher->Update())
		{
			DiscoverPrefabs();
		}

		if (m_MeshDirectoryWatcher->Update())
		{
			DiscoverMeshes();
		}

		if (m_TextureDirectoryWatcher->Update())
		{
			DiscoverTextures();
		}
	}

	void ResourceManager::Destroy()
	{
		delete m_AudioDirectoryWatcher;
		m_AudioDirectoryWatcher = nullptr;

		delete m_PrefabDirectoryWatcher;
		m_PrefabDirectoryWatcher = nullptr;

		delete m_MeshDirectoryWatcher;
		m_MeshDirectoryWatcher = nullptr;

		delete m_TextureDirectoryWatcher;
		m_TextureDirectoryWatcher = nullptr;

		for (BitmapFont* font : fontsScreenSpace)
		{
			delete font;
		}
		fontsScreenSpace.clear();

		for (BitmapFont* font : fontsWorldSpace)
		{
			delete font;
		}
		fontsWorldSpace.clear();

		for (Texture* loadedTexture : loadedTextures)
		{
			delete loadedTexture;
		}
		loadedTextures.clear();

		for (PrefabTemplateInfo& prefabTemplateInfo : prefabTemplates)
		{
			prefabTemplateInfo.templateObject->Destroy();
			delete prefabTemplateInfo.templateObject;
		}
		prefabTemplates.clear();
	}

	void ResourceManager::DestroyAllLoadedMeshes()
	{
		for (auto& loadedMeshPair : loadedMeshes)
		{
			cgltf_free(loadedMeshPair.second->data);
			delete loadedMeshPair.second;
		}
		loadedMeshes.clear();
	}

	void ResourceManager::PreSceneChange()
	{
		for (PrefabTemplateInfo& prefabTemplateInfo : prefabTemplates)
		{
			prefabTemplateInfo.templateObject->Destroy();
			delete prefabTemplateInfo.templateObject;
		}
		prefabTemplates.clear();
	}

	void ResourceManager::OnSceneChanged()
	{
		DiscoverMeshes();
	}

	bool ResourceManager::FindPreLoadedMesh(const std::string& relativeFilePath, LoadedMesh** loadedMesh)
	{
		auto iter = loadedMeshes.find(relativeFilePath);
		if (iter == loadedMeshes.end())
		{
			return false;
		}
		else
		{
			*loadedMesh = iter->second;
			return true;
		}
	}

	LoadedMesh* ResourceManager::FindOrLoadMesh(const std::string& relativeFilePath, bool bForceReload /* = false */)
	{
		LoadedMesh* result = nullptr;
		if (bForceReload || !FindPreLoadedMesh(relativeFilePath, &result))
		{
			// Mesh hasn't been loaded before, load it now
			result = Mesh::LoadMesh(relativeFilePath);
		}
		return result;
	}

	bool ResourceManager::MeshFileNameConforms(const std::string& fileName)
	{
		return EndsWith(fileName, "glb") || EndsWith(fileName, "gltf");
	}

	void ResourceManager::ParseMeshJSON(i32 sceneFileVersion, GameObject* parent, const JSONObject& meshObj, const std::vector<MaterialID>& materialIDs, bool bCreateRenderObject)
	{
		std::string meshFilePath;
		if (meshObj.TryGetString("mesh", meshFilePath))
		{
			if (sceneFileVersion >= 4)
			{
				meshFilePath = MESH_DIRECTORY + meshFilePath;
			}
			else
			{
				// "file" field stored mesh name without extension in versions <= 3, try to guess it
				bool bMatched = false;
				for (const std::string& path : discoveredMeshes)
				{
					std::string discoveredMeshName = StripFileType(path);
					if (discoveredMeshName.compare(meshFilePath) == 0)
					{
						meshFilePath = MESH_DIRECTORY + path;
						bMatched = true;
						break;
					}
				}

				if (!bMatched)
				{
					std::string glbFilePath = MESH_DIRECTORY + meshFilePath + ".glb";
					std::string gltfFilePath = MESH_DIRECTORY + meshFilePath + ".gltf";
					if (FileExists(glbFilePath))
					{
						meshFilePath = glbFilePath;
					}
					else if (FileExists(glbFilePath))
					{
						meshFilePath = gltfFilePath;
					}
				}

				if (!FileExists(meshFilePath))
				{
					PrintError("Failed to upgrade scene file, unable to find path of mesh with name %s\n", meshFilePath.c_str());
					return;
				}
			}

			Mesh::ImportFromFile(meshFilePath, parent, materialIDs, bCreateRenderObject);
			return;
		}

		std::string prefabName;
		if (meshObj.TryGetString("prefab", prefabName))
		{
			Mesh::ImportFromPrefab(prefabName, parent, materialIDs, bCreateRenderObject);
			return;
		}
	}

	JSONField ResourceManager::SerializeMesh(Mesh* mesh)
	{
		JSONField meshObject = {};

		switch (mesh->GetType())
		{
		case Mesh::Type::FILE:
		{
			std::string prefixStr = MESH_DIRECTORY;
			std::string meshFilepath = mesh->GetRelativeFilePath().substr(prefixStr.length());
			meshObject = JSONField("mesh", JSONValue(meshFilepath));
		} break;
		case Mesh::Type::PREFAB:
		{
			std::string prefabShapeStr = MeshComponent::PrefabShapeToString(mesh->GetSubMesh(0)->GetShape());
			meshObject = JSONField("prefab", JSONValue(prefabShapeStr));
		} break;
		default:
		{
			PrintError("Unhandled mesh prefab type when attempting to serialize scene!\n");
		} break;
		}

		return meshObject;
	}

	void ResourceManager::DiscoverMeshes()
	{
		std::vector<std::string> filePaths;
		if (Platform::FindFilesInDirectory(MESH_DIRECTORY, filePaths, "*"))
		{
			i32 modifiedFileCount = 0;
			for (const std::string& filePath : filePaths)
			{
				std::string fileName = StripLeadingDirectories(filePath);
				if (MeshFileNameConforms(filePath))
				{
					if (Contains(discoveredMeshes, fileName))
					{
						// Existing mesh

						if (Contains(m_MeshDirectoryWatcher->modifiedFilePaths, filePath))
						{
							// File has been modified, update all usages
							g_SceneManager->CurrentScene()->OnExternalMeshChange(filePath);
							++modifiedFileCount;
						}
					}
					else
					{
						// Newly discovered mesh

						// TODO: Support storing meshes in child directories
						discoveredMeshes.push_back(fileName);
					}
				}
			}

			if (modifiedFileCount > 0)
			{
				Print("Found and re-imported %d modified mesh%s\n", modifiedFileCount, modifiedFileCount > 1 ? "es" : "");
			}
		}
	}

	bool ParsePrefabTemplate(const std::string& filePath, std::map<PrefabID, std::string>& prefabNames, std::vector<ResourceManager::PrefabTemplateInfo>& templateInfos)
	{
		JSONObject prefabObject;
		if (JSONParser::ParseFromFile(filePath, prefabObject))
		{
			const std::string fileName = StripLeadingDirectories(filePath);

			i32 prefabVersion = prefabObject.GetInt("version");

			i32 sceneVersion = 6;
			// Added in prefab v3, older files use scene version 6
			prefabObject.TryGetInt("scene version", sceneVersion);

			PrefabID prefabID;
			if (prefabVersion >= 2)
			{
				// Added in prefab v2
				std::string idStr = prefabObject.GetString("prefab id");
				prefabID = GUID::FromString(idStr);
			}
			else
			{
				prefabID = Platform::GenerateGUID();
			}

			std::string prefabName = StripFileType(fileName);
			prefabNames.emplace(prefabID, prefabName);


			JSONObject prefabRootObject = prefabObject.GetObject("root");

			using CopyFlags = GameObject::CopyFlags;

			CopyFlags copyFlags = (CopyFlags)(
				(u32)CopyFlags::ALL &
				~(u32)CopyFlags::CREATE_RENDER_OBJECT &
				~(u32)CopyFlags::ADD_TO_SCENE);
			PrefabIDPair prefabIDPair;
			prefabIDPair.m_PrefabID = prefabID;
			prefabIDPair.m_SubGameObjectID = prefabRootObject.GetGameObjectID("id");
			GameObject* prefabTemplate = GameObject::CreateObjectFromJSON(prefabRootObject, nullptr, sceneVersion, prefabIDPair, true, copyFlags);

			CHECK(prefabTemplate->IsPrefabTemplate());

			templateInfos.emplace_back(prefabTemplate, prefabID, fileName);
		}
		else
		{
			PrintError("Failed to parse prefab file: %s, error: %s\n", filePath.c_str(), JSONParser::GetErrorString());
			return false;
		}

		return true;
	}

	void ResourceManager::DiscoverPrefabs()
	{
		for (PrefabTemplateInfo& prefabTemplateInfo : prefabTemplates)
		{
			prefabTemplateInfo.templateObject->Destroy();
			delete prefabTemplateInfo.templateObject;
		}
		prefabTemplates.clear();
		discoveredPrefabs.clear();

		std::vector<std::string> foundFiles;
		if (Platform::FindFilesInDirectory(PREFAB_DIRECTORY, foundFiles, ".json"))
		{
			for (const std::string& foundFilePath : foundFiles)
			{
				if (g_bEnableLogging_Loading)
				{
					const std::string fileName = StripLeadingDirectories(foundFilePath);
					Print("Parsing prefab: %s\n", fileName.c_str());
				}

				ParsePrefabTemplate(foundFilePath, discoveredPrefabs, prefabTemplates);
			}
		}
		else
		{
			PrintError("Failed to find prefab files in \"" PREFAB_DIRECTORY "\"!\n");
			return;
		}

		if (g_bEnableLogging_Loading)
		{
			Print("Parsed %u prefabs\n", (u32)prefabTemplates.size());
		}
	}

	void ResourceManager::DiscoverAudioFiles()
	{
		std::map<StringID, AudioFileMetaData> newDiscoveredAudioFiles;
		i32 modifiedCount = 0;
		i32 addedCount = 0;
		i32 removedCount = 0;

		StringBuilder errorStringBuilder;
		std::vector<std::string> foundFiles;
		if (Platform::FindFilesInDirectory(SFX_DIRECTORY, foundFiles, ".wav"))
		{
			for (std::string& foundFilePath : foundFiles)
			{
				std::string relativeFilePath = foundFilePath.substr(strlen(SFX_DIRECTORY));
				StringID stringID = SID(relativeFilePath.c_str());
				AudioFileMetaData metaData(relativeFilePath);
				auto iter = discoveredAudioFiles.find(stringID);
				if (iter != discoveredAudioFiles.end())
				{
					// Existing file
					auto modifiedIter = std::find(m_AudioDirectoryWatcher->modifiedFilePaths.begin(), m_AudioDirectoryWatcher->modifiedFilePaths.end(), foundFilePath);
					if (modifiedIter != m_AudioDirectoryWatcher->modifiedFilePaths.end())
					{
						++modifiedCount;
						if (iter->second.sourceID != InvalidAudioSourceID)
						{
							// Reload existing audio file if already loaded, it's out of date
							errorStringBuilder.Clear();
							if (AudioManager::ReplaceAudioSource(foundFilePath, iter->second.sourceID, &errorStringBuilder) != InvalidAudioSourceID)
							{
								metaData.bInvalid = false;
							}
							else
							{
								// Failed to replace
								metaData.bInvalid = true;
								newDiscoveredAudioFiles.emplace(stringID, metaData);
								continue;
							}
						}
					}

					metaData.sourceID = discoveredAudioFiles[stringID].sourceID;
				}
				else
				{
					// Newly discovered file
					++addedCount;
				}
				newDiscoveredAudioFiles.emplace(stringID, metaData);
			}
		}

		removedCount = (i32)discoveredAudioFiles.size() - (i32)(newDiscoveredAudioFiles.size() - addedCount);

		discoveredAudioFiles.clear();
		discoveredAudioFiles = newDiscoveredAudioFiles;

		if (g_bEnableLogging_Loading)
		{
			if (modifiedCount != 0)
			{
				Print("%d audio file%s modified\n", modifiedCount, modifiedCount > 1 ? "s" : "");
			}

			if (addedCount != 0)
			{
				Print("%d audio file%s added\n", addedCount, addedCount > 1 ? "s" : "");
			}

			if (removedCount != 0)
			{
				Print("%d audio file%s removed\n", removedCount, removedCount > 1 ? "s" : "");
			}
		}
	}

	void ResourceManager::DiscoverTextures()
	{
		{
			discoveredTextures.clear();

			// Zeroth index represents no texture
			discoveredTextures.emplace_back("");
			std::vector<std::string> foundFiles;
			if (Platform::FindFilesInDirectory(TEXTURE_DIRECTORY, foundFiles, s_SupportedTextureFormats, ARRAY_LENGTH(s_SupportedTextureFormats)))
			{
				for (const std::string& foundFilePath : foundFiles)
				{
					discoveredTextures.push_back(foundFilePath);
				}

				if (g_bEnableLogging_Loading)
				{
					Print("Discovered %u textures\n", (u32)(discoveredTextures.size() - 1));
				}
			}
			else
			{
				PrintError("Failed to find texture files in \"" TEXTURE_DIRECTORY "\"!\n");
				return;
			}
		}

		{
			discoveredIcons.clear();

			std::vector<std::string> foundIconFiles;
			if (Platform::FindFilesInDirectory(ICON_DIRECTORY, foundIconFiles, s_SupportedTextureFormats, ARRAY_LENGTH(s_SupportedTextureFormats), true))
			{
				for (const std::string& foundFilePath : foundIconFiles)
				{
					std::string trimmedFileName = RelativePathToAbsolute(foundFilePath);
					trimmedFileName = StripLeadingDirectories(StripFileType(foundFilePath));
					i32 resolution = 0;
					size_t iconEnd = trimmedFileName.find_last_of("-icon-");
					if (iconEnd != std::string::npos)
					{
						resolution = ParseInt(trimmedFileName.substr(iconEnd + 1));
						trimmedFileName = trimmedFileName.substr(0, iconEnd - 6 + 1);
					}
					trimmedFileName = Replace(trimmedFileName, '-', ' ');
					StringID prefabNameSID = Hash(trimmedFileName.c_str());
					IconMetaData iconMetaData = {};
					iconMetaData.relativeFilePath = foundFilePath;
					iconMetaData.resolution = resolution;
					discoveredIcons.emplace_back(prefabNameSID, iconMetaData);
				}
			}
		}

		// Check for modified files to reload
		{
			std::vector<std::string> foundFiles;
			if (Platform::FindFilesInDirectory(TEXTURE_DIRECTORY, foundFiles, s_SupportedTextureFormats, ARRAY_LENGTH(s_SupportedTextureFormats), true))
			{
				i32 modifiedTextureCount = 0;
				for (const std::string& foundFilePath : foundFiles)
				{
					if (Contains(m_TextureDirectoryWatcher->modifiedFilePaths, foundFilePath))
					{
						for (Texture* tex : loadedTextures)
						{
							// File has been modified externally, reload its contents
							if (tex->relativeFilePath == foundFilePath)
							{
								tex->Reload();
								++modifiedTextureCount;
							}
						}
					}
				}

				if (modifiedTextureCount > 0)
				{
					Print("Found and re-imported %d modified texture%s\n", modifiedTextureCount, modifiedTextureCount > 1 ? "s" : "");
				}
			}
		}
	}

	void ResourceManager::DiscoverParticleSystemTemplates()
	{
		m_ParticleTemplates.clear();

		std::string absoluteDirectory = RelativePathToAbsolute(PARTICLE_SYSTEMS_DIRECTORY);
		if (Platform::DirectoryExists(absoluteDirectory))
		{
			std::vector<std::string> filePaths;
			if (Platform::FindFilesInDirectory(absoluteDirectory, filePaths, ".json"))
			{
				for (const std::string& filePath : filePaths)
				{
					std::string fileName = StripLeadingDirectories(StripFileType(filePath));

					ParticleSystemTemplate particleTemplate = {};
					particleTemplate.filePath = filePath;
					particleTemplate.nameSID = SID(fileName.c_str());
					if (!ParticleParameters::Deserialize(filePath, particleTemplate.params))
					{
						PrintError("Failed to read particle parameters file at %s\n", filePath.c_str());
						continue;
					}

					m_ParticleTemplates.emplace(particleTemplate.nameSID, particleTemplate);
				}
			}
		}
	}

	void ResourceManager::SerializeAllParticleSystemTemplates()
	{
		for (auto& pair : m_ParticleTemplates)
		{
			ParticleParameters::Serialize(pair.second.filePath, pair.second.params);
		}
	}

	void ResourceManager::ParseGameObjectTypesFile()
	{
		gameObjectTypeStringIDPairs.clear();
		std::string fileContents;
		// TODO: Gather this info from reflection?
		if (ReadFile(GAME_OBJECT_TYPES_LOCATION, fileContents, false))
		{
			std::vector<std::string> lines = Split(fileContents, '\n');
			for (const std::string& line : lines)
			{
				if (!line.empty())
				{
					const char* lineCStr = line.c_str();
					StringID typeID = Hash(lineCStr);
					if (gameObjectTypeStringIDPairs.find(typeID) != gameObjectTypeStringIDPairs.end())
					{
						PrintError("Game Object Type hash collision on %s!\n", lineCStr);
					}
					gameObjectTypeStringIDPairs.emplace(typeID, line);
				}
			}
		}
		else
		{
			PrintError("Failed to read game object types file from %s!\n", GAME_OBJECT_TYPES_LOCATION);
		}
	}

	void ResourceManager::SerializeGameObjectTypesFile()
	{
		StringBuilder fileContents;

		for (auto iter = gameObjectTypeStringIDPairs.begin(); iter != gameObjectTypeStringIDPairs.end(); ++iter)
		{
			fileContents.AppendLine(iter->second);
		}

		if (!WriteFile(GAME_OBJECT_TYPES_LOCATION, fileContents.ToString(), false))
		{
			PrintError("Failed to write game object types file to %s\n", GAME_OBJECT_TYPES_LOCATION);
		}
	}

	const char* ResourceManager::TypeIDToString(StringID typeID)
	{
		for (const auto& pair : gameObjectTypeStringIDPairs)
		{
			if (pair.first == typeID)
			{
				return pair.second.c_str();
			}
		}
		return "Unknown";
	}

	void ResourceManager::ParseFontFile()
	{
		PROFILE_AUTO("ResourceManager ParseFontFile");

		if (!FileExists(m_FontsFilePathAbs))
		{
			PrintError("Fonts file missing!\n");
		}
		else
		{
			JSONObject fontSettings;
			if (JSONParser::ParseFromFile(m_FontsFilePathAbs, fontSettings))
			{
				std::vector<JSONObject> fontObjs;
				if (fontSettings.TryGetObjectArray("fonts", fontObjs))
				{
					for (const JSONObject& fontObj : fontObjs)
					{
						FontMetaData metaData = {};

						fontObj.TryGetString("name", metaData.name);
						std::string fileName;
						fontObj.TryGetString("file path", fileName);
						metaData.size = (i16)fontObj.GetInt("size");
						fontObj.TryGetBool("screen space", metaData.bScreenSpace);
						fontObj.TryGetFloat("threshold", metaData.threshold);
						fontObj.TryGetFloat("shadow opacity", metaData.shadowOpacity);
						fontObj.TryGetVec2("shadow offset", metaData.shadowOffset);
						fontObj.TryGetFloat("soften", metaData.soften);

						if (fileName.empty())
						{
							PrintError("Font doesn't contain file name!\n");
							continue;
						}

						metaData.filePath = FONT_DIRECTORY + fileName;
						SetRenderedSDFFilePath(metaData);

						std::string fontName = fontObj.GetString("name");
						StringID fontNameID = Hash(fontName.c_str());
						if (fontMetaData.find(fontNameID) != fontMetaData.end())
						{
							// TODO: Handle collision
							PrintError("Hash collision detected in font meta data for %s : %lu\n", fontName.c_str(), fontNameID);
						}
						fontMetaData[fontNameID] = metaData;
					}
				}
			}
			else
			{
				PrintError("Failed to parse font config file %s\n\terror: %s\n", m_FontsFilePathAbs.c_str(), JSONParser::GetErrorString());
			}
		}
	}

	void ResourceManager::SerializeFontFile()
	{
		std::vector<JSONObject> fontObjs;

		for (auto& fontPair : fontMetaData)
		{
			FontMetaData metaData = fontMetaData[fontPair.first];

			JSONObject fontObj = {};

			fontObj.fields.emplace_back("name", JSONValue(metaData.name));
			std::string relativeFilePath = StripLeadingDirectories(metaData.filePath);
			fontObj.fields.emplace_back("file path", JSONValue(relativeFilePath));
			fontObj.fields.emplace_back("size", JSONValue((i32)metaData.size));
			fontObj.fields.emplace_back("screen space", JSONValue(metaData.bScreenSpace));
			fontObj.fields.emplace_back("threshold", JSONValue(metaData.threshold, 2));
			fontObj.fields.emplace_back("shadow opacity", JSONValue(metaData.shadowOpacity, 2));
			fontObj.fields.emplace_back("shadow offset", JSONValue(VecToString(metaData.shadowOffset, 2)));
			fontObj.fields.emplace_back("soften", JSONValue(metaData.soften, 2));

			fontObjs.push_back(fontObj);
		}

		JSONObject fontSettings;
		fontSettings.fields.emplace_back("fonts", JSONValue(fontObjs));

		std::string fileContents = fontSettings.ToString();

		if (!WriteFile(m_FontsFilePathAbs, fileContents, false))
		{
			PrintError("Failed to write font file to %s\n", m_FontsFilePathAbs.c_str());
		}
	}

	void ResourceManager::ParseMaterialsFiles()
	{
		PROFILE_AUTO("ResourceManager ParseMaterialsFiles");

		parsedMaterialInfos.clear();

		std::vector<std::string> filePaths;
		if (Platform::FindFilesInDirectory(MATERIALS_DIRECTORY, filePaths, "*", false))
		{
			for (const std::string& filePath : filePaths)
			{
				JSONObject parentObj;
				if (JSONParser::ParseFromFile(filePath, parentObj))
				{
					i32 fileVersion = parentObj.GetInt("version");

					MaterialCreateInfo matCreateInfo = {};
					JSONObject materialObj = parentObj.GetObject("material");
					Material::ParseJSONObject(materialObj, matCreateInfo, fileVersion);

					parsedMaterialInfos.push_back(matCreateInfo);
				}
				else
				{
					PrintError("Failed to parse material file at %s\n\terror: %s\n", filePath.c_str(), JSONParser::GetErrorString());
				}
			}
		}
		else
		{
			PrintError("Failed to find any material files in %s\n", MATERIALS_DIRECTORY);
			return;
		}

		if (g_bEnableLogging_Loading)
		{
			Print("Parsed %u materials\n", (u32)parsedMaterialInfos.size());
		}
	}

	bool ResourceManager::SerializeAllMaterials() const
	{
		bool bAllSucceeded = true;

		for (const MaterialCreateInfo& materialInfo : parsedMaterialInfos)
		{
			MaterialID matID;
			if (g_Renderer->FindOrCreateMaterialByName(materialInfo.name, matID))
			{
				if (!SerializeMaterial(g_Renderer->GetMaterial(matID)))
				{
					bAllSucceeded = false;
				}
			}
		}

		return bAllSucceeded;
	}

	bool ResourceManager::SerializeLoadedMaterials() const
	{
		const std::map<MaterialID, Material*>& materials = g_Renderer->GetLoadedMaterials();

		bool bAllSucceeded = true;

		for (auto& matPair : materials)
		{
			Material* material = matPair.second;
			if (!SerializeMaterial(material))
			{
				bAllSucceeded = false;
			}
		}

		return bAllSucceeded;
	}

	bool ResourceManager::SerializeMaterial(Material* material) const
	{
		if (material->bSerializable)
		{
			JSONObject materialObj = material->Serialize();
			std::string fileContents = materialObj.ToString();

			std::string hypenatedName = Replace(material->name, ' ', '-');
			const std::string fileName = MATERIALS_DIRECTORY + hypenatedName + ".json";
			if (!WriteFile(fileName, fileContents, false))
			{
				PrintWarn("Failed to serialize material %s to file %s\n", material->name.c_str(), fileName.c_str());
				return false;
			}
		}
		return true;
	}

	void ResourceManager::ParseDebugOverlayNamesFile()
	{
		debugOverlayNames.clear();

		std::string fileContents;
		if (!ReadFile(DEBUG_OVERLAY_NAMES_LOCATION, fileContents, false))
		{
			PrintError("Failed to read debug overlay names definition file\n");
			return;
		}

		JSONObject rootObject;
		if (!JSONParser::Parse(fileContents, rootObject))
		{
			PrintError("Failed to parse debug overlay names definition file\n");
			return;
		}

		std::vector<JSONField> nameFields = rootObject.GetFieldArray("debug overlay names");

		debugOverlayNames.reserve(nameFields.size());
		for (JSONField& field : nameFields)
		{
			debugOverlayNames.emplace_back(field.value.strValue);
		}
	}

	void ResourceManager::SetRenderedSDFFilePath(FontMetaData& metaData)
	{
		static const std::string DPIStr = FloatToString(g_Monitor->DPI.x, 0) + "DPI";

		metaData.renderedTextureFilePath = StripFileType(StripLeadingDirectories(metaData.filePath));
		metaData.renderedTextureFilePath += "-" + IntToString(metaData.size, 2) + "-" + DPIStr + m_FontImageExtension;
		metaData.renderedTextureFilePath = FONT_SDF_DIRECTORY + metaData.renderedTextureFilePath;
	}

	bool ResourceManager::LoadFontMetrics(const std::vector<char>& fileMemory,
		FT_Library ft,
		FontMetaData& metaData,
		std::map<i32, FontMetric*>* outCharacters,
		std::array<glm::vec2i, 4>* outMaxPositions,
		FT_Face* outFace)
	{
		PROFILE_BEGIN("LoadFontMetrics");

		CHECK_EQ(metaData.bitmapFont, nullptr);

		// TODO: Save in common place
		u32 sampleDensity = 32;

		FT_Error error = FT_New_Memory_Face(ft, (FT_Byte*)fileMemory.data(), (FT_Long)fileMemory.size(), 0, outFace);
		FT_Face& face = *outFace;
		if (error == FT_Err_Unknown_File_Format)
		{
			PrintError("Unhandled font file format: %s\n", metaData.filePath.c_str());
			return false;
		}
		else if (error != FT_Err_Ok || !face)
		{
			PrintError("Failed to create new font face: %s\n", metaData.filePath.c_str());
			return false;
		}

		i32 fontHeight = metaData.size * sampleDensity;
		error = FT_Set_Char_Size(face,
			0, fontHeight,
			(FT_UInt)g_Monitor->DPI.x,
			(FT_UInt)g_Monitor->DPI.y);

		if (error != FT_Err_Ok)
		{
			PrintError("Failed to set font size to %d for font %s\n", fontHeight, metaData.filePath.c_str());
			return false;
		}

		if (g_bEnableLogging_Loading)
		{
			const std::string fileName = StripLeadingDirectories(metaData.filePath);
			Print("Loaded font file %s\n", fileName.c_str());
		}

		std::string fontName = std::string(face->family_name) + " - " + face->style_name;
		metaData.bitmapFont = new BitmapFont(metaData, fontName, face->num_glyphs);
		BitmapFont* newFont = metaData.bitmapFont;

		if (metaData.bScreenSpace)
		{
			fontsScreenSpace.push_back(newFont);
		}
		else
		{
			fontsWorldSpace.push_back(newFont);
		}

		//newFont->SetUseKerning(FT_HAS_KERNING(face) != 0);

		// Atlas helper variables
		glm::vec2i startPos[4] = { { 0.0f, 0.0f },{ 0.0f, 0.0f },{ 0.0f, 0.0f },{ 0.0f, 0.0f } };
		glm::vec2i maxPos[4] = { { 0.0f, 0.0f },{ 0.0f, 0.0f },{ 0.0f, 0.0f },{ 0.0f, 0.0f } };
		bool bHorizontal = false; // Direction this pass expands the map in (internal moves are !bHorizontal)
		u32 posCount = 1; // Internal move count in this pass
		u32 curPos = 0;   // Internal move count
		u32 channel = 0;  // Current channel writing to

		u32 padding = 1;
		u32 spread = 5;
		u32 totPadding = padding + spread;

		for (i32 c = 0; c < BitmapFont::CHAR_COUNT - 1; ++c)
		{
			FontMetric* metric = newFont->GetMetric((wchar_t)c);
			if (!metric)
			{
				continue;
			}

			metric->character = (wchar_t)c;

			u32 glyphIndex = FT_Get_Char_Index(face, c);
			if (glyphIndex == 0)
			{
				continue;
			}

			if (newFont->bUseKerning && glyphIndex)
			{
				for (i32 previous = 0; previous < BitmapFont::CHAR_COUNT - 1; ++previous)
				{
					FT_Vector delta;

					u32 prevIdx = FT_Get_Char_Index(face, previous);
					FT_Get_Kerning(face, prevIdx, glyphIndex, FT_KERNING_DEFAULT, &delta);

					if (delta.x != 0 || delta.y != 0)
					{
						std::wstring charKey(std::wstring(1, (wchar_t)previous) + std::wstring(1, (wchar_t)c));
						metric->kerning[charKey] =
							glm::vec2((real)delta.x / 64.0f, (real)delta.y / 64.0f);
					}
				}
			}

			if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER))
			{
				PrintError("Failed to load glyph with index %u\n", glyphIndex);
				continue;
			}


			u32 width = face->glyph->bitmap.width + totPadding * 2;
			u32 height = face->glyph->bitmap.rows + totPadding * 2;


			metric->width = (u16)width;
			metric->height = (u16)height;
			metric->offsetX = (i16)(face->glyph->bitmap_left + totPadding);
			metric->offsetY = -(i16)(face->glyph->bitmap_top + totPadding);
			metric->advanceX = (real)face->glyph->advance.x / 64.0f;

			// Generate atlas coordinates
			metric->channel = (u8)channel;
			metric->texCoord = startPos[channel];
			if (bHorizontal)
			{
				maxPos[channel].y = std::max(maxPos[channel].y, startPos[channel].y + (i32)height);
				startPos[channel].y += height;
				maxPos[channel].x = std::max(maxPos[channel].x, startPos[channel].x + (i32)width);
			}
			else
			{
				maxPos[channel].x = std::max(maxPos[channel].x, startPos[channel].x + (i32)width);
				startPos[channel].x += width;
				maxPos[channel].y = std::max(maxPos[channel].y, startPos[channel].y + (i32)height);
			}
			channel++;
			if (channel == 4)
			{
				channel = 0;
				curPos++;
				if (curPos == posCount)
				{
					curPos = 0;
					bHorizontal = !bHorizontal;
					if (bHorizontal)
					{
						for (u8 cha = 0; cha < 4; ++cha)
						{
							startPos[cha] = glm::vec2i(maxPos[cha].x, 0);
						}
					}
					else
					{
						for (u8 cha = 0; cha < 4; ++cha)
						{
							startPos[cha] = glm::vec2i(0, maxPos[cha].y);
						}
						posCount++;
					}
				}
			}

			metric->bIsValid = true;

			(*outCharacters)[c] = metric;
		}

		(*outMaxPositions)[0] = maxPos[0];
		(*outMaxPositions)[1] = maxPos[1];
		(*outMaxPositions)[2] = maxPos[2];
		(*outMaxPositions)[3] = maxPos[3];
		*outFace = face;

		return true;
	}

	void ResourceManager::LoadFonts(bool bForceRender)
	{
		PROFILE_AUTO("Load fonts");

		fontsScreenSpace.clear();
		fontsWorldSpace.clear();

		for (auto& pair : fontMetaData)
		{
			FontMetaData& metaData = pair.second;

			if (metaData.bitmapFont != nullptr)
			{
				delete metaData.bitmapFont;
				metaData.bitmapFont = nullptr;
			}

			const std::string fontName = StripFileType(StripLeadingDirectories(metaData.filePath));
			g_Renderer->LoadFont(metaData, bForceRender);
		}
	}

	Texture* ResourceManager::FindLoadedTextureWithPath(const std::string& filePath)
	{
		for (Texture* texture : loadedTextures)
		{
			if (texture && !filePath.empty() && filePath.compare(texture->relativeFilePath) == 0)
			{
				return texture;
			}
		}

		return nullptr;
	}

	Texture* ResourceManager::FindLoadedTextureWithName(const std::string& fileName)
	{
		for (Texture* texture : loadedTextures)
		{
			if (texture && fileName.compare(texture->fileName) == 0)
			{
				return texture;
			}
		}

		return nullptr;
	}

	Texture* ResourceManager::GetLoadedTexture(TextureID textureID)
	{
		if (textureID < loadedTextures.size())
		{
			return loadedTextures[textureID];
		}
		return nullptr;
	}

	TextureID ResourceManager::GetOrLoadTexture(const std::string& textureFilePath)
	{
		for (u32 i = 0; i < (u32)loadedTextures.size(); ++i)
		{
			Texture* texture = loadedTextures[i];
			if (texture && texture->relativeFilePath.compare(textureFilePath) == 0)
			{
				return (TextureID)i;
			}
		}

		// TODO: Support other samplers
		TextureID texID = g_Renderer->InitializeTextureFromFile(textureFilePath, g_Renderer->GetSamplerLinearRepeat(), false, false, false);
		return texID;
	}

	bool ResourceManager::RemoveLoadedTexture(TextureID textureID, bool bDestroy)
	{
		return RemoveLoadedTexture(GetLoadedTexture(textureID), bDestroy);
	}

	bool ResourceManager::RemoveLoadedTexture(Texture* texture, bool bDestroy)
	{
		if (texture == nullptr)
		{
			return false;
		}

		for (auto iter = loadedTextures.begin(); iter != loadedTextures.end(); ++iter)
		{
			if (*iter == texture)
			{
				TextureID textureID = (TextureID)(iter - loadedTextures.begin());
				if (bDestroy)
				{
					g_Renderer->OnTextureDestroyed(textureID);
					delete* iter;
				}
				loadedTextures[textureID] = nullptr;
				return true;
			}
		}

		return false;
	}

	TextureID ResourceManager::GetOrLoadIcon(StringID prefabNameSID, i32 resolution /* = -1 */)
	{
		for (Pair<StringID, IconMetaData>& pair : discoveredIcons)
		{
			if (pair.first == prefabNameSID &&
				(resolution == -1 || pair.second.resolution == resolution))
			{
				if (pair.second.textureID == InvalidTextureID)
				{
					pair.second.textureID = GetOrLoadTexture(pair.second.relativeFilePath);
				}
				return pair.second.textureID;
			}
		}

		return InvalidTextureID;
	}

	TextureID ResourceManager::GetNextAvailableTextureID()
	{
		for (u32 i = 0; i < loadedTextures.size(); ++i)
		{
			if (loadedTextures[i] == nullptr)
			{
				return (TextureID)i;
			}
		}
		loadedTextures.push_back(nullptr);
		return (TextureID)(loadedTextures.size() - 1);
	}

	TextureID ResourceManager::AddLoadedTexture(Texture* texture)
	{
		TextureID textureID = GetNextAvailableTextureID();
		loadedTextures[textureID] = texture;
		return textureID;
	}

	MaterialCreateInfo* ResourceManager::GetMaterialInfo(const char* materialName)
	{
		for (MaterialCreateInfo& parsedMatInfo : parsedMaterialInfos)
		{
			if (parsedMatInfo.name.compare(materialName) == 0)
			{
				return &parsedMatInfo;
			}
		}

		return nullptr;
	}

	// DEPRECATED: Use PrefabID overload instead. This function should only be used for scene files <= v5
	GameObject* ResourceManager::GetPrefabTemplate(const char* prefabName) const
	{
		for (const PrefabTemplateInfo& prefabTemplateInfo : prefabTemplates)
		{
			if (StrCmpCaseInsensitive(prefabTemplateInfo.templateObject->GetName().c_str(), prefabName) == 0)
			{
				return prefabTemplateInfo.templateObject;
			}
		}

		return nullptr;
	}

	PrefabID ResourceManager::GetPrefabID(const char* prefabName) const
	{
		for (const PrefabTemplateInfo& prefabTemplateInfo : prefabTemplates)
		{
			if (StrCmpCaseInsensitive(prefabTemplateInfo.templateObject->GetName().c_str(), prefabName) == 0)
			{
				return prefabTemplateInfo.prefabID;
			}
		}

		return InvalidPrefabID;
	}

	GameObject* ResourceManager::GetPrefabTemplate(const PrefabIDPair& prefabIDPair) const
	{
		return GetPrefabTemplate(prefabIDPair.m_PrefabID, prefabIDPair.m_SubGameObjectID);
	}

	GameObject* ResourceManager::GetPrefabTemplate(const PrefabID& prefabID) const
	{
		return GetPrefabTemplate(prefabID, InvalidGameObjectID);
	}

	GameObject* ResourceManager::GetPrefabTemplate(const PrefabID& prefabID, const GameObjectID& subObjectID) const
	{
		// TODO: Use map
		for (const PrefabTemplateInfo& prefabTemplateInfo : prefabTemplates)
		{
			if (prefabTemplateInfo.prefabID == prefabID)
			{
				if (!subObjectID.IsValid())
				{
					return prefabTemplateInfo.templateObject;
				}

				return GetPrefabSubObject(prefabTemplateInfo.templateObject, subObjectID);
			}
		}

		return nullptr;
	}

	GameObject* ResourceManager::GetPrefabSubObject(GameObject* prefabTemplate, const GameObjectID& subObjectID) const
	{
		if (prefabTemplate->ID == subObjectID)
		{
			return prefabTemplate;
		}

		for (GameObject* gameObject : prefabTemplate->m_Children)
		{
			GameObject* subObject = GetPrefabSubObject(gameObject, subObjectID);
			if (subObject != nullptr)
			{
				return subObject;
			}
		}

		return nullptr;
	}

	std::string ResourceManager::GetPrefabFileName(const PrefabID& prefabID) const
	{
		for (const PrefabTemplateInfo& prefabTemplateInfo : prefabTemplates)
		{
			if (prefabTemplateInfo.prefabID == prefabID)
			{
				return prefabTemplateInfo.fileName;
			}
		}

		return "";
	}

	bool ResourceManager::IsPrefabDirty(const PrefabID& prefabID) const
	{
		for (const PrefabTemplateInfo& prefabTemplateInfo : prefabTemplates)
		{
			if (prefabTemplateInfo.prefabID == prefabID)
			{
				return prefabTemplateInfo.bDirty;
			}
		}

		return true;
	}

	void ResourceManager::SetPrefabDirty(const PrefabID& prefabID)
	{
		for (PrefabTemplateInfo& prefabTemplateInfo : prefabTemplates)
		{
			if (prefabTemplateInfo.prefabID == prefabID)
			{
				prefabTemplateInfo.bDirty = true;
			}
		}
	}

	void ResourceManager::SetAllPrefabsDirty(bool bDirty)
	{
		for (PrefabTemplateInfo& prefabTemplateInfo : prefabTemplates)
		{
			prefabTemplateInfo.bDirty = bDirty;
		}
	}

	void ResourceManager::UpdatePrefabData(GameObject* prefabTemplate, const PrefabID& prefabID)
	{
		for (PrefabTemplateInfo& prefabTemplateInfo : prefabTemplates)
		{
			if (prefabID == prefabTemplateInfo.prefabID)
			{
				CHECK_NE(prefabTemplateInfo.templateObject, prefabTemplate);

				prefabTemplateInfo.templateObject->Destroy();
				delete prefabTemplateInfo.templateObject;

				prefabTemplateInfo.templateObject = prefabTemplate;

				WritePrefabToDisk(prefabTemplateInfo);

				g_SceneManager->CurrentScene()->OnPrefabChanged(prefabID);

				return;
			}
		}

		char prefabIDStrBuf[33];
		prefabID.ToString(prefabIDStrBuf);
		std::string prefabName = prefabTemplate->GetName();
		PrintError("Attempted to update prefab template but no previous prefabs with PrefabID %s exist (name: %s)\n", prefabIDStrBuf, prefabName.c_str());
	}

	bool ResourceManager::WriteExistingPrefabToDisk(GameObject* prefabTemplate)
	{
		std::string prefabName = prefabTemplate->GetName();
		PrefabID prefabID = GetPrefabID(prefabName.c_str());
		char fileNameStrBuff[256];
		snprintf(fileNameStrBuff, ARRAY_LENGTH(fileNameStrBuff), "%s.json", prefabName.c_str());
		PrefabTemplateInfo prefabTemplateInfo(prefabTemplate, prefabID, std::string(fileNameStrBuff));

		prefabTemplate->m_bIsTemplate = true;
		bool bResult = WritePrefabToDisk(prefabTemplateInfo);
		prefabTemplate->m_bIsTemplate = false;
		return bResult;
	}

	PrefabID ResourceManager::CreateNewPrefab(GameObject* sourceObject, const char* fileName)
	{
		using CopyFlags = GameObject::CopyFlags;

		PrefabID newPrefabID = Platform::GenerateGUID();

		CopyFlags copyFlags = (CopyFlags)(
			(CopyFlags::ALL &
				~CopyFlags::ADD_TO_SCENE &
				~CopyFlags::CREATE_RENDER_OBJECT)
			| CopyFlags::COPYING_TO_PREFAB);
		std::string templateName = sourceObject->GetName();
		GameObject* prefabTemplate = sourceObject->CopySelf(nullptr, copyFlags, &templateName);

		// Give all objects new IDs so they don't conflict with the instance's
		prefabTemplate->ChangeAllIDs();

		prefabTemplate->m_SourcePrefabID.m_PrefabID = newPrefabID;
		prefabTemplate->m_SourcePrefabID.m_SubGameObjectID = prefabTemplate->ID;

		prefabTemplates.emplace_back(prefabTemplate, newPrefabID, fileName);

		return newPrefabID;
	}

	bool ResourceManager::IsPrefabIDValid(const PrefabID& prefabID)
	{
		GameObject* prefabTemplate = GetPrefabTemplate(prefabID);
		return prefabTemplate != nullptr;
	}

	void ResourceManager::DeletePrefabTemplate(const PrefabID& prefabID)
	{
		for (auto iter = prefabTemplates.begin(); iter != prefabTemplates.end(); ++iter)
		{
			if (iter->prefabID == prefabID)
			{
				g_SceneManager->CurrentScene()->DeleteInstancesOfPrefab(prefabID);

				if (!Platform::DeleteFile(PREFAB_DIRECTORY + iter->fileName))
				{
					PrintError("Failed to delete prefab template at %s\n", iter->fileName.c_str());
				}
				iter->templateObject->Destroy();
				delete iter->templateObject;
				prefabTemplates.erase(iter);

				break;
			}
		}
	}

	bool ResourceManager::PrefabTemplateContainsChild(const PrefabID& prefabID, GameObject* child) const
	{
		GameObject* prefabTemplate = GetPrefabTemplate(prefabID);

		return PrefabTemplateContainsChildRecursive(prefabTemplate, child);
	}

	void ResourceManager::SerializeAllPrefabTemplates()
	{
		for (PrefabTemplateInfo& prefabTemplateInfo : prefabTemplates)
		{
			CHECK(prefabTemplateInfo.templateObject->m_bIsTemplate);
			WritePrefabToDisk(prefabTemplateInfo);
		}
	}

	AudioSourceID ResourceManager::GetAudioSourceID(StringID audioFileSID)
	{
		return discoveredAudioFiles[audioFileSID].sourceID;
	}

	AudioSourceID ResourceManager::GetOrLoadAudioSourceID(StringID audioFileSID, bool b2D)
	{
		auto iter = discoveredAudioFiles.find(audioFileSID);
		if (iter == discoveredAudioFiles.end())
		{
			PrintError("Attempted to get undiscovered audio file with SID %lu", audioFileSID);
			return InvalidAudioSourceID;
		}

		if (iter->second.sourceID == InvalidAudioSourceID)
		{
			LoadAudioFile(audioFileSID, nullptr, b2D);
		}

		return discoveredAudioFiles[audioFileSID].sourceID;
	}

	void ResourceManager::DestroyAudioSource(AudioSourceID audioSourceID)
	{
		for (auto& pair : discoveredAudioFiles)
		{
			if (pair.second.sourceID == audioSourceID)
			{
				AudioManager::DestroyAudioSource(pair.second.sourceID);
				pair.second.sourceID = InvalidAudioSourceID;
				break;
			}
		}
	}

	void ResourceManager::LoadAudioFile(StringID audioFileSID, StringBuilder* errorStringBuilder, bool b2D)
	{
		std::string filePath = SFX_DIRECTORY + discoveredAudioFiles[audioFileSID].name;
		discoveredAudioFiles[audioFileSID].sourceID = AudioManager::AddAudioSource(filePath, errorStringBuilder, b2D);
		discoveredAudioFiles[audioFileSID].bInvalid = (discoveredAudioFiles[audioFileSID].sourceID == InvalidAudioSourceID);
	}

	u32 ResourceManager::GetMaxStackSize(const PrefabID& prefabID)
	{
		GameObject* templateObject = GetPrefabTemplate(prefabID);
		if (templateObject != nullptr)
		{
			auto iter = m_NonDefaultStackSizes.find(templateObject->GetTypeID());
			if (iter != m_NonDefaultStackSizes.end())
			{
				return iter->second;
			}
		}
		return DEFAULT_MAX_STACK_SIZE;
	}

	void ResourceManager::AddNewGameObjectType(const char* newType)
	{
		StringID newTypeID = Hash(newType);
		gameObjectTypeStringIDPairs.emplace(newTypeID, std::string(newType));

		SerializeGameObjectTypesFile();
	}

	void ResourceManager::AddNewParticleTemplate(StringID particleTemplateNameSID, const ParticleSystemTemplate particleTemplate)
	{
		m_ParticleTemplates.emplace(particleTemplateNameSID, particleTemplate);
	}

	bool ResourceManager::GetParticleTemplate(StringID particleTemplateNameSID, ParticleSystemTemplate& outParticleTemplate)
	{
		for (auto& pair : m_ParticleTemplates)
		{
			if (pair.first == particleTemplateNameSID)
			{
				outParticleTemplate = pair.second;
				return true;
			}
		}
		return false;
	}

	bool ResourceManager::PrefabTemplateContainsChildRecursive(GameObject* prefabTemplate, GameObject* child) const
	{
		std::string childName = child->GetName();
		for (GameObject* prefabChild : prefabTemplate->m_Children)
		{
			// TODO: Use more robust metric to check for uniqueness
			if (prefabChild->GetName().compare(childName) == 0 && prefabChild->m_TypeID == child->m_TypeID)
			{
				return true;
			}

			if (PrefabTemplateContainsChildRecursive(prefabTemplate, prefabChild))
			{
				return true;
			}
		}

		return false;
	}

	bool ResourceManager::WritePrefabToDisk(PrefabTemplateInfo& prefabTemplateInfo)
	{
		std::string path = RelativePathToAbsolute(PREFAB_DIRECTORY + prefabTemplateInfo.fileName);

		JSONObject prefabJSON = {};
		prefabJSON.fields.emplace_back("version", JSONValue(BaseScene::LATETST_PREFAB_FILE_VERSION));
		// Added in prefab v3
		prefabJSON.fields.emplace_back("scene version", JSONValue(BaseScene::LATEST_SCENE_FILE_VERSION));

		if (!prefabTemplateInfo.prefabID.IsValid())
		{
			PrintError("Prefab %s has invalid prefabID!\n", prefabTemplateInfo.fileName.c_str());
		}

		// Added in prefab v2
		std::string prefabIDStr = prefabTemplateInfo.prefabID.ToString();
		prefabJSON.fields.emplace_back("prefab id", JSONValue(prefabIDStr));

		JSONObject objectSource = prefabTemplateInfo.templateObject->Serialize(g_SceneManager->CurrentScene(), true, true);
		prefabJSON.fields.emplace_back("root", JSONValue(objectSource));

		std::string fileContents = prefabJSON.ToString();
		if (WriteFile(path, fileContents, false))
		{
			prefabTemplateInfo.bDirty = false;
			std::string prefabName = prefabTemplateInfo.templateObject->GetName();
			Print("Successfully serialized prefab \"%s\"\n", prefabName.c_str());
			return true;
		}
		else
		{
			prefabTemplateInfo.bDirty = true;
			std::string prefabName = prefabTemplateInfo.templateObject->GetName();
			PrintError("Failed to serialize prefab %s to %s\n", prefabName.c_str(), path.c_str());
			return false;
		}
	}

	void ResourceManager::DrawImGuiMeshList(i32* selectedMeshIndex, ImGuiTextFilter* meshFilter)
	{
		std::string selectedMeshRelativeFilePath = discoveredMeshes[*selectedMeshIndex];

		if (ImGui::BeginChild("mesh list", ImVec2(0.0f, 120.0f), true))
		{
			for (i32 i = 0; i < (i32)discoveredMeshes.size(); ++i)
			{
				const std::string& meshFileName = discoveredMeshes[i];
				std::string meshFilePath = MESH_DIRECTORY + meshFileName;
				bool bSelected = (i == *selectedMeshIndex);
				if (meshFilter->PassFilter(meshFileName.c_str()))
				{
					if (ImGui::Selectable(meshFileName.c_str(), &bSelected))
					{
						*selectedMeshIndex = i;
					}

					if (ImGui::BeginPopupContextItem())
					{
						bool bLoaded = loadedMeshes.find(selectedMeshRelativeFilePath) != loadedMeshes.end();

						if (bLoaded)
						{
							if (ImGui::Button("Reload"))
							{
								Mesh::LoadMesh(meshFilePath);

								g_Renderer->ReloadObjectsWithMesh(meshFilePath);

								ImGui::CloseCurrentPopup();
							}
						}
						else
						{
							if (ImGui::Button("Load"))
							{
								Mesh::LoadMesh(meshFilePath);

								g_Renderer->ReloadObjectsWithMesh(meshFilePath);

								ImGui::CloseCurrentPopup();
							}
						}

						ImGui::EndPopup();
					}
					else
					{
						if (ImGui::IsItemActive())
						{
							if (ImGui::BeginDragDropSource())
							{
								const void* data = (void*)(meshFilePath.c_str());
								size_t size = strlen(meshFilePath.c_str()) * sizeof(char);

								ImGui::SetDragDropPayload(Editor::MeshPayloadCStr, data, size);

								ImGui::Text("%s", meshFileName.c_str());

								ImGui::EndDragDropSource();
							}
						}
					}
				}
			}
		}
		ImGui::EndChild();
	}

	void ResourceManager::DrawImGuiMenuItemizableItems()
	{
		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);
		for (PrefabTemplateInfo& pair : prefabTemplates)
		{
			if (pair.templateObject->IsItemizable())
			{
				ImGui::PushID(pair.templateObject->m_Name.c_str());

				ImGui::Text("%s", pair.templateObject->m_Name.c_str());

				ImGui::SameLine();

				if (ImGui::Button("x1"))
				{
					player->AddToInventory(pair.prefabID, 1);
				}

				ImGui::SameLine();

				u32 maxStackSize = g_ResourceManager->GetMaxStackSize(pair.prefabID);

				char maxStackSizeBuff[6];
				snprintf(maxStackSizeBuff, ARRAY_LENGTH(maxStackSizeBuff), "x%u", maxStackSize);

				if (ImGui::Button(maxStackSizeBuff))
				{
					player->AddToInventory(pair.prefabID, maxStackSize);
				}

				ImGui::PopID();
			}
		}
	}

	bool ResourceManager::DrawAudioSourceIDImGui(const char* label, StringID& audioSourceSID)
	{
		bool bValuesChanged = false;

		static const char* previewStrNone = "None";
		const char* previewStr;

		if (audioSourceSID != InvalidStringID)
		{
			previewStr = discoveredAudioFiles[audioSourceSID].name.c_str();
		}
		else
		{
			previewStr = previewStrNone;
		}

		if (ImGui::BeginCombo(label, previewStr))
		{
			for (auto& audioFilePair : discoveredAudioFiles)
			{
				const char* audioFileName = audioFilePair.second.name.c_str();

				bool bSelected = (audioFilePair.first == audioSourceSID);
				bool bWasInvalid = discoveredAudioFiles[audioFilePair.first].bInvalid;
				if (bWasInvalid)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
				}
				// TODO: Indicate when sound is playing with colour change
				if (ImGui::Selectable(audioFileName, &bSelected))
				{
					//if (audioFilePair.second == InvalidAudioSourceID)
					//{
					//	LoadAudioFile(audioFilePair.first, nullptr);
					//	audioFilePair.second = discoveredAudioFiles[audioFilePair.first].sourceID;
					//}

					audioSourceSID = audioFilePair.first;
					bValuesChanged = true;
				}
				if (bWasInvalid)
				{
					ImGui::PopStyleColor();
				}

				if (ImGui::IsItemActive())
				{
					if (ImGui::BeginDragDropSource())
					{
						void* data = (void*)&audioFilePair.first;
						size_t size = sizeof(StringID);

						ImGui::SetDragDropPayload(Editor::AudioFileNameSIDPayloadCStr, data, size);

						ImGui::Text("%s", audioFileName);

						ImGui::EndDragDropSource();
					}

					if (ImGui::BeginDragDropTarget())
					{
						const ImGuiPayload* audioSourcePayload = ImGui::AcceptDragDropPayload(Editor::AudioFileNameSIDPayloadCStr);
						if (audioSourcePayload != nullptr && audioSourcePayload->Data != nullptr)
						{
							StringID draggedSID = *((StringID*)audioSourcePayload->Data);
							audioSourceSID = draggedSID;
							bValuesChanged = true;
						}

						ImGui::EndDragDropTarget();
					}
				}
			}

			ImGui::EndCombo();
		}

		return bValuesChanged;
	}

	void ResourceManager::DrawParticleSystemTemplateImGuiObjects()
	{
		if (ImGui::TreeNode("Templates"))
		{
			for (auto iter = m_ParticleTemplates.begin(); iter != m_ParticleTemplates.end(); ++iter)
			{
				ParticleSystemTemplate::ImGuiResult result = iter->second.DrawImGuiObjects();
				switch (result)
				{
				case ParticleSystemTemplate::ImGuiResult::REMOVED:
				{
					Platform::DeleteFile(iter->second.filePath);
					m_ParticleTemplates.erase(iter);
				} break;
				}
			}
			ImGui::TreePop();
		}
	}

	void ResourceManager::DrawParticleParameterTypesImGui()
	{
		if (ImGui::TreeNode("Parameter types"))
		{
			if (ImGui::Button(m_bParticleParameterTypesDirty ? "Save to file*" : "Save to file"))
			{
				SerializeParticleParameterTypes();
			}

			ImGui::SameLine();

			if (ImGui::Button("Reload from file"))
			{
				DiscoverParticleParameterTypes();
			}

			for (auto iter = particleParameterTypes.begin(); iter != particleParameterTypes.end(); ++iter)
			{
				const char* typeName = iter->name.c_str();
				ImGui::PushID(typeName);

				ImGui::Text("%s", typeName);

				i32 valueTypeIndex = (i32)iter->valueType;
				if (ImGui::Combo("", &valueTypeIndex, ParticleParamterValueTypeStrings, ARRAY_LENGTH(ParticleParamterValueTypeStrings) - 1))
				{
					iter->valueType = (ParticleParamterValueType)valueTypeIndex;
					m_bParticleParameterTypesDirty = true;
				}

				bool bRemoved = false;
				if (ImGui::Button("Remove type"))
				{
					particleParameterTypes.erase(iter);
					m_bParticleParameterTypesDirty = true;
					bRemoved = true;
				}

				ImGui::PopID();

				if (bRemoved)
				{
					break;
				}
			}

			static const char* addNewTypePopup = "New particle parameter type";
			if (ImGui::Button("Add new type"))
			{
				ImGui::OpenPopup(addNewTypePopup);
			}

			if (ImGui::BeginPopupModal(addNewTypePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			{
				static char typeNameBuff[256];
				bool bCreateNewType = ImGui::InputText("Name", typeNameBuff, 256, ImGuiInputTextFlags_EnterReturnsTrue);

				static i32 typeIndex = (i32)ParticleParamterValueType::FLOAT;
				ImGui::Combo("Type", &typeIndex, ParticleParamterValueTypeStrings, ARRAY_LENGTH(ParticleParamterValueTypeStrings) - 1);

				bCreateNewType = ImGui::Button("Create") || bCreateNewType;

				if (bCreateNewType && typeIndex != (i32)ParticleParamterValueType::_NONE)
				{
					std::string typeNameStr(typeNameBuff);
					particleParameterTypes.emplace_back(typeNameStr, (ParticleParamterValueType)typeIndex);
					m_bParticleParameterTypesDirty = true;
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			ImGui::TreePop();
		}
	}

	void ResourceManager::DiscoverParticleParameterTypes()
	{
		particleParameterTypes.clear();

		const char* filePath = PARTICLE_PARAMETER_TYPES_LOCATION;
		if (FileExists(filePath))
		{
			std::string fileContents;
			if (!ReadFile(filePath, fileContents, false))
			{
				PrintError("Failed to read particle parameter types file at %s\n", filePath);
				return;
			}

			JSONObject rootObj;
			if (!JSONParser::Parse(fileContents, rootObj))
			{
				PrintError("Failed to parse particle parameter types file at %s\n", filePath);
				return;
			}

			std::vector<JSONObject> typeObjs = rootObj.GetObjectArray("particle parameter types");
			for (const JSONObject& typeObj : typeObjs)
			{
				std::string name = typeObj.GetString("name");
				std::string typeStr = typeObj.GetString("type");
				ParticleParamterValueType type = ParticleParamterValueTypeFromString(typeStr.c_str());

				if (type != ParticleParamterValueType::_NONE)
				{
					particleParameterTypes.emplace_back(name, type);
				}
			}

			m_bParticleParameterTypesDirty = false;
		}
	}

	void ResourceManager::SerializeParticleParameterTypes()
	{
		std::vector<JSONObject> parameterTypeObjs;
		for (ParticleParameterType& type : particleParameterTypes)
		{
			JSONObject obj = {};
			const char* valueTypeStr = ParticleParamterValueTypeToString(type.valueType);
			obj.fields.emplace_back("name", JSONValue(type.name));
			obj.fields.emplace_back("type", JSONValue(valueTypeStr));
			parameterTypeObjs.emplace_back(obj);
		}

		JSONObject particleParameterTypesObj = {};
		particleParameterTypesObj.fields.emplace_back("particle parameter types", JSONValue(parameterTypeObjs));
		std::string fileContents = particleParameterTypesObj.ToString();

		const char* filePath = PARTICLE_PARAMETER_TYPES_LOCATION;
		if (!WriteFile(filePath, fileContents, false))
		{
			PrintError("Failed to write particle parameter types to %s\n", filePath);
		}
		else
		{
			m_bParticleParameterTypesDirty = false;
		}
	}

	ParticleParamterValueType ResourceManager::GetParticleParameterValueType(const char* paramName)
	{
		for (const ParticleParameterType& type : particleParameterTypes)
		{
			if (strcmp(type.name.c_str(), paramName) == 0)
			{
				return type.valueType;
			}
		}
		return ParticleParamterValueType::_NONE;
	}

	void ResourceManager::DrawImGuiWindows()
	{
		bool* bFontsWindowOpen = g_EngineInstance->GetUIWindowOpen(SID_PAIR("fonts"));
		if (*bFontsWindowOpen)
		{
			if (ImGui::Begin("Fonts", bFontsWindowOpen))
			{
				for (auto& fontPair : fontMetaData)
				{
					FontMetaData& metaData = fontPair.second;
					BitmapFont* font = metaData.bitmapFont;

					ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollWithMouse;
					if (ImGui::BeginChild(metaData.renderedTextureFilePath.c_str(), ImVec2(0, 240), true, flags))
					{
						ImGui::Text("%s", font->name.c_str());

						ImGui::Columns(2);
						ImGui::SetColumnWidth(0, 350.0f);

						metaData.bDirty |= ImGui::DragFloat("Threshold", &metaData.threshold, 0.001f, 0.0f, 1.0f);
						metaData.bDirty |= ImGui::DragFloat2("Shadow Offset", &metaData.shadowOffset.x, 0.0007f);
						metaData.bDirty |= ImGui::DragFloat("Shadow Opacity", &metaData.shadowOpacity, 0.005f, 0.0f, 0.999f);
						metaData.bDirty |= ImGui::DragFloat("Soften", &metaData.soften, 0.001f, 0.0f, 1.0f);
						// TODO: Store "needs bake" flag as well
						metaData.bDirty |= ImGuiExt::DragInt16("Size", &metaData.size, 1.0f, 4, 256);

						ImGui::Text("Size: %i", metaData.size);
						ImGui::SameLine();
						ImGui::Text("%s space", metaData.bScreenSpace ? "Screen" : "World");
						glm::vec2u texSize(font->GetTextureSize());
						u32 texChannelCount = font->GetTextureChannelCount();
						const u32 bufSize = 64;
						char texSizeBuf[bufSize];
						ByteCountToString(texSizeBuf, bufSize, texSize.x * texSize.y * texChannelCount * sizeof(u32));
						ImGui::Text("Resolution: %ux%u (%s)", texSize.x, texSize.y, texSizeBuf);
						ImGui::Text("Char count: %i", font->characterCount);
						ImGui::Text("Byte count: %i", font->bufferSize);
						ImGui::Text("Use kerning: %s", font->bUseKerning ? "true" : "false");

						// TODO: Add support to ImGui vulkan renderer for images
						//VulkanTexture* tex = font->GetTexture();
						//ImVec2 texSize((real)tex->width, (real)tex->height);
						//ImVec2 uv0(0.0f, 0.0f);
						//ImVec2 uv1(1.0f, 1.0f);
						//ImGui::Image((void*)&tex->image, texSize, uv0, uv1);

						ImGui::NextColumn();
						bool bRebake = false;
						bRebake = ImGui::Button("Re-bake");

#if COMPILE_RENDERDOC_API
						bool bCaptureRenderDocFrame = false;
						ImGui::SameLine();
						if (ImGui::Button("Re-bake (trigger render doc capture)"))
						{
							bRebake = true;
							bCaptureRenderDocFrame = true;
						}
#endif

						if (bRebake)
						{
#if COMPILE_RENDERDOC_API
							if (bCaptureRenderDocFrame)
							{
								g_EngineInstance->RenderDocStartCapture();
							}
#endif

							if (metaData.bScreenSpace)
							{
								auto vecIterSS = std::find(fontsScreenSpace.begin(), fontsScreenSpace.end(), metaData.bitmapFont);
								CHECK(vecIterSS != fontsScreenSpace.end());

								fontsScreenSpace.erase(vecIterSS);
							}
							else
							{
								auto vecIterWS = std::find(fontsWorldSpace.begin(), fontsWorldSpace.end(), metaData.bitmapFont);
								CHECK(vecIterWS != fontsWorldSpace.end());

								fontsWorldSpace.erase(vecIterWS);
							}

							delete metaData.bitmapFont;
							metaData.bitmapFont = nullptr;
							font = nullptr;

							SetRenderedSDFFilePath(metaData);

							g_Renderer->LoadFont(metaData, true);

#if COMPILE_RENDERDOC_API
							if (bCaptureRenderDocFrame)
							{
								g_EngineInstance->RenderDocEndCapture();
							}
#endif
						}
						if (ImGui::Button("View SDF"))
						{
							std::string absDir = RelativePathToAbsolute(metaData.renderedTextureFilePath);
							Platform::OpenFileExplorer(absDir.c_str());
						}
						if (ImGui::Button("Open SDF in explorer"))
						{
							const std::string absDir = ExtractDirectoryString(RelativePathToAbsolute(metaData.renderedTextureFilePath));
							Platform::OpenFileExplorer(absDir.c_str());
						}
						ImGui::SameLine();
						if (ImGui::Button("Open font in explorer"))
						{
							const std::string absDir = ExtractDirectoryString(RelativePathToAbsolute(metaData.filePath));
							Platform::OpenFileExplorer(absDir.c_str());
						}
						bool bPreviewing = g_Renderer->previewedFont == fontPair.first;
						if (ImGui::Checkbox("Preview", &bPreviewing))
						{
							if (bPreviewing)
							{
								g_Renderer->previewedFont = fontPair.first;
							}
							else
							{
								g_Renderer->previewedFont = InvalidStringID;
							}
						}

						const bool bWasDirty = metaData.bDirty;
						if (bWasDirty)
						{
							ImVec4 buttonCol = ImGui::GetStyle().Colors[ImGuiCol_Button];
							ImVec4 darkButtonCol = ImVec4(buttonCol.x * 1.2f, buttonCol.y * 1.2f, buttonCol.z * 1.2f, buttonCol.w);
							ImGui::PushStyleColor(ImGuiCol_Button, darkButtonCol);
						}
						if (ImGui::Button(metaData.bDirty ? "Save*" : "Save"))
						{
							SerializeFontFile();
							metaData.bDirty = false;
						}
						if (bWasDirty)
						{
							ImGui::PopStyleColor();
						}
						ImGui::EndColumns();
					}
					ImGui::EndChild();
				}

				if (ImGui::Button("Re-bake all"))
				{
					LoadFonts(true);
				}
			}
			ImGui::End();
		}

		bool* bMaterialsWindowOpen = g_EngineInstance->GetUIWindowOpen(SID_PAIR("materials"));
		if (*bMaterialsWindowOpen)
		{
			if (ImGui::Begin("Materials", bMaterialsWindowOpen))
			{
				static bool bUpdateFields = true;
				static bool bMaterialSelectionChanged = true;
				static bool bScrollToSelected = true;
				const i32 MAX_NAME_LEN = 128;
				static MaterialID selectedMaterialID = 0;

				u32 matCount = (u32)g_Renderer->GetMaterialCount();
				while (selectedMaterialID < (matCount - 1) &&
					(!g_Renderer->MaterialExists(selectedMaterialID) ||
						(!bShowEditorMaterials && g_Renderer->GetMaterial(selectedMaterialID)->bEditorMaterial)))
				{
					++selectedMaterialID;
				}

				while (selectedMaterialID > 0 &&
					(!g_Renderer->MaterialExists(selectedMaterialID) ||
						(!bShowEditorMaterials && g_Renderer->GetMaterial(selectedMaterialID)->bEditorMaterial)))
				{
					--selectedMaterialID;
				}

				Material* material = g_Renderer->GetMaterial(selectedMaterialID);

				static std::string matName = "";
				static i32 selectedShaderID = 0;
				// One for each of the current material's texture slots, index into discoveredTextures
				static std::vector<i32> selectedTextureIndices;

				if (bMaterialSelectionChanged)
				{
					bMaterialSelectionChanged = false;

					bUpdateFields = true;

					matName = material->name;
					matName.resize(MAX_NAME_LEN);

					selectedTextureIndices.resize(material->textures.Count());

					i32 texIndex = 0;
					for (ShaderUniformContainer<Texture*>::TexPair& pair : material->textures)
					{
						for (u32 i = 0; i < discoveredTextures.size(); ++i)
						{
							if (pair.object->relativeFilePath.compare(discoveredTextures[i]) == 0)
							{
								selectedTextureIndices[texIndex] = i;
							}
						}
						++texIndex;
					}
				}

				if (bUpdateFields)
				{
					bUpdateFields = false;

					for (u32 texIndex = 0; texIndex < material->textures.Count(); ++texIndex)
					{
						TextureID loadedTexID = GetOrLoadTexture(discoveredTextures[selectedTextureIndices[texIndex]]);
						Texture* loadedTex = GetLoadedTexture(loadedTexID);
						if (loadedTex == nullptr)
						{
							const char* textureFilePath = discoveredTextures[selectedTextureIndices[texIndex]].c_str();
							PrintError("Failed to load texture %s\n", textureFilePath);
							// If texture failed to be found select blank texture
							selectedTextureIndices[texIndex] = 0;
							loadedTex = GetLoadedTexture(g_Renderer->blankTextureID);
						}
						material->textures.values[texIndex].object = loadedTex;

						bool bTextureIsBlankTex = (loadedTexID == g_Renderer->blankTextureID);
						if (loadedTex != nullptr && !bTextureIsBlankTex)
						{
							if (material->textures.values[texIndex].uniform->id == U_ALBEDO_SAMPLER.id)
							{
								material->enableAlbedoSampler = true;
								material->albedoTexturePath = loadedTex->relativeFilePath;
							}
							else if (material->textures.values[texIndex].uniform->id == U_EMISSIVE_SAMPLER.id)
							{
								material->enableEmissiveSampler = true;
								material->emissiveTexturePath = loadedTex->relativeFilePath;
							}
							else if (material->textures.values[texIndex].uniform->id == U_METALLIC_SAMPLER.id)
							{
								material->enableMetallicSampler = true;
								material->metallicTexturePath = loadedTex->relativeFilePath;
							}
							else if (material->textures.values[texIndex].uniform->id == U_ROUGHNESS_SAMPLER.id)
							{
								material->enableRoughnessSampler = true;
								material->roughnessTexturePath = loadedTex->relativeFilePath;
							}
							else if (material->textures.values[texIndex].uniform->id == U_NORMAL_SAMPLER.id)
							{
								material->enableNormalSampler = true;
								material->normalTexturePath = loadedTex->relativeFilePath;
							}
						}
					}

					i32 i = 0;
					for (Texture* texture : loadedTextures)
					{
						std::string texturePath = texture->relativeFilePath;
						std::string textureName = StripLeadingDirectories(texturePath);

						++i;
					}

					selectedShaderID = material->shaderID;

					g_Renderer->RenderObjectMaterialChanged(selectedMaterialID);
				}

				ImGui::PushItemWidth(160.0f);
				if (ImGui::InputText("Name", (char*)matName.data(), MAX_NAME_LEN, ImGuiInputTextFlags_EnterReturnsTrue))
				{
					// Remove trailing \0 characters
					matName = std::string(matName.c_str());
					material->name = matName;
				}
				ImGui::PopItemWidth();

				ImGui::SameLine();

				ImGui::PushItemWidth(240.0f);
				if (g_Renderer->DrawImGuiShadersDropdown(&selectedShaderID))
				{
					material->shaderID = selectedShaderID;
					bUpdateFields = true;
				}
				ImGui::PopItemWidth();

				ImGui::NewLine();

				ImGui::Columns(2);
				ImGui::SetColumnWidth(0, 240.0f);

				ImGui::ColorEdit3("Colour multiplier", &material->colourMultiplier.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);

				ImGui::ColorEdit4("Albedo", &material->constAlbedo.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);

				ImGui::ColorEdit4("Emissive", &material->constEmissive.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);

				if (material->enableMetallicSampler)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
				}
				ImGui::SliderFloat("Metallic", &material->constMetallic, 0.0f, 1.0f, "%.2f");
				if (material->enableMetallicSampler)
				{
					ImGui::PopStyleColor();
				}

				if (material->enableRoughnessSampler)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
				}
				ImGui::SliderFloat("Roughness", &material->constRoughness, 0.0f, 1.0f, "%.2f");
				if (material->enableRoughnessSampler)
				{
					ImGui::PopStyleColor();
				}

				ImGui::DragFloat("Texture scale", &material->textureScale, 0.1f);

				ImGui::Checkbox("Dynamic", &material->bDynamic);
				ImGui::Checkbox("Persistent", &material->persistent);
				ImGui::Checkbox("Visible in editor", &material->bEditorMaterial);

				ImGui::NextColumn();

				struct TextureFunctor
				{
					static bool GetTextureFileName(void* data, i32 idx, const char** out_str)
					{
						if (idx == 0)
						{
							*out_str = "NONE";
						}
						else
						{
							*out_str = static_cast<Texture**>(data)[idx - 1]->name.c_str();
						}
						return true;
					}
				};

				if (selectedTextureIndices.size() == material->textures.Count())
				{
					for (u32 texIndex = 0; texIndex < material->textures.Count(); ++texIndex)
					{
						// TODO: Pass in reference to material->textures?
						//Texture* texture = material->textures[texIndex];
#if DEBUG
						std::string texFieldName = std::string(material->textures.values[texIndex].uniform->DBG_name) + "##" + std::to_string(texIndex);
#else
						std::string texFieldName = std::to_string(material->textures.values[texIndex].uniform->id) + "##" + std::to_string(texIndex);
#endif
						bUpdateFields |= g_Renderer->DrawImGuiTextureSelector(texFieldName.c_str(), discoveredTextures, &selectedTextureIndices[texIndex]);
					}
				}

				ImGui::NewLine();

				ImGui::EndColumns();

				bMaterialSelectionChanged |= g_Renderer->DrawImGuiMaterialList(&selectedMaterialID, bShowEditorMaterials, bScrollToSelected);
				bScrollToSelected = false;

				const i32 MAX_MAT_NAME_LEN = 128;
				static std::string newMaterialName = "";

				const char* createMaterialPopupStr = "Create material##popup";
				if (ImGui::Button("Create material"))
				{
					ImGui::OpenPopup(createMaterialPopupStr);
					newMaterialName = "New Material 01";
					newMaterialName.resize(MAX_MAT_NAME_LEN);
				}

				if (ImGui::BeginPopupModal(createMaterialPopupStr, NULL, ImGuiWindowFlags_NoResize))
				{
					ImGui::Text("Name:");
					ImGui::InputText("##NameText", (char*)newMaterialName.data(), MAX_MAT_NAME_LEN);

					ImGui::Text("Shader:");
					static i32 newMatShaderIndex = 0;
					static Shader* newMatShader = nullptr;
					g_Renderer->DrawImGuiShadersDropdown(&newMatShaderIndex, &newMatShader);

					if (ImGui::Button("Cancel"))
					{
						ImGui::CloseCurrentPopup();
					}

					ImGui::SameLine();

					if (ImGui::Button("Create new material"))
					{
						// Remove trailing /0 characters
						newMaterialName = std::string(newMaterialName.c_str());

						bool bMaterialNameExists = g_Renderer->MaterialWithNameExists(newMaterialName);

						if (bMaterialNameExists)
						{
							newMaterialName = GetIncrementedPostFixedStr(newMaterialName, "new material");
						}

						MaterialCreateInfo createInfo = {};
						createInfo.name = newMaterialName;
						createInfo.shaderName = newMatShader->name;
						selectedMaterialID = g_Renderer->InitializeMaterial(&createInfo);

						bMaterialSelectionChanged = true;
						bScrollToSelected = true;

						ImGui::CloseCurrentPopup();
					}

					if (g_InputManager->GetKeyPressed(KeyCode::KEY_ESCAPE, true))
					{
						ImGui::CloseCurrentPopup();
					}

					ImGui::EndPopup();
				}

				// Only non-editor materials can be deleted
				if (material->bEditorMaterial)
				{
					ImGui::SameLine();

					ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColour);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColour);
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColour);
					if (ImGui::Button("Delete material"))
					{
						g_Renderer->ReplaceMaterialsOnObjects(selectedMaterialID, g_Renderer->GetPlaceholderMaterialID());
						g_Renderer->RemoveMaterial(selectedMaterialID);
						selectedMaterialID = 0;
						bMaterialSelectionChanged = true;
					}
					ImGui::PopStyleColor(3);
				}

				if (ImGui::Checkbox("Show editor materials", &bShowEditorMaterials))
				{
					bMaterialSelectionChanged = true;
				}
			}

			ImGui::End();
		}

		bool* bShadersWindowOpen = g_EngineInstance->GetUIWindowOpen(SID_PAIR("shaders"));
		if (*bShadersWindowOpen)
		{
			if (ImGui::Begin("Shaders", bShadersWindowOpen))
			{
				static i32 selectedShaderID = 0;
				Shader* selectedShader = nullptr;
				g_Renderer->DrawImGuiShadersList(&selectedShaderID, true, &selectedShader);

#if COMPILE_SHADER_COMPILER
				g_Renderer->DrawImGuiShaderErrors();
#endif

				ImGui::Text("%s", selectedShader->name.c_str());

				ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

				ImGui::Checkbox("Enable depth write", &selectedShader->bDepthWriteEnable);
				ImGui::SameLine();
				ImGui::Checkbox("Translucent", &selectedShader->bTranslucent);
				ImGui::Checkbox("Compute", &selectedShader->bCompute);

				ImGui::PopStyleColor();
				ImGui::PopItemFlag();

#if COMPILE_SHADER_COMPILER
				if (ImGui::Button("Recompile"))
				{
					g_Renderer->ClearShaderHash(selectedShader->name);
					g_Renderer->RecompileShaders(false);
				}
#endif

				if (ImGui::TreeNode("Specialization constants##shader-view"))
				{
					if (g_Renderer->DrawShaderSpecializationConstantImGui((ShaderID)selectedShaderID))
					{
						g_Renderer->SetSpecializationConstantDirty();
					}

					ImGui::TreePop();
				}

				ImGui::Separator();

				g_Renderer->DrawSpecializationConstantInfoImGui();
			}

			ImGui::End();
		}

		bool* bTexturesWindowOpen = g_EngineInstance->GetUIWindowOpen(SID_PAIR("textures"));
		if (*bTexturesWindowOpen)
		{
			if (ImGui::Begin("Textures", bTexturesWindowOpen))
			{
				static ImGuiTextFilter textureFilter;
				textureFilter.Draw("##texture-filter");

				ImGui::SameLine();
				if (ImGui::Button("x"))
				{
					textureFilter.Clear();
				}

				static i32 selectedTextureIndex = 0;
				if (ImGui::BeginChild("texture list", ImVec2(0.0f, 120.0f), true))
				{
					for (i32 i = 1; i < (i32)discoveredTextures.size(); ++i)
					{
						const std::string& texFileName = StripLeadingDirectories(discoveredTextures[i]);

						if (textureFilter.PassFilter(texFileName.c_str()))
						{
							bool bSelected = (i == selectedTextureIndex);
							static char texNameBuf[256];
							memset(texNameBuf, 0, 256);
							memcpy(texNameBuf, texFileName.c_str(), texFileName.size());
							Texture* loadedTex = g_ResourceManager->FindLoadedTextureWithName(texFileName);
							if (loadedTex == nullptr)
							{
								static const char* unloadedStr = " (unloaded)\0";
								static u32 unloadedStrLen = (u32)strlen(unloadedStr);
								memcpy(texNameBuf + texFileName.size(), unloadedStr, unloadedStrLen);
							}
							else
							{
								memcpy(texNameBuf + texFileName.size(), "\0", 1);
							}
							if (ImGui::Selectable(texNameBuf, &bSelected))
							{
								selectedTextureIndex = i;
							}

							if (ImGui::BeginPopupContextItem())
							{
								//if (ImGui::Button("Reload"))
								//{
								//	texture->Reload();
								//	ImGui::CloseCurrentPopup();
								//}

								ImGui::Text("TODO");

								// TODO: Add rename, duplicate, remove, etc. options here

								ImGui::EndPopup();
							}

							if (ImGui::IsItemHovered())
							{
								if (loadedTex != nullptr)
								{
									g_Renderer->DrawImGuiTexturePreviewTooltip(loadedTex);
								}
							}
						}
					}
				}
				ImGui::EndChild();

				if (ImGui::Button("Open externally"))
				{
					std::string absFilePath = RelativePathToAbsolute(discoveredTextures[selectedTextureIndex]);
					Platform::OpenFileWithDefaultApplication(absFilePath);
				}

				ImGui::SameLine();

				if (ImGui::Button("Open in explorer"))
				{
					std::string absFilePath = RelativePathToAbsolute(ExtractDirectoryString(discoveredTextures[selectedTextureIndex]));
					Platform::OpenFileExplorer(absFilePath.c_str());
				}

				// TODO: Hook up DirectoryWatcher here
				if (ImGui::Button("Refresh"))
				{
					DiscoverTextures();
				}

				ImGui::SameLine();

				if (ImGui::Button("Import Texture"))
				{
					// TODO: Not all textures are directly in this directory! CLEANUP to make more robust
					std::string relativeDirPath = TEXTURE_DIRECTORY;
					std::string absoluteDirectoryStr = RelativePathToAbsolute(relativeDirPath);
					std::string selectedAbsFilePath;
					if (Platform::OpenFileDialog("Import texture", absoluteDirectoryStr, selectedAbsFilePath))
					{
						// TODO: Copy texture into Textures directory if not already there

						const std::string fileNameAndExtension = StripLeadingDirectories(selectedAbsFilePath);
						std::string relativeFilePath = relativeDirPath + fileNameAndExtension;

						bool bTextureAlreadyImported = false;
						for (Texture* texture : loadedTextures)
						{
							if (texture->relativeFilePath.compare(relativeFilePath) == 0)
							{
								bTextureAlreadyImported = true;
								break;
							}
						}

						if (bTextureAlreadyImported)
						{
							Print("Texture with path %s already imported\n", relativeFilePath.c_str());
						}
						else
						{
							// TODO: Support other samplers
							Print("Importing texture: %s\n", relativeFilePath.c_str());
							g_Renderer->InitializeTextureFromFile(relativeFilePath, g_Renderer->GetSamplerLinearRepeat(), false, false, false);
						}

						ImGui::CloseCurrentPopup();
					}
				}
			}

			ImGui::End();
		}

		bool* bMeshesWindowOpen = g_EngineInstance->GetUIWindowOpen(SID_PAIR("meshes"));
		if (*bMeshesWindowOpen)
		{
			if (ImGui::Begin("Meshes", bMeshesWindowOpen))
			{
				static i32 selectedMeshIndex = 0;

				std::string selectedMeshRelativeFilePath = discoveredMeshes[selectedMeshIndex];
				LoadedMesh* selectedMesh = nullptr;
				auto iter = loadedMeshes.find(selectedMeshRelativeFilePath);

				if (iter != loadedMeshes.end() && selectedMesh != nullptr)
				{
					ImGui::Columns(1);

					if (ImGui::Button("Re-import"))
					{
						g_Renderer->RecreateRenderObjectsWithMesh(selectedMeshRelativeFilePath);
					}

					ImGui::SameLine();

					if (ImGui::Button("Save"))
					{
						g_SceneManager->CurrentScene()->SerializeToFile(true);
					}
				}

				static ImGuiTextFilter meshFilter;
				meshFilter.Draw("##mesh-filter");

				ImGui::SameLine();
				if (ImGui::Button("x"))
				{
					meshFilter.Clear();
				}

				DrawImGuiMeshList(&selectedMeshIndex, &meshFilter);

				static bool bShowErrorDialogue = false;
				static std::string errorMessage;
				const char* importErrorPopupID = "Mesh import failed";
				if (ImGui::Button("Import Mesh"))
				{
					// TODO: Not all models are directly in this directory! CLEANUP to make more robust
					std::string relativeImportDirPath = MESH_DIRECTORY;
					std::string absoluteImportDirectoryStr = RelativePathToAbsolute(relativeImportDirPath);
					std::string selectedAbsFilePath;
					if (Platform::OpenFileDialog("Import mesh", absoluteImportDirectoryStr, selectedAbsFilePath))
					{
						const std::string absDirectory = ExtractDirectoryString(selectedAbsFilePath);
						if (absDirectory != absoluteImportDirectoryStr)
						{
							bShowErrorDialogue = true;
							errorMessage = "Attempted to import mesh from invalid directory!"
								"\n" + absDirectory +
								"\nMeshes must be imported from " + absoluteImportDirectoryStr + "\n";
							ImGui::OpenPopup(importErrorPopupID);
						}
						else
						{
							const std::string fileNameAndExtension = StripLeadingDirectories(selectedAbsFilePath);
							std::string selectedRelativeFilePath = relativeImportDirPath + fileNameAndExtension;

							bool bMeshAlreadyImported = false;
							for (const auto& meshPair : loadedMeshes)
							{
								if (meshPair.first.compare(selectedRelativeFilePath) == 0)
								{
									bMeshAlreadyImported = true;
									break;
								}
							}

							if (bMeshAlreadyImported)
							{
								Print("Mesh with filepath %s already imported\n", selectedAbsFilePath.c_str());
							}
							else
							{
								Print("Importing mesh: %s\n", selectedAbsFilePath.c_str());

								LoadedMesh* existingMesh = nullptr;
								if (FindPreLoadedMesh(selectedRelativeFilePath, &existingMesh))
								{
									i32 j = 0;
									for (const auto& meshPair : loadedMeshes)
									{
										if (meshPair.first.compare(selectedRelativeFilePath) == 0)
										{
											selectedMeshIndex = j;
											break;
										}

										++j;
									}
								}
								else
								{
									Mesh::LoadMesh(selectedRelativeFilePath);
									DiscoverMeshes();
								}
							}

							ImGui::CloseCurrentPopup();
						}
					}
				}

				ImGui::SetNextWindowSize(ImVec2(380, 120), ImGuiCond_FirstUseEver);
				if (ImGui::BeginPopupModal(importErrorPopupID, &bShowErrorDialogue))
				{
					ImGui::TextWrapped("%s", errorMessage.c_str());
					if (ImGui::Button("Ok"))
					{
						bShowErrorDialogue = false;
						ImGui::CloseCurrentPopup();
					}

					if (g_InputManager->GetKeyPressed(KeyCode::KEY_ESCAPE, true))
					{
						ImGui::CloseCurrentPopup();
					}

					ImGui::EndPopup();
				}
			}

			ImGui::End();
		}

		bool* bPrefabsWindowOpen = g_EngineInstance->GetUIWindowOpen(SID_PAIR("prefabs"));
		if (*bPrefabsWindowOpen)
		{
			if (ImGui::Begin("Prefabs", bPrefabsWindowOpen))
			{
				static ImGuiTextFilter prefabFilter;
				prefabFilter.Draw("##prefab-filter");

				ImGui::SameLine();
				if (ImGui::Button("x"))
				{
					prefabFilter.Clear();
				}

				ImGui::SameLine();

				if (ImGui::Button("Refresh"))
				{
					DiscoverPrefabs();
				}

				static u32 selectedPrefabIndex = 0;
				if (ImGui::BeginChild("prefab list", ImVec2(0.0f, 120.0f), true))
				{
					for (u32 i = 0; i < (u32)prefabTemplates.size(); ++i)
					{
						const PrefabTemplateInfo& prefabTemplateInfo = prefabTemplates[i];
						std::string prefabNameStr = prefabTemplateInfo.templateObject->GetName() + (prefabTemplateInfo.bDirty ? "*" : "");
						const char* prefabName = prefabNameStr.c_str();

						if (prefabFilter.PassFilter(prefabName))
						{
							bool bSelected = (i == selectedPrefabIndex);
							if (ImGui::Selectable(prefabName, &bSelected))
							{
								selectedPrefabIndex = i;
							}

							// TODO: Support renaming in context menu here

							if (ImGui::IsItemActive())
							{
								if (ImGui::BeginDragDropSource())
								{
									const void* data = (void*)&prefabTemplateInfo.prefabID;
									size_t size = sizeof(PrefabID);

									ImGui::SetDragDropPayload(Editor::PrefabPayloadCStr, data, size);

									ImGui::Text("%s", prefabName);

									ImGui::EndDragDropSource();
								}
							}
						}
					}
				}

				ImGui::EndChild();

				PrefabTemplateInfo& selectedPrefabTemplate = prefabTemplates[selectedPrefabIndex];
				GameObject* prefabTemplateObject = selectedPrefabTemplate.templateObject;
				std::string prefabNameStr = prefabTemplateObject->GetName() + (selectedPrefabTemplate.bDirty ? "*" : "") + " (" + selectedPrefabTemplate.prefabID.ToString() + ")";
				ImGui::Text("%s", prefabNameStr.c_str());

				const char* contextMenuID = "game object context window";
				static std::string newObjectName = selectedPrefabTemplate.templateObject->GetName();
				const size_t maxStrLen = 256;

				if (ImGui::Button("Rename") || g_Editor->GetWantRenameActiveElement())
				{
					ImGui::OpenPopup(contextMenuID);
					g_Editor->ClearWantRenameActiveElement();
				}

				ImGui::SameLine();

				static const char* deletePrefabPopupModalName = "Are you sure?";
				static bool bShowDeletePrefabPopup = false;
				static u32 bTemplateUsagesInScene = 0;

				if (ImGui::Button("Delete"))
				{
					bTemplateUsagesInScene = g_SceneManager->CurrentScene()->NumObjectsLoadedFromPrefabID(selectedPrefabTemplate.prefabID);

					ImGui::OpenPopup(deletePrefabPopupModalName);
					bShowDeletePrefabPopup = true;
				}

				ImGui::SetNextWindowSize(ImVec2(650, 180), ImGuiCond_FirstUseEver);
				if (ImGui::BeginPopupModal(deletePrefabPopupModalName, &bShowDeletePrefabPopup))
				{
					char prefabIDStrBuff[33];
					selectedPrefabTemplate.prefabID.ToString(prefabIDStrBuff);
					char confirmStrBuff[256];
					snprintf(confirmStrBuff, ARRAY_LENGTH(confirmStrBuff),
						"Are you sure you want to delete the prefab %s? (%s)",
						selectedPrefabTemplate.fileName.c_str(), prefabIDStrBuff);
					ImGui::TextWrapped("%s", confirmStrBuff);

					if (bTemplateUsagesInScene > 0)
					{
						ImGui::PushStyleColor(ImGuiCol_Text, g_WarningTextColour);
						ImGui::TextWrapped("This prefab is used %d times in the current scene! Deleting it will destroy those instances.\n", bTemplateUsagesInScene);
						ImGui::PopStyleColor();
					}

					ImGui::PushStyleColor(ImGuiCol_Button, g_WarningButtonColour);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_WarningButtonHoveredColour);
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_WarningButtonActiveColour);
					if (ImGui::Button("Delete"))
					{
						DeletePrefabTemplate(selectedPrefabTemplate.prefabID);
						bShowDeletePrefabPopup = false;
						ImGui::CloseCurrentPopup();
					}
					ImGui::PopStyleColor(3);

					ImGui::SameLine();

					if (ImGui::Button("Cancel") || g_InputManager->GetKeyPressed(KeyCode::KEY_ESCAPE, true))
					{
						bShowDeletePrefabPopup = false;
						ImGui::CloseCurrentPopup();
					}

					ImGui::EndPopup();
				}

				ImGui::SameLine();

				if (ImGui::Button("Duplicate"))
				{
					GameObject* newTemplateObject = selectedPrefabTemplate.templateObject->CopySelf();
					newTemplateObject->m_SourcePrefabID.Clear();
					newTemplateObject->m_bIsTemplate = true;
					std::string namePrefix = newTemplateObject->GetName();

					i16 numChars;
					i32 numEndingWith = GetNumberEndingWith(namePrefix, numChars);

					std::string newName = namePrefix.substr(0, namePrefix.size() - numChars) + IntToString(numEndingWith + 1, numChars);
					while (GetPrefabID(newName.c_str()).IsValid())
					{
						++numEndingWith;
						newName = namePrefix.substr(0, namePrefix.size() - numChars) + IntToString(numEndingWith + 1, numChars);
					}

					newTemplateObject->SetName(newName);
					newTemplateObject->SaveAsPrefab();
				}

				ImGui::SameLine();

				if (ImGui::Button("Instantiate in scene"))
				{
					GameObject* newInstance = selectedPrefabTemplate.templateObject->CopySelf();
					g_SceneManager->CurrentScene()->AddRootObject(newInstance);
				}

				if (ImGui::BeginPopupContextItem(contextMenuID))
				{
					if (ImGui::IsWindowAppearing())
					{
						ImGui::SetKeyboardFocusHere();
						newObjectName = selectedPrefabTemplate.templateObject->GetName();
					}

					bool bRename = ImGui::InputText("##rename-game-object",
						(char*)newObjectName.data(),
						maxStrLen,
						ImGuiInputTextFlags_EnterReturnsTrue);

					ImGui::SameLine();

					bRename |= ImGui::Button("Rename");

					bool bInvalidName = std::string(newObjectName.c_str()).empty();

					if (bRename && !bInvalidName)
					{
						// Remove excess trailing \0 chars
						newObjectName = std::string(newObjectName.c_str());

						std::string prevName = selectedPrefabTemplate.templateObject->GetName();
						std::string prevFilePath = PREFAB_DIRECTORY + selectedPrefabTemplate.fileName;

						selectedPrefabTemplate.templateObject->SetName(newObjectName);
						if (selectedPrefabTemplate.templateObject->SaveAsPrefab())
						{
							// NOTE: At this point selectedPrefabTemplate is invalid because DiscoverPrefabs has been called
							// and thus all prefab templates have been recreated.
							if (!Platform::DeleteFile(prevFilePath))
							{
								PrintError("Failed to delete previous prefab file at %s\n", prevFilePath.c_str());
							}
						}

						ImGui::CloseCurrentPopup();
					}

					// ID
					{
						char buffer[33];
						selectedPrefabTemplate.prefabID.ToString(buffer);
						char data0Buffer[17];
						char data1Buffer[17];
						memcpy(data0Buffer, buffer, 16);
						data0Buffer[16] = 0;
						memcpy(data1Buffer, buffer + 16, 16);
						data1Buffer[16] = 0;
						ImGui::Text("GUID: %s-%s", data0Buffer, data1Buffer);

						if (ImGui::IsItemHovered())
						{
							ImGui::BeginTooltip();

							ImGui::Text("Data1, Data2: %lu, %lu", selectedPrefabTemplate.prefabID.Data1, selectedPrefabTemplate.prefabID.Data2);

							ImGui::EndTooltip();
						}

						ImGui::SameLine();

						if (ImGui::Button("Copy"))
						{
							g_Window->SetClipboardText(buffer);
						}
					}

					ImGui::EndPopup();
				}
			}

			ImGui::End();
		}

		bool* bSoundsWindowOpen = g_EngineInstance->GetUIWindowOpen(SID_PAIR("audio"));
		if (*bSoundsWindowOpen)
		{
			if (ImGui::Begin("Audio", bSoundsWindowOpen))
			{
				// TODO: Add tickbox/env var somewhere to disable this
				static bool bAutoPlay = true;
				static StringID selectedAudioFileID = InvalidStringID;

				static ImGuiTextFilter soundFilter;
				soundFilter.Draw("##sounds-filter");

				ImGui::SameLine();
				if (ImGui::Button("x"))
				{
					soundFilter.Clear();
				}

				ImGui::SameLine();

				if (ImGui::Button("Refresh"))
				{
					DiscoverAudioFiles();
				}

				static bool bValuesChanged = true;
				static StringBuilder errorStringBuilder;
				bool bLoadSound = false;

				static real windowWidth = 200.0f;
				static real windowHeight = 300.0f;
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
				ImGui::BeginChild("child-above", ImVec2(windowWidth, windowHeight), true);
				{
					if (ImGui::BeginChild("audio file list", ImVec2(ImGui::GetWindowWidth() - 4.0f, -1.0f), true))
					{
						for (auto audioFilePair = discoveredAudioFiles.begin(); audioFilePair != discoveredAudioFiles.end(); ++audioFilePair)
						{
							const char* audioFileName = audioFilePair->second.name.c_str();

							if (soundFilter.PassFilter(audioFileName))
							{
								bool bSelected = (audioFilePair->first == selectedAudioFileID);
								bool bWasInvalid = discoveredAudioFiles[audioFilePair->first].bInvalid;
								if (bWasInvalid)
								{
									ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
								}
								// TODO: Indicate when sound is playing with colour change
								if (ImGui::Selectable(audioFileName, &bSelected))
								{
									selectedAudioFileID = audioFilePair->first;
									bValuesChanged = true;
									errorStringBuilder.Clear();

									if (bAutoPlay && discoveredAudioFiles[selectedAudioFileID].sourceID == InvalidAudioSourceID)
									{
										bLoadSound = true;
									}
								}
								if (bWasInvalid)
								{
									ImGui::PopStyleColor();
								}

								if (ImGui::IsItemActive())
								{
									if (ImGui::BeginDragDropSource())
									{
										void* data = (void*)&audioFilePair->first;
										size_t size = sizeof(StringID);

										ImGui::SetDragDropPayload(Editor::AudioFileNameSIDPayloadCStr, data, size);

										ImGui::Text("%s", audioFileName);

										ImGui::EndDragDropSource();
									}
								}
							}
						}
					}

					ImGui::EndChild();
				}
				ImGui::EndChild();
				// TODO: Make button much more visible (add knurling)
				ImGui::InvisibleButton("hsplitter", ImVec2(-1, 8.0f));
				if (ImGui::IsItemActive())
				{
					windowHeight += ImGui::GetIO().MouseDelta.y;
				}
				ImGui::BeginChild("child-below", ImVec2(0, 0), true);
				{
					if (selectedAudioFileID != InvalidStringID)
					{
						AudioSourceID sourceID = discoveredAudioFiles[selectedAudioFileID].sourceID;
						if (sourceID == InvalidAudioSourceID)
						{
							if (ImGui::Button("Load"))
							{
								bLoadSound = true;
							}
						}

						if (bLoadSound)
						{
							errorStringBuilder.Clear();
							LoadAudioFile(selectedAudioFileID, &errorStringBuilder, true);
							sourceID = discoveredAudioFiles[selectedAudioFileID].sourceID;
							bValuesChanged = true;
						}

						if (sourceID != InvalidAudioSourceID)
						{
							bool bPlaySound = false;
							if (AudioManager::IsSourcePlaying(sourceID))
							{
								if (ImGui::Button("Stop"))
								{
									AudioManager::StopSource(sourceID);
								}
							}
							else
							{
								bPlaySound = ImGui::Button("Play");
							}

							if (bPlaySound || (bValuesChanged && bAutoPlay))
							{
								AudioManager::PlaySource(sourceID);
							}

							AudioManager::Source* source = AudioManager::GetSource(sourceID);
							ImGui::Text("%.2f", source->gain);

							real gain = source->gain;
							if (ImGui::SliderFloat("Gain", &gain, 0.0f, 1.0f))
							{
								AudioManager::SetSourceGain(sourceID, gain);
							}

							ImGui::Spacing();

							real pitch = source->pitch;
							if (ImGui::SliderFloat("Pitch", &pitch, 0.5f, 2.0f))
							{
								AudioManager::SetSourcePitch(sourceID, pitch);
							}

							bool bLooping = source->bLooping;
							if (ImGui::Checkbox("looping", &bLooping))
							{
								AudioManager::SetSourceLooping(sourceID, bLooping);
							}

							// TODO: Show more info like file size, mono vs. stereo, etc.

							static u32 previousSourceVersion = 0;

							u32 valsCount;
							u32 sourceVersion;
							u8* vals = AudioManager::GetSourceSamples(sourceID, valsCount, sourceVersion);

							if (sourceVersion != previousSourceVersion)
							{
								bValuesChanged = true;
								previousSourceVersion = sourceVersion;
							}

							// NOTE: 1 << 16 is ImGui's max allowed number of samples
							valsCount = glm::min(valsCount, (u32)(1 << 16));

							static real* valsFloat = nullptr;
							static u32 valsFloatLen = 0;
							//static u32 selectionStart = 0;
							//static u32 selectionLength = 0;
							static u32 zoomCenter = 0;
							static u32 zoomHalfLength = 0;

							if (valsFloat == nullptr || valsFloatLen < valsCount)
							{
								free(valsFloat);
								valsFloat = (real*)malloc(sizeof(real) * valsCount);
								CHECK_NE(valsFloat, nullptr);
								valsFloatLen = valsCount;
								bValuesChanged = true;
							}

							if (bValuesChanged)
							{
								bValuesChanged = false;

								//selectionStart = 0;
								//selectionLength = 0;
								zoomCenter = (u32)(valsCount / 2);
								zoomHalfLength = valsCount / 2;

								for (u32 i = 0; i < valsCount; ++i)
								{
									valsFloat[i] = (vals[i] / 255.0f - 0.5f) * 2.0f;
								}
							}

							ImGui::PlotConfig conf;
							conf.values.ys = valsFloat + (zoomCenter - zoomHalfLength);
							conf.values.count = zoomHalfLength * 2;
							conf.scale.min = -1.0f;
							conf.scale.max = 1.0f;
							conf.tooltip.show = true;
							conf.tooltip.format = "%g: %.2f";
							conf.grid_x.show = true;
							conf.grid_x.size = 4096;
							conf.grid_x.subticks = 64;
							conf.grid_y.show = true;
							conf.grid_y.size = 0.5f;
							conf.grid_y.subticks = 5;
							//conf.selection.show = true;
							//conf.selection.start = &selectionStart;
							//conf.selection.length = &selectionLength;
							conf.frame_size = ImVec2(ImGui::GetWindowWidth() - 4.0f, 120.0f);
							conf.line_thickness = 2.f;
							conf.overlay_colour = IM_COL32(20, 165, 20, 65);

							ImVec2 cursorStart = ImGui::GetCursorScreenPos();

							ImGui::Plot("Waveform", conf);

							if (ImGui::IsItemHovered())
							{
								real mouseXN = glm::clamp((ImGui::GetIO().MousePos.x - cursorStart.x) / (conf.frame_size.x - cursorStart.x), 0.0f, 1.0f);
								real scrollY = ImGui::GetIO().MouseWheel;

								//const u32 lastZoomHalfLength = zoomHalfLength;
								//const u32 lastZoomCenter = zoomCenter;
								real inverseZoomPercent = zoomHalfLength / (valsCount / 2.0f);
								real alpha = (real)glm::pow(inverseZoomPercent, 2.0f);
								real deltaSlow = valsCount / 150.0f;
								real deltaFast = valsCount / 10.0f;
								i32 delta = (i32)Lerp(deltaSlow, deltaFast, alpha);
								if (inverseZoomPercent < 0.1f)
								{
									delta = (i32)((real)delta * glm::clamp(glm::pow(inverseZoomPercent / 0.1f, 2.0f), 0.1f, 1.0f));
								}
								zoomHalfLength = (u32)glm::clamp((i32)zoomHalfLength - (i32)(delta * scrollY), 10, (i32)valsCount / 2);

								if (scrollY != 0.0f)
								{
									//real moveScale = scrollY < 0.0f ? -1.0f : 1.0f;
									//u32 zoomDelta = zoomHalfLength - lastZoomHalfLength;
									// [0          [0.6 0.7]     1]
									//             [0     1]
									real left = (zoomCenter - zoomHalfLength) / (real)(valsCount);
									real right = (zoomCenter + zoomHalfLength) / (real)(valsCount);
									real mouseGlobalN = Lerp(left, right, mouseXN);
									real newTargetMouseCenter;
									// TODO: Fix this
									if (mouseGlobalN > 0.5f)
									{
										newTargetMouseCenter = Lerp(mouseGlobalN, right, Saturate((mouseXN - 0.5f) / 0.5f));
									}
									else
									{
										newTargetMouseCenter = Lerp(left, mouseGlobalN, Saturate(mouseXN / 0.5f));
									}
									u32 targetZoomCenter = glm::clamp((u32)(newTargetMouseCenter * valsCount), zoomHalfLength, valsCount - zoomHalfLength);
									zoomCenter = targetZoomCenter;// (u32)Lerp((real)zoomCenter, (real)targetZoomCenter, 0.5f);
									//Print("Old center + half len, new center + half len,:\n%u\t%u\n%u\t%u\n\n", lastZoomCenter, lastZoomHalfLength, zoomCenter, zoomHalfLength);
								}
							}

							// TODO: Take into account zoom level
							real playbackPosN = AudioManager::GetSourcePlaybackPos(sourceID) / AudioManager::GetSourceLength(sourceID);

							ImDrawList* drawlist = ImGui::GetWindowDrawList();
							drawlist->AddLine(
								cursorStart + ImVec2(playbackPosN * conf.frame_size.x, 0.0f),
								cursorStart + ImVec2(playbackPosN * conf.frame_size.x, conf.frame_size.y),
								IM_COL32(20, 250, 20, 255), 2.0f);

							//ImGui::Spacing();
							//
							//conf.selection.show = false;
							//conf.values.offset = selectionStart;
							//conf.values.count = selectionLength;
							//conf.line_thickness = 2.f;
							//ImGui::Plot("Selection", conf);
						}
						else
						{
							ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
							ImGui::Text("%s", errorStringBuilder.ToCString());
							discoveredAudioFiles[selectedAudioFileID].bInvalid = true;
							ImGui::PopStyleColor();
						}
					}
				}

				ImGui::Text("Effects");

				for (AudioEffect& effect : AudioManager::s_Effects)
				{
					if (ImGui::Combo("Type", (i32*)&effect.type, AudioEffectTypeStrings, ARRAY_LENGTH(AudioEffectTypeStrings)))
					{

					}

					if (effect.type == AudioEffect::Type::EAX_REVERB)
					{
						bool bChanged = false;

						static const Pair<const char*, EFXEAXREVERBPROPERTIES> presets[] =
						{
							/* Default Presets */

							{ "Generic", EFX_REVERB_PRESET_GENERIC },
							{ "Padded cell",  EFX_REVERB_PRESET_PADDEDCELL },
							{ "Room",  EFX_REVERB_PRESET_ROOM },
							{ "Bathroom",  EFX_REVERB_PRESET_BATHROOM },
							{ "Livingroom",  EFX_REVERB_PRESET_LIVINGROOM },
							{ "Stoneroom",  EFX_REVERB_PRESET_STONEROOM },
							{ "Auditorium",  EFX_REVERB_PRESET_AUDITORIUM },
							{ "Concerthall",  EFX_REVERB_PRESET_CONCERTHALL },
							{ "Cave",  EFX_REVERB_PRESET_CAVE },
							{ "Arena",  EFX_REVERB_PRESET_ARENA },
							{ "Hangar",  EFX_REVERB_PRESET_HANGAR },
							{ "Carpted hallway",  EFX_REVERB_PRESET_CARPETEDHALLWAY },
							{ "Hallway",  EFX_REVERB_PRESET_HALLWAY },
							{ "Stone corridor",  EFX_REVERB_PRESET_STONECORRIDOR },
							{ "Alley",  EFX_REVERB_PRESET_ALLEY },
							{ "Forest",  EFX_REVERB_PRESET_FOREST },
							{ "City",  EFX_REVERB_PRESET_CITY },
							{ "Mountains",  EFX_REVERB_PRESET_MOUNTAINS },
							{ "Quarry",  EFX_REVERB_PRESET_QUARRY },
							{ "Plain",  EFX_REVERB_PRESET_PLAIN },
							{ "Parking lot",  EFX_REVERB_PRESET_PARKINGLOT },
							{ "Sewer pipe",  EFX_REVERB_PRESET_SEWERPIPE },
							{ "Underwater",  EFX_REVERB_PRESET_UNDERWATER },
							{ "Drugged",  EFX_REVERB_PRESET_DRUGGED },
							{ "Dizzy",  EFX_REVERB_PRESET_DIZZY },
							{ "Psychotic",  EFX_REVERB_PRESET_PSYCHOTIC },

							/* Castle Presets */

							{ "Small room",  EFX_REVERB_PRESET_CASTLE_SMALLROOM },
							{ "Short passage",  EFX_REVERB_PRESET_CASTLE_SHORTPASSAGE },
							{ "Medium room",  EFX_REVERB_PRESET_CASTLE_MEDIUMROOM },
							{ "Large room",  EFX_REVERB_PRESET_CASTLE_LARGEROOM },
							{ "Long passage",  EFX_REVERB_PRESET_CASTLE_LONGPASSAGE },
							{ "Castle hall",  EFX_REVERB_PRESET_CASTLE_HALL },
							{ "Castle cupboard",  EFX_REVERB_PRESET_CASTLE_CUPBOARD },
							{ "Courtyard",  EFX_REVERB_PRESET_CASTLE_COURTYARD },
							{ "Alcove",  EFX_REVERB_PRESET_CASTLE_ALCOVE },

							/* Factory Presets */

							{ "Factory Smallroom",  EFX_REVERB_PRESET_FACTORY_SMALLROOM },
							{ "Factory short passage",  EFX_REVERB_PRESET_FACTORY_SHORTPASSAGE },
							{ "Factory medium room",  EFX_REVERB_PRESET_FACTORY_MEDIUMROOM },
							{ "Factory large room",  EFX_REVERB_PRESET_FACTORY_LARGEROOM },
							{ "Factory long passage",  EFX_REVERB_PRESET_FACTORY_LONGPASSAGE },
							{ "Factory hall",  EFX_REVERB_PRESET_FACTORY_HALL },
							{ "Factory cupboard",  EFX_REVERB_PRESET_FACTORY_CUPBOARD },
							{ "Factory courtyard",  EFX_REVERB_PRESET_FACTORY_COURTYARD },
							{ "Factory alcove",  EFX_REVERB_PRESET_FACTORY_ALCOVE },

							/* Ice Palace Presets */

							{ "Ice Palace small room",  EFX_REVERB_PRESET_ICEPALACE_SMALLROOM },
							{ "Ice Palace short passage",  EFX_REVERB_PRESET_ICEPALACE_SHORTPASSAGE },
							{ "Ice Palace mediumroom",  EFX_REVERB_PRESET_ICEPALACE_MEDIUMROOM },
							{ "Ice Palace large room",  EFX_REVERB_PRESET_ICEPALACE_LARGEROOM },
							{ "Ice Palace long passage",  EFX_REVERB_PRESET_ICEPALACE_LONGPASSAGE },
							{ "Ice Palace hall",  EFX_REVERB_PRESET_ICEPALACE_HALL },
							{ "Ice Palace cupboard",  EFX_REVERB_PRESET_ICEPALACE_CUPBOARD },
							{ "Ice Palace courtyard",  EFX_REVERB_PRESET_ICEPALACE_COURTYARD },
							{ "Ice Palace alcove",  EFX_REVERB_PRESET_ICEPALACE_ALCOVE },

							/* Space Station Presets */

							{ "Space Station small room",  EFX_REVERB_PRESET_SPACESTATION_SMALLROOM },
							{ "Space Station short passage",  EFX_REVERB_PRESET_SPACESTATION_SHORTPASSAGE },
							{ "Space Station medium room",  EFX_REVERB_PRESET_SPACESTATION_MEDIUMROOM },
							{ "Space Station large room",  EFX_REVERB_PRESET_SPACESTATION_LARGEROOM },
							{ "Space Station long passage",  EFX_REVERB_PRESET_SPACESTATION_LONGPASSAGE },
							{ "Space Station hall",  EFX_REVERB_PRESET_SPACESTATION_HALL },
							{ "Space Station cupboard",  EFX_REVERB_PRESET_SPACESTATION_CUPBOARD },
							{ "Space Station alcove",  EFX_REVERB_PRESET_SPACESTATION_ALCOVE },

							/* Wooden Galleon Presets */

							{ "Wooden small room",  EFX_REVERB_PRESET_WOODEN_SMALLROOM },
							{ "Wooden short passage",  EFX_REVERB_PRESET_WOODEN_SHORTPASSAGE },
							{ "Wooden medium room",  EFX_REVERB_PRESET_WOODEN_MEDIUMROOM },
							{ "Wooden large room",  EFX_REVERB_PRESET_WOODEN_LARGEROOM },
							{ "Wooden long passage",  EFX_REVERB_PRESET_WOODEN_LONGPASSAGE },
							{ "Wooden hall",  EFX_REVERB_PRESET_WOODEN_HALL },
							{ "Wooden cupboard",  EFX_REVERB_PRESET_WOODEN_CUPBOARD },
							{ "Wooden courtyard",  EFX_REVERB_PRESET_WOODEN_COURTYARD },
							{ "Wooden alcove",  EFX_REVERB_PRESET_WOODEN_ALCOVE },

							/* Sports Presets */

							{ "Sport empty stadium",  EFX_REVERB_PRESET_SPORT_EMPTYSTADIUM },
							{ "Sport squash court",  EFX_REVERB_PRESET_SPORT_SQUASHCOURT },
							{ "Sport small swimming pool",  EFX_REVERB_PRESET_SPORT_SMALLSWIMMINGPOOL },
							{ "Sport large swimming pool",  EFX_REVERB_PRESET_SPORT_LARGESWIMMINGPOOL },
							{ "Sport gymnasium",  EFX_REVERB_PRESET_SPORT_GYMNASIUM },
							{ "Sport full stadium",  EFX_REVERB_PRESET_SPORT_FULLSTADIUM },
							{ "Sport staduym tannoy",  EFX_REVERB_PRESET_SPORT_STADIUMTANNOY },

							/* Prefab Presets */

							{ "Prefab workshop",  EFX_REVERB_PRESET_PREFAB_WORKSHOP },
							{ "Prefab school room",  EFX_REVERB_PRESET_PREFAB_SCHOOLROOM },
							{ "Prefab practice room",  EFX_REVERB_PRESET_PREFAB_PRACTISEROOM },
							{ "Prefab outhouse",  EFX_REVERB_PRESET_PREFAB_OUTHOUSE },
							{ "Prefab caravan",  EFX_REVERB_PRESET_PREFAB_CARAVAN },

							/* Dome and Pipe Presets */

							{ "Dome tomb",  EFX_REVERB_PRESET_DOME_TOMB },
							{ "Dome pipe small",  EFX_REVERB_PRESET_PIPE_SMALL },
							{ "Dome Saint Pauls",  EFX_REVERB_PRESET_DOME_SAINTPAULS },
							{ "Pipe long thin",  EFX_REVERB_PRESET_PIPE_LONGTHIN },
							{ "Pipe large",  EFX_REVERB_PRESET_PIPE_LARGE },
							{ "Pipe resonant",  EFX_REVERB_PRESET_PIPE_RESONANT },

							/* Outdoors Presets */

							{ "Outdoors backyard",  EFX_REVERB_PRESET_OUTDOORS_BACKYARD },
							{ "Outdoors rolling plains",  EFX_REVERB_PRESET_OUTDOORS_ROLLINGPLAINS },
							{ "Outdoors deep canyon",  EFX_REVERB_PRESET_OUTDOORS_DEEPCANYON },
							{ "Outdoors creek",  EFX_REVERB_PRESET_OUTDOORS_CREEK },
							{ "Outdoors valley",  EFX_REVERB_PRESET_OUTDOORS_VALLEY },

							/* Mood Presets */

							{ "Mood heaven",  EFX_REVERB_PRESET_MOOD_HEAVEN },
							{ "Mood hell",  EFX_REVERB_PRESET_MOOD_HELL },
							{ "Mood memory",  EFX_REVERB_PRESET_MOOD_MEMORY },

							/* Driving Presets */

							{ "Driving commentator",  EFX_REVERB_PRESET_DRIVING_COMMENTATOR },
							{ "Driving pit garage",  EFX_REVERB_PRESET_DRIVING_PITGARAGE },
							{ "Driving in car racer",  EFX_REVERB_PRESET_DRIVING_INCAR_RACER },
							{ "Driving in car sports",  EFX_REVERB_PRESET_DRIVING_INCAR_SPORTS },
							{ "Driving in car luxury",  EFX_REVERB_PRESET_DRIVING_INCAR_LUXURY },
							{ "Driving full grand stand",  EFX_REVERB_PRESET_DRIVING_FULLGRANDSTAND },
							{ "Driving empty grand stand",  EFX_REVERB_PRESET_DRIVING_EMPTYGRANDSTAND },
							{ "Driving driving tunnel",  EFX_REVERB_PRESET_DRIVING_TUNNEL },

							/* City Presets */

							{ "City streets",  EFX_REVERB_PRESET_CITY_STREETS },
							{ "City subway",  EFX_REVERB_PRESET_CITY_SUBWAY },
							{ "City museum",  EFX_REVERB_PRESET_CITY_MUSEUM },
							{ "City library",  EFX_REVERB_PRESET_CITY_LIBRARY },
							{ "City underpass",  EFX_REVERB_PRESET_CITY_UNDERPASS },
							{ "City abandoned",  EFX_REVERB_PRESET_CITY_ABANDONED },

							/* Misc. Presets */

							{ "Dusty room",  EFX_REVERB_PRESET_DUSTYROOM },
							{ "Chapel",  EFX_REVERB_PRESET_CHAPEL },
							{ "Small water room",  EFX_REVERB_PRESET_SMALLWATERROOM },
						};

						if (ImGui::BeginCombo("Preset", effect.presetIndex != -1 ? presets[effect.presetIndex].first : ""))
						{
							for (i32 i = 0; i < (i32)ARRAY_LENGTH(presets); ++i)
							{
								if (ImGui::Selectable(presets[i].first))
								{
									effect.presetIndex = i;
									effect.reverbProperties = presets[i].second;
									bChanged = true;
								}
							}

							ImGui::EndCombo();
						}

						real speed = 0.02f;

						bChanged = ImGui::DragFloat("Density", &effect.reverbProperties.flDensity, speed, 0.0f, 1.0f) || bChanged;
						bChanged = ImGui::DragFloat("Diffusion", &effect.reverbProperties.flDiffusion, speed, 0.0f, 1.0f) || bChanged;
						bChanged = ImGui::DragFloat("Gain", &effect.reverbProperties.flGain, speed, 0.0f, 1.0f) || bChanged;
						bChanged = ImGui::DragFloat("Gain HF", &effect.reverbProperties.flGainHF, speed, 0.0f, 1.0f) || bChanged;
						bChanged = ImGui::DragFloat("Gain LF", &effect.reverbProperties.flGainLF, speed, 0.0f, 1.0f) || bChanged;
						bChanged = ImGui::DragFloat("Decay time", &effect.reverbProperties.flDecayTime, speed, 0.0f, 1.0f) || bChanged;
						bChanged = ImGui::DragFloat("Decay HF Ratio", &effect.reverbProperties.flDecayHFRatio, speed, 0.1f, 2.0f) || bChanged;
						bChanged = ImGui::DragFloat("Decay LF Ratio", &effect.reverbProperties.flDecayLFRatio, speed, 0.1f, 2.0f) || bChanged;
						bChanged = ImGui::DragFloat("Reflections gain", &effect.reverbProperties.flReflectionsGain, speed, 0.0f, 1.0f) || bChanged;
						bChanged = ImGui::DragFloat("Reflections delay", &effect.reverbProperties.flReflectionsDelay, 0.001f, 0.0f, 0.3f) || bChanged;
						bChanged = ImGui::DragFloat3("Reflections pan", effect.reverbProperties.flReflectionsPan, speed, 0.0f, 1.0f) || bChanged;
						bChanged = ImGui::DragFloat("Late reverb gain", &effect.reverbProperties.flLateReverbGain, speed, 0.0f, 5.0f) || bChanged;
						bChanged = ImGui::DragFloat("Late reverb delay", &effect.reverbProperties.flLateReverbDelay, 0.001f, 0.0f, 0.1f) || bChanged;
						bChanged = ImGui::DragFloat3("Late reverb pan", effect.reverbProperties.flLateReverbPan, speed, -1.0f, 1.0f) || bChanged;
						bChanged = ImGui::DragFloat("Echo time", &effect.reverbProperties.flEchoTime, 0.0005f, 0.1f, 0.25f) || bChanged;
						bChanged = ImGui::DragFloat("Echo depth", &effect.reverbProperties.flEchoDepth, speed, 0.0f, 1.0f) || bChanged;
						bChanged = ImGui::DragFloat("Modulation time", &effect.reverbProperties.flModulationTime, speed, 0.25f, 1.0f) || bChanged;
						bChanged = ImGui::DragFloat("Modulation depth", &effect.reverbProperties.flModulationDepth, speed, 0.0f, 1.0f) || bChanged;
						bChanged = ImGui::DragFloat("Air absorption gain HF", &effect.reverbProperties.flAirAbsorptionGainHF, 0.0001f, 0.9920f, 0.9943f) || bChanged;
						bChanged = ImGui::DragFloat("HF Reference", &effect.reverbProperties.flHFReference, 10.0f, 0.0f, 10000.0f) || bChanged;
						bChanged = ImGui::DragFloat("LF Reference", &effect.reverbProperties.flLFReference, 5.0f, 0.0f, 5000.0f) || bChanged;
						bChanged = ImGui::DragFloat("Room Rolloff Factor", &effect.reverbProperties.flRoomRolloffFactor, speed, 0.0f, 1.0f) || bChanged;
						bChanged = ImGui::DragInt("Decay HF Limit", &effect.reverbProperties.iDecayHFLimit, speed, 0, 1) || bChanged;

						if (bChanged)
						{
							AudioManager::SetupReverbEffect(&effect.reverbProperties, effect.effectID);
							AudioManager::UpdateReverbEffect(AudioManager::SLOT_DEFAULT_3D, (i32)effect.effectID);
						}
					}
				}

				ImGui::EndChild();
				ImGui::PopStyleVar();
			}

			ImGui::End();
		}
	}
} // namespace flex
