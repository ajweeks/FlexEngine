#pragma once

#include "Scene/GameObject.hpp"
#include "Spring.hpp"

// Tessellated, LODed ocean surface built from summed Gerstner waves
namespace flex
{
	class GerstnerWave final : public GameObject
	{
	public:
		explicit GerstnerWave(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual void Initialize() override;
		virtual void Update() override;
		virtual void Destroy(bool bDetachFromParent = true) override;

		virtual void DrawImGuiObjects(bool bDrawingEditorObjects) override;

		void AddWave();
		void RemoveWave(i32 index);

		// Height of the water surface (in world space) at the given position. Only valid near the camera, where the
		// surface is generated. Returns false when there is no surface there.
		bool GetWaterHeight(const glm::vec3& posWS, real& outHeight) const;
		// The first ocean in the current scene (if any)
		static GerstnerWave* GetActive() { return s_Active; }

		struct WaveInfo
		{
			bool enabled = true;
			real a = 0.0f;
			real waveDirTheta = 0.0f;
			real waveLen = 1.0f;

			// Non-serialized, calculated from fields above
			real waveDirCos;
			real waveDirSin;
			real moveSpeed = -1.0f;
			real waveVecMag = -1.0f;
			real accumOffset = 0.0f;
		};

		struct WaveTessellationLOD
		{
			WaveTessellationLOD(real squareDist, u32 vertCountPerAxis) :
				squareDist(squareDist),
				vertCountPerAxis(vertCountPerAxis)
			{}

			real squareDist;
			u32 vertCountPerAxis;
		};

		struct WaveSamplingLOD
		{
			WaveSamplingLOD(real squareDist, real amplitudeCutoff) :
				squareDist(squareDist),
				amplitudeCutoff(amplitudeCutoff)
			{}

			real squareDist;
			real amplitudeCutoff;
		};

		struct WaveChunk
		{
			WaveChunk(const glm::vec2i& index, u32 vertOffset, u32 tessellationLODLevel) :
				index(index),
				vertOffset(vertOffset),
				tessellationLODLevel(tessellationLODLevel)
			{}

			glm::vec2i index;
			u32 vertOffset;
			u32 tessellationLODLevel;
		};

	private:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

		void UpdateDependentVariables(i32 waveIndex);

		void DiscoverChunks();
		void UpdateWaveVertexData();

		// Generates the positions (in parallel) & then normals of every chunk
		void UpdateWaves();
		void UpdatePositionsForChunk(u32 chunkIdx, const glm::vec3& cameraPos);
		glm::vec4 ChooseColourFromLOD(real LOD);
		glm::vec3 QueryHeightFieldFromVerts(const glm::vec3& queryPos) const;
		WaveChunk const* GetChunkAtPos(const glm::vec2& pos) const;
		WaveTessellationLOD const* GetTessellationLOD(u32 lodLevel) const;
		u32 ComputeTesellationLODLevel(const glm::vec2i& chunkIdx);
		void UpdateNormalsForChunk(u32 chunkIdx);
		void SortWaves();
		void SortWaveSamplingLODs();
		void SortWaveTessellationLODs();
		real GetWaveAmplitudeLODCutoffForDistance(real dist) const;

		real size = 30.0f;
		real loadRadius = 35.0f;
		real updateSpeed = 20.0f;
		real blendDist = 10.0f;
		bool bDisableLODs = false;
		u32 maxChunkVertCountPerAxis = 64;
		real reverseWaveAmplitude = 0.5f;

		OceanData oceanData;

		MaterialID m_WaveMaterialID;

		std::vector<WaveTessellationLOD> waveTessellationLODs;
		std::vector<WaveSamplingLOD> waveSamplingLODs;

		std::vector<WaveInfo> waveContributions;
		i32 soloWaveIndex = -1;

		std::vector<WaveChunk> waveChunks;

		bool m_bPinCenter = false;
		glm::vec3 m_PinnedPos;

		VertexBufferDataCreateInfo m_VertexBufferCreateInfo;
		std::vector<u32> m_Indices;

		GameObject* bobber = nullptr;
		Spring<real> bobberTarget;

		u32 DEBUG_lastUsedVertCount = 0;

		static GerstnerWave* s_Active;
	};

	bool operator==(const GerstnerWave::WaveInfo& lhs, const GerstnerWave::WaveInfo& rhs);

	GerstnerWave::WaveChunk const* GetChunkAtPos(const glm::vec2& pos, const std::vector<GerstnerWave::WaveChunk>& waveChunks, real size);
	GerstnerWave::WaveTessellationLOD const* GetTessellationLOD(u32 lodLevel, const std::vector<GerstnerWave::WaveTessellationLOD>& waveTessellationLODs);
	u32 MapVertIndexAcrossLODs(u32 vertIndex, GerstnerWave::WaveTessellationLOD const* lod0, GerstnerWave::WaveTessellationLOD const* lod1);
} // namespace flex
