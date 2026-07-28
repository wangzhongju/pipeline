#ifndef __PL_ENC_WRAPPER_H__
#define __PL_ENC_WRAPPER_H__

#include "pl_comm_enc.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

ES_VOID getChnAttrs(const TEST_CHN_S_ENC *pChnInfo, VENC_CHN_ATTR_S *pVencChnAttr);
ES_VOID getChnParams(const TEST_CHN_S_ENC *pChn, VENC_CHN_PARAM_S *pChnParam);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif
