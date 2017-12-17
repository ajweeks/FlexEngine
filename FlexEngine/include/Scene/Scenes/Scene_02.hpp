#pragma once

#include "Scene/Scenes/BaseScene.hpp"

#include <vector>

#include "Scene/MeshPrefab.hpp"

namespace flex
{
	class ReflectionProbe;

	class Scene_02 : public BaseScene
	{
	public:
		Scene_02(const GameContext& gameContext);
		virtual ~Scene_02();

		virtual void Initialize(const GameContext& gameContext) override;
		virtual void PostInitialize(const GameContext& gameContext) override;
		virtual void Destroy(const GameContext& gameContext) override;
		virtual void Update(const GameContext& gameContext) override;

	private:
		std::vector<MeshPrefab*> m_Spheres;

		MeshPrefab* m_Arisaka = nullptr;
		MeshPrefab* m_Cerberus = nullptr;

		MaterialID m_SkyboxMatID_1;
		MaterialID m_SkyboxMatID_2;
		MaterialID m_SkyboxMatID_3;
		MaterialID m_SkyboxMatID_4;
		MaterialID m_SkyboxMatID_5;
		i32 m_CurrentSkyboxMatID;

		MeshPrefab* m_Skybox = nullptr;
		MeshPrefab* m_Grid = nullptr;

		ReflectionProbe* m_ReflectionProbe = nullptr;

		Scene_02(const Scene_02&) = delete;
		Scene_02& operator=(const Scene_02&) = delete;

	};
} // namespace flex
