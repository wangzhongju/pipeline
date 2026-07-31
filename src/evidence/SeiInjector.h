#pragma once

#include "media-agent.pb.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pipeline::evidence {

bool injectMospSei(const uint8_t* source, size_t source_size,
                   int codec_id,
                   const std::vector<DetectionObject>& objects,
                   int display_duration_ms,
                   std::vector<uint8_t>& output);

}  // namespace pipeline::evidence
