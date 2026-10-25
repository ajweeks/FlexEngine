#pragma once

#include "Audio/SoundRecipes.hpp"

// Game-facing sound effects & ambience. All sounds are synthesized procedurally (see Audio/SoundRecipes.cpp) on
// worker threads at startup; calls made before they're ready are silently ignored.
//
// Player footsteps, landings, wind, crickets & the listener's position are handled automatically in Update. Other
// systems call the Play* functions on events, & the Report*/UpdateVehicle functions every frame for looping sounds.
// Everything respects the master volume (the listener's gain) & the sfx/ambience volumes below.
namespace flex
{
	namespace GameSounds
	{
		using Surface = sound_recipes::Surface;

		// Kicks off generation (asynchronous)
		void Initialize();
		// Call before the job system is destroyed
		void Destroy();
		// Call once per frame (also while paused, to fade loops out)
		void Update();

		bool IsReady();

		// [0, 1] multipliers on top of the master volume
		void SetSfxVolume(real volume);
		void SetAmbienceVolume(real volume);

		// One shots
		void PlayFootstep(Surface surface, real gain = 1.0f);
		void PlayJump();
		// intensity: [0, 1], 0: a small hop, 1: a long fall
		void PlayLand(real intensity);
		void PlayGliderStow();
		// Putting an object down
		void PlayPlace(const glm::vec3& posWS);
		void PlayTorchIgnite(const glm::vec3& posWS);
		void PlayCampfireIgnite(const glm::vec3& posWS);
		// An axe hit on a tree & a felled tree hitting the ground
		void PlayChop(const glm::vec3& posWS);
		void PlayTreeFall(const glm::vec3& posWS);
		// Short UI click (e.g. a slider being released) & a brighter two note confirm
		void PlayUITick();
		void PlayUIConfirm();

		// Call every frame for each burning fire (intensity [0, 1]). A crackle loop plays at the nearest one.
		void ReportFire(const glm::vec3& posWS, real intensity);
		// Call every frame from the vehicle the player is driving. forwardSpeed in m/s, throttle in [-1, 1],
		// wheelSlip in [0, 1] (0: full grip).
		void UpdateVehicle(real forwardSpeed, real throttle, bool bBraking, real wheelSlip);

		// Which footstep sound to use when standing at footPosWS
		Surface GetSurfaceAt(const glm::vec3& footPosWS, const glm::vec3& groundNormal);
	} // namespace GameSounds
} // namespace flex
