#define PL_LOG_ID PL_LOG_COMMON
#include "dspManager.h"

#include <es_ak_api.h>
#include <es_ak_error.h>
#include <es_ak_types.h>

std::map<uint, uint> EsPlDspManager::m_openDspCountMap;

ES_S32 EsPlDspManager::openDsp(const int dspID) {
    int ret = 0;
    if (0 == m_openDspCountMap[0]) {
        ret = ES_AK_Init();
        if (0 != ret) {
            app_error("%s-%s-%d: %s %d\n", PLLOG_fileName(__FILE__), __func__, __LINE__, " dsp init error, the err is ", ret);
            return ret;
        }
    }
    app_debug("%s-%s-%d: %s \n", PLLOG_fileName(__FILE__), __func__, __LINE__, "will init dsp dev ");
    m_openDspCountMap[0]++;
    return 0;
}

int EsPlDspManager::closeDsp(const int dspID) {
    int ret = 0;
    if (1 == m_openDspCountMap[0]) {
        ret = ES_AK_Deinit();
        if (0 != ret) {
            app_error("%s-%s-%d: %s %d\n", PLLOG_fileName(__FILE__), __func__, __LINE__, " dsp close error, the err is ",
                      ret);
            return ret;
        }
    }
    app_debug("%s-%s-%d: %s \n", PLLOG_fileName(__FILE__), __func__, __LINE__, "will close dsp dev ");
    m_openDspCountMap[0]--;
    if (m_openDspCountMap[0] <= 0) {
        m_openDspCountMap[0] = 0;
    }
    return 0;
}
