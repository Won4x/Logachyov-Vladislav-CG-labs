#pragma once
#include <vector>
#include <limits>

class ZBuffer {
public:
    int width, height;
    std::vector<float> data;

    ZBuffer(int w, int h);
    void Clear();
};
