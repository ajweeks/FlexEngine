#pragma once

#include "Scene/BaseScene.h"

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

		MeshPrefab* m_Grid;
		MeshPrefab* m_Scene;
		MeshPrefab* m_Landscape;
		MeshPrefab* m_Cube;
		MeshPrefab* m_Cube2;
		MeshPrefab* m_ChamferBox;
		MeshPrefab* m_UVSphere;
		MeshPrefab* m_Capsule;
		MeshPrefab* m_Teapot;
		MeshPrefab* m_Rock1;
		MeshPrefab* m_Rock2;
		MeshPrefab* m_Skybox;

		MeshPrefab* m_TransformManipulator_1;
		MeshPrefab* m_TransformManipulator_2;
		MeshPrefab* m_TransformManipulator_3;
		MeshPrefab* m_TransformManipulator_4;
		MeshPrefab* m_TransformManipulator_5;

		TestScene(const TestScene&) = delete;
		TestScene& operator=(const TestScene&) = delete;
	};
} // namespace flex
