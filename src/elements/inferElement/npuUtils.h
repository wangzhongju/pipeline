#ifndef _NPU_UTILS_H__
#define _NPU_UTILS_H__

#include <es_npu_interface.h>

#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "infer.h"

class NpuContextUtils {
   public:
    NpuContextUtils(bool inferType = 0);
    ~NpuContextUtils();
    void releaseContext();

   public:
    int preparation(int deviceID);
    int setContext();
    int setDevice();
    npu_context m_context;
    bool m_isAsync;  // 0 - Sync, 1-Async;
    int m_deviceID;

   private:
    static std::map<uint64_t, bool> mapThreadDeviceSet;
    static std::map<uint64_t, bool> mapThreadContextSet;
};

class NpuUtils {
   public:
    NpuUtils(NpuContextUtils *contextUtils);
    ~NpuUtils();

    int releaseModel();

   public:
    int loadModel(std::string modelFileName);
    int submitAsync(std::vector<ES_U64> &_inputDmaFd, std::vector<ES_U64> &_outputDmaFd, NPU_TaskCallback callback,
                    void *callbackArg);
    int submitSync(std::vector<ES_U64> &_inputDmaFd, std::vector<ES_U64> &_outputDmaFd, NPU_TaskCallback callback,
                   void *callbackArg);
    std::vector<ModelInfo> getInputTensorDesc() const;
    std::vector<ModelInfo> getOutputTensorDesc() const;

   private:
    uint32_t m_modelId;
    int32_t m_numInputTensors;
    int32_t m_numOutputTensors;
    std::vector<NPU_TENSOR_S> m_inputTensors;
    std::vector<NPU_TENSOR_S> m_outputTensors;
    std::vector<NPU_TASK_S> m_taskVec;
    std::shared_ptr<NPU_TASK_S> prepareAndCheckData(std::vector<ES_U64> &_inputDmaFd, std::vector<ES_U64> &_outputDmaFd,
                                                    NPU_TaskCallback callback = NULL, void *callbackArg = NULL);

   public:
    sem_t m_querySem;
    bool m_threadExit;
    uint64_t m_stdThreadID;
    npu_stream m_stream;
    std::thread m_stdThread;
    NpuContextUtils *m_contextUtils;
};
#endif