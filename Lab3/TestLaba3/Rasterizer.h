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
