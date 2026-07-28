#define PL_LOG_ID PL_LOG_TESTSRC
#include "VideoGridDataGenerator.h"

#include <unistd.h>

VideoGridDataGenerator::~VideoGridDataGenerator() {
    if (m_videogridInputDataInfo.pool) {
        PL_ES_VB_DestroyPool(m_videogridInputDataInfo.pool);
    }
}

app_ret VideoGridDataGenerator::Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) {
    m_loopNum = loopNum;
    auto gridConfig = config["videogridInput"];

    m_videogridInputDataInfo.filepath = gridConfig["filepath"].as<std::string>();
    m_videogridInputDataInfo.width = gridConfig["width"].as<int>();
    m_videogridInputDataInfo.height = gridConfig["height"].as<int>();
    m_videogridInputDataInfo.dateType = gridConfig["datatype"].as<int>();

    VB_POOL_CONFIG_S poolCfg = {0};
    poolCfg.blkCnt = 10;
    poolCfg.blkSize = m_videogridInputDataInfo.width * m_videogridInputDataInfo.height * 3 / 2;
    poolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
    memcpy(poolCfg.mmzName, vbName.c_str(), vbName.length());
    ES_S32 ret = PL_ES_VB_CreatePool(&poolCfg, &m_videogridInputDataInfo.pool);
    app_debug("%s %d\n", "create pool ret is :", (int)ret);
    if (ret != ES_SUCCESS) {
        return APP_FAILURE;
    }
    return APP_SUCCESS;
}

CBaseMeta* VideoGridDataGenerator::GenerateData(int frameIndex, FILE*& fp) {
    CFrameMeta* frameMeta = nullptr;
    CBatchMeta* batchMeta = nullptr;

    if (frameIndex == m_loopNum) {
        if (m_videogridInputDataInfo.dateType <= 1) {
            frameMeta = new CFrameMeta;
            frameMeta->eosFlag = true;
            frameMeta->mMetaType = FRAME_META;
            return static_cast<CBaseMeta*>(frameMeta);
        } else {
            batchMeta = new CBatchMeta;
            batchMeta->eosFlag = true;
            batchMeta->mMetaType = BATCH_META;
            return static_cast<CBaseMeta*>(batchMeta);
        }
        if (NULL != fp) {
            fclose(fp);
            fp = NULL;
        }
    }

    if (fp == NULL) {
        fp = fopen(m_videogridInputDataInfo.filepath.c_str(), "r");
        if (fp == NULL) {
            app_error("Failed to open file: %s", m_videogridInputDataInfo.filepath.c_str());
            return nullptr;
        }
    }

    ES_U32 width = m_videogridInputDataInfo.width;
    ES_U32 height = m_videogridInputDataInfo.height;
    VIDEO_FRAME_INFO_S* videoFrameInfo = createVideoFrame(frameIndex, width, height, 30); // Add default fps value
    ES_U32 size = videoFrameInfo->videoFrame.stride[0] * height + videoFrameInfo->videoFrame.stride[1] * height / 2;

    ES_S32 ret =
        PL_ES_VB_GetBlock(m_videogridInputDataInfo.pool, size, "mmz_nid_0_part_0", &videoFrameInfo->videoFrame.fd);
    while (ret || videoFrameInfo->poolId < 0) {
        printf("%s get a block from poolid %d ret %d failed.", __FUNCTION__, m_videogridInputDataInfo.pool, ret);
        usleep(1000000);
        ret =
            PL_ES_VB_GetBlock(m_videogridInputDataInfo.pool, size, "mmz_nid_0_part_0", &videoFrameInfo->videoFrame.fd);
    }

    ES_U64 fd = videoFrameInfo->videoFrame.fd;
    ES_U64* pVirAddr = (ES_U64*)ES_SYS_Mmap(fd, size, SYS_CACHE_MODE_NOCACHE);
    if (NULL == pVirAddr) {
        free(videoFrameInfo);
        return nullptr;
    }

    size_t readBytes = fread(pVirAddr, 1, size, fp);
    if (readBytes != size) {
        app_debug("Warning: Only read %zu bytes out of %d expected bytes\n", readBytes, size);
    }
    ES_SYS_Munmap(pVirAddr, size);

    CImage* cimage = new CImage;
    cimage->mPic = videoFrameInfo;
    if (m_videogridInputDataInfo.dateType == 0) {
        frameMeta = new CFrameMeta;
        frameMeta->images.push_back(cimage);
        frameMeta->mMetaType = FRAME_META;
        return static_cast<CBaseMeta*>(frameMeta);
    } else if (m_videogridInputDataInfo.dateType == 1) {
        frameMeta = new CFrameMeta;
        CImage* cimage1 = new CImage;
        cimage1->mPic = NULL;
        frameMeta->images.push_back(cimage1);
        frameMeta->images.push_back(cimage);
        frameMeta->mMetaType = FRAME_META;
        return static_cast<CBaseMeta*>(frameMeta);
    }
    // Note: Original code did not handle dateType > 1 for videogrid, returning null.
    return nullptr;
}
