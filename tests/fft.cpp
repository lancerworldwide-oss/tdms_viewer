#include "fft.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace {

bool close_amplitude(double got, double expect) {
    if (!(std::abs(got - expect) <= 1e-8)) {
        std::cerr << got << " expected " << expect << "\n";
        return false;
    }
    return true;
}

std::vector<double> cosine(int count, int bin, double amplitude) {
    std::vector<double> samples(static_cast<std::size_t>(count));
    const double step = (2.0 * std::acos(-1.0) * static_cast<double>(bin)) / static_cast<double>(count);
    for (int index = 0; index < count; ++index) {
        samples[static_cast<std::size_t>(index)] = amplitude * std::cos(step * static_cast<double>(index));
    }
    return samples;
}

std::vector<double> add(const std::vector<double>& left, const std::vector<double>& right) {
    std::vector<double> sum(left.size());
    for (std::size_t index = 0; index < left.size(); ++index) {
        sum[index] = left[index] + right[index];
    }
    return sum;
}

const FftComponent* find_bin(const std::vector<FftComponent>& components, int bin) {
    for (const FftComponent& component : components) {
        if (component.bin == bin) {
            return &component;
        }
    }
    return nullptr;
}

bool expect_two_tones() {
    const int count = 32;
    const std::vector<double> low = cosine(count, 1, 1.0);
    const std::vector<double> high = cosine(count, 3, 0.5);
    const std::vector<double> samples = add(low, high);
    const std::vector<FftComponent> components = separate_components(samples.data(), count, 2);
    if (components.size() != 2 || !(components[0].bin < components[1].bin)) {
        std::cerr << "two tones -> " << components.size() << " components\n";
        return false;
    }
    const FftComponent* first = find_bin(components, 1);
    const FftComponent* second = find_bin(components, 3);
    if (first == nullptr || second == nullptr) {
        std::cerr << "two tones bins " << components[0].bin << " " << components[1].bin << "\n";
        return false;
    }
    return close_amplitude(first->amplitude, 1.0) && close_amplitude(second->amplitude, 0.5);
}

bool expect_tie_keeps_lower() {
    const std::vector<double> samples{2.0, -1.0, 0.0, -1.0};
    const std::vector<FftComponent> components = separate_components(samples.data(), 4, 1);
    if (components.size() != 1 || components[0].bin != 1) {
        std::cerr << "tie -> ";
        if (components.empty()) {
            std::cerr << "no component\n";
        } else {
            std::cerr << "bin " << components[0].bin << "\n";
        }
        return false;
    }
    return close_amplitude(components[0].amplitude, 1.0);
}

bool expect_keep_past_end() {
    const std::vector<double> samples{1.0, 2.0, 3.0, 4.0};
    const std::vector<FftComponent> components = separate_components(samples.data(), 4, 100);
    if (components.size() != 3 || components[0].bin != 0 || components[1].bin != 1 || components[2].bin != 2) {
        std::cerr << "keep past end -> " << components.size() << " components\n";
        return false;
    }
    return true;
}

bool expect_nonfinite(double bad) {
    std::vector<double> samples{1.0, bad, 3.0, 4.0};
    const std::vector<FftComponent> components = separate_components(samples.data(), 4, 2);
    if (!components.empty()) {
        std::cerr << "non-finite -> " << components.size() << " components\n";
        return false;
    }
    return true;
}

}  // namespace

int main() {
    int status = 0;
    if (!expect_two_tones()) {
        status = 1;
    }
    if (!expect_tie_keeps_lower()) {
        status = 1;
    }
    if (!expect_keep_past_end()) {
        status = 1;
    }
    if (!expect_nonfinite(std::numeric_limits<double>::quiet_NaN())) {
        status = 1;
    }
    if (!expect_nonfinite(std::numeric_limits<double>::infinity())) {
        status = 1;
    }
    return status;
}
