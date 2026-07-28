#define PL_LOG_ID PL_LOG_VENC
#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

#include <pthread.h>

#include "es_venc.h"
#include "log.h"
#include "pl_encwrapper.h"

#define ASSIGN_VUI_VALUE(VUI, pChnInfo)                                                             \
    do {                                                                                            \
        VUI.VUIAspectRatio.sarWidth = pChnInfo->protocol.VUIInfo.sarWidth;                          \
        VUI.VUIAspectRatio.sarHeight = pChnInfo->protocol.VUIInfo.sarHeight;                        \
        VUI.VUIVideoSignal.videoSignalTypePresentFlag = pChnInfo->protocol.VUIInfo.videoSingalFlag; \
        VUI.VUITimeInfo.timingInfoPresentFlag = pChnInfo->protocol.VUIInfo.timingInfoFlag;          \
        VUI.VUIVideoSignal.videoFormat = pChnInfo->protocol.VUIInfo.videoFormat;                    \
        VUI.VUIVideoSignal.videoFullRangeFlag = pChnInfo->protocol.VUIInfo.videoRange;              \
    } while (0)

#define SET_RC_PARAMS(pParam, pChnInfo)                                              \
    do {                                                                             \
        if (strlen(pChnInfo->qpMapFile)) {                                           \
            if (pChnInfo->qpMapBlockUnit >= 0) {                                     \
                pParam->qpMapMeta.qpMapBlockUnit = pChnInfo->qpMapBlockUnit;         \
            }                                                                        \
            if (strlen(pChnInfo->skipMapFile)) {                                     \
                pParam->qpMapMeta.metaType = QPMAP_META_TYPE_SKIP;                   \
                pParam->qpMapMeta.bMetaEnable = ES_TRUE;                             \
                if (pChnInfo->skipMapBlockUnit >= 0) {                               \
                    pParam->qpMapMeta.metaMapBlockUnit = pChnInfo->skipMapBlockUnit; \
                }                                                                    \
            } else if (strlen(pChnInfo->ipcmMapFile)) {                              \
                pParam->qpMapMeta.bMetaEnable = ES_TRUE;                             \
                pParam->qpMapMeta.metaType = QPMAP_META_TYPE_IPCM;                   \
                pParam->qpMapMeta.metaMapBlockUnit = 2;                              \
            }                                                                        \
        }                                                                            \
    } while (0)

// return the position of the string, -1 means not find.
ES_S32 stringFindLastOf(const ES_CHAR* pString, const ES_CHAR tok) {
    if (pString == ES_NULL) {
        return -1;
    }
    ES_S32 len = strlen(pString);
    ES_S32 i;

    if (len <= 0) {
        return -1;
    }

    for (i = len - 1; i >= 0; i--) {
        if (pString[i] == tok) {
            return i;
        }
    }
    return -1;
}

/******************************************************************************
 * funciton : get file postfix according palyload_type.
 ******************************************************************************/
ES_S32 getFilePostfix(PAYLOAD_TYPE_E payload, char* szFilePostfix) {
    if (PT_H264 == payload) {
        strcpy(szFilePostfix, ".h264");
    } else if (PT_H265 == payload) {
        strcpy(szFilePostfix, ".h265");
    } else if (PT_JPEG == payload) {
        strcpy(szFilePostfix, ".jpg");
    } else if (PT_MJPEG == payload) {
        strcpy(szFilePostfix, ".mjp");
    } else {
        ASSERT(0);
    }
    return ES_SUCCESS;
}

/******************************************************************************
 * funciton : save stream
 ******************************************************************************/
ES_S32 saveStream(FILE* pFd, VENC_STREAM_S* pStream) {
    app_debug("saveStream packCount:%u, seq:%u! \n", pStream->packCount, pStream->seq);
    for (ES_U32 i = 0; i < pStream->packCount; i++) {
        ES_U64 dataSize = pStream->pPack[i].len - pStream->pPack[i].offset;
        ES_U64* pVirAddr = (ES_U64*)(pStream->pPack[i].pAddr + pStream->pPack[i].offset);
        if (ES_NULL == pFd) {
            app_error("%s \n", "FILESINK_SendStream: illegal params");
            return ES_FALSE;
        }

        if (dataSize > 0) {
            fwrite(pVirAddr, 1, dataSize, pFd);
            fflush(pFd);
        }
    }

    return ES_SUCCESS;
}

ES_S32 saveStreamJpeg(FILE* pFd, VENC_STREAM_S* pStream, char* filename) {
    app_debug("saveStream packCount:%u, seq:%u! \n", pStream->packCount, pStream->seq);
    for (ES_U32 i = 0; i < pStream->packCount; i++) {
        ES_U64 dataSize = pStream->pPack[i].len - pStream->pPack[i].offset;
        ES_U64* pVirAddr = (ES_U64*)(pStream->pPack[i].pAddr + pStream->pPack[i].offset);
        if (dataSize > 0) {
            FILE* fd = fopen(filename, "wb");
            fwrite(pVirAddr, 1, dataSize, fd);
            fflush(fd);
            fclose(fd);
        }
    }

    return ES_SUCCESS;
}

/******************************************************************************
 * funciton : Start venc stream mode
 * note      : rate control parameter need adjust, according your case.
 ******************************************************************************/
ES_S32 initAndStart(const TEST_Client_S_ENC* pEncClient) {
    ES_S32 ret;
    VENC_PARAM_MOD_S modParam = {0};

    ret = ES_VENC_Init();
    if (ES_SUCCESS != ret) {
        app_error("VENC_Init faild with %#x! \n", ret);
        return ES_FAILURE;
    }

    ES_BOOL isH264Set = ES_FALSE;
    ES_BOOL isH265Set = ES_FALSE;
    ES_BOOL isJpegSet = ES_FALSE;
    for (ES_U32 i = pEncClient->startGrpId; i < pEncClient->startGrpId + pEncClient->groupNum; i++) {
        TEST_CHN_S_ENC* pChnInfo = pEncClient->pMultiChn[i];
        if (pChnInfo != NULL) {
            if (PT_H264 == pChnInfo->type && isH264Set) {
                continue;
            } else if (PT_H265 == pChnInfo->type && isH265Set) {
                continue;
            } else if (PT_JPEG == pChnInfo->type && isJpegSet) {
                continue;
            }
        } else {
            app_error("pChnInfo is NULL! i = %d, startGrpId = %d, groupNum = %d \n", i, pEncClient->startGrpId,
                      pEncClient->groupNum);
            continue;
        }
        modParam.vencModType =
            (PT_H264 == pChnInfo->type) ? MODTYPE_H264E : ((PT_H265 == pChnInfo->type) ? MODTYPE_H265E : MODTYPE_JPEGE);

        ret = ES_VENC_GetModParam(&modParam);
        if (ES_SUCCESS != ret) {
            app_error("VENC_GetModParam faild with %#x! \n", ret);
            return ES_FAILURE;
        }
        // hisi default 0, eswin defalt 1, because 0 will do copy in essdk
        if (PT_H264 == pChnInfo->type) {
            modParam.h264eModParam.oneStreamBuffer = pChnInfo->oneStreamBuffer;
            modParam.h264eModParam.bQPHstgrmEn = ES_TRUE;
            ASSERT(pChnInfo->vbSource == DEFAULT);
            isH264Set = ES_TRUE;
        } else if (PT_H265 == pChnInfo->type) {
            modParam.h265eModParam.oneStreamBuffer = pChnInfo->oneStreamBuffer;
            modParam.h265eModParam.bQPHstgrmEn = ES_TRUE;
            ASSERT(pChnInfo->vbSource == DEFAULT);
            isH265Set = ES_TRUE;
        } else if (PT_JPEG == pChnInfo->type || PT_MJPEG == pChnInfo->type) {
            // modParam.jpegeModParam.oneStreamBuffer = pChnInfo->oneStreamBuffer;
            isJpegSet = ES_TRUE;
        }
        ret = ES_VENC_SetModParam(&modParam);
        if (ES_SUCCESS != ret) {
            app_error("VENC_SetModParam faild with %#x! \n", ret);
            return ES_FAILURE;
        }
        app_debug("VENC_SetModParam OneStream:%d \n", pChnInfo->oneStreamBuffer);
    }

    return ES_SUCCESS;
}

ES_S32 startChn(const TEST_Client_S_ENC* pEncClient, ES_S32 i) {
    ES_S32 ret;
    TEST_CHN_S_ENC* pChnInfo = pEncClient->pMultiChn[i];
    /******************************************
     step 1:  Creat Encode Channel
    ******************************************/
    VENC_CHN_ATTR_S attr = {0};
    getChnAttrs(pChnInfo, &attr);
    ret = ES_VENC_CreateChn(pChnInfo->grpId, 0, &attr);
    if (ES_SUCCESS != ret) {
        app_error("createChannel faild with %#x! \n", ret);
        return ES_FAILURE;
    }

    VENC_CHN_PARAM_S chnParam = {0};
    getChnParams(pChnInfo, &chnParam);
    ret = ES_VENC_SetChnParam(pChnInfo->grpId, &chnParam);
    if (ES_SUCCESS != ret) {
        app_error("Set chn param fail %#x! \n", ret);
        return ES_FAILURE;
    }

    /*************************************************************************
     step 2:  Set parameters after channel created, before encoding started.
    **************************************************************************/
    if (PT_H264 == pChnInfo->type || PT_H265 == pChnInfo->type) {
        // intra refresh
        VENC_INTRA_REFRESH_S intraRefresh = {0};
        ret = ES_VENC_GetIntraRefresh(pChnInfo->grpId, &intraRefresh);
        app_debug("%s GetIntraRefresh ret:0x%x: enable:%d, mode:%d, refreshnum:%d, reqIQP:%d \n", __FUNCTION__, ret,
                  intraRefresh.bRefreshEnable, intraRefresh.intraRefreshMode, intraRefresh.refreshNum,
                  intraRefresh.reqIQP);

        memcpy(&intraRefresh, &pChnInfo->protocol.intraRefresh, sizeof(VENC_INTRA_REFRESH_S));
        ret = ES_VENC_SetIntraRefresh(pChnInfo->grpId, &intraRefresh);
        app_debug("%s SetIntraRefresh ret:0x%x: enable:%d, mode:%d, refreshnum:%d, reqIQP:%d \n", __FUNCTION__, ret,
                  intraRefresh.bRefreshEnable, intraRefresh.intraRefreshMode, intraRefresh.refreshNum,
                  intraRefresh.reqIQP);

        // rc param
        if (strlen(pChnInfo->qpMapFile)) {
            VENC_RC_PARAM_S rcParam = {0};
            ES_VENC_GetRcParam(pChnInfo->grpId, &rcParam);
            switch (pChnInfo->rcAttr.rcMode) {
                case VENC_RC_MODE_H264CBR: {
                    VENC_PARAM_H264_CBR_S* pParam = &rcParam.paramH264CBR;
                    pParam->bQPMapEn = ES_TRUE;
                    SET_RC_PARAMS(pParam, pChnInfo);
                } break;
                case VENC_RC_MODE_H264VBR: {
                    VENC_PARAM_H264_VBR_S* pParam = &rcParam.paramH264VBR;
                    pParam->bQPMapEn = ES_TRUE;
                    SET_RC_PARAMS(pParam, pChnInfo);
                } break;
                case VENC_RC_MODE_H264AVBR: {
                    VENC_PARAM_H264_AVBR_S* pParam = &rcParam.paramH264AVBR;
                    pParam->bQPMapEn = ES_TRUE;
                    SET_RC_PARAMS(pParam, pChnInfo);
                } break;
                case VENC_RC_MODE_H264QVBR: {
                    VENC_PARAM_H264_QVBR_S* pParam = &rcParam.paramH264QVBR;
                    pParam->bQPMapEn = ES_TRUE;
                    SET_RC_PARAMS(pParam, pChnInfo);
                } break;
                case VENC_RC_MODE_H264CVBR: {
                    VENC_PARAM_H264_CVBR_S* pParam = &rcParam.paramH264CVBR;
                    pParam->bQPMapEn = ES_TRUE;
                    SET_RC_PARAMS(pParam, pChnInfo);
                } break;
                case VENC_RC_MODE_H264QPMAP: {
                    VENC_PARAM_H264_QPMAP_S* pParam = &rcParam.paramH264QPMap;
                    SET_RC_PARAMS(pParam, pChnInfo);
                } break;
                case VENC_RC_MODE_H265CBR: {
                    VENC_PARAM_H265_CBR_S* pParam = &rcParam.paramH265CBR;
                    pParam->bQPMapEn = ES_TRUE;
                    SET_RC_PARAMS(pParam, pChnInfo);
                } break;
                case VENC_RC_MODE_H265VBR: {
                    VENC_PARAM_H265_VBR_S* pParam = &rcParam.paramH265VBR;
                    pParam->bQPMapEn = ES_TRUE;
                    SET_RC_PARAMS(pParam, pChnInfo);
                } break;
                case VENC_RC_MODE_H265AVBR: {
                    VENC_PARAM_H265_AVBR_S* pParam = &rcParam.paramH265AVBR;
                    pParam->bQPMapEn = ES_TRUE;
                    SET_RC_PARAMS(pParam, pChnInfo);
                } break;
                case VENC_RC_MODE_H265QVBR: {
                    VENC_PARAM_H265_QVBR_S* pParam = &rcParam.paramH265QVBR;
                    pParam->bQPMapEn = ES_TRUE;
                    SET_RC_PARAMS(pParam, pChnInfo);
                } break;
                case VENC_RC_MODE_H265CVBR: {
                    VENC_PARAM_H265_CVBR_S* pParam = &rcParam.paramH265CVBR;
                    pParam->bQPMapEn = ES_TRUE;
                    SET_RC_PARAMS(pParam, pChnInfo);
                } break;
                case VENC_RC_MODE_H265QPMAP: {
                    VENC_PARAM_H265_QPMAP_S* pParam = &rcParam.paramH265QPMap;
                    SET_RC_PARAMS(pParam, pChnInfo);
                } break;
                default:
                    return ES_FAILURE;
            }
            ES_VENC_SetRcParam(pChnInfo->grpId, &rcParam);
        }
    }

    if (PT_H264 == pChnInfo->type) {
        // vui
        VENC_H264_VUI_S vuiParam = {0};
        ES_VENC_GetH264VUI(pChnInfo->grpId, &vuiParam);
        ASSIGN_VUI_VALUE(vuiParam, pChnInfo);
        ES_VENC_SetH264VUI(pChnInfo->grpId, &vuiParam);

        // trans
        VENC_H264_TRANS_S transParam = {0};
        ES_VENC_GetH264Trans(pChnInfo->grpId, &transParam);
        transParam.chromaQPIndexOffset = pChnInfo->protocol.transInfo.qpOffset;
        ES_VENC_SetH264Trans(pChnInfo->grpId, &transParam);

        // entropy
        VENC_H264_ENTROPY_S entropy = {0};
        ES_VENC_GetH264Entropy(pChnInfo->grpId, &entropy);
        entropy.entropyEncMode = pChnInfo->protocol.entropyInfo.enableCabac;
        ES_VENC_SetH264Entropy(pChnInfo->grpId, &entropy);

        // dblk
        VENC_H264_DBLK_S dblk = {0};
        ES_VENC_GetH264Dblk(pChnInfo->grpId, &dblk);
        dblk.disDeblkFilterIdc = pChnInfo->protocol.dblkInfo.disableDeblockingFlag;
        dblk.sliceAlphaOffset = pChnInfo->protocol.dblkInfo.tcOffset;
        dblk.sliceBetaOffset = pChnInfo->protocol.dblkInfo.betaOffset;
        ES_VENC_SetH264Dblk(pChnInfo->grpId, &dblk);
    } else if (PT_H265 == pChnInfo->type) {
        // vui
        VENC_H265_VUI_S vuiParam = {0};
        ES_VENC_GetH265VUI(pChnInfo->grpId, &vuiParam);
        ASSIGN_VUI_VALUE(vuiParam, pChnInfo);
        ES_VENC_SetH265VUI(pChnInfo->grpId, &vuiParam);

        // trans
        VENC_H265_TRANS_S transParam = {0};
        ES_VENC_GetH265Trans(pChnInfo->grpId, &transParam);
        transParam.bScalingListEnabled = pChnInfo->protocol.transInfo.enableScalingList;
        transParam.cbQPOffset = pChnInfo->protocol.transInfo.qpOffset;
        transParam.crQPOffset = pChnInfo->protocol.transInfo.qpOffset;
        ES_VENC_SetH265Trans(pChnInfo->grpId, &transParam);

        // SAO
        VENC_H265_SAO_S SAO = {0};
        ES_VENC_GetH265SAO(pChnInfo->grpId, &SAO);
        SAO.sliceSAOLumaFlag = pChnInfo->protocol.SAOEnabledFlag;
        SAO.sliceSAOChromaFlag = pChnInfo->protocol.SAOEnabledFlag;  // chroma_flag is set for hisi
        ES_VENC_SetH265SAO(pChnInfo->grpId, &SAO);

        // predUnit
        VENC_H265_PU_S pu = {0};
        ES_VENC_GetH265PredUnit(pChnInfo->grpId, &pu);
        pu.strongIntraSmoothingEnabledFlag = pChnInfo->protocol.smoothingIntra;
        ES_VENC_SetH265PredUnit(pChnInfo->grpId, &pu);

        // dblk
        VENC_H265_DBLK_S dblk = {0};
        ES_VENC_GetH265Dblk(pChnInfo->grpId, &dblk);
        dblk.sliceDeblkFilterDisabledFlag = pChnInfo->protocol.dblkInfo.disableDeblockingFlag;
        dblk.sliceTcOffset = pChnInfo->protocol.dblkInfo.tcOffset;
        dblk.sliceBetaOffset = pChnInfo->protocol.dblkInfo.betaOffset;
        ES_VENC_SetH265Dblk(pChnInfo->grpId, &dblk);
    } else if (PT_JPEG == pChnInfo->type) {
        if (pChnInfo->JPEGParam.enableQt || pChnInfo->JPEGParam.MCUPerECS != 0 || pChnInfo->JPEGParam.qFactor != 0) {
            VENC_JPEG_PARAM_S jpegParam = {0};
            ES_VENC_GetJpegParam(pChnInfo->grpId, &jpegParam);
            if (pChnInfo->JPEGParam.qFactor >= 1 && pChnInfo->JPEGParam.qFactor <= 99) {
                jpegParam.qFactor = pChnInfo->JPEGParam.qFactor;
            }
            if (pChnInfo->JPEGParam.MCUPerECS != 0) {
                jpegParam.MCUPerECS = pChnInfo->JPEGParam.MCUPerECS;
            }
            ES_VENC_SetJpegParam(pChnInfo->grpId, &jpegParam);
        }
    }
    /******************************************
     step 3:  Start Recv Venc Pictures
    ******************************************/
    VENC_RECV_PIC_PARAM_S recvParam;
    recvParam.recvPicNum = -1;
    ret = ES_VENC_StartRecvFrame(pChnInfo->grpId, &recvParam);
    app_debug("Start receive frame for chn[%d] \n", pChnInfo->grpId);
    if (ES_SUCCESS != ret) {
        app_error("ES_VENC_StartRecvPic faild with%#x! \n", ret);
        return ES_FAILURE;
    }
    return ES_SUCCESS;
}

/******************************************************************************
 * funciton : Stop venc ( stream mode -- H264, MJPEG )
 ******************************************************************************/
ES_S32 destroyChns(ES_S32 chnId) {
    /******************************************
     Distroy Venc Channel
    ******************************************/
    return ES_VENC_DestroyChn(chnId);
}

void strReplace(char* str1, char* str2, char* str3) {
    int i, j, k, done, count = 0, gap = 0;
    char temp[100];
    for (i = 0; i < strlen(str1); i += gap) {
        if (str1[i] == str2[0]) {
            done = 0;
            for (j = i, k = 0; k < strlen(str2); j++, k++) {
                if (str1[j] != str2[k]) {
                    done = 1;
                    gap = k;
                    break;
                }
            }
            if (done == 0) {                                                     // 已找到待替换字符串并替换
                for (j = i + strlen(str2), k = 0; j < strlen(str1); j++, k++) {  // 保存原字符串中剩余的字符
                    temp[k] = str1[j];
                }
                temp[k] = '\0';                                   // 将字符数组变成字符串
                for (j = i, k = 0; k < strlen(str3); j++, k++) {  // 字符串替换
                    str1[j] = str3[k];
                    count++;
                }
                for (k = 0; k < strlen(temp); j++, k++) {  // 剩余字符串回接
                    str1[j] = temp[k];
                }
                str1[j] = '\0';  // 将字符数组变成字符串
                gap = strlen(str2);
            }
        } else {
            gap = 1;
        }
    }
    if (count == 0) {
        app_debug("%s\n", "Can't find the replaced string! ");
    }
    return;
}

/******************************************************************************
 * funciton : stop get venc stream process.
 ******************************************************************************/
ES_S32 stopGetStream(const TEST_Client_S_ENC* pEncClient, ES_S32 chnId) {
    TEST_CHN_S_ENC* pChn = pEncClient->pMultiChn[chnId];
    pthread_join(pChn->getData.getDataPid, 0);
    pChn->getData.getDataPid = 0;
    return ES_SUCCESS;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
