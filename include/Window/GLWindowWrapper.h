#pragma once

#include "GLFWWindowWrapper.h"

struct GameContext;

class GLWindowWrapper : public GLFWWindowWrapper
{
public:
	GLWindowWrapper(std::string title, glm::vec2i size, GameContext& gameContext);
	virtual ~GLWindowWrapper();

	virtual void SetSize(int width, int height) override;

private:
	GLWindowWrapper(const GLWindowWrapper&) = delete;
	GLWindowWrapper& operator=(const GLWindowWrapper&) = delete;
};
