// CubeDemoApp.cpp
#include "CubeDemoApp.h"

CubeApp::CubeApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
    mMainWndCaption = L"CG lab2 | Deferred Rendering | 1 Dir 2 Point 3 Spot | C place light";
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

    mRenderingSystem = std::make_unique<RenderingSystem>(
        mDevice.Get(),
        mCommandList.Get(),
        mRtvDescriptorSize,
        mCbvSrvUavDescriptorSize);

    mRenderingSystem->BuildResources(mClientWidth, mClientHeight);

    mCommandList->Close();
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, cmdsLists);
    FlushCommandQueue();

    return true;
}

void CubeApp::OnResize()
{
    D3DApp::OnResize();
    if (mRenderingSystem)
        mRenderingSystem->OnResize(mClientWidth, mClientHeight);
}

void CubeApp::Update(const GameTimer& gt)
{

    mRenderingSystem->Update(gt.TotalTime(), gt.DeltaTime(), mInput);
}

void CubeApp::Draw(const GameTimer& /*gt*/)
{
    mCommandAllocator->Reset();
    mCommandList->Reset(mCommandAllocator.Get(), nullptr);

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    mRenderingSystem->Draw(
        mCommandList.Get(),
        CurrentBackBuffer(),
        CurrentBackBufferView(),
        DepthStencilView());

    mCommandList->Close();
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, cmdsLists);

    ThrowIfFailed(mSwapChain->Present(1, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    FlushCommandQueue();
}
