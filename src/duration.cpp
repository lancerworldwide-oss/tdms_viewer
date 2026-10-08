#include "duration.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

double duration_seconds(int start_index, int end_index, double increment) {
    const long long span =
        std::llabs(static_cast<long long>(end_index) - static_cast<long long>(start_index));
    return static_cast<double>(span) * increment;
}

std::string format_duration(double seconds) {
    double value = seconds;
    const char* unit = "ns";
    if (seconds >= 60.0) {
        value = seconds / 60.0;
        unit = "min";
    } else if (seconds >= 1.0) {
        unit = "s";
    } else if (seconds >= 0.001) {
        value = seconds * 1000.0;
        unit = "ms";
    } else if (seconds >= 0.000001) {
        value = seconds * 1000000.0;
        unit = "us";
    } else {
        value = seconds * 1000000000.0;
    }
    char number[64];
    std::snprintf(number, sizeof(number), "%.6g", value);
    return std::string(number) + " " + unit;
}
