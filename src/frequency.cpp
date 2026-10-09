#include "frequency.hpp"

#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>

namespace {

constexpr int basis_count = 3;

bool finite_samples(const double* samples, int count) {
    for (int index = 0; index < count; ++index) {
        if (!std::isfinite(samples[index])) {
            return false;
        }
    }
    return true;
}

double largest_abs(const double matrix[basis_count][basis_count]) {
    double largest = 0.0;
    for (int row = 0; row < basis_count; ++row) {
        for (int col = 0; col < basis_count; ++col) {
            const double value = std::fabs(matrix[row][col]);
            if (value > largest) {
                largest = value;
            }
        }
    }
    return largest;
}

void solve_normal(
    double matrix[basis_count][basis_count + 1],
    double max_abs,
    double coefficients[basis_count]) {
    const double tolerance = 1e-12 * max_abs;
    bool pivoted[basis_count] = {};
    int pivot_row[basis_count] = {};
    bool row_used[basis_count] = {};

    for (int col = 0; col < basis_count; ++col) {
        int best = -1;
        double best_abs = 0.0;
        for (int row = 0; row < basis_count; ++row) {
            if (row_used[row]) {
                continue;
            }
            const double magnitude = std::fabs(matrix[row][col]);
            if (best < 0 || magnitude > best_abs) {
                best = row;
                best_abs = magnitude;
            }
        }
        if (best < 0 || !(best_abs > tolerance)) {
            continue;
        }
        row_used[best] = true;
        pivoted[col] = true;
        pivot_row[col] = best;
        const double pivot = matrix[best][col];
        for (int row = 0; row < basis_count; ++row) {
            if (row == best) {
                continue;
            }
            const double factor = matrix[row][col] / pivot;
            for (int entry = col; entry < basis_count + 1; ++entry) {
                matrix[row][entry] -= factor * matrix[best][entry];
            }
        }
    }

    for (int col = 0; col < basis_count; ++col) {
        coefficients[col] = 0.0;
    }
    for (int col = basis_count - 1; col >= 0; --col) {
        if (!pivoted[col]) {
            continue;
        }
        const int row = pivot_row[col];
        double value = matrix[row][basis_count];
        for (int entry = col + 1; entry < basis_count; ++entry) {
            value -= matrix[row][entry] * coefficients[entry];
        }
        coefficients[col] = value / matrix[row][col];
    }
}

double squared_error(const double* samples, int count, int k, const double coefficients[basis_count]) {
    const double span = static_cast<double>(count - 1);
    double error = 0.0;
    for (int index = 0; index < count; ++index) {
        const double angle =
            (2.0 * std::numbers::pi * static_cast<double>(k) * static_cast<double>(index)) / span;
        const double model =
            coefficients[0] * std::cos(angle) + coefficients[1] * std::sin(angle) + coefficients[2];
        const double residual = samples[index] - model;
        error += residual * residual;
    }
    return error;
}

}  // namespace

std::optional<double> least_squares_frequency(const double* samples, int count, double dt) {
    if (samples == nullptr || count < 3 || !std::isfinite(dt) || !(dt > 0.0) || !finite_samples(samples, count)) {
        return std::nullopt;
    }

    const int span = count - 1;
    const int k_max = span / 2;
    bool have = false;
    double best_error = 0.0;
    double best_hz = 0.0;
    for (int k = 1; k <= k_max; ++k) {
        double gram[basis_count][basis_count] = {};
        double rhs[basis_count] = {};
        for (int index = 0; index < count; ++index) {
            const double angle =
                (2.0 * std::numbers::pi * static_cast<double>(k) * static_cast<double>(index)) /
                static_cast<double>(span);
            const double basis[basis_count] = {std::cos(angle), std::sin(angle), 1.0};
            const double sample = samples[index];
            for (int row = 0; row < basis_count; ++row) {
                rhs[row] += sample * basis[row];
                for (int col = 0; col < basis_count; ++col) {
                    gram[row][col] += basis[row] * basis[col];
                }
            }
        }

        const double max_abs = largest_abs(gram);
        double augmented[basis_count][basis_count + 1];
        for (int row = 0; row < basis_count; ++row) {
            for (int col = 0; col < basis_count; ++col) {
                augmented[row][col] = gram[row][col];
            }
            augmented[row][basis_count] = rhs[row];
        }
        double coefficients[basis_count];
        solve_normal(augmented, max_abs, coefficients);
        const double error = squared_error(samples, count, k, coefficients);
        if (!std::isfinite(error)) {
            continue;
        }
        if (!have || error < best_error) {
            have = true;
            best_error = error;
            best_hz = static_cast<double>(k) / (static_cast<double>(span) * dt);
        }
    }
    if (!have) {
        return std::nullopt;
    }
    return best_hz;
}

std::string format_frequency(double hertz) {
    char number[64];
    std::snprintf(number, sizeof(number), "%.6g", hertz);
    return std::string(number) + " Hz";
}
