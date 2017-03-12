
#include "Scene/TestScene.h"

#include <glm/vec3.hpp>

using namespace glm;

TestScene::TestScene(const GameContext& gameContext) :
	BaseScene("TestScene")
{
	m_Sphere1.Init(gameContext, vec3(0.0f, 0.0f, 0.0f));
	m_Cube.Init(gameContext, vec3(0.0f, 0.0f, -6.0f), quat(vec3(0.0f, 1.0f, 0.0f)), vec3(3.0f, 1.0f, 1.0f));
	m_Cube2.Init(gameContext, vec3(0.0f, 0.0f, 4.0f), quat(vec3(0.0f, 0.0f, 2.0f)), vec3(1.0f, 5.0f, 1.0f));
}

TestScene::~TestScene()
{
}

void TestScene::UpdateAndRender(const GameContext& gameContext)
{
	m_Sphere1.Render(gameContext);
	m_Cube.Render(gameContext);
	m_Cube2.Render(gameContext);
}
