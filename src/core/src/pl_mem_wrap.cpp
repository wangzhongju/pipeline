#define PL_LOG_ID PL_LOG_CORE
#include "pl_mem_wrap.h"

#include <unistd.h>

#include <map>

#include "queue.h"
using namespace std;
BlockQueueManager manager;

ES_S32 PL_ES_VB_CreatePool(VB_POOL_CONFIG_S *pstVbPoolCfg, VB_POOL *poolId, bool bisGetIOVA) {
    ES_S32 ret = ES_SUCCESS;

    ret = ES_VB_CreatePool(pstVbPoolCfg, poolId);
    if (ES_SUCCESS == ret) {
        for (ES_U32 cnt = 0; cnt < pstVbPoolCfg->blkCnt; cnt++) {
            plVBstr *vbStruct1 = new plVBstr;
            ES_CHAR mmzName[ES_MAX_MMZ_NAME_LEN] = {0};
            memcpy(mmzName, pstVbPoolCfg->mmzName, ES_MAX_MMZ_NAME_LEN);
            vbStruct1->poolId = *poolId;
            vbStruct1->blkSize = pstVbPoolCfg->blkSize;
            memcpy(vbStruct1->name, pstVbPoolCfg->mmzName, ES_MAX_MMZ_NAME_LEN);
            ret = 1;
            do {
                ret = ES_VB_GetBlock(*poolId, pstVbPoolCfg->blkSize, mmzName, &vbStruct1->memFd);
                if (ES_SUCCESS != ret) {
                    app_error("%s-%s-%d fail creat fail  poolId %d \n", PLLOG_fileName(__FILE__), __func__, __LINE__,
                              *poolId);
                }
                if (bisGetIOVA && ES_SUCCESS == ret) {
                    ES_VOID *pIOVA = ES_NULL;
                    int tempret = ES_VB_AllocIOVA(vbStruct1->memFd, VB_UID_HAE, &pIOVA);
                    if (ES_SUCCESS != tempret) {
                        app_error("%s-%s-%d  fail  get IOVA \n", PLLOG_fileName(__FILE__), __func__, __LINE__);
                    }
                    vbStruct1->pIOVA = pIOVA;
                }

                app_info("\n creat  get poolId %p  cnt %u poolID %u  fd %lu size %d name %s\n", vbStruct1, cnt, *poolId,
                         vbStruct1->memFd, vbStruct1->blkSize, vbStruct1->name);

                usleep(10000);
            } while (ES_SUCCESS != ret);

            manager.insert(*poolId, vbStruct1);
        }
    }
    return ret;
}

ES_S32 PL_ES_VB_GetBlock(VB_POOL poolId, ES_U64 blkSize, const ES_CHAR *strZone, ES_U64 *memFd, const char *name,
                         bool bisGetIOVA, ES_VOID **pIOVA) {
    ES_S32 ret = ES_SUCCESS;

    plVBstr *poppedStruct1 = manager.pop(poolId);

    if (poppedStruct1->poolId == poolId && blkSize <= poppedStruct1->blkSize &&
        0 == strcmp(strZone, poppedStruct1->name)) {
        app_debug("%s-%s-%d-%s block to got %p poolID %u fd %lu name %s\n", PLLOG_fileName(__FILE__), __func__, __LINE__,
                  name, poppedStruct1, poppedStruct1->poolId, poppedStruct1->memFd, strZone);

        if (bisGetIOVA) {
            *pIOVA = poppedStruct1->pIOVA;
        }

        *memFd = poppedStruct1->memFd;
        ret = ES_SUCCESS;
    } else {
        app_error("%s-%s-%d-%s error block to got\n", PLLOG_fileName(__FILE__), __func__, __LINE__, name);
        ret = -1;
    }
    return ret;
}

ES_S32 PL_ES_VB_ReleaseBlock(ES_U64 memFd) {
    ES_S32 ret = ES_SUCCESS;

    plVBstr *poppedStruct1 = manager.release(memFd);
    if (nullptr != poppedStruct1) {
        app_debug("\n PL_ES_VB_ReleaseBlock memfd %lu\n", memFd);
        return 0;
    } else {
        app_error("%s-%s-%d release  %lu poolid %u  \n", PLLOG_fileName(__FILE__), __func__, __LINE__, memFd,
                  poppedStruct1->poolId);
        ret = -1;
    }
    return ret;
}

ES_S32 PL_ES_VB_DestroyPool(VB_POOL poolId) { return ES_VB_DestroyPool(poolId); }
