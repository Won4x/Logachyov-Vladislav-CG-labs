#include "RenderingSystem.h"
#include <algorithm>
#include <cstdio>
#include <cwctype>

namespace
{
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
            texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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
}

void RenderingSystem::BuildResources(UINT width, UINT height)
{
    mGbuffer = std::make_unique<Gbuffer>(mDevice, mRtvDescriptorSize, mSrvDescriptorSize);
    mGbuffer->Resize(width, height);
    OnResize(width, height);

    BuildModelGeometry();
    FitModelToView();
    BuildTextureResources();
    BuildConstantBuffers();
    BuildGeometryRootSignature();
    BuildLightingRootSignature();
    BuildPipelineStates();
    BuildLights();
}

void RenderingSystem::OnResize(UINT width, UINT height)
{
    if (mGbuffer)
        mGbuffer->Resize(width, height);

    const float aspect = height > 0 ? (float)width / (float)height : 1.0f;
    XMStoreFloat4x4(&mProj, XMMatrixPerspectiveFovLH(0.25f * XM_PI, aspect, 0.1f, 5000.0f));
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
            { XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(-1.0f,  1.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3( 1.0f,  1.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3( 1.0f, -1.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT2(1.0f, 1.0f) },
        };
        mMeshData.Indices = { 0, 1, 2, 0, 2, 3 };
        ObjMaterialData material;
        material.Name = "Default";
        mMeshData.Materials = { material };
        mMeshData.Subsets = { ObjSubset{ "Default", 0, 6 } };
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

    const float maxSize = (std::max)(maxP.x - minP.x, (std::max)(maxP.y - minP.y, maxP.z - minP.z));
    mModelScale = maxSize > 0.0001f ? 250.0f / maxSize : 1.0f;
}

void RenderingSystem::CreateFallbackTexture()
{
    const UINT width = 4;
    const UINT height = 4;
    uint32_t pixels[width * height] = {};
    for (UINT y = 0; y < height; ++y)
    {
        for (UINT x = 0; x < width; ++x)
            pixels[y * width + x] = ((x + y) % 2) == 0 ? 0xffffffffu : 0xff707070u;
    }

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
        texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mCmdList->ResourceBarrier(1, &barrier);

    mTextures.push_back(texture);
    mTextureUploads.push_back(upload);
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

        if (!src.DiffuseTexture.empty() && FileExists(src.DiffuseTexture))
        {
            auto found = textureLookup.find(src.DiffuseTexture);
            if (found != textureLookup.end())
            {
                dst.TextureIndex = found->second;
            }
            else
            {
                ComPtr<ID3D12Resource> texture;
                ComPtr<ID3D12Resource> upload;
                if (LoadTextureFromFile(mDevice, mCmdList, src.DiffuseTexture, texture, upload))
                {
                    dst.TextureIndex = (UINT)mTextures.size();
                    textureLookup[src.DiffuseTexture] = dst.TextureIndex;
                    mTextures.push_back(texture);
                    mTextureUploads.push_back(upload);
                }
            }
        }
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
        mRenderSubsets.push_back(RenderSubset{ 0, mIndexCount, 0 });

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

    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto geoDesc = CD3DX12_RESOURCE_DESC::Buffer(mGeometryConstantByteSize * (UINT)mRenderSubsets.size());
    auto lightDesc = CD3DX12_RESOURCE_DESC::Buffer(mLightingConstantByteSize);

    ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &geoDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mGeometryConstantBuffer)));
    ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &lightDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mLightingConstantBuffer)));
}

void RenderingSystem::BuildGeometryRootSignature()
{
    D3D12_DESCRIPTOR_RANGE texTable = {};
    texTable.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    texTable.NumDescriptors = 1;
    texTable.BaseShaderRegister = 0;
    texTable.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[2] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &texTable;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

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

void RenderingSystem::BuildLightingRootSignature()
{
    D3D12_DESCRIPTOR_RANGE gbufferTable = {};
    gbufferTable.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    gbufferTable.NumDescriptors = Gbuffer::TargetCount;
    gbufferTable.BaseShaderRegister = 0;
    gbufferTable.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[2] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &gbufferTable;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(_countof(rootParams), rootParams, 1, &sampler,
        D3D12_ROOT_SIGNATURE_FLAG_NONE);

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSig, &errorBlob));
    ThrowIfFailed(mDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mLightingRootSignature)));
}

void RenderingSystem::BuildPipelineStates()
{
    ComPtr<ID3DBlob> gVs;
    ComPtr<ID3DBlob> gPs;
    ComPtr<ID3DBlob> lVs;
    ComPtr<ID3DBlob> lPs;
    CompileShader(L"Shaders/GBufferVS.hlsl", "VSMain", "vs_5_0", gVs);
    CompileShader(L"Shaders/GBufferPS.hlsl", "PSMain", "ps_5_0", gPs);
    CompileShader(L"Shaders/DeferredLightingVS.hlsl", "VSMain", "vs_5_0", lVs);
    CompileShader(L"Shaders/DeferredLightingPS.hlsl", "PSMain", "ps_5_0", lPs);

    D3D12_INPUT_ELEMENT_DESC inputLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC geoDesc = {};
    geoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    geoDesc.pRootSignature = mGeometryRootSignature.Get();
    geoDesc.VS = { gVs->GetBufferPointer(), gVs->GetBufferSize() };
    geoDesc.PS = { gPs->GetBufferPointer(), gPs->GetBufferSize() };
    CD3DX12_RASTERIZER_DESC rast(D3D12_DEFAULT);
    rast.CullMode = D3D12_CULL_MODE_NONE;
    geoDesc.RasterizerState = rast;
    geoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    geoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    geoDesc.SampleMask = UINT_MAX;
    geoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    geoDesc.NumRenderTargets = Gbuffer::TargetCount;
    for (UINT i = 0; i < Gbuffer::TargetCount; ++i)
        geoDesc.RTVFormats[i] = mGbuffer->Format(i);
    geoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    geoDesc.SampleDesc.Count = 1;
    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&geoDesc, IID_PPV_ARGS(&mGeometryPSO)));

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

    setDirectional(0, XMFLOAT3(0.35f, -1.0f, 0.25f), XMFLOAT3(1.0f, 0.96f, 0.86f), 0.55f);
    setPoint(1, XMFLOAT3(-65.0f, 25.0f, -35.0f), 130.0f, XMFLOAT3(1.0f, 0.96f, 0.86f), 12.0f);
    setSpot(2, XMFLOAT3(35.0f, 70.0f, -100.0f), 180.0f, XMFLOAT3(-0.2f, -0.75f, 0.6f), 8.0f, XMFLOAT3(1.0f, 0.96f, 0.86f), 16.0f);
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
    float moveSpeed = 90.0f;
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

void RenderingSystem::Update(float, float deltaTime, const InputDevice& input)
{
    UpdateCamera(input, deltaTime);
    UpdateLightControls(input, deltaTime);

    XMMATRIX world =
        XMMatrixTranslation(-mModelCenter.x, -mModelCenter.y, -mModelCenter.z) *
        XMMatrixScaling(mModelScale, mModelScale, mModelScale);

    XMVECTOR pos = XMLoadFloat3(&mCameraPos);
    XMVECTOR forward = XMVectorSet(cosf(mPitch) * sinf(mYaw), sinf(mPitch), cosf(mPitch) * cosf(mYaw), 0.0f);
    XMMATRIX view = XMMatrixLookAtLH(pos, pos + forward, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    XMMATRIX proj = XMLoadFloat4x4(&mProj);
    XMMATRIX wvp = world * view * proj;

    XMStoreFloat4x4(&mGeometryConstants.WorldViewProj, XMMatrixTranspose(wvp));
    XMStoreFloat4x4(&mGeometryConstants.World, XMMatrixTranspose(world));
    mGeometryConstants.TextureScaleX = 4.0f;
    mGeometryConstants.TextureScaleY = 4.0f;
    mGeometryConstants.TextureOffsetX = 0.0f;
    mGeometryConstants.TextureOffsetY = 0.0f;
    mLightingConstants.EyePosW = mCameraPos;
}

void RenderingSystem::UploadGeometryConstants(UINT bufferIndex)
{
    void* mapped = nullptr;
    ThrowIfFailed(mGeometryConstantBuffer->Map(0, nullptr, &mapped));
    auto* dst = reinterpret_cast<uint8_t*>(mapped) + (size_t)bufferIndex * mGeometryConstantByteSize;
    memcpy(dst, &mGeometryConstants, sizeof(GeometryConstants));
    mGeometryConstantBuffer->Unmap(0, nullptr);
}

void RenderingSystem::UploadLightingConstants()
{
    void* mapped = nullptr;
    ThrowIfFailed(mLightingConstantBuffer->Map(0, nullptr, &mapped));
    memcpy(mapped, &mLightingConstants, sizeof(LightingConstants));
    mLightingConstantBuffer->Unmap(0, nullptr);
}

void RenderingSystem::Draw(ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* backBuffer,
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferView,
    D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView)
{
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
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &mVBV);
    cmdList->IASetIndexBuffer(&mIBV);

    for (UINT i = 0; i < (UINT)mRenderSubsets.size(); ++i)
    {
        const auto& subset = mRenderSubsets[i];
        const RenderMaterial& material = mRenderMaterials[subset.MaterialIndex];
        mGeometryConstants.DiffuseColor = material.Diffuse;
        mGeometryConstants.SpecularColor = material.Specular;
        mGeometryConstants.Shininess = material.Shininess;
        UploadGeometryConstants(i);

        CD3DX12_GPU_DESCRIPTOR_HANDLE textureHandle(mTextureHeap->GetGPUDescriptorHandleForHeapStart());
        textureHandle.Offset(material.TextureIndex, mSrvDescriptorSize);
        D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mGeometryConstantBuffer->GetGPUVirtualAddress() + (UINT64)i * mGeometryConstantByteSize;
        cmdList->SetGraphicsRootConstantBufferView(0, cbAddress);
        cmdList->SetGraphicsRootDescriptorTable(1, textureHandle);
        cmdList->DrawIndexedInstanced(subset.IndexCount, 1, subset.IndexStart, 0, 0);
    }

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

    ID3D12DescriptorHeap* gbufferHeaps[] = { mGbuffer->SrvHeap() };
    cmdList->SetDescriptorHeaps(1, gbufferHeaps);
    cmdList->SetGraphicsRootConstantBufferView(0, mLightingConstantBuffer->GetGPUVirtualAddress());
    cmdList->SetGraphicsRootDescriptorTable(1, mGbuffer->Srv(0));
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 0, nullptr);
    cmdList->IASetIndexBuffer(nullptr);
    cmdList->DrawInstanced(3, 1, 0, 0);

    auto toPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    cmdList->ResourceBarrier(1, &toPresent);
}
