#include "Rasterizer.h"
#include <algorithm>

static Vec3f Barycentric(const Vec3f& A, const Vec3f& B, const Vec3f& C, const Vec3f& P) {
    Vec3f s0(C.x - A.x, B.x - A.x, A.x - P.x);
    Vec3f s1(C.y - A.y, B.y - A.y, A.y - P.y);
    Vec3f u = s0.cross(s1);
    if (std::abs(u.z) < 1e-2) return Vec3f(-1, 1, 1);
    return Vec3f(1.f - (u.x + u.y) / u.z, u.y / u.z, u.x / u.z);
}

void DrawTriangle(Vec4f clip[3], IShader& shader, Image& image, ZBuffer& zbuffer) {
    Vec3f pts[3];
    for (int i = 0; i < 3; i++) {
        float x = clip[i].x / clip[i].w;
        float y = clip[i].y / clip[i].w;
        float z = clip[i].z / clip[i].w;

        pts[i].x = (x + 1.f) * 0.5f * image.width;
        pts[i].y = (y + 1.f) * 0.5f * image.height;
        pts[i].z = z;
    }


    int minX = std::max(0, int(std::min({ pts[0].x, pts[1].x, pts[2].x })));
    int maxX = std::min(image.width - 1, int(std::max({ pts[0].x, pts[1].x, pts[2].x })));
    int minY = std::max(0, int(std::min({ pts[0].y, pts[1].y, pts[2].y })));
    int maxY = std::min(image.height - 1, int(std::max({ pts[0].y, pts[1].y, pts[2].y })));

    for (int x = minX; x <= maxX; x++) {
        for (int y = minY; y <= maxY; y++) {
            Vec3f bc = Barycentric(pts[0], pts[1], pts[2], Vec3f(x, y, 0));
            if (bc.x < 0 || bc.y < 0 || bc.z < 0) continue;

            float depth =
                pts[0].z * bc.x +
                pts[1].z * bc.y +
                pts[2].z * bc.z;

            int idx = x + y * image.width;
            if (depth > zbuffer.data[idx]) {
                Vec3f color;
                if (!shader.Fragment(bc, color)) {
                    zbuffer.data[idx] = depth;
                    image.SetPixel(x, y, color);
                }
            }
        }
    }
}


void DrawTriangleTransparent(
    Vec4f clip[3],
    const Vec3f& color,
    float alpha,
    Image& image,
    ZBuffer& zbuffer
) {
    Vec3f pts[3];
    for (int i = 0; i < 3; i++) {
        float x = clip[i].x / clip[i].w;
        float y = clip[i].y / clip[i].w;
        float z = clip[i].z / clip[i].w;

        pts[i].x = (x + 1.f) * 0.5f * image.width;
        pts[i].y = (1.f - (y + 1.f) * 0.5f) * image.height;
        pts[i].z = z;
    }

    int minX = std::max(0, int(std::min({ pts[0].x, pts[1].x, pts[2].x })));
    int maxX = std::min(image.width - 1, int(std::max({ pts[0].x, pts[1].x, pts[2].x })));
    int minY = std::max(0, int(std::min({ pts[0].y, pts[1].y, pts[2].y })));
    int maxY = std::min(image.height - 1, int(std::max({ pts[0].y, pts[1].y, pts[2].y })));

    for (int x = minX; x <= maxX; x++) {
        for (int y = minY; y <= maxY; y++) {
            Vec3f bc = Barycentric(pts[0], pts[1], pts[2], Vec3f(x, y, 0));
            if (bc.x < 0 || bc.y < 0 || bc.z < 0) continue;

            float depth =
                pts[0].z * bc.x +
                pts[1].z * bc.y +
                pts[2].z * bc.z;

            int idx = x + y * image.width;

            
            if (depth > zbuffer.data[idx]) {
                image.BlendPixel(x, y, color, alpha);
            }
        }
    }
}

