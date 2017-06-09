#pragma once

#include "BaseScene.h"
#include "Prefabs/CubePrefab.h"
#include "Prefabs/SpherePrefab.h"
#include "Prefabs/GridPrefab.h"

#include <vector>

class TestScene : public BaseScene
{
public:
	TestScene(const GameContext& gameContext);
	virtual ~TestScene();
	
	virtual void Initialize(const GameContext& gameContext) override;
	virtual void Destroy(const GameContext& gameContext) override;
	virtual void UpdateAndRender(const GameContext& gameContext) override;

private:
	std::vector<CubePrefab> m_Cubes;

	CubePrefab m_Cube;
	CubePrefab m_Cube2;
	SpherePrefab m_Sphere1;

	GridPrefab m_Grid;

	glm::uint m_TimeID;

	size_t m_CubesLong;
	size_t m_CubesWide;

	TestScene(const TestScene&) = delete;
	TestScene& operator=(const TestScene&) = delete;

};