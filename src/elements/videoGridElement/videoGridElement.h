#ifndef _VIDEOGRID_ELEMENT_H__
#define _VIDEOGRID_ELEMENT_H__
#include <chrono>
#include <mutex>

#include "batch_meta.h"
#include "element.h"
#include "es_sys_memory.h"
#include "es_vb_memory.h"
#include "yaml_parser.h"

extern "C" {
#include "es_vps.h"
}
#define MAX_BATCH_SIZE MAX_VIDEO_GRP_NUM

struct GridIovaData {
    ES_U64 fd;
    ES_U64 pIOVA;
};

class VideoGridElement : public CElement {
   public:
    VideoGridElement(const char* name = "element", const char* config = "", int dieIndex = 0, int startPadIndex = 0)
        : CElement(name, config, dieIndex) {
        mPerfType = SYNC_PERF_ELEMENT;
        m_startPadIndex = startPadIndex;
        m_exitFlag = false;
    }
    ~VideoGridElement() {};

    app_ret Init() override;
    app_ret Start() override;
    app_ret Wait() override;
    app_ret ProcessData(CBaseMeta* baseMeta, CElement const* previousElement) override;
    app_ret perfStat() override;
    // app_ret TransMitToNextToProcess(CBaseMeta *baseMeta);
    // app_ret ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *self =
    // 0);
    app_ret Finish() override;

   private:
    VIDEOGRID_PARAM_S videoGridParam;
    VB_POOL pool;
    VB_POOL poolCfgPicPool;
    int m_startPadIndex;
    ES_U64 backupFd;
    ES_U64 backupIOVA;
    ES_S32 poolCount;

    FILE* dumpFp;
    PerformanceStatic* multisrcCost;
    PerformanceStatic* dumpCost;
    CFrameMeta* mFrameMetas[MAX_BATCH_SIZE];

    ES_S32 rgb[3];
    ES_S32 specialRectNum;
    ES_S32 specialCol[100];
    ES_S32 specialRow[100];

   public:
    MetaPool<CVideoGridMeta>* gmetaPool;

   private:
    BlockQueue<GridIovaData> m_gridIOVAFd;
    bool m_exitFlag;
};
#endif  //_VIDEOGRID_ELEMENT_H__
