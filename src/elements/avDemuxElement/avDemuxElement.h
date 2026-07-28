#ifndef _AVDEMUX_ELEMENT_H__
#define _AVDEMUX_ELEMENT_H__
#include <chrono>
#include <map>
#include <mutex>

#include "audio.h"
#include "batch_meta.h"
#include "element.h"

#define MAX_STREAM_NAME_LEN (128)

typedef struct {
    char streamName[MAX_STREAM_NAME_LEN];
    // video param
    PAYLOAD_TYPE_E videotype;
    ES_S32 width;
    ES_S32 height;
    // audio param
    PAYLOAD_TYPE_E audiotype;
    ADEC_MODE_E Mode;
    ES_U32 frame_size;
    ES_U32 num_channels;
    ES_U32 sample_rate;
    int profile;
    int outfps;
    int totalFrame;
    int isIpc;
    // element
    CElement* element;
} AVDEMUX_PARAM_S;

class AvDemuxElement : public CElement {
   public:
    AvDemuxElement(const char* name = "element", const char* config = "", int loopNum = 1, int dieIndex = 0)
        : CElement(name, config, dieIndex) {
        mLoopNum = loopNum;
    };
    ~AvDemuxElement(){};

    app_ret Init() override;
    app_ret Start() override;
    app_ret Wait() override;
    app_ret ProcessData(CBaseMeta* baseMeta, CElement const* previousElement) override;
    app_ret TransMitToNextToProcess(CBaseMeta* baseMeta, int outChannelIdx);
    app_ret perfStat() override;
    // app_ret ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *self = 0);
    app_ret Finish() override;
    app_ret InfoQuery(void* data, BASE_QUERY_TYPE type, BASE_QUERY_DIRECTION direction, int padIndex,
                      CElement* inquirerElement) override;

   public:
    PerformanceStatic* sendStreamPerformance;
    FILE* dumpFp;
    int voutidx;
    int aoutidx;
    int mLoopNum;
    int mPadIndex;
    MetaPool<CVideoPacketMeta>* vpacketPool;

   private:
    AVDEMUX_PARAM_S avDemuxParam;
    pthread_t sendDataPid;
};
#endif  //_AVDEMUX_ELEMENT_H__
