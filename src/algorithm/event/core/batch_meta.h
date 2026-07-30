#pragma once

#include "object_meta.h"

#include <cstdint>
#include <string>
#include <vector>

namespace algorithm::cdky {

struct CBaseMeta {
    bool eosFlag = false;
    virtual ~CBaseMeta() = default;
};

struct CFrameMeta : public CBaseMeta {
    uint64_t index = 0;
    int64_t timestampMs = 0;
    std::string cameraId;
    std::vector<CObjectMeta*> objs;
};

} // namespace algorithm::cdky
