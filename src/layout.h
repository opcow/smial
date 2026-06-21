#pragma once
#include <cstdint>
#include <vector>

struct Key {
    int   row, col;
    float x, y, w;  // in key units (1 unit = one standard key width)
};

const std::vector<Key>& layout();
