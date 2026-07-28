#ifndef _DSP_OP_H__
#define _DSP_OP_H__

#include <es_ak_api.h>

#include <map>

#include "../common/dspManager.h"
#include "element.h"
#include "es_sys_memory.h"
#include "es_vb_memory.h"
#include "infer.h"
#include "pl_mem_wrap.h"
#include "postParams.h"

class DspOp {
   public:
    DspOp(PerformanceStatic **softmax, PerformanceStatic **argmax, PerformanceStatic **detection, int dspID = 0,
          int dieIndex = 0);
    ~DspOp();
    void closeDspDevice();

   public:
    app_ret classifyPostprocess(CInferOutputMeta *inferOutput, float softmaxScale, int argmaxK,
                                std::vector<int> &maxIndexVec, std::vector<float> &maxConfidenceVec);
    app_ret detectionPostprocess(CInferOutputMeta *inferOutput, DetectionOutParams &detectionParams,
                                 std::vector<std::vector<DetectionOutput>> &outputVec);
    app_ret segmentPostprocess(CInferOutputMeta *inferOutput);
    app_ret rtmposePostprocess(CInferOutputMeta *inferOutput, RtmposeParams &detectionParams,
                               std::vector<std::vector<RtmPosePointInfo>> &outputVec);
    app_ret init(PostProcessDetectionParams &initParam);

   private:
    VB_POOL m_softmaxOutputPool;
    sem_t m_softmaxSem;
    bool m_softmaxCreateFlag;
    VB_POOL m_argmaxOutputPool;
    VB_POOL m_argmaxIndexPool;
    bool m_argmaxCreateFlag;

   private:
    int32_t postprocess_detectionOut(std::vector<ES_TENSOR_S> &inTensors, DetectionOutParams &detectionParams,
                                     std::vector<DetectionOutput> &outputVec);
    app_ret prapareDspOutFd(ES_TENSOR_S &output, ES_TENSOR_S &outputCount, DetectionOutParams &detectionParams);
    app_ret prepareDspCFG(ES_DETECTION_OUT_CFG &stDspCfg, DetectionOutParams &detectionParams);
    template <typename T>
    std::vector<int32_t> extractBoxNum(void *data, int32_t count);
    template <typename T>
    std::vector<std::vector<DetectionOutput>> extractBoxInfo(void *data, std::vector<int32_t> &boxCnt, int32_t size);
    app_ret extractDspOut(ES_TENSOR_S &output, ES_TENSOR_S &outputCount, std::vector<int32_t> &boxCount,
                          std::vector<std::vector<DetectionOutput>> &boxInfo);

    PerformanceStatic **softmaxPerformance;
    PerformanceStatic **argmaxPerformance;
    PerformanceStatic **detectionPerformance;
    VB_POOL m_detectionOutputPool;
    VB_POOL m_detectionCountPool;
    bool m_detectionCreateFlag;

   private:
    int m_dspID;
    EsPlDspManager m_dspManager;
    int m_DieIndex;
    ES_AK_DEVICE_E mDevice = ES_AK_DEV_BUTT;
    std::string m_VBName;
};

#endif  //_DSP_OP_H__