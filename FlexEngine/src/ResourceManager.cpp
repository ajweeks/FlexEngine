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

		DiscoverTextures();
		DiscoverAudioFiles();
		ParseDebugOverlayNamesFile();

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
	}

	void ResourceManager::Destroy()
	{
		delete m_AudioDirectoryWatcher;
		m_AudioDirectoryWatcher = nullptr;

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

		for (PrefabTemplatePair& prefabTemplatePair : prefabTemplates)
		{
			prefabTemplatePair.templateObject->Destroy();
			delete prefabTemplatePair.templateObject;
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
		for (PrefabTemplatePair& prefabTemplatePair : prefabTemplates)
		{
			prefabTemplatePair.templateObject->Destroy();
			delete prefabTemplatePair.templateObject;
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

	LoadedMesh* ResourceManager::FindOrLoadMesh(const std::string& relativeFilePath)
	{
		LoadedMesh* result = nullptr;
		if (!FindPreLoadedMesh(relativeFilePath, &result))
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

	void ResourceManager::DiscoverMeshes()
	{
		std::vector<std::string> filePaths;
		if (Platform::FindFilesInDirectory(MESH_DIRECTORY, filePaths, "*"))
		{
			for (const std::string& filePath : filePaths)
			{
				std::string fileName = StripLeadingDirectories(filePath);
				if (MeshFileNameConforms(filePath) && !Contains(discoveredMeshes, fileName))
				{
					// TODO: Support storing meshes in child directories
					discoveredMeshes.push_back(fileName);
				}
			}
		}
	}

	void ResourceManager::DiscoverPrefabs()
	{
		for (PrefabTemplatePair& prefabTemplatePair : prefabTemplates)
		{
			prefabTemplatePair.templateObject->Destroy();
			delete prefabTemplatePair.templateObject;
		}
		prefabTemplates.clear();

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

				JSONObject prefabObject;
				if (JSONParser::ParseFromFile(foundFilePath, prefabObject))
				{
					const std::string fileName = StripLeadingDirectories(foundFilePath);

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

					JSONObject prefabRootObject = prefabObject.GetObject("root");

					GameObject* prefabTemplate = GameObject::CreateObjectFromJSON(prefabRootObject, nullptr, sceneVersion, true);

					prefabTemplates.emplace_back(prefabTemplate, prefabID, fileName, false);
				}
				else
				{
					PrintError("Failed to parse prefab file: %s, error: %s\n", foundFilePath.c_str(), JSONParser::GetErrorString());
					return;
				}
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
		i32 unchangedCount = 0;
		i32 modifiedCount = 0;
		i32 addedCount = 0;
		i32 removedCount = 0;
		i32 failedCount = 0;

		StringBuilder errorStringBuilder;

		std::vector<std::string> foundFiles;
		if (Platform::FindFilesInDirectory(SFX_DIRECTORY, foundFiles, ".wav"))
		{
			for (std::string& foundFilePath : foundFiles)
			{
				std::string relativeFilePath = foundFilePath.substr(strlen(SFX_DIRECTORY));
				StringID stringID = SID(relativeFilePath.c_str());
				AudioFileMetaData metaData(relativeFilePath);
				if (Platform::GetFileModifcationTime(foundFilePath.c_str(), metaData.fileModifiedDate))
				{
					auto iter = discoveredAudioFiles.find(stringID);
					if (iter != discoveredAudioFiles.end())
					{
						// Existing file
						if (iter->second.fileModifiedDate != metaData.fileModifiedDate)
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
									++failedCount;
									continue;
								}
							}
						}
						else
						{
							++unchangedCount;
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
				else
				{
					PrintError("Failed to get file modification time for %s\n", relativeFilePath.c_str());
					newDiscoveredAudioFiles.emplace(stringID, metaData);
				}
			}
		}
		else
		{
			PrintError("Failed to find sound files in \"" SFX_DIRECTORY "\"!\n");
			return;
		}

		removedCount = (i32)discoveredAudioFiles.size() - (i32)(newDiscoveredAudioFiles.size() - addedCount);

		discoveredAudioFiles.clear();
		discoveredAudioFiles = newDiscoveredAudioFiles;

		if (g_bEnableLogging_Loading)
		{
			Print("unchanged: %d, mod: %d, add: %d, removed: %d, failed: %d\n", unchangedCount, modifiedCount, addedCount, removedCount, failedCount);

			Print("Discovered %u audio files\n", (u32)discoveredAudioFiles.size());
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
			icons.clear();

			std::vector<std::string> foundFiles;
			if (Platform::FindFilesInDirectory(ICON_DIRECTORY, foundFiles, s_SupportedTextureFormats, ARRAY_LENGTH(s_SupportedTextureFormats)))
			{
				for (const std::string& foundFilePath : foundFiles)
				{
					std::string trimmedFileName = RelativePathToAbsolute(foundFilePath);
					trimmedFileName = StripLeadingDirectories(StripFileType(foundFilePath));
					if (EndsWith(trimmedFileName, "-icon-256"))
					{
						trimmedFileName = RemoveEndIfPresent(trimmedFileName, "-icon-256");
					}
					trimmedFileName = Replace(trimmedFileName, '-', ' ');
					StringID gameObjectTypeID = Hash(trimmedFileName.c_str());
					icons.emplace_back(Pair<StringID, Pair<std::string, TextureID>>{ gameObjectTypeID, { foundFilePath, InvalidTextureID } });
				}
			}
		}
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

	void ResourceManager::ParseMaterialsFile()
	{
		PROFILE_AUTO("ResourceManager ParseMaterialsFile");

		parsedMaterialInfos.clear();

		if (FileExists(MATERIALS_FILE_LOCATION))
		{
			if (g_bEnableLogging_Loading)
			{
				const std::string cleanedFilePath = StripLeadingDirectories(MATERIALS_FILE_LOCATION);
				Print("Parsing materials file at %s\n", cleanedFilePath.c_str());
			}

			JSONObject obj;
			if (JSONParser::ParseFromFile(MATERIALS_FILE_LOCATION, obj))
			{
				i32 fileVersion = obj.GetInt("version");

				std::vector<JSONObject> materialObjects = obj.GetObjectArray("materials");
				for (const JSONObject& materialObject : materialObjects)
				{
					MaterialCreateInfo matCreateInfo = {};
					Material::ParseJSONObject(materialObject, matCreateInfo, fileVersion);

					parsedMaterialInfos.push_back(matCreateInfo);
				}
			}
			else
			{
				PrintError("Failed to parse materials file: %s\n\terror: %s\n", MATERIALS_FILE_LOCATION, JSONParser::GetErrorString());
				return;
			}
		}
		else
		{
			PrintError("Failed to parse materials file at %s\n", MATERIALS_FILE_LOCATION);
			return;
		}

		if (g_bEnableLogging_Loading)
		{
			Print("Parsed %u materials\n", (u32)parsedMaterialInfos.size());
		}
	}

	bool ResourceManager::SerializeMaterialFile() const
	{
		JSONObject materialsObj = {};

		materialsObj.fields.emplace_back("version", JSONValue(BaseScene::LATEST_MATERIALS_FILE_VERSION));

		// Overwrite all materials in current scene in case any values were tweaked
		std::vector<JSONObject> materialJSONObjects = g_Renderer->SerializeAllMaterialsToJSON();

		materialsObj.fields.emplace_back("materials", JSONValue(materialJSONObjects));

		std::string fileContents = materialsObj.ToString();

		const std::string fileName = StripLeadingDirectories(MATERIALS_FILE_LOCATION);
		if (WriteFile(MATERIALS_FILE_LOCATION, fileContents, false))
		{
			Print("Serialized materials file to: %s\n", fileName.c_str());
		}
		else
		{
			PrintWarn("Failed to serialize materials file to: %s\n", fileName.c_str());
			return false;
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

	void ResourceManager::ParseMeshJSON(i32 sceneFileVersion, GameObject* parent, const JSONObject& meshObj, const std::vector<MaterialID>& materialIDs)
	{
		bool bCreateRenderObject = !parent->IsTemplate();

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

	void ResourceManager::SetRenderedSDFFilePath(FontMetaData& metaData)
	{
		static const std::string DPIStr = FloatToString(g_Monitor->DPI.x, 0) + "DPI";

		metaData.renderedTextureFilePath = StripFileType(StripLeadingDirectories(metaData.filePath));
		metaData.renderedTextureFilePath += "-" + IntToString(metaData.size, 2) + "-" + DPIStr + m_FontImageExtension;
		metaData.renderedTextureFilePath = FONT_SDF_DIRECTORY + metaData.renderedTextureFilePath;
	}

	bool ResourceManager::LoadFontMetrics(const std::vector<char>& fileMemory,
		FT_Library& ft,
		FontMetaData& metaData,
		std::map<i32, FontMetric*>* outCharacters,
		std::array<glm::vec2i, 4>* outMaxPositions,
		FT_Face* outFace)
	{
		assert(metaData.bitmapFont == nullptr);

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

		TextureID texID = g_Renderer->InitializeTextureFromFile(textureFilePath, false, false, false);
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

	TextureID ResourceManager::GetOrLoadIcon(StringID gameObjectTypeID)
	{
		for (Pair<StringID, Pair<std::string, TextureID>>& pair : icons)
		{
			if (pair.first == gameObjectTypeID)
			{
				if (pair.second.second == InvalidTextureID)
				{
					pair.second.second = GetOrLoadTexture(pair.second.first);
				}
				return pair.second.second;
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
		for (const PrefabTemplatePair& prefabTemplatePair : prefabTemplates)
		{
			if (StrCmpCaseInsensitive(prefabTemplatePair.templateObject->GetName().c_str(), prefabName) == 0)
			{
				return prefabTemplatePair.templateObject;
			}
		}

		return nullptr;
	}

	PrefabID ResourceManager::GetPrefabID(const char* prefabName) const
	{
		for (const PrefabTemplatePair& prefabTemplatePair : prefabTemplates)
		{
			if (StrCmpCaseInsensitive(prefabTemplatePair.templateObject->GetName().c_str(), prefabName) == 0)
			{
				return prefabTemplatePair.prefabID;
			}
		}

		return InvalidPrefabID;
	}

	GameObject* ResourceManager::GetPrefabTemplate(const PrefabID& prefabID) const
	{
		// TODO: Use map
		for (const PrefabTemplatePair& prefabTemplatePair : prefabTemplates)
		{
			if (prefabTemplatePair.prefabID == prefabID)
			{
				return prefabTemplatePair.templateObject;
			}
		}

		return nullptr;
	}

	std::string ResourceManager::GetPrefabFileName(const PrefabID& prefabID) const
	{
		for (const PrefabTemplatePair& prefabTemplatePair : prefabTemplates)
		{
			if (prefabTemplatePair.prefabID == prefabID)
			{
				return prefabTemplatePair.fileName;
			}
		}

		return "";
	}

	bool ResourceManager::IsPrefabDirty(const PrefabID& prefabID) const
	{
		for (const PrefabTemplatePair& prefabTemplatePair : prefabTemplates)
		{
			if (prefabTemplatePair.prefabID == prefabID)
			{
				return prefabTemplatePair.bDirty;
			}
		}

		return true;
	}

	void ResourceManager::SetPrefabDirty(const PrefabID& prefabID)
	{
		for (PrefabTemplatePair& prefabTemplatePair : prefabTemplates)
		{
			if (prefabTemplatePair.prefabID == prefabID)
			{
				prefabTemplatePair.bDirty = true;
			}
		}
	}

	void ResourceManager::SetAllPrefabsDirty(bool bDirty)
	{
		for (PrefabTemplatePair& prefabTemplatePair : prefabTemplates)
		{
			prefabTemplatePair.bDirty = bDirty;
		}
	}

	void ResourceManager::UpdatePrefabData(GameObject* prefabTemplate, const PrefabID& prefabID)
	{
		for (PrefabTemplatePair& prefabTemplatePair : prefabTemplates)
		{
			if (prefabID == prefabTemplatePair.prefabID)
			{
				prefabTemplatePair.templateObject->Destroy();
				delete prefabTemplatePair.templateObject;

				prefabTemplatePair.templateObject = prefabTemplate;

				WritePrefabToDisk(prefabTemplatePair, prefabID);

				g_SceneManager->CurrentScene()->OnPrefabChanged(prefabID);

				return;
			}
		}

		std::string prefabIDStr = prefabID.ToString();
		std::string prefabName = prefabTemplate->GetName();
		PrintError("Attempted to update prefab template but no previous prefabs with PrefabID %s exist (name: %s)\n", prefabIDStr.c_str(), prefabName.c_str());
	}

	PrefabID ResourceManager::AddNewPrefab(GameObject* prefabTemplate, const char* fileName /* = nullptr */)
	{
		PrefabID newID = Platform::GenerateGUID();

		std::string fileNameStr;

		if (fileName != nullptr)
		{
			fileNameStr = std::string(fileName);
		}
		else
		{
			fileNameStr = prefabTemplate->GetName() + ".json";
		}

		PrefabTemplatePair prefabTemplatePair = { prefabTemplate, newID, fileNameStr, false };

		prefabTemplates.emplace_back(prefabTemplatePair);

		WritePrefabToDisk(prefabTemplatePair, newID);

		return newID;
	}

	bool ResourceManager::IsPrefabIDValid(const PrefabID& prefabID)
	{
		GameObject* prefabTemplate = GetPrefabTemplate(prefabID);
		return prefabTemplate != nullptr;
	}

	void ResourceManager::RemovePrefabTemplate(const PrefabID& prefabID)
	{
		for (auto iter = prefabTemplates.begin(); iter != prefabTemplates.end(); ++iter)
		{
			if (iter->prefabID == prefabID)
			{
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

	AudioSourceID ResourceManager::GetAudioID(StringID audioFileSID)
	{
		return discoveredAudioFiles[audioFileSID].sourceID;
	}

	AudioSourceID ResourceManager::GetOrLoadAudioID(StringID audioFileSID)
	{
		if (discoveredAudioFiles[audioFileSID].sourceID == InvalidAudioSourceID)
		{
			LoadAudioFile(audioFileSID, nullptr);
		}

		return discoveredAudioFiles[audioFileSID].sourceID;
	}

	void ResourceManager::LoadAudioFile(StringID audioFileSID, StringBuilder* errorStringBuilder)
	{
		std::string filePath = SFX_DIRECTORY + discoveredAudioFiles[audioFileSID].name;
		discoveredAudioFiles[audioFileSID].sourceID = AudioManager::AddAudioSource(filePath, errorStringBuilder);
		discoveredAudioFiles[audioFileSID].bInvalid = (discoveredAudioFiles[audioFileSID].sourceID == InvalidAudioSourceID);
	}

	u32 ResourceManager::GetMaxStackSize(const PrefabID& prefabID)
	{
		GameObject* templateObject = GetPrefabTemplate(prefabID);
		auto iter = m_NonDefaultStackSizes.find(templateObject->GetTypeID());
		if (iter != m_NonDefaultStackSizes.end())
		{
			return iter->second;
		}
		return DEFAULT_MAX_STACK_SIZE;
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

	void ResourceManager::WritePrefabToDisk(PrefabTemplatePair& prefabTemplatePair, const PrefabID& prefabID)
	{
		std::string path = RelativePathToAbsolute(PREFAB_DIRECTORY + prefabTemplatePair.fileName);

		JSONObject prefabJSON = {};
		prefabJSON.fields.emplace_back("version", JSONValue(BaseScene::LATETST_PREFAB_FILE_VERSION));
		// Added in prefab v3
		prefabJSON.fields.emplace_back("scene version", JSONValue(BaseScene::LATEST_SCENE_FILE_VERSION));

		// Added in prefab v2
		std::string prefabIDStr = prefabID.ToString();
		prefabJSON.fields.emplace_back("prefab id", JSONValue(prefabIDStr));

		JSONObject objectSource = prefabTemplatePair.templateObject->Serialize(g_SceneManager->CurrentScene(), true, true);
		prefabJSON.fields.emplace_back("root", JSONValue(objectSource));

		std::string fileContents = prefabJSON.ToString();
		if (WriteFile(path, fileContents, false))
		{
			prefabTemplatePair.bDirty = false;
		}
		else
		{
			prefabTemplatePair.bDirty = true;
			std::string prefabName = prefabTemplatePair.templateObject->GetName();
			PrintError("Failed to write prefab to disk (%s, %s\n)", prefabName.c_str(), path.c_str());
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
		for (PrefabTemplatePair& pair : prefabTemplates)
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

				static char maxStackSizeBuff[6];
				static bool bMaxStackSizeBuffInitialized = false;
				if (!bMaxStackSizeBuffInitialized)
				{
					snprintf(maxStackSizeBuff, ARRAY_LENGTH(maxStackSizeBuff), "x%u", maxStackSize);
					bMaxStackSizeBuffInitialized = true;
				}

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
						if (ImGui::Button("Re-bake"))
						{
							if (metaData.bScreenSpace)
							{
								auto vecIterSS = std::find(fontsScreenSpace.begin(), fontsScreenSpace.end(), metaData.bitmapFont);
								assert(vecIterSS != fontsScreenSpace.end());

								fontsScreenSpace.erase(vecIterSS);
							}
							else
							{
								auto vecIterWS = std::find(fontsWorldSpace.begin(), fontsWorldSpace.end(), metaData.bitmapFont);
								assert(vecIterWS != fontsWorldSpace.end());

								fontsWorldSpace.erase(vecIterWS);
							}

							delete metaData.bitmapFont;
							metaData.bitmapFont = nullptr;
							font = nullptr;

							SetRenderedSDFFilePath(metaData);

							g_Renderer->LoadFont(metaData, true);
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
					ImGui::PopStyleColor();
					ImGui::PopStyleColor();
					ImGui::PopStyleColor();
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
							Print("Importing texture: %s\n", relativeFilePath.c_str());
							g_Renderer->InitializeTextureFromFile(relativeFilePath, false, false, false);
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
						const PrefabTemplatePair& prefabTemplatePair = prefabTemplates[i];
						std::string prefabNameStr = prefabTemplatePair.templateObject->GetName() + (prefabTemplatePair.bDirty ? "*" : "");
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
									const void* data = (void*)&prefabTemplatePair.prefabID;
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

				PrefabTemplatePair& selectedPrefabTemplate = prefabTemplates[selectedPrefabIndex];
				GameObject* prefabTemplateObject = selectedPrefabTemplate.templateObject;
				std::string prefabNameStr = prefabTemplateObject->GetName() + (selectedPrefabTemplate.bDirty ? "*" : "");
				ImGui::Text("%s", prefabNameStr.c_str());

				if (ImGui::Button("Delete"))
				{
					RemovePrefabTemplate(selectedPrefabTemplate.prefabID);
				}

				if (ImGui::Button("Duplicate"))
				{
					// TODO: Implement
					PrintError("Unimplemented!\n");
				}
			}

			ImGui::End();
		}

		bool* bSoundsWindowOpen = g_EngineInstance->GetUIWindowOpen(SID_PAIR("sounds"));
		if (*bSoundsWindowOpen)
		{
			if (ImGui::Begin("Sound clips", bSoundsWindowOpen))
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
							LoadAudioFile(selectedAudioFileID, &errorStringBuilder);
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
								assert(valsFloat != nullptr);
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
				ImGui::EndChild();
				ImGui::PopStyleVar();
			}

			ImGui::End();
		}
	}
} // namespace flex
