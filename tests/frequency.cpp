#include "frequency.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace {

bool expect_frequency(const std::vector<double>& samples, double dt, double expect) {
    const std::optional<double> got =
        least_squares_frequency(samples.data(), static_cast<int>(samples.size()), dt);
    if (got.has_value() && *got == expect) {
        return true;
    }
    std::cerr << "frequency -> ";
    if (got.has_value()) {
        std::cerr << *got;
    } else {
        std::cerr << "nullopt";
    }
    std::cerr << " expected " << expect << "\n";
    return false;
}

bool expect_none(const double* samples, int count, double dt) {
    const std::optional<double> got = least_squares_frequency(samples, count, dt);
    if (!got.has_value()) {
        return true;
    }
    std::cerr << "frequency -> " << *got << " expected nullopt\n";
    return false;
}

bool expect_label(double hertz, const std::string& expect) {
    const std::string got = format_frequency(hertz);
    if (got == expect) {
        return true;
    }
    std::cerr << "label " << hertz << " -> \"" << got << "\" expected \"" << expect << "\"\n";
    return false;
}

double grid_hz(int k, int count, double dt) {
    return static_cast<double>(k) / ((static_cast<double>(count) - 1.0) * dt);
}

std::vector<double> tone(int count, int k, double amplitude, double offset) {
    std::vector<double> samples(static_cast<std::size_t>(count));
    const double span = static_cast<double>(count - 1);
    for (int index = 0; index < count; ++index) {
        const double angle =
            (2.0 * std::numbers::pi * static_cast<double>(k) * static_cast<double>(index)) / span;
        samples[static_cast<std::size_t>(index)] = offset + amplitude * std::sin(angle);
    }
    return samples;
}

}  // namespace

int main() {
    int status = 0;
    const double dt = 0.01;
    const int count = 32;
    const int k = 4;
    const double expect = grid_hz(k, count, dt);
    if (!expect_frequency(tone(count, k, 1.0, 0.0), dt, expect)) {
        status = 1;
    }
    if (!expect_frequency(tone(count, k, 1.5, 0.5), dt, expect)) {
        status = 1;
    }

    const double dt_nyquist = 0.2;
    const std::vector<double> nyquist{1.0, -1.0, 1.0};
    if (!expect_frequency(nyquist, dt_nyquist, grid_hz(1, 3, dt_nyquist))) {
        status = 1;
    }

    const int mixed_count = 16;
    const double mixed_dt = 0.05;
    std::vector<double> mixed(static_cast<std::size_t>(mixed_count));
    const double mixed_span = static_cast<double>(mixed_count - 1);
    for (int index = 0; index < mixed_count; ++index) {
        const double sample_index = static_cast<double>(index);
        const double weak =
            std::sin((2.0 * std::numbers::pi * sample_index) / mixed_span);
        const double strong =
            std::sin((2.0 * std::numbers::pi * 3.0 * sample_index) / mixed_span);
        mixed[static_cast<std::size_t>(index)] = 0.1 * weak + 4.0 * strong;
    }
    if (!expect_frequency(mixed, mixed_dt, grid_hz(3, mixed_count, mixed_dt))) {
        status = 1;
    }

    const std::vector<double> tie(8, 0.0);
    if (!expect_frequency(tie, 0.5, grid_hz(1, 8, 0.5))) {
        status = 1;
    }

    std::vector<double> nonfinite = tone(count, k, 1.0, 0.0);
    nonfinite[3] = std::numeric_limits<double>::quiet_NaN();
    if (!expect_none(nonfinite.data(), count, dt)) {
        status = 1;
    }
    nonfinite[3] = std::numeric_limits<double>::infinity();
    if (!expect_none(nonfinite.data(), count, dt)) {
        status = 1;
    }

    const double pair[2] = {1.0, 2.0};
    if (!expect_none(pair, 2, dt) || !expect_none(pair, 1, dt) || !expect_none(pair, 0, dt) ||
        !expect_none(nullptr, count, dt) || !expect_none(pair, 2, 0.0) || !expect_none(pair, 2, -1.0) ||
        !expect_none(pair, 2, std::numeric_limits<double>::quiet_NaN())) {
        status = 1;
    }

    if (!expect_label(1.25, "1.25 Hz") || !expect_label(1.0, "1 Hz")) {
        status = 1;
    }
    return status;
}
