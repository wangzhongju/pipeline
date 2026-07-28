#ifndef _INFER_ELEMENT_H__
#define _INFER_ELEMENT_H__

#include <memory>
#include <queue>
#include <vector>

#include "batch_meta.h"
#include "element.h"
#include "es_sys_memory.h"
#include "es_vb_memory.h"
#include "infer.h"
#include "npuUtils.h"

class InferElement;

struct queueData {
    bool isEnd;                         // 本数据是否是最后一个batch的数据；
    CBatchMeta *batchMeta;              // 本数据所属的batchMeta;
    CInferOutputMeta *inferOutputData;  // 本次batch的推理结果；
    std::shared_ptr<NPU_TASK_S> task;   // 防止task被释放；
    BlockQueue<queueData *> *pinferOutputQueue;
    uint64_t taskStartTime;
    uint64_t taskEndTime;
    uint64_t pretaskEndTime;
    std::atomic<uint64_t> *pNpuOutCount;
};

struct AsyncTimeData {
    AsyncTimeData(uint64_t start, uint64_t end, uint64_t preEndTime) {
        taskStartTime = start;
        taskEndTime = end;
        pretaskEndTime = preEndTime;
    };
    uint64_t taskStartTime;
    uint64_t taskEndTime;
    uint64_t pretaskEndTime;
};

struct InitParams {
    InitParams() {
        modelFileName = "";
        outputPoolSize = 3;
        uniqueID = 1;
        dieID = 0;
        dumpflag = 0;
        isAsync = 1;
    };
    std::string modelFileName;
    uint outputPoolSize;
    uint uniqueID;
    uint dieID;
    int dumpflag;
    bool isAsync;
};

class InferElement : public CElement {
   public:
    InferElement(const char *name, const char *configFile, int dieIndex = 0)
        : CElement(name, configFile, dieIndex),
          m_taskCount(0),
          m_transMitToNextIndex(0),
          m_inferOutputQueue(-1, "inferOutput"){};
    ~InferElement() = default;

   public:
    app_ret Init() override;
    app_ret Start() override;
    app_ret Finish() override;
    app_ret Wait() override;
    app_ret perfStat() override;
    app_ret ProcessData(CBaseMeta *baseMeta, CElement const *privious = 0) override;
    app_ret ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *privious = 0) override;
    BlockQueue<queueData *> m_inferOutputQueue;

   private:
    void attachInferOutputMeta();
    void attachInferOutputMetaAsync();
    app_ret parseConfigFile(std::string _configFilePath);
    app_ret savePerformace();

   private:
    std::shared_ptr<NpuContextUtils> m_NpuContextUtils;
    std::shared_ptr<NpuUtils> m_NpuUtilsPtr;
    ModelInfo modelInputInfo, modelOutputInfo;
    std::vector<ModelInfo> m_modelInputInfo, m_modelOutputInfo;
    std::thread m_attachThread;
    std::vector<VB_POOL> inferOutputPool;
    InitParams initParams;
    std::queue<AsyncTimeData> m_AsyncConsumeTime;
    std::vector<AsyncTimeData> m_historyConsumeTime;
    std::vector<int> m_batchRealFrameNum;
    std::deque<CBatchMeta *> m_savedBatchMetaQueue;
    atomic<int64_t> m_taskCount;
    uint64_t m_transMitToNextIndex;
    MetaPool<CInferOutputMeta> *inferOutPool;

   public:
    atomic<uint64_t> gInferInCount = 0;
    atomic<uint64_t> gInferOutCount = 0;

    atomic<uint64_t> gNpuInCount = 0;
    atomic<uint64_t> gNpuOutCount = 0;
    std::mutex gNpuMtx;
};
#endif  //_INFER_ELEMENT_H__
