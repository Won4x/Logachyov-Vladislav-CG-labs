#include "Camera.h"

Mat4 Camera::GetViewMatrix() const {
    return Mat4::LookAt(position, target, up);
}

Mat4 Camera::GetProjectionMatrix() const {
    return Mat4::Perspective(fov, aspect, nearPlane, farPlane);
}
