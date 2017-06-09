
#include "stdafx.h"

#include "Graphics\MultiRenderer.h"
#include "Logger.h"

MultiRenderer::MultiRenderer(GameContext& gameContext)
{
}

MultiRenderer::~MultiRenderer()
{
	for (size_t i = 0; i < m_Renderers.size(); i++)
	{
		SafeDelete(m_Renderers[i]);
	}
	m_Renderers.clear();
}

void MultiRenderer::AddRenderer(Renderer* renderer)
{
	for (size_t i = 0; i < m_Renderers.size(); ++i)
	{
		if (m_Renderers[i] == renderer) return;
	}

	m_Renderers.push_back(renderer);
}

void MultiRenderer::RemoveRenderer(Renderer* renderer)
{
	for (size_t i = 0; i < m_Renderers.size(); ++i)
	{
		if (m_Renderers[i] == renderer)
		{
			m_Renderers.erase(m_Renderers.begin() + i);
			return;
		}
	}

	Logger::LogError("Renderer not found in MultiRenderer!");
}

void MultiRenderer::PostInitialize()
{
	for (size_t i = 0; i < m_Renderers.size(); ++i)
	{
		m_Renderers[i]->PostInitialize();
	}
}

glm::uint MultiRenderer::Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices)
{
	for (size_t i = 0; i < m_Renderers.size(); ++i)
	{
		m_Renderers[i]->Initialize(gameContext, vertices);
	}
	return glm::uint();
}

glm::uint MultiRenderer::Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices, std::vector<glm::uint>* indices)
{
	for (size_t i = 0; i < m_Renderers.size(); ++i)
	{
		m_Renderers[i]->Initialize(gameContext, vertices, indices);
	}
	return glm::uint();
}

void MultiRenderer::Draw(const GameContext& gameContext, glm::uint renderID)
{
	for (size_t i = 0; i < m_Renderers.size(); ++i)
	{
		m_Renderers[i]->Draw(gameContext, renderID);
	}
}

void MultiRenderer::OnWindowSize(int width, int height)
{
	for (size_t i = 0; i < m_Renderers.size(); i++)
	{
		m_Renderers[i]->OnWindowSize(width, height);
	}
}

void MultiRenderer::SetVSyncEnabled(bool enableVSync)
{
	for (size_t i = 0; i < m_Renderers.size(); ++i)
	{
		m_Renderers[i]->PostInitialize();
	}
}

void MultiRenderer::Clear(int flags, const GameContext& gameContext)
{
	for (size_t i = 0; i < m_Renderers.size(); ++i)
	{
		m_Renderers[i]->Clear(flags, gameContext);
	}
}

void MultiRenderer::SwapBuffers(const GameContext& gameContext)
{
	for (size_t i = 0; i < m_Renderers.size(); ++i)
	{
		m_Renderers[i]->SwapBuffers(gameContext);
	}
}

void MultiRenderer::UpdateTransformMatrix(const GameContext& gameContext, glm::uint renderID, const glm::mat4x4& model)
{
	for (size_t i = 0; i < m_Renderers.size(); ++i)
	{
		m_Renderers[i]->UpdateTransformMatrix(gameContext, renderID, model);
	}
}

int MultiRenderer::GetShaderUniformLocation(glm::uint program, const std::string uniformName)
{
	for (size_t i = 0; i < m_Renderers.size(); ++i)
	{
		return m_Renderers[i]->GetShaderUniformLocation(program, uniformName);
	}
	return 0;
}

void MultiRenderer::SetUniform1f(glm::uint location, float val)
{
	for (size_t i = 0; i < m_Renderers.size(); ++i)
	{
		m_Renderers[i]->SetUniform1f(location, val);
	}
}

void MultiRenderer::DescribeShaderVariable(glm::uint renderID, glm::uint program, const std::string& variableName, int size, 
	Renderer::Type renderType, bool normalized, int stride, void* pointer)
{
	for (size_t i = 0; i < m_Renderers.size(); ++i)
	{
		m_Renderers[i]->DescribeShaderVariable(renderID, program, variableName, size, renderType, normalized, stride, pointer);
	}
}

void MultiRenderer::Destroy(glm::uint renderID)
{
	for (size_t i = 0; i < m_Renderers.size(); ++i)
	{
		m_Renderers[i]->Destroy(renderID);
	}
}
