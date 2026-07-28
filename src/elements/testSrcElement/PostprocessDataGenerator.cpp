#define PL_LOG_ID PL_LOG_TESTSRC
#include "PostprocessDataGenerator.h"

PostprocessDataGenerator::~PostprocessDataGenerator() {
    for (auto poolId : m_postprocessInputDataInfo.postprocessInputPool) {
        PL_ES_VB_DestroyPool(poolId);
    }
}

// Implementation of preparePostprocessInputData, moved from the original testSrcElement.cpp
CBatchMeta* PostprocessDataGenerator::preparePostprocessInputData() {
    int ret = 0;
    static int frameIndex = 0;
    CBatchMeta* batchMeta = new CBatchMeta();

    CFrameMeta* oriFrameMeta = new CFrameMeta();
    oriFrameMeta->index = frameIndex++;

    CInferOutputMeta* inferOutputMeta = new CInferOutputMeta();
    inferOutputMeta->uniqueID = m_postprocessInputDataInfo.outputInferID;
    inferOutputMeta->parentFrameMeta.push_back(oriFrameMeta);

    for (int iSize = 0; iSize < m_postprocessInputDataInfo.inputFileName.size(); iSize++) {
        ES_U64 tempFd;
        ret = PL_ES_VB_GetBlock(m_postprocessInputDataInfo.postprocessInputPool[iSize],
                                m_postprocessInputDataInfo.size[iSize], "mmz_nid_0_part_0", &tempFd);
        if (ret != ES_SUCCESS) {
            app_error("%s get a block from pool %d failed.", __FUNCTION__,
                      m_postprocessInputDataInfo.postprocessInputPool[iSize]);
            // Clean up and return
            delete inferOutputMeta;
            delete oriFrameMeta;
            delete batchMeta;
            return nullptr;
        }
        inferOutputMeta->memFd.push_back(tempFd);

        ModelInfo tempModelInfo;
        tempModelInfo.dims = m_postprocessInputDataInfo.dataDims[iSize];
        tempModelInfo.dataSize = m_postprocessInputDataInfo.size[iSize];
        tempModelInfo.dataType = m_postprocessInputDataInfo.dataType;
        inferOutputMeta->onputDataInfo.push_back(tempModelInfo);

        inferOutputMeta->pVirAddr =
            ES_SYS_Mmap(inferOutputMeta->memFd[iSize], m_postprocessInputDataInfo.size[iSize], SYS_CACHE_MODE_NOCACHE);

        FILE* fp = fopen(m_postprocessInputDataInfo.inputFileName[iSize].c_str(), "r");
        if (NULL != fp) {
            size_t expected = inferOutputMeta->onputDataInfo[iSize].dataSize;
            size_t actual = fread(inferOutputMeta->pVirAddr, 1, expected, fp);  // size=1, count=expected

            if (actual != expected) {
                if (feof(fp)) {
                    fprintf(stderr, "Read only %zu of %zu bytes: premature EOF\n", actual, expected);
                } else {
                    perror("fread failed");
                }
            }
            fclose(fp);
        }

        ES_SYS_Munmap(inferOutputMeta->pVirAddr, m_postprocessInputDataInfo.size[iSize]);
    }

    batchMeta->addFrameMeta(oriFrameMeta);
    batchMeta->m_inferOutputMetaVec.push_back(inferOutputMeta);
    return batchMeta;
}

app_ret PostprocessDataGenerator::Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) {
    m_loopNum = loopNum;
    auto postConfig = config["createPostprocessInput"];

    m_postprocessInputDataInfo.batchSize = (uint8_t)postConfig["batchSize"].as<int>();
    m_postprocessInputDataInfo.outputInferID = postConfig["outputInferID"].as<int>();

    if (YAML::NodeType::Sequence == postConfig["inputData"].Type()) {
        for (const auto& item : postConfig["inputData"]) {
            m_postprocessInputDataInfo.inputFileName.push_back(item.as<std::string>());
        }
    } else {
        m_postprocessInputDataInfo.inputFileName.push_back(postConfig["inputData"].as<std::string>());
    }

    for (const auto& fileName : m_postprocessInputDataInfo.inputFileName) {
        FILE* fp = fopen(fileName.c_str(), "r");
        if (NULL != fp) {
            fseek(fp, 0, SEEK_END);
            m_postprocessInputDataInfo.size.push_back(ftell(fp));
            fclose(fp);
        } else {
            app_error("Cannot open file %s to get size", fileName.c_str());
            return APP_FAILURE;
        }
    }

    for (int i = 0; i < m_postprocessInputDataInfo.size.size(); ++i) {
        VB_POOL_CONFIG_S poolCfg = {0};
        poolCfg.blkCnt = 100;
        poolCfg.blkSize = m_postprocessInputDataInfo.size[i];
        poolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
        memcpy(poolCfg.mmzName, vbName.c_str(), vbName.length());
        VB_POOL tempPool;
        ES_S32 ret = PL_ES_VB_CreatePool(&poolCfg, &tempPool);
        if (ES_SUCCESS != ret) {
            app_error("%s create pool failed.", __FUNCTION__);
            return APP_FAILURE;
        }
        m_postprocessInputDataInfo.postprocessInputPool.push_back(tempPool);
    }

    for (const auto& dimNode : postConfig["dims"]) {
        CDims tempDim;
        tempDim.n = dimNode[0].as<int>();
        tempDim.h = dimNode[1].as<int>();
        tempDim.w = dimNode[2].as<int>();
        tempDim.c = dimNode[3].as<int>();
        m_postprocessInputDataInfo.dataDims.push_back(tempDim);
    }

    m_postprocessInputDataInfo.dataType = (CDataType)postConfig["datatype"].as<int>();

    return APP_SUCCESS;
}

CBaseMeta* PostprocessDataGenerator::GenerateData(int frameIndex, FILE*& fp) {
    CBatchMeta* batchMeta = preparePostprocessInputData();
    if (frameIndex == m_loopNum) {
        if (batchMeta) {
            batchMeta->eosFlag = true;
        } else {
            batchMeta = new CBatchMeta();
            batchMeta->eosFlag = true;
        }
    }
    return (CBaseMeta*)(batchMeta);
}
