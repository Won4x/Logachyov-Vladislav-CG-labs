#include "RenderingSystem.h"
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cwctype>
#include <iomanip>
#include <sstream>

namespace
{
    constexpr UINT CascadeCount = 4;
    constexpr UINT ShadowMapSize = 2048;
    constexpr float ShadowNearZ = 0.1f;
    constexpr float ShadowFarZ = 850.0f;
    constexpr float CascadeSplitLambda = 0.78f;

    struct GpuParticle
    {
        XMFLOAT3 Position;
        float Age;
        XMFLOAT3 Velocity;
        float LifetimeScale;
        XMFLOAT4 Color;
        float Size;
        UINT Seed;
        XMFLOAT2 Padding;
    };

    bool FileExists(const std::wstring& path)
    {
        DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    std::wstring ToLower(std::wstring text)
    {
        std::transform(text.begin(), text.end(), text.begin(),
            [](wchar_t ch) { return (wchar_t)towlower(ch); });
        return text;
    }

    bool HasExtension(const std::wstring& filename, const std::wstring& extension)
    {
        const std::wstring lower = ToLower(filename);
        return lower.size() >= extension.size()
            && lower.compare(lower.size() - extension.size(), extension.size(), extension) == 0;
    }

    std::wstring FindExistingModelPath()
    {
        const std::wstring candidates[] =
        {
            L"Models/sponza.obj",
            L"../x64/Debug/Models/sponza.obj",
            L"x64/Debug/Models/sponza.obj",
            L"../../x64/Debug/Models/sponza.obj"
        };

        for (const auto& candidate : candidates)
        {
            if (FileExists(candidate))
                return candidate;
        }

        return L"";
    }

    UINT FindMaterialIndex(const ObjMeshData& mesh, const std::string& name)
    {
        for (UINT i = 0; i < (UINT)mesh.Materials.size(); ++i)
        {
            if (mesh.Materials[i].Name == name)
                return i;
        }

        return 0;
    }

    bool LoadTgaPixels(const std::wstring& filename, UINT& width, UINT& height, std::vector<uint8_t>& rgbaPixels)
    {
#pragma pack(push, 1)
        struct TgaHeader
        {
            uint8_t IdLength;
            uint8_t ColorMapType;
            uint8_t ImageType;
            uint16_t ColorMapFirstEntry;
            uint16_t ColorMapLength;
            uint8_t ColorMapEntrySize;
            uint16_t XOrigin;
            uint16_t YOrigin;
            uint16_t Width;
            uint16_t Height;
            uint8_t PixelDepth;
            uint8_t ImageDescriptor;
        };
#pragma pack(pop)

        FILE* file = nullptr;
        if (_wfopen_s(&file, filename.c_str(), L"rb") != 0 || file == nullptr)
            return false;

        TgaHeader header{};
        if (fread(&header, 1, sizeof(header), file) != sizeof(header)
            || header.Width == 0 || header.Height == 0)
        {
            fclose(file);
            return false;
        }

        const bool isColor = header.ImageType == 2;
        const bool isGray = header.ImageType == 3;
        const UINT bytesPerPixel = header.PixelDepth / 8;
        if ((!isColor && !isGray) || header.ColorMapType != 0
            || (isColor && bytesPerPixel != 3 && bytesPerPixel != 4)
            || (isGray && bytesPerPixel != 1))
        {
            fclose(file);
            return false;
        }

        fseek(file, header.IdLength, SEEK_CUR);
        width = header.Width;
        height = header.Height;

        const size_t srcStride = (size_t)width * bytesPerPixel;
        std::vector<uint8_t> src(srcStride * height);
        if (fread(src.data(), 1, src.size(), file) != src.size())
        {
            fclose(file);
            return false;
        }
        fclose(file);

        rgbaPixels.assign((size_t)width * height * 4, 255);
        const bool topOrigin = (header.ImageDescriptor & 0x20) != 0;
        for (UINT y = 0; y < height; ++y)
        {
            const UINT srcY = topOrigin ? y : (height - 1 - y);
            const uint8_t* srcRow = src.data() + (size_t)srcY * srcStride;
            uint8_t* dstRow = rgbaPixels.data() + (size_t)y * width * 4;

            for (UINT x = 0; x < width; ++x)
            {
                const uint8_t* s = srcRow + (size_t)x * bytesPerPixel;
                uint8_t* d = dstRow + (size_t)x * 4;
                if (isGray)
                {
                    d[0] = d[1] = d[2] = s[0];
                    d[3] = 255;
                }
                else
                {
                    d[0] = s[2];
                    d[1] = s[1];
                    d[2] = s[0];
                    d[3] = bytesPerPixel == 4 ? s[3] : 255;
                }
            }
        }

        return true;
    }

    bool LoadTextureFromFile(ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        const std::wstring& filename,
        ComPtr<ID3D12Resource>& texture,
        ComPtr<ID3D12Resource>& upload)
    {
        UINT width = 0;
        UINT height = 0;
        std::vector<uint8_t> pixels;

        if (HasExtension(filename, L".tga"))
        {
            if (!LoadTgaPixels(filename, width, height, pixels))
                return false;
        }
        else
        {
            return false;
        }

        CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
        auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height);
        if (FAILED(device->CreateCommittedResource(
            &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture))))
            return false;

        UINT64 uploadSize = GetRequiredIntermediateSize(texture.Get(), 0, 1);
        CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
        auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
        if (FAILED(device->CreateCommittedResource(
            &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))))
            return false;

        D3D12_SUBRESOURCE_DATA subresource = {};
        subresource.pData = pixels.data();
        subresource.RowPitch = width * 4;
        subresource.SlicePitch = subresource.RowPitch * height;
        UpdateSubresources(cmdList, texture.Get(), upload.Get(), 0, 0, 1, &subresource);

        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmdList->ResourceBarrier(1, &barrier);
        return true;
    }

    void CompileShader(const std::wstring& filename, const char* entry, const char* target, ComPtr<ID3DBlob>& blob)
    {
        ComPtr<ID3DBlob> errors;
        HRESULT hr = D3DCompileFromFile(filename.c_str(), nullptr, nullptr, entry, target,
            D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &blob, &errors);

        if (FAILED(hr))
        {
            if (errors)
                MessageBoxA(nullptr, (char*)errors->GetBufferPointer(), "Shader compile error", MB_OK);
            ThrowIfFailed(hr);
        }
    }
}

RenderingSystem::RenderingSystem(ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    UINT rtvDescriptorSize,
    UINT srvDescriptorSize)
    : mDevice(device)
    , mCmdList(cmdList)
    , mRtvDescriptorSize(rtvDescriptorSize)
    , mSrvDescriptorSize(srvDescriptorSize)
{
    XMStoreFloat4x4(&mProj, XMMatrixPerspectiveFovLH(0.25f * XM_PI, 1280.0f / 720.0f, 0.1f, 5000.0f));
    for (UINT i = 0; i < 2; ++i)
    {
        mParticleBufferStates[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        mParticleCounterStates[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
}

void RenderingSystem::BuildResources(UINT width, UINT height)
{
    mGbuffer = std::make_unique<Gbuffer>(mDevice, mRtvDescriptorSize, mSrvDescriptorSize);
    mGbuffer->Resize(width, height);
    OnResize(width, height);

    BuildModelGeometry();
    FitModelToView();
    BuildSceneObjects();
    BuildOctree();
    BuildTextureResources();
    BuildShadowResources();
    BuildParticleResources();
    BuildConstantBuffers();
    BuildGeometryRootSignature();
    BuildShadowRootSignature();
    BuildLightingRootSignature();
    BuildParticleRootSignatures();
    BuildLightingDescriptors();
    BuildParticleDescriptors();
    BuildPipelineStates();
    BuildLights();
}

void RenderingSystem::OnResize(UINT width, UINT height)
{
    mRenderWidth = (std::max)(1u, width);
    mRenderHeight = (std::max)(1u, height);

    if (mGbuffer)
        mGbuffer->Resize(width, height);

    const float aspect = height > 0 ? (float)width / (float)height : 1.0f;
    XMStoreFloat4x4(&mProj, XMMatrixPerspectiveFovLH(0.25f * XM_PI, aspect, 0.1f, 5000.0f));

    if (mShadowMap)
        BuildLightingDescriptors();
}

void RenderingSystem::BuildModelGeometry()
{
    const std::wstring modelPath = FindExistingModelPath();
    if (!modelPath.empty())
        ObjLoader::LoadObjPosNormal(modelPath, mMeshData, true);

    if (mMeshData.Vertices.empty() || mMeshData.Indices.empty())
    {
        mMeshData.Vertices =
        {
            { XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(-1.0f,  1.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3( 1.0f,  1.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3( 1.0f, -1.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
        };
        mMeshData.Indices.clear();
        mMeshData.Indices.push_back(0);
        mMeshData.Indices.push_back(1);
        mMeshData.Indices.push_back(2);
        mMeshData.Indices.push_back(0);
        mMeshData.Indices.push_back(2);
        mMeshData.Indices.push_back(3);
        ObjMaterialData material;
        material.Name = "Default";
        mMeshData.Materials.clear();
        mMeshData.Materials.push_back(material);

        ObjSubset subset;
        subset.MaterialName = "Default";
        subset.IndexStart = 0;
        subset.IndexCount = 6;
        mMeshData.Subsets.clear();
        mMeshData.Subsets.push_back(subset);
    }

    mIndexCount = (UINT)mMeshData.Indices.size();
    const UINT vBufferSize = (UINT)(mMeshData.Vertices.size() * sizeof(VertexPosNormal));
    const UINT iBufferSize = (UINT)(mMeshData.Indices.size() * sizeof(uint32_t));

    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vBufferSize);
    auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(iBufferSize);

    ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mVertexBuffer)));
    ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mVBUpload)));

    void* mapped = nullptr;
    ThrowIfFailed(mVBUpload->Map(0, nullptr, &mapped));
    memcpy(mapped, mMeshData.Vertices.data(), vBufferSize);
    mVBUpload->Unmap(0, nullptr);
    mCmdList->CopyBufferRegion(mVertexBuffer.Get(), 0, mVBUpload.Get(), 0, vBufferSize);
    auto vbBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        mVertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    mCmdList->ResourceBarrier(1, &vbBarrier);

    ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &ibDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mIndexBuffer)));
    ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &ibDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mIBUpload)));

    ThrowIfFailed(mIBUpload->Map(0, nullptr, &mapped));
    memcpy(mapped, mMeshData.Indices.data(), iBufferSize);
    mIBUpload->Unmap(0, nullptr);
    mCmdList->CopyBufferRegion(mIndexBuffer.Get(), 0, mIBUpload.Get(), 0, iBufferSize);
    auto ibBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        mIndexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    mCmdList->ResourceBarrier(1, &ibBarrier);

    mVBV.BufferLocation = mVertexBuffer->GetGPUVirtualAddress();
    mVBV.StrideInBytes = sizeof(VertexPosNormal);
    mVBV.SizeInBytes = vBufferSize;
    mIBV.BufferLocation = mIndexBuffer->GetGPUVirtualAddress();
    mIBV.Format = DXGI_FORMAT_R32_UINT;
    mIBV.SizeInBytes = iBufferSize;
}

void RenderingSystem::FitModelToView()
{
    XMFLOAT3 minP = mMeshData.Vertices[0].Pos;
    XMFLOAT3 maxP = mMeshData.Vertices[0].Pos;

    for (const auto& vertex : mMeshData.Vertices)
    {
        minP.x = (std::min)(minP.x, vertex.Pos.x);
        minP.y = (std::min)(minP.y, vertex.Pos.y);
        minP.z = (std::min)(minP.z, vertex.Pos.z);
        maxP.x = (std::max)(maxP.x, vertex.Pos.x);
        maxP.y = (std::max)(maxP.y, vertex.Pos.y);
        maxP.z = (std::max)(maxP.z, vertex.Pos.z);
    }

    mModelCenter = XMFLOAT3(
        0.5f * (minP.x + maxP.x),
        0.5f * (minP.y + maxP.y),
        0.5f * (minP.z + maxP.z));

    const XMFLOAT3 extents(
        0.5f * (maxP.x - minP.x),
        0.5f * (maxP.y - minP.y),
        0.5f * (maxP.z - minP.z));

    mLocalMeshBounds = BoundingBox(mModelCenter, extents);

    const float maxSize = (std::max)(maxP.x - minP.x, (std::max)(maxP.y - minP.y, maxP.z - minP.z));
    mModelScale = maxSize > 0.0001f ? 18.0f / maxSize : 1.0f;
}

void RenderingSystem::BuildSceneObjects()
{
    const UINT objectCount = 1u;
    mSceneObjects.clear();
    mSceneObjects.reserve(objectCount);
    mVisibleObjectIndices.reserve(objectCount);

    for (UINT i = 0; i < objectCount; ++i)
    {
        XMMATRIX world =
            XMMatrixTranslation(-mModelCenter.x, -mModelCenter.y, -mModelCenter.z) *
            XMMatrixScaling(mModelScale, mModelScale, mModelScale);

        SceneObject object;
        XMStoreFloat4x4(&object.World, world);
        mLocalMeshBounds.Transform(object.Bounds, world);
        mSceneObjects.push_back(object);
    }
}

void RenderingSystem::BuildOctree()
{
    mOctreeRoot.reset();
    if (mSceneObjects.empty())
        return;

    XMFLOAT3 minP(
        mSceneObjects[0].Bounds.Center.x - mSceneObjects[0].Bounds.Extents.x,
        mSceneObjects[0].Bounds.Center.y - mSceneObjects[0].Bounds.Extents.y,
        mSceneObjects[0].Bounds.Center.z - mSceneObjects[0].Bounds.Extents.z);
    XMFLOAT3 maxP(
        mSceneObjects[0].Bounds.Center.x + mSceneObjects[0].Bounds.Extents.x,
        mSceneObjects[0].Bounds.Center.y + mSceneObjects[0].Bounds.Extents.y,
        mSceneObjects[0].Bounds.Center.z + mSceneObjects[0].Bounds.Extents.z);

    for (const SceneObject& object : mSceneObjects)
    {
        const XMFLOAT3 bMin(
            object.Bounds.Center.x - object.Bounds.Extents.x,
            object.Bounds.Center.y - object.Bounds.Extents.y,
            object.Bounds.Center.z - object.Bounds.Extents.z);
        const XMFLOAT3 bMax(
            object.Bounds.Center.x + object.Bounds.Extents.x,
            object.Bounds.Center.y + object.Bounds.Extents.y,
            object.Bounds.Center.z + object.Bounds.Extents.z);

        minP.x = (std::min)(minP.x, bMin.x);
        minP.y = (std::min)(minP.y, bMin.y);
        minP.z = (std::min)(minP.z, bMin.z);
        maxP.x = (std::max)(maxP.x, bMax.x);
        maxP.y = (std::max)(maxP.y, bMax.y);
        maxP.z = (std::max)(maxP.z, bMax.z);
    }

    XMFLOAT3 center(
        0.5f * (minP.x + maxP.x),
        0.5f * (minP.y + maxP.y),
        0.5f * (minP.z + maxP.z));
    XMFLOAT3 extents(
        0.5f * (maxP.x - minP.x) + 1.0f,
        0.5f * (maxP.y - minP.y) + 1.0f,
        0.5f * (maxP.z - minP.z) + 1.0f);
    extents.y = (std::max)(extents.y, 120.0f);

    mOctreeRoot = std::make_unique<OctreeNode>();
    mOctreeRoot->CellBounds = BoundingBox(center, extents);
    mOctreeRoot->Bounds = mOctreeRoot->CellBounds;

    std::vector<UINT> allObjects(mSceneObjects.size());
    for (UINT i = 0; i < (UINT)mSceneObjects.size(); ++i)
        allObjects[i] = i;

    const UINT maxDepth = 6;
    const UINT maxObjectsPerLeaf = 8;

    auto objectFitsInside = [&](const BoundingBox& child, const BoundingBox& object)
    {
        const XMFLOAT3 cMin(child.Center.x - child.Extents.x, child.Center.y - child.Extents.y, child.Center.z - child.Extents.z);
        const XMFLOAT3 cMax(child.Center.x + child.Extents.x, child.Center.y + child.Extents.y, child.Center.z + child.Extents.z);
        const XMFLOAT3 oMin(object.Center.x - object.Extents.x, object.Center.y - object.Extents.y, object.Center.z - object.Extents.z);
        const XMFLOAT3 oMax(object.Center.x + object.Extents.x, object.Center.y + object.Extents.y, object.Center.z + object.Extents.z);

        return oMin.x >= cMin.x && oMax.x <= cMax.x
            && oMin.y >= cMin.y && oMax.y <= cMax.y
            && oMin.z >= cMin.z && oMax.z <= cMax.z;
    };

    auto buildNode = [&](auto&& self, OctreeNode& node, const std::vector<UINT>& objects, UINT depth) -> void
    {
        if (depth >= maxDepth || objects.size() <= maxObjectsPerLeaf)
        {
            node.ObjectIndices = objects;
            return;
        }

        const XMFLOAT3 childCellExtents(
            node.CellBounds.Extents.x * 0.5f,
            node.CellBounds.Extents.y * 0.5f,
            node.CellBounds.Extents.z * 0.5f);
        const float looseFactor = 2.0f;

        BoundingBox childCellBounds[8];
        BoundingBox childLooseBounds[8];
        std::vector<UINT> childObjects[8];
        std::vector<UINT> nodeObjects;

        for (UINT i = 0; i < 8; ++i)
        {
            const float sx = (i & 1) ? 1.0f : -1.0f;
            const float sy = (i & 2) ? 1.0f : -1.0f;
            const float sz = (i & 4) ? 1.0f : -1.0f;
            childCellBounds[i] = BoundingBox(
                XMFLOAT3(
                    node.CellBounds.Center.x + sx * childCellExtents.x,
                    node.CellBounds.Center.y + sy * childCellExtents.y,
                    node.CellBounds.Center.z + sz * childCellExtents.z),
                childCellExtents);
            childLooseBounds[i] = BoundingBox(
                childCellBounds[i].Center,
                XMFLOAT3(
                    childCellExtents.x * looseFactor,
                    childCellExtents.y * looseFactor,
                    childCellExtents.z * looseFactor));
        }

        for (UINT objectIndex : objects)
        {
            int childIndex = -1;
            for (UINT i = 0; i < 8; ++i)
            {
                if (objectFitsInside(childLooseBounds[i], mSceneObjects[objectIndex].Bounds))
                {
                    childIndex = (int)i;
                    break;
                }
            }

            if (childIndex >= 0)
                childObjects[childIndex].push_back(objectIndex);
            else
                nodeObjects.push_back(objectIndex);
        }

        node.ObjectIndices = nodeObjects;
        bool hasChild = false;
        for (UINT i = 0; i < 8; ++i)
        {
            if (!childObjects[i].empty())
            {
                hasChild = true;
                node.Children[i] = std::make_unique<OctreeNode>();
                node.Children[i]->CellBounds = childCellBounds[i];
                node.Children[i]->Bounds = childLooseBounds[i];
                self(self, *node.Children[i], childObjects[i], depth + 1);
            }
        }

        if (!hasChild)
            node.ObjectIndices = objects;
    };

    buildNode(buildNode, *mOctreeRoot, allObjects, 0);
}

void RenderingSystem::CreateFallbackTexture()
{
    const UINT width = 4;
    const UINT height = 4;
    auto createTexture = [&](const uint32_t* pixels)
    {
        CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
        auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height);
        ComPtr<ID3D12Resource> texture;
        ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture)));

        UINT64 uploadSize = GetRequiredIntermediateSize(texture.Get(), 0, 1);
        CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
        auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
        ComPtr<ID3D12Resource> upload;
        ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)));

        D3D12_SUBRESOURCE_DATA subresource = {};
        subresource.pData = pixels;
        subresource.RowPitch = width * sizeof(uint32_t);
        subresource.SlicePitch = subresource.RowPitch * height;
        UpdateSubresources(mCmdList, texture.Get(), upload.Get(), 0, 0, 1, &subresource);
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        mCmdList->ResourceBarrier(1, &barrier);

        mTextures.push_back(texture);
        mTextureUploads.push_back(upload);
    };

    uint32_t diffusePixels[width * height] = {};
    for (UINT y = 0; y < height; ++y)
    {
        for (UINT x = 0; x < width; ++x)
            diffusePixels[y * width + x] = ((x + y) % 2) == 0 ? 0xffffffffu : 0xff707070u;
    }

    uint32_t normalPixels[width * height] = {};
    uint32_t displacementPixels[width * height] = {};
    for (UINT i = 0; i < width * height; ++i)
    {
        normalPixels[i] = 0xffff8080u;
        displacementPixels[i] = 0xff808080u;
    }

    createTexture(diffusePixels);
    createTexture(normalPixels);
    createTexture(displacementPixels);
}

void RenderingSystem::BuildTextureResources()
{
    CreateFallbackTexture();
    std::unordered_map<std::wstring, UINT> textureLookup;
    mRenderMaterials.resize(mMeshData.Materials.size());

    for (UINT i = 0; i < (UINT)mMeshData.Materials.size(); ++i)
    {
        const auto& src = mMeshData.Materials[i];
        auto& dst = mRenderMaterials[i];
        dst.Diffuse = src.Diffuse;
        dst.Specular = src.Specular;
        dst.Shininess = src.Shininess;

        auto loadTextureIndex = [&](const std::wstring& path, UINT& index)
        {
            if (path.empty() || !FileExists(path))
                return;

            auto found = textureLookup.find(path);
            if (found != textureLookup.end())
            {
                index = found->second;
            }
            else
            {
                ComPtr<ID3D12Resource> texture;
                ComPtr<ID3D12Resource> upload;
                if (LoadTextureFromFile(mDevice, mCmdList, path, texture, upload))
                {
                    index = (UINT)mTextures.size();
                    textureLookup[path] = index;
                    mTextures.push_back(texture);
                    mTextureUploads.push_back(upload);
                }
            }
        };

        loadTextureIndex(src.DiffuseTexture, dst.TextureIndex);
        loadTextureIndex(src.NormalTexture, dst.NormalTextureIndex);
        loadTextureIndex(src.DisplacementTexture, dst.DisplacementTextureIndex);
    }

    for (const auto& subset : mMeshData.Subsets)
    {
        RenderSubset renderSubset;
        renderSubset.IndexStart = subset.IndexStart;
        renderSubset.IndexCount = subset.IndexCount;
        renderSubset.MaterialIndex = FindMaterialIndex(mMeshData, subset.MaterialName);
        mRenderSubsets.push_back(renderSubset);
    }

    if (mRenderSubsets.empty())
    {
        RenderSubset subset;
        subset.IndexStart = 0;
        subset.IndexCount = mIndexCount;
        subset.MaterialIndex = 0;
        mRenderSubsets.push_back(subset);
    }

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = (UINT)mTextures.size();
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mTextureHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(mTextureHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < (UINT)mTextures.size(); ++i)
    {
        auto desc = mTextures[i]->GetDesc();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        mDevice->CreateShaderResourceView(mTextures[i].Get(), &srvDesc, handle);
        handle.Offset(1, mSrvDescriptorSize);
    }
}

void RenderingSystem::BuildConstantBuffers()
{
    mGeometryConstantByteSize = (sizeof(GeometryConstants) + 255) & ~255;
    mLightingConstantByteSize = (sizeof(LightingConstants) + 255) & ~255;
    mShadowConstantByteSize = (sizeof(ShadowConstants) + 255) & ~255;
    const UINT particleConstantByteSize = (sizeof(ParticleConstants) + 255) & ~255;
    const UINT particleSimConstantByteSize = (sizeof(ParticleSimConstants) + 255) & ~255;

    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    const UINT drawConstantCount = (std::max)(1u, (UINT)(mSceneObjects.size() * mRenderSubsets.size()));
    const UINT shadowConstantCount = (std::max)(1u, CascadeCount * drawConstantCount);
    auto geoDesc = CD3DX12_RESOURCE_DESC::Buffer(mGeometryConstantByteSize * drawConstantCount);
    auto lightDesc = CD3DX12_RESOURCE_DESC::Buffer(mLightingConstantByteSize);
    auto shadowDesc = CD3DX12_RESOURCE_DESC::Buffer(mShadowConstantByteSize * shadowConstantCount);
    auto particleDesc = CD3DX12_RESOURCE_DESC::Buffer(particleConstantByteSize);
    auto particleSimDesc = CD3DX12_RESOURCE_DESC::Buffer(particleSimConstantByteSize);

    ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &geoDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mGeometryConstantBuffer)));
    ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &lightDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mLightingConstantBuffer)));
    ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &shadowDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mShadowConstantBuffer)));
    ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &particleDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mParticleConstantBuffer)));
    ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &particleSimDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mParticleSimConstantBuffer)));
}

void RenderingSystem::BuildShadowResources()
{
    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    auto shadowDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R24G8_TYPELESS,
        ShadowMapSize,
        ShadowMapSize,
        CascadeCount,
        1,
        1,
        0,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &shadowDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&mShadowMap)));
    mShadowMapState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = CascadeCount;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mShadowDsvHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(mShadowDsvHeap->GetCPUDescriptorHandleForHeapStart());
    const UINT dsvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    for (UINT i = 0; i < CascadeCount; ++i)
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.MipSlice = 0;
        dsvDesc.Texture2DArray.FirstArraySlice = i;
        dsvDesc.Texture2DArray.ArraySize = 1;
        mDevice->CreateDepthStencilView(mShadowMap.Get(), &dsvDesc, dsvHandle);
        dsvHandle.Offset(1, dsvDescriptorSize);
    }

    mShadowViewport.TopLeftX = 0.0f;
    mShadowViewport.TopLeftY = 0.0f;
    mShadowViewport.Width = (float)ShadowMapSize;
    mShadowViewport.Height = (float)ShadowMapSize;
    mShadowViewport.MinDepth = 0.0f;
    mShadowViewport.MaxDepth = 1.0f;

    mShadowScissorRect = { 0, 0, (LONG)ShadowMapSize, (LONG)ShadowMapSize };
}

void RenderingSystem::BuildParticleResources()
{
    std::vector<GpuParticle> initialParticles(ParticleCount);
    for (UINT i = 0; i < ParticleCount; ++i)
    {
        initialParticles[i].Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
        initialParticles[i].Age = 0.0f;
        initialParticles[i].Velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
        initialParticles[i].LifetimeScale = 1.0f;
        initialParticles[i].Color = XMFLOAT4(1.0f, 0.55f, 0.12f, 1.0f);
        initialParticles[i].Size = 1.0f;
        initialParticles[i].Seed = 747796405u * (i + 1u) + 2891336453u;
        initialParticles[i].Padding = XMFLOAT2(0.0f, 0.0f);
    }

    const UINT64 particleBufferSize = (UINT64)initialParticles.size() * sizeof(GpuParticle);
    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(particleBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(particleBufferSize);
    auto counterDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto counterUploadDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT) * 2);

    for (UINT i = 0; i < 2; ++i)
    {
        ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mParticleBuffers[i])));
        ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mParticleBufferUploads[i])));

        void* mapped = nullptr;
        ThrowIfFailed(mParticleBufferUploads[i]->Map(0, nullptr, &mapped));
        memcpy(mapped, initialParticles.data(), (size_t)particleBufferSize);
        mParticleBufferUploads[i]->Unmap(0, nullptr);
        mCmdList->CopyBufferRegion(mParticleBuffers[i].Get(), 0, mParticleBufferUploads[i].Get(), 0, particleBufferSize);

        auto particleBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
            mParticleBuffers[i].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        mCmdList->ResourceBarrier(1, &particleBarrier);
        mParticleBufferStates[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &counterDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mParticleCounterBuffers[i])));
        mParticleCounterStates[i] = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &counterUploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mParticleCounterResetUpload)));

    UINT* counters = nullptr;
    ThrowIfFailed(mParticleCounterResetUpload->Map(0, nullptr, reinterpret_cast<void**>(&counters)));
    counters[0] = 0;
    counters[1] = ParticleCount;
    mParticleCounterResetUpload->Unmap(0, nullptr);

    for (UINT i = 0; i < 2; ++i)
    {
        mCmdList->CopyBufferRegion(mParticleCounterBuffers[i].Get(), 0, mParticleCounterResetUpload.Get(), sizeof(UINT), sizeof(UINT));
        auto counterBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
            mParticleCounterBuffers[i].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        mCmdList->ResourceBarrier(1, &counterBarrier);
        mParticleCounterStates[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    mParticleReadBuffer = 0;
    mParticleWriteBuffer = 1;
}

void RenderingSystem::BuildLightingDescriptors()
{
    if (!mGbuffer || !mShadowMap)
        return;

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = Gbuffer::TargetCount + 1;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mLightingSrvHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE dst(mLightingSrvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < Gbuffer::TargetCount; ++i)
    {
        mDevice->CopyDescriptorsSimple(1, dst, mGbuffer->CpuSrv(i), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        dst.Offset(1, mSrvDescriptorSize);
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC shadowSrvDesc = {};
    shadowSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    shadowSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    shadowSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    shadowSrvDesc.Texture2DArray.MostDetailedMip = 0;
    shadowSrvDesc.Texture2DArray.MipLevels = 1;
    shadowSrvDesc.Texture2DArray.FirstArraySlice = 0;
    shadowSrvDesc.Texture2DArray.ArraySize = CascadeCount;
    mDevice->CreateShaderResourceView(mShadowMap.Get(), &shadowSrvDesc, dst);
}

void RenderingSystem::BuildParticleDescriptors()
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 6;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mParticleDescriptorHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(mParticleDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < 2; ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = ParticleCount;
        srvDesc.Buffer.StructureByteStride = sizeof(GpuParticle);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        mDevice->CreateShaderResourceView(mParticleBuffers[i].Get(), &srvDesc, handle);
        handle.Offset(1, mSrvDescriptorSize);
    }

    for (UINT i = 0; i < 2; ++i)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = ParticleCount;
        uavDesc.Buffer.StructureByteStride = sizeof(GpuParticle);
        uavDesc.Buffer.CounterOffsetInBytes = 0;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        mDevice->CreateUnorderedAccessView(mParticleBuffers[i].Get(), mParticleCounterBuffers[i].Get(), &uavDesc, handle);
        handle.Offset(1, mSrvDescriptorSize);
    }

    for (UINT i = 0; i < 2; ++i)
    {
        const UINT bufferIndex = 1 - i;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = ParticleCount;
        uavDesc.Buffer.StructureByteStride = sizeof(GpuParticle);
        uavDesc.Buffer.CounterOffsetInBytes = 0;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        mDevice->CreateUnorderedAccessView(mParticleBuffers[bufferIndex].Get(), mParticleCounterBuffers[bufferIndex].Get(), &uavDesc, handle);
        handle.Offset(1, mSrvDescriptorSize);
    }
}

void RenderingSystem::BuildGeometryRootSignature()
{
    D3D12_DESCRIPTOR_RANGE texTables[3] = {};
    for (UINT i = 0; i < _countof(texTables); ++i)
    {
        texTables[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        texTables[i].NumDescriptors = 1;
        texTables[i].BaseShaderRegister = i;
        texTables[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }

    D3D12_ROOT_PARAMETER rootParams[4] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &texTables[0];
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &texTables[1];
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[3].DescriptorTable.pDescriptorRanges = &texTables[2];
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_ANISOTROPIC,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(_countof(rootParams), rootParams, 1, &sampler,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSig, &errorBlob));
    ThrowIfFailed(mDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mGeometryRootSignature)));
}

void RenderingSystem::BuildShadowRootSignature()
{
    D3D12_ROOT_PARAMETER rootParams[1] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(_countof(rootParams), rootParams, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSig, &errorBlob));
    ThrowIfFailed(mDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mShadowRootSignature)));
}

void RenderingSystem::BuildLightingRootSignature()
{
    D3D12_DESCRIPTOR_RANGE gbufferTable = {};
    gbufferTable.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    gbufferTable.NumDescriptors = Gbuffer::TargetCount;
    gbufferTable.BaseShaderRegister = 0;
    gbufferTable.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE shadowTable = {};
    shadowTable.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    shadowTable.NumDescriptors = 1;
    shadowTable.BaseShaderRegister = 3;
    shadowTable.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[3] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &gbufferTable;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &shadowTable;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_STATIC_SAMPLER_DESC samplers[2] =
    {
        CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
        CD3DX12_STATIC_SAMPLER_DESC(1, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
            D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER,
            0.0f, 16, D3D12_COMPARISON_FUNC_LESS_EQUAL, D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE)
    };

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(_countof(rootParams), rootParams, _countof(samplers), samplers,
        D3D12_ROOT_SIGNATURE_FLAG_NONE);

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSig, &errorBlob));
    ThrowIfFailed(mDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mLightingRootSignature)));
}

void RenderingSystem::BuildParticleRootSignatures()
{
    D3D12_DESCRIPTOR_RANGE computeUavTable = {};
    computeUavTable.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    computeUavTable.NumDescriptors = 2;
    computeUavTable.BaseShaderRegister = 0;
    computeUavTable.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER computeParams[2] = {};
    computeParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    computeParams[0].Descriptor.ShaderRegister = 0;
    computeParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    computeParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    computeParams[1].DescriptorTable.NumDescriptorRanges = 1;
    computeParams[1].DescriptorTable.pDescriptorRanges = &computeUavTable;
    computeParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    CD3DX12_ROOT_SIGNATURE_DESC computeDesc(_countof(computeParams), computeParams, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_NONE);

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    ThrowIfFailed(D3D12SerializeRootSignature(&computeDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSig, &errorBlob));
    ThrowIfFailed(mDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mParticleComputeRootSignature)));

    D3D12_DESCRIPTOR_RANGE renderSrvTable = {};
    renderSrvTable.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    renderSrvTable.NumDescriptors = 1;
    renderSrvTable.BaseShaderRegister = 0;
    renderSrvTable.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER renderParams[2] = {};
    renderParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    renderParams[0].Descriptor.ShaderRegister = 0;
    renderParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    renderParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    renderParams[1].DescriptorTable.NumDescriptorRanges = 1;
    renderParams[1].DescriptorTable.pDescriptorRanges = &renderSrvTable;
    renderParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    CD3DX12_ROOT_SIGNATURE_DESC renderDesc(_countof(renderParams), renderParams, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    serializedRootSig.Reset();
    errorBlob.Reset();
    ThrowIfFailed(D3D12SerializeRootSignature(&renderDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSig, &errorBlob));
    ThrowIfFailed(mDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mParticleRenderRootSignature)));
}

void RenderingSystem::BuildPipelineStates()
{
    ComPtr<ID3DBlob> gVs;
    ComPtr<ID3DBlob> gHs;
    ComPtr<ID3DBlob> gDs;
    ComPtr<ID3DBlob> gPs;
    ComPtr<ID3DBlob> lVs;
    ComPtr<ID3DBlob> lPs;
    ComPtr<ID3DBlob> sVs;
    ComPtr<ID3DBlob> pCs;
    ComPtr<ID3DBlob> pVs;
    ComPtr<ID3DBlob> pGs;
    ComPtr<ID3DBlob> pPs;
    CompileShader(L"Shaders/GBufferVS.hlsl", "VSMain", "vs_5_0", gVs);
    CompileShader(L"Shaders/GBufferHS.hlsl", "HSMain", "hs_5_0", gHs);
    CompileShader(L"Shaders/GBufferDS.hlsl", "DSMain", "ds_5_0", gDs);
    CompileShader(L"Shaders/GBufferPS.hlsl", "PSMain", "ps_5_0", gPs);
    CompileShader(L"Shaders/DeferredLightingVS.hlsl", "VSMain", "vs_5_0", lVs);
    CompileShader(L"Shaders/DeferredLightingPS.hlsl", "PSMain", "ps_5_0", lPs);
    CompileShader(L"Shaders/ShadowVS.hlsl", "VSMain", "vs_5_0", sVs);
    CompileShader(L"Shaders/ParticleSimCS.hlsl", "CSMain", "cs_5_0", pCs);
    CompileShader(L"Shaders/ParticleRender.hlsl", "VSMain", "vs_5_0", pVs);
    CompileShader(L"Shaders/ParticleRender.hlsl", "GSMain", "gs_5_0", pGs);
    CompileShader(L"Shaders/ParticleRender.hlsl", "PSMain", "ps_5_0", pPs);

    D3D12_INPUT_ELEMENT_DESC inputLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC geoDesc = {};
    geoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    geoDesc.pRootSignature = mGeometryRootSignature.Get();
    geoDesc.VS = { gVs->GetBufferPointer(), gVs->GetBufferSize() };
    geoDesc.HS = { gHs->GetBufferPointer(), gHs->GetBufferSize() };
    geoDesc.DS = { gDs->GetBufferPointer(), gDs->GetBufferSize() };
    geoDesc.PS = { gPs->GetBufferPointer(), gPs->GetBufferSize() };
    CD3DX12_RASTERIZER_DESC rast(D3D12_DEFAULT);
    rast.CullMode = D3D12_CULL_MODE_NONE;
    geoDesc.RasterizerState = rast;
    geoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    geoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    geoDesc.SampleMask = UINT_MAX;
    geoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    geoDesc.NumRenderTargets = Gbuffer::TargetCount;
    for (UINT i = 0; i < Gbuffer::TargetCount; ++i)
        geoDesc.RTVFormats[i] = mGbuffer->Format(i);
    geoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    geoDesc.SampleDesc.Count = 1;
    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&geoDesc, IID_PPV_ARGS(&mGeometryPSO)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowDesc = {};
    shadowDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    shadowDesc.pRootSignature = mShadowRootSignature.Get();
    shadowDesc.VS = { sVs->GetBufferPointer(), sVs->GetBufferSize() };
    CD3DX12_RASTERIZER_DESC shadowRasterizer(D3D12_DEFAULT);
    shadowRasterizer.CullMode = D3D12_CULL_MODE_NONE;
    shadowRasterizer.DepthBias = 9000;
    shadowRasterizer.DepthBiasClamp = 0.02f;
    shadowRasterizer.SlopeScaledDepthBias = 1.8f;
    shadowDesc.RasterizerState = shadowRasterizer;
    shadowDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    shadowDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    shadowDesc.SampleMask = UINT_MAX;
    shadowDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    shadowDesc.NumRenderTargets = 0;
    shadowDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    shadowDesc.SampleDesc.Count = 1;
    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&shadowDesc, IID_PPV_ARGS(&mShadowPSO)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC lightDesc = {};
    lightDesc.pRootSignature = mLightingRootSignature.Get();
    lightDesc.VS = { lVs->GetBufferPointer(), lVs->GetBufferSize() };
    lightDesc.PS = { lPs->GetBufferPointer(), lPs->GetBufferSize() };
    CD3DX12_RASTERIZER_DESC lightRasterizer(D3D12_DEFAULT);
    lightRasterizer.CullMode = D3D12_CULL_MODE_NONE;
    lightDesc.RasterizerState = lightRasterizer;
    lightDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    lightDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    lightDesc.DepthStencilState.DepthEnable = FALSE;
    lightDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    lightDesc.SampleMask = UINT_MAX;
    lightDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    lightDesc.NumRenderTargets = 1;
    lightDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    lightDesc.SampleDesc.Count = 1;
    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&lightDesc, IID_PPV_ARGS(&mLightingPSO)));

    D3D12_COMPUTE_PIPELINE_STATE_DESC particleComputeDesc = {};
    particleComputeDesc.pRootSignature = mParticleComputeRootSignature.Get();
    particleComputeDesc.CS = { pCs->GetBufferPointer(), pCs->GetBufferSize() };
    ThrowIfFailed(mDevice->CreateComputePipelineState(&particleComputeDesc, IID_PPV_ARGS(&mParticleComputePSO)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC particleDesc = {};
    particleDesc.InputLayout = { nullptr, 0 };
    particleDesc.pRootSignature = mParticleRenderRootSignature.Get();
    particleDesc.VS = { pVs->GetBufferPointer(), pVs->GetBufferSize() };
    particleDesc.GS = { pGs->GetBufferPointer(), pGs->GetBufferSize() };
    particleDesc.PS = { pPs->GetBufferPointer(), pPs->GetBufferSize() };
    CD3DX12_RASTERIZER_DESC particleRasterizer(D3D12_DEFAULT);
    particleRasterizer.CullMode = D3D12_CULL_MODE_NONE;
    particleDesc.RasterizerState = particleRasterizer;
    particleDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    particleDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    particleDesc.SampleMask = UINT_MAX;
    particleDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    particleDesc.NumRenderTargets = Gbuffer::TargetCount;
    for (UINT i = 0; i < Gbuffer::TargetCount; ++i)
        particleDesc.RTVFormats[i] = mGbuffer->Format(i);
    particleDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    particleDesc.SampleDesc.Count = 1;
    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&particleDesc, IID_PPV_ARGS(&mParticleRenderPSO)));
}

void RenderingSystem::BuildLights()
{
    mLightingConstants.AmbientColor = XMFLOAT4(0.06f, 0.065f, 0.07f, 1.0f);
    mLightingConstants.LightCount = 3.0f;

    auto setDirectional = [&](UINT i, XMFLOAT3 direction, XMFLOAT3 color, float intensity)
        {
            mLightingConstants.Lights[i].PositionRange = XMFLOAT4(0, 0, 0, 0);
            mLightingConstants.Lights[i].DirectionSpot = XMFLOAT4(direction.x, direction.y, direction.z, 0.0f);
            mLightingConstants.Lights[i].ColorIntensity = XMFLOAT4(color.x, color.y, color.z, intensity);
            mLightingConstants.Lights[i].Params = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
        };

    auto setPoint = [&](UINT i, XMFLOAT3 position, float range, XMFLOAT3 color, float intensity)
        {
            mLightingConstants.Lights[i].PositionRange = XMFLOAT4(position.x, position.y, position.z, range);
            mLightingConstants.Lights[i].DirectionSpot = XMFLOAT4(0, 0, 0, 0);
            mLightingConstants.Lights[i].ColorIntensity = XMFLOAT4(color.x, color.y, color.z, intensity);
            mLightingConstants.Lights[i].Params = XMFLOAT4(1.0f, 0.0f, 0.0f, 0.0f);
        };

    auto setSpot = [&](UINT i, XMFLOAT3 position, float range, XMFLOAT3 direction, float spotPower, XMFLOAT3 color, float intensity)
        {
            mLightingConstants.Lights[i].PositionRange = XMFLOAT4(position.x, position.y, position.z, range);
            mLightingConstants.Lights[i].DirectionSpot = XMFLOAT4(direction.x, direction.y, direction.z, spotPower);
            mLightingConstants.Lights[i].ColorIntensity = XMFLOAT4(color.x, color.y, color.z, intensity);
            mLightingConstants.Lights[i].Params = XMFLOAT4(2.0f, 0.0f, 0.0f, 0.0f);
        };

    setDirectional(0, XMFLOAT3(0.35f, -1.0f, 0.25f), XMFLOAT3(1.0f, 0.96f, 0.86f), 0.85f);
    setPoint(1, XMFLOAT3(-65.0f, 25.0f, -35.0f), 130.0f, XMFLOAT3(1.0f, 0.96f, 0.86f), 3.0f);
    setSpot(2, XMFLOAT3(35.0f, 70.0f, -100.0f), 180.0f, XMFLOAT3(-0.2f, -0.75f, 0.6f), 8.0f, XMFLOAT3(1.0f, 0.96f, 0.86f), 4.0f);
}

void RenderingSystem::UpdateLightControls(const InputDevice& input, float dt)
{
    for (UINT i = 0; i <= 2; ++i)
    {
        if (input.WasKeyPressed((uint8_t)('1' + i)))
            mSelectedLight = i;
    }

    DeferredLight& light = mLightingConstants.Lights[mSelectedLight];
    if (input.WasKeyPressed('C'))
    {
        if (mSelectedLight == 0)
        {
            light.DirectionSpot = XMFLOAT4(mCameraForward.x, mCameraForward.y, mCameraForward.z, 0.0f);
        }
        else
        {
            light.PositionRange.x = mCameraPos.x;
            light.PositionRange.y = mCameraPos.y;
            light.PositionRange.z = mCameraPos.z;

            if (light.Params.x > 1.5f)
                light.DirectionSpot = XMFLOAT4(mCameraForward.x, mCameraForward.y, mCameraForward.z, light.DirectionSpot.w);
        }
    }

    if (mSelectedLight == 0)
    {
        XMVECTOR direction = XMLoadFloat4(&light.DirectionSpot);
        XMVECTOR up = XMVectorSet(0, 1, 0, 0);
        XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, direction));
        XMVECTOR dirUp = XMVector3Normalize(XMVector3Cross(direction, right));
        const float turnSpeed = (input.IsKeyDown(VK_SHIFT) ? 1.8f : 0.6f) * dt;

        if (input.IsKeyDown('J')) direction -= right * turnSpeed;
        if (input.IsKeyDown('L')) direction += right * turnSpeed;
        if (input.IsKeyDown('I')) direction += dirUp * turnSpeed;
        if (input.IsKeyDown('K')) direction -= dirUp * turnSpeed;

        direction = XMVector3Normalize(direction);
        XMFLOAT3 d;
        XMStoreFloat3(&d, direction);
        light.DirectionSpot = XMFLOAT4(d.x, d.y, d.z, 0.0f);
    }

    const float moveSpeed = (input.IsKeyDown(VK_SHIFT) ? 120.0f : 40.0f) * dt;
    const float rangeSpeed = (input.IsKeyDown(VK_SHIFT) ? 120.0f : 40.0f) * dt;
    const float intensitySpeed = (input.IsKeyDown(VK_SHIFT) ? 12.0f : 4.0f) * dt;

    if (mSelectedLight != 0)
    {
        if (input.IsKeyDown('J')) light.PositionRange.x -= moveSpeed;
        if (input.IsKeyDown('L')) light.PositionRange.x += moveSpeed;
        if (input.IsKeyDown('I')) light.PositionRange.z += moveSpeed;
        if (input.IsKeyDown('K')) light.PositionRange.z -= moveSpeed;
        if (input.IsKeyDown('U')) light.PositionRange.y += moveSpeed;
        if (input.IsKeyDown('O')) light.PositionRange.y -= moveSpeed;

        if (input.IsKeyDown('R')) light.PositionRange.w += rangeSpeed;
        if (input.IsKeyDown('F')) light.PositionRange.w = (std::max)(10.0f, light.PositionRange.w - rangeSpeed);
    }

    if (input.IsKeyDown('T')) light.ColorIntensity.w += intensitySpeed;
    if (input.IsKeyDown('G')) light.ColorIntensity.w = (std::max)(0.0f, light.ColorIntensity.w - intensitySpeed);
}

void RenderingSystem::UpdateCamera(const InputDevice& input, float dt)
{
    float moveSpeed = 65.0f;
    if (input.IsKeyDown(VK_SHIFT))
        moveSpeed *= 3.0f;
    if (input.IsKeyDown(VK_CONTROL))
        moveSpeed *= 0.25f;

    const float mouseSens = input.IsKeyDown(VK_CONTROL) ? 0.0012f : 0.0032f;
    if (input.IsMouseDown(1))
    {
        POINT md = input.MouseDelta();
        mYaw += md.x * mouseSens;
        mPitch -= md.y * mouseSens;
        const float limit = XM_PIDIV2 - 0.1f;
        mPitch = (std::max)(-limit, (std::min)(limit, mPitch));
    }

    XMVECTOR forward = XMVectorSet(cosf(mPitch) * sinf(mYaw), sinf(mPitch), cosf(mPitch) * cosf(mYaw), 0.0f);
    XMStoreFloat3(&mCameraForward, XMVector3Normalize(forward));
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, forward));
    XMVECTOR pos = XMLoadFloat3(&mCameraPos);

    if (input.IsKeyDown('W')) pos += forward * moveSpeed * dt;
    if (input.IsKeyDown('S')) pos -= forward * moveSpeed * dt;
    if (input.IsKeyDown('A')) pos -= right * moveSpeed * dt;
    if (input.IsKeyDown('D')) pos += right * moveSpeed * dt;
    if (input.IsKeyDown('Q')) pos -= up * moveSpeed * dt;
    if (input.IsKeyDown('E')) pos += up * moveSpeed * dt;

    const float wheelSteps = (float)input.WheelDelta() / 120.0f;
    if (wheelSteps != 0.0f)
        pos += forward * (wheelSteps * moveSpeed * 0.45f);

    XMStoreFloat3(&mCameraPos, pos);
}

void RenderingSystem::UpdateCullingMode(const InputDevice& input)
{
    if (input.WasKeyPressed(VK_F1))
        mFrustumCullingEnabled = !mFrustumCullingEnabled;
    if (input.WasKeyPressed(VK_F2))
        mOctreeCullingEnabled = !mOctreeCullingEnabled;
}

void RenderingSystem::UpdateParticleControls(const InputDevice& input, float dt)
{
    const bool fast = input.IsKeyDown(VK_SHIFT);
    const float speedScale = fast ? 3.0f : 1.0f;
    const float emitterSpeed = 35.0f * speedScale * dt;

    if (input.IsKeyDown(VK_LEFT)) mParticleEmitterPos.x -= emitterSpeed;
    if (input.IsKeyDown(VK_RIGHT)) mParticleEmitterPos.x += emitterSpeed;
    if (input.IsKeyDown(VK_UP)) mParticleEmitterPos.z += emitterSpeed;
    if (input.IsKeyDown(VK_DOWN)) mParticleEmitterPos.z -= emitterSpeed;
    if (input.IsKeyDown(VK_PRIOR)) mParticleEmitterPos.y += emitterSpeed;
    if (input.IsKeyDown(VK_NEXT)) mParticleEmitterPos.y -= emitterSpeed;

    const float sizeStep = 2.0f * speedScale * dt;
    if (input.IsKeyDown('X'))
        mParticleSizeScale = (std::min)(6.0f, mParticleSizeScale + sizeStep);
    if (input.IsKeyDown('Z'))
        mParticleSizeScale = (std::max)(0.2f, mParticleSizeScale - sizeStep);

    const float lifetimeStep = 2.0f * speedScale * dt;
    if (input.IsKeyDown(VK_OEM_6))
        mParticleLifetime = (std::min)(8.0f, mParticleLifetime + lifetimeStep);
    if (input.IsKeyDown(VK_OEM_4))
        mParticleLifetime = (std::max)(0.3f, mParticleLifetime - lifetimeStep);

    const float gravityStep = 16.0f * speedScale * dt;
    if (input.IsKeyDown('N'))
        mParticleGravity = (std::max)(-40.0f, mParticleGravity - gravityStep);
    if (input.IsKeyDown('M'))
        mParticleGravity = (std::min)(20.0f, mParticleGravity + gravityStep);
}

void RenderingSystem::CollectVisibleObjectsLinear(const BoundingFrustum& frustum)
{
    mVisibleObjectIndices.clear();
    mLastTestedBounds = (UINT)mSceneObjects.size();
    mTotalOctreeNodeVisits = 0;

    for (UINT i = 0; i < (UINT)mSceneObjects.size(); ++i)
    {
        if (frustum.Intersects(mSceneObjects[i].Bounds))
            mVisibleObjectIndices.push_back(i);
    }
}

void RenderingSystem::CollectVisibleObjectsOctree(const BoundingFrustum& frustum)
{
    mVisibleObjectIndices.clear();
    mLastTestedBounds = 0;
    mTotalOctreeNodeVisits = 0;
    mOctreeSelfCheckPassed = true;

    auto appendSubtree = [&](auto&& self, const OctreeNode& node) -> void
    {
        mVisibleObjectIndices.insert(
            mVisibleObjectIndices.end(),
            node.ObjectIndices.begin(),
            node.ObjectIndices.end());

        for (const auto& child : node.Children)
        {
            if (child)
                self(self, *child);
        }
    };

    auto visitNode = [&](auto&& self, const OctreeNode& node) -> void
    {
        ++mTotalOctreeNodeVisits;
        const ContainmentType nodeState = frustum.Contains(node.Bounds);
        if (nodeState == DISJOINT)
            return;

        if (nodeState == CONTAINS)
        {
            appendSubtree(appendSubtree, node);
            return;
        }

        for (UINT objectIndex : node.ObjectIndices)
        {
            ++mLastTestedBounds;
            if (frustum.Intersects(mSceneObjects[objectIndex].Bounds))
                mVisibleObjectIndices.push_back(objectIndex);
        }

        for (const auto& child : node.Children)
        {
            if (child)
                self(self, *child);
        }
    };

    if (mOctreeRoot)
        visitNode(visitNode, *mOctreeRoot);

#if defined(_DEBUG)
    std::vector<UINT> linearVisible;
    linearVisible.reserve(mSceneObjects.size());
    for (UINT i = 0; i < (UINT)mSceneObjects.size(); ++i)
    {
        if (frustum.Intersects(mSceneObjects[i].Bounds))
            linearVisible.push_back(i);
    }

    std::vector<UINT> octreeVisible = mVisibleObjectIndices;
    std::sort(linearVisible.begin(), linearVisible.end());
    std::sort(octreeVisible.begin(), octreeVisible.end());
    mOctreeSelfCheckPassed = linearVisible == octreeVisible;
#endif
}

void RenderingSystem::CollectVisibleObjects(const BoundingFrustum& frustum)
{
    if (!mFrustumCullingEnabled)
    {
        mVisibleObjectIndices.clear();
        mVisibleObjectIndices.reserve(mSceneObjects.size());
        for (UINT i = 0; i < (UINT)mSceneObjects.size(); ++i)
            mVisibleObjectIndices.push_back(i);
        mLastTestedBounds = 0;
        mTotalOctreeNodeVisits = 0;
        return;
    }

    if (mOctreeCullingEnabled)
        CollectVisibleObjectsOctree(frustum);
    else
        CollectVisibleObjectsLinear(frustum);
}

void RenderingSystem::Update(float totalTime, float deltaTime, const InputDevice& input)
{
    UpdateCullingMode(input);
    UpdateCamera(input, deltaTime);
    UpdateLightControls(input, deltaTime);
    UpdateParticleControls(input, deltaTime);

    XMVECTOR pos = XMLoadFloat3(&mCameraPos);
    XMVECTOR forward = XMVectorSet(cosf(mPitch) * sinf(mYaw), sinf(mPitch), cosf(mPitch) * cosf(mYaw), 0.0f);
    XMMATRIX view = XMMatrixLookAtLH(pos, pos + forward, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    XMMATRIX proj = XMLoadFloat4x4(&mProj);
    UpdateShadowMatrices(view, proj);

    BoundingFrustum viewFrustum;
    BoundingFrustum::CreateFromMatrix(viewFrustum, proj);
    XMMATRIX invView = XMMatrixInverse(nullptr, view);
    BoundingFrustum worldFrustum;
    viewFrustum.Transform(worldFrustum, invView);
    CollectVisibleObjects(worldFrustum);

    mGeometryConstants.TextureTransform = XMFLOAT4(4.0f, 4.0f, 0.0f, 0.0f);
    mGeometryConstants.EyeDisplacement = XMFLOAT4(mCameraPos.x, mCameraPos.y, mCameraPos.z, 0.35f);
    mGeometryConstants.TessellationParams = XMFLOAT4(2.0f, 1.0f, 55.0f, 330.0f);
    mLightingConstants.EyePosW = mCameraPos;
    XMStoreFloat4x4(&mLightingConstants.View, XMMatrixTranspose(view));

    XMVECTOR right = XMVector3Normalize(XMVector3Cross(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), forward));
    XMVECTOR up = XMVector3Normalize(XMVector3Cross(forward, right));
    XMFLOAT3 rightF;
    XMFLOAT3 upF;
    XMFLOAT3 forwardF;
    XMStoreFloat3(&rightF, right);
    XMStoreFloat3(&upF, up);
    XMStoreFloat3(&forwardF, XMVector3Normalize(forward));

    XMStoreFloat4x4(&mParticleConstants.ViewProj, XMMatrixTranspose(view * proj));
    mParticleConstants.CameraRightSize = XMFLOAT4(rightF.x, rightF.y, rightF.z, mParticleSizeScale);
    mParticleConstants.CameraUp = XMFLOAT4(upF.x, upF.y, upF.z, 0.0f);
    mParticleConstants.CameraForward = XMFLOAT4(forwardF.x, forwardF.y, forwardF.z, 0.0f);
    mParticleConstants.Color = XMFLOAT4(1.0f, 0.42f, 0.08f, 1.0f);

    mParticleSimConstants.EmitterPositionTime = XMFLOAT4(
        mParticleEmitterPos.x,
        mParticleEmitterPos.y,
        mParticleEmitterPos.z,
        totalTime);
    mParticleSimConstants.GravityDeltaTime = XMFLOAT4(0.0f, mParticleGravity, 0.0f, (std::min)(deltaTime, 0.05f));
    mParticleSimConstants.EmitterParams = XMFLOAT4(
        (float)ParticleCount,
        mParticleSpawnChance,
        mParticleLifetime,
        mParticleSpread);
}

void RenderingSystem::UploadGeometryConstants(UINT bufferIndex, const GeometryConstants& constants)
{
    void* mapped = nullptr;
    ThrowIfFailed(mGeometryConstantBuffer->Map(0, nullptr, &mapped));
    auto* dst = reinterpret_cast<uint8_t*>(mapped) + (size_t)bufferIndex * mGeometryConstantByteSize;
    memcpy(dst, &constants, sizeof(GeometryConstants));
    mGeometryConstantBuffer->Unmap(0, nullptr);
}

void RenderingSystem::UploadShadowConstants(UINT bufferIndex, const ShadowConstants& constants)
{
    void* mapped = nullptr;
    ThrowIfFailed(mShadowConstantBuffer->Map(0, nullptr, &mapped));
    auto* dst = reinterpret_cast<uint8_t*>(mapped) + (size_t)bufferIndex * mShadowConstantByteSize;
    memcpy(dst, &constants, sizeof(ShadowConstants));
    mShadowConstantBuffer->Unmap(0, nullptr);
}

void RenderingSystem::UpdateShadowMatrices(const XMMATRIX& view, const XMMATRIX& proj)
{
    const float aspect = mRenderHeight > 0 ? (float)mRenderWidth / (float)mRenderHeight : 1.0f;
    const float tanHalfFovY = tanf(0.125f * XM_PI);
    const float tanHalfFovX = tanHalfFovY * aspect;
    const XMMATRIX invView = XMMatrixInverse(nullptr, view);

    float previousSplit = ShadowNearZ;
    float splitDepths[CascadeCount] = {};
    for (UINT i = 0; i < CascadeCount; ++i)
    {
        const float p = (float)(i + 1) / (float)CascadeCount;
        const float logarithmic = ShadowNearZ * powf(ShadowFarZ / ShadowNearZ, p);
        const float uniform = ShadowNearZ + (ShadowFarZ - ShadowNearZ) * p;
        splitDepths[i] = CascadeSplitLambda * logarithmic + (1.0f - CascadeSplitLambda) * uniform;
    }

    XMVECTOR lightDir = XMVector3Normalize(XMLoadFloat4(&mLightingConstants.Lights[0].DirectionSpot));
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    if (fabsf(XMVectorGetX(XMVector3Dot(lightDir, up))) > 0.95f)
        up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);

    for (UINT cascade = 0; cascade < CascadeCount; ++cascade)
    {
        const float cascadeNear = previousSplit;
        const float cascadeFar = splitDepths[cascade];
        previousSplit = cascadeFar;

        XMVECTOR corners[8] = {};
        UINT cornerIndex = 0;
        for (UINT zIndex = 0; zIndex < 2; ++zIndex)
        {
            const float z = zIndex == 0 ? cascadeNear : cascadeFar;
            const float x = tanHalfFovX * z;
            const float y = tanHalfFovY * z;
            corners[cornerIndex++] = XMVector3TransformCoord(XMVectorSet(-x, -y, z, 1.0f), invView);
            corners[cornerIndex++] = XMVector3TransformCoord(XMVectorSet(-x,  y, z, 1.0f), invView);
            corners[cornerIndex++] = XMVector3TransformCoord(XMVectorSet( x,  y, z, 1.0f), invView);
            corners[cornerIndex++] = XMVector3TransformCoord(XMVectorSet( x, -y, z, 1.0f), invView);
        }

        XMVECTOR center = XMVectorZero();
        for (XMVECTOR corner : corners)
            center += corner;
        center /= 8.0f;

        XMMATRIX lightView = XMMatrixLookAtLH(center - lightDir * ShadowFarZ, center, up);

        XMFLOAT3 minP(FLT_MAX, FLT_MAX, FLT_MAX);
        XMFLOAT3 maxP(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        for (XMVECTOR corner : corners)
        {
            XMFLOAT3 p;
            XMStoreFloat3(&p, XMVector3TransformCoord(corner, lightView));
            minP.x = (std::min)(minP.x, p.x);
            minP.y = (std::min)(minP.y, p.y);
            minP.z = (std::min)(minP.z, p.z);
            maxP.x = (std::max)(maxP.x, p.x);
            maxP.y = (std::max)(maxP.y, p.y);
            maxP.z = (std::max)(maxP.z, p.z);
        }

        const float zPadding = 240.0f;
        XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(
            minP.x, maxP.x,
            minP.y, maxP.y,
            (std::max)(0.01f, minP.z - zPadding),
            maxP.z + zPadding);

        XMMATRIX shadowViewProj = lightView * lightProj;
        XMStoreFloat4x4(&mLightingConstants.ShadowViewProj[cascade], XMMatrixTranspose(shadowViewProj));
    }

    mLightingConstants.CascadeSplits = XMFLOAT4(splitDepths[0], splitDepths[1], splitDepths[2], splitDepths[3]);
    mLightingConstants.ShadowTexelSizeBias = XMFLOAT4(1.0f / (float)ShadowMapSize, 0.0007f, 0.0018f, 0.045f);
}

void RenderingSystem::UploadParticleConstants()
{
    void* mapped = nullptr;
    ThrowIfFailed(mParticleConstantBuffer->Map(0, nullptr, &mapped));
    memcpy(mapped, &mParticleConstants, sizeof(ParticleConstants));
    mParticleConstantBuffer->Unmap(0, nullptr);
}

void RenderingSystem::UploadParticleSimConstants(float, float)
{
    void* mapped = nullptr;
    ThrowIfFailed(mParticleSimConstantBuffer->Map(0, nullptr, &mapped));
    memcpy(mapped, &mParticleSimConstants, sizeof(ParticleSimConstants));
    mParticleSimConstantBuffer->Unmap(0, nullptr);
}

std::wstring RenderingSystem::StatusText() const
{
    std::wostringstream text;
    text << std::fixed << std::setprecision(2);
    text << L"CG lab6 | CSM+PCF | GPU particles " << ParticleCount
        << L" | emitter (" << mParticleEmitterPos.x << L"," << mParticleEmitterPos.y << L"," << mParticleEmitterPos.z << L")"
        << L" | spawn " << mParticleSpawnChance
        << L" | size " << mParticleSizeScale
        << L" | life " << mParticleLifetime
        << L" | grav " << mParticleGravity
        << L" | objects " << mVisibleObjectIndices.size() << L"/" << mSceneObjects.size()
        << L" | F1 culling " << (mFrustumCullingEnabled ? L"ON" : L"OFF")
        << L" | F2 octree " << (mOctreeCullingEnabled ? L"ON" : L"OFF");

    if (mFrustumCullingEnabled)
    {
        text << L" | object tests " << mLastTestedBounds << L"/" << mSceneObjects.size();
        if (mOctreeCullingEnabled)
        {
            text << L" | node tests " << mTotalOctreeNodeVisits;
#if defined(_DEBUG)
            text << L" | octree " << (mOctreeSelfCheckPassed ? L"OK" : L"MISMATCH");
#endif
        }
    }

    return text.str();
}

void RenderingSystem::DrawShadowMap(ID3D12GraphicsCommandList* cmdList)
{
    if (mShadowMapState != D3D12_RESOURCE_STATE_DEPTH_WRITE)
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            mShadowMap.Get(), mShadowMapState, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmdList->ResourceBarrier(1, &barrier);
        mShadowMapState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    cmdList->RSSetViewports(1, &mShadowViewport);
    cmdList->RSSetScissorRects(1, &mShadowScissorRect);
    cmdList->SetPipelineState(mShadowPSO.Get());
    cmdList->SetGraphicsRootSignature(mShadowRootSignature.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &mVBV);
    cmdList->IASetIndexBuffer(&mIBV);

    const UINT dsvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    UINT drawConstantIndex = 0;
    for (UINT cascade = 0; cascade < CascadeCount; ++cascade)
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(mShadowDsvHeap->GetCPUDescriptorHandleForHeapStart(), cascade, dsvDescriptorSize);
        cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
        cmdList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);

        for (UINT i = 0; i < (UINT)mRenderSubsets.size(); ++i)
        {
            const auto& subset = mRenderSubsets[i];
            for (UINT objectIndex = 0; objectIndex < (UINT)mSceneObjects.size(); ++objectIndex)
            {
                ShadowConstants constants = {};
                XMMATRIX world = XMLoadFloat4x4(&mSceneObjects[objectIndex].World);
                XMMATRIX shadowViewProj = XMMatrixTranspose(XMLoadFloat4x4(&mLightingConstants.ShadowViewProj[cascade]));
                XMStoreFloat4x4(&constants.WorldViewProj, XMMatrixTranspose(world * shadowViewProj));

                UploadShadowConstants(drawConstantIndex, constants);
                D3D12_GPU_VIRTUAL_ADDRESS cbAddress =
                    mShadowConstantBuffer->GetGPUVirtualAddress() + (UINT64)drawConstantIndex * mShadowConstantByteSize;
                cmdList->SetGraphicsRootConstantBufferView(0, cbAddress);
                cmdList->DrawIndexedInstanced(subset.IndexCount, 1, subset.IndexStart, 0, 0);
                ++drawConstantIndex;
            }
        }
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        mShadowMap.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);
    mShadowMapState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void RenderingSystem::UploadLightingConstants()
{
    void* mapped = nullptr;
    ThrowIfFailed(mLightingConstantBuffer->Map(0, nullptr, &mapped));
    memcpy(mapped, &mLightingConstants, sizeof(LightingConstants));
    mLightingConstantBuffer->Unmap(0, nullptr);
}

void RenderingSystem::SimulateParticles(ID3D12GraphicsCommandList* cmdList)
{
    const UINT read = mParticleReadBuffer;
    const UINT write = mParticleWriteBuffer;

    if (mParticleBufferStates[read] != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            mParticleBuffers[read].Get(), mParticleBufferStates[read], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->ResourceBarrier(1, &barrier);
        mParticleBufferStates[read] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (mParticleBufferStates[write] != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            mParticleBuffers[write].Get(), mParticleBufferStates[write], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->ResourceBarrier(1, &barrier);
        mParticleBufferStates[write] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (mParticleCounterStates[write] != D3D12_RESOURCE_STATE_COPY_DEST)
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            mParticleCounterBuffers[write].Get(), mParticleCounterStates[write], D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->ResourceBarrier(1, &barrier);
        mParticleCounterStates[write] = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    cmdList->CopyBufferRegion(mParticleCounterBuffers[write].Get(), 0, mParticleCounterResetUpload.Get(), 0, sizeof(UINT));

    auto counterToUav = CD3DX12_RESOURCE_BARRIER::Transition(
        mParticleCounterBuffers[write].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->ResourceBarrier(1, &counterToUav);
    mParticleCounterStates[write] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    D3D12_RESOURCE_BARRIER beforeDispatchBarriers[2] =
    {
        CD3DX12_RESOURCE_BARRIER::UAV(mParticleBuffers[read].Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(mParticleBuffers[write].Get())
    };
    cmdList->ResourceBarrier(_countof(beforeDispatchBarriers), beforeDispatchBarriers);

    UploadParticleSimConstants(0.0f, 0.0f);
    cmdList->SetPipelineState(mParticleComputePSO.Get());
    cmdList->SetComputeRootSignature(mParticleComputeRootSignature.Get());
    ID3D12DescriptorHeap* heaps[] = { mParticleDescriptorHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetComputeRootConstantBufferView(0, mParticleSimConstantBuffer->GetGPUVirtualAddress());
    const UINT uavTableOffset = read == 0 ? 2 : 4;
    CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandle(mParticleDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), uavTableOffset, mSrvDescriptorSize);
    cmdList->SetComputeRootDescriptorTable(1, uavHandle);
    cmdList->Dispatch((ParticleCount + 255) / 256, 1, 1);

    D3D12_RESOURCE_BARRIER afterDispatchBarriers[2] =
    {
        CD3DX12_RESOURCE_BARRIER::UAV(mParticleBuffers[read].Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(mParticleBuffers[write].Get())
    };
    cmdList->ResourceBarrier(_countof(afterDispatchBarriers), afterDispatchBarriers);

    auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
        mParticleBuffers[write].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &toSrv);
    mParticleBufferStates[write] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    mParticleReadBuffer = write;
    mParticleWriteBuffer = read;
}

void RenderingSystem::DrawParticles(ID3D12GraphicsCommandList* cmdList)
{
    UploadParticleConstants();

    cmdList->SetPipelineState(mParticleRenderPSO.Get());
    cmdList->SetGraphicsRootSignature(mParticleRenderRootSignature.Get());
    ID3D12DescriptorHeap* heaps[] = { mParticleDescriptorHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetGraphicsRootConstantBufferView(0, mParticleConstantBuffer->GetGPUVirtualAddress());
    CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(mParticleDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), mParticleReadBuffer, mSrvDescriptorSize);
    cmdList->SetGraphicsRootDescriptorTable(1, srvHandle);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    cmdList->IASetVertexBuffers(0, 0, nullptr);
    cmdList->IASetIndexBuffer(nullptr);
    cmdList->DrawInstanced(ParticleCount, 1, 0, 0);
}

void RenderingSystem::Draw(ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* backBuffer,
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferView,
    D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView)
{
    DrawShadowMap(cmdList);
    SimulateParticles(cmdList);

    D3D12_VIEWPORT screenViewport = {};
    screenViewport.TopLeftX = 0.0f;
    screenViewport.TopLeftY = 0.0f;
    screenViewport.Width = (float)mRenderWidth;
    screenViewport.Height = (float)mRenderHeight;
    screenViewport.MinDepth = 0.0f;
    screenViewport.MaxDepth = 1.0f;
    D3D12_RECT screenScissorRect = { 0, 0, (LONG)mRenderWidth, (LONG)mRenderHeight };
    cmdList->RSSetViewports(1, &screenViewport);
    cmdList->RSSetScissorRects(1, &screenScissorRect);

    mGbuffer->TransitionToRenderTargets(cmdList);
    mGbuffer->Clear(cmdList);
    cmdList->ClearDepthStencilView(depthStencilView, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[Gbuffer::TargetCount] =
    {
        mGbuffer->Rtv(0),
        mGbuffer->Rtv(1),
        mGbuffer->Rtv(2)
    };

    cmdList->SetPipelineState(mGeometryPSO.Get());
    cmdList->SetGraphicsRootSignature(mGeometryRootSignature.Get());
    cmdList->OMSetRenderTargets(Gbuffer::TargetCount, rtvs, FALSE, &depthStencilView);

    ID3D12DescriptorHeap* textureHeaps[] = { mTextureHeap.Get() };
    cmdList->SetDescriptorHeaps(1, textureHeaps);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    cmdList->IASetVertexBuffers(0, 1, &mVBV);
    cmdList->IASetIndexBuffer(&mIBV);

    XMVECTOR pos = XMLoadFloat3(&mCameraPos);
    XMVECTOR forward = XMVectorSet(cosf(mPitch) * sinf(mYaw), sinf(mPitch), cosf(mPitch) * cosf(mYaw), 0.0f);
    XMMATRIX view = XMMatrixLookAtLH(pos, pos + forward, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    XMMATRIX proj = XMLoadFloat4x4(&mProj);
    UINT drawConstantIndex = 0;

    for (UINT i = 0; i < (UINT)mRenderSubsets.size(); ++i)
    {
        const auto& subset = mRenderSubsets[i];
        const RenderMaterial& material = mRenderMaterials[subset.MaterialIndex];

        CD3DX12_GPU_DESCRIPTOR_HANDLE textureHandle(mTextureHeap->GetGPUDescriptorHandleForHeapStart());
        textureHandle.Offset(material.TextureIndex, mSrvDescriptorSize);
        CD3DX12_GPU_DESCRIPTOR_HANDLE normalTextureHandle(mTextureHeap->GetGPUDescriptorHandleForHeapStart());
        normalTextureHandle.Offset(material.NormalTextureIndex, mSrvDescriptorSize);
        CD3DX12_GPU_DESCRIPTOR_HANDLE displacementTextureHandle(mTextureHeap->GetGPUDescriptorHandleForHeapStart());
        displacementTextureHandle.Offset(material.DisplacementTextureIndex, mSrvDescriptorSize);
        cmdList->SetGraphicsRootDescriptorTable(1, textureHandle);
        cmdList->SetGraphicsRootDescriptorTable(2, normalTextureHandle);
        cmdList->SetGraphicsRootDescriptorTable(3, displacementTextureHandle);

        for (UINT objectIndex : mVisibleObjectIndices)
        {
            GeometryConstants constants = mGeometryConstants;
            XMMATRIX world = XMLoadFloat4x4(&mSceneObjects[objectIndex].World);
            XMMATRIX wvp = world * view * proj;

            XMStoreFloat4x4(&constants.WorldViewProj, XMMatrixTranspose(wvp));
            XMStoreFloat4x4(&constants.World, XMMatrixTranspose(world));
            constants.DiffuseColor = material.Diffuse;
            constants.SpecularColor = material.Specular;
            constants.MaterialParams = XMFLOAT4(material.Shininess, 0.0f, 0.0f, 0.0f);

            UploadGeometryConstants(drawConstantIndex, constants);
            D3D12_GPU_VIRTUAL_ADDRESS cbAddress =
                mGeometryConstantBuffer->GetGPUVirtualAddress() + (UINT64)drawConstantIndex * mGeometryConstantByteSize;
            cmdList->SetGraphicsRootConstantBufferView(0, cbAddress);
            cmdList->DrawIndexedInstanced(subset.IndexCount, 1, subset.IndexStart, 0, 0);
            ++drawConstantIndex;
        }
    }

    DrawParticles(cmdList);

    mGbuffer->TransitionToShaderResources(cmdList);

    auto toRenderTarget = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->ResourceBarrier(1, &toRenderTarget);

    const float clearColor[] = { 0.05f, 0.06f, 0.075f, 1.0f };
    cmdList->ClearRenderTargetView(backBufferView, clearColor, 0, nullptr);
    cmdList->OMSetRenderTargets(1, &backBufferView, TRUE, nullptr);

    UploadLightingConstants();
    cmdList->SetPipelineState(mLightingPSO.Get());
    cmdList->SetGraphicsRootSignature(mLightingRootSignature.Get());

    ID3D12DescriptorHeap* lightingHeaps[] = { mLightingSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, lightingHeaps);
    cmdList->SetGraphicsRootConstantBufferView(0, mLightingConstantBuffer->GetGPUVirtualAddress());
    cmdList->SetGraphicsRootDescriptorTable(1, mLightingSrvHeap->GetGPUDescriptorHandleForHeapStart());
    CD3DX12_GPU_DESCRIPTOR_HANDLE shadowHandle(mLightingSrvHeap->GetGPUDescriptorHandleForHeapStart(), Gbuffer::TargetCount, mSrvDescriptorSize);
    cmdList->SetGraphicsRootDescriptorTable(2, shadowHandle);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 0, nullptr);
    cmdList->IASetIndexBuffer(nullptr);
    cmdList->DrawInstanced(3, 1, 0, 0);

    auto toPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    cmdList->ResourceBarrier(1, &toPresent);
}
