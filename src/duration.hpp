#pragma once

#include <string>

double duration_seconds(int start_index, int end_index, double increment);

std::string format_duration(double seconds);
