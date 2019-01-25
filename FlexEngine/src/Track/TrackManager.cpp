
#include "stdafx.hpp"

#include "Track/TrackManager.hpp"

#pragma warning(push, 0)
#include "LinearMath/btIDebugDraw.h"

#include <glm/gtx/norm.hpp> // For distance2
#pragma warning(pop)

#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"
#include "Player.hpp"
#include "PlayerController.hpp"
#include "Profiler.hpp"
#include "Scene/BaseScene.hpp"
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

	TrackManager::TrackManager(BaseScene* owningScene) :
		m_OwningScene(owningScene)
	{
		m_PreviewJunctionDir.junctionIndex = -1;
		m_PreviewJunctionDir.dir = VEC3_ZERO;
	}

	void TrackManager::InitializeFromJSON(const JSONObject& obj)
	{
		const JSONObject& tracksObj = obj.GetObject("tracks");

		for (const JSONField& field : tracksObj.fields)
		{
			m_Tracks.push_back(BezierCurveList::InitializeFromJSON(field.value.objectValue));
		}

		FindJunctions();
	}

	TrackID TrackManager::AddTrack(const BezierCurveList& track)
	{
		m_Tracks.push_back(track);
		return (TrackID)m_Tracks.size();
	}

	flex::BezierCurveList* TrackManager::GetTrack(TrackID trackID)
	{
		assert((i32)trackID < (i32)m_Tracks.size());
		return &m_Tracks[(i32)trackID];
	}

	void TrackManager::Update()
	{
		
	}

	glm::vec3 TrackManager::GetPointOnTrack(TrackID trackID,
		real distAlongTrack,
		real pDistAlongTrack,
		LookDirection desiredDir,
		bool bReversingDownTrack,
		TrackID* outNewTrackID,
		real* outNewDistAlongTrack,
		i32* outJunctionIndex,
		i32* outCurveIndex,
		TrackState* outTrackState,
		bool bPrint)
	{
		BezierCurveList* track = &m_Tracks[(i32)trackID];
		TrackID newTrackID = InvalidTrackID;
		real newDistAlongTrack = -1.0f;
		i32 pCurveIdx = -1;
		i32 newCurveIdx = -1;
		i32 junctionIndex = -1;
		TrackState newTrackState = TrackState::NONE;

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

			glm::vec3 trackForward = track->GetCurveDirectionAt(distAlongTrack);
			trackForward = glm::normalize(newPoint - pPoint);
			glm::vec3 trackRight = glm::cross(trackForward, VEC3_UP);

			// Check for junction crossings
			for (i32 i = 0; i < (i32)m_Junctions.size() && newTrackID == InvalidTrackID; ++i)
			{
				Junction& junction = m_Junctions[i];

				for (i32 j = 0; j < junction.trackCount && newTrackID == InvalidTrackID; ++j)
				{
					if ((TrackID)junction.trackIndices[j] == trackID &&
						NearlyEquals(junction.pos, nextJunctionPos, JUNCTION_THRESHOLD_DIST))
					{
						junctionIndex = i;

						i32 newTrackIdx = -1;

						if (junction.trackCount == 2)
						{
							if (bReachedEndOfTheLine)
							{
								newTrackIdx = ((TrackID)junction.trackIndices[0] == trackID ? 1 : 0);
								if (bPrint) Print("Changed to only other track at junction (it's the end of the line baby)\n");
							}
							else
							{
								if (desiredDir == LookDirection::CENTER)
								{
									if (bPrint) Print("Went straight through a 2-way junction\n");
								}
								else
								{
									newTrackIdx = ((TrackID)junction.trackIndices[0] == trackID ? 1 : 0);
									if (bPrint) Print("Changed to only other track at junction\n");
								}
							}
						}
						else
						{
							if (desiredDir == LookDirection::CENTER)
							{
								if (bReachedEndOfTheLine)
								{
									glm::vec3 dir = glm::normalize(newPoint - pPoint);
									newTrackIdx = GetTrackIndexInDir(dir, junction, trackID, bReachedEndOfTheLine);
									if (bPrint) Print("Went straight through an n-way junction (jumped tracks)\n");
								}
								else
								{
									if (bPrint) Print("Went straight through an n-way junction (same track)\n");
								}
							}
							else
							{
								if (desiredDir == LookDirection::LEFT)
								{
									// Want to go left
									glm::vec3 desiredDirVec = -trackRight;
									if (bReversingDownTrack)
									{
										desiredDirVec = -desiredDirVec;
									}
									newTrackIdx = GetTrackIndexInDir(desiredDirVec, junction, trackID, bReachedEndOfTheLine);
									if (newTrackIdx != -1)
									{
										if (bPrint) Print("Went left at an n-way junction\n");
									}
								}
								else if (desiredDir == LookDirection::RIGHT)
								{
									// Want to go right
									glm::vec3 desiredDirVec = trackRight;
									if (bReversingDownTrack)
									{
										desiredDirVec = -desiredDirVec;
									}
									newTrackIdx = GetTrackIndexInDir(desiredDirVec, junction, trackID, bReachedEndOfTheLine);
									if (newTrackIdx != -1)
									{
										if (bPrint) Print("Went right at an n-way junction\n");
									}
								}
								else
								{
									if (bPrint) Print("Went straight through an n-way junction\n");
								}
							}
						}

						if (newTrackIdx != -1)
						{
							newTrackID = junction.trackIndices[newTrackIdx];
							newCurveIdx = junction.curveIndices[newTrackIdx];
							newDistAlongTrack = (real)newCurveIdx / (real)(m_Tracks[(i32)newTrackID].curves.size());
							glm::vec3 desiredDirVec =
								(desiredDir == LookDirection::CENTER ? glm::normalize(newPoint - pPoint) :
									desiredDir == LookDirection::RIGHT ? trackRight : -trackRight);
							if (desiredDir == LookDirection::CENTER && bReversingDownTrack)
							{
								desiredDirVec = -desiredDirVec;
							}
							newTrackState = GetTrackStateInDir(desiredDirVec, junction.trackIndices[newTrackIdx], newDistAlongTrack, bReversingDownTrack);
							if (bReversingDownTrack)
							{
								if (newTrackState == TrackState::FACING_FORWARD)
								{
									newDistAlongTrack -= 0.001f;
								}
								else if (newTrackState == TrackState::FACING_BACKWARD)
								{
									newDistAlongTrack += 0.001f;
								}
								else
								{
									PrintWarn("Unexpected track state when desired dir != 1\n");
								}
							}
							else
							{
								if (newTrackState == TrackState::FACING_FORWARD)
								{
									newDistAlongTrack += 0.001f;
								}
								else if (newTrackState == TrackState::FACING_BACKWARD)
								{
									newDistAlongTrack -= 0.001f;
								}
								else
								{
									PrintWarn("Unexpected track state when desired dir != 1\n");
								}
							}
							newDistAlongTrack = glm::clamp(newDistAlongTrack, 0.0f, 1.0f);
						}
					}
				}
			}

			if (newTrackID == InvalidTrackID && newDistAlongTrack == -1.0f)
			{
				newDistAlongTrack = glm::clamp(distAlongTrack, 0.0f, 1.0f);
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
		glm::vec3 currentFor,
		bool bFacingForwardDownTrack,
		bool bReversingDownTrack)
	{
		BezierCurveList* track = &m_Tracks[(i32)trackID];
		// TODO: Use real-world distance rather than [0-1] spine dist
		// TODO: Ensure not stepping over multiple junctions
		real range = 0.2f;
		real dotResult = glm::dot(track->GetCurveDirectionAt(distAlongTrack), currentFor);
		real queryDist = distAlongTrack;
		bool bFacingDownTrack = dotResult > 0.0f;
		if (bFacingDownTrack)
		{
			queryDist += range;
		}
		else
		{
			queryDist -= range;
		}
		queryDist = glm::clamp(queryDist, 0.0f, 1.0f);
		TrackID newTrackID = InvalidTrackID;
		real newDist = -1.0f;
		i32 junctionIndex = -1;
		i32 curveIndex = -1;
		TrackState newTrackState = TrackState::NONE;
		glm::vec3 newPoint = GetPointOnTrack(trackID, queryDist, distAlongTrack, desiredDir, bReversingDownTrack, &newTrackID, &newDist, &junctionIndex, &curveIndex, &newTrackState, false);

		if (junctionIndex == -1)
		{
			// No junctions in range
			m_PreviewJunctionDir.junctionIndex = -1;
			m_PreviewJunctionDir.dir = VEC3_ZERO;
		}
		else
		{
			// There is a junction in range
			m_PreviewJunctionDir.junctionIndex = junctionIndex;
			real dist = (newDist == -1.0f ? distAlongTrack : newDist);
			if (newTrackID == InvalidTrackID)
			{
				m_PreviewJunctionDir.dir = track->GetCurveDirectionAt(dist);
			}
			else
			{
				m_PreviewJunctionDir.dir = m_Tracks[(i32)newTrackID].GetCurveDirectionAt(dist);
			}

			if (newTrackState == TrackState::FACING_BACKWARD ||
				(!bFacingForwardDownTrack && newTrackID == InvalidTrackID))
			{
				m_PreviewJunctionDir.dir = -m_PreviewJunctionDir.dir;
			}
		}
	}

	bool TrackManager::GetPointInRange(const glm::vec3& p, bool bIncludeHandles, real range, glm::vec3* outPoint)
	{
		real smallestDist = range;
		bool bFoundPointInRange = false;
		for (const BezierCurveList& track : m_Tracks)
		{
			for (const BezierCurve& curve : track.curves)
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
		for (i32 t = 0; t < (i32)m_Tracks.size(); ++t)
		{
			for (i32 c = 0; c < (i32)m_Tracks[t].curves.size(); ++c)
			{
				for (i32 i = 0; i < 4; ++i)
				{
					glm::vec3 curveP = m_Tracks[t].curves[c].points[i];
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

	// TODO: Remove final param in place of junc.pos?
	i32 TrackManager::GetTrackIndexInDir(const glm::vec3& desiredDir,
		Junction& junc,
		TrackID trackID,
		bool bEndOfTheLine)
	{
		i32 newTrackIdx = -1;

		i32 bestTrackIdx = -1;
		real bestTrackDot = -1.0f;
		for (i32 k = 0; k < junc.trackCount; ++k)
		{
			if ((TrackID)junc.trackIndices[k] != trackID)
			{
				BezierCurveList* testTrack = &m_Tracks[junc.trackIndices[k]];
				i32 testTrackCurveCount = (i32)testTrack->curves.size();

				i32 testCurveIdx = 0;
				glm::vec3 testPoint;
				bool bFoundPoint = false;
				for (i32 m = 0; m <= testTrackCurveCount; ++m)
				{
					testPoint = testTrack->GetPointAtJunction(m);
					if (NearlyEquals(testPoint, junc.pos, JUNCTION_THRESHOLD_DIST))
					{
						testCurveIdx = m;
						bFoundPoint = true;
						break;
					}
				}

				if (!bFoundPoint)
				{
					continue;
				}

				static const real TEST_DIST = 0.01f;
				real testDist = TEST_DIST;
				if (testCurveIdx == 0)
				{
					testDist = TEST_DIST;
				}
				else if (testCurveIdx == testTrackCurveCount)
				{
					testDist = 1.0f - TEST_DIST;
				}
				else
				{
					real distToJunc = (real)testCurveIdx / (real)(testTrackCurveCount);
					glm::vec3 dirAtJunc = testTrack->GetCurveDirectionAt(distToJunc);
					real dotResult = glm::dot(dirAtJunc, desiredDir);
					if (dotResult > 0.0f)
					{
						testDist = distToJunc + TEST_DIST;
					}
					else
					{
						testDist = distToJunc - TEST_DIST;
					}
				}
				i32 testPosCurveIdx = -1;
				glm::vec3 testPos = testTrack->GetPointOnCurve(testDist, &testPosCurveIdx);
				glm::vec3 dPos = glm::normalize(testPos - junc.pos); // not newPoint?
				real dotResult = glm::dot(dPos, desiredDir);
				if (dotResult > 0.0f && dotResult > bestTrackDot)
				{
					bestTrackDot = dotResult;
					bestTrackIdx = k;
				}
			}
		}

		if (bestTrackIdx == -1)
		{
			if (bEndOfTheLine)
			{
				// Just stay on current track I guess?
				newTrackIdx = -1;
			}
			else
			{
				// Let's stay on this track, there isn't a track to the left
				newTrackIdx = -1;
			}
		}
		else
		{
			newTrackIdx = bestTrackIdx;
		}

		return newTrackIdx;
	}

	TrackState TrackManager::GetTrackStateInDir(const glm::vec3& desiredDir,
		TrackID trackID,
		real distAlongTrack,
		bool bReversing)
	{
		bool bFacingDownTrack = m_Tracks[(i32)trackID].IsVectorFacingDownTrack(distAlongTrack, desiredDir);

		if (bReversing)
		{
			if (bFacingDownTrack)
			{
				return TrackState::FACING_BACKWARD;
			}
			else
			{
				return TrackState::FACING_FORWARD;
			}
		}
		else
		{
			if (bFacingDownTrack)
			{
				return TrackState::FACING_BACKWARD;
			}
			else
			{
				return TrackState::FACING_FORWARD;
			}
		}
	}

	void TrackManager::FindJunctions()
	{
		// Clear previous junctions
		m_Junctions.clear();

		for (i32 i = 0; i < (i32)m_Tracks.size(); ++i)
		{
			const std::vector<BezierCurve>& curvesA = m_Tracks[i].curves;

			for (i32 j = i + 1; j < (i32)m_Tracks.size(); ++j)
			{
				const std::vector<BezierCurve>& curvesB = m_Tracks[j].curves;

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
									for (Junction& junc : m_Junctions)
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
										m_Junctions.push_back(newJunc);
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
		const BezierCurveList* track = &m_Tracks[(i32)trackID];

		// Let's brute force it baby
		i32 sampleCount = 250;

		bool bInRange = false;
		real smallestDist = range;
		// TODO: Pre-compute AABBs for each curve for early pruning
		for (i32 i = 0; i <= sampleCount; ++i)
		{
			real t = (real)i / (real)(sampleCount);
			i32 curveIndex;
			real dist = glm::distance(track->GetPointOnCurve(t, &curveIndex), pos);
			if (dist < smallestDist)
			{
				smallestDist = dist;
				*outDistAlongTrack = t;
				*outDistToTrack = smallestDist;
				bInRange = true;
			}
		}

		return bInRange;
	}

	TrackID TrackManager::GetTrackInRangeID(const glm::vec3& pos, real range, real* outDistAlongTrack)
	{
		TrackID trackID = InvalidTrackID;

		real smallestDist = range;
		for (i32 i = 0; i < (i32)m_Tracks.size(); ++i)
		{
			if (IsTrackInRange((TrackID)i, pos, range, &smallestDist, outDistAlongTrack))
			{
				range = smallestDist;
				trackID = (TrackID)i;
			}
		}

		return trackID;
	}

	void TrackManager::DrawDebug()
	{
		PROFILE_AUTO("TrackManager DrawDebug");

		Player* m_Player0 = g_SceneManager->CurrentScene()->GetPlayer(0);
		BezierCurveList* trackRiding = (m_Player0->m_TrackRidingID == InvalidTrackID ? nullptr : &m_Tracks[(i32)m_Player0->m_TrackRidingID]);
		real distAlongClosestTrack = -1.0f;
		TrackID closestTrackID = GetTrackInRangeID(m_Player0->GetTransform()->GetWorldPosition(),
			m_Player0->m_TrackAttachMinDist, &distAlongClosestTrack);
		for (i32 i = 0; i < (i32)m_Tracks.size(); ++i)
		{
			btVector4 highlightColour(0.8f, 0.84f, 0.22f, 1.0f);
			real distAlongTrack = -1.0f;
			if (trackRiding)
			{
				if (&m_Tracks[i] == trackRiding)
				{
					distAlongTrack = m_Player0->GetDistAlongTrack();
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
			m_Tracks[i].DrawDebug(highlightColour, distAlongTrack);
		}

		btIDebugDraw* debugDrawer = g_Renderer->GetDebugDrawer();

		for (i32 i = 0; i < (i32)m_Junctions.size(); ++i)
		{
			BezierCurveList* track0 = &m_Tracks[m_Junctions[i].trackIndices[0]];
			 real distAlongTrack0 = m_Junctions[i].curveIndices[0] / (real)track0->curves.size();
			 i32 curveIndex;
			 glm::vec3 pos = track0->GetPointOnCurve(distAlongTrack0, &curveIndex);
			 btVector3 sphereCol = btVector3(0.9f, 0.2f, 0.2f);
			 if (i == m_DEBUG_highlightedJunctionIndex)
			 {
				 sphereCol = btVector3(0.9f, 0.9f, 0.9f);
			 }
			 debugDrawer->drawSphere(ToBtVec3(pos), 0.5f, sphereCol);

			 for (i32 j = 0; j < m_Junctions[i].trackCount; ++j)
			 {
				 btVector3 lineColPos = btVector3(0.2f, 0.6f, 0.25f);
				 btVector3 lineColNeg = btVector3(0.8f, 0.3f, 0.2f);
				 btVector3 lineColPreview = btVector3(0.95f, 0.95f, 0.98f);

				 BezierCurveList* track = &m_Tracks[m_Junctions[i].trackIndices[j]];
				 curveIndex = m_Junctions[i].curveIndices[j];

				 real tAtJunc = track->GetTAtJunction(curveIndex);
				 i32 outCurveIdx;
				 btVector3 start = ToBtVec3(pos + VEC3_UP * 1.5f);

				 if (curveIndex < (i32)track->curves.size())
				 {
					 glm::vec3 trackP1 = track->GetPointOnCurve(tAtJunc + 0.01f, &outCurveIdx);
					 glm::vec3 dir1 = glm::normalize(trackP1 - pos);
					 bool bDirsEqual = NearlyEquals(m_PreviewJunctionDir.dir, dir1, 0.1f);
					 btVector3 lineCol = ((m_PreviewJunctionDir.junctionIndex == i && bDirsEqual) ? lineColPreview : lineColPos);
					 debugDrawer->drawLine(start, ToBtVec3(pos + dir1 * 5.0f + VEC3_UP * 1.5f), lineCol);
				 }
				 if (curveIndex > 0)
				 {
					 glm::vec3 trackP2 = track->GetPointOnCurve(tAtJunc - 0.01f, &outCurveIdx);
					 glm::vec3 dir2 = glm::normalize(trackP2 - pos);
					 bool bDirsEqual = NearlyEquals(m_PreviewJunctionDir.dir, dir2, 0.1f);
					 btVector3 lineCol = ((m_PreviewJunctionDir.junctionIndex == i && bDirsEqual) ? lineColPreview : lineColNeg);
					 debugDrawer->drawLine(start, ToBtVec3(pos + dir2 * 5.0f + VEC3_UP * 1.5f), lineCol);
				 }
			 }
		}
	}

	void TrackManager::DrawImGuiObjects()
	{
		if (ImGui::TreeNode("Track Manager"))
		{
			ImGui::Text("%d tracks, %d junctions", m_Tracks.size(), m_Junctions.size());
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
				m_DEBUG_highlightedJunctionIndex = glm::min(m_DEBUG_highlightedJunctionIndex, (i32)m_Junctions.size() - 1);
			}

			if (m_DEBUG_highlightedJunctionIndex != -1)
			{
				ImGui::Text("Junction connected track count: %i", m_Junctions[m_DEBUG_highlightedJunctionIndex].trackCount);
			}

			ImGui::Text("Preview junc idx: %i", m_PreviewJunctionDir.junctionIndex);
			ImGui::Text("Preview curve dir: %s", Vec3ToString(m_PreviewJunctionDir.dir, 2).c_str());

			ImGui::TreePop();
		}
	}

	real TrackManager::AdvanceTAlongTrack(TrackID trackID, real amount, real t)
	{
		const BezierCurveList* track = &m_Tracks[(i32)trackID];
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

	JSONObject TrackManager::Serialize() const
	{
		JSONObject result = {};

		JSONObject tracks = {};

		for (const BezierCurveList& track : m_Tracks)
		{
			tracks.fields.emplace_back("track", JSONValue(track.Serialize()));
		}

		result.fields.emplace_back("tracks", JSONValue(tracks));

		return result;
	}

	real TrackManager::GetCartTargetDistAlongTrackInChain(CartChainID cartChainID, CartID cartID) const
	{
		real targetT = -1.0f;

		CartManager* cartManager = m_OwningScene->GetCartManager();
		CartChain* cartChain = cartManager->GetCartChain(cartChainID);
		i32 cartIndex = cartChain->GetCartIndex(cartID);
		if (cartIndex == 0)
		{
			targetT = cartChain->GetCartAtIndexDistAlongTrack(cartIndex + 1);
			//targetT = cartManager->GetCart(cartID)->distAlongTrack + cartManager->GetChainDrivePower(cartChainID);
		}
		else
		{
			targetT = cartChain->GetCartAtIndexDistAlongTrack(cartIndex - 1);
		}

		return targetT;
	}

	void TrackManager::Destroy()
	{
		m_Tracks.clear();
		m_Junctions.clear();
	}

} // namespace flex
