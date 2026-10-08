#include "rolling_rms.hpp"

#include <cmath>

bool view_sample_bounds(double x_min, double x_max, int count, int& first, int& last) {
    if (count <= 0) {
        return false;
    }
    const double last_index = static_cast<double>(count - 1);
    if (!(x_max >= 0.0) || !(x_min <= last_index)) {
        return false;
    }
    double first_d = std::ceil(x_min);
    double last_d = std::floor(x_max);
    if (first_d < 0.0) {
        first_d = 0.0;
    }
    if (last_d > last_index) {
        last_d = last_index;
    }
    if (!(first_d <= last_d)) {
        return false;
    }
    first = static_cast<int>(first_d);
    last = static_cast<int>(last_d);
    return true;
}
