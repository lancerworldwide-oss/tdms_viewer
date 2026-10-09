#pragma once

#include <vector>

enum class IirFamily {
    Butterworth,
    ChebyshevI,
    ChebyshevII,
    Rbj,
};

enum class IirResponse {
    LowPass,
    HighPass,
    BandPass,
    BandStop,
};

struct IirSettings {
    IirFamily family = IirFamily::Butterworth;
    IirResponse response = IirResponse::LowPass;
    int order = 4;
    double cutoff = 1.0;
    double center = 1.0;
    double width = 1.0;
    double q = 1.0;
    double ripple = 1.0;
};

inline constexpr int kMaxIirOrder = 10;

bool iir_settings_valid(const IirSettings& settings, double sample_rate);

bool iir_design_ok(const IirSettings& settings, double sample_rate);

bool filter_channel(
    const double* samples,
    int count,
    double sample_rate,
    const IirSettings& settings,
    std::vector<double>& out);
