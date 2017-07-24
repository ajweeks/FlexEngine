#pragma once

#include "Prefab.h"

struct SpherePrefab : public Prefab
{
	enum class Type
	{
		ICOSPHERE,
		UVSPHERE
	};

	SpherePrefab();
	~SpherePrefab();

	void Init(const GameContext& gameContext, Type type = Type::ICOSPHERE, const Transform& transform = Transform());
	virtual void Destroy(const GameContext& gameContext) override;

	virtual void Render(const GameContext& gameContext) override;

	void SetIterationCount(size_t iterationCount);

	Type m_Type;
	size_t m_IterationCount;

	std::vector<VertexPosCol> m_Vertices;
	std::vector<glm::uint> m_Indices;

	glm::uint m_RenderID;

	glm::uint m_UniformTimeID;
};
