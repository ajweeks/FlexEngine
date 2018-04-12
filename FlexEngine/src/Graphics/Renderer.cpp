#include "stdafx.hpp"

#include "Graphics/Renderer.hpp"

#include "Logger.hpp"

namespace flex
{
	Renderer::Renderer()
	{
	}

	Renderer::~Renderer()
	{
	}

	void Renderer::SetReflectionProbeMaterial(MaterialID reflectionProbeMaterialID)
	{
		m_ReflectionProbeMaterialID = reflectionProbeMaterialID;
	}

	PhysicsDebuggingSettings& Renderer::GetPhysicsDebuggingSettings()
	{
		return m_PhysicsDebuggingSettings;
	}
} // namespace flex