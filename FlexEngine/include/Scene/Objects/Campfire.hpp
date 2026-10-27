#pragma once

#include "PlacementPreview.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/TerrainGeneration.hpp" // For PlantInfo

// Woodcraft objects: campfires (crafted from wood, lit by holding E, burning down to a scorched patch), trees falling
// after being chopped down (dropping wood), & the little bits of debris these throw around
namespace flex
{
	struct VertexBufferDataCreateInfo;

	namespace woodcraft
	{
		// Procedural geometry rebuilt every frame (vertices are given in world space)
		class DynamicMesh
		{
		public:
			DynamicMesh() = default;
			~DynamicMesh();
			DynamicMesh(const DynamicMesh&) = delete;
			DynamicMesh& operator=(const DynamicMesh&) = delete;

			void Create(GameObject* parent, const char* name, MaterialID materialID, u32 initialMaxVertexCount = 1024);

			void Begin();
			// Flat shaded
			void AddTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);
			void AddBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation);
			void AddOctahedron(const glm::vec3& center, const glm::vec3& radii, const glm::quat& rotation);
			// Converts to the object's local space & uploads (hiding the object when there's nothing to draw)
			void Upload();

			GameObject* object = nullptr;

		private:
			VertexBufferDataCreateInfo* m_VertexData = nullptr;
			std::vector<u32> m_Indices;
		};

		struct Particle
		{
			glm::vec3 pos;
			glm::vec3 vel;
			glm::quat rot;
			glm::vec3 spin; // Angular velocity (radians/s)
			real size;
			real age;
			real life;
		};

		// Simple CPU simulated box/octahedron particles drawn with one material
		struct ParticleSet
		{
			void Create(GameObject* parent, const char* name, MaterialID materialID, bool bOctahedra);
			void Spawn(const Particle& particle);
			// gravity: m/s^2 (downwards), drag: per second, growth: size multiplier per second
			void Update(real dt, real gravity, real drag, real growth);
			bool Empty() const { return m_Particles.empty(); }
			void Clear() { m_Particles.clear(); }

			DynamicMesh mesh;
			bool bOctahedra = false;
			std::vector<Particle> m_Particles;
		};

		// Creates (or finds) a pbr material with constant values (sRGB albedo). Dynamic meshes need dynamic materials (&
		// static meshes static ones).
		MaterialID GetOrCreateMaterial(const char* name, const glm::vec4& albedo, real roughness, bool bDynamic);

		// Unlit (self illuminated) colour, for flames & embers (pbr materials don't support emission)
		MaterialID GetOrCreateGlowMaterial(const char* name, const glm::vec4& colour);

		// Wood item (prefab "wood")
		PrefabID GetWoodPrefabID();
		// Wood dropped by a felled plant
		i32 WoodYield(const terrain::PlantInfo& plant);
	} // namespace woodcraft

	// Wood chips, leaves & dust (one shared object per scene)
	class WoodcraftEffects final : public GameObject
	{
	public:
		WoodcraftEffects();

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;

		// Creates the scene's instance if there isn't one yet
		static WoodcraftEffects* Get();

		// Chips flying out of a trunk being chopped (normal: out of the trunk)
		void SpawnChips(const glm::vec3& pos, const glm::vec3& normal, u32 count, const glm::vec3& colour);
		void SpawnLeaves(const glm::vec3& center, real radius, u32 count);
		void SpawnDust(const glm::vec3& center, real radius, u32 count);

	private:
		woodcraft::ParticleSet m_Chips;
		woodcraft::ParticleSet m_Leaves;
		woodcraft::ParticleSet m_Dust;
		u32 m_Seed = 1;

		static WoodcraftEffects* s_Instance;
	};

	// A tree or cactus which has just been cut down (the terrain no longer contains it): topples over, then sinks
	// away, leaving wood to pick up
	class FallingPlant final : public GameObject
	{
	public:
		FallingPlant(const terrain::PlantInfo& plant, const glm::vec3& fallDirection);
		~FallingPlant();

		virtual void Initialize() override;
		virtual void Update() override;

		// Spawns a falling copy of the plant (hooked up to TerrainGenerator::s_OnPlantFelled)
		static void OnPlantFelled(const terrain::PlantInfo& plant, const glm::vec3& fallDirection);

	private:
		void UploadPose();
		void DropWood();

		terrain::PlantInfo m_Plant;
		glm::vec3 m_FallDirection;
		glm::vec3 m_Axis;

		// Original (world space) mesh
		std::vector<glm::vec3> m_Positions;
		std::vector<glm::vec3> m_Normals;
		std::vector<glm::vec4> m_Colours;
		std::vector<glm::vec4> m_Weights;
		std::vector<u32> m_Indices;
		VertexBufferDataCreateInfo* m_VertexData = nullptr;

		real m_Angle = 0.06f; // Radians from upright
		real m_AngularVelocity = 0.0f;
		real m_MaxAngle = 1.45f;
		u32 m_Bounces = 0;
		sec m_RestTime = 0.0f;
		real m_Scale = 1.0f;
		bool m_bLanded = false;
		// Stays upright (overlapping the terrain's copy) until the terrain has been regenerated without the plant
		bool m_bReleased = false;
		sec m_WaitTime = 0.0f;
		bool m_bWoodDropped = false;
	};

	class Campfire final : public GameObject
	{
	public:
		enum class State : u8
		{
			UNLIT,
			LIT,
			BURNT,

			_COUNT
		};

		Campfire(const std::string& name, const GameObjectID& gameObjectID = InvalidGameObjectID, const PrefabIDPair& sourcePrefabID = InvalidPrefabIDPair, bool bIsPrefabTemplate = false);

		static PropertyCollection* BuildTypeUniquePropertyCollection();

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;
		virtual void Update() override;

		State GetState() const { return m_State; }
		bool IsLit() const { return m_State == State::LIT; }
		void Light();
		sec GetBurnTimeRemaining() const { return m_BurnTimeRemaining; }
		// (for testing)
		void SetBurnTimeRemaining(sec burnTimeRemaining) { m_BurnTimeRemaining = burnTimeRemaining; }

		// A fresh campfire's shape (local space, on flat ground) for placement previews
		static void BuildPreviewGeometry(PlacementPreview::Geometry& geometry);

		// Nearest campfire within maxDistance of pos (in any state)
		static Campfire* FindNearest(const glm::vec3& pos, real maxDistance);
		static const std::vector<Campfire*>& GetAll() { return s_Campfires; }

		static constexpr sec BURN_DURATION = 600.0f;
		static constexpr sec LIGHT_HOLD_DURATION = 1.5f;
		static constexpr i32 WOOD_COST = 5;

	protected:
		virtual void ParseTypeUniqueFields(const JSONObject& parentObject, const std::vector<MaterialID>& matIDs) override;
		virtual void SerializeTypeUniqueFields(JSONObject& parentObject, bool bSerializePrefabData) override;

	private:
		void CreateMeshes();
		void ApplyStateVisibility();
		void UpdateFire(real intensity);
		// Gives the nearest lit campfires one of the (limited) point lights
		static void UpdateLights();
		static void ReleaseLights();

		State m_State = State::UNLIT;
		sec m_BurnTimeRemaining = BURN_DURATION;
		real m_Seed = 0.0f;

		GameObject* m_FreshLogs = nullptr;
		GameObject* m_Remains = nullptr; // Charred logs & scorched ground
		woodcraft::DynamicMesh m_Flames;
		woodcraft::DynamicMesh m_FlameCores;
		woodcraft::ParticleSet m_Embers;
		woodcraft::ParticleSet m_Smoke;
		sec m_EmberTimer = 0.0f;
		sec m_SmokeTimer = 0.0f;
		u32 m_ParticleSeed = 1;

		static std::vector<Campfire*> s_Campfires;
	};
} // namespace flex
