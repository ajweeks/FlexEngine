
#include "stdafx.hpp"

#include "Systems/TrackManager.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtx/norm.hpp> // For distance2
IGNORE_WARNINGS_POP

#include "Systems/CartManager.hpp"
#include "Graphics/Renderer.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Helpers.hpp"
#include "Player.hpp"
#include "PlayerController.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	const real TrackManager::JUNCTION_THRESHOLD_DIST = 0.01f;

	JSONObject Junction::Serialize() const
	{
		JSONObject result = {};

		const char* delim = ", ";
		std::string trackIndicesStr(IntToString(trackIndices[0]) + delim +
			IntToString(trackIndices[1]) + delim +
			IntToString(trackIndices[2]) + delim +
			IntToString(trackIndices[3]));
		std::string curveIndicesStr(IntToString(curveIndices[0]) + delim +
			IntToString(curveIndices[1]) + delim +
			IntToString(curveIndices[2]) + delim +
			IntToString(curveIndices[3]));

		result.fields.emplace_back("track indices", JSONValue(trackIndicesStr));
		result.fields.emplace_back("curve indices", JSONValue(curveIndicesStr));

		return result;
	}

	// ---

	namespace
	{
		struct TrackMeshBuilder
		{
			void Clear()
			{
				vertexData.positions_3D.clear();
				vertexData.texCoords_UV.clear();
				vertexData.colours_R32G32B32A32.clear();
				vertexData.normals.clear();
				vertexData.tangents.clear();
				indices.clear();
			}

			bool IsEmpty() const
			{
				return indices.empty();
			}

			// Corners are given in loop order, winding is chosen so the quad faces along normal
			void AddQuad(const glm::vec3 corners[4], const glm::vec2 uvs[4], const glm::vec3& normal, const glm::vec3& tangent)
			{
				u32 baseIndex = (u32)vertexData.positions_3D.size();
				for (i32 i = 0; i < 4; ++i)
				{
					vertexData.positions_3D.push_back(corners[i]);
					vertexData.texCoords_UV.push_back(uvs[i]);
					vertexData.colours_R32G32B32A32.push_back(VEC4_ONE);
					vertexData.normals.push_back(normal);
					vertexData.tangents.push_back(tangent);
				}

				bool bFlip = glm::dot(glm::cross(corners[1] - corners[0], corners[2] - corners[0]), normal) < 0.0f;
				if (bFlip)
				{
					indices.insert(indices.end(), { baseIndex, baseIndex + 2, baseIndex + 1, baseIndex, baseIndex + 3, baseIndex + 2 });
				}
				else
				{
					indices.insert(indices.end(), { baseIndex, baseIndex + 1, baseIndex + 2, baseIndex, baseIndex + 2, baseIndex + 3 });
				}
			}

			// Axes must be normalized, extents are half-lengths. Bottom face is omitted since it's never visible
			void AddBox(const glm::vec3& centre, const glm::vec3& right, const glm::vec3& up, const glm::vec3& forward, const glm::vec3& extents)
			{
				const glm::vec3 r = right * extents.x;
				const glm::vec3 u = up * extents.y;
				const glm::vec3 f = forward * extents.z;
				const glm::vec2 uvs[4] = { glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(1.0f, 1.0f), glm::vec2(0.0f, 1.0f) };

				glm::vec3 top[4] = { centre + u - r - f, centre + u + r - f, centre + u + r + f, centre + u - r + f };
				AddQuad(top, uvs, up, right);
				glm::vec3 front[4] = { centre + f - r - u, centre + f + r - u, centre + f + r + u, centre + f - r + u };
				AddQuad(front, uvs, forward, right);
				glm::vec3 back[4] = { centre - f - r - u, centre - f + r - u, centre - f + r + u, centre - f - r + u };
				AddQuad(back, uvs, -forward, right);
				glm::vec3 side0[4] = { centre + r - f - u, centre + r + f - u, centre + r + f + u, centre + r - f + u };
				AddQuad(side0, uvs, right, forward);
				glm::vec3 side1[4] = { centre - r - f - u, centre - r + f - u, centre - r + f + u, centre - r - f + u };
				AddQuad(side1, uvs, -right, forward);
			}

			VertexBufferDataCreateInfo vertexData;
			std::vector<u32> indices;
		};

		struct TrackSample
		{
			glm::vec3 pos;
			glm::vec3 forward;
			glm::vec3 right;
			glm::vec3 up;
			real dist; // Distance along track
		};

		void SampleTrack(const BezierCurveList& track, real maxSegmentLength, std::vector<TrackSample>& outSamples)
		{
			outSamples.clear();

			glm::vec3 prevForward = VEC3_FORWARD;
			glm::vec3 prevRight = VEC3_RIGHT;
			real dist = 0.0f;
			for (i32 c = 0; c < (i32)track.curves.size(); ++c)
			{
				const BezierCurve3D& curve = track.curves[c];
				real curveLength = curve.calculatedLength > 0.0f ? curve.calculatedLength : glm::distance(curve.points[0], curve.points[3]);
				i32 segmentCount = glm::max((i32)glm::ceil(curveLength / maxSegmentLength), 1);
				// Curves share their end points, so skip the first sample of all but the first curve
				for (i32 s = (c == 0 ? 0 : 1); s <= segmentCount; ++s)
				{
					real t = (real)s / (real)segmentCount;

					TrackSample sample;
					sample.pos = curve.GetPointOnCurve(t);

					glm::vec3 derivative = curve.GetFirstDerivativeOnCurve(t);
					sample.forward = glm::length2(derivative) > 0.00001f ? glm::normalize(derivative) : prevForward;

					glm::vec3 right = glm::cross(sample.forward, VEC3_UP);
					sample.right = glm::length2(right) > 0.00001f ? glm::normalize(right) : prevRight;
					sample.up = glm::normalize(glm::cross(sample.right, sample.forward));

					if (!outSamples.empty())
					{
						dist += glm::distance(outSamples.back().pos, sample.pos);
					}
					sample.dist = dist;

					prevForward = sample.forward;
					prevRight = sample.right;
					outSamples.push_back(sample);
				}
			}
		}

		// Sweeps a rail's cross section (two sides, top & end caps) along the samples
		void AddRail(TrackMeshBuilder& builder, const std::vector<TrackSample>& samples, real centreOffset, real baseHeight, const TrackMeshSettings& settings)
		{
			const real halfWidth = settings.railWidth * 0.5f;
			const real topHeight = baseHeight + settings.railHeight;
			const real uvScale = 0.5f;

			// Profile corners as (right, up) offsets, in loop order
			const glm::vec2 profile[4] =
			{
				glm::vec2(centreOffset - halfWidth, baseHeight),
				glm::vec2(centreOffset - halfWidth, topHeight),
				glm::vec2(centreOffset + halfWidth, topHeight),
				glm::vec2(centreOffset + halfWidth, baseHeight),
			};
			// Normal of the face between profile[i] and profile[i + 1] in (right, up) space
			const glm::vec2 faceNormals[3] = { glm::vec2(-1.0f, 0.0f), glm::vec2(0.0f, 1.0f), glm::vec2(1.0f, 0.0f) };

			auto ToWorld = [](const TrackSample& sample, const glm::vec2& offset)
			{
				return sample.pos + sample.right * offset.x + sample.up * offset.y;
			};

			for (i32 i = 0; i < (i32)samples.size() - 1; ++i)
			{
				const TrackSample& s0 = samples[i];
				const TrackSample& s1 = samples[i + 1];
				if (s1.dist - s0.dist < 0.0001f)
				{
					continue;
				}

				for (i32 f = 0; f < 3; ++f)
				{
					glm::vec3 corners[4] = { ToWorld(s0, profile[f]), ToWorld(s0, profile[f + 1]), ToWorld(s1, profile[f + 1]), ToWorld(s1, profile[f]) };
					glm::vec2 uvs[4] = { glm::vec2(0.0f, s0.dist * uvScale), glm::vec2(1.0f, s0.dist * uvScale), glm::vec2(1.0f, s1.dist * uvScale), glm::vec2(0.0f, s1.dist * uvScale) };
					glm::vec3 avgRight = glm::normalize(s0.right + s1.right);
					glm::vec3 avgUp = glm::normalize(s0.up + s1.up);
					glm::vec3 normal = avgRight * faceNormals[f].x + avgUp * faceNormals[f].y;
					builder.AddQuad(corners, uvs, normal, glm::normalize(s0.forward + s1.forward));
				}
			}

			// End caps
			const glm::vec2 capUVs[4] = { glm::vec2(0.0f, 0.0f), glm::vec2(0.0f, 1.0f), glm::vec2(1.0f, 1.0f), glm::vec2(1.0f, 0.0f) };
			for (i32 end = 0; end < 2; ++end)
			{
				const TrackSample& sample = (end == 0 ? samples.front() : samples.back());
				glm::vec3 corners[4] = { ToWorld(sample, profile[0]), ToWorld(sample, profile[1]), ToWorld(sample, profile[2]), ToWorld(sample, profile[3]) };
				builder.AddQuad(corners, capUVs, end == 0 ? -sample.forward : sample.forward, sample.right);
			}
		}

		void AddSleepers(TrackMeshBuilder& builder, const std::vector<TrackSample>& samples, const TrackMeshSettings& settings)
		{
			const real totalLength = samples.back().dist;
			if (totalLength <= 0.0f || settings.sleeperSpacing <= 0.0f)
			{
				return;
			}

			// Centre the sleepers along the track so both ends look the same
			i32 sleeperCount = glm::max((i32)(totalLength / settings.sleeperSpacing), 1);
			real startDist = (totalLength - (sleeperCount - 1) * settings.sleeperSpacing) * 0.5f;
			const glm::vec3 extents(settings.sleeperLength * 0.5f, settings.sleeperHeight * 0.5f, settings.sleeperWidth * 0.5f);

			i32 sampleIndex = 0;
			for (i32 i = 0; i < sleeperCount; ++i)
			{
				real dist = startDist + i * settings.sleeperSpacing;
				while (sampleIndex < (i32)samples.size() - 2 && samples[sampleIndex + 1].dist < dist)
				{
					++sampleIndex;
				}

				const TrackSample& s0 = samples[sampleIndex];
				const TrackSample& s1 = samples[glm::min(sampleIndex + 1, (i32)samples.size() - 1)];
				real segmentLength = s1.dist - s0.dist;
				real alpha = segmentLength > 0.0001f ? Saturate((dist - s0.dist) / segmentLength) : 0.0f;

				glm::vec3 pos = Lerp(s0.pos, s1.pos, alpha);
				glm::vec3 forward = glm::normalize(Lerp(s0.forward, s1.forward, alpha));
				glm::vec3 right = glm::normalize(Lerp(s0.right, s1.right, alpha));
				glm::vec3 up = glm::normalize(glm::cross(right, forward));

				builder.AddBox(pos + up * extents.y, right, up, forward, extents);
			}
		}
	} // namespace

	TrackManager::TrackManager()
	{
		m_PreviewJunctionDir.junctionIndex = -1;
		m_PreviewJunctionDir.dir = VEC3_ZERO;
	}

	void TrackManager::Initialize()
	{
	}

	void TrackManager::Destroy()
	{
		tracks.clear();
		junctions.clear();
		m_TrackMeshes.clear();
		m_PreviewTrackMesh = {};
		m_bPreviewTrackVisible = false;
		m_HighlightTrackMesh = {};
		m_HighlightedTrackID = InvalidTrackID;
	}

	void TrackManager::Update()
	{
		PROFILE_AUTO("TrackManager Update");

		if (g_SceneManager->CurrentScene() == nullptr)
		{
			return;
		}

		CHECK_EQ(m_TrackMeshes.size(), tracks.size());
		for (i32 i = 0; i < (i32)tracks.size(); ++i)
		{
			if (m_TrackMeshes[i].bDirty)
			{
				UpdateTrackMesh(m_TrackMeshes[i], tracks[i], "Track mesh", "track rail", "track sleeper", 0.0f);
				if (i == (i32)m_HighlightedTrackID)
				{
					m_HighlightTrackMesh.bDirty = true;
				}
			}
		}

		if (m_HighlightedTrackID != InvalidTrackID)
		{
			if (m_HighlightTrackMesh.bDirty)
			{
				// Slightly larger than the track so it covers it
				UpdateTrackMesh(m_HighlightTrackMesh, tracks[(i32)m_HighlightedTrackID], "Track highlight mesh", "track delete highlight", "track delete highlight", 0.015f);
			}

			MaterialID highlightMatID;
			if (g_Renderer->FindOrCreateMaterialByName("track delete highlight", highlightMatID))
			{
				// Flash
				Material* highlightMat = g_Renderer->GetMaterial(highlightMatID);
				real pulse = 0.5f + 0.5f * glm::sin(g_SecElapsedSinceProgramStart * 8.0f);
				highlightMat->constAlbedo = glm::vec4(Lerp(glm::vec3(0.12f, 0.02f, 0.02f), glm::vec3(0.35f, 0.04f, 0.04f), pulse), 1.0f);
				highlightMat->constEmissive = glm::vec4(Lerp(glm::vec3(0.05f, 0.0f, 0.0f), glm::vec3(0.4f, 0.02f, 0.02f), pulse), 1.0f);
			}
		}
		else if (m_HighlightTrackMesh.object != nullptr && m_HighlightTrackMesh.object->IsVisible())
		{
			m_HighlightTrackMesh.object->SetVisible(false);
		}

		if (m_bPreviewTrackVisible)
		{
			if (m_PreviewTrackMesh.bDirty)
			{
				UpdateTrackMesh(m_PreviewTrackMesh, m_PreviewTrack, "Track preview mesh", "track rail", "track sleeper", 0.0f);
			}
		}
		else if (m_PreviewTrackMesh.object != nullptr && m_PreviewTrackMesh.object->IsVisible())
		{
			// Hidden rather than destroyed since the preview is shown & hidden frequently
			m_PreviewTrackMesh.object->SetVisible(false);
		}
	}

	void TrackManager::OnPreSceneChange()
	{
		// Mesh objects are owned (and destroyed) by the scene
		for (TrackMesh& trackMesh : m_TrackMeshes)
		{
			trackMesh.object = nullptr;
		}
		m_PreviewTrackMesh = {};
		m_bPreviewTrackVisible = false;
		m_HighlightTrackMesh = {};
		m_HighlightedTrackID = InvalidTrackID;

		// Clear here rather than in OnSceneChanged, which runs after the new scene has loaded its tracks
		tracks.clear();
		junctions.clear();
		m_TrackMeshes.clear();
	}

	void TrackManager::UpdateTrackMesh(TrackMesh& trackMesh, const BezierCurveList& track, const char* objectName,
		const char* railMaterialName, const char* sleeperMaterialName, real inflate)
	{
		PROFILE_AUTO("TrackManager UpdateTrackMesh");

		trackMesh.bDirty = false;

		static std::vector<TrackSample> samples;
		SampleTrack(track, m_MeshSettings.segmentLength, samples);
		if (samples.size() < 2 || samples.back().dist < 0.01f)
		{
			if (trackMesh.object != nullptr && trackMesh.object->IsVisible())
			{
				trackMesh.object->SetVisible(false);
			}
			return;
		}

		MaterialID railMatID, sleeperMatID;
		if (!g_Renderer->FindOrCreateMaterialByName(railMaterialName, railMatID))
		{
			PrintWarn("Failed to find %s material, using placeholder\n", railMaterialName);
			railMatID = g_Renderer->GetPlaceholderMaterialID();
		}
		if (!g_Renderer->FindOrCreateMaterialByName(sleeperMaterialName, sleeperMatID))
		{
			PrintWarn("Failed to find %s material, using placeholder\n", sleeperMaterialName);
			sleeperMatID = g_Renderer->GetPlaceholderMaterialID();
		}

		TrackMeshSettings settings = m_MeshSettings;
		settings.railWidth += inflate * 2.0f;
		settings.railHeight += inflate;
		settings.sleeperLength += inflate * 2.0f;
		settings.sleeperWidth += inflate * 2.0f;
		settings.sleeperHeight += inflate;

		TrackMeshBuilder railBuilder, sleeperBuilder;
		railBuilder.vertexData.attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(railMatID)->shaderID)->vertexAttributes;
		sleeperBuilder.vertexData.attributes = g_Renderer->GetShader(g_Renderer->GetMaterial(sleeperMatID)->shaderID)->vertexAttributes;

		// Rails sit on top of the (un-inflated) sleepers
		const real halfGauge = settings.gauge * 0.5f;
		AddRail(railBuilder, samples, -halfGauge, m_MeshSettings.sleeperHeight, settings);
		AddRail(railBuilder, samples, halfGauge, m_MeshSettings.sleeperHeight, settings);
		AddSleepers(sleeperBuilder, samples, settings);

		if (railBuilder.IsEmpty() || sleeperBuilder.IsEmpty())
		{
			if (trackMesh.object != nullptr && trackMesh.object->IsVisible())
			{
				trackMesh.object->SetVisible(false);
			}
			return;
		}

		if (trackMesh.object == nullptr)
		{
			// Vertices are in world space, so the object stays at the origin
			GameObject* object = GameObject::CreateObjectOfType(BaseObjectSID, objectName);
			object->SetSerializable(false);
			object->SetVisibleInSceneExplorer(false);

			Mesh* mesh = object->SetMesh(new Mesh(object));
			Mesh::CreateInfo meshCreateInfo = {};
			meshCreateInfo.bDynamic = true;
			meshCreateInfo.vertexBufferCreateInfo = &railBuilder.vertexData;
			meshCreateInfo.indices = railBuilder.indices;
			meshCreateInfo.materialIDs = { railMatID };
			meshCreateInfo.initialMaxVertexCount = (u32)railBuilder.vertexData.positions_3D.size();
			mesh->LoadFromMemory(meshCreateInfo);

			MeshComponent::LoadFromMemoryDynamic(mesh, sleeperBuilder.vertexData, sleeperBuilder.indices, sleeperMatID,
				(u32)sleeperBuilder.vertexData.positions_3D.size());

			g_SceneManager->CurrentScene()->AddRootObject(object);
			trackMesh.object = object;
		}

		else if (!trackMesh.object->IsVisible())
		{
			trackMesh.object->SetVisible(true);
		}

		Mesh* mesh = trackMesh.object->GetMesh();
		mesh->GetSubMesh(0)->UpdateDynamicVertexData(railBuilder.vertexData, railBuilder.indices);
		mesh->GetSubMesh(1)->UpdateDynamicVertexData(sleeperBuilder.vertexData, sleeperBuilder.indices);
	}

	void TrackManager::DestroyTrackMesh(TrackMesh& trackMesh)
	{
		if (trackMesh.object != nullptr)
		{
			g_SceneManager->CurrentScene()->RemoveObject(trackMesh.object, true);
			trackMesh.object = nullptr;
		}
	}

	void TrackManager::DrawImGui()
	{
		if (ImGui::TreeNode("Track Manager"))
		{
			ImGui::Text("%u tracks, %u junctions", (u32)tracks.size(), (u32)junctions.size());

			ImGui::Checkbox("Draw debug lines", &m_bDrawDebugLines);

			if (ImGui::TreeNode("Mesh settings"))
			{
				bool bChanged = false;
				bChanged = ImGui::SliderFloat("Gauge", &m_MeshSettings.gauge, 0.3f, 3.0f) || bChanged;
				bChanged = ImGui::SliderFloat("Rail width", &m_MeshSettings.railWidth, 0.01f, 0.5f) || bChanged;
				bChanged = ImGui::SliderFloat("Rail height", &m_MeshSettings.railHeight, 0.01f, 0.5f) || bChanged;
				bChanged = ImGui::SliderFloat("Sleeper length", &m_MeshSettings.sleeperLength, 0.1f, 4.0f) || bChanged;
				bChanged = ImGui::SliderFloat("Sleeper width", &m_MeshSettings.sleeperWidth, 0.02f, 1.0f) || bChanged;
				bChanged = ImGui::SliderFloat("Sleeper height", &m_MeshSettings.sleeperHeight, 0.01f, 0.5f) || bChanged;
				bChanged = ImGui::SliderFloat("Sleeper spacing", &m_MeshSettings.sleeperSpacing, 0.2f, 4.0f) || bChanged;
				bChanged = ImGui::SliderFloat("Segment length", &m_MeshSettings.segmentLength, 0.1f, 4.0f) || bChanged;
				if (bChanged)
				{
					for (TrackMesh& trackMesh : m_TrackMeshes)
					{
						trackMesh.bDirty = true;
					}
					m_PreviewTrackMesh.bDirty = true;
					m_HighlightTrackMesh.bDirty = true;
				}

				ImGui::TreePop();
			}

			if (ImGui::SmallButton("<"))
			{
				m_DEBUG_highlightedJunctionIndex--;
				m_DEBUG_highlightedJunctionIndex = glm::max(m_DEBUG_highlightedJunctionIndex, -1);
			}
			ImGui::SameLine();
			ImGui::Text("highlighted junction: %i", m_DEBUG_highlightedJunctionIndex);
			ImGui::SameLine();
			if (ImGui::SmallButton(">"))
			{
				m_DEBUG_highlightedJunctionIndex++;
				m_DEBUG_highlightedJunctionIndex = glm::min(m_DEBUG_highlightedJunctionIndex, (i32)junctions.size() - 1);
			}

			if (m_DEBUG_highlightedJunctionIndex != -1)
			{
				ImGui::Text("Junction connected track count: %i", junctions[m_DEBUG_highlightedJunctionIndex].trackCount);
			}

			ImGui::Text("Preview junc idx: %i", m_PreviewJunctionDir.junctionIndex);
			ImGui::Text("Preview curve dir: %s", VecToString(m_PreviewJunctionDir.dir, 2).c_str());

			ImGui::TreePop();
		}
	}

	JSONObject TrackManager::Serialize() const
	{
		JSONObject result = {};

		JSONObject tracksObj = {};

		for (const BezierCurveList& track : tracks)
		{
			tracksObj.fields.emplace_back("track", JSONValue(track.Serialize()));
		}

		result.fields.emplace_back("tracks", JSONValue(tracksObj));

		return result;
	}

	void TrackManager::InitializeFromJSON(const JSONObject& obj)
	{
		const JSONObject& tracksObj = obj.GetObject("tracks");

		for (TrackMesh& trackMesh : m_TrackMeshes)
		{
			DestroyTrackMesh(trackMesh);
		}

		tracks.resize(tracksObj.fields.size());
		m_TrackMeshes.clear();
		m_TrackMeshes.resize(tracks.size());
		for (u32 i = 0; i < tracks.size(); ++i)
		{
			tracks[i].InitializeFromJSON(tracksObj.fields[i].value.objectValue);
		}

		FindJunctions();
	}

	TrackID TrackManager::AddTrack(const BezierCurveList& track)
	{
		tracks.push_back(track);
		for (BezierCurve3D& curve : tracks.back().curves)
		{
			curve.CalculateLength();
		}
		m_TrackMeshes.emplace_back();
		return (TrackID)(tracks.size() - 1);
	}

	void TrackManager::RemoveTrack(TrackID trackID)
	{
		CHECK_LT((i32)trackID, (i32)tracks.size());

		DestroyTrackMesh(m_TrackMeshes[(i32)trackID]);
		tracks.erase(tracks.begin() + (i32)trackID);
		m_TrackMeshes.erase(m_TrackMeshes.begin() + (i32)trackID);

		GetSystem<CartManager>(SystemType::CART_MANAGER)->OnTrackRemoved(trackID);

		BaseScene* scene = g_SceneManager->CurrentScene();
		for (i32 i = 0; i < 2; ++i)
		{
			Player* player = scene->GetPlayer(i);
			if (player != nullptr)
			{
				player->OnTrackRemoved(trackID);
			}
		}

		if (m_HighlightedTrackID == trackID)
		{
			SetHighlightedTrack(InvalidTrackID);
		}
		else if (m_HighlightedTrackID != InvalidTrackID && m_HighlightedTrackID > trackID)
		{
			--m_HighlightedTrackID;
		}

		m_PreviewJunctionDir = {};
		m_DEBUG_highlightedJunctionIndex = -1;

		FindJunctions();
	}

	void TrackManager::OnTrackModified(TrackID trackID)
	{
		for (BezierCurve3D& curve : tracks[(i32)trackID].curves)
		{
			curve.CalculateLength();
		}
		m_TrackMeshes[(i32)trackID].bDirty = true;
	}

	BezierCurveList TrackManager::CreateTrackThroughNodes(const std::vector<glm::vec3>& nodes, glm::vec3 startDir, glm::vec3 endDir)
	{
		BezierCurveList result;

		const i32 nodeCount = (i32)nodes.size();
		if (nodeCount < 2)
		{
			return result;
		}

		// Catmull-Rom tangents for interior nodes
		std::vector<glm::vec3> tangents(nodeCount);
		for (i32 i = 1; i < nodeCount - 1; ++i)
		{
			tangents[i] = (nodes[i + 1] - nodes[i - 1]) * 0.5f;
		}

		// End tangents are either forced (to line up with an existing track), or chosen such that the curvature at the end is zero
		auto ChooseEndTangent = [](const glm::vec3& forcedDir, const glm::vec3& chord, const glm::vec3* neighbourTangent)
		{
			if (glm::length2(forcedDir) > 0.0f)
			{
				glm::vec3 dir = glm::normalize(forcedDir);
				// Existing tracks can be joined from either direction
				if (glm::dot(dir, chord) < 0.0f)
				{
					dir = -dir;
				}
				return dir * glm::length(chord);
			}
			if (neighbourTangent != nullptr)
			{
				return chord * 1.5f - *neighbourTangent * 0.5f;
			}
			return chord;
		};

		const glm::vec3 firstChord = nodes[1] - nodes[0];
		const glm::vec3 lastChord = nodes[nodeCount - 1] - nodes[nodeCount - 2];
		if (nodeCount == 2)
		{
			// Neither end has an interior neighbour, so resolve forced tangents first and have free ends follow them
			bool bStartForced = glm::length2(startDir) > 0.0f;
			bool bEndForced = glm::length2(endDir) > 0.0f;
			tangents[0] = ChooseEndTangent(startDir, firstChord, nullptr);
			tangents[1] = ChooseEndTangent(endDir, lastChord, nullptr);
			if (bStartForced && !bEndForced)
			{
				tangents[1] = ChooseEndTangent(VEC3_ZERO, lastChord, &tangents[0]);
			}
			else if (bEndForced && !bStartForced)
			{
				tangents[0] = ChooseEndTangent(VEC3_ZERO, firstChord, &tangents[1]);
			}
		}
		else
		{
			tangents[0] = ChooseEndTangent(startDir, firstChord, &tangents[1]);
			tangents[nodeCount - 1] = ChooseEndTangent(endDir, lastChord, &tangents[nodeCount - 2]);
		}

		result.curves.reserve(nodeCount - 1);
		for (i32 i = 0; i < nodeCount - 1; ++i)
		{
			result.curves.emplace_back(
				nodes[i],
				nodes[i] + tangents[i] / 3.0f,
				nodes[i + 1] - tangents[i + 1] / 3.0f,
				nodes[i + 1]);
		}

		return result;
	}

	void TrackManager::CreateLoopedTracks(const std::vector<glm::vec3>& nodes, glm::vec3 startDir, i32 loopNodeIndex, std::vector<BezierCurveList>& outTracks)
	{
		outTracks.clear();

		const i32 nodeCount = (i32)nodes.size();
		const i32 loopNodeCount = nodeCount - loopNodeIndex;
		if (loopNodeIndex < 0 || loopNodeCount < 3)
		{
			outTracks.push_back(CreateTrackThroughNodes(nodes, startDir, VEC3_ZERO));
			return;
		}

		// Closed Catmull-Rom spline through the loop's nodes
		auto LoopNode = [&](i32 i)
		{
			return nodes[loopNodeIndex + ((i % loopNodeCount) + loopNodeCount) % loopNodeCount];
		};
		auto LoopTangent = [&](i32 i)
		{
			return (LoopNode(i + 1) - LoopNode(i - 1)) * 0.5f;
		};

		std::vector<BezierCurve3D> loopCurves;
		loopCurves.reserve(loopNodeCount);
		for (i32 i = 0; i < loopNodeCount; ++i)
		{
			loopCurves.emplace_back(
				LoopNode(i),
				LoopNode(i) + LoopTangent(i) / 3.0f,
				LoopNode(i + 1) - LoopTangent(i + 1) / 3.0f,
				LoopNode(i + 1));
		}

		if (loopNodeIndex > 0)
		{
			std::vector<glm::vec3> leadInNodes(nodes.begin(), nodes.begin() + loopNodeIndex + 1);
			outTracks.push_back(CreateTrackThroughNodes(leadInNodes, startDir, LoopTangent(0)));
		}

		const i32 splitIndex = loopNodeCount / 2;
		outTracks.emplace_back(std::vector<BezierCurve3D>(loopCurves.begin(), loopCurves.begin() + splitIndex));
		outTracks.emplace_back(std::vector<BezierCurve3D>(loopCurves.begin() + splitIndex, loopCurves.end()));
	}

	TrackPointRef TrackManager::GetClosestNode(const glm::vec3& pos, real range, const std::vector<TrackPointRef>* exclude /* = nullptr */) const
	{
		TrackPointRef result = {};

		real smallestDistSq = range * range;
		for (i32 t = 0; t < (i32)tracks.size(); ++t)
		{
			for (i32 c = 0; c < (i32)tracks[t].curves.size(); ++c)
			{
				for (i32 p = 0; p < 4; p += 3)
				{
					real distSq = glm::distance2(pos, tracks[t].curves[c].points[p]);
					if (distSq < smallestDistSq)
					{
						TrackPointRef ref = { (TrackID)t, c, p };
						if (exclude != nullptr && Contains(*exclude, ref))
						{
							continue;
						}
						smallestDistSq = distSq;
						result = ref;
					}
				}
			}
		}

		return result;
	}

	glm::vec3 TrackManager::GetPoint(const TrackPointRef& ref) const
	{
		return tracks[(i32)ref.trackID].curves[ref.curveIndex].points[ref.pointIndex];
	}

	glm::vec3 TrackManager::GetNodeDirection(const TrackPointRef& ref) const
	{
		const BezierCurve3D& curve = tracks[(i32)ref.trackID].curves[ref.curveIndex];
		glm::vec3 derivative = curve.GetFirstDerivativeOnCurve(ref.pointIndex == 0 ? 0.0f : 1.0f);
		if (glm::length2(derivative) < 0.00001f)
		{
			derivative = curve.points[3] - curve.points[0];
		}
		return glm::length2(derivative) < 0.00001f ? VEC3_ZERO : glm::normalize(derivative);
	}

	void TrackManager::GetCoincidentNodes(const TrackPointRef& ref, std::vector<TrackPointRef>& outNodes) const
	{
		outNodes.clear();

		const glm::vec3 pos = GetPoint(ref);
		for (i32 t = 0; t < (i32)tracks.size(); ++t)
		{
			for (i32 c = 0; c < (i32)tracks[t].curves.size(); ++c)
			{
				for (i32 p = 0; p < 4; p += 3)
				{
					if (NearlyEquals(tracks[t].curves[c].points[p], pos, JUNCTION_THRESHOLD_DIST))
					{
						outNodes.push_back({ (TrackID)t, c, p });
					}
				}
			}
		}
	}

	bool TrackManager::RemoveNode(const TrackPointRef& ref)
	{
		CHECK(ref.IsNode());

		std::vector<BezierCurve3D>& curves = tracks[(i32)ref.trackID].curves;
		const i32 curveCount = (i32)curves.size();

		// Nodes are shared by adjacent curves, refer to the node by the curve following it
		const i32 nodeIndex = ref.curveIndex + (ref.pointIndex == 3 ? 1 : 0);

		if (curveCount <= 1)
		{
			RemoveTrack(ref.trackID);
			return false;
		}

		if (nodeIndex == 0)
		{
			curves.erase(curves.begin());
		}
		else if (nodeIndex == curveCount)
		{
			curves.pop_back();
		}
		else
		{
			// Join the curves either side of the node, then re-smooth below
			curves[nodeIndex - 1].points[2] = curves[nodeIndex].points[2];
			curves[nodeIndex - 1].points[3] = curves[nodeIndex].points[3];
			curves.erase(curves.begin() + nodeIndex);
		}

		glm::vec3 startDir, endDir;
		GetJunctionEndDirs(ref.trackID, startDir, endDir);
		SmoothTrack(ref.trackID, startDir, endDir);
		FindJunctions();
		return true;
	}

	void TrackManager::GetJunctionEndDirs(TrackID trackID, glm::vec3& outStartDir, glm::vec3& outEndDir) const
	{
		outStartDir = VEC3_ZERO;
		outEndDir = VEC3_ZERO;

		const BezierCurveList& track = tracks[(i32)trackID];
		if (track.curves.empty())
		{
			return;
		}

		std::vector<TrackPointRef> coincidentNodes;
		const TrackPointRef ends[2] = { { trackID, 0, 0 }, { trackID, (i32)track.curves.size() - 1, 3 } };
		for (i32 i = 0; i < 2; ++i)
		{
			GetCoincidentNodes(ends[i], coincidentNodes);
			for (const TrackPointRef& node : coincidentNodes)
			{
				if (node.trackID != trackID)
				{
					(i == 0 ? outStartDir : outEndDir) = GetNodeDirection(ends[i]);
					break;
				}
			}
		}
	}

	void TrackManager::SmoothTrack(TrackID trackID, const glm::vec3& startDir, const glm::vec3& endDir)
	{
		BezierCurveList& track = tracks[(i32)trackID];
		if (track.curves.empty())
		{
			return;
		}

		std::vector<glm::vec3> nodes;
		nodes.reserve(track.curves.size() + 1);
		for (const BezierCurve3D& curve : track.curves)
		{
			nodes.push_back(curve.points[0]);
		}
		nodes.push_back(track.curves.back().points[3]);

		track.curves = CreateTrackThroughNodes(nodes, startDir, endDir).curves;
		OnTrackModified(trackID);
	}

	void TrackManager::SetHighlightedTrack(TrackID trackID)
	{
		if (trackID != m_HighlightedTrackID)
		{
			m_HighlightedTrackID = trackID;
			m_HighlightTrackMesh.bDirty = true;
		}
	}

	TrackID TrackManager::GetClosestTrack(const glm::vec3& pos, real range) const
	{
		TrackID result = InvalidTrackID;

		real smallestDistSq = range * range;
		for (i32 t = 0; t < (i32)tracks.size(); ++t)
		{
			for (const BezierCurve3D& curve : tracks[t].curves)
			{
				// Cheaply reject curves whose control points are all far away (curves lie within their control points' hull)
				glm::vec3 minPoint = glm::min(glm::min(curve.points[0], curve.points[1]), glm::min(curve.points[2], curve.points[3])) - glm::vec3(range);
				glm::vec3 maxPoint = glm::max(glm::max(curve.points[0], curve.points[1]), glm::max(curve.points[2], curve.points[3])) + glm::vec3(range);
				if (glm::any(glm::lessThan(pos, minPoint)) || glm::any(glm::greaterThan(pos, maxPoint)))
				{
					continue;
				}

				real curveLength = curve.calculatedLength > 0.0f ? curve.calculatedLength : glm::distance(curve.points[0], curve.points[3]);
				i32 segmentCount = glm::clamp((i32)(curveLength / 0.5f), 4, 512);
				glm::vec3 prevPoint = curve.points[0];
				for (i32 i = 1; i <= segmentCount; ++i)
				{
					glm::vec3 point = curve.GetPointOnCurve((real)i / (real)segmentCount);
					glm::vec3 segment = point - prevPoint;
					real segmentLengthSq = glm::length2(segment);
					real alpha = segmentLengthSq > 0.0f ? Saturate(glm::dot(pos - prevPoint, segment) / segmentLengthSq) : 0.0f;
					real distSq = glm::distance2(pos, prevPoint + segment * alpha);
					if (distSq < smallestDistSq)
					{
						smallestDistSq = distSq;
						result = (TrackID)t;
					}
					prevPoint = point;
				}
			}
		}

		return result;
	}

	void TrackManager::SetPreviewTrack(const BezierCurveList* track)
	{
		if (track == nullptr || track->curves.empty())
		{
			m_bPreviewTrackVisible = false;
			return;
		}

		m_PreviewTrack.curves = track->curves;
		for (BezierCurve3D& curve : m_PreviewTrack.curves)
		{
			curve.CalculateLength();
		}
		m_bPreviewTrackVisible = true;
		m_PreviewTrackMesh.bDirty = true;
	}

	flex::BezierCurveList* TrackManager::GetTrack(TrackID trackID)
	{
		CHECK_LT((i32)trackID, (i32)tracks.size());
		return &tracks[(i32)trackID];
	}

	void TrackManager::OnSceneChanged()
	{
		// Tracks are cleared in OnPreSceneChange, before the new scene loads its tracks
	}

	void TrackManager::DrawDebug()
	{
		PROFILE_AUTO("TrackManager DrawDebug");

		Player* player0 = g_SceneManager->CurrentScene()->GetPlayer(0);
		BezierCurveList* trackRiding = nullptr;
		if (player0 != nullptr && player0->GetTrackRidingID() != InvalidTrackID)
		{
			trackRiding = &tracks[(i32)player0->GetTrackRidingID()];
		}
		real distAlongClosestTrack = -1.0f;
		TrackID closestTrackID = InvalidTrackID;
		if (m_bDrawDebugLines && player0 != nullptr)
		{
			closestTrackID = GetTrackInRangeID(player0->GetTransform()->GetWorldPosition(), player0->GetTrackAttachMinDist(), &distAlongClosestTrack);
		}
		for (i32 i = 0; m_bDrawDebugLines && i < (i32)tracks.size(); ++i)
		{
			btVector4 highlightColour(0.8f, 0.84f, 0.22f, 1.0f);
			real distAlongTrack = -1.0f;
			if (trackRiding)
			{
				if (&tracks[i] == trackRiding)
				{
					distAlongTrack = player0->GetDistAlongTrack();
				}
			}
			else
			{
				if ((i32)closestTrackID == i)
				{
					highlightColour = btVector4(0.75f, 0.65f, 0.75f, 1.0f);
					distAlongTrack = distAlongClosestTrack;
				}
			}
			tracks[i].DrawDebug(highlightColour, distAlongTrack);
		}

		DebugRenderer* debugRenderer = g_Renderer->GetDebugRenderer();

		for (i32 i = 0; i < (i32)junctions.size(); ++i)
		{
			// Without debug lines, only show which way the rider will turn at the upcoming junction
			if (!m_bDrawDebugLines && (trackRiding == nullptr || i != m_PreviewJunctionDir.junctionIndex))
			{
				continue;
			}

			BezierCurveList* track0 = &tracks[junctions[i].trackIndices[0]];
			real distAlongTrack0 = junctions[i].curveIndices[0] / (real)track0->curves.size();
			i32 curveIndex;
			glm::vec3 pos = track0->GetPointOnCurve(distAlongTrack0, &curveIndex);

			if (!m_bDrawDebugLines)
			{
				// Arrow for each way onwards, the one which will be taken is highlighted
				static std::vector<JunctionExit> exits;
				GetJunctionExits(junctions[i], m_PreviewTravelDir, exits);
				const btVector3 chosenCol(0.95f, 0.95f, 0.98f);
				const btVector3 otherCol(0.35f, 0.35f, 0.38f);
				const glm::vec3 arrowStart = pos + VEC3_UP * 1.5f;
				for (const JunctionExit& exit : exits)
				{
					const bool bChosen = NearlyEquals(exit.dir, m_PreviewJunctionDir.dir, 0.01f);
					const btVector3 col = bChosen ? chosenCol : otherCol;
					const glm::vec3 arrowEnd = arrowStart + exit.dir * 4.0f;
					const glm::vec3 side = glm::normalize(glm::cross(VEC3_UP, exit.dir)) * 0.5f;
					debugRenderer->drawLine(ToBtVec3(arrowStart), ToBtVec3(arrowEnd), col);
					debugRenderer->drawLine(ToBtVec3(arrowEnd), ToBtVec3(arrowEnd - exit.dir * 0.8f + side), col);
					debugRenderer->drawLine(ToBtVec3(arrowEnd), ToBtVec3(arrowEnd - exit.dir * 0.8f - side), col);
				}
				continue;
			}

			if (m_bDrawDebugLines)
			{
				btVector3 sphereCol = btVector3(0.9f, 0.2f, 0.2f);
				if (i == m_DEBUG_highlightedJunctionIndex)
				{
					sphereCol = btVector3(0.9f, 0.9f, 0.9f);
				}
				debugRenderer->drawSphere(ToBtVec3(pos), 0.5f, sphereCol);
			}

			for (i32 j = 0; j < junctions[i].trackCount; ++j)
			{
				btVector3 lineColPos = btVector3(0.2f, 0.6f, 0.25f);
				btVector3 lineColNeg = btVector3(0.8f, 0.3f, 0.2f);
				btVector3 lineColPreview = btVector3(0.95f, 0.95f, 0.98f);

				BezierCurveList* track = &tracks[junctions[i].trackIndices[j]];
				curveIndex = junctions[i].curveIndices[j];

				real tAtJunc = track->GetTAtJunction(curveIndex);
				i32 outCurveIdx;
				btVector3 start = ToBtVec3(pos + VEC3_UP * 1.5f);

				if (curveIndex < (i32)track->curves.size())
				{
					glm::vec3 trackP1 = track->GetPointOnCurve(tAtJunc + 0.01f, &outCurveIdx);
					glm::vec3 dir1 = glm::normalize(trackP1 - pos);
					bool bDirsEqual = NearlyEquals(m_PreviewJunctionDir.dir, dir1, 0.1f);
					btVector3 lineCol = ((m_PreviewJunctionDir.junctionIndex == i && bDirsEqual) ? lineColPreview : lineColPos);
					debugRenderer->drawLine(start, ToBtVec3(pos + dir1 * 5.0f + VEC3_UP * 1.5f), lineCol);
				}
				if (curveIndex > 0)
				{
					glm::vec3 trackP2 = track->GetPointOnCurve(tAtJunc - 0.01f, &outCurveIdx);
					glm::vec3 dir2 = glm::normalize(trackP2 - pos);
					bool bDirsEqual = NearlyEquals(m_PreviewJunctionDir.dir, dir2, 0.1f);
					btVector3 lineCol = ((m_PreviewJunctionDir.junctionIndex == i && bDirsEqual) ? lineColPreview : lineColNeg);
					debugRenderer->drawLine(start, ToBtVec3(pos + dir2 * 5.0f + VEC3_UP * 1.5f), lineCol);
				}
			}
		}
	}

	void TrackManager::GetJunctionExits(const Junction& junction, const glm::vec3& travelDir, std::vector<JunctionExit>& outExits) const
	{
		outExits.clear();

		for (i32 j = 0; j < junction.trackCount; ++j)
		{
			const TrackID exitTrackID = (TrackID)junction.trackIndices[j];
			const BezierCurveList& exitTrack = tracks[(i32)exitTrackID];
			const i32 curveCount = (i32)exitTrack.curves.size();
			const i32 curveIndex = junction.curveIndices[j];
			const real t = (real)curveIndex / (real)curveCount;

			// A junction can lead both ways along a track (when it's not at an end)
			for (i32 side = 0; side < 2; ++side)
			{
				const bool bForwards = (side == 0);
				if ((bForwards && curveIndex >= curveCount) || (!bForwards && curveIndex <= 0))
				{
					continue;
				}

				// Tracks branching off a node start out parallel to it (so the join is smooth), so their tangents at the
				// junction can't tell branches apart. Instead use the direction to a point a little way along each exit
				const real lookAheadDist = 4.0f;
				const BezierCurve3D& exitCurve = exitTrack.curves[bForwards ? curveIndex : curveIndex - 1];
				const real curveLength = exitCurve.calculatedLength > 0.0f ? exitCurve.calculatedLength : glm::distance(exitCurve.points[0], exitCurve.points[3]);
				const real lookAheadT = curveLength > 0.0f ? glm::clamp(lookAheadDist / curveLength, 0.05f, 1.0f) : 1.0f;
				const glm::vec3 junctionPoint = bForwards ? exitCurve.points[0] : exitCurve.points[3];
				const glm::vec3 aheadPoint = exitCurve.GetPointOnCurve(bForwards ? lookAheadT : 1.0f - lookAheadT);
				glm::vec3 dir = aheadPoint - junctionPoint;
				if (glm::length2(dir) < 0.000001f)
				{
					continue;
				}
				dir = glm::normalize(dir);

				// Only exits continuing onwards, which excludes the way we came in
				if (glm::dot(dir, travelDir) < 0.1f)
				{
					continue;
				}

				JunctionExit exit;
				exit.trackID = exitTrackID;
				exit.t = t;
				exit.travelSign = bForwards ? 1.0f : -1.0f;
				exit.dir = dir;
				outExits.push_back(exit);
			}
		}
	}

	i32 TrackManager::ChooseJunctionExit(const std::vector<JunctionExit>& exits, const glm::vec3& travelDir, real steer)
	{
		// With no steering take the straightest path, otherwise the exit furthest to that side
		const glm::vec3 right = glm::normalize(glm::cross(VEC3_UP, travelDir));
		const glm::vec3 preferredDir = (steer == 0.0f) ? travelDir : right * glm::sign(steer);

		i32 bestIndex = -1;
		real bestDot = -2.0f;
		for (i32 i = 0; i < (i32)exits.size(); ++i)
		{
			real dot = glm::dot(exits[i].dir, preferredDir);
			if (dot > bestDot)
			{
				bestDot = dot;
				bestIndex = i;
			}
		}
		return bestIndex;
	}

	glm::vec3 TrackManager::GetPointOnTrack(TrackID trackID,
		real distAlongTrack,
		real pDistAlongTrack,
		LookDirection desiredDir,
		bool bMovingBackwards,
		TrackID* outNewTrackID,
		real* outNewDistAlongTrack,
		i32* outJunctionIndex,
		i32* outCurveIndex,
		TrackState* outTrackState,
		bool bPrint,
		glm::vec3* outExitDir /* = nullptr */,
		i32* outForwardExitCount /* = nullptr */)
	{
		BezierCurveList* track = &tracks[(i32)trackID];
		TrackID newTrackID = InvalidTrackID;
		real newDistAlongTrack = -1.0f;
		i32 pCurveIdx = -1;
		i32 newCurveIdx = -1;
		i32 junctionIndex = -1;
		TrackState newTrackState = TrackState::_NONE;

		glm::vec3 pPoint = track->GetPointOnCurve(pDistAlongTrack, &pCurveIdx);
		glm::vec3 newPoint = track->GetPointOnCurve(distAlongTrack, &newCurveIdx);

		const bool bReachedEndOfTheLine =
			(distAlongTrack <= 0.0f && pDistAlongTrack > 0.0f) ||
			(distAlongTrack >= 1.0f && pDistAlongTrack < 1.0f);
		const bool bChangedCurve = newCurveIdx != pCurveIdx;
		if (bChangedCurve || bReachedEndOfTheLine)
		{
			i32 nextJunctionCurveIdx = newCurveIdx;
			if (newCurveIdx < pCurveIdx)
			{
				nextJunctionCurveIdx = pCurveIdx;
			}
			else if (newCurveIdx == pCurveIdx)
			{
				if (distAlongTrack == 0.0f)
				{
					nextJunctionCurveIdx = 0;
				}
				else
				{
					nextJunctionCurveIdx = (i32)track->curves.size();
				}
			}
			const glm::vec3 nextJunctionPos = track->GetPointAtJunction(nextJunctionCurveIdx);

			glm::vec3 travelDir = newPoint - pPoint;
			if (glm::length2(travelDir) < 0.000001f)
			{
				travelDir = track->GetCurveDirectionAt(distAlongTrack) * (distAlongTrack >= pDistAlongTrack ? 1.0f : -1.0f);
			}
			travelDir = glm::normalize(travelDir);

			// Check for junction crossings
			for (i32 i = 0; i < (i32)junctions.size() && junctionIndex == -1; ++i)
			{
				Junction& junction = junctions[i];

				bool bOnThisJunction = false;
				for (i32 j = 0; j < junction.trackCount; ++j)
				{
					bOnThisJunction = bOnThisJunction || ((TrackID)junction.trackIndices[j] == trackID);
				}
				if (!bOnThisJunction || !NearlyEquals(junction.pos, nextJunctionPos, JUNCTION_THRESHOLD_DIST))
				{
					continue;
				}

				junctionIndex = i;

				const real steer = (desiredDir == LookDirection::LEFT ? -1.0f : desiredDir == LookDirection::RIGHT ? 1.0f : 0.0f);
				static std::vector<JunctionExit> exits;
				GetJunctionExits(junction, travelDir, exits);
				i32 exitIndex = ChooseJunctionExit(exits, travelDir, steer);
				if (outForwardExitCount != nullptr)
				{
					*outForwardExitCount = (i32)exits.size();
				}
				if (exitIndex == -1)
				{
					if (bPrint) Print("End of the line, no exits from junction\n");
					continue;
				}

				const JunctionExit& exit = exits[exitIndex];
				if (outExitDir != nullptr)
				{
					*outExitDir = exit.dir;
				}

				if (exit.trackID == trackID)
				{
					// Carrying straight on along the current track
					if (bPrint) Print("Stayed on track at junction\n");
					continue;
				}

				if (bPrint) Print("Changed to track %u at junction\n", (u32)exit.trackID);

				newTrackID = exit.trackID;
				newCurveIdx = glm::clamp((i32)(exit.t * tracks[(i32)newTrackID].curves.size()), 0, (i32)tracks[(i32)newTrackID].curves.size() - 1);
				// Nudge off the junction so it isn't immediately crossed again
				newDistAlongTrack = Saturate(exit.t + exit.travelSign * 0.001f);

				// Face the way we're travelling, unless moving backwards. FACING_BACKWARD faces towards increasing t
				const real facingSign = exit.travelSign * (bMovingBackwards ? -1.0f : 1.0f);
				newTrackState = (facingSign > 0.0f ? TrackState::FACING_BACKWARD : TrackState::FACING_FORWARD);
			}

			if (newTrackID == InvalidTrackID && newDistAlongTrack == -1.0f)
			{
				newDistAlongTrack = Saturate(distAlongTrack);
			}
		}
		else
		{
			newDistAlongTrack = distAlongTrack;
		}

		if (newTrackID == InvalidTrackID)
		{
			newTrackID = trackID;
		}

		*outCurveIndex = newCurveIdx;
		*outNewTrackID = newTrackID;
		*outNewDistAlongTrack = newDistAlongTrack;
		*outJunctionIndex = junctionIndex;
		*outTrackState = newTrackState;

		return newPoint;
	}

	void TrackManager::UpdatePreview(TrackID trackID,
		real distAlongTrack,
		LookDirection desiredDir,
		real travelSign,
		bool bMovingBackwards)
	{
		m_PreviewJunctionDir.junctionIndex = -1;
		m_PreviewJunctionDir.dir = VEC3_ZERO;
		m_PreviewTravelDir = VEC3_ZERO;
		m_PreviewForwardExitCount = 0;

		BezierCurveList* track = &tracks[(i32)trackID];
		const i32 curveCount = (i32)track->curves.size();
		if (curveCount == 0)
		{
			return;
		}

		// Junctions can only be at nodes, so look at the next one in the direction of travel
		const real scaledDist = distAlongTrack * curveCount;
		const i32 nextNodeIndex = travelSign > 0.0f ? (i32)glm::floor(scaledDist) + 1 : (i32)glm::ceil(scaledDist) - 1;
		if (nextNodeIndex < 0 || nextNodeIndex > curveCount)
		{
			return;
		}

		const real maxPreviewDist = 20.0f;
		i32 curveIndex;
		const glm::vec3 currentPos = track->GetPointOnCurve(distAlongTrack, &curveIndex);
		const glm::vec3 nodePos = track->GetPointAtJunction(nextNodeIndex);
		if (glm::distance2(currentPos, nodePos) > maxPreviewDist * maxPreviewDist)
		{
			return;
		}

		const real queryDist = Saturate((real)nextNodeIndex / (real)curveCount + travelSign * 0.001f);
		TrackID newTrackID = InvalidTrackID;
		real newDist = -1.0f;
		i32 junctionIndex = -1;
		TrackState newTrackState = TrackState::_NONE;
		glm::vec3 exitDir = VEC3_ZERO;
		i32 forwardExitCount = 0;
		GetPointOnTrack(trackID, queryDist, distAlongTrack, desiredDir, bMovingBackwards,
			&newTrackID, &newDist, &junctionIndex, &curveIndex, &newTrackState, false, &exitDir, &forwardExitCount);

		if (junctionIndex != -1)
		{
			m_PreviewJunctionDir.junctionIndex = junctionIndex;
			m_PreviewJunctionDir.dir = exitDir;
			m_PreviewTravelDir = glm::normalize(track->GetCurveDirectionAt(distAlongTrack) * travelSign);
			m_PreviewForwardExitCount = forwardExitCount;
		}
	}

	bool TrackManager::IsForkAhead() const
	{
		return m_PreviewJunctionDir.junctionIndex != -1 && m_PreviewForwardExitCount >= 2;
	}

	bool TrackManager::GetPointInRange(const glm::vec3& p, bool bIncludeHandles, real range, glm::vec3* outPoint)
	{
		real smallestDist = range;
		bool bFoundPointInRange = false;
		for (const BezierCurveList& track : tracks)
		{
			for (const BezierCurve3D& curve : track.curves)
			{
				for (i32 i = 0; i < 4; i += (bIncludeHandles ? 1 : 3))
				{
					glm::vec3 curveP = curve.points[i];
					real dist = glm::distance(p, curveP);
					if (dist < smallestDist)
					{
						bFoundPointInRange = true;
						smallestDist = dist;
						*outPoint = curveP;
					}
				}
			}
		}

		return bFoundPointInRange;
	}

	bool TrackManager::GetPointInRange(const glm::vec3& p, real range, TrackID* outTrackID, i32* outCurveIndex, i32* outPointIdx)
	{
		real smallestDist = range;
		bool bFoundPointInRange = false;
		for (i32 t = 0; t < (i32)tracks.size(); ++t)
		{
			for (i32 c = 0; c < (i32)tracks[t].curves.size(); ++c)
			{
				for (i32 i = 0; i < 4; ++i)
				{
					glm::vec3 curveP = tracks[t].curves[c].points[i];
					real dist = glm::distance(p, curveP);
					if (dist < smallestDist)
					{
						bFoundPointInRange = true;
						smallestDist = dist;
						*outTrackID = (TrackID)t;
						*outCurveIndex = c;
						*outPointIdx = i;
					}
				}
			}
		}

		return bFoundPointInRange;
	}

	void TrackManager::FindJunctions()
	{
		// Clear previous junctions
		junctions.clear();

		for (i32 i = 0; i < (i32)tracks.size(); ++i)
		{
			const std::vector<BezierCurve3D>& curvesA = tracks[i].curves;

			for (i32 j = i + 1; j < (i32)tracks.size(); ++j)
			{
				const std::vector<BezierCurve3D>& curvesB = tracks[j].curves;

				for (i32 k = 0; k < (i32)curvesA.size(); ++k)
				{
					for (i32 m = 0; m < (i32)curvesB.size(); ++m)
					{
						for (i32 p1 = 0; p1 < 2; ++p1)
						{
							for (i32 p2 = 0; p2 < 2; ++p2)
							{
								// 0 on first iteration, 3 on second (first and last points)
								i32 p1Idx = p1 * 3;
								i32 p2Idx = p2 * 3;

								if (NearlyEquals(curvesA[k].points[p1Idx], curvesB[m].points[p2Idx], JUNCTION_THRESHOLD_DIST))
								{
									i32 curveIndexA = (p1Idx == 3 ? k + 1 : k);
									i32 curveIndexB = (p2Idx == 3 ? m + 1 : m);

									bool bJunctionExists = false;
									for (Junction& junc : junctions)
									{
										// Junction already exists at this location, check if this track is a part of it
										if (NearlyEquals(junc.pos, curvesA[k].points[p1Idx], JUNCTION_THRESHOLD_DIST))
										{
											bJunctionExists = true;
											i32 trackIndex = -1;
											for (i32 o = 0; o < junc.trackCount; ++o)
											{
												if (junc.trackIndices[o] == j)
												{
													trackIndex = o;
													break;
												}
											}

											if (trackIndex == -1)
											{
												junc.trackIndices[junc.trackCount] = j;
												junc.curveIndices[junc.trackCount] = curveIndexB;
												junc.trackCount++;
											}

											break;
										}
									}

									if (!bJunctionExists)
									{
										Junction newJunc = {};
										newJunc.pos = curvesA[k].points[p1Idx];
										newJunc.trackIndices[newJunc.trackCount] = i;
										newJunc.curveIndices[newJunc.trackCount] = curveIndexA;
										newJunc.trackCount++;
										newJunc.trackIndices[newJunc.trackCount] = j;
										newJunc.curveIndices[newJunc.trackCount] = curveIndexB;
										newJunc.trackCount++;
										junctions.push_back(newJunc);
									}
								}
							}
						}
					}
				}
			}
		}
	}

	bool TrackManager::IsTrackInRange(TrackID trackID,
		const glm::vec3& pos,
		real range,
		real* outDistToTrack,
		real* outDistAlongTrack)
	{
		const BezierCurveList* track = &tracks[(i32)trackID];

		// Let's brute force it baby
		i32 sampleCount = 25;

		bool bInRange = false;
		real smallestSqDist = range*range;
		// TODO: Pre-compute AABBs for each curve for early pruning
		for (i32 i = 0; i <= sampleCount; ++i)
		{
			real t = (real)i / (real)(sampleCount);
			i32 curveIndex;
			real distSq = glm::distance2(track->GetPointOnCurve(t, &curveIndex), pos);
			if (distSq < smallestSqDist)
			{
				smallestSqDist = distSq;
				*outDistAlongTrack = t;
				*outDistToTrack = glm::sqrt(smallestSqDist);
				bInRange = true;
			}
		}

		return bInRange;
	}

	TrackID TrackManager::GetTrackInRangeID(const glm::vec3& pos, real range, real* outDistAlongTrack)
	{
		TrackID trackID = InvalidTrackID;

		real smallestDist = range;
		for (i32 i = 0; i < (i32)tracks.size(); ++i)
		{
			if (IsTrackInRange((TrackID)i, pos, range, &smallestDist, outDistAlongTrack))
			{
				range = smallestDist;
				trackID = (TrackID)i;
			}
		}

		return trackID;
	}

	real TrackManager::AdvanceTAlongTrack(TrackID trackID, real amount, real t)
	{
		const BezierCurveList* track = &tracks[(i32)trackID];
		i32 startCurveIndex;
		real oldLocalT = 0.0f;
		track->GetCurveIndexAndLocalTFromGlobalT(t, &startCurveIndex, &oldLocalT);

		real startCurveLen = track->curves[startCurveIndex].calculatedLength;
		if (startCurveLen == -1.0f)
		{
			PrintWarn("TrackManager::AdvanceTAlongTrack > Curve length hasn't been calculated\n!");
		}

		static const real LENGTH_SCALE = 100.0f;
		real newLocalT = oldLocalT + amount * (LENGTH_SCALE / startCurveLen);

		real newGlobalT = track->GetGlobalTFromCurveIndexAndLocalT(startCurveIndex, newLocalT);
		return newGlobalT;
	}

	real TrackManager::GetCartTargetDistAlongTrackInChain(CartChainID cartChainID, CartID cartID) const
	{
		real targetT = -1.0f;

		CartManager* cartManager = GetSystem<CartManager>(SystemType::CART_MANAGER);
		CartChain* cartChain = cartManager->GetCartChain(cartChainID);
		i32 cartIndex = cartChain->GetCartIndex(cartID);
		if (cartIndex == 0)
		{
			targetT = cartChain->GetCartAtIndexDistAlongTrack(1);
			//targetT = cartManager->GetCart(cartID)->distAlongTrack + cartManager->GetChainDrivePower(cartChainID);
		}
		else
		{
			targetT = cartChain->GetCartAtIndexDistAlongTrack(cartIndex - 1);
		}

		return targetT;
	}
} // namespace flex
