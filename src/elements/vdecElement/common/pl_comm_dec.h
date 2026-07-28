

#ifndef __PL_COMM_DEC_H__
#define __PL_COMM_DEC_H__

#include <pthread.h>

#include "es_buffer.h"
#include "es_comm_vdec.h"
#include "es_comm_video.h"
#include "es_common.h"
#include "es_math.h"
#include "log.h"

#ifndef __cplusplus
#include <stdatomic.h>
#else
#include <atomic>
#define _Atomic(X) std::atomic<X>
#endif

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

/*******************************************************
    macro define
*******************************************************/
#define MAX_FILE_NAME_LEN (128)
#define MAX_CHN_NUM (128)

#define CHECK_CHN_RET(express, Chn, name)                                                                          \
    do {                                                                                                           \
        ES_S32 ret;                                                                                                \
        ret = express;                                                                                             \
        if (ES_SUCCESS != ret) {                                                                                   \
            app_error("\033[0;31m%s chn %d failed at %s: LINE: %d with %#x!\033[0;39m\n", name, Chn, __FUNCTION__, \
                      __LINE__, ret);                                                                              \
            fflush(stdout);                                                                                        \
            return ret;                                                                                            \
        }                                                                                                          \
    } while (0)

#define CHECK_RET(express, name)                                                                                       \
    do {                                                                                                               \
        ES_S32 ret;                                                                                                    \
        ret = express;                                                                                                 \
        if (ES_SUCCESS != ret) {                                                                                       \
            app_error("\033[0;31m%s failed at %s: LINE: %d with %#x!\033[0;39m\n", name, __FUNCTION__, __LINE__, ret); \
            return ret;                                                                                                \
        }                                                                                                              \
    } while (0)

#define TEST_PRT(fmt...)                             \
    do {                                             \
        printf("[%s]-%d: ", __FUNCTION__, __LINE__); \
        printf(fmt);                                 \
    } while (0)

#define CHECK_NULL_PTR(ptr)                                                       \
    do {                                                                          \
        if (NULL == ptr) {                                                        \
            app_error("func:%s,line:%d, NULL pointer\n", __FUNCTION__, __LINE__); \
            return ES_FAILURE;                                                    \
        }                                                                         \
    } while (0)

#define PRINTF_VDEC_GRP_STATUS(Chn, status)                                                                           \
    do {                                                                                                              \
        app_info(                                                                                                     \
            "\033[0;33m---------------------------------------------------------------------------------\033[0;39m"); \
        app_info(                                                                                                     \
            "\033[0;33mchn:%d, Type:%d, bStart:%d, DecodeFrames:%d, LeftPics:%d, LeftBytes:%d, LeftFrames:%d, "       \
            "RecvFrames:%d\033[0;39m",                                                                                \
            Chn, status.type, status.bStartRecvStream, status.decodeStreamFrames, status.leftPics,                    \
            status.leftStreamBytes, status.leftStreamFrames, status.recvStreamFrames);                                \
        app_info(                                                                                                     \
            "\033[0;33mFormatErr:%d, picSizeErrSet:%d, streamUnsprt:%d, packErr:%d,"                                  \
            "prtclNumErrSet:%d,  vdecHardwareErr:%d,  picBufSizeErrSet:%d, vdecStreamNotRelease: %d\033[0;39m",       \
            status.vdecDecErr.formatErr, status.vdecDecErr.picSizeErrSet, status.vdecDecErr.streamUnsprt,             \
            status.vdecDecErr.packErr, status.vdecDecErr.prtclNumErrSet, status.vdecDecErr.vdecHardwareErr,           \
            status.vdecDecErr.picBufSizeErrSet, status.vdecDecErr.vdecStreamNotRelease);                              \
        app_info(                                                                                                     \
            "\033[0;33m---------------------------------------------------------------------------------\033[0;39m"); \
    } while (0)

#define ASSERT(expr)                            \
    do {                                        \
        if (!(expr)) {                          \
            TEST_PRT(                           \
                "\nASSERT at:\n"                \
                "  >Function : %s\n"            \
                "  >Line No. : %d\n"            \
                "  >Condition: %s\n",           \
                __FUNCTION__, __LINE__, #expr); \
            exit(-1);                           \
        }                                       \
    } while (0)
/*******************************************************
    enum define
*******************************************************/

typedef enum esDEC_CODEC_TYPE_E {
    TEST_CODEC_TYPE_DECODE,
} DEC_CODEC_TYPE_E;

/*******************************************************
    structure define
*******************************************************/

typedef struct esTEST_VDEC_BUF {
    ES_U32 picBufSize;
    ES_U32 tmvBufSize;
    ES_BOOL bPicBufAlloc;
    ES_BOOL bTmvBufAlloc;
} TEST_VDEC_BUF;

typedef struct esDEC_SENDDATA_PARA_S {
    pthread_t sendDataPid;
    ES_BOOL bThreadStart;
    _Atomic(ES_U32) fpsCount;
    pthread_mutex_t sendDataMutex;
    pthread_cond_t sendDataCond;
} DEC_SENDDATA_PARA_S;

typedef struct esDEC_GETDATA_PARA_S {
    pthread_t getDataPid;
    ES_BOOL bThreadStart;
} DEC_GETDATA_PARA_S;

typedef struct esDEC_CHN_S {
    /* parser related*/
    ES_U32 multiple;  // for "-x" value
    ES_CHAR *pStreamcfg[MAX_CHN_NUM];

    ES_S32 grpId;
    ES_S32 kpiHandle;
    ES_S32 sinkType;
    ES_S32 srcCircleNum;
    ES_S32 srcSendRate;
    ES_U32 effectNumber;
    ES_U32 firstPic;
    ES_U32 lastPic;
    ES_U32 assignOutputBufSize;

    PAYLOAD_TYPE_E type;
    ES_U32 width;
    ES_U32 nDieID;
    ES_CHAR vbName[MAX_FILE_NAME_LEN];
    ES_U32 height;
    ES_U32 align;
    ES_CHAR inputFile[MAX_FILE_NAME_LEN];
    ES_CHAR outputFile[MAX_FILE_NAME_LEN];
    DEC_SENDDATA_PARA_S sendData;
    DEC_GETDATA_PARA_S getData;
    CROP_INFO_S cropParam[ES_VDEC_OUT_CHN_NUM];
    SCALE_S scaleParam[ES_VDEC_OUT_CHN_NUM];
    ES_U32 dynamicRange;

    /* for vdec */
    ES_U32 frameBufCnt;
    VIDEO_MODE_E videoMode;
    ES_BOOL outputChn[ES_VDEC_OUT_CHN_NUM];
    ES_S32 notDisplay;
    VIDEO_DEC_MODE_E videoDecMode;
    VIDEO_OUTPUT_ORDER_E outputOrder;
    ES_U32 displayFrameNum;
    ES_U32 refFrameNum;
    PIXEL_FORMAT_E pixelFormat[ES_VDEC_OUT_CHN_NUM];
    ES_U32 alpha;
    ES_S32 milliSec;  // may no need
    ES_U64 ptsInit;
    ES_U64 ptsIncrease;
    ES_BOOL bCircleSend;
    ES_S32 minBufSize;
    ES_BOOL bGetSEIData;
    COLOR_GAMUT_E colorGamut;
    ES_S32 userPicInstant;
    VB_POOL userPicPoolId;

    /*for element*/
    ES_VOID *element;
} DEC_CHN_S;

typedef struct esDEC_Client_S {
    DEC_CHN_S *pMultiChn[MAX_CHN_NUM];
    ES_U32 groupNum;
    ES_S32 startGrpId;
    ES_S32 grpIdOffset;
    VDEC_MOD_PARAM_S modParam;
} DEC_Client_S;

/*******************************************************
    function announce
*******************************************************/
// Vdec
ES_S32 COMM_VDEC_InitVBPool(const DEC_Client_S *pDecClient);
ES_VOID COMM_VDEC_ExitVBPool(const DEC_Client_S *pDecClient);
ES_S32 COMM_VDEC_Start(const DEC_Client_S *pDecClient);

ES_VOID printfVdecGrpStatus(ES_S32 grpId, VDEC_GRP_STATUS_S status);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */

#endif /* End of #ifndef __PL_COMM_VIDEOON_H__ */
