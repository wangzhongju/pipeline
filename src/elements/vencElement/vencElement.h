#ifndef _VENC_ELEMENT_H__
#define _VENC_ELEMENT_H__
#include "batch_meta.h"
#include "element.h"
#include "es_sys_memory.h"
#include "es_vb_memory.h"
#include "sharedCounter.h"

extern "C" {
#include "common/pl_comm_enc.h"
}

#define GOP_MIN_VALUE (1)
#define GOP_MAX_VALUE (65536)

class VencElement : public CElement {
   public:
    VencElement(const char *name = "vencelement", const char *config = "") : CElement(name, config) {
        mPerfType = ASYNC_PERF_ELEMENT;
    };
    ~VencElement() = default;

    app_ret Init() override;
    app_ret Start() override;
    app_ret Wait() override;
    app_ret ProcessData(CBaseMeta *baseMeta, CElement const *previousElement) override;
    app_ret Finish() override;
    app_ret ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *self) override;
    app_ret perfStat() override;

   public:
    PerformanceStatic *sendFramePerformance;
    PerformanceStatic *getStreamPerformance;

    // PerformanceStatic* getStreamSelectPerformance;
    // PerformanceStatic* getStreamQueryPerformance;
    // PerformanceStatic* getStreamGetPerformance;
    // PerformanceStatic* getStreamReleasePerformance;

    ES_BOOL sendFrameThdExit;
    PAYLOAD_TYPE_E type;
    ES_S32 chnId;  // true chnId,will be assign when read config

   private:
    std::mutex mtx;
    TEST_Client_S_ENC mEncClient;
    bool chnStarted;

    ES_BOOL bEncExisted;

    int frameCount;
    ES_BOOL frameDumpflag;

    static bool gEncStarted;
    static bool gEncFinished;
    static TEST_Client_S_ENC gEncClient;
};
#endif  //_VENC_ELEMENT_H__
