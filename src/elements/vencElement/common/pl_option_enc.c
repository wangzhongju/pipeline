#define PL_LOG_ID PL_LOG_VENC

#include "pl_option_enc.h"

#include <stdint.h>

#include "log.h"
#define GOP_MIN_VALUE (1)
#define GOP_MAX_VALUE (65536)

PIXEL_FORMAT_E convertPixelFmt(const ES_CHAR *pValue) {
    if (0 == strcmp(pValue, "yuv420p")) {
        return PIXEL_FORMAT_I420;
    } else if (0 == strcmp(pValue, "yvu420p")) {
        return PIXEL_FORMAT_YV12;
    } else if (0 == strcmp(pValue, "yuv420sp")) {
        return PIXEL_FORMAT_NV12;
    } else if (0 == strcmp(pValue, "yvu420sp")) {
        return PIXEL_FORMAT_NV21;
    } else if (0 == strcmp(pValue, "yuyv")) {
        return PIXEL_FORMAT_YUY2;
    } else if (0 == strcmp(pValue, "uyvy")) {
        return PIXEL_FORMAT_UYVY;
    } else if (0 == strcmp(pValue, "i010")) {
        return PIXEL_FORMAT_YUV420P010BE;
    } else if (0 == strcmp(pValue, "p010")) {
        return PIXEL_FORMAT_YUV420P010LE;
    } else {
        return PIXEL_FORMAT_BUTT;
    }
}

ES_VOID setDefaultParams(TEST_CHN_S_ENC *pChnParams) {
    if (pChnParams) {
        /* parser related*/
        pChnParams->multiple = 0;
        for (ES_S32 i = 0; i < MAX_CHN_NUM; i++) {
            pChnParams->pStreamcfg[i] = ES_NULL;
        }

        pChnParams->grpId = 0;
        // pParam->kpiHandle = TEST_KPI_INVALID_HANLE;
        pChnParams->sinkType = 0;
        pChnParams->srcCircleNum = 1;
        pChnParams->srcMaxBufNum = 5;
        pChnParams->srcWaterLevel = 5;
        pChnParams->srcSendRate = -1;
        pChnParams->enableFd = ES_TRUE;
        pChnParams->type = PT_BUTT;
        pChnParams->width = 0;
        pChnParams->height = 0;
        memset(pChnParams->inputFile, 0, MAX_FILE_NAME_LEN);
        memset(pChnParams->outputFile, 0, MAX_FILE_NAME_LEN);
        pChnParams->pixelFormat = PIXEL_FORMAT_BUTT;
        pChnParams->effectNumber = -1;
        pChnParams->lastPic = UINT32_MAX;
        pChnParams->firstPic = 0;

        /* for venc */
        pChnParams->priority = 0;
        pChnParams->pollWakeUpFrmCnt = 1;
        pChnParams->constChroma.bEnableConstChroma = ES_FALSE;
        pChnParams->constChroma.cbValue = UINT32_MAX;
        pChnParams->constChroma.crValue = UINT32_MAX;
        pChnParams->rotation = ROTATION_0;
        pChnParams->oneStreamBuffer = 1;  // stream in one packet
        pChnParams->vbSource = DEFAULT;
        pChnParams->frameRate.srcFrmRate = -1;
        pChnParams->frameRate.dstFrmRate = -1;
        pChnParams->rcAttr.rcMode = VENC_RC_MODE_BUTT;
        pChnParams->GOPAttr.GOPMode = VENC_GOPMODE_BUTT;
        for (size_t i = 0; i < ES_VENC_MAX_ROI_NUM; i++) {
            pChnParams->ROIParam.bROISet[i] = ES_FALSE;
            memset(&pChnParams->ROIParam.ROIAttr[i], 0, sizeof(VENC_ROI_ATTR_S));
            pChnParams->ROIParam.ROIAttr[i].index = i;
        }
        pChnParams->IDRInfo.instant = -1;
        for (ES_S32 i = 0; i < MAX_SEI_NUM; i++) {
            memset(&pChnParams->encSEI.SEIData[i], 0, sizeof(TEST_ENC_SEI_S));
        }
        pChnParams->protocol.sliceSize = 0;
        pChnParams->protocol.VUIInfo.timingInfoFlag = 0;
        pChnParams->protocol.VUIInfo.sarWidth = 1;
        pChnParams->protocol.VUIInfo.sarHeight = 1;
        pChnParams->protocol.VUIInfo.videoRange = 1;
        pChnParams->protocol.VUIInfo.videoFormat = 5;
        pChnParams->protocol.VUIInfo.videoSingalFlag = 1;
        pChnParams->protocol.transInfo.enableScalingList = 0;
        pChnParams->protocol.transInfo.qpOffset = 0;
        pChnParams->protocol.SAOEnabledFlag = 1;
        pChnParams->protocol.entropyInfo.enableCabac = 0;  // CABAC as default,h264,base not support CABAC
        pChnParams->protocol.smoothingIntra = 1;
        pChnParams->protocol.dblkInfo.disableDeblockingFlag = 0;
        pChnParams->protocol.dblkInfo.tcOffset = 0;
        pChnParams->protocol.dblkInfo.betaOffset = 0;
        pChnParams->protocol.intraRefresh.bRefreshEnable = ES_FALSE;
        pChnParams->protocol.intraRefresh.intraRefreshMode = INTRA_REFRESH_ROW;
        pChnParams->protocol.intraRefresh.refreshNum = 0;
        pChnParams->protocol.intraRefresh.reqIQP = 51;
        pChnParams->JPEGParam.qFactor = 0;
        pChnParams->JPEGParam.enableQt = ES_FALSE;
        pChnParams->JPEGParam.MCUPerECS = 0;
        pChnParams->align = 0;
        pChnParams->bByFrame = ES_TRUE;
        for (size_t i = 0; i < ES_VENC_MAX_SSE_NUM; i++) {
            pChnParams->SSEParam.bSSESet[i] = ES_FALSE;
            memset(&pChnParams->SSEParam.SSECfg[i], 0, sizeof(VENC_SSE_CFG_S));
            pChnParams->SSEParam.SSECfg[i].index = i;
        }

        pChnParams->profile = -1;
        pChnParams->bCircleSend = ES_FALSE;
        pChnParams->colorGamut = COLOR_GAMUT_BT709;
        pChnParams->cropParam.bEnable = ES_FALSE;
        pChnParams->cropParam.rect.x = 0;
        pChnParams->cropParam.rect.y = 0;
        pChnParams->cropParam.rect.width = 0;
        pChnParams->cropParam.rect.height = 0;

        pChnParams->qpMapFlag = 1;  // skip
        pChnParams->qpMapBlockUnit = -1;
        memset(pChnParams->qpMapFile, 0, MAX_FILE_NAME_LEN);
        memset(pChnParams->ipcmMapFile, 0, MAX_FILE_NAME_LEN);
        pChnParams->skipMapBlockUnit = -1;
        memset(pChnParams->skipMapFile, 0, MAX_FILE_NAME_LEN);

        pChnParams->ctbRc = DEFAULT;
        pChnParams->blockRCSize = DEFAULT;
        pChnParams->rcQpDeltaRange = DEFAULT;
        pChnParams->rcBaseMBComplexity = DEFAULT;
        pChnParams->tolCtbRcInter = DEFAULT;
        pChnParams->tolCtbRcIntra = DEFAULT;
        pChnParams->ctbRcRowQpStep = DEFAULT;
    }
}

static ES_VOID setDefaultEncodeParameter(TEST_CHN_S_ENC *pChnParams) {
    if (pChnParams->pixelFormat == PIXEL_FORMAT_BUTT) {
        pChnParams->pixelFormat = PIXEL_FORMAT_NV21;
    }
    /* rate control */
    if (pChnParams->rcAttr.rcMode == VENC_RC_MODE_BUTT && pChnParams->type == PT_H264) {
        pChnParams->rcAttr.rcMode = VENC_RC_MODE_H264CBR;
        pChnParams->rcAttr.h264CBR.GOP = 10;
    } else if (pChnParams->rcAttr.rcMode == VENC_RC_MODE_BUTT && pChnParams->type == PT_H265) {
        pChnParams->rcAttr.rcMode = VENC_RC_MODE_H265CBR;
        pChnParams->rcAttr.h265CBR.GOP = 10;
    } else if (pChnParams->rcAttr.rcMode == VENC_RC_MODE_BUTT && pChnParams->type == PT_MJPEG) {
        pChnParams->rcAttr.rcMode = VENC_RC_MODE_MJPEGCBR;
    }

    /* GOP: group of pic */
    if (pChnParams->GOPAttr.GOPMode == VENC_GOPMODE_BUTT) {
        pChnParams->GOPAttr.GOPMode = VENC_GOPMODE_NORMALP;
        pChnParams->GOPAttr.normalP.IPQpDelta = 3;
    }
}

static ES_VOID setDefaultCommonParameter(TEST_CHN_S_ENC *pChnParams) {
    if (!pChnParams->multiple) {
        pChnParams->multiple = 1;
    }
}

static ES_VOID setDefaultCodecParameter(TEST_CHN_S_ENC *pChnParams, TEST_CODEC_TYPE_E_ENC codecType) {
    if (codecType == TEST_CODEC_TYPE_ENCODE) {
        setDefaultEncodeParameter(pChnParams);
    }
    setDefaultCommonParameter(pChnParams);
}

static ES_BOOL validateAndCorrectCommand(TEST_CHN_S_ENC *pChnParams, TEST_CODEC_TYPE_E_ENC codecType) {
    if (pChnParams->type == PT_BUTT) {
        app_error("%s \n", "'type' not set by user.");
        return ES_FALSE;
    }

    if (!pChnParams->width || !pChnParams->height) {
        app_debug("Illegal size, w:%d h:%d. \n", pChnParams->width, pChnParams->height);
    }

    if (codecType == TEST_CODEC_TYPE_ENCODE) {
        if (-1 == pChnParams->profile) {
            pChnParams->profile = 0;
            app_debug("%s %d.\n", "Correct profile to ", pChnParams->profile);
        }
        app_debug("%s %d.\n", "'profile' is  ", pChnParams->profile);
    }

    if (pChnParams->multiple > MAX_CHN_NUM) {
        app_error("%s %u.\n", "'multiple' is ", pChnParams->multiple);
        return ES_FALSE;
    }
    if (!pChnParams->cropParam.bEnable) {
        pChnParams->cropParam.rect.width = pChnParams->width;
        pChnParams->cropParam.rect.height = pChnParams->height;
    }

    setDefaultCodecParameter(pChnParams, codecType);

    app_debug("%s %s.\n", "'input' is ", pChnParams->inputFile);
    app_debug("%s %d.\n", "'type' is ", pChnParams->type);
    app_debug("%s %ux%u.\n", "'size' is ", pChnParams->width, pChnParams->height);
    app_debug("%s %d.\n", "'pixelFmt' is ", pChnParams->pixelFormat);
    app_debug("%s %d\n", "'gop_mode' is ", pChnParams->GOPAttr.GOPMode);
    app_debug("%s %d\n", "'rc_mode' is ", pChnParams->rcAttr.rcMode);
    app_debug("%s %d.\n", "'requestIDR' is ", pChnParams->IDRInfo.instant);
    app_debug("%s %s.\n", "'output' is ", pChnParams->outputFile);
    app_debug("%s %u.\n", "'multiple' is ", pChnParams->multiple);
    return ES_TRUE;
}

ES_S32 validateAndCreateChns(TEST_CHN_S_ENC *pChnParams, TEST_Client_S_ENC *pEncClient) {
    ES_S32 ret = ES_SUCCESS;
    TEST_CODEC_TYPE_E_ENC codecType = TEST_CODEC_TYPE_ENCODE;
    // Guess codec type.
    // guessCodecTypeByUrl(pChnParams->inputFile, &codecType);
    if (!validateAndCorrectCommand(pChnParams, codecType)) {
        ASSERT(0);
    }

    // // Create client channel
    TEST_Client_S_ENC *pProxyClient = pEncClient;
    ES_S32 offset = pChnParams->grpId + pProxyClient->groupNum;
    for (ES_S32 i = 0; i < pChnParams->multiple; i++) {
        app_debug("######## offset + i :%d #########", offset + i);
        pProxyClient->pMultiChn[offset + i] = (TEST_CHN_S_ENC *)calloc(1, sizeof(TEST_CHN_S_ENC));
        if (!pProxyClient->pMultiChn[offset + i]) {
            app_error("%s \n", "Calloc TEST_CHN_S failed.");

            ret = ES_FAILURE;
            break;
        }
        memcpy(pProxyClient->pMultiChn[offset + i], pChnParams, sizeof(TEST_CHN_S_ENC));
        pProxyClient->pMultiChn[offset + i]->grpId = offset + i;
    }
    pProxyClient->groupNum += pChnParams->multiple;
    pProxyClient->startGrpId = (pChnParams->grpId > 0) ? pChnParams->grpId : 0;
    app_debug("Create Chns finish. groupNum = %d, startGrpId = %d \n ", pProxyClient->groupNum,
              pProxyClient->startGrpId);
    return ret;
}

ES_VOID deinitOptions(TEST_Client_S_ENC *pEncClient, ES_S32 chnId) {
    if (pEncClient->pMultiChn[chnId]) {
        free(pEncClient->pMultiChn[chnId]);
        pEncClient->pMultiChn[chnId] = ES_NULL;
    }
}
