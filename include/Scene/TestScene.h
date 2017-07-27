#pragma once

#include "BaseScene.h"
#include "Prefabs/MeshPrefab.h"

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
	std::vector<MeshPrefab*> m_Cubes;

	MeshPrefab* m_Cube;
	MeshPrefab* m_Cube2;
	MeshPrefab* m_UVSphere;
	//MeshPrefab m_IcoSphere;

	MeshPrefab* m_Grid;
	MeshPrefab* m_CubeFromFile;
	MeshPrefab* m_Teapot;
	MeshPrefab* m_Scene;
	MeshPrefab* m_Landscape;

	glm::uint m_TimeID;

	size_t m_CubesLong;
	size_t m_CubesWide;

	TestScene(const TestScene&) = delete;
	TestScene& operator=(const TestScene&) = delete;

};