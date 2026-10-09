#pragma once

#include <optional>
#include <string>

std::optional<double> least_squares_frequency(const double* samples, int count, double dt);

std::string format_frequency(double hertz);
