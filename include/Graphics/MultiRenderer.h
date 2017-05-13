#pragma once

#include "Renderer.h"

#include <vector>

struct GameContext;

class MultiRenderer : public Renderer
{
public:
	MultiRenderer(GameContext& gameContext);
	virtual ~MultiRenderer();

	void AddRenderer(Renderer* renderer);
	void RemoveRenderer(Renderer* renderer);

	virtual void PostInitialize() override;

	virtual glm::uint Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices) override;
	virtual glm::uint Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices, std::vector<glm::uint>* indices) override;
	
	virtual void Draw(const GameContext& gameContext, glm::uint renderID) override;

	virtual void OnWindowSize(int width, int height) override;

	virtual void SetVSyncEnabled(bool enableVSync) override;
	virtual void Clear(int flags, const GameContext& gameContext) override;
	virtual void SwapBuffers(const GameContext& gameContext) override;

	virtual void UpdateTransformMatrix(const GameContext& gameContext, glm::uint renderID, const glm::mat4x4& model) override;
	
	virtual int GetShaderUniformLocation(glm::uint program, const std::string uniformName) override;
	virtual void SetUniform1f(glm::uint location, float val) override;

	virtual void DescribeShaderVariable(glm::uint renderID, glm::uint program, const std::string& variableName, int size, Renderer::Type renderType, bool normalized,
		int stride, void* pointer) override;

	virtual void Destroy(glm::uint renderID) override;

private:
	std::vector<Renderer*> m_Renderers;


};