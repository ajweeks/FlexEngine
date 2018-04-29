#pragma once

#include "Scene/GameObject.hpp"

namespace flex
{
	class MeshPrefab;

	class ReflectionProbe : public GameObject
	{
	public:
		ReflectionProbe(const std::string& name, bool startVisible = true, const glm::vec3& startPosition = glm::vec3(0.0f));
		virtual ~ReflectionProbe();

		virtual void Initialize(const GameContext& gameContext) override;
		virtual void PostInitialize(const GameContext& gameContext) override;

		MaterialID GetCaptureMaterialID() const;

		bool IsSphereVisible(const GameContext& gameContext) const;
		void SetSphereVisible(bool visible, const GameContext& gameContext);

	private:
		MeshPrefab* m_SphereMesh = nullptr; // The object visible in the scene
		GameObject* m_Capture = nullptr; // The object doing the capturing
		MaterialID m_CaptureMatID = 0;

		bool m_StartVisible = true;
		glm::vec3 m_StartPosition;

		// bool enabled, ivec4 influenceBoundingBox, bool update (real updateFrequency?), ...

		ReflectionProbe(const ReflectionProbe&) = delete;
		ReflectionProbe& operator=(const ReflectionProbe&) = delete;
	};
}
