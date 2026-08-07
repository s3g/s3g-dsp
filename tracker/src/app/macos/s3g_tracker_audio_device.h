#pragma once

#include <cstdint>
#include <string>

namespace s3g::tracker::app {

struct AudioOutputDevice {
    uint32_t id = 0u;
    std::string name;
    double sampleRate = 0.0;
    uint32_t outputChannels = 0u;
    bool isDefault = false;
};

} // namespace s3g::tracker::app
