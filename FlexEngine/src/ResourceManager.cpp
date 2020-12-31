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
#include "Editor.hpp"
#include "InputManager.hpp"
#include "JSONParser.hpp"
#include "Platform/Platform.hpp"
#include "Profiler.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/LoadedMesh.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Window/Monitor.hpp"
#include "Window/Window.hpp"

namespace flex
{
	ResourceManager::ResourceManager() :
		m_FontsFilePathAbs(RelativePathToAbsolute(FONT_DEFINITION_LOCATION))
	{
	}

	ResourceManager::~ResourceManager()
	{
	}

	void ResourceManager::Initialize()
	{
	}

	void ResourceManager::Destroy()
	{
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
		parsedPrefabInfos.clear();

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
					JSONObject prefabRootObject = prefabObject.GetObject("root");

					PrefabInfo prefabInfo = ParsePrefabInfoFromJSON(prefabRootObject);

					i32 version = prefabObject.GetInt("version");

					if (version >= 2)
					{
						// Added in prefab v2
						std::string idStr = prefabObject.GetString("prefab id");
						prefabInfo.ID = GUID::FromString(idStr);
					}
					else
					{
						prefabInfo.ID = Platform::GenerateGUID();
					}

					parsedPrefabInfos.push_back(prefabInfo);
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
			PrintError("Failed to find files in \"" PREFAB_DIRECTORY "\"!\n");
			return;
		}

		if (g_bEnableLogging_Loading)
		{
			Print("Parsed %u prefabs\n", (u32)parsedPrefabInfos.size());
		}
	}

	PrefabInfo ResourceManager::ParsePrefabInfoFromJSON(const JSONObject& prefabRootObj)
	{
		PrefabInfo prefabInfo = {};

		prefabInfo.name = prefabRootObj.GetString("name");
		std::string typeName = prefabRootObj.GetString("type");
		prefabInfo.typeID = Hash(typeName.c_str());

		std::string prefabType = prefabRootObj.GetString("prefab type");

		if (!prefabRootObj.SetBoolChecked("visible", prefabInfo.bVisible))
		{
			prefabInfo.bVisible = true;
		}

		JSONObject transformObj;
		if (prefabRootObj.SetObjectChecked("transform", transformObj))
		{
			prefabInfo.transform = Transform::ParseJSON(transformObj);
		}

		std::vector<JSONObject> children;
		if (prefabRootObj.SetObjectArrayChecked("children", children))
		{
			prefabInfo.children.reserve(children.size());
			for (const JSONObject& childObj : children)
			{
				PrefabInfo childInfo = ParsePrefabInfoFromJSON(childObj);
				prefabInfo.children.push_back(childInfo);
			}
		}

		prefabInfo.sourceData = prefabRootObj;
		prefabInfo.bDirty = false;

		return prefabInfo;
	}

	void ResourceManager::ParseFontFile()
	{
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
				if (fontSettings.SetObjectArrayChecked("fonts", fontObjs))
				{
					for (const JSONObject& fontObj : fontObjs)
					{
						FontMetaData metaData = {};

						fontObj.SetStringChecked("name", metaData.name);
						std::string fileName;
						fontObj.SetStringChecked("file path", fileName);
						metaData.size = (i16)fontObj.GetInt("size");
						fontObj.SetBoolChecked("screen space", metaData.bScreenSpace);
						fontObj.SetFloatChecked("threshold", metaData.threshold);
						fontObj.SetFloatChecked("shadow opacity", metaData.shadowOpacity);
						fontObj.SetVec2Checked("shadow offset", metaData.shadowOffset);
						fontObj.SetFloatChecked("soften", metaData.soften);

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
							PrintError("Hash collision detected in font meta data for %s : %llu\n", fontName.c_str(), fontNameID);
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

		std::string fileContents = fontSettings.Print(0);

		if (!WriteFile(m_FontsFilePathAbs, fileContents, false))
		{
			PrintError("Failed to write font file to %s\n", m_FontsFilePathAbs.c_str());
		}
	}

	void ResourceManager::ParseMaterialsFile()
	{
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
				std::vector<JSONObject> materialObjects = obj.GetObjectArray("materials");
				for (const JSONObject& materialObject : materialObjects)
				{
					MaterialCreateInfo matCreateInfo = {};
					Material::ParseJSONObject(materialObject, matCreateInfo);

					parsedMaterialInfos.push_back(matCreateInfo);

					g_Renderer->InitializeMaterial(&matCreateInfo);
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

		std::string fileContents = materialsObj.Print(0);

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

	void ResourceManager::ParseMeshJSON(GameObject* parent, const JSONObject& meshObj, const std::vector<MaterialID>& materialIDs)
	{
		std::string meshFilePath;
		if (meshObj.SetStringChecked("mesh", meshFilePath))
		{
			i32 sceneVersion = g_SceneManager->CurrentScene()->GetSceneFileVersion();
			if (sceneVersion >= 4)
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

			Mesh::ImportFromFile(meshFilePath, parent, materialIDs);
			return;
		}

		std::string prefabName;
		if (meshObj.SetStringChecked("prefab", prefabName))
		{
			Mesh::ImportFromPrefab(prefabName, parent, materialIDs);
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

		error = FT_Set_Char_Size(face,
			0, metaData.size * sampleDensity,
			(FT_UInt)g_Monitor->DPI.x,
			(FT_UInt)g_Monitor->DPI.y);

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
		for (Texture* vulkanTexture : loadedTextures)
		{
			if (vulkanTexture && !filePath.empty() && filePath.compare(vulkanTexture->relativeFilePath) == 0)
			{
				return vulkanTexture;
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

	bool ResourceManager::RemoveLoadedTexture(TextureID textureID, bool bDestroy)
	{
		return RemoveLoadedTexture(GetLoadedTexture(textureID), bDestroy);
	}

	bool ResourceManager::RemoveLoadedTexture(Texture* texture, bool bDestroy)
	{
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

	MaterialCreateInfo* ResourceManager::GetMaterialInfo(const std::string& materialName)
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
	PrefabInfo* ResourceManager::GetPrefabInfo(const std::string& prefabName)
	{
		for (PrefabInfo& prefabInfo : parsedPrefabInfos)
		{
			if (prefabInfo.name.compare(prefabName) == 0)
			{
				return &prefabInfo;
			}
		}

		return nullptr;
	}

	PrefabInfo* ResourceManager::GetPrefabInfo(const PrefabID& prefabID)
	{
		// TODO: Use map
		for (PrefabInfo& prefabInfo : parsedPrefabInfos)
		{
			if (prefabInfo.ID == prefabID)
			{
				return &prefabInfo;
			}
		}

		return nullptr;
	}

	bool ResourceManager::IsPrefabDirty(const PrefabID& prefabID) const
	{
		for (const PrefabInfo& prefabInfo : parsedPrefabInfos)
		{
			if (prefabInfo.ID == prefabID)
			{
				return prefabInfo.bDirty;
			}
		}

		return true;
	}

	void ResourceManager::SetPrefabDirty(const PrefabID& prefabID)
	{
		for (PrefabInfo& prefabInfo : parsedPrefabInfos)
		{
			if (prefabInfo.ID == prefabID)
			{
				prefabInfo.bDirty = true;
			}
		}
	}

	void ResourceManager::SetAllPrefabsDirty(bool bDirty)
	{
		for (PrefabInfo& prefabInfo : parsedPrefabInfos)
		{
			prefabInfo.bDirty = bDirty;
		}
	}

	void ResourceManager::UpdatePrefabData(const PrefabInfo& prefabInfo)
	{
		for (PrefabInfo& info : parsedPrefabInfos)
		{
			if (info.ID == prefabInfo.ID)
			{
				info = prefabInfo;

				std::string path = RelativePathToAbsolute(PREFAB_DIRECTORY + prefabInfo.name + ".json");
				JSONObject prefabJSON = {};
				prefabJSON.fields.emplace_back("version", JSONValue(BaseScene::LATETST_PREFAB_FILE_VERSION));

				// Added in prefab v2
				std::string prefabIDStr = prefabInfo.ID.ToString();
				prefabJSON.fields.emplace_back("prefab id", JSONValue(prefabIDStr));

				prefabJSON.fields.emplace_back("root", JSONValue(prefabInfo.sourceData));

				std::string fileContents = prefabJSON.Print(0);
				if (WriteFile(path, fileContents, false))
				{
					info.bDirty = false;
				}
				else
				{
					info.bDirty = true;
					PrintError("Failed to write prefab to disk (%s, %s\n)", prefabInfo.name.c_str(), path.c_str());
				}

				g_SceneManager->CurrentScene()->OnPrefabChanged(prefabInfo.ID);

				return;
			}
		}

		PrintError("Attempted to update prefab info but no previous entries exist for name (%s)\n", prefabInfo.name.c_str());
	}

	void ResourceManager::AddNewPrefab(PrefabInfo& prefabInfo)
	{
		for (PrefabInfo& info : parsedPrefabInfos)
		{
			if (info.ID == prefabInfo.ID)
			{
				std::string idStr = info.ID.ToString();
				PrintError("Attempted to add prefab with same ID multiple times! (%s %s)\n", info.name.c_str(), idStr.c_str());
				return;
			}
		}

		prefabInfo.bDirty = true;
		parsedPrefabInfos.push_back(prefabInfo);
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

	void ResourceManager::DrawImGuiWindows()
	{
		if (bFontWindowShowing)
		{
			if (ImGui::Begin("Fonts", &bFontWindowShowing))
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
						metaData.bDirty |= ImGuiExt::DragInt16("Size", &metaData.size, 4, 256);

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
							Platform::OpenExplorer(absDir);
						}
						if (ImGui::Button("Open SDF in explorer"))
						{
							const std::string absDir = ExtractDirectoryString(RelativePathToAbsolute(metaData.renderedTextureFilePath));
							Platform::OpenExplorer(absDir);
						}
						ImGui::SameLine();
						if (ImGui::Button("Open font in explorer"))
						{
							const std::string absDir = ExtractDirectoryString(RelativePathToAbsolute(metaData.filePath));
							Platform::OpenExplorer(absDir);
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

		if (bMaterialWindowShowing)
		{
			if (ImGui::Begin("Materials", &bMaterialWindowShowing))
			{
				static bool bUpdateFields = true;
				static bool bMaterialSelectionChanged = true;
				const i32 MAX_NAME_LEN = 128;
				static i32 selectedMaterialIndexShort = 0; // Index into shortened array
				static MaterialID selectedMaterialID = 0;

				// Skip by any editor materials in case bShowEditorMaterials was set to false while we had one selected
				if (!bShowEditorMaterials)
				{
					i32 matCount = g_Renderer->GetMaterialCount();
					while (!g_Renderer->MaterialExists(selectedMaterialID) ||
						(!g_Renderer->GetMaterial(selectedMaterialID)->visibleInEditor && selectedMaterialID < (u32)(matCount - 1)))
					{
						++selectedMaterialID;
					}
				}

				Material* material = g_Renderer->GetMaterial(selectedMaterialID);

				static std::string matName = "";
				static i32 selectedShaderIndex = 0;
				// Texture index values of 0 represent no texture, 1 = first index into textures array and so on
				//static i32 albedoTextureIndex = 0;
				static std::vector<i32> selectedTextureIndices; // One for each of the current material's texture slots
				//static bool bUpdateAlbedoTextureMaterial = false;

				if (bMaterialSelectionChanged)
				{
					bMaterialSelectionChanged = false;

					//bUpdateFields = true; // ?

					matName = material->name;
					matName.resize(MAX_NAME_LEN);

					selectedTextureIndices.resize(material->textures.Count());

					i32 texIndex = 0;
					for (ShaderUniformContainer<Texture*>::TexPair& pair : material->textures)
					{
						for (u32 loadedTexIndex = 0; loadedTexIndex < loadedTextures.size(); ++loadedTexIndex)
						{
							// TODO: Compare IDs
							if (pair.object == loadedTextures[loadedTexIndex])
							{
								selectedTextureIndices[texIndex] = loadedTexIndex;
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
						material->textures.values[texIndex].object = GetLoadedTexture(selectedTextureIndices[texIndex]);
					}

					i32 i = 0;
					for (Texture* texture : loadedTextures)
					{
						std::string texturePath = texture->relativeFilePath;
						std::string textureName = StripLeadingDirectories(texturePath);

						++i;
					}

					selectedShaderIndex = material->shaderID;

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
				if (g_Renderer->DrawImGuiShadersDropdown(&selectedShaderIndex))
				{
					material->shaderID = selectedShaderIndex;
					bUpdateFields = true;
				}
				ImGui::PopItemWidth();

				ImGui::NewLine();

				ImGui::Columns(2);
				ImGui::SetColumnWidth(0, 240.0f);

				ImGui::ColorEdit3("Colour multiplier", &material->colourMultiplier.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);

				ImGui::ColorEdit3("Albedo", &material->constAlbedo.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);

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
				ImGui::Checkbox("Visible in editor", &material->visibleInEditor);

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
						std::string texFieldName = material->textures.slotNames[texIndex] + "##" + std::to_string(texIndex);
						bUpdateFields |= g_Renderer->DrawImGuiTextureSelector(texFieldName.c_str(), loadedTextures, &selectedTextureIndices[texIndex]);
					}
				}

				ImGui::NewLine();

				ImGui::EndColumns();

				bMaterialSelectionChanged |= g_Renderer->DrawImGuiMaterialList(&selectedMaterialIndexShort, &selectedMaterialID, bShowEditorMaterials);

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
					if (g_Renderer->DrawImGuiShadersDropdown(&newMatShaderIndex, &newMatShader))
						//if (ImGui::BeginChild("Shader", ImVec2(0, 120), true))
						//{
						//	i32 i = 0;
						//	for (VulkanShader& shader : m_Shaders)
						//	{
						//		bool bSelectedShader = (i == newMatShaderIndex);
						//		if (ImGui::Selectable(shader->name.c_str(), &bSelectedShader))
						//		{
						//			newMatShaderIndex = i;
						//		}
						//
						//		++i;
						//	}
						//}
						//ImGui::EndChild(); // Shader

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
							g_Renderer->InitializeMaterial(&createInfo);

							ImGui::CloseCurrentPopup();
						}

					ImGui::SameLine();

					if (ImGui::Button("Cancel"))
					{
						ImGui::CloseCurrentPopup();
					}

					if (g_InputManager->GetKeyPressed(KeyCode::KEY_ESCAPE, true))
					{
						ImGui::CloseCurrentPopup();
					}

					ImGui::EndPopup();
				}

				// Only non-editor materials can be deleted
				if (material->visibleInEditor)
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

		if (bShaderWindowShowing)
		{
			if (ImGui::Begin("Shaders", &bShaderWindowShowing))
			{
				static i32 selectedShaderIndex = 0;
				Shader* selectedShader = nullptr;
				g_Renderer->DrawImGuiShadersList(&selectedShaderIndex, true, &selectedShader);

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
			}

			ImGui::End();
		}

		if (bTextureWindowShowing)
		{
			if (ImGui::Begin("Textures", &bTextureWindowShowing))
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
					i32 i = 0;
					for (Texture* texture : loadedTextures)
					{
						std::string textureName = texture->name;
						if (textureFilter.PassFilter(textureName.c_str()))
						{
							bool bSelected = (i == selectedTextureIndex);
							if (ImGui::Selectable(textureName.c_str(), &bSelected))
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
								g_Renderer->DrawImGuiTexturePreviewTooltip(texture);
							}
							++i;
						}
					}
				}
				ImGui::EndChild();

				if (ImGui::Button("Import Texture"))
				{
					// TODO: Not all textures are directly in this directory! CLEANUP to make more robust
					std::string relativeDirPath = TEXTURE_DIRECTORY;
					std::string absoluteDirectoryStr = RelativePathToAbsolute(relativeDirPath);
					std::string selectedAbsFilePath;
					if (Platform::OpenFileDialog("Import texture", absoluteDirectoryStr, selectedAbsFilePath))
					{
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

							g_Renderer->InitializeTextureFromFile(relativeFilePath, 3, false, false, false);
						}

						ImGui::CloseCurrentPopup();
					}
				}
			}

			ImGui::End();
		}

		if (bMeshWindowShowing)
		{
			if (ImGui::Begin("Meshes", &bMeshWindowShowing))
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

		if (bPrefabsWindowShowing)
		{
			if (ImGui::Begin("Prefabs", &bPrefabsWindowShowing))
			{
				static ImGuiTextFilter prefabFilter;
				prefabFilter.Draw("##prefab-filter");

				ImGui::SameLine();
				if (ImGui::Button("x"))
				{
					prefabFilter.Clear();
				}

				static u32 selectedPrefabIndex = 0;
				if (ImGui::BeginChild("prefab list", ImVec2(0.0f, 120.0f), true))
				{
					for (u32 i = 0; i < (u32)parsedPrefabInfos.size(); ++i)
					{
						const PrefabInfo& prefabInfo = parsedPrefabInfos[i];
						std::string prefabNameStr = prefabInfo.name + (prefabInfo.bDirty ? "*" : "");
						const char* prefabName = prefabNameStr.c_str();

						if (prefabFilter.PassFilter(prefabName))
						{
							bool bSelected = (i == selectedPrefabIndex);
							if (ImGui::Selectable(prefabName, &bSelected))
							{
								selectedPrefabIndex = i;
							}

							if (ImGui::IsItemActive())
							{
								if (ImGui::BeginDragDropSource())
								{
									const void* data = (void*)&prefabInfo;
									size_t size = sizeof(PrefabInfo);

									ImGui::SetDragDropPayload(Editor::PrefabPayloadCStr, data, size);

									ImGui::Text("%s", prefabName);

									ImGui::EndDragDropSource();
								}
							}
						}
					}
				}

				ImGui::EndChild();
			}

			ImGui::End();
		}
	}
} // namespace flex
