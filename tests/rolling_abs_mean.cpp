#include "rolling_abs_mean.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace {

bool same_points(
    const std::vector<double>& indexes,
    const std::vector<double>& values,
    const std::vector<double>& expect_indexes,
    const std::vector<double>& expect_values) {
    if (indexes == expect_indexes && values == expect_values) {
        return true;
    }
    std::cerr << "abs mean indexes";
    for (double index : indexes) {
        std::cerr << " " << index;
    }
    std::cerr << "\nabs mean values";
    for (double value : values) {
        std::cerr << " " << value;
    }
    std::cerr << "\n";
    return false;
}

}  // namespace

int main() {
    int status = 0;

    const double unit[] = {1.0, -2.0, 3.0};
    std::vector<double> indexes;
    std::vector<double> values;
    rolling_abs_mean(unit, 0, 2, 1, indexes, values);
    if (!same_points(indexes, values, {0.0, 1.0, 2.0}, {1.0, 2.0, 3.0})) {
        status = 1;
    }

    const float unit_float[] = {1.0f, -2.0f, 3.0f};
    rolling_abs_mean(unit_float, 0, 2, 1, indexes, values);
    if (!same_points(indexes, values, {0.0, 1.0, 2.0}, {1.0, 2.0, 3.0})) {
        status = 1;
    }

    const double odd[] = {0.0, 0.0, 3.0, 0.0, 0.0};
    rolling_abs_mean(odd, 0, 4, 3, indexes, values);
    if (!same_points(indexes, values, {1.0, 2.0, 3.0}, {1.0, 1.0, 1.0})) {
        status = 1;
    }

    const double even[] = {0.0, 0.0, 0.0, 0.0, 2.0};
    rolling_abs_mean(even, 0, 4, 4, indexes, values);
    if (!same_points(indexes, values, {2.0, 3.0}, {0.0, 0.5})) {
        status = 1;
    }

    double clipped[11];
    for (double& sample : clipped) {
        sample = 2.0;
    }
    clipped[2] = 100.0;
    rolling_abs_mean(clipped, 3, 10, 5, indexes, values);
    if (!same_points(indexes, values, {5.0, 6.0, 7.0, 8.0}, {2.0, 2.0, 2.0, 2.0})) {
        status = 1;
    }

    const double nonfinite[] = {
        3.0,
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -3.0,
    };
    rolling_abs_mean(nonfinite, 0, 3, 1, indexes, values);
    if (!same_points(indexes, values, {0.0, 3.0}, {3.0, 3.0})) {
        status = 1;
    }

    const double gap[] = {3.0, 3.0, 3.0, std::numeric_limits<double>::quiet_NaN(), 3.0, 3.0, 3.0};
    rolling_abs_mean(gap, 0, 6, 3, indexes, values);
    if (!same_points(indexes, values, {1.0, 5.0}, {3.0, 3.0})) {
        status = 1;
    }

    const double short_view[] = {1.0, 2.0};
    rolling_abs_mean(short_view, 0, 1, 5, indexes, values);
    if (!same_points(indexes, values, {}, {})) {
        status = 1;
    }

    return status;
}
