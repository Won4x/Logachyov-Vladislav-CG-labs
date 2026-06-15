#pragma once
#include "DxCommon.h"
#include "Gbuffer.h"
#include "InputTracker.h"
#include "ObjMeshLoader.h"

struct GeometryConstants
{
    XMFLOAT4X4 WorldViewProj;
    XMFLOAT4X4 World;
    XMFLOAT4 DiffuseColor;
    XMFLOAT4 SpecularColor;
    float Shininess;
    float TextureScaleX;
    float TextureScaleY;
    float TextureOffsetX;
    float TextureOffsetY;
    XMFLOAT3 Pad0;
};

struct DeferredLight
{
    XMFLOAT4 PositionRange;
    XMFLOAT4 DirectionSpot;
    XMFLOAT4 ColorIntensity;
    XMFLOAT4 Params;
};

struct LightingConstants
{
    XMFLOAT3 EyePosW;
    float LightCount;
    XMFLOAT4 AmbientColor;
    DeferredLight Lights[16];
};

class RenderingSystem
{
public:
    RenderingSystem(ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        UINT rtvDescriptorSize,
        UINT srvDescriptorSize);

    void BuildResources(UINT width, UINT height);
    void OnResize(UINT width, UINT height);
    void Update(float totalTime, float deltaTime, const InputDevice& input);
    void Draw(ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* backBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE backBufferView,
        D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView);

private:
    struct RenderMaterial
    {
        XMFLOAT4 Diffuse = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
        XMFLOAT4 Specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        float Shininess = 16.0f;
        UINT TextureIndex = 0;
    };

    struct RenderSubset
    {
        UINT IndexStart = 0;
        UINT IndexCount = 0;
        UINT MaterialIndex = 0;
    };

    void BuildModelGeometry();
    void FitModelToView();
    void BuildTextureResources();
    void CreateFallbackTexture();
    void BuildConstantBuffers();
    void BuildGeometryRootSignature();
    void BuildLightingRootSignature();
    void BuildPipelineStates();
    void BuildLights();
    void UploadGeometryConstants(UINT bufferIndex);
    void UploadLightingConstants();
    void UpdateCamera(const InputDevice& input, float dt);
    void UpdateLightControls(const InputDevice& input, float dt);

private:
    ID3D12Device* mDevice = nullptr;
    ID3D12GraphicsCommandList* mCmdList = nullptr;
    UINT mRtvDescriptorSize = 0;
    UINT mSrvDescriptorSize = 0;
    UINT mGeometryConstantByteSize = 0;
    UINT mLightingConstantByteSize = 0;

    std::unique_ptr<Gbuffer> mGbuffer;

    ComPtr<ID3D12Resource> mVertexBuffer;
    ComPtr<ID3D12Resource> mIndexBuffer;
    ComPtr<ID3D12Resource> mVBUpload;
    ComPtr<ID3D12Resource> mIBUpload;
    D3D12_VERTEX_BUFFER_VIEW mVBV = {};
    D3D12_INDEX_BUFFER_VIEW mIBV = {};
    UINT mIndexCount = 0;

    ComPtr<ID3D12Resource> mGeometryConstantBuffer;
    ComPtr<ID3D12Resource> mLightingConstantBuffer;
    ComPtr<ID3D12DescriptorHeap> mTextureHeap;

    std::vector<ComPtr<ID3D12Resource>> mTextures;
    std::vector<ComPtr<ID3D12Resource>> mTextureUploads;
    ObjMeshData mMeshData;
    std::vector<RenderMaterial> mRenderMaterials;
    std::vector<RenderSubset> mRenderSubsets;

    ComPtr<ID3D12RootSignature> mGeometryRootSignature;
    ComPtr<ID3D12RootSignature> mLightingRootSignature;
    ComPtr<ID3D12PipelineState> mGeometryPSO;
    ComPtr<ID3D12PipelineState> mLightingPSO;

    GeometryConstants mGeometryConstants = {};
    LightingConstants mLightingConstants = {};
    XMFLOAT4X4 mProj;
    XMFLOAT3 mCameraPos = { 0.0f, 55.0f, -185.0f };
    XMFLOAT3 mCameraForward = { 0.0f, 0.0f, 1.0f };
    float mYaw = 0.0f;
    float mPitch = -0.05f;
    XMFLOAT3 mModelCenter = { 0.0f, 0.0f, 0.0f };
    float mModelScale = 1.0f;
    UINT mSelectedLight = 1;
};
