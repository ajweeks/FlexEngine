#pragma once

#include "Scene/Scenes/BaseScene.hpp"

#include <vector>

#include "Scene/MeshPrefab.hpp"

namespace flex
{
	class ReflectionProbe;

	class TestScene : public BaseScene
	{
	public:
		TestScene(const GameContext& gameContext);
		virtual ~TestScene();

		virtual void Initialize(const GameContext& gameContext) override;
		virtual void PostInitialize(const GameContext& gameContext) override;
		virtual void Destroy(const GameContext& gameContext) override;
		virtual void Update(const GameContext& gameContext) override;

	private:
		ReflectionProbe* m_ReflectionProbe = nullptr;

		PointLightID m_PointLight1ID;
		PointLightID m_PointLight2ID;
		PointLightID m_PointLight3ID;
		PointLightID m_PointLight4ID;

		TestScene(const TestScene&) = delete;
		TestScene& operator=(const TestScene&) = delete;
	};
} // namespace flex
