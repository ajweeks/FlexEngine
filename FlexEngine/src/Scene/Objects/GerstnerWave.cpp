#include "stdafx.hpp"

IGNORE_WARNINGS_PUSH

#include <LinearMath/btIDebugDraw.h>

#define SSE_MATHFUN_WITH_CODE
#include "sse_mathfun.h"

#include <glm/gtx/norm.hpp> // for distance2
#include <glm/gtx/quaternion.hpp> // for rotate
#include <glm/gtx/vector_angle.hpp> // for angle
#include <glm/ext/matrix_transform.hpp> // for rotate

#include <imgui/imgui_internal.h> // For PushItemFlag

IGNORE_WARNINGS_POP

#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "Platform/Platform.hpp"
#include "PropertyCollection.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/Objects/GerstnerWave.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	GerstnerWave* GerstnerWave::s_Active = nullptr;

	GerstnerWave::GerstnerWave(const std::string& name, const GameObjectID& gameObjectID /* = InvalidGameObjectID */, const PrefabIDPair& sourcePrefabID /* = InvalidPrefabIDPair */, bool bIsPrefabTemplate /*= false */) :
		GameObject(name, GerstnerWaveSID, gameObjectID, sourcePrefabID, bIsPrefabTemplate)
	{
		// Defaults to use if not set in file
		oceanData = {};
		oceanData.top = glm::vec4(0.1f, 0.3f, 0.6f, 0.0f);
		oceanData.mid = glm::vec4(0.1f, 0.2f, 0.5f, 0.0f);
		oceanData.btm = glm::vec4(0.05f, 0.15f, 0.4f, 0.0f);
		oceanData.fresnelFactor = 0.8f;
		oceanData.fresnelPower = 7.0f;
		oceanData.skyReflectionFactor = 0.8f;
		oceanData.fogFalloff = 1.2f;
		oceanData.fogDensity = 1.2f;

		MaterialCreateInfo matCreateInfo = {};
		matCreateInfo.name = "gerstner";
		matCreateInfo.shaderName = "water";
		matCreateInfo.constAlbedo = glm::vec4(0.4f, 0.5f, 0.8f, 1.0f);
		matCreateInfo.constMetallic = 0.8f;
		matCreateInfo.constRoughness = 0.01f;
		matCreateInfo.bDynamic = true;
		// TODO: Define material in material editor
		matCreateInfo.albedoTexturePath = TEXTURE_DIRECTORY "wave-n-2.png";
		matCreateInfo.enableAlbedoSampler = true;
		matCreateInfo.bSerializable = false;

		m_WaveMaterialID = g_Renderer->InitializeMaterial(&matCreateInfo);

		bobberTarget = Spring<real>(0.0f);
		bobberTarget.DR = 2.5f;
		bobberTarget.UAF = 40.0f;
		bobber = new GameObject("Bobber", BaseObjectSID);
		bobber->SetSerializable(false);
		MaterialID bobberMatID = InvalidMaterialID;
		if (!g_Renderer->FindOrCreateMaterialByName("pbr red", bobberMatID))
		{
			PrintError("Failed to find material for bobber!\n");
		}
		else
		{
			bobberMatID = g_Renderer->GetPlaceholderMaterialID();
		}
		Mesh* mesh = bobber->SetMesh(new Mesh(bobber));
		if (!mesh->LoadFromFile(MESH_DIRECTORY "sphere.glb", bobberMatID))
		{
			PrintError("Failed to load bobber mesh\n");
		}
		g_SceneManager->CurrentScene()->AddRootObject(bobber);
	}

	PropertyCollection* GerstnerWave::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		collection->childCollection = GetPropertyCollectionManager()->AllocateCollection("gerstner wave");

		collection->childCollection->RegisterProperty("fresnel factor", FIELD_OFFSET_AND_TYPE(GerstnerWave, oceanData.fresnelFactor))
			.VersionAdded(6)
			.Precision(2);
		collection->childCollection->RegisterProperty("fresnel power", FIELD_OFFSET_AND_TYPE(GerstnerWave, oceanData.fresnelPower))
			.VersionAdded(2)
			.Precision(2);
		collection->childCollection->RegisterProperty("sky reflection factor", FIELD_OFFSET_AND_TYPE(GerstnerWave, oceanData.skyReflectionFactor))
			.VersionAdded(6)
			.Precision(2);
		collection->childCollection->RegisterProperty("fog falloff", FIELD_OFFSET_AND_TYPE(GerstnerWave, oceanData.fogFalloff))
			.VersionAdded(6)
			.Precision(2);
		collection->childCollection->RegisterProperty("fog density", FIELD_OFFSET_AND_TYPE(GerstnerWave, oceanData.fogDensity))
			.VersionAdded(6)
			.Precision(2);

		return collection;
	}

	void GerstnerWave::Initialize()
	{
		m_VertexBufferCreateInfo = {};
		m_VertexBufferCreateInfo.attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(m_WaveMaterialID)->shaderID)->vertexAttributes;

		SortWaveSamplingLODs();
		SortWaveTessellationLODs();

		DiscoverChunks();
		UpdateWaveVertexData();

		SetMesh(new Mesh(this));
		Mesh::CreateInfo meshCreateInfo = {};
		meshCreateInfo.bDynamic = true;
		meshCreateInfo.vertexBufferCreateInfo = &m_VertexBufferCreateInfo;
		meshCreateInfo.indices = m_Indices;
		meshCreateInfo.materialIDs = { m_WaveMaterialID };
		meshCreateInfo.initialMaxVertexCount = (u32)m_VertexBufferCreateInfo.positions_3D.size();
		m_Mesh->LoadFromMemory(meshCreateInfo);

		if (s_Active == nullptr && !m_bIsTemplate)
		{
			s_Active = this;
		}

		GameObject::Initialize();
	}

	void GerstnerWave::Update()
	{
		if (!m_bVisible)
		{
			return;
		}

		PROFILE_AUTO("GerstnerWave Update");

		for (WaveInfo& waveInfo : waveContributions)
		{
			if (waveInfo.enabled)
			{
				waveInfo.accumOffset += (waveInfo.moveSpeed * g_DeltaTime);
			}
		}

#if 0
		for (u32 i = 0; i < (u32)waveChunks.size(); ++i)
		{
			const glm::vec2i& chunkIndex = waveChunks[i].index;
			for (u32 j = 0; j < i; ++j)
			{
				g_Renderer->GetDebugRenderer()->drawSphere(btVector3((chunkIndex.x) * size, 8.0f + 1.5f * j, (chunkIndex.y) * size), 0.5f, btVector4(1, 1, 1, 1));
			}
		}
#endif

		DiscoverChunks();
		UpdateWaveVertexData();

		MeshComponent* meshComponent = m_Mesh->GetSubMesh(0);
		if (!m_Indices.empty())
		{
			meshComponent->UpdateDynamicVertexData(m_VertexBufferCreateInfo, m_Indices);
			g_Renderer->ShrinkDynamicVertexData(meshComponent->renderID, 0.5f);
		}

		// Bobber
		if (!m_Indices.empty())
		{
			const glm::vec3 wavePos = m_Transform.GetWorldPosition();
			glm::vec3 bobberTargetPos = QueryHeightFieldFromVerts(m_bPinCenter ? m_PinnedPos : g_CameraManager->CurrentCamera()->position);
			bobberTarget.SetTargetPos(bobberTargetPos.y);
			bobberTarget.Tick(g_DeltaTime);
			const real bobberYOffset = 0.2f;
			glm::vec3 newPos = wavePos + glm::vec3(bobberTargetPos.x, bobberTarget.pos + bobberYOffset, bobberTargetPos.z);
			bobber->GetTransform()->SetWorldPosition(newPos);
		}

		g_Renderer->SetGlobalUniform(&U_OCEAN_DATA, &oceanData, sizeof(oceanData));

		GameObject::Update();
	}

	void GerstnerWave::Destroy(bool bDetachFromParent /* = true */)
	{
		g_Renderer->ClearGlobalUniform(&U_OCEAN_DATA, &oceanData);

		if (s_Active == this)
		{
			s_Active = nullptr;
		}

		GameObject::Destroy(bDetachFromParent);
	}

	GerstnerWave::WaveChunk const* GerstnerWave::GetChunkAtPos(const glm::vec2& pos) const
	{
		return flex::GetChunkAtPos(pos, waveChunks, size);
	}

	GerstnerWave::WaveTessellationLOD const* GerstnerWave::GetTessellationLOD(u32 lodLevel) const
	{
		return flex::GetTessellationLOD(lodLevel, waveTessellationLODs);
	}

	u32 GerstnerWave::ComputeTesellationLODLevel(const glm::vec2i& chunkIdx)
	{
		if (waveTessellationLODs.empty())
		{
			waveTessellationLODs.emplace_back(0.0f, maxChunkVertCountPerAxis);
		}

		glm::vec3 center = m_bPinCenter ? m_PinnedPos : g_CameraManager->CurrentCamera()->position;
		real sqrDist = glm::distance2(glm::vec2(center.x, center.z), glm::vec2(chunkIdx) * size);

		for (u32 i = 0; i < (u32)waveTessellationLODs.size(); ++i)
		{
			if (sqrDist > waveTessellationLODs[i].squareDist)
			{
				return i;
			}
		}

		return (u32)(waveTessellationLODs.size() - 1);
	}

	glm::vec3 GerstnerWave::QueryHeightFieldFromVerts(const glm::vec3& queryPos) const
	{
		WaveChunk const* chunk = GetChunkAtPos(glm::vec2(queryPos.x, queryPos.z));
		if (chunk)
		{
			glm::vec2i chunkIdx = chunk->index;
			WaveTessellationLOD const* tessellationLOD = GetTessellationLOD(chunk->tessellationLODLevel);
			u32 chunkVertCountPerAxis = tessellationLOD->vertCountPerAxis;
			const u32 vertsPerChunk = chunkVertCountPerAxis * chunkVertCountPerAxis;
			glm::vec2 chunkMin((chunkIdx.x - 0.5f) * size, (chunkIdx.y - 0.5f) * size);
			glm::vec2 chunkUV = Saturate(glm::vec2((queryPos.x - chunkMin.x) / size, (queryPos.z - chunkMin.y) / size));
			u32 chunkLocalVertexIndex = (u32)(chunkUV.x * (chunkVertCountPerAxis - 1)) + ((u32)(chunkUV.y * ((real)chunkVertCountPerAxis - 1.0f)) * chunkVertCountPerAxis);
			const u32 idxMax = vertsPerChunk - 1;
			glm::vec3 A(m_VertexBufferCreateInfo.positions_3D[chunk->vertOffset + glm::min(chunkLocalVertexIndex + 0u, idxMax)]);
			glm::vec3 B(m_VertexBufferCreateInfo.positions_3D[chunk->vertOffset + glm::min(chunkLocalVertexIndex + 1u, idxMax)]);
			glm::vec3 C(m_VertexBufferCreateInfo.positions_3D[chunk->vertOffset + glm::min(chunkLocalVertexIndex + chunkVertCountPerAxis, idxMax)]);
			glm::vec3 D(m_VertexBufferCreateInfo.positions_3D[chunk->vertOffset + glm::min(chunkLocalVertexIndex + chunkVertCountPerAxis + 1u, idxMax)]);
			glm::vec2 vertexUV = Saturate(glm::vec2((queryPos.x - A.x) / (B.x - A.x), (queryPos.z - B.z) / (D.z - B.z)));
			glm::vec3 result = Lerp(Lerp(A, B, vertexUV.x), Lerp(C, D, vertexUV.x), vertexUV.y);

			return result;
		}

		// No chunk corresponds with query pos
		return VEC3_ZERO;
	}

	bool GerstnerWave::GetWaterHeight(const glm::vec3& posWS, real& outHeight) const
	{
		if (m_Indices.empty() || !m_bVisible)
		{
			return false;
		}

		// Chunks are generated in world space (around the camera), offset vertically by the object's position
		const glm::vec3 wavePos = m_Transform.GetWorldPosition();
		if (GetChunkAtPos(glm::vec2(posWS.x, posWS.z)) == nullptr)
		{
			return false;
		}
		outHeight = wavePos.y + QueryHeightFieldFromVerts(posWS).y;
		return true;
	}

	void GerstnerWave::DiscoverChunks()
	{
		std::vector<WaveChunk> chunksInRadius;

		glm::vec3 center = m_bPinCenter ? m_PinnedPos : g_CameraManager->CurrentCamera()->position;
		const glm::vec2 centerXZ(center.x, center.z);
		const glm::vec2i camChunkIdx = (glm::vec2i)(glm::vec2(center.x, center.z) / size);
		const i32 maxChunkIdxDiff = (i32)glm::ceil(loadRadius / (real)size);
		const real radiusSqr = loadRadius * loadRadius;
		u32 vertOffset = 0;
		for (i32 x = camChunkIdx.x - maxChunkIdxDiff; x < camChunkIdx.x + maxChunkIdxDiff; ++x)
		{
			for (i32 z = camChunkIdx.y - maxChunkIdxDiff; z < camChunkIdx.y + maxChunkIdxDiff; ++z)
			{
				glm::vec2 chunkCenter(x * size, z * size);

				//if (g_Renderer->GetDebugRenderer() != nullptr)
				//{
				//	g_Renderer->GetDebugRenderer()->drawSphere(btVector3(chunkCenter.x, 1.0f, chunkCenter.y), 0.5f, btVector3(0.75f, 0.5f, 0.6f));
				//}

				if (glm::distance2(chunkCenter, centerXZ) < radiusSqr)
				{
					u32 lodLevel = ComputeTesellationLODLevel(glm::vec2i(x, z));
					chunksInRadius.emplace_back(glm::vec2i(x, z), vertOffset, lodLevel);
					vertOffset += waveTessellationLODs[lodLevel].vertCountPerAxis * waveTessellationLODs[lodLevel].vertCountPerAxis;
				}
			}
		}

		waveChunks = chunksInRadius;
	}

	void GerstnerWave::UpdateWaveVertexData()
	{
		i32 vertCount = 0;
		i32 indexCount = 0;
		for (u32 chunkIdx = 0; chunkIdx < (u32)waveChunks.size(); ++chunkIdx)
		{
			WaveChunk& waveChunk = waveChunks[chunkIdx];
			WaveTessellationLOD const* tessellationLOD = GetTessellationLOD(waveChunk.tessellationLODLevel);
			vertCount += tessellationLOD->vertCountPerAxis * tessellationLOD->vertCountPerAxis;
			indexCount += 6 * (tessellationLOD->vertCountPerAxis - 1) * (tessellationLOD->vertCountPerAxis - 1);
		}

		DEBUG_lastUsedVertCount = vertCount;

		// Resize & regenerate index buffer
		if ((i32)m_Indices.size() != indexCount)
		{
			m_Indices.resize(indexCount);
			i32 i = 0;
			for (u32 chunkIdx = 0; chunkIdx < (u32)waveChunks.size(); ++chunkIdx)
			{
				WaveChunk& waveChunk = waveChunks[chunkIdx];
				WaveTessellationLOD const* tessellationLOD = GetTessellationLOD(waveChunk.tessellationLODLevel);
				for (u32 z = 0; z < tessellationLOD->vertCountPerAxis - 1; ++z)
				{
					for (u32 x = 0; x < tessellationLOD->vertCountPerAxis - 1; ++x)
					{
						u32 vertIdx = z * tessellationLOD->vertCountPerAxis + x + waveChunk.vertOffset;
						m_Indices[i++] = vertIdx;
						m_Indices[i++] = vertIdx + tessellationLOD->vertCountPerAxis;
						m_Indices[i++] = vertIdx + 1;

						vertIdx = vertIdx + 1 + tessellationLOD->vertCountPerAxis;
						m_Indices[i++] = vertIdx;
						m_Indices[i++] = vertIdx - tessellationLOD->vertCountPerAxis;
						m_Indices[i++] = vertIdx - 1;
					}
				}
			}
		}

		// Resize vertex buffers
		if (vertCount > (i32)m_VertexBufferCreateInfo.positions_3D.size())
		{
			m_VertexBufferCreateInfo.positions_3D.resize(vertCount);
			m_VertexBufferCreateInfo.texCoords_UV.resize(vertCount);
			m_VertexBufferCreateInfo.normals.resize(vertCount);
			m_VertexBufferCreateInfo.tangents.resize(vertCount);
			m_VertexBufferCreateInfo.colours_R32G32B32A32.resize(vertCount);
		}
		else
		{
			// Shrink vertex buffers once they have > 50% unused space
			real excess = (real)(m_VertexBufferCreateInfo.positions_3D.size() - vertCount) / m_VertexBufferCreateInfo.positions_3D.size();
			if (excess > 0.5f)
			{
				m_VertexBufferCreateInfo.positions_3D.resize(vertCount);
				m_VertexBufferCreateInfo.positions_3D.shrink_to_fit();
				m_VertexBufferCreateInfo.texCoords_UV.resize(vertCount);
				m_VertexBufferCreateInfo.texCoords_UV.shrink_to_fit();
				m_VertexBufferCreateInfo.normals.resize(vertCount);
				m_VertexBufferCreateInfo.normals.shrink_to_fit();
				m_VertexBufferCreateInfo.tangents.resize(vertCount);
				m_VertexBufferCreateInfo.tangents.shrink_to_fit();
				m_VertexBufferCreateInfo.colours_R32G32B32A32.resize(vertCount);
				m_VertexBufferCreateInfo.colours_R32G32B32A32.shrink_to_fit();
			}
		}

		{
			PROFILE_AUTO("Update waves");
			UpdateWaves();
		}
	}

	void GerstnerWave::UpdateWaves()
	{
		// Chunks' positions are independent so they're generated in parallel. Normals read neighboring chunks'
		// positions, so they're generated afterwards.
		const glm::vec3 cameraPos = g_CameraManager->CurrentCamera()->position;
		const u32 chunkCount = (u32)waveChunks.size();

		JobSystem::Context context;
		JobSystem::Dispatch(context, chunkCount, 1, [this, &cameraPos](JobSystem::JobArgs args)
		{
			UpdatePositionsForChunk(args.jobIndex, cameraPos);
		});
		JobSystem::Wait(context);

		JobSystem::Dispatch(context, chunkCount, 1, [this](JobSystem::JobArgs args)
		{
			UpdateNormalsForChunk(args.jobIndex);
		});
		JobSystem::Wait(context);
	}

	void GerstnerWave::UpdatePositionsForChunk(u32 chunkIdx, const glm::vec3& cameraPos)
	{
		const WaveChunk& waveChunk = waveChunks[chunkIdx];
		WaveTessellationLOD const* tessellationLOD = GetTessellationLOD(waveChunk.tessellationLODLevel);
		const u32 vertCountPerAxis = tessellationLOD->vertCountPerAxis;
		const u32 vertCount = vertCountPerAxis * vertCountPerAxis;

		const glm::vec3 chunkCenter((waveChunk.index.x - 0.5f) * size, 0.0f, (waveChunk.index.y - 0.5f) * size);
		const real chunkDist = glm::distance(chunkCenter, glm::vec3(cameraPos.x, 0.0f, cameraPos.z));
		const real amplitudeLODCutoff = GetWaveAmplitudeLODCutoffForDistance(chunkDist);

		// Structure of arrays (padded to a whole number of SSE vectors) so four vertices are displaced at once
		const u32 vectorCount = (vertCount + 3) / 4;
		thread_local std::vector<real> xs, ys, zs;
		xs.resize(vectorCount * 4);
		ys.assign(vectorCount * 4, 0.0f);
		zs.resize(vectorCount * 4);
		real* x = xs.data();
		real* y = ys.data();
		real* z = zs.data();
		const real step = size / (real)(vertCountPerAxis - 1);
		for (u32 i = 0; i < vectorCount * 4; ++i)
		{
			const u32 vertIdx = glm::min(i, vertCount - 1);
			x[i] = chunkCenter.x + step * (real)(vertIdx % vertCountPerAxis);
			z[i] = chunkCenter.z + step * (real)(vertIdx / vertCountPerAxis);
		}

		for (i32 i = 0; i < (i32)waveContributions.size(); ++i)
		{
			const WaveInfo& waveInfo = waveContributions[i];

			// Waves are sorted by amplitude, smaller ones aren't visible from far away
			if (waveInfo.a < amplitudeLODCutoff)
			{
				break;
			}

			if ((soloWaveIndex != -1 && soloWaveIndex != i) || !waveInfo.enabled)
			{
				continue;
			}

			const glm::vec2 waveVec = glm::vec2(waveInfo.waveDirCos, waveInfo.waveDirSin) * waveInfo.waveVecMag;
			const glm::vec2 waveVecN = glm::normalize(waveVec);
			// A weaker wave travelling the opposite way breaks up the regularity
			const real reversePhaseOffset = fmod(abs(waveVec.x + 1.0f) * 29193.123456f, 1.0f);

			const __m128 kx = _mm_set1_ps(waveVec.x);
			const __m128 kz = _mm_set1_ps(waveVec.y);
			const __m128 offset = _mm_set1_ps(waveInfo.accumOffset);
			const __m128 reverseOffset = _mm_set1_ps(waveInfo.accumOffset + reversePhaseOffset);
			const __m128 amplitude = _mm_set1_ps(waveInfo.a);
			const __m128 dirX = _mm_set1_ps(-waveVecN.x * waveInfo.a);
			const __m128 dirZ = _mm_set1_ps(-waveVecN.y * waveInfo.a);
			const __m128 reverseScale = _mm_set1_ps(reverseWaveAmplitude);

			for (u32 v = 0; v < vectorCount; ++v)
			{
				__m128 px = _mm_loadu_ps(x + v * 4);
				__m128 py = _mm_loadu_ps(y + v * 4);
				__m128 pz = _mm_loadu_ps(z + v * 4);

				// d = dot(waveVec, pos.xz)
				__m128 d = _mm_add_ps(_mm_mul_ps(kx, px), _mm_mul_ps(kz, pz));
				__m128 s, c;
				sincos_ps(_mm_add_ps(d, offset), &s, &c);
				px = _mm_add_ps(px, _mm_mul_ps(dirX, s));
				py = _mm_add_ps(py, _mm_mul_ps(amplitude, c));
				pz = _mm_add_ps(pz, _mm_mul_ps(dirZ, s));

				// Reverse wave (dot with -waveVec, using the displaced position)
				__m128 dr = _mm_sub_ps(_mm_setzero_ps(), _mm_add_ps(_mm_mul_ps(kx, px), _mm_mul_ps(kz, pz)));
				__m128 sr, cr;
				sincos_ps(_mm_add_ps(dr, reverseOffset), &sr, &cr);
				sr = _mm_mul_ps(sr, reverseScale);
				cr = _mm_mul_ps(cr, reverseScale);
				px = _mm_add_ps(px, _mm_mul_ps(dirX, sr));
				py = _mm_add_ps(py, _mm_mul_ps(amplitude, cr));
				pz = _mm_add_ps(pz, _mm_mul_ps(dirZ, sr));

				_mm_storeu_ps(x + v * 4, px);
				_mm_storeu_ps(y + v * 4, py);
				_mm_storeu_ps(z + v * 4, pz);
			}
		}

		glm::vec3* positions = m_VertexBufferCreateInfo.positions_3D.data() + waveChunk.vertOffset;
		for (u32 i = 0; i < vertCount; ++i)
		{
			positions[i] = glm::vec3(x[i], y[i], z[i]);
		}
	}

	glm::vec4 GerstnerWave::ChooseColourFromLOD(real LOD)
	{
		static const glm::vec4 COLOURS[] = { glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), glm::vec4(0.0f, 1.0f, 0.0f, 1.0f), glm::vec4(0.0f, 0.0f, 1.0f, 1.0f), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) };
		//static const glm::vec4 COLOURS[] = { glm::vec4(0.8f, 0.4f, 0.2f, 1.0f), glm::vec4(0.1f, 0.8f, 0.3f, 1.0f), glm::vec4(0.2f, 0.4f, 0.8f, 1.0f), glm::vec4(0.5f, 0.7f, 0.5f, 1.0f) };
		static const u32 colourCount = ARRAY_SIZE(COLOURS);

		u32 lodLevel = (u32)LOD;
		real blend = LOD - lodLevel;

		return blend == 0.0f ? COLOURS[lodLevel] : (blend == 1.0f ? COLOURS[glm::clamp(lodLevel + 1, 0u, colourCount - 1)] : Lerp(COLOURS[lodLevel], COLOURS[glm::clamp(lodLevel + 1, 0u, colourCount - 1)], blend));
	}

	GerstnerWave::WaveChunk const* GetChunkAtPos(const glm::vec2& pos, const std::vector<GerstnerWave::WaveChunk>& waveChunks, real size)
	{
		// Chunk i spans [(i - 0.5) * size, (i + 0.5) * size] (see UpdatePositionsForChunk)
		glm::vec2i queryPosInt((i32)floor(pos.x / size + 0.5f), (i32)floor(pos.y / size + 0.5f));
		for (u32 chunkIdx = 0; chunkIdx < (u32)waveChunks.size(); ++chunkIdx)
		{
			if (waveChunks[chunkIdx].index == queryPosInt)
			{
				return &waveChunks[chunkIdx];
			}
		}

		return nullptr;
	}

	GerstnerWave::WaveTessellationLOD const* GetTessellationLOD(u32 lodLevel, const std::vector<GerstnerWave::WaveTessellationLOD>& waveTessellationLODs)
	{
		if (lodLevel < (u32)waveTessellationLODs.size())
		{
			return &waveTessellationLODs[lodLevel];
		}
		return nullptr;
	}

	void GerstnerWave::UpdateNormalsForChunk(u32 chunkIdx)
	{
		WaveChunk const* waveChunk = &waveChunks[chunkIdx];
		WaveTessellationLOD const* tessellationLOD = GetTessellationLOD(waveChunk->tessellationLODLevel);

		glm::vec3* positions = m_VertexBufferCreateInfo.positions_3D.data();
		glm::vec3* normals = m_VertexBufferCreateInfo.normals.data();
		glm::vec3* tangents = m_VertexBufferCreateInfo.tangents.data();

		const u32 chunkVertCountPerAxis = tessellationLOD->vertCountPerAxis;

		const glm::vec2 chunkCenter((waveChunk->index.x - 0.5f) * size, (waveChunk->index.y - 0.5f) * size);

		WaveChunk const* chunkLeft = GetChunkAtPos(chunkCenter - glm::vec2(size, 0.0f));
		WaveChunk const* chunkRight = GetChunkAtPos(chunkCenter + glm::vec2(size, 0.0f));
		WaveChunk const* chunkBack = GetChunkAtPos(chunkCenter - glm::vec2(0.0f, size));
		WaveChunk const* chunkFor = GetChunkAtPos(chunkCenter + glm::vec2(0.0f, size));
		WaveTessellationLOD const* LODLeft = chunkLeft ? GetTessellationLOD(chunkLeft->tessellationLODLevel) : nullptr;
		WaveTessellationLOD const* LODRight = chunkRight ? GetTessellationLOD(chunkRight->tessellationLODLevel) : nullptr;
		WaveTessellationLOD const* LODBack = chunkBack ? GetTessellationLOD(chunkBack->tessellationLODLevel) : nullptr;
		WaveTessellationLOD const* LODFor = chunkFor ? GetTessellationLOD(chunkFor->tessellationLODLevel) : nullptr;

		for (u32 z = 0; z < chunkVertCountPerAxis; ++z)
		{
			for (u32 x = 0; x < chunkVertCountPerAxis; ++x)
			{
				const u32 localIdx = z * chunkVertCountPerAxis + x;
				const u32 vertIdx = localIdx + waveChunk->vertOffset;

				// glm::vec3 planePos = glm::vec3(
					// chunkCenter.x + size * ((real)x / (chunkVertCountPerAxis - 1)),
					// 0.0f,
					// chunkCenter.y + size * ((real)z / (chunkVertCountPerAxis - 1)));

				real left = 0.0f;
				real right = 0.0f;
				real back = 0.0f;
				real forward = 0.0f;
				if (x >= 1)
				{
					left = positions[vertIdx - 1].y;
				}
				else if (chunkLeft)
				{
					u32 zz = MapVertIndexAcrossLODs(z, tessellationLOD, LODLeft);
					left = positions[(zz * LODLeft->vertCountPerAxis + LODLeft->vertCountPerAxis - 2) + chunkLeft->vertOffset].y;
				}
				if (x < chunkVertCountPerAxis - 1)
				{
					right = positions[vertIdx + 1].y;
				}
				else if (chunkRight)
				{
					u32 zz = MapVertIndexAcrossLODs(z, tessellationLOD, LODRight);
					right = positions[(zz * LODRight->vertCountPerAxis + 1) + chunkRight->vertOffset].y;
				}
				if (z >= 1)
				{
					back = positions[vertIdx - chunkVertCountPerAxis].y;
				}
				else if (chunkBack)
				{
					u32 xx = MapVertIndexAcrossLODs(x, tessellationLOD, LODBack);
					back = positions[(LODBack->vertCountPerAxis - 2) * LODBack->vertCountPerAxis + xx + chunkBack->vertOffset].y;
				}
				if (z < chunkVertCountPerAxis - 1)
				{
					forward = positions[vertIdx + chunkVertCountPerAxis].y;
				}
				else if (chunkFor)
				{
					u32 xx = MapVertIndexAcrossLODs(x, tessellationLOD, LODFor);
					forward = positions[LODFor->vertCountPerAxis + xx + chunkFor->vertOffset].y;
				}

				real dX = left - right;
				real dZ = back - forward;
				normals[vertIdx] = glm::vec3(dX, 2.0f, dZ);
				tangents[vertIdx] = glm::normalize(-glm::cross(normals[vertIdx], glm::vec3(0.0f, 0.0f, 1.0f)));
			}
		}
	}

	u32 MapVertIndexAcrossLODs(u32 vertIndex, GerstnerWave::WaveTessellationLOD const* lod0, GerstnerWave::WaveTessellationLOD const* lod1)
	{
		if (lod0->vertCountPerAxis == lod1->vertCountPerAxis)
		{
			return vertIndex;
		}

		u32 result = (u32)((real)vertIndex / lod0->vertCountPerAxis * lod1->vertCountPerAxis);
		return result;
	}

	void GerstnerWave::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		ImGui::Text("Loaded chunks: %d", (u32)waveChunks.size());


		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.95f, 1.0f), "Gerstner");

		ImGui::DragFloat("Loaded distance", &loadRadius, 0.01f);
		if (ImGui::DragFloat("Update speed", &updateSpeed, 0.1f))
		{
			updateSpeed = glm::clamp(updateSpeed, 0.1f, 10000.0f);
			for (i32 i = 0; i < (i32)waveContributions.size(); ++i)
			{
				UpdateDependentVariables(i);
			}
		}

		ImGui::SliderFloat("Blend dist", &blendDist, 0.0f, size);

		if (ImGui::Checkbox("Pin center", &m_bPinCenter))
		{
			if (m_bPinCenter)
			{
				m_PinnedPos = g_CameraManager->CurrentCamera()->position;
			}
		}

		if (m_bPinCenter)
		{
			ImGui::SameLine();

			ImGui::Text("(%.2f, %.2f, %.2f)", m_PinnedPos.x, m_PinnedPos.y, m_PinnedPos.z);
		}

		ImGui::SliderFloat("Reverse wave amp", &reverseWaveAmplitude, 0.0f, 2.0f);

		// TODO: Remove
		ImGuiExt::ColorEdit3Gamma("Top", &oceanData.top.x);
		ImGuiExt::ColorEdit3Gamma("Mid", &oceanData.mid.x);
		ImGuiExt::ColorEdit3Gamma("Bottom", &oceanData.btm.x);
		ImGui::SliderFloat("Fresnel factor", &oceanData.fresnelFactor, 0.0f, 1.0f);
		ImGui::DragFloat("Fresnel power", &oceanData.fresnelPower, 0.01f, 0.0f, 75.0f);
		ImGui::SliderFloat("Sky reflection", &oceanData.skyReflectionFactor, 0.0f, 1.0f);
		ImGui::DragFloat("Fog falloff", &oceanData.fogFalloff, 0.01f, 0.0f, 3.0f);
		ImGui::DragFloat("Fog density", &oceanData.fogDensity, 0.01f, 0.0f, 3.0f);

		if (ImGuiExt::SliderUInt("Max chunk vert count", &maxChunkVertCountPerAxis, 4u, 256u))
		{
			maxChunkVertCountPerAxis = glm::clamp(maxChunkVertCountPerAxis - (maxChunkVertCountPerAxis % 4), 4u, 256u);
		}

		if (ImGui::SliderFloat("Chunk size", &size, 32.0f, 1024.0f))
		{
			size = glm::clamp(size, 32.0f, 1024.0f);
		}

		ImGui::Checkbox("Disable LODs", &bDisableLODs);

		glm::vec3 bobberPosWS = bobber->GetTransform()->GetWorldPosition();
		ImGui::DragFloat3("Bobber", &bobberPosWS.x);

		ImGui::PushItemWidth(30.0f);
		ImGui::DragFloat("Bobber DR", &bobberTarget.DR, 0.01f);
		ImGui::SameLine();
		ImGui::DragFloat("UAF", &bobberTarget.UAF, 0.01f);
		ImGui::PopItemWidth();

		// Vertex buffer size
		{
			ImGui::NewLine();

			MeshComponent* meshComponent = m_Mesh->GetSubMesh(0);

			{
				char byteCountStr[64];
				ByteCountToString(byteCountStr, 64, (u64)(m_VertexBufferCreateInfo.positions_3D.size() * sizeof(glm::vec3)));

				char nameBuf[256];
				snprintf(nameBuf, 256, "CPU Vertex buffer size %s", byteCountStr);

				real usage = (real)DEBUG_lastUsedVertCount / meshComponent->GetVertexBufferData()->VertexCount;
				ImGui::ProgressBar(usage, ImVec2(0, 0), "");
				ImGui::Text("%s", nameBuf);
			}

			{
				u32 gpuSize = meshComponent->GetVertexBufferData()->UsedVertexBufferSize;
				u32 usedGPUSize = g_Renderer->GetDynamicVertexBufferUsedSize(meshComponent->renderID);

				char byteCountStr[64];
				ByteCountToString(byteCountStr, 64, gpuSize);

				char nameBuf[256];
				snprintf(nameBuf, 256, "GPU Vertex buffer size %s", byteCountStr);

				real usage = (real)usedGPUSize / gpuSize;
				ImGui::ProgressBar(usage, ImVec2(0, 0), "");
				ImGui::Text("%s", nameBuf);
			}

			ImGui::NewLine();
		}

		if (ImGui::TreeNode("Sampling LODs"))
		{
			static bool bNeedsSort = false;

			for (i32 i = 0; i < (i32)waveSamplingLODs.size(); ++i)
			{
				std::string childName = "##samp " + IntToString(i, 2);
				std::string removeStr = "-" + childName;
				if (ImGui::Button(removeStr.c_str()))
				{
					waveSamplingLODs.erase(waveSamplingLODs.begin() + i);
					bNeedsSort = true;
					break;
				}

				ImGui::SameLine();

				std::string distStr = "distance" + childName;
				real dist = glm::sqrt(waveSamplingLODs[i].squareDist);
				if (ImGui::DragFloat(distStr.c_str(), &dist, 1.0f, 0.0f, 10000.0f))
				{
					waveSamplingLODs[i].squareDist = dist * dist;
					bNeedsSort = true;
				}

				std::string amplitudeStr = "amplitude" + childName;
				ImGui::DragFloat(amplitudeStr.c_str(), &waveSamplingLODs[i].amplitudeCutoff, 0.0001f, 0.0f, 10.0f);
			}

			if (ImGui::Button("+"))
			{
				real sqrDist = waveSamplingLODs.empty() ? 100.0f : waveSamplingLODs[waveSamplingLODs.size() - 1].squareDist + 10.0f;
				real amplitude = waveSamplingLODs.empty() ? 1.0f : waveSamplingLODs[waveSamplingLODs.size() - 1].amplitudeCutoff + 1.0f;
				waveSamplingLODs.emplace_back(sqrDist, amplitude);
				bNeedsSort = true;
			}

			ImGui::TreePop();

			if (bNeedsSort && ImGui::IsMouseReleased(0))
			{
				bNeedsSort = false;
				SortWaveSamplingLODs();
			}
		}

		if (ImGui::TreeNode("Tessellation LODs"))
		{
			static bool bNeedsSort = false;

			for (i32 i = 0; i < (i32)waveTessellationLODs.size(); ++i)
			{
				std::string childName = "##tess " + IntToString(i, 2);
				std::string removeStr = "-" + childName;
				if (ImGui::Button(removeStr.c_str()))
				{
					waveTessellationLODs.erase(waveTessellationLODs.begin() + i);
					bNeedsSort = true;
					break;
				}

				ImGui::SameLine();

				std::string distStr = "distance" + childName;
				real dist = glm::sqrt(waveTessellationLODs[i].squareDist);
				if (ImGui::DragFloat(distStr.c_str(), &dist, 1.0f, 0.0f, 10000.0f))
				{
					waveTessellationLODs[i].squareDist = dist * dist;
					bNeedsSort = true;
				}
				std::string vertCountStr = "vert count per axis" + childName;
				if (ImGuiExt::SliderUInt(vertCountStr.c_str(), &waveTessellationLODs[i].vertCountPerAxis, 4u, maxChunkVertCountPerAxis))
				{
					waveTessellationLODs[i].vertCountPerAxis = glm::clamp(waveTessellationLODs[i].vertCountPerAxis - (waveTessellationLODs[i].vertCountPerAxis % 4), 4u, maxChunkVertCountPerAxis);
				}
			}

			if (ImGui::Button("+"))
			{
				real sqrDist = waveTessellationLODs.empty() ? 100.0f : waveTessellationLODs[waveTessellationLODs.size() - 1].squareDist + 10.0f;
				u32 vertCount = 16;
				waveTessellationLODs.emplace_back(sqrDist, vertCount);
				bNeedsSort = true;
			}

			ImGui::TreePop();

			if (bNeedsSort && ImGui::IsMouseReleased(0))
			{
				bNeedsSort = false;
				SortWaveTessellationLODs();
			}
		}

		if (ImGui::TreeNode("Wave factors"))
		{
			for (i32 i = 0; i < (i32)waveContributions.size(); ++i)
			{
				const bool bWasDisabled = (soloWaveIndex != -1) && (soloWaveIndex != i);
				if (bWasDisabled)
				{
					ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
					ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
				}

				std::string childName = "##wave " + IntToString(i, 2);

				std::string removeStr = "-" + childName;
				if (ImGui::Button(removeStr.c_str()))
				{
					RemoveWave(i);
					break;
				}

				ImGui::SameLine();

				static bool bNeedUpdate = false;
				static bool bNeedSort = false;

				std::string enabledStr = "Enabled" + childName;

				ImGui::Checkbox(enabledStr.c_str(), &waveContributions[i].enabled);

				ImGui::SameLine();

				bool bSolo = soloWaveIndex == i;
				std::string soloStr = "Solo" + childName;
				if (ImGui::Checkbox(soloStr.c_str(), &bSolo))
				{
					if (bSolo)
					{
						soloWaveIndex = i;
					}
					else
					{
						soloWaveIndex = -1;
					}
				}

				std::string aStr = "amplitude" + childName;
				bNeedUpdate |= ImGui::DragFloat(aStr.c_str(), &waveContributions[i].a, 0.01f);
				bNeedSort |= bNeedUpdate;
				std::string waveLenStr = "wave len" + childName;
				bNeedUpdate |= ImGui::DragFloat(waveLenStr.c_str(), &waveContributions[i].waveLen, 0.01f);
				bNeedSort |= bNeedUpdate;
				std::string dirStr = "dir" + childName;
				if (ImGui::DragFloat(dirStr.c_str(), &waveContributions[i].waveDirTheta, 0.001f, 0.0f, 10.0f))
				{
					bNeedUpdate = true;
					waveContributions[i].waveDirTheta = fmod(waveContributions[i].waveDirTheta, TWO_PI);
				}

				if (bNeedSort && ImGui::IsMouseReleased(0))
				{
					bNeedSort = false;
					SortWaves();
				}

				if (bNeedUpdate)
				{
					bNeedUpdate = false;
					UpdateDependentVariables(i);
				}

				if (bWasDisabled)
				{
					ImGui::PopItemFlag();
					ImGui::PopStyleVar();
				}

				ImGui::Separator();
			}

			if (ImGui::Button("+"))
			{
				AddWave();
			}

			ImGui::TreePop();
		}
	}

	bool operator==(const GerstnerWave::WaveInfo& lhs, const GerstnerWave::WaveInfo& rhs)
	{
		return memcmp(&lhs, &rhs, sizeof(GerstnerWave::WaveInfo)) == 0;
	}

	void GerstnerWave::ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs)
	{
		FLEX_UNUSED(matIDs);

		JSONObject gerstnerWaveObj;
		if (parentObject.TryGetObject("gerstner wave", gerstnerWaveObj))
		{
			std::vector<JSONObject> waveObjs;
			if (gerstnerWaveObj.TryGetObjectArray("waves", waveObjs))
			{
				for (const JSONObject& waveObj : waveObjs)
				{
					WaveInfo waveInfo = {};
					waveInfo.enabled = waveObj.GetBool("enabled");
					waveInfo.a = waveObj.GetFloat("amplitude");
					waveInfo.waveDirTheta = waveObj.GetFloat("waveDir");
					waveInfo.waveLen = waveObj.GetFloat("waveLen");
					waveContributions.push_back(waveInfo);
				}
			}

			gerstnerWaveObj.TryGetUInt("max chunk vert count per axis", maxChunkVertCountPerAxis);
			gerstnerWaveObj.TryGetFloat("chunk size", size);
			gerstnerWaveObj.TryGetFloat("chunk load radius", loadRadius);
			gerstnerWaveObj.TryGetFloat("update speed", updateSpeed);

			gerstnerWaveObj.TryGetBool("pin center", m_bPinCenter);
			gerstnerWaveObj.TryGetVec3("pinned center position", m_PinnedPos);

			gerstnerWaveObj.TryGetFloat("blend dist", blendDist);

			std::vector<JSONObject> waveSamplingLODsArrObj;
			if (gerstnerWaveObj.TryGetObjectArray("wave sampling lods", waveSamplingLODsArrObj))
			{
				waveSamplingLODs.clear();
				waveSamplingLODs.reserve(waveSamplingLODsArrObj.size());
				for (u32 i = 0; i < (u32)waveSamplingLODsArrObj.size(); ++i)
				{
					real sqrDist;
					waveSamplingLODsArrObj[i].TryGetFloat("square dist", sqrDist);
					real amplitude;
					waveSamplingLODsArrObj[i].TryGetFloat("amplitude cutoff", amplitude);

					waveSamplingLODs.emplace_back(sqrDist, amplitude);
				}

				SortWaveSamplingLODs();
			}
			else
			{
				// Set good default value
				waveSamplingLODs.clear();
				waveSamplingLODs.reserve(1);
				waveSamplingLODs.emplace_back(35721.0f, 0.0f);
			}

			std::vector<JSONObject> waveTessellationLODsArrObj;
			if (gerstnerWaveObj.TryGetObjectArray("wave tessellation lods", waveTessellationLODsArrObj))
			{
				waveTessellationLODs.clear();
				waveTessellationLODs.reserve(waveTessellationLODsArrObj.size());
				for (u32 i = 0; i < (u32)waveTessellationLODsArrObj.size(); ++i)
				{
					real sqrDist;
					waveTessellationLODsArrObj[i].TryGetFloat("square dist", sqrDist);
					u32 vertCount;
					waveTessellationLODsArrObj[i].TryGetUInt("vert count per axis", vertCount);

					waveTessellationLODs.emplace_back(sqrDist, vertCount);
				}
			}
			else
			{
				// Set good default value
				waveTessellationLODs.clear();
				waveTessellationLODs.reserve(4);
				waveTessellationLODs.emplace_back(37636.0f, 8);
				waveTessellationLODs.emplace_back(14438.9f, 28);
				waveTessellationLODs.emplace_back(4116.8f, 52);
				waveTessellationLODs.emplace_back(0.0f, 80);
			}

			if (gerstnerWaveObj.TryGetVec4("colour top", oceanData.top))
			{
				oceanData.top = glm::pow(oceanData.top, glm::vec4(2.2f));
			}
			if (gerstnerWaveObj.TryGetVec4("colour mid", oceanData.mid))
			{
				oceanData.mid = glm::pow(oceanData.mid, glm::vec4(2.2f));
			}
			if (gerstnerWaveObj.TryGetVec4("colour btm", oceanData.btm))
			{
				oceanData.btm = glm::pow(oceanData.btm, glm::vec4(2.2f));
			}
		}

		SortWaves();

		// Init dependent variables
		for (i32 i = 0; i < (i32)waveContributions.size(); ++i)
		{
			UpdateDependentVariables(i);
		}
	}

	void GerstnerWave::SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData)
	{
		PropertyCollection* collection = GetPropertyCollectionManager()->GetCollectionForObjectType(m_TypeID);

		JSONObject gerstnerWaveObj = {};

		std::vector<JSONObject> waveObjs;
		waveObjs.resize(waveContributions.size());
		for (i32 i = 0; i < (i32)waveContributions.size(); ++i)
		{
			JSONObject& waveObj = waveObjs[i];
			waveObj.fields.emplace_back("enabled", JSONValue(waveContributions[i].enabled));
			waveObj.fields.emplace_back("amplitude", JSONValue(waveContributions[i].a));
			waveObj.fields.emplace_back("waveDir", JSONValue(waveContributions[i].waveDirTheta));
			waveObj.fields.emplace_back("waveLen", JSONValue(waveContributions[i].waveLen));
		}

		gerstnerWaveObj.fields.emplace_back("waves", JSONValue(waveObjs));

		gerstnerWaveObj.fields.emplace_back("max chunk vert count per axis", JSONValue(maxChunkVertCountPerAxis));
		gerstnerWaveObj.fields.emplace_back("chunk size", JSONValue(size));
		gerstnerWaveObj.fields.emplace_back("chunk load radius", JSONValue(loadRadius));
		gerstnerWaveObj.fields.emplace_back("update speed", JSONValue(updateSpeed));

		gerstnerWaveObj.fields.emplace_back("pin center", JSONValue(m_bPinCenter));
		gerstnerWaveObj.fields.emplace_back("pinned center position", JSONValue(m_PinnedPos, 2));

		gerstnerWaveObj.fields.emplace_back("blend dist", JSONValue(blendDist));

		std::vector<JSONObject> waveSamplingLODsArrObj(waveSamplingLODs.size());
		for (u32 i = 0; i < (u32)waveSamplingLODs.size(); ++i)
		{
			JSONObject samplingLOD = {};
			samplingLOD.fields.emplace_back("square dist", JSONValue(waveSamplingLODs[i].squareDist, 1));
			samplingLOD.fields.emplace_back("amplitude cutoff", JSONValue(waveSamplingLODs[i].amplitudeCutoff));
			waveSamplingLODsArrObj[i] = samplingLOD;
		}
		gerstnerWaveObj.fields.emplace_back("wave sampling lods", JSONValue(waveSamplingLODsArrObj));

		std::vector<JSONObject> waveTessellationLODsArrObj(waveTessellationLODs.size());
		for (u32 i = 0; i < (u32)waveTessellationLODs.size(); ++i)
		{
			JSONObject tessellationLOD = {};
			tessellationLOD.fields.emplace_back("square dist", JSONValue(waveTessellationLODs[i].squareDist, 1));
			tessellationLOD.fields.emplace_back("vert count per axis", JSONValue(waveTessellationLODs[i].vertCountPerAxis));
			waveTessellationLODsArrObj[i] = tessellationLOD;
		}
		gerstnerWaveObj.fields.emplace_back("wave tessellation lods", JSONValue(waveTessellationLODsArrObj));

		gerstnerWaveObj.fields.emplace_back("colour top", JSONValue(glm::pow(oceanData.top, glm::vec4(1.0f / 2.2f)), 2));
		gerstnerWaveObj.fields.emplace_back("colour mid", JSONValue(glm::pow(oceanData.mid, glm::vec4(1.0f / 2.2f)), 2));
		gerstnerWaveObj.fields.emplace_back("colour btm", JSONValue(glm::pow(oceanData.btm, glm::vec4(1.0f / 2.2f)), 2));

		SerializeRegisteredProperties(gerstnerWaveObj, collection->childCollection, bSerializePrefabData);

		parentObject.fields.emplace_back("gerstner wave", JSONValue(gerstnerWaveObj));
	}

	void GerstnerWave::UpdateDependentVariables(i32 waveIndex)
	{
		if (waveIndex >= 0 && waveIndex < (i32)waveContributions.size())
		{
			waveContributions[waveIndex].waveVecMag = TWO_PI / waveContributions[waveIndex].waveLen;
			waveContributions[waveIndex].moveSpeed = glm::sqrt(updateSpeed * waveContributions[waveIndex].waveVecMag);

			waveContributions[waveIndex].waveDirCos = cos(waveContributions[waveIndex].waveDirTheta);
			waveContributions[waveIndex].waveDirSin = sin(waveContributions[waveIndex].waveDirTheta);
		}
	}

	void GerstnerWave::AddWave()
	{
		waveContributions.push_back({});
		UpdateDependentVariables((u32)waveContributions.size() - 1);
		SortWaves();
	}

	void GerstnerWave::RemoveWave(i32 index)
	{
		if (index >= 0 && index < (i32)waveContributions.size())
		{
			waveContributions.erase(waveContributions.begin() + index);
			SortWaves();
		}
	}

	void GerstnerWave::SortWaves()
	{
		WaveInfo soloWaveCopy;
		if (soloWaveIndex != -1)
		{
			soloWaveCopy = waveContributions[soloWaveIndex];
		}
		std::sort(waveContributions.begin(), waveContributions.end(),
			[](const WaveInfo& waveA, const WaveInfo& waveB)
		{
			return abs(waveA.a) > abs(waveB.a);
		});
		if (soloWaveIndex != -1)
		{
			for (i32 i = 0; i < (i32)waveContributions.size(); ++i)
			{
				const WaveInfo& waveInfo = waveContributions[i];
				if (waveInfo == soloWaveCopy)
				{
					soloWaveIndex = i;
					break;
				}
			}
		}
	}

	void GerstnerWave::SortWaveSamplingLODs()
	{
		std::sort(waveSamplingLODs.begin(), waveSamplingLODs.end(),
			[](const WaveSamplingLOD& lodA, const WaveSamplingLOD& lodB)
		{
			return abs(lodA.squareDist) > abs(lodB.squareDist);
		});
	}

	void GerstnerWave::SortWaveTessellationLODs()
	{
		std::sort(waveTessellationLODs.begin(), waveTessellationLODs.end(),
			[](const WaveTessellationLOD& lodA, const WaveTessellationLOD& lodB)
		{
			return abs(lodA.squareDist) > abs(lodB.squareDist);
		});
	}

	real GerstnerWave::GetWaveAmplitudeLODCutoffForDistance(real dist) const
	{
		if (bDisableLODs)
		{
			return 0.0f;
		}

		for (u32 i = 0; i < (u32)waveSamplingLODs.size(); ++i)
		{
			if (dist >= waveSamplingLODs[i].squareDist)
			{
				return waveSamplingLODs[i].amplitudeCutoff;
			}
		}
		return 0.0f;
	}
} // namespace flex
