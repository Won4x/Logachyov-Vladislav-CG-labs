// CubeDemoApp.cpp
#include "CubeDemoApp.h"

CubeApp::CubeApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
    mMainWndCaption = L"CG lab1 | RMB camera | LMB model | WASD QE move | Wheel zoom | Shift fast | Ctrl precise";
}

CubeApp::~CubeApp()
{
}

bool CubeApp::Initialize()
{
    if (!D3DApp::Initialize())
        return false;


    mCommandAllocator->Reset();
    mCommandList->Reset(mCommandAllocator.Get(), nullptr);

    mCube = std::make_unique<CubeRenderer>(
        mDevice.Get(),
        mCommandList.Get(),
        mCbvSrvUavDescriptorSize);

    mCube->BuildResources();

    mCommandList->Close();
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, cmdsLists);
    FlushCommandQueue();

    return true;
}

void CubeApp::OnResize()
{
    D3DApp::OnResize();
}

void CubeApp::Update(const GameTimer& gt)
{

    mCube->Update(gt.TotalTime(), gt.DeltaTime(), mInput);
}

void CubeApp::Draw(const GameTimer& /*gt*/)
{
    mCommandAllocator->Reset();
    mCommandList->Reset(mCommandAllocator.Get(), mCube->GetPSO());

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);


    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &barrier);

    float clearColor[] = { 0.68f, 0.85f, 0.95f, 1.0f };
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), clearColor, 0, nullptr);
    mCommandList->ClearDepthStencilView(
        DepthStencilView(),
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f, 0, 0, nullptr);

    auto rtv = CurrentBackBufferView();
    auto dsv = DepthStencilView();
    mCommandList->OMSetRenderTargets(1, &rtv, TRUE, &dsv);

    mCommandList->SetGraphicsRootSignature(mCube->GetRootSignature());
    mCube->Draw(mCommandList.Get());


    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &barrier);

    mCommandList->Close();
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, cmdsLists);

    ThrowIfFailed(mSwapChain->Present(1, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    FlushCommandQueue();
}
