#ifndef __PL_OPTION_ENC_H__
#define __PL_OPTION_ENC_H__

#include "pl_comm_enc.h"

ES_VOID deinitOptions(TEST_Client_S_ENC *pEncClient, ES_S32 chnId);
PIXEL_FORMAT_E convertPixelFmt(const ES_CHAR *pValue);
ES_S32 validateAndCreateChns(TEST_CHN_S_ENC *pChnParams, TEST_Client_S_ENC *pEncClient);
ES_VOID setDefaultParams(TEST_CHN_S_ENC *pChnParams);

static ES_BOOL validateAndCorrectCommand(TEST_CHN_S_ENC *pChnParams, TEST_CODEC_TYPE_E_ENC codecType);
static ES_VOID setDefaultEncodeParameter(TEST_CHN_S_ENC *pChnParams);
static ES_VOID setDefaultCommonParameter(TEST_CHN_S_ENC *pChnParams);
static ES_VOID setDefaultCodecParameter(TEST_CHN_S_ENC *pChnParams, TEST_CODEC_TYPE_E_ENC codecType);

#endif