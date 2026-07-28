#ifndef _POSTPROCESS_ELEMENT_H__
#define _POSTPROCESS_ELEMENT_H__

#include <map>
#include <memory>

#include "batch_meta.h"
#include "cpuOp.h"
#include "dspOp.h"
#include "element.h"
#include "es_sys_memory.h"
#include "es_vb_memory.h"
#include "infer.h"
#include "postParams.h"

struct classifyGoldData {
    classifyGoldData() : classID(0), confidence(0.0){};
    int classID;
    float confidence;
};

class PostprocessElement : public CElement {
   public:
    PostprocessElement(const char *name, const char *configFile, int dieIndex = 0)
        : CElement(name, configFile, dieIndex) {
        mPerfType = SYNC_PERF_ELEMENT;
    };
    ~PostprocessElement() = default;

   public:
    app_ret Wait() override;
    app_ret Init() override;
    app_ret Finish() override;
    app_ret perfStat() override;
    app_ret ProcessData(CBaseMeta *baseMeta, CElement const *privious = 0) override;

   public:
    PostProcessInitParams m_postprocessInitParams;
    MetaPool<CObjectMeta> *ometaPool;

   private:
    app_ret parseConfigFile();
    app_ret classifyAttachBatchMeta(CInferOutputMeta *inferOutputMeta, std::vector<int> &maxIndex,
                                    std::vector<float> &maxConfidenc);
    std::vector<string> m_postLabels;
    app_ret parseLabelsFile(const std::string &labelsFilePath);
    app_ret detectionAttachBatchMeta(CInferOutputMeta *inferOutputMeta, PostProcessInitParams &_postprocessInitParams,
                                     std::vector<std::vector<DetectionOutput>> &outputVec);
    app_ret rtmposeAttachBatchMeta(CInferOutputMeta *inferOutputMeta, PostProcessInitParams &_postprocessInitParams,
                                   std::vector<std::vector<RtmPosePointInfo>> &outputVec);

   private:
    PerformanceStatic *softmaxPerformance;
    PerformanceStatic *argmaxPerformance;
    PerformanceStatic *detectionPerformance;
    std::unique_ptr<DspOp> pdspOperationObject;
    CpuOp cpuProcessData;
    std::vector<classifyGoldData> m_goldenData;
    std::map<uint, std::vector<classifyGoldData>> m_classifyOutput;
};
#endif  //_POSTPROCESS_ELEMENT_H__
