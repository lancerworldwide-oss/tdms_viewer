#include "export_range.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char* unchanged_property =
    "This channel has a property that cannot be exported unchanged.";
constexpr const char* range_error = "The sample range cannot be exported.";
constexpr const char* path_error = "Channel path cannot be exported.";

struct NumericType {
    const char* name;
    std::uint32_t id;
    std::size_t size;
};

constexpr NumericType numeric_types[] = {
    {"tdsTypeI8", 1, 1},
    {"tdsTypeI16", 2, 2},
    {"tdsTypeI32", 3, 4},
    {"tdsTypeU8", 5, 1},
    {"tdsTypeU16", 6, 2},
    {"tdsTypeU32", 7, 4},
    {"tdsTypeU64", 8, 8},
    {"tdsTypeSingleFloat", 9, 4},
    {"tdsTypeDoubleFloat", 10, 8},
};

const NumericType* find_numeric(const std::string& name) {
    for (const NumericType& type : numeric_types) {
        if (name == type.name) {
            return &type;
        }
    }
    return nullptr;
}

void append_bytes(std::vector<unsigned char>& out, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

void append_u32(std::vector<unsigned char>& out, std::uint32_t value) {
    const unsigned char bytes[] = {
        static_cast<unsigned char>(value & 0xffu),
        static_cast<unsigned char>((value >> 8) & 0xffu),
        static_cast<unsigned char>((value >> 16) & 0xffu),
        static_cast<unsigned char>((value >> 24) & 0xffu),
    };
    append_bytes(out, bytes, sizeof(bytes));
}

void append_u64(std::vector<unsigned char>& out, std::uint64_t value) {
    unsigned char bytes[8];
    for (int index = 0; index < 8; ++index) {
        bytes[index] = static_cast<unsigned char>(value & 0xffu);
        value >>= 8;
    }
    append_bytes(out, bytes, sizeof(bytes));
}

void append_string(std::vector<unsigned char>& out, const std::string& text, const char* error) {
    if (text.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(error);
    }
    append_u32(out, static_cast<std::uint32_t>(text.size()));
    append_bytes(out, text.data(), text.size());
}

void check_range(TDMS::object& channel, int start, int stop) {
    if (channel.data() == nullptr || start < 0 || stop < start) {
        throw std::runtime_error(range_error);
    }
    if (static_cast<std::size_t>(stop) >= channel.number_values()) {
        throw std::runtime_error(range_error);
    }
}

std::vector<std::string> ancestor_paths(const std::string& path) {
    if (path.empty() || path.front() != '/') {
        throw std::runtime_error(path_error);
    }
    std::vector<std::string> ancestors;
    if (path == "/") {
        return ancestors;
    }
    ancestors.push_back("/");
    std::size_t index = 1;
    while (index < path.size()) {
        if (path[index] != '\'') {
            throw std::runtime_error(path_error);
        }
        std::size_t cursor = index + 1;
        bool closed = false;
        while (cursor < path.size()) {
            if (path[cursor] == '\'') {
                if (cursor + 1 < path.size() && path[cursor + 1] == '\'') {
                    cursor += 2;
                    continue;
                }
                closed = true;
                break;
            }
            ++cursor;
        }
        if (!closed) {
            throw std::runtime_error(path_error);
        }
        const std::size_t after = cursor + 1;
        if (after == path.size()) {
            return ancestors;
        }
        if (path[after] != '/') {
            throw std::runtime_error(path_error);
        }
        ancestors.push_back(path.substr(0, after));
        index = after + 1;
    }
    throw std::runtime_error(path_error);
}

void append_property(
    std::vector<unsigned char>& out,
    const std::string& name,
    const TDMS::object::property& property) {
    append_string(out, name, unchanged_property);
    if (property.data_type.name == "tdsTypeString") {
        const auto* text = static_cast<const std::string*>(property.value);
        if (text == nullptr) {
            throw std::runtime_error(unchanged_property);
        }
        append_u32(out, 0x20u);
        append_string(out, *text, unchanged_property);
        return;
    }
    const NumericType* type = find_numeric(property.data_type.name);
    if (type == nullptr || property.value == nullptr || property.data_type.ctype_length != type->size) {
        throw std::runtime_error(unchanged_property);
    }
    append_u32(out, type->id);
    append_bytes(out, property.value, type->size);
}

void append_no_data_object(std::vector<unsigned char>& metadata, const std::string& path) {
    append_string(metadata, path, path_error);
    append_u32(metadata, 0xffffffffu);
    append_u32(metadata, 0);
}

std::vector<unsigned char> tdms_range_bytes(TDMS::object& channel, int start, int stop) {
    check_range(channel, start, stop);
    const NumericType* type = find_numeric(channel.data_type());
    if (type == nullptr) {
        throw std::runtime_error(range_error);
    }
    const std::string path = channel.get_path();
    const std::vector<std::string> ancestors = ancestor_paths(path);
    const auto properties = channel.get_properties();
    if (ancestors.size() + 1 > std::numeric_limits<std::uint32_t>::max() ||
        properties.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(range_error);
    }

    const auto count = static_cast<std::uint64_t>(stop - start) + 1;
    std::vector<unsigned char> metadata;
    append_u32(metadata, static_cast<std::uint32_t>(ancestors.size() + 1));
    for (const std::string& ancestor : ancestors) {
        append_no_data_object(metadata, ancestor);
    }
    append_string(metadata, path, path_error);
    append_u32(metadata, 20);
    append_u32(metadata, type->id);
    append_u32(metadata, 1);
    append_u64(metadata, count);
    append_u32(metadata, static_cast<std::uint32_t>(properties.size()));
    for (const auto& entry : properties) {
        if (entry.second == nullptr) {
            throw std::runtime_error(unchanged_property);
        }
        append_property(metadata, entry.first, *entry.second);
    }

    const auto* samples = static_cast<const unsigned char*>(channel.data());
    const std::size_t bytes = static_cast<std::size_t>(count) * type->size;
    std::vector<unsigned char> raw(
        samples + (static_cast<std::size_t>(start) * type->size),
        samples + (static_cast<std::size_t>(start) * type->size) + bytes);

    std::vector<unsigned char> file;
    append_bytes(file, "TDSm", 4);
    append_u32(file, (1u << 1) | (1u << 2) | (1u << 3));
    append_u32(file, 4712);
    append_u64(file, metadata.size() + raw.size());
    append_u64(file, metadata.size());
    append_bytes(file, metadata.data(), metadata.size());
    append_bytes(file, raw.data(), raw.size());
    return file;
}

template <typename T>
void append_integer_line(std::string& out, T value) {
    char buffer[32];
    std::to_chars_result result;
    if constexpr (sizeof(T) == 1) {
        result = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<int>(value));
    } else {
        result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    }
    if (result.ec != std::errc()) {
        throw std::runtime_error(range_error);
    }
    out.append(buffer, result.ptr);
    out.push_back('\n');
}

template <typename T>
void append_float_line(std::string& out, T value) {
    if (!std::isfinite(value)) {
        if (std::isnan(value)) {
            out += "nan";
        } else if (value < 0) {
            out += "-inf";
        } else {
            out += "inf";
        }
        out.push_back('\n');
        return;
    }
    char buffer[128];
    const std::to_chars_result result = std::to_chars(
        buffer,
        buffer + sizeof(buffer),
        value,
        std::chars_format::general,
        std::numeric_limits<T>::max_digits10);
    if (result.ec != std::errc()) {
        throw std::runtime_error(range_error);
    }
    out.append(buffer, result.ptr);
    out.push_back('\n');
}

template <typename T>
void append_integer_samples(std::string& out, const void* data, int start, int stop) {
    const auto* samples = static_cast<const T*>(data);
    for (int index = start; index <= stop; ++index) {
        append_integer_line(out, samples[index]);
    }
}

template <typename T>
void append_float_samples(std::string& out, const void* data, int start, int stop) {
    const auto* samples = static_cast<const T*>(data);
    for (int index = start; index <= stop; ++index) {
        append_float_line(out, samples[index]);
    }
}

std::string csv_range_text(TDMS::object& channel, int start, int stop) {
    check_range(channel, start, stop);
    const std::string type_name = channel.data_type();
    const void* data = channel.data();
    std::string text;
    if (type_name == "tdsTypeI8") {
        append_integer_samples<std::int8_t>(text, data, start, stop);
    } else if (type_name == "tdsTypeI16") {
        append_integer_samples<std::int16_t>(text, data, start, stop);
    } else if (type_name == "tdsTypeI32") {
        append_integer_samples<std::int32_t>(text, data, start, stop);
    } else if (type_name == "tdsTypeU8") {
        append_integer_samples<std::uint8_t>(text, data, start, stop);
    } else if (type_name == "tdsTypeU16") {
        append_integer_samples<std::uint16_t>(text, data, start, stop);
    } else if (type_name == "tdsTypeU32") {
        append_integer_samples<std::uint32_t>(text, data, start, stop);
    } else if (type_name == "tdsTypeU64") {
        append_integer_samples<std::uint64_t>(text, data, start, stop);
    } else if (type_name == "tdsTypeSingleFloat") {
        append_float_samples<float>(text, data, start, stop);
    } else if (type_name == "tdsTypeDoubleFloat") {
        append_float_samples<double>(text, data, start, stop);
    } else {
        throw std::runtime_error(range_error);
    }
    return text;
}

void write_all(const std::string& path, const void* data, std::size_t size) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        throw std::runtime_error("File \"" + path + "\" could not be opened");
    }
    const std::size_t written = std::fwrite(data, 1, size, file);
    const int closed = std::fclose(file);
    if (written != size || closed != 0) {
        throw std::runtime_error("File \"" + path + "\" could not be written");
    }
}

}  // namespace

void export_tdms_range(TDMS::object& channel, int start, int stop, const std::string& path) {
    const std::vector<unsigned char> bytes = tdms_range_bytes(channel, start, stop);
    write_all(path, bytes.data(), bytes.size());
}

void export_csv_range(TDMS::object& channel, int start, int stop, const std::string& path) {
    const std::string text = csv_range_text(channel, start, stop);
    write_all(path, text.data(), text.size());
}
