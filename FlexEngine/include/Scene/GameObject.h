#pragma once

#include <vector>

#include "GameContext.h"

class GameObject
{
public:
	GameObject(GameObject* pParent = nullptr);
	virtual ~GameObject();

protected:
	virtual void Initialize(const GameContext& gameContext) = 0;
	virtual void Update(const GameContext& gameContext) = 0;
	virtual void Destroy(const GameContext& gameContext) = 0;

private:
	void RootInitialize(const GameContext& gameContext);
	void RootUpdate(const GameContext& gameContext);
	void RootDestroy(const GameContext& gameContext);

	friend class BaseScene;

	GameObject* m_pParent = nullptr;

	std::vector<GameObject*> m_Children;

};