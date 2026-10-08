#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

bool view_sample_bounds(double x_min, double x_max, int count, int& first, int& last);

template <typename T>
void rolling_rms(
    const T* samples,
    int first,
    int last,
    int window,
    std::vector<double>& indexes,
    std::vector<double>& values) {
    indexes.clear();
    values.clear();
    if (samples == nullptr || window < 1 || first < 0 || last < first) {
        return;
    }

    const int before = window / 2;
    const int after = (window - 1) / 2;
    const long long start_center = static_cast<long long>(first) + before;
    const long long end_center = static_cast<long long>(last) - after;
    if (start_center > end_center) {
        return;
    }

    const int center_count = static_cast<int>(end_center - start_center + 1);
    indexes.reserve(static_cast<std::size_t>(center_count));
    values.reserve(static_cast<std::size_t>(center_count));

    int window_begin = first;
    double sumsq = 0.0;
    int nonfinite = 0;
    for (int offset = 0; offset < window; ++offset) {
        const double sample = static_cast<double>(samples[window_begin + offset]);
        if (!std::isfinite(sample)) {
            ++nonfinite;
        } else {
            sumsq += sample * sample;
        }
    }

    const auto push = [&](long long center) {
        if (nonfinite != 0) {
            return;
        }
        double mean_sq = sumsq / static_cast<double>(window);
        if (mean_sq < 0.0) {
            mean_sq = 0.0;
        }
        indexes.push_back(static_cast<double>(center));
        values.push_back(std::sqrt(mean_sq));
    };

    push(start_center);
    for (long long center = start_center; center < end_center; ++center) {
        const double leaving = static_cast<double>(samples[window_begin]);
        const double entering = static_cast<double>(samples[window_begin + window]);
        if (!std::isfinite(leaving)) {
            --nonfinite;
        } else {
            sumsq -= leaving * leaving;
        }
        if (!std::isfinite(entering)) {
            ++nonfinite;
        } else {
            sumsq += entering * entering;
        }
        ++window_begin;
        push(center + 1);
    }
}
