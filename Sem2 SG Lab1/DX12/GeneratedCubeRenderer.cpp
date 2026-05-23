// GeneratedCubeRenderer.cpp
#include "GeneratedCubeRenderer.h"
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

    DXGI_FORMAT WicToDxgiFormat(WICPixelFormatGUID format)
    {
        if (format == GUID_WICPixelFormat32bppRGBA) return DXGI_FORMAT_R8G8B8A8_UNORM;
        if (format == GUID_WICPixelFormat32bppBGRA) return DXGI_FORMAT_B8G8R8A8_UNORM;
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    std::wstring FindExistingModelPath()
    {
        // Model path candidates. Add a new OBJ path here if you want another model.
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

        static_assert(sizeof(TgaHeader) == 18, "Unexpected TGA header size");

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
        if ((!isColor && !isGray) || header.ColorMapType != 0)
        {
            fclose(file);
            return false;
        }

        const UINT bytesPerPixel = header.PixelDepth / 8;
        if ((isColor && bytesPerPixel != 3 && bytesPerPixel != 4) || (isGray && bytesPerPixel != 1))
        {
            fclose(file);
            return false;
        }

        if (fseek(file, header.IdLength, SEEK_CUR) != 0)
        {
            fclose(file);
            return false;
        }

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
                    d[0] = s[0];
                    d[1] = s[0];
                    d[2] = s[0];
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
}

CubeRenderer::CubeRenderer(ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    UINT cbvSrvUavDescriptorSize)
    : mDevice(device)
    , mCmdList(cmdList)
    , mCbvSrvUavDescriptorSize(cbvSrvUavDescriptorSize)
{
    float aspect = 1280.0f / 720.0f;
    // Camera lens: FOV, aspect ratio, near plane, far plane.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * XM_PI, aspect, 0.1f, 5000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void CubeRenderer::BuildResources()
{
    BuildModelGeometry();
    FitModelToView();
    BuildTextureResources();
    BuildConstantBuffer();
    BuildRootSignature();
    BuildPSO();
}

void CubeRenderer::FitModelToView()
{
    if (mMeshData.Vertices.empty())
        return;

    XMFLOAT3 minP = mMeshData.Vertices[0].Pos;
    XMFLOAT3 maxP = mMeshData.Vertices[0].Pos;

    for (size_t i = 1; i < mMeshData.Vertices.size(); ++i)
    {
        const XMFLOAT3& p = mMeshData.Vertices[i].Pos;
        minP.x = (std::min)(minP.x, p.x);
        minP.y = (std::min)(minP.y, p.y);
        minP.z = (std::min)(minP.z, p.z);
        maxP.x = (std::max)(maxP.x, p.x);
        maxP.y = (std::max)(maxP.y, p.y);
        maxP.z = (std::max)(maxP.z, p.z);
    }

    mModelCenter = XMFLOAT3(
        0.5f * (minP.x + maxP.x),
        0.5f * (minP.y + maxP.y),
        0.5f * (minP.z + maxP.z));

    const float sizeX = maxP.x - minP.x;
    const float sizeY = maxP.y - minP.y;
    const float sizeZ = maxP.z - minP.z;
    const float maxSize = (std::max)(sizeX, (std::max)(sizeY, sizeZ));

    // Final model size on screen. Increase 10.0f to make imported OBJ bigger.
    mModelScale = maxSize > 0.0001f ? 150.0f / maxSize : 1.0f;
}

void CubeRenderer::BuildModelGeometry()
{
    const std::wstring modelPath = FindExistingModelPath();
    if (!modelPath.empty())
        ObjLoader::LoadObjPosNormal(modelPath, mMeshData, true);

    if (mMeshData.Vertices.empty() || mMeshData.Indices.empty())
    {
        // Fallback quad used when the OBJ file cannot be found or parsed.
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
        material.Diffuse = XMFLOAT4(0.85f, 0.85f, 0.85f, 1.0f);
        mMeshData.Materials = { material };
        mMeshData.Subsets = { ObjSubset{ "Default", 0, 6 } };
    }

    mIndexCount = (UINT)mMeshData.Indices.size();

    const UINT vBufferSize = (UINT)(mMeshData.Vertices.size() * sizeof(VertexPosNormal));
    const UINT iBufferSize = (UINT)(mMeshData.Indices.size() * sizeof(uint32_t));

    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vBufferSize);
    CD3DX12_RESOURCE_DESC ibDesc = CD3DX12_RESOURCE_DESC::Buffer(iBufferSize);

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mVertexBuffer)));
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mVBUpload)));

    void* mapped = nullptr;
    ThrowIfFailed(mVBUpload->Map(0, nullptr, &mapped));
    memcpy(mapped, mMeshData.Vertices.data(), vBufferSize);
    mVBUpload->Unmap(0, nullptr);

    mCmdList->CopyBufferRegion(mVertexBuffer.Get(), 0, mVBUpload.Get(), 0, vBufferSize);
    auto vbBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        mVertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    mCmdList->ResourceBarrier(1, &vbBarrier);

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &ibDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mIndexBuffer)));
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &ibDesc,
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

void CubeRenderer::CreateFallbackTexture()
{
    // Default checker texture used when a material has no valid map_Kd file.
    const UINT width = 4;
    const UINT height = 4;
    uint32_t pixels[width * height] = {};
    for (UINT y = 0; y < height; ++y)
    {
        for (UINT x = 0; x < width; ++x)
        {
            const bool bright = ((x + y) % 2) == 0;
            pixels[y * width + x] = bright ? 0xffffffffu : 0xff707070u;
        }
    }

    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height);
    ComPtr<ID3D12Resource> texture;
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture)));

    UINT64 uploadSize = GetRequiredIntermediateSize(texture.Get(), 0, 1);
    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
    ComPtr<ID3D12Resource> upload;
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadDesc,
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

bool LoadTextureFromFile(ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const std::wstring& filename,
    ComPtr<ID3D12Resource>& texture,
    ComPtr<ID3D12Resource>& upload)
{
    UINT width = 0;
    UINT height = 0;
    std::vector<uint8_t> pixels;
    DXGI_FORMAT dxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    if (HasExtension(filename, L".tga"))
    {
        if (!LoadTgaPixels(filename, width, height, pixels))
            return false;
    }
    else
    {
        HRESULT initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE)
            return false;

        ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr))
            return false;

        ComPtr<IWICBitmapDecoder> decoder;
        hr = factory->CreateDecoderFromFilename(filename.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
        if (FAILED(hr))
            return false;

        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, &frame)))
            return false;

        frame->GetSize(&width, &height);

        WICPixelFormatGUID srcFormat;
        frame->GetPixelFormat(&srcFormat);

        ComPtr<IWICBitmapSource> bitmapSource = frame;
        dxgiFormat = WicToDxgiFormat(srcFormat);
        if (srcFormat != GUID_WICPixelFormat32bppRGBA)
        {
            ComPtr<IWICFormatConverter> converter;
            if (FAILED(factory->CreateFormatConverter(&converter)))
                return false;

            if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
                return false;

            bitmapSource = converter;
            dxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        }

        pixels.resize((size_t)width * height * 4);
        if (FAILED(bitmapSource->CopyPixels(nullptr, width * 4, (UINT)pixels.size(), pixels.data())))
            return false;
    }

    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(dxgiFormat, width, height);
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

void CubeRenderer::BuildTextureResources()
{
    CreateFallbackTexture();

    // Load every unique diffuse texture once and reuse it between materials.
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
        // OBJ usemtl ranges become draw subsets with their own material.
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
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(mSrvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < (UINT)mTextures.size(); ++i)
    {
        auto desc = mTextures[i]->GetDesc();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        mDevice->CreateShaderResourceView(mTextures[i].Get(), &srvDesc, handle);
        handle.Offset(1, mCbvSrvUavDescriptorSize);
    }
}

void CubeRenderer::BuildConstantBuffer()
{
    // DirectX 12 CBVs must start at 256-byte aligned addresses.
    mConstantBufferByteSize = (sizeof(ObjectConstants) + 255) & ~255;

    // One constant-buffer slot per subset keeps materials from overwriting each other.
    const UINT bufferCount = (std::max)(1u, (UINT)mRenderSubsets.size());
    const UINT bufferSize = mConstantBufferByteSize * bufferCount;
    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC cbDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &cbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mConstantUploadBuffer)));

    mConstantBuffer = mConstantUploadBuffer;
}

void CubeRenderer::BuildRootSignature()
{
    D3D12_DESCRIPTOR_RANGE texTable = {};
    texTable.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    texTable.NumDescriptors = 1;
    texTable.BaseShaderRegister = 0;
    texTable.RegisterSpace = 0;
    texTable.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[2] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &texTable;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_STATIC_SAMPLER_DESC sampler(
        0,
        D3D12_FILTER_ANISOTROPIC,
        // WRAP enables texture tiling when UV coordinates go outside 0..1.
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
        _countof(rootParams), rootParams,
        1, &sampler,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    ThrowIfFailed(D3D12SerializeRootSignature(
        &rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSig, &errorBlob));

    ThrowIfFailed(mDevice->CreateRootSignature(
        0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mRootSignature)));
}

void CubeRenderer::BuildPSO()
{
    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    ComPtr<ID3DBlob> errors;

    HRESULT hr = D3DCompileFromFile(
        L"Shaders/ModelVS.hlsl", nullptr, nullptr, "VSMain", "vs_5_0",
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &vs, &errors);
    if (FAILED(hr))
    {
        if (errors) MessageBoxA(nullptr, (char*)errors->GetBufferPointer(), "VS compile error", MB_OK);
        ThrowIfFailed(hr);
    }

    hr = D3DCompileFromFile(
        L"Shaders/ModelPS.hlsl", nullptr, nullptr, "PSMain", "ps_5_0",
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &ps, &errors);
    if (FAILED(hr))
    {
        if (errors) MessageBoxA(nullptr, (char*)errors->GetBufferPointer(), "PS compile error", MB_OK);
        ThrowIfFailed(hr);
    }

    D3D12_INPUT_ELEMENT_DESC inputLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature = mRootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };

    CD3DX12_RASTERIZER_DESC rast(D3D12_DEFAULT);
    rast.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState = rast;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc.Count = 1;

    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
}

void CubeRenderer::UpdateCubeRotation(const InputDevice& input, float)
{
    if (input.IsMouseDown(0))
    {
        // Left mouse drag rotation speed for the model.
        const float rotSpeed = 0.01f;
        POINT md = input.MouseDelta();
        mCubeYaw += md.x * rotSpeed;
        mCubePitch += md.y * rotSpeed;

        const float limit = XM_PIDIV2 - 0.01f;
        if (mCubePitch > limit)  mCubePitch = limit;
        if (mCubePitch < -limit) mCubePitch = -limit;
    }
}

void CubeRenderer::UpdateCamera(const InputDevice& input, float dt)
{
    // WASD movement speed and right mouse look sensitivity.
    const float moveSpeed = 5.0f;
    const float mouseSens = 0.0025f;

    if (input.IsMouseDown(1))
    {
        POINT md = input.MouseDelta();
        mYaw += md.x * mouseSens;
        mPitch += md.y * mouseSens;

        const float limit = XM_PIDIV2 - 0.1f;
        if (mPitch > limit)  mPitch = limit;
        if (mPitch < -limit) mPitch = -limit;
    }

    XMVECTOR forward = XMVectorSet(
        cosf(mPitch) * sinf(mYaw),
        sinf(mPitch),
        cosf(mPitch) * cosf(mYaw),
        0.0f);

    XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, forward));
    XMVECTOR pos = XMLoadFloat3(&mCameraPos);

    if (input.IsKeyDown('W')) pos += forward * moveSpeed * dt;
    if (input.IsKeyDown('S')) pos -= forward * moveSpeed * dt;
    if (input.IsKeyDown('A')) pos -= right * moveSpeed * dt;
    if (input.IsKeyDown('D')) pos += right * moveSpeed * dt;

    XMStoreFloat3(&mCameraPos, pos);
}

void CubeRenderer::Update(float totalTime, float deltaTime, const InputDevice& input)
{
    UpdateCubeRotation(input, deltaTime);
    UpdateCamera(input, deltaTime);

    XMMATRIX world =
        XMMatrixTranslation(-mModelCenter.x, -mModelCenter.y, -mModelCenter.z) *
        XMMatrixScaling(mModelScale, mModelScale, mModelScale) *
        XMMatrixRotationX(mCubePitch) *
        XMMatrixRotationY(mCubeYaw);

    XMVECTOR pos = XMLoadFloat3(&mCameraPos);
    XMVECTOR forward = XMVectorSet(
        cosf(mPitch) * sinf(mYaw),
        sinf(mPitch),
        cosf(mPitch) * cosf(mYaw),
        0.0f);

    XMMATRIX view = XMMatrixLookAtLH(pos, pos + forward, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    XMMATRIX proj = XMLoadFloat4x4(&mProj);
    XMMATRIX wvp = world * view * proj;

    XMStoreFloat4x4(&mConstants.WorldViewProj, XMMatrixTranspose(wvp));
    XMStoreFloat4x4(&mConstants.World, XMMatrixTranspose(world));

    mConstants.EyePosW = mCameraPos;

    // Directional light. Change xyz to move the highlights and shadows.
    mConstants.LightDir = XMFLOAT4(0.5f, -1.0f, -0.3f, 0.0f);

    // Texture tiling: 4.0 means the texture repeats four times over UV 0..1.
    mConstants.TextureScaleX = 4.0f;
    mConstants.TextureScaleY = 4.0f;

    // Texture animation: x scroll speed, y small sine-wave wobble.
    mConstants.TextureOffset = XMFLOAT2(totalTime * 0.08f, sinf(totalTime * 0.5f) * 0.05f);
}

void CubeRenderer::UploadConstants(UINT bufferIndex)
{
    void* mapped = nullptr;
    ThrowIfFailed(mConstantUploadBuffer->Map(0, nullptr, &mapped));
    auto* dst = reinterpret_cast<uint8_t*>(mapped) + (size_t)bufferIndex * mConstantBufferByteSize;
    memcpy(dst, &mConstants, sizeof(ObjectConstants));
    mConstantUploadBuffer->Unmap(0, nullptr);
}

void CubeRenderer::Draw(ID3D12GraphicsCommandList* cmdList)
{
    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &mVBV);
    cmdList->IASetIndexBuffer(&mIBV);

    for (UINT i = 0; i < (UINT)mRenderSubsets.size(); ++i)
    {
        const auto& subset = mRenderSubsets[i];
        const RenderMaterial& material = mRenderMaterials[subset.MaterialIndex];

        // Per-subset material loaded from the OBJ/MTL pair.
        mConstants.DiffuseColor = material.Diffuse;
        mConstants.SpecularColor = material.Specular;
        mConstants.Shininess = material.Shininess;
        UploadConstants(i);

        CD3DX12_GPU_DESCRIPTOR_HANDLE textureHandle(mSrvHeap->GetGPUDescriptorHandleForHeapStart());
        textureHandle.Offset(material.TextureIndex, mCbvSrvUavDescriptorSize);
        D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mConstantUploadBuffer->GetGPUVirtualAddress() + (UINT64)i * mConstantBufferByteSize;
        cmdList->SetGraphicsRootConstantBufferView(0, cbAddress);
        cmdList->SetGraphicsRootDescriptorTable(1, textureHandle);
        cmdList->DrawIndexedInstanced(subset.IndexCount, 1, subset.IndexStart, 0, 0);
    }
}
