#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>
#include <implot_internal.h>
#include <tdms.hpp>

#include "duration.hpp"
#include "export_range.hpp"
#include "fft.hpp"
#include "frequency.hpp"
#include "iir_filter.hpp"
#include "rolling_abs_mean.hpp"
#include "rolling_rms.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

namespace {

enum class SampleKind {
    NotPlotted,
    Float,
    Double,
    Int8,
    Int16,
    Int32,
    Uint8,
    Uint16,
    Uint32,
    Uint64,
};

struct IntegerSamples {
    std::optional<std::string> path;
    std::vector<double> values;
};

enum class ViewAction {
    None,
    Reset,
    Frame,
};

struct FrameLimits {
    double x_min = 0.0;
    double x_max = 0.0;
    double y_min = 0.0;
    double y_max = 0.0;
};

struct SetFrameDialog {
    bool request_open = false;
    bool invalid = false;
    char start[32] = {};
    char stop[32] = {};
};

struct RmsDialog {
    bool request_open = false;
    bool invalid = false;
    char length[32] = {};
};

struct StoredRange {
    bool set = false;
    std::string path;
    int start = 0;
    int stop = 0;
};

struct FrequencyDialog {
    bool request_open = false;
    bool fit = false;
    double hertz = 0.0;
};

constexpr int kIirCount = 16;

struct IirOverlay {
    bool on = false;
    int order = 4;
    double cutoff = 1.0;
    double center = 1.0;
    double width = 1.0;
    double q = 1.0 / std::sqrt(2.0);
    double ripple = 1.0;
    std::vector<double> values;
    bool cache_set = false;
    bool cache_failed = false;
    std::string cache_path;
    std::size_t cache_count = 0;
    double cache_dt = 0.0;
    int cache_order = 0;
    double cache_cutoff = 0.0;
    double cache_center = 0.0;
    double cache_width = 0.0;
    double cache_q = 0.0;
    double cache_ripple = 0.0;
};

struct IirDialog {
    bool request_open = false;
    bool invalid = false;
    int index = -1;
    char order[32] = {};
    char cutoff[64] = {};
    char center[64] = {};
    char width[64] = {};
    char q[64] = {};
    char ripple[64] = {};
};

struct Viewer {
    std::unique_ptr<TDMS::file> capture;
    std::optional<std::string> selected_path;
    std::string error;
    IntegerSamples integer_samples;
    ViewAction view_action = ViewAction::None;
    FrameLimits frame_limits;
    bool plot_x_known = false;
    double plot_x_min = 0.0;
    double plot_x_max = 0.0;
    SetFrameDialog set_frame;
    StoredRange stored_range;
    bool rms_on = false;
    int rms_window = 0;
    RmsDialog rms_dialog;
    std::vector<double> rms_indexes;
    std::vector<double> rms_values;
    bool avg_abs_on = false;
    int avg_abs_window = 0;
    RmsDialog avg_abs_dialog;
    std::vector<double> avg_abs_indexes;
    std::vector<double> avg_abs_values;
    bool fft_on = false;
    int fft_count = 0;
    RmsDialog fft_dialog;
    bool fft_hist_known = false;
    std::string fft_hist_path;
    int fft_hist_first = 0;
    int fft_hist_last = 0;
    bool duration_on = false;
    std::optional<std::string> duration_path;
    std::optional<int> duration_start;
    std::optional<int> duration_end;
    FrequencyDialog frequency_dialog;
    IirOverlay iir[kIirCount];
    IirDialog iir_dialog;
};

constexpr const char* frame_range_error =
    "Start and stop must be whole numbers from 0 through the last sample, and start must not be after stop.";
constexpr const char* rms_window_error = "Window length must be a positive whole number.";
constexpr const char* fft_count_error = "Component count must be a positive whole number.";

SampleKind sample_kind(const std::string& type_name) {
    if (type_name == "tdsTypeSingleFloat") {
        return SampleKind::Float;
    }
    if (type_name == "tdsTypeDoubleFloat") {
        return SampleKind::Double;
    }
    if (type_name == "tdsTypeI8") {
        return SampleKind::Int8;
    }
    if (type_name == "tdsTypeI16") {
        return SampleKind::Int16;
    }
    if (type_name == "tdsTypeI32") {
        return SampleKind::Int32;
    }
    if (type_name == "tdsTypeU8") {
        return SampleKind::Uint8;
    }
    if (type_name == "tdsTypeU16") {
        return SampleKind::Uint16;
    }
    if (type_name == "tdsTypeU32") {
        return SampleKind::Uint32;
    }
    if (type_name == "tdsTypeU64") {
        return SampleKind::Uint64;
    }
    return SampleKind::NotPlotted;
}

bool is_integer_kind(SampleKind kind) {
    switch (kind) {
    case SampleKind::Int8:
    case SampleKind::Int16:
    case SampleKind::Int32:
    case SampleKind::Uint8:
    case SampleKind::Uint16:
    case SampleKind::Uint32:
    case SampleKind::Uint64:
        return true;
    case SampleKind::NotPlotted:
    case SampleKind::Float:
    case SampleKind::Double:
        return false;
    }
    return false;
}

template <typename T>
void copy_samples(const void* data, std::size_t count, std::vector<double>& out) {
    const auto* samples = static_cast<const T*>(data);
    out.resize(count);
    for (std::size_t index = 0; index < count; ++index) {
        out[index] = static_cast<double>(samples[index]);
    }
}

void fill_integer_samples(TDMS::object& object, SampleKind kind, IntegerSamples& cache) {
    const std::string path = object.get_path();
    if (cache.path.has_value() && *cache.path == path) {
        return;
    }
    cache.values.clear();
    cache.path = path;
    const std::size_t count = object.number_values();
    const void* data = object.data();
    if (data == nullptr || count == 0) {
        return;
    }
    switch (kind) {
    case SampleKind::Int8:
        copy_samples<std::int8_t>(data, count, cache.values);
        break;
    case SampleKind::Int16:
        copy_samples<std::int16_t>(data, count, cache.values);
        break;
    case SampleKind::Int32:
        copy_samples<std::int32_t>(data, count, cache.values);
        break;
    case SampleKind::Uint8:
        copy_samples<std::uint8_t>(data, count, cache.values);
        break;
    case SampleKind::Uint16:
        copy_samples<std::uint16_t>(data, count, cache.values);
        break;
    case SampleKind::Uint32:
        copy_samples<std::uint32_t>(data, count, cache.values);
        break;
    case SampleKind::Uint64:
        copy_samples<std::uint64_t>(data, count, cache.values);
        break;
    case SampleKind::NotPlotted:
    case SampleKind::Float:
    case SampleKind::Double:
        cache.path.reset();
        break;
    }
}

void clear_duration_markers(Viewer& viewer) {
    viewer.duration_path.reset();
    viewer.duration_start.reset();
    viewer.duration_end.reset();
}

void clear_iir_caches(Viewer& viewer) {
    for (IirOverlay& overlay : viewer.iir) {
        overlay.values.clear();
        overlay.cache_set = false;
        overlay.cache_failed = false;
        overlay.cache_path.clear();
        overlay.cache_count = 0;
    }
}

void clear_selection(Viewer& viewer) {
    viewer.selected_path.reset();
    viewer.integer_samples.path.reset();
    viewer.integer_samples.values.clear();
    viewer.stored_range = {};
    clear_duration_markers(viewer);
    clear_iir_caches(viewer);
}

bool load_capture(Viewer& viewer, const std::string& path) {
    try {
        auto loaded = std::make_unique<TDMS::file>(path);
        viewer.capture = std::move(loaded);
        clear_selection(viewer);
        viewer.error.clear();
        return true;
    } catch (const std::exception& ex) {
        viewer.error = ex.what();
        return false;
    }
}

std::optional<std::string> choose_tdms_path(GLFWwindow* window) {
    char path[MAX_PATH] = {};
    OPENFILENAMEA dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = glfwGetWin32Window(window);
    dialog.lpstrFilter = "TDMS\0*.tdms\0All files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&dialog) != TRUE) {
        return std::nullopt;
    }
    return std::string(path);
}

std::optional<std::string> choose_save_path(GLFWwindow* window, const char* filter, const char* extension) {
    char path[MAX_PATH] = {};
    OPENFILENAMEA dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = glfwGetWin32Window(window);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = extension;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameA(&dialog) != TRUE) {
        return std::nullopt;
    }
    return std::string(path);
}

TDMS::object* find_selected(TDMS::file& capture, const std::string& path) {
    for (TDMS::object* object : capture) {
        if (object->get_path() == path) {
            return object;
        }
    }
    return nullptr;
}

bool is_plotted_channel(TDMS::object& object) {
    const SampleKind kind = sample_kind(object.data_type());
    const std::size_t count = object.number_values();
    if (kind == SampleKind::NotPlotted) {
        return false;
    }
    if (count > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    return count > 0 && object.data() != nullptr;
}

TDMS::object* plotted_object(Viewer& viewer) {
    if (!viewer.selected_path.has_value() || viewer.capture == nullptr) {
        return nullptr;
    }
    TDMS::object* object = find_selected(*viewer.capture, *viewer.selected_path);
    if (object == nullptr || !is_plotted_channel(*object)) {
        return nullptr;
    }
    return object;
}

std::optional<int> parse_frame_text(const char* text) {
    std::string_view view(text);
    while (!view.empty() && (view.front() == ' ' || view.front() == '\t')) {
        view.remove_prefix(1);
    }
    while (!view.empty() && (view.back() == ' ' || view.back() == '\t')) {
        view.remove_suffix(1);
    }
    if (view.empty()) {
        return std::nullopt;
    }
    int value = 0;
    const char* const begin = view.data();
    const char* const end = begin + view.size();
    const std::from_chars_result result = std::from_chars(begin, end, value);
    if (result.ec != std::errc() || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

void expand_flat_limits(double& min_value, double& max_value) {
    if (min_value == max_value) {
        min_value -= 0.5;
        max_value += 0.5;
    }
}

int nearest_sample_index(double edge, int last) {
    const double high = static_cast<double>(last);
    if (edge < 0.0) {
        edge = 0.0;
    } else if (edge > high) {
        edge = high;
    }
    return static_cast<int>(std::llround(edge));
}

template <typename T>
bool finite_extent(const T* samples, int start, int stop, double& y_min, double& y_max) {
    bool any = false;
    for (int index = start; index <= stop; ++index) {
        const double sample = static_cast<double>(samples[index]);
        if (!std::isfinite(sample)) {
            continue;
        }
        if (!any) {
            y_min = sample;
            y_max = sample;
            any = true;
            continue;
        }
        if (sample < y_min) {
            y_min = sample;
        }
        if (sample > y_max) {
            y_max = sample;
        }
    }
    return any;
}

bool finite_y_limits(
    TDMS::object& object,
    IntegerSamples& integer_samples,
    int start,
    int stop,
    double& y_min,
    double& y_max) {
    const SampleKind kind = sample_kind(object.data_type());
    if (kind == SampleKind::Float) {
        return finite_extent(static_cast<const float*>(object.data()), start, stop, y_min, y_max);
    }
    if (kind == SampleKind::Double) {
        return finite_extent(static_cast<const double*>(object.data()), start, stop, y_min, y_max);
    }
    if (!is_integer_kind(kind)) {
        return false;
    }
    fill_integer_samples(object, kind, integer_samples);
    if (integer_samples.values.size() != object.number_values()) {
        return false;
    }
    return finite_extent(integer_samples.values.data(), start, stop, y_min, y_max);
}

bool try_set_frame(Viewer& viewer) {
    TDMS::object* object = plotted_object(viewer);
    const std::optional<int> start = parse_frame_text(viewer.set_frame.start);
    const std::optional<int> stop = parse_frame_text(viewer.set_frame.stop);
    if (object == nullptr || !start.has_value() || !stop.has_value()) {
        return false;
    }
    const int last = static_cast<int>(object->number_values() - 1);
    if (*start < 0 || *stop < 0 || *start > last || *stop > last || *start > *stop) {
        return false;
    }
    double y_min = 0.0;
    double y_max = 0.0;
    if (!finite_y_limits(*object, viewer.integer_samples, *start, *stop, y_min, y_max)) {
        return false;
    }
    double x_min = static_cast<double>(*start);
    double x_max = static_cast<double>(*stop);
    expand_flat_limits(x_min, x_max);
    expand_flat_limits(y_min, y_max);
    viewer.frame_limits = FrameLimits{x_min, x_max, y_min, y_max};
    viewer.view_action = ViewAction::Frame;
    viewer.stored_range.set = true;
    viewer.stored_range.path = object->get_path();
    viewer.stored_range.start = *start;
    viewer.stored_range.stop = *stop;
    return true;
}

void open_set_frame(Viewer& viewer, TDMS::object& object) {
    const int last = static_cast<int>(object.number_values() - 1);
    int start = 0;
    int stop = last;
    if (viewer.plot_x_known && std::isfinite(viewer.plot_x_min) && std::isfinite(viewer.plot_x_max)) {
        start = nearest_sample_index(viewer.plot_x_min, last);
        stop = nearest_sample_index(viewer.plot_x_max, last);
    }
    std::snprintf(viewer.set_frame.start, sizeof(viewer.set_frame.start), "%d", start);
    std::snprintf(viewer.set_frame.stop, sizeof(viewer.set_frame.stop), "%d", stop);
    viewer.set_frame.invalid = false;
    viewer.set_frame.request_open = true;
}

void draw_set_frame_dialog(Viewer& viewer) {
    if (viewer.set_frame.request_open) {
        ImGui::OpenPopup("Set Frame");
        viewer.set_frame.request_open = false;
    }
    if (!ImGui::BeginPopupModal("Set Frame", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::InputText("Start frame", viewer.set_frame.start, sizeof(viewer.set_frame.start));
    ImGui::InputText("Stop frame", viewer.set_frame.stop, sizeof(viewer.set_frame.stop));
    if (viewer.set_frame.invalid) {
        ImGui::TextWrapped("%s", frame_range_error);
    }
    if (ImGui::Button("OK")) {
        if (try_set_frame(viewer)) {
            viewer.set_frame.invalid = false;
            ImGui::CloseCurrentPopup();
        } else {
            viewer.set_frame.invalid = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        viewer.set_frame.invalid = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void open_rms_dialog(Viewer& viewer) {
    if (viewer.rms_window >= 1) {
        std::snprintf(
            viewer.rms_dialog.length, sizeof(viewer.rms_dialog.length), "%d", viewer.rms_window);
    } else {
        std::snprintf(viewer.rms_dialog.length, sizeof(viewer.rms_dialog.length), "1");
    }
    viewer.rms_dialog.invalid = false;
    viewer.rms_dialog.request_open = true;
}

bool accept_rms_window(Viewer& viewer) {
    const std::optional<int> length = parse_frame_text(viewer.rms_dialog.length);
    if (!length.has_value() || *length < 1) {
        return false;
    }
    viewer.rms_window = *length;
    viewer.rms_on = true;
    return true;
}

void draw_rms_dialog(Viewer& viewer) {
    if (viewer.rms_dialog.request_open) {
        ImGui::OpenPopup("RMS");
        viewer.rms_dialog.request_open = false;
    }
    if (!ImGui::BeginPopupModal("RMS", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::InputText("Window length", viewer.rms_dialog.length, sizeof(viewer.rms_dialog.length));
    if (viewer.rms_dialog.invalid) {
        ImGui::TextWrapped("%s", rms_window_error);
    }
    if (ImGui::Button("OK")) {
        if (accept_rms_window(viewer)) {
            viewer.rms_dialog.invalid = false;
            ImGui::CloseCurrentPopup();
        } else {
            viewer.rms_dialog.invalid = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        viewer.rms_dialog.invalid = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

template <typename T>
void plot_rms_overlay(const T* samples, int count, Viewer& viewer) {
    if (!viewer.rms_on || viewer.rms_window < 1 || samples == nullptr) {
        return;
    }
    const ImPlotRect limits = ImPlot::GetPlotLimits();
    int first = 0;
    int last = 0;
    if (!view_sample_bounds(limits.X.Min, limits.X.Max, count, first, last)) {
        return;
    }
    rolling_rms(samples, first, last, viewer.rms_window, viewer.rms_indexes, viewer.rms_values);
    if (viewer.rms_indexes.empty()) {
        return;
    }
    ImPlot::PlotLine(
        "RMS",
        viewer.rms_indexes.data(),
        viewer.rms_values.data(),
        static_cast<int>(viewer.rms_indexes.size()));
}

void open_avg_abs_dialog(Viewer& viewer) {
    if (viewer.avg_abs_window >= 1) {
        std::snprintf(
            viewer.avg_abs_dialog.length, sizeof(viewer.avg_abs_dialog.length), "%d", viewer.avg_abs_window);
    } else {
        std::snprintf(viewer.avg_abs_dialog.length, sizeof(viewer.avg_abs_dialog.length), "1");
    }
    viewer.avg_abs_dialog.invalid = false;
    viewer.avg_abs_dialog.request_open = true;
}

bool accept_avg_abs_window(Viewer& viewer) {
    const std::optional<int> length = parse_frame_text(viewer.avg_abs_dialog.length);
    if (!length.has_value() || *length < 1) {
        return false;
    }
    viewer.avg_abs_window = *length;
    viewer.avg_abs_on = true;
    return true;
}

void draw_avg_abs_dialog(Viewer& viewer) {
    if (viewer.avg_abs_dialog.request_open) {
        ImGui::OpenPopup("AVG(ABS(Y))");
        viewer.avg_abs_dialog.request_open = false;
    }
    if (!ImGui::BeginPopupModal("AVG(ABS(Y))", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::InputText("Window length", viewer.avg_abs_dialog.length, sizeof(viewer.avg_abs_dialog.length));
    if (viewer.avg_abs_dialog.invalid) {
        ImGui::TextWrapped("%s", rms_window_error);
    }
    if (ImGui::Button("OK")) {
        if (accept_avg_abs_window(viewer)) {
            viewer.avg_abs_dialog.invalid = false;
            ImGui::CloseCurrentPopup();
        } else {
            viewer.avg_abs_dialog.invalid = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        viewer.avg_abs_dialog.invalid = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

template <typename T>
void plot_avg_abs_overlay(const T* samples, int count, Viewer& viewer) {
    if (!viewer.avg_abs_on || viewer.avg_abs_window < 1 || samples == nullptr) {
        return;
    }
    const ImPlotRect limits = ImPlot::GetPlotLimits();
    int first = 0;
    int last = 0;
    if (!view_sample_bounds(limits.X.Min, limits.X.Max, count, first, last)) {
        return;
    }
    rolling_abs_mean(
        samples, first, last, viewer.avg_abs_window, viewer.avg_abs_indexes, viewer.avg_abs_values);
    if (viewer.avg_abs_indexes.empty()) {
        return;
    }
    ImPlot::PlotLine(
        "AVG(ABS(Y))",
        viewer.avg_abs_indexes.data(),
        viewer.avg_abs_values.data(),
        static_cast<int>(viewer.avg_abs_indexes.size()));
}

void open_fft_dialog(Viewer& viewer) {
    if (viewer.fft_count >= 1) {
        std::snprintf(viewer.fft_dialog.length, sizeof(viewer.fft_dialog.length), "%d", viewer.fft_count);
    } else {
        std::snprintf(viewer.fft_dialog.length, sizeof(viewer.fft_dialog.length), "1");
    }
    viewer.fft_dialog.invalid = false;
    viewer.fft_dialog.request_open = true;
}

bool accept_fft_count(Viewer& viewer) {
    const std::optional<int> count = parse_frame_text(viewer.fft_dialog.length);
    if (!count.has_value() || *count < 1) {
        return false;
    }
    viewer.fft_count = *count;
    viewer.fft_on = true;
    return true;
}

void draw_fft_dialog(Viewer& viewer) {
    if (viewer.fft_dialog.request_open) {
        ImGui::OpenPopup("FFT");
        viewer.fft_dialog.request_open = false;
    }
    if (!ImGui::BeginPopupModal("FFT", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::InputText("Component count", viewer.fft_dialog.length, sizeof(viewer.fft_dialog.length));
    if (viewer.fft_dialog.invalid) {
        ImGui::TextWrapped("%s", fft_count_error);
    }
    if (ImGui::Button("OK")) {
        if (accept_fft_count(viewer)) {
            viewer.fft_dialog.invalid = false;
            ImGui::CloseCurrentPopup();
        } else {
            viewer.fft_dialog.invalid = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        viewer.fft_dialog.invalid = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

std::optional<double> waveform_increment(TDMS::object& object) {
    const auto properties = object.get_properties();
    const auto found = properties.find(std::string("wf_increment"));
    if (found == properties.end() || found->second == nullptr || found->second->value == nullptr) {
        return std::nullopt;
    }
    const TDMS::object::property& property = *found->second;
    double value = 0.0;
    if (property.data_type.name == "tdsTypeDoubleFloat" && property.data_type.ctype_length == sizeof(double)) {
        value = *static_cast<const double*>(property.value);
    } else if (property.data_type.name == "tdsTypeSingleFloat" &&
               property.data_type.ctype_length == sizeof(float)) {
        value = static_cast<double>(*static_cast<const float*>(property.value));
    } else {
        return std::nullopt;
    }
    if (!std::isfinite(value) || !(value > 0.0)) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> plotted_sample(TDMS::object& object, Viewer& viewer, int index) {
    if (index < 0) {
        return std::nullopt;
    }
    const std::size_t count = object.number_values();
    if (static_cast<std::size_t>(index) >= count || object.data() == nullptr) {
        return std::nullopt;
    }
    const SampleKind kind = sample_kind(object.data_type());
    double value = 0.0;
    if (kind == SampleKind::Float) {
        value = static_cast<double>(static_cast<const float*>(object.data())[index]);
    } else if (kind == SampleKind::Double) {
        value = static_cast<const double*>(object.data())[index];
    } else if (is_integer_kind(kind)) {
        fill_integer_samples(object, kind, viewer.integer_samples);
        if (viewer.integer_samples.values.size() != count) {
            return std::nullopt;
        }
        value = viewer.integer_samples.values[static_cast<std::size_t>(index)];
    } else {
        return std::nullopt;
    }
    if (!std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

template <typename T>
int closest_sample(const T* samples, int count, double mouse_x, const ImVec2& mouse_px) {
    if (samples == nullptr || count <= 0) {
        return -1;
    }
    const int last = count - 1;
    const int seed = nearest_sample_index(mouse_x, last);
    const ImPlotRect limits = ImPlot::GetPlotLimits();
    const double x_span = limits.X.Max - limits.X.Min;
    const float plot_width = ImPlot::GetPlotSize().x;
    const double pixels_per_index =
        plot_width > 0.0f && x_span != 0.0 ? static_cast<double>(plot_width) / std::fabs(x_span) : 0.0;

    int best = -1;
    double best_dist = 0.0;
    for (int radius = 0; radius <= last; ++radius) {
        if (best >= 0 && pixels_per_index > 0.0) {
            const double index_gap = static_cast<double>(radius) - 0.5;
            if (index_gap > 0.0) {
                const double x_dist = index_gap * pixels_per_index;
                if (x_dist * x_dist >= best_dist) {
                    break;
                }
            }
        }
        const int candidates[2] = {seed - radius, seed + radius};
        const int candidate_count = radius == 0 ? 1 : 2;
        for (int n = 0; n < candidate_count; ++n) {
            const int index = candidates[n];
            if (index < 0 || index > last) {
                continue;
            }
            const double y = static_cast<double>(samples[index]);
            if (!std::isfinite(y)) {
                continue;
            }
            const ImVec2 sample_px = ImPlot::PlotToPixels(static_cast<double>(index), y);
            const double dx = static_cast<double>(sample_px.x - mouse_px.x);
            const double dy = static_cast<double>(sample_px.y - mouse_px.y);
            const double dist = dx * dx + dy * dy;
            if (best < 0 || dist < best_dist) {
                best = index;
                best_dist = dist;
            }
        }
    }
    return best;
}

int closest_clicked_sample(TDMS::object& object, Viewer& viewer, const ImPlotPoint& mouse, const ImVec2& mouse_px) {
    const int count = static_cast<int>(object.number_values());
    const SampleKind kind = sample_kind(object.data_type());
    if (kind == SampleKind::Float) {
        return closest_sample(static_cast<const float*>(object.data()), count, mouse.x, mouse_px);
    }
    if (kind == SampleKind::Double) {
        return closest_sample(static_cast<const double*>(object.data()), count, mouse.x, mouse_px);
    }
    if (!is_integer_kind(kind)) {
        return -1;
    }
    fill_integer_samples(object, kind, viewer.integer_samples);
    if (static_cast<int>(viewer.integer_samples.values.size()) != count) {
        return -1;
    }
    return closest_sample(viewer.integer_samples.values.data(), count, mouse.x, mouse_px);
}

void place_duration_marker(Viewer& viewer, const std::string& path, int index) {
    if (!viewer.duration_path.has_value() || *viewer.duration_path != path || !viewer.duration_start.has_value()) {
        viewer.duration_path = path;
        viewer.duration_start = index;
        viewer.duration_end.reset();
        return;
    }
    if (!viewer.duration_end.has_value()) {
        viewer.duration_end = index;
    }
}

void draw_duration(TDMS::object& object, Viewer& viewer) {
    if (!viewer.duration_on || !viewer.duration_path.has_value() || *viewer.duration_path != object.get_path() ||
        !viewer.duration_start.has_value()) {
        return;
    }
    const std::optional<double> increment = waveform_increment(object);
    const std::optional<double> start_y = plotted_sample(object, viewer, *viewer.duration_start);
    if (!increment.has_value() || !start_y.has_value()) {
        return;
    }
    double xs[2] = {static_cast<double>(*viewer.duration_start), 0.0};
    double ys[2] = {*start_y, 0.0};
    int point_count = 1;
    if (viewer.duration_end.has_value()) {
        const std::optional<double> end_y = plotted_sample(object, viewer, *viewer.duration_end);
        if (!end_y.has_value()) {
            return;
        }
        xs[1] = static_cast<double>(*viewer.duration_end);
        ys[1] = *end_y;
        point_count = 2;
    }
    ImPlotSpec spec;
    spec.Marker = ImPlotMarker_Circle;
    spec.Flags = ImPlotItemFlags_NoFit;
    if (point_count == 1) {
        spec.Flags |= ImPlotItemFlags_NoLegend;
        ImPlot::PlotScatter("##duration_start", xs, ys, 1, spec);
        return;
    }
    ImPlot::PlotLine("Duration", xs, ys, point_count, spec);
    const std::string label =
        format_duration(duration_seconds(*viewer.duration_start, *viewer.duration_end, *increment));
    const ImVec4 text_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImPlot::Annotation(
        (xs[0] + xs[1]) * 0.5,
        (ys[0] + ys[1]) * 0.5,
        text_color,
        ImVec2(0.0f, 0.0f),
        true,
        "%s",
        label.c_str());
}

void handle_duration_click(TDMS::object& object, Viewer& viewer) {
    if (!viewer.duration_on || !waveform_increment(object).has_value()) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.MouseReleased[ImGuiMouseButton_Left]) {
        return;
    }
    const ImPlotPlot& plot = *GImPlot->CurrentPlot;
    const float threshold = io.MouseDragThreshold;
    const bool within_drag = io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < threshold * threshold;
    const bool release_inside = plot.Hovered && plot.PlotRect.Contains(io.MousePos);
    const bool press_inside = plot.PlotRect.Contains(io.MouseClickedPos[ImGuiMouseButton_Left]);
    if (!within_drag || !release_inside || !press_inside) {
        return;
    }
    const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
    if (!std::isfinite(mouse.x) || !std::isfinite(mouse.y)) {
        return;
    }
    const int index = closest_clicked_sample(object, viewer, mouse, io.MousePos);
    if (index < 0) {
        return;
    }
    place_duration_marker(viewer, object.get_path(), index);
}

struct IirSpec {
    const char* title;
    IirFamily family;
    IirResponse response;
    const char* error;
};

constexpr IirSpec kIirSpecs[kIirCount] = {
    {"Butterworth Lowpass",
     IirFamily::Butterworth,
     IirResponse::LowPass,
     "Order must be a whole number from 1 through 10, and cutoff must be greater than 0 and below the Nyquist frequency."},
    {"Butterworth Highpass",
     IirFamily::Butterworth,
     IirResponse::HighPass,
     "Order must be a whole number from 1 through 10, and cutoff must be greater than 0 and below the Nyquist frequency."},
    {"Butterworth Bandpass",
     IirFamily::Butterworth,
     IirResponse::BandPass,
     "Order must be a whole number from 1 through 10. Center and width must be greater than 0, and the band from center minus half the width through center plus half the width must lie inside 0 and the Nyquist frequency."},
    {"Butterworth Bandstop",
     IirFamily::Butterworth,
     IirResponse::BandStop,
     "Order must be a whole number from 1 through 10. Center and width must be greater than 0, and the band from center minus half the width through center plus half the width must lie inside 0 and the Nyquist frequency."},
    {"Chebyshev I Lowpass",
     IirFamily::ChebyshevI,
     IirResponse::LowPass,
     "Order must be a whole number from 1 through 10, cutoff must be greater than 0 and below the Nyquist frequency, and passband ripple must be greater than 0."},
    {"Chebyshev I Highpass",
     IirFamily::ChebyshevI,
     IirResponse::HighPass,
     "Order must be a whole number from 1 through 10, cutoff must be greater than 0 and below the Nyquist frequency, and passband ripple must be greater than 0."},
    {"Chebyshev I Bandpass",
     IirFamily::ChebyshevI,
     IirResponse::BandPass,
     "Order must be a whole number from 1 through 10. Center and width must be greater than 0, the band from center minus half the width through center plus half the width must lie inside 0 and the Nyquist frequency, and passband ripple must be greater than 0."},
    {"Chebyshev I Bandstop",
     IirFamily::ChebyshevI,
     IirResponse::BandStop,
     "Order must be a whole number from 1 through 10. Center and width must be greater than 0, the band from center minus half the width through center plus half the width must lie inside 0 and the Nyquist frequency, and passband ripple must be greater than 0."},
    {"Chebyshev II Lowpass",
     IirFamily::ChebyshevII,
     IirResponse::LowPass,
     "Order must be a whole number from 1 through 10, cutoff must be greater than 0 and below the Nyquist frequency, and stopband ripple must be greater than 0."},
    {"Chebyshev II Highpass",
     IirFamily::ChebyshevII,
     IirResponse::HighPass,
     "Order must be a whole number from 1 through 10, cutoff must be greater than 0 and below the Nyquist frequency, and stopband ripple must be greater than 0."},
    {"Chebyshev II Bandpass",
     IirFamily::ChebyshevII,
     IirResponse::BandPass,
     "Order must be a whole number from 1 through 10. Center and width must be greater than 0, the band from center minus half the width through center plus half the width must lie inside 0 and the Nyquist frequency, and stopband ripple must be greater than 0."},
    {"Chebyshev II Bandstop",
     IirFamily::ChebyshevII,
     IirResponse::BandStop,
     "Order must be a whole number from 1 through 10. Center and width must be greater than 0, the band from center minus half the width through center plus half the width must lie inside 0 and the Nyquist frequency, and stopband ripple must be greater than 0."},
    {"RBJ Lowpass",
     IirFamily::Rbj,
     IirResponse::LowPass,
     "Cutoff must be greater than 0 and below the Nyquist frequency, and Q must be greater than 0."},
    {"RBJ Highpass",
     IirFamily::Rbj,
     IirResponse::HighPass,
     "Cutoff must be greater than 0 and below the Nyquist frequency, and Q must be greater than 0."},
    {"RBJ Bandpass",
     IirFamily::Rbj,
     IirResponse::BandPass,
     "Center and width must be greater than 0, and the band from center minus half the width through center plus half the width must lie inside 0 and the Nyquist frequency."},
    {"RBJ Bandstop",
     IirFamily::Rbj,
     IirResponse::BandStop,
     "Center and width must be greater than 0, and the band from center minus half the width through center plus half the width must lie inside 0 and the Nyquist frequency."},
};

bool iir_response_is_band(IirResponse response) {
    return response == IirResponse::BandPass || response == IirResponse::BandStop;
}

void format_iir_number(char* text, std::size_t size, double value) {
    if (size == 0) {
        return;
    }
    const std::to_chars_result result = std::to_chars(
        text,
        text + size - 1,
        value,
        std::chars_format::general,
        std::numeric_limits<double>::max_digits10);
    if (result.ec == std::errc()) {
        *result.ptr = '\0';
        return;
    }
    text[0] = '\0';
}

std::optional<double> parse_iir_number(const char* text) {
    std::string_view view(text);
    while (!view.empty() && (view.front() == ' ' || view.front() == '\t')) {
        view.remove_prefix(1);
    }
    while (!view.empty() && (view.back() == ' ' || view.back() == '\t')) {
        view.remove_suffix(1);
    }
    if (view.empty()) {
        return std::nullopt;
    }
    double value = 0.0;
    const char* const begin = view.data();
    const char* const end = begin + view.size();
    const std::from_chars_result result = std::from_chars(begin, end, value);
    if (result.ec != std::errc() || result.ptr != end || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

void open_iir_dialog(Viewer& viewer, int index) {
    const IirOverlay& overlay = viewer.iir[index];
    std::snprintf(viewer.iir_dialog.order, sizeof(viewer.iir_dialog.order), "%d", overlay.order);
    format_iir_number(viewer.iir_dialog.cutoff, sizeof(viewer.iir_dialog.cutoff), overlay.cutoff);
    format_iir_number(viewer.iir_dialog.center, sizeof(viewer.iir_dialog.center), overlay.center);
    format_iir_number(viewer.iir_dialog.width, sizeof(viewer.iir_dialog.width), overlay.width);
    format_iir_number(viewer.iir_dialog.q, sizeof(viewer.iir_dialog.q), overlay.q);
    format_iir_number(viewer.iir_dialog.ripple, sizeof(viewer.iir_dialog.ripple), overlay.ripple);
    viewer.iir_dialog.index = index;
    viewer.iir_dialog.invalid = false;
    viewer.iir_dialog.request_open = true;
}

bool accept_iir_dialog(Viewer& viewer) {
    const int index = viewer.iir_dialog.index;
    if (index < 0 || index >= kIirCount) {
        return false;
    }
    const IirSpec& spec = kIirSpecs[index];
    IirOverlay& overlay = viewer.iir[index];
    IirSettings settings;
    settings.family = spec.family;
    settings.response = spec.response;
    settings.order = overlay.order;
    settings.cutoff = overlay.cutoff;
    settings.center = overlay.center;
    settings.width = overlay.width;
    settings.q = overlay.q;
    settings.ripple = overlay.ripple;

    const bool band = iir_response_is_band(spec.response);
    const bool rbj = spec.family == IirFamily::Rbj;
    const bool chebyshev = spec.family == IirFamily::ChebyshevI || spec.family == IirFamily::ChebyshevII;
    if (!rbj) {
        const std::optional<int> order = parse_frame_text(viewer.iir_dialog.order);
        if (!order.has_value()) {
            return false;
        }
        settings.order = *order;
    }
    if (!band) {
        const std::optional<double> cutoff = parse_iir_number(viewer.iir_dialog.cutoff);
        if (!cutoff.has_value()) {
            return false;
        }
        settings.cutoff = *cutoff;
    } else {
        const std::optional<double> center = parse_iir_number(viewer.iir_dialog.center);
        const std::optional<double> width = parse_iir_number(viewer.iir_dialog.width);
        if (!center.has_value() || !width.has_value()) {
            return false;
        }
        settings.center = *center;
        settings.width = *width;
    }
    if (rbj && !band) {
        const std::optional<double> q = parse_iir_number(viewer.iir_dialog.q);
        if (!q.has_value()) {
            return false;
        }
        settings.q = *q;
    }
    if (chebyshev) {
        const std::optional<double> ripple = parse_iir_number(viewer.iir_dialog.ripple);
        if (!ripple.has_value()) {
            return false;
        }
        settings.ripple = *ripple;
    }

    TDMS::object* object = plotted_object(viewer);
    if (object == nullptr) {
        return false;
    }
    const std::optional<double> increment = waveform_increment(*object);
    if (!increment.has_value() || !iir_design_ok(settings, 1.0 / *increment)) {
        return false;
    }

    overlay.order = settings.order;
    overlay.cutoff = settings.cutoff;
    overlay.center = settings.center;
    overlay.width = settings.width;
    overlay.q = settings.q;
    overlay.ripple = settings.ripple;
    overlay.on = true;
    return true;
}

void draw_iir_dialog(Viewer& viewer) {
    if (viewer.iir_dialog.index < 0 || viewer.iir_dialog.index >= kIirCount) {
        return;
    }
    const IirSpec& spec = kIirSpecs[viewer.iir_dialog.index];
    if (viewer.iir_dialog.request_open) {
        ImGui::OpenPopup(spec.title);
        viewer.iir_dialog.request_open = false;
    }
    if (!ImGui::BeginPopupModal(spec.title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const bool band = iir_response_is_band(spec.response);
    const bool rbj = spec.family == IirFamily::Rbj;
    const bool chebyshev = spec.family == IirFamily::ChebyshevI || spec.family == IirFamily::ChebyshevII;
    if (!rbj) {
        ImGui::InputText("Order", viewer.iir_dialog.order, sizeof(viewer.iir_dialog.order));
    }
    if (!band) {
        ImGui::InputText("Cutoff (Hz)", viewer.iir_dialog.cutoff, sizeof(viewer.iir_dialog.cutoff));
    } else {
        ImGui::InputText("Center (Hz)", viewer.iir_dialog.center, sizeof(viewer.iir_dialog.center));
        ImGui::InputText("Width (Hz)", viewer.iir_dialog.width, sizeof(viewer.iir_dialog.width));
    }
    if (rbj && !band) {
        ImGui::InputText("Q", viewer.iir_dialog.q, sizeof(viewer.iir_dialog.q));
    }
    if (chebyshev) {
        const char* label =
            spec.family == IirFamily::ChebyshevI ? "Passband ripple (dB)" : "Stopband ripple (dB)";
        ImGui::InputText(label, viewer.iir_dialog.ripple, sizeof(viewer.iir_dialog.ripple));
    }
    if (viewer.iir_dialog.invalid) {
        ImGui::TextWrapped("%s", spec.error);
    }
    if (ImGui::Button("OK")) {
        if (accept_iir_dialog(viewer)) {
            viewer.iir_dialog.invalid = false;
            ImGui::CloseCurrentPopup();
        } else {
            viewer.iir_dialog.invalid = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        viewer.iir[viewer.iir_dialog.index].on = false;
        viewer.iir_dialog.invalid = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

bool iir_cache_matches(const IirOverlay& overlay, const std::string& path, std::size_t count, double dt) {
    return overlay.cache_set && overlay.cache_path == path && overlay.cache_count == count && overlay.cache_dt == dt &&
           overlay.cache_order == overlay.order && overlay.cache_cutoff == overlay.cutoff &&
           overlay.cache_center == overlay.center && overlay.cache_width == overlay.width &&
           overlay.cache_q == overlay.q && overlay.cache_ripple == overlay.ripple;
}

const double* iir_channel_samples(
    TDMS::object& object, SampleKind kind, Viewer& viewer, std::vector<double>& copied) {
    const std::size_t count = object.number_values();
    const void* data = object.data();
    if (data == nullptr) {
        return nullptr;
    }
    if (kind == SampleKind::Float) {
        const auto* samples = static_cast<const float*>(data);
        copied.resize(count);
        for (std::size_t index = 0; index < count; ++index) {
            copied[index] = static_cast<double>(samples[index]);
        }
        return copied.data();
    }
    if (kind == SampleKind::Double) {
        return static_cast<const double*>(data);
    }
    if (!is_integer_kind(kind)) {
        return nullptr;
    }
    fill_integer_samples(object, kind, viewer.integer_samples);
    if (viewer.integer_samples.values.size() != count) {
        return nullptr;
    }
    return viewer.integer_samples.values.data();
}

void plot_iir_overlays(TDMS::object& object, SampleKind kind, Viewer& viewer) {
    const std::optional<double> increment = waveform_increment(object);
    if (!increment.has_value()) {
        return;
    }
    if (is_integer_kind(kind)) {
        fill_integer_samples(object, kind, viewer.integer_samples);
        if (viewer.integer_samples.values.size() != object.number_values()) {
            return;
        }
    }
    const double dt = *increment;
    const std::string path = object.get_path();
    const std::size_t count = object.number_values();
    if (count > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return;
    }
    const int plot_count = static_cast<int>(count);
    const double* samples = nullptr;
    std::vector<double> copied;
    bool samples_loaded = false;
    for (int index = 0; index < kIirCount; ++index) {
        IirOverlay& overlay = viewer.iir[index];
        if (!overlay.on) {
            continue;
        }
        if (!iir_cache_matches(overlay, path, count, dt)) {
            if (!samples_loaded) {
                samples = iir_channel_samples(object, kind, viewer, copied);
                samples_loaded = true;
            }
            if (samples == nullptr && plot_count != 0) {
                continue;
            }
            IirSettings settings;
            settings.family = kIirSpecs[index].family;
            settings.response = kIirSpecs[index].response;
            settings.order = overlay.order;
            settings.cutoff = overlay.cutoff;
            settings.center = overlay.center;
            settings.width = overlay.width;
            settings.q = overlay.q;
            settings.ripple = overlay.ripple;
            std::vector<double> filtered;
            const bool filtered_ok = filter_channel(samples, plot_count, 1.0 / dt, settings, filtered);
            overlay.cache_path = path;
            overlay.cache_count = count;
            overlay.cache_dt = dt;
            overlay.cache_order = overlay.order;
            overlay.cache_cutoff = overlay.cutoff;
            overlay.cache_center = overlay.center;
            overlay.cache_width = overlay.width;
            overlay.cache_q = overlay.q;
            overlay.cache_ripple = overlay.ripple;
            overlay.cache_set = true;
            if (!filtered_ok || filtered.size() != count) {
                overlay.values.clear();
                overlay.cache_failed = true;
                continue;
            }
            overlay.values = std::move(filtered);
            overlay.cache_failed = false;
        }
        if (overlay.cache_failed || overlay.values.size() != count || plot_count == 0) {
            continue;
        }
        ImPlot::PlotLine(kIirSpecs[index].title, overlay.values.data(), plot_count);
    }
}

void draw_plot(TDMS::object& object, Viewer& viewer) {
    const std::string path = object.get_path();
    const std::string type_name = object.data_type();
    const std::size_t count = object.number_values();
    const SampleKind kind = sample_kind(type_name);
    const bool fits = count <= static_cast<std::size_t>(std::numeric_limits<int>::max());

    ImGui::TextUnformatted(path.c_str());
    ImGui::TextUnformatted(("Type: " + type_name).c_str());
    ImGui::TextUnformatted(("Values: " + std::to_string(count)).c_str());

    if (kind == SampleKind::NotPlotted) {
        ImGui::TextUnformatted("This channel is not plotted.");
        return;
    }
    if (!fits) {
        ImGui::TextUnformatted("The sample count does not fit in an ImPlot count.");
        return;
    }
    if (!is_plotted_channel(object)) {
        return;
    }

    if (viewer.view_action == ViewAction::Reset) {
        ImPlot::SetNextAxesToFit();
    }

    const int plot_count = static_cast<int>(count);
    if (!ImPlot::BeginPlot("##channel", ImVec2(-1.0f, -1.0f))) {
        return;
    }
    if (viewer.view_action == ViewAction::Frame) {
        const FrameLimits& limits = viewer.frame_limits;
        ImPlot::SetupAxesLimits(
            limits.x_min, limits.x_max, limits.y_min, limits.y_max, ImPlotCond_Always);
    }
    if (viewer.view_action == ViewAction::None) {
        ImPlotPlot& plot = *GImPlot->CurrentPlot;
        const ImGuiIO& io = ImGui::GetIO();
        if (plot.Initialized && io.KeyCtrl && io.MouseWheel != 0.0f &&
            plot.PlotRect.Contains(io.MousePos)) {
            const double zoom_rate = static_cast<double>(ImPlot::GetInputMap().ZoomRate);
            const double factor =
                io.MouseWheel > 0.0f ? 1.0 / (1.0 + zoom_rate) : 1.0 + zoom_rate;
            const ImPlotRange y_range = plot.Axes[ImAxis_Y1].Range;
            ImPlot::SetupAxisLimits(
                ImAxis_Y1, y_range.Min * factor, y_range.Max * factor, ImPlotCond_Always);
            ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, plot.ID);
        }
    }
    if (kind == SampleKind::Float) {
        const auto* samples = static_cast<const float*>(object.data());
        ImPlot::PlotLine(path.c_str(), samples, plot_count);
        plot_rms_overlay(samples, plot_count, viewer);
        plot_avg_abs_overlay(samples, plot_count, viewer);
    } else if (kind == SampleKind::Double) {
        const auto* samples = static_cast<const double*>(object.data());
        ImPlot::PlotLine(path.c_str(), samples, plot_count);
        plot_rms_overlay(samples, plot_count, viewer);
        plot_avg_abs_overlay(samples, plot_count, viewer);
    } else if (is_integer_kind(kind)) {
        fill_integer_samples(object, kind, viewer.integer_samples);
        if (viewer.integer_samples.values.size() == count) {
            ImPlot::PlotLine(path.c_str(), viewer.integer_samples.values.data(), plot_count);
            plot_rms_overlay(viewer.integer_samples.values.data(), plot_count, viewer);
            plot_avg_abs_overlay(viewer.integer_samples.values.data(), plot_count, viewer);
        }
    }
    plot_iir_overlays(object, kind, viewer);
    handle_duration_click(object, viewer);
    draw_duration(object, viewer);
    ImPlot::EndPlot();
    if (ImPlotPlot* plot = ImPlot::GetPlot("##channel")) {
        if (plot->Initialized) {
            const ImPlotRange& range = plot->Axes[ImAxis_X1].Range;
            viewer.plot_x_min = range.Min;
            viewer.plot_x_max = range.Max;
            viewer.plot_x_known = true;
        }
    }
    viewer.view_action = ViewAction::None;
}

void draw_channels(Viewer& viewer) {
    ImGui::BeginChild("channels", ImVec2(320.0f, 0.0f), ImGuiChildFlags_Borders);
    if (viewer.capture == nullptr) {
        ImGui::EndChild();
        return;
    }

    bool any_object = false;
    int index = 0;
    for (TDMS::object* object : *viewer.capture) {
        any_object = true;
        const std::string path = object->get_path();
        const bool selected = viewer.selected_path.has_value() && *viewer.selected_path == path;
        ImGui::PushID(index);
        if (ImGui::Selectable(path.c_str(), selected)) {
            viewer.selected_path = path;
        }
        ImGui::PopID();
        ++index;
    }
    if (!any_object) {
        ImGui::TextUnformatted("There are no objects.");
    }
    ImGui::EndChild();
}

void draw_detail(Viewer& viewer) {
    ImGui::BeginChild("detail", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (!viewer.error.empty()) {
        ImGui::TextWrapped("%s", viewer.error.c_str());
    }
    if (!viewer.selected_path.has_value() || viewer.capture == nullptr) {
        ImGui::TextUnformatted("Select a channel.");
        ImGui::EndChild();
        return;
    }

    TDMS::object* object = find_selected(*viewer.capture, *viewer.selected_path);
    if (object == nullptr) {
        ImGui::TextUnformatted("Select a channel.");
        ImGui::EndChild();
        return;
    }
    draw_plot(*object, viewer);
    ImGui::EndChild();
}

std::optional<double> visible_frequency(Viewer& viewer) {
    TDMS::object* object = plotted_object(viewer);
    if (object == nullptr || !viewer.plot_x_known || !std::isfinite(viewer.plot_x_min) ||
        !std::isfinite(viewer.plot_x_max)) {
        return std::nullopt;
    }
    const std::optional<double> increment = waveform_increment(*object);
    if (!increment.has_value()) {
        return std::nullopt;
    }
    const int count = static_cast<int>(object->number_values());
    int first = 0;
    int last = 0;
    if (!view_sample_bounds(viewer.plot_x_min, viewer.plot_x_max, count, first, last)) {
        return std::nullopt;
    }
    const int window = last - first + 1;
    const SampleKind kind = sample_kind(object->data_type());
    const double* samples = nullptr;
    std::vector<double> copied;
    if (kind == SampleKind::Float) {
        const auto* data = static_cast<const float*>(object->data());
        copied.resize(static_cast<std::size_t>(window));
        for (int index = 0; index < window; ++index) {
            copied[static_cast<std::size_t>(index)] =
                static_cast<double>(data[first + index]);
        }
        samples = copied.data();
    } else if (kind == SampleKind::Double) {
        samples = static_cast<const double*>(object->data()) + first;
    } else if (is_integer_kind(kind)) {
        fill_integer_samples(*object, kind, viewer.integer_samples);
        if (viewer.integer_samples.values.size() != object->number_values()) {
            return std::nullopt;
        }
        samples = viewer.integer_samples.values.data() + static_cast<std::size_t>(first);
    } else {
        return std::nullopt;
    }
    return least_squares_frequency(samples, window, *increment);
}

void open_frequency(Viewer& viewer) {
    const std::optional<double> hertz = visible_frequency(viewer);
    viewer.frequency_dialog.fit = hertz.has_value();
    viewer.frequency_dialog.hertz = hertz.value_or(0.0);
    viewer.frequency_dialog.request_open = true;
}

void draw_frequency_dialog(Viewer& viewer) {
    if (viewer.frequency_dialog.request_open) {
        ImGui::OpenPopup("Frequency");
        viewer.frequency_dialog.request_open = false;
    }
    if (!ImGui::BeginPopupModal("Frequency", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (viewer.frequency_dialog.fit) {
        const std::string label = format_frequency(viewer.frequency_dialog.hertz);
        ImGui::TextUnformatted(label.c_str());
    } else {
        ImGui::TextUnformatted("The visible samples cannot be fit.");
    }
    if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void export_current_range(Viewer& viewer, TDMS::object& object, const std::string& path, bool csv) {
    try {
        if (csv) {
            export_csv_range(object, viewer.stored_range.start, viewer.stored_range.stop, path);
        } else {
            export_tdms_range(object, viewer.stored_range.start, viewer.stored_range.stop, path);
        }
        viewer.error.clear();
    } catch (const std::exception& ex) {
        viewer.error = ex.what();
    }
}

void draw_fft_window(Viewer& viewer) {
    if (!viewer.fft_on) {
        return;
    }
    bool open = true;
    if (!ImGui::Begin("FFT###histogram", &open, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        if (!open) {
            viewer.fft_on = false;
        }
        return;
    }

    std::vector<double> xs;
    std::vector<double> ys;
    bool hertz = false;
    double width = 0.8;
    bool bounds_known = false;
    std::string path;
    int first = 0;
    int last = 0;

    TDMS::object* object = plotted_object(viewer);
    if (object == nullptr) {
        viewer.fft_hist_known = false;
    } else if (viewer.plot_x_known && std::isfinite(viewer.plot_x_min) && std::isfinite(viewer.plot_x_max) &&
               viewer.fft_count >= 1) {
        const int count = static_cast<int>(object->number_values());
        if (view_sample_bounds(viewer.plot_x_min, viewer.plot_x_max, count, first, last)) {
            const int window = last - first + 1;
            const SampleKind kind = sample_kind(object->data_type());
            std::vector<double> copied;
            const double* samples = nullptr;
            if (kind == SampleKind::Float) {
                const auto* data = static_cast<const float*>(object->data());
                copied.resize(static_cast<std::size_t>(window));
                for (int index = 0; index < window; ++index) {
                    copied[static_cast<std::size_t>(index)] =
                        static_cast<double>(data[first + index]);
                }
                samples = copied.data();
            } else if (kind == SampleKind::Double) {
                samples = static_cast<const double*>(object->data()) + first;
            } else if (is_integer_kind(kind)) {
                fill_integer_samples(*object, kind, viewer.integer_samples);
                if (viewer.integer_samples.values.size() == object->number_values()) {
                    samples = viewer.integer_samples.values.data() + static_cast<std::size_t>(first);
                }
            }
            if (samples != nullptr) {
                path = object->get_path();
                bounds_known = true;
                const std::optional<double> increment = waveform_increment(*object);
                hertz = increment.has_value();
                const double bin_step =
                    hertz ? 1.0 / (static_cast<double>(window) * *increment) : 1.0;
                width = 0.8 * bin_step;
                const std::vector<FftComponent> components =
                    separate_components(samples, window, viewer.fft_count);
                xs.resize(components.size());
                ys.resize(components.size());
                for (std::size_t index = 0; index < components.size(); ++index) {
                    xs[index] = static_cast<double>(components[index].bin) * bin_step;
                    ys[index] = components[index].amplitude;
                }
            }
        }
    }

    const bool changed = bounds_known &&
        (!viewer.fft_hist_known || viewer.fft_hist_path != path || viewer.fft_hist_first != first ||
         viewer.fft_hist_last != last);
    if (!xs.empty() && changed) {
        ImPlot::SetNextAxesToFit();
    }
    if (ImPlot::BeginPlot("##fft_histogram", ImVec2(-1.0f, -1.0f))) {
        ImPlot::SetupAxis(ImAxis_X1, hertz ? "Hz" : "");
        if (!xs.empty()) {
            ImPlot::PlotBars("FFT", xs.data(), ys.data(), static_cast<int>(xs.size()), width);
        }
        ImPlot::EndPlot();
        if (bounds_known) {
            viewer.fft_hist_known = true;
            viewer.fft_hist_path = std::move(path);
            viewer.fft_hist_first = first;
            viewer.fft_hist_last = last;
        }
    }
    ImGui::End();
    if (!open) {
        viewer.fft_on = false;
    }
}

void draw_ui(Viewer& viewer, GLFWwindow* window) {
    TDMS::object* plotted = plotted_object(viewer);
    const bool plotted_channel = plotted != nullptr;
    const bool can_export = plotted_channel && viewer.stored_range.set &&
                            viewer.stored_range.path == plotted->get_path();
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open")) {
                const std::optional<std::string> path = choose_tdms_path(window);
                if (path.has_value() && load_capture(viewer, *path)) {
                    const std::string title = "tdms_viewer - " + *path;
                    glfwSetWindowTitle(window, title.c_str());
                }
            }
            if (ImGui::MenuItem("Export Range", nullptr, false, can_export)) {
                const std::optional<std::string> path =
                    choose_save_path(window, "TDMS\0*.tdms\0All files\0*.*\0", "tdms");
                if (path.has_value()) {
                    export_current_range(viewer, *plotted, *path, false);
                }
            }
            if (ImGui::MenuItem("Export Range to CSV", nullptr, false, can_export)) {
                const std::optional<std::string> path =
                    choose_save_path(window, "CSV\0*.csv\0All files\0*.*\0", "csv");
                if (path.has_value()) {
                    export_current_range(viewer, *plotted, *path, true);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reset", nullptr, false, plotted_channel)) {
                viewer.view_action = ViewAction::Reset;
            }
            if (ImGui::MenuItem("Set Range", nullptr, false, plotted_channel)) {
                open_set_frame(viewer, *plotted);
            }
            if (ImGui::MenuItem("RMS", nullptr, viewer.rms_on, plotted_channel)) {
                if (viewer.rms_on) {
                    viewer.rms_on = false;
                } else {
                    open_rms_dialog(viewer);
                }
            }
            if (ImGui::MenuItem("AVG(ABS(Y))", nullptr, viewer.avg_abs_on, plotted_channel)) {
                if (viewer.avg_abs_on) {
                    viewer.avg_abs_on = false;
                } else {
                    open_avg_abs_dialog(viewer);
                }
            }
            if (ImGui::MenuItem("FFT", nullptr, viewer.fft_on, plotted_channel)) {
                if (viewer.fft_on) {
                    viewer.fft_on = false;
                } else {
                    open_fft_dialog(viewer);
                }
            }
            const bool iir_channel = plotted_channel && waveform_increment(*plotted).has_value();
            for (int index = 0; index < kIirCount; ++index) {
                if (ImGui::MenuItem(
                        kIirSpecs[index].title, nullptr, viewer.iir[index].on, iir_channel || viewer.iir[index].on)) {
                    if (viewer.iir[index].on) {
                        viewer.iir[index].on = false;
                    } else {
                        open_iir_dialog(viewer, index);
                    }
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tools")) {
            const bool can_measure = plotted_channel && waveform_increment(*plotted).has_value();
            if (ImGui::MenuItem("Duration", nullptr, viewer.duration_on, can_measure || viewer.duration_on)) {
                viewer.duration_on = !viewer.duration_on;
                clear_duration_markers(viewer);
            }
            if (ImGui::MenuItem("Frequency", nullptr, false, can_measure)) {
                open_frequency(viewer);
            }
            ImGui::EndMenu();
        }
        draw_set_frame_dialog(viewer);
        draw_rms_dialog(viewer);
        draw_avg_abs_dialog(viewer);
        draw_fft_dialog(viewer);
        draw_iir_dialog(viewer);
        draw_frequency_dialog(viewer);
        ImGui::EndMainMenuBar();
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("tdms_viewer", nullptr, flags);
    draw_channels(viewer);
    ImGui::SameLine();
    draw_detail(viewer);
    ImGui::End();
    draw_fft_window(viewer);
}

void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

}  // namespace

int main() {
    glfwSetErrorCallback(glfw_error_callback);
    if (glfwInit() == GLFW_FALSE) {
        return 1;
    }

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    const float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    GLFWwindow* window = glfwCreateWindow(
        static_cast<int>(1280 * main_scale),
        static_cast<int>(800 * main_scale),
        "tdms_viewer",
        nullptr,
        nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    Viewer viewer;
    const ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        draw_ui(viewer, window);
        ImGui::Render();

        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(
            clear_color.x * clear_color.w,
            clear_color.y * clear_color.w,
            clear_color.z * clear_color.w,
            clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
