#ifndef _VDEC_ELEMENT_H__
#define _VDEC_ELEMENT_H__
#include <chrono>
#include <map>
#include <mutex>

#include "./common/pl_comm_dec.h"
#include "base_meta.h"
#include "element.h"
#include "es_sys_memory.h"
#include "es_vb_memory.h"
#include "sharedCounter.h"

class CImageVd : public CImage {
   public:
    CImageVd() {};
    CImageVd(ES_S32 grpId, VDEC_CHN vdChn, VIDEO_FRAME_INFO_S *pic)
        : mGrpId(grpId), mVdChn(vdChn), CImage(pic), misIOVA(false) {};
    virtual ~CImageVd() {};
    void release();
    void setparam(ES_S32 grpId, VDEC_CHN vdChn, VIDEO_FRAME_INFO_S *pic) {
        mGrpId = grpId;
        mVdChn = vdChn;
        mPic = pic;
        return;
    }
    bool misIOVA;

   private:
    ES_S32 mGrpId;
    VDEC_CHN mVdChn;
};

class VdecElement : public CElement {
   public:
    VdecElement(const char *name = "element", const char *config = "", int dieIndex = 0)
        : CElement(name, config, dieIndex) {
        mPerfType = IGNORE_PERF_ELEMENT;
        isIpc = 0;
    };
    ~VdecElement() {};

    app_ret Init() override;
    app_ret Start() override;
    app_ret Wait() override;
    app_ret ProcessData(CBaseMeta *baseMeta, CElement const *previousElement) override;
    // app_ret TransMitToNextToProcess(CBaseMeta *baseMeta);
    app_ret ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *self = 0) override;
    app_ret perfStat() override;
    app_ret Finish() override;

   public:
    PerformanceStatic *getFramePerformance;
    FILE *dumpFp[ES_VDEC_OUT_CHN_NUM];
    int isIpc;
    int mPadIndex;
    MetaPool<CFrameMeta> *fmetaPool;
    MetaPool<CImageVd> *cimagePool;
    static int m_ThreadGroupCount;

   private:
    ES_BOOL mDecExisted;
    DEC_Client_S mDecClient;
    ES_S32 grpIdOffset;

    // void* reserve;
   public:
    static DEC_Client_S gDecClient;
    static bool gInitialized;
};
#endif  //_VDEC_ELEMENT_H__
