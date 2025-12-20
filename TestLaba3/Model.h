#pragma once
#include <vector>
#include <string>
#include "Vec.h"

struct ModelVertex {
    Vec3f position;
    Vec3f normal;
};


class Model {
public:
    std::vector<ModelVertex> vertices;
    std::vector<int> indices;

    bool Load(const std::string& path);
};
