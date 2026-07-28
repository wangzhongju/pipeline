#define PL_LOG_ID PL_LOG_INFER
#include "inferElement.h"

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <fstream>
#include <iostream>

#include "es_sys.h"

// bool queryThreadFlag = false;

app_ret InferElement::parseConfigFile(std::string _configFilePath) {
    app_debug("infer_%d: %s %s\n", initParams.uniqueID, "the config file is : ", _configFilePath.c_str());
    if (_configFilePath.empty() || !std::filesystem::exists(_configFilePath)) {
        app_error("%s %s\n", "Config File  doesn't exist, the file path is : ", _configFilePath.c_str());
        return APP_FAILURE;
    }
    app_debug("infer_%d: %s \n", initParams.uniqueID, "will load configFile ");
    YAML::Node configyml = YAML::LoadFile(_configFilePath);

    if (!(configyml.size() > 0)) {
        app_error("%s %s\n", "Unable to parse config file, the file path is : ", _configFilePath.c_str());
        return APP_FAILURE;
    }
    app_debug("infer_%d: %s \n", initParams.uniqueID, "will parse configFile ");

    // Parse the config file here
    std::set<std::string> mandatoryString{"model-filepath"};
    for (YAML::const_iterator itr = configyml.begin(); itr != configyml.end(); ++itr) {
        std::string paramKey = itr->first.as<std::string>();
        app_debug("infer_%d: %s %s\n", initParams.uniqueID, "will parse ", paramKey.c_str());
        if (paramKey == "model-filepath") {
            app_debug("infer_%d: %s %s\n", initParams.uniqueID, "the mandatory will erase : ", paramKey.c_str());
            mandatoryString.erase(paramKey);
            app_debug("infer_%d: %s \n", initParams.uniqueID, "the mandatory erase success ");
            app_debug("infer_%d: %s \n", initParams.uniqueID, "will get file name ");
            initParams.modelFileName = itr->second.as<std::string>();
            app_debug("infer_%d: %s %s\n", initParams.uniqueID, "file name is ", initParams.modelFileName.c_str());
            app_debug("%s %s\n", "model-filepath is : ", initParams.modelFileName.c_str());
        } else if (paramKey == "unique-id") {
            initParams.uniqueID = itr->second.as<int>();
            app_debug("%s %d\n", "unique-id is : ", initParams.uniqueID);
        } else if (paramKey == "die-id") {
            int deviceID = itr->second.as<int>();
            if (deviceID >= 0 && deviceID <= 1) {
                initParams.dieID = deviceID;
                app_debug("%s %d\n", "die-id is : ", initParams.dieID);
            } else {
                initParams.dieID = 0;
                app_debug("%s %d\n", "die-id exceed the value range, now the die-id is : ", initParams.dieID);
            }
        } else if (paramKey == "inferOutputPoolSize") {
            initParams.outputPoolSize = itr->second.as<int>();
            app_debug("%s %d\n", "inferOutputPoolSize is : ", initParams.outputPoolSize);
            if (initParams.outputPoolSize < 1) {
                return APP_FAILURE;
            }
        } else if (paramKey == "dumpflag") {
            initParams.dumpflag = itr->second.as<int>();
            app_debug("%s %d\n", "dumpflag is : ", initParams.dumpflag);
        } else if (paramKey == "isAsync") {
            initParams.isAsync = itr->second.as<int>();
            app_debug("%s %d\n", "isAsync is : ", initParams.isAsync);
        } else {
            app_debug("%s %s\n", "Unknown parameter  : ", paramKey.c_str());
        }
    }
    if (mandatoryString.size() > 0) {
        for (auto it = mandatoryString.begin(); it != mandatoryString.end(); it++) {
            std::string tempStr = *it;
            app_error("%s %s\n", tempStr.c_str(), "  is not set ");
        }
        return APP_FAILURE;
    }
    return APP_SUCCESS;
}

app_ret InferElement::Start() {
    if (initParams.isAsync) {
        m_attachThread = std::thread(std::mem_fn(&InferElement::attachInferOutputMetaAsync), this);
    }
    return APP_SUCCESS;
}

app_ret InferElement::Wait() {
    app_debug("%s %s\n", mName.c_str(), " InferElement::Wait start ");
    app_debug("%s \n", " InferElement wait start ");
    if (initParams.isAsync) {
        m_attachThread.join();
    }

    std::string frameNumFile = std::string("everyBatchFrameNum_") + std::to_string(int(initParams.uniqueID)) + ".log";
    fstream frameFile(frameNumFile.c_str(), ios::app);
    if (m_modelInputInfo.size() > 0) {
        frameFile << m_modelInputInfo[0].dims.n << "\n";
    }
    frameFile.close();
    app_debug("%s \n", " calc preformace data end ");
    app_debug("%s %s\n", mName.c_str(), " InferElement::Wait end ");
    return APP_SUCCESS;
}

app_ret InferElement::savePerformace() {
    app_debug("%s \n", " will save preformace data ");
    std::string logFile = std::string("infer_") + std::to_string(int(initParams.uniqueID)) + ".log";
    fstream tempStrem(logFile.c_str(), ios::app);
    tempStrem << "the model is : " << initParams.modelFileName << "\n";
    tempStrem << "the batch size is : " << m_modelInputInfo[0].dims.n << "\n";
    static uint64_t tempPreEndTime = 0;

    app_debug("%s \n", " will calc preformace data ");
    if (m_historyConsumeTime.size()) {
        uint64_t avgTime = m_historyConsumeTime[0].taskEndTime - m_historyConsumeTime[0].taskStartTime;
        if (0 == tempPreEndTime) {
            avgTime = m_historyConsumeTime[0].taskEndTime - m_historyConsumeTime[0].taskStartTime;
        } else {
            if (m_historyConsumeTime[0].taskStartTime < tempPreEndTime) {
                avgTime = m_historyConsumeTime[0].taskEndTime - tempPreEndTime;
            } else if (m_historyConsumeTime[0].taskStartTime >= tempPreEndTime) {
                avgTime = m_historyConsumeTime[0].taskEndTime - m_historyConsumeTime[0].taskStartTime;
            }
        }
        tempStrem << avgTime << "\n";
        app_debug("%lu \n", avgTime);
        for (int index = 1; index < m_historyConsumeTime.size(); index++) {
            if (m_historyConsumeTime[index].taskStartTime < m_historyConsumeTime[index - 1].taskEndTime) {
                avgTime = m_historyConsumeTime[index].taskEndTime - m_historyConsumeTime[index - 1].taskEndTime;
                app_debug("%lu \n", avgTime);
            } else if (m_historyConsumeTime[index].taskStartTime >= m_historyConsumeTime[index - 1].taskEndTime) {
                avgTime = m_historyConsumeTime[index].taskEndTime - m_historyConsumeTime[index].taskStartTime;
                app_debug("%lu \n", avgTime);
            }
            tempStrem << avgTime << "\n";
            tempPreEndTime = m_historyConsumeTime[index].taskEndTime;
        }
    }
    tempStrem << " ================================================================= "
              << "\n";
    tempStrem.close();

    std::string historyFile = std::string("infer_") + std::to_string(int(initParams.uniqueID)) + "_taskTime.log";
    fstream tempStrem22(historyFile.c_str(), ios::app);
    if (m_historyConsumeTime.size()) {
        for (int index = 0; index < m_historyConsumeTime.size(); index++) {
            tempStrem22 << m_historyConsumeTime[index].taskEndTime << "   " << m_historyConsumeTime[index].taskStartTime
                        << "\n";
        }
    }
    tempStrem22.close();

    m_historyConsumeTime.clear();
    return 0;
}

app_ret InferElement::perfStat() {
    app_debug("%s \n", " will save preformace data ");
    std::string logFile = std::string("infer_") + std::to_string(int(initParams.uniqueID)) + ".log";
    fstream tempStrem(logFile.c_str(), ios::app);
    tempStrem << "the model is : " << initParams.modelFileName << "\n";
    tempStrem << "the batch size is : " << m_modelInputInfo[0].dims.n << "\n";
    static uint64_t tempPreEndTime = 0;

    app_debug("%s \n", " will calc preformace data ");
    if (m_historyConsumeTime.size()) {
        uint64_t avgTime = m_historyConsumeTime[0].taskEndTime - m_historyConsumeTime[0].taskStartTime;
        if (0 == tempPreEndTime) {
            avgTime = m_historyConsumeTime[0].taskEndTime - m_historyConsumeTime[0].taskStartTime;
        } else {
            if (m_historyConsumeTime[0].taskStartTime < tempPreEndTime) {
                avgTime = m_historyConsumeTime[0].taskEndTime - tempPreEndTime;
            } else if (m_historyConsumeTime[0].taskStartTime >= tempPreEndTime) {
                avgTime = m_historyConsumeTime[0].taskEndTime - m_historyConsumeTime[0].taskStartTime;
            }
        }
        tempStrem << avgTime << "\n";
        app_debug("%lu \n", avgTime);
        for (int index = 1; index < m_historyConsumeTime.size(); index++) {
            if (m_historyConsumeTime[index].taskStartTime < m_historyConsumeTime[index - 1].taskEndTime) {
                avgTime = m_historyConsumeTime[index].taskEndTime - m_historyConsumeTime[index - 1].taskEndTime;
                app_debug("%lu \n", avgTime);
            } else if (m_historyConsumeTime[index].taskStartTime >= m_historyConsumeTime[index - 1].taskEndTime) {
                avgTime = m_historyConsumeTime[index].taskEndTime - m_historyConsumeTime[index].taskStartTime;
                app_debug("%lu \n", avgTime);
            }
            tempStrem << avgTime << "\n";
            tempPreEndTime = m_historyConsumeTime[index].taskEndTime;
        }
    }
    tempStrem << " ================================================================= "
              << "\n";
    tempStrem.close();
    m_historyConsumeTime.clear();
    return APP_SUCCESS;
}

void InferElement::attachInferOutputMetaAsync() {
    prctl(PR_SET_NAME, mName.c_str());
    app_debug("infer_%d: %s \n", initParams.uniqueID, "attachInferOutputMetaAsync  start");
    while (1) {
        app_debug("infer_%d: %s %d\n", initParams.uniqueID, "m_inferOutputQueue  size is ",
                  (int)m_inferOutputQueue.size());
        int ipreParentObjSize = 0;

        std::unique_ptr<queueData> tempQueueData = nullptr;
        tempQueueData.reset((queueData *)m_inferOutputQueue.pop());
        app_debug("infer_%d: %s \n", initParams.uniqueID, " get one taskdata ");
        m_taskCount--;
        std::lock_guard<std::mutex> lock(gNpuMtx);
        AsyncTimeData tempTime(tempQueueData->taskStartTime, tempQueueData->taskEndTime, tempQueueData->pretaskEndTime);

        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ///////////////////这里为临时添加，因为更改了eosflag的下发逻辑，所以这里做调整；////////////////
        if (tempQueueData->batchMeta->eosFlag) {
            goto next;
        }

        if (m_AsyncConsumeTime.size() > 0) {
            app_debug("infer_%d: %s \n", initParams.uniqueID, "the queue size large >  0");
            tempQueueData->pretaskEndTime = m_AsyncConsumeTime.back().taskEndTime;
            tempTime.pretaskEndTime = m_AsyncConsumeTime.back().taskEndTime;
            // 计时策略调整：
            // 从第一个任务开始推演：
            // 统一概念：排队数据即当前帧的start<上一帧的end；不排队即当前帧的start>上一帧的end;
            // 如果上一帧数据是不排队数据，即上一帧的start>preEnd，则当前帧排队或者不排队都能处理；
            // 如果上一帧数据是排队数据，则当前帧数据如果排队，则好算，如果当前帧是不排队数据，则无法计算；

            // 1. 如果上一帧是不排队数据
            if (m_AsyncConsumeTime.back().taskStartTime > m_AsyncConsumeTime.back().pretaskEndTime) {
                // 1.1：当前帧是不排队数据；
                if (tempTime.taskStartTime > m_AsyncConsumeTime.back().taskEndTime) {
                    uint64_t avgTime = m_AsyncConsumeTime.back().taskEndTime - m_AsyncConsumeTime.back().taskStartTime;
                    app_debug("infer_%d: %s %.4f ms, %s %lu us\n", initParams.uniqueID,
                              "the submit Async consume avg 0000000  time is : ", avgTime / 1000.0,
                              " the submit Async cost[0] time is : ", avgTime);
                } else  // 1.2: 当前帧是排队数据
                {
                    uint64_t avgTime = tempTime.taskEndTime - m_AsyncConsumeTime.back().taskEndTime;
                    app_debug("infer_%d: %s %.4f ms, %s %lu us\n", initParams.uniqueID,
                              "the submit Async consume avg 0000000  time is : ", avgTime / 1000.0,
                              " the submit Async cost[1] time is : ", avgTime);
                }
            } else  // 2. 如果上一帧是排队数据
            {
                // 2.1：当前帧是不排队数据；
                if (tempTime.taskStartTime > m_AsyncConsumeTime.back().taskEndTime) {
                    app_debug("infer_%d: %s \n", initParams.uniqueID,
                              " the submit Async cost[2] time can not calculate ");
                } else  // 2.2: 当前帧是排队数据
                {
                    uint64_t avgTime = tempTime.taskEndTime - m_AsyncConsumeTime.back().taskEndTime;
                    app_debug("infer_%d: %s %.4f ms, %s %lu us\n", initParams.uniqueID,
                              "the submit Async consume avg 0000000  time is : ", avgTime / 1000.0,
                              " the submit Async cost[3] time is : ", avgTime);
                }
            }
        } else {
            app_debug("infer_%d: %s \n", initParams.uniqueID, "the queue size is 0");
            // 在第一帧数据中记录前一帧任务的完成时间是0；
            tempQueueData->pretaskEndTime = 0;
            tempTime.pretaskEndTime = 0;
        }

        if (gperfStatisFlag != PERF_STATIC_NO) {
            app_debug("infer_%d: %s \n", initParams.uniqueID, "push to queue");
            m_AsyncConsumeTime.push(tempTime);
            app_debug("infer_%d: %s \n", initParams.uniqueID, "push_back to vec");
            m_historyConsumeTime.push_back(tempTime);
        }

        tempQueueData->batchMeta->m_inferOutputMetaVec.push_back(tempQueueData->inferOutputData);

        ipreParentObjSize = tempQueueData->inferOutputData->parentObjMeta.size();
        // need handle infer
        // ES_ASSERT(ipreParentObjSize == 0, "the npu parent Obj meta size is : %d", ipreParentObjSize);

        static int inferIndex = 0;
        if (initParams.dumpflag) {
            for (int iOutputSize = 0; iOutputSize < tempQueueData->inferOutputData->memFd.size(); iOutputSize++) {
                app_debug("infer_%d: %s %ld\n", initParams.uniqueID, "tempQueueData->inferOutputData the fd is ",
                          tempQueueData->inferOutputData->memFd[iOutputSize]);
                app_debug("%s \n", " ES_SYS_Mmap start ");
                app_debug("infer_%d: %s %ld %s %ld\n", initParams.uniqueID, "the fd is ",
                          tempQueueData->inferOutputData->memFd[iOutputSize],
                          "the blk size is : ", tempQueueData->inferOutputData->blkSize[iOutputSize]);
                ES_VOID *pVirAddr =
                    ES_SYS_Mmap(tempQueueData->inferOutputData->memFd[iOutputSize],
                                tempQueueData->inferOutputData->blkSize[iOutputSize], SYS_CACHE_MODE_NOCACHE);
                app_debug("%s \n", " ES_SYS_Mmap end ");
                app_debug("infer_%d: %s %p\n", initParams.uniqueID, "the   pVirAddr is ", pVirAddr);

                std::string fileName = std::string("inferOutput_inferID_") + std::to_string(initParams.uniqueID) +
                                       std::string("_") + std::to_string(inferIndex) + std::string("_") +
                                       std::to_string(iOutputSize);
                dumpFile(fileName, (char *)pVirAddr, tempQueueData->inferOutputData->blkSize[iOutputSize]);
                app_debug("%s \n", " ES_SYS_Munmap start ");
                ES_S32 unmapRet = ES_SYS_Munmap(pVirAddr, tempQueueData->inferOutputData->blkSize[iOutputSize]);
                ES_ASSERT(unmapRet == ES_SUCCESS, "npu get output unmpa failed");
                app_debug("%s \n", " ES_SYS_Munmap end ");
            }
        }
        inferIndex++;

        ///////////////////////////////////////////////////////////////////////////////////////////////////
        ///////////////////这里 next label
        /// 为临时添加，因为更改了eosflag的下发逻辑，所以这里做调整；////////////////
    next:

        bool isEosFlag = tempQueueData->batchMeta->eosFlag;
        if (tempQueueData->isEnd) {
            app_debug("%s %d\n", "TransMitToNextToProcess ", m_taskCount.load());

            while (m_savedBatchMetaQueue.size() > 0) {
                app_debug("%s %lu \n", " the saved batch size is : ", m_savedBatchMetaQueue.size());
                CBatchMeta *savedBatchMeta = m_savedBatchMetaQueue.front();
                app_debug("%s %lu %s %lu\n", " the infer batchIndex is : ", tempQueueData->batchMeta->batchIndex,
                          " the saved batchIndex is : ", savedBatchMeta->batchIndex);
                if (savedBatchMeta->batchIndex < tempQueueData->batchMeta->batchIndex) {
                    app_debug("%s %lu\n", "TransMitToNextToProcess to next batchIndex is ", savedBatchMeta->batchIndex);
                    TransMitToNextToProcess((CBaseMeta *)savedBatchMeta);
                    m_savedBatchMetaQueue.pop_front();
                    gInferOutCount++;
                } else {
                    break;
                }
            }
            int lastTransBatchIndex = tempQueueData->batchMeta->batchIndex;
            app_debug("%s %lu\n", "TransMitToNextToProcess to next batchIndex is ",
                      tempQueueData->batchMeta->batchIndex);
            TransMitToNextToProcess((CBaseMeta *)tempQueueData->batchMeta);

            while (m_savedBatchMetaQueue.size() > 0) {
                app_debug("%s %lu \n", " the saved batch size is : ", m_savedBatchMetaQueue.size());
                CBatchMeta *savedBatchMeta = m_savedBatchMetaQueue.front();
                app_debug("%s %lu %s %lu\n", " the last transmit batchIndex is : ", lastTransBatchIndex,
                          " the saved batchIndex is : ", savedBatchMeta->batchIndex);
                if (savedBatchMeta->batchIndex == lastTransBatchIndex + 1) {
                    app_debug("%s %lu\n", "TransMitToNextToProcess to next batchIndex is ", savedBatchMeta->batchIndex);
                    TransMitToNextToProcess((CBaseMeta *)savedBatchMeta);
                    m_savedBatchMetaQueue.pop_front();
                    lastTransBatchIndex += 1;
                    gInferOutCount++;
                } else {
                    break;
                }
            }

            gInferOutCount++;
        }
        app_debug("infer_%d: %s , %s %d\n", initParams.uniqueID, " transmit success ", " the eos flag is ",
                  int(isEosFlag));
        if (isEosFlag) {
            app_debug("infer_%d: %s \n", initParams.uniqueID, " will exit attach Async ");
            break;
        }
    }
    return;
}

void InferElement::attachInferOutputMeta() {
    app_debug("infer_%d: %s \n", initParams.uniqueID, "attachInferOutputMeta  start");
    std::unique_ptr<queueData> tempQueueData = nullptr;
    tempQueueData.reset((queueData *)m_inferOutputQueue.pop());
    m_taskCount--;

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    /////////////////////////////这里
    /// 为临时添加，因为更改了eosflag的下发逻辑，所以这里做调整；////////////////
    if (tempQueueData->batchMeta->eosFlag) {
        goto next;
    }

    tempQueueData->batchMeta->m_inferOutputMetaVec.push_back(tempQueueData->inferOutputData);
    for (int iOutputSize = 0; iOutputSize < tempQueueData->inferOutputData->memFd.size(); iOutputSize++) {
        app_debug("infer_%d: %s %ld\n", initParams.uniqueID, "tempQueueData->inferOutputData the fd is ",
                  tempQueueData->inferOutputData->memFd[iOutputSize]);
    }

    static int inferIndex = 0;
    if (initParams.dumpflag) {
        for (int iOutputSize = 0; iOutputSize < tempQueueData->inferOutputData->memFd.size(); iOutputSize++) {
            app_debug("infer_%d: %s %ld\n", initParams.uniqueID, "tempQueueData->inferOutputData the fd is ",
                      tempQueueData->inferOutputData->memFd[iOutputSize]);

            app_debug("%s \n", " ES_SYS_Mmap start ");
            app_debug("infer_%d: %s %ld %s %ld\n", initParams.uniqueID, "the fd is ",
                      tempQueueData->inferOutputData->memFd[iOutputSize],
                      "the blk size is : ", tempQueueData->inferOutputData->blkSize[iOutputSize]);
            ES_VOID *pVirAddr =
                ES_SYS_Mmap(tempQueueData->inferOutputData->memFd[iOutputSize],
                            tempQueueData->inferOutputData->blkSize[iOutputSize], SYS_CACHE_MODE_NOCACHE);
            app_debug("%s \n", " ES_SYS_Mmap end ");
            app_debug("infer_%d: %s %p\n", initParams.uniqueID, "the   pVirAddr is ", pVirAddr);

            std::string fileName = std::string("inferOutput_inferID_") + std::to_string(initParams.uniqueID) +
                                   std::string("_") + std::to_string(inferIndex) + std::string("_") +
                                   std::to_string(iOutputSize);
            dumpFile(fileName, (char *)pVirAddr, tempQueueData->inferOutputData->blkSize[iOutputSize]);
            app_debug("%s \n", " ES_SYS_Munmap start ");
            ES_S32 unmapRet = ES_SYS_Munmap(pVirAddr, tempQueueData->inferOutputData->blkSize[iOutputSize]);
            ES_ASSERT(unmapRet == ES_SUCCESS, "npu get output unmpa failed");
            app_debug("%s \n", " ES_SYS_Munmap end ");
        }
    }
    inferIndex++;

///////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////这里  next标签
/// 为临时添加，因为更改了eosflag的下发逻辑，所以这里做调整；//////
next:
    if (tempQueueData->isEnd) {
        // app_debug("infer_%d: %s \n", initParams.uniqueID,
        // "TransMitToNextToProcess"); TransMitToNextToProcess((CBaseMeta
        // *)tempQueueData->batchMeta);
        app_debug("%s \n", "TransMitToNextToProcess");

        while (m_savedBatchMetaQueue.size() > 0) {
            app_debug("%s %lu \n", " the saved batch size is : ", m_savedBatchMetaQueue.size());
            CBatchMeta *savedBatchMeta = m_savedBatchMetaQueue.front();
            app_debug("%s %lu %s %lu\n", " the infer batchIndex is : ", tempQueueData->batchMeta->batchIndex,
                      " the saved batchIndex is : ", savedBatchMeta->batchIndex);
            if (savedBatchMeta->batchIndex < tempQueueData->batchMeta->batchIndex) {
                app_debug("%s %lu\n", "TransMitToNextToProcess to next batchIndex is ", savedBatchMeta->batchIndex);
                TransMitToNextToProcess((CBaseMeta *)savedBatchMeta);
                m_savedBatchMetaQueue.pop_front();
            } else {
                break;
            }
        }

        app_debug("%s %lu\n", "TransMitToNextToProcess to next batchIndex is ", tempQueueData->batchMeta->batchIndex);
        TransMitToNextToProcess((CBaseMeta *)tempQueueData->batchMeta);
    }
    return;
}

#define SIZE_4K 0x1000
#define SIZE_64K 0x10000
#define SIZE_512K 0x80000
#define SIZE_1M 0x100000
#define SIZE_2M 0x200000
#define ALIGN(size, alignment) (((size) + (alignment - 1)) & ~(alignment - 1))

ES_U64 align_size(ES_U64 size) {
    if (size <= SIZE_4K) {
        return ALIGN(size, SIZE_2M);
    } else if (size <= SIZE_64K) {
        return ALIGN(size, SIZE_2M);
    } else if (size <= SIZE_512K) {
        return ALIGN(size, SIZE_2M);
    } else if (size <= SIZE_1M) {
        return ALIGN(size, SIZE_2M);
    } else {
        return ALIGN(size, SIZE_2M);
    }
}
app_ret InferElement::Init() {
    app_debug("infer_%d: %s \n", initParams.uniqueID, "Init  start");
    set_thread_affinity(m_dieIndex);
    app_ret ret = parseConfigFile(m_configFile);
    if (APP_SUCCESS != ret) {
        app_error("infer_%d: %s \n", initParams.uniqueID, "Init  error");
        return ret;
    }
    initParams.dieID = m_dieIndex;

    std::string metaName = "die" + std::to_string(m_dieIndex) + "_InferOut_" + mName;
    inferOutPool = new MetaPool<CInferOutputMeta>(initParams.outputPoolSize, m_dieIndex, metaName);

    // 0. create context
    app_debug("infer_%d: %s \n", initParams.uniqueID, "0. create context");
    m_NpuContextUtils = std::make_shared<NpuContextUtils>(initParams.isAsync);
    if (initParams.isAsync) {
        mPerfType = ASYNC_PERF_ELEMENT;
    } else {
        mPerfType = SYNC_PERF_ELEMENT;
    }
    int flag = 0;  // m_NpuContextUtils->preparation(initParams.dieID);
    std::thread t([this]() { this->m_NpuContextUtils->preparation(this->initParams.dieID); });
    // 等待线程完成
    t.join();

    if (0 != flag) {
        app_error("infer_%d: %s \n", initParams.uniqueID, "0. create context  fail");
        return APP_FAILURE;
    }
    app_debug("infer_%d: %s \n", initParams.uniqueID, "0. create context  success");

    // 1. load model
    app_debug("infer_%d: %s \n", initParams.uniqueID, "1. load model");
    m_NpuUtilsPtr = std::make_shared<NpuUtils>(m_NpuContextUtils.get());
    flag = m_NpuUtilsPtr->loadModel(initParams.modelFileName);
    if (0 != flag) {
        app_error("infer_%d: %s \n", initParams.uniqueID, "1. load model  fail");
        return APP_FAILURE;
    }
    app_debug("infer_%d: %s \n", initParams.uniqueID, "1. load model  success");

    // queryThreadFlag = true;

    // 2. get model input and output descriptor
    app_debug("infer_%d: %s \n", initParams.uniqueID, "2. get model input and output descriptor");

    m_modelInputInfo = m_NpuUtilsPtr->getInputTensorDesc();
    m_modelOutputInfo = m_NpuUtilsPtr->getOutputTensorDesc();

    if (0 == m_modelInputInfo.size() || 0 == m_modelOutputInfo.size()) {
        app_debug("infer_%d: %s \n", initParams.uniqueID, "2. the model input or output count is 0");
        return APP_FAILURE;
    }
    app_debug("infer_%d: %s [%d, %d]\n", initParams.uniqueID,
              "2.5 the model input or output count is :", int(m_modelInputInfo.size()), int(m_modelOutputInfo.size()));

    // 3. prepare buffer pool
    app_debug("infer_%d: %s \n", initParams.uniqueID, "3. prepare buffer pool");
    for (int iPoolSize = 0; iPoolSize < m_modelOutputInfo.size(); iPoolSize++) {
        VB_POOL_CONFIG_S poolCfg = {0};
        poolCfg.blkCnt = initParams.outputPoolSize;
        poolCfg.blkSize = align_size(((size_t)(m_modelOutputInfo[iPoolSize].dataSize)));
        poolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
        memcpy(poolCfg.mmzName, m_VBName.c_str(), strlen(m_VBName.c_str()));
        app_debug("infer_%d: %s %d %s %ld\n", initParams.uniqueID, "the output buffer pool cnt is : ", poolCfg.blkCnt,
                  "the buffer size is : ", poolCfg.blkSize);
        app_debug("infer_%d: %s %d\n", initParams.uniqueID, "will create inferOutputPool, the index is :", iPoolSize);
        VB_POOL inferOutputTempPool;
        ret = PL_ES_VB_CreatePool(&poolCfg, &inferOutputTempPool);
        if (ES_SUCCESS != ret) {
            app_error("%s create pool failed.", __FUNCTION__);
            return APP_FAILURE;
        }
        inferOutputPool.push_back(inferOutputTempPool);
        app_debug("infer_%d: %s \n", initParams.uniqueID, "inferOutputTempPool create success");
    }

    app_debug("infer_%d: %s \n", initParams.uniqueID, "Init  end");
    return APP_SUCCESS;
}

app_ret InferElement::Finish() {
    app_debug("%s %s\n", mName.c_str(), " InferElement::Finish start ");
    // queryThreadFlag = false;

    app_debug("infer_%d: %s \n", initParams.uniqueID, "releaseModel");
    m_NpuUtilsPtr->releaseModel();

    app_debug("infer_%d: %s \n", initParams.uniqueID, "releaseContext");
    m_NpuContextUtils->releaseContext();

    app_debug("infer_%d: %s \n", initParams.uniqueID, "will destory pool ");
    for (int iPoolIndex = 0; iPoolIndex < inferOutputPool.size(); iPoolIndex++) {
        VB_POOL tempPool = inferOutputPool[iPoolIndex];
        PL_ES_VB_DestroyPool(tempPool);
    }
    delete inferOutPool;
    freeNumaNode(this, sizeof(InferElement));
    app_debug("%s \n", "destory pool success ");
    app_debug("%s \n", "InferElement::Finish end ");
    return APP_SUCCESS;
}

static ES_S32 taskCallBack(void *data) {
    app_debug("%s \n", "taskCallBack  start");
    queueData *_queueData = (queueData *)(data);
    _queueData->taskEndTime = (uint64_t)esclock();
    _queueData->pNpuOutCount->fetch_add(1, std::memory_order_relaxed);
    _queueData->pinferOutputQueue->push_back(_queueData);
    app_warn("%s \n", "taskCallBack  end");
    return 0;
}

app_ret InferElement::ProcessData(CBaseMeta *baseMeta, CElement const *privious) {
    if (!m_cpuSetFlag) {
        // cpu_set_t cpuset;
        // CPU_ZERO(&cpuset);
        // CPU_SET(m_cpuID, &cpuset);
        // pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        set_thread_affinity(m_dieIndex);
        m_cpuSetFlag = true;
    }
    app_ret ret = APP_SUCCESS;
    int idMatchCount = 0;
    app_debug("infer_%d in: %s \n", initParams.uniqueID, "ProcessData  start");
    CBatchMeta *batchMeta = (CBatchMeta *)baseMeta;

    if (batchMeta->m_inferOutputMetaVec.size() != 0) batchMeta->releaseInferOutputMeta();

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////这里为临时添加，因为更改了eosflag的下发逻辑，所以这里做调整；///////////////
    if (batchMeta->eosFlag) {
        std::unique_ptr<queueData> argData = std::make_unique<queueData>();
        argData->isEnd = true;
        argData->batchMeta = batchMeta;
        argData->pinferOutputQueue = &m_inferOutputQueue;
        argData->pNpuOutCount = &gNpuOutCount;
        if (initParams.isAsync) {
            while (m_taskCount > 0 || gNpuInCount != gNpuOutCount) {
                usleep(10 * 1000);
            }
            usleep(1000 * 1000);
            app_debug("infer_%d: %s \n", initParams.uniqueID, " get the eos flag, will end the element");
            taskCallBack(argData.get());
            argData.release();
        } else {
            taskCallBack(argData.get());
            argData.release();
            attachInferOutputMeta();
        }
        app_debug("ElementInner eos InferElement");
        return APP_SUCCESS;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    gInferInCount++;
    int iBachSize = batchMeta->mBatchedImgs.size();
    app_debug("infer_%d: %s %d\n", initParams.uniqueID, " the preprocess data size is : ", iBachSize);
    for (int index = 0; index < iBachSize; index++) {
        app_debug("infer_%d: %s %d\n", initParams.uniqueID,
                  "start  to   infer , the preprocess data index is : ", index);
        // 0 :  judge if will infer
        CPreprocessMeta *preMeta = batchMeta->mBatchedImgs[index];

        if (std::find(preMeta->targetInferIds.begin(), preMeta->targetInferIds.end(), initParams.uniqueID) ==
            preMeta->targetInferIds.end()) {
            app_info("infer_%d: %s \n", initParams.uniqueID, " can not find the match preprocessMeta ");
            continue;
        }
        idMatchCount++;

        // 1 : prepare input fd
        app_debug("infer_%d: %s %lld\n", initParams.uniqueID,
                  " <<  prepare  input  fd >>, the input fd is : ", preMeta->memFd);

        std::vector<ES_U64> inputFd;
        inputFd.push_back(preMeta->memFd);

        // 2. prepare output fd
        app_debug("infer_%d: %s \n", initParams.uniqueID, " <<  prepare  output  fd >>");
        CInferOutputMeta *inferOutputMeta = inferOutPool->allocate();  // new CInferOutputMeta;
        inferOutputMeta->pool = inferOutPool;
        inferOutputMeta->parentObjMeta = preMeta->originObjMetas;
        inferOutputMeta->parentFrameMeta = preMeta->originFrameMetas;
        // int ipreParentObjSize = preMeta->originObjMetas.size();
        // ES_ASSERT(ipreParentObjSize == 0, "the npu parent Obj meta size is : %d", ipreParentObjSize);
        inferOutputMeta->aspectRatio = preMeta->aspectRatio;
        inferOutputMeta->aspectRatioExtX = preMeta->aspectRatioExtX;
        inferOutputMeta->aspectRatioExtY = preMeta->aspectRatioExtY;
        inferOutputMeta->aspectRatioExtW = preMeta->aspectRatioExtW;
        inferOutputMeta->aspectRatioExtH = preMeta->aspectRatioExtH;
        app_debug("\n %p preMeta infer initParams.uniqueID %d  size  [%d %d } ext{%f %f %f %f}\n", preMeta,
                  inferOutputMeta->aspectRatioExtY.size(), preMeta->aspectRatioExtY.size(), initParams.uniqueID,
                  inferOutputMeta->aspectRatioExtX[0], inferOutputMeta->aspectRatioExtY[0],
                  inferOutputMeta->aspectRatioExtW[0], inferOutputMeta->aspectRatioExtH[0]);
        // m_batchRealFrameNum.push_back(preMeta->originFrameMetas.size());
        inferOutputMeta->uniqueID = initParams.uniqueID;
        for (int iOutputFdSize = 0; iOutputFdSize < m_modelOutputInfo.size(); iOutputFdSize++) {
            inferOutputMeta->blkSize.push_back(m_modelOutputInfo[iOutputFdSize].dataSize);
            app_debug("infer_%d: %s \n", initParams.uniqueID, "will PL_ES_VB_GetBlock");
            ES_U64 tempOutputFd;
            ret = PL_ES_VB_GetBlock(inferOutputPool[iOutputFdSize], m_modelOutputInfo[iOutputFdSize].dataSize,
                                    m_VBName.c_str(), &tempOutputFd, mName.c_str());
            app_debug("infer_%d: %s %d, %s %ld, %s %ld\n", initParams.uniqueID, "PL_ES_VB_GetBlock ret is ", ret,
                      "PL_ES_VB_GetBlock the fd is ", tempOutputFd, "PL_ES_VB_GetBlock the size is ",
                      m_modelOutputInfo[iOutputFdSize].dataSize);

            if (ret != ES_SUCCESS) {
                app_error("%s get a block from pool %d failed.", __FUNCTION__, inferOutputPool);
                return APP_FAILURE;
            }
            inferOutputMeta->memFd.push_back(tempOutputFd);
        }

        inferOutputMeta->onputDataInfo = m_modelOutputInfo;

        std::vector<ES_U64> outputFd;
        outputFd = inferOutputMeta->memFd;

        for (int iOutputFdSize = 0; iOutputFdSize < outputFd.size(); iOutputFdSize++) {
            app_debug("infer_%d: %s %ld\n", initParams.uniqueID, "the output fd is ", outputFd[iOutputFdSize]);
        }

        // 3. prepare the callback data
        std::unique_ptr<queueData> argData = std::make_unique<queueData>();
        if (index == iBachSize - 1) {
            argData->isEnd = true;
        } else {
            argData->isEnd = false;
        }
        argData->batchMeta = batchMeta;
        argData->inferOutputData = inferOutputMeta;
        argData->pinferOutputQueue = &m_inferOutputQueue;
        argData->pNpuOutCount = &gNpuOutCount;
        // 4. submit
        app_debug("infer_%d: %s \n", initParams.uniqueID, " <<  submit  data >>");

        if (initParams.isAsync) {
            m_taskCount++;
            gNpuInCount++;
            ret = m_NpuUtilsPtr->submitAsync(inputFd, outputFd, taskCallBack, argData.get());
            if (APP_SUCCESS != ret) {
                app_error("infer_%d: %s \n", initParams.uniqueID, "the submit async is error");
            }
            argData.release();
        } else {
            m_taskCount++;
            ret = m_NpuUtilsPtr->submitSync(inputFd, outputFd, taskCallBack, argData.get());
            uint64_t duratime = argData->taskEndTime - argData->taskStartTime;
            AsyncTimeData tempTime(argData->taskStartTime, argData->taskEndTime, 0);
            m_historyConsumeTime.push_back(tempTime);
            app_debug("infer_%d: %s %.4f ms, %s %lu us\n", initParams.uniqueID,
                      "the submit sync consume time is : ", duratime / 1000.0,
                      "the submit sync consume time is : ", duratime);

            if (APP_SUCCESS != ret) {
                app_error("infer_%d: %s \n", initParams.uniqueID, "the submit sync is error");
            }

            taskCallBack(argData.get());
            argData.release();
            attachInferOutputMeta();
        }

        if (APP_SUCCESS != ret) {
            if (APP_SUCCESS != ret) {
                app_error("infer_%d: %s \n", initParams.uniqueID, "processdata error");
            }
            return ret;
        }
    }
    app_debug("\n idMatchCount %d m_taskCount %ld \n", idMatchCount, m_taskCount.load());

    if (0 == idMatchCount) {
        // 这里情况很复杂，主要为了应对二级推理以及跳帧的情况,大多数时候，不会触发这里
        std::lock_guard<std::mutex> lock(gNpuMtx);
        if (m_taskCount > 0) {
            app_debug("infer_%d: %s \n", initParams.uniqueID,
                      " the taskCount >0, and this batchMeta no need to infer "
                      ", will add to queue ");
            app_debug("%s %lu\n", " will push this batchMeta to queue, the batchIndex is ", batchMeta->batchIndex);
            m_savedBatchMetaQueue.push_back(batchMeta);
        } else if (0 == m_taskCount) {
            if (m_savedBatchMetaQueue.size() > 0) {
                app_debug("infer_%d: %s \n", initParams.uniqueID,
                          " the taskCount >0, and this batchMeta no need to "
                          "infer , will add to queue ");
                app_debug("%s %lu\n", " will push this batchMeta to queue, the batchIndex is ", batchMeta->batchIndex);
                m_savedBatchMetaQueue.push_back(batchMeta);
            } else {
                app_debug("infer_%d: %s \n", initParams.uniqueID,
                          " the taskCount is 0, and this batchMeta no need to "
                          "infer, will tranmit to next ");
                app_debug("%s %lu\n", " will push this TransMitToNextToProcess to next batchIndex is ",
                          batchMeta->batchIndex);
                TransMitToNextToProcess((CBaseMeta *)batchMeta);
                gInferOutCount++;
            }
        } else {
            if (m_savedBatchMetaQueue.size() > 0) {
                app_debug("infer_%d: %s \n", initParams.uniqueID,
                          " the taskCount >0, and this batchMeta no need to "
                          "infer , will add to queue ");
                app_debug("%s %lu\n", " will push this batchMeta to queue, the batchIndex is ", batchMeta->batchIndex);
                m_savedBatchMetaQueue.push_back(batchMeta);
            } else {
                app_debug("infer_%d: %s \n", initParams.uniqueID,
                          " the taskCount is 0, and this batchMeta no need to "
                          "infer, will tranmit to next ");
                app_debug("%s %lu\n", " will push this TransMitToNextToProcess to next batchIndex is ",
                          batchMeta->batchIndex);
                TransMitToNextToProcess((CBaseMeta *)batchMeta);
                gInferOutCount++;
            }
        }
    }
    app_debug("infer_%d out: %s \n", initParams.uniqueID, " << ProcessData  end >> ");
    return ret;
}

app_ret InferElement::ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *privious) {
    ProcessData(baseMeta, this);
    return APP_SUCCESS;
}

extern "C" CElement *createEsInferElement(const char *name, const char *configFile, int dieIndex) {
    return new (bindNumaNode(dieIndex, sizeof(InferElement))) InferElement(name, configFile, dieIndex);
}
