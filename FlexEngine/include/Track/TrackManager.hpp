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
			bool bReversingDownTrack,
			BezierCurveList** outNewTrack,
			real* outNewDistAlongTrack,
			i32* outJunctionIndex,
			i32* outCurveIndex);

		void UpdatePreview(BezierCurveList* track,
			real distAlongTrack,
			i32 desiredDir,
			glm::vec3 currentFor,
			bool bFacingForwardDownTrack,
			bool bReversingDownTrack);

		glm::vec3 GetDirectionOnTrack(BezierCurveList* track, real distAlongTrack);

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
		i32 GetTrackIndexInDir(const glm::vec3& desiredDir, Junction& junc, BezierCurveList* track, bool bEndOfTheLine);

		static const real JUNCTION_THRESHOLD_DIST;

		std::vector<BezierCurveList> m_Tracks;
		std::vector<Junction> m_Junctions;
		struct JunctionDirPair
		{
			i32 junctionIndex = -1;
			glm::vec3 dir;
		};
		// Shows the player where they will turn if they continue down the track and don't change their inputs
		JunctionDirPair m_PreviewJunctionDir;

		i32 m_DEBUG_highlightedJunctionIndex = -1;

	};
} // namespace flex