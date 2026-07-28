#define PL_LOG_ID PL_LOG_VDEC

#include "pl_option_dec.h"

#include <getopt.h>
#include <stdint.h>

#include "log.h"
#include "pl_comm_dec.h"
//#include "../../common/pl_comm_sys.h"

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"

#define LINE_MAX_LENGTH (512)
#define MAXARGS (128)

typedef struct hiDEMUXER_META_S {
    PAYLOAD_TYPE_E type;
    ES_S32 width;
    ES_S32 height;
} DEMUXER_META_S;

typedef struct hiDEMUXER_S {
    ES_BOOL bAbort;
    ES_BOOL bEos;
    ES_U32 circleNum;
    ES_S32 videoIdx;
    ES_CHAR *pUrl;
    pthread_t thrd;
    ES_VOID *pDemuxerHandle;  // AVFormatContext
    ES_VOID *pBufferQueue;    // PACKET_QUEUE_S
    ES_VOID *pFilter;         // AVBSFContext
} DEMUXER_S;

typedef struct hiDEMUXER_BUF_INFO_S {
    ES_S64 PTS;
    ES_S64 DTS;
    ES_U8 *pData;
    ES_S32 size;
    ES_BOOL bKeyFrame;
    ES_BOOL bEos;
} DEMUXER_BUF_INFO_S;

// static ES_BOOL isCodecSupport(enum AVCodecID id) {
//     static enum AVCodecID supportCodec[] = {
//         AV_CODEC_ID_H264,
//         AV_CODEC_ID_HEVC,
//         AV_CODEC_ID_H265,
//         AV_CODEC_ID_MJPEG,
//     };
//     size_t num = sizeof(supportCodec) / sizeof(supportCodec[0]);
//     for (size_t i = 0; i < num; i++) {
//         if (id == supportCodec[i]) {
//             return ES_TRUE;
//         }
//     }
//     return ES_FALSE;
// }

// static ES_S32 openInputAndFindStream(const ES_CHAR* pUrl, AVFormatContext** pContext, ES_S32* pStreamIndex) {
//     if (!pUrl) {
//         app_error("url is null.");
//         return ES_FAILURE;
//     }
//     /* Try to open url. */
//     AVFormatContext* pFmtCtx = avformat_alloc_context();
//     if (!pFmtCtx) {
//         app_error("alloc context failed.");
//         return ES_FAILURE;
//     }
//     /* Open input file, and allocate format context. */
//     if (avformat_open_input(&pFmtCtx, pUrl, ES_NULL, ES_NULL) < 0) {
//         app_debug("Could not open source file '%s'.", pUrl);
//         avformat_free_context(pFmtCtx);
//         return ES_FAILURE;
//     }
//     /* Retrieve stream information. */
//     if (avformat_find_stream_info(pFmtCtx, ES_NULL) < 0) {
//         app_error("Could not find stream information.");
//         avformat_free_context(pFmtCtx);
//         return ES_FAILURE;
//     }
//     /* Find best video stream. */
//     int ret = av_find_best_stream(pFmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, ES_NULL, 0);
//     if (ret < 0) {
//         app_error("Could not find video stream in input file '%s'.", pUrl);
//         avformat_free_context(pFmtCtx);
//         return ES_FAILURE;
//     }
//     /* Check if we support video codec. */
//     int streamIdx = ret;
//     enum AVCodecID id = pFmtCtx->streams[streamIdx]->codecpar->codec_id;
//     if (!isCodecSupport(id)) {
//         app_error("Could not support %s video stream.", avcodec_get_name(id));
//         avformat_free_context(pFmtCtx);
//         return ES_FAILURE;
//     }
//     *pContext = pFmtCtx;
//     *pStreamIndex = streamIdx;
//     return ES_SUCCESS;
// }

// ES_S32 DEMUXER_ProbeMeta(const ES_CHAR* pUrl, DEMUXER_META_S* pMeta) {
//     if (!pMeta) {
//         app_error("pMeta is null.");
//         return ES_FAILURE;
//     }
//     pMeta->type = PT_BUTT;
//     pMeta->height = -1;
//     pMeta->width = -1;
//     AVFormatContext* pFmtCtx = ES_NULL;
//     ES_S32 streamIndex = -1;
//     if (openInputAndFindStream(pUrl, &pFmtCtx, &streamIndex) == ES_SUCCESS) {
//         pMeta->width = pFmtCtx->streams[streamIndex]->codecpar->width;
//         pMeta->height = pFmtCtx->streams[streamIndex]->codecpar->height;
//         enum AVCodecID id = pFmtCtx->streams[streamIndex]->codecpar->codec_id;
//         ES_S32 format = pFmtCtx->streams[streamIndex]->codecpar->format;
//         app_debug("Find %dx%d %s stream format %d with index %d in source file '%s'.",
//                 pMeta->width,
//                 pMeta->height,
//                 avcodec_get_name(id),
//                 format,
//                 streamIndex,
//                 pUrl);
//         if (id == AV_CODEC_ID_H264) {
//             pMeta->type = PT_H264;
//         } else if (id == AV_CODEC_ID_HEVC || id == AV_CODEC_ID_H265) {
//             pMeta->type = PT_H265;
//         } else if (id == AV_CODEC_ID_MJPEG) {
//             pMeta->type = PT_MJPEG;
//         }
//         avformat_free_context(pFmtCtx);
//         return ES_SUCCESS;
//     }
//     return ES_FAILURE;
// }

static ES_VOID setDefaultDecodeParameter(DEC_CHN_S *pCmd) {
    if (!pCmd->displayFrameNum) {
        pCmd->displayFrameNum = 2;
    }
    if (!pCmd->refFrameNum) {
        pCmd->refFrameNum = 8;
    }
    if (pCmd->minBufSize == -1) {
        pCmd->minBufSize = (pCmd->width * pCmd->height * 3) >> 1;
    }
    if (pCmd->videoMode == VIDEO_MODE_BUTT) {
        pCmd->videoMode = VIDEO_MODE_FRAME;
    }
    if (pCmd->videoDecMode == VIDEO_DEC_MODE_BUTT) {
        pCmd->videoDecMode = VIDEO_DEC_MODE_IPB;
    }
    if (pCmd->outputOrder == VIDEO_OUTPUT_ORDER_BUTT) {
        pCmd->outputOrder = VIDEO_OUTPUT_ORDER_DISP;
    }
    if (pCmd->pixelFormat[0] == PIXEL_FORMAT_BUTT) {
        pCmd->pixelFormat[0] = PIXEL_FORMAT_NV12;
    }
    if (pCmd->pixelFormat[1] == PIXEL_FORMAT_BUTT) {
        pCmd->pixelFormat[1] = PIXEL_FORMAT_NV12;
    }
    switch (pCmd->type) {
        case PT_H264:
        case PT_H265:
            pCmd->frameBufCnt = pCmd->refFrameNum + pCmd->displayFrameNum;
            break;
        case PT_JPEG:
        case PT_MJPEG:
            pCmd->frameBufCnt = pCmd->displayFrameNum + 1;
            break;
        default:
            break;
    }
}

static ES_VOID setDefaultCommonParameter(DEC_CHN_S *pCmd) {
    if (!pCmd->multiple) {
        pCmd->multiple = 1;
    }
}

static ES_VOID setDefaultCodecParameter(DEC_CHN_S *pCmd, DEC_CODEC_TYPE_E codecType) {
    if (codecType == TEST_CODEC_TYPE_DECODE) {
        setDefaultDecodeParameter(pCmd);
    }
    setDefaultCommonParameter(pCmd);
}

static ES_BOOL validateAndCorrectCommand(DEC_CHN_S *pCmd, const DEMUXER_META_S *pMeta, DEC_CODEC_TYPE_E codecType) {
    if (pCmd->type == PT_BUTT) {
        if (codecType == TEST_CODEC_TYPE_DECODE && pMeta->type != PT_BUTT) {
            pCmd->type = pMeta->type;
            app_info("Correct 'type' to %d.", pCmd->type);
        } else {
            app_error("'type' not set by user.");
            return ES_FALSE;
        }
    }

    if (!pCmd->width || !pCmd->height) {
        if (codecType == TEST_CODEC_TYPE_DECODE && pMeta->width >= 0 && pMeta->height >= 0) {
            pCmd->width = pMeta->width;
            pCmd->height = pMeta->height;
            app_info("Correct 'w/d' to %d/%d.", pCmd->width, pCmd->height);
        } else {
            app_info("Illegal size, w:%d h:%d.", pCmd->width, pCmd->height);
        }
    }

    if (pCmd->multiple > MAX_CHN_NUM) {
        app_error("'multiple' is %u.", pCmd->multiple);
        return ES_FALSE;
    }
    for (ES_S32 i = 0; i < ES_VDEC_OUT_CHN_NUM; i++) {
        if (!pCmd->cropParam[i].bEnable) {
            pCmd->cropParam[i].rect.width = pCmd->width;
            pCmd->cropParam[i].rect.height = pCmd->height;
        }
        if (!pCmd->scaleParam[i].bEnable) {
            pCmd->scaleParam[i].scaleWidth = pCmd->width;
            pCmd->scaleParam[i].scaleHeight = pCmd->height;
        }
    }

    setDefaultCodecParameter(pCmd, codecType);

    app_debug("'input' is %s.", pCmd->inputFile);
    app_debug("'type' is %d.", pCmd->type);
    app_debug("'size' is %ux%u.", pCmd->width, pCmd->height);
    app_debug("'pixelFmt' is %d %d.", pCmd->pixelFormat[0], pCmd->pixelFormat[1]);
    app_debug("'stream_send_mode' is %d.", pCmd->videoMode);
    app_debug("'dec_mode' is %d.", pCmd->videoDecMode);
    app_debug("'output_order' is %d.", pCmd->outputOrder);
    app_debug("'dp_frame_num' is %u.", pCmd->displayFrameNum);
    app_debug("'ref_frame_num' is %u.", pCmd->refFrameNum);
    app_debug("'output' is %s.", pCmd->outputFile);
    app_debug("'multiple' is %u.", pCmd->multiple);
    return ES_TRUE;
}

// static ES_VOID getMetaAndGuessCodecTypeByUrl(const char *pUrl, DEMUXER_META_S *pMeta, DEC_CODEC_TYPE_E *pType) {
//     DEMUXER_ProbeMeta(pUrl, pMeta);
//     *pType = TEST_CODEC_TYPE_DECODE;
// }

ES_S32 validateAndCreateChns_ext(DEC_CHN_S *pChnParams, PAYLOAD_TYPE_E type, ES_S32 width, ES_S32 height) {
    ES_S32 ret = ES_SUCCESS;
    DEMUXER_META_S meta = {type, width, height};
    DEC_CODEC_TYPE_E codecType = TEST_CODEC_TYPE_DECODE;
    // Get metadata and guess codec type.
    if (!validateAndCorrectCommand(pChnParams, &meta, codecType)) {
        ASSERT(0);
    }

    return ret;
}