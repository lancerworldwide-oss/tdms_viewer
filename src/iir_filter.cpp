#include "iir_filter.hpp"

#include "Iir.h"

#include <cmath>
#include <exception>
#include <vector>

namespace {

bool positive_finite(double value) {
    return std::isfinite(value) && value > 0.0;
}

bool is_band(IirResponse response) {
    return response == IirResponse::BandPass || response == IirResponse::BandStop;
}

bool is_chebyshev(IirFamily family) {
    return family == IirFamily::ChebyshevI || family == IirFamily::ChebyshevII;
}

template <typename Apply>
bool with_filter(const IirSettings& settings, double sample_rate, Apply apply) {
    constexpr int capacity = kMaxIirOrder;
    const int order = settings.order;
    const double cutoff = settings.cutoff;
    const double center = settings.center;
    const double width = settings.width;
    const double q = settings.q;
    const double ripple = settings.ripple;

    if (settings.family == IirFamily::Butterworth && settings.response == IirResponse::LowPass) {
        Iir::Butterworth::LowPass<capacity> filter;
        return apply(filter, [&] { filter.setup(order, sample_rate, cutoff); });
    }
    if (settings.family == IirFamily::Butterworth && settings.response == IirResponse::HighPass) {
        Iir::Butterworth::HighPass<capacity> filter;
        return apply(filter, [&] { filter.setup(order, sample_rate, cutoff); });
    }
    if (settings.family == IirFamily::Butterworth && settings.response == IirResponse::BandPass) {
        Iir::Butterworth::BandPass<capacity> filter;
        return apply(filter, [&] { filter.setup(order, sample_rate, center, width); });
    }
    if (settings.family == IirFamily::Butterworth && settings.response == IirResponse::BandStop) {
        Iir::Butterworth::BandStop<capacity> filter;
        return apply(filter, [&] { filter.setup(order, sample_rate, center, width); });
    }
    if (settings.family == IirFamily::ChebyshevI && settings.response == IirResponse::LowPass) {
        Iir::ChebyshevI::LowPass<capacity> filter;
        return apply(filter, [&] { filter.setup(order, sample_rate, cutoff, ripple); });
    }
    if (settings.family == IirFamily::ChebyshevI && settings.response == IirResponse::HighPass) {
        Iir::ChebyshevI::HighPass<capacity> filter;
        return apply(filter, [&] { filter.setup(order, sample_rate, cutoff, ripple); });
    }
    if (settings.family == IirFamily::ChebyshevI && settings.response == IirResponse::BandPass) {
        Iir::ChebyshevI::BandPass<capacity> filter;
        return apply(filter, [&] { filter.setup(order, sample_rate, center, width, ripple); });
    }
    if (settings.family == IirFamily::ChebyshevI && settings.response == IirResponse::BandStop) {
        Iir::ChebyshevI::BandStop<capacity> filter;
        return apply(filter, [&] { filter.setup(order, sample_rate, center, width, ripple); });
    }
    if (settings.family == IirFamily::ChebyshevII && settings.response == IirResponse::LowPass) {
        Iir::ChebyshevII::LowPass<capacity> filter;
        return apply(filter, [&] { filter.setup(order, sample_rate, cutoff, ripple); });
    }
    if (settings.family == IirFamily::ChebyshevII && settings.response == IirResponse::HighPass) {
        Iir::ChebyshevII::HighPass<capacity> filter;
        return apply(filter, [&] { filter.setup(order, sample_rate, cutoff, ripple); });
    }
    if (settings.family == IirFamily::ChebyshevII && settings.response == IirResponse::BandPass) {
        Iir::ChebyshevII::BandPass<capacity> filter;
        return apply(filter, [&] { filter.setup(order, sample_rate, center, width, ripple); });
    }
    if (settings.family == IirFamily::ChebyshevII && settings.response == IirResponse::BandStop) {
        Iir::ChebyshevII::BandStop<capacity> filter;
        return apply(filter, [&] { filter.setup(order, sample_rate, center, width, ripple); });
    }
    if (settings.family == IirFamily::Rbj && settings.response == IirResponse::LowPass) {
        Iir::RBJ::LowPass filter;
        return apply(filter, [&] { filter.setup(sample_rate, cutoff, q); });
    }
    if (settings.family == IirFamily::Rbj && settings.response == IirResponse::HighPass) {
        Iir::RBJ::HighPass filter;
        return apply(filter, [&] { filter.setup(sample_rate, cutoff, q); });
    }
    if (settings.family == IirFamily::Rbj && settings.response == IirResponse::BandPass) {
        Iir::RBJ::BandPass2 filter;
        return apply(filter, [&] { filter.setup(sample_rate, center, center / width); });
    }
    if (settings.family == IirFamily::Rbj && settings.response == IirResponse::BandStop) {
        Iir::RBJ::BandStop filter;
        return apply(filter, [&] { filter.setup(sample_rate, center, center / width); });
    }
    return false;
}

}  // namespace

bool iir_settings_valid(const IirSettings& settings, double sample_rate) {
    if (!positive_finite(sample_rate)) {
        return false;
    }
    const bool band = is_band(settings.response);
    const bool rbj = settings.family == IirFamily::Rbj;
    if (!rbj && (settings.order < 1 || settings.order > kMaxIirOrder)) {
        return false;
    }
    if (is_chebyshev(settings.family) && !positive_finite(settings.ripple)) {
        return false;
    }
    const double nyquist = sample_rate / 2.0;
    if (!band) {
        if (!positive_finite(settings.cutoff) || !(settings.cutoff < nyquist)) {
            return false;
        }
        if (rbj && !positive_finite(settings.q)) {
            return false;
        }
        return true;
    }
    if (!positive_finite(settings.center) || !positive_finite(settings.width)) {
        return false;
    }
    const double low = settings.center - (settings.width / 2.0);
    const double high = settings.center + (settings.width / 2.0);
    return low > 0.0 && high < nyquist;
}

bool iir_design_ok(const IirSettings& settings, double sample_rate) {
    if (!iir_settings_valid(settings, sample_rate)) {
        return false;
    }
    return with_filter(settings, sample_rate, [](auto& /*filter*/, auto setup) {
        try {
            setup();
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });
}

bool filter_channel(
    const double* samples,
    int count,
    double sample_rate,
    const IirSettings& settings,
    std::vector<double>& out) {
    out.clear();
    if (!iir_settings_valid(settings, sample_rate) || count < 0 || (count > 0 && samples == nullptr)) {
        return false;
    }
    for (int index = 0; index < count; ++index) {
        if (!std::isfinite(samples[index])) {
            return false;
        }
    }
    return with_filter(settings, sample_rate, [&](auto& filter, auto setup) {
        try {
            setup();
        } catch (const std::exception&) {
            out.clear();
            return false;
        }
        filter.reset();
        out.resize(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            const double value = filter.template filter<double>(samples[index]);
            if (!std::isfinite(value)) {
                out.clear();
                return false;
            }
            out[static_cast<std::size_t>(index)] = value;
        }
        return true;
    });
}
