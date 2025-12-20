#include "PhongShader.h"
#include "Math.h"

Vec4f PhongShader::Vertex(int face, int vertex) {
    int idx = model->indices[face * 3 + vertex];
    ModelVertex& v = model->vertices[idx];


    varyingNormal[vertex] = v.normal;
    varyingWorldPos[vertex] = v.position;

    return MVP * Vec4f(v.position.x, v.position.y, v.position.z, 1.f);
}


bool PhongShader::Fragment(const Vec3f& bar, Vec3f& color) {
    Vec3f n =
        varyingNormal[0] * bar.x +
        varyingNormal[1] * bar.y +
        varyingNormal[2] * bar.z;
    n.normalize();


    Vec3f l = lightDir;
    l.normalize();

    float diff = std::max(0.f, n.dot(l));

    Vec3f ambient(0.1f, 0.1f, 0.1f);
    Vec3f diffuse(diff, diff, diff);

    color = Vec3f(
        std::min(1.f, ambient.x + diffuse.x),
        std::min(1.f, ambient.y + diffuse.y),
        std::min(1.f, ambient.z + diffuse.z)
    );

    return false;
}
