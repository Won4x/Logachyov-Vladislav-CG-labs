#include "ZBuffer.h"

ZBuffer::ZBuffer(int w, int h) : width(w), height(h) {
    data.resize(w * h);
    Clear();
}

void ZBuffer::Clear() {
    std::fill(data.begin(), data.end(), -std::numeric_limits<float>::infinity());
}
