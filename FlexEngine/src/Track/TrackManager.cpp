
#include "stdafx.hpp"

#include "Track/TrackManager.hpp"

#include "Graphics/Renderer.hpp"
#include "Player.hpp"
#include "PlayerController.hpp"
#include "Scene/SceneManager.hpp"

#pragma warning(push, 0)
#include "LinearMath/btIDebugDraw.h"
#pragma warning(pop)

namespace flex
{
	const real TrackManager::JUNCTION_THRESHOLD_DIST = 0.01f;

	TrackManager::TrackManager()
	{
	}

	void TrackManager::AddTrack(const BezierCurveList& track)
	{
		if (m_TrackCount == MAX_TRACK_COUNT)
		{
			PrintWarn("Attempted to add more tracks to TrackManager than allowed (MAX_TRACK_COUNT: %d)\n", MAX_TRACK_COUNT);
			return;
		}
		m_Tracks[m_TrackCount++] = track;
	}

	glm::vec3 TrackManager::GetPointOnTrack(BezierCurveList* track,
		real distAlongTrack,
		real pDistAlongTrack,
		i32 desiredDir,
		BezierCurveList** newTrack,
		real& newDistAlongTrack)
	{
		i32 pCurveIdx = -1;
		i32 newCurveIdx = -1;
		glm::vec3 pPoint = track->GetPointOnCurve(pDistAlongTrack, pCurveIdx);
		glm::vec3 newPoint = track->GetPointOnCurve(distAlongTrack, newCurveIdx);

		bool bEndOfTheLine =
			(distAlongTrack == 0.0f && pDistAlongTrack != 0.0f) ||
			(distAlongTrack == 1.0f && pDistAlongTrack != 1.0f);

		if (newCurveIdx != pCurveIdx || bEndOfTheLine)
		{
			// Check for junction crossings
			for (i32 i = 0; i < m_JunctionCount; ++i)
			{
				for (i32 j = 0; j < m_Junctions[i].trackCount; ++j)
				{
					if (track == m_Junctions[i].tracks[j])
					{
						i32 curveIdxToCheck = newCurveIdx;
						if (newCurveIdx < pCurveIdx)
						{
							curveIdxToCheck = pCurveIdx;
						}
						else if (newCurveIdx == pCurveIdx)
						{
							if (distAlongTrack == 0.0f)
							{
								curveIdxToCheck = 0;
							}
							else
							{
								curveIdxToCheck = (i32)track->GetCurves().size();
							}
						}
						glm::vec3 pos = track->GetPointAtJunction(curveIdxToCheck);
						if (NearlyEquals(pos, m_Junctions[j].pos, JUNCTION_THRESHOLD_DIST))
						{
							i32 newTrackIdx = -1;

							if (m_Junctions[i].trackCount == 2)
							{
								if (bEndOfTheLine)
								{
									newTrackIdx = (m_Junctions[j].tracks[0] == track ? 1 : 0);
									Print("Changed to only other track at junction (it's the end of the line baby)\n");
								}
								else
								{
									if (desiredDir == 1)
									{
										Print("Went straight through a 2-way junction\n");
									}
									else
									{
										newTrackIdx = (m_Junctions[j].tracks[0] == track ? 1 : 0);
										Print("Changed to only other track at junction\n");
									}
								}

							}
							else
							{
								if (desiredDir == 1)
								{
									Print("Went straight through an n-way junction\n");
								}
								else
								{
									glm::vec3 trackForward = glm::normalize(track->GetCurveDirectionAt(distAlongTrack));
									glm::vec3 trackRight = glm::cross(trackForward, VEC3_UP);

									if (desiredDir == 0)
									{
										// Want to go left
										glm::vec3 desiredDirVec = -trackRight;
										newTrackIdx = GetTrackIndexInDir(desiredDirVec, m_Junctions[i], track, bEndOfTheLine, newPoint);
										if (newTrackIdx != -1)
										{
											Print("Went left at an n-way junction\n");
										}
									}
									else if (desiredDir == 2)
									{
										// Want to go right
										glm::vec3 desiredDirVec = trackRight;
										newTrackIdx = GetTrackIndexInDir(desiredDirVec, m_Junctions[i], track, bEndOfTheLine, newPoint);
										if (newTrackIdx != -1)
										{
											Print("Went right at an n-way junction\n");
										}
									}
									else
									{
										Print("Went straight through an n-way junction\n");
									}
								}
							}

							if (newTrackIdx != -1)
							{
								*newTrack = m_Junctions[j].tracks[newTrackIdx];
								newCurveIdx = m_Junctions[j].curveIndices[newTrackIdx];
								newDistAlongTrack = (real)newCurveIdx / (real)((*newTrack)->GetCurves().size());
								return newPoint;
							}
						}

						break;
					}
				}
			}
		}

		return newPoint;
	}

	i32 TrackManager::GetTrackIndexInDir(const glm::vec3& desiredDir, Junction& junc, BezierCurveList* track, bool bEndOfTheLine, const glm::vec3& newPoint)
	{
		i32 newTrackIdx = -1;

		i32 bestTrackIdx = -1;
		real bestTrackDot = 1.0f;
		for (i32 k = 0; k < junc.trackCount; ++k)
		{
			if (junc.tracks[k] != track)
			{
				BezierCurveList* testTrack = junc.tracks[k];
				i32 testTrackCurveCount = (i32)testTrack->GetCurves().size();

				i32 testPointCurveIdx;
				i32 testCurveIdx = 0;
				glm::vec3 testPoint;
				bool bFoundPoint = false;
				for (i32 m = 0; m <= testTrackCurveCount; ++m)
				{
					testPoint = testTrack->GetPointAtJunction(testCurveIdx);
					if (NearlyEquals(testPoint, newPoint, JUNCTION_THRESHOLD_DIST))
					{
						bFoundPoint = true;
						break;
					}
				}

				if (!bFoundPoint)
				{
					Print("ruh roh - didn't find point\n");
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
					real distToJunc = (real)testCurveIdx / (real)(testTrackCurveCount + 1);
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
				glm::vec3 testPos = testTrack->GetPointOnCurve(testDist, testPosCurveIdx);
				glm::vec3 dPos = glm::normalize(testPos - newPoint);
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

	void TrackManager::FindJunctions()
	{
		// Clear previous junctions
		for (i32 i = m_JunctionCount; i >= 0; --i)
		{
			m_Junctions[i] = {};
		}
		m_JunctionCount = 0;

		for (i32 i = 0; i < m_TrackCount; ++i)
		{
			const std::vector<BezierCurve>& curvesA = m_Tracks[i].GetCurves();

			for (i32 j = i + 1; j < m_TrackCount; ++j)
			{
				const std::vector<BezierCurve>& curvesB = m_Tracks[j].GetCurves();

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
									for (i32 n = 0; n < m_JunctionCount; ++n)
									{
										// Junction already exists at this location, check if this track is a part of it
										if (NearlyEquals(m_Junctions[n].pos, curvesA[k].points[p1Idx], JUNCTION_THRESHOLD_DIST))
										{
											bJunctionExists = true;
											i32 trackIndex = -1;
											for (i32 o = 0; o < m_Junctions[n].trackCount; ++o)
											{
												if (m_Junctions[n].tracks[o] == &m_Tracks[j])
												{
													trackIndex = o;
													break;
												}
											}

											if (trackIndex == -1)
											{
												Junction& junct = m_Junctions[n];
												junct.tracks[junct.trackCount] = &m_Tracks[j];
												junct.curveIndices[junct.trackCount] = curveIndexB;
												junct.trackCount++;
											}

											break;
										}
									}

									if (!bJunctionExists)
									{
										Junction& junct = m_Junctions[m_JunctionCount++];
										junct = {};
										junct.pos = curvesA[k].points[p1Idx];
										junct.tracks[junct.trackCount] = &m_Tracks[i];
										junct.curveIndices[junct.trackCount] = curveIndexA;
										junct.trackCount++;
										junct.tracks[junct.trackCount] = &m_Tracks[j];
										junct.curveIndices[junct.trackCount] = curveIndexB;
										junct.trackCount++;
									}
								}
							}
						}
					}
				}
			}
		}

		Print("Found %d junctions\n", m_JunctionCount);
	}

	bool TrackManager::IsTrackInRange(const BezierCurveList* track, const glm::vec3& pos, real range, real& outDistToTrack, real& outDistAlongTrack)
	{
		// Let's brute force it baby
		i32 sampleCount = 250;

		bool bInRange = false;
		real smallestDist = range;
		// TODO: Pre-compute AABBs for each curve for early pruning
		for (i32 i = 0; i <= sampleCount; ++i)
		{
			real t = (real)i / (real)(sampleCount);
			i32 curveIndex;
			real dist = glm::distance(track->GetPointOnCurve(t, curveIndex), pos);
			if (dist < smallestDist)
			{
				smallestDist = dist;
				outDistAlongTrack = t;
				outDistToTrack = smallestDist;
				bInRange = true;
			}
		}

		return bInRange;
	}

	BezierCurveList* TrackManager::GetTrackInRange(const glm::vec3& pos, real range, real& outDistAlongTrack)
	{
		BezierCurveList* result = nullptr;

		real smallestDist = range;
		for (BezierCurveList& track : m_Tracks)
		{
			if (IsTrackInRange(&track, pos, range, smallestDist, outDistAlongTrack))
			{
				range = smallestDist;
				result = &track;
			}
		}

		return result;
	}

	void TrackManager::DrawDebug()
	{
		for (i32 i = 0; i < m_TrackCount; ++i)
		{
			btVector4 highlightColour(0.8f, 0.84f, 0.22f, 1.0f);
			real distAlongTrack = -1.0f;
			Player* player0 = g_SceneManager->CurrentScene()->GetPlayer(0);
			PlayerController* playerController = player0->GetController();
			BezierCurveList* trackRiding = playerController->GetTrackRiding();
			if (trackRiding)
			{
				if (&m_Tracks[i] == trackRiding)
				{
					distAlongTrack = playerController->GetDistAlongTrack();
				}
			}
			else
			{
				real distToTrack;
				if (IsTrackInRange(&m_Tracks[i], player0->GetTransform()->GetWorldPosition(),
					playerController->GetTrackAttachDistThreshold(),
					distToTrack,
					distAlongTrack))
				{
					highlightColour = btVector4(0.75f, 0.65, 0.75f, 1.0f);
				}
			}
			m_Tracks[i].DrawDebug(highlightColour, distAlongTrack);
		}

		for (i32 i = 0; i < m_JunctionCount; ++i)
		{
			 btIDebugDraw* debugDrawer = g_Renderer->GetDebugDrawer();
			 real distAlongTrack0 = m_Junctions[i].curveIndices[0] / (real)m_Junctions[i].tracks[0]->GetCurves().size();
			 i32 curveIndex;
			 glm::vec3 pos = m_Junctions[i].tracks[0]->GetPointOnCurve(distAlongTrack0, curveIndex);
			 btVector3 sphereCol = btVector3(0.9f, 0.2f, 0.2f);
			 if (i == m_DEBUG_highlightedJunctionIndex)
			 {
				 sphereCol = btVector3(0.9f, 0.9f, 0.9f);
			 }
			 debugDrawer->drawSphere(ToBtVec3(pos), 0.5f, sphereCol);

			 for (i32 j = 0; j < m_Junctions[i].trackCount; ++j)
			 {
				 BezierCurveList* track = m_Junctions[i].tracks[j];
				 i32 curveIndex = m_Junctions[i].curveIndices[j];

				 real tAtJunc = track->GetTAtJunction(curveIndex);
				 i32 outCurveIdx;
				 btVector3 start = ToBtVec3(pos + VEC3_UP * 1.5f);

				 if (curveIndex < (i32)track->GetCurves().size())
				 {
					 glm::vec3 trackP1 = track->GetPointOnCurve(tAtJunc + 0.01f, outCurveIdx);
					 glm::vec3 dir1 = glm::normalize(trackP1 - pos);
					 debugDrawer->drawLine(start, ToBtVec3(pos + dir1 * 5.0f + VEC3_UP * 1.5f), btVector3(0.2f, 0.2f, 0.8f));
				 }
				 if (curveIndex > 0)
				 {
					 glm::vec3 trackP2 = track->GetPointOnCurve(tAtJunc - 0.01f, outCurveIdx);
					 glm::vec3 dir2 = glm::normalize(trackP2 - pos);
					 debugDrawer->drawLine(start, ToBtVec3(pos + dir2 * 5.0f + VEC3_UP * 1.5f), btVector3(0.2f, 0.6f, 0.25f));
				 }
			 }
		}
	}

	void TrackManager::DrawImGuiObjects()
	{
		if (ImGui::TreeNode("Track Manager"))
		{
			ImGui::Text("%d tracks, selected: %d", m_TrackCount, m_DEBUG_highlightedJunctionIndex);
			if (m_DEBUG_highlightedJunctionIndex != -1)
			{
				if (ImGui::SmallButton("<"))
				{
					m_DEBUG_highlightedJunctionIndex--;
				}
			}
			else
			{
				ImGui::Spacing();
				ImGui::Spacing();
				ImGui::Spacing();
			}
			if (m_DEBUG_highlightedJunctionIndex != m_TrackCount - 1)
			{
				ImGui::SameLine();
				if (ImGui::SmallButton(">"))
				{
					m_DEBUG_highlightedJunctionIndex++;
				}
			}

			if (m_DEBUG_highlightedJunctionIndex != -1)
			{
				ImGui::Text("Junction connected track count: %d", m_Junctions[m_DEBUG_highlightedJunctionIndex].trackCount);
			}

			ImGui::Text("%d junctions", m_JunctionCount);

			ImGui::TreePop();
		}
	}

	bool Junction::Equals(BezierCurveList* trackA, BezierCurveList* trackB, i32 curveIndexA, i32 curveIndexB)
	{
		i32 trackAIndex = -1;
		i32 trackBIndex = -1;

		for (i32 i = 0; i < trackCount; ++i)
		{
			if (tracks[i] == trackA)
			{
				trackAIndex = i;
			}
			else if (tracks[i] == trackB)
			{
				trackBIndex = i;
			}
		}

		if (trackAIndex != -1 &&
			trackBIndex != -1)
		{
			if (curveIndices[trackAIndex] == curveIndexA &&
				curveIndices[trackBIndex] == curveIndexB)
			{
				return true;
			}
		}

		return false;
	}
} // namespace flex
