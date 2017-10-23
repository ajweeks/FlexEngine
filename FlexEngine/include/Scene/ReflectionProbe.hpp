#pragma once

#include "Scene/GameObject.hpp"

namespace flex
{
	class MeshPrefab;

	class ReflectionProbe : public GameObject
	{
	public:
		ReflectionProbe();
		~ReflectionProbe();

		virtual void Initialize(const GameContext& gameContext) override;
		virtual void PostInitialize(const GameContext& gameContext) override;
		virtual void Update(const GameContext& gameContext) override;
		virtual void Destroy(const GameContext& gameContext) override;

	private:
		MeshPrefab* m_MeshPrefab = nullptr;

		// bool enabled, float influenceRadius, bool update (float updateFrequency?), ...

		ReflectionProbe(const ReflectionProbe&) = delete;
		ReflectionProbe& operator=(const ReflectionProbe&) = delete;
	};
}
