#pragma once
#include <vector>
#include "Vec.h"

class Image {
public:
    int width, height;
    std::vector<Vec3f> pixels;

    Image(int w, int h);
    void Clear(const Vec3f& color);
    void SetPixel(int x, int y, const Vec3f& color);
    void SavePPM(const char* filename);
    void BlendPixel(int x, int y, const Vec3f& color, float alpha);
};
