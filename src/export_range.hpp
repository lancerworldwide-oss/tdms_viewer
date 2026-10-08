#pragma once

#include <tdms.hpp>

#include <string>

void export_tdms_range(TDMS::object& channel, int start, int stop, const std::string& path);
void export_csv_range(TDMS::object& channel, int start, int stop, const std::string& path);
