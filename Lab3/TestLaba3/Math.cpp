#include "Math.h"
#include <cmath>

Mat4 Mat4::Identity() {
    Mat4 r{};
    for (int i = 0; i < 4; i++)
        r.m[i][i] = 1.f;
    return r;
}

Mat4 Mat4::LookAt(const Vec3f& eye, const Vec3f& center, const Vec3f& up) {
    Vec3f z = (center - eye);
    z.normalize();
    Vec3f x = z.cross(up);
    x.normalize();
    Vec3f y = x.cross(z);


    Mat4 r = Identity();
    r.m[0][0] = x.x; r.m[0][1] = x.y; r.m[0][2] = x.z;
    r.m[1][0] = y.x; r.m[1][1] = y.y; r.m[1][2] = y.z;
    r.m[2][0] = z.x; r.m[2][1] = z.y; r.m[2][2] = z.z;

    r.m[0][3] = -x.dot(eye);
    r.m[1][3] = -y.dot(eye);
    r.m[2][3] = -z.dot(eye);

    return r;
}

Mat4 Mat4::Perspective(float fov, float aspect, float n, float f) {
    Mat4 r{};
    float t = tanf(fov * 0.5f * 3.1415926f / 180.f);

    r.m[0][0] = 1.f / (aspect * t);
    r.m[1][1] = 1.f / t;
    r.m[2][2] = -(f + n) / (f - n);
    r.m[2][3] = -(2.f * f * n) / (f - n);
    r.m[3][2] = -1.f;

    return r;
}

Mat4 Mat4::operator*(const Mat4& o) const {
    Mat4 r{};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            for (int k = 0; k < 4; k++)
                r.m[i][j] += m[i][k] * o.m[k][j];
    return r;
}

Vec4f Mat4::operator*(const Vec4f& v) const {
    return {
        m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3] * v.w,
        m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3] * v.w,
        m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3] * v.w,
        m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3] * v.w
    };
}
