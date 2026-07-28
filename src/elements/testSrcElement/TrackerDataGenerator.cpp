#define PL_LOG_ID PL_LOG_TESTSRC
#include "TrackerDataGenerator.h"

#include <unistd.h>

TrackerDataGenerator::~TrackerDataGenerator() {
    if (m_trackerInputDataInfo.poolId) {
        PL_ES_VB_DestroyPool(m_trackerInputDataInfo.poolId);
    }
}

// Implementation of prepareTrackerData, moved from the original testSrcElement.cpp
CBatchMeta *TrackerDataGenerator::prepareTrackerData(int frameIndex, FILE *fp) {
    ES_U32 width = m_trackerInputDataInfo.img_width;
    ES_U32 height = m_trackerInputDataInfo.img_height;

    CBatchMeta *batchMeta = new CBatchMeta;
    batchMeta->eosFlag = false;
    for (int batchIndex = 0; batchIndex < m_trackerInputDataInfo.batchSize; batchIndex++) {
        CFrameMeta *frameMeta = new CFrameMeta;
        for (int ppi = 0; ppi < 2; ppi++) {
            VIDEO_FRAME_INFO_S *videoFrameInfo = createVideoFrame(frameIndex, width, height, 30); // Add default fps value
            ES_U32 size =
                videoFrameInfo->videoFrame.stride[0] * height + videoFrameInfo->videoFrame.stride[1] * height / 2;

            ES_S32 ret = PL_ES_VB_GetBlock(m_trackerInputDataInfo.poolId, size, "mmz_nid_0_part_0",
                                           &videoFrameInfo->videoFrame.fd);
            videoFrameInfo->poolId = ES_VB_Handle2PoolId(videoFrameInfo->videoFrame.fd);
            while (ret || videoFrameInfo->poolId < 0) {
                printf("%s get a block from poolid %d ret %d failed. \n", __FUNCTION__, m_trackerInputDataInfo.poolId,
                       ret);
                usleep(1000);
                ret = PL_ES_VB_GetBlock(m_trackerInputDataInfo.poolId, size, "mmz_nid_0_part_0",
                                        &videoFrameInfo->videoFrame.fd);
                videoFrameInfo->poolId = ES_VB_Handle2PoolId(videoFrameInfo->videoFrame.fd);
            }

            ES_U64 fd = videoFrameInfo->videoFrame.fd;
            ES_U64 *pVirAddr = (ES_U64 *)ES_SYS_Mmap(fd, size, SYS_CACHE_MODE_NOCACHE);
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

            CImage *cimage = new CImage;
            cimage->mPic = videoFrameInfo;
            frameMeta->images.push_back(cimage);
        }

        if (frameIndex % 4 == 0) {
            for (int i = 0; i < 2; i++) {
                CObjectMeta *objMeta1 = new CObjectMeta();
                objMeta1->detectorBboxInfo.left = 0.5;
                objMeta1->detectorBboxInfo.top = 0.6;
                objMeta1->detectorBboxInfo.width = 0.1;
                objMeta1->detectorBboxInfo.height = 0.2;
                objMeta1->detectorConfidence = 0.9;
                objMeta1->classId = i;
                frameMeta->objs.push_back(objMeta1);
            }
        }

        frameMeta->indexInBatch = batchIndex;
        frameMeta->padIndex = batchIndex;
        batchMeta->addFrameMeta(frameMeta);
    }

    return batchMeta;
}

app_ret TrackerDataGenerator::Init(const YAML::Node &config, const std::string &vbName, int fps, uint16_t loopNum) {
    m_loopNum = loopNum;
    auto trackerConfig = config["createTrackerInput"];

    m_trackerInputDataInfo.batchSize = trackerConfig["batch_size"].as<int>();
    m_trackerInputDataInfo.img_width = trackerConfig["width"].as<int>();
    m_trackerInputDataInfo.img_height = trackerConfig["height"].as<int>();
    m_trackerInputDataInfo.inputFileName = trackerConfig["inputFileData"].as<string>();

    VB_POOL_CONFIG_S poolCfg = {0};
    poolCfg.blkCnt = 10;
    poolCfg.blkSize = m_trackerInputDataInfo.img_width * m_trackerInputDataInfo.img_height * 3 / 2;
    poolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
    memcpy(poolCfg.mmzName, vbName.c_str(), vbName.length());
    ES_S32 ret = PL_ES_VB_CreatePool(&poolCfg, &m_trackerInputDataInfo.poolId);
    app_debug("%s %d\n", "create pool ret is :", (int)ret);
    if (ret != ES_SUCCESS) {
        return APP_FAILURE;
    }
    return APP_SUCCESS;
}

CBaseMeta *TrackerDataGenerator::GenerateData(int frameIndex, FILE *&fp) {
    CBatchMeta *batchMeta = nullptr;
    if (frameIndex == m_loopNum) {
        batchMeta = new CBatchMeta;
        batchMeta->eosFlag = true;
        for (int i = 0; i < m_trackerInputDataInfo.batchSize; i++) {
            CFrameMeta *frameMeta = new CFrameMeta;
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
            fp = fopen(m_trackerInputDataInfo.inputFileName.c_str(), "r");
            if (fp == NULL) {
                app_error("Failed to open file: %s", m_trackerInputDataInfo.inputFileName.c_str());
                return nullptr;
            }
        }
        batchMeta = prepareTrackerData(frameIndex, fp);
    }
    return (CBaseMeta *)(batchMeta);
}
