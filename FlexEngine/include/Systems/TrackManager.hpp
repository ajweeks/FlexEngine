#pragma once

#include "JSONTypes.hpp"
#include "Systems/System.hpp"

namespace flex
{
	class BaseScene;
	class BezierCurveList;
	enum class TrackState;

	struct Junction
	{
		// TODO: Update ToString if this value changes
		static const i32 MAX_TRACKS = 4;

		JSONObject Serialize() const;

		glm::vec3 pos;
		i32 trackCount = 0;
		i32 trackIndices[MAX_TRACKS]{ -1, -1, -1, -1 };
		// Stores which part of the curve is intersecting the junction
		i32 curveIndices[MAX_TRACKS]{ -1, -1, -1, -1 };
	};

	class TrackManager final : public System
	{
	public:
		TrackManager();
		~TrackManager() = default;

		virtual void Initialize() override;
		virtual void Destroy() override;
		virtual void Update() override;

		virtual void DrawImGui() override;

		JSONObject Serialize() const;

		void InitializeFromJSON(const JSONObject& obj);

		// Returns the ID of the new track
		TrackID AddTrack(const BezierCurveList& track);
		BezierCurveList* GetTrack(TrackID trackID);

		void OnSceneChanged();

		void DrawDebug();

		glm::vec3 GetPointOnTrack(TrackID trackID,
			real distAlongTrack,
			real pDistAlongTrack,
			LookDirection desiredDir,
			bool bReversingDownTrack,
			TrackID* outNewTrackID,
			real* outNewDistAlongTrack,
			i32* outJunctionIndex,
			i32* outCurveIndex,
			TrackState* outTrackState,
			bool bPrint);

		void UpdatePreview(TrackID trackID,
			real distAlongTrack,
			LookDirection desiredDir,
			glm::vec3 currentFor,
			bool bFacingForwardDownTrack,
			bool bReversingDownTrack);

		bool GetPointInRange(const glm::vec3& p, bool bIncludeHandles, real range, glm::vec3* outPoint);
		bool GetPointInRange(const glm::vec3& p, real range, TrackID* outTrackID, i32* outCurveIndex, i32* outPointIdx);

		// Compares curve end points on all BezierCurves and creates junctions when positions are
		// within a threshold of each other
		void FindJunctions();

		bool IsTrackInRange(TrackID trackID, const glm::vec3& pos, real range, real* outDistToTrack, real* outDistAlongTrack);
		TrackID GetTrackInRangeID(const glm::vec3& pos, real range, real* outDistAlongTrack);

		// Moves t along track according to curve length
		real AdvanceTAlongTrack(TrackID trackID, real amount, real t);

		real GetCartTargetDistAlongTrackInChain(CartChainID cartChainID, CartID cartID) const;

		std::vector<BezierCurveList> tracks;
		std::vector<Junction> junctions;

	private:
		i32 GetTrackIndexInDir(const glm::vec3& desiredDir, Junction& junc, TrackID trackID, bool bEndOfTheLine);
		TrackState GetTrackStateInDir(const glm::vec3& desiredDir, TrackID trackID, real distAlongTrack, bool bReversing);

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