#pragma once

#include "element.h"

class EvidenceRecorderElement : public CElement {
public:
    EvidenceRecorderElement(const char* name, int die_index)
        : CElement(name, "", die_index, VIDEO_DECODER) {}

    app_ret ProcessData(CBaseMeta* base_meta,
                        CElement const* previous_element = nullptr) override;
};
