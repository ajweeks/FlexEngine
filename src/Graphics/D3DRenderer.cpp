#include "stdafx.h"
#if COMPILE_D3D

#include "Graphics/D3DRenderer.h"
#include "GameContext.h"
#include "Window/Window.h"
#include "Logger.h"
#include "FreeCamera.h"

#include <algorithm>

using namespace glm;

D3DRenderer::D3DRenderer(GameContext& gameContext) :
	Renderer(gameContext),
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


	static float a = 0.0f;
	a += 0.16f;
	m_World = Matrix::CreateRotationY(cosf(a * 0.5f) * 0.5f);

	//m_d3dContext->OMSetBlendState(m_States->Opaque(), nullptr, 0xFFFFFFFF);
	//m_d3dContext->OMSetDepthStencilState(m_States->DepthNone(), 0);
	//if (m_EnableMSAA)
	//{
	//	m_d3dContext->RSSetState(m_RasterizerState.Get());
	//}
	//else
	//{
	//	m_d3dContext->RSSetState(m_States->CullNone());
	//}

	//m_Effect->Apply(m_d3dContext.Get());

	//m_d3dContext->IASetInputLayout(m_InputLayout.Get());

	//m_VertexBatch->Begin();

	// Triangle
	//VertexPositionColor v1(Vector3(400.0f, 150.0f, 0.0f), Colors::Red);
	//VertexPositionColor v2(Vector3(600.0f, 450.0f, 0.0f), Colors::Orange);
	//VertexPositionColor v3(Vector3(200.0f, 450.0f, 0.0f), Colors::Green);
	//
	//m_VertexBatch->DrawTriangle(v1, v2, v3);

	// Grid
	//Vector3 xAxis(2.0f, 0.0f, 0.0f);
	//Vector3 yAxis(0.0f, 0.0f, 2.0f);
	//Vector3 zAxis(0.0f, 2.0f, 0.0f);
	//Vector3 origin = Vector3::Zero;
	//
	//size_t divisions = 20;
	//
	//for (size_t i = 0; i <= divisions; i++)
	//{
	//	float fPercent = (float(i) / float(divisions));
	//	fPercent = (fPercent * 2.0f) - 1.0f;
	//	Vector3 scale = xAxis * fPercent + origin;
	//
	//	VertexPositionColor gridV1(scale - yAxis, Colors::White);
	//	VertexPositionColor gridV2(scale + yAxis, Colors::White);
	//	m_VertexBatch->DrawLine(gridV1, gridV2);
	//}
	//
	//for (size_t i = 0; i <= divisions; i++)
	//{
	//	float fPercent = (float(i) / float(divisions));
	//	fPercent = (fPercent * 2.0f) - 1.0f;
	//	Vector3 scale = yAxis * fPercent + origin;
	//
	//	VertexPositionColor gridV1(scale - xAxis, Colors::White);
	//	VertexPositionColor gridV2(scale + xAxis, Colors::White);
	//	m_VertexBatch->DrawLine(gridV1, gridV2);
	//}

	//m_VertexBatch->End();

	//m_Effect->SetWorld(m_World);

	//m_Cube->Draw(m_Effect.get(), m_InputLayout.Get());
	m_Cube->Draw(m_World, m_View, m_Proj, Colors::White, nullptr, m_Wireframe);

	//m_Model->Draw(m_d3dContext.Get(), *m_States, m_World, m_View, m_Proj);

	// Text
	//m_FontSpriteBatch->Begin();
	//const char* ascii = "Hello World!";
	//std::wstring_convert<std::codecvt_utf8<wchar_t>> asciiConverter;
	//std::wstring outputStr = asciiConverter.from_bytes(ascii);
	//Vector2 fontOrigin = m_Font->MeasureString(outputStr.c_str()) / 2.0f;
	//
	////static float a = 0.0f;
	////a += m_timer.GetElapsedSeconds();
	//m_Font->DrawString(m_FontSpriteBatch.get(), outputStr.c_str(), m_FontPos + Vector2(-1.0f, -1.0f), Colors::Black, 0.0f, fontOrigin);
	//m_Font->DrawString(m_FontSpriteBatch.get(), outputStr.c_str(), m_FontPos, Colors::White, 0.0f, fontOrigin);
	//
	//m_FontSpriteBatch->End();
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
}

void D3DRenderer::SetVSyncEnabled(bool enableVSync)
{
	m_VSyncEnabled = enableVSync;
	//glfwSwapInterval(enableVSync ? 1 : 0);
}

void D3DRenderer::Clear(int flags, const GameContext& gameContext)
{
	UINT d3dClearFlags;
	if (flags & (int)ClearFlag::DEPTH) d3dClearFlags |= D3D11_CLEAR_DEPTH;
	if (flags & (int)ClearFlag::STENCIL) d3dClearFlags |= D3D11_CLEAR_STENCIL;

	m_d3dContext->ClearRenderTargetView(m_renderTargetView.Get(), m_ClearColor);
	m_d3dContext->ClearDepthStencilView(m_depthStencilView.Get(), d3dClearFlags, 1.0f, 0);

	m_d3dContext->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());

	const vec2i windowSize = gameContext.window->GetSize();
	CD3D11_VIEWPORT viewport(0.0f, 0.0f, static_cast<float>(windowSize.x), static_cast<float>(windowSize.y));
	m_d3dContext->RSSetViewports(1, &viewport);
}

void D3DRenderer::SwapBuffers(const GameContext& gameContext)
{
	// The first argument instructs DXGI to block until VSync, putting the application
	// to sleep until the next VSync. This ensures we don't waste any cycles rendering
	// frames that will never be displayed to the screen.
	HRESULT hr = m_swapChain->Present(1, 0);

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
		(void)m_d3dContext.As(&m_d3dContext1);

	// TODO: Initialize device dependent objects here (independent of window size).
	/*
	//m_SpriteBatch = std::make_unique<SpriteBatch>(m_d3dContext.Get());
	//
	//ComPtr<ID3D11Resource> resource;
	//DX::ThrowIfFailed(CreateWICTextureFromFile(m_d3dDevice.Get(), L"resources/textures/workworkworkworkwork.jpg", 
	//	resource.GetAddressOf(),
	//	m_Texture.ReleaseAndGetAddressOf()));
	//
	//ComPtr<ID3D11Texture2D> work;
	//DX::ThrowIfFailed(resource.As(&work));
	//
	//CD3D11_TEXTURE2D_DESC workDesc;
	//work->GetDesc(&workDesc);


	//m_Origin.x = float(workDesc.Width * 2.0f);
	//m_Origin.y = float(workDesc.Height * 2.0f);
	//
	//m_TileRect.left = workDesc.Width * 2;
	//m_TileRect.right = workDesc.Width * 6;
	//m_TileRect.top = workDesc.Width * 2;
	//m_TileRect.bottom = workDesc.Width * 6;

	//// Font
	//m_Font = std::make_unique<SpriteFont>(m_d3dDevice.Get(), L"resources/fonts/myfile.spritefont");
	//m_FontSpriteBatch = std::make_unique<SpriteBatch>(m_d3dContext.Get());
	//
	//// Triangle
	//m_States = std::make_unique<CommonStates>(m_d3dDevice.Get());

	//m_Effect = std::make_unique<BasicEffect>(m_d3dDevice.Get());
	//m_Effect->SetTextureEnabled(true);
	//m_Effect->SetPerPixelLighting(true);
	//m_Effect->SetLightingEnabled(true);
	//m_Effect->SetLightEnabled(0, true);
	//m_Effect->SetLightDiffuseColor(0, Colors::White);
	//m_Effect->SetLightDirection(0, -Vector3::UnitZ);
	//m_Effect->SetVertexColorEnabled(true);

	//m_VertexBatch = std::make_unique<PrimitiveBatch<VertexPositionColor>>(m_d3dContext.Get());
	*/
	// Grid

	// Cube
	m_Cube = GeometricPrimitive::CreateCube(m_d3dContext.Get());
	//m_Cube->CreateInputLayout(m_Effect.get(), m_InputLayout.ReleaseAndGetAddressOf());

	m_World = Matrix::Identity;

	/*
	//m_States = std::make_unique<CommonStates>(m_d3dDevice.Get());
	//m_fxFactory = std::make_unique<DGSLEffectFactory>(m_d3dDevice.Get());
	//m_Model = Model::CreateFromCMO(m_d3dDevice.Get(), L"resources/models/cup/cup.cmo", *m_fxFactory);

	//m_Model->UpdateEffects([&](IEffect* effect)
	//{
	//	auto lights = dynamic_cast<IEffectLights*>(effect);
	//	if (lights)
	//	{
	//		lights->SetLightingEnabled(true);
	//		lights->SetPerPixelLighting(true);
	//		lights->SetLightEnabled(0, true);
	//		lights->SetLightDiffuseColor(0, Colors::Pink);
	//		lights->SetLightEnabled(1, false);
	//		lights->SetLightEnabled(2, false);
	//	}
	//
	//	auto fog = dynamic_cast<IEffectFog*>(effect);
	//	if (fog)
	//	{
	//		fog->SetFogEnabled(true);
	//		fog->SetFogColor(m_ClearColor);
	//		fog->SetFogStart(3.0f);
	//		fog->SetFogEnd(4.0f);
	//	}
	//});

	//DX::ThrowIfFailed(CreateWICTextureFromFile(m_d3dDevice.Get(), L"resources/textures/workworkworkworkwork.jpg", nullptr,
	//	m_Texture.ReleaseAndGetAddressOf()));
	//m_Effect->SetTexture(m_Texture.Get());

	//void const* shaderByteCode;
	//size_t byteCodeLength;

	//m_Effect->GetVertexShaderBytecode(&shaderByteCode, &byteCodeLength);
	//DX::ThrowIfFailed(m_d3dDevice->CreateInputLayout(VertexPositionColor::InputElements, VertexPositionColor::InputElementCount,
	//	shaderByteCode, byteCodeLength, m_InputLayout.ReleaseAndGetAddressOf()));

	//// Rasterizer state
	//CD3D11_RASTERIZER_DESC rastDesc(D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE,
	//	D3D11_DEFAULT_DEPTH_BIAS, D3D11_DEFAULT_DEPTH_BIAS_CLAMP,
	//	D3D11_DEFAULT_SLOPE_SCALED_DEPTH_BIAS, TRUE, FALSE, m_EnableMSAA ? TRUE : FALSE, FALSE);
	//
	//DX::ThrowIfFailed(m_d3dDevice->CreateRasterizerState(&rastDesc, m_RasterizerState.ReleaseAndGetAddressOf()));

	*/
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
	//m_FontPos.x = 160.0f;
	//m_FontPos.y = 30.0f;

	m_View = Matrix::CreateLookAt(Vector3(2.0f, 2.0f, 2.0f),
		Vector3::Zero, Vector3::UnitY);

	const float FOV = XM_PI / 4.0f;
	const float nearPlane = 0.1f;
	const float farPlane = 10.0f;
	const float aspectRatio = float(backBufferWidth) / float(backBufferHeight);
	m_Proj = Matrix::CreatePerspectiveFieldOfView(FOV, aspectRatio, nearPlane, farPlane);

	//m_Effect->SetView(m_View);
	//m_Effect->SetProjection(m_Proj);
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

	//m_SpriteBatch.reset();
	//m_Font.reset();
	//m_FontSpriteBatch.reset();

	//m_States.reset();
	//m_fxFactory.reset();
	//m_Model.reset();

	//m_VertexBatch.reset();
	//m_RasterizerState.Reset();

	//m_Effect.reset();
	//m_InputLayout.Reset();

	m_Cube.reset();
	//m_Texture.Reset();

	CreateDevice();
	CreateResources(gameContext);
}

#endif // COMPILE_D3D
