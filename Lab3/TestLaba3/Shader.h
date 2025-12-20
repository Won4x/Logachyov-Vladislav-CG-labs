#pragma once
#include "Vec.h"

struct IShader {
    virtual Vec4f Vertex(int face, int vertex) = 0;
    virtual bool Fragment(const Vec3f& bar, Vec3f& color) = 0;
    virtual ~IShader() {}
};
