#pragma once

#include <vector>

struct FftComponent {
    int bin = 0;
    double amplitude = 0.0;
};

std::vector<FftComponent> separate_components(const double* samples, int count, int keep);
