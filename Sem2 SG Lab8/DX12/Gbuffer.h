#pragma once
#include "DxCommon.h"

class Gbuffer
{
public:
    static const UINT TargetCount = 3;

    enum Target : UINT
    {
        AlbedoMetallic = 0,
        NormalRoughness = 1,
        PositionAO = 2
    };

    Gbuffer(ID3D12Device* device, UINT rtvDescriptorSize, UINT srvDescriptorSize);

    void Resize(UINT width, UINT height);
    void TransitionToRenderTargets(ID3D12GraphicsCommandList* cmdList);
    void TransitionToShaderResources(ID3D12GraphicsCommandList* cmdList);
    void Clear(ID3D12GraphicsCommandList* cmdList);

    D3D12_CPU_DESCRIPTOR_HANDLE Rtv(UINT index) const;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuSrv(UINT index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE Srv(UINT index) const;
    ID3D12DescriptorHeap* SrvHeap() const { return mSrvHeap.Get(); }

    DXGI_FORMAT Format(UINT index) const { return mFormats[index]; }

private:
    void BuildResources();
    void BuildDescriptors();

private:
    ID3D12Device* mDevice = nullptr;
    UINT mRtvDescriptorSize = 0;
    UINT mSrvDescriptorSize = 0;
    UINT mWidth = 1;
    UINT mHeight = 1;

    DXGI_FORMAT mFormats[TargetCount] =
    {
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R32G32B32A32_FLOAT
    };

    D3D12_RESOURCE_STATES mCurrentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    ComPtr<ID3D12Resource> mTargets[TargetCount];
    ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    ComPtr<ID3D12DescriptorHeap> mSrvHeap;
};
