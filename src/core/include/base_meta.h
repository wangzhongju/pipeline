#ifndef _ESSDK_PIPELINE_BASE_META_H_
#define _ESSDK_PIPELINE_BASE_META_H_

#include <atomic>
#include <shared_mutex>

#include "log.h"

enum BASE_META_TYPE {
    BATCH_META = 0,
    FRAME_META,
    AUDIO_FRAME_META,
    VIDEO_PACKET_META,
    AUDIO_PACKET_META,
};

class CBaseMeta {
   public:
    CBaseMeta(BASE_META_TYPE type) : mMetaType(type){};
    virtual ~CBaseMeta() {}
    virtual void release() = 0;

    void setUseCount(int32_t useCount) { mUseCount.store(useCount, std::memory_order_relaxed); };
    int32_t getUseCount() { return mUseCount.load(std::memory_order_relaxed); };
    void addUseCount() { mUseCount.fetch_add(1, std::memory_order_relaxed); };
    void reduceUseCount() {
        if (mUseCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            release();
        }
    };

   public:
    bool eosFlag = false;
    BASE_META_TYPE mMetaType;
    std::shared_mutex sLock;
    std::atomic_int32_t mUseCount = 1;
    int dieIndex = 0;

   private:
    CBaseMeta(const CBaseMeta &) = delete;
    CBaseMeta &operator=(const CBaseMeta &) = delete;
};
#endif