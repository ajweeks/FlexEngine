#pragma once

#include "Helpers.hpp" // For TrackState

namespace flex
{
	class BezierCurveList;

	struct Junction
	{
		static const i32 MAX_TRACKS = 4;

		bool Equals(BezierCurveList* trackA, BezierCurveList* trackB, i32 curveIndexA, i32 curveIndexB);

		glm::vec3 pos;
		i32 trackCount = 0;
		i32 trackIndices[MAX_TRACKS];
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
			i32* outCurveIndex,
			TrackState* outTrackState,
			bool bPrint);

		void UpdatePreview(BezierCurveList* track,
			real distAlongTrack,
			i32 desiredDir,
			glm::vec3 currentFor,
			bool bFacingForwardDownTrack,
			bool bReversingDownTrack);

		bool GetControlPointInRange(const glm::vec3& p, real range, glm::vec3* outPoint);

		// Compares curve end points on all BezierCurves and creates junctions when positions are
		// within a threshold of each other
		void FindJunctions();

		bool IsTrackInRange(const BezierCurveList* track, const glm::vec3& pos, real range, real& outDistToTrack, real& outDistAlongTrack);
		i32 GetTrackInRangeIndex(const glm::vec3& pos, real range, real& outDistAlongTrack);

		void DrawDebug();

		void DrawImGuiObjects();

		// Moves t along track according to curve length
		real AdvanceTAlongTrack(BezierCurveList* track, real amount, real t);

		std::vector<BezierCurveList> m_Tracks;
		std::vector<Junction> m_Junctions;

	private:
		i32 GetTrackIndexInDir(const glm::vec3& desiredDir, Junction& junc, BezierCurveList* track, bool bEndOfTheLine);
		TrackState GetTrackStateInDir(const glm::vec3& desiredDir, BezierCurveList* track, real distAlongTrack, bool bReversing);

		static const real JUNCTION_THRESHOLD_DIST;

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