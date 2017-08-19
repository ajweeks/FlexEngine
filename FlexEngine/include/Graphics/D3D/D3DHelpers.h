#pragma once
#if COMPILE_D3D

#include <d3d11.h>

ID3DX11Effect* LoadEffectFromFile(const std::wstring& filePath, ID3D11Device* device);

#endif