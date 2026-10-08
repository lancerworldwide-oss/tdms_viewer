#include <tdms.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void append_bytes(std::vector<unsigned char>& out, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

void append_u32(std::vector<unsigned char>& out, std::uint32_t value) {
    unsigned char bytes[4];
    for (int index = 0; index < 4; ++index) {
        bytes[index] = static_cast<unsigned char>(value & 0xffu);
        value >>= 8;
    }
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

void append_f64(std::vector<unsigned char>& out, double value) {
    append_bytes(out, &value, sizeof(value));
}

std::vector<unsigned char> segment(
    std::uint32_t toc,
    const std::string& path,
    std::uint64_t count,
    const std::vector<double>& samples) {
    std::vector<unsigned char> metadata;
    append_u32(metadata, 1);
    append_u32(metadata, static_cast<std::uint32_t>(path.size()));
    append_bytes(metadata, path.data(), path.size());
    append_u32(metadata, 20);
    append_u32(metadata, 10);
    append_u32(metadata, 1);
    append_u64(metadata, count);
    append_u32(metadata, 0);

    std::vector<unsigned char> raw;
    for (double sample : samples) {
        append_f64(raw, sample);
    }

    std::vector<unsigned char> bytes;
    append_bytes(bytes, "TDSm", 4);
    append_u32(bytes, toc);
    append_u32(bytes, 4712);
    append_u64(bytes, metadata.size() + raw.size());
    append_u64(bytes, metadata.size());
    append_bytes(bytes, metadata.data(), metadata.size());
    append_bytes(bytes, raw.data(), raw.size());
    return bytes;
}

}  // namespace

int main() {
    const std::string channel = "/'ch'";
    const std::uint32_t metadata = 1u << 1;
    const std::uint32_t new_object_list = 1u << 2;
    const std::uint32_t raw_data = 1u << 3;
    const auto first = segment(metadata | new_object_list | raw_data, channel, 2, {1.0, 2.0});
    const auto second = segment(metadata | raw_data, channel, 4, {3.0, 4.0, 5.0, 6.0});

    const std::filesystem::path file_path =
        std::filesystem::temp_directory_path() / "tdms_continued_segment_count.tdms";
    {
        std::ofstream out(file_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(first.data()), static_cast<std::streamsize>(first.size()));
        out.write(reinterpret_cast<const char*>(second.data()), static_cast<std::streamsize>(second.size()));
        if (!out) {
            std::cerr << "could not write " << file_path << "\n";
            return 1;
        }
    }

    const double expected[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    int status = 0;
    try {
        TDMS::file capture(file_path.string());
        TDMS::object* object = nullptr;
        for (TDMS::object* candidate : capture) {
            if (candidate->get_path() == channel) {
                object = candidate;
            }
        }
        if (object == nullptr) {
            std::cerr << "channel missing\n";
            status = 1;
        } else if (object->data_type() != "tdsTypeDoubleFloat") {
            std::cerr << "type " << object->data_type() << "\n";
            status = 1;
        } else if (object->number_values() != 6 || object->data() == nullptr) {
            std::cerr << "count " << object->number_values() << "\n";
            status = 1;
        } else {
            const auto* samples = static_cast<const double*>(object->data());
            for (std::size_t index = 0; index < 6; ++index) {
                if (samples[index] != expected[index]) {
                    std::cerr << "sample " << index << " " << samples[index] << "\n";
                    status = 1;
                    break;
                }
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        status = 1;
    }

    std::error_code ignored;
    std::filesystem::remove(file_path, ignored);
    return status;
}
