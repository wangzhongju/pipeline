#define PL_LOG_ID PL_LOG_VENC

#include "pl_encwrapper.h"

#include <stdint.h>

#include "es_venc.h"
#include "log.h"

#define MAX_GOPCFG_ROW_LEN 128
#define MAX_GOPCFG_ROW_CNT 16
// static ES_CHAR *gpGopCfgStr[MAX_GOPCFG_ROW_CNT];

typedef enum esPIC_SIZE_E {
    PIC_CIF,
    PIC_144x128,
    PIC_176x220,
    PIC_256x144,
    PIC_320x240,
    PIC_480x360,
    PIC_360P,    /* 640 * 360 */
    PIC_D1_PAL,  /* 720 * 576 */
    PIC_D1_NTSC, /* 720 * 480 */
    PIC_720P,    /* 1280 * 720  */
    PIC_1080P,   /* 1920 * 1080 */
    PIC_2592x1520,
    PIC_2592x1944,
    PIC_3840x2160,
    PIC_4096x2160,
    PIC_3000x3000,
    PIC_4000x3000,
    PIC_7680x4320,
    PIC_3840x8640,
    PIC_8192x4320,
    PIC_BUTT
} PIC_SIZE_E;

/****************************** private ******************************/
static PIC_SIZE_E getDisplayPicSize(ES_U32 width, ES_U32 height) {
    struct member {
        ES_U32 w;
        ES_U32 h;
        PIC_SIZE_E dispPicSize;
    };

    static const struct member resolutions[] = {
        // map with getBitRate
        {7680, 4320, PIC_7680x4320}, {4000, 3000, PIC_4000x3000}, {3840, 2160, PIC_3840x2160},
        {2592, 1944, PIC_2592x1944}, {1920, 1080, PIC_1080P},     {1280, 720, PIC_720P},
        {720, 576, PIC_D1_PAL},      {720, 486, PIC_D1_PAL},      {640, 360, PIC_360P},
        {480, 360, PIC_480x360},     {320, 240, PIC_320x240},     {256, 144, PIC_256x144},
        {176, 220, PIC_176x220},     {144, 128, PIC_144x128},
    };
    size_t count = sizeof(resolutions) / sizeof(resolutions[0]);
    for (size_t i = 0; i < count; ++i) {
        if (width == resolutions[i].w && height == resolutions[i].h) {
            return resolutions[i].dispPicSize;
        }
    }
    return PIC_BUTT;
}

static ES_U32 getBitRate(VENC_RC_MODE_E rcMode, PIC_SIZE_E size, ES_U32 frameRate) {
    ES_U32 bitRate = 0;

    if (VENC_RC_MODE_H264CBR == rcMode || VENC_RC_MODE_H265CBR == rcMode) {
        switch (size) {
            case PIC_144x128:
            case PIC_176x220:
            case PIC_256x144:
            case PIC_480x360:
            case PIC_360P:
                bitRate = 1024 * 1 + 1024 * frameRate / 30;
                break;
            case PIC_D1_PAL:
            case PIC_720P:
            case PIC_1080P:
                bitRate = 1024 * 2 + 1024 * frameRate / 30;
                break;
            case PIC_2592x1944:
                bitRate = 1024 * 3 + 3072 * frameRate / 30;
                break;
            case PIC_3840x2160:
                bitRate = 1024 * 5 + 5120 * frameRate / 30;
                break;
            case PIC_4000x3000:
                bitRate = 1024 * 12 + 5120 * frameRate / 30;
                break;
            case PIC_7680x4320:
                bitRate = 1024 * 10 + 5120 * frameRate / 30;
                break;
            default:
                bitRate = 1024 * 15 + 2048 * frameRate / 30;
                break;
        }
    } else if ((VENC_RC_MODE_H264VBR == rcMode || VENC_RC_MODE_H265VBR == rcMode) ||
               (VENC_RC_MODE_H264AVBR == rcMode || VENC_RC_MODE_H265AVBR == rcMode) ||
               (VENC_RC_MODE_H264CVBR == rcMode || VENC_RC_MODE_H265CVBR == rcMode)) {
        switch (size) {
            case PIC_144x128:
            case PIC_256x144:
                bitRate = 1024 * 1 + 1024 * frameRate / 30;
                break;
            case PIC_D1_PAL:
            case PIC_720P:
                bitRate = 1024 * 2 + 1024 * frameRate / 30;
                break;
            case PIC_1080P:
                bitRate = 1024 * 2 + 2048 * frameRate / 30;
                break;
            case PIC_2592x1944:
                bitRate = 1024 * 3 + 3072 * frameRate / 30;
                break;
            case PIC_3840x2160:
                bitRate = 1024 * 5 + 5120 * frameRate / 30;
                break;
            case PIC_4000x3000:
                bitRate = 1024 * 10 + 5120 * frameRate / 30;
                break;
            case PIC_7680x4320:
                bitRate = 1024 * 20 + 5120 * frameRate / 30;
                break;
            default:
                bitRate = 1024 * 15 + 2048 * frameRate / 30;
                break;
        }
    } else if (VENC_RC_MODE_MJPEGCBR == rcMode) {
        switch (size) {
            case PIC_144x128:
            case PIC_256x144:
            case PIC_320x240:
                bitRate = 1024 * 1 + 1024 * frameRate / 30;
                break;
            case PIC_360P:
                bitRate = 1024 * 3 + 1024 * frameRate / 30;
                break;
            case PIC_D1_PAL:
                bitRate = 1024 * 4 + 1024 * frameRate / 30;
                break;
            case PIC_720P:
                bitRate = 1024 * 5 + 1024 * frameRate / 30;
                break;
            case PIC_1080P:
                bitRate = 1024 * 8 + 2048 * frameRate / 30;  //
                break;
            case PIC_2592x1944:
                bitRate = 1024 * 20 + 3072 * frameRate / 30;  //
                break;
            case PIC_3840x2160:
                bitRate = 1024 * 25 + 5120 * frameRate / 30;  //
                break;
            case PIC_4000x3000:
                bitRate = 1024 * 30 + 5120 * frameRate / 30;  //
                break;
            case PIC_7680x4320:
                bitRate = 1024 * 40 + 5120 * frameRate / 30;  //
                break;
            default:
                bitRate = 1024 * 20 + 2048 * frameRate / 30;  //
                break;
        }
    }

    return bitRate;
}

ES_VOID getChnAttrs(const TEST_CHN_S_ENC *pChnInfo, VENC_CHN_ATTR_S *pVencChnAttr) {
    ES_U32 frameRate = pChnInfo->frameRate.dstFrmRate > 0 ? pChnInfo->frameRate.dstFrmRate : 30;
    frameRate = pChnInfo->dstFrameRate > 0 ? pChnInfo->dstFrameRate : 30;
    ES_U32 statTime;
    // ES_U32 GOP = (ES_U32) * (&(pChnInfo->rcAttr.rcMode) + 1);
    PIC_SIZE_E dispPicSize = getDisplayPicSize(pChnInfo->width, pChnInfo->height);
    const VENC_GOP_ATTR_S *pGOPAttr = &pChnInfo->GOPAttr;

    pVencChnAttr->vencAttr.type = pChnInfo->type;
    pVencChnAttr->vencAttr.maxPicWidth = pChnInfo->width;
    pVencChnAttr->vencAttr.maxPicHeight = pChnInfo->height;
    pVencChnAttr->vencAttr.picWidth = pChnInfo->width;
    pVencChnAttr->vencAttr.picHeight = pChnInfo->height;
    pVencChnAttr->vencAttr.bufSize =
        COMMON_GetPicBufferSize(pChnInfo->pixelFormat, pChnInfo->width, pChnInfo->height,
                                pChnInfo->align ? pChnInfo->align : 1, 1);  // hisi remommended value w*h*1.5

    pVencChnAttr->vencAttr.profile = pChnInfo->profile;
    pVencChnAttr->vencAttr.bByFrame = pChnInfo->bByFrame;

    statTime = 1;

    if (VENC_RC_MODE_BUTT == pChnInfo->rcAttr.rcMode) {
        if (PT_H264 == pChnInfo->type) {
            pVencChnAttr->rcAttr.rcMode = VENC_RC_MODE_H264CBR;
        } else if (PT_H265 == pChnInfo->type) {
            pVencChnAttr->rcAttr.rcMode = VENC_RC_MODE_H265CBR;
        } else if (PT_H264 == pChnInfo->type) {
            pVencChnAttr->rcAttr.rcMode = VENC_RC_MODE_MJPEGCBR;
        }
    } else {
        pVencChnAttr->rcAttr.rcMode = pChnInfo->rcAttr.rcMode;
    }

    app_debug("type = %d, rcMode:%d, picWidth = %d, picHeight = %d \n", pVencChnAttr->vencAttr.type,
              pVencChnAttr->rcAttr.rcMode, pVencChnAttr->vencAttr.picWidth, pVencChnAttr->vencAttr.picHeight);

    switch (pChnInfo->type) {
        case PT_H264: {
            if (VENC_RC_MODE_H264CBR == pChnInfo->rcAttr.rcMode) {
                VENC_H264_CBR_S *pH264Cbr = &pVencChnAttr->rcAttr.h264CBR;
                pH264Cbr->GOP = pChnInfo->rcAttr.h264CBR.GOP;
                pH264Cbr->statTime = statTime;
                // case PIC_1080P:
                // bitRate = 1024 * 2 + 1024 * frameRate / 30;frameRate=30=>3072 *1.25=
                //     *pCpbSize = p->cpbSizeRatio * p->bitRate * 1000; -> *pCpbSize = p->cpbSize * 1000;
                pH264Cbr->dstFrameRate = frameRate;
                pH264Cbr->bitRate = pChnInfo->bitrate > 0 ? pChnInfo->bitrate
                                                          : getBitRate(pChnInfo->rcAttr.rcMode, dispPicSize, frameRate);
                pH264Cbr->cpbSize = pH264Cbr->bitRate * 1.25;
            } else if (VENC_RC_MODE_H264FIXQP == pChnInfo->rcAttr.rcMode) {
                VENC_H264_FIXQP_S *pH264FixQP = &pVencChnAttr->rcAttr.h264FixQP;
                pH264FixQP->GOP = pChnInfo->rcAttr.h264FixQP.GOP;
                pH264FixQP->dstFrameRate = frameRate;
                pH264FixQP->IQP = 25;
                pH264FixQP->PQP = 30;
                pH264FixQP->BQP = 32;
            } else if (VENC_RC_MODE_H264VBR == pChnInfo->rcAttr.rcMode) {
                VENC_H264_VBR_S *pH264Vbr = &pVencChnAttr->rcAttr.h264VBR;
                pH264Vbr->GOP = pChnInfo->rcAttr.h264VBR.GOP;
                pH264Vbr->statTime = statTime;
                pH264Vbr->dstFrameRate = frameRate;
                pH264Vbr->maxBitRate = pChnInfo->bitrate > 0
                                           ? pChnInfo->bitrate
                                           : getBitRate(pChnInfo->rcAttr.rcMode, dispPicSize, frameRate);
            } else if (VENC_RC_MODE_H264AVBR == pChnInfo->rcAttr.rcMode) {
                VENC_H264_AVBR_S *pH264AVbr = &pVencChnAttr->rcAttr.h264AVBR;
                pH264AVbr->GOP = pChnInfo->rcAttr.h264AVBR.GOP;
                pH264AVbr->statTime = statTime;
                pH264AVbr->dstFrameRate = frameRate;
                pH264AVbr->maxBitRate = pChnInfo->bitrate > 0
                                            ? pChnInfo->bitrate
                                            : getBitRate(pChnInfo->rcAttr.rcMode, dispPicSize, frameRate);
            } else if (VENC_RC_MODE_H264QPMAP == pChnInfo->rcAttr.rcMode) {
                VENC_H264_QPMAP_S *pH264QpMap = &pVencChnAttr->rcAttr.h264QPMap;
                pH264QpMap->GOP = pChnInfo->rcAttr.h264QPMap.GOP;
                pH264QpMap->statTime = statTime;
                pH264QpMap->dstFrameRate = frameRate;
            } else if (VENC_RC_MODE_H264CVBR == pChnInfo->rcAttr.rcMode) {
                VENC_H264_CVBR_S *pH264CVbr = &pVencChnAttr->rcAttr.h264CVBR;
                pH264CVbr->GOP = pChnInfo->rcAttr.h264QPMap.GOP;
                pH264CVbr->statTime = statTime; /* [1, 60]; the rate statistic time (s) */
                pH264CVbr->dstFrameRate = frameRate;
                pH264CVbr->maxBitRate = pChnInfo->bitrate > 0
                                            ? pChnInfo->bitrate
                                            : getBitRate(pChnInfo->rcAttr.rcMode, dispPicSize, frameRate);
                pH264CVbr->shortTermStatTime = 30; /* [1, 120]; the long-term rate statistic time (s)*/
                pH264CVbr->longTermStatTime = 5;   /* [1, 1440]; the long-term rate statistic time, the unit is
                                                 longTermStatTimeUnit(default minute)*/
                pH264CVbr->longTermMaxBitrate = pH264CVbr->maxBitRate - 1024; /* [2, 614400];the long-term target max
                                            bitrate, can not be larger than maxBitRate,the unit is kbps */
                pH264CVbr->longTermMinBitrate = 0; /* [0, 614400];the long-term target min bitrate,  can not be
                                                 larger than longTermMaxBitrate,the unit is kbps */
            } else {
                ASSERT(0);
            }
            break;
        }
        case PT_H265: {
            if (VENC_RC_MODE_H265CBR == pChnInfo->rcAttr.rcMode) {
                VENC_H265_CBR_S *pH265Cbr = &pVencChnAttr->rcAttr.h265CBR;
                pH265Cbr->GOP = pChnInfo->rcAttr.h265CBR.GOP;
                pH265Cbr->statTime = statTime;
                pH265Cbr->dstFrameRate = frameRate;
                pH265Cbr->bitRate = pChnInfo->bitrate > 0 ? pChnInfo->bitrate
                                                          : getBitRate(pChnInfo->rcAttr.rcMode, dispPicSize, frameRate);
            } else if (VENC_RC_MODE_H265FIXQP == pChnInfo->rcAttr.rcMode) {
                VENC_H265_FIXQP_S *pH265FixQp = &pVencChnAttr->rcAttr.h265FixQP;
                pH265FixQp->GOP = pChnInfo->rcAttr.h265FixQP.GOP;
                pH265FixQp->dstFrameRate = frameRate;
                pH265FixQp->IQP = 25;
                pH265FixQp->PQP = 30;
                pH265FixQp->BQP = 32;
            } else if (VENC_RC_MODE_H265VBR == pChnInfo->rcAttr.rcMode) {
                VENC_H265_VBR_S *pH265Vbr = &pVencChnAttr->rcAttr.h265VBR;
                pH265Vbr->GOP = pChnInfo->rcAttr.h265VBR.GOP;
                pH265Vbr->statTime = statTime;
                pH265Vbr->dstFrameRate = frameRate;
                pH265Vbr->maxBitRate = pChnInfo->bitrate > 0
                                           ? pChnInfo->bitrate
                                           : getBitRate(pChnInfo->rcAttr.rcMode, dispPicSize, frameRate);
            } else if (VENC_RC_MODE_H265AVBR == pChnInfo->rcAttr.rcMode) {
                VENC_H265_AVBR_S *pH265AVbr = &pVencChnAttr->rcAttr.h265AVBR;
                pH265AVbr->GOP = pChnInfo->rcAttr.h265AVBR.GOP;
                pH265AVbr->statTime = statTime;
                pH265AVbr->dstFrameRate = frameRate;
                pH265AVbr->maxBitRate = pChnInfo->bitrate > 0
                                            ? pChnInfo->bitrate
                                            : getBitRate(pChnInfo->rcAttr.rcMode, dispPicSize, frameRate);
            } else if (VENC_RC_MODE_H265QPMAP == pChnInfo->rcAttr.rcMode) {
                VENC_H265_QPMAP_S *pH265QpMap = &pVencChnAttr->rcAttr.h265QPMap;
                pH265QpMap->GOP = pChnInfo->rcAttr.h265QPMap.GOP;
                pH265QpMap->statTime = statTime;
                pH265QpMap->dstFrameRate = frameRate;
            } else {
                ASSERT(0);
            }
            break;
        }
        default:
            break;
    }

    if (PT_MJPEG == pChnInfo->type || PT_JPEG == pChnInfo->type) {
        pVencChnAttr->GOPAttr.GOPMode = VENC_GOPMODE_NORMALP;
    } else {
        memcpy(&pVencChnAttr->GOPAttr, pGOPAttr, sizeof(VENC_GOP_ATTR_S));
    }
    pVencChnAttr->vencAttr.pixelFormat = pChnInfo->pixelFormat;
}

ES_VOID getChnParams(const TEST_CHN_S_ENC *pChn, VENC_CHN_PARAM_S *pChnParam) {
    ES_VENC_GetChnParam(pChn->grpId, pChnParam);
    pChnParam->maxStrmCnt = MAX_STRM_NUM;
    pChnParam->priority = pChn->priority;
    pChnParam->pollWakeUpFrmCnt = pChn->pollWakeUpFrmCnt;
    pChnParam->frameRate.srcFrmRate = pChn->frameRate.srcFrmRate;
    pChnParam->frameRate.dstFrmRate = pChn->frameRate.dstFrmRate;
    pChnParam->cropCfg.bEnable = pChn->cropParam.bEnable;
    pChnParam->cropCfg.rect.x = pChn->cropParam.rect.x;
    pChnParam->cropCfg.rect.y = pChn->cropParam.rect.y;
    pChnParam->cropCfg.rect.width = pChn->cropParam.rect.width;
    pChnParam->cropCfg.rect.height = pChn->cropParam.rect.height;
    pChnParam->rotation = pChn->rotation;
    pChnParam->constChroma.bEnableConstChroma = pChn->constChroma.bEnableConstChroma;
    if (pChn->constChroma.cbValue != UINT32_MAX) {
        pChnParam->constChroma.cbValue = pChn->constChroma.cbValue;
    }
    if (pChn->constChroma.crValue != UINT32_MAX) {
        pChnParam->constChroma.crValue = pChn->constChroma.crValue;
    }
}
