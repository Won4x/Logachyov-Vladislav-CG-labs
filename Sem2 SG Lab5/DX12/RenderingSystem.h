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
    XMFLOAT4 MaterialParams;
    XMFLOAT4 TextureTransform;
    XMFLOAT4 EyeDisplacement;
    XMFLOAT4 TessellationParams;
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
    XMFLOAT4X4 View;
    XMFLOAT4X4 ShadowViewProj[4];
    XMFLOAT4 CascadeSplits;
    XMFLOAT4 ShadowTexelSizeBias;
};

struct ShadowConstants
{
    XMFLOAT4X4 WorldViewProj;
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
    std::wstring StatusText() const;

private:
    struct RenderMaterial
    {
        XMFLOAT4 Diffuse = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
        XMFLOAT4 Specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        float Shininess = 16.0f;
        UINT TextureIndex = 0;
        UINT NormalTextureIndex = 1;
        UINT DisplacementTextureIndex = 2;
    };

    struct RenderSubset
    {
        UINT IndexStart = 0;
        UINT IndexCount = 0;
        UINT MaterialIndex = 0;
    };

    void BuildModelGeometry();
    void FitModelToView();
    void BuildSceneObjects();
    void BuildOctree();
    void BuildTextureResources();
    void CreateFallbackTexture();
    void BuildConstantBuffers();
    void BuildShadowResources();
    void BuildGeometryRootSignature();
    void BuildShadowRootSignature();
    void BuildLightingRootSignature();
    void BuildLightingDescriptors();
    void BuildPipelineStates();
    void BuildLights();
    void UploadGeometryConstants(UINT bufferIndex, const GeometryConstants& constants);
    void UploadShadowConstants(UINT bufferIndex, const ShadowConstants& constants);
    void UploadLightingConstants();
    void UpdateShadowMatrices(const XMMATRIX& view, const XMMATRIX& proj);
    void UpdateCamera(const InputDevice& input, float dt);
    void UpdateLightControls(const InputDevice& input, float dt);
    void UpdateCullingMode(const InputDevice& input);
    void CollectVisibleObjects(const BoundingFrustum& frustum);
    void CollectVisibleObjectsLinear(const BoundingFrustum& frustum);
    void CollectVisibleObjectsOctree(const BoundingFrustum& frustum);
    void DrawShadowMap(ID3D12GraphicsCommandList* cmdList);

private:
    struct SceneObject
    {
        XMFLOAT4X4 World = {};
        BoundingBox Bounds = {};
    };

    struct OctreeNode
    {
        BoundingBox CellBounds = {};
        BoundingBox Bounds = {};
        std::vector<UINT> ObjectIndices;
        std::unique_ptr<OctreeNode> Children[8];
    };

    ID3D12Device* mDevice = nullptr;
    ID3D12GraphicsCommandList* mCmdList = nullptr;
    UINT mRtvDescriptorSize = 0;
    UINT mSrvDescriptorSize = 0;
    UINT mGeometryConstantByteSize = 0;
    UINT mLightingConstantByteSize = 0;
    UINT mShadowConstantByteSize = 0;
    UINT mRenderWidth = 1280;
    UINT mRenderHeight = 720;

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
    ComPtr<ID3D12Resource> mShadowConstantBuffer;
    ComPtr<ID3D12DescriptorHeap> mTextureHeap;
    ComPtr<ID3D12Resource> mShadowMap;
    ComPtr<ID3D12DescriptorHeap> mShadowDsvHeap;
    ComPtr<ID3D12DescriptorHeap> mLightingSrvHeap;

    std::vector<ComPtr<ID3D12Resource>> mTextures;
    std::vector<ComPtr<ID3D12Resource>> mTextureUploads;
    ObjMeshData mMeshData;
    std::vector<RenderMaterial> mRenderMaterials;
    std::vector<RenderSubset> mRenderSubsets;
    std::vector<SceneObject> mSceneObjects;
    std::vector<UINT> mVisibleObjectIndices;
    std::unique_ptr<OctreeNode> mOctreeRoot;
    BoundingBox mLocalMeshBounds;

    ComPtr<ID3D12RootSignature> mGeometryRootSignature;
    ComPtr<ID3D12RootSignature> mShadowRootSignature;
    ComPtr<ID3D12RootSignature> mLightingRootSignature;
    ComPtr<ID3D12PipelineState> mGeometryPSO;
    ComPtr<ID3D12PipelineState> mShadowPSO;
    ComPtr<ID3D12PipelineState> mLightingPSO;

    GeometryConstants mGeometryConstants = {};
    LightingConstants mLightingConstants = {};
    D3D12_VIEWPORT mShadowViewport = {};
    D3D12_RECT mShadowScissorRect = {};
    D3D12_RESOURCE_STATES mShadowMapState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    XMFLOAT4X4 mProj;
    XMFLOAT3 mCameraPos = { 0.0f, 70.0f, -430.0f };
    XMFLOAT3 mCameraForward = { 0.0f, 0.0f, 1.0f };
    float mYaw = 0.0f;
    float mPitch = -0.08f;
    XMFLOAT3 mModelCenter = { 0.0f, 0.0f, 0.0f };
    float mModelScale = 1.0f;
    UINT mSelectedLight = 1;
    UINT mTotalOctreeNodeVisits = 0;
    UINT mLastTestedBounds = 0;
    bool mOctreeSelfCheckPassed = true;
    bool mFrustumCullingEnabled = true;
    bool mOctreeCullingEnabled = true;
};
