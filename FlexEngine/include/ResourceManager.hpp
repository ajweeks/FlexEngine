#pragma once

struct FT_LibraryRec_;
struct FT_FaceRec_;
typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_* FT_Face;

namespace flex
{
	struct JSONObject;
	struct LoadedMesh;
	struct FontMetaData;
	class BitmapFont;
	struct FontMetric;
	struct Texture;

	class ResourceManager
	{
	public:
		ResourceManager();
		~ResourceManager();

		void Initialize();
		void Destroy();
		void DestroyAllLoadedMeshes();

		void RemoveMaterialID(MaterialID materialID);
		void AddMaterialID(MaterialID newMaterialID);
		std::vector<MaterialID> GetMaterialIDs();
		std::vector<MaterialID> RetrieveMaterialIDsFromJSON(const JSONObject& object, i32 fileVersion);

		// Returns true if found and *loadedMesh was set
		bool FindPreLoadedMesh(const std::string& relativeFilePath, LoadedMesh** loadedMesh);
		LoadedMesh* FindOrLoadMesh(const std::string& relativeFilePath, MeshImportSettings* importSettings = nullptr);
		void DiscoverMeshes();

		bool MeshFileNameConforms(const std::string& fileName);

		void ParseFontFile();
		void SerializeFontFile();

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

		// Relative file path (e.g. MESH_DIRECTORY "cube.glb") -> LoadedMesh
		std::map<std::string, LoadedMesh*> loadedMeshes;
		std::vector<std::string> discoveredMeshes;

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

	private:

		std::string m_FontsFilePathAbs;
		std::string m_FontImageExtension = ".png";

		/*
		* Stores all unique initialized materials
		* A "material array index" is used to index into this array
		*/
		// TODO: Unify with Renderer::m_Materials somehow
		std::vector<MaterialID> m_LoadedMaterials;

	};
} // namespace flex