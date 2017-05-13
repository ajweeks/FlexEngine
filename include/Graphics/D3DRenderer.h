#pragma once
#if COMPILE_D3D

#include "Renderer.h"

struct GameContext;

class D3DRenderer : public Renderer
{
public:
	D3DRenderer(GameContext& gameContext);
	virtual ~D3DRenderer();

	virtual glm::uint Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices) override;
	virtual glm::uint Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices,
		std::vector<glm::uint>* indices) override;

	virtual void SetClearColor(float r, float g, float b) override;

	virtual void PostInitialize() override;

	virtual void Draw(const GameContext& gameContext, glm::uint renderID) override;

	virtual void OnWindowSize(int width, int height) override;

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
	
	void PostProcess();

	D3D_FEATURE_LEVEL m_featureLevel;
	Microsoft::WRL::ComPtr<ID3D11Device> m_d3dDevice;
	Microsoft::WRL::ComPtr<ID3D11Device1> m_d3dDevice1;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_d3dContext;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext1> m_d3dContext1;

	Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
	Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain1;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTargetView;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_depthStencilView;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_backBuffer;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_sceneTex;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sceneSRV;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_sceneRT;

	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_rt1SRV;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rt1RT;

	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_rt2SRV;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rt2RT;

	RECT m_bloomRect;

	DirectX::XMVECTORF32 m_ClearColor;

	//Microsoft::WRL::ComPtr<ID3D11Buffer> m_shapeVB;
	//Microsoft::WRL::ComPtr<ID3D11Buffer> m_shapeIB;
	//Microsoft::WRL::ComPtr<ID3D11PixelShader> m_PixelShader;

	DirectX::SimpleMath::Matrix m_World;
	DirectX::SimpleMath::Matrix m_View;
	DirectX::SimpleMath::Matrix m_Projection;

	std::unique_ptr<DirectX::CommonStates> m_States;
	std::unique_ptr<DirectX::SpriteBatch> m_SpriteBatch;
	std::unique_ptr<DirectX::GeometricPrimitive> m_Shape;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_Background;
	RECT m_FullscreenRect;

	Microsoft::WRL::ComPtr<ID3D11PixelShader> m_BloomExtractPS;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> m_BloomCombinePS;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> m_GaussianBlurPS;

	Microsoft::WRL::ComPtr<ID3D11Buffer> m_BloomParams;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_BlurParamsWidth;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_BlurParamsHeight;

	//Microsoft::WRL::ComPtr<ID3D11InputLayout> m_InputLayout;
	//Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_Texture;
	//Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_Cubemap;

	bool m_Wireframe = false;
	bool m_EnableMSAA = false;
};

#endif // COMPILE_D3D