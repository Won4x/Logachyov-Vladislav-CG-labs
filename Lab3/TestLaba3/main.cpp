#include "Image.h"
#include "ZBuffer.h"
#include "Camera.h"
#include "Model.h"
#include "PhongShader.h"
#include "Rasterizer.h"

int main() {
    const int W = 800;
    const int H = 800;

    Image image(W, H);
    ZBuffer zbuffer(W, H);

    Model model;
    model.Load("african_head.obj");
    if (!model.Load("african_head.obj")) {
        printf("MODEL NOT LOADED\n");
        return 0;
    }

    Camera cam;
    cam.position = { 0, 0, 4};
    cam.target = { 0, 0, 0 };
    cam.up = { 0, 1, 0 };
    cam.fov = 60.f;
    cam.aspect = float(W) / H;
    cam.nearPlane = 0.1f;
    cam.farPlane = 100.f;

    Mat4 MVP = cam.GetProjectionMatrix() * cam.GetViewMatrix();

    PhongShader shader;
    shader.model = &model;
    shader.MVP = MVP;
    shader.lightDir = { 2, 2, 2 };

    for (size_t i = 0; i < model.indices.size() / 3; i++) {
        Vec4f clip[3];
        for (int j = 0; j < 3; j++)
            clip[j] = shader.Vertex(i, j);

        DrawTriangle(clip, shader, image, zbuffer);
    }
    Vec3f cubeVerts[] = {
    {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
    {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}
    };

    int cubeIndices[] = {
        0,1,2, 0,2,3,
        4,5,6, 4,6,7,
        0,1,5, 0,5,4,
        2,3,7, 2,7,6,
        1,2,6, 1,6,5,
        0,3,7, 0,7,4
    };

    Vec3f cubeColor(0.9f, 0.9f, 0.9f);
    float alpha = 0.4f;

    for (int i = 0; i < 36; i += 3) {
        Vec4f clip[3];
        for (int j = 0; j < 3; j++) {
            Vec3f p = cubeVerts[cubeIndices[i + j]];
            clip[j] = MVP * Vec4f(p.x, p.y, p.z, 1.f);
        }

        DrawTriangleTransparent(clip, cubeColor, alpha, image, zbuffer);
    }

    image.SavePPM("output.ppm");
    return 0;
}
