#include "iir_filter.hpp"

#include "Iir.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace {

constexpr double kSampleRate = 1000.0;
constexpr double kSamples[] = {0.0, 0.2, -0.4, 0.6, -0.1, 0.8, 0.3, -0.7, 0.5, 0.1};
constexpr int kCount = 10;

bool same_samples(const std::vector<double>& out, const std::vector<double>& expect) {
    if (out == expect) {
        return true;
    }
    std::cerr << "filtered";
    for (double value : out) {
        std::cerr << " " << value;
    }
    std::cerr << "\nexpected";
    for (double value : expect) {
        std::cerr << " " << value;
    }
    std::cerr << "\n";
    return false;
}

template <typename Filter, typename Setup>
bool expect_match(const char* name, const IirSettings& settings, Setup setup) {
    std::vector<double> out;
    if (!iir_design_ok(settings, kSampleRate) ||
        !filter_channel(kSamples, kCount, kSampleRate, settings, out)) {
        std::cerr << name << " rejected a valid channel\n";
        return false;
    }
    Filter filter;
    setup(filter);
    filter.reset();
    std::vector<double> expect(static_cast<std::size_t>(kCount));
    for (int index = 0; index < kCount; ++index) {
        expect[static_cast<std::size_t>(index)] = filter.template filter<double>(kSamples[index]);
    }
    if (!same_samples(out, expect)) {
        std::cerr << name << " mismatch\n";
        return false;
    }
    return true;
}

bool expect_reject(const char* name, const IirSettings& settings, const double* samples, int count) {
    std::vector<double> out{1.0, 2.0, 3.0};
    if (iir_settings_valid(settings, kSampleRate) || iir_design_ok(settings, kSampleRate) ||
        filter_channel(samples, count, kSampleRate, settings, out) || !out.empty()) {
        std::cerr << name << " was accepted\n";
        return false;
    }
    return true;
}

IirSettings lowpass(IirFamily family, int order, double cutoff, double q, double ripple) {
    IirSettings settings;
    settings.family = family;
    settings.response = IirResponse::LowPass;
    settings.order = order;
    settings.cutoff = cutoff;
    settings.q = q;
    settings.ripple = ripple;
    return settings;
}

IirSettings bandpass(IirFamily family, int order, double center, double width, double ripple) {
    IirSettings settings;
    settings.family = family;
    settings.response = IirResponse::BandPass;
    settings.order = order;
    settings.center = center;
    settings.width = width;
    settings.ripple = ripple;
    return settings;
}

}  // namespace

int main() {
    int status = 0;
    constexpr int capacity = kMaxIirOrder;
    constexpr double cutoff = 40.0;
    constexpr double center = 100.0;
    constexpr double width = 40.0;
    constexpr double ripple_i = 1.0;
    constexpr double ripple_ii = 20.0;
    const double q = 1.0 / std::sqrt(2.0);

    if (!expect_match<Iir::Butterworth::LowPass<capacity>>(
            "Butterworth lowpass",
            lowpass(IirFamily::Butterworth, 4, cutoff, q, ripple_i),
            [&](auto& filter) { filter.setup(4, kSampleRate, cutoff); })) {
        status = 1;
    }
    IirSettings highpass_settings = lowpass(IirFamily::Butterworth, 2, cutoff, q, ripple_i);
    highpass_settings.response = IirResponse::HighPass;
    if (!expect_match<Iir::Butterworth::HighPass<capacity>>(
            "Butterworth highpass",
            highpass_settings,
            [&](auto& filter) { filter.setup(2, kSampleRate, cutoff); })) {
        status = 1;
    }
    if (!expect_match<Iir::Butterworth::BandPass<capacity>>(
            "Butterworth bandpass",
            bandpass(IirFamily::Butterworth, 3, center, width, ripple_i),
            [&](auto& filter) { filter.setup(3, kSampleRate, center, width); })) {
        status = 1;
    }
    IirSettings bandstop_settings = bandpass(IirFamily::Butterworth, 3, center, width, ripple_i);
    bandstop_settings.response = IirResponse::BandStop;
    if (!expect_match<Iir::Butterworth::BandStop<capacity>>(
            "Butterworth bandstop",
            bandstop_settings,
            [&](auto& filter) { filter.setup(3, kSampleRate, center, width); })) {
        status = 1;
    }

    if (!expect_match<Iir::ChebyshevI::LowPass<capacity>>(
            "Chebyshev I lowpass",
            lowpass(IirFamily::ChebyshevI, 4, cutoff, q, ripple_i),
            [&](auto& filter) { filter.setup(4, kSampleRate, cutoff, ripple_i); })) {
        status = 1;
    }
    highpass_settings = lowpass(IirFamily::ChebyshevI, 4, cutoff, q, ripple_i);
    highpass_settings.response = IirResponse::HighPass;
    if (!expect_match<Iir::ChebyshevI::HighPass<capacity>>(
            "Chebyshev I highpass",
            highpass_settings,
            [&](auto& filter) { filter.setup(4, kSampleRate, cutoff, ripple_i); })) {
        status = 1;
    }
    if (!expect_match<Iir::ChebyshevI::BandPass<capacity>>(
            "Chebyshev I bandpass",
            bandpass(IirFamily::ChebyshevI, 4, center, width, ripple_i),
            [&](auto& filter) { filter.setup(4, kSampleRate, center, width, ripple_i); })) {
        status = 1;
    }
    bandstop_settings = bandpass(IirFamily::ChebyshevI, 4, center, width, ripple_i);
    bandstop_settings.response = IirResponse::BandStop;
    if (!expect_match<Iir::ChebyshevI::BandStop<capacity>>(
            "Chebyshev I bandstop",
            bandstop_settings,
            [&](auto& filter) { filter.setup(4, kSampleRate, center, width, ripple_i); })) {
        status = 1;
    }

    if (!expect_match<Iir::ChebyshevII::LowPass<capacity>>(
            "Chebyshev II lowpass",
            lowpass(IirFamily::ChebyshevII, 5, cutoff, q, ripple_ii),
            [&](auto& filter) { filter.setup(5, kSampleRate, cutoff, ripple_ii); })) {
        status = 1;
    }
    highpass_settings = lowpass(IirFamily::ChebyshevII, 5, cutoff, q, ripple_ii);
    highpass_settings.response = IirResponse::HighPass;
    if (!expect_match<Iir::ChebyshevII::HighPass<capacity>>(
            "Chebyshev II highpass",
            highpass_settings,
            [&](auto& filter) { filter.setup(5, kSampleRate, cutoff, ripple_ii); })) {
        status = 1;
    }
    if (!expect_match<Iir::ChebyshevII::BandPass<capacity>>(
            "Chebyshev II bandpass",
            bandpass(IirFamily::ChebyshevII, 5, center, width, ripple_ii),
            [&](auto& filter) { filter.setup(5, kSampleRate, center, width, ripple_ii); })) {
        status = 1;
    }
    bandstop_settings = bandpass(IirFamily::ChebyshevII, 5, center, width, ripple_ii);
    bandstop_settings.response = IirResponse::BandStop;
    if (!expect_match<Iir::ChebyshevII::BandStop<capacity>>(
            "Chebyshev II bandstop",
            bandstop_settings,
            [&](auto& filter) { filter.setup(5, kSampleRate, center, width, ripple_ii); })) {
        status = 1;
    }

    if (!expect_match<Iir::RBJ::LowPass>(
            "RBJ lowpass",
            lowpass(IirFamily::Rbj, 0, cutoff, q, ripple_i),
            [&](auto& filter) { filter.setup(kSampleRate, cutoff, q); })) {
        status = 1;
    }
    highpass_settings = lowpass(IirFamily::Rbj, 0, cutoff, q, ripple_i);
    highpass_settings.response = IirResponse::HighPass;
    if (!expect_match<Iir::RBJ::HighPass>(
            "RBJ highpass",
            highpass_settings,
            [&](auto& filter) { filter.setup(kSampleRate, cutoff, q); })) {
        status = 1;
    }
    if (!expect_match<Iir::RBJ::BandPass2>(
            "RBJ bandpass",
            bandpass(IirFamily::Rbj, 0, center, width, ripple_i),
            [&](auto& filter) { filter.setup(kSampleRate, center, center / width); })) {
        status = 1;
    }
    bandstop_settings = bandpass(IirFamily::Rbj, 0, center, width, ripple_i);
    bandstop_settings.response = IirResponse::BandStop;
    if (!expect_match<Iir::RBJ::BandStop>(
            "RBJ bandstop",
            bandstop_settings,
            [&](auto& filter) { filter.setup(kSampleRate, center, center / width); })) {
        status = 1;
    }

    IirSettings settings = lowpass(IirFamily::Butterworth, 0, cutoff, q, ripple_i);
    if (!expect_reject("order 0", settings, kSamples, kCount)) {
        status = 1;
    }
    settings.order = 11;
    if (!expect_reject("order 11", settings, kSamples, kCount)) {
        status = 1;
    }
    settings.order = 4;
    settings.cutoff = kSampleRate / 2.0;
    if (!expect_reject("cutoff at Nyquist", settings, kSamples, kCount)) {
        status = 1;
    }
    settings.cutoff = kSampleRate;
    if (!expect_reject("cutoff above Nyquist", settings, kSamples, kCount)) {
        status = 1;
    }

    settings = bandpass(IirFamily::Butterworth, 4, 10.0, 30.0, ripple_i);
    if (!expect_reject("band crosses 0", settings, kSamples, kCount)) {
        status = 1;
    }
    settings.center = 450.0;
    settings.width = 120.0;
    if (!expect_reject("band crosses Nyquist", settings, kSamples, kCount)) {
        status = 1;
    }

    settings = lowpass(IirFamily::ChebyshevI, 4, cutoff, q, 0.0);
    if (!expect_reject("passband ripple 0", settings, kSamples, kCount)) {
        status = 1;
    }
    settings = lowpass(IirFamily::ChebyshevII, 4, cutoff, q, 0.0);
    if (!expect_reject("stopband ripple 0", settings, kSamples, kCount)) {
        status = 1;
    }
    settings = lowpass(IirFamily::Rbj, 0, cutoff, 0.0, ripple_i);
    if (!expect_reject("Q 0", settings, kSamples, kCount)) {
        status = 1;
    }

    double nonfinite[kCount];
    for (int index = 0; index < kCount; ++index) {
        nonfinite[index] = kSamples[index];
    }
    nonfinite[3] = std::numeric_limits<double>::quiet_NaN();
    settings = lowpass(IirFamily::Butterworth, 4, cutoff, q, ripple_i);
    std::vector<double> out{1.0};
    if (filter_channel(nonfinite, kCount, kSampleRate, settings, out) || !out.empty()) {
        std::cerr << "non-finite sample was filtered\n";
        status = 1;
    }

    return status;
}
