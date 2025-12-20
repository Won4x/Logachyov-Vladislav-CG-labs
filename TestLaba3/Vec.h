#pragma once
#include <cmath>

struct Vec2f {
    float x, y;
};

struct Vec3f {
    float x, y, z;

    Vec3f() : x(0), y(0), z(0) {}
    Vec3f(float X, float Y, float Z) : x(X), y(Y), z(Z) {}

    Vec3f operator + (const Vec3f& v) const;
    Vec3f operator - (const Vec3f& v) const;
    Vec3f operator * (float f) const;

    float dot(const Vec3f& v) const;
    Vec3f cross(const Vec3f& v) const;

    void normalize();
};

struct Vec4f {
    float x, y, z, w;

    Vec4f() : x(0), y(0), z(0), w(1) {}
    Vec4f(float X, float Y, float Z, float W = 1.f);
};
