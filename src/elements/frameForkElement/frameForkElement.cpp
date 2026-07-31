#define PL_LOG_ID PL_LOG_TEE
#include "frameForkElement.h"

#include <utility>

namespace {

class BranchFrameMeta final : public CFrameMeta {
public:
    explicit BranchFrameMeta(CFrameMeta* owner) : owner_(owner) {
        owner_->addUseCount();
        eosFlag = owner_->eosFlag;
        dieIndex = owner_->dieIndex;
        source = owner_->source;
        indexInBatch = owner_->indexInBatch;
        padIndex = owner_->padIndex;
        index = owner_->index;
        pts = owner_->pts;
        srcTime = owner_->srcTime;
        feature = owner_->feature;
        streamId = owner_->streamId;
        images = owner_->images;
    }

    void release() override {
        images.clear();
        for (auto* object : objs) {
            if (object != nullptr) {
                object->release();
            }
        }
        objs.clear();
        for (auto* object : rtmObjs) {
            if (object != nullptr) {
                object->release();
            }
        }
        rtmObjs.clear();
        owner_->reduceUseCount();
        delete this;
    }

private:
    CFrameMeta* owner_;
};

}  // namespace

app_ret FrameForkElement::Init() {
    return m_PreviousElementVec.size() == 1 && !m_NextElementVec.empty()
               ? APP_SUCCESS
               : APP_FAILURE;
}

CBatchMeta* FrameForkElement::cloneBatch(CBatchMeta& source) const {
    auto* clone = new CBatchMeta();
    clone->eosFlag = source.eosFlag;
    clone->dieIndex = source.dieIndex;
    clone->creationTime = source.creationTime;
    clone->smuxTimeout = source.smuxTimeout;
    clone->batchIndex = source.batchIndex;
    for (int index = 0; index < source.getFrameMetaSize(); ++index) {
        CFrameMeta* frame = source.getFrameMeta(index);
        if (frame != nullptr) {
            clone->addFrameMeta(new BranchFrameMeta(frame));
        }
    }
    return clone;
}

app_ret FrameForkElement::ProcessData(
    CBaseMeta* baseMeta, CElement const* previousElement) {
    (void)previousElement;
    return baseMeta != nullptr && baseMeta->mMetaType == BATCH_META
               ? APP_SUCCESS
               : APP_FAILURE;
}

app_ret FrameForkElement::ProcessAndTransmit(
    CBaseMeta* baseMeta, CElement const* previousElement) {
    (void)previousElement;
    if (ProcessData(baseMeta, previousElement) != APP_SUCCESS) {
        return APP_FAILURE;
    }

    auto* batch = static_cast<CBatchMeta*>(baseMeta);
    app_ret result = APP_SUCCESS;
    for (auto* next : m_NextElementVec) {
        CBatchMeta* branch = cloneBatch(*batch);
        const app_ret current = next->ProcessAndTransmit(branch, this);
        if (current != APP_SUCCESS) {
            branch->reduceUseCount();
            result = current;
        }
    }
    batch->reduceUseCount();
    return result;
}

extern "C" CElement* createEsFrameForkElement(
    const char* name, int dieIndex) {
    return new FrameForkElement(name, dieIndex);
}
