
#define PL_LOG_ID PL_LOG_VDEC
#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

#define LOG_TAG "T_Comm_Dec"

#include <stdatomic.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "pl_comm_dec.h"

// #include "pl_kpi.h"
// #include "sink.h"
// #include "source_if.h"
// #include "demuxer_source_if.h"

#include "es_comm_video.h"
#include "es_type.h"
#include "es_vb_memory.h"
#include "es_vdec.h"
#include "log.h"

#define VB_MAX_COMM_POOLS 16

VB_POOL gPicVbPool[ES_VB_MAX_POOLS] = {[0 ...(ES_VB_MAX_POOLS - 1)] = ES_VB_INVALID_POOLID};
VB_POOL gTmvVbPool[ES_VB_MAX_POOLS] = {[0 ...(ES_VB_MAX_POOLS - 1)] = ES_VB_INVALID_POOLID};

static ES_U32 PL_GetBpp(PIXEL_FORMAT_E pixelFormat) {
    ES_U32 bpp = 0;

    switch (pixelFormat) {
        case PIXEL_FORMAT_A8:
        case PIXEL_FORMAT_R8:
        case PIXEL_FORMAT_YUV400:
            bpp = 8;
            break;
        case PIXEL_FORMAT_I420:
        case PIXEL_FORMAT_YV12:
        case PIXEL_FORMAT_NV12:
        case PIXEL_FORMAT_NV21:
            bpp = 12;
            break;
        case PIXEL_FORMAT_B8G8R8:
        case PIXEL_FORMAT_B8G8R8_PLANAR:
        case PIXEL_FORMAT_B8G8R8I:
        case PIXEL_FORMAT_B8G8R8I_PLANAR:
        case PIXEL_FORMAT_R8G8B8:
        case PIXEL_FORMAT_R8G8B8_PLANAR:
        case PIXEL_FORMAT_R8G8B8I:
        case PIXEL_FORMAT_R8G8B8I_PLANAR:
            bpp = 24;
            break;
        case PIXEL_FORMAT_A8R8G8B8:
        case PIXEL_FORMAT_R8G8B8A8:
        case PIXEL_FORMAT_B8G8R8A8:
            bpp = 32;
            break;
        default:
            break;
    }
    return bpp;
}

static ES_U64 PL_GetPicBufferSize(PIXEL_FORMAT_E pixelFormat, ES_U32 width, ES_U32 height, ES_U32 alignWidth) {
    ES_U32 stride, calculateH, pageSize;
    ES_U64 bufSize;
    ES_U32 alignHeight = 0;

    stride = ES_ALIGN_UP(width, alignWidth);
    calculateH = (alignHeight > 0) ? ES_ALIGN_UP(height, alignHeight) : height;
    bufSize = (ES_U64)stride * calculateH * PL_GetBpp(pixelFormat) / 8;
    pageSize = getpagesize();

    return ES_ALIGN_UP(bufSize, pageSize);
}

static ES_VOID getChnParams(const DEC_CHN_S *pChn, VDEC_GRP_PARAM_S *pGrpParam) {
    ES_VDEC_GetGrpParam(pChn->grpId, pGrpParam);
    if (PT_H264 == pChn->type || PT_H265 == pChn->type) {
        pGrpParam->vdecVideoParam.decMode = pChn->videoDecMode;
        pGrpParam->vdecVideoParam.outputOrder = pChn->outputOrder;
    }
    pGrpParam->displayFrameNum = pChn->displayFrameNum;
}

static ES_VOID getChnMode(VDEC_CHN vdChn, VDEC_CHN_MODE_S *pMode, const DEC_CHN_S *pChn) {
    ES_VDEC_GetChnMode(pChn->grpId, vdChn, pMode);
    pMode->cropParam.bEnable = pChn->cropParam[vdChn].bEnable;
    pMode->cropParam.rect.x = pChn->cropParam[vdChn].rect.x;
    pMode->cropParam.rect.y = pChn->cropParam[vdChn].rect.y;
    pMode->cropParam.rect.width = pChn->cropParam[vdChn].rect.width;
    pMode->cropParam.rect.height = pChn->cropParam[vdChn].rect.height;
    memcpy(&pMode->scale, &pChn->scaleParam[vdChn], sizeof(SCALE_S));
    pMode->pixelFormat = pChn->pixelFormat[vdChn];
    if (vdChn == 0) {
        pMode->alpha = pChn->alpha;
        pMode->colorGamut = pChn->colorGamut;
    }
}

static ES_VOID getChnAttrs(const DEC_CHN_S *pChn, VDEC_GRP_ATTR_S *pGrpAttr) {
    pGrpAttr->type = pChn->type;
    pGrpAttr->mode = pChn->videoMode;
    pGrpAttr->picWidth = pChn->width;
    pGrpAttr->picHeight = pChn->height;
    pGrpAttr->streamBufSize = pChn->minBufSize;
    pGrpAttr->frameBufCnt = pChn->frameBufCnt;
    pGrpAttr->align = pChn->align;

    ES_U64 blkSize = 0;
    for (ES_U32 i = 0; i < ES_VDEC_OUT_CHN_NUM; i++) {
        if (!pChn->outputChn[i]) {
            continue;
        }
        blkSize += PL_GetPicBufferSize(pChn->pixelFormat[i], pChn->width, pChn->height, pChn->align);
    }
    if (PT_H264 == pChn->type || PT_H265 == pChn->type) {
        pGrpAttr->vdecVideoAttr.refFrameNum = pChn->refFrameNum;
        pGrpAttr->frameBufSize = blkSize;
    } else if (PT_JPEG == pChn->type || PT_MJPEG == pChn->type) {
        pGrpAttr->mode = VIDEO_MODE_FRAME;
        pGrpAttr->frameBufSize = blkSize;
    }
    if (pChn->assignOutputBufSize > 0) {
        pGrpAttr->frameBufSize = pChn->assignOutputBufSize;
    }
}

ES_S32 COMM_VDEC_InitVBPool(const DEC_Client_S *pDecClient) {
    ES_U32 i = 0, j = 0;
    TEST_VDEC_BUF vdecBuf[MAX_CHN_NUM];
    VB_POOL_CONFIG_S vbPoolCfg;
    VB_CONFIG_S vbCfg;
    memset(vdecBuf, 0, sizeof(TEST_VDEC_BUF) * MAX_CHN_NUM);
    memset(&vbCfg, 0, sizeof(VB_CONFIG_S));
    for (i = 0; i < VB_MAX_COMM_POOLS; i++) {
        vbCfg.poolCfgs[i].enRemapMode = SYS_CACHE_MODE_NOCACHE;
    }
    // char tempCharDie0 [] = "mmz_nid_0_part_0";
    // memcpy(vbCfg.poolCfgs[0].mmzName, tempCharDie0, strlen(tempCharDie0));
    // char tempCharDie1 [] = "mmz_nid_1_part_0";
    // memcpy(vbCfg.poolCfgs[1].mmzName, tempCharDie1, strlen(tempCharDie1));

    // for (j = 0; j < VB_MAX_COMM_POOLS; j++) {
    //     for (i = pDecClient->startGrpId; i < pDecClient->startGrpId + pDecClient->groupNum; i++) {
    //         DEC_CHN_S *pChn = pDecClient->pMultiChn[i];
    //         if(0 == pChn->nDieID)
    //         {
    //             char tempChar [] = "mmz_nid_0_part_0";
    //             memcpy(vbCfg.poolCfgs[j].mmzName, tempChar, strlen(tempChar));
    //         }
    //         else
    //         {
    //             char tempChar [] = "mmz_nid_1_part_0";
    //             memcpy(vbCfg.poolCfgs[j].mmzName, tempChar, strlen(tempChar));
    //         }
    //     }
    // }

    for (i = pDecClient->startGrpId; i < pDecClient->startGrpId + pDecClient->groupNum; i++) {
        DEC_CHN_S *pChn = pDecClient->pMultiChn[i];
        for (ES_U32 j = 0; j < ES_VDEC_OUT_CHN_NUM; j++) {
            if (!pChn->outputChn[j]) {
                continue;
            }
            if (pChn->scaleParam[j].bEnable) {
                vdecBuf[i].picBufSize += PL_GetPicBufferSize(pChn->pixelFormat[j], pChn->scaleParam[j].scaleWidth,
                                                             pChn->scaleParam[j].scaleHeight, pChn->align);
            } else {
                vdecBuf[i].picBufSize +=
                    PL_GetPicBufferSize(pChn->pixelFormat[j], pChn->width, pChn->height, pChn->align);
            }
        }

        if (pDecClient->pMultiChn[i]->assignOutputBufSize > 0) {
            vdecBuf[i].picBufSize = pDecClient->pMultiChn[i]->assignOutputBufSize;
        }
    }

    /* PicBuffer */
    // for (j = 0; j < VB_MAX_COMM_POOLS; j++) {
    //     bFindFlag = ES_FALSE;
    //     for (i = pDecClient->startGrpId; i < pDecClient->startGrpId + pDecClient->groupNum; i++) {
    //         if (!bFindFlag && vdecBuf[i].picBufSize && !vdecBuf[i].bPicBufAlloc) {
    //             vbCfg.poolCfgs[j].blkSize = vdecBuf[i].picBufSize;
    //             vbCfg.poolCfgs[j].blkCnt = pDecClient->pMultiChn[i]->frameBufCnt;
    //             vdecBuf[i].bPicBufAlloc = ES_TRUE;
    //             bFindFlag = ES_TRUE;
    //             pos = j;
    //         }
    //         DEC_CHN_S *pChn = pDecClient->pMultiChn[i];

    //         if (bFindFlag && !vdecBuf[i].bPicBufAlloc && vbCfg.poolCfgs[j].blkSize == vdecBuf[i].picBufSize && (0 ==
    //         pChn->nDieID)) {
    //             vbCfg.poolCfgs[0].blkCnt += pDecClient->pMultiChn[i]->frameBufCnt;
    //             vdecBuf[i].bPicBufAlloc = ES_TRUE;
    //         }
    //         else if(bFindFlag && !vdecBuf[i].bPicBufAlloc && vbCfg.poolCfgs[j].blkSize == vdecBuf[i].picBufSize && (1
    //         == pChn->nDieID))
    //         {
    //             vbCfg.poolCfgs[1].blkCnt += pDecClient->pMultiChn[i]->frameBufCnt;
    //             vdecBuf[i].bPicBufAlloc = ES_TRUE;
    //         }
    //     }
    // }
    // vbCfg.poolCnt = pos + 1;

    for (i = pDecClient->startGrpId; i < pDecClient->startGrpId + pDecClient->groupNum; i++) {
        DEC_CHN_S *pChn = pDecClient->pMultiChn[i];
        ES_BOOL findFlag = ES_FALSE;
        ES_S32 poolNum = -1;
        for (j = 0; j < VB_MAX_COMM_POOLS; j++) {
            if (vbCfg.poolCfgs[j].blkSize == vdecBuf[i].picBufSize) {
                findFlag = ES_TRUE;
                break;
            }
        }

        if (findFlag) {
            for (j = 0; j < VB_MAX_COMM_POOLS; j++) {
                if (!strcmp(vbCfg.poolCfgs[j].mmzName, 0 == pChn->nDieID ? "mmz_nid_0_part_0" : "mmz_nid_1_part_0")) {
                    poolNum = j;
                    break;
                }
            }
            if (poolNum >= 0) {
                vbCfg.poolCfgs[poolNum].blkCnt += pChn->frameBufCnt;
            } else {
                vbCfg.poolCfgs[vbCfg.poolCnt].blkSize = vdecBuf[i].picBufSize;
                vbCfg.poolCfgs[vbCfg.poolCnt].blkCnt = pChn->frameBufCnt;
                strcpy(vbCfg.poolCfgs[vbCfg.poolCnt].mmzName,
                       0 == pChn->nDieID ? "mmz_nid_0_part_0" : "mmz_nid_1_part_0");
                vbCfg.poolCnt++;
            }
        } else {
            vbCfg.poolCfgs[vbCfg.poolCnt].blkSize = vdecBuf[i].picBufSize;
            vbCfg.poolCfgs[vbCfg.poolCnt].blkCnt = pChn->frameBufCnt;
            strcpy(vbCfg.poolCfgs[vbCfg.poolCnt].mmzName, 0 == pChn->nDieID ? "mmz_nid_0_part_0" : "mmz_nid_1_part_0");
            vbCfg.poolCnt++;
        }

        // if (findFlag && poolNum >= 0 && (!strcmp(vbCfg.poolCfgs[poolNum].mmzName, 0 ==
        // pChn->nDieID?"mmz_nid_0_part_0":"mmz_nid_1_part_0"))) {
        //     vbCfg.poolCfgs[poolNum].blkCnt += pChn->frameBufCnt;
        // } else {
        //     printf("the chnNUm is %d , the poolNum is %d ; the mmz is %d, %s, the pool mmz is %s\n", i, poolNum,
        //     pChn->nDieID, 0 == pChn->nDieID?"mmz_nid_0_part_0":"mmz_nid_1_part_0",
        //     (char*)vbCfg.poolCfgs[vbCfg.poolCnt].mmzName); vbCfg.poolCfgs[vbCfg.poolCnt].blkSize =
        //     vdecBuf[i].picBufSize; vbCfg.poolCfgs[vbCfg.poolCnt].blkCnt = pChn->frameBufCnt;
        //     strcpy(vbCfg.poolCfgs[vbCfg.poolCnt].mmzName, 0 == pChn->nDieID?"mmz_nid_0_part_0":"mmz_nid_1_part_0");
        //     vbCfg.poolCnt++;
        // }
    }

    if (VB_SOURCE_MODULE == pDecClient->modParam.vdecVBSource) {
        // ES_VB_ModPoolExit(VB_UID_VDEC);
        CHECK_RET(ES_VB_SetModPoolConfig(VB_UID_VDEC, &vbCfg), "ES_VB_SetModPoolConfig");
        ES_S32 ret = ES_VB_ModPoolInit(VB_UID_VDEC);
        if (ES_SUCCESS != ret) {
            app_error("ES_VB_ModPoolInit fail for 0x%x", ret);
            ES_VB_ModPoolExit(VB_UID_VDEC);
            return ES_FAILURE;
        }
    } else if (VB_SOURCE_USER == pDecClient->modParam.vdecVBSource) {
        for (i = pDecClient->startGrpId; i < pDecClient->startGrpId + pDecClient->groupNum; i++) {
            if (vdecBuf[i].picBufSize && pDecClient->pMultiChn[i]->frameBufCnt) {
                memset(&vbPoolCfg, 0, sizeof(VB_POOL_CONFIG_S));
                vbPoolCfg.blkSize = vdecBuf[i].picBufSize;
                vbPoolCfg.blkCnt = pDecClient->pMultiChn[i]->frameBufCnt;
                vbPoolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
                ES_VB_CreatePool(&vbPoolCfg, &gPicVbPool[i]);
                if (ES_VB_INVALID_POOLID == gPicVbPool[i]) {
                    goto fail;
                }
                app_debug("%s VB_SOURCE_USER create a pic vb pool[%u], size[%llu], cnt[%u]", __FUNCTION__,
                          gPicVbPool[i], vbPoolCfg.blkSize, vbPoolCfg.blkCnt);
            }
        }
    }
    return ES_SUCCESS;
fail:
    COMM_VDEC_ExitVBPool(pDecClient);
    return ES_FAILURE;
}

ES_VOID COMM_VDEC_ExitVBPool(const DEC_Client_S *pDecClient) {
    if (VB_SOURCE_MODULE == pDecClient->modParam.vdecVBSource) {
        ES_VB_ModPoolExit(VB_UID_VDEC);
    } else if (VB_SOURCE_USER == pDecClient->modParam.vdecVBSource) {
        for (ES_S32 i = ES_VB_MAX_POOLS - 1; i >= 0; i--) {
            if (ES_VB_INVALID_POOLID != gPicVbPool[i]) {
                if (ES_SUCCESS != ES_VB_DestroyPool(gPicVbPool[i])) {
                    app_error("ES_VB_DestroyPool %d fail!", gPicVbPool[i]);
                }
                gPicVbPool[i] = ES_VB_INVALID_POOLID;
            }
        }
    }
    for (ES_S32 i = pDecClient->startGrpId; i < pDecClient->startGrpId + pDecClient->groupNum; i++) {
        if (pDecClient->pMultiChn[i]->userPicPoolId != ES_VB_INVALID_POOLID) {
            ES_VB_DestroyPool(pDecClient->pMultiChn[i]->userPicPoolId);
        }
    }
}

ES_S32 COMM_VDEC_Start(const DEC_Client_S *pDecClient) {
    VDEC_GRP_POOL_S pool;
    VDEC_MOD_PARAM_S modParam = {0};
    ES_S32 ret = ES_SUCCESS;

    ret = ES_VDEC_Init();
    if (ES_SUCCESS != ret) {
        app_error("VDEC_Init faild with %#x! ", ret);
        return ES_FAILURE;
    }

    CHECK_RET(ES_VDEC_GetModParam(&modParam), "ES_VDEC_GetModParam");
    modParam.vdecVBSource = pDecClient->modParam.vdecVBSource;
    CHECK_RET(ES_VDEC_SetModParam(&modParam), "ES_VDEC_SetModParam");

    for (ES_S32 i = pDecClient->startGrpId; i < pDecClient->startGrpId + pDecClient->groupNum; i++) {
        DEC_CHN_S *pChn = pDecClient->pMultiChn[i];
        VDEC_GRP_ATTR_S attr = {0};
        getChnAttrs(pChn, &attr);
        int tmpGrpId = i + pDecClient->grpIdOffset;
        CHECK_CHN_RET(ES_VDEC_CreateGrp(tmpGrpId, pChn->nDieID, &attr), tmpGrpId, "ES_VDEC_CreateGrp");
        pChn->grpId += pDecClient->grpIdOffset;

        if (VB_SOURCE_USER == pDecClient->modParam.vdecVBSource) {
            pool.picVbPool = gPicVbPool[i];
            CHECK_CHN_RET(ES_VDEC_AttachVbPool(tmpGrpId, &pool), tmpGrpId, "ES_VDEC_AttachVbPool");
        }

        VDEC_GRP_PARAM_S chnParam = {0};
        getChnParams(pChn, &chnParam);
        CHECK_CHN_RET(ES_VDEC_SetGrpParam(tmpGrpId, &chnParam), tmpGrpId, "ES_VDEC_SetGrpParam");
        for (ES_S32 j = 0; j < ES_VDEC_OUT_CHN_NUM; j++) {
            if (pChn->outputChn[j]) {
                CHECK_CHN_RET(ES_VDEC_EnableChn(tmpGrpId, j), tmpGrpId, "ES_VDEC_EnableChn");
                VDEC_CHN_MODE_S mode = {0};
                getChnMode(j, &mode, pChn);
                CHECK_CHN_RET(ES_VDEC_SetChnMode(tmpGrpId, j, &mode), tmpGrpId, "ES_VDEC_SetChnMode");
            }
        }
        CHECK_CHN_RET(ES_VDEC_StartRecvStream(tmpGrpId), tmpGrpId, "ES_VDEC_StartRecvStream");
        app_debug("Start receive stream for vdec chn[%d].", pChn->grpId);
    }
    return ES_SUCCESS;
}

ES_VOID printfVdecGrpStatus(ES_S32 grpId, VDEC_GRP_STATUS_S status) { PRINTF_VDEC_GRP_STATUS(grpId, status); }

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
