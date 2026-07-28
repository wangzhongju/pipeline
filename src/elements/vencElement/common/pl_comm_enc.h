

#ifndef __PL_COMM_ENC_H__
#define __PL_COMM_ENC_H__

#include <stdio.h>

#include "es_buffer.h"
#include "es_comm_venc.h"

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
#define MAX_POSTFIX_LEN (16)
#define MAX_CHN_NUM (128)
#define MAX_SEI_NUM (10)
#define MAX_STRM_NUM (0xffffffff)
#define DEFAULT -255

#define TEST_PRT(fmt...)                             \
    do {                                             \
        printf("[%s]-%d: ", __FUNCTION__, __LINE__); \
        printf(fmt);                                 \
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

typedef enum esTEST_CODEC_TYPE_E_ENC {
    TEST_CODEC_TYPE_ENCODE,
} TEST_CODEC_TYPE_E_ENC;

/*******************************************************
    structure define
*******************************************************/
typedef enum esTHREAD_CONTRL_E {
    THREAD_CTRL_START,
    THREAD_CTRL_PAUSE,
    THREAD_CTRL_STOP,
} THREAD_CONTRL_E;

typedef struct esIDR_CHN_S {
    ES_S32 instant;
} IDR_CHN_S;

typedef struct esTEST_ROI_S {
    ES_BOOL bROISet[ES_VENC_MAX_ROI_NUM];
    VENC_ROI_ATTR_S ROIAttr[ES_VENC_MAX_ROI_NUM];
    VENC_JPEG_ROI_ATTR_S JPEGROIAttr;
    ES_BOOL JPEGROIEnable;
} TEST_ROI_S;

typedef struct esTEST_SSE_S {
    ES_BOOL bSSESet[ES_VENC_MAX_SSE_NUM];
    VENC_SSE_CFG_S SSECfg[ES_VENC_MAX_SSE_NUM];
} TEST_SSE_S;

typedef struct esTEST_ENC_SEI_S {
    ES_U8 *pData;
    ES_U32 len;
} TEST_ENC_SEI_S;

typedef struct esTEST_ENC_SEI_INFO_S {
    TEST_ENC_SEI_S SEIData[MAX_SEI_NUM];
} TEST_ENC_SEI_INFO_S;

typedef struct TEST_VUI_INFO_S {
    ES_U8 videoSingalFlag;
    ES_U8 videoFormat;
    ES_U8 videoRange;
    ES_U16 sarWidth;
    ES_U16 sarHeight;
    ES_U8 timingInfoFlag;
} VUI_INFO_S;

typedef struct esTEST_TRANS_INFO_S {
    ES_BOOL enableScalingList;
    ES_S32 qpOffset;
} TEST_TRANS_INFO_S;

typedef struct esTEST_ENTROPY_INFO_S {
    ES_U32 enableCabac;  // 0: CAVLC, 1: CABAC.
} TEST_ENTROPY_INFO_S;

typedef struct esTEST_DBLK_INFO_S {
    ES_U32 disableDeblockingFlag;
    ES_S32 tcOffset;
    ES_S32 betaOffset;
} TEST_DBLK_INFO_S;

typedef struct esTEST_VENC_PROTOCOL_S {
    // slicesplit
    ES_U32 sliceSize;
    // VUI
    VUI_INFO_S VUIInfo;
    // trans
    TEST_TRANS_INFO_S transInfo;
    // SAO
    ES_U32 SAOEnabledFlag;
    // entropy
    TEST_ENTROPY_INFO_S entropyInfo;
    // smoothingIntra
    ES_U32 smoothingIntra;
    // dblk
    TEST_DBLK_INFO_S dblkInfo;
    // intra refresh
    VENC_INTRA_REFRESH_S intraRefresh;
} TEST_VENC_PROTOCOL_S;

typedef struct esTEST_VENC_GETSTRM_SINK_S {
    ES_CHAR *pOutputFile;
    ES_VOID *pSink;
    ES_S32 sinkType;
} TEST_VENC_GETSTRM_SINK_S;

// special for fd mode
typedef struct esTEST_VENC_GETSTREAM_PARA_S {
    pthread_t threadPid;
    THREAD_CONTRL_E threadStat;
    ES_S32 chnCnt;
    ES_S32 startGrpId;
    ES_U32 pollWakeUpFrmCnt[ES_VENC_MAX_CHN_NUM];
    ES_S32 kpiHandle[ES_VENC_MAX_CHN_NUM];
    TEST_VENC_GETSTRM_SINK_S chnSink[ES_VENC_MAX_CHN_NUM];
} TEST_VENC_GETSTREAM_PARA_S;

typedef struct esTEST_SENDDATA_PARA_S_ENC {
    pthread_t sendDataPid;
    ES_BOOL bThreadStart;
    _Atomic(ES_U32) fpsCount;
    pthread_mutex_t sendDataMutex;
    pthread_cond_t sendDataCond;
} TEST_SENDDATA_PARA_S_ENC;

typedef struct esTEST_GETDATA_PARA_S_ENC {
    pthread_t getDataPid;
    ES_BOOL bThreadStart;
} TEST_GETDATA_PARA_S_ENC;

typedef struct esTEST_JPEG_PARA_S {
    ES_U32 qFactor;
    ES_BOOL enableQt;
    ES_CHAR qTableFile[MAX_FILE_NAME_LEN];
    ES_U32 MCUPerECS;
} TEST_JPEG_PARA_S;

typedef struct esTEST_HDR10_DISPLAY_S {
    ES_U32 maxluma;
    ES_U32 minluma;
} TEST_HDR10_DISPLAY_S;

typedef struct esTEST_CONST_CHROMA_S {
    ES_BOOL bEnableConstChroma;
    ES_U32 cbValue;
    ES_U32 crValue;
} TEST_CONST_CHROMA_S;

typedef struct esTEST_CHN_S_ENC {
    /* parser related*/
    ES_U32 multiple;  // for "-x" value
    ES_CHAR *pStreamcfg[MAX_CHN_NUM];

    ES_S32 grpId;
    ES_S32 kpiHandle;
    ES_S32 sinkType;
    ES_S32 srcCircleNum;
    ES_U32 srcMaxBufNum;
    ES_U32 srcWaterLevel;
    ES_S32 srcSendRate;
    ES_S32 enableFd;
    ES_S32 effectNumber;
    ES_U32 firstPic;
    ES_U32 lastPic;

    PAYLOAD_TYPE_E type;
    ES_U32 width;
    ES_U32 height;
    ES_CHAR inputFile[MAX_FILE_NAME_LEN];
    ES_CHAR outputFile[MAX_FILE_NAME_LEN];
    TEST_SENDDATA_PARA_S_ENC sendData;
    TEST_GETDATA_PARA_S_ENC getData;
    CROP_INFO_S cropParam;
    ES_U32 dynamicRange;

    /* for venc */
    ES_U32 priority;
    ES_U32 pollWakeUpFrmCnt;
    TEST_CONST_CHROMA_S constChroma;
    ES_U32 oneStreamBuffer;
    ES_S32 vbSource;
    VENC_FRAME_RATE_S frameRate;
    VENC_RC_ATTR_S rcAttr;
    VENC_GOP_ATTR_S GOPAttr;
    char *pGopCfg;
    TEST_ROI_S ROIParam;
    TEST_SSE_S SSEParam;
    IDR_CHN_S IDRInfo;
    TEST_ENC_SEI_INFO_S encSEI;
    TEST_VENC_PROTOCOL_S protocol;
    TEST_JPEG_PARA_S JPEGParam;
    ES_U32 align;
    TEST_HDR10_DISPLAY_S hdr10Display;
    ES_BOOL bByFrame;

    ES_S32 profile;
    PIXEL_FORMAT_E pixelFormat;
    ES_BOOL bCircleSend;
    COLOR_GAMUT_E colorGamut;
    ROTATION_E rotation;

    ES_S32 qpMapFlag;
    ES_S32 qpMapBlockUnit;
    ES_CHAR qpMapFile[MAX_FILE_NAME_LEN];
    ES_CHAR ipcmMapFile[MAX_FILE_NAME_LEN];
    ES_S32 skipMapBlockUnit;
    ES_CHAR skipMapFile[MAX_FILE_NAME_LEN];

    // rc data
    ES_S32 ctbRc;
    ES_S32 dstFrameRate;
    ES_S32 blockRCSize;
    ES_U32 rcQpDeltaRange;
    ES_U32 rcBaseMBComplexity;
    ES_S32 picSkip;
    ES_S32 picQpDeltaMin;
    ES_S32 picQpDeltaMax;
    ES_S32 ctbRcRowQpStep;

    ES_FLOAT tolCtbRcInter;
    ES_FLOAT tolCtbRcIntra;

    /*for element*/
    ES_VOID *element;
    ES_BOOL frameDumpFlag;
    ES_BOOL packDumpFlag;
    ES_S32 bitrate;

} TEST_CHN_S_ENC;

typedef struct esTEST_Client_S_ENC {
    TEST_CHN_S_ENC *pMultiChn[MAX_CHN_NUM];
    ES_U32 groupNum;
    ES_S32 startGrpId;
} TEST_Client_S_ENC;

/*******************************************************
    function announce
*******************************************************/
// // System
// ES_S32 COMM_SYS_GetPicSize(PIC_SIZE_E picSize, SIZE_S *pSize);
// // ES_VOID COMM_SYS_Exit(void);
// ES_S32 COMM_SYS_Init(const VB_CONFIG_S *pVbConfig);
// ES_S32 COMM_SYS_InitWithVbSupplement(VB_CONFIG_S *pVbConf, ES_U32 supplementConfig);

// Venc
// ES_S32 COMM_VENC_MemConfig(ES_VOID);
ES_S32 initAndStart(const TEST_Client_S_ENC *pEncClient);
ES_S32 destroyChns(ES_S32 chnId);
// ES_S32 COMM_VENC_StartGetStream(const TEST_Client_S *pEncClient);
ES_S32 startChn(const TEST_Client_S_ENC *pEncClient, ES_S32 i);
ES_S32 startGetStream(const TEST_Client_S_ENC *pEncClient, ES_S32 chnId);
ES_S32 stopGetStream(const TEST_Client_S_ENC *pEncClient, ES_S32 chnId);
// ES_S32 COMM_VENC_StartSendFrame(const TEST_Client_S *pEncClient);
// ES_S32 COMM_VENC_StopSendFrame(const TEST_Client_S *pEncClient, ES_BOOL bForce);
// ES_VOID COMM_VENC_FinishSendFrame(VENC_CHN chnId);
// ES_VOID COMM_VENC_FinishSendFrame(VENC_CHN chnId);
ES_S32 stringFindLastOf(const ES_CHAR *pString, const ES_CHAR tok);
ES_S32 getFilePostfix(PAYLOAD_TYPE_E payload, char *szFilePostfix);
ES_S32 saveStream(FILE *pFd, VENC_STREAM_S *pStream);
ES_S32 saveStreamJpeg(FILE *pFd, VENC_STREAM_S *pStream, char *filename);
void strReplace(char *str1, char *str2, char *str3);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */

#endif /* End of #ifndef __TEST_COMMON_H__ */
