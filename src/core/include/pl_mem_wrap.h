#ifndef _ESSDK_PL_MEM_WRAP_H__
#define _ESSDK_PL_MEM_WRAP_H__
#include <semaphore.h>

#include "es_vb_memory.h"
#include "log.h"

ES_S32 PL_ES_VB_CreatePool(VB_POOL_CONFIG_S *pstVbPoolCfg, VB_POOL *poolId, bool bisGetIOVA = false);
ES_S32 PL_ES_VB_GetBlock(VB_POOL poolId, ES_U64 blkSize, const ES_CHAR *strZone, ES_U64 *memFd, const char *name = "",
                         bool bisGetIOVA = false, ES_VOID **pIOVA = NULL);
ES_S32 PL_ES_VB_ReleaseBlock(ES_U64 memFd);
ES_S32 PL_ES_VB_DestroyPool(VB_POOL poolId);

#endif