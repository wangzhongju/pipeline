#define PL_LOG_ID PL_LOG_TESTSRC
#include "InferDataGenerator.h"

// Implementation of prepareInferInputData, moved from the original testSrcElement.cpp
CBatchMeta *InferDataGenerator::prepareInferInputData() {
    int ret = 0;
    app_debug("%s \n", "prepareInferInputData start");
    CBatchMeta *batchMeta = new CBatchMeta();

    static int frameIndex = 0;
    CFrameMeta *oriFrameMeta = new CFrameMeta();
    oriFrameMeta->index = frameIndex;
    frameIndex++;

    CPreprocessMeta *preprocessMeta = new CPreprocessMeta();
    preprocessMeta->targetInferIds.push_back(m_inferInputDataInfo.targetInferID);
    preprocessMeta->targetInferIds.push_back(m_inferInputDataInfo.targetInferID + 1);
    app_debug("%s \n", "PL_ES_VB_GetBlock start");
    ret = PL_ES_VB_GetBlock(m_inferInputDataInfo.pool, m_inferInputDataInfo.size, "mmz_nid_0_part_0",
                            &preprocessMeta->memFd, "testsrcInfer");
    app_debug("%s %d\n", "PL_ES_VB_GetBlock ret is :", ret);
    if (ret != ES_SUCCESS) {
        delete oriFrameMeta;
        delete preprocessMeta;
        delete batchMeta;
        return nullptr;
    }

    preprocessMeta->dims.n = m_inferInputDataInfo.dataDims.n;
    preprocessMeta->dims.c = m_inferInputDataInfo.dataDims.c;
    preprocessMeta->dims.h = m_inferInputDataInfo.dataDims.h;
    preprocessMeta->dims.w = m_inferInputDataInfo.dataDims.w;
    preprocessMeta->data_size = m_inferInputDataInfo.size;

    // avoid inferElement coredump
    float aspectRatio = 0.5;
    preprocessMeta->aspectRatio.push_back(aspectRatio);
    preprocessMeta->aspectRatioExtW.push_back(aspectRatio / 1920);
    preprocessMeta->aspectRatioExtH.push_back(aspectRatio / 1080);
    preprocessMeta->aspectRatioExtX.push_back(0);
    preprocessMeta->aspectRatioExtY.push_back(0);

    preprocessMeta->originFrameMetas.push_back(oriFrameMeta);

    ret = ES_VB_MmapPool(m_inferInputDataInfo.pool);
    app_debug("%s %d\n", "ES_VB_MmapPool ret is :", ret);

    void *pVirAddr = NULL;
    ret = ES_VB_GetBlockVirAddr(m_inferInputDataInfo.pool, preprocessMeta->memFd, &pVirAddr);
    app_debug("%s %d\n", "ES_VB_GetBlockVirAddr ret is :", ret);

    FILE *fp = fopen(m_inferInputDataInfo.inputFileName.c_str(), "r");
    if (NULL != fp) {
        int count = fread(pVirAddr, sizeof(char), m_inferInputDataInfo.size, fp);
        fclose(fp);
        if (count != m_inferInputDataInfo.size) {
            app_debug("%s \n", "read file size is not match the need size ");
        }
    } else {
        app_error("Failed to open file: %s", m_inferInputDataInfo.inputFileName.c_str());
    }

    ret = ES_VB_MunmapPool(m_inferInputDataInfo.pool);
    app_debug("%s %d\n", "ES_VB_MunmapPool ret is :", ret);

    batchMeta->addFrameMeta(oriFrameMeta);
    batchMeta->mBatchedImgs.push_back(preprocessMeta);
    app_debug("%s \n", "prepareInferInputData end");
    return batchMeta;
}

app_ret InferDataGenerator::Init(const YAML::Node &config, const std::string &vbName, int fps, uint16_t loopNum) {
    m_loopNum = loopNum;
    auto inferConfig = config["createInferInput"];

    m_inferInputDataInfo.dataDims.n = inferConfig["dims"][0].as<int>();
    m_inferInputDataInfo.dataDims.h = inferConfig["dims"][1].as<int>();
    m_inferInputDataInfo.dataDims.w = inferConfig["dims"][2].as<int>();
    m_inferInputDataInfo.dataDims.c = inferConfig["dims"][3].as<int>();
    m_inferInputDataInfo.batchSize = (uint8_t)inferConfig["batchSize"].as<int>();
    m_inferInputDataInfo.targetInferID = inferConfig["targetInferID"].as<int>();
    m_inferInputDataInfo.inputFileName = inferConfig["inputData"].as<string>();

    FILE *fp = fopen(m_inferInputDataInfo.inputFileName.c_str(), "r");
    if (NULL != fp) {
        fseek(fp, 0, SEEK_END);
        m_inferInputDataInfo.size = ftell(fp);
        fclose(fp);
    } else {
        app_error("Cannot open file %s to get size", m_inferInputDataInfo.inputFileName.c_str());
        return APP_FAILURE;
    }

    VB_POOL_CONFIG_S poolCfg = {0};
    poolCfg.blkCnt = 200;
    poolCfg.blkSize = m_inferInputDataInfo.size;
    poolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
    memcpy(poolCfg.mmzName, vbName.c_str(), vbName.length());
    ES_S32 ret = PL_ES_VB_CreatePool(&poolCfg, &m_inferInputDataInfo.pool);
    if (ES_SUCCESS != ret) {
        app_error("%s create pool failed.", __FUNCTION__);
        return APP_FAILURE;
    }
    return APP_SUCCESS;
}

CBaseMeta *InferDataGenerator::GenerateData(int frameIndex, FILE *&fp) {
    // Note: The original implementation did not use the file pointer 'fp' for this type.
    // It re-opens the file inside prepareInferInputData.
    CBatchMeta *batchMeta = prepareInferInputData();
    if (frameIndex == m_loopNum) {
        if (batchMeta) {
            batchMeta->eosFlag = true;
        } else {
            batchMeta = new CBatchMeta();
            batchMeta->eosFlag = true;
        }
    }
    return (CBaseMeta *)(batchMeta);
}