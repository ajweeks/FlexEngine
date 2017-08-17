#include "stdafx.h"

#include "Graphics/Renderer.h"

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
