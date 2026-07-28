#define PL_LOG_ID PL_LOG_TESTSRC
#include "TTSDataGenerator.h"

#include <unistd.h>

TTSDataGenerator::~TTSDataGenerator() {
    if (m_ttsInputDataInfo.videoFramePoolId) {
        PL_ES_VB_DestroyPool(m_ttsInputDataInfo.videoFramePoolId);
    }
    if (m_ttsInputDataInfo.inferPoolId) {
        PL_ES_VB_DestroyPool(m_ttsInputDataInfo.inferPoolId);
    }
}

CBatchMeta* TTSDataGenerator::prepareTTSData(int frameIndex, FILE* fp) {
    ES_U32 width = m_ttsInputDataInfo.img_width;
    ES_U32 height = m_ttsInputDataInfo.img_height;

    CBatchMeta* batchMeta = new CBatchMeta;
    batchMeta->eosFlag = false;
    for (int batchIndex = 0; batchIndex < m_ttsInputDataInfo.batchSize; batchIndex++) {
        CFrameMeta* frameMeta = new CFrameMeta;
        for (int ppi = 0; ppi < 2; ppi++) {
            VIDEO_FRAME_INFO_S* videoFrameInfo = createVideoFrame(frameIndex, width, height, 30); // Add default fps value
            ES_U32 size =
                videoFrameInfo->videoFrame.stride[0] * height + videoFrameInfo->videoFrame.stride[1] * height / 2;

            ES_S32 ret = PL_ES_VB_GetBlock(m_ttsInputDataInfo.videoFramePoolId, size, "mmz_nid_0_part_0",
                                           &videoFrameInfo->videoFrame.fd);
            while (ret) {
                printf("%s get a block from poolid %d ret %d failed.", __FUNCTION__,
                       m_ttsInputDataInfo.videoFramePoolId, ret);
                usleep(1000);
                ret = PL_ES_VB_GetBlock(m_ttsInputDataInfo.videoFramePoolId, size, "mmz_nid_0_part_0",
                                        &videoFrameInfo->videoFrame.fd);
            }

            ES_U64 fd = videoFrameInfo->videoFrame.fd;
            ES_U64* pVirAddr = (ES_U64*)ES_SYS_Mmap(fd, size, SYS_CACHE_MODE_NOCACHE);
            if (NULL == pVirAddr) {
                free(videoFrameInfo);
                delete frameMeta;
                delete batchMeta;
                return nullptr;
            }

            size_t readBytes = fread(pVirAddr, 1, size, fp);
            if (readBytes != size) {
                app_debug("Warning: Only read %zu bytes out of %d expected bytes\n", readBytes, size);
            }
            ES_SYS_Munmap(pVirAddr, size);

            CImage* cimage = new CImage;
            cimage->mPic = videoFrameInfo;
            frameMeta->images.push_back(cimage);
        }

        frameMeta->indexInBatch = batchIndex;
        frameMeta->padIndex = batchIndex;

        for (int m = 0; m < 4; m++) {
            CObjectMeta* objMeta = new CObjectMeta();
            if (m == 0) {
                frameMeta->objs.push_back(objMeta);
                continue;
            }
            // The original selectedFaces and personInfo logic was commented out or incomplete.
            // Replicating the structure but noting it's mostly placeholder.
            // objMeta->selectedFaces = new CFaceSelectMeta();
            // objMeta->selectedFaces->personInfo.id = 0;
            // ...

            frameMeta->objs.push_back(objMeta);
        }
        batchMeta->addFrameMeta(frameMeta);
    }
    return batchMeta;
}

app_ret TTSDataGenerator::Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) {
    m_loopNum = loopNum;
    auto ttsConfig = config["TTSInput"];

    m_ttsInputDataInfo.batchSize = ttsConfig["batch_size"].as<int>();
    m_ttsInputDataInfo.img_width = ttsConfig["width"].as<int>();
    m_ttsInputDataInfo.img_height = ttsConfig["height"].as<int>();
    m_ttsInputDataInfo.inputFileName = ttsConfig["inputFileData"].as<std::string>();
    m_ttsInputDataInfo.featureShape = ttsConfig["featureShape"].as<int>();
    m_ttsInputDataInfo.inferBatchSize = ttsConfig["inferBatchSize"].as<int>();

    // Create video frame pool
    VB_POOL_CONFIG_S poolCfg = {0};
    poolCfg.blkCnt = 10;
    poolCfg.blkSize = m_ttsInputDataInfo.img_width * m_ttsInputDataInfo.img_height * 3 / 2;
    poolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
    memcpy(poolCfg.mmzName, vbName.c_str(), vbName.length());
    ES_S32 ret = PL_ES_VB_CreatePool(&poolCfg, &m_ttsInputDataInfo.videoFramePoolId);
    if (ret != ES_SUCCESS) return APP_FAILURE;

    // Create infer tensor pool
    int fackInferTensor_size = 1 * 1 * m_ttsInputDataInfo.inferBatchSize * m_ttsInputDataInfo.featureShape * 1 * 4;
    poolCfg = {0};
    poolCfg.blkCnt = 10 * 3;
    poolCfg.blkSize = fackInferTensor_size;
    poolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
    memcpy(poolCfg.mmzName, vbName.c_str(), vbName.length());
    ret = PL_ES_VB_CreatePool(&poolCfg, &m_ttsInputDataInfo.inferPoolId);
    if (ret != ES_SUCCESS) return APP_FAILURE;

    return APP_SUCCESS;
}

CBaseMeta* TTSDataGenerator::GenerateData(int frameIndex, FILE*& fp) {
    CBatchMeta* batchMeta = nullptr;
    if (frameIndex == m_loopNum) {
        batchMeta = new CBatchMeta;
        batchMeta->eosFlag = true;
        for (int i = 0; i < m_ttsInputDataInfo.batchSize; i++) {
            CFrameMeta* frameMeta = new CFrameMeta;
            frameMeta->eosFlag = true;
            frameMeta->indexInBatch = i;
            frameMeta->padIndex = i;
            batchMeta->addFrameMeta(frameMeta);
        }
        if (NULL != fp) {
            fclose(fp);
            fp = NULL;
        }
    } else {
        if (fp == NULL) {
            fp = fopen(m_ttsInputDataInfo.inputFileName.c_str(), "r");
            if (fp == NULL) {
                app_error("Failed to open file: %s", m_ttsInputDataInfo.inputFileName.c_str());
                return nullptr;
            }
        }
        batchMeta = prepareTTSData(frameIndex, fp);
    }
    return static_cast<CBaseMeta*>(batchMeta);
}
