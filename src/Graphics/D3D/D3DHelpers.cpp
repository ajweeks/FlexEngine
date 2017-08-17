#include "stdafx.h"
#if COMPILE_D3D

#include <fstream>
#include <sstream>

#include "Graphics/D3D/D3DHelpers.h"
#include "Logger.h"

ID3DX11Effect* LoadEffectFromFile(const std::wstring& filePath, ID3D11Device* device)
{
	HRESULT hr;
	ID3DBlob* pErrorBlob = nullptr;
	ID3DX11Effect* pEffect;

	DWORD shaderFlags = 0;
	#if defined( DEBUG ) || defined( _DEBUG )
		shaderFlags |= D3DCOMPILE_DEBUG;
		shaderFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
	#endif

	hr = D3DX11CompileEffectFromFile(filePath.c_str(),
		nullptr,
		nullptr,
		shaderFlags,
		0,
		device,
		&pEffect,
		&pErrorBlob);

	if (FAILED(hr))
	{
		if (pErrorBlob != nullptr)
		{
			char *errors = (char*)pErrorBlob->GetBufferPointer();

			std::wstringstream ss;
			for (unsigned int i = 0; i < pErrorBlob->GetBufferSize(); ++i)
			{
				ss << errors[i];
			}

			SafeRelease(pErrorBlob);

			Logger::LogError(ss.str());
		}
		else
		{
			Logger::LogError(L"Failed to Create effect from file: " + filePath);
		}

		return nullptr;
	}

	SafeRelease(pErrorBlob);

	return pEffect;
}

#endif