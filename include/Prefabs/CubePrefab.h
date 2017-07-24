#pragma once

#include "Prefab.h"

struct CubePrefab : public Prefab
{
	CubePrefab();
	~CubePrefab();

	void Init(const GameContext& gameContext, const Transform& transform);
	virtual void Destroy(const GameContext& gameContext) override;

	virtual void Render(const GameContext& gameContext) override;
	
	glm::uint m_RenderID;

	glm::uint m_UniformTimeID;
};
