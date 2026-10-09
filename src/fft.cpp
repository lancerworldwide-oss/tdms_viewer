#include "fft.hpp"

#include "pocketfft_hdronly.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace {

bool finite_samples(const double* samples, int count) {
    for (int index = 0; index < count; ++index) {
        if (!std::isfinite(samples[index])) {
            return false;
        }
    }
    return true;
}

std::vector<std::complex<double>> forward_bins(const double* samples, int count) {
    const pocketfft::shape_t shape{static_cast<std::size_t>(count)};
    const pocketfft::stride_t stride_in{static_cast<std::ptrdiff_t>(sizeof(double))};
    const pocketfft::stride_t stride_out{static_cast<std::ptrdiff_t>(sizeof(std::complex<double>))};
    const pocketfft::shape_t axes{0};
    std::vector<std::complex<double>> spectrum(static_cast<std::size_t>(count) / 2 + 1);
    pocketfft::r2c(
        shape,
        stride_in,
        stride_out,
        axes,
        pocketfft::FORWARD,
        samples,
        spectrum.data(),
        1.0,
        1);
    return spectrum;
}

double bin_amplitude(const std::complex<double>& coeff, int bin, int count) {
    const double magnitude = std::abs(coeff) / static_cast<double>(count);
    if (bin == 0 || (count % 2 == 0 && bin == count / 2)) {
        return magnitude;
    }
    return 2.0 * magnitude;
}

struct RankedBin {
    int bin = 0;
    double amplitude = 0.0;
};

}  // namespace

std::vector<FftComponent> separate_components(const double* samples, int count, int keep) {
    if (samples == nullptr || count < 1 || keep < 1 || !finite_samples(samples, count)) {
        return {};
    }

    const std::vector<std::complex<double>> spectrum = forward_bins(samples, count);
    std::vector<RankedBin> ranked(spectrum.size());
    for (int bin = 0; bin < static_cast<int>(spectrum.size()); ++bin) {
        ranked[static_cast<std::size_t>(bin)].bin = bin;
        ranked[static_cast<std::size_t>(bin)].amplitude =
            bin_amplitude(spectrum[static_cast<std::size_t>(bin)], bin, count);
    }
    std::sort(ranked.begin(), ranked.end(), [](const RankedBin& left, const RankedBin& right) {
        if (left.amplitude != right.amplitude) {
            return left.amplitude > right.amplitude;
        }
        return left.bin < right.bin;
    });
    const int kept = std::min(keep, static_cast<int>(ranked.size()));
    ranked.resize(static_cast<std::size_t>(kept));
    std::sort(ranked.begin(), ranked.end(), [](const RankedBin& left, const RankedBin& right) {
        return left.bin < right.bin;
    });

    std::vector<FftComponent> components(static_cast<std::size_t>(kept));
    for (int index = 0; index < kept; ++index) {
        components[static_cast<std::size_t>(index)].bin = ranked[static_cast<std::size_t>(index)].bin;
        components[static_cast<std::size_t>(index)].amplitude =
            ranked[static_cast<std::size_t>(index)].amplitude;
    }
    return components;
}
