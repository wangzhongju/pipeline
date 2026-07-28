#ifndef _ESSDK_PIPELINE_INFERENCE_META_H_
#define _ESSDK_PIPELINE_INFERENCE_META_H_

#include <vector>

#include "defines.h"
#include "error.h"
#include "es_comm_video.h"
#include "forward.h"
#include "pl_mem_wrap.h"
#include "pool.h"
#include "queue.h"

struct CDims {
    CDims() {
        n = 0;
        c = 0;
        h = 0;
        w = 0;
    }
    int n;
    int c;
    int h;
    int w;
};

struct ModelInfo {
    CDims dims;
    CDataType dataType;
    PIXEL_FORMAT_E pixFormat;
    CDataFormat dataFormat;
    int dataSize;
};

class CPreprocessMeta {
   public:
    CPreprocessMeta(){};
    ~CPreprocessMeta(){};

    virtual void release() {
        targetInferIds.clear();
        originObjMetas.clear();
        selectClassIds.clear();
        originFrameMetas.clear();
        aspectRatio.clear();
        aspectRatioExtX.clear();
        aspectRatioExtY.clear();
        aspectRatioExtW.clear();
        aspectRatioExtH.clear();

        ES_S32 ret = PL_ES_VB_ReleaseBlock(memFd);
        ES_ASSERT(ret == ES_SUCCESS, "preprocess release fd failed\n");

        if (pool) {
            pool->deallocate(this);
        } else {
            delete this;
        }
    }

    std::vector<int> targetInferIds;
    std::vector<int> selectClassIds;
    std::vector<CFrameMeta *> originFrameMetas;
    std::vector<float> aspectRatio;
    std::vector<float> aspectRatioExtX;
    std::vector<float> aspectRatioExtY;
    std::vector<float> aspectRatioExtW;
    std::vector<float> aspectRatioExtH;
    std::vector<CObjectMeta *> originObjMetas;
    ES_U64 memFd;
    PIXEL_FORMAT_E data_format;
    CDims dims;
    int data_size;
    CDataType dataType;
    CDataFormat dataOrder;
    MetaPool<CPreprocessMeta> *pool = nullptr;
};

class CInferOutputMeta {
   public:
    CInferOutputMeta() = default;
    ~CInferOutputMeta(){

    };

    void release() {
        for (int iSize = 0; iSize < memFd.size(); iSize++) {
            ES_S32 ret = PL_ES_VB_ReleaseBlock(memFd[iSize]);
            ES_ASSERT(ret == ES_SUCCESS, " infer meta release fd failed size %d iSize %d fd %lld\n", memFd.size(),
                      iSize, memFd[iSize]);
        }
        memFd.clear();
        onputDataInfo.clear();
        blkSize.clear();
        onputDataInfo.clear();
        parentObjMeta.clear();
        parentFrameMeta.clear();
        aspectRatio.clear();
        aspectRatioExtX.clear();
        aspectRatioExtY.clear();
        aspectRatioExtW.clear();
        aspectRatioExtH.clear();

        if (pool) {
            pool->deallocate(this);
        } else {
            delete this;
        }
    };

   public:
    uint uniqueID;              // 本插件的ID号;
    std::vector<ES_U64> memFd;  // 推理输出的结果数据；
    ES_VOID *pVirAddr;
    std::vector<ES_U64> blkSize;
    std::vector<ModelInfo> onputDataInfo;  // 推理输出数据的信息；
    std::vector<CObjectMeta *> parentObjMeta;
    std::vector<CFrameMeta *> parentFrameMeta;
    std::vector<float> aspectRatio;
    std::vector<float> aspectRatioExtX;
    std::vector<float> aspectRatioExtY;
    std::vector<float> aspectRatioExtW;
    std::vector<float> aspectRatioExtH;

    MetaPool<CInferOutputMeta> *pool;
};

#endif