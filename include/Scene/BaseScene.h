#pragma once

#include "FreeCamera.h"
#include "GameContext.h"

#include <string>

class GameObject;

class BaseScene
{
public:
	BaseScene(std::string name);
	virtual ~BaseScene();

	std::string GetName() const;

protected:
	virtual void Initialize(const GameContext& gameContext) = 0;
	virtual void Destroy(const GameContext& gameContext) = 0;
	virtual void UpdateAndRender(const GameContext& gameContext) = 0;

	void AddChild(GameObject* pGameObject);
	void RemoveChild(GameObject* pGameObject, bool deleteChild);
	void RemoveAllChildren(bool deleteChildren);

private:
	void RootInitialize(const GameContext& gameContext);
	void RootUpdateAndRender(const GameContext& gameContext);
	void RootDestroy(const GameContext& gameContext);

	friend class SceneManager;

	std::string m_Name;

	std::vector<GameObject*> m_Children;

	BaseScene(const BaseScene&) = delete;
	BaseScene& operator=(const BaseScene&) = delete;
};

