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
		void PostInitialize();
		void Update();
		void Destroy();
		void DestroyAllLoadedMeshes();

		void PreSceneChange();
		void OnSceneChanged();

		// Returns true if found and *loadedMesh was set
		bool FindPreLoadedMesh(const std::string& relativeFilePath, LoadedMesh** loadedMesh);
		LoadedMesh* FindOrLoadMesh(const std::string& relativeFilePath, bool bForceReload = false);

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

		void ParseDebugOverlayNamesFile();

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
		bool DrawAudioSourceIDImGui(const char* label, StringID& audioSourceSID);

		// Returns a pointer into loadedTextures if a texture has been loaded from that file path, otherwise returns nullptr
		Texture* FindLoadedTextureWithPath(const std::string& filePath);
		Texture* FindLoadedTextureWithName(const std::string& fileName);
		Texture* GetLoadedTexture(TextureID textureID);
		TextureID GetOrLoadTexture(const std::string& textureFilePath);
		bool RemoveLoadedTexture(TextureID textureID, bool bDestroy);
		bool RemoveLoadedTexture(Texture* texture, bool bDestroy);
		TextureID GetOrLoadIcon(StringID gameObjectTypeID);

		TextureID GetNextAvailableTextureID();
		TextureID AddLoadedTexture(Texture* texture);

		MaterialCreateInfo* GetMaterialInfo(const char* materialName);
		// DEPRECATED (see cpp)
		GameObject* GetPrefabTemplate(const char* prefabName) const;
		// DEPRECATED (see cpp)
		PrefabID GetPrefabID(const char* prefabName) const;
		GameObject* GetPrefabTemplate(const PrefabID& prefabID) const;
		std::string GetPrefabFileName(const PrefabID& prefabID) const;
		bool IsPrefabDirty(const PrefabID& prefabID) const;
		void SetPrefabDirty(const PrefabID& prefabID);
		void SetAllPrefabsDirty(bool bDirty);
		void UpdatePrefabData(GameObject* prefabTemplate, const PrefabID& prefabID);
		PrefabID WriteNewPrefabToDisk(GameObject* prefabTemplate, const char* fileName = nullptr);
		bool IsPrefabIDValid(const PrefabID& prefabID);

		void RemovePrefabTemplate(const PrefabID& prefabID);

		bool PrefabTemplateContainsChild(const PrefabID& prefabID, GameObject* child) const;

		AudioSourceID GetAudioID(StringID audioFileSID);
		AudioSourceID GetOrLoadAudioID(StringID audioFileSID);
		void LoadAudioFile(StringID audioFileSID, StringBuilder* errorStringBuilder);

		u32 GetMaxStackSize(const PrefabID& prefabID);

		static const i32 DEFAULT_MAX_STACK_SIZE = 32;

		bool bShowEditorMaterials = false;

		std::map<StringID, FontMetaData> fontMetaData;
		// TODO: Separate fonts from font buffers
		std::vector<BitmapFont*> fontsScreenSpace;
		std::vector<BitmapFont*> fontsWorldSpace;

		std::vector<Texture*> loadedTextures;
		std::vector<std::string> discoveredTextures; // Stores relative file paths to all textures in textures directory
		// Pair of (GameObjectTypeID, (Relative file path, texture ID))
		// texture ID will be invalid until texture is loaded
		std::vector<Pair<StringID, Pair<std::string, TextureID>>> icons;
		TextureID tofuIconID = InvalidTextureID;

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
		};
		std::map<StringID, AudioFileMetaData> discoveredAudioFiles;

		static const char* s_SupportedTextureFormats[];

		std::vector<std::string> debugOverlayNames;

	private:
		void WritePrefabToDisk(PrefabTemplatePair& prefabTemplatePair, const PrefabID& prefabID);
		bool PrefabTemplateContainsChildRecursive(GameObject* prefabTemplate, GameObject* child) const;

		UIContainer* ParseUIConfig(const char* filePath);
		bool SerializeUIConfig(const char* filePath, UIContainer* uiContainer);

		std::string m_FontsFilePathAbs;
		std::string m_FontImageExtension = ".png";

		DirectoryWatcher* m_AudioDirectoryWatcher = nullptr;
		i32 m_AudioRefreshFrameCountdown = -1; // When non-negative, counts down each frame until refresh is applied
		DirectoryWatcher* m_PrefabDirectoryWatcher = nullptr;
		DirectoryWatcher* m_MeshDirectoryWatcher = nullptr;
		DirectoryWatcher* m_TextureDirectoryWatcher = nullptr;

		std::map<StringID, u32> m_NonDefaultStackSizes;

	};
} // namespace flex
