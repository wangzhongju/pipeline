#define PL_LOG_ID PL_LOG_TESTSRC
#include "CompareFaceDataGenerator.h"
#include <unistd.h>
#include <cassert>

CompareFaceDataGenerator::~CompareFaceDataGenerator() {
    if (m_compareFaceInputDataInfo.videoFramePoolId) {
        PL_ES_VB_DestroyPool(m_compareFaceInputDataInfo.videoFramePoolId);
    }
    if (m_compareFaceInputDataInfo.inferPoolId) {
        PL_ES_VB_DestroyPool(m_compareFaceInputDataInfo.inferPoolId);
    }
}

app_ret CompareFaceDataGenerator::transVectorToFackInferResult(ES_U64& faceFeatureFd, ES_U64& faceFeatureSize,
                                                               ModelInfo& faceFeatureModelInfo,
                                                               std::vector<float>& featureVect) {
    assert(featureVect.size() == m_compareFaceInputDataInfo.featureShape);

    faceFeatureSize = 1 * 1 * 1 * m_compareFaceInputDataInfo.featureShape * 1 * 4;
    ES_S32 ret = PL_ES_VB_GetBlock(m_compareFaceInputDataInfo.inferPoolId, faceFeatureSize, "mmz_nid_0_part_0", &faceFeatureFd);
    if (ES_SUCCESS != ret) {
        app_error("%s get a block from poolid %d ret %d failed.", __FUNCTION__, m_compareFaceInputDataInfo.inferPoolId, ret);
        return APP_FAILURE;
    }

    ES_U64* pVirAddr = (ES_U64*)ES_SYS_Mmap(faceFeatureFd, faceFeatureSize, SYS_CACHE_MODE_NOCACHE);
    if (NULL == pVirAddr) {
        app_error("%s ES_SYS_Mmap failed", __FUNCTION__);
        return APP_FAILURE;
    }

    float* tensorMat = (float*)pVirAddr;
    for (int w_i = 0; w_i < m_compareFaceInputDataInfo.featureShape; w_i++) {
        tensorMat[w_i] = featureVect[w_i];
    }

    ret = ES_SYS_Munmap(pVirAddr, faceFeatureSize);
    if (ES_SUCCESS != ret) {
        app_error("%s ES_SYS_Munmap failed", __FUNCTION__);
        return APP_FAILURE;
    }

    faceFeatureModelInfo.dims.n = 1;
    faceFeatureModelInfo.dims.c = 1;
    faceFeatureModelInfo.dims.h = 1;
    faceFeatureModelInfo.dims.w = m_compareFaceInputDataInfo.featureShape;
    faceFeatureModelInfo.dataType = DATA_F32;
    faceFeatureModelInfo.dataSize = faceFeatureSize;

    return APP_SUCCESS;
}

CBatchMeta* CompareFaceDataGenerator::prepareCompareFaceData(int frameIndex, FILE* fp) {
    ES_U32 width = m_compareFaceInputDataInfo.img_width;
    ES_U32 height = m_compareFaceInputDataInfo.img_height;

    CBatchMeta* batchMeta = new CBatchMeta;
    batchMeta->eosFlag = false;
    for (int batchIndex = 0; batchIndex < m_compareFaceInputDataInfo.batchSize; batchIndex++) {
        CFrameMeta* frameMeta = new CFrameMeta;
        for (int ppi = 0; ppi < 2; ppi++) {
            VIDEO_FRAME_INFO_S* videoFrameInfo = createVideoFrame(frameIndex, width, height, 30); // Add default fps value
            ES_U32 size = videoFrameInfo->videoFrame.stride[0] * height + videoFrameInfo->videoFrame.stride[1] * height / 2;

            ES_S32 ret = PL_ES_VB_GetBlock(m_compareFaceInputDataInfo.videoFramePoolId, size, "mmz_nid_0_part_0", &videoFrameInfo->videoFrame.fd);
            while (ret) {
                printf("%s get a block from poolid %d ret %d failed.", __FUNCTION__, m_compareFaceInputDataInfo.videoFramePoolId, ret);
                usleep(1000);
                ret = PL_ES_VB_GetBlock(m_compareFaceInputDataInfo.videoFramePoolId, size, "mmz_nid_0_part_0", &videoFrameInfo->videoFrame.fd);
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
            
            // The original selectedFaces logic was commented out or incomplete.
            // Replicating the structure but noting it's mostly placeholder.
            // objMeta->selectedFaces = new CFaceSelectMeta();
            
            std::vector<float> featureVect;
            for (int j = 0; j < m_compareFaceInputDataInfo.featureShape; j++) {
                featureVect.push_back(j + 0.1 * batchIndex + 0.01 * m);
            }
            
            // The original call was to transVectorToFackInferResult, but it was commented out.
            // We are not calling it here to maintain original behavior.
            // app_ret ret = transVectorToFackInferResult(objMeta->selectedFaces->faceFeatureFd, ...);
            // assert(ret == APP_SUCCESS);

            frameMeta->objs.push_back(objMeta);
        }
        batchMeta->addFrameMeta(frameMeta);
    }
    return batchMeta;
}

app_ret CompareFaceDataGenerator::Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) {
    m_loopNum = loopNum;
    auto compareConfig = config["compareFaceInput"];

    m_compareFaceInputDataInfo.batchSize = compareConfig["batch_size"].as<int>();
    m_compareFaceInputDataInfo.img_width = compareConfig["width"].as<int>();
    m_compareFaceInputDataInfo.img_height = compareConfig["height"].as<int>();
    m_compareFaceInputDataInfo.inputFileName = compareConfig["inputFileData"].as<string>();
    m_compareFaceInputDataInfo.featureShape = compareConfig["featureShape"].as<int>();
    m_compareFaceInputDataInfo.inferBatchSize = compareConfig["inferBatchSize"].as<int>();

    // Create video frame pool
    VB_POOL_CONFIG_S poolCfg = {0};
    poolCfg.blkCnt = 10;
    poolCfg.blkSize = m_compareFaceInputDataInfo.img_width * m_compareFaceInputDataInfo.img_height * 3 / 2;
    poolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
    memcpy(poolCfg.mmzName, vbName.c_str(), vbName.length());
    ES_S32 ret = PL_ES_VB_CreatePool(&poolCfg, &m_compareFaceInputDataInfo.videoFramePoolId);
    if (ret != ES_SUCCESS) return APP_FAILURE;

    // Create infer tensor pool
    int fackInferTensor_size = 1 * 1 * m_compareFaceInputDataInfo.inferBatchSize * m_compareFaceInputDataInfo.featureShape * 1 * 4;
    poolCfg = {0};
    poolCfg.blkCnt = 10 * 3;
    poolCfg.blkSize = fackInferTensor_size;
    poolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
    memcpy(poolCfg.mmzName, vbName.c_str(), vbName.length());
    ret = PL_ES_VB_CreatePool(&poolCfg, &m_compareFaceInputDataInfo.inferPoolId);
    if (ret != ES_SUCCESS) return APP_FAILURE;

    return APP_SUCCESS;
}

CBaseMeta* CompareFaceDataGenerator::GenerateData(int frameIndex, FILE*& fp) {
    CBatchMeta* batchMeta = nullptr;
    if (frameIndex == m_loopNum) {
        batchMeta = new CBatchMeta;
        batchMeta->eosFlag = true;
        for (int i = 0; i < m_compareFaceInputDataInfo.batchSize; i++) {
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
            fp = fopen(m_compareFaceInputDataInfo.inputFileName.c_str(), "r");
            if (fp == NULL) {
                app_error("Failed to open file: %s", m_compareFaceInputDataInfo.inputFileName.c_str());
                return nullptr;
            }
        }
        batchMeta = prepareCompareFaceData(frameIndex, fp);
    }
    return static_cast<CBaseMeta*>(batchMeta);
}
