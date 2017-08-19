#pragma once

#define NOMINMAX

#define COMPILE_OPEN_GL 1
#define COMPILE_VULKAN 1
#define COMPILE_D3D 0

#pragma warning(disable : 4201)
#pragma warning(disable : 4820)
#pragma warning(disable : 4868)
#pragma warning(disable : 4710)

#if COMPILE_VULKAN
#pragma warning(push, 0) // Don't generate warnings for 3rd party code    
	#include <glad/glad.h>
	#include <vulkan/vulkan.h>
	#include <GLFW/glfw3.h>
#pragma warning(pop)

	#include "Graphics/Vulkan/VulkanRenderer.h"
	#include "Window/Vulkan/VulkanWindowWrapper.h"
#endif // COMPILE_VULKAN

#if COMPILE_OPEN_GL
#pragma warning(push, 0) // Don't generate warnings for 3rd party code    
#define GLFW_EXPOSE_NATIVE_WIN32
	#include <glad/glad.h>
	#include <GLFW/glfw3.h>
	#include <GLFW/glfw3native.h>
#pragma warning(pop)

	#include "Graphics/GL/GLRenderer.h"
	#include "Window/GL/GLWindowWrapper.h"
#endif // COMPILE_OPEN_GL

#if COMPILE_D3D
#pragma warning(push, 0) // Don't generate warnings for 3rd party code    
#include <d3d11.h>
	#pragma comment(lib, "d3d11.lib")
	#include <d3dcompiler.h>
	#pragma comment(lib, "d3dcompiler.lib")

	#include "d3dx11effect.h"
	#if defined(DEBUG) || defined(_DEBUG)
		#pragma comment(lib, "DxEffects11_vc14_Debug.lib")
	#else 
		#pragma comment(lib, "DxEffects11_vc14_Release.lib")
	#endif

	// DirectXTK
	#include "CommonStates.h"
	#include "DDSTextureLoader.h"
	#include "DirectXHelpers.h"
	#include "Effects.h"
	#include "GamePad.h"
	#include "GeometricPrimitive.h"
	#include "GraphicsMemory.h"
	#include "Keyboard.h"
	#include "Model.h"
	#include "Mouse.h"
	#include "PrimitiveBatch.h"
	#include "ScreenGrab.h"
	#include "SimpleMath.h"
	#include "SpriteBatch.h"
	#include "SpriteFont.h"
	#include "VertexTypes.h"
	#include "WICTextureLoader.h"
#pragma warning(pop)

	#include "Graphics/D3D/D3DRenderer.h"
	#include "Window/D3D/D3DWindowWrapper.h"

	namespace DX
	{
		inline void ThrowIfFailed(HRESULT hr)
		{
			if (FAILED(hr))
			{
				// Set a breakpoint on this line to catch DirectX API errors
				throw std::exception();
			}
		}
	}

	template<class Interface>
	inline void SafeRelease(Interface &pInterfaceToRelease)
	{
		if (pInterfaceToRelease != 0)
		{
			pInterfaceToRelease->Release();
			pInterfaceToRelease = 0;
		}
	}
#endif // COMPILE_D3D


template<class T>
inline void SafeDelete(T &pObjectToDelete)
{
	if (pObjectToDelete != 0)
	{
		delete(pObjectToDelete);
		pObjectToDelete = 0;
	}
}

#pragma warning(push, 0) // Don't generate warnings for 3rd party code    
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#include <glm/glm.hpp>
#pragma warning(pop)

static const std::string RESOURCE_LOCATION = "FlexEngine/resources/";
