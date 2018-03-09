#pragma once

#include "Scene/GameObject.hpp"

namespace flex
{
	class MeshPrefab;

	class ReflectionProbe : public GameObject
	{
	public:
		ReflectionProbe(bool visible = true);
		~ReflectionProbe();

		virtual void Initialize(const GameContext& gameContext) override;
		virtual void PostInitialize(const GameContext& gameContext) override;
		virtual void Update(const GameContext& gameContext) override;
		virtual void Destroy(const GameContext& gameContext) override;

		MaterialID GetCaptureMaterialID() const;
		void SetSphereVisible(bool visible, const GameContext& gameContext);

		Transform& GetTransform() override;

	private:
		MeshPrefab* m_SphereMesh = nullptr; // The object visible in the scene
		GameObject* m_Capture = nullptr; // The object doing the capturing
		MaterialID m_CaptureMatID = 0;

		bool m_Visible;

		// bool enabled, ivec4 influenceBoundingBox, bool update (real updateFrequency?), ...

		ReflectionProbe(const ReflectionProbe&) = delete;
		ReflectionProbe& operator=(const ReflectionProbe&) = delete;
	};
}
