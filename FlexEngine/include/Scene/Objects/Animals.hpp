#pragma once

#include "Scene/GameObject.hpp"

// Animals: goats, cows, chickens, crows, seagulls, roosters, squirrels & horses. Spawned deterministically (from the
// world seed) per terrain cell around the player, appropriate to each cell's biome. Only animals within SIM_RADIUS of
// the player are simulated (the nearest every frame, the rest round-robin with larger steps); the others are kept as
// dormant records, & wild ones far enough away are forgotten (their cell is regenerated from the seed when the player
// returns). Tamed animals (caught with a lasso) are kept forever, can be named, follow the player & can be tied to
// fence posts. Animals sleep at the times their species normally does.
//
// Navigation is probe (ray cast) based: each step tries a few headings around the desired one, sampling the terrain's
// height & water at the step's end (& casting a ray against built structures near the player), & takes the first
// which isn't too steep, under water or blocked.
namespace flex
{
	class GameUI;
	struct JSONObject;

	enum class AnimalSpecies : u8
	{
		GOAT,
		COW,
		CHICKEN,
		CROW,
		SEAGULL,
		ROOSTER,
		SQUIRREL,
		HORSE,

		_COUNT
	};

	const char* AnimalSpeciesName(AnimalSpecies species);
	// Localized
	const char* AnimalSpeciesDisplayName(AnimalSpecies species);

	namespace Animals
	{
		static constexpr real SIM_RADIUS = 2000.0f;
		static constexpr real CELL_SIZE = 256.0f;

		// Called every frame while the game world is being played: spawns, simulates & draws the animals
		void Update();
		// Aim prompt, & the dialog for naming a newly tamed animal
		void DrawHUD(GameUI& ui);
		// While naming an animal (the cursor is shown & player input ignored)
		bool IsNamingDialogOpen();

		// Saved with the world (see Game::SaveWorld): adds "animals" & "animal cells" fields
		void Serialize(JSONObject& worldObj);
		// Replaces all animals with those saved in the world (none if it has no animal fields)
		void Load(const JSONObject& worldObj);
		void Clear();

		u32 GetAnimalCount();
		u32 GetSimulatedCount();
	} // namespace Animals

	// Draws the simulated animals near the camera (one instance in the scene, not serialized)
	class AnimalRenderer final : public GameObject
	{
	public:
		AnimalRenderer();

		virtual void Initialize() override;
		virtual void Destroy(bool bDetachFromParent = true) override;

		// Creates the scene's instance if there isn't one yet
		static AnimalRenderer* Get();

		struct ColourMesh;
		ColourMesh* GetMesh(u32 colour);
		void BeginFrame(const glm::vec3& origin);
		void EndFrame();

	private:
		std::vector<ColourMesh*> m_Meshes;

		static AnimalRenderer* s_Instance;
	};
} // namespace flex
