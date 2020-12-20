#pragma once

struct FT_LibraryRec_;
struct FT_FaceRec_;
typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_* FT_Face;

namespace flex
{
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

	class ResourceManager
	{
	public:
		ResourceManager();
		~ResourceManager();

		void Initialize();
		void Destroy();
		void DestroyAllLoadedMeshes();

		// Returns true if found and *loadedMesh was set
		bool FindPreLoadedMesh(const std::string& relativeFilePath, LoadedMesh** loadedMesh);
		LoadedMesh* FindOrLoadMesh(const std::string& relativeFilePath);

		bool MeshFileNameConforms(const std::string& fileName);

		void DiscoverMeshes();
		void DiscoverPrefabs();

		void ParseMeshJSON(GameObject* parent, const JSONObject& meshObj, const std::vector<MaterialID>& materialIDs);

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

		// Returns a pointer into loadedTextures if a texture has been loaded from that file path, otherwise returns nullptr
		Texture* FindLoadedTextureWithPath(const std::string& filePath);
		Texture* GetLoadedTexture(TextureID textureID);
		bool RemoveLoadedTexture(TextureID textureID, bool bDestroy);
		bool RemoveLoadedTexture(Texture* texture, bool bDestroy);

		TextureID GetNextAvailableTextureID();
		TextureID AddLoadedTexture(Texture* texture);

		MaterialCreateInfo* GetMaterialInfo(const std::string& materialName);
		PrefabInfo* GetPrefabInfo(const std::string& prefabName);

		// ImGui window flags
		bool bFontWindowShowing = false;
		bool bMaterialWindowShowing = false;
		bool bShaderWindowShowing = false;
		bool bTextureWindowShowing = false;
		bool bMeshWindowShowing = false;

		bool bShowEditorMaterials = false;

		std::map<std::string, FontMetaData> fontMetaData;
		// TODO: Separate fonts from font buffers
		std::vector<BitmapFont*> fontsScreenSpace;
		std::vector<BitmapFont*> fontsWorldSpace;

		std::vector<Texture*> loadedTextures;

		std::vector<MaterialCreateInfo> parsedMaterialInfos;
		std::vector<PrefabInfo> parsedPrefabInfos;

		// Relative file path (e.g. MESH_DIRECTORY "cube.glb") -> LoadedMesh
		std::map<std::string, LoadedMesh*> loadedMeshes;
		std::vector<std::string> discoveredMeshes;

	private:

		std::string m_FontsFilePathAbs;
		std::string m_FontImageExtension = ".png";

	};
} // namespace flex