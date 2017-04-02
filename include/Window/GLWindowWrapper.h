#pragma once

#include "GLFWWindowWrapper.h"

struct GameContext;

class GLWindowWrapper : public GLFWWindowWrapper
{
public:
	GLWindowWrapper(std::string title, glm::vec2i size, GameContext& gameContext);
	virtual ~GLWindowWrapper();

private:
	GLWindowWrapper(const GLWindowWrapper&) = delete;
	GLWindowWrapper& operator=(const GLWindowWrapper&) = delete;
};
