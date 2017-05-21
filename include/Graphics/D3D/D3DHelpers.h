#pragma once

#include <vector>

ID3DX11Effect* LoadEffectFromFile(const std::wstring& filePath, ID3D11Device* device);
