#pragma once
#if COMPILE_D3D

#include "Renderer.h"

using namespace DirectX;
using namespace DirectX::SimpleMath;

using Microsoft::WRL::ComPtr;

struct GameContext;

class D3DRenderer : public Renderer
{
public:
	D3DRenderer(GameContext& gameContext);
	virtual ~D3DRenderer();

	virtual glm::uint Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices) override;
	virtual glm::uint Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices,
		std::vector<glm::uint>* indices) override;
	
	virtual void PostInitialize() override;

	virtual void Draw(const GameContext& gameContext, glm::uint renderID) override;

	virtual void SetVSyncEnabled(bool enableVSync) override;
	virtual void Clear(int flags, const GameContext& gameContext) override;
	virtual void SwapBuffers(const GameContext& gameContext) override;

	virtual void UpdateTransformMatrix(const GameContext& gameContext, glm::uint renderID, const glm::mat4x4& model) override;

	virtual int GetShaderUniformLocation(glm::uint program, const std::string uniformName) override;
	virtual void SetUniform1f(glm::uint location, float val) override;

	virtual void DescribeShaderVariable(glm::uint renderID, glm::uint program, const std::string& variableName, int size,
		Renderer::Type renderType, bool normalized, int stride, void* pointer) override;

	virtual void Destroy(glm::uint renderID) override;

private:
	static glm::uint BufferTargetToD3DTarget(BufferTarget bufferTarget);
	static glm::uint TypeToD3DType(Type type);
	static glm::uint UsageFlagToD3DUsageFlag(UsageFlag usage);
	static glm::uint ModeToD3DMode(Mode mode);

	struct RenderObject
	{
		glm::uint renderID;

		glm::uint VAO;
		glm::uint VBO;
		glm::uint IBO;

		glm::uint vertexBuffer;
		std::vector<VertexPosCol>* vertices = nullptr;

		bool indexed;
		glm::uint indexBuffer;
		std::vector<glm::uint>* indices = nullptr;

		glm::uint MVP;
	};

	RenderObject* GetRenderObject(int renderID);

	// TODO: use sorted data type (map)
	std::vector<RenderObject*> m_RenderObjects;

	bool m_VSyncEnabled;
	glm::uint m_Program;

	D3DRenderer(const D3DRenderer&) = delete;
	D3DRenderer& operator=(const D3DRenderer&) = delete;

	// --------- Tutorial Code ---------
	void CreateDevice();
	void CreateResources(const GameContext& gameContext);

	void OnDeviceLost(const GameContext& gameContext);

	D3D_FEATURE_LEVEL m_featureLevel;
	Microsoft::WRL::ComPtr<ID3D11Device> m_d3dDevice;
	Microsoft::WRL::ComPtr<ID3D11Device1> m_d3dDevice1;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_d3dContext;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext1> m_d3dContext1;

	Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
	Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain1;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTargetView;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_depthStencilView;

	// Rendering loop timer
	DirectX::XMVECTORF32 m_ClearColor = DirectX::Colors::DarkGoldenrod;

	// Sprite
	//Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_Texture;
	//std::unique_ptr<DirectX::SpriteBatch> m_SpriteBatch;
	//DirectX::SimpleMath::Vector2 m_ScreenPos;
	//DirectX::SimpleMath::Vector2 m_Origin;
	//RECT m_TileRect;
	//std::unique_ptr<DirectX::CommonStates> m_States;

	// Font
	//std::unique_ptr<DirectX::SpriteFont> m_Font;
	//DirectX::SimpleMath::Vector2 m_FontPos;
	//std::unique_ptr<DirectX::SpriteBatch> m_FontSpriteBatch;
	//
	//std::unique_ptr<DirectX::PrimitiveBatch<DirectX::VertexPositionColor>> m_VertexBatch;

	DirectX::SimpleMath::Matrix m_World;
	DirectX::SimpleMath::Matrix m_View;
	DirectX::SimpleMath::Matrix m_Proj;

	//std::unique_ptr<DirectX::BasicEffect> m_Effect;
	//Microsoft::WRL::ComPtr<ID3D11InputLayout> m_InputLayout;
	//std::unique_ptr<DirectX::CommonStates> m_States;
	//std::unique_ptr<DirectX::IEffectFactory> m_fxFactory;
	//std::unique_ptr<DirectX::Model> m_Model;

	// Sphere
	std::unique_ptr<DirectX::GeometricPrimitive> m_Cube;
	//Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_Texture;

	//Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_RasterizerState;

	bool m_Wireframe = false;
	bool m_EnableMSAA = false;
};

#endif // COMPILE_D3D