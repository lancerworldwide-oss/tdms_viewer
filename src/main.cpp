#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>
#include <implot_internal.h>
#include <tdms.hpp>

#include "export_range.hpp"
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
};

constexpr const char* frame_range_error =
    "Start and stop must be whole numbers from 0 through the last sample, and start must not be after stop.";
constexpr const char* rms_window_error = "Window length must be a positive whole number.";

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

void clear_selection(Viewer& viewer) {
    viewer.selected_path.reset();
    viewer.integer_samples.path.reset();
    viewer.integer_samples.values.clear();
    viewer.stored_range = {};
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
            ImGui::EndMenu();
        }
        draw_set_frame_dialog(viewer);
        draw_rms_dialog(viewer);
        draw_avg_abs_dialog(viewer);
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
