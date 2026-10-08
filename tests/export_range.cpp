#include "export_range.hpp"

#include <tdms.hpp>

#include <cstdint>
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

void append_string(std::vector<unsigned char>& out, const std::string& text) {
    append_u32(out, static_cast<std::uint32_t>(text.size()));
    append_bytes(out, text.data(), text.size());
}

void append_f64(std::vector<unsigned char>& out, double value) {
    append_bytes(out, &value, sizeof(value));
}

std::vector<unsigned char> source_file() {
    const std::string path = "/'Group'/'Channel'";
    std::vector<unsigned char> metadata;
    append_u32(metadata, 1);
    append_string(metadata, path);
    append_u32(metadata, 20);
    append_u32(metadata, 10);
    append_u32(metadata, 1);
    append_u64(metadata, 5);
    append_u32(metadata, 2);
    append_string(metadata, "unit");
    append_u32(metadata, 0x20u);
    append_string(metadata, "volts");
    append_string(metadata, "gain");
    append_u32(metadata, 3);
    append_u32(metadata, 3);

    std::vector<unsigned char> raw;
    for (double sample : {1.0, 2.0, 3.0, 4.0, 5.0}) {
        append_f64(raw, sample);
    }

    std::vector<unsigned char> bytes;
    append_bytes(bytes, "TDSm", 4);
    append_u32(bytes, (1u << 1) | (1u << 2) | (1u << 3));
    append_u32(bytes, 4712);
    append_u64(bytes, metadata.size() + raw.size());
    append_u64(bytes, metadata.size());
    append_bytes(bytes, metadata.data(), metadata.size());
    append_bytes(bytes, raw.data(), raw.size());
    return bytes;
}

bool write_bytes(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        std::cerr << "could not write " << path << "\n";
        return false;
    }
    return true;
}

TDMS::object* find_object(TDMS::file& capture, const std::string& path) {
    for (TDMS::object* candidate : capture) {
        if (candidate->get_path() == path) {
            return candidate;
        }
    }
    return nullptr;
}

bool expect_empty(TDMS::file& capture, const std::string& path) {
    TDMS::object* object = find_object(capture, path);
    if (object == nullptr) {
        std::cerr << "missing " << path << "\n";
        return false;
    }
    if (object->number_values() != 0 || !object->get_properties().empty()) {
        std::cerr << path << " has data or properties\n";
        return false;
    }
    return true;
}

}  // namespace

int main() {
    const std::filesystem::path directory = std::filesystem::temp_directory_path();
    const std::filesystem::path source = directory / "tdms_export_range_source.tdms";
    const std::filesystem::path exported = directory / "tdms_export_range_out.tdms";
    const std::filesystem::path csv_path = directory / "tdms_export_range_out.csv";
    int status = 0;
    if (!write_bytes(source, source_file())) {
        status = 1;
    } else {
        try {
            TDMS::file capture(source.string());
            TDMS::object* channel = find_object(capture, "/'Group'/'Channel'");
            if (channel == nullptr) {
                std::cerr << "source channel missing\n";
                status = 1;
            } else {
                export_tdms_range(*channel, 1, 3, exported.string());
                export_csv_range(*channel, 1, 3, csv_path.string());
            }
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << "\n";
            status = 1;
        }
    }

    if (status == 0) {
        try {
            TDMS::file capture(exported.string());
            int count = 0;
            for (TDMS::object* object : capture) {
                (void)object;
                ++count;
            }
            if (count != 3) {
                std::cerr << "object count " << count << "\n";
                status = 1;
            }
            if (!expect_empty(capture, "/") || !expect_empty(capture, "/'Group'")) {
                status = 1;
            }
            TDMS::object* channel = find_object(capture, "/'Group'/'Channel'");
            if (channel == nullptr) {
                std::cerr << "exported channel missing\n";
                status = 1;
            } else if (channel->data_type() != "tdsTypeDoubleFloat" || channel->number_values() != 3 ||
                       channel->data() == nullptr) {
                std::cerr << "exported channel shape\n";
                status = 1;
            } else {
                const auto* samples = static_cast<const double*>(channel->data());
                if (samples[0] != 2.0 || samples[1] != 3.0 || samples[2] != 4.0) {
                    std::cerr << "exported samples\n";
                    status = 1;
                }
                const auto properties = channel->get_properties();
                const auto gain = properties.find("gain");
                const auto unit = properties.find("unit");
                if (properties.size() != 2 || gain == properties.end() || unit == properties.end() ||
                    gain->second == nullptr || unit->second == nullptr ||
                    gain->second->data_type.name != "tdsTypeI32" ||
                    unit->second->data_type.name != "tdsTypeString" ||
                    *static_cast<const std::int32_t*>(gain->second->value) != 3 ||
                    *static_cast<const std::string*>(unit->second->value) != "volts") {
                    std::cerr << "exported properties\n";
                    status = 1;
                }
            }
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << "\n";
            status = 1;
        }
    }

    if (status == 0) {
        std::ifstream in(csv_path, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (text != "2\n3\n4\n") {
            std::cerr << "csv [" << text << "]\n";
            status = 1;
        }
    }

    std::error_code ignored;
    std::filesystem::remove(source, ignored);
    std::filesystem::remove(exported, ignored);
    std::filesystem::remove(csv_path, ignored);
    return status;
}
