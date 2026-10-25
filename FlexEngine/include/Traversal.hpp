#pragma once

class btCollisionObject;
class btRigidBody;

namespace flex
{
	class Player;
	class Mesh;
	class GameObject;
	class TerrainGenerator;

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
		bool IsGrappling() const { return m_Hook.state != HookState::NONE; }
		// Whether the player is flying/sliding fast enough that walking speed limits shouldn't apply
		bool HasMomentum() const;

		// Camera effects
		real GetFOVOffset() const { return m_FOVOffset; }
		real GetCameraRoll() const { return m_CameraRoll; }

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
		bool IsHookAttached() const { return m_Hook.state == HookState::ATTACHED; }
		real GetRopeLength() const { return m_Hook.ropeLength; }
		glm::vec3 GetHookPos() const { return m_Hook.pos; }
		real GetStamina() const { return m_Stamina; }

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
			real ropeLength = 0.0f;
			bool bReelingIn = false;
			bool bReelingOut = false;
		};

		struct Settings
		{
			real jumpSpeed = 7.5f;
			real coyoteTime = 0.15f;

			real glideSpeed = 12.0f;
			real glideSpeedBoost = 5.0f; // Added/removed with forward/back input
			real glideSinkRate = 2.0f;
			real glideMinHeight = 3.0f; // Above the ground, to deploy

			real wingsuitLift = 0.035f;
			real wingsuitDrag = 0.0018f;
			real wingsuitMaxLift = 40.0f; // m/s^2
			real wingsuitMaxLiftAngle = 45.0f; // Degrees, steeper angles of attack produce no more lift

			real hookRange = 150.0f;
			real hookSpeed = 180.0f;
			real reelSpeed = 16.0f;
			real swingControl = 7.0f; // Acceleration from move input while hanging

			real climbSpeed = 3.0f;
			real climbReach = 1.9f; // From the player's center to the wall
			real climbStaminaDrain = 0.07f; // Per second while hanging
			real climbMoveStaminaDrain = 0.12f; // Extra per second while moving
			real climbLeapStamina = 0.2f;
			real staminaRegen = 0.35f; // Per second while grounded

			real skiMaxSpeed = 48.0f;
			real skiTurnRate = 2.2f; // Radians per second
			real skiPush = 4.0f;
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

		void FixedUpdateHook(glm::vec3& velocity);
		void FixedUpdateGlide(glm::vec3& velocity);
		void FixedUpdateWingsuit(glm::vec3& velocity);
		bool FixedUpdateClimb(glm::vec3& velocity);
		void FixedUpdateSki(glm::vec3& velocity);

		// Raycasts against physics colliders (ignoring the player) & the terrain's density
		bool Raycast(const glm::vec3& from, const glm::vec3& to, glm::vec3& outHitPos, glm::vec3& outHitNormal) const;
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

		// Climbing
		glm::vec3 m_WallNormal = VEC3_FORWARD;
		bool m_bLeapQueued = false;

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

		// Rope & glider canopy geometry (rebuilt each frame while visible, in child objects of the player)
		DynamicMesh* m_RopeMesh = nullptr;
		DynamicMesh* m_CanopyMesh = nullptr;
		DynamicMesh* m_MotesMesh = nullptr;
		MaterialID m_MotesMaterialID = InvalidMaterialID;
		MaterialID m_RopeMaterialID = InvalidMaterialID;
		MaterialID m_CanopyMaterialID = InvalidMaterialID;
		real m_CanopyOpenness = 0.0f;

		real m_WindAngle = 0.0f;
		std::vector<Thermal> m_NearbyThermals;
	};
} // namespace flex
