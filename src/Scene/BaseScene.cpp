
#include "Scene/BaseScene.h"
#include "Logger.h"

#include <glm\vec3.hpp>

using namespace glm;

BaseScene::BaseScene(std::string name) :
	m_Name(name)
{
}

BaseScene::~BaseScene()
{
	delete m_Camera;
}

std::string BaseScene::GetName() const
{
	return m_Name;
}
