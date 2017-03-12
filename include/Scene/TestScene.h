#pragma once

#include "BaseScene.h"

#include "Primitives.h"

class TestScene : public BaseScene
{
public:
	TestScene(const GameContext& gameContext);
	virtual ~TestScene();

	void UpdateAndRender(const GameContext& gameContext) override;

private:

	CubePosCol m_Cube;
	CubePosCol m_Cube2;
	SpherePosCol m_Sphere1;

	TestScene(const TestScene&) = delete;
	TestScene& operator=(const TestScene&) = delete;

};