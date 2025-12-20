#include "model.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

bool Model::Load(const std::string& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path,
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_JoinIdenticalVertices
    );

    if (!scene || !scene->HasMeshes()) return false;

    aiMesh* mesh = scene->mMeshes[0];

    vertices.resize(mesh->mNumVertices);
    for (unsigned i = 0; i < mesh->mNumVertices; i++) {
        vertices[i].position = {
            mesh->mVertices[i].x,
            mesh->mVertices[i].y,
            mesh->mVertices[i].z
        };
        vertices[i].normal = {
            mesh->mNormals[i].x,
            mesh->mNormals[i].y,
            mesh->mNormals[i].z
        };
    }

    for (unsigned i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        for (unsigned j = 0; j < face.mNumIndices; j++)
            indices.push_back(face.mIndices[j]);
    }

    return true;
}
