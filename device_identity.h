#pragma once

#include <cstddef>

constexpr std::size_t CT_DEVICE_ID_SIZE = 18;

bool ctBuildStableDeviceId(char* output, std::size_t capacity);
