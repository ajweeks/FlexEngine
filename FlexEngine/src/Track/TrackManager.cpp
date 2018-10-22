
#include "stdafx.hpp"

#include "Track/TrackManager.hpp"

#include "Player.hpp"
#include "Scene/SceneManager.hpp"
#include "PlayerController.hpp"

namespace flex
{
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
			real dist = glm::distance(track->GetPointOnCurve(t), pos);
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
		for (const BezierCurveList& track : m_Tracks)
		{
			btVector3 highlightColour(0.8f, 0.84f, 0.22f);
			real distAlongTrack = -1.0f;
			Player* player0 = g_SceneManager->CurrentScene()->GetPlayer(0);
			PlayerController* playerController = player0->GetController();
			BezierCurveList* trackRiding = playerController->GetTrackRiding();
			if (trackRiding)
			{
				if (&track == trackRiding)
				{
					distAlongTrack = playerController->GetDistAlongTrack();
				}
			}
			else
			{
				real distToTrack;
				if (IsTrackInRange(&track, player0->GetTransform()->GetWorldPosition(),
					playerController->GetTrackAttachDistThreshold(),
					distToTrack,
					distAlongTrack))
				{
					highlightColour = btVector3(0.75f, 0.65, 0.75f);
				}
			}
			track.DrawDebug(highlightColour, distAlongTrack);
		}
	}
} // namespace flex
