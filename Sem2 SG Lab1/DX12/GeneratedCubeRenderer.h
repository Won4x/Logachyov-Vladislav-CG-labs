// GeneratedCubeRenderer.h
#pragma once
#include "DxCommon.h"
#include "InputTracker.h"
#include "ObjMeshLoader.h"

struct ObjectConstants
{
    // Matrices sent to shaders: object placement plus camera projection.
    XMFLOAT4X4 WorldViewProj;
    XMFLOAT4X4 World;

    // Lighting and material parameters used in ModelPS.hlsl.
    XMFLOAT3   EyePosW;
    float      pad0;

    XMFLOAT4   LightDir;
    XMFLOAT4   DiffuseColor;

    XMFLOAT4   SpecularColor;
    float      Shininess;

    // Texture controls: scale repeats the texture, offset animates it.
    float      TextureScaleX;
    float      TextureScaleY;
    XMFLOAT2   TextureOffset;
    float      pad1;
    float      pad2;
    float      pad3;

    // x = time, y = curtain enable, z = wind strength, w = wave speed.
    XMFLOAT4   WindParams;
};

class CubeRenderer
{
public:
    CubeRenderer(ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        UINT cbvSrvUavDescriptorSize);

    void BuildResources();
    void BuildPSO();

    void Update(float totalTime, float deltaTime, const InputDevice& input);
    void Draw(ID3D12GraphicsCommandList* cmdList);

    ID3D12RootSignature* GetRootSignature() const { return mRootSignature.Get(); }
    ID3D12PipelineState* GetPSO() const { return mPSO.Get(); }

private:
    void BuildModelGeometry();
    void BuildConstantBuffer();
    void BuildRootSignature();
    void BuildTextureResources();
    void CreateFallbackTexture();
    void UploadConstants(UINT bufferIndex);
    void FitModelToView();

    void UpdateCamera(const InputDevice& input, float dt);
    void UpdateCubeRotation(const InputDevice& input, float dt);

private:
    ID3D12Device* mDevice;
    ID3D12GraphicsCommandList* mCmdList;

    UINT mCbvSrvUavDescriptorSize;
    UINT mConstantBufferByteSize = 0;

    ComPtr<ID3D12Resource> mVertexBuffer;
    ComPtr<ID3D12Resource> mIndexBuffer;

    
    ComPtr<ID3D12Resource> mVBUpload;
    ComPtr<ID3D12Resource> mIBUpload;

    ComPtr<ID3D12Resource> mConstantBuffer;
    ComPtr<ID3D12Resource> mConstantUploadBuffer;
    ComPtr<ID3D12DescriptorHeap> mSrvHeap;

    D3D12_VERTEX_BUFFER_VIEW mVBV = {};
    D3D12_INDEX_BUFFER_VIEW  mIBV = {};

    UINT mIndexCount = 0;

    ObjectConstants mConstants;
    ObjMeshData mMeshData;

    struct RenderMaterial
    {
        // Values are loaded from MTL: Kd, Ks, Ns and map_Kd.
        XMFLOAT4 Diffuse = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
        XMFLOAT4 Specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        float Shininess = 16.0f;
        UINT TextureIndex = 0;
        bool IsCurtain = false;
    };

    struct RenderSubset
    {
        UINT IndexStart = 0;
        UINT IndexCount = 0;
        UINT MaterialIndex = 0;
    };

    std::vector<RenderMaterial> mRenderMaterials;
    std::vector<RenderSubset> mRenderSubsets;
    std::vector<ComPtr<ID3D12Resource>> mTextures;
    std::vector<ComPtr<ID3D12Resource>> mTextureUploads;

    ComPtr<ID3D12RootSignature> mRootSignature;
    ComPtr<ID3D12PipelineState> mPSO;

    XMFLOAT4X4 mProj;

    // Start camera position. Change this if the model opens too close/far.
    XMFLOAT3 mCameraPos = { 0.0f, 0.5f, -7.0f };
    float mYaw = 0.0f;
    float mPitch = 0.0f;

    // Object rotation controlled by left mouse button.
    float mCubeYaw = 0.0f;
    float mCubePitch = 0.0f;

    // Model auto-fit values calculated from the OBJ bounding box.
    XMFLOAT3 mModelCenter = { 0.0f, 0.0f, 0.0f };
    float mModelScale = 1.0f;
};
