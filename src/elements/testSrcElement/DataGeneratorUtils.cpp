#define PL_LOG_ID PL_LOG_TESTSRC
#include "DataGeneratorUtils.h"

#include <unistd.h>

#include "es_sys.h"
#include "es_vb_memory.h"

// Implementation of helper functions moved from testSrcElement.cpp

static app_ret videoGridMeta2VoInfo(CVideoGridMeta *pVideogridMeta, VIDEO_FRAME_INFO_S *pFrameInfo) {
    if (NULL == pVideogridMeta || NULL == pFrameInfo) {
        return APP_FAILURE;
    }
    memset(pFrameInfo, 0, sizeof(VIDEO_FRAME_INFO_S));
    pFrameInfo->videoFrame.fd = pVideogridMeta->memFd;
    // pFrameInfo->poolId = ES_VB_Handle2PoolId(pVideogridMeta->memFd);

    pFrameInfo->videoFrame.pixelFormat = pVideogridMeta->data_format;
    pFrameInfo->videoFrame.field = VIDEO_FIELD_FRAME;
    pFrameInfo->videoFrame.dynamicRange = DYNAMIC_RANGE_NONE;
    pFrameInfo->videoFrame.colorGamut = COLOR_GAMUT_BT709;

    pFrameInfo->videoFrame.width = pVideogridMeta->width;
    pFrameInfo->videoFrame.height = pVideogridMeta->height;
    if (pFrameInfo->videoFrame.pixelFormat == PIXEL_FORMAT_NV12) {
        pFrameInfo->videoFrame.stride[0] = pVideogridMeta->stride[0];
        pFrameInfo->videoFrame.stride[1] = pVideogridMeta->stride[1];

        pFrameInfo->videoFrame.offset[0] = 0;
        pFrameInfo->videoFrame.offset[1] = pFrameInfo->videoFrame.stride[0] * pFrameInfo->videoFrame.height;
    } else {
        app_error("only surport nv12");
    }
    return APP_SUCCESS;
}

VIDEO_FRAME_INFO_S *createVideoFrame(ES_U32 count, ES_U32 width = 1280, ES_U32 height = 720, ES_U32 fps = 25) {
    PIXEL_FORMAT_E srcParam_pixelFormat = PIXEL_FORMAT_NV12;

    VIDEO_FRAME_INFO_S *videoFrameInfo = (VIDEO_FRAME_INFO_S *)malloc(sizeof(VIDEO_FRAME_INFO_S));
    memset(videoFrameInfo, 0, sizeof(VIDEO_FRAME_INFO_S));

    videoFrameInfo->videoFrame.fd = 0;
    videoFrameInfo->poolId = 0;

    videoFrameInfo->videoFrame.stride[0] = width;
    videoFrameInfo->videoFrame.stride[1] = width;

    videoFrameInfo->videoFrame.width = width;
    videoFrameInfo->videoFrame.height = height;

    ES_U32 lStride = width;
    videoFrameInfo->videoFrame.offset[0] = 0;
    videoFrameInfo->videoFrame.stride[0] = lStride;

    ES_U32 lumaSize = lStride * height;
    ES_U32 cStride = lStride;
    ES_U32 chrmSize = (cStride * height) >> 2;
    videoFrameInfo->videoFrame.offset[1] = lumaSize;
    videoFrameInfo->videoFrame.offset[2] = lumaSize + chrmSize;
    videoFrameInfo->videoFrame.stride[1] = cStride;
    videoFrameInfo->videoFrame.stride[2] = cStride;

    videoFrameInfo->videoFrame.pixelFormat = srcParam_pixelFormat;
    videoFrameInfo->videoFrame.field = VIDEO_FIELD_FRAME;

    videoFrameInfo->videoFrame.dynamicRange = DYNAMIC_RANGE_NONE;
    videoFrameInfo->videoFrame.colorGamut = COLOR_GAMUT_BT709;

    return videoFrameInfo;
}

CBaseMeta *prepareEncodeInputData(EncodeInputDataInfo &encodeInputDataInfo, int fps, int frameIndex, FILE *fp) {
    ES_U32 width = encodeInputDataInfo.width;
    ES_U32 height = encodeInputDataInfo.height;

    VIDEO_FRAME_INFO_S *videoFrameInfo = createVideoFrame(frameIndex, width, height);  // count -> pts
    ES_U32 size = videoFrameInfo->videoFrame.stride[0] * height + videoFrameInfo->videoFrame.stride[1] * height / 2;
    ES_S32 ret = PL_ES_VB_GetBlock(encodeInputDataInfo.pool, size, "mmz_nid_0_part_0", &videoFrameInfo->videoFrame.fd,
                                   "testsrc");
    while (ret || videoFrameInfo->poolId < 0) {
        printf("%s get a block from poolid %d  ret %d failed. \n", __FUNCTION__, encodeInputDataInfo.pool, ret);
        ret = PL_ES_VB_GetBlock(encodeInputDataInfo.pool, size, "mmz_nid_0_part_0", &videoFrameInfo->videoFrame.fd);
        usleep(1000000);
    }

    ES_U64 fd = videoFrameInfo->videoFrame.fd;
    ES_U64 *pVirAddr = ES_NULL;
    pVirAddr = (ES_U64 *)ES_SYS_Mmap(fd, size, SYS_CACHE_MODE_NOCACHE);
    if (NULL == pVirAddr) {
        free(videoFrameInfo);
        return ES_NULL;
    }

    int tempCount = fread(pVirAddr, 1, size, fp);
    // If we didn't read the full frame and loopFile is enabled, rewind and try again
    if (size != tempCount) {
        app_debug("%s \n", "Reached end of file, rewinding to beginning");
        fseek(fp, 0, SEEK_SET);
        tempCount = fread(pVirAddr, 1, size, fp);
        if (size != tempCount) {
            app_debug("%s \n", "File is smaller than one frame, padding with zeros");
            // If file is smaller than one frame, pad with zeros
            memset((char *)pVirAddr + tempCount, 0, size - tempCount);
        }
    }

    ret = ES_SYS_Munmap(pVirAddr, size);
    if (ret != ES_SUCCESS) {
        free(videoFrameInfo);
        return ES_NULL;
    }

    CFrameMeta *frameMeta = nullptr;
    CBatchMeta *batchMeta = nullptr;

    CImage *cimage = new CImage;
    cimage->mPic = videoFrameInfo;
    if (encodeInputDataInfo.dateType == 0) {
        frameMeta = new CFrameMeta;
        frameMeta->images.push_back(cimage);
        frameMeta->mMetaType = FRAME_META;
        frameMeta->index = frameIndex;
        return (CBaseMeta *)frameMeta;
    } else if (encodeInputDataInfo.dateType == 1) {  // pp1 mode
        frameMeta = new CFrameMeta;
        CImage *cimage1 = new CImage;
        cimage1->mPic = NULL;
        frameMeta->images.push_back(cimage1);
        frameMeta->images.push_back(cimage);
        frameMeta->mMetaType = FRAME_META;
        frameMeta->index = frameIndex;
        return (CBaseMeta *)frameMeta;
    } else {
        batchMeta = new CBatchMeta;
        batchMeta->mMetaType = BATCH_META;
        CVideoGridMeta *videoGrid = new CVideoGridMeta();
        videoGrid->memFd = videoFrameInfo->videoFrame.fd;
        videoGrid->width = videoFrameInfo->videoFrame.width;
        videoGrid->height = videoFrameInfo->videoFrame.height;
        videoGrid->data_format = videoFrameInfo->videoFrame.pixelFormat;
        videoGrid->stride[0] = videoFrameInfo->videoFrame.stride[0];
        videoGrid->stride[1] = videoFrameInfo->videoFrame.stride[1];
        videoGrid->stride[2] = videoFrameInfo->videoFrame.stride[2];
        videoGrid->gridPic = new VIDEO_FRAME_INFO_S;
        videoGridMeta2VoInfo(videoGrid, videoGrid->gridPic);
        videoGrid->gridPic->poolId = encodeInputDataInfo.pool;
        videoGrid->gridPic->modId = ES_ID_VB;
        batchMeta->videoGrid = videoGrid;
        return (CBaseMeta *)batchMeta;
    }
}