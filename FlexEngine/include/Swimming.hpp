#pragma once

namespace flex
{
	class Player;

	// Swimming & breath: the player floats at the surface of deep water, dives (crouch) & swims up (jump), & holds
	// their breath while their head is under. Once it runs out the screen flashes black, & they drown after
	// DROWN_TIME. Also keeps the first person camera out of the water line & drives the underwater look & sound.
	// Owned by Traversal, which hands it control of the player's velocity while swimming.
	class Swimming final
	{
	public:
		void Initialize(Player* player);
		// Back to full breath, out of the water (respawning/teleporting)
		void Reset();

		// Every frame: breath, drowning, underwater rendering & audio
		void Update();
		// Every physics step. Returns true while swimming (velocity is then set by this & walking is skipped).
		bool FixedUpdate(glm::vec3& velocity, bool bCanControl, real moveForward, real moveRight, bool bSwimUp, bool bSwimDown, bool bSprint);

		// Moves a camera at pos off the water line (where the near plane would cut through the surface, showing half
		// above & half below), keeping it on whichever side it was last on. Sets the underwater look to match.
		void AdjustCameraForWaterLine(glm::vec3& pos);

		void DrawHUD() const;
		void DrawImGui();

		bool IsSwimming() const { return m_bSwimming; }
		bool IsHeadUnderwater() const { return m_bHeadUnderwater; }
		// [0, 1] of the player's height which is under water
		real GetSubmersion() const { return m_Submersion; }
		real GetBreath() const { return m_Breath; }
		real GetBreathFraction() const { return m_Breath / MAX_BREATH; }
		// Seconds since breath ran out (0 while there's breath left)
		sec GetDrowningTime() const { return m_DrowningTime; }
		// Black overlay while drowning [0, 1]
		real GetBlackoutAlpha() const { return m_BlackoutAlpha; }
		// Number of times the player has drowned (for testing)
		u32 GetDrownCount() const { return m_DrownCount; }

		// Seconds of breath when diving from full
		static constexpr sec MAX_BREATH = 20.0f;
		// Seconds after breath runs out until the player drowns
		static constexpr sec DROWN_TIME = 8.0f;

	private:
		void Drown();
		void UpdateTest();
		void UpdateUnderwaterEffects(bool bUnderwater, real depth);

		Player* m_Player = nullptr;

		bool m_bSwimming = false;
		bool m_bHeadUnderwater = false;
		bool m_bCameraUnderwater = false;
		real m_Submersion = 0.0f;
		real m_SurfaceHeight = 0.0f;
		bool m_bHasSurface = false;
		real m_Breath = MAX_BREATH;
		sec m_DrowningTime = 0.0f;
		real m_BlackoutAlpha = 0.0f;
		real m_UnderwaterAmount = 0.0f;
		u32 m_DrownCount = 0;
		sec m_TimeSinceSplash = 10.0f;

		// FLEX_TEST_SWIM
		i32 m_TestState = 0;
		sec m_TestTime = 0.0f;
		bool m_bTestDive = false;
		glm::vec3 m_TestWaterPos = VEC3_ZERO;
		GameObjectID m_TestWoodID = InvalidGameObjectID;
		GameObjectID m_TestBatteryID = InvalidGameObjectID;
	};
} // namespace flex
