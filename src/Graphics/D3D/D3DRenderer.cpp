#include "stdafx.h"
#if COMPILE_D3D

#include "Graphics/D3D/D3DRenderer.h"
#include "Graphics/D3D/D3DHelpers.h"
#include "GameContext.h"
#include "Window/Window.h"
#include "Logger.h"
#include "FreeCamera.h"
#include "Helpers.h"
#include "Vertex.h"

#include <algorithm>

using namespace glm;
using namespace DirectX;
using namespace DirectX::SimpleMath;
using Microsoft::WRL::ComPtr;

D3DRenderer::D3DRenderer(GameContext& gameContext) :
	m_featureLevel(D3D_FEATURE_LEVEL_11_1)
{
	CreateDevice();
	CreateResources(gameContext);

	const std::wstring effectFilePath = L"resources/shaders/HLSL/simple.hlsl";
	m_pEffect = LoadEffectFromFile(effectFilePath, m_Device.Get());
	m_pTechnique = m_pEffect->GetTechniqueByIndex(0);

	if (!m_pWorldViewProjectionVariable)
	{
		m_pWorldViewProjectionVariable = m_pEffect->GetVariableBySemantic("WORLDVIEWPROJECTION")->AsMatrix();
		if (!m_pWorldViewProjectionVariable->IsValid())
		{
			Logger::LogError(L"Couldn't find variable with semantic WORLDVIEWPROJECTION in effect " + effectFilePath);
		}
	}

	CreateInputLayout();

	// For 60 FPS fixed timestep update logic:
	/*
	m_timer.SetFixedTimeStep(true);
	m_timer.SetTargetElapsedSeconds(1.0 / 60);
	*/


	if (m_PrintExtraD3DErrorInfo)
	{
		m_Device.Get()->QueryInterface(__uuidof(ID3D11Debug), (void**)&m_pDebugInterface);
	}
}

D3DRenderer::~D3DRenderer()
{
	SafeRelease(m_pTechnique);
	SafeRelease(m_pInputLayout);
	SafeRelease(m_pEffect);

	auto iter = m_RenderObjects.begin();
	while (iter != m_RenderObjects.end())
	{
		iter = Destroy(iter);
	}

	m_BackBuffer.ReleaseAndGetAddressOf();

	m_Device.ReleaseAndGetAddressOf();

	if (m_PrintExtraD3DErrorInfo)
	{
		if (m_pDebugInterface)
		{
			m_pDebugInterface->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
			m_pDebugInterface->Release();
		}
	}
}

uint D3DRenderer::Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices)
{
	const uint renderID = m_RenderObjects.size();

	RenderObject* renderObject = new RenderObject();
	renderObject->renderID = renderID;
	renderObject->vertices = vertices;

	m_RenderObjects.push_back(renderObject);

	InitializeVertexBuffer(renderID);
	UpdateVertexBuffer(renderID);

	return renderID;
}

uint D3DRenderer::Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices, std::vector<uint>* indices)
{
	const uint renderID = Initialize(gameContext, vertices);

	RenderObject* renderObject = GetRenderObject(renderID);

	renderObject->indices = indices;
	renderObject->indexed = true;

	InitializeIndexBuffer(renderID);
	UpdateIndexBuffer(renderID);

	return renderID;
}

void D3DRenderer::SetTopologyMode(glm::uint renderID, TopologyMode topology)
{
	RenderObject* renderObject = GetRenderObject(renderID);
	renderObject->topology = TopologyModeToD3DMode(topology);
}

void D3DRenderer::SetClearColor(float r, float g, float b)
{
	m_ClearColor = { r, g, b, 1.0f };
}

void D3DRenderer::PostInitialize()
{
}

void D3DRenderer::Draw(const GameContext& gameContext, uint renderID)
{
	UNREFERENCED_PARAMETER(gameContext);
}

void D3DRenderer::OnWindowSize(int width, int height)
{
	// TODO: Recreate swap chain!
	UNREFERENCED_PARAMETER(width);
	UNREFERENCED_PARAMETER(height);
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

	// Post-processed
	m_DeviceContext->ClearRenderTargetView(m_RenderTargetView.Get(), m_ClearColor);
	m_DeviceContext->ClearDepthStencilView(m_DepthStencilView.Get(), d3dClearFlags, 1.0f, 0);
	m_DeviceContext->OMSetRenderTargets(1, m_RenderTargetView.GetAddressOf(), m_DepthStencilView.Get());

	// Non-post-processed:
	//m_DeviceContext->ClearDepthStencilView(m_DepthStencilView.Get(), d3dClearFlags, 1.0f, 0);
	//m_DeviceContext->ClearRenderTargetView(m_RenderTargetView.Get(), m_ClearColor);
	//m_DeviceContext->OMSetRenderTargets(1, m_RenderTargetView.GetAddressOf(), m_DepthStencilView.Get());

	const vec2i windowSize = gameContext.window->GetSize();
	CD3D11_VIEWPORT viewport(0.0f, 0.0f, static_cast<float>(windowSize.x), static_cast<float>(windowSize.y));
	m_DeviceContext->RSSetViewports(1, &viewport);
}

void D3DRenderer::SwapBuffers(const GameContext& gameContext)
{
	float totalTime = gameContext.elapsedTime;

	Draw(gameContext);

	HRESULT hr = m_SwapChain->Present(m_VSyncEnabled ? 1 : 0, 0);

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

	renderObject->world = model;
}

int D3DRenderer::GetShaderUniformLocation(uint program, const std::string uniformName)
{
	// TODO: Implement
	UNREFERENCED_PARAMETER(program);
	UNREFERENCED_PARAMETER(uniformName);
	return 0;
}

void D3DRenderer::SetUniform1f(uint location, float val)
{
	// TODO: Implement
	UNREFERENCED_PARAMETER(location);
	UNREFERENCED_PARAMETER(val);
}

void D3DRenderer::DescribeShaderVariable(uint renderID, uint program, const std::string& variableName, int size, Renderer::Type renderType, bool normalized, int stride, void* pointer)
{
	RenderObject* renderObject = GetRenderObject(renderID);

	// TODO: Implement
	UNREFERENCED_PARAMETER(renderID);
	UNREFERENCED_PARAMETER(program);
	UNREFERENCED_PARAMETER(variableName);
	UNREFERENCED_PARAMETER(size);
	UNREFERENCED_PARAMETER(renderType);
	UNREFERENCED_PARAMETER(normalized);
	UNREFERENCED_PARAMETER(stride);
	UNREFERENCED_PARAMETER(pointer);

}

void D3DRenderer::Destroy(uint renderID)
{
	for (auto iter = m_RenderObjects.begin(); iter != m_RenderObjects.end(); ++iter)
	{
		if ((*iter)->renderID == renderID)
		{
			iter = Destroy(iter);
			return;
		}
	}
}

D3DRenderer::RenderObjectIter D3DRenderer::Destroy(RenderObjectIter iter)
{
	RenderObject* pRenderObject = *iter;
	auto newIter = m_RenderObjects.erase(iter);
	SafeRelease(pRenderObject->vertexBuffer);
	SafeRelease(pRenderObject->indexBuffer);
	SafeDelete(pRenderObject);

	return newIter;
}

glm::uint D3DRenderer::BufferTargetToD3DTarget(BufferTarget bufferTarget)
{
	glm::uint glTarget = 0;

	// TODO: Implement
	UNREFERENCED_PARAMETER(bufferTarget);

	return glTarget;
}

glm::uint D3DRenderer::TypeToD3DType(Type type)
{
	glm::uint glType = 0;

	// TODO: Implement
	UNREFERENCED_PARAMETER(type);

	return glType;
}

glm::uint D3DRenderer::UsageFlagToD3DUsageFlag(UsageFlag usage)
{
	glm::uint glUsage = 0;

	// TODO: Implement
	UNREFERENCED_PARAMETER(usage);

	return glUsage;
}

D3D_PRIMITIVE_TOPOLOGY D3DRenderer::TopologyModeToD3DMode(TopologyMode topology)
{
	switch (topology)
	{
	case TopologyMode::POINT_LIST: return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
	case TopologyMode::LINE_LIST: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
	//case TopologyMode::LINE_LOOP: return D3D_PRIMITIVE_TOPOLOGY_LINELOOP; // Unsupported
	case TopologyMode::LINE_STRIP: return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
	case TopologyMode::TRIANGLE_LIST: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	case TopologyMode::TRIANGLE_STRIP: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
	//case TopologyMode::TRIANGLE_FAN: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLEFAN; // Unsupported
	default: return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
	}
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
		nullptr,                                  // Specify nullptr to use the default adapter
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		creationFlags,
		featureLevels,
		_countof(featureLevels),
		D3D11_SDK_VERSION,
		m_Device.ReleaseAndGetAddressOf(),        // Returns the Direct3D device created
		&m_featureLevel,                          // Returns feature level of device created
		m_DeviceContext.ReleaseAndGetAddressOf()  // Returns the device immediate context
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
			m_Device.ReleaseAndGetAddressOf(),
			&m_featureLevel,
			m_DeviceContext.ReleaseAndGetAddressOf()
		);
	}

	DX::ThrowIfFailed(hr);

	// DirectX 11.1 if present
	if (SUCCEEDED(m_Device.As(&m_Device1)))
	{
		(void)m_DeviceContext.As(&m_DeviceContext1);
	}

	if (m_pTechnique)
	{
		SafeRelease(m_pTechnique);
	}

	if (m_pInputLayout)
	{
		SafeRelease(m_pInputLayout);
	}
}

void D3DRenderer::InitializeVertexBuffer(glm::uint renderID)
{
	RenderObject* renderObject = GetRenderObject(renderID);

	if (renderObject->vertexBuffer)
	{
		SafeRelease(renderObject->vertexBuffer);
	}

	D3D11_BUFFER_DESC vertexBuffDesc;
	vertexBuffDesc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_VERTEX_BUFFER;
	vertexBuffDesc.ByteWidth = sizeof(VertexPosCol) * renderObject->vertices->size();
	vertexBuffDesc.CPUAccessFlags = D3D11_CPU_ACCESS_FLAG::D3D11_CPU_ACCESS_WRITE;
	vertexBuffDesc.Usage = D3D11_USAGE::D3D11_USAGE_DYNAMIC;
	vertexBuffDesc.MiscFlags = 0;
	m_Device->CreateBuffer(&vertexBuffDesc, NULL, &renderObject->vertexBuffer);
}

void D3DRenderer::UpdateVertexBuffer(glm::uint renderID)
{
	RenderObject* renderObject = GetRenderObject(renderID);

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	m_DeviceContext->Map(renderObject->vertexBuffer, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mappedResource);
	memcpy(mappedResource.pData, renderObject->vertices->data(), sizeof(VertexPosCol) * renderObject->vertices->size());
	m_DeviceContext->Unmap(renderObject->vertexBuffer, 0);
}

void D3DRenderer::InitializeIndexBuffer(glm::uint renderID)
{
	RenderObject* renderObject = GetRenderObject(renderID);

	if (renderObject->indexBuffer)
	{
		SafeRelease(renderObject->indexBuffer);
	}

	D3D11_BUFFER_DESC indexBuffDesc;
	indexBuffDesc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_INDEX_BUFFER;
	indexBuffDesc.ByteWidth = sizeof(uint) * renderObject->indices->size();
	indexBuffDesc.CPUAccessFlags = D3D11_CPU_ACCESS_FLAG::D3D11_CPU_ACCESS_WRITE;
	indexBuffDesc.Usage = D3D11_USAGE::D3D11_USAGE_DYNAMIC;
	indexBuffDesc.MiscFlags = 0;
	m_Device->CreateBuffer(&indexBuffDesc, NULL, &renderObject->indexBuffer);
}

void D3DRenderer::UpdateIndexBuffer(glm::uint renderID)
{
	RenderObject* renderObject = GetRenderObject(renderID);

	if (!renderObject->indexed)
	{
		return;
	}

	if (renderObject->indices->empty())
	{
		Logger::LogError("Render object's index buffer is empty, but it is flagged as indexed");
		return;
	}

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	m_DeviceContext->Map(renderObject->indexBuffer, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mappedResource);
	memcpy(mappedResource.pData, renderObject->indices->data(), sizeof(uint) * renderObject->indices->size());
	m_DeviceContext->Unmap(renderObject->indexBuffer, 0);
}

// Allocate all memory resources that change on a window SizeChanged event.
void D3DRenderer::CreateResources(const GameContext& gameContext)
{
	// Clear the previous window size specific context.
	ID3D11RenderTargetView* nullViews[] = { nullptr };
	m_DeviceContext->OMSetRenderTargets(_countof(nullViews), nullViews, nullptr);
	m_RenderTargetView.Reset();
	m_DepthStencilView.Reset();
	m_DeviceContext->Flush();

	const vec2i windowSize = gameContext.window->GetSize();
	UINT backBufferWidth = static_cast<UINT>(windowSize.x);
	UINT backBufferHeight = static_cast<UINT>(windowSize.y);
	DXGI_FORMAT backBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
	DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	UINT backBufferCount = 2;

	// If the swap chain already exists, resize it, otherwise create one.
	if (m_SwapChain)
	{
		HRESULT hr = m_SwapChain->ResizeBuffers(backBufferCount, backBufferWidth, backBufferHeight, backBufferFormat, 0);

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
		DX::ThrowIfFailed(m_Device.As(&dxgiDevice));

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
				m_Device.Get(),
				windowHWND,
				&swapChainDesc,
				&fsSwapChainDesc,
				nullptr,
				m_SwapChain1.ReleaseAndGetAddressOf()
			));

			DX::ThrowIfFailed(m_SwapChain1.As(&m_SwapChain));
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

			DX::ThrowIfFailed(dxgiFactory->CreateSwapChain(m_Device.Get(), &swapChainDesc, m_SwapChain.ReleaseAndGetAddressOf()));
		}

		// This template does not support exclusive fullscreen mode and prevents DXGI from responding to the ALT+ENTER shortcut.
		DX::ThrowIfFailed(dxgiFactory->MakeWindowAssociation(windowHWND, DXGI_MWA_NO_ALT_ENTER));
	}

	// Obtain the backbuffer for this window which will be the final 3D rendertarget.
	ComPtr<ID3D11Texture2D> backBuffer;
	DX::ThrowIfFailed(m_SwapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf())));

	// Create a view interface on the rendertarget to use on bind.
	DX::ThrowIfFailed(m_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_RenderTargetView.ReleaseAndGetAddressOf()));

	// Obtain the backbuffer for this window which will be the final 3D rendertarget.
	DX::ThrowIfFailed(m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &m_BackBuffer));

	// Create a view interface on the rendertarget to use on bind.
	DX::ThrowIfFailed(m_Device->CreateRenderTargetView(m_BackBuffer.Get(), nullptr, m_RenderTargetView.ReleaseAndGetAddressOf()));

	// Allocate a 2-D surface as the depth/stencil buffer and
	// create a DepthStencil view on this surface to use on bind.
	CD3D11_TEXTURE2D_DESC depthStencilDesc(depthBufferFormat, backBufferWidth, backBufferHeight, 1, 1, D3D11_BIND_DEPTH_STENCIL);
	//	D3D11_USAGE_DEFAULT, 0, (m_EnableMSAA ? 4 : 1), 0);

	ComPtr<ID3D11Texture2D> depthStencil;
	DX::ThrowIfFailed(m_Device->CreateTexture2D(&depthStencilDesc, nullptr, depthStencil.GetAddressOf()));

	//CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc(m_EnableMSAA ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D);
	CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc(D3D11_DSV_DIMENSION_TEXTURE2D);
	DX::ThrowIfFailed(m_Device->CreateDepthStencilView(depthStencil.Get(), &depthStencilViewDesc, m_DepthStencilView.ReleaseAndGetAddressOf()));


	// Initialize windows-size dependent objects here:

	// Full-size render target for scene
	//CD3D11_TEXTURE2D_DESC sceneDesc(backBufferFormat, backBufferWidth, backBufferHeight,
	//	1, 1, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
	//DX::ThrowIfFailed(m_Device->CreateTexture2D(&sceneDesc, nullptr, m_SceneTex.GetAddressOf()));
	//DX::ThrowIfFailed(m_Device->CreateRenderTargetView(m_SceneTex.Get(), nullptr, m_SceneRT.ReleaseAndGetAddressOf()));
	//DX::ThrowIfFailed(m_Device->CreateShaderResourceView(m_SceneTex.Get(), nullptr, m_SceneSRV.ReleaseAndGetAddressOf()));
}

void D3DRenderer::Draw(const GameContext& gameContext)
{
	XMMATRIX proj = XMLoadFloat4x4(&XMFLOAT4X4(&gameContext.camera->GetProj()[0][0]));
	XMMATRIX view = XMLoadFloat4x4(&XMFLOAT4X4(&gameContext.camera->GetView()[0][0]));
	XMMATRIX viewProjection = view * proj;

	m_DeviceContext->IASetInputLayout(m_pInputLayout);

	for (size_t i = 0; i < m_RenderObjects.size(); i++)
	{
		RenderObject* renderObject = GetRenderObject(i);

		m_DeviceContext->IASetPrimitiveTopology(renderObject->topology);

		XMFLOAT4X4 worldF = XMFLOAT4X4(&renderObject->world[0][0]);
		auto world = XMLoadFloat4x4(&worldF);
		XMMATRIX wvp = world * viewProjection;
		m_pWorldViewProjectionVariable->SetMatrix(reinterpret_cast<float*>(&wvp));

		UINT offset = 0;
		UINT stride = sizeof(VertexPosCol);
		m_DeviceContext->IASetVertexBuffers(0, 1, &renderObject->vertexBuffer, &stride, &offset);
		m_DeviceContext->IASetIndexBuffer(renderObject->indexBuffer, DXGI_FORMAT_R32_UINT, 0);

		D3DX11_TECHNIQUE_DESC techDesc;
		m_pTechnique->GetDesc(&techDesc);
		for (UINT p = 0; p < techDesc.Passes; ++p)
		{
			m_pTechnique->GetPassByIndex(p)->Apply(0, m_DeviceContext.Get());
			if (renderObject->indexed)
			{
				m_DeviceContext->DrawIndexed(renderObject->indices->size(), 0, 0);
			}
			else
			{
				m_DeviceContext->Draw(renderObject->vertices->size(), 0);
			}
		}
	}
}

void D3DRenderer::CreateInputLayout()
{
	D3D11_INPUT_ELEMENT_DESC inputLayoutDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
	UINT numElements = sizeof(inputLayoutDesc) / sizeof(inputLayoutDesc[0]);

	D3DX11_PASS_DESC PassDesc;
	m_pTechnique->GetPassByIndex(0)->GetDesc(&PassDesc);
	HRESULT hr = m_Device->CreateInputLayout(inputLayoutDesc, numElements, PassDesc.pIAInputSignature, PassDesc.IAInputSignatureSize, &m_pInputLayout);
	if (FAILED(hr))
	{
		Logger::LogError(L"D3DRenderer couldn't create input layout");
	}
}

void D3DRenderer::OnDeviceLost(const GameContext& gameContext)
{
	m_DepthStencilView.Reset();
	m_RenderTargetView.Reset();
	m_SwapChain1.Reset();
	m_SwapChain.Reset();
	m_DeviceContext1.Reset();
	m_DeviceContext.Reset();
	m_Device1.Reset();
	m_Device.Reset();

	m_BackBuffer.Reset();

	CreateDevice();
	CreateResources(gameContext);
}

void D3DRenderer::UpdateUniformBuffers(const GameContext& gameContext)
{
	// TODO: Implement
	UNREFERENCED_PARAMETER(gameContext);
}

#endif // COMPILE_D3D
