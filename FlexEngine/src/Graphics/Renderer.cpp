#include "stdafx.h"

#include "Graphics/Renderer.h"

namespace flex
{
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
} // namespace flex