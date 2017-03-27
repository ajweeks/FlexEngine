#pragma once

#include "BaseScene.h"
#include "Prefabs/CubePosCol.h"
#include "Prefabs/SpherePosCol.h"

#include <vector>

class TestScene : public BaseScene
{
public:
	TestScene(const GameContext& gameContext);
	virtual ~TestScene();
	
	virtual void Destroy(const GameContext& gameContext) override;

	void UpdateAndRender(const GameContext& gameContext) override;

private:

	std::vector<CubePosCol> m_Cubes;

	CubePosCol m_Cube;
	CubePosCol m_Cube2;
	SpherePosCol m_Sphere1;

	int m_CubesLong;
	int m_CubesWide;

	TestScene(const TestScene&) = delete;
	TestScene& operator=(const TestScene&) = delete;

};