#pragma once

#define NOMINMAX

#define COMPILE_OPEN_GL 0
#define COMPILE_VULKAN 0
#define COMPILE_D3D 1

// Ensure more than one API isn't defined
#if COMPILE_VULKAN
#if COMPILE_OPEN_GL || COMPILE_D3D
assert(false);
#endif
#endif

#if COMPILE_OPEN_GL
#if COMPILE_VULKAN || COMPILE_D3D
assert(false);
#endif
#endif

#if COMPILE_D3D
#if COMPILE_OPEN_GL || COMPILE_VULKAN
assert(false);
#endif
#endif

#if COMPILE_VULKAN
	#include <glad/glad.h>
	#define GLFW_INCLUDE_VULKAN
	#include <GLFW/glfw3.h>

	#include "Graphics/VulkanRenderer.h"
	#include "Window/VulkanWindowWrapper.h"
#endif // COMPILE_VULKAN

#if COMPILE_OPEN_GL
	#include <glad/glad.h>
	#define GLFW_INCLUDE_NONE
	#include <GLFW/glfw3.h>

	#include "Graphics/GLRenderer.h"
	#include "Window/GLWindowWrapper.h"
#endif // COMPILE_OPEN_GL

#if COMPILE_D3D
	#include <d3d11.h>
	#include <DirectXMath.h>
	#include <DirectXColors.h>
	#include <wrl/client.h>

	#pragma comment(lib, "d3d11.lib")

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

	#include "Graphics/D3DRenderer.h"
	#include "Window/D3DWindowWrapper.h"

	#include "ReadData.h"

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
#endif // COMPILE_D3D

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#include <glm/glm.hpp>
