// DxCommon.h
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <wincodec.h>

#include <string>
#include <vector>
#include <stdexcept>
#include <cstdint>
#include <memory>
#include <unordered_map>

#pragma warning(push)
#pragma warning(disable : 6387)
#include "d3dx12.h"
#pragma warning(pop)

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
        throw std::runtime_error("HRESULT failed");
}

