#pragma once

#include "Track/BezierCurveList.hpp"

namespace flex
{
	struct Junction
	{
		static const i32 MAX_TRACKS = 4;

		bool Equals(BezierCurveList* trackA, BezierCurveList* trackB, i32 curveIndexA, i32 curveIndexB);

		glm::vec3 pos;
		i32 trackCount = 0;
		BezierCurveList* tracks[MAX_TRACKS];
		// Stores which part of the curve is intersecting the junction
		i32 curveIndices[MAX_TRACKS];
	};

	class TrackManager
	{
	public:
		TrackManager();

		void AddTrack(const BezierCurveList& track);

		glm::vec3 GetPointOnTrack(BezierCurveList* track,
			real distAlongTrack,
			real pDistAlongTrack,
			i32 desiredDir,
			BezierCurveList** newTrack,
			real& newDistAlongTrack);

		// Compares curve end points on all BezierCurves and creates junctions when positions are
		// within a threshold of each other
		void FindJunctions();

		bool IsTrackInRange(const BezierCurveList* track, const glm::vec3& pos, real range, real& outDistToTrack, real& outDistAlongTrack);
		BezierCurveList* GetTrackInRange(const glm::vec3& pos, real range, real& outDistAlongTrack);

		void DrawDebug();

		void DrawImGuiObjects();

		// Moves t along track according to curve length
		real AdvanceTAlongTrack(BezierCurveList* track, real amount, real t);

	private:
		i32 GetTrackIndexInDir(const glm::vec3& desiredDir, Junction& junc, BezierCurveList* track, bool bEndOfTheLine, const glm::vec3& newPoint);

		static const i32 MAX_TRACK_COUNT = 8;
		static const i32 MAX_JUNCTION_COUNT = 64;
		static const real JUNCTION_THRESHOLD_DIST;

		i32 m_TrackCount = 0;
		BezierCurveList m_Tracks[MAX_TRACK_COUNT];
		i32 m_JunctionCount = 0;
		Junction m_Junctions[MAX_JUNCTION_COUNT];

		i32 m_DEBUG_highlightedJunctionIndex = -1;

	};
} // namespace flex