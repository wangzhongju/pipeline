#define PL_LOG_ID PL_LOG_CORE
#include "log.h"

#include <string.h>

zlog_category_t *category[PL_LOG_BUTT];

ES_S32 LOGPL_Init(const ES_CHAR *pConfig) {
    if (pConfig && 0 != strlen(pConfig)) {
        printf("LOGPL_Init given config file %s\n", pConfig);
        if (zlog_init(pConfig)) {
            printf("LOGPL_Init fail\n");
            return ES_FAILURE;
        }
    } else {
        printf("LOGPL_Init use default config file\n");
#if defined __x86_64__
        if (zlog_init("logcat.conf")) {
#else
        if (zlog_init("/usr/local/etc/logcat.conf")) {
#endif
            printf("LOGPL_Init fail\n");
            return ES_FAILURE;
        }
    }

    memset(category, 0, sizeof(category));

    category[PL_LOG_AVDEMUX] = zlog_get_category("pl_avdemux");
    category[PL_LOG_VDEC] = zlog_get_category("pl_vdec");
    category[PL_LOG_VENC] = zlog_get_category("pl_venc");
    category[PL_LOG_SMUX] = zlog_get_category("pl_smux");
    category[PL_LOG_DUAL_MUX] = zlog_get_category("pl_dualmux");
    category[PL_LOG_PREPROC] = zlog_get_category("pl_preproc");
    category[PL_LOG_INFER] = zlog_get_category("pl_infer");
    category[PL_LOG_POSTPROC] = zlog_get_category("pl_postproc");
    category[PL_LOG_TRACKER] = zlog_get_category("pl_tracker");
    category[PL_LOG_OSD] = zlog_get_category("pl_osd");
    category[PL_LOG_GRID] = zlog_get_category("pl_grid");
    category[PL_LOG_DUALGRID] = zlog_get_category("pl_dualgrid");
    category[PL_LOG_SDEMUX] = zlog_get_category("pl_sdemux");
    category[PL_LOG_VO] = zlog_get_category("pl_vo");
    category[PL_LOG_ADEC] = zlog_get_category("pl_adec");
    category[PL_LOG_AO] = zlog_get_category("pl_ao");
    category[PL_LOG_AVSYNC] = zlog_get_category("pl_avsync");
    category[PL_LOG_CORE] = zlog_get_category("pl_core");
    category[PL_LOG_AVMUX] = zlog_get_category("pl_avmux");
    category[PL_LOG_COMMON] = zlog_get_category("pl_common");
    category[PL_LOG_QUEUE] = zlog_get_category("pl_queue");
    category[PL_LOG_TEE] = zlog_get_category("pl_tee");
    category[PL_LOG_OPENVO] = zlog_get_category("pl_openvo");
    category[PL_LOG_V4L2] = zlog_get_category("pl_v4l2");
    category[PL_LOG_TESTSRC] = zlog_get_category("pl_testsrc");
    category[PL_LOG_TESTSINK] = zlog_get_category("pl_testsink");
    category[PL_LOG_OTHERS] = zlog_get_category("pl_others");

    zlog_profile();

    printf("LOGPL_Init succ\n");
    return ES_SUCCESS;
}

ES_VOID LOGPL_Uninit() { zlog_fini(); }

zlog_category_t *getModCat(LOGPL_ID_E modID) { return category[modID]; }

ES_S32 LOGPL_SetLevel(LOGPL_ID_E catId, LOGPL_LEVEL_E level) {
    zlog_category_t *p = getModCat(catId);
    if (!p) {
        return ES_FAILURE;
    }
    ES_S32 zlogLev = 0;
    switch (level) {
        case LOGPL_TRACE:
            zlogLev = ZLOG_LEVEL_TRACE;
            break;
        case LOGPL_DEBUG:
            zlogLev = ZLOG_LEVEL_DEBUG;
            break;
        case LOGPL_INFO:
            zlogLev = ZLOG_LEVEL_INFO;
            break;
        case LOGPL_WARN:
            zlogLev = ZLOG_LEVEL_WARN;
            break;
        case LOGPL_ERROR:
            zlogLev = ZLOG_LEVEL_ERROR;
            break;
        case LOGPL_FATAL:
            zlogLev = ZLOG_LEVEL_FATAL;
            break;
        default:
            return ES_FAILURE;
    }
    zlog_level_switch(p, zlogLev);
    return ES_SUCCESS;
}

// abandoned begin

ES_U32 LOGPL_GetLevel() { return LOGPL_TRACE; }

ES_VOID LOGPL_SetQuiet(ES_BOOL bQuiet) {
    (ES_VOID)(bQuiet);
    return;
}

ES_BOOL LOGPL_GetQuiet() { return ES_TRUE; }

ES_S32 LOGPL_AddFpCallback(FILE *pFp, ES_U32 level) {
    (ES_VOID)(pFp);
    (ES_VOID)(level);
    return ES_SUCCESS;
}
// abandoned end
