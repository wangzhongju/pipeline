#ifndef _PREPROC_ELEMENT_H__
#define _PREPROC_ELEMENT_H__
#include <chrono>
#include <mutex>
#include <unordered_set>

#include "batch_meta.h"
#include "element.h"
#include "es_sys_memory.h"
#include "es_vb_memory.h"
#include "infer.h"
// #include "bufferpool.h"
#include "yaml_parser.h"
extern "C" {
#include "es_vps.h"
}

struct IovaData {
    ES_U64 fd;
    ES_U64 pIOVA;
};

class PreProcElement : public CElement {
   public:
    PreProcElement(const char* name = "element", const char* config = "", int dieIndex = 0)
        : CElement(name, config, dieIndex) {
        mPerfType = SYNC_PERF_ELEMENT;
    };
    ~PreProcElement() {};

    app_ret Init() override;
    app_ret Start() override;
    app_ret ProcessData(CBaseMeta* baseMeta, CElement const* previousElement) override;
    // app_ret TransMitToNextToProcess(CBaseMeta *baseMeta) override;
    // app_ret ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *self = 0) override;
    app_ret perfStat() override;
    app_ret Finish() override;

   private:
    bool checkClassIndex(int classId);
    app_ret check_valid_frame(ES_S32 index, ES_S32 padIndex);

    PREPROC_PARAM_S preProcParam;
    VB_POOL pool;
    ES_S32 poolCount;

    FILE* dumpFp = nullptr;
    PerformanceStatic* normalCost = nullptr;
    PerformanceStatic* dumpCost = nullptr;
    PerformanceStatic* onlyNormalCost = nullptr;
    PerformanceStatic* onlyNormalCost2 = nullptr;
    PerformanceStatic* onlyNormalCost3 = nullptr;
    VIDEO_FRAME_S* mbackGround = nullptr;
    RECT_S mbackGroundRect;
    ES_S32 changeCnt[MAX_VIDEO_GRP_NUM];

    ES_S32* validFrameIndex;

   public:
    MetaPool<CPreprocessMeta>* premetaPool;

    BlockQueue<IovaData> m_preIOVAFd = BlockQueue<IovaData>(120);
    std::thread prepareIOVAThread;
    void prepareIOVA();

    BaseMetaQueue inputBaseMetaQueue = BaseMetaQueue(10);
    BaseMetaQueue outputBaseMetaQueue = BaseMetaQueue(10);
    BaseMetaQueue releaseIOVAQueue = BaseMetaQueue(10);

    std::thread releaseIOVAThread;
    void releaseIOVA();

    std::thread processThread;
    void process();
    bool m_exitFlag = false;

    std::unordered_set<ES_U64> m_isAlreadyNormalization;

   private:
    VIDEO_FRAME_S testFrameIn, testFrameOut;
};
#endif  //_PREPROC_ELEMENT_H__
