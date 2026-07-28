#define PL_LOG_ID PL_LOG_INFER
#include "npuUtils.h"

#include <iostream>
#include <memory>

#include "inferElement.h"
#include "log.h"

// extern bool queryThreadFlag;
std::map<uint64_t, bool> NpuContextUtils::mapThreadDeviceSet;
std::map<uint64_t, bool> NpuContextUtils::mapThreadContextSet;

static uint64_t getThreadID() {
    std::thread::id Id;
    Id = std::this_thread::get_id();
    uint64_t threadID = *(uint64_t *)&Id;
    return threadID;
}

NpuContextUtils::NpuContextUtils(bool isAsync) : m_context(NULL), m_deviceID(-1), m_isAsync(isAsync) {
    if (!m_isAsync) {
        app_debug("%s \n", "NpuContextUtils : the infer is Sync");
    } else if (m_isAsync) {
        app_debug("%s \n", "NpuContextUtils : the infer is Async");
    }
}

NpuContextUtils::~NpuContextUtils() {}

void NpuContextUtils::releaseContext() {
    app_debug("%s \n", " releaseContext ");

    if (m_isAsync) {
        if (m_context) {
            uint64_t threadID = getThreadID();
            app_debug("%s %lld\n", " will erase contextThreadSet, the threadID is : ", threadID);
            if (mapThreadContextSet.end() != mapThreadContextSet.find(threadID)) {
                mapThreadContextSet.erase(threadID);
            }
            app_debug("%s \n", " will ES_NPU_DestroyContext");
            ES_NPU_DestroyContext(m_context);
            m_context = NULL;
            app_debug("%s \n", "  ES_NPU_DestroyContext success ");
        }
    }

    if (-1 != m_deviceID) {
        app_debug("%s %d\n", " will ES_NPU_ReleaseDevice , deviceID is ", m_deviceID);
        ES_NPU_ReleaseDevice(m_deviceID);
        uint64_t threadID = getThreadID();
        app_debug("%s %lld\n", " will erase deviceThreadSet, the threadID is : ", threadID);
        if (mapThreadDeviceSet.end() != mapThreadDeviceSet.find(threadID)) {
            mapThreadDeviceSet.erase(threadID);
        }
        m_deviceID = -1;
    }
    return;
}

int NpuContextUtils::setDevice() {
    ES_S32 err;
    // 如果此线程已经设置过device了，则不再设置；
    if (mapThreadDeviceSet.find(m_deviceID) == mapThreadDeviceSet.end()) {
        err = ES_NPU_SetDevice(m_deviceID);
        if (ES_SUCCESS != err) {
            app_error("%s %d\n", "ES_NPU_SetDevice err, err num is : ", (int)err);
            return -1;
        }
    }
    return 0;
}

int NpuContextUtils::preparation(int deviceID) {
    ES_S32 err;
    uint16_t deviceNum = 0;
    err = ES_NPU_GetNumDevices(&deviceNum);
    if (ES_SUCCESS != err) {
        app_error("%s %d\n", "ES_NPU_GetNumDevices err, err num is : ", (int)err);
        return -1;
    }
    app_debug("%s %d\n", "ES_NPU_GetNumDevices success, device num is : ", deviceNum);

    if (deviceID > deviceNum) {
        app_error("%s %d\n", "the deviveID is  : ", deviceID);
        return -1;
    }

    {
        uint64_t threadID = getThreadID();
        if (mapThreadDeviceSet.end() == mapThreadDeviceSet.find(threadID)) {
            err = ES_NPU_SetDevice(deviceID);
            mapThreadDeviceSet.insert(std::pair<uint64_t, bool>(threadID, true));
        }
    }

    if (ES_SUCCESS != err) {
        app_error("%s %d\n", "ES_NPU_SetDevice err, err num is : ", (int)err);
        return -1;
    }
    m_deviceID = deviceID;
    app_debug("%s %d\n", "ES_NPU_SetDevice success, device ID is ", m_deviceID);

    if (m_isAsync) {
        // for sync infer, cannot create context
        err = ES_NPU_CreateContext(&m_context, m_deviceID);
    } else {
        err = ES_NPU_GetCurrentContext(&m_context);
    }

    if (ES_SUCCESS != err) {
        ES_NPU_ReleaseDevice(deviceID);
        uint64_t threadID = getThreadID();
        if (mapThreadDeviceSet.end() != mapThreadDeviceSet.find(threadID)) {
            mapThreadDeviceSet.erase(threadID);
        }

        m_deviceID = -1;
        app_error("%s %d\n", "ES_NPU_CreateContext err, err num is : ", (int)err);
        return -1;
    }
    app_debug("%s \n", "ES_NPU_CreateContext success");

    app_debug("%s \n", "preparation success");
    return 0;
}

int NpuContextUtils::setContext() {
    app_debug("%s \n", "setContext start");
    if (NULL == m_context) {
        app_error("m_context NULL\n");
        return -1;
    }

    {
        uint64_t threadID = getThreadID();
        if (mapThreadContextSet.end() == mapThreadContextSet.find(threadID)) {
            ES_S32 err;
            err = ES_NPU_SetCurrentContext(m_context);
            if (ES_SUCCESS != err) {
                app_error("%s  %d\n", "ES_NPU_SetCurrentContext err, err num is : ", (int)err);
                return -1;
            }
            mapThreadContextSet.insert(std::pair<uint64_t, bool>(threadID, true));
        }
    }

    app_debug("%s \n", "setContext  end");
    return 0;
}

NpuUtils::NpuUtils(NpuContextUtils *contextUtils) : m_contextUtils(contextUtils), m_threadExit(false), m_stream(NULL) {
    app_debug("%s \n", "NpuUtils::NpuUtils start");
    if (NULL != m_contextUtils) {
        m_contextUtils->setContext();
    }
    sem_init(&m_querySem, 0, 0);
    app_debug("%s \n", "NpuUtils::NpuUtils end");
}

NpuUtils::~NpuUtils() { sem_destroy(&m_querySem); }

int NpuUtils::releaseModel() {
    app_debug("%s \n", "NpuUtils::releaseModel start");
    m_threadExit = true;
    if (NULL != m_contextUtils) {
        m_contextUtils->setContext();
    }

    if (m_contextUtils->m_isAsync) {
        // app_debug("%s \n", " will ES_NPU_AbortStream ");
        // ES_NPU_AbortStream(m_stream);
        // app_debug("%s \n", " ES_NPU_AbortStream end ");

        // app_debug("%s \n", " will ES_NPU_DestroyStream ");
        // if (NULL != m_stream)
        // {
        //     ES_NPU_DestroyStream(m_stream);
        // }
    }

    app_debug("%s \n", " will ES_NPU_UnloadModel ");
    ES_NPU_UnloadModel(m_modelId);

    app_debug("%s \n", "NpuUtils::releaseModel end");
    return 0;
}

static void *queryThreadFunc(void *pArg) {
    prctl(PR_SET_NAME, (unsigned long)"queryThreadFunc");
    NpuUtils *pNpuUtils = (NpuUtils *)pArg;
    if (NULL != pNpuUtils->m_contextUtils) {
        pNpuUtils->m_contextUtils->setContext();
    }

    std::thread::id Id;
    ES_S32 err;
    Id = std::this_thread::get_id();
    pNpuUtils->m_stdThreadID = *(uint64_t *)&Id;

    set_thread_affinity(pNpuUtils->m_contextUtils->m_deviceID);

    while (1) {
        sem_wait(&pNpuUtils->m_querySem);
        err = ES_NPU_ProcessReport(pNpuUtils->m_stream, -1);
        if (err) {
            app_error("%s \n", "err:ES_NPU_ProcessReport failed!break...");
            break;
        }
        if (pNpuUtils->m_threadExit) {
            break;
        }
    }

    return (void *)0;
}

int NpuUtils::loadModel(std::string modelFileName) {
    app_debug("%s \n", "loadModel  start");
    if (NULL != m_contextUtils) {
        m_contextUtils->setContext();
    } else {
        app_error(" setContext error\n");
        return -1;
    }

    ES_S32 err;
    app_debug("%s %s\n", "start  ES_NPU_LoadModelFromFile, the file name is : ", modelFileName.c_str());
    err = ES_NPU_LoadModelFromFile(&m_modelId, (ES_CHAR *)(modelFileName.c_str()));
    app_debug("%s %d\n", "ES_NPU_LoadModelFromFile success, the modelID is ", m_modelId);

    if (ES_SUCCESS != err) {
        m_modelId = -1;
        app_error("%s %d\n", "ES_NPU_LoadModelFromFile err, err num is : ", (int)err);
        return -1;
    }
    app_debug("%s %d\n", "ES_NPU_LoadModelFromFile success, the modelID is ", m_modelId);

    err = ES_NPU_GetNumInputTensors(m_modelId, &m_numInputTensors);
    if (ES_SUCCESS != err) {
        app_error("%s %d\n", "ES_NPU_GetNumInputTensors err, err num is : ", (int)err);
        goto ERR;
    }
    app_debug("%s %d\n", "ES_NPU_GetNumInputTensors success, the inputTensor num is ", m_numInputTensors);

    if (1 != m_numInputTensors) {
        app_error("%s %d\n", "the input tensor num is not 1 , the num is : ", m_numInputTensors);
        goto ERR;
    }

    err = ES_NPU_GetNumOutputTensors(m_modelId, &m_numOutputTensors);
    if (ES_SUCCESS != err) {
        app_error("%s %d\n", "ES_NPU_GetNumOutputTensors err, err num is : ", (int)err);
        goto ERR;
    }
    app_debug("%s %d\n", "ES_NPU_GetNumOutputTensors success, the outputTensor num is ", m_numOutputTensors);

    if (1 != m_numOutputTensors) {
        app_debug("%s %d\n", "the output tensor num is not 1 , the num is : ", m_numOutputTensors);
        // goto ERR;
    }

    for (int iBindIndex = 0; iBindIndex < m_numInputTensors; iBindIndex++) {
        NPU_TENSOR_S tmpTensor;
        err = ES_NPU_GetInputTensorDesc(m_modelId, iBindIndex, &tmpTensor);
        if (ES_SUCCESS != err) {
            app_error("%s %d\n", "ES_NPU_GetInputTensorDesc err, err num is : ", (int)err);
            goto ERR;
        }
        app_debug("%s %d\n", "ES_NPU_GetInputTensorDesc success, tensor index is : ", iBindIndex);
        app_debug("%s %d\n", "the model input dataFormat is : ", tmpTensor.dataFormat);
        app_debug("%s %d\n", "the model input dataType is : ", tmpTensor.dataType);
        app_debug("%s %d\n", "the model input pixelFormat is : ", tmpTensor.pixelFormat);
        m_inputTensors.push_back(tmpTensor);
    }

    for (int iBindIndex = 0; iBindIndex < m_numOutputTensors; iBindIndex++) {
        NPU_TENSOR_S tmpTensor;
        err = ES_NPU_GetOutputTensorDesc(m_modelId, iBindIndex, &tmpTensor);
        if (ES_SUCCESS != err) {
            app_error("%s %d\n", "ES_NPU_GetOutputTensorDesc err, err num is : ", (int)err);
            goto ERR;
        }
        app_debug("%s %d\n", "ES_NPU_GetOutputTensorDesc success, tensor index is : ", iBindIndex);
        app_debug("%s %d\n", "the model output dataFormat is : ", tmpTensor.dataFormat);
        app_debug("%s %d\n", "the model output dataType is : ", tmpTensor.dataType);
        app_debug("%s %d\n", "the model output pixelFormat is : ", tmpTensor.pixelFormat);
        m_outputTensors.push_back(tmpTensor);
    }

    if (m_contextUtils->m_isAsync) {
        err = ES_NPU_CreateStream(&m_stream);
        if (ES_SUCCESS != err) {
            app_error("%s %d\n", "ES_NPU_CreateStream err, err num is : ", (int)err);
            goto ERR;
        }
        app_debug("%s \n", "ES_NPU_CreateStream success ");

        m_stdThread = std::thread(queryThreadFunc, this);
    }

    return 0;

ERR:
    ES_NPU_UnloadModel(m_modelId);
    return -1;
}

std::shared_ptr<NPU_TASK_S> NpuUtils::prepareAndCheckData(std::vector<ES_U64> &_inputDmaFd,
                                                          std::vector<ES_U64> &_outputDmaFd, NPU_TaskCallback callback,
                                                          void *callbackArg) {
    if (NULL != m_contextUtils) {
        m_contextUtils->setContext();
    } else {
        return NULL;
    }

    if (m_numInputTensors != _inputDmaFd.size() || m_numOutputTensors != _outputDmaFd.size()) {
        app_error("%s \n", "the inputBuf/outputBuf size is not equal model input/output size");
    }

    static int setContextFlag = 0;
    setContextFlag++;

    std::shared_ptr<NPU_TASK_S> task = std::make_shared<NPU_TASK_S>();
    task->inputFdNum = m_numInputTensors;
    task->outputFdNum = m_numOutputTensors;
    task->modelId = m_modelId;
    task->taskId = setContextFlag;
    for (int iInputIndex = 0; iInputIndex < m_numInputTensors; iInputIndex++) {
        app_debug("%s %lld\n", "the inputBuf fd is : ", _inputDmaFd[iInputIndex]);

        task->inputFd[iInputIndex].memFd = _inputDmaFd[iInputIndex];
    }

    for (int iOutputIndex = 0; iOutputIndex < m_numOutputTensors; iOutputIndex++) {
        app_debug("%s %lld\n", "the outputFd fd is : ", _outputDmaFd[iOutputIndex]);

        task->outputFd[iOutputIndex].memFd = _outputDmaFd[iOutputIndex];
    }

    if (NULL != callback && NULL != callbackArg) {
        task->callback = callback;
        ((queueData *)callbackArg)->task = task;
        task->callbackArg = callbackArg;
    }
    return task;
}

int NpuUtils::submitAsync(std::vector<ES_U64> &_inputDmaFd, std::vector<ES_U64> &_outputDmaFd,
                          NPU_TaskCallback callback, void *callbackArg) {
    std::shared_ptr<NPU_TASK_S> task = prepareAndCheckData(_inputDmaFd, _outputDmaFd, callback, callbackArg);
    queueData *argData = (queueData *)(callbackArg);
    argData->taskStartTime = (uint64_t)esclock();
    app_debug("%s \n", " npu will submit async task");
    ES_S32 err = ES_NPU_SubmitAsync(task.get(), 1, m_stream);
    sem_post(&m_querySem);
    if (ES_SUCCESS != err) {
        app_error("%s %d\n", "the ES_NPU_SubmitAsync err, err num is :", int(err));
    }

    return 0;
}

int NpuUtils::submitSync(std::vector<ES_U64> &_inputDmaFd, std::vector<ES_U64> &_outputDmaFd, NPU_TaskCallback callback,
                         void *callbackArg) {
    std::shared_ptr<NPU_TASK_S> task = prepareAndCheckData(_inputDmaFd, _outputDmaFd, callback, callbackArg);
    queueData *argData = (queueData *)(callbackArg);
    argData->taskStartTime = (uint64_t)esclock();

    app_debug("%s \n", " npu will submit sync task");

    ES_S32 err = ES_NPU_Submit(task.get(), 1);
    argData->taskEndTime = (uint64_t)esclock();
    if (ES_SUCCESS != err) {
        app_error("%s %d\n", "the ES_NPU_Submit err, err num is :", int(err));
        return -1;
    }

    return 0;
}

CDataType modelDataTypeToEssdkplDataType(int modelDataType) {
    switch (modelDataType) {
        case 1:
            return DATA_F32;
            break;
        case 2:
            return DATA_F16;
            break;
        case 3:
            return DATA_S16;
            break;
        case 4:
            return DATA_S8;
            break;
        case 5:
            return DATA_U8;
            break;
        case 6:
            return DATA_U16;
            break;
    }
    return DATA_UNKNOW;
}

std::vector<ModelInfo> NpuUtils::getInputTensorDesc() const {
    std::vector<ModelInfo> retVec;
    for (int i = 0; i < m_numInputTensors; i++) {
        ModelInfo dataInfo;
        dataInfo.dims.n = m_inputTensors[i].dims.n;
        dataInfo.dims.h = m_inputTensors[i].dims.h;
        dataInfo.dims.w = m_inputTensors[i].dims.w;
        dataInfo.dims.c = m_inputTensors[i].dims.c;
        dataInfo.dataType = modelDataTypeToEssdkplDataType(m_inputTensors[i].dataType);
        app_debug("%s [%d, %d, %d, %d]\n", "the model input tensor dims [n, h, w, c ] is : ", dataInfo.dims.n,
                  dataInfo.dims.h, dataInfo.dims.w, dataInfo.dims.c);
        app_debug("%s %d\n", "the model input tensor nput data type is : ", m_inputTensors[i].dataType);
        app_debug("%s %d\n", "the model input tensor essdkpl data type is : ", int(dataInfo.dataType));
        dataInfo.dataSize = m_inputTensors[i].bufferSize;
        retVec.push_back(dataInfo);
    }
    return retVec;
}

std::vector<ModelInfo> NpuUtils::getOutputTensorDesc() const {
    std::vector<ModelInfo> retVec;
    for (int i = 0; i < m_numOutputTensors; i++) {
        ModelInfo dataInfo;
        dataInfo.dims.n = m_outputTensors[i].dims.n;
        dataInfo.dims.h = m_outputTensors[i].dims.h;
        dataInfo.dims.w = m_outputTensors[i].dims.w;
        dataInfo.dims.c = m_outputTensors[i].dims.c;
        dataInfo.dataType = modelDataTypeToEssdkplDataType(m_outputTensors[i].dataType);
        app_debug("%s [%d, %d, %d, %d]\n", "the model output tensor dims [n, h, w, c ] is : ", dataInfo.dims.n,
                  dataInfo.dims.h, dataInfo.dims.w, dataInfo.dims.c);
        app_debug("%s %d\n", "the model output tensor nput data type is : ", m_outputTensors[i].dataType);
        app_debug("%s %d\n", "the model output tensor essdkpl data type is : ", int(dataInfo.dataType));
        dataInfo.dataSize = m_outputTensors[i].bufferSize;
        retVec.push_back(dataInfo);
    }
    return retVec;
}
