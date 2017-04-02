#include "stdafx.h"

#include "Graphics/Renderer.h"
#include "GameContext.h"

Renderer::Renderer(GameContext& gameContext)
{
	gameContext.renderer = this;
}

Renderer::~Renderer()
{
}
