#define PL_LOG_ID PL_LOG_VDEC
#include "vdecElement.h"

#include <sys/prctl.h>
#include <yaml-cpp/yaml.h>
extern "C" {
#include "./common/pl_option_dec.h"
#include "es_vdec.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
}
int VdecElement::m_ThreadGroupCount = -1;
bool VdecElement::gInitialized = false;
DEC_Client_S VdecElement::gDecClient = {0};

#define MAX_WIDTH 3840
#define MAX_HEIGHT 2160
#define MAX_BLK_CNT_EACH_POOL (10 * 1024)

static atomic<uint64_t> gVdecGetCount = 0;
static atomic<uint64_t> gVdecReleaseCount = 0;
std::map<int64_t, int64_t> releaseCountMap;
static std::map<int, DEC_CHN_S *> gChnVdecElement;

static PerformanceStatic *iovaReleasePerformance =
    new PerformanceStatic("iovaRelease_Performance", PERF_STATIC_SEGMENT);

static PerformanceStatic *vdecFrameSdkReleasePerformance =
    new PerformanceStatic("sdkVdecRelease_Performance", PERF_STATIC_SEGMENT);

void CImageVd::release() {
    app_debug(
        "----------------------mGrpId:%d mVdChn:%d mPic fd:%llx, count : %lld "
        "---------\n",
        mGrpId, mVdChn, mPic->videoFrame.fd, releaseCountMap[mGrpId]);

    if (misIOVA) {
        // iovaReleasePerformance->performanceStaticStart();
        ES_VB_FreeIOVA(VB_UID_HAE, mPic->videoFrame.fd);
        // iovaReleasePerformance->performanceStaticEnd();
    }

    vdecFrameSdkReleasePerformance->performanceStaticStart();
    ES_VDEC_ReleaseFrame(mGrpId, mVdChn, mPic);
    vdecFrameSdkReleasePerformance->performanceStaticEnd();

    releaseCountMap[mGrpId]++;
    gVdecReleaseCount++;
    free(mPic);
    mPic = nullptr;
    if (pool) {
        pool->deallocate(this);
    } else {
        delete this;
    }
}

static ES_VOID setDefaultParams(DEC_CHN_S *pParam) {
    if (pParam) {
        /* parser related*/
        pParam->multiple = 0;
        for (ES_S32 i = 0; i < MAX_CHN_NUM; i++) {
            pParam->pStreamcfg[i] = ES_NULL;
        }

        pParam->grpId = -1;
        pParam->srcCircleNum = 1;
        pParam->srcSendRate = 30;
        pParam->type = PT_BUTT;
        pParam->width = 0;
        pParam->height = 0;
        pParam->align = 1;
        memset(pParam->inputFile, 0, MAX_FILE_NAME_LEN);
        memset(pParam->outputFile, 0, MAX_FILE_NAME_LEN);
        memset(pParam->pixelFormat, PIXEL_FORMAT_BUTT, sizeof(pParam->pixelFormat));
        pParam->effectNumber = 0;
        pParam->firstPic = 0;
        pParam->lastPic = UINT32_MAX;
        pParam->userPicInstant = -1;
        pParam->assignOutputBufSize = 0;

        /* for vdec */
        pParam->frameBufCnt = 0;
        pParam->videoMode = VIDEO_MODE_BUTT;
        pParam->outputChn[0] = ES_FALSE;
        pParam->outputChn[1] = ES_FALSE;
        pParam->notDisplay = -1;
        pParam->videoDecMode = VIDEO_DEC_MODE_BUTT;
        pParam->outputOrder = VIDEO_OUTPUT_ORDER_BUTT;
        pParam->displayFrameNum = 0;
        pParam->refFrameNum = 0;
        pParam->alpha = 255;
        pParam->milliSec = -1;
        pParam->ptsInit = 0;
        pParam->ptsIncrease = 0;
        pParam->bCircleSend = ES_FALSE;
        pParam->minBufSize = -1;
        pParam->bGetSEIData = ES_FALSE;
        pParam->colorGamut = COLOR_GAMUT_BT709;
        pParam->cropParam[0].bEnable = ES_FALSE;
        pParam->cropParam[0].rect.x = 0;
        pParam->cropParam[0].rect.y = 0;
        pParam->cropParam[0].rect.width = 0;
        pParam->cropParam[0].rect.height = 0;
        pParam->scaleParam[0].bEnable = ES_FALSE;
        pParam->scaleParam[0].scaleWidth = 0;
        pParam->scaleParam[0].scaleHeight = 0;
        pParam->cropParam[1].bEnable = ES_FALSE;
        pParam->cropParam[1].rect.x = 0;
        pParam->cropParam[1].rect.y = 0;
        pParam->cropParam[1].rect.width = 0;
        pParam->cropParam[1].rect.height = 0;
        pParam->scaleParam[1].bEnable = ES_FALSE;
        pParam->scaleParam[1].scaleWidth = 0;
        pParam->scaleParam[1].scaleHeight = 0;
    }
}

void otherGrpIdGetFrame(int grpId, ES_S32 &getCountNewGrpId) {
    VDEC_GRP_STATUS_S status;
    ES_S32 ret;
    int newGrpID = grpId;
    if (newGrpID >= gChnVdecElement.size()) {
        return;
    }
    DEC_CHN_S *pClientVdecParam = gChnVdecElement[newGrpID];
    VdecElement *pVdecElement = (VdecElement *)gChnVdecElement[newGrpID]->element;
    pVdecElement->getFramePerformance->performanceStaticStart();
    ret = ES_VDEC_QueryStatus(newGrpID, &status);
    ASSERT(ES_SUCCESS == ret);
    if (0 == status.leftPics) {
        if ((0 == status.leftStreamBytes) && (0 == status.bStartRecvStream) &&
            (!pClientVdecParam->sendData.bThreadStart)) {
            printfVdecGrpStatus(newGrpID, status);
            return;
        }
    }
    CFrameMeta *fMeta = pVdecElement->fmetaPool->allocate();
    fMeta->pool = pVdecElement->fmetaPool;
    fMeta->dieIndex = pVdecElement->m_dieIndex;
    fMeta->source = pVdecElement->mName;
    for (ES_S32 i = 0; i < ES_VDEC_OUT_CHN_NUM; i++) {
        if (pClientVdecParam->outputChn[i]) {
            VIDEO_FRAME_INFO_S *frame = (VIDEO_FRAME_INFO_S *)malloc(sizeof(VIDEO_FRAME_INFO_S));
            if (0 == i) {
                ret = ES_VDEC_GetFrame(newGrpID, i, frame, 10 /*pClientVdecParam->milliSec*/);
            } else if (1 == i) {
                ret = ES_VDEC_GetFrame(newGrpID, i, frame, -1);
            }

            if (ES_SUCCESS == ret) {
                app_debug("%s grp:%d, chn:%d getCount:%d\n", __FUNCTION__, newGrpID, i, getCountNewGrpId);
                gVdecGetCount++;
                fMeta->pts = frame->videoFrame.PTS;
                CImageVd *img = pVdecElement->cimagePool->allocate();
                img->setparam(newGrpID, i, frame);
                img->pool = (MetaPool<CImage> *)pVdecElement->cimagePool;
                fMeta->images.push_back(img);
            } else {
                free(frame);
                break;
            }
        }
    }

    if (ret != ES_SUCCESS) {
        fMeta->reduceUseCount();
        return;
    }

    if (pVdecElement->isIpc) {
        gIpcDecCnt.ipcDecodedCount[gIpcDecCnt.ipcCnt] += 1;
    }
    fMeta->padIndex = pVdecElement->mPadIndex;

    fMeta->srcTime = esclock();
    fMeta->index = getCountNewGrpId++;

    ret = pVdecElement->TransMitToNextToProcess((CBaseMeta *)fMeta);
    if (ret != APP_SUCCESS) {
        app_error("vdec TransMitToNextToProcess error!\n");
    }
    pVdecElement->getFramePerformance->performanceStaticEnd();
    return;
}

void releaseOtherGrpId(int newGrpID, ES_S32 &getCountNewGrpId) {
    if (newGrpID < gChnVdecElement.size()) {
        VdecElement *pVdecElement = (VdecElement *)gChnVdecElement[newGrpID]->element;

        CFrameMeta *fMeta = pVdecElement->fmetaPool->allocate();
        fMeta->pool = pVdecElement->fmetaPool;
        fMeta->source = pVdecElement->mName;
        fMeta->padIndex = pVdecElement->mPadIndex;
        fMeta->dieIndex = pVdecElement->m_dieIndex;
        fMeta->eosFlag = true;
        fMeta->index = getCountNewGrpId++;
        fMeta->srcTime = esclock();
        pVdecElement->TransMitToNextToProcess((CBaseMeta *)fMeta);
    }
}

static ES_VOID *plStartGetFrame(void *pArgs) {
    DEC_CHN_S *pClientVdecParam = (DEC_CHN_S *)pArgs;
    VdecElement *pVdecElement = (VdecElement *)pClientVdecParam->element;

    if (!pVdecElement->m_cpuSetFlag) {
        // cpu_set_t cpuset;
        // CPU_ZERO(&cpuset);
        // CPU_SET(pVdecElement->m_cpuID, &cpuset);
        // pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        set_thread_affinity(pVdecElement->m_dieIndex);
        pVdecElement->m_cpuSetFlag = true;
    }
    pVdecElement->getCpuNumaID(__func__);

    VDEC_GRP_STATUS_S status;
    ES_S32 ret;
    ES_S32 grpId = pClientVdecParam->grpId;
    ES_S32 getCount = 0;
    // ES_S32 getCountNewGrpId = 0;
    // ES_S32 getCountNewGrpId2 = 0;
    // ES_S32 getCountNewGrpId3 = 0;
    // ES_S32 getCountNewGrpId4 = 0;

    std::vector<ES_S32> getCountNewGrpId(100, 0);
    if (pVdecElement->m_ThreadGroupCount > 0) {
        getCountNewGrpId.resize(pVdecElement->m_ThreadGroupCount);
        if (0 != grpId % pVdecElement->m_ThreadGroupCount) {
            return (ES_VOID *)ES_SUCCESS;
        }
    }

    string threadName = pVdecElement->mName + "_get_frame";
    prctl(PR_SET_NAME, (unsigned long)(threadName.c_str()));

    app_info("%s start, grpid:%d.\n", __FUNCTION__, grpId);

    // ES_S32 maxChn = pClientVdecParam->outputChn[1] ? 2 : 1;

    PerformanceStatic *decoderDumpCost =
        new PerformanceStatic(pVdecElement->mName + "_decoderDumpCost", PERF_STATIC_SEGMENT);
    PerformanceStatic *decoderTransmitCost =
        new PerformanceStatic(pVdecElement->mName + "_decoderTransmitCost", PERF_STATIC_SEGMENT);

    while (1) {
        pVdecElement->getFramePerformance->performanceStaticStart();
        ret = ES_VDEC_QueryStatus(grpId, &status);
        ASSERT(ES_SUCCESS == ret);
        if (0 == status.leftPics) {
            // app_warn("%s xxxxxxxxxxxxxxxxx\n", pVdecElement->mName.c_str());
            if ((0 == status.leftStreamBytes) && (0 == status.bStartRecvStream) &&
                (!pClientVdecParam->sendData.bThreadStart)) {
                printfVdecGrpStatus(grpId, status);
                break;
            }
            //            else {
            //                es_usleep(10 * 1000);
            //                continue;
            //            }
        }
        CFrameMeta *fMeta = pVdecElement->fmetaPool->allocate();
        fMeta->pool = pVdecElement->fmetaPool;
        fMeta->dieIndex = pVdecElement->m_dieIndex;
        fMeta->source = pVdecElement->mName;
        for (ES_S32 i = 0; i < ES_VDEC_OUT_CHN_NUM; i++) {
            // send stream mode will take long time, increase waiting time to
            // avoid normal wait.
            if (pClientVdecParam->outputChn[i]) {
                VIDEO_FRAME_INFO_S *frame = (VIDEO_FRAME_INFO_S *)malloc(sizeof(VIDEO_FRAME_INFO_S));
                if (0 == i) {
                    ret = ES_VDEC_GetFrame(grpId, i, frame, 10 /*pClientVdecParam->milliSec*/);
                } else if (1 == i) {
                    ret = ES_VDEC_GetFrame(grpId, i, frame, -1);
                }

                if (ES_SUCCESS == ret) {
                    {
                        if (1 == i) {
                            // ES_VOID *pIOVA = ES_NULL;
                            // ret = ES_VB_AllocIOVA(frame->videoFrame.fd, VB_UID_HAE, &pIOVA);
                            // frame->videoFrame.supplement.haeIOVA = (ES_U64)pIOVA;
                        }
                    }
                    app_debug("%s grp:%d, chn:%d getCount:%d\n", __FUNCTION__, grpId, i, getCount);
                    gVdecGetCount++;
                    fMeta->pts = frame->videoFrame.PTS;
                    CImageVd *img = pVdecElement->cimagePool->allocate();
                    img->setparam(grpId, i, frame);
                    img->pool = (MetaPool<CImage> *)pVdecElement->cimagePool;
                    if (1 == i) {
                        img->misIOVA = true;
                    } else {
                        img->misIOVA = false;
                    }
                    fMeta->images.push_back(img);
                    // ret = ES_VDEC_ReleaseFrame(grpId, i, frame);
                } else {
                    // app_info("%s no frame got now,try again
                    // \n",pVdecElement->mName.c_str());
                    free(frame);
                    break;
                }
            }
        }

        if (ret != ES_SUCCESS) {
            // delete fMeta;
            fMeta->reduceUseCount();
            // app_warn("%s tttttttttttttttttttttttttttt\n",
            // pVdecElement->mName.c_str()); app_warn("%s , the leftpics and
            // leftStream is %d, %d\n", pVdecElement->mName.c_str(),
            // status.leftPics, status.leftStreamBytes);
            continue;
        }

        if (pVdecElement->isIpc) {
            // fMeta->padIndex = pVdecElement->mPadIndex;
            gIpcDecCnt.ipcDecodedCount[gIpcDecCnt.ipcCnt] += 1;
        }
        fMeta->padIndex = pVdecElement->mPadIndex;

        fMeta->srcTime = esclock();
        fMeta->index = getCount++;
        // dump file
        for (int c = 0; c < ES_VDEC_OUT_CHN_NUM; c++) {
            if (pVdecElement->dumpFp[c] != NULL) {
                decoderDumpCost->performanceStaticStart();
                VIDEO_FRAME_INFO_S *pic = fMeta->images[c]->mPic;
                int stride[3] = {0};
                for (int i = 0; i < 3; i++) {
                    stride[i] = pic->videoFrame.stride[i];
                }
                int height = pic->videoFrame.height;
                int size = 0;
                if (pic->videoFrame.pixelFormat == PIXEL_FORMAT_NV12) {
                    size = stride[0] * height + stride[1] * height / 2;
                } else {
                    size = stride[0] * height;
                }

                ES_U64 *pVirAddr = ES_NULL;
                pVirAddr = (ES_U64 *)ES_SYS_Mmap(pic->videoFrame.fd, size, SYS_CACHE_MODE_NOCACHE);
                fwrite((void *)pVirAddr, 1, size, pVdecElement->dumpFp[c]);
                fflush(pVdecElement->dumpFp[c]);
                ES_SYS_Munmap(pVirAddr, size);
                decoderDumpCost->performanceStaticEnd();
            }
        }
        decoderTransmitCost->performanceStaticStart();
        app_ret ret = pVdecElement->TransMitToNextToProcess((CBaseMeta *)fMeta);
        if (ret != APP_SUCCESS) {
            app_error("vdec TransMitToNextToProcess error!\n");
            // todo
        }
        decoderTransmitCost->performanceStaticEnd();
        pVdecElement->getFramePerformance->performanceStaticEnd();

        /**************ES_S32 getCount = 0;*/
        {
            if (pVdecElement->m_ThreadGroupCount > 0) {
                for (size_t tempIndex = 1; tempIndex < pVdecElement->m_ThreadGroupCount; tempIndex++) {
                    otherGrpIdGetFrame(grpId + tempIndex, getCountNewGrpId[tempIndex]);
                }
            }

            // otherGrpIdGetFrame(grpId+1, getCountNewGrpId);
            // otherGrpIdGetFrame(grpId+2, getCountNewGrpId2);
            // otherGrpIdGetFrame(grpId+3, getCountNewGrpId3);
            // otherGrpIdGetFrame(grpId+4, getCountNewGrpId4);
            // int newGrpID = grpId+1;
            // if(newGrpID >= gChnVdecElement.size())
            // {
            //     continue;
            // }
            // VdecElement* pVdecElement = (VdecElement*)gChnVdecElement[newGrpID]->element;
            // pVdecElement->getFramePerformance->performanceStaticStart();
            // ret = ES_VDEC_QueryStatus(newGrpID, &status);
            // ASSERT(ES_SUCCESS == ret);
            // if (0 == status.leftPics) {
            //     if ((0 == status.leftStreamBytes) && (0 == status.bStartRecvStream) &&
            //     (!pClientVdecParam->sendData.bThreadStart)) {
            //         printfVdecGrpStatus(newGrpID, status);
            //         break;
            //     }
            // }
            // CFrameMeta* fMeta = pVdecElement->fmetaPool->allocate();
            // fMeta->pool = pVdecElement->fmetaPool;
            // fMeta->dieIndex = pVdecElement->m_dieIndex;
            // for (ES_S32 i = 0; i < ES_VDEC_OUT_CHN_NUM; i++) {
            //     if (pClientVdecParam->outputChn[i]) {
            //         VIDEO_FRAME_INFO_S* frame = (VIDEO_FRAME_INFO_S*)malloc(sizeof(VIDEO_FRAME_INFO_S));
            //         if (0 == i) {
            //             ret = ES_VDEC_GetFrame(newGrpID, i, frame, 10 /*pClientVdecParam->milliSec*/);
            //         } else if (1 == i) {
            //             ret = ES_VDEC_GetFrame(newGrpID, i, frame, -1);
            //         }

            //         if (ES_SUCCESS == ret) {
            //             app_debug("%s grp:%d, chn:%d getCount:%d\n", __FUNCTION__, newGrpID, i, getCountNewGrpId);
            //             gVdecGetCount++;
            //             fMeta->pts = frame->videoFrame.PTS;
            //             CImageVd* img = pVdecElement->cimagePool->allocate();
            //             img->setparam(newGrpID, i, frame);
            //             img->pool = (MetaPool<CImage>*)pVdecElement->cimagePool;
            //             fMeta->images.push_back(img);
            //         } else {
            //             free(frame);
            //             break;
            //         }
            //     }
            // }

            // if (ret != ES_SUCCESS) {
            //     fMeta->reduceUseCount();
            //     continue;
            // }

            // if (pVdecElement->isIpc) {
            //     gIpcDecCnt.ipcDecodedCount[gIpcDecCnt.ipcCnt] += 1;
            // }
            // fMeta->padIndex = pVdecElement->mPadIndex;

            // fMeta->srcTime = esclock();
            // fMeta->index = getCountNewGrpId++;

            // app_ret ret = pVdecElement->TransMitToNextToProcess((CBaseMeta*)fMeta);
            // if (ret != APP_SUCCESS) {
            //     app_error("vdec TransMitToNextToProcess error!\n");
            // }
            // pVdecElement->getFramePerformance->performanceStaticEnd();
        }
    }

    decoderDumpCost->performanceStaticReport();
    decoderTransmitCost->performanceStaticReport();
    delete decoderDumpCost;
    delete decoderTransmitCost;
    // end of stream
    CFrameMeta *fMeta = pVdecElement->fmetaPool->allocate();
    fMeta->pool = pVdecElement->fmetaPool;
    fMeta->source = pVdecElement->mName;
    fMeta->padIndex = pVdecElement->mPadIndex;
    fMeta->dieIndex = pVdecElement->m_dieIndex;
    fMeta->eosFlag = true;
    fMeta->index = getCount++;
    fMeta->srcTime = esclock();
    ret = pVdecElement->TransMitToNextToProcess((CBaseMeta *)fMeta);
    if (ret != APP_SUCCESS) {
        app_error("vdec TransMitToNextToProcess error!\n");
        // todo
    }

    {
        if (pVdecElement->m_ThreadGroupCount > 0) {
            for (size_t tempIndex = 1; tempIndex < pVdecElement->m_ThreadGroupCount; tempIndex++) {
                releaseOtherGrpId(grpId + tempIndex, getCountNewGrpId[tempIndex]);
            }
        }
        // releaseOtherGrpId(grpId+1, getCountNewGrpId);
        // releaseOtherGrpId(grpId+2, getCountNewGrpId2);
        // releaseOtherGrpId(grpId+3, getCountNewGrpId3);
        // releaseOtherGrpId(grpId+4, getCountNewGrpId4);
    }

    app_warn("%s end, grpId:%d\n", __FUNCTION__, grpId);

    return (ES_VOID *)ES_SUCCESS;
}

app_ret VdecElement::Init() {
    if (m_NextElementVec.size() != 1) {
        return APP_FAILURE;
    }

    // init client
    memset(&mDecClient, 0, sizeof(DEC_Client_S));
    mDecClient.groupNum = 1;
    // mDecClient.modParam.vdecVBSource = VB_SOURCE_MODULE;
    mDecClient.modParam.vdecVBSource = VB_SOURCE_USER;
    mDecClient.pMultiChn[0] = (DEC_CHN_S *)calloc(1, sizeof(DEC_CHN_S));
    setDefaultParams(mDecClient.pMultiChn[0]);
    DEC_CHN_S *pMultiChn = mDecClient.pMultiChn[0];
    //  pMultiChn->width = MAX_WIDTH;
    //  pMultiChn->height = MAX_HEIGHT;
    pMultiChn->multiple = 1;
    pMultiChn->nDieID = m_dieIndex;
    strcpy(pMultiChn->vbName, m_dieIndex == 0 ? "mmz_nid_0_part_0" : "mmz_nid_1_part_0");
    // pMultiChn->vbName = m_dieIndex == 0? "mmz_nid_0_part_0":"mmz_nid_1_part_0";
    pMultiChn->grpId = gDecClient.groupNum;
    pMultiChn->element = (void *)this;
    pMultiChn->userPicPoolId = ES_VB_INVALID_POOLID;
    releaseCountMap.insert(std::map<int64_t, int64_t>::value_type(gDecClient.groupNum, 0));

    // updata gDecClient
    gDecClient.pMultiChn[gDecClient.groupNum] = mDecClient.pMultiChn[0];
    gDecClient.groupNum += 1;
    // gDecClient.modParam.vdecVBSource = VB_SOURCE_MODULE;
    gDecClient.modParam.vdecVBSource = VB_SOURCE_USER;

    YAML::Node config = YAML::LoadFile(m_configFile);
    // param
    YAML::Node param = config["param"];
    int die_id = param["die-id"].template as<int>();
    int align = param["align"].template as<int>();
    if (align > 0) {
        pMultiChn->align = align;
    }
    app_debug("die_id: %d, align: %d\n", die_id, align);

    if (param["poolsize"].IsDefined()) {
        pMultiChn->displayFrameNum = param["poolsize"].template as<int>();
    } else {
        pMultiChn->displayFrameNum = 10;
    }

    // output pp0
    YAML::Node output = config["output"];
    YAML::Node picture0 = output["picture-0"];
    pMultiChn->outputChn[0] = ES_TRUE;
    string pp0_format = picture0["video-format"].template as<string>();
    if (pp0_format == "nv12") {
        pMultiChn->pixelFormat[0] = PIXEL_FORMAT_NV12;
    } else if (pp0_format == "rgb") {
        pMultiChn->pixelFormat[0] = PIXEL_FORMAT_R8G8B8;
    } else if (pp0_format == "bgra") {
        pMultiChn->pixelFormat[0] = PIXEL_FORMAT_B8G8R8A8;
    } else if (pp0_format == "bgr") {
        pMultiChn->pixelFormat[0] = PIXEL_FORMAT_B8G8R8;
    }

    YAML::Node scale = picture0["scale"];
    if (scale.IsDefined()) {
        pMultiChn->scaleParam[0].bEnable = ES_TRUE;
        pMultiChn->scaleParam[0].scaleWidth = scale[0].template as<int>();
        pMultiChn->scaleParam[0].scaleHeight = scale[1].template as<int>();
    } else {
        pMultiChn->scaleParam[0].bEnable = ES_FALSE;
    }

    YAML::Node crop = picture0["crop"];
    if (crop.IsDefined()) {
        pMultiChn->cropParam[0].bEnable = ES_TRUE;
        pMultiChn->cropParam[0].rect.x = crop[0].template as<int>();
        pMultiChn->cropParam[0].rect.y = crop[1].template as<int>();
        pMultiChn->cropParam[0].rect.width = crop[2].template as<int>();
        pMultiChn->cropParam[0].rect.height = crop[3].template as<int>();
    } else {
        pMultiChn->cropParam[0].bEnable = ES_FALSE;
    }
    // output pp1
    YAML::Node picture1 = output["picture-1"];
    if (picture1.IsDefined()) {
        pMultiChn->outputChn[1] = ES_TRUE;
        string pp1_format = picture1["video-format"].template as<string>();
        if (pp1_format == "nv12") {
            pMultiChn->pixelFormat[1] = PIXEL_FORMAT_NV12;
        } else if (pp1_format == "yuy2") {
            pMultiChn->pixelFormat[1] = PIXEL_FORMAT_YUY2;
        } else if (pp1_format == "rgb") {
            app_error("pp1 dont surpport rgb\n");
        } else if (pp1_format == "rgba") {
            app_error("pp1 dont surpport rgb\n");
        }
        YAML::Node scale = picture1["scale"];
        if (scale.IsDefined()) {
            pMultiChn->scaleParam[1].bEnable = ES_TRUE;
            pMultiChn->scaleParam[1].scaleWidth = scale[0].template as<int>();
            pMultiChn->scaleParam[1].scaleHeight = scale[1].template as<int>();
        } else {
            pMultiChn->scaleParam[1].bEnable = ES_FALSE;
        }

        YAML::Node crop = picture1["crop"];
        if (crop.IsDefined()) {
            pMultiChn->cropParam[1].bEnable = ES_TRUE;
            pMultiChn->cropParam[1].rect.x = crop[0].template as<int>();
            pMultiChn->cropParam[1].rect.y = crop[1].template as<int>();
            pMultiChn->cropParam[1].rect.width = crop[2].template as<int>();
            pMultiChn->cropParam[1].rect.height = crop[3].template as<int>();
        } else {
            pMultiChn->cropParam[1].bEnable = ES_FALSE;
        }
        pMultiChn->colorGamut = COLOR_GAMUT_BT709;
    }

    // dump
    YAML::Node debug = config["dump"];
    bool dumpFlag = debug["enable"].template as<bool>();
    if (dumpFlag) {
        for (int c = 0; c < ES_VDEC_OUT_CHN_NUM; c++) {
            if (pMultiChn->outputChn[c]) {
                string tmp = mName + "_pp_" + to_string(c) + "_dump.raw";
                dumpFp[c] = fopen(tmp.c_str(), "wb");
            } else {
                dumpFp[c] = NULL;
            }
        }
    } else {
        for (int c = 0; c < ES_VDEC_OUT_CHN_NUM; c++) {
            dumpFp[c] = NULL;
        }
    }

    if (config["threadGroupCount"]) {
        m_ThreadGroupCount = config["threadGroupCount"].template as<int>();
    }

    // todo

    // validateAndCreateChns_ext(&mDecClient);
    // setDefaultDecodeParameter(mDecClient.pMultiChn[0]);
    mDecExisted = ES_FALSE;
    mElementType = VIDEO_DECODER;

    VideoStreamInfo info;
    m_PreviousElementVec[0]->InfoQuery((void *)(&info), VIDEO_STREAM_INFO, PREV_ELEMENT, -1, this);
    for (int i = 0; i < gDecClient.groupNum; i++) {
        validateAndCreateChns_ext(gDecClient.pMultiChn[i], info.type, info.width, info.height);
    }
    // performance statics
    getFramePerformance = new PerformanceStatic(mName + "_getFramePerformance", PERF_STATIC_SEGMENT);

    //
    fmetaPool = new MetaPool<CFrameMeta>(50, m_dieIndex);
    cimagePool = new MetaPool<CImageVd>(50, m_dieIndex);

    return APP_SUCCESS;
}

app_ret VdecElement::Start() {
    if (!gInitialized) {
        ES_BOOL mDecExisted = (gDecClient.groupNum > 0) ? ES_TRUE : ES_FALSE;
        ES_S32 ret = ES_SUCCESS;
        if (mDecExisted) {
            /* update startGrpId. */
            auto &sharedCounter = SharedCounter::sharedCounter();
            if (sharedCounter.lockSharedMemory() == -1) {
                app_error("VDEC Start get shared mem lock failed!\n");
                return APP_FAILURE;
            }

            try {
                grpIdOffset = sharedCounter.vdecCounterGet();
                gDecClient.grpIdOffset = grpIdOffset;
                /* init module VB or user VB. */
                ret = COMM_VDEC_InitVBPool(&gDecClient);
                if (ret != ES_SUCCESS) {
                    app_error("COMM_VDEC_InitVBPool failed!\n");
                    return APP_FAILURE;
                }
                /* start vdec. */
                ret = COMM_VDEC_Start(&gDecClient);
                if (ret != ES_SUCCESS) {
                    app_error("COMM_VDEC_Start failed!\n");
                    return APP_FAILURE;
                }

                for (int i = 0; i < gDecClient.groupNum; i++) {
                    sharedCounter.vdecCounterIncrement();
                }
            } catch (...) {
                sharedCounter.unlockSharedMemory();
                app_error("VDEC elements try to start failed \n");
                throw;
            }
            sharedCounter.unlockSharedMemory();
        }
        gInitialized = true;
        gIpcDecCnt.totalVideoGrpCnt = gDecClient.groupNum;
    }

    mDecExisted = mDecClient.groupNum > 0 ? ES_TRUE : ES_FALSE;
    DEC_CHN_S *pChn = mDecClient.pMultiChn[0];
    if (mDecExisted) {
        /* Create decoding get frame threads. */
        // COMM_VDEC_StartGetFrame(&mDecClient);
        pChn->getData.bThreadStart = ES_TRUE;

        {
            gChnVdecElement.insert(std::map<int, DEC_CHN_S *>::value_type(pChn->grpId, pChn));
        }

        pthread_create(&pChn->getData.getDataPid, 0, plStartGetFrame, (void *)pChn);
        app_info("vdec startGetFrame thread for grp[%d] created\n", pChn->grpId);
    }

    return APP_SUCCESS;
}

app_ret VdecElement::Wait() {
    if (mDecExisted) {
        bool bForce = false;
        /* Waiting all the decoding send stream threads exit. */
        for (ES_U32 i = 0; i < mDecClient.groupNum; i++) {
            DEC_CHN_S *pChn = mDecClient.pMultiChn[i];
            if (0 != pChn->sendData.bThreadStart) {
                if (bForce) {
                    pChn->sendData.bThreadStart = ES_FALSE;
                    pthread_join(pChn->sendData.sendDataPid, ES_NULL);
                } else {
                    // waiting sendStream complete
                    pthread_join(pChn->sendData.sendDataPid, ES_NULL);
                    pChn->sendData.bThreadStart = ES_FALSE;
                }
                pChn->sendData.sendDataPid = 0;
            }
        }

        // while(1)
        // {
        //     if(gIpcDecCnt.endframecnt < gIpcDecCnt.totalVideoGrpCnt)
        //     {
        //         usleep(1000);
        //     }
        //     else
        //     {
        //         break;
        //     }
        // }

        /* Waiting all the decoding get frame threads exit. */
        for (ES_U32 i = 0; i < mDecClient.groupNum; i++) {
            DEC_CHN_S *pChn = mDecClient.pMultiChn[i];
            if (pChn->getData.bThreadStart) {
                if (bForce) {
                    pChn->getData.bThreadStart = ES_FALSE;
                    pthread_join(pChn->getData.getDataPid, ES_NULL);
                } else {
                    pthread_join(pChn->getData.getDataPid, ES_NULL);
                    pChn->getData.bThreadStart = ES_FALSE;
                }
                pChn->getData.getDataPid = 0;
            }
        }
    }

    return APP_SUCCESS;
}

app_ret VdecElement::ProcessData(CBaseMeta *baseMeta, CElement const *previousElement) {
    app_info("%s-%s-%d-%s in\n", PLLOG_fileName(__FILE__), __func__, __LINE__, mName.c_str());
    app_ret retVal = APP_SUCCESS;
    ES_S32 ret = ES_SUCCESS;
    CVideoPacketMeta *videoPacketMeta = (CVideoPacketMeta *)baseMeta;
    DEC_CHN_S *pChn = mDecClient.pMultiChn[0];

    VDEC_STREAM_S *stream = videoPacketMeta->videoPkt;
    if (0 == pChn->notDisplay && (PT_H264 == pChn->type || PT_H265 == pChn->type)) {
        stream->bDisplay = ES_FALSE;
    } else {
        stream->bDisplay = ES_TRUE;
    }
    isIpc = videoPacketMeta->isIpc;
    mPadIndex = videoPacketMeta->padIndex;

    ES_BOOL bEndOfStream = stream->bEndOfStream;
SendAgain:
    ret = ES_VDEC_SendStream(pChn->grpId, stream, pChn->milliSec);
    if (ES_SUCCESS != ret) {
        app_error("%s: ES_VDEC_SendStream failed, chn:%d, err:0x%x!\n", __FUNCTION__, pChn->grpId, ret);
        if (pChn->sendData.bThreadStart) {
            goto SendAgain;
        }
    } else {
        app_info("ES_VDEC_SendStream success, chn:%d.\n", pChn->grpId);
    }

    if (bEndOfStream) {
        ret = ES_VDEC_StopRecvStream(pChn->grpId);
        app_warn("%s: StopRecvStream vechn[%d] end with %#x!\n", __FUNCTION__, pChn->grpId, ret);
        pChn->sendData.bThreadStart = ES_FALSE;

        gIpcDecCnt.eosflag[pChn->grpId] = 1;
    }
    videoPacketMeta->reduceUseCount();

    app_info("%s-%s-%d-%s out\n", PLLOG_fileName(__FILE__), __func__, __LINE__, mName.c_str());

    return retVal;
}

app_ret VdecElement::Finish() {
    if (gInitialized) {
        if (mDecExisted) {
            auto &sharedCounter = SharedCounter::sharedCounter();
            if (sharedCounter.lockSharedMemory() == -1) {
                app_error("VDEC Start get shared mem lock failed!\n");
                return APP_FAILURE;
            }

            for (ES_S32 i = 0; i < gDecClient.groupNum; i++) {
                ES_S32 ret = ES_VDEC_DestroyGrp(i + grpIdOffset);
                try {
                    sharedCounter.vdecCounterDecrement();
                } catch (...) {
                    sharedCounter.unlockSharedMemory();
                    app_error("VDEC elements try to start failed \n");
                    throw;
                }
                if (ES_SUCCESS != ret) {
                    app_error("ES_VDEC_DestroyGrp failed!! ret: 0x%x \n", ret);
                }
            }
            sharedCounter.unlockSharedMemory();
            ES_VDEC_Deinit();
        }
        if (mDecExisted) {
            COMM_VDEC_ExitVBPool(&gDecClient);
        }
        gInitialized = false;
    }
    delete getFramePerformance;

    for (int c = 0; c < ES_VDEC_OUT_CHN_NUM; c++) {
        if (dumpFp[c] != NULL) {
            fclose(dumpFp[c]);
        }
    }

    delete fmetaPool;
    delete cimagePool;
    freeNumaNode(this, sizeof(VdecElement));
    return APP_SUCCESS;
}

app_ret VdecElement::ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *self) {
    if (!mRunningFlag) {
        sem_wait(&mStartFlag);
        mRunningFlag = true;
    }
    ProcessData(baseMeta, this);
    return APP_SUCCESS;
}

app_ret VdecElement::perfStat() {
    getFramePerformance->performanceStaticReport();
    iovaReleasePerformance->performanceStaticReport();
    vdecFrameSdkReleasePerformance->performanceStaticReport();
    return APP_SUCCESS;
}

extern "C" CElement *createEsVdecElement(const char *name, const char *path, int dieIndex) {
    return new (bindNumaNode(dieIndex, sizeof(VdecElement))) VdecElement(name, path, dieIndex);
}
