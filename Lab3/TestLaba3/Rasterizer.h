#pragma once
#include "Vec.h"
#include "Image.h"
#include "ZBuffer.h"
#include "Shader.h"

void DrawTriangle(
    Vec4f clip[3],
    IShader& shader,
    Image& image,
    ZBuffer& zbuffer
);

void DrawTriangleTransparent(
    Vec4f clip[3],
    const Vec3f& color,
    float alpha,
    Image& image,
    ZBuffer& zbuffer
);

