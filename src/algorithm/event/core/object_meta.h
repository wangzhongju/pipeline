#pragma once

#include <cstdint>
#include <string>

namespace algorithm::cdky {

struct DetectorBboxInfo {
    float left = 0.0F;
    float top = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

struct CObjectMeta {
    std::string objLable;
    int classId = 0;
    uint64_t trackerId = 0;
    float detectorConfidence = 0.0F;
    DetectorBboxInfo detectorBboxInfo;
};

} // namespace algorithm::cdky
