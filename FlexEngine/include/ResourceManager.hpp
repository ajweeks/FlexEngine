#pragma once

#include "Pair.hpp"
#include "Platform/Platform.hpp" // For Date
#include "UIMesh.hpp"

struct FT_LibraryRec_;
struct FT_FaceRec_;
typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_* FT_Face;

namespace flex
{
	class DirectoryWatcher;
	struct JSONField;
	struct JSONObject;
	struct LoadedMesh;
	struct FontMetaData;
	class BitmapFont;
	struct FontMetric;
	struct Texture;
	class Mesh;
	struct MeshInfo;
	struct MaterialCreateInfo;
	struct PrefabInfo;
	class StringBuilder;

	class ResourceManager
	{
	public:
		ResourceManager();
		~ResourceManager();

		void Initialize();
		void Update();
		void Destroy();
		void DestroyAllLoadedMeshes();

		void PreSceneChange();
		void OnSceneChanged();

		// Returns true if found and *loadedMesh was set
		bool FindPreLoadedMesh(const std::string& relativeFilePath, LoadedMesh** loadedMesh);
		LoadedMesh* FindOrLoadMesh(const std::string& relativeFilePath);

		bool MeshFileNameConforms(const std::string& fileName);

		void DiscoverMeshes();
		void DiscoverPrefabs();
		void DiscoverAudioFiles();
		void DiscoverTextures();

		void ParseMeshJSON(i32 sceneFileVersion, GameObject* parent, const JSONObject& meshObj, const std::vector<MaterialID>& materialIDs);

		void ParseFontFile();
		void SerializeFontFile();

		void ParseMaterialsFile();
		bool SerializeMaterialFile() const;

		JSONField SerializeMesh(Mesh* mesh);

		void SetRenderedSDFFilePath(FontMetaData& metaData);

		bool LoadFontMetrics(const std::vector<char>& fileMemory,
			FT_Library& ft,
			FontMetaData& metaData,
			std::map<i32, FontMetric*>* outCharacters,
			std::array<glm::vec2i, 4>* outMaxPositions,
			FT_Face* outFace);

		void LoadFonts(bool bForceRender);

		void DrawImGuiWindows();
		void DrawImGuiMeshList(i32* selectedMeshIndex, ImGuiTextFilter* meshFilter);
		// Expects to be called from within an ImGui menu
		void DrawImGuiMenuItemizableItems();

		// Returns a pointer into loadedTextures if a texture has been loaded from that file path, otherwise returns nullptr
		Texture* FindLoadedTextureWithPath(const std::string& filePath);
		Texture* FindLoadedTextureWithName(const std::string& fileName);
		Texture* GetLoadedTexture(TextureID textureID);
		Texture* GetOrLoadTexture(const std::string& textureName);
		bool RemoveLoadedTexture(TextureID textureID, bool bDestroy);
		bool RemoveLoadedTexture(Texture* texture, bool bDestroy);

		TextureID GetNextAvailableTextureID();
		TextureID AddLoadedTexture(Texture* texture);

		MaterialCreateInfo* GetMaterialInfo(const std::string& materialName);
		// DEPRECATED (see cpp)
		GameObject* GetPrefabTemplate(const std::string& prefabName) const;
		// DEPRECATED (see cpp)
		PrefabID GetPrefabID(const std::string& prefabName) const;
		GameObject* GetPrefabTemplate(const PrefabID& prefabID) const;
		std::string GetPrefabFileName(const PrefabID& prefabID) const;
		bool IsPrefabDirty(const PrefabID& prefabID) const;
		void SetPrefabDirty(const PrefabID& prefabID);
		void SetAllPrefabsDirty(bool bDirty);
		void UpdatePrefabData(GameObject* prefabTemplate, const PrefabID& prefabID);
		PrefabID AddNewPrefab(GameObject* prefabTemplate, const char* fileName = nullptr);

		bool PrefabTemplateContainsChild(const PrefabID& prefabID, GameObject* child) const;

		void LoadAudioFile(StringID audioFileSID, StringBuilder* errorStringBuilder);

		// ImGui window flags
		bool bFontWindowShowing = false;
		bool bMaterialWindowShowing = false;
		bool bShaderWindowShowing = false;
		bool bTextureWindowShowing = false;
		bool bMeshWindowShowing = false;
		bool bPrefabsWindowShowing = false;
		bool bSoundsWindowShowing = false;

		bool bShowEditorMaterials = false;

		std::map<StringID, FontMetaData> fontMetaData;
		// TODO: Separate fonts from font buffers
		std::vector<BitmapFont*> fontsScreenSpace;
		std::vector<BitmapFont*> fontsWorldSpace;

		std::vector<Texture*> loadedTextures;

		std::vector<MaterialCreateInfo> parsedMaterialInfos;

		struct PrefabTemplatePair
		{
			PrefabTemplatePair(GameObject* templateObject, const PrefabID& prefabID, const std::string& fileName, bool bDirty) :
				templateObject(templateObject),
				prefabID(prefabID),
				fileName(fileName),
				bDirty(bDirty)
			{
			}

			GameObject* templateObject = nullptr;
			PrefabID prefabID = InvalidPrefabID;
			std::string fileName;
			bool bDirty = false;
		};
		std::vector<PrefabTemplatePair> prefabTemplates;


		// Relative file path (e.g. MESH_DIRECTORY "cube.glb") -> LoadedMesh
		std::map<std::string, LoadedMesh*> loadedMeshes;
		std::vector<std::string> discoveredTextures;
		std::vector<std::string> discoveredMeshes;

		struct AudioFileMetaData
		{
			AudioFileMetaData()	{}

			explicit AudioFileMetaData(std::string name, AudioSourceID sourceID = InvalidShaderID) :
				name(name),
				sourceID(sourceID)
			{
			}

			std::string name;
			AudioSourceID sourceID = InvalidAudioSourceID;
			bool bInvalid = false;

			// Editor-only
			Date fileModifiedDate;
		};
		std::map<StringID, AudioFileMetaData> discoveredAudioFiles;

		static const char* s_SupportedTextureFormats[];

	private:
		void WritePrefabToDisk(PrefabTemplatePair& prefabTemplatePair, const PrefabID& prefabID);
		bool PrefabTemplateContainsChildRecursive(GameObject* prefabTemplate, GameObject* child) const;

		UIContainer* ParseUIConfig(const char* filePath);
		bool SerializeUIConfig(const char* filePath, UIContainer* uiContainer);

		std::string m_FontsFilePathAbs;
		std::string m_FontImageExtension = ".png";

		DirectoryWatcher* m_AudioDirectoryWatcher = nullptr;
		i32 m_AudioRefreshFrameCountdown = -1; // When non-negative, counts down each frame until refresh is applied

	};
} // namespace flex