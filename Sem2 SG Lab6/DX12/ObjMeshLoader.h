// ObjMeshLoader.h
#pragma once
#include "DxCommon.h"
#include <string>

struct VertexPosNormal
{
    XMFLOAT3 Pos;
    XMFLOAT3 Normal;
    XMFLOAT3 Tangent;
    XMFLOAT2 TexC;
};

struct ObjMaterialData
{
    std::string Name;
    XMFLOAT4 Diffuse = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
    XMFLOAT4 Specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    float Shininess = 16.0f;
    std::wstring DiffuseTexture;
    std::wstring NormalTexture;
    std::wstring DisplacementTexture;
};

struct ObjSubset
{
    std::string MaterialName;
    uint32_t IndexStart = 0;
    uint32_t IndexCount = 0;
};

struct ObjMeshData
{
    std::vector<VertexPosNormal> Vertices;
    std::vector<uint32_t> Indices;
    std::vector<ObjMaterialData> Materials;
    std::vector<ObjSubset> Subsets;
};

class ObjLoader
{
public:
   
    static bool LoadObjPosNormal(const std::wstring& filename, ObjMeshData& out, bool convertToLH = true);
};

