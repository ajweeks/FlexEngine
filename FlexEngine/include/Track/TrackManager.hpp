#pragma once

#include "Track/BezierCurveList.hpp"

namespace flex
{
	struct Junction
	{
		static const i32 MAX_TRACKS = 4;
		BezierCurveList* tracks[MAX_TRACKS];
		// Stores which part of the curve is intersecting the junction
		i32 curveIndices[MAX_TRACKS];
	};

	class TrackManager
	{
	public:
		TrackManager();

		void AddTrack(const BezierCurveList& track);

		bool IsTrackInRange(const BezierCurveList* track, const glm::vec3& pos, real range, real& outDistToTrack, real& outDistAlongTrack);
		BezierCurveList* GetTrackInRange(const glm::vec3& pos, real range, real& outDistAlongTrack);

		void DrawDebug();

	private:
		i32 m_TrackCount = 0;
		static const i32 MAX_TRACK_COUNT = 8;
		BezierCurveList m_Tracks[MAX_TRACK_COUNT];

	};
} // namespace flex