#pragma once

#include "Pair.hpp"
#include "Platform/Platform.hpp" // For Date
#include "UIMesh.hpp"
#include "Particles.hpp"

struct FT_LibraryRec_;
struct FT_FaceRec_;
typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_* FT_Face;

namespace flex
{
	class BitmapFont;
	class DirectoryWatcher;
	struct FontMetaData;
	struct FontMetric;
	struct JSONField;
	struct JSONObject;
	struct LoadedMesh;
	struct MaterialCreateInfo;
	class Mesh;
	struct MeshInfo;
	struct PrefabInfo;
	class StringBuilder;
	struct Texture;

	struct TextureLoadInfo
	{
		std::string relativeFilePath;
		HTextureSampler sampler;
		bool bFlipVertically;
		bool bGenerateMipMaps;
		bool bHDR;
	};

	class ResourceManager
	{
	public:
		struct PrefabTemplateInfo;

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

		void ParseMeshJSON(i32 sceneFileVersion, GameObject* parent, const JSONObject& meshObj, const std::vector<MaterialID>& materialIDs, bool bCreateRenderObject);

		JSONField SerializeMesh(Mesh* mesh);

		void DiscoverMeshes();
		void DiscoverPrefabs();
		void DiscoverAudioFiles();
		void DiscoverTextures();
		void DiscoverParticleParameterTypes();
		void SerializeParticleParameterTypes();
		void DiscoverParticleSystemTemplates();
		void SerializeAllParticleSystemTemplates();

		void ParseGameObjectTypesFile();
		void SerializeGameObjectTypesFile();
		const char* TypeIDToString(StringID typeID);

		void ParseFontFile();
		void SerializeFontFile();

		void ParseMaterialsFiles();
		bool SerializeAllMaterials() const;
		bool SerializeLoadedMaterials() const;

		void ParseDebugOverlayNamesFile();

		void SetRenderedSDFFilePath(FontMetaData& metaData);

		bool LoadFontMetrics(const std::vector<char>& fileMemory,
			FT_Library ft,
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
		void DrawParticleSystemTemplateImGuiObjects();
		void DrawParticleParameterTypesImGui();

		// Returns a pointer into loadedTextures if a texture has been loaded from that file path, otherwise returns nullptr
		Texture* FindLoadedTextureWithPath(const std::string& filePath);
		Texture* FindLoadedTextureWithName(const std::string& fileName);
		bool IsTextureLoading(TextureID textureID) const;
		bool IsTextureCreated(TextureID textureID) const;
		Texture* GetLoadedTexture(TextureID textureID, bool bProvideFallbackWhileLoading = true);
		TextureID GetOrLoadTexture(const std::string& textureFilePath, HTextureSampler sampler = nullptr);
		bool RemoveLoadedTexture(TextureID textureID, bool bDestroy);
		bool RemoveLoadedTexture(Texture* texture, bool bDestroy);
		TextureID GetOrLoadIcon(StringID prefabNameSID, i32 resolution = -1);

		TextureID GetNextAvailableTextureID();
		TextureID QueueTextureLoad(const std::string& relativeFilePath,
			HTextureSampler inSampler,
			bool bFlipVertically,
			bool bGenerateMipMaps,
			bool bHDR);
		TextureID QueueTextureLoad(const TextureLoadInfo& loadInfo);
		TextureID LoadTextureImmediate(const std::string& relativeFilePath,
			HTextureSampler inSampler,
			bool bFlipVertically,
			bool bGenerateMipMaps,
			bool bHDR);
		TextureID LoadTextureImmediate(const TextureLoadInfo& loadInfo);
		bool GetQueuedTextureLoadInfo(TextureID textureID, TextureLoadInfo& outLoadInfo);
		TextureID AddLoadedTexture(Texture* texture, TextureID existingTextureID = InvalidTextureID);
		TextureID InitializeTextureArrayFromMemory(void* data, u32 size, TextureFormat inFormat, const std::string& name, u32 width, u32 height, u32 layerCount, u32 channelCount, HTextureSampler inSampler);

		MaterialCreateInfo* GetMaterialInfo(const char* materialName);
		// DEPRECATED (see cpp)
		GameObject* GetPrefabTemplate(const char* prefabName) const;
		// DEPRECATED (see cpp)
		PrefabID GetPrefabID(const char* prefabName) const;
		GameObject* GetPrefabTemplate(const PrefabIDPair& prefabIDPair) const;
		GameObject* GetPrefabTemplate(const PrefabID& prefabID) const;
		GameObject* GetPrefabTemplate(const PrefabID& prefabID, const GameObjectID& subObjectID) const;
		GameObject* GetPrefabSubObject(GameObject* prefabTemplate, const GameObjectID& subObjectID) const;
		std::string GetPrefabFileName(const PrefabID& prefabID) const;
		bool IsPrefabDirty(const PrefabID& prefabID) const;
		void SetPrefabDirty(const PrefabID& prefabID);
		void SetAllPrefabsDirty(bool bDirty);
		void UpdatePrefabData(GameObject* prefabTemplate, const PrefabID& prefabID);
		bool WriteExistingPrefabToDisk(GameObject* prefabTemplate);
		// Creates and registers a new prefab template from the given object, but does not write it to disk
		PrefabID CreateNewPrefab(GameObject* sourceObject, const char* fileName);
		bool WritePrefabToDisk(PrefabTemplateInfo& prefabTemplateInfo);
		bool IsPrefabIDValid(const PrefabID& prefabID);

		void DeletePrefabTemplate(const PrefabID& prefabID);

		bool PrefabTemplateContainsChild(const PrefabID& prefabID, GameObject* child) const;

		void SerializeAllPrefabTemplates();

		AudioSourceID GetAudioSourceID(StringID audioFileSID);
		AudioSourceID GetOrLoadAudioSourceID(StringID audioFileSID, bool b2D);
		void DestroyAudioSource(AudioSourceID audioSourceID);
		void LoadAudioFile(StringID audioFileSID, StringBuilder* errorStringBuilder, bool b2D);

		u32 GetMaxStackSize(const PrefabID& prefabID);

		void AddNewGameObjectType(const char* newType);

		void AddNewParticleTemplate(StringID particleTemplateNameSID, const ParticleSystemTemplate particleTemplate);
		bool GetParticleTemplate(StringID particleTemplateNameSID, ParticleSystemTemplate& outParticleTemplate);
		ParticleParamterValueType GetParticleParameterValueType(const char* paramName);

		static const i32 DEFAULT_MAX_STACK_SIZE = 32;

		bool bShowEditorMaterials = false;

		std::map<StringID, FontMetaData> fontMetaData;
		// TODO: Separate fonts from font buffers
		std::vector<BitmapFont*> fontsScreenSpace;
		std::vector<BitmapFont*> fontsWorldSpace;

		std::vector<Texture*> loadedTextures;
		std::mutex m_LoadedTexturesMutex;
		std::vector<std::string> discoveredTextures; // Stores relative file paths to all textures in textures directory
		struct IconMetaData
		{
			std::string relativeFilePath;
			TextureID textureID = InvalidTextureID;
			i32 resolution = 0;
		};
		// One entry per icon found on disk, mapped on prefab name SID
		// Texture ID will be invalid until texture is loaded
		std::vector<Pair<StringID, IconMetaData>> discoveredIcons;
		TextureID tofuIconID = InvalidTextureID;

		JobSystem::Context m_TextureLoadingContext;
		std::mutex m_QueuedTextureLoadInfoMutex;
		std::vector<Pair<TextureID, TextureLoadInfo>> m_QueuedTextureLoadInfos;

		// Creation info for all discovered materials
		std::vector<MaterialCreateInfo> parsedMaterialInfos;

		struct PrefabTemplateInfo
		{
			PrefabTemplateInfo(GameObject* templateObject, const PrefabID& prefabID, const std::string& fileName) :
				templateObject(templateObject),
				prefabID(prefabID),
				fileName(fileName),
				bDirty(false)
			{
			}

			GameObject* templateObject = nullptr;
			PrefabID prefabID = InvalidPrefabID;
			std::string fileName;
			bool bDirty = false;
		};
		// TODO: Use map
		std::vector<PrefabTemplateInfo> prefabTemplates;
		std::map<PrefabID, std::string> discoveredPrefabs;

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

		std::map<StringID, std::string> gameObjectTypeStringIDPairs;

		std::vector<std::string> debugOverlayNames;

		std::vector<ParticleParameterType> particleParameterTypes;

	private:
		bool PrefabTemplateContainsChildRecursive(GameObject* prefabTemplate, GameObject* child) const;
		bool SerializeMaterial(Material* material) const;

		std::string m_FontsFilePathAbs;
		std::string m_FontImageExtension = ".png";

		DirectoryWatcher* m_AudioDirectoryWatcher = nullptr;
		i32 m_AudioRefreshFrameCountdown = -1; // When non-negative, counts down each frame until refresh is applied
		DirectoryWatcher* m_PrefabDirectoryWatcher = nullptr;
		DirectoryWatcher* m_MeshDirectoryWatcher = nullptr;
		DirectoryWatcher* m_TextureDirectoryWatcher = nullptr;

		std::map<StringID, u32> m_NonDefaultStackSizes;

		bool m_bParticleParameterTypesDirty = false;

		std::unordered_map<StringID, ParticleSystemTemplate> m_ParticleTemplates;

	};
} // namespace flex
