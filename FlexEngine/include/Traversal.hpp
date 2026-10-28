#pragma once

#include "Spring.hpp"

class btCollisionObject;
class btRigidBody;

namespace flex
{
	class Player;
	class Mesh;
	class GameObject;
	class TerrainGenerator;
	class BuildPiece;
	class Swimming;

	// Movement abilities for getting around large terrain: jumping, a paraglider (lifted by thermals & ridge lift),
	// a wingsuit, a grappling hook, climbing (using stamina) & skiing/sandboarding. Owned by a Player, which hands
	// over control of its velocity whenever a mode is active.
	class Traversal final
	{
	public:
		enum class Mode
		{
			NONE, // Walking or falling
			GLIDING,
			WINGSUIT,
			CLIMBING,
			SKIING,

			_COUNT
		};

		void Initialize(Player* player);
		void Destroy();
		// Cancels any active mode & retracts the hook (e.g. when respawning)
		void Reset();

		// Called every frame: input, sounds & camera effects
		void Update();
		// Rebuilds the rope & glider geometry. Called once the player's render pose (interpolated between physics
		// steps) is known so the geometry lines up with the rendered player.
		void UpdateVisuals();
		// Called every physics step. Returns true when traversal controlled the player's velocity this step (so
		// regular walking movement should be skipped).
		bool FixedUpdate();


		Mode GetMode() const { return m_Mode; }
		// Swimming & breath (see Swimming.hpp)
		Swimming* GetSwimming() const { return m_Swimming; }
		bool IsGrappling() const { return m_Hook.state != HookState::NONE; }
		// Whether the player is flying/sliding fast enough that walking speed limits shouldn't apply
		bool HasMomentum() const;

		// Camera effects
		real GetFOVOffset() const { return m_FOVOffset; }
		real GetCameraRoll() const { return m_CameraRoll; }
		// How far the player (& glider) is banked (radians, + leans right) & pitched (+ nose up) while flying
		real GetBodyBank() const { return m_BankSpring.pos; }
		real GetBodyPitch() const { return m_PitchSpring.pos; }
		// How deployed the glider is [0, 1] (it inflates & folds away over a fraction of a second)
		real GetCanopyOpenness() const { return m_CanopyOpenness; }
		// How spread the wingsuit's wings are [0, 1]
		real GetWingsuitOpenness() const { return m_WingsuitOpenness; }
		// Number of rushing air streaks drawn this frame
		u32 GetAirStreakCount() const { return m_AirStreakCount; }

		void DrawHUD() const;
		void DrawImGui();

		static const char* ModeName(Mode mode);

		struct Thermal
		{
			glm::vec2 center; // At ground level (thermals drift downwind with height)
			real radius;
			real strength; // Peak updraft (m/s), before sun & ground factors
		};

		// Exposed for testing
		// Lift (m/s of updraft) a glider would experience at a position
		real ComputeLift(const glm::vec3& pos) const;
		// Thermals whose cells overlap the square of the given half size around pos (in xz)
		void GetThermalsNear(const glm::vec2& pos, real halfSize, std::vector<Thermal>& outThermals) const;
		// Removes the part of a horizontal velocity which would take a crawling player off a ledge (a drop of more than a
		// step). Returns true if anything was removed.
		bool BlockLedges(glm::vec3& velocity) const;
		bool IsHookAttached() const { return m_Hook.state == HookState::ATTACHED; }
		real GetRopeLength() const { return m_Hook.ropeLength; }
		glm::vec3 GetHookPos() const { return m_Hook.pos; }
		real GetStamina() const { return m_Stamina; }
		// Seconds since the player last jumped off the ground (ground snapping is suspended right after a jump)
		sec GetTimeSinceJump() const { return m_TimeSinceJump; }
		bool IsMantling() const { return m_bMantling; }
		// Number of times the player has pulled themselves up over a ledge (for testing)
		u32 GetMantleCount() const { return m_MantleCount; }

		// Throws a rope in the look direction. Where it lands (on top of a ledge, or snagged on a wall) it's anchored &
		// falls down to the ground, becoming a climbable rope (a BuildPiece). Returns false if one is already in flight.
		bool TossRope();
		bool IsTossingRope() const { return m_RopeToss.state != RopeTossState::NONE; }
		// The rope created by the last toss which caught on something (invalid if none has)
		GameObjectID GetLastTossedRopeID() const { return m_LastTossedRopeID; }
		// Why the last toss didn't catch on anything (empty if it did)
		const std::string& GetRopeTossFailure() const { return m_RopeTossFailure; }

	private:
		enum class HookState
		{
			NONE,
			FLYING, // Travelling towards its target
			ATTACHED,
			RETRACTING,
		};

		struct Hook
		{
			HookState state = HookState::NONE;
			glm::vec3 pos = VEC3_ZERO; // Current hook position (the anchor once attached)
			glm::vec3 target = VEC3_ZERO;
			glm::vec3 targetNormal = VEC3_UP;
			real ropeLength = 0.0f;
			// Checks the anchor is still there (e.g. trees can be chopped down)
			sec anchorCheckTimer = 0.0f;
			u32 anchorMissCount = 0;
			bool bReelingIn = false;
			bool bReelingOut = false;
		};

		struct Settings
		{
			// Jumps use stronger gravity than the rest of the world so they're short & snappy rather than floaty
			real jumpSpeed = 7.8f;
			real coyoteTime = 0.12f;
			real jumpBufferTime = 0.12f; // Jump presses this long before landing still jump
			real riseGravityScale = 2.4f; // While rising with jump held
			real jumpCutGravityScale = 4.0f; // While rising after releasing jump (short hops)
			real fallGravityScale = 2.8f;
			real maxFallSpeed = 55.0f;

			real glideSpeed = 12.0f;
			real glideSpeedBoost = 5.0f; // Added/removed with forward/back input
			real glideSinkRate = 2.0f;
			real glideMinHeight = 2.5f; // Above the ground, to deploy

			real wingsuitLift = 0.035f;
			real wingsuitDrag = 0.0018f;
			real wingsuitMaxLift = 40.0f; // m/s^2
			real wingsuitMaxLiftAngle = 45.0f; // Degrees, steeper angles of attack produce no more lift

			real hookRange = 150.0f;
			real hookSpeed = 180.0f;
			real reelSpeed = 16.0f;
			real swingControl = 7.0f; // Acceleration from move input while hanging

			real climbSpeed = 2.6f;
			real climbReach = 1.0f; // From the player's center to the wall
			real climbStaminaDrain = 0.03f; // Per second while hanging
			real climbMoveStaminaDrain = 0.05f; // Extra per second while moving
			real climbLeapStamina = 0.15f;
			// Ledges up to this high above the feet are pulled up onto when jumping/falling/sliding into them (& up to
			// stepAssistHeight when walking into them)
			real mantleReach = 2.1f;
			real stepAssistHeight = 1.1f;

			real ropeTossSpeed = 21.0f;
			real ropeMaxLength = 40.0f;
			real staminaRegen = 0.35f; // Per second while grounded

			bool bSkiingEnabled = false;
			real skiMaxSpeed = 48.0f;
			real skiTurnRate = 2.2f; // Radians per second
			real skiPush = 4.0f;
		};

		enum class RopeTossState
		{
			NONE,
			FLYING, // Thrown, following a ballistic arc
			FALLING, // Anchored, unrolling down to the ground
		};

		struct RopeToss
		{
			RopeTossState state = RopeTossState::NONE;
			glm::vec3 pos = VEC3_ZERO;
			glm::vec3 velocity = VEC3_ZERO;
			sec time = 0.0f;
			// Where the rope hangs from, where it leaves the ground (if it's laid over a rounded lip, else the same as
			// anchor) & its bottom
			glm::vec3 anchor = VEC3_ZERO;
			glm::vec3 mid = VEC3_ZERO;
			glm::vec3 bottom = VEC3_ZERO;
		};

		// Raycast options
		enum RaycastFlags : u32
		{
			RAYCAST_FOLIAGE = 1 << 0, // Also hit tree foliage (which nothing collides with)
			RAYCAST_PHYSICS_ONLY = 1 << 1, // Only test what the player collides with (skip the terrain's density)
		};

		struct GroundInfo
		{
			bool bHit = false;
			glm::vec3 pos = VEC3_ZERO;
			glm::vec3 normal = VEC3_UP;
			real distance = 0.0f; // From the player's center
		};

		// Jump button events (when not used for placing items)
		void OnJumpPressed();
		void OnJumpReleased();

		void SetMode(Mode mode);
		void SetGravityEnabled(bool bEnabled);
		bool CanControl() const;
		real GetMoveForward() const;
		real GetMoveRight() const;

		struct DynamicMesh;

		void UpdateInput();
		void UpdateHook();
		void UpdateSurroundings();
		void UpdateSounds();
		// Faint motes rising in nearby thermals, so they can be found by eye
		void DrawThermalMotes();

		void FireHook();
		void ReleaseHook();
		bool TryStartClimbing();
		// Grabs onto a nearby ladder or rope
		bool TryStartClimbingObject(bool bClimbHeld);
		bool FixedUpdateClimbObject(glm::vec3& velocity);

		void FixedUpdateHook(glm::vec3& velocity);
		void FixedUpdateGlide(glm::vec3& velocity);
		void FixedUpdateWingsuit(glm::vec3& velocity);
		bool FixedUpdateClimb(glm::vec3& velocity);
		// Finds a point to stand on over the top of the wall being climbed
		bool FindLedge(const glm::vec3& wallNormal, glm::vec3& outStandPos) const;
		// Whether the player could stand at groundPos without sliding off (dir: the direction they'd be moving in)
		bool HasFooting(const glm::vec3& groundPos, const glm::vec3& groundNormal, const glm::vec3& dir) const;
		// Pulls the player up & over onto standPos (where their center will be once standing)
		void StartMantle(const glm::vec3& standPos);
		// While walking/jumping into a steep lip, climbs up onto it if it's within reach
		bool TryAutoMantle(const glm::vec3& velocity);
		// Somewhere to stand in direction dir (horizontal), between minHeight & maxHeight above the player's feet
		bool FindMantleTarget(const glm::vec3& dir, real minHeight, real maxHeight, glm::vec3& outStandPos) const;

		void UpdateRopeToss();
		// Where a rope caught at hitPos hangs from (outTop), where it leaves the ground (outMid, the same as outTop if
		// it hangs straight down) & its bottom
		bool ComputeRopeHang(const glm::vec3& hitPos, const glm::vec3& hitNormal, glm::vec3& outTop, glm::vec3& outMid, glm::vec3& outBottom) const;
		void SpawnTossedRope();
		// The rope piece continuing on from the top/bottom of the given one (tossed ropes may have two)
		BuildPiece* FindConnectedRope(BuildPiece* rope, bool bAbove) const;
		void FixedUpdateSki(glm::vec3& velocity);
		// Where the body (& glider) should lean, from how the player is flying
		void UpdateBodyTiltTargets(const glm::vec3& velocity);
		// Smooths the lean & applies it to the player's mesh (called with the render pose)
		void UpdateBodyTilt();
		void BuildGlider();
		// The wingsuit's fabric (between the arms & body, & the legs) & limbs, lying along the leaning body
		void BuildWingsuit();
		// Thin streaks of rushing air around the camera while flying or falling fast
		void DrawAirStreaks();

		// Raycasts against physics colliders (ignoring the player) & the terrain's density
		bool Raycast(const glm::vec3& from, const glm::vec3& to, glm::vec3& outHitPos, glm::vec3& outHitNormal, u32 flags = 0) const;
		GroundInfo ProbeGround(real maxDistance) const;
		// How slippery the ground is [0: grippy, 1: fresh snow]
		real GetGroundSlipperiness(const GroundInfo& ground) const;
		glm::vec3 GetHandPos() const;
		glm::vec3 GetLookDirection() const;
		glm::vec3 GetHeadingXZ() const;


		Player* m_Player = nullptr;
		Settings m_Settings;

		Mode m_Mode = Mode::NONE;
		Hook m_Hook;

		real m_Stamina = 1.0f;
		bool m_bStaminaExhausted = false; // Must regenerate fully before climbing again
		sec m_TimeSinceGrounded = 0.0f;
		bool m_bGrounded = false;
		bool m_bJumpHeld = false;
		bool m_bJumpQueued = false;
		sec m_JumpQueuedTime = 0.0f;
		sec m_TimeSinceJump = 100.0f;

		// Climbing
		glm::vec3 m_WallNormal = VEC3_FORWARD;
		bool m_bLeapQueued = false;
		// Ladder or rope being climbed (invalid when climbing terrain)
		GameObjectID m_ClimbObjectID;
		// Pulling up over the top of a wall onto m_MantleTarget (the player's center once standing)
		bool m_bMantling = false;
		glm::vec3 m_MantleTarget = VEC3_ZERO;
		// Rises to this height (the player's center) before moving over, to clear the lip
		real m_MantleClearY = 0.0f;
		sec m_MantleTime = 0.0f;
		sec m_MantleCooldown = 0.0f;
		real m_MantleLastY = 0.0f;
		u32 m_MantleCount = 0;

		RopeToss m_RopeToss;
		GameObjectID m_LastTossedRopeID;
		std::string m_RopeTossFailure;

		// Cached surroundings (updated each frame rather than each physics step)
		GroundInfo m_Ground;
		real m_HeightAboveGround = 0.0f;
		real m_Lift = 0.0f;
		real m_GroundSlipperiness = 0.0f;

		// Feedback
		glm::vec3 m_LastVelocity = VEC3_ZERO;
		real m_FOVOffset = 0.0f;
		real m_CameraRoll = 0.0f;
		real m_VarioTimer = 0.0f;
		AudioSourceID m_VarioBeepID = InvalidAudioSourceID;
		AudioSourceID m_HookFireID = InvalidAudioSourceID;
		AudioSourceID m_HookAttachID = InvalidAudioSourceID;
		AudioSourceID m_DeployID = InvalidAudioSourceID;

		// Rope & glider geometry (rebuilt each frame while visible, in child objects of the player). The glider's
		// fabric is two colours (each a mesh), its lines, risers & harness bar another.
		DynamicMesh* m_RopeMesh = nullptr;
		DynamicMesh* m_CanopyMesh = nullptr;
		DynamicMesh* m_CanopyTrimMesh = nullptr;
		DynamicMesh* m_GliderFrameMesh = nullptr;
		DynamicMesh* m_MotesMesh = nullptr;
		MaterialID m_MotesMaterialID = InvalidMaterialID;
		MaterialID m_RopeMaterialID = InvalidMaterialID;
		MaterialID m_CanopyMaterialID = InvalidMaterialID;
		MaterialID m_CanopyTrimMaterialID = InvalidMaterialID;
		MaterialID m_GliderFrameMaterialID = InvalidMaterialID;
		real m_CanopyOpenness = 0.0f;
		DynamicMesh* m_WingsuitMesh = nullptr;
		DynamicMesh* m_WingsuitTrimMesh = nullptr;
		MaterialID m_WingsuitMaterialID = InvalidMaterialID;
		MaterialID m_WingsuitTrimMaterialID = InvalidMaterialID;
		real m_WingsuitOpenness = 0.0f;

		// Air streaks: still in the world (the player rushes past them), respawned ahead once passed
		struct AirStreak
		{
			glm::vec3 pos = VEC3_ZERO;
			real age = 0.0f;
			real lengthScale = 1.0f;
			bool bAlive = false;
		};
		static const u32 MAX_AIR_STREAKS = 64;
		AirStreak m_AirStreaks[MAX_AIR_STREAKS];
		u32 m_AirStreakSeed = 0;
		u32 m_AirStreakCount = 0;
		real m_AirStreakIntensity = 0.0f;
		DynamicMesh* m_AirStreakMesh = nullptr;
		MaterialID m_AirStreakMaterialID = InvalidMaterialID;

		// Leaning into turns & pitching with the flight path (the player's mesh & the glider, not the capsule)
		GameObject* m_BodyMeshObject = nullptr;
		glm::vec3 m_BodyMeshBasePos = VEC3_ZERO;
		glm::quat m_BodyMeshBaseRot = QUAT_IDENTITY;
		glm::vec3 m_TiltLastVelocity = VEC3_ZERO;
		Spring<real> m_BankSpring;
		Spring<real> m_PitchSpring;
		// The pivot the body & glider lean around (the harness, above the player's center) & the lean as a world space
		// rotation (both updated with the render pose)
		glm::vec3 m_TiltPivot = VEC3_ZERO;
		glm::quat m_TiltRotation = QUAT_IDENTITY;

		real m_WindAngle = 0.0f;
		std::vector<Thermal> m_NearbyThermals;

		Swimming* m_Swimming = nullptr;
	};
} // namespace flex
