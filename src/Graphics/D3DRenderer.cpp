#include "stdafx.h"
#if COMPILE_D3D

#include "Graphics/D3DRenderer.h"
#include "GameContext.h"
#include "Window/Window.h"
#include "Logger.h"
#include "FreeCamera.h"
#include "ReadFile.h"

#include <algorithm>

using namespace glm;
using namespace DirectX;
using namespace DirectX::SimpleMath;
using Microsoft::WRL::ComPtr;

namespace
{
	struct VS_BLOOM_PARAMETERS
	{
		float bloomThreshold; 
		float blurAmount;
		float bloomIntensity;
		float baseIntensity;
		float bloomSaturation;
		float baseSaturation;
		uint8_t na[8];
	};

	static_assert(!(sizeof(VS_BLOOM_PARAMETERS) % 16),
		"VS_BLOOM_PARAMETERS needs to be 16 bytes aligned");

	struct VS_BLUR_PARAMETERS
	{
		static const size_t SAMPLE_COUNT = 15;

		XMFLOAT4 sampleOffsets[SAMPLE_COUNT];
		XMFLOAT4 sampleWeights[SAMPLE_COUNT];

		void SetBlurEffectParameters(float dx, float dy,
			const VS_BLOOM_PARAMETERS& params)
		{
			sampleWeights[0].x = ComputeGaussian(0, params.blurAmount);
			sampleOffsets[0].x = sampleOffsets[0].y = 0.f;

			float totalWeights = sampleWeights[0].x;

			// Add pairs of additional sample taps, positioned
			// along a line in both directions from the center.
			for (size_t i = 0; i < SAMPLE_COUNT / 2; i++)
			{
				// Store weights for the positive and negative taps.
				float weight = ComputeGaussian(float(i + 1.f), params.blurAmount);

				sampleWeights[i * 2 + 1].x = weight;
				sampleWeights[i * 2 + 2].x = weight;

				totalWeights += weight * 2;

				// To get the maximum amount of blurring from a limited number of
				// pixel shader samples, we take advantage of the bilinear filtering
				// hardware inside the texture fetch unit. If we position our texture
				// coordinates exactly halfway between two texels, the filtering unit
				// will average them for us, giving two samples for the price of one.
				// This allows us to step in units of two texels per sample, rather
				// than just one at a time. The 1.5 offset kicks things off by
				// positioning us nicely in between two texels.
				float sampleOffset = float(i) * 2.f + 1.5f;

				Vector2 delta = Vector2(dx, dy) * sampleOffset;

				// Store texture coordinate offsets for the positive and negative taps.
				sampleOffsets[i * 2 + 1].x = delta.x;
				sampleOffsets[i * 2 + 1].y = delta.y;
				sampleOffsets[i * 2 + 2].x = -delta.x;
				sampleOffsets[i * 2 + 2].y = -delta.y;
			}

			for (size_t i = 0; i < SAMPLE_COUNT; i++)
			{
				sampleWeights[i].x /= totalWeights;
			}
		}

	private:
		float ComputeGaussian(float n, float theta)
		{
			return (float)((1.0 / sqrtf(2 * XM_PI * theta))
				* expf(-(n * n) / (2 * theta * theta)));
		}
	};

	static_assert(!(sizeof(VS_BLUR_PARAMETERS) % 16),
		"VS_BLUR_PARAMETERS needs to be 16 bytes aligned");

	enum BloomPresets
	{
		Default = 0,
		Soft,
		Desaturated,
		Saturated,
		Blurry,
		Subtle,
		None
	};

	BloomPresets g_Bloom = BloomPresets::Subtle;

	static const VS_BLOOM_PARAMETERS g_BloomPresets[] =
	{
		//Thresh  Blur Bloom  Base  BloomSat BaseSat
		{ 0.25f,  4,   1.25f, 1,    1,       1 }, // Default
		{ 0,      3,   1,     1,    1,       1 }, // Soft
		{ 0.5f,   8,   2,     1,    0,       1 }, // Desaturated
		{ 0.25f,  4,   2,     1,    2,       0 }, // Saturated
		{ 0,      2,   1,     0.1f, 1,       1 }, // Blurry
		{ 0.5f,   2,   1,     1,    1,       1 }, // Subtle
		{ 0.25f,  4,   1.25f, 1,    1,       1 }, // None
	};
};

D3DRenderer::D3DRenderer(GameContext& gameContext) :
	m_Program(gameContext.program),

	m_featureLevel(D3D_FEATURE_LEVEL_9_1)
{
	//glClearColor(0.08f, 0.13f, 0.2f, 1.0f);

	//glEnable(GL_DEPTH_TEST);
	//glDepthFunc(GL_LESS);

	//glUseProgram(gameContext.program);

	//LoadAndBindGLTexture("resources/images/test2.jpg", m_TextureID);
	//glUniform1i(glGetUniformLocation(m_ProgramID, "texTest"), 0);

	CreateDevice();
	CreateResources(gameContext);

	// TODO: Change the timer settings if you want something other than the default variable timestep mode.
	// e.g. for 60 FPS fixed timestep update logic, call:
	/*
	m_timer.SetFixedTimeStep(true);
	m_timer.SetTargetElapsedSeconds(1.0 / 60);
	*/
}

D3DRenderer::~D3DRenderer()
{
	// TODO: Terminate D3D
}

uint D3DRenderer::Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices)
{
	const uint renderID = m_RenderObjects.size();

	RenderObject* object = new RenderObject();
	object->renderID = renderID;

	//glGenVertexArrays(1, &object->VAO);
	//glBindVertexArray(object->VAO);
	//
	//glGenBuffers(1, &object->VBO);
	//glBindBuffer(GL_ARRAY_BUFFER, object->VBO);
	//glBufferData(GL_ARRAY_BUFFER, sizeof(vertices->at(0)) * vertices->size(), vertices->data(), GL_STATIC_DRAW);
	//
	//object->vertices = vertices;
	//
	//uint posAttrib = glGetAttribLocation(gameContext.program, "in_Position");
	//glEnableVertexAttribArray(posAttrib);
	//glVertexAttribPointer(posAttrib, 3, GL_FLOAT, false, VertexPosCol::stride, 0);
	//
	//object->MVP = glGetUniformLocation(gameContext.program, "in_MVP");

	m_RenderObjects.push_back(object);

	//glBindVertexArray(0);

	return renderID;
}

uint D3DRenderer::Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices, std::vector<uint>* indices)
{
	const uint renderID = Initialize(gameContext, vertices);

	RenderObject* object = GetRenderObject(renderID);

	object->indices = indices;
	object->indexed = true;

	//glGenBuffers(1, &object->IBO);
	//glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, object->IBO);
	//glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices->at(0)) * indices->size(), indices->data(), GL_STATIC_DRAW);

	return renderID;
}

void D3DRenderer::PostInitialize()
{
}

void D3DRenderer::Draw(const GameContext& gameContext, uint renderID)
{
	UNREFERENCED_PARAMETER(gameContext);

	RenderObject* renderObject = GetRenderObject(renderID);

	//glBindVertexArray(renderObject->VAO);
	//
	//glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);
	//
	//if (renderObject->indexed)
	//{
	//	glDrawElements(GL_TRIANGLES, renderObject->indices->size(), GL_UNSIGNED_INT, (void*)renderObject->indices->data());
	//}
	//else
	//{
	//	glDrawArrays(GL_TRIANGLES, 0, renderObject->vertices->size());
	//}
	//
	//glBindVertexArray(0);

	//m_World = Matrix::CreateRotationY(cosf(a * 0.5f) * 0.5f);

	m_SpriteBatch->Begin();
	m_SpriteBatch->Draw(m_Background.Get(), m_FullscreenRect);
	m_SpriteBatch->End();

	m_Shape->Draw(m_World, m_View, m_Projection);
}

void D3DRenderer::SetVSyncEnabled(bool enableVSync)
{
	m_VSyncEnabled = enableVSync;
}

void D3DRenderer::Clear(int flags, const GameContext& gameContext)
{
	UINT d3dClearFlags;
	if (flags & (int)ClearFlag::DEPTH) d3dClearFlags |= D3D11_CLEAR_DEPTH;
	if (flags & (int)ClearFlag::STENCIL) d3dClearFlags |= D3D11_CLEAR_STENCIL;

	//m_d3dContext->ClearRenderTargetView(m_renderTargetView.Get(), m_ClearColor);
	m_d3dContext->ClearDepthStencilView(m_depthStencilView.Get(), d3dClearFlags, 1.0f, 0);

	//m_d3dContext->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());
	m_d3dContext->OMSetRenderTargets(1, m_sceneRT.GetAddressOf(), m_depthStencilView.Get());

	const vec2i windowSize = gameContext.window->GetSize();
	CD3D11_VIEWPORT viewport(0.0f, 0.0f, static_cast<float>(windowSize.x), static_cast<float>(windowSize.y));
	m_d3dContext->RSSetViewports(1, &viewport);
}

void D3DRenderer::SwapBuffers(const GameContext& gameContext)
{
	float totalTime = gameContext.elapsedTime;

	m_World = Matrix::CreateRotationZ(totalTime / 2.f)
		* Matrix::CreateRotationY(totalTime)
		* Matrix::CreateRotationX(totalTime * 2.f);


	PostProcess();
	
	// The first argument instructs DXGI to block until VSync, putting the application
	// to sleep until the next VSync. This ensures we don't waste any cycles rendering
	// frames that will never be displayed to the screen.
	HRESULT hr = m_swapChain->Present(m_VSyncEnabled ? 1 : 0, 0);

	// If the device was reset we must completely reinitialize the renderer.
	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
	{
		OnDeviceLost(gameContext);
	}
	else
	{
		DX::ThrowIfFailed(hr);
	}
}

void D3DRenderer::UpdateTransformMatrix(const GameContext& gameContext, uint renderID, const glm::mat4x4& model)
{
	RenderObject* renderObject = GetRenderObject(renderID);

	glm::mat4 MVP = gameContext.camera->GetViewProjection() * model;
	//glUniformMatrix4fv(renderObject->MVP, 1, false, &MVP[0][0]);
}

int D3DRenderer::GetShaderUniformLocation(uint program, const std::string uniformName)
{
	//return glGetUniformLocation(program, uniformName.c_str());
	return 0;
}

void D3DRenderer::SetUniform1f(uint location, float val)
{
	//glUniform1f(location, val);
}

void D3DRenderer::DescribeShaderVariable(uint renderID, uint program, const std::string& variableName, int size, Renderer::Type renderType, bool normalized, int stride, void* pointer)
{
	RenderObject* renderObject = GetRenderObject(renderID);

	//glBindVertexArray(renderObject->VAO);
	//
	//GLuint location = glGetAttribLocation(program, variableName.c_str());
	//glEnableVertexAttribArray(location);
	//GLenum glRenderType = TypeToGLType(renderType);
	//glVertexAttribPointer(location, size, glRenderType, normalized, stride, pointer);
	//
	//glBindVertexArray(0);
}

void D3DRenderer::Destroy(uint renderID)
{
	for (auto iter = m_RenderObjects.begin(); iter != m_RenderObjects.end(); ++iter)
	{
		if ((*iter)->renderID == renderID)
		{
			RenderObject* object = *iter;
			m_RenderObjects.erase(iter);
			delete object;

			return;
		}
	}
}

glm::uint D3DRenderer::BufferTargetToD3DTarget(BufferTarget bufferTarget)
{
	glm::uint glTarget = 0;

	//if (bufferTarget == BufferTarget::ARRAY_BUFFER) glTarget = GL_ARRAY_BUFFER;
	//else if (bufferTarget == BufferTarget::ELEMENT_ARRAY_BUFFER) glTarget = GL_ELEMENT_ARRAY_BUFFER;
	//else Logger::LogError("Unhandled BufferTarget passed to GLRenderer: " + std::to_string((int)bufferTarget));

	return glTarget;
}

glm::uint D3DRenderer::TypeToD3DType(Type type)
{
	glm::uint glType = 0;

	//if (type == Type::BYTE) glType = GL_BYTE;
	//else if (type == Type::UNSIGNED_BYTE) glType = GL_UNSIGNED_BYTE;
	//else if (type == Type::SHORT) glType = GL_SHORT;
	//else if (type == Type::UNSIGNED_SHORT) glType = GL_UNSIGNED_SHORT;
	//else if (type == Type::INT) glType = GL_INT;
	//else if (type == Type::UNSIGNED_INT) glType = GL_UNSIGNED_INT;
	//else if (type == Type::FLOAT) glType = GL_FLOAT;
	//else if (type == Type::DOUBLE) glType = GL_DOUBLE;
	//else Logger::LogError("Unhandled Type passed to GLRenderer: " + std::to_string((int)type));

	return glType;
}

glm::uint D3DRenderer::UsageFlagToD3DUsageFlag(UsageFlag usage)
{
	glm::uint glUsage = 0;

	//if (usage == UsageFlag::STATIC_DRAW) glUsage = GL_STATIC_DRAW;
	//else if (usage == UsageFlag::DYNAMIC_DRAW) glUsage = GL_DYNAMIC_DRAW;
	//else Logger::LogError("Unhandled usage flag passed to GLRenderer: " + std::to_string((int)usage));

	return glUsage;
}

glm::uint D3DRenderer::ModeToD3DMode(Mode mode)
{
	glm::uint glMode = 0;

	//if (mode == Mode::POINTS) glMode = GL_POINTS;
	//else if (mode == Mode::LINES) glMode = GL_LINES;
	//else if (mode == Mode::LINE_LOOP) glMode = GL_LINE_LOOP;
	//else if (mode == Mode::LINE_STRIP) glMode = GL_LINE_STRIP;
	//else if (mode == Mode::TRIANGLES) glMode = GL_TRIANGLES;
	//else if (mode == Mode::TRIANGLE_STRIP) glMode = GL_TRIANGLE_STRIP;
	//else if (mode == Mode::TRIANGLE_FAN) glMode = GL_TRIANGLE_FAN;
	//else Logger::LogError("Unhandled Mode passed to GLRenderer: " + std::to_string((int)mode));

	return glMode;
}

D3DRenderer::RenderObject* D3DRenderer::GetRenderObject(int renderID)
{
	return m_RenderObjects[renderID];
}

// On Window size
/*
	m_outputWidth = std::max(width, 1);
	m_outputHeight = std::max(height, 1);

	CreateResources();
*/

void D3DRenderer::CreateDevice()
{
	UINT creationFlags = 0;

#ifdef _DEBUG
	creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	static const D3D_FEATURE_LEVEL featureLevels[] =
	{
		// TODO: Modify for supported Direct3D feature levels (see code below related to 11.1 fallback handling).
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
		D3D_FEATURE_LEVEL_9_3,
		D3D_FEATURE_LEVEL_9_2,
		D3D_FEATURE_LEVEL_9_1,
	};

	// Create the DX11 API device object, and get a corresponding context.
	HRESULT hr = D3D11CreateDevice(
		nullptr,                                // specify nullptr to use the default adapter
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		creationFlags,
		featureLevels,
		_countof(featureLevels),
		D3D11_SDK_VERSION,
		m_d3dDevice.ReleaseAndGetAddressOf(),   // returns the Direct3D device created
		&m_featureLevel,                        // returns feature level of device created
		m_d3dContext.ReleaseAndGetAddressOf()   // returns the device immediate context
	);

	if (hr == E_INVALIDARG)
	{
		// DirectX 11.0 platforms will not recognize D3D_FEATURE_LEVEL_11_1 so we need to retry without it.
		hr = D3D11CreateDevice(nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			creationFlags,
			&featureLevels[1],
			_countof(featureLevels) - 1,
			D3D11_SDK_VERSION,
			m_d3dDevice.ReleaseAndGetAddressOf(),
			&m_featureLevel,
			m_d3dContext.ReleaseAndGetAddressOf()
		);
	}

	DX::ThrowIfFailed(hr);

#ifndef NDEBUG
	ComPtr<ID3D11Debug> d3dDebug;
	if (SUCCEEDED(m_d3dDevice.As(&d3dDebug)))
	{
		ComPtr<ID3D11InfoQueue> d3dInfoQueue;
		if (SUCCEEDED(d3dDebug.As(&d3dInfoQueue)))
		{
#ifdef _DEBUG
			d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
			d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
#endif
			D3D11_MESSAGE_ID hide[] =
			{
				D3D11_MESSAGE_ID_SETPRIVATEDATA_CHANGINGPARAMS,
				// TODO: Add more message IDs here as needed.
			};
			D3D11_INFO_QUEUE_FILTER filter = {};
			filter.DenyList.NumIDs = _countof(hide);
			filter.DenyList.pIDList = hide;
			d3dInfoQueue->AddStorageFilterEntries(&filter);
		}
	}
#endif

	// DirectX 11.1 if present
	if (SUCCEEDED(m_d3dDevice.As(&m_d3dDevice1)))
	{
		(void)m_d3dContext.As(&m_d3dContext1);
	}

	m_States = std::make_unique<CommonStates>(m_d3dDevice.Get());

	DX::ThrowIfFailed(CreateWICTextureFromFile(m_d3dDevice.Get(), L"resources/textures/slice-of-lemon-640.jpg", nullptr,
		m_Background.ReleaseAndGetAddressOf()));

	m_SpriteBatch = std::make_unique<SpriteBatch>(m_d3dContext.Get());
	m_Shape = GeometricPrimitive::CreateTorus(m_d3dContext.Get());

	m_View = Matrix::CreateLookAt(Vector3(0.0f, 3.0f, -3.0f), Vector3::Zero, Vector3::UnitY);


	auto blob = DX::ReadData(L"BloomExtract.cso");
	DX::ThrowIfFailed(m_d3dDevice->CreatePixelShader(blob.data(), blob.size(),
		nullptr, m_BloomExtractPS.ReleaseAndGetAddressOf()));

	blob = DX::ReadData(L"BloomCombine.cso");
	DX::ThrowIfFailed(m_d3dDevice->CreatePixelShader(blob.data(), blob.size(),
		nullptr, m_BloomCombinePS.ReleaseAndGetAddressOf()));

	blob = DX::ReadData(L"GaussianBlur.cso");
	DX::ThrowIfFailed(m_d3dDevice->CreatePixelShader(blob.data(), blob.size(),
		nullptr, m_GaussianBlurPS.ReleaseAndGetAddressOf()));

	{
		CD3D11_BUFFER_DESC cbDesc(sizeof(VS_BLOOM_PARAMETERS),
			D3D11_BIND_CONSTANT_BUFFER);
		D3D11_SUBRESOURCE_DATA initData;
		initData.pSysMem = &g_BloomPresets[g_Bloom];
		initData.SysMemPitch = sizeof(VS_BLOOM_PARAMETERS);
		DX::ThrowIfFailed(m_d3dDevice->CreateBuffer(&cbDesc, &initData,
			m_BloomParams.ReleaseAndGetAddressOf()));
	}

	{
		CD3D11_BUFFER_DESC cbDesc(sizeof(VS_BLUR_PARAMETERS),
			D3D11_BIND_CONSTANT_BUFFER);
		DX::ThrowIfFailed(m_d3dDevice->CreateBuffer(&cbDesc, nullptr,
			m_BlurParamsWidth.ReleaseAndGetAddressOf()));
		DX::ThrowIfFailed(m_d3dDevice->CreateBuffer(&cbDesc, nullptr,
			m_BlurParamsHeight.ReleaseAndGetAddressOf()));
	}
}

// Allocate all memory resources that change on a window SizeChanged event.
void D3DRenderer::CreateResources(const GameContext& gameContext)
{
	// Clear the previous window size specific context.
	ID3D11RenderTargetView* nullViews[] = { nullptr };
	m_d3dContext->OMSetRenderTargets(_countof(nullViews), nullViews, nullptr);
	m_renderTargetView.Reset();
	m_depthStencilView.Reset();
	m_d3dContext->Flush();

	const vec2i windowSize = gameContext.window->GetSize();
	UINT backBufferWidth = static_cast<UINT>(windowSize.x);
	UINT backBufferHeight = static_cast<UINT>(windowSize.y);
	DXGI_FORMAT backBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
	DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	UINT backBufferCount = 2;

	// If the swap chain already exists, resize it, otherwise create one.
	if (m_swapChain)
	{
		HRESULT hr = m_swapChain->ResizeBuffers(backBufferCount, backBufferWidth, backBufferHeight, backBufferFormat, 0);

		if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
		{
			// If the device was removed for any reason, a new device and swap chain will need to be created.
			OnDeviceLost(gameContext);

			// Everything is set up now. Do not continue execution of this method. OnDeviceLost will reenter this method 
			// and correctly set up the new device.
			return;
		}
		else
		{
			DX::ThrowIfFailed(hr);
		}
	}
	else
	{
		// First, retrieve the underlying DXGI Device from the D3D Device.
		ComPtr<IDXGIDevice1> dxgiDevice;
		DX::ThrowIfFailed(m_d3dDevice.As(&dxgiDevice));

		// Identify the physical adapter (GPU or card) this device is running on.
		ComPtr<IDXGIAdapter> dxgiAdapter;
		DX::ThrowIfFailed(dxgiDevice->GetAdapter(dxgiAdapter.GetAddressOf()));

		// And obtain the factory object that created it.
		ComPtr<IDXGIFactory1> dxgiFactory;
		DX::ThrowIfFailed(dxgiAdapter->GetParent(IID_PPV_ARGS(dxgiFactory.GetAddressOf())));

		D3DWindowWrapper* d3dWindow = dynamic_cast<D3DWindowWrapper*>(gameContext.window);
		HWND windowHWND = d3dWindow->GetWindowHandle();

		ComPtr<IDXGIFactory2> dxgiFactory2;
		if (SUCCEEDED(dxgiFactory.As(&dxgiFactory2)))
		{
			// DirectX 11.1 or later

			// Create a descriptor for the swap chain.
			DXGI_SWAP_CHAIN_DESC1 swapChainDesc = { 0 };
			swapChainDesc.Width = backBufferWidth;
			swapChainDesc.Height = backBufferHeight;
			swapChainDesc.Format = backBufferFormat;
			swapChainDesc.SampleDesc.Count = 1;
			//swapChainDesc.SampleDesc.Count = (m_EnableMSAA ? 4 : 1);
			swapChainDesc.SampleDesc.Quality = 0;
			swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swapChainDesc.BufferCount = backBufferCount;

			DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = { 0 };
			fsSwapChainDesc.Windowed = TRUE;

			// Create a SwapChain from a Win32 window.
			DX::ThrowIfFailed(dxgiFactory2->CreateSwapChainForHwnd(
				m_d3dDevice.Get(),
				windowHWND,
				&swapChainDesc,
				&fsSwapChainDesc,
				nullptr,
				m_swapChain1.ReleaseAndGetAddressOf()
			));

			DX::ThrowIfFailed(m_swapChain1.As(&m_swapChain));
		}
		else
		{
			DXGI_SWAP_CHAIN_DESC swapChainDesc = { 0 };
			swapChainDesc.BufferCount = backBufferCount;
			swapChainDesc.BufferDesc.Width = backBufferWidth;
			swapChainDesc.BufferDesc.Height = backBufferHeight;
			swapChainDesc.BufferDesc.Format = backBufferFormat;
			swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swapChainDesc.OutputWindow = windowHWND;
			swapChainDesc.SampleDesc.Count = 1;
			swapChainDesc.SampleDesc.Quality = 0;
			swapChainDesc.Windowed = TRUE;

			DX::ThrowIfFailed(dxgiFactory->CreateSwapChain(m_d3dDevice.Get(), &swapChainDesc, m_swapChain.ReleaseAndGetAddressOf()));
		}

		// This template does not support exclusive fullscreen mode and prevents DXGI from responding to the ALT+ENTER shortcut.
		DX::ThrowIfFailed(dxgiFactory->MakeWindowAssociation(windowHWND, DXGI_MWA_NO_ALT_ENTER));
	}

	// Obtain the backbuffer for this window which will be the final 3D rendertarget.
	ComPtr<ID3D11Texture2D> backBuffer;
	DX::ThrowIfFailed(m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf())));

	// Create a view interface on the rendertarget to use on bind.
	DX::ThrowIfFailed(m_d3dDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, m_renderTargetView.ReleaseAndGetAddressOf()));

	// Obtain the backbuffer for this window which will be the final 3D rendertarget.
	DX::ThrowIfFailed(m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &m_backBuffer));

	// Create a view interface on the rendertarget to use on bind.
	DX::ThrowIfFailed(m_d3dDevice->CreateRenderTargetView(m_backBuffer.Get(), nullptr, m_renderTargetView.ReleaseAndGetAddressOf()));

	// Allocate a 2-D surface as the depth/stencil buffer and
	// create a DepthStencil view on this surface to use on bind.
	CD3D11_TEXTURE2D_DESC depthStencilDesc(depthBufferFormat, backBufferWidth, backBufferHeight, 1, 1, D3D11_BIND_DEPTH_STENCIL);
	//	D3D11_USAGE_DEFAULT, 0, (m_EnableMSAA ? 4 : 1), 0);

	ComPtr<ID3D11Texture2D> depthStencil;
	DX::ThrowIfFailed(m_d3dDevice->CreateTexture2D(&depthStencilDesc, nullptr, depthStencil.GetAddressOf()));

	//CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc(m_EnableMSAA ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D);
	CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc(D3D11_DSV_DIMENSION_TEXTURE2D);
	DX::ThrowIfFailed(m_d3dDevice->CreateDepthStencilView(depthStencil.Get(), &depthStencilViewDesc, m_depthStencilView.ReleaseAndGetAddressOf()));


	// TODO: Initialize windows-size dependent objects here.

	// Full-size render target for scene
	CD3D11_TEXTURE2D_DESC sceneDesc(backBufferFormat, backBufferWidth, backBufferHeight,
		1, 1, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
	DX::ThrowIfFailed(m_d3dDevice->CreateTexture2D(&sceneDesc, nullptr,
		m_sceneTex.GetAddressOf()));
	DX::ThrowIfFailed(m_d3dDevice->CreateRenderTargetView(m_sceneTex.Get(), nullptr,
		m_sceneRT.ReleaseAndGetAddressOf()));
	DX::ThrowIfFailed(m_d3dDevice->CreateShaderResourceView(m_sceneTex.Get(), nullptr,
		m_sceneSRV.ReleaseAndGetAddressOf()));

	// Half-size blurring render targets
	CD3D11_TEXTURE2D_DESC rtDesc(backBufferFormat, backBufferWidth / 2, backBufferHeight / 2,
		1, 1, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
	Microsoft::WRL::ComPtr<ID3D11Texture2D> rtTexture1;
	DX::ThrowIfFailed(m_d3dDevice->CreateTexture2D(&rtDesc, nullptr,
		rtTexture1.GetAddressOf()));
	DX::ThrowIfFailed(m_d3dDevice->CreateRenderTargetView(rtTexture1.Get(), nullptr,
		m_rt1RT.ReleaseAndGetAddressOf()));
	DX::ThrowIfFailed(m_d3dDevice->CreateShaderResourceView(rtTexture1.Get(), nullptr,
		m_rt1SRV.ReleaseAndGetAddressOf()));

	Microsoft::WRL::ComPtr<ID3D11Texture2D> rtTexture2;
	DX::ThrowIfFailed(m_d3dDevice->CreateTexture2D(&rtDesc, nullptr,
		rtTexture2.GetAddressOf()));
	DX::ThrowIfFailed(m_d3dDevice->CreateRenderTargetView(rtTexture2.Get(), nullptr,
		m_rt2RT.ReleaseAndGetAddressOf()));
	DX::ThrowIfFailed(m_d3dDevice->CreateShaderResourceView(rtTexture2.Get(), nullptr,
		m_rt2SRV.ReleaseAndGetAddressOf()));

	m_bloomRect.left = 0;
	m_bloomRect.top = 0;
	m_bloomRect.right = backBufferWidth / 2;
	m_bloomRect.bottom = backBufferHeight / 2;

	m_FullscreenRect.left = 0;
	m_FullscreenRect.top = 0;
	m_FullscreenRect.right = backBufferWidth;
	m_FullscreenRect.bottom = backBufferHeight;

	m_Projection = Matrix::CreatePerspectiveFieldOfView(XM_PIDIV4, float(backBufferWidth) / float(backBufferHeight), 0.01f, 100.f);

	VS_BLUR_PARAMETERS blurData;
	blurData.SetBlurEffectParameters(1.f / (backBufferWidth / 2), 0, g_BloomPresets[g_Bloom]);
	m_d3dContext->UpdateSubresource(m_BlurParamsWidth.Get(), 0, nullptr, &blurData, sizeof(VS_BLUR_PARAMETERS), 0);

	blurData.SetBlurEffectParameters(0, 1.f / (backBufferHeight / 2), g_BloomPresets[g_Bloom]);
	m_d3dContext->UpdateSubresource(m_BlurParamsHeight.Get(), 0, nullptr, &blurData, sizeof(VS_BLUR_PARAMETERS), 0);

}

void D3DRenderer::OnDeviceLost(const GameContext& gameContext)
{
	// TODO: Add Direct3D resource cleanup here.

	m_depthStencilView.Reset();
	m_renderTargetView.Reset();
	m_swapChain1.Reset();
	m_swapChain.Reset();
	m_d3dContext1.Reset();
	m_d3dContext.Reset();
	m_d3dDevice1.Reset();
	m_d3dDevice.Reset();

	m_States.reset();
	m_SpriteBatch.reset();
	m_Shape.reset();
	m_Background.Reset();

	m_BloomExtractPS.Reset();
	m_BloomCombinePS.Reset();
	m_GaussianBlurPS.Reset();

	m_BloomParams.Reset();
	m_BlurParamsWidth.Reset();
	m_BlurParamsHeight.Reset();

	m_sceneTex.Reset();
	m_sceneSRV.Reset();
	m_sceneRT.Reset();
	m_rt1SRV.Reset();
	m_rt1RT.Reset();
	m_rt2SRV.Reset();
	m_rt2RT.Reset();
	m_backBuffer.Reset();

	CreateDevice();
	CreateResources(gameContext);
}

void D3DRenderer::PostProcess()
{
	ID3D11ShaderResourceView* null[] = { nullptr, nullptr };

	if (g_Bloom == None)
	{
		// Pass-through test
		m_d3dContext->CopyResource(m_backBuffer.Get(), m_sceneTex.Get());
	}
	else
	{
		// scene -> RT1 (downsample)
		m_d3dContext->OMSetRenderTargets(1, m_rt1RT.GetAddressOf(), nullptr);
		m_SpriteBatch->Begin(SpriteSortMode_Immediate,
			nullptr, nullptr, nullptr, nullptr,
			[=]() {
			m_d3dContext->PSSetConstantBuffers(0, 1, m_BloomParams.GetAddressOf());
			m_d3dContext->PSSetShader(m_BloomExtractPS.Get(), nullptr, 0);
		});
		m_SpriteBatch->Draw(m_sceneSRV.Get(), m_bloomRect);
		m_SpriteBatch->End();

		// RT1 -> RT2 (blur horizontal)
		m_d3dContext->OMSetRenderTargets(1, m_rt2RT.GetAddressOf(), nullptr);
		m_SpriteBatch->Begin(SpriteSortMode_Immediate,
			nullptr, nullptr, nullptr, nullptr,
			[=]() {
			m_d3dContext->PSSetShader(m_GaussianBlurPS.Get(), nullptr, 0);
			m_d3dContext->PSSetConstantBuffers(0, 1,
				m_BlurParamsWidth.GetAddressOf());
		});
		m_SpriteBatch->Draw(m_rt1SRV.Get(), m_bloomRect);
		m_SpriteBatch->End();

		m_d3dContext->PSSetShaderResources(0, 2, null);

		// RT2 -> RT1 (blur vertical)
		m_d3dContext->OMSetRenderTargets(1, m_rt1RT.GetAddressOf(), nullptr);
		m_SpriteBatch->Begin(SpriteSortMode_Immediate,
			nullptr, nullptr, nullptr, nullptr,
			[=]() {
			m_d3dContext->PSSetShader(m_GaussianBlurPS.Get(), nullptr, 0);
			m_d3dContext->PSSetConstantBuffers(0, 1,
				m_BlurParamsHeight.GetAddressOf());
		});
		m_SpriteBatch->Draw(m_rt2SRV.Get(), m_bloomRect);
		m_SpriteBatch->End();

		// RT1 + scene
		m_d3dContext->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), nullptr);
		m_SpriteBatch->Begin(SpriteSortMode_Immediate,
			nullptr, nullptr, nullptr, nullptr,
			[=]() {
			m_d3dContext->PSSetShader(m_BloomCombinePS.Get(), nullptr, 0);
			m_d3dContext->PSSetShaderResources(1, 1, m_sceneSRV.GetAddressOf());
			m_d3dContext->PSSetConstantBuffers(0, 1, m_BloomParams.GetAddressOf());
		});
		m_SpriteBatch->Draw(m_rt1SRV.Get(), m_FullscreenRect);
		m_SpriteBatch->End();
	}

	m_d3dContext->PSSetShaderResources(0, 2, null);
}

#endif // COMPILE_D3D
