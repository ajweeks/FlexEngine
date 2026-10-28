#pragma once

namespace flex
{
	class GameObject;
	class BaseScene;

	// Water queries & buoyancy shared by swimming, dropped items & physics objects
	namespace water
	{
		// kg/m^3
		static constexpr real DENSITY = 1000.0f;
		// m/s^2, downwards (matches the physics world's gravity)
		static constexpr real GRAVITY_ACCEL = 9.81f;
		// The density used for anything not listed in GetDensity (light enough to float)
		static constexpr real DEFAULT_OBJECT_DENSITY = 800.0f;

		// Height of the water surface above/below pos (lakes, the sea & any GerstnerWave ocean). False if there's no
		// water at pos (pos may be above or below the surface).
		bool SampleSurface(const glm::vec3& pos, real& outSurfaceHeight);

		// How dense an object (or the item a prefab makes) is, in kg/m^3: below DENSITY floats, above sinks. Batteries,
		// vehicles & metal tools sink; wood, plants & most other things float.
		real GetDensity(StringID typeID, const std::string& name);
		real GetDensity(GameObject* object);
		real GetPrefabDensity(const PrefabID& prefabID);

		// Applies buoyancy & water drag to every dynamic rigid body in the scene's physics world which doesn't
		// handle water itself (boats & the player do). Call once per fixed step.
		void FixedUpdateBuoyancy(BaseScene* scene);

		// Despawns objects which have fallen below the world (see terrain::DESPAWN_HEIGHT). Cheap, call every frame.
		void UpdateDespawning(BaseScene* scene);
	} // namespace water
} // namespace flex
