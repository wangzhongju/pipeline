#ifndef __PL_LOG__H__
#define __PL_LOG__H__

#include <stdio.h>

#include "es_sys.h"
#include "zlog.h"

#define PLLOG_fileName(x) (strrchr(x, '/') ? strrchr(x, '/') + 1 : x)

#ifdef __cplusplus
extern "C" {
#endif

typedef enum esLOGPL_ID_E {
    PL_LOG_AVDEMUX = 1,
    PL_LOG_VDEC = 2,
    PL_LOG_VENC = 3,
    PL_LOG_SMUX = 4,
    PL_LOG_PREPROC = 5,
    PL_LOG_INFER = 6,
    PL_LOG_POSTPROC = 7,
    PL_LOG_TRACKER = 8,
    PL_LOG_OSD = 9,
    PL_LOG_GRID = 10,
    PL_LOG_SDEMUX = 11,
    PL_LOG_VO = 12,
    PL_LOG_ADEC = 13,
    PL_LOG_AO = 14,
    PL_LOG_AVSYNC = 15,
    PL_LOG_CORE = 16,
    PL_LOG_AVMUX = 17,
    PL_LOG_COMMON = 18,
    PL_LOG_QUEUE = 19,
    PL_LOG_TEE = 20,
    PL_LOG_LANUCH = 21,
    PL_LOG_OPENVO = 22,
    PL_LOG_OTHERS = 23,
    PL_LOG_DUAL_MUX = 24,
    PL_LOG_DUALGRID = 25,
    PL_LOG_IPCGRID = 26,
    PL_LOG_V4L2 = 27,
    PL_LOG_TESTSRC = 28,
    PL_LOG_TESTSINK = 29,
    PL_LOG_BUTT,
} LOGPL_ID_E;

enum {
    ZLOG_LEVEL_TRACE = 10,
    /* must equals conf file setting */
};

typedef enum esLOGPL_LEVEL_E {
    LOGPL_TRACE = 0,
    LOGPL_DEBUG = 1,
    LOGPL_INFO = 2,
    LOGPL_WARN = 3,
    LOGPL_ERROR = 4,
    LOGPL_FATAL = 5,
} LOGPL_LEVEL_E;

#ifndef LOG_TAG
#define LOG_TAG PLLOG_fileName(__FILE__)
#endif

#define app_notice(...) \
    zlog(getModCat(PL_LOG_ID), LOG_TAG, sizeof(LOG_TAG) - 1, ES_NULL, 0, __LINE__, ZLOG_LEVEL_TRACE, ##__VA_ARGS__)
#define app_debug(...) \
    zlog(getModCat(PL_LOG_ID), LOG_TAG, sizeof(LOG_TAG) - 1, ES_NULL, 0, __LINE__, ZLOG_LEVEL_DEBUG, ##__VA_ARGS__)
#define app_info(...) \
    zlog(getModCat(PL_LOG_ID), LOG_TAG, sizeof(LOG_TAG) - 1, ES_NULL, 0, __LINE__, ZLOG_LEVEL_INFO, ##__VA_ARGS__)
#define app_warn(...) \
    zlog(getModCat(PL_LOG_ID), LOG_TAG, sizeof(LOG_TAG) - 1, ES_NULL, 0, __LINE__, ZLOG_LEVEL_WARN, ##__VA_ARGS__)
#define app_error(...) \
    zlog(getModCat(PL_LOG_ID), LOG_TAG, sizeof(LOG_TAG) - 1, ES_NULL, 0, __LINE__, ZLOG_LEVEL_ERROR, ##__VA_ARGS__)
#define app_fatal(...) \
    zlog(getModCat(PL_LOG_ID), LOG_TAG, sizeof(LOG_TAG) - 1, ES_NULL, 0, __LINE__, ZLOG_LEVEL_FATAL, ##__VA_ARGS__)

ES_S32 LOGPL_Init(const ES_CHAR *pConfig);
ES_VOID LOGPL_Uninit();
zlog_category_t *getModCat(LOGPL_ID_E modID);

ES_S32 LOGPL_SetLevel(LOGPL_ID_E catId, LOGPL_LEVEL_E level);

// abandoned begin

ES_U32 LOGPL_GetLevel();
ES_VOID LOGPL_SetQuiet(ES_BOOL bQuiet);
ES_BOOL LOGPL_GetQuiet();
ES_S32 LOGPL_AddFpCallback(FILE *pFp, ES_U32 level);
// abandoned end
#ifdef __cplusplus
}
#endif

#endif
