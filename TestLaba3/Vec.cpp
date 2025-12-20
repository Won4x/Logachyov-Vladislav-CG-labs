#include "Vec.h"
#include <cmath>

Vec3f Vec3f::operator+(const Vec3f& v) const {
    return { x + v.x, y + v.y, z + v.z };
}

Vec3f Vec3f::operator-(const Vec3f& v) const {
    return { x - v.x, y - v.y, z - v.z };
}

Vec3f Vec3f::operator*(float f) const {
    return { x * f, y * f, z * f };
}

float Vec3f::dot(const Vec3f& v) const {
    return x * v.x + y * v.y + z * v.z;
}

Vec3f Vec3f::cross(const Vec3f& v) const {
    return {
        y * v.z - z * v.y,
        z * v.x - x * v.z,
        x * v.y - y * v.x
    };
}

void Vec3f::normalize() {
    float l = std::sqrt(dot(*this));
    if (l > 0) {
        x /= l; y /= l; z /= l;
    }
}

Vec4f::Vec4f(float X, float Y, float Z, float W)
    : x(X), y(Y), z(Z), w(W) {
}
