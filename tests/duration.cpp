#include "duration.hpp"

#include <cmath>
#include <iostream>
#include <string>

namespace {

bool ends_with(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool expect_seconds(int start, int end, double increment, double expect) {
    const double got = duration_seconds(start, end, increment);
    if (got == expect) {
        return true;
    }
    std::cerr << "seconds " << start << " " << end << " *" << increment << " -> " << got << " expected " << expect
              << "\n";
    return false;
}

bool expect_label(double seconds, const std::string& expect) {
    const std::string got = format_duration(seconds);
    if (got == expect) {
        return true;
    }
    std::cerr << "label " << seconds << " -> \"" << got << "\" expected \"" << expect << "\"\n";
    return false;
}

bool expect_unit(double seconds, const std::string& unit) {
    const std::string got = format_duration(seconds);
    if (ends_with(got, unit)) {
        return true;
    }
    std::cerr << "unit " << seconds << " -> \"" << got << "\" expected suffix \"" << unit << "\"\n";
    return false;
}

}  // namespace

int main() {
    int status = 0;
    if (!expect_seconds(0, 4, 0.5, 2.0) || !expect_seconds(4, 0, 0.5, 2.0) ||
        !expect_seconds(7, 7, 3.0, 0.0)) {
        status = 1;
    }
    if (!expect_label(0.0, "0 ns") || !expect_label(1e-9, "1 ns") || !expect_label(1e-6, "1 us") ||
        !expect_label(1e-3, "1 ms") || !expect_label(1.0, "1 s") || !expect_label(60.0, "1 min") ||
        !expect_label(90.0, "1.5 min")) {
        status = 1;
    }
    if (!expect_unit(std::nextafter(60.0, 0.0), " s") || !expect_unit(std::nextafter(1.0, 0.0), " ms") ||
        !expect_unit(std::nextafter(1e-3, 0.0), " us") || !expect_unit(std::nextafter(1e-6, 0.0), " ns")) {
        status = 1;
    }
    return status;
}
