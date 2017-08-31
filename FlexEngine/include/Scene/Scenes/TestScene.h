#pragma once

#include "Scene/Scenes/BaseScene.h"

#include <vector>

#include "Scene/MeshPrefab.h"

namespace flex
{
	class TestScene : public BaseScene
	{
	public:
		TestScene(const GameContext& gameContext);
		virtual ~TestScene();

		virtual void Initialize(const GameContext& gameContext) override;
		virtual void Destroy(const GameContext& gameContext) override;
		virtual void Update(const GameContext& gameContext) override;

	private:
		std::vector<MeshPrefab*> m_Cubes;

		MeshPrefab* m_Grid = nullptr;
		MeshPrefab* m_Plane = nullptr;
		MeshPrefab* m_Scene = nullptr;
		MeshPrefab* m_Landscape = nullptr;
		MeshPrefab* m_Cube = nullptr;
		MeshPrefab* m_Cube2 = nullptr;
		MeshPrefab* m_ChamferBox = nullptr;
		MeshPrefab* m_UVSphere = nullptr;
		MeshPrefab* m_Capsule = nullptr;
		MeshPrefab* m_Teapot = nullptr;
		MeshPrefab* m_Rock1 = nullptr;
		MeshPrefab* m_Rock2 = nullptr;
		MeshPrefab* m_Skybox = nullptr;

		MeshPrefab* m_TransformManipulator_1 = nullptr;
		MeshPrefab* m_TransformManipulator_2 = nullptr;
		MeshPrefab* m_TransformManipulator_3 = nullptr;
		MeshPrefab* m_TransformManipulator_4 = nullptr;
		MeshPrefab* m_TransformManipulator_5 = nullptr;

		PointLightID m_PointLight1ID;
		PointLightID m_PointLight2ID;
		PointLightID m_PointLight3ID;
		PointLightID m_PointLight4ID;

		TestScene(const TestScene&) = delete;
		TestScene& operator=(const TestScene&) = delete;
	};
} // namespace flex
