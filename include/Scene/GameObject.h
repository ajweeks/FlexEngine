#pragma once

#include <vector>

struct GameContext;

class GameObject
{
public:
	GameObject(GameObject* pParent = nullptr);
	virtual ~GameObject();

protected:
	virtual void Initialize(const GameContext& gameContext) = 0;
	virtual void UpdateAndRender(const GameContext& gameContext) = 0;
	virtual void Destroy(const GameContext& gameContext) = 0;

private:
	void RootInitialize(const GameContext& gameContext);
	void RootUpdateAndRender(const GameContext& gameContext);
	void RootDestroy(const GameContext& gameContext);

	friend class BaseScene;

	GameObject* m_pParent = nullptr;

	std::vector<GameObject*> m_Children;

};