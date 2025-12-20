#pragma once
#include "Vec.h"
#include "Math.h"

class Camera {
public:
    Vec3f position;
    Vec3f target;
    Vec3f up;

    float fov;
    float aspect;
    float nearPlane;
    float farPlane;

    Mat4 GetViewMatrix() const;
    Mat4 GetProjectionMatrix() const;
};
