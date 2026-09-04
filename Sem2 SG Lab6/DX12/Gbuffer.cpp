#include "Gbuffer.h"

Gbuffer::Gbuffer(ID3D12Device* device, UINT rtvDescriptorSize, UINT srvDescriptorSize)
    : mDevice(device)
    , mRtvDescriptorSize(rtvDescriptorSize)
    , mSrvDescriptorSize(srvDescriptorSize)
{
}

void Gbuffer::Resize(UINT width, UINT height)
{
    mWidth = (std::max)(1u, width);
    mHeight = (std::max)(1u, height);

    for (UINT i = 0; i < TargetCount; ++i)
        mTargets[i].Reset();

    BuildResources();
    BuildDescriptors();
    mCurrentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void Gbuffer::BuildResources()
{
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

    for (UINT i = 0; i < TargetCount; ++i)
    {
        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = mFormats[i];
        clearValue.Color[0] = 0.0f;
        clearValue.Color[1] = 0.0f;
        clearValue.Color[2] = 0.0f;
        clearValue.Color[3] = 0.0f;

        auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
            mFormats[i],
            mWidth,
            mHeight,
            1,
            1,
            1,
            0,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

        ThrowIfFailed(mDevice->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clearValue,
            IID_PPV_ARGS(&mTargets[i])));
    }
}

void Gbuffer::BuildDescriptors()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = TargetCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mRtvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = TargetCount;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(mSrvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < TargetCount; ++i)
    {
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Format = mFormats[i];
        rtvDesc.Texture2D.MipSlice = 0;
        mDevice->CreateRenderTargetView(mTargets[i].Get(), &rtvDesc, rtvHandle);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = mFormats[i];
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        mDevice->CreateShaderResourceView(mTargets[i].Get(), &srvDesc, srvHandle);

        rtvHandle.Offset(1, mRtvDescriptorSize);
        srvHandle.Offset(1, mSrvDescriptorSize);
    }
}

void Gbuffer::TransitionToRenderTargets(ID3D12GraphicsCommandList* cmdList)
{
    if (mCurrentState == D3D12_RESOURCE_STATE_RENDER_TARGET)
        return;

    D3D12_RESOURCE_BARRIER barriers[TargetCount] = {};
    for (UINT i = 0; i < TargetCount; ++i)
    {
        barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
            mTargets[i].Get(),
            mCurrentState,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    cmdList->ResourceBarrier(TargetCount, barriers);
    mCurrentState = D3D12_RESOURCE_STATE_RENDER_TARGET;
}

void Gbuffer::TransitionToShaderResources(ID3D12GraphicsCommandList* cmdList)
{
    if (mCurrentState == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
        return;

    D3D12_RESOURCE_BARRIER barriers[TargetCount] = {};
    for (UINT i = 0; i < TargetCount; ++i)
    {
        barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
            mTargets[i].Get(),
            mCurrentState,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    cmdList->ResourceBarrier(TargetCount, barriers);
    mCurrentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void Gbuffer::Clear(ID3D12GraphicsCommandList* cmdList)
{
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (UINT i = 0; i < TargetCount; ++i)
        cmdList->ClearRenderTargetView(Rtv(i), clearColor, 0, nullptr);
}

D3D12_CPU_DESCRIPTOR_HANDLE Gbuffer::Rtv(UINT index) const
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(mRtvHeap->GetCPUDescriptorHandleForHeapStart(), index, mRtvDescriptorSize);
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE Gbuffer::CpuSrv(UINT index) const
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(mSrvHeap->GetCPUDescriptorHandleForHeapStart(), index, mSrvDescriptorSize);
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE Gbuffer::Srv(UINT index) const
{
    CD3DX12_GPU_DESCRIPTOR_HANDLE handle(mSrvHeap->GetGPUDescriptorHandleForHeapStart(), index, mSrvDescriptorSize);
    return handle;
}
