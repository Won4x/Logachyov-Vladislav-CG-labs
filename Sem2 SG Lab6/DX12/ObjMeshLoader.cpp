// ObjMeshLoader.cpp
#include "ObjMeshLoader.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <cstdio>
#include <cmath>

struct IdxTriplet
{
    int v = 0;
    int vt = 0;
    int vn = 0;
    bool operator==(const IdxTriplet& o) const { return v == o.v && vt == o.vt && vn == o.vn; }
};

struct IdxHash
{
    size_t operator()(const IdxTriplet& t) const noexcept
    {
        return (size_t)t.v * 73856093u ^ (size_t)t.vt * 83492791u ^ (size_t)t.vn * 19349663u;
    }
};

static std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
        return {};

    int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (count <= 0)
        return std::wstring(text.begin(), text.end());

    std::wstring result((size_t)count - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &result[0], count);
    return result;
}

static bool ReadTextFileLines(const std::wstring& filename, std::vector<std::string>& lines)
{
    lines.clear();

    FILE* file = nullptr;
    if (_wfopen_s(&file, filename.c_str(), L"rb") != 0 || file == nullptr)
        return false;

    std::string content;
    char buffer[4096];
    size_t read = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0)
        content.append(buffer, read);
    fclose(file);

    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line))
    {
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.pop_back();
        lines.push_back(line);
    }

    return true;
}

static std::wstring GetDirectoryName(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return L"";

    return path.substr(0, slash);
}

static std::wstring CombinePath(const std::wstring& directory, const std::wstring& file)
{
    if (directory.empty())
        return file;
    if (file.size() > 1 && (file[1] == L':' || file[0] == L'/' || file[0] == L'\\'))
        return file;

    wchar_t last = directory[directory.size() - 1];
    if (last == L'\\' || last == L'/')
        return directory + file;

    return directory + L"\\" + file;
}

static float ReadFloatOrDefault(std::stringstream& ss, float fallback)
{
    float value = fallback;
    ss >> value;
    return value;
}

static bool ParseFaceToken(const std::string& tok, int& v, int& vt, int& vn)
{
    v = 0;
    vt = 0;
    vn = 0;

    std::stringstream ss(tok);
    std::string s1, s2, s3;
    std::getline(ss, s1, '/');
    std::getline(ss, s2, '/');
    std::getline(ss, s3, '/');

    if (!s1.empty())
        v = std::stoi(s1);
    if (!s2.empty())
        vt = std::stoi(s2);
    if (!s3.empty())
        vn = std::stoi(s3);

    return v != 0;
}

static void LoadMtlFile(const std::wstring& path, ObjMeshData& out)
{
    std::vector<std::string> lines;
    if (!ReadTextFileLines(path, lines))
        return;

    ObjMaterialData* current = nullptr;
    for (const std::string& line : lines)
    {
        if (line.empty() || line[0] == '#')
            continue;

        std::stringstream ss(line);
        std::string tag;
        ss >> tag;

        if (tag == "newmtl")
        {
            ObjMaterialData material;
            ss >> material.Name;
            out.Materials.push_back(material);
            current = &out.Materials.back();
        }
        else if (current && tag == "Kd")
        {
            current->Diffuse.x = ReadFloatOrDefault(ss, current->Diffuse.x);
            current->Diffuse.y = ReadFloatOrDefault(ss, current->Diffuse.y);
            current->Diffuse.z = ReadFloatOrDefault(ss, current->Diffuse.z);
        }
        else if (current && tag == "Ks")
        {
            current->Specular.x = ReadFloatOrDefault(ss, current->Specular.x);
            current->Specular.y = ReadFloatOrDefault(ss, current->Specular.y);
            current->Specular.z = ReadFloatOrDefault(ss, current->Specular.z);
        }
        else if (current && tag == "Ns")
        {
            current->Shininess = (std::max)(1.0f, ReadFloatOrDefault(ss, current->Shininess));
        }
        else if (current && tag == "d")
        {
            current->Diffuse.w = ReadFloatOrDefault(ss, current->Diffuse.w);
        }
        else if (current && tag == "Tr")
        {
            current->Diffuse.w = 1.0f - ReadFloatOrDefault(ss, 0.0f);
        }
        else if (current && tag == "map_Kd")
        {
            std::string textureName;
            std::getline(ss >> std::ws, textureName);
            if (!textureName.empty())
                current->DiffuseTexture = CombinePath(GetDirectoryName(path), Utf8ToWide(textureName));
        }
        else if (current && (tag == "map_Disp" || tag == "disp" || tag == "bump" || tag == "map_Bump"))
        {
            std::string textureName;
            std::getline(ss >> std::ws, textureName);
            if (!textureName.empty())
            {
                std::wstring texturePath = CombinePath(GetDirectoryName(path), Utf8ToWide(textureName));
                current->NormalTexture = texturePath;
                current->DisplacementTexture = texturePath;
            }
        }
    }
}

static void StartSubset(ObjMeshData& out, ObjSubset& current, const std::string& materialName)
{
    if (current.IndexCount > 0)
        out.Subsets.push_back(current);

    current.MaterialName.clear();
    current.IndexStart = 0;
    current.IndexCount = 0;
    current.MaterialName = materialName;
    current.IndexStart = (uint32_t)out.Indices.size();
}

static void EnsureDefaultMaterial(ObjMeshData& out)
{
    if (!out.Materials.empty())
        return;

    ObjMaterialData material;
    material.Name = "Default";
    out.Materials.push_back(material);
}

static const ObjMaterialData* FindMaterial(const ObjMeshData& data, const std::string& name)
{
    for (const auto& material : data.Materials)
    {
        if (material.Name == name)
            return &material;
    }

    return data.Materials.empty() ? nullptr : &data.Materials.front();
}

bool ObjLoader::LoadObjPosNormal(const std::wstring& filename, ObjMeshData& out, bool convertToLH)
{
    out.Vertices.clear();
    out.Indices.clear();
    out.Materials.clear();
    out.Subsets.clear();

    std::vector<std::string> lines;
    if (!ReadTextFileLines(filename, lines))
        return false;

    const std::wstring objDirectory = GetDirectoryName(filename);
    std::vector<XMFLOAT3> positions(1);
    std::vector<XMFLOAT3> normals(1);
    std::vector<XMFLOAT2> texcoords(1);

    std::unordered_map<IdxTriplet, uint32_t, IdxHash> uniqueMap;
    ObjSubset currentSubset{};
    currentSubset.MaterialName = "Default";

    for (const std::string& line : lines)
    {
        if (line.empty() || line[0] == '#') continue;

        std::stringstream ss(line);
        std::string tag;
        ss >> tag;

        if (tag == "mtllib")
        {
            std::string mtlName;
            std::getline(ss >> std::ws, mtlName);
            if (!mtlName.empty())
                LoadMtlFile(CombinePath(objDirectory, Utf8ToWide(mtlName)), out);
        }
        else if (tag == "usemtl")
        {
            std::string materialName;
            ss >> materialName;
            StartSubset(out, currentSubset, materialName.empty() ? "Default" : materialName);
        }
        else if (tag == "v")
        {
            float x, y, z;
            ss >> x >> y >> z;
            if (convertToLH) z = -z;
            positions.push_back(XMFLOAT3(x, y, z));
        }
        else if (tag == "vt")
        {
            float u = 0.0f, v = 0.0f;
            ss >> u >> v;
            if (convertToLH) v = 1.0f - v;
            texcoords.push_back(XMFLOAT2(u, v));
        }
        else if (tag == "vn")
        {
            float x, y, z;
            ss >> x >> y >> z;
            if (convertToLH) z = -z;
            normals.push_back(XMFLOAT3(x, y, z));
        }
        else if (tag == "f")
        {
            std::vector<std::string> toks;
            std::string t;
            while (ss >> t) toks.push_back(t);

            if (toks.size() < 3) continue;

            auto getIndex = [&](const std::string& tok) -> uint32_t
                {
                    int v = 0, vt = 0, vn = 0;
                    ParseFaceToken(tok, v, vt, vn);

                    if (v < 0) v = (int)positions.size() + v;
                    if (vt < 0) vt = (int)texcoords.size() + vt;
                    if (vn < 0) vn = (int)normals.size() + vn;

                    IdxTriplet key{ v, vt, vn };
                    auto it = uniqueMap.find(key);
                    if (it != uniqueMap.end())
                        return it->second;

                    VertexPosNormal vert{};
                    vert.Pos = positions[(size_t)v];

                    if (vn > 0 && (size_t)vn < normals.size())
                        vert.Normal = normals[(size_t)vn];
                    else
                        vert.Normal = XMFLOAT3(0, 1, 0);

                    if (vt > 0 && (size_t)vt < texcoords.size())
                        vert.TexC = texcoords[(size_t)vt];
                    else
                        vert.TexC = XMFLOAT2(0, 0);

                    uint32_t newIndex = (uint32_t)out.Vertices.size();
                    out.Vertices.push_back(vert);
                    uniqueMap[key] = newIndex;
                    return newIndex;
                };

            uint32_t i0 = getIndex(toks[0]);
            for (size_t i = 1; i + 1 < toks.size(); ++i)
            {
                uint32_t i1 = getIndex(toks[i]);
                uint32_t i2 = getIndex(toks[i + 1]);

                if (convertToLH)
                {
                    out.Indices.push_back(i0);
                    out.Indices.push_back(i2);
                    out.Indices.push_back(i1);
                }
                else
                {
                    out.Indices.push_back(i0);
                    out.Indices.push_back(i1);
                    out.Indices.push_back(i2);
                }

                currentSubset.IndexCount += 3;
            }
        }
    }

    if (currentSubset.IndexCount > 0)
        out.Subsets.push_back(currentSubset);

    EnsureDefaultMaterial(out);

    std::vector<XMFLOAT3> tangents(out.Vertices.size(), XMFLOAT3(0.0f, 0.0f, 0.0f));
    for (size_t i = 0; i + 2 < out.Indices.size(); i += 3)
    {
        const uint32_t i0 = out.Indices[i + 0];
        const uint32_t i1 = out.Indices[i + 1];
        const uint32_t i2 = out.Indices[i + 2];
        const auto& v0 = out.Vertices[i0];
        const auto& v1 = out.Vertices[i1];
        const auto& v2 = out.Vertices[i2];

        XMVECTOR p0 = XMLoadFloat3(&v0.Pos);
        XMVECTOR p1 = XMLoadFloat3(&v1.Pos);
        XMVECTOR p2 = XMLoadFloat3(&v2.Pos);
        XMVECTOR e1 = p1 - p0;
        XMVECTOR e2 = p2 - p0;

        const float du1 = v1.TexC.x - v0.TexC.x;
        const float dv1 = v1.TexC.y - v0.TexC.y;
        const float du2 = v2.TexC.x - v0.TexC.x;
        const float dv2 = v2.TexC.y - v0.TexC.y;
        const float denom = du1 * dv2 - du2 * dv1;
        if (fabsf(denom) < 1e-6f)
            continue;

        XMVECTOR tangent = (e1 * dv2 - e2 * dv1) / denom;
        XMFLOAT3 t;
        XMStoreFloat3(&t, tangent);
        tangents[i0].x += t.x; tangents[i0].y += t.y; tangents[i0].z += t.z;
        tangents[i1].x += t.x; tangents[i1].y += t.y; tangents[i1].z += t.z;
        tangents[i2].x += t.x; tangents[i2].y += t.y; tangents[i2].z += t.z;
    }

    for (size_t i = 0; i < out.Vertices.size(); ++i)
    {
        XMVECTOR n = XMLoadFloat3(&out.Vertices[i].Normal);
        XMVECTOR t = XMLoadFloat3(&tangents[i]);
        t = t - n * XMVectorGetX(XMVector3Dot(n, t));
        if (XMVectorGetX(XMVector3LengthSq(t)) < 1e-8f)
            t = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
        t = XMVector3Normalize(t);
        XMStoreFloat3(&out.Vertices[i].Tangent, t);
    }

    for (auto& subset : out.Subsets)
    {
        if (!FindMaterial(out, subset.MaterialName))
            subset.MaterialName = out.Materials.front().Name;
    }

    return !out.Vertices.empty() && !out.Indices.empty();
}
