#pragma once
#include "shader.h"
#include "Model.h"
#include "Math.h"

class PhongShader : public IShader {
public:
    Model* model;

    Mat4 MVP;
    Mat4 ModelMatrix;

    Vec3f lightDir;
    Vec3f cameraPos;

    Vec3f varyingNormal[3];
    Vec3f varyingWorldPos[3];

    Vec4f Vertex(int face, int vertex) override;
    bool Fragment(const Vec3f& bar, Vec3f& color) override;
};
