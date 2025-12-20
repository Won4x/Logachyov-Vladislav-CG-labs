#pragma once
#include "Vec.h"

struct Mat4 {
    float m[4][4];

    static Mat4 Identity();
    static Mat4 LookAt(const Vec3f& eye, const Vec3f& center, const Vec3f& up);
    static Mat4 Perspective(float fov, float aspect, float zNear, float zFar);
    static Mat4 Viewport(int x, int y, int w, int h);

    Mat4 operator * (const Mat4& other) const;
    Vec4f operator * (const Vec4f& v) const;
};
