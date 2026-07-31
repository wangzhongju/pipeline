#ifndef _FRAME_FORK_ELEMENT_H_
#define _FRAME_FORK_ELEMENT_H_

#include "batch_meta.h"
#include "element.h"

class FrameForkElement : public CElement {
public:
    FrameForkElement(const char* name, int dieIndex)
        : CElement(name, "", dieIndex) {}

    app_ret Init() override;
    app_ret ProcessData(
        CBaseMeta* baseMeta,
        CElement const* previousElement = nullptr) override;
    app_ret ProcessAndTransmit(
        CBaseMeta* baseMeta,
        CElement const* previousElement = nullptr) override;

private:
    CBatchMeta* cloneBatch(CBatchMeta& source) const;
};

#endif
