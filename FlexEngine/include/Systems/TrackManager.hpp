#pragma once

#include "JSONTypes.hpp"
#include "Systems/System.hpp"
#include "Track/BezierCurve3D.hpp"
#include "Track/BezierCurveList.hpp"

namespace flex
{
	class BaseScene;
	class GameObject;
	enum class TrackState;

	// Identifies one of the four points of a curve in a track
	struct TrackPointRef
	{
		bool IsValid() const { return trackID != InvalidTrackID; }
		bool IsNode() const { return pointIndex == 0 || pointIndex == 3; }
		bool operator==(const TrackPointRef& other) const
		{
			return trackID == other.trackID && curveIndex == other.curveIndex && pointIndex == other.pointIndex;
		}

		TrackID trackID = InvalidTrackID;
		i32 curveIndex = -1;
		i32 pointIndex = -1;
	};

	// Dimensions of the generated track mesh, in metres
	struct TrackMeshSettings
	{
		real gauge = 1.1f; // Distance between rail centres
		real railWidth = 0.08f;
		real railHeight = 0.12f;
		real sleeperLength = 1.6f;
		real sleeperWidth = 0.22f;
		real sleeperHeight = 0.1f;
		real sleeperSpacing = 0.65f;
		real segmentLength = 0.4f; // Max length of each rail segment
	};

	// Discrete pieces of track which are placed one at a time, each continuing smoothly on from the end of the last
	enum class TrackPieceType : u32
	{
		STRAIGHT,
		STRAIGHT_LONG,
		CURVE_LEFT,
		CURVE_RIGHT,
		RAMP_UP,
		RAMP_DOWN,
		S_BEND_LEFT,
		S_BEND_RIGHT,
		FOLLOW_GROUND, // A ramp up, down or level, whichever best follows the ground ahead

		_COUNT
	};

	// Dimensions of track pieces, in metres. Grades are rise over run.
	struct TrackPieceSettings
	{
		real straightLength = 4.0f;
		real longStraightLength = 8.0f;
		real curveRadius = 10.0f;
		real curveAngle = PI / 4.0f; // Eight curves make a full circle
		real rampRun = 6.0f; // Horizontal length of ramps
		real gradeStep = 0.1f; // Change in grade over a ramp
		real maxGrade = 0.3f; // Steeper track can't be placed
		real sBendLength = 10.0f;
		real sBendOffset = 3.0f; // Sideways shift (far enough for a parallel track)

		// How far the terrain can be cut down or filled up to meet the track
		real maxCutDepth = 4.0f;
		real maxFillDepth = 4.0f;
		// Half width of the strip of ground flattened under the track (the outer half blends into the terrain)
		real earthworksRadius = 3.0f;
	};

	// Whether a piece can be placed, & why not
	enum class TrackPieceCheck : u32
	{
		OK,
		TOO_STEEP,
		CUT_TOO_DEEP,
		FILL_TOO_DEEP,
		BLOCKED_BY_TRACK,

		_COUNT
	};

	// An end of a track which no other track continues on from
	struct TrackEnd
	{
		TrackPointRef ref;
		glm::vec3 pos;
		glm::vec3 dir; // Normalized, pointing away from the track
	};

	// Describes a track being cut in two (see TrackManager::CutTrack), so riders can be moved onto the remaining parts
	struct TrackCut
	{
		// Moves a position on the cut track onto the part it's now on. Returns false if it was on the removed section.
		// Positions on other tracks are left alone.
		bool Remap(TrackID& inOutTrackID, real& inOutDistAlongTrack) const;

		TrackID trackID = InvalidTrackID; // The track which was cut (now the part before the cut, or after if there's nothing before)
		TrackID afterTrackID = InvalidTrackID; // The part after the cut (if there is one)
		bool bKeptBefore = false;
		bool bKeptAfter = false;
		// The removed section runs from (beforeCurve, beforeT) to (afterCurve, afterT), in the original track's curves
		i32 curveCount = 0;
		i32 beforeCurve = 0;
		real beforeT = 1.0f;
		i32 afterCurve = 0;
		real afterT = 0.0f;
	};

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
		virtual void OnPreSceneChange() override;

		virtual void DrawImGui() override;

		JSONObject Serialize() const;

		void InitializeFromJSON(const JSONObject& obj);

		// Returns the ID of the new track
		TrackID AddTrack(const BezierCurveList& track);
		BezierCurveList* GetTrack(TrackID trackID);
		// Shifts the IDs of all following tracks down by one
		void RemoveTrack(TrackID trackID);
		// Removes every track (e.g. when leaving a world)
		void ClearTracks();
		// Must be called after changing a track's points
		void OnTrackModified(TrackID trackID);

		// Builds a smooth track passing through each node. Non-zero start/end dirs force the tangent at that end
		static BezierCurveList CreateTrackThroughNodes(const std::vector<glm::vec3>& nodes, glm::vec3 startDir, glm::vec3 endDir);

		// Returns the closest node within range, ignoring any in exclude
		TrackPointRef GetClosestNode(const glm::vec3& pos, real range, const std::vector<TrackPointRef>* exclude = nullptr) const;
		glm::vec3 GetPoint(const TrackPointRef& ref) const;
		// Returns the (normalized) direction the track travels in at the given node
		glm::vec3 GetNodeDirection(const TrackPointRef& ref) const;
		// Returns every node sharing the given node's position (including the given node)
		void GetCoincidentNodes(const TrackPointRef& ref, std::vector<TrackPointRef>& outNodes) const;
		// Removes a node, merging its two neighbouring curves. Returns false if the whole track was removed
		bool RemoveNode(const TrackPointRef& ref);
		// Returns the current directions at the track's ends which meet other tracks (zero for free ends),
		// so smoothing can keep junctions lined up
		void GetJunctionEndDirs(TrackID trackID, glm::vec3& outStartDir, glm::vec3& outEndDir) const;
		// Regenerates all curve handles so the track passes smoothly through its nodes
		void SmoothTrack(TrackID trackID, const glm::vec3& startDir, const glm::vec3& endDir);

		// Displays a translucent track which isn't part of the track network (pass nullptr to hide it), red when it
		// can't be placed
		void SetPreviewTrack(const BezierCurveList* track, bool bValid = true);
		// Flashes the given track red (pass InvalidTrackID to clear)
		void SetHighlightedTrack(TrackID trackID);
		// Returns the track passing closest to pos, if any are within range
		TrackID GetClosestTrack(const glm::vec3& pos, real range) const;

		// Builds tracks through the nodes, with the last node looping back to join loopNodeIndex.
		// Since riding only moves between different tracks, the loop is split into two tracks (plus a lead-in track when loopNodeIndex > 0)
		static void CreateLoopedTracks(const std::vector<glm::vec3>& nodes, glm::vec3 startDir, i32 loopNodeIndex, std::vector<BezierCurveList>& outTracks);

		void OnSceneChanged();

		// Track pieces
		const TrackPieceSettings& GetPieceSettings() const { return m_PieceSettings; }
		// Finds the free track end closest to pos within range
		bool GetClosestFreeEnd(const glm::vec3& pos, real range, TrackEnd& outEnd) const;
		// Builds a piece starting at startPos heading in startDir (whose slope is the starting grade). gradeChange is
		// only used by FOLLOW_GROUND pieces (see ChooseFollowGroundGradeChange)
		BezierCurve3D BuildPiece(TrackPieceType type, const glm::vec3& startPos, const glm::vec3& startDir, real gradeChange = 0.0f) const;
		// The grade change (a multiple of the grade step) with which a ramp best follows the ground ahead
		real ChooseFollowGroundGradeChange(const glm::vec3& startPos, const glm::vec3& startDir) const;
		// Whether a piece can be placed: not too steep, the ground can be cut or filled to meet it & it doesn't run
		// into other tracks. continuesFromTrackID is the track it continues on from (if any), which it may touch.
		// outMaxCut & outMaxFill are how far the ground will be cut down & built up
		TrackPieceCheck CheckPiece(const BezierCurve3D& piece, const glm::vec3& startDir, TrackID continuesFromTrackID,
			real* outMaxCut = nullptr, real* outMaxFill = nullptr) const;
		// Cuts & fills the terrain along the track so it sits flush on the ground (felling trees in the way)
		void ConformTerrainToTrack(const BezierCurve3D& curve);
		// The ground below pos (the terrain, or static collision when there's no terrain)
		bool FindGroundBelow(const glm::vec3& pos, real maxDistance, glm::vec3& outGroundPos) const;
		// Rise over run of a direction
		static real GetGrade(const glm::vec3& dir);

		// Removes the section of track between t0 & t1 (both in [0, 1]), leaving up to two tracks. Riders on the
		// removed section derail. Returns false if the whole track was removed.
		bool CutTrack(TrackID trackID, real t0, real t1);
		// Breaks tracks whose support has been dug out of the ground below them
		void OnTerrainDug(const glm::vec3& center, real radius);

		void DrawDebug();

		glm::vec3 GetPointOnTrack(TrackID trackID,
			real distAlongTrack,
			real pDistAlongTrack,
			LookDirection desiredDir,
			bool bMovingBackwards, // Rider is facing opposite to the direction of travel
			TrackID* outNewTrackID,
			real* outNewDistAlongTrack,
			i32* outJunctionIndex,
			i32* outCurveIndex,
			TrackState* outTrackState,
			bool bPrint,
			glm::vec3* outExitDir = nullptr,
			i32* outForwardExitCount = nullptr);

		// Finds the junction ahead (if any) and which way the rider will go through it.
		// travelSign is +1 when moving towards increasing t
		void UpdatePreview(TrackID trackID,
			real distAlongTrack,
			LookDirection desiredDir,
			real travelSign,
			bool bMovingBackwards);
		// True when the upcoming junction offers more than one way onwards
		bool IsForkAhead() const;

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

		struct JunctionExit
		{
			TrackID trackID = InvalidTrackID;
			real t = 0.0f; // Position of the junction along the track
			real travelSign = 1.0f; // +1 when leaving towards increasing t
			glm::vec3 dir; // Direction of travel when leaving the junction
		};

		// Ways onward from a junction when arriving travelling in travelDir
		void GetJunctionExits(const Junction& junction, const glm::vec3& travelDir, std::vector<JunctionExit>& outExits) const;
		// steer: -1 = left, 0 = straight, 1 = right. Returns index into exits, or -1 if there are none
		static i32 ChooseJunctionExit(const std::vector<JunctionExit>& exits, const glm::vec3& travelDir, real steer);

		std::vector<BezierCurveList> tracks;
		std::vector<Junction> junctions;

	private:

		struct TrackMesh
		{
			GameObject* object = nullptr; // Owned by the scene
			bool bDirty = true;
		};

		// Inflate grows the mesh in every direction (used to cover a track with another material)
		void UpdateTrackMesh(TrackMesh& trackMesh, const BezierCurveList& track, const char* objectName,
			const char* railMaterialName, const char* sleeperMaterialName, real inflate);
		void DestroyTrackMesh(TrackMesh& trackMesh);
		// Previews are static meshes (the hologram shader has no dynamic vertex buffer), relative to m_PreviewOrigin so
		// they only need rebuilding when their shape changes
		void UpdatePreviewMesh(TrackMesh& trackMesh, bool bValid);

		static const real JUNCTION_THRESHOLD_DIST;

		// Parallel to tracks
		std::vector<TrackMesh> m_TrackMeshes;
		TrackMesh m_PreviewTrackMesh;
		// Shown instead of m_PreviewTrackMesh when the preview can't be placed
		TrackMesh m_BlockedPreviewTrackMesh;
		BezierCurveList m_PreviewTrack; // Relative to m_PreviewOrigin
		glm::vec3 m_PreviewOrigin = VEC3_ZERO;
		bool m_bPreviewTrackVisible = false;
		bool m_bPreviewTrackValid = true;

		TrackPieceSettings m_PieceSettings;
		// Set while making a track's own earthworks, which mustn't break tracks
		bool m_bConformingTerrain = false;

		// Drawn over the highlighted track to show it's about to be deleted
		TrackMesh m_HighlightTrackMesh;
		TrackID m_HighlightedTrackID = InvalidTrackID;

		TrackMeshSettings m_MeshSettings;
		bool m_bDrawDebugLines = false;

		struct JunctionDirPair
		{
			i32 junctionIndex = -1;
			glm::vec3 dir;
		};
		// Shows the player where they will turn if they continue down the track and don't change their inputs
		JunctionDirPair m_PreviewJunctionDir;
		glm::vec3 m_PreviewTravelDir = VEC3_ZERO;
		i32 m_PreviewForwardExitCount = 0;

		i32 m_DEBUG_highlightedJunctionIndex = -1;

	};

	// Scripted test of placing pieces on sloped terrain, riding them & digging under them (FLEX_TRACK_TEST=1 with
	// --test-scenes on a terrain scene, run by SceneTestRunner). EndTrackTest returns false if it didn't finish.
	void BeginTrackTest(bool bScreenshots);
	void UpdateTrackTest();
	bool EndTrackTest();
} // namespace flex