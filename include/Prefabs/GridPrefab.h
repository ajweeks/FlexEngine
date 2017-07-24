#pragma once

#include "Prefab.h"

struct GridPrefab : public Prefab
{
	GridPrefab();
	~GridPrefab();

	void Init(const GameContext& gameContext, float rowWidth, int lineCount, const Transform& transform = Transform());
	virtual void Destroy(const GameContext& gameContext) override;

	virtual void Render(const GameContext& gameContext) override;

	float m_RowWidth;
	int m_LineCount;

	glm::uint m_RenderID;

	glm::uint m_UniformTimeID;
};
