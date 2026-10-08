#include "rolling_rms.hpp"

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
    std::cerr << "rms indexes";
    for (double index : indexes) {
        std::cerr << " " << index;
    }
    std::cerr << "\nrms values";
    for (double value : values) {
        std::cerr << " " << value;
    }
    std::cerr << "\n";
    return false;
}

bool expect_bounds(double x_min, double x_max, int count, bool present, int expect_first, int expect_last) {
    int first = -1;
    int last = -1;
    const bool got = view_sample_bounds(x_min, x_max, count, first, last);
    if (got != present || (present && (first != expect_first || last != expect_last))) {
        std::cerr << "bounds " << x_min << " " << x_max << " count " << count << " -> " << got << " " << first
                  << " " << last << "\n";
        return false;
    }
    return true;
}

}  // namespace

int main() {
    int status = 0;

    if (!expect_bounds(3.0, 10.0, 11, true, 3, 10) ||
        !expect_bounds(3.2, 10.0, 11, true, 4, 10) ||
        !expect_bounds(2.0, 10.9, 11, true, 2, 10) ||
        !expect_bounds(-2.0, 1.2, 11, true, 0, 1) ||
        !expect_bounds(20.0, 30.0, 11, false, 0, 0) ||
        !expect_bounds(0.1, 0.9, 11, false, 0, 0) ||
        !expect_bounds(0.0, 1.0, 0, false, 0, 0) ||
        !expect_bounds(std::numeric_limits<double>::quiet_NaN(), 4.0, 11, false, 0, 0)) {
        status = 1;
    }

    const double unit[] = {1.0, -2.0, 3.0};
    std::vector<double> indexes;
    std::vector<double> values;
    rolling_rms(unit, 0, 2, 1, indexes, values);
    if (!same_points(indexes, values, {0.0, 1.0, 2.0}, {1.0, 2.0, 3.0})) {
        status = 1;
    }

    const float unit_float[] = {1.0f, -2.0f, 3.0f};
    rolling_rms(unit_float, 0, 2, 1, indexes, values);
    if (!same_points(indexes, values, {0.0, 1.0, 2.0}, {1.0, 2.0, 3.0})) {
        status = 1;
    }

    const double odd[] = {0.0, 0.0, 3.0, 0.0, 0.0};
    rolling_rms(odd, 0, 4, 3, indexes, values);
    const double odd_rms = std::sqrt(3.0);
    if (!same_points(indexes, values, {1.0, 2.0, 3.0}, {odd_rms, odd_rms, odd_rms})) {
        status = 1;
    }

    const double even[] = {0.0, 0.0, 0.0, 0.0, 2.0};
    rolling_rms(even, 0, 4, 4, indexes, values);
    if (!same_points(indexes, values, {2.0, 3.0}, {0.0, 1.0})) {
        status = 1;
    }

    double clipped[11];
    for (double& sample : clipped) {
        sample = 2.0;
    }
    clipped[2] = 100.0;
    rolling_rms(clipped, 3, 10, 5, indexes, values);
    if (!same_points(indexes, values, {5.0, 6.0, 7.0, 8.0}, {2.0, 2.0, 2.0, 2.0})) {
        status = 1;
    }

    const double nonfinite[] = {
        3.0,
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -3.0,
    };
    rolling_rms(nonfinite, 0, 3, 1, indexes, values);
    if (!same_points(indexes, values, {0.0, 3.0}, {3.0, 3.0})) {
        status = 1;
    }

    const double gap[] = {3.0, 3.0, 3.0, std::numeric_limits<double>::quiet_NaN(), 3.0, 3.0, 3.0};
    rolling_rms(gap, 0, 6, 3, indexes, values);
    if (!same_points(indexes, values, {1.0, 5.0}, {3.0, 3.0})) {
        status = 1;
    }

    const double short_view[] = {1.0, 2.0};
    rolling_rms(short_view, 0, 1, 5, indexes, values);
    if (!same_points(indexes, values, {}, {})) {
        status = 1;
    }

    return status;
}
