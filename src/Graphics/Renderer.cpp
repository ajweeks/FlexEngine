#include "stdafx.h"

#include "Graphics/Renderer.h"
#include "GameContext.h"

Renderer::Renderer()
{
}

Renderer::~Renderer()
{
}

Renderer::SceneInfo& Renderer::GetSceneInfo()
{
	return m_SceneInfo;
}
