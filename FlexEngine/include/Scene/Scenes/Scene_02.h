#pragma once

#include "Scene/Scenes/BaseScene.h"

#include <vector>

#include "Scene/MeshPrefab.h"
namespace flex
{
	class Scene_02 : public BaseScene
	{
	public:
		Scene_02(const GameContext& gameContext);
		virtual ~Scene_02();

		virtual void Initialize(const GameContext& gameContext) override;
		virtual void Destroy(const GameContext& gameContext) override;
		virtual void Update(const GameContext& gameContext) override;

	private:
		std::vector<MeshPrefab*> m_Cubes;

		MeshPrefab* m_Grid;

		MeshPrefab* m_Teapot;
		MeshPrefab* m_Orb;

		MeshPrefab* m_Skybox;

		Scene_02(const Scene_02&) = delete;
		Scene_02& operator=(const Scene_02&) = delete;

	};
} // namespace flex
